#include "iphone_internal.h"
#include <deque>
#include <mutex>

static const NimBLEUUID UUID_ANCS("7905F431-B5CE-4E99-A40F-4B1E122D00D0");
static const NimBLEUUID UUID_BATTERY_SVC((uint16_t)0x180F);
static const NimBLEUUID UUID_BATTERY_LVL((uint16_t)0x2A19);

#define SETUP_RETRY_MS 3000

// Parámetros de conexión pedidos al iPhone (unidades de 1,25 ms y 10 ms), dentro de lo que
// admite Apple. El margen de 4 s evita cortes cuando la WiFi ocupa la antena un momento.
#define CONN_MIN_INTERVAL 24       // 30 ms
#define CONN_MAX_INTERVAL 40       // 50 ms
#define CONN_TIMEOUT      400      // 4 s

struct RawPacket { PacketSource src; std::vector<uint8_t> bytes; };
struct Command { bool media; uint32_t uid; uint8_t code; };

static std::mutex lock;            // protege todo lo que comparten las tareas
static std::deque<RawPacket> packets;
static std::deque<Command> commands;
static std::deque<IphoneEvent> events;
static IphoneMedia media;
static uint32_t media_seq;
static IphoneState state = IPHONE_OFF;
static volatile int battery = -1;

static volatile uint16_t conn_handle = BLE_HS_CONN_HANDLE_NONE;
static volatile bool encrypted = false;
static volatile bool lost = false;  // se ha cortado desde la última vuelta de la tarea

// ---------- Lo que usa el resto del programa ----------

IphoneState iphone_state()
{
    std::lock_guard<std::mutex> g(lock);
    return state;
}

int iphone_battery() { return battery; }

bool iphone_next_event(IphoneEvent &ev)
{
    std::lock_guard<std::mutex> g(lock);
    if (events.empty())
        return false;
    ev = events.front();
    events.pop_front();
    return true;
}

bool iphone_media(IphoneMedia &out, uint32_t &seq)
{
    std::lock_guard<std::mutex> g(lock);
    if (seq == media_seq)
        return false;
    out = media;
    seq = media_seq;
    return true;
}

// Las órdenes que llegan sin el iPhone listo se descartan: si se guardaran, al reconectar
// se ejecutarían todas seguidas (pausa, play, pausa...).
void iphone_notif_action(uint32_t uid, uint8_t action)
{
    std::lock_guard<std::mutex> g(lock);
    if (state == IPHONE_READY)
        commands.push_back({false, uid, action});
}

void iphone_media_cmd(uint8_t cmd)
{
    std::lock_guard<std::mutex> g(lock);
    if (state == IPHONE_READY)
        commands.push_back({true, 0, cmd});
    else
        Serial.println("Orden de música descartada: iPhone sin conectar");
}

// ---------- Para ancs.cpp y ams.cpp ----------

void iphone_queue_packet(PacketSource src, const uint8_t *data, size_t len)
{
    std::lock_guard<std::mutex> g(lock);
    packets.push_back({src, std::vector<uint8_t>(data, data + len)});
}

void iphone_emit(const IphoneEvent &ev)
{
    std::lock_guard<std::mutex> g(lock);
    events.push_back(ev);
}

void iphone_set_media(const IphoneMedia &m)
{
    std::lock_guard<std::mutex> g(lock);
    media = m;
    media_seq++;
}

static void set_state(IphoneState s)
{
    std::lock_guard<std::mutex> g(lock);
    state = s;
}

// ---------- Conexión ----------

class ServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer *, NimBLEConnInfo &info) override
    {
        Serial.printf("iPhone conectado (intervalo %.1f ms, margen %u ms)\n",
                      info.getConnInterval() * 1.25f, info.getConnTimeout() * 10);
        conn_handle = info.getConnHandle();
        encrypted = info.isEncrypted();
        // Pide el cifrado: la primera vez el iPhone muestra "Solicitud de enlace Bluetooth".
        NimBLEDevice::startSecurity(info.getConnHandle());
    }
    void onDisconnect(NimBLEServer *, NimBLEConnInfo &, int reason) override
    {
        Serial.printf("iPhone desconectado (%d)\n", reason);
        conn_handle = BLE_HS_CONN_HANDLE_NONE;
        encrypted = false;
        lost = true;
    }
    void onAuthenticationComplete(NimBLEConnInfo &info) override
    {
        encrypted = info.isEncrypted();
        Serial.printf("Emparejamiento: %s\n", encrypted ? "cifrado" : "fallido");
        if (!encrypted) {
            NimBLEDevice::getServer()->disconnect(info.getConnHandle());
            return;
        }
        NimBLEDevice::getServer()->updateConnParams(info.getConnHandle(), CONN_MIN_INTERVAL,
                                                    CONN_MAX_INTERVAL, 0, CONN_TIMEOUT);
    }
    // Para ver si el iPhone acepta el margen de 4 s pedido (si no, los cortes 520 siguen).
    void onConnParamsUpdate(NimBLEConnInfo &info) override
    {
        Serial.printf("Conexión: intervalo %.1f ms, margen %u ms, latencia %u\n",
                      info.getConnInterval() * 1.25f, info.getConnTimeout() * 10,
                      info.getConnLatency());
    }
};

