package cc700.route.plan

import cc700.route.Pt
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.JsonArray
import kotlinx.serialization.json.JsonElement
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.JsonPrimitive
import kotlinx.serialization.json.add
import kotlinx.serialization.json.addJsonArray
import kotlinx.serialization.json.buildJsonObject
import kotlinx.serialization.json.doubleOrNull
import kotlinx.serialization.json.intOrNull
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

/**
 * OpenRouteService directions (`POST /v2/directions/{profile}/geojson`). Round trips use
 * `options.round_trip {length, points, seed}`. The key is passed in by the caller from secure storage on
 * the phone and is never logged, printed or kept here (START-HERE rule 2); tests use a fake one.
 */
class OrsClient(
    private val http: OkHttpClient,
    private val key: () -> String?,
    private val base: HttpUrl = "https://api.openrouteservice.org".toHttpUrl(),
) : RoutingEngine, PlaceSearch {
    private val json = Json { ignoreUnknownKeys = true }

    /** Which ORS profile a plan uses. Road riders who favour cycle paths get the regular cycling
     * profile, which ORS weights towards cycleways; without that preference the road profile. */
    internal fun orsProfile(p: BikeProfile, favourPaths: Boolean) = when (p) {
        BikeProfile.ROAD -> if (favourPaths) "cycling-regular" else "cycling-road"
        BikeProfile.GRAVEL -> "cycling-regular"
        BikeProfile.MTB -> "cycling-mountain"
    }

    override fun roundTrip(start: Pt, lengthM: Int, seed: Int, profile: BikeProfile, favourPaths: Boolean): Candidate {
        val body = buildJsonObject {
            putJsonArray("coordinates") { addJsonArray { add(start.lonDeg()); add(start.latDeg()) } }
            putJsonObject("options") {
                putJsonObject("round_trip") { put("length", lengthM); put("points", 3); put("seed", seed) }
            }
            common()
        }
        return request(orsProfile(profile, favourPaths), body, "s$seed")
    }

    override fun route(waypoints: List<Pt>, profile: BikeProfile, favourPaths: Boolean): Candidate {
        require(waypoints.size >= 2) { "a route needs at least two waypoints" }
        val body = buildJsonObject {
            putJsonArray("coordinates") { for (w in waypoints) addJsonArray { add(w.lonDeg()); add(w.latDeg()) } }
            common()
        }
        val id = "r" + Integer.toHexString(waypoints.hashCode())
        return request(orsProfile(profile, favourPaths), body, id)
    }

    /** ORS geocoding (Pelias) `GET /geocode/search`, limited to a circle around [near]. */
    override fun searchPlaces(query: String, near: Pt, radiusKm: Double, max: Int): List<Place> {
        val k = key()?.takeIf { it.isNotBlank() } ?: throw RoutingError.NeedsKey()
        val url = base.newBuilder().addPathSegments("geocode/search")
            .addQueryParameter("text", query)
            .addQueryParameter("focus.point.lat", near.latDeg().toString())
            .addQueryParameter("focus.point.lon", near.lonDeg().toString())
            .addQueryParameter("boundary.circle.lat", near.latDeg().toString())
            .addQueryParameter("boundary.circle.lon", near.lonDeg().toString())
            .addQueryParameter("boundary.circle.radius", radiusKm.coerceIn(0.5, 100.0).toString())
            .addQueryParameter("size", max.coerceIn(1, 10).toString())
            .build()
        http.newCall(Request.Builder().url(url).header("Authorization", k).build()).execute().use { r ->
            val text = r.body?.string().orEmpty()
            if (!r.isSuccessful) throw classify(r.code, text)
            val feats = ((json.parseToJsonElement(text) as? JsonObject)?.get("features") as? JsonArray) ?: return emptyList()
            return feats.mapNotNull { f ->
                val o = f as? JsonObject ?: return@mapNotNull null
                val c = ((o["geometry"] as? JsonObject)?.get("coordinates") as? JsonArray) ?: return@mapNotNull null
                if (c.size < 2) return@mapNotNull null
                val lon = (c[0] as? JsonPrimitive)?.doubleOrNull ?: return@mapNotNull null
                val lat = (c[1] as? JsonPrimitive)?.doubleOrNull ?: return@mapNotNull null
                val p = o["properties"] as? JsonObject
                val name = (p?.get("label") as? JsonPrimitive)?.content ?: (p?.get("name") as? JsonPrimitive)?.content ?: return@mapNotNull null
                Place(name, Pt((lat * 1e7).roundToInt(), (lon * 1e7).roundToInt()), (p?.get("distance") as? JsonPrimitive)?.doubleOrNull)
            }
        }
    }

    private fun kotlinx.serialization.json.JsonObjectBuilder.common() {
        put("elevation", true)
        put("instructions", false)
        putJsonArray("extra_info") { add("waytype"); add("surface"); add("steepness") }
    }

    private fun request(profile: String, body: JsonObject, id: String): Candidate {
        val k = key()?.takeIf { it.isNotBlank() } ?: throw RoutingError.NeedsKey()
        val url = base.newBuilder().addPathSegments("v2/directions/$profile/geojson").build()
        val req = Request.Builder().url(url)
            .header("Authorization", k)
            .header("Accept", "application/geo+json, application/json")
            .post(body.toString().toRequestBody("application/json".toMediaType()))
            .build()
        http.newCall(req).execute().use { r ->
            val text = r.body?.string().orEmpty()
            if (!r.isSuccessful) throw classify(r.code, text)
            return parse(text, id)
        }
    }

    private fun classify(code: Int, text: String): RoutingError {
        val msg = runCatching {
            val err = (json.parseToJsonElement(text) as JsonObject)["error"]
            ((err as? JsonObject)?.get("message") as? JsonPrimitive)?.content ?: (err as? JsonPrimitive)?.content
        }.getOrNull() ?: "HTTP $code"
        return when (code) {
            401, 403 -> RoutingError.NeedsKey()
            429 -> RoutingError.RateLimited()
            400, 404 -> RoutingError.NoRoute(msg)
            else -> RoutingError.Failed(code, msg)
        }
    }

    /** Reads the first feature of an ORS GeoJSON response. */
    internal fun parse(text: String, id: String): Candidate {
        val root = json.parseToJsonElement(text) as? JsonObject ?: throw RoutingError.Failed(200, "unexpected response")
        val f = (root["features"] as? JsonArray)?.firstOrNull() as? JsonObject
            ?: throw RoutingError.NoRoute("the routing service returned no route")
        val coords = ((f["geometry"] as? JsonObject)?.get("coordinates") as? JsonArray)
            ?: throw RoutingError.Failed(200, "route without geometry")
        val pts = coords.map { c ->
            val a = c as JsonArray
            Pt(
                (a[1].num() * 1e7).roundToInt(), (a[0].num() * 1e7).roundToInt(),
                a.getOrNull(2)?.let { (it as? JsonPrimitive)?.doubleOrNull },
            )
        }
        if (pts.size < 2) throw RoutingError.NoRoute("the route has fewer than two points")
        val props = f["properties"] as? JsonObject ?: JsonObject(emptyMap())
        val summary = props["summary"] as? JsonObject
        val extras = props["extras"] as? JsonObject
        return Candidate(
            id = id,
            points = pts,
            distanceM = summary?.get("distance").numOr(0.0),
            ascentM = props["ascent"].numOr(0.0),
            durationS = summary?.get("duration").numOr(0.0),
            wayTypes = spans(extras, "waytypes"),
            surfaces = spans(extras, "surface"),
            steepness = spans(extras, "steepness"),
        )
    }

    private fun spans(extras: JsonObject?, name: String): List<Span> {
        val values = ((extras?.get(name) as? JsonObject)?.get("values") as? JsonArray) ?: return emptyList()
        return values.mapNotNull { v ->
            val a = v as? JsonArray ?: return@mapNotNull null
            if (a.size < 3) return@mapNotNull null
            val from = (a[0] as? JsonPrimitive)?.intOrNull ?: return@mapNotNull null
            val to = (a[1] as? JsonPrimitive)?.intOrNull ?: return@mapNotNull null
            val value = (a[2] as? JsonPrimitive)?.intOrNull ?: return@mapNotNull null
            Span(from, to, value)
        }
    }

    private fun JsonElement.num(): Double = (this as JsonPrimitive).doubleOrNull ?: throw RoutingError.Failed(200, "bad number")
    private fun JsonElement?.numOr(d: Double): Double = (this as? JsonPrimitive)?.doubleOrNull ?: d
}

internal fun Pt.latDeg() = latE7 / 1e7
internal fun Pt.lonDeg() = lonE7 / 1e7
