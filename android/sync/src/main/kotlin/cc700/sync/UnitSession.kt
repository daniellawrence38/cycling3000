package cc700.sync

import cc700.link.Hello
import cc700.link.LinkClient
import cc700.link.LinkException
import cc700.link.LinkTransport
import cc700.link.ObjInfo
import cc700.link.Op
import cc700.link.RideInfo
import cc700.link.Status
import cc700.testing.SocketTransport
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import java.util.TimeZone

/** A unit found by a scan. [address] is the BLE address, or host:port for the simulated unit. */
data class FoundUnit(val name: String, val address: String)

class PairingException(msg: String) : Exception(msg)

/**
 * How the app reaches a unit. :app implements it over BLE (Nordic library: scan for the service UUID,
 * bond with the passkey, MTU 247, notifications on TX); [SimulatedConnector] reaches simunit over TCP.
 */
interface Connector {
    fun scan(timeoutMs: Long = 10_000): List<FoundUnit>
    /** Connects and bonds. [passkey] asks the user for the 6 digits the unit shows (null: cancelled). */
    fun pair(unit: FoundUnit, passkey: () -> String?): LinkTransport
    /** Connects to the bonded unit. */
    fun connect(address: String): LinkTransport
    /** Removes the phone's bond. The unit's own bond is removed on the unit (screen 19). */
    fun forget(address: String)
}

/**
 * simunit over [SocketTransport] (brief WP6). simunit has no BLE security, so pairing is simulated: the
 * connector plays the unit's part and accepts only [passkey] (shown in the app's connect dialog the way
 * the unit shows it on screen 23).
 */
class SimulatedConnector(
    private val host: String,
    private val port: Int,
    private val mtu: Int = 247,
    val passkey: String = "123456",
) : Connector {
    override fun scan(timeoutMs: Long) = listOf(FoundUnit("CC700-SIM", "$host:$port"))

    override fun pair(unit: FoundUnit, passkey: () -> String?): LinkTransport {
        val k = passkey() ?: throw PairingException("Pairing cancelled")
        if (k.trim() != this.passkey) throw PairingException("Pairing failed: wrong passkey")
        return connect(unit.address)
    }

    override fun connect(address: String): LinkTransport {
        val h = address.substringBeforeLast(':')
        val p = address.substringAfterLast(':').toInt()
        return SocketTransport(p, mtu, h)
    }

    override fun forget(address: String) {}
}

enum class LinkState { NOT_PAIRED, DISCONNECTED, CONNECTING, CONNECTED }

data class UnitStatus(
    val state: LinkState,
    val unit: PairedUnit? = null,
    val hello: Hello? = null,
    val ftp: Int? = null,
    val workouts: List<ObjInfo> = emptyList(),
    val routes: List<ObjInfo> = emptyList(),
    val rides: List<RideInfo> = emptyList(),
    val error: String? = null,
) {
    val connected get() = state == LinkState.CONNECTED
    /** 0 ready, 1 recording, 2 paused (HELLO, EVENT_STATE). */
    val unitState get() = hello?.state ?: 0
    val recording get() = connected && unitState != 0
}

/** A refusal from the unit, with readable text. */
class UnitRefused(val status: Int, msg: String) : Exception(msg)

fun statusText(st: Int): String = when (st) {
    Status.BUSY -> "The unit is recording. Try again after the ride."
    Status.FULL -> "The unit's storage is full."
    Status.TOO_BIG -> "Too big for the unit."
    Status.BAD_OBJECT -> "The unit rejected the file."
    Status.BAD_CRC -> "The transfer was corrupted. Try again."
    Status.NOT_FOUND -> "Not on the unit."
    Status.EXISTS -> "Already on the unit."
    Status.BAD_ARG -> "The unit refused the value."
    Status.IO_ERROR -> "The unit could not write to its storage."
    else -> "The unit answered with status $st."
}

/**
 * One unit: pairing, connection, the mirror (protocol 5) and the requests the screens make. Blocking and
 * serialised: call from one background thread (the app uses a single-threaded dispatcher). A request
 * refused by the unit throws [UnitRefused] and keeps the link; a timeout or a dropped link sets
 * DISCONNECTED with the reason.
 */
