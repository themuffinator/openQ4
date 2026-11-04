#!/usr/bin/env python3
"""Qualify M04 alarm pacing and old/new saves through the actual null OpenAL mixer.

Uses the staged SP executable, hidden/windowed, with no input injection. Baseline
mode creates regression saves with the previous binary before rebuilding it.
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
from sound_distance_runtime import fields

SIREN = 'sound/ambience/alarms_buzzers/1shot_alarm01_q4xdm7.wav'
# Observe the first console from Tak's authored standing position; its speaker
# itself sits inside dressing where a listener would be outside the render world.
COMPUTERS = (('compy_alarm1', '404 -2272 570', 3),
             ('compy_alarm2', '774.87 -623.03 342.1', 3),
             ('alarm_compy2', '923.1 -1083.58 370.25', 1))


def sample(label, shader=SIREN):
    # DeclManager canonicalizes implicit sample declarations without extensions.
    return [f'echo ALARM_{label}', f's_soundDistances {shader.rsplit(".", 1)[0]}', 'echo ALARM_SAMPLE_END']


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--retail', type=Path, required=True)
    parser.add_argument('--awakening-root', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--mode', choices=('baseline', 'fresh', 'active', 'gap', 'off'), required=True)
    parser.add_argument('--source', type=Path)
    parser.add_argument('--timeout', type=float, default=300)
    args = parser.parse_args()
    home = task_path(args.output)
    game = home/'q4xbase'
    game.mkdir(parents=True, exist_ok=False)
    if args.source:
        stage_input(args.source.resolve(strict=True), game/'savegames', 'alarm_in')
    lines = ['set g_autoExecAfterMapLoad ""', 'set g_stopTime 0']
    if not args.source:
        lines += ['god', 'notarget', 'noclip']
    lines += ['setviewpos 1416 -3104 528 0 0 0', 'waitMsec 350']
    if args.mode == 'baseline':
        lines += ['saveGame alarm_off', 'trigger security_alarm_1', 'waitMsec 350',
                  *sample('BASELINE'), 'saveGame alarm_active']
    else:
        lines += ['listSpawnArgs security_alarm_1', 'listSpawnArgs compy_alarm1',
                  'listSpawnArgs compy_alarm2', 'listSpawnArgs alarm_compy2']
        if args.mode == 'fresh':
            lines += sample('INITIAL_OFF')
            lines += ['trigger security_alarm_1', 'waitMsec 350']
        elif args.mode == 'off':
            lines += sample('RESTORED_OFF')
            lines += ['waitMsec 17000', *sample('STILL_OFF'),
                      'trigger security_alarm_1', 'waitMsec 350']
        lines += sample('RESTORED_GAP' if args.mode == 'gap' else 'ON')
        lines += ['waitMsec 2200', *sample('GAP'), 'saveGame alarm_gap',
                  # A restored gap already consumed part of its interval.
                  f'waitMsec {6000 if args.mode == "gap" else 8250}']
        repeat_count = 33 if args.mode == 'gap' else 24
        for i in range(repeat_count):
            lines += ['waitMsec 250', *sample(f'REPEAT_{i}')]
        # Stop in a guaranteed gap: the last possible repeat ends by 17.5 s.
        lines += ['waitMsec 2000', 'trigger security_alarm_1', 'waitMsec 2000',
                  *sample('STOP'), 'saveGame alarm_off',
                  'waitMsec 17000', *sample('STOP_PERSISTS')]
        # The authored terminal must still own shutoff and objective completion.
        lines += ['trigger security_alarm_1', 'waitMsec 350', *sample('RESTART'),
                  'testMission call unlockSecRoomDoors', 'waitMsec 2200',
                  *sample('CONSOLE_OFF'), 'waitMsec 17000', *sample('CONSOLE_STAYS_OFF')]
        # Positional falloff is tested with the same source at a distant listener.
        lines += ['setviewpos 978 660 320 0 0 0', 'waitMsec 100',
                  'trigger security_alarm_1', 'waitMsec 350', *sample('DISTANT')]
        if args.mode == 'fresh':
            for name, origin, recording in COMPUTERS:
                shader = f'sound/ambience/m05/computer_alarm{recording}.ogg'
                # Inaudible one-shots can be culled. Inspect each real speaker
                # nearby, restarting its timer with the normal toggle event.
                lines += [f'setviewpos {origin} 0 0 0', f'trigger {name}', f'trigger {name}',
                          'waitMsec 350', *sample('COMPUTER_'+name, shader),
                          'waitMsec 4500', *sample('COMPUTER_GAP_'+name, shader), f'trigger {name}']
    lines += ['echo ALARM_DONE', 'quit']
    cfg = game/CFG_REL
    cfg.parent.mkdir(parents=True)
    cfg.write_text('\n'.join(lines)+'\n', encoding='ascii')
    options = argparse.Namespace(mode='load' if args.source else 'write',
        basepath=args.retail.resolve(strict=True), game='q4xbase', tag='alarm',
        map='game/m04_prison', xvfb=False, set=[
            ('fs_awakeningpath', str(args.awakening_root.resolve(strict=True))),
            ('fs_devpath', str(home)), ('fs_cachepath', str(task_path(ROOT/'.tmp/awakening-m03-m04/gl/cache'))),
            ('com_gameMode', 'SP'), ('ui_autoJoin', '0'), ('ui_retained', '1'),
            ('sys_consoleWindow', '0'), ('r_fullscreenDesktop', '0'),
            ('com_fixedTic', '0'), ('s_noSound', '0'), ('s_muteUnfocused', '0'),
            ('s_useOcclusion', '1'), ('s_volume', '1'), ('s_volume_dB', '0'), ('ai_disable', '1')])
    runtime = ROOT/'.install'
    exe = runtime/'openQ4-client_x64.exe'
    identity = {name: sha256(runtime/name) for name in
                ('openQ4-client_x64.exe', 'baseoq4/game-sp_x64.dll', 'baseoq4/pak0.pk4')}
    for name in ('openQ4-client_x64.exe', 'baseoq4/game-sp_x64.dll'):
        assert identity[name] == sha256(ROOT/'builddir'/name), 'stage the current build first'
    command = launch_command(options, exe, home)
    (home/'plan.json').write_text(json.dumps(dict(command=command, identity=identity,
        input_control=False, audio_backend='OpenAL null'), indent=2))
    startup = None
    if os.name == 'nt':
        startup = subprocess.STARTUPINFO()
        startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        startup.wShowWindow = subprocess.SW_HIDE
    env = {**os.environ, 'ALSOFT_DRIVERS': 'null', 'ALSOFT_LOGFILE': str(home/'openal.log')}
    logpath = game/'logs'/LOG_NAME
    started = time.monotonic()
    with (home/'stdout.log').open('w') as output:
        process = subprocess.Popen(command, cwd=runtime, env=env, stdin=subprocess.DEVNULL,
            stdout=output, stderr=subprocess.STDOUT, startupinfo=startup)
        while process.poll() is None and time.monotonic()-started < args.timeout:
            time.sleep(.5)
            log = logpath.read_text(errors='replace') if logpath.exists() else ''
            if re.search(r'ERROR:|Compile failed|script runtime error|Assertion failed', log):
                break
        stop_process(process)
    log = logpath.read_text(errors='replace') if logpath.exists() else ''
    failures = re.findall(r'(?mi)^.*(?:ERROR:|Compile failed|script runtime error|Assertion failed|Unknown command).*$', log)
    if process.returncode != 0 or 'ALARM_DONE' not in log:
        failures.append(f'incomplete run / exit {process.returncode}')
    samples = {}
    for label, block in re.findall(r'(?m)^ALARM_(\w+)\s*\n(.*?)^ALARM_SAMPLE_END', log, re.S):
        samples[label] = dict(channels=[fields(line) for line in re.findall(r'(?m)^SOUND_DISTANCE (.+)$', block)],
            backend=[fields(line) for line in re.findall(r'(?m)^SOUND_DISTANCE_BACKEND (.+)$', block)])
    def audible(label):
        return any(row.get('playing') == '1' and float(row.get('gain', '0')) > 0
                   for row in samples.get(label, {}).get('backend', []))
    if args.mode == 'baseline':
        if not audible('BASELINE'):
            failures.append('baseline alarm is not actually playing')
    else:
        for label in (('RESTART',) if args.mode == 'gap' else ('ON', 'RESTART')):
            if not audible(label):
                failures.append(label+' is not actually playing')
        for label in ('INITIAL_OFF', 'RESTORED_OFF', 'STILL_OFF', 'RESTORED_GAP', 'GAP', 'STOP',
                      'STOP_PERSISTS', 'CONSOLE_OFF', 'CONSOLE_STAYS_OFF', 'DISTANT'):
            if label in samples and audible(label):
                failures.append(label+' should be silent')
        if not any(audible(label) for label in samples if label.startswith('REPEAT_')):
            failures.append('no reminder pulse')
        computer_labels = ['COMPUTER_'+name for name, _, _ in COMPUTERS] if args.mode == 'fresh' else []
        for label in ('ON', 'RESTART', 'DISTANT', *computer_labels):
            for row in samples.get(label, {}).get('channels', []):
                # LOOPING=32 and GLOBAL=8 in the engine's sound.h.
                if int(row['flags']) & (8 | 32):
                    failures.append(label+' is global or looping')
                expected_gain = .125 if label.startswith('COMPUTER') else .25
                if abs(float(row['authoredGain']) - expected_gain) > .0001:
                    failures.append(label+' has the wrong authored gain')
        if args.mode == 'fresh':
            for name, _, _ in COMPUTERS:
                if not audible('COMPUTER_'+name):
                    failures.append(name+' does not play nearby')
                if audible('COMPUTER_GAP_'+name):
                    failures.append(name+' should have finished its one-shot')
        if '"oq4_prisonAlarm"' not in log:
            failures.append('repaired speaker args missing')
    if any(sha256(runtime/name) != value for name, value in identity.items()):
        failures.append('runtime replaced during probe')
    result = dict(passed=not failures, failures=failures, mode=args.mode,
                  seconds=round(time.monotonic()-started, 2), identity=identity, samples=samples)
    (home/'result.json').write_text(json.dumps(result, indent=2))
    print(json.dumps({key: result[key] for key in ('passed', 'failures', 'mode', 'seconds')}))
    return 0 if result['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
