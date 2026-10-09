#include "agenda.h"

#include "third_party/cjson/cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

const SDL_Color AGENDA_PALETTE[AGENDA_COLORS] = {
    { 48, 209,  88, 255},   /* verde */
    { 10, 132, 255, 255},   /* azul */
    {255,  69,  58, 255},   /* rojo */
    {255, 159,  10, 255},   /* naranja */
    {255, 214,  10, 255},   /* amarillo */
    {191,  90, 242, 255},   /* morado */
    {255,  55,  95, 255},   /* rosa */
    {142, 142, 147, 255},   /* gris */
};

#define MAX_FILE  (512 * 1024)

static SDL_mutex *g_lock;           /* protege g_new */
static AgendaNote *g_new;           /* lista nueva del hilo de red, pendiente de recoger */
static int g_new_n;
static bool g_new_ready;
static AgendaNote *g_notes;         /* solo el hilo principal */
static int g_notes_n;

static void file_path(char *buf, size_t n, bool make_dir)
{
#ifdef __vita__
    if (make_dir)
        mkdir("ux0:data/VitaCar", 0777);
    snprintf(buf, n, "ux0:data/VitaCar/agenda.json");
#else
    const char *home = getenv("HOME");
    char dir[200];
    snprintf(dir, sizeof(dir), "%s/.cache/vitacar", home ? home : ".");
    if (make_dir)
        mkdir(dir, 0777);
    snprintf(buf, n, "%s/agenda.json", dir);
#endif
}

static int compare_notes(const void *pa, const void *pb)
{
    const AgendaNote *a = pa, *b = pb;
    if (a->year != b->year) return a->year - b->year;
    if (a->month != b->month) return a->month - b->month;
    if (a->day != b->day) return a->day - b->day;
    /* Las de todo el día (sin hora) primero. */
    return strcmp(a->time_str, b->time_str);
}

static void copy_str(const cJSON *obj, const char *key, char *dst, size_t n)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
    snprintf(dst, n, "%s", cJSON_IsString(v) ? v->valuestring : "");
}

/* "notes": [{date: "2026-10-09", time, title, text, color}, ...] */
static AgendaNote *parse_notes(const cJSON *msg, int *count)
{
    const cJSON *arr = cJSON_GetObjectItemCaseSensitive(msg, "notes");
    int n = cJSON_IsArray(arr) ? cJSON_GetArraySize(arr) : 0;
    if (n > AGENDA_MAX_NOTES)
        n = AGENDA_MAX_NOTES;
    *count = 0;
    AgendaNote *notes = calloc(n > 0 ? n : 1, sizeof(AgendaNote));
    if (!notes)
        return NULL;
    const cJSON *item;
    cJSON_ArrayForEach(item, arr) {
        if (*count == n)
            break;
        AgendaNote *a = &notes[*count];
        const cJSON *date = cJSON_GetObjectItemCaseSensitive(item, "date");
        if (!cJSON_IsString(date) || sscanf(date->valuestring, "%d-%d-%d", &a->year, &a->month, &a->day) != 3)
            continue;
        copy_str(item, "time", a->time_str, sizeof(a->time_str));
        copy_str(item, "title", a->title, sizeof(a->title));
        copy_str(item, "text", a->text, sizeof(a->text));
        const cJSON *color = cJSON_GetObjectItemCaseSensitive(item, "color");
        a->color = cJSON_IsNumber(color) ? color->valueint : 0;
        if (a->color < 0 || a->color >= AGENDA_COLORS)
            a->color = 0;
        (*count)++;
    }
    qsort(notes, *count, sizeof(AgendaNote), compare_notes);
    return notes;
}

static void set_pending(AgendaNote *notes, int n)
{
    SDL_LockMutex(g_lock);
    free(g_new);
    g_new = notes;
    g_new_n = n;
    g_new_ready = true;
    SDL_UnlockMutex(g_lock);
}

static void save(const cJSON *msg)
{
    char path[256];
    file_path(path, sizeof(path), true);
    char *text = cJSON_PrintUnformatted(msg);
    if (!text)
        return;
    FILE *f = fopen(path, "wb");
    if (f) {
        fwrite(text, 1, strlen(text), f);
        fclose(f);
    }
    cJSON_free(text);
}

static void load(void)
{
    char path[256];
    file_path(path, sizeof(path), false);
    FILE *f = fopen(path, "rb");
    if (!f)
        return;
    char *buf = malloc(MAX_FILE);
    size_t len = buf ? fread(buf, 1, MAX_FILE, f) : 0;
    fclose(f);
    cJSON *msg = len > 0 ? cJSON_ParseWithLength(buf, len) : NULL;
    free(buf);
    if (!msg)
        return;
    int n;
    AgendaNote *notes = parse_notes(msg, &n);
    cJSON_Delete(msg);
    if (notes)
        set_pending(notes, n);
}

void agenda_init(void)
{
    g_lock = SDL_CreateMutex();
    load();
}

void agenda_shutdown(void)
{
    free(g_new);
    free(g_notes);
    g_new = g_notes = NULL;
    g_new_n = g_notes_n = 0;
    SDL_DestroyMutex(g_lock);
}

void agenda_poll(void)
{
    SDL_LockMutex(g_lock);
    if (g_new_ready) {
        free(g_notes);
        g_notes = g_new;
        g_notes_n = g_new_n;
        g_new = NULL;
        g_new_n = 0;
        g_new_ready = false;
    }
    SDL_UnlockMutex(g_lock);
}

int agenda_notes(const AgendaNote **notes)
{
    *notes = g_notes;
    return g_notes_n;
}

void agenda_on_json(const cJSON *msg)
{
    int n;
    AgendaNote *notes = parse_notes(msg, &n);
    if (!notes)
        return;
    set_pending(notes, n);
    save(msg);
}
