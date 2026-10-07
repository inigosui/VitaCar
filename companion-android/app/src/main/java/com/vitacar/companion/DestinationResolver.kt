package com.vitacar.companion

import android.content.Context
import android.location.Address
import android.location.Geocoder
import android.location.Location
import android.util.Log
import org.json.JSONArray
import java.net.HttpURLConnection
import java.net.URL
import java.net.URLDecoder
import java.net.URLEncoder
import java.util.Locale

/**
 * Convierte en coordenadas lo que comparte Google Maps (u otra app) o lo que se busca
 * en la app. Bloquea: llamar fuera del hilo principal.
 */
object DestinationResolver {

    private const val TAG = "DestinationResolver"
    private const val USER_AGENT = "VitaCar/0.3 (Android; companion app for PS Vita, personal use)"
    private const val MAX_REDIRECTS = 6

    private val URL_RE = Regex("""(https?://|geo:)\S+""")
    private const val NUM = """(-?\d{1,3}\.\d+)"""
    /** De más fiable a menos: la chincheta del sitio, coordenadas en la consulta, centro del mapa. */
    private val COORD_PATTERNS = listOf(
        Regex("""!3d$NUM!4d$NUM"""),
        Regex("""[?&](?:q|query|destination|daddr|ll|center)=(?:loc:)?$NUM,\s*\+?$NUM"""),
        Regex("""^geo:$NUM,$NUM"""),
        Regex("""/(?:search|dir/[^@]*)/$NUM,\s*\+?$NUM"""),
        Regex("""@$NUM,$NUM"""),
    )

    /** Texto compartido: "Nombre\nDirección\nhttps://maps.app.goo.gl/..." o solo el enlace. */
    fun fromSharedText(ctx: Context, text: String, near: Location?): RouteNavigator.Destination? {
        val urls = URL_RE.findAll(text).map { it.value }.toList()
        val words = text.lines().map { it.trim() }.filter { it.isNotEmpty() && URL_RE.find(it)?.range?.first != 0 }
            .map { URL_RE.replace(it, "").trim() }.filter { it.isNotEmpty() }
        var name = words.firstOrNull()
        var query: String? = null

        for (url in urls) {
            for (u in expand(url)) {
                val decoded = decode(u)
                coords(decoded)?.let { (lat, lon) ->
                    return RouteNavigator.Destination(lat, lon, name ?: placeName(decoded) ?: "Destino")
                }
                if (query == null) query = queryText(decoded)
                if (name == null) name = placeName(decoded)
            }
        }
        // Sin coordenadas en el enlace: buscar el nombre y la dirección.
        val search = words.joinToString(", ").ifEmpty { query ?: name ?: return null }
        return search(ctx, search, near).firstOrNull()?.let { it.copy(name = name ?: it.name) }
    }

    /** Búsqueda por texto: primero el buscador de Android y, si no hay, Nominatim (OpenStreetMap). */
    fun search(ctx: Context, query: String, near: Location?): List<RouteNavigator.Destination> {
        val q = query.trim()
        if (q.isEmpty()) return emptyList()
        coords(q)?.let { (lat, lon) -> return listOf(RouteNavigator.Destination(lat, lon, q)) }
        val local = geocoder(ctx, q, near)
        return local.ifEmpty { nominatim(q, near) }
    }

    @Suppress("DEPRECATION")
    private fun geocoder(ctx: Context, q: String, near: Location?): List<RouteNavigator.Destination> {
        if (!Geocoder.isPresent()) return emptyList()
        return try {
            val g = Geocoder(ctx, Locale.getDefault())
            // Primero cerca de ti (unos 100 km); si no hay nada, en cualquier sitio.
            val nearby = near?.let {
                g.getFromLocationName(q, 6, it.latitude - 1, it.longitude - 1.3, it.latitude + 1, it.longitude + 1.3)
            }.orEmpty()
            nearby.ifEmpty { g.getFromLocationName(q, 6).orEmpty() }
                .filter { it.hasLatitude() && it.hasLongitude() }
                .map { RouteNavigator.Destination(it.latitude, it.longitude, label(it)) }
        } catch (e: Exception) {
            Log.w(TAG, "Geocoder falló", e)
            emptyList()
        }
    }

