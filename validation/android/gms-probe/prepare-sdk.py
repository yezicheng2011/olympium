#!/usr/bin/env python3
"""Download publisher-pinned tools for the independent validation APK."""
import json
from pathlib import Path
import sys

repo = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(repo / 'image'))
from android_runtime import download, extract

root = Path(sys.argv[1]).resolve()
root.mkdir(parents=True, exist_ok=True)
cache = root / 'downloads'
cache.mkdir(exist_ok=True)
for record in json.loads(Path(__file__).with_name('build-tools.json').read_text())['artifacts']:
    if (root / record['destination']).exists():
        raise SystemExit('Use a fresh SDK destination; refusing to merge unrecognized SDK files')
    archive = download(record, cache)
    extract(archive, record, root)
print(root)
