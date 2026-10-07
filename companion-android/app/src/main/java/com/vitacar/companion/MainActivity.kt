package com.vitacar.companion

import android.Manifest
import android.app.Activity
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.graphics.Color
import android.graphics.Typeface
import android.graphics.drawable.GradientDrawable
import android.media.projection.MediaProjectionConfig
import android.media.projection.MediaProjectionManager
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.os.PowerManager
import android.provider.Settings
import android.text.InputType
import android.view.Gravity
import android.view.View
import android.view.WindowInsets
import android.view.ViewGroup.LayoutParams.MATCH_PARENT
import android.view.ViewGroup.LayoutParams.WRAP_CONTENT
import android.widget.Button
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import android.widget.Toast
import java.net.Inet4Address
import java.net.NetworkInterface
import kotlin.concurrent.thread

class MainActivity : Activity() {

    companion object {
        private const val REQ_AUDIO_PERM = 10
        private const val REQ_PROJECTION = 20
        private const val KEY_AUDIO = "audio_vita"
    }

    private class PermissionRow(
        val title: String,
        val description: String,
        val isGranted: () -> Boolean,
        val request: () -> Unit,
    ) {
        lateinit var status: TextView
        lateinit var button: Button
    }

    private val bg = Color.rgb(12, 14, 18)
    private val card = Color.rgb(34, 38, 47)
    private val dim = Color.rgb(146, 152, 165)
    private val green = Color.rgb(48, 209, 88)
    private val amber = Color.rgb(255, 179, 64)

