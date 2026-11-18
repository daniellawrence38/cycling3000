package cc700.sync

import cc700.link.LinkTransport
import cc700.testing.FakeUnit
import org.junit.jupiter.api.Assertions.assertEquals
import org.junit.jupiter.api.Assertions.assertFalse
import org.junit.jupiter.api.Assertions.assertTrue
import org.junit.jupiter.api.Test

/**
 * The unit forgot the phone (Daniel's first real test: the bond was lost at a power cycle). Every
 * reconnect then made Android pop a pairing request. After two refusals in a row the app stops trying,
 * says why, and only Forget unit and a new pairing bring it back.
 */
class AuthRefusedTest {
    private companion object { const val T0 = 1793000000L }
    private val fake = FakeUnit()

    private class Refusing(val unit: FakeUnit) : Connector {
        var mode = "ok" // "ok", "auth" (unit refuses the bond), "away" (out of range)
        var connects = 0
        var pairs = 0
        override fun scan(timeoutMs: Long) = listOf(FoundUnit("CC700-FAKE", "fake"))
        override fun pair(unit: FoundUnit, passkey: () -> String?): LinkTransport {
            pairs++
            return link()
        }
        override fun connect(address: String): LinkTransport {
            connects++
            return when (mode) {
                "auth" -> throw AuthRefused("encryption failed")
                "away" -> throw java.io.IOException("The unit is not in range")
                else -> link()
            }
        }
        private fun link() = object : LinkTransport by unit { override fun close() {} }
        override fun forget(address: String) {}
    }

    private fun session(c: Refusing) =
        UnitSession(c, MemoryPairingStore(PairedUnit("CC700-FAKE", "fake")), nowS = { T0 }, tzMin = { 480 })

    @Test fun stopsAfterTwoRefusalsAndSaysWhy() {
        val c = Refusing(fake)
        val s = session(c)
        c.mode = "auth"
        s.connect()
        assertFalse(s.needsRepair) // one refusal could be a glitch: keep trying
        assertTrue(s.autoReconnect)
        assertTrue(s.reconnectIfDue(30_000))
        assertTrue(s.needsRepair)
        assertFalse(s.autoReconnect)
        assertEquals(REPAIR_TEXT, s.status.value.error)
        assertTrue(s.status.value.needsRepair)
        assertEquals(LinkState.DISCONNECTED, s.status.value.state)
        assertEquals(2, c.connects)
        // No further attempt from the idle loop, the Connect button or the app coming to the front.
        assertFalse(s.reconnectIfDue(60_000))
        assertFalse(s.reconnectIfDue(600_000))
        s.connect()
        assertEquals(2, c.connects)
        assertEquals(REPAIR_TEXT, s.status.value.error)
        assertEquals(0, c.pairs) // and a reconnect never pairs
    }

    @Test fun outOfRangeIsNotARefusal() {
        val c = Refusing(fake)
        val s = session(c)
        c.mode = "away"
        repeat(5) { i -> if (i == 0) s.connect() else s.reconnectIfDue(i * 30_000L) }
        assertFalse(s.needsRepair)
        assertTrue(s.autoReconnect)
        assertEquals(5, c.connects)
    }

    @Test fun aGoodConnectResetsTheCount() {
        val c = Refusing(fake)
        val s = session(c)
        c.mode = "auth"; s.connect()
        c.mode = "ok"; s.reconnectIfDue(30_000)
        assertTrue(s.connected)
        s.disconnect()
        c.mode = "auth"; s.connect()
        assertFalse(s.needsRepair) // the earlier refusal no longer counts
    }

    @Test fun forgetThenPairRecovers() {
        val c = Refusing(fake)
        val store = MemoryPairingStore(PairedUnit("CC700-FAKE", "fake"))
        val s = UnitSession(c, store, nowS = { T0 }, tzMin = { 480 })
        c.mode = "auth"
        s.connect(); s.connect()
        assertTrue(s.needsRepair)
        s.forget()
        assertFalse(s.needsRepair)
        assertEquals(LinkState.NOT_PAIRED, s.status.value.state)
        c.mode = "ok"
        s.pair(FoundUnit("CC700-FAKE", "fake")) { "123456" }
        assertTrue(s.connected)
        assertFalse(s.status.value.needsRepair)
        assertTrue(s.autoReconnect)
    }
}
