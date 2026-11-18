package cc700.app.data

import android.content.Context
import android.graphics.BitmapFactory
import android.util.LruCache
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.graphics.asImageBitmap
import okhttp3.OkHttpClient
import okhttp3.Request
import java.io.File

/**
 * OpenStreetMap raster tiles for the route previews, kept in memory and on disk for 30 days so a preview
 * opened again costs nothing. The OSM tile policy asks for a real User-Agent, caching, attribution
 * (shown on the map) and light use; a few dozen tiles per plan for one rider is that. Nothing about the
 * rider is sent beyond which tiles are wanted.
 */
class TileCache(ctx: Context, private val http: OkHttpClient) {
    private val dir = File(ctx.cacheDir, "tiles").apply { mkdirs() }
    private val mem = LruCache<String, ImageBitmap>(96)
    private val maxAgeMs = 30L * 24 * 3600 * 1000

    /** Blocking; call off the main thread. Null when the tile cannot be had (offline, server error). */
    fun get(z: Int, x: Int, y: Int): ImageBitmap? {
        val key = "$z-$x-$y"
        mem.get(key)?.let { return it }
        val f = File(dir, "$key.png")
        val bytes = if (f.isFile && System.currentTimeMillis() - f.lastModified() < maxAgeMs) f.readBytes()
        else runCatching {
            val req = Request.Builder().url("https://tile.openstreetmap.org/$z/$x/$y.png")
                .header("User-Agent", "CC700-companion/0.1 (personal bike computer app)").build()
            http.newCall(req).execute().use { r -> if (r.isSuccessful) r.body?.bytes() else null }
        }.getOrNull()?.also { b -> runCatching { File(dir, "$key.tmp").apply { writeBytes(b); renameTo(f) } } }
            ?: (if (f.isFile) f.readBytes() else null) // a stale tile beats none
        val img = bytes?.let { runCatching { BitmapFactory.decodeByteArray(it, 0, it.size)?.asImageBitmap() }.getOrNull() } ?: return null
        mem.put(key, img)
        return img
    }
}
