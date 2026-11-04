#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "app.h"
#include "discovery.h"
#include "ftp.h"
#include "gui_assets.h"
#include "http.h"
#include "json.h"
#include "net.h"
#include "store.h"
#include "sync.h"
#include "ui.h"

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <knownfolders.h>
#include <io.h>
#else
#include <unistd.h>
#endif

namespace fs = std::filesystem;

static std::atomic<bool> g_stop{false};

// ------------------------------------------------------------------ helpers

static bool read_body(http::Request& req, std::string& out, std::string& err) {
    if (req.contentLength > 64ull * 1024 * 1024) {
        err = "слишком большое тело запроса";
        return false;
    }
    out.clear();
    if (req.contentLength == 0) return true;
    out.resize((size_t)req.contentLength);
    if (!req.conn) {
        err = "нет соединения";
        return false;
    }
    if (!req.conn->read_bytes(&out[0], (size_t)req.contentLength)) {
        err = "тело запроса не прочитано";
        return false;
    }
    return true;
}

static std::string ok_json() { return "{\"ok\":true}"; }

// Имя сетевой папки из запроса; без параметра — первая папка (совместимость).
// Если задан key (ключ синхронизации), находим свою папку по нему — так узел
// отвечает соседу, у которого эта папка называется иначе.
static std::string req_share(http::Request& req) {
    std::string key = req.str("key");
    if (!key.empty()) {
        store::ShareInfo si;
        if (store::get_share_by_key(key, si)) return si.name;
        return std::string();
    }
    std::string s = req.str("share");
    if (s.empty()) s = store::default_share();
    return s;
}

static std::string name_from_path(const std::string& path) {
    fs::path p = store::from_utf8(path);
    std::string n = store::to_utf8(p.filename());
    if (n.empty() || n == "." || n == "..") n = "Обмен";
    return n;
}

static bool valid_single_name(const std::string& name) {
    if (name.empty()) return false;
    if (name == "." || name == "..") return false;
    if (name.find('/') != std::string::npos) return false;
    if (name.find('\\') != std::string::npos) return false;
    if (name.find(':') != std::string::npos) return false;
    for (unsigned char c : name) {
        if (c < 0x20) return false;
    }
    return true;
}

static std::string parent_rel(const std::string& rel) {
    std::string s = rel;
    while (!s.empty() && s.back() == '/') s.pop_back();
    size_t p = s.find_last_of('/');
    return p == std::string::npos ? std::string() : s.substr(0, p);
}

// Настройки читаются и пишутся в app.cpp (config_load / config_save),
// чтобы их одинаково использовали сервер и встроенное окно.

static std::string exe_dir() {
#ifdef _WIN32
    wchar_t buf[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (n == 0) return ".";
    fs::path p(buf);
    return store::to_utf8(p.parent_path());
#else
    return ".";
#endif
}

// Каталог настроек: «Документы\Обмен». Рядом с exe их держать нельзя — программу
// часто запускают с рабочего стола, и там появлялись obmen.ini и папка обмена.
static std::string config_dir() {
    std::string dir;
#ifdef _WIN32
    PWSTR docs = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, KF_FLAG_CREATE, nullptr, &docs)) && docs) {
        dir = store::to_utf8(fs::path(docs) / L"Обмен");
    }
    if (docs) CoTaskMemFree(docs);
#else
    const char* home = std::getenv("HOME");
    if (home && *home) dir = std::string(home) + "/Documents/Обмен";
#endif
    if (dir.empty()) dir = exe_dir() + "/Обмен";  // запасной вариант
    std::error_code ec;
    fs::create_directories(store::from_utf8(dir), ec);
    return dir;
}

static std::string default_node_name() {
    const char* env = std::getenv("COMPUTERNAME");
    if (env && *env) return env;
#ifdef _WIN32
    wchar_t buf[256];
    DWORD n = sizeof buf / sizeof buf[0];
    if (GetComputerNameW(buf, &n)) return store::to_utf8(fs::path(buf));
#endif
    return "Обмен";
}

static std::string make_id() {
    std::random_device rd;
    char buf[40];
    std::snprintf(buf, sizeof buf, "%08x%08x", (unsigned)rd(), (unsigned)rd());
    return buf;
}

// --------------------------------------------------------------- API: status

