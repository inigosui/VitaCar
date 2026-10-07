#include "net.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RECV_TIMEOUT_US  1000000
#define UDP_TIMEOUT_US   250000

#ifdef __vita__

#include <psp2/net/net.h>
#include <psp2/net/netctl.h>
#include <psp2/sysmodule.h>

#define NET_POOL_SIZE  (1 * 1024 * 1024)

static void *g_pool;

bool net_init(void)
{
    if (sceSysmoduleLoadModule(SCE_SYSMODULE_NET) < 0)
        return false;
    if (sceNetShowNetstat() == (int)SCE_NET_ERROR_ENOTINIT) {
        g_pool = malloc(NET_POOL_SIZE);
        SceNetInitParam param = { g_pool, NET_POOL_SIZE, 0 };
        if (sceNetInit(&param) < 0)
            return false;
    }
    return sceNetCtlInit() >= 0;
}

void net_shutdown(void)
{
    sceNetCtlTerm();
    sceNetTerm();
    free(g_pool);
    g_pool = NULL;
    sceSysmoduleUnloadModule(SCE_SYSMODULE_NET);
}

void net_wifi_info(NetWifiInfo *info)
{
    memset(info, 0, sizeof(*info));
    int state = 0;
    if (sceNetCtlInetGetState(&state) < 0 || state != SCE_NETCTL_STATE_CONNECTED)
        return;
    info->connected = true;

    SceNetCtlInfo ci;
    if (sceNetCtlInetGetInfo(SCE_NETCTL_INFO_GET_DEFAULT_ROUTE, &ci) >= 0)
        snprintf(info->gateway, sizeof(info->gateway), "%s", ci.default_route);
    if (sceNetCtlInetGetInfo(SCE_NETCTL_INFO_GET_IP_ADDRESS, &ci) >= 0)
        snprintf(info->ip, sizeof(info->ip), "%s", ci.ip_address);
    if (sceNetCtlInetGetInfo(SCE_NETCTL_INFO_GET_SSID, &ci) >= 0)
        snprintf(info->ssid, sizeof(info->ssid), "%s", ci.ssid);

    SceNetInAddr ip, mask;
    if (sceNetInetPton(SCE_NET_AF_INET, info->ip, &ip) > 0) {
        if (sceNetCtlInetGetInfo(SCE_NETCTL_INFO_GET_NETMASK, &ci) < 0 ||
            sceNetInetPton(SCE_NET_AF_INET, ci.netmask, &mask) <= 0)
            mask.s_addr = sceNetHtonl(0xFFFFFF00);  /* la máscara habitual de una WiFi de casa */
        ip.s_addr |= ~mask.s_addr;
        sceNetInetNtop(SCE_NET_AF_INET, &ip, info->broadcast, sizeof(info->broadcast));
    }
}

int net_connect(const char *ip, int port, int timeout_ms)
{
    SceNetSockaddrIn addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_len = sizeof(addr);
    addr.sin_family = SCE_NET_AF_INET;
    addr.sin_port = sceNetHtons(port);
    if (sceNetInetPton(SCE_NET_AF_INET, ip, &addr.sin_addr) <= 0)
        return NET_INVALID;

    int s = sceNetSocket("vitacar_phone", SCE_NET_AF_INET, SCE_NET_SOCK_STREAM, 0);
    if (s < 0)
        return NET_INVALID;

    /* Conexión no bloqueante + epoll para poder abandonar a los timeout_ms. */
    int on = 1, off = 0;
    sceNetSetsockopt(s, SCE_NET_SOL_SOCKET, SCE_NET_SO_NBIO, &on, sizeof(on));
    int r = sceNetConnect(s, (SceNetSockaddr *)&addr, sizeof(addr));
    if (r < 0 && r != (int)SCE_NET_ERROR_EINPROGRESS) {
        sceNetSocketClose(s);
        return NET_INVALID;
    }
    if (r < 0) {
        int ep = sceNetEpollCreate("vitacar_connect", 0);
        SceNetEpollEvent ev;
        memset(&ev, 0, sizeof(ev));
        ev.events = SCE_NET_EPOLLOUT | SCE_NET_EPOLLERR | SCE_NET_EPOLLHUP;
        ev.data.fd = s;
        sceNetEpollControl(ep, SCE_NET_EPOLL_CTL_ADD, s, &ev);
        int n = sceNetEpollWait(ep, &ev, 1, timeout_ms * 1000);
        sceNetEpollDestroy(ep);

        int err = 0;
        unsigned int len = sizeof(err);
        sceNetGetsockopt(s, SCE_NET_SOL_SOCKET, SCE_NET_SO_ERROR, &err, &len);
        if (n <= 0 || err != 0 || !(ev.events & SCE_NET_EPOLLOUT)) {
            sceNetSocketClose(s);
            return NET_INVALID;
        }
    }
    sceNetSetsockopt(s, SCE_NET_SOL_SOCKET, SCE_NET_SO_NBIO, &off, sizeof(off));

    int tmo = RECV_TIMEOUT_US;
    sceNetSetsockopt(s, SCE_NET_SOL_SOCKET, SCE_NET_SO_RCVTIMEO, &tmo, sizeof(tmo));
    sceNetSetsockopt(s, SCE_NET_SOL_SOCKET, SCE_NET_SO_SNDTIMEO, &tmo, sizeof(tmo));
    sceNetSetsockopt(s, SCE_NET_IPPROTO_TCP, SCE_NET_TCP_NODELAY, &on, sizeof(on));
    return s;
}

