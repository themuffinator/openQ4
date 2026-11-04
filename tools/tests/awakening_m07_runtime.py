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
    controls = re.search(r'testVehicle: [^\n]*movement=(\d) shooting=(\d)', section)
    assert controls, 'missing vehicle control state'
    result['movement'], result['shooting'] = map(int, controls.groups())
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


def failure_commands(args, second=False):
    # Sample inside the authored ten-second failure return delay. The fixture's
    # fixed start waits leave ~32/69 seconds in stage one and ~92/139 in two.
    frames = (640 if args.map == 'm07_race' else 930) if second else (250 if args.map == 'm07_race' else 480)
    lines = ['set com_fixedTic 10', f'wait {frames}', 'set com_fixedTic 1'] + report_state('FAILED')
    lines += ['testMission call area1Cleared', 'wait 2', 'testMission call area2Cleared',
              'testMission call startTimerArea1', 'testMission call startTimerArea2', 'wait 2']
    lines += [f'testMission call raceProgress{progress}' for progress in range(0, 101, 10)]
    if args.map == 'm07_race2':
        lines += ['testMission call raceExitCinematic', 'wait 100']
    return lines + report_state('AFTER_FAILED')


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


def encounter_commands(args):
    number = args.bomber
    parts = ('strafingFlyer', 'strafeBase', 'strafeRocketA', 'strafeRocketB',
             'flyerDamage', 'flyerStrafeBoom', 'flyerStrafeExplode')
    inspect = [f'testMission {part}_{number}' for part in parts] + ['testMission strafingFlyer_1']
    lines = []
    if not args.input:
        lines += ['testMission call area1Cleared', 'wait 2', 'testMission call area2Cleared', 'wait 2',
                  'echo M07_ENCOUNTER_BEFORE', 'listEntities *'] + inspect
        if args.attack_phase != 'before':
            # Strike between the paired shots: the third map omits bomber two's
            # one-second start delay; bomber four opens its girder volley at .3s.
            frames = 720 if args.attack_phase == 'retreat' else (22 if number == 4 else 129 if args.map == 'm07_race2' else 192)
            lines += [f'testMission call beginBomberRun_{number}', f'wait {frames}']
        lines += ['echo M07_ENCOUNTER_STRIKE', f'testMission call bomberExplode_{number}',
                  f'testMission call bomberExplode_{number}', 'wait 1', 'set g_stopTime 1',
                  'echo M07_ENCOUNTER_SAVED', 'listThreads', 'saveGame bomber',
                  'wait 3', 'set g_stopTime 0']
    else:
        lines += ['echo M07_ENCOUNTER_STRIKE', f'testMission call bomberExplode_{number}']
    lines += ['wait 8', 'echo M07_ENCOUNTER_DESTROYED', 'listEntities *'] + inspect
    if args.attack_phase == 'before':
        lines += [f'testMission call beginBomberRun_{number}']
    lines += [f'testMission call bomberExplode_{number}', 'wait 200',
              'echo M07_ENCOUNTER_FINAL', 'listEntities *', 'listThreads'] + inspect
    return lines + ['saveGame encounter_finished', 'wait 3']


