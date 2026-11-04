#include "store.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <system_error>

#include "json.h"

namespace store {

namespace fs = std::filesystem;

static std::mutex g_sharesMtx;
static std::vector<ShareInfo> g_shares;

static const char* kStateFile = ".obmen-state.json";

template <class StringT>
static std::string narrow(const StringT& s) {
    return std::string(s.begin(), s.end());
}

std::string to_utf8(const fs::path& p) { return narrow(p.u8string()); }

fs::path from_utf8(const std::string& s) { return fs::u8path(s); }

static bool is_internal_name(const std::string& name) {
    return name == kStateFile || (name.size() > 5 && name.compare(name.size() - 5, 5, ".part") == 0);
}

// Имя папки в сети: без разделителей пути и служебных символов.
static bool valid_share_name(const std::string& name) {
    if (name.empty() || name.size() > 64) return false;
    if (name == "." || name == "..") return false;
    for (char c : name) {
        if ((unsigned char)c < 0x20) return false;
        switch (c) {
        case '/':
        case '\\':
        case ':':
        case '*':
        case '?':
        case '"':
        case '<':
        case '>':
        case '|':
            return false;
        default:
            break;
        }
    }
    return true;
}

static std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    return s;
}

static std::string derive_name(const std::string& path) {
    std::string p = path;
    while (!p.empty() && (p.back() == '\\' || p.back() == '/')) p.pop_back();
    fs::path f = from_utf8(p);
    std::string name = to_utf8(f.filename());
    if (!valid_share_name(name)) name = "Обмен";
    return name;
}

std::vector<ShareInfo> shares() {
    std::lock_guard<std::mutex> lk(g_sharesMtx);
    return g_shares;
}

bool has_share(const std::string& name) {
    std::lock_guard<std::mutex> lk(g_sharesMtx);
    std::string n = lower(name);
    for (const auto& s : g_shares) {
        if (lower(s.name) == n) return true;
    }
    return false;
}

bool get_share(const std::string& name, ShareInfo& out) {
    std::lock_guard<std::mutex> lk(g_sharesMtx);
    std::string n = lower(name);
    for (const auto& s : g_shares) {
        if (lower(s.name) == n) {
            out = s;
            return true;
        }
    }
    return false;
}

bool get_share_by_key(const std::string& key, ShareInfo& out) {
    std::lock_guard<std::mutex> lk(g_sharesMtx);
    std::string k = lower(key);
    for (const auto& s : g_shares) {
        const std::string& sk = s.key.empty() ? s.name : s.key;
        if (lower(sk) == k) {
            out = s;
            return true;
        }
    }
    return false;
}

std::string default_share() {
    std::lock_guard<std::mutex> lk(g_sharesMtx);
    return g_shares.empty() ? std::string() : g_shares.front().name;
}

// ---------------------------------------------------- надгробия (кэш)

static std::mutex g_tombMtx;
static std::map<std::string, std::map<std::string, int64_t>> g_tombs;
static std::set<std::string> g_loaded;

static void tomb_reset_cache() {
    std::lock_guard<std::mutex> lk(g_tombMtx);
    g_tombs.clear();
    g_loaded.clear();
}

static void tomb_save_locked(const std::string& share);

// Возвращает кэш надгробий папки, при необходимости читая файл состояния.
static std::map<std::string, int64_t>& tomb_cache(const std::string& share) {
    auto it = g_loaded.find(share);
    if (it != g_loaded.end()) return g_tombs[share];

    g_loaded.insert(share);
    std::map<std::string, int64_t>& m = g_tombs[share];
    ShareInfo si;
    if (!get_share(share, si)) return m;
    std::ifstream f(from_utf8(si.path) / kStateFile, std::ios::binary);
    if (!f) return m;
    std::string data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (data.empty()) return m;
    Json j;
    if (!Json::parse(data, j)) return m;
    Json deletedArr = j.get("deleted");
    for (const Json& e : deletedArr.items()) {
        std::string p = e.get("p").asString();
        int64_t t = e.get("t").asInt();
        if (!p.empty() && t > 0) m[p] = t;
    }
    return m;
}

