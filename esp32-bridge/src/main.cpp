// VitaCar - puente ESP32: iPhone (Bluetooth) -> PS Vita (WiFi), con el mismo protocolo que el
// móvil Android (docs/PROTOCOLO.md).
// Avisos, llamadas y música del iPhone.
// Botón BOOT de la placa: cambia de página en la pantalla (velocidad, estado, música).

#include <Arduino.h>
#include <vector>
#include "config.h"
#include "display.h"
#include "vita_link.h"
#include "iphone.h"

// Llamada del iPhone (call_uid != 0).
static String call_state = "idle";
static String call_name;
static uint32_t call_uid = 0;

static std::vector<IphoneNotif> notifs;   // avisos del iPhone, el más nuevo al final
static IphoneMedia media;
static uint32_t media_seq = 0;
static int sent_battery = -2;
static IphoneState last_state = IPHONE_OFF;
static DisplayState screen;

static void send_hello()
{
    JsonDocument m;
    m["t"] = "hello";
    m["v"] = 1;
    m["name"] = BRIDGE_NAME;
    vita_send(m);
}

static void send_battery()
{
    sent_battery = iphone_battery();
    JsonDocument m;
    m["t"] = "battery";
    m["pct"] = sent_battery;
    m["charging"] = false;
    vita_send(m);
}

static uint32_t media_pos()
{
    uint32_t p = media.elapsed_ms + (media.playing ? millis() - media.stamp : 0);
    return media.dur_ms && p > media.dur_ms ? media.dur_ms : p;
}

static void send_media()
{
    JsonDocument m;
    m["t"] = "media";
    m["active"] = media.active;
    m["app"] = media.app;
    m["title"] = media.title;
    m["artist"] = media.artist;
    m["dur"] = media.dur_ms;
    m["pos"] = media_pos();
    m["playing"] = media.playing;
    m["art_id"] = 0;
    vita_send(m);
}

static void send_notif(const String &id, const String &app, const String &title, const String &text,
                       const String &time, bool silent)
{
    JsonDocument m;
    m["t"] = "notif";
    m["id"] = id;
    m["app"] = app;
    m["title"] = title;
    m["text"] = text;
    m["time"] = time;
    m["can_reply"] = false;
    m["silent"] = silent;
    vita_send(m);
}

static void send_notif_rm(const String &id)
{
    JsonDocument m;
    m["t"] = "notif_rm";
    m["id"] = id;
    vita_send(m);
}

static String ios_id(uint32_t uid) { return "ios" + String(uid); }

static void send_ios_notif(const IphoneNotif &n, bool silent)
{
    send_notif(ios_id(n.uid), n.app, n.title, n.text, n.time, silent);
}

static void send_call()
{
    JsonDocument m;
    m["t"] = "call";
    m["state"] = call_state;
    m["name"] = call_name;
    m["number"] = "";
    vita_send(m);
    screen.call = call_state == "ringing" ? call_name
                : call_state == "active"  ? "En curso" : "";
}

static void end_call()
{
    call_uid = 0;
    call_state = "idle";
    send_call();
}

// Al conectar la Vita: estado completo, igual que el móvil Android.
static void on_vita_connect()
{
    send_hello();
    send_battery();
    send_media();
    for (const IphoneNotif &n : notifs)
        send_ios_notif(n, true);
    send_call();
}

static int find_notif(uint32_t uid)
{
    for (size_t i = 0; i < notifs.size(); i++)
        if (notifs[i].uid == uid)
            return i;
    return -1;
}

// ---------- iPhone ----------

static void on_iphone_notif(const IphoneNotif &n)
{
    // Nunca se escribe el contenido (remitente, texto) en el registro: es privado.
    Serial.printf("iPhone aviso %u (%s, categoría %u)\n", n.uid, n.app.c_str(), n.category);
    if (n.category == ANCS_CAT_INCOMING_CALL) {
        // Las llamadas que ya sonaban antes de conectar también se muestran.
        call_uid = n.uid;
        call_state = "ringing";
        call_name = n.title.length() ? n.title : String("Llamada");
        send_call();
        return;
    }
    int i = find_notif(n.uid);
    if (i >= 0)
        notifs.erase(notifs.begin() + i);
    if (notifs.size() >= MAX_NOTIFS)
        notifs.erase(notifs.begin());
    notifs.push_back(n);
    send_ios_notif(n, n.silent || n.modified);
    if (!n.silent)
        screen.last_notif = n.app + ": " + n.title;
}

