# Third-Party Notices — Olympium Universal OS (ISO)

## First-Party Code
All first-party code in this repository (`desktop/`, `vbnet/`, `image/`, `branding/`, and the build docs) is licensed under **Apache License, Version 2.0** ([LICENSE](LICENSE), [NOTICE](NOTICE)). It is not dual-licensed. Nothing in this file relicenses a third-party component to Apache-2.0.

## Third-Party Components Bundled or Invoked in the Universal ISO

Every third-party package stays under its original upstream license (never relicensed to Apache-2.0). Exact installed package versions are emitted to `/usr/share/doc/dev-desktop/sbom.tsv`, and per-package copyright files are preserved under `/usr/share/doc/*/copyright`.

| Component | Role in ISO | License | Notes / Source Availability |
| :--- | :--- | :--- | :--- |
| **Linux kernel** (`linux-image-generic` amd64/arm64, Debian `linux-image-686-pae` i386) | Unmodified OS kernel (`binfmt_misc`, Landlock, DRM/KMS) | **GPL-2.0-only** | Unmodified upstream distro kernel; source via Ubuntu 24.04 / Debian 12 archives |
| **GRUB 2** (`grub-pc-bin`, `grub-efi-amd64-bin`, `grub-efi-ia32-bin`, `grub-efi-arm64-bin`) | Multi-architecture BIOS + UEFI ISO bootloader | **GPL-3.0-or-later** | Unmodified distro packages |
| **live-boot** & **initramfs-tools** | Live SquashFS + tmpfs overlayfs boot | **GPL-3.0-or-later** / **GPL-2.0-or-later** | Unmodified distro packages |
| **QEMU** (`qemu-user-static`, `qemu-system-x86_64`) | Cross-arch user execution (`x86`/`x86_64`/`armhf`/`arm64`) & 64-bit guest on i386 | **GPL-2.0-only** (+ BSD/MIT/LGPL parts) | Standalone binaries registered via `binfmt_misc` |
| **Wine** (`wine`, `wine32`, `wine64`) | Automatic background `.exe`/`.msi` execution | **LGPL-2.1-or-later** | Standalone runtime invoked via `olympium-wine-exec` + `binfmt_misc` |
| **Darling** (`OLYMPIUM_DARLING=1`, optional) | macOS Intel/Universal2 GUI & optional ARM64 execution | **GPL-3.0-only** (+ **APSL-2.0** / BSD / MIT submodules) | Separate installation; `LICENSE` & `SOURCE` placed in `/usr/share/doc/darling/` |
| **.NET 8 Runtime** (`dotnet-runtime-8.0`) | Runs VB.NET `Olympium.dll` desktop & system tools | **MIT** | Unmodified Microsoft .NET 8 runtime |
| **wlroots 0.17**, **Wayland**, **wayland-protocols** | Wayland compositor library & `xdg-shell-protocol.h` | **MIT** | Dynamically linked + MIT generated header |
| **GTK 3, GLib, Pango, GDK-Pixbuf** | Desktop UI windows & PNG icon loading | **LGPL-2.1-or-later** | Dynamically linked shared libraries |
| **Cairo**, **Pixman** | 2D rendering & glass blur buffers | **LGPL-2.1-only OR MPL-1.1** / **MIT** | Dynamically linked shared libraries |
| **libxkbcommon**, **libdrm**, **Mesa llvmpipe**, **Xwayland**, **foot** | Input, graphics, X11 bridge, terminal | **MIT** | Dynamically linked / standalone binaries |
| **Android `adb` & `scrcpy`** (`OLYMPIUM_SLIM_MOBILE=1`) | Slim Android APK install, bidirectional clipboard & display mirroring | **Apache-2.0** | Slim userland tools only; no Android OS image bundled |
| **microG Services Core (`GmsCore` / `GsfProxy`)** | Built-in open-source GMS APIs (NO proprietary Google Play Store bundled) | **Apache-2.0** | Staged under `/opt/olympium/android/microg/`; optional manual Play guide in Settings |
| **OpenHarmony `hdc` / slim tools** (`OLYMPIUM_SLIM_MOBILE=1`) | Slim OpenHarmony `.hap` install & local staging | **Apache-2.0** | Slim tools only; no full OpenHarmony OS image bundled |
| **Mozilla Firefox / Firefox ESR** | Preinstalled web browser | **MPL-2.0** | Unmodified Mozilla APT / Debian package |
| **dpkg**, **rpm2cpio**, **GNU cpio**, **7-Zip (`p7zip-full`)** | Confined `.deb`/`.rpm`/`.dmg`/`.pkg` extraction | **GPL-2.0-or-later** / **GPL-3.0-or-later** / **LGPL-2.1-or-later & BSD** | Standalone child tools executed inside Landlock sandbox |
| **liblzma (`xz-utils`)**, **zlib**, **libxcrypt** | Compression & account password hashing | **0BSD / Public Domain**, **Zlib**, **LGPL-2.1-or-later** | Dynamically linked system libraries |
| **Ubuntu 24.04 / Debian 12 Base Userspace** (`glibc`, `systemd`, `squashfs-tools`, `musl`) | Base OS libraries & utilities | **LGPL-2.1+ / GPL-2.0+ / MIT** | Unmodified distro packages (see `/usr/share/doc/*/copyright`) |
| **Ubuntu Fonts**, **Noto Fonts**, **DejaVu Fonts** | System UI, monospace, and CJK fallback glyphs | **Ubuntu Font License 1.0**, **OFL-1.1**, **Bitstream Vera** | Unmodified font packages |
| **Olympium Runtime** (optional external binary) | Optional external runtime at `/usr/bin/olympium-linux` | Retains its own `LICENSE` & `NOTICE` | Installed to `/usr/share/doc/olympium-runtime/` |
| **microG Reference Constants** (`validation/android/microg-framework/`) | Validation reference only (not staged in ISO) | **Apache-2.0** | Retains upstream LineageOS/microG Apache-2.0 notices |

