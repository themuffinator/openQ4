#!/usr/bin/env python3
"""Profile moving retained scopes over SP gameplay without native input.

Uses the stable staged client, private saves, a hidden window and engine
screenshots. --output must be a new directory below the project's .tmp.
Frame intervals include the whole game; the native --scope benchmark isolates
the UI CPU cost. Hidden-window presentation is not a display latency test.
"""
import argparse
import json
import math
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/ui'))
import capture_retained_screens as capture


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--assets', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--renderer', choices=('gl', 'vulkan'), default='gl')
    parser.add_argument('--size', default='1920x1080')
    parser.add_argument('--frames', type=int, default=180)
    args = parser.parse_args()
    args.output = args.output.resolve()
    if args.output.exists() or not args.output.is_relative_to((ROOT / '.tmp').resolve()):
        parser.error('choose a new output directory under .tmp')
    if not 60 <= args.frames <= 600:
        parser.error('--frames must be 60..600')
    args.runtime = ROOT / '.install'
    args.gate = '1'
    args.language = 'english'
    args.timeout = 240
    setattr(args, 'set', ['com_gameMode=SP', 'r_swapInterval=0', 'com_maxfps=0'])
    stages = ['god', 'notarget', 'set ui_testGuiInGame 1']
    for kind in ('machinegun', 'railgun', 'nailgun'):
        stages += [f'testGUI guis/weapons/{kind}_scope.q4ui', 'waitMsec 1500',
                   f'echo SCOPE_PROFILE {kind}', f'ui_retainedProfile {args.frames}']
        for frame in range(args.frames + 2):
            stages += [f'openq4_retainedGui state playerYaw {frame * .7 % 360:.3f}']
            if kind == 'nailgun':
                stages += [f'openq4_retainedGui pending lockYaw {frame * 1.3 % 360:.3f}',
                           f'openq4_retainedGui state lockStatus {.5 + .5 * math.sin(frame * .03):.5f}']
            stages += ['wait 1']
        stages += [f'screenshot "screenshots/{kind}-moving.tga"']
        if kind == 'nailgun':
            stages += ['openq4_retainedGui pending lockRange 12.4',
                       'openq4_retainedGui state lockTime 2.0',
                       'openq4_retainedGui event lockAquired', 'waitMsec 150',
                       'screenshot "screenshots/nailgun-locked.tga"',
                       'openq4_retainedGui event lockStop', 'waitMsec 400']
    # Also verify that the real weapon selects the retained scope in gameplay.
    # The weapon script owns held zoom; this probe does not synthesize input.
    stages += ['testGUI', 'give weapon_railgun', 'waitMsec 1000',
               'testWeaponScope weapon_railgun', 'waitMsec 1500',
               'testWeaponScope', 'screenshot "screenshots/railgun-selected.tga"']
    capture.SCENARIOS['scope-performance'] = ('game/airdefense1', stages)
    runtime_files = ('openQ4-client_x64.exe', 'baseoq4/game-sp_x64.dll', 'baseoq4/pak0.pk4',
                     f'renderer-{"gl" if args.renderer == "gl" else "vk"}_x64.dll')
    identity = {name: capture.digest(args.runtime / name) for name in runtime_files}
    result = capture.run(args, 'scope-performance')
    result['runtime_sha256'] = identity
    result['runtime_unchanged'] = identity == {name: capture.digest(args.runtime / name) for name in runtime_files}
    log = (ROOT / result['log']).read_text(encoding='utf-8', errors='replace')
    result['profiles'] = [json.loads(value) for value in
                          re.findall(r'Retained UI root profile: (\{[^\n]+\})', log)]
    result['vector_work'] = [json.loads(value) for value in
                            re.findall(r'Retained UI vector work: (\{[^\n]+\})', log)]
    result['document_work'] = re.findall(r'Retained UI document work: [^\n]+', log)
    result['passed'] = (result['complete'] and result['returncode'] == 0 and result['runtime_unchanged'] and
                        not result['errors'] and len(result['profiles']) == 3 and
                        'WEAPON_SCOPE gui=guis/weapons/railgun_scope.q4ui' in log and
                        'frame geometry ring overflow' not in log and
                        not result['retained_warnings'])
    (args.output / 'report.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({key: result[key] for key in ('passed', 'profiles', 'errors', 'retained_warnings')}))
    return 0 if result['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
