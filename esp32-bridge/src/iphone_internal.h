// Piezas internas del Bluetooth del iPhone, compartidas por iphone.cpp, ancs.cpp y ams.cpp.
// Todo lo de aquí se llama desde la tarea del iPhone.
#pragma once
#include "iphone.h"
#include <NimBLEDevice.h>
#include <vector>

enum PacketSource { PKT_ANCS_NOTIF, PKT_ANCS_DATA, PKT_AMS_ENTITY };

// Copia lo que llega por Bluetooth (en la tarea de NimBLE) para tratarlo en la tarea del iPhone.
void iphone_queue_packet(PacketSource src, const uint8_t *data, size_t len);
void iphone_emit(const IphoneEvent &ev);
void iphone_set_media(const IphoneMedia &m);

bool ancs_setup(NimBLEClient *client);
void ancs_reset();
void ancs_handle(PacketSource src, const std::vector<uint8_t> &bytes);
void ancs_tick();
void ancs_perform(uint32_t uid, uint8_t action);

bool ams_setup(NimBLEClient *client);
void ams_reset();
void ams_handle(const std::vector<uint8_t> &bytes);
void ams_tick();
void ams_command(uint8_t cmd);
