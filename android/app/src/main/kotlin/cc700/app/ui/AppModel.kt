package cc700.app.ui

import android.app.Application
import android.net.Uri
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import cc700.app.data.AppSettings
import cc700.app.data.Here
import cc700.app.graph
import cc700.route.Pt
import cc700.route.assist.AssistError
import cc700.route.assist.AssistPlanner
import cc700.route.plan.GpxWriter
import cc700.route.plan.Plan
import cc700.route.plan.PlanPrefs
import cc700.route.plan.Planner
import cc700.route.plan.RoutingError
import cc700.route.plan.Scored
import cc700.route.plan.defaultSpeedKmh
import kotlin.math.roundToInt
import cc700.app.work.UploadWorker
import cc700.link.ObjInfo
import cc700.sync.FoundUnit
import cc700.sync.LocalRoute
import cc700.sync.LocalWorkout
import cc700.sync.RideRecord
import cc700.sync.RouteImport
import cc700.sync.UnitRefused
import cc700.sync.UnitStatus
import cc700.sync.WorkoutImport
import cc700.sync.elevationProvider
import cc700.sync.importRoute
import cc700.sync.toLocal
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

/** What the screens show and the actions they take. Unit requests run on the graph's unit thread. */
class AppModel(app: Application) : AndroidViewModel(app) {
    private val g = app.graph

    val status: StateFlow<UnitStatus> = g.session.status
    val rides: StateFlow<List<RideRecord>> = g.rides.rides
    val settings: StateFlow<AppSettings> = g.settings.settings

    private val _workouts = MutableStateFlow(g.library.workouts())
    val workouts: StateFlow<List<LocalWorkout>> = _workouts.asStateFlow()
    private val _routes = MutableStateFlow(g.library.routes())
    val routes: StateFlow<List<LocalRoute>> = _routes.asStateFlow()
    private val _hasKey = MutableStateFlow(g.keys.hasKey())
    val hasKey: StateFlow<Boolean> = _hasKey.asStateFlow()

    /** One-line messages for the snackbar. */
    private val _message = MutableStateFlow<String?>(null)
    val message: StateFlow<String?> = _message.asStateFlow()
    fun messageShown() { _message.value = null }
    fun say(text: String) { _message.value = text }

    private val _busy = MutableStateFlow<String?>(null)
    /** What is running, for a progress line ("Sending…"); null when idle. */
    val busy: StateFlow<String?> = _busy.asStateFlow()

    /** Text shared into the app or picked from a file, waiting for the Workouts screen. */
    val incomingWorkout = MutableStateFlow<String?>(null)

    private fun unit(what: String, block: () -> Unit) {
        viewModelScope.launch {
            _busy.value = what
            try {
                withContext(g.unitThread) { block() }
            } catch (e: UnitRefused) {
                _message.value = e.message
            } catch (e: IllegalArgumentException) {
                _message.value = e.message
            } catch (e: Exception) {
                _message.value = e.message ?: e.javaClass.simpleName
            } finally {
                _busy.value = null
            }
        }
    }

    // ------------------------------------------------------------------ unit
    fun connect() {
        if (g.session.status.value.unit == null || g.session.connected) return
        unit("Connecting") { g.session.connect() }
    }
    fun disconnect() = unit("Disconnecting") { g.session.disconnect() }
    fun forget() = unit("Forgetting") { g.session.forget() }

    private val _found = MutableStateFlow<List<FoundUnit>?>(null)
    val found: StateFlow<List<FoundUnit>?> = _found.asStateFlow()
    fun scan() = unit("Scanning") { _found.value = g.session.scan() }
    fun clearScan() { _found.value = null }

    /** [passkey] is what the user typed (simulated unit); over BLE Android's own dialog asks for it. */
    fun pair(u: FoundUnit, passkey: String?) = unit("Pairing") {
        val s = g.session.pair(u) { passkey }
        _found.value = null
        if (s.error != null) _message.value = s.error else _message.value = "Paired with ${u.name}"
    }

    fun setFtp(w: Int) = unit("Setting FTP") {
        val ftp = g.session.setFtp(w)
        _message.value = "FTP set to $ftp W"
    }

    // ------------------------------------------------------------------ workouts
    fun saveWorkout(ok: WorkoutImport.Ok) {
        viewModelScope.launch(Dispatchers.IO) {
            g.library.putWorkout(ok.toLocal())
            _workouts.value = g.library.workouts()
            _message.value = "Saved ${ok.workout.name}"
        }
    }

    fun deleteLocalWorkout(w: LocalWorkout) {
        viewModelScope.launch(Dispatchers.IO) { g.library.deleteWorkout(w.idHex); _workouts.value = g.library.workouts() }
    }

