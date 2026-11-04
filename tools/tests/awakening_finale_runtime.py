#!/usr/bin/env python3
"""Input-free M08/M09 script, boss-phase and fresh-process restore probes.

Uses installed content, a hidden window and engine screenshots. These focused
encounters do not substitute for a complete traversal or combat playthrough.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import subprocess
import sys
import time

from campaign_runtime import ROOT, task_path
from awakening_missions_runtime import states, ERROR
from mp_round_remote_smoke import verify_runtime_is_current
from savegame_cross_load import CFG_REL, LOG_NAME, client_path, launch_command, stage_input, stop_process


def commands(case, restoring):
    lines = ['set g_autoExecAfterMapLoad ""', 'set g_stopTime 0',
             'set ai_debugActions valk', 'set g_debugDamage 1']
    if not restoring:
        lines += ['god']
    if case == 'boss':
        if not restoring:
            lines += ['wait 120', 'trigger trigger_once_63', 'wait 2',
                      'echo FINALE_EARLY', 'testMission valk',
                      'damage valk 100000', 'testMission valk',
                      'trigger trigger_once_62', 'wait 1200',
                      'setviewpos -3500 300 -4690 270', 'wait 120',
                      'echo FINALE_DOCKED', 'testMission valk', 'damage valk 100000',
                      'testMission valk', 'screenshot screenshots/docked.tga',
                      'saveGame docked', 'wait 5']
        # idDamagable uses the damage definition's amount, not the console
        # scale. Eight ordinary 125-damage rail hits break the 1000-HP sphere.
        lines += ['testMission mak_sphere'] + ['damage mak_sphere 1 damage_railgun'] * 8
        lines += ['wait 1200', 'echo FINALE_RELEASED', 'listThreads', 'aiDumpState valk -v',
                  'testMission valk', 'damage valk 100', 'testMission valk',
                  'screenshot screenshots/released.tga', 'saveGame released', 'wait 5',
                  'damage valk 100000', 'wait 2', 'echo FINALE_DEAD',
                  'testMission valk', 'wait 1000', 'testMission undervalkdoor', 'testMission player1']
    elif case in ('cooling', 'cooling-early'):
        if not restoring:
            lines += ['wait 120', 'trigger darnell', 'trigger redshirt']
            if case == 'cooling-early':
                lines += ['testMission fog_lifter', 'trigger trigger_relay_110',
                          'testMission call TurnOffFreeze', 'wait 30']
            else:
                lines += ['trigger trigger_relay_110', 'wait 3600',
                          'echo FINALE_FROZEN', 'testMission fog_lifter',
                          'testMission call TurnOffFreeze', 'wait 30']
            lines += ['saveGame cooling', 'wait 5']
        lines += ['testMission call TurnOffFreeze', 'testMission call TurnOffFreeze',
                  'wait 4800', 'echo FINALE_THAWED', 'testMission fog_lifter',
                  'testMission call TurnOffFreeze', 'wait 900',
                  'echo FINALE_REPEAT', 'testMission fog_lifter',
                  'echo FINALE_COOLING_EXIT', 'listSpawnArgs func_door_370', 'echo FINALE_COOLING_EXIT_END',
                  'screenshot screenshots/cooled.tga', 'saveGame cooled', 'wait 5']
    elif case == 'tak':
        if not restoring:
            lines += ['wait 120', 'trigger trigger_relay_113', 'wait 1',
                      'saveGame tak_scene', 'wait 5']
        for i in range(60):
            lines += ['wait 30', 'testMission valkaryne_1', 'testMission tak']
        lines += ['testMission navajas', 'listThreads']
    elif case == 'lights':
        lights = ('vt_light_1', 'vt_light_31', 'vt_light_63', 'vt2_light_14', 'vt3_light_10')
        lines += ['wait 120'] + [f'testMission {name}' for name in lights]
        lines += ['testMission call oq4_m09_v1::valktauntOne',
                  'testMission call oq4_m09_v1::valktauntTwo',
                  'testMission call oq4_m09_v1::valktauntThree', 'wait 120']
        lines += [f'testMission {name}' for name in lights]
        lines += ['wait 780'] + [f'testMission {name}' for name in lights]
        lines += ['listThreads', 'saveGame lights', 'wait 5']
    elif case == 'navajas':
        lines += ['wait 120', 'setviewpos -2520 -112 160 270', 'trigger navajas02', 'wait 5',
                  'testMission call oq4_m09_v1::navajasDies', 'wait 60',
                  'testMission navajas03', 'saveGame navajas_scene', 'wait 5',
                  'wait 1800', 'testMission navajas03', 'testMission valkaryne02', 'testMission valk_attacker01',
                  'listThreads']
    elif case == 'intro':
        lines += ['wait 600', 'listThreads', 'testMission player1',
                  'screenshot screenshots/intro.tga', 'saveGame intro', 'wait 5']
    elif case == 'thaw-door':
        lines += ['wait 120', 'trigger trigger_relay_109', 'wait 6000',
                  'echo FINALE_THAW_DOOR', 'listSpawnArgs cryodoor_6',
                  'echo FINALE_THAW_DOOR_END', 'listThreads', 'saveGame thaw_door', 'wait 5']
    elif case == 'missed-extraction':
        if not restoring:
            lines += ['wait 120', 'trigger item_objective_2',
                      'testMission call oq4_m09_v1::moveDropship', 'wait 900',
                      'testMission call oq4_m09_v1::operate_exit_4door',
                      'wait 1500', 'saveGame departure', 'wait 5']
        lines += ['wait 900', 'testMission call oq4_m09_v1::youAreTehWinnar',
                  # Check after failure at ~46 seconds, before its automatic
                  # return to the menu makes the save diagnostic ambiguous.
                  'wait 540', 'saveGame forbidden', 'wait 5',
                  'screenshot screenshots/missed-extraction.tga', 'listThreads']
    return lines + ['echo FINALE_PROBE_DONE', 'quit']


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--case', choices=('boss', 'cooling', 'cooling-early', 'thaw-door', 'tak', 'lights', 'navajas', 'intro', 'missed-extraction'), required=True)
    p.add_argument('--map', choices=('m08_cryofac', 'm09_valkaryne'))
    p.add_argument('--retail', type=Path, required=True)
    p.add_argument('--awakening', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--cache', type=Path, required=True)
    p.add_argument('--input', type=Path)
    p.add_argument('--legacy', action='store_true', help='Start with the original script program for compatibility checks')
    p.add_argument('--renderer', choices=('gl', 'vulkan'), default='gl')
    p.add_argument('--timeout', type=int, default=300)
    args = p.parse_args()
    if args.legacy and args.case != 'intro':
        p.error('--legacy is supported by the intro/save compatibility probe')
    if args.input and args.case in ('navajas', 'thaw-door'):
        p.error('--input is not supported by this fresh sequence probe')
    exe = client_path(ROOT/'.install')
    verify_runtime_is_current(ROOT/'.install', exe.name)
    output, cache = task_path(args.output), task_path(args.cache)
    output.mkdir(parents=True, exist_ok=False)
    game = output/'q4xbase'
    game.mkdir()
    if args.input:
        stage_input(args.input.resolve(), game/'savegames', 'finale_in')
    cfg = game/CFG_REL
    cfg.parent.mkdir(parents=True, exist_ok=True)
    cfg.write_text('\n'.join(commands(args.case, bool(args.input)))+'\n', encoding='ascii')
    map_name = 'game/'+(args.map or ('m08_cryofac' if args.case in ('cooling', 'cooling-early', 'thaw-door', 'tak') else 'm09_valkaryne'))
    options = argparse.Namespace(mode='load' if args.input else 'write', basepath=args.retail.resolve(),
        game='q4xbase', tag='finale', map=map_name, xvfb=False,
        set=[('fs_awakeningpath', str(args.awakening.resolve())), ('fs_devpath', str(output)),
             ('fs_cachepath', str(cache)), ('ui_retained', '1'), ('com_gameMode', 'SP'),
             ('ui_autoJoin', '0'), ('r_renderApi', args.renderer), ('sys_consoleWindow', '0'),
             ('r_fullscreenDesktop', '0'), ('r_windowWidth', '960'), ('r_windowHeight', '540'),
             ('g_autoSkipCinematics', '0')])
    if args.legacy:
        options.set.append(('g_awakeningLateMissions', '0'))
    command = launch_command(options, exe, output)
    # Keep a complete staged binary identity so another build cannot silently
    # replace the runtime in the middle of a probe.
    module_root = exe.parent.parent/'Frameworks' if sys.platform == 'darwin' else exe.parent/'baseoq4'
    arch = 'arm64' if platform.machine().lower() in ('arm64', 'aarch64') else 'x64'
    suffix = '.dll' if os.name == 'nt' else '.dylib' if sys.platform == 'darwin' else '.so'
    module = module_root/f'game-sp_{arch}{suffix}'
    binaries = [exe, module,
                *sorted(exe.parent.glob('renderer-*'))]
    identity = lambda: {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                        for p in binaries if p.suffix not in ('.pdb', '.lib')}
    before = identity()
    report = dict(case=args.case, map=map_name, command=command, identity=before)
    (output/'plan.json').write_text(json.dumps(report, indent=2)+'\n')
    startup = None
    if os.name == 'nt':
        startup = subprocess.STARTUPINFO()
        startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        startup.wShowWindow = subprocess.SW_HIDE
    failures = []
    start = time.monotonic()
    path = game/'logs'/LOG_NAME
    with (output/'stdout.log').open('w') as stdout:
        proc = subprocess.Popen(command, cwd=exe.parent, stdin=subprocess.DEVNULL,
            stdout=stdout, stderr=subprocess.STDOUT, startupinfo=startup,
            env={**os.environ, 'TMP': str(output), 'TEMP': str(output), 'TMPDIR': str(output)},
            start_new_session=os.name != 'nt')
        while proc.poll() is None:
            log = path.read_text(errors='replace') if path.exists() else ''
            if time.monotonic()-start > args.timeout or ERROR.search(log):
                failures.append('timeout or runtime error')
                stop_process(proc)
                break
            time.sleep(.5)
    log = path.read_text(errors='replace') if path.exists() else ''
    if ERROR.search(log) or log.count('FINALE_PROBE_DONE') != 1 or proc.returncode != 0:
        failures.append('runtime error, nonzero exit or missing completion')
    if before != identity():
        failures.append('runtime changed during probe')
    if args.input and 'Game Map Init SaveGame' not in log:
        failures.append('save did not restore')
    if not args.input and not args.legacy and f'Awakening {map_name[5:8].upper()}: mission corrections v1 enabled' not in log:
        failures.append('corrected map program was not selected')
    if 'expected an existing zero-argument script function' in log:
        failures.append('probe callback missing')
    script_errors = [line for line in log.splitlines()
                     if ('is a null entity' in line or 'Terminating thread.' in line
                         or 'not supported on entity' in line)
                     and "::coldInHere'" not in line]
    if script_errors:
        failures.append('script thread failed: ' + '\n'.join(script_errors))
    if args.case == 'boss':
        rows = states(log, 'valk')
        report['boss_states'] = rows
        expected = 3 if args.input else 7
        if len(rows) != expected:
            failures.append('missing boss state samples')
        elif (any(r['health'] <= 0 for r in rows[:-1]) or rows[-1]['health'] > 0
              or rows[-3]['health'] <= rows[-2]['health']
              or (not args.input and len({r['health'] for r in rows[:4]}) != 1)):
            failures.append('intro/docked immunity, release damage or victory failed')
        if 'oq4_m09_v1::valkDead' not in log and 'Escape' not in log:
            failures.append('escape objective absent after victory')
        if not args.input:
            protected = log.split('FINALE_DOCKED', 1)[-1].split('testMission: name=mak_sphere', 1)[0]
            if 'valk: action Torso_Pain' in protected:
                failures.append('invulnerable damage interrupted the docked attack')
    if args.case in ('cooling', 'cooling-early'):
        rows = states(log, 'fog_lifter')
        report['fog_states'] = rows
        if len(rows) != (2 if args.input else 3) or rows[-1]['origin'] != rows[-2]['origin']:
            failures.append('repeated shutdown moved the fog again')
        elif not args.input:
            z = lambda row: float(row['origin'].split()[2])
            delta = 0 if args.case == 'cooling-early' else 192
            if abs(z(rows[0])-z(rows[1])-delta) > .1:
                failures.append('shutdown did not lower fog by the authored distance')
        door = log.split('FINALE_COOLING_EXIT \n')[-1].split('FINALE_COOLING_EXIT_END')[0]
        if not re.search(r'"locked"\s+(?:\^\d)?"0"', door):
            failures.append('cooling sequence did not unlock its exit')
    if args.case == 'lights':
        for name in ('vt_light_1', 'vt_light_31', 'vt_light_63', 'vt2_light_14', 'vt3_light_10'):
            colors = re.findall(rf'testMissionLight: name={name} radius=\([^)]+\) color=\(([^)]+)\)', log)
            if len(colors) != 3 or colors[0] != colors[2] or any(float(v) != 0 for v in colors[1].split()):
                failures.append(f'{name} did not extinguish and restore')
    if args.case == 'tak':
        samples = [tuple(map(int, row)) for row in re.findall(
            r'testMissionAnim: name=valkaryne_1 anim=\S* torso=cr_kill_tak time=(\d+) start=(-?\d+) end=(-?\d+)', log)]
        # Ignore the map's static initial pose; require a finite playing clip
        # still present near its end, then the authored cleanup.
        report['tak_animation_samples'] = samples
        if (not any(end-start >= 8000 and now-start >= 7500 and now < end
                    for now, start, end in samples)
                or 'testMission: missing=valkaryne_1' not in log):
            failures.append('Tak death animation did not play through before cleanup')
    if args.case == 'navajas':
        victim, attacker = states(log, 'navajas03'), states(log, 'valk_attacker01')
        if (not victim or (victim[-1]['health'] > 0 and 'testMission: missing=navajas03' not in log)
                or not attacker or attacker[-1]['hidden'] or 'testMission: missing=valkaryne02' not in log):
            failures.append('Navajas death did not hand off to the descending attacker')
    if args.case == 'missed-extraction':
        if "Can't save after failed mission" not in log or (game/'savegames/forbidden.save').exists():
            failures.append('missed extraction did not fail the mission and block saves')
        if not args.input and not (game/'savegames/departure.save').exists():
            failures.append('extraction failed before the promised thirty-second deadline')
    if args.case == 'thaw-door':
        door = log.split('FINALE_THAW_DOOR \n')[-1].split('FINALE_THAW_DOOR_END')[0]
        if not re.search(r'"locked"\s+(?:\^\d)?"0"', door) or "Entity not found for event 'scriptedMove'" in log:
            failures.append('first thaw did not reach and unlock cryodoor_6')
    if args.case == 'intro':
        saved = game/'savegames/intro.save'
        revision = (map_name.split('/')[-1]+'.openq4-v1.script').encode('ascii')
        expected = (revision in args.input.read_bytes()) if args.input else not args.legacy
        if not saved.is_file() or (revision in saved.read_bytes()) != expected:
            failures.append('intro save did not retain the selected script revision')
        report['script_revision'] = 'v1' if expected else 'original'
    report.update(passed=not failures, failures=failures, seconds=round(time.monotonic()-start, 1),
                  warnings=[line for line in log.splitlines() if 'WARNING:' in line])
    (output/'report.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps({k: report[k] for k in ('case', 'map', 'passed', 'failures', 'seconds')}))
    return int(bool(failures))


if __name__ == '__main__':
    raise SystemExit(main())
