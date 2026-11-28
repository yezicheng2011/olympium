/*
 * Copyright 2026 Development Desktop contributors
 * SPDX-License-Identifier: Apache-2.0 — Licensed under the Apache License, Version 2.0. See LICENSE.
 *
 * Install and launch single-entry HAPs into an explicitly configured
 * OpenHarmony SDK session.
 */
#include "common.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <regex>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

using namespace olympium;

static constexpr uint64_t MAX_METADATA = 512 * 1024;
static constexpr uint64_t MAX_ICON = 1024 * 1024;

static bool is_valid_bundle(const std::string &s) {
    static const std::regex re(R"([A-Za-z][A-Za-z0-9_]*(?:\.[A-Za-z][A-Za-z0-9_]*)+)");
    return s.size() <= 128 && std::regex_match(s, re);
}

static bool is_valid_identifier(const std::string &s) {
    static const std::regex re(R"([A-Za-z_][A-Za-z0-9_.]*)");
    return s.size() <= 128 && std::regex_match(s, re);
}

static std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

static std::string sanitize_label(const std::string &raw, const std::string &fallback) {
    auto words = split_ws(raw);
    std::string joined;
    for (size_t i = 0; i < words.size(); i++) {
        if (i > 0) joined.push_back(' ');
        joined += words[i];
    }
    std::string clean;
    for (unsigned char c : joined) {
        if (c >= 0x20 && c != 0x7f) clean.push_back(static_cast<char>(c));
    }
    if (clean.size() > 120) {
        clean.resize(120);
        while (!clean.empty() && (static_cast<unsigned char>(clean.back()) & 0xC0) == 0x80) {
            clean.pop_back();
        }
    }
    return clean.empty() ? fallback : clean;
}

static Json package_info(const fs::path &path, const fs::path &root) {
    if (!fs::is_regular_file(path)) {
        throw std::runtime_error("Expected a regular HAP file.");
    }
    ZipReader archive(path);
    const ZipEntry *mod_ent = archive.find("module.json");
    if (!mod_ent) throw std::runtime_error("HAP is missing module.json.");
    Json manifest = Json::parse(archive.read_member(*mod_ent, MAX_METADATA));
    const Json &app = manifest.at("app");
    const Json &mod = manifest.at("module");
    std::string bundle = app.at("bundleName").sval;
    std::string ability = mod.at("mainElement").sval;
    bool exported_main = false;
    if (mod.contains("abilities") && mod.at("abilities").is_array()) {
        for (const auto &a : mod.at("abilities").aval) {
            if (a.get_string("name") == ability && a.get_bool("exported")) {
                exported_main = true;
                break;
            }
        }
    }
    if (!is_valid_bundle(bundle) || !is_valid_identifier(ability) ||
        mod.get_string("type") != "entry" || !exported_main) {
        throw std::runtime_error("Expected an entry HAP with an exported main ability.");
    }
    std::string label = app.get_string("label");
    if (label.empty() || label[0] == '$') {
        std::string stem = path.stem().string();
        label = (stem != bundle && !stem.empty()) ? stem : bundle;
    }
    label = sanitize_label(label, bundle);
    std::string identifier = "oh-" + bytes_digest(bundle, "sha256").substr(0, 16);
    std::string sha = file_digest(path, "sha256");

    std::string icon_path;
    std::string icon_ref = app.get_string("icon");
    static const std::regex media_re(R"(\$media:([A-Za-z0-9_]+))");
    std::smatch m;
    if (std::regex_match(icon_ref, m, media_re)) {
        std::string member_name = "resources/base/media/" + m[1].str() + ".png";
        if (const ZipEntry *ient = archive.find(member_name); ient != nullptr && ient->uncomp_size <= MAX_ICON) {
            std::string data;
            try { data = archive.read_member(*ient, MAX_ICON); } catch (...) {}
            static const unsigned char png_ihdr[16] = {
                0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n', 0, 0, 0, 0x0d, 'I', 'H', 'D', 'R'
            };
            if (data.size() >= 24 && std::memcmp(data.data(), png_ihdr, 16) == 0) {
                auto be32 = [&](size_t off) -> uint32_t {
                    return (static_cast<uint32_t>(static_cast<unsigned char>(data[off])) << 24) |
                           (static_cast<uint32_t>(static_cast<unsigned char>(data[off + 1])) << 16) |
                           (static_cast<uint32_t>(static_cast<unsigned char>(data[off + 2])) << 8) |
                           static_cast<uint32_t>(static_cast<unsigned char>(data[off + 3]));
                };
                uint32_t w = be32(16), h = be32(20);
                if (w > 0 && w <= 1024 && h > 0 && h <= 1024) {
                    fs::path icons = root / "icons";
                    fs::create_directories(icons);
                    chmod(icons.c_str(), 0700);
                    fs::path dest = icons / (identifier + "-" + sha.substr(0, 16) + ".png");
                    fs::path tmp = icons / (".icon-" + random_hex(8) + ".tmp");
                    write_file_bytes(tmp, data);
                    fs::rename(tmp, dest);
                    icon_path = dest.string();
                }
            }
        }
    }

    Json out = Json::object();
    out["ok"] = true;
    out["id"] = identifier;
    out["package"] = bundle;
    out["ability"] = ability;
    out["name"] = label;
    out["path"] = path.string();
    out["sha"] = sha;
    out["icon"] = icon_path;
    return out;
}

