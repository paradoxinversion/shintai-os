package com.saboteur.shintaioperator

import android.app.Application
import android.content.Context
import android.os.Handler
import android.os.Looper
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.lifecycle.AndroidViewModel
import com.saboteur.shintai.core.Channel
import com.saboteur.shintai.core.ConnectionState
import com.saboteur.shintai.core.DEFAULT_PRECEDENCE
import com.saboteur.shintai.core.Precedence
import com.saboteur.shintai.core.Role
import com.saboteur.shintai.core.ShintaiBleClient
import com.saboteur.shintai.core.ShintaiGatt
import com.saboteur.shintai.core.ShintaiReadings
import com.saboteur.shintai.core.Units
import com.saboteur.shintai.core.fold
import com.saboteur.shintai.core.foldBinary
import com.saboteur.shintai.core.mergeReadings
import com.saboteur.shintai.core.suppliedChannels
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import java.util.ArrayDeque
import java.util.UUID

/** What the recording chip shows. */
data class RecordingUi(val active: Boolean = false, val rows: Int = 0, val fileName: String? = null)

/**
 * The Operator's brain. Beyond the shared fold ([ShintaiReadings.fold]), it owns the
 * phone-only capabilities the glasses can't hold: scan & pairing, CSV recording, the
 * full channel set (subscribes to [ShintaiGatt.ALL], including ENVIRONMENT), rolling
 * history for the trend sparklines, and — for Bunshin — running two pods at once and
 * the live per-channel source precedence. That breadth of orchestration is why the
 * function count runs past detekt's default (same call as OperatorScreen's panel stack).
 */
@Suppress("TooManyFunctions")
class OperatorViewModel(app: Application) : AndroidViewModel(app) {

    private val _readings = MutableStateFlow(ShintaiReadings())
    val readings: StateFlow<ShintaiReadings> = _readings.asStateFlow()

    private val _scan = MutableStateFlow<List<DeviceEntry>>(emptyList())
    val scan: StateFlow<List<DeviceEntry>> = _scan.asStateFlow()

    private val _scanning = MutableStateFlow(false)
    val scanning: StateFlow<Boolean> = _scanning.asStateFlow()

    private val _recording = MutableStateFlow(RecordingUi())
    val recording: StateFlow<RecordingUi> = _recording.asStateFlow()

    private val _log = MutableStateFlow(listOf("SHINTAI-OS // OPERATOR ONLINE"))
    val log: StateFlow<List<String>> = _log.asStateFlow()

    private val _distanceHistory = MutableStateFlow<List<Float>>(emptyList())
    val distanceHistory: StateFlow<List<Float>> = _distanceHistory.asStateFlow()

    private val _co2History = MutableStateFlow<List<Float>>(emptyList())
    val co2History: StateFlow<List<Float>> = _co2History.asStateFlow()

    private val prefs = app.getSharedPreferences("shintai_operator", Context.MODE_PRIVATE)

    // Bunshin: the live per-channel precedence (contract defaults + the user's overrides)
    // and which channels each connected pod currently supplies (drives the Sources screen).
    private val _precedence = MutableStateFlow(loadPrecedence())
    val precedence: StateFlow<Precedence> = _precedence.asStateFlow()

    private val _podSupply = MutableStateFlow<Map<Role, Set<Channel>>>(emptyMap())
    val podSupply: StateFlow<Map<Role, Set<Channel>>> = _podSupply.asStateFlow()

    // Bunshin: the pair screen is normally shown only while Idle, but with one pod
    // already linked the user needs to return to it to link the SECOND. This re-opens
    // it on demand (over the live console).
    private val _pairingOpen = MutableStateFlow(false)
    val pairingOpen: StateFlow<Boolean> = _pairingOpen.asStateFlow()

    /** Display unit system, persisted. Defaults to imperial (parity with Glass). */
    var units by mutableStateOf(
        if (prefs.getBoolean(KEY_IMPERIAL, true)) Units.IMPERIAL else Units.METRIC
    )
        private set

    /** Whether any pod is remembered — gates the one-tap "reconnect" fast path. */
    val hasLast: Boolean get() = Role.values().any { prefs.getString(roleAddrKey(it), null) != null }

    private val scanner = ShintaiScanner(app)

    // Bunshin: one BLE client + one running snapshot PER pod (fwd/aft). The public
    // `readings` flow is the MERGED view both the UI and the recorder consume.
    private val clients = mutableMapOf<Role, ShintaiBleClient>()
    private val podReadings = mutableMapOf<Role, ShintaiReadings>()

