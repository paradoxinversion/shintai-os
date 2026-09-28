// The Operator screen is a stack of panel @Composables (one per instrument cluster),
// so the file-level function-count rule doesn't apply here — same call as ui/Components.kt.
@file:Suppress("TooManyFunctions")

package com.saboteur.shintaioperator

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawingPadding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.background
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.saboteur.shintai.core.Channel
import com.saboteur.shintai.core.ConnectionState
import kotlinx.coroutines.delay
import com.saboteur.shintai.core.HokanPdr
import com.saboteur.shintai.core.LightningConfig
import com.saboteur.shintai.core.LightningState
import com.saboteur.shintai.core.PaneState
import com.saboteur.shintai.core.Precedence
import com.saboteur.shintai.core.Role
import com.saboteur.shintai.core.NEAR_MM
import com.saboteur.shintai.core.ShintaiReadings
import com.saboteur.shintai.core.Smell
import com.saboteur.shintai.core.Units
import com.saboteur.shintai.core.distanceParts
import com.saboteur.shintai.core.label
import com.saboteur.shintai.core.AirReadout

import com.saboteur.shintai.core.formatFuda
import com.saboteur.shintai.core.formatGps
import com.saboteur.shintai.core.formatTemp
import com.saboteur.shintai.core.formatThermal
import com.saboteur.shintai.core.parseRadarPeaks
import com.saboteur.shintai.core.suppliedChannels
import com.saboteur.shintaioperator.ui.AScope
import com.saboteur.shintaioperator.ui.AlertBanner
import com.saboteur.shintaioperator.ui.ConsoleButton
import com.saboteur.shintaioperator.ui.DepthField
import com.saboteur.shintaioperator.ui.HeatGrid
import com.saboteur.shintaioperator.ui.HokanMap
import com.saboteur.shintaioperator.ui.LogTerminal
import com.saboteur.shintaioperator.ui.Panel
import com.saboteur.shintaioperator.ui.ReadoutRow
import com.saboteur.shintaioperator.ui.ScopeBlip
import com.saboteur.shintaioperator.ui.SegmentBar
import com.saboteur.shintaioperator.ui.Sparkline
import com.saboteur.shintaioperator.ui.StatusLed
import com.saboteur.shintaioperator.ui.TrackerGauge
import kotlin.math.roundToInt


/**
 * The sentinel a string channel carries until its characteristic has notified. Mirrors
 * `Merge.kt`'s private BLANK — a channel still showing this has never reported, which on
 * this contract means its sensor is absent (a characteristic only notifies when its sensor
 * is present and has data).
 */
private const val BLANK = "—"

private fun String.reported(): Boolean = this != BLANK

/**
 * A [ReadoutRow] that renders NOTHING when its channel has never reported. On a partial
 * rig most of the console was dashes — rows for hardware that isn't fitted. Kept as a
 * helper rather than an `if` at each call site so the panel bodies stay flat and don't
 * each cost a branch against detekt's complexity ceiling.
 *
 * Note what this deliberately does NOT hide: a channel that has reported but currently has
 * no value. A GPS with no lock still notifies ("no fix sats=N"), so its row stays and reads
 * "searching" — absent hardware and unlocked hardware are different claims.
 */
@Composable
private fun OptionalRow(
    label: String,
    value: String,
    unit: String = "",
    valueColor: Color = T.Phosphor,
) {
    if (value.reported()) ReadoutRow(label, value, unit, valueColor)
}

/** The nullable-field twin of [OptionalRow] — skips the row when the value is absent. */
@Composable
private fun OptionalRow(label: String, value: String?, unit: String = "") {
    if (value != null) ReadoutRow(label, value, unit)
}

/** The Operator's pocket mission-control surface. VOID ground, chamfered panels,
 *  the motion tracker as the hero, and the full sensor cluster the glasses skip. */
@Composable
fun OperatorScreen(vm: OperatorViewModel, onRequestPermissions: () -> Unit) {
    val r by vm.readings.collectAsState()
    val scanning by vm.scanning.collectAsState()
    val devices by vm.scan.collectAsState()
    val rec by vm.recording.collectAsState()
    val logLines by vm.log.collectAsState()
    val distHist by vm.distanceHistory.collectAsState()
    val co2Hist by vm.co2History.collectAsState()
    val pairingOpen by vm.pairingOpen.collectAsState()
    val units = vm.units

    Column(
        Modifier
            .fillMaxSize()
            .background(T.Void)
            .safeDrawingPadding()
            .verticalScroll(rememberScrollState())
            .padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(14.dp),
    ) {
        TopBar(r, rec)

        when {
            r.connection == ConnectionState.PermissionNeeded -> AccessPanel(onRequestPermissions)
            // First link, or re-opened to add the second pod over the live console.
            r.connection == ConnectionState.Idle ->
                PairPanel(scanning, devices, vm, r.perBoard.keys, onBack = null)
            pairingOpen ->
                PairPanel(scanning, devices, vm, r.perBoard.keys, onBack = vm::closePairing)
            else -> Console(r, units, distHist, co2Hist, rec.active, vm)
        }

        Spacer(Modifier.height(2.dp))
        LogTerminal(logLines)
    }
}

