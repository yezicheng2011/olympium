/*
 * Copyright 2026 Development Desktop contributors
 * SPDX-License-Identifier: Apache-2.0 — Licensed under the Apache License, Version 2.0. See LICENSE.
 *
 * Shared C++20 runtime utilities for Olympium system tools, Android runtime,
 * and OpenHarmony runtime (JSON, SHA-256/SHA-1, ZIP/TAR, subprocess, flock).
 */
#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <map>
#include <optional>
#include <set>
#include <signal.h>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

#include <openssl/evp.h>
#include <zlib.h>

namespace olympium {

namespace fs = std::filesystem;

inline std::string trim(std::string_view s) {
    size_t a = 0;
    while (a < s.size() && std::isspace(static_cast<unsigned char>(s[a]))) a++;
    size_t b = s.size();
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) b--;
    return std::string(s.substr(a, b - a));
}

inline std::vector<std::string> split_lines(std::string_view s) {
    std::vector<std::string> out;
    size_t start = 0;
    while (start <= s.size()) {
        size_t nl = s.find('\n', start);
        if (nl == std::string_view::npos) {
            if (start < s.size()) {
                std::string line(s.substr(start));
                if (!line.empty() && line.back() == '\r') line.pop_back();
                out.push_back(std::move(line));
            }
            break;
        }
        std::string line(s.substr(start, nl - start));
        if (!line.empty() && line.back() == '\r') line.pop_back();
        out.push_back(std::move(line));
        start = nl + 1;
    }
    return out;
}

inline std::vector<std::string> split_ws(std::string_view s) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) i++;
        size_t start = i;
        while (i < s.size() && !std::isspace(static_cast<unsigned char>(s[i]))) i++;
        if (i > start) out.emplace_back(s.substr(start, i - start));
    }
    return out;
}

inline std::string sh_quote(std::string_view s) {
    if (s.empty()) return "''";
    bool safe = true;
    for (unsigned char c : s) {
        if (!(std::isalnum(c) || c == '_' || c == '@' || c == '%' || c == '+' ||
              c == '=' || c == ':' || c == ',' || c == '.' || c == '/' || c == '-')) {
            safe = false;
            break;
        }
    }
    if (safe) return std::string(s);
    std::string out = "'";
    for (char c : s) {
        if (c == '\'') out += "'\"'\"'";
        else out.push_back(c);
    }
    out.push_back('\'');
    return out;
}

inline std::string read_file_bytes(const fs::path &p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) throw std::runtime_error("Cannot open file: " + p.string());
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

inline void write_file_bytes(const fs::path &p, std::string_view data) {
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("Cannot write file: " + p.string());
    out.write(data.data(), static_cast<std::streamsize>(data.size()));
    if (!out) throw std::runtime_error("Failed writing file: " + p.string());
}

inline std::string random_hex(size_t bytes = 16) {
    std::vector<unsigned char> buf(bytes);
    int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if (fd < 0) throw std::runtime_error("Cannot open /dev/urandom");
    size_t got = 0;
    while (got < bytes) {
        ssize_t n = read(fd, buf.data() + got, bytes - got);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) {
            close(fd);
            throw std::runtime_error("Short read from /dev/urandom");
        }
        got += static_cast<size_t>(n);
    }
    close(fd);
    static const char hex[] = "0123456789abcdef";
    std::string out;
    out.reserve(bytes * 2);
    for (unsigned char b : buf) {
        out.push_back(hex[b >> 4]);
        out.push_back(hex[b & 0xf]);
    }
    return out;
}

inline std::string getenv_str(const char *name, const std::string &fallback = "") {
    const char *v = std::getenv(name);
    return (v != nullptr && v[0] != '\0') ? std::string(v) : fallback;
}

inline std::string find_in_path(const std::string &name) {
    if (name.find('/') != std::string::npos) {
        return access(name.c_str(), X_OK) == 0 ? name : "";
    }
    std::string path_env = getenv_str("PATH", "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin");
    size_t start = 0;
    while (start <= path_env.size()) {
        size_t colon = path_env.find(':', start);
        std::string dir = colon == std::string::npos ? path_env.substr(start) : path_env.substr(start, colon - start);
        if (dir.empty()) dir = ".";
        fs::path cand = fs::path(dir) / name;
        struct stat st {};
        if (stat(cand.c_str(), &st) == 0 && S_ISREG(st.st_mode) && access(cand.c_str(), X_OK) == 0) {
            return cand.string();
        }
        if (colon == std::string::npos) break;
        start = colon + 1;
    }
    return "";
}

/* ---- SHA digest via OpenSSL EVP ---- */

inline const EVP_MD *evp_md_by_name(const std::string &algo) {
    if (algo == "sha256" || algo == "SHA256") return EVP_sha256();
    if (algo == "sha1" || algo == "SHA1") return EVP_sha1();
    if (algo == "sha512" || algo == "SHA512") return EVP_sha512();
    throw std::runtime_error("Unsupported digest algorithm: " + algo);
}

inline std::string hex_digest(const unsigned char *buf, unsigned int len) {
    static const char hex[] = "0123456789abcdef";
    std::string out;
    out.reserve(len * 2);
    for (unsigned int i = 0; i < len; i++) {
        out.push_back(hex[buf[i] >> 4]);
        out.push_back(hex[buf[i] & 0xf]);
    }
    return out;
}

inline std::string bytes_digest(std::string_view data, const std::string &algo = "sha256") {
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    if (!ctx) throw std::runtime_error("EVP_MD_CTX_new failed");
    unsigned char out[EVP_MAX_MD_SIZE];
    unsigned int out_len = 0;
    bool ok = EVP_DigestInit_ex(ctx, evp_md_by_name(algo), nullptr) == 1 &&
              EVP_DigestUpdate(ctx, data.data(), data.size()) == 1 &&
              EVP_DigestFinal_ex(ctx, out, &out_len) == 1;
    EVP_MD_CTX_free(ctx);
    if (!ok) throw std::runtime_error("OpenSSL digest failed");
    return hex_digest(out, out_len);
}

inline std::string file_digest(const fs::path &p, const std::string &algo = "sha256") {
    int fd = open(p.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) throw std::runtime_error("Cannot open file for digest: " + p.string());
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    if (!ctx) {
        close(fd);
        throw std::runtime_error("EVP_MD_CTX_new failed");
    }
    bool ok = EVP_DigestInit_ex(ctx, evp_md_by_name(algo), nullptr) == 1;
    std::vector<char> buf(256 * 1024);
    while (ok) {
        ssize_t n = read(fd, buf.data(), buf.size());
        if (n < 0 && errno == EINTR) continue;
        if (n < 0) { ok = false; break; }
        if (n == 0) break;
        ok = EVP_DigestUpdate(ctx, buf.data(), static_cast<size_t>(n)) == 1;
    }
    close(fd);
    unsigned char out[EVP_MAX_MD_SIZE];
    unsigned int out_len = 0;
    if (ok) ok = EVP_DigestFinal_ex(ctx, out, &out_len) == 1;
    EVP_MD_CTX_free(ctx);
    if (!ok) throw std::runtime_error("Digest failed for: " + p.string());
    return hex_digest(out, out_len);
}

