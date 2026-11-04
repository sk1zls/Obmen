#include "http.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <mutex>
#include <thread>

#include "app.h"

namespace http {

// ---------------------------------------------------------------- Conn

void Conn::compact() {
    if (pos > 65536 && pos * 2 > buf.size()) {
        buf.erase(0, pos);
        pos = 0;
    }
}

bool Conn::fill() {
    if (closed) return false;
    char tmp[16384];
    int n = net::recv_some(s, tmp, sizeof tmp);
    if (n <= 0) {
        closed = true;
        return false;
    }
    buf.append(tmp, (size_t)n);
    return true;
}

bool Conn::read_line(std::string& line) {
    for (;;) {
        size_t idx = buf.find('\n', pos);
        if (idx != std::string::npos) {
            size_t end = idx;
            if (end > pos && buf[end - 1] == '\r') --end;
            line.assign(buf, pos, end - pos);
            pos = idx + 1;
            compact();
            return true;
        }
        if (buf.size() - pos > 65536) return false;
        if (!fill()) return false;
    }
}

bool Conn::read_bytes(char* dst, size_t n) {
    size_t got = 0;
    while (got < n) {
        if (pos < buf.size()) {
            size_t take = std::min(n - got, buf.size() - pos);
            std::memcpy(dst + got, buf.data() + pos, take);
            pos += take;
            got += take;
            compact();
            continue;
        }
        buf.clear();
        pos = 0;
        int r = net::recv_some(s, dst + got, n - got);
        if (r <= 0) {
            closed = true;
            return false;
        }
        got += (size_t)r;
    }
    return true;
}

bool Conn::send_data(const void* d, size_t n) { return net::send_all(s, d, n); }

// ---------------------------------------------------------------- helpers

static std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return s;
}

static std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r')) --b;
    return s.substr(a, b - a);
}

static int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

std::string url_decode(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (c == '%' && i + 2 < s.size()) {
            int h = hexval(s[i + 1]), l = hexval(s[i + 2]);
            if (h >= 0 && l >= 0) {
                out += (char)((h << 4) | l);
                i += 2;
                continue;
            }
        }
        out += c;
    }
    return out;
}

std::string url_encode(const std::string& s) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    out.reserve(s.size() * 3);
    for (unsigned char c : s) {
        bool safe = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                    c == '-' || c == '_' || c == '.' || c == '~' || c == '/';
        if (safe) {
            out += (char)c;
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 0xF];
        }
    }
    return out;
}

std::string mime_type(const std::string& name) {
    size_t dot = name.find_last_of('.');
    if (dot == std::string::npos) return "application/octet-stream";
    std::string ext = lower(name.substr(dot + 1));
    if (ext == "html" || ext == "htm") return "text/html; charset=utf-8";
    if (ext == "css") return "text/css; charset=utf-8";
    if (ext == "js") return "application/javascript; charset=utf-8";
    if (ext == "json") return "application/json; charset=utf-8";
    if (ext == "png") return "image/png";
    if (ext == "jpg" || ext == "jpeg") return "image/jpeg";
    if (ext == "gif") return "image/gif";
    if (ext == "svg") return "image/svg+xml";
    if (ext == "ico") return "image/x-icon";
    if (ext == "txt" || ext == "log" || ext == "md") return "text/plain; charset=utf-8";
    if (ext == "pdf") return "application/pdf";
    if (ext == "mp4") return "video/mp4";
    if (ext == "mp3") return "audio/mpeg";
    if (ext == "zip") return "application/zip";
    return "application/octet-stream";
}

void Response::fail(int code, const std::string& msg) {
    status = code;
    contentType = "application/json; charset=utf-8";
    Json j = Json::object();
    j.set("ok", false);
    j.set("error", msg);
    body = j.dump();
}

std::string Request::str(const std::string& key, const std::string& def) const {
    auto it = params.find(key);
    return it == params.end() ? def : it->second;
}

int64_t Request::num(const std::string& key, int64_t def) const {
    auto it = params.find(key);
    if (it == params.end()) return def;
    try {
        return (int64_t)std::stoll(it->second);
    } catch (...) {
        return def;
    }
}

bool Request::flag(const std::string& key) const {
    auto it = params.find(key);
    if (it == params.end()) return false;
    const std::string& v = it->second;
    return v == "1" || v == "true" || v == "yes" || v == "on";
}

std::string Request::header(const std::string& key, const std::string& def) const {
    auto it = headers.find(lower(key));
    return it == headers.end() ? def : it->second;
}

// ---------------------------------------------------------------- router

static std::map<std::string, Handler> g_routes;
static std::mutex g_routesMtx;

void route(const std::string& method, const std::string& path, Handler h) {
    std::lock_guard<std::mutex> lk(g_routesMtx);
    g_routes[method + " " + path] = std::move(h);
}

