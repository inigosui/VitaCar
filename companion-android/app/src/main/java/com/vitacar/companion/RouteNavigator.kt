package com.vitacar.companion

import android.content.Context
import android.location.Location
import android.location.LocationManager
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import android.util.Log
import org.json.JSONArray
import org.json.JSONObject
import java.net.HttpURLConnection
import java.net.URL
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import java.util.concurrent.Executors
import kotlin.math.cos
import kotlin.math.roundToLong
import kotlin.math.sqrt

/**
 * Ruta propia hasta un destino, calculada con OSRM (gratis, datos de OpenStreetMap).
 * Se envía entera a la Vita para dibujarla; después, con cada posición, solo lo que
 * queda. Si te desvías, se recalcula. Todo el estado se toca en el hilo principal.
 */
object RouteNavigator {

    data class Destination(val lat: Double, val lon: Double, val name: String)

    enum class Mode(val label: String, val servers: List<String>) {
        CAR(
            "Coche",
            listOf(
                "https://routing.openstreetmap.de/routed-car/route/v1/driving/",
                "https://router.project-osrm.org/route/v1/driving/",
            ),
        ),
        FOOT("A pie", listOf("https://routing.openstreetmap.de/routed-foot/route/v1/driving/")),
    }

    private class Route(
        val lat: DoubleArray,
        val lon: DoubleArray,
        val cum: DoubleArray,       // distancia acumulada desde el inicio hasta cada punto (m)
        val duration: Double,       // segundos de toda la ruta, según OSRM
    ) {
        val length get() = cum.last()
    }

    private const val TAG = "RouteNavigator"
    private const val USER_AGENT = "VitaCar/0.3 (Android; companion app for PS Vita, personal use)"
    private const val KEY_MODE = "route_mode"
    private const val SIMPLIFY_M = 3.0          // Douglas-Peucker: error máximo al quitar puntos
    private const val MAX_POINTS = 6000
    private const val OFF_ROUTE_M = 45.0        // más la precisión del GPS (hasta 30 m)
    private const val OFF_ROUTE_FIXES = 3       // posiciones seguidas fuera de la ruta para recalcular
    private const val REROUTE_MIN_MS = 12_000L
    private const val RETRY_MS = 30_000L        // reintento si falló el cálculo (p. ej. sin datos)
    private const val ARRIVE_M = 35.0
    private const val PROGRESS_MS = 2_000L
    private const val LOOKAHEAD = 300           // puntos por delante donde buscar la posición
    private const val GPS_FRESH_MS = 10_000L    // con GPS reciente se ignoran las posiciones de red

    private val main = Handler(Looper.getMainLooper())
    private val executor = Executors.newSingleThreadExecutor { r -> Thread(r, "route").apply { isDaemon = true } }

    var destination: Destination? = null
        private set
    /** Texto de estado para la app del móvil. */
    var status = ""
        private set

    private var route: Route? = null
    private var generation = 0                  // descarta resultados de destinos anteriores
    private var calculating = false
    private var lastAttempt = 0L
    /** Última posición útil (también para buscar destinos cerca). */
    var lastLocation: Location? = null
        private set
    private var lastGpsFix = 0L
    private var progressIdx = 0
    private var offCount = 0
    private var lastProgressSent = 0L

    fun mode(ctx: Context): Mode =
        if (ctx.getSharedPreferences(TileFetcher.PREFS, Context.MODE_PRIVATE).getString(KEY_MODE, "") == Mode.FOOT.name)
            Mode.FOOT else Mode.CAR

    fun setMode(ctx: Context, mode: Mode) {
        ctx.getSharedPreferences(TileFetcher.PREFS, Context.MODE_PRIVATE).edit().putString(KEY_MODE, mode.name).apply()
        destination?.let { start(ctx, it) }
    }

