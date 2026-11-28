/*
 * Copyright 2026 Development Desktop contributors
 * SPDX-License-Identifier: Apache-2.0 — Licensed under the Apache License, Version 2.0. See LICENSE.
 *
 * Print the driver and firmware files this live image actually contains.
 * Walks the module tree once via std::filesystem.
 */
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <sys/utsname.h>
#include <unordered_set>
#include <vector>

namespace fs = std::filesystem;

static void say(const std::string &msg) {
    std::printf("%s\n", msg.c_str());
    std::ofstream kmsg("/dev/kmsg");
    if (kmsg) kmsg << "olympium-drivers: " << msg << "\n";
}

static std::string normalize_mod(std::string s) {
    std::replace(s.begin(), s.end(), '-', '_');
    return s;
}

int main() {
    struct utsname u {};
    uname(&u);
    std::string kver = u.release;
    std::string arch = u.machine;
    const char *override_mod = std::getenv("OLYMPIUM_MOD_DIR");
    fs::path mod = (override_mod && override_mod[0]) ? fs::path(override_mod)
                                                     : (fs::path("/lib/modules") / kver);

    say("kernel " + kver + " arch " + arch);
    std::error_code ec;
    if (!fs::is_directory(mod, ec)) {
        say("no module tree at " + mod.string());
        return 0;
    }

    std::unordered_set<std::string> module_files;
    std::unordered_set<std::string> module_builtin;
    for (auto it = fs::recursive_directory_iterator(mod, fs::directory_options::skip_permission_denied, ec);
         !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (!it->is_regular_file(ec) && !it->is_symlink(ec)) continue;
        std::string base = it->path().filename().string();
        size_t pos = base.find(".ko");
        if (pos != std::string::npos) {
            module_files.insert(normalize_mod(base.substr(0, pos)));
        }
    }
    if (std::ifstream bf(mod / "modules.builtin"); bf) {
        std::string line;
        while (std::getline(bf, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            std::string base = fs::path(line).filename().string();
            if (base.size() > 3 && base.compare(base.size() - 3, 3, ".ko") == 0) {
                base.resize(base.size() - 3);
            }
            module_builtin.insert(normalize_mod(base));
        }
    }

    const std::vector<std::string> want = {
        "e1000", "virtio_net", "virtio_blk", "virtio_pci", "nvme", "ahci",
        "xhci-hcd", "xhci-pci", "usb-storage", "hid-multitouch", "i915", "xe",
        "amdgpu", "radeon", "nouveau", "qxl", "snd", "snd-hda-intel",
        "btusb", "iwlwifi", "ath9k", "bochs", "virtio-gpu",
    };
    for (const auto &name : want) {
        std::string alt = normalize_mod(name);
        if (module_files.count(alt)) {
            say("module " + name + " present");
        } else if (module_builtin.count(alt)) {
            say("module " + name + " built-in");
        } else {
            say("module " + name + " absent");
        }
    }

    const std::vector<std::string> fw_dirs = {
        "i915", "amdgpu", "radeon", "iwlwifi", "ath10k",
        "rtl_nic", "rtlwifi", "qca", "brcm", "mediatek", "cirrus",
    };
    fs::path fw_root("/lib/firmware");
    for (const auto &dir : fw_dirs) {
        fs::path sub = fw_root / dir;
        size_t count = 0;
        if (fs::is_directory(sub, ec)) {
            for (auto it = fs::recursive_directory_iterator(sub, fs::directory_options::skip_permission_denied, ec);
                 !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
                if (it->is_regular_file(ec)) count++;
            }
            say("firmware " + dir + " files=" + std::to_string(count));
        } else if (fs::is_directory(fw_root, ec)) {
            for (auto it = fs::recursive_directory_iterator(fw_root, fs::directory_options::skip_permission_denied, ec);
                 !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
                if (it.depth() > 1) { it.disable_recursion_pending(); continue; }
                if (it->is_regular_file(ec)) {
                    std::string fn = it->path().filename().string();
                    if (fn.rfind(dir, 0) == 0) count++;
                }
            }
            if (count > 0) say("firmware " + dir + " files=" + std::to_string(count));
            else say("firmware " + dir + " absent");
        } else {
            say("firmware " + dir + " absent");
        }
    }

    if (fs::is_directory("/usr/lib/x86_64-linux-gnu/dri", ec) ||
        fs::is_directory("/usr/lib/aarch64-linux-gnu/dri", ec) ||
        fs::is_directory("/usr/lib/i386-linux-gnu/dri", ec)) {
        say("mesa dri present");
    } else {
        say("mesa dri absent");
    }
    say("proprietary nvidia and broadcom-sta are not bundled");
    return 0;
}