    fun send(kind: Int, bytes: ByteArray, name: String) = unit("Sending $name") {
        val st = g.session.send(kind, bytes)
        _message.value = if (st == cc700.link.Status.EXISTS) "$name is already on the unit" else "Sent $name"
    }

    fun deleteOnUnit(o: ObjInfo) = unit("Deleting ${o.name}") {
        g.session.delete(o.kind, o.id)
        _message.value = "Deleted ${o.name} from the unit"
    }

    // ------------------------------------------------------------------ routes
    private val _routeImport = MutableStateFlow<RouteImport?>(null)
    val routeImport: StateFlow<RouteImport?> = _routeImport.asStateFlow()
    fun clearRouteImport() { _routeImport.value = null }

    fun importGpx(uri: Uri, fallbackName: String) {
        viewModelScope.launch {
            _busy.value = "Reading route and elevations"
            _routeImport.value = withContext(Dispatchers.IO) {
                val s = settings.value
                val provider = elevationProvider(s.elevation, g.http, g.elevationCache)
                getApplication<Application>().contentResolver.openInputStream(uri)?.use {
                    importRoute(it, fallbackName, provider, s.thresholds.params())
                } ?: RouteImport.Invalid("Could not open the file.")
            }
            _busy.value = null
        }
    }

    fun saveRoute(ok: RouteImport.Ok) {
        viewModelScope.launch(Dispatchers.IO) {
            g.library.putRoute(ok.toLocal())
            _routes.value = g.library.routes()
            _routeImport.value = null
            _message.value = "Saved ${ok.name}"
        }
    }

    fun deleteLocalRoute(r: LocalRoute) {
        viewModelScope.launch(Dispatchers.IO) { g.library.deleteRoute(r.idHex); _routes.value = g.library.routes() }
    }

    // ------------------------------------------------------------------ route planner
    private val _hasOrsKey = MutableStateFlow(g.orsKey.hasKey())
    val hasOrsKey: StateFlow<Boolean> = _hasOrsKey.asStateFlow()
    private val _plan = MutableStateFlow<Plan?>(null)
    /** The options from the last plan; null before planning or after one is used. */
    val plan: StateFlow<Plan?> = _plan.asStateFlow()
    fun clearPlan() { _plan.value = null; _reasons.value = emptyMap() }
    private val _reasons = MutableStateFlow<Map<String, String>>(emptyMap())
    /** The assistant's one-line reason for each option it chose, by candidate id; empty for a plain plan. */
    val reasons: StateFlow<Map<String, String>> = _reasons.asStateFlow()
    private val _hasAssistKey = MutableStateFlow(g.assistKey.hasKey())
    val hasAssistKey: StateFlow<Boolean> = _hasAssistKey.asStateFlow()

    /** Asks the OpenRouter model to plan from [request]. It only sees ids and stats, never the position. */
    fun askAi(minutes: Int, start: Pt?, request: String) {
        viewModelScope.launch {
            g.settings.update { it.copy(planMinutes = minutes) }
            _busy.value = "Finding your position"
            try {
                val from = start ?: Here.fix(getApplication())
                if (from == null) {
                    _message.value = "No position yet. Turn on location, or type a start point."
                    return@launch
                }
                _busy.value = "Asking ${settings.value.assistModel.substringAfter('/')}"
                val ap = withContext(Dispatchers.IO) {
                    AssistPlanner(g.http, { g.assistKey.get() }, g.router(), g.places(), settings.value.assistModel)
                        .plan(from, minutes, request, prefs())
                }
                _plan.value = Plan(ap.targetM, ap.options.map { it.scored }, 0)
                _reasons.value = ap.options.associate { it.scored.candidate.id to it.reason }
            } catch (e: RoutingError.NeedsKey) {
                _message.value = "Add an OpenRouteService key in Settings, or check the one saved."
            } catch (e: RoutingError) {
                _message.value = e.message
            } catch (e: AssistError) {
                _message.value = e.message
            } catch (e: java.io.IOException) {
                _message.value = "Could not reach OpenRouter or the routing service. Check the connection."
            } finally {
                _busy.value = null
            }
        }
    }

    fun saveAssistKey(key: String) {
        viewModelScope.launch(Dispatchers.IO) {
            if (key.isBlank()) return@launch
            g.assistKey.set(key)
            _hasAssistKey.value = true
            _message.value = "OpenRouter key saved"
        }
    }

    fun clearAssistKey() {
        viewModelScope.launch(Dispatchers.IO) { g.assistKey.clear(); _hasAssistKey.value = false }
    }

    fun prefs(s: AppSettings = settings.value) = PlanPrefs(
        profile = s.planProfile,
        favourPaths = s.planFavourPaths,
        speedKmh = s.planSpeedKmh ?: defaultSpeedKmh(s.planProfile),
    )