/* ---- Minimal RFC-8259 JSON ---- */

struct Json {
    enum Type { Null, Bool, Int, Double, String, Array, Object };
    Type type = Null;
    bool bval = false;
    int64_t ival = 0;
    double dval = 0.0;
    std::string sval;
    std::vector<Json> aval;
    std::map<std::string, Json> oval;

    Json() = default;
    Json(std::nullptr_t) : type(Null) {}
    Json(bool b) : type(Bool), bval(b) {}
    Json(int v) : type(Int), ival(v) {}
    Json(int64_t v) : type(Int), ival(v) {}
    Json(uint64_t v) : type(Int), ival(static_cast<int64_t>(v)) {}
    Json(double d) : type(Double), dval(d) {}
    Json(const char *s) : type(String), sval(s ? s : "") {}
    Json(std::string s) : type(String), sval(std::move(s)) {}
    Json(std::vector<Json> a) : type(Array), aval(std::move(a)) {}
    Json(std::map<std::string, Json> o) : type(Object), oval(std::move(o)) {}

    static Json array() { Json j; j.type = Array; return j; }
    static Json object() { Json j; j.type = Object; return j; }

    bool is_null() const { return type == Null; }
    bool is_bool() const { return type == Bool; }
    bool is_int() const { return type == Int; }
    bool is_string() const { return type == String; }
    bool is_array() const { return type == Array; }
    bool is_object() const { return type == Object; }

    bool contains(const std::string &k) const {
        return type == Object && oval.find(k) != oval.end();
    }
    Json &operator[](const std::string &k) {
        if (type == Null) type = Object;
        return oval[k];
    }
    const Json &at(const std::string &k) const {
        if (type != Object) throw std::runtime_error("JSON value is not an object");
        auto it = oval.find(k);
        if (it == oval.end()) throw std::runtime_error("Missing JSON key: " + k);
        return it->second;
    }
    std::string get_string(const std::string &k, const std::string &def = "") const {
        if (!contains(k)) return def;
        const auto &v = oval.at(k);
        return v.is_string() ? v.sval : def;
    }
    int64_t get_int(const std::string &k, int64_t def = 0) const {
        if (!contains(k)) return def;
        const auto &v = oval.at(k);
        if (v.is_int()) return v.ival;
        if (v.type == Double) return static_cast<int64_t>(v.dval);
        return def;
    }
    bool get_bool(const std::string &k, bool def = false) const {
        if (!contains(k)) return def;
        const auto &v = oval.at(k);
        return v.is_bool() ? v.bval : def;
    }

    bool operator==(const Json &o) const {
        if (type != o.type) return false;
        switch (type) {
        case Null: return true;
        case Bool: return bval == o.bval;
        case Int: return ival == o.ival;
        case Double: return dval == o.dval;
        case String: return sval == o.sval;
        case Array: return aval == o.aval;
        case Object: return oval == o.oval;
        }
        return false;
    }
    bool operator!=(const Json &o) const { return !(*this == o); }

    static void append_utf8(std::string &out, uint32_t cp) {
        if (cp < 0x80) {
            out.push_back(static_cast<char>(cp));
        } else if (cp < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }

    static std::string escape_str(std::string_view s) {
        std::string out = "\"";
        for (unsigned char c : s) {
            switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof buf, "\\u%04x", c);
                    out += buf;
                } else {
                    out.push_back(static_cast<char>(c));
                }
            }
        }
        out.push_back('"');
        return out;
    }

    void dump_to(std::string &out, int indent, int depth) const {
        auto nl = [&](int d) {
            if (indent >= 0) {
                out.push_back('\n');
                out.append(static_cast<size_t>(d * indent), ' ');
            }
        };
        switch (type) {
        case Null: out += "null"; break;
        case Bool: out += (bval ? "true" : "false"); break;
        case Int: out += std::to_string(ival); break;
        case Double: {
            std::ostringstream ss;
            ss << dval;
            out += ss.str();
            break;
        }
        case String: out += escape_str(sval); break;
        case Array: {
            out.push_back('[');
            for (size_t i = 0; i < aval.size(); i++) {
                if (i > 0) out.push_back(',');
                if (indent >= 0) nl(depth + 1);
                aval[i].dump_to(out, indent, depth + 1);
            }
            if (!aval.empty() && indent >= 0) nl(depth);
            out.push_back(']');
            break;
        }
        case Object: {
            out.push_back('{');
            bool first = true;
            for (const auto &[k, v] : oval) {
                if (!first) out.push_back(',');
                first = false;
                if (indent >= 0) nl(depth + 1);
                out += escape_str(k);
                out += (indent >= 0 ? ": " : ":");
                v.dump_to(out, indent, depth + 1);
            }
            if (!oval.empty() && indent >= 0) nl(depth);
            out.push_back('}');
            break;
        }
        }
    }

    std::string dump(int indent = -1) const {
        std::string out;
        dump_to(out, indent, 0);
        return out;
    }

    static Json parse(std::string_view src) {
        size_t pos = 0;
        auto skip = [&]() {
            while (pos < src.size() && (src[pos] == ' ' || src[pos] == '\t' || src[pos] == '\r' || src[pos] == '\n'))
                pos++;
        };
        auto hex4 = [&]() -> uint32_t {
            if (pos + 4 > src.size()) throw std::runtime_error("Invalid JSON \\u escape");
            uint32_t v = 0;
            for (int i = 0; i < 4; i++) {
                char c = src[pos++];
                v <<= 4;
                if (c >= '0' && c <= '9') v |= static_cast<uint32_t>(c - '0');
                else if (c >= 'a' && c <= 'f') v |= static_cast<uint32_t>(c - 'a' + 10);
                else if (c >= 'A' && c <= 'F') v |= static_cast<uint32_t>(c - 'A' + 10);
                else throw std::runtime_error("Invalid hex digit in JSON");
            }
            return v;
        };
        auto parse_string = [&]() -> std::string {
            if (pos >= src.size() || src[pos] != '"') throw std::runtime_error("Expected JSON string");
            pos++;
            std::string out;
            while (pos < src.size()) {
                char c = src[pos++];
                if (c == '"') return out;
                if (c == '\\') {
                    if (pos >= src.size()) throw std::runtime_error("Unterminated JSON escape");
                    char e = src[pos++];
                    switch (e) {
                    case '"': out.push_back('"'); break;
                    case '\\': out.push_back('\\'); break;
                    case '/': out.push_back('/'); break;
                    case 'b': out.push_back('\b'); break;
                    case 'f': out.push_back('\f'); break;
                    case 'n': out.push_back('\n'); break;
                    case 'r': out.push_back('\r'); break;
                    case 't': out.push_back('\t'); break;
                    case 'u': {
                        uint32_t cp = hex4();
                        if (cp >= 0xD800 && cp <= 0xDBFF && pos + 2 <= src.size() &&
                            src[pos] == '\\' && src[pos + 1] == 'u') {
                            pos += 2;
                            uint32_t low = hex4();
                            if (low >= 0xDC00 && low <= 0xDFFF) {
                                cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                            }
                        }
                        append_utf8(out, cp);
                        break;
                    }
                    default: throw std::runtime_error("Invalid JSON escape char");
                    }
                } else {
                    out.push_back(c);
                }
            }
            throw std::runtime_error("Unterminated JSON string");
        };
        std::function<Json(int)> parse_val = [&](int depth) -> Json {
            if (depth > 128) throw std::runtime_error("JSON nesting too deep");
            skip();
            if (pos >= src.size()) throw std::runtime_error("Unexpected end of JSON");
            char c = src[pos];
            if (c == '"') return Json(parse_string());
            if (c == '{') {
                pos++;
                Json obj = Json::object();
                skip();
                if (pos < src.size() && src[pos] == '}') { pos++; return obj; }
                while (true) {
                    skip();
                    std::string key = parse_string();
                    skip();
                    if (pos >= src.size() || src[pos] != ':') throw std::runtime_error("Expected ':' in JSON");
                    pos++;
                    obj.oval[std::move(key)] = parse_val(depth + 1);
                    skip();
                    if (pos >= src.size()) throw std::runtime_error("Unterminated JSON object");
                    if (src[pos] == '}') { pos++; break; }
                    if (src[pos] != ',') throw std::runtime_error("Expected ',' in JSON object");
                    pos++;
                }
                return obj;
            }
            if (c == '[') {
                pos++;
                Json arr = Json::array();
                skip();
                if (pos < src.size() && src[pos] == ']') { pos++; return arr; }
                while (true) {
                    arr.aval.push_back(parse_val(depth + 1));
                    skip();
                    if (pos >= src.size()) throw std::runtime_error("Unterminated JSON array");
                    if (src[pos] == ']') { pos++; break; }
                    if (src[pos] != ',') throw std::runtime_error("Expected ',' in JSON array");
                    pos++;
                }
                return arr;
            }
            if (src.substr(pos, 4) == "true") { pos += 4; return Json(true); }
            if (src.substr(pos, 5) == "false") { pos += 5; return Json(false); }
            if (src.substr(pos, 4) == "null") { pos += 4; return Json(nullptr); }
            if (c == '-' || (c >= '0' && c <= '9')) {
                size_t start = pos;
                if (src[pos] == '-') pos++;
                bool is_float = false;
                while (pos < src.size() && std::isdigit(static_cast<unsigned char>(src[pos]))) pos++;
                if (pos < src.size() && src[pos] == '.') {
                    is_float = true;
                    pos++;
                    while (pos < src.size() && std::isdigit(static_cast<unsigned char>(src[pos]))) pos++;
                }
                if (pos < src.size() && (src[pos] == 'e' || src[pos] == 'E')) {
                    is_float = true;
                    pos++;
                    if (pos < src.size() && (src[pos] == '+' || src[pos] == '-')) pos++;
                    while (pos < src.size() && std::isdigit(static_cast<unsigned char>(src[pos]))) pos++;
                }
                std::string num(src.substr(start, pos - start));
                if (is_float) return Json(std::stod(num));
                return Json(static_cast<int64_t>(std::stoll(num)));
            }
            throw std::runtime_error("Unexpected character in JSON");
        };
        Json res = parse_val(0);
        skip();
        if (pos != src.size()) throw std::runtime_error("Trailing data in JSON");
        return res;
    }
};

