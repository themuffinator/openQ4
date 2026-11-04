#!/usr/bin/env python3
"""Run hangar1's authored Viper Squad lift in a hidden, input-free client.

The probe positions the squad at its boarding spots, then calls the stock
boarding sequence and lift trigger. Console script diagnostics are intentionally
not saved: the developer console's script source cannot be recompiled on load.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import re
import subprocess
import sys
import time

import savegame_cross_load as cross


def commands():
    lines = ['set g_autoExecAfterMapLoad ""', 'god', 'notarget', 'set g_debugState elgar',
             'trigger ai_tether_none_2']
    for name, spot in (('elgar', 'elevA_elgarSpot_1'), ('puccini', 'elevA_pucciniSpot_1'), ('mahler', 'elevA_mahlerSpot')):
        lines += [f'script ${name}.show()', f'script ${name}.setOrigin(${spot}.getWorldOrigin())']
    lines += ['script map_hangar1::viperToLift()', 'wait 600', 'echo Q4_LIFT_BOARDED',
              'script sys.println(map_hangar1::viperAtElev)',
              'teleport trigger_playerOnElevator']
    for label, frames in (('START', 1), ('MOVING', 300), ('EXIT', 1200), ('FOLLOW', 900)):
        lines += [f'wait {frames}', f'echo Q4_LIFT_{label}',
                  'script sys.println($elevA.getOrigin())',
                  'script sys.println($elgar.getOrigin())',
                  'script sys.println($elgar.scriptedDone())']
    return [('script "' + line[7:] + '"') if line.startswith('script ') else line for line in lines] + ['echo Q4_LIFT_DONE', 'quit', '']


def inspect(text):
    readings = {}
    for label in ('BOARDED', 'START', 'MOVING', 'EXIT', 'FOLLOW'):
        match = re.search(r'Q4_LIFT_'+label+r'[ \t]*\r?\n(.*?)(?=Q4_LIFT_|\Z)', text, re.S)
        readings[label] = match[1].splitlines()[:12] if match else []
    return readings


def assess(text, expect_stall=False):
    readings = inspect(text)
    checks = dict(boarded=False, lift_finished=False, released=False, reached_tether=False,
                  clean='Error:' not in text and 'ERROR:' not in text and 'scriptedMove:' not in text,
                  finished=text.count('Q4_LIFT_DONE') == 1)
    try:
        checks['boarded'] = int(readings['BOARDED'][0]) == 3
        start = [float(value) for value in readings['START'][0].split()]
        end = [float(value) for value in readings['FOLLOW'][0].split()]
        actor = [float(value) for value in readings['FOLLOW'][1].split()]
        done = int(readings['FOLLOW'][2])
        assert len(start) == len(end) == len(actor) == 3
        checks['lift_finished'] = abs(end[2]-start[2]-480) < 1
        checks['released'] = done == 1
        checks['reached_tether'] = math.dist(actor[:2], (-928, -528)) < 72 and abs(actor[2]+5472) < 2
    except (ValueError, IndexError, AssertionError):
        pass
    expected = (checks['boarded'] and checks['lift_finished'] and checks['clean'] and checks['finished']
                and not checks['released'] and not checks['reached_tether']) if expect_stall else all(checks.values())
    return checks, expected


def self_test():
    text = ('Q4_LIFT_BOARDED \n3\nQ4_LIFT_START \n-352 -416 -5968\n-272 -416 -5952\n0\n'
            'Q4_LIFT_FOLLOW \n-352 -416 -5488\n-873 -530 -5472\n1\nQ4_LIFT_DONE \n')
    assert assess(text)[1]
    for bad in (text.replace('\n3\n', '\n2\n'), text.replace('-5488', '-5968'),
                text.replace('\n1\n', '\n0\n'), text.replace('-873', '-272'),
                text.replace('Q4_LIFT_DONE', 'MISSING'), text+'Error: script failed\n'):
        assert not assess(bad)[1]
    stuck = text.replace('-873 -530 -5472\n1', '-272 -416 -5472\n0')
    assert assess(stuck, True)[1] and not assess(text, True)[1]
    assert not assess('Q4_LIFT_DONE', True)[1]
    assert 'script "$elgar.show()"' in commands()
    print('turbo_lift_runtime: parser and negative-control self-test passed')
    return 0


def main():
    if sys.argv[1:] == ['--self-test']:
        return self_test()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--install-root', type=Path, required=True)
    parser.add_argument('--basepath', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--renderer', choices=('gl', 'vulkan'), default='gl')
    parser.add_argument('--turbo', choices=('0', '1'), default='1')
    parser.add_argument('--expect-stall', action='store_true', help='Negative control for a separate runtime with the original Turbo mover behaviour.')
    parser.add_argument('--timeout', type=int, default=240)
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    leg = cross.parse_args(['write', '--install-root', str(args.install_root.resolve()),
        '--basepath', str(args.basepath.resolve()), '--home', str(output), '--map', 'game/hangar1', '--tag', 'unused',
        '--set', 'r_renderApi', args.renderer, '--set', 'g_turboMode', args.turbo])
    game = output / 'baseoq4'
    cfg = game / cross.CFG_REL
    cfg.parent.mkdir(parents=True, exist_ok=True)
    cfg.write_text('\n'.join(commands()), encoding='utf-8')
    client = cross.client_path(leg.install_root)
    command = cross.launch_command(leg, client, output)
    startup = None
    if sys.platform == 'win32':
        startup = subprocess.STARTUPINFO()
        startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        startup.wShowWindow = subprocess.SW_HIDE
    begin = time.monotonic()
    timed_out = False
    with (output / 'stdout.log').open('w') as stream:
        process = subprocess.Popen(command, cwd=client.parent, stdin=subprocess.DEVNULL,
            stdout=stream, stderr=subprocess.STDOUT, startupinfo=startup, start_new_session=sys.platform != 'win32')
        try:
            process.wait(timeout=args.timeout)
        except subprocess.TimeoutExpired:
            timed_out = True
        finally:
            cross.stop_process(process)
    log = game / 'logs' / cross.LOG_NAME
    text = log.read_text(errors='replace') if log.is_file() else ''
    checks, expected = assess(text, args.expect_stall)
    report = dict(command=command, renderer=args.renderer, turbo=args.turbo, timed_out=timed_out,
        exit_code=process.returncode, seconds=round(time.monotonic()-begin, 1), readings=inspect(text),
        expect_stall=args.expect_stall, checks=checks,
        passed=expected and not timed_out and process.returncode == 0,
        runtime={str(path.relative_to(client.parent)): hashlib.sha256(path.read_bytes()).hexdigest()
                 for path in (client, *sorted((client.parent/'baseoq4').glob('game-*.*')))
                 if path.suffix.lower() in ('.exe', '.dll', '.so', '.dylib')},
        log_sha256=hashlib.sha256(log.read_bytes()).hexdigest() if log.is_file() else None)
    (output / 'report.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps({key: report[key] for key in ('passed', 'checks', 'seconds', 'readings')}, indent=2))
    return int(not report['passed'])


if __name__ == '__main__':
    raise SystemExit(main())
