#!/usr/bin/env python3
"""Qualify the retained buy card and multiplayer buying in windowed gameplay.

A listen server sells by The Awakening's rules in CTF: the buy key's command
opens the card over the match, a weapon and then its mod are bought from the
card's plates, each tab is captured and the card closes back to the match. A
second run sells by Quake 4's rules, whose MODS page is empty. A remote client
on a dedicated server buys a weapon mod and must build the modded weapon from
the player snapshot. Uses semantic engine commands and engine render-target
screenshots; it never injects device input.
"""
import argparse
import json
import os
import re
import sys
import zipfile
from pathlib import Path

import capture_retained_screens as capture

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/tests'))
from mp_round_remote_smoke import verify_runtime_is_current

# buyCatalog's order (src/mpgame/mp/Buying.cpp): the hyperblaster, its bounce
# mod, the nailgun's firepower mod and the team ammo regeneration.
HYPERBLASTER, BOUNCE, AMMO_REGEN = 1, 9, 16


def press(key):
    return [f'openq4_retainedGui menu {key} 1', f'openq4_retainedGui menu {key} 0', 'waitMsec 300']


def buy(index):
    return [f'openq4_retainedGui focus "buy-{index}"', *press('accept'), 'waitMsec 900']


def shot(name):
    return [f'echo BUY_STAGE "{name}"', 'openq4_retainedGui report', 'openq4_reportMPState', 'openq4_reportMPWorld player1',
            f'screenshot "screenshots/{name}.tga"']


def local_steps():
    return ['addbot', 'waitMsec 5000', *shot('match'), 'buyMenu', 'waitMsec 900', *shot('weapons'),
            'openq4_retainedGui focus "buy-0"', 'waitMsec 300', *shot('weapons-focus'),
            *buy(HYPERBLASTER), *shot('weapons-bought'),
            'openq4_retainedGui event tab_mods', 'waitMsec 500', *shot('mods'),
            *buy(BOUNCE), 'waitMsec 1200', *shot('mods-bought'),
            'openq4_retainedGui event tab_gear', 'waitMsec 500', *shot('gear'),
            'openq4_retainedGui event tab_team', 'waitMsec 500', *shot('team'),
            *buy(AMMO_REGEN), *shot('team-bought'),
            *press('back'), 'waitMsec 800', 'openq4_assertMPGameplayView', *shot('closed'),
            'kill 0', 'waitMsec 14000', 'openq4_assertMPClientActive', 'waitMsec 1500', *shot('respawned')]


def quake4_steps():
    return ['addbot', 'waitMsec 5000', 'buyMenu', 'waitMsec 900', *shot('q4-weapons'),
            'openq4_retainedGui event tab_mods', 'waitMsec 500', *shot('q4-mods'),
            'openq4_retainedGui event tab_team', 'waitMsec 500', *shot('q4-team'),
            *press('back'), 'waitMsec 600']


def bot_steps():
    # Two bots shop on their own with the Awakening's rules and plenty of
    # credits: weapons and armour first, then the mod for the gun in hand.
    return ['addbot', 'addbot', 'set bot_pause 0', 'waitMsec 40000', 'echo BUY_STAGE "bots"', 'openq4_reportMPWorld player2',
            'openq4_reportMPWorld player3']


def remote_steps():
    # The server's two bots take the first client slots; the remote player is
    # player3, which every remote stage also reports.
    def remote_shot(name):
        return [*shot(name), 'openq4_reportMPWorld player3']
    return ['connect 127.0.0.1:28971', capture.AFTER_MAP, 'waitMsec 4000', 'buyMenu', 'waitMsec 900', *remote_shot('remote-weapons'),
            *buy(HYPERBLASTER), 'openq4_retainedGui event tab_mods', 'waitMsec 500', *buy(BOUNCE), 'waitMsec 2500',
            *remote_shot('remote-mods'), *press('back'), 'waitMsec 2500', *remote_shot('remote-hyperblaster')]


BUYING = {'si_isBuyingEnabled': '1', 'si_buyModeStartingCredits': '20000', 'si_gameType': 'CTF', 'si_warmup': '0', 'si_countDown': '4',
          'si_minPlayers': '2', 'si_timeLimit': '0', 'si_captureLimit': '5', 'bot_minPlayers': '0', 'bot_pause': '1',
          'ui_ready': 'Ready'}


def stage_sections(log: str) -> dict:
    return dict(re.findall(r'^BUY_STAGE (\S+)\s*\n(.*?)(?=^BUY_STAGE |\Z)', log, re.M | re.S))