/* ---- File lock & atomic JSON persistence ---- */

struct FileLock {
    int fd = -1;
    explicit FileLock(const fs::path &p) {
        fs::create_directories(p.parent_path());
        fd = open(p.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
        if (fd < 0) throw std::runtime_error("Cannot open lock file: " + p.string());
        while (flock(fd, LOCK_EX) < 0) {
            if (errno == EINTR) continue;
            close(fd);
            fd = -1;
            throw std::runtime_error("flock failed: " + p.string());
        }
    }
    ~FileLock() {
        if (fd >= 0) close(fd);
    }
    FileLock(const FileLock &) = delete;
    FileLock &operator=(const FileLock &) = delete;
};

inline void save_json(const fs::path &path, const Json &value) {
    fs::path dir = path.parent_path();
    fs::create_directories(dir);
    std::string tmpl = (dir / ".tmp-XXXXXX").string();
    std::vector<char> buf(tmpl.begin(), tmpl.end());
    buf.push_back('\0');
    int fd = mkstemp(buf.data());
    if (fd < 0) throw std::runtime_error("mkstemp failed in " + dir.string());
    std::string staged(buf.data());
    std::string payload = value.dump(2) + "\n";
    size_t written = 0;
    while (written < payload.size()) {
        ssize_t n = write(fd, payload.data() + written, payload.size() - written);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) {
            close(fd);
            unlink(staged.c_str());
            throw std::runtime_error("Failed writing temporary JSON");
        }
        written += static_cast<size_t>(n);
    }
    fsync(fd);
    close(fd);
    if (rename(staged.c_str(), path.c_str()) != 0) {
        unlink(staged.c_str());
        throw std::runtime_error("Failed renaming temporary JSON to " + path.string());
    }
    int dfd = open(dir.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (dfd >= 0) {
        fsync(dfd);
        close(dfd);
    }
}

/* ---- Bounded subprocess runner ---- */

struct CmdResult {
    int code = -1;
    std::string output;
    bool timed_out = false;
};

inline CmdResult run_cmd(const std::vector<std::string> &argv, int timeout_sec = 60,
                         const std::map<std::string, std::string> &extra_env = {},
                         const std::function<void()> &monitor = nullptr,
                         size_t max_bytes = 16 * 1024 * 1024) {
    if (argv.empty()) throw std::runtime_error("Empty argv");
    if (monitor) monitor();
    char tmpl[] = "/tmp/olympium-cmd-XXXXXX";
    int tmpfd = mkstemp(tmpl);
    if (tmpfd < 0) throw std::runtime_error("mkstemp failed");
    unlink(tmpl);

    pid_t pid = fork();
    if (pid < 0) {
        close(tmpfd);
        throw std::runtime_error("fork failed");
    }
    if (pid == 0) {
        int nul = open("/dev/null", O_RDONLY);
        if (nul >= 0) {
            dup2(nul, 0);
            if (nul > 2) close(nul);
        }
        dup2(tmpfd, 1);
        dup2(tmpfd, 2);
        if (tmpfd > 2) close(tmpfd);
        for (const auto &[k, v] : extra_env) {
            setenv(k.c_str(), v.c_str(), 1);
        }
        std::vector<char *> cargv;
        cargv.reserve(argv.size() + 1);
        for (const auto &a : argv) cargv.push_back(const_cast<char *>(a.c_str()));
        cargv.push_back(nullptr);
        execvp(cargv[0], cargv.data());
        _exit(127);
    }

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeout_sec);
    int status = 0;
    bool done = false;
    bool timed_out = false;
    try {
        while (!done) {
            if (monitor) monitor();
            pid_t r = waitpid(pid, &status, WNOHANG);
            if (r == pid) {
                done = true;
                break;
            }
            if (r < 0 && errno != EINTR) {
                break;
            }
            if (std::chrono::steady_clock::now() >= deadline) {
                timed_out = true;
                kill(pid, SIGKILL);
                while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
                break;
            }
            usleep(50000);
        }
    } catch (...) {
        kill(pid, SIGKILL);
        while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
        close(tmpfd);
        throw;
    }

