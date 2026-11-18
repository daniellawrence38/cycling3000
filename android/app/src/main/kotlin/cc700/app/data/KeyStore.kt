package cc700.app.data

import android.content.Context
import com.google.crypto.tink.Aead
import com.google.crypto.tink.KeyTemplates
import com.google.crypto.tink.aead.AeadConfig
import com.google.crypto.tink.integration.android.AndroidKeysetManager
import java.io.File

/** Which service a stored key is for. Each slot has its own file and its own associated data, so a
 * ciphertext cannot be moved from one slot to another. The Intervals.icu slot keeps the file name and
 * associated data it had before slots existed, so a key saved by an earlier version still opens. */
enum class KeySlot(val fileName: String, val associatedData: String) {
    INTERVALS("intervals.key.enc", "intervals.icu api key"),
    ORS("ors.key.enc", "openrouteservice api key"),
    OPENROUTER("openrouter.key.enc", "openrouter api key"),
}

/**
 * An API key, encrypted with Tink AEAD under a keyset that is itself wrapped by an Android Keystore key
 * (brief 7.6). The key is never logged, displayed or written anywhere else: the screens only learn
 * whether one is set.
 */
class KeyStore(ctx: Context, slot: KeySlot = KeySlot.INTERVALS) {
    private val app = ctx.applicationContext
    private val file = File(app.noBackupFilesDir, slot.fileName)
    private val ad = slot.associatedData.toByteArray()

    fun hasKey(): Boolean = file.isFile

    /** Only the code that calls the service reads this. */
    fun get(): String? = if (!file.isFile) null else runCatching { String(aead(app).decrypt(file.readBytes(), ad)) }.getOrNull()

    fun set(key: String) {
        val tmp = File(file.path + ".tmp")
        tmp.writeBytes(aead(app).encrypt(key.trim().toByteArray(), ad))
        tmp.renameTo(file)
    }

    fun clear() { file.delete() }

    private companion object {
        @Volatile private var shared: Aead? = null

        /** One keyset for every slot, created once even when several slots are first used together. */
        fun aead(ctx: Context): Aead = shared ?: synchronized(this) {
            shared ?: run {
                AeadConfig.register()
                AndroidKeysetManager.Builder()
                    .withSharedPref(ctx, "cc700_keyset", "cc700_keyset_prefs")
                    .withKeyTemplate(KeyTemplates.get("AES256_GCM"))
                    .withMasterKeyUri("android-keystore://cc700_master_key")
                    .build()
                    .keysetHandle
                    .getPrimitive(Aead::class.java)
            }.also { shared = it }
        }
    }
}
