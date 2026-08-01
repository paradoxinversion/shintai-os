package com.saboteur.shintai.core

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

/**
 * [AirReadout] — the contract's shared-slot rule for air temperature and humidity.
 *
 * The case that motivated this: a board with a BME688 and no SCD-40 reported temperature
 * and humidity on the Environment channel, but the console rendered the raw Climate and
 * Environment payloads by source, so the Climate row read blank and the operator concluded
 * the sensor wasn't reporting. Precedence (BME688 → SCD-40) has to live in one place.
 */
class AirTest {

    private val blank = "—"

    private val envPayload = "919.9hPa 38635ohm 22.4C 59%RH"      // BME688
    private val climatePayload = "23.0C 41%RH 750ppm"             // SCD-40

    @Test
    fun bmeWinsTheSharedSlotsOverScd() {
        val a = AirReadout.resolve(climatePayload, envPayload)
        assertEquals(22.4f, a.tempC!!, 0.01f)        // BME's 22.4, not the SCD's 23.0
        assertEquals(59f, a.humidityPct!!, 0.01f)    // BME's 59, not the SCD's 41
        assertEquals(750, a.co2Ppm)                  // CO2 has no shared slot — SCD only
        assertEquals(919.9f, a.pressureHpa!!, 0.01f)
        assertEquals(38635f, a.gasOhms!!, 0.01f)
    }

    @Test
    fun bmeOnlyBoardStillYieldsTempAndHumidity() {
        // The reported bug: no SCD-40 attached. Temp/humidity must still resolve.
        val a = AirReadout.resolve(blank, envPayload)
        assertEquals(22.4f, a.tempC!!, 0.01f)
        assertEquals(59f, a.humidityPct!!, 0.01f)
        assertNull(a.co2Ppm)                          // no SCD -> no CO2, correctly
    }

    @Test
    fun scdOnlyBoardFallsBackForTempAndHumidity() {
        val a = AirReadout.resolve(climatePayload, blank)
        assertEquals(23.0f, a.tempC!!, 0.01f)
        assertEquals(41f, a.humidityPct!!, 0.01f)
        assertEquals(750, a.co2Ppm)
        assertNull(a.pressureHpa)                     // pressure is BME-only
        assertNull(a.gasOhms)
    }

    @Test
    fun neitherChannelYieldsAllNulls() {
        val a = AirReadout.resolve(blank, blank)
        assertNull(a.tempC); assertNull(a.humidityPct); assertNull(a.co2Ppm)
        assertNull(a.pressureHpa); assertNull(a.gasOhms)
        assertNull(a.gasKohm)
    }

    @Test
    fun negativeAndClimateWithoutCo2Parse() {
        // The SCD payload's ppm group is optional (it warms up); sub-zero air is real.
        val a = AirReadout.resolve("-5.5C 88%RH", blank)
        assertEquals(-5.5f, a.tempC!!, 0.01f)
        assertEquals(88f, a.humidityPct!!, 0.01f)
        assertNull(a.co2Ppm)
    }

    @Test
    fun gasConvertsToKilohms() {
        assertEquals(38.635f, AirReadout.resolve(blank, envPayload).gasKohm!!, 0.001f)
    }
}
