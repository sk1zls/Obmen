#include "ftp.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include "app.h"
#include "net.h"
#include "store.h"

namespace ftp {

namespace fs = std::filesystem;

static socket_t g_ls = kInvalidSocket;
static std::atomic<bool> g_run{false};
static std::thread g_accept;

struct Session {
    socket_t ctrl = kInvalidSocket;
    std::string ip;
    std::string user;
    bool logged = false;
    bool quit = false;
    bool binary = true;
    std::string share;  // выбранная сетевая папка ("" — верхний уровень)
    std::string cwd;    // путь внутри папки, без ведущего '/'
    socket_t pasv = kInvalidSocket;
    uint64_t rest = 0;
    std::string buf;
    size_t pos = 0;
    bool closed = false;

    bool fill() {
        if (closed) return false;
        char tmp[8192];
        int n = net::recv_some(ctrl, tmp, sizeof tmp);
        if (n <= 0) {
            closed = true;
            return false;
        }
        buf.append(tmp, (size_t)n);
        return true;
    }
    bool read_line(std::string& out) {
        for (;;) {
            size_t idx = buf.find('\n', pos);
            if (idx != std::string::npos) {
                size_t end = idx;
                if (end > pos && buf[end - 1] == '\r') --end;
                out.assign(buf, pos, end - pos);
                pos = idx + 1;
                if (pos > 65536) {
                    buf.erase(0, pos);
                    pos = 0;
                }
                return true;
            }
            if (buf.size() - pos > 8192) return false;
            if (!fill()) return false;
        }
    }
};

static bool reply(Session& s, const std::string& line) {
    std::string out = line;
    out += "\r\n";
    return net::send_all(s.ctrl, out.data(), out.size());
}

static std::string upper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::toupper(c); });
    return s;
}

// Разбирает аргумент команды в (папка, путь внутри папки) относительно текущего места.
// Пустая папка означает верхний уровень FTP — список сетевых папок.
static void resolve_ftp(const Session& s, const std::string& arg, std::string& share,
                        std::string& rel) {
    std::string combined;
    if (!arg.empty() && arg[0] == '/') {
        combined = arg.substr(1);
    } else {
        combined = s.share;
        if (!s.cwd.empty()) combined += "/" + s.cwd;
        if (!arg.empty()) {
            if (!combined.empty()) combined += "/";
            combined += arg;
        }
    }
    std::vector<std::string> parts;
    size_t i = 0;
    while (i <= combined.size()) {
        size_t j = combined.find('/', i);
        if (j == std::string::npos) j = combined.size();
        std::string part = combined.substr(i, j - i);
        if (!part.empty() && part != ".") {
            if (part == "..") {
                if (!parts.empty()) parts.pop_back();
            } else {
                parts.push_back(part);
            }
        }
        if (j >= combined.size()) break;
        i = j + 1;
    }
    share = parts.empty() ? std::string() : parts.front();
    rel.clear();
    for (size_t k = 1; k < parts.size(); ++k) {
        if (k > 1) rel += "/";
        rel += parts[k];
    }
}

// Путь для PWD: "/" или "/папка/путь".
static std::string ftp_path(const Session& s) {
    std::string p = "/";
    if (!s.share.empty()) {
        p += s.share;
        if (!s.cwd.empty()) p += "/" + s.cwd;
    }
    return p;
}

static std::string parent_rel(const std::string& rel) {
    size_t p = rel.find_last_of('/');
    return p == std::string::npos ? std::string() : rel.substr(0, p);
}

