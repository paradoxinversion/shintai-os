package com.saboteur.shintai.core

/**
 * Fuda (札) — the NFC / RFID identity sense's consumer-side helpers.
 *
 * The board notifies the Fuda characteristic (and logs the `nfc_uid` / `nfc_type`
 * columns) as a single string `"UID:<hex> <type>"`, e.g. `"UID:04A1B2C3 ISO14443A"`.
 * This is where that raw payload becomes the parts the two apps render — kept in `:core`
 * (not a UI layer) so Glass and Operator agree, and in its own file (like [Kyukaku])
 * rather than swelling `Units`. See CONTRACT.md → Fuda.
 */

/**
 * Parse the raw Fuda payload `"UID:04A1B2C3 ISO14443A"` into `(UID, RF-type)` —
 * e.g. `("04A1B2C3", "ISO14443A")`. Parsed once: the lean glasses HUD surfaces the UID
 * alone ([Pair.first]); the Operator's Identity panel shows both. Not a unit conversion,
 * so no metric/imperial split. Each part is `"—"` when no tag has been read (payload
 * absent — the `"—"` blank sentinel — or unparseable).
 */
fun formatFuda(raw: String): Pair<String, String> {
    val m = Regex("""UID:(\S+)(?:\s+(\S+))?""").find(raw)
    val uid = m?.groupValues?.get(1)?.takeIf { it.isNotEmpty() } ?: "—"
    val type = m?.groupValues?.get(2)?.takeIf { it.isNotEmpty() } ?: "—"
    return uid to type
}
