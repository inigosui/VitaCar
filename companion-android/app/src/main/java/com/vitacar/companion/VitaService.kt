package com.vitacar.companion

import android.Manifest
import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.content.pm.PackageManager
import android.content.pm.ServiceInfo
import android.media.projection.MediaProjection
import android.media.projection.MediaProjectionManager
import android.net.wifi.WifiManager
import android.os.BatteryManager
import android.os.Build
import android.os.Handler
import android.os.IBinder
import android.os.Looper
import android.os.PowerManager
import android.os.SystemClock
import android.provider.Settings
import android.util.Log
import org.json.JSONObject
import java.io.IOException
import java.net.DatagramPacket
import java.net.DatagramSocket
import java.net.Inet4Address
import java.net.InetAddress
import java.net.InetSocketAddress
import java.net.NetworkInterface
import java.net.ServerSocket
import java.net.SocketTimeoutException
import kotlin.concurrent.thread

/** Servicio en primer plano: escucha a la Vita y mantiene vivos GPS, llamadas, etc. */
class VitaService : Service() {

    companion object {
        const val PORT = 47474
        private const val BEACON = "VITACAR1"
        private const val QUERY = "VITACAR?"
        private const val BEACON_INTERVAL_MS = 1000L
        private const val CHANNEL_ID = "vitacar"
        private const val NOTIF_ID = 1
        private const val ACTION_STOP = "com.vitacar.companion.STOP"
        private const val ACTION_AUDIO_START = "com.vitacar.companion.AUDIO_START"
        private const val ACTION_AUDIO_STOP = "com.vitacar.companion.AUDIO_STOP"
        private const val EXTRA_RESULT_CODE = "result_code"
        private const val EXTRA_RESULT_DATA = "result_data"
        private const val TAG = "VitaService"

        @Volatile
        var running = false
            private set

        fun start(ctx: Context) {
            ctx.startForegroundService(Intent(ctx, VitaService::class.java))
        }

        fun stop(ctx: Context) {
            ctx.startService(Intent(ctx, VitaService::class.java).setAction(ACTION_STOP))
        }

        /** Con el permiso de "emitir pantalla" recién concedido: empieza a enviar el sonido. */
        fun startAudio(ctx: Context, resultCode: Int, data: Intent) {
            ctx.startForegroundService(
                Intent(ctx, VitaService::class.java).setAction(ACTION_AUDIO_START)
                    .putExtra(EXTRA_RESULT_CODE, resultCode)
                    .putExtra(EXTRA_RESULT_DATA, data),
            )
        }

        fun stopAudio(ctx: Context) {
            if (running) ctx.startService(Intent(ctx, VitaService::class.java).setAction(ACTION_AUDIO_STOP))
        }
    }

    private var server: ServerSocket? = null
    private var wakeLock: PowerManager.WakeLock? = null
    private lateinit var location: LocationTracker
    private lateinit var weather: WeatherFetcher
    private lateinit var tiles: TileFetcher
    private lateinit var calls: CallMonitor
    private var projection: MediaProjection? = null
    private var audio: AudioStreamer? = null
    private val hubListener: () -> Unit = { updateNotification() }

