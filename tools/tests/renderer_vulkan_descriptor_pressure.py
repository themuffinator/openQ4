"""Exercise a real driver's descriptor-pool refusal for authored material draws.

`r_vkMaterialDescriptorPoolTest N` gives each Vulkan frame slot one real,
unchained descriptor pool sized for N authored draws, so later draws in the
frame are refused by the driver itself (VK_ERROR_OUT_OF_POOL_MEMORY). The
fixture stacks 128 identical authored stages on one specimen: skipped stages
must leave the image unchanged, the drill must stop skipping when it is
cleared, and reload and video restart (with and without the drill) must
recover exactly. OpenGL runs the same commands as the image reference; the
cvar does not exist there and is set inertly.
"""
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
PROGRAM = pressure.PROGRAM
PREFIX = 'textures/openq4/descriptor_pressure'
DRILL_DRAWS = 32
# VK_FRAMES_IN_FLIGHT in src/renderer/Vulkan/VulkanDevice.h.
FRAMES_IN_FLIGHT = 2
STAGES = 128
NAMES = ('clear', 'base', 'below', 'drill', 'recovered', 'reloaded', 'drill-restarted', 'restarted')
# Cases that run with the drill active and must refuse; every other case must not.
DRILL_CASES = ('drill', 'drill-restarted')
EXPECTED = re.compile(r"^WARNING: Vulkan authored GLSL 'tests/oq4_uniform_pressure\.glsl': material descriptor allocation failed; the stage is skipped$")
POOL_LINE = re.compile(r'Vulkan: authored descriptor pools for frame slot (\d+) now hold (\d+) draws')


def plain(line):
    return re.sub(r'\^[0-9]', '', line)


def expected_diagnostic(line):
    return bool(EXPECTED.fullmatch(plain(line)))


def section(log, name):
    """The captured state at the end of a case (after its commands and wait)."""
    return log.split(f'PBRLAB_descriptor-{name}_BEGIN', 1)[-1].split(f'PBRLAB_descriptor-{name}_END', 1)[0]


