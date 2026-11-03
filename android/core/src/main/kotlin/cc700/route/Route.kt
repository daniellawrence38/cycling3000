package cc700.route

import cc700.util.ByteBuf
import cc700.util.objectId
import cc700.util.varint
import cc700.util.zigzag
import org.xml.sax.Attributes
import org.xml.sax.helpers.DefaultHandler
import java.io.InputStream
import javax.xml.parsers.SAXParserFactory
import kotlin.math.abs
import kotlin.math.asin
import kotlin.math.atan2
import kotlin.math.ceil
import kotlin.math.cos
import kotlin.math.floor
import kotlin.math.max
import kotlin.math.min
import kotlin.math.roundToInt
import kotlin.math.sin
import kotlin.math.sqrt

/** A point in degrees x 1e7, with the GPX elevation if the file had one. */
data class Pt(val latE7: Int, val lonE7: Int, val ele: Double? = null)

data class Turn(val distM: Long, val type: Int, val exit: Int = 0, val street: String = "")
data class Climb(val startM: Long, val lengthM: Int, val ascentM: Int, val avgX10: Int, val maxX10: Int)

object TurnType {
    const val CONTINUE = 0; const val SLIGHT_LEFT = 1; const val LEFT = 2; const val SHARP_LEFT = 3
    const val SLIGHT_RIGHT = 4; const val RIGHT = 5; const val SHARP_RIGHT = 6; const val UTURN = 7
    const val ROUNDABOUT = 8; const val ARRIVE = 9; const val DEPART = 10
}

class GpxError(msg: String) : Exception(msg)

// ------------------------------------------------------------------ GPX (brief 7.4)
/** `<trk>` segments joined, or `<rte>` points; waypoints ignored. Returns the name and points. */
fun parseGpx(input: InputStream): Pair<String, List<Pt>> {
    val trk = ArrayList<Pt>()
    val rte = ArrayList<Pt>()
    var name = ""
    val f = SAXParserFactory.newInstance()
    f.isNamespaceAware = true
    try {
        f.setFeature("http://apache.org/xml/features/disallow-doctype-decl", true)
    } catch (_: Exception) {
    }
    f.newSAXParser().parse(input, object : DefaultHandler() {
        var cur: String? = null
        var lat = 0.0
        var lon = 0.0
        var ele: Double? = null
        val text = StringBuilder()
        var depthName = false

        override fun startElement(uri: String?, local: String, q: String?, a: Attributes) {
            text.setLength(0)
            when (local) {
                "trkpt", "rtept" -> {
                    cur = local
                    lat = a.getValue("lat")?.toDoubleOrNull() ?: throw GpxError("point without lat")
                    lon = a.getValue("lon")?.toDoubleOrNull() ?: throw GpxError("point without lon")
                    ele = null
                }
                "name" -> depthName = cur == null && name.isEmpty()
            }
        }

        override fun characters(ch: CharArray, start: Int, length: Int) {
            text.appendRange(ch, start, start + length)
        }

        override fun endElement(uri: String?, local: String, q: String?) {
            when (local) {
                "ele" -> if (cur != null) ele = text.toString().trim().toDoubleOrNull()
                "name" -> if (depthName) { name = text.toString().trim(); depthName = false }
                "trkpt", "rtept" -> {
                    val p = Pt((lat * 1e7).roundToInt(), (lon * 1e7).roundToInt(), ele)
                    if (local == "trkpt") trk += p else rte += p
                    cur = null
                }
            }
        }
    })
    val pts = if (trk.isNotEmpty()) trk else rte
    if (pts.size < 2) throw GpxError("the file has fewer than two track or route points")
    return name to pts
}

// ------------------------------------------------------------------ geometry
fun haversineM(a: Pt, b: Pt): Double {
    val r = 6371000.0
    val la1 = Math.toRadians(a.latE7 / 1e7); val lo1 = Math.toRadians(a.lonE7 / 1e7)
    val la2 = Math.toRadians(b.latE7 / 1e7); val lo2 = Math.toRadians(b.lonE7 / 1e7)
    val h = sin((la2 - la1) / 2).let { it * it } + cos(la1) * cos(la2) * sin((lo2 - lo1) / 2).let { it * it }
    return 2 * r * asin(sqrt(h))
}

private class Xy(val x: Double, val y: Double)