    struct stat st {};
    if (fstat(tmpfd, &st) == 0 && static_cast<size_t>(st.st_size) > max_bytes) {
        close(tmpfd);
        throw std::runtime_error("Android tool output exceeded the metadata limit");
    }
    lseek(tmpfd, 0, SEEK_SET);
    std::string out;
    if (st.st_size > 0) {
        out.resize(static_cast<size_t>(st.st_size));
        size_t got = 0;
        while (got < out.size()) {
            ssize_t n = read(tmpfd, out.data() + got, out.size() - got);
            if (n < 0 && errno == EINTR) continue;
            if (n <= 0) break;
            got += static_cast<size_t>(n);
        }
        out.resize(got);
    }
    close(tmpfd);

    CmdResult res;
    res.timed_out = timed_out;
    res.output = std::move(out);
    if (timed_out) {
        res.code = 124;
    } else if (WIFEXITED(status)) {
        res.code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        res.code = 128 + WTERMSIG(status);
    } else {
        res.code = 1;
    }
    return res;
}

inline int run_passthrough(const std::vector<std::string> &argv,
                           const std::map<std::string, std::string> &extra_env = {}) {
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        for (const auto &[k, v] : extra_env) setenv(k.c_str(), v.c_str(), 1);
        std::vector<char *> cargv;
        cargv.reserve(argv.size() + 1);
        for (const auto &a : argv) cargv.push_back(const_cast<char *>(a.c_str()));
        cargv.push_back(nullptr);
        execvp(cargv[0], cargv.data());
        _exit(127);
    }
    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
    return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + (WIFSIGNALED(status) ? WTERMSIG(status) : 0);
}

/* ---- /proc process identity ---- */

struct ProcIdentity {
    int64_t pid = 0;
    std::string start_ticks;
    char state = '?';
    Json to_json() const {
        Json j = Json::object();
        j["pid"] = pid;
        j["start_ticks"] = start_ticks;
        return j;
    }
};

inline ProcIdentity process_identity(int64_t pid) {
    fs::path proc = fs::path("/proc") / std::to_string(pid);
    struct stat st {};
    if (stat(proc.c_str(), &st) != 0) {
        throw std::system_error(errno, std::generic_category(), proc.string());
    }
    if (st.st_uid != getuid()) {
        throw std::runtime_error("Managed emulator process is owned by another user");
    }
    std::string stat_txt = read_file_bytes(proc / "stat");
    size_t rp = stat_txt.rfind(')');
    if (rp == std::string::npos || rp + 2 >= stat_txt.size()) {
        throw std::runtime_error("Malformed /proc stat");
    }
    auto fields = split_ws(std::string_view(stat_txt).substr(rp + 2));
    if (fields.size() <= 19) throw std::runtime_error("Short /proc stat");
    ProcIdentity id;
    id.pid = pid;
    id.state = fields[0].empty() ? '?' : fields[0][0];
    id.start_ticks = fields[19];
    return id;
}

/* ---- Sparse copy ---- */

inline void sparse_copy_fd(int out_fd, const std::function<ssize_t(char *, size_t)> &reader) {
    constexpr size_t CHUNK = 1024 * 1024;
    std::vector<char> buf(CHUNK);
    off_t total = 0;
    while (true) {
        ssize_t n = reader(buf.data(), CHUNK);
        if (n < 0) throw std::runtime_error("Read error during sparse copy");
        if (n == 0) break;
        total += n;
        bool all_zero = true;
        for (ssize_t i = 0; i < n; i++) {
            if (buf[static_cast<size_t>(i)] != 0) { all_zero = false; break; }
        }
        if (all_zero) {
            if (lseek(out_fd, n, SEEK_CUR) < 0) throw std::runtime_error("lseek failed in sparse copy");
        } else {
            size_t done = 0;
            while (done < static_cast<size_t>(n)) {
                ssize_t w = write(out_fd, buf.data() + done, static_cast<size_t>(n) - done);
                if (w < 0 && errno == EINTR) continue;
                if (w <= 0) throw std::runtime_error("write failed in sparse copy");
                done += static_cast<size_t>(w);
            }
        }
    }
    if (ftruncate(out_fd, total) != 0) throw std::runtime_error("ftruncate failed in sparse copy");
}