@Composable
private fun TopBar(r: ShintaiReadings, rec: RecordingUi) {
    Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
        Text(
            "SHINTAI-OS", color = T.Bone, fontFamily = T.Title,
            fontSize = 14.sp, letterSpacing = 2.sp,
        )
        Text("  ▸ OPERATOR", color = T.PhosphorDim, fontFamily = T.Mono, fontSize = 13.sp, letterSpacing = 2.sp)
        Spacer(Modifier.weight(1f))
        if (rec.active) {
            StatusLed(T.Alert, blink = true)
            Text(" REC ${rec.rows}", color = T.Alert, fontFamily = T.Mono, fontSize = 12.sp)
            Spacer(Modifier.width(10.dp))
        }
        StatusLed(ledColor(r.connection), blink = r.connection == ConnectionState.Disconnected)
        Text(
            "  ${stateLabel(r.connection)}${if (r.packets > 0) " · rx ${r.packets}" else ""}",
            color = if (r.connection == ConnectionState.Live) T.Phosphor else T.BoneDim,
            fontFamily = T.Mono, fontSize = 12.sp,
        )
        // Bunshin: per-pod liveness — a dropped pod shows here even while the other stays live.
        r.perBoard.entries.sortedBy { it.key.name }.forEach { (role, st) ->
            Spacer(Modifier.width(8.dp))
            StatusLed(ledColor(st), blink = st == ConnectionState.Disconnected)
            Text(
                " ${role.name.uppercase()}",
                color = if (st == ConnectionState.Live) T.Phosphor else T.BoneDim,
                fontFamily = T.Mono, fontSize = 11.sp,
            )
        }
    }
}

/** Hitokage's multi-peak A-scope panel (specs/zokyo/hitokage.md). `RADAR_MAX_RANGE_MM`
 *  (5000mm) is the firmware's own `HITOKAGE_DIST_RANGE_END_MM` — kept in sync by hand
 *  since the peak list's range span isn't itself a contract field to check against.
 *  The raw payload stays visible below the scope for reference. */
private const val RADAR_MAX_RANGE_MM = 5000

@Composable
private fun RadarPeaksPanel(radarPeaks: String, units: Units) {
    // distColor is the SAME near=red/far=green language TrackerGauge's Proximity panel
    // uses — reusing it (rather than a scope-local palette) means a blip's colour needs
    // no separate legend to read.
    val blips = parseRadarPeaks(radarPeaks).map { ScopeBlip(it.distanceMm, it.strengthRaw, distColor(it.distanceMm)) }
    Panel("Radar Peaks", ledColor = T.Amber) {
        AScope(blips, RADAR_MAX_RANGE_MM, units, modifier = Modifier.fillMaxWidth().height(130.dp))
        Spacer(Modifier.height(6.dp))
        ReadoutRow("Peaks", radarPeaks)
    }
}

/**
 * Presence predicates for the gated panels. Deliberately expression bodies rather than
 * inline `if` conditions: detekt caps both a function's cyclomatic complexity and the
 * operand count of a single condition, and the panels these feed were already at the
 * ceiling. Naming them also states the rule once instead of at each call site.
 */
private fun ShintaiReadings.hasProximity(): Boolean =
    distanceText.reported() || rearDepthGrid != null

private fun ShintaiReadings.hasTelemetry(): Boolean =
    heading.reported() || accel.reported() || gps.reported() || thermal.reported()

/** Any of the five quantities AirReadout resolves from the Climate/Environment pair. */
private fun AirReadout.hasAny(): Boolean =
    co2Ppm != null || tempC != null || humidityPct != null ||
        pressureHpa != null || gasKohm != null

/** Air is fed by five sensors independently (SCD-40, BME688, Enmu, Hitokage, Hizashi);
 *  any one of them earns the panel. */
private fun ShintaiReadings.hasAirChannel(air: AirReadout): Boolean =
    air.hasAny() || particulate.reported() || presence.reported() || hizashi.known

