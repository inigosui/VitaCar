#include "ui.h"

#include <SDL2/SDL_ttf.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define PI_F             3.14159265f
#define MAX_VERTS        1024
#define MAX_INDICES      3072
#define TEXT_CACHE_SIZE  96
#define TEXT_KEY_LEN     256
#define FIT_CACHE_SIZE   48

static const struct { bool bold; int size; } FONT_SPECS[FONT_COUNT] = {
    [FONT_SMALL] = { false, 18 },
    [FONT_LABEL] = { false, 21 },
    [FONT_BODY]  = { false, 26 },
    [FONT_CLOCK] = { true,  28 },
    [FONT_TITLE] = { true,  36 },
    [FONT_HUGE]  = { true,  84 },
};

/* Caché de texturas de texto: renderizar con FreeType cada frame es caro en la Vita.
 * El texto se rasteriza en blanco y se tiñe con color mod al dibujar. */
typedef struct {
    SDL_Texture *tex;
    int w, h;
    FontId font;
    char text[TEXT_KEY_LEN];
    Uint32 last_used;
} TextEntry;

static SDL_Renderer *g_r;
static TTF_Font *g_fonts[FONT_COUNT];
static TextEntry g_cache[TEXT_CACHE_SIZE];
static Uint32 g_frame;

/* Caché de recortes con "…": medir texto en cada frame es caro. */
typedef struct {
    FontId font;
    int max_w;
    char src[TEXT_KEY_LEN];
    char out[TEXT_KEY_LEN];
    Uint32 last_used;
    bool used;
} FitEntry;

static FitEntry g_fit[FIT_CACHE_SIZE];

static SDL_Vertex g_verts[MAX_VERTS];
static int g_idx[MAX_INDICES];
static int g_nv, g_ni;
static SDL_Color g_col;

/* ---------- Geometría ---------- */

static void geo_begin(SDL_Color c)
{
    g_nv = 0;
    g_ni = 0;
    g_col = c;
}

static int geo_vert(float x, float y)
{
    if (g_nv >= MAX_VERTS)
        return g_nv - 1;
    SDL_Vertex *v = &g_verts[g_nv];
    v->position.x = x;
    v->position.y = y;
    v->color = g_col;
    v->tex_coord.x = 0;
    v->tex_coord.y = 0;
    return g_nv++;
}

static void geo_tri(int a, int b, int c)
{
    if (g_ni + 3 > MAX_INDICES)
        return;
    g_idx[g_ni++] = a;
    g_idx[g_ni++] = b;
    g_idx[g_ni++] = c;
}

static void geo_end(void)
{
    if (g_ni > 0)
        SDL_RenderGeometry(g_r, NULL, g_verts, g_nv, g_idx, g_ni);
}

static int segments_for(float r)
{
    int s = (int)(r * 0.8f);
    if (s < 12) s = 12;
    if (s > 64) s = 64;
    return s;
}

void ui_fill_rect(float x, float y, float w, float h, SDL_Color c)
{
    SDL_FRect rect = { x, y, w, h };
    SDL_SetRenderDrawColor(g_r, c.r, c.g, c.b, c.a);
    SDL_RenderFillRectF(g_r, &rect);
}

void ui_fill_round_rect(float x, float y, float w, float h, float radius, SDL_Color c)
{
    if (radius * 2 > w) radius = w / 2;
    if (radius * 2 > h) radius = h / 2;
    if (radius < 1) {
        ui_fill_rect(x, y, w, h, c);
        return;
    }

    /* Abanico desde el centro recorriendo las cuatro esquinas en sentido horario. */
    const float cxs[4] = { x + w - radius, x + radius, x + radius, x + w - radius };
    const float cys[4] = { y + h - radius, y + h - radius, y + radius, y + radius };
    int seg = segments_for(radius) / 4 + 2;

    geo_begin(c);
    int center = geo_vert(x + w / 2, y + h / 2);
    int first = -1, prev = -1;
    for (int k = 0; k < 4; k++) {
        for (int i = 0; i <= seg; i++) {
            float a = (k * 90.0f + 90.0f * i / seg) * PI_F / 180.0f;
            int v = geo_vert(cxs[k] + cosf(a) * radius, cys[k] + sinf(a) * radius);
            if (prev >= 0)
                geo_tri(center, prev, v);
            else
                first = v;
            prev = v;
        }
    }
    geo_tri(center, prev, first);
    geo_end();
}

void ui_fill_circle(float cx, float cy, float r, SDL_Color c)
{
    int seg = segments_for(r);
    geo_begin(c);
    int center = geo_vert(cx, cy);
    for (int i = 0; i <= seg; i++) {
        float a = 2.0f * PI_F * i / seg;
        int v = geo_vert(cx + cosf(a) * r, cy + sinf(a) * r);
        if (i > 0)
            geo_tri(center, v - 1, v);
    }
    geo_end();
}

