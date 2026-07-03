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
import androidx.compose.foundation.background
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
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp

/** Cyberpunk-ish palette, matching the HUD in hud.py. */
private val BG = Color(0xFF05060A)
private val PANEL = Color(0xFF0B0E1A)
private val NEON = Color(0xFF00F0FF)
private val MAGENTA = Color(0xFFFF2BD6)
private val DIM = Color(0xFF5A6B82)
private val ALERT = Color(0xFFFF3B3B)
private val CHIP = Color(0xFF1B2440)
private val mono = FontFamily.Monospace

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
    BoxWithConstraints(
        modifier = Modifier
            .fillMaxSize()
            .background(if (r.alertActive) Color(0xFF1A0608) else BG)
    ) {
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
            // Header: title + status, and a gear that opens settings. The gear lives
            // in the header (not a floating align box) because align'd overlays on
            // this device report zero hit-bounds — the header has solid bounds.
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Text(
                    "▸ SHINTAI-OS",
                    color = NEON, fontFamily = mono, fontWeight = FontWeight.Bold, fontSize = 22.sp,
                )
                // The whole status+gear group is one tappable target (data flowing =
                // the rx counter climbs).
                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    modifier = Modifier
                        .clickable { onToggleSettings() }
                        .padding(8.dp),
                ) {
                    Text(
                        if (r.packets > 0) "${stateLabel(r.connection)}  ·  rx ${r.packets}"
                        else stateLabel(r.connection),
                        color = if (r.connection == ConnectionState.Live) NEON else DIM,
                        fontFamily = mono, fontSize = 16.sp,
                    )
                    Text("  ${if (settingsOpen) "✕" else "⚙"}", color = NEON,
                        fontFamily = mono, fontSize = 20.sp)
                }
            }

            Spacer(Modifier.height(20.dp))

            if (settingsOpen) {
                // Inline at the top (real bounds, never below the fold) instead of a
                // floating overlay.
                SettingsPanel(units, stereo, ipdNudge, onAdjustIpd, onToggleUnits, onToggleSettings)
            } else if (r.connection == ConnectionState.PermissionNeeded) {
                // Denied BLUETOOTH_CONNECT: explain and offer a retry rather than
                // leaving a blank "—" readout that never fills in.
                PermissionPrompt(onRequestPermission)
            } else {
                // The headline: distance, big (converted to the chosen unit).
                val (distVal, distUnit) = distanceParts(r.distanceMm, r.distanceText, units)
                Text("DISTANCE", color = MAGENTA, fontFamily = mono, fontSize = 14.sp)
                Text(
                    text = distVal,
                    color = if (r.alertActive) ALERT else NEON,
                    fontFamily = mono, fontWeight = FontWeight.Bold, fontSize = 72.sp,
                )
                Text(distUnit, color = DIM, fontFamily = mono, fontSize = 20.sp)
                if (r.alertActive) {
                    Spacer(Modifier.height(8.dp))
                    Text(
                        "⚠ TOO CLOSE",
                        color = ALERT, fontFamily = mono, fontWeight = FontWeight.Bold, fontSize = 28.sp,
                    )
                }

                Spacer(Modifier.height(28.dp))

                // Mini readout — the rest of the subscribed channels.
                Column(
                    modifier = Modifier
                        .fillMaxWidth()
                        .background(PANEL, RoundedCornerShape(10.dp))
                        .padding(18.dp),
                    verticalArrangement = Arrangement.spacedBy(10.dp),
                ) {
                    ReadoutRow("HEADING", r.heading)
                    ReadoutRow("ACCEL", r.accel)
                    ReadoutRow("GPS", formatGps(r.gps, units))
                    ReadoutRow("CLIMATE", formatClimate(r.climate, units))
                    ReadoutRow("THERMAL", formatThermal(r.thermal, units))
                }
            }
        }
    }
}

