package com.saboteur.shintaiglass

import java.util.UUID

/**
 * The GATT contract exposed by the Shintai-OS firmware (`shintai-os.ino`).
 *
 * Every characteristic is READ | NOTIFY and carries a plain UTF-8 string — the
 * sketch builds each value with Arduino `String(...)` and calls `setValue` +
 * `notify`. There is no binary packing, so parsing here is just string work.
 *
 * The standard 0x2902 Client Characteristic Configuration Descriptor (CCCD)
 * sits on each characteristic; writing ENABLE_NOTIFICATION to it is what turns
 * the notify stream on.
 */
object ShintaiGatt {

    val SERVICE: UUID = UUID.fromString("12345678-1234-1234-1234-123456789abc")

    val DISTANCE: UUID = UUID.fromString("abcd1234-ab12-ab12-ab12-abcdef123456")
    val ALERT: UUID = UUID.fromString("abcd5678-ab12-ab12-ab12-abcdef123456")
    val HEADING: UUID = UUID.fromString("abcd9012-ab12-ab12-ab12-abcdef123456")
    val ACCEL: UUID = UUID.fromString("abcdef12-ab12-ab12-ab12-abcdef123456")
    val GPS: UUID = UUID.fromString("abcd3456-ab12-ab12-ab12-abcdef123456")
    val CLIMATE: UUID = UUID.fromString("abcdba98-ab12-ab12-ab12-abcdef123456")
    val THERMAL: UUID = UUID.fromString("abcd6789-ab12-ab12-ab12-abcdef123456")

    /** Standard CCCD UUID (Bluetooth Base UUID: note the `8000`, not `0000`). */
    val CCCD: UUID = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")

    /** Characteristics this app subscribes to, in subscription order. */
    val SUBSCRIPTIONS: List<UUID> =
        listOf(DISTANCE, ALERT, HEADING, ACCEL, GPS, CLIMATE, THERMAL)
}

/** Immutable snapshot of everything the UI renders. */
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
    val packets: Int = 0,              // total notifications received — a visible heartbeat
)

enum class ConnectionState { Idle, Connecting, Discovering, Live, Disconnected }
