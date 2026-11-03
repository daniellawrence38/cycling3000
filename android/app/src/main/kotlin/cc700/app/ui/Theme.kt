package cc700.app.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import cc700.route.Climb
import cc700.sync.PreviewStep

/** Design tokens (spec 12). */
object Tok {
    val bg = Color(0xFF0B0D10)
    val bar = Color(0xFF12161B)
    val panel = Color(0xFF1B2027)
    val divider = Color(0xFF232830)
    val muted = Color(0xFF9AA4B0)
    val text = Color(0xFFF2F5F8)
    val route = Color(0xFF4C9AFF)
    val path = Color(0xFF4FD1C5)
    val zones = listOf(0xFF8E9AA6, 0xFF3B8BEB, 0xFF3DBE6B, 0xFFF2C230, 0xFFF08A24, 0xFFE5484D, 0xFFB46BE8).map { Color(it) }
    val ok = Color(0xFF3DBE6B)
    val warn = Color(0xFFF2C230)
    val bad = Color(0xFFE5484D)
    fun zone(z: Int) = zones[(z - 1).coerceIn(0, 6)]
}

private val scheme = darkColorScheme(
    primary = Tok.route, onPrimary = Tok.bg,
    background = Tok.bg, onBackground = Tok.text,
    surface = Tok.bar, onSurface = Tok.text,
    surfaceVariant = Tok.panel, onSurfaceVariant = Tok.muted,
    surfaceContainer = Tok.bar, surfaceContainerHigh = Tok.panel,
    outline = Tok.divider, error = Tok.bad, onError = Tok.bg,
)

@Composable
fun CcTheme(content: @Composable () -> Unit) = MaterialTheme(colorScheme = scheme, content = content)

/** A status chip: dark text on the colour, and the state is always in the words (spec 12). */
@Composable
fun Chip(text: String, colour: Color) {
    Box(Modifier.background(colour, RoundedCornerShape(4.dp)).padding(horizontal = 6.dp, vertical = 2.dp)) {
        Text(text, color = Tok.bg, fontSize = 12.sp, fontWeight = FontWeight.SemiBold)
    }
}

fun hms(s: Long): String = if (s >= 3600) "%d:%02d:%02d".format(s / 3600, s / 60 % 60, s % 60) else "%d:%02d".format(s / 60, s % 60)
fun km(m: Long): String = "%.1f km".format(m / 1000.0)

/** The workout as bars: width by duration, height by watts, colour by zone; ramps as slopes. */
@Composable
fun WorkoutChart(steps: List<PreviewStep>, modifier: Modifier = Modifier.fillMaxWidth().height(120.dp)) {
    Canvas(modifier.background(Tok.panel)) {
        val total = steps.sumOf { it.secs }.coerceAtLeast(1)
        val top = (steps.maxOfOrNull { maxOf(it.lowW, it.highW) } ?: 1).coerceAtLeast(1) * 1.1f
        var x = 0f
        for (s in steps) {
            val w = size.width * s.secs / total
            fun y(watts: Int) = size.height * (1 - watts / top)
            val c = Tok.zone(s.zone)
            if (s.ramp) {
                val p = Path().apply {
                    moveTo(x, size.height); lineTo(x, y(s.lowW)); lineTo(x + w, y(s.highW)); lineTo(x + w, size.height); close()
                }
                drawPath(p, c)
            } else {
                val mid = (s.lowW + s.highW) / 2
                drawRect(c, Offset(x, y(mid)), Size((w - 1f).coerceAtLeast(1f), size.height - y(mid)))
            }
            x += w
        }
    }
}

/** Elevation profile with the climbs shaded. */
@Composable
fun ProfileChart(profile: DoubleArray, step: Int, climbs: List<Climb>, modifier: Modifier = Modifier.fillMaxWidth().height(120.dp)) {
    Canvas(modifier.background(Tok.panel)) {
        if (profile.size < 2) return@Canvas
        val lo = profile.min(); val hi = profile.max().coerceAtLeast(lo + 10)
        val dist = (profile.size - 1).toFloat() * step
        fun px(m: Float) = size.width * m / dist
        fun py(e: Double) = (size.height * (1 - (e - lo) / (hi - lo) * 0.9)).toFloat()
        for (c in climbs) drawRect(Tok.warn.copy(alpha = 0.25f), Offset(px(c.startM.toFloat()), 0f),
            Size(px(c.lengthM.toFloat()), size.height))
        val p = Path().apply {
            moveTo(0f, size.height)
            profile.forEachIndexed { i, e -> lineTo(px(i.toFloat() * step), py(e)) }
            lineTo(size.width, size.height); close()
        }
        drawPath(p, Tok.route.copy(alpha = 0.6f))
    }
}
