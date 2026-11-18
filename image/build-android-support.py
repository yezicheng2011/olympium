#!/usr/bin/env python3
"""Build small Android support assets using the pinned API 37 SDK (not a device)."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import zipfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--sdk', type=Path, help='Already prepared pinned SDK; otherwise download verified build tools')
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
repo = Path(__file__).resolve().parents[1]
source = repo / 'image/android-compat'
if args.sdk:
    sdk = args.sdk.resolve(strict=True)
else:
    from android_runtime import download, extract
    sdk = repo / 'build/android-support-sdk'
    sdk.mkdir(parents=True, exist_ok=True)
    cache = sdk / 'downloads'
    cache.mkdir(exist_ok=True)
    pins = repo / 'validation/android/gms-probe/build-tools.json'
    for record in json.loads(pins.read_text())['artifacts']:
        archive = download(record, cache)
        # Re-extract the checked archive; an existing directory alone is not
        # evidence of toolchain integrity.
        destination = sdk / record['destination']
        if destination.exists():
            shutil.rmtree(destination)
        extract(archive, record, sdk)
tools = sdk / 'build-tools/37.0.0'
platform = sdk / 'platforms/android-37.0/android.jar'
out = args.output.resolve()
out.mkdir(parents=True, exist_ok=True)


def run(*command):
    subprocess.run(list(map(str, command)), check=True)


with tempfile.TemporaryDirectory(prefix='android-support-') as temporary:
    work = Path(temporary)
    for name in ('classes', 'dex', 'health-classes', 'health-dex'):
        (work / name).mkdir()
    run('javac', '--release', '8', '-classpath', platform, '-d', work / 'classes',
        *sorted(source.glob('*.java')))
    run('jar', '--create', '--file', work / 'helper.jar', '-C', work / 'classes', '.')
    run(tools / 'd8', '--lib', platform, '--min-api', '37', '--output', work / 'dex', work / 'helper.jar')
    shutil.copyfile(work / 'dex/classes.dex', out / 'helper.dex')
    # A static native helper runs only as root inside our disposable AOSP VM.
    run('musl-gcc', '-static', '-s', '-O2', '-Wall', '-Wextra', '-Werror', source / 'remount-ro.c', '-o', out / 'mount-helper')
    probe = repo / 'validation/android/gms-probe'
    java = (probe / 'Probe.java').read_text().replace('org.olympium.validation.gms', 'org.olympium.android.health')
    (work / 'Probe.java').write_text(java)
    manifest = (probe / 'AndroidManifest.xml').read_text().replace('org.olympium.validation.gms', 'org.olympium.android.health')
    start, end = manifest.index('            <intent-filter>'), manifest.index('            </intent-filter>')
    manifest = manifest[:start] + manifest[end + len('            </intent-filter>'):]
    manifest = manifest.replace('GMS compatibility probe', 'Android Services')
    (work / 'AndroidManifest.xml').write_text(manifest)
    run('javac', '--release', '8', '-classpath', platform, '-d', work / 'health-classes', work / 'Probe.java')
    run('jar', '--create', '--file', work / 'health.jar', '-C', work / 'health-classes', '.')
    run(tools / 'd8', '--lib', platform, '--min-api', '37', '--output', work / 'health-dex', work / 'health.jar')
    run(tools / 'aapt2', 'link', '-I', platform, '--manifest', work / 'AndroidManifest.xml', '-o', work / 'unsigned.apk')
    with zipfile.ZipFile(work / 'unsigned.apk', 'a', compression=zipfile.ZIP_DEFLATED) as apk:
        apk.write(work / 'health-dex/classes.dex', 'classes.dex')
    run(tools / 'zipalign', '-f', '4', work / 'unsigned.apk', work / 'aligned.apk')
    # Private disposable build key is never staged or committed. Installations
    # pin the entire resulting APK; a rebuilt probe uses a new package install.
    run('keytool', '-genkeypair', '-keystore', work / 'key', '-alias', 'health',
        '-storepass', 'android', '-keypass', 'android', '-keyalg', 'RSA', '-keysize', '2048',
        '-validity', '3650', '-dname', 'CN=Olympium Android Services')
    run(tools / 'apksigner', 'sign', '--ks', work / 'key', '--ks-pass', 'pass:android',
        '--key-pass', 'pass:android', '--out', out / 'health.apk', work / 'aligned.apk')
    run(tools / 'apksigner', 'verify', out / 'health.apk')
for name in ('profile.json', 'LICENSE'):
    shutil.copyfile(source / name, out / name)
shutil.copyfile('/usr/share/doc/musl/copyright', out / 'musl-LICENSE')
files = ('helper.dex', 'mount-helper', 'health.apk', 'profile.json')
manifest = {name: hashlib.sha256((out / name).read_bytes()).hexdigest() for name in files}
(out / 'assets.json').write_text(json.dumps(manifest, indent=2) + '\n')
print(json.dumps(manifest, indent=2))
