/*
 * Copyright 2026 Development Desktop contributors
 * SPDX-License-Identifier: Apache-2.0 — Licensed under the Apache License, Version 2.0. See LICENSE.
 *
 * First-use guest fetch. A pin is fetched only when its record says the
 * artifact may be downloaded from the named upstream.
 */
#include "common.hpp"

#include <cstdio>
#include <fstream>
#include <string>

using namespace olympium;

int main(int argc, char **argv) {
    fs::path pin = (argc >= 2 && argv[1][0]) ? fs::path(argv[1]) : fs::path("/usr/share/olympium/guest-pins.txt");
    fs::path dest = (argc >= 3 && argv[2][0]) ? fs::path(argv[2]) : fs::path("/var/lib/olympium/guests");
    fs::create_directories(dest);
    if (!fs::is_regular_file(pin)) {
        std::fprintf(stderr, "no pin file: %s\n", pin.c_str());
        return 1;
    }
    std::ifstream in(pin);
    std::string line;
    while (std::getline(in, line)) {
        std::string t = trim(line);
        if (t.empty() || t[0] == '#') continue;
        auto cols = split_ws(t);
        if (cols.size() < 4) continue;
        const std::string &name = cols[0];
        const std::string &url = cols[1];
        const std::string &sha = cols[2];
        const std::string &policy = cols[3];
        if (policy != "first-use") {
            std::printf("refuse %s: policy '%s' does not allow a download\n", name.c_str(), policy.c_str());
            continue;
        }
        if (name.empty() || name.find('/') != std::string::npos || name == "." || name == ".." || name[0] == '-') {
            std::fprintf(stderr, "refuse invalid pin name: %s\n", name.c_str());
            return 1;
        }
        if (url.rfind("https://", 0) != 0) {
            std::fprintf(stderr, "refuse non-HTTPS url for %s\n", name.c_str());
            return 1;
        }
        fs::path out = dest / name;
        fs::path part = out.string() + ".partial";
        std::printf("fetch %s\n", name.c_str());
        std::fflush(stdout);
        int rc = run_passthrough({"curl", "-fL", "--retry", "2", "-o", part.string(), "--", url});
        if (rc != 0) {
            std::error_code ec;
            fs::remove(part, ec);
            return 1;
        }
        std::string got = file_digest(part, "sha256");
        if (got != sha) {
            std::fprintf(stderr, "checksum mismatch for %s\n", name.c_str());
            std::error_code ec;
            fs::remove(part, ec);
            return 1;
        }
        fs::rename(part, out);
        std::printf("kept %s\n", out.c_str());
    }
    return 0;
}
