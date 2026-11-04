#!/usr/bin/env python3
"""M04 console repeat and camera/ambush sequencing, without device input.

The camera controls use real map triggers, normal damage and the authored death
counter. Source maps/scripts stay untouched; save legs use no console scripts.
"""
from __future__ import annotations
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import time

from campaign_runtime import ROOT, task_path, sha256
from savegame_cross_load import CFG_REL, LOG_NAME, launch_command, stage_input, stop_process


def inspect_camera(label):
    return [f'echo PRISON_{label}', 'testMission prison_camera_8', 'testMission monster_grunt_2',
            'testMission monster_gunner_1', 'listThreads', 'echo PRISON_SAMPLE_END']


def commands(args):
    lines = ['set g_autoExecAfterMapLoad ""', 'set g_stopTime 0', 'set r_skipRender 1']
    if not args.source:
        lines += ['god', 'notarget', 'wait 60', 'noclip']
    if args.case == 'console':
        lines += ['setviewpos 1416 -3104 528 0 0 0']
        if not args.source:
            lines += ['trigger security_alarm_1', 'wait 5', 'trigger trigger_relay_15', 'wait 90']
        for label in ('FIRST', 'REPEAT'):
            if label == 'REPEAT':
                lines += ['trigger trigger_relay_15', 'wait 90']
            lines += [f'echo PRISON_{label}', 'testMission sec_windowShield_01',
                      'testMission sec_windowShield_02', 'listSpawnArgs func_static_54011',
                      's_soundDistances sound/ambience/alarms_buzzers/1shot_alarm01_q4xdm7',
                      'echo PRISON_SAMPLE_END']
        lines += ['set g_stopTime 1', 'saveGame prison', 'wait 3']
    else:
        # Stay by the preceding camera, clear of trigger_once_155's volume.
        lines += ['setviewpos -480 -1590 550 0 180 0']
        if not args.source:
            lines += ['trigger prison_camera_8', 'wait 30', 'echo PRISON_CAMERA_AWAKE',
                      'testMission prison_camera_8', 'aiDumpState prison_camera_8 -v', 'echo PRISON_SAMPLE_END']
            if args.case == 'camera-early':
                lines += ['damage prison_camera_8 10000', 'wait 10']
            lines += [('testMission call gruntAndGunnerAttack' if args.baseline else 'trigger trigger_once_155'), 'wait 10']
            if args.case == 'camera-late':
                lines += ['damage prison_camera_8 10000', 'wait 5']
            if args.save_pending:
                return lines + inspect_camera('PENDING') + ['set g_stopTime 1', 'saveGame prison', 'wait 3']
        # The bypass's initial four-second wait identifies the relay-owned hack
        # before its thread enters shared AI movement helpers.
        lines += [('wait 30' if args.case == 'camera-early' else 'wait 240'), *inspect_camera('BEFORE_DEATHS'),
                  'set r_skipRender 0', 'wait 1', 'screenshot screenshots/camera.tga', 'set r_skipRender 1']
        if args.case != 'camera-early':
            lines += ['damage monster_grunt_2 10000', 'damage monster_gunner_1 10000',
                      'wait 5', *inspect_camera('AFTER_DEATHS')]
    return lines


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--retail', type=Path, required=True)
    parser.add_argument('--awakening-root', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--case', choices=('console', 'camera-early', 'camera-late', 'camera-alive'), required=True)
    parser.add_argument('--source', type=Path)
    parser.add_argument('--save-pending', action='store_true')
    parser.add_argument('--baseline', action='store_true', help='Expect the demonstrated original defect')
    parser.add_argument('--renderer', choices=('gl', 'vulkan'), default='gl')
    parser.add_argument('--timeout', type=float, default=300)
    args = parser.parse_args()
    runtime = ROOT/'.install'
    exe = runtime/'openQ4-client_x64.exe'
    identity = {name: sha256(runtime/name) for name in
                ('openQ4-client_x64.exe', 'baseoq4/game-sp_x64.dll', 'baseoq4/pak0.pk4')}
    if not args.baseline:
        for name in ('openQ4-client_x64.exe', 'baseoq4/game-sp_x64.dll'):
            assert identity[name] == sha256(ROOT/'builddir'/name), 'Stage the current build first'
    home = task_path(args.output)
    game = home/'q4xbase'
    game.mkdir(parents=True, exist_ok=False)
    if args.source:
        stage_input(args.source.resolve(strict=True), game/'savegames', 'prison_in')
    cfg = game/CFG_REL
    cfg.parent.mkdir(parents=True)
    cfg.write_text('\n'.join(commands(args) + ['echo PRISON_DONE', 'quit'])+'\n', encoding='ascii')
    settings = [('fs_awakeningpath', str(args.awakening_root.resolve(strict=True))),
                ('fs_devpath', str(home)), ('fs_cachepath', str(task_path(ROOT/'.tmp/awakening-m03-m04/gl/cache'))),
                ('com_gameMode', 'SP'), ('ui_autoJoin', '0'), ('ui_retained', '1'),
                ('r_renderApi', args.renderer), ('sys_consoleWindow', '0'),
                ('r_fullscreenDesktop', '0'), ('com_fixedTic', '4'), ('g_autoSkipCinematics', '0')]
    if args.case == 'console':
        settings += [('s_noSound', '0'), ('s_muteUnfocused', '0')]
    options = argparse.Namespace(mode='load' if args.source else 'write', basepath=args.retail.resolve(strict=True),
        game='q4xbase', tag='prison', map='game/m04_prison', xvfb=False, set=settings)
    command = launch_command(options, exe, home)
    (home/'plan.json').write_text(json.dumps(dict(command=command, identity=identity, input_control=False,
        source=None if not args.source else dict(path=str(args.source), sha256=sha256(args.source))), indent=2))
    startup = None
    if os.name == 'nt':
        startup = subprocess.STARTUPINFO()
        startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        startup.wShowWindow = subprocess.SW_HIDE
    logpath = game/'logs'/LOG_NAME
    started = time.monotonic()
    with (home/'stdout.log').open('w') as output:
        process = subprocess.Popen(command, cwd=runtime, stdin=subprocess.DEVNULL, stdout=output,
            stderr=subprocess.STDOUT, startupinfo=startup, env={**os.environ, 'ALSOFT_DRIVERS': 'null'})
        while process.poll() is None and time.monotonic()-started < args.timeout:
            time.sleep(.5)
            log = logpath.read_text(errors='replace') if logpath.exists() else ''
            if re.search(r'ERROR:|Compile failed|script runtime error|Assertion failed', log):
                break
        stop_process(process)
    log = re.sub(r'\^[0-9]', '', logpath.read_text(errors='replace') if logpath.exists() else '')
    failures = re.findall(r'(?mi)^.*(?:ERROR:|Compile failed|script runtime error|Assertion failed|Unknown command|^entity not found).*$', log)
    if process.returncode != 0 or 'PRISON_DONE' not in log:
        failures.append(f'incomplete run / exit {process.returncode}')
    samples = dict(re.findall(r'(?m)^PRISON_(\w+)\s*\n(.*?)^PRISON_SAMPLE_END', log, re.S))
    if args.case != 'console' and not args.source:
        awake = samples.get('CAMERA_AWAKE', '')
        if not re.search(r'name=prison_camera_8 health=100 hidden=0', awake) or 'idAI.aifl.awake = true' not in awake:
            failures.append('camera failed to wake')
        if 'name=prison_camera_8 anim=scan' not in awake:
            failures.append('camera failed to scan')
    if args.case == 'console':
        positions = [re.findall(r'testMission: name=sec_windowShield_0[12].*origin=\(([^)]+)\)',
                               samples.get(label, '')) for label in ('FIRST', 'REPEAT')]
        moved_again = positions[0] != positions[1]
        if len(positions[0]) != 2 or moved_again != args.baseline:
            failures.append('unexpected shutter movement on repeated console activation')
        if not args.baseline and '"gui_noninteractive"  "1"' not in samples.get('REPEAT', ''):
            failures.append('completed console still interactive')
        if 'SOUND_DISTANCE_END channels=0' not in samples.get('FIRST', ''):
            failures.append('first console use did not silence the alarm')
        if not args.baseline and 'SOUND_DISTANCE_END channels=0' not in samples.get('REPEAT', ''):
            failures.append('repeated console use restarted the alarm')
        if args.baseline and 'playing=1' not in samples.get('REPEAT', ''):
            failures.append('baseline did not reproduce the alarm restart')
    elif args.save_pending:
        if "Saved 'prison'" not in log or 'PENDING' not in samples:
            failures.append('pending encounter did not save')
        if 'testMission: missing=prison_camera_8' not in samples.get('PENDING', ''):
            failures.append('camera was not destroyed before saving the pending ambush')
        pending = samples.get('PENDING', '')
        if not re.search(r'm04_prison_v3.script\(\d+\)', pending) or any(not re.search(
                rf'name={name} health=[1-9]\d* hidden=1', pending)
                for name in ('monster_grunt_2', 'monster_gunner_1')):
            failures.append('checkpoint was not inside the ambush startup delay')
    else:
        before = samples.get('BEFORE_DEATHS', '')
        if args.case != 'camera-alive' and 'testMission: missing=prison_camera_8' not in before:
            failures.append('camera was not destroyed')
        if args.case == 'camera-alive' and not re.search(r'name=prison_camera_8 health=100 hidden=0', before):
            failures.append('live camera did not remain active')
        def hacks(block):
            return sum('squadTopLoop7Pos' in line or bool(re.search(
                r': maps[\\/]game[\\/]m04_prison.script\(13(?:[56]\d|7\d|8[0-5])\)', line))
                for line in block.splitlines() if re.match(r'^\s*\d+: ', line))
        hack_before = hacks(before)
        if args.case == 'camera-early':
            if hack_before != 1 or not re.search(r'name=monster_gunner_1 health=[1-9]\d* hidden=1', before):
                failures.append('destroying the camera early did not bypass the ambush')
        else:
            if hack_before != (1 if args.baseline else 0):
                failures.append('door hack starts before the ambush death counter')
            if not re.search(r'name=monster_gunner_1 health=[1-9]\d* hidden=0', before):
                failures.append('ambush gunner never activated')
            after = samples.get('AFTER_DEATHS', '')
            expected = 2 if args.baseline else 1
            if hacks(after) != expected:
                failures.append('ambush deaths did not start exactly one hack')
    if any(sha256(runtime/name) != value for name, value in identity.items()):
        failures.append('runtime changed during probe')
    result = dict(passed=not failures, failures=failures, case=args.case, baseline=args.baseline,
                  seconds=round(time.monotonic()-started, 2), identity=identity, samples=samples)
    (home/'result.json').write_text(json.dumps(result, indent=2))
    print(json.dumps({key: result[key] for key in ('passed', 'failures', 'case', 'baseline', 'seconds')}))
    return 0 if result['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
