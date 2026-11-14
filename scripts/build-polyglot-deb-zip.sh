#!/bin/sh
# Copyright 2026 Ye Zicheng
# SPDX-License-Identifier: Apache-2.0
# Build one byte string that is a gzip Debian package and a zip archive.
# The file is an experiment. It is not the macOS download and it is not signed.
set -eu
cd "$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)"

mode=probe
binary=
version=0.0.3
out=
while [ $# -gt 0 ]; do
  case "$1" in
    --probe) mode=probe; shift ;;
    --binary) mode=binary; binary=$2; shift 2 ;;
    --version) version=$2; shift 2 ;;
    *) out=$1; shift ;;
  esac
done
if [ -z "$out" ]; then
  echo "Usage: $0 [--probe | --binary FILE --version VER] OUTPUT" >&2
  exit 2
fi

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
pkg=$work/pkg
mkdir -p "$pkg/DEBIAN"

if [ "$mode" = probe ]; then
  mkdir -p "$pkg/usr/bin"
  cat > "$pkg/usr/bin/olympium-polyglot-probe" <<'EOF'
#!/bin/sh
echo olympium-polyglot-probe-ok
EOF
  chmod 755 "$pkg/usr/bin/olympium-polyglot-probe"
  cat > "$pkg/DEBIAN/control" <<EOF
Package: olympium-polyglot-probe
Version: ${version}
Section: misc
Priority: optional
Architecture: all
Maintainer: Ye Zicheng <yezicheng2011@users.noreply.github.com>
Description: experimental deb and zip polyglot probe
 Installs one script so the same bytes can be checked as a Debian package and as a zip archive. Not a release.
EOF
  zip_dir=$work/zip
  mkdir -p "$zip_dir"
  cp "$pkg/usr/bin/olympium-polyglot-probe" "$zip_dir/olympium-polyglot-probe"
  printf '%s\n' 'Experimental deb+zip polyglot. Not the official Olympium download.' > "$zip_dir/README.txt"
else
  if [ ! -f "$binary" ] || [ ! -x "$binary" ]; then
    echo "Binary is missing: $binary" >&2
    exit 1
  fi
  mkdir -p "$pkg/usr/lib/olympium-polyglot" "$pkg/usr/share/doc/olympium-polyglot"
  cp "$binary" "$pkg/usr/lib/olympium-polyglot/olympium"
  chmod 755 "$pkg/usr/lib/olympium-polyglot/olympium"
  cat > "$pkg/usr/share/doc/olympium-polyglot/copyright" <<EOF
Olympium polyglot experiment
Copyright 2026 Ye Zicheng
SPDX-License-Identifier: Apache-2.0

The olympium command in this package is Apache-2.0.
This package is not the macOS 0.1.0 download and it is not signed.
Guest images are not included.
EOF
  cat > "$pkg/DEBIAN/control" <<EOF
Package: olympium-polyglot
Version: ${version}
Section: utils
Priority: optional
Architecture: amd64
Depends: libc6
Maintainer: Ye Zicheng <yezicheng2011@users.noreply.github.com>
Description: experimental Olympium command in a deb+zip polyglot
 Installs the olympium command under /usr/lib/olympium-polyglot. The same file is also a zip archive. Not the official macOS package.
EOF
  zip_dir=$work/zip
  mkdir -p "$zip_dir"
  cp "$pkg/usr/lib/olympium-polyglot/olympium" "$zip_dir/olympium"
  cp "$pkg/usr/share/doc/olympium-polyglot/copyright" "$zip_dir/copyright"
  printf '%s\n' "Experimental Olympium ${version} deb+zip polyglot for Linux amd64. Not the macOS download. Not signed." > "$zip_dir/README.txt"
fi

dpkg-deb -Zgzip --root-owner-group --build "$pkg" "$work/plain.deb" >/dev/null
(
  cd "$zip_dir"
  # Store the names without a directory prefix so unzip -p README.txt works.
  zip -q -X "$work/payload.zip" -- *
)
cat "$work/plain.deb" "$work/payload.zip" > "$work/poly.bin"
mkdir -p "$(dirname "$out")"
cp "$work/poly.bin" "$out"
sha=$(sha256sum "$out" | awk '{print $1}')
bytes=$(wc -c < "$out" | tr -d ' ')
printf '%s  %s\n' "$sha" "$(basename "$out")" > "${out}.sha256"
echo "$sha $bytes $out"
