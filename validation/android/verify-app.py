#!/usr/bin/env python3
"""Launch a real Android APK and record foreground/UI/screenshot evidence.

Run against an explicitly selected test device. This verifies a visible app;
calculation, desktop integration and native performance are separate checks.
"""
import argparse
import hashlib
import io
import json
from pathlib import Path
import re
import subprocess
import time
import xml.etree.ElementTree as ET

from PIL import Image


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--adb', required=True)
    parser.add_argument('--serial', required=True)
    parser.add_argument('--container', help='Run the ADB client in this Docker container')
    parser.add_argument('--package', required=True)
    parser.add_argument('--cold-start', action='store_true', help='Force-stop this test app before launch')
    parser.add_argument('--apk', type=Path, help='Install this APK before launch (same path in container)')
    parser.add_argument('--apk-sha256', help='Required expected digest when installing an APK')
    parser.add_argument('--fossify-calculation', action='store_true',
                        help='Verify 2+3=5 using Fossify Calculator UI controls')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--boot-timeout', type=int, default=600)
    parser.add_argument('--unlock', action='store_true',
                        help='Wake/dismiss an unprotected test-device keyguard before launch')
    args = parser.parse_args()
    if not re.fullmatch(r'[A-Za-z][A-Za-z0-9_]*(?:\.[A-Za-z0-9_]+)+', args.package):
        parser.error('Invalid Android package identifier')
    if args.fossify_calculation and args.package != 'org.fossify.math':
        parser.error('--fossify-calculation requires org.fossify.math')
    if (args.apk and not args.apk_sha256) or (args.apk_sha256 and not re.fullmatch(r'[0-9a-f]{64}', args.apk_sha256)):
        parser.error('--apk requires an expected --apk-sha256')
    if args.apk and hashlib.sha256(args.apk.read_bytes()).hexdigest() != args.apk_sha256:
        parser.error('APK checksum mismatch; APK will not be installed')
    args.output.mkdir(parents=True, exist_ok=True)
    base = (['docker', 'exec', args.container] if args.container else [])
    base += [args.adb, '-s', args.serial]
    record = {'package': args.package, 'serial': args.serial, 'status': 'failed',
              'calculation_verified': False, 'desktop_window_verified': False}
    (args.output / 'result.json').write_text(json.dumps(dict(record, status='running'), indent=2) + '\n')
    events = []

    def run(*command, timeout=30, check=True):
        p = subprocess.run(base + list(command), capture_output=True, timeout=timeout)
        events.append({'command': list(command), 'exit_code': p.returncode,
                       'stdout_bytes': len(p.stdout),
                       'stderr': p.stderr.decode(errors='replace')[-4000:]})
        if check and p.returncode:
            raise RuntimeError(f'{command}: {p.stderr.decode(errors="replace")} '
                               f'{p.stdout.decode(errors="replace")[:2000]}')
        return p.stdout

    try:
        deadline = time.monotonic() + args.boot_timeout
        print('Waiting for boot completion and package manager', flush=True)
        while time.monotonic() < deadline:
            try:
                boot = run('shell', 'getprop', 'sys.boot_completed', timeout=15)
                if boot.strip() == b'1':
                    service = run('shell', 'service', 'check', 'package', timeout=30, check=False)
                    if b'found' in service and b'not found' not in service:
                        break
            except (RuntimeError, subprocess.TimeoutExpired):
                pass
            time.sleep(5)
        else:
            raise RuntimeError('Boot/package readiness timed out; app was not verified')
        if args.apk:
            print('Installing checksum-verified APK', flush=True)
            installed = run('install', '--no-streaming', '-r', str(args.apk), timeout=600)
            (args.output / 'install.txt').write_bytes(installed)
            if b'Success' not in installed:
                raise RuntimeError('APK installation did not return Success')
        if args.apk_sha256:
            print('Checking installed APK bytes', flush=True)
            paths = run('shell', 'pm', 'path', args.package, timeout=60).decode().splitlines()
            base_paths = [line[8:] for line in paths if line.startswith('package:') and line.endswith('/base.apk')]
            if len(base_paths) != 1 or not re.fullmatch(r'/[A-Za-z0-9_./+=@~-]+\.apk', base_paths[0]):
                raise RuntimeError('No unique installed base APK for the requested package')
            remote_digest = run('shell', 'sha256sum', base_paths[0], timeout=60).decode().split()[0]
            if remote_digest != args.apk_sha256:
                raise RuntimeError('Installed package APK differs from the verified download')
            record['installed_apk_sha256'] = remote_digest
        record['android_api'] = run('shell', 'getprop', 'ro.build.version.sdk').decode().strip()
        record['abi'] = run('shell', 'getprop', 'ro.product.cpu.abi').decode().strip()
        identity = run('shell', 'dumpsys', 'package', args.package, timeout=60)
        (args.output / 'package.txt').write_bytes(identity)
        resolved = run('shell', 'cmd', 'package', 'resolve-activity', '--brief',
                       '-a', 'android.intent.action.MAIN', '-c',
                       'android.intent.category.LAUNCHER', '-p', args.package, timeout=60)
        components = [line.strip() for line in resolved.decode().splitlines()
                      if re.fullmatch(re.escape(args.package) + r'/[A-Za-z0-9_.$]+', line.strip())]
        if len(components) != 1:
            raise RuntimeError('No unique launchable activity: ' + resolved.decode())
        record['component'] = components[0]
        if args.unlock:
            run('shell', 'input', 'keyevent', 'KEYCODE_WAKEUP', timeout=120)
            run('shell', 'wm', 'dismiss-keyguard', timeout=120)
        if args.cold_start:
            run('shell', 'am', 'force-stop', args.package, timeout=60)
        record['cold_start'] = args.cold_start
        print('Launching ' + components[0], flush=True)
        launch = run('shell', 'am', 'start', '-W', '-a', 'android.intent.action.MAIN',
                     '-c', 'android.intent.category.LAUNCHER', '-n', components[0], timeout=180)
        (args.output / 'launch.txt').write_bytes(launch)
        if b'Status: ok' not in launch or b'Error' in launch:
            raise RuntimeError('Activity launch did not return Status: ok')
        deadline = time.monotonic() + 120
        while time.monotonic() < deadline:
            windows = run('shell', 'dumpsys', 'window', 'windows', timeout=45)
            lines = [line for line in windows.decode(errors='replace').splitlines()
                     if 'mCurrentFocus=' in line]
            if any(args.package + '/' in line and 'Not Responding' not in line for line in lines):
                break
            time.sleep(3)
        else:
            raise RuntimeError('The requested app did not own the foreground window')
        (args.output / 'windows.txt').write_bytes(windows)
        def ui():
            # UiAutomator can return exit 0 with no root node during an
            # accessibility transition. Restart the client, never reuse XML.
            for attempt in range(3):
                run('shell', 'rm', '-f', '/sdcard/olympium-validation-ui.xml', timeout=30)
                dump = run('shell', 'uiautomator', 'dump', '/sdcard/olympium-validation-ui.xml', timeout=90)
                error = events[-1]['stderr']
                if 'null root node' in error:
                    events[-1]['retry_reason'] = 'accessibility root unavailable'
                    time.sleep(3)
                    continue
                if b'ERROR' in dump or b'Exception' in dump or 'ERROR' in error:
                    raise RuntimeError('UI dump failed: ' + dump.decode(errors='replace') + error)
                data = run('shell', 'cat', '/sdcard/olympium-validation-ui.xml', timeout=45)
                return data, ET.fromstring(data)
            raise RuntimeError('No fresh accessibility root after three attempts')

        xml, tree = ui()
        if not any(node.attrib.get('package') == args.package for node in tree.iter('node')):
            raise RuntimeError('UI hierarchy did not contain the requested application')
        (args.output / 'ui-before.xml').write_bytes(xml)
        if args.fossify_calculation:
            nodes = {node.attrib.get('resource-id'): node.attrib for node in tree.iter('node')}
            for button in ['btn_clear', 'btn_2', 'btn_plus', 'btn_3', 'btn_equals']:
                node = nodes.get(args.package + ':id/' + button)
                if not node or node.get('clickable') != 'true':
                    raise RuntimeError('Missing calculator control: ' + button)
                bounds = re.fullmatch(r'\[(\d+),(\d+)\]\[(\d+),(\d+)\]', node.get('bounds', ''))
                if not bounds:
                    raise RuntimeError('Invalid calculator control bounds')
                x1, y1, x2, y2 = map(int, bounds.groups())
                run('shell', 'input', 'tap', str((x1+x2)//2), str((y1+y2)//2), timeout=120)
                if button == 'btn_clear':
                    reset_xml, reset_tree = ui()
                    reset = {n.attrib.get('resource-id'): n.attrib.get('text', '')
                             for n in reset_tree.iter('node')}
                    if reset.get(args.package + ':id/formula') not in ('', '0') or reset.get(args.package + ':id/result') not in ('', '0'):
                        raise RuntimeError('Clear did not reset the calculation; stale results cannot pass')
                    (args.output / 'ui-cleared.xml').write_bytes(reset_xml)
                    record['clear_verified'] = True
            xml, tree = ui()
            values = {node.attrib.get('resource-id'): node.attrib.get('text', '')
                      for node in tree.iter('node')}
            formula = values.get(args.package + ':id/formula', '').replace(' ', '')
            result = values.get(args.package + ':id/result', '')
            if formula != '2+3' or result != '5':
                raise RuntimeError(f'Unexpected calculation: {formula!r} = {result!r}')
            record.update(calculation_verified=True, formula=formula, result=result)
        (args.output / 'ui.xml').write_bytes(xml)
        png = run('exec-out', 'screencap', '-p', timeout=90)
        image = Image.open(io.BytesIO(png)).convert('RGB')
        colors = image.resize((90, 160)).getcolors(maxcolors=90*160)
        if colors is not None and len(colors) < 16:
            raise RuntimeError('Screenshot is blank or lacks meaningful app content')
        (args.output / 'screenshot.png').write_bytes(png)
        record.update(status='app-window-verified', foreground_verified=True,
                      ui_hierarchy_verified=True, screenshot_sha256=hashlib.sha256(png).hexdigest(),
                      screenshot_size=list(image.size))
        print('App foreground, UI hierarchy and rendered screenshot verified', flush=True)
    except Exception as error:
        record['error'] = str(error)
        raise
    finally:
        (args.output / 'result.json').write_text(json.dumps(record, indent=2) + '\n')
        (args.output / 'commands.json').write_text(json.dumps(events, indent=2) + '\n')


if __name__ == '__main__':
    main()
