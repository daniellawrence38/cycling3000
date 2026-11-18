package cc700.app.ble

import android.annotation.SuppressLint
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothGattCharacteristic
import android.bluetooth.BluetoothManager
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanResult
import android.bluetooth.le.ScanSettings
import android.content.Context
import android.os.ParcelUuid
import android.util.Log
import cc700.link.LinkTransport
import cc700.sync.AuthRefused
import cc700.sync.Connector
import cc700.sync.FoundUnit
import cc700.sync.PairingException
import no.nordicsemi.android.ble.BleManager
import no.nordicsemi.android.ble.observer.BondingObserver
import no.nordicsemi.android.ble.observer.ConnectionObserver
import java.util.UUID
import java.util.concurrent.LinkedBlockingDeque
import java.util.concurrent.TimeUnit

/** Protocol 2.1. */
object CcGatt {
    val SERVICE: UUID = UUID.fromString("c7a1e6b0-4d52-4f0a-9c3e-0b5d7a2f8e11")
    val RX: UUID = UUID.fromString("c7a1e6b1-4d52-4f0a-9c3e-0b5d7a2f8e11")
    val TX: UUID = UUID.fromString("c7a1e6b2-4d52-4f0a-9c3e-0b5d7a2f8e11")
}

private const val TAG = "CC700Ble"

/** How long a link waits for the unit's first notification before the first request (see [CcBleManager.open]). */
private const val READY_WAIT_MS = 5_000L
/** After this much quiet, the next write asks Android for the shortest connection interval again
 * (protocol 2.4: the unit drops to 100 to 200 ms after 10 s idle; the phone speeds it up for a transfer). */
private const val BOOST_AFTER_MS = 5_000L

/**
 * One GATT link to the unit through the Nordic Android BLE library: MTU 247 requested, notifications on
 * TX, packets written to RX without response (the library queues and paces them). Logs go to logcat
 * under the tag CC700Ble (message sizes and states only; never a passkey, a key or a payload).
 */
class CcBleManager(ctx: Context) : BleManager(ctx) {
    private var rx: BluetoothGattCharacteristic? = null
    private var tx: BluetoothGattCharacteristic? = null
    val inbox = LinkedBlockingDeque<ByteArray>()
    @Volatile var lost = false
        private set
    @Volatile private var lastWriteMs = 0L

    fun currentMtu(): Int = mtu

    override fun isRequiredServiceSupported(gatt: BluetoothGatt): Boolean {
        val s = gatt.getService(CcGatt.SERVICE) ?: return false
        rx = s.getCharacteristic(CcGatt.RX)
        tx = s.getCharacteristic(CcGatt.TX)
        return rx != null && tx != null
    }

    override fun initialize() {
        // Protocol 5, Connect: MTU 247, then notifications on TX. Both work before the link is encrypted;
        // the unit sends nothing until it is.
        requestMtu(247).with { _, m -> Log.i(TAG, "MTU $m") }.enqueue()
        setNotificationCallback(tx).with { _, data -> data.value?.let { inbox.put(it) } }
        enableNotifications(tx).enqueue()
    }

    override fun onServicesInvalidated() {
        rx = null; tx = null
        lost = true
    }

    fun write(packet: ByteArray) {
        val c = rx ?: throw java.io.IOException("not connected")
        val now = System.currentTimeMillis()
        if (now - lastWriteMs > BOOST_AFTER_MS)
            requestConnectionPriority(BluetoothGatt.CONNECTION_PRIORITY_HIGH).enqueue()
        lastWriteMs = now
        writeCharacteristic(c, packet, BluetoothGattCharacteristic.WRITE_TYPE_NO_RESPONSE).await()
    }

    val linkUp: Boolean get() = isConnected && !lost

    /**
     * Connects. With [bond] (first pairing, protocol 5), creates the bond: Android shows its pairing
     * dialog and the user types the 6 digits the unit shows; waits up to 60 s, the unit's pairing window.
     * Without [bond], the phone must already be bonded; the unit then asks for
     * encryption and Android uses the stored key.
     *
     * Then waits up to 5 s for the unit's first notification: the unit sends EVENT_STATE as soon as the
     * link is encrypted and subscribed, so a request sent after it is not lost to a link that is not yet
     * encrypted. The notification stays in [inbox] for the protocol client. Without it (events are a
     * convenience, protocol 4) the requests go ahead anyway and their own timeouts apply.
     */
    @SuppressLint("MissingPermission")
    fun open(device: BluetoothDevice, bond: Boolean) {
        if (bond) pairNew(device) else reconnect(device)
    }

    // What went wrong with the bond on a reconnect, set by the observers below (any thread).
    @Volatile private var authProblem: String? = null
    @Volatile private var connectedAtMs = 0L
    @Volatile private var disconnectReason = Int.MIN_VALUE
    @Volatile private var reconnecting = false

