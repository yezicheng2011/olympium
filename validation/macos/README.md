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
