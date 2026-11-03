package cc700.link

import cc700.util.ByteBuf
import cc700.util.ByteReader
import cc700.util.crc32

/** Protocol section 2.6 status codes. */
object Status {
    const val OK = 0; const val BAD_CRC = 1; const val FULL = 2; const val BUSY = 3; const val BAD_OBJECT = 4
    const val BAD_OPCODE = 5; const val BAD_ARG = 6; const val NOT_FOUND = 7; const val TOO_BIG = 8; const val EXISTS = 9
    const val NO_SESSION = 10; const val OFFSET = 11; const val IO_ERROR = 12
}

object Op {
    const val HELLO = 0x01; const val FTP_GET = 0x02; const val FTP_SET = 0x03; const val TIME_SET = 0x04
    const val OBJ_LIST = 0x10; const val PUT_BEGIN = 0x11; const val PUT_DATA = 0x12; const val PUT_END = 0x13
    const val OBJ_DELETE = 0x14
    const val RIDE_LIST = 0x20; const val RIDE_OPEN = 0x21; const val RIDE_READ = 0x22; const val RIDE_MARK = 0x23
    const val RIDE_DELETE = 0x24
    const val EV_STATE = 0xA0; const val EV_RIDE_SAVED = 0xA1
}

// ------------------------------------------------------------------ framing (protocol 2.5, V§5)
/** Splits a message into packets of header byte plus at most [dataCap] bytes (ATT_MTU - 4). */
fun fragment(msg: ByteArray, dataCap: Int): List<ByteArray> {
    val n = if (msg.isEmpty()) 1 else (msg.size + dataCap - 1) / dataCap
    return (0 until n).map { i ->
        val s = i * dataCap
        val e = minOf(msg.size, s + dataCap)
        val h = (if (i == 0) 0x80 else 0) or (if (i == n - 1) 0x40 else 0) or (i and 0x3F)
        byteArrayOf(h.toByte()) + msg.copyOfRange(s, e)
    }
}

/** Reassembles packets; drops a partial message on a counter mismatch, a packet without START when
 * idle, more than 4096 bytes or 2 s of silence. */
class Reassembler(private val maxLen: Int = 4096, private val gapMs: Long = 2000) {
    private val buf = ByteBuf(4096)
    private var active = false
    private var next = 0
    private var last = 0L
    private var parts = ArrayList<ByteArray>()

    fun feed(pkt: ByteArray, nowMs: Long): ByteArray? {
        if (pkt.isEmpty()) return null
        val h = pkt[0].toInt() and 0xFF
        if (active && nowMs - last > gapMs) active = false
        if (h and 0x80 != 0) { active = true; parts = ArrayList(); next = 0 }
        else if (!active) return null
        val total = parts.sumOf { it.size } + pkt.size - 1
        if ((h and 0x3F) != next || total > maxLen) { active = false; return null }
        parts += pkt.copyOfRange(1, pkt.size)
        next = (next + 1) and 0x3F
        last = nowMs
        if (h and 0x40 != 0) {
            active = false
            val out = ByteArray(total)
            var o = 0
            for (p in parts) { p.copyInto(out, o); o += p.size }
            return out
        }
        return null
    }
}

// ------------------------------------------------------------------ transport
/** One BLE link (or simunit's TCP framing, or FakeUnit). Packets in and out; [mtu] is the ATT MTU. */
interface LinkTransport : AutoCloseable {
    val mtu: Int
    fun send(packet: ByteArray)
    /** Next packet from the unit, or null after [timeoutMs]. */
    fun receive(timeoutMs: Long): ByteArray?
    override fun close() {}
}

class LinkException(msg: String, val status: Int = -1) : Exception(msg)

data class Hello(val proto: Int, val fw: String, val battery: Int, val state: Int, val workoutFree: Int, val routeFree: Int)
data class ObjInfo(val kind: Int, val id: ByteArray, val size: Long, val name: String, val a: Long, val b: Int, val c: Int)
data class RideInfo(val rideId: Long, val size: Long, val flags: Int, val durationS: Long, val distanceM: Long) {
    val uploaded get() = flags and 1 != 0
    val recovered get() = flags and 2 != 0
    val timeUnknown get() = flags and 4 != 0
}

/** The phone's side of the protocol (sections 4 and 5): one message in flight, 3 s timeout (10 s for
 * PUT_END and RIDE_OPEN). Events (req_id 0) go to [onEvent]. Not thread safe: one caller at a time. */
class LinkClient(private val t: LinkTransport, private val clock: () -> Long = System::currentTimeMillis) {
    var onEvent: (op: Int, payload: ByteArray) -> Unit = { _, _ -> }
    private val rx = Reassembler()
    private var req = 0
    private val dataCap get() = t.mtu - 4
    /** Chunk size for PUT_DATA and RIDE_READ: 4000 at an MTU of 185 or more, else 512 (brief 7.1). */
    val chunk get() = if (t.mtu >= 185) 4000 else 512

    /** Sends a request and returns (status, payload after the status). */
    fun request(op: Int, payload: ByteArray = ByteArray(0), timeoutMs: Long = 3000): Pair<Int, ByteArray> {
        req = req % 255 + 1
        val id = req
        for (p in fragment(byteArrayOf(op.toByte(), id.toByte()) + payload, dataCap)) t.send(p)
        val deadline = clock() + timeoutMs
        while (true) {
            val left = deadline - clock()
            if (left <= 0) throw LinkException("timeout waiting for 0x%02x".format(op))
            val pkt = t.receive(left) ?: continue
            val m = rx.feed(pkt, clock()) ?: continue
            if (m.size < 2) continue
            val mop = m[0].toInt() and 0xFF
            val mid = m[1].toInt() and 0xFF
            if (mid == 0 && mop >= 0xA0) { onEvent(mop, m.copyOfRange(2, m.size)); continue } /* E7 */
            if (mop != (op or 0x80) || mid != id) continue /* a late answer to an earlier request */
            if (m.size < 3) throw LinkException("short response")
            return (m[2].toInt() and 0xFF) to m.copyOfRange(3, m.size)
        }
    }

