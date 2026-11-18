package cc700

import cc700.route.Pt
import cc700.route.assist.AssistError
import cc700.route.assist.AssistPlanner
import cc700.route.haversineM
import cc700.route.plan.BikeProfile
import cc700.route.plan.Candidate
import cc700.route.plan.Place
import cc700.route.plan.PlaceSearch
import cc700.route.plan.PlanPrefs
import cc700.route.plan.RoutingEngine
import cc700.route.plan.Span
import cc700.route.plan.WayType
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.JsonArray
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.JsonPrimitive
import okhttp3.OkHttpClient
import okhttp3.mockwebserver.MockResponse
import okhttp3.mockwebserver.MockWebServer
import okhttp3.mockwebserver.RecordedRequest
import org.junit.jupiter.api.AfterEach
import org.junit.jupiter.api.Assertions.assertEquals
import org.junit.jupiter.api.Assertions.assertFalse
import org.junit.jupiter.api.Assertions.assertThrows
import org.junit.jupiter.api.Assertions.assertTrue
import org.junit.jupiter.api.BeforeEach
import org.junit.jupiter.api.Test
import kotlin.math.PI
import kotlin.math.cos
import kotlin.math.roundToInt
import kotlin.math.sin

/** The assistant planner's tool loop against a stubbed OpenRouter endpoint. No real key, no network. */
class AssistPlannerTest {
    private lateinit var srv: MockWebServer
    private val http = OkHttpClient()
    /** Not a real key: tests never hold one (START-HERE rule 2). */
    private val fakeKey = "not-a-real-key"
    private val home = Pt(515_074_000, -1_278_000)

    @BeforeEach fun up() { srv = MockWebServer(); srv.start() }
    @AfterEach fun down() { srv.shutdown() }

    private fun planner(eng: Engine, places: PlaceSearch? = eng, max: Int = 20) =
        AssistPlanner(http, { fakeKey }, eng, places, base = srv.url("/api/v1/"), maxRoutingCalls = max)

    private fun circle(lengthM: Double, bearingDeg: Double): List<Pt> {
        val r = lengthM / (2 * PI)
        val kx = 111_320.0 * cos(Math.toRadians(home.latE7 / 1e7)); val ky = 110_540.0
        val b = Math.toRadians(bearingDeg)
        val cx = r * sin(b); val cy = r * cos(b)
        val a0 = Math.atan2(-cy, -cx)
        return (0..120).map { i ->
            val a = a0 + 2 * PI * i / 120
            Pt(home.latE7 + ((cy + r * sin(a)) / ky * 1e7).roundToInt(), home.lonE7 + ((cx + r * cos(a)) / kx * 1e7).roundToInt())
        }
    }

    inner class Engine : RoutingEngine, PlaceSearch {
        var calls = 0
        override fun roundTrip(start: Pt, lengthM: Int, seed: Int, profile: BikeProfile, favourPaths: Boolean): Candidate {
            calls++
            val pts = circle(lengthM.toDouble(), seed * 70.0)
            val d = (0 until pts.size - 1).sumOf { haversineM(pts[it], pts[it + 1]) }
            return Candidate("s$seed", pts, d, 150.0, 0.0, wayTypes = listOf(Span(0, pts.size - 1, WayType.CYCLEWAY)))
        }
        override fun route(waypoints: List<Pt>, profile: BikeProfile, favourPaths: Boolean): Candidate {
            calls++
            return Candidate("r", waypoints, (0 until waypoints.size - 1).sumOf { haversineM(waypoints[it], waypoints[it + 1]) }, 50.0, 0.0)
        }
        override fun searchPlaces(query: String, near: Pt, radiusKm: Double, max: Int) =
            listOf(Place("Riverside Café, Kew", Pt(near.latE7 + 50_000, near.lonE7 - 80_000), 6.2))
    }

    /** A chat completion whose message calls one tool; [args] is the JSON the model wrote. */
    private fun call(id: String, name: String, args: String): MockResponse {
        val quoted = JsonPrimitive(args).toString()
        return MockResponse().setHeader("Content-Type", "application/json").setBody(
            """{"id":"gen-1","model":"deepseek/deepseek-v4.1-flash","choices":[{"index":0,"finish_reason":"tool_calls",
               "message":{"role":"assistant","content":null,"tool_calls":[{"id":"$id","type":"function",
               "function":{"name":"$name","arguments":$quoted}}]}}]}""")
    }

    private fun text(finish: String, content: String) = MockResponse().setHeader("Content-Type", "application/json").setBody(
        """{"id":"gen-2","choices":[{"index":0,"finish_reason":"$finish","message":{"role":"assistant","content":"$content"}}]}""")

    private fun body(r: RecordedRequest) = Json.parseToJsonElement(r.body.readUtf8()) as JsonObject
    private fun last(b: JsonObject) = (b["messages"] as JsonArray).last() as JsonObject
    private fun s(o: JsonObject, k: String) = (o[k] as JsonPrimitive).content

