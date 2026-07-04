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
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.saboteur.shintai.core.HokanPdr

/**
 * Hokan's live dead-reckoned breadcrumb — the HUD's second *image* surface (after
 * Metsuke's heat panel). The walked path is integrated from steps + heading in
 * `:core` ([HokanPdr], the same math as `groundstation/hokan.py`), here autoscaled
 * to a square mini-map: a phosphor trail with a brighter "you are here" dot at the
 * current end and a faint origin dot. Strokes only on black — style.md's no-fill
 * rule holds (this is a plotted path, not a filled image like the heat grid). Shown
 * only once the wearer has actually moved (the track has more than the origin point).
 */
@Composable
fun HokanPanel(pdr: HokanPdr) {
    Column {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text("PDR PATH", color = G.Bone, fontFamily = G.Mono, fontSize = 13.sp, letterSpacing = 1.sp)
            Spacer(Modifier.width(10.dp))
            Text(
                "${pdr.steps} steps · ${pdr.cadence}/min",
                color = G.Phosphor, fontFamily = G.Mono, fontSize = 13.sp,
            )
        }
        Spacer(Modifier.height(6.dp))
        Canvas(Modifier.width(120.dp).height(120.dp).border(2.dp, G.Grid)) {
            val pts = pdr.track
            if (pts.size < 2) return@Canvas
            val pad = 10.dp.toPx()
            val minX = pts.minOf { it.x }
            val maxX = pts.maxOf { it.x }
            val minY = pts.minOf { it.y }
            val maxY = pts.maxOf { it.y }
            // Equal scale on both axes so the walked shape isn't distorted; guard a
            // zero span (a straight line) so it still centres instead of blowing up.
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
                drawLine(G.Phosphor, screen[i - 1], screen[i], strokeWidth = 2.dp.toPx())
            }
            drawCircle(G.PhosphorDim, radius = 2.5.dp.toPx(), center = screen.first())  // origin
            drawCircle(G.Phosphor, radius = 4.dp.toPx(), center = screen.last())        // you are here
        }
    }
}
