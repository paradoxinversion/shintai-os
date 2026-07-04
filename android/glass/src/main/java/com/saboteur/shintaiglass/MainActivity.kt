package com.saboteur.shintaiglass

import android.Manifest
import android.content.pm.PackageManager
import android.os.Bundle
import android.view.KeyEvent
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.viewModels
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawingPadding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.PathEffect
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.saboteur.shintai.core.ConnectionState
import com.saboteur.shintai.core.ShintaiReadings
import com.saboteur.shintai.core.Units
import com.saboteur.shintai.core.distanceParts
import com.saboteur.shintai.core.formatClimate
import com.saboteur.shintai.core.formatGps
import com.saboteur.shintai.core.formatThermal

/** Width:height above which we treat the surface as a side-by-side stereo display.
 *  X3 Pro = 2.67; Pixel-class phones in landscape ≈ 2.16, portrait < 1. */
private const val STEREO_ASPECT = 2.4f

/** IPD nudge step per volume-key press, in dp. */
private const val IPD_STEP_DP = 2

/** Larger IPD step for the on-screen +/- buttons (pointer is coarser than keys). */
private const val IPD_STEP_MENU = 6

class MainActivity : ComponentActivity() {

    private val vm: ShintaiViewModel by viewModels()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        // API 35 enforces edge-to-edge; opt in explicitly and inset the content
        // (see EyePane's safeDrawingPadding) so text never hides behind a phone's
        // status/nav bars. The glasses report zero insets, so it's a no-op there.
        enableEdgeToEdge()
        setContent {
            // Request BLUETOOTH_CONNECT, then connect. On denial we surface a
            // PermissionNeeded state (with a tappable retry) instead of silently
            // sitting idle forever — a dead-end that's easy to hit on the glasses.
            val permLauncher = androidx.activity.compose.rememberLauncherForActivityResult(
                ActivityResultContracts.RequestPermission()
            ) { granted -> if (granted) vm.connect() else vm.onPermissionDenied() }

            val requestPermission = {
                if (checkSelfPermission(Manifest.permission.BLUETOOTH_CONNECT)
                    == PackageManager.PERMISSION_GRANTED
                ) {
                    vm.connect()
                } else {
                    permLauncher.launch(Manifest.permission.BLUETOOTH_CONNECT)
                }
            }

            androidx.compose.runtime.LaunchedEffect(Unit) { requestPermission() }

            val readings by vm.readings.collectAsState()
            var settingsOpen by rememberSaveable { mutableStateOf(false) }
            ShintaiHud(
                r = readings,
                ipdNudge = vm.ipdNudge,
                units = vm.units,
                settingsOpen = settingsOpen,
                onToggleSettings = { settingsOpen = !settingsOpen },
                onAdjustIpd = { vm.adjustIpd(it) },
                onToggleUnits = { vm.toggleUnits() },
                onRequestPermission = requestPermission,
            )
        }
    }

    /** On the glasses (stereo surface), volume up/down tune the IPD nudge and the
     *  events are consumed. On a phone (mono), volume keys keep normal behavior. */
    override fun onKeyDown(keyCode: Int, event: KeyEvent): Boolean {
        if (isStereoSurface()) when (keyCode) {
            KeyEvent.KEYCODE_VOLUME_UP -> { vm.adjustIpd(IPD_STEP_DP); return true }
            KeyEvent.KEYCODE_VOLUME_DOWN -> { vm.adjustIpd(-IPD_STEP_DP); return true }
        }
        return super.onKeyDown(keyCode, event)
    }

    private fun isStereoSurface(): Boolean {
        val c = resources.configuration
        return c.screenHeightDp > 0 &&
            c.screenWidthDp.toFloat() / c.screenHeightDp >= STEREO_ASPECT
    }
}

