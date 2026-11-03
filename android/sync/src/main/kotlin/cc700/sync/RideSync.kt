package cc700.sync

import cc700.upload.IntervalsClient
import cc700.upload.UploadResult
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import java.time.Instant
import java.time.ZoneOffset
import java.time.format.DateTimeFormatter

/** What an upload pass did, for the Rides screen and for WorkManager's result. */
data class UploadPass(val uploaded: Int, val duplicates: Int, val failed: Int, val retryLater: Boolean, val needsKey: Boolean)

/**
 * Ride sync and the upload queue (protocol 5, "Ride sync"; brief 7.6; spec 9, 10.6).
 *
 * Fetch: for each ride on the unit without the uploaded flag that the phone does not hold yet, RIDE_OPEN
 * and RIDE_READ with the size and CRC-32 check, then keep the FIT file locally and queue it. A ride
 * flagged "time unknown" is queued as NEEDS_DATE and held until [setStart].
 *
 * Upload: each WAITING ride goes to Intervals.icu (duplicate check first, inside [IntervalsClient]). Only
 * after a 2xx (or a duplicate found) is the ride marked UPLOADED, then RIDE_MARK is sent if the unit is
 * connected, otherwise on the next connect ([markPending]). On 401 the pass stops and the rides wait for
 * a new key; on 429, 5xx or no network they stay WAITING and the pass asks WorkManager to retry with
 * backoff; any other 4xx marks that ride FAILED with the server's message.
 */
