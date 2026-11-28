/*
 * Copyright 2026 Development Desktop contributors
 * SPDX-License-Identifier: Apache-2.0 — Licensed under the Apache License, Version 2.0. See LICENSE.
 *
 * Optional install for drivers that are not bundled.
 */
#include "common.hpp"

#include <cstdio>
#include <fstream>
#include <iostream>
#include <regex>
#include <string>
#include <sys/utsname.h>
#include <vector>

using namespace olympium;

static void usage(FILE *out) {
    std::fprintf(out,
        "Usage: driver-install [--detect] [--install nvidia[:BRANCH]|broadcom]\n\n"
        "Bundled: kernel modules (i915, xe, amdgpu, radeon, nouveau), Mesa\n"
        "(iris, crocus where the arch ships it, radeonsi, r600, ANV, RADV, NVK),\n"
        "and the redistributable linux-firmware files. The proprietary NVIDIA\n"
        "kernel module is not in the ISO.\n\n"
        "NVIDIA branches you can ask for, from the Ubuntu 24.04 archive:\n"
        "  nvidia:580       nvidia-driver-580. Prebuilt for kernel 6.8.0-149.\n"
        "                   Widest current branch: Maxwell, Pascal, Volta, Turing,\n"
        "                   Ampere, Ada, and later cards that the 580 series still lists.\n"
        "  nvidia:580-open  nvidia-driver-580-open. Open kernel module. Turing and newer.\n"
        "  nvidia:595       nvidia-driver-595. Prebuilt for kernel 6.8.0-149.\n"
        "                   Newer branch. Maxwell, Pascal, and Volta are not in this branch.\n"
        "  nvidia:470       nvidia-driver-470. Kepler (GeForce 600/700 era) only.\n"
        "                   No prebuilt module for 6.8.0-149. DKMS must compile it,\n"
        "                   which needs linux-headers and can fail.\n"
        "  nvidia           same as nvidia:580.\n\n"
        "Not offered. This archive has no package, so the script will not invent one:\n"
        "  nvidia-driver-390 and older, Fermi (GeForce 400/500), and anything before Kepler.\n"
        "  The noble-updates package nvidia-driver-535 is only a transitional package\n"
        "  that depends on nvidia-driver-580. It is not a separate 535 driver.\n\n"
        "--install requires a matching PCI device, a network path to the Ubuntu\n"
        "archive, the restricted component, and typing YES. It prints the license\n"
        "step before apt runs. After a successful install it tries modprobe and\n"
        "tells you to reboot; it does not claim the desktop has switched yet.\n");
}

static void nvidia_note() {
    std::printf(
        "Proprietary NVIDIA is not installed. Nouveau and its Mesa driver are.\n"
        "Branches: 580 (widest, prebuilt for 6.8.0-149), 580-open (Turing+),\n"
        "595 (newer, no Maxwell/Pascal/Volta), 470 (Kepler, DKMS, may fail to build).\n"
        "Fermi and older have no package in this archive.\n"
        "License: NVIDIA proprietary license, shown by the package. Network: required.\n"
        "Command: driver-install --install nvidia:580   (or :595, :470, :580-open)\n");
}

static std::string get_lspci() {
    if (find_in_path("lspci").empty()) return "";
    return run_cmd({"lspci", "-nn"}, 15).output;
}

