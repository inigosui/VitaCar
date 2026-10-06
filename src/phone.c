#include "phone.h"
#include "net.h"

#include <SDL2/SDL_image.h>
#include "third_party/cjson/cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FRAME_JSON          1
#define FRAME_ART           2
#define FRAME_TILE          3
#define MAX_FRAME           (4 * 1024 * 1024)

#define CONNECT_TIMEOUT_MS  3000
#define RETRY_MS            2000
#define PING_INTERVAL_MS    3000
#define RX_TIMEOUT_MS       12000

#ifdef __vita__
/* Si existe, se usa esta IP en vez de la puerta de enlace (móvil y Vita en la misma WiFi). */
#define IP_OVERRIDE_FILE    "ux0:data/VitaCar/phone_ip.txt"
#endif

static SDL_Thread *g_thread;
static SDL_mutex *g_lock;           /* protege g_shared y la portada pendiente */
static SDL_mutex *g_send_lock;      /* protege g_sock y las escrituras */
static SDL_atomic_t g_quit;
static int g_sock = NET_INVALID;

static PhoneState g_shared;         /* lo escribe el hilo de red */
static PhoneState g_snap;           /* copia para el hilo principal */

static SDL_Surface *g_pending_art;
static int g_pending_art_id;
static int g_art_id;                /* id de portada anunciado en el último "media" */
static SDL_Texture *g_art;
static int g_art_tex_id;
static int g_wanted_art;            /* copia de g_art_id para el hilo principal */

/* ---------- Envío ---------- */

static bool send_frame(int type, const void *data, int len)
{
    Uint8 hdr[5];
    Uint32 n = (Uint32)len + 1;
    hdr[0] = n >> 24;
    hdr[1] = n >> 16;
    hdr[2] = n >> 8;
    hdr[3] = n;
    hdr[4] = (Uint8)type;

    bool ok = false;
    SDL_LockMutex(g_send_lock);
    if (g_sock != NET_INVALID)
        ok = net_send_all(g_sock, hdr, 5) && net_send_all(g_sock, data, len);
    SDL_UnlockMutex(g_send_lock);
    return ok;
}

static void send_json(cJSON *obj)
{
    char *text = obj ? cJSON_PrintUnformatted(obj) : NULL;
    if (text) {
        send_frame(FRAME_JSON, text, (int)strlen(text));
        cJSON_free(text);
    }
    cJSON_Delete(obj);
}

/* Mensaje {"t": type} al que se le añaden campos con cJSON_Add*ToObject. */
static cJSON *new_msg(const char *type)
{
    cJSON *obj = cJSON_CreateObject();
    cJSON_AddStringToObject(obj, "t", type);
    return obj;
}

static void send_simple(const char *type, const char *key, const char *value)
{
    cJSON *msg = new_msg(type);
    if (key)
        cJSON_AddStringToObject(msg, key, value);
    send_json(msg);
}

/* ---------- Lectura de JSON ---------- */

static const char *jstr_ptr(const cJSON *obj, const char *key)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsString(v) ? v->valuestring : NULL;
}

static void jstr(const cJSON *obj, const char *key, char *dst, size_t n)
{
    const char *v = jstr_ptr(obj, key);
    snprintf(dst, n, "%s", v ? v : "");
}

static double jnum(const cJSON *obj, const char *key, double def)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsNumber(v) ? v->valuedouble : def;
}

static bool jbool(const cJSON *obj, const char *key, bool def)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsBool(v) ? cJSON_IsTrue(v) : def;
}

static int find_notif(const char *id)
{
    for (int i = 0; i < g_shared.notif_count; i++) {
        if (strcmp(g_shared.notifs[i].id, id) == 0)
            return i;
    }
    return -1;
}

static void remove_notif(int i)
{
    memmove(&g_shared.notifs[i], &g_shared.notifs[i + 1],
            (g_shared.notif_count - i - 1) * sizeof(PhoneNotif));
    g_shared.notif_count--;
}