    init {
        setBondingObserver(object : BondingObserver {
            override fun onBondingRequired(device: BluetoothDevice) {
                // On a reconnect this means Android began a new pairing by itself (the unit no longer has
                // the key): the system pairing dialog is coming. Never let a reconnect pair.
                if (reconnecting) authProblem = "Android started a new pairing on a reconnect"
            }
            override fun onBonded(device: BluetoothDevice) {}
            override fun onBondingFailed(device: BluetoothDevice) {
                if (reconnecting) authProblem = "bonding failed on a reconnect"
            }
        })
        setConnectionObserver(object : ConnectionObserver {
            override fun onDeviceConnecting(device: BluetoothDevice) {}
            override fun onDeviceConnected(device: BluetoothDevice) { connectedAtMs = System.currentTimeMillis() }
            override fun onDeviceFailedToConnect(device: BluetoothDevice, reason: Int) {}
            override fun onDeviceReady(device: BluetoothDevice) {}
            override fun onDeviceDisconnecting(device: BluetoothDevice) {}
            override fun onDeviceDisconnected(device: BluetoothDevice, reason: Int) { disconnectReason = reason }
        })
    }

    /**
     * Reconnect to a bonded unit. Never creates a bond and never lets Android pair: if the unit refuses
     * the stored key, Android may start a pairing by itself and pop its dialog, so the link is dropped at
     * once and [AuthRefused] tells the session (two in a row stop the automatic reconnect). The signs:
     * the phone's bond is gone before or after the attempt, the bond state turns to bonding or none, a
     * bonding failure, or the unit closing the link within 10 s of connecting without a first
     * notification (the unit refuses a pairing by disconnecting with 0x05; Nordic's library reports a
     * peer termination or an unknown reason, not the raw code).
     */
    @SuppressLint("MissingPermission")
    private fun reconnect(device: BluetoothDevice) {
        lost = false
        inbox.clear()
        authProblem = null
        disconnectReason = Int.MIN_VALUE
        connectedAtMs = 0L
        if (device.bondState != BluetoothDevice.BOND_BONDED)
            throw AuthRefused("This phone's pairing with the unit is gone")
        reconnecting = true
        try {
            Log.i(TAG, "reconnecting (bonded)")
            try {
                connect(device).useAutoConnect(false).retry(3, 200).timeout(15_000).await()
            } catch (e: Exception) {
                refusedOrNull(device)?.let { throw AuthRefused(it) }
                throw e
            }
            val until = System.currentTimeMillis() + READY_WAIT_MS
            while (inbox.isEmpty() && System.currentTimeMillis() < until) {
                refusedOrNull(device)?.let { throw AuthRefused(it) }
                if (!isConnected) throw java.io.IOException("the unit closed the link")
                Thread.sleep(50)
            }
            refusedOrNull(device)?.let { throw AuthRefused(it) }
            Log.i(TAG, if (inbox.isEmpty()) "no first notification within 5 s; going ahead" else "unit ready, MTU $mtu")
        } catch (e: AuthRefused) {
            Log.w(TAG, "the unit refused this phone's bond: ${e.message}")
            runCatching { disconnect().enqueue() } // ends any pairing Android began, and its dialog
            throw e
        } finally {
            reconnecting = false
        }
    }

    @SuppressLint("MissingPermission")
    private fun refusedOrNull(device: BluetoothDevice): String? {
        authProblem?.let { return it }
        when (device.bondState) {
            BluetoothDevice.BOND_BONDING -> return "Android started a new pairing on a reconnect"
            BluetoothDevice.BOND_NONE -> return "this phone's pairing with the unit was removed"
        }
        val r = disconnectReason
        val early = connectedAtMs != 0L && System.currentTimeMillis() - connectedAtMs < 10_000
        if (!isConnected && early && inbox.isEmpty() && (r == ConnectionObserver.REASON_TERMINATE_PEER_USER ||
                r == ConnectionObserver.REASON_UNKNOWN || r == ConnectionObserver.REASON_NOT_SUPPORTED))
            return "the unit closed the link right after connecting (reason $r)"
        return null
    }

    @SuppressLint("MissingPermission")
    private fun pairNew(device: BluetoothDevice) {
        lost = false
        inbox.clear()
        Log.i(TAG, "connecting to pair (phone bond state ${device.bondState})")
        connect(device).useAutoConnect(false).retry(3, 200).timeout(15_000).await()
        // The only place the app starts a pairing: the explicit Add unit flow (UnitSession.pair).
        createBondInsecure().enqueue() // createBond() is deprecated in 2.9 in its favour; same request
        val bondBy = System.currentTimeMillis() + 60_000
        while (device.bondState != BluetoothDevice.BOND_BONDED) {
            if (System.currentTimeMillis() > bondBy) throw java.io.IOException("not bonded within 60 s")
            if (!isConnected) throw java.io.IOException("the unit closed the link while pairing")
            Thread.sleep(200)
        }
        Log.i(TAG, "bonded")
        val until = System.currentTimeMillis() + READY_WAIT_MS
        while (inbox.isEmpty() && System.currentTimeMillis() < until) {
            if (!isConnected) throw java.io.IOException("the unit closed the link")
            Thread.sleep(50)
        }
        Log.i(TAG, if (inbox.isEmpty()) "no first notification within 5 s; going ahead" else "unit ready, MTU $mtu")
    }

