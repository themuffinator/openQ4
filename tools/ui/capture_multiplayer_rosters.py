#!/usr/bin/env python3
"""Capture live multiplayer menus and 32-slot roster fixtures through the engine.

Uses the staged executable, hidden windowed mode and semantic diagnostics.
No OS input, microphone or screen capture. The 32-slot documents use explicit
fixture data; this is not a 32-network-client server qualification.
"""
import argparse
import json
import os
from pathlib import Path
import re
import sys
import zipfile

import capture_retained_screens as capture
from capture_system_refresh import active_renderer

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/tests'))
from mp_round_remote_smoke import verify_runtime_is_current

VIEWS = (('escape', 'mp_escape', 'players', 'players'),
         ('welcome', 'mp_welcome', 'players', 'players'),
         ('statistics', 'mp_summary', 'statistics', 'statistics'),
         ('scoreboard', 'mp_summary', 'scoreboard', 'board'))


def menu(action):
    return [f'openq4_retainedGui menu {action} 1', f'openq4_retainedGui menu {action} 0', 'waitMsec 200']


def state(values):
    return [*[f'openq4_retainedGui pending {key} "{value}"' for key, value in values.items()], 'openq4_retainedGui update']


def observe(stage, prefix, last, screenshot=True):
    return [f'echo "ROSTER_STAGE {stage}"', 'openq4_retainedGui report',
            f'openq4_retainedGui widget "{prefix}-scroll"',
            *[f'openq4_retainedGui inspect "{prefix}-{node}"' for node in ('headings', 'viewport', last)],
            *([f'screenshot "screenshots/{stage}.tga"'] if screenshot else [])]


def steps():
    commands = ['waitMsec 6000', 'screenshot "screenshots/gameplay.tga"',
                'openq4_assertMenuActivation 4000 game', 'waitMsec 500',
                'openq4_retainedGui event tab_players', 'waitMsec 500',
                'openq4_retainedGui focus "players-a-1"', *menu('accept'),
                'screenshot "screenshots/live-players.tga"', 'openq4_retainedGui event onBack', 'waitMsec 500']
    for name, document, tab, prefix in VIEWS:
        commands += [f'testGUI "guis/menu/{document}.rui"', 'waitMsec 500']
        values = {'mp.players.a.shown': 1, 'mp.players.a.count': 32, 'mp.players.a.color': 3,
                  'mp.players.a.title': '#str_200038', 'mp.players.b.shown': 0,
                  'mp.players.b.count': 0, 'mp.players.s.count': 0}
        for row in range(32):
            key = f'mp.players.a{row}'
            values.update({f'{key}.client': row, f'{key}.name': f'Roster fixture {row + 1:02}',
                           f'{key}.score': row, f'{key}.ping': 20 + row})
        if name != 'welcome':
            values.update({'mp.stat.client': 31, 'mp.stat.name': 'Roster fixture 32'})
        commands += state(values) + ['openq4_retainedGui event open', f'openq4_retainedGui event tab_{tab}',
                                     'waitMsec 500', f'openq4_retainedGui focus "{prefix}-scroll"', *menu('home')]
        commands += observe(f'{name}-top', prefix, 'a-0')
        commands += menu('end') + observe(f'{name}-end', prefix, 'a-31')
        # Actual adapter snapshot save/restore retains the scroll; a shrink
        # afterwards must remove the obsolete offset.
        commands += ['openq4_retainedGui save', *menu('home'), 'openq4_retainedGui restore', 'waitMsec 300']
        commands += observe(f'{name}-restored', prefix, 'a-31', False)
        mixed = {'mp.players.a.count': 10, 'mp.players.b.shown': 1,
                 'mp.players.b.title': '#str_200199', 'mp.players.b.color': 1, 'mp.players.b.count': 10,
                 'mp.players.s.count': 12, 'mp.players.s.title': '#str_200281'}
        for kind, count in (('b', 10), ('s', 12)):
            for row in range(count):
                key = f'mp.players.{kind}{row}'
                mixed.update({f'{key}.client': 10 + row if kind == 'b' else 20 + row,
                              f'{key}.name': f'{"Strogg" if kind == "b" else "Spectator"} {row+1:02}',
                              f'{key}.score': row if kind == 'b' else '', f'{key}.ping': 30 + row})
        commands += state(mixed)
        # Publication and the next rendered layout are separate engine steps.
        commands += ['waitMsec 300', f'openq4_retainedGui focus "{prefix}-scroll"', *menu('end')]
        commands += observe(f'{name}-mixed', prefix, 's-11')
        commands += state({'mp.players.a.count': 1, 'mp.players.b.shown': 0,
                           'mp.players.b.count': 0, 'mp.players.s.count': 0})
        commands += ['waitMsec 300'] + observe(f'{name}-short', prefix, 'a-0', False)
        commands += ['testGUI', 'waitMsec 300']
    commands += ['screenshot "screenshots/gameplay-return.tga"']
    return commands


