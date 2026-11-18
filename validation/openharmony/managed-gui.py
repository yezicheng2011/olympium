#!/usr/bin/env python3
"""KVM-only GUI acceptance of the owned, unprotected OpenHarmony SDK image."""
import argparse
from decimal import Decimal, InvalidOperation
import json
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


def app_nodes(tree):
    # Only descendants of an actual app-owned root can supply calculator proof.
    if tree.get('attributes', {}).get('bundleName') == BUNDLE:
        return list(nodes(tree))
    for child in tree.get('children', []):
        result = app_nodes(child)
        if result:
            return result
    return []


def click_node(node):
    coords = list(map(int, re.findall(r'-?\d+', node.get('bounds', ''))))
    if len(coords) != 4:
        raise RuntimeError('Invalid button bounds')
    x1, y1, x2, y2 = coords
    if not (0 <= x1 < x2 <= 360 and 0 <= y1 < y2 <= 720):
        raise RuntimeError('Button outside physical display')
    shell('uitest', 'uiInput', 'click', str((x1+x2)//2), str((y1+y2)//2))


def calculator(round_name):
    result = shell('aa', 'start', '-a', 'MainAbility', '-b', BUNDLE)
    if 'error:' in result.lower():
        raise RuntimeError('Ability start rejected: ' + result)
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
        current = app_nodes(layout(round_name + '-before-click'))
        candidates = [n for n in current if n.get('text') in texts and n.get('visible') != 'false']
        if not candidates:
            raise RuntimeError('Calculator button absent: ' + repr(texts))
        click_node(candidates[-1])

    click(('C', 'AC', '清除'))
    before = app_nodes(layout(round_name + '-cleared'))
    for texts in (('2',), ('+',), ('3',), ('=',)):
        click(texts)
    original = {(n.get('hierarchy'), n.get('bounds')): n.get('text') for n in before}

    def changed_five():
        current = app_nodes(layout(round_name + '-result'))
        for node in current:
            text = node.get('text', '').strip()
            try:
                five = Decimal(text) == 5
            except InvalidOperation:
                continue
            key = node.get('hierarchy'), node.get('bounds')
            if five and key in original and original[key] != node.get('text'):
                return True
        return False

    wait_for(changed_five, 30, 'changed calculator output equals five')
    capture(round_name + '-result')
    report['input_verified'] = True


try:
    if not Path('/dev/kvm').exists():
        raise RuntimeError('Hardware acceptance requires /dev/kvm')
    if not (work / 'publisher-provenance.json').exists():
        run(sys.executable, ROOT / 'validation/openharmony/prepare-runtime.py', '--work', work, timeout=1800)
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
    calculator('first')
    shell('aa', 'force-stop', BUNDLE)
    wait_for(lambda: not app_nodes(layout('stopped')), 30, 'app window removed')
    calculator('reopened')
    report['relaunch_verified'] = True
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
