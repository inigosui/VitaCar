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

/* Ruta: puntos a menos de ROUTE_MIN_PX en pantalla se saltan, y los tramos se recortan a la
 * pantalla con ROUTE_MARGIN de margen. Así se dibujan pocos tramos aunque la ruta sea larga. */
#define ROUTE_MIN_PX      4.0
#define ROUTE_MARGIN      40.0
#define ROUTE_DRAW_MAX    2048
#define ROUTE_MAX_RUNS    64

static const SDL_Color TILE_BG   = { 30, 33, 40, 255 };
static const SDL_Color ARROW_COL = { 10, 132, 255, 255 };
static const SDL_Color NAV_BG    = { 22, 110, 62, 255 };
static const SDL_Color ROUTE_COL  = { 66, 160, 255, 255 };
static const SDL_Color ROUTE_EDGE = { 18, 70, 150, 255 };

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

/* Recorta el tramo a-b al rectángulo (Liang-Barsky). Devuelve false si queda fuera. */
static bool clip_segment(double *ax, double *ay, double *bx, double *by,
                         double x0, double y0, double x1, double y1)
{
    double dx = *bx - *ax, dy = *by - *ay, t0 = 0, t1 = 1;
    const double p[4] = { -dx, dx, -dy, dy };
    const double q[4] = { *ax - x0, x1 - *ax, *ay - y0, y1 - *ay };
    for (int i = 0; i < 4; i++) {
        if (p[i] == 0) {
            if (q[i] < 0)
                return false;
            continue;
        }
        double r = q[i] / p[i];
        if (p[i] < 0) {
            if (r > t1) return false;
            if (r > t0) t0 = r;
        } else {
            if (r < t0) return false;
            if (r < t1) t1 = r;
        }
    }
    double sx = *ax, sy = *ay;
    *ax = sx + t0 * dx;
    *ay = sy + t0 * dy;
    *bx = sx + t1 * dx;
    *by = sy + t1 * dy;
    return true;
}

/* Lo que queda de ruta, desde la flecha hasta el destino. Los tramos que salen de la
 * pantalla parten la línea en varios trozos ("runs"). */
static void draw_route(const PhoneState *ps, double wx, double wy, int z, float cx, float cy)
{
    const double *pts;
    int n = phone_route(&pts);
    int start = ps->route_idx < 0 ? 0 : ps->route_idx;
    if (start >= n)
        return;

    static float xy[ROUTE_DRAW_MAX * 2];
    int runs[ROUTE_MAX_RUNS + 1], nruns = 0, m = 0;
    double world = TILE_SIZE * (double)(1 << z);
    double left = wx - (cx - CONTENT_X), top = wy - cy;
    double x0 = CONTENT_X - ROUTE_MARGIN, y0 = -ROUTE_MARGIN;
    double x1 = SCREEN_W + ROUTE_MARGIN, y1 = SCREEN_H + ROUTE_MARGIN;

    /* La línea empieza en la flecha; sin GPS, en el primer punto pendiente. */
    double ax, ay;
    int i = start;
    if (ps->gps_valid) {
        ax = cx;
        ay = cy;
    } else {
        ax = CONTENT_X + pts[i * 2] * world - left;
        ay = pts[i * 2 + 1] * world - top;
        i++;
    }
    bool open = false;      /* el último punto guardado continúa el trozo actual */

    for (; i < n; i++) {
        double bx = CONTENT_X + pts[i * 2] * world - left;
        double by = pts[i * 2 + 1] * world - top;
        double dx = bx - ax, dy = by - ay;
        if (i + 1 < n && dx * dx + dy * dy < ROUTE_MIN_PX * ROUTE_MIN_PX)
            continue;

        double sx = ax, sy = ay, ex = bx, ey = by;
        if (clip_segment(&sx, &sy, &ex, &ey, x0, y0, x1, y1)) {
            bool starts_inside = sx == ax && sy == ay;
            if (!open || !starts_inside) {
                if (nruns == ROUTE_MAX_RUNS || m + 2 > ROUTE_DRAW_MAX)
                    break;
                runs[nruns++] = m;
                xy[m * 2] = (float)sx;
                xy[m * 2 + 1] = (float)sy;
                m++;
            }
            if (m == ROUTE_DRAW_MAX)
                break;
            xy[m * 2] = (float)ex;
            xy[m * 2 + 1] = (float)ey;
            m++;
            open = ex == bx && ey == by;
        } else {
            open = false;
        }
        ax = bx;
        ay = by;
    }
    runs[nruns] = m;

    for (int pass = 0; pass < 2; pass++) {
        for (int r = 0; r < nruns; r++) {
            int count = runs[r + 1] - runs[r];
            if (count >= 2)
                ui_polyline(&xy[runs[r] * 2], count, pass == 0 ? 15 : 9, pass == 0 ? ROUTE_EDGE : ROUTE_COL);
        }
    }

    /* Destino: punto final con borde blanco, si cae en pantalla. */
    double dx = CONTENT_X + pts[(n - 1) * 2] * world - left;
    double dy = pts[(n - 1) * 2 + 1] * world - top;
    if (dx > x0 && dx < x1 && dy > y0 && dy < y1) {
        ui_fill_circle((float)dx, (float)dy, 13, COL_WHITE);
        ui_fill_circle((float)dx, (float)dy, 9, COL_RED);
    }
}

