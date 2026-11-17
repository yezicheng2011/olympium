# Olympium

The live session and ISO volume are named Olympium. The menu-bar mark is an original abstract Eastern dragon. It is not an Apple logo, and no Apple artwork, wallpaper, font, or system asset is in this tree.

This is a native Linux live system with its own Wayland desktop. It is not a web page and not Electron. The layout follows the macOS desktop closely: a thin menu bar, left window controls, a floating dock, and a full-screen launcher. Every pixel of it is drawn here, by a small compositor in `desktop/compositor.c`. Windows are normal Wayland clients.

## What the desktop does

- Glass. The menu bar, menus, dock, window title bars, dialogs, and Launchpad read the framebuffer back on every frame, bend it toward the edges, blur it, and add a bright rim and a sheen. It is the live picture behind each surface, not a pre-blurred wallpaper. Glass is the default.
- If the renderer cannot read pixels back, the shell switches to an opaque drawing and says so in Settings. That fallback does not claim glass.
- Menu bar: the dragon mark, the front application's name, File, Edit, View, Go, Window, and Help on the left. A network glyph, search and quick-settings glyphs, and the clock on the right. The network glyph is a fan wedge that fills with the live wireless signal, shows three dots under a full wedge for a wired link, and is struck through with no link.
- Windows: rounded corners, a soft shadow, a centered title, and red, yellow, and green controls on the left. That includes Settings, Files, the terminal, and the purchase reminder.
- Files: a sidebar of favorites and locations, back and forward, icon and list views, search, and an item count. A Mac application folder is shown as one item with its own icon. A disk image and an installer package have their own drawings.
- Dock: a centered glass capsule of unlabeled icons. An icon grows under the pointer, shows its name above the dock, and has a dot while that application has a window open.
- Launchpad: a full-screen blurred backdrop, a search field at the top center, a five-by-three grid of large icons, folders, and page dots. It lists programs that are really on the image, each with its own icon and its whole command: Linux programs, the shortcuts Windows installers create under Wine, and installed Mac applications. Arrow keys, Return, Page Up and Page Down, typing to search, and Escape all work.
- Settings: a search field, an icon sidebar, and grouped rows, in the arrangement of a system-settings window. The panes are appearance, drivers, compatibility, Olympium, license, and about. The sidebar glyphs are original.
- Search: the magnifier in the menu bar, or Control or Super with Space, opens a glass panel. Typing finds programs by name in the same list Launchpad shows, names that start with what was typed first. Up and Down choose, Return starts, Escape closes. It finds programs, not files.
- Quick settings: the two sliders in the menu bar open a short menu. Reduce Transparency and Widgets are ticked when on, a line reports the network, and the last line opens Settings.
- Widgets: the time and date, this month, and memory, disk, and battery, as glass on the wallpaper under the windows. View, Show or Hide Widgets turns them off.
- A window that is not in front has grey controls and a paler title.
- Alt-drag moves a window.
- The dock entry Olympium starts the Olympium runtime, a separate program that the image build copies in when a prebuilt copy is on the build machine. Without it the entry opens an explanation. See `docs/olympium-integration.md`.
- The interface font is the Ubuntu family. Ubuntu Mono is the monospace face. Glyphs those faces do not include come from Noto Sans, then Noto Sans CJK, then DejaVu. Settings → Appearance names that chain.
- Firefox is installed from Mozilla's package archive and is the default handler for http and https. The dock entry starts it. Ubuntu's transitional snap package is not used.
- `install-to-disk /dev/sdX` copies the running system onto a whole disk and makes it bootable, for BIOS or UEFI, whichever this boot used.

What still differs from macOS at a glance: the system font is Ubuntu, not San Francisco. There is one title bar above each application's own toolbar instead of a single merged one. Application content is drawn by GTK, so buttons, text fields, and scroll bars inside a window are GTK's. There is no window-open or minimize animation and no Mission Control. The search panel finds programs and does not look in files. Quick settings is a menu of what this desktop can switch itself; it has no sound, brightness, or Wi-Fi controls.

## Compatibility development

