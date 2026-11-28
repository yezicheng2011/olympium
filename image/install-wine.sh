#!/bin/bash
# Copyright 2026 Daniel Ye
# Licensed under the Apache License, Version 2.0. See LICENSE.
#
# Installs the distribution's own Wine and Xwayland into an image root. Wine
# is not rewritten or patched here. What differs per image is which halves of
# Wine are installed, because Wine runs a Windows program's own instructions
# and cannot run one built for a CPU the machine does not have.
#
#   amd64   wine64 and wine32:i386. 64-bit and 32-bit x86 Windows programs.
#           Without wine32 a 32-bit program opens no window and Wine exits 0.
#   i386    wine32. 32-bit x86 Windows programs only.
#   arm64   wine64 for arm64. ARM64 Windows programs only. No x86 translator
#           is installed, so an x86 or x86_64 .exe is refused by dev-install.
#
# Run with /proc mounted in the root. It uses the image's own package
# archive and downloads nothing else.
set -euo pipefail
ROOT=${1:?root file system}
ARCH=${2:?amd64|i386|arm64}
in_root() {
	if [ "$ROOT" = / ]; then
		env DEBIAN_FRONTEND=noninteractive "$@"
	else
		chroot "$ROOT" /usr/bin/env DEBIAN_FRONTEND=noninteractive "$@"
	fi
}
case "$ARCH" in
amd64)
	in_root dpkg --add-architecture i386
	in_root apt-get update
	in_root apt-get install -y --no-install-recommends wine wine64 wine32:i386 xwayland
	;;
i386)
	in_root apt-get update
	in_root apt-get install -y --no-install-recommends wine wine32 xwayland
	;;
arm64)
	in_root apt-get update
	in_root apt-get install -y --no-install-recommends wine wine64 xwayland
	;;
*)
	echo "install-wine: unknown architecture $ARCH" >&2
	exit 2
	;;
esac
in_root dpkg-query -W -f '${Package}:${Architecture} ${Version}\n' wine wine64 wine32 xwayland 2>/dev/null || true
