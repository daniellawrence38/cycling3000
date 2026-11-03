package cc700

import cc700.link.Reassembler
import cc700.link.fragment
import cc700.route.Climb
import cc700.route.Pt
import cc700.route.Turn
import cc700.route.detectClimbs
import cc700.route.encodeCr1
import cc700.util.crc32
import cc700.util.fdiv
import cc700.util.hex
import cc700.util.sha256
import cc700.util.unhex
import cc700.util.varint
import cc700.util.zigzag
import cc700.util.ByteBuf
import cc700.workout.WorkoutError
import cc700.workout.band
import cc700.workout.decodeCw1
import cc700.workout.encodeCw1
import cc700.workout.parseWorkout
import cc700.workout.rampP10
import cc700.workout.stepRange
import cc700.workout.targetZone
import cc700.workout.validate
import cc700.workout.watts
import cc700.workout.zoneOfPower
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.JsonArray
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.int
import kotlinx.serialization.json.jsonArray
import kotlinx.serialization.json.jsonObject
import kotlinx.serialization.json.jsonPrimitive
import kotlinx.serialization.json.long
import kotlinx.serialization.json.double
import org.junit.jupiter.api.Assertions.assertArrayEquals
import org.junit.jupiter.api.Assertions.assertEquals
import org.junit.jupiter.api.Assertions.assertTrue
import org.junit.jupiter.api.Test
import org.junit.jupiter.api.assertThrows

/** Every number in claude/cc700-test-vectors.md that the phone computes (WP5), from vectors.json, which
 * tools/reference/export_fixtures.py writes from the same objects as the vectors file. */
class VectorsTest {
    private val v = Json.parseToJsonElement(javaClass.getResource("/vectors.json")!!.readText()).jsonObject

    @Test fun anchors() {
        assertEquals(0xCBF43926L, crc32("123456789".toByteArray()))
        assertEquals("ba7816bf8f01cfea", sha256("abc".toByteArray()).copyOf(8).hex())
    }

    private fun checkWorkout(key: String) {
        val w = v[key]!!.jsonObject
        val wo = parseWorkout(w["doc"].toString())
        val obj = encodeCw1(wo)
        assertEquals(w["hex"]!!.jsonPrimitive.content, obj.hex(), "$key bytes")
        assertEquals(w["id"]!!.jsonPrimitive.content, obj.copyOfRange(4, 12).hex())
        assertEquals(w["crc"]!!.jsonPrimitive.long, crc32(obj))
        assertEquals(wo, decodeCw1(obj))
    }

    @Test fun w1AndW2Bytes() { checkWorkout("w1"); checkWorkout("w2") }

    @Test fun w1Table() {
        val wo = parseWorkout(v["w1"]!!.jsonObject["doc"].toString())
        assertEquals(10, wo.steps.size)
        assertEquals(4080, wo.totalSecs)
        val at260 = listOf(124 to 137, 247 to 260, 117 to 143, 247 to 260, 117 to 143, 247 to 260, 117 to 143, 247 to 260, 117 to 143, 136 to 150)
        val at250 = listOf(119 to 131, 238 to 250, 113 to 138, 238 to 250, 113 to 138, 238 to 250, 113 to 138, 238 to 250, 113 to 138, 131 to 145)
        wo.steps.forEachIndexed { i, s ->
            assertEquals(at260[i], stepRange(260, s, 0), "step ${i + 1} at 260")
            assertEquals(at250[i], stepRange(250, s, 0), "step ${i + 1} at 250")
        }
        assertEquals(4, targetZone(wo.steps[1])); assertEquals(1, targetZone(wo.steps[2]))
        assertEquals(1, targetZone(wo.steps[0], 0)); assertEquals(2, targetZone(wo.steps[0], 600))
    }

