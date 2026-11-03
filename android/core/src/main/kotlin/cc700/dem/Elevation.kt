package cc700.dem

import cc700.route.Pt
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.JsonArray
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.JsonPrimitive
import kotlinx.serialization.json.doubleOrNull
import kotlinx.serialization.json.jsonArray
import kotlinx.serialization.json.jsonObject
import okhttp3.HttpUrl
import okhttp3.HttpUrl.Companion.toHttpUrl
import okhttp3.OkHttpClient
import okhttp3.Request
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit
import kotlin.math.floor

/** Elevation in metres for each point, or null where there is no data (brief 7.4). */
fun interface ElevationProvider {
    fun sample(points: List<Pt>): List<Double?>
}

/** Sample cache keyed by a 1e-4 degree cell (brief 7.4). The app stores it in Room. */
interface ElevationCache {
    fun get(key: Long): Double?
    fun put(key: Long, value: Double)
}

class MemoryCache : ElevationCache {
    private val m = HashMap<Long, Double>()
    @Synchronized override fun get(key: Long) = m[key]
    @Synchronized override fun put(key: Long, value: Double) { m[key] = value }
}

fun cellKey(p: Pt): Long {
    val la = floor(p.latE7 / 1000.0).toLong()
    val lo = floor(p.lonE7 / 1000.0).toLong()
    return (la shl 32) xor (lo and 0xFFFFFFFFL)
}

private val json = Json { ignoreUnknownKeys = true }

/** Geoscience Australia 5 m LiDAR DEM through ArcGIS `identify`, one request per point, at most 4 at a
 * time (brief 7.4). Verified against the live service on 3 November 2026 (DEM-FINDINGS.md): it answers
 * with `Stretch.Pixel Value`, not the documented `Pixel Value`, and returns NoData over the Perth Hills,
 * so hills rides fall through to Open Topo Data. Both attribute names are accepted (E8). */
class GaProvider(
    private val http: OkHttpClient,
    private val cache: ElevationCache,
    private val base: HttpUrl = "https://services.ga.gov.au/gis/rest/services/DEM_LiDAR_5m_2025/MapServer".toHttpUrl(),
    private val concurrency: Int = 4,
) : ElevationProvider {
    fun one(p: Pt): Double? {
        val key = cellKey(p)
        cache.get(key)?.let { return it }
        val lon = p.lonE7 / 1e7; val lat = p.latE7 / 1e7
        val url = base.newBuilder().addPathSegment("identify")
            .addQueryParameter("geometry", "$lon,$lat")
            .addQueryParameter("geometryType", "esriGeometryPoint")
            .addQueryParameter("sr", "4326")
            .addQueryParameter("tolerance", "0")
            .addQueryParameter("mapExtent", "%.6f,%.6f,%.6f,%.6f".format(lon - 0.001, lat - 0.001, lon + 0.001, lat + 0.001))
            .addQueryParameter("imageDisplay", "100,100,96")
            .addQueryParameter("returnGeometry", "false")
            .addQueryParameter("f", "json").build()
        http.newCall(Request.Builder().url(url).build()).execute().use { r ->
            if (!r.isSuccessful) return null
            val o = json.parseToJsonElement(r.body?.string() ?: return null).jsonObject
            val res = o["results"] as? JsonArray ?: return null
            for (item in res) {
                val attrs = (item as? JsonObject)?.get("attributes") as? JsonObject ?: continue
                // The live service names this "Stretch.Pixel Value" (the renderer prefixes it), not the
                // plain "Pixel Value" the ArcGIS REST documentation shows. Accept either (E8).
                val name = attrs.keys.firstOrNull { it == "Pixel Value" || it.endsWith(".Pixel Value") } ?: continue
                val v = (attrs[name] as? JsonPrimitive)?.content?.toDoubleOrNull() ?: continue
                if (v < -1000 || v > 9000) continue /* NoData */
                cache.put(key, v)
                return v
            }
            return null
        }
    }

    override fun sample(points: List<Pt>): List<Double?> {
        val pool = Executors.newFixedThreadPool(concurrency)
        try {
            val futures = points.map { p -> pool.submit<Double?> { runCatching { one(p) }.getOrNull() } }
            return futures.map { it.get(60, TimeUnit.SECONDS) }
        } finally {
            pool.shutdown()
        }
    }
}

/** Open Topo Data SRTM 30 m: 100 locations per request, at most one request a second (public limits). */
class OpenTopoProvider(
    private val http: OkHttpClient,
    private val cache: ElevationCache,
    private val base: HttpUrl = "https://api.opentopodata.org/v1/srtm30m".toHttpUrl(),
    private val pause: (Long) -> Unit = { Thread.sleep(it) },
) : ElevationProvider {
    override fun sample(points: List<Pt>): List<Double?> {
        val out = arrayOfNulls<Double>(points.size)
        val todo = points.indices.filter { i -> cache.get(cellKey(points[i]))?.also { out[i] = it } == null }
        todo.chunked(100).forEachIndexed { k, batch ->
            if (k > 0) pause(1000)
            val locs = batch.joinToString("|") { "%.6f,%.6f".format(points[it].latE7 / 1e7, points[it].lonE7 / 1e7) }
            val url = base.newBuilder().addQueryParameter("locations", locs).build()
            http.newCall(Request.Builder().url(url).build()).execute().use { r ->
                if (!r.isSuccessful) return out.toList()
                val o = json.parseToJsonElement(r.body?.string() ?: return out.toList()).jsonObject
                val res = o["results"]?.jsonArray ?: return out.toList()
                res.forEachIndexed { j, e ->
                    val v = ((e as? JsonObject)?.get("elevation") as? JsonPrimitive)?.doubleOrNull
                    if (v != null && j < batch.size) {
                        out[batch[j]] = v
                        cache.put(cellKey(points[batch[j]]), v)
                    }
                }
            }
        }
        return out.toList()
    }
}

/** Tries each provider in order; each fills only the samples the earlier ones left empty. A provider
 * that throws is skipped. The caller falls back to the GPX file's own elevations when all are null. */
class ChainProvider(private val providers: List<ElevationProvider>) : ElevationProvider {
    override fun sample(points: List<Pt>): List<Double?> {
        val out = arrayOfNulls<Double>(points.size)
        for (p in providers) {
            val missing = points.indices.filter { out[it] == null }
            if (missing.isEmpty()) break
            val got = runCatching { p.sample(missing.map { points[it] }) }.getOrNull() ?: continue
            missing.forEachIndexed { k, i -> out[i] = got.getOrNull(k) }
        }
        return out.toList()
    }
}
