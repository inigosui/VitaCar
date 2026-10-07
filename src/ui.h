#pragma once

#include <SDL2/SDL.h>
#include <stdbool.h>

#define SCREEN_W 960
#define SCREEN_H 544

#define COL_BG        ((SDL_Color){ 12,  14,  18, 255})
#define COL_SIDEBAR   ((SDL_Color){ 26,  29,  36, 255})
#define COL_PANEL     ((SDL_Color){ 34,  38,  47, 255})
#define COL_TEXT      ((SDL_Color){240, 242, 245, 255})
#define COL_TEXT_DIM  ((SDL_Color){146, 152, 165, 255})
#define COL_WHITE     ((SDL_Color){255, 255, 255, 255})
#define COL_GREEN     ((SDL_Color){ 48, 209,  88, 255})
#define COL_RED       ((SDL_Color){255,  69,  58, 255})
#define COL_AMBER     ((SDL_Color){255, 179,  64, 255})
#define COL_SHADE     ((SDL_Color){  0,   0,   0, 170})

typedef enum {
    FONT_SMALL,
    FONT_LABEL,
    FONT_BODY,
    FONT_CLOCK,
    FONT_TITLE,
    FONT_HUGE,
    FONT_COUNT
} FontId;

typedef enum {
    ALIGN_LEFT,
    ALIGN_CENTER,
    ALIGN_RIGHT
} Align;

bool ui_init(SDL_Renderer *renderer, const char *font_regular, const char *font_bold);
void ui_shutdown(void);
void ui_begin_frame(void);

void ui_fill_rect(float x, float y, float w, float h, SDL_Color c);
void ui_fill_round_rect(float x, float y, float w, float h, float radius, SDL_Color c);
void ui_fill_circle(float cx, float cy, float r, SDL_Color c);
/* Ángulos en grados: 0 = derecha, crecen en sentido horario (eje Y hacia abajo). */
void ui_fill_arc(float cx, float cy, float r_in, float r_out, float a0, float a1, SDL_Color c);
void ui_fill_triangle(float x1, float y1, float x2, float y2, float x3, float y3, SDL_Color c);
void ui_line(float x1, float y1, float x2, float y2, float thick, SDL_Color c, bool round_caps);
/* xy = {x0, y0, x1, y1, ...} con n puntos. */
void ui_polyline(const float *xy, int n, float thick, SDL_Color c);

/* src NULL = textura completa. */
void ui_image(SDL_Texture *tex, const SDL_Rect *src, float x, float y, float w, float h);
void ui_clip(int x, int y, int w, int h);
void ui_unclip(void);

/* y es el borde superior del texto. Devuelve el ancho dibujado. */
int ui_text(FontId font, const char *text, float x, float y, SDL_Color c, Align align);
/* Como ui_text, pero recorta con "…" si el texto supera max_w píxeles. */
int ui_text_fit(FontId font, const char *text, float x, float y, float max_w, SDL_Color c, Align align);
/* Texto en varias líneas partiendo por palabras. Devuelve el número de líneas dibujadas. */
int ui_text_wrap(FontId font, const char *text, float x, float y, float max_w, int max_lines, SDL_Color c);
int ui_text_width(FontId font, const char *text);
int ui_font_height(FontId font);
