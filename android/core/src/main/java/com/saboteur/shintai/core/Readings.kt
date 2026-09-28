package com.saboteur.shintai.core

import java.util.UUID
import kotlin.math.PI
import kotlin.math.cos
import kotlin.math.sin

/** Proximity-alert threshold in mm, matching `NEAR_MM` in shintai-os.ino. */
const val NEAR_MM = 200

/** Immutable snapshot of everything a consumer might render. Both apps observe
 *  this; the Glass HUD simply ignores the fields it doesn't draw (e.g.
 *  [environment]), so the model is a superset of what either app shows. */
data class ShintaiReadings(
    val connection: ConnectionState = ConnectionState.Idle,
    val distanceText: String = "—",   // packed dual-arc, e.g. "L:1234 R:1180 mm"
    val distanceMm: Int? = null,       // the NEARER arc in mm, null when neither has a target
    val distanceLMm: Int? = null,      // rear-left  arc (mux ch0), null = no target/absent
    val distanceRMm: Int? = null,      // rear-right arc (mux ch1), null = no target/absent
    val alertActive: Boolean = false,  // proximity warning latched from the Alert char
    val heading: String = "—",         // e.g. "169.0° S"
    val accel: String = "—",           // e.g. "X:1.8 Y:0.0 Z:9.8"
    val gps: String = "—",             // e.g. "37.12345,-122.12345 12m 3.4km/h"
    val climate: String = "—",         // e.g. "23.0C 41%RH 750ppm" (SCD-40, warms up ~5s)
    val thermal: String = "—",         // e.g. "Ctr:23.1 Min:22.6 Max:31.4C" (MLX90640)
    val environment: String = "—",     // e.g. "1007.2hPa 84200ohm 22.8C 39%RH" (BME688)
    val particulate: String = "—",     // e.g. "PM1.0:12 PM2.5:34 PM10:56 ug/m3" (Enmu, PMSA003I)
    val fuda: String = "—",            // e.g. "UID:04A1B2C3 ISO14443A" (Fuda, PN532) — event-driven
    val presence: String = "—",        // e.g. "present=1 dist=260mm" (Hitokage, XM125 radar)
    val hizashi: HizashiState = HizashiState(),  // Hizashi's ambient light (TSL2591), parsed
    val radarPeaks: String = "—",      // e.g. "n=2 0:2230/-2774 1:4809/-3865" (Hitokage, distance mode only)
    val pane: PaneState = PaneState(),  // the board's OLED pane (Pane Control char) — per-pod, not merged
    val kyukaku: KyukakuState = KyukakuState(),  // Kyūkaku's smell state, derived from environment's gas_ohms
    val thermalGrid: ThermalGrid? = null,  // Metsuke's 32×24 heat grid, null until the first frame
    val rearDepthGrid: DepthGrid? = null,  // Zanshin's 8×8 rear depth field, null until the first notify
    val hokan: HokanPdr? = null,       // Hokan's dead-reckoned breadcrumb, null until the first notify
    val lightning: LightningState = LightningState(),  // Enrai's last-strike snapshot + count
    val lightningConfig: LightningConfig = LightningConfig(),  // Enrai's AS3935 tuning (Lightning Control char)
    val packets: Int = 0,              // total notifications received — a visible heartbeat
    val perBoard: Map<Role, ConnectionState> = emptyMap(),  // Bunshin: per-pod connection (empty = single-producer)
)

enum class ConnectionState { Idle, PermissionNeeded, Connecting, Discovering, Live, Disconnected }

/**
 * Metsuke's decoded thermal grid (the contract's one binary characteristic).
 * [cells] is a row-major 32×24 of 0..255 normalised levels; [minC]/[maxC] are the
 * scene's temperature range in °C, so the renderer auto-ranges the palette across
 * exactly the span these cells were normalised over. Uses `List<Int>` (not IntArray)
 * so the enclosing data class keeps value equality.
 */
