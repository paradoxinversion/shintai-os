package com.saboteur.shintaiglass

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.width
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.saboteur.shintai.core.NEAR_MM
import com.saboteur.shintai.core.Units
import com.saboteur.shintai.core.distanceParts
import kotlin.math.PI
import kotlin.math.cos
import kotlin.math.sin

/** VL53L4CX ceiling (~6 m) — the blip-radius scale. Mirrors the firmware range. */
private const val RANGE_MAX_MM = 6000f

/** Concentric rear range rings. */
private const val RINGS = 3

/**
 * Kōei's rear dual-arc as a glanceable waveguide overlay — the glass twin of the
 * operator's motion tracker, oriented *behind* the wearer. The origin (you) sits at
 * the top; the lower semicircle is the space at your back; the two ToF arcs show as
 * contacts — the left arc down-left, the right arc down-right — each blip's radius
 * tracking its distance. Strokes only on black (style.md's no-fill rule holds for
 * this plotted overlay, as it does for HokanPanel's path). Per the waveguide's
 * alarm-fatigue rule, a blip is phosphor until it breaks NEAR_MM, then it turns red
 * and blinks with the hero — amber is not used here. A null arc (no target / sensor
 * absent) simply doesn't draw.
 */
@Composable
fun RearArcPanel(leftMm: Int?, rightMm: Int?, alert: Boolean, units: Units) {
    val blink = if (alert) alertBlink() else 1f
    Column {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text("REAR ARC", color = G.Bone, fontFamily = G.Mono, fontSize = 13.sp, letterSpacing = 1.sp)
            Spacer(Modifier.width(12.dp))
            ArcValue("L", leftMm, units, blink)
            Spacer(Modifier.width(14.dp))
            ArcValue("R", rightMm, units, blink)
        }
        Spacer(Modifier.height(6.dp))
        Canvas(Modifier.width(200.dp).height(104.dp).border(2.dp, G.Grid)) {
            val pad = 10.dp.toPx()
            val cx = size.width / 2f
            val cy = pad                                  // origin near the top; arcs open downward
            val maxR = minOf(size.width / 2f - pad, size.height - cy - pad)

            // Concentric rear range rings (lower semicircle: 0°→180° is clockwise =
            // down, in screen coords where y grows downward), dim phosphor.
            for (i in 1..RINGS) {
                val r = maxR * i / RINGS
                drawArc(
                    color = G.PhosphorDim,
                    startAngle = 0f, sweepAngle = 180f, useCenter = false,
                    topLeft = Offset(cx - r, cy - r), size = Size(2 * r, 2 * r),
                    style = Stroke(width = 1.5.dp.toPx()),
                )
            }
            // Shoulder line + origin crosshair (you-are-here).
            drawLine(G.Grid, Offset(cx - maxR, cy), Offset(cx + maxR, cy), 1.5.dp.toPx())
            val ch = 5.dp.toPx()
            drawLine(G.Phosphor, Offset(cx - ch, cy), Offset(cx + ch, cy), 1.5.dp.toPx())
            drawLine(G.Phosphor, Offset(cx, cy), Offset(cx, cy + ch), 1.5.dp.toPx())

            // The two rear contacts: left arc down-left (135°), right arc down-right (45°).
            for ((mm, deg) in listOf(leftMm to 135f, rightMm to 45f)) {
                if (mm == null) continue
                val frac = (mm.toFloat() / RANGE_MAX_MM).coerceIn(0f, 1f)
                val rad = deg * PI.toFloat() / 180f
                val center = Offset(cx + maxR * frac * cos(rad), cy + maxR * frac * sin(rad))
                val color = if (mm <= NEAR_MM) G.Alert.copy(alpha = blink) else G.Phosphor
                drawCircle(color, radius = 4.5.dp.toPx(), center = center)
            }
        }
    }
}

/** One arc's header value — "L 1234 mm" / "R —" — red when that arc is inside NEAR_MM. */
@Composable
private fun ArcValue(label: String, mm: Int?, units: Units, blink: Float) {
    val text = if (mm == null) "$label —" else distanceParts(mm, "", units).let { (v, u) -> "$label $v $u" }
    val alerting = mm != null && mm <= NEAR_MM
    Text(
        text,
        color = if (alerting) G.Alert.copy(alpha = blink) else G.Phosphor,
        fontFamily = G.Mono, fontSize = 14.sp,
    )
}