@Composable
private fun ShintaiHud(
    r: ShintaiReadings,
    ipdNudge: Int,
    units: Units,
    settingsOpen: Boolean,
    onToggleSettings: () -> Unit,
    onAdjustIpd: (Int) -> Unit,
    onToggleUnits: () -> Unit,
    onRequestPermission: () -> Unit = {},
) {
    // Pure black ground: on the waveguide black emits no light and reads as
    // see-through over the world. Even an alarm never washes the background —
    // the alert speaks through red strokes/blink, not a fill (style.md §8).
    BoxWithConstraints(Modifier.fillMaxSize().background(G.Black)) {
        // Branch on the real render surface, not the device model: a side-by-side
        // stereo framebuffer (the X3 Pro is 1280x480 = 2.67:1) is far wider than any
        // phone — even a Pixel 7 Pro locked to landscape is ~2.16:1. Above the
        // threshold -> draw the readout into each eye-half; below -> a single pane.
        val stereo = maxHeight.value > 0f &&
            (maxWidth.value / maxHeight.value) >= STEREO_ASPECT
        if (stereo) {
            val nudge = ipdNudge.dp
            // Same readout in both halves (one source of truth, drawn twice) so the
            // eyes fuse instead of fighting. Opposite nudges: left eye outward = left
            // (−), right eye outward = right (+).
            Row(Modifier.fillMaxSize()) {
                EyePane(r, Modifier.weight(1f).fillMaxHeight(), units, nudge = -nudge,
                    stereo = true, ipdNudge = ipdNudge, settingsOpen = settingsOpen,
                    onToggleSettings = onToggleSettings, onAdjustIpd = onAdjustIpd,
                    onToggleUnits = onToggleUnits, onRequestPermission = onRequestPermission)
                EyePane(r, Modifier.weight(1f).fillMaxHeight(), units, nudge = nudge,
                    stereo = true, ipdNudge = ipdNudge, settingsOpen = settingsOpen,
                    onToggleSettings = onToggleSettings, onAdjustIpd = onAdjustIpd,
                    onToggleUnits = onToggleUnits, onRequestPermission = onRequestPermission)
            }
        } else {
            EyePane(r, Modifier.fillMaxSize(), units, settingsOpen = settingsOpen,
                onToggleSettings = onToggleSettings, onAdjustIpd = onAdjustIpd,
                onToggleUnits = onToggleUnits, onRequestPermission = onRequestPermission)
        }
    }
}

/** One eye's view of the HUD — sized to its ~640px half. [nudge] shifts the
 *  content horizontally for IPD tuning (0 in mono); [stereo] enables the IPD
 *  control in the settings panel. */