static void api_status(http::Request&, http::Response& res) {
    Json root = Json::object();

    Json node = Json::object();
    node.set("id", g_app.nodeId);
    node.set("name", g_app.cfg.name);
    node.set("started", (long long)g_app.startedAt);
    node.set("uptime", (long long)(now_sec() - g_app.startedAt));
    root.set("node", node);

    Json ports = Json::object();
    ports.set("http", g_app.cfg.httpPort);
    ports.set("ftp", g_app.cfg.ftpEnabled ? g_app.cfg.ftpPort : 0);
    ports.set("udp", g_app.cfg.udpPort);
    root.set("ports", ports);

    Json ips = Json::array();
    for (const auto& ip : net::local_ip_list()) ips.push(ip);
    root.set("ips", ips);

    Json sharesArr = Json::array();
    std::string firstPath;
    for (const auto& s : store::shares()) {
        if (firstPath.empty()) firstPath = s.path;
        Json j = Json::object();
        j.set("name", s.name);
        j.set("path", s.path);
        j.set("key", s.key.empty() ? s.name : s.key);
        sharesArr.push(j);
    }
    root.set("shares", sharesArr);
    root.set("root", firstPath);
    root.set("rootError", get_root_error());

    Json syncj = Json::object();
    syncj.set("enabled", g_app.syncEnabled.load());
    syncj.set("interval", g_app.syncIntervalSec.load());
    syncj.set("running", g_app.syncRunning.load());
    syncj.set("pulled", (unsigned long long)g_app.syncPulled.load());
    // Прогресс текущего раунда: интерфейс рисует по нему полосу «сколько из сколько»
    // (bytesTotal/bytesDone — план и факт, lastAt/lastFiles — итог прошлого раунда).
    const SyncProgress& pr = g_app.syncProgress;
    syncj.set("peersTotal", pr.peersTotal.load());
    syncj.set("peersDone", pr.peersDone.load());
    syncj.set("filesTotal", pr.filesTotal.load());
    syncj.set("filesDone", pr.filesDone.load());
    syncj.set("bytesTotal", (unsigned long long)pr.bytesTotal.load());
    syncj.set("bytesDone", (unsigned long long)pr.bytesDone.load());
    syncj.set("failed", pr.failed.load());
    syncj.set("lastAt", (long long)pr.lastAt.load());
    syncj.set("lastFiles", pr.lastFiles.load());
    syncj.set("lastBytes", (unsigned long long)pr.lastBytes.load());
    root.set("sync", syncj);

    Json peers = Json::array();
    {
        std::lock_guard<std::mutex> lk(g_app.mtx);
        for (const auto& kv : g_app.peers) {
            const Peer& p = kv.second;
            Json j = Json::object();
            j.set("id", p.id);
            j.set("name", p.name);
            j.set("ip", p.ip);
            j.set("http", p.httpPort);
            j.set("ftp", p.ftpPort);
            j.set("lastSeen", (long long)p.lastSeen);
            j.set("online", peer_is_online(p));
            j.set("manual", p.manual);
            Json ps = Json::array();
            for (const auto& n : p.shares) ps.push(n);
            j.set("shares", ps);
            peers.push(j);
        }
    }
    root.set("peers", peers);

    Json stats = Json::object();
    stats.set("bytesIn", (unsigned long long)g_app.bytesIn.load());
    stats.set("bytesOut", (unsigned long long)g_app.bytesOut.load());
    root.set("stats", stats);

    Json transfers = Json::array();
    {
        std::lock_guard<std::mutex> lk(g_app.mtx);
        size_t start = g_app.transfers.size() > 60 ? g_app.transfers.size() - 60 : 0;
        for (size_t i = start; i < g_app.transfers.size(); ++i) {
            const Transfer& t = g_app.transfers[i];
            Json j = Json::object();
            const char* d = t.dir == TxDir::Upload ? "up"
                            : t.dir == TxDir::Download ? "down"
                            : t.dir == TxDir::Ftp      ? "ftp"
                                                       : "sync";
            j.set("dir", d);
            j.set("file", t.file);
            j.set("peer", t.peer);
            j.set("bytes", (unsigned long long)t.bytes);
            j.set("total", (unsigned long long)t.total);
            j.set("status", t.status);
            j.set("ts", (long long)t.ts);
            transfers.push(j);
        }
    }
    root.set("transfers", transfers);

    res.json(root.dump());
}

// ---------------------------------------------------------------- API: files

static void api_files(http::Request& req, http::Response& res) {
    std::string shareName = req_share(req);
    std::string rel = req.str("path");
    std::vector<store::Entry> entries;
    std::string err;
    if (!store::list(shareName, rel, entries, err)) {
        res.fail(400, err);
        return;
    }
    Json arr = Json::array();
    for (const auto& e : entries) {
        Json j = Json::object();
        j.set("name", e.name);
        j.set("rel", e.rel);
        j.set("isDir", e.isDir);
        j.set("size", (unsigned long long)e.size);
        j.set("mtime", (long long)e.mtime);
        arr.push(j);
    }
    Json out = Json::object();
    out.set("share", shareName);
    out.set("path", rel);
    out.set("parent", parent_rel(rel));
    out.set("entries", arr);
    res.json(out.dump());
}

static void api_download(http::Request& req, http::Response& res) {
    std::string rel = req.str("path");
    if (rel.empty()) {
        res.fail(400, "путь не задан");
        return;
    }
    std::string shareName = req_share(req);
    fs::path p;
    std::string err;
    if (!store::resolve(shareName, rel, p, err)) {
        res.fail(400, err);
        return;
    }
    std::error_code ec;
    if (!fs::is_regular_file(p, ec)) {
        res.fail(404, "файл не найден");
        return;
    }
    uint64_t size = (uint64_t)fs::file_size(p, ec);
    if (ec) {
        res.fail(500, "не удалось определить размер файла");
        return;
    }
    std::string name = store::to_utf8(p.filename());
    res.contentType = http::mime_type(name);
    res.stream = true;
    res.streamLength = size;
    res.add("Content-Disposition", "attachment; filename*=UTF-8''" + http::url_encode(name));
    uint64_t txId = tx_begin(TxDir::Download, rel, req.remoteIp, size);
    res.streamer = [p, size, txId](http::Conn& c) -> bool {
        std::ifstream f(p, std::ios::binary);
        if (!f) return false;
        std::vector<char> buf(256 * 1024);
        uint64_t sent = 0;
        uint64_t lastReport = 0;
        while (sent < size) {
            size_t want = (size_t)std::min<uint64_t>(buf.size(), size - sent);
            f.read(buf.data(), (std::streamsize)want);
            std::streamsize got = f.gcount();
            if (got <= 0) break;
            if (!c.send_data(buf.data(), (size_t)got)) {
                tx_done(txId, "error: соединение прервано");
                return false;
            }
            sent += (uint64_t)got;
            if (sent - lastReport > 1024 * 1024) {
                lastReport = sent;
                tx_update(txId, sent);
            }
        }
        if (sent != size) {
            tx_done(txId, "error: файл изменился");
            return false;
        }
        tx_done(txId, "done");
        g_app.bytesOut.fetch_add(sent);
        return true;
    };
}

