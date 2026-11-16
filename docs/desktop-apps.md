# Desktop applications

This is the install and interoperability matrix for `dev-install`. It is not a claim that every installer or every application works.

The three images `6258fda9…`, `2ce330f0…`, and `bef87685…` do not contain `dev-install`. A later x86_64 image does.

| Image | SHA-256 | What that boot showed |
| --- | --- | --- |
| `build/dev-live-amd64-install.iso`, the `.deb` boot | `c30c081c502058e0859d4d1c72d874e0ca061181b38982bc03a13f9e520edc2f` | Install for a real `.deb` with the checkbox off, then Desk Note and Desk Paste, a file under `exchange/`, and Launchpad entries. Maintainer scripts were not run. |
| `build/dev-live-amd64-install.iso`, rebuilt after that | `9c1b41e427bd891a6887a305de0a3cbfc8024a6f8786875ab1620c9e601c07a1` | Install for `desk-win.exe` with the checkbox off, then a Wine window titled Desk Win, Launchpad entries and dock pins for `desk-win.exe` and `desk-rpm.rpm`, and `exchange/` files `Desk Win ready` and `desk-rpm-ready`. |

`9c1b41e4…` is the image that contains Ubuntu Wine and Xwayland. Its boot did not repeat the `.deb` exercise. i386 and arm64 were not rebuilt for this pass. The host test is `desktop/test-install.sh`, against `/var/tmp/aaa-out/olympium` from commit `517aaaa5409f227de42444cf16612d546c29251b`.

Pictures from that host, under Xvfb, not from a booted ISO: `olympium-install-linux.png` (a Linux program, consent still required, nothing run) and `olympium-install-deb.png` (a `.deb` stub, Import stays off).

## What a double-click does

Files used to start an executable immediately. It now runs `dev-install open` and leaves the file unrun. If `dev-install` is missing, Files says so and does not start the file. A directory is still opened as a directory.

`open` prints the review. With a display, and without `--no-ui`, it also shows the Install window. Import is disabled until the checkbox is on, and the checkbox itself is disabled unless the decision is `import` or `keep`. Launch stays disabled until a launchable import has been recorded. The progress text is "No download. This desktop does not fetch guest images." There is no download.

## Formats

| File | Decision | What happened in the test |
| --- | --- | --- |
| Linux ELF whose plan is `process`, `surface.mode` `native`, `hides_runtime_chrome` true. The sample was a copy of `/usr/bin/true`. | `import` | Inspect wrote no library row and reported `ran: false`. A second inspect was a cache hit. Import without `--consent` exited non-zero and wrote nothing. Import with `--consent` wrote a checksum, a desktop file, a wrapper, and a dock pin, and called `olympium add`. Launch exited 0 through `olympium-run`, which logged `native x86_64`. After one extra byte, launch exited 4 and did not start the file. |
| Shell script with a shebang, imported with `--mime note`. | `import` | `open` of the script did not run it. `open` of a `.note` file then ran the wrapper and the script recorded that path. |
| Same ELF with `OLYMPIUM_BIN` pointing at a missing binary. | `import` | Local ELF recognition. Import recorded `launch=native`. This is the i386 case, where the image has no runtime binary. |
| Mach-O header (`CF FA ED FE`). | `keep` | `add` ran. The row is `launch=refused`. No desktop file. Launch exited 3. The runtime's refusal text is unchanged. |
| Zip named `.ipa` with a `Payload/` entry. | `refuse` | Import failed. `olympium list` did not change. The message is "iOS is not part of this system." Without the runtime binary, the same zip is not labeled iOS and is still not launchable. |
| Zip named `.apk` with `AndroidManifest.xml`. | `review` | Not launchable. Import exited non-zero. No library row. No image was downloaded. |
| Zip named `.hap` with `module.json`. | `review` | Same. The runtime's sentence names a fetch command. This desktop did not run it. |
| Ar archive starting with `!<arch>` and `debian-binary`. | `review` | The plan type was `extract_then_run`. The archive was not extracted and not run. |
| RPM magic `ed ab ee db`. | `review` | Not installed. `rpm2cpio` was not run. |
| Bytes starting with `MZ`. | `review` | Not installed. Wine was not started. |

The rows above are stubs, and `desktop/test-install.sh` still requires them to stay uninstalled. Separate from that test:

| File | What ran |
| --- | --- |
| Real `.deb` `desk-note`, built in this repo. | On `c30c081c…`, consent extracted it with `dpkg-deb -x`. No maintainer script. Open of `hello.dnote` mapped Desk Note. A second window read the exchange file. |
| Real `.rpm` `desk-rpm`. | On `9c1b41e4…`, consent extracted it with `rpm2cpio` and `cpio`. The script wrote `desk-rpm-ready`. Launchpad and the dock showed it. |
| Real PE32+ `desk-win.exe`, built with mingw. | On `9c1b41e4…`, consent did not start it. Launch ran `/usr/bin/wine` with that path. Wine is Ubuntu `9.0~repack-4build3`. The window mapped at 43.3s of compositor time. The process timer to `WM_CREATE` was 26 ms, which is not the wait for the window. `docs/startup-latency.md` splits the 43.3 s. |
| A Mach-O file on its own. | Still `keep`. The pinned plan on Linux is `apple-platform-on-non-apple-hardware`. No window was opened. |
| Mac `.dmg`, `.pkg`, and `.app`, written by `desktop/mac-fixtures.py`. | `mac`. On the build machine, consent unpacked the application in a confined child and put it in Applications with a Launchpad entry and its icon. No installer script ran. With Darling built on that machine, a command-line Mach-O program installed this way was started and ran. `docs/mac-apps.md` has the list. |
| `.apk`, `.hap`, `.msi`, Flatpak, snap, AppImage. | Not installed. Not claimed. |

