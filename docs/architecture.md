# Architecture

Olympium is one engine and five host clients.

```text
olympium-core          launch policy, package readers, catalog
olympium               command line for Linux, Windows, and macOS
olympium-linux         GTK 4 window
hosts/windows          WPF window and a .NET protocol library
hosts/macos            SwiftUI source and OlympiumKit
hosts/android          Compose app and a Java refusal check
hosts/harmony          OpenHarmony stage-model app and a JavaScript refusal check
```

There is no `hosts/ios` directory. iOS is a guest system only. OpenHarmony is both a host and a guest. Commercial HarmonyOS is neither.

## Policy

`olympium-core` is the only writer of the compatibility document. Every host and guest pair has a level:

| Level | Meaning |
| --- | --- |
| ready | This repository can run it on that host. |
| engine | An adapter calls a tool you install. The tool is not bundled. |
| limited | Some cases run. The rest are refused, with a reason. |
| scaffold | A hook exists. Nothing runs until you set a command. |
| refused | Olympium will not run it. |

macOS and iOS guests are refused on Android, Windows, and Linux before a command line exists. On a Mac, disk images and packages use the normal macOS tools. IPA files are refused there too.

The Windows, macOS, Android, and OpenHarmony clients each contain `LaunchGate.mustRefuse`. It refuses again when the guest is macOS or iOS and the host is not macOS, when the guest is OpenHarmony and the host is Android, and when any guest other than OpenHarmony is launched on the OpenHarmony client, even if a compatibility file has been edited to say otherwise.

A desktop OpenHarmony launch talks to `hdc` and a host-window bridge shipped inside the slim image (`bin/linkspan-runtime`, `bin/linkspan-surface`). If the bridge is missing, Olympium refuses instead of opening a nested emulator window. On an OpenHarmony device the plan is `bm install` and `aa start`. Wine is still the Windows engine on Linux and macOS. Its plan does not mention OpenHarmony, and an OpenHarmony plan does not mention Wine.

## Engines

| Id | Hosts | What it calls |
| --- | --- | --- |
| native | all | The program itself, or `open` for a Mac app. |
| deb | Linux, and extract-then-run elsewhere | Built-in gzip `.deb` reader, or `dpkg-deb` for xz and zstd. |
| rpm | Linux | `rpm2cpio`, then a cpio reader that rejects paths outside the archive. |
| flatpak | Linux | `flatpak run` for an installed name. Files are not downloaded. |
| snap | Linux | `snap run` for an installed name. Packages are not installed. |
| wine | Linux, macOS | `wine` or `wine64`. MSI installers use `wine msiexec`. |
| proton | Linux, macOS | A Proton script you point at, for executables only. |
| wsl | Windows | `wsl.exe --exec` for a Linux program. |
| lima | macOS | `limactl shell`, or a command prefix you set. |
| android-runtime | Linux, Windows, macOS | `waydroid` or `adb`. |
| android-intent | Android | Android's installer and launcher. |
| apple-native | macOS | `open`, disk-image attach, and `installer` for the current user. |
| ios-mac | macOS | `open -b` for an installed app, or `xcrun simctl` for a Simulator build. |
| linux-hook, windows-hook | Android | A command prefix you configure. |
| openharmony-native | OpenHarmony | `bm` and `aa` on the device. |
| openharmony-runtime | Linux, Windows, macOS | `hdc` against a real OpenHarmony system, plus the host-window bridge from the slim image. |

None of those tools are vendored. A missing tool is an error with an install hint, not a successful launch.

## Packages

`.deb` and RPM payloads are extracted into the user's cache. Paths that escape the destination are rejected. Symbolic links are skipped. Setuid bits are not kept. The chosen program is marked executable inside the cache only. Olympium does not change the permissions of a file you pointed at.

Flatpak and snap names are checked so they cannot be flags or URLs. Android package names and bundle ids have to look like dotted ids. Arguments are passed as separate argv entries.

## Clients

The command speaks JSON with `--json`. The Windows client can call that command. The macOS client can too. The Android client does not embed the Rust binary in this version. It uses the same compatibility document and the Java refusal check, then Android's own installer for an APK.

Appearance is `system`, `light`, or `dark` on every client. The default follows the system.
