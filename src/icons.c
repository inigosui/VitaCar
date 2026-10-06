#include "icons.h"
#include "ui.h"

#include <math.h>

#define PI_F 3.14159265f

static void draw_music(float cx, float cy, float s, SDL_Color fg)
{
    float stem = s * 0.08f;
    float head = s * 0.13f;
    float lx = cx - s * 0.20f, ly = cy + s * 0.26f;
    float rx = cx + s * 0.26f, ry = cy + s * 0.18f;
    float top_l = cy - s * 0.30f, top_r = cy - s * 0.38f;

    ui_fill_circle(lx, ly, head, fg);
    ui_fill_circle(rx, ry, head, fg);
    ui_line(lx + head - stem / 2, ly, lx + head - stem / 2, top_l, stem, fg, false);
    ui_line(rx + head - stem / 2, ry, rx + head - stem / 2, top_r, stem, fg, false);
    ui_line(lx + head - stem, top_l + s * 0.06f, rx + head, top_r + s * 0.06f, s * 0.13f, fg, false);
}

static void draw_maps(float cx, float cy, float s, SDL_Color fg, SDL_Color bg)
{
    float r = s * 0.28f;
    float py = cy - s * 0.12f;
    ui_fill_circle(cx, py, r, fg);
    ui_fill_triangle(cx - r * 0.86f, py + r * 0.5f, cx + r * 0.86f, py + r * 0.5f, cx, cy + s * 0.46f, fg);
    ui_fill_circle(cx, py, r * 0.42f, bg);
}

static void draw_phone(float cx, float cy, float s, SDL_Color fg)
{
    /* Auricular: arco grueso abombado hacia abajo-izquierda con extremos redondeados. */
    float ox = cx + s * 0.10f, oy = cy - s * 0.10f;
    float r_in = s * 0.24f, r_out = s * 0.40f;
    float a0 = 55.0f, a1 = 215.0f;
    ui_fill_arc(ox, oy, r_in, r_out, a0, a1, fg);

    float rm = (r_in + r_out) / 2, cap = (r_out - r_in) * 0.62f;
    for (int i = 0; i < 2; i++) {
        float a = (i == 0 ? a0 : a1) * PI_F / 180.0f;
        ui_fill_circle(ox + cosf(a) * rm, oy + sinf(a) * rm, cap, fg);
    }
}

static void draw_messages(float cx, float cy, float s, SDL_Color fg)
{
    ui_fill_round_rect(cx - s * 0.42f, cy - s * 0.36f, s * 0.84f, s * 0.60f, s * 0.24f, fg);
    ui_fill_triangle(cx - s * 0.30f, cy + s * 0.12f, cx - s * 0.04f, cy + s * 0.20f,
                     cx - s * 0.38f, cy + s * 0.42f, fg);
}

static void draw_weather(float cx, float cy, float s, SDL_Color fg)
{
    ui_fill_circle(cx, cy, s * 0.20f, fg);
    for (int i = 0; i < 8; i++) {
        float a = i * PI_F / 4;
        float c = cosf(a), sn = sinf(a);
        ui_line(cx + c * s * 0.31f, cy + sn * s * 0.31f, cx + c * s * 0.42f, cy + sn * s * 0.42f,
                s * 0.08f, fg, true);
    }
}

static void draw_settings(float cx, float cy, float s, SDL_Color fg, SDL_Color bg)
{
    for (int i = 0; i < 8; i++) {
        float a = i * PI_F / 4 + PI_F / 8;
        ui_line(cx, cy, cx + cosf(a) * s * 0.45f, cy + sinf(a) * s * 0.45f, s * 0.16f, fg, false);
    }
    ui_fill_circle(cx, cy, s * 0.33f, fg);
    ui_fill_circle(cx, cy, s * 0.14f, bg);
}

