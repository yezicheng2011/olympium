# Windows programs by CPU

Wine is the distribution's package on every image. It is not rewritten or patched in this tree. Wine runs a Windows program's own instructions, so what each image can run depends on which halves of Wine are installed and on the CPU.

## What was wrong

- On the x86_64 image, a 32-bit Windows program did not open a window. Only `wine64` was installed. Wine printed `failed to load L"\\??\\C:\\windows\\syswow64\\ntdll.dll" error c0000135` to a log nobody sees and exited 0. `dev-install` did not look at which CPU a `.exe` was built for, so Launch was offered and then nothing appeared. This was reproduced on the build machine with `desk-win.c` built by `i686-w64-mingw32-gcc`.
- The same would happen to an x86 `.exe` on the arm64 image and to a 64-bit `.exe` on the i386 image, if Wine were installed there. Neither image has Wine.
- The i386 image builds wlroots without Xwayland, and `compositor.c` included the Xwayland header without a condition. The compositor from the previous commit could not be built for i386 at all.
- With no X display, Wine starts, cannot create its window, and the program dies. Nothing checked for that.

## What changed

- `dev-install` reads the machine field of the PE header. Launch is offered only when this system can run that CPU. Otherwise the review says why and nothing is started.
- `dev-install launch` refuses a Windows program when `DISPLAY` is not set, and says Xwayland is not running.
- `desktop/xwayland-compat.h` lets the compositor build against a wlroots with or without Xwayland. Without it the session starts with no `DISPLAY`.
- `image/install-wine.sh ROOT ARCH` installs the right packages for each image. `OLYMPIUM_WINE=1 ./image/repack-install.sh ARCH` calls it.
- `OLYMPIUM_WINE=1 ./image/build-iso-i386.sh` builds wlroots with Xwayland and installs Debian's `wine32`. Without that variable the i386 build is what it was.
- `repack-install.sh` also builds `desk-win32.exe` when `i686-w64-mingw32-gcc` is on the build machine.

| Image | Packages | 64-bit x86 `.exe` | 32-bit x86 `.exe` | ARM64 `.exe` |
| --- | --- | --- | --- | --- |
| amd64 | `wine`, `wine64`, `wine32:i386`, `xwayland` | Runs | Runs with `wine32`. Refused without it. | Refused |
| i386 | `wine`, `wine32`, `xwayland` | Refused | Runs | Refused |
| arm64 | `wine`, `wine64`, `xwayland` | Refused: no x86 translator | Refused: no x86 translator | Runs, by the package's description. Not tried. |

## What was run

`make -C desktop test-wine` on the x86_64 build machine, Ubuntu 24.04, Wine `9.0~repack-4build3` with `wine32:i386` added, three runs, all passing:

- The 32-bit `desk-win32.exe` was imported and launched, and its window mapped 0.40 to 0.51 s after the request, with the prefix in place.
- With `wine32` reported absent, the same file's review was `review`, the message named `wine32`, and import exited non-zero.
- The 64-bit sample was refused when the host was reported as `aarch64` and as `i686`. A copy with its machine field set to ARM64 was refused on x86_64.
- Launch with `DISPLAY` unset exited 3 and no window mapped.
- The compositor built with `-DDEV_DESK_NO_XWAYLAND` started a session with no `DISPLAY`.

With `wine32` installed, a new prefix is 1.3 GB before deduplication, and Wine built it in 21 s on the build machine. The squashfs cost of the 32-bit half was not measured.

## What was not run

An amd64 image was built and booted under QEMU emulation with `install-wine.sh` and `wine-prefix.sh` run in its root (`docs/startup-latency.md`). i386 and arm64 images were not rebuilt. Before that boot, `install-wine.sh` had not been run in a chroot; the amd64 package set is the one installed on the build machine, which is the same Ubuntu release. The i386 Xwayland build of wlroots and Debian's `wine32` were not built or run. Wine on arm64 was not run, and no ARM64 Windows program was built.

Running an x86 or x86_64 Windows program on arm64 needs an x86 translator under Wine, such as FEX or box64, or Wine's ARM64EC path. None is installed, none was tried, and this tree does not claim it.

## macOS

Nothing in this tree runs on macOS. The images are Linux, and Wine here is the Linux package. A crash of Wine on a Mac is in whatever starts Wine there, which is the Olympium runtime's own repository or a macOS Wine build, and it was not reproduced or changed here.
