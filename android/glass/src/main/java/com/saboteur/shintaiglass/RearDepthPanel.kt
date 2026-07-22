package com.saboteur.shintaiglass

import android.graphics.Bitmap
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.size
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.FilterQuality
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.unit.IntSize
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.saboteur.shintai.core.DepthGrid
import com.saboteur.shintai.core.argb
import kotlin.math.roundToInt

/**
 * Zanshin's live rear depth field — the HUD's rear-facing image surface (the complement to
 * Metsuke's forward thermal panel). An 8×8 VL53L5CX depth grid behind the pack: a near zone
 * reads as alarming red, fading through amber/green to a cool far blue; no-target zones stay
 * black (see-through on the waveguide). Bilinear-upscaled so the coarse 8×8 reads smooth —
 * "what's behind you," as a field rather than two blips.
 */
@Composable
fun RearDepthPanel(grid: DepthGrid) {
    val depth = remember(grid) {
        Bitmap.createBitmap(grid.argb(), DepthGrid.W, DepthGrid.H, Bitmap.Config.ARGB_8888)
            .asImageBitmap()
    }
    Column {
        Text(
            "REAR FIELD", color = G.Bone, fontFamily = G.Mono, fontSize = 13.sp,
            letterSpacing = 1.sp,
        )
        Spacer(Modifier.height(6.dp))
        // Square panel — the 8×8 field's native aspect.
        Canvas(Modifier.size(120.dp).border(2.dp, G.Grid)) {
            drawImage(
                image = depth,
                dstSize = IntSize(size.width.roundToInt(), size.height.roundToInt()),
                filterQuality = FilterQuality.High,
            )
        }
    }
}
