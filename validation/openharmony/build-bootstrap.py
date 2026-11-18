#!/usr/bin/env python3
"""Build an owned SDK test client against pinned OpenHarmony platform headers."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--sdk', type=Path, required=True, help='Verified OpenHarmony 6.1 native SDK directory')
parser.add_argument('--sources', type=Path, required=True)
parser.add_argument('--system-image', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
source = Path(__file__).resolve().parent
sdk, sources, image, out = (p.resolve() for p in (args.sdk, args.sources, args.system_image, args.output))
sources.mkdir(parents=True, exist_ok=True)
out.mkdir(parents=True, exist_ok=True)


def run(*words):
    subprocess.run(list(map(str, words)), check=True, timeout=300)


pins = json.loads((source / 'sources.json').read_text())
for name, revision in pins.items():
    dest = sources / name
    if not dest.exists():
        run('git', 'init', dest)
        run('git', '-C', dest, 'fetch', '--depth', '1', 'https://github.com/openharmony/' + name + '.git', revision)
        run('git', '-C', dest, 'checkout', '--detach', 'FETCH_HEAD')
    actual = subprocess.check_output(['git', '-C', str(dest), 'rev-parse', 'HEAD'], text=True).strip()
    if actual != revision:
        raise SystemExit('SDK header revision mismatch: ' + name)
    run('git', '-C', dest, 'diff', '--exit-code', 'HEAD')

libraries = {
    'libscreenlock_client.z.so': '/system/lib64/platformsdk/libscreenlock_client.z.so',
    'libipc_single.z.so': '/system/lib64/platformsdk/libipc_single.z.so',
    'libutils.z.so': '/system/lib64/chipset-sdk-sp/libutils.z.so',
    'libc++.so': '/system/lib64/chipset-sdk-sp/libc++.so',
    'libnativetoken_shared.z.so': '/system/lib64/platformsdk/libnativetoken_shared.z.so',
    'libtokensetproc_shared.z.so': '/system/lib64/platformsdk/libtokensetproc_shared.z.so',
}
links = out / 'libraries'
links.mkdir(exist_ok=True)
for name, guest in libraries.items():
    target = links / name
    quoted = '"' + str(target).replace('\\', '\\\\').replace('"', '\\"') + '"'
    run('debugfs', '-R', 'dump ' + guest + ' ' + quoted, image)
    if not target.read_bytes().startswith(b'\x7fELF'):
        raise SystemExit('Publisher SDK platform library is not ELF: ' + name)

# The public NDK uses __n1; the publisher's internal platform libraries use
# __h. Keep the SDK's complete config, changing only this platform namespace.
config = (sdk / 'llvm/include/libcxx-ohos/include/c++/v1/__config_site').read_text()
old = '#define _LIBCPP_ABI_NAMESPACE __n1'
if config.count(old) != 1:
    raise SystemExit('Unexpected SDK C++ ABI configuration')
config_file = out / 'platform-cxx-config.h'
config_file.write_text(config.replace(old, '#define _LIBCPP_ABI_NAMESPACE __h'))
includes = [
    'theme_screenlock_mgr/interfaces/inner_api/include', 'theme_screenlock_mgr/frameworks/native/include',
    'commonlibrary_c_utils/base/include', 'communication_ipc/interfaces/innerkits/ipc_core/include',
    'security_access_token/interfaces/innerkits/nativetoken/include',
    'security_access_token/interfaces/innerkits/token_setproc/include',
    'powermgr_power_manager/interfaces/inner_api/native/include',
]
run(sdk / 'llvm/bin/clang++', '--target=x86_64-linux-ohos', '--sysroot=' + str(sdk / 'sysroot'),
    '-std=c++17', '-include', config_file, '-fno-rtti', '-fno-exceptions', '-nostdlib++',
    '-Wall', '-Wextra', '-Werror', *['-I' + str(sources / p) for p in includes],
    source / 'bootstrap.cpp', '-L' + str(links), '-Wl,--allow-shlib-undefined',
    *['-l:' + name for name in libraries], '-ldl', '-o', out / 'bootstrap')
run(sdk / 'llvm/bin/clang', '--target=x86_64-linux-ohos', '--sysroot=' + str(sdk / 'sysroot'),
    '-Wall', '-Wextra', '-Werror', source / 'account-client.c', '-ldl', '-o', out / 'account-client')
files = [source / 'bootstrap.cpp', source / 'account-client.c', out / 'bootstrap', out / 'account-client']
report = dict(source_revisions=pins, sha256={p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in files},
              platform_libraries={name: hashlib.sha256((links / name).read_bytes()).hexdigest() for name in libraries})
(out / 'build-result.json').write_text(json.dumps(report, indent=2) + '\n')
