#!/usr/bin/env python3
"""Validate CTF/DeadZone POI using the simulated OpenXR runtime, without a headset.

Uses the stable staged client, installed maps, a hidden window and private .tmp
profiles. No controller actions or host input are injected. Head poses come from
the test runtime; all images use the engine's registered screenshot command.
This validates submission and placement, not headset optics or comfort.
"""
from __future__ import annotations
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import sys

from PIL import Image, ImageChops

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/ui'))
import capture_retained_screens as capture
from capture_objective_markers import BASE
from mp_round_remote_smoke import verify_runtime_is_current
from openxr_vr_mp_smoke import read_events


def pair(name: str, flat: bool = False) -> list[str]:
    target = '' if flat else 'image _vrScreenColor '
    return [f'echo POI_VR_{name}', 'vr_status', 'set hud_objectiveMarkers 1', 'waitMsec 350',
            f'screenshot {target}"screenshots/{name}-on.tga"',
            'set hud_objectiveMarkers 0', 'waitMsec 350',
            f'screenshot {target}"screenshots/{name}-off.tga"', 'set hud_objectiveMarkers 1']


def difference(game: Path, name: str, right_edge: bool = False) -> dict:
    with Image.open(game / f'screenshots/{name}-on.tga') as on, \
            Image.open(game / f'screenshots/{name}-off.tga') as off:
        # Ignore the timer, team panels and lower inventory. The HUD layer has
        # no world animation; changes in this band are the objective symbols.
        delta = ImageChops.difference(on.convert('RGB'), off.convert('RGB')).convert('L')
        bounds = (int(delta.width * .9), int(delta.height * .28), delta.width, int(delta.height * .85)) if right_edge else \
                 (0, int(delta.height * .14), delta.width, int(delta.height * .87))
        band = delta.crop(bounds)
        mask = band.point(lambda value: 255 if value > 12 else 0)
        return {'pixels': mask.histogram()[255], 'bounds_in_band': mask.getbbox(), 'size': list(on.size)}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--assets', type=Path, required=True)
    parser.add_argument('--runtime', type=Path, default=ROOT / '.install')
    parser.add_argument('--build-dir', type=Path, default=ROOT / 'builddir')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--size', default='1280x720')
    parser.add_argument('--scenario', action='append', choices=('ctf', 'dz'))
    parser.add_argument('--timeout', type=int, default=240)
    args = parser.parse_args()
    args.output = args.output.resolve()
    if args.output.exists() or not args.output.is_relative_to((ROOT / '.tmp').resolve()):
        parser.error('use a fresh output under .tmp')
    manifest = (args.build_dir / 'openq4_xr_test_runtime.json').resolve()
    if not manifest.is_file():
        parser.error('build the OpenXR test runtime first')
    verify_runtime_is_current(args.runtime)
    args.renderer, args.gate, args.language, args.set = 'gl', '1', 'english', []
    results = []
    for mode in args.scenario or ['ctf', 'dz']:
        scenario = f'vr-poi-{mode}'
        profile = args.output / scenario
        profile.mkdir(parents=True)
        game = profile / 'save/baseoq4'
        xr_script, xr_log = profile / 'xr-script.txt', profile / 'xr.jsonl'
        script = []

        def pose(name: str, command: str) -> list[str]:
            marker = f'vr_marker_{name}.txt'
            # Relative to the game's explicit working directory, so a parent
            # checkout path containing spaces still works with the runtime's
            # whitespace-delimited script format.
            marker_path = os.path.relpath(game / marker, args.runtime.resolve()).replace('\\', '/')
            if any(char.isspace() for char in marker_path):
                parser.error('the test runtime requires an output path without spaces relative to --runtime')
            script.append(f'when {marker_path} {command}')
            return [f'condump {marker}', 'waitMsec 700']

        view = 'setviewpos -2176 400 216 80' if mode == 'ctf' else 'setviewpos -200 332 340 0'
        names = ['base', 'look', 'lean', 'geometry', 'head-aim', 'behind', 'stock-reference', 'stock', 'status', 'fallback']
        # Stay below the engine's 64 startup-command limit; presentation-only
        # settings can be applied after loading the map.
        settings = {'ui_hudLayout': '1', 'ui_hudScale': '1', 'ui_hudSafeInset': '0',
                    'vr_debug': '1', 'vr_aimMode': '1', 'vr_roomScale': '0', 'vr_physicalCrouch': '0',
                    'vr_hudDistance': '1.4', 'vr_hudWidth': '1.2', 'vr_hudHeightOffset': '-0.1'}
        stages = [f'set {key} {value}' for key, value in settings.items()] + BASE + [view, 'waitMsec 900'] + pair('base')
        stages += ['screenshot image _vrEyeColor0 "screenshots/left-eye.tga"',
                   'screenshot image _vrEyeColor1 "screenshots/right-eye.tga"']
        stages += pose('look', 'head 20 -8 6') + pair('look')
        stages += pose('lean', 'head 20 -8 6 0.12 1.5 -0.1') + pair('lean')
        # The runtime samples layer metadata every 90 frames: keep each quad
        # geometry for more than one sample interval before taking its pair.
        stages += ['set vr_hudWidth 1.8', 'set vr_hudDistance 2', 'set vr_hudHeightOffset -0.18', 'waitMsec 1200'] + pair('geometry')
        stages += ['set vr_aimMode 0', 'waitMsec 1200'] + pair('head-aim')
        stages += pose('behind', 'head 175 0 0 0 1.6 0') + pair('behind')
        stages += pose('home', 'head 0 0 0 0 1.6 0')
        stages += ['set vr_aimMode 1', 'set vr_hudWidth 1.2', 'set vr_hudDistance 1.4',
                   'set vr_hudHeightOffset -0.1'] + pair('stock-reference')
        stages += ['set ui_retained 0'] + pair('stock') + ['set ui_retained 1']
        if mode == 'ctf':
            stages += ['teleport mp_ctf_strogg_flag_1', 'waitMsec 700', view, 'waitMsec 900',
                       'ui_hudTest guis/mphud.gui get oq4_flag1_state']
        else:
            stages += [step for number in range(3, 7) for step in
                       (f'teleport powerup_deadzone_{number}', 'waitMsec 400')]
            stages += ['setviewpos 272 332 340 0', 'waitMsec 1200', 'ui_hudTest guis/mphud.gui get oq4_zone']
        stages += pair('status')
        # Runtime-requested exit leaves vr_enable=1: the ordinary projection
        # must resume. A requested-but-inactive VR mode cannot suppress POI.
        stages += pose('exit', 'exit') + ['waitMsec 1000', view, 'waitMsec 700', 'vr_enable'] + pair('fallback', True)
        xr_script.write_text('\n'.join(script) + '\n', encoding='utf-8')
        capture.SCENARIOS[scenario] = ('mp:mp/q4ctf1' if mode == 'ctf' else 'mp:mp/q4dz1', stages)
        capture.SCENARIO_CVARS[scenario] = {
            'com_gameMode': 'MP', 'com_nextGameModule': 'game_mp', 'si_gameType': 'CTF' if mode == 'ctf' else 'DeadZone',
            'ui_team': 'Marine', 'ui_ready': 'Ready', 'ui_autoJoin': '1', 'ui_spectate': 'Play',
            'si_minPlayers': '2', 'si_warmup': '0', 'si_countDown': '4', 'si_timeLimit': '0', 'si_captureLimit': '5',
            'si_controlTime': '120', 'bot_pause': '1', 'bot_minPlayers': '0', 'net_allowCheats': '1', 'net_ip': '127.0.0.1',
            'vr_enable': '1',
            'win_allowMultipleInstances' if os.name == 'nt' else 'sys_allowMultipleInstances': '1',
        }
        environment = {'XR_RUNTIME_JSON': str(manifest), 'OPENQ4_XR_TEST_LOG': str(xr_log),
                       'OPENQ4_XR_TEST_SCRIPT': str(xr_script), 'OPENQ4_XR_TEST_EYE_SIZE': '1200x1320',
                       'OPENQ4_XR_TEST_RATE': '90', 'OPENQ4_XR_TEST_NO_INPUT': '1'}
        previous = {key: os.environ.get(key) for key in environment}
        try:
            os.environ.update(environment)
            result = capture.run(args, scenario)
        finally:
            for key, value in previous.items():
                if value is None:
                    os.environ.pop(key, None)
                else:
                    os.environ[key] = value
        events = read_events(xr_log)
        frames = [event for event in events if event.get('event') == 'end_frame' and
                  [layer['type'] for layer in event['layers']] == ['projection', 'quad']]
        quads = [frame['layers'][1] for frame in frames]
        log = (game / 'logs/openq4.log').read_text(encoding='utf-8', errors='replace')
        diffs = {name: difference(game, name) for name in names
                 if (game / f'screenshots/{name}-on.tga').is_file() and (game / f'screenshots/{name}-off.tga').is_file()}
        result['differences'] = diffs
        # Both modes put a stationary marker at the right edge here. Check
        # its complete footprint, without timer/chat/health animation: fills
        # and text alone must not pass when the stock renderer culls strokes.
        parity = {name: difference(game, name, True) for name in ('stock-reference', 'stock') if name in diffs}
        result['stock_parity'] = parity
        parity_ok = len(parity) == 2 and parity['stock-reference']['pixels'] > 0 and \
                    .65 < parity['stock']['pixels'] / parity['stock-reference']['pixels'] < 1.5
        result['stereo_hud_frames'] = len(frames)
        result['quad_shapes'] = sorted({(q['width'], q['height'], q['y'], q['z']) for q in quads})
        result['xr_errors'] = [event for event in events if event.get('event') in ('end_frame_invalid', 'script_error')]
        result['tracked_script_commands'] = [event for event in events if event.get('event') == 'script_command']
        result['environment'] = environment
        status = 'key=oq4_flag1_state value=1' if mode == 'ctf' else 'key=oq4_zone value=1'
        result['passed'] = bool(result['complete'] and result['returncode'] == 0 and not result['errors'] and
                                not result['xr_errors'] and not result['retained_warnings'] and frames and status in log and
                                'more than 64 startup commands' not in log and
                                len(result['screenshots']) == 22 and len(diffs) == len(names) and parity_ok and
                                all(diff['pixels'] > 30 for diff in diffs.values()) and
                                'OPENQ4_STOCK_BASELINE_MP_CLIENT_ACTIVE' in log and re.search(r'MP_STATE .* phase=3 ', log) and
                                'OpenXR: no instance' in log.partition('POI_VR_fallback')[2] and
                                '"vr_enable" is:"1"' in log and
                                any(abs(q['width'] - 1.8) < .001 and abs(q['z'] + 2) < .001 and abs(q['y'] + .18) < .001 for q in quads) and
                                any(abs(q['width'] - 1.8) < .001 and q['y'] == 0 for q in quads))
        results.append(result)
        payload = {'source': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
                   'working_tree': subprocess.check_output(['git', 'status', '--short'], cwd=ROOT, text=True),
                   'runtime': {str(path.relative_to(args.runtime)): capture.digest(path)
                               for path in [args.runtime / ('openQ4-client_x64.exe' if os.name == 'nt' else 'openQ4-client_x64'),
                                            *sorted((args.runtime / 'baseoq4').glob('game_*'))] if path.is_file()},
                   'results': results}
        (args.output / 'report.json').write_text(json.dumps(payload, indent=2) + '\n', encoding='utf-8')
        print(mode, 'PASS' if result['passed'] else 'FAIL', len(frames), 'stereo HUD frames;',
              {name: diff['pixels'] for name, diff in diffs.items()}, flush=True)
    return 0 if all(result['passed'] for result in results) else 1


if __name__ == '__main__':
    raise SystemExit(main())