A native Linux program runs as the user. Running it is not sandboxed, and no Apple or Microsoft signature is checked.

Unpacking is another matter. A package is whatever its author made it, and `dpkg-deb`, `tar`, `rpm2cpio`, and `cpio` run as the session's user, which on the live image is root. They now run in a confined child: with Landlock they can read the package and write only under the folder they unpack into, and on a kernel without Landlock they run as the user `nobody`. Afterwards set-user-ID and set-group-ID bits are cleared, devices and pipes are removed, and the files are given to the user who installed them. `docs/mac-apps.md` describes the confinement; it is the same code.

`make -C desktop test-packages` on the build machine, with a stand-in for the `olympium` command that answers `classify` for these two formats:

- The sample `.deb` installed, and its program was byte for byte the one in the package. The sample `.rpm` installed and its program ran.
- A `.deb` with a set-user-ID and set-group-ID program and a sticky world-writable folder installed with those bits cleared.
- A `.deb` with a device node in it was not installed, and no device was left behind.
- A `.deb` with a link pointing outside, a file to be written through it, a path of `../`, and an absolute path was not installed, and nothing was written outside.
- With the confinement forced to `nobody`, the sample `.deb` installed the same and ended up owned by root.

Before this, the same packages were unpacked by root with no limit, and a set-user-ID program in a package stayed set-user-ID. The checksum is the SHA-256 of the file at import, checked again at launch. The runtime's refusal to start macOS or iOS software is not weakened.

## Where an imported program shows up

| Surface | Mechanism | Tested |
| --- | --- | --- |
| Its own process | Launch execs the program. An ELF goes through `/usr/libexec/dev-desktop/olympium-run` when that file is executable, or through `OLYMPIUM_RUN` in the test. A script is exec'd directly, because `olympium-run` rejects a non-ELF. | The `true` copy exited 0 and the log said `native x86_64`. No imported program was drawn as a window in the compositor. The Lawful test app window in the earlier ISO boots was not opened by this installer. |
| Launchpad | A `.desktop` file whose `Exec` is the wrapper path, with an `Icon` line when the program has a picture. Launchpad runs the whole `Exec` line, so an entry written by something else keeps its arguments. The directory is `$OLYMPIUM_DESK_HOME/applications` when that variable is set, otherwise `~/.local/share/applications`, and the folders under it are read too. | On the build machine, with the headless backend: Launchpad listed and started a Windows shortcut, and listed three Mac applications with their icon files. On the boot of `9c1b41e4…` it listed `desk-win.exe` and `desk-rpm.rpm`. |
| Dock | `$OLYMPIUM_DESK_HOME/pinned`, or `~/.local/share/olympium-desk/pinned`, lines of `name`, a tab, and an executable path. At most four. A program is pinned once, however often it is imported. A Mac application is not pinned. | The pin file was written. On the boot of `9c1b41e4…` the dock had a pin for each imported program. |
| Open with | `--mime EXT` stores the extension. `dev-install open` of a file that is not itself a package, and whose extension matches, execs that wrapper. | A `.note` file was passed to the script. |
| Files between programs | `$OLYMPIUM_DESK_HOME/exchange`, or `~/.local/share/olympium-desk/exchange`. It is drive `S:` for a Windows program. | A write and a read of `hello.txt` on the host. Under Wine on the build machine, a file went each way through `S:`. See `docs/interoperation.md`. |
| Clipboard | The compositor creates a Wayland data device, and Xwayland's window manager carries the selection between X programs, which is what a Wine window is, and Wayland programs. | `make -C desktop test-wine` on the build machine, four runs: Desk Win copied `Desk Win ready` and the GTK Wayland program Desk Paste read it; Desk Note copied `Desk Note ready` and `desk-win.exe --paste` read it as `CF_TEXT`. Text only. Not run on a booted ISO. |

Two limits of that clipboard path. A Windows program that copies before its window is in front is ignored: wlroots does not let an X program that is not focused replace the clipboard. A copy made in the front window is carried. And a Wayland program can only set the clipboard with a serial from an input event, so a program that copies before it has ever had focus is refused.

Before this pass the seat only recorded which window was in front when a keyboard was attached. With no keyboard, no client was ever offered the clipboard. Focus is now recorded either way.

There is no Android runtime in this image, so there is no Android clipboard to connect. Images, files, and rich text on the clipboard were not tested.

## Time

`dev-install timings` on this host, the same run as the passing test:

```
cold_runtime_ms 2
warm_runtime_ms 1
cold_exec_ms 1
warm_exec_ms 0
```

`cold_runtime_ms` is `olympium --json run --file /usr/bin/true --capture`. The second call was 1 ms. That gap is not a warm-start cache in the pinned runtime, and this desktop does not pretend one exists. Direct exec of `/usr/bin/true` was 1 ms, then 0 ms.

The desktop cache stores classify and plan text under the desk directory, keyed by path, size, and mtime. On this host a first inspect of `/usr/bin/true` took 8.3 ms and the next took 5.3 ms. A `.deb` stub took 8.2 ms, then 5.2 ms. Page cache was not dropped.
