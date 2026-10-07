#include "phone.h"
#include "audio.h"
#include "net.h"

#include <SDL2/SDL_image.h>
#include <math.h>
#include "third_party/cjson/cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FRAME_JSON          1
#define FRAME_ART           2
#define FRAME_TILE          3
#define FRAME_AUDIO         4
#define MAX_FRAME           (4 * 1024 * 1024)
#define MAX_ROUTE_POINTS    20000
#define PI_D                3.14159265358979323846

#define CONNECT_TIMEOUT_MS  3000
#define RETRY_MS            2000
#define PING_INTERVAL_MS    3000
#define RX_TIMEOUT_MS       12000
#define DISCOVER_MS         2500
#define QUERY_MS            500
#define DISCOVER_QUERY      "VITACAR?"  /* pregunta de la Vita */
#define DISCOVER_MAGIC      "VITACAR1"  /* respuesta y aviso del móvil */

#ifdef __vita__
/* Si existe, se usa esta IP en vez de la puerta de enlace (móvil y Vita en la misma WiFi). */
#define IP_OVERRIDE_FILE    "ux0:data/VitaCar/phone_ip.txt"
#endif

static SDL_Thread *g_thread;
static SDL_mutex *g_lock;           /* protege g_shared y la portada pendiente */
static SDL_mutex *g_send_lock;      /* protege g_sock y las escrituras */
static SDL_atomic_t g_quit;
static int g_sock = NET_INVALID;
static int g_udp = NET_INVALID;     /* recibe los avisos del móvil; solo lo usa el hilo de red */

static PhoneState g_shared;         /* lo escribe el hilo de red */
static PhoneState g_snap;           /* copia para el hilo principal */

static SDL_Surface *g_pending_art;
static int g_pending_art_id;
static int g_art_id;                /* id de portada anunciado en el último "media" */
static SDL_Texture *g_art;
static int g_art_tex_id;
static int g_wanted_art;            /* copia de g_art_id para el hilo principal */

/* Ruta: el hilo de red deja la nueva en g_route_new (con g_lock) y phone_poll la pasa a g_route. */
static double *g_route_new;
static int g_route_new_n;
static bool g_route_new_ready;
static double *g_route;             /* solo el hilo principal */
static int g_route_n;

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

/* Sustituye la ruta pendiente. Con g_lock tomado. */
static void set_pending_route(double *pts, int n)
{
    free(g_route_new);
    g_route_new = pts;
    g_route_new_n = n;
    g_route_new_ready = true;
}

/* "pts" = [lat0, lon0, lat1, lon1, ...]. Se convierte aquí a Web Mercator para no
 * repetir senos y logaritmos en cada frame. Se hace fuera de g_lock: puede tardar. */
