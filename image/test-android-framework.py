#!/usr/bin/env python3
"""Exercise the privileged setup boundary, rollback and warm-start behavior."""
import json
from pathlib import Path
import tempfile
import unittest
from unittest import mock

from android_framework import digest, prepare_framework


class FrameworkSetup(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.assets = self.root / 'assets'
        self.assets.mkdir()
        self.profile = dict(profile_id='fixture', original_services_jar_sha256='a' * 64)
        (self.assets / 'profile.json').write_text(json.dumps(self.profile))
        for name in ('helper.dex', 'mount-helper', 'health.apk'):
            (self.assets / name).write_bytes(name.encode())
        self.manifest = {name: digest(self.assets / name) for name in
                         ('helper.dex', 'mount-helper', 'health.apk', 'profile.json')}
        (self.assets / 'assets.json').write_text(json.dumps(self.manifest))
        build = self.root / 'android/services/fixture'
        build.mkdir(parents=True)
        (build / 'services.jar').write_bytes(b'compiled framework fixture')
        self.expected = digest(build / 'services.jar')
        (build / 'result.json').write_text(json.dumps(dict(sha256=self.expected, helper=self.manifest['helper.dex'])))
        self.identity = dict(pid=123, start_ticks='456')
        self.addCleanup(mock.patch.stopall)
        mock.patch.dict('os.environ', {'OLYMPIUM_ANDROID_COMPAT_ASSETS': str(self.assets)}).start()
        self.ownership = mock.patch('android_framework.owned_runtime', return_value=self.identity).start()
        self.calls = []
        self.server = '100'
        self.mounted = False
        self.rooted = False
        self.compatible = True
        self.readonly = True
        self.vanish_on_result = False
        self.user_states = ['RUNNING_UNLOCKED']
        self.last_user_state = None
        self.original = self.profile['original_services_jar_sha256']

    def adb(self, serial, *args, **kwargs):
        self.assertEqual(serial, 'emulator-5560')
        self.calls.append(args)
        if args[0] in ('root', 'unroot'):
            self.rooted = args[0] == 'root'
        if args[0] in ('root', 'unroot', 'wait-for-device', 'push', 'install'):
            return 'Success'
        self.assertEqual(args[0], 'shell')
        words = args[1:]
        if words == ('pidof', 'system_server'):
            return self.server
        if words[:1] == ('getprop',):
            return {'ro.build.version.sdk': '37', 'ro.build.type': 'userdebug', 'sys.boot_completed': '1'}[words[1]]
        if words == ('id', '-u'):
            return '0' if self.rooted else '2000'
        if words[:1] == ('sha256sum',):
            value = self.expected if self.mounted or words[1].startswith('/data/') else self.original
            return value + '  framework'
        if words[:1] in (('mkdir',), ('chmod',), ('chcon',)):
            return ''
        if words[0].endswith('/mount-helper'):
            self.mounted = words[1] == 'install'
            return ''
        if words[:1] == ('setprop',):
            self.server = str(int(self.server) + 1)
            return ''
        if words == ('sm', 'list-volumes', 'private'):
            return 'private mounted null'
        if words == ('getenforce',):
            return 'Enforcing'
        if words == ('cat', '/proc/1/mountinfo'):
            mode = 'ro' if self.readonly else 'rw'
            return '42 40 0:8 /services.jar /system/framework/services.jar ' + mode + ',nosuid,nodev - ext4 /dev/block/vdb rw'
        if words[:2] == ('pm', 'path'):
            return ''
        if words[:2] == ('pm', 'clear'):
            return 'Success'
        if words == ('am', 'get-started-user-state', '0'):
            self.last_user_state = self.user_states.pop(0) if len(self.user_states) > 1 else self.user_states[0]
            return self.last_user_state
        if words[:2] == ('am', 'start'):
            self.assertEqual(self.last_user_state, 'RUNNING_UNLOCKED')
            return ''
        if words[:1] == ('am',):
            return ''
        if words[:1] == ('run-as',):
            if self.vanish_on_result:
                self.ownership.side_effect = RuntimeError('emulator exited')
                raise RuntimeError('emulator exited')
            return json.dumps(dict(caller_package='org.olympium.android.health', caller_uid=10101,
                                  android_api=37, probe_completed=True,
                                  signature_compatibility_verified=self.compatible,
                                  companion_signature_compatibility_verified=True,
                                  gms_service_handshake_verified=True))
        self.fail('Unexpected device operation: ' + repr(words))

    def run_setup(self):
        return prepare_framework('emulator-5560', self.root, self.root, self.adb,
                                 lambda *args: self.manifest['health.apk'])

    def test_failed_client_check_restores_original_and_does_not_certify_success(self):
        self.compatible = False
        with self.assertRaisesRegex(RuntimeError, 'health check failed'):
            self.run_setup()
        self.assertFalse(self.mounted)
        self.assertIn(('shell', '/data/misc/olympium-framework/mount-helper', 'restore'), self.calls)
        self.assertFalse((self.root / 'android/services/framework.json').exists())
        self.assertEqual(self.calls[-3:], [('unroot',), ('wait-for-device',), ('shell', 'id', '-u')])

    def test_changed_framework_is_never_overwritten(self):
        self.original = 'b' * 64
        with self.assertRaisesRegex(RuntimeError, 'changed outside'):
            self.run_setup()
        self.assertFalse(any(args[0] == 'push' for args in self.calls))
        self.assertFalse(self.mounted)

    def test_restart_waits_for_credential_storage_before_launching_health(self):
        self.user_states = ['RUNNING_LOCKED', 'RUNNING_UNLOCKING', 'RUNNING_UNLOCKED']
        with mock.patch('android_framework.time.sleep'):
            self.assertTrue(self.run_setup()['basic_services_verified'])
        self.assertEqual(self.last_user_state, 'RUNNING_UNLOCKED')

    def test_dead_or_replaced_vm_never_receives_cleanup_commands(self):
        self.vanish_on_result = True
        with self.assertRaisesRegex(RuntimeError, 'emulator exited'):
            self.run_setup()
        self.assertFalse(any(args[0] == 'unroot' or args[-1] == 'restore' for args in self.calls))

    def test_writable_mount_is_rejected_and_restored(self):
        self.readonly = False
        with self.assertRaisesRegex(RuntimeError, 'not read-only'):
            self.run_setup()
        self.assertFalse(self.mounted)
        self.assertFalse(any(args[:3] == ('shell', 'pm', 'clear') for args in self.calls))

    def test_warm_start_does_not_restart_or_reinstall(self):
        result = self.run_setup()
        self.assertTrue(result['basic_services_verified'])
        self.assertFalse(result['gms_api_compatibility_verified'])
        self.calls.clear()
        self.assertEqual(self.run_setup(), result)
        self.assertEqual(self.calls, [('shell', 'pidof', 'system_server')])

    def test_server_restart_reuses_active_overlay_without_rewriting_it(self):
        self.run_setup()
        self.server = '200'
        self.calls.clear()
        self.run_setup()
        self.assertFalse(any(args[0] == 'push' or args[:2] == ('shell', 'setprop') for args in self.calls))


if __name__ == '__main__':
    unittest.main()
