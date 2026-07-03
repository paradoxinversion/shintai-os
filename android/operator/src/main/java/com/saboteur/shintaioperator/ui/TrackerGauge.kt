package com.saboteur.shintaioperator.ui

import androidx.compose.animation.core.LinearEasing
import androidx.compose.animation.core.RepeatMode
import androidx.compose.animation.core.animateFloat
import androidx.compose.animation.core.infiniteRepeatable
import androidx.compose.animation.core.rememberInfiniteTransition
import androidx.compose.animation.core.tween
import androidx.compose.foundation.Canvas
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.unit.dp
import com.saboteur.shintai.core.NEAR_MM
import com.saboteur.shintaioperator.Bands
import com.saboteur.shintaioperator.T
import kotlin.math.PI
import kotlin.math.cos
import kotlin.math.sin

/**
 * The motion-tracker gauge — the project's soul and the most direct Aliens
 * quotation (style.md §5.1). Bottom-anchored origin is the wearer; concentric
 * range rings climb outward; a sweep line rotates across the forward field; the
 * nearest contact shows as a blip whose colour crosses green → amber → red as it
 * enters FAR_MM then NEAR_MM. We only have forward range (the ToF looks ahead),
 * so the single contact sits dead-ahead at a radius proportional to distance.
 */
@Composable
fun TrackerGauge(distanceMm: Int?, alert: Boolean, modifier: Modifier = Modifier) {
    val sweep = rememberInfiniteTransition(label = "sweep").animateFloat(
        initialValue = 180f, targetValue = 360f,
        animationSpec = infiniteRepeatable(tween(2600, easing = LinearEasing), RepeatMode.Restart),
        label = "sweepAngle",
    ).value
    val blipAlpha = if (alert) blinkAlpha() else 1f

    Canvas(modifier) {
        val cx = size.width / 2f
        val cy = size.height
        val maxR = minOf(size.width / 2f, size.height) * 0.92f
        fun onArc(deg: Float, r: Float): Offset {
            val rad = deg * PI.toFloat() / 180f
            return Offset(cx + r * cos(rad), cy + r * sin(rad))
        }

        // Concentric range rings (top half), dim phosphor.
        for (i in 1..RINGS) {
            val r = maxR * i / RINGS
            drawArc(
                color = T.PhosphorDim,
                startAngle = 180f, sweepAngle = 180f, useCenter = false,
                topLeft = Offset(cx - r, cy - r), size = Size(2 * r, 2 * r),
                style = Stroke(width = 1.dp.toPx()),
            )
        }
        // Graduation ticks on the outer ring.
        var deg = 180f
        while (deg <= 360f) {
            drawLine(T.PhosphorDim, onArc(deg, maxR * 0.95f), onArc(deg, maxR), 1.dp.toPx())
            deg += 30f
        }
        // Baseline across the origin.
        drawLine(T.Grid, Offset(cx - maxR, cy), Offset(cx + maxR, cy), 1.dp.toPx())

        // Rotating sweep line.
        drawLine(T.Phosphor.copy(alpha = 0.55f), Offset(cx, cy), onArc(sweep, maxR), 1.5.dp.toPx())

        // Crosshair at the origin (you-are-here).
        val ch = 6.dp.toPx()
        drawLine(T.Phosphor, Offset(cx - ch, cy), Offset(cx + ch, cy), 1.5.dp.toPx())
        drawLine(T.Phosphor, Offset(cx, cy - ch), Offset(cx, cy), 1.5.dp.toPx())

        // The contact blip, dead-ahead (12 o'clock = 270°), radius ∝ distance.
        if (distanceMm != null) {
            val frac = (distanceMm.toFloat() / Bands.RANGE_MAX_MM).coerceIn(0f, 1f)
            val color = when {
                distanceMm <= NEAR_MM -> T.Alert
                distanceMm <= Bands.FAR_MM -> T.Amber
                else -> T.Phosphor
            }
            drawCircle(color.copy(alpha = blipAlpha), radius = 5.dp.toPx(), center = onArc(270f, maxR * frac))
        }
    }
}

private const val RINGS = 3
