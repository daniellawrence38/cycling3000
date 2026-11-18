package cc700.route.plan

import cc700.route.Pt
import cc700.route.haversineM
import java.util.concurrent.Callable
import java.util.concurrent.ExecutionException
import java.util.concurrent.Executors
import kotlin.math.abs
import kotlin.math.cos
import kotlin.math.max
import kotlin.math.roundToInt

/** How a [Candidate] breaks down, as fractions of its distance (0..1). */
data class Breakdown(
    val cyclePath: Double,   // cycleway, plus path and track where the profile allows unpaved
    val quietRoad: Double,   // residential streets
    val busyRoad: Double,    // state roads
    val unpaved: Double,
    val steep: Double,       // steepness category 4 or 5 either way (about 10 % and over)
    val backtrack: Double,   // share of the loop that runs back over itself
)

data class Scored(val candidate: Candidate, val breakdown: Breakdown, val lengthError: Double, val score: Double)

/** What the rider asked for. */
data class PlanPrefs(
    val profile: BikeProfile = BikeProfile.ROAD,
    val favourPaths: Boolean = true,
    /** Average moving speed used to turn minutes into kilometres. */
    val speedKmh: Double = defaultSpeedKmh(BikeProfile.ROAD),
    val options: Int = 3,
    val tries: Int = 6,
    val seedBase: Int = 1,
)

fun defaultSpeedKmh(p: BikeProfile) = when (p) { BikeProfile.ROAD -> 22.0; BikeProfile.GRAVEL -> 16.0; BikeProfile.MTB -> 12.0 }

/** Rates candidates. Higher is better; the weights are a first guess, to be tuned on real rides. */
object Scorer {
    fun breakdown(c: Candidate, profile: BikeProfile): Breakdown {
        val seg = segmentLengths(c.points)
        val total = seg.sum().takeIf { it > 0 } ?: return Breakdown(0.0, 0.0, 0.0, 0.0, 0.0, 0.0)
        fun share(spans: List<Span>, pick: (Int) -> Boolean): Double {
            var d = 0.0
            for (s in spans) if (pick(s.value)) for (i in s.from until minOf(s.to, seg.size)) if (i >= 0) d += seg[i]
            return d / total
        }
        // Traffic-free: cycleways, and paths and footways the router chose to ride (in the UK most shared-use
        // paths are tagged footway or path with bicycle=yes, not cycleway). Tracks count for gravel and MTB
        // always, for road only where paved.
        val free = setOf(WayType.CYCLEWAY, WayType.PATH, WayType.FOOTWAY)
        val pavedTrack = trackPaved(c)
        return Breakdown(
            cyclePath = share(c.wayTypes) { it in free } +
                if (profile == BikeProfile.ROAD) pavedTrack / total else share(c.wayTypes) { it == WayType.TRACK },
            quietRoad = share(c.wayTypes) { it == WayType.STREET },
            busyRoad = share(c.wayTypes) { it == WayType.STATE_ROAD },
            unpaved = share(c.surfaces) { Surface.isUnpaved(it) },
            steep = share(c.steepness) { abs(it) >= 4 },
            backtrack = backtrackShare(c.points),
        )
    }

    fun score(c: Candidate, targetM: Double, prefs: PlanPrefs): Scored {
        val b = breakdown(c, prefs.profile)
        val err = if (targetM > 0) (c.distanceM - targetM) / targetM else 0.0
        val unpavedWeight = when (prefs.profile) { BikeProfile.ROAD -> 2.0; BikeProfile.GRAVEL -> 0.0; BikeProfile.MTB -> -0.5 }
        val s = (if (prefs.favourPaths) 3.0 else 0.5) * b.cyclePath +
            1.0 * b.quietRoad -
            2.0 * b.busyRoad -
            unpavedWeight * b.unpaved -
            1.0 * b.steep -
            1.5 * b.backtrack -
            2.0 * abs(err)
        return Scored(c, b, err, s)
    }

    private fun segmentLengths(p: List<Pt>) = DoubleArray(max(0, p.size - 1)) { haversineM(p[it], p[it + 1]) }

    /** Metres of track whose surface is paved. */
    private fun trackPaved(c: Candidate): Double {
        val seg = segmentLengths(c.points)
        val paved = BooleanArray(seg.size)
        for (s in c.surfaces) if (Surface.isPaved(s.value)) for (i in s.from until minOf(s.to, seg.size)) if (i >= 0) paved[i] = true
        var d = 0.0
        for (s in c.wayTypes) if (s.value == WayType.TRACK) for (i in s.from until minOf(s.to, seg.size)) if (i >= 0 && paved[i]) d += seg[i]
        return d
    }

    /** The share of distance (sampled every ~50 m) that lies within 25 m of a part of the route at
     * least 500 m away along it: out-and-back legs and repeated laps. */
    internal fun backtrackShare(pts: List<Pt>): Double {
        val s = resample(pts, 50.0)
        if (s.size < 4) return 0.0
        val xy = local(s)
        var hits = 0
        val skip = 10 // 10 samples x 50 m = 500 m
        for (i in xy.indices) {
            for (j in xy.indices) {
                if (abs(i - j) < skip) continue
                // A loop's start and end meet by design: ignore matches that close the loop.
                if (minOf(i, j) < skip && maxOf(i, j) > xy.size - 1 - skip) continue
                val dx = xy[i].first - xy[j].first
                val dy = xy[i].second - xy[j].second
                if (dx * dx + dy * dy < 25.0 * 25.0) { hits++; break }
            }
        }
        return hits.toDouble() / xy.size
    }
}

