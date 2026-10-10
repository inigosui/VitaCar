/* Agenda: calendario del mes con las notas del móvil, reloj y pronóstico del día elegido. */

#include "screens_internal.h"
#include "agenda.h"
#include "sysinfo.h"

#include <math.h>
#include <stdio.h>

#define CAL_X      (CONTENT_X + 24)
#define CELL_W     72
#define CELL_H     60
#define CAL_W      (CELL_W * 7)
#define ARROW_Y    46
#define ARROW_R    22
#define DOW_Y      82
#define GRID_Y     110

#define SIDE_X     (CAL_X + CAL_W + 32)
#define SIDE_W     (SCREEN_W - 24 - SIDE_X)
#define SIDE_BOTTOM 496
#define NOTE_H     58

static const char *MONTHS[12] = {
    "enero", "febrero", "marzo", "abril", "mayo", "junio",
    "julio", "agosto", "septiembre", "octubre", "noviembre", "diciembre",
};
static const char *WEEKDAYS[7] = { "Lunes", "Martes", "Miércoles", "Jueves", "Viernes", "Sábado", "Domingo" };
static const char *WEEKDAYS_SHORT[7] = { "Lun", "Mar", "Mié", "Jue", "Vie", "Sáb", "Dom" };
static const char *DOW_LETTERS[7] = { "L", "M", "X", "J", "V", "S", "D" };

/* ---------- Fechas ---------- */