/** CO₂ value + its segment bar — absent entirely without an SCD-40. */
@Composable
private fun AirCo2(ppm: Int?) {
    if (ppm == null) return
    ReadoutRow("CO₂", "$ppm", "ppm", valueColor = co2Color(ppm))
    Spacer(Modifier.height(6.dp))
    SegmentBar(
        value = ppm.toFloat(),
        fullScale = Bands.CO2_FULLSCALE_PPM.toFloat(),
        warn = Bands.CO2_WARN_PPM.toFloat(),
        alarm = Bands.CO2_ALARM_PPM.toFloat(),
    )
    Spacer(Modifier.height(8.dp))
}

/**
 * Kyūkaku's derived smell. It has NO BLE channel of its own — it is folded from
 * Environment's gas_ohms — so it lives or dies with that channel rather than with a
 * value of its own. Violet is its identity, distinct from the CO₂ green→red ramp.
 */
@Composable
private fun NoseRow(r: ShintaiReadings) {
    if (!r.environment.reported()) return
    ReadoutRow(
        "Nose", r.kyukaku.smell.label.uppercase(), "r ${"%.2f".format(r.kyukaku.ratio)}",
        valueColor = when (r.kyukaku.smell) {
            Smell.Settling -> T.BoneDim
            Smell.Clean -> T.Phosphor
            else -> T.Violet
        },
    )
}

/** AIR — the climate + environment cluster plus Kyūkaku's derived smell. ENVIRONMENT
 *  is the channel the glasses take only for the smell spike; the Operator shows it in
 *  full. Kyūkaku (no BLE channel of its own — see :core Kyukaku.kt) is folded from the
 *  environment's gas_ohms; violet is its identity, distinct from the CO₂ green→red ramp. */
@Composable
private fun AirPanel(r: ShintaiReadings, units: Units) {
    val air = AirReadout.resolve(r.climate, r.environment)
    val ppm = air.co2Ppm
    // Nothing in this panel has reported -> no panel at all, rather than a box of dashes.
    if (!r.hasAirChannel(air)) return
    Panel("Air") {
        AirCo2(ppm)
        // Quantity-first, not source-first. CONTRACT.md makes air temp / humidity SHARED
        // slots (BME688 → SCD-40), which the CSV honours; rendering the raw Climate and
        // Environment payloads instead showed an empty Climate row on a BME-only board
        // while the same temperature and humidity sat inside the Environment blob. These
        // rows mirror the CSV columns, so what the console shows and what gets logged
        // finally agree. AirReadout owns the precedence rule for both apps.
        val (tVal, tUnit) = air.tempParts(units)
        OptionalRow("Temperature", tVal.takeIf { air.tempC != null }, tUnit)
        OptionalRow("Humidity", air.humidityPct?.let { "%.0f".format(it) }, "%RH")
        OptionalRow("Pressure", air.pressureHpa?.let { "%.1f".format(it) }, "hPa")
        OptionalRow("Gas", air.gasKohm?.let { "%.1f".format(it) }, "kΩ")
        // Enmu: particulate (µg/m³ — unit-agnostic, rendered raw). The full PM1.0/2.5/10
        // triad on the console; the glasses show only the health-relevant PM2.5.
        OptionalRow("Particulate", r.particulate)
        // Hitokage: radar presence/range, rendered raw — no derived/lean split, both apps
        // show the same payload (there's no PM2.5-style "one number that matters" here).
        OptionalRow("Presence", r.presence)
        // Hizashi: the console gets illuminance AND the raw IR channel — the visible/IR
        // ratio is what separates daylight from incandescent from LED, and that comparison
        // is console work. The HUD takes the lux alone.
        OptionalRow("Ambient Light", r.hizashi.detail)
        NoseRow(r)
    }
    if (ppm != null && ppm >= Bands.CO2_ALARM_PPM) {
        AlertBanner("CO₂ $ppm ppm — ventilate", alarm = true)
    }
    val smell = r.kyukaku.smell
    if (smell == Smell.Spike || smell == Smell.Foul) {
        AlertBanner(
            if (smell == Smell.Spike) "Nose — the air just changed" else "Nose — air is foul",
            alarm = false, color = T.Violet,
        )
    }
}

/**
 * Rolling range / CO₂ history. Each sparkline appears only once its channel has produced
 * samples, and the panel is skipped entirely when neither has — an empty sparkline draws a
 * flat grey line that reads as data rather than as absence. Extracted from [Console] to
 * keep that function inside detekt's method-length budget.
 */