def world(section: str, key: str, entity: int = 0) -> str:
    match = re.search(rf'^MP_WORLD_{key} entity={entity} (.*)$', section, re.M)
    return match.group(1) if match else ''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--assets', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--renderer', choices=('gl', 'vulkan'), required=True)
    parser.add_argument('--size', default='1280x720')
    parser.add_argument('--language', default='english')
    parser.add_argument('--scenario', action='append', choices=('awakening', 'quake4', 'remote', 'bots'))
    args = parser.parse_args()
    args.runtime, args.gate, args.timeout, args.set = ROOT / '.install', '1', 240, []
    args.output = args.output.resolve()
    if args.output.exists() or not args.output.is_relative_to((ROOT / '.tmp').resolve()):
        parser.error('choose a new output directory under the project .tmp')
    verify_runtime_is_current(args.runtime)
    args.output.mkdir(parents=True)
    with zipfile.ZipFile(args.runtime / 'baseoq4/pak0.pk4') as pack:
        document = 'guis/menu/mp_buy.rui'
        if pack.read(document).replace(b'\r\n', b'\n') != (ROOT / 'content/baseoq4/pak0' / document).read_bytes().replace(b'\r\n', b'\n'):
            raise RuntimeError('staged buy document differs from source')
    common = {'com_gameMode': 'MP', 'com_nextGameModule': 'game_mp', 'ui_team': 'Marine', 'ui_autoJoin': '1', 'ui_spectate': 'Play',
              'net_allowCheats': '1', 'developer': '1', 'win_allowMultipleInstances' if os.name == 'nt' else 'sys_allowMultipleInstances': '1'}
    plans = {
        'awakening': ('mp:mp/q4ctf1', local_steps(), {**common, **BUYING, 'si_buyingRules': '1'}, None),
        'quake4': ('mp:mp/q4ctf1', quake4_steps(), {**common, **BUYING, 'si_buyingRules': '0'}, None),
        'bots': ('mp:mp/q4ctf1', bot_steps(), {**common, **BUYING, 'si_buyingRules': '1', 'si_minPlayers': '1'}, None),
        'remote': (None, remote_steps(), {**common, 'net_port': '28972'},
                   {'map': 'mp/q4ctf1', 'port': 28971, 'name': 'Buy card remote',
                    'cvars': {**BUYING, 'si_buyingRules': '1', 'net_allowCheats': '1', 'bot_minPlayers': '2'}}),
    }
    results, failed = {}, []
    for name in args.scenario or list(plans):
        level, steps, cvars, server = plans[name]
        scenario = f'buy-{name}'
        capture.SCENARIOS[scenario] = (level, steps)
        capture.SCENARIO_CVARS[scenario] = cvars
        if server:
            capture.SCENARIO_SERVERS[scenario] = server
        result = capture.run(args, scenario)
        log = re.sub(r'\^[0-9]', '', (ROOT / result['log']).read_text(encoding='utf-8', errors='replace')) if result['log'] else ''
        sections = stage_sections(log)
        checks = {'completed': result['complete'] and result['returncode'] == 0 and not result['errors'],
                  'no-retained-warnings': not result['retained_warnings'],
                  'semantic-operations': not re.search(r'RETAINED_GUI_OPERATION .* failed', log)}
        if name == 'awakening':
            checks['card-loaded'] = 'guis/menu/mp_buy.rui' in ' '.join(result['retained_loaded']) or 'mp_buy.rui' in log
            checks['bought-hyperblaster'] = 'credits=19300' in world(sections.get('weapons-bought', ''), 'INVENTORY')
            checks['bought-bounce'] = 'credits=11300' in world(sections.get('mods-bought', ''), 'INVENTORY')
            checks['mod-on-weapon'] = bool(re.search(r'mods=3:1\b', world(sections.get('mods-bought', ''), 'WEAPON_MODS')))
            checks['bought-team-special'] = 'credits=1300' in world(sections.get('team-bought', ''), 'INVENTORY')
            checks['mod-kept-after-death'] = bool(re.search(r'mods=3:1\b', world(sections.get('respawned', ''), 'WEAPON_MODS')))
            # A suicide costs the content's 300 credits under the Awakening's rules too.
            checks['suicide-cost'] = 'credits=1000' in world(sections.get('respawned', ''), 'INVENTORY')
            checks['session-requests'] = sum('command=mpBuy' in request for request in result['session_requests']) >= 3
        elif name == 'quake4':
            checks['card-loaded'] = 'mp_buy.rui' in log
        elif name == 'bots':
            section = sections.get('bots', '')
            bought = [world(section, 'INVENTORY', entity) for entity in (1, 2)]
            modded = [world(section, 'WEAPON_MODS', entity) for entity in (1, 2)]
            checks['bots-shopped'] = all(line and 'credits=20000' not in line for line in bought)
            # Informational: a bot holding a gun the rules sell a mod for buys it.
            result['bot_mods'] = modded
        else:
            # What the remote client knows of its own player (entity 2), from
            # the server's snapshots: the credits the server charged, the mod
            # bits and the hyperblaster built with the bounce mod's projectile.
            remote_mods = world(sections.get('remote-hyperblaster', ''), 'WEAPON_MODS', 2)
            checks['remote-charged'] = 'credits=11300' in world(sections.get('remote-mods', ''), 'INVENTORY', 2)
            checks['remote-mod-networked'] = bool(re.search(r'mods=3:1\b', world(sections.get('remote-mods', ''), 'WEAPON_MODS', 2)))
            checks['remote-weapon-built-with-mod'] = 'weapon=3 ' in remote_mods and 'mods=3:1' in remote_mods
            checks['remote-bounce-projectile'] = 'projectile=projectile_hyperblaster_bounce1' in remote_mods
        result['checks'] = checks
        result['stages'] = {stage: {key: world(section, key) for key in ('INVENTORY', 'WEAPON', 'WEAPON_MODS')}
                            for stage, section in sections.items()}
        results[name] = result
        failed += [f'{name}:{key}' for key, value in checks.items() if not value]
        print(name, 'PASS' if all(checks.values()) else 'FAIL', len(result['screenshots']), 'screenshots', flush=True)
    (args.output / 'result.json').write_text(json.dumps({'scope': __doc__.strip(), 'results': results}, indent=2) + '\n',
                                             encoding='utf-8')
    print(json.dumps({'status': 'captured_pending_visual_review' if not failed else 'failed', 'failed': failed}, indent=2))
    return 0 if not failed else 1


if __name__ == '__main__':
    raise SystemExit(main())