int net_recv(int sock, void *buf, int len)
{
    int r = sceNetRecv(sock, buf, len, 0);
    if (r == (int)SCE_NET_ERROR_EAGAIN)
        return NET_TIMEOUT;
    return r;
}

bool net_send_all(int sock, const void *buf, int len)
{
    const char *p = buf;
    while (len > 0) {
        int r = sceNetSend(sock, p, len, 0);
        if (r <= 0)
            return false;
        p += r;
        len -= r;
    }
    return true;
}

int net_udp_listen(int port)
{
    int s = sceNetSocket("vitacar_discover", SCE_NET_AF_INET, SCE_NET_SOCK_DGRAM, 0);
    if (s < 0)
        return s;

    int on = 1, tmo = UDP_TIMEOUT_US;
    sceNetSetsockopt(s, SCE_NET_SOL_SOCKET, SCE_NET_SO_REUSEADDR, &on, sizeof(on));
    sceNetSetsockopt(s, SCE_NET_SOL_SOCKET, SCE_NET_SO_BROADCAST, &on, sizeof(on));
    sceNetSetsockopt(s, SCE_NET_SOL_SOCKET, SCE_NET_SO_RCVTIMEO, &tmo, sizeof(tmo));

    SceNetSockaddrIn addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_len = sizeof(addr);
    addr.sin_family = SCE_NET_AF_INET;
    addr.sin_port = sceNetHtons(port);
    addr.sin_addr.s_addr = sceNetHtonl(SCE_NET_INADDR_ANY);
    int r = sceNetBind(s, (SceNetSockaddr *)&addr, sizeof(addr));
    if (r < 0) {
        sceNetSocketClose(s);
        return r;
    }
    return s;
}

int net_recvfrom(int sock, void *buf, int len, char *ip, size_t ip_len)
{
    SceNetSockaddrIn from;
    unsigned int from_len = sizeof(from);
    int r = sceNetRecvfrom(sock, buf, len, 0, (SceNetSockaddr *)&from, &from_len);
    if (r == (int)SCE_NET_ERROR_EAGAIN || r == (int)SCE_NET_ERROR_ETIMEDOUT)
        return NET_TIMEOUT;
    if (r >= 0 && !sceNetInetNtop(SCE_NET_AF_INET, &from.sin_addr, ip, ip_len))
        ip[0] = '\0';
    return r;
}

int net_sendto(int sock, const void *buf, int len, const char *ip, int port)
{
    SceNetSockaddrIn to;
    memset(&to, 0, sizeof(to));
    to.sin_len = sizeof(to);
    to.sin_family = SCE_NET_AF_INET;
    to.sin_port = sceNetHtons(port);
    if (strcmp(ip, "255.255.255.255") == 0)
        to.sin_addr.s_addr = SCE_NET_INADDR_BROADCAST;
    else if (sceNetInetPton(SCE_NET_AF_INET, ip, &to.sin_addr) <= 0)
        return NET_BADADDR;
    return sceNetSendto(sock, buf, len, 0, (SceNetSockaddr *)&to, sizeof(to));
}