    private val main = Handler(Looper.getMainLooper())
    private val recorder = TelemetryRecorder(app.getExternalFilesDir(null) ?: app.filesDir)
    private val distances = ArrayDeque<Float>()
    private val co2s = ArrayDeque<Float>()

    fun toggleUnits() {
        units = if (units == Units.METRIC) Units.IMPERIAL else Units.METRIC
        prefs.edit().putBoolean(KEY_IMPERIAL, units == Units.IMPERIAL).apply()
    }

    // --- permissions -------------------------------------------------------

    /** Called with the runtime-permission outcome. Scan needs SCAN; connect needs
     *  CONNECT. Without CONNECT we can't do anything, so surface PermissionNeeded. */
    fun onPermissions(hasScan: Boolean, hasConnect: Boolean) {
        if (!hasConnect) {
            _readings.update { it.copy(connection = ConnectionState.PermissionNeeded) }
            return
        }
        if (hasScan) startScan() else reconnectLast()
    }

    // --- scanning ----------------------------------------------------------

    fun startScan() {
        if (_scanning.value) return
        _scan.value = emptyList()
        _scanning.value = true
        log("SCAN…")
        scanner.start { _scan.value = it }
        // Bounded scan — stop after a window so the radio isn't held indefinitely.
        main.postDelayed({ if (_scanning.value) stopScan() }, SCAN_MS)
    }

    fun stopScan() {
        if (!_scanning.value) return
        scanner.stop()
        _scanning.value = false
    }

    /** Re-open the pair screen to link an ADDITIONAL pod while one is already linked. */
    fun openPairing() {
        _pairingOpen.value = true
        startScan()
    }

    /** Leave the pair screen (back to the live console) without linking anything more. */
    fun closePairing() {
        stopScan()
        _pairingOpen.value = false
    }

    // --- connection --------------------------------------------------------

    /** Connect a discovered pod, deriving its role (fwd/aft) from the advertised name. */
    fun connect(entry: DeviceEntry) = connect(entry.address, roleOf(entry.name))

    /** Reconnect every remembered pod — the one-tap path when scan is unavailable. */
    fun reconnectLast() {
        Role.values().forEach { role ->
            prefs.getString(roleAddrKey(role), null)?.let { connect(it, role) }
        }
    }

    fun connect(address: String, role: Role) {
        stopScan()
        if (clients.containsKey(role)) return          // this pod is already linked
        prefs.edit().putString(roleAddrKey(role), address).apply()
        log("CONNECT ${role.tag} $address")
        // Operator takes EVERY string channel (ENVIRONMENT included) plus Metsuke's
        // binary thermal grid, on BOTH pods; the merge decides what each channel shows.
        clients[role] = ShintaiBleClient(
            getApplication(), address,
            ShintaiGatt.ALL + ShintaiGatt.THERMAL_GRID + ShintaiGatt.REAR_DEPTH_GRID, listenerFor(role),
        ).also { it.connect() }
        podReadings[role] = ShintaiReadings(connection = ConnectionState.Connecting)
        _pairingOpen.value = false       // linked → return to the console
        remerge()
    }

    fun disconnect() {
        if (recorder.active) stopRecording()
        clients.values.forEach { it.close() }
        clients.clear()
        podReadings.clear()
        _readings.value = ShintaiReadings(connection = ConnectionState.Idle)
        log("DISCONNECT")
    }

    // --- recording ---------------------------------------------------------

    fun toggleRecording() = if (recorder.active) stopRecording() else startRecording()

    private fun startRecording() {
        val name = recorder.start() ?: run { log("REC FAILED"); return }
        _recording.value = RecordingUi(active = true, rows = 0, fileName = name)
        log("REC ▶ $name")
        main.post(sampler)
    }

    private fun stopRecording() {
        main.removeCallbacks(sampler)
        val name = recorder.stop()
        _recording.value = RecordingUi(active = false, rows = recorder.rows, fileName = name)
        log("REC ■ ${recorder.rows} rows")
    }

    /** While recording, sample the latest snapshot at a steady cadence — decoupled
     *  from notification timing, exactly like the ground-station logger's rows. */
    private val sampler = object : Runnable {
        override fun run() {
            if (!recorder.active) return
            val now = System.currentTimeMillis()
            // One row PER pod, each tagged with its board — mirrors the firmware CSV so
            // the base-side merge can align the two streams. Single-source → one blank-board row.
            if (podReadings.isEmpty()) recorder.writeRow(_readings.value, null, now)
            else podReadings.forEach { (role, r) -> recorder.writeRow(r, role, now) }
            _recording.update { it.copy(rows = recorder.rows) }
            main.postDelayed(this, RECORD_MS)
        }
    }

