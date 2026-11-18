#!/usr/bin/env python3
"""Actual managed APK signing, data retention and platform rollback acceptance."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import xml.etree.ElementTree as ET
import zipfile

repo = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(repo / 'image'))
from android_framework import digest, owned_runtime

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--work', type=Path, required=True, help='Existing successful managed-roundtrip work directory')
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
work, output = args.work.resolve(), args.output.resolve()
output.mkdir(parents=True, exist_ok=True)
build = work / 'update-fixtures'
build.mkdir(exist_ok=True)
sdk = repo / 'build/android-support-sdk'
tools = sdk / 'build-tools/37.0.0'
platform = sdk / 'platforms/android-37.0/android.jar'
package = 'org.olympium.validation.gms'
runtime, root = work / 'runtime', work / 'desktop'
adb = [str(runtime / 'platform-tools/adb'), '-s', 'emulator-5560']
env = dict(os.environ, OLYMPIUM_ANDROID_SDK=str(runtime),
           OLYMPIUM_ANDROID_COMPAT_ASSETS=str(repo / 'build/android-support-assets'))
for name in ('OLYMPIUM_ANDROID_SERIAL', 'OLYMPIUM_ANDROID_ADB', 'OLYMPIUM_ANDROID_ALLOW_TCG',
             'OLYMPIUM_ANDROID_TCG_ICOUNT', 'OLYMPIUM_ANDROID_LEGACY_WIFI'):
    env.pop(name, None)
record = dict(status='failed', signed_update_verified=False, data_retained=False,
              wrong_signer_rejected=False, apk_rollback_verified=False, private_data_rollback_verified=False)


def run(label, words, check=True, timeout=300):
    result = subprocess.run(list(map(str, words)), env=env, capture_output=True, timeout=timeout)
    (output / (label + '.stdout.log')).write_bytes(result.stdout)
    (output / (label + '.stderr.log')).write_bytes(result.stderr)
    if check and result.returncode:
        raise RuntimeError(label + ' failed; inspect the captured logs')
    return result


def apk(version, key, name):
    stage = build / name
    stage.mkdir(exist_ok=True)
    ET.register_namespace('android', 'http://schemas.android.com/apk/res/android')
    tree = ET.parse(repo / 'validation/android/gms-probe/AndroidManifest.xml')
    tree.getroot().set('{http://schemas.android.com/apk/res/android}versionCode', str(version))
    tree.getroot().set('{http://schemas.android.com/apk/res/android}versionName', str(version))
    tree.write(stage / 'AndroidManifest.xml', encoding='utf-8', xml_declaration=True)
    run(name + '-resources', [tools / 'aapt2', 'link', '-I', platform, '--manifest',
                              stage / 'AndroidManifest.xml', '-o', stage / 'unsigned.apk'])
    with zipfile.ZipFile(stage / 'unsigned.apk', 'a', compression=zipfile.ZIP_DEFLATED) as archive:
        archive.write(work / 'probe-build/dex/classes.dex', 'classes.dex')
    run(name + '-align', [tools / 'zipalign', '-f', '4', stage / 'unsigned.apk', stage / 'aligned.apk'])
    signed = stage / 'probe.apk'
    run(name + '-sign', [tools / 'apksigner', 'sign', '--ks', key, '--ks-pass', 'pass:android',
                        '--key-pass', 'pass:android', '--out', signed, stage / 'aligned.apk'])
    run(name + '-verify', [tools / 'apksigner', 'verify', '--verbose', '--print-certs', signed])
    return signed


def backend(label, command, value, check=True):
    return run(label, [sys.executable, repo / 'image/olympium-android', command, value,
                       '--root', root], check, 1200)


def marker(label):
    return run(label, adb + ['shell', 'run-as', package, 'cat', 'files/update-marker']).stdout.decode().strip()


try:
    if not os.access('/dev/kvm', os.R_OK | os.W_OK):
        raise RuntimeError('This acceptance requires KVM')
    original_key = work / 'probe-build/debug.keystore'
    wrong_key = build / 'wrong.keystore'
    run('new-untrusted-test-key', ['keytool', '-genkeypair', '-keystore', wrong_key,
        '-alias', 'androiddebugkey', '-storepass', 'android', '-keypass', 'android',
        '-keyalg', 'RSA', '-keysize', '2048', '-validity', '3650', '-dname', 'CN=Untrusted update fixture'])
    first = apk(1, original_key, 'v1')
    second = apk(2, original_key, 'v2')
    wrong = apk(3, wrong_key, 'wrong-signer')
    record['apk_sha256'] = dict(v1=digest(first), v2=digest(second), wrong_signer=digest(wrong))
    backend('install-v1', 'install', first)
    uid = run('uid-before', adb + ['shell', 'run-as', package, 'id', '-u']).stdout.decode().strip()
    if not uid.isdecimal() or int(uid) < 10000:
        raise RuntimeError('Fixture did not have an ordinary app UID')
    run('seed-private-state', adb + ['shell', "run-as " + package + " sh -c 'mkdir -p files; printf before-update > files/update-marker'"])
    backend('update-v2', 'update', second)
    state = json.loads((root / 'android/packages' / package / 'installed.json').read_text())
    if state['sha'] != digest(second) or state['version_code'] != 2:
        raise RuntimeError('Managed updater did not retain version 2 identity')
    record['signed_update_verified'] = True
    if marker('state-after-update') != 'before-update':
        raise RuntimeError('Update lost the existing private app data')
    record['data_retained'] = True
    run('change-new-version-state', adb + ['shell', "run-as " + package + " sh -c 'printf after-update > files/update-marker'"])
    rejected = backend('reject-wrong-signer', 'update', wrong, check=False)
    text = (rejected.stdout + rejected.stderr).decode(errors='replace')
    if rejected.returncode == 0 or 'INSTALL_FAILED_UPDATE_INCOMPATIBLE' not in text:
        raise RuntimeError('Wrong signer was not rejected by Android')
    if json.loads((root / 'android/packages' / package / 'installed.json').read_text())['sha'] != digest(second):
        raise RuntimeError('Rejected update changed the managed receipt')
    if marker('state-after-rejection') != 'after-update':
        raise RuntimeError('Rejected update changed private app data')
    record['wrong_signer_rejected'] = True
    backend('rollback-v1', 'rollback', package)
    state = json.loads((root / 'android/packages' / package / 'installed.json').read_text())
    if state['sha'] != digest(first) or state['version_code'] != 1:
        raise RuntimeError('Platform rollback did not restore version 1')
    record['apk_rollback_verified'] = True
    if marker('state-after-rollback') != 'before-update':
        raise RuntimeError('Platform rollback did not restore its private-data snapshot')
    uid_after = run('uid-after', adb + ['shell', 'run-as', package, 'id', '-u']).stdout.decode().strip()
    if uid != uid_after:
        raise RuntimeError('Application UID changed across update/rollback')
    record.update(status='passed', private_data_rollback_verified=True, app_uid=int(uid))
except Exception as error:
    record['error'] = str(error)
    raise
finally:
    (output / 'result.json').write_text(json.dumps(record, indent=2) + '\n')
    try:
        owned_runtime(root, runtime, 'emulator-5560')
        run('stop-owned-vm', adb + ['emu', 'kill'], timeout=30)
    except (OSError, RuntimeError, ValueError, subprocess.TimeoutExpired):
        pass