/** Points every [stepM] metres along [pts], keeping the first and last. */
internal fun resample(pts: List<Pt>, stepM: Double): List<Pt> {
    if (pts.size < 2) return pts
    val out = ArrayList<Pt>()
    out += pts[0]
    var carry = 0.0
    for (i in 0 until pts.size - 1) {
        val a = pts[i]
        val b = pts[i + 1]
        val d = haversineM(a, b)
        if (d <= 0) continue
        var t = stepM - carry
        while (t <= d) {
            val f = t / d
            out += Pt(a.latE7 + ((b.latE7 - a.latE7) * f).roundToInt(), a.lonE7 + ((b.lonE7 - a.lonE7) * f).roundToInt())
            t += stepM
        }
        carry = d - (t - stepM)
    }
    if (out.last() != pts.last()) out += pts.last()
    return out
}

/** Equirectangular metres around the first point; good enough over a ride's extent. */
internal fun local(pts: List<Pt>): List<Pair<Double, Double>> {
    if (pts.isEmpty()) return emptyList()
    val lat0 = pts[0].latE7 / 1e7
    val kx = 111_320.0 * cos(Math.toRadians(lat0)) / 1e7
    val ky = 110_540.0 / 1e7
    return pts.map { (it.lonE7 - pts[0].lonE7) * kx to (it.latE7 - pts[0].latE7) * ky }
}

/** The share of [a] (sampled every 100 m) lying within 60 m of [b]. */
internal fun overlap(a: List<Pt>, b: List<Pt>): Double {
    val sa = resample(a, 100.0)
    val sb = resample(b, 100.0)
    if (sa.isEmpty() || sb.isEmpty()) return 0.0
    val all = local(sa + sb)
    val xa = all.subList(0, sa.size)
    val xb = all.subList(sa.size, all.size)
    var near = 0
    for (p in xa) if (xb.any { q -> val dx = p.first - q.first; val dy = p.second - q.second; dx * dx + dy * dy < 60.0 * 60.0 }) near++
    return near.toDouble() / xa.size
}

/** The result of a plan: the target, the options kept, and how many tries failed. */
data class Plan(val targetM: Double, val options: List<Scored>, val failedTries: Int)

/**
 * Turns "N minutes from here" into a few distinct loops. Asks the engine for [PlanPrefs.tries] seeds,
 * drops loops far from the target length and near-duplicates, and keeps the best [PlanPrefs.options].
 */
class Planner(private val engine: RoutingEngine, private val parallelism: Int = 3) {
    fun targetMetres(minutes: Int, speedKmh: Double) = minutes / 60.0 * speedKmh * 1000.0

    fun plan(start: Pt, minutes: Int, prefs: PlanPrefs = PlanPrefs()): Plan {
        require(minutes in 10..600) { "ride length must be 10 to 600 minutes" }
        val target = targetMetres(minutes, prefs.speedKmh)
        val seeds = (0 until prefs.tries).map { prefs.seedBase + it }
        val results = fetchAll(seeds) { engine.roundTrip(start, target.roundToInt(), it, prefs.profile, prefs.favourPaths) }
        val got = results.filterIsInstance<Result.Ok>().map { it.c }
        if (got.isEmpty()) {
            val errs = results.filterIsInstance<Result.Err>().map { it.e }
            throw errs.firstOrNull { it !is RoutingError.NoRoute } ?: errs.firstOrNull()
                ?: RoutingError.NoRoute("no route could be made from here")
        }
        return Plan(target, choose(got, target, prefs), results.count { it is Result.Err })
    }

    /** Scores, filters and de-duplicates [cands]. Within 15 % of the target if enough are; else the
     * closest are allowed in so the rider still gets options. */
    fun choose(cands: List<Candidate>, targetM: Double, prefs: PlanPrefs): List<Scored> {
        val scored = cands.map { Scorer.score(it, targetM, prefs) }
        val close = scored.filter { abs(it.lengthError) <= 0.15 }
        val pool = (if (close.size >= prefs.options) close else scored.filter { abs(it.lengthError) <= 0.35 }.ifEmpty { scored })
            .sortedByDescending { it.score }
        val kept = ArrayList<Scored>()
        for (s in pool) {
            if (kept.size >= prefs.options) break
            if (kept.none { overlap(s.candidate.points, it.candidate.points) > 0.7 }) kept += s
        }
        return kept
    }

    private sealed class Result {
        class Ok(val c: Candidate) : Result()
        class Err(val e: Exception) : Result()
    }

    private fun fetchAll(seeds: List<Int>, f: (Int) -> Candidate): List<Result> {
        val pool = Executors.newFixedThreadPool(parallelism.coerceIn(1, seeds.size.coerceAtLeast(1)))
        try {
            val futures = seeds.map { s -> pool.submit(Callable { f(s) }) }
            return futures.map { fu ->
                try {
                    Result.Ok(fu.get())
                } catch (e: ExecutionException) {
                    val cause = e.cause
                    // A missing key or used-up quota will fail every try the same way: stop at once.
                    if (cause is RoutingError.NeedsKey || cause is RoutingError.RateLimited) throw cause
                    Result.Err(cause as? Exception ?: e)
                }
            }
        } finally {
            pool.shutdownNow()
        }
    }
}
