package cc700

import cc700.route.Pt
import cc700.route.haversineM
import cc700.route.parseGpx
import cc700.route.plan.BikeProfile
import cc700.route.plan.Candidate
import cc700.route.plan.GpxWriter
import cc700.route.plan.OrsClient
import cc700.route.plan.PlanPrefs
import cc700.route.plan.Planner
import cc700.route.plan.RoutingEngine
import cc700.route.plan.RoutingError
import cc700.route.plan.Scorer
import cc700.route.plan.Span
import cc700.route.plan.WayType
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.JsonArray
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.JsonPrimitive
import kotlinx.serialization.json.int
import okhttp3.OkHttpClient
import okhttp3.mockwebserver.MockResponse
import okhttp3.mockwebserver.MockWebServer
import org.junit.jupiter.api.AfterEach
import org.junit.jupiter.api.Assertions.assertEquals
import org.junit.jupiter.api.Assertions.assertThrows
import org.junit.jupiter.api.Assertions.assertTrue
import org.junit.jupiter.api.BeforeEach
import org.junit.jupiter.api.Test
import java.util.concurrent.atomic.AtomicInteger
import kotlin.math.PI
import kotlin.math.cos
import kotlin.math.roundToInt
import kotlin.math.sin

class RoutePlanTest {
    private lateinit var srv: MockWebServer
    private val http = OkHttpClient()
    /** Not a real key: tests never hold one (START-HERE rule 2). */
    private val fakeKey = "not-a-real-key"
    private val home = Pt(515_074_000, -1_278_000) // central London, west of Greenwich: negative longitude

    @BeforeEach fun up() { srv = MockWebServer(); srv.start() }
    @AfterEach fun down() { srv.shutdown() }

    // ------------------------------------------------------------------ geometry helpers
    /** A circular loop of circumference [lengthM] starting and ending at [start], its centre placed in
     * direction [bearingDeg]. */
    private fun circle(start: Pt, lengthM: Double, bearingDeg: Double, n: Int = 120): List<Pt> {
        val r = lengthM / (2 * PI)
        val kx = 111_320.0 * cos(Math.toRadians(start.latE7 / 1e7))
        val ky = 110_540.0
        val b = Math.toRadians(bearingDeg)
        val cx = r * sin(b)
        val cy = r * cos(b)
        val a0 = Math.atan2(-cy, -cx)
        return (0..n).map { i ->
            val a = a0 + 2 * PI * i / n
            val x = cx + r * cos(a)
            val y = cy + r * sin(a)
            Pt(start.latE7 + (y / ky * 1e7).roundToInt(), start.lonE7 + (x / kx * 1e7).roundToInt(), 20.0)
        }
    }

    private fun length(p: List<Pt>) = (0 until p.size - 1).sumOf { haversineM(p[it], p[it + 1]) }

    private fun cand(id: String, pts: List<Pt>, way: Int = WayType.STREET) =
        Candidate(id, pts, length(pts), 100.0, 3600.0, wayTypes = listOf(Span(0, pts.size - 1, way)))

    private fun geojson(pts: List<Pt>, waytype: Int = WayType.CYCLEWAY): String {
        val coords = pts.joinToString(",") { "[${it.lonE7 / 1e7},${it.latE7 / 1e7},${it.ele ?: 0.0}]" }
        val last = pts.size - 1
        return """{"type":"FeatureCollection","features":[{"type":"Feature",
            "geometry":{"type":"LineString","coordinates":[$coords]},
            "properties":{"ascent":123.4,"descent":123.4,"summary":{"distance":${length(pts)},"duration":3000.0},
              "extras":{"waytypes":{"values":[[0,$last,$waytype]]},"surface":{"values":[[0,$last,3]]},
                        "steepness":{"values":[[0,$last,0]]}}}}]}"""
    }