@Composable
private fun TrendPanel(distHist: List<Float>, co2Hist: List<Float>) {
    if (distHist.isEmpty() && co2Hist.isEmpty()) return
    Panel("Trend") {
        if (distHist.isNotEmpty()) {
            Text("RANGE", color = T.Bone, fontFamily = T.Mono, fontSize = 11.sp, letterSpacing = 2.sp)
            Sparkline(distHist, T.Phosphor)
        }
        if (distHist.isNotEmpty() && co2Hist.isNotEmpty()) Spacer(Modifier.height(8.dp))
        if (co2Hist.isNotEmpty()) {
            Text("CO₂", color = T.Bone, fontFamily = T.Mono, fontSize = 11.sp, letterSpacing = 2.sp)
            Sparkline(co2Hist, T.Amber)
        }
    }
}

/**
 * Connected, but not one sensor channel has reported. With every panel gated on presence
 * the honest render of a bare board is an empty scroll, which reads as a broken app —
 * so say it plainly instead. Keyed on :core's suppliedChannels(), the same presence test
 * the Bunshin merge uses, so the console and the merge agree on what "reporting" means.
 */
@Composable
private fun NoChannelsNotice(r: ShintaiReadings) {
    if (r.suppliedChannels().isNotEmpty()) return
    Panel("Status") {
        Text("No sensor channels reporting.", color = T.Bone, fontFamily = T.Mono, fontSize = 13.sp)
        Text(
            "A channel appears when its sensor notifies.",
            color = T.BoneDim, fontFamily = T.Mono, fontSize = 11.sp,
        )
    }
}

/**
 * PROXIMITY — the hero: motion tracker + the one big glanceable value. Present only once
 * the rear field has reported; [ShintaiReadings.hasProximity] is the same test Merge.kt's
 * supplies(Distance) uses, so the console and the merge agree. A rig with no ToF used to
 * show a full-height empty tracker gauge reading "—".
 */
@Composable
private fun ProximityPanel(r: ShintaiReadings, units: Units) {
    if (!r.hasProximity()) return
Panel("Proximity", ledColor = distColor(r.distanceMm)) {
    Row(verticalAlignment = Alignment.CenterVertically) {
        TrackerGauge(
            leftMm = r.distanceLMm,
            rightMm = r.distanceRMm,
            alert = r.alertActive,
            modifier = Modifier.weight(1f).height(190.dp),
        )
        Spacer(Modifier.width(12.dp))
        Column(horizontalAlignment = Alignment.End, modifier = Modifier.width(120.dp)) {
            val (v, u) = distanceParts(r.distanceMm, r.distanceText, units)
            val numeric = r.distanceMm != null
            // Hero = the NEARER arc (closest threat); the per-arc L/R breakdown sits
            // below it, each line coloured by its own proximity band.
            Text("NEAREST", color = T.Bone, fontFamily = T.Mono, fontSize = 11.sp, letterSpacing = 2.sp)
            Text(
                v, color = distColor(r.distanceMm),
                // The ONE glanceable value in 7-segment DSEG; the "—" placeholder
                // falls back to Plex (DSEG has no em dash).
                fontFamily = if (numeric) T.Numeral else T.Mono,
                fontWeight = if (numeric) FontWeight.Normal else FontWeight.Bold,
                fontSize = if (numeric) 56.sp else 44.sp,
            )
            Text(u.uppercase(), color = T.BoneDim, fontFamily = T.Mono, fontSize = 14.sp)
            Spacer(Modifier.height(8.dp))
            ArcReadout("L", r.distanceLMm, units)
            ArcReadout("R", r.distanceRMm, units)
        }
    }
    if (r.alertActive) {
        Spacer(Modifier.height(10.dp))
        AlertBanner("Object inside 0.2 m — hold", alarm = true)
    }
}
}

/**
 * TELEMETRY — motion & position, each row gated on its own channel and the panel dropped
 * when none of the four has reported. GPS is the subtle one: the firmware notifies
 * whenever the RECEIVER is present, fix or not, so a searching receiver keeps its row and
 * reads "no fix" — only a pod with no GPS hardware at all loses it.
 */
@Composable
private fun TelemetryPanel(r: ShintaiReadings, units: Units) {
    if (!r.hasTelemetry()) return
    Panel("Telemetry") {
        OptionalRow("Heading", r.heading)
        OptionalRow("Accel", r.accel)
        OptionalRow("GPS", formatGps(r.gps, units).takeIf { r.gps.reported() })
        OptionalRow("Thermal", formatThermal(r.thermal, units).takeIf { r.thermal.reported() })
    }
}

