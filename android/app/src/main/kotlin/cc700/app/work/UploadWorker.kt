package cc700.app.work

import android.content.Context
import androidx.work.BackoffPolicy
import androidx.work.Constraints
import androidx.work.ExistingWorkPolicy
import androidx.work.NetworkType
import androidx.work.OneTimeWorkRequestBuilder
import androidx.work.WorkManager
import androidx.work.Worker
import androidx.work.WorkerParameters
import cc700.app.graph
import java.util.concurrent.TimeUnit

/**
 * One upload pass over the queue (brief 7.6). 429, 5xx and no connection come back as retry, which
 * WorkManager repeats with exponential backoff from 30 s; 401 and other 4xx end the pass, leaving the
 * rides marked for the Rides screen.
 */
class UploadWorker(ctx: Context, params: WorkerParameters) : Worker(ctx, params) {
    override fun doWork(): Result {
        val pass = applicationContext.graph.rides.uploadPending()
        return if (pass.retryLater) Result.retry() else Result.success()
    }

    companion object {
        /** [replace] restarts a pass waiting in backoff (Sync now, a new key, Retry). */
        fun enqueue(ctx: Context, replace: Boolean) {
            val req = OneTimeWorkRequestBuilder<UploadWorker>()
                .setConstraints(Constraints.Builder().setRequiredNetworkType(NetworkType.CONNECTED).build())
                .setBackoffCriteria(BackoffPolicy.EXPONENTIAL, 30, TimeUnit.SECONDS)
                .build()
            WorkManager.getInstance(ctx).enqueueUniqueWork("upload", if (replace) ExistingWorkPolicy.REPLACE else ExistingWorkPolicy.KEEP, req)
        }
    }
}