void ui_fill_arc(float cx, float cy, float r_in, float r_out, float a0, float a1, SDL_Color c)
{
    int seg = (int)(segments_for(r_out) * fabsf(a1 - a0) / 360.0f) + 2;
    geo_begin(c);
    for (int i = 0; i <= seg; i++) {
        float a = (a0 + (a1 - a0) * i / seg) * PI_F / 180.0f;
        int outer = geo_vert(cx + cosf(a) * r_out, cy + sinf(a) * r_out);
        int inner = geo_vert(cx + cosf(a) * r_in, cy + sinf(a) * r_in);
        if (i > 0) {
            geo_tri(outer - 2, inner - 2, outer);
            geo_tri(inner - 2, inner, outer);
        }
    }
    geo_end();
}

void ui_fill_triangle(float x1, float y1, float x2, float y2, float x3, float y3, SDL_Color c)
{
    geo_begin(c);
    int a = geo_vert(x1, y1);
    int b = geo_vert(x2, y2);
    int d = geo_vert(x3, y3);
    geo_tri(a, b, d);
    geo_end();
}

void ui_line(float x1, float y1, float x2, float y2, float thick, SDL_Color c, bool round_caps)
{
    float dx = x2 - x1, dy = y2 - y1;
    float len = sqrtf(dx * dx + dy * dy);
    if (len < 0.001f)
        return;
    float nx = -dy / len * thick / 2, ny = dx / len * thick / 2;

    geo_begin(c);
    int a = geo_vert(x1 + nx, y1 + ny);
    int b = geo_vert(x2 + nx, y2 + ny);
    int d = geo_vert(x2 - nx, y2 - ny);
    int e = geo_vert(x1 - nx, y1 - ny);
    geo_tri(a, b, d);
    geo_tri(a, d, e);
    geo_end();

    if (round_caps) {
        ui_fill_circle(x1, y1, thick / 2, c);
        ui_fill_circle(x2, y2, thick / 2, c);
    }
}

/* Línea quebrada en un solo lote de geometría, con uniones redondeadas. Para la ruta del mapa,
 * que puede tener cientos de tramos: un ui_line por tramo sería demasiado lento en la Vita. */
void ui_polyline(const float *xy, int n, float thick, SDL_Color c)
{
    enum { JOIN_SEG = 8 };
    float r = thick / 2;
    geo_begin(c);
    for (int i = 0; i < n; i++) {
        /* Cabe el siguiente tramo y su unión; si no, se dibuja lo acumulado y se empieza otro lote. */
        if (g_nv + 4 + JOIN_SEG + 2 > MAX_VERTS || g_ni + 6 + JOIN_SEG * 3 > MAX_INDICES) {
            geo_end();
            geo_begin(c);
        }
        float x = xy[i * 2], y = xy[i * 2 + 1];
        int center = geo_vert(x, y);
        for (int k = 0; k <= JOIN_SEG; k++) {
            float a = 2.0f * PI_F * k / JOIN_SEG;
            int v = geo_vert(x + cosf(a) * r, y + sinf(a) * r);
            if (k > 0)
                geo_tri(center, v - 1, v);
        }
        if (i + 1 == n)
            break;
        float x2 = xy[i * 2 + 2], y2 = xy[i * 2 + 3];
        float dx = x2 - x, dy = y2 - y;
        float len = sqrtf(dx * dx + dy * dy);
        if (len < 0.001f)
            continue;
        float nx = -dy / len * r, ny = dx / len * r;
        int a = geo_vert(x + nx, y + ny);
        int b = geo_vert(x2 + nx, y2 + ny);
        int d = geo_vert(x2 - nx, y2 - ny);
        int e = geo_vert(x - nx, y - ny);
        geo_tri(a, b, d);
        geo_tri(a, d, e);
    }
    geo_end();
}

void ui_image(SDL_Texture *tex, const SDL_Rect *src, float x, float y, float w, float h)
{
    SDL_FRect dst = { x, y, w, h };
    SDL_RenderCopyF(g_r, tex, src, &dst);
}

void ui_clip(int x, int y, int w, int h)
{
    SDL_Rect r = { x, y, w, h };
    SDL_RenderSetClipRect(g_r, &r);
}

void ui_unclip(void)
{
    SDL_RenderSetClipRect(g_r, NULL);
}

/* ---------- Texto ---------- */

