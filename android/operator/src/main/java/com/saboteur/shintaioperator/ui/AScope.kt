package com.saboteur.shintaioperator.ui

import androidx.compose.foundation.Canvas
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.drawText
import androidx.compose.ui.text.rememberTextMeasurer
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.saboteur.shintai.core.Units
import com.saboteur.shintai.core.formatRangeM
import com.saboteur.shintaioperator.T

/**
 * Hitokage's multi-peak A-scope (specs/zokyo/hitokage.md). The XM125 is a single-beam
 * radar with no angle/azimuth data in either onboard app, so a rotating-sweep PPI
 * display (like [TrackerGauge]) would have to fake a direction that doesn't exist. An
 * A-scope — the pre-PPI radar display, amplitude vs. range along one axis — is what the
 * sensor actually measures.
 *
 * First pass (raw spikes, no labels) tested as genuinely hard to read: no way to tell
 * what the axis meant or what a spike's height represented. This version reads the way
 * the rest of the console does — labeled range ticks, a metres/feet number by each
 * contact, and the SAME near=red/far=green proximity language [TrackerGauge] uses, so a
 * blip's colour is legible without decoding an unlabeled scale first. [ScopeBlip.color]
 * is resolved by the caller (this file stays UI-primitive, no proximity-band knowledge).
 */
@Composable
fun AScope(blips: List<ScopeBlip>, maxRangeMm: Int, units: Units, modifier: Modifier = Modifier) {
    val maxAbsStrength = (blips.maxOfOrNull { kotlin.math.abs(it.strengthRaw) } ?: 0).coerceAtLeast(1)
    val textMeasurer = rememberTextMeasurer()
    val tickStyle = TextStyle(color = T.BoneDim, fontFamily = T.Mono, fontSize = 9.sp)
    val labelStyle = TextStyle(color = T.Bone, fontFamily = T.Mono, fontSize = 10.sp)

    Canvas(modifier) {
        val baseline = size.height * 0.68f
        val left = 4.dp.toPx()
        val right = size.width - 4.dp.toPx()
        val spikeTop = size.height * 0.28f

        fun xFor(mm: Int) = left + (right - left) * (mm.toFloat() / maxRangeMm).coerceIn(0f, 1f)

        // Labeled range ticks (quarter span) — the fix for "what does the axis mean?".
        for (i in 0..4) {
            val mm = maxRangeMm * i / 4
            val x = xFor(mm)
            drawLine(T.PhosphorDim, Offset(x, baseline), Offset(x, baseline + 6.dp.toPx()), 1.dp.toPx())
            val tick = textMeasurer.measure(formatRangeM(mm, units), tickStyle)
            drawText(tick, topLeft = Offset(x - tick.size.width / 2f, baseline + 8.dp.toPx()))
        }
        drawLine(T.Grid, Offset(left, baseline), Offset(right, baseline), 1.dp.toPx())

        // The trace: flat at the baseline except a spike at each blip's range.
        val sorted = blips.sortedBy { it.distanceMm }
        val path = Path().apply { moveTo(left, baseline) }
        val spikeHalfWidth = 5.dp.toPx()
        for (b in sorted) {
            val x = xFor(b.distanceMm)
            val frac = kotlin.math.abs(b.strengthRaw).toFloat() / maxAbsStrength
            val peakY = baseline - (baseline - spikeTop) * frac
            path.lineTo((x - spikeHalfWidth).coerceAtLeast(left), baseline)
            path.lineTo(x, peakY)
            path.lineTo((x + spikeHalfWidth).coerceAtMost(right), baseline)
        }
        path.lineTo(right, baseline)
        drawPath(path, T.PhosphorDim, style = Stroke(width = 1.5.dp.toPx()))

        // Each contact: a coloured dot (near=alert, far=calm — TrackerGauge's language)
        // at its spike peak, and its distance printed right above it — the fix for
        // "I have to eyeball position against an unlabeled scale."
        for (b in sorted) {
            val x = xFor(b.distanceMm)
            val frac = kotlin.math.abs(b.strengthRaw).toFloat() / maxAbsStrength
            val peakY = baseline - (baseline - spikeTop) * frac
            drawCircle(b.color, radius = 3.5.dp.toPx(), center = Offset(x, peakY))
            val label = textMeasurer.measure(formatRangeM(b.distanceMm, units), labelStyle)
            drawText(
                label,
                topLeft = Offset(
                    (x - label.size.width / 2f).coerceIn(0f, size.width - label.size.width),
                    (peakY - label.size.height - 4.dp.toPx()).coerceAtLeast(0f),
                ),
            )
        }
    }
}
