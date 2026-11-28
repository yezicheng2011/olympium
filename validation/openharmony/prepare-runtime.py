#!/usr/bin/env python3
"""Prepare publisher-pinned SDK/image fixtures without executing guest code."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import sys
import tarfile

repo = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(repo / 'image'))
from android_runtime import download, extract

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--work', type=Path, required=True)
parser.add_argument('--with-ets', action='store_true')
args = parser.parse_args()
work = args.work.resolve()
work.mkdir(parents=True, exist_ok=True)
cache = work / 'downloads'
cache.mkdir(exist_ok=True)
pins = json.loads((repo / 'validation/runtime-artifacts.json').read_text())['artifacts']
parts = [next(p for p in pins if p['path'].endswith('ohos-sdk-windows_linux-public.tar.gz.' + suffix))
         for suffix in ('aa', 'ab')]
combined = next(p for p in pins if p['path'] == 'OpenHarmony SDK 6.1 assembled archive')
archive = work / 'verified-sdk.tar.gz'
with archive.open('wb') as stream:
    for pin in parts:
        part = download(pin, cache)
        with part.open('rb') as source:
            shutil.copyfileobj(source, stream, 1024 * 1024)
        part.unlink()
with archive.open('rb') as stream:
    if hashlib.file_digest(stream, 'sha256').hexdigest() != combined['checksum']:
        raise SystemExit('Assembled SDK checksum mismatch')
names = {
    'linux/native-linux-x64-6.1.0.31-Release.zip': 'native',
    'linux/toolchains-linux-x64-6.1.0.31-Release.zip': 'toolchains',
}
if args.with_ets:
    names['linux/ets-linux-x64-6.1.0.31-Release.zip'] = 'ets'
selected = []
with tarfile.open(archive, 'r|gz') as sdk:
    for entry in sdk:
        name = entry.name.removeprefix('./')
        if name not in names:
            continue
        if not entry.isfile():
            raise SystemExit('Unexpected SDK archive member type')
        target = work / Path(name).name
        with sdk.extractfile(entry) as source, target.open('wb') as dest:
            shutil.copyfileobj(source, dest, 1024 * 1024)
        selected.append((target, names[name]))
if len(selected) != len(names):
    raise SystemExit('SDK build/toolchain archives were not found')
archive.unlink()
for target, destination in selected:
    shutil.rmtree(work / 'sdk' / destination, ignore_errors=True)
    extract(target, dict(destination=destination, strip=1), work / 'sdk')
    target.unlink()
image = next(p for p in pins if p['path'] == 'Oniro/OpenHarmony 6.1 emulator')
image_archive = download(image, cache)
shutil.rmtree(work / 'oniro', ignore_errors=True)
extract(image_archive, dict(destination='oniro', strip=0), work)
image_archive.unlink()
(work / 'publisher-provenance.json').write_text(json.dumps(dict(sdk_parts=parts, sdk=combined, image=image), indent=2) + '\n')
