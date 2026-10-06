package com.vitacar.companion

import android.Manifest
import android.content.Context
import android.content.pm.PackageManager
import android.location.Location
import android.location.LocationListener
import android.location.LocationManager
import android.os.Looper

/** GPS del móvil -> mensajes "gps" a la Vita (1 por segundo como mucho). */
class LocationTracker(
    private val context: Context,
    private val onLocation: (Location) -> Unit,
) : LocationListener {

    private val manager = context.getSystemService(Context.LOCATION_SERVICE) as LocationManager
    private var lastSent = 0L
    private var lastBearing = 0f

    fun start() {
        if (context.checkSelfPermission(Manifest.permission.ACCESS_FINE_LOCATION) != PackageManager.PERMISSION_GRANTED)
            return
        try {
            for (provider in listOf(LocationManager.GPS_PROVIDER, LocationManager.NETWORK_PROVIDER)) {
                if (!manager.allProviders.contains(provider)) continue
                manager.getLastKnownLocation(provider)?.let(::onLocationChanged)
                val interval = if (provider == LocationManager.GPS_PROVIDER) 1000L else 5000L
                manager.requestLocationUpdates(provider, interval, 0f, this, Looper.getMainLooper())
            }
        } catch (_: SecurityException) {
        }
    }

    fun stop() {
        manager.removeUpdates(this)
    }

    override fun onLocationChanged(location: Location) {
        onLocation(location)
        val now = System.currentTimeMillis()
        if (now - lastSent < 900) return
        lastSent = now
        // Parado el rumbo no es fiable: se mantiene el último para que la flecha no gire sola.
        if (location.hasBearing() && location.speed > 1.5f) lastBearing = location.bearing
        VitaHub.updateGps(
            VitaHub.msg(
                "gps",
                "lat" to location.latitude,
                "lon" to location.longitude,
                "speed" to if (location.hasSpeed()) location.speed.toDouble() else 0.0,
                "bearing" to lastBearing.toDouble(),
            ),
        )
    }

    @Deprecated("Requerido en API < 29")
    override fun onStatusChanged(provider: String?, status: Int, extras: android.os.Bundle?) = Unit
    override fun onProviderEnabled(provider: String) = Unit
    override fun onProviderDisabled(provider: String) = Unit
}
