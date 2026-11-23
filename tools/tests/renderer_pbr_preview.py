#!/usr/bin/env python3
"""Qualify HDR-off PBR radiance, classic-domain preservation and scene recovery.

Uses the original laboratory and engine screenshots, with windowed hidden games
and no input control. Comparisons keep the fixed two-byte full-image gate.
"""
import argparse
import json
from pathlib import Path
import re
import shutil
import sys

import numpy as np
from PIL import Image

import renderer_vulkan_hdr_scene as scene

lab = scene.lab


def scene_commands(name):
    # Printed with the world frozen, after the case's own commands. Nothing
    # changes before the capture that follows.
    return [f'echo "PBRSCENE_{name}_BEGIN"', 'listRenderLightDefs', 'listRenderEntityDefs',
            f'echo "PBRSCENE_{name}_END"']


def captured_scene(log, name):
    marker = re.search(rf'(?m)^PBRSCENE_{re.escape(name)}_BEGIN\s*$([\s\S]*?)^PBRSCENE_{re.escape(name)}_END\s*$', log)
    if not marker:
        return None
    defs = re.findall(r'(?m)^\s*\d+:\s+\d+ intr\s+\d+ refs (\S+)(?: at [^\r\n]*)?\s*$', marker[1])
    return dict(specimens=sum(d.lower() == lab.lab.MODEL.lower() for d in defs),
                lights=sorted(d.rsplit('/', 1)[-1] for d in defs
                              if re.fullmatch(r'lights/openq4/pbr_lab/(?:fog|blend)', d, re.I)))


def configure(manifest, samples):
    lab.BASE.update(r_screenFraction='100', r_resolutionScaleMode='1', r_temporalAA='0')
    profile = {}
    for name, spec in scene.configure_composition(manifest, samples).items():
        profile[name.replace('linear-', 'preview-', 1)] = dict(spec,
            settings={**spec['settings'], 'r_hdrToneMap': '0', 'g_renderFastNoPostDirect': '0',
                      'r_rendererModernLightingParity': '0'},
            reference=None, light=None if spec['effect'] == 'clear' else spec['effect'].split('-')[0])
    base = next(iter(profile.values()))
    settings = {**base['settings'], 'r_pbrDebug': '0'}
    commands = ['g_stopTime 0',
        'script "$linear_specimen_19.hide(); $linear_specimen_19.remove(); $linear_overlay_19.remove()"',
        'wait 30', f'spawn func_static name preview_lit model "{lab.lab.MODEL}" '
        f'shader "{lab.lab.PREFIX}/emissive" origin "0 -700 380" angle 0 solid 0',
        'wait 30', 'g_stopTime 1']

    def add(suffix, changes=None, commands=None, reference=None, light=None):
        profile['preview-' + suffix] = dict(settings={**settings, **(changes or {})},
            commands=commands or [], reference=reference, light=light)

    add('lit-emission', commands=commands, reference='preview-composition-emissive-6-clear')
    for effect in ('fog', 'blend'):
        light = 'preview_emission_' + effect
        add('lit-' + effect, commands=['g_stopTime 0',
            f'spawn light name {light} origin "0 0 400" angle 0 '
            f'light_radius "1400 1400 1000" texture "lights/openq4/pbr_lab/{effect}" noshadows 1',
            'wait 30', 'g_stopTime 1'], light=effect)
        add('lit-' + effect + '-restored', commands=['g_stopTime 0',
            f'script "${light}.remove()"', 'wait 30', 'g_stopTime 1'], reference='preview-lit-emission')
    for scale in (50, 75, 125, 150, 200):
        add('scale-' + str(scale), {'r_screenFraction': str(scale)})
    add('scale-restored', reference='preview-lit-emission')
    add('direct-target', {'g_renderFastNoPostDirect': '1'}, reference='preview-lit-emission')
    add('game-target-restored', reference='preview-lit-emission')
    add('hdr', {'r_hdrToneMap': '1'})
    add('hdr-restored', reference='preview-lit-emission')
    for boundary in ('image-reload', 'partial-restart', 'full-restart'):
        add(boundary, commands=lab.CASE_COMMANDS[boundary], reference='preview-lit-emission')
    add('disabled', {'r_pbrMaterials': '0'})
    add('restored', reference='preview-lit-emission')
    return profile


