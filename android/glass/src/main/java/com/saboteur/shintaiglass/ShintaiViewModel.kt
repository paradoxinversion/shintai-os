package com.saboteur.shintaiglass

import android.app.Application
import android.content.Context
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.lifecycle.AndroidViewModel
import com.saboteur.shintai.core.ConnectionState
import com.saboteur.shintai.core.Role
import com.saboteur.shintai.core.ShintaiBleClient
import com.saboteur.shintai.core.ShintaiGatt
import com.saboteur.shintai.core.ShintaiReadings
import com.saboteur.shintai.core.Units
import com.saboteur.shintai.core.fold
import com.saboteur.shintai.core.foldBinary
import com.saboteur.shintai.core.mergeReadings
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import java.util.UUID

/**
 * Owns the BLE client and folds each notification into a single [ShintaiReadings]
 * snapshot the HUD observes. The parse lives in `:core` ([fold]) so Glass and
 * Operator can never disagree on how a payload becomes state; this view model
 * only adds the glass-specific concerns — the hardcoded MACs, IPD nudge, and units.
 *
 * Bunshin: the glasses federate two pods (fwd/aft) exactly like the Operator, but
 * leaner — two *hardcoded* MACs (the RayNeo radio can't scan), one client + snapshot
 * per pod, merged with the contract-DEFAULT precedence ([mergeReadings]). There is no
 * Sources override here; picking precedence is the Operator's job (CONTRACT.md BU-6).
 */
class ShintaiViewModel(app: Application) : AndroidViewModel(app) {

    private val _readings = MutableStateFlow(ShintaiReadings())
    val readings: StateFlow<ShintaiReadings> = _readings.asStateFlow()

    // One BLE client + one running snapshot PER pod; `readings` is the merged view.
    private val clients = mutableMapOf<Role, ShintaiBleClient>()
    private val podReadings = mutableMapOf<Role, ShintaiReadings>()

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

    /** Called once the BLUETOOTH_CONNECT permission is in hand. Idempotent. Brings up
     *  BOTH pods; a MAC that isn't present just fails gracefully to Disconnected. */
    fun connect() {
        if (clients.isNotEmpty()) return
        // The HUD renders eight string channels plus Metsuke's binary thermal grid.
        // It subscribes to ENVIRONMENT only to derive Kyūkaku's smell SPIKE badge —
        // it does not render the raw pressure/gas readout (that stays on the Operator).
        // See CONTRACT.md "Consumer coverage".
        POD_ADDRESSES.forEach { (role, address) ->
            clients[role] = ShintaiBleClient(getApplication(), address, GLASS_SUBSCRIPTIONS, listenerFor(role))
                .also { it.connect() }
            podReadings[role] = ShintaiReadings(connection = ConnectionState.Connecting)
        }
        remerge()
    }

    /** BLUETOOTH_CONNECT was denied — surface it so the UI can prompt a retry
     *  instead of sitting silently at [ConnectionState.Idle] forever. */
    fun onPermissionDenied() {
        _readings.update { it.copy(connection = ConnectionState.PermissionNeeded) }
    }

    /** One listener per pod — folds that pod's notifications into its own snapshot,
     *  then re-merges into the single HUD view. */
    private fun listenerFor(role: Role) = object : ShintaiBleClient.Listener {
        override fun onState(state: ConnectionState) {
            podReadings[role] = pod(role).copy(connection = state)
            remerge()
        }

        override fun onValue(uuid: UUID, value: String) {
            podReadings[role] = pod(role).fold(uuid, value)
            remerge()
        }

        override fun onBinary(uuid: UUID, value: ByteArray) {
            // Metsuke's thermal grid — the one binary channel. Parsed in :core.
            podReadings[role] = pod(role).foldBinary(uuid, value)
            remerge()
        }
    }

    private fun pod(role: Role): ShintaiReadings = podReadings[role] ?: ShintaiReadings()

    /** Recompute the merged HUD snapshot from the per-pod snapshots (default precedence). */
    private fun remerge() { _readings.value = mergeReadings(podReadings.toMap()) }

    override fun onCleared() {
        clients.values.forEach { it.close() }
        clients.clear()
    }

    companion object {
        /**
         * ▒▒▒ HARDCODE BOTH PODS' MACs HERE ▒▒▒
         * No scanning on the glasses (the RayNeo radio starves a scan), so these must
         * be the real static addresses of your two QT Pys. Find each once over USB
         * serial or a scanner app: the forward pod advertises as "ShintaiOS-fwd", the
         * aft pod as "ShintaiOS-aft" (the Operator app scans instead). A MAC that isn't
         * present just fails gracefully to Disconnected, so a single-pod rig works too.
         */
        const val FWD_ADDRESS = "68:EE:8F:6E:77:BD"
        const val AFT_ADDRESS = "68:EE:8F:00:00:00"   // ← set to your aft pod's MAC

        /** Bunshin: the two pods the HUD federates, by role. */
        private val POD_ADDRESSES = mapOf(Role.Fwd to FWD_ADDRESS, Role.Aft to AFT_ADDRESS)

        /** The channels the HUD renders: eight string readouts (Hokan's PDR
         *  breadcrumb included) plus Metsuke's binary THERMAL_GRID (the heat panel),
         *  and ENVIRONMENT — taken not for its raw readout (that's the Operator's) but
         *  to derive Kyūkaku's smell SPIKE badge from its gas_ohms. See CONTRACT.md
         *  "Consumer coverage". */
        private val GLASS_SUBSCRIPTIONS = listOf(
            ShintaiGatt.DISTANCE, ShintaiGatt.ALERT, ShintaiGatt.HEADING, ShintaiGatt.ACCEL,
            ShintaiGatt.GPS, ShintaiGatt.CLIMATE, ShintaiGatt.THERMAL, ShintaiGatt.HOKAN,
            ShintaiGatt.ENVIRONMENT, ShintaiGatt.THERMAL_GRID,
        )

        private const val KEY_IPD = "ipd_nudge"
        private const val KEY_IMPERIAL = "imperial"
        /** Max |IPD nudge| in dp — beyond this the readout clips at the pane edge. */
        private const val IPD_LIMIT = 120
    }
}
