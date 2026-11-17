"""Create the pinned microG client-query wrapper using precompiled local assets."""
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import time
import zipfile


def digest(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def aligned_write(archive, entry, data):
    # The uncompressed DEX payload starts on a four-byte boundary. Preserve
    # existing metadata and use a valid padding extra field, not arbitrary bytes.
    offset = archive.fp.tell() + 30 + len(entry.filename.encode('utf-8')) + len(entry.extra)
    padding = (-(offset + 4)) % 4
    entry.extra += struct.pack('<HH', 0xffff, padding) + b'\0' * padding
    archive.writestr(entry, data)


def patch_services(original, helper_dex, output, profile):
    original, helper_dex, output = map(Path, (original, helper_dex, output))
    if digest(original) != profile['original_services_jar_sha256']:
        raise RuntimeError('Android framework differs from the supported, pinned system image')
    output.mkdir(parents=True, exist_ok=True)
    tree = output / 'smali'
    if tree.exists():
        raise RuntimeError('Framework compilation needs a fresh staging directory')
    dex = output / 'original.dex'
    with zipfile.ZipFile(original) as archive:
        dex.write_bytes(archive.read(profile['target_dex']))
    subprocess.run(['baksmali', 'disassemble', str(dex), '-o', str(tree)], check=True,
                   timeout=180, stdout=subprocess.DEVNULL)
    engine = tree / 'com/android/server/pm/ComputerEngine.smali'
    text = engine.read_text()
    signature = '(Lcom/android/server/pm/pkg/PackageStateInternal;JI)Landroid/content/pm/PackageInfo;'
    method = '.method public final generatePackageInfo' + signature
    renamed = 'generatePackageInfoWithoutOlympiumCompat'
    if text.count(method) != 1 or renamed in text:
        raise RuntimeError('Unexpected Android package-manager API; framework was not changed')
    text = text.replace(method, '.method public final ' + renamed + signature)
    text += f'''
{method}
    .registers 7
    invoke-virtual/range {{p0 .. p4}}, Lcom/android/server/pm/ComputerEngine;->{renamed}{signature}
    move-result-object v0
    invoke-static {{v0, p1}}, Lorg/olympium/android/compat/MicrogSignatureCompat;->rewrite(Landroid/content/pm/PackageInfo;Ljava/lang/Object;)Landroid/content/pm/PackageInfo;
    move-result-object v0
    return-object v0
.end method
'''
    engine.write_text(text)
    patched = output / 'patched.dex'
    subprocess.run(['smali', 'assemble', str(tree), '-o', str(patched)], check=True,
                   timeout=180, stdout=subprocess.DEVNULL)
    jar = output / 'services.jar'
    with zipfile.ZipFile(original) as archive, zipfile.ZipFile(jar, 'w') as result:
        names = archive.namelist()
        index = 2
        while f'classes{index}.dex' in names:
            index += 1
        for entry in archive.infolist():
            data = patched.read_bytes() if entry.filename == profile['target_dex'] else archive.read(entry)
            aligned_write(result, entry, data)
        extra = zipfile.ZipInfo(f'classes{index}.dex', (1980, 1, 1, 0, 0, 0))
        extra.compress_type = zipfile.ZIP_STORED
        aligned_write(result, extra, helper_dex.read_bytes())
    return jar


def process_identity(pid):
    """Bind records to a specific owned process, including across PID reuse."""
    proc = Path('/proc') / str(int(pid))
    if proc.stat().st_uid != os.getuid():
        raise RuntimeError('Managed emulator process is owned by another user')
    stat = (proc / 'stat').read_text()
    return dict(pid=int(pid), start_ticks=stat[stat.rfind(')') + 2:].split()[19])


def owned_runtime(root, runtime, serial):
    if serial != 'emulator-5560':
        raise RuntimeError('Automatic framework support requires the managed Android runtime')
    record = json.loads((root / 'android/avd/process.json').read_text())
    if process_identity(record['pid']) != record:
        raise RuntimeError('Managed Android emulator process identity changed')
    proc = Path('/proc') / str(record['pid'])
    executable = (proc / 'exe').resolve(strict=True)
    if not executable.is_relative_to(runtime.resolve() / 'emulator'):
        raise RuntimeError('Android emulator does not belong to the managed SDK')
    words = (proc / 'cmdline').read_bytes().decode().split('\0')
    for flag, value in (('-avd', 'olympium-android17'), ('-port', '5560')):
        if flag not in words or words[words.index(flag) + 1] != value:
            raise RuntimeError('Android emulator command does not match the managed runtime')
    environment = (proc / 'environ').read_bytes().split(b'\0')
    expected_home = os.fsencode('ANDROID_AVD_HOME=' + str(root / 'android/avd'))
    if expected_home not in environment:
        raise RuntimeError('Android emulator belongs to another desktop data directory')
    return record


def prepare_framework(serial, root, runtime, adb, installed_digest, ready_timeout=600):
    """Install/recheck a bounded framework overlay only in our owned SDK VM."""
    identity = owned_runtime(root, runtime, serial)

    def same_runtime():
        try:
            return owned_runtime(root, runtime, serial) == identity
        except (OSError, RuntimeError):
            return False
    assets = Path(os.environ.get('OLYMPIUM_ANDROID_COMPAT_ASSETS', '/usr/libexec/dev-desktop/android-compat'))
    manifest = json.loads((assets / 'assets.json').read_text())
    for name in ('helper.dex', 'mount-helper', 'health.apk', 'profile.json'):
        if digest(assets / name) != manifest.get(name):
            raise RuntimeError('Android support asset checksum mismatch: ' + name)
    profile = json.loads((assets / 'profile.json').read_text())
    services = root / 'android/services'
    state_file = services / 'framework.json'

    def shell(*args, **kwargs):
        return adb(serial, 'shell', *args, **kwargs)

    def server():
        return shell('pidof', 'system_server').strip()

    pid = server()
    if not pid.isdecimal():
        raise RuntimeError('Managed Android system server is not ready')
    cache_key = dict(process=identity, system_server=pid, assets=manifest)
    if state_file.exists():
        cached = json.loads(state_file.read_text())
        if cached.get('identity') == cache_key and cached.get('basic_services_verified') is True:
            return cached
    # Never restart Android's Java runtime while a desktop bridge is displaying
    # an app on this device. Other SDK devices and physical phones are untouched.
    for proc in Path('/proc').glob('[0-9]*'):
        try:
            words = (proc / 'cmdline').read_bytes().split(b'\0')
            if words and Path(os.fsdecode(words[0])).name == 'scrcpy' and serial.encode() in words:
                raise RuntimeError('Close Android app windows before updating Android Services')
        except (OSError, ValueError):
            continue
    if shell('getprop', 'ro.build.version.sdk').strip() != '37' or \
            shell('getprop', 'ro.build.type').strip() not in ('eng', 'userdebug'):
        raise RuntimeError('This framework profile requires the pinned AOSP API 37 development image')
    build = services / profile['profile_id']
    jar = build / 'services.jar'
    built = build / 'result.json'
    build_record = json.loads(built.read_text()) if built.exists() else {}
    if not jar.exists() or digest(jar) != build_record.get('sha256') or build_record.get('helper') != manifest['helper.dex']:
        with tempfile.TemporaryDirectory(prefix='framework-', dir=services) as temporary:
            work = Path(temporary)
            original = work / 'original.jar'
            adb(serial, 'pull', '/system/framework/services.jar', str(original), timeout=240)
            compiled = patch_services(original, assets / 'helper.dex', work / 'compile', profile)
            build.mkdir(exist_ok=True)
            os.replace(compiled, jar)
            built.write_text(json.dumps(dict(sha256=digest(jar), helper=manifest['helper.dex'])) + '\n')
    expected = digest(jar)
    remote = '/data/misc/olympium-framework'
    mounted = False
    rooted = False
    try:
        # Check ownership again immediately before any privileged guest action.
        if not same_runtime():
            raise RuntimeError('Managed Android emulator exited or changed before setup')
        adb(serial, 'root', timeout=120)
        rooted = True
        adb(serial, 'wait-for-device', timeout=120)
        if shell('id', '-u').strip() != '0':
            raise RuntimeError('Managed AOSP runtime did not provide its development root service')
        # Read init's mount view; an older adbd may retain the pre-overlay view.
        current_sha = shell('sha256sum', '/proc/1/root/system/framework/services.jar', timeout=240).split()[0]
        if current_sha not in (expected, profile['original_services_jar_sha256']):
            raise RuntimeError('Managed Android framework changed outside this support profile')
        if current_sha != expected:
            shell('mkdir', '-p', remote)
            shell('chmod', '0700', remote)
            # Push to a private directory; APK processes cannot replace the sources.
            adb(serial, 'push', str(jar), remote + '/services.jar', timeout=240)
            adb(serial, 'push', str(assets / 'mount-helper'), remote + '/mount-helper', timeout=120)
            shell('chmod', '0444', remote + '/services.jar')
            shell('chmod', '0700', remote + '/mount-helper')
            shell('chcon', 'u:object_r:system_file:s0', remote + '/services.jar')
            if shell('sha256sum', remote + '/services.jar').split()[0] != expected:
                raise RuntimeError('Staged Android framework checksum mismatch')
            shell(remote + '/mount-helper', 'install')
            mounted = True
            shell('setprop', 'ctl.restart', 'zygote')
            deadline = time.monotonic() + ready_timeout
            while time.monotonic() < deadline:
                try:
                    new_pid = server()
                    if new_pid and new_pid != pid and shell('getprop', 'sys.boot_completed').strip() == '1' \
                            and 'private mounted' in shell('sm', 'list-volumes', 'private'):
                        break
                except (RuntimeError, subprocess.TimeoutExpired):
                    if not same_runtime():
                        raise RuntimeError('Managed Android emulator exited or changed during setup')
                time.sleep(2)
            else:
                raise RuntimeError('Android Services restart did not complete')
        if shell('getenforce').strip() != 'Enforcing':
            raise RuntimeError('Managed Android SELinux enforcement changed')
        mounts = [line.split() for line in shell('cat', '/proc/1/mountinfo').splitlines()]
        overlays = [row for row in mounts if len(row) > 6 and row[4] == '/system/framework/services.jar']
        if len(overlays) != 1 or not {'ro', 'nosuid', 'nodev'}.issubset(set(overlays[0][5].split(','))):
            raise RuntimeError('Android Services framework mount is not read-only and restricted')
        health = 'org.olympium.android.health'
        previous = json.loads(state_file.read_text()) if state_file.exists() else {}
        present = shell('pm', 'path', health, check=False)
        if 'package:' in present:
            sha = installed_digest(serial, health)
            if sha != manifest['health.apk']:
                if sha != previous.get('health_apk_sha256'):
                    raise RuntimeError('A different Android Services health package is installed')
                adb(serial, 'uninstall', health, timeout=180)
                present = ''
        if 'package:' not in present:
            adb(serial, 'install', '--no-streaming', str(assets / 'health.apk'), timeout=600)
        if installed_digest(serial, health) != manifest['health.apk']:
            raise RuntimeError('Android Services health package checksum mismatch')
        if 'Success' not in shell('pm', 'clear', health):
            raise RuntimeError('Could not clear stale Android Services health data')
        shell('am', 'start', '--display', '0', '-n', health + '/.Probe', timeout=180)
        deadline = time.monotonic() + 240
        while time.monotonic() < deadline:
            text = shell('run-as', health, 'cat', 'files/result.json', check=False)
            try:
                result = json.loads(text)
                break
            except ValueError:
                time.sleep(2)
        else:
            raise RuntimeError('No fresh Android Services health result')
        checks = ('probe_completed', 'signature_compatibility_verified',
                  'companion_signature_compatibility_verified', 'gms_service_handshake_verified')
        if result.get('caller_package') != health or not isinstance(result.get('caller_uid'), int) \
                or result['caller_uid'] < 10000 or result.get('android_api') != 37 \
                or not all(result.get(name) is True for name in checks):
            raise RuntimeError('Android Services signature or Binder health check failed')
        shell('am', 'force-stop', health)
        state = dict(identity=dict(cache_key, system_server=server()), profile=profile['profile_id'],
                     framework_sha256=expected, health_apk_sha256=manifest['health.apk'],
                     basic_services_verified=True, gms_api_compatibility_verified=False, probe=result)
        temporary = state_file.with_suffix('.tmp')
        temporary.write_text(json.dumps(state, indent=2) + '\n')
        temporary.replace(state_file)
        return state
    except Exception as error:
        if mounted and same_runtime():
            try:
                shell(remote + '/mount-helper', 'restore')
                restored = shell('sha256sum', '/proc/1/root/system/framework/services.jar', timeout=240).split()[0]
                if restored != profile['original_services_jar_sha256']:
                    raise RuntimeError('Restored framework did not match the original system image')
                shell('setprop', 'ctl.restart', 'zygote')
            except Exception as rollback:
                raise RuntimeError('Android Services setup failed; restoring the original framework also failed: ' + str(rollback)) from error
        raise
    finally:
        if rooted and same_runtime():
            adb(serial, 'unroot', timeout=120, check=False)
            adb(serial, 'wait-for-device', timeout=120, check=False)
