#!/bin/bash
# Copyright 2026 Development Desktop contributors
# License: daniel.ye Source License, Version 1.0. See LICENSE.
#
# Builds the Wine prefix once, while the image is built, so the first Windows
# program on a booted system does not wait for Wine to build it. Run with the
# root file system as the argument, after Wine is installed in it and with
# /proc mounted there.
#
# The prefix is about 690 MB of files that are byte-for-byte the DLLs already
# in /usr/lib/x86_64-linux-gnu/wine. mksquashfs stores identical files once,
# so both copies below add well under 1 MB to the ISO. They are real files,
# not links into /usr/lib: a Windows installer that overwrites a system DLL
# must change the prefix and never the Wine package.
#
#   /usr/lib/olympium/wine-prefix   the copy dev-install seeds a prefix from
#   /var/lib/olympium/desk/wine     the live session's own prefix, already in
#                                   place, so a live boot copies nothing
#
# Nothing here downloads anything. mscoree and mshtml are disabled so Wine
# does not ask for Mono or Gecko.
set -euo pipefail
ROOT=${1:?root file system}
if [ ! -x "$ROOT/usr/bin/wineboot" ]; then
	echo "wine-prefix: no Wine in $ROOT; no prefix was built"
	exit 0
fi
TPL=/usr/lib/olympium/wine-prefix
LIVE=/var/lib/olympium/desk/wine
rm -rf "$ROOT$TPL" "$ROOT$LIVE"
mkdir -p "$ROOT/usr/lib/olympium" "$ROOT/var/lib/olympium/desk"
run() {
	if [ "$ROOT" = / ]; then
		env "$@"
	else
		chroot "$ROOT" /usr/bin/env "$@"
	fi
}
# TMPDIR is taken out: with it set, even empty, Ubuntu's Wine 9.0 aborts or
# segfaults before it builds anything, and a build machine often exports it.
run -u DISPLAY -u WAYLAND_DISPLAY -u TMPDIR HOME=/root WINEPREFIX="$TPL" WINEDEBUG=-all \
	WINEDLLOVERRIDES="mscoree,mshtml=" /usr/bin/wineboot --init >/dev/null 2>&1
run -u TMPDIR HOME=/root WINEPREFIX="$TPL" /usr/bin/wineserver -w
if [ ! -s "$ROOT$TPL/system.reg" ]; then
	echo "wine-prefix: wineboot did not produce a registry; no prefix was installed" >&2
	rm -rf "$ROOT$TPL"
	exit 1
fi
cp -a "$ROOT$TPL" "$ROOT$LIVE"
du -sh "$ROOT$TPL" | awk '{ print "wine-prefix: built, " $1 " before deduplication" }'
