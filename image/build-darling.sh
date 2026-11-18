#!/bin/bash
# Copyright 2026 Development Desktop contributors
# License: daniel.ye Source License, Version 1.0. See LICENSE.
#
# Builds Darling's command-line components from source, at the revision this
# tree was run against, and stages the result where the image build looks for
# it. Darling is the open-source project that runs macOS programs on Linux. It
# is a separate program under GPL-3.0, with parts under other licenses. Its
# source is not in this tree and is not changed by it.
#
#   image/build-darling.sh
#
#   DARLING_SRC         where the source is cloned       /var/tmp/darling-src
#   DARLING_OUT         where the result is staged       /var/tmp/darling-out
#   DARLING_COMPONENTS  what to build                    cli
#
# "cli" is the loader, the server, the system libraries, and the command-line
# programs. It runs command-line Mach-O programs. It has no AppKit, so it does
# not open a graphical Mac application. Use DARLING_COMPONENTS='cli;gui'
# to build AppKit and its framework dependencies.
#
# On the build machine, an x86_64 computer with four processors and Ubuntu
# 24.04, the clone with its 157 submodules was 5 GB, the build was 15,457
# steps and about 20 minutes, and the staged result was 293 MB. x86_64 only.
set -euo pipefail
umask 022
PIN=3fcd2e1aa85079789d19d82415695bdecc512241
SRC=${DARLING_SRC:-/var/tmp/darling-src}
OUT=${DARLING_OUT:-/var/tmp/darling-out}
COMPONENTS=${DARLING_COMPONENTS:-cli}
SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
PATCH_DIR=$SCRIPT_DIR/darling-patches
PATCHES=()
normalized_components=${COMPONENTS,,}
normalized_components=${normalized_components//[[:space:]]/}
normalized_components=${normalized_components//,/;}
case ";${normalized_components};" in
	*';gui;'*|*';gui_frameworks;'*)
		# At PIN, AppKit links UniformTypeIdentifiers. Darling enables that
		# framework through gui_stubs, which gui does not include by itself.
		COMPONENTS+=";gui_stubs"
		;;
esac
case ";${normalized_components};" in
	*';gui;'*|*';gui_frameworks;'*|*';gui_stubs;'*|*';stock;'*|*';all;'*)
		PATCHES=("$PATCH_DIR"/*.patch)
		;;
esac

missing=""
for tool in git cmake ninja clang bison flex pkg-config python3; do
	command -v "$tool" >/dev/null 2>&1 || missing="$missing $tool"
done
if [ -n "$missing" ]; then
	echo "build-darling: these are not installed:$missing" >&2
	echo "On Ubuntu 24.04 the whole set is:" >&2
	echo "  apt-get install cmake clang bison flex libfuse-dev libudev-dev pkg-config libc6-dev-i386 gcc-multilib \\" >&2
	echo "    libcairo2-dev libgl1-mesa-dev libtiff5-dev libfreetype6-dev libelf-dev libxml2-dev libegl1-mesa-dev \\" >&2
	echo "    libglu1-mesa-dev libfontconfig1-dev libbsd-dev ninja-build libxrandr-dev libxcursor-dev libgif-dev \\" >&2
	echo "    libavcodec-dev libavformat-dev libswresample-dev libavutil-dev libpulse-dev libdbus-1-dev \\" >&2
	echo "    libxkbfile-dev libssl-dev libcap-dev llvm-dev libvulkan-dev libcap2-bin lld" >&2
	exit 1
fi

# The exact revision, without its history. Large files kept with Git LFS are
# left as pointers: the components built here do not need them.
export GIT_LFS_SKIP_SMUDGE=1
if [ ! -d "$SRC/.git" ]; then
	mkdir -p "$SRC"
	git -C "$SRC" init -q
	git -C "$SRC" remote add origin https://github.com/darlinghq/darling
fi
git -C "$SRC" fetch -q --depth 1 origin "$PIN"
git -C "$SRC" checkout -q FETCH_HEAD
git -C "$SRC" submodule update --init --recursive --depth 1 --jobs 2
if [ "${#PATCHES[@]}" -gt 0 ]; then
	cocotron=$SRC/src/external/cocotron
	if ! git -C "$cocotron" apply --reverse --check "${PATCHES[@]}" >/dev/null 2>&1; then
		git -C "$cocotron" apply --check "${PATCHES[@]}"
		git -C "$cocotron" apply "${PATCHES[@]}"
	fi
fi

mkdir -p "$SRC/build"
cd "$SRC/build"
cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -DCOMPONENTS="$COMPONENTS" -DCMAKE_INSTALL_PREFIX=/usr/local ..
ninja
rm -rf "$OUT"
DESTDIR="$OUT" ninja install
# Groff's install rule preserves checkout modes, including the guest /usr
# directory. Make the staged system files accessible to guest app users.
guest_root="$OUT/usr/local/libexec/darling"
guest_usr="$guest_root/usr"
if [ -d "$guest_usr" ] && [ ! -L "$guest_usr" ]; then
	chmod -R a+rX "$guest_usr"
fi
chmod -R go-w "$guest_root"

# What the image needs beside the programs: the license, and what was built.
install -m 644 "$SRC/LICENSE" "$OUT/LICENSE"
{
	echo "Darling, https://github.com/darlinghq/darling"
	echo "commit $PIN"
	echo "components $COMPONENTS"
	echo "built by image/build-darling.sh"
	if [ "${#PATCHES[@]}" -gt 0 ]; then
		echo "AppKit patch SHA256:"
		(cd "$PATCH_DIR" && sha256sum -- ./*.patch)
	fi
	echo "license GPL-3.0 for Darling itself; each submodule keeps its own,"
	echo "including the Apple Public Source License for the parts Apple published"
	echo
	echo "sha256 of the Linux programs:"
	(cd "$OUT/usr/local/bin" && sha256sum darling darlingserver darpath darling-coredump)
} > "$OUT/SOURCE"
du -sh "$OUT" | awk '{ print "build-darling: staged " $1 " at '"$OUT"'" }'