    fun shut() {
        runCatching { disconnect().await() }
        close()
    }

    fun unbond() { removeBond().enqueue() }
}

/** The blocking [LinkTransport] that :core's LinkClient uses, over [CcBleManager]. Never on the main thread. */
class BleLink(private val m: CcBleManager) : LinkTransport {
    override val mtu: Int get() = m.currentMtu()
    override fun send(packet: ByteArray) = m.write(packet)
    override fun receive(timeoutMs: Long): ByteArray? {
        if (!m.linkUp && m.inbox.isEmpty()) throw java.io.IOException("connection lost")
        return m.inbox.poll(timeoutMs, TimeUnit.MILLISECONDS)
    }
    override fun close() = m.shut()
}

/**
 * Scan, pair and connect over BLE (protocol 5). The passkey is typed into Android's own pairing dialog,
 * which appears when the bond is created, so the passkey callback is not used here. Needs the
 * BLUETOOTH_SCAN and BLUETOOTH_CONNECT permissions, which the screens request first.
 */
@SuppressLint("MissingPermission")
class BleConnector(private val ctx: Context) : Connector {
    private val adapter get() = (ctx.getSystemService(Context.BLUETOOTH_SERVICE) as BluetoothManager).adapter

    /** Units advertising the CC700 service (in the scan response) or named CC700-xxxx. Matched here rather
     * than with a hardware ScanFilter, which on some phones does not look at the scan response. */
    override fun scan(timeoutMs: Long): List<FoundUnit> {
        val a = adapter ?: throw PairingException("This phone has no Bluetooth")
        if (!a.isEnabled) throw PairingException("Bluetooth is off")
        val scanner = a.bluetoothLeScanner ?: throw PairingException("Bluetooth is off")
        val found = LinkedHashMap<String, FoundUnit>()
        val svc = ParcelUuid(CcGatt.SERVICE)
        val cb = object : ScanCallback() {
            override fun onScanResult(callbackType: Int, result: ScanResult) {
                val rec = result.scanRecord
                val name = rec?.deviceName ?: result.device.name
                val hasService = rec?.serviceUuids?.contains(svc) == true
                if (hasService || name?.startsWith("CC700-") == true)
                    synchronized(found) { found[result.device.address] = FoundUnit(name ?: "CC700", result.device.address) }
            }
            override fun onScanFailed(errorCode: Int) { Log.w(TAG, "scan failed: $errorCode") }
        }
        scanner.startScan(null, ScanSettings.Builder().setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY).build(), cb)
        try {
            Thread.sleep(timeoutMs)
        } finally {
            scanner.stopScan(cb)
        }
        return synchronized(found) { found.values.toList() }.also { Log.i(TAG, "scan found ${it.size} unit(s)") }
    }

    override fun pair(unit: FoundUnit, passkey: () -> String?): LinkTransport {
        val d = adapter.getRemoteDevice(unit.address)
        if (d.bondState == BluetoothDevice.BOND_BONDED) {
            // A bond left from before (the unit was forgotten on the unit, or the app was reinstalled):
            // removing it first makes this a clean pairing. Removing a bond drops the link, so it is
            // done on its own connection, as Forget unit does.
            Log.i(TAG, "removing the phone's old bond with ${unit.name} before pairing again")
            forget(unit.address)
        }
        val m = CcBleManager(ctx)
        try {
            m.open(d, bond = true)
        } catch (e: Exception) {
            Log.w(TAG, "pairing failed: ${e.message}")
            runCatching { m.shut() }
            throw PairingException("Pairing failed. On the unit open Menu, Phone, PAIR, then try again within 60 s " +
                "and type the 6 digits it shows.")
        }
        return BleLink(m)
    }

    override fun connect(address: String): LinkTransport {
        val m = CcBleManager(ctx)
        try {
            m.open(adapter.getRemoteDevice(address), bond = false)
        } catch (e: AuthRefused) {
            runCatching { m.shut() }
            throw e // UnitSession counts these and stops after two (REPAIR_TEXT)
        } catch (e: Exception) {
            Log.w(TAG, "connect failed: ${e.message}")
            runCatching { m.shut() }
            throw java.io.IOException("The unit is not in range")
        }
        return BleLink(m)
    }

    override fun forget(address: String) {
        val d = adapter?.getRemoteDevice(address) ?: return
        if (d.bondState == BluetoothDevice.BOND_NONE) return
        // BluetoothDevice.removeBond is hidden API; the Nordic library calls it for us, over a connection.
        runCatching {
            val m = CcBleManager(ctx)
            m.connect(d).useAutoConnect(false).timeout(10_000).await()
            m.unbond()
            val until = System.currentTimeMillis() + 5_000
            while (d.bondState != BluetoothDevice.BOND_NONE && System.currentTimeMillis() < until) Thread.sleep(100)
            m.shut()
        }
    }
}
