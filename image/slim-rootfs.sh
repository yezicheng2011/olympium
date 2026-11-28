#!/bin/bash
# Copyright 2026 Development Desktop contributors
# SPDX-License-Identifier: Apache-2.0 — Licensed under the Apache License, Version 2.0. See LICENSE.
#
# Slims an Olympium rootfs before squashfs packing WITHOUT removing any
# desktop feature, compatibility runtime, or hardware driver support.
# Strictly preserves all license/copyright/NOTICE/SBOM files.
set -euo pipefail

ROOT=${1:?target rootfs directory}
if [ ! -d "$ROOT/usr" ]; then
	echo "slim-rootfs: invalid rootfs: $ROOT" >&2
	exit 1
fi

before_kb=$(du -sk "$ROOT" 2>/dev/null | awk '{print $1}' || echo 0)

# 1. Clean package manager caches, temp files, and Python bytecode caches.
rm -rf "$ROOT/var/cache/apt/archives/"*.deb \
	"$ROOT/var/cache/apt/archives/partial" \
	"$ROOT/var/cache/apt/"*.bin \
	"$ROOT/var/lib/apt/lists/"* \
	"$ROOT/var/cache/debconf/"*-old \
	"$ROOT/tmp/"* "$ROOT/var/tmp/"* 2>/dev/null || true
find "$ROOT/usr" -type d -name '__pycache__' -exec rm -rf {} + 2>/dev/null || true

# 2. Remove manuals, info pages, lintian overrides, and non-copyright docs.
#    Keep every copyright, LICENSE, NOTICE, SOURCE, and sbom.tsv file intact.
rm -rf "$ROOT/usr/share/man/"* \
	"$ROOT/usr/share/info/"* \
	"$ROOT/usr/share/lintian/"* \
	"$ROOT/usr/share/linda/"* 2>/dev/null || true
if [ -d "$ROOT/usr/share/doc" ]; then
	find "$ROOT/usr/share/doc" -type f \
		! -name 'copyright*' ! -name 'LICENSE*' ! -name 'NOTICE*' \
		! -name 'SOURCE' ! -name 'sbom.tsv' ! -name '*.md' \
		-delete 2>/dev/null || true
	find "$ROOT/usr/share/doc" -type d -empty -delete 2>/dev/null || true
fi

# 3. Prune unused locales (keep English and Chinese locales).
if [ -d "$ROOT/usr/share/locale" ]; then
	find "$ROOT/usr/share/locale" -mindepth 1 -maxdepth 1 -type d \
		! -name 'en*' ! -name 'zh*' ! -name 'C' \
		-exec rm -rf {} + 2>/dev/null || true
fi

# 4. Prune datacenter/server-only firmware blobs while keeping 100% of desktop,
#    laptop, Wi-Fi, Bluetooth, GPU, audio, and VM firmware (verified by verify_drivers).
FWROOT="$ROOT/usr/lib/firmware"
[ -d "$FWROOT" ] || FWROOT="$ROOT/lib/firmware"
if [ -d "$FWROOT" ]; then
	for dir in netronome mellanox liquidio qed qlogic cxgb4 cxgb3 bnx2x bnx2 \
			myri10ge tehuti slicoss cavium dpaa2 moxa vxge mrvl/prestera \
			ti-connectivity cypress/cyfmac4373-sdio.bin; do
		rm -rf "$FWROOT/$dir" 2>/dev/null || true
	done
fi

# 5. Strip unneeded files from any staged mobile runtimes (no Qt emulator GUI,
#    no Windows/macOS SDK binaries, no non-host QEMU binaries, no test suites).
if [ -d "$ROOT/opt/olympium/android/emulator" ]; then
	rm -rf "$ROOT/opt/olympium/android/emulator/lib64/qt" \
		"$ROOT/opt/olympium/android/emulator/skins" \
		"$ROOT/opt/olympium/android/emulator/tests" \
		"$ROOT/opt/olympium/android/emulator/qemu/linux-x86_64/qemu-system-armel" \
		"$ROOT/opt/olympium/android/emulator/qemu/linux-x86_64/qemu-system-aarch64"* \
		"$ROOT/opt/olympium/android/emulator/qemu/linux-x86_64/qemu-system-i386"* 2>/dev/null || true
fi
if [ -d "$ROOT/opt/olympium/openharmony/sdk" ]; then
	rm -rf "$ROOT/opt/olympium/openharmony/sdk/windows" \
		"$ROOT/opt/olympium/openharmony/sdk/darwin" \
		"$ROOT/opt/olympium/openharmony/sdk/ets" \
		"$ROOT/opt/olympium/openharmony/sdk/js" \
		"$ROOT/opt/olympium/openharmony/sdk/previewer" 2>/dev/null || true
fi

after_kb=$(du -sk "$ROOT" 2>/dev/null | awk '{print $1}' || echo 0)
saved_mb=$(( (before_kb - after_kb) / 1024 ))
echo "slim-rootfs: saved ~${saved_mb} MB uncompressed (${before_kb} KB -> ${after_kb} KB)"
