#!/usr/bin/env python3
"""Mixed-version voice interoperability: a published openQ4 release against this build.

Two directions, each at si_pure 1 and 0, all on loopback with hidden windows and
synthetic voice only (no microphone, no host input):

* new dedicated server, a new talking client and an old-release client;
* old-release dedicated server and a new talking client.

The report records whether the join is admitted at all and, when it is, whether
the old peer ever receives or is sent the new voice message type (old engines
print "unknown unreliable ..." once per unexpected voice packet), and whether
the new client transmits anything to a server that does not advertise
`si_voiceProtocol`. The old release runs in place from its extracted directory
and is never modified; its paks must sit beside it, because each executable
checks its own compiled pak digests.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import subprocess
import time
from pathlib import Path

from mp_round_remote_smoke import verify_runtime_is_current, write_script

ROOT = Path(__file__).resolve().parents[2]
UNKNOWN = re.compile(r'unknown unreliable (?:server )?message')
REJECTION = re.compile(r'(?i)(not in sync|checksum mismatch|missing|pure|rejected|bad protocol|server uses protocol|disconnect|denied)[^\n]*')


def game_module_sha256(runtime: Path) -> str | None:
    """The module that served multiplayer: game_x64 now, game-mp_x64 in releases before 0.16."""
    for name in ('game_x64.dll', 'game-mp_x64.dll'):
        module = runtime / 'baseoq4' / name
        if module.is_file():
            return hashlib.sha256(module.read_bytes()).hexdigest()
    return None


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--assets', type=Path, required=True)
    parser.add_argument('--historical', type=Path, required=True, help='extracted older openQ4 release (its exe dir)')
    parser.add_argument('--output', type=Path, default=ROOT / '.tmp/audio/voice-mixed-version')
    parser.add_argument('--port', type=int, default=28981)
    parser.add_argument('--join-timeout', type=int, default=150)
    parser.add_argument('--pure', choices=('0', '1', 'both'), default='both')
    parser.add_argument('--render-api', default='vulkan')
    args = parser.parse_args()
    runtime, old_runtime = ROOT / '.install', args.historical.resolve()
    suffix = '.exe' if os.name == 'nt' else ''
    verify_runtime_is_current(runtime)
    verify_runtime_is_current(runtime, f'openQ4-ded_x64{suffix}')
    binaries = {('new', 'client'): runtime / f'openQ4-client_x64{suffix}', ('new', 'server'): runtime / f'openQ4-ded_x64{suffix}',
                ('old', 'client'): old_runtime / f'openQ4-client_x64{suffix}', ('old', 'server'): old_runtime / f'openQ4-ded_x64{suffix}'}
    for path in binaries.values():
        if not path.is_file():
            parser.error(f'missing runtime binary {path}')
    old_version = (old_runtime / 'VERSION.txt').read_text(errors='replace') if (old_runtime / 'VERSION.txt').is_file() else ''
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)

    scenarios = []
    for pure in (('1', '0') if args.pure == 'both' else (args.pure,)):
        scenarios.append({'name': f'new-server-old-client-pure{pure}', 'pure': pure,
                          'roles': {'server': 'new', 'talker': 'new', 'guest': 'old'}})
        scenarios.append({'name': f'old-server-new-client-pure{pure}', 'pure': pure,
                          'roles': {'server': 'old', 'talker': 'new'}})

    results = []
    for number, scenario in enumerate(scenarios):
        base = output / scenario['name']
        port = args.port + number * 20
        roles = scenario['roles']
        saves = {role: base / role for role in roles}
        games = {role: saves[role] / 'baseoq4' for role in roles}
        logs = {role: games[role] / 'logs/openq4.log' for role in roles}
        steps = {role: 0 for role in roles}
        processes, streams = {}, []
        for role in roles:
            games[role].mkdir(parents=True, exist_ok=True)
            logs[role].unlink(missing_ok=True)
            for stale in games[role].glob('mix_cmd_*.cfg'):
                stale.unlink()
            # Same single-use step pointer as voice_chat_soak.py.
            write_script(games[role] / 'mix_loop.cfg', ['waitMsec 500', 'openq4_reportMPState', 'exec mix_step.cfg', 'exec mix_loop.cfg'])
            write_script(games[role] / 'mix_step.cfg', ['// idle'])
            write_script(games[role] / 'mix_start.cfg', ['exec mix_loop.cfg'])
            (saves[role] / 'alsoft.conf').write_text('[general]\ndrivers=null\n')

        def read(role):
            return logs[role].read_text(encoding='utf-8', errors='replace') if logs[role].is_file() else ''

        def launch(role):
            version = roles[role]
            server = role == 'server'
            exe = binaries[(version, 'server' if server else 'client')]
            cvars = {
                'fs_basepath': str(args.assets.resolve()), 'fs_game': 'baseoq4', 'com_gameMode': 'MP',
                'fs_savepath': str(saves[role]), 'fs_devpath': str(saves[role]),
                'r_fullscreen': '0', 'r_borderless': '0', 'r_mode': '-1', 'r_customWidth': '640', 'r_customHeight': '360',
                'r_hiddenWindow': '1', 'r_renderApi': args.render_api, 'r_swapInterval': '0', 'com_maxfps': '60',
                'in_mouse': '0', 'in_joystick': '0', 's_noSound': '0', 's_muteUnfocused': '0',
                's_voiceChatCapture': '0', 's_voiceChatSend': '1', 's_voiceChatReceive': '1', 's_voiceChatEcho': '0',
                'logFile': '2', 'logFileName': 'logs/openq4.log', 'developer': '1', 'ui_retained': '1',
                'ui_autoJoin': '1', 'ui_spectate': 'Play', 'ui_ready': 'Ready', 'ui_name': f'mix_{version}_{role}',
                'net_allowCheats': '1', 'net_ip': '127.0.0.1', 'net_enableIPv6': '0', 'net_port': str(port + 1 + list(roles).index(role)),
                'com_skipLoadingContinue': '1', 'g_autoExecAfterMapLoad': 'mix_start.cfg', 'g_autoExecAfterMapLoadDelayMs': '500',
                'win_allowMultipleInstances' if os.name == 'nt' else 'sys_allowMultipleInstances': '1',
            }
            if server:
                cvars.update(net_port=str(port), net_serverDedicated='1', net_LANServer='1', si_pure=scenario['pure'],
                             si_maxPlayers='4', si_gameType='DM', si_minPlayers='1', si_warmup='0', si_useReady='0',
                             si_timeLimit='0', si_fragLimit='0', bot_minPlayers='0', si_voiceChat='1')
            command = [str(exe)]
            for key, value in cvars.items():
                command += ['+set', key, value]
            assert len(cvars) + 1 <= 64
            command += ['+spawnServer', 'mp/q4dm1'] if server else ['+connect', f'127.0.0.1:{port}']
            (base / f'{role}-launch.json').write_text(json.dumps(command, indent=2))
            options = {'env': {**os.environ, 'ALSOFT_CONF': str(saves[role] / 'alsoft.conf')}}
            if os.name == 'nt':
                startup = subprocess.STARTUPINFO()
                startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
                startup.wShowWindow = 0
                options.update(startupinfo=startup, creationflags=subprocess.CREATE_NO_WINDOW)
            stream = (base / f'{role}-process.log').open('w')
            streams.append(stream)
            processes[role] = subprocess.Popen(command, cwd=exe.parent, stdout=stream, stderr=subprocess.STDOUT, **options)

        def wait_for(predicate, timeout):
            deadline = time.monotonic() + timeout
            while time.monotonic() < deadline:
                current = {role: read(role) for role in roles}
                if predicate(current):
                    return True
                if any(process.poll() is not None for process in processes.values()):
                    return False
                time.sleep(.5)
            return False

        def step(role, lines, marker, timeout=60):
            steps[role] += 1
            start = len(read(role))
            name = f'mix_cmd_{steps[role]:05d}.cfg'
            write_script(games[role] / name, [f'echo {marker}_BEGIN', *lines, f'echo {marker}', 'waitMsec 2000'])
            write_script(games[role] / 'mix_step.cfg', ['exec ' + name])
            begun = wait_for(lambda logs: f'\n{marker}_BEGIN' in logs[role][start:], timeout)
            write_script(games[role] / 'mix_step.cfg', ['// idle'])
            return begun and wait_for(lambda logs: f'\n{marker}' + '\n' in logs[role][start:], timeout)

        record = {'scenario': scenario['name'], 'pure': scenario['pure'], 'versions': roles, 'joined': False}
        try:
            launch('server')
            record['server_ready'] = wait_for(lambda logs: 'MP_STATE' in logs['server'], args.join_timeout)
            launch('talker')
            record['talker_joined'] = wait_for(lambda logs: re.search(r'MP_PLAYER slot=0 .*ingame=1', logs['server']) is not None, args.join_timeout)
            guest_slot = 0
            if 'guest' in roles:
                launch('guest')
                guest_slot = 1
                record['joined'] = wait_for(lambda logs: re.search(r'MP_PLAYER slot=1 .*ingame=1', logs['server']) is not None, args.join_timeout)
            else:
                record['joined'] = record['talker_joined']
            if record['joined']:
                if roles['server'] == 'new':
                    step('server', ['set g_voiceChatDebug 2'], 'MIX_SERVER_DEBUG')
                step('talker', ['set g_voiceChatDebug 1'], 'MIX_TALKER_DEBUG')
                for burst in range(3):
                    step('talker', ['testVoiceChat 3', 'waitMsec 3500', 'listVoiceChat'], f'MIX_BURST_{burst}', timeout=90)
                time.sleep(3)
        except Exception as exc:
            record['error'] = f'{type(exc).__name__}: {exc}'
        finally:
            for role, process in processes.items():
                if process.poll() is None:
                    write_script(games[role] / 'mix_step.cfg', ['quit'])
            for process in processes.values():
                try:
                    process.wait(timeout=30)
                except subprocess.TimeoutExpired:
                    process.kill()
                    try:
                        process.wait(timeout=30)
                    except subprocess.TimeoutExpired:
                        pass
            for stream in streams:
                stream.close()
        texts = {role: read(role) for role in roles}
        record['exit_codes'] = {role: process.returncode for role, process in processes.items()}
        # A refused client never loads a map, so its console loop never starts
        # and the harness must stop it; only processes that ran the loop can quit.
        record['ran_console_loop'] = {role: 'AutoExecAfterMapLoad: executed' in text for role, text in texts.items()}
        record['refusal'] = {role: reason for role, text in texts.items() for reason in (
            'missing pure paks' if re.search(r'missing \d+ paks?:', text) else None,
            'decl checksum mismatch' if 'not in sync with server data' in text else None) if reason}
        record['server_refusal'] = sorted(set(re.findall(r'Decl checksum mismatch from \S+ (client=0x[0-9a-f]+ server=0x[0-9a-f]+ \((?:non-)?pure\))', texts['server'])))
        record['unknown_unreliable'] = {role: len(UNKNOWN.findall(text)) for role, text in texts.items()}
        record['talker_sends'] = len(re.findall(r'^VOICE_SEND bytes=\d+ sent=1', texts['talker'], re.M))
        record['talker_captured'] = max([int(value) for value in re.findall(r'^VOICE_STATUS .*?captured=(\d+)', texts['talker'], re.M)] or [0])
        record['server_forwards'] = {f'to_{slot}': len(re.findall(rf'^VOICE_FORWARD from=\d+ to={slot} accepted=1', texts['server'], re.M))
                                     for slot in (0, 1)}
        record['server_build'] = (re.search(r'^openQ4 [^\n]*', texts['server'], re.M) or [''])[0][:160]
        record['rejection_lines'] = {role: sorted(set(match[0][:160] for match in REJECTION.finditer(text)))[:12] for role, text in texts.items()
                                     if role != 'server' or not record['joined']}
        results.append(record)
        print(json.dumps({key: record[key] for key in ('scenario', 'joined', 'refusal', 'unknown_unreliable', 'talker_sends', 'server_forwards')}), flush=True)

    checks = {}
    for record in results:
        name = record['scenario']
        # Whatever the join outcome, an old peer must never be handed the new
        # message type and must never log it, and a new client must never
        # transmit voice to a server that does not advertise the protocol.
        checks[f'{name}:no_unknown_unreliable'] = all(count == 0 for count in record['unknown_unreliable'].values())
        if record['versions']['server'] == 'old':
            checks[f'{name}:new_client_silent_to_old_server'] = record['talker_sends'] == 0
        elif record['joined']:
            checks[f'{name}:old_client_not_forwarded'] = record['server_forwards']['to_1'] == 0
        if not record['joined']:
            # Not joining is the expected outcome across releases; it must be
            # an explicit, recorded refusal rather than a hang or a crash.
            checks[f'{name}:explicit_refusal'] = bool(record['refusal'])
        checks[f'{name}:clean_exit'] = all(code == 0 for role, code in record['exit_codes'].items() if record['ran_console_loop'][role])
    report = {'passed': all(checks.values()), 'checks': checks, 'scenarios': results, 'old_release': old_version,
              'old_sha256': {f'{version}-{kind}': hashlib.sha256(path.read_bytes()).hexdigest() for (version, kind), path in binaries.items()},
              'old_game_mp_sha256': game_module_sha256(old_runtime) if os.name == 'nt' else None,
              'new_game_mp_sha256': game_module_sha256(runtime) if os.name == 'nt' else None,
              'microphone': False, 'host_input': False,
              'harness_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest()}
    (output / 'result.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({'result': str(output / 'result.json'), 'checks': checks}, indent=2))
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
