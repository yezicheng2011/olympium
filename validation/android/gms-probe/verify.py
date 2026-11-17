#!/usr/bin/env python3
"""Execute a checksum-identified independent APK against pinned microG packages."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import time

PACKAGE = 'org.olympium.validation.gms'
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--adb', required=True)
parser.add_argument('--container')
parser.add_argument('--serial', required=True)
parser.add_argument('--apk', type=Path, required=True,
                    help='Same absolute path must be visible in the ADB container, if selected')
parser.add_argument('--apk-sha256', required=True)
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--timeout', type=int, default=240)
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=True)
record = dict(status='failed', android17_verified=False, gms_api_compatibility_verified=False)
adb = (['docker', 'exec', args.container] if args.container else []) + [args.adb, '-s', args.serial]


def run(*words, timeout=60, check=True):
    result = subprocess.run(adb + list(words), capture_output=True, timeout=timeout)
    text = result.stdout.decode(errors='replace')
    if len(result.stdout) + len(result.stderr) > 32 * 1024 * 1024:
        raise RuntimeError('ADB output exceeded the validation limit')
    if check and result.returncode:
        raise RuntimeError((text + result.stderr.decode(errors='replace'))[-3000:])
    return text


def installed_sha(package):
    files = run('shell', 'pm', 'path', package).splitlines()
    bases = [p[8:] for p in files if p.startswith('package:') and p.endswith('/base.apk')]
    if len(bases) != 1 or not re.fullmatch(r'/[A-Za-z0-9_./+=@~-]+\.apk', bases[0]):
        raise RuntimeError('No unique installed APK for ' + package)
    return run('shell', 'sha256sum', bases[0]).split()[0]


try:
    if not re.fullmatch(r'[0-9a-f]{64}', args.apk_sha256):
        raise RuntimeError('Expected SHA256 must contain 64 lower-case hex digits')
    apk = args.apk.resolve(strict=True)
    with apk.open('rb') as stream:
        actual = hashlib.file_digest(stream, 'sha256').hexdigest()
    if actual != args.apk_sha256:
        raise RuntimeError('Probe APK checksum mismatch')
    if not 1 <= args.timeout <= 3600:
        raise RuntimeError('Timeout must be between 1 and 3600 seconds')
    release = run('shell', 'getprop', 'ro.build.version.release').strip()
    api = int(run('shell', 'getprop', 'ro.build.version.sdk').strip())
    record.update(android_release=release, android_api=api, probe_apk_sha256=actual)
    if release != '17' or api != 37:
        raise RuntimeError('This acceptance check requires Android 17 / API 37')
    if run('shell', 'getprop', 'sys.boot_completed').strip() != '1':
        raise RuntimeError('Android boot has not completed')
    pins = json.loads((Path(__file__).resolve().parents[3] / 'image/android-runtime-pins.json').read_text())
    identities = {}
    for pin in pins['artifacts']:
        if pin.get('format') == 'apk':
            package = Path(pin['destination']).stem
            identities[package] = installed_sha(package)
            if identities[package] != pin['checksum']:
                raise RuntimeError('Installed package differs from pinned microG: ' + package)
    record['installed_microg_sha256'] = identities
    result = run('install', '--no-streaming', '-r', str(apk), timeout=600)
    if 'Success' not in result or installed_sha(PACKAGE) != actual:
        raise RuntimeError('Probe installation was not verified')
    # Only this independent test app's data is cleared, so stale output cannot pass.
    if 'Success' not in run('shell', 'pm', 'clear', PACKAGE):
        raise RuntimeError('Could not clear old probe results')
    run('shell', 'am', 'start', '-W', '-n', PACKAGE + '/.Probe', timeout=180)
    deadline = time.monotonic() + args.timeout
    while time.monotonic() < deadline:
        text = run('shell', 'run-as', PACKAGE, 'cat', 'files/result.json', check=False)
        try:
            result = json.loads(text)
            break
        except ValueError:
            time.sleep(2)
    else:
        raise RuntimeError('No fresh probe result before deadline')
    if result.get('caller_package') != PACKAGE or not isinstance(result.get('caller_uid'), int) \
            or result['caller_uid'] < 10000 or result.get('android_api') != 37:
        raise RuntimeError('Result did not come from the expected ordinary Android app identity')
    record['probe'] = result
    record['android17_verified'] = True
    checks = ['probe_completed', 'signature_compatibility_verified',
              'companion_signature_compatibility_verified', 'gms_service_handshake_verified']
    record['checks_passed'] = all(result.get(name) is True for name in checks)
    record['status'] = 'passed-probe' if record['checks_passed'] else 'failed-checks'
except (RuntimeError, OSError, ValueError, subprocess.TimeoutExpired) as error:
    record['error'] = str(error)
finally:
    (args.output / 'result.json').write_text(json.dumps(record, indent=2) + '\n')
print(json.dumps(record, indent=2))
raise SystemExit(0 if record.get('checks_passed') else 1)
