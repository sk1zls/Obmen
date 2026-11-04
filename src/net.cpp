#include "net.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#ifdef _WIN32
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "iphlpapi.lib")
#include <iphlpapi.h>
#else
#include <ifaddrs.h>
#include <net/if.h>
#endif

namespace net {

static bool g_inited = false;

bool init(std::string& err) {
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        err = "WSAStartup: не удалось инициализировать Winsock";
        return false;
    }
#endif
    g_inited = true;
    return true;
}

void shutdown() {
#ifdef _WIN32
    if (g_inited) WSACleanup();
#endif
    g_inited = false;
}

void set_blocking(socket_t s, bool blocking) {
#ifdef _WIN32
    u_long mode = blocking ? 0u : 1u;
    ioctlsocket(s, FIONBIO, &mode);
#else
    int fl = fcntl(s, F_GETFL, 0);
    if (fl < 0) return;
    if (blocking) fcntl(s, F_SETFL, fl & ~O_NONBLOCK);
    else fcntl(s, F_SETFL, fl | O_NONBLOCK);
#endif
}

bool set_timeouts(socket_t s, int recvMs, int sendMs) {
#ifdef _WIN32
    DWORD rt = (DWORD)(recvMs > 0 ? recvMs : 0);
    DWORD st = (DWORD)(sendMs > 0 ? sendMs : 0);
    bool ok = setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&rt, sizeof rt) == 0;
    ok = setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char*)&st, sizeof st) == 0 && ok;
    return ok;
#else
    int ok = 0;
    if (recvMs > 0) {
        timeval tv{};
        tv.tv_sec = recvMs / 1000;
        tv.tv_usec = (recvMs % 1000) * 1000;
        ok += setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv) == 0;
    }
    if (sendMs > 0) {
        timeval tv{};
        tv.tv_sec = sendMs / 1000;
        tv.tv_usec = (sendMs % 1000) * 1000;
        ok += setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv) == 0;
    }
    return ok > 0;
#endif
}

socket_t listen_tcp(uint16_t port, std::string& err, bool reuse) {
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    char ps[16];
    std::snprintf(ps, sizeof ps, "%u", (unsigned)port);
    addrinfo* res = nullptr;
    if (getaddrinfo(nullptr, ps, &hints, &res) != 0) {
        err = "getaddrinfo не удался";
        return kInvalidSocket;
    }
    socket_t ls = kInvalidSocket;
    int lastErr = 0;
    for (addrinfo* ai = res; ai; ai = ai->ai_next) {
        ls = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (ls == kInvalidSocket) continue;
        int one = 1;
#ifdef _WIN32
        (void)reuse;
        if (setsockopt(ls, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char*)&one, sizeof one) != 0) {
            lastErr = WSAGetLastError();
            close_socket(ls);
            ls = kInvalidSocket;
            continue;
        }
#else
        if (reuse) setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
#endif
        if (bind(ls, ai->ai_addr, (int)ai->ai_addrlen) != 0) {
#ifdef _WIN32
            lastErr = WSAGetLastError();
#else
            lastErr = errno;
#endif
            close_socket(ls);
            ls = kInvalidSocket;
            continue;
        }
        if (listen(ls, SOMAXCONN) != 0) {
            close_socket(ls);
            ls = kInvalidSocket;
            continue;
        }
        break;
    }
    freeaddrinfo(res);
    if (ls == kInvalidSocket) {
        err = "порт " + std::to_string((unsigned)port) + " занят (код " + std::to_string(lastErr) + ")";
    }
    return ls;
}

socket_t listen_udp(uint16_t port, std::string& err, bool reuse) {
    socket_t s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == kInvalidSocket) {
        err = "не удалось создать UDP-сокет";
        return kInvalidSocket;
    }
    int one = 1;
#ifdef _WIN32
    (void)reuse;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char*)&one, sizeof one);
#else
    if (reuse) setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
#endif
    setsockopt(s, SOL_SOCKET, SO_BROADCAST, (const char*)&one, sizeof one);
    sockaddr_in sa{};
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(INADDR_ANY);
    sa.sin_port = htons(port);
    if (bind(s, (sockaddr*)&sa, sizeof sa) != 0) {
        err = "UDP-порт " + std::to_string((unsigned)port) + " занят";
        close_socket(s);
        return kInvalidSocket;
    }
    return s;
}

