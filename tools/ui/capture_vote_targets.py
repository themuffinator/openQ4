#!/usr/bin/env python3
"""Qualify retained kick-vote catalog and draft ownership in windowed gameplay.

Uses semantic engine diagnostics and engine render-target screenshots. It never
injects device input. Loopback bots exercise slot replacement; delayed remote
wire admission is covered separately by ui_player_targets.py.
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


def press(key):
    return [f'openq4_retainedGui menu {key} 1', f'openq4_retainedGui menu {key} 0', 'waitMsec 300']


def activate(node):
    return [f'openq4_retainedGui focus "{node}"', *press('accept')]


def observe(stage):
    return [f'echo VOTE_TARGET_STAGE "{stage}"', 'openq4_retainedGui report',
            'openq4_retainedGui widget "vote-row-kick"', 'openq4_retainedGui widget "vote-row-restart"',
            'openq4_retainedGui inspect "vote-call-lock"',
            'openq4_retainedGui inspect "vote-call-reason"',
            f'screenshot "screenshots/{stage}.tga"']


def replace_subject():
    return ['removebot VoteSubject', 'waitMsec 300', 'addbot VoteSubject', 'waitMsec 1500']


def steps():
    return ['waitMsec 4000', 'addbot VoteSubject', 'waitMsec 2500',
            'screenshot "screenshots/gameplay.tga"', 'openq4_assertMenuActivation 5000 game',
            'waitMsec 500', 'openq4_retainedGui event tab_vote', 'waitMsec 500', *observe('initial'),
            # Keep the displayed label identical across the replacement.
            *activate('vote-row-kick'), *press('down'), 'openq4_retainedGui menu accept 1',
            *replace_subject(), 'openq4_retainedGui menu accept 0', 'waitMsec 500', *observe('held-replaced'),
            'openq4_retainedGui focus "vote-row-kick"', *press('right'), *observe('drafted'),
            *activate('vote-row-restart'), *observe('mixed'),
            *replace_subject(), *observe('stale'),
            'openq4_retainedGui event vote_call', 'waitMsec 3000', *observe('blocked'),
            # Explicitly choosing No one acknowledges the changed draft.
            *activate('vote-row-kick'), *press('home'), *press('accept'), *observe('cleared'),
            *activate('vote-row-restart'), 'openq4_retainedGui focus "vote-row-kick"',
            *press('right'), *observe('reselected'), *activate('vote-call'), 'waitMsec 6000',
            'openq4_assertMenuActivation 5000 game', 'waitMsec 500', 'openq4_retainedGui event tab_vote',
            'waitMsec 500', *activate('vote-row-kick'), *observe('after-kick'),
            *press('back'), 'openq4_retainedGui event onBack', 'waitMsec 500',
            'openq4_assertMPGameplayView', 'screenshot "screenshots/gameplay-return.tga"']


def qualify(log):
    sections = dict(re.findall(r'^VOTE_TARGET_STAGE (\S+)\s*\n(.*?)(?=^VOTE_TARGET_STAGE |\Z)', log, re.M | re.S))
    checks = {}
    # selected row, restart flag, popup open, and disabled-call lock.
    expected = {'initial': (-1, 0, 0, 'block'), 'held-replaced': (-1, 0, 0, 'block'),
                'drafted': (0, 0, 0, 'none'), 'mixed': (0, 1, 0, 'none'),
                'stale': (-1, 1, 0, 'block'), 'blocked': (-1, 1, 0, 'block'),
                'cleared': (-1, 1, 0, 'none'), 'reselected': (0, 0, 0, 'none'),
                'after-kick': (-1, 0, 1, 'block')}
    for stage, (choice, restart, popup, lock) in expected.items():
        section = sections.get(stage, '')
        widgets = {name: (float(value), int(opened)) for name, value, opened in re.findall(
            r'^RETAINED_GUI_WIDGET id=(\S+) .*? accepted=(\S+) .*? popup=(\d)', section, re.M)}
        checks[stage] = widgets.get('vote-row-kick') == (choice, popup) and widgets.get('vote-row-restart', (-1,))[0] == restart
        checks[stage+'-call'] = bool(re.search(r'^RETAINED_GUI_NODE id=vote-call-lock .*? display='+lock+r'\b', section, re.M))
    checks['same-name-slot-reuse'] = re.findall(r"addbot: 'VoteSubject' joined as client (\d+)", log) == ['1', '1', '1']
    # A row's local display flag does not describe ancestor clipping/counting.
    # Check the disconnect after the fresh request instead. The server and
    # chat lines share a localized reason; do not require an English message.
    final_vote = sections.get('reselected', '')
    checks['fresh-kick'] = bool(re.search(r'^client 1 ([^\n]+)\nVoteSubject \1$', final_vote, re.M))
    checks['semantic-operations'] = not re.search(r'RETAINED_GUI_OPERATION .* failed|openq4_retainedGui: requires|openq4_guiGet: unknown', log)
    return checks


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--assets', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--renderer', choices=('gl', 'vulkan'), required=True)
    parser.add_argument('--size', default='1280x720')
    parser.add_argument('--language', default='english')
    args = parser.parse_args()
    args.runtime, args.gate, args.timeout, args.set = ROOT / '.install', '1', 180, []
    args.output = args.output.resolve()
    if args.output.exists() or not args.output.is_relative_to((ROOT / '.tmp').resolve()):
        parser.error('choose a new output directory under the project .tmp')
    verify_runtime_is_current(args.runtime)
    args.output.mkdir(parents=True)
    binaries = [args.runtime / ('openQ4-client_x64.exe' if os.name == 'nt' else 'openQ4-client_x64'),
                *sorted(args.runtime.glob('renderer-*')), *sorted((args.runtime / 'baseoq4').glob('game-*'))]
    binaries = [p for p in binaries if p.is_file()]
    bindings = {str(p.relative_to(args.runtime)): capture.digest(p) for p in binaries}
    sources = ['src/mpgame/Player.cpp', 'src/mpgame/Player.h', 'src/mpgame/MultiplayerGame.cpp',
               'src/mpgame/MultiplayerGame.h', 'src/mpgame/Game_network.cpp', 'src/mpgame/gamesys/SysCmds.cpp',
               'tools/ui/retained_mp_menus.py',
               'src/ui/UserInterfaceRetained.cpp', 'src/framework/Session_menu.cpp',
               'src/ui/retained/Interaction.cpp', 'src/ui/retained/State.cpp', 'src/ui/retained/Document.cpp',
               'src/ui/retained/Document.h', 'tools/ui/capture_vote_targets.py',
               'content/baseoq4/pak0/guis/menu/mp_escape.q4ui']
    source_bindings = {p: capture.digest(ROOT / p) for p in sources}
    with zipfile.ZipFile(args.runtime / 'baseoq4/pak0.pk4') as pack:
        document = 'guis/menu/mp_escape.q4ui'
        if pack.read(document).replace(b'\r\n', b'\n') != (ROOT / 'content/baseoq4/pak0' / document).read_bytes().replace(b'\r\n', b'\n'):
            raise RuntimeError('staged Vote document differs from source')
    capture.SCENARIOS['vote-targets'] = ('mp:mp/q4dm1', steps())
    capture.SCENARIO_CVARS['vote-targets'] = {'net_ip': '127.0.0.1', 'net_port': '28984', 'ui_autoJoin': '1',
        'si_allowVoting': '1', 'si_voteFlags': '0', 'bot_minPlayers': '0', 'bot_characters': '0', 'si_useReady': '1', 'si_warmup': '1'}
    result = capture.run(args, 'vote-targets')
    log = re.sub(r'\^[0-9]', '', (ROOT / result['log']).read_text(encoding='utf-8', errors='replace'))
    checks = qualify(log)
    checks.update(completed=result['complete'] and result['returncode'] == 0 and not result['errors'],
                  renderer=active_renderer(log) == args.renderer, captures=len(result['screenshots']) == 11,
                  unchanged_binaries=bindings == {str(p.relative_to(args.runtime)): capture.digest(p) for p in binaries},
                  unchanged_sources=source_bindings == {p: capture.digest(ROOT / p) for p in sources})
    result.update(checks=checks, binaries=bindings, sources=source_bindings, log_sha256=capture.digest(ROOT / result['log']),
                  active_renderer=active_renderer(log), scope=__doc__.strip(),
                  status='captured_pending_visual_review' if all(checks.values()) else 'failed')
    (args.output / 'result.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({'status': result['status'], 'failed': [k for k, value in checks.items() if not value]}, indent=2))
    return 0 if all(checks.values()) else 1


if __name__ == '__main__':
    raise SystemExit(main())
