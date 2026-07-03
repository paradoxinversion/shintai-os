package com.saboteur.shintaioperator

import android.annotation.SuppressLint
import android.bluetooth.BluetoothManager
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanResult
import android.content.Context
import android.util.Log

/** One advertising device the picker can offer. */
data class DeviceEntry(val address: String, val name: String?, val rssi: Int)

/**
 * A thin BLE-scan wrapper — the Operator's answer to "which board?". The glasses
 * never scan (their radio starves it, hence the hardcoded MAC in :glass); the
 * phone's radio is dependable, so here we scan, list what we find, and let the
 * operator pick. Results are deduped by address and reported newest-RSSI-wins.
 *
 * Caller guarantees BLUETOOTH_SCAN is granted before [start].
 */
@SuppressLint("MissingPermission")
class ShintaiScanner(private val context: Context) {

    private var callback: ScanCallback? = null
    private val found = LinkedHashMap<String, DeviceEntry>()

    /** Begin scanning; [onResults] is called with the full deduped list on each hit. */
    fun start(onResults: (List<DeviceEntry>) -> Unit) {
        val manager = context.getSystemService(Context.BLUETOOTH_SERVICE) as BluetoothManager
        val scanner = manager.adapter?.bluetoothLeScanner
        if (scanner == null) {
            Log.w(TAG, "no BLE scanner (adapter off?)")
            return
        }
        found.clear()
        val cb = object : ScanCallback() {
            override fun onScanResult(callbackType: Int, result: ScanResult) {
                val dev = result.device
                found[dev.address] = DeviceEntry(dev.address, dev.name, result.rssi)
                // ShintaiOS boards first, then by signal strength.
                onResults(found.values.sortedWith(
                    compareByDescending<DeviceEntry> { it.name == ADVERTISED_NAME }
                        .thenByDescending { it.rssi }
                ))
            }

            override fun onScanFailed(errorCode: Int) {
                Log.w(TAG, "scan failed: $errorCode")
            }
        }
        callback = cb
        // No ScanFilter: some ESP32 advertisements don't surface the 128-bit service
        // UUID in the ADV packet, so we take everything and rank ShintaiOS to the top.
        scanner.startScan(cb)
        Log.i(TAG, "scan started")
    }

    fun stop() {
        val manager = context.getSystemService(Context.BLUETOOTH_SERVICE) as BluetoothManager
        val scanner = manager.adapter?.bluetoothLeScanner
        callback?.let { scanner?.stopScan(it) }
        callback = null
        Log.i(TAG, "scan stopped")
    }

    companion object {
        private const val TAG = "ShintaiScan"
        /** The name the firmware advertises (shintai-os.ino). */
        const val ADVERTISED_NAME = "ShintaiOS"
    }
}
