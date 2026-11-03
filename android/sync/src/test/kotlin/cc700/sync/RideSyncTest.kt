package cc700.sync

import cc700.link.LinkTransport
import cc700.testing.FakeUnit
import cc700.upload.IntervalsClient
import cc700.util.ByteBuf
import okhttp3.OkHttpClient
import okhttp3.mockwebserver.MockResponse
import okhttp3.mockwebserver.MockWebServer
import org.junit.jupiter.api.AfterEach
import org.junit.jupiter.api.Assertions.assertEquals
import org.junit.jupiter.api.Assertions.assertFalse
import org.junit.jupiter.api.Assertions.assertTrue
import org.junit.jupiter.api.BeforeEach
import org.junit.jupiter.api.Test

/** The upload queue's error paths (brief 7.6), against FakeUnit and MockWebServer. */
class RideSyncTest {
    private lateinit var server: MockWebServer
    private val fake = FakeUnit()
    private var key: String? = "test-key-not-real"
    private lateinit var session: UnitSession
    private lateinit var store: MemoryRideStore
    private lateinit var sync: RideSync

    private class FakeConnector(val unit: FakeUnit) : Connector {
        var reachable = true
        override fun scan(timeoutMs: Long) = listOf(FoundUnit("CC700-FAKE", "fake"))
        override fun pair(unit: FoundUnit, passkey: () -> String?): LinkTransport = connect(unit.address)
        override fun connect(address: String): LinkTransport {
            if (!reachable) throw java.io.IOException("out of range")
            return object : LinkTransport by unit { override fun close() {} }
        }
        override fun forget(address: String) {}
    }

    private val connector = FakeConnector(fake)

    @BeforeEach fun setUp() {
        server = MockWebServer().apply { start() }
        session = UnitSession(connector, MemoryPairingStore(PairedUnit("CC700-FAKE", "fake")), nowS = { T0 }, tzMin = { 480 })
        store = MemoryRideStore()
        sync = RideSync(session, store, MemoryBlobStore()) {
            key?.let { k -> IntervalsClient(OkHttpClient(), { k }, server.url("/")) }
        }
    }

    @AfterEach fun tearDown() = server.shutdown()

    private fun addRide(start: Long, flags: Int = 0) {
        fake.rides[start] = FakeUnit.Ride(start, tinyFit(start), flags, 600, 5000)
    }

    private fun empty() = MockResponse().setBody("[]")
    private fun state(id: Long) = sync.rides.value.single { it.rideId == id }

    @Test fun uploadedAndMarked() {
        addRide(T0)
        session.connect()
        assertEquals(1, sync.fetchNew())
        assertEquals(0, sync.fetchNew()) // already held
        server.enqueue(empty()); server.enqueue(MockResponse().setResponseCode(200).setBody("""{"id":"i9"}"""))
        assertEquals(1, sync.uploadPending().uploaded)
        assertEquals(RideState.UPLOADED, state(T0).state)
        assertTrue(fake.rides[T0]!!.flags and 1 != 0)
        assertEquals(0, sync.fetchNew())
    }

    @Test fun duplicateIsMarkedWithoutSending() {
        addRide(T0)
        session.connect(); sync.fetchNew()
        server.enqueue(MockResponse().setBody("""[{"id":"i5","start_date":"${java.time.Instant.ofEpochSecond(T0 + 40)}"}]"""))
        val p = sync.uploadPending()
        assertEquals(1, p.duplicates)
        assertEquals(1, server.requestCount) // no POST
        assertEquals(RideState.UPLOADED, state(T0).state)
        assertEquals("i5", state(T0).activityId)
        assertTrue(fake.rides[T0]!!.flags and 1 != 0)
    }

    @Test fun unauthorizedStopsThePassAndWaitsForAKey() {
        addRide(T0); addRide(T0 + 7200)
        session.connect(); sync.fetchNew()
        server.enqueue(MockResponse().setResponseCode(401))
        val p = sync.uploadPending()
        assertTrue(p.needsKey)
        assertEquals(1, server.requestCount)
        assertTrue(sync.rides.value.all { it.state == RideState.NEEDS_KEY })
        sync.keyChanged()
        assertTrue(sync.rides.value.all { it.state == RideState.WAITING })
    }

    @Test fun noKeyAsksForOne() {
        key = null
        addRide(T0)
        session.connect(); sync.fetchNew()
        assertTrue(sync.uploadPending().needsKey)
        assertEquals(RideState.NEEDS_KEY, state(T0).state)
        assertEquals(0, server.requestCount)
    }

