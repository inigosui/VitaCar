#include "screens.h"
#include "screens_internal.h"
#include "sysinfo.h"

#include <stdio.h>
#include <string.h>

#define HOME_COLS     3
#define HOME_TILE     132

#define DOCK_TILE     62
#define HOME_BTN_Y    478

#define BANNER_MS     5000
#define TOAST_MS      2500

#define CALL_BTN_R    56
#define CALL_BTN_Y    320

const AppDef APPS[APP_COUNT] = {
    [APP_MUSIC]    = { "Música",   ICON_MUSIC,    {252,  60,  78, 255}, music_draw,     music_input,     music_touch },
    [APP_MAPS]     = { "Mapas",    ICON_MAPS,     { 10, 132, 255, 255}, maps_draw,      maps_input,      maps_touch },
    [APP_PHONE]    = { "Teléfono", ICON_PHONE,    { 48, 199,  89, 255}, phone_app_draw, phone_app_input, phone_app_touch },
    [APP_MESSAGES] = { "Mensajes", ICON_MESSAGES, { 94,  92, 230, 255}, messages_draw,  messages_input,  messages_touch },
    [APP_WEATHER]  = { "Tiempo",   ICON_WEATHER,  {255, 159,  10, 255}, weather_draw,   NULL,            NULL },
    [APP_SETTINGS] = { "Ajustes",  ICON_SETTINGS, {120, 124, 134, 255}, settings_draw,  NULL,            NULL },
};

/* Accesos directos fijos en la barra lateral, como el dock de CarPlay. */
static const int DOCK[] = { APP_MUSIC, APP_MAPS, APP_PHONE };
#define DOCK_COUNT ((int)(sizeof(DOCK) / sizeof(DOCK[0])))

/* ---------- Utilidades compartidas ---------- */

bool hit_circle(float x, float y, float cx, float cy, float r)
{
    float dx = x - cx, dy = y - cy;
    return dx * dx + dy * dy <= r * r;
}

bool hit_rect(float x, float y, float rx, float ry, float rw, float rh)
{
    return x >= rx && x <= rx + rw && y >= ry && y <= ry + rh;
}

void hint_bar(const char *text)
{
    ui_text(FONT_SMALL, text, CONTENT_X + CONTENT_W / 2.0f, SCREEN_H - 34, COL_TEXT_DIM, ALIGN_CENTER);
}

void show_toast(App *app, const char *text)
{
    snprintf(app->toast, sizeof(app->toast), "%s", text);
    app->toast_until = SDL_GetTicks() + TOAST_MS;
}

void open_app(App *app, int index)
{
    app->current = index;
    if (index == APP_MESSAGES)
        app->unread = 0;
}

void draw_button(float x, float y, float w, float h, const char *label, SDL_Color bg, bool focused)
{
    if (focused)
        ui_fill_round_rect(x - 4, y - 4, w + 8, h + 8, h / 2 + 4, COL_WHITE);
    ui_fill_round_rect(x, y, w, h, h / 2, bg);
    float ty = y + (h - ui_font_height(FONT_LABEL)) / 2.0f;
    ui_text_fit(FONT_LABEL, label, x + w / 2, ty, w - 24, COL_TEXT, ALIGN_CENTER);
}

/* ---------- Barra lateral ---------- */

static float dock_y(int i)
{
    return 184 + i * 86;
}

