/* Pantallas informativas: Tiempo y Ajustes. */

#include "screens_internal.h"
#include "audio.h"
#include "sysinfo.h"

#include <math.h>
#include <stdio.h>

/* ---------- Tiempo ---------- */

/* Códigos meteorológicos WMO que devuelve Open-Meteo. */
const char *weather_text(int code)
{
    if (code == 0) return "Despejado";
    if (code <= 2) return "Poco nuboso";
    if (code == 3) return "Nublado";
    if (code <= 48) return "Niebla";
    if (code <= 57) return "Llovizna";
    if (code <= 67) return "Lluvia";
    if (code <= 77) return "Nieve";
    if (code <= 82) return "Chubascos";
    if (code <= 86) return "Chubascos de nieve";
    return "Tormenta";
}

IconId weather_icon(int code)
{
    if (code <= 2) return ICON_WEATHER;
    if (code <= 48) return ICON_CLOUD;
    return ICON_RAIN;
}

void weather_draw(App *app)
{
    (void)app;
    const PhoneState *ps = phone_state();
    float cx = CONTENT_X + CONTENT_W / 2.0f;
    SDL_Color accent = APPS[APP_WEATHER].color;

    if (!ps->weather_valid) {
        icon_tile(ICON_WEATHER, cx, 190, 140, accent);
        ui_text(FONT_TITLE, "Tiempo", cx, 286, COL_TEXT, ALIGN_CENTER);
        ui_text(FONT_BODY, ps->link == LINK_CONNECTED ? "Esperando datos del móvil…"
                                                      : "Conecta el móvil para ver el tiempo.",
                cx, 338, COL_TEXT_DIM, ALIGN_CENTER);
        hint_bar("O  inicio");
        return;
    }

    char buf[32];
    icon_tile(weather_icon(ps->weather_code), CONTENT_X + 230, 230, 200, accent);
    float tx = CONTENT_X + 400;
    ui_text_fit(FONT_BODY, ps->place[0] ? ps->place : "Tu ubicación", tx, 112, CONTENT_W - 440, COL_TEXT_DIM, ALIGN_LEFT);
    snprintf(buf, sizeof(buf), "%d°", (int)lroundf(ps->temp));
    ui_text(FONT_HUGE, buf, tx - 4, 136, COL_TEXT, ALIGN_LEFT);
    ui_text(FONT_TITLE, weather_text(ps->weather_code), tx, 258, COL_TEXT, ALIGN_LEFT);
    snprintf(buf, sizeof(buf), "Máx. %d°   Mín. %d°", (int)lroundf(ps->temp_max), (int)lroundf(ps->temp_min));
    ui_text(FONT_BODY, buf, tx, 310, COL_TEXT_DIM, ALIGN_LEFT);
    hint_bar("Datos: Open-Meteo     O  inicio");
}

/* ---------- Ajustes ---------- */

void settings_draw(App *app)
{
    (void)app;
    const PhoneState *ps = phone_state();
    char battery[32], phone[96];
    snprintf(battery, sizeof(battery), "%d%%%s", sys_battery_percent(),
             sys_battery_charging() ? " (cargando)" : "");
    if (ps->link == LINK_CONNECTED)
        snprintf(phone, sizeof(phone), "%s", ps->phone_name[0] ? ps->phone_name : "Conectado");
    else
        snprintf(phone, sizeof(phone), "%s", ps->link == LINK_SEARCHING ? "Buscando…" : "Sin WiFi");

    const char *sound = !ps->audio_active ? "Por el móvil"
                      : audio_playing() ? "Por la Vita" : "Por la Vita (cargando…)";

    const char *rows[][2] = {
        { "Batería de la Vita", battery },
        { "Móvil", phone },
        { "Sonido", sound },
        { "Red WiFi", ps->ssid[0] ? ps->ssid : "—" },
        { "Versión", APP_VERSION },
    };

    float x = CONTENT_X + 48, w = CONTENT_W - 96;
    ui_text(FONT_TITLE, "Ajustes", x, 36, COL_TEXT, ALIGN_LEFT);
    for (int i = 0; i < (int)(sizeof(rows) / sizeof(rows[0])); i++) {
        float y = 110 + i * 78;
        ui_fill_round_rect(x, y, w, 64, 16, COL_PANEL);
        float ty = y + (64 - ui_font_height(FONT_BODY)) / 2.0f;
        ui_text(FONT_BODY, rows[i][0], x + 24, ty, COL_TEXT, ALIGN_LEFT);
        ui_text_fit(FONT_BODY, rows[i][1], x + w - 24, ty, w / 2, COL_TEXT_DIM, ALIGN_RIGHT);
    }
    hint_bar("O  volver al inicio");
}
