package cc700.sync

import cc700.link.LinkTransport
import cc700.testing.FakeUnit
import org.junit.jupiter.api.Assertions.assertEquals
import org.junit.jupiter.api.Assertions.assertFalse
import org.junit.jupiter.api.Assertions.assertTrue
import org.junit.jupiter.api.Test

/** Spec 10.5: the app reconnects by itself, at most every 20 s, and never after Disconnect or Forget. */
class AutoReconnectTest {
    private companion object { const val T0 = 1793000000L }
    private val fake = FakeUnit()

    private class Counting(val unit: FakeUnit) : Connector {
        var reachable = true
        var attempts = 0
        override fun scan(timeoutMs: Long) = listOf(FoundUnit("CC700-FAKE", "fake"))
        override fun pair(unit: FoundUnit, passkey: () -> String?): LinkTransport = connect(unit.address)
        override fun connect(address: String): LinkTransport {
            attempts++
            if (!reachable) throw java.io.IOException("The unit is not in range")
            return object : LinkTransport by unit { override fun close() {} }
        }
        override fun forget(address: String) {}
    }

    @Test fun notBeforeTheUserAsks() {
        val c = Counting(fake)
        val s = UnitSession(c, MemoryPairingStore(PairedUnit("CC700-FAKE", "fake")), nowS = { T0 }, tzMin = { 480 })
        assertFalse(s.reconnectIfDue(0))
        assertEquals(0, c.attempts)
    }

    @Test fun retriesEvery20sUntilBackThenStops() {
        val c = Counting(fake)
        val s = UnitSession(c, MemoryPairingStore(PairedUnit("CC700-FAKE", "fake")), nowS = { T0 }, tzMin = { 480 })
        c.reachable = false
        s.connect() // the user's Connect: fails, but the wish stands
        assertTrue(s.autoReconnect)
        assertEquals(LinkState.DISCONNECTED, s.status.value.state)
        assertTrue(s.reconnectIfDue(1_000))
        assertFalse(s.reconnectIfDue(5_000)) // too soon
        assertEquals(2, c.attempts)
        c.reachable = true
        assertTrue(s.reconnectIfDue(21_000))
        assertTrue(s.connected)
        assertFalse(s.reconnectIfDue(60_000)) // connected: nothing to do
        assertEquals(3, c.attempts)
    }

    @Test fun disconnectAndForgetStopIt() {
        val c = Counting(fake)
        val store = MemoryPairingStore(null)
        val s = UnitSession(c, store, nowS = { T0 }, tzMin = { 480 })
        s.pair(FoundUnit("CC700-FAKE", "fake")) { "123456" }
        assertTrue(s.connected && s.autoReconnect)
        s.disconnect()
        assertFalse(s.autoReconnect)
        assertFalse(s.reconnectIfDue(100_000))
        s.connect()
        assertTrue(s.connected && s.autoReconnect)
        s.forget()
        assertFalse(s.autoReconnect)
        assertFalse(s.reconnectIfDue(200_000))
    }
}
