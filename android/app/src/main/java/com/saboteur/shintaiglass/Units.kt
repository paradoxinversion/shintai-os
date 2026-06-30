package com.saboteur.shintaiglass

import java.util.Locale

/** Display unit system. The board always reports metric; we convert at render. */
enum class Units { METRIC, IMPERIAL }

private fun f1(x: Double) = String.format(Locale.US, "%.1f", x)
private fun f0(x: Double) = String.format(Locale.US, "%.0f", x)
private fun cToF(c: Double) = c * 9.0 / 5.0 + 32.0

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

/** Distance headline as (value, unit). mm stays mm; imperial shows inches. */
fun distanceParts(mm: Int?, fallbackText: String, u: Units): Pair<String, String> =
    when {
        mm == null -> "—" to fallbackText
        u == Units.IMPERIAL -> f1(mm / 25.4) to "in"
        else -> mm.toString() to "mm"
    }