private fun project(pts: List<Pt>): List<Xy> {
    val lat0 = pts.sumOf { it.latE7.toLong() } / pts.size / 1e7
    val kx = 111320.0 * cos(Math.toRadians(lat0))
    return pts.map { Xy(it.lonE7 / 1e7 * kx, it.latE7 / 1e7 * 111320.0) }
}

private fun segDist(p: Xy, a: Xy, b: Xy): Double {
    val dx = b.x - a.x; val dy = b.y - a.y
    val l2 = dx * dx + dy * dy
    val t = if (l2 > 0) ((p.x - a.x) * dx + (p.y - a.y) * dy) / l2 else 0.0
    val c = t.coerceIn(0.0, 1.0)
    val ex = a.x + c * dx - p.x; val ey = a.y + c * dy - p.y
    return sqrt(ex * ex + ey * ey)
}

private fun douglasPeucker(xy: List<Xy>, tol: Double): BooleanArray {
    val keep = BooleanArray(xy.size)
    keep[0] = true; keep[xy.size - 1] = true
    val stack = ArrayDeque<Pair<Int, Int>>()
    stack.addLast(0 to xy.size - 1)
    while (stack.isNotEmpty()) {
        val (s, e) = stack.removeLast()
        var best = -1; var bd = 0.0
        for (i in s + 1 until e) {
            val d = segDist(xy[i], xy[s], xy[e])
            if (d > bd) { bd = d; best = i }
        }
        if (best >= 0 && bd > tol) {
            keep[best] = true
            stack.addLast(s to best); stack.addLast(best to e)
        }
    }
    return keep
}

/** Brief 7.4: drop points closer than 1 m to the previous, then Douglas-Peucker at 3 m, raising the
 * tolerance 1 m at a time until at most [maxPts] points remain. */
fun simplify(raw: List<Pt>, maxPts: Int = 8000): List<Pt> {
    val d = ArrayList<Pt>()
    for (p in raw) if (d.isEmpty() || haversineM(d.last(), p) >= 1.0) d += p
    if (d.size >= 2 && d.last() != raw.last()) d[d.size - 1] = raw.last()
    if (d.size < 2) return d
    val xy = project(d)
    var tol = 3.0
    while (true) {
        val keep = douglasPeucker(xy, tol)
        val out = d.filterIndexed { i, _ -> keep[i] }
        if (out.size <= maxPts) return out
        tol += 1.0
    }
}

fun cumulative(pts: List<Pt>): DoubleArray {
    val c = DoubleArray(pts.size)
    for (i in 1 until pts.size) c[i] = c[i - 1] + haversineM(pts[i - 1], pts[i])
    return c
}

/** Profile spacing S = max(25, ceil(dist / 12000)) metres (brief 7.4). */
fun profileStep(distM: Double): Int = max(25, ceil(distM / 12000).toInt())

/** Positions along the line at every S metres, for sampling a DEM. */
fun samplePositions(pts: List<Pt>, step: Int): List<Pt> {
    val cum = cumulative(pts)
    val n = floor(cum.last() / step).toInt() + 1
    val out = ArrayList<Pt>(n)
    var j = 0
    for (k in 0 until n) {
        val d = k.toDouble() * step
        while (j < pts.size - 2 && cum[j + 1] < d) j++
        val seg = cum[j + 1] - cum[j]
        val f = if (seg > 0) ((d - cum[j]) / seg).coerceIn(0.0, 1.0) else 0.0
        val a = pts[j]; val b = pts[j + 1]
        val ele = if (a.ele != null && b.ele != null) a.ele + f * (b.ele - a.ele) else null
        out += Pt((a.latE7 + f * (b.latE7 - a.latE7)).roundToInt(), (a.lonE7 + f * (b.lonE7 - a.lonE7)).roundToInt(), ele)
    }
    return out
}

/** Fills gaps (null) by linear interpolation, then a 3-sample median and a 5-sample mean (brief 7.4). */
fun smoothProfile(raw: List<Double?>): DoubleArray {
    val n = raw.size
    val e = DoubleArray(n)
    val known = raw.indices.filter { raw[it] != null }
    require(known.isNotEmpty()) { "no elevation" }
    for (i in 0 until n) {
        val v = raw[i]
        if (v != null) { e[i] = v; continue }
        val before = known.lastOrNull { it < i }; val after = known.firstOrNull { it > i }
        e[i] = when {
            before == null -> raw[after!!]!!
            after == null -> raw[before]!!
            else -> raw[before]!! + (raw[after]!! - raw[before]!!) * (i - before) / (after - before).toDouble()
        }
    }
    val med = DoubleArray(n) { i ->
        if (i == 0 || i == n - 1) e[i] else listOf(e[i - 1], e[i], e[i + 1]).sorted()[1]
    }
    return DoubleArray(n) { i ->
        val lo = max(0, i - 2); val hi = min(n - 1, i + 2)
        (lo..hi).sumOf { med[it] } / (hi - lo + 1)
    }
}

