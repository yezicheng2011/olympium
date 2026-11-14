#!/bin/sh
# Copyright 2026 Ye Zicheng
# SPDX-License-Identifier: Apache-2.0

set -eu
cd "$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)"

cargo build -p olympium-cli -p hello-linux -p olympium-linux
bin="./target/debug/olympium"
home="$(mktemp -d)"
export LINKSPAN_HOME="$home"

"$bin" run --file ./target/debug/hello-linux --json | grep -q 'olympium-guest-ok platform=linux format=elf'

chmod +x examples/linux-deb/build.sh
deb="$(examples/linux-deb/build.sh)"
"$bin" run --file "$deb" --json | grep -q 'olympium-guest-ok platform=linux format=deb'

if command -v x86_64-w64-mingw32-gcc >/dev/null && command -v wine >/dev/null; then
    exe="$(mktemp --suffix .exe)"
    x86_64-w64-mingw32-gcc -O2 -o "$exe" examples/windows/hello.c
    "$bin" run --capture --file "$exe" | grep -q 'olympium-guest-ok platform=windows format=exe'
fi

printf 'dmg' > "$home/example.dmg"
if "$bin" run --file "$home/example.dmg" --json >/tmp/olympium-dmg.json; then
    echo "a macOS disk image was started on Linux" >&2
    exit 1
fi
grep -q 'apple-platform-on-non-apple-hardware' /tmp/olympium-dmg.json

if command -v xvfb-run >/dev/null; then
    ready="$(mktemp)"
    out="$(mktemp)"
    LINKSPAN_SELF_CHECK="$PWD/target/debug/hello-linux" \
    LINKSPAN_SELF_CHECK_OUT="$out" \
    LINKSPAN_SELF_CHECK_READY="$ready" \
    GDK_BACKEND=x11 \
    xvfb-run -a -s "-screen 0 1200x800x24" timeout 40 ./target/debug/olympium-linux
    grep -q ready "$ready"
    grep -q 'olympium-guest-ok platform=linux format=elf' "$out"
fi

echo "Linux end-to-end checks passed."
