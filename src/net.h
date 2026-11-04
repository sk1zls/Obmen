#pragma once
// Тонкий переносимый слой над сокетами (Winsock2 / POSIX).
#include <cstdint>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
using socket_t = SOCKET;
static const socket_t kInvalidSocket = INVALID_SOCKET;
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
using socket_t = int;
static const socket_t kInvalidSocket = -1;
#endif

namespace net {

bool init(std::string& err);
void shutdown();

// Слушающие сокеты. port == 0 -> эфемерный порт.
socket_t listen_tcp(uint16_t port, std::string& err, bool reuse = false);
socket_t listen_udp(uint16_t port, std::string& err, bool reuse = true);
uint16_t local_port(socket_t s);

socket_t accept_tcp(socket_t ls);
bool connect_tcp(const std::string& host, uint16_t port, socket_t& out, std::string& err, int timeoutMs = 5000);

bool send_all(socket_t s, const void* data, size_t len);
// >0 — принято байт, 0 — соединение закрыто, -1 — ошибка/таймаут.
int recv_some(socket_t s, void* buf, size_t len);

void close_socket(socket_t s);
bool set_timeouts(socket_t s, int recvMs, int sendMs);
void set_blocking(socket_t s, bool blocking);

std::string peer_ip(socket_t s);
std::string socket_local_ip(socket_t s);
std::vector<std::string> local_ip_list();

// Локальный IPv4-интерфейс: свой адрес и направленный broadcast его подсети
// (192.168.1.10 / 192.168.1.255). Нужен, чтобы рассылать пакеты всем адаптерам:
// 255.255.255.255 уходит только через интерфейс маршрута по умолчанию.
struct Ipv4If {
    std::string ip;
    std::string broadcast;
};
std::vector<Ipv4If> ipv4_interfaces();

}  // namespace net