static void handle_notif(const cJSON *msg)
{
    PhoneNotif n;
    jstr(msg, "id", n.id, sizeof(n.id));
    jstr(msg, "app", n.app, sizeof(n.app));
    jstr(msg, "title", n.title, sizeof(n.title));
    jstr(msg, "text", n.text, sizeof(n.text));
    jstr(msg, "time", n.time_str, sizeof(n.time_str));
    n.can_reply = jbool(msg, "can_reply", false);

    int existing = find_notif(n.id);
    if (existing >= 0)
        remove_notif(existing);
    if (g_shared.notif_count == PHONE_MAX_NOTIFS)
        g_shared.notif_count--;
    memmove(&g_shared.notifs[1], &g_shared.notifs[0], g_shared.notif_count * sizeof(PhoneNotif));
    g_shared.notifs[0] = n;
    g_shared.notif_count++;

    /* Las que llegan al conectar ("silent") no deben mostrar el aviso emergente. */
    if (!jbool(msg, "silent", false))
        g_shared.notif_seq++;
}

static void handle_json(const char *text, int len)
{
    cJSON *msg = cJSON_ParseWithLength(text, len);
    if (!msg)
        return;
    const char *t = jstr_ptr(msg, "t");
    if (!t) {
        cJSON_Delete(msg);
        return;
    }

    if (strcmp(t, "ping") == 0) {
        send_simple("pong", NULL, NULL);
        cJSON_Delete(msg);
        return;
    }

    SDL_LockMutex(g_lock);
    PhoneState *s = &g_shared;
    if (strcmp(t, "hello") == 0) {
        jstr(msg, "name", s->phone_name, sizeof(s->phone_name));
        s->notif_count = 0;     /* el móvil reenvía las notificaciones activas */
    } else if (strcmp(t, "battery") == 0) {
        s->battery = (int)jnum(msg, "pct", -1);
        s->charging = jbool(msg, "charging", false);
    } else if (strcmp(t, "media") == 0) {
        s->media_active = jbool(msg, "active", true);
        jstr(msg, "app", s->media_app, sizeof(s->media_app));
        jstr(msg, "title", s->title, sizeof(s->title));
        jstr(msg, "artist", s->artist, sizeof(s->artist));
        s->duration_ms = (int)jnum(msg, "dur", 0);
        s->position_ms = (int)jnum(msg, "pos", 0);
        s->playing = jbool(msg, "playing", false);
        s->media_stamp = SDL_GetTicks();
        g_art_id = (int)jnum(msg, "art_id", 0);
    } else if (strcmp(t, "notif") == 0) {
        handle_notif(msg);
    } else if (strcmp(t, "notif_rm") == 0) {
        const char *id = jstr_ptr(msg, "id");
        int i = id ? find_notif(id) : -1;
        if (i >= 0)
            remove_notif(i);
    } else if (strcmp(t, "call") == 0) {
        const char *st = jstr_ptr(msg, "state");
        CallState cs = !st ? CALL_IDLE
                     : strcmp(st, "ringing") == 0 ? CALL_RINGING
                     : strcmp(st, "active") == 0 ? CALL_ACTIVE : CALL_IDLE;
        if (cs == CALL_ACTIVE && s->call != CALL_ACTIVE)
            s->call_since = SDL_GetTicks();
        s->call = cs;
        jstr(msg, "name", s->call_name, sizeof(s->call_name));
        jstr(msg, "number", s->call_number, sizeof(s->call_number));
    } else if (strcmp(t, "gps") == 0) {
        s->gps_valid = true;
        s->lat = jnum(msg, "lat", 0);
        s->lon = jnum(msg, "lon", 0);
        s->speed = (float)jnum(msg, "speed", 0);
        s->bearing = (float)jnum(msg, "bearing", 0);
    } else if (strcmp(t, "nav") == 0) {
        s->nav_active = jbool(msg, "active", false);
        jstr(msg, "title", s->nav_title, sizeof(s->nav_title));
        jstr(msg, "text", s->nav_text, sizeof(s->nav_text));
        jstr(msg, "sub", s->nav_sub, sizeof(s->nav_sub));
    } else if (strcmp(t, "weather") == 0) {
        s->weather_valid = true;
        s->temp = (float)jnum(msg, "temp", 0);
        s->temp_max = (float)jnum(msg, "max", 0);
        s->temp_min = (float)jnum(msg, "min", 0);
        s->weather_code = (int)jnum(msg, "code", 0);
        s->is_day = jbool(msg, "is_day", true);
        jstr(msg, "place", s->place, sizeof(s->place));
    } else if (strcmp(t, "tile_err") == 0) {
        SDL_UnlockMutex(g_lock);
        tiles_on_phone_tile((int)jnum(msg, "z", 0), (int)jnum(msg, "x", 0), (int)jnum(msg, "y", 0), NULL, 0);
        cJSON_Delete(msg);
        return;
    }
    SDL_UnlockMutex(g_lock);
    cJSON_Delete(msg);
}

