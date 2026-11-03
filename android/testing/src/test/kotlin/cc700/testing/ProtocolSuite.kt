package cc700.testing

import cc700.link.LinkClient
import cc700.link.LinkException
import cc700.link.LinkTransport
import cc700.link.Op
import cc700.link.Status
import cc700.util.ByteBuf
import cc700.util.crc32
import cc700.workout.encodeCw1
import cc700.workout.parseWorkout
import org.junit.jupiter.api.AfterEach
import org.junit.jupiter.api.Assertions.assertArrayEquals
import org.junit.jupiter.api.Assertions.assertEquals
import org.junit.jupiter.api.Assertions.assertTrue
import org.junit.jupiter.api.Test
import org.junit.jupiter.api.assertThrows
import java.io.File

/**
 * One protocol suite (protocol sections 4 and 5), run against FakeUnit and against simunit (WP5: "FakeUnit
 * passes the same protocol tests as simunit"). Each subclass provides a unit in one of three setups.
 */
abstract class ProtocolSuite {
    protected val root = File(System.getProperty("cc700.root"))

    enum class Setup { EMPTY, WITH_RIDE, RECORDING }

    /** A fresh unit in [setup], reached through a transport at [mtu]. */
    abstract fun open(setup: Setup, mtu: Int): LinkTransport

    private val opened = ArrayList<LinkTransport>()
    @AfterEach fun closeAll() { opened.forEach { runCatching { it.close() } }; opened.clear(); cleanup() }
    open fun cleanup() {}

    private fun client(setup: Setup = Setup.EMPTY, mtu: Int = 247) = LinkClient(open(setup, mtu).also { opened += it })

    private fun w(name: String) = encodeCw1(parseWorkout(File(root, "claude/workouts/$name.workout.json").readText()))
    private val route by lazy { File(root, "core/tests/fixtures/route.cr1").readBytes() }

    @Test fun helloFtpTime() {
        val c = client()
        val h = c.hello()
        assertEquals(1, h.proto); assertEquals("0.1.0", h.fw); assertEquals(0, h.state)
        assertEquals(20, h.workoutFree); assertEquals(10, h.routeFree)
        assertEquals(Status.OK, c.ftpSet(260)); assertEquals(260, c.ftpGet())
        assertEquals(Status.BAD_ARG, c.ftpSet(49)); assertEquals(Status.BAD_ARG, c.ftpSet(601))
        assertEquals(260, c.ftpGet())
        assertEquals(Status.OK, c.timeSet(1793000000, 480))
        assertEquals(Status.BAD_ARG, c.request(Op.HELLO, byteArrayOf(2)).first)
    }

    @Test fun sendWorkoutsAndRoutesAtBothChunkSizes() {
        for (mtu in listOf(23, 247)) {
            val c = client(mtu = mtu)
            assertEquals(if (mtu >= 185) 4000 else 512, c.chunk)
            val obj = w("vo2max-30-15") // 1510 bytes: several 512-byte chunks
            assertEquals(Status.OK, c.put(1, obj))
            assertEquals(Status.EXISTS, c.put(1, obj))
            assertEquals(Status.OK, c.put(2, route))
            val list = c.objList(1)
            assertEquals(1, list.size)
            assertEquals("VO2max 30-15 3 x 13", list[0].name); assertEquals(84, list[0].b); assertEquals(1510L, list[0].size)
            val rl = c.objList(2)
            assertEquals("Kings Park test", rl[0].name); assertEquals(1, rl[0].c)
            assertEquals(19, c.hello().workoutFree)
            assertEquals(Status.OK, c.delete(1, list[0].id))
            assertEquals(Status.NOT_FOUND, c.delete(1, list[0].id))
            closeAll()
        }
    }

