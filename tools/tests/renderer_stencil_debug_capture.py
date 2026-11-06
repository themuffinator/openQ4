#!/usr/bin/env python3
"""Compare stencil debug views and the weapon depth hack on OpenGL and Vulkan (VK-07).

The scene is the original PBR laboratory with its key light casting a stencil
shadow from an occluder onto a diffuse sphere. Cases, each an engine
screenshot of the frozen scene:

- base: the lit scene with stencil shadows;
- shadow-count: r_showShadowCount 1 colours the screen by shadow-volume depth
  complexity read from the stencil buffer;
- shadow-overdraw: r_showShadowCount 2 also prints the stencil overdraw;
- light-count / light-count-walls: r_showLightCount 1 and 2 colour surfaces by
  light count (2 also counts through walls);
- light-overdraw: r_showLightCount 3 also prints the overdraw;
- two-lights: a second light, so light-count colours change;
- weapon / weapon-depth: the player's view weapon (weaponDepthHack) in the lit
  scene without specular, and under r_showDepth;
- restarted / base-restored: vid_restart partial must reproduce shadow-count,
  and clearing every debug view must reproduce base exactly.

The printed overdraw is the same stencil interpretation the colours use. OpenGL
prints it from the frame; Vulkan prints it a frame or two later with a
labelled header. --compare requires every GL/Vulkan image within the existing
two-display-level limit and the largest printed overdraw of each counting
case to agree to the printed precision.
"""
import argparse
import json
from pathlib import Path
import re
import shutil
import sys

import numpy as np
from PIL import Image

import renderer_pbr_laboratory as lab

CASES = ('base', 'shadow-count', 'shadow-overdraw', 'light-count', 'light-count-walls',
         'light-overdraw', 'two-lights', 'weapon', 'weapon-depth', 'restarted', 'base-restored')
COUNTING = ('shadow-overdraw', 'light-overdraw')
DEBUG = {'shadow-count': {'r_showShadowCount': '1'}, 'shadow-overdraw': {'r_showShadowCount': '2'},
         'light-count': {'r_showLightCount': '1'}, 'light-count-walls': {'r_showLightCount': '2'},
         'light-overdraw': {'r_showLightCount': '3'}, 'two-lights': {'r_showLightCount': '1'},
         'weapon-depth': {'r_showDepth': '1'}, 'restarted': {'r_showShadowCount': '1'}}
NAME = 'stencil-debug-'
# OpenGL prints 'all shadows overdraw: N' on one line; Vulkan prints the
# value under its readback header a frame or two later.
OVERDRAW = re.compile(r'overdraw:\s*([0-9.]+)')


def load(path):
    return np.asarray(Image.open(path).convert('RGB'), dtype=np.int16)


def section(log, name):
    return log.split(f'PBRLAB_{name}_BEGIN', 1)[-1].split(f'PBRLAB_{name}_END', 1)[0]


def profile(manifest):
    cases = {}
    for suffix in CASES:
        name = NAME + suffix
        settings = dict(r_pbrMaterials='0', r_pbrIBL='0', r_rendererReflectionProbes='0', r_useLightGrid='0',
                        r_hdrToneMap='0', r_lightScale='0.4', r_shadows='1', r_useShadowMap='0',
                        g_showPlayerShadow='0', r_showShadowCount='0', r_showLightCount='0', r_showDepth='0',
                        ui_showGun='1' if suffix.startswith('weapon') else '0',
                        # The weapon cases qualify the depth hack's geometry and
                        # depth; its specular highlights belong to lighting parity.
                        r_skipSpecular='1' if suffix.startswith('weapon') else '0')
        settings.update(DEBUG.get(suffix, {}))
        commands = ['g_stopTime 0']
        if not cases:
            commands += [f'script "${s["name"]}.hide()"' for s in manifest['stations']]
            commands += [f'script "${light}.Off()"' for light in ('key', 'blue_fill', 'warm_fill', 'lab_projector')]
            commands += [f'spawn func_static name stencil_specimen model "{lab.lab.MODEL}" '
                         f'shader "{lab.lab.PREFIX}/classic_diffuse" origin "100 160 120" angle 0 solid 0',
                         f'spawn func_static name stencil_occluder model "{lab.lab.MODEL}" '
                         f'shader "{lab.lab.PREFIX}/classic_floor" origin "60 -30 350" angle 0 solid 0',
                         'script "$key.On()"']
        if suffix == 'two-lights':
            commands += ['script "$blue_fill.On()"']
        if suffix == 'weapon':
            commands += ['script "$blue_fill.Off()"', 'give weapon_machinegun']
        if suffix == 'restarted':
            commands += ['vid_restart partial']
        commands += ['wait 120' if suffix == 'weapon' else 'wait 60', 'g_stopTime 1']
        cases[name] = dict(settings=settings, commands=commands)
    return cases


