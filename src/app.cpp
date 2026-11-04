#include "app.h"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>

#include "http.h"
#include "json.h"
#include "store.h"

App g_app;

static std::string g_configPath;

int64_t now_sec() {
    return (int64_t)std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

bool peer_is_online(const Peer& p) { return p.lastSeen > 0 && now_sec() - p.lastSeen <= 15; }

uint64_t tx_begin(TxDir dir, const std::string& file, const std::string& peer, uint64_t total) {
    std::lock_guard<std::mutex> lk(g_app.mtx);
    Transfer t;
    t.id = ++g_app.seq;
    t.dir = dir;
    t.file = file;
    t.peer = peer;
    t.total = total;
    t.status = "active";
    t.ts = now_sec();
    g_app.transfers.push_back(t);
    while (g_app.transfers.size() > 400) g_app.transfers.pop_front();
    return t.id;
}

void tx_update(uint64_t id, uint64_t bytes) {
    std::lock_guard<std::mutex> lk(g_app.mtx);
    for (auto it = g_app.transfers.rbegin(); it != g_app.transfers.rend(); ++it) {
        if (it->id == id) {
            it->bytes = bytes;
            return;
        }
    }
}

void tx_done(uint64_t id, const std::string& status) {
    std::lock_guard<std::mutex> lk(g_app.mtx);
    for (auto it = g_app.transfers.rbegin(); it != g_app.transfers.rend(); ++it) {
        if (it->id == id) {
            it->status = status;
            if (status == "done" && it->total > 0) it->bytes = it->total;
            return;
        }
    }
}

void tx_clear() {
    std::lock_guard<std::mutex> lk(g_app.mtx);
    g_app.transfers.clear();
}

void log_line(const std::string& msg) {
    static std::mutex ioMtx;
    std::lock_guard<std::mutex> lk(ioMtx);
    std::time_t t = std::time(nullptr);
    std::tm tmv{};
#ifdef _WIN32
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    char buf[16];
    std::strftime(buf, sizeof buf, "%H:%M:%S", &tmv);
    std::cout << "[" << buf << "] " << msg << std::endl;
}

void set_root_error(const std::string& e) {
    std::lock_guard<std::mutex> lk(g_app.errMtx);
    g_app.rootError = e;
}

std::string get_root_error() {
    std::lock_guard<std::mutex> lk(g_app.errMtx);
    return g_app.rootError;
}

// ------------------------------------------------------------ настройки

static std::string trim_cfg(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r')) --b;
    return s.substr(a, b - a);
}

void config_set_path(const std::string& path) { g_configPath = path; }

const std::string& config_path() { return g_configPath; }

void config_store_shares() {
    g_app.cfg.shares.clear();
    for (const auto& s : store::shares()) g_app.cfg.shares.push_back(Share{s.name, s.path, s.key});
}

void config_save() {
    if (g_configPath.empty()) return;
    // Путь настроек может быть с кириллицей («Документы\Обмен»), поэтому только
    // через from_utf8: fs::path(std::string) на Windows читает байты как ANSI.
    std::ofstream f(store::from_utf8(g_configPath), std::ios::trunc | std::ios::binary);
    if (!f) return;
    f << "# Конфигурация Обмен (общие папки в локальной сети)\n";
    for (const auto& s : g_app.cfg.shares) {
        f << "share=" << s.name << "|" << s.path;
        if (!s.key.empty() && s.key != s.name) f << "|" << s.key;
        f << "\n";
    }
    f << "name=" << g_app.cfg.name << "\n";
    f << "http_port=" << g_app.cfg.httpPort << "\n";
    f << "ftp_port=" << g_app.cfg.ftpPort << "\n";
    f << "udp_port=" << g_app.cfg.udpPort << "\n";
    f << "ftp_enabled=" << (g_app.cfg.ftpEnabled ? 1 : 0) << "\n";
    f << "sync_enabled=" << (g_app.syncEnabled.load() ? 1 : 0) << "\n";
    f << "sync_interval=" << g_app.syncIntervalSec.load() << "\n";
}

static std::string share_name_from_path(const std::string& path) {
    std::filesystem::path p = store::from_utf8(path);
    std::string name = store::to_utf8(p.filename());
    while (!name.empty() && (name.back() == '\\' || name.back() == '/')) name.pop_back();
    if (name.empty() || name == "." || name == "..") name = "Обмен";
    return name;
}

static void add_loaded_share(const std::string& name, const std::string& path,
                             const std::string& key = std::string()) {
    std::string n = trim_cfg(name);
    std::string p = trim_cfg(path);
    if (p.empty()) return;
    if (n.empty()) n = share_name_from_path(p);
    // имена должны быть уникальны
    std::string base = n;
    int k = 2;
    for (;;) {
        bool taken = false;
        for (const auto& s : g_app.cfg.shares) {
            if (s.name == n) {
                taken = true;
                break;
            }
        }
        if (!taken) break;
        n = base + " (" + std::to_string(k++) + ")";
    }
    std::string kk = trim_cfg(key);
    if (kk.empty()) kk = n;
    g_app.cfg.shares.push_back(Share{n, p, kk});
}

void config_load() {
    if (g_configPath.empty()) return;
    std::ifstream f(store::from_utf8(g_configPath), std::ios::binary);
    if (!f) return;
    g_app.cfg.shares.clear();
    std::string line;
    bool first = true;
    while (std::getline(f, line)) {
        if (first) {
            first = false;
            if (line.size() >= 3 && (unsigned char)line[0] == 0xEF && (unsigned char)line[1] == 0xBB &&
                (unsigned char)line[2] == 0xBF) {
                line = line.substr(3);
            }
        }
        line = trim_cfg(line);
        if (line.empty() || line[0] == '#') continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = trim_cfg(line.substr(0, eq));
        std::string v = trim_cfg(line.substr(eq + 1));
        if (k == "share") {
            size_t bar = v.find('|');
            if (bar == std::string::npos) {
                add_loaded_share("", v);
            } else {
                std::string rest = v.substr(bar + 1);
                size_t bar2 = rest.find('|');
                if (bar2 == std::string::npos) {
                    add_loaded_share(v.substr(0, bar), rest);
                } else {
                    add_loaded_share(v.substr(0, bar), rest.substr(0, bar2), rest.substr(bar2 + 1));
                }
            }
        } else if (k == "root") {
            // старый формат: одна папка
            if (!v.empty()) add_loaded_share("", v);
        } else if (k == "name") {
            g_app.cfg.name = v;
        } else if (k == "http_port") {
            g_app.cfg.httpPort = std::atoi(v.c_str());
        } else if (k == "ftp_port") {
            g_app.cfg.ftpPort = std::atoi(v.c_str());
        } else if (k == "udp_port") {
            g_app.cfg.udpPort = std::atoi(v.c_str());
        } else if (k == "ftp_enabled") {
            g_app.cfg.ftpEnabled = (v == "1" || v == "true");
        } else if (k == "sync_enabled") {
            g_app.cfg.syncEnabled = (v == "1" || v == "true");
        } else if (k == "sync_interval") {
            g_app.cfg.syncIntervalSec = std::atoi(v.c_str());
        }
    }
}

// ------------------------------------------------------------- пиры

bool add_manual_peer(const std::string& addrIn, std::string& err) {
    std::string addr = trim_cfg(addrIn);
    if (addr.empty()) {
        err = "укажите адрес вида 192.168.0.10:8080";
        return false;
    }
    std::string host = addr;
    int port = 8080;
    size_t colon = addr.rfind(':');
    if (colon != std::string::npos) {
        host = addr.substr(0, colon);
        try {
            port = std::stoi(addr.substr(colon + 1));
        } catch (...) {
            port = 0;
        }
    }
    if (host.empty() || port <= 0 || port > 65535) {
        err = "формат: 192.168.0.10:8080";
        return false;
    }

    int status = 0;
    std::string body;
    if (!http::get(host, (uint16_t)port, "/api/status", 3000, status, body, err) || status != 200) {
        if (err.empty()) err = "нет ответа";
        err = "узел недоступен: " + err;
        return false;
    }
    Json st;
    if (!Json::parse(body, st, &err)) {
        err = "неверный ответ узла";
        return false;
    }
    std::string id = st.get("node").get("id").asString();
    std::string name = st.get("node").get("name").asString();
    if (id.empty() || id == g_app.nodeId) {
        err = "это тот же самый узел";
        return false;
    }
    Peer p;
    p.id = id;
    p.name = name.empty() ? host : name;
    p.ip = host;
    p.httpPort = port;
    p.ftpPort = (int)st.get("ports").get("ftp").asInt(0);
    p.lastSeen = now_sec();
    p.manual = true;
    Json peerShares = st.get("shares");
    for (const Json& s : peerShares.items()) {
        std::string n = s.get("name").asString();
        if (!n.empty()) p.shares.push_back(n);
    }
    {
        std::lock_guard<std::mutex> lk(g_app.mtx);
        auto it = g_app.peers.find(id);
        if (it != g_app.peers.end()) p.manual = p.manual || it->second.manual;
        g_app.peers[id] = p;
    }
    log_line("узел добавлен вручную: " + p.name + " (" + host + ":" + std::to_string(port) + ")");
    return true;
}
