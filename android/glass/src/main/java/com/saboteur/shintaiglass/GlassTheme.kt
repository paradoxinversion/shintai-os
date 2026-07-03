package com.saboteur.shintaiglass

import androidx.compose.animation.core.RepeatMode
import androidx.compose.animation.core.animateFloat
import androidx.compose.animation.core.infiniteRepeatable
import androidx.compose.animation.core.rememberInfiniteTransition
import androidx.compose.animation.core.tween
import androidx.compose.runtime.Composable
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Outline
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.Shape
import androidx.compose.ui.text.font.Font
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.Density
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.LayoutDirection
import androidx.compose.ui.unit.dp
import com.saboteur.shintai.core.R as CoreR

/**
 * The SHINTAI-OS instrument language on the RayNeo waveguide (docs/style.md §8).
 * The waveguide rules are non-negotiable: pure-black ground (black pixels emit no
 * light → read as see-through over the world), NO panel fills — bright strokes
 * only, no mid-tones, and amber/red reserved for real alerts to avoid alarm
 * fatigue. Phosphor green is the default channel.
 *
 * Typography caveat (§3): no pixel fonts on the waveguide (VT323 turns to mush) —
 * IBM Plex Mono for text, DSEG only at large size for the one hero value. Fonts
 * are shared from :core.
 */
object G {
    val Black = Color.Black          // waveguide: black == transparent
    val Phosphor = Color(0xFF58F07A) // primary: text, live data, nominal
    val PhosphorDim = Color(0xFF2E7A45)
    val Amber = Color(0xFFF2A93B)    // caution (reserved)
    val Alert = Color(0xFFFF4438)    // alarm (reserved) — proximity inside NEAR_MM
    val Bone = Color(0xFFC9CDBC)     // printed chrome: labels, units, titles
    val BoneDim = Color(0xFF6B6F62)
    val Grid = Color(0xFF2E7A45)     // strokes/leaders — dim phosphor (no grey mid-tones)

    val Mono = FontFamily(
        Font(CoreR.font.ibm_plex_mono_regular, FontWeight.Normal),
        Font(CoreR.font.ibm_plex_mono_bold, FontWeight.Bold),
    )
    val Title = FontFamily(Font(CoreR.font.michroma_regular))
    val Numeral = FontFamily(Font(CoreR.font.dseg7_classic_bold))
}

/** Chamfered rectangle — corners cut at 45°, never rounded (style.md §4). */
class ChamferShape(private val cut: Dp = 4.dp) : Shape {
    override fun createOutline(size: Size, layoutDirection: LayoutDirection, density: Density): Outline {
        val c = with(density) { cut.toPx() }.coerceAtMost(minOf(size.width, size.height) / 2f)
        val p = Path().apply {
            moveTo(c, 0f); lineTo(size.width - c, 0f); lineTo(size.width, c)
            lineTo(size.width, size.height - c); lineTo(size.width - c, size.height)
            lineTo(c, size.height); lineTo(0f, size.height - c); lineTo(0f, c); close()
        }
        return Outline.Generic(p)
    }
}

/** Steady ~1Hz blink for a proximity alarm — the only motion the waveguide allows
 *  besides a slow sweep (style.md §7). */
@Composable
fun alertBlink(): Float {
    val t = rememberInfiniteTransition(label = "alert")
    return t.animateFloat(
        initialValue = 1f, targetValue = 0.3f,
        animationSpec = infiniteRepeatable(tween(500), RepeatMode.Reverse),
        label = "alertA",
    ).value
}