    private fun ok(r: Pair<Int, ByteArray>, what: String): ByteArray {
        if (r.first != Status.OK) throw LinkException("$what failed: status ${r.first}", r.first)
        return r.second
    }

    fun hello(): Hello {
        val (st, p) = request(Op.HELLO, byteArrayOf(1))
        if (p.size < 8) throw LinkException("short HELLO")
        if (st != Status.OK) throw LinkException("the unit speaks protocol ${p[0]}", st)
        return Hello(p[0].toInt(), "${p[1]}.${p[2]}.${p[3]}", p[4].toInt() and 0xFF, p[5].toInt(), p[6].toInt(), p[7].toInt())
    }

    fun ftpGet(): Int = ByteReader(ok(request(Op.FTP_GET), "FTP_GET")).u16()
    fun ftpSet(w: Int): Int = request(Op.FTP_SET, ByteBuf().u16(w).toByteArray()).first
    fun timeSet(unix: Long, tzMin: Int): Int = request(Op.TIME_SET, ByteBuf().u32(unix).u16(tzMin and 0xFFFF).toByteArray()).first

    fun objList(kind: Int): List<ObjInfo> {
        val r = ByteReader(ok(request(Op.OBJ_LIST, byteArrayOf(kind.toByte())), "OBJ_LIST"))
        val n = r.u8()
        return (0 until n).map {
            val id = r.bytes(8); val size = r.u32(); val name = String(r.bytes(r.u8()), Charsets.US_ASCII)
            if (kind == 1) ObjInfo(kind, id, size, name, r.u32(), r.u8(), 0)
            else ObjInfo(kind, id, size, name, r.u32(), r.u16(), r.u8())
        }
    }

    /** Sends an object (protocol 5). Returns OK, or EXISTS when it is already on the unit. */
    fun put(kind: Int, obj: ByteArray): Int {
        val begin = ByteBuf().u8(kind).bytes(obj.copyOfRange(4, 12)).u32(obj.size.toLong()).u32(crc32(obj)).toByteArray()
        val (st, _) = request(Op.PUT_BEGIN, begin)
        if (st == Status.EXISTS) return st
        if (st != Status.OK) throw LinkException("PUT_BEGIN failed: status $st", st)
        var off = 0
        while (off < obj.size) {
            val n = minOf(chunk, obj.size - off)
            val (s2, p) = request(Op.PUT_DATA, ByteBuf().u32(off.toLong()).bytes(obj.copyOfRange(off, off + n)).toByteArray())
            when (s2) {
                Status.OK -> off = ByteReader(p).u32().toInt()
                Status.OFFSET -> off = ByteReader(p).u32().toInt() /* resume where the unit is */
                else -> throw LinkException("PUT_DATA failed: status $s2", s2)
            }
        }
        val (s3, _) = request(Op.PUT_END, timeoutMs = 10000)
        if (s3 != Status.OK) throw LinkException("PUT_END failed: status $s3", s3)
        return Status.OK
    }

    fun delete(kind: Int, id: ByteArray): Int = request(Op.OBJ_DELETE, byteArrayOf(kind.toByte()) + id).first

    fun rideList(): List<RideInfo> {
        val r = ByteReader(ok(request(Op.RIDE_LIST), "RIDE_LIST"))
        return (0 until r.u8()).map { RideInfo(r.u32(), r.u32(), r.u8(), r.u32(), r.u32()) }
    }

    /** Reads a ride and checks its size and CRC-32 against RIDE_OPEN. */
    fun rideFetch(rideId: Long, progress: (Long, Long) -> Unit = { _, _ -> }): ByteArray {
        val r = ByteReader(ok(request(Op.RIDE_OPEN, ByteBuf().u32(rideId).toByteArray(), 10000), "RIDE_OPEN"))
        val size = r.u32(); val crc = r.u32()
        val out = ByteBuf(size.toInt() + 16)
        while (out.size < size) {
            val p = ok(request(Op.RIDE_READ, ByteBuf().u32(rideId).u32(out.size.toLong()).u16(chunk).toByteArray()), "RIDE_READ")
            if (p.isEmpty()) break
            out.bytes(p)
            progress(out.size.toLong(), size)
        }
        val data = out.toByteArray()
        if (data.size.toLong() != size) throw LinkException("ride ${rideId}: ${data.size} of $size bytes")
        if (crc32(data) != crc) throw LinkException("ride ${rideId}: CRC mismatch")
        return data
    }

    fun rideMark(rideId: Long, uploaded: Boolean): Int =
        request(Op.RIDE_MARK, ByteBuf().u32(rideId).u8(if (uploaded) 1 else 0).toByteArray()).first

    fun rideDelete(rideId: Long): Int = request(Op.RIDE_DELETE, ByteBuf().u32(rideId).toByteArray()).first

    /** The connect sequence (protocol 5): HELLO, FTP_GET, TIME_SET, both lists, RIDE_LIST. */
    data class Mirror(val hello: Hello, val ftp: Int, val workouts: List<ObjInfo>, val routes: List<ObjInfo>, val rides: List<RideInfo>)

    fun connect(unixNow: Long, tzMin: Int): Mirror {
        val h = hello()
        val ftp = ftpGet()
        timeSet(unixNow, tzMin)
        return Mirror(h, ftp, objList(1), objList(2), rideList())
    }
}
