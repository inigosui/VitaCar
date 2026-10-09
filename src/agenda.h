#pragma once

#include <SDL2/SDL.h>
#include <stdbool.h>

/* Notas del calendario. Se crean y editan en el móvil; la Vita guarda la última copia
 * en la tarjeta para verlas también sin conexión. */

#define AGENDA_MAX_NOTES  256
#define AGENDA_COLORS     8

typedef struct {
    int year, month, day;
    char time_str[8];       /* "HH:mm", o vacío si es para todo el día */
    char title[168];        /* el móvil limita a 80 caracteres */
    char text[408];         /* y a 200 */
    int color;              /* índice en AGENDA_PALETTE */
} AgendaNote;

/* Mismos colores y en el mismo orden que AgendaStore.COLORS en el móvil. */
extern const SDL_Color AGENDA_PALETTE[AGENDA_COLORS];

void agenda_init(void);
void agenda_shutdown(void);
/* Hilo principal, una vez por frame: recoge las notas nuevas que haya dejado el hilo de red. */
void agenda_poll(void);
/* Notas ordenadas por fecha y hora (solo hilo principal). Devuelve cuántas hay. */
int agenda_notes(const AgendaNote **notes);

/* Lo llama el hilo de red con el mensaje "agenda" del móvil. */
struct cJSON;
void agenda_on_json(const struct cJSON *msg);
