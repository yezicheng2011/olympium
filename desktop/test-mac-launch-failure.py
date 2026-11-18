#!/usr/bin/env python3
"""Verify desktop error reporting when a Mac runtime exits or crashes.

The fixture is a Mach-O header; the stand-in runtime tests launch supervision,
not macOS application compatibility.
"""
import os
from pathlib import Path
import plistlib
import struct
import subprocess
import tempfile

here = Path(__file__).resolve().parent
os.umask(0o022)
with tempfile.TemporaryDirectory(prefix='mac-launch-failure-') as directory:
    work = Path(directory)
    work.chmod(0o755)  # The nobody extraction fallback must reach this public fixture.
    app = work / 'Launch Failure.app'
    executable = app / 'Contents/MacOS/Fixture'
    executable.parent.mkdir(parents=True)
    executable.write_bytes(struct.pack('<IiiIIIII', 0xfeedfacf, 0x01000007, 3, 2, 0, 0, 0, 0))
    executable.chmod(0o755)
    (app / 'Contents/Info.plist').write_bytes(plistlib.dumps(dict(
        CFBundleExecutable='Fixture', CFBundleName='Launch Failure', CFBundlePackageType='APPL',
        CFBundleIdentifier='org.olympium.fixture.launchfailure')))
    runtime = work / 'runtime'
    runtime.write_text('#!/bin/sh\n[ "$1" != --check ] || exit 0\n'
                       '[ "${FIXTURE_SIGNAL:-0}" != 1 ] || kill -TERM $$\n'
                       'exit "${FIXTURE_EXIT:-0}"\n')
    runtime.chmod(0o755)
    env = dict(os.environ, OLYMPIUM_DESK_HOME=str(work / 'desk'),
               OLYMPIUM_MAC_RUNTIME=str(runtime), OLYMPIUM_NO_DIALOG='1')
    install = subprocess.run([str(here / 'dev-install'), 'import', '--consent', str(app)],
                             env=env, text=True, capture_output=True, timeout=30)
    assert install.returncode == 0, install.stdout + install.stderr
    rows = (work / 'desk/library.tsv').read_text().splitlines()
    identifier = next(row.split('\t')[0] for row in rows if row and not row.startswith('#'))
    for code in (0, 37):
        result = subprocess.run([str(here / 'dev-install'), 'launch', identifier, 'argument with spaces'],
                                env=dict(env, FIXTURE_EXIT=str(code)), text=True,
                                capture_output=True, timeout=30)
        assert result.returncode == code, result
        assert ('could not run with the installed Mac runtime' in result.stderr) == bool(code), result
        if code:
            assert 'exit 37' in result.stderr and 'stays in Applications' in result.stderr
    assert (work / 'desk/library.tsv').read_text().splitlines() == rows
    crashed = subprocess.run([str(here / 'dev-install'), 'launch', identifier],
                             env=dict(env, FIXTURE_SIGNAL='1'), text=True,
                             capture_output=True, timeout=30)
    assert crashed.returncode != 0 and 'could not run with the installed Mac runtime' in crashed.stderr
    print('TEST_MAC_LAUNCH_FAILURE_OK (supervision fixture; no Mac app executed)')