void dispatch(Request& req, Response& res) {
    Handler h;
    {
        std::lock_guard<std::mutex> lk(g_routesMtx);
        auto it = g_routes.find(req.method + " " + req.path);
        if (it != g_routes.end()) h = it->second;
    }
    if (!h) {
        res.fail(404, "не найдено: " + req.path);
        return;
    }
    try {
        h(req, res);
    } catch (const std::exception& e) {
        res.fail(500, std::string("внутренняя ошибка: ") + e.what());
    } catch (...) {
        res.fail(500, "внутренняя ошибка");
    }
}

// ---------------------------------------------------------------- server

static socket_t g_ls = kInvalidSocket;
static std::atomic<bool> g_running{false};
static std::thread g_acceptThread;
static std::atomic<int> g_conns{0};
static const int kMaxConns = 96;

static const char* reason(int code) {
    switch (code) {
    case 200: return "OK";
    case 201: return "Created";
    case 204: return "No Content";
    case 206: return "Partial Content";
    case 301: return "Moved Permanently";
    case 302: return "Found";
    case 400: return "Bad Request";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 409: return "Conflict";
    case 411: return "Length Required";
    case 413: return "Payload Too Large";
    case 416: return "Range Not Satisfiable";
    case 500: return "Internal Server Error";
    case 501: return "Not Implemented";
    default: return "OK";
    }
}

static bool write_response(Conn& c, Response& res, bool headOnly) {
    std::string head;
    head.reserve(256);
    head += "HTTP/1.1 ";
    head += std::to_string(res.status);
    head += " ";
    head += reason(res.status);
    head += "\r\n";
    head += "Content-Type: ";
    head += res.contentType;
    head += "\r\n";
    uint64_t len = res.stream ? res.streamLength : (uint64_t)res.body.size();
    head += "Content-Length: " + std::to_string(len) + "\r\n";
    head += "Connection: close\r\n";
    head += "Cache-Control: no-store\r\n";
    head += "X-Content-Type-Options: nosniff\r\n";
    for (const auto& h : res.headers) {
        head += h.first;
        head += ": ";
        head += h.second;
        head += "\r\n";
    }
    head += "\r\n";
    if (!c.send_data(head.data(), head.size())) return false;
    if (headOnly) return true;
    if (res.stream) return res.streamer ? res.streamer(c) : false;
    if (!res.body.empty()) return c.send_data(res.body.data(), res.body.size());
    return true;
}

static void serve_conn(socket_t s) {
    net::set_timeouts(s, 30000, 30000);
    Conn conn;
    conn.s = s;

    std::string line;
    if (!conn.read_line(line)) {
        net::close_socket(s);
        return;
    }
    Request req;
    req.conn = &conn;
    req.remoteIp = net::peer_ip(s);

    // "METHOD target HTTP/1.1"
    size_t sp1 = line.find(' ');
    if (sp1 == std::string::npos) {
        net::close_socket(s);
        return;
    }
    req.method = line.substr(0, sp1);
    size_t sp2 = line.find(' ', sp1 + 1);
    if (sp2 == std::string::npos) sp2 = line.size();
    req.target = line.substr(sp1 + 1, sp2 - sp1 - 1);

    size_t q = req.target.find('?');
    if (q == std::string::npos) {
        req.path = req.target;
    } else {
        req.path = req.target.substr(0, q);
        req.query = req.target.substr(q + 1);
        size_t i = 0;
        while (i < req.query.size()) {
            size_t amp = req.query.find('&', i);
            if (amp == std::string::npos) amp = req.query.size();
            std::string pair = req.query.substr(i, amp - i);
            size_t eq = pair.find('=');
            std::string k = eq == std::string::npos ? pair : pair.substr(0, eq);
            std::string v = eq == std::string::npos ? std::string() : pair.substr(eq + 1);
            if (!k.empty()) req.params[url_decode(k)] = url_decode(v);
            i = amp + 1;
        }
    }
    req.path = url_decode(req.path);

    // Заголовки
    size_t headerBytes = 0;
    for (;;) {
        if (!conn.read_line(line)) {
            net::close_socket(s);
            return;
        }
        if (line.empty()) break;
        headerBytes += line.size();
        if (headerBytes > 65536) {
            net::close_socket(s);
            return;
        }
        size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string k = lower(trim(line.substr(0, colon)));
        std::string v = trim(line.substr(colon + 1));
        req.headers[k] = v;
    }
    auto cl = req.headers.find("content-length");
    if (cl != req.headers.end()) {
        try {
            req.contentLength = (uint64_t)std::stoull(cl->second);
        } catch (...) {
            req.contentLength = 0;
        }
    }

    Response res;
    dispatch(req, res);
    bool headOnly = (req.method == "HEAD");
    write_response(conn, res, headOnly);
    net::close_socket(s);
}

