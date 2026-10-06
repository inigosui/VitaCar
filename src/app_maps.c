#include "screens_internal.h"
#include "tiles.h"

#include <math.h>
#include <stdio.h>

#define MIN_ZOOM      4
#define MAX_ZOOM      18
#define DEFAULT_ZOOM  16

/* Sin GPS todavía: vista general de la península. */
#define FALLBACK_LAT  40.4168
#define FALLBACK_LON  -3.7038
#define FALLBACK_ZOOM 6

#define ZOOM_BTN_X    (SCREEN_W - 52)
#define ZOOM_IN_Y     214
#define ZOOM_OUT_Y    300
#define ZOOM_BTN_R    32

#define PI_D          3.14159265358979323846

static const SDL_Color TILE_BG   = { 30, 33, 40, 255 };
static const SDL_Color ARROW_COL = { 10, 132, 255, 255 };
static const SDL_Color NAV_BG    = { 22, 110, 62, 255 };

static int current_zoom(App *app)
{
    if (app->map_zoom < MIN_ZOOM || app->map_zoom > MAX_ZOOM)
        app->map_zoom = DEFAULT_ZOOM;
    return app->map_zoom;
}

/* Coordenadas en píxeles del mundo (proyección Web Mercator) al zoom z. */
static void project(double lat, double lon, int z, double *px, double *py)
{
    double world = TILE_SIZE * (double)(1 << z);
    double lat_r = lat * PI_D / 180.0;
    *px = (lon + 180.0) / 360.0 * world;
    *py = (1.0 - log(tan(lat_r) + 1.0 / cos(lat_r)) / PI_D) / 2.0 * world;
}

static void draw_tiles(double wx, double wy, int z, float cx, float cy)
{
    int n = 1 << z;
    double left = wx - (cx - CONTENT_X), top = wy - cy;
    int tx0 = (int)floor(left / TILE_SIZE), ty0 = (int)floor(top / TILE_SIZE);
    int tx1 = (int)floor((left + CONTENT_W) / TILE_SIZE), ty1 = (int)floor((top + SCREEN_H) / TILE_SIZE);

    for (int ty = ty0; ty <= ty1; ty++) {
        for (int tx = tx0; tx <= tx1; tx++) {
            float sx = (float)(CONTENT_X + tx * (double)TILE_SIZE - left);
            float sy = (float)(ty * (double)TILE_SIZE - top);
            if (ty < 0 || ty >= n) {
                ui_fill_rect(sx, sy, TILE_SIZE, TILE_SIZE, TILE_BG);
                continue;
            }
            int wrapped = ((tx % n) + n) % n;
            SDL_Texture *tex = tiles_get(z, wrapped, ty);
            if (tex) {
                ui_image(tex, NULL, sx, sy, TILE_SIZE, TILE_SIZE);
                continue;
            }
            /* Mientras llega, la tesela del zoom anterior ampliada. */
            SDL_Texture *parent = z > 0 ? tiles_peek(z - 1, wrapped / 2, ty / 2) : NULL;
            if (parent) {
                SDL_Rect src = { (wrapped % 2) * TILE_SIZE / 2, (ty % 2) * TILE_SIZE / 2, TILE_SIZE / 2, TILE_SIZE / 2 };
                ui_image(parent, &src, sx, sy, TILE_SIZE, TILE_SIZE);
            } else {
                ui_fill_rect(sx, sy, TILE_SIZE, TILE_SIZE, TILE_BG);
            }
        }
    }
}

static void draw_arrow(float cx, float cy, float bearing)
{
    /* Flecha de navegación girada según el rumbo (0 = norte, sentido horario). */
    static const float shape[4][2] = { { 0, -24 }, { 17, 18 }, { 0, 8 }, { -17, 18 } };
    float a = bearing * (float)PI_D / 180.0f, c = cosf(a), s = sinf(a);
    float out[4][2], border[4][2];
    for (int i = 0; i < 4; i++) {
        float x = shape[i][0], y = shape[i][1];
        out[i][0] = cx + x * c - y * s;
        out[i][1] = cy + x * s + y * c;
        border[i][0] = cx + (x * c - y * s) * 1.3f;
        border[i][1] = cy + (x * s + y * c) * 1.3f;
    }
    ui_fill_circle(cx, cy, 34, (SDL_Color){10, 132, 255, 50});
    ui_fill_triangle(border[0][0], border[0][1], border[1][0], border[1][1], border[2][0], border[2][1], COL_WHITE);
    ui_fill_triangle(border[0][0], border[0][1], border[2][0], border[2][1], border[3][0], border[3][1], COL_WHITE);
    ui_fill_triangle(out[0][0], out[0][1], out[1][0], out[1][1], out[2][0], out[2][1], ARROW_COL);
    ui_fill_triangle(out[0][0], out[0][1], out[2][0], out[2][1], out[3][0], out[3][1], ARROW_COL);
}

