# Runtime contract for the desktop

The Olympium runtime and this desktop are separate trees. Both were asked for one desktop around installed programs. This desktop implements its side against the command that is already built from commit `517aaaa5409f227de42444cf16612d546c29251b`. It does not vendor a newer binary, and it does not treat that commit as if it already had a warm-start cache, a clipboard bridge, a per-window guest, or a unified installer.

The i386 image has no `olympium` binary. `dev-install` then classifies only a Linux ELF or a shebang script, and it says the runtime is absent.

## Commands the desktop calls

`OLYMPIUM_BIN` if that path is executable, otherwise `/usr/bin/olympium`. There is no fallback to a build directory.

| Call | When |
| --- | --- |
| `olympium --json classify FILE` | Every inspect, unless the classify/plan cache already has this path, size, and mtime. |
| `olympium --json plan --file FILE --name NAME` | Same inspect, after classify reports `ok`. |
| `olympium --json add --file FILE --name NAME` | Only after the user passes `--consent` or presses Import, and only when the desktop's own decision is `import` or `keep`. |
| `olympium --json run --file /usr/bin/true --name True --capture` | Only `dev-install timings`, as a measurement. Import and Launch do not call `run`. |

`fetch`, `guest`, `qmp`, and `share` are not called. A plan whose text tells the user to run `linkspan runtime fetch` is shown and not followed.

## Fields that are read

The envelope is the current one: `ok`, `command`, `host`, `data`, `error`. Anything else is ignored. In particular the desktop does not read a `warmup` field. A future runtime can add one; this desktop will not call it until this file changes and a test measures it.

From classify `data`: `guest`, `kind`, `detail`.

From plan `data`: `summary`, `launch.type`, `launch.program`, `surface.mode`, `surface.hides_runtime_chrome`.

From a failed envelope: `error.message`.

From add `data`: `id`.

`share` on this commit still lists an `ios` guest. The desktop refuses `guest` `ios` anyway. iOS is not part of this system.

## When a file may be launched

All of these have to be true. If any one is missing, Launch stays off.

- `guest` is `linux`
- `kind` is `linux_executable` or `linux_script`
- `launch.type` is exactly `process`
- `launch.program` exists and is executable
- `surface.mode` is `native`
- `surface.hides_runtime_chrome` is true

`extract_then_run` is not launched. A `surface.mode` of `desktop`, or `hides_runtime_chrome` false, is not launched. That is how a separate guest desktop stays hidden: this desktop does not start it. No guest image was downloaded, so that refusal was not exercised against a real guest.

A `macos` file can be kept with `add`. The library row is `launch=refused`. There is no desktop file and no dock pin. `run` is not called. The runtime's own refusal (`apple-platform-on-non-apple-hardware`) is unchanged.

An `ios` file is not passed to `add` or `run`.

## Mac files do not go to the runtime

A `.dmg`, a `.pkg`, and an `.app` folder are recognized by the desktop before any of the calls above, and none of those calls is made for them. `classify`, `plan`, `add`, and `run` never see them. The desktop unpacks the application itself and starts it only through Darling when Darling is installed; `docs/mac-apps.md` describes that. The runtime's refusal to start macOS software is not touched and not worked around: nothing asks the runtime to start one.

A Mach-O file on its own is still sent to `classify` and `plan` and can still be kept with `add`, as the section above says.

## What a newer runtime must keep

A drop-in `olympium` can add JSON keys. It must not stop returning the fields above, and it must not start a macOS or iOS program on Linux. A native Linux plan must still say `launch.type` `process`, `surface.mode` `native`, and `hides_runtime_chrome` true, or the desktop will stop offering Launch.

This repository will not replace the pinned binary until that binary is built from the runtime tree and the image checksums are regenerated on purpose.