static void start_advertising()
{
    NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
    NimBLEAdvertisementData data;
    data.setFlags(BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP);
    // "Service solicitation" de ANCS: así el iPhone sabe que se le piden los avisos.
    uint8_t sol[18] = {17, 0x15};
    memcpy(sol + 2, UUID_ANCS.getValue(), 16);   // NimBLE guarda los bytes ya al revés
    data.addData(sol, sizeof(sol));
    data.setName("VitaCar");                     // cabe justo: 3 + 18 + 9 bytes de 31
    adv->setAdvertisementData(data);
    adv->enableScanResponse(false);
    bool ok = adv->start();
    Serial.printf("Anuncio Bluetooth: %s\n", ok && adv->isAdvertising() ? "en marcha" : "FALLO");
}

// La batería es opcional: si el iPhone no la ofrece, no pasa nada.
static void setup_battery(NimBLEClient *client)
{
    NimBLERemoteService *svc = client->getService(UUID_BATTERY_SVC);
    NimBLERemoteCharacteristic *lvl = svc ? svc->getCharacteristic(UUID_BATTERY_LVL) : nullptr;
    if (!lvl)
        return;
    battery = lvl->readValue<uint8_t>();
    lvl->subscribe(true, [](NimBLERemoteCharacteristic *, uint8_t *d, size_t l, bool) {
        if (l > 0)
            battery = d[0];
    });
}

static void reset_session()
{
    ancs_reset();
    ams_reset();
    battery = -1;
    iphone_set_media(IphoneMedia());
    std::lock_guard<std::mutex> g(lock);
    packets.clear();
    commands.clear();
}

static void task(void *)
{
    bool ready = false;
    uint32_t next_setup = 0;
    for (;;) {
        if (lost) {
            lost = false;
            ready = false;
            reset_session();
        }
        if (conn_handle == BLE_HS_CONN_HANDLE_NONE) {
            set_state(IPHONE_OFF);
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        if (!ready) {
            set_state(IPHONE_CONNECTING);
            if (encrypted && millis() >= next_setup) {
                NimBLEClient *client = NimBLEDevice::getServer()->getClient(conn_handle);
                ready = client && ancs_setup(client);
                if (ready) {
                    if (!ams_setup(client))
                        Serial.println("El iPhone no ofrece AMS: sin música");
                    setup_battery(client);
                    set_state(IPHONE_READY);
                } else {
                    next_setup = millis() + SETUP_RETRY_MS;
                }
            }
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        while (true) {
            RawPacket p;
            {
                std::lock_guard<std::mutex> g(lock);
                if (packets.empty())
                    break;
                p = std::move(packets.front());
                packets.pop_front();
            }
            if (p.src == PKT_AMS_ENTITY)
                ams_handle(p.bytes);
            else
                ancs_handle(p.src, p.bytes);
        }
        while (true) {
            Command c;
            {
                std::lock_guard<std::mutex> g(lock);
                if (commands.empty())
                    break;
                c = commands.front();
                commands.pop_front();
            }
            if (c.media)
                ams_command(c.code);
            else
                ancs_perform(c.uid, c.code);
        }
        ancs_tick();
        ams_tick();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void iphone_begin()
{
    NimBLEDevice::init("VitaCar");
    NimBLEDevice::setMTU(517);
    NimBLEDevice::setSecurityAuth(true, false, true);          // enlazar y recordar, sin PIN
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
    NimBLEServer *server = NimBLEDevice::createServer();
    server->setCallbacks(new ServerCallbacks());
    server->advertiseOnDisconnect(true);
    start_advertising();
    Serial.printf("Bluetooth listo, %d iPhone(s) enlazado(s)\n", NimBLEDevice::getNumBonds());
    xTaskCreatePinnedToCore(task, "iphone", 8192, nullptr, 1, nullptr, 1);
}
