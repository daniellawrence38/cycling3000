package cc700.sync

import cc700.util.getU16
import cc700.util.getU32
import cc700.util.putU32

/** FIT time base: 1989-12-31T00:00:00Z in Unix seconds. */
const val FIT_EPOCH = 631065600L

class FitFormatException(msg: String) : Exception(msg)

private val CRC_TABLE = intArrayOf(
    0x0000, 0xCC01, 0xD801, 0x1400, 0xF001, 0x3C00, 0x2800, 0xE401,
    0xA001, 0x6C00, 0x7800, 0xB401, 0x5000, 0x9C01, 0x8801, 0x4400,
)

/** The FIT CRC-16 (FIT SDK, as `cc_fit_crc16`). */
fun fitCrc16(b: ByteArray, off: Int = 0, len: Int = b.size - off, init: Int = 0): Int {
    var crc = init
    for (i in off until off + len) {
        val byte = b[i].toInt() and 0xFF
        var t = CRC_TABLE[crc and 0xF]
        crc = (crc shr 4) and 0x0FFF
        crc = crc xor t xor CRC_TABLE[byte and 0xF]
        t = CRC_TABLE[crc and 0xF]
        crc = (crc shr 4) and 0x0FFF
        crc = crc xor t xor CRC_TABLE[(byte shr 4) and 0xF]
    }
    return crc
}

private class Def(val global: Int, val nums: IntArray, val sizes: IntArray, val devBytes: Int) {
    val total = sizes.sum() + devBytes
}

/** End of the data records (header size plus data size); checks the header. */
private fun dataEnd(b: ByteArray): Int {
    if (b.size < 14) throw FitFormatException("too short")
    val hs = b[0].toInt() and 0xFF
    if ((hs != 12 && hs != 14) || String(b, 8, 4, Charsets.US_ASCII) != ".FIT") throw FitFormatException("not a FIT file")
    val end = hs.toLong() + getU32(b, 4)
    if (end + 2 != b.size.toLong()) throw FitFormatException("size does not match the header")
    return end.toInt()
}

/**
 * Walks the messages and reports each 4-byte time field: `timestamp` (253) in every message, `start_time`
 * (2) in lap and session, `time_created` (4) in file_id. The same set as the unit's correction (brief 5.6,
 * `cc_fit_shift_times`).
 */
private fun walkTimes(b: ByteArray, visit: (global: Int, field: Int, off: Int) -> Unit) {
    val end = dataEnd(b)
    val defs = arrayOfNulls<Def>(16)
    var off = b[0].toInt() and 0xFF
    while (off < end) {
        val hdr = b[off].toInt() and 0xFF
        if (hdr and 0x80 != 0) throw FitFormatException("compressed timestamp headers are not supported")
        val local = hdr and 0x0F
        if (hdr and 0x40 != 0) {
            if (off + 6 > end) throw FitFormatException("truncated definition")
            if (b[off + 2].toInt() != 0) throw FitFormatException("big-endian messages are not supported")
            val global = getU16(b, off + 3)
            val nf = b[off + 5].toInt() and 0xFF
            var p = off + 6
            if (p + 3 * nf > end) throw FitFormatException("truncated definition")
            val nums = IntArray(nf) { b[p + 3 * it].toInt() and 0xFF }
            val sizes = IntArray(nf) { b[p + 3 * it + 1].toInt() and 0xFF }
            p += 3 * nf
            var dev = 0
            if (hdr and 0x20 != 0) {
                val nd = b[p].toInt() and 0xFF
                for (k in 0 until nd) dev += b[p + 1 + 3 * k + 1].toInt() and 0xFF
                p += 1 + 3 * nd
            }
            defs[local] = Def(global, nums, sizes, dev)
            off = p
            continue
        }
        val d = defs[local] ?: throw FitFormatException("data message without a definition")
        if (off + 1 + d.total > end) throw FitFormatException("truncated data message")
        var o = off + 1
        for (i in d.nums.indices) {
            val n = d.nums[i]
            val time = d.sizes[i] == 4 && (n == 253 || ((d.global == 18 || d.global == 19) && n == 2) || (d.global == 0 && n == 4))
            if (time) visit(d.global, n, o)
            o += d.sizes[i]
        }
        off += 1 + d.total
    }
}

/** True when the header CRC (if present) and the file CRC are right. */
fun fitValid(b: ByteArray): Boolean = try {
    val end = dataEnd(b)
    val hs = b[0].toInt() and 0xFF
    val hdrOk = hs == 12 || getU16(b, 12) == 0 || getU16(b, 12) == fitCrc16(b, 0, 12)
    hdrOk && getU16(b, end) == fitCrc16(b, 0, end)
} catch (e: FitFormatException) {
    false
}

/** The ride's start in Unix seconds: file_id `time_created`, else the first timestamp. */
fun fitStartUtc(b: ByteArray): Long? {
    var created: Long? = null
    var first: Long? = null
    walkTimes(b) { g, f, o ->
        val v = getU32(b, o)
        if (v != 0xFFFFFFFFL) {
            if (g == 0 && f == 4 && created == null) created = v + FIT_EPOCH
            if (f == 253 && first == null) first = v + FIT_EPOCH
        }
    }
    return created ?: first
}

/**
 * Returns a copy with [offsetS] added to every time field (invalid values left alone) and both CRCs
 * recomputed. Used for a ride flagged "time unknown" once Daniel has entered its date (spec 10.6).
 */
fun shiftFitTimes(src: ByteArray, offsetS: Long): ByteArray {
    val b = src.copyOf()
    walkTimes(b) { _, _, o ->
        val v = getU32(b, o)
        if (v != 0xFFFFFFFFL) putU32(b, o, (v + offsetS) and 0xFFFFFFFFL)
    }
    val end = dataEnd(b)
    if ((b[0].toInt() and 0xFF) == 14) {
        val c = fitCrc16(b, 0, 12)
        b[12] = c.toByte(); b[13] = (c shr 8).toByte()
    }
    val c = fitCrc16(b, 0, end)
    b[end] = c.toByte(); b[end + 1] = (c shr 8).toByte()
    return b
}

/** Counts the time fields (for tests). */
fun fitTimeFields(b: ByteArray): List<Long> {
    val out = ArrayList<Long>()
    walkTimes(b) { _, _, o -> out += getU32(b, o) }
    return out
}