static void api_upload(http::Request& req, http::Response& res) {
    std::string rel = req.str("path");
    std::string name = req.str("name");
    if (!valid_single_name(name)) {
        res.fail(400, "недопустимое имя файла");
        return;
    }
    std::string shareName = req_share(req);
    fs::path dir;
    std::string err;
    if (!store::resolve(shareName, rel, dir, err)) {
        res.fail(400, err);
        return;
    }
    std::error_code ec;
    // Синхронизация выкладывает файлы во вложенные каталоги, которых у соседа
    // может ещё не быть: mkdirs=1 разрешает создать путь.
    if (req.flag("mkdirs")) {
        fs::create_directories(dir, ec);
        ec.clear();
    }
    if (!fs::is_directory(dir, ec)) {
        res.fail(400, "папка не найдена");
        return;
    }
    fs::path target = dir / store::from_utf8(name);
    fs::path tmp = store::temp_path(target);

    std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
    if (!f) {
        res.fail(500, "не удалось создать файл в папке обмена");
        return;
    }
    uint64_t total = req.contentLength;
    uint64_t txId = tx_begin(TxDir::Upload, rel.empty() ? name : rel + "/" + name, req.remoteIp, total);
    std::vector<char> buf(256 * 1024);
    uint64_t got = 0;
    bool ok = true;
    while (got < total) {
        size_t want = (size_t)std::min<uint64_t>(buf.size(), total - got);
        if (!req.conn || !req.conn->read_bytes(buf.data(), want)) {
            ok = false;
            break;
        }
        f.write(buf.data(), (std::streamsize)want);
        if (!f) {
            ok = false;
            break;
        }
        got += want;
        tx_update(txId, got);
    }
    f.close();
    if (!ok) {
        std::error_code ecr;
        fs::remove(tmp, ecr);
        tx_done(txId, "error: приём прерван");
        res.fail(400, "приём файла прерван");
        return;
    }
    std::string rerr;
    if (!store::replace_file(tmp, target, rerr)) {
        tx_done(txId, "error: " + rerr);
        res.fail(500, rerr);
        return;
    }
    // mtime присылает синхронизация: у соседа файл должен сохранить время
    // источника, иначе узлы будут считать версии разными и качать по кругу.
    int64_t mt = req.num("mtime", 0);
    if (mt > 0) {
        std::error_code ecm;
        fs::last_write_time(target, store::from_epoch(mt), ecm);
    }
    tx_done(txId, "done");
    g_app.bytesIn.fetch_add(got);
    log_line("принят файл «" + name + "» (" + std::to_string(got) + " б) от " + req.remoteIp);
    sync::wake();
    res.json(ok_json());
}

static void api_delete(http::Request& req, http::Response& res) {
    std::string rel = req.str("path");
    if (rel.empty()) {
        res.fail(400, "нельзя удалить корневую папку");
        return;
    }
    std::string shareName = req_share(req);
    std::string err;
    if (!store::remove_entry(shareName, rel, err)) {
        res.fail(400, err);
        return;
    }
    log_line("удалено: " + rel);
    sync::wake();
    res.json(ok_json());
}

static void api_mkdir(http::Request& req, http::Response& res) {
    std::string rel = req.str("path");
    std::string name = req.str("name");
    if (!valid_single_name(name)) {
        res.fail(400, "недопустимое имя папки");
        return;
    }
    std::string shareName = req_share(req);
    fs::path dir;
    std::string err;
    if (!store::resolve(shareName, rel, dir, err)) {
        res.fail(400, err);
        return;
    }
    std::error_code ec;
    fs::create_directories(dir / store::from_utf8(name), ec);
    if (ec) {
        res.fail(500, "не удалось создать папку: " + ec.message());
        return;
    }
    res.json(ok_json());
}

static void api_setroot(http::Request& req, http::Response& res) {
    std::string body, err;
    if (!read_body(req, body, err)) {
        res.fail(400, err);
        return;
    }
    Json j;
    if (!Json::parse(body, j, &err)) {
        res.fail(400, err);
        return;
    }
    std::string path = j.get("path").asString();
    std::string shareName = j.get("share").asString();
    if (shareName.empty()) shareName = store::default_share();
    std::string serr;
    if (shareName.empty()) {
        // Папок ещё нет — создаём первую.
        std::string nm = j.get("name").asString();
        if (nm.empty()) nm = name_from_path(path);
        if (!store::add_share(nm, path, "", serr)) {
            set_root_error(serr);
            res.fail(400, serr);
            return;
        }
        shareName = nm;
    } else if (!store::set_share_path(shareName, path, serr)) {
        set_root_error(serr);
        res.fail(400, serr);
        return;
    }
    set_root_error("");
    config_store_shares();
    config_save();
    log_line("папка «" + shareName + "»: " + path);
    sync::wake();
    res.json(ok_json());
}

