#include "tiles.h"
#include "phone.h"

#include <SDL2/SDL_image.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define CACHE_SIZE        96
#define MAX_PENDING       64
#define QUEUE_SIZE        64
#define UPLOADS_PER_FRAME 4
#define PHONE_WAIT_MS     15000
#define RETRY_MS          3000

typedef struct {
    int z, x, y;
} TileKey;

typedef struct {
    TileKey key;
    SDL_Texture *tex;
    Uint32 last_used;
} CacheEntry;

typedef enum { PEND_FREE, PEND_LOADING, PEND_FAILED } PendState;

typedef struct {
    TileKey key;
    PendState state;
    Uint32 since;
} Pending;

typedef struct {
    TileKey key;
    SDL_Surface *surf;      /* NULL = no disponible ahora mismo */
} Result;

static SDL_Renderer *g_r;
static CacheEntry g_cache[CACHE_SIZE];
static Pending g_pending[MAX_PENDING];
static Uint32 g_frame;

/* Peticiones del hilo principal al hilo cargador. */
static TileKey g_requests[QUEUE_SIZE];
static int g_req_head, g_req_count;
static SDL_mutex *g_req_lock;
static SDL_cond *g_req_cond;

/* Resultados de los hilos cargador y de red al hilo principal. */
static Result g_results[QUEUE_SIZE];
static int g_res_head, g_res_count;
static SDL_mutex *g_res_lock;

static SDL_Thread *g_thread;
static SDL_atomic_t g_quit;

static bool key_eq(TileKey a, TileKey b)
{
    return a.z == b.z && a.x == b.x && a.y == b.y;
}

static void tile_path(TileKey k, char *buf, size_t n)
{
#ifdef __vita__
    snprintf(buf, n, "ux0:data/VitaCar/tiles/%d/%d/%d.png", k.z, k.x, k.y);
#else
    const char *home = getenv("HOME");
    snprintf(buf, n, "%s/.cache/vitacar/tiles/%d/%d/%d.png", home ? home : ".", k.z, k.x, k.y);
#endif
}

static void mkdir_parents(const char *path)
{
    char tmp[256];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0777);
            *p = '/';
        }
    }
}

/* "Modo noche": invertir colores y girar el tono 180° (como el filtro CSS
 * invert(1) hue-rotate(180deg)). Las calles quedan oscuras sin que el agua,
 * los parques o las autopistas pierdan su color. */
static SDL_Surface *night_mode(SDL_Surface *src)
{
    if (!src)
        return NULL;
    SDL_Surface *s = SDL_ConvertSurfaceFormat(src, SDL_PIXELFORMAT_ARGB8888, 0);
    SDL_FreeSurface(src);
    if (!s)
        return NULL;

    SDL_LockSurface(s);
    for (int y = 0; y < s->h; y++) {
        Uint32 *row = (Uint32 *)((Uint8 *)s->pixels + y * s->pitch);
        for (int x = 0; x < s->w; x++) {
            Uint32 p = row[x];
            float r = 255 - ((p >> 16) & 0xFF);
            float g = 255 - ((p >> 8) & 0xFF);
            float b = 255 - (p & 0xFF);
            float nr = -0.574f * r + 1.430f * g + 0.144f * b;
            float ng =  0.426f * r + 0.430f * g + 0.144f * b;
            float nb =  0.426f * r + 1.430f * g - 0.856f * b;
            /* Un poco más apagado para que no deslumbre de noche. */
            int ir = (int)(nr * 0.82f), ig = (int)(ng * 0.82f), ib = (int)(nb * 0.88f);
            ir = ir < 0 ? 0 : ir > 255 ? 255 : ir;
            ig = ig < 0 ? 0 : ig > 255 ? 255 : ig;
            ib = ib < 0 ? 0 : ib > 255 ? 255 : ib;
            row[x] = (p & 0xFF000000u) | ((Uint32)ir << 16) | ((Uint32)ig << 8) | (Uint32)ib;
        }
    }
    SDL_UnlockSurface(s);
    return s;
}

static void push_result(TileKey k, SDL_Surface *surf)
{
    SDL_LockMutex(g_res_lock);
    if (g_res_count < QUEUE_SIZE) {
        g_results[(g_res_head + g_res_count++) % QUEUE_SIZE] = (Result){ k, surf };
    } else if (surf) {
        SDL_FreeSurface(surf);
    }
    SDL_UnlockMutex(g_res_lock);
}

/* ---------- Hilo cargador: tarjeta o, si no está, petición al móvil ---------- */

static int loader_main(void *unused)
{
    (void)unused;
    for (;;) {
        SDL_LockMutex(g_req_lock);
        while (g_req_count == 0 && !SDL_AtomicGet(&g_quit))
            SDL_CondWait(g_req_cond, g_req_lock);
        if (SDL_AtomicGet(&g_quit)) {
            SDL_UnlockMutex(g_req_lock);
            return 0;
        }
        TileKey k = g_requests[g_req_head];
        g_req_head = (g_req_head + 1) % QUEUE_SIZE;
        g_req_count--;
        SDL_UnlockMutex(g_req_lock);

        char path[256];
        tile_path(k, path, sizeof(path));
        SDL_Surface *surf = night_mode(IMG_Load(path));
        if (surf)
            push_result(k, surf);
        else if (!phone_request_tile(k.z, k.x, k.y))
            push_result(k, NULL);
        /* Si se pidió al móvil, la respuesta llega por tiles_on_phone_tile. */
    }
}

