package cc700

import cc700.route.Pt
import cc700.route.TurnType
import cc700.route.buildRoute
import cc700.route.cr1HasProfile
import cc700.route.cumulative
import cc700.route.generateTurns
import cc700.route.parseGpx
import cc700.route.profileStep
import cc700.route.simplify
import cc700.route.smoothProfile
import org.junit.jupiter.api.Assertions.assertEquals
import org.junit.jupiter.api.Assertions.assertFalse
import org.junit.jupiter.api.Assertions.assertTrue
import org.junit.jupiter.api.Test
import java.io.File
import kotlin.math.cos
import kotlin.math.roundToInt

class RouteTest {
    private val root = File(System.getProperty("cc700.root"))
    private val lat0 = -319640000; private val lon0 = 1158300000
    private val kx = 1e-7 * 111320.0 * cos(Math.toRadians(-31.964))
    private fun at(x: Double, y: Double, e: Double? = null) = Pt(lat0 + (y / (1e-7 * 111320.0)).roundToInt(), lon0 + (x / kx).roundToInt(), e)

    @Test fun gpxFixtureBuildsTheExpectedRoute() {
        val (name, raw) = parseGpx(File(root, "core/tests/fixtures/route.gpx").inputStream())
        assertEquals("Kings Park test", name)
        assertEquals(49, raw.size)
        // Elevation from a known function: flat 20 m, 5 % from 1000 m to 1600 m, then flat (gen_ride)
        val cum0 = cumulative(raw)
        val built = buildRoute(name, raw, { samples ->
            val c = cumulative(samples)
            c.map { d -> 20.0 + 0.05 * (d - 1000).coerceIn(0.0, 600.0) }
        })
        assertTrue(built.points.size <= raw.size)
        assertEquals(25, built.step)
        assertEquals(1, built.climbs.size, "${built.climbs}")
        val c = built.climbs[0]
        // The 5-sample mean (brief 7.4) at 25 m spacing spreads each end of the climb by up to 50 m, and the
        // climb starts at the last sample of the flat base, so it reads about 950 m to 1675 m for 30 m.
        assertTrue(c.startM in 900..1050 && c.lengthM in 550..750 && c.ascentM in 28..30, "$c")
        val kinds = built.turns.map { it.type }
        assertEquals(listOf(TurnType.DEPART, TurnType.LEFT, TurnType.RIGHT, TurnType.ARRIVE), kinds)
        assertTrue(kotlin.math.abs(built.turns[1].distM - 1000) <= 5 && kotlin.math.abs(built.turns[2].distM - 1600) <= 5, "${built.turns}")
        assertTrue(cum0.last() > 2390)
        assertEquals(false, built.fromFile)
    }

    @Test fun gpxElevationsUsedWhenTheDemHasNothing() {
        val raw = (0..40).map { at(it * 25.0, 0.0, 20.0 + if (it in 10..30) (it - 10) * 1.5 else if (it > 30) 30.0 else 0.0) }
        val b = buildRoute("Hill", raw, { s -> s.map { null } })
        assertTrue(b.fromFile)
        assertEquals(1, b.climbs.size)
    }


    /** A route with no elevation anywhere — no service answered and the GPX carried none — must say so
     * rather than present an ascent of 0 and an empty climb list as a flat route. It must also not claim
     * the elevation came "from file" when the file had none: that was the old behaviour, and it made the
     * import preview state a provenance and a figure that were both untrue. The same shape of fault as
     * Q19 on the unit. */
    @Test fun noElevationAnywhereIsReportedAsUnknownNotFlat() {
        val raw = (0..40).map { at(it * 25.0, 0.0) }       // no ele in the file
        val b = buildRoute("No elevation", raw, { s -> s.map { null } })  // and no service
        assertFalse(b.hasElevation)
        assertFalse(b.fromFile, "the file had no elevation, so nothing came from it")
        assertEquals(0, b.profileM.size)
        assertEquals(0, b.climbs.size)
        assertEquals(0, b.obj.ascentM)                      // 0 because unknown, which is why hasElevation exists
        assertFalse(cr1HasProfile(b.obj.bytes), "nprof must be 0 so a stored route reads as unknown too")

        // A genuinely flat route, with elevation known to be constant, is the case that must stay
        // distinguishable: same ascent of 0, but a profile and so a real claim.
        val flat = buildRoute("Flat", raw, { s -> s.map { 12.0 } })
        assertTrue(flat.hasElevation)
        assertEquals(0, flat.obj.ascentM)
        assertTrue(cr1HasProfile(flat.obj.bytes))
    }

    @Test fun cornersCountOnce() {
        // A 90 degree left corner drawn with three points 10 m apart: one LEFT, not a U-turn
        val pts = listOf(at(0.0, 0.0), at(200.0, 0.0), at(207.0, 3.0), at(210.0, 10.0), at(210.0, 210.0))
            .map { Pt(it.latE7, it.lonE7) }
        // north after going east is a left turn
        val t = generateTurns(pts)
        assertEquals(listOf(TurnType.DEPART, TurnType.LEFT, TurnType.ARRIVE), t.map { it.type })
        // a gentle 20 degree bend is no turn; a hairpin is a U-turn
        val bend = listOf(at(0.0, 0.0), at(200.0, 0.0), at(400.0, 73.0))
        assertEquals(listOf(TurnType.DEPART, TurnType.ARRIVE), generateTurns(bend).map { it.type })
        val hair = listOf(at(0.0, 0.0), at(200.0, 0.0), at(205.0, 15.0), at(0.0, 20.0))
        assertEquals(TurnType.UTURN, generateTurns(hair)[1].type)
        // right turn
        val right = listOf(at(0.0, 0.0), at(200.0, 0.0), at(200.0, -200.0))
        assertEquals(TurnType.RIGHT, generateTurns(right)[1].type)
    }

    @Test fun simplificationAndLimits() {
        // 20,000 points on a wiggly line come down to at most 8000, and close points are dropped
        val raw = (0 until 20000).map { at(it * 2.0, 3.0 * kotlin.math.sin(it / 7.0)) }
        val s = simplify(raw)
        assertTrue(s.size <= 8000)
        assertEquals(raw.first(), s.first()); assertEquals(raw.last(), s.last())
        assertEquals(25, profileStep(2000.0)); assertEquals(25, profileStep(300000.0)); assertEquals(34, profileStep(400000.0))
        val sm = smoothProfile(listOf(null, 10.0, 10.0, 50.0, 10.0, 10.0, null))
        assertTrue(sm.all { it < 20 }) // a one-sample spike is removed by the median
    }
}
