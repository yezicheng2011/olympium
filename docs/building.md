# Building

Rust 1.85 or newer builds the engine. The desktop clients need the tools named below. A local Mac build is unsigned. GitHub Actions is the only path that produces a `.pkg`, and it ad-hoc signs the app unless a certificate is present as a secret. It does not notarize unless the notary secrets are set. No certificate is stored in this repository.

## Linux

Install a C toolchain, pkg-config, GTK 4, and libadwaita. On Debian or Ubuntu:

```bash
sudo apt install build-essential pkg-config libgtk-4-dev libadwaita-1-dev adwaita-icon-theme
```

`dpkg-deb` is used for `.deb` packages that are not gzip. It comes with `dpkg`.

Icons for every host are generated from the Olympium mark. See [branding/README.md](../branding/README.md). Check them with:

```bash
python3 scripts/check-icons.py
```

```bash
cargo test --workspace
cargo build -p olympium-cli -p hello-linux -p olympium-linux
./target/debug/olympium doctor
./target/debug/olympium-linux
```

Optional, for the Windows sample guest:

```bash
sudo apt install mingw-w64 wine
```

## Windows

Install Rust, then from the repository root:

```bash
cargo build -p olympium-cli --release
```

The window needs the .NET 8 Windows Desktop workload:

```bash
dotnet test hosts\windows\Olympium.Protocol.Tests
dotnet build hosts\windows\Olympium.Windows -c Release
```

`Olympium.Protocol` and its tests also build on Linux with the .NET 8 SDK. The WPF window itself builds on Windows.

Put `olympium.exe` on `PATH` or set `LINKSPAN_CLI`.

WSL is the Linux engine on Windows. Olympium does not install a distribution. Wine is not required on Windows, because Windows programs run directly.

## macOS

The kit does not produce an application bundle:

```bash
cd hosts/macos
swift test
```

Open `hosts/macos/Olympium.xcodeproj` in Xcode and build the Debug configuration for your Mac. The project turns signing off. Do not archive it, do not notarize it, and do not wrap it in a dmg or pkg. Those steps are intentionally not in this repository.

The engine is still the Rust command:

```bash
cargo build -p olympium-cli --release
```

Lima is optional and is not downloaded for you. Xcode is required only for Simulator builds of apps you compiled.

## Android

The refusal check does not need the Android SDK:

```bash
javac -d /tmp/olympium-java \
  hosts/android/protocol/src/main/java/app/olympium/protocol/LaunchGate.java \
  hosts/android/protocol/src/test/java/app/olympium/protocol/LaunchGateTest.java
java -cp /tmp/olympium-java app.olympium.protocol.LaunchGateTest
```

The app needs JDK 17, Android SDK 35, and a Gradle wrapper or a local Gradle 8.11 or newer. From `hosts/android`:

```bash
gradle wrapper --gradle-version 8.11.1
./gradlew :app:assembleDebug
```

This repository does not generate the wrapper jar, because that binary is fetched when you run the command above. The debug APK is a local build. It is not published from here.

There is no iOS target to build.

## OpenHarmony

The client source is `hosts/harmony`. The SDK is not vendored. With `hvigorw` from the OpenHarmony SDK:

```bash
cd hosts/harmony
hvigorw assembleHap
```

Debug-install and first launch are in [openharmony-host.md](openharmony-host.md). The refusal check does not need the SDK:

```bash
node hosts/harmony/protocol/launch-gate.test.js
```

## macOS package on GitHub Actions

`.github/workflows/ci.yml` runs `scripts/package-macos.sh` on a macOS runner. The script builds `Olympium.app`, ad-hoc signs it with `codesign -s -`, and wraps it in a `.pkg`. If `APPLE_CERTIFICATE_P12`, `APPLE_SIGN_IDENTITY`, and `APPLE_INSTALLER_IDENTITY` are set as repository secrets, the script uses those identities instead. If the notary secrets are set, it submits the package. None of those values are written into the source tree.

The package does not contain a guest image.

## Slim images

`share/runtime-lock.json` is compiled into the client with `include_str!`. It names the four image repositories. The images are not built by this repository.

`olympium runtime` chooses GitHub or a China mirror from the client IP. `olympium fetch --guest linux` and `olympium fetch --guest windows` download the published Alpine and Wine archives and check SHA-256. Android and OpenHarmony stay unpublished. On Linux, `olympium run` of a Windows program uses the slim Wine binary when no system Wine is installed and the archive's `HOST_OS` says `linux`.

The published macOS package is 0.1.0 and does not contain this lock. `scripts/check-client-version.sh` refuses to package 0.1.0 again. `scripts/package-macos.sh` reads that version and, on macOS, builds `dist/Olympium-<version>.pkg`. GitHub Actions uploads that package as a workflow artifact. It does not publish a GitHub Release and it does not replace `Linkspan-0.1.0.pkg`. Publishing 0.1.1 is a separate follow-up after this source is merged.

## Experimental deb and zip polyglot

`scripts/test-polyglot-deb-zip.sh` builds one byte string and checks it with `dpkg-deb` and Python's zip reader. The same bytes are not an RPM, an MSI, a macOS package, an APK, or a HAP. Details and the limits are in [polyglot.md](polyglot.md). The page prepared for olympium.net is [../site/manual/index.html](../site/manual/index.html). It is not deployed by this repository.
