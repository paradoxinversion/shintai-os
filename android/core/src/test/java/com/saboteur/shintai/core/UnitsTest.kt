package com.saboteur.shintai.core

import org.junit.Assert.assertEquals
import org.junit.Test

/**
 * Unit-formatting helpers. Covers [formatPm25], the one parser in `Units` — the lean
 * glasses HUD depends on it pulling PM2.5 out of Enmu's full triad payload, and on it
 * degrading to the blank sentinel rather than throwing on a payload that never arrived.
 */
class UnitsTest {

    @Test
    fun formatPm25ExtractsOnlyThePm25Fraction() {
        // The middle value, not PM1.0 (which the "PM1.0:" label could shadow) or PM10.
        assertEquals("34 µg/m³", formatPm25("PM1.0:12 PM2.5:34 PM10:56 ug/m3"))
    }

    @Test
    fun formatPm25HandlesZeroAndLargeValues() {
        assertEquals("0 µg/m³", formatPm25("PM1.0:0 PM2.5:0 PM10:0 ug/m3"))
        assertEquals("999 µg/m³", formatPm25("PM1.0:800 PM2.5:999 PM10:1200 ug/m3"))
    }

    @Test
    fun formatPm25FallsBackToBlankWhenAbsentOrUnparseable() {
        assertEquals("—", formatPm25("—"))          // no notify yet — the blank sentinel
        assertEquals("—", formatPm25(""))
        assertEquals("—", formatPm25("PM1.0:12 PM10:56 ug/m3"))   // PM2.5 missing from the payload
    }

    @Test
    fun formatFudaSplitsUidAndType() {
        assertEquals("04A1B2C3" to "ISO14443A", formatFuda("UID:04A1B2C3 ISO14443A"))
    }

    @Test
    fun formatFudaYieldsBlankTypeWhenOnlyAUidIsCarried() {
        // The glasses render .first alone, so a type-less payload must still give a UID.
        assertEquals("04A1B2C3" to "—", formatFuda("UID:04A1B2C3"))
    }

    @Test
    fun formatFudaFallsBackToBlankPairWhenNoTagRead() {
        assertEquals("—" to "—", formatFuda("—"))   // event-driven: no scan yet
        assertEquals("—" to "—", formatFuda(""))
        assertEquals("—" to "—", formatFuda("garbage"))
    }
}
