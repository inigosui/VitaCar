package com.vitacar.companion

import android.content.Context
import android.graphics.Color
import android.util.Log
import org.json.JSONArray
import org.json.JSONObject
import java.io.File

/**
 * Notas del calendario, guardadas en el móvil (agenda.json). Cada cambio se manda
 * entero a la Vita, que guarda su propia copia para verlas también sin conexión.
 */
object AgendaStore {

    data class Note(
        val id: Long,
        val date: String,       // "2026-10-09"
        val time: String,       // "HH:mm" o "" (todo el día)
        val title: String,
        val text: String,
        val color: Int,         // índice en COLORS
    )

    /** Mismo orden que AGENDA_PALETTE en la Vita (src/agenda.c). */
    val COLORS = listOf(
        "Verde" to Color.rgb(48, 209, 88),
        "Azul" to Color.rgb(10, 132, 255),
        "Rojo" to Color.rgb(255, 69, 58),
        "Naranja" to Color.rgb(255, 159, 10),
        "Amarillo" to Color.rgb(255, 214, 10),
        "Morado" to Color.rgb(191, 90, 242),
        "Rosa" to Color.rgb(255, 55, 95),
        "Gris" to Color.rgb(142, 142, 147),
    )

    private const val FILE = "agenda.json"
    private val lock = Any()
    private var file: File? = null
    private val notes = mutableListOf<Note>()

    /** Carga las notas (una sola vez) y deja el mensaje listo para la Vita. */
    fun init(context: Context) {
        synchronized(lock) {
            if (file != null) return
            val f = File(context.applicationContext.filesDir, FILE)
            file = f
            try {
                if (f.exists()) {
                    val arr = JSONObject(f.readText()).optJSONArray("notes") ?: JSONArray()
                    for (i in 0 until arr.length()) notes += fromJson(arr.getJSONObject(i))
                }
            } catch (e: Exception) {
                Log.w("AgendaStore", "No se pudo leer la agenda", e)
            }
        }
        VitaHub.updateAgenda(message())
    }

    private fun sorted(list: List<Note>) = list.sortedWith(compareBy({ it.date }, { it.time }, { it.id }))

    fun all(): List<Note> = synchronized(lock) { sorted(notes) }

    fun forDate(date: String): List<Note> = synchronized(lock) { sorted(notes.filter { it.date == date }) }

    /** Añade la nota o, si ya existe una con su id, la sustituye. */
    fun put(note: Note) {
        synchronized(lock) {
            notes.removeAll { it.id == note.id }
            notes += note
            save()
        }
        VitaHub.updateAgenda(message())
    }

    fun delete(id: Long) {
        synchronized(lock) {
            notes.removeAll { it.id == id }
            save()
        }
        VitaHub.updateAgenda(message())
    }

    fun newId(): Long = System.currentTimeMillis()

    private fun toJson(n: Note) = JSONObject().apply {
        put("id", n.id)
        put("date", n.date)
        put("time", n.time)
        put("title", n.title)
        put("text", n.text)
        put("color", n.color)
    }

    private fun fromJson(o: JSONObject) = Note(
        id = o.optLong("id"),
        date = o.optString("date"),
        time = o.optString("time"),
        title = o.optString("title"),
        text = o.optString("text"),
        color = o.optInt("color").coerceIn(0, COLORS.size - 1),
    )

    /** Con lock tomado. */
    private fun save() {
        val f = file ?: return
        try {
            val tmp = File(f.path + ".tmp")
            tmp.writeText(JSONObject().put("notes", JSONArray(notes.map(::toJson))).toString())
            tmp.renameTo(f)
        } catch (e: Exception) {
            Log.w("AgendaStore", "No se pudo guardar la agenda", e)
        }
    }

    /** Mensaje "agenda" con todas las notas (la Vita admite hasta 256). */
    private fun message(): JSONObject {
        val list = synchronized(lock) { sorted(notes) }
        return VitaHub.msg("agenda", "notes" to JSONArray(list.takeLast(256).map(::toJson)))
    }
}
