package com.saboteur.shintai.core

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * Hizashi (日差し) — parsing the TSL2591's "lux=412.5 ir=57" channel and its place in the
 * Bunshin merge. The payload is parsed rather than held raw (unlike Particulate/Presence)
 * because both apps want the lux as a number, so the parse is worth pinning down.
 */
class HizashiTest {

    private fun fold(v: String, prev: HizashiState? = null) = HizashiState.fold(prev, v)

    @Test
    fun parsesLuxAndIr() {
        val s = fold("lux=412.5 ir=57")
        assertEquals(412.5f, s.lux, 0.001f)
        assertEquals(57, s.ir)
        assertTrue(s.known)
    }

    @Test
    fun unknownUntilFirstReading() {
        val s = HizashiState()
        assertFalse(s.known)
        assertEquals("—", s.label)
        assertEquals("—", s.detail)
    }

    @Test
    fun labelIsLeanAndDetailCarriesIr() {
        val s = fold("lux=412.5 ir=57")
        assertEquals("412 lx", s.label)          // Glass: whole lux, nothing else
        assertEquals("412 lx   IR 57", s.detail) // Operator: the visible/IR comparison
    }

    /** Zero lux is a READING (a dark room), not an absence — it must not read as unknown. */
    @Test
    fun zeroLuxIsAReadingNotAnAbsence() {
        val s = fold("lux=0.0 ir=0")
        assertTrue(s.known)
        assertEquals("0 lx", s.label)
    }

    /** A payload that parses neither field is not evidence a sensor exists. */
    @Test
    fun garbagePayloadKeepsPreviousAndDoesNotClaimPresence() {
        assertFalse(fold("").known)
        assertFalse(fold("nonsense").known)
        val prev = fold("lux=100.0 ir=20")
        val after = fold("nonsense", prev)
        assertEquals(100f, after.lux, 0.001f)
        assertEquals(20, after.ir)
    }

    /** A partial payload keeps the missing half rather than zeroing it. */
    @Test
    fun partialPayloadKeepsTheMissingField() {
        val prev = fold("lux=100.0 ir=20")
        val after = fold("lux=250.0", prev)
        assertEquals(250f, after.lux, 0.001f)
        assertEquals(20, after.ir)
    }

    // ── merge: Hizashi is fwd-preferred (head reads the light being looked at; a pack
    // sensor reads the wearer's own shadow). See CONTRACT.md's authority table.

    private fun pod(v: String) =
        ShintaiReadings(connection = ConnectionState.Live, hizashi = fold(v))

    @Test
    fun fwdWinsWhenBothPodsSupply() {
        val merged = mergeReadings(mapOf(Role.Fwd to pod("lux=900.0 ir=80"), Role.Aft to pod("lux=12.0 ir=5")))
        assertEquals(900f, merged.hizashi.lux, 0.001f)
    }

    /** A preferred-but-absent pod never beats a present one — the skip-invalid rule. */
    @Test
    fun aftWinsWhenFwdHasNoSensor() {
        val merged = mergeReadings(
            mapOf(Role.Fwd to ShintaiReadings(connection = ConnectionState.Live),
                  Role.Aft to pod("lux=12.0 ir=5")),
        )
        assertEquals(12f, merged.hizashi.lux, 0.001f)
        assertTrue(merged.hizashi.known)
    }

    @Test
    fun noPodSupplyingLeavesItUnknown() {
        val merged = mergeReadings(mapOf(Role.Fwd to ShintaiReadings(connection = ConnectionState.Live)))
        assertFalse(merged.hizashi.known)
    }

    @Test
    fun suppliedChannelsIncludesHizashiOnlyWhenKnown() {
        assertTrue(Channel.Hizashi in pod("lux=5.0 ir=1").suppliedChannels())
        assertFalse(Channel.Hizashi in ShintaiReadings().suppliedChannels())
    }
}