## GPL / LGPL / MPL Distribution Checklist
When redistributing `olympium-universal.iso`:
1. Keep `/usr/share/doc/dev-desktop/sbom.tsv`, `/usr/share/doc/*/copyright`, and `/usr/share/doc/darling/{LICENSE,SOURCE}` intact inside the image.
2. Provide this `THIRD_PARTY_NOTICES.md`, `LICENSE`, and `NOTICE` alongside the ISO download.
3. For unmodified Ubuntu/Debian/Darling GPL binaries, preserve upstream source-code offer pointers (Ubuntu 24.04 and Debian 12 source archives, plus the Darling commit and patches in `image/darling-patches/`).

## Source pins

These are the pins the build scripts and validation profile already record. Each checkout keeps its upstream license.

- Darling, when built with `image/build-darling.sh`: commit `3fcd2e1aa85079789d19d82415695bdecc512241`, plus the patches in `image/darling-patches/`. The image install copies Darling's `LICENSE` and a `SOURCE` file to `/usr/share/doc/darling/`. Darling is GPL-3.0-only; its submodules stay under their own terms, including APSL where Apple published that code.
- Optional Olympium runtime copied into an image by `image/build-iso.sh`: commit `517aaaa5409f227de42444cf16612d546c29251b`. That project's own `LICENSE` and `NOTICE` are installed at `/usr/share/doc/olympium-runtime/`. This repository does not vendor that runtime and does not relicense it.
- Optional source checkouts from `image/fetch-compat-sources.py`, pinned in `image/compat-sources.tsv`: Waydroid `5b7e2e71be3f6bfaaaab3b461251dacaf1ce4991` (GPL-3.0), Darling `3fcd2e1aa85079789d19d82415695bdecc512241`, and the OpenHarmony manifest `2929176279e011ade6df28357566d95fa3829e9c` (Apache-2.0). Fetching them does not put their system images in the ISO. They are written under `build/compat-sources/`, which is gitignored.
- microG reference constants in `validation/android/microg-framework/` and `image/android-compat/profile.json`: LineageOS `android_frameworks_base` revision `48b4df3c577586ef05a3cd7205fa1c0e71504cbd`, Apache-2.0. The generated framework is not committed in this tree.