static void tomb_save_locked(const std::string& share) {
    ShareInfo si;
    if (!get_share(share, si)) return;
    auto it = g_tombs.find(share);
    if (it == g_tombs.end()) return;
    Json arr = Json::array();
    for (const auto& kv : it->second) {
        Json j = Json::object();
        j.set("p", kv.first);
        j.set("t", (long long)kv.second);
        arr.push(j);
    }
    Json rootObj = Json::object();
    rootObj.set("deleted", arr);
    std::ofstream f(from_utf8(si.path) / kStateFile, std::ios::binary | std::ios::trunc);
    if (f) f << rootObj.dump();
}

// ---------------------------------------------------- набор папок

static bool prepare_path(const std::string& path, fs::path& out, std::string& err) {
    if (path.empty()) {
        err = "путь не задан";
        return false;
    }
    std::error_code ec;
    fs::path p = from_utf8(path);
    if (!fs::exists(p, ec)) {
        fs::create_directories(p, ec);
        if (ec) {
            err = "не удалось создать папку: " + ec.message();
            return false;
        }
    }
    if (!fs::is_directory(p, ec)) {
        err = "указанный путь не является папкой";
        return false;
    }
    fs::path canon = fs::weakly_canonical(p, ec);
    if (ec || canon.empty()) {
        canon = fs::absolute(p, ec);
        if (ec || canon.empty()) {
            err = "не удалось определить путь: " + ec.message();
            return false;
        }
    }
    out = canon;
    return true;
}

bool set_shares(const std::vector<ShareInfo>& list, std::string& err) {
    std::vector<ShareInfo> prepared;
    std::set<std::string> used;
    std::set<std::string> usedKeys;
    std::string firstProblem;
    for (const auto& s : list) {
        fs::path p;
        std::string e;
        if (!prepare_path(s.path, p, e)) {
            if (firstProblem.empty()) firstProblem = "«" + s.name + "»: " + e;
            continue;
        }
        std::string name = valid_share_name(s.name) ? s.name : derive_name(s.path);
        std::string base = name;
        int k = 2;
        while (used.count(lower(name))) name = base + " (" + std::to_string(k++) + ")";
        used.insert(lower(name));
        // Ключ синхронизации не должен совпадать у двух локальных папок.
        std::string key = valid_share_name(s.key) ? s.key : name;
        if (lower(key) != lower(name) && usedKeys.count(lower(key))) key = name;
        usedKeys.insert(lower(key));
        prepared.push_back(ShareInfo{name, to_utf8(p), key});
    }
    bool any = !prepared.empty();
    {
        std::lock_guard<std::mutex> lk(g_sharesMtx);
        g_shares = std::move(prepared);
    }
    tomb_reset_cache();
    if (!any) err = firstProblem.empty() ? "нет доступных папок" : firstProblem;
    return any;
}

bool add_share(const std::string& nameIn, const std::string& pathIn, const std::string& keyIn,
               std::string& err) {
    fs::path p;
    if (!prepare_path(pathIn, p, err)) return false;
    std::string name = nameIn.empty() ? derive_name(pathIn) : nameIn;
    if (!valid_share_name(name)) {
        err = "недопустимое имя папки (без \\ / : * ? \" < > |)";
        return false;
    }
    std::string key = keyIn.empty() ? name : keyIn;
    if (!valid_share_name(key)) {
        err = "недопустимый ключ синхронизации (без \\ / : * ? \" < > |)";
        return false;
    }
    {
        std::lock_guard<std::mutex> lk(g_sharesMtx);
        for (const auto& s : g_shares) {
            if (lower(s.name) == lower(name)) {
                err = "папка с именем «" + name + "» уже есть";
                return false;
            }
            if (lower(s.path) == lower(to_utf8(p))) {
                err = "эта папка уже добавлена как «" + s.name + "»";
                return false;
            }
            const std::string& sk = s.key.empty() ? s.name : s.key;
            if (lower(sk) == lower(key)) {
                err = "ключ «" + key + "» уже занят папкой «" + s.name + "»";
                return false;
            }
        }
        g_shares.push_back(ShareInfo{name, to_utf8(p), key});
    }
    tomb_reset_cache();
    return true;
}

