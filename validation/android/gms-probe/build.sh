#!/bin/sh
set -eu
source_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
sdk=$(realpath "${1:?usage: build.sh PINNED_SDK OUTPUT_DIRECTORY}")
mkdir -p "${2:?usage: build.sh PINNED_SDK OUTPUT_DIRECTORY}"
out=$(realpath "$2")
tools=$sdk/build-tools/37.0.0
platform=$sdk/platforms/android-37.0/android.jar
mkdir -p "$out/classes" "$out/dex"
javac --release 8 -classpath "$platform" -d "$out/classes" "$source_dir/Probe.java"
jar --create --file "$out/classes.jar" -C "$out/classes" .
"$tools/d8" --lib "$platform" --min-api 28 --output "$out/dex" "$out/classes.jar"
"$tools/aapt2" link -I "$platform" --manifest "$source_dir/AndroidManifest.xml" -o "$out/unsigned.apk"
python3 - "$out" <<'PY'
from pathlib import Path
import sys, zipfile
out = Path(sys.argv[1])
with zipfile.ZipFile(out / 'unsigned.apk', 'a', compression=zipfile.ZIP_DEFLATED) as apk:
    apk.write(out / 'dex/classes.dex', 'classes.dex')
PY
"$tools/zipalign" -f 4 "$out/unsigned.apk" "$out/aligned.apk"
if [ ! -f "$out/debug.keystore" ]; then
    (umask 077; keytool -genkeypair -keystore "$out/debug.keystore" -alias androiddebugkey \
        -storepass android -keypass android -keyalg RSA -keysize 2048 -validity 3650 \
        -dname 'CN=Olympium Validation' > "$out/keytool.log" 2>&1)
fi
"$tools/apksigner" sign --ks "$out/debug.keystore" --ks-pass pass:android \
    --key-pass pass:android --out "$out/gms-probe.apk" "$out/aligned.apk"
"$tools/apksigner" verify --verbose "$out/gms-probe.apk"
sha256sum "$source_dir/Probe.java" "$source_dir/AndroidManifest.xml" "$out/gms-probe.apk"
