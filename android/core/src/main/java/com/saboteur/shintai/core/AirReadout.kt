package com.saboteur.shintai.core

/**
 * The air quantities, resolved across the two channels that can carry them.
 *
 * `CONTRACT.md` defines `air_temp_c` and `humidity_pct` as **shared semantic slots**:
 * whichever sensor is present fills them, with the **BME688 taking precedence** over the
 * SCD-40 (faster, no self-heat offset). The firmware honours that when it builds the CSV,
 * and so does the serial readout.
 *
 * BLE does not: it splits by *source* — Climate (`abcdba98`) carries the SCD-40's
 * temp/RH/CO₂, Environment (`abcdc0de`) the BME688's pressure/gas/temp/RH. A consumer that
 * renders those two payloads as-is therefore shows an empty Climate row on a board with a
 * BME and no SCD, while the very same temperature and humidity sit inside the Environment
 * blob. This type puts the contract's rule back in one shared, tested place so both apps
 * resolve it identically instead of each re-deriving it from raw strings.
 *
 * Every field is null when nothing supplies it — the caller renders the blank sentinel.
 */
data class AirReadout(
    val co2Ppm: Int? = null,        // SCD-40 only; the BME688 does not measure CO₂
    val tempC: Float? = null,       // shared slot: BME688 → SCD-40
    val humidityPct: Float? = null, // shared slot: BME688 → SCD-40
    val pressureHpa: Float? = null, // BME688 only
    val gasOhms: Float? = null,     // BME688 only (VOC proxy; lower = more VOC)
) {
    /** Gas resistance in kΩ — the scale the console and the serial readout both show. */
    val gasKohm: Float? get() = gasOhms?.let { it / 1000f }

    /** Air temperature as (value, unit) for a labelled row — `"22.4"` to `"°C"`, or
     *  `"72.3"` to `"°F"`. Null (nothing supplying the shared slot) yields the blank
     *  sentinel and no unit, so the row renders like any other absent channel. */
    fun tempParts(u: Units): Pair<String, String> = when {
        tempC == null -> "—" to ""
        u == Units.IMPERIAL -> f1(cToF(tempC.toDouble())) to "°F"
        else -> f1(tempC.toDouble()) to "°C"
    }

    companion object {
        /** Climate payload, e.g. `"23.0C 41%RH 750ppm"` (SCD-40). */
        private val CLIMATE = Regex("""(-?[\d.]+)C\s+([\d.]+)%RH(?:\s+(\d+)ppm)?""")

        /** Environment payload, e.g. `"1007.2hPa 84200ohm 22.8C 39%RH"` (BME688). */
        private val ENVIRONMENT =
            Regex("""([\d.]+)hPa\s+([\d.]+)ohm\s+(-?[\d.]+)C\s+([\d.]+)%RH""")

        /**
         * Resolve both raw payloads into the five air quantities. Pass the blank sentinel
         * (or anything unparseable) for a channel that has not reported — it simply
         * contributes nothing, so a board with one sensor still yields a full readout for
         * the quantities that sensor covers.
         */
        fun resolve(climate: String, environment: String): AirReadout {
            val env = ENVIRONMENT.find(environment)
            val cli = CLIMATE.find(climate)
            return AirReadout(
                // CO₂ has no shared slot — only the SCD-40 measures it.
                co2Ppm = cli?.groupValues?.get(3)?.toIntOrNull(),
                // BME688 first, exactly as the firmware's CSV precedence does.
                tempC = env?.groupValues?.get(3)?.toFloatOrNull()
                    ?: cli?.groupValues?.get(1)?.toFloatOrNull(),
                humidityPct = env?.groupValues?.get(4)?.toFloatOrNull()
                    ?: cli?.groupValues?.get(2)?.toFloatOrNull(),
                pressureHpa = env?.groupValues?.get(1)?.toFloatOrNull(),
                gasOhms = env?.groupValues?.get(2)?.toFloatOrNull(),
            )
        }
    }
}
