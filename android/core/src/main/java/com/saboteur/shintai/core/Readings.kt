package com.saboteur.shintai.core

import java.util.UUID

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
    val packets: Int = 0,              // total notifications received — a visible heartbeat
)

enum class ConnectionState { Idle, PermissionNeeded, Connecting, Discovering, Live, Disconnected }

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
        else -> base
    }
}
