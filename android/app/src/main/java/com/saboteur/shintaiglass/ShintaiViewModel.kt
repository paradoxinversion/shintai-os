package com.saboteur.shintaiglass

import android.app.Application
import android.content.Context
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.lifecycle.AndroidViewModel
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import java.util.UUID

/**
 * Owns the BLE client and folds each notification into a single [ShintaiReadings]
 * snapshot the UI observes. Parsing lives here, not in the BLE layer, so the
 * client stays a dumb transport.
 */
class ShintaiViewModel(app: Application) : AndroidViewModel(app) {

    private val _readings = MutableStateFlow(ShintaiReadings())
    val readings: StateFlow<ShintaiReadings> = _readings.asStateFlow()

    private var client: ShintaiBleClient? = null

    private val prefs = app.getSharedPreferences("shintai", Context.MODE_PRIVATE)

    /** Live IPD nudge in dp (stereo view only), persisted across launches. */
    var ipdNudge by mutableIntStateOf(prefs.getInt(KEY_IPD, 0))
        private set

    /** Display unit system, persisted. Defaults to imperial. */
    var units by mutableStateOf(
        if (prefs.getBoolean(KEY_IMPERIAL, true)) Units.IMPERIAL else Units.METRIC
    )
        private set

    /** Adjust the IPD nudge by [deltaDp], clamped, and persist it. */
    fun adjustIpd(deltaDp: Int) {
        ipdNudge = (ipdNudge + deltaDp).coerceIn(-IPD_LIMIT, IPD_LIMIT)
        prefs.edit().putInt(KEY_IPD, ipdNudge).apply()
    }

    /** Flip between metric and imperial, and persist. */
    fun toggleUnits() {
        units = if (units == Units.METRIC) Units.IMPERIAL else Units.METRIC
        prefs.edit().putBoolean(KEY_IMPERIAL, units == Units.IMPERIAL).apply()
    }

    /** Called once the BLUETOOTH_CONNECT permission is in hand. Idempotent. */
    fun connect() {
        if (client != null) return
        client = ShintaiBleClient(getApplication(), DEVICE_ADDRESS, listener).also { it.connect() }
    }

    /** BLUETOOTH_CONNECT was denied — surface it so the UI can prompt a retry
     *  instead of sitting silently at [ConnectionState.Idle] forever. */
    fun onPermissionDenied() {
        _readings.update { it.copy(connection = ConnectionState.PermissionNeeded) }
    }

    private val listener = object : ShintaiBleClient.Listener {
        override fun onState(state: ConnectionState) {
            _readings.update { it.copy(connection = state) }
        }

        override fun onValue(uuid: UUID, value: String) {
            _readings.update { prev ->
                val base = prev.copy(packets = prev.packets + 1) // heartbeat: every notification counts
                when (uuid) {
                    ShintaiGatt.DISTANCE -> {
                        val mm = Regex("""\d+""").find(value)?.value?.toIntOrNull()
                        base.copy(
                            distanceText = value,
                            distanceMm = mm,
                            // Mirror the firmware: the warning clears once we're back past 20 cm.
                            alertActive = if (mm != null && mm > NEAR_MM) false else base.alertActive,
                        )
                    }
                    // Alert is edge-triggered ("CLOSE") with no explicit clear, so we
                    // latch it here and let a far distance reading above clear it.
                    ShintaiGatt.ALERT -> base.copy(alertActive = true)
                    ShintaiGatt.HEADING -> base.copy(heading = value)
                    ShintaiGatt.ACCEL -> base.copy(accel = value)
                    ShintaiGatt.GPS -> base.copy(gps = value)
                    ShintaiGatt.CLIMATE -> base.copy(climate = value)
                    ShintaiGatt.THERMAL -> base.copy(thermal = value)
                    else -> base
                }
            }
        }
    }

    override fun onCleared() {
        client?.close()
        client = null
    }

    companion object {
        /**
         * ▒▒▒ HARDCODE THE BOARD'S MAC HERE ▒▒▒
         * No scanning, so this must be the real static address of your QT Py.
         * Find it once over USB serial, or from `bluetoothctl`/a scanner app:
         * look for the device advertising as "ShintaiOS".
         */
        const val DEVICE_ADDRESS = "68:EE:8F:6E:77:BD"

        /** Proximity-alert threshold, matching NEAR_MM in shintai-os.ino. */
        private const val NEAR_MM = 200

        private const val KEY_IPD = "ipd_nudge"
        private const val KEY_IMPERIAL = "imperial"
        /** Max |IPD nudge| in dp — beyond this the readout clips at the pane edge. */
        private const val IPD_LIMIT = 120
    }
}