/* Decodifica un carácter UTF-8. Devuelve los bytes consumidos (mínimo 1). */
static int utf8_next(const char *s, Uint32 *cp)
{
    const unsigned char *u = (const unsigned char *)s;
    if (u[0] < 0x80) { *cp = u[0]; return 1; }
    if ((u[0] & 0xE0) == 0xC0 && (u[1] & 0xC0) == 0x80) {
        *cp = ((u[0] & 0x1F) << 6) | (u[1] & 0x3F);
        return 2;
    }
    if ((u[0] & 0xF0) == 0xE0 && (u[1] & 0xC0) == 0x80 && (u[2] & 0xC0) == 0x80) {
        *cp = ((u[0] & 0x0F) << 12) | ((u[1] & 0x3F) << 6) | (u[2] & 0x3F);
        return 3;
    }
    if ((u[0] & 0xF8) == 0xF0 && (u[1] & 0xC0) == 0x80 && (u[2] & 0xC0) == 0x80 && (u[3] & 0xC0) == 0x80) {
        *cp = ((u[0] & 0x07) << 18) | ((u[1] & 0x3F) << 12) | ((u[2] & 0x3F) << 6) | (u[3] & 0x3F);
        return 4;
    }
    *cp = 0xFFFD;
    return 1;
}

/* Quita los caracteres que la fuente no tiene (emojis sobre todo) para que no
 * salgan como cajas, y convierte saltos de línea y tabuladores en espacios. */
static void filter_text(FontId font, const char *text, char *out, size_t n)
{
    size_t o = 0;
    while (*text && o + 5 < n) {
        Uint32 cp;
        int len = utf8_next(text, &cp);
        if (cp == '\n' || cp == '\r' || cp == '\t') {
            out[o++] = ' ';
        } else if (cp >= 0x20 && TTF_GlyphIsProvided32(g_fonts[font], cp)) {
            memcpy(out + o, text, len);
            o += len;
        }
        text += len;
    }
    out[o] = '\0';
}

static SDL_Texture *render_text(FontId font, const char *text, int *w, int *h)
{
    char clean[1024];
    filter_text(font, text, clean, sizeof(clean));
    if (!clean[0])
        return NULL;
    SDL_Surface *surf = TTF_RenderUTF8_Blended(g_fonts[font], clean, COL_WHITE);
    if (!surf)
        return NULL;
    SDL_Texture *tex = SDL_CreateTextureFromSurface(g_r, surf);
    *w = surf->w;
    *h = surf->h;
    SDL_FreeSurface(surf);
    return tex;
}

static TextEntry *text_lookup(FontId font, const char *text)
{
    TextEntry *victim = NULL;
    for (int i = 0; i < TEXT_CACHE_SIZE; i++) {
        TextEntry *e = &g_cache[i];
        if (e->tex && e->font == font && strcmp(e->text, text) == 0) {
            e->last_used = g_frame;
            return e;
        }
        if (!victim || (victim->tex && (!e->tex || e->last_used < victim->last_used)))
            victim = e;
    }

    if (victim->tex)
        SDL_DestroyTexture(victim->tex);
    victim->tex = render_text(font, text, &victim->w, &victim->h);
    victim->font = font;
    victim->last_used = g_frame;
    strcpy(victim->text, text);
    return victim->tex ? victim : NULL;
}

static void blit_text(SDL_Texture *tex, int w, int h, float x, float y, SDL_Color c, Align align)
{
    if (align == ALIGN_CENTER)
        x -= w / 2.0f;
    else if (align == ALIGN_RIGHT)
        x -= w;
    SDL_FRect dst = { roundf(x), roundf(y), (float)w, (float)h };
    SDL_SetTextureColorMod(tex, c.r, c.g, c.b);
    SDL_SetTextureAlphaMod(tex, c.a);
    SDL_RenderCopyF(g_r, tex, NULL, &dst);
}

int ui_text(FontId font, const char *text, float x, float y, SDL_Color c, Align align)
{
    if (!text || !*text)
        return 0;

    if (strlen(text) < TEXT_KEY_LEN) {
        TextEntry *e = text_lookup(font, text);
        if (!e)
            return 0;
        blit_text(e->tex, e->w, e->h, x, y, c, align);
        return e->w;
    }

    /* Textos largos: sin caché. */
    int w, h;
    SDL_Texture *tex = render_text(font, text, &w, &h);
    if (!tex)
        return 0;
    blit_text(tex, w, h, x, y, c, align);
    SDL_DestroyTexture(tex);
    return w;
}

int ui_text_width(FontId font, const char *text)
{
    char clean[1024];
    int w = 0;
    filter_text(font, text, clean, sizeof(clean));
    if (clean[0])
        TTF_SizeUTF8(g_fonts[font], clean, &w, NULL);
    return w;
}

/* Copia como mucho n-1 bytes sin partir un carácter UTF-8. */
static void utf8_copy(char *dst, const char *src, size_t n)
{
    size_t o = 0;
    while (src[o]) {
        Uint32 cp;
        int len = utf8_next(src + o, &cp);
        if (o + len >= n)
            break;
        o += len;
    }
    memcpy(dst, src, o);
    dst[o] = '\0';
}