/* Días desde el 1-1-1970 (algoritmo de Howard Hinnant). */
static long day_number(int y, int m, int d)
{
    y -= m <= 2;
    long era = (y >= 0 ? y : y - 399) / 400;
    long yoe = y - era * 400;
    long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

static void from_day_number(long z, int *y, int *m, int *d)
{
    z += 719468;
    long era = (z >= 0 ? z : z - 146096) / 146097;
    long doe = z - era * 146097;
    long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    long mp = (5 * doy + 2) / 153;
    *d = (int)(doy - (153 * mp + 2) / 5 + 1);
    *m = (int)(mp < 10 ? mp + 3 : mp - 9);
    *y = (int)(yoe + era * 400 + (*m <= 2));
}

/* 0 = lunes … 6 = domingo. El 1-1-1970 fue jueves. */
static int weekday(int y, int m, int d)
{
    long n = day_number(y, m, d);
    return (int)(((n % 7) + 7 + 3) % 7);
}

static int days_in_month(int y, int m)
{
    static const int DAYS[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    bool leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
    return m == 2 && leap ? 29 : DAYS[m - 1];
}

static void ensure_selection(App *app)
{
    AgendaState *st = &app->agenda;
    if (st->year == 0)
        sys_local_date(&st->year, &st->month, &st->day);
}

static void move_days(App *app, int delta)
{
    AgendaState *st = &app->agenda;
    from_day_number(day_number(st->year, st->month, st->day) + delta, &st->year, &st->month, &st->day);
}

static void move_month(App *app, int delta)
{
    AgendaState *st = &app->agenda;
    int m = st->month - 1 + delta;
    st->year += m < 0 ? -1 : m / 12;
    st->month = (m % 12 + 12) % 12 + 1;
    int dim = days_in_month(st->year, st->month);
    if (st->day > dim)
        st->day = dim;
}

/* ---------- Dibujo ---------- */

static float cell_x(int col) { return CAL_X + col * CELL_W; }
static float cell_y(int row) { return GRID_Y + row * CELL_H; }

/* Texto oscuro sobre los colores claros (naranja, amarillo) para que se lea. */
static SDL_Color text_on(SDL_Color bg)
{
    float lum = 0.299f * bg.r + 0.587f * bg.g + 0.114f * bg.b;
    return lum > 165 ? COL_BG : COL_WHITE;
}

static void draw_arrow(float cx, int dir)
{
    ui_fill_circle(cx, ARROW_Y, ARROW_R, COL_PANEL);
    float h = 8, w = 7;
    ui_fill_triangle(cx - dir * w * 0.6f, ARROW_Y - h, cx - dir * w * 0.6f, ARROW_Y + h, cx + dir * w, ARROW_Y, COL_TEXT);
}

static void draw_calendar(App *app, int ty, int tm, int td)
{
    const AgendaState *st = &app->agenda;
    SDL_Color accent = APPS[APP_AGENDA].color;
    char buf[48];

    snprintf(buf, sizeof(buf), "%c%s %d", MONTHS[st->month - 1][0] - 32, MONTHS[st->month - 1] + 1, st->year);
    ui_text(FONT_TITLE, buf, CAL_X + CAL_W / 2.0f, ARROW_Y - ui_font_height(FONT_TITLE) / 2.0f, COL_TEXT, ALIGN_CENTER);
    draw_arrow(CAL_X + ARROW_R, -1);
    draw_arrow(CAL_X + CAL_W - ARROW_R, 1);

    for (int c = 0; c < 7; c++)
        ui_text(FONT_SMALL, DOW_LETTERS[c], cell_x(c) + CELL_W / 2.0f, DOW_Y, COL_TEXT_DIM, ALIGN_CENTER);

    /* Notas de cada día del mes: color de la primera y cuántas hay. */
    int first_color[32], count[32] = {0};
    const AgendaNote *notes;
    int n = agenda_notes(&notes);
    for (int i = 0; i < n; i++) {
        const AgendaNote *a = &notes[i];
        if (a->year != st->year || a->month != st->month || a->day < 1 || a->day > 31)
            continue;
        if (count[a->day]++ == 0)
            first_color[a->day] = a->color;
    }

    int first = weekday(st->year, st->month, 1);
    int dim = days_in_month(st->year, st->month);
    for (int d = 1; d <= dim; d++) {
        int slot = first + d - 1;
        float x = cell_x(slot % 7) + 4, y = cell_y(slot / 7) + 4;
        float w = CELL_W - 8, h = CELL_H - 8, r = 12;
        bool selected = d == st->day;
        bool today = st->year == ty && st->month == tm && d == td;
        SDL_Color fill = count[d] ? AGENDA_PALETTE[first_color[d]] : COL_BG;

        if (selected || today)
            ui_fill_round_rect(x - 3, y - 3, w + 6, h + 6, r + 3, selected ? COL_WHITE : accent);
        if (count[d] || selected || today)
            ui_fill_round_rect(x, y, w, h, r, fill);

        SDL_Color fg = count[d] ? text_on(fill) : COL_TEXT;
        FontId font = today ? FONT_CLOCK : FONT_LABEL;
        float ny = y + (count[d] > 1 ? (today ? 2 : 7) : (h - ui_font_height(font)) / 2.0f);
        snprintf(buf, sizeof(buf), "%d", d);
        ui_text(font, buf, x + w / 2, ny, fg, ALIGN_CENTER);

        /* Varias notas ese día: un punto por cada una (hasta 4). */
        int dots = count[d] > 4 ? 4 : count[d];
        for (int i = 0; dots > 1 && i < dots; i++)
            ui_fill_circle(x + w / 2 + (i - (dots - 1) / 2.0f) * 10, y + h - 9, 3, fg);
    }
}

static void draw_forecast(App *app, float y, long today_num)
{
    const AgendaState *st = &app->agenda;
    const PhoneState *ps = phone_state();
    SDL_Color sun = APPS[APP_WEATHER].color;
    float x = SIDE_X, w = SIDE_W, h = 84;
    char label[48], buf[96];

    long diff = day_number(st->year, st->month, st->day) - today_num;
    int wd = weekday(st->year, st->month, st->day);
    if (diff == 0)
        snprintf(label, sizeof(label), "Hoy");
    else if (diff == 1)
        snprintf(label, sizeof(label), "Mañana");
    else
        snprintf(label, sizeof(label), "%s %d %.3s", WEEKDAYS_SHORT[wd], st->day, MONTHS[st->month - 1]);

    ui_fill_round_rect(x, y, w, h, 16, COL_PANEL);

    const WeatherDay *day = NULL;
    for (int i = 0; i < ps->forecast_count; i++) {
        const WeatherDay *f = &ps->forecast[i];
        if (f->year == st->year && f->month == st->month && f->day == st->day)
            day = f;
    }
    if (!day) {
        ui_text(FONT_SMALL, label, x + 16, y + 12, COL_TEXT_DIM, ALIGN_LEFT);
        const char *why = !ps->weather_valid && phone_is_bridge() ? "Pronóstico no disponible con iPhone"
                        : !ps->weather_valid ? "Conecta el móvil para ver el tiempo"
                        : diff < 0 ? "Día pasado: sin pronóstico"
                        : "Aún sin pronóstico para ese día";
        ui_text_wrap(FONT_SMALL, why, x + 16, y + 38, w - 32, 2, COL_TEXT_DIM);
        return;
    }

    icon_draw(weather_icon(day->code), x + 42, y + h / 2, 48, sun, COL_PANEL);
    float tx = x + 80, tw = w - 92;
    snprintf(buf, sizeof(buf), "%s · %s", label, weather_text(day->code));
    ui_text_fit(FONT_SMALL, buf, tx, y + 14, tw, COL_TEXT_DIM, ALIGN_LEFT);
    snprintf(buf, sizeof(buf), "%d° / %d°", (int)lroundf(day->max), (int)lroundf(day->min));
    ui_text(FONT_BODY, buf, tx, y + 38, COL_TEXT, ALIGN_LEFT);
}

static void draw_day_notes(App *app, float y)
{
    const AgendaState *st = &app->agenda;
    const AgendaNote *notes;
    int n = agenda_notes(&notes);
    int first = -1, count = 0;
    for (int i = 0; i < n; i++) {
        if (notes[i].year == st->year && notes[i].month == st->month && notes[i].day == st->day) {
            if (first < 0)
                first = i;
            count++;
        }
    }

    char buf[64];
    snprintf(buf, sizeof(buf), count == 1 ? "1 nota" : "%d notas", count);
    ui_text(FONT_SMALL, count ? buf : "Sin notas", SIDE_X + 4, y, COL_TEXT_DIM, ALIGN_LEFT);
    y += 28;
    if (!count) {
        ui_text_wrap(FONT_SMALL, "Añádelas desde la app del móvil, en «Agenda».", SIDE_X + 4, y, SIDE_W - 8, 3, COL_TEXT_DIM);
        return;
    }

    int rows = (int)((SIDE_BOTTOM - y) / NOTE_H);
    for (int i = 0; i < count && i < rows; i++, y += NOTE_H) {
        float x = SIDE_X, w = SIDE_W, h = NOTE_H - 8;
        ui_fill_round_rect(x, y, w, h, 12, COL_PANEL);
        if (i == rows - 1 && count > rows) {
            snprintf(buf, sizeof(buf), "y %d más en el móvil", count - rows + 1);
            ui_text(FONT_LABEL, buf, x + w / 2, y + (h - ui_font_height(FONT_LABEL)) / 2.0f, COL_TEXT_DIM, ALIGN_CENTER);
            break;
        }
        const AgendaNote *a = &notes[first + i];
        ui_fill_round_rect(x, y, 8, h, 4, AGENDA_PALETTE[a->color]);
        float tx = x + 20, tw = w - 32;
        int time_w = 0;
        if (a->time_str[0])
            time_w = ui_text(FONT_SMALL, a->time_str, x + w - 12, y + 7, COL_TEXT_DIM, ALIGN_RIGHT) + 10;
        if (a->text[0]) {
            ui_text_fit(FONT_LABEL, a->title[0] ? a->title : "Nota", tx, y + 3, tw - time_w, COL_TEXT, ALIGN_LEFT);
            ui_text_fit(FONT_SMALL, a->text, tx, y + 27, tw, COL_TEXT_DIM, ALIGN_LEFT);
        } else {
            ui_text_fit(FONT_LABEL, a->title[0] ? a->title : "Nota", tx, y + (h - ui_font_height(FONT_LABEL)) / 2.0f,
                        tw - time_w, COL_TEXT, ALIGN_LEFT);
        }
    }
}

void agenda_draw(App *app)
{
    ensure_selection(app);
    int ty, tm, td, hour, minute;
    sys_local_date(&ty, &tm, &td);
    sys_local_time(&hour, &minute);

    draw_calendar(app, ty, tm, td);

    /* Derecha: reloj grande, fecha de hoy, tiempo y notas del día elegido. */
    char buf[64];
    float cx = SIDE_X + SIDE_W / 2.0f, y = 6;
    snprintf(buf, sizeof(buf), "%02d:%02d", hour, minute);
    ui_text(FONT_HUGE, buf, cx, y, COL_TEXT, ALIGN_CENTER);
    y += ui_font_height(FONT_HUGE) - 8;
    snprintf(buf, sizeof(buf), "%s, %d de %s", WEEKDAYS[weekday(ty, tm, td)], td, MONTHS[tm - 1]);
    ui_text_fit(FONT_BODY, buf, cx, y, SIDE_W, COL_TEXT_DIM, ALIGN_CENTER);
    y += ui_font_height(FONT_BODY) + 14;

    draw_forecast(app, y, day_number(ty, tm, td));
    draw_day_notes(app, y + 84 + 16);

    hint_bar("L / R  mes     X  hoy     O  inicio");
}

bool agenda_input(App *app, InputAction action)
{
    ensure_selection(app);
    switch (action) {
    case IN_LEFT:  move_days(app, -1); break;
    case IN_RIGHT: move_days(app, 1); break;
    case IN_UP:    move_days(app, -7); break;
    case IN_DOWN:  move_days(app, 7); break;
    case IN_PREV:  move_month(app, -1); break;
    case IN_NEXT:  move_month(app, 1); break;
    case IN_CONFIRM:
        app->agenda.year = 0;
        ensure_selection(app);
        break;
    default:
        return false;
    }
    return true;
}

void agenda_touch(App *app, float x, float y)
{
    ensure_selection(app);
    if (hit_circle(x, y, CAL_X + ARROW_R, ARROW_Y, ARROW_R + 12)) {
        move_month(app, -1);
        return;
    }
    if (hit_circle(x, y, CAL_X + CAL_W - ARROW_R, ARROW_Y, ARROW_R + 12)) {
        move_month(app, 1);
        return;
    }
    if (x < CAL_X || x >= CAL_X + CAL_W || y < GRID_Y)
        return;
    AgendaState *st = &app->agenda;
    int slot = (int)((y - GRID_Y) / CELL_H) * 7 + (int)((x - CAL_X) / CELL_W);
    int d = slot - weekday(st->year, st->month, 1) + 1;
    if (d >= 1 && d <= days_in_month(st->year, st->month))
        st->day = d;
}