def interval(log, name):
    """Everything a case's commands printed: from the previous case's end to its own."""
    index = NAMES.index(name)
    start = log.split(f'PBRLAB_descriptor-{NAMES[index - 1]}_END', 1)[-1] if index else log
    return start.split(f'PBRLAB_descriptor-{name}_END', 1)[0]


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--runtime-root', type=Path)
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--basepath', type=Path)
    parser.add_argument('--backend', choices=('gl', 'vk'))
    parser.add_argument('--device', default='0')
    parser.add_argument('--compare', nargs=2, type=Path, metavar=('GL_REPORT', 'VK_REPORT'))
    args = parser.parse_args()
    args.output_dir = args.output_dir.resolve()
    if args.compare:
        proof = pressure.compare(args.compare, NAMES, 'descriptor-')
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
                 'materials/openq4_descriptor_pressure.mtr': ''.join(
                     f'{PREFIX}/{name}\n{{\ntranslucent\ntwoSided\nnoShadows\n' + stage * count + '}\n'
                     for name, count in (('base', 1), ('many', STAGES)))}
        for name, content in files.items():
            target = args.output_dir / 'batch/baseoq4' / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(content, encoding='utf-8')
            fixture_hashes[name] = lab.digest(target)
        augmented = []
        for command in commands:
            augmented.append(command)
            if command.startswith('echo "PBRLAB_') and command.endswith('_BEGIN"') and args.backend == 'vk':
                augmented += ['reportShaderPrograms']
        return original_write(path, augmented)
    lab.write_capture_commands = write
    original_startup = lab.startup_args

    def startup(cvars, save):
        result = original_startup(cvars, save)
        with (save / 'baseoq4/autoexec.cfg').open('a') as config:
            config.write('set r_vkMaterialDescriptorPoolTest 0\n')
            for name in ('base', 'many'):
                config.write(f'touch material {PREFIX}/{name}\n')
        return result
    lab.startup_args = startup
    original_diagnostics = lab.diagnostic_lines
    lab.diagnostic_lines = lambda text: [line for line in original_diagnostics(text) if not expected_diagnostic(line)]

    def spawn(material):
        return f'spawn func_static name pressure_0 model "{lab.lab.MODEL}" shader "{PREFIX}/{material}" origin "100 160 120" angle 0 solid 0'
    profile = {}
    for suffix in NAMES:
        commands = ['g_stopTime 0']
        if suffix == 'clear':
            commands += [f'script "${s["name"]}.hide()"' for s in manifest['stations']]
            commands += [f'script "${s}.Off()"' for s in ('key', 'blue_fill', 'warm_fill', 'lab_projector')]
        if suffix == 'base': commands += [spawn('base')]
        if suffix == 'below': commands += ['script "$pressure_0.remove()"', 'wait 3', spawn('many')]
        if suffix == 'drill': commands += [f'set r_vkMaterialDescriptorPoolTest {DRILL_DRAWS}']
        if suffix == 'recovered':
            # Each frame slot returns to production pools at its own next frame
            # start; skips may continue only until both slots have switched.
            commands += ['set r_vkMaterialDescriptorPoolTest 0', 'wait 10']
            if args.backend == 'vk': commands += ['echo "DESCRIPTOR_SETTLED"', 'reportShaderPrograms']
        if suffix == 'reloaded': commands += ['reloadARBprograms']
        if suffix == 'drill-restarted': commands += [f'set r_vkMaterialDescriptorPoolTest {DRILL_DRAWS}', 'vid_restart']
        if suffix == 'restarted': commands += ['set r_vkMaterialDescriptorPoolTest 0', 'vid_restart']
        commands += ['wait 15', 'g_stopTime 1']
        name = 'descriptor-' + suffix
        settings = dict(r_pbrMaterials='0', r_pbrIBL='0', r_rendererReflectionProbes='0',
                        r_useLightGrid='0', r_pbrDebug='0', r_hdrToneMap='0', image_anisotropy='1')
        lab.CASES[name], lab.CASE_COMMANDS[name] = settings, commands
        lab.LEGACY_CASES.add(name)
        profile[name] = dict(settings=settings, commands=commands)
    sys.argv = [__file__, '--runtime-root', str(args.runtime_root), '--output-dir', str(args.output_dir),
                '--basepath', str(args.basepath), '--backend', args.backend, '--cases', ','.join(profile),
                '--batch', '--camera', 'coverage', '--timeout', '900']
    if args.backend == 'gl': sys.argv += ['--gl-debug']
    code = lab.main()
    path = args.output_dir / 'report.json'
    report = json.loads(path.read_text())
    failures, checks, images = [], {}, {}
    for row in report['results']:
        name = row['case'].removeprefix('descriptor-')
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
        # reportShaderPrograms runs after each case's commands and wait, so a
        # section's counters are that case's end state; they are cumulative
        # since the last reload or video restart.
        counters = {}
        for name in NAMES[1:]:
            rows = re.findall(r'compiled GLSL tests/oq4_uniform_pressure\.glsl drawn=(\d+) skipped=(\d+)', section(log, name))
            text = interval(log, name)
            checks.setdefault(name, {}).update(programCounters=rows, poolChanges=POOL_LINE.findall(text),
                                refusals=sum(1 for line in text.splitlines() if expected_diagnostic(line)))
            if not rows or int(rows[-1][0]) == 0:
                failures.append(name + ': no submitted authored draws')
                continue
            counters[name] = tuple(map(int, rows[-1]))
        if len(counters) == len(NAMES) - 1:
            if counters['below'][1]: failures.append('below: skipped draws before the drill')
            if counters['drill'][1] <= counters['below'][1]: failures.append('drill: no skipped draws')
            settled_text = interval(log, 'recovered').split('DESCRIPTOR_SETTLED', 1)
            settled_rows = re.findall(r'compiled GLSL tests/oq4_uniform_pressure\.glsl drawn=(\d+) skipped=(\d+)',
                                      settled_text[1].split('PBRLAB_descriptor-recovered_BEGIN', 1)[0]) if len(settled_text) == 2 else []
            settled = tuple(map(int, settled_rows[-1])) if settled_rows else None
            checks['recovered']['settledCounters'] = settled
            transition = (FRAMES_IN_FLIGHT + 1) * (STAGES - DRILL_DRAWS)
            if settled is None:
                failures.append('recovered: missing settled counters')
            else:
                checks['skipsWhileSlotsSwitch'] = settled[1] - counters['drill'][1]
                if settled[1] - counters['drill'][1] > transition:
                    failures.append('recovered: skips continued after every frame slot switched')
                if counters['recovered'][1] != settled[1] or counters['recovered'][0] <= settled[0]:
                    failures.append('recovered: clearing the drill did not stop skips and resume draws')
            if counters['reloaded'][1]: failures.append('reloaded: skipped draws after reload')
            if not counters['drill-restarted'][1]: failures.append('drill-restarted: no skipped draws after restart')
            if counters['restarted'][1]: failures.append('restarted: skipped draws after the drill was cleared')
            checks['skipsDuringDrill'] = counters['drill'][1] - counters['below'][1]
            checks['skipsAfterDrillRestart'] = counters['drill-restarted'][1]
        for name in DRILL_CASES:
            if not any(int(pool) == DRILL_DRAWS for _, pool in checks[name]['poolChanges']):
                failures.append(name + ': drill pools were not created')
            if not checks[name]['refusals']:
                failures.append(name + ': the driver refusal was not reported')
        if not any(int(pool) == 2048 for _, pool in checks['recovered']['poolChanges']):
            failures.append('recovered: production pools were not restored')
        for name in NAMES[1:]:
            if name not in DRILL_CASES and checks[name]['refusals']:
                failures.append(name + ': refusal outside a drill interval')
    elif diagnostics: failures.append('OpenGL diagnostics')
    if not report.get('complete') or not report.get('runtimeUnchanged'): failures.append('incomplete or modified runtime')
    if any(lab.digest(args.output_dir / 'batch/baseoq4' / name) != digest for name, digest in fixture_hashes.items()):
        failures.append('fixture modified')
    assert lab.digest(Path(__file__)) == source
    shutil.copy2(__file__, args.output_dir / 'harness' / Path(__file__).name)
    report['harnessSources'][Path(__file__).name] = source
    for dependency in (Path(authored.__file__), Path(pressure.__file__)):
        shutil.copy2(dependency, args.output_dir / 'harness' / dependency.name)
        report['harnessSources'][dependency.name] = lab.digest(dependency)
    report.update(pressureProfile=profile, pressureSources=fixture_hashes, expectedDiagnostics=expected,
                  pressureProof=dict(status='fail' if failures else 'pass', failures=failures, checks=checks))
    path.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report['pressureProof'], indent=2))
    return int(code or bool(failures))


if __name__ == '__main__':
    raise SystemExit(main())
