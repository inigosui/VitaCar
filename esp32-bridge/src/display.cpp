#include "display.h"
#include "config.h"
#include <SPI.h>
#include <Preferences.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>

#define HEADER_H  30
#define CALL_Y    204          // franja roja de llamada, abajo, en todas las páginas
#define GREY      0x8410
#define DARK_GREY 0x2104
#define BLUE      0x0339

static Adafruit_ST7789 tft(-1, PIN_TFT_DC, PIN_TFT_RST);
static Preferences prefs;
static DisplayPage page = PAGE_STATUS;
static DisplayState shown;
static bool first = true;      // hay que redibujar la página entera

static const char *PAGE_NAMES[PAGE_COUNT] = {"Velocidad", "Estado", "Musica"};

// La fuente de Adafruit no tiene tildes: se cambian por la letra sin tilde.
static String ascii(const String &in)
{
    static const char *from[] = {"á","é","í","ó","ú","Á","É","Í","Ó","Ú","ñ","Ñ","ü","Ü","¿","¡"};
    static const char *to[]   = {"a","e","i","o","u","A","E","I","O","U","n","N","u","U","",""};
    String s = in;
    for (size_t i = 0; i < sizeof(from) / sizeof(from[0]); i++)
        s.replace(from[i], to[i]);
    String out;
    for (size_t i = 0; i < s.length(); i++)
        out += (uint8_t)s[i] < 128 ? s[i] : '?';
    return out;
}

// Recorta a n letras como mucho, con un punto al final si no cabe.
static String fit(const String &s, size_t n)
{
    return s.length() > n ? s.substring(0, n - 1) + "." : s;
}

// Texto centrado; cada letra mide 6x8 por el tamaño.
static void centered(int y, int size, uint16_t color, const String &text)
{
    tft.setTextSize(size);
    tft.setTextColor(color);
    tft.setCursor((240 - (int)text.length() * 6 * size) / 2, y);
    tft.print(text);
}

static void header()
{
    tft.fillRect(0, 0, 240, HEADER_H, BLUE);
    tft.setTextSize(2);
    tft.setTextColor(ST77XX_WHITE);
    tft.setCursor(8, 8);
    tft.print(PAGE_NAMES[page]);
    // Un punto por página, el de la actual relleno.
    for (int i = 0; i < PAGE_COUNT; i++) {
        int x = 240 - 14 - (PAGE_COUNT - 1 - i) * 14;
        if (i == page)
            tft.fillCircle(x, 15, 4, ST77XX_WHITE);
        else
            tft.drawCircle(x, 15, 4, ST77XX_WHITE);
    }
}

static void call_banner(const DisplayState &s)
{
    tft.fillRect(0, CALL_Y, 240, 240 - CALL_Y, s.call.length() ? ST77XX_RED : ST77XX_BLACK);
    if (s.call.length())
        centered(CALL_Y + 10, 2, ST77XX_WHITE, fit(ascii(s.call), 19));
}

// ---------- Página Estado ----------

// Una fila de texto: borra la franja y escribe recortando a lo que cabe (19 letras a tamaño 2).
static void row(int y, uint16_t color, const String &label, const String &value)
{
    tft.fillRect(0, y, 240, 36, ST77XX_BLACK);
    tft.setTextSize(1);
    tft.setTextColor(GREY);
    tft.setCursor(6, y);
    tft.print(label);
    tft.setTextSize(2);
    tft.setTextColor(color);
    tft.setCursor(6, y + 12);
    tft.print(fit(ascii(value), 19));
}

static void draw_status(const DisplayState &s)
{
    if (first || s.vita_connected != shown.vita_connected || s.vita_ip != shown.vita_ip) {
        if (s.vita_connected)
            row(38, ST77XX_GREEN, "VITA", "Conectada " + s.vita_ip.substring(s.vita_ip.lastIndexOf('.')));
        else
            row(38, ST77XX_ORANGE, "VITA", "Esperando...");
    }
    if (first || s.iphone != shown.iphone || s.iphone_ok != shown.iphone_ok)
        row(78, s.iphone_ok ? ST77XX_GREEN : ST77XX_ORANGE, "IPHONE", s.iphone);
    if (first || s.title != shown.title || s.artist != shown.artist || s.playing != shown.playing)
        row(118, ST77XX_CYAN, s.playing ? "MUSICA  >" : "MUSICA  ||",
            s.title.length() ? s.title + " - " + s.artist : "-");
    if (first || s.last_notif != shown.last_notif)
        row(158, ST77XX_YELLOW, "AVISO", s.last_notif.length() ? s.last_notif : "-");
}

// ---------- Página Velocidad ----------

