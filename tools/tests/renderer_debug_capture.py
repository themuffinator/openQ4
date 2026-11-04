#!/usr/bin/env python3
"""Compare engine depth/intensity debug captures on OpenGL and Vulkan (VK-07).

Uses the original PBR laboratory's fixed scene and stable runtime. Windowed,
input-disabled, engine screenshots only; this is not presentation qualification.
"""
import argparse
import json
from pathlib import Path
import re
import shutil
import sys

import numpy as np
from PIL import Image

import renderer_smaa_orientation as smaa

lab = smaa.lab


def intensity(rgb):
    """Independent byte-domain contract, including the inherited black wrap."""
    peak = rgb.astype(np.int16).max(axis=2)
    low = peak < 128
    return np.stack((np.where(low, 2 * (128 - peak), 0),
                     np.where(low, 2 * peak, 2 * (255 - peak)),
                     np.where(low, 0, 2 * (peak - 128))), axis=2).astype(np.uint8)


def configure(manifest, samples):
    reference = smaa.configure(manifest, samples)
    first = reference['smaa-material0-hdr0-aa0']
    base = dict(first['settings'], r_showDepth='0', r_showIntensity='0', r_resolutionScaleMode='1')
    # Do not let first-run GPU detection apply a preset and restart the menu
    # renderer before our explicit scene settings are restored by autoexec.
    lab.BASE.update(r_showDepth='0', r_showIntensity='0',
                    com_performancePreset='balanced', ui_retained='1')
    profile = {}

    def add(name, scale, mode, commands=(), restore=None):
        profile[name] = dict(settings={**base, 'r_screenFraction': str(scale),
            'r_showDepth': str(int(mode == 'depth')), 'r_showIntensity': str(int(mode == 'intensity'))},
            mode=mode, scale=scale, commands=list(commands), restore=restore)

    for scale in (100, 50, 75, 125, 150, 200):
        for mode in ('off', 'depth', 'intensity'):
            add(f'debug-{scale}-{mode}', scale, mode, first['commands'] if not profile else ())
    for scale in (50, 75):
        for mode in ('off', 'depth', 'intensity'):
            # The game owns the scene target here, so mode 0 still resamples
            # that target. This does not exercise the renderer's legacy crop.
            name = f'debug-mode0-{scale}-{mode}'
            add(name, scale, mode)
            profile[name]['settings']['r_resolutionScaleMode'] = '0'
    add('debug-scale-restored', 100, 'off', restore='debug-100-off')
    for boundary in ('image-reload', 'partial-restart', 'full-restart'):
        for mode in ('intensity', 'depth', 'off'):
            add(f'debug-{boundary}-{mode}', 100, mode,
                lab.CASE_COMMANDS[boundary] if mode == 'intensity' else (), 'debug-100-' + mode)
    return profile


