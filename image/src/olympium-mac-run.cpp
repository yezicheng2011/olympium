/*
 * Copyright 2026 Development Desktop contributors
 * SPDX-License-Identifier: Apache-2.0 — Licensed under the Apache License, Version 2.0. See LICENSE.
 *
 * Starts one Mac program through Darling, when Darling is installed.
 */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <pwd.h>
#include <string>
#include <sys/stat.h>
#include <sys/vfs.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;

static constexpr unsigned long OVERLAYFS_SUPER_MAGIC_VAL = 0x794c7630UL;
static constexpr unsigned long TMPFS_MAGIC_VAL = 0x01021994UL;

static bool have_darling() {
    const char *path_env = std::getenv("PATH");
    std::string paths = path_env ? path_env : "/usr/local/bin:/usr/bin:/bin";
    size_t pos = 0;
    while (pos <= paths.size()) {
        size_t colon = paths.find(':', pos);
        std::string dir = colon == std::string::npos ? paths.substr(pos) : paths.substr(pos, colon - pos);
        if (!dir.empty() && access((dir + "/darling").c_str(), X_OK) == 0) return true;
        if (colon == std::string::npos) break;
        pos = colon + 1;
    }
    return false;
}

static bool fs_magic_is(const std::string &path, unsigned long magic) {
    struct statfs sf {};
    return statfs(path.c_str(), &sf) == 0 && static_cast<unsigned long>(sf.f_type) == magic;
}

static bool darling_shell_true() {
    pid_t pid = fork();
    if (pid < 0) return false;
    if (pid == 0) {
        int nul = open("/dev/null", O_RDWR);
        if (nul >= 0) {
            dup2(nul, 0); dup2(nul, 1); dup2(nul, 2);
            if (nul > 2) close(nul);
        }
        execlp("darling", "darling", "shell", "true", static_cast<char *>(nullptr));
        _exit(127);
    }
    int st = 0;
    while (waitpid(pid, &st, 0) < 0 && errno == EINTR) {}
    return WIFEXITED(st) && WEXITSTATUS(st) == 0;
}

int main(int argc, char **argv) {
    const char *sw = std::getenv("OLYMPIUM_MAC_SOFTWARE");
    if (sw && std::strcmp(sw, "1") == 0) {
        setenv("LIBGL_ALWAYS_SOFTWARE", "1", 1);
        setenv("GALLIUM_DRIVER", "softpipe", 1);
        setenv("LP_NUM_THREADS", "0", 1);
        setenv("GALLIUM_THREAD", "0", 1);
        setenv("mesa_glthread", "false", 1);
        setenv("MESA_SHADER_CACHE_DISABLE", "true", 1);
    }
    if (argc >= 2 && std::strcmp(argv[1], "--check") == 0) {
        return have_darling() ? 0 : 1;
    }
    if (argc < 3) {
        std::fprintf(stderr, "usage: olympium-mac-run APP PROGRAM [ARG...]\n");
        return 2;
    }
    if (!have_darling()) {
        std::fprintf(stderr, "olympium-mac-run: Darling is not installed\n");
        return 3;
    }
    std::string program = argv[2];
    if (program.empty() || program[0] != '/') {
        std::fprintf(stderr, "olympium-mac-run: the program path must be absolute\n");
        return 2;
    }
    const char *home = std::getenv("HOME");
    if (!home || !home[0]) {
        struct passwd *pw = getpwuid(getuid());
        setenv("HOME", (pw && pw->pw_dir && pw->pw_dir[0]) ? pw->pw_dir : "/root", 1);
        home = std::getenv("HOME");
    }
    const char *dprefix = std::getenv("DPREFIX");
    if ((!dprefix || !dprefix[0]) && home && fs_magic_is(home, OVERLAYFS_SUPER_MAGIC_VAL)) {
        uid_t uid = getuid();
        const char *xdg = std::getenv("XDG_RUNTIME_DIR");
        std::vector<std::string> bases;
        if (xdg && xdg[0]) bases.emplace_back(xdg);
        bases.emplace_back("/run");
        bases.emplace_back("/dev/shm");
        for (const auto &base : bases) {
            if (access(base.c_str(), W_OK) == 0 && fs_magic_is(base, TMPFS_MAGIC_VAL)) {
                fs::path dir = fs::path(base) / ("olympium-mac-" + std::to_string(uid));
                std::error_code ec;
                fs::create_directories(dir, ec);
                struct stat st {};
                if (!ec && chmod(dir.c_str(), 0700) == 0 &&
                    lstat(dir.c_str(), &st) == 0 && S_ISDIR(st.st_mode) && st.st_uid == uid) {
                    setenv("DPREFIX", (dir / "prefix").c_str(), 1);
                    break;
                }
            }
        }
    }
    for (int tries = 0; !darling_shell_true(); tries++) {
        if (tries >= 11) {
            std::fprintf(stderr, "olympium-mac-run: Darling's container did not start\n");
            return 4;
        }
        sleep(1);
    }
    std::vector<char *> args;
    args.push_back(const_cast<char *>("darling"));
    args.push_back(const_cast<char *>(program.c_str()));
    for (int i = 3; i < argc; i++) args.push_back(argv[i]);
    args.push_back(nullptr);
    execvp("darling", args.data());
    perror("darling");
    return 127;
}