data class ThermalGrid(
    val minC: Float,
    val maxC: Float,
    val cells: List<Int>,
) {
    companion object {
        const val W = 32
        const val H = 24
        const val CELLS = W * H          // 768 (full native MLX90640 resolution)
        const val BYTES = 4 + CELLS      // 772 — the reassembled canonical grid
        const val CHUNKS = 4             // chunks per frame on the wire
        const val CHUNK_HEADER = 7       // frame_seq + chunk_index + chunk_count + min_dC + max_dC

        /**
         * Parse the reassembled **772-byte canonical grid** (int16 min/max + 768 cells); null if
         * short. The wire delivers each frame as [CHUNKS] chunks — [ThermalGridAssembler]
         * reassembles them before this runs. See CONTRACT.md "Thermal Grid".
         */
        fun parse(b: ByteArray): ThermalGrid? {
            if (b.size < BYTES) return null
            // Little-endian signed int16: combine bytes then narrow to Short for the sign.
            fun le16(i: Int): Int =
                ((((b[i + 1].toInt() and 0xFF) shl 8) or (b[i].toInt() and 0xFF)).toShort()).toInt()
            val cells = List(CELLS) { b[4 + it].toInt() and 0xFF }
            return ThermalGrid(minC = le16(0) / 10f, maxC = le16(2) / 10f, cells = cells)
        }
    }
}

/**
 * Reassembles the chunked Thermal Grid (CONTRACT.md "Thermal Grid"): the wire delivers each
 * 32×24 frame as [ThermalGrid.CHUNKS] chunks, and this buffers them by `frame_seq`, emitting
 * the full 772-byte canonical grid the instant every chunk of one frame has arrived. Stateful —
 * one per connection; an incomplete frame (a dropped chunk) is discarded when the next
 * `frame_seq` begins. Kept in `:core` so both apps reassemble identically.
 */
class ThermalGridAssembler {
    private var frameSeq = -1
    private var received = 0                        // bitmask of chunk indices seen this frame
    private var count = 0
    private val header = ByteArray(4)               // frame-global min/max (int16 LE ×2)
    private val cells = ByteArray(ThermalGrid.CELLS)

    /** Feed one wire chunk; returns the 772-byte canonical grid when a frame completes, else null. */
    fun feed(chunk: ByteArray): ByteArray? {
        if (chunk.size < ThermalGrid.CHUNK_HEADER) return null
        val seq = chunk[0].toInt() and 0xFF
        val idx = chunk[1].toInt() and 0xFF
        val cnt = chunk[2].toInt() and 0xFF
        // One combined guard (detekt caps returns at 5): a valid chunk_count (≤32 for the
        // Int bitmask, dividing the grid evenly), an in-range index, and enough bytes.
        val per = if (cnt in 1..32 && ThermalGrid.CELLS % cnt == 0) ThermalGrid.CELLS / cnt else 0
        if (per == 0 || idx >= cnt || chunk.size < ThermalGrid.CHUNK_HEADER + per) return null

        if (seq != frameSeq) {                                   // a new frame — reset accumulation
            frameSeq = seq
            count = cnt
            received = 0
            System.arraycopy(chunk, 3, header, 0, 4)             // min/max (repeated in every chunk)
        }
        System.arraycopy(chunk, ThermalGrid.CHUNK_HEADER, cells, idx * per, per)
        received = received or (1 shl idx)

        if (received != (1 shl count) - 1) return null           // frame not yet complete
        received = 0                                             // consumed; await the next frame
        return ByteArray(ThermalGrid.BYTES).also {
            System.arraycopy(header, 0, it, 0, 4)
            System.arraycopy(cells, 0, it, 4, ThermalGrid.CELLS)
        }
    }
}

/**
 * Ironbow heat ramp (Metsuke MD-4): map a 0..255 cell level to an (r, g, b) triple —
 * black → deep red → orange → yellow → near-white, piecewise-linear. Shared so Glass
 * and Operator render the *same* palette for the one binary channel. Returns plain
 * ints (no toolkit `Color`) so `:core` stays UI-free; each app wraps the triple in
 * its own colour type. Cold stays black, so on the Glass waveguide it reads as
 * see-through and only real heat emits light.
 */
fun ironbow(level: Int): Triple<Int, Int, Int> {
    val t = level.coerceIn(0, 255) / 255f
    // Each stop is (position, r, g, b) along the ramp.
    val stops = arrayOf(
        floatArrayOf(0.00f, 0f, 0f, 0f),
        floatArrayOf(0.30f, 90f, 0f, 0f),
        floatArrayOf(0.55f, 200f, 40f, 0f),
        floatArrayOf(0.75f, 255f, 130f, 0f),
        floatArrayOf(0.90f, 255f, 210f, 40f),
        floatArrayOf(1.00f, 255f, 255f, 210f),
    )
    var i = 0
    while (i < stops.size - 1 && t > stops[i + 1][0]) i++
    val lo = stops[i]
    val hi = stops[minOf(i + 1, stops.size - 1)]
    val span = hi[0] - lo[0]
    val f = if (span > 0f) (t - lo[0]) / span else 0f
    fun mix(c: Int) = (lo[c] + (hi[c] - lo[c]) * f).toInt().coerceIn(0, 255)
    return Triple(mix(1), mix(2), mix(3))
}