bool remove_share(const std::string& name, std::string& err) {
    bool removed = false;
    {
        std::lock_guard<std::mutex> lk(g_sharesMtx);
        std::string n = lower(name);
        auto it = std::remove_if(g_shares.begin(), g_shares.end(),
                                 [&](const ShareInfo& s) { return lower(s.name) == n; });
        if (it != g_shares.end()) {
            g_shares.erase(it, g_shares.end());
            removed = true;
        }
    }
    if (removed) {
        tomb_reset_cache();
    } else {
        err = "папка «" + name + "» не найдена";
    }
    return removed;
}

bool set_share_path(const std::string& name, const std::string& path, std::string& err) {
    fs::path p;
    if (!prepare_path(path, p, err)) return false;
    bool ok = false;
    {
        std::lock_guard<std::mutex> lk(g_sharesMtx);
        std::string n = lower(name);
        for (auto& s : g_shares) {
            if (lower(s.name) == n) {
                s.path = to_utf8(p);
                ok = true;
                break;
            }
        }
    }
    if (!ok) err = "папка не найдена";
    else tomb_reset_cache();
    return ok;
}

// Меняет ключ синхронизации: пустой ключ = синхронизировать по имени папки.
bool set_share_key(const std::string& name, const std::string& keyIn, std::string& err) {
    bool ok = false;
    {
        std::lock_guard<std::mutex> lk(g_sharesMtx);
        std::string n = lower(name);
        std::string key = keyIn;
        for (const auto& s : g_shares) {
            if (lower(s.name) == n) {
                if (key.empty()) key = s.name;
                ok = true;
                break;
            }
        }
        if (!ok) {
            err = "папка «" + name + "» не найдена";
            return false;
        }
        if (!valid_share_name(key)) {
            err = "недопустимый ключ синхронизации (без \\ / : * ? \" < > |)";
            return false;
        }
        for (const auto& s : g_shares) {
            const std::string& sk = s.key.empty() ? s.name : s.key;
            if (lower(s.name) != n && lower(sk) == lower(key)) {
                err = "ключ «" + key + "» уже занят папкой «" + s.name + "»";
                return false;
            }
        }
        for (auto& s : g_shares) {
            if (lower(s.name) == n) {
                s.key = key;
                break;
            }
        }
    }
    tomb_reset_cache();
    return true;
}

// ---------------------------------------------------- содержимое папки

static bool split_rel(const std::string& rel, std::vector<std::string>& parts, std::string& err) {
    std::string s = rel;
    for (auto& c : s) {
        if (c == '\\') c = '/';
    }
    parts.clear();
    size_t i = 0;
    while (i <= s.size()) {
        size_t j = s.find('/', i);
        if (j == std::string::npos) j = s.size();
        std::string part = s.substr(i, j - i);
        if (!part.empty() && part != ".") {
            if (part == "..") {
                err = "недопустимый путь (..)";
                return false;
            }
            if (part.find(':') != std::string::npos || part.find('\0') != std::string::npos) {
                err = "недопустимый путь";
                return false;
            }
            parts.push_back(part);
        }
        if (j >= s.size()) break;
        i = j + 1;
    }
    return true;
}

bool resolve(const std::string& share, const std::string& rel, fs::path& out, std::string& err) {
    ShareInfo si;
    if (!get_share(share, si)) {
        err = "папка «" + share + "» не найдена";
        return false;
    }
    fs::path base = from_utf8(si.path);
    std::vector<std::string> parts;
    if (!split_rel(rel, parts, err)) return false;

    fs::path p = base;
    for (const auto& part : parts) p /= from_utf8(part);

    std::error_code ec;
    fs::path canon = fs::weakly_canonical(p, ec);
    if (ec || canon.empty()) canon = p.lexically_normal();

    fs::path relchk = fs::relative(canon, base, ec);
    if (ec) {
        err = "не удалось проверить путь";
        return false;
    }
    auto it = relchk.begin();
    if (it != relchk.end() && *it == "..") {
        err = "выход за пределы папки";
        return false;
    }
    out = canon;
    return true;
}