@Composable
private fun Console(
    r: ShintaiReadings,
    units: Units,
    distHist: List<Float>,
    co2Hist: List<Float>,
    recording: Boolean,
    vm: OperatorViewModel,
) {
    NoChannelsNotice(r)
    ProximityPanel(r, units)
    TelemetryPanel(r, units)

    // NAVIGATION — Hokan's pedometer + dead-reckoned path. Its own composable so
    // Console stays within the method-length budget; present only while the IMU
    // streams the Hokan channel.
    r.hokan?.let { NavigationPanel(it) }

    // THERMAL GRID — Metsuke's live heat image (the one binary channel). Present
    // once the MLX90640 is attached + streaming; absent otherwise, so no empty box.
    r.thermalGrid?.let { grid ->
        Panel("Thermal Grid", ledColor = T.Amber) {
            ReadoutRow("Range", "${formatTemp(grid.minC, units)} – ${formatTemp(grid.maxC, units)}")
            Spacer(Modifier.height(8.dp))
            HeatGrid(grid)
        }
    }

    // REAR FIELD — Zanshin's live 8×8 rear depth panel (the second binary channel).
    // Present once the VL53L5CX is attached + streaming; absent otherwise (no empty box).
    r.rearDepthGrid?.let { grid ->
        Panel("Rear Field", ledColor = T.Amber) {
            DepthField(grid)
        }
    }

    // RADAR PEAKS — Hitokage's multi-peak A-scope (specs/zokyo/hitokage.md). Present
    // only once the board has actually notified radarPeaks — a parsed empty peak list
    // is NOT the gate ("n=0, no peaks in range" is a valid live reading); gate on the
    // raw payload having arrived at all, same idiom as thermalGrid/rearDepthGrid above.
    // Stays absent when the module's on the presence-mode fallback (no peak list there).
    if (r.radarPeaks != "—") RadarPeaksPanel(r.radarPeaks, units)

    // AIR — climate + environment + Kyūkaku's derived smell. Its own composable so
    // Console stays within the method-length/complexity budget (same as NavigationPanel).
    AirPanel(r, units)

    // LIGHTNING — Enrai's AS3935 last-strike readout; the LED flashes on each new strike.
    // Gated on the CONFIG channel, not on strikes: Lightning notifies event-driven (only
    // when lightning actually strikes), so a present-but-quiet AS3935 would vanish on a
    // clear day. Lightning Control rides every telemetry cycle and reports "enrai=none"
    // when the sensor is absent, so it is the honest present/absent signal — and it keeps
    // "sensor fitted, no strikes yet" visibly different from "no sensor".
    if (r.lightningConfig.known) LightningPanel(r.lightning)

    // IDENTITY — Fuda's NFC tag. Its own category (not air); event-driven, so present
    // only once a tag has been read (then it persists the last one).
    if (r.fuda != "—") IdentityPanel(r)

    // TREND — rolling history the glasses don't keep. Gated on the histories themselves
    // rather than on the channels: a sparkline needs samples, and an empty one is a flat
    // grey line that says nothing.
    TrendPanel(distHist, co2Hist)

    // Bunshin: per-channel source precedence — appears only when two pods are linked.
    MultiPodSources(vm)

    // ── CONTROLS ────────────────────────────────────────────────────────────────────
    // Everything above reads the board; everything below acts on it. The board-actuating
    // panels sit together here rather than beside the readouts they relate to, so the
    // console has one control zone instead of two separated by a readout.

    // Enrai AS3935 tuning — writes tokens to the board over the Lightning Control char.
    // Shown only once a board reports a real config: a rig with nothing at 0x03 sends
    // "enrai=none", so the knobs don't appear for a sensor that isn't there. Safe to gate
    // now that the config rides the telemetry cycle — when it only notified on a write,
    // hiding the panel would have hidden the very control needed to produce one.
    if (r.lightningConfig.known) {
        LightningTuningPanel(r.lightningConfig, vm::sendLightningCommand)
    }

    // OLED PANE — page the board's tertiary panel from the console, the twin of the
    // on-body APDS swipe. Shown only once a board has reported a pane: a rig with no
    // panel at 0x3D never notifies one, so the control would have nothing to drive.
    if (r.pane.known) PanePanel(r.pane, vm::sendPaneCommand)

    // Bunshin: return to the pair screen to link the OTHER pod — until both are in.
    if (r.perBoard.size < 2) {
        ConsoleButton("Link Another Unit", vm::openPairing, modifier = Modifier.fillMaxWidth())
    }

    // Controls.
    Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(10.dp)) {
        ConsoleButton(
            if (recording) "Stop" else "Record", vm::toggleRecording,
            modifier = Modifier.weight(1f), active = recording,
        )
        ConsoleButton(
            if (units == Units.IMPERIAL) "Imperial" else "Metric", vm::toggleUnits,
            modifier = Modifier.weight(1f),
        )
        ConsoleButton("Unlink", vm::disconnect, modifier = Modifier.weight(1f))
    }
}