@Composable
private fun EyePane(
    r: ShintaiReadings,
    modifier: Modifier,
    units: Units,
    nudge: Dp = 0.dp,
    stereo: Boolean = false,
    ipdNudge: Int = 0,
    settingsOpen: Boolean = false,
    onToggleSettings: () -> Unit = {},
    onAdjustIpd: (Int) -> Unit = {},
    onToggleUnits: () -> Unit = {},
    onRequestPermission: () -> Unit = {},
) {
    Box(modifier.offset(x = nudge).safeDrawingPadding().padding(horizontal = 22.dp, vertical = 16.dp)) {
        Column(
            modifier = Modifier
                .fillMaxSize()
                .verticalScroll(rememberScrollState())
        ) {
            // Header: wordmark (Michroma) + a tappable status group that opens
            // settings. The group lives in the header (not a floating align box)
            // because align'd overlays on this device report zero hit-bounds.
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Text(
                    "SHINTAI-OS",
                    color = G.Bone, fontFamily = G.Title, fontSize = 18.sp,
                    letterSpacing = 2.sp,
                )
                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    modifier = Modifier.clickable { onToggleSettings() }.padding(8.dp),
                ) {
                    StatusLed(ledColor(r.connection))
                    Text(
                        "  " + if (r.packets > 0) "${stateLabel(r.connection)}  rx ${r.packets}"
                        else stateLabel(r.connection),
                        color = if (r.connection == ConnectionState.Live) G.Phosphor else G.BoneDim,
                        fontFamily = G.Mono, fontSize = 14.sp,
                    )
                    Text("  ${if (settingsOpen) "×" else "SET"}", color = G.Phosphor,
                        fontFamily = G.Mono, fontSize = 15.sp)
                }
            }

            Spacer(Modifier.height(18.dp))

            if (settingsOpen) {
                SettingsPanel(units, stereo, ipdNudge, onAdjustIpd, onToggleUnits, onToggleSettings)
            } else if (r.connection == ConnectionState.PermissionNeeded) {
                PermissionPrompt(onRequestPermission)
            } else {
                // The one hero value: distance as big DSEG numerals. Amber/red are
                // reserved for the real proximity alarm (style.md avoids alarm
                // fatigue on the waveguide), so the value is phosphor until it fires.
                val (distVal, distUnit) = distanceParts(r.distanceMm, r.distanceText, units)
                val numeric = r.distanceMm != null
                val blink = if (r.alertActive) alertBlink() else 1f
                val heroColor = if (r.alertActive) G.Alert.copy(alpha = blink) else G.Phosphor

                Text("RANGE", color = G.Bone, fontFamily = G.Mono, fontSize = 13.sp, letterSpacing = 2.sp)
                Text(
                    text = distVal,
                    color = heroColor,
                    // DSEG has no em dash, so the "—" no-reading placeholder falls back to Plex.
                    fontFamily = if (numeric) G.Numeral else G.Mono,
                    fontWeight = if (numeric) FontWeight.Normal else FontWeight.Bold,
                    fontSize = if (numeric) 60.sp else 44.sp,
                )
                Text(distUnit.uppercase(), color = G.BoneDim, fontFamily = G.Mono, fontSize = 18.sp)
                if (r.alertActive) {
                    Spacer(Modifier.height(6.dp))
                    Text(
                        "OBJECT INSIDE 0.2 M — HOLD",
                        color = G.Alert.copy(alpha = blink),
                        fontFamily = G.Mono, fontWeight = FontWeight.Bold, fontSize = 20.sp,
                        letterSpacing = 1.sp,
                    )
                }

                Spacer(Modifier.height(22.dp))
                HairRule()
                Spacer(Modifier.height(12.dp))

                // Supporting channels — dotted-leader rows on black, strokes only.
                ReadoutRow("HEADING", r.heading)
                ReadoutRow("ACCEL", r.accel)
                ReadoutRow("GPS", formatGps(r.gps, units))
                ReadoutRow("CLIMATE", formatClimate(r.climate, units))
                ReadoutRow("THERMAL", formatThermal(r.thermal, units))
                // Hokan: cumulative steps + cadence (the pedometer readout). Always
                // shown so the row layout is stable; the PDR mini-map below appears
                // only once there's a walked path.
                ReadoutRow("STEPS", r.hokan?.let { "${it.steps}  ${it.cadence}/min" } ?: "—")

                // Metsuke: the live 8×8 heat panel — Shikai's one *image* surface.
                // Only present once the first grid frame arrives (MLX90640 attached
                // + subscribed); absent otherwise, so it never shows an empty box.
                r.thermalGrid?.let { grid ->
                    Spacer(Modifier.height(14.dp))
                    ThermalPanel(grid, units)
                }

                // Hokan: the dead-reckoned breadcrumb — the HUD's second image
                // surface. Only once the wearer has moved (track past the origin).
                r.hokan?.takeIf { it.track.size > 1 }?.let { pdr ->
                    Spacer(Modifier.height(14.dp))
                    HokanPanel(pdr)
                }
            }
        }
    }
}

/** Shown when BLUETOOTH_CONNECT was denied: a clear reason (amber = caution) plus
 *  a retry with a hit target the glasses pointer can land on. */
@Composable
private fun PermissionPrompt(onRequestPermission: () -> Unit) {
    Column(verticalArrangement = Arrangement.spacedBy(14.dp)) {
        Text("BLUETOOTH NEEDED", color = G.Amber, fontFamily = G.Title, fontSize = 18.sp)
        Text(
            "Shintai Glass reads the board over Bluetooth. Grant the permission to start streaming.",
            color = G.BoneDim, fontFamily = G.Mono, fontSize = 14.sp,
        )
        TapChip("GRANT BLUETOOTH", onRequestPermission)
        Text(
            "If no dialog appears, enable it in system settings → apps → Shintai Glass → permissions.",
            color = G.BoneDim, fontFamily = G.Mono, fontSize = 11.sp,
        )
    }
}

