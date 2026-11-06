#!/usr/bin/env python3
"""Compare nested, legacy-cropped and offset views between GL and Vulkan.

The scene is the emissive specimen of the SMAA laboratory fixture that
renderer_image_capture.py qualifies (lights off, so only unlit emission is
drawn). Cases, each captured with engine screenshots:

- nested: a func_cameraview close in front of the specimen, and a flat
  screen beside it showing that camera through a `remoteRenderMap` material
  (a subview cropped, captured into an image and sampled in the main view).
  The screen must be flat: like Doom 3, precise subview culling rejects any
  surface with a back-facing triangle, so a sphere never renders a subview;
- nested-control: the same screen with a constant material;
- nested-cropped: the nested case inside a 50% legacy crop
  (r_screenFraction 50, r_resolutionScaleMode 0): the remote crop is pushed
  inside the scene crop;
- cropped-cube: an `envshot` cube capture (always a legacy crop at 256x256)
  inside the 50% scene crop;
- offset-gui: a test GUI whose renderDef window is offset from the origin,
  so its 3D view's viewport starts at a non-zero x/y;
- nested-restarted: `vid_restart`, which must reproduce `nested` exactly.

Each backend must show the specimen's cyan on the screen (and the control
must not), keep it through the crop and restart, and draw the offset window; --compare then requires every GL/Vulkan image and
cube face to agree within the existing two-display-level limit.
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
PREFIX = 'textures/openq4/view_capture'
DIRECT = {}
CASES = ('nested', 'nested-control', 'nested-cropped', 'cropped-cube', 'offset-gui', 'nested-restarted')
FACES = ('px', 'nx', 'py', 'ny', 'pz', 'nz')
MATERIALS = f'''{PREFIX}/remote
{{
	noshadows
	{{
		remoteRenderMap 256 256
		map textures/common/camera1.tga
		scale 1, -1
	}}
}}
{PREFIX}/constant
{{
	noshadows
	{{
		map _white
		rgb 0.3
	}}
}}
'''
GUI = '''windowDef Desktop
{
	rect 0, 0, 640, 480
	backcolor 0, 0, 0, 0
	visible 1
	renderDef offsetView
	{
		rect 360, 250, 240, 180
		visible 1
		backcolor 0, 0, 0, 0
		model "models/openq4/pbr_lab/sphere.ase"
		modelOrigin 0, 0, 0, 0
		viewOffset -160, 0, 0, 0
		lightOrigin0 -100, -50, 60, 0
		lightColor0 1, 1, 1, 0
	}
}
'''
SCREEN = 'spawn func_static name view_screen model "models/openq4/pbr_lab/backface_receiver.ase" shader "{shader}" origin "-200 -700 440" angle 0 solid 0 cameraTarget view_camera'
# The screen covers the left of the frame; the specimen itself stays right of it.
SCREEN_COLUMNS = slice(0, 520)
NESTED_CASES = ('nested', 'nested-cropped', 'nested-restarted')
CAMERA = 'spawn func_cameraview name view_camera origin "0 -800 440" angle 90'


def load(path):
    return np.asarray(Image.open(path).convert('RGB'), dtype=np.int16)


def screen_cyan(image):
    region = image[:, SCREEN_COLUMNS]
    return int(np.count_nonzero((region[..., 0] <= 80) & (region[..., 1] >= 150) & (region[..., 2] >= 200)))


def section_writes(log, name):
    section = log.split(f'VIEW_CAPTURE_{name}_BEGIN', 1)[-1].split(f'VIEW_CAPTURE_{name}_END', 1)[0]
    return re.findall(r'^Wrote (\S+\.tga)', re.sub(r'\^[0-9]', '', section), re.M)


def profile_commands():
    screen = {'nested': 'remote', 'nested-control': 'constant', 'nested-cropped': 'remote'}
    profile, spawned = {}, False
    for name in CASES:
        commands = ['g_stopTime 0']
        if name == 'nested':
            commands += [CAMERA]
        if name in screen:
            if spawned:
                commands += ['script "$view_screen.remove()"', 'wait 3']
            commands += [SCREEN.format(shader=f'{PREFIX}/{screen[name]}')]
            spawned = True
        if name == 'offset-gui':
            commands += ['testGUI guis/openq4_view_capture.gui']
        if name == 'nested-restarted':
            commands += ['testGUI', 'vid_restart']
        commands += ['wait 30', 'g_stopTime 1', 'wait 5', f'echo "VIEW_CAPTURE_{name}_BEGIN"',
                     f'screenshot "screenshots/view-{name}.tga"']
        if name == 'cropped-cube':
            commands += [f'envshot "view-{name}" 256 1']
        commands += [f'echo "VIEW_CAPTURE_{name}_END"']
        profile[name] = commands
    return profile


def qualify(report, output):
    failures, checks, files = [], {}, {}
    rows = {row['case'].removeprefix('view-capture-'): row for row in report['results']}
    if not report.get('complete') or not report.get('runtimeUnchanged') or set(rows) != set(CASES):
        failures.append('incomplete sequence, changed runtime or case set')
        return dict(status='fail', failures=failures, checks=checks, files=files)
    root = output / 'batch/baseoq4'
    log = Path(next(iter(rows.values()))['log']).read_text(errors='replace')
    images = {}
    for name, row in rows.items():
        failures += [name + ': ' + error for error in row['failures']]
        path = root / f'screenshots/view-{name}.tga'
        if not path.is_file():
            failures.append(name + ': capture missing')
            continue
        files[name] = dict(path=str(path.relative_to(root)), sha256=lab.digest(path))
        images[name] = load(path)
        if name == 'cropped-cube':
            for face in FACES:
                cube = root / f'env/view-{name}_{face}.tga'
                if not cube.is_file():
                    failures.append(f'{name}: cube face {face} missing')
                    continue
                files[f'{name}-{face}'] = dict(path=str(cube.relative_to(root)), sha256=lab.digest(cube))
        if f'screenshots/view-{name}.tga' not in section_writes(log, name):
            failures.append(name + ': screenshot not reported as written')
    if set(images) == set(CASES):
        def changed(a, b):
            return int(np.count_nonzero(np.abs(images[a] - images[b]).max(axis=2) > 2))
        checks['nestedVsControl'] = changed('nested', 'nested-control')
        checks['offsetGuiVsNested'] = changed('offset-gui', 'nested-control')
        checks['croppedVsNested'] = changed('nested-cropped', 'nested')
        checks['restartedExact'] = files['nested']['sha256'] == files['nested-restarted']['sha256']
        checks['screenCyan'] = {name: screen_cyan(images[name]) for name in (*NESTED_CASES, 'nested-control')}
        for name in NESTED_CASES:
            if checks['screenCyan'][name] < 2000:
                failures.append(name + ': the remote camera view is not on the screen')
        if checks['screenCyan']['nested-control'] > 50: failures.append('control screen shows the specimen colour')
        if checks['nestedVsControl'] < 500: failures.append('nested view is not visible on the screen surface')
        if checks['offsetGuiVsNested'] < 500: failures.append('offset GUI render window is not drawn')
        if not checks['restartedExact']: failures.append('nested view did not recover exactly after vid_restart')
    return dict(status='fail' if failures else 'pass', failures=failures, checks=checks, files=files)


def compare(left, right):
    reports = [json.loads(path.read_text()) for path in (left, right)]
    failures, checks = [], {}
    for backend, report in zip(('gl', 'vk'), reports):
        if report.get('viewCaptureProof', {}).get('status') != 'pass':
            failures.append(backend + ': independent view-capture proof failed')
    for key in ('runtimeSHA256', 'compiledMapSHA256'):
        if reports[0].get(key) != reports[1].get(key):
            failures.append('different ' + key)
    files = [report.get('viewCaptureProof', {}).get('files', {}) for report in reports]
    # Resolve captures beside each report, so retained evidence can be moved.
    roots = [path.resolve().parent / 'batch/baseoq4' for path in (left, right)]
    for key in sorted(set(files[0]) | set(files[1])):
        try:
            a, b = (load(root / f[key]['path']) for root, f in zip(roots, files))
        except (KeyError, OSError):
            failures.append(key + ': capture missing on one backend')
            continue
        if a.shape != b.shape:
            failures.append(key + ': sizes differ')
            continue
        delta = np.abs(a - b).max(axis=2)
        checks[key] = dict(maximumError=int(delta.max()), overTolerancePixels=int(np.count_nonzero(delta > 2)))
        if np.any(delta > 2):
            failures.append(key + ': exceeds two display levels')
    return dict(status='fail' if failures else 'pass', failures=failures, checks=checks,
                reports={str(path.resolve()): lab.digest(path) for path in (left, right)})


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    for name in ('runtime-root', 'output-dir', 'basepath'):
        parser.add_argument('--' + name, type=Path, required=name == 'output-dir')
    parser.add_argument('--backend', choices=('gl', 'vk'))
    parser.add_argument('--device', default='0')
    parser.add_argument('--samples', type=int, choices=(4, 8), default=4)
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
    base = image_capture.configure(manifest, args.samples)['image-capture-baseline']
    lab.BASE.update(r_vkDevice=args.device)
    fixtures = {}
    original_write = lab.write_capture_commands

    def write(path, commands):
        for name, content in (('materials/openq4_view_capture.mtr', MATERIALS), ('guis/openq4_view_capture.gui', GUI)):
            target = args.output_dir / 'batch/baseoq4' / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(content, encoding='utf-8')
            fixtures[name] = lab.digest(target)
        return original_write(path, commands)
    lab.write_capture_commands = write
    original_startup = lab.startup_args

    def startup(cvars, save):
        result = original_startup(cvars, save)
        with (save / 'baseoq4/autoexec.cfg').open('a') as config:
            for material in ('remote', 'constant'):
                config.write(f'touch material {PREFIX}/{material}\n')
        return result
    lab.startup_args = startup
    # The camera is spawned after map load, as no fixture map entity uses it.
    expected = "WARNING: Loading non pre-cached entityDef decl func_cameraview"
    original_diagnostics = lab.diagnostic_lines
    lab.diagnostic_lines = lambda text: [line for line in original_diagnostics(text)
                                        if re.sub(r'\^[0-9]', '', line).strip() != expected]
    profile = {}
    for index, (suffix, commands) in enumerate(profile_commands().items()):
        name = 'view-capture-' + suffix
        settings = dict(base['settings'], **DIRECT)
        if suffix in ('nested-cropped', 'cropped-cube'):
            settings.update(r_screenFraction='50', r_resolutionScaleMode='0')
        if index == 0:
            commands = list(base['commands']) + commands
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
    proof = qualify(report, args.output_dir)
    if any(lab.digest(args.output_dir / 'batch/baseoq4' / name) != digest for name, digest in fixtures.items()):
        proof['failures'].append('fixture modified')
        proof['status'] = 'fail'
    report.update(outputDir=str(args.output_dir), viewCaptureProfile=profile, viewCaptureFixtures=fixtures,
                  requestedSamples=args.samples, viewCaptureProof=proof)
    for source in (Path(__file__), Path(image_capture.__file__)):
        shutil.copy2(source, args.output_dir / 'harness' / source.name)
        report['harnessSources'][source.name] = lab.digest(source)
    path.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(dict(status=proof['status'], failures=proof['failures'], checks=proof['checks']), indent=2), flush=True)
    return int(bool(code or proof['failures']))


if __name__ == '__main__':
    raise SystemExit(main())
