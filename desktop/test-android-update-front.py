#!/usr/bin/env python3
"""Desktop catalog/provider glue only; native signing/data tests run on KVM."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

HELPER = r'''#!/usr/bin/env python3
import hashlib,json,sys
from pathlib import Path
command,value=sys.argv[1:3]
root=Path(sys.argv[sys.argv.index('--root')+1]);root.mkdir(parents=True,exist_ok=True)
statefile=root/'provider-state.json'
state=json.loads(statefile.read_text()) if statefile.exists() else {}
with (root/'provider-calls.jsonl').open('a') as stream:stream.write(json.dumps(dict(command=command,value=value))+'\n')
if command=='rollback':
    state['current']=state['before'];statefile.write_text(json.dumps(state));print(json.dumps(state['current']));sys.exit(0)
file=Path(value);version=file.read_text().strip()
info=dict(ok=True,package='org.olympium.fixture',id='android-0123456789abcdef',name='Fixture '+version,
          path=str(file),sha=hashlib.sha256(file.read_bytes()).hexdigest(),icon='')
if command in ('install','update'):
    if command=='update' and version=='reject':
        print(json.dumps(dict(ok=False,message='INSTALL_FAILED_UPDATE_INCOMPATIBLE')));sys.exit(3)
    if state.get('current',{}).get('sha')!=info['sha']:
        if 'current' in state:state['before']=state['current']
        state['current']=info;statefile.write_text(json.dumps(state))
print(json.dumps(info))
'''


class Frontend(unittest.TestCase):
    def test_update_and_rollback_keep_one_launcher_and_commit_only_after_success(self):
        with tempfile.TemporaryDirectory(prefix='desktop-update-') as temporary:
            base = Path(temporary)
            root = base / 'desk'
            helper = base / 'provider'
            helper.write_text(HELPER)
            helper.chmod(0o700)
            first, second, wrong = [base / name for name in ('version one.apk', 'version two.apk', 'reject.apk')]
            for file, version in ((first, 'v1'), (second, 'v2'), (wrong, 'reject')):
                file.write_text(version)
            env = dict(os.environ, OLYMPIUM_DESK_HOME=str(root), OLYMPIUM_ANDROID_RUNTIME=str(helper),
                       XDG_DATA_HOME=str(base / 'xdg'), OLYMPIUM_NO_DIALOG='1')
            binary = Path(__file__).resolve().with_name('dev-install')
            def run(*words, success=True):
                result = subprocess.run([str(binary), *map(str, words)], env=env, capture_output=True)
                self.assertEqual(result.returncode == 0, success, result.stderr.decode())
                return result
            run('import', '--consent', first)
            original = (root / 'library.tsv').read_bytes()
            run('update', second, success=False)
            self.assertEqual((root / 'library.tsv').read_bytes(), original)
            calls = [json.loads(line) for line in (root / 'provider-calls.jsonl').read_text().splitlines()]
            self.assertFalse(any(call['command'] == 'update' for call in calls))
            run('update', '--consent', second)
            rows = [line for line in (root / 'library.tsv').read_text().splitlines() if not line.startswith('#')]
            self.assertEqual(len(rows), 1)
            identity = rows[0].split('\t')[0]
            self.assertEqual(identity, 'android-0123456789abcdef')
            self.assertIn('Fixture v2', rows[0])
            updated = (root / 'library.tsv').read_bytes()
            run('update', '--consent', wrong, success=False)
            self.assertEqual((root / 'library.tsv').read_bytes(), updated)
            run('rollback', identity, success=False)
            self.assertEqual((root / 'library.tsv').read_bytes(), updated)
            run('rollback', '--consent', identity)
            restored = [line for line in (root / 'library.tsv').read_text().splitlines() if not line.startswith('#')]
            self.assertEqual(len(restored), 1)
            self.assertEqual(restored[0].split('\t')[0], identity)
            self.assertIn('Fixture v1', restored[0])
            self.assertIn(hashlib.sha256(first.read_bytes()).hexdigest(), restored[0])
            self.assertEqual(len(list((root / 'applications').glob('*.desktop'))), 1)
            self.assertTrue((root / 'launch' / identity).is_file())


if __name__ == '__main__':
    unittest.main()