/** The pointer-driven settings overlay: units toggle, and (stereo) IPD +/-. */
@Composable
private fun SettingsPanel(
    units: Units,
    stereo: Boolean,
    ipdNudge: Int,
    onAdjustIpd: (Int) -> Unit,
    onToggleUnits: () -> Unit,
    onClose: () -> Unit,
) {
    Column(verticalArrangement = Arrangement.spacedBy(12.dp)) {
        Text("SETTINGS", color = G.Bone, fontFamily = G.Title, fontSize = 15.sp)

        Row(verticalAlignment = Alignment.CenterVertically) {
            Text("UNITS", color = G.Bone, fontFamily = G.Mono, fontSize = 14.sp,
                modifier = Modifier.padding(end = 14.dp))
            TapChip(if (units == Units.IMPERIAL) "IMPERIAL" else "METRIC", onToggleUnits)
        }

        if (stereo) {
            // True side-by-side AR mode: the app draws each eye, so IPD nudging works.
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text("IPD", color = G.Bone, fontFamily = G.Mono, fontSize = 14.sp,
                    modifier = Modifier.padding(end = 14.dp))
                TapChip("−") { onAdjustIpd(-IPD_STEP_MENU) }
                Text("  ${if (ipdNudge >= 0) "+" else ""}$ipdNudge dp  ",
                    color = G.Phosphor, fontFamily = G.Mono, fontSize = 18.sp)
                TapChip("+") { onAdjustIpd(IPD_STEP_MENU) }
            }
        } else {
            // 2D-screen mode: the glasses duplicate one window to both eyes, so
            // focus/convergence is a system setting the app can't touch.
            Text(
                "FOCUS / IPD — set in glasses system (2D mode)",
                color = G.BoneDim, fontFamily = G.Mono, fontSize = 12.sp,
            )
        }

        TapChip("CLOSE", onClose)
    }
}

/** A chamfered stroke button (no fill — waveguide rule) with a comfortable hit
 *  target for the glasses pointer. */
@Composable
private fun TapChip(label: String, onClick: () -> Unit) {
    Box(
        modifier = Modifier
            .clickable { onClick() }
            .border(2.dp, G.PhosphorDim, ChamferShape())
            .padding(horizontal = 16.dp, vertical = 10.dp),
    ) {
        Text(label, color = G.Phosphor, fontFamily = G.Mono, fontSize = 17.sp)
    }
}

/** LABEL · dotted leader · VALUE — the ledger row, strokes only (style.md §5.3). */
@Composable
private fun ReadoutRow(label: String, value: String) {
    Row(
        modifier = Modifier.fillMaxWidth().padding(vertical = 4.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(label, color = G.Bone, fontFamily = G.Mono, fontSize = 13.sp, letterSpacing = 1.sp)
        Canvas(Modifier.weight(1f).height(10.dp).padding(horizontal = 8.dp)) {
            val y = size.height / 2f
            drawLine(
                color = G.Grid, start = Offset(0f, y), end = Offset(size.width, y),
                strokeWidth = 2.dp.toPx(),
                pathEffect = PathEffect.dashPathEffect(floatArrayOf(2f, 6f), 0f),
            )
        }
        Text(value, color = G.Phosphor, fontFamily = G.Mono, fontSize = 16.sp)
    }
}

/** A 8dp square status LED (never a circle) — style.md §5.6. */
@Composable
private fun StatusLed(color: Color) {
    Box(Modifier.size(9.dp).background(color))
}

/** A thin 2px phosphor-dim rule (1px can vanish on the waveguide — style.md §8). */
@Composable
private fun HairRule() {
    Box(Modifier.fillMaxWidth().height(2.dp).background(G.Grid))
}

private fun ledColor(s: ConnectionState): Color = when (s) {
    ConnectionState.Live -> G.Phosphor
    ConnectionState.Connecting, ConnectionState.Discovering -> G.Amber
    ConnectionState.Disconnected -> G.Alert
    ConnectionState.PermissionNeeded -> G.Amber
    ConnectionState.Idle -> G.BoneDim
}

private fun stateLabel(s: ConnectionState): String = when (s) {
    ConnectionState.Idle -> "idle"
    ConnectionState.PermissionNeeded -> "needs bluetooth"
    ConnectionState.Connecting -> "connecting…"
    ConnectionState.Discovering -> "discovering…"
    ConnectionState.Live -> "LIVE"
    ConnectionState.Disconnected -> "disconnected"
}