    // ------------------------------------------------------------------ GPX
    @Test fun gpxRoundTrip() {
        val pts = listOf(Pt(515_074_123, -1_278_456, 12.5), Pt(-338_688_000, 1_512_093_000, null), Pt(1, -1, -3.0))
        val (name, back) = parseGpx(GpxWriter.write("Loop <A & B>", pts).byteInputStream())
        assertEquals("Loop <A & B>", name)
        assertEquals(pts, back)
    }

    // ------------------------------------------------------------------ OpenRouteService
    @Test fun orsRoundTripRequestAndParse() {
        val loop = circle(home, 20_000.0, 90.0)
        srv.enqueue(MockResponse().setBody(geojson(loop)))
        val c = OrsClient(http, { fakeKey }, srv.url("/")).roundTrip(home, 20_000, 7, BikeProfile.ROAD, favourPaths = true)

        val req = srv.takeRequest()
        assertEquals("POST", req.method)
        assertEquals("/v2/directions/cycling-regular/geojson", req.path)
        assertEquals(fakeKey, req.getHeader("Authorization"))
        val body = Json.parseToJsonElement(req.body.readUtf8()) as JsonObject
        val coord = ((body["coordinates"] as JsonArray)[0] as JsonArray).map { (it as JsonPrimitive).content.toDouble() }
        assertEquals(listOf(-0.1278, 51.5074), coord, "ORS takes [lon, lat]")
        val rt = (body["options"] as JsonObject)["round_trip"] as JsonObject
        assertEquals(20_000, (rt["length"] as JsonPrimitive).int)
        assertEquals(7, (rt["seed"] as JsonPrimitive).int)
        assertEquals("true", (body["elevation"] as JsonPrimitive).content)

        assertEquals("s7", c.id)
        assertEquals(loop.size, c.points.size)
        assertEquals(loop[5].latE7, c.points[5].latE7)
        assertEquals(loop[5].lonE7, c.points[5].lonE7)
        assertEquals(123.4, c.ascentM)
        assertEquals(listOf(Span(0, loop.size - 1, WayType.CYCLEWAY)), c.wayTypes)
    }

    @Test fun orsProfilesFollowBikeAndPreference() {
        val ors = OrsClient(http, { fakeKey }, srv.url("/"))
        for ((args, want) in listOf(
            (BikeProfile.ROAD to false) to "cycling-road",
            (BikeProfile.GRAVEL to false) to "cycling-regular",
            (BikeProfile.MTB to true) to "cycling-mountain",
        )) {
            srv.enqueue(MockResponse().setBody(geojson(circle(home, 5000.0, 0.0))))
            ors.roundTrip(home, 5000, 1, args.first, args.second)
            assertEquals("/v2/directions/$want/geojson", srv.takeRequest().path)
        }
    }

    @Test fun orsErrors() {
        val ors = OrsClient(http, { fakeKey }, srv.url("/"))
        srv.enqueue(MockResponse().setResponseCode(403).setBody("""{"error":"Access to this API has been disallowed"}"""))
        assertThrows(RoutingError.NeedsKey::class.java) { ors.roundTrip(home, 5000, 1, BikeProfile.ROAD, true) }
        srv.enqueue(MockResponse().setResponseCode(429).setBody("""{"error":"Rate limit exceeded"}"""))
        assertThrows(RoutingError.RateLimited::class.java) { ors.roundTrip(home, 5000, 1, BikeProfile.ROAD, true) }
        srv.enqueue(MockResponse().setResponseCode(404).setBody("""{"error":{"code":2009,"message":"Route could not be found"}}"""))
        val e = assertThrows(RoutingError.NoRoute::class.java) { ors.roundTrip(home, 5000, 1, BikeProfile.ROAD, true) }
        assertEquals("Route could not be found", e.message)
    }

    @Test fun orsWithoutKeySendsNothing() {
        val ors = OrsClient(http, { null }, srv.url("/"))
        assertThrows(RoutingError.NeedsKey::class.java) { ors.roundTrip(home, 5000, 1, BikeProfile.ROAD, true) }
        assertEquals(0, srv.requestCount)
    }