    /** Nuevo destino: calcula la ruta en cuanto haya una posición. Llamar en el hilo principal. */
    fun start(ctx: Context, dest: Destination) {
        destination = dest
        route = null
        generation++
        calculating = false
        lastAttempt = 0L
        offCount = 0
        progressIdx = 0
        // La ruta anterior desaparece de la Vita hasta que esté la nueva.
        VitaHub.updateRoute(VitaHub.msg("route", "active" to false), null)
        setStatus(if (lastLocation == null) "Esperando tu ubicación para calcular la ruta…" else "Calculando la ruta…")
        lastLocation?.let { calculate(ctx.applicationContext, it) }
    }

    fun clear(arrived: Boolean = false) {
        destination = null
        route = null
        generation++
        calculating = false
        VitaHub.updateRoute(VitaHub.msg("route", "active" to false, "arrived" to arrived), null)
        setStatus(if (arrived) "Has llegado a tu destino." else "")
    }

    /** Cada posición del móvil (hilo principal). */
    fun onLocation(ctx: Context, loc: Location) {
        // La última posición conocida al arrancar puede ser de hace horas.
        if (System.currentTimeMillis() - loc.time > 120_000) return
        val now = SystemClock.elapsedRealtime()
        if (loc.provider == LocationManager.GPS_PROVIDER) lastGpsFix = now
        else if (now - lastGpsFix < GPS_FRESH_MS) return
        lastLocation = loc

        val dest = destination ?: return
        val r = route
        if (r == null) {
            if (!calculating && (lastAttempt == 0L || now - lastAttempt > RETRY_MS))
                calculate(ctx.applicationContext, loc)
            return
        }

        if (distance(loc.latitude, loc.longitude, dest.lat, dest.lon) < ARRIVE_M) {
            clear(arrived = true)
            return
        }

        val (idx, t, off) = locate(r, loc.latitude, loc.longitude)
        val tolerance = OFF_ROUTE_M + if (loc.hasAccuracy()) minOf(loc.accuracy.toDouble(), 30.0) else 30.0
        if (off > tolerance) {
            offCount++
            if (offCount >= OFF_ROUTE_FIXES && !calculating && now - lastAttempt > REROUTE_MIN_MS) {
                setStatus("Te has desviado: recalculando…")
                calculate(ctx.applicationContext, loc)
            }
            return
        }
        offCount = 0
        val moved = idx != progressIdx
        progressIdx = idx
        if (moved || now - lastProgressSent >= PROGRESS_MS) {
            lastProgressSent = now
            val done = r.cum[idx] + t * (r.cum[idx + 1] - r.cum[idx])
            sendProgress(r, idx + 1, r.length - done)
        }
    }

    // ---------- Cálculo ----------

    private fun calculate(ctx: Context, from: Location) {
        val dest = destination ?: return
        val gen = generation
        val mode = mode(ctx)
        calculating = true
        lastAttempt = SystemClock.elapsedRealtime()
        // Si vas en marcha, que la ruta salga hacia donde vas y no te mande dar la vuelta.
        val bearing = if (mode == Mode.CAR && from.hasBearing() && from.speed > 3f) from.bearing.toInt() else null
        executor.execute {
            val result = try {
                fetch(mode, from.latitude, from.longitude, bearing, dest)
                    ?: if (bearing != null) fetch(mode, from.latitude, from.longitude, null, dest) else null
            } catch (e: Exception) {
                Log.w(TAG, "No se pudo calcular la ruta", e)
                null
            }
            main.post { onCalculated(gen, result) }
        }
    }

    private fun onCalculated(gen: Int, r: Route?) {
        if (gen != generation) return
        calculating = false
        val dest = destination ?: return
        if (r == null) {
            setStatus("No se pudo calcular la ruta (¿sin datos?). Se reintentará en 30 s.")
            return
        }
        route = r
        offCount = 0
        progressIdx = 0
        lastProgressSent = SystemClock.elapsedRealtime()

        val pts = JSONArray()
        for (i in r.lat.indices) {
            pts.put(round5(r.lat[i]))
            pts.put(round5(r.lon[i]))
        }
        val m = VitaHub.msg(
            "route",
            "active" to true,
            "dest" to dest.name,
            "dist" to r.length.roundToLong(),
            "dur" to r.duration.roundToLong(),
            "arrive" to arriveTime(r.duration),
            "pts" to pts,
        )
        VitaHub.updateRoute(m, null)
        setStatus("Ruta: ${formatDistance(r.length)} · ${formatDuration(r.duration)}")
    }