Pinned upstream source roots can be downloaded with `python3 image/fetch-compat-sources.py`. This does not install or enable Android, macOS GUI, or OpenHarmony support. See `docs/five-platform-integration.md` for the verified state and remaining runtime work.

## Build

The build needs root, `debootstrap`, `mksquashfs`, `grub-mkrescue`, and a network path to the Ubuntu 24.04 archive. The x86_64 build also requires a host JDK (`javac`, `jar`, `keytool`) and `musl-tools`. It downloads checksum-pinned Android build tools to build the small microG support assets; the SDK/JDK and guest images stay outside the ISO. On x86_64:

```sh
./image/build-iso.sh amd64
```

The ISO is written to `build/dev-live-amd64.iso` with a sibling `.sha256` file. The script records every installed package in the image at `/usr/share/doc/dev-desktop/sbom.tsv`.

For aarch64, on an x86_64 machine with `qemu-user-static`:

```sh
./image/build-iso.sh arm64
```

That path cross-builds the desktop inside an arm64 chroot and asks GRUB there for an arm64 EFI image. It is slower because every package script runs under qemu-user.

Host checks that do not need an ISO:

```sh
make -C desktop test
make -C desktop test-render
make -C desktop test-startup
make -C desktop test-wine
make -C desktop test-mac
make -C desktop test-packages
```

`test-startup` checks native session startup and driver indexing. `test-render` and `test-startup` need Xwayland, Python 3, and the desktop build dependencies. It checks idle frame counts, shell invalidation, and repeated X window unmap/remap/destroy cycles. Run compositor integration suites sequentially: they share `/run/dev-desktop/control` and status. `test-wine` needs Wine, Xwayland, and mingw on the host. `test-mac` needs 7-Zip, cpio, and python3. `test-packages` needs dpkg-deb, and the rpm tools for its `.rpm` part. Each exits 77 without what it needs. Building `dev-install` needs the `liblzma` and `zlib` headers.

`desktop/protocol/xdg-shell-protocol.h` is generated from wayland-protocols (MIT). Regenerate it with `wayland-scanner server-header` if you need to.

## Boot

x86_64 hybrid ISO, BIOS or UEFI, tested in QEMU:

```sh
qemu-system-x86_64 -m 2048 -smp 2 -cdrom build/dev-live-amd64.iso -boot d \
  -vga std -serial stdio
```

Add `-enable-kvm` when the host is x86_64 and KVM is available. The live root is a squashfs. Writes go to a tmpfs overlay and disappear on power-off.

aarch64, only as a QEMU `virt` guest with AAVMF:

```sh
qemu-system-aarch64 -machine virt -cpu max -m 2048 \
  -bios /usr/share/AAVMF/AAVMF_CODE.fd \
  -cdrom build/dev-live-arm64.iso -boot d -serial stdio \
  -device virtio-gpu-pci -device usb-kbd -device usb-tablet
```

That aarch64 command is emulated when the host is x86_64. It does not show that the image boots a Raspberry Pi or Apple Silicon. Details are in `docs/firmware.md`.

i386, a 32-bit kernel, tested in QEMU as an emulated PC:

```sh
qemu-system-i386 -M pc -m 2048 -cdrom build/dev-live-i386.iso -boot d \
  -vga virtio -serial stdio
```

Use `-vga virtio`. Debian 6.1's bochs display does not advertise PRIME import, and this compositor will not open that device. There is no armhf image. See `docs/32-bit.md`.

`install-to-disk /dev/sdX` from a terminal on the live system erases that disk, copies the running system onto it, and installs the boot loader. It asks you to type the device name before it changes anything.

## Mac applications

A Mac disk image (`.dmg`), installer package (`.pkg`), or application folder (`.app`) opens in Install. The application is copied into this desktop's Applications folder and appears in Launchpad with its own icon. Nothing from the file is run, and a package's installer scripts are not used. The unpacking is done by a confined child process that can write nowhere but its staging folder. `docs/mac-apps.md` says what was run.

