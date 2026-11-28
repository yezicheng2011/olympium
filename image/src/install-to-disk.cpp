/*
 * Copyright 2026 Development Desktop contributors
 * SPDX-License-Identifier: Apache-2.0 — Licensed under the Apache License, Version 2.0. See LICENSE.
 *
 * Copies the running live system onto a whole disk and makes that disk bootable.
 */
#include "common.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <unistd.h>
#include <vector>

using namespace olympium;

[[noreturn]] static void fail(const std::string &msg) {
    std::fprintf(stderr, "install-to-disk: %s\n", msg.c_str());
    std::exit(1);
}

static std::string part_path(const std::string &disk, int n) {
    if (!disk.empty() && std::isdigit(static_cast<unsigned char>(disk.back()))) {
        return disk + "p" + std::to_string(n);
    }
    return disk + std::to_string(n);
}

int main(int argc, char **argv) {
    bool yes = false;
    int idx = 1;
    if (idx < argc && std::string(argv[idx]) == "--yes") {
        yes = true;
        idx++;
    }
    std::string disk = (idx < argc) ? argv[idx] : getenv_str("DEV_INSTALL_DISK");
    const std::string target = "/mnt/olympium-target";
    const std::string label = "olympium-root";

    if (geteuid() != 0) fail("run this as root.");
    struct stat st {};
    if (disk.empty() || stat(disk.c_str(), &st) != 0 || !S_ISBLK(st.st_mode)) {
        std::fprintf(stderr, "Usage: install-to-disk [--yes] /dev/sdX\n");
        std::fprintf(stderr, "The whole disk you name is erased. Disks on this computer:\n");
        for (const auto &l : split_lines(run_cmd({"lsblk", "-dno", "NAME,SIZE,MODEL"}, 10).output)) {
            if (!l.empty()) std::fprintf(stderr, "  /dev/%s\n", l.c_str());
        }
        return 1;
    }
    if (trim(run_cmd({"lsblk", "-dno", "TYPE", disk}, 10).output) != "disk") {
        fail(disk + " is not a whole disk. Name the disk, not a partition.");
    }
    if (!trim(run_cmd({"lsblk", "-no", "MOUNTPOINT", disk}, 10).output).empty()) {
        fail(disk + " has a mounted file system. It may be the disk this session started from.");
    }

    bool efi = fs::is_directory("/sys/firmware/efi");
    std::string grub_target;
    if (efi) {
        struct utsname u {};
        uname(&u);
        std::string m = u.machine;
        if (m == "x86_64") grub_target = "x86_64-efi";
        else if (m == "aarch64") grub_target = "arm64-efi";
        else if (m == "i386" || m == "i686") grub_target = "i386-efi";
        else fail("no boot loader target for " + m + ".");
    }
    std::vector<std::string> tools = {"wipefs", "sfdisk", "mkfs.ext4", "grub-install", "tar", "partprobe"};
    if (efi) tools.push_back("mkfs.vfat");
    for (const auto &t : tools) {
        if (find_in_path(t).empty()) fail(t + " is not on this system, so nothing was changed.");
    }

    // Measure actual rootfs payload (works on overlayfs live ISOs).
    uint64_t need_kb = 0;
    auto du = run_cmd({"du", "-skx",
        "--exclude=/proc", "--exclude=/sys", "--exclude=/dev", "--exclude=/run",
        "--exclude=/tmp", "--exclude=/mnt", "--exclude=/media",
        "--exclude=/var/lib/olympium", "--exclude=/etc/fstab", "/"}, 120);
    auto du_words = split_ws(du.output);
    if (!du_words.empty()) {
        try { need_kb = std::stoull(du_words[0]); } catch (...) {}
    }
    if (need_kb == 0) {
        auto df_lines = split_lines(run_cmd({"df", "-k", "--output=used", "/"}, 15).output);
        if (!df_lines.empty()) {
            try { need_kb = std::stoull(trim(df_lines.back())); } catch (...) {}
        }
    }
    uint64_t disk_bytes = std::stoull(trim(run_cmd({"lsblk", "-bdno", "SIZE", disk}, 10).output));
    uint64_t have_kb = disk_bytes / 1024;
    uint64_t req_kb = need_kb + need_kb / 4 + 600000;
    if (have_kb < req_kb) {
        fail(disk + " is too small: the system needs about " + std::to_string(req_kb / 1024) + " MB.");
    }

    if (!yes) {
        std::printf("Everything on %s will be erased. Type %s to continue: ", disk.c_str(), disk.c_str());
        std::fflush(stdout);
        std::string answer;
        if (!std::getline(std::cin, answer) || answer != disk) {
            fail("not confirmed. Nothing was changed.");
        }
    }

    struct UnmountGuard {
        std::string tgt;
        ~UnmountGuard() {
            run_cmd({"umount", tgt + "/boot/efi"}, 30);
            run_cmd({"umount", tgt}, 30);
        }
    } guard{target};

    std::printf("install-to-disk: partitioning %s (%s)\n", disk.c_str(), efi ? "UEFI" : "BIOS");
    if (run_cmd({"wipefs", "-a", disk}, 30).code != 0) fail("wipefs failed on " + disk);
    std::string table = efi ? "label: gpt\n,512MiB,U\n,,L\n" : "label: dos\n,,L,*\n";
    if (run_cmd({"sh", "-c", "printf '%s' " + sh_quote(table) + " | sfdisk --quiet " + sh_quote(disk)}, 60).code != 0) {
        fail("sfdisk failed on " + disk);
    }
    std::string esp = efi ? part_path(disk, 1) : "";
    std::string rootdev = efi ? part_path(disk, 2) : part_path(disk, 1);
    run_cmd({"partprobe", disk}, 30);
    run_cmd({"udevadm", "settle"}, 30);
    if (stat(rootdev.c_str(), &st) != 0 || !S_ISBLK(st.st_mode)) {
        fail("the new partition " + rootdev + " did not appear.");
    }

    if (run_cmd({"mkfs.ext4", "-q", "-F", "-L", label, rootdev}, 300).code != 0) fail("mkfs.ext4 failed");
    fs::create_directories(target);
    if (run_cmd({"mount", rootdev, target}, 30).code != 0) fail("mount " + rootdev + " failed");
    if (efi) {
        if (run_cmd({"mkfs.vfat", "-F", "32", "-n", "OLYMPIUMEFI", esp}, 60).code != 0) fail("mkfs.vfat failed");
        fs::create_directories(target + "/boot/efi");
        if (run_cmd({"mount", esp, target + "/boot/efi"}, 30).code != 0) fail("mount " + esp + " failed");
    }

    std::printf("install-to-disk: copying the system\n");
    std::fflush(stdout);
    std::string copy_cmd =
        "tar -C / -cf - --one-file-system "
        "--exclude=./proc --exclude=./sys --exclude=./dev --exclude=./run "
        "--exclude=./tmp --exclude=./mnt --exclude=./media "
        "--exclude=./var/lib/olympium --exclude=./etc/fstab . | tar -C " + sh_quote(target) + " -xf -";
    if (run_passthrough({"sh", "-c", copy_cmd}) != 0) fail("system copy failed");
    for (const char *d : {"proc", "sys", "dev", "run", "mnt", "media"}) {
        fs::create_directories(fs::path(target) / d);
    }
    fs::create_directories(fs::path(target) / "tmp");
    chmod((fs::path(target) / "tmp").c_str(), 01777);

    std::string fstab = "LABEL=" + label + " / ext4 errors=remount-ro 0 1\n";
    if (efi) fstab += "LABEL=OLYMPIUMEFI /boot/efi vfat umask=0077 0 1\n";
    write_file_bytes(fs::path(target) / "etc/fstab", fstab);
    write_file_bytes(fs::path(target) / "etc/machine-id", "");

    auto kout = trim(run_cmd({"sh", "-c", "cd " + sh_quote(target + "/boot") + " && ls vmlinuz-* 2>/dev/null | sort -V | tail -1 || true"}, 15).output);
    if (kout.empty()) fail("no kernel was copied to " + target + "/boot.");
    std::string version = kout.substr(std::string("vmlinuz-").size());
    if (!fs::is_regular_file(fs::path(target) / "boot" / ("initrd.img-" + version))) {
        fail("no initrd for kernel " + version + " was copied.");
    }

    fs::create_directories(fs::path(target) / "boot/grub");
    std::string grub_cfg =
        "set timeout=3\n"
        "set default=0\n"
        "insmod part_msdos\n"
        "insmod part_gpt\n"
        "insmod ext2\n"
        "search --no-floppy --label " + label + " --set=root\n"
        "menuentry \"Olympium\" {\n"
        "\tlinux /boot/" + kout + " root=LABEL=" + label + " ro quiet\n"
        "\tinitrd /boot/initrd.img-" + version + "\n"
        "}\n"
        "menuentry \"Olympium, contrast\" {\n"
        "\tlinux /boot/" + kout + " root=LABEL=" + label + " ro quiet olympium.contrast=1\n"
        "\tinitrd /boot/initrd.img-" + version + "\n"
        "}\n";
    write_file_bytes(fs::path(target) / "boot/grub/grub.cfg", grub_cfg);

    std::printf("install-to-disk: installing the boot loader\n");
    std::fflush(stdout);
    int grc = 0;
    if (efi) {
        grc = run_passthrough({"grub-install", "--target=" + grub_target,
            "--efi-directory=" + target + "/boot/efi", "--boot-directory=" + target + "/boot",
            "--removable", "--no-nvram"});
    } else {
        grc = run_passthrough({"grub-install", "--target=i386-pc",
            "--boot-directory=" + target + "/boot", disk});
    }
    if (grc != 0) fail("grub-install failed.");
    sync();
    std::printf("install-to-disk: done. Olympium is on %s (%s). Remove the live medium and restart.\n",
                disk.c_str(), rootdev.c_str());
    return 0;
}
