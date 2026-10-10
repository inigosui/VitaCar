// Pantalla del puente: varias páginas que se cambian con el botón BOOT.
#pragma once
#include <Arduino.h>

enum DisplayPage { PAGE_SPEED, PAGE_STATUS, PAGE_SONG, PAGE_COUNT };

struct DisplayState {
    bool vita_connected = false;
    String vita_ip;
    String iphone;        // estado del iPhone
    bool iphone_ok = false;
    String title;         // canción ("" si no suena nada)
    String artist;
    bool playing = false;
    uint32_t pos_s = 0;   // segundos de la canción
    uint32_t dur_s = 0;
    String last_notif;
    String call;          // "" si no hay llamada
    int speed_kmh = -1;   // -1 = sin GPS
};

void display_begin();
// Redibuja solo lo que ha cambiado desde la última vez.
void display_update(const DisplayState &s);
// Pasa a la página siguiente; se guarda para el próximo arranque.
void display_next_page();
