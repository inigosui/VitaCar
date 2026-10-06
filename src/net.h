#pragma once

#include <stdbool.h>
#include <stddef.h>

/* Capa fina sobre sockets: API nativa sceNet en la Vita, POSIX en el PC. */

#define NET_INVALID   (-1)
#define NET_TIMEOUT   (-2)

typedef struct {
    bool connected;
    char gateway[16];   /* con el punto de acceso del móvil, es el propio móvil */
    char ip[16];
    char ssid[33];
} NetWifiInfo;

bool net_init(void);
void net_shutdown(void);
void net_wifi_info(NetWifiInfo *info);

/* Devuelve el socket o NET_INVALID. */
int  net_connect(const char *ip, int port, int timeout_ms);
/* Bytes leídos, 0 si el otro extremo cerró, NET_TIMEOUT tras ~1 s sin datos, <0 si hay error. */
int  net_recv(int sock, void *buf, int len);
bool net_send_all(int sock, const void *buf, int len);
/* Desbloquea un net_recv en curso desde otro hilo, sin cerrar el socket. */
void net_abort(int sock);
void net_close(int sock);
