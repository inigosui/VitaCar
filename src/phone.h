#pragma once

#include <SDL2/SDL.h>
#include <stdbool.h>

/* Conexión con la app compañera del móvil. Protocolo en docs/PROTOCOLO.md. */

#define PHONE_PORT        47474
#define PHONE_MAX_NOTIFS  24

typedef enum {
    LINK_NO_WIFI,       /* la Vita no está conectada a ninguna WiFi */
    LINK_SEARCHING,     /* hay WiFi pero el móvil no responde */
    LINK_CONNECTED,
} LinkState;

typedef enum {
    CALL_IDLE,
    CALL_RINGING,
    CALL_ACTIVE,
} CallState;

typedef struct {
    char id[128];
    char app[48];
    char title[128];
    char text[512];
    char time_str[8];       /* "14:05", hora local del móvil */
    bool can_reply;
} PhoneNotif;

typedef enum {
    TARGET_NONE,
    TARGET_MANUAL,      /* ux0:data/VitaCar/phone_ip.txt */
    TARGET_DISCOVERED,  /* el móvil respondió a la búsqueda */
    TARGET_GATEWAY,     /* puerta de enlace (punto de acceso del móvil) */
} TargetSource;

typedef struct {
    LinkState link;
    char phone_name[64];
    char phone_ip[16];
    char ssid[33];

    /* Diagnóstico de la búsqueda del móvil (se muestra en la app Teléfono). */
    char vita_ip[16];
    TargetSource target_src;
    int disc_sent;          /* preguntas enviadas sin error */
    int disc_rx;            /* respuestas o avisos del móvil recibidos */
    int disc_err;           /* último error de red de la búsqueda; 0 si ninguno */
    int battery;            /* -1 si no se sabe */
    bool charging;

    bool media_active;
    char media_app[48];
    char title[160];
    char artist[160];
    int duration_ms;
    int position_ms;
    bool playing;
    Uint32 media_stamp;     /* SDL_GetTicks() al recibir position_ms */

    PhoneNotif notifs[PHONE_MAX_NOTIFS];   /* la más reciente primero */
    int notif_count;
    Uint32 notif_seq;       /* sube con cada notificación nueva */

    CallState call;
    char call_name[96];
    char call_number[40];
    Uint32 call_since;

    bool gps_valid;
    double lat, lon;
    float speed;            /* m/s */
    float bearing;          /* grados, 0 = norte */

    bool nav_active;
    char nav_title[160];
    char nav_text[256];
    char nav_sub[160];

    bool weather_valid;
    float temp, temp_max, temp_min;
    int weather_code;       /* código WMO */
    bool is_day;
    char place[64];
} PhoneState;

void phone_start(void);
void phone_stop(void);

/* Hilo principal, una vez por frame: copia el estado y sube texturas pendientes. */
void phone_poll(SDL_Renderer *renderer);
const PhoneState *phone_state(void);
SDL_Texture *phone_album_art(void);
int phone_media_position_ms(void);

void phone_media_cmd(const char *action);       /* "play_pause", "next", "prev" */
void phone_reply(const char *id, const char *text);
void phone_dismiss(const char *id);
void phone_call_cmd(const char *action);        /* "answer", "hangup" */
/* Pide una tesela al móvil. Devuelve false si no hay conexión. Seguro desde cualquier hilo. */
bool phone_request_tile(int z, int x, int y);

/* Lo llama el hilo de red al recibir una tesela. Implementado en tiles.c. */
void tiles_on_phone_tile(int z, int x, int y, const void *png, int len);
