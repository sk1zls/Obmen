#pragma once
// Общие папки: безопасное разрешение путей, листинг, надгробия удалений.
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace store {

struct ShareInfo {
    std::string name;  // имя папки в сети (локальный адрес: файлы, надгробия)
    std::string path;  // абсолютный путь на диске
    std::string key;   // ключ синхронизации; пустой = совпадает с name
};

struct Entry {
    std::string rel;   // путь внутри папки, разделитель '/'
    std::string name;  // имя элемента
    bool isDir = false;
    uint64_t size = 0;
    int64_t mtime = 0;  // unix-время, секунды
};

struct Tombstone {
    std::string rel;
    int64_t ts = 0;
};

// ---- набор общих папок узла ----
std::vector<ShareInfo> shares();
bool has_share(const std::string& name);
bool get_share(const std::string& name, ShareInfo& out);
// Папка по ключу синхронизации: так узел находит свою папку для чужого запроса.
bool get_share_by_key(const std::string& key, ShareInfo& out);
// Имя первой папки в списке ("" если папок нет).
std::string default_share();

bool set_shares(const std::vector<ShareInfo>& list, std::string& err);
bool add_share(const std::string& name, const std::string& path, const std::string& key,
               std::string& err);
bool remove_share(const std::string& name, std::string& err);
bool set_share_path(const std::string& name, const std::string& path, std::string& err);
bool set_share_key(const std::string& name, const std::string& key, std::string& err);

// ---- работа с содержимым конкретной папки ----
bool resolve(const std::string& share, const std::string& rel, std::filesystem::path& out,
             std::string& err);
bool list(const std::string& share, const std::string& rel, std::vector<Entry>& out,
          std::string& err);
bool manifest(const std::string& share, std::vector<Entry>& out, std::string& err);

// ---- «надгробия»: отметки об удалении, чтобы удаление расходилось по узлам ----
std::vector<Tombstone> tombstones(const std::string& share);
int64_t tombstone_time(const std::string& share, const std::string& rel);
void add_tombstone(const std::string& share, const std::string& rel, int64_t ts);
void clear_tombstone(const std::string& share, const std::string& rel);

// Удаляет файл или папку и фиксирует надгробия для всех удалённых файлов.
bool remove_entry(const std::string& share, const std::string& rel, std::string& err);

// ---- утилиты ----
int64_t to_epoch(std::filesystem::file_time_type t);
std::filesystem::file_time_type from_epoch(int64_t secs);

std::string to_utf8(const std::filesystem::path& p);
std::filesystem::path from_utf8(const std::string& s);

// Атомарная замена файла (Windows требует явного удаления цели).
bool replace_file(const std::filesystem::path& src, const std::filesystem::path& dst, std::string& err);

// Уникальный временный файл рядом с целевым: имя оканчивается на «.part»,
// поэтому в листинг и манифест он не попадает. Нужен, чтобы две передачи
// одного и того же файла (например, отправка соседу и её же фоновая загрузка)
// не писали в один и тот же временный файл.
std::filesystem::path temp_path(const std::filesystem::path& target);

}  // namespace store