void net_abort(int sock)
{
    if (sock >= 0)
        sceNetSocketAbort(sock, 0);
}

void net_close(int sock)
{
    if (sock >= 0) {
        sceNetShutdown(sock, SCE_NET_SHUT_RDWR);
        sceNetSocketClose(sock);
    }
}

#else /* PC: el "móvil" es VITACAR_PHONE o 127.0.0.1 (útil con el simulador) */

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

bool net_init(void)
{
    return true;
}

void net_shutdown(void)
{
}

void net_wifi_info(NetWifiInfo *info)
{
    memset(info, 0, sizeof(*info));
    const char *host = getenv("VITACAR_PHONE");
    info->connected = true;
    snprintf(info->gateway, sizeof(info->gateway), "%s", host ? host : "127.0.0.1");
    snprintf(info->ip, sizeof(info->ip), "127.0.0.1");
    snprintf(info->ssid, sizeof(info->ssid), "PC");
}

int net_connect(const char *ip, int port, int timeout_ms)
{
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, ip, &addr.sin_addr) <= 0)
        return NET_INVALID;

    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0)
        return NET_INVALID;

    int flags = fcntl(s, F_GETFL, 0);
    fcntl(s, F_SETFL, flags | O_NONBLOCK);
    if (connect(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        if (errno != EINPROGRESS) {
            close(s);
            return NET_INVALID;
        }
        struct pollfd pfd = { s, POLLOUT, 0 };
        int err = 0;
        socklen_t len = sizeof(err);
        if (poll(&pfd, 1, timeout_ms) <= 0 ||
            getsockopt(s, SOL_SOCKET, SO_ERROR, &err, &len) < 0 || err != 0) {
            close(s);
            return NET_INVALID;
        }
    }
    fcntl(s, F_SETFL, flags);

    struct timeval tv = { RECV_TIMEOUT_US / 1000000, RECV_TIMEOUT_US % 1000000 };
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    int on = 1;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on));
    return s;
}

int net_recv(int sock, void *buf, int len)
{
    ssize_t r = recv(sock, buf, len, 0);
    if (r < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
        return NET_TIMEOUT;
    return (int)r;
}

bool net_send_all(int sock, const void *buf, int len)
{
    const char *p = buf;
    while (len > 0) {
        ssize_t r = send(sock, p, len, MSG_NOSIGNAL);
        if (r <= 0)
            return false;
        p += r;
        len -= r;
    }
    return true;
}

int net_udp_listen(int port)
{
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0)
        return -errno;

    int on = 1;
    struct timeval tv = { 0, UDP_TIMEOUT_US };
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    setsockopt(s, SOL_SOCKET, SO_BROADCAST, &on, sizeof(on));
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        int err = -errno;
        close(s);
        return err;
    }
    return s;
}

int net_recvfrom(int sock, void *buf, int len, char *ip, size_t ip_len)
{
    struct sockaddr_in from;
    socklen_t from_len = sizeof(from);
    ssize_t r = recvfrom(sock, buf, len, 0, (struct sockaddr *)&from, &from_len);
    if (r < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
        return NET_TIMEOUT;
    if (r >= 0 && !inet_ntop(AF_INET, &from.sin_addr, ip, ip_len))
        ip[0] = '\0';
    return (int)r;
}

int net_sendto(int sock, const void *buf, int len, const char *ip, int port)
{
    struct sockaddr_in to;
    memset(&to, 0, sizeof(to));
    to.sin_family = AF_INET;
    to.sin_port = htons(port);
    if (inet_pton(AF_INET, ip, &to.sin_addr) <= 0)
        return NET_BADADDR;
    ssize_t r = sendto(sock, buf, len, 0, (struct sockaddr *)&to, sizeof(to));
    return r < 0 ? -errno : (int)r;
}

void net_abort(int sock)
{
    if (sock >= 0)
        shutdown(sock, SHUT_RDWR);
}

void net_close(int sock)
{
    if (sock >= 0) {
        shutdown(sock, SHUT_RDWR);
        close(sock);
    }
}

#endif