// ------------------------------------------------------------------ climbs: ref.detect_climbs, exactly
data class ClimbParams(
    val hysteresis: Double = 5.0, val mergeDrop: Double = 8.0, val mergeGap: Int = 150,
    val minLen: Int = 300, val minAscent: Double = 15.0, val minGrade: Double = 3.0,
)

private fun pyHalfUp(x: Double): Int = floor((x * 2 + 1) / 2).toInt()

fun detectClimbs(e: DoubleArray, spacing: Int, p: ClimbParams = ClimbParams()): List<Climb> {
    val n = e.size
    if (n == 0) return emptyList()
    val raw = ArrayList<IntArray>()
    var v = 0; var m = 0
    for (i in 1 until n) {
        if (e[m] - e[i] >= p.hysteresis) {
            raw += intArrayOf(v, m)
            var best = m
            for (k in m..i) if (e[k] < e[best]) best = k
            v = best; m = v
        } else if (e[i] <= e[v]) {
            v = i; m = i
        } else if (e[i] > e[m]) {
            m = i
        }
    }
    raw += intArrayOf(v, m)
    val merged = ArrayList<IntArray>()
    for (seg in raw) {
        if (seg[1] == seg[0]) continue
        if (merged.isNotEmpty()) {
            val (pv, pm) = merged.last().let { it[0] to it[1] }
            if (e[pm] - e[seg[0]] <= p.mergeDrop && (seg[0] - pm) * spacing <= p.mergeGap) {
                merged[merged.size - 1] = intArrayOf(pv, if (e[seg[1]] >= e[pm]) seg[1] else pm)
                continue
            }
        }
        merged += seg
    }
    val out = ArrayList<Climb>()
    for (seg in merged) {
        val (cv, cm) = seg[0] to seg[1]
        val length = (cm - cv) * spacing
        val asc = e[cm] - e[cv]
        if (length < p.minLen || asc < p.minAscent || asc / length * 100 < p.minGrade) continue
        val w = max(1, 50 / spacing)
        val mg = if (cm - cv >= w) (cv..cm - w).maxOf { (e[it + w] - e[it]) / (w * spacing) * 100 } else asc / length * 100
        out += Climb(cv.toLong() * spacing, length, pyHalfUp(asc), pyHalfUp(asc / length * 1000), pyHalfUp(mg * 10))
    }
    return out
}

// ------------------------------------------------------------------ turns from geometry (brief 7.5)
private fun bearing(a: Xy, b: Xy): Double = Math.toDegrees(atan2(b.x - a.x, b.y - a.y))

/** Turns from geometry (brief 7.5, QUESTIONS Q25). The heading change at each vertex of the simplified
 * line is summed over clusters of vertices less than 40 m apart, so a corner drawn with several points
 * counts once, with its full angle. Positive is to the right. */
fun generateTurns(pts: List<Pt>): List<Turn> {
    val turns = ArrayList<Turn>()
    turns += Turn(0, TurnType.DEPART)
    val cum = cumulative(pts)
    if (pts.size >= 3) {
        val xy = project(pts)
        var clusterAngle = 0.0
        var clusterStart = -1.0
        var clusterAt = 0.0
        var lastVertex = -1e9
        fun flush() {
            if (clusterStart < 0) return
            val a = abs(clusterAngle)
            val right = clusterAngle > 0
            val type = when {
                a < 35 -> null
                a < 60 -> if (right) TurnType.SLIGHT_RIGHT else TurnType.SLIGHT_LEFT
                a < 120 -> if (right) TurnType.RIGHT else TurnType.LEFT
                a < 165 -> if (right) TurnType.SHARP_RIGHT else TurnType.SHARP_LEFT
                else -> TurnType.UTURN
            }
            if (type != null) turns += Turn(clusterAt.roundToInt().toLong(), type)
            clusterStart = -1.0
            clusterAngle = 0.0
        }
        for (i in 1 until pts.size - 1) {
            var d = bearing(xy[i], xy[i + 1]) - bearing(xy[i - 1], xy[i])
            while (d > 180) d -= 360
            while (d < -180) d += 360
            if (abs(d) < 1.0) continue
            if (clusterStart >= 0 && cum[i] - lastVertex >= 40) flush()
            if (clusterStart < 0) { clusterStart = cum[i]; clusterAt = cum[i] }
            if (abs(d) > 20 && abs(clusterAngle) < 20) clusterAt = cum[i] /* the turn is where it is sharpest */
            clusterAngle += d
            lastVertex = cum[i]
        }
        flush()
    }
    turns += Turn(cum.last().roundToInt().toLong(), TurnType.ARRIVE)
    return turns
}

