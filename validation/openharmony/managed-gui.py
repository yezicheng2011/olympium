#!/usr/bin/env python3
"""KVM-only GUI acceptance of the owned, unprotected OpenHarmony SDK image."""
import argparse
from decimal import Decimal, InvalidOperation
import hashlib
import json
import os
from pathlib import Path
import re
import socket
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'image'))
from android_runtime import sparse_copy

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--work', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--desktop-pointer', type=Path,
                    help='Built Wayland test pointer, enables host window acceptance')
parser.add_argument('--fresh-hap', action='store_true',
                    help='Build and verify first installation of a separate SDK test app')
a = parser.parse_args()
work, out = a.work.resolve(), a.output.resolve()
work.mkdir(parents=True, exist_ok=True)
out.mkdir(parents=True, exist_ok=True)
report = dict(kvm_verified=False, guest_app_gui_verified=False, input_verified=False,
              relaunch_verified=False, desktop_window_verified=False)
vm = None
qmp = None
serial = None
started = time.monotonic()
BUNDLE = 'ohos.samples.distributedcalc'
FRESH_BUNDLE = 'org.olympium.validation.hap'


def run(*args, timeout=180, check=True):
    result = subprocess.run(list(map(str, args)), text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, timeout=timeout)
    with (out / 'commands.log').open('a') as stream:
        stream.write('$ ' + ' '.join(map(str, args)) + '\n' + result.stdout + '\n')
    if check and result.returncode:
        raise RuntimeError(f'Command failed ({result.returncode}): {args[0]}')
    return result.stdout


def wait_for(fn, seconds, description):
    end = time.monotonic() + seconds
    last = ''
    while time.monotonic() < end:
        if vm and vm.poll() is not None:
            raise RuntimeError('SDK emulator exited unexpectedly')
        try:
            result = fn()
            if result:
                return result
        except (subprocess.SubprocessError, OSError, ValueError) as error:
            last = str(error)
        time.sleep(2)
    raise RuntimeError('Timed out: ' + description + ' ' + last)


def qcommand(name, arguments=None):
    qmp.write((json.dumps(dict(execute=name, arguments=arguments or {})) + '\n').encode())
    qmp.flush()
    while True:
        line = qmp.readline()
        if not line:
            raise RuntimeError('QMP connection closed')
        result = json.loads(line)
        if 'error' in result:
            raise RuntimeError(str(result['error']))
        if 'return' in result:
            return result['return']


def capture(name):
    from PIL import Image, ImageStat
    ppm = out / (name + '.ppm')
    qcommand('screendump', dict(filename=str(ppm)))
    with Image.open(ppm) as frame:
        if max(ImageStat.Stat(frame.convert('RGB')).stddev) < 2:
            raise RuntimeError('Guest display is blank')
        frame.save(out / (name + '.png'))
    ppm.unlink()


def nodes(tree):
    yield tree.get('attributes', {})
    for child in tree.get('children', []):
        yield from nodes(child)


sequence = 0

def layout(label):
    global sequence
    sequence += 1
    name = f'{sequence:03d}-{label}.json'
    remote = '/data/local/tmp/olympium-' + name
    shell('uitest', 'dumpLayout', '-p', remote, timeout=45)
    run(hdc, '-t', target, 'file', 'recv', remote, out / name, timeout=45)
    return json.loads((out / name).read_text())


def app_nodes(tree, bundle=BUNDLE):
    # Only descendants of an actual app-owned root can supply calculator proof.
    if tree.get('attributes', {}).get('bundleName') == bundle:
        return list(nodes(tree))
    for child in tree.get('children', []):
        result = app_nodes(child, bundle)
        if result:
            return result
    return []


def click_node(node):
    x, y = node_center(node)
    shell('uitest', 'uiInput', 'click', str(x), str(y))


def node_center(node):
    coords = list(map(int, re.findall(r'-?\d+', node.get('bounds', ''))))
    if len(coords) != 4:
        raise RuntimeError('Invalid button bounds')
    x1, y1, x2, y2 = coords
    if not (0 <= x1 < x2 <= 360 and 0 <= y1 < y2 <= 720):
        raise RuntimeError('Button outside physical display')
    return (x1 + x2) // 2, (y1 + y2) // 2


