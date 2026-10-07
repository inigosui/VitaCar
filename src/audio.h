#pragma once

#include <stdbool.h>

/* Sonido del móvil por la Vita (experimental). Llega como PCM de 16 bits, estéreo, 48 kHz. */

#define AUDIO_RATE      48000
#define AUDIO_CHANNELS  2

/* Antes de arrancar el hilo de red. */
void audio_init(void);
/* Hilo principal, una vez por frame: abre o cierra la salida y regula el colchón. */
void audio_update(bool active);
/* Hilo de red: añade sonido recibido. */
void audio_feed(const void *pcm, int len);
void audio_shutdown(void);
/* true si está sonando (no esperando a llenar el colchón). */
bool audio_playing(void);
