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
one independent Cocoa probe. Later third-party tests are recorded below.

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
framework gaps applied to that official runtime test.

The first test of the original x86_64 slice from the publisher PikoPixel
1.0 BETA10 DMG reached the New Image dialog with the first two AppKit
patches under `image/darling-patches/`. A host pointer click created a 64×64 document and
changed the title to `Untitled 1 (64x64) (8x)`. The experiment used an
isolated source-built framework path over the official Darling runtime.
The canvas remained gray and the process later exited with an error.
See `evidence/pikopixel-beta10-source-runtime-result.json` for the source,
binary, and patch hashes and the two screenshots.

The complete x86_64 source runtime also builds and runs. The libdispatch patch
avoids registering its internal observer on a kqueue filter that Darling
does not implement. The DarlingServer patch reloads the active native
worker's thread state after a suspendable callback. In the full-runtime
test, PikoPixel rendered its own Wayland dialog and still exited with 139
after New Image. DarlingServer remained running and `darling shell true`
returned zero afterward. See `evidence/full-darling-source-result.json`.

With the current AppKit patches and the full source runtime, the original DMG
installs through the desktop installer and opens a 64×64 transparent canvas
over native Wayland. The patches handle an omitted button alternate title,
route unhandled input through the window controller, copy bitmap storage
independently, and support grayscale bitmap drawing contexts. The app's pencil
now paints visible pixels after a real desktop pointer drag.

The recorded run used the test-only pointer client on a 1280×720 output. Click
**OK** in **New Image**, activate the canvas, and drag across an exposed area
with `pointer-click --drag`. Click the document titlebar close button, verify
the window disappears, and launch the same installed app ID again with
`desktop/dev-install launch`. Create a new image and draw a second stroke.
`evidence/pikopixel-full-runtime-drawing.json` records before/after screenshots,
pixel counts in crops away from the cursor, and the source and binary hashes.
The first process stayed alive after closing its window. Clipboard, native process quit, and GPU acceleration still need validation.

The save round trip used the same AppKit binary and a separate headless test
seat with virtual keyboard input. Command+S opened the application's save
panel and clicking **Save** wrote a native `.piko` document. After closing
its window, the installer launched a new process with the document's Darling
path as an argument. The reopened canvas matched all 26 ink pixels in the
saved document's embedded PNG, including their positions.
`evidence/pikopixel-save-roundtrip.json` records the document SHA-256,
the input path, and the screenshot comparison.

Closing a document after a new stroke originally discarded the unsaved edit
without a prompt. AppKit now counts committed undo groups as edits. Foundation
clears pending actions when the application clears its undo history during
document initialization, so opening a saved file keeps it clean. The patched
runtime closed an untouched document directly. After editing, **Cancel** left
the stroke visible and the file hash unchanged. Choosing **Yes** on the next
close wrote six new ink pixels into the embedded PNG and closed the window.
See `evidence/pikopixel-close-guard.json`.

The independent `clipboard-gui.c` fixture verifies plain text using Cocoa's
`NSPasteboardTypeString` and native Wayland. Compile it with the same target
and link options as `hello-gui.c`. Compile `gtk-clipboard-button.c` on Linux
with `$(pkg-config --cflags --libs gtk+-3.0)`. Install the Mach-O app under
Landlock and click **Copy from Cocoa**, then start the Linux `desk-paste`
client. It reported `Olympium Cocoa copy`. Next click **Copy from Linux**
in the GTK fixture and **Read host clipboard** in Cocoa with both windows
still running. Cocoa read `Linux Wayland copy`. Text after the GTK owner
closed did not pass in the isolated session without the Xwayland clipboard
helper. See `evidence/cocoa-wayland-clipboard.json`.

Independent Cocoa and GTK probes exchanged a 4×4 PNG in both directions
while the source was open. Cocoa published with `NSPasteboard setData:forType:`
and read with `dataForType:`. A direct GTK `image/tiff` request also read a
Cocoa TIFF. Each successful transfer decoded to the expected RGBA pixels,
including alpha. GTK's generic image request did not pick up the TIFF-only
offer. PikoPixel's Copy action did not publish a selection in the tested
document. See `evidence/cocoa-wayland-image-clipboard.json`.

Cocoa and GTK clipboard probes also exchanged the same local file in both
directions while the source was open. The filename contained Chinese
characters, spaces and a percent sign. The receiving probes opened the
file and confirmed its contents via the decoded path. See
`evidence/cocoa-wayland-file-clipboard.json`.

In the full source runtime, the same publisher Hex Fiend 2.3.0 now opens a
known byte sequence using the default Mach-O two-level namespace. It shows
the expected hex and text. Drag-selecting byte `4F` updates the inspector to
`79`, and desktop keyboard input changes that byte to `41` in the editor.
After providing the current application icon through AppKit, Command+S
wrote `41` at byte zero and preserved every other byte. The titlebar closed
the document, and reopening the same file displayed the saved value. See
`evidence/hexfiend-source-runtime-result.json`.
