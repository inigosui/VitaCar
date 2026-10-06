package com.vitacar.companion

import android.content.Context
import android.location.Geocoder
import android.location.Location
import android.util.Log
import org.json.JSONObject
import java.net.HttpURLConnection
import java.net.URL
import java.util.Locale
import java.util.concurrent.Executors
import java.util.concurrent.ScheduledFuture
import java.util.concurrent.TimeUnit

/** Tiempo actual con Open-Meteo (gratis, sin clave) cada 15 minutos. */
class WeatherFetcher(private val context: Context) {

    private val executor = Executors.newSingleThreadScheduledExecutor()
    private var task: ScheduledFuture<*>? = null
    @Volatile
    private var location: Location? = null
    @Volatile
    private var fetchedOnce = false

    fun start() {
        task = executor.scheduleWithFixedDelay(::fetch, 15, 15, TimeUnit.MINUTES)
    }

    fun stop() {
        task?.cancel(true)
        executor.shutdownNow()
    }

    fun onLocation(loc: Location) {
        location = loc
        if (!fetchedOnce) {
            fetchedOnce = true
            executor.execute(::fetch)
        }
    }

    private fun fetch() {
        val loc = location ?: return
        try {
            val url = URL(
                String.format(
                    Locale.US,
                    "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f" +
                        "&current=temperature_2m,weather_code,is_day" +
                        "&daily=temperature_2m_max,temperature_2m_min&timezone=auto&forecast_days=1",
                    loc.latitude, loc.longitude,
                ),
            )
            val conn = url.openConnection() as HttpURLConnection
            conn.connectTimeout = 10_000
            conn.readTimeout = 10_000
            val body = conn.inputStream.bufferedReader().use { it.readText() }
            val json = JSONObject(body)
            val current = json.getJSONObject("current")
            val daily = json.getJSONObject("daily")
            VitaHub.updateWeather(
                VitaHub.msg(
                    "weather",
                    "temp" to current.getDouble("temperature_2m"),
                    "code" to current.getInt("weather_code"),
                    "is_day" to (current.optInt("is_day", 1) == 1),
                    "max" to daily.getJSONArray("temperature_2m_max").getDouble(0),
                    "min" to daily.getJSONArray("temperature_2m_min").getDouble(0),
                    "place" to placeName(loc),
                ),
            )
        } catch (e: Exception) {
            Log.w("WeatherFetcher", "No se pudo obtener el tiempo", e)
        }
    }

    @Suppress("DEPRECATION")
    private fun placeName(loc: Location): String = try {
        val address = Geocoder(context, Locale.getDefault()).getFromLocation(loc.latitude, loc.longitude, 1)?.firstOrNull()
        address?.locality ?: address?.subAdminArea ?: ""
    } catch (_: Exception) {
        ""
    }
}
