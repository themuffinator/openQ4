#!/usr/bin/env python3
"""Qualify larger-than-window (tiled) engine screenshots on GL and Vulkan.

`screenshot W H` with a size larger than the window renders the frame once
per window-sized tile with the viewport moved to that tile, and stitches the
readbacks (R_ReadTiledPixels). Each case writes an ordinary window capture,
a 2x2-tiled 2560x1600 capture and a 1920x1200 capture whose right and top
tiles are partial, in the SMAA laboratory scene that renderer_image_capture.py
qualifies.

`direct` draws the view straight into the window (no game post-process
target: post AA off; the whole batch runs without MSAA). There each backend must, on its own, place every tile's part of the
frame (no repeated tile), and a box-filtered 2x capture must reproduce the
window capture's structure.

`post` and `restored` draw through the game-owned SMAA scene target, which
the capture resizes to the whole capture. Only views drawn to the window take
the tile offset: the scene is drawn at the target's origin, and the view that
composites the target to the window moves to the tile. Every case is gated the
same way. The comparison mode requires GL and Vulkan to agree per tile within
the existing two-display-level limit in every case, and `restored` to repeat
`post` exactly.
"""
import argparse
import json
from pathlib import Path
import re
import shutil
import sys

import numpy as np
from PIL import Image

import renderer_image_capture as image_capture

lab = image_capture.lab
smaa = image_capture.smaa
SIZES = ((2560, 1600), (1920, 1200))
WINDOW = (1280, 800)
CASES = ('direct', 'post', 'restored')
# Cases whose independent per-backend tile checks are gated: all of them.
GATED = CASES


def tiles(image, width, height):
    """Window-sized tiles in readback order: GL x outer, y (from the bottom) inner."""
    rows = image.shape[0]
    result = {}
    for xo in range(0, width, WINDOW[0]):
        for yo in range(0, height, WINDOW[1]):
            bottom, top = rows - yo, max(0, rows - yo - WINDOW[1])
            result[f'{xo},{yo}'] = image[top:bottom, xo:xo + WINDOW[0]]
    return result


def load(path):
    return np.asarray(Image.open(path).convert('RGB'), dtype=np.int16)


def written(log, name):
    section = log.split(f'TILED_CAPTURE_{name}_BEGIN', 1)[-1].split(f'TILED_CAPTURE_{name}_END', 1)[0]
    return re.findall(r'^Wrote (\S+\.tga)\s*$', re.sub(r'\^[0-9]', '', section), re.M)


def qualify(report, output):
    failures, checks, files = [], {}, {}
    if not report.get('complete') or not report.get('runtimeUnchanged'):
        failures.append('incomplete sequence or changed runtime')
    rows = {row['case'].removeprefix('tiled-capture-'): row for row in report['results']}
    if set(rows) != set(CASES):
        failures.append('case set differs from the profile')
        return dict(status='fail', failures=failures, checks=checks, files=files)
    log = Path(next(iter(rows.values()))['log']).read_text(errors='replace')
    screenshots = output / 'batch/baseoq4'
    for name, row in rows.items():
        failures += [name + ': ' + error for error in row['failures']]
        shots = written(log, name)
        if len(shots) != 1 + len(SIZES):
            failures.append(f'{name}: expected {1 + len(SIZES)} written captures, found {shots}')
            continue
        files[name] = {}
        window = load(screenshots / shots[0])
        files[name]['window'] = dict(path=shots[0], sha256=lab.digest(screenshots / shots[0]))
        if window.shape[:2] != (WINDOW[1], WINDOW[0]):
            failures.append(f'{name}: window capture is {window.shape[1]}x{window.shape[0]}')
        for (width, height), shot in zip(SIZES, shots[1:]):
            key = f'{width}x{height}'
            path = screenshots / shot
            image = load(path)
            files[name][key] = dict(path=shot, sha256=lab.digest(path))
            if image.shape[:2] != (height, width):
                failures.append(f'{name} {key}: capture is {image.shape[1]}x{image.shape[0]}')
                continue
            parts = tiles(image, width, height)
            # A repeated tile is the failure this gate exists for: every pair of
            # full tiles must differ visibly.
            full = {k: v for k, v in parts.items() if v.shape[:2] == (WINDOW[1], WINDOW[0])}
            keys = sorted(full)
            distinct = []
            for i, a in enumerate(keys):
                for b in keys[i + 1:]:
                    changed = int(np.count_nonzero(np.abs(full[a] - full[b]).max(axis=2) > 2))
                    distinct.append(dict(tiles=[a, b], changedPixels=changed))
                    if changed < 1000 and name in GATED:
                        failures.append(f'{name} {key}: tiles {a} and {b} repeat the same image')
            entry = dict(tiles=sorted(parts), distinctPairs=distinct)
            if (width, height) == (2 * WINDOW[0], 2 * WINDOW[1]):
                reduced = image.reshape(WINDOW[1], 2, WINDOW[0], 2, 3).mean(axis=(1, 3))
                delta = np.abs(reduced - window).max(axis=2)
                correlation = float(np.corrcoef(reduced.ravel(), window.ravel())[0, 1])
                entry.update(boxFilteredCorrelation=correlation,
                             boxFilteredPixelsWithin8=float(np.mean(delta <= 8)))
                # The 2x capture is a supersampled render of the window's view:
                # a misplaced or mirrored tile breaks this structure.
                if correlation < 0.98 and name in GATED:
                    failures.append(f'{name} {key}: box-filtered capture does not reproduce the window view')
            checks.setdefault(name, {})[key] = entry
    if {'post', 'restored'} <= set(files):
        same = all(files['post'][k]['sha256'] == files['restored'][k]['sha256'] for k in files['post'])
        checks['restoredExact'] = same
        if not same:
            failures.append('restored: captures differ from post')
    return dict(status='fail' if failures else 'pass', failures=failures, checks=checks, files=files)


