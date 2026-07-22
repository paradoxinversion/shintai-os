// A small library of instrument primitives — many tiny @Composables by design,
// so the file-level function-count rule doesn't apply here.
@file:Suppress("TooManyFunctions")

package com.saboteur.shintaioperator.ui

import androidx.compose.animation.core.RepeatMode
import androidx.compose.animation.core.animateFloat
import androidx.compose.animation.core.infiniteRepeatable
import androidx.compose.animation.core.rememberInfiniteTransition
import androidx.compose.animation.core.tween
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.drawBehind
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.FilterQuality
import androidx.compose.ui.graphics.PathEffect
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.IntSize
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.material3.Text
import android.graphics.Bitmap
import com.saboteur.shintai.core.DepthGrid
import com.saboteur.shintai.core.HokanPdr
import com.saboteur.shintai.core.ThermalGrid
import com.saboteur.shintai.core.argb
import com.saboteur.shintaioperator.ChamferShape
import com.saboteur.shintaioperator.T
import kotlin.math.roundToInt

/** A steady ~1Hz blink alpha for alerts (style.md §7: blink, never glitch). */
@Composable
fun blinkAlpha(): Float {
    val t = rememberInfiniteTransition(label = "blink")
    return t.animateFloat(
        initialValue = 1f, targetValue = 0.25f,
        animationSpec = infiniteRepeatable(tween(500), RepeatMode.Reverse),
        label = "blinkA",
    ).value
}

/** L-shaped reticle brackets at each corner — Aliens-HUD framing (style.md §4),
 *  drawn instead of a heavy continuous border. */
private fun Modifier.reticleTicks(color: Color): Modifier = drawBehind {
    val len = 10.dp.toPx()
    val w = size.width
    val h = size.height
    val sw = 1.5.dp.toPx()
    fun l(a: Offset, b: Offset) = drawLine(color, a, b, sw)
    // top-left
    l(Offset(0f, 0f), Offset(len, 0f)); l(Offset(0f, 0f), Offset(0f, len))
    // top-right
    l(Offset(w, 0f), Offset(w - len, 0f)); l(Offset(w, 0f), Offset(w, len))
    // bottom-left
    l(Offset(0f, h), Offset(len, h)); l(Offset(0f, h), Offset(0f, h - len))
    // bottom-right
    l(Offset(w, h), Offset(w - len, h)); l(Offset(w, h), Offset(w, h - len))
}

/** A chamfered console panel: VOID/panel fill, 1px grid border, reticle ticks, and
 *  a title bar (Michroma-ish tracked label in BONE + a status LED). style.md §5.2. */
@Composable
fun Panel(
    title: String,
    modifier: Modifier = Modifier,
    ledColor: Color = T.PhosphorDim,
    content: @Composable () -> Unit,
) {
    val shape = ChamferShape()
    Column(
        modifier
            .fillMaxWidth()
            .background(T.Panel, shape)
            .border(1.dp, T.Grid, shape)
            .reticleTicks(T.Grid)
            .padding(14.dp),
    ) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            StatusLed(ledColor)
            Spacer(Modifier.width(8.dp))
            Text(
                title.uppercase(), color = T.Bone, fontFamily = T.Title,
                fontSize = 11.sp, letterSpacing = 1.sp,
            )
        }
        Spacer(Modifier.height(10.dp))
        content()
    }
}

/** A 8dp square (never a circle) status LED — style.md §5.6. */
@Composable
fun StatusLed(color: Color, blink: Boolean = false) {
    val a = if (blink) blinkAlpha() else 1f
    Box(
        Modifier
            .size(9.dp)
            .background(color.copy(alpha = a)),
    )
}

