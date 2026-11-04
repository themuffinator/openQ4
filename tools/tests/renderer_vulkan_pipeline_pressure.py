"""Probe authored pipeline capacity and reload recovery with engine images."""
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
import renderer_vulkan_uniform_pressure as pressure
lab = authored.lab
PREFIX = 'textures/openq4/pipeline_pressure'
NAMES = ('clear', 'base', 'below', 'pressure', 'recovered', 'reloaded', 'settled', 'restarted')
EXPECTED = re.compile(r"^WARNING: Vulkan authored GLSL 'tests/oq4_pipeline_\d+\.glsl': material pipeline allocation failed; the stage is skipped$")

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--runtime-root', type=Path)
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--basepath', type=Path)
    parser.add_argument('--backend', choices=('gl', 'vk'))
    parser.add_argument('--device', default='0')
    parser.add_argument('--compare', nargs=2, type=Path, metavar=('GL_REPORT', 'VK_REPORT'))
    args = parser.parse_args()
    args.output_dir = args.output_dir.resolve()
    if args.compare:
        proof = pressure.compare(args.compare, NAMES, 'pipeline-')
        args.output_dir.mkdir(parents=True, exist_ok=True)
        (args.output_dir / 'comparison.json').write_text(json.dumps(proof, indent=2) + '\n')
        print(json.dumps(proof, indent=2))
        return int(proof['status'] != 'pass')
    if not all((args.runtime_root, args.basepath, args.backend)):
        parser.error('capture needs --runtime-root, --basepath and --backend')
    manifest = json.loads((args.runtime_root / 'pbr-lab.json').read_text())
    lab.BASE.update(com_performancePreset='balanced', ui_retained='1', image_anisotropy='1', r_vkDevice=args.device)
    source = lab.digest(Path(__file__))
    fixture_hashes = {}
    original_write = lab.write_capture_commands
    def write(path, commands):
        fixture_hashes.update(authored.write_fixture(args.output_dir / 'batch'))
        files, materials = {}, []
        for i in range(66):
            program = f'tests/oq4_pipeline_{i}.glsl'
            files[f'glprogs/{program}vp'] = authored.VERTEX
            files[f'glprogs/{program}fp'] = authored.FRAGMENT
            stages = []
            for mask in range(8):
                masks = '\n'.join('mask' + color for bit, color in enumerate(('Red', 'Green', 'Blue')) if mask & (1 << bit))
                stages.append(f'''{{
blend blend
{masks}
glslProgram {program}
shaderParm Offset 0, 0, 0, 0
shaderParm Tint 0.7, 0.3, 0.1, 1
shaderParm Mode 0, 0, 0, 0
shaderTexture Image nearest clamp {authored.PREFIX}/pattern
}}
''')
            materials.append(f'{PREFIX}/{i}\n{{\ntranslucent\ntwoSided\nnoShadows\n' + ''.join(stages) + '}\n')
            if i == 0: materials.append(f'{PREFIX}/base\n{{\ntranslucent\ntwoSided\nnoShadows\n' + stages[0] + '}\n')
        files['materials/openq4_pipeline_pressure.mtr'] = ''.join(materials)
        for name, content in files.items():
            target = args.output_dir / 'batch/baseoq4' / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(content, encoding='utf-8')
            fixture_hashes[name] = lab.digest(target)
        augmented = []
        for command in commands:
            augmented.append(command)
            if command.startswith('echo "PBRLAB_') and command.endswith('_BEGIN"') and args.backend == 'vk':
                augmented += ['reportShaderPrograms', 'rendererVulkanMemoryInfo']
        return original_write(path, augmented)
    lab.write_capture_commands = write
    original_startup = lab.startup_args
    def startup(cvars, save):
        result = original_startup(cvars, save)
        with (save / 'baseoq4/autoexec.cfg').open('a') as config:
            for name in ('base', *range(66)):
                config.write(f'touch material {PREFIX}/{name}\n')
        return result
    lab.startup_args = startup
    original_diagnostics = lab.diagnostic_lines
    lab.diagnostic_lines = lambda text: [line for line in original_diagnostics(text)
        if not EXPECTED.fullmatch(re.sub(r'\^[0-9]', '', line))]
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
            commands += [spawn(i, str(i)) for i in range(63)]
        if suffix == 'pressure': commands += [spawn(i, str(i)) for i in range(63, 65)]
        if suffix == 'recovered':
            commands += [f'script "$pressure_{i}.remove()"' for i in range(65)]
            commands += ['wait 3', spawn(0, 'base')]
        if suffix == 'reloaded':
            commands += ['script "$pressure_0.remove()"', 'wait 3', 'reloadARBprograms', spawn(0, '65')]
        if suffix == 'restarted': commands += ['vid_restart']
        commands += ['wait 15', 'g_stopTime 1']
        name = 'pipeline-' + suffix
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
        name = row['case'].removeprefix('pipeline-')
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
    expected = [line for line in original_diagnostics(log) if EXPECTED.fullmatch(re.sub(r'\^[0-9]', '', line))]
    if args.backend == 'vk':
        if not expected: failures.append('pipeline rejection absent')
        before = log.split('PBRLAB_pipeline-below_END', 1)[0]
        after = log.split('PBRLAB_pipeline-recovered_BEGIN', 1)[-1]
        if any(EXPECTED.fullmatch(re.sub(r'\^[0-9]', '', line)) for line in original_diagnostics(before + after)):
            failures.append('capacity warning outside the pressure interval')
        for name in NAMES[1:]:
            section = log.split(f'PBRLAB_pipeline-{name}_BEGIN', 1)[-1].split(f'PBRLAB_pipeline-{name}_END', 1)[0]
            stats = re.search(r'Vulkan authored GLSL/ARB programs: (\d+) of 256 versions \((\d+) current, (\d+) invalid, (\d+) evicted\)', section)
            if not stats:
                failures.append(name + ': missing cache statistics')
                continue
            live, current, invalid, evicted = map(int, stats.groups())
            checks[name]['cache'] = dict(live=live, current=current, invalid=invalid, evicted=evicted)
            if invalid: failures.append(name + ': invalid programs')
            if name == 'below' and current != 63: failures.append('below limit did not draw 63 programs')
            if name == 'pressure' and current != 65: failures.append('pressure did not reach 65 programs')
            if name in ('reloaded', 'settled'):
                if not evicted: failures.append(name + ': retired pipelines were not reclaimed')
                counters = re.findall(r'compiled GLSL tests/oq4_pipeline_65\.glsl drawn=(\d+) skipped=(\d+)', section)
                checks[name]['programCounters'] = counters
                if not counters or int(counters[-1][0]) == 0 or int(counters[-1][1]) != 0:
                    failures.append(name + ': new program failed after reload')
    if not report.get('complete') or not report.get('runtimeUnchanged'): failures.append('incomplete or modified runtime')
    if any(lab.digest(args.output_dir / 'batch/baseoq4' / name) != digest for name, digest in fixture_hashes.items()):
        failures.append('fixture modified')
    assert lab.digest(Path(__file__)) == source
    shutil.copy2(__file__, args.output_dir / 'harness' / Path(__file__).name)
    report['harnessSources'][Path(__file__).name] = source
    for dependency in (Path(authored.__file__), Path(pressure.__file__)):
        shutil.copy2(dependency, args.output_dir / 'harness' / dependency.name)
        report['harnessSources'][dependency.name] = lab.digest(dependency)
    report.update(pressureProfile=profile, pressureSources=fixture_hashes,
                  expectedDiagnostics=expected, pressureProof=dict(status='fail' if failures else 'pass', failures=failures, checks=checks))
    path.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report['pressureProof'], indent=2))
    return int(code or bool(failures))

if __name__ == '__main__': raise SystemExit(main())