class RideSync(
    private val unit: UnitSession,
    private val store: RideStore,
    private val files: BlobStore,
    /** The client with the current key, or null when no key is set. */
    private val uploader: () -> IntervalsClient?,
) {
    private val _rides = MutableStateFlow(sorted())
    val rides: StateFlow<List<RideRecord>> = _rides.asStateFlow()

    private fun sorted() = store.all().sortedByDescending { it.rideId }
    private fun publish() { _rides.value = sorted() }
    private fun put(r: RideRecord) { store.put(r); publish() }

    /** Runs on every connect: mark rides uploaded while the unit was away, then fetch new ones. */
    fun onConnected() {
        runCatching { markPending() }
        runCatching { fetchNew() }
    }

    /** Fetches every ride the unit holds that is not uploaded and not already on the phone. Returns how
     * many were fetched. */
    @Synchronized
    fun fetchNew(): Int {
        val onUnit = unit.rideList()
        var n = 0
        for (ri in onUnit) {
            val have = store.get(ri.rideId)
            if (ri.uploaded) {
                if (have != null && have.state != RideState.UPLOADED) put(have.copy(state = RideState.UPLOADED, markedOnUnit = true, message = ""))
                continue
            }
            if (have != null && files.read(have.fileName) != null) continue
            if (have != null && have.state == RideState.UPLOADED) continue
            val data = unit.rideFetch(ri.rideId)
            val rec = RideRecord(
                rideId = ri.rideId, sizeBytes = ri.size, durationS = ri.durationS, distanceM = ri.distanceM, flags = ri.flags,
                state = if (ri.timeUnknown) RideState.NEEDS_DATE else RideState.WAITING,
                startUtc = if (ri.timeUnknown) (fitStartUtc(data) ?: ri.rideId) else ri.rideId,
            )
            files.write(rec.fileName, data)
            put(rec)
            n++
        }
        publish()
        return n
    }

    /** Daniel's date for a "time unknown" ride: the FIT times are shifted so the ride starts at
     * [startUtc] (Unix seconds), and the ride joins the queue. */
    @Synchronized
    fun setStart(rideId: Long, startUtc: Long) {
        val r = store.get(rideId) ?: return
        val data = files.read(r.fileName) ?: return
        val provisional = fitStartUtc(data) ?: return
        put(r.copy(state = RideState.WAITING, startUtc = startUtc, timeOffsetS = startUtc - provisional, message = ""))
    }

    /** Puts a FAILED or NEEDS_KEY ride back in the queue. */
    @Synchronized
    fun retry(rideId: Long) {
        val r = store.get(rideId) ?: return
        if (r.state == RideState.FAILED || r.state == RideState.NEEDS_KEY) put(r.copy(state = RideState.WAITING, message = ""))
    }

    /** After a new key is saved: every NEEDS_KEY ride goes back to WAITING. */
    @Synchronized
    fun keyChanged() {
        for (r in store.all()) if (r.state == RideState.NEEDS_KEY) store.put(r.copy(state = RideState.WAITING, message = ""))
        publish()
    }

    /** The FIT file as it will be uploaded (times shifted if needed). */
    fun uploadBytes(r: RideRecord): ByteArray? {
        val data = files.read(r.fileName) ?: return null
        return if (r.timeOffsetS != 0L) shiftFitTimes(data, r.timeOffsetS) else data
    }

    /** Uploads every WAITING ride once. */
    @Synchronized
    fun uploadPending(): UploadPass {
        var up = 0; var dup = 0; var failed = 0; var retry = false; var needsKey = false
        val queue = store.all().filter { it.state == RideState.WAITING }.sortedBy { it.rideId }
        if (queue.isEmpty()) return UploadPass(0, 0, 0, false, false)
        val client = uploader()
        if (client == null) {
            for (r in queue) put(r.copy(state = RideState.NEEDS_KEY, message = "Add your Intervals.icu API key in Settings."))
            return UploadPass(0, 0, 0, false, true)
        }
        for (r in queue) {
            val data = uploadBytes(r)
            if (data == null) {
                put(r.copy(state = RideState.FAILED, message = "The ride file is missing on the phone. Sync again."))
                failed++
                continue
            }
            val start = fitStartUtc(data) ?: r.startUtc
            val res = client.upload(data, uploadName(start), start)
            val tried = r.copy(attempts = r.attempts + 1)
            when (res) {
                is UploadResult.Uploaded -> { done(tried, res.activityId, "Uploaded"); up++ }
                is UploadResult.Duplicate -> { done(tried, res.activityId, "Already on Intervals.icu"); dup++ }
                is UploadResult.Unauthorized -> {
                    for (q in store.all().filter { it.state == RideState.WAITING })
                        put(q.copy(state = RideState.NEEDS_KEY, message = "Intervals.icu refused the API key. Enter a new key in Settings."))
                    needsKey = true
                    break
                }
                is UploadResult.RetryLater -> {
                    put(tried.copy(message = if (res.code > 0) "Will retry (HTTP ${res.code})" else "Will retry (no connection)"))
                    retry = true
                }
                is UploadResult.Failed -> { put(tried.copy(state = RideState.FAILED, message = res.message)); failed++ }
            }
        }
        return UploadPass(up, dup, failed, retry, needsKey)
    }

    /** RIDE_MARK for rides uploaded while the unit was not connected. */
    @Synchronized
    fun markPending() {
        if (!unit.connected) return
        for (r in store.all().filter { it.state == RideState.UPLOADED && !it.markedOnUnit }) {
            try {
                unit.rideMark(r.rideId, true)
                put(r.copy(markedOnUnit = true))
            } catch (e: UnitRefused) {
                if (e.status == cc700.link.Status.NOT_FOUND) put(r.copy(markedOnUnit = true)) else return
            }
        }
    }

    private fun done(r: RideRecord, activityId: String?, msg: String) {
        var rec = r.copy(state = RideState.UPLOADED, activityId = activityId, message = msg)
        put(rec)
        if (unit.connected) {
            try {
                unit.rideMark(r.rideId, true)
                rec = rec.copy(markedOnUnit = true)
                put(rec)
            } catch (_: UnitRefused) {
                /* marked on the next connect */
            }
        }
        files.delete(r.fileName)
    }

    companion object {
        private val NAME = DateTimeFormatter.ofPattern("yyyyMMdd-HHmmss").withZone(ZoneOffset.UTC)
        fun uploadName(startUtc: Long) = "cc700-${NAME.format(Instant.ofEpochSecond(startUtc))}.fit"
    }
}
