package cc700.testing

import cc700.link.LinkTransport
import java.io.DataInputStream
import java.io.EOFException
import java.net.InetSocketAddress
import java.net.Socket
import java.net.SocketTimeoutException

/** Talks to simunit over its TCP framing: each packet is a u16 little-endian length then the packet. */
class SocketTransport(port: Int, override val mtu: Int = 247, host: String = "127.0.0.1") : LinkTransport {
    private val s = Socket().apply { tcpNoDelay = true; connect(InetSocketAddress(host, port), 3000) }
    private val inp = DataInputStream(s.getInputStream().buffered())
    private val outp = s.getOutputStream()

    override fun send(packet: ByteArray) {
        val b = ByteArray(2 + packet.size)
        b[0] = packet.size.toByte(); b[1] = (packet.size shr 8).toByte()
        packet.copyInto(b, 2)
        outp.write(b)
        outp.flush()
    }

    override fun receive(timeoutMs: Long): ByteArray? {
        s.soTimeout = maxOf(1, timeoutMs.toInt())
        return try {
            val lo = inp.read()
            if (lo < 0) throw EOFException("simunit closed the connection")
            s.soTimeout = 3000
            val n = lo or (inp.readUnsignedByte() shl 8)
            ByteArray(n).also { inp.readFully(it) }
        } catch (e: SocketTimeoutException) {
            null
        }
    }

    override fun close() = s.close()
}
