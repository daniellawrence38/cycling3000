package cc700.workout

import cc700.util.ByteBuf
import cc700.util.ByteReader
import cc700.util.fdiv
import cc700.util.objectId
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.JsonArray
import kotlinx.serialization.json.JsonElement
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.JsonPrimitive
import kotlinx.serialization.json.booleanOrNull
import kotlinx.serialization.json.doubleOrNull
import kotlin.math.abs


/** A validation error: a code from V§4 and a JSON pointer. [message] is the readable text (brief 7.3). */
class WorkoutError(val code: String, val path: String) : Exception("$code at ${path.ifEmpty { "(root)" }}") {
    val readable: String get() = describe(code, path)
}

/** One step after conversion: percentages in tenths (V§2.1). */
data class PctStep(
    val label: String, val secs: Int, val p0: Int, val p1: Int, val ramp: Boolean, val rpm: Int,
    val repIdx: Int, val repTotal: Int,
)

data class Workout(val name: String, val steps: List<PctStep>) {
    val totalSecs: Int get() = steps.sumOf { it.secs }
}

private val LABEL_RE = Regex("^[A-Za-z0-9 ]{1,16}$")
private val STEP_KEYS = setOf("label", "secs", "ftp_pct", "ramp", "rpm")
private val GROUP_KEYS = setOf("reps", "steps")

/** Typography normalisation before validation (brief 7.3): nothing else is corrected. */
fun normalise(text: String): String = text
    .replace('×', 'x')
    .replace('–', '-').replace('—', '-')
    .replace('‘', '\'').replace('’', '\'')
    .replace('“', '"').replace('”', '"')
    .replace(' ', ' ')

private fun isInt(e: JsonElement?): Boolean {
    if (e !is JsonPrimitive || e.isString || e.booleanOrNull != null) return false
    val c = e.content
    return c.isNotEmpty() && c.all { it.isDigit() || it == '-' }
}

private fun intOf(e: JsonElement?): Long? = if (isInt(e)) (e as JsonPrimitive).content.toLongOrNull() else null

private fun isNumber(e: JsonElement?): Boolean =
    e is JsonPrimitive && !e.isString && e.booleanOrNull == null && e.doubleOrNull != null

/** Percent value to tenths: 0.1 resolution, 1.0 to 300.0 (ref.tenths). */
private fun tenths(e: JsonElement?, path: String): Int {
    if (!isNumber(e)) throw WorkoutError("E_PCT", path)
    val x = (e as JsonPrimitive).doubleOrNull!!
    val t = Math.rint(x * 10)
    if (abs(x * 10 - t) > 1e-6 || t < 10 || t > 3000) throw WorkoutError("E_PCT", path)
    return t.toInt()
}

private fun validateStep(s: JsonElement, path: String) {
    if (s !is JsonObject) throw WorkoutError("E_STEP_TYPE", path)
    for (k in s.keys) if (k !in STEP_KEYS) throw WorkoutError("E_UNKNOWN_FIELD", "$path/$k")
    for (k in listOf("label", "secs", "ftp_pct")) if (k !in s) throw WorkoutError("E_FIELD_MISSING", "$path/$k")
    val lab = s["label"]
    if (lab !is JsonPrimitive || !lab.isString || !LABEL_RE.matches(lab.content) || lab.content.isBlank())
        throw WorkoutError("E_LABEL", "$path/label")
    val secs = intOf(s["secs"])
    if (secs == null || secs < 5 || secs > 43200) throw WorkoutError("E_SECS", "$path/secs")
    val p = s["ftp_pct"]!!
    val rampEl = s["ramp"]
    val ramp = when {
        rampEl == null -> false
        rampEl is JsonPrimitive && !rampEl.isString && rampEl.booleanOrNull != null -> rampEl.booleanOrNull!!
        else -> throw WorkoutError("E_RAMP", "$path/ramp")
    }
    if (p is JsonArray) {
        if (p.size != 2) throw WorkoutError("E_PCT", "$path/ftp_pct")
        val a = tenths(p[0], "$path/ftp_pct/0")
        val b = tenths(p[1], "$path/ftp_pct/1")
        if (!ramp && a >= b) throw WorkoutError("E_PCT", "$path/ftp_pct")
        if (ramp && a == b) throw WorkoutError("E_RAMP", "$path/ramp")
    } else {
        tenths(p, "$path/ftp_pct")
        if (ramp) throw WorkoutError("E_RAMP", "$path/ramp")
    }
    if ("rpm" in s) {
        val r = intOf(s["rpm"])
        if (r == null || r < 40 || r > 130) throw WorkoutError("E_RPM", "$path/rpm")
    }
}

