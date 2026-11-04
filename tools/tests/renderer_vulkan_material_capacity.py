#!/usr/bin/env python3
"""Exercise authored-program eviction through real draws and source reloads.

Run twice: --padding-bytes 0 reaches the 256-version limit; --padding-bytes
196608 reaches the 32 MiB source/module budget first. A file handshake advances
each edit only after the preceding version has drawn. All captures come from
the engine in a hidden, input-disabled window. The installed assets are read-only.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import sys
import threading

import renderer_pbr_laboratory as lab
import renderer_vulkan_material_programs as authored

STATES = re.compile(r'Vulkan authored GLSL/ARB programs: (\d+) of (\d+) versions '
                    r'\((\d+) current, (\d+) invalid, (\d+) evicted\), (\d+) of (\d+) compiler bytes')
SOURCE = 'glprogs/tests/oq4_authored.glslfp'
CASES = ('baseline', 'stress', 'restored', 'partial', 'restart')


def source(version: int, padding: int) -> bytes:
    return (authored.FRAGMENT + f'\n// version {version}\n/*' + 'x' * padding + '*/\n').encode()


def gate(version: int) -> str:
    return f'vk_capacity_{version}.cfg'


def advance(version: int) -> list[str]:
    return [f'echo VK_CAPACITY_EDIT_{version}', 'wait 6', f'exec {gate(version)}']


def prove(report: dict) -> dict:
    failures, checks = [], {}
    rows = report.get('results', [])
    if (not report.get('complete') or not report.get('runtimeUnchanged')
            or [row['case'] for row in rows] != ['capacity-' + name for name in CASES]):
        failures.append('incomplete capture set or changed runtime')
    images = {}
    for row in rows:
        failures.extend(row['case'] + ': ' + error for error in row['failures'])
        for kind in ('screenshot', 'log'):
            path = Path(row.get(kind, ''))
            if not path.is_file() or lab.digest(path) != row['sha256'].get(kind):
                failures.append(row['case'] + ': missing or changed ' + kind)
        path = Path(row['screenshot'])
        if path.is_file():
            images[row['case']] = lab.capture_rgb(path)
    baseline = images.get('capacity-baseline')
    if baseline is None or len(set(baseline)) < 32:
        failures.append('baseline capture is missing or blank')
    for name, pixels in images.items():
        if pixels != baseline:
            failures.append(name + ': output changed after a source-only edit/restart')
    if rows and Path(rows[0]['log']).is_file():
        text = Path(rows[0]['log']).read_text(errors='replace')
        # Exclude the full restart: its new device deliberately starts at zero.
        section = text.split('VK_CAPACITY_STRESS_END', 1)[0]
        states = [tuple(map(int, match)) for match in STATES.findall(section)]
        draws = section.count(f"Vulkan: drawing authored GLSL '{authored.PROGRAM}'")
        checks.update(states=states, drawnVersions=draws)
        if len(states) != report['edits'] + 1 or draws != report['edits'] + 1:
            failures.append('not every edited version was reported and drawn')
        if any(count > limit or invalid or used > budget for count, limit, _, invalid, _, used, budget in states):
            failures.append('invalid version or exceeded capacity')
        if not states or states[-1][4] == 0:
            failures.append('the run did not reach eviction pressure')
        if report['paddingBytes'] == 0 and states and states[-1][0] != 256:
            failures.append('the run did not reach the version limit')
        if report['paddingBytes'] and states and max(state[0] for state in states) >= 256:
            failures.append('the padded run did not isolate storage pressure')
    journal = report.get('sourceEdits', [])
    if [entry['version'] for entry in journal] != list(range(1, report['edits'] + 1)):
        failures.append('incomplete source-edit handshake')
    for entry in journal:
        if entry['sha256'] != hashlib.sha256(source(entry['version'], report['paddingBytes'])).hexdigest():
            failures.append('source edit hash mismatch')
    failures.extend(report.get('sourceEditErrors', []))
    return dict(status='fail' if failures else 'pass', failures=failures, checks=checks)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runtime-root', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--basepath', type=Path, required=True)
    parser.add_argument('--padding-bytes', type=int, choices=(0, 196608), default=0)
    parser.add_argument('--edits', type=int, default=270)
    parser.add_argument('--timeout', type=int, default=900)
    args = parser.parse_args()
    minimum = 180 if args.padding_bytes else 260
    if args.edits < minimum or args.edits > 1024:
        parser.error(f'--edits must be between {minimum} and 1024 for this padding size')
    args.runtime_root, args.output_dir = args.runtime_root.resolve(), args.output_dir.resolve()
    manifest = json.loads((args.runtime_root / 'pbr-lab.json').read_text())
    save = args.output_dir / 'batch'
    journal, errors = [], []
    harnesses = {Path(__file__): lab.digest(Path(__file__)), Path(authored.__file__): lab.digest(Path(authored.__file__))}
    lab.BASE.update(image_anisotropy='1')
    for name in CASES:
        commands = []
        if name == 'baseline':
            commands = ['g_stopTime 0']
            commands += [f'script "${station["name"]}.hide()"' for station in manifest['stations']]
            commands += [f'script "${light}.Off()"' for light in ('key', 'blue_fill', 'warm_fill', 'lab_projector')]
            commands += [f'spawn func_static name capacity_specimen model "{lab.lab.MODEL}" '
                         f'shader "{authored.PREFIX}/base" origin "100 160 120" solid 0',
                         'wait 30', 'g_stopTime 1', 'reportShaderPrograms summary']
        elif name == 'stress':
            commands = advance(1) + ['echo VK_CAPACITY_STRESS_END']
        else:
            commands = [{'restored': 'reloadGLSLprograms', 'partial': 'vid_restart partial',
                         'restart': 'vid_restart'}[name]]
        case = 'capacity-' + name
        lab.CASES[case] = dict(r_pbrMaterials='0', r_pbrIBL='0', r_rendererReflectionProbes='0',
                              r_useLightGrid='0', r_pbrDebug='0', r_hdrToneMap='0', image_anisotropy='1')
        lab.CASE_COMMANDS[case] = commands
        lab.LEGACY_CASES.add(case)
    original_write, original_run, original_startup = lab.write_capture_commands, lab.run, lab.startup_args

    def write_commands(path, commands):
        authored.write_fixture(save)
        (save / 'baseoq4' / SOURCE).write_bytes(source(0, args.padding_bytes))
        for version in range(1, args.edits + 1):
            (save / 'baseoq4' / gate(version)).write_text(f'wait 6\nexec {gate(version)}\n')
        return original_write(path, commands)

    def startup(cvars, directory):
        result = original_startup(cvars, directory)
        with (directory / 'baseoq4/autoexec.cfg').open('a') as cfg:
            cfg.write(f'touch material {authored.PREFIX}/base\n')
        return result

    def run(command, cwd, output, timeout):
        stop = threading.Event()

        def replace(target, data):
            pending = target.with_suffix('.pending')
            pending.write_bytes(data)
            # Windows refuses replacement while exec has the old gate open.
            # Retry that sharing race; never expose a partially written cfg.
            for attempt in range(250):
                try:
                    pending.replace(target)
                    return
                except PermissionError:
                    if stop.wait(0.02):
                        raise RuntimeError('source-edit handshake interrupted')
            raise RuntimeError('source-edit replacement remained locked: ' + str(target))

        def watch():
            try:
                version = 1
                log = save / 'baseoq4/logs/openq4.log'
                offset, partial = 0, ''
                markers = set()
                while version <= args.edits and not stop.wait(0.02):
                    if not log.is_file():
                        continue
                    # A complete echo line, never a command listing or a prefix
                    # match for version 1 inside version 10.
                    with log.open('r', errors='replace') as stream:
                        stream.seek(offset)
                        chunk = partial + stream.read()
                        offset = stream.tell()
                    lines = chunk.split('\n')
                    partial = lines.pop()
                    markers.update(re.sub(r'\^(?:c\d{3}|\d)', '', line).strip() for line in lines
                                   if 'VK_CAPACITY_EDIT_' in line)
                    if f'VK_CAPACITY_EDIT_{version}' not in markers:
                        continue
                    data = source(version, args.padding_bytes)
                    target = save / 'baseoq4' / SOURCE
                    replace(target, data)
                    continuation = ['reloadGLSLprograms', 'wait 6', 'reportShaderPrograms summary']
                    if version < args.edits:
                        continuation += advance(version + 1)
                    target = save / 'baseoq4' / gate(version)
                    replace(target, ('\n'.join(continuation) + '\n').encode())
                    journal.append(dict(version=version, sha256=hashlib.sha256(data).hexdigest()))
                    version += 1
            except Exception as error:
                errors.append(str(error))

        thread = threading.Thread(target=watch, name='vulkan-capacity-edits', daemon=True)
        thread.start()
        try:
            return original_run(command, cwd, output, timeout)
        finally:
            stop.set()
            thread.join(timeout=5)
            if thread.is_alive():
                errors.append('source edit watcher did not stop')

    lab.write_capture_commands, lab.run, lab.startup_args = write_commands, run, startup
    sys.argv = [__file__, '--runtime-root', str(args.runtime_root), '--output-dir', str(args.output_dir),
                '--basepath', str(args.basepath), '--backend', 'vk', '--batch', '--camera', 'coverage',
                '--cases', ','.join('capacity-' + name for name in CASES), '--timeout', str(args.timeout)]
    code = lab.main()
    path = args.output_dir / 'report.json'
    report = json.loads(path.read_text())
    report.update(edits=args.edits, paddingBytes=args.padding_bytes, sourceEdits=journal, sourceEditErrors=errors)
    for harness, digest in harnesses.items():
        if lab.digest(harness) != digest:
            errors.append('harness changed during run: ' + str(harness))
        shutil.copy2(harness, args.output_dir / 'harness' / harness.name)
        report['harnessSources'][harness.name] = digest
    report['capacityProof'] = prove(report)
    path.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report['capacityProof'], indent=2))
    return int(bool(code) or report['capacityProof']['status'] != 'pass')


if __name__ == '__main__':
    raise SystemExit(main())
