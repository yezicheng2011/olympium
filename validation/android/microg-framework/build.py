#!/usr/bin/env python3
"""Build a pinned, experimental client-query hook; never deploys it to a device."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--services-jar', type=Path, required=True)
parser.add_argument('--sdk', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
repo = Path(__file__).resolve().parents[3]
import sys
sys.path.insert(0, str(repo / 'image'))
from android_framework import patch_services
source = repo / 'image/android-compat'
profile = json.loads((source / 'profile.json').read_text())
original = args.services_jar.resolve(strict=True)
out = args.output.resolve()
out.mkdir(parents=True, exist_ok=True)


def run(*command):
    subprocess.run(list(map(str, command)), check=True)


classes, dex_out = out / 'classes', out / 'helper-dex'
classes.mkdir()
dex_out.mkdir()
platform = args.sdk.resolve() / 'platforms/android-37.0/android.jar'
tools = args.sdk.resolve() / 'build-tools/37.0.0'
run('javac', '--release', '8', '-classpath', platform, '-d', classes, *sorted(source.glob('*.java')))
run('jar', '--create', '--file', out / 'helper.jar', '-C', classes, '.')
run(tools / 'd8', '--lib', platform, '--min-api', '37', '--output', dex_out, out / 'helper.jar')
patch_services(original, dex_out / 'classes.dex', out, profile)
run(tools / 'zipalign', '-c', '4', out / 'services.jar')
report = dict(profile=profile, status='built-only-not-runtime-verified',
              original_sha256=hashlib.sha256(original.read_bytes()).hexdigest(),
              modified_sha256=hashlib.sha256((out / 'services.jar').read_bytes()).hexdigest(),
              source_sha256={p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                             for p in sorted(source.glob('*.java'))})
(out / 'build-result.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report, indent=2))
