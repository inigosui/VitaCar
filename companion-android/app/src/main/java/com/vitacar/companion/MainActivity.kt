package com.vitacar.companion

import android.Manifest
import android.app.Activity
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.graphics.Color
import android.graphics.Typeface
import android.graphics.drawable.GradientDrawable
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

class MainActivity : Activity() {

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
    private lateinit var rows: List<PermissionRow>
    private val main = Handler(Looper.getMainLooper())
    private val hubListener: () -> Unit = { refresh() }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        rows = buildPermissionRows()
        setContentView(buildUi())
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
                if (VitaService.running) VitaService.stop(this@MainActivity) else VitaService.start(this@MainActivity)
                main.postDelayed(::refresh, 400)
            }
        }
        status.addView(toggleButton, LinearLayout.LayoutParams(MATCH_PARENT, WRAP_CONTENT).apply { topMargin = dp(10) })
        root.addView(status)

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
            "1. Activa el punto de acceso del móvil. En sus ajustes elige la banda de 2,4 GHz " +
                "(la Vita no ve la de 5 GHz) y desactiva el apagado automático.",
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
            // Android 15 dibuja bajo las barras del sistema: dejar su hueco.
            setOnApplyWindowInsetsListener { v, insets ->
                val bars = insets.getInsets(WindowInsets.Type.systemBars())
                v.setPadding(bars.left, bars.top, bars.right, bars.bottom)
                insets
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
            running -> "Esperando a la Vita…"
            else -> "Pulsa «Iniciar» para que la Vita pueda conectarse"
        }
        vitaText.setTextColor(if (conn != null) green else if (running) amber else dim)
        ipText.text = localAddresses().let {
            if (it.isEmpty()) "Sin red. Activa el punto de acceso."
            else "Direcciones de este móvil: " + it.joinToString("  ·  ")
        }
        toggleButton.text = if (running) "Detener" else "Iniciar"

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