    private fun fetch(mode: Mode, lat: Double, lon: Double, bearing: Int?, dest: Destination): Route? {
        val coords = String.format(Locale.US, "%.6f,%.6f;%.6f,%.6f", lon, lat, dest.lon, dest.lat)
        var query = "?overview=full&geometries=geojson&steps=false"
        if (bearing != null) query += "&bearings=$bearing,60;"
        for (server in mode.servers) {
            try {
                val conn = URL(server + coords + query).openConnection() as HttpURLConnection
                conn.setRequestProperty("User-Agent", USER_AGENT)
                conn.connectTimeout = 10_000
                conn.readTimeout = 15_000
                val code = conn.responseCode
                val body = (if (code == 200) conn.inputStream else conn.errorStream)
                    ?.bufferedReader()?.use { it.readText() } ?: ""
                val json = JSONObject(body)
                if (json.optString("code") != "Ok") {
                    Log.w(TAG, "OSRM $server: HTTP $code ${json.optString("code")} ${json.optString("message")}")
                    // Sin ruta posible (p. ej. destino en una isla): otro servidor no lo arreglará.
                    if (json.optString("code") in setOf("NoRoute", "NoSegment")) return null
                    continue
                }
                val best = json.getJSONArray("routes").getJSONObject(0)
                val coordinates = best.getJSONObject("geometry").getJSONArray("coordinates")
                val n = coordinates.length()
                if (n < 2) return null
                val la = DoubleArray(n)
                val lo = DoubleArray(n)
                for (i in 0 until n) {
                    val p = coordinates.getJSONArray(i)
                    lo[i] = p.getDouble(0)
                    la[i] = p.getDouble(1)
                }
                return build(la, lo, best.getDouble("duration"))
            } catch (e: Exception) {
                Log.w(TAG, "OSRM $server falló", e)
            }
        }
        return null
    }

    /** Quita puntos que no cambian la forma y calcula las distancias acumuladas. */
    private fun build(lat: DoubleArray, lon: DoubleArray, duration: Double): Route {
        var tol = SIMPLIFY_M
        var keep = simplify(lat, lon, tol)
        while (keep.size > MAX_POINTS) {
            tol *= 2
            keep = simplify(lat, lon, tol)
        }
        val la = DoubleArray(keep.size) { lat[keep[it]] }
        val lo = DoubleArray(keep.size) { lon[keep[it]] }
        val cum = DoubleArray(keep.size)
        for (i in 1 until keep.size) cum[i] = cum[i - 1] + distance(la[i - 1], lo[i - 1], la[i], lo[i])
        return Route(la, lo, cum, duration)
    }

    /** Douglas-Peucker con pila (sin recursión: las rutas largas tienen miles de puntos). */
    private fun simplify(lat: DoubleArray, lon: DoubleArray, tol: Double): IntArray {
        val n = lat.size
        val keep = BooleanArray(n)
        keep[0] = true
        keep[n - 1] = true
        val stack = ArrayDeque<Pair<Int, Int>>()
        stack.addLast(0 to n - 1)
        while (stack.isNotEmpty()) {
            val (a, b) = stack.removeLast()
            if (b - a < 2) continue
            var worst = -1
            var worstDist = tol
            for (i in a + 1 until b) {
                val d = segmentDistance(lat[i], lon[i], lat[a], lon[a], lat[b], lon[b]).first
                if (d > worstDist) {
                    worstDist = d
                    worst = i
                }
            }
            if (worst >= 0) {
                keep[worst] = true
                stack.addLast(a to worst)
                stack.addLast(worst to b)
            }
        }
        return (0 until n).filter { keep[it] }.toIntArray()
    }

