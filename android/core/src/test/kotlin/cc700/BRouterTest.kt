package cc700

import cc700.route.Pt
import cc700.route.plan.BRouterClient
import cc700.route.plan.BikeProfile
import cc700.route.plan.PlanPrefs
import cc700.route.plan.RoutingError
import cc700.route.plan.Scorer
import cc700.route.plan.Span
import cc700.route.plan.WayType
import okhttp3.OkHttpClient
import okhttp3.mockwebserver.MockResponse
import okhttp3.mockwebserver.MockWebServer
import org.junit.jupiter.api.AfterEach
import org.junit.jupiter.api.Assertions.assertEquals
import org.junit.jupiter.api.Assertions.assertThrows
import org.junit.jupiter.api.Assertions.assertTrue
import org.junit.jupiter.api.BeforeEach
import org.junit.jupiter.api.Test

/** BRouter against a stub serving its GeoJSON shape (a real response, trimmed to five points). */
class BRouterTest {
    private lateinit var srv: MockWebServer
    private val http = OkHttpClient()
    private val home = Pt(514_600_000, -3_000_000)

    @BeforeEach fun up() { srv = MockWebServer(); srv.start() }
    @AfterEach fun down() { srv.shutdown() }

    private fun client() = BRouterClient(http, srv.url("/brouter"))

    /** Points 0..4 about 100 m apart going north. Stretch 0-2 is a cycleway, 2-3 a primary road, 3-4 a
     * paved footway. Message rows give each stretch's end point in 1e-6 degrees, as BRouter does. */
    private val body = """{"type":"FeatureCollection","features":[{"type":"Feature",
      "properties":{"creator":"BRouter-1.7.11-beta","track-length":"400","filtered ascend":"12","total-time":"80",
        "messages":[["Longitude","Latitude","Elevation","Distance","CostPerKm","ElevCost","TurnCost","NodeCost","InitialCost","WayTags","NodeTags","Time","Energy"],
          ["-300000","51460900","10","100","1000","0","0","0","0","highway=cycleway surface=asphalt","","20","100"],
          ["-300000","51461800","11","100","1000","0","0","0","0","highway=cycleway surface=asphalt","","40","200"],
          ["-300000","51462700","12","100","3000","0","0","0","0","highway=primary surface=asphalt oneway=yes","","60","300"],
          ["-300000","51463600","12","100","1200","0","0","0","0","highway=footway surface=paving_stones bicycle=yes","","80","400"]]},
      "geometry":{"type":"LineString","coordinates":[[-0.3,51.46,10.0],[-0.3,51.4609,10.5],[-0.3,51.4618,11.0],[-0.3,51.4627,12.0],[-0.3,51.4636,12.0]]}}]}"""

    @Test fun roundTripRequestAndTags() {
        srv.enqueue(MockResponse().setBody(body))
        val c = client().roundTrip(home, 20_000, 2, BikeProfile.ROAD, favourPaths = true)
        val q = srv.takeRequest().requestUrl!!
        assertEquals("-0.3,51.46", q.queryParameter("lonlats"))
        assertEquals("safety", q.queryParameter("profile"))
        assertEquals("4", q.queryParameter("engineMode"))
        assertEquals("3333", q.queryParameter("roundTripDistance"), "radius = length / 6")
        assertEquals("75", q.queryParameter("direction"), "seed 2 heads out at 75 degrees")

        assertEquals(5, c.points.size)
        assertEquals(400.0, c.distanceM); assertEquals(12.0, c.ascentM)
        assertEquals(listOf(Span(0, 1, WayType.CYCLEWAY), Span(1, 2, WayType.CYCLEWAY), Span(2, 3, WayType.STATE_ROAD),
            Span(3, 4, WayType.FOOTWAY)), c.wayTypes)
        assertEquals(listOf(3, 3, 3, 14), c.surfaces.map { it.value })

        val b = Scorer.breakdown(c, BikeProfile.ROAD)
        assertEquals(0.75, b.cyclePath, 0.01, "cycleway and the shared footway are traffic-free")
        assertEquals(0.25, b.busyRoad, 0.01)
        assertTrue(Scorer.score(c, 400.0, PlanPrefs()).score > 0)
    }

    @Test fun profilesAndDirections() {
        val br = client()
        srv.enqueue(MockResponse().setBody(body))
        br.roundTrip(home, 20_000, 1, BikeProfile.ROAD, favourPaths = false)
        assertEquals("fastbike", srv.takeRequest().requestUrl!!.queryParameter("profile"))
        srv.enqueue(MockResponse().setBody(body))
        br.roundTrip(home, 20_000, 1, BikeProfile.GRAVEL, favourPaths = true)
        assertEquals("trekking", srv.takeRequest().requestUrl!!.queryParameter("profile"))
        // Six seeds give six directions 60 degrees apart; the next six sit between them.
        val first = (1..6).map { br.direction(it) }
        assertEquals(listOf(15, 75, 135, 195, 255, 315), first)
        assertEquals(listOf(45, 105), (7..8).map { br.direction(it) })
    }

    @Test fun unreachablePointIsNoRoute() {
        srv.enqueue(MockResponse().setResponseCode(400).setBody("target island detected for section 0\n"))
        val e = assertThrows(RoutingError.NoRoute::class.java) { client().roundTrip(home, 30_000, 4, BikeProfile.ROAD, true) }
        assertEquals("target island detected for section 0", e.message)
    }

    @Test fun waypointRoute() {
        srv.enqueue(MockResponse().setBody(body))
        client().route(listOf(home, Pt(514_700_000, -3_100_000), home), BikeProfile.ROAD, true)
        assertEquals("-0.3,51.46|-0.31,51.47|-0.3,51.46", srv.takeRequest().requestUrl!!.queryParameter("lonlats"))
    }
}
