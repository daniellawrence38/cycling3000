package cc700.sync

import cc700.route.Pt
import cc700.route.decodeCr1
import cc700.route.plan.GpxWriter
import org.junit.jupiter.api.Assertions.assertEquals
import org.junit.jupiter.api.Assertions.assertTrue
import org.junit.jupiter.api.Test
import kotlin.math.PI
import kotlin.math.cos
import kotlin.math.roundToInt
import kotlin.math.sin

/** A planned loop takes the same path as an imported GPX file: GpxWriter, importRoute, a CR1 object the
 * unit's decoder accepts, with the elevation the routing service supplied used as file elevation. */
class PlannedRouteTest {
    @Test fun plannedLoopBecomesAValidRoute() {
        val lat0 = 515_074_000; val lon0 = -1_278_000
        val r = 20_000.0 / (2 * PI)
        val pts = (0..240).map { i ->
            val a = 2 * PI * i / 240
            val x = r * sin(a); val y = r * (1 - cos(a))
            Pt(lat0 + (y / 110_540.0 * 1e7).roundToInt(),
               lon0 + (x / (111_320.0 * cos(Math.toRadians(51.5074))) * 1e7).roundToInt(),
               20.0 + 40.0 * sin(a))
        }
        val gpx = GpxWriter.write("Loop 1, 20 km (55 min)", pts)
        val noService: (List<Pt>) -> List<Double?> = { p -> List(p.size) { null } }
        val imp = importRoute(gpx.byteInputStream(), "fallback", noService, ClimbThresholds().params())
        assertTrue(imp is RouteImport.Ok, (imp as? RouteImport.Invalid)?.message ?: "")
        val ok = imp as RouteImport.Ok
        assertEquals("Loop 1, 20 km (55 min)", ok.name)
        assertTrue(ok.route.fromFile, "elevation from the routing service is used as file elevation")
        val info = decodeCr1(ok.route.obj.bytes)
        assertEquals("Loop 1, 20 km (55 min)", info.name)
        assertEquals(20_000.0, info.distM.toDouble(), 200.0)
        assertTrue(info.ascentM in 30..90, "ascent ${info.ascentM}")
    }
}