// Быстрый переход («Документы», «Рабочий стол», …) для окна выбора папки.
// Считается один раз: пути стандартных папок за время работы программы не меняются.
static const std::vector<std::pair<std::string, std::string>>& known_places() {
    static std::once_flag once;
    static std::vector<std::pair<std::string, std::string>> cache;
    std::call_once(once, [] {
#ifdef _WIN32
        struct Known {
            const KNOWNFOLDERID* id;
            const char* label;
        };
        static const Known known[] = {
            {&FOLDERID_Desktop, "Рабочий стол"},
            {&FOLDERID_Documents, "Документы"},
            {&FOLDERID_Downloads, "Загрузки"},
            {&FOLDERID_Pictures, "Изображения"},
            {&FOLDERID_Music, "Музыка"},
            {&FOLDERID_Videos, "Видео"},
            {&FOLDERID_Profile, "Домашняя папка"},
        };
        bool com = SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED));
        for (const auto& k : known) {
            PWSTR raw = nullptr;
            if (FAILED(SHGetKnownFolderPath(*k.id, KF_FLAG_DONT_VERIFY, nullptr, &raw)) || !raw) {
                continue;
            }
            fs::path dir(raw);
            CoTaskMemFree(raw);
            std::error_code ec;
            if (!fs::is_directory(dir, ec)) continue;
            cache.emplace_back(k.label, store::to_utf8(dir));
        }
        if (com) CoUninitialize();
#else
        const char* home = std::getenv("HOME");
        if (home) {
            static const std::pair<const char*, const char*> dirs[] = {
                {"Рабочий стол", "Desktop"}, {"Документы", "Documents"},
                {"Загрузки", "Downloads"},   {"Изображения", "Pictures"},
                {"Музыка", "Music"},         {"Видео", "Videos"},
            };
            std::error_code ec;
            fs::path homePath(home);
            if (fs::is_directory(homePath, ec)) cache.emplace_back("Домашняя папка", home);
            for (const auto& d : dirs) {
                fs::path dir = homePath / d.second;
                if (fs::is_directory(dir, ec)) cache.emplace_back(d.first, store::to_utf8(dir));
            }
        }
#endif
    });
    return cache;
}

static void api_browse(http::Request& req, http::Response& res) {
    std::string path = req.str("path");
    // drives=1 — показать только список дисков (кнопка «Все диски»).
    bool drivesOnly = req.str("drives") == "1";
    std::error_code ec;
    fs::path p;
    if (path.empty() && !drivesOnly) {
        // Начинаем с домашней папки пользователя, а не с корня диска.
        for (const auto& place : known_places()) {
            if (place.first == "Домашняя папка") {
                p = store::from_utf8(place.second);
                break;
            }
        }
    }
    if (p.empty() && !path.empty()) {
        p = store::from_utf8(path);
    }
    if (p.empty() && !drivesOnly) {
#ifdef _WIN32
        p = fs::path("C:\\");
#else
        p = fs::path("/");
#endif
    }
    while (!p.empty() && !fs::is_directory(p, ec)) {
        fs::path up = p.parent_path();
        if (up == p || up.empty()) break;
        p = up;
    }
    Json drives = Json::array();
#ifdef _WIN32
    for (char c = 'A'; c <= 'Z'; ++c) {
        std::string d = std::string(1, c) + ":\\";
        if (fs::exists(fs::path(d), ec)) drives.push(d);
    }
#endif
    Json dirs = Json::array();
    if (!p.empty()) {
        fs::directory_iterator it(p, fs::directory_options::skip_permission_denied, ec);
        fs::directory_iterator end;
        for (; it != end; it.increment(ec)) {
            if (ec) break;
            std::error_code ec2;
            if (!it->is_directory(ec2)) continue;
            std::string nm = store::to_utf8(it->path().filename());
            if (nm.empty()) continue;
            Json d = Json::object();
            d.set("name", nm);
            d.set("path", store::to_utf8(it->path()));
            dirs.push(d);
        }
    }
    Json out = Json::object();
    out.set("path", store::to_utf8(p));
    fs::path parent = p.parent_path();
    if (!parent.empty() && parent != p) out.set("parent", store::to_utf8(parent));
    else out.set("parent", Json());
    Json places = Json::array();
    for (const auto& place : known_places()) {
        Json d = Json::object();
        d.set("label", place.first);
        d.set("path", place.second);
        places.push(d);
    }
    out.set("places", places);
    out.set("drives", drives);
    out.set("dirs", dirs);
    res.json(out.dump());
}

static void api_manifest(http::Request& req, http::Response& res) {
    std::string shareName = req_share(req);
    if (shareName.empty()) {
        res.fail(404, "папка с таким ключом не найдена");
        return;
    }
    if (!store::has_share(shareName)) {
        res.fail(404, "папка «" + shareName + "» не найдена");
        return;
    }
    std::vector<store::Entry> files;
    std::string err;
    if (!store::manifest(shareName, files, err)) {
        res.fail(400, err);
        return;
    }
    Json arr = Json::array();
    for (const auto& e : files) {
        Json j = Json::object();
        j.set("p", e.rel);
        j.set("s", (unsigned long long)e.size);
        j.set("m", (long long)e.mtime);
        arr.push(j);
    }
    res.json(arr.dump());
}

static void api_tombstones(http::Request& req, http::Response& res) {
    std::string shareName = req_share(req);
    if (!shareName.empty() && !store::has_share(shareName)) {
        res.fail(404, "папка «" + shareName + "» не найдена");
        return;
    }
    Json arr = Json::array();
    for (const auto& t : store::tombstones(shareName)) {
        Json j = Json::object();
        j.set("p", t.rel);
        j.set("t", (long long)t.ts);
        arr.push(j);
    }
    res.json(arr.dump());
}

// Приём удалений от соседа: нажатие «Синхронизировать» на его узле выкладывает
// и его надгробия, чтобы файлы исчезали у нас сразу, а не через цикл.
static void api_apply_tombstones(http::Request& req, http::Response& res) {
    std::string body, err;
    if (!read_body(req, body, err)) {
        res.fail(400, err);
        return;
    }
    Json arr;
    if (!Json::parse(body, arr, &err) || !arr.isArray()) {
        res.fail(400, "ожидался список надгробий");
        return;
    }
    std::string shareName = req_share(req);
    if (shareName.empty() || !store::has_share(shareName)) {
        res.fail(404, "папка с таким ключом не найдена");
        return;
    }
    std::vector<std::pair<std::string, int64_t>> items;
    for (const Json& it : arr.items()) {
        std::string rel = it.get("p").asString();
        int64_t ts = it.get("t").asInt();
        if (!rel.empty() && ts > 0) items.emplace_back(rel, ts);
    }
    int removed = sync::apply_remote_tombstones(shareName, items);
    res.json(ok_json());
    if (removed > 0) {
        log_line("синхронизация: удалено по отметкам соседа " + std::to_string(removed) + " файл(ов)");
    }
}

