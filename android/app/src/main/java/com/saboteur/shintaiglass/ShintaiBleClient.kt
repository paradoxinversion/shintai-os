package com.saboteur.shintaiglass

import android.annotation.SuppressLint
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothGattCharacteristic
import android.bluetooth.BluetoothGattDescriptor
import android.bluetooth.BluetoothManager
import android.bluetooth.BluetoothProfile
import android.content.Context
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.util.Log
import java.util.ArrayDeque
import java.util.UUID

/**
 * Connects to the Shintai-OS board by a HARDCODED MAC and streams its five
 * notify characteristics back to [listener]. No scanning anywhere — on the
 * RayNeo X3 Pro the glasses' own BLE traffic starves a scan, so we go straight
 * to [android.bluetooth.BluetoothAdapter.getRemoteDevice] + connectGatt with
 * autoConnect=true (the stack reconnects on its own when the board is in range).
 *
 * The one non-obvious BLE rule this class exists to honour: only ONE GATT
 * operation may be in flight at a time. Enabling notifications on five
 * characteristics therefore can't be fired in a loop — each CCCD write must
 * wait for the previous onDescriptorWrite. See [subscribeQueue]/[subscribeNext].
 */
@SuppressLint("MissingPermission") // caller guarantees BLUETOOTH_CONNECT is granted before connect()
class ShintaiBleClient(
    private val context: Context,
    private val deviceAddress: String,
    private val listener: Listener,
) {
    interface Listener {
        fun onState(state: ConnectionState)
        /** A characteristic fired; [uuid] identifies it, [value] is the UTF-8 payload. */
        fun onValue(uuid: UUID, value: String)
    }

    private val main = Handler(Looper.getMainLooper())
    private var gatt: BluetoothGatt? = null
    private var manualClose = false

    /** CCCDs still to be written, drained one-at-a-time as each write completes. */
    private val subscribeQueue = ArrayDeque<UUID>()

    fun connect() {
        val manager = context.getSystemService(Context.BLUETOOTH_SERVICE) as BluetoothManager
        val adapter = manager.adapter
        if (adapter == null || !adapter.isEnabled) {
            listener.onState(ConnectionState.Disconnected)
            Log.w(TAG, "Bluetooth adapter unavailable or disabled")
            return
        }
        manualClose = false
        val device = adapter.getRemoteDevice(deviceAddress) // by MAC, no scan
        listener.onState(ConnectionState.Connecting)
        Log.i(TAG, "connectGatt -> $deviceAddress (direct)")
        // autoConnect=false: a DIRECT connection attempt — fast and deterministic,
        // still no scanning. autoConnect=true does a slow indefinite background
        // connect that, on this radio, often never reports STATE_CONNECTED.
        gatt = device.connectGatt(
            context,
            /* autoConnect = */ false,
            callback,
            BluetoothDevice.TRANSPORT_LE,
        )
    }

    fun close() {
        manualClose = true
        gatt?.let {
            it.disconnect()
            it.close()
        }
        gatt = null
        subscribeQueue.clear()
    }

    /**
     * Invoke the hidden BluetoothGatt.refresh() via reflection to drop Android's
     * cached service/descriptor table for this device. Not public API, but stable
     * for years and the only way to recover from a stale cache that's missing CCCDs.
     */
    private fun refreshDeviceCache(g: BluetoothGatt): Boolean = try {
        (g.javaClass.getMethod("refresh").invoke(g) as? Boolean) ?: false
    } catch (e: Exception) {
        Log.w(TAG, "refresh() unavailable: $e")
        false
    }

    /** Tear down the dead GATT and fire a fresh direct connect after a short delay. */
    private fun reconnectSoon() {
        gatt?.close()
        gatt = null
        if (manualClose) return
        main.postDelayed({ if (!manualClose) connect() }, RETRY_MS)
    }

    private val callback = object : android.bluetooth.BluetoothGattCallback() {

        override fun onConnectionStateChange(g: BluetoothGatt, status: Int, newState: Int) {
            Log.i(TAG, "onConnectionStateChange status=$status newState=$newState")
            when (newState) {
                BluetoothProfile.STATE_CONNECTED -> {
                    listener.onState(ConnectionState.Discovering)
                    // Clear Android's cached GATT table first — a stale cache from an
                    // earlier firmware layout serves the characteristics WITHOUT their
                    // 0x2902 CCCDs, which silently kills all notifications. refresh()
                    // forces discoverServices() to re-read descriptors from the board.
                    val refreshed = refreshDeviceCache(g)
                    Log.i(TAG, "gatt cache refresh=$refreshed")
                    // Negotiate a bigger MTU before discovery: the default 23-byte
                    // ATT MTU caps notifications at 20 bytes, which truncates the
                    // thermal and full-fix GPS strings. Discovery waits for onMtuChanged.
                    main.postDelayed({
                        if (!g.requestMtu(247)) {
                            Log.w(TAG, "requestMtu failed; discovering at default MTU")
                            g.discoverServices()
                        }
                    }, 600)
                }
                BluetoothProfile.STATE_DISCONNECTED -> {
                    listener.onState(ConnectionState.Disconnected)
                    Log.w(TAG, "disconnected (status=$status) — retrying in ${RETRY_MS}ms")
                    reconnectSoon() // direct connect won't retry on its own
                }
            }
        }

        override fun onMtuChanged(g: BluetoothGatt, mtu: Int, status: Int) {
            Log.i(TAG, "MTU negotiated=$mtu status=$status — discovering")
            g.discoverServices()
        }

        override fun onServicesDiscovered(g: BluetoothGatt, status: Int) {
            val service = g.getService(ShintaiGatt.SERVICE)
            if (status != BluetoothGatt.GATT_SUCCESS || service == null) {
                Log.e(TAG, "service not found (status=$status)")
                listener.onState(ConnectionState.Disconnected)
                return
            }
            // Log the full characteristic/descriptor inventory once, so a capture
            // tells us immediately whether the CCCDs we expect are even present.
            // Queue every characteristic, then kick off the serialized subscribe.
            subscribeQueue.clear()
            subscribeQueue.addAll(ShintaiGatt.SUBSCRIPTIONS)
            subscribeNext(g)
        }

        override fun onDescriptorWrite(
            g: BluetoothGatt,
            descriptor: BluetoothGattDescriptor,
            status: Int,
        ) {
            // A non-zero status means this CCCD didn't actually enable notifications.
            if (status != BluetoothGatt.GATT_SUCCESS) {
                Log.w(TAG, "CCCD write failed for ${descriptor.characteristic.uuid} status=$status")
            }
            subscribeNext(g)
        }

        // API 33+ delivers the value directly.
        override fun onCharacteristicChanged(
            g: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic,
            value: ByteArray,
        ) = deliver(characteristic.uuid, value)

        // Pre-33 path: read the cached value off the characteristic.
        @Suppress("DEPRECATION")
        override fun onCharacteristicChanged(
            g: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic,
        ) {
            if (Build.VERSION.SDK_INT >= 33) return // 33+ uses the value overload above
            @Suppress("DEPRECATION")
            deliver(characteristic.uuid, characteristic.value ?: return)
        }
    }

    private fun deliver(uuid: UUID, bytes: ByteArray) {
        listener.onValue(uuid, bytes.decodeToString())
    }

    /** Pop the next characteristic and enable its notifications, or go Live when drained. */
    private fun subscribeNext(g: BluetoothGatt) {
        val uuid = subscribeQueue.poll()
        if (uuid == null) {
            listener.onState(ConnectionState.Live)
            Log.i(TAG, "subscribe queue drained")
            return
        }
        val service = g.getService(ShintaiGatt.SERVICE)
        val ch = service?.getCharacteristic(uuid)
        val cccd = ch?.getDescriptor(ShintaiGatt.CCCD)
        if (ch == null || cccd == null) {
            Log.w(TAG, "missing characteristic/CCCD for $uuid, skipping")
            subscribeNext(g) // skip and keep the queue moving
            return
        }
        g.setCharacteristicNotification(ch, true)
        val enable = BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
        if (Build.VERSION.SDK_INT >= 33) {
            g.writeDescriptor(cccd, enable)
        } else {
            @Suppress("DEPRECATION")
            cccd.value = enable
            @Suppress("DEPRECATION")
            g.writeDescriptor(cccd)
        }
    }

    companion object {
        private const val TAG = "ShintaiBle"
        private const val RETRY_MS = 2000L
    }
}
