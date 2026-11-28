/*
 * Copyright 2026 Development Desktop contributors
 * SPDX-License-Identifier: Apache-2.0 — Licensed under the Apache License, Version 2.0. See LICENSE.
 *
 * Runs one 64-bit x86 program on a 32-bit system via a swap-backed 64-bit guest.
 */
#include "common.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <unistd.h>
#include <vector>

using namespace olympium;

static void kmsg(const std::string &msg) {
    std::printf("%s\n", msg.c_str());
    std::fflush(stdout);
    if (std::ofstream k("/dev/kmsg"); k) k << "<6>olympium-run64: " << msg << "\n";
}

static bool cmdline_has(const std::string &flag) {
    if (std::ifstream in("/proc/cmdline"); in) {
        std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        return text.find(flag) != std::string::npos;
    }
    return false;
}

static bool is_block_device(const std::string &p) {
    struct stat st {};
    return stat(p.c_str(), &st) == 0 && S_ISBLK(st.st_mode);
}

static bool same_or_child_dev(const std::string &root, const std::string &dev) {
    if (root == dev) return true;
    if (root.rfind(dev, 0) == 0 && root.size() > dev.size()) {
        char next = root[dev.size()];
        if (next == 'p' || std::isdigit(static_cast<unsigned char>(next))) return true;
    }
    return false;
}

static std::string setup_backing() {
    bool want_format = cmdline_has("olympium.formatdisk=1") || getenv_str("OLYMPIUM64_FORMAT", "0") == "1";
    std::string root = trim(run_cmd({"findmnt", "-n", "-o", "SOURCE", "/"}, 10).output);
    std::vector<std::string> devs;
    if (std::string extra = getenv_str("OLYMPIUM64_DISK"); !extra.empty()) {
        for (const auto &w : split_ws(extra)) devs.push_back(w);
    }
    for (const char *d : {"/dev/vdb", "/dev/vda", "/dev/sdb", "/dev/sda"}) devs.emplace_back(d);

    std::string disk_mnt;
    for (const auto &dev : devs) {
        if (!is_block_device(dev)) continue;
        if (same_or_child_dev(root, dev)) continue;
        std::string mp = trim(run_cmd({"findmnt", "-n", "-o", "TARGET", dev}, 10).output);
        if (!mp.empty()) {
            if (mp == "/var/lib/olympium/disk") {
                disk_mnt = mp;
                break;
            }
            continue;
        }
        std::string fstype = trim(run_cmd({"blkid", "-o", "value", "-s", "TYPE", dev}, 10).output);
        if (fstype == "swap") {
            if (run_cmd({"swapon", dev}, 30).code == 0) kmsg("swap on " + dev);
        } else if (fstype == "ext2" || fstype == "ext3" || fstype == "ext4") {
            fs::create_directories("/var/lib/olympium/disk");
            if (run_cmd({"mount", dev, "/var/lib/olympium/disk"}, 30).code == 0) {
                disk_mnt = "/var/lib/olympium/disk";
            }
        } else if (fstype.empty() && want_format) {
            if (!trim(run_cmd({"blkid", dev}, 10).output).empty()) continue;
            if (!trim(run_cmd({"lsblk", "-no", "MOUNTPOINT", dev}, 10).output).empty()) continue;
            auto types_lines = split_lines(run_cmd({"lsblk", "-nr", "-o", "TYPE", dev}, 10).output);
            std::set<std::string> types;
            for (const auto &l : types_lines) {
                std::string t = trim(l);
                if (!t.empty()) types.insert(t);
            }
            if (types.size() != 1 || *types.begin() != "disk") continue;
            auto mk = run_cmd({"mkfs.ext4", "-F", "-L", "OLYMPIUM64", dev}, 120);
            write_file_bytes("/tmp/olympium64-mkfs.log", mk.output);
            if (mk.code != 0) continue;
            fs::create_directories("/var/lib/olympium/disk");
            if (run_cmd({"mount", dev, "/var/lib/olympium/disk"}, 30).code == 0) {
                disk_mnt = "/var/lib/olympium/disk";
                fs::path swap = fs::path(disk_mnt) / "swap";
                if (!fs::is_regular_file(swap)) {
                    run_cmd({"dd", "if=/dev/zero", "of=" + swap.string(), "bs=1M", "count=2048", "status=none"}, 300);
                    chmod(swap.c_str(), 0600);
                    run_cmd({"mkswap", swap.string()}, 30);
                }
                if (run_cmd({"swapon", swap.string()}, 30).code == 0) kmsg("swap file on " + dev);
                kmsg("formatted " + dev + " for the 64-bit guest");
            }
        }
        if (!disk_mnt.empty()) break;
    }

    if (!disk_mnt.empty()) {
        fs::path swap = fs::path(disk_mnt) / "swap";
        if (fs::is_regular_file(swap)) {
            run_cmd({"swapon", swap.string()}, 30);
            kmsg("swap file " + swap.string());
        }
        kmsg("guest RAM file on " + disk_mnt);
    } else {
        kmsg("no extra disk; guest RAM is anonymous");
    }
    if (std::ifstream mem("/proc/meminfo"); mem) {
        std::string line;
        while (std::getline(mem, line)) {
            if (line.rfind("MemAvailable", 0) == 0 || line.rfind("SwapTotal", 0) == 0 ||
                line.rfind("SwapFree", 0) == 0) {
                kmsg(line);
            }
        }
    }
    return disk_mnt;
}

