#pragma once

#include "phone.h"

#include <SDL2/SDL.h>
#include <stdbool.h>

#define APP_VERSION   "0.3.0"
#define SCREEN_HOME   (-1)

typedef enum {
    IN_UP,
    IN_DOWN,
    IN_LEFT,
    IN_RIGHT,
    IN_CONFIRM,
    IN_BACK,
    IN_PREV,
    IN_NEXT,
} InputAction;

typedef struct {
    int focus;          /* fila seleccionada en la lista */
    int scroll;
    bool open;          /* viendo el detalle de un mensaje */
    char open_id[128];
    int button;         /* botón seleccionado en el detalle */
} MessagesState;

typedef struct {
    bool running;
    int current;        /* índice en APPS o SCREEN_HOME */
    int home_focus;
    int music_focus;    /* 0 anterior, 1 reproducir, 2 siguiente */
    int map_zoom;
    int call_focus;     /* 0 rechazar, 1 contestar */
    MessagesState msg;

    Uint32 seen_notif_seq;
    int unread;
    PhoneNotif banner;
    Uint32 banner_until;
    CallState last_call;
    Uint32 seen_arrived;

    char toast[64];
    Uint32 toast_until;
} App;
