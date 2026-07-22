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
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.saboteur.shintai.core.Channel
import com.saboteur.shintai.core.ConnectionState
import com.saboteur.shintai.core.HokanPdr
import com.saboteur.shintai.core.Precedence
import com.saboteur.shintai.core.Role
import com.saboteur.shintai.core.NEAR_MM
import com.saboteur.shintai.core.ShintaiReadings
import com.saboteur.shintai.core.Smell
import com.saboteur.shintai.core.Units
import com.saboteur.shintai.core.distanceParts
import com.saboteur.shintai.core.label
import com.saboteur.shintai.core.formatClimate
import com.saboteur.shintai.core.formatEnvironment
import com.saboteur.shintai.core.formatGps
import com.saboteur.shintai.core.formatTemp
import com.saboteur.shintai.core.formatThermal
import com.saboteur.shintaioperator.ui.AlertBanner
import com.saboteur.shintaioperator.ui.ConsoleButton
import com.saboteur.shintaioperator.ui.DepthField
import com.saboteur.shintaioperator.ui.HeatGrid
import com.saboteur.shintaioperator.ui.HokanMap
import com.saboteur.shintaioperator.ui.LogTerminal
import com.saboteur.shintaioperator.ui.Panel
import com.saboteur.shintaioperator.ui.ReadoutRow
import com.saboteur.shintaioperator.ui.SegmentBar
import com.saboteur.shintaioperator.ui.Sparkline
import com.saboteur.shintaioperator.ui.StatusLed
import com.saboteur.shintaioperator.ui.TrackerGauge
import kotlin.math.roundToInt

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

/** AIR — the climate + environment cluster plus Kyūkaku's derived smell. ENVIRONMENT
 *  is the channel the glasses take only for the smell spike; the Operator shows it in
 *  full. Kyūkaku (no BLE channel of its own — see :core Kyukaku.kt) is folded from the
 *  environment's gas_ohms; violet is its identity, distinct from the CO₂ green→red ramp. */
@Composable
private fun AirPanel(r: ShintaiReadings, units: Units) {
    val ppm = co2Ppm(r.climate)
    Panel("Air") {
        ReadoutRow("CO₂", ppm?.let { "$it" } ?: "—", "ppm", valueColor = co2Color(ppm))
        Spacer(Modifier.height(6.dp))
        SegmentBar(
            value = (ppm ?: 0).toFloat(),
            fullScale = Bands.CO2_FULLSCALE_PPM.toFloat(),
            warn = Bands.CO2_WARN_PPM.toFloat(),
            alarm = Bands.CO2_ALARM_PPM.toFloat(),
        )
        Spacer(Modifier.height(8.dp))
        ReadoutRow("Climate", formatClimate(r.climate, units))
        ReadoutRow("Environment", formatEnvironment(r.environment, units))
        ReadoutRow(
            "Nose", r.kyukaku.smell.label.uppercase(), "r ${"%.2f".format(r.kyukaku.ratio)}",
            valueColor = when (r.kyukaku.smell) {
                Smell.Settling -> T.BoneDim
                Smell.Clean -> T.Phosphor
                else -> T.Violet
            },
        )
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

@Composable
private fun Console(
    r: ShintaiReadings,
    units: Units,
    distHist: List<Float>,
    co2Hist: List<Float>,
    recording: Boolean,
    vm: OperatorViewModel,
) {
    // PROXIMITY — the hero: motion tracker + the one big glanceable value.
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

    // TELEMETRY — motion & position.
    Panel("Telemetry") {
        ReadoutRow("Heading", r.heading)
        ReadoutRow("Accel", r.accel)
        ReadoutRow("GPS", formatGps(r.gps, units))
        ReadoutRow("Thermal", formatThermal(r.thermal, units))
    }

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

    // AIR — climate + environment + Kyūkaku's derived smell. Its own composable so
    // Console stays within the method-length/complexity budget (same as NavigationPanel).
    AirPanel(r, units)

    // TREND — rolling history the glasses don't keep.
    Panel("Trend") {
        Text("RANGE", color = T.Bone, fontFamily = T.Mono, fontSize = 11.sp, letterSpacing = 2.sp)
        Sparkline(distHist, T.Phosphor)
        Spacer(Modifier.height(8.dp))
        Text("CO₂", color = T.Bone, fontFamily = T.Mono, fontSize = 11.sp, letterSpacing = 2.sp)
        Sparkline(co2Hist, T.Amber)
    }

    // Bunshin: per-channel source precedence — appears only when two pods are linked.
    MultiPodSources(vm)

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
