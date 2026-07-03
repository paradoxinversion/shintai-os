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
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.drawBehind
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.PathEffect
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.material3.Text
import com.saboteur.shintaioperator.ChamferShape
import com.saboteur.shintaioperator.T

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
 *  (style.md §5.7). */
@Composable
fun AlertBanner(text: String, alarm: Boolean, modifier: Modifier = Modifier) {
    val color = if (alarm) T.Alert else T.Amber
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
