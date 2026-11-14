# Olympium and AeroAndroid

AeroAndroid runs Android apps on a Mac by booting a real Android system image and showing each app in a Mac window. Olympium follows that idea for every guest it can run: a real system, not an API translation, with the host drawing the window. It is a separate program. It does not copy AeroAndroid's source, and it does not rename AeroAndroid.

The slim images are not in this repository. They are downloaded at runtime from `olympium-android`, `olympium-linux`, `olympium-openharmony`, and `olympium-wine`.

| AeroAndroid capability | Olympium |
| --- | --- |
| Real Android system, not a translation layer | Android guests use Waydroid or adb when you already have them, or the slim AOSP image after it is published. No Play services are bundled. |
| One host window per app, no emulator chrome | Launch plans ask for `host-window` and refuse to open a nested emulator frame when the runtime's `linkspan-surface` bridge is missing. Wine, which is kept for Windows, already opens native host windows. OpenHarmony on an OpenHarmony device uses the system window. |
| Runtime downloaded at install, SHA-256 checked | `olympium runtime` and `olympium fetch` download a published slim image, show progress, and reject a bad hash. Linux and Wine are published. Android and OpenHarmony are reported as not published, not invented. |
| GitHub or a China mirror | The client IP is read from a configurable HTTPS lookup. Country `CN` selects `engines.mirrors.china_prefix` (default `https://gh-proxy.com/`). Otherwise GitHub is used. If the lookup fails, GitHub is probed, which is AeroAndroid's fallback. Prefixes are configurable. The official site is https://olympium.net. |
| Light and dark appearance | Every host follows the system, or a forced light or dark choice. |
| Memory budget | Automatic budget from available memory, with a minimum and maximum. Manual mode is a setting. |
| Shared folders | Paths in `engines.experience.shared_folders` are passed to the runtime. They are not uploaded anywhere. |
| Settings | Appearance, mirror, graphics, camera, gamepad, memory, and quick boot are settings. The OpenHarmony page shows developer mode. |
| Install, launch, remove | The library add, plan, run, and remove commands are the same engine on every desktop. The OpenHarmony client installs with `bm` / `hdc install`. |
| Doctor | `olympium doctor` reports engines, the slim OpenHarmony image, and the slim Wine runtime. |
| Camera, gamepad, quick boot | Passed to the slim runtime as arguments. They do nothing until that runtime is published. |
| SIP phone, home-screen widgets, Metal compositor | Not reimplemented. AeroAndroid's phone stack and Mac widget host are part of that project. Olympium's window bridge is the `linkspan-surface` program shipped inside a slim image, not a second copy of AeroAndroid. |
| Optional microG, Magisk, ARM translation | Not included. The slim Android edition is AOSP-only. |
| macOS package | GitHub Actions can build an ad-hoc signed Mac `.pkg`. Certificates are not in the repository. Notarization runs only when the matching secrets exist. |
| iOS host | Not built. AeroAndroid does not ship an iOS host either. iOS remains a guest, and only on a Mac, for an installed app or a Simulator build you compiled. |
| Harmony / OpenHarmony | AeroAndroid does not run OpenHarmony. Olympium runs OpenHarmony packages on a real OpenHarmony system and refuses HarmonyOS commercial images. |

What can run today, in this repository, without a published image:

- Linux programs, scripts, gzip `.deb` packages, and AppImages on Linux.
- RPM, Flatpak, and snap on Linux when those tools are installed.
- A Windows `.exe` through Wine on Linux or macOS when Wine is installed. Wine is kept. It is not the OpenHarmony path.
- The sample programs under `examples/`.

What is refused:

- macOS and iOS software on Android, Windows, Linux, and OpenHarmony.
- IPA files everywhere, including on a Mac.
- OpenHarmony packages on Android.
- Android, Windows, and Linux packages on the OpenHarmony client.
- A runtime that reports HarmonyOS.
- An unsigned OpenHarmony package when developer mode is off.
- A slim image that has not been published. The client does not substitute a different system.