private fun isGroup(e: JsonElement) = e is JsonObject && ("reps" in e || "steps" in e)

/** Validates a parsed document (V§4); throws the first [WorkoutError] in document order. */
fun validate(doc: JsonElement) {
    if (doc !is JsonObject) throw WorkoutError("E_JSON", "")
    for (k in doc.keys) if (k !in setOf("v", "name", "steps")) throw WorkoutError("E_UNKNOWN_FIELD", "/$k")
    if (intOf(doc["v"]) != 1L) throw WorkoutError("E_VERSION", "/v")
    val name = doc["name"]
    if (name !is JsonPrimitive || !name.isString || name.content.length !in 1..40 || name.content.any { it.code !in 0x20..0x7E })
        throw WorkoutError("E_NAME", "/name")
    val steps = doc["steps"]
    if (steps !is JsonArray || steps.isEmpty()) throw WorkoutError("E_STEPS_EMPTY", "/steps")
    var count = 0L
    var total = 0L
    steps.forEachIndexed { i, item ->
        val path = "/steps/$i"
        if (isGroup(item)) {
            item as JsonObject
            for (k in item.keys) if (k !in GROUP_KEYS) throw WorkoutError("E_UNKNOWN_FIELD", "$path/$k")
            val reps = intOf(item["reps"])
            if (reps == null || reps < 2 || reps > 30) throw WorkoutError("E_REPS", "$path/reps")
            val inner = item["steps"]
            if (inner !is JsonArray || inner.isEmpty()) throw WorkoutError("E_STEPS_EMPTY", "$path/steps")
            inner.forEachIndexed { j, s ->
                if (isGroup(s)) throw WorkoutError("E_NESTED", "$path/steps/$j")
                validateStep(s, "$path/steps/$j")
            }
            count += reps * inner.size
            total += reps * inner.sumOf { intOf((it as JsonObject)["secs"])!! }
        } else {
            validateStep(item, path)
            count += 1
            total += intOf((item as JsonObject)["secs"])!!
        }
        if (count > 100) throw WorkoutError("E_TOO_MANY_STEPS", "/steps")
    }
    if (total > 86400) throw WorkoutError("E_TOO_LONG", "/steps")
}

/** Parses text (after normalisation), validates and converts (V§2.1). */
fun parseWorkout(text: String): Workout {
    val doc = try {
        Json.parseToJsonElement(normalise(text))
    } catch (e: Exception) {
        throw WorkoutError("E_JSON", "")
    }
    validate(doc)
    return toPercentForm(doc as JsonObject)
}

private fun pct10(e: JsonElement) = Math.rint((e as JsonPrimitive).doubleOrNull!! * 10).toInt()

/** Repeats expanded, single targets widened to +/- 5 % of the target (ref.to_percent_form). */
fun toPercentForm(doc: JsonObject): Workout {
    val out = ArrayList<PctStep>()
    for (item in doc["steps"] as JsonArray) {
        val group = if (isGroup(item)) {
            val reps = intOf((item as JsonObject)["reps"])!!.toInt()
            (1..reps).flatMap { r -> (item["steps"] as JsonArray).map { Triple(it as JsonObject, r, reps) } }
        } else listOf(Triple(item as JsonObject, 0, 0))
        for ((s, ri, rt) in group) {
            val p = s["ftp_pct"]!!
            val ramp = (s["ramp"] as? JsonPrimitive)?.booleanOrNull ?: false
            val (p0, p1) = if (p is JsonArray) pct10(p[0]) to pct10(p[1]) else {
                val n = pct10(p)
                fdiv(n * 95 + 50, 100) to fdiv(n * 105 + 50, 100)
            }
            out += PctStep((s["label"] as JsonPrimitive).content, intOf(s["secs"])!!.toInt(), p0, p1, ramp,
                intOf(s["rpm"])?.toInt() ?: 0, ri, rt)
        }
    }
    return Workout((doc["name"] as JsonPrimitive).content, out)
}