    @Test fun serverErrorsRetryLater() {
        addRide(T0)
        session.connect(); sync.fetchNew()
        server.enqueue(empty()); server.enqueue(MockResponse().setResponseCode(503))
        val p = sync.uploadPending()
        assertTrue(p.retryLater)
        assertEquals(RideState.WAITING, state(T0).state)
        assertEquals("Will retry (HTTP 503)", state(T0).message)
        server.enqueue(MockResponse().setResponseCode(429))
        assertTrue(sync.uploadPending().retryLater)
        assertEquals(2, state(T0).attempts)
        assertFalse(fake.rides[T0]!!.flags and 1 != 0)
    }

    @Test fun otherClientErrorsFailWithTheMessage() {
        addRide(T0)
        session.connect(); sync.fetchNew()
        server.enqueue(empty()); server.enqueue(MockResponse().setResponseCode(422).setBody("""{"error":"File is not a FIT file"}"""))
        assertEquals(1, sync.uploadPending().failed)
        assertEquals(RideState.FAILED, state(T0).state)
        assertEquals("File is not a FIT file", state(T0).message)
        sync.retry(T0)
        assertEquals(RideState.WAITING, state(T0).state)
    }

    @Test fun uploadWhileTheUnitIsAwayIsMarkedOnTheNextConnect() {
        addRide(T0)
        session.onConnected = { sync.onConnected() }
        session.connect()
        session.disconnect()
        server.enqueue(empty()); server.enqueue(MockResponse().setBody("""{"id":"i1"}"""))
        assertEquals(1, sync.uploadPending().uploaded)
        assertFalse(state(T0).markedOnUnit)
        assertFalse(fake.rides[T0]!!.flags and 1 != 0)
        session.connect()
        assertTrue(state(T0).markedOnUnit)
        assertTrue(fake.rides[T0]!!.flags and 1 != 0)
    }

    @Test fun timeUnknownWaitsForADate() {
        val provisional = 946684800L + 1234
        addRide(provisional, flags = 4)
        session.connect(); sync.fetchNew()
        assertEquals(RideState.NEEDS_DATE, state(provisional).state)
        assertEquals(0, sync.uploadPending().uploaded)
        assertEquals(0, server.requestCount)
        sync.setStart(provisional, T0)
        assertEquals(T0 - provisional, state(provisional).timeOffsetS)
        val bytes = sync.uploadBytes(state(provisional))!!
        assertEquals(T0, fitStartUtc(bytes))
        assertTrue(fitValid(bytes))
        server.enqueue(empty()); server.enqueue(MockResponse().setBody("""{"id":"i2"}"""))
        assertEquals(1, sync.uploadPending().uploaded)
        val get = server.takeRequest()
        assertTrue(get.path!!.contains("oldest=2026-10-25"), get.path) // around the corrected day, not 2000
    }

    @Test fun refusedRequestsKeepTheLink() {
        session.connect()
        fake.state = 1 // recording
        val e = runCatching { session.setFtp(260) }.exceptionOrNull()
        assertTrue(e is UnitRefused && e.status == cc700.link.Status.BUSY, "$e")
        assertTrue(session.connected)
        assertTrue(runCatching { session.setFtp(20) }.exceptionOrNull() is IllegalArgumentException)
    }

    @Test fun lostLinkSetsDisconnected() {
        connector.reachable = false
        val s = session.connect()
        assertEquals(LinkState.DISCONNECTED, s.state)
        assertEquals("out of range", s.error)
    }

    companion object {
        const val T0 = 1793000000L

        /** A minimal valid FIT file: file_id with time_created, one record. */
        fun tinyFit(startUtc: Long): ByteArray {
            val t = startUtc - FIT_EPOCH
            val d = ByteBuf()
            d.u8(0x40).u8(0).u8(0).u16(0).u8(2).u8(4).u8(4).u8(0x86).u8(0).u8(1).u8(0x00)
            d.u8(0x00).u32(t).u8(4)
            d.u8(0x41).u8(0).u8(0).u16(20).u8(2).u8(253).u8(4).u8(0x86).u8(7).u8(2).u8(0x84)
            d.u8(0x01).u32(t + 1).u16(200)
            val body = d.toByteArray()
            val h = ByteBuf().u8(14).u8(0x20).u16(2132).u32(body.size.toLong()).bytes(".FIT".toByteArray()).toByteArray()
            val hc = fitCrc16(h, 0, 12)
            val head = ByteBuf().bytes(h).u16(hc).toByteArray()
            val all = head + body
            return all + ByteBuf().u16(fitCrc16(all)).toByteArray()
        }
    }
}
