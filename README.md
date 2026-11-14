# Linkspan

Linkspan runs a program that belongs to another system on the computer you already have. There is a client for Android, Windows, Linux, macOS, and OpenHarmony. They share one engine that decides what can run, which runtime would run it, and what this project refuses to do.

There is no iOS app. iOS software is a guest, not a host. The OpenHarmony client is a host. Its manual is [docs/openharmony-host.md](docs/openharmony-host.md).

Linkspan does not reimplement Windows, Android, Linux, macOS, iOS, or OpenHarmony. A guest runs in a real system: the host itself, or a slim image of that system downloaded when you open the app. Wine is kept for Windows programs on Linux and macOS. It opens a native host window. It is not used for OpenHarmony. Where a combination cannot run without a translation layer, the client refuses it and says why.

This repository is the product source. The official site is https://olympium.net. It does not include a guest system image. Slim images live in separate repositories and are downloaded at runtime. GitHub Actions can build an ad-hoc signed Mac package. Certificates are not stored here.

## License

Linkspan's own source is [Apache-2.0](LICENSE) only. Copyright 2026 Ye Zicheng. It is not dual-licensed, and it is not offered under the MIT license or the GPL.

Engines and system libraries you install stay under their own licenses. The Linux desktop client links to the system copies of GTK 4 and libadwaita, which remain LGPL-2.1 or later. That does not change Linkspan's license. See [NOTICE](NOTICE).

## What you can add

| Kind | Examples | What Linkspan does |
| --- | --- | --- |
| Android | `.apk`, an installed package name | Installs or opens it on Android. On a desktop, uses Waydroid or `adb` if you already have one, or the slim AOSP image after it is published. |
| Windows | `.exe`, `.msi` | Runs it directly on Windows. On Linux or macOS, uses Wine, or Proton for an executable when you select Proton. Wine is kept. A slim Wine runtime can be downloaded. Neither is a Windows license. |
| Linux | ELF, script, `.deb`, `.rpm`, AppImage, Flatpak, snap | Runs executables, scripts, AppImages, and `.deb` packages on Linux. `.deb` packages are extracted for your user and are not installed system-wide. RPM needs `rpm2cpio`. Flatpak and snap run only by the name of an app that is already installed. |
| macOS | `.app`, `.dmg`, `.pkg` | Opens them with macOS. Other hosts keep the file and refuse to start it. |
| iOS | an installed bundle id, a Simulator `.app` you compiled, an `.ipa` | On a Mac, opens an iOS app that is already installed, or launches a Simulator build you compiled with Xcode. An `.ipa` is recognized and refused everywhere, including on a Mac. |
| OpenHarmony | `.hap`, an OpenHarmony `.app` bundle, an installed bundle name | Runs in a real OpenHarmony system. On an OpenHarmony device, that system is the device. On Linux, Windows, and macOS, it is a connected device or the slim image from `olympium-openharmony`. Android refuses it. HarmonyOS commercial images are refused. |

## What runs in this repository today

On a Linux machine you can build the command and the GTK app. Both can start the sample Linux program. A `.deb` guest is extracted and started without a system install. A small Windows `.exe` you compile yourself runs through Wine when Wine is installed.

The Windows, macOS, and Android clients are real projects in this repository. Their refusal rule is tested here. The Windows desktop window and the macOS app build on those systems. The Android app builds with the Android SDK. This environment does not assemble those device packages.

## What will not run

### macOS and iOS on non-Apple computers

Linkspan will not run macOS or iOS software on non-Apple hardware. Apple licenses those systems for Apple hardware. Linkspan does not include an emulator, disk image, translation layer, or installer for them, and it does not describe how to build one. You can still add the file. The app keeps it and explains why it will not start.

### iOS packages

Linkspan will not install an IPA or a device `.app`, including on a Mac. It does not decrypt App Store packages, remove DRM, or re-sign apps. On a Mac it can open an iOS app that is already installed, and it can launch a Simulator build of an app you compiled with Xcode.

There is no iOS version of Linkspan.

### Windows

Wine is kept for Windows programs. It opens native host windows. It is not a Windows license, and it is not used for OpenHarmony. Linkspan does not ship a Windows image, product key, or activation workaround. A full Windows virtual machine needs an image and a license you already have.

### OpenHarmony

Harmony guests are OpenHarmony packages. Linkspan does not translate a HAP into an Android or Windows program. It does not ship a HarmonyOS image or a Huawei key. Unsigned packages install only in developer mode, through OpenHarmony's own installer. The OpenHarmony host manual is [docs/openharmony-host.md](docs/openharmony-host.md).

### Slim images

Images are not inside Linkspan. When a desktop client starts, it reads the client IP and chooses GitHub or a China mirror. A mirror that changes a file fails the SHA-256 check. There is no macOS or iOS image repository.

Published image releases:

