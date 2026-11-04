#!/usr/bin/env python3
"""Qualify public retained binding names and text fitting in SP/MP gameplay.

Uses a hidden, windowed staged client and semantic engine commands, with no
device input or desktop capture. Screenshots come from the engine render target.
This is bounded UI-02/UI-09 evidence, not physical-device or full text acceptance.
"""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import re
import sys
import zipfile

import capture_retained_screens as capture
from capture_system_refresh import active_renderer

ROOT = capture.ROOT
sys.path.insert(0, str(ROOT / 'tools/tests'))
from mp_round_remote_smoke import verify_runtime_is_current

LABEL = 'controls_bind_0-value'


def activate(node):
    return [f'openq4_retainedGui focus "{node}"', 'openq4_retainedGui menu accept 1',
            'openq4_retainedGui menu accept 0', 'waitMsec 1200']


def observe(stage):
    return [f'echo TEXT_QUERY_STAGE {stage}', 'openq4_retainedGui report',
            f'openq4_retainedGui inspect "{LABEL}"',
            f'openq4_guiGet "{LABEL}::text"', f'openq4_retainedGui textfit "{LABEL}" "W"',
            f'openq4_retainedGui textfit "{LABEL}" "' + 'W' * 160 + '"',
            f'screenshot "screenshots/{stage}.tga"']


def steps(mode):
    opening = (['openq4_assertMenuActivation 10000 game', 'waitMsec 1500',
                'openq4_retainedGui event tab_settings', 'waitMsec 500', *activate('settings-mpSettingsControls')]
               if mode == 'mp' else ['openq4_assertMenuActivation 10000', 'waitMsec 1500',
                                    *activate('nav_settings'), *activate('controls')])
    closing = (['openq4_guiAction main_b_returnmp', 'waitMsec 1500', 'openq4_assertMPGameplayView']
               if mode == 'mp' else ['openq4_retainedGui event onBack', 'waitMsec 1200',
                                    'openq4_retainedGui event onBack', 'waitMsec 1500'])
    return ['waitMsec 1500', 'screenshot "screenshots/gameplay.tga"', *opening, *observe('initial'),
            *activate('controls_bind_0'), *observe('capture'),
            'testControlsKey f', 'waitMsec 400', *observe('bound'),
            'testControlsKey DEL', 'waitMsec 400', *observe('cleared'),
            'set ui_retainedTextScale 2', 'waitMsec 400', *observe('scaled'),
            'set ui_retainedTextScale 1', 'openq4_retainedGui event onBack', 'waitMsec 1200',
            *closing,
            'screenshot "screenshots/gameplay-return.tga"']


