package cc700.app.ble

import android.annotation.SuppressLint
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothGattCharacteristic
import android.bluetooth.BluetoothManager
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanFilter
import android.bluetooth.le.ScanResult
import android.bluetooth.le.ScanSettings
import android.content.Context
import android.os.ParcelUuid
import cc700.link.LinkTransport
import cc700.sync.Connector
import cc700.sync.FoundUnit
import cc700.sync.PairingException
import no.nordicsemi.android.ble.BleManager
import java.util.UUID
import java.util.concurrent.LinkedBlockingQueue
import java.util.concurrent.TimeUnit

/** Protocol 2.1. */
object CcGatt {
    val SERVICE: UUID = UUID.fromString("c7a1e6b0-4d52-4f0a-9c3e-0b5d7a2f8e11")
    val RX: UUID = UUID.fromString("c7a1e6b1-4d52-4f0a-9c3e-0b5d7a2f8e11")
    val TX: UUID = UUID.fromString("c7a1e6b2-4d52-4f0a-9c3e-0b5d7a2f8e11")
}

/**
 * One GATT link to the unit through the Nordic Android BLE library: MTU 247 requested, notifications on
 * TX, packets written to RX without response (the library queues and paces them).
 */
class CcBleManager(ctx: Context) : BleManager(ctx) {
    private var rx: BluetoothGattCharacteristic? = null
    private var tx: BluetoothGattCharacteristic? = null
    val inbox = LinkedBlockingQueue<ByteArray>()
    @Volatile var lost = false
        private set

    fun currentMtu(): Int = mtu

    override fun isRequiredServiceSupported(gatt: BluetoothGatt): Boolean {
        val s = gatt.getService(CcGatt.SERVICE) ?: return false
        rx = s.getCharacteristic(CcGatt.RX)
        tx = s.getCharacteristic(CcGatt.TX)
        return rx != null && tx != null
    }

    override fun initialize() {
        requestMtu(247).enqueue()
        setNotificationCallback(tx).with { _, data -> data.value?.let { inbox.put(it) } }
        enableNotifications(tx).enqueue()
    }

    override fun onServicesInvalidated() {
        rx = null; tx = null
        lost = true
    }

    fun write(packet: ByteArray) {
        val c = rx ?: throw java.io.IOException("not connected")
        writeCharacteristic(c, packet, BluetoothGattCharacteristic.WRITE_TYPE_NO_RESPONSE).await()
    }

    /** Connects; with [bond], creates the bond (Android shows its passkey dialog) and waits up to 60 s
     * for it, the length of the unit's pairing window. */
    @SuppressLint("MissingPermission")
    fun open(device: BluetoothDevice, bond: Boolean) {
        lost = false
        inbox.clear()
        connect(device).useAutoConnect(false).retry(3, 200).timeout(15_000).await()
        if (bond) {
            ensureBond().enqueue()
            val until = System.currentTimeMillis() + 60_000
            while (device.bondState != BluetoothDevice.BOND_BONDED) {
                if (System.currentTimeMillis() > until || !isConnected) throw java.io.IOException("not bonded")
                Thread.sleep(200)
            }
        }
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
        if (m.lost && m.inbox.isEmpty()) throw java.io.IOException("connection lost")
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

    override fun scan(timeoutMs: Long): List<FoundUnit> {
        val scanner = adapter?.bluetoothLeScanner ?: throw PairingException("Bluetooth is off")
        val found = LinkedHashMap<String, FoundUnit>()
        val cb = object : ScanCallback() {
            override fun onScanResult(callbackType: Int, result: ScanResult) {
                val name = result.scanRecord?.deviceName ?: result.device.name ?: return
                if (name.startsWith("CC700-")) synchronized(found) { found[result.device.address] = FoundUnit(name, result.device.address) }
            }
        }
        val filter = ScanFilter.Builder().setServiceUuid(ParcelUuid(CcGatt.SERVICE)).build()
        scanner.startScan(listOf(filter), ScanSettings.Builder().setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY).build(), cb)
        try {
            Thread.sleep(timeoutMs)
        } finally {
            scanner.stopScan(cb)
        }
        return synchronized(found) { found.values.toList() }
    }

    override fun pair(unit: FoundUnit, passkey: () -> String?): LinkTransport {
        val m = CcBleManager(ctx)
        try {
            m.open(adapter.getRemoteDevice(unit.address), bond = true)
        } catch (e: Exception) {
            runCatching { m.shut() }
            throw PairingException("Pairing failed. Open Menu, Phone, Pair on the unit and try again within 60 s.")
        }
        return BleLink(m)
    }

    override fun connect(address: String): LinkTransport {
        val m = CcBleManager(ctx)
        try {
            m.open(adapter.getRemoteDevice(address), bond = false)
        } catch (e: Exception) {
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
            m.open(d, bond = false)
            m.unbond()
            val until = System.currentTimeMillis() + 5_000
            while (d.bondState != BluetoothDevice.BOND_NONE && System.currentTimeMillis() < until) Thread.sleep(100)
            m.shut()
        }
    }
}
