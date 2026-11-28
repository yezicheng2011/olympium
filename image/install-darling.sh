#!/bin/bash
# Copyright 2026 Development Desktop contributors
# SPDX-License-Identifier: Apache-2.0 — Licensed under the Apache License, Version 2.0. See LICENSE.
#
# Installs Darling GUI runtime (for Intel x86_64 and Universal2 Mac apps, plus
# optional ARM64/M-series Darling staging) into an Olympium image root.
#
# Supports two verified paths:
#   1. Source-built Darling GUI staged at $DARLING_OUT (/var/tmp/darling-out)
#   2. Pinned official Darling GUI .deb release (v0.1.20260608 from
#      validation/runtime-artifacts.json) when OLYMPIUM_DARLING_DEBS=1 or
#      when no source build is present at $DARLING_OUT.
set -euo pipefail

ROOT=${1:?root file system}
OUT=${2:-${DARLING_OUT:-/var/tmp/darling-out}}
SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$SCRIPT_DIR/.." && pwd)

in_root() {
	if [ "$ROOT" = / ]; then
		env DEBIAN_FRONTEND=noninteractive "$@"
	else
		chroot "$ROOT" /usr/bin/env DEBIAN_FRONTEND=noninteractive "$@"
	fi
}

install_from_staged() {
	if [ ! -f "$OUT/LICENSE" ] || [ ! -f "$OUT/SOURCE" ]; then
		echo "install-darling: $OUT has no LICENSE and SOURCE, so nothing is installed" >&2
		exit 1
	fi
	in_root apt-get update
	in_root apt-get install -y --no-install-recommends \
		libxml2 libicu74 libssl3t64 zlib1g liblzma5 libstdc++6 \
		libx11-6 libxext6 libxrandr2 libxcursor1 libgl1 libegl1 libcairo2 \
		libfontconfig1 libfreetype6 libdbus-1-3 xwayland

	install -d "$ROOT/usr/local/bin" "$ROOT/usr/local/libexec" "$ROOT/usr/share/doc/darling"
	rm -rf "$ROOT/usr/local/libexec/darling"
	cp -a "$OUT/usr/local/libexec/darling" "$ROOT/usr/local/libexec/darling"
	for name in darling darlingserver darpath darling-coredump; do
		if [ -x "$OUT/usr/local/bin/$name" ]; then
			install -m 755 "$OUT/usr/local/bin/$name" "$ROOT/usr/local/bin/$name"
		fi
	done
	chown root:root "$ROOT/usr/local/bin/darling"
	chmod 4755 "$ROOT/usr/local/bin/darling"
	install -m 644 "$OUT/LICENSE" "$OUT/SOURCE" "$ROOT/usr/share/doc/darling/"
	if in_root sh -c 'ldd /usr/local/bin/darlingserver | grep -q "not found"'; then
		echo "install-darling: darlingserver has a missing library in this root" >&2
		in_root ldd /usr/local/bin/darlingserver | grep "not found" >&2
		exit 1
	fi
	du -sh "$ROOT/usr/local/libexec/darling" | awk '{ print "install-darling: installed GUI runtime from source stage, " $1 }'
}

install_from_official_debs() {
	local pin_url="https://github.com/darlinghq/darling/releases/download/v0.1.20260608/debs_20260608.zip"
	local pin_sha="27469ef3932da2e91dd7fb34b70e3628a3e54b7af9fb5480051f44af35eca1fd"
	local cache="/var/tmp/dev-live/darling-debs-cache"
	mkdir -p "$cache"
	local zip="$cache/debs_20260608.zip"
	if [ ! -f "$zip" ]; then
		echo "install-darling: downloading official Darling GUI release v0.1.20260608..."
		curl -fsSL -o "${zip}.part" "$pin_url"
		local got
		got=$(sha256sum "${zip}.part" | awk '{print $1}')
		if [ "$got" != "$pin_sha" ]; then
			rm -f "${zip}.part"
			echo "install-darling: checksum mismatch for official Darling GUI release ($got != $pin_sha)" >&2
			exit 1
		fi
		mv "${zip}.part" "$zip"
	fi
	local tmpdir
	tmpdir=$(mktemp -d)
	trap 'rm -rf "$tmpdir"' RETURN
	python3 -c "import zipfile,sys; zipfile.ZipFile(sys.argv[1]).extractall(sys.argv[2])" "$zip" "$tmpdir"
	mkdir -p "$ROOT/tmp/darling-debs"
	find "$tmpdir" -name '*.deb' -exec cp {} "$ROOT/tmp/darling-debs/" \;
	in_root apt-get update
	in_root sh -c 'dpkg -i /tmp/darling-debs/*.deb || apt-get install -f -y --no-install-recommends'
	rm -rf "$ROOT/tmp/darling-debs"
	if [ -x "$ROOT/usr/bin/darling" ] && [ ! -e "$ROOT/usr/local/bin/darling" ]; then
		ln -sfn /usr/bin/darling "$ROOT/usr/local/bin/darling"
	fi
	echo "install-darling: installed official Darling GUI packages v0.1.20260608."
}

if [ -x "$OUT/usr/local/bin/darling" ] && [ -d "$OUT/usr/local/libexec/darling" ]; then
	install_from_staged
elif [ "${OLYMPIUM_DARLING_DEBS:-1}" = 1 ]; then
	install_from_official_debs
else
	echo "install-darling: no staged build at $OUT and OLYMPIUM_DARLING_DEBS=0; skipping Darling"
fi

# Optional Apple Silicon (M-series / ARM64) Darling build when pre-staged.
ARM64_OUT=${DARLING_ARM64_OUT:-/var/tmp/darling-arm64-out}
if [ -x "$ARM64_OUT/bin/darling" ]; then
	install -d "$ROOT/opt/olympium/darling-arm64" "$ROOT/usr/local/bin"
	cp -a "$ARM64_OUT/." "$ROOT/opt/olympium/darling-arm64/"
	ln -sfn /opt/olympium/darling-arm64/bin/darling "$ROOT/usr/local/bin/darling-arm64"
	echo "install-darling: staged ARM64 (M-series) Darling runtime under /opt/olympium/darling-arm64."
fi
