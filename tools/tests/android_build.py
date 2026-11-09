#!/usr/bin/env python3
"""Check Android cross-toolchain validation and renderer-free engine source split."""

from __future__ import annotations

from pathlib import Path
import json
import io
import hashlib
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
import ssl
import zipfile
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/build"))
import android_cross
import prepare_android_deps
import android_elf
import verify_android_apk
import android_emulator_smoke
from android_package import package_manifest


def android_library(api: int = 24, alignment: int = 16384, machine: int = 183) -> bytes:
    """Small valid ELF fixture with one load segment and an NDK target note."""
    data = bytearray(200)
    data[:7] = b"\x7fELF\x02\x01\x01"
    struct.pack_into("<HHI", data, 16, 3, machine, 1)
    struct.pack_into("<Q", data, 32, 64)
    struct.pack_into("<HHH", data, 52, 64, 56, 2)
    struct.pack_into("<IIQQQQQQ", data, 64, 1, 5, 0, 0, 0, len(data), len(data), alignment)
    struct.pack_into("<IIQQQQQQ", data, 120, 4, 4, 176, 176, 176, 24, 24, 4)
    struct.pack_into("<III8sI", data, 176, 8, 4, 1, b"Android\0", api)
    return bytes(data)


def staged_package(stage: Path, variant: str) -> tuple[dict[str, bytes], dict[str, bytes]]:
    abi, _, _ = verify_android_apk.variant_settings(variant)
    target = android_elf.android_abi(abi)
    files = {'lib/' + name: android_library(machine=target.machine)
             for name in verify_android_apk.libraries_for_abi(abi)}
    faces = {name: ('typeface: ' + name).encode() for name in verify_android_apk.SETUP_FONTS}
    pack = io.BytesIO()
    with zipfile.ZipFile(pack, 'w') as archive:
        for name, data in faces.items():
            archive.writestr(name, data)
    files.update({
        'android-build.json': json.dumps({'format': 1, 'variant': variant, 'abi': abi,
                                          'androidApi': 24, 'sdlVersion': '3.4.18'}).encode(),
        'baseoq4/mod.json': b'{"version":"0.14.000","requiredopenQ4Version":"0.14.000"}',
        'baseoq4/pak0.pk4': pack.getvalue(), 'baseoq4/pak1.pk4': b'second pack', 'baseoq4/pak2.pk4': b'third pack',
        'LICENSE': b'engine license', 'LICENSING.md': b'component licenses', 'CONTRIBUTORS.md': b'credits',
        'licenses/QUAKE-4-SDK-EULA.rtf': b'SDK EULA', 'licenses/MD4.txt': b'MD4 notice',
        'licenses/sdl3/LICENSE.txt': b'SDL', 'licenses/openal-soft/COPYING': b'OpenAL',
        'licenses/android-ndk/NOTICE.toolchain': b'NDK',
    })
    if verify_android_apk.variant_info(variant).headset:
        files[verify_android_apk.HEADSET_MARKER] = b'# openQ4 Meta Quest build marker\n'
    entries = {}
    for name, data in sorted(files.items()):
        path = stage / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        entries[f'lib/{abi}/' + name[4:] if name.startswith('lib/') else 'assets/' + name] = data
    manifest = package_manifest(stage / 'baseoq4')
    entries['assets/openq4-package-manifest.txt'] = manifest
    entries['assets/openq4-package-version.txt'] = hashlib.sha256(manifest).hexdigest().encode()
    entries.update({'assets/' + name: data for name, data in faces.items()})
    return files, entries


def write_apk(apk: Path, entries: dict[str, bytes]) -> None:
    with zipfile.ZipFile(apk, 'w') as archive:
        for name, data in entries.items():
            archive.writestr(name, data)
        archive.writestr('AndroidManifest.xml', b'manifest checked by aapt')


class AndroidBuildTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        (ROOT / '.tmp').mkdir(exist_ok=True)

    def test_android_required_packs_follow_the_content_pack_table(self) -> None:
        from android_package import content_pack_names, required_package_files
        table = json.loads((ROOT / 'tools/build/content_packs.json').read_text(encoding='utf-8'))
        names = tuple(pack['name'] for pack in table['packs'])
        self.assertEqual(content_pack_names(), names)
        self.assertEqual(required_package_files(), {'mod.json', *names})
        java = (ROOT / 'mobile/android/app/src/main/java/com/darkmatter/openq4/PackageStorage.java').read_text(encoding='utf-8')
        listed = '{ ' + ', '.join(f'"{name}"' for name in ('mod.json', *names)) + ' }'
        self.assertIn(f'for (String required : new String[] {listed})', java,
                      'PackageStorage.java must require every pack in tools/build/content_packs.json')
        for gradle in ('mobile/android/package-manifest.gradle', 'mobile/android/app/build.gradle'):
            self.assertIn('openQ4ContentPackNames()', (ROOT / gradle).read_text(encoding='utf-8'), gradle)

    def test_emulator_file_check_distinguishes_absence_from_command_failure(self) -> None:
        probe = android_emulator_smoke.Probe.__new__(android_emulator_smoke.Probe)
        for code, stdout, stderr, expected in (
            (0, b'', b'', True), (1, b'', b'', False),
            (243, b'', b'run-as: exec failed for test: Permission denied', None),
            (1, b'', b'permission denied', None), (0, b'unexpected output', b'', None),
        ):
            with self.subTest(code=code, stderr=stderr):
                probe.adb = mock.Mock(return_value=subprocess.CompletedProcess([], code, stdout, stderr))
                if expected is None:
                    with self.assertRaisesRegex(RuntimeError, 'Private-file test failed'):
                        probe.private_test('-f', 'files/saves/baseoq4/autoexec.cfg')
                else:
                    self.assertIs(probe.private_test('-f', 'files/saves/baseoq4/autoexec.cfg'), expected)
                # Exercise the actual shell builtin on older Android, where
                # directly executing `test` through run-as fails.
                self.assertIn('sh', probe.adb.call_args.args)
                self.assertIn('-c', probe.adb.call_args.args)
        probe.adb = mock.Mock(return_value=subprocess.CompletedProcess([], 1, b'', b'permission denied'))
        with self.assertRaisesRegex(RuntimeError, 'Retail-file test failed'):
            probe.remote_sha('/sdcard/Android/data/com.darkmatter.openq4.emulator/files/q4base/pak001.pk4')

    def test_emulator_preflight_rejects_gles2_before_install_or_data_writes(self) -> None:
        probe = android_emulator_smoke.Probe.__new__(android_emulator_smoke.Probe)
        probe.report = {}
        properties = {
            'ro.kernel.qemu': b'1', 'sys.boot_completed': b'1',
            'ro.product.cpu.abi': b'x86_64', 'ro.opengles.version': b'131072',
            'ro.build.version.sdk': b'24', 'ro.build.version.release': b'7.0',
            'ro.build.fingerprint': b'API24 fixture',
        }
        calls = []

        def adb(*args, **kwargs):
            calls.append(args)
            if args[:2] == ('shell', 'getprop'):
                output = properties[args[2]]
            elif args == ('shell', 'id'):
                output = b'uid=0(root)'
            elif args in (('shell', 'pidof', android_emulator_smoke.PACKAGE), ('root',)):
                output = b''
            else:
                self.fail(f'Unexpected operation on an unsupported image: {args}')
            return subprocess.CompletedProcess([], 0, output, b'')

        probe.adb = adb
        with self.assertRaisesRegex(ValueError, 'required OpenGL ES 3.0'):
            probe.run()
        self.assertEqual(probe.report['ro.opengles.version'], '131072')
        self.assertNotIn('install', [args[0] for args in calls])

    def test_emulator_pose_reader_allows_draw_messages_but_not_later_measurements(self) -> None:
        sample = 'origin: (-5122.8 2847.2 -5819.8) angles: (4.3 -7.2 0.0)\n'
        log = 'EMULATOR_SAVE_RESTORED \nDeferred GenerateAllInteractions (RunFrame)\n' + sample + 'EMULATOR_SMOKE_COMPLETE\n'
        expected = [-5122.8, 2847.2, -5819.8, 4.3, -7.2, 0.0]
        self.assertEqual(android_emulator_smoke.pose_after_marker(log, 'SAVE_RESTORED'), expected)
        self.assertEqual(android_emulator_smoke.pose_after_marker(log + sample, 'SAVE_RESTORED'), expected)
        for bad in (log.replace(sample, ''),
                    log.replace(sample, sample * 2), log.replace('-5122.8', 'nan')):
            with self.assertRaises(ValueError):
                android_emulator_smoke.pose_after_marker(bad, 'SAVE_RESTORED')
        with self.assertRaises(ValueError):
            android_emulator_smoke.pose_after_marker('EMULATOR_SAVE_RESTORED\nEMULATOR_OTHER\n' + sample,
                                                    'SAVE_RESTORED')
        self.assertEqual(android_emulator_smoke.pose_after_marker(
            'EMULATOR_GAMEPLAY_READY\n(1.00 -2.50 3.00) 352.8\nEMULATOR_TOUCH_RELEASED\n',
            'GAMEPLAY_READY', 'mp'), [1.0, -2.5, 3.0, 352.8])

    def test_release_apk_payload_rejects_mixed_or_incomplete_packages(self) -> None:
        with tempfile.TemporaryDirectory(prefix='android-apk-', dir=ROOT / '.tmp') as temporary:
            stage = Path(temporary) / 'stage'
            files, entries = staged_package(stage, 'phone')
            apk = Path(temporary) / 'release.apk'

            def package(payload):
                write_apk(apk, payload)

            package(entries)
            verify_android_apk.verify_payload(apk, stage, '0.14.000')
            notice = stage / 'licenses/MD4.txt'
            notice.unlink()
            with self.assertRaisesRegex(ValueError, 'Missing required packaged file: licenses/MD4.txt'):
                verify_android_apk.verify_payload(apk, stage, '0.14.000')
            notice.write_bytes(files['licenses/MD4.txt'])
            for name, replacement, message in (
                ('lib/arm64-v8a/libquake4.so', b'old binary', 'differs from validated stage'),
                ('lib/arm64-v8a/libgame-sp_arm64.so', None, 'seven matching'),
                ('assets/baseoq4/pak0.pk4', b'old data', 'differs from validated stage'),
                ('assets/licenses/openal-soft/COPYING', None, 'asset set differs'),
                ('assets/licenses/QUAKE-4-SDK-EULA.rtf', None, 'asset set differs'),
                ('assets/licenses/MD4.txt', None, 'asset set differs'),
                ('assets/fonts/marine.ttf', None, 'asset set differs'),
                ('assets/fonts/lowpixel.ttf', b'wrong face', 'typeface differs'),
                ('assets/fonts/extra.ttf', b'unexpected face', 'asset set differs'),
                ('assets/q4base/pak001.pk4', b'retail data', 'asset set differs'),
                ('assets/openq4-package-version.txt', b'wrong revision', 'extraction revision'),
                ('assets/openq4-package-manifest.txt', b'wrong manifest', 'extraction manifest'),
                ('assets/openq4-package-manifest.txt', None, 'asset set differs'),
            ):
                with self.subTest(name=name):
                    changed = dict(entries)
                    if replacement is None:
                        changed.pop(name)
                    else:
                        changed[name] = replacement
                    package(changed)
                    with self.assertRaisesRegex(ValueError, message):
                        verify_android_apk.verify_payload(apk, stage, '0.14.000')
            package(entries)
            with self.assertRaisesRegex(ValueError, 'versions do not match'):
                verify_android_apk.verify_payload(apk, stage, '0.15.000')
            with zipfile.ZipFile(apk, 'w', compression=zipfile.ZIP_DEFLATED) as archive:
                for name, data in entries.items():
                    archive.writestr(name, data)
            with self.assertRaisesRegex(ValueError, 'without redundant APK compression'):
                verify_android_apk.verify_payload(apk, stage, '0.14.000')

    def test_emulator_payload_cannot_be_mistaken_for_phone_release(self) -> None:
        with tempfile.TemporaryDirectory(prefix='android-emulator-', dir=ROOT / '.tmp') as temporary:
            stage = Path(temporary) / 'stage'
            files, entries = staged_package(stage, 'emulator')
            apk = Path(temporary) / 'emulator.apk'
            write_apk(apk, entries)
            verify_android_apk.verify_payload(apk, stage, '0.14.000', 'emulator')
            with self.assertRaisesRegex(ValueError, 'Expected phone'):
                verify_android_apk.verify_payload(apk, stage, '0.14.000')
            for name, value in (
                ('lib/x86_64/libopenxr_loader.so', android_library(machine=62)),
                ('lib/x86_64/libgame-sp_arm64.so', android_library()),
                ('lib/arm64-v8a/libquake4.so', android_library()),
            ):
                write_apk(apk, dict(entries, **{name: value}))
                with self.assertRaisesRegex(ValueError, 'seven matching'):
                    verify_android_apk.verify_payload(apk, stage, '0.14.000', 'emulator')
            write_apk(apk, entries)
            # Correct filenames are not evidence of the target instruction set.
            library = stage / 'lib/libquake4.so'
            library.write_bytes(android_library())
            with self.assertRaisesRegex(ValueError, 'x86-64'):
                verify_android_apk.verify_payload(apk, stage, '0.14.000', 'emulator')
            library.write_bytes(files['lib/libquake4.so'])
            extra = stage / 'lib/libdebug_runtime.so'
            extra.write_bytes(android_library(machine=62))
            with self.assertRaisesRegex(ValueError, 'Stage must contain exactly'):
                verify_android_apk.verify_payload(apk, stage, '0.14.000', 'emulator')
            extra.unlink()
            metadata = json.loads(files['android-build.json'])
            for field, value in (
                ('format', 2), ('variant', None), ('variant', 'phone'), ('abi', 'arm64-v8a'),
                ('androidApi', 28), ('sdlVersion', None),
            ):
                changed = dict(metadata)
                if value is None:
                    changed.pop(field)
                else:
                    changed[field] = value
                (stage / 'android-build.json').write_text(json.dumps(changed))
                with self.assertRaisesRegex(ValueError, 'Expected emulator'):
                    verify_android_apk.verify_payload(apk, stage, '0.14.000', 'emulator')

    def test_existing_phone_metadata_remains_verifiable(self) -> None:
        with tempfile.TemporaryDirectory(prefix='android-legacy-stage-', dir=ROOT / '.tmp') as temporary:
            stage = Path(temporary) / 'stage'
            files, entries = staged_package(stage, 'phone')
            metadata = json.loads(files['android-build.json'])
            metadata.pop('variant')
            legacy = json.dumps(metadata).encode()
            (stage / 'android-build.json').write_bytes(legacy)
            entries['assets/android-build.json'] = legacy
            apk = Path(temporary) / 'phone.apk'
            write_apk(apk, entries)
            verify_android_apk.verify_payload(apk, stage, '0.14.000')

    def test_release_apk_manifest_and_signer_gates(self) -> None:
        certificate = ROOT / 'mobile/android/release-signing-cert.pem'
        cert_digest = hashlib.sha256(ssl.PEM_cert_to_DER_cert(certificate.read_text())).hexdigest()
        good = [
            f'Verified using v2 scheme (APK Signature Scheme v2): true\nSigner #1 certificate SHA-256 digest: {cert_digest}\n',
            '',
            "package: name='com.darkmatter.openq4' versionCode='14000' versionName='0.14.000'\nsdkVersion:'24'\ntargetSdkVersion:'35'\nnative-code: 'arm64-v8a'\nlaunchable-activity: name='com.darkmatter.openq4.LauncherActivity'\n",
            'A: android:extractNativeLibs(0x010104ea)=(type 0x12)0xffffffff',
        ]
        for index, replacement, message in (
            (-1, '', ''),
            (0, good[0].replace(cert_digest, '0' * 64), 'pinned release certificate'),
            (0, good[0].replace(': true', ': false'), 'valid v2 signature'),
            (2, good[2].replace("sdkVersion:'24'", "sdkVersion:'28'"), 'manifest mismatch'),
            (2, good[2].replace("versionCode='14000'", "versionCode='13001'"), 'manifest mismatch'),
            (2, good[2] + 'application-debuggable', 'not be debuggable'),
            (2, good[2].replace('.LauncherActivity', '.OpenQ4Activity'), 'manifest mismatch'),
            (3, good[3].replace('0xffffffff', '0x0'), 'extract native libraries'),
        ):
            outputs = good.copy()
            if index >= 0:
                outputs[index] = replacement
            with mock.patch.object(verify_android_apk.subprocess, 'run', side_effect=[
                subprocess.CompletedProcess([], 0, stdout=text) for text in outputs
            ]):
                if message:
                    with self.assertRaisesRegex(ValueError, message):
                        verify_android_apk.verify_tools(Path('release.apk'), Path('tools'), certificate, '0.14.000')
                else:
                    verify_android_apk.verify_tools(Path('release.apk'), Path('tools'), certificate, '0.14.000')

    def test_emulator_manifest_requires_separate_debug_identity_and_pinned_certificate(self) -> None:
        # The fixture certificate is just a pinned identity here; no private
        # signing key is used by this test.
        certificate = ROOT / 'mobile/android/release-signing-cert.pem'
        cert_digest = hashlib.sha256(ssl.PEM_cert_to_DER_cert(certificate.read_text())).hexdigest()
        good = [
            f'Verified using v2 scheme (APK Signature Scheme v2): true\nSigner #1 certificate SHA-256 digest: {cert_digest}\n',
            '',
            "package: name='com.darkmatter.openq4.emulator' versionCode='14000' versionName='0.14.000'\nsdkVersion:'24'\ntargetSdkVersion:'35'\nnative-code: 'x86_64'\nlaunchable-activity: name='com.darkmatter.openq4.LauncherActivity'\napplication-debuggable\n",
            'A: android:extractNativeLibs(0x010104ea)=(type 0x12)0xffffffff',
        ]
        for variant, index, replacement, message in (
            ('emulator', -1, '', ''),
            ('phone', -1, '', 'manifest mismatch'),
            ('emulator', 0, good[0].replace(cert_digest, '0' * 64), 'pinned diagnostic certificate'),
            ('emulator', 0, good[0].replace(': true', ': false'), 'valid v2 signature'),
            ('emulator', 2, good[2].replace('.openq4.emulator', '.openq4'), 'manifest mismatch'),
            ('emulator', 2, good[2].replace("'x86_64'", "'arm64-v8a'"), 'manifest mismatch'),
            ('emulator', 2, good[2].replace("sdkVersion:'24'", "sdkVersion:'28'"), 'manifest mismatch'),
            ('emulator', 2, good[2].replace('application-debuggable', ''), 'diagnostic debuggable build'),
        ):
            outputs = good.copy()
            if index >= 0:
                outputs[index] = replacement
            with self.subTest(variant=variant, index=index, message=message), mock.patch.object(
                    verify_android_apk.subprocess, 'run', side_effect=[
                        subprocess.CompletedProcess([], 0, stdout=text) for text in outputs]):
                if message:
                    with self.assertRaisesRegex(ValueError, message):
                        verify_android_apk.verify_tools(Path('emulator.apk'), Path('tools'), certificate, '0.14.000', variant)
                else:
                    verify_android_apk.verify_tools(Path('emulator.apk'), Path('tools'), certificate, '0.14.000', variant)

    def test_quest_flavor_payload_is_its_own_package(self) -> None:
        with tempfile.TemporaryDirectory(prefix='android-quest-', dir=ROOT / '.tmp') as temporary:
            stage = Path(temporary) / 'stage'
            files, entries = staged_package(stage, 'quest')
            apk = Path(temporary) / 'quest.apk'
            write_apk(apk, entries)
            verify_android_apk.verify_payload(apk, stage, '0.14.000', 'quest')
            with self.assertRaisesRegex(ValueError, 'Expected phone'):
                verify_android_apk.verify_payload(apk, stage, '0.14.000')
            # The headset-free test runtime never ships, staged or packaged.
            write_apk(apk, dict(entries, **{'lib/arm64-v8a/' + verify_android_apk.XR_TEST_RUNTIME: android_library()}))
            with self.assertRaisesRegex(ValueError, 'never carry the OpenXR test runtime'):
                verify_android_apk.verify_payload(apk, stage, '0.14.000', 'quest')
            runtime = stage / 'lib' / verify_android_apk.XR_TEST_RUNTIME
            runtime.write_bytes(android_library())
            write_apk(apk, entries)
            with self.assertRaisesRegex(ValueError, 'never be staged'):
                verify_android_apk.verify_payload(apk, stage, '0.14.000', 'quest')
            runtime.unlink()
            # The marker is what starts the game in VR: required here, packaged exactly.
            marker = stage / verify_android_apk.HEADSET_MARKER
            marker.unlink()
            with self.assertRaisesRegex(ValueError, 'must carry the openq4-headset marker'):
                verify_android_apk.verify_payload(apk, stage, '0.14.000', 'quest')
            marker.write_bytes(files[verify_android_apk.HEADSET_MARKER])
            changed = dict(entries)
            changed.pop('assets/' + verify_android_apk.HEADSET_MARKER)
            write_apk(apk, changed)
            with self.assertRaisesRegex(ValueError, 'asset set differs'):
                verify_android_apk.verify_payload(apk, stage, '0.14.000', 'quest')
            write_apk(apk, dict(entries, **{'assets/' + verify_android_apk.HEADSET_MARKER: b'other marker'}))
            with self.assertRaisesRegex(ValueError, 'differs from validated stage'):
                verify_android_apk.verify_payload(apk, stage, '0.14.000', 'quest')
        with tempfile.TemporaryDirectory(prefix='android-quest-phone-', dir=ROOT / '.tmp') as temporary:
            # A phone stage that carries the marker would start phones in VR.
            stage = Path(temporary) / 'stage'
            files, entries = staged_package(stage, 'phone')
            (stage / verify_android_apk.HEADSET_MARKER).write_bytes(b'marker')
            apk = Path(temporary) / 'phone.apk'
            write_apk(apk, dict(entries, **{'assets/' + verify_android_apk.HEADSET_MARKER: b'marker'}))
            with self.assertRaisesRegex(ValueError, 'belongs to Quest stages only'):
                verify_android_apk.verify_payload(apk, stage, '0.14.000')

    def test_quest_manifest_declares_an_immersive_headset_app(self) -> None:
        certificate = ROOT / 'mobile/android/release-signing-cert.pem'
        cert_digest = hashlib.sha256(ssl.PEM_cert_to_DER_cert(certificate.read_text())).hexdigest()
        android = 'http://schemas.android.com/apk/res/android'
        feature = ('    E: uses-feature (line=5)\n'
                   '      A: android:name(0x01010003)="android.hardware.vr.headtracking" (Raw: "android.hardware.vr.headtracking")\n'
                   '      A: android:required(0x0101028e)=(type 0x12)0xffffffff\n'
                   '      A: android:version(0x01010519)=(type 0x10)0x1\n')
        permission = ('    E: uses-permission (line=6)\n'
                      '      A: android:name(0x01010003)="org.khronos.openxr.permission.OPENXR" (Raw: "org.khronos.openxr.permission.OPENXR")\n')
        devices = ('        E: meta-data (line=20)\n'
                   '          A: android:name(0x01010003)="com.oculus.supportedDevices" (Raw: "com.oculus.supportedDevices")\n'
                   '          A: android:value(0x01010024)="quest2|questpro|quest3|quest3s" (Raw: "quest2|questpro|quest3|quest3s")\n')
        immersive = ''.join(f'            E: category (line=30)\n              A: {android}:name(0x01010003)="{name}" (Raw: "{name}")\n'
                            for name in verify_android_apk.HEADSET_CATEGORIES)
        application = ('    E: application (line=10)\n      A: android:extractNativeLibs(0x010104ea)=(type 0x12)0xffffffff\n'
                       '      E: activity (line=15)\n')
        tree = lambda *parts: f'N: android={android}\n  E: manifest (line=2)\n' + ''.join(parts)
        quest_tree = tree(feature, permission, application, devices, '          E: intent-filter (line=28)\n', immersive)
        badging = ("package: name='com.darkmatter.openq4.quest' versionCode='14000' versionName='0.14.000'\nsdkVersion:'24'\n"
                   "targetSdkVersion:'35'\nnative-code: 'arm64-v8a'\nlaunchable-activity: name='com.darkmatter.openq4.LauncherActivity'\n")
        signer = f'Verified using v2 scheme (APK Signature Scheme v2): true\nSigner #1 certificate SHA-256 digest: {cert_digest}\n'
        phone_badging = badging.replace('.openq4.quest', '.openq4')
        for variant, outputs, message in (
            ('quest', [signer, '', badging, quest_tree], ''),
            ('phone', [signer, '', badging, quest_tree], 'manifest mismatch'),
            ('quest', [signer, '', badging + 'application-debuggable\n', quest_tree], 'Quest APK must not be debuggable'),
            ('quest', [signer, '', badging, quest_tree.replace('(type 0x12)0xffffffff\n      A: android:version', '(type 0x12)0x0\n      A: android:version')],
             'require head tracking'),
            ('quest', [signer, '', badging, quest_tree.replace(feature, '')], 'require head tracking'),
            ('quest', [signer, '', badging, quest_tree.replace(permission, '')], 'OpenXR runtime permission'),
            ('quest', [signer, '', badging, quest_tree.replace('category.IMMERSIVE_HMD', 'category.HOME')], 'immersive OpenXR activity'),
            ('quest', [signer, '', badging, quest_tree.replace(devices, '')], 'supported headsets'),
            ('phone', [signer, '', phone_badging, tree(application)], ''),
            ('phone', [signer, '', phone_badging, tree(feature, application)], 'must not declare the OpenXR headset'),
            ('phone', [signer, '', phone_badging, tree(permission, application)], 'must not declare the OpenXR headset'),
            ('phone', [signer, '', phone_badging, tree(application, immersive)], 'must not declare the OpenXR headset'),
        ):
            with self.subTest(variant=variant, message=message), mock.patch.object(
                    verify_android_apk.subprocess, 'run', side_effect=[
                        subprocess.CompletedProcess([], 0, stdout=text) for text in outputs]):
                if message:
                    with self.assertRaisesRegex(ValueError, message):
                        verify_android_apk.verify_tools(Path('quest.apk'), Path('tools'), certificate, '0.14.000', variant)
                else:
                    verify_android_apk.verify_tools(Path('quest.apk'), Path('tools'), certificate, '0.14.000', variant)

    def test_library_validation_checks_loadability(self) -> None:
        (ROOT / ".tmp").mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(prefix="android-elf-test-", dir=ROOT / ".tmp") as temporary:
            library = Path(temporary) / "libfixture.so"
            library.write_bytes(android_library(api=21))
            self.assertEqual(android_elf.validate_library(library, 24), 21)
            for contents, error in (
                (android_library(api=28), "above the configured API 24"),
                (android_library(alignment=4096), "16 KiB"),
                (android_library(machine=62), "AArch64"),
                (android_library()[:130], "program headers"),
            ):
                library.write_bytes(contents)
                with self.assertRaisesRegex(ValueError, error):
                    android_elf.validate_library(library, 24)
            library.write_bytes(android_library(api=28))
            self.assertEqual(android_elf.validate_library(library, 28), 28)
            library.write_bytes(android_library(machine=62))
            self.assertEqual(android_elf.validate_library(library, 24, 'x86_64'), 24)
            for contents, error in (
                (android_library(), 'x86-64'),
                (android_library(api=28, machine=62), 'above the configured API 24'),
                (android_library(machine=62, alignment=4096), '16 KiB'),
            ):
                library.write_bytes(contents)
                with self.assertRaisesRegex(ValueError, error):
                    android_elf.validate_library(library, 24, 'x86_64')
            with self.assertRaisesRegex(ValueError, 'Unsupported Android ABI'):
                android_elf.validate_library(library, 24, 'x86')

    def test_dependency_preparation_preserves_and_requires_notices(self) -> None:
        self._check_dependency_preparation('arm64-v8a')

    def test_emulator_dependencies_use_their_own_toolchain_and_runtime(self) -> None:
        self._check_dependency_preparation('x86_64')

    def _check_dependency_preparation(self, abi: str) -> None:
        (ROOT / ".tmp").mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(prefix="android-deps-test-", dir=ROOT / ".tmp") as temporary:
            fixture = Path(temporary)
            ndk, sdl, openal = (fixture / name for name in ("ndk", "sdl", "openal"))
            sources = {
                ndk / "build/cmake/android.toolchain.cmake": "# toolchain\n",
                ndk / "NOTICE.toolchain": "C++ runtime notice\n",
                ndk / "NOTICE": "NDK notice\n",
                sdl / "CMakeLists.txt": "# SDL\n",
                sdl / "LICENSE.txt": "SDL notice\n",
                openal / "CMakeLists.txt": "# OpenAL\n",
                openal / "COPYING": "OpenAL notice\n",
                openal / "LICENSE-pffft": "PFFFT notice\n",
                openal / "fmt-11.2.0/LICENSE": "fmt notice\n",
                openal / "gsl/LICENSE": "GSL notice\n",
            }
            for path, contents in sources.items():
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(contents, encoding="utf-8")
            prefix = fixture / "prefix"
            target = android_elf.android_abi(abi)
            runtime = android_library(api=21, machine=target.machine)
            runtime_path = ndk / "toolchains/llvm/prebuilt/windows-x86_64/sysroot/usr/lib" / target.triple / "libc++_shared.so"
            for path in (
                runtime_path,
                prefix / "lib/libSDL3.so", prefix / "lib/libopenal.so",
            ):
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(runtime)
            argv = ["prepare_android_deps.py", "--ndk", str(ndk), "--sdl-source", str(sdl),
                    "--openal-source", str(openal), "--prefix", str(prefix),
                    "--build-root", str(fixture / "build"), "--api", "28", "--abi", abi]
            with mock.patch.object(sys, "argv", argv), mock.patch.object(prepare_android_deps.platform, "system", return_value="Windows"):
                with mock.patch.object(prepare_android_deps.subprocess, "run") as runner:
                    prepare_android_deps.main()
                # The dependency build must honor the same target API and
                # page-size settings for both independent native libraries.
                self.assertEqual(runner.call_count, 6)
                for call in (runner.call_args_list[0], runner.call_args_list[3]):
                    self.assertIn(f"-DANDROID_ABI={abi}", call.args[0])
                    self.assertIn("-DANDROID_PLATFORM=android-28", call.args[0])
                    self.assertIn("-DANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON", call.args[0])
                for relative, expected in {
                    "licenses/sdl3/LICENSE.txt": "SDL notice\n",
                    "licenses/openal-soft/COPYING": "OpenAL notice\n",
                    "licenses/openal-soft/LICENSE-fmt": "fmt notice\n",
                    "licenses/openal-soft/LICENSE-pffft": "PFFFT notice\n",
                    "licenses/openal-soft/LICENSE-gsl": "GSL notice\n",
                    "licenses/android-ndk/NOTICE.toolchain": "C++ runtime notice\n",
                }.items():
                    self.assertEqual((prefix / relative).read_text(encoding="utf-8"), expected)
                self.assertEqual((prefix / "lib/libc++_shared.so").read_bytes(), runtime)
                runtime_path.write_bytes(android_library(machine=62 if abi == 'arm64-v8a' else 183))
                with mock.patch.object(prepare_android_deps.subprocess, "run") as runner:
                    with mock.patch("sys.stderr"), self.assertRaises(SystemExit) as error:
                        prepare_android_deps.main()
                    self.assertEqual(error.exception.code, 2)
                    runner.assert_not_called()
                runtime_path.write_bytes(runtime)
                (sdl / "LICENSE.txt").unlink()
                with mock.patch.object(prepare_android_deps.subprocess, "run") as runner:
                    with mock.patch("sys.stderr"), self.assertRaises(SystemExit) as error:
                        prepare_android_deps.main()
                    self.assertEqual(error.exception.code, 2)
                    runner.assert_not_called()

    @unittest.skipUnless(os.name == "nt", "Windows Meson wrapper")
    def test_windows_wrapper_refreshes_android_game_sources(self) -> None:
        (ROOT / ".tmp").mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(prefix="android-wrapper-test-", dir=ROOT / ".tmp") as temporary:
            fixture = Path(temporary)
            project = fixture / "openQ4"
            game = project
            build = project / "builddir"
            scripts = project / "tools/build"
            scripts.mkdir(parents=True)
            for filename in ("meson_setup.ps1", "game_source_inventory.py"):
                shutil.copy2(ROOT / "tools/build" / filename, scripts / filename)
            for module in ("game", "mpgame"):
                (game / "src" / module).mkdir(parents=True)
                (game / "src" / module / "Game_local.cpp").write_text("// game\n", encoding="utf-8")
            (build / "meson-private").mkdir(parents=True)
            (build / "meson-info").mkdir()
            (build / "meson-private/coredata.dat").touch()
            (build / "build.ninja").touch()
            (build / "meson-info/intro-machines.json").write_text(
                json.dumps({"host": {"system": "android", "cpu_family": "aarch64"}}), encoding="utf-8")
            (build / "meson-info/intro-buildoptions.json").write_text(
                json.dumps([{"name": "build_engine", "value": True}, {"name": "build_games", "value": True}]), encoding="utf-8")
            # Record wrapper dispatch without loading MSVC or running a compiler.
            fake_meson = fixture / "python/mesonbuild"
            fake_meson.mkdir(parents=True)
            (fake_meson / "__init__.py").touch()
            log = fixture / "meson.jsonl"
            (fake_meson / "mesonmain.py").write_text(
                "import json, os, sys\n"
                "if __name__ == '__main__':\n"
                "    with open(os.environ['OPENQ4_TEST_MESON_LOG'], 'a') as output:\n"
                "        output.write(json.dumps(sys.argv[1:]) + '\\n')\n", encoding="utf-8")
            env = os.environ.copy()
            env.update({"PYTHONPATH": str(fake_meson.parent), "OPENQ4_GAMELIBS_REPO": str(game),
                        "OPENQ4_TEST_MESON_LOG": str(log)})

            def invoke() -> list[list[str]]:
                log.unlink(missing_ok=True)
                result = subprocess.run(["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
                                         str(scripts / "meson_setup.ps1"), "compile", "-Cbuilddir"],
                                        cwd=project, env=env, capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                return [json.loads(line) for line in log.read_text().splitlines()]

            self.assertEqual([call[0] for call in invoke()], ["setup", "compile"])
            inventory_path = build / 'openq4_game_sources.json'
            generated = subprocess.run([sys.executable, str(scripts / 'game_source_inventory.py'), str(project)],
                                       check=True,capture_output=True,text=True)
            inventory_path.write_text(generated.stdout,encoding='utf-8')
            self.assertEqual([call[0] for call in invoke()], ["compile"])
            # An edit refreshes the provenance snapshot in place without a reconfigure.
            before = json.loads(inventory_path.read_text(encoding="utf-8"))
            (game / "src/game/Game_local.cpp").write_text("// changed game\n", encoding="utf-8")
            self.assertEqual([call[0] for call in invoke()], ["compile"])
            after = json.loads(inventory_path.read_text(encoding="utf-8"))
            self.assertNotEqual(before["sourceDigest"], after["sourceDigest"])
            self.assertEqual(after["sourceDigest"], json.loads(subprocess.run(
                [sys.executable, str(scripts / 'game_source_inventory.py'), str(project)],
                check=True, capture_output=True, text=True).stdout)["sourceDigest"])
            # A new source file changes what Meson compiles, so it reconfigures.
            (game / "src/mpgame/Added.cpp").write_text("// new game file\n", encoding="utf-8")
            self.assertEqual([call[0] for call in invoke()], ["setup", "compile"])

    def test_ndk_target_and_missing_tools(self) -> None:
        (ROOT / ".tmp").mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(prefix="android-build-test-", dir=ROOT / ".tmp") as temporary:
            ndk = Path(temporary)
            toolchain = ndk / "toolchains/llvm/prebuilt/windows-x86_64"
            binaries = toolchain / "bin"
            binaries.mkdir(parents=True)
            for name in ("clang.exe", "clang++.exe", "llvm-ar.exe", "llvm-strip.exe"):
                (binaries / name).touch()
            (toolchain / "sysroot/usr/lib/aarch64-linux-android/24").mkdir(parents=True)
            contents = android_cross.generate(ndk, 24, "windows-x86_64")
            self.assertIn("--target=aarch64-linux-android24", contents)
            self.assertIn("system = 'android'", contents)
            self.assertIn("needs_exe_wrapper = true", contents)
            self.assertNotIn(".cmd", contents)
            self.assertIn("max-page-size=16384", contents)
            with self.assertRaisesRegex(ValueError, "API 24"):
                android_cross.generate(ndk, 23, "windows-x86_64")
            with self.assertRaisesRegex(ValueError, "requested arm64-v8a API"):
                android_cross.generate(ndk, 25, "windows-x86_64")
            with self.assertRaisesRegex(ValueError, "requested x86_64 API"):
                android_cross.generate(ndk, 24, "windows-x86_64", "x86_64")
            (toolchain / "sysroot/usr/lib/x86_64-linux-android/24").mkdir(parents=True)
            emulator = android_cross.generate(ndk, 24, "windows-x86_64", "x86_64")
            self.assertIn("--target=x86_64-linux-android24", emulator)
            self.assertIn("cpu_family = 'x86_64'", emulator)
            self.assertNotIn("aarch64", emulator)
            self.assertIn("max-page-size=16384", emulator)
            with self.assertRaisesRegex(ValueError, "Unsupported Android ABI"):
                android_cross.generate(ndk, 24, "windows-x86_64", "x86")
            (binaries / "clang.exe").unlink()
            with self.assertRaisesRegex(ValueError, "Missing NDK tool"):
                android_cross.generate(ndk, 24, "windows-x86_64")

    def test_module_only_android_sources(self) -> None:
        command = [sys.executable, str(ROOT / "tools/build/meson_sources.py"),
                   "--host-system", "android", "--include-game", "false", "--renderer", "module"]
        result = subprocess.run(command, capture_output=True, text=True, check=True)
        sources = result.stdout.splitlines()
        self.assertEqual([p for p in sources if p.startswith("src/renderer/")],
                         ["src/renderer/RendererModule.cpp"])
        self.assertIn("src/sys/android/android_main.cpp", sources)
        self.assertIn("src/sys/android/android_sdl3.cpp", sources)
        self.assertFalse(any("/sys/linux/" in p or "/sys/win32/" in p for p in sources))
        for rejected in (["--platform-backend", "native"], ["--target-kind", "dedicated"]):
            result = subprocess.run(command + rejected, capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("Android requires the SDL3 client", result.stderr)


if __name__ == "__main__":
    unittest.main()
