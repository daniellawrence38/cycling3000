package cc700.util

import java.security.MessageDigest
import java.util.zip.CRC32

/** CRC-32 (IEEE 802.3), as V§1. */
fun crc32(b: ByteArray, off: Int = 0, len: Int = b.size - off): Long {
    val c = CRC32()
    c.update(b, off, len)
    return c.value
}

fun sha256(b: ByteArray): ByteArray = MessageDigest.getInstance("SHA-256").digest(b)

/** Floor division (Python `//`), brief 5.0. Kotlin `/` truncates toward zero. */
fun fdiv(a: Long, b: Long): Long = Math.floorDiv(a, b)
fun fdiv(a: Int, b: Int): Int = Math.floorDiv(a, b)

fun zigzag(n: Long): Long = (n shl 1) xor (n shr 63)
fun unzigzag(u: Long): Long = (u ushr 1) xor -(u and 1)

fun varint(v: Long, out: ByteBuf) {
    var u = v
    while (true) {
        val b = (u and 0x7F).toInt()
        u = u ushr 7
        if (u != 0L) out.u8(b or 0x80) else { out.u8(b); return }
    }
}

fun ByteArray.hex(): String = joinToString("") { "%02x".format(it.toInt() and 0xFF) }
fun String.unhex(): ByteArray = replace(" ", "").replace("\n", "").chunked(2).map { it.toInt(16).toByte() }.toByteArray()

/** Little-endian byte builder. */
class ByteBuf(initial: Int = 256) {
    private var a = ByteArray(initial)
    var size = 0
        private set

    private fun ensure(n: Int) {
        if (size + n > a.size) a = a.copyOf(maxOf(a.size * 2, size + n))
    }

    fun u8(v: Int) = apply { ensure(1); a[size++] = v.toByte() }
    fun u16(v: Int) = apply { u8(v and 0xFF); u8((v shr 8) and 0xFF) }
    fun u32(v: Long) = apply { for (i in 0 until 4) u8(((v shr (8 * i)) and 0xFF).toInt()) }
    fun i32(v: Int) = u32(v.toLong() and 0xFFFFFFFFL)
    fun bytes(b: ByteArray) = apply { ensure(b.size); b.copyInto(a, size); size += b.size }
    fun toByteArray(): ByteArray = a.copyOf(size)
}

/** Little-endian reader with bounds checks. */
class ByteReader(val b: ByteArray, var pos: Int = 0) {
    val remaining get() = b.size - pos
    fun u8(): Int { require(pos + 1 <= b.size) { "truncated" }; return b[pos++].toInt() and 0xFF }
    fun u16(): Int = u8() or (u8() shl 8)
    fun i16(): Int = u16().toShort().toInt()
    fun u32(): Long = (u16().toLong()) or (u16().toLong() shl 16)
    fun i32(): Int = u32().toInt()
    fun bytes(n: Int): ByteArray { require(pos + n <= b.size) { "truncated" }; val r = b.copyOfRange(pos, pos + n); pos += n; return r }
}

fun putU32(b: ByteArray, off: Int, v: Long) { for (i in 0 until 4) b[off + i] = ((v shr (8 * i)) and 0xFF).toByte() }
fun getU32(b: ByteArray, off: Int): Long =
    (0 until 4).fold(0L) { acc, i -> acc or ((b[off + i].toLong() and 0xFF) shl (8 * i)) }
fun getU16(b: ByteArray, off: Int): Int = (b[off].toInt() and 0xFF) or ((b[off + 1].toInt() and 0xFF) shl 8)

/** Object ID: first 8 bytes of SHA-256 over the object with bytes 4..11 zeroed (protocol 3.1). */
fun objectId(obj: ByteArray): ByteArray {
    val z = obj.copyOf()
    for (i in 4 until 12) z[i] = 0
    return sha256(z).copyOf(8)
}