def prove(report):
    failures, checks, images, prints = [], {}, {}, {}
    rows = {row['case'].removeprefix(NAME): row for row in report['results']}
    if not report.get('complete') or not report.get('runtimeUnchanged') or list(rows) != list(CASES):
        failures.append('incomplete or changed capture set/runtime')
    for name, row in rows.items():
        failures += [name + ': ' + error for error in row['failures']]
        valid = True
        for kind in ('screenshot', 'log'):
            path = Path(row.get(kind, ''))
            if not path.is_file() or lab.digest(path) != row['sha256'].get(kind):
                failures.append(name + ': missing or changed ' + kind)
                valid = False
        if not valid:
            continue
        images[name] = load(row['screenshot'])
        if name in COUNTING:
            text = section(Path(row['log']).read_text(encoding='utf-8', errors='replace'), NAME + name)
            values = [float(value) for value in OVERDRAW.findall(text)]
            prints[name] = values
            checks[name] = dict(printed=len(values), largest=max(values, default=None))
            if not values or max(values) <= 0.0:
                failures.append(name + ': no nonzero stencil overdraw was printed')
    if set(images) == set(CASES):
        def changed(a, b):
            return int(np.count_nonzero(np.abs(images[a] - images[b]).max(axis=2) > 2))
        for name, reference in (('shadow-count', 'base'), ('light-count', 'base'),
                                ('light-count-walls', 'base'), ('two-lights', 'light-count'),
                                ('weapon', 'base'), ('weapon-depth', 'weapon')):
            checks.setdefault(name, {})['changedPixels'] = changed(name, reference)
            if checks[name]['changedPixels'] < 500:
                failures.append(f'{name}: no visible response against {reference}')
        for name, reference in (('restarted', 'shadow-count'), ('base-restored', 'base')):
            exact = bool(np.array_equal(images[name], images[reference]))
            checks.setdefault(name, {})['exactRecovery'] = exact
            if not exact:
                failures.append(f'{name}: did not reproduce {reference} exactly')
        depth = images['weapon-depth']
        grey = bool(np.array_equal(depth[..., 0], depth[..., 1]) and np.array_equal(depth[..., 0], depth[..., 2]))
        checks['weapon-depth']['grayscale'] = grey
        if not grey:
            failures.append('weapon-depth: depth view is not grayscale')
    return dict(status='fail' if failures else 'pass', failures=failures, checks=checks, overdraw=prints)