/**
 * The grid as a row-major `W*H` array of opaque ARGB pixels (ironbow), ready to be
 * wrapped in a Bitmap/ImageBitmap and **bilinear-upscaled** for a smooth heat panel
 * (Metsuke Forward-path: interpolation costs zero extra BLE bytes). UI-free — plain
 * Int packing — so `:core` stays Compose-independent; each app wraps the array.
 */
fun ThermalGrid.argb(): IntArray = IntArray(cells.size) { i ->
    val (r, g, b) = ironbow(cells[i])
    (0xFF shl 24) or (r shl 16) or (g shl 8) or b
}

/**
 * Zanshin's decoded rear depth field (the contract's second binary characteristic).
 * [zonesMm] is a row-major 8×8 of per-zone distances in mm; **0 = no valid target**. The
 * renderer maps near→alarming, far→cool, 0→dark. `List<Int>` keeps the data class's value
 * equality. See CONTRACT.md "Rear Depth Grid".
 */
data class DepthGrid(val zonesMm: List<Int>) {
    companion object {
        const val W = 8
        const val H = 8
        const val ZONES = W * H       // 64
        const val BYTES = ZONES * 2   // 128 (uint16 LE per zone)

        /** Parse the 128-byte little-endian depth grid; null if short. */
        fun parse(b: ByteArray): DepthGrid? {
            if (b.size < BYTES) return null
            val zones = List(ZONES) {
                (b[it * 2].toInt() and 0xFF) or ((b[it * 2 + 1].toInt() and 0xFF) shl 8)
            }
            return DepthGrid(zones)
        }
    }
}

/**
 * Rear-depth ramp: a zone's distance (mm) → (r, g, b). NEAR reads as alarming red, through
 * amber and green, to a cool far blue; a zone with no target (0 mm) is black. Shared so Glass
 * and Operator render the same rear panel. Plain ints so `:core` stays UI-free.
 */
fun depthColor(mm: Int): Triple<Int, Int, Int> {
    if (mm <= 0) return Triple(0, 0, 0)                              // no target → dark
    val t = ((mm - NEAR_MM) / (3000f - NEAR_MM)).coerceIn(0f, 1f)   // 0 = closest … 1 = far
    val stops = arrayOf(
        floatArrayOf(0.00f, 255f, 40f, 0f),    // near: red — something's on you
        floatArrayOf(0.30f, 255f, 170f, 0f),   // amber
        floatArrayOf(0.65f, 40f, 200f, 70f),   // green
        floatArrayOf(1.00f, 0f, 60f, 130f),    // far: cool blue
    )
    var i = 0
    while (i < stops.size - 1 && t > stops[i + 1][0]) i++
    val lo = stops[i]
    val hi = stops[minOf(i + 1, stops.size - 1)]
    val span = hi[0] - lo[0]
    val f = if (span > 0f) (t - lo[0]) / span else 0f
    fun mix(c: Int) = (lo[c] + (hi[c] - lo[c]) * f).toInt().coerceIn(0, 255)
    return Triple(mix(1), mix(2), mix(3))
}

/** The depth field as a row-major `W*H` array of opaque ARGB pixels (near→warm, far→cool,
 *  no-target→dark), ready to wrap in a Bitmap/ImageBitmap and upscale. UI-free. */
fun DepthGrid.argb(): IntArray = IntArray(zonesMm.size) { i ->
    val (r, g, b) = depthColor(zonesMm[i])
    (0xFF shl 24) or (r shl 16) or (g shl 8) or b
}

/** One point on the dead-reckoned track, in METRES (East = +x, North = +y). Plain
 *  floats so `:core` stays UI-free; each app maps it to its own canvas coordinates. */
data class PdrPoint(val x: Float, val y: Float)

/**
 * Hokan's live pedestrian dead-reckoning (specs/zokyo/hokan.md), integrated from the
 * "steps heading cadence" breadcrumb characteristic. [track] is the reconstructed
 * walked path in metres starting at the origin; each notification advances it by
 * Δsteps × [STEP_LEN_M] along the current heading — the SAME math as
 * `groundstation/hokan.py`, run live so the glasses draw a breadcrumb mini-map that
 * mirrors the base-side path. A held heading (missing field) reuses the last.
 */