def calculator_button(tree, ids):
    for node in app_nodes(tree):
        if (node.get('type') == 'Button' and node.get('visible') != 'false' and
                (node.get('id') in ids or node.get('text') in ids)):
            return node
    raise RuntimeError('Calculator button absent: ' + repr(ids))


def desktop_calculation(tree, label):
    from PIL import Image

    runtime = work / f'{label}-wayland'
    runtime.mkdir(mode=0o700, exist_ok=True)
    env = dict(os.environ, XDG_RUNTIME_DIR=str(runtime), WAYLAND_DISPLAY='wayland-0',
               WLR_BACKENDS='headless', WLR_RENDERER='pixman', DEV_DESK_HEADLESS='1',
               GDK_BACKEND='wayland', OLYMPIUM_DESK_HOME=str(work / 'host-library'))
    app = app_nodes(tree)[0]
    bounds = list(map(int, re.findall(r'\d+', app['bounds'])))
    if bounds != [0, 36, 360, 684]:
        raise RuntimeError('Unexpected calculator content bounds')
    reference = work / f'{label}-reference.png'
    reference.unlink(missing_ok=True)

    def host_frame(name):
        frame = out / (name + '.png')
        subprocess.run(['grim', str(frame)], env=env, timeout=10, check=True)
        with Image.open(frame) as image:
            return image.convert('RGB')

    def locate():
        if not reference.is_file():
            return None
        with Image.open(reference) as image:
            guest = image.convert('RGB')
        # Match the entire interior keypad to disambiguate repeated solid
        # rows. Use its rendered position for host pointer coordinates.
        sample_x, anchor_y, top_y, bottom_y = 20, 270, 225, 485
        sample_end = guest.width - sample_x
        anchor = guest.crop((sample_x, anchor_y, sample_end, anchor_y + 1)).tobytes()
        keypad = guest.crop((sample_x, top_y, sample_end, bottom_y)).tobytes()
        if len(set(anchor)) < 3 or len(set(keypad)) < 3:
            raise RuntimeError('Cannot locate the calculator keypad on the host')
        screen = host_frame(f'{label}-calculator-opened')
        width, height = screen.size
        for row in range(anchor_y, height - guest.height + anchor_y + 1):
            line = screen.crop((0, row, width, row + 1)).tobytes()
            pos = line.find(anchor)
            if pos < 0 or pos % 3:
                continue
            left = pos // 3 - sample_x
            top = row - anchor_y
            other = screen.crop((left + sample_x, top + top_y, left + sample_end, top + bottom_y))
            if other.tobytes() == keypad:
                return left, top, guest.width, guest.height, width, height
        return None

    with ((out / f'{label}-compositor.log').open('w') as compositor_log,
          (out / f'{label}-window.log').open('w') as window_log):
        compositor = subprocess.Popen([ROOT / 'desktop/dev-compositor-input-test', '-n'],
                                      env=env, stdout=compositor_log, stderr=subprocess.STDOUT)
        window = None
        try:
            wait_for(lambda: (runtime / 'wayland-0').exists(), 20, 'host Wayland socket')
            window = subprocess.Popen([sys.executable, ROOT / 'validation/openharmony/app-window.py',
                                       '--qmp', work / 'window.sock', '--hdc', hdc, '--target', target,
                                       '--bounds', *map(str, bounds), '--reference', reference],
                                      env=env, stdout=window_log, stderr=subprocess.STDOUT)

            def live_location():
                if window.poll() is not None:
                    raise RuntimeError('OpenHarmony Wayland test window exited')
                return locate()

            left, top, display_w, display_h, width, height = wait_for(
                live_location, 30, 'calculator pixels on host')
            report['desktop_window_verified'] = True

            def pointer_click(x, y):
                subprocess.run([str(a.desktop_pointer.resolve()), str(width), str(height),
                                str(x), str(y)], env=env, timeout=10, check=True)
            def guest_fields():
                current = app_nodes(layout('host-pointer-result'))
                return {n.get('id'): n.get('text') for n in current if n.get('type') == 'Text'}

            cases = (('C', '', ''), ('2', '2', '2'), ('+', '2+', '2'),
                     ('3', '2+3', '5'), ('=', '5', ''))
            for button, expression, result in cases:
                gx, gy = node_center(calculator_button(tree, (button,)))
                x = left + int((gx - bounds[0]) * display_w / (bounds[2] - bounds[0]))
                y = top + int((gy - bounds[1]) * display_h / (bounds[3] - bounds[1]))
                pointer_click(x, y)

                def input_observed():
                    fields = guest_fields()
                    return fields.get('expression') == expression and fields.get('result') == result

                wait_for(input_observed, 20, f'guest response to host button {button}')
                if button == '3':
                    time.sleep(0.5)
                    host_frame(f'{label}-calculator-preview')
            time.sleep(0.5)
            host_frame(f'{label}-calculator-result')
            report['desktop_pointer_verified'] = True
            # Olympium places the close button 16px from the left edge and
            # halfway up its 34px server decoration. Exercise its real close.
            pointer_click(left + 16, top - 17)
            if window.wait(timeout=20) != 0:
                raise RuntimeError('The host app window did not close cleanly')
            shell('aa', 'force-stop', BUNDLE)
            wait_for(lambda: not app_nodes(layout(f'{label}-desktop-closed')), 30,
                     'guest application closed after desktop close')
            report['desktop_close_verified'] = True
        finally:
            for process in (window, compositor):
                if process is not None and process.poll() is None:
                    process.terminate()
                    try:
                        process.wait(timeout=8)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait()