void tiles_on_phone_tile(int z, int x, int y, const void *png, int len)
{
    TileKey k = { z, x, y };
    if (!png || len <= 0) {
        push_result(k, NULL);
        return;
    }

    char path[256];
    tile_path(k, path, sizeof(path));
    mkdir_parents(path);
    FILE *f = fopen(path, "wb");
    if (f) {
        fwrite(png, 1, len, f);
        fclose(f);
    }
    push_result(k, night_mode(IMG_Load_RW(SDL_RWFromConstMem(png, len), 1)));
}

/* ---------- Hilo principal ---------- */

static Pending *find_pending(TileKey k)
{
    for (int i = 0; i < MAX_PENDING; i++) {
        if (g_pending[i].state != PEND_FREE && key_eq(g_pending[i].key, k))
            return &g_pending[i];
    }
    return NULL;
}

static void cache_insert(TileKey k, SDL_Texture *tex)
{
    CacheEntry *victim = &g_cache[0];
    for (int i = 0; i < CACHE_SIZE; i++) {
        CacheEntry *e = &g_cache[i];
        if (!e->tex) {
            victim = e;
            break;
        }
        if (e->last_used < victim->last_used)
            victim = e;
    }
    if (victim->tex)
        SDL_DestroyTexture(victim->tex);
    victim->key = k;
    victim->tex = tex;
    victim->last_used = g_frame;
}

void tiles_update(void)
{
    g_frame++;
    Uint32 now = SDL_GetTicks();

    for (int n = 0; n < UPLOADS_PER_FRAME; n++) {
        SDL_LockMutex(g_res_lock);
        if (g_res_count == 0) {
            SDL_UnlockMutex(g_res_lock);
            break;
        }
        Result r = g_results[g_res_head];
        g_res_head = (g_res_head + 1) % QUEUE_SIZE;
        g_res_count--;
        SDL_UnlockMutex(g_res_lock);

        Pending *p = find_pending(r.key);
        if (r.surf) {
            cache_insert(r.key, SDL_CreateTextureFromSurface(g_r, r.surf));
            SDL_FreeSurface(r.surf);
            if (p)
                p->state = PEND_FREE;
        } else if (p) {
            p->state = PEND_FAILED;
            p->since = now;
        }
    }

    /* Peticiones al móvil sin respuesta: liberar para reintentar. */
    for (int i = 0; i < MAX_PENDING; i++) {
        Pending *p = &g_pending[i];
        if ((p->state == PEND_LOADING && now - p->since > PHONE_WAIT_MS) ||
            (p->state == PEND_FAILED && now - p->since > RETRY_MS))
            p->state = PEND_FREE;
    }
}

SDL_Texture *tiles_peek(int z, int x, int y)
{
    TileKey k = { z, x, y };
    for (int i = 0; i < CACHE_SIZE; i++) {
        if (g_cache[i].tex && key_eq(g_cache[i].key, k)) {
            g_cache[i].last_used = g_frame;
            return g_cache[i].tex;
        }
    }
    return NULL;
}

SDL_Texture *tiles_get(int z, int x, int y)
{
    SDL_Texture *tex = tiles_peek(z, x, y);
    if (tex)
        return tex;

    TileKey k = { z, x, y };
    if (find_pending(k))
        return NULL;

    Pending *slot = NULL;
    for (int i = 0; i < MAX_PENDING && !slot; i++) {
        if (g_pending[i].state == PEND_FREE)
            slot = &g_pending[i];
    }
    if (!slot)
        return NULL;

    SDL_LockMutex(g_req_lock);
    bool queued = g_req_count < QUEUE_SIZE;
    if (queued) {
        g_requests[(g_req_head + g_req_count++) % QUEUE_SIZE] = k;
        SDL_CondSignal(g_req_cond);
    }
    SDL_UnlockMutex(g_req_lock);

    if (queued)
        *slot = (Pending){ k, PEND_LOADING, SDL_GetTicks() };
    return NULL;
}

void tiles_init(SDL_Renderer *renderer)
{
    g_r = renderer;
    g_req_lock = SDL_CreateMutex();
    g_req_cond = SDL_CreateCond();
    g_res_lock = SDL_CreateMutex();
    SDL_AtomicSet(&g_quit, 0);
    g_thread = SDL_CreateThread(loader_main, "tiles", NULL);
}

void tiles_shutdown(void)
{
    SDL_AtomicSet(&g_quit, 1);
    SDL_LockMutex(g_req_lock);
    SDL_CondSignal(g_req_cond);
    SDL_UnlockMutex(g_req_lock);
    SDL_WaitThread(g_thread, NULL);

    for (int i = 0; i < CACHE_SIZE; i++) {
        if (g_cache[i].tex)
            SDL_DestroyTexture(g_cache[i].tex);
    }
    memset(g_cache, 0, sizeof(g_cache));
    while (g_res_count > 0) {
        Result *r = &g_results[g_res_head];
        if (r->surf)
            SDL_FreeSurface(r->surf);
        g_res_head = (g_res_head + 1) % QUEUE_SIZE;
        g_res_count--;
    }
    SDL_DestroyMutex(g_req_lock);
    SDL_DestroyCond(g_req_cond);
    SDL_DestroyMutex(g_res_lock);
}
