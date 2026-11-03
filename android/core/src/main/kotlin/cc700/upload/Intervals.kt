package cc700.upload

import kotlinx.serialization.json.Json
import kotlinx.serialization.json.JsonArray
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.JsonPrimitive
import okhttp3.Credentials
import okhttp3.HttpUrl
import okhttp3.HttpUrl.Companion.toHttpUrl
import okhttp3.MediaType.Companion.toMediaType
import okhttp3.MultipartBody
import okhttp3.OkHttpClient
import okhttp3.Request
import okhttp3.RequestBody.Companion.toRequestBody
import java.time.Instant
import java.time.ZoneOffset
import java.time.format.DateTimeFormatter

/** The outcome of one upload attempt (brief 7.6). */
sealed class UploadResult {
    data class Uploaded(val activityId: String?) : UploadResult()
    /** An activity already starts within 60 s of the ride: mark the ride uploaded, send nothing. */
    data class Duplicate(val activityId: String?) : UploadResult()
    /** 401: stop and ask for a new key. */
    object Unauthorized : UploadResult()
    /** 429 or 5xx (or no connection): retry later with backoff (WorkManager). */
    data class RetryLater(val code: Int) : UploadResult()
    /** Any other 4xx: mark the ride failed with the server's message. */
    data class Failed(val code: Int, val message: String) : UploadResult()
}

/**
 * Intervals.icu upload: `POST /api/v1/athlete/0/activities`, multipart with the FIT file, basic auth with
 * the user `API_KEY` and the personal key as the password (brief 7.6). The key is passed in by the caller
 * from secure storage and is never logged, printed or kept here. The duplicate check lists the activities
 * of the ride's day and compares their `start_date` (UTC); the exact response fields are as documented by
 * Intervals.icu and are Unverified until Daniel's first live upload.
 */
class IntervalsClient(
    private val http: OkHttpClient,
    private val key: () -> String,
    private val base: HttpUrl = "https://intervals.icu".toHttpUrl(),
) {
    private val json = Json { ignoreUnknownKeys = true }
    private fun auth() = Credentials.basic("API_KEY", key())

    private fun activitiesUrl() = base.newBuilder().addPathSegments("api/v1/athlete/0/activities")

    /** Finds an activity starting within 60 s of [startUtc]. Returns its id, "" if found without an id,
     * or null if none. Throws [HttpStatus] on an HTTP error. */
    fun findDuplicate(startUtc: Long): String? {
        val day = DateTimeFormatter.ISO_LOCAL_DATE.withZone(ZoneOffset.UTC)
        val url = activitiesUrl()
            .addQueryParameter("oldest", day.format(Instant.ofEpochSecond(startUtc - 86400)))
            .addQueryParameter("newest", day.format(Instant.ofEpochSecond(startUtc + 86400)))
            .build()
        http.newCall(Request.Builder().url(url).header("Authorization", auth()).build()).execute().use { r ->
            if (!r.isSuccessful) throw HttpStatus(r.code, r.body?.string().orEmpty())
            val arr = json.parseToJsonElement(r.body?.string() ?: "[]") as? JsonArray ?: return null
            for (e in arr) {
                val o = e as? JsonObject ?: continue
                val s = (o["start_date"] as? JsonPrimitive)?.content ?: continue
                val t = runCatching { Instant.parse(s).epochSecond }.getOrNull() ?: continue
                if (kotlin.math.abs(t - startUtc) <= 60) return (o["id"] as? JsonPrimitive)?.content ?: ""
            }
            return null
        }
    }

    class HttpStatus(val code: Int, val body: String) : Exception("HTTP $code")

    fun upload(fit: ByteArray, fileName: String, startUtc: Long): UploadResult {
        try {
            val dup = findDuplicate(startUtc)
            if (dup != null) return UploadResult.Duplicate(dup.ifEmpty { null })
        } catch (e: HttpStatus) {
            return classify(e.code, e.body)
        } catch (e: java.io.IOException) {
            return UploadResult.RetryLater(-1)
        }
        val body = MultipartBody.Builder().setType(MultipartBody.FORM)
            .addFormDataPart("file", fileName, fit.toRequestBody("application/octet-stream".toMediaType()))
            .build()
        val req = Request.Builder().url(activitiesUrl().build()).header("Authorization", auth()).post(body).build()
        return try {
            http.newCall(req).execute().use { r ->
                val text = r.body?.string().orEmpty()
                if (r.isSuccessful) {
                    val id = runCatching {
                        val e = json.parseToJsonElement(text)
                        ((e as? JsonObject)?.get("id") as? JsonPrimitive)?.content
                            ?: (e as? JsonArray)?.let { ((it.firstOrNull() as? JsonObject)?.get("id") as? JsonPrimitive)?.content }
                    }.getOrNull()
                    UploadResult.Uploaded(id)
                } else classify(r.code, text)
            }
        } catch (e: java.io.IOException) {
            UploadResult.RetryLater(-1)
        }
    }

    private fun classify(code: Int, body: String): UploadResult = when {
        code == 401 -> UploadResult.Unauthorized
        code == 429 || code >= 500 -> UploadResult.RetryLater(code)
        else -> UploadResult.Failed(code, message(body).ifBlank { "HTTP $code" })
    }

    private fun message(body: String): String = runCatching {
        val o = json.parseToJsonElement(body) as JsonObject
        (o["error"] as? JsonPrimitive)?.content ?: (o["message"] as? JsonPrimitive)?.content ?: body
    }.getOrDefault(body).take(200)

}
