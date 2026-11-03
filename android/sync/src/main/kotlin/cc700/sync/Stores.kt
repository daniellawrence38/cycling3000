package cc700.sync

/** Persistence the app needs, as interfaces: :app implements them with Room, DataStore and files; tests
 * use the memory versions below. */

/** The bonded unit (protocol 5: the app stores the bonded address and unit name). */
data class PairedUnit(val name: String, val address: String)

interface PairingStore {
    fun load(): PairedUnit?
    fun save(unit: PairedUnit?)
}

/** Where a ride is in the upload queue. */
enum class RideState {
    /** Flagged "time unknown" on the unit: held until Daniel enters its date (spec 10.6). */
    NEEDS_DATE,
    /** Fetched and waiting to upload (also after a 429, a 5xx or no connection; WorkManager retries). */
    WAITING,
    UPLOADED,
    /** Another 4xx: the server's message is kept; Retry puts it back to WAITING. */
    FAILED,
    /** 401: the key is wrong or revoked. A new key puts these back to WAITING. */
    NEEDS_KEY,
}

data class RideRecord(
    val rideId: Long,
    val sizeBytes: Long,
    val durationS: Long,
    val distanceM: Long,
    val flags: Int,
    val state: RideState,
    /** Start time in Unix seconds after any correction; for a time-unknown ride, set with the date. */
    val startUtc: Long,
    /** Seconds added to every FIT time before upload (0 unless the ride was flagged time unknown). */
    val timeOffsetS: Long = 0,
    val message: String = "",
    val activityId: String? = null,
    /** RIDE_MARK has been sent; false after an upload done while the unit was away. */
    val markedOnUnit: Boolean = false,
    val attempts: Int = 0,
) {
    val recovered get() = flags and 2 != 0
    val timeUnknown get() = flags and 4 != 0
    val fileName get() = "ride-$rideId.fit"
}

interface RideStore {
    fun all(): List<RideRecord>
    fun get(rideId: Long): RideRecord?
    fun put(r: RideRecord)
    fun delete(rideId: Long)
}

/** Fetched FIT files, kept until uploaded so the upload does not need the unit. */
interface BlobStore {
    fun write(name: String, bytes: ByteArray)
    fun read(name: String): ByteArray?
    fun delete(name: String)
}

/** A workout imported on the phone: the source text and its CW1 object. */
class LocalWorkout(val idHex: String, val name: String, val totalSecs: Int, val steps: Int, val source: String, val cw1: ByteArray)

/** A route imported on the phone: its CR1 object and what the preview shows. */
class LocalRoute(
    val idHex: String, val name: String, val distM: Long, val ascentM: Int, val climbs: Int,
    val elevationFromFile: Boolean, val cr1: ByteArray,
)

interface LibraryStore {
    fun workouts(): List<LocalWorkout>
    fun putWorkout(w: LocalWorkout)
    fun deleteWorkout(idHex: String)
    fun routes(): List<LocalRoute>
    fun putRoute(r: LocalRoute)
    fun deleteRoute(idHex: String)
}

// ------------------------------------------------------------------ memory versions (tests, previews)
class MemoryPairingStore(private var unit: PairedUnit? = null) : PairingStore {
    override fun load() = unit
    override fun save(unit: PairedUnit?) { this.unit = unit }
}

class MemoryRideStore : RideStore {
    private val m = sortedMapOf<Long, RideRecord>()
    @Synchronized override fun all() = m.values.toList()
    @Synchronized override fun get(rideId: Long) = m[rideId]
    @Synchronized override fun put(r: RideRecord) { m[r.rideId] = r }
    @Synchronized override fun delete(rideId: Long) { m.remove(rideId) }
}

class MemoryBlobStore : BlobStore {
    private val m = HashMap<String, ByteArray>()
    @Synchronized override fun write(name: String, bytes: ByteArray) { m[name] = bytes.copyOf() }
    @Synchronized override fun read(name: String) = m[name]?.copyOf()
    @Synchronized override fun delete(name: String) { m.remove(name) }
}

class MemoryLibraryStore : LibraryStore {
    private val w = LinkedHashMap<String, LocalWorkout>()
    private val r = LinkedHashMap<String, LocalRoute>()
    @Synchronized override fun workouts() = w.values.toList()
    @Synchronized override fun putWorkout(w: LocalWorkout) { this.w[w.idHex] = w }
    @Synchronized override fun deleteWorkout(idHex: String) { w.remove(idHex) }
    @Synchronized override fun routes() = r.values.toList()
    @Synchronized override fun putRoute(r: LocalRoute) { this.r[r.idHex] = r }
    @Synchronized override fun deleteRoute(idHex: String) { r.remove(idHex) }
}