    /** Plans loops of [minutes] from [start], or from the phone's position when [start] is null. */
    fun planRide(minutes: Int, start: Pt?) {
        viewModelScope.launch {
            g.settings.update { it.copy(planMinutes = minutes) }
            _busy.value = "Finding your position"
            try {
                val from = start ?: Here.fix(getApplication())
                if (from == null) {
                    _message.value = "No position yet. Turn on location, or type a start point."
                    return@launch
                }
                _busy.value = "Planning ${minutes} min loops"
                val p = withContext(Dispatchers.IO) { Planner(g.router(), parallelism = 2).plan(from, minutes, prefs()) }
                _plan.value = p
                _reasons.value = emptyMap()
                if (p.options.isEmpty()) _message.value = "No loops came back. Try a different length or start."
            } catch (e: RoutingError.NeedsKey) {
                _message.value = "Add an OpenRouteService key in Settings, or check the one saved."
            } catch (e: RoutingError) {
                _message.value = e.message
            } catch (e: java.io.IOException) {
                _message.value = "Could not reach the routing service. Check the connection."
            } finally {
                _busy.value = null
            }
        }
    }

    private fun gpxOf(o: Scored, n: Int): Pair<String, String> {
        val p = _plan.value
        val mins = p?.let { (it.targetM / 1000.0 / prefs().speedKmh * 60).roundToInt() } ?: 0
        val name = "Loop $n, ${"%.0f".format(o.candidate.distanceM / 1000)} km" + if (mins > 0) " ($mins min)" else ""
        return name to GpxWriter.write(name, o.candidate.points)
    }

    /** Turns option [n] (1-based) into a route preview, the same one a GPX import shows, so saving and
     * sending to the unit work exactly as for an imported file. */
    fun usePlanned(o: Scored, n: Int) {
        val (name, gpx) = gpxOf(o, n)
        viewModelScope.launch {
            _busy.value = "Reading route and elevations"
            _routeImport.value = withContext(Dispatchers.IO) {
                val s = settings.value
                val provider = elevationProvider(s.elevation, g.http, g.elevationCache)
                importRoute(gpx.byteInputStream(), name, provider, s.thresholds.params())
            }
            _plan.value = null
            _busy.value = null
        }
    }

    fun suggestedGpxName(o: Scored, n: Int) = gpxOf(o, n).first.replace(Regex("[^A-Za-z0-9 ()-]"), "").trim() + ".gpx"

    fun exportPlanned(o: Scored, n: Int, uri: Uri) {
        val (name, gpx) = gpxOf(o, n)
        viewModelScope.launch(Dispatchers.IO) {
            val ok = runCatching {
                getApplication<Application>().contentResolver.openOutputStream(uri)?.use { it.write(gpx.toByteArray()) } != null
            }.getOrDefault(false)
            _message.value = if (ok) "Saved $name as GPX" else "Could not write the GPX file."
        }
    }

    fun saveOrsKey(key: String) {
        viewModelScope.launch(Dispatchers.IO) {
            if (key.isBlank()) return@launch
            g.orsKey.set(key)
            _hasOrsKey.value = true
            _message.value = "OpenRouteService key saved"
        }
    }

    fun clearOrsKey() {
        viewModelScope.launch(Dispatchers.IO) { g.orsKey.clear(); _hasOrsKey.value = false }
    }

    // ------------------------------------------------------------------ rides
    fun syncNow() = unit("Syncing rides") {
        if (g.session.connected) {
            g.rides.markPending()
            val n = g.rides.fetchNew()
            _message.value = if (n == 0) "No new rides" else "Fetched $n ride${if (n == 1) "" else "s"}"
        }
        UploadWorker.enqueue(getApplication(), replace = true)
    }

    fun retry(r: RideRecord) {
        viewModelScope.launch(Dispatchers.IO) {
            g.rides.retry(r.rideId)
            UploadWorker.enqueue(getApplication(), replace = true)
        }
    }

    /** The date (and start time) entered for a ride flagged "time unknown". */
    fun setStart(r: RideRecord, startUtc: Long) {
        viewModelScope.launch(Dispatchers.IO) {
            g.rides.setStart(r.rideId, startUtc)
            UploadWorker.enqueue(getApplication(), replace = true)
        }
    }

    // ------------------------------------------------------------------ settings
    fun saveKey(key: String) {
        viewModelScope.launch(Dispatchers.IO) {
            if (key.isBlank()) return@launch
            g.keys.set(key)
            _hasKey.value = true
            g.rides.keyChanged()
            UploadWorker.enqueue(getApplication(), replace = true)
            _message.value = "API key saved"
        }
    }

    fun clearKey() {
        viewModelScope.launch(Dispatchers.IO) { g.keys.clear(); _hasKey.value = false }
    }

    fun updateSettings(f: (AppSettings) -> AppSettings) {
        viewModelScope.launch { g.settings.update(f) }
    }
}
