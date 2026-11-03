package cc700.app.data

import android.content.Context
import com.google.crypto.tink.Aead
import com.google.crypto.tink.KeyTemplates
import com.google.crypto.tink.aead.AeadConfig
import com.google.crypto.tink.integration.android.AndroidKeysetManager
import java.io.File

/**
 * The Intervals.icu API key, encrypted with Tink AEAD under a keyset that is itself wrapped by an Android
 * Keystore key (brief 7.6). The key is never logged, displayed or written anywhere else: the screens
 * only learn whether one is set.
 */
class KeyStore(ctx: Context) {
    private val file = File(ctx.noBackupFilesDir, "intervals.key.enc")
    private val aead: Aead by lazy {
        AeadConfig.register()
        AndroidKeysetManager.Builder()
            .withSharedPref(ctx, "cc700_keyset", "cc700_keyset_prefs")
            .withKeyTemplate(KeyTemplates.get("AES256_GCM"))
            .withMasterKeyUri("android-keystore://cc700_master_key")
            .build()
            .keysetHandle
            .getPrimitive(Aead::class.java)
    }
    private val ad = "intervals.icu api key".toByteArray()

    fun hasKey(): Boolean = file.isFile

    /** Only the upload path calls this. */
    fun get(): String? = if (!file.isFile) null else runCatching { String(aead.decrypt(file.readBytes(), ad)) }.getOrNull()

    fun set(key: String) {
        val tmp = File(file.path + ".tmp")
        tmp.writeBytes(aead.encrypt(key.trim().toByteArray(), ad))
        tmp.renameTo(file)
    }

    fun clear() { file.delete() }
}
