package cc700.sync

import cc700.dem.MemoryCache
import cc700.dem.OpenTopoProvider
import cc700.route.decodeCr1
import cc700.upload.IntervalsClient
import okhttp3.OkHttpClient
import okhttp3.mockwebserver.Dispatcher
import okhttp3.mockwebserver.MockResponse
import okhttp3.mockwebserver.MockWebServer
import okhttp3.mockwebserver.RecordedRequest
import okio.Buffer
import org.junit.jupiter.api.AfterEach
import org.junit.jupiter.api.Assertions.assertEquals
import org.junit.jupiter.api.Assertions.assertFalse
import org.junit.jupiter.api.Assertions.assertNotNull
import org.junit.jupiter.api.Assertions.assertNull
import org.junit.jupiter.api.Assertions.assertTrue
import org.junit.jupiter.api.Test
import org.junit.jupiter.api.io.TempDir
import java.io.File
import java.time.LocalDateTime
import java.time.ZoneOffset
import java.util.Collections

/**
 * The WP6 done-condition flow, driven through the same classes the app's screens call, against the real
 * C core in simunit over SocketTransport, with Intervals.icu and the elevation service on MockWebServer:
 * pair (passkey simulated), read FTP, set FTP, import W1 and W2, send them, see them in the library,
 * delete one, import a GPX route, see its climbs, send it, list a ride, upload it, and handle a ride
 * flagged "time unknown".
 */
class SimunitAppFlowTest {
    private val procs = ArrayList<Process>()
    private val servers = ArrayList<MockWebServer>()

    @AfterEach fun stop() {
        procs.forEach { it.destroy(); it.waitFor() }
        servers.forEach { it.shutdown() }
    }

    private fun res(name: String) = javaClass.getResource("/$name")!!.readText()

    /** Intervals.icu: no activities yet; every upload is accepted. Uploaded FIT files are kept. */
    private class Intervals : Dispatcher() {
        val uploads: MutableList<ByteArray> = Collections.synchronizedList(ArrayList())
        val auth: MutableList<String?> = Collections.synchronizedList(ArrayList())
        override fun dispatch(request: RecordedRequest): MockResponse {
            auth += request.getHeader("Authorization")
            return when {
                request.method == "GET" && request.path!!.startsWith("/api/v1/athlete/0/activities") ->
                    MockResponse().setBody("[]")
                request.method == "POST" && request.path == "/api/v1/athlete/0/activities" -> {
                    uploads += fitPart(request.body.clone())
                    MockResponse().setResponseCode(201).setBody("""{"id":"i${uploads.size}"}""")
                }
                else -> MockResponse().setResponseCode(404)
            }
        }

        /** The file part of the multipart body. */
        private fun fitPart(b: Buffer): ByteArray {
            val all = b.readByteArray()
            val s = String(all, Charsets.ISO_8859_1)
            val head = s.indexOf("\r\n\r\n", s.indexOf("filename=")) + 4
            val end = s.indexOf("\r\n--", head)
            return all.copyOfRange(head, end)
        }
    }

    /** Open Topo Data: a 40 m hill, up between 115.834 and 115.8395 degrees east, down again between
     * 115.8427 and 115.8459 (the route's north leg is on the top). */
    private class Topo : Dispatcher() {
        override fun dispatch(request: RecordedRequest): MockResponse {
            val locs = request.requestUrl!!.queryParameter("locations")!!.split("|")
            val body = locs.joinToString(",", "{\"results\":[", "]}") { l ->
                val lon = l.split(",")[1].toDouble()
                val up = ((lon - 115.834) / 0.0055).coerceIn(0.0, 1.0)
                val down = ((lon - 115.8427) / 0.0032).coerceIn(0.0, 1.0)
                val e = 20.0 + 40.0 * (up - down)
                """{"elevation":$e}"""
            }
            return MockResponse().setBody(body)
        }
    }

