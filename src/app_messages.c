#include "screens_internal.h"

#include <stdio.h>
#include <string.h>

#define LIST_X      (CONTENT_X + 32)
#define LIST_Y      24
#define LIST_W      (CONTENT_W - 64)
#define ROW_H       90
#define ROW_GAP     10
#define VISIBLE     5

#define BTN_Y       404
#define BTN_H       64
#define BTN_GAP     14

/* Respuestas rápidas: lo que se ve en el botón y lo que se envía. */
static const char *const REPLY_LABELS[] = { "Conduciendo", "Llego en 10 min", "Vale" };
static const char *const REPLY_TEXTS[]  = { "Estoy conduciendo, te escribo luego.", "Llego en 10 minutos.", "Vale" };
#define REPLY_COUNT 3

static const PhoneNotif *find(const char *id)
{
    const PhoneState *ps = phone_state();
    for (int i = 0; i < ps->notif_count; i++) {
        if (strcmp(ps->notifs[i].id, id) == 0)
            return &ps->notifs[i];
    }
    return NULL;
}

/* Botones del detalle: respuestas (si la app lo permite) + Borrar. */
static int button_count(const PhoneNotif *n)
{
    return (n->can_reply ? REPLY_COUNT : 0) + 1;
}

static void button_rect(int i, int count, float *x, float *w)
{
    *w = (LIST_W - (count - 1) * BTN_GAP) / (float)count;
    *x = LIST_X + i * (*w + BTN_GAP);
}

static void activate_button(App *app, const PhoneNotif *n, int i)
{
    char id[sizeof(n->id)];
    snprintf(id, sizeof(id), "%s", n->id);
    if (n->can_reply && i < REPLY_COUNT) {
        phone_reply(id, REPLY_TEXTS[i]);
        show_toast(app, "Respuesta enviada");
    } else {
        show_toast(app, "Mensaje borrado");
    }
    phone_dismiss(id);
    app->msg.open = false;
}

void messages_open(App *app, const char *id)
{
    open_app(app, APP_MESSAGES);
    snprintf(app->msg.open_id, sizeof(app->msg.open_id), "%s", id);
    app->msg.open = true;
    app->msg.button = 0;
}

/* ---------- Lista ---------- */

static void clamp_focus(App *app)
{
    int count = phone_state()->notif_count;
    MessagesState *m = &app->msg;
    if (m->focus >= count)
        m->focus = count - 1;
    if (m->focus < 0)
        m->focus = 0;
    if (m->focus < m->scroll)
        m->scroll = m->focus;
    if (m->focus >= m->scroll + VISIBLE)
        m->scroll = m->focus - VISIBLE + 1;
    if (m->scroll > count - VISIBLE)
        m->scroll = count - VISIBLE > 0 ? count - VISIBLE : 0;
}

static void draw_list(App *app)
{
    const PhoneState *ps = phone_state();
    float cx = CONTENT_X + CONTENT_W / 2.0f;

    if (ps->notif_count == 0) {
        icon_tile(ICON_MESSAGES, cx, 190, 140, APPS[APP_MESSAGES].color);
        ui_text(FONT_TITLE, "Sin mensajes", cx, 286, COL_TEXT, ALIGN_CENTER);
        ui_text(FONT_BODY, ps->link == LINK_CONNECTED ? "Las notificaciones del móvil aparecerán aquí."
                                                      : "Conecta el móvil para ver sus notificaciones.",
                cx, 338, COL_TEXT_DIM, ALIGN_CENTER);
        hint_bar("O  inicio");
        return;
    }

    clamp_focus(app);
    for (int row = 0; row < VISIBLE; row++) {
        int i = app->msg.scroll + row;
        if (i >= ps->notif_count)
            break;
        const PhoneNotif *n = &ps->notifs[i];
        float y = LIST_Y + row * (ROW_H + ROW_GAP);
        if (i == app->msg.focus)
            ui_fill_round_rect(LIST_X - 4, y - 4, LIST_W + 8, ROW_H + 8, 22, COL_WHITE);
        ui_fill_round_rect(LIST_X, y, LIST_W, ROW_H, 18, COL_PANEL);

        float tx = LIST_X + 22, tw = LIST_W - 44;
        ui_text_fit(FONT_SMALL, n->app, tx, y + 8, tw - 80, APPS[APP_MESSAGES].color, ALIGN_LEFT);
        ui_text(FONT_SMALL, n->time_str, tx + tw, y + 8, COL_TEXT_DIM, ALIGN_RIGHT);
        ui_text_fit(FONT_BODY, n->title, tx, y + 28, tw, COL_TEXT, ALIGN_LEFT);
        ui_text_fit(FONT_LABEL, n->text, tx, y + 60, tw, COL_TEXT_DIM, ALIGN_LEFT);
    }
    hint_bar("X  abrir     O  inicio");
}