uint16_t local_port(socket_t s) {
    sockaddr_storage ss{};
    int len = (int)sizeof ss;
    if (getsockname(s, (sockaddr*)&ss, &len) != 0) return 0;
    if (ss.ss_family == AF_INET) return ntohs(((sockaddr_in*)&ss)->sin_port);
    if (ss.ss_family == AF_INET6) return ntohs(((sockaddr_in6*)&ss)->sin6_port);
    return 0;
}

socket_t accept_tcp(socket_t ls) { return ::accept(ls, nullptr, nullptr); }

bool connect_tcp(const std::string& host, uint16_t port, socket_t& out, std::string& err, int timeoutMs) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    char ps[16];
    std::snprintf(ps, sizeof ps, "%u", (unsigned)port);
    addrinfo* res = nullptr;
    if (getaddrinfo(host.c_str(), ps, &hints, &res) != 0) {
        err = "не удалось разрешить адрес " + host;
        return false;
    }
    for (addrinfo* ai = res; ai; ai = ai->ai_next) {
        socket_t s = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (s == kInvalidSocket) continue;
        set_blocking(s, false);
        int r = ::connect(s, ai->ai_addr, (int)ai->ai_addrlen);
        bool ok = (r == 0);
        if (!ok) {
#ifdef _WIN32
            int e = WSAGetLastError();
            bool inprogress = (e == WSAEWOULDBLOCK);
#else
            bool inprogress = (errno == EINPROGRESS);
#endif
            if (inprogress) {
                fd_set wf;
                FD_ZERO(&wf);
                FD_SET(s, &wf);
                timeval tv{};
                tv.tv_sec = timeoutMs / 1000;
                tv.tv_usec = (timeoutMs % 1000) * 1000;
                int sr = select((int)(s + 1), nullptr, &wf, nullptr, &tv);
                if (sr > 0) {
                    int soerr = 0;
#ifdef _WIN32
                    int slen = (int)sizeof soerr;
#else
                    socklen_t slen = sizeof soerr;
#endif
                    getsockopt(s, SOL_SOCKET, SO_ERROR, (char*)&soerr, &slen);
                    ok = (soerr == 0);
                }
            }
        }
        if (ok) {
            set_blocking(s, true);
            out = s;
            freeaddrinfo(res);
            return true;
        }
        close_socket(s);
    }
    freeaddrinfo(res);
    err = "нет соединения с " + host + ":" + std::to_string((unsigned)port);
    return false;
}

bool send_all(socket_t s, const void* data, size_t len) {
    const char* p = (const char*)data;
    size_t left = len;
    while (left > 0) {
#ifdef _WIN32
        int n = ::send(s, p, (int)std::min<size_t>(left, 1u << 20), 0);
#else
        ssize_t n = ::send(s, p, std::min<size_t>(left, 1u << 20), MSG_NOSIGNAL);
#endif
        if (n <= 0) return false;
        p += n;
        left -= (size_t)n;
    }
    return true;
}

int recv_some(socket_t s, void* buf, size_t len) {
#ifdef _WIN32
    int n = ::recv(s, (char*)buf, (int)len, 0);
#else
    ssize_t n = ::recv(s, buf, len, 0);
#endif
    if (n <= 0) return (int)n;
    return (int)n;
}

void close_socket(socket_t s) {
    if (s == kInvalidSocket) return;
#ifdef _WIN32
    closesocket(s);
#else
    ::close(s);
#endif
}

std::string peer_ip(socket_t s) {
    sockaddr_storage ss{};
    int len = (int)sizeof ss;
    if (getpeername(s, (sockaddr*)&ss, &len) != 0) return "?";
    char buf[64] = {0};
    if (ss.ss_family == AF_INET) {
        inet_ntop(AF_INET, &((sockaddr_in*)&ss)->sin_addr, buf, sizeof buf);
    } else if (ss.ss_family == AF_INET6) {
        inet_ntop(AF_INET6, &((sockaddr_in6*)&ss)->sin6_addr, buf, sizeof buf);
    }
    return buf;
}

std::string socket_local_ip(socket_t s) {
    sockaddr_storage ss{};
    int len = (int)sizeof ss;
    if (getsockname(s, (sockaddr*)&ss, &len) != 0) return "127.0.0.1";
    char buf[64] = {0};
    if (ss.ss_family == AF_INET) {
        inet_ntop(AF_INET, &((sockaddr_in*)&ss)->sin_addr, buf, sizeof buf);
    } else if (ss.ss_family == AF_INET6) {
        inet_ntop(AF_INET6, &((sockaddr_in6*)&ss)->sin6_addr, buf, sizeof buf);
    }
    return buf;
}