bool list(const std::string& share, const std::string& rel, std::vector<Entry>& out,
          std::string& err) {
    fs::path dir;
    if (!resolve(share, rel, dir, err)) return false;
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) {
        err = "это не папка";
        return false;
    }
    std::string base = rel;
    for (auto& c : base) {
        if (c == '\\') c = '/';
    }
    while (!base.empty() && base.back() == '/') base.pop_back();

    out.clear();
    fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec);
    fs::directory_iterator end;
    for (; it != end; it.increment(ec)) {
        if (ec) break;
        Entry e;
        std::string name = to_utf8(it->path().filename());
        if (name.empty()) continue;
        if (is_internal_name(name)) continue;
        e.name = name;
        e.rel = base.empty() ? name : base + "/" + name;
        std::error_code ec2;
        e.isDir = it->is_directory(ec2);
        if (!e.isDir) {
            e.size = it->is_regular_file(ec2) ? (uint64_t)it->file_size(ec2) : 0;
        }
        std::error_code ec3;
        auto wt = fs::last_write_time(it->path(), ec3);
        if (!ec3) e.mtime = to_epoch(wt);
        out.push_back(std::move(e));
    }
    std::sort(out.begin(), out.end(), [](const Entry& a, const Entry& b) {
        if (a.isDir != b.isDir) return a.isDir;
        return lower(a.name) < lower(b.name);
    });
    return true;
}

bool manifest(const std::string& share, std::vector<Entry>& out, std::string& err) {
    ShareInfo si;
    if (!get_share(share, si)) {
        err = "папка «" + share + "» не найдена";
        return false;
    }
    fs::path base = from_utf8(si.path);
    out.clear();
    std::error_code ec;
    if (!fs::is_directory(base, ec)) {
        err = "папка недоступна";
        return false;
    }
    fs::recursive_directory_iterator it(base, fs::directory_options::skip_permission_denied, ec);
    fs::recursive_directory_iterator end;
    for (; it != end; it.increment(ec)) {
        if (ec) break;
        std::error_code ec2;
        if (!it->is_regular_file(ec2)) continue;
        std::string name = to_utf8(it->path().filename());
        if (is_internal_name(name)) continue;
        fs::path relp = fs::relative(it->path(), base, ec2);
        if (ec2) continue;
        Entry e;
        e.rel = narrow(relp.generic_u8string());
        e.name = name;
        e.size = (uint64_t)it->file_size(ec2);
        if (ec2) continue;
        std::error_code ec3;
        auto wt = fs::last_write_time(it->path(), ec3);
        if (!ec3) e.mtime = to_epoch(wt);
        out.push_back(std::move(e));
    }
    return true;
}

// ---------------------------------------------------- надгробия (API)

std::vector<Tombstone> tombstones(const std::string& share) {
    std::vector<Tombstone> out;
    std::lock_guard<std::mutex> lk(g_tombMtx);
    auto& m = tomb_cache(share);
    out.reserve(m.size());
    for (const auto& kv : m) {
        Tombstone t;
        t.rel = kv.first;
        t.ts = kv.second;
        out.push_back(std::move(t));
    }
    std::sort(out.begin(), out.end(),
              [](const Tombstone& a, const Tombstone& b) { return a.ts < b.ts; });
    return out;
}

int64_t tombstone_time(const std::string& share, const std::string& rel) {
    std::lock_guard<std::mutex> lk(g_tombMtx);
    auto& m = tomb_cache(share);
    auto it = m.find(rel);
    return it == m.end() ? 0 : it->second;
}