static void sidebar_draw(App *app)
{
    const PhoneState *ps = phone_state();
    ui_fill_rect(0, 0, SIDEBAR_W, SCREEN_H, COL_SIDEBAR);

    int hour, minute;
    char buf[16];
    sys_local_time(&hour, &minute);
    snprintf(buf, sizeof(buf), "%d:%02d", hour, minute);
    ui_text(FONT_CLOCK, buf, SIDEBAR_W / 2.0f, 14, COL_TEXT, ALIGN_CENTER);

    /* Batería de la Vita */
    int pct = sys_battery_percent();
    float bx = SIDEBAR_W / 2.0f - 19, by = 58;
    SDL_Color fill = pct <= 15 ? COL_RED : sys_battery_charging() ? COL_GREEN : COL_TEXT;
    ui_fill_round_rect(bx, by, 34, 17, 4, COL_TEXT_DIM);
    ui_fill_round_rect(bx + 2, by + 2, 30, 13, 3, COL_SIDEBAR);
    ui_fill_round_rect(bx + 3, by + 3, 28 * pct / 100.0f, 11, 2, fill);
    ui_fill_round_rect(bx + 35, by + 5, 3, 7, 1, COL_TEXT_DIM);
    snprintf(buf, sizeof(buf), "%d%%", pct);
    ui_text(FONT_SMALL, buf, SIDEBAR_W / 2.0f, 80, COL_TEXT_DIM, ALIGN_CENTER);

    /* Estado del móvil */
    SDL_Color dot = ps->link == LINK_CONNECTED ? COL_GREEN : ps->link == LINK_SEARCHING ? COL_AMBER : COL_TEXT_DIM;
    ui_fill_circle(24, 124, 5, dot);
    ui_text(FONT_SMALL, "Móvil", 34, 113, ps->link == LINK_CONNECTED ? COL_TEXT : COL_TEXT_DIM, ALIGN_LEFT);

    for (int i = 0; i < DOCK_COUNT; i++) {
        const AppDef *def = &APPS[DOCK[i]];
        icon_tile(def->icon, SIDEBAR_W / 2.0f, dock_y(i), DOCK_TILE, def->color);
        if (app->current == DOCK[i])
            ui_fill_circle(8, dock_y(i), 4, COL_TEXT);
    }
    if (ps->call == CALL_ACTIVE) {
        /* Llamada en curso: aro verde alrededor del icono de Teléfono. */
        float s = DOCK_TILE + 12;
        ui_fill_arc(SIDEBAR_W / 2.0f, dock_y(2), s / 2 - 3, s / 2, 0, 360, COL_GREEN);
    }

    ui_fill_round_rect(SIDEBAR_W / 2.0f - DOCK_TILE / 2.0f, HOME_BTN_Y - DOCK_TILE / 2.0f,
                       DOCK_TILE, DOCK_TILE, DOCK_TILE * 0.23f, COL_PANEL);
    icon_draw(ICON_HOME, SIDEBAR_W / 2.0f, HOME_BTN_Y, DOCK_TILE * 0.5f, COL_TEXT, COL_PANEL);
}

static void sidebar_touch(App *app, float x, float y)
{
    float cx = SIDEBAR_W / 2.0f, r = DOCK_TILE * 0.65f;
    for (int i = 0; i < DOCK_COUNT; i++) {
        if (hit_circle(x, y, cx, dock_y(i), r)) {
            open_app(app, DOCK[i]);
            return;
        }
    }
    if (hit_circle(x, y, cx, HOME_BTN_Y, r))
        app->current = SCREEN_HOME;
}

/* ---------- Inicio ---------- */

static void home_tile_center(int i, float *cx, float *cy)
{
    float col_w = (float)CONTENT_W / HOME_COLS;
    *cx = CONTENT_X + col_w * (i % HOME_COLS + 0.5f);
    *cy = 168 + (i / HOME_COLS) * 214;
}

static void home_draw(App *app)
{
    for (int i = 0; i < APP_COUNT; i++) {
        float cx, cy;
        home_tile_center(i, &cx, &cy);
        if (i == app->home_focus) {
            float s = HOME_TILE + 14;
            ui_fill_round_rect(cx - s / 2, cy - s / 2, s, s, s * 0.25f, COL_WHITE);
            ui_fill_round_rect(cx - s / 2 + 4, cy - s / 2 + 4, s - 8, s - 8, (s - 8) * 0.24f, COL_BG);
        }
        icon_tile(APPS[i].icon, cx, cy, HOME_TILE, APPS[i].color);
        ui_text(FONT_LABEL, APPS[i].name, cx, cy + HOME_TILE / 2.0f + 14, COL_TEXT, ALIGN_CENTER);

        if (i == APP_MESSAGES && app->unread > 0) {
            char buf[8];
            snprintf(buf, sizeof(buf), "%d", app->unread > 99 ? 99 : app->unread);
            float bx = cx + HOME_TILE / 2.0f - 6, by = cy - HOME_TILE / 2.0f + 6;
            ui_fill_circle(bx, by, 20, COL_RED);
            ui_text(FONT_LABEL, buf, bx, by - ui_font_height(FONT_LABEL) / 2.0f, COL_WHITE, ALIGN_CENTER);
        }
    }
}