static double *parse_route_points(const cJSON *msg, int *count)
{
    const cJSON *arr = cJSON_GetObjectItemCaseSensitive(msg, "pts");
    int n = cJSON_IsArray(arr) ? cJSON_GetArraySize(arr) / 2 : 0;
    if (n > MAX_ROUTE_POINTS)
        n = MAX_ROUTE_POINTS;
    *count = 0;
    if (n < 2)
        return NULL;
    double *pts = malloc(sizeof(double) * 2 * n);
    if (!pts)
        return NULL;
    const cJSON *v = arr->child;
    for (int i = 0; i < n && v && v->next; i++, v = v->next->next) {
        double lat = v->valuedouble, lon = v->next->valuedouble;
        if (lat > 85.0) lat = 85.0;
        if (lat < -85.0) lat = -85.0;
        double lat_r = lat * PI_D / 180.0;
        pts[i * 2] = (lon + 180.0) / 360.0;
        pts[i * 2 + 1] = (1.0 - log(tan(lat_r) + 1.0 / cos(lat_r)) / PI_D) / 2.0;
        *count = i + 1;
    }
    return pts;
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

    int route_n = 0;
    double *route_pts = NULL;
    bool route_on = strcmp(t, "route") == 0 && jbool(msg, "active", false);
    if (route_on)
        route_pts = parse_route_points(msg, &route_n);

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
    } else if (strcmp(t, "route") == 0) {
        s->route_active = route_on && route_n >= 2;
        jstr(msg, "dest", s->route_dest, sizeof(s->route_dest));
        s->route_left_m = (float)jnum(msg, "dist", 0);
        s->route_left_s = (float)jnum(msg, "dur", 0);
        jstr(msg, "arrive", s->route_arrive, sizeof(s->route_arrive));
        s->route_idx = 0;
        if (!route_on && jbool(msg, "arrived", false))
            s->route_arrived++;
        set_pending_route(route_pts, route_n);
        route_pts = NULL;
    } else if (strcmp(t, "route_left") == 0) {
        s->route_left_m = (float)jnum(msg, "dist", 0);
        s->route_left_s = (float)jnum(msg, "dur", 0);
        jstr(msg, "arrive", s->route_arrive, sizeof(s->route_arrive));
        s->route_idx = (int)jnum(msg, "idx", 0);
    } else if (strcmp(t, "audio") == 0) {
        s->audio_active = jbool(msg, "active", false);
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
    free(route_pts);
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
    } else if (type == FRAME_AUDIO) {
        audio_feed(data, len);
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

static bool read_ip_override(char *ip, size_t n)
{
#ifdef IP_OVERRIDE_FILE
    FILE *f = fopen(IP_OVERRIDE_FILE, "r");
    if (f) {
        char line[32] = "";
        bool ok = false;
        if (fgets(line, sizeof(line), f)) {
            line[strcspn(line, " \r\n")] = '\0';
            if (line[0]) {
                snprintf(ip, n, "%s", line);
                ok = true;
            }
        }
        fclose(f);
        return ok;
    }
#endif
    (void)ip;
    (void)n;
    return false;
}

static void note_discovery(int sent, int rx, int err)
{
    SDL_LockMutex(g_lock);
    g_shared.disc_sent += sent;
    g_shared.disc_rx += rx;
    if (err)
        g_shared.disc_err = err;
    SDL_UnlockMutex(g_lock);
}

/* Pregunta a toda la red por el móvil, que responde directamente a la Vita. */
static void send_query(const NetWifiInfo *wifi)
{
    const char *targets[] = { wifi->broadcast, "255.255.255.255" };
    for (int i = 0; i < 2; i++) {
        if (!targets[i][0] || (i == 1 && strcmp(targets[0], targets[1]) == 0))
            continue;
        int r = net_sendto(g_udp, DISCOVER_QUERY, strlen(DISCOVER_QUERY), targets[i], PHONE_PORT);
        note_discovery(r >= 0, 0, r < 0 ? r : 0);
    }
}

/* Busca el móvil durante DISCOVER_MS: pregunta cada QUERY_MS y acepta tanto su respuesta
 * como el aviso que envía por su cuenta. El socket sigue abierto entre intentos, así
 * que puede haber avisos antiguos en cola: se queda con el último. */
static bool discover_phone(const NetWifiInfo *wifi, char *ip, size_t n)
{
    if (g_udp == NET_INVALID) {
        /* En el puerto fijo llegan también los avisos del móvil. Si no se puede usar, vale
         * uno libre: el móvil responde a la pregunta en el puerto del que salió. */
        g_udp = net_udp_listen(PHONE_PORT);
        if (g_udp < 0) {
            note_discovery(0, 0, g_udp);
            g_udp = net_udp_listen(0);
        }
        if (g_udp < 0) {
            note_discovery(0, 0, g_udp);
            g_udp = NET_INVALID;
            return false;
        }
    }

    bool found = false;
    Uint32 start = SDL_GetTicks(), next_query = start;
    while (!SDL_AtomicGet(&g_quit)) {
        Uint32 now = SDL_GetTicks();
        if (!found && (Sint32)(now - next_query) >= 0) {
            send_query(wifi);
            next_query = now + QUERY_MS;
        }
        char buf[64], from[16];
        int r = net_recvfrom(g_udp, buf, sizeof(buf) - 1, from, sizeof(from));
        if (r == NET_TIMEOUT) {
            if (found || SDL_GetTicks() - start >= DISCOVER_MS)
                break;
            continue;
        }
        if (r < 0) {
            note_discovery(0, 0, r);
            net_close(g_udp);
            g_udp = NET_INVALID;
            break;
        }
        buf[r] = '\0';
        /* Ignora la propia pregunta, que también llega a la Vita por ser broadcast. */
        if (strcmp(buf, DISCOVER_MAGIC) == 0 && from[0] && strcmp(from, wifi->ip) != 0) {
            snprintf(ip, n, "%s", from);
            found = true;
            note_discovery(0, 1, 0);
        }
    }
    return found;
}

/* Prioridad: IP escrita a mano, búsqueda en la red y, por último, la puerta de enlace
 * (el caso del punto de acceso del móvil, por si la red bloquea los broadcasts). */
static TargetSource pick_target(const NetWifiInfo *wifi, char *ip, size_t n)
{
    if (read_ip_override(ip, n))
        return TARGET_MANUAL;
    if (discover_phone(wifi, ip, n))
        return TARGET_DISCOVERED;
    snprintf(ip, n, "%s", wifi->gateway);
    return TARGET_GATEWAY;
}

static void reset_session_state(void)
{
    SDL_LockMutex(g_lock);
    g_shared.link = LINK_SEARCHING;
    g_shared.media_active = false;
    g_shared.playing = false;
    g_shared.call = CALL_IDLE;
    g_shared.nav_active = false;
    g_shared.route_active = false;      /* el móvil la reenvía al volver a conectar */
    g_shared.audio_active = false;
    set_pending_route(NULL, 0);
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
        snprintf(g_shared.vita_ip, sizeof(g_shared.vita_ip), "%s", wifi.ip);
        SDL_UnlockMutex(g_lock);

        char target[16] = "";
        TargetSource src = wifi.connected ? pick_target(&wifi, target, sizeof(target)) : TARGET_NONE;
        SDL_LockMutex(g_lock);
        g_shared.target_src = src;
        SDL_UnlockMutex(g_lock);
        int sock = target[0] ? net_connect(target, PHONE_PORT, CONNECT_TIMEOUT_MS) : NET_INVALID;
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
    net_close(g_udp);
    g_udp = NET_INVALID;
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
    audio_init();
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
    audio_shutdown();
    if (g_art)
        SDL_DestroyTexture(g_art);
    if (g_pending_art)
        SDL_FreeSurface(g_pending_art);
    g_art = NULL;
    g_pending_art = NULL;
    free(g_route);
    free(g_route_new);
    g_route = g_route_new = NULL;
    g_route_n = g_route_new_n = 0;
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
    if (g_route_new_ready) {
        free(g_route);
        g_route = g_route_new;
        g_route_n = g_route_new_n;
        g_route_new = NULL;
        g_route_new_n = 0;
        g_route_new_ready = false;
    }
    SDL_UnlockMutex(g_lock);

    audio_update(g_snap.audio_active);

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

int phone_route(const double **xy)
{
    *xy = g_route;
    return g_snap.route_active ? g_route_n : 0;
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
