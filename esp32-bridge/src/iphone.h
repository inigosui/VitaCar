// iPhone por Bluetooth: avisos y llamadas (ANCS) y música (AMS, Apple Media Service).
// La ESP32 se anuncia como "VitaCar"; el iPhone se enlaza una vez (con nRF Connect) y después
// se reconecta solo. Todo el Bluetooth va en su propia tarea para no frenar la WiFi de la Vita.
#pragma once
#include <Arduino.h>

// Categorías de ANCS que se usan.
#define ANCS_CAT_INCOMING_CALL 1
#define ANCS_CAT_MISSED_CALL   2

// Acciones sobre un aviso.
#define NOTIF_ACTION_POSITIVE  0   // contestar
#define NOTIF_ACTION_NEGATIVE  1   // rechazar / borrar

// Órdenes de música (códigos de AMS).
#define MEDIA_CMD_TOGGLE       2
#define MEDIA_CMD_NEXT         3
#define MEDIA_CMD_PREV         4

enum IphoneState { IPHONE_OFF, IPHONE_CONNECTING, IPHONE_READY };

struct IphoneNotif {
    uint32_t uid = 0;
    uint8_t category = 0;
    bool silent = false;      // ya estaba antes de conectar, o iOS la marca como silenciosa
    bool modified = false;    // es un cambio de una que ya se había mandado
    String app, title, text, time;   // time = "HH:mm"
};

struct IphoneEvent {
    enum Kind { NOTIF, REMOVED } kind;
    IphoneNotif notif;        // en REMOVED solo valen uid y category
};

struct IphoneMedia {
    bool active = false;      // hay una app de música abierta
    String app, title, artist;
    uint32_t dur_ms = 0;
    uint32_t elapsed_ms = 0;  // posición en el momento stamp (millis)
    uint32_t stamp = 0;
    bool playing = false;
};

void iphone_begin();
IphoneState iphone_state();
int iphone_battery();                         // %, -1 si no se sabe
bool iphone_next_event(IphoneEvent &ev);      // avisos nuevos o quitados, en orden
// Copia la música si ha cambiado desde seq (y actualiza seq).
bool iphone_media(IphoneMedia &out, uint32_t &seq);
void iphone_notif_action(uint32_t uid, uint8_t action);
void iphone_media_cmd(uint8_t cmd);
