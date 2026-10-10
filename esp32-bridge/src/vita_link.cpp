#include "vita_link.h"
#include "config.h"
#include <WiFi.h>
#include <WiFiUdp.h>

#define FRAME_JSON        1
#define MAX_RX_FRAME      8192     // la Vita solo manda JSON cortos
#define RX_TIMEOUT_MS     15000
#define ANNOUNCE_MS       1000

static WiFiServer server(VITA_PORT);
static WiFiClient client;
static WiFiUDP udp;
static VitaMessageHandler handler;
static VitaConnectHandler connect_handler;
static uint32_t last_rx, last_announce;

static uint8_t hdr[5];
static size_t hdr_got;
static uint8_t *body;
static size_t body_len, body_got;

static void drop_client(const char *why)
{
    Serial.printf("Vita desconectada: %s\n", why);
    client.stop();
    free(body);
    body = nullptr;
    hdr_got = 0;
}

void vita_begin(VitaMessageHandler on_msg, VitaConnectHandler on_connect)
{
    handler = on_msg;
    connect_handler = on_connect;
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASSWORD);
    Serial.printf("WiFi \"%s\" creada, IP %s\n", AP_SSID, WiFi.softAPIP().toString().c_str());
    server.begin();
    server.setNoDelay(true);
    udp.begin(VITA_PORT);
}

bool vita_connected() { return client && client.connected(); }

String vita_ip() { return vita_connected() ? client.remoteIP().toString() : String(); }

void vita_send(JsonDocument &msg)
{
    if (!vita_connected())
        return;
    String json;
    serializeJson(msg, json);
    uint32_t n = json.length() + 1;
    uint8_t h[5] = {(uint8_t)(n >> 24), (uint8_t)(n >> 16), (uint8_t)(n >> 8), (uint8_t)n, FRAME_JSON};
    client.write(h, 5);
    client.write((const uint8_t *)json.c_str(), json.length());
}

static void handle_json(const uint8_t *data, size_t len)
{
    JsonDocument msg;
    if (deserializeJson(msg, data, len)) {
        Serial.println("JSON de la Vita no válido");
        return;
    }
    const char *t = msg["t"] | "";
    if (strcmp(t, "ping") == 0) {
        JsonDocument pong;
        pong["t"] = "pong";
        vita_send(pong);
        return;
    }
    // Solo el tipo y la acción: una respuesta podría llevar texto privado.
    if (strcmp(t, "pong") != 0)
        Serial.printf("Vita -> %s %s\n", t, msg["action"] | "");
    handler(msg);
}

static void read_client()
{
    while (client.available()) {
        last_rx = millis();
        if (hdr_got < 5) {
            hdr[hdr_got++] = client.read();
            if (hdr_got < 5)
                continue;
            uint32_t n = ((uint32_t)hdr[0] << 24) | ((uint32_t)hdr[1] << 16) | ((uint32_t)hdr[2] << 8) | hdr[3];
            if (n < 1 || n > MAX_RX_FRAME) {
                drop_client("trama de tamaño raro");
                return;
            }
            body_len = n - 1;
            body_got = 0;
            body = (uint8_t *)malloc(body_len + 1);
            if (!body) {
                drop_client("sin memoria");
                return;
            }
        } else {
            body_got += client.read(body + body_got, body_len - body_got);
        }
        if (hdr_got == 5 && body_got == body_len) {
            if (hdr[4] == FRAME_JSON)
                handle_json(body, body_len);
            free(body);
            body = nullptr;
            hdr_got = 0;
        }
    }
}

// Contesta a la pregunta de la Vita y, sin Vita, avisa cada segundo por broadcast.
static void discovery()
{
    char buf[16];
    int n;
    while ((n = udp.parsePacket()) > 0) {
        int r = udp.read(buf, sizeof(buf) - 1);
        buf[r > 0 ? r : 0] = '\0';
        if (strcmp(buf, "VITACAR?") == 0) {
            udp.beginPacket(udp.remoteIP(), udp.remotePort());
            udp.print("VITACAR1");
            udp.endPacket();
        }
    }
    if (!vita_connected() && millis() - last_announce >= ANNOUNCE_MS) {
        last_announce = millis();
        udp.beginPacket(IPAddress(192, 168, 4, 255), VITA_PORT);
        udp.print("VITACAR1");
        udp.endPacket();
    }
}

void vita_loop()
{
    discovery();

    WiFiClient incoming = server.available();
    if (incoming) {
        if (vita_connected())
            drop_client("ha entrado otra conexión");
        client = incoming;
        client.setNoDelay(true);
        last_rx = millis();
        hdr_got = 0;
        Serial.printf("Vita conectada desde %s\n", client.remoteIP().toString().c_str());
        connect_handler();
    }

    if (!client)
        return;
    if (!client.connected()) {
        drop_client("cerrada por la Vita");
        return;
    }
    read_client();
    if (client && millis() - last_rx > RX_TIMEOUT_MS)
        drop_client("sin respuesta");
}
