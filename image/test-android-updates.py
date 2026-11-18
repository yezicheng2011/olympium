#!/usr/bin/env python3
"""Transaction and failure boundaries; real signing/data tests run on KVM."""
import hashlib
import importlib.machinery
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest import mock

loader = importlib.machinery.SourceFileLoader('android_update_installer', str(Path(__file__).with_name('olympium-android')))
backend = importlib.util.module_from_spec(importlib.util.spec_from_loader(loader.name, loader))
loader.exec_module(backend)


class Updates(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.package = 'org.olympium.validation.gms'
        self.dest = self.root / 'android/packages' / self.package
        self.dest.mkdir(parents=True)
        self.old_bytes = b'previous signed APK fixture'
        self.old_sha = hashlib.sha256(self.old_bytes).hexdigest()
        old_apk = self.dest / (self.old_sha + '.apk')
        old_apk.write_bytes(self.old_bytes)
        self.old = dict(ok=True, package=self.package, name='Probe', version='1', version_code=1,
                        min_sdk=37, id='android-' + hashlib.sha256(self.package.encode()).hexdigest()[:16],
                        sha=self.old_sha, path=str(old_apk), serial='emulator-5560', icon='')
        self.manifest = self.dest / 'installed.json'
        self.manifest.write_text(json.dumps(self.old))
        self.apk = self.root / 'update.apk'
        self.apk.write_bytes(b'newer signed APK fixture')
        self.new = dict(self.old, version='2', version_code=2, sha=backend.digest(self.apk))
        self.actual = self.old_sha
        self.calls = []
        self.reject = False
        self.addCleanup(mock.patch.stopall)
        mock.patch.object(backend, 'inspect', side_effect=lambda *args: dict(self.new)).start()
        mock.patch.object(backend, 'device', return_value='emulator-5560').start()
        mock.patch.object(backend, 'prepare_services').start()
        mock.patch.object(backend, 'installed_digest', side_effect=lambda *args: self.actual).start()
        mock.patch.object(backend, 'adb', side_effect=self.adb).start()

    def adb(self, serial, *args, **kwargs):
        self.calls.append(args)
        if args == ('shell', 'getprop', 'ro.build.version.sdk'):
            return '37'
        if args[0] == 'install':
            if self.reject:
                raise RuntimeError('INSTALL_FAILED_UPDATE_INCOMPATIBLE')
            self.actual = backend.digest(args[-1])
            return 'Success'
        if args == ('shell', 'pm', 'rollback-app', self.package):
            self.actual = self.old_sha
            return 'Success'
        self.fail('Unexpected native operation: ' + repr(args))

    def test_upgrade_requests_native_data_rollback_and_keeps_previous_payload(self):
        result = backend.install(self.apk, self.root, update_only=True)
        self.assertEqual(result['sha'], self.new['sha'])
        native = next(call for call in self.calls if call[0] == 'install')
        self.assertEqual(native[1:-1], ('--no-streaming', '-r', '--enable-rollback', '0'))
        self.assertEqual(Path(self.old['path']).read_bytes(), self.old_bytes)
        self.assertFalse(any(call[0] == 'uninstall' for call in self.calls))
        self.assertEqual(backend.load_installation(self.root, self.package)['sha'], self.actual)

    def test_native_signer_rejection_retains_the_old_receipt(self):
        self.reject = True
        before = self.manifest.read_bytes()
        with self.assertRaisesRegex(RuntimeError, 'UPDATE_INCOMPATIBLE'):
            backend.install(self.apk, self.root, update_only=True)
        self.assertEqual(self.manifest.read_bytes(), before)
        self.assertEqual(self.actual, self.old_sha)
        self.assertEqual(json.loads((self.dest / 'updates/pending.json').read_text())['status'], 'failed')

    def test_rollback_restores_receipt_only_after_native_hash_matches(self):
        backend.install(self.apk, self.root, update_only=True)
        result = backend.rollback(self.package, self.root)
        self.assertTrue(result['rolled_back'])
        self.assertEqual(backend.load_installation(self.root, self.package)['sha'], self.old_sha)
        self.assertTrue(backend.rollback(self.package, self.root)['unchanged'])

    def test_identical_import_does_not_expire_native_rollback(self):
        self.apk.write_bytes(self.old_bytes)
        self.new = dict(self.old)
        self.assertTrue(backend.install(self.apk, self.root)['unchanged'])
        self.assertFalse(any(call[0] == 'install' for call in self.calls))

    def test_downgrade_and_unmanaged_update_never_reach_package_installation(self):
        self.new['version_code'] = 0
        with self.assertRaisesRegex(RuntimeError, 'newer Android version'):
            backend.install(self.apk, self.root, update_only=True)
        self.manifest.unlink()
        with self.assertRaisesRegex(RuntimeError, 'existing managed application'):
            backend.install(self.apk, self.root, update_only=True)
        self.assertFalse(any(call[0] == 'install' for call in self.calls))

    def test_recovery_finishes_a_native_commit_without_reinstalling(self):
        backend.install(self.apk, self.root, update_only=True)
        latest = self.dest / 'updates/latest.json'
        pending = latest.with_name('pending.json')
        pending.write_bytes(latest.read_bytes())
        latest.unlink()
        self.manifest.write_text(json.dumps(self.old))
        self.calls.clear()
        backend.recover_pending_update(self.root, self.package, 'emulator-5560')
        self.assertEqual(backend.load_installation(self.root, self.package)['sha'], self.new['sha'])
        self.assertFalse(self.calls)
        self.assertFalse(pending.exists())

    def test_recovery_does_not_adopt_unrelated_installed_bytes(self):
        self.reject = True
        with self.assertRaises(RuntimeError):
            backend.install(self.apk, self.root, update_only=True)
        self.actual = 'a' * 64
        with self.assertRaisesRegex(RuntimeError, 'does not match'):
            backend.recover_pending_update(self.root, self.package, 'emulator-5560')
        self.assertEqual(backend.load_installation(self.root, self.package)['sha'], self.old_sha)

    def test_recovery_reconciles_a_native_rollback_without_repeating_it(self):
        backend.install(self.apk, self.root, update_only=True)
        latest = self.dest / 'updates/latest.json'
        transaction = json.loads(latest.read_text())
        transaction['status'] = 'rolling-back'
        latest.write_text(json.dumps(transaction))
        self.actual = self.old_sha
        self.calls.clear()
        backend.recover_pending_update(self.root, self.package, 'emulator-5560')
        self.assertEqual(backend.load_installation(self.root, self.package)['sha'], self.old_sha)
        self.assertEqual(json.loads(latest.read_text())['status'], 'rolled-back')
        self.assertFalse(self.calls)


if __name__ == '__main__':
    unittest.main()