class UnitSession(
    private val connector: Connector,
    private val pairing: PairingStore,
    private val nowS: () -> Long = { System.currentTimeMillis() / 1000 },
    private val tzMin: () -> Int = { TimeZone.getDefault().getOffset(System.currentTimeMillis()) / 60_000 },
) {
    private val _status = MutableStateFlow(
        pairing.load().let { UnitStatus(if (it == null) LinkState.NOT_PAIRED else LinkState.DISCONNECTED, it) })
    val status: StateFlow<UnitStatus> = _status.asStateFlow()

    /** Called after every successful connect (RideSync marks and fetches rides here). */
    var onConnected: () -> Unit = {}
    /** EVENT_RIDE_SAVED. */
    var onRideSaved: (Long) -> Unit = {}

    private var transport: LinkTransport? = null
    private var client: LinkClient? = null
    private val lock = Any()

    fun scan(): List<FoundUnit> = connector.scan()

    /** First pairing (protocol 5): connect, bond with the passkey, HELLO, store the unit, then the mirror. */
    fun pair(unit: FoundUnit, passkey: () -> String?): UnitStatus {
        synchronized(lock) {
            drop(null)
            _status.value = UnitStatus(LinkState.CONNECTING, null)
            try {
                val t = connector.pair(unit, passkey)
                open(t)
                pairing.save(PairedUnit(unit.name, unit.address))
                mirror(PairedUnit(unit.name, unit.address))
            } catch (e: Exception) {
                drop(null)
                _status.value = UnitStatus(if (pairing.load() == null) LinkState.NOT_PAIRED else LinkState.DISCONNECTED,
                    pairing.load(), error = e.message ?: "Pairing failed")
                return _status.value
            }
        }
        onConnected()
        return _status.value
    }

    /** Connects to the bonded unit and runs the connect sequence. */
    fun connect(): UnitStatus {
        synchronized(lock) {
            val u = pairing.load() ?: return _status.value
            if (client != null) return _status.value
            _status.value = _status.value.copy(state = LinkState.CONNECTING, unit = u, error = null)
            try {
                open(connector.connect(u.address))
                mirror(u)
            } catch (e: Exception) {
                drop(e.message ?: "Could not connect")
                return _status.value
            }
        }
        onConnected()
        return _status.value
    }

    fun disconnect() = synchronized(lock) { drop(null) }

    /** Forget unit: removes the phone's bond and the stored unit. */
    fun forget() = synchronized(lock) {
        val u = pairing.load()
        drop(null)
        if (u != null) connector.forget(u.address)
        pairing.save(null)
        _status.value = UnitStatus(LinkState.NOT_PAIRED)
    }

    /** Re-reads FTP, both lists and the rides. */
    fun refresh(): UnitStatus = call { c ->
        val h = c.hello()
        _status.value = _status.value.copy(hello = h, ftp = c.ftpGet(), workouts = c.objList(1), routes = c.objList(2),
            rides = c.rideList())
        _status.value
    }

    /** FTP_SET (50 to 600 W, only while the unit is stopped). */
    fun setFtp(w: Int): Int = call { c ->
        require(w in 50..600) { "FTP must be 50 to 600 W" }
        val st = c.ftpSet(w)
        if (st != Status.OK) throw UnitRefused(st, statusText(st))
        val ftp = c.ftpGet()
        _status.value = _status.value.copy(ftp = ftp)
        ftp
    }

    /** Sends a CW1 (kind 1) or CR1 (kind 2) object. Returns OK or EXISTS. */
    fun send(kind: Int, obj: ByteArray): Int = call { c ->
        val st = try { c.put(kind, obj) } catch (e: LinkException) {
            if (e.status >= 0) throw UnitRefused(e.status, statusText(e.status)) else throw e
        }
        lists(c)
        st
    }

    fun delete(kind: Int, id: ByteArray) = call { c ->
        val st = c.delete(kind, id)
        if (st != Status.OK && st != Status.NOT_FOUND) throw UnitRefused(st, statusText(st))
        lists(c)
    }

    fun rideList(): List<RideInfo> = call { c -> c.rideList().also { _status.value = _status.value.copy(rides = it) } }

    fun rideFetch(rideId: Long): ByteArray = call { c ->
        try { c.rideFetch(rideId) } catch (e: LinkException) {
            if (e.status >= 0) throw UnitRefused(e.status, statusText(e.status)) else throw e
        }
    }

    fun rideMark(rideId: Long, uploaded: Boolean) = call { c ->
        val st = c.rideMark(rideId, uploaded)
        if (st != Status.OK) throw UnitRefused(st, statusText(st))
    }

    /** Listens for events while idle (EVENT_STATE, EVENT_RIDE_SAVED). Returns false if not connected. */
    fun idle(ms: Long): Boolean {
        val c = synchronized(lock) { client } ?: return false
        return try {
            synchronized(lock) { c.pollEvents(ms) }
            true
        } catch (e: Exception) {
            synchronized(lock) { drop(e.message ?: "Connection lost") }
            false
        }
    }

    val connected get() = _status.value.connected

    // ------------------------------------------------------------------ internals
    private fun <T> call(block: (LinkClient) -> T): T = synchronized(lock) {
        val c = client ?: throw UnitRefused(-1, "Not connected to the unit.")
        try {
            block(c)
        } catch (e: UnitRefused) {
            throw e
        } catch (e: IllegalArgumentException) {
            throw e
        } catch (e: LinkException) {
            if (e.status >= 0) throw UnitRefused(e.status, statusText(e.status))
            drop(e.message ?: "Connection lost")
            throw UnitRefused(-1, "Connection lost: ${e.message}")
        } catch (e: Exception) {
            drop(e.message ?: "Connection lost")
            throw UnitRefused(-1, "Connection lost: ${e.message}")
        }
    }

    private fun open(t: LinkTransport) {
        transport = t
        client = LinkClient(t).also { c ->
            c.onEvent = { op, p -> event(op, p) }
        }
    }

    private fun event(op: Int, p: ByteArray) {
        when (op) {
            Op.EV_STATE -> if (p.size >= 2) _status.value.hello?.let { h ->
                _status.value = _status.value.copy(hello = h.copy(state = p[0].toInt(), battery = p[1].toInt() and 0xFF))
            }
            Op.EV_RIDE_SAVED -> if (p.size >= 4) onRideSaved(cc700.util.getU32(p, 0))
        }
    }

    private fun mirror(u: PairedUnit) {
        val c = client!!
        val m = c.connect(nowS(), tzMin())
        _status.value = UnitStatus(LinkState.CONNECTED, u, m.hello, m.ftp, m.workouts, m.routes, m.rides)
    }

    private fun lists(c: LinkClient) {
        _status.value = _status.value.copy(workouts = c.objList(1), routes = c.objList(2))
    }

    private fun drop(reason: String?) {
        runCatching { transport?.close() }
        transport = null
        client = null
        val u = pairing.load()
        _status.value = _status.value.copy(
            state = if (u == null) LinkState.NOT_PAIRED else LinkState.DISCONNECTED, unit = u, error = reason)
    }
}