def calculator(round_name):
    home = layout(round_name + '-launcher')
    launched = False
    for child in home.get('children', []):
        if child.get('attributes', {}).get('bundleName') == 'com.ohos.launcher':
            for node in nodes(child):
                if node.get('text') == 'Calculator':
                    click_node(node)
                    launched = True
                    report['launch_method'] = 'ordinary guest launcher icon'
                    break
        if launched:
            break
    if not launched:
        result = shell('aa', 'start', '-a', 'MainAbility', '-b', BUNDLE)
        if 'error:' in result.lower():
            raise RuntimeError('Ability start rejected: ' + result)
        report['launch_method'] = 'aa start'
    def ready():
        tree = layout(round_name + '-start')
        # The bundled distributed calculator asks for device collaboration on
        # first launch. Basic arithmetic needs no device discovery permission.
        for child in tree.get('children', []):
            attrs = child.get('attributes', {})
            if (attrs.get('bundleName') == 'com.ohos.permissionmanager' and
                    attrs.get('abilityName') == 'com.ohos.permissionmanager.GrantAbility'):
                for node in nodes(child):
                    if node.get('type') == 'Button' and node.get('text') == 'Deny':
                        capture(round_name + '-optional-permission')
                        click_node(node)
                        report['optional_collaboration_permission'] = 'denied through guest UI'
                        return None
        return tree if app_nodes(tree) else None

    tree = wait_for(ready, 90, 'calculator-owned UI')
    capture(round_name + '-opened')
    report['guest_app_gui_verified'] = True

    def click(texts):
        click_node(calculator_button(layout(round_name + '-before-click'), texts))

    click(('C', 'AC', '清除'))
    before = app_nodes(layout(round_name + '-cleared'))
    for texts in (('2',), ('+',), ('3',)):
        click(texts)
    original = next(n.get('text') for n in before if n.get('id') == 'result')

    def preview_five():
        current = app_nodes(layout(round_name + '-result'))
        expression = next((n.get('text') for n in current if n.get('id') == 'expression'), '')
        for node in current:
            if node.get('id') != 'result' or node.get('type') != 'Text':
                continue
            text = node.get('text', '').strip()
            try:
                five = Decimal(text) == 5
            except InvalidOperation:
                continue
            if five and expression == '2+3' and original != node.get('text'):
                report[round_name + '_preview'] = dict(result=text, expression=expression)
                return True
        return False

    wait_for(preview_five, 30, 'expression 2+3 and changed preview result five')
    capture(round_name + '-preview')
    click(('=',))

    def committed_five():
        current = app_nodes(layout(round_name + '-committed-result'))
        fields = {n.get('id'): n.get('text') for n in current if n.get('type') == 'Text'}
        # This sample commits '=' by moving its preview to the expression row
        # and clearing the preview row. Validate both transitions, not a key 5.
        if fields.get('expression') == '5' and fields.get('result') == '':
            report[round_name + '_calculation'] = dict(
                expression='5', preview_cleared=True, input_button_ids=['C', '2', '+', '3', '='])
            return True
        return False

    wait_for(committed_five, 30, 'equals commits five and clears preview')
    capture(round_name + '-result')
    report['input_verified'] = True
    return tree


