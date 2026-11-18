package cc700.app

import android.Manifest
import android.app.Application
import android.bluetooth.BluetoothManager
import android.content.Context
import android.content.pm.PackageManager
import android.os.Build
import cc700.app.ble.BleConnector
import cc700.app.data.AppDb
import cc700.app.data.ConnectionMode
import cc700.app.data.FileBlobStore
import cc700.app.data.KeySlot
import cc700.app.data.KeyStore
import cc700.app.data.RoomElevationCache
import cc700.app.data.RoomLibraryStore
import cc700.app.data.RoomRideStore
import cc700.app.data.SettingsRepo
import cc700.app.work.UploadWorker
import cc700.link.LinkTransport
import cc700.app.data.RouterChoice
import cc700.app.data.TileCache
import cc700.route.plan.BRouterClient
import cc700.route.plan.OrsClient
import cc700.route.plan.PlaceSearch
import cc700.route.plan.RoutingEngine
import cc700.sync.Connector
import cc700.sync.FoundUnit
import cc700.sync.RideSync
import cc700.sync.SimulatedConnector
import cc700.sync.UnitSession
import cc700.upload.IntervalsClient
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.asCoroutineDispatcher
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlinx.coroutines.yield
import okhttp3.HttpUrl.Companion.toHttpUrl
import okhttp3.OkHttpClient
import java.io.File
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit

class CcApp : Application() {
    lateinit var graph: Graph
        private set

    override fun onCreate() {
        super.onCreate()
        graph = Graph(this)
    }
}

val Context.graph: Graph get() = (applicationContext as CcApp).graph

/** The app's objects, built once. Every unit request runs on [unitThread], one at a time. */
class Graph(private val ctx: Context) {
    val unitThread = Executors.newSingleThreadExecutor { r -> Thread(r, "cc700-unit") }.asCoroutineDispatcher()
    val scope = CoroutineScope(SupervisorJob() + unitThread)
    val db = AppDb.open(ctx)
    val settings = SettingsRepo(ctx, scope)
    val keys = KeyStore(ctx)
    val orsKey = KeyStore(ctx, KeySlot.ORS)
    val assistKey = KeyStore(ctx, KeySlot.OPENROUTER)
    val http: OkHttpClient = OkHttpClient.Builder().callTimeout(90, TimeUnit.SECONDS).build()
    /** Route planning on the phone; the key is read from [orsKey] for each request and kept nowhere else. */
    val routing = OrsClient(http, { orsKey.get() })
    /** BRouter: the default route planner, which keeps to cycle paths and needs no key. */
    val brouter = BRouterClient(http)
    /** The planner's routing service as chosen in Settings. */
    fun router(): RoutingEngine = if (settings.settings.value.planRouter == RouterChoice.ORS) routing else brouter
    /** Place search ("a café", "Richmond Park") for Ask AI: OpenRouteService geocoding, only with its key. */
    fun places(): PlaceSearch? = if (orsKey.hasKey()) routing else null
    /** Map tiles for route previews. */
    val tiles = TileCache(ctx, http)
    val library = RoomLibraryStore(db.library())
    val elevationCache = RoomElevationCache(db.elevation())

    /** Bluetooth or simunit, as chosen in Settings. A stored simunit address is host:port (one colon); a
     * BLE address has five. */
    private val connector = object : Connector {
        private fun simulated(address: String) = address.count { it == ':' } == 1
        private val ble by lazy { BleConnector(ctx) }
        private fun pick(): Connector {
            val s = settings.settings.value
            return if (s.mode == ConnectionMode.SIMULATED) SimulatedConnector(s.simHost, s.simPort) else ble
        }
        override fun scan(timeoutMs: Long) = pick().scan(timeoutMs)
        override fun pair(unit: FoundUnit, passkey: () -> String?): LinkTransport = pick().pair(unit, passkey)
        override fun connect(address: String): LinkTransport {
            val s = settings.settings.value
            return if (simulated(address)) SimulatedConnector(s.simHost, s.simPort).connect(address) else ble.connect(address)
        }
        override fun forget(address: String) { if (!simulated(address)) ble.forget(address) }
    }

    val session = UnitSession(connector, settings)

    /** Simulated mode always; Bluetooth only with the permission granted and the adapter on. */
    private fun mayConnect(): Boolean {
        if (settings.settings.value.mode == ConnectionMode.SIMULATED) return true
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S &&
            ctx.checkSelfPermission(Manifest.permission.BLUETOOTH_CONNECT) != PackageManager.PERMISSION_GRANTED) return false
        val bt = ctx.getSystemService(Context.BLUETOOTH_SERVICE) as? BluetoothManager
        return bt?.adapter?.isEnabled == true
    }

    val rides = RideSync(session, RoomRideStore(db.rides()), FileBlobStore(File(ctx.filesDir, "rides"))) {
        keys.get()?.let { k -> IntervalsClient(http, { k }, settings.settings.value.intervalsBase.toHttpUrl()) }
    }

    init {
        // While connected and idle, listen for EVENT_STATE and EVENT_RIDE_SAVED in short slices so the
        // screens' requests on the same thread wait at most 300 ms.
        scope.launch {
            while (true) {
                if (!session.idle(300)) {
                    // Spec 10.5: back in range, the app reconnects by itself (every 20 s at most).
                    if (mayConnect()) session.reconnectIfDue(System.currentTimeMillis())
                    delay(1000)
                }
                yield()
            }
        }
        session.onConnected = {
            rides.onConnected()
            UploadWorker.enqueue(ctx, replace = false)
        }
        session.onRideSaved = {
            scope.launch {
                runCatching { rides.fetchNew() }
                UploadWorker.enqueue(ctx, replace = false)
            }
        }
    }
}
