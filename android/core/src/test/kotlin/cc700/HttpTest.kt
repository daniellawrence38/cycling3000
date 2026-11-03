package cc700

import cc700.dem.ChainProvider
import cc700.dem.ElevationProvider
import cc700.dem.GaProvider
import cc700.dem.MemoryCache
import cc700.dem.OpenTopoProvider
import cc700.route.Pt
import cc700.upload.IntervalsClient
import cc700.upload.UploadResult
import okhttp3.OkHttpClient
import okhttp3.mockwebserver.MockResponse
import okhttp3.mockwebserver.MockWebServer
import org.junit.jupiter.api.AfterEach
import org.junit.jupiter.api.Assertions.assertEquals
import org.junit.jupiter.api.Assertions.assertNull
import org.junit.jupiter.api.Assertions.assertTrue
import org.junit.jupiter.api.BeforeEach
import org.junit.jupiter.api.Test
import java.util.Base64

class HttpTest {
    private lateinit var srv: MockWebServer
    private val http = OkHttpClient()
    /** Not a real key: tests never hold one (START-HERE rule 2). */
    private val fakeKey = "not-a-real-key"

    @BeforeEach fun up() { srv = MockWebServer(); srv.start() }
    @AfterEach fun down() { srv.shutdown() }

    private fun client() = IntervalsClient(http, { fakeKey }, srv.url("/"))
    private val start = 1793000000L // 2026-10-26T07:33:20Z
    private val fit = byteArrayOf(14, 0x10, 1, 2, 3)

    @Test fun uploadSuccess() {
        srv.enqueue(MockResponse().setBody("""[{"id":"i1","start_date":"2026-10-25T07:00:00Z"}]"""))
        srv.enqueue(MockResponse().setResponseCode(201).setBody("""{"id":"i99"}"""))
        assertEquals(UploadResult.Uploaded("i99"), client().upload(fit, "1793000000.fit", start))
        val list = srv.takeRequest()
        assertEquals("GET", list.method)
        assertTrue(list.path!!.startsWith("/api/v1/athlete/0/activities?oldest=2026-10-25&newest=2026-10-27"), list.path)
        val auth = list.getHeader("Authorization")!!
        assertEquals("API_KEY:$fakeKey", String(Base64.getDecoder().decode(auth.removePrefix("Basic "))))
        val post = srv.takeRequest()
        assertEquals("POST", post.method)
        assertEquals("/api/v1/athlete/0/activities", post.path)
        assertTrue(post.getHeader("Content-Type")!!.startsWith("multipart/form-data"))
        val body = post.body.readUtf8()
        assertTrue(body.contains("name=\"file\"; filename=\"1793000000.fit\""))
    }

    @Test fun duplicateIsNotSentAgain() {
        srv.enqueue(MockResponse().setBody("""[{"id":"i7","start_date":"2026-10-26T07:34:10Z"}]""")) // 50 s later
        assertEquals(UploadResult.Duplicate("i7"), client().upload(fit, "x.fit", start))
        assertEquals(1, srv.requestCount)
        // 61 s apart is a different activity
        srv.enqueue(MockResponse().setBody("""[{"id":"i8","start_date":"2026-10-26T07:34:21Z"}]"""))
        srv.enqueue(MockResponse().setBody("""{"id":"i9"}"""))
        assertEquals(UploadResult.Uploaded("i9"), client().upload(fit, "x.fit", start))
    }

    @Test fun unauthorizedStops() {
        srv.enqueue(MockResponse().setResponseCode(401).setBody("""{"error":"Unauthorized"}"""))
        assertEquals(UploadResult.Unauthorized, client().upload(fit, "x.fit", start))
        srv.enqueue(MockResponse().setBody("[]"))
        srv.enqueue(MockResponse().setResponseCode(401))
        assertEquals(UploadResult.Unauthorized, client().upload(fit, "x.fit", start))
    }

    @Test fun rateLimitAndServerErrorsRetry() {
        srv.enqueue(MockResponse().setBody("[]"))
        srv.enqueue(MockResponse().setResponseCode(429))
        assertEquals(UploadResult.RetryLater(429), client().upload(fit, "x.fit", start))
        srv.enqueue(MockResponse().setBody("[]"))
        srv.enqueue(MockResponse().setResponseCode(503))
        assertEquals(UploadResult.RetryLater(503), client().upload(fit, "x.fit", start))
        srv.enqueue(MockResponse().setResponseCode(500))
        assertEquals(UploadResult.RetryLater(500), client().upload(fit, "x.fit", start))
    }

