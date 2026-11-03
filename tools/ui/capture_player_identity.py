#!/usr/bin/env python3
"""Qualify player-slot replacement through live menus and engine screenshots.

Uses a hidden window, a loopback listen server and semantic engine diagnostics;
never injects OS input. Bots exercise connection reuse, not remote voice transport.
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


def activate(control):
    return [f'openq4_retainedGui focus "{control}"', 'openq4_retainedGui menu accept 1',
            'openq4_retainedGui menu accept 0', 'waitMsec 300']


def observe(stage):
    nodes = ('players-a-0-chosen', 'players-a-1-chosen', 'players-a-1-friend', 'players-a-1-speaker-slash')
    return [f'echo "PLAYER_IDENTITY_STAGE {stage}"', 'openq4_retainedGui report',
            *[f'openq4_retainedGui inspect "{node}"' for node in nodes], f'screenshot "screenshots/{stage}.tga"']


def observe_admin(stage):
    nodes = ('admin-player-1', 'admin-player-1-chosen', 'admin-player-1-c0', 'admin-kick-reason')
    return [f'echo "PLAYER_IDENTITY_STAGE {stage}"', 'openq4_retainedGui report',
            *[f'openq4_retainedGui inspect "{node}"' for node in nodes], f'screenshot "screenshots/{stage}.tga"']


def steps():
    return ['waitMsec 5000', 'addbot IdentityOriginal', 'waitMsec 3000',
            'screenshot "screenshots/gameplay.tga"', 'openq4_assertMenuActivation 4000 game',
            'waitMsec 500', 'openq4_retainedGui event tab_players', 'waitMsec 500',
            *activate('players-a-1'), *activate('players-friend'), *activate('players-mute'), *observe('marked'),
            # The press belongs to the first connection. Replacement and fresh
            # publication before release must cancel it and reset the selection.
            'openq4_retainedGui focus "players-a-1"', 'openq4_retainedGui menu accept 1',
            'removebot IdentityOriginal', 'waitMsec 300', 'addbot IdentityReplacement', 'waitMsec 1500',
            'openq4_retainedGui menu accept 0', 'waitMsec 500', *observe('replaced'),
            *activate('players-a-1'), *activate('players-friend'), *activate('players-mute'), *observe('fresh-marks'),
            # Queue the current row's event, then replace it before the next
            # session/game dispatch. Frozen operands must reject that request.
            'openq4_retainedGui event players_a1', 'removebot IdentityReplacement', 'waitMsec 300',
            'addbot IdentityFinal', 'waitMsec 1500', *observe('queued-replaced'),
            *activate('players-a-1'), *observe('fresh-selection'),
            # Admin buttons must also retire a held action and freeze its
            # selected target before later session dispatch.
            'openq4_retainedGui event tab_admin', 'waitMsec 500',
            *activate('admin-player-1'), *observe_admin('admin-selected'),
            'openq4_retainedGui focus "admin-kick"', 'openq4_retainedGui menu accept 1',
            'removebot IdentityFinal', 'waitMsec 300', 'addbot AdminReplacement', 'waitMsec 1500',
            'openq4_retainedGui menu accept 0', 'waitMsec 500', *observe_admin('admin-held-replaced'),
            *activate('admin-player-1'), 'openq4_retainedGui event admin_kick',
            'removebot AdminReplacement', 'waitMsec 300', 'addbot AdminFinal', 'waitMsec 1500',
            *observe_admin('admin-queued-replaced'),
            *activate('admin-player-1'), *activate('admin-kick'), 'waitMsec 1000', *observe_admin('admin-fresh-kick'),
            'openq4_retainedGui event onBack', 'waitMsec 500', 'screenshot "screenshots/gameplay-return.tga"']


def qualify(log):
    sections = dict(re.findall(r'^PLAYER_IDENTITY_STAGE (\S+)\s*\n(.*?)(?=^PLAYER_IDENTITY_STAGE |\Z)', log, re.M | re.S))
    checks = {}
    for stage in ('marked', 'replaced', 'fresh-marks', 'queued-replaced', 'fresh-selection'):
        section = sections.get(stage, '')
        nodes = {node: (float(opacity), display) for node, opacity, display in re.findall(
            r'^RETAINED_GUI_NODE id=(\S+) bounds=\S+ opacity=(\S+) display=(\S+)', section, re.M)}
        marked = stage in ('marked', 'fresh-marks')
        selected = marked or stage == 'fresh-selection'
        checks[stage + '-friend'] = nodes.get('players-a-1-friend', (-1, ''))[0] == (1 if marked else .125)
        checks[stage + '-mute'] = nodes.get('players-a-1-speaker-slash', (-1, ''))[1] == ('block' if marked else 'none')
        checks[stage + '-selection'] = nodes.get('players-a-1-chosen', (-1, ''))[1] == ('block' if selected else 'none') and \
            nodes.get('players-a-0-chosen', (-1, ''))[1] == ('none' if selected else 'block')
    slots = re.findall(r"addbot: 'Identity(?:Original|Replacement|Final)' joined as client (\d+)", log)
    checks['same-slot-reused'] = slots == ['1', '1', '1']
    for stage in ('admin-selected', 'admin-held-replaced', 'admin-queued-replaced', 'admin-fresh-kick'):
        section = sections.get(stage, '')
        nodes = {node: display for node, display in re.findall(
            r'^RETAINED_GUI_NODE id=(\S+) bounds=\S+ opacity=\S+ display=(\S+)', section, re.M)}
        checks[stage] = nodes.get('admin-player-1') == ('none' if stage == 'admin-fresh-kick' else 'block') and \
            nodes.get('admin-player-1-chosen') == ('block' if stage == 'admin-selected' else 'none')
    checks['admin-slot-reused'] = re.findall(r"addbot: 'Admin(?:Replacement|Final)' joined as client (\d+)", log) == ['1', '1']
    checks['semantic-operations'] = not re.search(r'RETAINED_GUI_OPERATION .* failed|openq4_retainedGui: requires', log)
    return checks


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--assets', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--renderer', choices=('gl', 'vulkan'), required=True)
    parser.add_argument('--size', default='1280x720')
    args = parser.parse_args()
    args.runtime, args.language, args.gate, args.timeout, args.set = ROOT / '.install', 'english', '1', 180, []
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
               'tools/ui/retained_mp_admin.py',
               'src/ui/UserInterfaceRetained.cpp', 'src/framework/Session_menu.cpp',
               'src/ui/retained/Interaction.cpp', 'src/ui/retained/State.cpp', 'tools/ui/capture_player_identity.py',
               'content/baseoq4/pak0/guis/menu/mp_escape.q4ui']
    source_bindings = {p: capture.digest(ROOT / p) for p in sources}
    with zipfile.ZipFile(args.runtime / 'baseoq4/pak0.pk4') as pack:
        document = 'guis/menu/mp_escape.q4ui'
        if pack.read(document).replace(b'\r\n', b'\n') != (ROOT / 'content/baseoq4/pak0' / document).read_bytes().replace(b'\r\n', b'\n'):
            raise RuntimeError('staged Players document differs from source')
    capture.SCENARIOS['player-identity'] = ('mp:mp/q4dm1', steps())
    capture.SCENARIO_CVARS['player-identity'] = {'net_ip': '127.0.0.1', 'net_port': '28984', 'ui_autoJoin': '1',
        'bot_minPlayers': '0', 'bot_characters': '0', 'si_useReady': '1', 'si_warmup': '1'}
    result = capture.run(args, 'player-identity')
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
