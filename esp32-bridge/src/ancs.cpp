// ANCS (Apple Notification Center Service): avisos y llamadas del iPhone.
#include "iphone_internal.h"
#include <deque>
#include <map>

static const NimBLEUUID UUID_ANCS("7905F431-B5CE-4E99-A40F-4B1E122D00D0");
static const NimBLEUUID UUID_NOTIF_SRC("9FBF120D-6301-42D9-8C58-25E699A21DBD");
static const NimBLEUUID UUID_CTRL_POINT("69D1D8F3-45E1-49A8-9821-9BBDFDAAD9D9");
static const NimBLEUUID UUID_DATA_SRC("22EAC6E9-24D6-4BB5-BE44-B36ACE7C7BFB");

// Órdenes y atributos de ANCS.
#define CMD_NOTIF_ATTRS   0
#define CMD_APP_ATTRS     1
#define CMD_PERFORM       2
#define ATTR_APP_ID       0
#define ATTR_TITLE        1
#define ATTR_MESSAGE      3
#define ATTR_DATE         5
#define ATTR_POS_LABEL    6
#define ATTR_NEG_LABEL    7
#define EVENT_ADDED       0
#define EVENT_MODIFIED    1
#define EVENT_REMOVED     2
#define FLAG_SILENT       0x01
#define FLAG_PRE_EXISTING 0x04

#define REQUEST_TIMEOUT_MS 4000

static NimBLEClient *client;
static NimBLERemoteCharacteristic *ctrl_point;

// Avisos pendientes de pedir sus textos.
struct Pending { uint32_t uid; uint8_t category; bool silent, modified; };
static std::deque<Pending> fetch_queue;
static std::map<std::string, String> app_names;   // identificador de la app -> nombre visible

// Petición en curso.
enum ReqKind { REQ_NONE, REQ_NOTIF, REQ_APP };
static ReqKind req = REQ_NONE;
static uint32_t req_started;
static std::vector<uint8_t> resp;
static IphoneNotif current;
static std::string current_app_id;

void ancs_reset()
{
    client = nullptr;
    ctrl_point = nullptr;
    req = REQ_NONE;
    fetch_queue.clear();
}

bool ancs_setup(NimBLEClient *c)
{
    NimBLERemoteService *svc = c->getService(UUID_ANCS);
    if (!svc) {
        Serial.println("El iPhone no ofrece ANCS (aún no enlazado o sin permiso)");
        return false;
    }
    NimBLERemoteCharacteristic *ns = svc->getCharacteristic(UUID_NOTIF_SRC);
    NimBLERemoteCharacteristic *ds = svc->getCharacteristic(UUID_DATA_SRC);
    ctrl_point = svc->getCharacteristic(UUID_CTRL_POINT);
    if (!ns || !ds || !ctrl_point)
        return false;
    // Primero el canal de datos, para no perder la respuesta a las primeras peticiones.
    if (!ds->subscribe(true, [](NimBLERemoteCharacteristic *, uint8_t *d, size_t l, bool) {
            iphone_queue_packet(PKT_ANCS_DATA, d, l);
        }))
        return false;
    if (!ns->subscribe(true, [](NimBLERemoteCharacteristic *, uint8_t *d, size_t l, bool) {
            iphone_queue_packet(PKT_ANCS_NOTIF, d, l);
        }))
        return false;
    client = c;
    Serial.println("ANCS listo");
    return true;
}

static bool write_ctrl(const uint8_t *data, size_t len)
{
    if (ctrl_point->writeValue(data, len, true))
        return true;
    // Errores de ANCS: 0xA1 orden desconocida, 0xA2 orden mal formada, 0xA3 parámetro no válido,
    // 0xA4 la acción ha fallado.
    Serial.printf("ANCS rechaza la orden %u: error %d\n", data[0], client ? client->getLastError() : -1);
    return false;
}

static void send_notif_request(uint32_t uid)
{
    uint8_t cmd[] = {
        CMD_NOTIF_ATTRS, (uint8_t)uid, (uint8_t)(uid >> 8), (uint8_t)(uid >> 16), (uint8_t)(uid >> 24),
        ATTR_APP_ID,
        ATTR_TITLE, 64, 0,
        ATTR_MESSAGE, 240, 0,
        ATTR_DATE,
        ATTR_POS_LABEL,
        ATTR_NEG_LABEL,
    };
    resp.clear();
    req = REQ_NOTIF;
    req_started = millis();
    if (!write_ctrl(cmd, sizeof(cmd)))
        req = REQ_NONE;
}

static void send_app_request(const std::string &app_id)
{
    std::vector<uint8_t> cmd;
    cmd.push_back(CMD_APP_ATTRS);
    cmd.insert(cmd.end(), app_id.begin(), app_id.end());
    cmd.push_back(0);
    cmd.push_back(0);   // atributo 0: nombre visible de la app
    resp.clear();
    req = REQ_APP;
    req_started = millis();
    if (!write_ctrl(cmd.data(), cmd.size()))
        req = REQ_NONE;
}

