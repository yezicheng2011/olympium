# Olympium integration

The dock entry runs `/usr/libexec/dev-desktop/olympium-launch`. That script starts `olympium-linux` when the image carries it and opens the Olympium page in Settings when it does not.

## What is in the image

The Olympium runtime is a separate program with its own repository. Its source is not in this tree, and this tree never writes to that repository. The image build copies in prebuilt programs when they are on the build machine:

- `olympium-linux` and `linkspan-linux`, the GTK 4 window
- `olympium` and `linkspan`, the command-line program

They were built without source changes from commit `517aaaa5409f227de42444cf16612d546c29251b` on `cursor/guest-runtime-site-9aa9`. The x86_64 copy was built on the x86_64 build machine. The arm64 copy was cross-built with `image/build-runtime-arm64.sh` against the arm64 image's own libraries. That commit pins Rust 1.85.0; both builds used the newer stable compiler on the build machine.

That repository's `LICENSE` and `NOTICE` are copied as they are to `/usr/share/doc/olympium-runtime/`, next to a `SOURCE` file with the commit and the checksums of the installed programs.

## What it does here

The window opens under this desktop and lists programs, engines, runtimes, compatibility, and limits. The shell shows the window as Olympium, the same name as the dock entry. The header inside the window still says Linkspan, because that text is in the runtime and this tree does not change it.

It is work in progress. No guest image is in the ISO; the runtime downloads a guest image only when asked to, from that image's own source. Wine, Flatpak, Android tools, and OpenHarmony tools are not installed, so the engines that need them report them missing. macOS and iOS software is kept in its library and is not started; that is the runtime's own policy and this image does not patch it. Some guest paths in that revision write their captures to a fixed directory used during its development. http and https links open Firefox, which this image installs from Mozilla's own package archive.

Pure software x86/ARM translation belongs in the Olympium tree. This desktop stays native to each ISO architecture and does not require KVM for its own display. QEMU TCG used to boot an arm64 ISO is not Apple Silicon support and is not the Olympium translator.
