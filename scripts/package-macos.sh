#!/bin/sh
# Copyright 2026 Ye Zicheng
# SPDX-License-Identifier: Apache-2.0
#
# Build a Olympium .pkg on macOS. The app is ad-hoc signed unless a
# certificate is provided in the environment. Nothing in this script
# writes a certificate into the repository. Notarization runs only when
# the notary variables are all set.

set -eu
cd "$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)"

version="$(sh scripts/check-client-version.sh)"

if [ "$(uname -s)" != Darwin ]; then
  echo "This packaging script runs on macOS." >&2
  exit 2
fi

dist="dist/macos"
rm -rf "$dist"
mkdir -p "$dist/payload" "$dist/notes"

if command -v cargo >/dev/null 2>&1; then
  cargo build -p olympium-cli --release
fi

xcodebuild \
  -project hosts/macos/Olympium.xcodeproj \
  -scheme Olympium \
  -configuration Release \
  -destination 'platform=macOS' \
  -derivedDataPath "$dist/DerivedData" \
  CODE_SIGNING_ALLOWED=YES \
  CODE_SIGN_IDENTITY=- \
  CODE_SIGN_STYLE=Manual \
  DEVELOPMENT_TEAM= \
  build

app="$(find "$dist/DerivedData" -type d -name 'Olympium.app' -print | head -n 1)"
if [ -z "$app" ]; then
  echo "xcodebuild did not produce Olympium.app" >&2
  exit 1
fi

stage="$dist/payload/Olympium.app"
rm -rf "$stage"
cp -R "$app" "$stage"
if [ -x target/release/olympium ]; then
  mkdir -p "$stage/Contents/MacOS"
  cp target/release/olympium "$stage/Contents/MacOS/olympium"
fi
if [ -f hosts/macos/Olympium/Resources/Olympium.icns ]; then
  mkdir -p "$stage/Contents/Resources"
  cp hosts/macos/Olympium/Resources/Olympium.icns "$stage/Contents/Resources/Olympium.icns"
  if ! /usr/libexec/PlistBuddy -c "Set :CFBundleIconFile Olympium" "$stage/Contents/Info.plist"; then
    /usr/libexec/PlistBuddy -c "Add :CFBundleIconFile string Olympium" "$stage/Contents/Info.plist"
  fi
fi

identity="-"
signing_note="The application bundle is ad-hoc signed with codesign -s -. No certificate was read from the repository."
if [ -n "${APPLE_CERTIFICATE_P12:-}" ] && [ -n "${APPLE_SIGN_IDENTITY:-}" ]; then
  keychain="$dist/ci.keychain"
  security create-keychain -p olympium-ci "$keychain"
  security default-keychain -s "$keychain"
  security unlock-keychain -p olympium-ci "$keychain"
  security set-keychain-settings -t 600 "$keychain"
  if printf '' | base64 -d >/dev/null 2>&1; then
    printf '%s' "$APPLE_CERTIFICATE_P12" | base64 -d > "$dist/cert.p12"
  else
    printf '%s' "$APPLE_CERTIFICATE_P12" | base64 -D > "$dist/cert.p12"
  fi
  security import "$dist/cert.p12" -k "$keychain" -P "${APPLE_CERTIFICATE_PASSWORD:-}" -T /usr/bin/codesign -T /usr/bin/productsign
  security set-key-partition-list -S apple-tool:,apple: -s -k olympium-ci "$keychain" >/dev/null
  rm -f "$dist/cert.p12"
  identity="$APPLE_SIGN_IDENTITY"
  signing_note="The application was signed with the identity named by APPLE_SIGN_IDENTITY from the runner environment. The certificate file was not written into the repository."
fi

codesign --force --deep --sign "$identity" "$stage"
codesign --verify --verbose=2 "$stage"

pkg="$dist/notes/Olympium-${version}.pkg"
pkgbuild \
  --component "$stage" \
  --identifier app.olympium.macos \
  --version "$version" \
  --install-location /Applications \
  "$pkg"

pkg_note="Apple does not ad-hoc sign installer packages the way it ad-hoc signs an application. The .pkg contains the ad-hoc signed app. It stays unsigned unless APPLE_INSTALLER_IDENTITY is set."
if [ -n "${APPLE_INSTALLER_IDENTITY:-}" ]; then
  signed="$dist/notes/Olympium-${version}-signed.pkg"
  productsign --sign "$APPLE_INSTALLER_IDENTITY" "$pkg" "$signed"
  mv "$signed" "$pkg"
  pkg_note="The .pkg was signed with APPLE_INSTALLER_IDENTITY."
fi

if [ -n "${APPLE_NOTARY_APPLE_ID:-}" ] && [ -n "${APPLE_NOTARY_PASSWORD:-}" ] && [ -n "${APPLE_NOTARY_TEAM_ID:-}" ]; then
  xcrun notarytool submit "$pkg" \
    --apple-id "$APPLE_NOTARY_APPLE_ID" \
    --password "$APPLE_NOTARY_PASSWORD" \
    --team-id "$APPLE_NOTARY_TEAM_ID" \
    --wait
  pkg_note="$pkg_note Notarization was submitted because the notary secrets were present."
else
  pkg_note="$pkg_note Notarization was skipped because the notary secrets were not set."
fi

cat > "$dist/notes/SIGNING.txt" <<EOF
$signing_note
$pkg_note
EOF

cat > "$dist/notes/update.json" <<EOF
{
  "product": "Olympium",
  "version": "$version",
  "package": "Olympium-${version}.pkg",
  "notes": "Produced by GitHub Actions as a workflow artifact. It does not replace https://olympium.net/download/v0.1.0/Linkspan-0.1.0.pkg and it is not a GitHub Release. Slim guest images are not inside the package. The deb+zip polyglot is a separate experiment and is not this package."
}
EOF

cp "$dist/notes/SIGNING.txt" "$dist/notes/update.json" "$pkg" dist/ 2>/dev/null || true
mkdir -p dist
cp "$pkg" "dist/Olympium-${version}.pkg"
cp "$dist/notes/SIGNING.txt" dist/SIGNING.txt
cp "$dist/notes/update.json" dist/update.json
echo "Wrote dist/Olympium-${version}.pkg"
