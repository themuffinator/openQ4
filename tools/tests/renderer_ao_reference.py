#!/usr/bin/env python3
"""REN-10 ambient occlusion qualification against a brute-force reference.

Opt-in, local and hardware-bound. For each backend and stock pose it launches
the staged client once (hidden window, frozen game time, no synthesized input)
and captures r_ssaoDebug views of:

- SSAO (r_ssaoMethod 0), HBAO (1) and GTAO (2) at the Quality budget
  (12 samples, automatic half resolution) and the Ultra budget (32, full);
- the brute-force reference (r_ssaoMethod 3: 256 cosine-weighted rays per
  pixel marched through the depth buffer, full resolution, unfiltered);
- SSAO again after the other methods ran in the same process (rollback);
- the Quality captures and the reference at a pose moved one and a half units
  sideways (motion);
- the lit frame without AO, whose luminance edges mark silhouettes.

Checks, per pose and tier:

- accuracy: GTAO's mean absolute difference from the reference is below SSAO's;
- halos: GTAO darkens the silhouette band beyond the reference no more than
  SSAO does;
- crawl: the part of GTAO's change under the small move that the reference's
  own change does not explain is no larger than SSAO's;
- rollback: SSAO after the other methods is byte-identical to SSAO before;
- parity: HBAO and GTAO on OpenGL and Vulkan agree within 2 levels on average.

HBAO is measured and reported beside them; it weights directions evenly
rather than by cosine, so it is expected to read darker than the reference
and carries no accuracy gate.

Writable state is isolated per launch under --output-dir; the executable is
reused in place. A JSON report and a Markdown summary are written there.

    python tools/tests/renderer_ao_reference.py --basepath .tmp/runtime/retail-assets \\
        --output-dir .tmp/ao-reference
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
import time
from pathlib import Path

import numpy as np
from PIL import Image, ImageFilter

ROOT = Path(__file__).resolve().parents[2]

POSES = {
    'storage2': ('game/storage2', (-656, 1264, -2688, 0, 0, 0)),
    'hub2': ('game/hub2', (3568, -5780, -296, 0, 0, 0)),
    'hub2-down': ('game/hub2', (3568, -5780, -296, 35, 0, 0)),
}
QUALITY = {'r_ssaoSamples': '12', 'r_ssaoResolution': '0'}
ULTRA = {'r_ssaoSamples': '32', 'r_ssaoResolution': '0'}
# (case, settings, shifted pose)
CASES = [
    ('off', {'r_ssao': '0', 'r_ssaoDebug': '0'}, False),
    ('ssao-quality', {'r_ssaoMethod': '0', **QUALITY}, False),
    ('ssao-ultra', {'r_ssaoMethod': '0', **ULTRA}, False),
    ('hbao-quality', {'r_ssaoMethod': '1', **QUALITY}, False),
    ('hbao-ultra', {'r_ssaoMethod': '1', **ULTRA}, False),
    ('gtao-quality', {'r_ssaoMethod': '2', **QUALITY}, False),
    ('gtao-ultra', {'r_ssaoMethod': '2', **ULTRA}, False),
    ('reference', {'r_ssaoMethod': '3', **ULTRA}, False),
    ('ssao-quality-rollback', {'r_ssaoMethod': '0', **QUALITY}, False),
    ('ssao-quality-moved', {'r_ssaoMethod': '0', **QUALITY}, True),
    ('hbao-quality-moved', {'r_ssaoMethod': '1', **QUALITY}, True),
    ('gtao-quality-moved', {'r_ssaoMethod': '2', **QUALITY}, True),
    ('reference-moved', {'r_ssaoMethod': '3', **ULTRA}, True),
    ('hbao-quality-lit', {'r_ssaoMethod': '1', 'r_ssaoDebug': '0', **QUALITY}, False),
    ('gtao-quality-lit', {'r_ssaoMethod': '2', 'r_ssaoDebug': '0', **QUALITY}, False),
    ('ssao-quality-lit', {'r_ssaoMethod': '0', 'r_ssaoDebug': '0', **QUALITY}, False),
]
BASE = {
    'r_fullscreen': '0', 'r_borderless': '0', 'r_borderlessDefaultMigrated': '1',
    'r_hiddenWindow': '1', 'in_mouse': '0', 'in_joystick': '0', 's_noSound': '1',
    'r_mode': '-1', 'r_customWidth': '1280', 'r_customHeight': '800',
    'r_windowWidth': '1280', 'r_windowHeight': '800', 'r_swapInterval': '0',
    'r_multiSamples': '0', 'com_maxfps': '0', 'logFile': '2', 'com_fixedTic': '1',
    'logFileName': 'logs/openq4.log', 'developer': '0',
    'com_skipLoadingContinue': '1', 'com_loadingContinueAutoAdvance': '1',
    'g_autoSkipCinematics': '1', 'g_autoScreenshot': '0', 'ui_showGun': '0', 'g_showHud': '0',
    'r_postAA': '0', 'r_temporalAA': '0', 'r_motionBlur': '0', 'r_bloom': '0',
    'r_ssao': '1', 'r_ssaoDebug': '1', 'r_ssaoMethod': '0',
    'r_ssaoRadius': '36', 'r_ssaoIntensity': '1.35', 'r_ssaoPower': '1.6', 'r_ssaoMaxDistance': '220',
}
TIERS = ('quality', 'ultra')
METHODS = ('ssao', 'hbao', 'gtao')
PARITY_MEAN_LEVELS = 2.0


def launch(runtime: Path, basepath: Path, backend: str, pose_name: str, out: Path, timeout: int) -> dict:
    map_name, pose = POSES[pose_name]
    save = out / f'{backend}-{pose_name}'
    if save.exists():
        raise SystemExit(f'refusing to reuse {save}')
    (save / 'baseoq4').mkdir(parents=True)
    (save / 'baseoq4/config.spec').write_text('')
    cvars = {**BASE, 'r_renderApi': backend, 'fs_game': 'baseoq4', 'fs_basepath': str(basepath),
             'fs_savepath': str(save), 'fs_devpath': str(save),
             'g_autoExecAfterMapLoad': 'ao_reference.cfg', 'g_autoExecAfterMapLoadDelayMs': '500'}
    (save / 'baseoq4/autoexec.cfg').write_text(''.join(f'set {k} "{v}"\n' for k, v in cvars.items()))
    # One and a half units along the view's right (yaw 0 looks down +x).
    moved = (pose[0], pose[1] - 1.5, *pose[2:])
    commands = ['wait 30', 'god', 'notarget', 'noclip', 'g_stopTime 1',
                'setviewpos ' + ' '.join(map(str, pose)), 'wait 30']
    for case, settings, shifted in CASES:
        commands += ['setviewpos ' + ' '.join(map(str, moved if shifted else pose))]
        commands += ['set r_ssao 1', 'set r_ssaoDebug 1'] + [f'set {k} "{v}"' for k, v in settings.items()]
        commands += ['wait 12', f'echo AOREF_{case}_BEGIN', f'screenshot "screenshots/{case}.tga"', f'echo AOREF_{case}_END']
    commands += ['wait 5', 'quit']
    (save / 'baseoq4/ao_reference.cfg').write_text('\n'.join(commands) + '\n')
    early = ('fs_game', 'fs_basepath', 'fs_savepath', 'fs_devpath', 'r_renderApi', 'r_fullscreen',
             'r_hiddenWindow', 'in_mouse', 's_noSound', 'logFile', 'logFileName')
    exe = runtime / ('openQ4-client_x64.exe' if sys.platform == 'win32' else 'openQ4-client_x64')
    command = [str(exe), *[x for k in early for x in ('+set', k, cvars[k])], '+exec', 'autoexec.cfg', '+map', map_name]
    (save / 'command.json').write_text(json.dumps(command, indent=2))
    start = time.monotonic()
    with (save / 'process.log').open('w') as log:
        process = subprocess.Popen(command, cwd=runtime, stdout=log, stderr=subprocess.STDOUT)
        try:
            code = process.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            process.kill()
            code = 124
    shots = save / 'baseoq4/screenshots'
    images = {}
    for case, _, _ in CASES:
        path = shots / f'{case}.tga'
        if path.is_file():
            images[case] = np.asarray(Image.open(path).convert('L'), dtype=np.float64)
    return {'code': code, 'seconds': round(time.monotonic() - start, 1), 'images': images, 'save': save}


def silhouette_band(lit: np.ndarray) -> np.ndarray:
    """Pixels within four of a strong luminance edge of the frame without AO."""
    image = Image.fromarray(lit.astype(np.uint8))
    edges = np.asarray(image.filter(ImageFilter.FIND_EDGES), dtype=np.float64)
    threshold = np.percentile(edges, 95)
    mask = Image.fromarray(((edges >= max(threshold, 8.0)) * 255).astype(np.uint8))
    return np.asarray(mask.filter(ImageFilter.MaxFilter(9)), dtype=np.float64) > 0


def judge(result: dict) -> dict:
    images = result['images']
    missing = [case for case, _, _ in CASES if case not in images]
    if missing:
        return {'failures': [f'missing captures: {", ".join(missing)}']}
    reference = images['reference']
    covered = reference < 254.5
    for method in METHODS:
        covered |= images[f'{method}-ultra'] < 254.5
    band = silhouette_band(images['off']) & covered
    metrics, failures = {}, []
    reference_motion = np.abs(images['reference-moved'] - reference)[covered].mean()
    for tier in TIERS:
        for method in METHODS:
            image = images[f'{method}-{tier}']
            metrics[f'{method}-{tier}'] = {
                'meanAbsError': round(float(np.abs(image - reference)[covered].mean()), 3),
                'meanSignedError': round(float((image - reference)[covered].mean()), 3),
                'haloDarkening': round(float(np.maximum(reference - image, 0)[band].mean()) if band.any() else 0.0, 3),
            }
        if metrics[f'gtao-{tier}']['meanAbsError'] >= metrics[f'ssao-{tier}']['meanAbsError']:
            failures.append(f'GTAO {tier} is not closer to the reference than SSAO')
        if metrics[f'gtao-{tier}']['haloDarkening'] > metrics[f'ssao-{tier}']['haloDarkening'] + 0.5:
            failures.append(f'GTAO {tier} darkens silhouettes more than SSAO')
    # Crawl: the part of a method's change under the small move that the
    # reference's own change does not explain.
    reference_change = images['reference-moved'] - reference
    crawl = {}
    for method in METHODS:
        change = images[f'{method}-quality-moved'] - images[f'{method}-quality']
        crawl[method] = round(float(np.abs(change - reference_change)[covered].mean()), 3)
    metrics['crawl'] = {**crawl, 'referenceChange': round(float(reference_motion), 3)}
    if crawl['gtao'] > crawl['ssao'] + 0.25:
        failures.append('GTAO changes more than SSAO under the small move')
    identical = bool(np.array_equal(images['ssao-quality'], images['ssao-quality-rollback']))
    metrics['rollbackIdentical'] = identical
    if not identical:
        failures.append('SSAO after HBAO, GTAO and the reference is not byte-identical')
    metrics['coveredPixels'] = int(covered.sum())
    metrics['bandPixels'] = int(band.sum())
    return {'metrics': metrics, 'failures': failures}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--runtime-root', type=Path, default=ROOT / '.install')
    parser.add_argument('--basepath', type=Path, required=True,
                        help='Quake 4 install with q4base packs (prefer a packs-only view)')
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--backends', default='gl,vk')
    parser.add_argument('--poses', default=','.join(POSES))
    parser.add_argument('--timeout', type=int, default=600)
    args = parser.parse_args()
    if not (args.basepath / 'q4base').is_dir():
        parser.error('--basepath must contain q4base')
    args.output_dir.mkdir(parents=True, exist_ok=True)
    backends = {'gl': 'gl', 'vk': 'vulkan'}
    report = {'runtime': str(args.runtime_root), 'poses': {}, 'failures': []}
    kernels = {}
    for pose in args.poses.split(','):
        for backend in args.backends.split(','):
            result = launch(args.runtime_root.resolve(), args.basepath.resolve(), backends[backend], pose,
                            args.output_dir.resolve(), args.timeout)
            verdict = judge(result) if result['code'] == 0 else {'failures': [f'engine exited {result["code"]}']}
            report['poses'].setdefault(pose, {})[backend] = {'seconds': result['seconds'], **verdict}
            report['failures'] += [f'{pose}/{backend}: {failure}' for failure in verdict['failures']]
            for method in ('hbao', 'gtao'):
                if all(f'{method}-{tier}' in result['images'] for tier in TIERS):
                    kernels[(pose, backend, method)] = tuple(result['images'][f'{method}-{tier}'] for tier in TIERS)
            print(f'{pose}/{backend}: {json.dumps(verdict)}', flush=True)
        parity = {}
        for method in ('hbao', 'gtao'):
            if (pose, 'gl', method) not in kernels or (pose, 'vk', method) not in kernels:
                continue
            for index, tier in enumerate(TIERS):
                difference = np.abs(kernels[(pose, 'gl', method)][index] - kernels[(pose, 'vk', method)][index])
                parity[f'{method}-{tier}'] = {'meanAbs': round(float(difference.mean()), 3),
                                              'over1Level': int((difference > 1).sum())}
                if parity[f'{method}-{tier}']['meanAbs'] > PARITY_MEAN_LEVELS:
                    report['failures'].append(f'{pose}: {method.upper()} {tier} differs by '
                                              f'{parity[f"{method}-{tier}"]["meanAbs"]} levels between OpenGL and Vulkan')
        if parity:
            report['poses'][pose]['parity'] = parity
    (args.output_dir / 'report.json').write_text(json.dumps(report, indent=2))
    lines = ['# REN-10 ambient occlusion against the brute-force reference', '',
             '| Pose | Backend | Tier | SSAO error | HBAO error | GTAO error | SSAO halo | HBAO halo | GTAO halo |',
             '|---|---|---|---:|---:|---:|---:|---:|---:|']
    for pose, rows in report['poses'].items():
        for backend in ('gl', 'vk'):
            metrics = rows.get(backend, {}).get('metrics')
            if not metrics:
                continue
            for tier in TIERS:
                s, h, g = (metrics[f'{method}-{tier}'] for method in METHODS)
                lines.append(f'| {pose} | {backend} | {tier} | {s["meanAbsError"]} | {h["meanAbsError"]} | {g["meanAbsError"]} '
                             f'| {s["haloDarkening"]} | {h["haloDarkening"]} | {g["haloDarkening"]} |')
    lines += ['', 'Failures: ' + ('none' if not report['failures'] else '; '.join(report['failures']))]
    (args.output_dir / 'summary.md').write_text('\n'.join(lines) + '\n')
    print('\n'.join(lines))
    return 1 if report['failures'] else 0


if __name__ == '__main__':
    sys.exit(main())
