# Experimental deb and zip polyglot

This is a parser test, not an official Olympium download. The macOS package remains [Linkspan-0.1.0.pkg](https://github.com/yezicheng2011/olympium/releases/download/v0.1.0/Linkspan-0.1.0.pkg).

`scripts/build-polyglot-deb-zip.sh` writes one byte string. Copying it to a `.deb` name or a `.zip` name does not change those bytes. `scripts/test-polyglot-deb-zip.sh` checks the copies.

## What was demonstrated

A gzip Debian package produced by `dpkg-deb -Zgzip`, with a zip archive appended:

| Check | Result |
| --- | --- |
| `.deb` and `.zip` copies | Byte-identical |
| `dpkg-deb -I` and `dpkg-deb -x` | Read the package |
| `dpkg -i` then run, then `dpkg -r` | The recorded install used `/usr/bin/linkspan-polyglot-probe` and printed `linkspan-polyglot-probe-ok`. The script now installs `/usr/bin/olympium-polyglot-probe` and prints `olympium-polyglot-probe-ok`. |
| Python `zipfile` | Listed and extracted the zip members from the same bytes |
| `unzip -p` | Printed `README.txt`, then exited 1 because of the warning `extra bytes at beginning or within zipfile` |
| `file(1)` | Reports only `Debian binary package` |
| GNU `ar t` | Rejects the file as a malformed archive |
| `rpm -qp` | Rejects it: not an rpm package |
| `msiinfo tables` | Cannot open it |

The same construction with `target/release/linkspan` (version 0.1.1, before the command was renamed) produced a 2487905-byte file, SHA-256 `e0336a1fbf6c7ae2967144989fc8a68f7a7f818094f756ed68fbfdc8d77161de`. `dpkg -i` installed it at `/usr/lib/linkspan-polyglot/linkspan` and `linkspan --version` printed `linkspan 0.1.1`. That file was not rebuilt, so that checksum still belongs to the `linkspan` binary.

A later file, `olympium-0.1.1-deb-zip.bin`, is 2531540 bytes, SHA-256 `423a52863955a008fb8333ffb701e4a5e07b8f731ff35a394c0c4740693447b6`. It contains `target/release/olympium`. `dpkg-deb` read it. `dpkg -i` installed `/usr/lib/olympium-polyglot/olympium`, that command printed `olympium 0.1.1`, and `dpkg -r olympium-polyglot` removed it. Python's zip reader extracted the same `olympium` bytes. Info-ZIP printed the extra-bytes warning and exited 1. The copies named `.deb` and `.zip` were the same bytes. It is a Linux amd64 ELF. It is not a macOS package, RPM, MSI, APK, or HAP. It is not signed. It is not the official download.

No signature was added. A whole-file signature, Authenticode, APK v2/v3, or a Debian signature over the exact archive bytes would not survive extra bytes. This does not remove Gatekeeper, SmartScreen, or any other warning.

## What was not combined

These formats put a different magic number at byte 0, so only one of them can be the start of the file:

| Format | Magic at byte 0 | In this polyglot |
| --- | --- | --- |
| Debian `.deb` | `!<arch>\n` | Yes, this is the start |
| RPM | ed ab ee db | No. `rpm` rejects the file |
| Windows Installer `.msi` | OLE `D0 CF 11 E0` | No. `msiinfo` cannot open this file |
| Windows `.exe` | `MZ` | No |
| macOS `.pkg` (xar) | `xar!` | Not built and not tested with `pkgutil` |
| AppImage | ELF | Not built |
| Android APK and OpenHarmony HAP | ZIP, and signing covers offsets from the start | Not built. A prefix or suffix breaks the signed layout |

Separate parser trials, not shipped as downloads:

- An empty MSI database still opened in `msiinfo` after a zip was appended. It installs nothing. It was not turned into a Windows installer.
- An unsigned 64-bit PE hello program with a zip appended printed `pe-polyglot-ok` under Wine, and Python read the zip. `file(1)` then called the file a zip. It is not an installer and it is not published. Authenticode was not used.

A UDIF `.dmg` wants its `koly` block in the last 512 bytes. A zip wants its end-of-central-directory record at the end. Both were not demonstrated together, because this host has no `hdiutil`.