static void draw_speed(const DisplayState &s)
{
    if (first || s.speed_kmh != shown.speed_kmh) {
        tft.fillRect(0, 50, 240, 140, ST77XX_BLACK);
        String v = s.speed_kmh >= 0 ? String(s.speed_kmh) : String("--");
        centered(64, 9, ST77XX_WHITE, v);              // 54x72 px por cifra
        centered(150, 3, GREY, "km/h");
        if (s.speed_kmh < 0)
            centered(184, 1, GREY, "Sin GPS");
    }
}

// ---------- Página Música ----------

// Parte el texto en líneas de n letras como mucho, cortando por espacios si se puede.
static int wrap(const String &text, size_t n, String *lines, int max_lines)
{
    String rest = text;
    int count = 0;
    while (rest.length() && count < max_lines) {
        if (rest.length() <= n || count == max_lines - 1) {
            lines[count++] = fit(rest, n);
            break;
        }
        int cut = rest.lastIndexOf(' ', n);
        if (cut <= 0)
            cut = n;
        lines[count++] = rest.substring(0, cut);
        rest = rest.substring(cut);
        rest.trim();
    }
    return count;
}

static String mmss(uint32_t s)
{
    char buf[8];
    snprintf(buf, sizeof(buf), "%u:%02u", (unsigned)(s / 60), (unsigned)(s % 60));
    return buf;
}

static void draw_song(const DisplayState &s)
{
    bool song_changed = first || s.title != shown.title || s.artist != shown.artist;
    if (song_changed) {
        tft.fillRect(0, HEADER_H, 240, 132, ST77XX_BLACK);
        if (!s.title.length()) {
            centered(100, 2, GREY, "Nada sonando");
        } else {
            String lines[3];
            int n = wrap(ascii(s.title), 13, lines, 3);   // tamaño 3: 13 letras por línea
            for (int i = 0; i < n; i++)
                centered(42 + i * 30, 3, ST77XX_WHITE, lines[i]);
            centered(138, 2, ST77XX_CYAN, fit(ascii(s.artist), 19));
        }
    }
    if (!s.title.length()) {
        if (song_changed)
            tft.fillRect(0, 162, 240, CALL_Y - 162, ST77XX_BLACK);
        return;
    }
    // Barra de progreso y tiempos: solo cuando cambia el segundo o la pausa.
    if (song_changed || s.pos_s != shown.pos_s || s.dur_s != shown.dur_s || s.playing != shown.playing) {
        tft.fillRect(0, 162, 240, CALL_Y - 162, ST77XX_BLACK);
        int w = s.dur_s ? (int)(200UL * min(s.pos_s, s.dur_s) / s.dur_s) : 0;
        tft.fillRect(20, 168, 200, 6, DARK_GREY);
        tft.fillRect(20, 168, w, 6, ST77XX_CYAN);
        tft.setTextSize(2);
        tft.setTextColor(GREY);
        tft.setCursor(20, 182);
        tft.print(mmss(s.pos_s));
        String d = s.dur_s ? mmss(s.dur_s) : String("");
        tft.setCursor(220 - d.length() * 12, 182);
        tft.print(d);
        // Pausa (dos barras) o sonando (triángulo), en el centro.
        if (s.playing)
            tft.fillTriangle(114, 180, 114, 198, 128, 189, ST77XX_WHITE);
        else {
            tft.fillRect(112, 180, 5, 18, ST77XX_WHITE);
            tft.fillRect(123, 180, 5, 18, ST77XX_WHITE);
        }
    }
}

// ---------- Común ----------

void display_begin()
{
    prefs.begin("pantalla", false);
    int p = prefs.getUChar("pagina", PAGE_STATUS);
    page = p < PAGE_COUNT ? (DisplayPage)p : PAGE_STATUS;
    pinMode(PIN_TFT_BLK, OUTPUT);
    digitalWrite(PIN_TFT_BLK, HIGH);
    tft.init(240, 240, SPI_MODE3);
    tft.setRotation(2);
    tft.fillScreen(ST77XX_BLACK);
}

void display_next_page()
{
    page = (DisplayPage)((page + 1) % PAGE_COUNT);
    prefs.putUChar("pagina", page);
    first = true;
}

void display_update(const DisplayState &s)
{
    if (first) {
        tft.fillScreen(ST77XX_BLACK);
        header();
    }
    switch (page) {
    case PAGE_SPEED:  draw_speed(s);  break;
    case PAGE_STATUS: draw_status(s); break;
    case PAGE_SONG:   draw_song(s);   break;
    default: break;
    }
    if (first || s.call != shown.call)
        call_banner(s);
    shown = s;
    first = false;
}
