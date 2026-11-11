#!/usr/bin/env python3
"""Capture legacy scope and message backing over SP gameplay, then retained UI.

Uses a hidden window, no host input, and the engine screenshot command only.
The aperture check rejects an opaque scope even when its GUI loads correctly.
"""
import argparse
import json
from pathlib import Path

import capture_retained_screens as capture

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--assets', type=Path, required=True)
    parser.add_argument('--runtime', type=Path, default=ROOT / '.install')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--renderer', choices=('gl', 'vulkan'), default='gl')
    parser.add_argument('--size', default='1280x720')
    args = parser.parse_args()
    args.output = args.output.resolve()
    if args.output.exists() or not args.output.is_relative_to((ROOT / '.tmp').resolve()):
        parser.error('choose a new output directory under the project .tmp')
    args.gate = '0'
    args.language = 'english'
    args.timeout = 240
    stages = [
        'god', 'notarget', 'set ui_testGuiInGame 1', 'waitMsec 1500',
        'set g_stopTime 1', 'waitMsec 300', 'screenshot "screenshots/scene.tga"',
        'testGUI guis/weapons/machinegun_scope.gui', 'waitMsec 500',
        'screenshot "screenshots/legacy-scope.tga"', 'testGUI',
        'testGUI guis/msg.gui', 'waitMsec 500',
        'screenshot "screenshots/legacy-message.tga"', 'testGUI',
        'set ui_retained 1', 'waitMsec 500',
        'testGUI guis/weapons/machinegun_scope.rui', 'waitMsec 500',
        'screenshot "screenshots/retained-scope.tga"', 'testGUI',
        'set ui_retained 0', 'waitMsec 300',
        'testGUI guis/weapons/machinegun_scope.gui', 'waitMsec 500',
        'screenshot "screenshots/legacy-return.tga"', 'testGUI',
    ]
    capture.SCENARIOS['legacy-ui'] = ('game/airdefense1', stages)
    capture.SCENARIO_CVARS['legacy-ui'] = {'com_gameMode': 'SP'}
    result = capture.run(args, 'legacy-ui')
    # Write diagnostics even when image assertions fail.
    report = args.output / 'report.json'
    report.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    assert result['complete'] and result['returncode'] == 0 and not result['errors'], result
    assert len(result['screenshots']) == 5, result['screenshots']

    from PIL import Image, ImageStat
    shots = args.output / 'legacy-ui/save/baseoq4/screenshots'
    metrics = {}
    for name in ('legacy-scope', 'retained-scope', 'legacy-return'):
        im = Image.open(shots / (name + '.tga')).convert('RGB')
        w, h = im.size
        # A narrow region inside the clear aperture. The opaque
        # regression has virtually no detail here; a scene has visible texture.
        canvas = min(w / 640.0, h / 480.0)
        patch = im.crop((int(w/2-32*canvas), int(h/2-120*canvas),
                         int(w/2+32*canvas), int(h/2+100*canvas)))
        metrics[name] = ImageStat.Stat(patch.convert('L')).stddev[0]
        assert metrics[name] > 3.0, ('scope hides scene', name, metrics[name])

    scene = Image.open(shots / 'scene.tga').convert('RGB')
    message = Image.open(shots / 'legacy-message.tga').convert('RGB')
    w, h = scene.size
    # Away from the centered glow column, both old-canvas and expanded margins
    # must receive the stock 94% black backing (4:3, wide and tall alike).
    for name, box in [('left', (.02, .22, .08, .34)), ('right', (.92, .22, .98, .34)),
                      ('top', (.18, .02, .24, .08)), ('bottom', (.76, .92, .82, .98))]:
        box = tuple(int(v * (w if i % 2 == 0 else h)) for i, v in enumerate(box))
        before = ImageStat.Stat(scene.crop(box).convert('L')).mean[0]
        after = ImageStat.Stat(message.crop(box).convert('L')).mean[0]
        metrics['message-' + name] = {'scene': before, 'dimmed': after}
        assert after < max(4.0, before * .20), ('message margin not dimmed', name, before, after)
    result['image_checks'] = metrics
    report.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print(f"legacy UI: {args.renderer} {args.size}, scope/retained/fallback and full-view message backing passed")


if __name__ == '__main__':
    main()
