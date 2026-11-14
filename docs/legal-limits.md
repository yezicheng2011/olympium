# Legal and technical limits

The text in the apps comes from `linkspan limits`. This page is the same set of rules.

## macOS and iOS on non-Apple computers

Linkspan will not run macOS or iOS software on non-Apple hardware. Apple licenses those systems for Apple hardware. Linkspan does not include an emulator, disk image, translation layer, or installer for them, and the source does not include a recipe for one.

A `.dmg`, `.pkg`, `.app`, Mach-O file, or `.ipa` can sit in the library on Linux, Windows, or Android. Launching it returns a refusal. The refusal is not a missing feature to be filled in later with an emulator.

## iOS packages

Linkspan will not install an IPA or a device app bundle, including on a Mac. It does not decrypt App Store packages, remove DRM, or re-sign applications.

On a Mac, two narrower cases are implemented:

- `open -b` for an iOS app the Mac already installed, which is Apple's own behavior on that Mac.
- `xcrun simctl` for a Simulator `.app` you compiled with Xcode. Linkspan does not download Xcode.

## No iOS host

There is no iOS client. iOS software is only a guest, and only where the rules above allow it.

## Windows

Wine is kept for Windows programs. It opens native host windows. It is not a Windows license, and it is not used for OpenHarmony. Linkspan does not ship a Windows image, product key, or activation workaround. A slim Wine runtime can be downloaded from its own repository. A full Windows virtual machine needs an image and a license you already have.

## OpenHarmony

Harmony guests are OpenHarmony `.hap` packages and OpenHarmony `.app` bundles. Linkspan installs them with OpenHarmony's own tools. It does not translate a HAP into another system. It does not include a HarmonyOS commercial image, a Huawei signing key, or an unofficial port.

An unsigned or unreadable package is installed only when the OpenHarmony runtime is in developer mode. A signature block is passed through unchanged. Linkspan does not decide that a signature is trusted. The runtime does.

Android cannot run these packages. The OpenHarmony client runs them on device and refuses every other guest. The manual is [openharmony-host.md](openharmony-host.md).

## Slim images

Linkspan does not contain guest system images. A desktop client reads the client IP, chooses GitHub or a configurable China mirror, and downloads a published slim image from `olympium-android`, `olympium-linux`, `olympium-openharmony`, or `olympium-wine`. The file is discarded if the SHA-256 does not match. There is no macOS or iOS image.

## Google Play

Linkspan does not include Google Play, Play Services, or a Google account. You bring an APK you have the right to use, or you name a package that is already installed.

## Downloads and engines

Linkspan does not download applications. It does download slim guest runtimes, when they are published, from the repositories named above. Wine, Proton, QEMU, Lima, Waydroid, WSL, OpenHarmony, GTK, and the system package tools stay under their own licenses. They are not relicensed as Apache-2.0, and Linkspan is not dual-licensed with them.

## Your programs

You are responsible for the rights to the programs you run.
