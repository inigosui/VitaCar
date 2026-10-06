package com.vitacar.companion

import android.content.Context
import android.util.Log
import java.io.File
import java.net.HttpURLConnection
import java.net.URL
import java.util.concurrent.Executors

/**
 * Descarga teselas de mapa para la Vita y las guarda en caché en el móvil.
 * Por defecto usa los servidores de OpenStreetMap, que piden identificar la
 * app y no descargar zonas en masa: aquí solo se pide lo que la Vita muestra.
 */
class TileFetcher(private val context: Context) {

    companion object {
        const val DEFAULT_URL = "https://tile.openstreetmap.org/{z}/{x}/{y}.png"
        const val PREFS = "vitacar"
        const val KEY_URL = "tile_url"
        private const val USER_AGENT = "VitaCar/0.2 (Android; companion app for PS Vita, personal use)"
        private const val CACHE_DAYS = 30L
    }

    private val executor = Executors.newFixedThreadPool(3)
    private val cacheDir = File(context.cacheDir, "tiles")

    fun fetch(z: Int, x: Int, y: Int) {
        executor.execute {
            VitaHub.sendTile(z, x, y, load(z, x, y))
        }
    }

    fun shutdown() {
        executor.shutdownNow()
    }

    private fun load(z: Int, x: Int, y: Int): ByteArray? {
        val file = File(cacheDir, "$z/$x/$y.png")
        val maxAge = CACHE_DAYS * 24 * 3600 * 1000
        if (file.exists() && System.currentTimeMillis() - file.lastModified() < maxAge)
            return file.readBytes()

        val template = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
            .getString(KEY_URL, DEFAULT_URL) ?: DEFAULT_URL
        val url = template.replace("{z}", "$z").replace("{x}", "$x").replace("{y}", "$y")
        return try {
            val conn = URL(url).openConnection() as HttpURLConnection
            conn.setRequestProperty("User-Agent", USER_AGENT)
            conn.connectTimeout = 10_000
            conn.readTimeout = 10_000
            if (conn.responseCode != 200) {
                Log.w("TileFetcher", "HTTP ${conn.responseCode} para $url")
                return if (file.exists()) file.readBytes() else null
            }
            val bytes = conn.inputStream.use { it.readBytes() }
            file.parentFile?.mkdirs()
            file.writeBytes(bytes)
            bytes
        } catch (e: Exception) {
            // Sin datos: mejor una tesela antigua que ninguna.
            if (file.exists()) file.readBytes() else null
        }
    }
}
