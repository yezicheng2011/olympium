/*
 * Copyright 2026 Development Desktop contributors
 * SPDX-License-Identifier: Apache-2.0 — Licensed under the Apache License, Version 2.0. See LICENSE.
 *
 * Darling isolates SysV IPC; software X11 clients must use socket image copies.
 */
#include <unistd.h>
#include <vector>

int main(int argc, char **argv) {
    std::vector<char *> args;
    args.push_back(const_cast<char *>("/usr/bin/Xwayland"));
    args.push_back(const_cast<char *>("-extension"));
    args.push_back(const_cast<char *>("MIT-SHM"));
    for (int i = 1; i < argc; i++) args.push_back(argv[i]);
    args.push_back(nullptr);
    execv("/usr/bin/Xwayland", args.data());
    return 127;
}
