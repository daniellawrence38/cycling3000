package cc700.sync

import cc700.workout.describe
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.jsonArray
import kotlinx.serialization.json.jsonObject
import kotlinx.serialization.json.jsonPrimitive
import org.junit.jupiter.api.Assertions.assertEquals
import org.junit.jupiter.api.Assertions.assertTrue
import org.junit.jupiter.api.Test
import java.io.File

/** The import path the Workouts screen uses: the same V§4 rows as the instrumented ImportErrorsTest. */
class LibraryTest {
    private val vectors = Json.parseToJsonElement(File(Sim.root, "android/core/src/test/resources/vectors.json").readText()).jsonObject

    @Test fun everyInvalidRowGivesItsReadableMessage() {
        val rows = vectors["invalid"]!!.jsonArray
        assertTrue(rows.size >= 30)
        for (r in rows) {
            val o = r.jsonObject
            val res = importWorkout(o["doc"]!!.toString())
            val code = o["code"]!!.jsonPrimitive.content
            val path = o["path"]!!.jsonPrimitive.content
            assertTrue(res is WorkoutImport.Invalid, o["name"].toString())
            res as WorkoutImport.Invalid
            assertEquals(code, res.code, o["name"].toString())
            assertEquals(describe(code, path), res.message)
        }
    }

    @Test fun messagesNameTheStep() {
        val r = importWorkout("""{"v":1,"name":"t","steps":[{"label":"A","secs":60,"ftp_pct":50},{"label":"B","secs":60,"ftp_pct":50},{"label":"Way too long label","secs":60,"ftp_pct":50}]}""")
        assertEquals("Step 3: label must be 1 to 16 letters, digits or spaces", (r as WorkoutImport.Invalid).message)
    }

    @Test fun curlyQuotesAreNormalised() {
        val r = importWorkout("{“v”: 1, “name”: “Sweet spot”, “steps”: [{“label”: “SS”, “secs”: 600, “ftp_pct”: 90}]}")
        assertTrue(r is WorkoutImport.Ok)
        assertEquals("Sweet spot", (r as WorkoutImport.Ok).workout.name)
    }

    @Test fun identicalWorkoutsHaveTheSameId() {
        val a = importWorkout(javaClass.getResource("/w1.json")!!.readText()) as WorkoutImport.Ok
        val b = importWorkout(javaClass.getResource("/w1.json")!!.readText().replace("\n", " ")) as WorkoutImport.Ok
        assertEquals(a.idHex, b.idHex)
        assertEquals(16, a.idHex.length)
    }

    @Test fun badGpxIsReadable() {
        val r = importRoute("<gpx></gpx>".byteInputStream(), "x", { pts -> List(pts.size) { null } }, ClimbThresholds().params())
        assertTrue(r is RouteImport.Invalid)
    }
}