def qualify(report, profile, backend, samples, code):
    failures, checks, images = [], {}, {}
    if code or not report.get('complete') or not report.get('runtimeUnchanged'):
        failures.append('laboratory failed, incomplete sequence, or changed runtime')
    if [r['case'] for r in report['results']] != list(profile):
        failures.append('captured sequence differs from requested profile')
    for row in report['results']:
        name, spec = row['case'], profile[row['case']]
        failures.extend(name + ': ' + failure for failure in row['failures'])
        for kind in ('screenshot', 'log'):
            if lab.digest(Path(row[kind])) != row['sha256'][kind]:
                failures.append(name + ': changed ' + kind)
        log = Path(row['log']).read_text(errors='replace')
        marker = re.search(rf'(?m)^PBRLAB_{re.escape(name)}_BEGIN\s*$([\s\S]*?)^PBRLAB_{re.escape(name)}_END\s*$', log)
        section = marker[1] if marker else ''
        if not marker:
            failures.append(name + ': missing capture markers')
        expected = int(spec['settings'].get('r_hdrToneMap') == '0'
                       and spec['settings'].get('r_pbrMaterials') != '0')
        state = dict(re.findall(r'(\w+)=([^\s]+)', next((line for line in section.splitlines()
            if line.startswith('Vulkan PBR preview:')), '')))
        checks[name] = dict(expectedOwnership=expected, ownership=state)
        # The image must show the sequence's scene: one specimen and exactly
        # the intended fog/blend light, not a predecessor's leftovers.
        intended = dict(specimens=1, lights=[spec['light']] if spec.get('light') else [])
        captured = captured_scene(log, name)
        checks[name]['scene'] = dict(expected=intended, captured=captured)
        if captured != intended:
            failures.append(f'{name}: captured scene {captured} differs from its sequence {intended}')
        if backend == 'vk':
            owner = dict(requested=str(expected), committed=str(expected),
                         reason='complete' if expected else 'disabled')
            if expected:
                owner['samples'] = str(max(1, samples))
            if any(state.get(k) != v for k, v in owner.items()):
                failures.append(name + ': preview ownership differs from requested control')
        if not re.search(rf'Renderer AA: MSAA requested={samples} effective={samples}\b', section):
            failures.append(name + ': effective MSAA differs from request')
        with Image.open(row['screenshot']) as raw:
            rgb = np.asarray(raw.convert('RGB'))
        if rgb.shape != (800, 1280, 3):
            failures.append(name + ': wrong engine screenshot extent')
        images[name] = rgb
        reference = spec.get('reference')
        if reference:
            equal = np.array_equal(rgb, images.get(reference))
            checks[name]['restoredExactly'] = equal
            if not equal:
                failures.append(name + ': image differs from ' + reference)
    return dict(status='fail' if failures else 'pass', checks=checks, failures=failures)


def compare(gl_path, vk_path):
    reports = [json.loads(p.read_text()) for p in (gl_path, vk_path)]
    for report in reports:
        if report['previewProof']['status'] != 'pass':
            raise ValueError('comparison requires two independently qualified runs')
    for key in ('fixture', 'compiledMapSHA256', 'requestedSamples', 'previewProfile', 'harnessSources'):
        if reports[0][key] != reports[1][key]:
            raise ValueError('comparison provenance differs: ' + key)
    rows = [{row['case']: row for row in report['results']} for report in reports]
    pairs = {}
    for name in rows[0]:
        for data in rows:
            row = data[name]
            if lab.digest(Path(row['screenshot'])) != row['sha256']['screenshot']:
                raise ValueError('comparison image changed: ' + name)
        images = [np.asarray(Image.open(data[name]['screenshot']).convert('RGB')).astype(np.int16) for data in rows]
        delta = np.abs(images[0] - images[1])
        pairs[name] = dict(maximum=int(delta.max()), overTwoChannels=int((delta > 2).sum()),
                           rms=float(np.sqrt(np.mean(delta.astype(float) ** 2))))
    failures = [name for name, pair in pairs.items() if pair['maximum'] > 2]
    return dict(status='fail' if failures else 'pass', threshold=2, pairs=pairs, failures=failures,
                reports={str(p.resolve()): lab.digest(p) for p in (gl_path, vk_path)})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('runtime-root', 'output-dir', 'basepath'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--backend', choices=('gl', 'vk'), default='vk')
    parser.add_argument('--samples', type=int, choices=(0, 2, 4, 8), default=4)
    parser.add_argument('--compare-gl-report', type=Path)
    args = parser.parse_args()
    manifest = json.loads((args.runtime_root / 'pbr-lab.json').read_text())
    profile = configure(manifest, args.samples)
    for name, spec in profile.items():
        lab.CASES[name] = spec['settings']
        lab.CASE_CAMERAS[name] = 'sampling'
        lab.CASE_COMMANDS[name] = [*spec['commands'], 'wait 30', *scene_commands(name)]
        if spec['settings'].get('r_pbrMaterials') == '0':
            lab.LEGACY_CASES.add(name)
    sys.argv = [__file__, '--runtime-root', str(args.runtime_root), '--output-dir', str(args.output_dir),
        '--basepath', str(args.basepath), '--backend', args.backend, '--camera', 'sampling',
        '--batch', '--cases', ','.join(profile), '--timeout', '900']
    if args.backend == 'gl':
        sys.argv.append('--gl-debug')
    code = lab.main()
    path = args.output_dir / 'report.json'
    report = json.loads(path.read_text())
    report.update(previewProfile=profile, requestedSamples=args.samples,
                  previewProof=qualify(report, profile, args.backend, args.samples, code))
    for source in (Path(__file__), Path(scene.__file__)):
        shutil.copy2(source, args.output_dir / 'harness' / source.name)
        report['harnessSources'][source.name] = lab.digest(source)
    path.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    comparison = None
    if args.compare_gl_report:
        comparison = compare(args.compare_gl_report, path)
        (args.output_dir / 'comparison.json').write_text(json.dumps(comparison, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(dict(status=report['previewProof']['status'], cases=len(profile),
        failures=report['previewProof']['failures'], comparison=comparison), indent=2), flush=True)
    return int(bool(code or report['previewProof']['failures']
                    or (comparison or {}).get('failures')))


if __name__ == '__main__':
    raise SystemExit(main())