static void api_sync_toggle(http::Request& req, http::Response& res) {
    std::string body, err;
    if (read_body(req, body, err) && !body.empty()) {
        Json j;
        if (Json::parse(body, j, &err)) {
            if (j.has("enabled")) g_app.syncEnabled.store(j.get("enabled").asBool());
            if (j.has("interval")) {
                int iv = (int)j.get("interval").asInt(10);
                if (iv < 3) iv = 3;
                if (iv > 3600) iv = 3600;
                g_app.syncIntervalSec.store(iv);
                g_app.cfg.syncIntervalSec = iv;
            }
        }
    }
    g_app.cfg.syncEnabled = g_app.syncEnabled.load();
    config_save();
    sync::wake();
    res.json(ok_json());
}

// Кнопка «Синхронизировать»: узел выкладывает все свои папки соседям.
static void api_sync_now(http::Request&, http::Response& res) {
    sync::push_now();
    res.json(ok_json());
}

static void api_peers(http::Request& req, http::Response& res) {
    std::string body, err;
    if (!read_body(req, body, err)) {
        res.fail(400, err);
        return;
    }
    Json j;
    if (!Json::parse(body, j, &err)) {
        res.fail(400, err);
        return;
    }
    if (!add_manual_peer(j.get("addr").asString(), err)) {
        res.fail(400, err);
        return;
    }
    res.json(ok_json());
}

static void api_transfers(http::Request&, http::Response& res) {
    Json arr = Json::array();
    std::lock_guard<std::mutex> lk(g_app.mtx);
    for (const auto& t : g_app.transfers) {
        Json j = Json::object();
        const char* d = t.dir == TxDir::Upload ? "up"
                        : t.dir == TxDir::Download ? "down"
                        : t.dir == TxDir::Ftp      ? "ftp"
                                                   : "sync";
        j.set("dir", d);
        j.set("file", t.file);
        j.set("peer", t.peer);
        j.set("bytes", (unsigned long long)t.bytes);
        j.set("total", (unsigned long long)t.total);
        j.set("status", t.status);
        j.set("ts", (long long)t.ts);
        arr.push(j);
    }
    res.json(arr.dump());
}

static void api_clear_transfers(http::Request&, http::Response& res) {
    tx_clear();
    res.json(ok_json());
}

static void api_open_root(http::Request& req, http::Response& res) {
    std::string shareName = req_share(req);
    fs::path r;
    std::string serr;
    if (shareName.empty() || !store::resolve(shareName, "", r, serr)) {
        res.fail(400, "папка обмена не выбрана");
        return;
    }
#ifdef _WIN32
    INT_PTR rc = (INT_PTR)ShellExecuteW(nullptr, L"open", r.wstring().c_str(), nullptr, nullptr,
                                        SW_SHOWNORMAL);
    if (rc <= 32) {
        res.fail(500, "не удалось открыть проводник");
        return;
    }
    res.json(ok_json());
#else
    res.fail(501, "не поддерживается на этой платформе");
#endif
}

static Json shares_json() {
    Json arr = Json::array();
    for (const auto& s : store::shares()) {
        Json j = Json::object();
        j.set("name", s.name);
        j.set("path", s.path);
        j.set("key", s.key.empty() ? s.name : s.key);
        arr.push(j);
    }
    return arr;
}

static void api_shares(http::Request& req, http::Response& res) {
    if (req.method == "GET") {
        Json out = Json::object();
        out.set("shares", shares_json());
        res.json(out.dump());
        return;
    }
    std::string body, err;
    if (!read_body(req, body, err)) {
        res.fail(400, err);
        return;
    }
    Json j;
    if (!Json::parse(body, j, &err)) {
        res.fail(400, err);
        return;
    }
    std::string action = j.get("action").asString("add");
    if (action == "add") {
        std::string name = j.get("name").asString();
        std::string path = j.get("path").asString();
        std::string key = j.get("key").asString();
        if (name.empty()) name = name_from_path(path);
        if (!store::add_share(name, path, key, err)) {
            res.fail(400, err);
            return;
        }
        log_line("добавлена папка «" + name + "»: " + path);
    } else if (action == "setkey") {
        std::string name = j.get("name").asString();
        std::string key = j.get("key").asString();
        if (name.empty()) {
            res.fail(400, "не задано имя папки");
            return;
        }
        if (!store::set_share_key(name, key, err)) {
            res.fail(400, err);
            return;
        }
        log_line("папка «" + name + "» синхронизируется как «" + (key.empty() ? name : key) + "»");
    } else if (action == "remove") {
        std::string name = j.get("name").asString();
        if (name.empty()) {
            res.fail(400, "не задано имя папки");
            return;
        }
        if (!store::remove_share(name, err)) {
            res.fail(400, err);
            return;
        }
        log_line("папка убрана: " + name);
    } else {
        res.fail(400, "неизвестное действие: " + action);
        return;
    }
    config_store_shares();
    config_save();
    sync::wake();
    Json out = Json::object();
    out.set("shares", shares_json());
    res.json(out.dump());
}

static void api_favicon(http::Request&, http::Response& res) {
    res.status = 204;
    res.body.clear();
}

// ------------------------------------------------------------------ main