static void accept_loop() {
    while (g_running.load()) {
        socket_t c = net::accept_tcp(g_ls);
        if (c == kInvalidSocket) {
            if (!g_running.load()) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            continue;
        }
        if (g_conns.load() >= kMaxConns) {
            net::close_socket(c);
            continue;
        }
        g_conns.fetch_add(1);
        std::thread([c]() {
            serve_conn(c);
            g_conns.fetch_sub(1);
        }).detach();
    }
}

bool listen(uint16_t port, std::string& err) {
    std::string e;
    g_ls = net::listen_tcp(port, e);
    if (g_ls == kInvalidSocket) {
        err = e;
        return false;
    }
    g_running.store(true);
    g_acceptThread = std::thread(accept_loop);
    return true;
}

void stop() {
    if (!g_running.exchange(false)) return;
    if (g_ls != kInvalidSocket) {
        net::close_socket(g_ls);
        g_ls = kInvalidSocket;
    }
    if (g_acceptThread.joinable()) g_acceptThread.join();
}

// ---------------------------------------------------------------- client

static std::string build_request(const std::string& method, const std::string& hostHeader,
                                 const std::string& target, const std::string& body) {
    std::string r;
    r.reserve(256 + body.size());
    r += method;
    r += " ";
    r += target;
    r += " HTTP/1.1\r\nHost: ";
    r += hostHeader;
    r += "\r\nUser-Agent: obmen/1.0\r\nAccept: */*\r\nConnection: close\r\n";
    if (!body.empty() || method == "POST") {
        r += "Content-Length: " + std::to_string(body.size()) + "\r\n";
    }
    r += "\r\n";
    r += body;
    return r;
}

static bool read_head(Conn& c, int& status, std::map<std::string, std::string>& hdrs, std::string& err) {
    std::string line;
    if (!c.read_line(line)) {
        err = "нет ответа от узла";
        return false;
    }
    size_t sp = line.find(' ');
    if (sp == std::string::npos) {
        err = "плохой ответ";
        return false;
    }
    status = std::atoi(line.c_str() + sp + 1);
    for (;;) {
        if (!c.read_line(line)) {
            err = "соединение оборвано";
            return false;
        }
        if (line.empty()) break;
        size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        hdrs[lower(trim(line.substr(0, colon)))] = trim(line.substr(colon + 1));
    }
    return true;
}

static bool open_client(const std::string& host, uint16_t port, int timeoutMs, Conn& c, std::string& err) {
    socket_t s = kInvalidSocket;
    if (!net::connect_tcp(host, port, s, err, timeoutMs)) return false;
    net::set_timeouts(s, timeoutMs, timeoutMs);
    c.s = s;
    c.buf.clear();
    c.pos = 0;
    c.closed = false;
    return true;
}

bool get(const std::string& host, uint16_t port, const std::string& target, int timeoutMs, int& status,
         std::string& body, std::string& err) {
    status = 0;
    Conn c;
    if (!open_client(host, port, timeoutMs, c, err)) return false;
    std::string req = build_request("GET", host + ":" + std::to_string((unsigned)port), target, "");
    if (!c.send_data(req.data(), req.size())) {
        net::close_socket(c.s);
        err = "не удалось отправить запрос";
        return false;
    }
    std::map<std::string, std::string> hdrs;
    if (!read_head(c, status, hdrs, err)) {
        net::close_socket(c.s);
        return false;
    }
    body.clear();
    auto it = hdrs.find("content-length");
    if (it != hdrs.end()) {
        uint64_t len = 0;
        try {
            len = (uint64_t)std::stoull(it->second);
        } catch (...) {
            len = 0;
        }
        if (len > 0) {
            body.resize((size_t)len);
            if (!c.read_bytes(&body[0], (size_t)len)) {
                net::close_socket(c.s);
                err = "тело ответа не дочитано";
                return false;
            }
        }
    } else {
        char tmp[8192];
        for (;;) {
            int n = net::recv_some(c.s, tmp, sizeof tmp);
            if (n <= 0) break;
            body.append(tmp, (size_t)n);
        }
    }
    net::close_socket(c.s);
    return true;
}

