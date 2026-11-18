#!/usr/bin/env python3
"""Build a normal-privilege application with the public OpenHarmony SDK test signer."""

import argparse
import json
from pathlib import Path
import re
import subprocess
import sys
import time
import uuid
import zipfile

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'image'))
from android_runtime import download

BUNDLE = 'org.olympium.validation.hap'
APP_CERT_CHAIN = {
    'path': 'Public OpenHarmony 6.1 SDK application certificate chain',
    'url': 'https://raw.githubusercontent.com/openharmony/developtools_hapsigner/'
           '36b5049dfae2c4a09fb7b3599a0d9759aa273504/dist/OpenHarmonyApplication.pem',
    'size': 2556,
    'algorithm': 'sha256',
    'checksum': 'b5c1d6254faa91eab645a32fc3ce136128c94d88a739a0e956e6a49b331387e5',
}
# Published SDK test keystore password. This package has no requested permissions.
SDK_TEST_PASSWORD = '123456'


def run(*args):
    subprocess.run(list(map(str, args)), check=True, timeout=120)


def make_resources(sdk, work):
    assets = work / 'assets'
    base = assets / 'resources/base'
    for name in ('element', 'profile', 'media'):
        (base / name).mkdir(parents=True)
    (base / 'element/string.json').write_text(json.dumps({
        'string': [{'name': 'app_name', 'value': 'Olympium HAP Probe'}]}))
    (base / 'element/color.json').write_text(json.dumps({
        'color': [{'name': 'white', 'value': '#FFFFFF'}]}))
    (base / 'profile/pages.json').write_text(json.dumps({'src': ['pages/Index']}))
    icon = Image.new('RGBA', (40, 40), '#2145a8')
    drawing = ImageDraw.Draw(icon)
    drawing.rounded_rectangle((6, 6, 34, 34), 6, fill='white')
    drawing.line([(13, 20), (18, 25), (28, 15)], fill='#2145a8', width=4)
    icon.save(base / 'media/probe.png')

    app = dict(bundleName=BUNDLE, vendor='Olympium', bundleType='app', versionName='1.0.0',
               versionCode=1000000, minAPIVersion=14, targetAPIVersion=20,
               compileSdkType='OpenHarmony', compileSdkVersion='6.1.0.31',
               apiReleaseType='Release', icon='$media:probe', label='$string:app_name')
    ability = dict(name='MainAbility', srcEntry='./ets/MainAbility.ets', exported=True,
                   launchType='singleton', icon='$media:probe', label='$string:app_name',
                   startWindowIcon='$media:probe', startWindowBackground='$color:white',
                   skills=[dict(entities=['entity.system.home'], actions=['action.system.home'])])
    module = dict(name='entry', packageName='entry', type='entry', mainElement='MainAbility',
                  deliveryWithInstall=True, installationFree=False, deviceTypes=['default', 'tablet'],
                  compileMode='esmodule', virtualMachine='ark12.0.6.0',
                  pages='$profile:pages', abilities=[ability])
    manifest = work / 'module.json'
    manifest.write_text(json.dumps(dict(app=app, module=module)))
    resources = work / 'resources'
    resources.mkdir()
    run(sdk / 'toolchains/restool', '-i', assets, '-p', BUNDLE, '-m', 'entry', '-j', manifest,
        '-o', resources, '-r', work / 'ResourceTable.js', '--icon-check')
    return resources


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk', type=Path, required=True)
    parser.add_argument('--work', type=Path, required=True, help='New directory for the fixture')
    args = parser.parse_args()
    sdk, work = args.sdk.resolve(), args.work.resolve()
    work.mkdir(parents=True)
    resources = make_resources(sdk, work)
    source = Path(__file__).resolve().parent / 'fresh-hap'
    compiler = sdk / 'ets/build-tools/ets-loader/bin/ark/build/bin/es2abc'
    files = work / 'filesInfo.txt'
    files.write_text(''.join(
        f'{source / filename};{BUNDLE}/entry/ets/{record};esm;'
        f'entry/src/main/ets/{record}.ets;entry;false;ets\n'
        for filename, record in (('MainAbility.js', 'MainAbility'), ('Index.js', 'pages/Index'))))
    abc = work / 'modules.abc'
    run(compiler, '--merge-abc', '--output', abc, '@' + str(files))

    pack = dict(summary=dict(
        app=dict(bundleName=BUNDLE, bundleType='app', version=dict(code=1000000, name='1.0.0')),
        modules=[dict(distro=dict(moduleType='entry', installationFree=False, deliveryWithInstall=True,
                                  moduleName='entry'),
                      deviceType=['default', 'tablet'], mainAbility='MainAbility',
                      apiVersion=dict(compatible=14, releaseType='Release', target=20),
                      abilities=[dict(name='MainAbility', label='$string:app_name')])]),
        packages=[dict(deviceType=['default', 'tablet'], moduleType='entry',
                       deliveryWithInstall=True, name='entry-default')])
    unsigned = work / 'unsigned.hap'
    with zipfile.ZipFile(unsigned, 'w', zipfile.ZIP_DEFLATED) as archive:
        archive.writestr('pack.info', json.dumps(pack))
        archive.write(abc, 'ets/modules.abc')
        for path in sorted(resources.rglob('*')):
            if path.is_file() and path.name != 'ResourceTable.txt':
                archive.write(path, path.relative_to(resources))

    lib = sdk / 'toolchains/lib'
    cert = download(APP_CERT_CHAIN, work)
    chain = re.findall(r'-----BEGIN CERTIFICATE-----.*?-----END CERTIFICATE-----',
                       cert.read_text(), re.S)
    if len(chain) != 3:
        raise RuntimeError('Unexpected OpenHarmony application certificate chain')
    certificates = work / 'application.pem'
    certificates.write_text('\n'.join(chain) + '\n')
    profile = json.loads((lib / 'UnsgnedReleasedProfileTemplate.json').read_text())
    now = int(time.time())
    profile.update(uuid=str(uuid.uuid4()), validity={'not-before': now - 86400,
                                                   'not-after': now + 30 * 86400})
    profile['bundle-info']['bundle-name'] = BUNDLE
    if profile['bundle-info']['apl'] != 'normal':
        raise RuntimeError('Unexpected SDK signing profile privilege')
    profile_path = work / 'profile.json'
    profile_path.write_text(json.dumps(profile))
    signed_profile = work / 'profile.p7b'
    signer = ('java', '-jar', lib / 'hap-sign-tool.jar')
    flags = ('-mode', 'localSign', '-signAlg', 'SHA256withECDSA', '-keystoreFile',
             lib / 'OpenHarmony.p12', '-keystorePwd', SDK_TEST_PASSWORD, '-keyPwd', SDK_TEST_PASSWORD)
    run(*signer, 'sign-profile', *flags, '-keyAlias', 'openharmony application profile release',
        '-profileCertFile', lib / 'OpenHarmonyProfileRelease.pem',
        '-inFile', profile_path, '-outFile', signed_profile)
    run(*signer, 'sign-app', *flags, '-keyAlias', 'openharmony application release',
        '-appCertFile', certificates, '-profileFile', signed_profile,
        '-inFile', unsigned, '-outFile', work / 'probe.hap', '-compatibleVersion', '14')


if __name__ == '__main__':
    main()