static std::vector<std::string> grep_icase(const std::string &text, const std::string &needle) {
    std::string lower_needle = needle;
    std::transform(lower_needle.begin(), lower_needle.end(), lower_needle.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::vector<std::string> hits;
    for (const auto &line : split_lines(text)) {
        std::string low = line;
        std::transform(low.begin(), low.end(), low.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (low.find(lower_needle) != std::string::npos) hits.push_back(line);
    }
    return hits;
}

static void detect() {
    std::printf("PCI devices:\n");
    std::string pci;
    if (!find_in_path("lspci").empty()) {
        pci = run_cmd({"lspci", "-nn"}, 15).output;
        std::printf("%s", pci.c_str());
    } else {
        std::printf("lspci is not installed\n");
    }
    std::printf("USB devices:\n");
    if (!find_in_path("lsusb").empty()) {
        std::printf("%s", run_cmd({"lsusb"}, 15).output.c_str());
    } else {
        std::printf("lsusb is not installed\n");
    }
    auto nv = grep_icase(pci, "10de:");
    if (!nv.empty()) {
        std::printf("NVIDIA GPU detected (vendor 10de).\n");
        for (const auto &l : nv) std::printf("%s\n", l.c_str());
        nvidia_note();
    } else {
        std::printf("No NVIDIA PCI device (10de) detected. Nothing to install for NVIDIA.\n");
        std::printf("Nouveau remains the bundled driver if a card appears later.\n");
    }
    auto bc = grep_icase(pci, "14e4:");
    if (!bc.empty()) {
        std::printf("Broadcom PCI device detected (vendor 14e4).\n");
        std::printf("Many Broadcom chips use the bundled brcmfmac firmware.\n");
        std::printf("broadcom-sta-dkms is the proprietary wl alternative and is not bundled.\n");
        std::printf("License: Broadcom STA license. Network: required for apt.\n");
    } else {
        std::printf("No Broadcom PCI device (14e4) detected.\n");
    }
    std::printf("Bundled GPU modules when this kernel ships them: i915, xe, amdgpu, radeon, nouveau, bochs, virtio-gpu, qxl.\n");
    std::printf("A QEMU boot is not a test of a real AMD, Intel, or NVIDIA card, a generic ARM board, or Apple Silicon.\n");
}

static bool archive_reachable() {
    std::string host;
    if (std::ifstream in("/etc/apt/sources.list"); in) {
        std::string line;
        while (std::getline(in, line)) {
            if (line.rfind("deb ", 0) == 0) {
                auto words = split_ws(line);
                if (words.size() >= 2) {
                    host = words[1];
                    if (host.rfind("http://", 0) == 0) host = host.substr(7);
                    else if (host.rfind("https://", 0) == 0) host = host.substr(8);
                    size_t sl = host.find('/');
                    if (sl != std::string::npos) host = host.substr(0, sl);
                    break;
                }
            }
        }
    }
    if (!host.empty() && run_cmd({"ping", "-c", "1", "-W", "3", host}, 10).code == 0) return true;
    return run_cmd({"ping", "-c", "1", "-W", "3", "archive.ubuntu.com"}, 10).code == 0;
}

static void install_pkg(const std::string &name, const std::string &pkg) {
    if (find_in_path("apt-get").empty()) {
        std::fprintf(stderr, "Failed: apt-get is not available, so %s cannot be installed.\n", name.c_str());
        std::exit(1);
    }
    if (!archive_reachable()) {
        std::fprintf(stderr, "Failed: no route to the Ubuntu archive. %s needs a network and was not installed.\n", name.c_str());
        std::exit(1);
    }
    std::printf("About to install %s.\n", pkg.c_str());
    std::printf("This downloads software under that package's own license, not this image's license.\n");
    std::printf("Read the license apt prints. Type YES to continue:\n");
    std::fflush(stdout);
    std::string answer;
    if (!std::getline(std::cin, answer) || answer != "YES") {
        std::printf("Stopped. Nothing was installed.\n");
        std::exit(1);
    }
    if (run_cmd({"apt-cache", "show", pkg}, 30).code != 0 && fs::is_regular_file("/etc/apt/sources.list")) {
        std::string sources = read_file_bytes("/etc/apt/sources.list");
        if (sources.find("restricted") == std::string::npos) {
            std::printf("%s is not in main or universe.\n", pkg.c_str());
            std::printf("Adding the Ubuntu restricted component, which is where this proprietary package is published.\n");
            std::string updated;
            for (const auto &line : split_lines(sources)) {
                if (line.size() >= 14 && line.compare(line.size() - 14, 14, " main universe") == 0) {
                    updated += line + " restricted\n";
                } else {
                    updated += line + "\n";
                }
            }
            write_file_bytes("/etc/apt/sources.list", updated);
        }
    }
    setenv("DEBIAN_FRONTEND", "noninteractive", 1);
    if (run_passthrough({"apt-get", "update"}) != 0) {
        std::fprintf(stderr, "Failed: apt-get update did not succeed. Check the network and /etc/apt/sources.list.\n");
        std::exit(1);
    }
    if (run_cmd({"apt-cache", "show", pkg}, 30).code != 0) {
        struct utsname u {};
        uname(&u);
        std::fprintf(stderr, "Failed: %s is not published for %s in this archive. Nothing further was installed.\n",
                     pkg.c_str(), u.machine);
        std::fprintf(stderr, "The prebuilt NVIDIA desktop packages are amd64. An arm64 machine keeps the bundled nouveau driver.\n");
        std::exit(1);
    }
    if (run_passthrough({"apt-get", "install", "-y", pkg}) != 0) {
        std::fprintf(stderr, "Failed: apt-get install %s did not succeed.\n", pkg.c_str());
        std::exit(1);
    }
    std::printf("Installed %s.\n", pkg.c_str());
}

static void nvidia_after_install(const std::string &pkg) {
    std::printf("Reboot before the desktop leaves nouveau.\n");
    std::printf("The Ubuntu package blacklists nouveau on the next boot and loads nvidia instead.\n");
    auto res = run_cmd({"modprobe", "nvidia"}, 30);
    if (res.code == 0) {
        std::printf("modprobe nvidia loaded a module in this session.\n");
        std::printf("The running desktop is still on nouveau until you reboot.\n");
    } else {
        std::printf("modprobe nvidia did not load a module.\n");
        for (const auto &l : split_lines(res.output)) std::printf("  %s\n", l.c_str());
        std::printf("If the device is busy, nouveau already owns it. Reboot, then run nvidia-smi.\n");
        std::printf("If the message says the module is missing, the DKMS build did not produce nvidia.ko.\n");
        std::printf("Look in /var/lib/dkms and do not treat this install as a working driver yet.\n");
    }
    std::printf("Package installed: %s. Display switch happens at the next boot, not now.\n", pkg.c_str());
}

int main(int argc, char **argv) {
    std::string mode = "detect";
    std::string target;
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--detect") {
            mode = "detect";
        } else if (arg == "--install") {
            mode = "install";
            target = (i + 1 < argc) ? argv[++i] : "";
        } else if (arg == "-h" || arg == "--help") {
            usage(stdout);
            return 0;
        } else {
            std::fprintf(stderr, "Unknown argument: %s\n", arg.c_str());
            usage(stderr);
            return 2;
        }
    }
    detect();
    if (mode == "detect") return 0;

    if (target == "nvidia" || target == "nvidia:580" || target == "nvidia:580-open" ||
        target == "nvidia:595" || target == "nvidia:470") {
        if (grep_icase(get_lspci(), "10de:").empty()) {
            std::fprintf(stderr, "Failed: no NVIDIA device detected, so no nvidia-driver package was installed.\n");
            return 1;
        }
        std::string pkg = "nvidia-driver-580";
        if (target == "nvidia:580-open") pkg = "nvidia-driver-580-open";
        else if (target == "nvidia:595") pkg = "nvidia-driver-595";
        else if (target == "nvidia:470") pkg = "nvidia-driver-470";
        if (pkg == "nvidia-driver-470") {
            struct utsname u {};
            uname(&u);
            std::printf("470 is the Kepler legacy branch. Fermi and older are not supported and will not be installed.\n");
            std::printf("There is no prebuilt 470 module for kernel %s. DKMS will try to compile one.\n", u.release);
            install_pkg("kernel headers for DKMS", std::string("linux-headers-") + u.release);
            install_pkg("compiler for DKMS", "build-essential");
        }
        install_pkg("NVIDIA proprietary driver", pkg);
        nvidia_after_install(pkg);
        return 0;
    }
    if (target == "broadcom") {
        if (grep_icase(get_lspci(), "14e4:").empty()) {
            std::fprintf(stderr, "Failed: no Broadcom device detected, so broadcom-sta-dkms was not installed.\n");
            return 1;
        }
        install_pkg("Broadcom STA", "broadcom-sta-dkms");
        std::printf("Reboot before wl replaces the in-tree Broadcom driver.\n");
        return 0;
    }
    std::fprintf(stderr, "Failed: --install needs nvidia, nvidia:580, nvidia:580-open, nvidia:595, nvidia:470, or broadcom.\n");
    std::fprintf(stderr, "Fermi and older, and a real 535 driver, are not offered. nvidia-driver-535 in noble-updates depends on 580.\n");
    return 2;
}