An installed Mac application is started through Darling, the open-source project that runs macOS programs on Linux. `image/build-darling.sh` builds it from source and the image build copies it in when it is asked to with `OLYMPIUM_DARLING=1`. Without Darling, opening a Mac application shows a window that says so, and the application stays installed. This desktop has no Mach-O loader of its own and no other way to start one.

On the build machine, with Darling built from source there, a real Mach-O command-line program was carried in a `.dmg` and in a `.pkg`, installed by the desktop, started through Darling, and printed its arguments. No graphical Mac application was run. The Darling that was built has no AppKit, and Darling describes its support for graphical applications as in development.

This image does not include Apple trademarks, fonts, artwork, an Apple SDK, or a macOS system image, and it does not bypass code signing or DRM. Apple's own applications are licensed for Apple hardware and are not what this is for. The test files are written byte by byte by `desktop/mac-fixtures.py`; none comes from Apple or from any vendor.

The dock, Launchpad, and Files also open the Lawful test app. It is a Linux window. It is not a Mac application. The Olympium runtime at `517aaaa5409f227de42444cf16612d546c29251b` lists that program as a Linux executable. It also lists a Mach-O header fixture and does not start it. This repository does not change that runtime, and Mac files are not sent to it. iOS is not part of this system.

`dev-compat probe FILE` identifies a Mach-O or fat header and stops. `docs/isa-and-abi.md` and `docs/compatibility-matrix.md` are the current matrix. No Apple, Google Play, or Windows system image is bundled. See `docs/guest-images.md`.

## Installing a program

Double-clicking a file in Files opens Install. The file is not run. A Linux program whose plan is a native process can be imported after a checkbox, then launched. Launch uses `olympium-run` when that helper is installed, so a 32-bit program on a 64-bit CPU still takes the native path and a 64-bit program on the i386 image still takes the swap-backed guest.

A Mach-O file on its own can be kept and is not started. The pinned runtime refuses macOS software on this Linux builder (`apple-platform-on-non-apple-hardware`), and that refusal is left as it is. A `.dmg`, `.pkg`, or `.app` is handled by the desktop, as the section above says. No macOS guest is downloaded. An iOS package is refused. Nothing is downloaded.

A real `.deb` is extracted with `dpkg-deb -x` after consent. Maintainer scripts are not run. The extraction, like that of an `.rpm`, runs in a confined child that can write only under the folder it unpacks into, and what comes out loses its set-user-ID bits; `docs/desktop-apps.md` says what was tried against it. That path was booted on an earlier x86_64 image, `c30c081c502058e0859d4d1c72d874e0ca061181b38982bc03a13f9e520edc2f`, and is not the image below. A real `.rpm` is extracted with `rpm2cpio` and `cpio` after consent. Package scripts are not run. A real Windows `.exe` is imported after consent and launched with the distro Wine binary the runtime's plan names (`/usr/bin/wine`). Wine is not a Windows license, and no Windows image is downloaded. Flatpak, snap, AppImage, and `.msi` are not claimed. `desktop/test-install.sh` still requires stub archives to stay uninstalled. The matrix is `docs/desktop-apps.md`.

## What this environment actually booted

Three images, volume id `OLYMPIUM`. These checksums predate `dev-install` and do not contain it. The x86_64 and aarch64 images carry `/usr/bin/olympium-linux` from commit `517aaaa5409f227de42444cf16612d546c29251b`. The i386 image has no runtime binary for that architecture, so its Olympium dock entry opens Settings. The interface font on all three is Ubuntu. `install-to-disk` was verified on an earlier x86_64 image (`25ee807c92c2e7bf7b96fbd5309d0858e996b81202824fee1d1db8d4c51e0f20`) and was not run again for the checksums below. Older part trees, including `a97dabb9…` and `8fc90725…`, were left in place.

x86_64 ISO `build/dev-live-amd64.iso`, 1,465,339,904 bytes, hybrid BIOS and UEFI.

```
6258fda904f660ce2011f9e7f4e27e99548e99fa1e0d2818f64b0388dfece955  dev-live-amd64.iso
```

