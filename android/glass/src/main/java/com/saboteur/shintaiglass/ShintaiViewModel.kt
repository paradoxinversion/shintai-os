package com.saboteur.shintaiglass

import android.app.Application
import android.content.Context
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.lifecycle.AndroidViewModel
import com.saboteur.shintai.core.ConnectionState
import com.saboteur.shintai.core.ShintaiBleClient
import com.saboteur.shintai.core.ShintaiGatt
import com.saboteur.shintai.core.ShintaiReadings
import com.saboteur.shintai.core.Units
import com.saboteur.shintai.core.fold
import com.saboteur.shintai.core.foldBinary
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import java.util.UUID

/**
 * Owns the BLE client and folds each notification into a single [ShintaiReadings]
 * snapshot the HUD observes. The parse lives in `:core` ([fold]) so Glass and
 * Operator can never disagree on how a payload becomes state; this view model
 * only adds the glass-specific concerns — the hardcoded MAC, IPD nudge, and units.
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
        // The HUD renders seven string channels plus Metsuke's binary thermal
        // grid, and deliberately skips ENVIRONMENT (pressure/gas). See CONTRACT.md
        // "Consumer coverage".
        client = ShintaiBleClient(getApplication(), DEVICE_ADDRESS, GLASS_SUBSCRIPTIONS, listener)
            .also { it.connect() }
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
            _readings.update { it.fold(uuid, value) }
        }

        override fun onBinary(uuid: UUID, value: ByteArray) {
            // Metsuke's thermal grid — the one binary channel. Parsed in :core.
            _readings.update { it.foldBinary(uuid, value) }
        }
    }

    override fun onCleared() {
        client?.close()
        client = null
    }

    companion object {
        /**
         * ▒▒▒ HARDCODE THE BOARD'S MAC HERE ▒▒▒
         * No scanning on the glasses (the RayNeo radio starves a scan), so this
         * must be the real static address of your QT Py. Find it once over USB
         * serial, or from `bluetoothctl`/a scanner app: look for the device
         * advertising as "ShintaiOS". (The Operator app scans instead.)
         */
        const val DEVICE_ADDRESS = "68:EE:8F:6E:77:BD"

        /** The channels the HUD renders: seven string readouts plus Metsuke's
         *  binary THERMAL_GRID (the heat panel). Skips only ENVIRONMENT (BME688
         *  pressure + gas), which only the Operator shows. */
        private val GLASS_SUBSCRIPTIONS = listOf(
            ShintaiGatt.DISTANCE, ShintaiGatt.ALERT, ShintaiGatt.HEADING, ShintaiGatt.ACCEL,
            ShintaiGatt.GPS, ShintaiGatt.CLIMATE, ShintaiGatt.THERMAL, ShintaiGatt.THERMAL_GRID,
        )

        private const val KEY_IPD = "ipd_nudge"
        private const val KEY_IMPERIAL = "imperial"
        /** Max |IPD nudge| in dp — beyond this the readout clips at the pane edge. */
        private const val IPD_LIMIT = 120
    }
}