    private val batteryReceiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context, intent: Intent) {
            val level = intent.getIntExtra(BatteryManager.EXTRA_LEVEL, -1)
            val scale = intent.getIntExtra(BatteryManager.EXTRA_SCALE, 100)
            val status = intent.getIntExtra(BatteryManager.EXTRA_STATUS, -1)
            if (level >= 0 && scale > 0) {
                VitaHub.updateBattery(
                    level * 100 / scale,
                    status == BatteryManager.BATTERY_STATUS_CHARGING || status == BatteryManager.BATTERY_STATUS_FULL,
                )
            }
        }
    }

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onCreate() {
        super.onCreate()
        createChannel()
        startInForeground()
        running = true

        val name = Settings.Global.getString(contentResolver, "device_name")
            ?: "${Build.MANUFACTURER} ${Build.MODEL}"
        VitaHub.setDeviceName(name)
        VitaHub.commandHandler = ::handleCommand
        AgendaStore.init(this)
        VitaHub.addListener(hubListener)

        wakeLock = (getSystemService(POWER_SERVICE) as PowerManager)
            .newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "VitaCar::server")
            .apply { acquire() }

        tiles = TileFetcher(this)
        weather = WeatherFetcher(this)
        location = LocationTracker(this) { loc ->
            weather.onLocation(loc)
            RouteNavigator.onLocation(this, loc)
        }
        location.start()
        weather.start()
        calls = CallMonitor(this)
        calls.start()
        registerReceiver(batteryReceiver, IntentFilter(Intent.ACTION_BATTERY_CHANGED))

        thread(name = "vita-server", isDaemon = true) { acceptLoop() }
        thread(name = "vita-discovery", isDaemon = true) { discoveryLoop() }
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        when (intent?.action) {
            ACTION_STOP -> {
                stopSelf()
                return START_NOT_STICKY
            }
            ACTION_AUDIO_START -> startAudio(intent)
            ACTION_AUDIO_STOP -> stopAudio()
        }
        return START_STICKY
    }

    override fun onDestroy() {
        running = false
        stopAudio()
        try {
            server?.close()
        } catch (_: IOException) {
        }
        VitaHub.connection?.close()
        VitaHub.commandHandler = null
        VitaHub.removeListener(hubListener)
        unregisterReceiver(batteryReceiver)
        location.stop()
        weather.stop()
        calls.stop()
        tiles.shutdown()
        wakeLock?.release()
        super.onDestroy()
    }

    // ---------- Sonido por la Vita ----------

    private fun startAudio(intent: Intent) {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.Q) return
        @Suppress("DEPRECATION")
        val data = intent.getParcelableExtra<Intent>(EXTRA_RESULT_DATA) ?: return
        val code = intent.getIntExtra(EXTRA_RESULT_CODE, 0)
        stopAudio()
        if (checkSelfPermission(Manifest.permission.RECORD_AUDIO) != PackageManager.PERMISSION_GRANTED) return
        try {
            // Android exige estar en primer plano como "emisión de pantalla" antes de usar el permiso.
            startInForeground(withProjection = true)
            val mp = getSystemService(MediaProjectionManager::class.java).getMediaProjection(code, data) ?: return
            mp.registerCallback(object : MediaProjection.Callback() {
                // El usuario lo ha parado desde la barra de notificaciones (o Android lo ha cortado).
                override fun onStop() {
                    if (projection === mp) stopAudio()
                }
            }, Handler(Looper.getMainLooper()))
            projection = mp
            val streamer = AudioStreamer(mp)
            if (!streamer.start()) {
                Log.w(TAG, "No se pudo empezar a capturar el sonido")
                stopAudio()
                return
            }
            audio = streamer
            VitaHub.updateAudio(true)
        } catch (e: Exception) {
            Log.e(TAG, "No se pudo activar el sonido por la Vita", e)
            stopAudio()
        }
    }

    private fun stopAudio() {
        val wasActive = projection != null
        audio?.stop()
        audio = null
        projection?.let {
            projection = null
            it.stop()
        }
        if (wasActive) {
            VitaHub.updateAudio(false)
            if (running) startInForeground(withProjection = false)
        }
    }

    private fun acceptLoop() {
        try {
            val srv = ServerSocket().apply {
                reuseAddress = true
                bind(InetSocketAddress(PORT))
            }
            server = srv
            while (running) {
                val socket = srv.accept()
                Log.i(TAG, "Vita conectada desde ${socket.inetAddress?.hostAddress}")
                val conn = VitaConnection(socket, ::onVitaMessage, VitaHub::onClosed)
                VitaHub.onConnected(conn)
                conn.start()
            }
        } catch (e: IOException) {
            if (running) Log.e(TAG, "Servidor detenido", e)
        }
    }

    /**
     * Descubrimiento en la red local, para que la Vita encuentre el móvil en cualquier WiFi:
     * responde a la pregunta de la Vita y, mientras no hay ninguna conectada, se anuncia
     * cada segundo. El MulticastLock evita que la WiFi del móvil descarte los broadcasts.
     */
    private fun discoveryLoop() {
        val wifi = applicationContext.getSystemService(WIFI_SERVICE) as WifiManager
        val lock = wifi.createMulticastLock("VitaCar::discovery").apply {
            setReferenceCounted(false)
            acquire()
        }
        val answer = BEACON.toByteArray(Charsets.US_ASCII)
        val buf = ByteArray(64)
        try {
            DatagramSocket(null).use { socket ->
                socket.reuseAddress = true
                socket.broadcast = true
                socket.soTimeout = BEACON_INTERVAL_MS.toInt()
                socket.bind(InetSocketAddress(PORT))
                var nextBeacon = 0L
                while (running) {
                    val now = SystemClock.elapsedRealtime()
                    if (now >= nextBeacon) {
                        if (VitaHub.connection == null) sendBeacons(socket, answer)
                        nextBeacon = now + BEACON_INTERVAL_MS
                    }
                    val packet = DatagramPacket(buf, buf.size)
                    try {
                        socket.receive(packet)
                    } catch (_: SocketTimeoutException) {
                        continue
                    }
                    if (String(packet.data, 0, packet.length, Charsets.US_ASCII) != QUERY) continue
                    VitaHub.onVitaQuery()
                    try {
                        socket.send(DatagramPacket(answer, answer.size, packet.address, packet.port))
                    } catch (e: IOException) {
                        Log.w(TAG, "No se pudo responder a la Vita", e)
                    }
                }
            }
        } catch (e: Exception) {
            if (running) Log.e(TAG, "Descubrimiento detenido", e)
        } finally {
            lock.release()
        }
    }

    private fun sendBeacons(socket: DatagramSocket, payload: ByteArray) {
        for (target in broadcastTargets()) {
            try {
                socket.send(DatagramPacket(payload, payload.size, target, PORT))
            } catch (_: IOException) {
                // Interfaz sin ruta (p. ej. 255.255.255.255 sin red por defecto).
            }
        }
    }

    /** Dirección de broadcast de cada interfaz activa (WiFi y punto de acceso), más la general. */
    private fun broadcastTargets(): Set<InetAddress> {
        val targets = mutableSetOf<InetAddress>(InetAddress.getByName("255.255.255.255"))
        try {
            for (iface in NetworkInterface.getNetworkInterfaces()) {
                if (!iface.isUp || iface.isLoopback) continue
                iface.interfaceAddresses
                    .filter { it.address is Inet4Address }
                    .mapNotNullTo(targets) { it.broadcast }
            }
        } catch (_: Exception) {
        }
        return targets
    }

    private fun onVitaMessage(conn: VitaConnection, msg: JSONObject) {
        when (msg.optString("t")) {
            "hello" -> VitaHub.sendSnapshot(conn)
            "ping" -> conn.sendJson(VitaHub.msg("pong"))
            "pong" -> Unit
            "tile" -> tiles.fetch(msg.optInt("z"), msg.optInt("x"), msg.optInt("y"))
            else -> handleCommand(msg)
        }
    }

    private fun handleCommand(msg: JSONObject) {
        val listener = NotifListener.instance
        when (msg.optString("t")) {
            "media_cmd" -> MediaCommands.send(this, listener?.activeController(), msg.optString("action"))
            "reply" -> listener?.reply(msg.optString("id"), msg.optString("text"))
            "notif_dismiss" -> {
                listener?.dismiss(msg.optString("id"))
                VitaHub.removeNotif(msg.optString("id"))
            }
            "call_cmd" -> calls.command(msg.optString("action"))
        }
    }

    // ---------- Notificación del servicio ----------

    private fun createChannel() {
        val channel = NotificationChannel(CHANNEL_ID, "Conexión con la Vita", NotificationManager.IMPORTANCE_LOW)
        getSystemService(NotificationManager::class.java).createNotificationChannel(channel)
    }

    private fun buildNotification(): Notification {
        val conn = VitaHub.connection
        val text = if (conn != null) "Vita conectada (${conn.remoteAddress})" else "Esperando a la Vita…"
            .let { if (VitaHub.audioActive) "$it · sonido por la Vita" else it }
        val open = PendingIntent.getActivity(
            this, 0, Intent(this, MainActivity::class.java), PendingIntent.FLAG_IMMUTABLE,
        )
        val stop = PendingIntent.getService(
            this, 1, Intent(this, VitaService::class.java).setAction(ACTION_STOP), PendingIntent.FLAG_IMMUTABLE,
        )
        return Notification.Builder(this, CHANNEL_ID)
            .setSmallIcon(android.R.drawable.stat_sys_data_bluetooth)
            .setContentTitle("VitaCar")
            .setContentText(text)
            .setContentIntent(open)
            .setOngoing(true)
            .addAction(Notification.Action.Builder(null, "Detener", stop).build())
            .build()
    }

    private fun updateNotification() {
        if (running) getSystemService(NotificationManager::class.java).notify(NOTIF_ID, buildNotification())
    }

    private fun startInForeground(withProjection: Boolean = false) {
        val notification = buildNotification()
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            var type = ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE
            if (checkSelfPermission(Manifest.permission.ACCESS_FINE_LOCATION) == PackageManager.PERMISSION_GRANTED)
                type = type or ServiceInfo.FOREGROUND_SERVICE_TYPE_LOCATION
            if (withProjection) type = type or ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PROJECTION
            startForeground(NOTIF_ID, notification, type)
        } else {
            startForeground(NOTIF_ID, notification)
        }
    }
}
