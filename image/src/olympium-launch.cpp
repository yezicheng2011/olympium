/*
 * Copyright 2026 Development Desktop contributors
 * SPDX-License-Identifier: Apache-2.0 — Licensed under the Apache License, Version 2.0. See LICENSE.
 */
#include <cstdlib>
#include <unistd.h>
#include <vector>

int main(int argc, char **argv) {
    if (!std::getenv("GDK_BACKEND") || !std::getenv("GDK_BACKEND")[0]) {
        setenv("GDK_BACKEND", "wayland", 1);
    }
    if (!std::getenv("GSK_RENDERER") || !std::getenv("GSK_RENDERER")[0]) {
        setenv("GSK_RENDERER", "cairo", 1);
    }
    const char *candidates[] = {
        "/usr/bin/olympium-linux",
        "/usr/bin/linkspan-linux",
        "/usr/local/bin/olympium-linux",
    };
    for (const char *bin : candidates) {
        if (access(bin, X_OK) == 0) {
            std::vector<char *> args;
            args.push_back(const_cast<char *>(bin));
            for (int i = 1; i < argc; i++) args.push_back(argv[i]);
            args.push_back(nullptr);
            execv(bin, args.data());
        }
    }
    execl("/usr/bin/dev-settings", "dev-settings", "--page=olympium", static_cast<char *>(nullptr));
    return 127;
}