data class HokanPdr(
    val steps: Int = 0,
    val headingDeg: Float = 0f,
    val cadence: Int = 0,
    val track: List<PdrPoint> = listOf(PdrPoint(0f, 0f)),
    val lastSteps: Int? = null,   // previous cumulative count, for the Δ integration
) {
    /** Advance by the positive step delta along the (new or held) heading. */
    fun advance(newSteps: Int?, newHeading: Float?, newCadence: Int?): HokanPdr {
        val hdg = newHeading ?: headingDeg
        var pts = track
        if (lastSteps != null && newSteps != null && newSteps > lastSteps) {
            val dist = (newSteps - lastSteps) * STEP_LEN_M
            val rad = hdg * PI / 180.0                       // heading 0 = N -> +y, 90 = E -> +x
            val last = track.lastOrNull() ?: PdrPoint(0f, 0f)
            val next = PdrPoint(last.x + (dist * sin(rad)).toFloat(),
                                last.y + (dist * cos(rad)).toFloat())
            pts = (track + next).let { if (it.size > MAX_POINTS) it.takeLast(MAX_POINTS) else it }
        }
        return copy(
            steps = newSteps ?: steps,
            headingDeg = hdg,
            cadence = newCadence ?: cadence,
            track = pts,
            lastSteps = newSteps ?: lastSteps,
        )
    }

    companion object {
        const val STEP_LEN_M = 0.7f    // HkD-3, mirrors groundstation/hokan.py
        const val MAX_POINTS = 600     // bound the breadcrumb history

        /** Fold a "1240 98.5 112" (steps · heading_deg · cadence) payload. */
        fun fold(prev: HokanPdr?, value: String): HokanPdr {
            val p = value.trim().split(Regex("""\s+"""))
            return (prev ?: HokanPdr()).advance(
                p.getOrNull(0)?.toIntOrNull(),
                p.getOrNull(1)?.toFloatOrNull(),
                p.getOrNull(2)?.toIntOrNull(),
            )
        }
    }
}

/**
 * Enrai's decoded lightning snapshot (specs/zokyo/enrai.md), folded from the
 * "km=<d> e=<energy> n=<count>" characteristic. Lightning is event-based, so this is
 * the **last strike** ([km], [energy]) plus a **monotonic count** ([strikes]) — a
 * consumer flashes when [strikes] changes. [km] follows the AS3935: `1` = overhead,
 * `63` = out of range, `0` = none yet.
 */
data class LightningState(
    val km: Int = 0,
    val energy: Long = 0,
    val strikes: Int = 0,
) {
    val hasStrike: Boolean get() = strikes > 0

    /** Human distance label: "overhead" / "out of range" / "~5 km" / "—". */
    val distanceLabel: String get() = when {
        strikes == 0 -> "—"
        km <= 1 -> "overhead"
        km >= 63 -> "out of range"
        else -> "~$km km"
    }

    companion object {
        /** Fold a "km=1 e=227467 n=8" payload; a malformed/missing field keeps prev. */
        fun fold(prev: LightningState?, value: String): LightningState {
            val p = prev ?: LightningState()
            fun field(tag: String) = Regex("""$tag=(-?\d+)""").find(value)?.groupValues?.get(1)
            return LightningState(
                km = field("km")?.toIntOrNull() ?: p.km,
                energy = field("e")?.toLongOrNull() ?: p.energy,
                strikes = field("n")?.toIntOrNull() ?: p.strikes,
            )
        }
    }
}

/**
 * Hizashi (日差し) — the TSL2591's ambient light, folded from the "lux=412.5 ir=57"
 * characteristic. Parsed rather than held raw (unlike Particulate/Presence) because both
 * apps want the lux NUMBER: Glass shows it alone, and the visible/IR split only means
 * something as two values you can compare.
 *
 * [known] is false until the first notify — which also covers the no-sensor case, since a
 * board with no TSL2591 never notifies this channel at all.
 */