static std::string fmt_time(int64_t epoch) {
    std::time_t t = (std::time_t)epoch;
    std::tm tmv{};
#ifdef _WIN32
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    static const char* mon[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    int m = tmv.tm_mon;
    if (m < 0 || m > 11) m = 0;
    char buf[40];
    std::snprintf(buf, sizeof buf, "%s %2d %02d:%02d", mon[m], tmv.tm_mday, tmv.tm_hour, tmv.tm_min);
    return buf;
}

static std::string fmt_mdtm(int64_t epoch) {
    std::time_t t = (std::time_t)epoch;
    std::tm tmv{};
#ifdef _WIN32
    gmtime_s(&tmv, &t);
#else
    gmtime_r(&t, &tmv);
#endif
    char buf[32];
    std::snprintf(buf, sizeof buf, "%04d%02d%02d%02d%02d%02d", tmv.tm_year + 1900, tmv.tm_mon + 1,
                  tmv.tm_mday, tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
    return buf;
}

static socket_t accept_data(Session& s) {
    if (s.pasv == kInvalidSocket) return kInvalidSocket;
    fd_set rf;
    FD_ZERO(&rf);
    FD_SET(s.pasv, &rf);
    timeval tv{};
    tv.tv_sec = 20;
    int r = select((int)(s.pasv + 1), &rf, nullptr, nullptr, &tv);
    socket_t d = kInvalidSocket;
    if (r > 0) d = net::accept_tcp(s.pasv);
    net::close_socket(s.pasv);
    s.pasv = kInvalidSocket;
    if (d != kInvalidSocket) net::set_timeouts(d, 60000, 60000);
    return d;
}

static void cmd_pasv(Session& s) {
    if (s.pasv != kInvalidSocket) {
        net::close_socket(s.pasv);
        s.pasv = kInvalidSocket;
    }
    std::string err;
    socket_t ls = net::listen_tcp(0, err);
    if (ls == kInvalidSocket) {
        reply(s, "425 Не удалось открыть пассивный порт");
        return;
    }
    s.pasv = ls;
    uint16_t port = net::local_port(ls);
    std::string ip = net::socket_local_ip(s.ctrl);
    if (ip.empty()) ip = "127.0.0.1";
    int a = 0, b = 0, c = 0, d = 0;
    if (std::sscanf(ip.c_str(), "%d.%d.%d.%d", &a, &b, &c, &d) != 4) {
        a = 127;
        b = 0;
        c = 0;
        d = 1;
    }
    char buf[128];
    std::snprintf(buf, sizeof buf, "227 Вход в пассивный режим (%d,%d,%d,%d,%d,%d)", a, b, c, d,
                  port / 256, port % 256);
    reply(s, buf);
}

static void cmd_list(Session& s, const std::string& arg, bool namesOnly) {
    std::string share, rel;
    resolve_ftp(s, arg, share, rel);
    std::error_code ec;
    std::string text;

    if (share.empty()) {
        // Верхний уровень FTP — список сетевых папок.
        for (const auto& sh : store::shares()) {
            if (namesOnly) {
                text += sh.name + "\r\n";
                continue;
            }
            char line[640];
            std::snprintf(line, sizeof line, "drwxr-xr-x 1 ftp ftp %12llu %s %s\r\n",
                          (unsigned long long)0, fmt_time(now_sec()).c_str(), sh.name.c_str());
            text += line;
        }
    } else {
        fs::path target;
        std::string err;
        if (!store::resolve(share, rel, target, err)) {
            reply(s, "550 " + err);
            return;
        }
        if (fs::is_directory(target, ec)) {
            std::vector<store::Entry> entries;
            std::string lerr;
            if (!store::list(share, rel, entries, lerr)) {
                reply(s, "550 " + lerr);
                return;
            }
            for (const auto& e : entries) {
                if (namesOnly) {
                    text += e.name + "\r\n";
                    continue;
                }
                bool isDir = e.isDir;
                uint64_t size = e.size;
                char line[640];
                std::snprintf(line, sizeof line, "%s 1 ftp ftp %12llu %s %s\r\n",
                              isDir ? "drwxr-xr-x" : "-rw-r--r--", (unsigned long long)size,
                              fmt_time(e.mtime).c_str(), e.name.c_str());
                text += line;
            }
        } else {
            std::string name = store::to_utf8(target.filename());
            if (namesOnly) {
                text = name + "\r\n";
            } else {
                std::error_code ec2;
                uint64_t size = fs::is_regular_file(target, ec2) ? (uint64_t)fs::file_size(target, ec2) : 0;
                auto wt = fs::last_write_time(target, ec2);
                char line[640];
                std::snprintf(line, sizeof line, "-rw-r--r-- 1 ftp ftp %12llu %s %s\r\n",
                              (unsigned long long)size,
                              fmt_time(ec2 ? now_sec() : store::to_epoch(wt)).c_str(), name.c_str());
                text += line;
            }
        }
    }

    if (!reply(s, "150 Открываю соединение для передачи данных")) return;
    socket_t d = accept_data(s);
    if (d == kInvalidSocket) {
        reply(s, "425 Соединение данных не установлено");
        return;
    }
    bool ok = text.empty() ? true : net::send_all(d, text.data(), text.size());
    net::close_socket(d);
    reply(s, ok ? "226 Передача завершена" : "426 Соединение прервано");
}

static void cmd_retr(Session& s, const std::string& arg) {
    std::string share, rel;
    resolve_ftp(s, arg, share, rel);
    fs::path target;
    std::string err;
    if (share.empty() || !store::resolve(share, rel, target, err)) {
        reply(s, "550 " + err);
        return;
    }
    std::error_code ec;
    if (!fs::is_regular_file(target, ec)) {
        reply(s, "550 Файл не найден");
        return;
    }
    std::ifstream f(target, std::ios::binary);
    if (!f) {
        reply(s, "550 Не удалось открыть файл");
        return;
    }
    uint64_t size = (uint64_t)fs::file_size(target, ec);
    if (s.rest > 0 && s.rest < size) f.seekg((std::streamoff)s.rest);
    else s.rest = 0;

    if (!reply(s, "150 Открываю соединение для передачи данных")) return;
    socket_t d = accept_data(s);
    if (d == kInvalidSocket) {
        reply(s, "425 Соединение данных не установлено");
        return;
    }
    uint64_t txId = tx_begin(TxDir::Ftp, share + "/" + rel, "ftp:" + s.ip, size);
    std::vector<char> buf(64 * 1024);
    uint64_t sent = 0;
    bool ok = true;
    while (f) {
        f.read(buf.data(), (std::streamsize)buf.size());
        std::streamsize got = f.gcount();
        if (got <= 0) break;
        if (!net::send_all(d, buf.data(), (size_t)got)) {
            ok = false;
            break;
        }
        sent += (uint64_t)got;
        if (sent % (1024 * 1024) < buf.size()) tx_update(txId, sent);
    }
    net::close_socket(d);
    uint64_t id = txId;
    tx_done(id, ok ? "done" : "error");
    g_app.bytesOut.fetch_add(sent);
    reply(s, ok ? "226 Передача завершена" : "426 Соединение прервано");
}

static void cmd_stor(Session& s, const std::string& arg, bool append) {
    std::string share, rel;
    resolve_ftp(s, arg, share, rel);
    fs::path target;
    std::string err;
    if (share.empty() || !store::resolve(share, rel, target, err)) {
        reply(s, "550 " + err);
        return;
    }
    std::error_code ec;
    fs::create_directories(target.parent_path(), ec);
    fs::path tmp = store::temp_path(target);

    if (!reply(s, "150 Открываю соединение для передачи данных")) return;
    socket_t d = accept_data(s);
    if (d == kInvalidSocket) {
        reply(s, "425 Соединение данных не установлено");
        return;
    }
    std::ios::openmode mode = std::ios::binary | std::ios::trunc;
    if (append) mode = std::ios::binary | std::ios::app;
    std::ofstream f(tmp, mode);
    if (!f) {
        net::close_socket(d);
        reply(s, "550 Не удалось создать файл");
        return;
    }
    uint64_t txId = tx_begin(TxDir::Ftp, share + "/" + rel, "ftp:" + s.ip, 0);
    std::vector<char> buf(64 * 1024);
    uint64_t got = 0;
    bool ok = true;
    for (;;) {
        int n = net::recv_some(d, buf.data(), buf.size());
        if (n < 0) {
            ok = false;
            break;
        }
        if (n == 0) break;
        f.write(buf.data(), (std::streamsize)n);
        if (!f) {
            ok = false;
            break;
        }
        got += (uint64_t)n;
        if (got % (1024 * 1024) < buf.size()) tx_update(txId, got);
    }
    f.close();
    net::close_socket(d);
    if (ok && !append) {
        std::string rerr;
        if (!store::replace_file(tmp, target, rerr)) {
            tx_done(txId, "error: " + rerr);
            reply(s, "550 " + rerr);
            return;
        }
    } else if (ok && append) {
        std::error_code ecr;
        fs::remove(tmp, ecr);
    }
    tx_done(txId, ok ? "done" : "error");
    g_app.bytesIn.fetch_add(got);
    reply(s, ok ? "226 Передача завершена" : "426 Соединение прервано");
}

static void handle(Session& s, const std::string& lineIn) {
    std::string line = lineIn;
    // Некоторые клиенты присылают команду с BOM — убираем его.
    if (line.size() >= 3 && (unsigned char)line[0] == 0xEF && (unsigned char)line[1] == 0xBB &&
        (unsigned char)line[2] == 0xBF) {
        line = line.substr(3);
    }
    if (line.empty()) {
        reply(s, "500 Пустая команда");
        return;
    }
    size_t sp = line.find(' ');
    std::string cmd = upper(line.substr(0, sp));
    std::string arg = (sp == std::string::npos) ? std::string() : line.substr(sp + 1);
    if (!arg.empty() && arg[0] == '-') arg.clear();  // опции LIST игнорируем

    if (!s.logged && cmd != "USER" && cmd != "PASS" && cmd != "QUIT" && cmd != "FEAT" &&
        cmd != "SYST" && cmd != "NOOP" && cmd != "AUTH" && cmd != "OPTS") {
        reply(s, "530 Сначала выполните вход (USER anonymous)");
        return;
    }

    if (cmd == "USER") {
        s.user = arg;
        reply(s, "331 Пароль не требуется, отправьте любой");
        return;
    }
    if (cmd == "PASS") {
        s.logged = true;
        reply(s, "230 Вход выполнен");
        return;
    }
    if (cmd == "QUIT") {
        reply(s, "221 До свидания");
        s.quit = true;
        return;
    }
    if (cmd == "NOOP") {
        reply(s, "200 OK");
        return;
    }
    if (cmd == "SYST") {
        reply(s, "215 UNIX Type: L8");
        return;
    }
    if (cmd == "FEAT") {
        reply(s, "211-Features:");
        reply(s, " UTF8");
        reply(s, " PASV");
        reply(s, " SIZE");
        reply(s, " MDTM");
        reply(s, " REST STREAM");
        reply(s, "211 End");
        return;
    }
    if (cmd == "OPTS") {
        reply(s, "200 OK");
        return;
    }
    if (cmd == "AUTH") {
        reply(s, "502 TLS не поддерживается");
        return;
    }
    if (cmd == "TYPE") {
        s.binary = arg.empty() || upper(arg)[0] != 'A';
        reply(s, "200 Режим передачи изменён");
        return;
    }
    if (cmd == "PWD" || cmd == "XPWD") {
        reply(s, "257 \"" + ftp_path(s) + "\"");
        return;
    }
    if (cmd == "CWD" || cmd == "XCWD") {
        std::string share, rel;
        resolve_ftp(s, arg, share, rel);
        if (share.empty()) {
            s.share.clear();
            s.cwd.clear();
            reply(s, "250 Переход выполнен");
            return;
        }
        if (!store::has_share(share)) {
            reply(s, "550 Папка не найдена");
            return;
        }
        fs::path target;
        std::string err;
        if (!store::resolve(share, rel, target, err)) {
            reply(s, "550 " + err);
            return;
        }
        std::error_code ec;
        if (!fs::is_directory(target, ec)) {
            reply(s, "550 Папка не найдена");
            return;
        }
        s.share = share;
        s.cwd = rel;
        reply(s, "250 Переход выполнен");
        return;
    }
    if (cmd == "CDUP" || cmd == "XCUP") {
        if (!s.cwd.empty()) {
            s.cwd = parent_rel(s.cwd);
        } else {
            s.share.clear();
            s.cwd.clear();
        }
        reply(s, "250 Переход выполнен");
        return;
    }
    if (cmd == "MKD" || cmd == "XMKD") {
        std::string share, rel;
        resolve_ftp(s, arg, share, rel);
        if (share.empty()) {
            reply(s, "550 Верхний уровень: сначала выберите сетевую папку");
            return;
        }
        fs::path target;
        std::string err;
        if (!store::resolve(share, rel, target, err)) {
            reply(s, "550 " + err);
            return;
        }
        std::error_code ec;
        fs::create_directories(target, ec);
        if (ec) {
            reply(s, "550 Не удалось создать папку");
            return;
        }
        reply(s, "257 \"/" + share + "/" + rel + "\" создана");
        return;
    }
    if (cmd == "RMD" || cmd == "XRMD") {
        std::string share, rel;
        resolve_ftp(s, arg, share, rel);
        if (share.empty() || rel.empty()) {
            reply(s, "550 Верхний уровень: нельзя удалить саму сетевую папку");
            return;
        }
        fs::path target;
        std::string err;
        if (!store::resolve(share, rel, target, err)) {
            reply(s, "550 " + err);
            return;
        }
        std::error_code ec;
        if (!fs::remove(target, ec) || ec) {
            reply(s, "550 Не удалось удалить папку");
            return;
        }
        reply(s, "250 Папка удалена");
        return;
    }
    if (cmd == "DELE") {
        std::string share, rel;
        resolve_ftp(s, arg, share, rel);
        if (share.empty() || rel.empty()) {
            reply(s, "550 Верхний уровень: нельзя удалить саму сетевую папку");
            return;
        }
        std::string derr;
        if (!store::remove_entry(share, rel, derr)) {
            reply(s, "550 " + derr);
            return;
        }
        reply(s, "250 Файл удалён");
        return;
    }
    if (cmd == "SIZE") {
        std::string share, rel;
        resolve_ftp(s, arg, share, rel);
        fs::path target;
        std::string err;
        if (share.empty() || !store::resolve(share, rel, target, err)) {
            reply(s, "550 " + err);
            return;
        }
        std::error_code ec;
        if (!fs::is_regular_file(target, ec)) {
            reply(s, "550 Не файл");
            return;
        }
        reply(s, "213 " + std::to_string((unsigned long long)fs::file_size(target, ec)));
        return;
    }
    if (cmd == "MDTM") {
        std::string share, rel;
        resolve_ftp(s, arg, share, rel);
        fs::path target;
        std::string err;
        if (share.empty() || !store::resolve(share, rel, target, err)) {
            reply(s, "550 " + err);
            return;
        }
        std::error_code ec;
        auto wt = fs::last_write_time(target, ec);
        if (ec) {
            reply(s, "550 Нет времени изменения");
            return;
        }
        reply(s, "213 " + fmt_mdtm(store::to_epoch(wt)));
        return;
    }
    if (cmd == "REST") {
        try {
            s.rest = (uint64_t)std::stoull(arg);
        } catch (...) {
            s.rest = 0;
        }
        reply(s, "350 Позиция принята");
        return;
    }
    if (cmd == "PASV" || cmd == "EPSV") {
        cmd_pasv(s);
        return;
    }
    if (cmd == "PORT" || cmd == "EPRT") {
        reply(s, "502 Активный режим не поддерживается, используйте PASV");
        return;
    }
    if (cmd == "LIST") {
        cmd_list(s, arg, false);
        return;
    }
    if (cmd == "NLST") {
        cmd_list(s, arg, true);
        return;
    }
    if (cmd == "RETR") {
        s.rest = 0;
        cmd_retr(s, arg);
        return;
    }
    if (cmd == "STOR" || cmd == "APPE") {
        cmd_stor(s, arg, cmd == "APPE");
        return;
    }
    reply(s, "502 Команда не поддерживается");
}

static void session_loop(socket_t ctrl) {
    Session s;
    s.ctrl = ctrl;
    s.ip = net::peer_ip(ctrl);
    net::set_timeouts(ctrl, 120000, 60000);
    reply(s, "220 Обмен FTP готов");
    std::string line;
    while (s.read_line(line)) {
        handle(s, line);
        if (s.quit) break;
    }
    if (s.pasv != kInvalidSocket) net::close_socket(s.pasv);
    net::close_socket(ctrl);
}

static void accept_loop() {
    while (g_run.load()) {
        socket_t c = net::accept_tcp(g_ls);
        if (c == kInvalidSocket) {
            if (!g_run.load()) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            continue;
        }
        std::thread([c]() { session_loop(c); }).detach();
    }
}

bool start(uint16_t port, std::string& err) {
    g_ls = net::listen_tcp(port, err);
    if (g_ls == kInvalidSocket) return false;
    g_run.store(true);
    g_accept = std::thread(accept_loop);
    return true;
}

void stop() {
    if (!g_run.exchange(false)) return;
    if (g_ls != kInvalidSocket) {
        net::close_socket(g_ls);
        g_ls = kInvalidSocket;
    }
    if (g_accept.joinable()) g_accept.join();
}

}  // namespace ftp
