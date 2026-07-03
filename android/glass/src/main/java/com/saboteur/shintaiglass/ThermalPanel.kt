package com.saboteur.shintaiglass

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.saboteur.shintai.core.ThermalGrid
import com.saboteur.shintai.core.Units
import com.saboteur.shintai.core.formatTemp
import com.saboteur.shintai.core.ironbow

/**
 * Metsuke's live thermal grid — the HUD's one *image* surface (Shikai as a sense,
 * not a readout). An 8×8 false-colour heat panel, auto-ranged to the frame's
 * min/max (shown as the label). Ironbow-on-black suits the waveguide: cold cells
 * are black (emit no light → see-through), hot cells emit — so the panel shows heat
 * as light, the one place a fill is the point (style.md's no-fill rule is for
 * readouts, not this image).
 */
@Composable
fun ThermalPanel(grid: ThermalGrid, units: Units) {
    Column {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text(
                "THERMAL GRID", color = G.Bone, fontFamily = G.Mono, fontSize = 13.sp,
                letterSpacing = 1.sp,
            )
            Spacer(Modifier.width(10.dp))
            Text(
                "${formatTemp(grid.minC, units)} – ${formatTemp(grid.maxC, units)}",
                color = G.Phosphor, fontFamily = G.Mono, fontSize = 13.sp,
            )
        }
        Spacer(Modifier.height(6.dp))
        Canvas(Modifier.size(132.dp).border(2.dp, G.Grid)) {
            val cw = size.width / ThermalGrid.W
            val ch = size.height / ThermalGrid.H
            for (row in 0 until ThermalGrid.H) {
                for (col in 0 until ThermalGrid.W) {
                    val (r, g, b) = ironbow(grid.cells[row * ThermalGrid.W + col])
                    drawRect(
                        color = Color(r, g, b),
                        topLeft = Offset(col * cw, row * ch),
                        // Slightly overdraw so anti-aliasing leaves no black seams.
                        size = Size(cw + 0.75f, ch + 0.75f),
                    )
                }
            }
        }
    }
}