QEMU `pc`, KVM, on an x86_64 host. The desktop ran a real i386 ELF by the kernel's IA-32 path. The log says `olympium-run: native IA-32` and the program printed `hello32 bits=32 machine=i386`. The same boot mapped the Lawful test app window. Mozilla Firefox in this image is `160.0~build1` from `packages.mozilla.org`. The picture is `olympium-x86_64-13-hello32-testapp.png`. Settings, the Ubuntu font row, and the Firefox window were drawn on the previous image `a97dabb9…`. That Firefox window is Firefox's welcome screen.

aarch64 ISO `build/dev-live-arm64.iso`, 1,575,903,232 bytes. It contains `/efi/boot/bootaa64.efi`. Desktop binaries are aarch64. `/usr/lib/firefox/firefox` is aarch64 and printed `Mozilla Firefox 160.0` in the build chroot.

```
2ce330f0d42a6aff714dbc7d80490c13ec2e02e93a004c764c4f69592f9272df  dev-live-arm64.iso
```

The kernel has `CONFIG_ARM64=y` and `CONFIG_COMPAT=y`. QEMU `virt`, `-cpu cortex-a57`, software emulation on an x86_64 host, booted that kernel and executed the image's armhf `hello32`. It printed `hello32 bits=32 machine=arm` and exited 0. That is not a Raspberry Pi, a generic ARM board, or Apple Silicon, and it is not a full desktop draw of this image. The earlier arm64 Files picture is from `8600cb65…`. The arm64 kernel does not include `i915.ko`.

i386 ISO `build/dev-live-i386.iso`, 724,064,256 bytes. The boot kernel is Debian `6.1.0-54-686-pae` (`CONFIG_X86_32=y`, `CONFIG_64BIT` unset). Userspace is Debian 12. The browser is Debian `firefox-esr` `153.7.0esr-1~deb12u1`, labeled as a Debian rebuild. It is not Mozilla's packages.mozilla.org build, and its window was not drawn in this boot.

```
bef87685bdebc51f24a8b1117a652a01aea65189a50bb3cc0e43dcfbac16d7a2  dev-live-i386.iso
```

