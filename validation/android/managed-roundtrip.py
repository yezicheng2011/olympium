#!/usr/bin/env python3
"""Real KVM acceptance: managed install, ordinary client, cold boot and repeat."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time

repo = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(repo / 'image'))
from android_framework import digest, owned_runtime, process_identity
from android_runtime import download

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--work', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
work = args.work.resolve()
output = args.output.resolve()
work.mkdir(parents=True, exist_ok=True)
output.mkdir(parents=True, exist_ok=True)
root, runtime = work / 'desktop', work / 'runtime'
env = dict(os.environ, OLYMPIUM_ANDROID_SDK=str(runtime),
           OLYMPIUM_ANDROID_COMPAT_ASSETS=str(repo / 'build/android-support-assets'))
for name in ('OLYMPIUM_ANDROID_SERIAL', 'OLYMPIUM_ANDROID_ADB', 'OLYMPIUM_ANDROID_ALLOW_TCG',
             'OLYMPIUM_ANDROID_TCG_ICOUNT', 'OLYMPIUM_ANDROID_LEGACY_WIFI',
             'OLYMPIUM_ANDROID_READY_TIMEOUT', 'OLYMPIUM_ANDROID_TOOL_TIMEOUT'):
    env.pop(name, None)
adb = runtime / 'platform-tools/adb'
record = dict(status='failed', kvm_access=os.access('/dev/kvm', os.R_OK | os.W_OK),
              automatic_microg_bootstrap_verified=False, cold_boot_recovery_verified=False,
              gms_api_compatibility_verified=False, desktop_window_verified=False, rounds=[])
record['kvm_checks'] = []


def run(label, command, timeout=300):
    with (output / (label + '.stdout.log')).open('wb') as stdout, \
            (output / (label + '.stderr.log')).open('wb') as stderr:
        result = subprocess.run(list(map(str, command)), env=env, cwd=repo,
                                stdout=stdout, stderr=stderr, timeout=timeout)
    if result.returncode:
        raise RuntimeError(label + ' failed; inspect its captured logs')


def stop_owned():
    identity = owned_runtime(root, runtime, 'emulator-5560')
    run('stop-' + str(identity['pid']), [adb, '-s', 'emulator-5560', 'emu', 'kill'], 30)
    deadline = time.monotonic() + 30
    while time.monotonic() < deadline:
        try:
            current = process_identity(identity['pid'])
            stat = (Path('/proc') / str(identity['pid']) / 'stat').read_text()
            if current != identity or stat[stat.rfind(')') + 2:].split()[0] == 'Z':
                return
        except (OSError, RuntimeError):
            return
        time.sleep(.2)
    raise RuntimeError('Owned emulator did not stop before the cold-boot deadline')


try:
    if not record['kvm_access']:
        raise RuntimeError('Hardware acceptance requires accessible /dev/kvm')
    pins = json.loads((repo / 'validation/runtime-artifacts.json').read_text())['artifacts']
    sample = next(pin for pin in pins if pin['path'].startswith('Fossify Calculator 1.4.0 '))
    cache = work / 'downloads'
    cache.mkdir(exist_ok=True)
    apk = work / 'calculator.apk'
    shutil.copyfile(download(sample, cache), apk)
    sdk = repo / 'build/android-support-sdk'
    probe_build = work / 'probe-build'
    run('build-independent-client', ['sh', repo / 'validation/android/gms-probe/build.sh', sdk, probe_build])
    for number in (1, 2):
        label = 'round-' + str(number)
        device = Path('/dev/kvm')
        details = dict(round=number, access=os.access(device, os.R_OK | os.W_OK),
                       process_gid=os.getgid(), groups=os.getgroups())
        if device.exists():
            stat = device.stat()
            details.update(mode=oct(stat.st_mode & 0o777), owner=stat.st_uid, group=stat.st_gid)
        record['kvm_checks'].append(details)
        started = time.monotonic()
        run(label + '-install', [repo / 'image/olympium-android',
            'install', apk, '--root', root], 1200)
        install_seconds = round(time.monotonic() - started, 3)
        install = json.loads((output / (label + '-install.stdout.log')).read_text())
        if install.get('package') != 'org.fossify.math' or install.get('sha') != sample['checksum']:
            raise RuntimeError('Managed installer did not retain the expected application bytes')
        framework = json.loads((root / 'android/services/framework.json').read_text())
        if framework.get('basic_services_verified') is not True or \
                framework.get('gms_api_compatibility_verified') is not False:
            raise RuntimeError('Managed microG subset health check did not pass honestly')
        identity = owned_runtime(root, runtime, 'emulator-5560')
        words = (Path('/proc') / str(identity['pid']) / 'cmdline').read_bytes().split(b'\0')
        if b'-accel' not in words or words[words.index(b'-accel') + 1] != b'on' or b'-icount' in words:
            raise RuntimeError('Hardware acceptance did not use normal-clock KVM acceleration')
        (output / (label + '-framework.json')).write_text(json.dumps(framework, indent=2) + '\n')
        run(label + '-calculator', [sys.executable, repo / 'validation/android/verify-app.py',
            '--adb', adb, '--serial', 'emulator-5560', '--package', 'org.fossify.math',
            '--apk-sha256', sample['checksum'], '--cold-start', '--unlock',
            '--fossify-calculation', '--output', output / (label + '-calculator')], 300)
        run(label + '-independent-client', [sys.executable, repo / 'validation/android/gms-probe/verify.py',
            '--adb', adb, '--serial', 'emulator-5560', '--apk', probe_build / 'gms-probe.apk',
            '--apk-sha256', digest(probe_build / 'gms-probe.apk'),
            '--output', output / (label + '-independent-client')], 300)
        probe = json.loads((output / (label + '-independent-client/result.json')).read_text())
        if probe.get('checks_passed') is not True:
            raise RuntimeError('Independent ordinary-UID GMS client failed')
        record['rounds'].append(dict(process=identity, managed_install_seconds=install_seconds,
                                     round_seconds=round(time.monotonic() - started, 3),
                                     framework_sha256=framework['framework_sha256'],
                                     basic_services_verified=True, calculator_input_verified=True))
        stop_owned()
    if record['rounds'][0]['process'] == record['rounds'][1]['process']:
        raise RuntimeError('Cold-boot verification reused the original emulator process')
    record.update(status='passed', automatic_microg_bootstrap_verified=True, cold_boot_recovery_verified=True)
except Exception as error:
    record['error'] = str(error)
    raise
finally:
    (output / 'result.json').write_text(json.dumps(record, indent=2) + '\n')
    diagnostic = root / 'android/services/framework-launch-failure.json'
    if diagnostic.exists():
        shutil.copyfile(diagnostic, output / diagnostic.name)
    # Upload only reviewed reports, never AVD data, ADB keys, signing keys or dumps.
    log = root / 'android/avd/emulator.log'
    if log.exists():
        lines = [line for line in log.read_text(errors='replace').splitlines()
                 if any(word in line for word in ('FATAL', 'Boot completed', 'ERROR'))]
        (output / 'emulator-events.log').write_text('\n'.join(lines) + '\n')
    try:
        stop_owned()
    except (OSError, RuntimeError, ValueError, subprocess.TimeoutExpired):
        pass
