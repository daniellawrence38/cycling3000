package cc700.app.data

import android.content.Context
import androidx.datastore.preferences.core.Preferences
import androidx.datastore.preferences.core.booleanPreferencesKey
import androidx.datastore.preferences.core.doublePreferencesKey
import androidx.datastore.preferences.core.edit
import androidx.datastore.preferences.core.intPreferencesKey
import androidx.datastore.preferences.core.stringPreferencesKey
import androidx.datastore.preferences.preferencesDataStore
import cc700.route.assist.AssistPlanner
import cc700.route.plan.BikeProfile
import cc700.sync.ClimbThresholds
import cc700.sync.ElevationSource
import cc700.sync.PairedUnit
import cc700.sync.PairingStore
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.flow.stateIn
import kotlinx.coroutines.runBlocking

private val Context.store by preferencesDataStore("settings")

/** How the app reaches the unit: Bluetooth, or simunit on the development machine (brief WP6). */
enum class ConnectionMode { BLE, SIMULATED }

data class AppSettings(
    val thresholds: ClimbThresholds = ClimbThresholds(),
    val elevation: ElevationSource = ElevationSource.AUTO,
    val mode: ConnectionMode = ConnectionMode.BLE,
    /** simunit as seen from the phone; 10.0.2.2 is the development machine from the emulator. */
    val simHost: String = "10.0.2.2",
    val simPort: Int = 7700,
    /** Intervals.icu base URL; changed only to point at a mock server for testing. */
    val intervalsBase: String = "https://intervals.icu",
    val paired: PairedUnit? = null,
    /** Route planner: the bike, whether to favour cycle paths, the last ride length asked for, and the
     * average speed that turns minutes into kilometres (null: the profile's default). */
    val planProfile: BikeProfile = BikeProfile.ROAD,
    val planFavourPaths: Boolean = true,
    val planMinutes: Int = 60,
    val planSpeedKmh: Double? = null,
    /** The OpenRouter model for "Ask AI" route planning. */
    val assistModel: String = AssistPlanner.DEFAULT_MODEL,
)

/** Settings in DataStore. The API key is not here: it is in [KeyStore]. */
class SettingsRepo(private val ctx: Context, scope: CoroutineScope) : PairingStore {
    private object K {
        val minLen = intPreferencesKey("climb_min_len")
        val minAscent = intPreferencesKey("climb_min_ascent")
        val minGrade = doublePreferencesKey("climb_min_grade")
        val elevation = stringPreferencesKey("elevation_source")
        val mode = stringPreferencesKey("connection_mode")
        val simHost = stringPreferencesKey("sim_host")
        val simPort = intPreferencesKey("sim_port")
        val base = stringPreferencesKey("intervals_base")
        val unitName = stringPreferencesKey("unit_name")
        val unitAddr = stringPreferencesKey("unit_address")
        val unitSim = booleanPreferencesKey("unit_simulated")
        val planProfile = stringPreferencesKey("plan_profile")
        val planPaths = booleanPreferencesKey("plan_favour_paths")
        val planMinutes = intPreferencesKey("plan_minutes")
        val planSpeed = doublePreferencesKey("plan_speed_kmh")
        val assistModel = stringPreferencesKey("assist_model")
    }

    private fun read(p: Preferences): AppSettings {
        val d = AppSettings()
        val name = p[K.unitName]; val addr = p[K.unitAddr]
        return AppSettings(
            thresholds = ClimbThresholds(p[K.minLen] ?: d.thresholds.minLenM, p[K.minAscent] ?: d.thresholds.minAscentM,
                p[K.minGrade] ?: d.thresholds.minGradePct),
            elevation = p[K.elevation]?.let { runCatching { ElevationSource.valueOf(it) }.getOrNull() } ?: d.elevation,
            mode = p[K.mode]?.let { runCatching { ConnectionMode.valueOf(it) }.getOrNull() } ?: d.mode,
            simHost = p[K.simHost] ?: d.simHost,
            simPort = p[K.simPort] ?: d.simPort,
            intervalsBase = p[K.base] ?: d.intervalsBase,
            paired = if (name != null && addr != null) PairedUnit(name, addr) else null,
            planProfile = p[K.planProfile]?.let { runCatching { BikeProfile.valueOf(it) }.getOrNull() } ?: d.planProfile,
            planFavourPaths = p[K.planPaths] ?: d.planFavourPaths,
            planMinutes = p[K.planMinutes] ?: d.planMinutes,
            planSpeedKmh = p[K.planSpeed],
            assistModel = p[K.assistModel]?.takeIf { it.isNotBlank() } ?: d.assistModel,
        )
    }

    val settings: StateFlow<AppSettings> = ctx.store.data.map { read(it) }
        .stateIn(scope, SharingStarted.Eagerly, runBlocking { read(ctx.store.data.first()) })

    suspend fun update(f: (AppSettings) -> AppSettings) {
        ctx.store.edit { p ->
            val s = f(read(p))
            p[K.minLen] = s.thresholds.minLenM
            p[K.minAscent] = s.thresholds.minAscentM
            p[K.minGrade] = s.thresholds.minGradePct
            p[K.elevation] = s.elevation.name
            p[K.mode] = s.mode.name
            p[K.simHost] = s.simHost
            p[K.simPort] = s.simPort
            p[K.base] = s.intervalsBase
            p[K.planProfile] = s.planProfile.name
            p[K.planPaths] = s.planFavourPaths
            p[K.planMinutes] = s.planMinutes
            val speed = s.planSpeedKmh
            if (speed == null) p.remove(K.planSpeed) else p[K.planSpeed] = speed
            p[K.assistModel] = s.assistModel
        }
    }

    // PairingStore: called from the session's background thread.
    override fun load(): PairedUnit? = runBlocking { read(ctx.store.data.first()).paired }

    override fun save(unit: PairedUnit?) {
        runBlocking {
            ctx.store.edit { p ->
                if (unit == null) { p.remove(K.unitName); p.remove(K.unitAddr); p.remove(K.unitSim) }
                else { p[K.unitName] = unit.name; p[K.unitAddr] = unit.address }
            }
        }
    }
}