/** Shown when BLUETOOTH_CONNECT was denied: a clear reason plus a retry with a
 *  hit target the glasses pointer can land on. Tapping re-fires the request; if
 *  the system no longer shows the dialog (permanently denied), the user is
 *  pointed at app settings. */
@Composable
private fun PermissionPrompt(onRequestPermission: () -> Unit) {
    Column(
        modifier = Modifier
            .fillMaxWidth()
            .background(PANEL, RoundedCornerShape(10.dp))
            .padding(18.dp),
        verticalArrangement = Arrangement.spacedBy(14.dp),
    ) {
        Text("BLUETOOTH NEEDED", color = MAGENTA, fontFamily = mono,
            fontWeight = FontWeight.Bold, fontSize = 20.sp)
        Text(
            "Shintai Glass reads the board over Bluetooth. Grant the permission to start streaming.",
            color = DIM, fontFamily = mono, fontSize = 14.sp,
        )
        TapChip("GRANT BLUETOOTH", onRequestPermission)
        Text(
            "If no dialog appears, enable it in system settings → apps → Shintai Glass → permissions.",
            color = DIM, fontFamily = mono, fontSize = 11.sp,
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
    Column(
        modifier = Modifier
            .fillMaxWidth()
            .background(PANEL, RoundedCornerShape(10.dp))
            .padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        Text("SETTINGS", color = NEON, fontFamily = mono, fontWeight = FontWeight.Bold,
            fontSize = 16.sp)

        Row(verticalAlignment = Alignment.CenterVertically) {
            Text("UNITS", color = MAGENTA, fontFamily = mono, fontSize = 15.sp,
                modifier = Modifier.padding(end = 14.dp))
            TapChip(if (units == Units.IMPERIAL) "IMPERIAL" else "METRIC", onToggleUnits)
        }

        if (stereo) {
            // True side-by-side AR mode: the app draws each eye, so IPD nudging works.
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text("IPD", color = MAGENTA, fontFamily = mono, fontSize = 15.sp,
                    modifier = Modifier.padding(end = 14.dp))
                TapChip("−") { onAdjustIpd(-IPD_STEP_MENU) }
                Text("  ${if (ipdNudge >= 0) "+" else ""}$ipdNudge dp  ",
                    color = Color(0xFF9FEFFF), fontFamily = mono, fontSize = 18.sp)
                TapChip("+") { onAdjustIpd(IPD_STEP_MENU) }
            }
        } else {
            // 2D-screen mode: the glasses duplicate one window to both eyes, so
            // focus/convergence is a system setting the app can't touch.
            Text(
                "FOCUS / IPD — set in glasses system (2D mode)",
                color = DIM, fontFamily = mono, fontSize = 12.sp,
            )
        }

        TapChip("CLOSE", onClose)
    }
}

/** A tappable chip with a comfortable hit target for the glasses pointer. */
@Composable
private fun TapChip(label: String, onClick: () -> Unit) {
    Text(
        label, color = NEON, fontFamily = mono, fontSize = 18.sp,
        modifier = Modifier
            .clickable { onClick() }
            .background(CHIP, RoundedCornerShape(6.dp))
            .padding(horizontal = 16.dp, vertical = 10.dp),
    )
}

@Composable
private fun ReadoutRow(label: String, value: String) {
    Row(modifier = Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
        Text(
            label,
            color = MAGENTA, fontFamily = mono, fontSize = 15.sp,
            modifier = Modifier.padding(end = 16.dp),
        )
        Text(value, color = Color(0xFF9FEFFF), fontFamily = mono, fontSize = 18.sp)
    }
}

private fun stateLabel(s: ConnectionState): String = when (s) {
    ConnectionState.Idle -> "idle"
    ConnectionState.PermissionNeeded -> "needs Bluetooth"
    ConnectionState.Connecting -> "connecting…"
    ConnectionState.Discovering -> "discovering…"
    ConnectionState.Live -> "● LIVE"
    ConnectionState.Disconnected -> "disconnected"
}