/** LABEL · dotted leader · VALUE · UNIT — the ledger row (style.md §5.3). */
@Composable
fun ReadoutRow(
    label: String,
    value: String,
    unit: String = "",
    valueColor: Color = T.Phosphor,
) {
    Row(
        Modifier.fillMaxWidth().padding(vertical = 3.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(
            label.uppercase(), color = T.Bone, fontFamily = T.Mono,
            fontSize = 12.sp, letterSpacing = 1.sp,
        )
        DottedLeader(Modifier.weight(1f).padding(horizontal = 8.dp))
        Text(value, color = valueColor, fontFamily = T.Mono, fontSize = 15.sp)
        if (unit.isNotEmpty()) {
            Spacer(Modifier.width(4.dp))
            Text(unit, color = T.BoneDim, fontFamily = T.Mono, fontSize = 12.sp)
        }
    }
}

@Composable
private fun DottedLeader(modifier: Modifier) {
    Canvas(modifier.height(10.dp)) {
        val y = size.height / 2f
        drawLine(
            color = T.Grid,
            start = Offset(0f, y),
            end = Offset(size.width, y),
            strokeWidth = 1.dp.toPx(),
            pathEffect = PathEffect.dashPathEffect(floatArrayOf(2f, 5f), 0f),
        )
    }
}

/** Segmented LED/VU meter — discrete blocks fill green→amber→red across thresholds
 *  (style.md §5.4). Spent segments drop to the current channel's dim. */
@Composable
fun SegmentBar(
    value: Float,
    fullScale: Float,
    warn: Float,
    alarm: Float,
    modifier: Modifier = Modifier,
    segments: Int = 14,
) {
    val (lit, dim) = when {
        value >= alarm -> T.Alert to T.AlertDim
        value >= warn -> T.Amber to T.AmberDim
        else -> T.Phosphor to T.PhosphorDim
    }
    val frac = (value / fullScale).coerceIn(0f, 1f)
    val on = (frac * segments).toInt().coerceIn(0, segments)
    Row(modifier.fillMaxWidth().height(16.dp), horizontalArrangement = Arrangement.spacedBy(3.dp)) {
        repeat(segments) { i ->
            Box(
                Modifier
                    .weight(1f)
                    .fillMaxHeight()
                    .background(if (i < on) lit else dim.copy(alpha = 0.35f)),
            )
        }
    }
}

/** Metsuke's 32×24 thermal grid as a false-colour heat panel (ironbow, shared with
 *  Glass via `:core`), bilinear-upscaled so the coarse grid reads as a smooth heat
 *  image. A phone screen can carry a full fill, so hot cells are bright, cool dark.
 *  The 4:3 box matches the grid's native aspect (no geometric distortion). */
@Composable
fun HeatGrid(grid: ThermalGrid, modifier: Modifier = Modifier) {
    val heat = remember(grid) {
        Bitmap.createBitmap(grid.argb(), ThermalGrid.W, ThermalGrid.H, Bitmap.Config.ARGB_8888)
            .asImageBitmap()
    }
    Canvas(modifier.fillMaxWidth().aspectRatio(4f / 3f).border(1.dp, T.Grid)) {
        drawImage(
            image = heat,
            dstSize = IntSize(size.width.roundToInt(), size.height.roundToInt()),
            filterQuality = FilterQuality.High,
        )
    }
}

/** Zanshin's 8×8 rear depth field as a colour panel (shared palette via `:core`),
 *  bilinear-upscaled so the coarse grid reads smooth. Near zones read alarming red,
 *  fading through amber/green to a cool far blue; no-target zones are dark. Square box —
 *  the field's native aspect. */
@Composable
fun DepthField(grid: DepthGrid, modifier: Modifier = Modifier) {
    val depth = remember(grid) {
        Bitmap.createBitmap(grid.argb(), DepthGrid.W, DepthGrid.H, Bitmap.Config.ARGB_8888)
            .asImageBitmap()
    }
    Canvas(modifier.fillMaxWidth().aspectRatio(1f).border(1.dp, T.Grid)) {
        drawImage(
            image = depth,
            dstSize = IntSize(size.width.roundToInt(), size.height.roundToInt()),
            filterQuality = FilterQuality.High,
        )
    }
}

/** Hokan's dead-reckoned breadcrumb as a square mini-map: the walked path integrated
 *  in `:core` ([HokanPdr], same math as `groundstation/hokan.py`), autoscaled with
 *  equal x/y so the route shape isn't distorted. Phosphor trail, a bright "you are
 *  here" dot at the current end, a dim origin dot — strokes only, the phone twin of
 *  the base-side path `analyze.py` draws. Needs a walked path (track past origin). */
@Composable
fun HokanMap(pdr: HokanPdr, modifier: Modifier = Modifier) {
    Canvas(modifier.fillMaxWidth().aspectRatio(1f).border(1.dp, T.Grid)) {
        val pts = pdr.track
        if (pts.size < 2) return@Canvas
        val pad = 12.dp.toPx()
        val minX = pts.minOf { it.x }
        val maxX = pts.maxOf { it.x }
        val minY = pts.minOf { it.y }
        val maxY = pts.maxOf { it.y }
        val spanX = (maxX - minX).coerceAtLeast(0.1f)
        val spanY = (maxY - minY).coerceAtLeast(0.1f)
        val scale = minOf((size.width - 2 * pad) / spanX, (size.height - 2 * pad) / spanY)
        val offX = (size.width - spanX * scale) / 2f
        val offY = (size.height - spanY * scale) / 2f
        // metres -> canvas px; North (y+) is UP, so invert the screen y axis.
        val screen = pts.map { p ->
            Offset(offX + (p.x - minX) * scale, size.height - (offY + (p.y - minY) * scale))
        }
        for (i in 1 until screen.size) {
            drawLine(T.Phosphor, screen[i - 1], screen[i], strokeWidth = 2.dp.toPx(), cap = StrokeCap.Round)
        }
        drawCircle(T.PhosphorDim, radius = 3.dp.toPx(), center = screen.first())   // origin
        drawCircle(T.Phosphor, radius = 4.5f.dp.toPx(), center = screen.last())    // you are here
    }
}

/** A trend polyline over a rolling buffer (style.md: reduced motion on phone). */
@Composable
fun Sparkline(data: List<Float>, color: Color, modifier: Modifier = Modifier) {
    Canvas(modifier.fillMaxWidth().height(40.dp)) {
        if (data.size < 2) {
            drawLine(T.Grid, Offset(0f, size.height / 2f), Offset(size.width, size.height / 2f), 1.dp.toPx())
            return@Canvas
        }
        val lo = data.min()
        val hi = data.max()
        val span = (hi - lo).takeIf { it > 0f } ?: 1f
        val dx = size.width / (data.size - 1)
        var prev = Offset(0f, size.height * (1f - (data[0] - lo) / span))
        for (i in 1 until data.size) {
            val p = Offset(dx * i, size.height * (1f - (data[i] - lo) / span))
            drawLine(color, prev, p, 2.dp.toPx(), cap = StrokeCap.Round)
            prev = p
        }
    }
}

/** Chamfered momentary button — border→phosphor when active, no gradients/glow
 *  (style.md §5.5). */
@Composable
fun ConsoleButton(
    label: String,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    enabled: Boolean = true,
    active: Boolean = false,
) {
    val shape = ChamferShape()
    val border = when {
        !enabled -> T.Grid
        active -> T.Phosphor
        else -> T.PhosphorDim
    }
    val fg = when {
        !enabled -> T.BoneDim
        active -> T.Void
        else -> T.Bone
    }
    Box(
        modifier
            .then(if (active) Modifier.background(T.Phosphor, shape) else Modifier)
            .border(1.dp, border, shape)
            .then(if (enabled) Modifier.clickable { onClick() } else Modifier)
            .padding(horizontal = 16.dp, vertical = 10.dp),
        contentAlignment = Alignment.Center,
    ) {
        Text(label.uppercase(), color = fg, fontFamily = T.Mono, fontSize = 13.sp, letterSpacing = 1.sp)
    }
}

/** Full-width caution/alarm strip — amber or red, blinking, imperative voice
 *  (style.md §5.7). [color] overrides the amber/red default (e.g. Kyūkaku violet). */
@Composable
fun AlertBanner(
    text: String,
    alarm: Boolean,
    modifier: Modifier = Modifier,
    color: Color = if (alarm) T.Alert else T.Amber,
) {
    val a = blinkAlpha()
    val shape = ChamferShape()
    Box(
        modifier
            .fillMaxWidth()
            .border(1.5.dp, color.copy(alpha = a), shape)
            .padding(horizontal = 14.dp, vertical = 10.dp),
    ) {
        Text(
            text.uppercase(), color = color.copy(alpha = a), fontFamily = T.Mono,
            fontSize = 16.sp, fontWeight = FontWeight.Bold, letterSpacing = 1.sp,
        )
    }
}

/** A little scrolling console log — event history in dim phosphor. */
@Composable
fun LogTerminal(lines: List<String>, modifier: Modifier = Modifier) {
    Column(modifier.fillMaxWidth()) {
        lines.forEach { line ->
            Text("> $line", color = T.PhosphorDim, fontFamily = T.Crt, fontSize = 15.sp)
        }
    }
}