def qualify(report, profile, samples):
    failures, checks, images, captures = [], {}, {}, {}
    if not report.get('complete') or not report.get('runtimeUnchanged'):
        failures.append('incomplete run or changed runtime')
    if [row['case'] for row in report['results']] != list(profile):
        failures.append('case sequence differs from requested profile')
    for row in report['results']:
        name = row['case']
        spec = profile[name]
        failures.extend(name + ': ' + failure for failure in row['failures'])
        for kind in ('screenshot', 'log'):
            path = Path(row[kind])
            if not path.is_file() or lab.digest(path) != row['sha256'].get(kind):
                failures.append(name + ': missing/changed ' + kind)
        log = Path(row['log']).read_text(errors='replace')
        section = log.split(f'PBRLAB_{name}_BEGIN', 1)[-1].split(f'PBRLAB_{name}_END', 1)[0]
        if not re.search(rf'Renderer AA: MSAA requested={samples} effective={samples}\b', section):
            failures.append(name + ': effective MSAA differs')
        status = re.search(rf'DEBUG_TARGET_{re.escape(name)}_BEGIN\s*\n(.*?)'
                           rf'DEBUG_TARGET_{re.escape(name)}_END', log, re.S)
        extent = re.search(r'manualScale=(\d+)% effectiveScale=(\d+)% scene=(\d+)x(\d+) output=(\d+)x(\d+)',
                           status[1] if status else '')
        actual = list(map(int, extent.groups())) if extent else None
        scale = spec['scale']
        expected = [scale, scale, 1280 * scale // 100, 800 * scale // 100, 1280, 800]
        if actual != expected:
            failures.append(name + ': observed scene/output extent differs from requested scale')
        try:
            with Image.open(row['screenshot']) as image:
                rgb = np.asarray(image.convert('RGB'))
            if rgb.shape != (800, 1280, 3):
                raise ValueError('wrong capture extent')
        except (OSError, ValueError) as error:
            failures.append(name + ': ' + str(error))
            continue
        images[name] = rgb
        captures[name] = dict(path=row['screenshot'], sha256=row['sha256']['screenshot'])
        check = checks[name] = dict(presentationExtent=actual)
        if spec['mode'] == 'depth':
            grey = np.array_equal(rgb[:, :, 0], rgb[:, :, 1]) and np.array_equal(rgb[:, :, 0], rgb[:, :, 2])
            check.update(grayscale=grey, distinctLevels=int(len(np.unique(rgb))))
            if not grey or check['distinctLevels'] < 2:
                failures.append(name + ': missing or non-grayscale depth view')
        if spec['restore']:
            check['restoredExactly'] = np.array_equal(rgb, images.get(spec['restore']))
            if not check['restoredExactly']:
                failures.append(name + ': debug output did not recover exactly')
    baseline = images.get('debug-100-off')
    mapped = images.get('debug-100-intensity')
    if baseline is not None and mapped is not None:
        error = np.abs(intensity(baseline).astype(np.int16) - mapped.astype(np.int16))
        checks['debug-100-intensity']['maximumRampError'] = int(error.max())
        if error.max() > 2:
            failures.append('full-size intensity differs from the byte-domain ramp')
        if np.count_nonzero(baseline.max(axis=2) > 64) < 10000:
            failures.append('emissive specimen is missing from the baseline')
    for scale in (50, 75, 125, 150, 200):
        scaled = images.get(f'debug-{scale}-off')
        if baseline is not None and scaled is not None and np.array_equal(baseline, scaled):
            failures.append(f'{scale}% scale did not change scene sampling')
        for mode in ('off', 'depth', 'intensity'):
            name = f'debug-mode0-{scale}-{mode}'
            if name in images and f'debug-{scale}-{mode}' in images:
                same = np.array_equal(images[name], images[f'debug-{scale}-{mode}'])
                checks[name]['matchesResampledScene'] = same
                if not same:
                    failures.append(name + ': game-owned target changed with resolution mode')
    return dict(status='fail' if failures else 'pass', failures=failures, checks=checks, captures=captures)


def compare(paths):
    reports = [json.loads(path.read_text()) for path in paths]
    failures, checks = [], {}
    for backend, report in zip(('gl', 'vk'), reports):
        if report.get('debugCaptureProof', {}).get('status') != 'pass':
            failures.append(backend + ': independent capture qualification failed')
        if not report['results'] or any(row['backend'] != backend for row in report['results']):
            failures.append('comparison requires OpenGL then Vulkan')
    for key in ('runtimeSHA256', 'compiledMapSHA256', 'fixture', 'harnessSources',
                'debugCaptureProfile', 'requestedSamples'):
        if reports[0].get(key) != reports[1].get(key):
            failures.append(key + ': inputs differ')
    captures = [report.get('debugCaptureProof', {}).get('captures', {}) for report in reports]
    if not captures[0] or captures[0].keys() != captures[1].keys():
        failures.append('capture pairs are incomplete')
    for name in captures[0].keys() & captures[1].keys():
        arrays = []
        for source in captures:
            record = source[name]
            path = Path(record['path'])
            if not path.is_file() or lab.digest(path) != record['sha256']:
                failures.append(name + ': capture missing or changed')
                break
            with Image.open(path) as image:
                arrays.append(np.asarray(image.convert('RGB'), dtype=np.int16))
        if len(arrays) != 2 or arrays[0].shape != arrays[1].shape:
            failures.append(name + ': capture pair unavailable or extent differs')
            continue
        delta = np.abs(arrays[0] - arrays[1])
        checks[name] = dict(maximumError=int(delta.max()),
                           overTolerancePixels=int(np.count_nonzero(delta.max(axis=2) > 2)))
        if np.any(delta > 2):
            failures.append(name + ': exceeds two display levels')
    return dict(status='fail' if failures else 'pass', failures=failures, checks=checks,
                reports={str(path.resolve()): lab.digest(path) for path in paths})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ('runtime-root', 'output-dir', 'basepath'):
        parser.add_argument('--' + key, type=Path)
    parser.add_argument('--backend', choices=('gl', 'vk'))
    parser.add_argument('--samples', type=int, choices=(0, 4, 8), default=0)
    parser.add_argument('--compare', nargs=2, type=Path, metavar=('GL_REPORT', 'VK_REPORT'))
    parser.add_argument('--self-test', action='store_true')
    args = parser.parse_args()
    if args.self_test:
        colors = np.array([[[0, 0, 0], [127, 0, 0], [0, 128, 0], [0, 0, 255]]], dtype=np.uint8)
        assert intensity(colors).tolist() == [[[0, 0, 0], [2, 254, 0], [0, 254, 0], [0, 0, 254]]]
        print('Debug intensity oracle: byte endpoints, midpoint, channel choice and black wrap passed')
        return 0
    if args.output_dir is None:
        parser.error('--output-dir is required')
    if args.compare:
        proof = compare(args.compare)
        args.output_dir.mkdir(parents=True, exist_ok=True)
        (args.output_dir / 'comparison.json').write_text(json.dumps(proof, indent=2) + '\n')
        print(json.dumps(proof, indent=2))
        return int(proof['status'] != 'pass')
    if not all((args.runtime_root, args.basepath, args.backend)):
        parser.error('capture requires --runtime-root, --basepath and --backend')
    manifest = json.loads((args.runtime_root / 'pbr-lab.json').read_text())
    profile = configure(manifest, args.samples)
    for name, spec in profile.items():
        lab.CASES[name] = spec['settings']
        lab.CASE_CAMERAS[name] = 'sampling'
        lab.CASE_COMMANDS[name] = [*spec['commands'], 'wait 30',
            f'echo "DEBUG_TARGET_{name}_BEGIN"', 'rendererTemporalPresentationStatus',
            f'echo "DEBUG_TARGET_{name}_END"']
        lab.LEGACY_CASES.add(name)
    sys.argv = [__file__, '--runtime-root', str(args.runtime_root), '--output-dir', str(args.output_dir),
        '--basepath', str(args.basepath), '--backend', args.backend, '--camera', 'sampling',
        '--batch', '--cases', ','.join(profile), '--timeout', '900']
    if args.backend == 'gl':
        sys.argv.append('--gl-debug')
    code = lab.main()
    path = args.output_dir / 'report.json'
    report = json.loads(path.read_text())
    report.update(debugCaptureProfile=profile, requestedSamples=args.samples,
                  debugCaptureProof=qualify(report, profile, args.samples))
    for source in (Path(__file__), Path(smaa.__file__), Path(smaa.scene.__file__)):
        shutil.copy2(source, args.output_dir / 'harness' / source.name)
        report['harnessSources'][source.name] = lab.digest(source)
    path.write_text(json.dumps(report, indent=2) + '\n')
    proof = report['debugCaptureProof']
    print(json.dumps(dict(status=proof['status'], cases=len(profile), failures=proof['failures'])), flush=True)
    return int(bool(code or proof['failures']))


if __name__ == '__main__':
    raise SystemExit(main())
