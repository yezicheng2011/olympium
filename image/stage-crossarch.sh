#!/bin/bash
# Copyright 2026 Development Desktop contributors
# SPDX-License-Identifier: Apache-2.0 — Licensed under the Apache License, Version 2.0. See LICENSE.
#
# Installs universal cross-architecture execution support into a rootfs so
# x86 (32-bit), x86_64 (64-bit), ARM 32-bit (armhf), and ARM 64-bit (arm64)
# Linux binaries run seamlessly on ANY host CPU via native compat or
# qemu-user-static + foreign dynamic linker sysroots + binfmt_misc.
set -euo pipefail

ROOT=${1:?target rootfs directory}
ARCH=${2:?amd64|i386|arm64|armhf}
SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)

in_root() {
	if [ "$ROOT" = / ]; then
		env DEBIAN_FRONTEND=noninteractive "$@"
	else
		chroot "$ROOT" /usr/bin/env DEBIAN_FRONTEND=noninteractive "$@"
	fi
}

# 1. Install qemu-user-static (provides qemu-aarch64-static, qemu-arm-static,
#    qemu-x86_64-static, qemu-i386-static) and systemd-binfmt support.
in_root apt-get update
in_root apt-get install -y --no-install-recommends qemu-user-static binfmt-support || \
	in_root apt-get install -y --no-install-recommends qemu-user-static

# 2. Install foreign-architecture libc6/libstdc++6 runtime libraries so
#    dynamically linked cross-arch software runs cleanly (not just static ELF).
install -d "$ROOT/etc/qemu-binfmt" "$ROOT/etc/binfmt.d"

case "$ARCH" in
amd64)
	in_root dpkg --add-architecture i386
	# On Ubuntu amd64, i386 lives on archive.ubuntu.com; arm64/armhf cross-libc
	# packages are available directly in universe as libc6-arm64-cross / libc6-armhf-cross!
	in_root apt-get update
	in_root apt-get install -y --no-install-recommends \
		libc6:i386 libgcc-s1:i386 libstdc++6:i386 \
		libc6-arm64-cross libstdc++6-arm64-cross \
		libc6-armhf-cross libstdc++6-armhf-cross || true
	[ -d "$ROOT/usr/aarch64-linux-gnu" ] && ln -sfn /usr/aarch64-linux-gnu "$ROOT/etc/qemu-binfmt/aarch64"
	[ -d "$ROOT/usr/arm-linux-gnueabihf" ] && ln -sfn /usr/arm-linux-gnueabihf "$ROOT/etc/qemu-binfmt/arm"
	# Filter out the host's own architecture from binfmt rules so native ELF never routes through QEMU.
	grep -v -E '^:olympium-(x86_64|i386):' "$SCRIPT_DIR/olympium-crossarch.binfmt.conf" \
		> "$ROOT/etc/binfmt.d/olympium-crossarch.conf"
	;;
arm64)
	in_root dpkg --add-architecture armhf
	in_root apt-get update
	in_root apt-get install -y --no-install-recommends \
		libc6:armhf libgcc-s1:armhf libstdc++6:armhf \
		libc6-amd64-cross libstdc++6-amd64-cross \
		libc6-i386-cross libstdc++6-i386-cross || true
	[ -d "$ROOT/usr/x86_64-linux-gnu" ] && ln -sfn /usr/x86_64-linux-gnu "$ROOT/etc/qemu-binfmt/x86_64"
	[ -d "$ROOT/usr/i686-linux-gnu" ] && ln -sfn /usr/i686-linux-gnu "$ROOT/etc/qemu-binfmt/i386"
	grep -v -E '^:olympium-(aarch64|arm):' "$SCRIPT_DIR/olympium-crossarch.binfmt.conf" \
		> "$ROOT/etc/binfmt.d/olympium-crossarch.conf"
	;;
i386)
	in_root apt-get install -y --no-install-recommends \
		libc6-amd64-cross libc6-arm64-cross libc6-armhf-cross || true
	[ -d "$ROOT/usr/x86_64-linux-gnu" ] && ln -sfn /usr/x86_64-linux-gnu "$ROOT/etc/qemu-binfmt/x86_64"
	[ -d "$ROOT/usr/aarch64-linux-gnu" ] && ln -sfn /usr/aarch64-linux-gnu "$ROOT/etc/qemu-binfmt/aarch64"
	[ -d "$ROOT/usr/arm-linux-gnueabihf" ] && ln -sfn /usr/arm-linux-gnueabihf "$ROOT/etc/qemu-binfmt/arm"
	grep -v -E '^:olympium-i386:' "$SCRIPT_DIR/olympium-crossarch.binfmt.conf" \
		> "$ROOT/etc/binfmt.d/olympium-crossarch.conf"
	;;
armhf)
	in_root apt-get install -y --no-install-recommends \
		libc6-arm64-cross libc6-amd64-cross libc6-i386-cross || true
	[ -d "$ROOT/usr/aarch64-linux-gnu" ] && ln -sfn /usr/aarch64-linux-gnu "$ROOT/etc/qemu-binfmt/aarch64"
	[ -d "$ROOT/usr/x86_64-linux-gnu" ] && ln -sfn /usr/x86_64-linux-gnu "$ROOT/etc/qemu-binfmt/x86_64"
	[ -d "$ROOT/usr/i686-linux-gnu" ] && ln -sfn /usr/i686-linux-gnu "$ROOT/etc/qemu-binfmt/i386"
	grep -v -E '^:olympium-arm:' "$SCRIPT_DIR/olympium-crossarch.binfmt.conf" \
		> "$ROOT/etc/binfmt.d/olympium-crossarch.conf"
	;;
esac

chmod 644 "$ROOT/etc/binfmt.d/olympium-crossarch.conf"
echo "stage-crossarch: enabled x86/x86_64/arm32/arm64 cross-architecture execution for $ARCH."
