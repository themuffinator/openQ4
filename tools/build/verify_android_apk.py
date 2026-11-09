#!/usr/bin/env python3
"""Verify a signed phone release, diagnostic emulator or Meta Quest headset APK against its stage.

Each variant has its own identity, ABI, signing role and manifest shape
(docs/dev/platform-recovery-review.md, Android selection and package
matrix). The Quest flavor is the phone build declared as an immersive OpenXR
headset app: the same native libraries (the OpenXR loader links into the
client), the openq4-headset marker asset, the head-tracking requirement, the
OpenXR permission and immersive category and the supported-device list.
Phone and emulator APKs must carry none of that, and no distributable APK
may carry the headset-free OpenXR test runtime."""
from __future__ import annotations

import argparse
from dataclasses import dataclass
import hashlib
import json
from pathlib import Path
import re
import ssl
import subprocess
import zipfile

from android_elf import android_abi, validate_library
from android_package import content_pack_names, package_manifest

@dataclass(frozen=True)
class Variant:
    abi: str
    package: str
    debuggable: bool
    headset: bool = False


# The Quest identity is the flavor's own until AND-05 settles distribution;
# it never shares the phone's, so neither APK can replace the other.
VARIANT_TABLE = {
    'phone': Variant('arm64-v8a', 'com.darkmatter.openq4', False),
    'emulator': Variant('x86_64', 'com.darkmatter.openq4.emulator', True),
    'quest': Variant('arm64-v8a', 'com.darkmatter.openq4.quest', False, headset=True),
}
# (abi, package, debuggable) for existing callers.
VARIANTS = {name: (v.abi, v.package, v.debuggable) for name, v in VARIANT_TABLE.items()}
SETUP_FONTS = {'fonts/marine.ttf', 'fonts/lowpixel.ttf'}
HEADSET_MARKER = 'openq4-headset'
XR_TEST_RUNTIME = 'libopenq4-xr-test-runtime.so'
# What makes an APK an immersive headset app; a Quest APK needs all of them and
# a phone or emulator APK none of the first three.
HEADSET_FEATURE = 'android.hardware.vr.headtracking'
HEADSET_PERMISSION = 'org.khronos.openxr.permission.OPENXR'
HEADSET_CATEGORIES = ('org.khronos.openxr.intent.category.IMMERSIVE_HMD', 'com.oculus.intent.category.VR')
HEADSET_DEVICES = 'com.oculus.supportedDevices'


def variant_settings(variant: str) -> tuple[str, str, bool]:
    try:
        return VARIANTS[variant]
    except KeyError:
        raise ValueError(f'Unsupported Android package variant: {variant!r}') from None


def variant_info(variant: str) -> Variant:
    variant_settings(variant)
    return VARIANT_TABLE[variant]


def manifest_elements(xmltree: str) -> list[tuple[str, dict[str, str]]]:
    """aapt's `dump xmltree` as (element, {attribute: raw value text}) in document order."""
    elements: list[tuple[str, dict[str, str]]] = []
    for line in xmltree.splitlines():
        text = line.strip()
        if text.startswith('E: '):
            elements.append((text[3:].split(' ', 1)[0], {}))
        elif text.startswith('A: ') and elements:
            match = re.match(r'A: (?:http://schemas\.android\.com/apk/res/android:|android:)?([\w.]+)(?:\(0x[0-9a-fA-F]+\))?=(.*)', text)
            if match:
                elements[-1][1][match.group(1)] = match.group(2)
    return elements


def _value(raw: str) -> str:
    match = re.match(r'"([^"]*)"', raw)
    return match.group(1) if match else raw


def verify_headset_manifest(xmltree: str, variant: str) -> None:
    """A Quest APK declares itself an immersive headset app; nothing else may."""
    headset = variant_info(variant).headset
    elements = manifest_elements(xmltree)
    names = [(element, _value(attributes.get('name', ''))) for element, attributes in elements]
    feature = [attributes for element, attributes in elements
               if element == 'uses-feature' and _value(attributes.get('name', '')) == HEADSET_FEATURE]
    immersive = [name for element, name in names if element == 'category' and name in HEADSET_CATEGORIES]
    permission = ('uses-permission', HEADSET_PERMISSION) in names
    devices = [attributes for element, attributes in elements
               if element == 'meta-data' and _value(attributes.get('name', '')) == HEADSET_DEVICES]
    if not headset:
        if feature or immersive or permission:
            raise ValueError(f'{variant} APK must not declare the OpenXR headset feature, permission or immersive category')
        return
    if not feature or not any(re.search(r'\(type 0x12\)0xffffffff', attributes.get('required', '')) for attributes in feature):
        raise ValueError('Quest APK must require head tracking (android.hardware.vr.headtracking)')
    if not permission:
        raise ValueError('Quest APK must request the OpenXR runtime permission')
    if set(immersive) != set(HEADSET_CATEGORIES):
        raise ValueError('Quest APK must open the game as an immersive OpenXR activity')
    if not devices or not _value(devices[0].get('value', '')):
        raise ValueError('Quest APK must name its supported headsets')


