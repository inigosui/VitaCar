package com.vitacar.companion

import android.app.Activity
import android.app.AlertDialog
import android.app.DatePickerDialog
import android.app.TimePickerDialog
import android.graphics.Color
import android.graphics.Typeface
import android.graphics.drawable.GradientDrawable
import android.os.Build
import android.os.Bundle
import android.text.InputFilter
import android.text.InputType
import android.text.SpannableString
import android.text.Spanned
import android.text.style.RelativeSizeSpan
import android.view.Gravity
import android.view.View
import android.view.ViewGroup.LayoutParams.MATCH_PARENT
import android.view.ViewGroup.LayoutParams.WRAP_CONTENT
import android.view.WindowInsets
import android.widget.Button
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import android.widget.Toast
import java.time.LocalDate
import java.time.temporal.ChronoUnit
import kotlin.math.roundToInt

/** Calendario con notas por colores (se ven en la app Agenda de la Vita) y pronóstico del día. */
class AgendaActivity : Activity() {

    companion object {
        val MONTHS = listOf(
            "enero", "febrero", "marzo", "abril", "mayo", "junio",
            "julio", "agosto", "septiembre", "octubre", "noviembre", "diciembre",
        )
        val WEEKDAYS = listOf("lunes", "martes", "miércoles", "jueves", "viernes", "sábado", "domingo")

        /** Códigos WMO de Open-Meteo, con los mismos textos que la Vita. */
        fun weatherText(code: Int) = when {
            code == 0 -> "Despejado"
            code <= 2 -> "Poco nuboso"
            code == 3 -> "Nublado"
            code <= 48 -> "Niebla"
            code <= 57 -> "Llovizna"
            code <= 67 -> "Lluvia"
            code <= 77 -> "Nieve"
            code <= 82 -> "Chubascos"
            code <= 86 -> "Chubascos de nieve"
            else -> "Tormenta"
        }

        /** "Poco nuboso · 21° / 12°" para ese día, o null si no hay pronóstico. */
        fun forecastFor(date: LocalDate): String? {
            val days = VitaHub.lastWeather()?.optJSONArray("days") ?: return null
            for (i in 0 until days.length()) {
                val d = days.getJSONObject(i)
                if (d.optString("date") == date.toString()) {
                    return "${weatherText(d.optInt("code"))} · " +
                        "${d.optDouble("max").roundToInt()}° / ${d.optDouble("min").roundToInt()}°"
                }
            }
            return null
        }

        fun capitalize(s: String) = s.replaceFirstChar { it.uppercase() }

        fun longDate(d: LocalDate) =
            "${capitalize(WEEKDAYS[d.dayOfWeek.value - 1])}, ${d.dayOfMonth} de ${MONTHS[d.monthValue - 1]}"
    }

    private val bg = Color.rgb(12, 14, 18)
    private val card = Color.rgb(34, 38, 47)
    private val dim = Color.rgb(146, 152, 165)
    private val accent = Color.rgb(0, 190, 180)

    private var selected: LocalDate = LocalDate.now()
    private var lastColor = 0

    private lateinit var monthTitle: TextView
    private lateinit var grid: LinearLayout
    private lateinit var dayTitle: TextView
    private lateinit var weatherText: TextView
    private lateinit var notesList: LinearLayout
    private val hubListener: () -> Unit = { refresh() }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        AgendaStore.init(this)
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

    // ---------- Interfaz ----------

    private fun dp(v: Int) = (v * resources.displayMetrics.density).toInt()

    private fun text(content: String, size: Float, color: Int = Color.WHITE, bold: Boolean = false) =
        TextView(this).apply {
            text = content
            textSize = size
            setTextColor(color)
            if (bold) typeface = Typeface.DEFAULT_BOLD
        }

    private fun rounded(color: Int, radius: Int, strokeColor: Int? = null) = GradientDrawable().apply {
        setColor(color)
        cornerRadius = dp(radius).toFloat()
        if (strokeColor != null) setStroke(dp(2), strokeColor)
    }