def compare(left, right):
    reports = [json.loads(path.read_text(encoding='utf-8')) for path in (left, right)]
    failures, checks = [], {}
    for backend, report in zip(('gl', 'vk'), reports):
        if report.get('stencilDebugProof', {}).get('status') != 'pass':
            failures.append(backend + ': independent stencil debug proof failed')
        if any(row['backend'] != backend for row in report['results']):
            failures.append('comparison requires OpenGL then Vulkan')
    for field in ('runtimeSHA256', 'compiledMapSHA256', 'stencilDebugProfile'):
        if reports[0].get(field) != reports[1].get(field):
            failures.append(field + ': comparison inputs differ')
    for a, b in zip(reports[0]['results'], reports[1]['results']):
        if a['case'] != b['case']:
            failures.append('case ordering mismatch')
            continue
        if not all(Path(row['screenshot']).is_file() for row in (a, b)):
            failures.append(a['case'] + ': capture missing')
            continue
        delta = np.abs(load(a['screenshot']) - load(b['screenshot'])).max(axis=2)
        checks[a['case']] = dict(maximumError=int(delta.max()), differingPixels=int(np.count_nonzero(delta)),
                                 overTolerancePixels=int(np.count_nonzero(delta > 2)))
        if np.any(delta > 2):
            failures.append(a['case'] + ': exceeds two display levels')
    prints = [report.get('stencilDebugProof', {}).get('overdraw', {}) for report in reports]
    for name in COUNTING:
        largest = [max(source.get(name) or [0.0]) for source in prints]
        checks['overdraw-' + name] = dict(gl=largest[0], vk=largest[1])
        # The value prints with one decimal; equal stencil counts print equal values.
        if abs(largest[0] - largest[1]) > 0.051:
            failures.append(f'{name}: printed overdraw differs ({largest[0]} vs {largest[1]})')
    return dict(status='fail' if failures else 'pass', failures=failures, checks=checks,
                reports={str(path.resolve()): lab.digest(path) for path in (left, right)})


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--runtime-root', type=Path)
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--basepath', type=Path)
    parser.add_argument('--backend', choices=('gl', 'vk'))
    parser.add_argument('--samples', type=int, choices=(0, 4), default=0)
    parser.add_argument('--timeout', type=int, default=900)
    parser.add_argument('--compare', nargs=2, type=Path)
    args = parser.parse_args()
    args.output_dir = args.output_dir.resolve()
    if args.compare:
        proof = compare(*args.compare)
        args.output_dir.mkdir(parents=True, exist_ok=True)
        (args.output_dir / 'comparison.json').write_text(json.dumps(proof, indent=2) + '\n', encoding='utf-8')
        print(json.dumps(proof, indent=2))
        return int(proof['status'] != 'pass')
    if not all((args.runtime_root, args.basepath, args.backend)):
        parser.error('capture needs --runtime-root, --basepath and --backend')
    args.runtime_root = args.runtime_root.resolve()
    manifest = json.loads((args.runtime_root / 'pbr-lab.json').read_text(encoding='utf-8'))
    source_hash = lab.digest(Path(__file__))
    lab.BASE.update(r_multiSamples=str(args.samples), image_anisotropy='1')
    cases = profile(manifest)
    # The laboratory restores every case cvar from BASE between cases; the
    # base case carries each debug view's off value.
    for key, value in cases[NAME + 'base']['settings'].items():
        lab.BASE.setdefault(key, value)
    for name, spec in cases.items():
        lab.CASES[name], lab.CASE_COMMANDS[name] = spec['settings'], spec['commands']
        lab.LEGACY_CASES.add(name)
    sys.argv = [__file__, '--runtime-root', str(args.runtime_root), '--output-dir', str(args.output_dir),
                '--basepath', str(args.basepath), '--backend', args.backend, '--cases', ','.join(cases),
                '--batch', '--camera', 'coverage', '--timeout', str(args.timeout)]
    if args.backend == 'gl':
        sys.argv += ['--gl-debug']
    code = lab.main()
    path = args.output_dir / 'report.json'
    report = json.loads(path.read_text(encoding='utf-8'))
    assert lab.digest(Path(__file__)) == source_hash, 'harness changed during capture'
    shutil.copy2(__file__, args.output_dir / 'harness' / Path(__file__).name)
    report['harnessSources'][Path(__file__).name] = source_hash
    report.update(stencilDebugProfile=cases, requestedSamples=args.samples)
    report['stencilDebugProof'] = prove(report)
    path.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(dict(status=report['stencilDebugProof']['status'],
                          failures=report['stencilDebugProof']['failures']), indent=2))
    return int(code or report['stencilDebugProof']['status'] != 'pass')


if __name__ == '__main__':
    raise SystemExit(main())