static void format_distance(float m, char *buf, size_t n)
{
    if (m < 1000)
        snprintf(buf, n, "%d m", (int)lroundf(m / 10) * 10);
    else if (m < 100000) {
        int tenths = (int)lroundf(m / 100);
        snprintf(buf, n, "%d,%d km", tenths / 10, tenths % 10);
    } else
        snprintf(buf, n, "%d km", (int)lroundf(m / 1000));
}

static void format_duration(float s, char *buf, size_t n)
{
    int min = (int)lroundf(s / 60);
    if (min < 1)
        min = 1;
    if (min < 60)
        snprintf(buf, n, "%d min", min);
    else
        snprintf(buf, n, "%d h %02d min", min / 60, min % 60);
}

/* Recuadro inferior: lo que queda y la hora de llegada. */
static void draw_route_info(const PhoneState *ps)
{
    char dist[24], dur[24], line1[64], line2[160];
    format_distance(ps->route_left_m, dist, sizeof(dist));
    format_duration(ps->route_left_s, dur, sizeof(dur));
    snprintf(line1, sizeof(line1), "%s · %s", dist, dur);
    if (ps->route_arrive[0])
        snprintf(line2, sizeof(line2), "Llegada %s · %s", ps->route_arrive, ps->route_dest);
    else
        snprintf(line2, sizeof(line2), "%s", ps->route_dest);

    float x = CONTENT_X + 132, y = SCREEN_H - 104, w = 400, h = 88;
    ui_fill_round_rect(x, y, w, h, 20, (SDL_Color){20, 22, 28, 230});
    ui_text_fit(FONT_CLOCK, line1, x + 18, y + 10, w - 36, COL_TEXT, ALIGN_LEFT);
    ui_text_fit(FONT_SMALL, line2, x + 18, y + 54, w - 36, COL_TEXT_DIM, ALIGN_LEFT);
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

    /* Con navegación o ruta, la posición va más abajo para ver más carretera por delante. */
    float cx = CONTENT_X + CONTENT_W / 2.0f;
    float cy = ps->nav_active || ps->route_active ? SCREEN_H * 0.62f : SCREEN_H / 2.0f;

    double wx, wy;
    project(lat, lon, z, &wx, &wy);
    draw_tiles(wx, wy, z, cx, cy);

    if (ps->route_active)
        draw_route(ps, wx, wy, z, cx, cy);

    if (ps->gps_valid)
        draw_arrow(cx, cy, ps->bearing);

    if (ps->nav_active)
        draw_nav_banner(ps);
    if (ps->route_active)
        draw_route_info(ps);

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
