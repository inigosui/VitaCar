#include "screens_internal.h"

#include <stdio.h>

#define ART_X     (CONTENT_X + 48)
#define ART_Y     88
#define ART_SIZE  300
#define INFO_X    (CONTENT_X + 392)
#define INFO_W    420
#define CTRL_Y    350

static const char *const ACTIONS[3] = { "prev", "play_pause", "next" };

static float ctrl_x(int i)
{
    return INFO_X + 70 + i * 140;
}

static void format_time(char *buf, size_t n, int ms)
{
    int secs = ms / 1000;
    snprintf(buf, n, "%d:%02d", secs / 60, secs % 60);
}

void music_draw(App *app)
{
    const PhoneState *ps = phone_state();
    SDL_Color accent = APPS[APP_MUSIC].color;
    SDL_Texture *art = phone_album_art();
    char buf[16];

    if (art) {
        ui_image(art, NULL, ART_X, ART_Y, ART_SIZE, ART_SIZE);
    } else {
        ui_fill_round_rect(ART_X, ART_Y, ART_SIZE, ART_SIZE, 26, accent);
        icon_draw(ICON_MUSIC, ART_X + ART_SIZE / 2.0f, ART_Y + ART_SIZE / 2.0f, ART_SIZE * 0.5f, COL_WHITE, accent);
    }

    if (ps->media_active) {
        ui_text_fit(FONT_SMALL, ps->media_app, INFO_X, 78, INFO_W, accent, ALIGN_LEFT);
        ui_text_fit(FONT_TITLE, ps->title, INFO_X, 102, INFO_W, COL_TEXT, ALIGN_LEFT);
        ui_text_fit(FONT_BODY, ps->artist, INFO_X, 152, INFO_W, COL_TEXT_DIM, ALIGN_LEFT);

        if (ps->duration_ms > 0) {
            int pos = phone_media_position_ms();
            ui_fill_round_rect(INFO_X, 230, INFO_W, 8, 4, COL_PANEL);
            ui_fill_round_rect(INFO_X, 230, INFO_W * (float)pos / ps->duration_ms, 8, 4, COL_TEXT);
            format_time(buf, sizeof(buf), pos);
            ui_text(FONT_SMALL, buf, INFO_X, 248, COL_TEXT_DIM, ALIGN_LEFT);
            buf[0] = '-';
            format_time(buf + 1, sizeof(buf) - 1, ps->duration_ms - pos);
            ui_text(FONT_SMALL, buf, INFO_X + INFO_W, 248, COL_TEXT_DIM, ALIGN_RIGHT);
        }
    } else {
        ui_text(FONT_TITLE, "Nada sonando", INFO_X, 102, COL_TEXT, ALIGN_LEFT);
        ui_text_wrap(FONT_BODY,
                     ps->link == LINK_CONNECTED
                         ? "Pon música en el móvil (Spotify, YouTube Music…) y aparecerá aquí."
                         : "Conecta el móvil para controlar su música desde aquí.",
                     INFO_X, 156, INFO_W, 3, COL_TEXT_DIM);
    }

    for (int i = 0; i < 3; i++) {
        float cx = ctrl_x(i);
        float r = i == 1 ? 50 : 40;
        if (i == app->music_focus)
            ui_fill_circle(cx, CTRL_Y, r + 6, COL_WHITE);
        ui_fill_circle(cx, CTRL_Y, r, COL_PANEL);
        IconId icon = i == 0 ? ICON_PREV : i == 2 ? ICON_NEXT : ps->playing ? ICON_PAUSE : ICON_PLAY;
        icon_draw(icon, cx, CTRL_Y, r, COL_TEXT, COL_PANEL);
    }

    hint_bar("X  pulsar     L / R  cambiar pista     O  inicio");
}

bool music_input(App *app, InputAction action)
{
    switch (action) {
    case IN_LEFT:    if (app->music_focus > 0) app->music_focus--; return true;
    case IN_RIGHT:   if (app->music_focus < 2) app->music_focus++; return true;
    case IN_CONFIRM: phone_media_cmd(ACTIONS[app->music_focus]); return true;
    case IN_PREV:    phone_media_cmd("prev"); return true;
    case IN_NEXT:    phone_media_cmd("next"); return true;
    default:         return false;
    }
}

void music_touch(App *app, float x, float y)
{
    for (int i = 0; i < 3; i++) {
        if (hit_circle(x, y, ctrl_x(i), CTRL_Y, 60)) {
            app->music_focus = i;
            phone_media_cmd(ACTIONS[i]);
            return;
        }
    }
}