static Uint32 read_be32(const Uint8 *p)
{
    return ((Uint32)p[0] << 24) | ((Uint32)p[1] << 16) | ((Uint32)p[2] << 8) | p[3];
}

static void handle_frame(int type, Uint8 *data, int len)
{
    if (type == FRAME_JSON) {
        handle_json((const char *)data, len);
    } else if (type == FRAME_ART && len > 4) {
        SDL_Surface *surf = IMG_Load_RW(SDL_RWFromConstMem(data + 4, len - 4), 1);
        if (!surf)
            return;
        SDL_LockMutex(g_lock);
        if (g_pending_art)
            SDL_FreeSurface(g_pending_art);
        g_pending_art = surf;
        g_pending_art_id = (int)read_be32(data);
        SDL_UnlockMutex(g_lock);
    } else if (type == FRAME_TILE && len > 9) {
        tiles_on_phone_tile(data[0], (int)read_be32(data + 1), (int)read_be32(data + 5), data + 9, len - 9);
    }
}

/* ---------- Hilo de red ---------- */

static bool recv_exact(int sock, void *buf, int len, Uint32 *last_rx, Uint32 *last_ping)
{
    char *p = buf;
    while (len > 0) {
        if (SDL_AtomicGet(&g_quit))
            return false;
        int r = net_recv(sock, p, len);
        Uint32 now = SDL_GetTicks();
        if (now - *last_ping >= PING_INTERVAL_MS) {
            send_simple("ping", NULL, NULL);
            *last_ping = now;
        }
        if (r == NET_TIMEOUT) {
            if (now - *last_rx > RX_TIMEOUT_MS)
                return false;
            continue;
        }
        if (r <= 0)
            return false;
        p += r;
        len -= r;
        *last_rx = now;
    }
    return true;
}

static void run_session(int sock)
{
    Uint32 last_rx = SDL_GetTicks(), last_ping = last_rx;
    Uint8 hdr[5];

    while (recv_exact(sock, hdr, 5, &last_rx, &last_ping)) {
        Uint32 n = read_be32(hdr);
        if (n < 1 || n > MAX_FRAME)
            return;
        int len = (int)n - 1;
        Uint8 *data = malloc(len + 1);
        if (!data)
            return;
        if (!recv_exact(sock, data, len, &last_rx, &last_ping)) {
            free(data);
            return;
        }
        data[len] = '\0';
        handle_frame(hdr[4], data, len);
        free(data);
    }
}

static void pick_target(const NetWifiInfo *wifi, char *ip, size_t n)
{
    snprintf(ip, n, "%s", wifi->gateway);
#ifdef IP_OVERRIDE_FILE
    FILE *f = fopen(IP_OVERRIDE_FILE, "r");
    if (f) {
        char line[32] = "";
        if (fgets(line, sizeof(line), f)) {
            line[strcspn(line, " \r\n")] = '\0';
            if (line[0])
                snprintf(ip, n, "%s", line);
        }
        fclose(f);
    }
#endif
}

static void reset_session_state(void)
{
    SDL_LockMutex(g_lock);
    g_shared.link = LINK_SEARCHING;
    g_shared.media_active = false;
    g_shared.playing = false;
    g_shared.call = CALL_IDLE;
    g_shared.nav_active = false;
    g_shared.battery = -1;
    g_art_id = 0;
    SDL_UnlockMutex(g_lock);
}

static int thread_main(void *unused)
{
    (void)unused;
    while (!SDL_AtomicGet(&g_quit)) {
        NetWifiInfo wifi;
        net_wifi_info(&wifi);

        SDL_LockMutex(g_lock);
        g_shared.link = wifi.connected ? LINK_SEARCHING : LINK_NO_WIFI;
        snprintf(g_shared.ssid, sizeof(g_shared.ssid), "%s", wifi.ssid);
        SDL_UnlockMutex(g_lock);

        char target[16];
        pick_target(&wifi, target, sizeof(target));
        int sock = (wifi.connected && target[0]) ? net_connect(target, PHONE_PORT, CONNECT_TIMEOUT_MS)
                                                 : NET_INVALID;
        if (sock == NET_INVALID) {
            for (int i = 0; i < RETRY_MS / 100 && !SDL_AtomicGet(&g_quit); i++)
                SDL_Delay(100);
            continue;
        }

        SDL_LockMutex(g_send_lock);
        g_sock = sock;
        SDL_UnlockMutex(g_send_lock);

        SDL_LockMutex(g_lock);
        g_shared.link = LINK_CONNECTED;
        snprintf(g_shared.phone_ip, sizeof(g_shared.phone_ip), "%s", target);
        SDL_UnlockMutex(g_lock);

        cJSON *hello = new_msg("hello");
        cJSON_AddNumberToObject(hello, "v", 1);
        cJSON_AddStringToObject(hello, "device", "PS Vita");
        send_json(hello);
        run_session(sock);

        SDL_LockMutex(g_send_lock);
        g_sock = NET_INVALID;
        SDL_UnlockMutex(g_send_lock);
        net_close(sock);
        reset_session_state();
        SDL_Delay(500);
    }
    return 0;
}