static void on_peer_found(const std::string& id, const std::string& name, const std::string& ip,
                          int httpPort, int ftpPort) {
    bool isNew = false;
    {
        std::lock_guard<std::mutex> lk(g_app.mtx);
        auto it = g_app.peers.find(id);
        if (it == g_app.peers.end()) {
            Peer p;
            p.id = id;
            p.name = name;
            p.ip = ip;
            p.httpPort = httpPort;
            p.ftpPort = ftpPort;
            p.lastSeen = now_sec();
            g_app.peers[id] = p;
            isNew = true;
        } else {
            Peer& p = it->second;
            p.name = name;
            p.ip = ip;
            p.httpPort = httpPort;
            p.ftpPort = ftpPort;
            p.lastSeen = now_sec();
        }
    }
    if (isNew) {
        log_line("найден узел: " + name + " (" + ip + ":" + std::to_string(httpPort) + ")");
        sync::wake();
        // Подтягиваем имена папок пира — они видны в интерфейсе.
        std::string host = ip;
        int port = httpPort;
        std::string peerId = id;
        std::thread([host, port, peerId]() {
            int status = 0;
            std::string body, err;
            if (!http::get(host, (uint16_t)port, "/api/status", 3000, status, body, err) ||
                status != 200) {
                return;
            }
            Json st;
            if (!Json::parse(body, st, &err)) return;
            std::vector<std::string> names;
            Json peerShares = st.get("shares");
            for (const Json& s : peerShares.items()) {
                // Новый узел отдаёт объекты {name, key}, старый — просто имена.
                std::string n = s.isObject() ? s.get("key").asString() : s.asString();
                if (n.empty()) n = s.get("name").asString();
                if (!n.empty()) names.push_back(n);
            }
            std::lock_guard<std::mutex> lk(g_app.mtx);
            auto it = g_app.peers.find(peerId);
            if (it != g_app.peers.end()) it->second.shares = names;
        }).detach();
    }
}

// Узел представляется по TCP: {"port":<http>,"ftp":<ftp>}. Нужно там, где UDP
// доходит только в одну сторону (брандмауэр, изоляция точек доступа): тогда
// обнаружение по broadcast у одной из сторон не срабатывает никогда. Адрес узла
// берём из самого TCP-соединения — он гарантированно тот, по которому нас видно.
static void api_announce(http::Request& req, http::Response& res) {
    std::string body, err;
    if (!read_body(req, body, err)) {
        res.fail(400, err);
        return;
    }
    Json j;
    if (!Json::parse(body, j, &err)) {
        res.fail(400, err);
        return;
    }
    std::string ip = req.remoteIp;
    int port = (int)j.get("port").asInt(0);
    int ftpPort = (int)j.get("ftp").asInt(0);
    if (ip.empty() || port <= 0 || port > 65535) {
        res.fail(400, "неверное представление узла");
        return;
    }
    res.json(ok_json());
    // Проверяем представившегося ответным /api/status и заводим как обычный узел.
    std::thread([ip, port, ftpPort]() {
        int status = 0;
        std::string statusBody, statusErr;
        if (!http::get(ip, (uint16_t)port, "/api/status", 3000, status, statusBody, statusErr) ||
            status != 200) {
            return;
        }
        Json st;
        if (!Json::parse(statusBody, st, &statusErr)) return;
        std::string id = st.get("node").get("id").asString();
        std::string name = st.get("node").get("name").asString();
        if (id.empty() || id == g_app.nodeId) return;
        if (name.empty()) name = ip;
        on_peer_found(id, name, ip, port, ftpPort);
    }).detach();
}

#ifdef _WIN32
static BOOL WINAPI ctrl_handler(DWORD) {
    g_stop.store(true);
    return TRUE;
}
#endif

static void print_banner() {
    std::cout << "\n=== Обмен — общие папки в локальной сети ===\n";
    std::cout << "Узел:   " << g_app.cfg.name << "  (id " << g_app.nodeId << ")\n";
    auto allShares = store::shares();
    if (allShares.empty()) {
        std::cout << "Папки:  нет\n";
    } else {
        std::cout << "Папки:  " << allShares.size() << "\n";
        for (const auto& s : allShares) {
            std::cout << "        «" << s.name << "»  " << s.path << "\n";
        }
    }
    std::string err = get_root_error();
    if (!err.empty()) std::cout << "Ошибка папки: " << err << "\n";
    auto ips = net::local_ip_list();
    std::cout << "GUI:    http://127.0.0.1:" << g_app.cfg.httpPort << "/";
    for (const auto& ip : ips) std::cout << "\n        http://" << ip << ":" << g_app.cfg.httpPort << "/";
    std::cout << "\n";
    if (g_app.cfg.ftpEnabled) {
        std::cout << "FTP:    ftp://127.0.0.1:" << g_app.cfg.ftpPort << "/  (логин anonymous, пароль любой)\n";
    } else {
        std::cout << "FTP:    выключен\n";
    }
    std::cout << "UDP:    :" << g_app.cfg.udpPort << " (обнаружение устройств)\n";
    std::cout << "Синхр.: " << (g_app.syncEnabled.load() ? "включена" : "выключена") << ", интервал "
              << g_app.syncIntervalSec.load() << " с\n";
    std::cout << "=============================================\n" << std::flush;
}

