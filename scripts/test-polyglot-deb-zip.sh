#!/bin/sh
# Copyright 2026 Ye Zicheng
# SPDX-License-Identifier: Apache-2.0
# Check one byte string with Debian and zip parsers. Optional extra file.
set -eu
cd "$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)"

fail() {
  echo "polyglot check failed: $*" >&2
  exit 1
}

check_pair() {
  src=$1
  expect_deb=$2
  work=$(mktemp -d)
  cp "$src" "$work/same.deb"
  cp "$src" "$work/same.zip"
  cmp -s "$work/same.deb" "$work/same.zip" || fail "the .deb and .zip copies differ"
  dpkg-deb -I "$work/same.deb" >/dev/null || fail "dpkg-deb cannot read the Debian side"
  dpkg-deb -x "$work/same.deb" "$work/deb"
  find "$work/deb" -type f | grep -q . || fail "dpkg-deb extracted no files"
  python3 - "$work/same.zip" "$work/zip" <<'PY'
import sys, zipfile
from pathlib import Path
src, dest = sys.argv[1:]
Path(dest).mkdir(parents=True, exist_ok=True)
with zipfile.ZipFile(src) as zf:
    names = zf.namelist()
    if not names:
        raise SystemExit("zip has no members")
    zf.extractall(dest)
PY
  find "$work/zip" -type f | grep -q . || fail "zipfile extracted no files"
  # Info-ZIP warns and returns 1 when bytes precede the zip. The member must still extract.
  unzip -p "$work/same.zip" README.txt > "$work/readme.txt" || true
  grep -q . "$work/readme.txt" || fail "unzip did not print README.txt"
  if ar t "$work/same.deb" >/dev/null 2>&1; then
    fail "GNU ar accepted trailing bytes; this test expected it to reject them"
  fi
  if command -v rpm >/dev/null 2>&1; then
    if rpm -qp "$work/same.deb" >/dev/null 2>&1; then
      fail "rpm accepted a Debian polyglot"
    fi
  fi
  if command -v msiinfo >/dev/null 2>&1; then
    if msiinfo tables "$work/same.deb" >/dev/null 2>&1; then
      fail "msiinfo accepted a Debian polyglot"
    fi
  fi
  file -b "$work/same.deb" | grep -q 'Debian binary package' || fail "file(1) did not identify the Debian side"
  if [ "$expect_deb" = install ] && [ "$(id -u)" -eq 0 ]; then
    dpkg -i "$work/same.deb" >/dev/null
    /usr/bin/linkspan-polyglot-probe | grep -q 'linkspan-polyglot-probe-ok'
    dpkg -r linkspan-polyglot-probe >/dev/null
    if [ -e /usr/bin/linkspan-polyglot-probe ]; then
      fail "dpkg remove left the probe script behind"
    fi
  fi
  rm -rf "$work"
}

probe=$(mktemp)
scripts/build-polyglot-deb-zip.sh --probe "$probe" >/dev/null
check_pair "$probe" install
rm -f "$probe"
echo "deb+zip polyglot passed Debian and zip parsers"

if [ $# -gt 0 ]; then
  check_pair "$1" noinstall
  echo "checked extra polyglot $1"
fi
