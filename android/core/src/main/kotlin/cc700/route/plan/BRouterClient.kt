package cc700.route.plan

import cc700.route.Pt
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.JsonArray
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.JsonPrimitive
import kotlinx.serialization.json.doubleOrNull
import okhttp3.HttpUrl
import okhttp3.HttpUrl.Companion.toHttpUrl
import okhttp3.OkHttpClient
import okhttp3.Request
import kotlin.math.roundToInt

/**
 * BRouter (brouter.de), a bike router whose profiles weigh traffic and road type heavily, so it keeps to
 * cycleways and quiet roads far more than a general router. No key is needed.
 *
 * Round trips use BRouter's round-trip mode (`engineMode=4`): `roundTripDistance` is the loop's radius
 * (measured: a 3.2 km radius gives about 20 km), and `direction` the bearing the loop heads out on, so
 * each seed is a different direction rather than a random reshuffle. Every stretch of the result carries
 * its OpenStreetMap tags, which are mapped to the same way-type and surface codes OpenRouteService uses,
 * so the [Scorer] treats both engines alike.
 */
class BRouterClient(
    private val http: OkHttpClient,
    private val base: HttpUrl = "https://brouter.de/brouter".toHttpUrl(),
) : RoutingEngine {
    private val json = Json { ignoreUnknownKeys = true }

    /** BRouter's built-in profiles: `safety` avoids traffic hardest, `trekking` is the touring default,
     * `fastbike` is for road riders who do not mind roads. */
    internal fun profile(p: BikeProfile, favourPaths: Boolean) = when (p) {
        BikeProfile.ROAD -> if (favourPaths) "safety" else "fastbike"
        BikeProfile.GRAVEL -> "trekking"
        BikeProfile.MTB -> "trekking"
    }

    /** The out-bound bearing for a seed: six directions 60° apart, shifted by 30° on each further round. */
    internal fun direction(seed: Int): Int {
        val i = Math.floorMod(seed - 1, 6)
        val round = Math.floorDiv(seed - 1, 6)
        return Math.floorMod(i * 60 + round * 30 + 15, 360)
    }

    override fun roundTrip(start: Pt, lengthM: Int, seed: Int, profile: BikeProfile, favourPaths: Boolean): Candidate {
        val url = base.newBuilder()
            .addQueryParameter("lonlats", "${start.lonDeg()},${start.latDeg()}")
            .addQueryParameter("profile", profile(profile, favourPaths))
            .addQueryParameter("engineMode", "4")
            .addQueryParameter("roundTripDistance", (lengthM / 6.0).roundToInt().coerceAtLeast(500).toString())
            .addQueryParameter("direction", direction(seed).toString())
            .addQueryParameter("format", "geojson")
            .build()
        return fetch(url, "b$seed")
    }

    override fun route(waypoints: List<Pt>, profile: BikeProfile, favourPaths: Boolean): Candidate {
        require(waypoints.size >= 2) { "a route needs at least two waypoints" }
        val url = base.newBuilder()
            .addQueryParameter("lonlats", waypoints.joinToString("|") { "${it.lonDeg()},${it.latDeg()}" })
            .addQueryParameter("profile", profile(profile, favourPaths))
            .addQueryParameter("alternativeidx", "0")
            .addQueryParameter("format", "geojson")
            .build()
        return fetch(url, "r" + Integer.toHexString(waypoints.hashCode()))
    }

    private fun fetch(url: HttpUrl, id: String): Candidate {
        val req = Request.Builder().url(url).header("User-Agent", "CC700-companion/0.1").build()
        http.newCall(req).execute().use { r ->
            val text = r.body?.string().orEmpty()
            if (!r.isSuccessful) {
                // BRouter answers a point it cannot route from or to ("target island detected", "position
                // not mapped") with 400 and a line of text.
                val msg = text.lineSequence().firstOrNull { it.isNotBlank() }?.trim()?.take(200) ?: "HTTP ${r.code}"
                throw when (r.code) {
                    400, 404 -> RoutingError.NoRoute(msg)
                    429 -> RoutingError.RateLimited()
                    else -> RoutingError.Failed(r.code, "BRouter: $msg")
                }
            }
            return parse(text, id)
        }
    }

    internal fun parse(text: String, id: String): Candidate {
        val root = runCatching { json.parseToJsonElement(text) as JsonObject }.getOrNull()
            ?: throw RoutingError.Failed(200, "BRouter returned something other than GeoJSON")
        val f = (root["features"] as? JsonArray)?.firstOrNull() as? JsonObject ?: throw RoutingError.NoRoute("BRouter returned no route")
        val coords = ((f["geometry"] as? JsonObject)?.get("coordinates") as? JsonArray) ?: throw RoutingError.Failed(200, "route without geometry")
        val pts = coords.mapNotNull { c ->
            val a = c as? JsonArray ?: return@mapNotNull null
            val lon = (a.getOrNull(0) as? JsonPrimitive)?.doubleOrNull ?: return@mapNotNull null
            val lat = (a.getOrNull(1) as? JsonPrimitive)?.doubleOrNull ?: return@mapNotNull null
            Pt((lat * 1e7).roundToInt(), (lon * 1e7).roundToInt(), (a.getOrNull(2) as? JsonPrimitive)?.doubleOrNull)
        }
        if (pts.size < 2) throw RoutingError.NoRoute("the route has fewer than two points")
        val props = f["properties"] as? JsonObject ?: JsonObject(emptyMap())
        fun prop(k: String) = (props[k] as? JsonPrimitive)?.content?.toDoubleOrNull()
        val (ways, surfaces) = spans(props["messages"] as? JsonArray, pts)
        return Candidate(
            id = id,
            points = pts,
            distanceM = prop("track-length") ?: 0.0,
            ascentM = prop("filtered ascend") ?: 0.0,
            durationS = prop("total-time") ?: 0.0,
            wayTypes = ways,
            surfaces = surfaces,
        )
    }

    /**
     * `messages` is a table: a header row, then one row per stretch with the stretch's end point
     * (lon, lat in 1e-6 degrees) and its way tags. Each row's end point is found in the geometry, scanning
     * forward, and the points since the previous row get that row's way type and surface.
     */
    private fun spans(messages: JsonArray?, pts: List<Pt>): Pair<List<Span>, List<Span>> {
        if (messages == null || messages.size < 2) return emptyList<Span>() to emptyList()
        val header = (messages[0] as? JsonArray)?.map { (it as? JsonPrimitive)?.content } ?: return emptyList<Span>() to emptyList()
        val iLon = header.indexOf("Longitude"); val iLat = header.indexOf("Latitude"); val iTags = header.indexOf("WayTags")
        if (iLon < 0 || iLat < 0 || iTags < 0) return emptyList<Span>() to emptyList()
        val e6 = pts.map { (it.lonE7 / 10.0).roundToInt() to (it.latE7 / 10.0).roundToInt() }
        val ways = ArrayList<Span>()
        val surfaces = ArrayList<Span>()
        var from = 0
        for (k in 1 until messages.size) {
            val row = messages[k] as? JsonArray ?: continue
            fun cell(i: Int) = (row.getOrNull(i) as? JsonPrimitive)?.content
            val lon = cell(iLon)?.toIntOrNull() ?: continue
            val lat = cell(iLat)?.toIntOrNull() ?: continue
            var to = -1
            for (j in from until e6.size) {
                val (x, y) = e6[j]
                if (kotlin.math.abs(x - lon) <= 1 && kotlin.math.abs(y - lat) <= 1) { to = j; break }
            }
            if (to < 0) continue
            if (to > from) {
                val tags = parseTags(cell(iTags).orEmpty())
                ways += Span(from, to, wayTypeOf(tags))
                surfaces += Span(from, to, surfaceOf(tags))
            }
            from = to
        }
        return ways to surfaces
    }

    internal fun parseTags(s: String): Map<String, String> =
        s.split(' ').mapNotNull { t -> t.indexOf('=').takeIf { it > 0 }?.let { t.substring(0, it) to t.substring(it + 1) } }.toMap()
}

