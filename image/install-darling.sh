#!/bin/bash
# Copyright 2026 Development Desktop contributors
# License: daniel.ye Source License, Version 1.0. See LICENSE.
#
# Copies a Darling that image/build-darling.sh staged into an image root, so
# that an installed Mac application has a runtime to be started with. Run it
# with /proc mounted in the root. It downloads nothing but the few Ubuntu
# libraries Darling's server is linked against.
#
#   image/install-darling.sh ROOT [STAGED]
#
# x86_64 roots only. Without a staged build it installs nothing and says so.
set -euo pipefail
ROOT=${1:?root file system}
OUT=${2:-${DARLING_OUT:-/var/tmp/darling-out}}
if [ ! -x "$OUT/usr/local/bin/darling" ] || [ ! -d "$OUT/usr/local/libexec/darling" ]; then
	echo "install-darling: no staged build at $OUT; the image ships without a Mac runtime"
	exit 0
fi
if [ ! -f "$OUT/LICENSE" ] || [ ! -f "$OUT/SOURCE" ]; then
	echo "install-darling: $OUT has no LICENSE and SOURCE, so nothing is installed" >&2
	exit 1
fi
in_root() {
	if [ "$ROOT" = / ]; then
		env DEBIAN_FRONTEND=noninteractive "$@"
	else
		chroot "$ROOT" /usr/bin/env DEBIAN_FRONTEND=noninteractive "$@"
	fi
}
# What darlingserver is linked against, by the names Ubuntu 24.04 gives them.
in_root apt-get update
in_root apt-get install -y --no-install-recommends libxml2 libicu74 libssl3t64 zlib1g liblzma5 libstdc++6

install -d "$ROOT/usr/local/bin" "$ROOT/usr/local/libexec" "$ROOT/usr/share/doc/darling"
rm -rf "$ROOT/usr/local/libexec/darling"
cp -a "$OUT/usr/local/libexec/darling" "$ROOT/usr/local/libexec/darling"
for name in darling darlingserver darpath darling-coredump; do
	install -m 755 "$OUT/usr/local/bin/$name" "$ROOT/usr/local/bin/$name"
done
# The launcher sets up the container's namespaces, which takes root.
chown root:root "$ROOT/usr/local/bin/darling"
chmod 4755 "$ROOT/usr/local/bin/darling"
install -m 644 "$OUT/LICENSE" "$OUT/SOURCE" "$ROOT/usr/share/doc/darling/"

if in_root sh -c 'ldd /usr/local/bin/darlingserver | grep -q "not found"'; then
	echo "install-darling: darlingserver has a missing library in this root" >&2
	in_root ldd /usr/local/bin/darlingserver | grep "not found" >&2
	exit 1
fi
du -sh "$ROOT/usr/local/libexec/darling" | awk '{ print "install-darling: installed, " $1 }'