static void draw_zoom_button(float cy, bool plus)
{
    ui_fill_circle(ZOOM_BTN_X, cy, ZOOM_BTN_R, (SDL_Color){34, 38, 47, 235});
    ui_line(ZOOM_BTN_X - 12, cy, ZOOM_BTN_X + 12, cy, 5, COL_TEXT, true);
    if (plus)
        ui_line(ZOOM_BTN_X, cy - 12, ZOOM_BTN_X, cy + 12, 5, COL_TEXT, true);
}

static void draw_nav_banner(const PhoneState *ps)
{
    float x = CONTENT_X + 16, y = 14, w = CONTENT_W - 120, h = ps->nav_sub[0] ? 118 : 96;
    ui_fill_round_rect(x, y + 3, w, h, 20, COL_SHADE);
    ui_fill_round_rect(x, y, w, h, 20, NAV_BG);
    ui_text_fit(FONT_TITLE, ps->nav_title, x + 22, y + 8, w - 44, COL_WHITE, ALIGN_LEFT);
    ui_text_fit(FONT_BODY, ps->nav_text, x + 22, y + 56, w - 44, (SDL_Color){220, 245, 228, 255}, ALIGN_LEFT);
    if (ps->nav_sub[0])
        ui_text_fit(FONT_SMALL, ps->nav_sub, x + 22, y + 90, w - 44, (SDL_Color){190, 230, 205, 255}, ALIGN_LEFT);
}

void maps_draw(App *app)
{
    const PhoneState *ps = phone_state();
    int z = ps->gps_valid ? current_zoom(app) : FALLBACK_ZOOM;
    double lat = ps->gps_valid ? ps->lat : FALLBACK_LAT;
    double lon = ps->gps_valid ? ps->lon : FALLBACK_LON;

    /* Con navegación activa, la posición va más abajo para ver más carretera por delante. */
    float cx = CONTENT_X + CONTENT_W / 2.0f;
    float cy = ps->nav_active ? SCREEN_H * 0.62f : SCREEN_H / 2.0f;

    double wx, wy;
    project(lat, lon, z, &wx, &wy);
    draw_tiles(wx, wy, z, cx, cy);

    if (ps->gps_valid)
        draw_arrow(cx, cy, ps->bearing);

    if (ps->nav_active)
        draw_nav_banner(ps);

    draw_zoom_button(ZOOM_IN_Y, true);
    draw_zoom_button(ZOOM_OUT_Y, false);

    if (ps->gps_valid) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", (int)lroundf(ps->speed * 3.6f));
        ui_fill_round_rect(CONTENT_X + 16, SCREEN_H - 104, 104, 88, 20, (SDL_Color){20, 22, 28, 230});
        ui_text(FONT_TITLE, buf, CONTENT_X + 68, SCREEN_H - 100, COL_TEXT, ALIGN_CENTER);
        ui_text(FONT_SMALL, "km/h", CONTENT_X + 68, SCREEN_H - 50, COL_TEXT_DIM, ALIGN_CENTER);
    } else {
        const char *msg = ps->link == LINK_CONNECTED ? "Esperando la ubicación del móvil…"
                                                     : "Conecta el móvil para ver tu posición";
        float w = ui_text_width(FONT_BODY, msg) + 56;
        float x = CONTENT_X + (CONTENT_W - w) / 2;
        ui_fill_round_rect(x, SCREEN_H / 2.0f - 32, w, 64, 32, (SDL_Color){20, 22, 28, 235});
        ui_text(FONT_BODY, msg, x + w / 2, SCREEN_H / 2.0f - ui_font_height(FONT_BODY) / 2.0f, COL_TEXT, ALIGN_CENTER);
    }

    /* Atribución exigida por la licencia de los datos y las teselas. */
    const char *attr = "© colaboradores de OpenStreetMap";
    float aw = ui_text_width(FONT_SMALL, attr) + 16;
    ui_fill_rect(SCREEN_W - aw, SCREEN_H - 26, aw, 26, (SDL_Color){0, 0, 0, 140});
    ui_text(FONT_SMALL, attr, SCREEN_W - 8, SCREEN_H - 25, COL_TEXT_DIM, ALIGN_RIGHT);
}

static void change_zoom(App *app, int delta)
{
    int z = current_zoom(app) + delta;
    if (z >= MIN_ZOOM && z <= MAX_ZOOM)
        app->map_zoom = z;
}

bool maps_input(App *app, InputAction action)
{
    switch (action) {
    case IN_UP:
    case IN_NEXT: change_zoom(app, 1); return true;
    case IN_DOWN:
    case IN_PREV: change_zoom(app, -1); return true;
    default:      return false;
    }
}

void maps_touch(App *app, float x, float y)
{
    if (hit_circle(x, y, ZOOM_BTN_X, ZOOM_IN_Y, ZOOM_BTN_R + 10))
        change_zoom(app, 1);
    else if (hit_circle(x, y, ZOOM_BTN_X, ZOOM_OUT_Y, ZOOM_BTN_R + 10))
        change_zoom(app, -1);
}