static void on_iphone_removed(uint32_t uid)
{
    if (uid == call_uid) {
        // Contestada desde la Vita: el iPhone quita el aviso, pero la llamada sigue.
        if (call_state != "active")
            end_call();
        return;
    }
    int i = find_notif(uid);
    if (i >= 0) {
        notifs.erase(notifs.begin() + i);
        send_notif_rm(ios_id(uid));
    }
}

static void poll_iphone()
{
    IphoneEvent ev;
    while (iphone_next_event(ev)) {
        if (ev.kind == IphoneEvent::NOTIF)
            on_iphone_notif(ev.notif);
        else
            on_iphone_removed(ev.notif.uid);
    }
    if (iphone_media(media, media_seq))
        send_media();

    IphoneState st = iphone_state();
    if (st == IPHONE_OFF && last_state != IPHONE_OFF) {
        // Sin iPhone, sus avisos ya no se pueden borrar ni actualizar: se quitan de la Vita.
        for (const IphoneNotif &n : notifs)
            send_notif_rm(ios_id(n.uid));
        notifs.clear();
        if (call_uid)
            end_call();
    }
    last_state = st;
    switch (st) {
    case IPHONE_OFF:
        screen.iphone = "Sin conectar";
        screen.iphone_ok = false;
        break;
    case IPHONE_CONNECTING:
        screen.iphone = "Conectando...";
        screen.iphone_ok = false;
        break;
    case IPHONE_READY:
        screen.iphone = iphone_battery() >= 0 ? "Conectado " + String(iphone_battery()) + "%" : String("Conectado");
        screen.iphone_ok = true;
        break;
    }
    if (iphone_battery() != sent_battery)
        send_battery();
}

// ---------- Vita ----------

static void on_vita_message(JsonDocument &msg)
{
    const char *t = msg["t"] | "";
    const char *action = msg["action"] | "";
    if (strcmp(t, "media_cmd") == 0) {
        if (strcmp(action, "play_pause") == 0)
            iphone_media_cmd(MEDIA_CMD_TOGGLE);
        else if (strcmp(action, "next") == 0)
            iphone_media_cmd(MEDIA_CMD_NEXT);
        else if (strcmp(action, "prev") == 0)
            iphone_media_cmd(MEDIA_CMD_PREV);
    } else if (strcmp(t, "call_cmd") == 0) {
        bool answer = strcmp(action, "answer") == 0;
        if (call_uid)
            iphone_notif_action(call_uid, answer ? NOTIF_ACTION_POSITIVE : NOTIF_ACTION_NEGATIVE);
        if (answer && call_state == "ringing") {
            call_state = "active";
            send_call();
        } else if (!answer) {
            end_call();
        }
    } else if (strcmp(t, "notif_dismiss") == 0) {
        String id = msg["id"] | "";
        if (id.startsWith("ios")) {
            uint32_t uid = strtoul(id.c_str() + 3, nullptr, 10);
            iphone_notif_action(uid, NOTIF_ACTION_NEGATIVE);   // lo borra también en el iPhone
            int i = find_notif(uid);
            if (i >= 0)
                notifs.erase(notifs.begin() + i);
        }
        send_notif_rm(id);
    } else if (strcmp(t, "tile") == 0) {
        // Sin internet no hay mapa: se dice que no hay tesela para que la Vita no espere.
        JsonDocument m;
        m["t"] = "tile_err";
        m["z"] = msg["z"];
        m["x"] = msg["x"];
        m["y"] = msg["y"];
        vita_send(m);
    }
}

static void check_button()
{
    static bool was_down = false;
    static uint32_t down_at = 0;
    bool down = digitalRead(PIN_BOOT_BTN) == LOW;
    if (down && !was_down)
        down_at = millis();
    if (!down && was_down && millis() - down_at > 30)   // 30 ms: ignora los rebotes
        display_next_page();
    was_down = down;
}

static void update_screen()
{
    screen.vita_connected = vita_connected();
    screen.vita_ip = vita_ip();
    screen.title = media.active ? media.title : String();
    screen.artist = media.artist;
    screen.playing = media.playing;
    screen.pos_s = media_pos() / 1000;
    screen.dur_s = media.dur_ms / 1000;
    display_update(screen);
}

void setup()
{
    Serial.begin(115200);
    pinMode(PIN_BOOT_BTN, INPUT_PULLUP);
    display_begin();
    vita_begin(on_vita_message, on_vita_connect);
    iphone_begin();
    Serial.println("VitaCar puente: avisos y música del iPhone");
}

void loop()
{
    vita_loop();
    poll_iphone();
    check_button();
    update_screen();
    delay(5);
}