def verify_fresh_install(package):
    report['fresh_hap_install_verified'] = False
    report['fresh_hap_input_verified'] = False
    report['fresh_hap_sha256'] = hashlib.sha256(package.read_bytes()).hexdigest()
    previous = shell('bm', 'dump', '-a')
    if BUNDLE not in previous or FRESH_BUNDLE in previous:
        raise RuntimeError('Could not confirm that the SDK probe was absent before install')
    report['fresh_hap_absent_before_install'] = True
    remote_path = '/data/local/tmp/olympium-fresh-probe.hap'
    run(hdc, '-t', target, 'file', 'send', package, remote_path)
    result = shell('bm', 'install', '-p', remote_path, timeout=120)
    (out / 'fresh-hap-install.log').write_text(result)
    if 'install bundle successfully' not in result.lower():
        raise RuntimeError('Bundle Manager rejected the fresh SDK test package')
    installed = shell('bm', 'dump', '-n', FRESH_BUNDLE)
    if FRESH_BUNDLE not in installed or 'MainAbility' not in installed:
        raise RuntimeError('Installed SDK probe has no registered MainAbility')
    report['fresh_hap_install_verified'] = True
    launch = shell('aa', 'start', '-a', 'MainAbility', '-b', FRESH_BUNDLE)
    if 'start ability successfully' not in launch.lower():
        raise RuntimeError('Ability Manager did not accept the SDK probe launch')

    def native_view():
        elements = app_nodes(layout('fresh-hap-opened'), FRESH_BUNDLE)
        if any(n.get('text') == 'Olympium fresh HAP' for n in elements):
            return elements
        return None

    elements = wait_for(native_view, 45, 'fresh HAP owned GUI')
    capture('fresh-hap-opened')
    report['fresh_hap_gui_verified'] = True
    button = next((n for n in elements if n.get('id') == 'verify-fresh-hap'), None)
    if button is None:
        raise RuntimeError('Fresh SDK probe input button is missing')
    click_node(button)
    wait_for(lambda: any(n.get('text') == 'Fresh HAP input received'
                         for n in app_nodes(layout('fresh-hap-input'), FRESH_BUNDLE)),
             25, 'fresh HAP changes its text after the pointer click')
    capture('fresh-hap-input')
    report['fresh_hap_input_verified'] = True


