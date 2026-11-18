"""Install pinned Android runtime components with HTTPS and publisher checksums."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import stat
import sys
import tarfile
import tempfile
import urllib.request
import zipfile

PINS = Path(__file__).with_name('android-runtime-pins.json')


def sparse_copy(source, output):
    size = 0
    while chunk := source.read(1024 * 1024):
        size += len(chunk)
        if chunk.count(0) == len(chunk):
            output.seek(len(chunk), 1)
        else:
            output.write(chunk)
    output.truncate(size)


def download(record, cache):
    target = cache / record['checksum']
    partial = target.with_suffix('.part')
    if not target.exists():
        # Resuming is accepted only when the publisher returns HTTP 206.
        offset = partial.stat().st_size if partial.exists() else 0
        request = urllib.request.Request(record['url'], headers={'Range': f'bytes={offset}-'} if offset else {})
        print('Downloading Android support: ' + record['path'], file=sys.stderr, flush=True)
        with urllib.request.urlopen(request, timeout=90) as response:
            append = offset and response.status == 206
            with partial.open('ab' if append else 'wb') as output:
                shutil.copyfileobj(response, output, 1024 * 1024)
        partial.replace(target)
    with target.open('rb') as stream:
        actual = hashlib.file_digest(stream, record['algorithm']).hexdigest()
    if target.stat().st_size != record['size'] or actual != record['checksum']:
        raise RuntimeError('Android publisher checksum mismatch: ' + record['path'])
    return target


def extract(archive, record, stage):
    base = stage / record['destination']
    links = []
    def target(name):
        parts = Path(name).parts
        if Path(name).is_absolute() or '..' in parts:
            raise RuntimeError('Unsafe Android archive member')
        parts = parts[record['strip']:]
        return base.joinpath(*parts) if parts else None
    if zipfile.is_zipfile(archive):
        with zipfile.ZipFile(archive) as bundle:
            for entry in bundle.infolist():
                dest = target(entry.filename)
                if dest is None:
                    continue
                mode = entry.external_attr >> 16
                if entry.is_dir():
                    dest.mkdir(parents=True, exist_ok=True)
                elif stat.S_ISLNK(mode):
                    links.append((dest, bundle.read(entry).decode()))
                else:
                    dest.parent.mkdir(parents=True, exist_ok=True)
                    with bundle.open(entry) as source, dest.open('wb') as output:
                        sparse_copy(source, output)
                    dest.chmod(0o755 if mode & 0o111 else 0o644)
    else:
        with tarfile.open(archive) as bundle:
            for entry in bundle:
                dest = target(entry.name)
                if dest is None:
                    continue
                if entry.isdir():
                    dest.mkdir(parents=True, exist_ok=True)
                elif entry.issym():
                    links.append((dest, entry.linkname))
                elif entry.isfile():
                    dest.parent.mkdir(parents=True, exist_ok=True)
                    with bundle.extractfile(entry) as source, dest.open('wb') as output:
                        sparse_copy(source, output)
                    dest.chmod(0o755 if entry.mode & 0o111 else 0o644)
                else:
                    raise RuntimeError('Unsupported Android archive member')
    # Symlinks are created after ordinary files, so they cannot redirect writes.
    for dest, link in links:
        if Path(link).is_absolute() or not (dest.parent / link).resolve().is_relative_to(stage.resolve()):
            raise RuntimeError('Unsafe Android archive link')
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.symlink_to(link)


def ensure_runtime(root):
    if os.uname().machine != 'x86_64':
        raise RuntimeError('The bundled Android 17 emulator currently requires Linux x86_64')
    pins = PINS.read_bytes()
    marker = root / 'runtime-version.json'
    if marker.is_file() and marker.read_bytes() == pins:
        return
    root.parent.mkdir(parents=True, exist_ok=True)
    cache = root.parent / 'downloads'
    cache.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='android-runtime-', dir=root.parent) as temporary:
        stage = Path(temporary)
        archives = []
        for record in json.loads(pins)['artifacts']:
            archive = download(record, cache)
            if record.get('format') == 'apk':
                dest = stage / record['destination']
                dest.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(archive, dest)
            else:
                extract(archive, record, stage)
            archives.append(archive)
        (stage / 'runtime-version.json').write_bytes(pins)
        if root.exists():
            # Only an incomplete runtime previously owned by this installer.
            if any(root.iterdir()):
                raise RuntimeError('Refusing to replace an existing unrecognized Android runtime')
            root.rmdir()
        stage.rename(root)
        for archive in archives:
            archive.unlink()
