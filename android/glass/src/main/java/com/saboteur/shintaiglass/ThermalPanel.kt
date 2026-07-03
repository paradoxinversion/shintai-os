package com.saboteur.shintaiglass

import android.graphics.Bitmap
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
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.FilterQuality
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.unit.IntSize
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.saboteur.shintai.core.ThermalGrid
import com.saboteur.shintai.core.Units
import com.saboteur.shintai.core.argb
import com.saboteur.shintai.core.formatTemp
import kotlin.math.roundToInt

/**
 * Metsuke's live thermal grid — the HUD's one *image* surface (Shikai as a sense,
 * not a readout). A 16×12 false-colour heat panel, auto-ranged to the frame's
 * min/max (shown as the label) and **bilinear-upscaled** so the coarse grid reads
 * as a smooth heat cloud rather than blocks. Ironbow-on-black suits the waveguide:
 * cold cells are black (emit no light → see-through), hot cells emit — so the panel
 * shows heat as light, the one place a fill is the point (style.md's no-fill rule is
 * for readouts, not this image).
 */
@Composable
fun ThermalPanel(grid: ThermalGrid, units: Units) {
    // Wrap the packed grid as a tiny W×H bitmap; the Canvas scales it up with
    // bilinear filtering (FilterQuality), so interpolation is free on the GPU.
    val heat = remember(grid) {
        Bitmap.createBitmap(grid.argb(), ThermalGrid.W, ThermalGrid.H, Bitmap.Config.ARGB_8888)
            .asImageBitmap()
    }
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
        // 4:3 panel matches the 16×12 grid's native aspect — no geometric distortion.
        Canvas(Modifier.width(160.dp).height(120.dp).border(2.dp, G.Grid)) {
            drawImage(
                image = heat,
                dstSize = IntSize(size.width.roundToInt(), size.height.roundToInt()),
                filterQuality = FilterQuality.High,
            )
        }
    }
}
