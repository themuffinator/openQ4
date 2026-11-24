#!/usr/bin/env python3
"""Exercise multiplayer objective markers with engine screenshots and no device input.

Uses installed stock maps, the staged runtime, hidden windowed clients and private
profiles below .tmp. Native projection sweeps complement these gameplay captures.
"""
from __future__ import annotations
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import sys

import capture_retained_screens as capture

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/tests'))
from mp_round_remote_smoke import verify_runtime_is_current


def shot(name):
    return f'screenshot "screenshots/{name}.tga"'


BASE = ['set g_autoExecAfterMapLoad ""', 'set developer 1', 'addbot', 'waitMsec 5000',
        'openq4_assertMPClientActive', 'openq4_reportMPState']
CTF = BASE + [
    'setviewpos -2176 400 216 60', 'waitMsec 900', shot('bases'),
    'setviewpos -2176 400 216 -120', 'waitMsec 500', shot('behind'),
    'setviewpos -2176 400 216 60', 'set hud_objectiveMarkers 0', 'waitMsec 300', shot('disabled'),
    'set hud_objectiveMarkers 1', 'set ui_retained 0', 'waitMsec 300', shot('stock'),
    'set ui_retained 1', 'set ui_hudScale 1.5', 'set ui_hudSafeInset 10', 'waitMsec 300', shot('scaled'),
    'set ui_hudScale 1', 'set ui_hudSafeInset 0',
    'teleport mp_ctf_strogg_flag_1', 'waitMsec 700',
    'setviewpos -2176 400 216 60', 'waitMsec 500', shot('taken'),
    'ui_hudTest guis/mphud.gui get oq4_flag1_state',
    'testScoreboard 1200', 'waitMsec 300', shot('scoreboard'), 'waitMsec 1200',
    'set g_showHud 0', 'waitMsec 200', shot('hidden-hud'), 'set g_showHud 1',
    'setviewpos -3104 -128 264 60', 'waitMsec 200', 'kill 0', 'waitMsec 200', shot('dead'), 'waitMsec 14000',
    'openq4_assertMPClientActive', 'setviewpos -2176 400 216 60', 'waitMsec 500', shot('dropped'),
    'ui_hudTest guis/mphud.gui get oq4_flag1_state',
]
DZ = BASE + [
    'setviewpos -200 332 340 25', 'waitMsec 900', shot('zone'),
    'setviewpos -200 332 340 -160', 'waitMsec 500', shot('zone-behind'),
    'setviewpos -200 332 340 25', 'set ui_retained 0', 'waitMsec 300', shot('zone-stock'),
    'set ui_retained 1', 'set hud_objectiveMarkers 0', 'waitMsec 300', shot('zone-disabled'),
    'set hud_objectiveMarkers 1',
    *[command for number in range(3,7) for command in (f'teleport powerup_deadzone_{number}', 'waitMsec 400')],
    'setviewpos 272 332 340 25', 'waitMsec 1200', shot('zone-control'),
    'ui_hudTest guis/mphud.gui get oq4_zone',
]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--assets', type=Path, required=True)
    parser.add_argument('--runtime', type=Path, default=ROOT / '.install')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--renderer', choices=('gl', 'vulkan'), default='gl')
    parser.add_argument('--size', default='1280x720')
    parser.add_argument('--language', default='english')
    parser.add_argument('--scenario', action='append', choices=('ctf', 'dz', 'ctf-remote', 'oneflag', 'dm'))
    parser.add_argument('--timeout', type=int, default=180)
    args = parser.parse_args()
    args.output = args.output.resolve()
    if args.output.exists() or not args.output.is_relative_to((ROOT / '.tmp').resolve()):
        parser.error('use a fresh output under .tmp')
    verify_runtime_is_current(args.runtime)
    args.gate, args.set = '1', []
    modes = {'ctf': 'CTF', 'ctf-remote': 'CTF', 'oneflag': 'One Flag CTF', 'dz': 'DeadZone', 'dm': 'DM'}
    scenarios = {
        'ctf': ('mp:mp/q4ctf1', CTF), 'dz': ('mp:mp/q4dz1', DZ),
        'oneflag': ('mp:mp/q4ctf1', BASE + ['setviewpos -2176 400 216 60', 'waitMsec 900', shot('oneflag')]),
        'dm': ('mp:mp/q4dm1', BASE + ['waitMsec 900', shot('no-objectives')]),
        'ctf-remote': (None, ['connect 127.0.0.1:28961', capture.AFTER_MAP] + [s for s in BASE if s != 'addbot'] +
                       ['waitMsec 900', shot('remote-bases'), 'ui_hudTest guis/mphud.gui get oq4_flag1_state']),
    }
    results = []
    for name in args.scenario or ['ctf', 'dz']:
        scenario = 'mp-poi-' + name
        capture.SCENARIOS[scenario] = scenarios[name]
        capture.SCENARIO_CVARS[scenario] = {
            'com_gameMode': 'MP', 'com_nextGameModule': 'game_mp', 'si_gameType': modes[name],
            'ui_team': 'Marine', 'ui_ready': 'Ready', 'ui_autoJoin': '1', 'ui_spectate': 'Play',
            'si_minPlayers': '2', 'si_warmup': '0', 'si_countDown': '4', 'si_timeLimit': '0', 'si_captureLimit': '5',
            'bot_pause': '1',
            'si_controlTime': '120', 'bot_minPlayers': '0', 'net_allowCheats': '1',
            'ui_hudLayout': '1', 'ui_hudScale': '1', 'ui_hudSafeInset': '0',
            'win_allowMultipleInstances' if os.name == 'nt' else 'sys_allowMultipleInstances': '1',
        }
        if name == 'ctf-remote':
            capture.SCENARIO_CVARS[scenario]['net_port'] = '28962'
            capture.SCENARIO_SERVERS[scenario] = {'map': 'mp/q4ctf1', 'port': 28961, 'name': 'POI capture',
                'cvars': {'si_gameType': 'CTF', 'si_minPlayers': '2', 'si_warmup': '0',
                          'net_allowCheats': '1', 'bot_minPlayers': '2', 'bot_pause': '1'}}
        result = capture.run(args, scenario)
        for screenshot in result['screenshots']:
            with (ROOT / screenshot['tga']).open('rb') as image:
                header = image.read(18)
            screenshot['size'] = [int.from_bytes(header[12:14], 'little'), int.from_bytes(header[14:16], 'little')]
        log = (ROOT / result['log']).read_text(encoding='utf-8', errors='replace') if result['log'] else ''
        traces = re.findall(r'HUD_COVERAGE source=native/objective-markers[^\r\n]*', log)
        result['objective_marker_traces'] = traces
        result['expected_screenshots'] = sum(s.startswith('screenshot ') for s in scenarios[name][1])
        result['passed'] = (result['complete'] and result['returncode'] == 0 and not result['errors'] and
                            len(result['screenshots']) == result['expected_screenshots'] and
                            'OPENQ4_STOCK_BASELINE_MP_CLIENT_ACTIVE' in log and
                            (not traces if name == 'dm' else bool(traces)) and not result['retained_warnings'])
        if name == 'ctf':
            result['passed'] &= all(f'key=oq4_flag1_state value={value}' in log for value in (1, 2))
        if name == 'dz':
            result['passed'] &= 'key=oq4_zone value=1' in log
        result['passed'] &= bool(re.search(r'MP_STATE .* phase=3 ', log))
        results.append(result)
        print(name, 'PASS' if result['passed'] else 'FAIL', len(result['screenshots']), 'screenshots', flush=True)
        payload = {'source': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
                   'working_tree': subprocess.check_output(['git', 'status', '--short'], cwd=ROOT, text=True),
                   'runtime': {str(path.relative_to(args.runtime)): capture.digest(path)
                               for path in [args.runtime / ('openQ4-client_x64.exe' if os.name == 'nt' else 'openQ4-client_x64'),
                                            *sorted((args.runtime / 'baseoq4').glob('game_*'))]
                               if path.is_file()}, 'results': results}
        (args.output / 'report.json').write_text(json.dumps(payload, indent=2) + '\n', encoding='utf-8')
    return 0 if all(result['passed'] for result in results) else 1


if __name__ == '__main__':
    raise SystemExit(main())
