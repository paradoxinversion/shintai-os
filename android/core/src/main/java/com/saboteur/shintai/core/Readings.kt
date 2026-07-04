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
    val distanceText: String = "—",   // e.g. "1234 mm" or "no reading"
    val distanceMm: Int? = null,       // parsed numeric mm, null when no reading
    val alertActive: Boolean = false,  // proximity warning latched from the Alert char
    val heading: String = "—",         // e.g. "169.0° S"
    val accel: String = "—",           // e.g. "X:1.8 Y:0.0 Z:9.8"
    val gps: String = "—",             // e.g. "37.12345,-122.12345 12m 3.4km/h"
    val climate: String = "—",         // e.g. "23.0C 41%RH 750ppm" (SCD-40, warms up ~5s)
    val thermal: String = "—",         // e.g. "Ctr:23.1 Min:22.6 Max:31.4C" (MLX90640)
    val environment: String = "—",     // e.g. "1007.2hPa 84200ohm 22.8C 39%RH" (BME688)
    val thermalGrid: ThermalGrid? = null,  // Metsuke's 8x8 heat grid, null until the first frame
    val hokan: HokanPdr? = null,       // Hokan's dead-reckoned breadcrumb, null until the first notify
    val packets: Int = 0,              // total notifications received — a visible heartbeat
)

enum class ConnectionState { Idle, PermissionNeeded, Connecting, Discovering, Live, Disconnected }

/**
 * Metsuke's decoded thermal grid (the contract's one binary characteristic).
 * [cells] is a row-major 16×12 of 0..255 normalised levels; [minC]/[maxC] are the
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
        const val W = 16
        const val H = 12
        const val BYTES = 196

        /** Parse a 68-byte little-endian packet (see CONTRACT.md "Thermal Grid"); null if short. */
        fun parse(b: ByteArray): ThermalGrid? {
            if (b.size < BYTES) return null
            // Little-endian signed int16: combine bytes then narrow to Short for the sign.
            fun le16(i: Int): Int =
                ((((b[i + 1].toInt() and 0xFF) shl 8) or (b[i].toInt() and 0xFF)).toShort()).toInt()
            val cells = List(W * H) { b[4 + it].toInt() and 0xFF }
            return ThermalGrid(minC = le16(0) / 10f, maxC = le16(2) / 10f, cells = cells)
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
        ShintaiGatt.DISTANCE -> {
            val mm = Regex("""\d+""").find(value)?.value?.toIntOrNull()
            base.copy(
                distanceText = value,
                distanceMm = mm,
                alertActive = if (mm != null && mm > NEAR_MM) false else base.alertActive,
            )
        }
        ShintaiGatt.ALERT -> base.copy(alertActive = true)
        ShintaiGatt.HEADING -> base.copy(heading = value)
        ShintaiGatt.ACCEL -> base.copy(accel = value)
        ShintaiGatt.GPS -> base.copy(gps = value)
        ShintaiGatt.CLIMATE -> base.copy(climate = value)
        ShintaiGatt.THERMAL -> base.copy(thermal = value)
        ShintaiGatt.ENVIRONMENT -> base.copy(environment = value)
        ShintaiGatt.HOKAN -> base.copy(hokan = HokanPdr.fold(base.hokan, value))
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
        else -> base
    }
}
