/*
 * Copyright 2026 Development Desktop contributors
 * SPDX-License-Identifier: Apache-2.0 — Licensed under the Apache License, Version 2.0. See LICENSE.
 *
 * Record a purchased Olympium license on this machine.
 * That only hides the purchase reminder. It is optional.
 */
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

int main(int argc, char **argv) {
    if (argc < 2 || argv[1][0] == '\0') {
        std::fprintf(stderr, "Usage: olympium-activate LICENSE-KEY\nNo license was written.\n");
        return 2;
    }
    try {
        std::filesystem::create_directories("/var/lib/olympium");
        std::ofstream out("/var/lib/olympium/license", std::ios::trunc);
        if (!out) {
            std::fprintf(stderr, "olympium-activate: cannot write /var/lib/olympium/license\n");
            return 1;
        }
        out << argv[1] << '\n';
    } catch (const std::exception &e) {
        std::fprintf(stderr, "olympium-activate: %s\n", e.what());
        return 1;
    }
    std::printf("License recorded. The purchase reminder will stay hidden.\n");
    std::printf("Restart the desktop session if the reminder is already on screen.\n");
    return 0;
}
