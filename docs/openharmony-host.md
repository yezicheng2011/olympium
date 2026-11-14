# OpenHarmony host manual

This is the instruction manual for the Olympium client that runs on OpenHarmony (the 鸿蒙 host, built from OpenHarmony). The source is in `hosts/harmony`. The client is English. Olympium's own source is Apache-2.0 only.

There is no iOS host. This manual does not describe one.

## What this client is

The OpenHarmony client is a normal OpenHarmony application. It runs on a phone, tablet, or 2-in-1 that is itself OpenHarmony. OpenHarmony packages (`.hap` files and OpenHarmony `.app` bundles) are installed with OpenHarmony's own installer. The window is the system's window. Olympium does not draw a second desktop inside the app.

Guest system images are not inside the application. A slim OpenHarmony, Android, Linux, or Wine runtime, when one is published, is downloaded by the desktop clients from a separate repository. This client does not embed those files.

## What this client will not do

- It will not run commercial HarmonyOS images, and it does not include Huawei signing keys.
- It will not translate a HAP into an Android APK, a Windows program, or a Linux program.
- It will not run Android, Windows, Linux, macOS, or iOS software on the OpenHarmony device. Those guests run on the Linux, Windows, or macOS clients, and only where that client has a real runtime. macOS and iOS guests still do not run on non-Apple hardware.
- It will not install an unsigned HAP when developer mode is off. It does not add a certificate, remove a signature, or skip the system verifier.
- It does not ship a product website, a Mac disk image, or an iOS build of Olympium.

## What you need

- An OpenHarmony device or emulator whose system identifies itself as OpenHarmony, not HarmonyOS.
- The OpenHarmony SDK that matches the project, currently the stage model and compatible SDK `5.0.0(12)`. DevEco Studio or the command-line SDK both work. This repository does not download the SDK.
- A debug signing certificate created on your computer by the SDK. Do not commit the certificate, the profile, or the passwords. `build-profile.json5` ships with an empty `signingConfigs` list for that reason.
- Developer mode on the device if you will install an unsigned or untrusted HAP. Signed packages are still checked by OpenHarmony.

## Build

From `hosts/harmony`, with the SDK's `hvigorw` on your path (DevEco installs it for you):

```bash
hvigorw assembleHap
```

The debug HAP is written under `entry/build`. This repository does not generate the hvigor wrapper binary, because that binary comes from the SDK. If `hvigorw` is missing, install the OpenHarmony command-line tools or open the `hosts/harmony` directory in DevEco Studio and build the `default` product.

The refusal rule can be checked without the SDK:

```bash
node hosts/harmony/protocol/launch-gate.test.js
```

That test is the same rule the desktop clients use: Apple guests do not run off a Mac, a HAP does not run on Android, and this OpenHarmony client does not run other guest systems.

## Sign and debug-install

1. In DevEco Studio, create a debug certificate and a debug profile for `app.olympium.openharmony`. Keep them outside the git repository.
2. Turn on developer mode on the device. The client reads `const.security.developermode.state` when it can. If the read fails, it says so and does not pretend developer mode is on.
3. Connect the device and install the client itself:

```bash
hdc install entry/build/default/outputs/default/entry-default-signed.hap
```

The exact file name follows the SDK. Use the HAP the build just produced. `hdc install` is OpenHarmony's installer. If the package has no signature block, this succeeds only in developer mode. If it is signed, OpenHarmony checks the signature. Olympium does not replace that check.

4. Start the client from the home screen, or:

```bash
hdc shell aa start -b app.olympium.openharmony -a EntryAbility
```

Do not copy a Huawei commercial signing key into the project. Do not disable signature verification.

## First launch

On first launch the window shows three facts:

- This device installs OpenHarmony software with the system installer.
- No guest image is bundled.
- Whether developer mode could be read.

The page background follows the system light and dark colors in `resources/base` and `resources/dark`.