struct Session {
    fs::path config;
    fs::path hdc;
    fs::path qmp;
    std::string target;

    Session() {
        std::string env_cfg = getenv_str("OLYMPIUM_OPENHARMONY_SESSION");
        fs::path cfg;
        if (!env_cfg.empty()) {
            cfg = env_cfg;
        } else {
            std::string xdg = getenv_str("XDG_RUNTIME_DIR", "/run/user/" + std::to_string(getuid()));
            cfg = fs::path(xdg) / "olympium-openharmony/session.json";
        }
        struct stat st {};
        if (stat(cfg.c_str(), &st) != 0) {
            throw std::runtime_error("Start and configure a local OpenHarmony SDK session first.");
        }
        if (!S_ISREG(st.st_mode) || st.st_uid != getuid() || (st.st_mode & 0077) != 0) {
            throw std::runtime_error("The SDK session descriptor must be private to the desktop user.");
        }
        config = fs::canonical(cfg);
        Json values = Json::parse(read_file_bytes(config));
        hdc = values.at("hdc").sval;
        qmp = values.at("qmp").sval;
        target = values.at("target").sval;

        struct stat qst {};
        static const std::regex target_re(R"(127\.0\.0\.1:([1-9][0-9]{0,4}))");
        std::smatch tm;
        bool target_ok = std::regex_match(target, tm, target_re) && std::stoi(tm[1].str()) <= 65535;
        if (!hdc.is_absolute() || access(hdc.c_str(), X_OK) != 0 ||
            !qmp.is_absolute() || stat(qmp.c_str(), &qst) != 0 || !S_ISSOCK(qst.st_mode) ||
            qst.st_uid != getuid() || !target_ok) {
            throw std::runtime_error("Expected a local SDK emulator, QMP socket and HDC executable.");
        }
    }

    std::string command(const std::vector<std::string> &words, int timeout = 30, bool with_target = true) const {
        std::vector<std::string> argv = {hdc.string()};
        if (with_target) { argv.push_back("-t"); argv.push_back(target); }
        argv.insert(argv.end(), words.begin(), words.end());
        auto res = run_cmd(argv, timeout);
        if (res.code != 0) {
            std::string msg = trim(res.output);
            if (msg.size() > 240) msg.resize(240);
            throw std::runtime_error("SDK command failed: " + msg);
        }
        return res.output;
    }

    std::string shell(const std::vector<std::string> &words, int timeout = 30) const {
        std::vector<std::string> argv = {"shell"};
        argv.insert(argv.end(), words.begin(), words.end());
        return command(argv, timeout, true);
    }

    void connect() const {
        command({"tconn", target}, 15, false);
        auto targets = split_ws(command({"list", "targets"}, 15, false));
        if (std::find(targets.begin(), targets.end(), target) == targets.end()) {
            throw std::runtime_error("The OpenHarmony SDK guest is not connected.");
        }
    }