/** Bunshin: the Sources precedence control. When two pods are linked it lists every
 *  channel both supply (contested → a pod toggle) and those only one supplies (info),
 *  letting the wearer pick which pod wins each. Collects its own state so Console stays
 *  one line lighter, and self-hides with a single pod. */
@Composable
private fun MultiPodSources(vm: OperatorViewModel) {
    val podSupply by vm.podSupply.collectAsState()
    val precedence by vm.precedence.collectAsState()
    if (podSupply.size < 2) return
    Panel("Sources") {
        Text(
            "Which pod wins each shared channel",
            color = T.BoneDim, fontFamily = T.Mono, fontSize = 11.sp,
        )
        Spacer(Modifier.height(8.dp))
        Channel.values().forEach { ch ->
            val suppliers = podSupply.filterValues { ch in it }.keys.sortedBy { it.name }
            when {
                suppliers.size >= 2 -> ContestedRow(ch, suppliers, precedence[ch]?.firstOrNull(), vm)
                suppliers.size == 1 -> SingleSourceRow(ch, suppliers.first())
            }
        }
        Spacer(Modifier.height(8.dp))
        ConsoleButton("Reset defaults", vm::resetPrecedence, modifier = Modifier.fillMaxWidth())
    }
}

@Composable
private fun ContestedRow(ch: Channel, suppliers: List<Role>, preferred: Role?, vm: OperatorViewModel) {
    Row(
        Modifier.fillMaxWidth().padding(vertical = 4.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(6.dp),
    ) {
        Text(channelLabel(ch), color = T.Bone, fontFamily = T.Mono, fontSize = 13.sp, modifier = Modifier.weight(1f))
        suppliers.forEach { role ->
            ConsoleButton(role.name.uppercase(), { vm.setPreferred(ch, role) }, active = role == preferred)
        }
    }
}

@Composable
private fun SingleSourceRow(ch: Channel, role: Role) {
    Row(Modifier.fillMaxWidth().padding(vertical = 4.dp)) {
        Text(channelLabel(ch), color = T.BoneDim, fontFamily = T.Mono, fontSize = 13.sp, modifier = Modifier.weight(1f))
        Text("${role.name.uppercase()} only", color = T.BoneDim, fontFamily = T.Mono, fontSize = 12.sp)
    }
}

private fun channelLabel(ch: Channel): String = when (ch) {
    Channel.Distance -> "Proximity"
    Channel.Heading -> "Heading"
    Channel.Accel -> "Accel"
    Channel.Thermal -> "Thermal"
    Channel.Climate -> "Climate"
    Channel.Environment -> "Environment"
    Channel.Gps -> "GPS"
    Channel.Hokan -> "Steps"
    Channel.Lightning -> "Lightning"
    Channel.Particulate -> "Particulate"
    Channel.Fuda -> "NFC Tag"
    Channel.Presence -> "Presence"
    Channel.RadarPeaks -> "Radar Peaks"
    Channel.Hizashi -> "Ambient Light"
}

/** The board's OLED pane — the console twin of the on-body APDS swipe, over the writable
 *  Pane Control characteristic. PREV/NEXT mirror exactly what a swipe does (the firmware
 *  also accepts direct pane-name tokens; deliberately not surfaced here, so the pane
 *  vocabulary lives only in the firmware and CONTRACT.md and can't drift into the app).
 *
 *  The readout is whatever the BOARD last reported, never what we asked for — it notifies
 *  on a swipe as well as on a write, so this stays correct when someone pages by hand. */
@Composable
private fun PanePanel(pane: PaneState, onCmd: (String) -> Unit) {
    Panel("OLED Pane", ledColor = if (pane.known) T.Phosphor else T.PhosphorDim) {
        Row(
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(10.dp),
            modifier = Modifier.fillMaxWidth().padding(vertical = 4.dp),
        ) {
            ConsoleButton("< PREV", { onCmd("prev") })
            Column(modifier = Modifier.weight(1f)) {
                Text(
                    pane.name, color = T.Phosphor, fontFamily = T.Mono, fontSize = 15.sp,
                    letterSpacing = 2.sp,
                )
                Text(
                    "${pane.index + 1} / ${pane.count}",
                    color = T.BoneDim, fontFamily = T.Mono, fontSize = 11.sp,
                )
            }
            ConsoleButton("NEXT >", { onCmd("next") })
        }
    }
}

/** Fuda's NFC readout — the last tag's UID and RF family. Its own panel category rather
 *  than a row in Air: this is the first channel that reports *who/what* (a discrete
 *  identity) instead of a physical scalar, so it doesn't belong under an air heading. */
@Composable
private fun IdentityPanel(r: ShintaiReadings) {
    val (uid, type) = formatFuda(r.fuda)
    Panel("Identity") {
        ReadoutRow("Tag UID", uid)
        ReadoutRow("Type", type)
    }
}

/** Milliseconds the Lightning LED stays lit after a strike. */
private const val STRIKE_FLASH_MS = 450L

/** Enrai's lightning readout — last-strike distance / energy / cumulative count. The panel
 *  LED flashes white for a beat on each new strike (the count is monotonic, so any increase
 *  is a fresh bolt); it idles amber otherwise. */
@Composable
private fun LightningPanel(l: LightningState) {
    var flash by remember { mutableStateOf(false) }
    LaunchedEffect(l.strikes) {
        if (l.strikes > 0) {
            flash = true
            delay(STRIKE_FLASH_MS)
            flash = false
        }
    }
    Panel("Lightning", ledColor = if (flash) T.Bone else T.Amber) {
        ReadoutRow("Distance", l.distanceLabel)
        ReadoutRow("Energy", if (l.hasStrike) l.energy.toString() else "—")
        ReadoutRow("Strikes", l.strikes.toString())
    }
}

/** Enrai AS3935 tuning — writes control tokens to the board over the Lightning Control char.
 *  Shows the boot defaults until the board's config notify confirms them ([LightningConfig.known]).
 *  Distance is a coarse storm-front estimate: OUTDOOR gain + Clear-stats spread it (see the spec). */
@Composable
private fun LightningTuningPanel(cfg: LightningConfig, onCmd: (String) -> Unit) {
    Panel("Lightning Tuning", ledColor = if (cfg.known) T.Phosphor else T.PhosphorDim) {
        ConsoleButton(
            if (cfg.outdoor) "Gain: OUTDOOR — tap for INDOOR" else "Gain: INDOOR — tap for OUTDOOR",
            { onCmd("gain") }, modifier = Modifier.fillMaxWidth(),
        )
        Spacer(Modifier.height(8.dp))
        TuneStepper("Spike reject", cfg.spike.toString(), { onCmd("spike-") }, { onCmd("spike+") })
        TuneStepper("Watchdog", cfg.watchdog.toString(), { onCmd("wdog-") }, { onCmd("wdog+") })
        TuneStepper("Tune cap", cfg.tune.toString(), { onCmd("tune-") }, { onCmd("tune+") })
        Spacer(Modifier.height(8.dp))
        ConsoleButton("Clear storm stats", { onCmd("clear") }, modifier = Modifier.fillMaxWidth())
    }
}

/** Label · [−] value [+] row — a compact stepper for one AS3935 setting. */
@Composable
private fun TuneStepper(label: String, value: String, onMinus: () -> Unit, onPlus: () -> Unit) {
    Row(
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(10.dp),
        modifier = Modifier.fillMaxWidth().padding(vertical = 4.dp),
    ) {
        Text(label, color = T.Bone, fontFamily = T.Mono, fontSize = 13.sp, modifier = Modifier.weight(1f))
        ConsoleButton("−", onMinus)
        Text(value, color = T.Phosphor, fontFamily = T.Mono, fontSize = 15.sp,
            modifier = Modifier.padding(horizontal = 6.dp))
        ConsoleButton("+", onPlus)
    }
}

/** Hokan's pedometer readout + dead-reckoned breadcrumb mini-map. Steps/cadence are
 *  always shown; the mini-map appears once there's a walked path (track past origin). */
@Composable
private fun NavigationPanel(pdr: HokanPdr) {
    Panel("Navigation") {
        ReadoutRow("Steps", pdr.steps.toString())
        ReadoutRow("Cadence", if (pdr.cadence > 0) "${pdr.cadence} /min" else "—")
        ReadoutRow("Heading", "${pdr.headingDeg.roundToInt()}°")
        if (pdr.track.size > 1) {
            Spacer(Modifier.height(8.dp))
            HokanMap(pdr)
        }
    }
}

@Composable
private fun PairPanel(
    scanning: Boolean,
    devices: List<DeviceEntry>,
    vm: OperatorViewModel,
    linkedRoles: Set<Role>,
    onBack: (() -> Unit)?,
) {
    Panel("Pair", ledColor = if (scanning) T.Amber else T.PhosphorDim) {
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(10.dp)) {
            ConsoleButton(
                if (scanning) "Scanning…" else "Scan", vm::startScan,
                modifier = Modifier.weight(1f), active = scanning,
            )
            // Add-a-pod mode shows Back (to the console); the first-link screen shows Reconnect.
            if (onBack != null) {
                ConsoleButton("Back", onBack, modifier = Modifier.weight(1f))
            } else if (vm.hasLast) {
                ConsoleButton("Reconnect", vm::reconnectLast, modifier = Modifier.weight(1f))
            }
        }
        if (linkedRoles.isNotEmpty()) {
            Spacer(Modifier.height(8.dp))
            Text(
                "Linked: " + linkedRoles.sortedBy { it.name }.joinToString(" ") { it.name.uppercase() } +
                    " — pick the other unit",
                color = T.Phosphor, fontFamily = T.Mono, fontSize = 11.sp,
            )
        }
        Spacer(Modifier.height(10.dp))
        if (devices.isEmpty()) {
            Text(
                if (scanning) "…searching for ShintaiOS" else "Scan to find the board.",
                color = T.BoneDim, fontFamily = T.Mono, fontSize = 12.sp,
            )
        } else {
            devices.forEach { d ->
                DeviceRow(d, linked = vm.roleOf(d.name) in linkedRoles) { vm.connect(d) }
            }
        }
    }
}

