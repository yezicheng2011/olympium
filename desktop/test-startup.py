#!/usr/bin/env python3
"""Verify native startup does not wait for Xwayland or launch twice."""
import os
from pathlib import Path
import shlex
import signal
import subprocess
import tempfile
import time


HERE = Path(__file__).resolve().parent


def run(native):
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        runtime = root / "run"
        runtime.mkdir(mode=0o700)
        # Deliberately slow X: checking event order is more reliable than a
        # machine-specific startup-time threshold.
        wrapper = root / "Xwayland"
        wrapper.write_text('#!/bin/sh\nsleep 1.5\nexec /usr/bin/Xwayland "$@"\n')
        wrapper.chmod(0o755)
        env = dict(os.environ, XDG_RUNTIME_DIR=str(runtime), WLR_BACKENDS="headless",
                   WLR_RENDERER="pixman", DEV_DESK_HEADLESS="1",
                   WLR_XWAYLAND=str(wrapper), OLYMPIUM_DESK_HOME=str(root / "desk"))
        env.pop("DISPLAY", None)
        env.pop("WAYLAND_DISPLAY", None)
        command = [str(HERE / "dev-compositor")]
        if native:
            command.append("-n")
        command += ["-s", "exec " + shlex.quote(str(HERE / "olympium-testapp"))]
        log = root / "compositor.log"
        with log.open("w") as output:
            started = time.monotonic()
            process = subprocess.Popen(command, env=env, stdout=output,
                                       stderr=output, start_new_session=True)
            try:
                mapped_at = None
                deadline = started + 15
                while time.monotonic() < deadline:
                    text = log.read_text()
                    if process.poll() is not None:
                        raise AssertionError(text)
                    mapped = "window mapped: Olympium (app olympium-testapp)"
                    if mapped in text and mapped_at is None:
                        mapped_at = time.monotonic() - started
                    if mapped in text and "xwayland on DISPLAY=" in text:
                        assert text.count("session start (") == 1, text
                        assert text.count(mapped) == 1, text
                        first_app = text.index(mapped)
                        first_x = text.index("xwayland on DISPLAY=")
                        assert (first_app < first_x) == native, text
                        if native and os.environ.get("TEST_STARTUP_WINE") == "1":
                            check_wine(root, env, log)
                        print(f"{'native' if native else 'X-compatible'}: "
                              f"client mapped in {mapped_at:.3f}s; session launched once")
                        return
                    time.sleep(0.02)
                raise AssertionError("Startup timed out:\n" + log.read_text())
            finally:
                if process.poll() is None:
                    os.killpg(process.pid, signal.SIGTERM)
                process.wait()


def check_wine(root, env, compositor_log):
    """The early native session has no DISPLAY; use the session note."""
    exe = root / "desk-win.exe"
    subprocess.run(["x86_64-w64-mingw32-gcc", "-mwindows", "-O2", "-o",
                    str(exe), str(HERE / "desk-win.c")], check=True)
    wine_env = dict(env, OLYMPIUM_BIN="/nonexistent", WINEDEBUG="-all",
                    WINEDLLOVERRIDES="mscoree,mshtml=", OLYMPIUM_WINE_IDLE="0")
    with (root / "wine.log").open("w") as output:
        subprocess.run([str(HERE / "dev-install"), "import", "--consent", str(exe)],
                       env=wine_env, stdout=output, stderr=output, check=True, timeout=30)
        records = [line.split("\t") for line in
                   (root / "desk/library.tsv").read_text().splitlines()]
        app_id = next(fields[0] for fields in records
                      if len(fields) > 5 and fields[5] == "windows_exe")
        try:
            subprocess.run([str(HERE / "dev-install"), "launch", app_id, "--once"],
                           env=wine_env, stdout=output, stderr=output, check=True, timeout=90)
            assert "window mapped: Desk Win" in compositor_log.read_text()
            print("native session: Wine window maps without inherited DISPLAY")
        finally:
            subprocess.run(["wineserver", "-k"],
                           env=dict(wine_env, WINEPREFIX=str(root / "desk/wine")),
                           stdout=output, stderr=output, check=False, timeout=10)


if __name__ == "__main__":
    run(False)
    run(True)
