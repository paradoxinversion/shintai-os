package com.saboteur.shintai.core

/**
 * Bunshin (分身) — federating two producer pods into one perception.
 *
 * Two boards run the identical firmware and split the sensor set by what's plugged
 * into each ([Role.Fwd] head-side, [Role.Aft] pack-side). Each pod streams its own
 * [ShintaiReadings]; [mergeReadings] folds the two into a single readings object the
 * apps' UIs consume unchanged. When both pods supply the same channel, a per-channel
 * precedence order (the contract's authority table) decides the winner — see
 * `CONTRACT.md` → "Multi-producer model (Bunshin)". This is the reference
 * implementation the glasses and the ground-station follow.
 */

/** Which physical pod a reading came from — one of Bunshin's two hosts. */
enum class Role { Fwd, Aft }

/**
 * The channel groups the authority table arbitrates. Each maps to a set of
 * [ShintaiReadings] fields that share one producing sensor, so they always resolve to
 * the same pod together (e.g. [Distance] carries the packed text, the parsed arcs, and
 * the alert latch; [Environment] carries the raw readout and its derived Kyūkaku state).
 */
enum class Channel {
    Distance, Heading, Accel, Thermal, Climate, Environment, Gps, Hokan, Lightning, Particulate,
    Fuda, Presence, RadarPeaks,
}

/** Per-channel precedence order, highest-priority pod first. */
typealias Precedence = Map<Channel, List<Role>>

/**
 * The contract-default authority table (`CONTRACT.md` → Multi-producer model). Shared
 * by every consumer; Operator may override rows at runtime, Glass and the
 * ground-station use these defaults.
 */
val DEFAULT_PRECEDENCE: Precedence = mapOf(
    Channel.Distance to listOf(Role.Aft, Role.Fwd),      // rear arc (Kōei) lives on the pack
    Channel.Heading to listOf(Role.Fwd, Role.Aft),       // HUD wants head orientation
    Channel.Accel to listOf(Role.Fwd, Role.Aft),         // head IMU
    Channel.Thermal to listOf(Role.Fwd, Role.Aft),       // forward-looking thermal
    Channel.Climate to listOf(Role.Aft, Role.Fwd),       // air chem rides the pack
    Channel.Environment to listOf(Role.Aft, Role.Fwd),   // air chem rides the pack
    Channel.Gps to listOf(Role.Fwd, Role.Aft),           // fix-gated (a no-fix pod supplies nothing)
    Channel.Hokan to listOf(Role.Aft, Role.Fwd),         // torso pedometer beats head-bob
    Channel.Lightning to listOf(Role.Aft, Role.Fwd),     // ambient storm sense rides the pack
    Channel.Particulate to listOf(Role.Aft, Role.Fwd),   // Enmu: the air you breathe is direction-agnostic
    Channel.Fuda to listOf(Role.Fwd, Role.Aft),          // Fuda: you scan the tag you're facing — head-side
    Channel.Presence to listOf(Role.Fwd, Role.Aft),      // Hitokage: forward presence rides the head
    Channel.RadarPeaks to listOf(Role.Fwd, Role.Aft),    // Hitokage: same pod as Presence — same sensor
)

/** The blank sentinel a string channel carries until its sensor supplies a value. */
private const val BLANK = "—"

/** The no-fix form of the GPS channel: a present receiver reporting that it has no lock
 *  ("no fix sats=N"). It is a *rendered* value — it shows "searching" instead of a dash —
 *  but NOT a supplied one, so it never wins GPS precedence over a pod holding a real fix. */
private const val GPS_NO_FIX_PREFIX = "no fix"

/** Does this pod currently supply a *valid* value for [ch]? Invalid sources are
 *  skipped before precedence, so a preferred-but-absent pod never beats a present one.
 *  GPS is fix-gated EXPLICITLY: the firmware notifies whenever the receiver is present,
 *  so silence no longer means "no fix" — it means "no receiver". Without the prefix test
 *  a searching fwd pod would outrank an aft pod that actually has a lock. */
private fun ShintaiReadings.supplies(ch: Channel): Boolean = when (ch) {
    Channel.Distance -> distanceText != BLANK || rearDepthGrid != null
    Channel.Heading -> heading != BLANK
    Channel.Accel -> accel != BLANK
    Channel.Thermal -> thermal != BLANK || thermalGrid != null
    Channel.Climate -> climate != BLANK
    Channel.Environment -> environment != BLANK
    Channel.Gps -> gps != BLANK && !gps.startsWith(GPS_NO_FIX_PREFIX)
    Channel.Hokan -> hokan != null
    Channel.Lightning -> lightning.hasStrike
    Channel.Particulate -> particulate != BLANK
    Channel.Fuda -> fuda != BLANK
    Channel.Presence -> presence != BLANK
    Channel.RadarPeaks -> radarPeaks != BLANK
}

