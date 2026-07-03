package com.saboteur.shintaioperator

import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Outline
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.Shape
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.unit.Density
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.LayoutDirection
import androidx.compose.ui.unit.dp

/**
 * The SHINTAI-OS instrument language for the Operator, per docs/style.md §2/§10.
 * Emissive tokens on a near-black void; color is meaning, never decoration
 * (green = nominal, amber = caution, red = alarm, bone = printed chrome).
 *
 * Typography: style.md names Michroma / IBM Plex Mono / DSEG / VT323 as the ideal
 * faces. Those are a bundled-font follow-up; until then everything uses the
 * platform monospace — legible, on-brand enough, and priority #1 (functional).
 */
object T {
    val Void = Color(0xFF05080A)
    val Panel = Color(0xFF0C1410)
    val Grid = Color(0xFF1C4028)
    val Phosphor = Color(0xFF58F07A)
    val PhosphorDim = Color(0xFF2E7A45)
    val Amber = Color(0xFFF2A93B)
    val AmberDim = Color(0xFF7A5620)
    val Alert = Color(0xFFFF4438)
    val AlertDim = Color(0xFF8A2820)
    val Bone = Color(0xFFC9CDBC)
    val BoneDim = Color(0xFF6B6F62)

    val Mono = FontFamily.Monospace
    val Chamfer = 4.dp
}

/**
 * Chamfered rectangle — corners cut at 45°, never rounded (style.md §4:
 * "Chamfers, not radii… rounded corners read modern/consumer"). Used for panel
 * fills, borders, and buttons.
 */
class ChamferShape(private val cut: Dp = T.Chamfer) : Shape {
    override fun createOutline(size: Size, layoutDirection: LayoutDirection, density: Density): Outline {
        val c = with(density) { cut.toPx() }.coerceAtMost(minOf(size.width, size.height) / 2f)
        val p = Path().apply {
            moveTo(c, 0f)
            lineTo(size.width - c, 0f)
            lineTo(size.width, c)
            lineTo(size.width, size.height - c)
            lineTo(size.width - c, size.height)
            lineTo(c, size.height)
            lineTo(0f, size.height - c)
            lineTo(0f, c)
            close()
        }
        return Outline.Generic(p)
    }
}

/** Display/tracker thresholds. NEAR_MM lives in :core (it mirrors the firmware);
 *  FAR_MM and the range ceiling are consumer-side presentation bands only. */
object Bands {
    const val FAR_MM = 1000           // beyond this = clear/green; NEAR_MM..FAR_MM = amber
    const val RANGE_MAX_MM = 4000     // outer ring of the motion tracker
    const val CO2_WARN_PPM = 1000     // amber above this
    const val CO2_ALARM_PPM = 1600    // red above this
    const val CO2_FULLSCALE_PPM = 2000 // segment-bar ceiling
    const val HISTORY = 90            // rolling samples kept for sparklines
}

/** Pull the CO₂ ppm out of a Climate payload ("23.0C 41%RH 750ppm"), or null. */
fun co2Ppm(climate: String): Int? =
    Regex("""(\d+)\s*ppm""").find(climate)?.groupValues?.get(1)?.toIntOrNull()
