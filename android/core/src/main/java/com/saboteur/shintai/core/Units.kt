package com.saboteur.shintai.core

import java.util.Locale

/** Display unit system. The board always reports metric; we convert at render. */
enum class Units { METRIC, IMPERIAL }

internal fun f1(x: Double) = String.format(Locale.US, "%.1f", x)
private fun f0(x: Double) = String.format(Locale.US, "%.0f", x)
internal fun cToF(c: Double) = c * 9.0 / 5.0 + 32.0

/** Climate raw: "23.4C 21%RH 908ppm" -> the "<n>C" temp becomes Fahrenheit. */
fun formatClimate(raw: String, u: Units): String =
    if (u == Units.METRIC) raw
    else Regex("""(-?\d+(?:\.\d+)?)C""").replace(raw) { m ->
        "${f1(cToF(m.groupValues[1].toDouble()))}F"
    }

/** Thermal raw: "Ctr:24.6 Min:22.2 Max:31.4C" -> each reading to Fahrenheit. */
fun formatThermal(raw: String, u: Units): String {
    if (u == Units.METRIC) return raw
    val converted = Regex("""(Ctr|Min|Max):(-?\d+(?:\.\d+)?)""").replace(raw) { m ->
        "${m.groupValues[1]}:${f1(cToF(m.groupValues[2].toDouble()))}"
    }
    return converted.replace(Regex("C$"), "F") // trailing unit only (not the "Ctr" label)
}

/** Environment raw: "1007.2hPa 84200ohm 22.8C 39%RH" -> the "<n>C" temp to Fahrenheit.
 *  Pressure (hPa) and gas resistance (ohm) are unit-agnostic, so they pass through. */
fun formatEnvironment(raw: String, u: Units): String =
    if (u == Units.METRIC) raw
    else Regex("""(-?\d+(?:\.\d+)?)C""").replace(raw) { m ->
        "${f1(cToF(m.groupValues[1].toDouble()))}F"
    }

/** GPS raw: "lat,lon 826m 0.0km/h" -> altitude in feet, speed in mph. */
fun formatGps(raw: String, u: Units): String {
    if (u == Units.METRIC) return raw
    // altitude: a number immediately followed by "m" then whitespace (not the "m" in km/h)
    var s = Regex("""(-?\d+(?:\.\d+)?)m(?=\s)""").replace(raw) { m ->
        "${f0(m.groupValues[1].toDouble() * 3.28084)}ft"
    }
    s = Regex("""(-?\d+(?:\.\d+)?)km/h""").replace(s) { m ->
        "${f1(m.groupValues[1].toDouble() * 0.621371)}mph"
    }
    return s
}

/** Enmu particulate raw "PM1.0:12 PM2.5:34 PM10:56 ug/m3" -> just the PM2.5 fraction as
 *  "34 µg/m³" — the one health-relevant number the lean glasses HUD surfaces (the full
 *  triad stays on the Operator console). µg/m³ is unit-agnostic, so no metric/imperial
 *  split. Returns "—" when the payload hasn't arrived or can't be parsed. */
fun formatPm25(raw: String): String {
    val v = Regex("""PM2\.5:(\d+)""").find(raw)?.groupValues?.get(1) ?: return "—"
    return "$v µg/m³"
}

/** A single temperature for a label: "31.4°C" (metric) or "88.5°F" (imperial).
 *  Used by Metsuke's thermal-grid panel to show the auto-range endpoints. */
fun formatTemp(c: Float, u: Units): String =
    if (u == Units.METRIC) "${f1(c.toDouble())}°C" else "${f1(cToF(c.toDouble()))}°F"

/** Distance headline as (value, unit). mm stays mm; imperial shows inches. */
fun distanceParts(mm: Int?, fallbackText: String, u: Units): Pair<String, String> =
    when {
        mm == null -> "—" to fallbackText
        u == Units.IMPERIAL -> f1(mm / 25.4) to "in"
        else -> mm.toString() to "mm"
    }