    /** Texto oscuro sobre los colores claros (naranja, amarillo). */
    private fun textOn(color: Int): Int {
        val lum = 0.299 * Color.red(color) + 0.587 * Color.green(color) + 0.114 * Color.blue(color)
        return if (lum > 165) bg else Color.WHITE
    }

    private fun buildUi(): View {
        val root = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(16), dp(24), dp(16), dp(32))
        }
        root.addView(text("Agenda", 28f, bold = true))
        root.addView(
            text("Las notas salen en la app Agenda de la Vita, con su color en el calendario.", 13f, dim)
                .apply { setPadding(0, 0, 0, dp(12)) },
        )

        // Cabecera del mes: ‹ Octubre 2026 › Hoy
        val head = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
        }
        head.addView(Button(this).apply {
            text = "‹"
            textSize = 20f
            setOnClickListener { selected = selected.minusMonths(1); refresh() }
        }, LinearLayout.LayoutParams(dp(52), WRAP_CONTENT))
        monthTitle = text("", 20f, bold = true).apply { gravity = Gravity.CENTER }
        head.addView(monthTitle, LinearLayout.LayoutParams(0, WRAP_CONTENT, 1f))
        head.addView(Button(this).apply {
            text = "›"
            textSize = 20f
            setOnClickListener { selected = selected.plusMonths(1); refresh() }
        }, LinearLayout.LayoutParams(dp(52), WRAP_CONTENT))
        head.addView(Button(this).apply {
            text = "Hoy"
            isAllCaps = false
            setOnClickListener { selected = LocalDate.now(); refresh() }
        })
        root.addView(head)

        val dow = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
        listOf("L", "M", "X", "J", "V", "S", "D").forEach {
            dow.addView(
                text(it, 13f, dim).apply { gravity = Gravity.CENTER },
                LinearLayout.LayoutParams(0, WRAP_CONTENT, 1f),
            )
        }
        root.addView(dow, LinearLayout.LayoutParams(MATCH_PARENT, WRAP_CONTENT).apply { topMargin = dp(8) })

        grid = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }
        root.addView(grid)

        // Día elegido
        dayTitle = text("", 20f, bold = true).apply { setPadding(0, dp(20), 0, dp(6)) }
        root.addView(dayTitle)
        weatherText = text("", 15f).apply {
            setPadding(dp(16), dp(12), dp(16), dp(12))
            background = rounded(card, 14)
        }
        root.addView(weatherText, LinearLayout.LayoutParams(MATCH_PARENT, WRAP_CONTENT))

        notesList = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }
        root.addView(notesList)

        root.addView(Button(this).apply {
            text = "Añadir nota"
            setOnClickListener { editNote(null) }
        }, LinearLayout.LayoutParams(MATCH_PARENT, WRAP_CONTENT).apply { topMargin = dp(12) })

        return ScrollView(this).apply {
            setBackgroundColor(bg)
            addView(root)
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
        monthTitle.text = "${capitalize(MONTHS[selected.monthValue - 1])} ${selected.year}"
        buildGrid()

        dayTitle.text = longDate(selected)
        val today = LocalDate.now()
        val diff = ChronoUnit.DAYS.between(today, selected)
        val forecast = forecastFor(selected)
        weatherText.text = when {
            forecast != null -> {
                val place = VitaHub.lastWeather()?.optString("place").orEmpty()
                if (place.isNotEmpty()) "$forecast\n$place" else forecast
            }
            VitaHub.lastWeather() == null -> "Pulsa «Iniciar» en la pantalla principal para tener el pronóstico."
            diff < 0 -> "Día pasado: sin pronóstico."
            else -> "Aún no hay pronóstico para ese día (llega hasta unos 15 días)."
        }
        weatherText.setTextColor(if (forecast != null) Color.WHITE else dim)

        notesList.removeAllViews()
        val notes = AgendaStore.forDate(selected.toString())
        if (notes.isEmpty()) {
            notesList.addView(text("Sin notas este día.", 14f, dim).apply { setPadding(0, dp(14), 0, 0) })
        }
        notes.forEach { notesList.addView(noteRow(it)) }
    }

    private fun buildGrid() {
        grid.removeAllViews()
        val first = selected.withDayOfMonth(1)
        val offset = first.dayOfWeek.value - 1
        val days = selected.lengthOfMonth()
        val today = LocalDate.now()
        val byDay = AgendaStore.all().filter { it.date.startsWith(first.toString().substring(0, 8)) }
            .groupBy { it.date }

        var day = 1 - offset
        while (day <= days) {
            val row = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
            for (c in 0 until 7) {
                val cell = TextView(this).apply {
                    gravity = Gravity.CENTER
                    textSize = 16f
                }
                if (day in 1..days) {
                    val date = first.withDayOfMonth(day)
                    val notes = byDay[date.toString()].orEmpty()
                    val fill = if (notes.isNotEmpty()) AgendaStore.COLORS[notes[0].color].second else bg
                    val stroke = when (date) {
                        selected -> Color.WHITE
                        today -> accent
                        else -> null
                    }
                    cell.background = rounded(fill, 10, stroke)
                    cell.setTextColor(if (notes.isNotEmpty()) textOn(fill) else Color.WHITE)
                    if (date == today) cell.typeface = Typeface.DEFAULT_BOLD
                    val label = if (notes.size > 1) "$day\n" + "•".repeat(minOf(notes.size, 4)) else "$day"
                    cell.text = SpannableString(label).apply {
                        if (notes.size > 1) {
                            setSpan(RelativeSizeSpan(0.7f), label.indexOf('\n'), label.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
                        }
                    }
                    cell.setLineSpacing(0f, 0.8f)
                    cell.setOnClickListener {
                        // Segundo toque en el día elegido: añadir una nota.
                        if (selected == date) editNote(null)
                        selected = date
                        refresh()
                    }
                }
                row.addView(cell, LinearLayout.LayoutParams(0, dp(48), 1f).apply { setMargins(dp(2), dp(2), dp(2), dp(2)) })
                day++
            }
            grid.addView(row)
        }
    }

    private fun noteRow(n: AgendaStore.Note): View {
        val row = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            background = rounded(card, 14)
            setOnClickListener { editNote(n) }
        }
        row.addView(View(this).apply { background = rounded(AgendaStore.COLORS[n.color].second, 4) },
            LinearLayout.LayoutParams(dp(8), MATCH_PARENT))
        val body = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(14), dp(10), dp(14), dp(10))
        }
        val title = n.title.ifBlank { "Nota" }
        body.addView(text(if (n.time.isNotEmpty()) "${n.time}  ·  $title" else title, 16f, bold = true))
        if (n.text.isNotBlank()) body.addView(text(n.text, 14f, dim))
        row.addView(body, LinearLayout.LayoutParams(0, WRAP_CONTENT, 1f))
        return row.apply {
            layoutParams = LinearLayout.LayoutParams(MATCH_PARENT, WRAP_CONTENT).apply { topMargin = dp(8) }
        }
    }

    // ---------- Crear y editar ----------

    private fun editNote(existing: AgendaStore.Note?) {
        var date = existing?.let { LocalDate.parse(it.date) } ?: selected
        var time = existing?.time ?: ""
        var color = existing?.color ?: lastColor

        val form = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(20), dp(8), dp(20), 0)
        }
        val titleField = EditText(this).apply {
            hint = "Título (p. ej. Dentista)"
            setText(existing?.title.orEmpty())
            inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_FLAG_CAP_SENTENCES
            filters = arrayOf<InputFilter>(InputFilter.LengthFilter(80))
        }
        form.addView(titleField)
        val textField = EditText(this).apply {
            hint = "Detalles (opcional)"
            setText(existing?.text.orEmpty())
            inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_FLAG_MULTI_LINE or
                InputType.TYPE_TEXT_FLAG_CAP_SENTENCES
            minLines = 2
            filters = arrayOf<InputFilter>(InputFilter.LengthFilter(200))
        }
        form.addView(textField)

        val whenRow = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
        val dateButton = Button(this).apply { isAllCaps = false }
        val timeButton = Button(this).apply { isAllCaps = false }
        fun updateWhen() {
            dateButton.text = "${date.dayOfMonth} ${MONTHS[date.monthValue - 1].take(3)} ${date.year}"
            timeButton.text = time.ifEmpty { "Todo el día" }
        }
        dateButton.setOnClickListener {
            DatePickerDialog(this, { _, y, m, d -> date = LocalDate.of(y, m + 1, d); updateWhen() },
                date.year, date.monthValue - 1, date.dayOfMonth).show()
        }
        timeButton.setOnClickListener {
            val (h, m) = time.split(":").takeIf { it.size == 2 }?.map { it.toInt() } ?: listOf(9, 0)
            TimePickerDialog(this, { _, hh, mm -> time = "%02d:%02d".format(hh, mm); updateWhen() }, h, m, true)
                .apply { setButton(AlertDialog.BUTTON_NEUTRAL, "Todo el día") { _, _ -> time = ""; updateWhen() } }
                .show()
        }
        updateWhen()
        whenRow.addView(dateButton, LinearLayout.LayoutParams(0, WRAP_CONTENT, 1f))
        whenRow.addView(timeButton, LinearLayout.LayoutParams(0, WRAP_CONTENT, 1f))
        form.addView(whenRow)

        form.addView(text("Color", 13f, dim).apply { setPadding(0, dp(10), 0, dp(6)) })
        val colors = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
        val swatches = AgendaStore.COLORS.mapIndexed { i, (name, c) ->
            View(this).apply {
                contentDescription = name
                setOnClickListener {
                    color = i
                    for (j in 0 until colors.childCount) {
                        colors.getChildAt(j).background = swatch(AgendaStore.COLORS[j].second, j == color)
                    }
                }
                background = swatch(c, i == color)
            }
        }
        swatches.forEach {
            colors.addView(it, LinearLayout.LayoutParams(0, dp(34), 1f).apply { setMargins(dp(3), 0, dp(3), 0) })
        }
        form.addView(colors)

        val builder = AlertDialog.Builder(this, android.R.style.Theme_DeviceDefault_Dialog_Alert)
            .setTitle(if (existing == null) "Nueva nota" else "Editar nota")
            .setView(ScrollView(this).apply { addView(form) })
            .setPositiveButton("Guardar") { _, _ ->
                val title = titleField.text.toString().trim()
                val body = textField.text.toString().trim()
                if (title.isEmpty() && body.isEmpty()) {
                    Toast.makeText(this, "Nota vacía: no se ha guardado.", Toast.LENGTH_SHORT).show()
                    return@setPositiveButton
                }
                lastColor = color
                AgendaStore.put(
                    AgendaStore.Note(existing?.id ?: AgendaStore.newId(), date.toString(), time, title, body, color),
                )
                selected = date
                refresh()
            }
            .setNegativeButton("Cancelar", null)
        if (existing != null) {
            builder.setNeutralButton("Borrar") { _, _ ->
                AlertDialog.Builder(this, android.R.style.Theme_DeviceDefault_Dialog_Alert)
                    .setMessage("¿Borrar «${existing.title.ifBlank { "Nota" }}»?")
                    .setPositiveButton("Borrar") { _, _ -> AgendaStore.delete(existing.id); refresh() }
                    .setNegativeButton("Cancelar", null)
                    .show()
            }
        }
        builder.show()
    }

    private fun swatch(c: Int, chosen: Boolean) = GradientDrawable().apply {
        setColor(c)
        cornerRadius = dp(17).toFloat()
        if (chosen) setStroke(dp(3), Color.WHITE)
    }
}
