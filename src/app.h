#pragma once
// Глобальное состояние узла обмена: конфигурация, пиры, журнал передач.
#include <atomic>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <vector>

// Сетевая папка: имя — её идентификатор в интерфейсе, путь — локальный каталог,
// key — ключ синхронизации (пустой = совпадает с именем).
struct Share {
    std::string name;
    std::string path;
    std::string key;
};

struct Config {
    std::vector<Share> shares;  // несколько общих папок (минимум одна)
    std::string name;
    int httpPort = 8080;
    int ftpPort = 2121;
    int udpPort = 45454;
    bool ftpEnabled = true;
    bool syncEnabled = true;
    int syncIntervalSec = 10;
};

struct Peer {
    std::string id;
    std::string name;
    std::string ip;
    int httpPort = 0;
    int ftpPort = 0;
    int64_t lastSeen = 0;
    bool manual = false;
    std::vector<std::string> shares;  // имена папок, которые узел раздаёт
};

enum class TxDir { Upload = 0, Download = 1, Sync = 2, Ftp = 3 };

struct Transfer {
    uint64_t id = 0;
    TxDir dir = TxDir::Upload;
    std::string file;
    std::string peer;
    uint64_t bytes = 0;
    uint64_t total = 0;
    std::string status;
    int64_t ts = 0;
};

// Прогресс синхронизации для статус-строки в интерфейсе: сколько файлов и байт
// предстоит передать в текущем раунде, сколько уже сделано и что вышло в прошлом.
// Пишет только поток синхронизации, читает HTTP-обработчик — поэтому атомики.
struct SyncProgress {
    std::atomic<int> peersTotal{0};      // соседей в раунде
    std::atomic<int> peersDone{0};
    std::atomic<int> filesTotal{0};      // файлов найдено к передаче
    std::atomic<int> filesDone{0};
    std::atomic<uint64_t> bytesTotal{0};  // сколько всего предстоит передать
    std::atomic<uint64_t> bytesDone{0};
    std::atomic<int> failed{0};          // файлов не удалось передать
    std::atomic<int64_t> lastAt{0};      // когда закончился прошлый раунд
    std::atomic<int> lastFiles{0};       // сколько файлов он передал
    std::atomic<uint64_t> lastBytes{0};

    void reset_round(int peers) {
        peersTotal.store(peers);
        peersDone.store(0);
        filesTotal.store(0);
        filesDone.store(0);
        bytesTotal.store(0);
        bytesDone.store(0);
        failed.store(0);
    }
};

struct App {
    Config cfg;
    std::string nodeId;
    int64_t startedAt = 0;

    mutable std::mutex mtx;               // защищает peers/transfers
    std::map<std::string, Peer> peers;    // ключ — устойчивый id пира
    std::deque<Transfer> transfers;

    std::atomic<uint64_t> seq{0};
    std::atomic<uint64_t> bytesIn{0};
    std::atomic<uint64_t> bytesOut{0};
    std::atomic<uint64_t> syncPulled{0};
    std::atomic<bool> syncEnabled{true};
    std::atomic<int> syncIntervalSec{10};
    std::atomic<bool> syncRunning{false};
    SyncProgress syncProgress;

    mutable std::mutex errMtx;
    std::string rootError;
};

extern App g_app;

int64_t now_sec();
bool peer_is_online(const Peer& p);

uint64_t tx_begin(TxDir dir, const std::string& file, const std::string& peer, uint64_t total);
void tx_update(uint64_t id, uint64_t bytes);
void tx_done(uint64_t id, const std::string& status);
void tx_clear();

void log_line(const std::string& msg);
void set_root_error(const std::string& e);
std::string get_root_error();

// ---- файл настроек obmen.ini (общий для сервера и встроенного окна) ----
void config_set_path(const std::string& path);
const std::string& config_path();
void config_load();
void config_save();

// Список папок в конфиге приводится к тому, что реально принято хранилищем.
void config_store_shares();

// Добавляет узел по адресу "192.168.0.10:8080", проверяя, что там наш узел.
bool add_manual_peer(const std::string& addr, std::string& err);
