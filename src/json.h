#pragma once
// Минимальная JSON-библиотека (запись + разбор), без внешних зависимостей.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

class Json {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Json() = default;
    Json(std::nullptr_t) {}
    Json(bool v) : t_(Type::Bool), b_(v) {}
    Json(int v) : t_(Type::Number), n_((double)v) {}
    Json(unsigned v) : t_(Type::Number), n_((double)v) {}
    Json(long v) : t_(Type::Number), n_((double)v) {}
    Json(long long v) : t_(Type::Number), n_((double)v) {}
    Json(unsigned long v) : t_(Type::Number), n_((double)v) {}
    Json(unsigned long long v) : t_(Type::Number), n_((double)v) {}
    Json(double v) : t_(Type::Number), n_(v) {}
    Json(const char* s) : t_(Type::String), s_(s ? s : "") {}
    Json(std::string s) : t_(Type::String), s_(std::move(s)) {}

    static Json array() {
        Json j;
        j.t_ = Type::Array;
        return j;
    }
    static Json object() {
        Json j;
        j.t_ = Type::Object;
        return j;
    }

    Type type() const { return t_; }
    bool isNull() const { return t_ == Type::Null; }
    bool isBool() const { return t_ == Type::Bool; }
    bool isNumber() const { return t_ == Type::Number; }
    bool isString() const { return t_ == Type::String; }
    bool isArray() const { return t_ == Type::Array; }
    bool isObject() const { return t_ == Type::Object; }

    bool asBool(bool def = false) const { return t_ == Type::Bool ? b_ : (t_ == Type::Number ? (n_ != 0.0) : def); }
    double asNumber(double def = 0.0) const { return t_ == Type::Number ? n_ : def; }
    long long asInt(long long def = 0) const { return t_ == Type::Number ? (long long)std::llround(n_) : def; }
    std::string asString(const std::string& def = std::string()) const { return t_ == Type::String ? s_ : def; }

    size_t size() const {
        if (t_ == Type::Array) return arr_.size();
        if (t_ == Type::Object) return obj_.size();
        return 0;
    }
    bool empty() const { return size() == 0; }
    const std::vector<Json>& items() const { return arr_; }
    const std::vector<std::pair<std::string, Json>>& fields() const { return obj_; }

    const Json* find(const std::string& key) const {
        if (t_ != Type::Object) return nullptr;
        for (const auto& kv : obj_)
            if (kv.first == key) return &kv.second;
        return nullptr;
    }
    Json get(const std::string& key, const Json& def = Json()) const {
        const Json* p = find(key);
        return p ? *p : def;
    }
    bool has(const std::string& key) const { return find(key) != nullptr; }

    Json& set(const std::string& key, Json value) {
        if (t_ != Type::Object) *this = object();
        for (auto& kv : obj_) {
            if (kv.first == key) {
                kv.second = std::move(value);
                return *this;
            }
        }
        obj_.emplace_back(key, std::move(value));
        return *this;
    }
    Json& operator[](const std::string& key) {
        if (t_ != Type::Object) *this = object();
        for (auto& kv : obj_) {
            if (kv.first == key) return kv.second;
        }
        obj_.emplace_back(key, Json());
        return obj_.back().second;
    }
    Json& push(Json value) {
        if (t_ != Type::Array) *this = array();
        arr_.push_back(std::move(value));
        return *this;
    }

    std::string dump(int indent = 0) const {
        std::string out;
        out.reserve(256);
        dump_to(out, indent, 0);
        return out;
    }

