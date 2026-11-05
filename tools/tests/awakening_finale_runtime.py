#!/usr/bin/env python3
"""Focused M08/M09 script, boss-phase and fresh-process restore probes.

Uses installed content, a hidden window and engine screenshots. These focused
encounters do not substitute for a complete traversal or combat playthrough.
The stairs-clearance case uses normal player input and requires authorization.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
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


def commands(case, restoring, revision='v2', breath=(), callbacks=()):
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
                      'testMission valk', 'screenshot \"screenshots/docked.tga\"',
                      'saveGame docked', 'wait 5']
        # idDamagable uses the damage definition's amount, not the console
        # scale. Eight ordinary 125-damage rail hits break the 1000-HP sphere.
        lines += ['testMission mak_sphere'] + ['damage mak_sphere 1 damage_railgun'] * 8
        lines += ['wait 1200', 'echo FINALE_RELEASED', 'listThreads', 'aiDumpState valk -v',
                  'testMission valk', 'damage valk 100', 'testMission valk',
                  'screenshot \"screenshots/released.tga\"', 'saveGame released', 'wait 5',
                  'damage valk 100000', 'wait 2', 'echo FINALE_DEAD',
                  'testMission valk', 'wait 1000', 'testMission undervalkdoor', 'testMission player1']
    elif case == 'docked-perches':
        if not restoring:
            lines += ['wait 120', 'setviewpos -2520 -112 160 270', 'trigger navajas02', 'wait 5',
                      'testMission call oq4_m09_v1::navajasDies', 'wait 3600']
            for callback, height in (('move01ValkAttacker01', -64), ('move02ValkAttacker01', -128),
                                     ('move04ValkAttacker01', -1264), ('move03ValkAttacker01', -576)):
                lines += [f'setviewpos -2688 0 {height} 0', f'testMission call oq4_m09_v1::{callback}',
                          'wait 240', 'testMission valk_attacker01']
            lines += ['saveGame perched', 'wait 5']
        else:
            lines += ['wait 240', 'testMission valk_attacker01']
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
                  'screenshot \"screenshots/cooled.tga\"', 'saveGame cooled', 'wait 5']
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
    elif case == 'escort-objective':
        if not restoring:
            lines += ['wait 180', 'echo FINALE_ESCORT_BEFORE', 'testMission player1',
                      'setviewpos -2520 -112 160 270', 'trigger navajas02', 'wait 5',
                      'testMission call oq4_m09_v1::navajasDies', 'wait 3600']
        lines += ['testMission call oq4_m09_v1::openq4_completeEscort',
                  'testMission call oq4_m09_v1::openq4_completeEscort', 'wait 15',
                  'echo FINALE_ESCORT_DONE', 'testMission player1', 'saveGame escort_done', 'wait 5']
    elif case == 'maiden-trigger':
        if not restoring:
            lines += ['wait 120', 'setviewpos -2688 -200 -1392 270',
                      'testMission monster_iron_maiden_5', 'trigger trigger_once_15', 'wait 240',
                      'testMission monster_iron_maiden_5', 'testMission env_wall_casket_5',
                      'saveGame maiden_active', 'wait 5']
        else:
            lines += ['wait 120', 'testMission monster_iron_maiden_5', 'testMission env_wall_casket_5']
    elif case == 'stairs-clearance':
        if not restoring:
            lines += ['wait 120', 'setviewpos -1104 1808 -1340 0', 'trigger trigger_once_97',
                      'wait 180', 'echo FINALE_STAIRS_WAITING', 'testMission stairway01',
                      'saveGame stairs_waiting', 'wait 5']
        lines += ['openq4_testInput 3200 forward 127 run', 'wait 205', 'openq4_testInput 0',
                  'wait 180', 'echo FINALE_STAIRS_CLEARED', 'testMission player1',
                  'testMission stairway01', 'testMission trigger_once_25', 'screenshot "screenshots/stairs-cleared.tga"']
    elif case == 'callbacks':
        lines += ['wait 180']
        for entity, key, original in callbacks:
            lines += [f'echo FINALE_CALLBACK_{entity}', f'testMission {entity}', f'listSpawnArgs {entity}', f'echo FINALE_CALLBACK_END_{entity}']
    elif case == 'cold-breath':
        lines += ['wait 180']
        for effect, actor in breath:
            lines += [f'testMission {actor}', f'testMission {effect}']
        lines += ['saveGame cold_breath', 'wait 5']
    elif case == 'entrance-clearance':
        lines += ['wait 3600', 'trigger trigger_relay_40', 'wait 1500',
                  'testMission tak', 'testMission navajas', 'testMission door_ice_damage_1']
    elif case == 'core-monitor':
        # Isolated scene setup, explicitly excluded from traversal evidence.
        # Console script additions make this unsuitable for save/restore tests.
        lines += ['wait 6000',
                  'script "$tak.setOrigin($tak_pos_28.getOrigin()); $navajas.setOrigin($tak_pos_28.getOrigin() + \'0 -64 0\')"',
                  'wait 5', 'setviewpos 112 -1100 -572 90', 'testMission call CoreMonitor', 'wait 2400',
                  'testMission navajas', 'echo FINALE_CORE_DOOR', 'listSpawnArgs cryodoor_5',
                  'echo FINALE_CORE_DOOR_END']
    elif case == 'freezer-sequence':
        # Deliberately overlap the two scene requests. Actor relocation is
        # isolated setup; no save is qualified after console script injection.
        lines += ['wait 120', 'setviewpos -1100 -1100 -710 0', 'wait 5',
                  'testMission stopThread Intro', 'testMission stopThread FirstFreeze',
                  'script "$tak.scriptedStop(); $navajas.scriptedStop(); $tak.setOrigin($tak_firstfreeze_pos_1.getOrigin()); $navajas.setOrigin($navajas_firstfreeze_pos_1.getOrigin())"',
                  'wait 5', 'testMission call FirstFreeze', 'testMission call StartFreezerMaidens',
                  'wait 120', 'echo FINALE_FREEZER_QUEUED', 'testMission darnell', 'testMission redshirt',
                  'script "sys.println(openq4_firstThawComplete)"', 'wait 5',
                  'wait 9000', 'echo FINALE_FREEZER_STAGED',
                  'script "sys.println(openq4_firstThawComplete)"', 'wait 5',
                  'testMission tak', 'testMission navajas', 'listSpawnArgs cryodoor_6',
                  'testMission darnell', 'testMission redshirt',
                  'setviewpos -1100 -540 -710 180', 'wait 5',
                  'screenshot "screenshots/freezer-staged.tga"']
    elif case == 'cryo-media':
        lines += ['wait 120', 'setviewpos -1040 -1160 -956 270', 'wait 10',
                  'screenshot "screenshots/cryo-props.tga"']
        for name in ('func_static_54642', 'func_static_55793', 'func_static_55794',
                     'func_static_55795', 'func_static_55797', 'func_static_55799', 'func_static_55800'):
            lines += [f'testMission {name}', f'listSpawnArgs {name}']
    elif case == 'freezer-navigation':
        lines += ['god', 'setviewpos -1100 -1100 -710 0', 'wait 5', 'testMission tak', 'testMission tak_pos_32']
        for goal in ('cryodoor_6_tak_pos_1', 'cryodoor_6_tak_pos_2', 'tak_pos_31', 'tak_pos_32'):
            lines += [f'echo BEFORE_{goal}', f'testMission path tak {goal}']
        lines += ['script "$cryodoor_6.lock(0); $cryodoor_6.open()"', 'wait 120']
        for goal in ('cryodoor_6_tak_pos_1', 'cryodoor_6_tak_pos_2', 'tak_pos_32'):
            lines += [f'echo OPEN_{goal}', f'testMission path tak {goal}']
        lines += ['script "$tak.scriptedMove($tak_pos_32, 8, 0)"', 'wait 1500', 'testMission tak',
                  'testMission path tak tak_pos_32', 'screenshot "screenshots/freezer-navigation.tga"']
    elif case == 'navigation':
        lines += ['wait 120', 'setviewpos 112 -1100 -572 90']
        for actor in ('tak', 'navajas'):
            lines += [f'testMission {actor}']
            for goal in ('tak_pos_28', 'navajas_pos_28', 'cryodoor_5_tak_pos_1', 'cryodoor_5_tak_pos_2'):
                lines += [f'testMission {goal}', f'testMission path {actor} {goal}']
        lines += ['testMission call CoreMonitor', 'wait 2400', 'testMission tak', 'testMission navajas', 'listSpawnArgs cryodoor_5',
                  'screenshot "screenshots/navigation.tga"']
    elif case == 'blast-doors':
        panels = ('exitdooronepanel', 'exitdoortwopanel', 'exitdoorthreepanel')
        doors = ('func_door_144', 'func_door_143', 'func_door_145', 'func_door_146', 'func_door_147', 'func_door_148')
        lines += ['wait 120']
        if not restoring:
            lines += ['testMission call oq4_m09_v1::valkDead', 'wait 120']
        for index, panel in enumerate(panels):
            x, y, yaw = ((-3520, -2032, 0), (-4048, -2400, 270), (-4416, -2928, 0))[index]
            lines += [f'setviewpos {x} {y} -4972 {yaw}', 'wait 5', f'testMission {panel}',
                      f'screenshot "screenshots/panel{index+1}.tga"',
                      f'testMission activateGui {panel}', 'wait 20']
            if index == 0 and not restoring:
                lines += ['saveGame sealing', 'wait 5']
            lines += ['wait 130', f'testMission {panel}',
                      f'testMission activateGui {panel}', f'trigger openq4_{panel}_relay', 'wait 5',
                      f'listSpawnArgs {panel}', f'testMission {panel}']
        lines += [f'testMission {door}' for door in doors]
        lines += ['saveGame sealed', 'wait 5',
                  'echo FINALE_SEAL_FLAGS', 'script "if (oq4_m09_v1::EXIT_DOOR_ONE_CLOSED) {sys.println(1);} if (oq4_m09_v1::EXIT_DOOR_TWO_CLOSED) {sys.println(1);} if (oq4_m09_v1::EXIT_DOOR_THREE_CLOSED) {sys.println(1);}"', 'wait 5']
        for hurt in ('makronroom_hurt', 'exit_tun_hurt_1', 'exit_tun_hurt_2', 'exit_tun_hurt_3'):
            lines += [f'testMission {hurt}']
    elif case == 'intro':
        lines += ['wait 600', 'listThreads', 'testMission player1',
                  'screenshot \"screenshots/intro.tga\"', 'saveGame intro', 'wait 5']
    elif case == 'thaw-door':
        lines += ['wait 120', 'trigger trigger_relay_109', 'wait 6000',
                  'echo FINALE_THAW_DOOR', 'listSpawnArgs cryodoor_6',
                  'echo FINALE_THAW_DOOR_END', 'listThreads', 'saveGame thaw_door', 'wait 5']
    elif case == 'successful-extraction':
        if not restoring:
            lines += ['set g_nightmare 0', 'wait 120', 'trigger item_objective_2',
                      'setviewpos -4416 -4032 1516 90',
                      'testMission call oq4_m09_v1::moveDropship', 'wait 900',
                      'testMission call oq4_m09_v1::operate_exit_4door', 'wait 60',
                      'testMission call oq4_m09_v1::youAreTehWinnar', 'wait 120',
                      'saveGame boarded', 'wait 5']
        lines += ['testMission player1', 'screenshot \"screenshots/boarded.tga\"',
                  'wait 2400', 'testMission player1', 'testMission npc_dekard',
                  'screenshot \"screenshots/patton.tga\"', 'wait 6000',
                  'echo FINALE_END_STATE', 'g_nightmare', 'listThreads']
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
                  'screenshot \"screenshots/missed-extraction.tga\"', 'listThreads']
    return [line.replace('oq4_m09_v1::', f'oq4_m09_{revision}::') for line in lines] + ['echo FINALE_PROBE_DONE', 'quit']


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--case', choices=('freezer-sequence', 'cryo-media', 'freezer-navigation', 'navigation', 'blast-doors', 'boss', 'docked-perches', 'escort-objective', 'maiden-trigger', 'stairs-clearance', 'cooling', 'cooling-early', 'thaw-door', 'tak', 'lights', 'navajas', 'intro', 'missed-extraction', 'cold-breath', 'entrance-clearance', 'core-monitor', 'callbacks', 'successful-extraction'), required=True)
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
    if args.case in ('navigation', 'freezer-navigation') and not args.input:
        p.error('navigation probes require an appropriate route checkpoint')
    if args.legacy and args.case not in ('intro', 'callbacks'):
        p.error('--legacy is supported by intro and callback probes')
    if args.input and args.case in ('navajas', 'thaw-door', 'core-monitor', 'entrance-clearance', 'callbacks', 'freezer-sequence'):
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
    map_name = 'game/'+(args.map or ('m08_cryofac' if args.case in ('cooling', 'cooling-early', 'thaw-door', 'tak', 'core-monitor', 'entrance-clearance', 'cryo-media', 'freezer-sequence', 'navigation', 'freezer-navigation') else 'm09_valkaryne'))
    revision = 'v3' if map_name.endswith('m08_cryofac') else 'v4'
    if args.input:
        payload = args.input.read_bytes()
        revision = next((v for v in ('v4', 'v3', 'v2') if (map_name.split('/')[-1]+f'.openq4-{v}.script').encode('ascii') in payload), 'v1')
    callbacks = []
    if args.case == 'callbacks':
        callbacks = [('trigger_once_46', 'call', 'ValkaryneEscape'),
                     ('trigger_once_64', 'call', 'VO_9_5_1'),
                     ('trigger_relay_76', 'call', 'RemoveDoorIce3'),
                     ('lab1_scientist_1', 'script_death', 'FirstLabCleared'),
                     ('lab1_scientist_2', 'script_death', 'FirstLabCleared')] if map_name.endswith('m08_cryofac') else [
                     ('trigger_once_70', 'call', 'map_m09::spawnTactical02'),
                     ('trigger_once_96', 'call', 'map_m09::spawnTrashCan01')]
    breath = []
    breath_offsets = {}
    if args.case == 'cold-breath':
        installed = (args.awakening/'q4xbase/scripts/maps/m09_valkaryne.script').read_text(encoding='latin-1')
        body = installed.split('void coldInHere()', 1)[1].split('}', 1)[0]
        breath = re.findall(r'\$(cold_breath_\d+)\.bindToJoint\(\s*\$(\w+),', body)
        breath_offsets = {effect: tuple(map(float, offset.split())) for effect, offset in
                          re.findall(r"\$(cold_breath_\d+)\.setOrigin\(\s*'([^']+)'", body)}
        if len(breath) != 36 or set(breath_offsets) != {effect for effect, actor in breath}:
            p.error('unrecognized cold-breath source placements')
    cfg.write_text('\n'.join(commands(args.case, bool(args.input), revision, breath, callbacks))+'\n', encoding='ascii')
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
    report = dict(case=args.case, map=map_name, script_revision='original' if args.legacy else revision,
                  command=command, identity=before)
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
    captures = re.findall(r'screenshot "([^"]+)"', cfg.read_text())
    for capture in captures:
        if not (game/capture).is_file():
            failures.append(f'engine screenshot missing: {capture}')
    if args.input and 'Game Map Init SaveGame' not in log:
        failures.append('save did not restore')
    if args.input and '-------------- Game Map Init ----------------' in log:
        failures.append('restore fell back to a fresh map')
    if not args.input and not args.legacy and f'Awakening {map_name[5:8].upper()}: mission corrections {revision} enabled' not in log:
        failures.append('corrected map program was not selected')
    if 'expected an existing zero-argument script function' in log:
        failures.append('probe callback missing')
    script_errors = [line for line in log.splitlines()
                     if ('is a null entity' in line or 'Terminating thread.' in line
                         or 'not supported on entity' in line)
                     and not ((revision == 'v1' or args.legacy) and "::coldInHere'" in line)]
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
        if f'oq4_m09_{revision}::valkDead' not in log and 'Escape' not in log:
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
    if args.case == 'successful-extraction':
        ending = re.sub(r'\^\d', '', log.split('FINALE_END_STATE \n')[-1])
        if not re.search(r'g_nightmare.*?is:\s*"1"', ending):
            failures.append('successful extraction did not reach the authored end-of-game target')
        if "Can't save after failed mission" in log:
            failures.append('successful extraction also failed the mission')
    if args.case == 'missed-extraction':
        if "Can't save after failed mission" not in log or (game/'savegames/forbidden.save').exists():
            failures.append('missed extraction did not fail the mission and block saves')
        if not args.input and not (game/'savegames/departure.save').exists():
            failures.append('extraction failed before the promised thirty-second deadline')
    if args.case == 'thaw-door':
        door = log.split('FINALE_THAW_DOOR \n')[-1].split('FINALE_THAW_DOOR_END')[0]
        if not re.search(r'"locked"\s+(?:\^\d)?"0"', door) or "Entity not found for event 'scriptedMove'" in log:
            failures.append('first thaw did not reach and unlock cryodoor_6')
    if args.case == 'freezer-sequence':
        queued = re.sub(r'\^\d', '', log.split('FINALE_FREEZER_QUEUED \n')[-1].split('FINALE_FREEZER_STAGED')[0])
        staged = re.sub(r'\^\d', '', log.split('FINALE_FREEZER_STAGED \n')[-1])
        if not re.search(r'^0(?:\.0*)?\s*$', queued, re.M) or not re.search(r'^1(?:\.0*)?\s*$', staged, re.M):
            failures.append('early freezer request did not wait for and resume after the first thaw')
        for actor, origin in (('tak', (-1251, -498, -768)), ('navajas', (-1251, -610, -768))):
            rows = states(log, actor)
            if not rows or math.dist(tuple(map(float, rows[-1]['origin'].split())), origin) > 24:
                failures.append(f'{actor} did not reach the freezer staging point')
        if not re.search(r'"locked"\s+(?:\^\d)?"0"', log):
            failures.append('first thaw did not open cryodoor_6')
        if 'WARNING: ^1scriptedMove:' in log:
            failures.append('sequenced freezer scene still reports a movement failure')
    if args.case == 'navigation':
        expected = {'tak': (-343, -1130, -640), 'navajas': (37, -898, -640)}
        for name, origin in expected.items():
            rows = states(log, name)
            if not rows or math.dist(tuple(map(float, rows[-1]['origin'].split())), origin) > 24:
                failures.append(f'{name} did not finish the console/door route')
        if not re.search(r'"locked"\s+(?:\^\d)?"0"', log):
            failures.append('console route did not unlock cryodoor_5')
        if 'WARNING: ^1scriptedMove:' in log:
            failures.append('console route still reports a scripted movement failure')
    if args.case == 'freezer-navigation':
        rows = states(log, 'tak')
        if not rows or math.dist(tuple(map(float, rows[-1]['origin'].split())), (-1251, -498, -768)) > 24:
            failures.append('Tak did not reach the freezer staging marker')
        if 'WARNING: ^1scriptedMove:' in log:
            failures.append('freezer staging still reports a scripted movement failure')
    if args.case in ('cryo-media', 'blast-doors'):
        if any(phrase in log for phrase in ("Couldn't load image:", "Couldn't load model:")):
            failures.append('late-mission media remains unresolved')
    if args.case == 'blast-doors':
        expected = {'func_door_144': (-3472, -1952, -4944), 'func_door_143': (-3568, -1952, -4944),
                    'func_door_145': (-3968, -2448, -4944), 'func_door_146': (-3968, -2352, -4944),
                    'func_door_147': (-4368, -2848, -4944), 'func_door_148': (-4464, -2848, -4944)}
        for name, origin in expected.items():
            rows = states(log, name)
            if not rows or math.dist(tuple(map(float, rows[-1]['origin'].split())), origin) > 1:
                failures.append(f'{name} did not seal at its authored closed origin')
        for index, name in enumerate(('exitdooronepanel', 'exitdoortwopanel', 'exitdoorthreepanel')):
            activations = log.count(f'testMissionGuiActivated: {name}\n')
            if activations != (0 if args.input and index == 0 else 1):
                failures.append(f'{name} did not accept exactly one GUI activation')
            if not re.search(rf'testMissionGui: name={name} interactive=0 ', log):
                failures.append(f'{name} did not become noninteractive')
        if not (game/'savegames/sealed.save').is_file():
            failures.append('sealed-door save was not written')
        flag_log = re.sub(r'\^\d', '', log.split('FINALE_SEAL_FLAGS \n')[-1])
        if len(re.findall(r'^1(?:\.0*)?\s*$', flag_log, re.M)) != 3:
            failures.append('three physical closures did not award all three authored delay flags')
    if args.case == 'intro':
        saved = game/'savegames/intro.save'
        source_id = (map_name.split('/')[-1]+f'.openq4-{revision}.script').encode('ascii')
        expected = (source_id in args.input.read_bytes()) if args.input else not args.legacy
        if not saved.is_file() or (source_id in saved.read_bytes()) != expected:
            failures.append('intro save did not retain the selected script revision')
        report['script_revision'] = revision if expected else 'original'
    if args.case == 'docked-perches':
        rows = states(log, 'valk_attacker01')
        expected = [(-2328, 0, -704)] if args.input else [(-2240, 168, -192), (-3040, -352, -256), (-2688, 384, -1392), (-2328, 0, -704)]
        report['perches'] = rows
        if len(rows) != len(expected) or any(row['hidden'] or math.dist(tuple(map(float, row['origin'].split())), origin) > 1 for row, origin in zip(rows, expected)):
            failures.append('docked Valkaryne drifted from a scripted perch')
    if args.case == 'escort-objective':
        before_escort = log.split('FINALE_ESCORT_BEFORE')[-1].split('testMission: call=')[0]
        after_escort = log.split('FINALE_ESCORT_DONE')[-1]
        if not args.input and 'testMissionObjective: Protect Navajas' not in before_escort:
            failures.append('escort objective was not initially active')
        if 'testMissionObjective: Protect Navajas' in after_escort or 'testMissionObjective: Confront The Valkaryne' not in after_escort:
            failures.append('escort completion removed the wrong objective or left escort active')
    if args.case == 'maiden-trigger':
        rows = states(log, 'monster_iron_maiden_5')
        report['maiden'] = rows
        if len(rows) != (1 if args.input else 2) or rows[-1]['hidden'] or rows[-1]['health'] <= 0 or (not args.input and not rows[0]['hidden']):
            failures.append('second-shaft Maiden did not activate or survive restoration')
        if not states(log, 'env_wall_casket_5'):
            failures.append('second-shaft Maiden casket is absent')
    if args.case == 'stairs-clearance':
        before_stairs = log.split('FINALE_STAIRS_WAITING')[-1].split('FINALE_STAIRS_CLEARED')[0]
        after_stairs = log.split('FINALE_STAIRS_CLEARED')[-1]
        players = states(after_stairs, 'player1')
        if not args.input and not states(before_stairs, 'stairway01'):
            failures.append('stairs collapsed before the player crossed')
        if not players or float(players[-1]['origin'].split()[0]) <= -768 or float(players[-1]['origin'].split()[2]) < -1281:
            failures.append('normal input did not reach the far stair landing')
        if 'testMission: missing=stairway01' not in after_stairs or 'testMission: missing=trigger_once_25' not in after_stairs:
            failures.append('far-landing trigger did not release the pending collapse')
    if args.case == 'callbacks':
        for entity, key, original in callbacks:
            block = log.split(f'FINALE_CALLBACK_{entity} \n')[-1].split(f'FINALE_CALLBACK_END_{entity}')[0]
            if not states(block, entity):
                failures.append(f'{entity}: callback owner is missing')
            target_key, target = {'trigger_once_46': ('target', 'escape_frost'),
                                  'trigger_once_70': ('target1', 'valk_speaker_7')}.get(entity, ('', ''))
            if target and not re.search(r'"'+target_key+r'"\s+(?:\^\d)?"'+target+r'"', block):
                failures.append(f'{entity}: surviving target chain changed')
            value = re.search(r'"'+re.escape(key)+r'"\s+(?:\^\d)?"([^"]*)"', block)
            expected = original if args.legacy else None
            if (value[1] if value else None) != expected:
                failures.append(f'{entity}: callback differs from selected baseline/corrected behavior')
        report['callbacks'] = dict(legacy=args.legacy, checked=len(callbacks))
    if args.case == 'cold-breath':
        attached, removed = [], []
        for effect, actor in breath:
            if states(log, actor):
                if not re.search(rf'testMission: name={effect} .* bind={actor}\s', log):
                    failures.append(f'{effect} did not attach to {actor}')
                effect_states = states(log, effect)
                if effect_states:
                    actual = math.dist(tuple(map(float, states(log, actor)[-1]['origin'].split())),
                                       tuple(map(float, effect_states[-1]['origin'].split())))
                    if abs(actual-math.sqrt(sum(v*v for v in breath_offsets[effect]))) > 1.0:
                        failures.append(f'{effect} did not retain its authored actor-relative offset')
                attached.append(effect)
            else:
                if f'testMission: missing={effect}' not in log:
                    failures.append(f'orphan {effect} is still present')
                removed.append(effect)
        if len(attached) != 29 or len(removed) != 7:
            failures.append('unexpected cold-breath host population')
        if "joint 'legs_channel' not found" in log:
            failures.append('invalid cold-breath joint still used')
        report['breath'] = dict(attached=attached, removed=removed)
    if args.case == 'entrance-clearance':
        for actor in ('tak', 'navajas'):
            rows = states(log, actor)
            if not rows or float(rows[-1]['origin'].split()[1]) < -5870:
                failures.append(f'{actor} did not clear the entrance throat')
        if 'testMission: missing=door_ice_damage_1' not in log:
            failures.append('entrance ice still present')
    if args.case == 'core-monitor':
        rows = states(log, 'navajas')
        if not rows or sum((a-b)**2 for a,b in zip(map(float, rows[-1]['origin'].split()), (37,-898,-640))) > 24**2:
            failures.append('Navajas did not reach the lower-floor console marker')
        door = log.split('FINALE_CORE_DOOR \n')[-1].split('FINALE_CORE_DOOR_END')[0]
        if not re.search(r'"locked"\s+(?:\^\d)?"0"', door):
            failures.append('core monitor scene did not unlock cryodoor_5')
    report.update(passed=not failures, failures=failures, seconds=round(time.monotonic()-start, 1),
                  warnings=[line for line in log.splitlines() if 'WARNING:' in line])
    (output/'report.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps({k: report[k] for k in ('case', 'map', 'passed', 'failures', 'seconds')}))
    return int(bool(failures))


if __name__ == '__main__':
    raise SystemExit(main())
