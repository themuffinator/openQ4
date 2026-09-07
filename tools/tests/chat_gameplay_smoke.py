#!/usr/bin/env python3
"""Exercise native chat over MP gameplay using engine commands and render-target screenshots."""
import argparse
import json
from pathlib import Path
import re
import subprocess
import time
import zipfile

ROOT = Path(__file__).resolve().parents[2]

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--width', type=int, default=1280)
    parser.add_argument('--height', type=int, default=720)
    parser.add_argument('--renderer', choices=['gl', 'vulkan'], default='gl')
    parser.add_argument('--stock-gui', action='store_true')
    parser.add_argument('--gametype', choices=['DM', 'Team DM'], default='Team DM')
    parser.add_argument('--basepath', type=Path, default=Path('C:/Program Files (x86)/Steam/steamapps/common/Quake 4'))
    args = parser.parse_args()
    tag = f'{args.renderer}-{args.width}x{args.height}' + ('-stock' if args.stock_gui else '') + ('-dm' if args.gametype == 'DM' else '')
    output = ROOT / '.tmp/chat-gameplay' / tag
    save = output / 'save'
    game = save / 'baseoq4'
    game.mkdir(parents=True, exist_ok=True)
    if args.stock_gui:
        wanted = {'guis/mphud.gui', 'guis/mpmsgmode.gui'}
        found = set()
        for pak in sorted((args.basepath / 'q4base').glob('*.pk4')):
            with zipfile.ZipFile(pak) as archive:
                for name in archive.namelist():
                    if name.lower() in wanted:
                        target = game / name.lower()
                        target.parent.mkdir(parents=True, exist_ok=True)
                        target.write_bytes(archive.read(name))
                        found.add(name.lower())
        assert found == wanted, 'both original chat GUI surfaces must be available'

    def shot(name):
        return ['waitMsec 150', f'echo CHAT_SMOKE_{name}', 'chatHistory status',
                f'screenshot "screenshots/{name}.tga"']

    commands = ['openq4_assertMPClientActive', 'say "All channel: ready to play."',
                'sayTeam "Team channel: hold the upper route."', *shot('passive'),
                'set ui_chatTime 0.2', 'waitMsec 1100', *shot('faded'), 'set ui_chatTime 3',
                'messagemode2', *shot('team_input')]
    commands += [f'addChatLine "History {i:02}: keep this conversation available."' for i in range(70)]
    commands += [*shot('full'), 'chatHistory up', *shot('scrolled'),
                 'addChatLine "A new arrival must preserve the reading position."', *shot('arrival'),
                 'set ui_chatScale 1.5', 'set ui_chatWidth 280', *shot('scaled'),
                 'chatHistory bottom', *shot('latest'),
                 'set ui_chatScale 1', 'set ui_chatWidth 320',
                 'addChatLine "Long wrapped message: ' + 'readable words ' * 25 + '"',
                 *shot('wrapped'), 'set ui_chatScale 2', 'set ui_chatWidth 200',
                 'set ui_chatLines 16', 'set ui_chatOffsetX 640', 'set ui_chatOffsetY -480',
                 *shot('compact'), 'quit']
    (game / 'chat_smoke.cfg').write_text('\n'.join(commands) + '\n', encoding='utf-8')
    cvars = {
        'fs_basepath': str(args.basepath), 'fs_savepath': str(save), 'fs_devpath': str(save),
        'fs_game': 'baseoq4', 'com_gameMode': 'MP', 'logFile': '2', 'logFileName': 'logs/openq4.log',
        'developer': '1', 'r_renderApi': args.renderer, 'r_fullscreen': '0', 'r_borderless': '0',
        'r_fullscreenDesktop': '0', 'r_borderlessDefaultMigrated': '1', 'r_mode': '-1',
        'r_customWidth': str(args.width), 'r_customHeight': str(args.height),
        'r_windowWidth': str(args.width), 'r_windowHeight': str(args.height),
        'r_hiddenWindow': '1', 'in_mouse': '0', 'in_joystick': '0', 's_noSound': '1',
        'com_maxfps': '60', 'r_swapInterval': '0', 'com_skipLoadingContinue': '1',
        'net_port': '28839', 'net_serverDedicated': '0', 'net_LANServer': '1',
        'si_pure': '0', 'si_gameType': args.gametype, 'si_maxPlayers': '4',
        'si_warmup': '1', 'si_useReady': '1', 'bot_minPlayers': '0',
        'ui_autoJoin': '1', 'ui_spectate': 'Play', 'ui_team': 'Marine', 'ui_name': 'ChatTest',
        'ui_chatScale': '1', 'ui_chatWidth': '320', 'ui_chatTime': '3', 'ui_chatLines': '6',
        'ui_chatOffsetX': '0', 'ui_chatOffsetY': '0', 'ui_chatAlpha': '0.5',
        'g_autoExecAfterMapLoad': 'chat_smoke.cfg', 'g_autoExecAfterMapLoadDelayMs': '500',
    }
    executable = ROOT / '.install/openQ4-client_x64.exe'
    command = [str(executable)]
    for name, value in cvars.items():
        command += ['+set', name, value]
    command += ['+spawnServer', 'mp/q4dm1']
    (output / 'launch.json').write_text(json.dumps(command, indent=2), encoding='utf-8')
    with (output / 'console.log').open('w', encoding='utf-8') as console:
        started = time.time()
        process = subprocess.Popen(command, cwd=ROOT / '.install', stdout=console, stderr=subprocess.STDOUT)
        try:
            code = process.wait(timeout=120)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
            raise RuntimeError('chat smoke timed out; inspect ' + str(output))
    log = (game / 'logs/openq4.log').read_text(encoding='utf-8', errors='replace')
    reports = {}
    for section in re.split(r'CHAT_SMOKE_', log)[1:]:
        name = section.splitlines()[0].strip()
        match = re.search(r'chatHistory: messages=(\d+) rows=(\d+) first=(\d+) serial=(\d+) following=(\d+) channel=(\d+)', section)
        if match:
            reports[name] = dict(zip(['messages', 'rows', 'first', 'serial', 'following', 'channel'], map(int, match.groups())))
    assert code == 0, f'game exited with {code}'
    assert reports['team_input']['channel'] == (1 if args.gametype == 'Team DM' else 0), reports
    assert reports['team_input']['rows'] > reports['team_input']['messages'], 'sample line must wrap rather than clip'
    assert reports['full']['messages'] == 64 and reports['full']['following'] == 1, reports
    assert reports['scrolled']['following'] == 0, reports
    assert reports['arrival']['serial'] == reports['scrolled']['serial'], reports
    assert reports['scaled']['serial'] == reports['arrival']['serial'], reports
    assert reports['latest']['following'] == 1, reports
    assert reports['wrapped']['rows'] > reports['wrapped']['messages'], reports
    for name in ('passive', 'faded', 'team_input', 'full', 'scrolled', 'arrival', 'scaled', 'latest', 'wrapped', 'compact'):
        image = game / f'screenshots/{name}.tga'
        assert image.is_file() and image.stat().st_mtime >= started, name
    failures = re.findall(r'^.*(?:ERROR:|WARNING:.*<openQ4 chat>).*$', log, re.M)
    assert not failures, failures
    (output / 'report.json').write_text(json.dumps(reports, indent=2), encoding='utf-8')
    print('Chat gameplay passed: ' + str(output))

if __name__ == '__main__':
    main()