static void home_input(App *app, InputAction action)
{
    int f = app->home_focus;
    switch (action) {
    case IN_LEFT:  if (f % HOME_COLS > 0) f--; break;
    case IN_RIGHT: if (f % HOME_COLS < HOME_COLS - 1 && f + 1 < APP_COUNT) f++; break;
    case IN_UP:    if (f >= HOME_COLS) f -= HOME_COLS; break;
    case IN_DOWN:  if (f + HOME_COLS < APP_COUNT) f += HOME_COLS; break;
    case IN_CONFIRM: open_app(app, f); break;
    default: break;
    }
    app->home_focus = f;
}

static void home_touch(App *app, float x, float y)
{
    for (int i = 0; i < APP_COUNT; i++) {
        float cx, cy;
        home_tile_center(i, &cx, &cy);
        if (hit_rect(x, y, cx - HOME_TILE / 2.0f, cy - HOME_TILE / 2.0f, HOME_TILE, HOME_TILE + 40)) {
            app->home_focus = i;
            open_app(app, i);
            return;
        }
    }
}

/* ---------- Aviso emergente de notificación ---------- */

#define BANNER_X  (CONTENT_X + 24)
#define BANNER_Y  14
#define BANNER_W  (CONTENT_W - 48)
#define BANNER_H  96

static bool banner_visible(App *app)
{
    return !SDL_TICKS_PASSED(SDL_GetTicks(), app->banner_until) && app->current != APP_MESSAGES;
}

static void banner_draw(App *app)
{
    const PhoneNotif *n = &app->banner;
    ui_fill_round_rect(BANNER_X, BANNER_Y + 3, BANNER_W, BANNER_H, 22, COL_SHADE);
    ui_fill_round_rect(BANNER_X, BANNER_Y, BANNER_W, BANNER_H, 22, (SDL_Color){48, 53, 64, 255});
    icon_tile(ICON_MESSAGES, BANNER_X + 46, BANNER_Y + BANNER_H / 2.0f, 56, APPS[APP_MESSAGES].color);
    float tx = BANNER_X + 92, tw = BANNER_W - 112;
    ui_text_fit(FONT_SMALL, n->app, tx, BANNER_Y + 10, tw, COL_TEXT_DIM, ALIGN_LEFT);
    ui_text_fit(FONT_BODY, n->title, tx, BANNER_Y + 30, tw, COL_TEXT, ALIGN_LEFT);
    ui_text_fit(FONT_LABEL, n->text, tx, BANNER_Y + 64, tw, COL_TEXT_DIM, ALIGN_LEFT);
}

/* ---------- Llamada entrante ---------- */

static float call_btn_x(int i)
{
    return CONTENT_X + CONTENT_W / 2.0f + (i == 0 ? -130 : 130);
}

static void call_overlay_draw(App *app)
{
    const PhoneState *ps = phone_state();
    ui_fill_rect(CONTENT_X, 0, CONTENT_W, SCREEN_H, COL_BG);
    float cx = CONTENT_X + CONTENT_W / 2.0f;

    ui_text(FONT_BODY, "Llamada entrante", cx, 70, COL_TEXT_DIM, ALIGN_CENTER);
    const char *who = ps->call_name[0] ? ps->call_name : ps->call_number[0] ? ps->call_number : "Número oculto";
    ui_text_fit(FONT_TITLE, who, cx, 130, CONTENT_W - 80, COL_TEXT, ALIGN_CENTER);
    if (ps->call_name[0] && ps->call_number[0])
        ui_text(FONT_BODY, ps->call_number, cx, 186, COL_TEXT_DIM, ALIGN_CENTER);

    const SDL_Color colors[2] = { COL_RED, COL_GREEN };
    const char *labels[2] = { "Rechazar", "Contestar" };
    for (int i = 0; i < 2; i++) {
        float bx = call_btn_x(i);
        if (app->call_focus == i)
            ui_fill_circle(bx, CALL_BTN_Y, CALL_BTN_R + 7, COL_WHITE);
        ui_fill_circle(bx, CALL_BTN_Y, CALL_BTN_R, colors[i]);
        icon_draw(ICON_PHONE, bx, CALL_BTN_Y, CALL_BTN_R * 1.1f, COL_WHITE, colors[i]);
        ui_text(FONT_LABEL, labels[i], bx, CALL_BTN_Y + CALL_BTN_R + 14, COL_TEXT, ALIGN_CENTER);
    }
}