def qualify(log):
    sections = dict(re.findall(r'^ROSTER_STAGE (\S+)\s*\n(.*?)(?=^ROSTER_STAGE |\Z)', log, re.M | re.S))
    checks = {}
    def close(a, b):
        return abs(a - b) <= 1
    for name, _, _, prefix in VIEWS:
        records = {}
        for suffix in ('top', 'end', 'restored', 'mixed', 'short'):
            stage = f'{name}-{suffix}'
            section = sections.get(stage, '')
            match = re.search(rf'^RETAINED_GUI_SCROLL id={prefix}-scroll (.*)$', section, re.M)
            scroll = {k: float(v) for k, v in re.findall(r'(\w+)=([-+\d.eE]+)', match[1])} if match else {}
            nodes = {key: [float(v) for v in value.split(',')] for key, value in
                     re.findall(r'^RETAINED_GUI_NODE id=(\S+) bounds=(\S+)', section, re.M)}
            last = 's-11' if suffix == 'mixed' else 'a-0' if suffix in ('top', 'short') else 'a-31'
            row, area = nodes.get(f'{prefix}-{last}', []), nodes.get(f'{prefix}-viewport', [])
            checks[f'{stage}-visible'] = len(row) == len(area) == 4 and row[3] > 0 and \
                row[1] >= area[1] - 1 and row[1] + row[3] <= area[1] + area[3] + 1
            checks[f'{stage}-scroll'] = bool(scroll) and (scroll.get('usable') == 0 and close(scroll.get('offset', -9), 0)
                if suffix == 'short' else scroll.get('usable') == 1 and scroll.get('range', 0) > 0 and
                close(scroll.get('offset', -9), 0 if suffix == 'top' else scroll.get('range', -99)))
            records[suffix] = nodes
        headings = [records[s].get(f'{prefix}-headings', []) for s in records]
        checks[f'{name}-fixed-headings'] = all(len(h) == 4 and h == headings[0] for h in headings)
    checks['semantic-operations'] = not re.search(r'RETAINED_GUI_OPERATION .* failed|openq4_retainedGui: requires', log)
    return checks


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--assets', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--renderer', choices=('gl', 'vulkan'), required=True)
    parser.add_argument('--size', default='1280x720')
    args = parser.parse_args()
    args.runtime, args.language, args.gate, args.timeout, args.set = ROOT / '.install', 'english', '1', 240, []
    args.output = args.output.resolve()
    if args.output.exists() or not args.output.is_relative_to((ROOT / '.tmp').resolve()):
        parser.error('choose a new output directory under the project .tmp')
    verify_runtime_is_current(args.runtime)
    args.output.mkdir(parents=True)
    executable = args.runtime / ('openQ4-client_x64.exe' if os.name == 'nt' else 'openQ4-client_x64')
    binaries = [executable, *sorted(args.runtime.glob('renderer-*')), *sorted((args.runtime / 'baseoq4').glob('game-*'))]
    binaries = [p for p in binaries if p.is_file()]
    bindings = {str(p.relative_to(args.runtime)): capture.digest(p) for p in binaries}
    documents = {f'guis/menu/{doc}.rui' for _, doc, _, _ in VIEWS}
    source_paths = [*(ROOT / 'content/baseoq4/pak0' / doc for doc in sorted(documents)),
                    ROOT / 'src/mpgame/MultiplayerGame.h', ROOT / 'src/mpgame/MultiplayerGame.cpp', Path(__file__).resolve()]
    sources = {str(p.relative_to(ROOT)): capture.digest(p) for p in source_paths}
    with zipfile.ZipFile(args.runtime / 'baseoq4/pak0.pk4') as pack:
        if any(pack.read(doc).replace(b'\r\n', b'\n') != (ROOT / 'content/baseoq4/pak0' / doc).read_bytes().replace(b'\r\n', b'\n')
               for doc in documents):
            raise RuntimeError('staged roster documents differ from current source')
    capture.SCENARIOS['rosters'] = ('mp:mp/q4dm1', steps())
    capture.SCENARIO_CVARS['rosters'] = {'net_ip': '127.0.0.1', 'net_port': '28983',
        'ui_autoJoin': '1', 'ui_retainedMultiplayer': '1', 'bot_minPlayers': '3', 'si_useReady': '0', 'si_warmup': '0'}
    result = capture.run(args, 'rosters')
    log = re.sub(r'\^[0-9]', '', (ROOT / result['log']).read_text(encoding='utf-8', errors='replace'))
    checks = qualify(log)
    checks.update(completed=result['complete'] and result['returncode'] == 0 and not result['errors'],
                  renderer=active_renderer(log) == args.renderer, captures=len(result['screenshots']) == 15,
                  unchanged_binaries=bindings == {str(p.relative_to(args.runtime)): capture.digest(p) for p in binaries},
                  unchanged_sources=sources == {str(p.relative_to(ROOT)): capture.digest(p) for p in source_paths})
    result.update(checks=checks, binaries=bindings, sources=sources, log_sha256=capture.digest(ROOT / result['log']),
                  active_renderer=active_renderer(log),
                  status='captured_pending_visual_review' if all(checks.values()) else 'failed',
                  scope='live three-player menu plus 32-slot production-document fixtures; no 32-client network qualification')
    (args.output / 'result.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({'status': result['status'], 'failed': [k for k, passed in checks.items() if not passed]}, indent=2))
    return 0 if all(checks.values()) else 1


if __name__ == '__main__':
    raise SystemExit(main())