    static bool parse(const std::string& text, Json& out, std::string* err = nullptr) {
        Parser p(text);
        Json v;
        if (!p.value(v)) {
            if (err) *err = p.err.empty() ? "json: ошибка разбора" : p.err;
            return false;
        }
        p.ws();
        if (p.i < text.size()) {
            if (err) *err = "json: лишние символы после значения";
            return false;
        }
        out = std::move(v);
        return true;
    }

private:
    void dump_to(std::string& o, int indent, int depth) const {
        switch (t_) {
        case Type::Null:
            o += "null";
            break;
        case Type::Bool:
            o += b_ ? "true" : "false";
            break;
        case Type::Number: {
            if (std::isfinite(n_)) {
                char buf[48];
                if (std::floor(n_) == n_ && std::fabs(n_) < 1e15) std::snprintf(buf, sizeof buf, "%.0f", n_);
                else std::snprintf(buf, sizeof buf, "%.17g", n_);
                o += buf;
            } else {
                o += "0";
            }
            break;
        }
        case Type::String:
            escape(s_, o);
            break;
        case Type::Array: {
            o += '[';
            for (size_t k = 0; k < arr_.size(); ++k) {
                if (k) o += ',';
                newline(o, indent, depth + 1);
                arr_[k].dump_to(o, indent, depth + 1);
            }
            if (!arr_.empty()) newline(o, indent, depth);
            o += ']';
            break;
        }
        case Type::Object: {
            o += '{';
            for (size_t k = 0; k < obj_.size(); ++k) {
                if (k) o += ',';
                newline(o, indent, depth + 1);
                escape(obj_[k].first, o);
                o += indent > 0 ? ": " : ":";
                obj_[k].second.dump_to(o, indent, depth + 1);
            }
            if (!obj_.empty()) newline(o, indent, depth);
            o += '}';
            break;
        }
        }
    }
    static void newline(std::string& o, int indent, int depth) {
        if (indent <= 0) return;
        o += '\n';
        o.append((size_t)(indent * depth), ' ');
    }
    static void escape(const std::string& s, std::string& o) {
        o += '"';
        for (unsigned char c : s) {
            switch (c) {
            case '"': o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n"; break;
            case '\r': o += "\\r"; break;
            case '\t': o += "\\t"; break;
            case '\b': o += "\\b"; break;
            case '\f': o += "\\f"; break;
            default:
                if (c < 0x20) {
                    char b[8];
                    std::snprintf(b, sizeof b, "\\u%04x", (unsigned)c);
                    o += b;
                } else {
                    o += (char)c;
                }
            }
        }
        o += '"';
    }

    struct Parser {
        const std::string& s;
        size_t i = 0;
        std::string err;
        explicit Parser(const std::string& str) : s(str) {}

