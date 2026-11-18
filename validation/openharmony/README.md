# OpenHarmony GUI acceptance

`managed-gui.py --work /tmp/oh-managed --output build/openharmony-managed-report`
requires Linux x86_64 with real KVM. It verifies publisher checksums, builds
the native test clients from the revisions in `sources.json`, and starts a
private copy of the public OpenHarmony 6.1 emulator userdata. Original system,
vendor and updater images use QEMU snapshot overlays.

The bootstrap client is restricted to the x86_general SDK guest. It refuses
credential-protected accounts. Using its own native service identity, it calls
the ordinary account, screen lock and power APIs; it never borrows another
process identity or changes a password. Screen-lock UI is disabled only in
this unprotected test image. The screen timeout override lasts ten guest
minutes. These clients are test fixtures, not installed production services.

Acceptance requires KVM confirmation, an app-owned fresh accessibility tree,
a nonblank physical display capture, actual input producing a changed result
of five, and successful force-stop/relaunch with repeated input. A successful
ability start alone is insufficient. Failures retain logs, trees and a display
capture. APK/SDK archives and guest userdata are not uploaded.

With `--desktop-pointer` and the test compositor built, acceptance also opens
the calculator in a GTK Wayland window. The window displays the app region
from QEMU and translates host pointer events to guest coordinates. The test
compares host pixels and reads guest expression and result after each click.
The [passing desktop run](https://github.com/yezicheng2011/bbb/actions/runs/44230816932)
recorded the host clicking `2+3=5` in that window. The window bridge is specific
to this controlled fixture. Desktop HAP import, arbitrary apps, and commercial
HarmonyOS compatibility still require separate implementation and testing.

The [KVM run with native HAP reinstallation](https://github.com/yezicheng2011/bbb/actions/runs/44222505725)
reached the unprotected launcher, rendered the calculator, and verified
`2+3=5` before and after force-stop and reinstallation. Bundle Manager accepted
the original publisher-signed calculator HAP. The recorded application was
already supplied by the emulator image. See the JSON and screenshot in
`evidence/` for the measured result.