inline void sparse_copy_file(const fs::path &src, const fs::path &dst) {
    int in_fd = open(src.c_str(), O_RDONLY | O_CLOEXEC);
    if (in_fd < 0) throw std::runtime_error("Cannot open " + src.string());
    int out_fd = open(dst.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (out_fd < 0) {
        close(in_fd);
        throw std::runtime_error("Cannot create " + dst.string());
    }
    try {
        sparse_copy_fd(out_fd, [&](char *buf, size_t n) -> ssize_t {
            while (true) {
                ssize_t r = read(in_fd, buf, n);
                if (r < 0 && errno == EINTR) continue;
                return r;
            }
        });
    } catch (...) {
        close(in_fd);
        close(out_fd);
        throw;
    }
    close(in_fd);
    close(out_fd);
}

/* ---- ZIP Reader & Writer (zlib) ---- */

struct ZipEntry {
    std::string name;
    uint16_t method = 0;
    uint16_t mod_time = 0;
    uint16_t mod_date = 0x21; // 1980-01-01
    uint32_t crc = 0;
    uint64_t comp_size = 0;
    uint64_t uncomp_size = 0;
    uint32_t ext_attr = 0;
    uint64_t local_offset = 0;
    std::string extra;

    bool is_dir() const { return !name.empty() && name.back() == '/'; }
    uint32_t unix_mode() const { return (ext_attr >> 16) & 0xFFFF; }
    bool is_symlink() const { return S_ISLNK(unix_mode()); }
};

inline uint16_t rd_le16(const unsigned char *p) {
    return static_cast<uint16_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8));
}
inline uint32_t rd_le32(const unsigned char *p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
inline uint64_t rd_le64(const unsigned char *p) {
    return static_cast<uint64_t>(rd_le32(p)) | (static_cast<uint64_t>(rd_le32(p + 4)) << 32);
}
inline void wr_le16(std::string &out, uint16_t v) {
    out.push_back(static_cast<char>(v & 0xFF));
    out.push_back(static_cast<char>((v >> 8) & 0xFF));
}
inline void wr_le32(std::string &out, uint32_t v) {
    for (int i = 0; i < 4; i++) out.push_back(static_cast<char>((v >> (i * 8)) & 0xFF));
}

class ZipReader {
public:
    explicit ZipReader(const fs::path &p) : path_(p) {
        fd_ = open(p.c_str(), O_RDONLY | O_CLOEXEC);
        if (fd_ < 0) throw std::runtime_error("Cannot open ZIP: " + p.string());
        struct stat st {};
        if (fstat(fd_, &st) != 0 || st.st_size < 22) {
            close(fd_);
            fd_ = -1;
            throw std::runtime_error("Invalid ZIP archive: " + p.string());
        }
        size_ = static_cast<uint64_t>(st.st_size);
        parse_cd();
    }
    ~ZipReader() { if (fd_ >= 0) close(fd_); }
    ZipReader(const ZipReader &) = delete;
    ZipReader &operator=(const ZipReader &) = delete;

    const std::vector<ZipEntry> &entries() const { return entries_; }

    const ZipEntry *find(const std::string &name) const {
        for (const auto &e : entries_) {
            if (e.name == name) return &e;
        }
        return nullptr;
    }

    std::string read_member(const ZipEntry &e, uint64_t limit = 64ULL * 1024 * 1024) const {
        if (e.uncomp_size > limit) throw std::runtime_error("ZIP member too large: " + e.name);
        unsigned char lh[30];
        pread_all(e.local_offset, lh, 30);
        if (rd_le32(lh) != 0x04034b50U) throw std::runtime_error("Bad ZIP local header");
        uint16_t nlen = rd_le16(lh + 26);
        uint16_t elen = rd_le16(lh + 28);
        uint64_t data_off = e.local_offset + 30 + nlen + elen;
        if (data_off + e.comp_size > size_) throw std::runtime_error("Truncated ZIP member");
        std::string comp(static_cast<size_t>(e.comp_size), '\0');
        if (!comp.empty()) pread_all(data_off, comp.data(), comp.size());
        std::string out;
        if (e.method == 0) {
            out = std::move(comp);
        } else if (e.method == 8) {
            out.resize(static_cast<size_t>(e.uncomp_size));
            z_stream zs {};
            if (inflateInit2(&zs, -MAX_WBITS) != Z_OK) throw std::runtime_error("inflateInit2 failed");
            zs.next_in = reinterpret_cast<Bytef *>(comp.data());
            zs.avail_in = static_cast<uInt>(comp.size());
            zs.next_out = reinterpret_cast<Bytef *>(out.data());
            zs.avail_out = static_cast<uInt>(out.size());
            int rc = inflate(&zs, Z_FINISH);
            size_t total = zs.total_out;
            inflateEnd(&zs);
            if (rc != Z_STREAM_END || total != e.uncomp_size) {
                throw std::runtime_error("ZIP deflate decompression failed: " + e.name);
            }
        } else {
            throw std::runtime_error("Unsupported ZIP compression method");
        }
        uLong actual_crc = crc32(0L, Z_NULL, 0);
        if (!out.empty()) {
            actual_crc = crc32(actual_crc, reinterpret_cast<const Bytef *>(out.data()),
                               static_cast<uInt>(out.size()));
        }
        if (static_cast<uint32_t>(actual_crc) != e.crc) {
            throw std::runtime_error("ZIP CRC mismatch: " + e.name);
        }
        return out;
    }

    void extract_to_fd_sparse(const ZipEntry &e, int out_fd) const {
        unsigned char lh[30];
        pread_all(e.local_offset, lh, 30);
        if (rd_le32(lh) != 0x04034b50U) throw std::runtime_error("Bad ZIP local header");
        uint64_t data_off = e.local_offset + 30 + rd_le16(lh + 26) + rd_le16(lh + 28);
        if (data_off + e.comp_size > size_) throw std::runtime_error("Truncated ZIP member");
        if (e.method == 0) {
            uint64_t pos = 0;
            uLong crc = crc32(0L, Z_NULL, 0);
            sparse_copy_fd(out_fd, [&](char *buf, size_t n) -> ssize_t {
                uint64_t rem = e.comp_size - pos;
                if (rem == 0) return 0;
                size_t want = static_cast<size_t>(std::min<uint64_t>(rem, n));
                pread_all(data_off + pos, buf, want);
                crc = crc32(crc, reinterpret_cast<const Bytef *>(buf), static_cast<uInt>(want));
                pos += want;
                return static_cast<ssize_t>(want);
            });
            if (static_cast<uint32_t>(crc) != e.crc) throw std::runtime_error("ZIP CRC mismatch");
            return;
        }
        if (e.method != 8) throw std::runtime_error("Unsupported ZIP compression method");
        z_stream zs {};
        if (inflateInit2(&zs, -MAX_WBITS) != Z_OK) throw std::runtime_error("inflateInit2 failed");
        std::vector<unsigned char> inbuf(256 * 1024);
        uint64_t pos = 0;
        uLong crc = crc32(0L, Z_NULL, 0);
        bool stream_end = false;
        try {
            sparse_copy_fd(out_fd, [&](char *buf, size_t n) -> ssize_t {
                if (stream_end) return 0;
                zs.next_out = reinterpret_cast<Bytef *>(buf);
                zs.avail_out = static_cast<uInt>(n);
                while (zs.avail_out > 0 && !stream_end) {
                    if (zs.avail_in == 0) {
                        uint64_t rem = e.comp_size - pos;
                        if (rem == 0) throw std::runtime_error("Truncated deflate stream");
                        size_t want = static_cast<size_t>(std::min<uint64_t>(rem, inbuf.size()));
                        pread_all(data_off + pos, inbuf.data(), want);
                        pos += want;
                        zs.next_in = inbuf.data();
                        zs.avail_in = static_cast<uInt>(want);
                    }
                    int rc = inflate(&zs, Z_NO_FLUSH);
                    if (rc == Z_STREAM_END) { stream_end = true; break; }
                    if (rc != Z_OK) throw std::runtime_error("ZIP deflate error");
                }
                size_t produced = n - zs.avail_out;
                if (produced > 0) {
                    crc = crc32(crc, reinterpret_cast<const Bytef *>(buf), static_cast<uInt>(produced));
                }
                return static_cast<ssize_t>(produced);
            });
        } catch (...) {
            inflateEnd(&zs);
            throw;
        }
        inflateEnd(&zs);
        if (static_cast<uint32_t>(crc) != e.crc) throw std::runtime_error("ZIP CRC mismatch");
    }

private:
    fs::path path_;
    int fd_ = -1;
    uint64_t size_ = 0;
    std::vector<ZipEntry> entries_;

    void pread_all(uint64_t off, void *dst, size_t len) const {
        size_t done = 0;
        while (done < len) {
            ssize_t n = pread(fd_, static_cast<char *>(dst) + done, len - done,
                              static_cast<off_t>(off + done));
            if (n < 0 && errno == EINTR) continue;
            if (n <= 0) throw std::runtime_error("Short read in ZIP: " + path_.string());
            done += static_cast<size_t>(n);
        }
    }

    void parse_cd() {
        size_t tail_len = static_cast<size_t>(std::min<uint64_t>(size_, 65557));
        std::vector<unsigned char> tail(tail_len);
        pread_all(size_ - tail_len, tail.data(), tail_len);
        int eocd = -1;
        for (int i = static_cast<int>(tail_len) - 22; i >= 0; i--) {
            if (rd_le32(tail.data() + i) == 0x06054b50U) {
                uint16_t comment_len = rd_le16(tail.data() + i + 20);
                if (static_cast<size_t>(i) + 22 + comment_len == tail_len) {
                    eocd = i;
                    break;
                }
            }
        }
        if (eocd < 0) throw std::runtime_error("ZIP EOCD not found");
        uint64_t cd_size = rd_le32(tail.data() + eocd + 12);
        uint64_t cd_off = rd_le32(tail.data() + eocd + 16);
        uint64_t total_entries = rd_le16(tail.data() + eocd + 10);
        if ((cd_size == 0xFFFFFFFFU || cd_off == 0xFFFFFFFFU || total_entries == 0xFFFFU) && eocd >= 20 &&
            rd_le32(tail.data() + eocd - 20) == 0x07064b50U) {
            uint64_t z64_off = rd_le64(tail.data() + eocd - 12);
            unsigned char z64[56];
            pread_all(z64_off, z64, sizeof z64);
            if (rd_le32(z64) != 0x06064b50U) throw std::runtime_error("Invalid ZIP64 EOCD");
            total_entries = rd_le64(z64 + 32);
            cd_size = rd_le64(z64 + 40);
            cd_off = rd_le64(z64 + 48);
        }
        if (cd_off + cd_size > size_) throw std::runtime_error("Truncated ZIP central directory");
        std::vector<unsigned char> cd(static_cast<size_t>(cd_size));
        pread_all(cd_off, cd.data(), cd.size());
        size_t p = 0;
        entries_.reserve(static_cast<size_t>(std::min<uint64_t>(total_entries, 100000)));
        while (p + 46 <= cd.size()) {
            if (rd_le32(cd.data() + p) != 0x02014b50U) throw std::runtime_error("Bad ZIP CD signature");
            ZipEntry e;
            e.method = rd_le16(cd.data() + p + 10);
            e.mod_time = rd_le16(cd.data() + p + 12);
            e.mod_date = rd_le16(cd.data() + p + 14);
            e.crc = rd_le32(cd.data() + p + 16);
            e.comp_size = rd_le32(cd.data() + p + 20);
            e.uncomp_size = rd_le32(cd.data() + p + 24);
            uint16_t nlen = rd_le16(cd.data() + p + 28);
            uint16_t elen = rd_le16(cd.data() + p + 30);
            uint16_t clen = rd_le16(cd.data() + p + 32);
            e.ext_attr = rd_le32(cd.data() + p + 38);
            e.local_offset = rd_le32(cd.data() + p + 42);
            if (p + 46 + nlen + elen + clen > cd.size()) throw std::runtime_error("Truncated ZIP CD entry");
            e.name.assign(reinterpret_cast<const char *>(cd.data() + p + 46), nlen);
            e.extra.assign(reinterpret_cast<const char *>(cd.data() + p + 46 + nlen), elen);
            // Parse ZIP64 extra field if present.
            size_t ep = 0;
            const unsigned char *ex = cd.data() + p + 46 + nlen;
            while (ep + 4 <= elen) {
                uint16_t tag = rd_le16(ex + ep);
                uint16_t sz = rd_le16(ex + ep + 2);
                if (ep + 4 + sz > elen) break;
                if (tag == 0x0001) {
                    size_t zp = ep + 4;
                    if (e.uncomp_size == 0xFFFFFFFFU && zp + 8 <= ep + 4 + sz) {
                        e.uncomp_size = rd_le64(ex + zp); zp += 8;
                    }
                    if (e.comp_size == 0xFFFFFFFFU && zp + 8 <= ep + 4 + sz) {
                        e.comp_size = rd_le64(ex + zp); zp += 8;
                    }
                    if (e.local_offset == 0xFFFFFFFFU && zp + 8 <= ep + 4 + sz) {
                        e.local_offset = rd_le64(ex + zp);
                    }
                }
                ep += 4 + sz;
            }
            entries_.push_back(std::move(e));
            p += 46 + nlen + elen + clen;
        }
    }
};

class ZipWriter {
public:
    explicit ZipWriter(const fs::path &p) : out_(p, std::ios::binary | std::ios::trunc) {
        if (!out_) throw std::runtime_error("Cannot create ZIP: " + p.string());
    }
    void write_entry(const std::string &name, std::string_view data, bool align4 = false,
                     uint16_t method = 0, uint32_t ext_attr = (0100644U << 16),
                     std::string extra = "") {
        std::string payload;
        uint16_t use_method = method;
        if (method == 8) {
            uLongf bound = compressBound(static_cast<uLong>(data.size()));
            payload.resize(bound);
            z_stream zs {};
            if (deflateInit2(&zs, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY) != Z_OK)
                throw std::runtime_error("deflateInit2 failed");
            zs.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(data.data()));
            zs.avail_in = static_cast<uInt>(data.size());
            zs.next_out = reinterpret_cast<Bytef *>(payload.data());
            zs.avail_out = static_cast<uInt>(payload.size());
            int rc = deflate(&zs, Z_FINISH);
            payload.resize(zs.total_out);
            deflateEnd(&zs);
            if (rc != Z_STREAM_END) throw std::runtime_error("deflate failed");
        } else {
            use_method = 0;
            payload.assign(data.data(), data.size());
        }
        uint32_t local_off = static_cast<uint32_t>(out_.tellp());
        if (align4) {
            size_t offset = local_off + 30 + name.size() + extra.size();
            size_t padding = (4 - ((offset + 4) % 4)) % 4;
            wr_le16(extra, 0xFFFF);
            wr_le16(extra, static_cast<uint16_t>(padding));
            extra.append(padding, '\0');
        }
        uLong crc = crc32(0L, Z_NULL, 0);
        if (!data.empty()) crc = crc32(crc, reinterpret_cast<const Bytef *>(data.data()), static_cast<uInt>(data.size()));

        std::string lh;
        wr_le32(lh, 0x04034b50U);
        wr_le16(lh, 20);
        wr_le16(lh, 0);
        wr_le16(lh, use_method);
        wr_le16(lh, 0);
        wr_le16(lh, 0x21);
        wr_le32(lh, static_cast<uint32_t>(crc));
        wr_le32(lh, static_cast<uint32_t>(payload.size()));
        wr_le32(lh, static_cast<uint32_t>(data.size()));
        wr_le16(lh, static_cast<uint16_t>(name.size()));
        wr_le16(lh, static_cast<uint16_t>(extra.size()));
        lh += name;
        lh += extra;
        out_.write(lh.data(), static_cast<std::streamsize>(lh.size()));
        if (!payload.empty()) out_.write(payload.data(), static_cast<std::streamsize>(payload.size()));

        Rec r;
        r.name = name;
        r.extra = extra;
        r.method = use_method;
        r.crc = static_cast<uint32_t>(crc);
        r.comp_size = static_cast<uint32_t>(payload.size());
        r.uncomp_size = static_cast<uint32_t>(data.size());
        r.ext_attr = ext_attr;
        r.local_off = local_off;
        recs_.push_back(std::move(r));
    }

    void finish() {
        if (finished_) return;
        finished_ = true;
        uint32_t cd_off = static_cast<uint32_t>(out_.tellp());
        std::string cd;
        for (const auto &r : recs_) {
            wr_le32(cd, 0x02014b50U);
            wr_le16(cd, (3U << 8) | 20U); // Unix created-by
            wr_le16(cd, 20);
            wr_le16(cd, 0);
            wr_le16(cd, r.method);
            wr_le16(cd, 0);
            wr_le16(cd, 0x21);
            wr_le32(cd, r.crc);
            wr_le32(cd, r.comp_size);
            wr_le32(cd, r.uncomp_size);
            wr_le16(cd, static_cast<uint16_t>(r.name.size()));
            wr_le16(cd, static_cast<uint16_t>(r.extra.size()));
            wr_le16(cd, 0);
            wr_le16(cd, 0);
            wr_le16(cd, 0);
            wr_le32(cd, r.ext_attr);
            wr_le32(cd, r.local_off);
            cd += r.name;
            cd += r.extra;
        }
        out_.write(cd.data(), static_cast<std::streamsize>(cd.size()));
        std::string eocd;
        wr_le32(eocd, 0x06054b50U);
        wr_le16(eocd, 0);
        wr_le16(eocd, 0);
        wr_le16(eocd, static_cast<uint16_t>(recs_.size()));
        wr_le16(eocd, static_cast<uint16_t>(recs_.size()));
        wr_le32(eocd, static_cast<uint32_t>(cd.size()));
        wr_le32(eocd, cd_off);
        wr_le16(eocd, 0);
        out_.write(eocd.data(), static_cast<std::streamsize>(eocd.size()));
        out_.flush();
        if (!out_) throw std::runtime_error("Failed finishing ZIP");
    }
    ~ZipWriter() {
        try { finish(); } catch (...) {}
    }

private:
    struct Rec {
        std::string name, extra;
        uint16_t method = 0;
        uint32_t crc = 0, comp_size = 0, uncomp_size = 0, ext_attr = 0, local_off = 0;
    };
    std::ofstream out_;
    std::vector<Rec> recs_;
    bool finished_ = false;
};