        void ws() {
            while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) ++i;
        }
        bool lit(const char* t) {
            size_t n = std::char_traits<char>::length(t);
            if (s.compare(i, n, t) != 0) return false;
            i += n;
            return true;
        }
        bool value(Json& out) {
            ws();
            if (i >= s.size()) {
                err = "json: неожиданный конец";
                return false;
            }
            char c = s[i];
            if (c == '{') return object(out);
            if (c == '[') return array(out);
            if (c == '"') {
                std::string v;
                if (!string(v)) return false;
                out = Json(std::move(v));
                return true;
            }
            if (c == 't') {
                if (!lit("true")) return bad("json: ожидалось true");
                out = Json(true);
                return true;
            }
            if (c == 'f') {
                if (!lit("false")) return bad("json: ожидалось false");
                out = Json(false);
                return true;
            }
            if (c == 'n') {
                if (!lit("null")) return bad("json: ожидалось null");
                out = Json();
                return true;
            }
            return number(out);
        }
        bool bad(const char* m) {
            err = m;
            return false;
        }
        bool number(Json& out) {
            size_t start = i;
            if (i < s.size() && (s[i] == '-' || s[i] == '+')) ++i;
            bool digits = false;
            while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
                ++i;
                digits = true;
            }
            if (i < s.size() && s[i] == '.') {
                ++i;
                while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
                    ++i;
                    digits = true;
                }
            }
            if (!digits) return bad("json: плохое число");
            if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
                ++i;
                if (i < s.size() && (s[i] == '-' || s[i] == '+')) ++i;
                while (i < s.size() && s[i] >= '0' && s[i] <= '9') ++i;
            }
            out = Json(std::strtod(s.substr(start, i - start).c_str(), nullptr));
            return true;
        }
        bool string(std::string& out) {
            if (i >= s.size() || s[i] != '"') return bad("json: ожидалась строка");
            ++i;
            out.clear();
            while (i < s.size()) {
                char c = s[i++];
                if (c == '"') return true;
                if (c != '\\') {
                    out += c;
                    continue;
                }
                if (i >= s.size()) break;
                char e = s[i++];
                switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    unsigned cp = 0;
                    if (!hex4(cp)) return false;
                    if (cp >= 0xD800 && cp <= 0xDBFF && i + 6 <= s.size() && s[i] == '\\' && s[i + 1] == 'u') {
                        i += 2;
                        unsigned lo = 0;
                        if (!hex4(lo)) return false;
                        cp = 0x10000u + ((cp - 0xD800u) << 10) + (lo - 0xDC00u);
                    }
                    append_utf8(out, cp);
                    break;
                }
                default: return bad("json: плохая escape-последовательность");
                }
            }
            return bad("json: незакрытая строка");
        }
        bool hex4(unsigned& cp) {
            if (i + 4 > s.size()) return bad("json: плохой \\u");
            cp = 0;
            for (int k = 0; k < 4; ++k) {
                char h = s[i++];
                cp <<= 4;
                if (h >= '0' && h <= '9') cp |= (unsigned)(h - '0');
                else if (h >= 'a' && h <= 'f') cp |= (unsigned)(h - 'a' + 10);
                else if (h >= 'A' && h <= 'F') cp |= (unsigned)(h - 'A' + 10);
                else return bad("json: плохая hex-цифра");
            }
            return true;
        }
        static void append_utf8(std::string& o, unsigned cp) {
            if (cp < 0x80) {
                o += (char)cp;
            } else if (cp < 0x800) {
                o += (char)(0xC0 | (cp >> 6));
                o += (char)(0x80 | (cp & 0x3F));
            } else if (cp < 0x10000) {
                o += (char)(0xE0 | (cp >> 12));
                o += (char)(0x80 | ((cp >> 6) & 0x3F));
                o += (char)(0x80 | (cp & 0x3F));
            } else {
                o += (char)(0xF0 | (cp >> 18));
                o += (char)(0x80 | ((cp >> 12) & 0x3F));
                o += (char)(0x80 | ((cp >> 6) & 0x3F));
                o += (char)(0x80 | (cp & 0x3F));
            }
        }
        bool array(Json& out) {
            ++i;
            out = Json::array();
            ws();
            if (i < s.size() && s[i] == ']') {
                ++i;
                return true;
            }
            for (;;) {
                Json v;
                if (!value(v)) return false;
                out.push(std::move(v));
                ws();
                if (i < s.size() && s[i] == ',') {
                    ++i;
                    continue;
                }
                if (i < s.size() && s[i] == ']') {
                    ++i;
                    return true;
                }
                return bad("json: ожидалось , или ]");
            }
        }
        bool object(Json& out) {
            ++i;
            out = Json::object();
            ws();
            if (i < s.size() && s[i] == '}') {
                ++i;
                return true;
            }
            for (;;) {
                ws();
                if (i >= s.size() || s[i] != '"') return bad("json: ожидался ключ");
                std::string key;
                if (!string(key)) return false;
                ws();
                if (i >= s.size() || s[i] != ':') return bad("json: ожидалось :");
                ++i;
                Json v;
                if (!value(v)) return false;
                out.set(key, std::move(v));
                ws();
                if (i < s.size() && s[i] == ',') {
                    ++i;
                    continue;
                }
                if (i < s.size() && s[i] == '}') {
                    ++i;
                    return true;
                }
                return bad("json: ожидалось , или }");
            }
        }
    };

    Type t_ = Type::Null;
    bool b_ = false;
    double n_ = 0.0;
    std::string s_;
    std::vector<Json> arr_;
    std::vector<std::pair<std::string, Json>> obj_;
};