def compare(left, right):
    reports = [json.loads(path.read_text()) for path in (left, right)]
    failures, checks = [], {}
    for backend, report in zip(('gl', 'vk'), reports):
        if report.get('tiledCaptureProof', {}).get('status') != 'pass':
            failures.append(backend + ': independent tiled-capture proof failed')
    for key in ('runtimeSHA256', 'compiledMapSHA256'):
        if reports[0].get(key) != reports[1].get(key):
            failures.append('different ' + key)
    # Resolve captures beside each report, so retained evidence can be moved.
    roots = [path.resolve().parent / 'batch/baseoq4' for path in (left, right)]
    files = [report.get('tiledCaptureProof', {}).get('files', {}) for report in reports]
    for name in CASES:
        for key in ('window',) + tuple(f'{w}x{h}' for w, h in SIZES):
            try:
                images = [load(root / f[name][key]['path']) for root, f in zip(roots, files)]
            except (KeyError, FileNotFoundError, OSError):
                failures.append(f'{name} {key}: capture missing')
                continue
            if images[0].shape != images[1].shape:
                failures.append(f'{name} {key}: sizes differ')
                continue
            size = (images[0].shape[1], images[0].shape[0])
            parts = [tiles(image, *size) for image in images]
            for tile in parts[0]:
                delta = np.abs(parts[0][tile] - parts[1][tile]).max(axis=2)
                over = int(np.count_nonzero(delta > 2))
                checks[f'{name} {key} {tile}'] = dict(maximumError=int(delta.max()), overTolerancePixels=over)
                if over:
                    failures.append(f'{name} {key} tile {tile}: exceeds two display levels')
    return dict(status='fail' if failures else 'pass', failures=failures, checks=checks,
                reports={str(path.resolve()): lab.digest(path) for path in (left, right)})


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    for name in ('runtime-root', 'output-dir', 'basepath'):
        parser.add_argument('--' + name, type=Path, required=name == 'output-dir')
    parser.add_argument('--backend', choices=('gl', 'vk'))
    parser.add_argument('--device', default='0')
    parser.add_argument('--compare', nargs=2, type=Path, metavar=('GL_REPORT', 'VK_REPORT'))
    args = parser.parse_args()
    args.output_dir = args.output_dir.resolve()
    if args.compare:
        proof = compare(*args.compare)
        args.output_dir.mkdir(parents=True, exist_ok=True)
        (args.output_dir / 'comparison.json').write_text(json.dumps(proof, indent=2) + '\n')
        print(json.dumps(proof, indent=2))
        return int(proof['status'] != 'pass')
    if not all((args.runtime_root, args.basepath, args.backend)):
        parser.error('capture requires --runtime-root, --basepath and --backend')
    manifest = json.loads((args.runtime_root / 'pbr-lab.json').read_text())
    reference = image_capture.configure(manifest, 4)
    base = reference['image-capture-baseline']
    lab.BASE.update(r_vkDevice=args.device)
    profile = {}
    for suffix in CASES:
        name = 'tiled-capture-' + suffix
        # MSAA keeps the game's resolve target, so the batch runs without it.
        settings = dict(base['settings'], r_multiSamples='0')
        if suffix == 'direct':
            settings.update(r_postAA='0', g_renderFastNoPostDirect='1')
        commands = list(base['commands']) if suffix == CASES[0] else []
        commands += ['wait 30', f'echo "TILED_CAPTURE_{suffix}_BEGIN"', f'screenshot "screenshots/{name}-window.tga"']
        commands += [f'screenshot {width} {height}' for width, height in SIZES]
        commands += [f'echo "TILED_CAPTURE_{suffix}_END"']
        profile[name] = dict(settings=settings, commands=commands)
        lab.CASES[name] = settings
        lab.CASE_CAMERAS[name] = 'sampling'
        lab.CASE_CAPTURE_EXTENTS[name] = tuple(base['presentation'])
        lab.CASE_COMMANDS[name] = commands
        if settings['r_pbrMaterials'] == '0':
            lab.LEGACY_CASES.add(name)
    sys.argv = [__file__, '--runtime-root', str(args.runtime_root), '--output-dir', str(args.output_dir),
                '--basepath', str(args.basepath), '--backend', args.backend, '--camera', 'sampling',
                '--batch', '--cases', ','.join(profile), '--timeout', '900']
    if args.backend == 'gl':
        sys.argv.append('--gl-debug')
    code = lab.main()
    path = args.output_dir / 'report.json'
    report = json.loads(path.read_text())
    report.update(outputDir=str(args.output_dir), tiledCaptureProfile=profile, 
                  tiledCaptureProof=qualify(report, args.output_dir))
    for source in (Path(__file__), Path(image_capture.__file__), Path(smaa.__file__)):
        shutil.copy2(source, args.output_dir / 'harness' / source.name)
        report['harnessSources'][source.name] = lab.digest(source)
    path.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    proof = report['tiledCaptureProof']
    print(json.dumps(dict(status=proof['status'], failures=proof['failures'], checks=proof['checks']), indent=2), flush=True)
    return int(bool(code or proof['failures']))


if __name__ == '__main__':
    raise SystemExit(main())
