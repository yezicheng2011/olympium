# Desktop applications

This is the install and interoperability matrix for `dev-install`. It records what ran on the build machine on November 15, 2026, around 2:15 AM ET. It is not a claim that every installer or every application works.

The three checksummed live images do not contain `dev-install`. Their hashes are unchanged. The next image build copies the binary into `/usr/bin/dev-install`. The host test is `desktop/test-install.sh`, against `/var/tmp/aaa-out/olympium` from commit `517aaaa5409f227de42444cf16612d546c29251b`.

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

These package files were stubs written for the test. A real `.deb`, `.rpm`, `.apk`, `.hap`, `.exe`, `.msi`, Flatpak, snap, AppImage, or `.dmg` has not been installed. Those formats are not claimed.

A native Linux program runs as the user. This does not add a sandbox, and it does not check an Apple or Microsoft signature. The checksum is the SHA-256 of the file at import, checked again at launch. The runtime's refusal to start macOS or iOS software is not weakened.

## Where an imported program shows up

| Surface | Mechanism | Tested |
| --- | --- | --- |
| Its own process | Launch execs the program. An ELF goes through `/usr/libexec/dev-desktop/olympium-run` when that file is executable, or through `OLYMPIUM_RUN` in the test. A script is exec'd directly, because `olympium-run` rejects a non-ELF. | The `true` copy exited 0 and the log said `native x86_64`. No imported program was drawn as a window in the compositor. The Lawful test app window in the earlier ISO boots was not opened by this installer. |
| Launchpad | A `.desktop` file whose `Exec` is the wrapper path and nothing else. The scanner stops at the first space, so the wrapper takes the arguments. The directory is `$OLYMPIUM_DESK_HOME/applications` when that variable is set, otherwise `~/.local/share/applications`. | The file was written. Launchpad was not opened in a running compositor. The compositor does compile with the user-application scan. |
| Dock | `$OLYMPIUM_DESK_HOME/pinned`, or `~/.local/share/olympium-desk/pinned`, lines of `name`, a tab, and an executable path. At most four. | The pin file was written. The dock was not drawn. |
| Open with | `--mime EXT` stores the extension. `dev-install open` of a file that is not itself a package, and whose extension matches, execs that wrapper. | A `.note` file was passed to the script. |
| Files between programs | `$OLYMPIUM_DESK_HOME/exchange`, or `~/.local/share/olympium-desk/exchange`. | A write and a read of `hello.txt` on the host. No guest runtime mounts this directory. |
| Clipboard | The compositor creates a Wayland data device. | A copy between two clients was not run. Guest runtimes were not given a clipboard bridge. |

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
