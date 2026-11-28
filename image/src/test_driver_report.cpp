/*
 * Copyright 2026 Development Desktop contributors
 * SPDX-License-Identifier: Apache-2.0 — Licensed under the Apache License, Version 2.0. See LICENSE.
 *
 * Verify driver-report C++ binary against a fixture module tree.
 */
#include "common.hpp"

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <string>

using namespace olympium;

int main() {
    char tmpl[] = "/tmp/test-driver-report-XXXXXX";
    char *dir = mkdtemp(tmpl);
    assert(dir != nullptr);
    fs::path root(dir);
    struct Guard { fs::path p; ~Guard() { std::error_code ec; fs::remove_all(p, ec); } } g{root};

    fs::path modules = root / "modules";
    fs::create_directories(modules / "kernel");
    for (const char *name : {"e1000.ko.xz", "virtio_net.ko.zst", "xhci-hcd.ko.gz", "virtio_gpu.ko"}) {
        write_file_bytes(modules / "kernel" / name, "");
    }
    write_file_bytes(modules / "modules.builtin",
                     "kernel/drivers/nvme.ko\nkernel/drivers/hid-multitouch.ko\n");

    auto res = run_cmd({"./driver-report"}, 15, {{"OLYMPIUM_MOD_DIR", modules.string()}});
    assert(res.code == 0);
    for (const char *expected : {
             "module e1000 present",
             "module virtio_net present",
             "module xhci-hcd present",
             "module virtio-gpu present",
             "module nvme built-in",
             "module hid-multitouch built-in",
             "module amdgpu absent",
         }) {
        if (res.output.find(expected) == std::string::npos) {
            std::fprintf(stderr, "FAIL: missing '%s' in driver-report output:\n%s\n",
                         expected, res.output.c_str());
            return 1;
        }
    }
    std::printf("driver-report C++ test: compressed modules, built-ins, missing modules: ok\n");
    return 0;
}