The client asks for the Internet permission so a later lookup can choose GitHub or a China mirror. The OpenHarmony client does not need to download an image to run an OpenHarmony HAP, because the device is already the runtime. Desktop clients do that download. See [Runtimes and download sources](#runtimes-and-download-sources).

## Run an OpenHarmony guest

A guest on this host is an OpenHarmony package you already have the right to use.

1. Type `openharmony` as the guest. The button checks the refusal rule. Any other guest name is refused, with the reason.
2. Install the package from a computer, in developer mode if it is unsigned:

```bash
hdc install /path/to/entry.hap
```

An OpenHarmony `.app` bundle is a zip of HAP files plus `pack.info`. Install the HAP modules it contains with `hdc install`, or install the bundle with the `hdc` that belongs to your SDK. Do not rename a HAP to `.apk` and do not send it to an Android installer.

3. Open the installed bundle:

```bash
hdc shell aa start -b com.example.app -a EntryAbility
```

Replace the bundle name and ability with the values in the package's `module.json`. If you type a bundle name in the client, the window tells you the same `hdc` command. It does not invent a certificate to make an unsigned package look signed.

A package that contains both `AndroidManifest.xml` and `module.json` is rejected by the engine. Olympium will not guess which system it belongs to.

## Guests that are refused on this device

| Guest | On this OpenHarmony client |
| --- | --- |
| OpenHarmony HAP or app bundle | Runs, through the system installer. Unsigned packages need developer mode. |
| Android APK | Refused. This device is not an Android runtime, and the HAP path is not run in reverse as an APK either. |
| Windows program | Refused here. Wine remains available on Linux and macOS, and Windows runs Windows programs directly. |
| Linux program | Refused here. Use the Linux client, WSL on Windows, or Lima on macOS. |
| macOS or iOS | Refused. Apple licenses those systems for Apple hardware. |

The file can still be kept on a desktop. The refusal is the result, not a missing feature to fill with an emulator of a system this device cannot lawfully or practically host.

## Runtimes and download sources

Desktop clients download slim images when those images are published. The OpenHarmony client does not contain them. The four repositories are:

| Runtime | Repository |
| --- | --- |
| Android, slim AOSP, no Play | `https://github.com/yezicheng2011/olympium-android` |
| Linux root filesystem | `https://github.com/yezicheng2011/olympium-linux` |
| OpenHarmony standard system, slim | `https://github.com/yezicheng2011/olympium-openharmony` |
| Wine, slim | `https://github.com/yezicheng2011/olympium-wine` |

There is no macOS image repository and no iOS image repository.

When a desktop client looks for a download, it reads the client IP from `engines.mirrors.ip_lookup_url` (default `https://ipinfo.io/json`). A country code of `CN` selects the China mirror prefix (`engines.mirrors.china_prefix`, default `https://gh-proxy.com/`). Any other country uses GitHub directly. If the lookup does not return a country, the client probes `engines.mirrors.probe_url` (default `https://github.com`), the same way AeroAndroid picks a mirror when GitHub does not answer. Set `engines.mirrors.prefer` to `github` or `china` to skip the lookup.

Every downloaded file is checked with SHA-256 from `share/runtime-lock.json`. A mirror that changes the bytes is discarded, and the original repository URL is tried next. Linux and Wine have published archives. Android and OpenHarmony still say `published: false`, and the client reports that instead of inventing a file.

On this OpenHarmony device you do not need the slim OpenHarmony image. The device is the runtime. The manual still describes the lookup so the same rule is visible here.

## Settings the desktop engine also stores

These are changed with `olympium config set` on a desktop, and the OpenHarmony window follows the system appearance on its own:

| Key | Values |
| --- | --- |
| `appearance` | `system`, `light`, `dark` |
| `mirrors.prefer` | `auto`, `github`, `china` |
| `experience.graphics` | `auto`, `host`, `software` |
| `experience.camera` | `auto`, `off` |
| `experience.gamepad` | `auto`, `off` |
| `experience.memory.mode` | `auto`, `manual` |
| `experience.quick_boot` | `on`, `off` |

Shared folders are listed in `engines.experience.shared_folders` in the config file. The slim runtime receives them as `--share` arguments. They are not copied into a private cache first.

Memory in automatic mode uses about a quarter of the memory left after a 2 GiB reserve, and stays between `memory_min_mib` (2048) and `memory_max_mib` (8192). The minimum is kept even when the machine is under pressure.

## If something fails

| What you see | What it means |
| --- | --- |
| Developer mode could not be read | The system parameter API is missing or blocked. Use `hdc` and check developer mode in system settings. |
| Unsigned package refused | Developer mode is off, or the package has no signature block. Turn developer mode on or install a package the device already trusts. Do not strip the signature. |
| The runtime reports HarmonyOS | That is a commercial image. Olympium will not use it. |
| `hvigorw` is not found | Install the OpenHarmony SDK. The repository does not vendor it. |
| A download says the image is not published | The slim image repository has no release yet. The client will not substitute another system. |

## Where the rest of the product is documented

- [README](../README.md) for the product overview.
- [Building](building.md) for the desktop clients and the macOS package job.
- [Legal and technical limits](legal-limits.md) for the rules this manual follows.
- [Architecture](architecture.md) for the engine and the host-window path.
