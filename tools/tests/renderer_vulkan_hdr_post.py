#!/usr/bin/env python3
"""Qualify prepared native HDR bloom/exposure on a static real-map specimen."""
import argparse
import json
import math
from pathlib import Path
import re
import shutil
import sys

import numpy as np
from PIL import Image

import renderer_vulkan_hdr_scene as scene

lab = scene.lab


def configure(manifest, samples):
    base = next(iter(scene.configure_composition(manifest, samples).values()))
    defaults = dict(r_bloomIntensity='.5', r_bloomThreshold='0', r_bloomSoftKnee='0',
                    r_bloomMipCount='1', r_bloomRadius='1', r_vkHDRPrepareFailure='0',
                    r_screenFraction='100')
    lab.BASE.update(defaults)
    settings = {**base['settings'], **defaults, 'r_hdrAutoExposure': '0',
                'r_hdrAutoExposureAsync': '1', 'r_hdrMinExposure': '1', 'r_hdrMaxExposure': '1',
                'r_hdrAdaptUpSpeed': '16', 'r_hdrAdaptDownSpeed': '16', 'r_bloom': '0'}
    profile = {}

    def add(name, overrides=None, commands=None, reference=None, pair=True, centre_y=400):
        profile['linear-post-' + name] = dict(settings={**settings, **(overrides or {})},
            commands=commands or [], reference='linear-post-' + reference if reference else None,
            pair=pair, centreY=centre_y)

    add('manual', commands=base['commands'])
    add('bloom-one', {'r_bloom': '1'})
    add('bloom-five', {'r_bloom': '1', 'r_bloomMipCount': '5'})
    add('bloom-wide', {'r_bloom': '1', 'r_bloomMipCount': '5', 'r_bloomRadius': '2'})
    add('bloom-threshold', {'r_bloom': '1', 'r_bloomThreshold': '.25'})
    add('bloom-knee', {'r_bloom': '1', 'r_bloomThreshold': '.5', 'r_bloomSoftKnee': '.5'})
    add('bloom-rejected', {'r_bloom': '1', 'r_bloomThreshold': '2'}, reference='manual')
    add('bloom-zero', {'r_bloom': '1', 'r_bloomIntensity': '0'}, reference='manual')
    add('manual-restored', reference='manual')
    for label, exposure in (('quarter', '.25'), ('one', '1'), ('two', '2')):
        for asynchronous in (0, 1):
            for bloom in (0, 1):
                add(f'auto-{label}-{asynchronous}-{bloom}', dict(r_hdrAutoExposure='1',
                    r_hdrAutoExposureAsync=str(asynchronous), r_hdrMinExposure=exposure,
                    r_hdrMaxExposure=exposure, r_bloom=str(bloom)))
    for asynchronous in (0, 1):
        add('auto-free-' + str(asynchronous), dict(r_hdrAutoExposure='1',
            r_hdrAutoExposureAsync=str(asynchronous), r_hdrMinExposure='.01', r_hdrMaxExposure='8'))
    combined = dict(r_hdrAutoExposure='1', r_bloom='1')
    add('fault-baseline', combined, reference='bloom-one')
    for fault in (1, 2, 3):
        add('fault-' + str(fault), {**combined, 'r_vkHDRPrepareFailure': str(fault)},
            reference='fault-1' if fault != 1 else None, pair=False)
    add('fault-restored', combined, reference='fault-baseline')
    for boundary, command in (('glsl-reload', 'reloadGLSLprograms'), ('arb-glsl-reload', 'reloadARBprograms')):
        add(boundary, combined, commands=[command], reference='fault-baseline')
    for boundary in ('image-reload', 'partial-restart', 'full-restart'):
        add(boundary, combined, commands=lab.CASE_COMMANDS[boundary], reference='fault-baseline')
    add('scaled', {**combined, 'r_screenFraction': '75'})
    def move_specimen(z):
        return ['g_stopTime 0', f'script "$linear_specimen_0.setOrigin(\'0 -700 {z}\')"',
                'wait 60', 'g_stopTime 1']

    # A centred sphere nearly hides a vertically inverted render texture.
    # Exercise asymmetric scene positions through both linear and fallback
    # ownership, with a CPU position oracle in addition to full-frame parity.
    add('scaled-above', {**combined, 'r_screenFraction': '75'}, move_specimen(440), centre_y=318)
    add('scaled-fallback-above', {**combined, 'r_screenFraction': '75', 'r_vkHDRPrepareFailure': '3'},
        pair=False, centre_y=318)
    add('scaled-below', {**combined, 'r_screenFraction': '75'}, move_specimen(320), centre_y=483)
    add('scale-restored', combined, move_specimen(380), reference='fault-baseline')
    add('resize', combined, commands=[
        'r_customWidth 960', 'r_customHeight 600', 'r_windowWidth 960', 'r_windowHeight 600',
        'vid_restart partial', 'wait 90', 'echo HDR_POST_SMALL_BEGIN', 'rendererVulkanHDRInfo',
        'screenshot "screenshots/linear-post-small.tga"', 'echo HDR_POST_SMALL_END',
        'r_customWidth 1280', 'r_customHeight 800', 'r_windowWidth 1280', 'r_windowHeight 800',
        'vid_restart partial', 'wait 90'], reference='fault-baseline', pair=False)
    add('capture-stability', combined, reference='fault-baseline')
    add('cube-capture-restored', combined, commands=['envshot "linear-post-cube" 256 1', 'wait 60'],
        reference='fault-baseline')
    return profile