static void draw_home(float cx, float cy, float s, SDL_Color fg)
{
    float cell = s * 0.36f, gap = s * 0.12f;
    float x0 = cx - cell - gap / 2, y0 = cy - cell - gap / 2;
    for (int i = 0; i < 4; i++) {
        float x = x0 + (i % 2) * (cell + gap);
        float y = y0 + (i / 2) * (cell + gap);
        ui_fill_round_rect(x, y, cell, cell, cell * 0.28f, fg);
    }
}

static void draw_play(float cx, float cy, float s, SDL_Color fg)
{
    float h = s * 0.40f;
    ui_fill_triangle(cx - h * 0.62f, cy - h, cx - h * 0.62f, cy + h, cx + h * 1.05f, cy, fg);
}

static void draw_pause(float cx, float cy, float s, SDL_Color fg)
{
    float w = s * 0.18f, h = s * 0.76f;
    ui_fill_round_rect(cx - w * 1.4f, cy - h / 2, w, h, w * 0.25f, fg);
    ui_fill_round_rect(cx + w * 0.4f, cy - h / 2, w, h, w * 0.25f, fg);
}

static void draw_skip(float cx, float cy, float s, SDL_Color fg, int dir)
{
    /* dir = 1 siguiente, -1 anterior: dos triángulos y una barra. */
    float h = s * 0.28f, w = s * 0.26f;
    for (int i = 0; i < 2; i++) {
        float base = cx + dir * (-w + i * w) - dir * s * 0.06f;
        ui_fill_triangle(base, cy - h, base, cy + h, base + dir * w, cy, fg);
    }
    float bar_x = cx + dir * (w + s * 0.02f);
    ui_line(bar_x, cy - h, bar_x, cy + h, s * 0.08f, fg, false);
}

static void draw_cloud(float cx, float cy, float s, SDL_Color fg)
{
    ui_fill_circle(cx - s * 0.16f, cy - s * 0.02f, s * 0.20f, fg);
    ui_fill_circle(cx + s * 0.08f, cy - s * 0.12f, s * 0.26f, fg);
    ui_fill_round_rect(cx - s * 0.44f, cy, s * 0.88f, s * 0.26f, s * 0.13f, fg);
}

static void draw_rain(float cx, float cy, float s, SDL_Color fg)
{
    draw_cloud(cx, cy - s * 0.14f, s, fg);
    for (int i = -1; i <= 1; i++) {
        float x = cx + i * s * 0.22f;
        ui_line(x, cy + s * 0.24f, x - s * 0.07f, cy + s * 0.42f, s * 0.07f, fg, true);
    }
}

void icon_draw(IconId id, float cx, float cy, float s, SDL_Color fg, SDL_Color bg)
{
    switch (id) {
    case ICON_MUSIC:    draw_music(cx, cy, s, fg); break;
    case ICON_MAPS:     draw_maps(cx, cy, s, fg, bg); break;
    case ICON_PHONE:    draw_phone(cx, cy, s, fg); break;
    case ICON_MESSAGES: draw_messages(cx, cy, s, fg); break;
    case ICON_WEATHER:  draw_weather(cx, cy, s, fg); break;
    case ICON_SETTINGS: draw_settings(cx, cy, s, fg, bg); break;
    case ICON_HOME:     draw_home(cx, cy, s, fg); break;
    case ICON_PLAY:     draw_play(cx, cy, s, fg); break;
    case ICON_PAUSE:    draw_pause(cx, cy, s, fg); break;
    case ICON_PREV:     draw_skip(cx, cy, s, fg, -1); break;
    case ICON_NEXT:     draw_skip(cx, cy, s, fg, 1); break;
    case ICON_CLOUD:    draw_cloud(cx, cy, s, fg); break;
    case ICON_RAIN:     draw_rain(cx, cy, s, fg); break;
    }
}

void icon_tile(IconId id, float cx, float cy, float size, SDL_Color color)
{
    ui_fill_round_rect(cx - size / 2, cy - size / 2, size, size, size * 0.23f, color);
    icon_draw(id, cx, cy, size * 0.56f, COL_WHITE, color);
}