    @Test fun fullFlow(@TempDir dir: File) {
        Sim.need()
        val unitRoot = File(dir, "CC700")
        Sim.recordRide(unitRoot, Sim.UTC)   // a normal ride
        Sim.recordRide(unitRoot, null)      // a ride without UTC: "time unknown"
        val port = Sim.freePort()
        procs += Sim.serve(unitRoot, port)

        val intervals = Intervals()
        val iv = MockWebServer().apply { dispatcher = intervals; start() }.also { servers += it }
        val topo = MockWebServer().apply { dispatcher = Topo(); start() }.also { servers += it }
        val http = OkHttpClient()

        // ---- pairing (simulated passkey)
        val connector = SimulatedConnector("127.0.0.1", port)
        val pairing = MemoryPairingStore()
        val session = UnitSession(connector, pairing, nowS = { Sim.UTC + 3600 }, tzMin = { 480 })
        val rides = RideSync(session, MemoryRideStore(), MemoryBlobStore()) {
            IntervalsClient(http, { "test-key-not-real" }, iv.url("/"))
        }
        session.onConnected = { rides.onConnected() }
        assertEquals(LinkState.NOT_PAIRED, session.status.value.state)
        val found = waitFor { session.scan().firstOrNull() }
        assertEquals("CC700-SIM", found.name)
        val wrong = session.pair(found) { "000000" }
        assertEquals(LinkState.NOT_PAIRED, wrong.state)
        assertTrue(wrong.error!!.contains("wrong passkey"))
        assertNull(pairing.load())
        val ok = session.pair(found) { connector.passkey }
        assertEquals(LinkState.CONNECTED, ok.state, ok.error)
        assertEquals(PairedUnit("CC700-SIM", "127.0.0.1:$port"), pairing.load())
        assertEquals(1, ok.hello!!.proto)

        // ---- FTP: read on connect, then set
        assertEquals(200, session.status.value.ftp)
        assertEquals(250, session.setFtp(250))
        assertEquals(250, session.refresh().ftp)

        // ---- workouts: import W1 and W2, send, see in the library, delete one
        val lib = MemoryLibraryStore()
        val w1 = importWorkout(res("w1.json")) as WorkoutImport.Ok
        val w2 = importWorkout(res("w2.json")) as WorkoutImport.Ok
        lib.putWorkout(w1.toLocal()); lib.putWorkout(w2.toLocal())
        assertEquals(2, lib.workouts().size)
        val prev = previewSteps(w1.workout, 250)
        assertEquals(w1.workout.steps.size, prev.size)
        assertEquals(125, prev.first().lowW)          // warm-up ramp 50 % of 250 W
        assertEquals(0, session.send(1, w1.cw1))
        assertEquals(0, session.send(1, w2.cw1))
        assertEquals(cc700.link.Status.EXISTS, session.send(1, w1.cw1))   // identical workouts dedupe
        val onUnit = session.status.value.workouts
        assertEquals(setOf(w1.workout.name, w2.workout.name), onUnit.map { it.name }.toSet())
        val w1Info = onUnit.first { it.name == w1.workout.name }
        assertEquals(w1.workout.totalSecs.toLong(), w1Info.a)
        assertEquals(w1.workout.steps.size, w1Info.b)
        session.delete(1, w1Info.id)
        assertEquals(listOf(w2.workout.name), session.status.value.workouts.map { it.name })

        // ---- route: import GPX with elevations from the (mock) service, see its climbs, send it
        val elev = OpenTopoProvider(http, MemoryCache(), topo.url("/v1/srtm30m"), pause = {})
        val imp = File(Sim.fixtures, "route.gpx").inputStream().use {
            importRoute(it, "route", elev, ClimbThresholds().params())
        } as RouteImport.Ok
        assertFalse(imp.route.fromFile)
        assertEquals(1, imp.route.climbs.size, "climbs: ${imp.route.climbs}")
        val climb = imp.route.climbs.single()
        assertTrue(climb.ascentM in 35..45, "ascent ${climb.ascentM}")
        assertEquals(imp.route.climbs, decodeCr1(imp.route.obj.bytes).climbs)
        lib.putRoute(imp.toLocal())
        assertEquals(0, session.send(2, imp.route.obj.bytes))
        val r = session.status.value.routes.single()
        assertEquals(imp.name, r.name)
        assertEquals(1, r.c)  // nclimbs

        // ---- rides: listed and fetched on connect; the normal one uploads, the other waits for a date
        val list = session.rideList()
        assertEquals(2, list.size)
        val known = list.single { !it.timeUnknown }
        val unknown = list.single { it.timeUnknown }
        rides.fetchNew()
        assertEquals(RideState.WAITING, rides.rides.value.single { it.rideId == known.rideId }.state)
        assertEquals(RideState.NEEDS_DATE, rides.rides.value.single { it.rideId == unknown.rideId }.state)

        val pass = rides.uploadPending()
        assertEquals(1, pass.uploaded)
        assertEquals(1, intervals.uploads.size)
        assertTrue(fitValid(intervals.uploads[0]))
        assertEquals(known.rideId, fitStartUtc(intervals.uploads[0]))
        assertTrue(intervals.auth.all { it != null && it.startsWith("Basic ") })
        val up = rides.rides.value.single { it.rideId == known.rideId }
        assertEquals(RideState.UPLOADED, up.state)
        assertEquals("i1", up.activityId)
        assertTrue(up.markedOnUnit)
        assertTrue(session.rideList().single { it.rideId == known.rideId }.uploaded)
        assertFalse(session.rideList().single { it.rideId == unknown.rideId }.uploaded)

        // ---- the "time unknown" ride: Daniel enters 1 November 2026 07:30 Perth time
        val start = LocalDateTime.of(2026, 11, 1, 7, 30).toEpochSecond(ZoneOffset.ofHours(8))
        rides.setStart(unknown.rideId, start)
        assertEquals(RideState.WAITING, rides.rides.value.single { it.rideId == unknown.rideId }.state)
        assertEquals(1, rides.uploadPending().uploaded)
        assertEquals(2, intervals.uploads.size)
        val shifted = intervals.uploads[1]
        assertTrue(fitValid(shifted))
        assertEquals(start, fitStartUtc(shifted))
        assertTrue(fitTimeFields(shifted).filter { it != 0xFFFFFFFFL }.all { it + FIT_EPOCH >= start })
        assertTrue(session.rideList().all { it.uploaded })
        assertNotNull(rides.rides.value.single { it.rideId == unknown.rideId }.activityId)

        // ---- forget
        session.forget()
        assertEquals(LinkState.NOT_PAIRED, session.status.value.state)
        assertNull(pairing.load())
    }

    private fun <T> waitFor(f: () -> T?): T {
        repeat(100) { f()?.let { return it }; Thread.sleep(50) }
        error("timed out")
    }
}
