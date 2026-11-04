#include "sync.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "app.h"
#include "http.h"
#include "json.h"
#include "store.h"

namespace sync {

namespace fs = std::filesystem;

static std::thread g_thread;
static std::atomic<bool> g_run{false};
static std::atomic<bool> g_push{false};  // нажата кнопка «Синхронизировать»
static std::mutex g_mtx;
static std::condition_variable g_cv;
static bool g_wake = false;

// Учёт прогресса для статус-строки интерфейса. Пишет только поток синхронизации,
// поэтому счётчики можно вести без мьютекса (они атомарные).
static void progress_plan(uint64_t size) {
    g_app.syncProgress.filesTotal.fetch_add(1);
    g_app.syncProgress.bytesTotal.fetch_add(size);
}

static void progress_moved(uint64_t delta) {
    if (delta) g_app.syncProgress.bytesDone.fetch_add(delta);
}

// Файл передан: дорисовываем остаток, который не попал в отчёты о ходе передачи.
static void progress_done(uint64_t size, uint64_t reported) {
    if (size > reported) g_app.syncProgress.bytesDone.fetch_add(size - reported);
    g_app.syncProgress.filesDone.fetch_add(1);
}

// Файл не передался: убираем его из плана, иначе полоса «зависнет» навсегда.
static void progress_failed(uint64_t size, uint64_t reported) {
    g_app.syncProgress.failed.fetch_add(1);
    g_app.syncProgress.filesTotal.fetch_sub(1);
    g_app.syncProgress.bytesTotal.fetch_sub(size);
    if (reported) g_app.syncProgress.bytesDone.fetch_sub(reported);
}

static std::vector<Peer> online_peers() {
    std::vector<Peer> v;
    std::lock_guard<std::mutex> lk(g_app.mtx);
    for (const auto& kv : g_app.peers) {
        if (kv.second.httpPort > 0 && peer_is_online(kv.second)) v.push_back(kv.second);
    }
    return v;
}

// Версия B новее версии A? Так сравнивают обе стороны: тот, кто отправляет,
// и тот, кто забирает, приходят к одному ответу и файл не ходит по кругу.
// При почти равном времени (расхождение часов до секунды) побеждает узел
// с меньшим id.
static bool b_is_newer(uint64_t aSize, int64_t aMtime, uint64_t bSize, int64_t bMtime,
                       const std::string& aId, const std::string& bId) {
    if (aSize != bSize) {
        int64_t diff = bMtime - aMtime;
        if (diff > 1) return true;
        if (diff >= -1) return bId < aId;
        return false;
    }
    return bMtime > aMtime + 1;
}

// Применяет одну отметку об удалении от соседа. true — файл был удалён.
static bool apply_tombstone_item(const std::string& share, const std::string& rel, int64_t ts) {
    if (rel.empty() || ts <= 0) return false;
    fs::path local;
    std::string err;
    if (!store::resolve(share, rel, local, err)) return false;
    std::error_code ec;
    if (!fs::exists(local, ec)) {
        store::add_tombstone(share, rel, ts);
        return false;
    }
    if (!fs::is_regular_file(local, ec)) return false;
    std::error_code ect;
    auto wt = fs::last_write_time(local, ect);
    int64_t lmtime = ect ? 0 : store::to_epoch(wt);
    if (ts < lmtime) {
        // Файл создан заново после удаления — «воскрешаем» его.
        store::clear_tombstone(share, rel);
        return false;
    }
    std::error_code ecr;
    fs::remove(local, ecr);
    if (ecr) return false;
    store::add_tombstone(share, rel, ts);
    log_line("синхронизация: удалён «" + share + "/" + rel + "» (удалён на другом узле)");
    // Подчищаем опустевшие папки, чтобы не оставалось пустых каталогов.
    fs::path base;
    std::string e3;
    if (store::resolve(share, "", base, e3)) {
        fs::path dir = local.parent_path();
        std::error_code ecp;
        while (!dir.empty() && dir != base) {
            if (!fs::is_empty(dir, ecp) || ecp) break;
            if (!fs::remove(dir, ecp) || ecp) break;
            dir = dir.parent_path();
        }
    }
    return true;
}

// Применяет удаления, сделанные на пире: файлы исчезают и у нас.
static void pull_tombstones(const Peer& p, const store::ShareInfo& s) {
    const std::string& share = s.name;                       // локальное имя
    const std::string key = s.key.empty() ? s.name : s.key;  // имя у пира
    int status = 0;
    std::string body, err;
    std::string target = "/api/tombstones?key=" + http::url_encode(key) +
                         "&share=" + http::url_encode(key);
    if (!http::get(p.ip, (uint16_t)p.httpPort, target, 4000, status, body, err)) return;
    if (status != 200) return;
    Json arr;
    if (!Json::parse(body, arr, &err) || !arr.isArray()) return;
    for (const Json& it : arr.items()) {
        apply_tombstone_item(share, it.get("p").asString(), it.get("t").asInt());
    }
}

// Забирает у пира файлы одной папки, которых у нас нет или которые у него новее.
static void pull_share(const Peer& p, const store::ShareInfo& s) {
    const std::string& share = s.name;                       // локальное имя
    const std::string key = s.key.empty() ? s.name : s.key;  // имя у пира
    int status = 0;
    std::string body, err;
    std::string target = "/api/manifest?key=" + http::url_encode(key) +
                         "&share=" + http::url_encode(key);
    if (!http::get(p.ip, (uint16_t)p.httpPort, target, 4000, status, body, err)) return;
    if (status == 404) return;  // у пира нет папки с таким ключом
    if (status != 200) return;
    Json arr;
    if (!Json::parse(body, arr, &err) || !arr.isArray()) return;

    const std::string peerLabel = p.name.empty() ? p.ip : p.name;
    pull_tombstones(p, s);

    for (const Json& it : arr.items()) {
        if (!g_run.load()) return;
        if (!g_app.syncEnabled.load()) return;

        std::string rel = it.get("p").asString();
        if (rel.empty()) continue;
        uint64_t size = (uint64_t)it.get("s").asInt();
        int64_t mtime = it.get("m").asInt();

        fs::path local;
        std::string err2;
        if (!store::resolve(share, rel, local, err2)) continue;

        int64_t tomb = store::tombstone_time(share, rel);
        if (tomb > 0) {
            if (mtime > tomb) {
                store::clear_tombstone(share, rel);  // файл новее удаления — воскрешаем
            } else {
                continue;  // файл удалён — не тянем обратно
            }
        }

        std::error_code ec;
        bool exists = fs::exists(local, ec);
        bool need = false;
        if (!exists) {
            need = true;
        } else if (!ec) {
            std::error_code ecs;
            uint64_t lsize = fs::is_regular_file(local, ecs) ? (uint64_t)fs::file_size(local, ecs) : 0;
            std::error_code ect;
            auto lwt = fs::last_write_time(local, ect);
            int64_t lmtime = ect ? 0 : store::to_epoch(lwt);
            // Локальная правка не затирается устаревшей копией.
            need = b_is_newer(lsize, lmtime, size, mtime, g_app.nodeId, p.id);
        }
        if (!need) continue;

        if (size > 8ull * 1024 * 1024 * 1024) continue;  // защитный лимит

        std::error_code ecd;
        fs::create_directories(local.parent_path(), ecd);

        fs::path tmp = store::temp_path(local);

        const std::string label = share + "/" + rel;
        uint64_t txId = tx_begin(TxDir::Sync, label, peerLabel, size);
        progress_plan(size);
        uint64_t lastReport = 0;
        std::string target2 = "/api/download?key=" + http::url_encode(key) +
                              "&share=" + http::url_encode(key) +
                              "&path=" + http::url_encode(rel);
        std::string derr;
        bool ok = http::download(p.ip, (uint16_t)p.httpPort, target2, tmp, 20000, derr,
                                 [&](uint64_t done, uint64_t total) {
                                     (void)total;
                                     if (done > lastReport && done - lastReport > 512 * 1024) {
                                         progress_moved(done - lastReport);
                                         lastReport = done;
                                         tx_update(txId, done);
                                     }
                                 });
        if (!ok) {
            tx_done(txId, "error: " + derr);
            progress_failed(size, lastReport);
            std::error_code ecr;
            fs::remove(tmp, ecr);
            continue;
        }

        std::error_code ecm;
        fs::last_write_time(tmp, store::from_epoch(mtime), ecm);
        std::string rerr;
        if (!store::replace_file(tmp, local, rerr)) {
            tx_done(txId, "error: " + rerr);
            std::error_code ecr;
            fs::remove(tmp, ecr);
            continue;
        }
        tx_done(txId, "done");
        progress_done(size, lastReport);
        g_app.syncPulled.fetch_add(1);
        g_app.bytesIn.fetch_add(size);
        log_line("синхронизация: получен «" + label + "» (" + std::to_string(size) + " б) от " +
                 peerLabel);
    }
}

// Выкладывает пиру свои удаления и файлы этой папки.
static void push_share(const Peer& p, const store::ShareInfo& s) {
    const std::string& share = s.name;                       // локальное имя
    const std::string key = s.key.empty() ? s.name : s.key;  // имя у пира
    int status = 0;
    std::string body, err;
    std::string target = "/api/manifest?key=" + http::url_encode(key) +
                         "&share=" + http::url_encode(key);
    if (!http::get(p.ip, (uint16_t)p.httpPort, target, 4000, status, body, err)) return;
    if (status == 404) return;  // у пира нет папки с таким ключом — некуда выкладывать
    if (status != 200) return;
    Json arr;
    if (!Json::parse(body, arr, &err) || !arr.isArray()) return;

    // Что уже есть у пира: путь -> (размер, mtime).
    std::map<std::string, std::pair<uint64_t, int64_t>> theirs;
    for (const Json& it : arr.items()) {
        std::string rel = it.get("p").asString();
        if (rel.empty()) continue;
        theirs[rel] = std::make_pair((uint64_t)it.get("s").asInt(), it.get("m").asInt());
    }

    const std::string peerLabel = p.name.empty() ? p.ip : p.name;

    // Сначала удаления: иначе сосед может получить новый файл и тут же удалить его.
    auto tombs = store::tombstones(share);
    if (!tombs.empty()) {
        Json list = Json::array();
        for (const auto& t : tombs) {
            Json j = Json::object();
            j.set("p", t.rel);
            j.set("t", (long long)t.ts);
            list.push(j);
        }
        std::string tt = "/api/tombstones?key=" + http::url_encode(key) +
                         "&share=" + http::url_encode(key);
        int st = 0;
        std::string rb, terr;
        // Не критично: если не дошло, удаления приедут фоновым циклом.
        http::post(p.ip, (uint16_t)p.httpPort, tt, list.dump(), 8000, st, rb, terr);
    }

    std::vector<store::Entry> files;
    std::string merr;
    if (!store::manifest(share, files, merr)) return;

    for (const auto& e : files) {
        if (!g_run.load() || !g_app.syncEnabled.load()) return;
        if (e.isDir || e.size > 8ull * 1024 * 1024 * 1024) continue;
        // Файл удалён локально (отметка не старше файла) — отправлять нечего.
        int64_t tomb = store::tombstone_time(share, e.rel);
        if (tomb > 0 && tomb >= e.mtime) continue;

        auto it = theirs.find(e.rel);
        bool need = it == theirs.end() ||
                    b_is_newer(it->second.first, it->second.second, e.size, e.mtime, p.id,
                               g_app.nodeId);
        if (!need) continue;

        fs::path local;
        std::string rerr;
        if (!store::resolve(share, e.rel, local, rerr)) continue;

        size_t slash = e.rel.rfind('/');
        std::string dirRel = slash == std::string::npos ? "" : e.rel.substr(0, slash);
        std::string fileName = slash == std::string::npos ? e.rel : e.rel.substr(slash + 1);
        std::string up = "/api/upload?key=" + http::url_encode(key) +
                         "&share=" + http::url_encode(key) +
                         "&path=" + http::url_encode(dirRel) +
                         "&name=" + http::url_encode(fileName) +
                         "&mtime=" + std::to_string((long long)e.mtime) + "&mkdirs=1";

        const std::string label = share + "/" + e.rel;
        uint64_t txId = tx_begin(TxDir::Sync, label, peerLabel, e.size);
        progress_plan(e.size);
        uint64_t lastReport = 0;
        std::string uerr;
        bool ok = http::upload(p.ip, (uint16_t)p.httpPort, up, local, e.size, 20000, uerr,
                               [&](uint64_t done, uint64_t total) {
                                   (void)total;
                                   if (done > lastReport && done - lastReport > 512 * 1024) {
                                       progress_moved(done - lastReport);
                                       lastReport = done;
                                       tx_update(txId, done);
                                   }
                               });
        if (!ok) {
            tx_done(txId, "error: " + uerr);
            progress_failed(e.size, lastReport);
            continue;
        }
        tx_done(txId, "done");
        progress_done(e.size, lastReport);
        g_app.bytesOut.fetch_add(e.size);
        log_line("синхронизация: отправлен «" + label + "» (" + std::to_string(e.size) + " б) узлу " +
                 peerLabel);
    }
}

// Уточняет у пира имя узла и список его сетевых папок (для интерфейса).
static void refresh_peer_info(const Peer& p) {
    int status = 0;
    std::string body, err;
    if (!http::get(p.ip, (uint16_t)p.httpPort, "/api/status", 3000, status, body, err)) return;
    if (status != 200) return;
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
    std::string nm = st.get("node").get("name").asString();
    std::lock_guard<std::mutex> lk(g_app.mtx);
    auto it = g_app.peers.find(p.id);
    if (it == g_app.peers.end()) return;
    it->second.shares = names;
    if (!nm.empty()) it->second.name = nm;
    it->second.lastSeen = now_sec();
}

static void pull_peer(const Peer& p) {
    refresh_peer_info(p);
    for (const auto& s : store::shares()) {
        if (!g_run.load() || !g_app.syncEnabled.load()) return;
        pull_share(p, s);
    }
}

static void push_peer(const Peer& p) {
    refresh_peer_info(p);
    for (const auto& s : store::shares()) {
        if (!g_run.load() || !g_app.syncEnabled.load()) return;
        push_share(p, s);
    }
}

static void run_round(bool push) {
    if (g_app.syncRunning.exchange(true)) {
        // Цикл уже идёт: попросим повторить отправку через секунду.
        if (push) g_push.store(true);
        return;
    }
    auto peers = online_peers();
    g_app.syncProgress.reset_round((int)peers.size());
    for (const auto& p : peers) {
        if (!g_run.load() || !g_app.syncEnabled.load()) break;
        if (push) {
            push_peer(p);
        } else {
            pull_peer(p);
        }
        g_app.syncProgress.peersDone.fetch_add(1);
    }
    g_app.syncProgress.lastAt.store(now_sec());
    g_app.syncProgress.lastFiles.store(g_app.syncProgress.filesDone.load());
    g_app.syncProgress.lastBytes.store(g_app.syncProgress.bytesDone.load());
    g_app.syncRunning.store(false);
}

static void loop() {
    while (g_run.load()) {
        int interval = g_app.syncIntervalSec.load();
        if (interval < 3) interval = 3;
        // Пока висит невыполненная отправка, ждём коротко, чтобы не терять нажатие.
        int waitSec = g_push.load() ? 1 : interval;
        {
            std::unique_lock<std::mutex> lk(g_mtx);
            g_cv.wait_for(lk, std::chrono::seconds(waitSec), [] { return g_wake; });
            g_wake = false;
        }
        if (!g_run.load()) break;
        if (!g_app.syncEnabled.load()) {
            g_push.store(false);
            continue;
        }
        bool push = g_push.exchange(false);
        run_round(push);
    }
}

void start() {
    if (g_run.exchange(true)) return;
    g_thread = std::thread(loop);
}

void stop() {
    if (!g_run.exchange(false)) return;
    {
        std::lock_guard<std::mutex> lk(g_mtx);
        g_wake = true;
    }
    g_cv.notify_all();
    if (g_thread.joinable()) g_thread.join();
}

// Событие (загрузка, удаление, новый узел): забрать недостающее у соседей.
void wake() {
    {
        std::lock_guard<std::mutex> lk(g_mtx);
        g_wake = true;
    }
    g_cv.notify_all();
}

// Кнопка «Синхронизировать»: выложить свои папки соседям прямо сейчас.
void push_now() {
    if (!g_run.load()) return;
    g_push.store(true);
    {
        std::lock_guard<std::mutex> lk(g_mtx);
        g_wake = true;
    }
    g_cv.notify_all();
}

int apply_remote_tombstones(const std::string& share,
                            const std::vector<std::pair<std::string, int64_t>>& items) {
    int removed = 0;
    for (const auto& it : items) {
        if (apply_tombstone_item(share, it.first, it.second)) ++removed;
    }
    return removed;
}

}  // namespace sync
