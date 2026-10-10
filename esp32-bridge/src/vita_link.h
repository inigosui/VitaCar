// Enlace con la Vita: la ESP32 se comporta como el móvil Android (docs/PROTOCOLO.md).
// WiFi propia, descubrimiento por UDP y servidor TCP con tramas [u32 long][u8 tipo][datos].
#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

// Se llama con cada mensaje JSON de la Vita (salvo ping, que se contesta aquí).
typedef void (*VitaMessageHandler)(JsonDocument &msg);
// Se llama al conectar una Vita, para mandarle el estado completo.
typedef void (*VitaConnectHandler)();

void vita_begin(VitaMessageHandler on_msg, VitaConnectHandler on_connect);
void vita_loop();
bool vita_connected();
String vita_ip();
void vita_send(JsonDocument &msg);
