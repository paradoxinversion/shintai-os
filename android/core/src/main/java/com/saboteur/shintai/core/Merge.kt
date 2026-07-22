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
enum class Channel { Distance, Heading, Accel, Thermal, Climate, Environment, Gps, Hokan, Lightning }

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
)

/** The blank sentinel a string channel carries until its sensor supplies a value. */
private const val BLANK = "—"

/** Does this pod currently supply a *valid* value for [ch]? Invalid sources are
 *  skipped before precedence, so a preferred-but-absent pod never beats a present one.
 *  GPS is "fix-gated" for free: with no fix the channel stays [BLANK], so it's skipped. */
private fun ShintaiReadings.supplies(ch: Channel): Boolean = when (ch) {
    Channel.Distance -> distanceText != BLANK || rearDepthGrid != null
    Channel.Heading -> heading != BLANK
    Channel.Accel -> accel != BLANK
    Channel.Thermal -> thermal != BLANK || thermalGrid != null
    Channel.Climate -> climate != BLANK
    Channel.Environment -> environment != BLANK
    Channel.Gps -> gps != BLANK
    Channel.Hokan -> hokan != null
    Channel.Lightning -> lightning.hasStrike
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
        gps = winner(Channel.Gps)?.gps ?: BLANK,
        climate = winner(Channel.Climate)?.climate ?: BLANK,
        thermal = thermal?.thermal ?: BLANK,
        environment = environment?.environment ?: BLANK,
        kyukaku = environment?.kyukaku ?: KyukakuState(),
        thermalGrid = thermal?.thermalGrid,
        hokan = winner(Channel.Hokan)?.hokan,
        lightning = winner(Channel.Lightning)?.lightning ?: LightningState(),
        packets = perPod.values.sumOf { it.packets },
        perBoard = perPod.mapValues { it.value.connection },
    )
}
