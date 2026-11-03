package cc700.testing

import cc700.link.LinkTransport
import cc700.link.Op
import cc700.link.Reassembler
import cc700.link.Status
import cc700.link.fragment
import cc700.route.decodeCr1
import cc700.util.ByteBuf
import cc700.util.ByteReader
import cc700.util.crc32
import cc700.util.hex
import cc700.util.objectId
import cc700.workout.decodeCw1
import java.util.concurrent.LinkedBlockingQueue
import java.util.concurrent.TimeUnit

/**
 * The unit's side of the protocol in Kotlin, for fast app tests (brief 7.1). It mirrors core/src/link.c:
 * same statuses, busy rules, chunk limits, EXISTS/FULL/TOO_BIG checks, req_id 0 ignored. Storage is in
 * memory. The shared ProtocolSuite runs the same tests against this and against simunit.
 */
class FakeUnit(override val mtu: Int = 247) : LinkTransport {
    class Ride(val id: Long, val data: ByteArray, var flags: Int, val durationS: Long, val distanceM: Long)

    var state = 0          // 0 ready, 1 recording, 2 paused
    var battery = 80
    var ftp = 200
    var utc = 0L; var tz = 0
    val workouts = LinkedHashMap<String, ByteArray>()
    val routes = LinkedHashMap<String, ByteArray>()
    val rides = sortedMapOf<Long, Ride>()
    var freeBytes = 1L shl 32

    private val out = LinkedBlockingQueue<ByteArray>()
    private val rx = Reassembler()
    private var putKind = 0; private var putId = ""; private var putSize = 0L; private var putCrc = 0L
    private var putBuf: ByteBuf? = null
    private var putLast = 0L

    override fun send(packet: ByteArray) {
        val m = rx.feed(packet, System.currentTimeMillis()) ?: return
        val r = handle(m) ?: return
        for (p in fragment(r, mtu - 4)) out.put(p)
    }

    override fun receive(timeoutMs: Long): ByteArray? = out.poll(timeoutMs, TimeUnit.MILLISECONDS)

    fun event(op: Int, payload: ByteArray) { for (p in fragment(byteArrayOf(op.toByte(), 0) + payload, mtu - 4)) out.put(p) }

