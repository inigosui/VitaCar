#include "audio.h"

#include <SDL2/SDL.h>

#define BYTES_PER_MS    (AUDIO_RATE * AUDIO_CHANNELS * 2 / 1000)
/* Colchón antes de empezar a sonar: absorbe los parones de la WiFi. Si se acumula
 * demasiado (la red recupera de golpe), se descarta para que el retraso no crezca. */
#define PREBUFFER_MS    150
#define MAX_QUEUE_MS    450

static SDL_mutex *g_lock;           /* protege g_dev frente al hilo de red */
static SDL_AudioDeviceID g_dev;
static bool g_paused = true;
static bool g_failed;               /* no reintentar cada frame si no hay salida de audio */

void audio_init(void)
{
    g_lock = SDL_CreateMutex();
}

static void open_device(void)
{
    if (!SDL_WasInit(SDL_INIT_AUDIO) && SDL_InitSubSystem(SDL_INIT_AUDIO) < 0) {
        SDL_Log("Audio: %s", SDL_GetError());
        g_failed = true;
        return;
    }
    SDL_AudioSpec want = { 0 };
    want.freq = AUDIO_RATE;
    want.format = AUDIO_S16LSB;
    want.channels = AUDIO_CHANNELS;
    want.samples = 1024;
    SDL_AudioDeviceID dev = SDL_OpenAudioDevice(NULL, 0, &want, NULL, 0);
    if (!dev) {
        SDL_Log("Audio: %s", SDL_GetError());
        g_failed = true;
        return;
    }
    SDL_LockMutex(g_lock);
    g_dev = dev;
    g_paused = true;
    SDL_UnlockMutex(g_lock);
}

static void close_device(void)
{
    SDL_LockMutex(g_lock);
    SDL_AudioDeviceID dev = g_dev;
    g_dev = 0;
    SDL_UnlockMutex(g_lock);
    SDL_CloseAudioDevice(dev);
}

void audio_update(bool active)
{
    if (!active) {
        g_failed = false;
        if (g_dev)
            close_device();
        return;
    }
    if (!g_dev && !g_failed && g_lock)
        open_device();
    if (!g_dev)
        return;

    Uint32 queued = SDL_GetQueuedAudioSize(g_dev);
    if (queued > MAX_QUEUE_MS * BYTES_PER_MS) {
        SDL_ClearQueuedAudio(g_dev);
        queued = 0;
    }
    if (g_paused && queued >= PREBUFFER_MS * BYTES_PER_MS) {
        SDL_PauseAudioDevice(g_dev, 0);
        g_paused = false;
    } else if (!g_paused && queued == 0) {
        /* Se ha vaciado: esperar a tener colchón otra vez en lugar de sonar a trozos. */
        SDL_PauseAudioDevice(g_dev, 1);
        g_paused = true;
    }
}

void audio_feed(const void *pcm, int len)
{
    if (!g_lock)
        return;
    SDL_LockMutex(g_lock);
    if (g_dev)
        SDL_QueueAudio(g_dev, pcm, (Uint32)(len & ~3));     /* muestras estéreo completas */
    SDL_UnlockMutex(g_lock);
}

void audio_shutdown(void)
{
    if (g_dev)
        close_device();
    if (g_lock)
        SDL_DestroyMutex(g_lock);
    g_lock = NULL;
}

bool audio_playing(void)
{
    return g_dev && !g_paused;
}