// ------------------------------------------------------------------ CR1 (protocol 3.3)
data class RouteObject(val bytes: ByteArray, val id: ByteArray, val distM: Long, val ascentM: Int)

/** Encodes as ref.encode_route: distance from the points, ascent from the profile (decimetres). */
fun encodeCr1(name: String, pts: List<Pt>, profStep: Int, profileDm: List<Int>, turns: List<Turn>, climbs: List<Climb>): RouteObject {
    require(pts.size in 2..8000 && turns.size <= 500 && climbs.size <= 60 && profileDm.size <= 12000)
    val dist = (0 until pts.size - 1).sumOf { haversineM(pts[it], pts[it + 1]) }.let { floor(it + 0.5).toLong() }
    val ascent = (0 until profileDm.size - 1).sumOf { max(0, profileDm[it + 1] - profileDm[it]) } / 10
    val nm = name.toByteArray(Charsets.US_ASCII)
    val b = ByteBuf(1024)
    b.u8('C'.code).u8('R'.code).u8(1).u8(0)
    repeat(8) { b.u8(0) }
    b.u32(dist).u16(ascent).u16(pts.size).u16(turns.size).u8(climbs.size).u16(profileDm.size)
        .u16(if (profileDm.isEmpty()) 0 else profStep).u8(nm.size).bytes(nm)
    b.i32(pts[0].latE7).i32(pts[0].lonE7)
    for (i in 1 until pts.size) {
        varint(zigzag((pts[i].latE7 - pts[i - 1].latE7).toLong()), b)
        varint(zigzag((pts[i].lonE7 - pts[i - 1].lonE7).toLong()), b)
    }
    for (a in profileDm) b.u16(a and 0xFFFF)
    for (t in turns) {
        val st = t.street.toByteArray(Charsets.US_ASCII)
        b.u32(t.distM).u8(t.type).u8(t.exit).u8(st.size).bytes(st)
    }
    for (c in climbs) b.u32(c.startM).u16(c.lengthM).u16(c.ascentM).u16(c.avgX10 and 0xFFFF).u16(c.maxX10 and 0xFFFF)
    val obj = b.toByteArray()
    val id = objectId(obj)
    id.copyInto(obj, 4)
    return RouteObject(obj, id, dist, ascent)
}

/** The whole phone-side pipeline: simplify, profile, climbs, turns, CR1. [elevations] gives one value
 * (or null) per sample position; null everywhere falls back to the GPX's own elevations. */
class BuiltRoute(val obj: RouteObject, val points: List<Pt>, val profileM: DoubleArray, val step: Int,
                 val climbs: List<Climb>, val turns: List<Turn>, val fromFile: Boolean)

fun buildRoute(name: String, raw: List<Pt>, elevations: (List<Pt>) -> List<Double?>, params: ClimbParams = ClimbParams()): BuiltRoute {
    val pts = simplify(raw)
    val cum = cumulative(pts)
    val step = profileStep(cum.last())
    val samples = samplePositions(raw.ifEmpty { pts }, step)
    var elev = elevations(samples)
    var fromFile = false
    if (elev.all { it == null }) {
        elev = samples.map { it.ele }
        fromFile = true
    }
    val prof = if (elev.any { it != null }) smoothProfile(elev) else DoubleArray(0)
    val climbs = if (prof.isNotEmpty()) detectClimbs(prof, step, params).take(60) else emptyList()
    val turns = generateTurns(pts).take(500)
    val dm = prof.map { (it * 10).roundToInt().coerceIn(-32768, 32767) }.take(12000)
    val safeName = name.filter { it.code in 0x20..0x7E }.take(40).ifBlank { "Route" }
    return BuiltRoute(encodeCr1(safeName, pts, step, dm, turns, climbs), pts, prof, step, climbs, turns, fromFile)
}