`qemu-system-i386 -M pc -vga virtio`, KVM, 2 GiB, on an x86_64 host. The session log says `arch i686 6.1.0-54-686-pae`. The compositor started. The Lawful test app opened (the log says `window mapped: Olympium (app olympium-testapp)`; the window's own subtitle is "Lawful test app"). A terminal then ran four 64-bit programs with the image's 32-bit `qemu-system-x86_64`, TCG, 512 MiB of guest RAM in a file on an extra virtio disk, and a 2 GiB swap file on that disk. All four exited 0: `hello64 bits=64 machine=x86_64` in 7 seconds, a mapped page at `0x100000000` in 6 seconds, GNU bash 5.2.21 reporting `x86_64` in 5 seconds, and `payload64 bits=64` in 5 seconds. SwapTotal was 2,097,148 kB and SwapFree fell to 2,096,884 kB, so 264 KiB was in swap. This is an emulated PC, not a physical 32-bit computer. The pictures are `olympium-i386-01-desktop.png` and `olympium-i386-02-64bit.png` (2:02 AM ET, Sunday, November 15, 2026). A boot with `-vga std` reached the same kernel and then the compositor exited, because bochs-drm does not advertise PRIME import. There is no armhf ISO. A 64-bit program that needs more than about 2 GiB of guest RAM still does not run: the translator is a 32-bit process.

On the build machine the Firefox content sandbox cannot create a user namespace (`clone` returns `EPERM`). The live image sets `kernel.apparmor_restrict_unprivileged_userns` to 0 so the sandbox can. If a boot still blocks it, `firefox-launch` starts Firefox again with the content sandbox off and writes that fact to the session log.

## Install window on a booted desktop

x86_64 ISO `build/dev-live-amd64-install.iso`, gzip squashfs, 2,256,891,904 bytes. This is not one of the three images above, and it does not replace their part files. Ubuntu Wine `9.0~repack-4build3` and Xwayland are installed from Ubuntu packages. The compositor keeps Xwayland running so a Wine window is a window on this desktop.

```
9c1b41e427bd891a6887a305de0a3cbfc8024a6f8786875ab1620c9e601c07a1  dev-live-amd64-install.iso
```

QEMU `pc`, KVM, `-vga std`, on an x86_64 host, append `olympium.showwine=1 olympium.importwine=1`. Those flags are the boot test's consent. A normal boot does not import anything. The session log, from the compositor clock:

- `xwayland on DISPLAY=:0` at 1.1s, then the session started.
- Install mapped at 9.6s for `desk-win.exe`. The checkbox was off and the window said nothing had been run. Picture: `wine-review.png`.
- Import exited 0 and did not start the program. Id `wine-9425039c`.
- A Wine setup window mapped at 24.6s (`wineboot.exe`).
- `Desk Win` mapped at 43.3s and painted `Desk Win ready`. Picture: `wine-window.png`. The program's own timer, from its process start to `WM_CREATE`, was 26 ms. That number is not the time from the click to the window.
- The exchange file read back `Desk Win ready`. A Wayland client was not shown pasting the Wine clipboard.
- `rpm2cpio` extracted `desk-rpm` and the program wrote `desk-rpm-ready`. It is a shell script, not a window.
- Launchpad at 55.6s listed `desk-win.exe` and `desk-rpm.rpm`, and the dock had a pin for each. Picture: `wine-launchpad.png`.

Of that 43.3 s, about 22 s was the boot test sleeping so its pictures could be taken, about 19 s was Wine building its prefix for the first time, about 1 s was the desktop, and the program was 26 ms. `docs/startup-latency.md` has the table. The image build now builds that prefix (`image/wine-prefix.sh`), launch reuses `wineserver` for 60 s and then lets it exit, and prewarm exists and is off. On the build machine, with no ISO boot, the launch request to the mapped window was 4.8 s when Wine built the prefix, 0.67 s with the prefix in place, and 0.16 s when Wine was still running from the last launch. `9c1b41e4…` was not rebuilt, so it does not contain any of this.

A Windows program and a Linux program share a clipboard for text, a folder that is drive `S:` on the Windows side, and documents: a Linux path handed to a Windows program arrives as `Z:\...`. `docs/interoperation.md` lists what was run and what is missing.

`make -C desktop test-wine` on the build machine also copied text from the Wine window to a Wayland program and from a Wayland program to the Wine window. That is the compositor with the headless backend, not a booted ISO. There is no Android runtime in the image, so nothing connects an Android clipboard.

`dev-install` now reads which CPU a `.exe` was built for and offers Launch only when this system can run it. A 32-bit Windows program on the x86_64 image needs `wine32`, which `9c1b41e4…` does not have: there it opened no window and Wine exited 0. `image/install-wine.sh` installs the right packages per image, and `docs/wine-architectures.md` has the table and what was run.

The pictures were taken at about 5:21 AM ET, Sunday, November 15, 2026. i386 and arm64 were not rebuilt with Wine. An x86 or x86_64 `.exe` does not run on the arm64 image without an x86 translator. That translator is not in this tree, and `dev-install` refuses the file instead of starting it. Nothing here runs on macOS. The earlier `.deb` boot remains `c30c081c…`.

## Purchase reminder

For the first 5 days after the first boot, Olympium does not ask for a license. After that, one reminder says the system is commercial and asks you to purchase one. It has a close control. Closing it leaves the desktop, menus, Launchpad, blur, drivers, and Olympium fully usable. The same reminder can appear again every 5 days until `olympium-activate LICENSE-KEY` records a purchase, which only hides the reminder.

A personal computer, a school, a business, an intranet, a cloud or CI image, and a factory or bulk install all follow that same rule. There is no second dialog, no watermark, no feature lock, and no shorter timer for those installs. The Olympium name and dragon mark stay on the boot splash and menu bar either way. That is the product name, not a lock.

## Licenses

First-party source is under daniel.ye Source License, Version 1.0. The full text is [LICENSE](LICENSE). Ubuntu packages and the libraries they contain keep their own licenses (`THIRD_PARTY_NOTICES.md`). The image is built from Ubuntu packages and is not Ubuntu. Ubuntu is a trademark of Canonical.