    @Test fun otherClientErrorsFailWithTheMessage() {
        srv.enqueue(MockResponse().setBody("[]"))
        srv.enqueue(MockResponse().setResponseCode(422).setBody("""{"error":"File is not a valid FIT file"}"""))
        assertEquals(UploadResult.Failed(422, "File is not a valid FIT file"), client().upload(fit, "x.fit", start))
    }

    @Test fun noConnectionRetries() {
        srv.shutdown()
        assertTrue(client().upload(fit, "x.fit", start) is UploadResult.RetryLater)
    }

    // ------------------------------------------------------------------ elevation
    private val kp = Pt(-319640000, 1158300000)

    /** The live service prefixes the attribute with its renderer: "Stretch.Pixel Value" (DEM-FINDINGS.md,
     * E8). These are the shapes seen on 3 November 2026; a value, and NoData over the Perth Hills. */
    @Test fun gaIdentifyLiveAttributeName() {
        srv.enqueue(MockResponse().setBody("""{"results":[{"layerId":0,"layerName":"DEM_LiDAR_5m_2025","attributes":{"Stretch.Pixel Value":"60.6492"}}]}"""))
        srv.enqueue(MockResponse().setBody("""{"results":[{"attributes":{"Stretch.Pixel Value":"NoData"}}]}"""))
        val ga = GaProvider(http, MemoryCache(), srv.url("/MapServer"), concurrency = 1)
        assertEquals(listOf(60.6492, null), ga.sample(listOf(kp, Pt(-319650000, 1158400000))))
    }

    @Test fun gaIdentify() {
        srv.enqueue(MockResponse().setBody("""{"results":[{"layerId":0,"attributes":{"Pixel Value":"23.75"}}]}"""))
        srv.enqueue(MockResponse().setBody("""{"results":[{"attributes":{"Pixel Value":"NoData"}}]}"""))
        val cache = MemoryCache()
        val ga = GaProvider(http, cache, srv.url("/MapServer"), concurrency = 1)
        assertEquals(listOf(23.75, null), ga.sample(listOf(kp, Pt(-319650000, 1158400000))))
        val r = srv.takeRequest()
        val u = r.requestUrl!!
        assertEquals("/MapServer/identify", u.encodedPath)
        assertEquals("115.83,-31.964", u.queryParameter("geometry"))
        assertEquals("esriGeometryPoint", u.queryParameter("geometryType"))
        assertEquals("4326", u.queryParameter("sr"))
        assertEquals("115.829000,-31.965000,115.831000,-31.963000", u.queryParameter("mapExtent"))
        // cached: no new request for the same 1e-4 degree cell
        assertEquals(listOf(23.75), ga.sample(listOf(Pt(-319639950, 1158300050)))) // same 1e-4 degree cell
        assertEquals(2, srv.requestCount)
    }

    @Test fun openTopoBatches() {
        val pts = (0 until 150).map { Pt(-319640000 + it * 2000, 1158300000) }
        srv.enqueue(MockResponse().setBody("""{"results":[${(0 until 100).joinToString(",") { """{"elevation":${10 + it}}""" }}],"status":"OK"}"""))
        srv.enqueue(MockResponse().setBody("""{"results":[${(0 until 50).joinToString(",") { """{"elevation":null}""" }}],"status":"OK"}"""))
        val pauses = ArrayList<Long>()
        val ot = OpenTopoProvider(http, MemoryCache(), srv.url("/v1/srtm30m"), pause = { pauses += it })
        val got = ot.sample(pts)
        assertEquals(10.0, got[0]); assertEquals(109.0, got[99]); assertNull(got[100])
        assertEquals(listOf(1000L), pauses) // at most one request a second
        val locs = srv.takeRequest().requestUrl!!.queryParameter("locations")!!.split("|")
        assertEquals(100, locs.size)
        assertEquals("-31.964000,115.830000", locs[0])
    }

    @Test fun chainFillsGaps() {
        val a = ElevationProvider { p -> p.map { if (it.latE7 % 2 == 0) 1.0 else null } }
        val b = ElevationProvider { p -> p.map { 2.0 } }
        val boom = ElevationProvider { throw RuntimeException("down") }
        val got = ChainProvider(listOf(boom, a, b)).sample(listOf(Pt(2, 0), Pt(3, 0)))
        assertEquals(listOf(1.0, 2.0), got)
    }
}
