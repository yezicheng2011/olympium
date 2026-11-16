#!/usr/bin/env python3
"""Verify driver classifications and prevent repeated module-tree scans."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

source = Path(__file__).with_name("driver-report").read_text()
with tempfile.TemporaryDirectory() as directory:
    root = Path(directory)
    modules = root / "modules"
    (modules / "kernel").mkdir(parents=True)
    for name in ("e1000.ko.xz", "virtio_net.ko.zst", "xhci-hcd.ko.gz", "virtio_gpu.ko"):
        (modules / "kernel" / name).touch()
    (modules / "modules.builtin").write_text(
        "kernel/drivers/nvme.ko\nkernel/drivers/hid-multitouch.ko\n")
    # Run the real script against a fixture module tree; do not touch /lib.
    script = root / "report"
    script.write_text("\n".join(
        "MOD=" + shlex.quote(str(modules)) if line.startswith("MOD=") else line
        for line in source.splitlines()) + "\n")
    commands = root / "bin"
    commands.mkdir()
    counter = root / "find-count"
    wrapper = commands / "find"
    wrapper.write_text(
        '#!/bin/sh\nif [ "$1" = ' + shlex.quote(str(modules)) +
        ' ]; then echo scan >> ' + shlex.quote(str(counter)) +
        '; fi\nexec /usr/bin/find "$@"\n')
    wrapper.chmod(0o755)
    result = subprocess.run(
        ["bash", str(script)],
        env=dict(os.environ, PATH=str(commands) + ":" + os.environ["PATH"]),
        capture_output=True, text=True, check=True)
    for expected in ("module e1000 present", "module virtio_net present",
                     "module xhci-hcd present", "module virtio-gpu present",
                     "module nvme built-in", "module hid-multitouch built-in",
                     "module amdgpu absent"):
        assert expected in result.stdout, expected
    assert counter.read_text().splitlines() == ["scan"]
    print("driver report: compressed modules, built-ins, missing modules; one scan: ok")
