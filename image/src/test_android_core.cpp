/*
 * Copyright 2026 Development Desktop contributors
 * SPDX-License-Identifier: Apache-2.0 — Licensed under the Apache License, Version 2.0. See LICENSE.
 *
 * Native C++20 tests for Android runtime integrity, archive confinement,
 * bounded APK icons, root-task boundaries, process identity, and AVD locks.
 */
#include "android_core.hpp"

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

using namespace olympium;
using namespace olympium::android;

static void expect_throws(const std::function<void()> &fn, const std::string &substr, const char *label) {
    try {
        fn();
    } catch (const std::exception &e) {
        if (std::string(e.what()).find(substr) != std::string::npos) return;
        std::fprintf(stderr, "FAIL (%s): unexpected error: %s (want '%s')\n", label, e.what(), substr.c_str());
        std::exit(1);
    }
    std::fprintf(stderr, "FAIL (%s): expected exception containing '%s'\n", label, substr.c_str());
    std::exit(1);
}

int main() {
    char tmpl[] = "/tmp/test-android-core-XXXXXX";
    char *dir = mkdtemp(tmpl);
    assert(dir != nullptr);
    fs::path root(dir);
    struct Guard { fs::path p; ~Guard() { std::error_code ec; fs::remove_all(p, ec); } } g{root};
    fs::path stage = root / "stage";
    fs::create_directories(stage);

    // 1. Download verifies checksum & never poisons cache on corrupt download.
    {
        fs::path src = root / "publisher";
        write_file_bytes(src, "verified bytes");
        fs::path cache = root / "cache";
        Json rec = Json::object();
        rec["path"] = "fixture";
        rec["url"] = "file://" + src.string();
        rec["size"] = 14;
        rec["algorithm"] = "sha256";
        rec["checksum"] = file_digest(src, "sha256");
        fs::path target = download_artifact(rec, cache);
        assert(read_file_bytes(target) == "verified bytes");
        write_file_bytes(target, "untrusted data");
        expect_throws([&]() { download_artifact(rec, cache); }, "checksum mismatch", "tampered cached download");

        Json bad = rec;
        bad["checksum"] = std::string(64, '0');
        bad["size"] = 100;
        expect_throws([&]() { download_artifact(bad, cache); }, "checksum mismatch", "corrupt download");
        assert(!fs::exists(cache / bad.at("checksum").sval));
    }

    // 2. Unrecognized existing runtime fails fast before download.
    {
        fs::path rt = root / "rt";
        fs::create_directories(rt);
        write_file_bytes(rt / "foreign.txt", "not ours");
        fs::path pins = root / "pins.json";
        write_file_bytes(pins, R"({"artifacts":[]})");
        expect_throws([&]() { ensure_runtime(rt, pins); }, "unrecognized Android runtime", "unrecognized runtime");
    }

    // 3. ZIP path traversal rejected.
    {
        fs::path z = root / "evil.zip";
        {
            ZipWriter zw(z);
            zw.write_entry("../escaped", "bad");
            zw.finish();
        }
        expect_throws([&]() { extract_archive(z, ".", 0, stage); }, "Unsafe Android archive", "zip traversal");
        assert(!fs::exists(root / "escaped"));
    }

    // 4. ZIP safe symlink + executable permissions preserved; escaping symlink rejected.
    {
        fs::path z = root / "sdk.zip";
        {
            ZipWriter zw(z);
            zw.write_entry("sdk/bin/tool", "program", false, 0, (0100755U << 16));
            zw.write_entry("sdk/bin/alias", "tool", false, 0, (0120777U << 16));
            zw.finish();
        }
        fs::path s2 = root / "stage2";
        fs::create_directories(s2);
        extract_archive(z, ".", 1, s2);
        assert(read_file_bytes(s2 / "bin/alias") == "program");
        struct stat st {};
        assert(stat((s2 / "bin/tool").c_str(), &st) == 0 && (st.st_mode & 0111) != 0);

        fs::path evil_sym = root / "evil-sym.zip";
        {
            ZipWriter zw(evil_sym);
            zw.write_entry("outside", "../escaped", false, 0, (0120777U << 16));
            zw.finish();
        }
        expect_throws([&]() { extract_archive(evil_sym, ".", 0, s2); }, "Unsafe Android archive link", "zip symlink escape");
        assert(!fs::exists(s2 / "outside"));
    }

    // 5. Sparse copy preserves bytes and length.
    {
        std::string payload = "header" + std::string(3 * 1024 * 1024, '\0') + "trailer" + std::string(100, '\0');
        fs::path in = root / "sparse.in";
        fs::path out = root / "sparse.out";
        write_file_bytes(in, payload);
        sparse_copy_file(in, out);
        assert(fs::file_size(out) == payload.size());
        assert(file_digest(out, "sha256") == bytes_digest(payload, "sha256"));
    }

    // 6. APK icon rejects external SVG url(...) and ignores .webp cleanly.
    {
        fs::path apk = root / "app.apk";
        {
            ZipWriter zw(apk);
            zw.write_entry("res/icon.xml", "compiled XML");
            zw.write_entry("res/mipmap-xxxhdpi/ic_launcher.webp", "RIFF\0\0\0\0WEBP");
            zw.finish();
        }
        std::string bad_xml =
            "  E: vector (line=1)\n"
            "    E: path (line=2)\n"
            "      A: android:fillColor(0x0)=\"url(https://example.invalid/track)\"\n"
            "      A: android:pathData(0x0)=\"M0,0 L2,2 Z\"\n";
        auto icon = apk_icon(apk, "application-icon-160:'res/icon.xml'",
                             [&](const std::vector<std::string> &) { return bad_xml; }, stage / "icon");
        assert(!icon.has_value());
        assert(!fs::exists(stage / "icon.svg"));

        auto webp = apk_icon(apk, "application-icon-640:'res/mipmap-xxxhdpi/ic_launcher.webp'",
                             [](const std::vector<std::string> &) -> std::string {
                                 throw std::runtime_error("should not call aapt on webp");
                             }, stage / "webp");
        assert(!webp.has_value());
    }

    // 7. Root task boundaries reject shared stacks and ambiguous duplicates.
    {
        std::string shared =
            "RootTask id=9 bounds=[0,0][360,640] displayId=0 userId=0\n"
            "  taskId=9: org.fossify.math/.Main bounds=[0,0][360,640] visible=true topActivity=ComponentInfo{org.fossify.math/.Main}\n"
            "  taskId=10: another.app/.Main visible=true topActivity=ComponentInfo{another.app/.Main}\n";
        expect_throws([&]() { app_root_task(shared, "org.fossify.math"); }, "other tasks", "shared root task");

        std::string single =
            "RootTask id=9 bounds=[0,0][360,640] displayId=0 userId=0\n"
            "  taskId=9: org.fossify.math/.Main visible=true topActivity=ComponentInfo{org.fossify.math/.Main}\n";
        auto got = app_root_task(single, "org.fossify.math");
        assert(got.has_value() && got->first == 0 && got->second == 9);
        expect_throws([&]() {
            std::string dup = single + single;
            size_t pos = dup.rfind("id=9");
            dup.replace(pos, 4, "id=11");
            app_root_task(dup, "org.fossify.math");
        }, "Ambiguous", "ambiguous root task");
        std::string similar = single;
        size_t pos = 0;
        while ((pos = similar.find("org.fossify.math", pos)) != std::string::npos) {
            similar.replace(pos, 16, "org.fossify.math.fake");
            pos += 21;
        }
        assert(!app_root_task(similar, "org.fossify.math").has_value());
    }

    // 8. Owned runtime rejects external serials, reused PIDs, and unrelated executables.
    {
        for (const char *s : {"emulator-5558", "physical-test-device", "127.0.0.1:5555"}) {
            expect_throws([&]() { owned_runtime(root, root, s); }, "managed Android runtime", "external device");
        }
        fs::path avd = root / "android/avd";
        fs::create_directories(avd);
        Json rec = process_identity(getpid()).to_json();
        Json bad = rec;
        bad["start_ticks"] = "0";
        write_file_bytes(avd / "process.json", bad.dump());
        expect_throws([&]() { owned_runtime(root, root, "emulator-5560"); }, "process identity changed", "reused pid");
        write_file_bytes(avd / "process.json", rec.dump());
        expect_throws([&]() { owned_runtime(root, root, "emulator-5560"); }, "managed SDK", "unrelated exe");
    }

    // 9. AVD crash recovery keeps live lock and removes zombie lock.
    {
        fs::path avd = root / "avd-lock";
        fs::create_directories(avd);
        fs::path lock = avd / "hardware-qemu.ini.lock";
        std::string live = std::to_string(getpid()) + std::string(1, '\0');
        write_file_bytes(lock, live);
        clean_stale_avd_lock(avd);
        assert(read_file_bytes(lock) == live);

        pid_t child = fork();
        assert(child >= 0);
        if (child == 0) _exit(0);
        for (int i = 0; i < 200; i++) {
            try {
                if (process_identity(child).state == 'Z') break;
            } catch (...) {}
            usleep(10000);
        }
        write_file_bytes(lock, std::to_string(child) + std::string(1, '\0'));
        clean_stale_avd_lock(avd);
        assert(!fs::exists(lock));
        int st = 0;
        waitpid(child, &st, 0);
    }

    // 10. Aligned ZIP DEX payload starts on a 4-byte boundary.
    {
        fs::path jar = root / "aligned.jar";
        {
            ZipWriter zw(jar);
            zw.write_entry("classes.dex", "dex\n035\0payload", true, 0);
            zw.write_entry("a.txt", "123", true, 0);
            zw.write_entry("classes2.dex", "dex\n035\0helper", true, 0);
            zw.finish();
        }
        ZipReader zr(jar);
        for (const auto &e : zr.entries()) {
            std::string raw = read_file_bytes(jar);
            const unsigned char *lh = reinterpret_cast<const unsigned char *>(raw.data() + e.local_offset);
            uint64_t data_off = e.local_offset + 30 + rd_le16(lh + 26) + rd_le16(lh + 28);
            assert(data_off % 4 == 0);
        }
    }

    std::printf("android-core C++ test passed: all 10 runtime/security checks ok\n");
    return 0;
}