def qualify(log):
    sections = dict(re.findall(r'^TEXT_QUERY_STAGE (\S+)\s*\n(.*?)(?=^TEXT_QUERY_STAGE |\Z)', log, re.M | re.S))
    checks, labels, fits, widths = {}, {}, {}, {}
    for stage in ('initial', 'capture', 'bound', 'cleared', 'scaled'):
        section = sections.get(stage, '')
        label = re.search(r'^GUI_VALUE ' + re.escape(LABEL) + r'::text=(.*)$', section, re.M)
        labels[stage] = label.group(1).strip() if label else ''
        rows = re.findall(r'^RETAINED_GUI_TEXT_FIT id=' + re.escape(LABEL) +
                          r' available=(\d) overflow=(\d) maxIndex=(-?\d+) lastWhitespace=(-?\d+)', section, re.M)
        fits[stage] = [tuple(map(int, row)) for row in rows]
        box = re.search(r'^RETAINED_GUI_NODE id=' + re.escape(LABEL) + r' bounds=[^,]+,[^,]+,([^,]+),', section, re.M)
        widths[stage] = float(box.group(1)) if box else 0
        checks[stage] = len(rows) == 2 and fits[stage][0] == (1, 0, -1, -1) and \
            fits[stage][1][0:2] == (1, 1) and 0 < fits[stage][1][2] < 160
    checks['capture-label'] = bool(labels['initial']) and labels['capture'] != labels['initial']
    checks['assigned-key'] = 'F' in labels['bound'] and 'testControlsKey f taken=1' in log
    checks['cleared-key'] = bool(labels['cleared']) and labels['cleared'] != labels['bound'] and 'testControlsKey DEL taken=1' in log
    # At 200% the two-column page becomes one wider column: both width and
    # type can double, leaving the same byte count. Use the measured width
    # ratio rather than assuming the value column is fixed. Half a scalar
    # centres each integer fit interval; allow one glyph for pixel rounding.
    checks['scaled-query'] = checks['cleared'] and checks['scaled'] and widths['cleared'] > 0 and widths['scaled'] > 0 and \
        abs((fits['scaled'][1][2] + .5) * 2 -
            (fits['cleared'][1][2] + .5) * widths['scaled'] / widths['cleared']) <= 2
    checks['semantic-commands'] = not re.search(r'RETAINED_GUI_OPERATION .* failed|openq4_retainedGui: requires|openq4_guiGet: unknown|openq4_guiAction: .*no reachable', log)
    return checks, labels, fits


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--assets', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--renderer', choices=('gl', 'vulkan'), required=True)
    parser.add_argument('--mode', choices=('sp', 'mp'), required=True)
    parser.add_argument('--language', default='english')
    parser.add_argument('--size', default='1280x720')
    args = parser.parse_args()
    args.runtime, args.gate, args.timeout, args.set = ROOT / '.install', '1', 240, []
    args.output = args.output.resolve()
    if args.output.exists() or not args.output.is_relative_to((ROOT / '.tmp').resolve()):
        parser.error('choose a new output directory under the project .tmp')
    verify_runtime_is_current(args.runtime)
    args.output.mkdir(parents=True)
    binaries = [args.runtime / ('openQ4-client_x64.exe' if os.name == 'nt' else 'openQ4-client_x64'),
                *args.runtime.glob('renderer-*.dll'), *args.runtime.glob('renderer-*.so'),
                *args.runtime.glob('renderer-*.dylib'), *sorted((args.runtime / 'baseoq4').glob('game-*'))]
    binaries = [p for p in binaries if p.is_file()]
    identity = {str(p.relative_to(args.runtime)): capture.digest(p) for p in binaries}
    source_paths = ['src/ui/UserInterfaceRetained.cpp', 'src/ui/retained/Runtime.cpp', 'src/ui/retained/Runtime.h',
                    'src/ui/retained/Document.cpp', 'src/ui/retained/Document.h', 'src/framework/Session_menu.cpp',
                    'tools/ui/retained_controls.py', 'tools/ui/capture_text_queries.py',
                    'content/baseoq4/pak0/guis/menu/settings/controls.q4ui']
    sources = {p: capture.digest(ROOT / p) for p in source_paths}
    with zipfile.ZipFile(args.runtime / 'baseoq4/pak0.pk4') as pack:
        document = 'guis/menu/settings/controls.q4ui'
        if pack.read(document).replace(b'\r\n', b'\n') != (ROOT / 'content/baseoq4/pak0' / document).read_bytes().replace(b'\r\n', b'\n'):
            raise RuntimeError('staged CONTROLS document differs from source')
    scenario = 'text-queries'
    capture.SCENARIOS[scenario] = ('mp:mp/q4dm1' if args.mode == 'mp' else 'game/airdefense1', steps(args.mode))
    capture.SCENARIO_CVARS[scenario] = {'net_ip': '127.0.0.1', 'net_port': '28984', 'ui_autoJoin': '1' if args.mode == 'mp' else '0'}
    result = capture.run(args, scenario)
    log = re.sub(r'\^[0-9]', '', (ROOT / result['log']).read_text(encoding='utf-8', errors='replace'))
    checks, labels, fits = qualify(log)
    checks.update(completed=result['complete'] and result['returncode'] == 0 and not result['errors'],
                  renderer=active_renderer(log) == args.renderer, captures=len(result['screenshots']) == 7,
                  unchanged_binaries=identity == {str(p.relative_to(args.runtime)): capture.digest(p) for p in binaries},
                  unchanged_sources=sources == {p: capture.digest(ROOT / p) for p in source_paths})
    result.update(checks=checks, labels=labels, fits=fits, binaries=identity, sources=sources,
                  status='captured_pending_visual_review' if all(checks.values()) else 'failed', scope=__doc__.strip())
    (args.output / 'result.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({'status': result['status'], 'failed': [k for k, value in checks.items() if not value]}, indent=2))
    return 0 if all(checks.values()) else 1


if __name__ == '__main__':
    raise SystemExit(main())