data class HizashiState(
    val lux: Float = 0f,
    val ir: Int = 0,
    val known: Boolean = false,
) {
    /** Lean readout for the HUD: "412 lx", or "—" before the first reading. Whole lux —
     *  a tenth of a lux is noise at every level a wearer can perceive. */
    val label: String get() = if (!known) "—" else "${lux.toInt()} lx"

    /** Console readout: illuminance plus the raw IR channel it was derived from. */
    val detail: String get() = if (!known) "—" else "${lux.toInt()} lx   IR $ir"

    companion object {
        /** Fold a "lux=412.5 ir=57" payload; a malformed field keeps the previous value. */
        fun fold(prev: HizashiState?, value: String): HizashiState {
            val p = prev ?: HizashiState()
            val lx = Regex("""lux=(-?[\d.]+)""").find(value)?.groupValues?.get(1)?.toFloatOrNull()
            val irv = Regex("""ir=(\d+)""").find(value)?.groupValues?.get(1)?.toIntOrNull()
            // A payload that parsed neither field is not evidence of a sensor.
            if (lx == null && irv == null) return p
            return HizashiState(lux = lx ?: p.lux, ir = irv ?: p.ir, known = true)
        }
    }
}

/**
 * The board's OLED pane state, from the writable Pane Control characteristic
 * (`pane=2/5 NAV`). The console drives its paging UI from this and writes tokens back.
 *
 * [name] doubles as the write token for jumping straight to this pane (lowercased) — the
 * firmware matches pane names case-insensitively, so the readout and the token vocabulary
 * are the same strings and cannot drift apart.
 *
 * [known] is false until the board's first state notify arrives — which also covers the
 * no-panel case, since a board with nothing at 0x3D never reports a pane.
 */
data class PaneState(
    val index: Int = 0,        // 0-based pane index
    val count: Int = 0,        // how many panes the carousel has
    val name: String = "—",    // e.g. "NAV"
    val known: Boolean = false,
) {
    companion object {
        /** Parse the board's pane payload, e.g. "pane=2/5 NAV". Malformed input stays unknown. */
        fun parse(value: String): PaneState {
            val m = Regex("""pane=(\d+)/(\d+)\s+(\S+)""").find(value) ?: return PaneState()
            val (n, total, label) = m.destructured
            val idx = n.toIntOrNull() ?: return PaneState()
            val cnt = total.toIntOrNull() ?: return PaneState()
            // The board reports 1-based for humans; keep it 0-based internally.
            return PaneState(index = (idx - 1).coerceAtLeast(0), count = cnt, name = label, known = true)
        }
    }
}

/**
 * Enrai's AS3935 tuning, from the writable Lightning Control characteristic
 * (specs/zokyo/enrai.md). The app drives its tuning UI from this and writes tokens back.
 *
 * [known] is false until the board reports a real config — which also covers the
 * no-sensor case: a board with nothing at 0x03 reports `enrai=none`, so the console hides
 * the tuning panel rather than offering knobs that drive nothing.
 */
data class LightningConfig(
    val outdoor: Boolean = false,   // AFE gain: false = INDOOR (high), true = OUTDOOR (lower)
    val spike: Int = 2,             // spike rejection 1..11
    val watchdog: Int = 1,          // watchdog threshold 1..10
    val tune: Int = 0,              // antenna tuning cap 0..15
    val known: Boolean = false,     // a config notify has been seen
) {
    companion object {
        /** The payload a board with no AS3935 reports — an explicit absence, not a config. */
        private const val ABSENT = "enrai=none"

        /** Parse the board's config payload, e.g. "gain=out spike=2 wdog=1 tune=0".
         *  [ABSENT] and anything unparseable yield a default with `known = false`. */
        fun parse(value: String): LightningConfig {
            // Checked explicitly rather than left to the gain regex failing to match, so
            // the absent case is intentional behaviour and not an accident of the pattern.
            if (value.startsWith(ABSENT)) return LightningConfig()
            fun intOf(k: String, d: Int) =
                Regex("""$k=(\d+)""").find(value)?.groupValues?.get(1)?.toIntOrNull() ?: d
            val gain = Regex("""gain=(\w+)""").find(value)?.groupValues?.get(1)
            return LightningConfig(
                outdoor = gain == "out",
                spike = intOf("spike", 2),
                watchdog = intOf("wdog", 1),
                tune = intOf("tune", 0),
                known = gain != null,
            )
        }
    }
}

/**
 * Parse the packed dual-arc Distance payload "L:1234 R:1180 mm" (per-arc "--" = no
 * target). The nearer arc drives the single-value proximity UI + alert latch, so an
 * app that shows one distance still sees the closest object. See CONTRACT.md.
 * Split out of [fold] to keep that function's `when` under detekt's complexity ceiling.
 */
