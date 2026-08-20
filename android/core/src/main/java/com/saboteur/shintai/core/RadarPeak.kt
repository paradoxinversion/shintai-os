package com.saboteur.shintai.core

/**
 * Hitokage's multi-peak radar readout (specs/zokyo/hitokage.md). The board notifies
 * [ShintaiGatt.RADAR_PEAKS] as `"n=<count> i:distMm/strength ..."`, e.g.
 * `"n=2 0:2894/-7029 1:4809/-3865"` — up to 9 simultaneous reflection peaks from the
 * XM125's onboard distance detector (its real default mode; a presence-mode fallback
 * has no peak-list concept, so this channel simply stays silent there). No
 * angle/azimuth: every peak is "somewhere along this one beam," not positioned
 * left/right. Kept in `:core` (not a UI layer) so both apps parse identically, in its
 * own file like [Fuda].
 */

/** One reflection peak: range in mm and the detector's raw signal strength (unitless,
 *  relative — not a physical dB scale; more positive/less negative is stronger). */
data class RadarPeak(val distanceMm: Int, val strengthRaw: Int)

/** Parse the raw RADAR_PEAKS payload into its peaks, nearest first is NOT guaranteed —
 *  the board emits them in detector index order. Malformed/absent payload yields empty. */
fun parseRadarPeaks(raw: String): List<RadarPeak> =
    Regex("""(\d+):(-?\d+)/(-?\d+)""").findAll(raw).mapNotNull { m ->
        val (_, dist, strength) = m.destructured
        val d = dist.toIntOrNull()
        val s = strength.toIntOrNull()
        if (d != null && s != null) RadarPeak(d, s) else null
    }.toList()

/** A longer-range distance as one compact label — metres or feet, unlike [distanceParts]'s
 *  mm/in (built for close-in proximity). Kept here rather than in Units.kt (already at its
 *  function-count budget); only Hitokage's A-scope needs a metres-scale label so far. */
fun formatRangeM(mm: Int, u: Units): String =
    if (u == Units.IMPERIAL) "${f1(mm / 304.8)}ft" else "${f1(mm / 1000.0)}m"