    std::optional<std::array<int, 4>> bounds(const std::string &bundle) const {
        std::string remote = "/data/local/tmp/olympium-" + random_hex(16) + ".json";
        char tmpl[] = "/tmp/olympium-oh-layout-XXXXXX";
        char *dir_c = mkdtemp(tmpl);
        if (!dir_c) return std::nullopt;
        fs::path dir(dir_c);
        struct Guard {
            const Session *self;
            std::string rem;
            fs::path d;
            ~Guard() {
                try { self->shell({"rm", "-f", rem}, 5); } catch (...) {}
                std::error_code ec;
                fs::remove_all(d, ec);
            }
        } guard{this, remote, dir};

        fs::path local = dir / "layout.json";
        shell({"uitest", "dumpLayout", "-p", remote}, 30);
        command({"file", "recv", remote, local.string()}, 30);
        Json root = Json::parse(read_file_bytes(local));
        std::vector<const Json *> todo = {&root};
        static const std::regex num_re(R"(-?\d+)");
        while (!todo.empty()) {
            const Json *node = todo.back();
            todo.pop_back();
            if (node->contains("attributes") && node->at("attributes").is_object()) {
                const Json &attrs = node->at("attributes");
                if (attrs.get_string("bundleName") == bundle &&
                    attrs.get_string("type") == "root" &&
                    attrs.get_string("visible") == "true") {
                    std::string bs = attrs.get_string("bounds");
                    std::vector<int> coords;
                    for (std::sregex_iterator it(bs.begin(), bs.end(), num_re), end; it != end; ++it) {
                        coords.push_back(std::stoi(it->str()));
                    }
                    if (coords.size() == 4 &&
                        0 <= coords[0] && coords[0] < coords[2] && coords[2] <= 360 &&
                        0 <= coords[1] && coords[1] < coords[3] && coords[3] <= 720) {
                        return std::array<int, 4>{coords[0], coords[1], coords[2], coords[3]};
                    }
                }
            }
            if (node->contains("children") && node->at("children").is_array()) {
                const auto &ch = node->at("children").aval;
                for (auto it = ch.rbegin(); it != ch.rend(); ++it) todo.push_back(&(*it));
            }
        }
        return std::nullopt;
    }
};

static Json do_install(const Session &session, const fs::path &source, const fs::path &root) {
    if (!fs::is_regular_file(source)) throw std::runtime_error("Expected a regular HAP file.");
    fs::path oh_dir = root / "openharmony";
    fs::create_directories(oh_dir);
    chmod(oh_dir.c_str(), 0700);
    std::string tmpl = (oh_dir / ".install-XXXXXX").string();
    std::vector<char> tbuf(tmpl.begin(), tmpl.end());
    tbuf.push_back('\0');
    char *sdir = mkdtemp(tbuf.data());
    if (!sdir) throw std::runtime_error("mkdtemp failed");
    fs::path tmpdir(sdir);
    struct DirGuard { fs::path d; ~DirGuard() { std::error_code ec; fs::remove_all(d, ec); } } dg{tmpdir};

    fs::path staged = tmpdir / source.filename();
    fs::copy_file(source, staged, fs::copy_options::overwrite_existing);
    Json info = package_info(staged, root);
    std::string bundle = info.get_string("package");
    fs::path saved = oh_dir / (bundle + ".hap");
    if (fs::exists(saved) && file_digest(saved, "sha256") != info.get_string("sha")) {
        throw std::runtime_error("This bundle is already in the library with different contents.");
    }
    std::string remote = "/data/local/tmp/olympium-" + random_hex(16) + ".hap";
    struct RemGuard {
        const Session *s;
        std::string r;
        ~RemGuard() { try { s->shell({"rm", "-f", r}, 5); } catch (...) {} }
    } rg{&session, remote};

    std::string installed;
    try { installed = session.shell({"bm", "dump", "-n", bundle}); } catch (...) {}
    if (installed.find(bundle) != std::string::npos &&
        installed.find(info.get_string("ability")) != std::string::npos &&
        fs::exists(saved)) {
        info["path"] = saved.string();
        return info;
    }
    session.command({"file", "send", staged.string(), remote}, 120);
    std::string result = session.shell({"bm", "install", "-p", remote}, 120);
    if (to_lower(result).find("install bundle successfully") == std::string::npos) {
        std::string msg = trim(result);
        if (msg.size() > 240) msg.resize(240);
        throw std::runtime_error("Bundle Manager rejected the HAP: " + msg);
    }
    fs::rename(staged, saved);
    info["path"] = saved.string();
    return info;
}

static fs::path find_window_helper(const char *argv0) {
    fs::path self = fs::weakly_canonical(argv0);
    fs::path sibling = self.parent_path() / "openharmony-window";
    if (access(sibling.c_str(), X_OK) == 0) return sibling;
    if (access("/usr/libexec/dev-desktop/openharmony-window", X_OK) == 0) {
        return "/usr/libexec/dev-desktop/openharmony-window";
    }
    throw std::runtime_error("openharmony-window binary is not installed.");
}

