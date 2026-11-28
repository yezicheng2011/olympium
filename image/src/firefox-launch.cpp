/*
 * Copyright 2026 Development Desktop contributors
 * SPDX-License-Identifier: Apache-2.0 — Licensed under the Apache License, Version 2.0. See LICENSE.
 *
 * Starts the real Firefox program as the olympium user under Wayland.
 */
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <pwd.h>
#include <string>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;

static void write_kmsg(const std::string &msg) {
    if (std::ofstream k("/dev/kmsg"); k) k << msg << "\n";
}

static bool is_unix_socket(const fs::path &p) {
    struct stat st {};
    return stat(p.c_str(), &st) == 0 && S_ISSOCK(st.st_mode);
}

static int launch_firefox(const std::string &real_bin, const std::string &runtime,
                          const std::string &wl, bool disable_sandbox,
                          int argc, char **argv, const std::string &log_path) {
    pid_t pid = fork();
    if (pid < 0) return 1;
    if (pid == 0) {
        int fd = open(log_path.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (fd >= 0) {
            dup2(fd, 1);
            dup2(fd, 2);
            if (fd > 2) close(fd);
        }
        std::vector<std::string> args = {
            "runuser", "-u", "olympium", "--", "env",
            "HOME=/home/olympium",
            "USER=olympium",
            "LOGNAME=olympium",
            "XDG_RUNTIME_DIR=" + runtime,
            "WAYLAND_DISPLAY=" + wl,
            "GDK_BACKEND=wayland",
            "MOZ_ENABLE_WAYLAND=1",
            "MOZ_WEBRENDER=0",
            "MOZ_ACCELERATED=0",
            "LIBGL_ALWAYS_SOFTWARE=1",
            "GALLIUM_DRIVER=llvmpipe",
            "MOZ_DBUS_REMOTE=1",
        };
        if (disable_sandbox) args.push_back("MOZ_DISABLE_CONTENT_SANDBOX=1");
        args.push_back("/usr/bin/dbus-run-session");
        args.push_back("--");
        args.push_back(real_bin);
        for (int i = 1; i < argc; i++) args.emplace_back(argv[i]);
        std::vector<char *> cargv;
        cargv.reserve(args.size() + 1);
        for (auto &s : args) cargv.push_back(s.data());
        cargv.push_back(nullptr);
        execvp("runuser", cargv.data());
        _exit(127);
    }
    int st = 0;
    while (waitpid(pid, &st, 0) < 0 && errno == EINTR) {}
    return WIFEXITED(st) ? WEXITSTATUS(st) : 1;
}

int main(int argc, char **argv) {
    std::string real_bin;
    for (const char *cand : {"/usr/lib/firefox/firefox", "/usr/lib/firefox-esr/firefox-esr"}) {
        if (access(cand, X_OK) == 0) {
            real_bin = cand;
            break;
        }
    }
    if (real_bin.empty()) {
        std::fprintf(stderr, "firefox: no Firefox program is installed\n");
        return 1;
    }
    struct passwd *pw = getpwnam("olympium");
    if (!pw) {
        std::fprintf(stderr, "firefox: the olympium user is missing\n");
        return 1;
    }
    std::string runtime = "/run/user/" + std::to_string(pw->pw_uid);
    std::error_code ec;
    fs::create_directories(runtime, ec);
    chmod(runtime.c_str(), 0700);
    if (chown(runtime.c_str(), pw->pw_uid, pw->pw_gid) != 0) {}

    const char *wl_env = std::getenv("WAYLAND_DISPLAY");
    std::string wl = (wl_env && wl_env[0]) ? wl_env : "wayland-0";
    const char *xdg = std::getenv("XDG_RUNTIME_DIR");
    fs::path sock;
    if (xdg && xdg[0] && is_unix_socket(fs::path(xdg) / wl)) {
        sock = fs::path(xdg) / wl;
    } else if (is_unix_socket(fs::path("/run/dev-desktop") / wl)) {
        sock = fs::path("/run/dev-desktop") / wl;
    }
    if (!sock.empty()) {
        chmod(sock.parent_path().c_str(), 0755);
        chmod(sock.c_str(), 0666);
        fs::path link_path = fs::path(runtime) / wl;
        fs::remove(link_path, ec);
        fs::create_symlink(sock, link_path, ec);
    }

    const std::string log_path = "/tmp/firefox.log";
    { std::ofstream trunc_log(log_path, std::ios::trunc); }
    int code = launch_firefox(real_bin, runtime, wl, false, argc, argv, log_path);
    if (code != 0) {
        std::ifstream in(log_path);
        std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (text.find("UserNamespace") != std::string::npos || text.find("EPERM") != std::string::npos) {
            const std::string note = "firefox: user namespaces are blocked, so the content sandbox is off";
            write_kmsg(note);
            { std::ofstream out(log_path, std::ios::app); out << note << "\n"; }
            code = launch_firefox(real_bin, runtime, wl, true, argc, argv, log_path);
        }
    }
    if (code != 0) {
        write_kmsg("firefox: exit " + std::to_string(code));
    }
    return code;
}