/* ---------- API del hilo principal ---------- */

void phone_start(void)
{
    IMG_Init(IMG_INIT_JPG | IMG_INIT_PNG);
    g_lock = SDL_CreateMutex();
    g_send_lock = SDL_CreateMutex();
    g_shared.battery = -1;
    g_shared.link = LINK_NO_WIFI;
    g_snap = g_shared;
    SDL_AtomicSet(&g_quit, 0);
    if (!net_init()) {
        SDL_Log("No se pudo iniciar la red");
        return;
    }
    g_thread = SDL_CreateThread(thread_main, "phone", NULL);
}

void phone_stop(void)
{
    SDL_AtomicSet(&g_quit, 1);
    SDL_LockMutex(g_send_lock);
    if (g_sock != NET_INVALID)
        net_abort(g_sock);
    SDL_UnlockMutex(g_send_lock);
    if (g_thread)
        SDL_WaitThread(g_thread, NULL);
    g_thread = NULL;
    if (g_art)
        SDL_DestroyTexture(g_art);
    if (g_pending_art)
        SDL_FreeSurface(g_pending_art);
    g_art = NULL;
    g_pending_art = NULL;
    net_shutdown();
    SDL_DestroyMutex(g_lock);
    SDL_DestroyMutex(g_send_lock);
    IMG_Quit();
}

void phone_poll(SDL_Renderer *renderer)
{
    SDL_LockMutex(g_lock);
    g_snap = g_shared;
    SDL_Surface *art = g_pending_art;
    int art_id = g_pending_art_id;
    g_pending_art = NULL;
    g_wanted_art = g_snap.media_active ? g_art_id : 0;
    SDL_UnlockMutex(g_lock);

    if (art) {
        if (g_art)
            SDL_DestroyTexture(g_art);
        g_art = SDL_CreateTextureFromSurface(renderer, art);
        g_art_tex_id = art_id;
        SDL_FreeSurface(art);
    }
}

const PhoneState *phone_state(void)
{
    return &g_snap;
}

SDL_Texture *phone_album_art(void)
{
    /* Solo si la portada cargada es la de la canción actual. */
    return (g_wanted_art != 0 && g_art_tex_id == g_wanted_art) ? g_art : NULL;
}

int phone_media_position_ms(void)
{
    int pos = g_snap.position_ms;
    if (g_snap.playing)
        pos += (int)(SDL_GetTicks() - g_snap.media_stamp);
    if (g_snap.duration_ms > 0 && pos > g_snap.duration_ms)
        pos = g_snap.duration_ms;
    return pos;
}

void phone_media_cmd(const char *action)
{
    send_simple("media_cmd", "action", action);
}

void phone_reply(const char *id, const char *text)
{
    cJSON *msg = new_msg("reply");
    cJSON_AddStringToObject(msg, "id", id);
    cJSON_AddStringToObject(msg, "text", text);
    send_json(msg);
}

void phone_dismiss(const char *id)
{
    send_simple("notif_dismiss", "id", id);
    SDL_LockMutex(g_lock);
    int i = find_notif(id);
    if (i >= 0)
        remove_notif(i);
    SDL_UnlockMutex(g_lock);
}

void phone_call_cmd(const char *action)
{
    send_simple("call_cmd", "action", action);
}

bool phone_request_tile(int z, int x, int y)
{
    cJSON *msg = new_msg("tile");
    cJSON_AddNumberToObject(msg, "z", z);
    cJSON_AddNumberToObject(msg, "x", x);
    cJSON_AddNumberToObject(msg, "y", y);
    char *text = cJSON_PrintUnformatted(msg);
    cJSON_Delete(msg);
    bool ok = text && send_frame(FRAME_JSON, text, (int)strlen(text));
    cJSON_free(text);
    return ok;
}
