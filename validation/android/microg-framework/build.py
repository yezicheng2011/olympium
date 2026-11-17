#!/usr/bin/env python3
"""Build a pinned, experimental client-query hook; never deploys it to a device."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import zipfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--services-jar', type=Path, required=True)
parser.add_argument('--sdk', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
source = Path(__file__).resolve().parent
profile = json.loads((source / 'profile.json').read_text())
original = args.services_jar.resolve(strict=True)
if hashlib.sha256(original.read_bytes()).hexdigest() != profile['original_services_jar_sha256']:
    raise SystemExit('Refusing to alter a framework other than the pinned Android 17 build')
out = args.output.resolve()
out.mkdir(parents=True, exist_ok=True)
smali = out / 'smali'
if smali.exists():
    raise SystemExit('Use a fresh output directory for the framework build')


def run(*command):
    subprocess.run(list(map(str, command)), check=True)


target = profile['target_dex']
with zipfile.ZipFile(original) as archive:
    dex = out / 'original.dex'
    dex.write_bytes(archive.read(target))
run('baksmali', 'disassemble', dex, '-o', smali)
engine = smali / 'com/android/server/pm/ComputerEngine.smali'
text = engine.read_text()
signature = '(Lcom/android/server/pm/pkg/PackageStateInternal;JI)Landroid/content/pm/PackageInfo;'
method = '.method public final generatePackageInfo' + signature
renamed = 'generatePackageInfoWithoutOlympiumCompat'
if text.count(method) != 1 or renamed in text:
    raise SystemExit('Unexpected ComputerEngine API; no framework patch was produced')
# Wrap the existing implementation instead of changing its register allocation,
# branches, exception handlers or the private package signing state.
text = text.replace(method, '.method public final ' + renamed + signature)
text += f'''
{method}
    .registers 7
    invoke-virtual/range {{p0 .. p4}}, Lcom/android/server/pm/ComputerEngine;->{renamed}{signature}
    move-result-object v0
    invoke-static {{v0, p1}}, Lorg/olympium/android/compat/MicrogSignatureCompat;->rewrite(Landroid/content/pm/PackageInfo;Ljava/lang/Object;)Landroid/content/pm/PackageInfo;
    move-result-object v0
    return-object v0
.end method
'''
engine.write_text(text)
run('smali', 'assemble', smali, '-o', out / 'patched.dex')
classes, dex_out = out / 'classes', out / 'helper-dex'
classes.mkdir()
dex_out.mkdir()
platform = args.sdk.resolve() / 'platforms/android-37.0/android.jar'
tools = args.sdk.resolve() / 'build-tools/37.0.0'
run('javac', '--release', '8', '-classpath', platform, '-d', classes, *sorted(source.glob('*.java')))
run('jar', '--create', '--file', out / 'helper.jar', '-C', classes, '.')
run(tools / 'd8', '--lib', platform, '--min-api', '37', '--output', dex_out, out / 'helper.jar')
with zipfile.ZipFile(original) as archive, zipfile.ZipFile(out / 'unaligned.jar', 'w') as result:
    names = archive.namelist()
    indexes = [1 if name == 'classes.dex' else int(re.fullmatch(r'classes(\d+)\.dex', name)[1])
               for name in names if re.fullmatch(r'classes(?:\d+)?\.dex', name)]
    next_index = max(indexes) + 1
    for entry in archive.infolist():
        result.writestr(entry, (out / 'patched.dex').read_bytes() if entry.filename == target else archive.read(entry))
    helper_files = sorted(dex_out.glob('classes*.dex'))
    if len(helper_files) != 1:
        raise SystemExit('Unexpected helper DEX count')
    result.writestr(f'classes{next_index}.dex', helper_files[0].read_bytes())
run(tools / 'zipalign', '-f', '4', out / 'unaligned.jar', out / 'services.jar')
report = dict(profile=profile, status='built-only-not-runtime-verified',
              original_sha256=hashlib.sha256(original.read_bytes()).hexdigest(),
              modified_sha256=hashlib.sha256((out / 'services.jar').read_bytes()).hexdigest(),
              source_sha256={p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                             for p in sorted(source.glob('*.java'))})
(out / 'build-result.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report, indent=2))
