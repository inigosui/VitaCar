#pragma once

#include <SDL2/SDL.h>

typedef enum {
    ICON_MUSIC,
    ICON_MAPS,
    ICON_PHONE,
    ICON_MESSAGES,
    ICON_WEATHER,
    ICON_SETTINGS,
    ICON_HOME,
    ICON_PLAY,
    ICON_PAUSE,
    ICON_PREV,
    ICON_NEXT,
    ICON_CLOUD,
    ICON_RAIN,
} IconId;

/* Dibuja el glifo centrado en (cx, cy) dentro de un cuadrado de lado s.
 * bg es el color del fondo, usado para recortar huecos (p. ej. el centro del engranaje). */
void icon_draw(IconId id, float cx, float cy, float s, SDL_Color fg, SDL_Color bg);

/* Baldosa redondeada de color con el glifo en blanco, estilo app de CarPlay. */
void icon_tile(IconId id, float cx, float cy, float size, SDL_Color color);