    // ---------- Progreso ----------

    /**
     * Tramo de la ruta más cercano a la posición: (índice del tramo, fracción recorrida
     * del tramo, distancia a la ruta en metros). Primero mira un poco por delante del último
     * tramo conocido, para no saltar a otra parte de la ruta que pase cerca (ida y vuelta).
     */
    private fun locate(r: Route, lat: Double, lon: Double): Triple<Int, Double, Double> {
        fun search(from: Int, to: Int): Triple<Int, Double, Double> {
            var best = Triple(from, 0.0, Double.MAX_VALUE)
            for (i in from until to) {
                val (d, t) = segmentDistance(lat, lon, r.lat[i], r.lon[i], r.lat[i + 1], r.lon[i + 1])
                if (d < best.third) best = Triple(i, t, d)
            }
            return best
        }
        val last = r.lat.size - 1
        val near = search(maxOf(0, progressIdx - 2), minOf(last, progressIdx + LOOKAHEAD))
        if (near.third <= OFF_ROUTE_M) return near
        val all = search(0, last)
        return if (all.third < near.third) all else near
    }

    private fun sendProgress(r: Route, idx: Int, left: Double) {
        val dur = if (r.length > 0) r.duration * left / r.length else 0.0
        VitaHub.updateRoute(
            null,
            VitaHub.msg(
                "route_left",
                "dist" to left.roundToLong(),
                "dur" to dur.roundToLong(),
                "arrive" to arriveTime(dur),
                "idx" to idx,
            ),
        )
        setStatus("Quedan ${formatDistance(left)} · ${formatDuration(dur)}")
    }

    // ---------- Utilidades ----------

    private fun setStatus(s: String) {
        status = s
        VitaHub.notifyListeners()
    }

    private fun round5(v: Double) = (v * 1e5).roundToLong() / 1e5

    private fun arriveTime(seconds: Double): String =
        SimpleDateFormat("HH:mm", Locale.getDefault()).format(Date(System.currentTimeMillis() + (seconds * 1000).toLong()))

    fun formatDistance(m: Double): String = when {
        m < 1000 -> "${(m / 10).roundToLong() * 10} m"
        m < 100_000 -> String.format(Locale("es", "ES"), "%.1f km", m / 1000)
        else -> "${(m / 1000).roundToLong()} km"
    }

    fun formatDuration(s: Double): String {
        val min = maxOf(1L, (s / 60).roundToLong())
        return if (min < 60) "$min min" else String.format(Locale.US, "%d h %02d min", min / 60, min % 60)
    }

    /** Distancia en metros (aproximación plana, de sobra para distancias cortas). */
    private fun distance(lat1: Double, lon1: Double, lat2: Double, lon2: Double): Double {
        val k = 111_320.0
        val dx = (lon2 - lon1) * k * cos(Math.toRadians((lat1 + lat2) / 2))
        val dy = (lat2 - lat1) * k
        return sqrt(dx * dx + dy * dy)
    }

    /** Distancia del punto p al tramo a-b en metros, y fracción (0..1) del tramo más cercana. */
    private fun segmentDistance(
        pLat: Double, pLon: Double, aLat: Double, aLon: Double, bLat: Double, bLon: Double,
    ): Pair<Double, Double> {
        val k = 111_320.0
        val kx = k * cos(Math.toRadians(pLat))
        val ax = (aLon - pLon) * kx
        val ay = (aLat - pLat) * k
        val bx = (bLon - pLon) * kx
        val by = (bLat - pLat) * k
        val dx = bx - ax
        val dy = by - ay
        val len2 = dx * dx + dy * dy
        val t = if (len2 < 1e-9) 0.0 else (-(ax * dx + ay * dy) / len2).coerceIn(0.0, 1.0)
        val cx = ax + t * dx
        val cy = ay + t * dy
        return sqrt(cx * cx + cy * cy) to t
    }
}
