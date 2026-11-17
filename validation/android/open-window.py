#!/usr/bin/env python3
"""Experimental Android 10 app window; requires a running, selected test device.

This starts scrcpy, waits for its virtual display, launches the resolved APK
entry point, and moves a redirected app task back onto that display. Window
placement is readiness only: verify pixels and host input separately.
"""
import argparse
import json
import os
from pathlib import Path
import re
import signal
import subprocess
import time


def app_stack(text, package):
    """Return the unique app-owned standard stack, never a shared/home stack."""
    found = []
    display = None
    for line in text.splitlines():
        match = re.match(r'Display #(\d+)', line)
        if match:
            display = int(match[1])
        match = re.match(r'  Stack #(\d+): type=standard\b', line)
        if match:
            found.append([display, int(match[1]), []])
        elif re.match(r'  Stack #', line):
            found.append([display, None, []])
        elif found:
            found[-1][2].append(line)
    candidates = []
    for display, stack, lines in found:
        block = '\n'.join(lines)
        if stack is None or not re.search(
                r'mResumedActivity:.*\bu\d+ ' + re.escape(package) + '/', block):
            continue
        # Android 10's move-stack moves all tasks. Do not move another app.
        tasks = re.findall(r'^    \* TaskRecord\{.*$', block, re.M)
        if len(tasks) != 1 or not re.search(r'\bA=' + re.escape(package) + r'(?:\s|})', tasks[0]):
            raise RuntimeError('Refusing to move a stack containing other tasks')
        candidates.append((display, stack))
    if len(candidates) > 1:
        raise RuntimeError('Ambiguous application stack')
    return candidates[0] if candidates else None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--adb', required=True)
    parser.add_argument('--scrcpy', required=True)
    parser.add_argument('--serial', required=True)
    parser.add_argument('--package', required=True)
    parser.add_argument('--title', default='Android application')
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    if not re.fullmatch(r'[A-Za-z][A-Za-z0-9_]*(?:\.[A-Za-z0-9_]+)+', args.package):
        parser.error('Invalid package identifier')
    args.output.mkdir(parents=True, exist_ok=True)
    adb = [args.adb, '-s', args.serial]
    def shell(*command, timeout=60):
        return subprocess.run(adb + ['shell', *command], check=True,
                              capture_output=True, text=True, timeout=timeout).stdout
    api = shell('getprop', 'ro.build.version.sdk').strip()
    if api != '29':
        raise RuntimeError('This experimental task-placement path is validated only on API 29')
    resolved = shell('cmd', 'package', 'resolve-activity', '--brief', '-a',
                     'android.intent.action.MAIN', '-c',
                     'android.intent.category.LAUNCHER', '-p', args.package)
    components = [s for s in resolved.splitlines() if re.fullmatch(
        re.escape(args.package) + r'/[A-Za-z0-9_.$]+', s)]
    if len(components) != 1:
        raise RuntimeError('No unique launcher activity')
    record = dict(package=args.package, serial=args.serial, android_api=api,
                  component=components[0], status='starting', desktop_input_verified=False)
    report = args.output / 'window.json'
    def save():
        report.write_text(json.dumps(record, indent=2) + '\n')
    save()
    env = dict(os.environ, ADB=args.adb, SDL_APP_ID=args.package)
    bridge = None
    def terminate(signum, frame):
        raise SystemExit(128 + signum)
    previous_term = signal.signal(signal.SIGTERM, terminate)
    try:
        with (args.output / 'scrcpy.log').open('w') as log:
            bridge = subprocess.Popen([args.scrcpy, '-s', args.serial,
                '--new-display=360x640/160', '--no-vd-system-decorations',
                '--window-title=' + args.title, '--no-audio', '--max-fps=15',
                '--render-driver=software'], env=env, stdout=log, stderr=log)
            deadline = time.monotonic() + 240
            while time.monotonic() < deadline:
                if bridge.poll() is not None:
                    raise RuntimeError('scrcpy exited before creating its display')
                match = re.search(r'New display:.*\(id=(\d+)\)',
                                  (args.output / 'scrcpy.log').read_text())
                if match:
                    display = int(match[1])
                    break
                time.sleep(1)
            else:
                raise RuntimeError('Virtual display readiness timed out')
            record['display_id'] = display
            launched = shell('am', 'start', '-W', '--display', str(display),
                '-a', 'android.intent.action.MAIN', '-c',
                'android.intent.category.LAUNCHER', '-n', components[0], timeout=180)
            (args.output / 'launch.txt').write_text(launched)
            if 'Error:' in launched or 'SecurityException' in launched:
                raise RuntimeError('Activity launch rejected')
            # A splash activity may start its main activity on display 0.
            # Never assume that the launcher activity's display was retained.
            deadline = time.monotonic() + 180
            while time.monotonic() < deadline:
                if bridge.poll() is not None:
                    raise RuntimeError('scrcpy exited during app launch')
                activities = shell('dumpsys', 'activity', 'activities')
                stack = app_stack(activities, args.package)
                if stack and stack[0] != display:
                    shell('am', 'display', 'move-stack', str(stack[1]), str(display))
                elif stack:
                    (args.output / 'activities.txt').write_text(activities)
                    record.update(status='task-on-app-display', stack_id=stack[1])
                    save()
                    print('App task is on its virtual display; verify rendering and host input.', flush=True)
                    break
                time.sleep(2)
            else:
                raise RuntimeError('App task did not remain on its own display')
            code = bridge.wait()
            if code:
                raise RuntimeError('scrcpy exited with status ' + str(code))
            record['status'] = 'closed'
    except BaseException as error:
        record.update(status='failed', error=str(error) or type(error).__name__)
        raise
    finally:
        if bridge and bridge.poll() is None:
            bridge.terminate()
            try:
                bridge.wait(timeout=15)
            except subprocess.TimeoutExpired:
                bridge.kill()
                bridge.wait()
        save()
        signal.signal(signal.SIGTERM, previous_term)


if __name__ == '__main__':
    main()