    private fun handle(m: ByteArray): ByteArray? {
        if (m.size < 2) return null
        val op = m[0].toInt() and 0xFF
        val req = m[1].toInt() and 0xFF
        if (req == 0) return null
        val p = m.copyOfRange(2, m.size)
        val o = ByteBuf().u8(op or 0x80).u8(req)
        fun st(s: Int) = o.u8(s).toByteArray()
        val known = op in listOf(1, 2, 3, 4) || op in 0x10..0x14 || op in 0x20..0x24
        if (op >= 0x80 || !known) return st(Status.BAD_OPCODE)
        val busyOk = op == Op.HELLO || op == Op.FTP_GET || op == Op.TIME_SET || op == Op.OBJ_LIST
        if (state != 0 && !busyOk) return st(Status.BUSY)
        if (putBuf != null && System.currentTimeMillis() - putLast > 15000) putBuf = null
        if (putBuf != null) putLast = System.currentTimeMillis()
        val r = ByteReader(p)
        return when (op) {
            Op.HELLO -> o.u8(if (p.isNotEmpty() && p[0].toInt() == 1) 0 else Status.BAD_ARG).u8(1).u8(0).u8(1).u8(0)
                .u8(battery).u8(state).u8(20 - workouts.size).u8(10 - routes.size).toByteArray()
            Op.FTP_GET -> o.u8(0).u16(ftp).toByteArray()
            Op.FTP_SET -> {
                if (p.size < 2) return st(Status.BAD_ARG)
                val f = r.u16()
                if (f !in 50..600) st(Status.BAD_ARG) else { ftp = f; st(0) }
            }
            Op.TIME_SET -> if (p.size < 6) st(Status.BAD_ARG) else { utc = r.u32(); tz = r.i16(); st(0) }
            Op.OBJ_LIST -> {
                val kind = if (p.isNotEmpty()) p[0].toInt() else 0
                if (kind != 1 && kind != 2) return st(Status.BAD_ARG)
                val lib = if (kind == 1) workouts else routes
                o.u8(0).u8(lib.size)
                for (obj in lib.values) {
                    o.bytes(obj.copyOfRange(4, 12)).u32(obj.size.toLong())
                    if (kind == 1) {
                        val w = decodeCw1(obj)
                        o.u8(w.name.length).bytes(w.name.toByteArray()).u32(w.totalSecs.toLong()).u8(w.steps.size)
                    } else {
                        val ri = decodeCr1(obj)
                        o.u8(ri.name.length).bytes(ri.name.toByteArray()).u32(ri.distM).u16(ri.ascentM).u8(ri.climbs.size)
                    }
                }
                o.toByteArray()
            }
            Op.PUT_BEGIN -> {
                if (p.size < 17) return st(Status.BAD_ARG)
                val kind = r.u8(); val id = r.bytes(8).hex(); val size = r.u32(); val crc = r.u32()
                if (kind != 1 && kind != 2) return st(Status.BAD_ARG)
                putBuf = null
                if (size > (if (kind == 1) 4096 else 131072)) return st(Status.TOO_BIG)
                if (size < 12) return st(Status.BAD_ARG)
                val lib = if (kind == 1) workouts else routes
                if (id in lib) return st(Status.EXISTS)
                if (lib.size >= (if (kind == 1) 20 else 10) || freeBytes < size + 4096) return st(Status.FULL)
                putKind = kind; putId = id; putSize = size; putCrc = crc; putBuf = ByteBuf(size.toInt()); putLast = System.currentTimeMillis()
                st(0)
            }
            Op.PUT_DATA -> {
                val b = putBuf ?: return st(Status.NO_SESSION)
                if (p.size < 5 || p.size - 4 > 4000) return st(Status.BAD_ARG)
                val off = r.u32()
                if (off != b.size.toLong()) return o.u8(Status.OFFSET).u32(b.size.toLong()).toByteArray()
                if (off + p.size - 4 > putSize) return o.u8(Status.BAD_ARG).u32(b.size.toLong()).toByteArray()
                b.bytes(p.copyOfRange(4, p.size))
                o.u8(0).u32(b.size.toLong()).toByteArray()
            }
            Op.PUT_END -> {
                val b = putBuf ?: return st(Status.NO_SESSION)
                putBuf = null
                val obj = b.toByteArray()
                if (obj.size.toLong() != putSize) return st(Status.BAD_ARG)
                if (crc32(obj) != putCrc) return st(Status.BAD_CRC)
                val valid = runCatching {
                    if (putKind == 1) { decodeCw1(obj); require(objectId(obj).contentEquals(obj.copyOfRange(4, 12))) }
                    else decodeCr1(obj)
                    require(obj.copyOfRange(4, 12).hex() == putId)
                }.isSuccess
                if (!valid) return st(Status.BAD_OBJECT)
                (if (putKind == 1) workouts else routes)[putId] = obj
                st(0)
            }
            Op.OBJ_DELETE -> {
                if (p.size < 9) return st(Status.BAD_ARG)
                val kind = r.u8(); val id = r.bytes(8).hex()
                if (kind != 1 && kind != 2) return st(Status.BAD_ARG)
                if ((if (kind == 1) workouts else routes).remove(id) == null) st(Status.NOT_FOUND) else st(0)
            }
            Op.RIDE_LIST -> {
                o.u8(0).u8(minOf(rides.size, 240))
                for (ride in rides.values.toList().takeLast(240))
                    o.u32(ride.id).u32(ride.data.size.toLong()).u8(ride.flags).u32(ride.durationS).u32(ride.distanceM)
                o.toByteArray()
            }
            Op.RIDE_OPEN -> {
                if (p.size < 4) return st(Status.BAD_ARG)
                val ride = rides[r.u32()] ?: return st(Status.NOT_FOUND)
                o.u8(0).u32(ride.data.size.toLong()).u32(crc32(ride.data)).toByteArray()
            }
            Op.RIDE_READ -> {
                if (p.size < 10) return st(Status.BAD_ARG)
                val id = r.u32(); val off = r.u32(); val n = r.u16()
                if (n !in 1..4000) return st(Status.BAD_ARG)
                val ride = rides[id] ?: return st(Status.NOT_FOUND)
                val s = minOf(off, ride.data.size.toLong()).toInt()
                o.u8(0).bytes(ride.data.copyOfRange(s, minOf(ride.data.size, s + n))).toByteArray()
            }
            Op.RIDE_MARK -> {
                if (p.size < 5) return st(Status.BAD_ARG)
                val ride = rides[r.u32()] ?: return st(Status.NOT_FOUND)
                ride.flags = if (r.u8() and 1 == 1) ride.flags or 1 else ride.flags and 1.inv()
                st(0)
            }
            Op.RIDE_DELETE -> {
                if (p.size < 4) return st(Status.BAD_ARG)
                if (rides.remove(r.u32()) == null) st(Status.NOT_FOUND) else st(0)
            }
            else -> st(Status.BAD_OPCODE)
        }
    }
}