- [olympium-linux](https://github.com/yezicheng2011/olympium-linux) v0.1.0: Alpine Linux 3.24.4 for x86_64 and aarch64. The x86_64 guest was booted with KVM. The aarch64 guest was booted with QEMU's software CPU on an x86_64 host, not on aarch64 hardware. Lima and WSL import were not tested.
- [olympium-wine](https://github.com/yezicheng2011/olympium-wine) v0.1.0: Wine 11.0 for 64-bit Windows programs on Linux x86_64. A 32-bit program failed, and there is no aarch64, macOS, or Windows-host build.

Not published, and not reported as working:

- [olympium-android](https://github.com/yezicheng2011/olympium-android)
- [olympium-openharmony](https://github.com/yezicheng2011/olympium-openharmony)

`share/runtime-lock.json` is compiled into the client. The macOS package already published as 0.1.0 does not contain these hashes, so it will not download the new images. This source is 0.1.1 for that follow-up package. Do not replace the 0.1.0 package or its assets.

The English manual prepared for https://olympium.net/manual/ is [site/manual/index.html](site/manual/index.html). Deploying it is described in [site/INTEGRATION.md](site/INTEGRATION.md). This repository does not change the live site. An experimental file that is both a Debian package and a zip archive is described in [docs/polyglot.md](docs/polyglot.md). It is not the Mac download.

### Google Play

Linkspan does not include Google Play, Play Services, or a Google sign-in. An Android program has to be an APK you already have, or a package already installed in your own Android runtime.

### Downloads

Linkspan does not download programs, system images, Linux distributions, Flatpak runtimes, or snaps.

## Clients

| Host | Where | What it is |
| --- | --- | --- |
| Linux | `linkspan` and `linkspan-linux` | Command line and a GTK 4 / libadwaita window. Light, dark, and follow-the-system. |
| Windows | `hosts/windows` | WPF window that follows the Windows appearance, plus a protocol library. |
| macOS | `hosts/macos` | SwiftUI source and a Swift package. Build from source. No signed release, disk image, or installer of Linkspan. |
| Android | `hosts/android` | Jetpack Compose app. Material 3 light and dark, including follow-the-system. |
| OpenHarmony | `hosts/harmony` | Stage-model application. System light and dark. Manual: [docs/openharmony-host.md](docs/openharmony-host.md). |

Build steps are in [docs/building.md](docs/building.md).

## Try it on Linux

Rust 1.85 or newer is required.

```bash
cargo build -p linkspan-cli -p hello-linux -p linkspan-linux
./target/debug/linkspan doctor
./target/debug/linkspan run --file ./target/debug/hello-linux
./target/debug/linkspan-linux
```

The sample prints `linkspan-guest-ok platform=linux format=elf`.

A Debian guest:

```bash
chmod +x examples/linux-deb/build.sh
examples/linux-deb/build.sh
./target/debug/linkspan run --file examples/linux-deb/out/hello-linkspan.deb
```

A Windows guest, when MinGW and Wine are installed:

```bash
x86_64-w64-mingw32-gcc -O2 -o /tmp/hello.exe examples/windows/hello.c
./target/debug/linkspan run --capture --file /tmp/hello.exe
```

The desktop window follows the system theme. Its menu can force light or dark. The choice is saved in the config file.

```bash
./target/debug/linkspan config set appearance dark
./target/debug/linkspan config set appearance system
```

`scripts/e2e-linux.sh` runs the Linux checks above.

## Command

```text
linkspan doctor
linkspan matrix
linkspan limits
linkspan add --file ./program.deb
linkspan add --android-package com.example.app --name Example
linkspan add --flatpak org.example.App
linkspan list
linkspan plan <id>
linkspan run <id> -- --your-arg
linkspan run --file ./program.exe
linkspan run --file ./entry.hap
linkspan add --openharmony-bundle com.example.app
linkspan runtime
linkspan fetch --guest openharmony
linkspan remove <id>
```

`--json` prints a single JSON object. A legal refusal exits with status 2. A missing engine exits with status 3.

Library files live under `LINKSPAN_HOME` when that variable is set. Otherwise they follow the XDG directories on Linux, `~/Library` on macOS, and `%APPDATA%\Linkspan` on Windows.

## How a launch is chosen

The Rust crate `linkspan-core` is the only place that decides. A Mac or iOS guest is refused before any command is built when the host is Android, Windows, or Linux. The desktop and phone clients repeat that check so a damaged compatibility file cannot get around it.

Launch plans are argument lists. Linkspan does not pass your path through a shell.

The checked-in document `share/compatibility.json` is generated by the engine. The other clients read that same document. Regenerate it with `linkspan share` and copy it back into the client asset folders if the policy changes.

More detail is in [docs/architecture.md](docs/architecture.md), [docs/legal-limits.md](docs/legal-limits.md), [docs/openharmony-host.md](docs/openharmony-host.md), and [docs/aeroandroid-comparison.md](docs/aeroandroid-comparison.md).

## What this is not

Linkspan is not a store, not a Windows license, and not a way to run Apple systems on other hardware. It is a new project. It is not a rename of any other product, and this repository does not publish it for you.
