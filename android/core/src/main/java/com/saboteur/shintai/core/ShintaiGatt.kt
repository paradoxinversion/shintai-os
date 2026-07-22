package com.saboteur.shintai.core

import java.util.UUID

/**
 * The GATT contract exposed by the Shintai-OS firmware (`shintai-os.ino`).
 *
 * Every characteristic is READ | NOTIFY. All but one carry a plain UTF-8 string —
 * the sketch builds each value with Arduino `String(...)` and calls `setValue` +
 * `notify`, so parsing is just string work. The exception is [THERMAL_GRID]
 * (Metsuke): a packed 68-byte BINARY heat grid, listed in [BINARY] and delivered
 * as raw bytes rather than decoded text.
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

    /** Hokan's live PDR breadcrumb: "steps heading cadence" (string). Both apps
     *  integrate it into a dead-reckoned mini-map. See CONTRACT.md. */
    val HOKAN: UUID = UUID.fromString("abcdf007-ab12-ab12-ab12-abcdef123456")

    /** Enrai's live lightning channel: "km=<d> e=<energy> n=<count>" (string),
     *  notified event-driven once per validated AS3935 strike. See CONTRACT.md. */
    val LIGHTNING: UUID = UUID.fromString("abcda535-ab12-ab12-ab12-abcdef123456")

    /** Metsuke's binary heat grid (packed bytes, not a string). See [BINARY]. */
    val THERMAL_GRID: UUID = UUID.fromString("abcd7890-ab12-ab12-ab12-abcdef123456")

    /** Zanshin's binary rear depth grid (128 bytes = 8×8 × uint16 mm, not a string).
     *  See [BINARY] and CONTRACT.md "Rear Depth Grid". */
    val REAR_DEPTH_GRID: UUID = UUID.fromString("abcd5c88-ab12-ab12-ab12-abcdef123456")

    /** Standard CCCD UUID (Bluetooth Base UUID: note the `8000`, not `0000`). */
    val CCCD: UUID = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")

    /** Characteristics whose payload is raw binary, not UTF-8. [ShintaiBleClient]
     *  routes these to `onBinary` instead of decoding them to a string. */
    val BINARY: Set<UUID> = setOf(THERMAL_GRID, REAR_DEPTH_GRID)

    /** Every STRING characteristic the board exposes, in a sensible subscribe
     *  order — the complete numeric readout the Operator takes. It does NOT include
     *  the binary [THERMAL_GRID]: both apps that render the heat panel append it to
     *  their own subscription list explicitly (the grid is an image channel, kept
     *  out of the string set). Apps pass the subset they render to [ShintaiBleClient];
     *  nothing forces an app to take them all (the Glass HUD skips [ENVIRONMENT]). */
    val ALL: List<UUID> =
        listOf(DISTANCE, ALERT, HEADING, ACCEL, GPS, CLIMATE, THERMAL, ENVIRONMENT, HOKAN, LIGHTNING)
}