    private fun label(a: Address): String {
        val line = a.getAddressLine(0)
        val feature = a.featureName
        return when {
            line == null -> feature ?: "Destino"
            feature != null && feature.any { it.isLetter() } && !line.startsWith(feature) -> "$feature, $line"
            else -> line
        }
    }

    private fun nominatim(q: String, near: Location?): List<RouteNavigator.Destination> = try {
        var url = "https://nominatim.openstreetmap.org/search?format=jsonv2&limit=6" +
            "&accept-language=${Locale.getDefault().language}&q=" + URLEncoder.encode(q, "UTF-8")
        if (near != null) {
            url += String.format(
                Locale.US, "&viewbox=%.4f,%.4f,%.4f,%.4f",
                near.longitude - 1.3, near.latitude + 1, near.longitude + 1.3, near.latitude - 1,
            )
        }
        val conn = URL(url).openConnection() as HttpURLConnection
        conn.setRequestProperty("User-Agent", USER_AGENT)
        conn.connectTimeout = 10_000
        conn.readTimeout = 10_000
        val arr = JSONArray(conn.inputStream.bufferedReader().use { it.readText() })
        (0 until arr.length()).map {
            val o = arr.getJSONObject(it)
            RouteNavigator.Destination(o.getString("lat").toDouble(), o.getString("lon").toDouble(), o.optString("display_name", q))
        }
    } catch (e: Exception) {
        Log.w(TAG, "Nominatim falló", e)
        emptyList()
    }

    /** El enlace y todas las direcciones a las que redirige (los enlaces cortos maps.app.goo.gl). */
    private fun expand(url: String): List<String> {
        val chain = mutableListOf(url)
        if (!url.startsWith("http") || coords(decode(url)) != null) return chain
        var current = url
        try {
            repeat(MAX_REDIRECTS) {
                val conn = URL(current).openConnection() as HttpURLConnection
                conn.instanceFollowRedirects = false
                conn.setRequestProperty("User-Agent", "Mozilla/5.0 (Linux; Android 14) VitaCar")
                conn.connectTimeout = 10_000
                conn.readTimeout = 10_000
                val code = conn.responseCode
                val next = conn.getHeaderField("Location")
                conn.disconnect()
                if (code !in 300..399 || next == null) return chain
                current = URL(URL(current), next).toString()
                chain += current
                if (coords(decode(current)) != null) return chain
            }
        } catch (e: Exception) {
            Log.w(TAG, "No se pudo abrir $current", e)
        }
        return chain
    }

    /** Decodifica dos veces: la página de consentimiento de Google mete el enlace en "continue=". */
    private fun decode(s: String): String = try {
        URLDecoder.decode(URLDecoder.decode(s.replace("+", "%2B"), "UTF-8").replace("+", "%2B"), "UTF-8")
    } catch (_: Exception) {
        s
    }

    private fun coords(s: String): Pair<Double, Double>? {
        for (re in COORD_PATTERNS) {
            val m = re.find(s) ?: continue
            val lat = m.groupValues[1].toDoubleOrNull() ?: continue
            val lon = m.groupValues[2].toDoubleOrNull() ?: continue
            if (lat in -90.0..90.0 && lon in -180.0..180.0 && !(lat == 0.0 && lon == 0.0)) return lat to lon
        }
        // Coordenadas escritas a mano: "40.4168, -3.7038".
        val plain = Regex("""^\s*$NUM\s*,\s*$NUM\s*$""").find(s) ?: return null
        val lat = plain.groupValues[1].toDouble()
        val lon = plain.groupValues[2].toDouble()
        return if (lat in -90.0..90.0 && lon in -180.0..180.0) lat to lon else null
    }

    /** Texto de búsqueda dentro del enlace (?q=… o geo:0,0?q=…). */
    private fun queryText(url: String): String? =
        Regex("""[?&](?:q|query|daddr|destination)=([^&]+)""").find(url)?.groupValues?.get(1)
            ?.replace('+', ' ')?.substringBefore('(')?.trim()?.takeIf { it.isNotEmpty() }

    /** Nombre del sitio en enlaces largos: /maps/place/Nombre+del+sitio/... */
    private fun placeName(url: String): String? =
        Regex("""/place/([^/@?]+)""").find(url)?.groupValues?.get(1)
            ?.replace('+', ' ')?.trim()?.takeIf { it.isNotEmpty() }
}
