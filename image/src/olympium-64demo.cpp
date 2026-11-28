/*
 * Copyright 2026 Development Desktop contributors
 * SPDX-License-Identifier: Apache-2.0 — Licensed under the Apache License, Version 2.0. See LICENSE.
 */
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <unistd.h>

int main() {
    struct utsname u {};
    uname(&u);
    std::printf("64-bit programs on this 32-bit system\n");
    std::printf("machine %s kernel %s\n", u.machine, u.release);
    const char *run = "/usr/libexec/dev-desktop/olympium-run";
    const char *progs[] = {
        "/usr/lib/olympium/amd64/bin/hello64",
        "/usr/lib/olympium/amd64/bin/highmap64",
        "/usr/lib/olympium/amd64/bin/bash",
        "/usr/lib/olympium/amd64/bin/payload64",
    };
    for (const char *prog : progs) {
        std::string base = std::filesystem::path(prog).filename().string();
        std::printf("\n== %s ==\n", base.c_str());
        std::fflush(stdout);
        pid_t pid = fork();
        if (pid == 0) {
            execl(run, run, prog, static_cast<char *>(nullptr));
            _exit(127);
        }
        int st = 0;
        if (pid > 0) {
            waitpid(pid, &st, 0);
            int code = WIFEXITED(st) ? WEXITSTATUS(st) : 128;
            if (code != 0) std::printf("exit %d\n", code);
        }
    }
    std::printf("\ndone\n");
    std::fflush(stdout);
    if (std::ofstream kmsg("/dev/kmsg"); kmsg) {
        kmsg << "<6>olympium-64demo: done\n";
    }
    sleep(180);
    return 0;
}