static void call_activate(App *app, int i)
{
    phone_call_cmd(i == 0 ? "hangup" : "answer");
    show_toast(app, i == 0 ? "Llamada rechazada" : "Contestando…");
}

/* ---------- Despacho ---------- */

void screens_update(App *app, float dt)
{
    (void)dt;
    const PhoneState *ps = phone_state();

    if (ps->notif_seq != app->seen_notif_seq) {
        int added = (int)(ps->notif_seq - app->seen_notif_seq);
        app->seen_notif_seq = ps->notif_seq;
        if (ps->notif_count > 0 && added > 0) {
            app->banner = ps->notifs[0];
            app->banner_until = SDL_GetTicks() + BANNER_MS;
            if (app->current != APP_MESSAGES)
                app->unread += added;
        }
    }

    if (ps->route_arrived != app->seen_arrived) {
        app->seen_arrived = ps->route_arrived;
        show_toast(app, "Has llegado a tu destino");
    }

    if (ps->call != app->last_call) {
        if (ps->call == CALL_RINGING)
            app->call_focus = 1;
        if (ps->call == CALL_ACTIVE)
            app->current = APP_PHONE;
        app->last_call = ps->call;
    }
}

void screens_draw(App *app)
{
    sidebar_draw(app);
    ui_clip(CONTENT_X, 0, CONTENT_W, SCREEN_H);
    if (app->current == SCREEN_HOME)
        home_draw(app);
    else
        APPS[app->current].draw(app);

    if (phone_state()->call == CALL_RINGING)
        call_overlay_draw(app);
    else if (banner_visible(app))
        banner_draw(app);

    if (!SDL_TICKS_PASSED(SDL_GetTicks(), app->toast_until)) {
        float w = ui_text_width(FONT_LABEL, app->toast) + 48;
        float x = CONTENT_X + (CONTENT_W - w) / 2, y = SCREEN_H - 92;
        ui_fill_round_rect(x, y, w, 48, 24, (SDL_Color){60, 66, 80, 240});
        ui_text(FONT_LABEL, app->toast, x + w / 2, y + (48 - ui_font_height(FONT_LABEL)) / 2.0f, COL_TEXT, ALIGN_CENTER);
    }
    ui_unclip();
}

void screens_input(App *app, InputAction action)
{
    if (phone_state()->call == CALL_RINGING) {
        if (action == IN_LEFT)
            app->call_focus = 0;
        else if (action == IN_RIGHT)
            app->call_focus = 1;
        else if (action == IN_CONFIRM)
            call_activate(app, app->call_focus);
        return;
    }

    if (app->current == SCREEN_HOME) {
        home_input(app, action);
        return;
    }
    const AppDef *def = &APPS[app->current];
    if (def->input && def->input(app, action))
        return;
    if (action == IN_BACK)
        app->current = SCREEN_HOME;
}

void screens_touch(App *app, float x, float y)
{
    if (x < SIDEBAR_W) {
        sidebar_touch(app, x, y);
        return;
    }
    if (phone_state()->call == CALL_RINGING) {
        for (int i = 0; i < 2; i++) {
            if (hit_circle(x, y, call_btn_x(i), CALL_BTN_Y, CALL_BTN_R + 10))
                call_activate(app, i);
        }
        return;
    }
    if (banner_visible(app) && hit_rect(x, y, BANNER_X, BANNER_Y, BANNER_W, BANNER_H)) {
        app->banner_until = 0;
        messages_open(app, app->banner.id);
        return;
    }
    if (app->current == SCREEN_HOME)
        home_touch(app, x, y);
    else if (APPS[app->current].touch)
        APPS[app->current].touch(app, x, y);
}

void screens_swipe(App *app, float x, float y, float dx, float dy)
{
    (void)y;
    (void)dx;
    if (x >= SIDEBAR_W && app->current == APP_MESSAGES && phone_state()->call != CALL_RINGING)
        messages_swipe(app, dy);
}