void ancs_perform(uint32_t uid, uint8_t action)
{
    if (!ctrl_point)
        return;
    uint8_t cmd[] = {CMD_PERFORM, (uint8_t)uid, (uint8_t)(uid >> 8), (uint8_t)(uid >> 16), (uint8_t)(uid >> 24), action};
    bool ok = write_ctrl(cmd, sizeof(cmd));
    Serial.printf("ANCS acción %u sobre el aviso %u: %s\n", action, uid, ok ? "aceptada" : "fallida");
}

// Lee atributos [id][long u16 LE][datos] desde off. Devuelve false si aún faltan bytes.
static bool parse_attrs(size_t off, int count, std::map<uint8_t, std::string> &out)
{
    for (int i = 0; i < count; i++) {
        if (off + 3 > resp.size())
            return false;
        uint8_t id = resp[off];
        size_t len = resp[off + 1] | (resp[off + 2] << 8);
        if (off + 3 + len > resp.size())
            return false;
        out[id] = std::string((const char *)&resp[off + 3], len);
        off += 3 + len;
    }
    return true;
}

static void finish_notif()
{
    auto it = app_names.find(current_app_id);
    current.app = it != app_names.end() ? it->second : String(current_app_id.c_str());
    req = REQ_NONE;
    IphoneEvent ev{IphoneEvent::NOTIF, current};
    iphone_emit(ev);
}

static void on_data(const std::vector<uint8_t> &bytes)
{
    if (req == REQ_NONE)
        return;
    resp.insert(resp.end(), bytes.begin(), bytes.end());
    std::map<uint8_t, std::string> attrs;

    if (req == REQ_NOTIF) {
        if (resp.size() < 5 || resp[0] != CMD_NOTIF_ATTRS || !parse_attrs(5, 6, attrs))
            return;
        current_app_id = attrs[ATTR_APP_ID];
        current.title = attrs[ATTR_TITLE].c_str();
        current.text = attrs[ATTR_MESSAGE].c_str();
        Serial.printf("  aviso %u: app=%s acciones=[%s]/[%s]\n", current.uid, current_app_id.c_str(),
                      attrs[ATTR_POS_LABEL].c_str(), attrs[ATTR_NEG_LABEL].c_str());
        const std::string &d = attrs[ATTR_DATE];   // "yyyyMMddTHHmmSS"
        current.time = d.size() >= 13 ? String((d.substr(9, 2) + ":" + d.substr(11, 2)).c_str()) : String();
        if (app_names.count(current_app_id) || current_app_id.empty())
            finish_notif();
        else
            send_app_request(current_app_id);
    } else {
        // [1][identificador\0][atributos]
        size_t z = 1;
        while (z < resp.size() && resp[z] != 0)
            z++;
        if (resp[0] != CMD_APP_ATTRS || z >= resp.size() || !parse_attrs(z + 1, 1, attrs))
            return;
        app_names[current_app_id] = attrs[0].empty() ? String(current_app_id.c_str()) : String(attrs[0].c_str());
        finish_notif();
    }
}

static void on_notification_source(const std::vector<uint8_t> &b)
{
    if (b.size() < 8)
        return;
    uint8_t event = b[0], flags = b[1], category = b[2];
    uint32_t uid = b[4] | (b[5] << 8) | (b[6] << 16) | ((uint32_t)b[7] << 24);
    Serial.printf("ANCS evento=%u flags=0x%02x categoria=%u uid=%u\n", event, flags, category, uid);
    if (event == EVENT_REMOVED) {
        for (auto it = fetch_queue.begin(); it != fetch_queue.end();)
            it = it->uid == uid ? fetch_queue.erase(it) : it + 1;
        IphoneEvent ev{IphoneEvent::REMOVED};
        ev.notif.uid = uid;
        ev.notif.category = category;
        iphone_emit(ev);
        return;
    }
    bool silent = (flags & (FLAG_SILENT | FLAG_PRE_EXISTING)) != 0;
    fetch_queue.push_back({uid, category, silent, event == EVENT_MODIFIED});
}

void ancs_handle(PacketSource src, const std::vector<uint8_t> &bytes)
{
    if (src == PKT_ANCS_DATA)
        on_data(bytes);
    else
        on_notification_source(bytes);
}

void ancs_tick()
{
    if (req != REQ_NONE && millis() - req_started > REQUEST_TIMEOUT_MS) {
        Serial.println("El iPhone no contestó a tiempo; se pasa al siguiente aviso");
        req = REQ_NONE;
    }
    if (req == REQ_NONE && !fetch_queue.empty() && ctrl_point) {
        Pending p = fetch_queue.front();
        fetch_queue.pop_front();
        current = IphoneNotif();
        current.uid = p.uid;
        current.category = p.category;
        current.silent = p.silent;
        current.modified = p.modified;
        send_notif_request(p.uid);
    }
}
