package cc700.route.assist

import cc700.route.Pt
import cc700.route.plan.BikeProfile
import cc700.route.plan.Candidate
import cc700.route.plan.Place
import cc700.route.plan.PlaceSearch
import cc700.route.plan.PlanPrefs
import cc700.route.plan.RoutingEngine
import cc700.route.plan.RoutingError
import cc700.route.plan.Scored
import cc700.route.plan.Scorer
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.JsonArray
import kotlinx.serialization.json.JsonNull
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.JsonObjectBuilder
import kotlinx.serialization.json.JsonPrimitive
import kotlinx.serialization.json.add
import kotlinx.serialization.json.addJsonObject
import kotlinx.serialization.json.booleanOrNull
import kotlinx.serialization.json.buildJsonArray
import kotlinx.serialization.json.buildJsonObject
import kotlinx.serialization.json.doubleOrNull
import kotlinx.serialization.json.put
import kotlinx.serialization.json.putJsonArray
import kotlinx.serialization.json.putJsonObject
import okhttp3.HttpUrl
import okhttp3.HttpUrl.Companion.toHttpUrl
import okhttp3.MediaType.Companion.toMediaType
import okhttp3.OkHttpClient
import okhttp3.Request
import okhttp3.RequestBody.Companion.toRequestBody
import kotlin.math.roundToInt

/** One option the assistant chose, with its one-sentence reason. */
data class AssistOption(val scored: Scored, val reason: String)

data class AssistPlan(val options: List<AssistOption>, val targetM: Double, val routingCalls: Int)

/** Errors from the language-model service. Routing errors stay [RoutingError]. */
sealed class AssistError(msg: String) : Exception(msg) {
    class NeedsKey : AssistError("OpenRouter refused the API key. Enter a new one in Settings.")
    class NoCredit : AssistError("The OpenRouter account has no credit left.")
    class RateLimited : AssistError("OpenRouter is rate limiting this key. Try again in a minute.")
    class Declined(msg: String) : AssistError(msg)
    class Failed(val code: Int, msg: String) : AssistError(msg)
}

/**
 * Plans a ride from a request in plain language ("75 minutes, mostly cycle paths, a café stop") with a
 * language model on OpenRouter (OpenAI-style chat completions with tool calls).
 *
 * The model chooses and explains; it never draws. Routes and places exist for it only as ids returned by
 * its tools, with their distance, climbing and road-type shares; the coordinates stay in this class. So
 * it cannot invent a route, and neither the rider's position nor any route geometry is sent to
 * OpenRouter. The routing service ([engine], [places]) receives the start point, as in the plain planner.
 *
 * At most [maxTurns] model calls and [maxRoutingCalls] routing-service calls per plan, so one request
 * cannot use up the routing quota. The key comes from secure storage on the phone and is never logged
 * or kept here (START-HERE rule 2).
 */
