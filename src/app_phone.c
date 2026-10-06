#include "screens_internal.h"

#include <stdio.h>

#define HANGUP_Y  320
#define HANGUP_R  56

static void draw_active_call(const PhoneState *ps)
{
    float cx = CONTENT_X + CONTENT_W / 2.0f;
    char buf[16];
    Uint32 secs = (SDL_GetTicks() - ps->call_since) / 1000;
    snprintf(buf, sizeof(buf), "%u:%02u", (unsigned)(secs / 60), (unsigned)(secs % 60));

    ui_text(FONT_BODY, "En llamada", cx, 70, COL_GREEN, ALIGN_CENTER);
    /* En llamadas salientes Android no da el número. */
    const char *who = ps->call_name[0] ? ps->call_name : ps->call_number[0] ? ps->call_number : "Llamada en curso";
    ui_text_fit(FONT_TITLE, who, cx, 130, CONTENT_W - 80, COL_TEXT, ALIGN_CENTER);
    ui_text(FONT_BODY, buf, cx, 190, COL_TEXT_DIM, ALIGN_CENTER);

    ui_fill_circle(cx, HANGUP_Y, HANGUP_R + 7, COL_WHITE);
    ui_fill_circle(cx, HANGUP_Y, HANGUP_R, COL_RED);
    icon_draw(ICON_PHONE, cx, HANGUP_Y, HANGUP_R * 1.1f, COL_WHITE, COL_RED);
    ui_text(FONT_LABEL, "Colgar", cx, HANGUP_Y + HANGUP_R + 14, COL_TEXT, ALIGN_CENTER);
}

static void draw_status(const PhoneState *ps)
{
    float x = CONTENT_X + 48, w = CONTENT_W - 96;
    icon_tile(ICON_PHONE, x + 44, 76, 88, APPS[APP_PHONE].color);

    const char *title, *subtitle;
    SDL_Color sub_col;
    if (ps->link == LINK_CONNECTED) {
        title = ps->phone_name[0] ? ps->phone_name : "Móvil";
        subtitle = "Conectado";
        sub_col = COL_GREEN;
    } else if (ps->link == LINK_SEARCHING) {
        title = "Buscando el móvil…";
        subtitle = "Abre VitaCar en el móvil y pulsa «Iniciar»";
        sub_col = COL_AMBER;
    } else {
        title = "Sin WiFi";
        subtitle = "La Vita no está conectada a ninguna red";
        sub_col = COL_TEXT_DIM;
    }
    ui_text_fit(FONT_TITLE, title, x + 112, 38, w - 112, COL_TEXT, ALIGN_LEFT);
    ui_text_fit(FONT_BODY, subtitle, x + 112, 86, w - 112, sub_col, ALIGN_LEFT);

    if (ps->link == LINK_CONNECTED) {
        char battery[32];
        if (ps->battery >= 0)
            snprintf(battery, sizeof(battery), "%d%%%s", ps->battery, ps->charging ? " (cargando)" : "");
        else
            snprintf(battery, sizeof(battery), "—");
        const char *rows[][2] = {
            { "Batería del móvil", battery },
            { "Red WiFi", ps->ssid[0] ? ps->ssid : "—" },
            { "IP del móvil", ps->phone_ip },
        };
        for (int i = 0; i < 3; i++) {
            float y = 160 + i * 76;
            ui_fill_round_rect(x, y, w, 62, 16, COL_PANEL);
            float ty = y + (62 - ui_font_height(FONT_BODY)) / 2.0f;
            ui_text(FONT_BODY, rows[i][0], x + 24, ty, COL_TEXT, ALIGN_LEFT);
            ui_text_fit(FONT_BODY, rows[i][1], x + w - 24, ty, w / 2, COL_TEXT_DIM, ALIGN_RIGHT);
        }
        hint_bar("Las llamadas entrantes aparecerán en pantalla     O  inicio");
        return;
    }

    static const char *const STEPS[] = {
        "1.  En el móvil, activa el punto de acceso WiFi en la banda de 2,4 GHz.",
        "2.  En la Vita: Ajustes › Red › Configuración de Wi-Fi › conéctate a esa red.",
        "3.  Abre VitaCar en el móvil y pulsa «Iniciar». Se conectará solo.",
    };
    ui_fill_round_rect(x, 160, w, 262, 18, COL_PANEL);
    for (int i = 0; i < 3; i++)
        ui_text_wrap(FONT_LABEL, STEPS[i], x + 24, 180 + i * 80, w - 48, 2, COL_TEXT);
    hint_bar("O  inicio");
}

void phone_app_draw(App *app)
{
    (void)app;
    const PhoneState *ps = phone_state();
    if (ps->call == CALL_ACTIVE)
        draw_active_call(ps);
    else
        draw_status(ps);
}

bool phone_app_input(App *app, InputAction action)
{
    if (phone_state()->call == CALL_ACTIVE && action == IN_CONFIRM) {
        phone_call_cmd("hangup");
        show_toast(app, "Colgando…");
        return true;
    }
    return false;
}

void phone_app_touch(App *app, float x, float y)
{
    if (phone_state()->call == CALL_ACTIVE &&
        hit_circle(x, y, CONTENT_X + CONTENT_W / 2.0f, HANGUP_Y, HANGUP_R + 10)) {
        phone_call_cmd("hangup");
        show_toast(app, "Colgando…");
    }
}
