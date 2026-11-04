#!/usr/bin/env python3
"""Input-free Awakening M01/M02 encounter and save probes against installed assets.

Uses the staged SP client in a hidden window and engine screenshots. Focused
encounter probes use the map's functions; they are not complete playthroughs.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import platform
import re
import subprocess
import sys
import time

from campaign_runtime import ROOT, task_path
from mp_round_remote_smoke import verify_runtime_is_current
from savegame_cross_load import CFG_REL, LOG_NAME, client_path, launch_command, stage_input, stop_process

FIRE_MAPS = {'m01': 'game/m01_stranarus_trench1', 'm02': 'game/m02_trianfac'}
STATE = re.compile(r'testMission: name=(\S+) health=(-?\d+) hidden=(\d) contents=(\d+) origin=\(([^)]+)\) bind=(\S+)')
ERROR = re.compile(r'(?mi)^(?:\^\d)?(?:ERROR:|FATAL ERROR:)|could not restore saved state|'
                   r'WriteObject FindIndex failed|Unknown command|unknown command')


def states(log, name):
    return [dict(health=int(m[2]), hidden=int(m[3]), contents=int(m[4]), origin=m[5], bind=m[6])
            for m in STATE.finditer(log) if m[1] == name]


def commands(args):
    lines = ['set g_autoExecAfterMapLoad ""', 'set g_stopTime 0', 'god', 'notarget']
    if args.case == 'legacy':
        return lines + ['wait 180', 'saveGame mission', 'wait 5']
    if args.case == 'fire-effects':
        fire = 'func_fire_volume_7' if args.map == 'm01' else 'func_fire_volume_1'
        return lines + ['wait 120', f'testMission {fire}', 'screenshot screenshots/fire.tga',
                        'saveGame mission', 'wait 5']
    if args.case == 'fire':
        fire = 'func_fire_volume_1'
        if not args.input:
            lines += ['wait 1200' if args.map == 'm01' else 'wait 120', f'testMission {fire}', 'testMission light_4859',
                      f'damage {fire} 1', f'testMission {fire}']
            lines += [f'damage {fire} 1 damage_freezegun'] * 20
            lines += [f'testMission {fire}', 'testMission light_4859', 'saveGame partial', 'wait 5']
        else:
            lines += [f'testMission {fire}', 'testMission light_4859']
        lines += [f'damage {fire} 1 damage_freezegun'] * 38
        view = '-1930 -2160 -160 180' if args.map == 'm01' else '-64 0 1480 90'
        lines += [f'setviewpos {view}', 'wait 10', f'testMission {fire}', 'testMission light_4859',
                  'screenshot screenshots/quenched.tga', 'saveGame mission', 'wait 5']
    elif args.case == 'repair':
        lines += ['set ai_disableAttacks 1', 'trigger item_objective_4']
        if not args.input:
            lines += ['wait 1200', 'testMission call PlaytestStartAtGen', 'wait 420',
                      'teleport gen_screen_1', 'testMission call ProcRepairBot', 'wait 2400',
                      'testMission call TakToGeneratorConsole', 'wait 180']
        else:
            lines += ['wait 180']
        lines += ['echo MISSION_BOTS_ALIVE', 'listThreads', 'testMission tak']
        lines += [f'testMission proc_bot_{i}' for i in range(1, 7)]
        lines += [f'testMission proc_bot_spot_{i}' for i in range(1, 7)]
        lines += ['saveGame waiting', 'wait 5']
        lines += [f'damage proc_bot_{i} 10000' for i in range(1, 7)]
        lines += ['wait 3600', 'echo MISSION_REPAIR_CLEARED', 'testMission tak', 'listThreads',
                  'screenshot screenshots/repair.tga', 'saveGame mission', 'wait 5']
    elif args.case == 'extraction':
        if not args.input:
            lines += ['testMission call PlaytestStartAfterPanel', 'wait 120',
                      'testMission call BringOnTheTank', 'wait 4800',
                      'testMission core_boss', 'saveGame tank_checkpoint', 'wait 5']
        lines += ['damage core_boss 100000', 'wait 600', 'testMission darnell_ending',
                  'screenshot screenshots/extraction_scene.tga', 'wait 1200', 'listThreads',
                  'testMission dekard', 'testMission darnell',
                  'screenshot screenshots/extraction.tga', 'saveGame mission', 'wait 5']
    elif args.case == 'landing':
        lines += ['testMission call LandDropShip', 'wait 2', 'testMission env_dropship_1',
                  'setviewpos 1576 2200 2100 270', 'wait 10', 'screenshot screenshots/landing.tga',
                  'wait 1200', 'testMission env_dropship_1', 'saveGame mission', 'wait 5']
    elif args.case == 'walker':
        vehicle = 'm04_maint_tunnel_unit1_uni_vehicle_walker_player'
        lines += ['wait 120', f'teleport {vehicle}', f'trigger {vehicle}', 'wait 120',
                  f'testVehicle {vehicle}', 'saveGame mission', 'wait 5']
    elif args.case == 'squad':
        lines += ['wait 120', 'trigger tak', 'wait 2', 'testMission tak', 'damage tak 10000', 'wait 10',
                  'saveGame forbidden', 'wait 5']
    elif args.case == 'objective':
        lines += ['testMission call TeamToGeneratorRoom', 'wait 5', 'testMission tak',
                  'saveGame mission', 'wait 5']
    elif args.case == 'nexus':
        lines += ['trigger item_objective_6', 'wait 2', 'echo MISSION_NEXUS_BEFORE', 'testMission player1',
                  'testMission call BossKilled', 'wait 2', 'echo MISSION_NEXUS_AFTER', 'testMission player1',
                  'saveGame mission', 'wait 5']
    return lines


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--case', choices=('legacy', 'fire', 'fire-effects', 'repair', 'landing', 'walker', 'squad', 'objective', 'nexus', 'extraction'), required=True)
    parser.add_argument('--map', choices=tuple(FIRE_MAPS), default='m02')
    parser.add_argument('--retail', type=Path, required=True)
    parser.add_argument('--awakening', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--cache', type=Path, required=True)
    parser.add_argument('--input', type=Path)
    parser.add_argument('--renderer', choices=('gl', 'vulkan'), default='gl')
    parser.add_argument('--timeout', type=int, default=240)
    args = parser.parse_args()
    if args.case == 'fire-effects' and not args.input:
        parser.error('fire-effects requires a burning-vent checkpoint via --input')
    exe = client_path(ROOT/'.install')
    verify_runtime_is_current(ROOT/'.install', exe.name)
    output = task_path(args.output)
    cache = task_path(args.cache)
    output.mkdir(parents=True, exist_ok=False)
    game = output / 'q4xbase'
    game.mkdir()
    if args.input:
        stage_input(args.input.resolve(), game/'savegames', 'mission_in')
    lines = commands(args) + ['echo MISSION_PROBE_DONE', 'quit']
    cfg = game/CFG_REL
    cfg.parent.mkdir(parents=True, exist_ok=True)
    cfg.write_text('\n'.join(lines)+'\n', encoding='ascii')
    map_name = ('game/m01_stranarus_trench2' if args.case in ('walker', 'squad') else FIRE_MAPS[args.map])
    options = argparse.Namespace(mode='load' if args.input else 'write', basepath=args.retail.resolve(),
        game='q4xbase', tag='mission', map=map_name, xvfb=False,
        set=[('fs_awakeningpath', str(args.awakening.resolve())), ('fs_devpath', str(output)),
             ('fs_cachepath', str(cache)), ('ui_retained', '1'), ('com_gameMode', 'SP'),
             ('ui_autoJoin', '0'), ('r_renderApi', args.renderer), ('sys_consoleWindow', '0'),
             ('r_fullscreenDesktop', '0'), ('r_windowWidth', '960'), ('r_windowHeight', '540'),
             ('g_autoSkipCinematics', '1' if args.case == 'fire' and args.map == 'm01' else '0')])
    arch = 'arm64' if platform.machine().lower() in ('arm64', 'aarch64') else 'x64'
    module_suffix = '.dll' if sys.platform == 'win32' else '.dylib' if sys.platform == 'darwin' else '.so'
    module_root = exe.parent.parent/'Frameworks' if sys.platform == 'darwin' else exe.parent/'baseoq4'
    content_root = exe.parent.parent/'Resources/baseoq4' if sys.platform == 'darwin' else exe.parent/'baseoq4'
    module = module_root/('game-sp_'+arch+module_suffix)
    command = launch_command(options, exe, output)
    identity = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in
                (exe, module, content_root/'pak0.pk4')}
    report = dict(case=args.case, map=map_name, command=command, identity=identity)
    (output/'plan.json').write_text(json.dumps(report, indent=2)+'\n')
    startup = None
    if sys.platform == 'win32':
        startup = subprocess.STARTUPINFO()
        startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        startup.wShowWindow = subprocess.SW_HIDE
    failures = []
    start = time.monotonic()
    path = game/'logs'/LOG_NAME
    with (output/'stdout.log').open('w') as stdout:
        process = subprocess.Popen(command, cwd=exe.parent, stdin=subprocess.DEVNULL,
            stdout=stdout, stderr=subprocess.STDOUT, startupinfo=startup,
            start_new_session=sys.platform != 'win32')
        while process.poll() is None:
            if time.monotonic()-start > args.timeout:
                failures.append('timeout')
                stop_process(process)
                break
            current = path.read_text(errors='replace') if path.exists() else ''
            if ERROR.search(current):
                stop_process(process)
                break
            time.sleep(0.5)
    log = path.read_text(errors='replace') if path.exists() else ''
    if ERROR.search(log) or log.count('MISSION_PROBE_DONE') != 1 or process.returncode != 0:
        failures.append('runtime error, nonzero exit or missing completion')
    if args.case not in ('squad', 'extraction') and not (game/'savegames/mission.save').is_file():
        failures.append('missing final save')
    if args.case == 'squad' and ("Can't save after failed mission" not in log
            or (game/'savegames/forbidden.save').exists()):
        failures.append('essential squadmate death did not fail mission and block saves')
    if args.input and ('Game Map Init SaveGame' not in log or 'loadingSaveGame=0' in log.split('AutoExecAfterMapLoad:', 1)[0]):
        failures.append('save did not restore')
    if args.case == 'fire':
        rows = states(log, 'func_fire_volume_1')
        report['fire'] = rows
        if not rows or rows[-1]['health'] > 0 or rows[-1]['contents'] != 0:
            failures.append('quenched vent still blocks collision')
        if not args.input and (len(rows) != 4 or rows[0]['health'] != rows[1]['health']
                or rows[0]['contents'] & 1 or rows[2]['health'] != 35):
            failures.append('burning/partial fire collision or damage gate incorrect')
        lights = re.findall(r'testMissionLight: name=light_4859 radius=\(([^)]+)\)', log)
        if not lights or any(abs(float(v)) > 0.01 for v in lights[-1].split()):
            failures.append('quenched fire left its light on')
    if args.case == 'fire-effects':
        fire = 'func_fire_volume_7' if args.map == 'm01' else 'func_fire_volume_1'
        rows = states(log, fire)
        effects = re.findall(r'testMissionEffect: name='+fire+r' effect=(\S+) loop=(\d) modelSuppressed=(\d)', log)
        report['effects'] = effects
        if not rows or rows[-1]['health'] <= 0:
            failures.append('fixture does not contain a burning vent')
        if len(effects) != 1 or effects[0][1:] != ('1', '0'):
            failures.append('restored fire does not have exactly one unsuppressed loop')
    if args.case == 'landing':
        anims = re.findall(r'testMissionAnim: name=env_dropship_1 anim=(\S+)', log)
        report['animations'] = anims
        if anims != ['hangar_land', 'hangar_idle']:
            failures.append('landing did not finish before idle')
    if args.case == 'extraction':
        if 'CAMPAIGN_CHAPTER reached 4 game/M03_airassault' not in log:
            failures.append('extraction did not transition to the next chapter')
        if not (game/'savegames/Autosave_game_M03_airassault.save').is_file():
            failures.append('next chapter autosave missing')
        if re.search(r"scriptedMove: '(?:dekard|darnell(?:_ending)?)'.*(?:could not start|made no progress)", log):
            failures.append('extraction actor route failed')
    if args.case == 'repair':
        before = log.split('MISSION_BOTS_ALIVE', 1)[-1].split('MISSION_REPAIR_CLEARED')[0]
        after = log.split('MISSION_REPAIR_CLEARED', 1)[-1]
        bots = [states(before, f'proc_bot_{i}') for i in range(1, 7)]
        report['bots'] = bots
        if any(not rows or rows[0]['health'] <= 0 or rows[0]['hidden']
               or rows[0]['bind'] != f'proc_bot_spot_{i}' for i, rows in enumerate(bots, 1)):
            failures.append('repair bots did not reach and bind their authored stations')
        if 'TakToGeneratorConsole' not in before or 'TakToGeneratorConsole' in after:
            failures.append('generator gate did not wait for bots then complete')
        if 'testMissionObjective: Destroy Trianthinide Generators' not in before \
                or 'testMissionObjective: Destroy Trianthinide Generators' in after:
            failures.append('generator objective did not remain active until shutdown')
        for i, rows in enumerate(bots, 1):
            spots = states(before, f'proc_bot_spot_{i}')
            delta = ([abs(a-b) for a, b in zip(map(float, rows[0]['origin'].split()),
                      map(float, spots[0]['origin'].split()))] if rows and spots else [])
            # MoveToEntity projects the third marker down onto its AAS area,
            # 16 units below the raw marker. Add the authored 8-unit arrival
            # range vertically; horizontal arrival still uses that range.
            if not delta or max(delta[:2]) > 8.01 or delta[2] > 24.01:
                failures.append(f'bot {i} did not reach its repair station')
    if args.case == 'objective' and 'testMissionObjective: Destroy Trianthinide Generators' not in log:
        failures.append('generator objective was not issued when the encounter began')
    if args.case == 'nexus':
        before = log.split('MISSION_NEXUS_BEFORE', 1)[-1].split('MISSION_NEXUS_AFTER')[0]
        after = log.split('MISSION_NEXUS_AFTER', 1)[-1]
        if 'testMissionObjective: Locate Nexus Access Point' not in before \
                or 'testMissionObjective: Locate Nexus Access Point' in after:
            failures.append('Tank victory did not complete the Nexus objective')
    if args.case == 'walker':
        seat = re.search(r'testVehicle: [^\n]*drivers=(\d+) playerSeat=(-?\d+) locked=(\d+)', log)
        if not seat or seat.groups() != ('1', '0', '1'):
            failures.append('walker did not enter its occupied, repair-locked state')
    report.update(passed=not failures, failures=failures, seconds=round(time.monotonic()-start, 1),
                  warnings=[line for line in log.splitlines() if 'WARNING:' in line])
    (output/'report.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps({k: report[k] for k in ('case', 'map', 'passed', 'failures', 'seconds')}))
    return int(bool(failures))


if __name__ == '__main__':
    raise SystemExit(main())
