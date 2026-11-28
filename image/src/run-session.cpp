/*
 * Copyright 2026 Development Desktop contributors
 * SPDX-License-Identifier: Apache-2.0 — Licensed under the Apache License, Version 2.0. See LICENSE.
 *
 * Launches the Olympium Wayland session and forwards compositor logs to kmsg
 * and any active serial console.
 */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <signal.h>
#include <string>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;

static std::vector<std::string> g_serials;

static void init_serials() {
    std::ifstream in("/sys/class/tty/console/active");
    std::string word;
    while (in >> word) {
        if (word.rfind("ttyS", 0) == 0 || word.rfind("ttyAMA", 0) == 0) {
            g_serials.push_back("/dev/" + word);
        }
    }
}

static void kmsg(const std::string &msg) {
    if (std::ofstream k("/dev/kmsg"); k) k << "<6>dev-desktop: " << msg << "\n";
    for (const auto &port : g_serials) {
        if (std::ofstream s(port); s) s << "dev-desktop: " << msg << "\r\n";
    }
}

static void run_bg_probe() {
    pid_t pid = fork();
    if (pid != 0) return;
    auto modprobe = [](const char *m) {
        pid_t p = fork();
        if (p == 0) {
            int nul = open("/dev/null", O_RDWR);
            if (nul >= 0) { dup2(nul, 1); dup2(nul, 2); if (nul > 2) close(nul); }
            execlp("modprobe", "modprobe", m, static_cast<char *>(nullptr));
            _exit(127);
        }
        int st = 0;
        if (p > 0) waitpid(p, &st, 0);
    };
    for (const char *m : {"e1000", "virtio_net", "virtio_blk", "virtio_pci", "nvme"}) modprobe(m);
    std::error_code ec;
    for (const auto &ent : fs::directory_iterator("/sys/class/net", ec)) {
        std::string name = ent.path().filename().string();
        if (name == "lo") continue;
        pid_t p = fork();
        if (p == 0) {
            int nul = open("/dev/null", O_RDWR);
            if (nul >= 0) { dup2(nul, 1); dup2(nul, 2); if (nul > 2) close(nul); }
            execlp("ip", "ip", "link", "set", name.c_str(), "up", static_cast<char *>(nullptr));
            _exit(127);
        }
        int st = 0;
        if (p > 0) waitpid(p, &st, 0);
    }
    pid_t p = fork();
    if (p == 0) {
        int fd = open("/tmp/driver-report.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd >= 0) { dup2(fd, 1); dup2(fd, 2); if (fd > 2) close(fd); }
        execl("/usr/bin/driver-report", "driver-report", static_cast<char *>(nullptr));
        _exit(127);
    }
    int st = 0;
    if (p > 0) waitpid(p, &st, 0);
    _exit(0);
}

static void modprobe_sync(const char *m) {
    pid_t p = fork();
    if (p == 0) {
        int nul = open("/dev/null", O_RDWR);
        if (nul >= 0) { dup2(nul, 1); dup2(nul, 2); if (nul > 2) close(nul); }
        execlp("modprobe", "modprobe", m, static_cast<char *>(nullptr));
        _exit(127);
    }
    int st = 0;
    if (p > 0) waitpid(p, &st, 0);
}

static bool cmdline_has(const std::string &flag) {
    std::ifstream in("/proc/cmdline");
    if (!in) return false;
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return text.find(flag) != std::string::npos;
}

int main() {
    init_serials();
    std::error_code ec;
    fs::create_directories("/run/dev-desktop", ec);
    chmod("/run/dev-desktop", 0755);

    struct utsname u {};
    uname(&u);
    kmsg("session start arch " + std::string(u.machine) + " " + u.release);
    modprobe_sync("bochs");
    modprobe_sync("virtio_gpu");
    run_bg_probe();

    if (cmdline_has("olympium.contrast=1")) setenv("DEV_DESK_CONTRAST", "1", 1);
    if (cmdline_has("olympium.gles=1")) {
        setenv("WLR_RENDERER", "gles2", 1);
        setenv("LIBGL_ALWAYS_SOFTWARE", "1", 1);
        setenv("GALLIUM_DRIVER", "llvmpipe", 1);
    } else if (!std::getenv("WLR_RENDERER") || !std::getenv("WLR_RENDERER")[0]) {
        setenv("WLR_RENDERER", "pixman", 1);
    }
    setenv("BROWSER", "/usr/bin/firefox", 1);
    setenv("MOZ_ENABLE_WAYLAND", "1", 1);
    setenv("OLYMPIUM_DESK_HOME", "/var/lib/olympium/desk", 1);
    fs::create_directories("/var/lib/olympium/desk", ec);

    std::string renderer = std::getenv("WLR_RENDERER") ? std::getenv("WLR_RENDERER") : "pixman";
    kmsg("renderer " + renderer);
    const std::string log_path = "/run/dev-desktop/compositor.log";
    { std::ofstream f(log_path, std::ios::trunc); }

    pid_t log_pid = fork();
    if (log_pid == 0) {
        int fd = open(log_path.c_str(), O_RDONLY);
        if (fd < 0) _exit(1);
        std::string buf;
        char chunk[4096];
        for (;;) {
            ssize_t n = read(fd, chunk, sizeof chunk);
            if (n > 0) {
                buf.append(chunk, static_cast<size_t>(n));
                size_t nl;
                while ((nl = buf.find('\n')) != std::string::npos) {
                    kmsg(buf.substr(0, nl));
                    buf.erase(0, nl + 1);
                }
            } else {
                usleep(100000);
            }
        }
    }
    if (log_pid > 0) {
        std::ofstream pf("/run/dev-desktop/logpipe.pid", std::ios::trunc);
        pf << log_pid << "\n";
    }

    auto run_compositor = [&]() -> int {
        pid_t p = fork();
        if (p < 0) return 1;
        if (p == 0) {
            int fd = open(log_path.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
            if (fd >= 0) { dup2(fd, 1); dup2(fd, 2); if (fd > 2) close(fd); }
            execl("/usr/bin/dev-compositor", "dev-compositor", "-n", "-s",
                  "/usr/libexec/dev-desktop/startup.sh", static_cast<char *>(nullptr));
            _exit(127);
        }
        int st = 0;
        while (waitpid(p, &st, 0) < 0 && errno == EINTR) {}
        return WIFEXITED(st) ? WEXITSTATUS(st) : 1;
    };

    int code = run_compositor();
    kmsg("compositor exit " + std::to_string(code));
    if (code != 0 && renderer != "pixman") {
        setenv("WLR_RENDERER", "pixman", 1);
        setenv("DEV_DESK_CONTRAST", "1", 1);
        kmsg("retry pixman contrast");
        code = run_compositor();
    }
    if (log_pid > 0) {
        kill(log_pid, SIGTERM);
        int st = 0;
        waitpid(log_pid, &st, 0);
    }
    return code;
}
