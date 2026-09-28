package com.saboteur.shintai.core

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * [Role.Ground] — the desk station. Same firmware, same contract, different PLACE: it
 * senses the room rather than the wearer, so [mergeReadings] must keep it out of every
 * channel while still reporting it as connected. These tests pin the exclusion, because
 * a regression here would silently assert the room's environment as the body's.
 */
class GroundRoleTest {

    private fun live(climate: String = "—", gps: String = "—", lux: Float? = null) =
        ShintaiReadings(
            connection = ConnectionState.Live,
            climate = climate,
            gps = gps,
            hizashi = lux?.let { HizashiState(it, 1, true) } ?: HizashiState(),
        )

    // ── wire tokens: the CSV `board` value and the ShintaiOS-<token> suffix ──

    @Test
    fun tokensAreThreeLetterAndStable() {
        assertEquals("fwd", Role.Fwd.token)
        assertEquals("aft", Role.Aft.token)
        // NOT "ground": the CSV has carried three-letter roles since Bunshin, and
        // name.lowercase() would have silently written "ground".
        assertEquals("gnd", Role.Ground.token)
    }

    @Test
    fun tokensRoundTrip() {
        Role.values().forEach { assertEquals(it, roleFromToken(it.token)) }
        assertEquals(Role.Ground, roleFromToken("GND"))
        assertNull(roleFromToken("nope"))
        assertNull(roleFromToken(null))
    }

    @Test
    fun onlyGroundIsNonBody() {
        assertTrue(Role.Fwd.isBody)
        assertTrue(Role.Aft.isBody)
        assertFalse(Role.Ground.isBody)
    }

    // ── the exclusion itself ──

    @Test
    fun groundNeverSuppliesAMergedChannel() {
        val merged = mergeReadings(mapOf(Role.Ground to live(climate = "19.0C 55%RH 480ppm", lux = 900f)))
        assertEquals("—", merged.climate)
        assertFalse(merged.hizashi.known)
    }

    /** The sharp case: ground must not fill a channel merely because no body pod has it. */
    @Test
    fun groundDoesNotFillAGapLeftByTheBodyPods() {
        val merged = mergeReadings(
            mapOf(
                Role.Fwd to live(),                                   // connected, supplies nothing
                Role.Ground to live(climate = "19.0C 55%RH 480ppm"),  // the desk has air data
            ),
        )
        assertEquals("—", merged.climate)
    }

    @Test
    fun bodyPodsStillMergeNormallyAlongsideAGroundPod() {
        val merged = mergeReadings(
            mapOf(
                Role.Aft to live(climate = "23.0C 41%RH 750ppm"),
                Role.Ground to live(climate = "19.0C 55%RH 480ppm"),
            ),
        )
        assertEquals("23.0C 41%RH 750ppm", merged.climate)   // aft wins Climate, ground ignored
    }

    /** GPS has its own no-fix fallback path — ground must be excluded from that too. */
    @Test
    fun groundIsExcludedFromTheGpsNoFixFallback() {
        val merged = mergeReadings(mapOf(Role.Ground to live(gps = "no fix sats=4")))
        assertEquals("—", merged.gps)
    }

    /** …but a ground pod is still visibly CONNECTED, so a console can render it. */
    @Test
    fun groundStillAppearsInPerBoard() {
        val merged = mergeReadings(
            mapOf(Role.Fwd to live(), Role.Ground to live(climate = "19.0C 55%RH 480ppm")),
        )
        assertEquals(ConnectionState.Live, merged.perBoard[Role.Ground])
        assertTrue(Role.Ground in merged.perBoard)
    }

    @Test
    fun aGroundOnlyRigMergesToNothing() {
        val merged = mergeReadings(mapOf(Role.Ground to live(climate = "19.0C 55%RH 480ppm", lux = 900f)))
        assertTrue(merged.suppliedChannels().isEmpty())
    }
}
