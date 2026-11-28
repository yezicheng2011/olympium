/*
 * Copyright 2026 Development Desktop contributors
 * SPDX-License-Identifier: Apache-2.0 — Licensed under the Apache License, Version 2.0. See LICENSE.
 *
 * Native C++20 APK metadata, installation, signed updates, rollback,
 * microG framework management, and app window launcher for Olympium.
 */
#include "android_core.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <regex>
#include <set>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

using namespace olympium;
using namespace olympium::android;

static fs::path g_runtime = "/opt/olympium/android";
static fs::path g_pins_path;
static std::optional<Json> g_managed_process;

static std::string find_tool(const std::string &name) {
    std::string upper = name;
    std::transform(upper.begin(), upper.end(), upper.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    std::string env = getenv_str(("OLYMPIUM_ANDROID_" + upper).c_str());
    std::string path = env;
    if (path.empty()) {
        if (name == "adb" && fs::is_regular_file(g_runtime / "platform-tools/adb")) {
            path = (g_runtime / "platform-tools/adb").string();
        } else if (name == "scrcpy" && fs::is_regular_file(g_runtime / "scrcpy/scrcpy")) {
            path = (g_runtime / "scrcpy/scrcpy").string();
        } else {
            path = find_in_path(name);
        }
    }
    if (path.empty() || access(path.c_str(), X_OK) != 0) {
        throw std::runtime_error("Android component is not installed: " + name);
    }
    return path;
}

static std::string run_tool(const std::vector<std::string> &argv, int timeout = 60, bool check = true,
                            const std::function<void()> &monitor = nullptr) {
    auto res = run_cmd(argv, timeout, {}, monitor);
    if (res.timed_out) throw std::runtime_error("Android tool timed out: " + argv[0]);
    if (check && res.code != 0) {
        std::string tail = res.output.size() > 2000 ? res.output.substr(res.output.size() - 2000) : res.output;
        throw std::runtime_error(tail.empty() ? "Android tool failed" : tail);
    }
    return res.output;
}

static int ready_timeout_sec() {
    int v = std::stoi(getenv_str("OLYMPIUM_ANDROID_READY_TIMEOUT", "600"));
    if (v < 60 || v > 3600) throw std::runtime_error("Android readiness timeout must be between 60 and 3600 seconds");
    return v;
}

static void managed_process_running() {
    if (!g_managed_process) return;
    try {
        auto current = process_identity(g_managed_process->at("pid").ival);
        if (current.to_json() == *g_managed_process && current.state != 'Z') return;
    } catch (...) {}
    throw std::runtime_error("Android emulator exited during this operation; see android/avd/emulator.log");
}

static std::string adb(const std::string &serial, const std::vector<std::string> &args,
                       int timeout = -1, bool check = true) {
    if (timeout < 0) {
        timeout = std::stoi(getenv_str("OLYMPIUM_ANDROID_TOOL_TIMEOUT", "60"));
        if (timeout < 15 || timeout > 600) {
            throw std::runtime_error("Android tool timeout must be between 15 and 600 seconds");
        }
    }
    std::function<void()> monitor;
    if (serial == "emulator-5560" && g_managed_process) monitor = managed_process_running;
    std::vector<std::string> argv = {find_tool("adb"), "-s", serial};
    argv.insert(argv.end(), args.begin(), args.end());

    bool read_only =
        (args.size() >= 2 && args[0] == "shell" &&
         (args[1] == "getprop" || args[1] == "sha256sum" || args[1] == "pidof")) ||
        (args.size() >= 3 && args[0] == "shell" &&
         ((args[1] == "pm" && args[2] == "path") || (args[1] == "id" && args[2] == "-u")));
    if (!read_only) return run_tool(argv, timeout, check, monitor);

    for (int attempt = 0; attempt < 6; attempt++) {
        std::string result;
        try {
            result = run_tool(argv, timeout, check, monitor);
        } catch (const std::runtime_error &e) {
            if (trim(e.what()) != "adb: device offline") throw;
            result = e.what();
        }
        if (trim(result) != "adb: device offline") return result;
        if (attempt < 5) sleep(1);
    }
    throw std::runtime_error("Android device offline while reading " + args[0]);
}

static std::string installed_digest(const std::string &serial, const std::string &package) {
    std::string out = adb(serial, {"shell", "pm", "path", package});
    static const std::regex path_re(R"(/[A-Za-z0-9_./+=@~-]+\.apk)");
    std::vector<std::string> bases;
    for (const auto &line : split_lines(out)) {
        if (line.rfind("package:", 0) == 0 && line.size() > 17 &&
            line.compare(line.size() - 9, 9, "/base.apk") == 0) {
            bases.push_back(line.substr(8));
        }
    }
    if (bases.size() != 1 || !std::regex_match(bases[0], path_re)) {
        throw std::runtime_error("No unique installed base APK");
    }
    auto words = split_ws(adb(serial, {"shell", "sha256sum", bases[0]}));
    if (words.empty()) throw std::runtime_error("Failed to read installed APK digest");
    return words[0];
}

static Json inspect_apk(const fs::path &apk_in, const fs::path &root) {
    fs::path apk = fs::canonical(apk_in);
    if (!fs::is_regular_file(apk) || fs::file_size(apk) > 1024ULL * 1024 * 1024) {
        throw std::runtime_error("Not a supported standalone APK");
    }
    std::set<std::string> abis;
    {
        ZipReader zr(apk);
        static const std::regex abi_re(R"(lib/([A-Za-z0-9_-]+)/[^/]+\.so)");
        for (const auto &e : zr.entries()) {
            std::smatch m;
            if (std::regex_match(e.name, m, abi_re)) abis.insert(m[1].str());
        }
        const ZipEntry *mf = zr.find("AndroidManifest.xml");
        if (!mf) throw std::runtime_error("APK has no Android manifest");
        if (mf->uncomp_size > 8ULL * 1024 * 1024) throw std::runtime_error("APK metadata exceeds the supported limit");
        if (const ZipEntry *res = zr.find("resources.arsc"); res && res->uncomp_size > 64ULL * 1024 * 1024) {
            throw std::runtime_error("APK metadata exceeds the supported limit");
        }
    }
    auto aapt_fn = [&](const std::vector<std::string> &args) -> std::string {
        std::vector<std::string> argv = {find_tool("aapt")};
        argv.insert(argv.end(), args.begin(), args.end());
        return run_tool(argv);
    };
    std::string badging = aapt_fn({"dump", "badging", apk.string()});
    static const std::regex pkg_re(R"(^package: name='([^']+)' versionCode='([^']+)' versionName='([^']*)'.*)");
    static const std::regex lbl_re(R"(^application-label:'(.*)'$)");
    static const std::regex sdk_re(R"(^sdkVersion:'(\d+)'$)");
    std::string pkg, ver_code_str, ver_name, label;
    int min_sdk = 1;
    for (const auto &line : split_lines(badging)) {
        std::smatch m;
        if (pkg.empty() && std::regex_match(line, m, pkg_re)) {
            pkg = m[1].str();
            ver_code_str = m[2].str();
            ver_name = m[3].str();
        } else if (label.empty() && std::regex_match(line, m, lbl_re)) {
            label = m[1].str();
        } else if (std::regex_match(line, m, sdk_re)) {
            min_sdk = std::stoi(m[1].str());
        }
    }
    if (pkg.empty() || !is_valid_package(pkg)) throw std::runtime_error("Invalid Android package identity");
    std::string name = label.empty() ? pkg : label;
    std::string clean;
    for (unsigned char c : name) if (c >= 0x20 && c != 0x7f) clean.push_back(static_cast<char>(c));
    clean = trim(clean);
    if (clean.size() > 120) {
        clean.resize(120);
        while (!clean.empty() && (static_cast<unsigned char>(clean.back()) & 0xC0) == 0x80) clean.pop_back();
    }
    if (clean.empty()) clean = pkg;
    std::string sha = file_digest(apk, "sha256");
    fs::path previews = root / "android/previews";
    fs::create_directories(previews);
    auto icon = apk_icon(apk, badging, aapt_fn, previews / sha);

    Json out = Json::object();
    out["ok"] = true;
    out["package"] = pkg;
    out["name"] = clean;
    out["version"] = ver_name;
    out["version_code"] = ver_code_str.empty() ? int64_t(0) : static_cast<int64_t>(std::stoll(ver_code_str));
    out["id"] = "android-" + bytes_digest(pkg, "sha256").substr(0, 16);
    out["sha"] = sha;
    out["icon"] = icon ? icon->string() : "";
    out["min_sdk"] = min_sdk;
    Json abi_arr = Json::array();
    for (const auto &a : abis) abi_arr.aval.push_back(Json(a));
    out["native_abis"] = std::move(abi_arr);
    out["needs_runtime_download"] =
        !fs::is_regular_file(g_runtime / "emulator/emulator") && getenv_str("OLYMPIUM_ANDROID_SERIAL").empty();
    return out;
}

static void start_runtime(const fs::path &root) {
    bool kvm_ok = access("/dev/kvm", R_OK | W_OK) == 0;
    std::string accel = kvm_ok ? "on" : "off";
    if (!kvm_ok && getenv_str("OLYMPIUM_ANDROID_ALLOW_TCG") != "1") {
        throw std::runtime_error(
            "Android 17 needs hardware virtualization. Enable it and allow this user to access KVM before installing Android support.");
    }
    fs::path avd_home = root / "android/avd";
    fs::create_directories(avd_home);
    FileLock lock(avd_home / "boot.lock");
    std::string devices = run_tool({find_tool("adb"), "devices"});
    if (std::regex_search(devices, std::regex(R"(emulator-5560\s)"))) return;
    if (!fs::is_regular_file(g_runtime / "emulator/emulator")) {
        ensure_runtime(g_runtime, g_pins_path);
    }
    fs::path avd = avd_home / "olympium-android17.avd";
    fs::create_directories(avd);
    clean_stale_avd_lock(avd);
    fs::path image = g_runtime / "system-images/android-37.0/default/x86_64";
    if (!fs::is_regular_file(image / "system.img")) {
        throw std::runtime_error("The Android system image is not installed");
    }
    write_file_bytes(avd_home / "olympium-android17.ini",
                     "avd.ini.encoding=UTF-8\npath=" + avd.string() + "\ntarget=android-37.0\n");
    write_file_bytes(avd / "config.ini",
        "avd.ini.encoding=UTF-8\nabi.type=x86_64\nhw.cpu.arch=x86_64\nhw.cpu.ncore=2\n"
        "hw.ramSize=2048\nhw.lcd.width=360\nhw.lcd.height=640\nhw.lcd.density=160\n"
        "hw.gpu.enabled=yes\nhw.gpu.mode=swiftshader_indirect\nhw.keyboard=yes\n"
        "image.sysdir.1=" + image.string() + "/\ndisk.dataPartition.size=6442450944\n"
        "showDeviceFrame=no\n");

    std::vector<std::string> argv = {
        (g_runtime / "emulator/emulator").string(),
        "-avd", "olympium-android17", "-port", "5560", "-no-window", "-no-audio",
        "-no-snapshot", "-no-boot-anim", "-no-metrics", "-gpu", "swiftshader_indirect",
        "-feature", "-Vulkan", "-accel", accel
    };
    if (getenv_str("OLYMPIUM_ANDROID_LEGACY_WIFI") == "1") {
        argv.push_back("-feature");
        argv.push_back("-WiFiPacketStream");
    }
    if (!kvm_ok && getenv_str("OLYMPIUM_ANDROID_TCG_ICOUNT") == "1") {
        argv.insert(argv.end(), {"-cores", "1", "-memory", "4096", "-qemu", "-icount", "shift=0,align=off,sleep=off"});
    }
    int logfd = open((avd_home / "emulator.log").c_str(), O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0600);
    if (logfd < 0) throw std::runtime_error("Cannot open emulator.log");
    pid_t pid = fork();
    if (pid == 0) {
        setsid();
        int nul = open("/dev/null", O_RDONLY);
        if (nul >= 0) { dup2(nul, 0); if (nul > 2) close(nul); }
        dup2(logfd, 1);
        dup2(logfd, 2);
        if (logfd > 2) close(logfd);
        setenv("ANDROID_AVD_HOME", avd_home.c_str(), 1);
        setenv("ANDROID_SDK_ROOT", g_runtime.c_str(), 1);
        std::vector<char *> cargv;
        for (auto &a : argv) cargv.push_back(a.data());
        cargv.push_back(nullptr);
        execv(cargv[0], cargv.data());
        _exit(127);
    }
    close(logfd);
    if (pid < 0) throw std::runtime_error("Failed to start Android emulator");
    write_file_bytes(avd_home / "process.json", process_identity(pid).to_json().dump() + "\n");

    int wait_s = ready_timeout_sec();
    for (int i = 0; i < wait_s; i++) {
        int st = 0;
        if (waitpid(pid, &st, WNOHANG) == pid) {
            throw std::runtime_error("Android emulator exited; see android/avd/emulator.log");
        }
        if (std::regex_search(run_tool({find_tool("adb"), "devices"}), std::regex(R"(emulator-5560\s)"))) {
            return;
        }
        sleep(1);
    }
    throw std::runtime_error("The Android emulator did not start; see android/avd/emulator.log");
}

static std::string ensure_device(const fs::path &root, const std::string &selected = "") {
    std::string serial = !selected.empty() ? selected : getenv_str("OLYMPIUM_ANDROID_SERIAL", "emulator-5560");
    static const std::regex serial_re(R"([A-Za-z0-9_.:\[\]-]+)");
    if (!std::regex_match(serial, serial_re)) throw std::runtime_error("Invalid configured Android device");
    std::string devices = run_tool({find_tool("adb"), "devices"});
    std::regex present_re(R"((?:^|\n))" + regex_escape(serial) + R"(\s+(?:device|offline)\b)");
    if (!std::regex_search(devices, present_re)) {
        if (serial != "emulator-5560") {
            throw std::runtime_error("The configured Android runtime is not running: " + serial);
        }
        start_runtime(root);
    }
    if (serial == "emulator-5560" && getenv_str("OLYMPIUM_ANDROID_SERIAL").empty()) {
        g_managed_process = owned_runtime(root, g_runtime, serial);
    }
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(ready_timeout_sec());
    while (std::chrono::steady_clock::now() < deadline) {
        try {
            if (trim(adb(serial, {"shell", "getprop", "sys.boot_completed"})) == "1") {
                std::string svc = adb(serial, {"shell", "service", "check", "package"});
                if (svc.find("found") != std::string::npos && svc.find("not found") == std::string::npos) {
                    std::string vols = adb(serial, {"shell", "sm", "list-volumes", "private"});
                    std::string user = trim(adb(serial, {"shell", "am", "get-started-user-state", "0"}));
                    if (std::regex_search(vols, std::regex(R"((?:^|\n)private\s+mounted\b)")) &&
                        user == "RUNNING_UNLOCKED") {
                        return serial;
                    }
                }
            }
        } catch (...) {
            if (serial == "emulator-5560" && g_managed_process) managed_process_running();
        }
        sleep(2);
    }
    throw std::runtime_error("Android did not become ready before the configured deadline");
}

static Json prepare_framework_overlay(const std::string &serial, const fs::path &root) {
    Json identity = owned_runtime(root, g_runtime, serial);
    auto same_runtime = [&]() {
        try { return owned_runtime(root, g_runtime, serial) == identity; }
        catch (...) { return false; }
    };
    fs::path assets = getenv_str("OLYMPIUM_ANDROID_COMPAT_ASSETS", "/usr/libexec/dev-desktop/android-compat");
    Json manifest = Json::parse(read_file_bytes(assets / "assets.json"));
    for (const char *name : {"helper.dex", "mount-helper", "health.apk", "profile.json"}) {
        if (file_digest(assets / name, "sha256") != manifest.get_string(name)) {
            throw std::runtime_error(std::string("Android support asset checksum mismatch: ") + name);
        }
    }
    Json profile = Json::parse(read_file_bytes(assets / "profile.json"));
    fs::path services = root / "android/services";
    fs::path state_file = services / "framework.json";

    auto shell = [&](const std::vector<std::string> &args, int timeout = -1, bool check = true) {
        std::vector<std::string> full = {"shell"};
        full.insert(full.end(), args.begin(), args.end());
        return adb(serial, full, timeout, check);
    };
    auto server_pid = [&]() { return trim(shell({"pidof", "system_server"})); };
    std::string pid = server_pid();
    if (pid.empty() || !std::all_of(pid.begin(), pid.end(), [](unsigned char c) { return std::isdigit(c); })) {
        throw std::runtime_error("Managed Android system server is not ready");
    }
    Json cache_key = Json::object();
    cache_key["process"] = identity;
    cache_key["system_server"] = pid;
    cache_key["assets"] = manifest;
    if (fs::is_regular_file(state_file)) {
        Json cached = Json::parse(read_file_bytes(state_file));
        if (cached.contains("identity") && cached.at("identity") == cache_key &&
            cached.get_bool("basic_services_verified")) {
            return cached;
        }
    }
    // Never restart Android's Java runtime while scrcpy is active for this serial.
    std::error_code ec;
    for (const auto &ent : fs::directory_iterator("/proc", ec)) {
        std::string dn = ent.path().filename().string();
        if (dn.empty() || !std::all_of(dn.begin(), dn.end(), [](unsigned char c) { return std::isdigit(c); })) continue;
        try {
            std::string raw = read_file_bytes(ent.path() / "cmdline");
            std::vector<std::string> words;
            size_t pos = 0;
            while (pos < raw.size()) {
                size_t nul = raw.find('\0', pos);
                words.push_back(nul == std::string::npos ? raw.substr(pos) : raw.substr(pos, nul - pos));
                if (nul == std::string::npos) break;
                pos = nul + 1;
            }
            if (!words.empty() && fs::path(words[0]).filename() == "scrcpy" &&
                std::find(words.begin(), words.end(), serial) != words.end()) {
                throw std::runtime_error("Close Android app windows before updating Android Services");
            }
        } catch (const std::runtime_error &e) {
            if (std::string(e.what()).find("Close Android") != std::string::npos) throw;
        }
    }
    std::string btype = trim(shell({"getprop", "ro.build.type"}));
    if (trim(shell({"getprop", "ro.build.version.sdk"})) != "37" || (btype != "eng" && btype != "userdebug")) {
        throw std::runtime_error("This framework profile requires the pinned AOSP API 37 development image");
    }

    fs::path build = services / profile.at("profile_id").sval;
    fs::path jar = build / "services.jar";
    fs::path built = build / "result.json";
    Json build_rec = fs::is_regular_file(built) ? Json::parse(read_file_bytes(built)) : Json::object();
    if (!fs::is_regular_file(jar) || file_digest(jar, "sha256") != build_rec.get_string("sha256") ||
        build_rec.get_string("helper") != manifest.get_string("helper.dex")) {
        std::string tmpl = (services / "framework-XXXXXX").string();
        std::vector<char> tbuf(tmpl.begin(), tmpl.end());
        tbuf.push_back('\0');
        char *wdir = mkdtemp(tbuf.data());
        if (!wdir) throw std::runtime_error("mkdtemp failed");
        fs::path work(wdir);
        struct WGuard { fs::path p; ~WGuard() { std::error_code e; fs::remove_all(p, e); } } wg{work};
        fs::path orig = work / "original.jar";
        adb(serial, {"pull", "/system/framework/services.jar", orig.string()}, 240);
        fs::path compiled = patch_services(orig, assets / "helper.dex", work / "compile", profile);
        fs::create_directories(build);
        fs::rename(compiled, jar);
        Json rec = Json::object();
        rec["sha256"] = file_digest(jar, "sha256");
        rec["helper"] = manifest.get_string("helper.dex");
        write_file_bytes(built, rec.dump() + "\n");
    }

    std::string expected = file_digest(jar, "sha256");
    const std::string remote = "/data/misc/olympium-framework";
    bool mounted = false;
    bool rooted = false;
    auto unroot = [&]() {
        if (rooted && same_runtime()) {
            adb(serial, {"unroot"}, 120, false);
            adb(serial, {"wait-for-device"}, 120);
            auto dl = std::chrono::steady_clock::now() + std::chrono::seconds(20);
            while (std::chrono::steady_clock::now() < dl) {
                if (!same_runtime()) throw std::runtime_error("Managed Android changed while restoring its ADB shell");
                if (trim(shell({"id", "-u"}, 15)) == "2000") return;
                sleep(1);
            }
            throw std::runtime_error("Managed Android did not return to its unprivileged ADB shell");
        }
    };

    try {
        if (!same_runtime()) throw std::runtime_error("Managed Android emulator exited or changed before setup");
        adb(serial, {"root"}, 120);
        rooted = true;
        adb(serial, {"wait-for-device"}, 120);
        if (trim(shell({"id", "-u"})) != "0") {
            throw std::runtime_error("Managed AOSP runtime did not provide its development root service");
        }
        auto sha_words = split_ws(shell({"sha256sum", "/proc/1/root/system/framework/services.jar"}, 240));
        std::string cur_sha = sha_words.empty() ? "" : sha_words[0];
        if (cur_sha != expected && cur_sha != profile.at("original_services_jar_sha256").sval) {
            throw std::runtime_error("Managed Android framework changed outside this support profile");
        }
        if (cur_sha != expected) {
            shell({"mkdir", "-p", remote});
            shell({"chmod", "0700", remote});
            adb(serial, {"push", jar.string(), remote + "/services.jar"}, 240);
            adb(serial, {"push", (assets / "mount-helper").string(), remote + "/mount-helper"}, 120);
            shell({"chmod", "0444", remote + "/services.jar"});
            shell({"chmod", "0700", remote + "/mount-helper"});
            shell({"chcon", "u:object_r:system_file:s0", remote + "/services.jar"});
            auto staged = split_ws(shell({"sha256sum", remote + "/services.jar"}));
            if (staged.empty() || staged[0] != expected) {
                throw std::runtime_error("Staged Android framework checksum mismatch");
            }
            shell({remote + "/mount-helper", "install"});
            mounted = true;
            shell({"setprop", "ctl.restart", "zygote"});
            auto dl = std::chrono::steady_clock::now() + std::chrono::seconds(ready_timeout_sec());
            bool restarted = false;
            while (std::chrono::steady_clock::now() < dl) {
                try {
                    std::string np = server_pid();
                    if (!np.empty() && np != pid &&
                        trim(shell({"getprop", "sys.boot_completed"})) == "1" &&
                        shell({"sm", "list-volumes", "private"}).find("private mounted") != std::string::npos &&
                        trim(shell({"am", "get-started-user-state", "0"})) == "RUNNING_UNLOCKED") {
                        restarted = true;
                        break;
                    }
                } catch (...) {
                    if (!same_runtime()) throw std::runtime_error("Managed Android emulator exited or changed during setup");
                }
                sleep(2);
            }
            if (!restarted) throw std::runtime_error("Android Services restart did not complete");
        }
        if (trim(shell({"getenforce"})) != "Enforcing") {
            throw std::runtime_error("Managed Android SELinux enforcement changed");
        }
        int overlay_count = 0;
        bool overlay_ok = false;
        for (const auto &line : split_lines(shell({"cat", "/proc/1/mountinfo"}))) {
            auto row = split_ws(line);
            if (row.size() > 6 && row[4] == "/system/framework/services.jar") {
                overlay_count++;
                std::set<std::string> opts;
                size_t p = 0;
                while (p <= row[5].size()) {
                    size_t c = row[5].find(',', p);
                    opts.insert(c == std::string::npos ? row[5].substr(p) : row[5].substr(p, c - p));
                    if (c == std::string::npos) break;
                    p = c + 1;
                }
                overlay_ok = opts.count("ro") && opts.count("nosuid") && opts.count("nodev");
            }
        }
        if (overlay_count != 1 || !overlay_ok) {
            throw std::runtime_error("Android Services framework mount is not read-only and restricted");
        }

        const std::string health = "org.olympium.android.health";
        Json prev = fs::is_regular_file(state_file) ? Json::parse(read_file_bytes(state_file)) : Json::object();
        std::string present = shell({"pm", "path", health}, -1, false);
        if (present.find("package:") != std::string::npos) {
            std::string hsha = installed_digest(serial, health);
            if (hsha != manifest.get_string("health.apk")) {
                if (hsha != prev.get_string("health_apk_sha256")) {
                    throw std::runtime_error("A different Android Services health package is installed");
                }
                adb(serial, {"uninstall", health}, 180);
                present.clear();
            }
        }
        if (present.find("package:") == std::string::npos) {
            adb(serial, {"install", "--no-streaming", (assets / "health.apk").string()}, 600);
        }
        if (installed_digest(serial, health) != manifest.get_string("health.apk")) {
            throw std::runtime_error("Android Services health package checksum mismatch");
        }
        if (shell({"pm", "clear", health}).find("Success") == std::string::npos) {
            throw std::runtime_error("Could not clear stale Android Services health data");
        }
        shell({"am", "start", "--user", "0", "--display", "0", "-n", health + "/.Probe"}, 180);
        auto dl = std::chrono::steady_clock::now() + std::chrono::seconds(240);
        std::optional<Json> probe;
        while (std::chrono::steady_clock::now() < dl) {
            std::string txt = shell({"run-as", health, "cat", "files/result.json"}, -1, false);
            try { probe = Json::parse(txt); break; } catch (...) { sleep(2); }
        }
        if (!probe) throw std::runtime_error("No fresh Android Services health result");
        if (probe->get_string("caller_package") != health || probe->get_int("caller_uid") < 10000 ||
            probe->get_int("android_api") != 37 ||
            !probe->get_bool("probe_completed") ||
            !probe->get_bool("signature_compatibility_verified") ||
            !probe->get_bool("companion_signature_compatibility_verified") ||
            !probe->get_bool("gms_service_handshake_verified")) {
            throw std::runtime_error("Android Services signature or Binder health check failed");
        }
        shell({"am", "force-stop", health});
        cache_key["system_server"] = server_pid();
        Json state = Json::object();
        state["identity"] = cache_key;
        state["profile"] = profile.at("profile_id").sval;
        state["framework_sha256"] = expected;
        state["health_apk_sha256"] = manifest.get_string("health.apk");
        state["basic_services_verified"] = true;
        state["gms_api_compatibility_verified"] = false;
        state["probe"] = *probe;
        save_json(state_file, state);
        unroot();
        return state;
    } catch (...) {
        if (mounted && same_runtime()) {
            try {
                shell({remote + "/mount-helper", "restore"});
                shell({"setprop", "ctl.restart", "zygote"});
            } catch (...) {}
        }
        try { unroot(); } catch (...) {}
        throw;
    }
}

static Json prepare_services(const std::string &serial, const fs::path &root) {
    if (serial == "emulator-5560" && getenv_str("OLYMPIUM_ANDROID_SERIAL").empty()) {
        owned_runtime(root, g_runtime, serial);
    }
    fs::path services = root / "android/services";
    fs::create_directories(services);
    FileLock lock(services / "operation.lock");
    fs::path cache = services / "downloads";
    fs::create_directories(cache);

    Json pins = Json::parse(read_file_bytes(g_pins_path));
    Json records = Json::array();
    for (const auto &artifact : pins.at("artifacts").aval) {
        if (artifact.get_string("format") != "apk") continue;
        std::string dest_rel = artifact.at("destination").sval;
        std::string pkg = fs::path(dest_rel).stem().string();
        fs::path src = g_runtime / dest_rel;
        if (!fs::is_regular_file(src)) src = download_artifact(artifact, cache);
        if (file_digest(src, "sha256") != artifact.at("checksum").sval) {
            throw std::runtime_error("microG artifact checksum mismatch");
        }
        std::string present = adb(serial, {"shell", "pm", "path", pkg}, -1, false);
        if (present.find("package:") != std::string::npos) {
            if (installed_digest(serial, pkg) != artifact.at("checksum").sval) {
                throw std::runtime_error("Another implementation of " + pkg +
                    " is installed. Use the AOSP Android 17 runtime for microG; its signer will not be replaced.");
            }
        } else {
            fs::path apk = services / (pkg + ".apk");
            fs::copy_file(src, apk, fs::copy_options::overwrite_existing);
            std::string res = adb(serial, {"install", "--no-streaming", apk.string()}, 600);
            if (res.find("Success") == std::string::npos || installed_digest(serial, pkg) != artifact.at("checksum").sval) {
                throw std::runtime_error("Android did not install the reviewed microG package");
            }
        }
        Json r = Json::object();
        r["package"] = pkg;
        r["sha256"] = artifact.at("checksum").sval;
        records.aval.push_back(std::move(r));
    }
    std::string perms = adb(serial, {"shell", "pm", "list", "permissions", "-f"});
    Json state = Json::object();
    state["packages"] = std::move(records);
    state["installed"] = true;
    state["signature_permission_declared"] = perms.find("android.permission.FAKE_PACKAGE_SIGNATURE") != std::string::npos;
    state["signature_compatibility_verified"] = false;
    state["gms_api_compatibility_verified"] = false;
    if (serial == "emulator-5560" && getenv_str("OLYMPIUM_ANDROID_SERIAL").empty()) {
        Json fw = prepare_framework_overlay(serial, root);
        state["signature_compatibility_verified"] = fw.get_bool("basic_services_verified");
        state["basic_services_verified"] = fw.get_bool("basic_services_verified");
        state["framework_profile"] = fw.get_string("profile");
    }
    save_json(services / "installed.json", state);
    return state;
}

static Json validate_installation(const Json &info, const fs::path &dest, const std::string &package) {
    static const std::regex sha_re(R"([0-9a-f]{64})");
    std::string sha = info.get_string("sha");
    if (info.get_string("package") != package || !std::regex_match(sha, sha_re)) {
        throw std::runtime_error("Android installation record has changed");
    }
    std::string expected_id = "android-" + bytes_digest(package, "sha256").substr(0, 16);
    if (info.get_string("id") != expected_id) {
        throw std::runtime_error("Android application identity has changed");
    }
    fs::path saved(info.get_string("path"));
    if (saved.parent_path() != dest || saved.filename() != (sha + ".apk") || file_digest(saved, "sha256") != sha) {
        throw std::runtime_error("Saved APK identity has changed");
    }
    return info;
}

static Json load_installation(const fs::path &root, const std::string &package) {
    fs::path dest = root / "android/packages" / package;
    return validate_installation(Json::parse(read_file_bytes(dest / "installed.json")), dest, package);
}

static void recover_pending_update(const fs::path &root, const std::string &package, const std::string &serial) {
    fs::path dest = root / "android/packages" / package;
    fs::path latest = dest / "updates/latest.json";
    if (fs::is_regular_file(latest)) {
        Json rec = Json::parse(read_file_bytes(latest));
        if (rec.get_string("status") == "rolling-back") {
            Json before = validate_installation(rec.at("before"), dest, package);
            Json after = validate_installation(rec.at("after"), dest, package);
            if (before.get_string("serial") != serial || after.get_string("serial") != serial) {
                throw std::runtime_error("Pending rollback belongs to another Android runtime");
            }
            std::string actual = installed_digest(serial, package);
            if (actual == before.get_string("sha")) {
                save_json(dest / "installed.json", before);
                rec["status"] = "rolled-back";
            } else if (actual == after.get_string("sha")) {
                rec["status"] = "installed";
            } else {
                throw std::runtime_error("Pending rollback does not match the installed APK");
            }
            save_json(latest, rec);
        }
    }
    fs::path pending = dest / "updates/pending.json";
    if (!fs::is_regular_file(pending)) return;
    Json tx = Json::parse(read_file_bytes(pending));
    Json before = validate_installation(tx.at("before"), dest, package);
    Json after = validate_installation(tx.at("after"), dest, package);
    if (before.get_string("serial") != serial || after.get_string("serial") != serial) {
        throw std::runtime_error("Pending update belongs to another Android runtime");
    }
    std::string actual = installed_digest(serial, package);
    if (actual == before.get_string("sha")) {
        tx["status"] = "not-installed";
        save_json(pending, tx);
        fs::rename(pending, pending.parent_path() / ("failed-" + after.get_string("sha") + ".json"));
    } else if (actual == after.get_string("sha")) {
        save_json(dest / "installed.json", after);
        tx["status"] = "installed";
        tx["rollback_requested"] = true;
        save_json(pending.parent_path() / "latest.json", tx);
        fs::remove(pending);
    } else {
        throw std::runtime_error("Pending update does not match the installed APK; review is required");
    }
}

static Json do_install(const fs::path &apk, const fs::path &root, bool update_only) {
    Json info = inspect_apk(apk, root);
    std::string pkg = info.at("package").sval;
    fs::path dest = root / "android/packages" / pkg;
    std::optional<Json> prev;
    if (fs::is_regular_file(dest / "installed.json")) prev = load_installation(root, pkg);
    if (update_only && !prev) throw std::runtime_error("Updates require an existing managed application");
    std::string serial = ensure_device(root, prev ? prev->get_string("serial") : "");
    int sdk = std::stoi(trim(adb(serial, {"shell", "getprop", "ro.build.version.sdk"})));
    if ((sdk != 29 && sdk != 37) || info.get_int("min_sdk") > sdk) {
        throw std::runtime_error("This app needs a different Android version; configure the Android 17 runtime");
    }
    std::set<std::string> native_abis;
    for (const auto &a : info.at("native_abis").aval) native_abis.insert(a.sval);
    if (!native_abis.empty()) {
        std::string rep = trim(adb(serial, {"shell", "getprop", "ro.product.cpu.abilist"}));
        if (rep.empty()) rep = trim(adb(serial, {"shell", "getprop", "ro.product.cpu.abi"}));
        std::set<std::string> supported;
        static const std::regex abi_re(R"([A-Za-z0-9_-]+)");
        size_t p = 0;
        while (p <= rep.size()) {
            size_t c = rep.find(',', p);
            std::string a = c == std::string::npos ? rep.substr(p) : rep.substr(p, c - p);
            if (std::regex_match(a, abi_re)) supported.insert(a);
            if (c == std::string::npos) break;
            p = c + 1;
        }
        bool match = supported.empty();
        for (const auto &a : native_abis) if (supported.count(a)) { match = true; break; }
        if (!match) throw std::runtime_error("APK native ABI is not supported by the selected Android device");
    }
    if (sdk == 37) prepare_services(serial, root);
    fs::create_directories(dest);
    FileLock lock(dest / "operation.lock");
    recover_pending_update(root, pkg, serial);
    prev = fs::is_regular_file(dest / "installed.json") ? std::optional<Json>(load_installation(root, pkg)) : std::nullopt;
    if (update_only && !prev) throw std::runtime_error("The managed application was removed during update preparation");

    bool adopting = false;
    if (prev) {
        std::string actual = installed_digest(serial, pkg);
        if (prev->get_string("serial") != serial) throw std::runtime_error("Android application belongs to another runtime");
        if (actual != prev->get_string("sha")) {
            if (!update_only && actual == info.get_string("sha")) {
                prev.reset();
                adopting = true;
            } else {
                throw std::runtime_error("Android application changed outside this installer; review it before updating");
            }
        }
    }
    if (prev) {
        if (prev->get_string("sha") == info.get_string("sha")) {
            Json same = *prev;
            same["unchanged"] = true;
            return same;
        }
        if (sdk != 37) throw std::runtime_error("Managed APK updates currently require the Android 17 runtime");
        int64_t old_code = prev->contains("version_code") ? prev->get_int("version_code")
                                                          : inspect_apk(fs::path(prev->get_string("path")), root).get_int("version_code");
        (*prev)["version_code"] = old_code;
        if (info.get_int("version_code") <= old_code) {
            throw std::runtime_error("An update must have a newer Android version code");
        }
    }

    fs::path staged = dest / (".stage-" + random_hex(8) + ".apk");
    struct StagedGuard { fs::path p; ~StagedGuard() { std::error_code e; fs::remove(p, e); } } sg{staged};
    fs::copy_file(apk, staged, fs::copy_options::overwrite_existing);
    if (file_digest(staged, "sha256") != info.get_string("sha")) {
        throw std::runtime_error("APK changed while it was copied");
    }
    fs::path saved = dest / (info.get_string("sha") + ".apk");
    fs::copy_file(staged, saved, fs::copy_options::overwrite_existing);
    info["path"] = saved.string();
    info["serial"] = serial;
    info["needs_runtime_download"] = false;

    fs::path history = dest / "updates";
    std::optional<Json> tx;
    if (prev) {
        fs::create_directories(history);
        Json t = Json::object();
        t["status"] = "installing";
        t["before"] = *prev;
        t["after"] = info;
        save_json(history / "pending.json", t);
        tx = std::move(t);
    }
    std::string result;
    try {
        if (adopting) {
            result = "Success";
        } else {
            std::vector<std::string> args = {"install", "--no-streaming", "-r"};
            if (prev) { args.push_back("--enable-rollback"); args.push_back("0"); }
            args.push_back(staged.string());
            result = adb(serial, args, 600);
        }
    } catch (const std::exception &e) {
        if (tx) {
            (*tx)["status"] = "failed";
            (*tx)["error"] = std::string(e.what());
            save_json(history / "pending.json", *tx);
        }
        throw;
    }
    if (result.find("Success") == std::string::npos || installed_digest(serial, pkg) != info.get_string("sha")) {
        throw std::runtime_error("Android did not install the selected APK bytes");
    }
    save_json(dest / "installed.json", info);
    if (tx) {
        (*tx)["status"] = "installed";
        (*tx)["after"] = info;
        (*tx)["rollback_requested"] = true;
        save_json(history / "latest.json", *tx);
        std::error_code ec;
        fs::remove(history / "pending.json", ec);
    }
    return info;
}

static Json do_rollback(const std::string &package, const fs::path &root) {
    fs::path dest = root / "android/packages" / package;
    FileLock lock(dest / "operation.lock");
    Json current = load_installation(root, package);
    std::string serial = ensure_device(root, current.get_string("serial"));
    recover_pending_update(root, package, serial);
    current = load_installation(root, package);
    fs::path history = dest / "updates/latest.json";
    Json tx = Json::parse(read_file_bytes(history));
    if (tx.get_string("status") == "rolled-back" && tx.contains("before") &&
        tx.at("before").get_string("sha") == current.get_string("sha")) {
        if (installed_digest(serial, package) != current.get_string("sha")) {
            throw std::runtime_error("Android application changed after rollback");
        }
        current["rolled_back"] = true;
        current["unchanged"] = true;
        return current;
    }
    if (tx.get_string("status") != "installed" || !tx.contains("after") ||
        tx.at("after").get_string("sha") != current.get_string("sha")) {
        throw std::runtime_error("No managed update is ready for rollback");
    }
    Json previous = validate_installation(tx.at("before"), dest, package);
    if (installed_digest(serial, package) != current.get_string("sha")) {
        throw std::runtime_error("Android application changed outside this installer; rollback was refused");
    }
    tx["status"] = "rolling-back";
    save_json(history, tx);
    std::string res = adb(serial, {"shell", "pm", "rollback-app", package}, 600);
    if (res.find("Success") == std::string::npos || installed_digest(serial, package) != previous.get_string("sha")) {
        throw std::runtime_error("Android did not restore the previous APK; its installation record was retained");
    }
    save_json(dest / "installed.json", previous);
    tx["status"] = "rolled-back";
    save_json(history, tx);
    previous["rolled_back"] = true;
    return previous;
}

int main(int argc, char **argv) {
    umask(0077);
    fs::path self = fs::weakly_canonical(argv[0]);
    g_pins_path = self.parent_path() / "android-runtime-pins.json";
    if (!fs::is_regular_file(g_pins_path)) {
        g_pins_path = "/usr/libexec/dev-desktop/android-runtime-pins.json";
    }

    if (argc >= 2 && std::string(argv[1]) == "window") {
        std::string adb_bin, scrcpy_bin, serial, package, title = "Android application", out_dir;
        for (int i = 2; i < argc; i++) {
            std::string a = argv[i];
            if (a == "--adb" && i + 1 < argc) adb_bin = argv[++i];
            else if (a == "--scrcpy" && i + 1 < argc) scrcpy_bin = argv[++i];
            else if (a == "--serial" && i + 1 < argc) serial = argv[++i];
            else if (a == "--package" && i + 1 < argc) package = argv[++i];
            else if (a == "--title" && i + 1 < argc) title = argv[++i];
            else if (a == "--output" && i + 1 < argc) out_dir = argv[++i];
        }
        if (adb_bin.empty() || scrcpy_bin.empty() || serial.empty() || package.empty() || out_dir.empty()) {
            std::fprintf(stderr, "Usage: olympium-android window --adb BIN --scrcpy BIN --serial S --package P --output DIR [--title T]\n");
            return 2;
        }
        try {
            run_android_window(adb_bin, scrcpy_bin, serial, package, title, out_dir);
            return 0;
        } catch (const std::exception &e) {
            std::fprintf(stderr, "Android window: %s\n", e.what());
            return 1;
        }
    }

    std::string cmd, value, root_str;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--root" && i + 1 < argc) root_str = argv[++i];
        else if (cmd.empty()) cmd = a;
        else if (value.empty()) value = a;
        else {
            std::fprintf(stderr, "Usage: olympium-android {check|inspect|install|update|rollback|recover|launch|remove} [VALUE] [--root DIR]\n");
            return 2;
        }
    }

    try {
        if (cmd == "check") {
            for (const char *n : {"aapt", "adb", "scrcpy"}) find_tool(n);
            return 0;
        }
        if (cmd != "inspect" && cmd != "install" && cmd != "update" && cmd != "rollback" &&
            cmd != "recover" && cmd != "launch" && cmd != "remove") {
            std::fprintf(stderr, "Usage: olympium-android {check|inspect|install|update|rollback|recover|launch|remove} [VALUE] [--root DIR]\n");
            return 2;
        }
        if (root_str.empty() || root_str[0] != '/') {
            std::fprintf(stderr, "--root must be an absolute desktop data path\n");
            return 2;
        }
        fs::path root = fs::weakly_canonical(root_str);
        fs::create_directories(root);
        chmod(root.c_str(), 0700);
        struct stat st {};
        if (stat(root.c_str(), &st) != 0 || st.st_uid != getuid() || (st.st_mode & 0022) != 0) {
            throw std::runtime_error("Android app data must be owned by this user and not writable by other users");
        }
        std::string sdk_env = getenv_str("OLYMPIUM_ANDROID_SDK");
        if (!sdk_env.empty()) g_runtime = fs::weakly_canonical(sdk_env);
        else if (!fs::is_regular_file(g_runtime / "emulator/emulator")) g_runtime = root / "android/runtime";

        if (cmd == "inspect" || cmd == "install" || cmd == "update") {
            if (value.empty()) { std::fprintf(stderr, "APK path is required\n"); return 2; }
            Json info = (cmd == "inspect") ? inspect_apk(value, root) : do_install(value, root, cmd == "update");
            std::printf("%s\n", info.dump().c_str());
            return 0;
        }
        if (value.empty() || !is_valid_package(value)) {
            std::fprintf(stderr, "Android package name is required\n");
            return 2;
        }
        if (cmd == "rollback") {
            std::printf("%s\n", do_rollback(value, root).dump().c_str());
            return 0;
        }
        fs::path dest = root / "android/packages" / value;
        if (cmd == "recover") {
            FileLock lock(dest / "operation.lock");
            Json cur = load_installation(root, value);
            std::string serial = ensure_device(root, cur.get_string("serial"));
            recover_pending_update(root, value, serial);
            std::printf("%s\n", load_installation(root, value).dump().c_str());
            return 0;
        }
        if (cmd == "remove") {
            if (!fs::is_directory(dest)) throw std::runtime_error("Android app is not installed");
            FileLock lock(dest / "operation.lock");
            Json info = load_installation(root, value);
            std::string serial = ensure_device(root, info.get_string("serial"));
            if (installed_digest(serial, value) != info.get_string("sha")) {
                throw std::runtime_error("The Android app was changed outside this installer; reinstall it to update the record");
            }
            if (adb(serial, {"uninstall", value}, 180).find("Success") == std::string::npos) {
                throw std::runtime_error("Android refused to uninstall this app");
            }
            fs::remove(dest / "installed.json");
            static const std::regex apk_re(R"([0-9a-f]{64}\.apk)");
            std::error_code ec;
            for (const auto &ent : fs::directory_iterator(dest, ec)) {
                if (std::regex_match(ent.path().filename().string(), apk_re)) fs::remove(ent.path(), ec);
            }
            fs::path upd = dest / "updates";
            if (fs::is_directory(upd)) {
                fs::remove(upd / "pending.json", ec);
                fs::remove(upd / "latest.json", ec);
                for (const auto &ent : fs::directory_iterator(upd, ec)) {
                    std::string fn = ent.path().filename().string();
                    if (fn.rfind("failed-", 0) == 0 && fn.size() > 5 && fn.compare(fn.size() - 5, 5, ".json") == 0) {
                        fs::remove(ent.path(), ec);
                    }
                }
            }
            for (const char *sfx : {".png", ".svg"}) {
                fs::remove(root / "android/previews" / (info.get_string("sha") + sfx), ec);
            }
            Json out = Json::object();
            out["ok"] = true;
            out["package"] = value;
            std::printf("%s\n", out.dump().c_str());
            return 0;
        }
        // launch
        Json info = load_installation(root, value);
        std::string serial = ensure_device(root, info.get_string("serial"));
        if (installed_digest(serial, value) != info.get_string("sha")) {
            throw std::runtime_error("The Android app was changed outside this installer; reinstall it to update the record");
        }
        if (std::stoi(trim(adb(serial, {"shell", "getprop", "ro.build.version.sdk"}))) == 37) {
            prepare_services(serial, root);
        }
        run_android_window(find_tool("adb"), find_tool("scrcpy"), serial, value,
                           info.get_string("name"), dest / "window");
        return 0;
    } catch (const std::exception &e) {
        Json err = Json::object();
        err["ok"] = false;
        err["message"] = std::string(e.what());
        std::printf("%s\n", err.dump().c_str());
        std::fprintf(stderr, "Android: %s\n", e.what());
        return 3;
    }
}
