package cc700.app.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.produceState
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.StrokeJoin
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.drawscope.clipRect
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.IntSize
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import cc700.app.graph
import cc700.route.Pt
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import kotlin.math.PI
import kotlin.math.atan2
import kotlin.math.cos
import kotlin.math.floor
import kotlin.math.ln
import kotlin.math.max
import kotlin.math.min
import kotlin.math.roundToInt
import kotlin.math.sin
import kotlin.math.tan

/** Web Mercator in "world pixels" at zoom [z] (256-pixel tiles). */
private fun worldX(lonDeg: Double, z: Int) = (lonDeg + 180.0) / 360.0 * 256.0 * (1 shl z)
private fun worldY(latDeg: Double, z: Int): Double {
    val r = Math.toRadians(latDeg.coerceIn(-85.0, 85.0))
    return (1 - ln(tan(r) + 1 / cos(r)) / PI) / 2 * 256.0 * (1 shl z)
}

private data class View(val z: Int, val left: Double, val top: Double, val scale: Float)

/** The largest zoom (up to 16) at which the route's box, with a margin, fits [wPx] x [hPx]. Tiles are
 * drawn at [scale] screen pixels per map pixel so labels stay legible on dense screens. */
private fun fit(pts: List<Pt>, wPx: Float, hPx: Float, scale: Float): View {
    val lats = pts.map { it.latE7 / 1e7 }; val lons = pts.map { it.lonE7 / 1e7 }
    val w = wPx / scale; val h = hPx / scale
    var z = 16
    while (z > 3) {
        val x0 = worldX(lons.min(), z); val x1 = worldX(lons.max(), z)
        val y0 = worldY(lats.max(), z); val y1 = worldY(lats.min(), z)
        if ((x1 - x0) * 1.15 + 24 <= w && (y1 - y0) * 1.15 + 24 <= h) break
        z--
    }
    val cx = (worldX(lons.min(), z) + worldX(lons.max(), z)) / 2
    val cy = (worldY(lats.max(), z) + worldY(lats.min(), z)) / 2
    return View(z, cx - w / 2, cy - h / 2, scale)
}

/**
 * A small map of a route on OpenStreetMap: the line, a green start dot and arrows for the direction of
 * travel. Enough to see roughly where a loop goes. Without tiles (offline) the line is drawn on a plain
 * background.
 */
@Composable
fun RouteMap(points: List<Pt>, modifier: Modifier = Modifier.fillMaxWidth(), height: Dp = 200.dp) {
    if (points.size < 2) return
    val tiles = LocalContext.current.graph.tiles
    val density = LocalDensity.current.density
    // At most ~600 points for drawing; the shape is what matters here.
    val pts = remember(points) { val step = max(1, points.size / 600); points.filterIndexed { i, _ -> i % step == 0 } + points.last() }
    BoxWithConstraints(modifier.height(height).background(Tok.panel)) {
        val wPx = constraints.maxWidth.toFloat()
        val hPx = constraints.maxHeight.toFloat()
        val view = remember(pts, wPx, hPx) { fit(pts, wPx, hPx, max(1f, density / 1.5f)) }
        val loaded by produceState(emptyMap<Triple<Int, Int, Int>, ImageBitmap>(), view) {
            val z = view.z
            val n = 1 shl z
            val tx0 = floor(view.left / 256).toInt(); val ty0 = floor(view.top / 256).toInt()
            val tx1 = floor((view.left + wPx / view.scale) / 256).toInt(); val ty1 = floor((view.top + hPx / view.scale) / 256).toInt()
            val acc = HashMap<Triple<Int, Int, Int>, ImageBitmap>()
            for (ty in ty0..ty1) for (tx in tx0..tx1) {
                if (ty !in 0 until n) continue
                val wx = Math.floorMod(tx, n)
                val img = withContext(Dispatchers.IO) { tiles.get(z, wx, ty) } ?: continue
                acc[Triple(z, tx, ty)] = img
                value = HashMap(acc)
            }
        }
        Canvas(Modifier.fillMaxSize()) {
            clipRect {
                for ((k, img) in loaded) {
                    val (_, tx, ty) = k
                    val dx = ((tx * 256 - view.left) * view.scale).roundToInt()
                    val dy = ((ty * 256 - view.top) * view.scale).roundToInt()
                    val s = (256 * view.scale).roundToInt()
                    drawImage(img, dstOffset = IntOffset(dx, dy), dstSize = IntSize(s, s))
                }
                // Dim the map a little so the route stands out.
                drawRect(Color.Black.copy(alpha = 0.15f))
                fun at(p: Pt) = Offset(
                    ((worldX(p.lonE7 / 1e7, view.z) - view.left) * view.scale).toFloat(),
                    ((worldY(p.latE7 / 1e7, view.z) - view.top) * view.scale).toFloat())
                val xy = pts.map(::at)
                val path = Path().apply { moveTo(xy[0].x, xy[0].y); for (i in 1 until xy.size) lineTo(xy[i].x, xy[i].y) }
                drawPath(path, Color(0xFF0B0D10), style = Stroke(width = 7.dp.toPx(), cap = StrokeCap.Round, join = StrokeJoin.Round))
                drawPath(path, Tok.route, style = Stroke(width = 4.dp.toPx(), cap = StrokeCap.Round, join = StrokeJoin.Round))
                // Direction arrows at 1/6 .. 5/6 of the way round.
                val cum = DoubleArray(xy.size)
                for (i in 1 until xy.size) cum[i] = cum[i - 1] + (xy[i] - xy[i - 1]).getDistance()
                val total = cum.last()
                if (total > 0) for (k in 1..5) {
                    val target = total * k / 6
                    val i = cum.indexOfFirst { it >= target }.coerceIn(1, xy.size - 1)
                    val a = xy[max(0, i - 3)]; val b = xy[min(xy.size - 1, i + 3)]
                    val ang = atan2((b.y - a.y).toDouble(), (b.x - a.x).toDouble())
                    val c = xy[i]
                    val len = 9.dp.toPx(); val wing = 0.55
                    val tip = Offset(c.x + (len * cos(ang)).toFloat(), c.y + (len * sin(ang)).toFloat())
                    val l = Offset(c.x - (len * cos(ang - wing)).toFloat(), c.y - (len * sin(ang - wing)).toFloat())
                    val r = Offset(c.x - (len * cos(ang + wing)).toFloat(), c.y - (len * sin(ang + wing)).toFloat())
                    drawPath(Path().apply { moveTo(tip.x, tip.y); lineTo(l.x, l.y); lineTo(r.x, r.y); close() }, Color.White)
                }
                drawCircle(Color(0xFF0B0D10), radius = 8.dp.toPx(), center = xy[0])
                drawCircle(Tok.ok, radius = 6.dp.toPx(), center = xy[0])
            }
        }
        Text("© OpenStreetMap contributors", color = Color(0xFF333333), fontSize = 9.sp,
            modifier = Modifier.align(Alignment.BottomEnd).background(Color.White.copy(alpha = 0.7f)).padding(horizontal = 3.dp))
    }
}
