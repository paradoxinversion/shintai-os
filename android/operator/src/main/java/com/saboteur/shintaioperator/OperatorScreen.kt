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
import com.saboteur.shintai.core.ConnectionState
import com.saboteur.shintai.core.HokanPdr
import com.saboteur.shintai.core.NEAR_MM
import com.saboteur.shintai.core.ShintaiReadings
import com.saboteur.shintai.core.Units
import com.saboteur.shintai.core.distanceParts
import com.saboteur.shintai.core.formatClimate
import com.saboteur.shintai.core.formatEnvironment
import com.saboteur.shintai.core.formatGps
import com.saboteur.shintai.core.formatTemp
import com.saboteur.shintai.core.formatThermal
import com.saboteur.shintaioperator.ui.AlertBanner
import com.saboteur.shintaioperator.ui.ConsoleButton
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

        when (r.connection) {
            ConnectionState.PermissionNeeded -> AccessPanel(onRequestPermissions)
            ConnectionState.Idle -> PairPanel(scanning, devices, vm)
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
                distanceMm = r.distanceMm,
                alert = r.alertActive,
                modifier = Modifier.weight(1f).height(190.dp),
            )
            Spacer(Modifier.width(12.dp))
            Column(horizontalAlignment = Alignment.End, modifier = Modifier.width(120.dp)) {
                val (v, u) = distanceParts(r.distanceMm, r.distanceText, units)
                val numeric = r.distanceMm != null
                Text("RANGE", color = T.Bone, fontFamily = T.Mono, fontSize = 11.sp, letterSpacing = 2.sp)
                Text(
                    v, color = distColor(r.distanceMm),
                    // The ONE glanceable value in 7-segment DSEG; the "—" placeholder
                    // falls back to Plex (DSEG has no em dash).
                    fontFamily = if (numeric) T.Numeral else T.Mono,
                    fontWeight = if (numeric) FontWeight.Normal else FontWeight.Bold,
                    fontSize = if (numeric) 56.sp else 44.sp,
                )
                Text(u.uppercase(), color = T.BoneDim, fontFamily = T.Mono, fontSize = 14.sp)
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

    // AIR — the climate + environment cluster. ENVIRONMENT is the channel the
    // glasses omit; the Operator is the complete readout.
    val ppm = co2Ppm(r.climate)
    Panel("Air") {
        ReadoutRow(
            "CO₂", ppm?.let { "$it" } ?: "—", "ppm",
            valueColor = co2Color(ppm),
        )
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
    }
    if (ppm != null && ppm >= Bands.CO2_ALARM_PPM) {
        AlertBanner("CO₂ $ppm ppm — ventilate", alarm = true)
    }

    // TREND — rolling history the glasses don't keep.
    Panel("Trend") {
        Text("RANGE", color = T.Bone, fontFamily = T.Mono, fontSize = 11.sp, letterSpacing = 2.sp)
        Sparkline(distHist, T.Phosphor)
        Spacer(Modifier.height(8.dp))
        Text("CO₂", color = T.Bone, fontFamily = T.Mono, fontSize = 11.sp, letterSpacing = 2.sp)
        Sparkline(co2Hist, T.Amber)
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
private fun PairPanel(scanning: Boolean, devices: List<DeviceEntry>, vm: OperatorViewModel) {
    Panel("Pair", ledColor = if (scanning) T.Amber else T.PhosphorDim) {
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(10.dp)) {
            ConsoleButton(
                if (scanning) "Scanning…" else "Scan", vm::startScan,
                modifier = Modifier.weight(1f), active = scanning,
            )
            vm.lastAddress?.let { addr ->
                ConsoleButton("Reconnect", { vm.connect(addr) }, modifier = Modifier.weight(1f))
            }
        }
        Spacer(Modifier.height(10.dp))
        if (devices.isEmpty()) {
            Text(
                if (scanning) "…searching for ShintaiOS" else "Scan to find the board.",
                color = T.BoneDim, fontFamily = T.Mono, fontSize = 12.sp,
            )
        } else {
            devices.forEach { d -> DeviceRow(d) { vm.connect(d.address) } }
        }
    }
}

@Composable
private fun DeviceRow(d: DeviceEntry, onLink: () -> Unit) {
    val isBoard = d.name == ShintaiScanner.ADVERTISED_NAME
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
        ConsoleButton("Link", onLink, active = isBoard)
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