static void do_launch(const Session &session, const std::string &bundle, const fs::path &root,
                      const std::string &title, const char *argv0) {
    fs::path saved = root / "openharmony" / (bundle + ".hap");
    Json info = package_info(saved, root);
    if (info.get_string("package") != bundle) {
        throw std::runtime_error("Library HAP package does not match its launcher.");
    }
    std::string result = session.shell({"aa", "start", "-a", info.get_string("ability"), "-b", bundle});
    if (to_lower(result).find("start ability successfully") == std::string::npos) {
        std::string msg = trim(result);
        if (msg.size() > 240) msg.resize(240);
        throw std::runtime_error("Ability Manager could not open the application: " + msg);
    }
    struct StopGuard {
        const Session *s;
        std::string b;
        ~StopGuard() {
            try { s->shell({"aa", "force-stop", b}, 5); } catch (...) {}
            std::printf("OpenHarmony app stopped\n");
            std::fflush(stdout);
        }
    } sg{&session, bundle};

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    std::optional<std::array<int, 4>> found;
    while (std::chrono::steady_clock::now() < deadline) {
        try { found = session.bounds(bundle); } catch (...) { found = std::nullopt; }
        if (found) break;
        sleep(1);
    }
    if (!found) throw std::runtime_error("The guest did not show an application-owned window.");

    fs::path win_bin = find_window_helper(argv0);
    int rc = run_passthrough({
        win_bin.string(),
        "--qmp", session.qmp.string(),
        "--hdc", session.hdc.string(),
        "--target", session.target,
        "--title", title.empty() ? info.get_string("name") : title,
        "--bounds",
        std::to_string((*found)[0]), std::to_string((*found)[1]),
        std::to_string((*found)[2]), std::to_string((*found)[3]),
    });
    if (rc != 0) throw std::runtime_error("OpenHarmony window exited with status " + std::to_string(rc));
}

int main(int argc, char **argv) {
    std::string cmd, value, root_str, title;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--root" && i + 1 < argc) root_str = argv[++i];
        else if (a == "--title" && i + 1 < argc) title = argv[++i];
        else if (cmd.empty()) cmd = a;
        else if (value.empty()) value = a;
        else {
            std::fprintf(stderr, "Usage: olympium-openharmony {inspect|install|launch|remove} VALUE --root DIR [--title TITLE]\n");
            return 2;
        }
    }
    if ((cmd != "inspect" && cmd != "install" && cmd != "launch" && cmd != "remove") ||
        value.empty() || root_str.empty()) {
        std::fprintf(stderr, "Usage: olympium-openharmony {inspect|install|launch|remove} VALUE --root DIR [--title TITLE]\n");
        return 2;
    }

    fs::path root = fs::weakly_canonical(root_str);
    try {
        Session session;
        if (cmd == "inspect") {
            std::printf("%s\n", package_info(fs::path(value), root).dump().c_str());
            return 0;
        }
        if ((cmd == "launch" || cmd == "remove") && !is_valid_bundle(value)) {
            throw std::runtime_error("Invalid OpenHarmony bundle name.");
        }
        fs::path lock_path = session.config;
        lock_path.replace_extension(".lock");
        int lock_fd = open(lock_path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
        if (lock_fd < 0) throw std::runtime_error("Cannot open OpenHarmony session lock.");
        if (flock(lock_fd, LOCK_EX | LOCK_NB) != 0) {
            close(lock_fd);
            throw std::runtime_error("Another OpenHarmony operation is already using this SDK session.");
        }
        struct FdGuard { int fd; ~FdGuard() { if (fd >= 0) close(fd); } } fg{lock_fd};

        session.connect();
        if (cmd == "install") {
            std::printf("%s\n", do_install(session, fs::path(value), root).dump().c_str());
        } else if (cmd == "launch") {
            do_launch(session, value, root, title, argv[0]);
        } else if (cmd == "remove") {
            std::string res = session.shell({"bm", "uninstall", "-n", value}, 60);
            if (to_lower(res).find("uninstall bundle successfully") == std::string::npos) {
                std::string msg = trim(res);
                if (msg.size() > 240) msg.resize(240);
                throw std::runtime_error("Bundle Manager could not remove the application: " + msg);
            }
            std::error_code ec;
            fs::remove(root / "openharmony" / (value + ".hap"), ec);
            Json out = Json::object();
            out["ok"] = true;
            out["package"] = value;
            std::printf("%s\n", out.dump().c_str());
        }
        return 0;
    } catch (const std::exception &e) {
        Json err = Json::object();
        err["ok"] = false;
        err["message"] = std::string(e.what());
        std::printf("%s\n", err.dump().c_str());
        std::fprintf(stderr, "OpenHarmony: %s\n", e.what());
        return 3;
    }
}
