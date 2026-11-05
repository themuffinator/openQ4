#!/usr/bin/env python3
"""Verify a signed phone release or diagnostic emulator APK against its stage."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import ssl
import subprocess
import zipfile

from android_elf import android_abi, validate_library
from android_package import content_pack_names, package_manifest

VARIANTS = {
    'phone': ('arm64-v8a', 'com.darkmatter.openq4', False),
    'emulator': ('x86_64', 'com.darkmatter.openq4.emulator', True),
}
SETUP_FONTS = {'fonts/marine.ttf', 'fonts/lowpixel.ttf'}


def variant_settings(variant: str) -> tuple[str, str, bool]:
    try:
        return VARIANTS[variant]
    except KeyError:
        raise ValueError(f'Unsupported Android package variant: {variant!r}') from None


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
        raise ValueError('Release APK must not be debuggable')
    if debuggable and 'application-debuggable' not in badging:
        raise ValueError('Emulator APK must be a diagnostic debuggable build')
    manifest = run('aapt', 'dump', 'xmltree', str(apk), 'AndroidManifest.xml')
    if not re.search(r'android:extractNativeLibs\([^)]*\)=\(type 0x12\)0xffffffff', manifest):
        raise ValueError('APK must extract native libraries for runtime module loading')


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--apk', type=Path, required=True)
    parser.add_argument('--stage', type=Path, required=True)
    parser.add_argument('--version', required=True)
    parser.add_argument('--build-tools', type=Path, required=True)
    parser.add_argument('--certificate', type=Path, required=True)
    parser.add_argument('--variant', choices=sorted(VARIANTS), default='phone',
                        help='phone verifies the production release; emulator requires a separate debug package')
    args = parser.parse_args()
    if not re.fullmatch(r'\d{1,3}\.\d{1,3}\.\d{1,3}', args.version):
        parser.error('Invalid release version')
    verify_tools(args.apk, args.build_tools, args.certificate, args.version, args.variant)
    verify_payload(args.apk, args.stage, args.version, args.variant)
    print(f'Android {args.variant} APK verified: certificate, manifest, 16 KiB ELF/ZIP alignment, exact native/data/license payload.')


if __name__ == '__main__':
    main()