def libraries_for_abi(abi: str) -> set[str]:
    arch = android_abi(abi).module_arch
    return {
        'libquake4.so', 'libSDL3.so', f'librenderer-gles_{arch}.so',
        f'libgame-sp_{arch}.so', f'libgame-mp_{arch}.so', 'libopenal.so', 'libc++_shared.so',
    }


# Preserve the established phone verifier API for existing callers.
LIBRARIES = libraries_for_abi('arm64-v8a')


def digest(stream) -> str:
    result = hashlib.sha256()
    while chunk := stream.read(1024 * 1024):
        result.update(chunk)
    return result.hexdigest()


def verify_payload(apk: Path, stage: Path, version: str, variant: str = 'phone') -> None:
    abi, _, _ = variant_settings(variant)
    headset = variant_info(variant).headset
    metadata = json.loads((stage / 'android-build.json').read_text(encoding='utf-8'))
    mod = json.loads((stage / 'baseoq4/mod.json').read_text(encoding='utf-8'))
    # Format 1 originally described only phones. Missing variant is accepted
    # solely for those existing stages, never inferred from an emulator ABI.
    if (metadata.get('format') != 1 or metadata.get('abi') != abi
            or metadata.get('variant', 'phone') != variant or metadata.get('androidApi') != 24
            or not re.fullmatch(r'\d+\.\d+\.\d+', str(metadata.get('sdlVersion', '')))):
        raise ValueError(f'Expected {variant} Android {abi} API 24 metadata with an SDL version')
    if mod.get('version') != version or mod.get('requiredopenQ4Version') != version:
        raise ValueError('Staged engine and mod versions do not match the release')
    expected = {}
    libraries = libraries_for_abi(abi)
    if (stage / 'lib' / XR_TEST_RUNTIME).exists():
        raise ValueError('The OpenXR test runtime must never be staged for a distributable APK')
    if {path.name for path in (stage / 'lib').iterdir()} != libraries:
        raise ValueError(f'Stage must contain exactly the seven matching {abi} native libraries')
    for name in libraries:
        path = stage / 'lib' / name
        validate_library(path, 24, abi)
        expected[f'lib/{abi}/{name}'] = path
    for name in (*(f'baseoq4/{pack}' for pack in content_pack_names()), 'baseoq4/mod.json',
                 'licenses/sdl3/LICENSE.txt', 'licenses/openal-soft/COPYING',
                 'licenses/android-ndk/NOTICE.toolchain', 'licenses/QUAKE-4-SDK-EULA.rtf', 'licenses/MD4.txt'):
        if not (stage / name).is_file():
            raise ValueError(f'Missing required packaged file: {name}')
    for directory in ('baseoq4', 'licenses'):
        for path in (stage / directory).rglob('*'):
            if path.is_file():
                expected['assets/' + path.relative_to(stage).as_posix()] = path
    for name in ('LICENSE', 'LICENSING.md', 'CONTRIBUTORS.md', 'android-build.json'):
        expected['assets/' + name] = stage / name
    marker = stage / HEADSET_MARKER
    if headset != marker.is_file():
        raise ValueError(f'The {HEADSET_MARKER} marker belongs to Quest stages only' if not headset
                         else f'Quest stage must carry the {HEADSET_MARKER} marker asset')
    if headset:
        expected['assets/' + HEADSET_MARKER] = marker
    with zipfile.ZipFile(stage / 'baseoq4/pak0.pk4') as content:
        fonts = {}
        for name in sorted(SETUP_FONTS):
            matches = [entry for entry in content.infolist() if entry.filename == name]
            if len(matches) != 1 or not 0 < matches[0].file_size <= 4 * 1024 * 1024:
                raise ValueError(f'Missing, duplicate or oversized setup typeface: {name}')
            fonts['assets/' + name] = content.read(matches[0])
    with zipfile.ZipFile(apk) as archive:
        names = archive.namelist()
        if len(names) != len(set(names)):
            raise ValueError('APK contains duplicate entries')
        native = {name for name in names if name.startswith('lib/') and not name.endswith('/')}
        if any(name.rsplit('/', 1)[-1] == XR_TEST_RUNTIME for name in native):
            raise ValueError('APK must never carry the OpenXR test runtime')
        if native != {name for name in expected if name.startswith('lib/')}:
            raise ValueError(f'APK must contain exactly the seven matching {abi} native libraries')
        assets = {name for name in names if name.startswith('assets/') and not name.endswith('/')}
        if assets != {name for name in expected if name.startswith('assets/')} | set(fonts) | {
                'assets/openq4-package-version.txt', 'assets/openq4-package-manifest.txt'}:
            raise ValueError('APK asset set differs from the staged openQ4 overlays and notices')
        for name, data in fonts.items():
            if archive.getinfo(name).file_size != len(data) or archive.read(name) != data:
                raise ValueError(f'APK setup typeface differs from staged pak0: {name}')
        for name in (f'assets/baseoq4/{pack}' for pack in content_pack_names()):
            if archive.getinfo(name).compress_type != zipfile.ZIP_STORED:
                raise ValueError('PK4 archives must be stored without redundant APK compression')
        for name, path in expected.items():
            with archive.open(name) as packaged, path.open('rb') as staged:
                if digest(packaged) != digest(staged):
                    raise ValueError(f'APK payload differs from validated stage: {name}')
        manifest = package_manifest(stage / 'baseoq4')
        if archive.read('assets/openq4-package-manifest.txt') != manifest:
            raise ValueError('APK extraction manifest does not match its packaged data')
        if archive.read('assets/openq4-package-version.txt') != hashlib.sha256(manifest).hexdigest().encode('ascii'):
            raise ValueError('APK extraction revision does not match its packaged data')
        if archive.testzip() is not None:
            raise ValueError('APK ZIP integrity failure')


