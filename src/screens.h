#pragma once

#include "app.h"

void screens_update(App *app, float dt);
void screens_draw(App *app);
void screens_input(App *app, InputAction action);
void screens_touch(App *app, float x, float y);
/* Arrastre del dedo de (x, y) con desplazamiento (dx, dy). */
void screens_swipe(App *app, float x, float y, float dx, float dy);