private fun ShintaiReadings.foldDistance(value: String): ShintaiReadings {
    fun arc(tag: String) =
        Regex("""$tag:(\d+)""").find(value)?.groupValues?.get(1)?.toIntOrNull()
    val l = arc("L")
    val r = arc("R")
    val near = listOfNotNull(l, r).minOrNull()
    return copy(
        distanceText = value,
        distanceMm = near,
        distanceLMm = l,
        distanceRMm = r,
        alertActive = if (near != null && near > NEAR_MM) false else alertActive,
    )
}

/**
 * Fold one characteristic notification into a new snapshot. Parsing lives here,
 * not in the BLE layer, so [ShintaiBleClient] stays a dumb transport and BOTH
 * view models share one source of truth for how a payload becomes state.
 *
 * The Alert characteristic is edge-triggered ("CLOSE", no explicit clear), so we
 * latch [ShintaiReadings.alertActive] and let a Distance reading back beyond
 * [NEAR_MM] clear it — mirroring the firmware.
 */
fun ShintaiReadings.fold(uuid: UUID, value: String): ShintaiReadings {
    val base = copy(packets = packets + 1) // heartbeat: every notification counts
    return when (uuid) {
        ShintaiGatt.DISTANCE -> base.foldDistance(value)
        ShintaiGatt.ALERT -> base.copy(alertActive = true)
        ShintaiGatt.HEADING -> base.copy(heading = value)
        ShintaiGatt.ACCEL -> base.copy(accel = value)
        ShintaiGatt.GPS -> base.copy(gps = value)
        ShintaiGatt.CLIMATE -> base.copy(climate = value)
        ShintaiGatt.THERMAL -> base.copy(thermal = value)
        // Environment also feeds Kyūkaku: its gas_ohms is folded into the derived
        // smell state (no BLE channel of its own — see Kyukaku.kt). Both apps get it.
        ShintaiGatt.ENVIRONMENT -> base.copy(environment = value, kyukaku = base.kyukaku.fold(value))
        ShintaiGatt.HOKAN -> base.copy(hokan = HokanPdr.fold(base.hokan, value))
        ShintaiGatt.LIGHTNING -> base.copy(lightning = LightningState.fold(base.lightning, value))
        // Enmu: particulate readout, stored raw for display (µg/m³ needs no unit conversion).
        ShintaiGatt.PARTICULATE -> base.copy(particulate = value)
        // Fuda: the last tag read. Event-driven, so this value only ever arrives on a scan;
        // the last tag persists until the next notify.
        ShintaiGatt.FUDA -> base.copy(fuda = value)
        // Hitokage: periodic radar readout, stored raw for display like Particulate.
        ShintaiGatt.PRESENCE -> base.copy(presence = value)
        ShintaiGatt.RADAR_PEAKS -> base.copy(radarPeaks = value)
        // Hizashi: ambient light, parsed so both apps can use the lux number directly.
        ShintaiGatt.HIZASHI -> base.copy(hizashi = HizashiState.fold(base.hizashi, value))
        ShintaiGatt.LIGHTNING_CTRL -> base.copy(lightningConfig = LightningConfig.parse(value))
        // Pane Control notifies on a console write AND on an on-body swipe, so this is the
        // board telling us what it's showing — never an echo of what we asked for.
        ShintaiGatt.PANE_CTRL -> base.copy(pane = PaneState.parse(value))
        else -> base
    }
}

/**
 * Fold one BINARY notification (currently only [ShintaiGatt.THERMAL_GRID]) into a
 * new snapshot. The string [fold] can't take raw bytes, so binary payloads route
 * here — same packet-heartbeat bookkeeping, parsing kept in `:core` so both apps
 * agree. A malformed packet keeps the previous grid rather than dropping to null.
 */
fun ShintaiReadings.foldBinary(uuid: UUID, value: ByteArray): ShintaiReadings {
    val base = copy(packets = packets + 1)
    return when (uuid) {
        ShintaiGatt.THERMAL_GRID -> base.copy(thermalGrid = ThermalGrid.parse(value) ?: thermalGrid)
        ShintaiGatt.REAR_DEPTH_GRID -> base.copy(rearDepthGrid = DepthGrid.parse(value) ?: rearDepthGrid)
        else -> base
    }
}