/** OpenStreetMap tags to OpenRouteService way-type codes ([WayType]). */
internal fun wayTypeOf(tags: Map<String, String>): Int {
    if (tags["route"] == "ferry") return WayType.FERRY
    return when (tags["highway"]) {
        "cycleway" -> WayType.CYCLEWAY
        "footway", "pedestrian" -> WayType.FOOTWAY
        "path", "bridleway" -> WayType.PATH
        "track" -> WayType.TRACK
        "steps" -> WayType.STEPS
        "residential", "living_street", "service" -> WayType.STREET
        "primary", "primary_link", "trunk", "trunk_link", "secondary", "secondary_link" -> WayType.STATE_ROAD
        "construction" -> WayType.CONSTRUCTION
        null -> WayType.UNKNOWN
        else -> WayType.ROAD
    }
}

/** OpenStreetMap `surface` to OpenRouteService surface codes ([Surface]). */
internal fun surfaceOf(tags: Map<String, String>): Int = when (tags["surface"]) {
    "paved" -> 1; "unpaved" -> 2; "asphalt" -> 3; "concrete", "concrete:plates", "concrete:lanes" -> 4
    "sett", "cobblestone", "unhewn_cobblestone" -> 5; "metal" -> 6; "wood" -> 7; "compacted" -> 8
    "fine_gravel" -> 9; "gravel", "pebblestone" -> 10; "dirt", "earth", "mud" -> 11; "ground" -> 12
    "ice", "snow" -> 13; "paving_stones" -> 14; "sand" -> 15; "woodchips" -> 16; "grass" -> 17; "grass_paver" -> 18
    else -> 0
}