    @Test fun widening() {
        val rows = listOf(
            intArrayOf(40, 380, 420, 99, 109, 76, 84, 116, 128), intArrayOf(55, 523, 578, 136, 150, 105, 116, 160, 176),
            intArrayOf(65, 618, 683, 161, 178, 124, 137, 188, 208), intArrayOf(75, 713, 788, 185, 205, 143, 158, 217, 240),
            intArrayOf(88, 836, 924, 217, 240, 167, 185, 255, 282), intArrayOf(100, 950, 1050, 247, 273, 190, 210, 290, 320),
            intArrayOf(105, 998, 1103, 259, 287, 200, 221, 304, 336), intArrayOf(120, 1140, 1260, 296, 328, 228, 252, 348, 384),
            intArrayOf(150, 1425, 1575, 371, 410, 285, 315, 435, 480), intArrayOf(300, 2850, 3150, 741, 819, 570, 630, 869, 961))
        for (r in rows) {
            val wo = parseWorkout("""{"v":1,"name":"t","steps":[{"label":"a","secs":60,"ftp_pct":${r[0]}}]}""")
            val s = wo.steps[0]
            assertEquals(r[1], s.p0); assertEquals(r[2], s.p1)
            assertEquals(listOf(r[3], r[4], r[5], r[6], r[7], r[8]),
                listOf(watts(260, s.p0), watts(260, s.p1), watts(200, s.p0), watts(200, s.p1), watts(305, s.p0), watts(305, s.p1)))
        }
        assertEquals(297, watts(305, 975))
    }

    @Test fun ramps() {
        val wo = parseWorkout(v["w1"]!!.jsonObject["doc"].toString())
        val wu = wo.steps[0]; val cd = wo.steps[9]
        val rows = listOf(
            intArrayOf(0, 500, 124, 137, 550, 136, 150), intArrayOf(1, 500, 124, 137, 550, 136, 150),
            intArrayOf(150, 550, 136, 150, 513, 127, 140), intArrayOf(300, 600, 148, 164, 475, 117, 130),
            intArrayOf(450, 650, 161, 178, 438, 108, 120), intArrayOf(599, 700, 173, 191, 400, 99, 109),
            intArrayOf(600, 700, 173, 191, 400, 99, 109))
        for (r in rows) {
            assertEquals(r[1], rampP10(wu, r[0])); assertEquals(r[2] to r[3], stepRange(260, wu, r[0]))
            assertEquals(r[4], rampP10(cd, r[0])); assertEquals(r[5] to r[6], stepRange(260, cd, r[0]))
        }
        assertEquals(501, rampP10(wu, 2))
        // V§2.5 floor-division table: Kotlin `/` gets the right-hand column
        val fl = listOf(intArrayOf(150, -44400, 513, 513), intArrayOf(300, -89400, 475, 476),
            intArrayOf(450, -134400, 438, 438), intArrayOf(599, -179100, 400, 401))
        for (r in fl) {
            assertEquals(r[1], (400 - 550) * r[0] * 2 + 600)
            assertEquals(r[2], 550 + fdiv(r[1], 1200))
            assertEquals(r[3], 550 + r[1] / 1200)
            assertEquals(r[2], rampP10(cd, r[0]))
        }
    }

    @Test fun zones() {
        val rows = listOf(0 to 1, 100 to 1, 142 to 1, 143 to 1, 144 to 1, 145 to 2, 195 to 2, 196 to 2, 197 to 3, 234 to 3,
            235 to 3, 236 to 4, 273 to 4, 274 to 4, 275 to 5, 312 to 5, 313 to 5, 314 to 6, 390 to 6, 391 to 6, 392 to 7, 600 to 7)
        for ((w, z) in rows) assertEquals(z, zoneOfPower(w, 260), "$w W")
        assertEquals(1, band(55)); assertEquals(2, band(56))
    }

    @Test fun validationAccepts() {
        val valid = v["valid"]!!.jsonArray
        assertEquals(12, valid.size)
        for (c in valid) validate(c.jsonObject["doc"]!!)
        val w = parseWorkout("""{"v":1,"name":"t","steps":[{"label":"Steady","secs":60,"ftp_pct":300}]}""")
        assertEquals(2850 to 3150, w.steps[0].p0 to w.steps[0].p1)
    }

    @Test fun validationRejects() {
        val inv = v["invalid"]!!.jsonArray
        assertEquals(39, inv.size)
        for (c in inv) {
            val o = c.jsonObject
            val e = assertThrows<WorkoutError>(o["name"]!!.jsonPrimitive.content) { validate(o["doc"]!!) }
            assertEquals(o["code"]!!.jsonPrimitive.content, e.code, o["name"]!!.jsonPrimitive.content)
            assertEquals(o["path"]!!.jsonPrimitive.content, e.path, o["name"]!!.jsonPrimitive.content)
        }
    }