/* ---------- Detalle ---------- */

static void draw_detail(App *app, const PhoneNotif *n)
{
    ui_text_fit(FONT_SMALL, n->app, LIST_X, 28, LIST_W - 80, APPS[APP_MESSAGES].color, ALIGN_LEFT);
    ui_text(FONT_SMALL, n->time_str, LIST_X + LIST_W, 28, COL_TEXT_DIM, ALIGN_RIGHT);
    ui_text_fit(FONT_TITLE, n->title, LIST_X, 52, LIST_W, COL_TEXT, ALIGN_LEFT);

    ui_fill_round_rect(LIST_X, 112, LIST_W, 270, 18, COL_PANEL);
    ui_text_wrap(FONT_BODY, n->text, LIST_X + 24, 128, LIST_W - 48, 6, COL_TEXT);

    int count = button_count(n);
    if (app->msg.button >= count)
        app->msg.button = count - 1;
    for (int i = 0; i < count; i++) {
        float x, w;
        button_rect(i, count, &x, &w);
        bool is_reply = n->can_reply && i < REPLY_COUNT;
        draw_button(x, BTN_Y, w, BTN_H, is_reply ? REPLY_LABELS[i] : "Borrar",
                    is_reply ? APPS[APP_MESSAGES].color : (SDL_Color){70, 74, 86, 255},
                    i == app->msg.button);
    }
    hint_bar(n->can_reply ? "X  responder     O  volver a la lista" : "X  pulsar     O  volver a la lista");
}

void messages_draw(App *app)
{
    const PhoneNotif *n = app->msg.open ? find(app->msg.open_id) : NULL;
    if (app->msg.open && !n)
        app->msg.open = false;     /* la borraron en el móvil */
    if (n)
        draw_detail(app, n);
    else
        draw_list(app);
}

bool messages_input(App *app, InputAction action)
{
    const PhoneState *ps = phone_state();
    MessagesState *m = &app->msg;

    if (m->open) {
        const PhoneNotif *n = find(m->open_id);
        if (!n) {
            m->open = false;
            return action == IN_BACK;
        }
        int count = button_count(n);
        switch (action) {
        case IN_LEFT:    if (m->button > 0) m->button--; return true;
        case IN_RIGHT:   if (m->button < count - 1) m->button++; return true;
        case IN_CONFIRM: activate_button(app, n, m->button); return true;
        case IN_BACK:    m->open = false; return true;
        default:         return false;
        }
    }

    switch (action) {
    case IN_UP:   m->focus--; clamp_focus(app); return true;
    case IN_DOWN: m->focus++; clamp_focus(app); return true;
    case IN_CONFIRM:
        if (m->focus < ps->notif_count)
            messages_open(app, ps->notifs[m->focus].id);
        return true;
    default:
        return false;
    }
}

void messages_swipe(App *app, float dy)
{
    if (app->msg.open)
        return;
    /* Deslizar hacia arriba muestra mensajes más antiguos. */
    int rows = (int)(-dy / (ROW_H + ROW_GAP) - (dy > 0 ? 0.5f : -0.5f));
    if (rows == 0)
        rows = dy < 0 ? 1 : -1;
    int count = phone_state()->notif_count;
    int max_scroll = count - VISIBLE > 0 ? count - VISIBLE : 0;
    MessagesState *m = &app->msg;
    m->scroll += rows;
    if (m->scroll > max_scroll)
        m->scroll = max_scroll;
    if (m->scroll < 0)
        m->scroll = 0;
    if (m->focus < m->scroll)
        m->focus = m->scroll;
    if (m->focus >= m->scroll + VISIBLE)
        m->focus = m->scroll + VISIBLE - 1;
}

void messages_touch(App *app, float x, float y)
{
    const PhoneState *ps = phone_state();
    MessagesState *m = &app->msg;

    if (m->open) {
        const PhoneNotif *n = find(m->open_id);
        if (!n)
            return;
        int count = button_count(n);
        for (int i = 0; i < count; i++) {
            float bx, bw;
            button_rect(i, count, &bx, &bw);
            if (hit_rect(x, y, bx, BTN_Y, bw, BTN_H)) {
                m->button = i;
                activate_button(app, n, i);
                return;
            }
        }
        return;
    }

    for (int row = 0; row < VISIBLE; row++) {
        int i = m->scroll + row;
        if (i >= ps->notif_count)
            break;
        if (hit_rect(x, y, LIST_X, LIST_Y + row * (ROW_H + ROW_GAP), LIST_W, ROW_H)) {
            m->focus = i;
            messages_open(app, ps->notifs[i].id);
            return;
        }
    }
}
