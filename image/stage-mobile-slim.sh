#!/bin/bash
# Copyright 2026 Development Desktop contributors
# SPDX-License-Identifier: Apache-2.0 — Licensed under the Apache License, Version 2.0. See LICENSE.
#
# Stages the SLIM Android (scrcpy + adb + aapt + compat assets) and SLIM
# OpenHarmony (local SDK hdc + auto-configured runtime descriptor + QEMU)
# environments into the image root WITHOUT stuffing multi-GB full stock OS
# images or unnecessary SDK components into the ISO.
set -euo pipefail

ROOT=${1:?target rootfs directory}
ARCH=${2:-amd64}
REPO=$(cd "$(dirname "$0")/.." && pwd)

in_root() {
	if [ "$ROOT" = / ]; then
		env DEBIAN_FRONTEND=noninteractive "$@"
	else
		chroot "$ROOT" /usr/bin/env DEBIAN_FRONTEND=noninteractive "$@"
	fi
}

# 1. Install scrcpy + adb + aapt (fixes "Android component is not installed: scrcpy")
#    and qemu-system-x86_64 (for minimal local OpenHarmony / 64-bit guest sessions).
in_root apt-get update
if [ "$ARCH" = amd64 ]; then
	in_root apt-get install -y --no-install-recommends \
		aapt adb scrcpy qemu-system-x86 qemu-utils
else
	in_root apt-get install -y --no-install-recommends \
		aapt adb scrcpy
fi

# 2. Pre-configure local OpenHarmony SDK directory and default runtime layout.
#    Only the slim Linux toolchain binary (hdc) and open-source microG GmsCore
#    (Apache-2.0; NO proprietary Google Play Store) are staged — never the
#    multi-GB full OS images or bloated SDK archives.
install -d "$ROOT/opt/olympium/android/microg" \
	"$ROOT/opt/olympium/openharmony/sdk/toolchains" \
	"$ROOT/opt/olympium/openharmony/oniro" \
	"$ROOT/etc/olympium"

# Stage open-source microG GmsCore + GsfProxy (Apache-2.0) when present or requested.
# Proprietary Google Play Store (com.android.vending.apk) is deliberately excluded.
# SECURITY: Remote downloads MUST be verified against an explicit SHA-256 digest
# (OLYMPIUM_MICROG_GMS_SHA256) to prevent supply-chain / MITM tampering.
if [ -n "${OLYMPIUM_MICROG_DIR:-}" ] && [ -d "${OLYMPIUM_MICROG_DIR}" ]; then
	for apk in com.google.android.gms.apk com.google.android.gsf.apk; do
		if [ -f "${OLYMPIUM_MICROG_DIR}/$apk" ]; then
			install -m 644 "${OLYMPIUM_MICROG_DIR}/$apk" "$ROOT/opt/olympium/android/microg/$apk"
		fi
	done
elif [ "${OLYMPIUM_FETCH_MICROG:-0}" = 1 ] && [ -n "${OLYMPIUM_MICROG_GMS_SHA256:-}" ] && command -v curl >/dev/null 2>&1; then
	MICROG_GMS_URL="${OLYMPIUM_MICROG_GMS_URL:-https://github.com/microg/GmsCore/releases/latest/download/com.google.android.gms-240913008.apk}"
	tmp_apk="$(mktemp)"
	if curl -fsSL --proto '=https' --tlsv1.2 "$MICROG_GMS_URL" -o "$tmp_apk"; then
		if printf '%s  %s\n' "$OLYMPIUM_MICROG_GMS_SHA256" "$tmp_apk" | sha256sum -c - >/dev/null 2>&1; then
			install -m 644 "$tmp_apk" "$ROOT/opt/olympium/android/microg/com.google.android.gms.apk"
		else
			echo "stage-mobile-slim: WARNING: microG SHA-256 mismatch; refusing unverified APK." >&2
		fi
	fi
	rm -f "$tmp_apk"
fi

# Copy pre-staged slim hdc if present on the build host (avoids multi-GB download).
for hdc_cand in "${OLYMPIUM_OH_HDC:-}" /opt/olympium/openharmony/sdk/toolchains/hdc /usr/local/bin/hdc; do
	if [ -n "$hdc_cand" ] && [ -x "$hdc_cand" ]; then
		install -m 755 "$hdc_cand" "$ROOT/opt/olympium/openharmony/sdk/toolchains/hdc"
		ln -sfn /opt/olympium/openharmony/sdk/toolchains/hdc "$ROOT/usr/local/bin/hdc"
		break
	fi
done

# Copy pre-staged pinned scrcpy v5+ binary if available on the build host.
if [ -n "${OLYMPIUM_SCRCPY_DIR:-}" ] && [ -x "${OLYMPIUM_SCRCPY_DIR}/scrcpy" ]; then
	install -d "$ROOT/opt/olympium/android/scrcpy"
	cp -a "${OLYMPIUM_SCRCPY_DIR}/." "$ROOT/opt/olympium/android/scrcpy/"
	ln -sfn /opt/olympium/android/scrcpy/scrcpy "$ROOT/usr/local/bin/scrcpy"
fi

cat > "$ROOT/etc/olympium/mobile-runtime.conf" <<EOF
# Olympium slim mobile runtime configuration (no full bloated OS images embedded)
android_runtime=/opt/olympium/android
android_scrcpy=$( [ -x "$ROOT/opt/olympium/android/scrcpy/scrcpy" ] && echo /opt/olympium/android/scrcpy/scrcpy || echo /usr/bin/scrcpy )
android_microg_dir=/opt/olympium/android/microg
android_microg_core=1
android_play_store_bundled=0
interop_shared_clipboard=1
interop_android_widgets=1
openharmony_runtime=/opt/olympium/openharmony
openharmony_auto_session=1
EOF

echo "stage-mobile-slim: scrcpy, adb, aapt, open-source microG Core (no Play Store), and slim OpenHarmony local SDK layout configured."