/* ---- Safe archive extraction (ZIP & TAR/.tar.gz/.tar.xz) ---- */

inline bool path_is_relative_to(const fs::path &p, const fs::path &base) {
    auto bp = base.begin();
    auto pp = p.begin();
    for (; bp != base.end(); ++bp, ++pp) {
        if (pp == p.end() || *pp != *bp) return false;
    }
    return true;
}

inline std::optional<fs::path> archive_target(const std::string &name, const fs::path &base, int strip) {
    fs::path p(name);
    if (p.is_absolute()) throw std::runtime_error("Unsafe Android archive member");
    std::vector<std::string> parts;
    for (const auto &part : p) {
        std::string s = part.string();
        if (s == "..") throw std::runtime_error("Unsafe Android archive member");
        if (s.empty() || s == ".") continue;
        parts.push_back(std::move(s));
    }
    if (static_cast<int>(parts.size()) <= strip) return std::nullopt;
    fs::path out = base;
    for (size_t i = static_cast<size_t>(strip); i < parts.size(); i++) out /= parts[i];
    return out;
}

inline void install_deferred_symlinks(const std::vector<std::pair<fs::path, std::string>> &links,
                                      const fs::path &stage) {
    fs::path stage_canon = fs::weakly_canonical(stage);
    for (const auto &[dest, link] : links) {
        if (fs::path(link).is_absolute()) {
            throw std::runtime_error("Unsafe Android archive link");
        }
        fs::path resolved = fs::weakly_canonical(dest.parent_path() / link);
        if (!path_is_relative_to(resolved, stage_canon)) {
            throw std::runtime_error("Unsafe Android archive link");
        }
        fs::create_directories(dest.parent_path());
        std::error_code ec;
        fs::remove(dest, ec);
        fs::create_symlink(link, dest);
    }
}