static void compute_fit(FontId font, const char *src, int max_w, char *out)
{
    if (ui_text_width(font, src) <= max_w) {
        strcpy(out, src);
        return;
    }

    /* Búsqueda binaria del prefijo más largo que cabe con "…" detrás. */
    int bounds[TEXT_KEY_LEN], nb = 0;
    for (int o = 0; src[o];) {
        Uint32 cp;
        o += utf8_next(src + o, &cp);
        bounds[nb++] = o;
    }
    char trial[TEXT_KEY_LEN + 4];
    int lo = 0, hi = nb - 1, best = 0;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        int len = bounds[mid];
        memcpy(trial, src, len);
        strcpy(trial + len, "…");
        if (ui_text_width(font, trial) <= max_w) {
            best = len;
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    while (best > 0 && src[best - 1] == ' ')
        best--;
    memcpy(out, src, best);
    strcpy(out + best, "…");
}

int ui_text_fit(FontId font, const char *text, float x, float y, float max_w, SDL_Color c, Align align)
{
    if (!text || !*text)
        return 0;

    char src[TEXT_KEY_LEN];
    utf8_copy(src, text, sizeof(src) - 4);
    int mw = (int)max_w;

    FitEntry *victim = NULL;
    for (int i = 0; i < FIT_CACHE_SIZE; i++) {
        FitEntry *e = &g_fit[i];
        if (e->used && e->font == font && e->max_w == mw && strcmp(e->src, src) == 0) {
            e->last_used = g_frame;
            return ui_text(font, e->out, x, y, c, align);
        }
        if (!victim || (victim->used && (!e->used || e->last_used < victim->last_used)))
            victim = e;
    }

    victim->used = true;
    victim->font = font;
    victim->max_w = mw;
    victim->last_used = g_frame;
    strcpy(victim->src, src);
    compute_fit(font, src, mw, victim->out);
    return ui_text(font, victim->out, x, y, c, align);
}

int ui_text_wrap(FontId font, const char *text, float x, float y, float max_w, int max_lines, SDL_Color c)
{
    char line[TEXT_KEY_LEN], trial[TEXT_KEY_LEN];
    int lines = 0, lh = ui_font_height(font);
    const char *p = text;

    while (*p && lines < max_lines) {
        while (*p == ' ')
            p++;
        if (!*p)
            break;
        if (lines == max_lines - 1) {
            /* Última línea disponible: todo lo que queda, recortado con "…". */
            ui_text_fit(font, p, x, y + lines * lh, max_w, c, ALIGN_LEFT);
            return lines + 1;
        }

        /* Mete palabras mientras quepan. */
        line[0] = '\0';
        const char *q = p;
        while (*q && *q != '\n') {
            const char *word = q;
            while (*q && *q != ' ' && *q != '\n')
                q++;
            snprintf(trial, sizeof(trial), "%s%s%.*s", line, line[0] ? " " : "", (int)(q - word), word);
            if (line[0] && ui_text_width(font, trial) > max_w) {
                q = word;
                break;
            }
            strcpy(line, trial);
            while (*q == ' ')
                q++;
        }
        ui_text_fit(font, line, x, y + lines * lh, max_w, c, ALIGN_LEFT);
        lines++;
        p = (*q == '\n') ? q + 1 : q;
    }
    return lines;
}

int ui_font_height(FontId font)
{
    return TTF_FontHeight(g_fonts[font]);
}

/* ---------- Ciclo de vida ---------- */

bool ui_init(SDL_Renderer *renderer, const char *font_regular, const char *font_bold)
{
    g_r = renderer;
    if (TTF_Init() < 0)
        return false;
    for (int i = 0; i < FONT_COUNT; i++) {
        const char *path = FONT_SPECS[i].bold ? font_bold : font_regular;
        g_fonts[i] = TTF_OpenFont(path, FONT_SPECS[i].size);
        if (!g_fonts[i]) {
            SDL_Log("No se pudo abrir la fuente %s: %s", path, TTF_GetError());
            return false;
        }
    }
    return true;
}

void ui_shutdown(void)
{
    for (int i = 0; i < TEXT_CACHE_SIZE; i++) {
        if (g_cache[i].tex)
            SDL_DestroyTexture(g_cache[i].tex);
    }
    memset(g_cache, 0, sizeof(g_cache));
    memset(g_fit, 0, sizeof(g_fit));
    for (int i = 0; i < FONT_COUNT; i++) {
        if (g_fonts[i])
            TTF_CloseFont(g_fonts[i]);
        g_fonts[i] = NULL;
    }
    TTF_Quit();
}

void ui_begin_frame(void)
{
    g_frame++;
}
