package com.saboteur.shintai.core

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * Bunshin merge reducer ([mergeReadings]) — the skip-invalid-then-precedence rule
 * from `CONTRACT.md`'s authority table. Pods are built with only the channels under
 * test set; everything else stays blank/null so a pod "supplies" exactly what we mean.
 */
class MergeTest {

    private fun withHeading(v: String, conn: ConnectionState = ConnectionState.Live) =
        ShintaiReadings(connection = conn, heading = v)

    private fun withDistance(text: String, near: Int?, alert: Boolean) =
        ShintaiReadings(
            connection = ConnectionState.Live,
            distanceText = text, distanceMm = near, distanceLMm = near, alertActive = alert,
        )

    @Test
    fun defaultPrecedenceFwdWinsHeading() {
        val merged = mergeReadings(
            mapOf(Role.Fwd to withHeading("10.0° N"), Role.Aft to withHeading("200.0° S")),
        )
        assertEquals("10.0° N", merged.heading)   // heading default: fwd → aft
    }

    @Test
    fun defaultPrecedenceAftWinsDistance() {
        val merged = mergeReadings(
            mapOf(
                Role.Fwd to withDistance("L:900 R:-- mm", 900, alert = false),
                Role.Aft to withDistance("L:150 R:300 mm", 150, alert = true),
            ),
        )
        assertEquals("L:150 R:300 mm", merged.distanceText)   // distance default: aft → fwd
        assertEquals(150, merged.distanceMm)
        assertTrue(merged.alertActive)                        // alert travels with the distance winner
    }

    @Test
    fun skipInvalidFallsBackToLowerPrecedencePod() {
        // Heading prefers fwd, but fwd has no magnetometer (blank) — aft must win.
        val merged = mergeReadings(
            mapOf(Role.Fwd to ShintaiReadings(), Role.Aft to withHeading("88.0° E")),
        )
        assertEquals("88.0° E", merged.heading)
    }

    @Test
    fun overridePrecedenceFlipsWinner() {
        val override = DEFAULT_PRECEDENCE + (Channel.Heading to listOf(Role.Aft, Role.Fwd))
        val merged = mergeReadings(
            mapOf(Role.Fwd to withHeading("10.0° N"), Role.Aft to withHeading("200.0° S")),
            override,
        )
        assertEquals("200.0° S", merged.heading)   // override: aft now wins heading
    }

    @Test
    fun gpsIsFixGatedViaSkipInvalid() {
        // fwd is default GPS winner but has no fix (blank); aft has a fix → aft wins.
        val merged = mergeReadings(
            mapOf(
                Role.Fwd to ShintaiReadings(gps = "—"),
                Role.Aft to ShintaiReadings(connection = ConnectionState.Live, gps = "37.1,-122.1 12m 3km/h"),
            ),
        )
        assertEquals("37.1,-122.1 12m 3km/h", merged.gps)
    }

    @Test
    fun singlePodIsTransparent() {
        val pod = withHeading("45.0° NE").copy(accel = "X:1.0 Y:0.0 Z:9.8", packets = 7)
        val merged = mergeReadings(mapOf(Role.Fwd to pod))
        assertEquals("45.0° NE", merged.heading)
        assertEquals("X:1.0 Y:0.0 Z:9.8", merged.accel)
        assertEquals(7, merged.packets)
        assertEquals(mapOf(Role.Fwd to ConnectionState.Live), merged.perBoard)
        assertEquals("—", merged.gps)   // a channel the lone pod lacks stays blank
    }

    @Test
    fun packetsSumAndPerBoardRecordsEachPod() {
        val merged = mergeReadings(
            mapOf(
                Role.Fwd to withHeading("10.0° N", ConnectionState.Live).copy(packets = 3),
                Role.Aft to withHeading("20.0° N", ConnectionState.Connecting).copy(packets = 4),
            ),
        )
        assertEquals(7, merged.packets)
        assertEquals(ConnectionState.Live, merged.connection)   // most-connected pod wins
        assertEquals(ConnectionState.Live, merged.perBoard[Role.Fwd])
        assertEquals(ConnectionState.Connecting, merged.perBoard[Role.Aft])
    }

    @Test
    fun emptyMapYieldsDefaults() {
        val merged = mergeReadings(emptyMap())
        assertEquals("—", merged.heading)
        assertEquals(0, merged.packets)
        assertNull(merged.hokan)
        assertFalse(merged.alertActive)
        assertTrue(merged.perBoard.isEmpty())
    }
}
