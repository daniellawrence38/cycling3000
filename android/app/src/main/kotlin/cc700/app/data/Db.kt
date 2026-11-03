package cc700.app.data

import android.content.Context
import androidx.room.Dao
import androidx.room.Database
import androidx.room.Entity
import androidx.room.Insert
import androidx.room.OnConflictStrategy
import androidx.room.PrimaryKey
import androidx.room.Query
import androidx.room.Room
import androidx.room.RoomDatabase
import cc700.dem.ElevationCache
import cc700.sync.BlobStore
import cc700.sync.LibraryStore
import cc700.sync.LocalRoute
import cc700.sync.LocalWorkout
import cc700.sync.RideRecord
import cc700.sync.RideState
import cc700.sync.RideStore
import java.io.File

// ------------------------------------------------------------------ entities
@Entity(tableName = "ride")
data class RideEntity(
    @PrimaryKey val rideId: Long,
    val sizeBytes: Long,
    val durationS: Long,
    val distanceM: Long,
    val flags: Int,
    val state: String,
    val startUtc: Long,
    val timeOffsetS: Long,
    val message: String,
    val activityId: String?,
    val markedOnUnit: Boolean,
    val attempts: Int,
)

@Entity(tableName = "workout")
class WorkoutEntity(
    @PrimaryKey val idHex: String,
    val name: String,
    val totalSecs: Int,
    val steps: Int,
    val source: String,
    val cw1: ByteArray,
    val addedAt: Long,
)

@Entity(tableName = "route")
class RouteEntity(
    @PrimaryKey val idHex: String,
    val name: String,
    val distM: Long,
    val ascentM: Int,
    val climbs: Int,
    val elevationFromFile: Boolean,
    val cr1: ByteArray,
    val addedAt: Long,
)

/** Elevation samples keyed by a 1e-4 degree cell (brief 7.4). */
@Entity(tableName = "elevation")
data class ElevationEntity(@PrimaryKey val cell: Long, val metres: Double)

// ------------------------------------------------------------------ DAOs
@Dao
interface RideDao {
    @Query("SELECT * FROM ride") fun all(): List<RideEntity>
    @Query("SELECT * FROM ride WHERE rideId = :id") fun get(id: Long): RideEntity?
    @Insert(onConflict = OnConflictStrategy.REPLACE) fun put(r: RideEntity)
    @Query("DELETE FROM ride WHERE rideId = :id") fun delete(id: Long)
}

@Dao
interface LibraryDao {
    @Query("SELECT * FROM workout ORDER BY addedAt DESC") fun workouts(): List<WorkoutEntity>
    @Insert(onConflict = OnConflictStrategy.REPLACE) fun putWorkout(w: WorkoutEntity)
    @Query("DELETE FROM workout WHERE idHex = :id") fun deleteWorkout(id: String)
    @Query("SELECT * FROM route ORDER BY addedAt DESC") fun routes(): List<RouteEntity>
    @Insert(onConflict = OnConflictStrategy.REPLACE) fun putRoute(r: RouteEntity)
    @Query("DELETE FROM route WHERE idHex = :id") fun deleteRoute(id: String)
}

@Dao
interface ElevationDao {
    @Query("SELECT metres FROM elevation WHERE cell = :cell") fun get(cell: Long): Double?
    @Insert(onConflict = OnConflictStrategy.REPLACE) fun put(e: ElevationEntity)
}

@Database(entities = [RideEntity::class, WorkoutEntity::class, RouteEntity::class, ElevationEntity::class], version = 1,
    exportSchema = false)
abstract class AppDb : RoomDatabase() {
    abstract fun rides(): RideDao
    abstract fun library(): LibraryDao
    abstract fun elevation(): ElevationDao

    companion object {
        // Main-thread reads are allowed: the tables are a few hundred rows at most and every write runs
        // on a background thread.
        fun open(ctx: Context): AppDb =
            Room.databaseBuilder(ctx, AppDb::class.java, "cc700.db").allowMainThreadQueries().build()
    }
}

// ------------------------------------------------------------------ the :sync interfaces over Room
class RoomRideStore(private val dao: RideDao) : RideStore {
    override fun all() = dao.all().map { it.toRecord() }
    override fun get(rideId: Long) = dao.get(rideId)?.toRecord()
    override fun put(r: RideRecord) = dao.put(RideEntity(r.rideId, r.sizeBytes, r.durationS, r.distanceM, r.flags, r.state.name,
        r.startUtc, r.timeOffsetS, r.message, r.activityId, r.markedOnUnit, r.attempts))
    override fun delete(rideId: Long) = dao.delete(rideId)

    private fun RideEntity.toRecord() = RideRecord(rideId, sizeBytes, durationS, distanceM, flags,
        runCatching { RideState.valueOf(state) }.getOrDefault(RideState.WAITING), startUtc, timeOffsetS, message, activityId,
        markedOnUnit, attempts)
}

class RoomLibraryStore(private val dao: LibraryDao) : LibraryStore {
    override fun workouts() = dao.workouts().map { LocalWorkout(it.idHex, it.name, it.totalSecs, it.steps, it.source, it.cw1) }
    override fun putWorkout(w: LocalWorkout) =
        dao.putWorkout(WorkoutEntity(w.idHex, w.name, w.totalSecs, w.steps, w.source, w.cw1, System.currentTimeMillis()))
    override fun deleteWorkout(idHex: String) = dao.deleteWorkout(idHex)
    override fun routes() = dao.routes().map { LocalRoute(it.idHex, it.name, it.distM, it.ascentM, it.climbs, it.elevationFromFile, it.cr1) }
    override fun putRoute(r: LocalRoute) =
        dao.putRoute(RouteEntity(r.idHex, r.name, r.distM, r.ascentM, r.climbs, r.elevationFromFile, r.cr1, System.currentTimeMillis()))
    override fun deleteRoute(idHex: String) = dao.deleteRoute(idHex)
}

class RoomElevationCache(private val dao: ElevationDao) : ElevationCache {
    override fun get(key: Long) = dao.get(key)
    override fun put(key: Long, value: Double) = dao.put(ElevationEntity(key, value))
}

/** Fetched FIT files in the app's private storage until they are uploaded. */
class FileBlobStore(private val dir: File) : BlobStore {
    init { dir.mkdirs() }
    override fun write(name: String, bytes: ByteArray) {
        val tmp = File(dir, "$name.tmp")
        tmp.writeBytes(bytes)
        tmp.renameTo(File(dir, name))
    }
    override fun read(name: String): ByteArray? = File(dir, name).takeIf { it.isFile }?.readBytes()
    override fun delete(name: String) { File(dir, name).delete() }
}
