#!/bin/sh
# Copyright 2026 Ye Zicheng
# SPDX-License-Identifier: Apache-2.0

set -eu
root=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
out="$root/out"
stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT
mkdir -p "$stage/control" "$stage/data/usr/bin" "$out"
cp "$root/DEBIAN/control" "$stage/control/control"
cp "$root/usr/bin/hello-olympium" "$stage/data/usr/bin/hello-olympium"
chmod 755 "$stage/data/usr/bin/hello-olympium"
tar -C "$stage/control" -czf "$stage/control.tar.gz" control
tar -C "$stage/data" -czf "$stage/data.tar.gz" usr
printf '2.0\n' > "$stage/debian-binary"
rm -f "$out/hello-olympium.deb"
(
    cd "$stage"
    ar rcs "$out/hello-olympium.deb" debian-binary control.tar.gz data.tar.gz
)
echo "$out/hello-olympium.deb"