inline void extract_tar_stream(const std::function<ssize_t(char *, size_t)> &read_exact_fn,
                               const fs::path &base, int strip, const fs::path &stage) {
    auto read_all = [&](char *buf, size_t n) -> bool {
        size_t got = 0;
        while (got < n) {
            ssize_t r = read_exact_fn(buf + got, n - got);
            if (r < 0) throw std::runtime_error("TAR read error");
            if (r == 0) return got == 0 ? false : throw std::runtime_error("Truncated TAR stream");
            got += static_cast<size_t>(r);
        }
        return true;
    };
    auto parse_octal = [](const char *p, size_t n) -> uint64_t {
        if (n > 0 && (static_cast<unsigned char>(p[0]) & 0x80)) {
            uint64_t v = static_cast<unsigned char>(p[0]) & 0x7F;
            for (size_t i = 1; i < n; i++) v = (v << 8) | static_cast<unsigned char>(p[i]);
            return v;
        }
        uint64_t v = 0;
        for (size_t i = 0; i < n && p[i] != '\0'; i++) {
            if (p[i] == ' ') continue;
            if (p[i] < '0' || p[i] > '7') break;
            v = (v << 3) | static_cast<uint64_t>(p[i] - '0');
        }
        return v;
    };
    std::vector<std::pair<fs::path, std::string>> links;
    std::string next_long_name, next_long_link;
    char hdr[512];
    while (read_all(hdr, 512)) {
        bool zero = true;
        for (char c : hdr) if (c != 0) { zero = false; break; }
        if (zero) break;
        std::string name(hdr, strnlen(hdr, 100));
        std::string prefix(hdr + 345, strnlen(hdr + 345, 155));
        if (!prefix.empty()) name = prefix + "/" + name;
        std::string linkname(hdr + 157, strnlen(hdr + 157, 100));
        uint64_t mode = parse_octal(hdr + 100, 8);
        uint64_t size = parse_octal(hdr + 124, 12);
        char type = hdr[156];
        if (!next_long_name.empty()) { name = std::move(next_long_name); next_long_name.clear(); }
        if (!next_long_link.empty()) { linkname = std::move(next_long_link); next_long_link.clear(); }
        uint64_t padded = ((size + 511) / 512) * 512;

        if (type == 'L' || type == 'K') {
            std::string data(static_cast<size_t>(padded), '\0');
            if (!data.empty()) read_all(data.data(), data.size());
            data.resize(static_cast<size_t>(size));
            while (!data.empty() && data.back() == '\0') data.pop_back();
            if (type == 'L') next_long_name = std::move(data);
            else next_long_link = std::move(data);
            continue;
        }
        if (type == 'x' || type == 'g') {
            std::string data(static_cast<size_t>(padded), '\0');
            if (!data.empty()) read_all(data.data(), data.size());
            data.resize(static_cast<size_t>(size));
            size_t pos = 0;
            while (pos < data.size()) {
                size_t sp = data.find(' ', pos);
                if (sp == std::string::npos) break;
                size_t rec_len = static_cast<size_t>(std::stoul(data.substr(pos, sp - pos)));
                if (rec_len == 0 || pos + rec_len > data.size()) break;
                std::string kv = data.substr(sp + 1, pos + rec_len - sp - 1);
                if (!kv.empty() && kv.back() == '\n') kv.pop_back();
                if (type == 'x') {
                    if (kv.rfind("path=", 0) == 0) next_long_name = kv.substr(5);
                    else if (kv.rfind("linkpath=", 0) == 0) next_long_link = kv.substr(9);
                }
                pos += rec_len;
            }
            continue;
        }

        auto dest = archive_target(name, base, strip);
        if (!dest) {
            std::vector<char> skip_buf(65536);
            uint64_t rem = padded;
            while (rem > 0) {
                size_t chunk = static_cast<size_t>(std::min<uint64_t>(rem, skip_buf.size()));
                read_all(skip_buf.data(), chunk);
                rem -= chunk;
            }
            continue;
        }

        if (type == '5') {
            fs::create_directories(*dest);
        } else if (type == '2') {
            links.emplace_back(*dest, linkname);
        } else if (type == '0' || type == '\0') {
            fs::create_directories(dest->parent_path());
            int out_fd = open(dest->c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC,
                              (mode & 0111) ? 0755 : 0644);
            if (out_fd < 0) throw std::runtime_error("Cannot create " + dest->string());
            uint64_t rem = size;
            try {
                sparse_copy_fd(out_fd, [&](char *buf, size_t n) -> ssize_t {
                    if (rem == 0) return 0;
                    size_t want = static_cast<size_t>(std::min<uint64_t>(rem, n));
                    if (!read_all(buf, want)) throw std::runtime_error("Truncated TAR file");
                    rem -= want;
                    return static_cast<ssize_t>(want);
                });
            } catch (...) {
                close(out_fd);
                throw;
            }
            close(out_fd);
            chmod(dest->c_str(), (mode & 0111) ? 0755 : 0644);
            uint64_t pad = padded - size;
            if (pad > 0) {
                char padbuf[512];
                read_all(padbuf, static_cast<size_t>(pad));
            }
        } else {
            throw std::runtime_error("Unsupported Android archive member");
        }
    }
    install_deferred_symlinks(links, stage);
}