@Composable
private fun DeviceRow(d: DeviceEntry, linked: Boolean, onLink: () -> Unit) {
    val isBoard = d.name?.startsWith(ShintaiScanner.ADVERTISED_NAME) == true
    Row(
        Modifier.fillMaxWidth().padding(vertical = 5.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Column(Modifier.weight(1f)) {
            Text(
                d.name ?: "(unnamed)",
                color = if (isBoard) T.Phosphor else T.Bone,
                fontFamily = T.Mono, fontSize = 14.sp,
            )
            Text("${d.address}  ${d.rssi} dBm", color = T.BoneDim, fontFamily = T.Mono, fontSize = 11.sp)
        }
        // A pod already linked (its role is in) shows a disabled marker, not a Link button.
        if (linked) {
            ConsoleButton("Linked", {}, enabled = false)
        } else {
            ConsoleButton("Link", onLink, active = isBoard)
        }
    }
}

@Composable
private fun AccessPanel(onRequestPermissions: () -> Unit) {
    Panel("Access", ledColor = T.Amber) {
        Text(
            "Shintai Operator reads the board over Bluetooth and scans to find it. " +
                "Grant Bluetooth to begin.",
            color = T.BoneDim, fontFamily = T.Mono, fontSize = 13.sp,
        )
        Spacer(Modifier.height(12.dp))
        ConsoleButton("Grant Bluetooth", onRequestPermissions)
        Spacer(Modifier.height(8.dp))
        Text(
            "If no dialog appears, enable it in system settings → apps → Shintai Operator.",
            color = T.BoneDim, fontFamily = T.Mono, fontSize = 11.sp,
        )
    }
}

/** One arc's range line under the hero numeral — "L  1234 mm" / "R  —", coloured by
 *  that arc's proximity band. The per-arc half of Kōei's rear dual-arc readout. */
@Composable
private fun ArcReadout(label: String, mm: Int?, units: Units) {
    val text = if (mm == null) "—" else distanceParts(mm, "", units).let { (v, u) -> "$v $u" }
    Text(
        "$label  $text",
        color = distColor(mm), fontFamily = T.Mono, fontSize = 13.sp, letterSpacing = 1.sp,
    )
}

// --- small mappings --------------------------------------------------------

private fun distColor(mm: Int?): Color = when {
    mm == null -> T.BoneDim
    mm <= NEAR_MM -> T.Alert
    mm <= Bands.FAR_MM -> T.Amber
    else -> T.Phosphor
}

private fun co2Color(ppm: Int?): Color = when {
    ppm == null -> T.BoneDim
    ppm >= Bands.CO2_ALARM_PPM -> T.Alert
    ppm >= Bands.CO2_WARN_PPM -> T.Amber
    else -> T.Phosphor
}

private fun ledColor(s: ConnectionState): Color = when (s) {
    ConnectionState.Live -> T.Phosphor
    ConnectionState.Connecting, ConnectionState.Discovering -> T.Amber
    ConnectionState.Disconnected -> T.Alert
    ConnectionState.PermissionNeeded -> T.Amber
    ConnectionState.Idle -> T.BoneDim
}

private fun stateLabel(s: ConnectionState): String = when (s) {
    ConnectionState.Idle -> "idle"
    ConnectionState.PermissionNeeded -> "needs bluetooth"
    ConnectionState.Connecting -> "connecting…"
    ConnectionState.Discovering -> "discovering…"
    ConnectionState.Live -> "● LIVE"
    ConnectionState.Disconnected -> "reconnecting…"
}