class AssistPlanner(
    private val http: OkHttpClient,
    private val key: () -> String?,
    private val engine: RoutingEngine,
    private val places: PlaceSearch?,
    private val model: String = DEFAULT_MODEL,
    private val base: HttpUrl = "https://openrouter.ai/api/v1/".toHttpUrl(),
    private val maxTurns: Int = 8,
    private val maxRoutingCalls: Int = 20,
) {
    companion object {
        /** DeepSeek V4.1 Flash on OpenRouter; any OpenRouter model with tool calling can be set instead. */
        const val DEFAULT_MODEL = "deepseek/deepseek-v4.1-flash"
    }

    private val json = Json { ignoreUnknownKeys = true }

    fun plan(start: Pt, minutes: Int, request: String, prefs: PlanPrefs): AssistPlan =
        Session(start, minutes / 60.0 * prefs.speedKmh * 1000.0, prefs).run(minutes, request.trim())

    private inner class Session(val start: Pt, val targetM: Double, val prefs: PlanPrefs) {
        val cands = LinkedHashMap<String, Scored>()
        val via = HashMap<String, List<String>>()
        val foundPlaces = LinkedHashMap<String, Place>()
        var routingCalls = 0
        var seed = prefs.seedBase

        fun run(minutes: Int, request: String): AssistPlan {
            val history = ArrayList<JsonObject>()
            history += msg("system", system())
            history += msg("user",
                "Ride request: ${request.ifEmpty { "(none given; plan a pleasant loop)" }}\n" +
                    "Target: about $minutes minutes, which is about ${"%.1f".format(targetM / 1000)} km at ${prefs.speedKmh.roundToInt()} km/h.\n" +
                    "Bike: ${prefs.profile.name.lowercase()}. Favour cycle paths: ${if (prefs.favourPaths) "yes" else "no"}.")
            repeat(maxTurns) {
                val choice = complete(history)
                val message = choice["message"] as? JsonObject ?: throw AssistError.Failed(200, "The model returned no message.")
                val finish = (choice["finish_reason"] as? JsonPrimitive)?.contentOrNullSafe()
                val calls = (message["tool_calls"] as? JsonArray)?.mapNotNull { it as? JsonObject }.orEmpty()
                if (finish == "content_filter") throw AssistError.Declined("The model declined this request. Try rewording it, or use Plan from here.")
                if (calls.isEmpty()) {
                    if (finish == "length") throw AssistError.Failed(200, "The model's reply was cut off. Try a shorter request.")
                    return fallback()
                }
                // Send back only what the API defines for an assistant turn.
                history += buildJsonObject {
                    put("role", "assistant")
                    put("content", (message["content"] as? JsonPrimitive)?.contentOrNullSafe() ?: "")
                    put("tool_calls", JsonArray(calls))
                }
                for (call in calls) {
                    val id = (call["id"] as? JsonPrimitive)?.content ?: "call"
                    val fn = call["function"] as? JsonObject
                    val name = (fn?.get("name") as? JsonPrimitive)?.content.orEmpty()
                    val args = runCatching {
                        json.parseToJsonElement((fn?.get("arguments") as? JsonPrimitive)?.content ?: "{}") as JsonObject
                    }.getOrNull()
                    if (args == null) { history += toolResult(id, "Error: the arguments were not valid JSON."); continue }
                    if (name == "present_options") {
                        val chosen = present(args)
                        if (chosen.isNotEmpty()) return AssistPlan(chosen, targetM, routingCalls)
                        history += toolResult(id, "Error: no valid ids. Use ids from generate_loops or generate_route: ${cands.keys}")
                        continue
                    }
                    history += toolResult(id, try {
                        tool(name, args)
                    } catch (e: RoutingError.NeedsKey) {
                        throw e
                    } catch (e: RoutingError.RateLimited) {
                        throw e
                    } catch (e: Exception) {
                        "Error: " + (e.message ?: e.javaClass.simpleName)
                    })
                }
            }
            return fallback()
        }

        /** One chat completion; returns the first choice. */
        private fun complete(history: List<JsonObject>): JsonObject {
            val k = key()?.takeIf { it.isNotBlank() } ?: throw AssistError.NeedsKey()
            val body = buildJsonObject {
                put("model", model)
                put("messages", JsonArray(history))
                put("tools", tools())
                put("tool_choice", "auto")
                put("max_tokens", 4000)
            }
            val req = Request.Builder().url(base.newBuilder().addPathSegments("chat/completions").build())
                .header("Authorization", "Bearer $k")
                .header("X-Title", "CC700 route planner")
                .post(body.toString().toRequestBody("application/json".toMediaType()))
                .build()
            http.newCall(req).execute().use { r ->
                val text = r.body?.string().orEmpty()
                val root = runCatching { json.parseToJsonElement(text) as? JsonObject }.getOrNull()
                // OpenRouter can report an error with HTTP 200 and an "error" object.
                val err = root?.get("error") as? JsonObject
                val code = if (!r.isSuccessful) r.code else (err?.get("code") as? JsonPrimitive)?.content?.toIntOrNull() ?: if (err != null) 500 else 200
                if (code != 200) {
                    val m = (err?.get("message") as? JsonPrimitive)?.content ?: "HTTP $code"
                    throw when (code) {
                        401, 403 -> AssistError.NeedsKey()
                        402 -> AssistError.NoCredit()
                        429 -> AssistError.RateLimited()
                        else -> AssistError.Failed(code, "The model service failed: $m")
                    }
                }
                return ((root?.get("choices") as? JsonArray)?.firstOrNull() as? JsonObject)
                    ?: throw AssistError.Failed(200, "The model service returned no answer.")
            }
        }

        private fun msg(role: String, content: String) = buildJsonObject { put("role", role); put("content", content) }
        private fun toolResult(id: String, content: String) =
            buildJsonObject { put("role", "tool"); put("tool_call_id", id); put("content", content) }

        private fun system() =
            "You plan bike rides for one rider using a routing service. You cannot see a map or any " +
                "coordinates: routes and places exist only as ids returned by your tools, with their stats. " +
                "Make candidates with the tools, compare them with what the rider asked for, then call " +
                "present_options with up to three distinct ids, best first, each with one plain sentence a " +
                "cyclist would find useful (what makes it fit the request, and anything to watch for). " +
                "The routing service has a small quota: about $maxRoutingCalls calls for this whole plan, so " +
                "ask for a few loops at a time and stop once you have good options. If nothing fits well, " +
                "present the closest and say what falls short."

        // ------------------------------------------------------------------ tools
        private fun tools() = buildJsonArray {
            fn("generate_loops", "Make round-trip loops from the rider's start of about length_km. Each call returns new, different loops.") {
                putJsonObject("length_km") { put("type", "number"); put("description", "Target loop length in km.") }
                putJsonObject("count") { put("type", "integer"); put("description", "How many loops to make, 1 to 4.") }
                profile()
                putJsonObject("favour_paths") { put("type", "boolean"); put("description", "Prefer cycleways and quiet roads.") }
            }
            fn("find_places", "Search for named places or kinds of place (\"cafe\", \"Richmond Park\") near the start. Returns place ids, names and distance from the start.") {
                putJsonObject("query") { put("type", "string") }
                putJsonObject("radius_km") { put("type", "number"); put("description", "Search radius from the start, in km.") }
            }
            fn("generate_route", "Make a loop from the start through the given places in order and back to the start.") {
                putJsonObject("place_ids") {
                    put("type", "array"); putJsonObject("items") { put("type", "string") }
                    put("description", "1 to 3 ids from find_places.")
                }
                profile()
                putJsonObject("favour_paths") { put("type", "boolean") }
            }
            fn("present_options", "Finish: show these routes to the rider, best first.") {
                putJsonObject("options") {
                    put("type", "array"); put("description", "1 to 3 routes.")
                    putJsonObject("items") {
                        put("type", "object")
                        putJsonObject("properties") {
                            putJsonObject("id") { put("type", "string") }
                            putJsonObject("reason") { put("type", "string") }
                        }
                        putJsonArray("required") { add("id"); add("reason") }
                    }
                }
            }
        }

        private fun kotlinx.serialization.json.JsonArrayBuilder.fn(name: String, description: String, props: JsonObjectBuilder.() -> Unit) {
            val p = buildJsonObject(props)
            addJsonObject {
                put("type", "function")
                putJsonObject("function") {
                    put("name", name); put("description", description)
                    putJsonObject("parameters") {
                        put("type", "object"); put("properties", p)
                        putJsonArray("required") { p.keys.forEach { add(it) } }
                    }
                }
            }
        }

        private fun JsonObjectBuilder.profile() = putJsonObject("profile") {
            put("type", "string"); putJsonArray("enum") { add("road"); add("gravel"); add("mtb") }
        }

        private fun tool(name: String, a: JsonObject): String = when (name) {
            "generate_loops" -> {
                val lengthKm = a.num("length_km") ?: (targetM / 1000)
                val count = (a.num("count")?.toInt() ?: 3).coerceIn(1, 4)
                val profile = profileOf(a.str("profile"))
                val paths = a.bool("favour_paths") ?: prefs.favourPaths
                val lengthM = (lengthKm.coerceIn(3.0, 200.0) * 1000).roundToInt()
                val out = buildJsonArray {
                    repeat(count) {
                        if (!budget()) return@repeat
                        val s = seed++
                        try {
                            add(summary(register(engine.roundTrip(start, lengthM, s, profile, paths), profile, emptyList())))
                        } catch (e: RoutingError.NoRoute) {
                            add(buildJsonObject { put("error", "no loop for this try") })
                        }
                    }
                }
                if (out.isEmpty()) throw IllegalStateException("The routing quota for this plan is used up. Present the best routes you have.")
                out.toString()
            }
            "find_places" -> {
                val ps = places ?: throw IllegalStateException("Place search is not available.")
                val q = a.str("query")?.takeIf { it.isNotBlank() } ?: throw IllegalArgumentException("query is empty")
                val radius = a.num("radius_km") ?: (targetM / 1000 / 3)
                if (!budget()) throw IllegalStateException("The routing quota for this plan is used up.")
                val found = ps.searchPlaces(q, start, radius)
                if (found.isEmpty()) "[] (nothing found; try another query or a larger radius)"
                else buildJsonArray {
                    for (p in found) {
                        val id = "p${foundPlaces.size + 1}"
                        foundPlaces[id] = p
                        addJsonObject {
                            put("place_id", id); put("name", p.name)
                            p.distanceKm?.let { put("distance_km", (it * 10).roundToInt() / 10.0) }
                        }
                    }
                }.toString()
            }
            "generate_route" -> {
                val ids = (a["place_ids"] as? JsonArray)?.mapNotNull { (it as? JsonPrimitive)?.content }.orEmpty()
                if (ids.isEmpty() || ids.size > 3) throw IllegalArgumentException("give 1 to 3 place ids")
                val unknown = ids.filter { it !in foundPlaces }
                if (unknown.isNotEmpty()) throw IllegalArgumentException("unknown place ids $unknown; use ids from find_places")
                if (!budget()) throw IllegalStateException("The routing quota for this plan is used up.")
                val profile = profileOf(a.str("profile"))
                val paths = a.bool("favour_paths") ?: prefs.favourPaths
                val pts = listOf(start) + ids.map { foundPlaces.getValue(it).pt } + listOf(start)
                summary(register(engine.route(pts, profile, paths), profile, ids.map { foundPlaces.getValue(it).name })).toString()
            }
            else -> throw IllegalArgumentException("unknown tool $name")
        }

        private fun budget(): Boolean = if (routingCalls >= maxRoutingCalls) false else { routingCalls++; true }

        private fun profileOf(v: String?) = when (v?.lowercase()) {
            "gravel" -> BikeProfile.GRAVEL; "mtb" -> BikeProfile.MTB; "road" -> BikeProfile.ROAD; else -> prefs.profile
        }

        private fun register(c: Candidate, profile: BikeProfile, viaNames: List<String>): Pair<String, Scored> {
            val id = "c${cands.size + 1}"
            val s = Scorer.score(c.copy(id = id), targetM, prefs.copy(profile = profile))
            cands[id] = s
            via[id] = viaNames
            return id to s
        }

        private fun summary(e: Pair<String, Scored>): JsonObject {
            val (id, s) = e
            val c = s.candidate
            val b = s.breakdown
            fun pct(f: Double) = (f * 100).roundToInt()
            return buildJsonObject {
                put("id", id)
                put("km", (c.distanceM / 100).roundToInt() / 10.0)
                put("ascent_m", c.ascentM.roundToInt())
                put("minutes_at_rider_speed", (c.distanceM / 1000 / prefs.speedKmh * 60).roundToInt())
                put("vs_target_pct", pct(s.lengthError))
                put("cycle_path_pct", pct(b.cyclePath))
                put("quiet_street_pct", pct(b.quietRoad))
                put("main_road_pct", pct(b.busyRoad))
                put("unpaved_pct", pct(b.unpaved))
                put("steep_pct", pct(b.steep))
                put("doubles_back_pct", pct(b.backtrack))
                val v = via[id].orEmpty()
                if (v.isNotEmpty()) putJsonArray("via") { v.forEach { add(it) } }
            }
        }

        /** The ids the model chose, in its order; unknown and repeated ids are dropped. */
        private fun present(a: JsonObject): List<AssistOption> {
            val opts = a["options"] as? JsonArray ?: return emptyList()
            val seen = HashSet<String>()
            return opts.mapNotNull { o ->
                val m = o as? JsonObject ?: return@mapNotNull null
                val id = m.str("id") ?: return@mapNotNull null
                val s = cands[id] ?: return@mapNotNull null
                if (!seen.add(id)) return@mapNotNull null
                AssistOption(s, m.str("reason").orEmpty().trim())
            }.take(3)
        }

        /** The model stopped without presenting, or ran out of turns: show the best candidates by score. */
        private fun fallback(): AssistPlan {
            if (cands.isEmpty()) throw AssistError.Failed(200, "The model did not make any routes. Try again, or use Plan from here.")
            val best = cands.values.sortedByDescending { it.score }.take(3)
            return AssistPlan(best.map { AssistOption(it, "") }, targetM, routingCalls)
        }
    }
}

private fun JsonPrimitive.contentOrNullSafe(): String? = if (this is JsonNull) null else content
private fun JsonObject.num(k: String): Double? = (this[k] as? JsonPrimitive)?.doubleOrNull
private fun JsonObject.str(k: String): String? = (this[k] as? JsonPrimitive)?.takeIf { it.isString }?.content
private fun JsonObject.bool(k: String): Boolean? = (this[k] as? JsonPrimitive)?.booleanOrNull