/** CW1 object (protocol 3.2) with its ID filled in. */
fun encodeCw1(w: Workout): ByteArray {
    val b = ByteBuf()
    b.u8('C'.code).u8('W'.code).u8(1).u8(0)
    repeat(8) { b.u8(0) }
    b.u32(w.totalSecs.toLong()).u8(w.steps.size).u8(w.name.length).bytes(w.name.toByteArray(Charsets.US_ASCII))
    for (s in w.steps) {
        val lab = s.label.toByteArray(Charsets.US_ASCII)
        b.u16(s.secs).u16(s.p0).u16(s.p1).u8(if (s.ramp) 1 else 0).u8(s.rpm).u8(s.repIdx).u8(s.repTotal).u8(lab.size).bytes(lab)
    }
    val obj = b.toByteArray()
    objectId(obj).copyInto(obj, 4)
    return obj
}

fun decodeCw1(obj: ByteArray): Workout {
    val r = ByteReader(obj)
    require(r.u8() == 'C'.code && r.u8() == 'W'.code && r.u8() == 1) { "not CW1" }
    r.u8(); r.bytes(8)
    r.u32()
    val n = r.u8()
    val name = String(r.bytes(r.u8()), Charsets.US_ASCII)
    val steps = (0 until n).map {
        val secs = r.u16(); val p0 = r.u16(); val p1 = r.u16(); val fl = r.u8(); val rpm = r.u8()
        val ri = r.u8(); val rt = r.u8(); val lab = String(r.bytes(r.u8()), Charsets.US_ASCII)
        PctStep(lab, secs, p0, p1, fl and 1 == 1, rpm, ri, rt)
    }
    require(r.remaining == 0) { "trailing bytes" }
    return Workout(name, steps)
}

// ------------------------------------------------------------------ unit-side maths, for the preview
fun watts(ftp: Int, p10: Int): Int = fdiv(ftp.toLong() * p10 + 500, 1000).toInt()
fun rampP10(s: PctStep, e: Int): Int = s.p0 + fdiv((s.p1 - s.p0).toLong() * e * 2 + s.secs, 2L * s.secs).toInt()
fun stepRange(ftp: Int, s: PctStep, e: Int = 0): Pair<Int, Int> {
    if (!s.ramp) return watts(ftp, s.p0) to watts(ftp, s.p1)
    val p = rampP10(s, e)
    return watts(ftp, fdiv(p * 95 + 50, 100)) to watts(ftp, fdiv(p * 105 + 50, 100))
}
fun band(r: Int): Int = when { r <= 55 -> 1; r <= 75 -> 2; r <= 90 -> 3; r <= 105 -> 4; r <= 120 -> 5; r <= 150 -> 6; else -> 7 }
fun zoneOfPower(w: Int, ftp: Int): Int = band(fdiv(w.toLong() * 200 + ftp, 2L * ftp).toInt())
fun targetZone(s: PctStep, e: Int = 0): Int = band(fdiv((if (s.ramp) rampP10(s, e) else fdiv(s.p0 + s.p1, 2)) + 5, 10))

/** Readable error text naming the field and position (brief 7.3). */
fun describe(code: String, path: String): String {
    val m = Regex("^/steps/(\\d+)(?:/steps/(\\d+))?").find(path)
    val where = when {
        m == null -> ""
        m.groupValues[2].isNotEmpty() -> "Step ${m.groupValues[1].toInt() + 1}, item ${m.groupValues[2].toInt() + 1}: "
        else -> "Step ${m.groupValues[1].toInt() + 1}: "
    }
    val field = path.substringAfterLast('/')
    return where + when (code) {
        "E_JSON" -> "this is not a workout file (it must be a JSON object)"
        "E_VERSION" -> "\"v\" must be 1"
        "E_UNKNOWN_FIELD" -> "unknown field \"$field\""
        "E_NAME" -> "name must be 1 to 40 plain ASCII characters"
        "E_STEPS_EMPTY" -> "steps must not be empty"
        "E_LABEL" -> "label must be 1 to 16 letters, digits or spaces"
        "E_SECS" -> "secs must be a whole number from 5 to 43200"
        "E_FIELD_MISSING" -> "\"$field\" is missing"
        "E_PCT" -> "ftp_pct must be 1 to 300 with at most one decimal place (a pair must be low then high)"
        "E_RAMP" -> "ramp needs a pair of different values"
        "E_RPM" -> "rpm must be a whole number from 40 to 130"
        "E_REPS" -> "reps must be a whole number from 2 to 30"
        "E_NESTED" -> "repeat groups cannot be nested"
        "E_TOO_MANY_STEPS" -> "more than 100 steps after repeats are expanded"
        "E_TOO_LONG" -> "longer than 24 hours in total"
        "E_STEP_TYPE" -> "each step must be an object"
        else -> code
    }
}
