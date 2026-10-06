#pragma once

#include <SDL2/SDL.h>

/* Teselas de mapa de 256x256. Orden de búsqueda: memoria -> tarjeta -> móvil.
 * Lo que llega del móvil se guarda en la tarjeta, así que las zonas ya
 * visitadas funcionan después sin conexión. */

#define TILE_SIZE 256

void tiles_init(SDL_Renderer *renderer);
void tiles_shutdown(void);
/* Hilo principal, una vez por frame: convierte en texturas lo que se ha cargado. */
void tiles_update(void);
/* Devuelve la textura si está en memoria; si no, la pide y devuelve NULL. */
SDL_Texture *tiles_get(int z, int x, int y);
/* Como tiles_get pero sin pedir nada (para usar teselas de otro zoom de relleno). */
SDL_Texture *tiles_peek(int z, int x, int y);
