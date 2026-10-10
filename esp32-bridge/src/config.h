// Ajustes del puente.
#pragma once

// WiFi propia de la ESP32 (opción A del plan). La Vita se conecta a ella.
#define AP_SSID      "VitaCar"
#define AP_PASSWORD  "vitacar2026"   // mínimo 8 caracteres

#define VITA_PORT    47474           // mismo puerto que el móvil Android (docs/PROTOCOLO.md)
#define BRIDGE_NAME  "iPhone (ESP32)"
#define MAX_NOTIFS   20              // avisos del iPhone que se guardan para la Vita

// Pantalla (sin CS, SPI modo 3).
#define PIN_TFT_DC   16
#define PIN_TFT_RST  4
#define PIN_TFT_BLK  17

#define PIN_BOOT_BTN 0               // botón BOOT de la placa
