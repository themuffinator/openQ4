#!/usr/bin/env python3
"""M07 gate, objective, timer and fresh-process save checks, without device input.

Uses the staged client, installed alpha, authored callbacks and engine captures.
These are focused mission-state checks, not a driven race/balance playthrough.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
import re
import subprocess
import sys
import time

from campaign_runtime import ROOT, task_path
from savegame_cross_load import CFG_REL, LOG_NAME, client_path, launch_command, stage_input, stop_process

MAPS = ('m07_race', 'm07_race1', 'm07_race2')
STATE = re.compile(r'testVehicleRace: seconds=(\S+) hundredths=(\S+) timer=(\d) timer2=(\d) '
                   r'progress=(\S+) boost=(\d) envelope=(\S+) failed=(\d)')
ERROR = re.compile(r'(?mi)^(?:\^\d)?(?:ERROR:|FATAL:|FATAL ERROR:)|could not restore saved state|'
                   r'WriteObject FindIndex failed|Unknown command|unknown command')


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def sample(log, marker):
    assert log.count(marker) == 1, f'missing/duplicate {marker}'
    section = log.split(marker, 1)[1].split('M07_SAMPLE_', 1)[0]
    match = STATE.search(section)
    assert match, f'missing race state at {marker}'
    values = list(map(float, match.groups()))
    assert all(math.isfinite(x) for x in values), 'non-finite race HUD'
    result = dict(zip(('seconds', 'hundredths', 'timer', 'timer2', 'progress', 'boost', 'envelope', 'failed'), values))
    assert result['seconds'] >= 0 and 0 <= result['hundredths'] < 100, 'negative/invalid countdown'
    seat = re.search(r'testVehicle: [^\n]*drivers=(\d+) playerSeat=(-?\d+) locked=(\d+)', section)
    assert seat and seat.groups() == ('1', '0', '1'), 'lost locked speeder seat'
    clock = re.search(r'testVehicle: [^\n]*time=(\d+)', section)
    assert clock, 'missing simulation clock'
    result['time'] = int(clock.group(1))
    result['objectives'] = re.findall(r'testMissionObjective: ([^\r\n]+)', section)
    return result


def report_state(label):
    return [f'echo M07_SAMPLE_{label}', 'testVehicle vehicle_speeder_1', 'testMission player1']


def timeout_commands():
    # Failure returns to the load screen after a short grace period. Observe
    # every four simulation seconds so a long wait cannot miss that state.
    lines = ['set com_fixedTic 10']
    for step in range(40):
        lines += ['wait 25'] + report_state(f'TIMEOUT_{step}')
    return lines + ['set com_fixedTic 1']


def bomber_entities(map_name):
    count = (3, 2, 4)[MAPS.index(map_name)]
    parts = ('strafingFlyer', 'strafeBase', 'strafeRocketA', 'strafeRocketB', 'flyerDamage')
    names = [f'{part}_{number}' for number in range(2, count+1) for part in parts]
    if map_name == 'm07_race1':
        names += [f'{part}_{number}' for number in (3, 4) for part in parts]
    return count, names


def transition_commands(map_name):
    next_map = ('m07_race1', 'm07_race2', 'm08_cryofac')[MAPS.index(map_name)]
    trigger = 'testMission call raceExitCinematic' if map_name == 'm07_race2' else 'trigger target_endlevel_1'
    return [trigger, 'set com_fixedTic 10', 'wait 120', 'set com_fixedTic 1',
            'openq4_assertMapState game/'+next_map]


def commands(args):
    lines = ['set g_autoExecAfterMapLoad ""', 'openq4_assertMapState game/'+args.map]
    if args.input:
        lines += report_state('RESTORED') + ['set g_stopTime 0']
    else:
        lines += ['god', 'notarget']
        if args.map == 'm07_race':
            # A stationary state probe stays inside the MCC's intended lethal
            # blast. Isolate that unrelated hazard; this does not test escape.
            lines += ['remove MCC_blast_pain']
        lines += ['set com_fixedTic 10',
                  'wait 320' if args.map == 'm07_race' else 'wait 12', 'set com_fixedTic 1', 'wait 2']
        if args.map == 'm07_race':
            # The first gate is introduced here, after the opening drive.
            lines += ['testMission call speederFlight02', 'set com_fixedTic 10',
                      'wait 180', 'set com_fixedTic 1', 'wait 2']
        lines += report_state('START')
        if args.case == 'timeout1':
            return lines + timeout_commands()
        lines += ['wait 30'] + report_state('FIRST')
        lines += ['testMission call startTimerArea1', 'wait 2'] + report_state('DUPLICATE')
        lines += ['testMission call area1Cleared', 'wait 2', 'testMission call area1Cleared', 'wait 2']
        lines += report_state('SECOND')
        if args.case == 'timeout2':
            return lines + timeout_commands()
        lines += ['set g_stopTime 1'] + report_state('SAVED') + ['saveGame race', 'wait 3', 'set g_stopTime 0']
    if args.case == 'outro':
        return lines + ['testMission call area2Cleared', 'wait 2'] + transition_commands(args.map)
    lines += ['testMission call area2Cleared', 'wait 2'] + report_state('FINISHED')
    # Exercise the original delayed objective window and stale callbacks.
    lines += ['testMission call area2Cleared', 'testMission call area1Cleared',
              'testMission call startTimerArea1', 'testMission call startTimerArea2',
              'testMission call speederInArea1', 'set com_fixedTic 10', 'wait 1000',
              'set com_fixedTic 1'] + report_state('SETTLED')
    count, entities = bomber_entities(args.map)
    inspect = ['testMission strafingFlyer_1'] + ['testMission '+name for name in entities]
    lines += ['echo M07_BOMBERS_BEFORE'] + inspect
    lines += [f'testMission call beginBomberRun_{number}' for number in range(2, count+1)]
    lines += ['set com_fixedTic 10', 'wait 150', 'set com_fixedTic 1']
    if args.map == 'm07_race1':
        lines += ['trigger flyerStrafeBoom_3', 'trigger flyerStrafeBoom_4', 'wait 8',
                  'testMission call bomberExplode_3', 'testMission call bomberExplode_4', 'wait 2']
    lines += ['echo M07_BOMBERS_AFTER'] + inspect
    lines += ['screenshot screenshots/finished.tga', 'saveGame finished', 'wait 3']
    return lines + transition_commands(args.map)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--map', choices=MAPS, required=True)
    parser.add_argument('--case', choices=('flow', 'timeout1', 'timeout2', 'outro'), default='flow')
    parser.add_argument('--input', type=Path)
    parser.add_argument('--retail', type=Path, required=True)
    parser.add_argument('--awakening', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--cache', type=Path, required=True)
    parser.add_argument('--renderer', choices=('gl', 'vulkan'), default='gl')
    parser.add_argument('--timeout', type=int, default=600)
    args = parser.parse_args()
    if args.case == 'outro' and args.map != 'm07_race2':
        parser.error('outro belongs to m07_race2')
    if args.input and args.case.startswith('timeout'):
        parser.error('timeout checks require a fresh map start')
    output, cache = task_path(args.output), task_path(args.cache)
    output.mkdir(parents=True, exist_ok=False)
    game = output/'q4xbase'
    game.mkdir()
    if args.input:
        stage_input(args.input.resolve(), game/'savegames', 'race_in')
    input_hash = sha(args.input) if args.input else None
    cfg = game/CFG_REL
    cfg.parent.mkdir(parents=True, exist_ok=True)
    cfg.write_text('\n'.join(commands(args)+['echo M07_PROBE_DONE', 'quit'])+'\n', encoding='ascii')
    exe = client_path(ROOT/'.install')
    options = argparse.Namespace(mode='load' if args.input else 'write', basepath=args.retail.resolve(),
        game='q4xbase', tag='race', map='game/'+args.map, xvfb=False,
        set=[('fs_awakeningpath', str(args.awakening.resolve())), ('fs_devpath', str(output)),
             ('fs_cachepath', str(cache)), ('ui_retained', '1'), ('com_gameMode', 'SP'),
             ('ui_autoJoin', '0'), ('r_renderApi', args.renderer), ('sys_consoleWindow', '0'),
             ('r_fullscreenDesktop', '0'), ('com_maxfps', '120'), ('g_autoSkipCinematics', '0')])
    command = launch_command(options, exe, output)
    modules = list((ROOT/'.install/baseoq4').glob('game-sp_*'))
    runtime = [exe, *(p for p in modules if p.suffix in ('.dll', '.so', '.dylib')),
               ROOT/'.install/baseoq4/pak0.pk4', ROOT/'.install/baseoq4/pak1.pk4']
    identity = {str(p.relative_to(ROOT)): sha(p) for p in runtime}
    asset = args.awakening/'maps/game'/f'{args.map}.script'
    asset_hash = sha(asset)
    plan = dict(case=args.case, map=args.map, command=command, identity=identity, installed_script_sha256=asset_hash)
    (output/'plan.json').write_text(json.dumps(plan, indent=2)+'\n')
    startup = None
    if sys.platform == 'win32':
        startup = subprocess.STARTUPINFO()
        startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        startup.wShowWindow = subprocess.SW_HIDE
    log_path = game/'logs'/LOG_NAME
    started = time.monotonic()
    failures = []
    with (output/'stdout.log').open('w') as stdout:
        process = subprocess.Popen(command, cwd=exe.parent, stdin=subprocess.DEVNULL, stdout=stdout,
            stderr=subprocess.STDOUT, startupinfo=startup, start_new_session=sys.platform != 'win32')
        while process.poll() is None:
            if time.monotonic()-started > args.timeout:
                failures.append('timeout')
                stop_process(process)
                break
            if log_path.exists() and ERROR.search(log_path.read_text(errors='replace')):
                stop_process(process)
                break
            time.sleep(0.5)
    log = log_path.read_text(errors='replace') if log_path.exists() else ''
    if process.returncode or ERROR.search(log) or log.count('M07_PROBE_DONE') != 1:
        failures.append('runtime error, nonzero exit or missing completion')
    samples = {}
    try:
        assert sha(asset) == asset_hash, 'installed script changed'
        for relative, digest in identity.items():
            assert sha(ROOT/relative) == digest, 'runtime was replaced during the test'
        if args.input:
            assert sha(args.input) == input_hash == sha(game/'savegames/race_in.save'), 'input save changed'
            assert 'Game Map Init SaveGame' in log, 'save was not restored'
        if not args.input:
            assert 'Awakening M07:' in log and 'mission corrections v1 enabled' in log, 'corrections not selected'
        if not args.case.startswith('timeout'):
            next_map = ('m07_race1', 'm07_race2', 'm08_cryofac')[MAPS.index(args.map)]
            assert 'expectedMap=game/'+next_map in log and 'Game Map Shutdown' in log, 'missing transition assertion'
        if args.case == 'outro':
            assert 'game/m08_cryofac' in log and 'Game Map Shutdown' in log, 'outro did not change map'
        else:
            labels = (['RESTORED'] if args.input else ['START'])
            if not args.input and args.case != 'timeout1':
                labels += ['FIRST', 'DUPLICATE', 'SECOND']
                if args.case == 'flow':
                    labels += ['SAVED']
            if not args.case.startswith('timeout'):
                labels += ['FINISHED', 'SETTLED']
            samples = {label: sample(log, 'M07_SAMPLE_'+label) for label in labels}
            if args.case.startswith('timeout'):
                for step in range(40):
                    state = sample(log, f'M07_SAMPLE_TIMEOUT_{step} ')
                    if state['failed']:
                        samples['FAILED'] = state
                        break
                assert 'FAILED' in samples, 'missed gate did not fail mission'
            if args.input:
                previous_log = args.input.resolve().parents[2]/'q4xbase/logs'/LOG_NAME
                previous = sample(previous_log.read_text(errors='replace'), 'M07_SAMPLE_SAVED')
                assert samples['RESTORED'] == previous, 'saved race state changed on restore'
            if 'FIRST' in samples:
                a, b, c = (samples[x] for x in ('FIRST', 'DUPLICATE', 'SECOND'))
                assert a['seconds']-1 <= b['seconds'] <= a['seconds'], 'duplicate timer restarted deadline'
                base = 60 if args.map == 'm07_race' else 70
                assert a['seconds']+base-1 <= c['seconds'] <= a['seconds']+base+1, 'lost or doubled carried time'
                assert len(c['objectives']) == 1, 'second objective missing or duplicated'
            if args.case.startswith('timeout'):
                assert samples['FAILED']['failed'] == 1, 'missed gate did not fail mission'
                assert samples['FAILED']['seconds'] == samples['FAILED']['hundredths'] == 0, 'expired timer did not settle at zero'
                running = samples['START' if args.case == 'timeout1' else 'SECOND']
                elapsed = (samples['FAILED']['time'] - running['time']) / 1000
                assert elapsed >= running['seconds'] + running['hundredths']/100 + 4.9, 'gate failed before final closure'
            else:
                running = samples['RESTORED' if args.input else 'SAVED']
                elapsed = (samples['SETTLED']['time'] - running['time']) / 1000
                assert elapsed > running['seconds'] + running['hundredths']/100 + 5, 'probe did not outlast gate closure'
                for label in ('FINISHED', 'SETTLED'):
                    s = samples[label]
                    assert s['timer'] == s['timer2'] == s['failed'] == 0, f'completion resurrected/fails at {label}'
                    assert s['progress'] == 10 and not s['objectives'], f'incomplete progress/objectives at {label}'
                assert (game/'savegames/finished.save').is_file(), 'completion not saveable'
                assert (game/'screenshots/finished.tga').is_file(), 'engine capture missing'
                before, after = log.split('M07_BOMBERS_BEFORE', 1)[1].split('M07_BOMBERS_AFTER', 1)
                assert 'testMission: missing=' not in before, 'bomber fixture missing before probe'
                assert 'testMission: name=strafingFlyer_1 ' in after, 'later bomber removed first bomber'
                for name in bomber_entities(args.map)[1]:
                    assert 'testMission: missing='+name+'\n' in after, 'bomber not removed: '+name
                bomber_log = log.split('M07_BOMBERS_BEFORE', 1)[1].split('Game Map Shutdown', 1)[0]
                assert 'unknown function' not in bomber_log and 'expected an existing' not in bomber_log, 'unresolved bomber callbacks'
    except (AssertionError, OSError, ValueError) as error:
        failures.append(str(error))
    report = dict(plan, passed=not failures, failures=failures, samples=samples,
                  seconds=time.monotonic()-started, warnings=re.findall(r'(?mi)^.*WARNING:.*$', log))
    (output/'report.json').write_text(json.dumps(report, indent=2)+'\n')
    print(f'{args.map}/{args.case}: {"PASS" if report["passed"] else "FAIL"}: {failures}', flush=True)
    raise SystemExit(bool(failures))


if __name__ == '__main__':
    main()
