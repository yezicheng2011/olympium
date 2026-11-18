# Cocoa GUI smoke validation

The independent `hello-gui.c` probe has no Apple SDK assets. `build-smoke.sh`
links an x86_64 Mach-O `.app` against Darling with explicit SDK metadata.
See `evidence/cocoa-result.json` for the exact source and binary hashes and
`evidence/cocoa-button-verified.png` for the actual rendered desktop.

The passing run used Ubuntu 24.04 in a full QEMU VM, original official Darling
GUI packages 0.1.20260609~noble, the headless pixman compositor and Xwayland.
Darling needs a full runtime environment; the restricted Docker container
could not create its mount namespace. Copy the built `.app` to a local ext4
path owned by the desktop user before import; read-only 9p import was rejected
by the existing confinement checks. Do not disable those checks.

For the software renderer, the compositor sets `OLYMPIUM_MAC_SOFTWARE=1` and
selects the installed `image/Xwayland-software` helper, which disables MIT-SHM.
For a source checkout, set `OLYMPIUM_XWAYLAND_SOFTWARE` to its absolute path.
`image/olympium-mac-run` then selects Mesa softpipe and disables its worker
threads. The old Darling native bridge aborted on llvmpipe worker threads;
X11 shared-memory IDs also could not cross its IPC namespace.

With the built `.app` in a local path and the desktop/runtime running:

```sh
export OLYMPIUM_MAC_RUNTIME=/absolute/repository/image/olympium-mac-run
/absolute/repository/desktop/dev-install import --consent /local/OlympiumCocoaFinal.app
```

Open **Olympium Cocoa Test** through desktop search. Confirm a persistent
window titled **Olympium Mac GUI**, then click **Verify Cocoa input**. The
callback must change the title to **Olympium Mac GUI - verified**. The recorded
run used the test-only virtual pointer client described in `../android/README.md`
at output size 1280x720, button coordinates 640,269. Inspect actual geometry
before using those coordinates on another session. No ADB tap or application
RPC invoked the Cocoa callback.

The screenshot demonstrates the callback title change. Its text-field label
did not visibly update, so label repaint is not claimed as verified. This is
one independent Cocoa probe; third-party macOS GUI applications, repeatable
cold starts, clipboard/files and near-native performance remain unverified.


The launch controller now waits for the Mac runtime and reports a nonzero exit
in a desktop dialog. `desktop/test-mac-launch-failure.py` verifies normal exit,
error exit and signal termination with a stand-in runtime; it is not a Mac app
execution test. The real Cocoa probe was reimported under Landlock and again
responded to a host pointer with this controller (`evidence/cocoa-runtime-supervision.json`).

An actual third-party test of the unmodified official Hex Fiend 2.3.0 Intel
archive failed before a usable app window: Sparkle needs `_OBJC_EHTYPE_id`
from WebKit. Diagnostic flat namespace loading progresses to a second missing
symbol, `DARegisterDiskAppearedCallback`; that diagnostic flag is not shipped.
See `evidence/hexfiend-2.3.0-result.json`. The associated screenshot is the
native desktop failure dialog, **not** a functioning Hex Fiend GUI. These
framework gaps remain unresolved.