void add_tombstone(const std::string& share, const std::string& rel, int64_t ts) {
    if (rel.empty() || ts <= 0) return;
    std::lock_guard<std::mutex> lk(g_tombMtx);
    auto& m = tomb_cache(share);
    auto it = m.find(rel);
    if (it == m.end() || it->second < ts) m[rel] = ts;
    if (m.size() > 20000) {
        std::vector<std::pair<int64_t, std::string>> all;
        all.reserve(m.size());
        for (const auto& kv : m) all.emplace_back(kv.second, kv.first);
        std::sort(all.begin(), all.end());
        for (size_t i = 0; i < all.size() / 2; ++i) m.erase(all[i].second);
    }
    tomb_save_locked(share);
}

void clear_tombstone(const std::string& share, const std::string& rel) {
    std::lock_guard<std::mutex> lk(g_tombMtx);
    auto& m = tomb_cache(share);
    auto it = m.find(rel);
    if (it == m.end()) return;
    m.erase(it);
    tomb_save_locked(share);
}

bool remove_entry(const std::string& share, const std::string& rel, std::string& err) {
    if (rel.empty()) {
        err = "нельзя удалить корень папки";
        return false;
    }
    fs::path p;
    if (!resolve(share, rel, p, err)) return false;
    std::error_code ec;
    int64_t ts = 0;
    {
        auto now = std::chrono::system_clock::now();
        ts = (int64_t)std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
    }
    if (fs::is_directory(p, ec)) {
        std::vector<std::string> files;
        std::error_code eci;
        fs::recursive_directory_iterator it(p, fs::directory_options::skip_permission_denied, eci);
        fs::recursive_directory_iterator end;
        for (; it != end; it.increment(eci)) {
            if (eci) break;
            std::error_code ec2;
            if (!it->is_regular_file(ec2)) continue;
            fs::path relp = fs::relative(it->path(), p, ec2);
            if (ec2) continue;
            files.push_back(rel + "/" + narrow(relp.generic_u8string()));
        }
        ec.clear();
        fs::remove_all(p, ec);
        if (ec) {
            err = "не удалось удалить: " + ec.message();
            return false;
        }
        for (const auto& f : files) add_tombstone(share, f, ts);
        return true;
    }
    ec.clear();
    fs::remove(p, ec);
    if (ec) {
        err = "не удалось удалить: " + ec.message();
        return false;
    }
    add_tombstone(share, rel, ts);
    return true;
}

// ---------------------------------------------------- утилиты

fs::path temp_path(const fs::path& target) {
    static std::atomic<uint64_t> seq{0};
    static const uint64_t base =
        (uint64_t)std::chrono::steady_clock::now().time_since_epoch().count();
    fs::path p = target;
    p += "." + std::to_string(base % 100000) + "-" + std::to_string(++seq) + ".part";
    return p;
}

int64_t to_epoch(fs::file_time_type t) {
    auto fileNow = fs::file_time_type::clock::now();
    auto sysNow = std::chrono::system_clock::now();
    auto diff = std::chrono::duration_cast<std::chrono::seconds>(t - fileNow).count();
    auto sysSecs =
        std::chrono::duration_cast<std::chrono::seconds>(sysNow.time_since_epoch()).count();
    return (int64_t)sysSecs + (int64_t)diff;
}

fs::file_time_type from_epoch(int64_t secs) {
    auto sysNow = std::chrono::system_clock::now();
    auto target = std::chrono::system_clock::time_point(std::chrono::seconds(secs));
    auto delta = std::chrono::duration_cast<fs::file_time_type::duration>(target - sysNow);
    return fs::file_time_type::clock::now() + delta;
}

bool replace_file(const fs::path& src, const fs::path& dst, std::string& err) {
    std::error_code ec;
    fs::remove(dst, ec);
    ec.clear();
    fs::rename(src, dst, ec);
    if (!ec) return true;
    ec.clear();
    fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        err = "не удалось сохранить файл: " + ec.message();
        return false;
    }
    ec.clear();
    fs::remove(src, ec);
    return true;
}

}  // namespace store
