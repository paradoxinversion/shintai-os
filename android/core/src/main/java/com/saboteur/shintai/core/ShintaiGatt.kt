package com.saboteur.shintai.core

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
 *
 * This lives in `:core` because it is the ONE mirror of `CONTRACT.md`'s GATT
 * table shared by both consumer apps (`:glass` and `:operator`). Which subset
 * an app subscribes to is the app's choice — see [ALL] and each app's own list.
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
    val ENVIRONMENT: UUID = UUID.fromString("abcdc0de-ab12-ab12-ab12-abcdef123456")

    /** Standard CCCD UUID (Bluetooth Base UUID: note the `8000`, not `0000`). */
    val CCCD: UUID = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")

    /** Every characteristic the board exposes, in a sensible subscribe order.
     *  Apps pass the subset they render to [ShintaiBleClient]; nothing forces an
     *  app to take them all (the Glass HUD deliberately skips [ENVIRONMENT]). */
    val ALL: List<UUID> =
        listOf(DISTANCE, ALERT, HEADING, ACCEL, GPS, CLIMATE, THERMAL, ENVIRONMENT)
}
