#!/bin/bash
# Copyright 2026 Development Desktop contributors
# License: daniel.ye Source License, Version 1.0. See LICENSE.
#
# First-use guest fetch. A pin is fetched only when its record says the
# artifact may be downloaded from the named upstream. Unknown or forbidden
# redistribution is refused. This is not a way around a license.
set -euo pipefail
PIN=${1:-/usr/share/olympium/guest-pins.txt}
DEST=${2:-/var/lib/olympium/guests}
mkdir -p "$DEST"
if [ ! -f "$PIN" ]; then
	echo "no pin file: $PIN" >&2
	exit 1
fi
awk '
	/^#/ || NF==0 { next }
	{
		name=$1; url=$2; sha=$3; policy=$4;
		print name, url, sha, policy
	}
' "$PIN" | while read -r name url sha policy; do
	case "$policy" in
		first-use)
			;;
		*)
			echo "refuse $name: policy '$policy' does not allow a download"
			continue
			;;
	esac
	out="$DEST/$name"
	echo "fetch $name"
	curl -fL --retry 2 -o "$out.partial" "$url"
	got=$(sha256sum "$out.partial" | awk '{print $1}')
	if [ "$got" != "$sha" ]; then
		echo "checksum mismatch for $name" >&2
		rm -f "$out.partial"
		exit 1
	fi
	mv "$out.partial" "$out"
	echo "kept $out"
done
