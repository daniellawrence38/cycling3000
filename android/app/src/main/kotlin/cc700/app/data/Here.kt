package cc700.app.data

import android.Manifest
import android.annotation.SuppressLint
import android.content.Context
import android.content.pm.PackageManager
import android.location.Location
import android.location.LocationManager
import android.os.Build
import cc700.route.Pt
import kotlinx.coroutines.suspendCancellableCoroutine
import kotlinx.coroutines.withTimeoutOrNull
import java.util.concurrent.Executors
import kotlin.coroutines.resume
import kotlin.math.roundToInt

/** One position fix for the route planner's start point. No Play Services: the platform's own
 * location manager, a single current fix where Android offers one (11 and later), else the last
 * known one. Nothing is stored. */
object Here {
    fun permitted(ctx: Context) =
        ctx.checkSelfPermission(Manifest.permission.ACCESS_FINE_LOCATION) == PackageManager.PERMISSION_GRANTED ||
            ctx.checkSelfPermission(Manifest.permission.ACCESS_COARSE_LOCATION) == PackageManager.PERMISSION_GRANTED

    /** True when only approximate location was granted: the start may be off by a kilometre or more. */
    fun approximateOnly(ctx: Context) =
        ctx.checkSelfPermission(Manifest.permission.ACCESS_FINE_LOCATION) != PackageManager.PERMISSION_GRANTED && permitted(ctx)

    @SuppressLint("MissingPermission") // checked by permitted() first
    suspend fun fix(ctx: Context, timeoutMs: Long = 20_000): Pt? {
        if (!permitted(ctx)) return null
        val lm = ctx.getSystemService(Context.LOCATION_SERVICE) as? LocationManager ?: return null
        val providers = buildList {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) add(LocationManager.FUSED_PROVIDER)
            add(LocationManager.GPS_PROVIDER)
            add(LocationManager.NETWORK_PROVIDER)
        }.filter { runCatching { lm.isProviderEnabled(it) }.getOrDefault(false) }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            for (p in providers) {
                val loc = withTimeoutOrNull(timeoutMs) {
                    suspendCancellableCoroutine<Location?> { cont ->
                        val signal = android.os.CancellationSignal()
                        cont.invokeOnCancellation { signal.cancel() }
                        lm.getCurrentLocation(p, signal, executor) { cont.resume(it) }
                    }
                }
                if (loc != null) return loc.toPt()
            }
        }
        return providers.mapNotNull { runCatching { lm.getLastKnownLocation(it) }.getOrNull() }
            .maxByOrNull { it.time }?.toPt()
    }

    private val executor = Executors.newSingleThreadExecutor { r -> Thread(r, "cc700-location") }

    private fun Location.toPt() = Pt((latitude * 1e7).roundToInt(), (longitude * 1e7).roundToInt())

    /** "51.5074, -0.1278" typed by hand when there is no fix. */
    fun parse(text: String): Pt? {
        val parts = text.split(',', ' ', ';').filter { it.isNotBlank() }
        if (parts.size != 2) return null
        val lat = parts[0].toDoubleOrNull() ?: return null
        val lon = parts[1].toDoubleOrNull() ?: return null
        if (lat !in -90.0..90.0 || lon !in -180.0..180.0) return null
        return Pt((lat * 1e7).roundToInt(), (lon * 1e7).roundToInt())
    }
}
