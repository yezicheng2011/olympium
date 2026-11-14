# Linkspan for macOS

This is the macOS client source. Build it on a Mac. A local Xcode build stays unsigned. The only packaging path is GitHub Actions, which can ad-hoc sign a `.pkg`. It does not notarize unless the notary secrets are set on the runner, and it never stores a certificate in this repository.

The engine is the Rust `linkspan` command. The Swift package `LinkspanKit` holds the refusal rule and the compatibility decoder. The SwiftUI app is the window.

## Kit

From `hosts/macos`, with a Swift toolchain:

```bash
swift test
```

That builds and tests the kit. It does not produce an application bundle.

## App

Open `Linkspan.xcodeproj` in Xcode and build the Debug configuration for your Mac. Signing is turned off in the project file. Do not notarize a local build. `scripts/package-macos.sh` is the GitHub Actions path that produces a `.pkg` and ad-hoc signs the app inside it.

The app follows the system appearance, and Settings can force light or dark.

Mac apps, disk images, and installer packages are opened with macOS. An iOS app that is already installed can be opened. A Simulator build you compiled can be launched when Xcode is installed. IPA files are recognized and refused. Linkspan does not decrypt or re-sign them.
