#include "app.h"
#include "phone.h"
#include "screens.h"
#include "tiles.h"
#include "ui.h"

#include <math.h>

#ifdef __vita__
#include <psp2/kernel/processmgr.h>
#endif

/* Índices de botones del joystick SDL en la PS Vita. */
enum {
    VITA_TRIANGLE = 0, VITA_CIRCLE, VITA_CROSS, VITA_SQUARE,
    VITA_L, VITA_R,
    VITA_DOWN, VITA_LEFT, VITA_UP, VITA_RIGHT,
    VITA_SELECT, VITA_START,
};

/* Un toque se considera pulsación si el dedo se mueve menos que esto. */
#define TAP_SLOP 24.0f
/* Cada cuánto se avisa al sistema de que hay actividad (evita que se apague la pantalla). */
#define POWER_TICK_MS 5000

static int map_button(int button)
{
    switch (button) {
    case VITA_UP:     return IN_UP;
    case VITA_DOWN:   return IN_DOWN;
    case VITA_LEFT:   return IN_LEFT;
    case VITA_RIGHT:  return IN_RIGHT;
    case VITA_CROSS:  return IN_CONFIRM;
    case VITA_CIRCLE: return IN_BACK;
    case VITA_L:      return IN_PREV;
    case VITA_R:      return IN_NEXT;
    default:          return -1;
    }
}

static int map_key(SDL_Keycode key)
{
    switch (key) {
    case SDLK_UP:        return IN_UP;
    case SDLK_DOWN:      return IN_DOWN;
    case SDLK_LEFT:      return IN_LEFT;
    case SDLK_RIGHT:     return IN_RIGHT;
    case SDLK_RETURN:    return IN_CONFIRM;
    case SDLK_ESCAPE:
    case SDLK_BACKSPACE: return IN_BACK;
    case SDLK_q:         return IN_PREV;
    case SDLK_e:         return IN_NEXT;
    default:             return -1;
    }
}

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK) < 0) {
        SDL_Log("SDL_Init: %s", SDL_GetError());
        return 1;
    }

    SDL_Window *window = SDL_CreateWindow("VitaCar", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                          SCREEN_W, SCREEN_H, 0);
    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1,
                                                SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!window || !renderer) {
        SDL_Log("No se pudo crear la ventana: %s", SDL_GetError());
        return 1;
    }
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    if (!ui_init(renderer, ASSET_PATH "fonts/NotoSans-Regular.ttf", ASSET_PATH "fonts/NotoSans-Bold.ttf"))
        return 1;

    phone_start();
    tiles_init(renderer);

    SDL_Joystick *pad = SDL_NumJoysticks() > 0 ? SDL_JoystickOpen(0) : NULL;
    /* El dispositivo táctil 0 es la pantalla frontal; ignoramos el panel trasero. */
    SDL_TouchID front_touch = SDL_GetNumTouchDevices() > 0 ? SDL_GetTouchDevice(0) : 0;

    App app = { .running = true, .current = SCREEN_HOME, .music_focus = 1 };
    float touch_x = 0, touch_y = 0;
    Uint32 last_ticks = SDL_GetTicks(), last_power_tick = 0;

    while (app.running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            int action = -1;
            switch (ev.type) {
            case SDL_QUIT:
                app.running = false;
                break;
            case SDL_JOYBUTTONDOWN:
                action = map_button(ev.jbutton.button);
                break;
            case SDL_KEYDOWN:
                action = map_key(ev.key.keysym.sym);
                break;
            case SDL_FINGERDOWN:
                if (ev.tfinger.touchId == front_touch) {
                    touch_x = ev.tfinger.x * SCREEN_W;
                    touch_y = ev.tfinger.y * SCREEN_H;
                }
                break;
            case SDL_FINGERUP:
                if (ev.tfinger.touchId == front_touch) {
                    float dx = ev.tfinger.x * SCREEN_W - touch_x;
                    float dy = ev.tfinger.y * SCREEN_H - touch_y;
                    if (fabsf(dx) < TAP_SLOP && fabsf(dy) < TAP_SLOP)
                        screens_touch(&app, touch_x, touch_y);
                    else
                        screens_swipe(&app, touch_x, touch_y, dx, dy);
                }
                break;
            case SDL_MOUSEBUTTONUP:
                if (ev.button.which != SDL_TOUCH_MOUSEID && ev.button.button == SDL_BUTTON_LEFT)
                    screens_touch(&app, ev.button.x, ev.button.y);
                break;
            }
            if (action >= 0)
                screens_input(&app, (InputAction)action);
        }

        Uint32 now = SDL_GetTicks();
        phone_poll(renderer);
        tiles_update();
        screens_update(&app, (now - last_ticks) / 1000.0f);
        last_ticks = now;

#ifdef __vita__
        /* En el coche la pantalla debe seguir encendida y la WiFi activa. */
        if (now - last_power_tick >= POWER_TICK_MS) {
            sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DEFAULT);
            last_power_tick = now;
        }
#else
        (void)last_power_tick;
#endif

        ui_begin_frame();
        SDL_SetRenderDrawColor(renderer, COL_BG.r, COL_BG.g, COL_BG.b, 255);
        SDL_RenderClear(renderer);
        screens_draw(&app);
        SDL_RenderPresent(renderer);
    }

    if (pad)
        SDL_JoystickClose(pad);
    tiles_shutdown();
    phone_stop();
    ui_shutdown();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
