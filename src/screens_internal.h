#pragma once

/* Compartido entre screens.c y las pantallas de cada app (app_*.c). */

#include "app.h"
#include "icons.h"
#include "phone.h"
#include "ui.h"

#define SIDEBAR_W     104
#define CONTENT_X     SIDEBAR_W
#define CONTENT_W     (SCREEN_W - SIDEBAR_W)

enum { APP_MUSIC, APP_MAPS, APP_PHONE, APP_MESSAGES, APP_AGENDA, APP_WEATHER, APP_SETTINGS, APP_COUNT };

typedef struct {
    const char *name;
    IconId icon;
    SDL_Color color;
    void (*draw)(App *app);
    /* Devuelve true si la pantalla gestionó la acción; si no, O vuelve al inicio. */
    bool (*input)(App *app, InputAction action);
    void (*touch)(App *app, float x, float y);
} AppDef;

extern const AppDef APPS[APP_COUNT];

bool hit_circle(float x, float y, float cx, float cy, float r);
bool hit_rect(float x, float y, float rx, float ry, float rw, float rh);
void hint_bar(const char *text);
void show_toast(App *app, const char *text);
void open_app(App *app, int index);
/* Botón redondeado con texto; focused dibuja el borde blanco de selección. */
void draw_button(float x, float y, float w, float h, const char *label, SDL_Color bg, bool focused);

void music_draw(App *app);
bool music_input(App *app, InputAction action);
void music_touch(App *app, float x, float y);

void maps_draw(App *app);
bool maps_input(App *app, InputAction action);
void maps_touch(App *app, float x, float y);

void phone_app_draw(App *app);
bool phone_app_input(App *app, InputAction action);
void phone_app_touch(App *app, float x, float y);

void messages_draw(App *app);
bool messages_input(App *app, InputAction action);
void messages_touch(App *app, float x, float y);
void messages_open(App *app, const char *id);
void messages_swipe(App *app, float dy);

void agenda_draw(App *app);
bool agenda_input(App *app, InputAction action);
void agenda_touch(App *app, float x, float y);

void weather_draw(App *app);
/* Texto e icono de un código meteorológico WMO. */
const char *weather_text(int code);
IconId weather_icon(int code);

void settings_draw(App *app);
