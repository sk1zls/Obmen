#pragma once
// Минимальный HTTP/1.1 сервер (одно соединение = один запрос) и клиент.
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "json.h"
#include "net.h"

namespace http {

struct Conn {
    socket_t s = kInvalidSocket;
    std::string buf;
    size_t pos = 0;
    bool closed = false;

    bool fill();
    bool read_line(std::string& line);
    bool read_bytes(char* dst, size_t n);
    bool send_data(const void* d, size_t n);
    void compact();
};

struct Request {
    std::string method;
    std::string target;
    std::string path;
    std::string query;
    std::map<std::string, std::string> params;
    std::map<std::string, std::string> headers;
    std::string remoteIp;
    uint64_t contentLength = 0;
    Conn* conn = nullptr;

    std::string str(const std::string& key, const std::string& def = "") const;
    int64_t num(const std::string& key, int64_t def = 0) const;
    bool flag(const std::string& key) const;
    std::string header(const std::string& key, const std::string& def = "") const;
};

struct Response {
    int status = 200;
    std::string contentType = "application/json; charset=utf-8";
    std::vector<std::pair<std::string, std::string>> headers;
    std::string body;
    bool stream = false;
    uint64_t streamLength = 0;
    std::function<bool(Conn&)> streamer;

    void json(const std::string& s) {
        contentType = "application/json; charset=utf-8";
        body = s;
    }
    void text(const std::string& s, int code = 200) {
        status = code;
        contentType = "text/plain; charset=utf-8";
        body = s;
    }
    void html(const std::string& s) {
        contentType = "text/html; charset=utf-8";
        body = s;
    }
    void fail(int code, const std::string& msg);
    void add(const std::string& k, const std::string& v) { headers.emplace_back(k, v); }
};

using Handler = std::function<void(Request&, Response&)>;

void route(const std::string& method, const std::string& path, Handler h);
void dispatch(Request& req, Response& res);

bool listen(uint16_t port, std::string& err);
void stop();

std::string url_decode(const std::string& s);
std::string url_encode(const std::string& s);
std::string mime_type(const std::string& name);

// ---- клиент ----
bool get(const std::string& host, uint16_t port, const std::string& target, int timeoutMs, int& status,
         std::string& body, std::string& err);
bool download(const std::string& host, uint16_t port, const std::string& target,
              const std::filesystem::path& dest, int timeoutMs, std::string& err,
              std::function<void(uint64_t done, uint64_t total)> progress = {});
// Отправляет файл целиком (POST, сырое тело) — обратная операция к download().
// Нужна синхронизации, чтобы узел сам выкладывал свои папки соседям.
bool upload(const std::string& host, uint16_t port, const std::string& target,
            const std::filesystem::path& src, uint64_t size, int timeoutMs, std::string& err,
            std::function<void(uint64_t done, uint64_t total)> progress = {});
bool post(const std::string& host, uint16_t port, const std::string& target, const std::string& body,
          int timeoutMs, int& status, std::string& resp, std::string& err);

}  // namespace http
