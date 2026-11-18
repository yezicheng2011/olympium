#!/usr/bin/env python3
"""Checks for runtime integrity, archive confinement and bounded APK icons."""
import hashlib
import importlib.machinery
import importlib.util
import io
import json
import os
from pathlib import Path
import stat
import subprocess
import sys
import tarfile
import tempfile
import time
import unittest
from unittest import mock
import zipfile

from android_icon import apk_icon
from android_runtime import download, extract, sparse_copy
from android_window import app_root_task
from android_framework import owned_runtime, process_identity

loader = importlib.machinery.SourceFileLoader('android_installer', str(Path(__file__).with_name('olympium-android')))
installer = importlib.util.module_from_spec(importlib.util.spec_from_loader(loader.name, loader))
loader.exec_module(installer)


class RuntimeFiles(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.stage = self.root / 'stage'
        self.stage.mkdir()

    def tearDown(self):
        self.temporary.cleanup()

    def test_download_rejects_changed_publisher_bytes(self):
        source = self.root / 'publisher'
        source.write_bytes(b'verified bytes')
        cache = self.root / 'cache'
        cache.mkdir()
        record = dict(path='fixture', url=source.as_uri(), size=14,
                      algorithm='sha256', checksum=hashlib.sha256(source.read_bytes()).hexdigest())
        target = download(record, cache)
        self.assertEqual(target.read_bytes(), source.read_bytes())
        target.write_bytes(b'untrusted data')
        with self.assertRaisesRegex(RuntimeError, 'checksum mismatch'):
            download(record, cache)

    def test_unaccelerated_host_is_rejected_before_download(self):
        with mock.patch.dict(installer.os.environ, {'OLYMPIUM_ANDROID_ALLOW_TCG': '0'}), \
                mock.patch.object(installer.os, 'access', return_value=False), \
                mock.patch.object(installer, 'ensure_runtime') as provision:
            with self.assertRaisesRegex(RuntimeError, 'hardware virtualization'):
                installer.start_runtime(self.root / 'desktop')
            provision.assert_not_called()

    def test_framework_never_targets_an_external_device(self):
        for serial in ('emulator-5558', 'physical-test-device', '127.0.0.1:5555'):
            with self.assertRaisesRegex(RuntimeError, 'managed Android runtime'):
                owned_runtime(self.root, self.root, serial)

    def test_unowned_managed_port_is_rejected_before_package_changes(self):
        with mock.patch.dict(installer.os.environ, {'OLYMPIUM_ANDROID_SERIAL': ''}), \
                mock.patch.object(installer, 'owned_runtime', side_effect=RuntimeError('not owned')), \
                mock.patch.object(installer, 'adb') as bridge:
            with self.assertRaisesRegex(RuntimeError, 'not owned'):
                installer.prepare_services('emulator-5560', self.root)
            bridge.assert_not_called()

    def test_avd_crash_recovery_keeps_live_process_lock(self):
        lock = self.root / 'hardware-qemu.ini.lock'
        contents = str(os.getpid()).encode() + b'\0'
        lock.write_bytes(contents)
        installer.clean_stale_avd_lock(self.root)
        self.assertEqual(lock.read_bytes(), contents)

    def test_managed_tool_is_reaped_when_emulator_monitor_fails(self):
        # Let the tool start, then simulate the owning VM disappearing. The
        # operation must terminate its child rather than wait its full deadline.
        monitor = mock.Mock(side_effect=[None, RuntimeError('emulator exited')])
        with self.assertRaisesRegex(RuntimeError, 'emulator exited'):
            installer.command([sys.executable, '-c', 'import time; time.sleep(30)'], monitor=monitor)
        self.assertEqual(monitor.call_count, 2)

    def test_avd_crash_recovery_removes_only_unreaped_process_lock(self):
        child = subprocess.Popen([sys.executable, '-c', 'pass'])
        try:
            deadline = time.monotonic() + 5
            stat_file = Path('/proc') / str(child.pid) / 'stat'
            while time.monotonic() < deadline:
                text = stat_file.read_text()
                if text[text.rfind(')') + 2:].split()[0] == 'Z':
                    break
                time.sleep(.01)
            else:
                self.fail('Test child did not become an unreaped zombie')
            lock = self.root / 'hardware-qemu.ini.lock'
            lock.write_bytes(str(child.pid).encode() + b'\0')
            other = self.root / 'userdata.img'
            other.write_bytes(b'keep user data')
            installer.clean_stale_avd_lock(self.root)
            self.assertFalse(lock.exists())
            self.assertEqual(other.read_bytes(), b'keep user data')
        finally:
            child.wait(timeout=5)

    def test_framework_rejects_reused_pid_and_unrelated_process(self):
        avd = self.root / 'android/avd'
        avd.mkdir(parents=True)
        record = process_identity(os.getpid())
        (avd / 'process.json').write_text(json.dumps(dict(record, start_ticks='0')))
        with self.assertRaisesRegex(RuntimeError, 'process identity changed'):
            owned_runtime(self.root, self.root, 'emulator-5560')
        (avd / 'process.json').write_text(json.dumps(record))
        with self.assertRaisesRegex(RuntimeError, 'managed SDK'):
            owned_runtime(self.root, self.root, 'emulator-5560')
        self.assertFalse((self.root / 'desktop').exists())

    def test_zip_cannot_write_outside_stage(self):
        archive = self.root / 'payload.zip'
        with zipfile.ZipFile(archive, 'w') as bundle:
            bundle.writestr('../escaped', 'bad')
        with self.assertRaises(RuntimeError):
            extract(archive, dict(destination='.', strip=0), self.stage)
        self.assertFalse((self.root / 'escaped').exists())

    def test_tar_link_cannot_escape_stage(self):
        archive = self.root / 'payload.tar'
        with tarfile.open(archive, 'w') as bundle:
            entry = tarfile.TarInfo('outside')
            entry.type = tarfile.SYMTYPE
            entry.linkname = '../escaped'
            bundle.addfile(entry)
        with self.assertRaises(RuntimeError):
            extract(archive, dict(destination='.', strip=0), self.stage)
        self.assertFalse((self.stage / 'outside').exists())

    def test_safe_sdk_link_and_executable(self):
        archive = self.root / 'sdk.zip'
        with zipfile.ZipFile(archive, 'w') as bundle:
            entry = zipfile.ZipInfo('sdk/bin/tool')
            entry.external_attr = (stat.S_IFREG | 0o755) << 16
            bundle.writestr(entry, b'program')
            entry = zipfile.ZipInfo('sdk/bin/alias')
            entry.create_system = 3
            entry.external_attr = (stat.S_IFLNK | 0o777) << 16
            bundle.writestr(entry, b'tool')
        extract(archive, dict(destination='.', strip=1), self.stage)
        self.assertEqual((self.stage / 'bin/alias').read_bytes(), b'program')
        self.assertTrue((self.stage / 'bin/tool').stat().st_mode & 0o111)

    def test_sparse_system_image_preserves_bytes_and_length(self):
        payload = b'header' + b'\0' * (3 * 1024 * 1024) + b'trailer' + b'\0' * 100
        out = self.stage / 'system.img'
        with out.open('wb') as stream:
            sparse_copy(io.BytesIO(payload), stream)
        self.assertEqual(out.stat().st_size, len(payload))
        self.assertEqual(hashlib.sha256(out.read_bytes()).digest(), hashlib.sha256(payload).digest())

    def test_external_svg_content_is_not_accepted(self):
        apk = self.root / 'app.apk'
        with zipfile.ZipFile(apk, 'w') as bundle:
            bundle.writestr('res/icon.xml', b'compiled XML fixture')
        text = '''  E: vector (line=1)
    E: path (line=2)
      A: android:fillColor(0x0)="url(https://example.invalid/track)"
      A: android:pathData(0x0)="M0,0 L2,2 Z"
'''
        icon = apk_icon(apk, "application-icon-160:'res/icon.xml'", lambda *args: text, self.stage / 'icon')
        self.assertIsNone(icon)
        self.assertFalse((self.stage / 'icon.svg').exists())

    def test_root_task_does_not_move_a_shared_task_group(self):
        dump = '''RootTask id=9 bounds=[0,0][360,640] displayId=0 userId=0
  taskId=9: org.fossify.math/.Main bounds=[0,0][360,640] visible=true topActivity=ComponentInfo{org.fossify.math/.Main}
  taskId=10: another.app/.Main visible=true topActivity=ComponentInfo{another.app/.Main}
'''
        with self.assertRaisesRegex(RuntimeError, 'other tasks'):
            app_root_task(dump, 'org.fossify.math')

    def test_root_task_rejects_duplicate_and_similar_package(self):
        dump = '''RootTask id=9 bounds=[0,0][360,640] displayId=0 userId=0
  taskId=9: org.fossify.math/.Main visible=true topActivity=ComponentInfo{org.fossify.math/.Main}
'''
        self.assertEqual(app_root_task(dump, 'org.fossify.math'), (0, 9))
        with self.assertRaisesRegex(RuntimeError, 'Ambiguous'):
            app_root_task(dump + dump.replace('id=9', 'id=11'), 'org.fossify.math')
        self.assertIsNone(app_root_task(dump.replace('org.fossify.math', 'org.fossify.math.fake'),
                                       'org.fossify.math'))


if __name__ == '__main__':
    unittest.main()
