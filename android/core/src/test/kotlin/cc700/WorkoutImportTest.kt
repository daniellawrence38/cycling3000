package cc700

import cc700.workout.WorkoutError
import cc700.workout.encodeCw1
import cc700.workout.normalise
import cc700.workout.parseWorkout
import org.junit.jupiter.api.Assertions.assertArrayEquals
import org.junit.jupiter.api.Assertions.assertEquals
import org.junit.jupiter.api.Test
import org.junit.jupiter.api.assertThrows
import java.io.File

class WorkoutImportTest {
    private val root = File(System.getProperty("cc700.root"))

    /** The six starter workouts give the same CW1 bytes as ref.py (core/tests/fixtures, made by
     * tools/reference/make_fixtures.py). */
    @Test fun starterWorkoutsMatchReference() {
        val files = File(root, "claude/workouts").listFiles { f -> f.name.endsWith(".workout.json") }!!.sorted()
        assertEquals(6, files.size)
        for (f in files) {
            val obj = encodeCw1(parseWorkout(f.readText()))
            val ref = File(root, "core/tests/fixtures/" + f.name.removeSuffix(".workout.json") + ".cw1").readBytes()
            assertArrayEquals(ref, obj, f.name)
        }
    }

    @Test fun typographyIsNormalisedAndNothingElse() {
        assertEquals("4 x 8 - 'x' \"y\" a b", normalise("4 × 8 – ‘x’ “y” a b"))
        val w = parseWorkout("{\"v\":1,\"name\":\"3 × 10 — tempo\",\"steps\":[{\"label\":\"Tempo\",\"secs\":600,\"ftp_pct\":[76,90]}]}")
        assertEquals("3 x 10 - tempo", w.name)
        // An accented name is not "corrected": it is refused
        val e = assertThrows<WorkoutError> { parseWorkout("""{"v":1,"name":"Café","steps":[{"label":"a","secs":60,"ftp_pct":50}]}""") }
        assertEquals("E_NAME", e.code)
    }

    @Test fun readableErrors() {
        val e = assertThrows<WorkoutError> {
            parseWorkout("""{"v":1,"name":"t","steps":[{"label":"a","secs":60,"ftp_pct":50},{"label":"a","secs":60,"ftp_pct":50},{"label":"Seventeen chars!!","secs":60,"ftp_pct":50}]}""")
        }
        assertEquals("Step 3: label must be 1 to 16 letters, digits or spaces", e.readable)
        val g = assertThrows<WorkoutError> { parseWorkout("""{"v":1,"name":"t","steps":[{"reps":2,"steps":[{"label":"a","secs":4,"ftp_pct":50}]}]}""") }
        assertEquals("Step 1, item 1: secs must be a whole number from 5 to 43200", g.readable)
        assertEquals("E_JSON", assertThrows<WorkoutError> { parseWorkout("not json") }.code)
    }
}
