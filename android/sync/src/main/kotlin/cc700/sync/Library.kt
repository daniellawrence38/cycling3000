package cc700.sync

import cc700.dem.ChainProvider
import cc700.dem.ElevationCache
import cc700.dem.ElevationProvider
import cc700.dem.GaProvider
import cc700.dem.OpenTopoProvider
import cc700.route.BuiltRoute
import cc700.route.ClimbParams
import cc700.route.GpxError
import cc700.route.buildRoute
import cc700.route.parseGpx
import cc700.util.hex
import cc700.workout.PctStep
import cc700.workout.Workout
import cc700.workout.WorkoutError
import cc700.workout.encodeCw1
import cc700.workout.parseWorkout
import cc700.workout.stepRange
import cc700.workout.targetZone
import cc700.workout.watts
import okhttp3.OkHttpClient
import java.io.InputStream

// ------------------------------------------------------------------ workouts (brief 7.3)
sealed class WorkoutImport {
    class Ok(val workout: Workout, val cw1: ByteArray, val source: String) : WorkoutImport() {
        val idHex: String get() = cw1.copyOfRange(4, 12).hex()
    }
    /** [message] names the field and the position, e.g. "Step 3: label is longer than 16 characters". */
    class Invalid(val message: String, val code: String) : WorkoutImport()
}

/** Normalises typography, validates against the coach rules and V§4, converts and encodes. Nothing is
 * corrected beyond the normalisation list. */
fun importWorkout(text: String): WorkoutImport = try {
    val w = parseWorkout(text)
    WorkoutImport.Ok(w, encodeCw1(w), text)
} catch (e: WorkoutError) {
    WorkoutImport.Invalid(e.readable, e.code)
}

/** One row of the preview at the unit's FTP: a steady step's band [lowW, highW], or a ramp's start and
 * end watts (lowW is the start, highW the end; a cool-down ramp goes down). */
data class PreviewStep(val label: String, val secs: Int, val lowW: Int, val highW: Int, val zone: Int, val ramp: Boolean,
                       val rep: String)

fun previewSteps(w: Workout, ftp: Int): List<PreviewStep> = w.steps.map { s: PctStep ->
    val (lo, hi) = if (s.ramp) watts(ftp, s.p0) to watts(ftp, s.p1) else stepRange(ftp, s)
    PreviewStep(s.label, s.secs, lo, hi, targetZone(s), s.ramp, if (s.repTotal > 0) "${s.repIdx}/${s.repTotal}" else "")
}

fun WorkoutImport.Ok.toLocal() = LocalWorkout(idHex, workout.name, workout.totalSecs, workout.steps.size, source, cw1)

// ------------------------------------------------------------------ routes (brief 7.4)
enum class ElevationSource(val label: String) {
    AUTO("GA 5 m LiDAR, then SRTM 30 m, then the file"),
    SRTM("SRTM 30 m, then the file"),
    FILE("The GPX file's own elevations"),
}

/** The provider chain for a source. GA and Open Topo Data are Unverified against the live services
 * (DEM-FINDINGS.md). */
fun elevationProvider(src: ElevationSource, http: OkHttpClient, cache: ElevationCache): ElevationProvider = when (src) {
    ElevationSource.AUTO -> ChainProvider(listOf(GaProvider(http, cache), OpenTopoProvider(http, cache)))
    ElevationSource.SRTM -> OpenTopoProvider(http, cache)
    ElevationSource.FILE -> ElevationProvider { pts -> List(pts.size) { null } }
}

sealed class RouteImport {
    class Ok(val name: String, val route: BuiltRoute) : RouteImport() {
        val idHex: String get() = route.obj.id.hex()
    }
    class Invalid(val message: String) : RouteImport()
}

fun importRoute(input: InputStream, fallbackName: String, elevation: ElevationProvider, params: ClimbParams): RouteImport = try {
    val (name, pts) = parseGpx(input)
    if (pts.size < 2) RouteImport.Invalid("The GPX file has fewer than two track points.")
    else {
        val n = name.ifBlank { fallbackName }
        RouteImport.Ok(n, buildRoute(n, pts, { elevation.sample(it) }, params))
    }
} catch (e: GpxError) {
    RouteImport.Invalid(e.message ?: "Not a GPX file.")
} catch (e: Exception) {
    RouteImport.Invalid("Could not read the GPX file: ${e.message}")
}

fun RouteImport.Ok.toLocal() = LocalRoute(idHex, route.obj.let { name }, route.obj.distM, route.obj.ascentM,
    route.climbs.size, route.fromFile, route.obj.bytes)

/** Climb thresholds the user can change in Settings (V§8 defaults). */
data class ClimbThresholds(val minLenM: Int = 300, val minAscentM: Int = 15, val minGradePct: Double = 3.0) {
    fun params() = ClimbParams(minLen = minLenM, minAscent = minAscentM.toDouble(), minGrade = minGradePct)
}