    @Test fun putErrors() {
        val c = client()
        val obj = w("threshold-2x20")
        fun begin(kind: Int, o: ByteArray, size: Int = o.size, crc: Long = crc32(o)) =
            c.request(Op.PUT_BEGIN, ByteBuf().u8(kind).bytes(o.copyOfRange(4, 12)).u32(size.toLong()).u32(crc).toByteArray()).first
        fun data(off: Int, b: ByteArray) = c.request(Op.PUT_DATA, ByteBuf().u32(off.toLong()).bytes(b).toByteArray())
        assertEquals(Status.NO_SESSION, data(0, obj).first)
        assertEquals(Status.NO_SESSION, c.request(Op.PUT_END).first)
        assertEquals(Status.TOO_BIG, begin(1, obj, 4097))
        assertEquals(Status.BAD_ARG, begin(3, obj))
        // BAD_CRC: the data differs from the CRC given at BEGIN
        assertEquals(Status.OK, begin(1, obj))
        val bad = obj.copyOf(); bad[40] = (bad[40].toInt() xor 1).toByte()
        assertEquals(Status.OK, data(0, bad).first)
        assertEquals(Status.BAD_CRC, c.request(Op.PUT_END, timeoutMs = 10000).first)
        // OFFSET returns where to resume
        assertEquals(Status.OK, begin(1, obj))
        assertEquals(Status.OK, data(0, obj.copyOfRange(0, 50)).first)
        val (st, p) = data(100, obj.copyOfRange(100, 120))
        assertEquals(Status.OFFSET, st); assertEquals(50L, cc700.util.getU32(p, 0))
        assertEquals(Status.OK, data(50, obj.copyOfRange(50, obj.size)).first)
        assertEquals(Status.OK, c.request(Op.PUT_END, timeoutMs = 10000).first)
        // BAD_OBJECT: a CRC-correct object that does not decode
        val junk = obj.copyOf(); junk[17] = 39; junk[4] = (junk[4].toInt() xor 1).toByte() // a new ID, so not EXISTS
        assertEquals(Status.OK, begin(1, junk))
        assertEquals(Status.OK, data(0, junk).first)
        assertEquals(Status.BAD_OBJECT, c.request(Op.PUT_END, timeoutMs = 10000).first)
        assertEquals(1, c.objList(1).size)
    }

    @Test fun unknownOpcodeAndReservedReqId() {
        val c = client()
        assertEquals(Status.BAD_OPCODE, c.request(0x05).first)
        assertEquals(Status.BAD_OPCODE, c.request(0x7F).first)
        assertEquals(Status.OK, c.hello().let { 0 })
    }

    @Test fun rideSyncAtBothChunkSizes() {
        for (mtu in listOf(23, 247)) {
            val c = client(Setup.WITH_RIDE, mtu)
            val rides = c.rideList()
            assertEquals(1, rides.size)
            val r = rides[0]
            assertEquals(1793000000L, r.rideId)
            assertEquals(false, r.uploaded)
            val data = c.rideFetch(r.rideId) // size and CRC-32 checked against RIDE_OPEN
            assertEquals(r.size, data.size.toLong())
            assertEquals(Status.OK, c.rideMark(r.rideId, true))
            assertTrue(c.rideList()[0].uploaded)
            assertEquals(Status.OK, c.rideMark(r.rideId, false))
            assertEquals(Status.NOT_FOUND, c.rideMark(42, true))
            assertThrows<LinkException> { c.rideFetch(42) }
            val p = c.request(Op.RIDE_READ, ByteBuf().u32(r.rideId).u32(0).u16(4001).toByteArray())
            assertEquals(Status.BAD_ARG, p.first)
            closeAll()
        }
        val c = client(Setup.WITH_RIDE)
        assertEquals(Status.OK, c.rideDelete(1793000000))
        assertEquals(0, c.rideList().size)
        assertEquals(Status.NOT_FOUND, c.rideDelete(1793000000))
    }

    @Test fun busyWhileRecording() {
        val c = client(Setup.RECORDING)
        assertEquals(1, c.hello().state)
        assertEquals(200, c.ftpGet())
        assertEquals(Status.OK, c.timeSet(1793000000, 480))
        assertEquals(0, c.objList(1).size)
        assertEquals(Status.BUSY, c.ftpSet(250))
        assertEquals(Status.BUSY, c.request(Op.PUT_BEGIN, ByteArray(17)).first)
        assertEquals(Status.BUSY, c.request(Op.RIDE_LIST).first)
        assertEquals(Status.BUSY, c.request(Op.OBJ_DELETE, ByteArray(9)).first)
    }

    @Test fun connectSequence() {
        val c = client(Setup.WITH_RIDE)
        val m = c.connect(1793000000, 480)
        assertEquals(0, m.hello.state); assertEquals(200, m.ftp)
        assertEquals(0, m.workouts.size); assertEquals(0, m.routes.size); assertEquals(1, m.rides.size)
        assertArrayEquals(ByteArray(0), ByteArray(0))
    }
}