    // ------------------------------------------------------------------ scoring
    @Test fun cyclewaysBeatStateRoads() {
        val pts = circle(home, 20_000.0, 0.0)
        val prefs = PlanPrefs()
        val path = Scorer.score(cand("a", pts, WayType.CYCLEWAY), 20_000.0, prefs)
        val road = Scorer.score(cand("b", pts, WayType.STATE_ROAD), 20_000.0, prefs)
        assertEquals(1.0, path.breakdown.cyclePath, 1e-6)
        assertEquals(1.0, road.breakdown.busyRoad, 1e-6)
        assertTrue(path.score > road.score)
    }

    @Test fun outAndBackIsBacktracking() {
        val out = (0..100).map { Pt(home.latE7 + it * 9000, home.lonE7) } // ~10 km north
        val outAndBack = out + out.reversed().drop(1)
        assertTrue(Scorer.backtrackShare(outAndBack) > 0.8)
        assertTrue(Scorer.backtrackShare(circle(home, 20_000.0, 45.0)) < 0.05)
    }

    // ------------------------------------------------------------------ planner
    /** Seed n gives a circle in direction 60n degrees; seeds 1 and 2 return the same loop, and seed 3
     * is far too long. */
    private inner class FakeEngine : RoutingEngine {
        val calls = AtomicInteger()
        override fun roundTrip(start: Pt, lengthM: Int, seed: Int, profile: BikeProfile, favourPaths: Boolean): Candidate {
            calls.incrementAndGet()
            val len = if (seed == 3) lengthM * 2.0 else lengthM.toDouble()
            val dir = if (seed == 2) 60.0 else seed * 60.0
            return cand("s$seed", circle(start, len, dir), if (seed == 5) WayType.CYCLEWAY else WayType.STREET)
        }
        override fun route(waypoints: List<Pt>, profile: BikeProfile, favourPaths: Boolean) = throw UnsupportedOperationException()
    }

    @Test fun plannerKeepsDistinctLoopsNearTheTarget() {
        val eng = FakeEngine()
        val plan = Planner(eng).plan(home, 60, PlanPrefs(speedKmh = 20.0))
        assertEquals(20_000.0, plan.targetM, 1e-6)
        assertEquals(6, eng.calls.get())
        val ids = plan.options.map { it.candidate.id }
        assertEquals(3, ids.size)
        assertEquals("s5", ids[0], "the all-cycleway loop ranks first")
        assertTrue("s3" !in ids, "the loop twice the target length is dropped")
        assertTrue(!("s1" in ids && "s2" in ids), "identical loops are not both offered")
        for (o in plan.options) assertTrue(kotlin.math.abs(o.lengthError) <= 0.15)
    }

    @Test fun plannerStopsOnMissingKey() {
        val eng = object : RoutingEngine {
            override fun roundTrip(start: Pt, lengthM: Int, seed: Int, profile: BikeProfile, favourPaths: Boolean): Candidate =
                throw RoutingError.NeedsKey()
            override fun route(waypoints: List<Pt>, profile: BikeProfile, favourPaths: Boolean) = throw UnsupportedOperationException()
        }
        assertThrows(RoutingError.NeedsKey::class.java) { Planner(eng).plan(home, 60) }
    }

    @Test fun plannerSurvivesSomeFailedTries() {
        val ok = FakeEngine()
        val eng = object : RoutingEngine by ok {
            override fun roundTrip(start: Pt, lengthM: Int, seed: Int, profile: BikeProfile, favourPaths: Boolean): Candidate =
                if (seed % 2 == 0) throw RoutingError.NoRoute("no") else ok.roundTrip(start, lengthM, seed, profile, favourPaths)
        }
        val plan = Planner(eng).plan(home, 60, PlanPrefs(speedKmh = 20.0))
        assertEquals(3, plan.failedTries)
        assertTrue(plan.options.isNotEmpty())
    }
}