/** The channels this pod currently supplies a valid value for. The Operator uses this
 *  to tell which channels are *contested* (supplied by more than one pod) and so worth a
 *  precedence control. */
fun ShintaiReadings.suppliedChannels(): Set<Channel> =
    Channel.values().filterTo(mutableSetOf()) { supplies(it) }

/** Rank for merging connection state: the most-connected pod wins (any Live → Live). */
private fun ConnectionState.rank(): Int = when (this) {
    ConnectionState.Idle -> 0
    ConnectionState.PermissionNeeded -> 1
    ConnectionState.Disconnected -> 2
    ConnectionState.Connecting -> 3
    ConnectionState.Discovering -> 4
    ConnectionState.Live -> 5
}

/**
 * Fold the per-pod readings into one merged snapshot using [precedence].
 *
 * For each channel: take the highest-precedence pod that currently [supplies] a valid
 * value; if none do, the field stays blank/null. `packets` sums across pods, the merged
 * `connection` is the most-connected pod's state, and `perBoard` records each pod's own
 * connection so the UI can show per-pod liveness. Pure — same inputs always yield the
 * same output — so a live precedence change just re-runs it.
 *
 * Single-pod ([perPod] of size 1) is transparent: every channel the lone pod supplies
 * comes from it, the rest stay blank — identical to that pod's own readings.
 */
/**
 * The OLED pane is NOT a sensor channel and has no precedence: each pod drives its own
 * panel. Take the first pod that has reported one so the console has something to render;
 * the per-pod truth stays in `perPod` for a UI that wants to page pods separately.
 *
 * Split out of [mergeReadings] to keep it under detekt's complexity ceiling — the merge
 * body is one long constructor call and every extra branch in it costs against the limit.
 */
private fun pickPane(perPod: Map<Role, ShintaiReadings>): PaneState =
    perPod.values.firstOrNull { it.pane.known }?.pane ?: PaneState()

/**
 * GPS: a fix beats everything; with no fix anywhere, fall back to any pod's no-fix report
 * so the UI shows "searching" rather than the dash that means "no receiver at all".
 * Split out of [mergeReadings] for the same complexity-ceiling reason as [pickPane].
 */
private fun pickGps(perPod: Map<Role, ShintaiReadings>, winner: ShintaiReadings?): String =
    winner?.gps
        ?: perPod.values.map { it.gps }.firstOrNull { it != BLANK }
        ?: BLANK

fun mergeReadings(
    perPod: Map<Role, ShintaiReadings>,
    precedence: Precedence = DEFAULT_PRECEDENCE,
): ShintaiReadings {
    fun winner(ch: Channel): ShintaiReadings? =
        (precedence[ch] ?: DEFAULT_PRECEDENCE.getValue(ch))
            .firstOrNull { perPod[it]?.supplies(ch) == true }
            ?.let { perPod[it] }

    val distance = winner(Channel.Distance)
    val thermal = winner(Channel.Thermal)
    val environment = winner(Channel.Environment)

    return ShintaiReadings(
        connection = perPod.values.maxByOrNull { it.connection.rank() }?.connection
            ?: ConnectionState.Idle,
        distanceText = distance?.distanceText ?: BLANK,
        distanceMm = distance?.distanceMm,
        distanceLMm = distance?.distanceLMm,
        distanceRMm = distance?.distanceRMm,
        rearDepthGrid = distance?.rearDepthGrid,
        alertActive = distance?.alertActive ?: false,
        heading = winner(Channel.Heading)?.heading ?: BLANK,
        accel = winner(Channel.Accel)?.accel ?: BLANK,
        gps = pickGps(perPod, winner(Channel.Gps)),
        climate = winner(Channel.Climate)?.climate ?: BLANK,
        particulate = winner(Channel.Particulate)?.particulate ?: BLANK,
        fuda = winner(Channel.Fuda)?.fuda ?: BLANK,
        presence = winner(Channel.Presence)?.presence ?: BLANK,
        radarPeaks = winner(Channel.RadarPeaks)?.radarPeaks ?: BLANK,
        pane = pickPane(perPod),
        thermal = thermal?.thermal ?: BLANK,
        environment = environment?.environment ?: BLANK,
        kyukaku = environment?.kyukaku ?: KyukakuState(),
        thermalGrid = thermal?.thermalGrid,
        hokan = winner(Channel.Hokan)?.hokan,
        lightning = winner(Channel.Lightning)?.lightning ?: LightningState(),
        lightningConfig = winner(Channel.Lightning)?.lightningConfig ?: LightningConfig(),
        packets = perPod.values.sumOf { it.packets },
        perBoard = perPod.mapValues { it.value.connection },
    )
}