    @Test fun framing() {
        val counts = mapOf("01 01 01" to (1 to 1))
        for (m in v["messages"]!!.jsonArray) {
            val b = m.jsonObject["hex"]!!.jsonPrimitive.content.unhex()
            for (cap in listOf(19, 243)) {
                val p = fragment(b, cap)
                assertEquals(1, p.size)
                val r = Reassembler()
                assertArrayEquals(b, p.mapNotNull { r.feed(it, 0) }.single())
            }
        }
        assertTrue(counts.isNotEmpty())
        val w1 = v["w1"]!!.jsonObject["hex"]!!.jsonPrimitive.content.unhex()
        val put = byteArrayOf(0x12, 0x04) + ByteBuf().u32(0).toByteArray() + w1
        assertEquals(233, put.size)
        val p19 = fragment(put, 19)
        assertEquals(13, p19.size)
        assertEquals("80 01 02 03 04 05 06 07 08 09 0a 0b 4c".replace(" ", ""), p19.map { it[0] }.toByteArray().hex())
        assertEquals("80120400000000435701001812240e1809c011f0", p19[0].hex())
        assertEquals("010f00000a133420782038206d696e2074687265", p19[1].hex())
        assertEquals("4c20646f776e", p19[12].hex())
        assertEquals(1, fragment(put, 243).size)
        val r = Reassembler()
        assertArrayEquals(put, p19.mapNotNull { r.feed(it, 0) }.single())
        assertEquals(17, fragment(ByteArray(4006), 243).size)
        val bad = w1.copyOf(); bad[100] = (bad[100].toInt() xor 1).toByte()
        assertEquals(0xF5C8CCE3L, crc32(bad))
    }

    @Test fun climbsV8() {
        val o = v["v8"]!!.jsonObject
        val prof = o["profile"]!!.jsonArray.map { it.jsonPrimitive.double }.toDoubleArray()
        val exp = o["climbs"]!!.jsonArray.map { val c = it.jsonObject
            Climb(c["start_m"]!!.jsonPrimitive.long, c["length_m"]!!.jsonPrimitive.int, c["ascent_m"]!!.jsonPrimitive.int,
                c["avg_x10"]!!.jsonPrimitive.int, c["max_x10"]!!.jsonPrimitive.int) }
        assertEquals(listOf(Climb(490, 1060, 48, 45, 60)), exp)
        assertEquals(exp, detectClimbs(prof, 10))
        assertEquals(emptyList<Climb>(), detectClimbs(DoubleArray(100) { 20.0 }, 10))
        assertEquals(emptyList<Climb>(), detectClimbs(DoubleArray(200) { 20.0 + 0.2 * it }, 10))
        val ramp = DoubleArray(30) { 20.0 } + DoubleArray(21) { 20.0 + 0.6 * it } + DoubleArray(30) { 32.0 }
        assertEquals(emptyList<Climb>(), detectClimbs(ramp, 10))
    }

    @Test fun routeV9() {
        val o = v["v9"]!!.jsonObject
        val pts = o["points"]!!.jsonArray.map { val a = it.jsonArray; Pt(a[0].jsonPrimitive.int, a[1].jsonPrimitive.int) }
        val prof = o["profile_dm"]!!.jsonArray.map { it.jsonPrimitive.int }
        val turns = o["turns"]!!.jsonArray.map { val t = it.jsonObject
            Turn(t["dist_m"]!!.jsonPrimitive.long, t["type"]!!.jsonPrimitive.int, t["exit"]!!.jsonPrimitive.int, t["street"]!!.jsonPrimitive.content) }
        val climbs = o["climbs"]!!.jsonArray.map { val c = it.jsonObject
            Climb(c["start_m"]!!.jsonPrimitive.long, c["length_m"]!!.jsonPrimitive.int, c["ascent_m"]!!.jsonPrimitive.int,
                c["avg_x10"]!!.jsonPrimitive.int, c["max_x10"]!!.jsonPrimitive.int) }
        val r = encodeCr1(o["name"]!!.jsonPrimitive.content, pts, o["prof_step"]!!.jsonPrimitive.int, prof, turns, climbs)
        assertEquals(o["hex"]!!.jsonPrimitive.content, r.bytes.hex())
        assertEquals(157, r.bytes.size)
        assertEquals("99ed9263a898d434", r.id.hex())
        assertEquals(0x34340AE7L, crc32(r.bytes))
        assertEquals(2257L, r.distM); assertEquals(17, r.ascentM)
        val b = ByteBuf(); varint(zigzag(10000), b)
        assertEquals("a09c01", b.toByteArray().hex())
        assertEquals(1L, zigzag(-1)); assertEquals(3L, zigzag(-2)); assertEquals(0L, zigzag(0)); assertEquals(2L, zigzag(1))
    }
}