def verify_tools(apk: Path, tools: Path, certificate: Path, version: str, variant: str = 'phone') -> None:
    abi, package_id, debuggable = variant_settings(variant)
    def run(tool: str, *args: str) -> str:
        suffix = '.bat' if tool == 'apksigner' and (tools / 'apksigner.bat').exists() else ''
        result = subprocess.run([str(tools / (tool + suffix)), *args], check=True, capture_output=True, text=True)
        return result.stdout

    signature = run('apksigner', 'verify', '--verbose', '--print-certs', str(apk))
    expected_cert = hashlib.sha256(ssl.PEM_cert_to_DER_cert(certificate.read_text())).hexdigest()
    actual_certs = re.findall(r'Signer #\d+ certificate SHA-256 digest: ([0-9a-fA-F]+)', signature)
    if actual_certs != [expected_cert] or not re.search(r'Verified using v2 scheme.*: true', signature):
        raise ValueError(f'APK must have a valid v2 signature from the pinned {"release" if not debuggable else "diagnostic"} certificate')
    run('zipalign', '-c', '-P', '16', '4', str(apk))
    badging = run('aapt', 'dump', 'badging', str(apk))
    code = sum(int(part) * scale for part, scale in zip(version.split('.'), (1000000, 1000, 1)))
    for token in (f"package: name='{package_id}'", f"versionCode='{code}'", f"versionName='{version}'",
                  "sdkVersion:'24'", "targetSdkVersion:'35'", f"native-code: '{abi}'",
                  "launchable-activity: name='com.darkmatter.openq4.LauncherActivity'"):
        if token not in badging:
            raise ValueError(f'APK manifest mismatch: {token}')
    if not debuggable and 'application-debuggable' in badging:
        raise ValueError(f'{"Quest" if variant_info(variant).headset else "Release"} APK must not be debuggable')
    if debuggable and 'application-debuggable' not in badging:
        raise ValueError('Emulator APK must be a diagnostic debuggable build')
    manifest = run('aapt', 'dump', 'xmltree', str(apk), 'AndroidManifest.xml')
    if not re.search(r'android:extractNativeLibs\([^)]*\)=\(type 0x12\)0xffffffff', manifest):
        raise ValueError('APK must extract native libraries for runtime module loading')
    verify_headset_manifest(manifest, variant)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--apk', type=Path, required=True)
    parser.add_argument('--stage', type=Path, required=True)
    parser.add_argument('--version', required=True)
    parser.add_argument('--build-tools', type=Path, required=True)
    parser.add_argument('--certificate', type=Path, required=True)
    parser.add_argument('--variant', choices=sorted(VARIANTS), default='phone',
                        help='phone verifies the production release; emulator requires a separate debug package; '
                             'quest requires the immersive OpenXR headset flavor')
    args = parser.parse_args()
    if not re.fullmatch(r'\d{1,3}\.\d{1,3}\.\d{1,3}', args.version):
        parser.error('Invalid release version')
    verify_tools(args.apk, args.build_tools, args.certificate, args.version, args.variant)
    verify_payload(args.apk, args.stage, args.version, args.variant)
    print(f'Android {args.variant} APK verified: certificate, manifest, 16 KiB ELF/ZIP alignment, exact native/data/license payload.')


if __name__ == '__main__':
    main()