inline void extract_archive(const fs::path &archive, const std::string &destination,
                            int strip, const fs::path &stage) {
    fs::path base = stage / destination;
    unsigned char magic[6] {};
    {
        std::ifstream in(archive, std::ios::binary);
        in.read(reinterpret_cast<char *>(magic), sizeof magic);
    }
    if (magic[0] == 'P' && magic[1] == 'K' && magic[2] == 3 && magic[3] == 4) {
        ZipReader zr(archive);
        std::vector<std::pair<fs::path, std::string>> links;
        for (const auto &entry : zr.entries()) {
            auto dest = archive_target(entry.name, base, strip);
            if (!dest) continue;
            if (entry.is_dir()) {
                fs::create_directories(*dest);
            } else if (entry.is_symlink()) {
                links.emplace_back(*dest, zr.read_member(entry, 65536));
            } else {
                fs::create_directories(dest->parent_path());
                mode_t m = (entry.unix_mode() & 0111) ? 0755 : 0644;
                int out_fd = open(dest->c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, m);
                if (out_fd < 0) throw std::runtime_error("Cannot create " + dest->string());
                try {
                    zr.extract_to_fd_sparse(entry, out_fd);
                } catch (...) {
                    close(out_fd);
                    throw;
                }
                close(out_fd);
                chmod(dest->c_str(), m);
            }
        }
        install_deferred_symlinks(links, stage);
        return;
    }
    if (magic[0] == 0x1F && magic[1] == 0x8B) {
        gzFile gz = gzopen(archive.c_str(), "rb");
        if (!gz) throw std::runtime_error("Cannot open gzip tar: " + archive.string());
        try {
            extract_tar_stream([&](char *buf, size_t n) -> ssize_t {
                return gzread(gz, buf, static_cast<unsigned int>(n));
            }, base, strip, stage);
        } catch (...) {
            gzclose(gz);
            throw;
        }
        gzclose(gz);
        return;
    }
    if (magic[0] == 0xFD && magic[1] == '7' && magic[2] == 'z' && magic[3] == 'X' && magic[4] == 'Z') {
        int fds[2];
        if (pipe2(fds, O_CLOEXEC) != 0) throw std::runtime_error("pipe2 failed");
        pid_t pid = fork();
        if (pid < 0) { close(fds[0]); close(fds[1]); throw std::runtime_error("fork failed"); }
        if (pid == 0) {
            dup2(fds[1], 1);
            close(fds[0]); close(fds[1]);
            execlp("xz", "xz", "-dc", "--", archive.c_str(), static_cast<char *>(nullptr));
            _exit(127);
        }
        close(fds[1]);
        try {
            extract_tar_stream([&](char *buf, size_t n) -> ssize_t {
                while (true) {
                    ssize_t r = read(fds[0], buf, n);
                    if (r < 0 && errno == EINTR) continue;
                    return r;
                }
            }, base, strip, stage);
        } catch (...) {
            close(fds[0]);
            kill(pid, SIGKILL);
            int st = 0;
            while (waitpid(pid, &st, 0) < 0 && errno == EINTR) {}
            throw;
        }
        close(fds[0]);
        int st = 0;
        while (waitpid(pid, &st, 0) < 0 && errno == EINTR) {}
        if (!WIFEXITED(st) || WEXITSTATUS(st) != 0) throw std::runtime_error("xz decompression failed");
        return;
    }
    // Plain uncompressed TAR.
    int fd = open(archive.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) throw std::runtime_error("Cannot open tar: " + archive.string());
    try {
        extract_tar_stream([&](char *buf, size_t n) -> ssize_t {
            while (true) {
                ssize_t r = read(fd, buf, n);
                if (r < 0 && errno == EINTR) continue;
                return r;
            }
        }, base, strip, stage);
    } catch (...) {
        close(fd);
        throw;
    }
    close(fd);
}

} // namespace olympium