    // --- BLE stream --------------------------------------------------------

    /** One listener per pod — folds that pod's notifications into its own snapshot,
     *  then re-merges. Sparklines track the MERGED value so the trend follows whichever
     *  pod currently wins the channel. */
    private fun listenerFor(role: Role) = object : ShintaiBleClient.Listener {
        override fun onState(state: ConnectionState) {
            podReadings[role] = pod(role).copy(connection = state)
            remerge()
            if (state == ConnectionState.Live) log("${role.tag} LIVE")
            if (state == ConnectionState.Disconnected) log("${role.tag} LOST")
        }

        override fun onValue(uuid: UUID, value: String) {
            podReadings[role] = pod(role).fold(uuid, value)
            remerge()
            val snap = _readings.value
            when (uuid) {
                ShintaiGatt.DISTANCE -> snap.distanceMm?.let { push(distances, it.toFloat(), _distanceHistory) }
                ShintaiGatt.CLIMATE -> co2Ppm(value)?.let { push(co2s, it.toFloat(), _co2History) }
            }
        }

        override fun onBinary(uuid: UUID, value: ByteArray) {
            // Metsuke's thermal grid — the one binary channel. Parsed in :core.
            podReadings[role] = pod(role).foldBinary(uuid, value)
            remerge()
        }
    }

    private fun pod(role: Role): ShintaiReadings = podReadings[role] ?: ShintaiReadings()

    /** Recompute the public merged snapshot + per-pod supply from the per-pod snapshots. */
    private fun remerge() {
        val pods = podReadings.toMap()
        _readings.value = mergeReadings(pods, _precedence.value)
        _podSupply.value = pods.mapValues { it.value.suppliedChannels() }
    }

    // --- Bunshin precedence (Sources screen) ------------------------------

    /** Make [role] the preferred (winning) pod for [channel] — persisted + applied live. */
    fun setPreferred(channel: Channel, role: Role) {
        prefs.edit().putString(precKey(channel), role.name).apply()
        _precedence.value = _precedence.value + (channel to orderPreferring(role))
        remerge()
    }

    /** Drop every override — back to the contract-default authority table. */
    fun resetPrecedence() {
        prefs.edit().apply { Channel.values().forEach { remove(precKey(it)) } }.apply()
        _precedence.value = DEFAULT_PRECEDENCE
        remerge()
    }

    private fun loadPrecedence(): Precedence = Channel.values().associateWith { ch ->
        prefs.getString(precKey(ch), null)
            ?.let { runCatching { Role.valueOf(it) }.getOrNull() }
            ?.let { orderPreferring(it) }
            ?: DEFAULT_PRECEDENCE.getValue(ch)
    }

    /** Ordered pod list with [role] first, the rest in enum order (forward-compatible for >2 pods). */
    private fun orderPreferring(role: Role): List<Role> =
        listOf(role) + Role.values().filter { it != role }

    private fun precKey(channel: Channel): String = "prec_${channel.name}"

    private fun push(buf: ArrayDeque<Float>, v: Float, flow: MutableStateFlow<List<Float>>) {
        buf.addLast(v)
        while (buf.size > Bands.HISTORY) buf.removeFirst()
        flow.value = buf.toList()
    }

    private fun log(line: String) {
        _log.update { (it + line).takeLast(LOG_LINES) }
    }

    override fun onCleared() {
        main.removeCallbacks(sampler)
        recorder.stop()
        scanner.stop()
        clients.values.forEach { it.close() }
        clients.clear()
    }

    // --- Bunshin pod helpers ----------------------------------------------

    /** Derive a pod's role from its advertised name (`ShintaiOS-aft` → Aft, else Fwd). */
    fun roleOf(name: String?): Role =
        if (name?.endsWith("-aft", ignoreCase = true) == true) Role.Aft else Role.Fwd

    private fun roleAddrKey(role: Role): String = "last_addr_${role.name.lowercase()}"

    private val Role.tag: String get() = name.uppercase()

    companion object {
        private const val KEY_IMPERIAL = "imperial"
        private const val SCAN_MS = 12_000L
        private const val RECORD_MS = 1_000L
        private const val LOG_LINES = 8
    }
}
