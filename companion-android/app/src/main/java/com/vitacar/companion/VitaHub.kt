package com.vitacar.companion

import android.os.Handler
import android.os.Looper
import org.json.JSONObject
import java.util.concurrent.CopyOnWriteArrayList

/**
 * Punto central: guarda el último estado de cada cosa (música, notificaciones,
 * GPS…) y lo envía a la Vita. Al conectarse una Vita le manda todo de golpe.
 */
object VitaHub {
    @Volatile
    var connection: VitaConnection? = null
        private set

    private val lock = Any()
    private var deviceName = "Móvil"
    private var battery: JSONObject? = null
    private var media: JSONObject? = null
    private var artId = 0
    private var artJpeg: ByteArray? = null
    private val notifs = LinkedHashMap<String, JSONObject>()
    private var gps: JSONObject? = null
    private var nav: JSONObject? = null
    private var weather: JSONObject? = null
    private var call: JSONObject? = null

    /** Ejecuta las órdenes que llegan de la Vita (lo registra VitaService). */
    @Volatile
    var commandHandler: ((JSONObject) -> Unit)? = null

    private val listeners = CopyOnWriteArrayList<() -> Unit>()
    private val mainHandler = Handler(Looper.getMainLooper())

    /** Veces que la Vita ha buscado el móvil en la red (diagnóstico para la pantalla principal). */
    @Volatile
    var vitaQueries = 0
        private set

    fun onVitaQuery() {
        vitaQueries++
        notifyListeners()
    }

    fun addListener(l: () -> Unit) = listeners.add(l)
    fun removeListener(l: () -> Unit) = listeners.remove(l)
    private fun notifyListeners() = mainHandler.post { listeners.forEach { it() } }

    fun msg(type: String, vararg fields: Pair<String, Any?>): JSONObject =
        JSONObject().apply {
            put("t", type)
            fields.forEach { (k, v) -> put(k, v ?: JSONObject.NULL) }
        }

    private fun send(obj: JSONObject) {
        connection?.sendJson(obj)
    }

    // ---------- Conexión ----------

    fun setDeviceName(name: String) {
        deviceName = name
    }

    fun onConnected(conn: VitaConnection) {
        connection?.close()
        connection = conn
        notifyListeners()
    }

    fun onClosed(conn: VitaConnection) {
        if (connection === conn) {
            connection = null
            notifyListeners()
        }
    }

    /** La Vita saludó: le mandamos el estado completo. */
    fun sendSnapshot(conn: VitaConnection) {
        synchronized(lock) {
            conn.sendJson(msg("hello", "v" to 1, "name" to deviceName))
            battery?.let(conn::sendJson)
            artJpeg?.let { conn.sendArt(artId, it) }
            media?.let(conn::sendJson)
            // De la más antigua a la más reciente: la Vita pone arriba la última recibida.
            notifs.values.forEach { conn.sendJson(JSONObject(it.toString()).put("silent", true)) }
            gps?.let(conn::sendJson)
            nav?.let(conn::sendJson)
            weather?.let(conn::sendJson)
            call?.let(conn::sendJson)
        }
    }

    // ---------- Estado que se envía ----------

    fun updateBattery(pct: Int, charging: Boolean) {
        val m = msg("battery", "pct" to pct, "charging" to charging)
        synchronized(lock) { battery = m }
        send(m)
    }

    fun updateMedia(m: JSONObject, newArtId: Int, jpeg: ByteArray?) {
        synchronized(lock) {
            if (newArtId != artId) {
                artId = newArtId
                artJpeg = jpeg
                if (jpeg != null) connection?.sendArt(newArtId, jpeg)
            }
            media = m
        }
        send(m)
    }

    fun putNotif(id: String, m: JSONObject) {
        synchronized(lock) {
            notifs.remove(id)
            notifs[id] = m
            while (notifs.size > 24) notifs.remove(notifs.keys.first())
        }
        send(m)
    }

    fun removeNotif(id: String) {
        val removed = synchronized(lock) { notifs.remove(id) != null }
        if (removed) send(msg("notif_rm", "id" to id))
    }

    fun clearNotifs() {
        synchronized(lock) { notifs.clear() }
    }

    fun updateGps(m: JSONObject) {
        synchronized(lock) { gps = m }
        send(m)
    }

    fun updateNav(m: JSONObject) {
        synchronized(lock) { nav = m }
        send(m)
    }

    fun updateWeather(m: JSONObject) {
        synchronized(lock) { weather = m }
        send(m)
    }

    fun updateCall(m: JSONObject) {
        synchronized(lock) { call = m }
        send(m)
    }

    fun sendTile(z: Int, x: Int, y: Int, png: ByteArray?) {
        val conn = connection ?: return
        if (png != null) conn.sendTile(z, x, y, png)
        else conn.sendJson(msg("tile_err", "z" to z, "x" to x, "y" to y))
    }
}