    private lateinit var serviceText: TextView
    private lateinit var vitaText: TextView
    private lateinit var ipText: TextView
    private lateinit var toggleButton: Button
    private lateinit var routeText: TextView
    private lateinit var routeStatus: TextView
    private lateinit var clearRouteButton: Button
    private lateinit var modeButtons: Map<RouteNavigator.Mode, Button>
    private lateinit var searchField: EditText
    private lateinit var searchButton: Button
    private lateinit var results: LinearLayout
    private var audioText: TextView? = null
    private var audioButton: Button? = null
    private lateinit var rows: List<PermissionRow>
    private val main = Handler(Looper.getMainLooper())
    private val hubListener: () -> Unit = { refresh() }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        rows = buildPermissionRows()
        setContentView(buildUi())
        handleIntent(intent)
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        setIntent(intent)
        handleIntent(intent)
    }

    // ---------- Sonido por la Vita ----------

    private fun audioWanted() = getSharedPreferences(TileFetcher.PREFS, MODE_PRIVATE).getBoolean(KEY_AUDIO, false)

    private fun setAudioWanted(on: Boolean) =
        getSharedPreferences(TileFetcher.PREFS, MODE_PRIVATE).edit().putBoolean(KEY_AUDIO, on).apply()

    /** Pide los dos permisos (micrófono, una vez; "emitir pantalla", cada vez) y activa el sonido. */
    private fun requestAudio() {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.Q) return
        if (granted(Manifest.permission.RECORD_AUDIO)) askProjection()
        else requestPermissions(arrayOf(Manifest.permission.RECORD_AUDIO), REQ_AUDIO_PERM)
    }

    private fun askProjection() {
        val mpm = getSystemService(MediaProjectionManager::class.java)
        // En Android 14+ se fuerza "toda la pantalla": con una sola app no se captura el sonido de las demás.
        val intent = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.UPSIDE_DOWN_CAKE)
            mpm.createScreenCaptureIntent(MediaProjectionConfig.createConfigForDefaultDisplay())
        else mpm.createScreenCaptureIntent()
        @Suppress("DEPRECATION")
        startActivityForResult(intent, REQ_PROJECTION)
    }

    @Deprecated("Activity sin AndroidX")
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        @Suppress("DEPRECATION")
        super.onActivityResult(requestCode, resultCode, data)
        if (requestCode != REQ_PROJECTION) return
        if (resultCode == RESULT_OK && data != null) {
            setAudioWanted(true)
            VitaService.startAudio(this, resultCode, data)
        } else {
            Toast.makeText(this, "Sin ese permiso el sonido sigue saliendo por el móvil.", Toast.LENGTH_LONG).show()
        }
        main.postDelayed(::refresh, 400)
    }

    // ---------- Destino compartido ----------

    /** Google Maps › Compartir › VitaCar, o un enlace geo: abierto con VitaCar. */
    private fun handleIntent(intent: Intent) {
        val text = when (intent.action) {
            Intent.ACTION_SEND -> intent.getStringExtra(Intent.EXTRA_TEXT)
            Intent.ACTION_VIEW -> intent.dataString
            else -> null
        } ?: return
        intent.action = null    // que no se repita al girar la pantalla
        Toast.makeText(this, "Buscando el sitio compartido…", Toast.LENGTH_SHORT).show()
        thread(name = "share", isDaemon = true) {
            val dest = DestinationResolver.fromSharedText(applicationContext, text, RouteNavigator.lastLocation)
            main.post {
                if (dest != null) setDestination(dest)
                else Toast.makeText(this, "No se ha encontrado ese sitio. Búscalo en «Ruta».", Toast.LENGTH_LONG).show()
            }
        }
    }

    private fun setDestination(dest: RouteNavigator.Destination) {
        RouteNavigator.start(this, dest)
        results.removeAllViews()
        // Sin el servicio no hay GPS ni conexión con la Vita.
        if (!VitaService.running) VitaService.start(this)
        Toast.makeText(this, "Destino: ${dest.name}", Toast.LENGTH_SHORT).show()
        main.postDelayed(::refresh, 400)
    }

    private fun search() {
        val q = searchField.text.toString()
        if (q.isBlank()) return
        searchButton.isEnabled = false
        results.removeAllViews()
        results.addView(text("Buscando…", 13f, dim))
        thread(name = "search", isDaemon = true) {
            val found = DestinationResolver.search(applicationContext, q, RouteNavigator.lastLocation)
            main.post {
                searchButton.isEnabled = true
                results.removeAllViews()
                if (found.isEmpty()) results.addView(text("Sin resultados.", 13f, dim))
                found.forEach { dest ->
                    results.addView(Button(this).apply {
                        text = dest.name
                        isAllCaps = false
                        gravity = Gravity.START or Gravity.CENTER_VERTICAL
                        setOnClickListener { setDestination(dest) }
                    })
                }
            }
        }
    }

    override fun onResume() {
        super.onResume()
        VitaHub.addListener(hubListener)
        refresh()
    }

    override fun onPause() {
        VitaHub.removeListener(hubListener)
        super.onPause()
    }

    override fun onRequestPermissionsResult(requestCode: Int, permissions: Array<out String>, grantResults: IntArray) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults)
        if (requestCode == REQ_AUDIO_PERM) {
            if (granted(Manifest.permission.RECORD_AUDIO)) askProjection()
            else Toast.makeText(this, "Sin ese permiso el sonido sigue saliendo por el móvil.", Toast.LENGTH_LONG).show()
            return
        }
        refresh()
        // El servicio solo usa los permisos que tenía al arrancar: reiniciarlo.
        if (VitaService.running) {
            VitaService.stop(this)
            main.postDelayed({ VitaService.start(this); refresh() }, 600)
        }
    }

    // ---------- Permisos ----------

    private fun granted(vararg perms: String) =
        perms.all { checkSelfPermission(it) == PackageManager.PERMISSION_GRANTED }

    private fun buildPermissionRows(): List<PermissionRow> {
        val list = mutableListOf<PermissionRow>()
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            list += PermissionRow(
                "Notificación del servicio",
                "Para mostrar «VitaCar conectado» mientras funciona.",
                { granted(Manifest.permission.POST_NOTIFICATIONS) },
                { requestPermissions(arrayOf(Manifest.permission.POST_NOTIFICATIONS), 1) },
            )
        }
        list += PermissionRow(
            "Ubicación",
            "Tu posición y velocidad en el mapa de la Vita, y el tiempo de tu zona.",
            { granted(Manifest.permission.ACCESS_FINE_LOCATION) },
            {
                requestPermissions(
                    arrayOf(Manifest.permission.ACCESS_FINE_LOCATION, Manifest.permission.ACCESS_COARSE_LOCATION), 2,
                )
            },
        )
        val phonePerms = arrayOf(
            Manifest.permission.READ_PHONE_STATE,
            Manifest.permission.READ_CALL_LOG,
            Manifest.permission.ANSWER_PHONE_CALLS,
            Manifest.permission.READ_CONTACTS,
        )
        list += PermissionRow(
            "Llamadas y contactos",
            "Ver quién llama, contestar y colgar desde la Vita.",
            { granted(*phonePerms) },
            { requestPermissions(phonePerms, 3) },
        )
        list += PermissionRow(
            "Acceso a notificaciones",
            "Mensajes, música de cualquier app e indicaciones de Google Maps o Waze. " +
                "Si Android dice «Ajuste restringido»: Ajustes › Aplicaciones › VitaCar › ⋮ › " +
                "«Permitir ajustes restringidos», y vuelve a intentarlo.",
            ::hasNotificationAccess,
            { startActivity(Intent(Settings.ACTION_NOTIFICATION_LISTENER_SETTINGS)) },
        )
        list += PermissionRow(
            "Batería sin restricciones",
            "Para que Android no cierre la conexión con la pantalla apagada.",
            { (getSystemService(Context.POWER_SERVICE) as PowerManager).isIgnoringBatteryOptimizations(packageName) },
            {
                startActivity(
                    Intent(Settings.ACTION_REQUEST_IGNORE_BATTERY_OPTIMIZATIONS, Uri.parse("package:$packageName")),
                )
            },
        )
        return list
    }

    private fun hasNotificationAccess(): Boolean =
        Settings.Secure.getString(contentResolver, "enabled_notification_listeners")
            ?.contains(packageName) == true

    // ---------- Interfaz ----------

    private fun dp(v: Int) = (v * resources.displayMetrics.density).toInt()

    private fun text(content: String, size: Float, color: Int = Color.WHITE, bold: Boolean = false) =
        TextView(this).apply {
            text = content
            textSize = size
            setTextColor(color)
            if (bold) typeface = Typeface.DEFAULT_BOLD
        }

    private fun cardLayout() = LinearLayout(this).apply {
        orientation = LinearLayout.VERTICAL
        setPadding(dp(16), dp(14), dp(16), dp(14))
        background = GradientDrawable().apply {
            setColor(card)
            cornerRadius = dp(16).toFloat()
        }
        layoutParams = LinearLayout.LayoutParams(MATCH_PARENT, WRAP_CONTENT).apply { topMargin = dp(12) }
    }

    private fun header(content: String) = text(content, 20f, bold = true).apply { setPadding(0, dp(24), 0, 0) }

    private fun buildUi(): View {
        val root = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(20), dp(28), dp(20), dp(32))
        }

        root.addView(text("VitaCar", 32f, bold = true))
        root.addView(text("Tu PS Vita como pantalla del coche", 15f, dim))

        // Estado
        val status = cardLayout()
        serviceText = text("", 17f, bold = true)
        vitaText = text("", 15f)
        ipText = text("", 13f, dim)
        status.addView(serviceText)
        status.addView(vitaText)
        status.addView(ipText)
        toggleButton = Button(this).apply {
            setOnClickListener {
                if (VitaService.running) {
                    VitaService.stop(this@MainActivity)
                } else {
                    VitaService.start(this@MainActivity)
                    // Android pide el permiso de "emitir pantalla" cada vez.
                    if (audioWanted()) requestAudio()
                }
                main.postDelayed(::refresh, 400)
            }
        }
        status.addView(toggleButton, LinearLayout.LayoutParams(MATCH_PARENT, WRAP_CONTENT).apply { topMargin = dp(10) })
        root.addView(status)

        // Ruta
        root.addView(header("Ruta"))
        val route = cardLayout()
        routeText = text("", 16f, bold = true)
        routeStatus = text("", 13f, dim)
        route.addView(routeText)
        route.addView(routeStatus)
        clearRouteButton = Button(this).apply {
            text = "Quitar ruta"
            setOnClickListener {
                RouteNavigator.clear()
                refresh()
            }
        }
        route.addView(clearRouteButton)
        val modes = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
        modeButtons = RouteNavigator.Mode.entries.associateWith { mode ->
            Button(this).apply {
                setOnClickListener {
                    RouteNavigator.setMode(this@MainActivity, mode)
                    refresh()
                }
                modes.addView(this, LinearLayout.LayoutParams(0, WRAP_CONTENT, 1f))
            }
        }
        route.addView(modes)
        val searchRow = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
        }
        searchField = EditText(this).apply {
            hint = "Destino: dirección o sitio"
            setTextColor(Color.WHITE)
            setHintTextColor(dim)
            textSize = 15f
            isSingleLine = true
            inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_VARIATION_POSTAL_ADDRESS
            setOnEditorActionListener { _, _, _ -> search(); true }
        }
        searchRow.addView(searchField, LinearLayout.LayoutParams(0, WRAP_CONTENT, 1f))
        searchButton = Button(this).apply {
            text = "Buscar"
            setOnClickListener { search() }
        }
        searchRow.addView(searchButton)
        route.addView(searchRow)
        results = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }
        route.addView(results)
        route.addView(
            text(
                "También puedes elegir un sitio en Google Maps y pulsar Compartir › VitaCar. " +
                    "La ruta se calcula con OpenStreetMap (OSRM) y se recalcula si te desvías. " +
                    "Si a la vez navegas con Google Maps, sus indicaciones pueden seguir otro camino.",
                12f, dim,
            ).apply { setPadding(0, dp(8), 0, 0) },
        )
        root.addView(route)

        // Sonido
        root.addView(header("Sonido por la Vita (experimental)"))
        val sound = cardLayout()
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            audioText = text("", 16f, bold = true)
            sound.addView(audioText)
            audioButton = Button(this).apply {
                setOnClickListener {
                    // Mismos casos que el texto del botón en refresh().
                    val turnOff = VitaHub.audioActive || (audioWanted() && !VitaService.running)
                    if (turnOff) {
                        setAudioWanted(false)
                        VitaService.stopAudio(this@MainActivity)
                        main.postDelayed(::refresh, 400)
                    } else {
                        requestAudio()
                    }
                }
            }
            sound.addView(audioButton)
            sound.addView(
                text(
                    "Envía a la Vita el sonido de las apps, para oírlo por su altavoz, por auriculares o por " +
                        "un transmisor Bluetooth. Límites de Android:\n" +
                        "• Spotify y otras apps no dejan capturar su sonido.\n" +
                        "• No se capturan las llamadas ni la voz de Google Maps.\n" +
                        "• Android pide permiso para «emitir pantalla» cada vez que pulsas Iniciar " +
                        "(solo se usa el sonido, no la imagen).\n" +
                        "• Hay un pequeño retraso y gasta más batería. Baja el volumen del móvil si suena por los dos.",
                    12f, dim,
                ).apply { setPadding(0, dp(8), 0, 0) },
            )
        } else {
            sound.addView(text("Necesita Android 10 o superior.", 13f, dim))
        }
        root.addView(sound)

        // Permisos
        root.addView(header("Permisos"))
        for (row in rows) {
            val c = cardLayout()
            val top = LinearLayout(this).apply {
                orientation = LinearLayout.HORIZONTAL
                gravity = Gravity.CENTER_VERTICAL
            }
            row.status = text("", 18f, bold = true)
            top.addView(row.status, LinearLayout.LayoutParams(dp(28), WRAP_CONTENT))
            top.addView(text(row.title, 16f, bold = true), LinearLayout.LayoutParams(0, WRAP_CONTENT, 1f))
            row.button = Button(this).apply {
                text = "Conceder"
                setOnClickListener { row.request() }
            }
            top.addView(row.button)
            c.addView(top)
            c.addView(text(row.description, 13f, dim).apply { setPadding(dp(28), dp(4), 0, 0) })
            root.addView(c)
        }

        val realme = cardLayout()
        realme.addView(text("Realme / Oppo / Xiaomi", 16f, bold = true))
        realme.addView(
            text(
                "Estas marcas cierran apps en segundo plano. En Información de la app › Uso de batería: " +
                    "activa «Permitir actividad en segundo plano» e «Inicio automático».",
                13f, dim,
            ),
        )
        realme.addView(Button(this).apply {
            text = "Abrir información de la app"
            setOnClickListener {
                startActivity(Intent(Settings.ACTION_APPLICATION_DETAILS_SETTINGS, Uri.parse("package:$packageName")))
            }
        })
        root.addView(realme)

        // Cómo conectar
        root.addView(header("Cómo conectar la Vita"))
        val steps = cardLayout()
        listOf(
            "1. Conecta este móvil y la Vita a la misma WiFi de 2,4 GHz (la Vita no ve la de 5 GHz): " +
                "la de casa, la de otro móvil o el punto de acceso de este (sin apagado automático).",
            "2. En la Vita: Ajustes › Red › Configuración de Wi-Fi, y conéctate a esa red.",
            "3. Aquí, pulsa «Iniciar».",
            "4. Abre VitaCar en la Vita: se conectará sola (el punto «Móvil» se pone verde).",
        ).forEach { steps.addView(text(it, 14f).apply { setPadding(0, dp(4), 0, dp(4)) }) }
        root.addView(steps)

        // Mapa
        root.addView(header("Mapa"))
        val map = cardLayout()
        map.addView(
            text(
                "Servidor de teselas. Por defecto, OpenStreetMap (gratis, sin clave). Puedes poner otro, " +
                    "p. ej. MapTiler con tu clave. Usa {z}, {x} e {y}.",
                13f, dim,
            ),
        )
        val prefs = getSharedPreferences(TileFetcher.PREFS, MODE_PRIVATE)
        val urlField = EditText(this).apply {
            setText(prefs.getString(TileFetcher.KEY_URL, TileFetcher.DEFAULT_URL))
            setTextColor(Color.WHITE)
            textSize = 13f
            inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_VARIATION_URI
        }
        map.addView(urlField)
        map.addView(Button(this).apply {
            text = "Guardar"
            setOnClickListener {
                val url = urlField.text.toString().trim().ifEmpty { TileFetcher.DEFAULT_URL }
                prefs.edit().putString(TileFetcher.KEY_URL, url).apply()
                urlField.setText(url)
                Toast.makeText(this@MainActivity, "Guardado", Toast.LENGTH_SHORT).show()
            }
        })
        root.addView(map)

        return ScrollView(this).apply {
            setBackgroundColor(bg)
            addView(root)
            // Android 15 dibuja bajo las barras del sistema: dejar su hueco. Antes de Android 11
            // no existe esta API (y no hace falta: el sistema ya deja el hueco).
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
                setOnApplyWindowInsetsListener { v, insets ->
                    val bars = insets.getInsets(WindowInsets.Type.systemBars())
                    v.setPadding(bars.left, bars.top, bars.right, bars.bottom)
                    insets
                }
            }
        }
    }

    private fun refresh() {
        val running = VitaService.running
        val conn = VitaHub.connection
        serviceText.text = if (running) "● Servicio activo" else "● Servicio detenido"
        serviceText.setTextColor(if (running) green else dim)
        vitaText.text = when {
            conn != null -> "Vita conectada (${conn.remoteAddress})"
            running && VitaHub.vitaQueries > 0 -> "Esperando a la Vita… (te ha buscado ${VitaHub.vitaQueries} veces)"
            running -> "Esperando a la Vita…"
            else -> "Pulsa «Iniciar» para que la Vita pueda conectarse"
        }
        vitaText.setTextColor(if (conn != null) green else if (running) amber else dim)
        ipText.text = localAddresses().let {
            if (it.isEmpty()) "Sin red. Conéctate a una WiFi o activa el punto de acceso."
            else "Direcciones de este móvil: " + it.joinToString("  ·  ")
        }
        toggleButton.text = if (running) "Detener" else "Iniciar"

        val dest = RouteNavigator.destination
        routeText.text = dest?.let { "Hacia ${it.name}" } ?: "Sin destino"
        routeText.setTextColor(if (dest != null) Color.WHITE else dim)
        routeStatus.text = RouteNavigator.status
        routeStatus.visibility = if (RouteNavigator.status.isEmpty()) View.GONE else View.VISIBLE
        clearRouteButton.visibility = if (dest != null) View.VISIBLE else View.GONE
        audioText?.let {
            val wanted = audioWanted()
            it.text = when {
                VitaHub.audioActive -> "● Activado: el sonido de las apps va a la Vita"
                wanted && running -> "● Activado, pero sin permiso: pulsa «Volver a activar»"
                wanted -> "● Activado: se pedirá permiso al pulsar «Iniciar»"
                else -> "● Desactivado: el sonido sale por el móvil"
            }
            it.setTextColor(if (VitaHub.audioActive) green else if (wanted) amber else dim)
        }
        audioButton?.text = when {
            VitaHub.audioActive -> "Desactivar"
            audioWanted() && running -> "Volver a activar"
            audioWanted() -> "Desactivar"
            else -> "Activar"
        }

        val mode = RouteNavigator.mode(this)
        modeButtons.forEach { (m, b) -> b.text = if (m == mode) "● ${m.label}" else m.label }

        for (row in rows) {
            val ok = row.isGranted()
            row.status.text = if (ok) "✓" else "!"
            row.status.setTextColor(if (ok) green else amber)
            row.button.visibility = if (ok) View.GONE else View.VISIBLE
        }
    }

    private fun localAddresses(): List<String> = try {
        NetworkInterface.getNetworkInterfaces().toList()
            .filter { it.isUp && !it.isLoopback }
            .flatMap { iface ->
                iface.inetAddresses.toList().filterIsInstance<Inet4Address>().map { "${it.hostAddress} (${iface.name})" }
            }
    } catch (_: Exception) {
        emptyList()
    }
}