def fields(line):
    return dict(re.findall(r'(\w+)=([^\s]+)', line))


def qualify(report, profile, backend, samples):
    failures, checks, images, states = [], {}, {}, {}
    if not report['complete'] or not report['runtimeUnchanged']:
        failures.append('incomplete capture sequence or changed runtime')
    for row in report['results']:
        name, spec = row['case'], profile[row['case']]
        if row['failures'] or row['diagnostics']:
            failures.append(f'{name}: {row["failures"]} {row["diagnostics"]}')
            continue
        for key in ('screenshot', 'log'):
            if lab.digest(Path(row[key])) != row['sha256'][key]:
                failures.append(name + ': changed ' + key)
        with Image.open(row['screenshot']) as raw:
            rgb = np.asarray(raw.convert('RGB')).copy()
        images[name] = rgb
        settings = spec['settings']
        automatic = settings['r_hdrAutoExposure'] == '1'
        fault = int(settings['r_vkHDRPrepareFailure']) if backend == 'vk' else 0
        expected_linear = str(int(fault == 0))
        if backend == 'vk':
            text = Path(row['log']).read_text(errors='replace')
            match = re.search(rf'(?m)^HDR_POST_{re.escape(name)}_BEGIN\s*$([\s\S]*?)^HDR_POST_{re.escape(name)}_END\s*$', text)
            section = match[1] if match else ''
            state = fields(next((line for line in section.splitlines() if line.startswith('Vulkan HDR:')), ''))
            owner = fields(next((line for line in row['telemetry'] if line.startswith('Vulkan HDR scene ownership:')), ''))
            expected = dict(committed=expected_linear, linearActive='0', requested='1',
                reason=('complete', 'exposure-resources', 'bloom-resources', 'output-resources')[fault])
            checks[name + '/ownership'] = dict(expected=expected, actual=owner)
            if any(owner.get(k) != v for k, v in expected.items()):
                failures.append(name + ': incorrect complete-view ownership')
            extent = '960x600' if settings['r_screenFraction'] == '75' else '1280x800'
            expected_state = dict(sceneFormat='RGBA16F', linearScene=expected_linear,
                autoExposure=str(int(automatic)), initialized=str(int(automatic)), samples=str(samples), extent=extent)
            if any(state.get(k) != v for k, v in expected_state.items()):
                failures.append(name + ': incorrect exposure/storage state ' + str(state))
            if automatic and (int(state.get('completed', '0')) <= 0 or int(state.get('queued', '0')) < int(state.get('completed', '0'))):
                failures.append(name + ': no real completed luminance samples')
            exposure = float(state.get('exposure', 'nan')) if automatic else 1.0
        else:
            state = row['hdrState']
            if state.get('linear') != 1 or state.get('auto') != int(automatic) or (automatic and state.get('initialized') != 1):
                failures.append(name + ': GL reference did not own linear HDR/exposure')
            exposure = state.get('adapted', float('nan')) if automatic else 1.0
            text = Path(row['log']).read_text(errors='replace')
            section = text.split(f'PBRLAB_{name}_BEGIN', 1)[-1].split(f'PBRLAB_{name}_END', 1)[0]
            graph = fields(next((line for line in section.splitlines()
                if line.startswith('Renderer graph extent:')), ''))
            extent = '960x600' if settings['r_screenFraction'] == '75' else '1280x800'
            checks[name + '/graph-extent'] = dict(expected=extent, actual=graph)
            if graph.get('scene') != extent or graph.get('native') != '1280x800':
                failures.append(name + ': GL graph did not rasterize at the requested scene extent')
            msaa = fields(next((line for line in row['telemetry']
                if line.startswith('Modern scene MSAA:')), ''))
            checks[name + '/graph-msaa'] = msaa
            if msaa.get('samples') != str(samples) or (samples and
                    (int(msaa.get('colorResolves', '0')) <= 0 or int(msaa.get('depthResolves', '0')) <= 0)):
                failures.append(name + ': GL graph did not use and resolve the requested sample count')
        states[name] = state
        checks[name + '/state'] = state
        low, high = float(settings['r_hdrMinExposure']), float(settings['r_hdrMaxExposure'])
        if automatic and (not math.isfinite(exposure) or not low - .00001 <= exposure <= high + .00001):
            failures.append(name + ': exposure violated its requested bounds')
        # A one-level bloom filter remains constant well inside this unlit
        # scalar sphere. Five-level spatial falloff is covered by paired images.
        if not fault and settings['r_bloomMipCount'] == '1' and (not automatic or low == high):
            color = scene.decode(188)
            threshold = float(settings['r_bloomThreshold'])
            knee = threshold * float(settings['r_bloomSoftKnee'])
            soft = min(2 * knee, max(0, color - threshold + knee)) ** 2 / (4 * knee) if knee > .0001 else 0
            bright = color if threshold <= .0001 else max(color - threshold, soft, 0)
            radiance = color + (bright * float(settings['r_bloomIntensity']) if settings['r_bloom'] == '1' else 0)
            expected = scene.output(radiance, low if automatic else 1)
            centre_y = spec['centreY']
            error = float(np.max(np.abs(rgb[centre_y-5:centre_y+5, 635:645].astype(float) - expected)))
            checks[name + '/radiance'] = dict(expected=expected, maximumByteError=error)
            if error > 1:
                failures.append(name + ': incorrect linear bloom/exposure response')
        if spec['centreY'] != 400:
            ys, _ = np.nonzero(rgb[:, :, 0] > 64)
            centroid = float(ys.mean()) if len(ys) else float('nan')
            checks[name + '/orientation'] = dict(expectedY=spec['centreY'], actualY=centroid,
                                                coveredPixels=len(ys))
            if len(ys) < 1000 or not math.isfinite(centroid) or abs(centroid - spec['centreY']) > 3:
                failures.append(name + ': inverted or misplaced offscreen scene')
    for name, spec in profile.items():
        reference = spec['reference']
        if name not in images:
            failures.append(name + ': missing qualified capture')
        elif reference:
            equal = reference in images and np.array_equal(images[name], images[reference])
            checks[name + '/restoration'] = dict(reference=reference, equal=equal)
            if not equal:
                failures.append(name + ': full-frame restoration failed')
    if backend == 'vk' and all('linear-post-' + n in states for n in ('fault-baseline', 'fault-1', 'fault-2', 'fault-3', 'fault-restored')):
        epochs = [int(states['linear-post-' + n]['generation']) for n in ('fault-baseline', 'fault-1', 'fault-2', 'fault-3', 'fault-restored')]
        checks['domain-epochs'] = epochs
        if not epochs[0] < epochs[1] == epochs[2] == epochs[3] < epochs[4]:
            failures.append('exposure did not reset exactly when numeric ownership changed')
    if backend == 'vk':
        rows = {row['case']: row for row in report['results']}
        for case, marker in (('resize', 'SMALL'), ('capture-stability', 'CAPTURE')):
            row = rows.get('linear-post-' + case)
            if row is None:
                continue
            text = Path(row['log']).read_text(errors='replace')
            match = re.search(rf'(?m)^HDR_POST_{marker}_BEGIN\s*$([\s\S]*?)^HDR_POST_{marker}_END\s*$', text)
            section = match[1] if match else ''
            values = [fields(line) for line in section.splitlines() if line.startswith('Vulkan HDR:')]
            shot = Path(row['screenshot']).with_name('linear-post-' + ('small' if case == 'resize' else 'duplicate') + '.tga')
            check = dict(states=values, screenshot=str(shot), sha256=lab.digest(shot) if shot.is_file() else None)
            checks[marker] = check
            if case == 'resize':
                valid = len(values) == 1 and all(values[0].get(k) == v for k, v in dict(extent='960x600', linearScene='1', initialized='1', samples=str(samples)).items())
                if shot.is_file():
                    with Image.open(shot) as raw:
                        valid = valid and raw.size == (960, 600)
                        error = max(abs(c - scene.output(scene.decode(188) * 1.5)) for c in raw.convert('RGB').getpixel((480, 300)))
                        check['maximumByteError'] = error
                        valid = valid and error <= 1
                else:
                    valid = False
            else:
                valid = len(values) == 2 and shot.is_file() and all(values[0].get(k) == values[1].get(k)
                    for k in ('generation', 'queued', 'average', 'target', 'exposure', 'linearScene'))
                if valid:
                    with Image.open(shot) as raw:
                        valid = np.array_equal(np.asarray(raw.convert('RGB')), images[row['case']])
            if not valid:
                failures.append(case + ': auxiliary capture/lifecycle contract failed')
    if 'linear-post-cube-capture-restored' in images:
        row = next(row for row in report['results'] if row['case'] == 'linear-post-cube-capture-restored')
        directory = Path(row['screenshot']).parent.parent / 'env'
        faces = {}
        for face in ('px', 'nx', 'py', 'ny', 'pz', 'nz'):
            path = directory / ('linear-post-cube_' + face + '.tga')
            try:
                with Image.open(path) as image:
                    if image.size != (256, 256):
                        failures.append('HDR cubemap ' + face + ': incorrect extent')
                    faces[face] = dict(path=str(path), sha256=lab.digest(path))
            except (OSError, ValueError) as error:
                failures.append('HDR cubemap ' + face + ': ' + str(error))
        checks['cube-captures'] = faces
    return dict(status='fail' if failures else 'pass', checks=checks, failures=failures)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runtime-root', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--basepath', type=Path, required=True)
    parser.add_argument('--backend', choices=('vk', 'gl'), default='vk')
    parser.add_argument('--samples', type=int, choices=(0, 4), default=0)
    parser.add_argument('--limit', type=int)
    args = parser.parse_args()
    source_hash = lab.digest(Path(__file__))
    manifest = json.loads((args.runtime_root / 'pbr-lab.json').read_text())
    profile = configure(manifest, args.samples)
    if args.backend == 'gl':
        profile = {k: v for k, v in profile.items() if v['pair']}
    if args.limit:
        profile = dict(list(profile.items())[:args.limit])
    for name, spec in profile.items():
        lab.CASES[name] = spec['settings']
        # Keep time frozen across the domain-only failure sequence. Resuming
        # simulation can itself create a valid exposure discontinuity and would
        # confound the exact generation check for numeric ownership changes.
        hold_time = name.rsplit('linear-post-', 1)[-1] in ('fault-1', 'fault-2', 'fault-3', 'fault-restored')
        commands = [*spec['commands'], *(['wait 90'] if hold_time else ['g_stopTime 0', 'wait 90', 'g_stopTime 1'])]
        if args.backend == 'vk':
            if name.endswith('capture-stability'):
                commands += ['echo HDR_POST_CAPTURE_BEGIN', 'rendererVulkanHDRInfo',
                    'screenshot "screenshots/linear-post-duplicate.tga"', 'rendererVulkanHDRInfo', 'echo HDR_POST_CAPTURE_END']
            commands += [f'echo "HDR_POST_{name}_BEGIN"', 'rendererVulkanHDRInfo', f'echo "HDR_POST_{name}_END"']
        lab.CASE_COMMANDS[name] = commands
        lab.CASE_CAMERAS[name] = 'sampling'
    sys.argv = [__file__, '--runtime-root', str(args.runtime_root), '--output-dir', str(args.output_dir),
        '--basepath', str(args.basepath), '--backend', args.backend, '--camera', 'sampling',
        '--batch', '--cases', ','.join(profile), '--timeout', '900']
    if args.backend == 'gl':
        sys.argv.append('--gl-debug')
    code = lab.main()
    path = args.output_dir / 'report.json'
    report = json.loads(path.read_text())
    assert source_hash == lab.digest(Path(__file__)), 'harness changed during execution'
    report.update(postProfile=profile, requestedSamples=args.samples, limited=args.limit,
                  postProof=qualify(report, profile, args.backend, args.samples))
    for source in (Path(__file__), Path(scene.__file__)):
        shutil.copy2(source, args.output_dir / 'harness' / source.name)
        report['harnessSources'][source.name] = lab.digest(source)
    path.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(dict(status=report['postProof']['status'], failures=report['postProof']['failures'], captures=len(report['results'])), indent=2))
    return int(bool(code or report['postProof']['failures']))


if __name__ == '__main__':
    raise SystemExit(main())
