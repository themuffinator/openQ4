"""Exercise the real per-frame authored uniform limit using engine captures."""
import argparse
import json
from pathlib import Path
import re
import shutil
import sys
import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/tests'))
import renderer_vulkan_material_programs as authored
lab = authored.lab
NAMES = ('clear', 'base', 'below', 'pressure', 'recovered', 'settled', 'reloaded', 'restarted')
PROGRAM = 'tests/oq4_uniform_pressure.glsl'
PREFIX = 'textures/openq4/uniform_pressure'
EXPECTED = (
    re.compile(r"^WARNING: Vulkan authored GLSL 'tests/oq4_uniform_pressure\.glsl': material uniform ring exhausted; the stage is skipped$"),
    re.compile(r'^WARNING: Vulkan: frame uniform ring overflow \(\d+ \+ \d+ > 4194304; \d+ frames so far\); the draw is skipped$'),
)

def expected_diagnostic(line):
    # idTech log colors are retained in the raw evidence, not part of the text.
    return any(pattern.fullmatch(re.sub(r'\^[0-9]', '', line)) for pattern in EXPECTED)

def compare(paths, names=NAMES, prefix='uniform-'):
    reports = [json.loads(path.read_text()) for path in paths]
    failures, checks = [], {}
    for report, backend in zip(reports, ('gl', 'vk')):
        if report.get('pressureProof', {}).get('status') != 'pass':
            failures.append(backend + ': independent pressure proof failed')
        if [row['case'] for row in report['results']] != [prefix + name for name in names]:
            failures.append(backend + ': incomplete case list')
        for row in report['results']:
            if row['backend'] != backend: failures.append('renderer ordering must be GL then Vulkan')
            for kind in ('screenshot', 'log'):
                path = Path(row[kind])
                if not path.is_file() or lab.digest(path) != row['sha256'][kind]:
                    failures.append(row['case'] + ': changed ' + kind)
    for key in ('runtimeSHA256', 'compiledMapSHA256', 'pressureSources', 'harnessSources'):
        if reports[0].get(key) != reports[1].get(key): failures.append('different ' + key)
    if not failures:
        for left, right in zip(reports[0]['results'], reports[1]['results']):
            images = [np.asarray(Image.open(row['screenshot']).convert('RGB'), dtype=np.int16) for row in (left, right)]
            delta = np.abs(images[0] - images[1]).max(axis=2)
            checks[left['case']] = dict(maximumError=int(delta.max()), overTolerancePixels=int(np.count_nonzero(delta > 2)))
            if np.any(delta > 2): failures.append(left['case'] + ': exceeds two display levels')
    return dict(status='fail' if failures else 'pass', failures=failures, checks=checks,
                reports={str(path.resolve()): lab.digest(path) for path in paths})

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runtime-root', type=Path)
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--basepath', type=Path)
    parser.add_argument('--backend', choices=('gl', 'vk'))
    parser.add_argument('--device', default='0')
    parser.add_argument('--compare', nargs=2, type=Path, metavar=('GL_REPORT', 'VK_REPORT'))
    args = parser.parse_args()
    args.output_dir = args.output_dir.resolve()
    if args.compare:
        proof = compare(args.compare)
        args.output_dir.mkdir(parents=True, exist_ok=True)
        (args.output_dir / 'comparison.json').write_text(json.dumps(proof, indent=2) + '\n')
        print(json.dumps(proof, indent=2))
        return int(proof['status'] != 'pass')
    if not all((args.runtime_root, args.basepath, args.backend)):
        parser.error('capture needs --runtime-root, --basepath and --backend')
    manifest = json.loads((args.runtime_root / 'pbr-lab.json').read_text())
    lab.BASE.update(com_performancePreset='balanced', ui_retained='1', image_anisotropy='1',
                    r_vkDevice=args.device)
    source = lab.digest(Path(__file__))
    fixture_hashes = {}
    original_write = lab.write_capture_commands
    def write(path, commands):
        fixture_hashes.update(authored.write_fixture(args.output_dir / 'batch'))
        stage = f'''{{
blend blend
glslProgram {PROGRAM}
shaderParm Offset 0, 0, 0, 0
shaderParm Tint 0.7, 0.3, 0.1, 1
shaderParm Mode 0, 0, 0, 0
shaderTexture Image nearest clamp {authored.PREFIX}/pattern
}}
'''
        files = {f'glprogs/{PROGRAM}vp': authored.VERTEX,
                 f'glprogs/{PROGRAM}fp': authored.FRAGMENT,
                 'materials/openq4_uniform_pressure.mtr': ''.join(
                     f'{PREFIX}/{name}\n{{\ntranslucent\ntwoSided\nnoShadows\n' + stage * count + '}\n'
                     for name, count in (('base', 1), ('many', 128)))}
        for name, content in files.items():
            target = args.output_dir / 'batch/baseoq4' / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(content, encoding='utf-8')
            fixture_hashes[name] = lab.digest(target)
        augmented = []
        for command in commands:
            augmented.append(command)
            if command.startswith('echo "PBRLAB_') and command.endswith('_BEGIN"'):
                augmented += ['reportShaderPrograms', 'rendererVulkanMemoryInfo'] if args.backend == 'vk' else []
        return original_write(path, augmented)
    lab.write_capture_commands = write
    original_startup = lab.startup_args
    def startup(cvars, save):
        result = original_startup(cvars, save)
        with (save / 'baseoq4/autoexec.cfg').open('a') as config:
            for name in ('base', 'many'):
                config.write(f'touch material {PREFIX}/{name}\n')
        return result
    lab.startup_args = startup
    original_diagnostics = lab.diagnostic_lines
    lab.diagnostic_lines = lambda text: [line for line in original_diagnostics(text)
        if not expected_diagnostic(line)]
    def spawn(index, material):
        return f'spawn func_static name pressure_{index} model "{lab.lab.MODEL}" shader "{PREFIX}/{material}" origin "100 160 120" angle 0 solid 0'
    profile = {}
    for suffix in NAMES:
        commands = ['g_stopTime 0']
        if suffix == 'clear':
            commands += [f'script "${s["name"]}.hide()"' for s in manifest['stations']]
            commands += [f'script "${s}.Off()"' for s in ('key', 'blue_fill', 'warm_fill', 'lab_projector')]
        if suffix == 'base': commands += [spawn(0, 'base')]
        if suffix == 'below':
            commands += ['script "$pressure_0.remove()"', 'wait 3']
            commands += [spawn(i, 'many') for i in range(16)]
        if suffix == 'pressure': commands += [spawn(i, 'many') for i in range(16, 32)]
        if suffix == 'recovered':
            commands += [f'script "$pressure_{i}.remove()"' for i in range(32)]
            commands += ['wait 3', spawn(0, 'base')]
        if suffix == 'reloaded': commands += ['reloadARBprograms']
        if suffix == 'restarted': commands += ['vid_restart']
        commands += ['wait 15', 'g_stopTime 1']
        name = 'uniform-' + suffix
        settings = dict(r_pbrMaterials='0', r_pbrIBL='0', r_rendererReflectionProbes='0',
                        r_useLightGrid='0', r_pbrDebug='0', r_hdrToneMap='0', image_anisotropy='1')
        lab.CASES[name], lab.CASE_COMMANDS[name] = settings, commands
        lab.LEGACY_CASES.add(name)
        profile[name] = dict(settings=settings, commands=commands)
    sys.argv = [__file__, '--runtime-root', str(args.runtime_root), '--output-dir', str(args.output_dir),
                '--basepath', str(args.basepath), '--backend', args.backend, '--cases', ','.join(profile),
                '--batch', '--camera', 'coverage', '--timeout', '600']
    if args.backend == 'gl': sys.argv += ['--gl-debug']
    code = lab.main()
    path = args.output_dir / 'report.json'
    report = json.loads(path.read_text())
    failures, checks, images = [], {}, {}
    for row in report['results']:
        name = row['case'].removeprefix('uniform-')
        failures += [name + ': ' + f for f in row['failures']]
        shot = Path(row['screenshot'])
        if shot.is_file() and lab.digest(shot) == row['sha256']['screenshot']:
            images[name] = np.asarray(Image.open(shot).convert('RGB'), dtype=np.int16)
    if set(images) != set(NAMES): failures.append('missing/changed images')
    else:
        for name in NAMES:
            delta = np.abs(images[name] - images['base']).max(axis=2)
            checks[name] = dict(maximumError=int(delta.max()), differingPixels=int(np.count_nonzero(delta)))
            if name == 'clear':
                if np.count_nonzero(delta > 2) < 1000: failures.append('specimen is not visible')
            elif np.any(delta): failures.append(name + ': identical draw or recovery differs')
    log = (args.output_dir / 'batch/baseoq4/logs/openq4.log').read_text(errors='replace')
    diagnostics = original_diagnostics(log)
    expected = [line for line in diagnostics if expected_diagnostic(line)]
    if args.backend == 'vk':
        normalized = [re.sub(r'\^[0-9]', '', line) for line in expected]
        if not any(EXPECTED[0].fullmatch(line) for line in normalized): failures.append('authored uniform rejection absent')
        if not any(EXPECTED[1].fullmatch(line) for line in normalized): failures.append('real four-MiB overflow absent')
        for name in NAMES[1:]:
            section = log.split(f'PBRLAB_uniform-{name}_BEGIN', 1)[-1].split(f'PBRLAB_uniform-{name}_END', 1)[0]
            counters = re.findall(r'compiled GLSL tests/oq4_uniform_pressure\.glsl drawn=(\d+) skipped=(\d+)', section)
            checks[name]['programCounters'] = counters
            if not counters or int(counters[-1][0]) == 0: failures.append(name + ': no submitted authored draws')
            elif name in ('base', 'below', 'reloaded', 'restarted') and int(counters[-1][1]): failures.append(name + ': unexpected skipped draws')
            elif name == 'pressure' and not int(counters[-1][1]): failures.append('no skipped draws under pressure')
        if checks['settled']['programCounters'] and checks['recovered']['programCounters']:
            recovered = list(map(int, checks['recovered']['programCounters'][-1]))
            settled = list(map(int, checks['settled']['programCounters'][-1]))
            if settled[0] <= recovered[0] or settled[1] != recovered[1]:
                failures.append('reduced workload did not stop skips and resume draws')
        before = log.split('PBRLAB_uniform-below_END', 1)[0]
        after = log.split('PBRLAB_uniform-recovered_BEGIN', 1)[-1]
        if any(expected_diagnostic(line) for line in original_diagnostics(before + after)):
            failures.append('capacity warning outside the pressure interval')
    elif diagnostics: failures.append('OpenGL diagnostics')
    if not report.get('complete') or not report.get('runtimeUnchanged'): failures.append('incomplete or modified runtime')
    if any(lab.digest(args.output_dir / 'batch/baseoq4' / name) != digest for name, digest in fixture_hashes.items()):
        failures.append('fixture modified')
    assert lab.digest(Path(__file__)) == source
    shutil.copy2(__file__, args.output_dir / 'harness' / Path(__file__).name)
    report['harnessSources'][Path(__file__).name] = source
    dependency = Path(authored.__file__)
    shutil.copy2(dependency, args.output_dir / 'harness' / dependency.name)
    report['harnessSources'][dependency.name] = lab.digest(dependency)
    report.update(pressureProfile=profile, pressureSources=fixture_hashes,
                  expectedDiagnostics=expected, pressureProof=dict(status='fail' if failures else 'pass', failures=failures, checks=checks))
    path.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report['pressureProof'], indent=2))
    return int(code or bool(failures))

if __name__ == '__main__':
    raise SystemExit(main())
