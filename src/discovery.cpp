#include "discovery.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <map>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "app.h"
#include "http.h"
#include "net.h"

namespace discovery {

static socket_t g_sock = kInvalidSocket;
static std::atomic<bool> g_run{false};
static std::thread g_thread;
static std::thread g_mirrorThread;
static Callback g_cb;
static uint16_t g_port = 0;

static const char* kMulticast = "239.255.42.99";

static std::string make_packet() {
    std::string name = g_app.cfg.name;
    for (auto& c : name) {
        if (c == '|' || c == '\r' || c == '\n') c = ' ';
    }
    return "OBMEN1|" + g_app.nodeId + "|" + name + "|" + std::to_string(g_app.cfg.httpPort) + "|" +
           std::to_string(g_app.cfg.ftpPort);
}

static void send_packet_to(const std::string& addr, const std::string& pkt) {
    if (g_sock == kInvalidSocket) return;
    sockaddr_in to{};
    to.sin_family = AF_INET;
    to.sin_port = htons(g_port);
    if (inet_pton(AF_INET, addr.c_str(), &to.sin_addr) != 1) return;
    sendto(g_sock, pkt.data(), (int)pkt.size(), 0, (sockaddr*)&to, sizeof to);
}

void announce() {
    if (g_sock == kInvalidSocket) return;
    std::string pkt = make_packet();

    // 255.255.255.255 уходит только через интерфейс маршрута по умолчанию, поэтому
    // на машине с несколькими адаптерами (Wi-Fi + Ethernet, VPN, Hyper-V) его мало:
    // шлём ещё направленный broadcast каждой подсети.
    send_packet_to("255.255.255.255", pkt);
    std::vector<net::Ipv4If> ifs = net::ipv4_interfaces();
    for (const auto& f : ifs) send_packet_to(f.broadcast, pkt);

    // Multicast — по каждому интерфейсу отдельно: с одним сокетом без IP_MULTICAST_IF
    // пакет уйдёт только в один адаптер.
    if (ifs.empty()) {
        send_packet_to(kMulticast, pkt);
        return;
    }
    for (const auto& f : ifs) {
        in_addr iface{};
        if (inet_pton(AF_INET, f.ip.c_str(), &iface) != 1) continue;
        if (setsockopt(g_sock, IPPROTO_IP, IP_MULTICAST_IF, (const char*)&iface, sizeof iface) != 0) {
            continue;
        }
        send_packet_to(kMulticast, pkt);
    }
}

static bool parse_packet(const std::string& msg, std::string& id, std::string& name, int& httpPort,
                         int& ftpPort) {
    if (msg.rfind("OBMEN1|", 0) != 0) return false;
    size_t p1 = msg.find('|', 7);
    if (p1 == std::string::npos) return false;
    size_t p2 = msg.find('|', p1 + 1);
    if (p2 == std::string::npos) return false;
    size_t p3 = msg.find('|', p2 + 1);
    if (p3 == std::string::npos) return false;
    id = msg.substr(7, p1 - 7);
    name = msg.substr(p1 + 1, p2 - p1 - 1);
    try {
        httpPort = std::stoi(msg.substr(p2 + 1, p3 - p2 - 1));
        ftpPort = std::stoi(msg.substr(p3 + 1));
    } catch (...) {
        return false;
    }
    return !id.empty();
}

static void reply_to(const sockaddr_in& from, const std::string& pkt) {
    if (g_sock == kInvalidSocket) return;
    sendto(g_sock, pkt.data(), (int)pkt.size(), 0, (const sockaddr*)&from, sizeof from);
}

static void loop() {
    auto nextBeacon = std::chrono::steady_clock::now();
    std::map<std::string, std::chrono::steady_clock::time_point> lastReply;
    char buf[2048];
    while (g_run.load()) {
        auto now = std::chrono::steady_clock::now();
        if (now >= nextBeacon) {
            announce();
            nextBeacon = now + std::chrono::seconds(4);
        }
        sockaddr_in from{};
#ifdef _WIN32
        int flen = (int)sizeof from;
#else
        socklen_t flen = sizeof from;
#endif
        int n = (int)recvfrom(g_sock, buf, (int)sizeof buf - 1, 0, (sockaddr*)&from, &flen);
        if (n <= 0) continue;
        std::string msg(buf, (size_t)n);
        std::string id, name;
        int hp = 0, fp = 0;
        if (!parse_packet(msg, id, name, hp, fp)) continue;
        if (id == g_app.nodeId) continue;

        // Доставка бывает односторонней: брандмауэр, изоляция точек доступа,
        // несколько адаптеров. Услышав соседа, отвечаем ему unicast — тогда он
        // узнаёт нас, даже если наши broadcast/multicast пакеты до него не доходят.
        // Ответ не чаще раза в 4 с на узел, чтобы ответы не пошли по кругу.
        auto it = lastReply.find(id);
        if (it == lastReply.end() || now - it->second >= std::chrono::seconds(4)) {
            lastReply[id] = now;
            reply_to(from, make_packet());
        }

        char ipbuf[64] = {0};
        inet_ntop(AF_INET, &from.sin_addr, ipbuf, sizeof ipbuf);
        if (g_cb) g_cb(id, name, std::string(ipbuf), hp, fp);
    }
}

// Представляемся соседям по TCP: POST /api/announce. Страховка на случай, когда
// UDP-пакеты доходят только в одну сторону — тогда один узел видит другого, а
// обратное обнаружение не работает никогда. Запрос дешёвый и идемпотентный:
// сосед проверяет нас ответным /api/status и заводит как обычный узел.
static void mirror_loop() {
    while (g_run.load()) {
        std::vector<std::pair<std::string, int>> targets;
        {
            std::lock_guard<std::mutex> lk(g_app.mtx);
            for (const auto& kv : g_app.peers) {
                const Peer& p = kv.second;
                if (p.ip.empty() || p.httpPort <= 0) continue;
                targets.emplace_back(p.ip, p.httpPort);
            }
        }
        if (!targets.empty()) {
            std::string body = "{\"port\":" + std::to_string(g_app.cfg.httpPort) +
                               ",\"ftp\":" + std::to_string(g_app.cfg.ftpPort) + "}";
            for (const auto& t : targets) {
                if (!g_run.load()) break;
                int status = 0;
                std::string resp, err;
                (void)http::post(t.first, (uint16_t)t.second, "/api/announce", body, 1500, status,
                                 resp, err);
            }
        }
        // Пауза 10 с (меньше 15 с, после которых узел считается offline).
        for (int i = 0; i < 100 && g_run.load(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

static void join_group(const std::string& ifaceIp) {
    ip_mreq mreq{};
    if (inet_pton(AF_INET, kMulticast, &mreq.imr_multiaddr) != 1) return;
    if (ifaceIp.empty()) {
        mreq.imr_interface.s_addr = htonl(INADDR_ANY);
    } else if (inet_pton(AF_INET, ifaceIp.c_str(), &mreq.imr_interface) != 1) {
        return;
    }
    (void)setsockopt(g_sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, (const char*)&mreq, sizeof mreq);
}

bool start(uint16_t port, Callback cb, std::string& err) {
    g_port = port;
    g_cb = std::move(cb);
    g_sock = net::listen_udp(port, err, true);
    if (g_sock == kInvalidSocket) return false;

    // Подписываемся на группу на каждом интерфейсе: с INADDR_ANY система выбирает
    // один адаптер, и multicast-пакеты, пришедшие на другой, отбрасываются.
    std::vector<net::Ipv4If> ifs = net::ipv4_interfaces();
    if (ifs.empty()) {
        join_group("");
    } else {
        for (const auto& f : ifs) join_group(f.ip);
    }

    unsigned char ttl = 2;
    (void)setsockopt(g_sock, IPPROTO_IP, IP_MULTICAST_TTL, (const char*)&ttl, sizeof ttl);
    unsigned char mcastLoop = 1;
    (void)setsockopt(g_sock, IPPROTO_IP, IP_MULTICAST_LOOP, (const char*)&mcastLoop, sizeof mcastLoop);

    net::set_timeouts(g_sock, 1000, 1000);
    g_run.store(true);
    g_thread = std::thread(loop);
    g_mirrorThread = std::thread(mirror_loop);
    return true;
}

void stop() {
    if (!g_run.exchange(false)) return;
    if (g_thread.joinable()) g_thread.join();
    if (g_mirrorThread.joinable()) g_mirrorThread.join();
    if (g_sock != kInvalidSocket) {
        net::close_socket(g_sock);
        g_sock = kInvalidSocket;
    }
}

}  // namespace discovery