int main(int argc, char** argv) {
    // Встроенное окно включено по умолчанию; --no-ui оставляет режим сервера.
    bool useUi = true;
    bool startInTray = false;
    bool openBrowser = false;
    bool keepConsole = false;

#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    std::vector<std::string> args;
    {
        int wn = 0;
        LPWSTR* wargv = CommandLineToArgvW(GetCommandLineW(), &wn);
        if (wargv) {
            for (int i = 0; i < wn; ++i) args.push_back(store::to_utf8(fs::path(wargv[i])));
            LocalFree(wargv);
        } else {
            for (int i = 0; i < argc; ++i) args.push_back(argv[i]);
        }
    }
#else
    std::vector<std::string> args;
    for (int i = 0; i < argc; ++i) args.push_back(argv[i]);
#endif

    std::string netErr;
    if (!net::init(netErr)) {
        std::cerr << netErr << std::endl;
        return 1;
    }

    g_app.cfg.name = default_node_name();

    std::string dir = exe_dir();
    std::string cfgPath = config_dir() + "/obmen.ini";
    // Переезд со старого места: раньше obmen.ini лежал рядом с exe (и попадал
    // на рабочий стол, если программу запускали оттуда).
    {
        std::error_code ece;
        if (!fs::exists(store::from_utf8(cfgPath), ece)) {
            std::string legacy = dir + "/obmen.ini";
            if (fs::exists(store::from_utf8(legacy), ece)) {
                std::error_code ecc;
                fs::copy_file(store::from_utf8(legacy), store::from_utf8(cfgPath),
                              fs::copy_options::overwrite_existing, ecc);
                if (!ecc) {
                    std::cout << "Настройки перенесены: " << legacy << " -> " << cfgPath << "\n"
                              << std::flush;
                    // Старый файл больше не читается — убираем его, чтобы он не путал
                    // (данные уже в «Документах»). Если не удалось — не страшно.
                    std::error_code ecd;
                    fs::remove(store::from_utf8(legacy), ecd);
                }
            }
        }
    }
    config_set_path(cfgPath);

    // Первый проход: только путь к настройкам, чтобы прочитать файл до остальных параметров.
    for (size_t i = 1; i + 1 < args.size(); ++i) {
        if (args[i] == "--config") config_set_path(args[i + 1]);
    }
    config_load();

    // Второй проход: аргументы командной строки (приоритет над файлом).
    for (size_t i = 1; i < args.size(); ++i) {
        const std::string& a = args[i];
        auto next = [&](std::string def) -> std::string {
            if (i + 1 < args.size()) return args[++i];
            return def;
        };
        if (a == "--root") {
            std::string p = next(std::string());
            if (!p.empty()) {
                if (g_app.cfg.shares.empty()) {
                    g_app.cfg.shares.push_back(Share{name_from_path(p), p});
                } else {
                    g_app.cfg.shares.front().path = p;
                }
            }
        } else if (a == "--share") {
            std::string v = next(std::string());
            if (!v.empty()) {
                size_t eq = v.find('=');
                if (eq == std::string::npos) {
                    g_app.cfg.shares.push_back(Share{name_from_path(v), v});
                } else {
                    g_app.cfg.shares.push_back(Share{v.substr(0, eq), v.substr(eq + 1)});
                }
            }
        }
        else if (a == "--name") g_app.cfg.name = next(g_app.cfg.name);
        else if (a == "--port" || a == "--http-port") g_app.cfg.httpPort = std::atoi(next("8080").c_str());
        else if (a == "--ftp-port") g_app.cfg.ftpPort = std::atoi(next("2121").c_str());
        else if (a == "--udp-port") g_app.cfg.udpPort = std::atoi(next("45454").c_str());
        else if (a == "--interval") g_app.cfg.syncIntervalSec = std::atoi(next("10").c_str());
        else if (a == "--no-sync") g_app.cfg.syncEnabled = false;
        else if (a == "--no-ftp") g_app.cfg.ftpEnabled = false;
        else if (a == "--config") { if (i + 1 < args.size()) ++i; }
        else if (a == "--no-ui" || a == "--headless") useUi = false;
        else if (a == "--tray") startInTray = true;
        else if (a == "--browser") openBrowser = true;
        else if (a == "--console") keepConsole = true;
        else if (a == "--help" || a == "-h") {
            std::cout << "Обмен — общая папка в локальной сети\n\n"
                         "  --root <папка>     первая сетевая папка\n"
                         "  --share <имя=путь> ещё одна сетевая папка (можно несколько раз)\n"
                         "  --name <имя>       имя устройства в сети\n"
                         "  --port <n>         HTTP-порт GUI (по умолчанию 8080)\n"
                         "  --ftp-port <n>     FTP-порт (по умолчанию 2121)\n"
                         "  --udp-port <n>     UDP-порт обнаружения (по умолчанию 45454)\n"
                         "  --interval <сек>   интервал автосинхронизации (по умолчанию 10)\n"
                         "  --no-sync          не запускать автосинхронизацию\n"
                         "  --no-ftp           не запускать FTP-сервер\n"
                         "  --no-ui            не открывать окно (режим сервера)\n"
                         "  --tray             запустить свёрнутым в трей\n"
                         "  --browser          дополнительно открыть веб-интерфейс в браузере\n"
                         "  --console          не скрывать консоль при запуске окна\n"
                         "  --config <файл>    путь к obmen.ini\n"
                         "  --help             эта справка\n";
            net::shutdown();
            return 0;
        }
    }

    if (g_app.cfg.httpPort <= 0 || g_app.cfg.httpPort > 65535) g_app.cfg.httpPort = 8080;
    if (g_app.cfg.ftpPort <= 0 || g_app.cfg.ftpPort > 65535) g_app.cfg.ftpPort = 2121;
    if (g_app.cfg.udpPort <= 0 || g_app.cfg.udpPort > 65535) g_app.cfg.udpPort = 45454;
    if (g_app.cfg.syncIntervalSec < 3) g_app.cfg.syncIntervalSec = 3;

    // Папок может не быть вовсе: это нормальное стартовое состояние — пользователь
    // добавляет папки сам, программа ничего на диске не создаёт.
    if (g_app.cfg.shares.empty()) {
        set_root_error("");
    } else {
        std::vector<store::ShareInfo> list;
        list.reserve(g_app.cfg.shares.size());
        for (const auto& s : g_app.cfg.shares) list.push_back(store::ShareInfo{s.name, s.path, s.key});
        std::string serr;
        if (store::set_shares(list, serr)) {
            set_root_error("");
        } else {
            // Добавляем по одной: недоступные пропускаем, остальные работают.
            std::cerr << "Папки: " << serr << std::endl;
            for (const auto& s : list) {
                std::string e;
                if (!store::add_share(s.name, s.path, s.key, e)) {
                    std::cerr << "  пропущена «" << s.name << "»: " << e << std::endl;
                }
            }
            set_root_error(store::shares().empty() ? serr : std::string());
        }
    }
    config_store_shares();

    g_app.nodeId = make_id();
    g_app.startedAt = now_sec();
    g_app.syncEnabled.store(g_app.cfg.syncEnabled);
    g_app.syncIntervalSec.store(g_app.cfg.syncIntervalSec);

    // Маршруты GUI.
    http::route("GET", "/", [](http::Request&, http::Response& res) { res.html(kIndexHtml); });
    http::route("GET", "/app.css", [](http::Request&, http::Response& res) {
        res.contentType = "text/css; charset=utf-8";
        res.body = kAppCss;
    });
    http::route("GET", "/app.js", [](http::Request&, http::Response& res) {
        res.contentType = "application/javascript; charset=utf-8";
        res.body = kAppJs;
    });
    http::route("GET", "/favicon.ico", api_favicon);

    // API.
    http::route("GET", "/api/status", api_status);
    http::route("GET", "/api/files", api_files);
    http::route("GET", "/api/download", api_download);
    http::route("POST", "/api/upload", api_upload);
    http::route("POST", "/api/delete", api_delete);
    http::route("POST", "/api/mkdir", api_mkdir);
    http::route("POST", "/api/setroot", api_setroot);
    http::route("GET", "/api/browse", api_browse);
    http::route("GET", "/api/manifest", api_manifest);
    http::route("GET", "/api/tombstones", api_tombstones);
    http::route("GET", "/api/transfers", api_transfers);
    http::route("POST", "/api/transfers/clear", api_clear_transfers);
    http::route("POST", "/api/sync", api_sync_toggle);
    http::route("POST", "/api/tombstones", api_apply_tombstones);
    http::route("POST", "/api/sync/now", api_sync_now);
    http::route("POST", "/api/peers", api_peers);
    http::route("POST", "/api/announce", api_announce);
    http::route("POST", "/api/openroot", api_open_root);
    http::route("GET", "/api/shares", api_shares);
    http::route("POST", "/api/shares", api_shares);

    std::string httpErr;
    if (!http::listen((uint16_t)g_app.cfg.httpPort, httpErr)) {
        std::cerr << "HTTP: " << httpErr << std::endl;
        net::shutdown();
        return 2;
    }

    std::string discErr;
    if (!discovery::start((uint16_t)g_app.cfg.udpPort, on_peer_found, discErr)) {
        log_line("обнаружение узлов недоступно: " + discErr);
    }

    if (g_app.cfg.ftpEnabled) {
        std::string ftpErr;
        if (!ftp::start((uint16_t)g_app.cfg.ftpPort, ftpErr)) {
            std::cerr << "FTP: " << ftpErr << std::endl;
            g_app.cfg.ftpEnabled = false;
        }
    }

    if (g_app.syncEnabled.load()) sync::start();

    config_save();
    print_banner();

    bool uiRunning = false;
#ifdef _WIN32
    // Окно программы — основной интерфейс, поэтому консоль скрываем сразу, а не
    // после создания окна: если WebView2 не поднимется, она иначе остаётся висеть
    // на экране, а её закрытие завершает программу. Скрываем только свою консоль:
    // при запуске из cmd.exe она принадлежит оболочке пользователя.
    if (useUi && !keepConsole) {
        HWND con = GetConsoleWindow();
        DWORD owners[2] = {0, 0};
        if (con && GetConsoleProcessList(owners, 2) <= 1) ShowWindow(con, SW_HIDE);
    }
#endif
    if (useUi && ui::available()) {
        if (openBrowser) {
#ifdef _WIN32
            std::string url = "http://127.0.0.1:" + std::to_string(g_app.cfg.httpPort) + "/";
            ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#endif
        }
        int rc = ui::run(startInTray, keepConsole);
        uiRunning = (rc == 0);
        if (!uiRunning) {
            std::cerr << "Не удалось открыть окно — продолжаю работу как сервер.\n";
#ifdef _WIN32
            // Работаем как сервер — вернём консоль, иначе не будет ни окна, ни логов.
            if (!keepConsole) {
                HWND con = GetConsoleWindow();
                if (con) ShowWindow(con, SW_SHOW);
            }
#endif
        }
    }

    if (!uiRunning) {
#ifdef _WIN32
        bool interactive = _isatty(_fileno(stdin)) != 0;
#else
        bool interactive = isatty(fileno(stdin)) != 0;
#endif
        if (interactive) {
            std::cout << "Enter — остановить сервер.\n" << std::flush;
            std::string dummy;
            std::getline(std::cin, dummy);
        } else {
#ifdef _WIN32
            SetConsoleCtrlHandler(ctrl_handler, TRUE);
#endif
            while (!g_stop.load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
            }
        }
    }

    std::cout << "Остановка…\n" << std::flush;
    sync::stop();
    discovery::stop();
    if (g_app.cfg.ftpEnabled) ftp::stop();
    http::stop();
    net::shutdown();
    std::cout << "Готово.\n" << std::flush;
    return 0;
}