try:
    if not Path('/dev/kvm').exists():
        raise RuntimeError('Hardware acceptance requires /dev/kvm')
    if not (work / 'publisher-provenance.json').exists():
        extra = ['--with-ets'] if a.fresh_hap else []
        run(sys.executable, ROOT / 'validation/openharmony/prepare-runtime.py',
            '--work', work, *extra, timeout=1800)
    if a.fresh_hap:
        fresh_work = work / 'fresh-hap-build'
        run(sys.executable, ROOT / 'validation/openharmony/build-fresh-hap.py',
            '--sdk', work / 'sdk', '--work', fresh_work, timeout=240)
    image = work / 'oniro/images'
    build = work / 'bootstrap-build'
    run(sys.executable, ROOT / 'validation/openharmony/build-bootstrap.py',
        '--sdk', work / 'sdk/native', '--sources', work / 'sources',
        '--system-image', image / 'system.img', '--output', build, timeout=900)
    (out / 'build-result.json').write_bytes((build / 'build-result.json').read_bytes())
    (out / 'publisher-provenance.json').write_bytes((work / 'publisher-provenance.json').read_bytes())
    userdata = work / 'userdata-test.img'
    with (image / 'userdata.img').open('rb') as source, userdata.open('wb') as dest:
        sparse_copy(source, dest)
    fsck = subprocess.run(['e2fsck', '-fy', str(userdata)], stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    (out / 'userdata-fsck.log').write_bytes(fsck.stdout)
    if fsck.returncode not in (0, 1, 2):
        raise RuntimeError('Private SDK userdata repair failed')
    monitor = work / 'qmp.sock'
    monitor.unlink(missing_ok=True)
    kernel = ('ip=dhcp loglevel=4 console=ttyS0,115200 init=init root=/dev/ram0 rw '
              'ohos.boot.hardware=x86_general '
              'ohos.required_mount.system=/dev/block/vdb@/usr@ext4@ro,barrier=1@wait,required '
              'ohos.required_mount.vendor=/dev/block/vdc@/vendor@ext4@ro,barrier=1@wait,required '
              'ohos.required_mount.misc=/dev/block/vda@/misc@none@none=@wait,required')
    command = ['qemu-system-x86_64', '-machine', 'q35', '-accel', 'kvm', '-cpu', 'host',
               '-global', 'ICH9-LPC.disable_s3=1', '-global', 'ICH9-LPC.disable_s4=1',
               '-smp', '2', '-m', '4096', '-vga', 'none', '-display', 'none',
               '-device', 'virtio-gpu-pci,xres=360,yres=720,max_outputs=1,addr=08.0',
               '-audiodev', 'driver=none,id=audio0', '-device', 'ES1370,audiodev=audio0',
               '-kernel', str(image / 'bzImage'), '-initrd', str(image / 'ramdisk.img'),
               '-append', kernel, '-netdev', 'user,id=net0,hostfwd=tcp:127.0.0.1:55555-:55555',
               '-device', 'virtio-net-pci,netdev=net0',
               '-object', 'rng-random,id=rng0,filename=/dev/urandom', '-device', 'virtio-rng-pci,rng=rng0',
               '-qmp', f'unix:{monitor},server=on,wait=off', '-serial', 'file:' + str(out / 'serial.log')]
    if a.desktop_pointer:
        window_monitor = work / 'window.sock'
        window_monitor.unlink(missing_ok=True)
        command += ['-qmp', f'unix:{window_monitor},server=on,wait=off']
    for i, name in enumerate(('updater', 'system', 'vendor')):
        command += ['-drive', f'file={image / (name + ".img")},if=virtio,format=raw,snapshot=on,index={i}']
    command += ['-drive', f'file={userdata},if=virtio,format=raw,index=3']
    serial = (out / 'qemu.log').open('w')
    vm = subprocess.Popen(command, stdout=serial, stderr=subprocess.STDOUT)
    wait_for(monitor.exists, 20, 'QMP socket')
    connection = socket.socket(socket.AF_UNIX)
    connection.settimeout(45)
    connection.connect(str(monitor))
    qmp = connection.makefile('rwb')
    json.loads(qmp.readline())
    qcommand('qmp_capabilities')
    report['kvm'] = qcommand('query-kvm')
    if not report['kvm'].get('enabled'):
        raise RuntimeError('QEMU is not using KVM')
    report['kvm_verified'] = True
    hdc = work / 'sdk/toolchains/hdc'
    target = '127.0.0.1:55555'

    def shell(*words, timeout=60):
        return run(hdc, '-t', target, 'shell', *words, timeout=timeout)

    def connected():
        run(hdc, 'tconn', target, timeout=10, check=False)
        return target in run(hdc, 'list', 'targets', timeout=10)

    wait_for(connected, 180, 'HDC connection')
    for name in ('account-client', 'bootstrap'):
        run(hdc, '-t', target, 'file', 'send', build / name, '/data/local/tmp/' + name)
        shell('chmod', '700', '/data/local/tmp/' + name)
    wait_for(lambda: 'foreground_result=0' in shell('/data/local/tmp/account-client', 'status'),
             120, 'account manager')
    shell('/data/local/tmp/account-client', 'activate-sdk-user')
    wait_for(lambda: 'foreground_user=100' in shell('/data/local/tmp/account-client', 'status'),
             120, 'foreground SDK user')
    with (out / 'bootstrap.log').open('w') as log:
        helper = subprocess.Popen([str(hdc), '-t', target, 'shell', '/data/local/tmp/bootstrap', '--app-runtime'],
                                  stdout=log, stderr=subprocess.STDOUT)
        try:
            wait_for(lambda: helper.poll() is not None or 'screen_on=1' in (out / 'bootstrap.log').read_text(),
                     60, 'ordinary wake API')
            if 'credential_configured=1' in (out / 'bootstrap.log').read_text():
                raise RuntimeError('SDK image requires authentication; refusing unlock')
            shell('uitest', 'uiInput', 'swipe', '180', '600', '180', '120', '600')
            helper.wait(timeout=60)
            log.flush()
            text = (out / 'bootstrap.log').read_text()
            if (helper.returncode or 'screen_on=1' not in text or
                    'app_profile_ready_for_normal_gesture' not in text):
                raise RuntimeError('SDK bootstrap did not complete normally')
        finally:
            if helper.poll() is None:
                helper.terminate()
                helper.wait(timeout=10)
    status = shell('/data/local/tmp/bootstrap', '--status')
    if 'credential_configured=0' not in status:
        raise RuntimeError('SDK credential state was not confirmed unprotected')
    report['lock_manager_reports_unlocked'] = 'screen_locked=0' in status
    # With this publisher image the launcher can be visible while the lock
    # service still reports locked. Exercise the ordinary user launch path;
    # retain the API discrepancy rather than equating it with unlock success.
    def home_ready():
        tree = layout('home-after-normal-gesture')
        current = list(nodes(tree))
        if any(n.get('bundleName') == 'com.ohos.launcher' for n in current):
            return True
        if any(n.get('abilityName') == 'com.ohos.systemui.screenlock.ServiceExtAbility'
               for n in current):
            # Input must follow an actual ready lock-screen tree. The initial
            # wake may finish before this surface starts accepting gestures.
            credential = shell('/data/local/tmp/bootstrap', '--status')
            if 'credential_configured=0' not in credential:
                raise RuntimeError('SDK guest is no longer confirmed unprotected')
            shell('uitest', 'uiInput', 'swipe', '180', '600', '180', '120', '600')
        return False

    with (out / 'normal-unlock.log').open('w') as unlock_log:
        normal_unlock = subprocess.Popen([str(hdc), '-t', target, 'shell', '/data/local/tmp/bootstrap'],
                                         stdout=unlock_log, stderr=subprocess.STDOUT)
        try:
            wait_for(home_ready, 90, 'ordinary guest launcher after unprotected gesture')
            normal_unlock.wait(timeout=45)
        finally:
            if normal_unlock.poll() is None:
                normal_unlock.terminate()
                normal_unlock.wait(timeout=10)
    status = shell('/data/local/tmp/bootstrap', '--status')
    report['lock_manager_reports_unlocked'] = 'screen_locked=0' in status
    calculator('first')
    shell('aa', 'force-stop', BUNDLE)
    wait_for(lambda: not app_nodes(layout('stopped')), 30, 'app window removed')
    # Exercise the native installer with the publisher's original signed HAP,
    # copied out of the system image and back through the normal data path.
    # This is reinstall acceptance, not a claim of desktop HAP integration.
    hap = work / 'publisher-calculator.hap'
    run(hdc, '-t', target, 'file', 'recv',
        '/system/app/com.example.distributedcalc/Calc_Demo.hap', hap)
    report['publisher_hap_sha256'] = hashlib.sha256(hap.read_bytes()).hexdigest()
    remote_hap = '/data/local/tmp/olympium-publisher-calculator.hap'
    run(hdc, '-t', target, 'file', 'send', hap, remote_hap)
    install = shell('bm', 'install', '-p', remote_hap, '-r', timeout=120)
    (out / 'native-hap-install.log').write_text(install)
    if 'install bundle successfully' not in install.lower():
        raise RuntimeError('Publisher HAP reinstall was not accepted by Bundle Manager')
    report['native_hap_reinstall_verified'] = True
    tree = calculator('reopened')
    report['relaunch_verified'] = True
    if a.desktop_pointer:
        desktop_calculation(tree, 'host')
        tree = calculator('desktop-reopened')
        desktop_calculation(tree, 'host-reopened')
        report['desktop_relaunch_verified'] = True
    if a.fresh_hap:
        verify_fresh_install(fresh_work / 'probe.hap')
except Exception as error:
    report['error'] = str(error)
    if qmp:
        try:
            capture('failure-display')
        except Exception as capture_error:
            report['capture_error'] = str(capture_error)
    raise
finally:
    report['elapsed_seconds'] = round(time.monotonic() - started, 3)
    (out / 'result.json').write_text(json.dumps(report, indent=2) + '\n')
    if vm and vm.poll() is None:
        vm.terminate()
        try:
            vm.wait(timeout=20)
        except subprocess.TimeoutExpired:
            vm.kill()
            vm.wait(timeout=10)
    if qmp:
        qmp.close()
    if serial:
        serial.close()
