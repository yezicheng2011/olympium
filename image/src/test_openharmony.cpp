/*
 * Copyright 2026 Development Desktop contributors
 * SPDX-License-Identifier: Apache-2.0 — Licensed under the Apache License, Version 2.0. See LICENSE.
 *
 * Native C++20 tests for olympium-openharmony HAP inspection, icon validation,
 * and private session.json enforcement.
 */
#include "common.hpp"

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

using namespace olympium;

static std::string make_1x1_png() {
    // Minimal 1x1 PNG header + IHDR (24+ bytes) sufficient for dimension/signature verification.
    std::string png;
    const unsigned char hdr[] = {
        0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n',
        0x00, 0x00, 0x00, 0x0d, 'I', 'H', 'D', 'R',
        0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x10,
        8, 6, 0, 0, 0, 0x1f, 0xf3, 0xff, 0x61
    };
    png.assign(reinterpret_cast<const char *>(hdr), sizeof hdr);
    return png;
}

int main() {
    char tmpl[] = "/tmp/test-openharmony-XXXXXX";
    char *dir = mkdtemp(tmpl);
    assert(dir != nullptr);
    fs::path root(dir);
    struct Guard { fs::path p; ~Guard() { std::error_code ec; fs::remove_all(p, ec); } } g{root};

    // Create a local Unix socket and fake executable for session.json.
    fs::path qmp = root / "qmp.sock";
    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    assert(sock >= 0);
    struct sockaddr_un addr {};
    addr.sun_family = AF_UNIX;
    std::snprintf(addr.sun_path, sizeof addr.sun_path, "%s", qmp.c_str());
    assert(bind(sock, reinterpret_cast<struct sockaddr *>(&addr), sizeof addr) == 0);

    fs::path fake_hdc = root / "hdc";
    write_file_bytes(fake_hdc, "#!/bin/sh\nexit 0\n");
    chmod(fake_hdc.c_str(), 0700);

    fs::path session = root / "session.json";
    Json sj = Json::object();
    sj["hdc"] = fake_hdc.string();
    sj["qmp"] = qmp.string();
    sj["target"] = "127.0.0.1:55555";
    write_file_bytes(session, sj.dump() + "\n");
    chmod(session.c_str(), 0644); // Intentionally world-readable first -> must reject!

    fs::path hap = root / "demo.hap";
    {
        ZipWriter zw(hap);
        Json mf = Json::object();
        mf["app"] = Json::object();
        mf["app"]["bundleName"] = "org.olympium.demo";
        mf["app"]["label"] = "Demo App";
        mf["app"]["icon"] = "$media:app_icon";
        mf["module"] = Json::object();
        mf["module"]["type"] = "entry";
        mf["module"]["mainElement"] = "MainAbility";
        Json ab = Json::object();
        ab["name"] = "MainAbility";
        ab["exported"] = true;
        mf["module"]["abilities"] = Json(std::vector<Json>{ab});
        zw.write_entry("module.json", mf.dump());
        zw.write_entry("resources/base/media/app_icon.png", make_1x1_png());
        zw.finish();
    }

    fs::path desk = root / "desk";
    auto bad_perm = run_cmd({"./olympium-openharmony", "inspect", hap.string(), "--root", desk.string()},
                            15, {{"OLYMPIUM_OPENHARMONY_SESSION", session.string()}});
    assert(bad_perm.code == 3);
    assert(bad_perm.output.find("private to the desktop user") != std::string::npos);

    chmod(session.c_str(), 0600);
    auto ok = run_cmd({"./olympium-openharmony", "inspect", hap.string(), "--root", desk.string()},
                      15, {{"OLYMPIUM_OPENHARMONY_SESSION", session.string()}});
    assert(ok.code == 0);
    Json parsed = Json::parse(trim(ok.output));
    assert(parsed.get_bool("ok"));
    assert(parsed.get_string("package") == "org.olympium.demo");
    assert(parsed.get_string("ability") == "MainAbility");
    assert(parsed.get_string("name") == "Demo App");
    assert(fs::is_regular_file(parsed.get_string("icon")));

    close(sock);
    std::printf("openharmony C++ test passed: HAP inspection, icon extraction, session privacy ok\n");
    return 0;
}