bool download(const std::string& host, uint16_t port, const std::string& target,
              const std::filesystem::path& dest, int timeoutMs, std::string& err,
              std::function<void(uint64_t, uint64_t)> progress) {
    Conn c;
    if (!open_client(host, port, timeoutMs, c, err)) return false;
    std::string req = build_request("GET", host + ":" + std::to_string((unsigned)port), target, "");
    if (!c.send_data(req.data(), req.size())) {
        net::close_socket(c.s);
        err = "не удалось отправить запрос";
        return false;
    }
    int status = 0;
    std::map<std::string, std::string> hdrs;
    if (!read_head(c, status, hdrs, err)) {
        net::close_socket(c.s);
        return false;
    }
    if (status != 200) {
        net::close_socket(c.s);
        err = "узел вернул код " + std::to_string(status);
        return false;
    }
    uint64_t total = 0;
    auto it = hdrs.find("content-length");
    bool hasLen = (it != hdrs.end());
    if (hasLen) {
        try {
            total = (uint64_t)std::stoull(it->second);
        } catch (...) {
            total = 0;
        }
    }
    std::ofstream f(dest, std::ios::binary | std::ios::trunc);
    if (!f) {
        net::close_socket(c.s);
        err = "не удалось создать файл";
        return false;
    }
    const size_t kBuf = 256 * 1024;
    std::vector<char> buf(kBuf);
    uint64_t got = 0;
    bool ok = true;
    for (;;) {
        if (hasLen && got >= total) break;  // в том числе файл нулевой длины
        size_t want = kBuf;
        if (hasLen && total - got < want) want = (size_t)(total - got);
        if (!c.read_bytes(buf.data(), want)) {
            ok = false;
            break;
        }
        f.write(buf.data(), (std::streamsize)want);
        got += want;
        if (progress) progress(got, total);
    }
    f.close();
    net::close_socket(c.s);
    if (!ok || (hasLen && got != total)) {
        err = "передача прервана";
        return false;
    }
    return true;
}

bool upload(const std::string& host, uint16_t port, const std::string& target,
            const std::filesystem::path& src, uint64_t size, int timeoutMs, std::string& err,
            std::function<void(uint64_t, uint64_t)> progress) {
    Conn c;
    if (!open_client(host, port, timeoutMs, c, err)) return false;
    std::error_code ecs;
    if (!std::filesystem::is_regular_file(src, ecs) || (uint64_t)std::filesystem::file_size(src, ecs) != size ||
        ecs) {
        net::close_socket(c.s);
        err = "файл изменился перед отправкой";
        return false;
    }
    std::ifstream f(src, std::ios::binary);
    if (!f) {
        net::close_socket(c.s);
        err = "не удалось открыть файл";
        return false;
    }
    std::string req = "POST " + target + " HTTP/1.1\r\nHost: " + host + ":" +
                      std::to_string((unsigned)port) +
                      "\r\nUser-Agent: obmen/1.0\r\nAccept: */*\r\n"
                      "Content-Type: application/octet-stream\r\nConnection: close\r\nContent-Length: " +
                      std::to_string(size) + "\r\n\r\n";
    if (!c.send_data(req.data(), req.size())) {
        net::close_socket(c.s);
        err = "не удалось отправить запрос";
        return false;
    }
    const size_t kBuf = 256 * 1024;
    std::vector<char> buf(kBuf);
    uint64_t sent = 0;
    while (sent < size) {
        size_t want = (size_t)std::min<uint64_t>(kBuf, size - sent);
        f.read(buf.data(), (std::streamsize)want);
        std::streamsize got = f.gcount();
        if (got <= 0) {
            net::close_socket(c.s);
            err = "файл изменился во время отправки";
            return false;
        }
        if (!c.send_data(buf.data(), (size_t)got)) {
            net::close_socket(c.s);
            err = "соединение прервано";
            return false;
        }
        sent += (uint64_t)got;
        if (progress) progress(sent, size);
    }
    int status = 0;
    std::map<std::string, std::string> hdrs;
    if (!read_head(c, status, hdrs, err)) {
        net::close_socket(c.s);
        return false;
    }
    net::close_socket(c.s);
    if (status < 200 || status >= 300) {
        err = "узел вернул код " + std::to_string(status);
        return false;
    }
    return true;
}

bool post(const std::string& host, uint16_t port, const std::string& target, const std::string& body,
          int timeoutMs, int& status, std::string& resp, std::string& err) {
    status = 0;
    Conn c;
    if (!open_client(host, port, timeoutMs, c, err)) return false;
    std::string req = build_request("POST", host + ":" + std::to_string((unsigned)port), target, body);
    if (!c.send_data(req.data(), req.size())) {
        net::close_socket(c.s);
        err = "не удалось отправить запрос";
        return false;
    }
    std::map<std::string, std::string> hdrs;
    if (!read_head(c, status, hdrs, err)) {
        net::close_socket(c.s);
        return false;
    }
    resp.clear();
    auto it = hdrs.find("content-length");
    if (it != hdrs.end()) {
        uint64_t len = 0;
        try {
            len = (uint64_t)std::stoull(it->second);
        } catch (...) {
            len = 0;
        }
        if (len > 0) {
            resp.resize((size_t)len);
            if (!c.read_bytes(&resp[0], (size_t)len)) {
                net::close_socket(c.s);
                err = "тело ответа не дочитано";
                return false;
            }
        }
    }
    net::close_socket(c.s);
    return true;
}

}  // namespace http
