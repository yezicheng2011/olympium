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
The [passing desktop run](https://github.com/yezicheng2011/bbb/actions/runs/44232561716)
recorded the host clicking `2+3=5` in that window, closing the window through
its title bar, and repeating the calculation in a newly opened window. The window bridge is specific
to this controlled fixture. The configured SDK desktop installer is described
below. Arbitrary apps and commercial HarmonyOS still need acceptance.

The [KVM run with native HAP reinstallation](https://github.com/yezicheng2011/bbb/actions/runs/44222505725)
reached the unprotected launcher, rendered the calculator, and verified
`2+3=5` before and after force-stop and reinstallation. Bundle Manager accepted
the original publisher-signed calculator HAP. The recorded application was
already supplied by the emulator image. See the JSON and screenshot in
`evidence/` for the measured result.

The [fresh HAP run](https://github.com/yezicheng2011/bbb/actions/runs/44233740169)
also uses a separate, normal-privilege app built with the public SDK test
signer. Bundle Manager first confirmed the probe package was absent, then
installed it without reinstall mode. A fresh app-owned UI tree, rendered
frame, and visible text change after a click verified first launch and input.
`build-fresh-hap.py` creates the test package from `fresh-hap/`. The test
signer is used only with the isolated SDK image.

## Desktop HAP integration for a configured SDK guest

The desktop recognizes single-entry `.hap` files and registers them with
Launchpad after explicit confirmation and successful guest Bundle Manager
installation. The desktop copies the archive intact. The guest verifies the
signature. A base PNG icon referenced by the package is reused. Launch finds
the entry ability's visible accessibility root and opens its region in a
Wayland window. Closing the window force-stops that application in the guest.
This bridge supports one active app per configured SDK session and primary
pointer clicks. It uses the owned OpenHarmony 6.1 SDK guest's 360×720 display.

Configure an already running SDK emulator with a private JSON descriptor at
`${XDG_RUNTIME_DIR}/olympium-openharmony/session.json` (mode 0600). It contains
the absolute HDC executable path in `hdc`, the desktop user's QMP socket in
`qmp`, and the loopback HDC endpoint in `target`. Launchpad uses this descriptor
from the desktop session. Guest provisioning remains manual. The SDK
acceptance workflow creates a disposable configuration and checks consent,
fresh installation, original icon, launcher input, close, reopen and removal.
Feature-module sets, keyboard and drag forwarding, and HarmonyOS commercial
apps require separate work and testing.

The [desktop HAP KVM run](https://github.com/yezicheng2011/bbb/actions/runs/44289494449)
passed with the normal-privilege SDK probe. It rejected installation without
consent, installed the signed archive into an empty package slot, verified
the original icon, and opened the generated GIO launcher. Pointer input
changed the app's visible text. Titlebar close stopped the guest app and
reopening the launcher repeated the interaction. Removal cleared both the
launcher and the guest package.

The host flow also passed locally with simulated HDC and QMP serving recorded
SDK frames and accessibility trees. Real Wayland mapping, pointer coordinates,
titlebar close and GIO relaunch were exercised in that run. See
`evidence/desktop-hap-host-replay.json`. The KVM workflow remains the test for
native installation and execution through this same desktop launcher.