std::vector<std::string> local_ip_list() {
    std::vector<std::string> out;
    char host[256] = {0};
    if (gethostname(host, sizeof host) != 0) return out;
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    if (getaddrinfo(host, nullptr, &hints, &res) != 0) return out;
    for (addrinfo* ai = res; ai; ai = ai->ai_next) {
        char buf[64] = {0};
        if (ai->ai_family != AF_INET) continue;
        inet_ntop(AF_INET, &((sockaddr_in*)ai->ai_addr)->sin_addr, buf, sizeof buf);
        std::string ip(buf);
        if (ip.empty() || ip == "127.0.0.1") continue;
        if (ip.rfind("169.254.", 0) == 0) continue;
        if (std::find(out.begin(), out.end(), ip) == out.end()) out.push_back(ip);
    }
    freeaddrinfo(res);
    if (out.empty()) out.push_back("127.0.0.1");
    return out;
}

std::vector<Ipv4If> ipv4_interfaces() {
    std::vector<Ipv4If> out;
    auto add = [&out](uint32_t ip, uint32_t bcast) {
        if (ip == 0) return;
        if ((ip >> 24) == 127) return;          // 127.0.0.0/8
        if ((ip >> 16) == 0xA9FE) return;       // 169.254.0.0/16 (link-local)
        char ipbuf[64] = {0};
        char bcbuf[64] = {0};
        in_addr a{};
        in_addr b{};
        a.s_addr = htonl(ip);
        b.s_addr = htonl(bcast);
        if (!inet_ntop(AF_INET, &a, ipbuf, sizeof ipbuf)) return;
        if (!inet_ntop(AF_INET, &b, bcbuf, sizeof bcbuf)) return;
        for (const auto& f : out) {
            if (f.ip == ipbuf) return;
        }
        Ipv4If rec;
        rec.ip = ipbuf;
        rec.broadcast = bcbuf;
        out.push_back(rec);
    };
#ifdef _WIN32
    ULONG size = 16 * 1024;
    std::vector<char> buf(size);
    auto* addrs = (IP_ADAPTER_ADDRESSES*)buf.data();
    const ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
    ULONG rc = GetAdaptersAddresses(AF_INET, flags, nullptr, addrs, &size);
    if (rc == ERROR_BUFFER_OVERFLOW) {
        buf.resize(size);
        addrs = (IP_ADAPTER_ADDRESSES*)buf.data();
        rc = GetAdaptersAddresses(AF_INET, flags, nullptr, addrs, &size);
    }
    if (rc != NO_ERROR) return out;
    for (auto* a = addrs; a; a = a->Next) {
        if (a->OperStatus != IfOperStatusUp) continue;
        if (a->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
        for (auto* u = a->FirstUnicastAddress; u; u = u->Next) {
            const sockaddr* sa = u->Address.lpSockaddr;
            if (!sa || sa->sa_family != AF_INET) continue;
            uint32_t ip = ntohl(((const sockaddr_in*)sa)->sin_addr.s_addr);
            unsigned prefix = (unsigned)u->OnLinkPrefixLength;
            if (prefix == 0 || prefix > 32) prefix = 24;
            uint32_t mask = (prefix == 32) ? 0xFFFFFFFFu : (0xFFFFFFFFu << (32 - prefix));
            add(ip, (ip & mask) | ~mask);
        }
    }
#else
    ifaddrs* ifs = nullptr;
    if (getifaddrs(&ifs) != 0) return out;
    for (ifaddrs* i = ifs; i; i = i->ifa_next) {
        if (!i->ifa_addr || i->ifa_addr->sa_family != AF_INET) continue;
        if (!(i->ifa_flags & IFF_UP)) continue;
        if (!(i->ifa_flags & IFF_BROADCAST) || !i->ifa_broadaddr) continue;
        add(ntohl(((sockaddr_in*)i->ifa_addr)->sin_addr.s_addr),
            ntohl(((sockaddr_in*)i->ifa_broadaddr)->sin_addr.s_addr));
    }
    freeifaddrs(ifs);
#endif
    return out;
}

}  // namespace net