def commands(args):
    lines = ['set g_autoExecAfterMapLoad ""', 'openq4_assertMapState game/'+args.map]
    if args.input:
        lines += report_state('RESTORED') + ['set g_stopTime 0']
        if args.case == 'encounters':
            return lines + encounter_commands(args)
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
        if args.case == 'encounters':
            return lines + encounter_commands(args)
        if args.case == 'failed':
            return lines + failure_commands(args)
        if args.case == 'machinery':
            lines += ['testMission call area1Cleared', 'wait 2', 'testMission call area2Cleared',
                      'wait 2', 'set com_fixedTic 10']
            for step in range(25):
                lines += ['wait 25', f'echo M07_MACHINE_{step}', 'testVehicle vehicle_speeder_1',
                          'testMission pipe_section_1', 'testMission pipeReamer_1', 'testMission pipeReamer_2',
                          'listThreads']
            return lines + ['set com_fixedTic 1', 'saveGame machinery', 'wait 3']
        if args.case == 'timeout1':
            return lines + timeout_commands()
        lines += ['wait 30'] + report_state('FIRST')
        lines += ['testMission call startTimerArea1', 'wait 2'] + report_state('DUPLICATE')
        lines += ['testMission call area1Cleared', 'wait 2', 'testMission call area1Cleared', 'wait 2']
        lines += report_state('SECOND')
        if args.case == 'timeout2':
            return lines + timeout_commands()
        if args.case == 'failed2':
            return lines + failure_commands(args, second=True)
        lines += ['set g_stopTime 1'] + report_state('SAVED') + ['saveGame race', 'wait 3', 'set g_stopTime 0']
    if args.case == 'outro':
        return lines + ['testMission call area2Cleared', 'wait 2'] + transition_commands(args.map)
    lines += ['testMission call area2Cleared', 'wait 2'] + report_state('FINISHED')
    if args.case == 'boundaries':
        lines += [f'testMission call raceProgress{progress}' for progress in range(0, 100, 10)]
        # The stock objective popup temporarily suppresses vehicle HUD drawing.
        # Let it close before the capture, without dismissing it through input.
        lines += ['set com_fixedTic 10', 'wait 75', 'set com_fixedTic 1'] + report_state('SETTLED')
        lines += ['screenshot screenshots/finished.tga', 'saveGame finished', 'wait 3']
        lines += transition_commands(args.map)
        return lines + (report_state('NEXT') if args.map != 'm07_race2' else [])
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
    parser.add_argument('--case', choices=('flow', 'timeout1', 'timeout2', 'outro', 'boundaries', 'failed', 'failed2', 'machinery', 'encounters'), default='flow')
    parser.add_argument('--attack-phase', choices=('before', 'volley', 'retreat'), default='volley')
    parser.add_argument('--bomber', type=int, choices=(2, 4), default=2)
    parser.add_argument('--input', type=Path)
    parser.add_argument('--saved-state', choices=('second', 'finished'), default='second',
                        help='State in --input; finished is only supported by the boundaries check')
    parser.add_argument('--retail', type=Path, required=True)
    parser.add_argument('--awakening', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--cache', type=Path, required=True)
    parser.add_argument('--renderer', choices=('gl', 'vulkan'), default='gl')
    parser.add_argument('--timeout', type=int, default=600)
    args = parser.parse_args()
    if args.saved_state == 'finished' and (not args.input or args.case != 'boundaries'):
        parser.error('finished restore requires --input and --case boundaries')
    if args.case == 'outro' and args.map != 'm07_race2':
        parser.error('outro belongs to m07_race2')
    if args.input and args.case in ('timeout1', 'timeout2', 'failed', 'failed2', 'machinery'):
        parser.error('this check requires a fresh map start')
    if args.case == 'machinery' and args.map != 'm07_race1':
        parser.error('machinery belongs to m07_race1')
    if args.case == 'encounters' and args.attack_phase == 'retreat' and args.map != 'm07_race1':
        parser.error('retreat fixture belongs to m07_race1')
    if args.bomber == 4 and (args.case != 'encounters' or args.map != 'm07_race2'):
        parser.error('bomber four fixture belongs to m07_race2 encounters')
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
    plan = dict(case=args.case, map=args.map, attack_phase=args.attack_phase if args.case == 'encounters' else None,
                bomber=args.bomber if args.case == 'encounters' else None,
                saved_state=('bomber' if args.case == 'encounters' else args.saved_state) if args.input else None,
                command=command, identity=identity, installed_script_sha256=asset_hash)
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
            assert 'Awakening M07:' in log and 'mission corrections v3 enabled' in log, 'corrections not selected'
        if args.case in ('flow', 'outro', 'boundaries'):
            next_map = ('m07_race1', 'm07_race2', 'm08_cryofac')[MAPS.index(args.map)]
            assert 'expectedMap=game/'+next_map in log and 'Game Map Shutdown' in log, 'missing transition assertion'
        if args.case == 'encounters':
            before_log = (args.input.resolve().parents[2]/'q4xbase/logs'/LOG_NAME).read_text(errors='replace') if args.input else log
            before = before_log.split('M07_ENCOUNTER_BEFORE', 1)[1].split('M07_ENCOUNTER_STRIKE', 1)[0]
            final = log.split('M07_ENCOUNTER_FINAL', 1)[1]
            destroyed = log.split('M07_ENCOUNTER_DESTROYED', 1)[1].split('M07_ENCOUNTER_FINAL', 1)[0]
            def count_entities(section, classname):
                return len(re.findall(r'(?m)^\s*\d+: '+re.escape(classname)+r'\s', section))
            samples = {key: {'pilots': count_entities(section, 'monster_strogg_marine'),
                             'aim_targets': count_entities(section, 'target_null')}
                       for key, section in (('BEFORE', before), ('DESTROYED', destroyed), ('FINAL', final))}
            assert samples['FINAL']['pilots'] == samples['BEFORE']['pilots'] + 1, 'duplicate/missing ejected pilot'
            # The girder attack retires its two authored map aim points when
            # interrupted; normal strafing owns only its temporary aim point.
            removed_targets = 2 if args.bomber == 4 and args.attack_phase == 'volley' else 0
            assert samples['FINAL']['aim_targets'] == samples['BEFORE']['aim_targets'] - removed_targets, 'stranded/missing volley aim target'
            for part in ('strafingFlyer', 'strafeBase', 'strafeRocketA', 'strafeRocketB', 'flyerDamage', 'flyerStrafeBoom', 'flyerStrafeExplode'):
                assert f'testMission: missing={part}_{args.bomber}\n' in destroyed, 'shot-down bomber left '+part
            assert 'testMission: name=strafingFlyer_1 ' in final, 'destroyed another bomber'
            assert not re.search(r'(?m)^\s*\d+: (?:beginBomberRun_'+str(args.bomber)+'|bomberExplode_'+str(args.bomber)+r')\s+:', final), 'stranded bomber thread'
            encounter_log = log.split('M07_ENCOUNTER_STRIKE', 1)[1]
            assert not re.search(r'(?i)WARNING:.*\.script\(|unknown function|expected an existing', encounter_log), 'encounter emitted script warnings'
            assert (game/'savegames/encounter_finished.save').is_file(), 'encounter not saveable'
        elif args.case == 'machinery':
            pipe_threads = set()
            appearances = {}
            for step in range(25):
                section = log.split(f'M07_MACHINE_{step} ', 1)[1].split('M07_MACHINE_', 1)[0]
                ids = re.findall(r'(?m)^\s*(\d+): pipeCycle\s+:', section)
                assert len(ids) <= 1, 'overlapping/stalled pipe cycles'
                pipe_threads.update(ids)
                for thread_id in ids:
                    appearances[thread_id] = appearances.get(thread_id, 0) + 1
                assert 'testMission: missing=' not in section, 'missing machinery'
            assert len(pipe_threads) >= 2 and max(appearances.values()) <= 9, 'pipe cycle did not finish and repeat'
            assert (game/'savegames/machinery.save').is_file(), 'machinery not saveable'
            samples = {'pipe_threads': sorted(pipe_threads), 'appearances': appearances}
        elif args.case in ('failed', 'failed2'):
            labels = ('START', 'SECOND', 'FAILED', 'AFTER_FAILED') if args.case == 'failed2' else ('START', 'FAILED', 'AFTER_FAILED')
            samples = {label: sample(log, 'M07_SAMPLE_'+label) for label in labels}
            failed, later = samples['FAILED'], samples['AFTER_FAILED']
            running = samples['SECOND' if args.case == 'failed2' else 'START']
            assert (failed['time'] - running['time']) / 1000 >= running['seconds'] + running['hundredths']/100 + 4.9, 'failed before final closure'
            assert failed['seconds'] == failed['hundredths'] == 0, 'failed timer did not settle at zero'
            assert failed['failed'] == later['failed'] == 1, 'failure did not persist'
            assert failed['movement'] == failed['shooting'] == 0, 'failed race still has vehicle controls'
            assert failed['objectives'] == later['objectives'] and failed['progress'] == later['progress'], 'late checkpoint completed a failed race'
            assert all(failed[key] == later[key] for key in ('seconds', 'hundredths', 'timer', 'timer2', 'movement', 'shooting')), 'failed state changed after callbacks'
        elif args.case == 'outro':
            assert 'game/m08_cryofac' in log and 'Game Map Shutdown' in log, 'outro did not change map'
        else:
            labels = (['RESTORED'] if args.input else ['START'])
            if not args.input and args.case != 'timeout1':
                labels += ['FIRST', 'DUPLICATE', 'SECOND']
                if args.case in ('flow', 'boundaries'):
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
                previous = sample(previous_log.read_text(errors='replace'),
                                  'M07_SAMPLE_SETTLED' if args.saved_state == 'finished' else 'M07_SAMPLE_SAVED')
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
                if args.case == 'flow':
                    elapsed = (samples['SETTLED']['time'] - running['time']) / 1000
                    assert elapsed > running['seconds'] + running['hundredths']/100 + 5, 'probe did not outlast gate closure'
                for label in ('FINISHED', 'SETTLED'):
                    s = samples[label]
                    assert s['timer'] == s['timer2'] == s['failed'] == 0, f'completion resurrected/fails at {label}'
                    assert s['progress'] == 10 and not s['objectives'], f'incomplete progress/objectives at {label}'
                assert (game/'savegames/finished.save').is_file(), 'completion not saveable'
                assert (game/'screenshots/finished.tga').is_file(), 'engine capture missing'
                if args.case == 'flow':
                    before, after = log.split('M07_BOMBERS_BEFORE', 1)[1].split('M07_BOMBERS_AFTER', 1)
                    assert 'testMission: missing=' not in before, 'bomber fixture missing before probe'
                    assert 'testMission: name=strafingFlyer_1 ' in after, 'later bomber removed first bomber'
                    for name in bomber_entities(args.map)[1]:
                        assert 'testMission: missing='+name+'\n' in after, 'bomber not removed: '+name
                    bomber_log = log.split('M07_BOMBERS_BEFORE', 1)[1].split('Game Map Shutdown', 1)[0]
                    assert 'unknown function' not in bomber_log and 'expected an existing' not in bomber_log, 'unresolved bomber callbacks'
                elif args.map != 'm07_race2':
                    samples['NEXT'] = sample(log, 'M07_SAMPLE_NEXT')
                    assert samples['NEXT']['progress'] == 0, 'completed HUD carried into next race'
    except (AssertionError, OSError, ValueError, IndexError) as error:
        failures.append(str(error))
    report = dict(plan, passed=not failures, failures=failures, samples=samples,
                  seconds=time.monotonic()-started, warnings=re.findall(r'(?mi)^.*WARNING:.*$', log))
    (output/'report.json').write_text(json.dumps(report, indent=2)+'\n')
    print(f'{args.map}/{args.case}: {"PASS" if report["passed"] else "FAIL"}: {failures}', flush=True)
    raise SystemExit(bool(failures))


if __name__ == '__main__':
    main()
