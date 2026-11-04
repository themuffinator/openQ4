#!/usr/bin/env python3
"""Qualify authored Awakening vehicle entry, shots and fresh-process save restores.

Uses engine commands in hidden, windowed SP. No device input, console-script
compilation, replacement map assets or desktop captures are used.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import re
import subprocess
import sys
import time
from pathlib import Path

from campaign_runtime import ROOT, task_path
from savegame_cross_load import CFG_REL, LOG_NAME, Payload, launch_command, stage_input, stop_process

CASES = {
    'turret': ('game/m04_prison', 'vehicle_q4x_turret_chaingun_4'),
    'walker': ('game/m01_stranarus_trench2', 'm04_maint_tunnel_unit1_uni_vehicle_walker_player'),
}
STATE = re.compile(r'testVehicle: name=(\S+) class=(\S+) time=(\d+) origin=\(([^)]+)\) '
                   r'positions=(\d+) drivers=(\d+) playerSeat=(-?\d+) locked=(\d+) hidden=(\d+) '
                   r'movement=(\d+) shooting=(\d+) health=(-?\d+)')
SEAT = re.compile(r'testVehicleSeat: name=(\S+) index=(\d+) driver=(\S+) parent=(\S+)')
ERROR = re.compile(r'(?mi)^(?:\^\d)?(?:ERROR:|FATAL ERROR:)|could not restore saved state|'
                   r'WriteObject FindIndex failed|Unknown command|unknown command|non-finite')


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def sample(text, marker, vehicle):
    assert text.count(marker) == 1, f'missing/duplicate {marker}'
    section = text.split(marker, 1)[1].split('Q4_VEHICLE_', 1)[0]
    match = STATE.search(section)
    assert match and match[1] == vehicle, f'missing vehicle report at {marker}'
    keys = ('positions', 'drivers', 'playerSeat', 'locked', 'hidden', 'movement', 'shooting', 'health')
    result = dict(zip(keys, map(int, match.groups()[4:])))
    result.update(name=match[1], classname=match[2], time=int(match[3]),
                  origin=list(map(float, match[4].split())))
    seat_section = section[match.end():].split('testVehicle:', 1)[0]
    result['seats'] = [dict(index=int(m[2]), driver=m[3], parent=m[4])
                       for m in SEAT.finditer(seat_section) if m[1] == vehicle]
    hud = re.search(r'testVehicleHud: name=(\S+) health=(\S+) shield=(\S+)', seat_section)
    assert hud and hud[1] == vehicle, f'missing HUD state at {marker}'
    result['hud'] = dict(health=float(hud[2]), shield=float(hud[3]))
    assert all(math.isfinite(v) and 0 <= v <= 1 for v in result['hud'].values()), 'invalid HUD fraction'
    assert len(result['seats']) == result['positions']
    return result


def occupied(state):
    return (state['drivers'] == 1 and state['playerSeat'] >= 0 and not state['hidden']
            and any(s['index'] == state['playerSeat'] and s['driver'] == 'player1'
                    and s['parent'] == state['name'] for s in state['seats']))


def preserved(before, after):
    return (bool(after) and all(before[k] == after[k] for k in
                ('classname', 'positions', 'drivers', 'playerSeat', 'locked', 'hidden', 'movement', 'shooting', 'health', 'hud', 'seats'))
            and max(abs(a-b) for a, b in zip(before['origin'], after['origin'])) <= 0.01)


def run_leg(args, case, leg, source=None):
    map_name, vehicle = CASES[case]
    home = args.output / case / leg
    home.mkdir(parents=True, exist_ok=False)
    game = home / 'q4xbase'
    game.mkdir()
    slot = 'vehicle'
    if source:
        stage_input(source, game / 'savegames', slot+'_in')
    lines = ['set g_autoExecAfterMapLoad ""']
    if leg == 'write':
        # Trigger uses the stock vehicle activation and EnterVehicle path. The
        # walker's authored enter_script starts its locked repair sequence.
        lines += ['god', 'notarget', 'wait 90', f'teleport {vehicle}', f'trigger {vehicle}', 'wait 90']
    lines += ['echo Q4_VEHICLE_INITIAL', f'testVehicle {vehicle}']
    if leg != 'reload':
        lines += ['set g_stopTime 0', 'wait 90', f'testVehicle {vehicle} fire', 'wait 2',
                  'screenshot screenshots/firing.tga', 'set g_stopTime 1',
                  'echo Q4_VEHICLE_SAVED', f'testVehicle {vehicle}', f'saveGame {slot}', 'wait 3']
    else:
        lines += ['set g_stopTime 0', 'wait 90', f'testVehicle {vehicle} exit',
                  'echo Q4_VEHICLE_EXIT', f'testVehicle {vehicle}', 'screenshot screenshots/exit.tga']
    lines += ['echo Q4_VEHICLE_DONE', 'quit']
    cfg = game / CFG_REL
    cfg.parent.mkdir(parents=True)
    cfg.write_text('\n'.join(lines)+'\n', encoding='ascii')
    options = argparse.Namespace(mode='load' if source else 'write', basepath=args.retail,
        game='q4xbase', tag=slot, map=map_name, xvfb=False,
        set=[('fs_awakeningpath', str(args.awakening_root)), ('ui_autoJoin', '0'), ('com_gameMode', 'SP'),
             ('ui_retained', '1'), ('r_renderApi', args.renderer), ('sys_consoleWindow', '0'),
             ('fs_devpath', str(home)), ('fs_cachepath', str(args.cache))])
    executable = ROOT / '.install/openQ4-client_x64.exe'
    command = launch_command(options, executable, home)
    report = dict(case=case, leg=leg, map=map_name, vehicle=vehicle, command=command,
                  input_sha256=sha(source) if source else None)
    (home/'plan.json').write_text(json.dumps(report, indent=2)+'\n')
    log = game/'logs'/LOG_NAME
    startup = subprocess.STARTUPINFO()
    startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = subprocess.SW_HIDE
    started = time.monotonic()
    with (home/'stdout.log').open('w') as stdout:
        process = subprocess.Popen(command, cwd=executable.parent, stdin=subprocess.DEVNULL,
            stdout=stdout, stderr=subprocess.STDOUT, startupinfo=startup)
        while process.poll() is None and time.monotonic()-started < args.timeout:
            if log.exists() and ERROR.search(log.read_text(errors='replace')):
                break
            time.sleep(0.5)
        timed_out = time.monotonic()-started >= args.timeout
        stop_process(process)
    text = log.read_text(errors='replace') if log.exists() else ''
    failures = []
    if process.returncode != 0 or timed_out:
        failures.append(f'exit={process.returncode}, timeout={timed_out}')
    if ERROR.search(text) or text.count('Q4_VEHICLE_DONE') != 1:
        failures.append('runtime error or missing completion marker')
    capture = game/'screenshots'/('exit.tga' if leg == 'reload' else 'firing.tga')
    if not capture.is_file():
        failures.append('engine screenshot missing')
    try:
        initial = sample(text, 'Q4_VEHICLE_INITIAL', vehicle)
        report['initial'] = initial
        if not occupied(initial):
            failures.append('player is not in the authored vehicle')
        if source and (report['input_sha256'] != sha(source)
                       or report['input_sha256'] != sha(game/'savegames'/f'{slot}_in.save')):
            failures.append('input save changed')
        if source and 'Game Map Init SaveGame' not in text:
            failures.append('save was not restored')
        if leg != 'reload':
            report['saved'] = sample(text, 'Q4_VEHICLE_SAVED', vehicle)
            if not occupied(report['saved']):
                failures.append('save was not made while driving')
            if f'testVehicle: action=fire name={vehicle} success=1' not in text:
                failures.append('vehicle did not fire')
            save = game/'savegames'/f'{slot}.save'
            payload = Payload(save.read_bytes())
            if payload.version != 5 or payload.wire_abi != 'le64-portable1':
                failures.append('unexpected saved layout')
            report.update(save=str(save), save_sha256=sha(save), save_stamp=payload.summary())
        else:
            final = sample(text, 'Q4_VEHICLE_EXIT', vehicle)
            report['exit'] = final
            expected_exit = 0 if initial['locked'] else 1
            if f'testVehicle: action=exit name={vehicle} success={expected_exit}' not in text:
                failures.append('exit did not respect the vehicle lock')
            if bool(occupied(final)) != bool(initial['locked']):
                failures.append('seat ownership after exit is wrong')
    except (AssertionError, OSError, ValueError) as error:
        failures.append(str(error))
    report.update(passed=not failures, failures=failures, elapsed=time.monotonic()-started,
                  engine_log_sha256=sha(log) if log.exists() else None,
                  captures={p.name:sha(p) for p in (game/'screenshots').glob('*.tga')})
    (home/'report.json').write_text(json.dumps(report, indent=2)+'\n')
    print(f'{case}/{leg}: {"PASS" if report["passed"] else "FAIL"} {failures}', flush=True)
    return report


def self_test():
    row = ('testVehicle: name=turret class=rvVehicleStatic time=100 origin=(1 2 3) positions=1 '
           'drivers=1 playerSeat=0 locked=0 hidden=0 movement=0 shooting=1 health=100\n'
           'testVehicleHud: name=turret health=1 shield=0\n'
           'testVehicleSeat: name=turret index=0 driver=player1 parent=turret\n')
    state = sample('Q4_VEHICLE_INITIAL\n'+row+'Q4_VEHICLE_DONE', 'Q4_VEHICLE_INITIAL', 'turret')
    assert occupied(state) and preserved(state, state)
    for key, value in (('drivers',0), ('playerSeat',-1), ('locked',1), ('origin',[9,2,3])):
        wrong = {**state, key:value}
        assert not preserved(state, wrong)
    assert not occupied({**state, 'seats':[dict(index=0, driver='other', parent='turret')]})
    for broken in ('', 'Q4_VEHICLE_INITIAL\nQ4_VEHICLE_INITIAL\n'+row,
                   'Q4_VEHICLE_INITIAL\n'+row.replace('health=1 shield=0', 'health=nan shield=0')):
        try:
            sample(broken, 'Q4_VEHICLE_INITIAL', 'turret')
        except AssertionError:
            pass
        else:
            raise AssertionError('missing or duplicate sample escaped detection')
    print('awakening_vehicle_runtime self-test: ok')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--self-test', action='store_true')
    parser.add_argument('--retail', type=Path)
    parser.add_argument('--awakening-root', type=Path)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--cache', type=Path, help='Reusable project .tmp cache; defaults to output/cache')
    parser.add_argument('--renderer', choices=('gl','vulkan'), default='gl')
    parser.add_argument('--case', choices=(*CASES, 'all'), default='all')
    parser.add_argument('--timeout', type=float, default=300)
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return 0
    if not all((args.retail, args.awakening_root, args.output)):
        parser.error('--retail, --awakening-root and --output are required')
    args.retail = args.retail.resolve(strict=True)
    args.awakening_root = args.awakening_root.resolve(strict=True)
    args.output = task_path(args.output)
    args.cache = task_path(args.cache or args.output/'cache')
    if args.output.exists() and any(args.output.iterdir()):
        parser.error('output must be new or empty')
    args.output.mkdir(parents=True, exist_ok=True)
    runtime = ROOT/'.install'
    identities = {p.relative_to(runtime).as_posix():sha(p) for p in
        (runtime/'openQ4-client_x64.exe', runtime/'baseoq4/game-sp_x64.dll',
         runtime/'renderer-gl_x64.dll', runtime/'renderer-vk_x64.dll', runtime/'baseoq4/pak0.pk4', runtime/'baseoq4/pak1.pk4')}
    for name in ('openQ4-client_x64.exe', 'baseoq4/game-sp_x64.dll'):
        assert identities[name] == sha(ROOT/'builddir'/name), 'Re-stage the current build first'
    reports = []
    for case in CASES if args.case == 'all' else (args.case,):
        source, previous = None, None
        for leg in ('write','load','reload'):
            report = run_leg(args, case, leg, source)
            if previous and not preserved(previous['saved'], report.get('initial', {})):
                report['failures'].append('vehicle state changed across restore')
                report['passed'] = False
            reports.append(report)
            if not report['passed']:
                break
            previous = report
            source = Path(report['save']) if leg != 'reload' else None
    report = dict(passed=all(r['passed'] for r in reports), renderer=args.renderer, results=reports,
                  runtime=identities, harness_sha256=sha(Path(__file__)), input_control=False,
                  scope='Authored vehicle activation, occupied save/reload, one shot and lock-respecting exit; not a campaign playthrough.')
    if any(identities[name] != sha(runtime/name) for name in identities):
        report['passed'] = False
        report['runtime_changed'] = True
    (args.output/'report.json').write_text(json.dumps(report, indent=2)+'\n')
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