int main(int argc, char **argv) {
    bool hold = false;
    int idx = 1;
    if (idx < argc && std::string(argv[idx]) == "--hold") { hold = true; idx++; }
    if (idx < argc && std::string(argv[idx]) == "--no-hold") { idx++; }
    if (idx >= argc || !fs::is_regular_file(argv[idx])) {
        std::fprintf(stderr, "olympium-run64: need a 64-bit program\n");
        return 2;
    }
    fs::path prog = argv[idx];
    std::string base = prog.filename().string();
    std::string cmd = "payload";
    if (base == "hello64") cmd = "hello";
    else if (base == "highmap64") cmd = "highmap";
    else if (base == "bash") cmd = "bash";

    const fs::path guest = "/usr/lib/olympium/amd64";
    if (!fs::is_regular_file(guest / "vmlinuz") || !fs::is_regular_file(guest / "initrd.cpio")) {
        std::fprintf(stderr, "olympium-run64: the 64-bit guest files are not installed\n");
        return 1;
    }
    if (access("/usr/bin/qemu-system-x86_64", X_OK) != 0) {
        std::fprintf(stderr, "olympium-run64: qemu-system-x86_64 is not installed\n");
        return 1;
    }

    struct utsname u {};
    uname(&u);
    kmsg("begin " + std::string(u.machine) + " cmd=" + cmd + " program=" + prog.string());
    std::string disk_mnt = setup_backing();

    char wtmpl[] = "/tmp/olympium64.XXXXXX";
    char stmpl[] = "/tmp/olympium64-stage.XXXXXX";
    char *wdir = mkdtemp(wtmpl);
    char *sdir = mkdtemp(stmpl);
    if (!wdir || !sdir) {
        if (wdir) fs::remove_all(wdir);
        if (sdir) fs::remove_all(sdir);
        return 1;
    }
    fs::path work(wdir), stage(sdir);
    struct Cleanup {
        fs::path a, b;
        ~Cleanup() { std::error_code ec; fs::remove_all(a, ec); fs::remove_all(b, ec); }
    } cleanup{work, stage};

    fs::create_directories(stage / "payload");
    write_file_bytes(stage / "payload/cmd", cmd + "\n");
    if (cmd == "payload") {
        fs::copy_file(prog, stage / "payload/program", fs::copy_options::overwrite_existing);
        chmod((stage / "payload/program").c_str(), 0755);
    }
    run_cmd({"sh", "-c", "cd " + sh_quote(stage.string()) + " && find . | cpio -o -H newc > " +
             sh_quote((work / "extra.cpio").string()) + " 2>/dev/null"}, 60);
    {
        std::ofstream initrd(work / "initrd", std::ios::binary | std::ios::trunc);
        std::ifstream a(guest / "initrd.cpio", std::ios::binary);
        std::ifstream b(work / "extra.cpio", std::ios::binary);
        initrd << a.rdbuf() << b.rdbuf();
    }

    std::string mem = getenv_str("OLYMPIUM64_MEM", "512");
    int timeout_sec = std::stoi(getenv_str("OLYMPIUM64_TIMEOUT", "240"));
    std::vector<std::string> qargv = {"qemu-system-x86_64"};
    if (!disk_mnt.empty()) {
        fs::path ram = fs::path(disk_mnt) / "guest.ram";
        run_cmd({"truncate", "-s", mem + "M", ram.string()}, 30);
        qargv.insert(qargv.end(), {
            "-machine", "pc,accel=tcg,memory-backend=mem",
            "-object", "memory-backend-file,id=mem,size=" + mem + "M,mem-path=" + ram.string() + ",share=off,prealloc=off"
        });
    } else {
        qargv.insert(qargv.end(), {"-machine", "pc,accel=tcg", "-m", mem});
    }
    qargv.insert(qargv.end(), {
        "-kernel", (guest / "vmlinuz").string(),
        "-initrd", (work / "initrd").string(),
        "-append", "console=ttyS0,115200 panic=1",
        "-serial", "file:" + (work / "serial").string(),
        "-display", "none", "-no-reboot", "-monitor", "none",
    });

    auto t0 = std::chrono::steady_clock::now();
    auto qres = run_cmd(qargv, timeout_sec);
    auto secs = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - t0).count();
    kmsg("qemu exit " + std::to_string(qres.code) + " seconds " + std::to_string(secs) + " mem " + mem + "M");
    if (fs::is_regular_file(work / "serial")) {
        bool printing = false;
        for (const auto &line : split_lines(read_file_bytes(work / "serial"))) {
            if (line.find("OLYMPIUM64_BEGIN") != std::string::npos) printing = true;
            if (printing) kmsg(line);
            if (line.find("OLYMPIUM64_END") != std::string::npos) printing = false;
        }
    } else {
        kmsg("no guest serial");
        auto lines = split_lines(qres.output);
        size_t start = lines.size() > 20 ? lines.size() - 20 : 0;
        for (size_t i = start; i < lines.size(); i++) kmsg("qemu: " + lines[i]);
    }
    if (std::ifstream meminfo("/proc/meminfo"); meminfo) {
        std::string line;
        while (std::getline(meminfo, line)) {
            if (line.rfind("SwapTotal", 0) == 0 || line.rfind("SwapFree", 0) == 0) {
                kmsg("after " + line);
            }
        }
    }
    if (hold) {
        kmsg("holding the terminal");
        sleep(static_cast<unsigned int>(std::stoi(getenv_str("OLYMPIUM64_HOLD", "150"))));
    }
    return qres.code;
}