    @Test fun loopsThenPresent() {
        val eng = Engine()
        srv.enqueue(call("t1", "generate_loops", """{"length_km":22,"count":2,"profile":"road","favour_paths":true}"""))
        srv.enqueue(call("t2", "present_options",
            """{"options":[{"id":"c2","reason":"Mostly cycleway along the river."},{"id":"c1","reason":"A little hillier."}]}"""))
        val plan = planner(eng).plan(home, 60, "an hour, mostly cycle paths", PlanPrefs(speedKmh = 22.0))

        assertEquals(listOf("c2", "c1"), plan.options.map { it.scored.candidate.id })
        assertEquals("Mostly cycleway along the river.", plan.options[0].reason)
        assertEquals(2, eng.calls)

        val first = srv.takeRequest()
        assertEquals("/api/v1/chat/completions", first.path)
        assertEquals("Bearer $fakeKey", first.getHeader("Authorization"))
        val b1 = body(first)
        assertEquals("deepseek/deepseek-v4.1-flash", s(b1, "model"))
        assertEquals("auto", s(b1, "tool_choice"))
        assertEquals(listOf("generate_loops", "find_places", "generate_route", "present_options"),
            (b1["tools"] as JsonArray).map { s(((it as JsonObject)["function"] as JsonObject), "name") })

        val b2 = body(srv.takeRequest())
        val msgs = b2["messages"] as JsonArray
        val assistant = msgs[msgs.size - 2] as JsonObject
        assertEquals("assistant", s(assistant, "role"))
        assertEquals("t1", s(((assistant["tool_calls"] as JsonArray)[0] as JsonObject), "id"))
        val result = last(b2)
        assertEquals("tool", s(result, "role")); assertEquals("t1", s(result, "tool_call_id"))
        assertTrue(s(result, "content").contains("\"id\":\"c1\"") && s(result, "content").contains("\"cycle_path_pct\":100"))

        // Neither the start nor any route geometry reaches OpenRouter.
        for (b in listOf(b1, b2)) {
            val t = b.toString()
            assertFalse(t.contains("51.507") || t.contains("-0.127") || t.contains("515074"), "coordinates sent to the model")
        }
    }

    @Test fun placesRouteAndUnknownIds() {
        val eng = Engine()
        srv.enqueue(call("t1", "find_places", """{"query":"cafe","radius_km":8}"""))
        srv.enqueue(call("t2", "generate_route", """{"place_ids":["p1"],"profile":"road","favour_paths":true}"""))
        srv.enqueue(call("t3", "present_options", """{"options":[{"id":"c99","reason":"made up"}]}"""))
        srv.enqueue(call("t4", "present_options", """{"options":[{"id":"c1","reason":"Coffee at Kew."}]}"""))
        val plan = planner(eng).plan(home, 60, "café stop", PlanPrefs())
        assertEquals("c1", plan.options.single().scored.candidate.id)
        srv.takeRequest()
        assertTrue(s(last(body(srv.takeRequest())), "content").contains("\"place_id\":\"p1\""))
        assertTrue(s(last(body(srv.takeRequest())), "content").contains("\"via\":[\"Riverside Café, Kew\"]"))
        assertTrue(s(last(body(srv.takeRequest())), "content").startsWith("Error: no valid ids"))
    }

    @Test fun badArgumentsAreReportedBack() {
        val eng = Engine()
        srv.enqueue(call("t1", "generate_loops", "{not json"))
        srv.enqueue(call("t2", "generate_loops", """{"length_km":22,"count":1,"profile":"road","favour_paths":true}"""))
        srv.enqueue(call("t3", "present_options", """{"options":[{"id":"c1","reason":"ok"}]}"""))
        planner(eng).plan(home, 60, "x", PlanPrefs())
        srv.takeRequest()
        assertTrue(s(last(body(srv.takeRequest())), "content").contains("not valid JSON"))
    }

    @Test fun endingWithoutPresentingFallsBackToBestScores() {
        val eng = Engine()
        srv.enqueue(call("t1", "generate_loops", """{"length_km":22,"count":3,"profile":"road","favour_paths":true}"""))
        srv.enqueue(text("stop", "Here you go."))
        val plan = planner(eng).plan(home, 60, "x", PlanPrefs(speedKmh = 22.0))
        assertEquals(3, plan.options.size)
        assertTrue(plan.options.all { it.reason.isEmpty() })
    }

    @Test fun routingBudgetIsEnforced() {
        val eng = Engine()
        srv.enqueue(call("t1", "generate_loops", """{"length_km":22,"count":4,"profile":"road","favour_paths":true}"""))
        srv.enqueue(call("t2", "generate_loops", """{"length_km":22,"count":4,"profile":"road","favour_paths":true}"""))
        srv.enqueue(call("t3", "present_options", """{"options":[{"id":"c1","reason":"ok"}]}"""))
        planner(eng, max = 5).plan(home, 60, "x", PlanPrefs())
        assertEquals(5, eng.calls)
    }

    @Test fun serviceErrors() {
        srv.enqueue(MockResponse().setResponseCode(401).setBody("""{"error":{"code":401,"message":"No auth credentials found"}}"""))
        assertThrows(AssistError.NeedsKey::class.java) { planner(Engine()).plan(home, 60, "x", PlanPrefs()) }
        srv.enqueue(MockResponse().setResponseCode(402).setBody("""{"error":{"code":402,"message":"Insufficient credits"}}"""))
        assertThrows(AssistError.NoCredit::class.java) { planner(Engine()).plan(home, 60, "x", PlanPrefs()) }
        // An error carried in a 200 response.
        srv.enqueue(MockResponse().setBody("""{"error":{"code":502,"message":"Provider returned error"}}"""))
        val e = assertThrows(AssistError.Failed::class.java) { planner(Engine()).plan(home, 60, "x", PlanPrefs()) }
        assertEquals(502, e.code)
        srv.enqueue(text("content_filter", ""))
        assertThrows(AssistError.Declined::class.java) { planner(Engine()).plan(home, 60, "x", PlanPrefs()) }
    }

    @Test fun noKeySendsNothing() {
        val p = AssistPlanner(http, { null }, Engine(), null, base = srv.url("/api/v1/"))
        assertThrows(AssistError.NeedsKey::class.java) { p.plan(home, 60, "x", PlanPrefs()) }
        assertEquals(0, srv.requestCount)
    }
}
