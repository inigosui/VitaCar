// AMS (Apple Media Service): la música que suena en el iPhone, con cualquier app (Spotify...).
#include "iphone_internal.h"

static const NimBLEUUID UUID_AMS("89D3502B-0F36-433A-8EF4-C502AD55F8DC");
static const NimBLEUUID UUID_REMOTE_CMD("9B3C81D8-57B1-4A8A-B8DF-0E56F7CA51C2");
static const NimBLEUUID UUID_ENTITY_UPDATE("2F7CABCE-808D-411F-9A0C-BB92BA96C102");

// Entidades y atributos de AMS.
#define ENTITY_PLAYER        0
#define ENTITY_TRACK         2
#define PLAYER_NAME          0
#define PLAYER_PLAYBACK_INFO 1   // "estado,velocidad,segundos": estado 0 pausa, 1 suena
#define TRACK_ARTIST         0
#define TRACK_TITLE          2
#define TRACK_DURATION       3   // segundos con decimales

// El iPhone manda título, artista, duración... uno a uno: se espera un poco para mandarlos juntos.
#define SETTLE_MS 150

static NimBLERemoteCharacteristic *remote_cmd;
static IphoneMedia m;
static bool dirty;
static uint32_t dirty_at;

void ams_reset()
{
    remote_cmd = nullptr;
    m = IphoneMedia();
    dirty = false;
}

bool ams_setup(NimBLEClient *client)
{
    NimBLERemoteService *svc = client->getService(UUID_AMS);
    if (!svc)
        return false;
    remote_cmd = svc->getCharacteristic(UUID_REMOTE_CMD);
    NimBLERemoteCharacteristic *eu = svc->getCharacteristic(UUID_ENTITY_UPDATE);
    if (!remote_cmd || !eu)
        return false;
    if (!eu->subscribe(true, [](NimBLERemoteCharacteristic *, uint8_t *d, size_t l, bool) {
            iphone_queue_packet(PKT_AMS_ENTITY, d, l);
        }))
        return false;
    // Pide los datos que interesan; el iPhone manda enseguida los valores actuales.
    const uint8_t player[] = {ENTITY_PLAYER, PLAYER_NAME, PLAYER_PLAYBACK_INFO};
    const uint8_t track[] = {ENTITY_TRACK, TRACK_ARTIST, TRACK_TITLE, TRACK_DURATION};
    if (!eu->writeValue(player, sizeof(player), true) || !eu->writeValue(track, sizeof(track), true))
        return false;
    Serial.println("AMS listo");
    return true;
}

void ams_handle(const std::vector<uint8_t> &b)
{
    // [entidad][atributo][marcas][valor en texto]
    if (b.size() < 3)
        return;
    uint8_t entity = b[0], attr = b[1];
    String value;
    value.concat((const char *)b.data() + 3, b.size() - 3);

    if (entity == ENTITY_PLAYER && attr == PLAYER_NAME) {
        m.app = value;
        m.active = value.length() > 0;
    } else if (entity == ENTITY_PLAYER && attr == PLAYER_PLAYBACK_INFO) {
        int c1 = value.indexOf(','), c2 = value.indexOf(',', c1 + 1);
        if (c1 < 0 || c2 < 0)
            return;
        m.playing = value.substring(0, c1).toInt() == 1;
        m.elapsed_ms = (uint32_t)(value.substring(c2 + 1).toFloat() * 1000);
        m.stamp = millis();
    } else if (entity == ENTITY_TRACK && attr == TRACK_ARTIST) {
        m.artist = value;
    } else if (entity == ENTITY_TRACK && attr == TRACK_TITLE) {
        m.title = value;
    } else if (entity == ENTITY_TRACK && attr == TRACK_DURATION) {
        m.dur_ms = (uint32_t)(value.toFloat() * 1000);
    } else {
        return;
    }
    dirty = true;
    dirty_at = millis();
}

void ams_tick()
{
    if (dirty && millis() - dirty_at >= SETTLE_MS) {
        dirty = false;
        // Sin título ni artista en el registro: solo la app y el estado.
        Serial.printf("Música: %s, %s\n", m.app.c_str(), m.playing ? "suena" : "pausa");
        iphone_set_media(m);
    }
}

void ams_command(uint8_t cmd)
{
    if (!remote_cmd)
        return;
    bool ok = remote_cmd->writeValue(&cmd, 1, true);
    Serial.printf("AMS orden %u: %s\n", cmd, ok ? "aceptada" : "fallida");
}
