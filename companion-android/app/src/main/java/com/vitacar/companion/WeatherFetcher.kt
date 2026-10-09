package com.vitacar.companion

import android.content.Context
import android.location.Geocoder
import android.location.Location
import android.util.Log
import org.json.JSONArray
import org.json.JSONObject
import java.net.HttpURLConnection
import java.net.URL
import java.util.Locale
import java.util.concurrent.Executors
import java.util.concurrent.ScheduledFuture
import java.util.concurrent.TimeUnit

/** Tiempo actual y pronóstico de 16 días con Open-Meteo (gratis, sin clave) cada 15 minutos. */
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
                        "&daily=weather_code,temperature_2m_max,temperature_2m_min&timezone=auto&forecast_days=16",
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
            val dates = daily.getJSONArray("time")
            val codes = daily.getJSONArray("weather_code")
            val maxs = daily.getJSONArray("temperature_2m_max")
            val mins = daily.getJSONArray("temperature_2m_min")
            val days = JSONArray()
            for (i in 0 until dates.length()) {
                // Los últimos días a veces llegan sin datos (null).
                if (codes.isNull(i) || maxs.isNull(i) || mins.isNull(i)) continue
                days.put(
                    JSONObject()
                        .put("date", dates.getString(i))
                        .put("code", codes.getInt(i))
                        .put("max", maxs.getDouble(i))
                        .put("min", mins.getDouble(i)),
                )
            }
            VitaHub.updateWeather(
                VitaHub.msg(
                    "weather",
                    "temp" to current.getDouble("temperature_2m"),
                    "code" to current.getInt("weather_code"),
                    "is_day" to (current.optInt("is_day", 1) == 1),
                    "max" to maxs.getDouble(0),
                    "min" to mins.getDouble(0),
                    "place" to placeName(loc),
                    "days" to days,
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
