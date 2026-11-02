#!/usr/bin/env python3
"""Qualify multiplayer air authority, spectator readout and MVD replay without input."""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import time

from mp_round_remote_smoke import read_log, verify_runtime_is_current, write_script

ROOT = Path(__file__).resolve().parents[2]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runtime-dir', type=Path, default=ROOT / '.install')
    parser.add_argument('--basepath', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--renderer', choices=('gl', 'vulkan'), default='gl')
    parser.add_argument('--hud-layout', type=int, choices=range(4), default=1)
    parser.add_argument('--retained', type=int, choices=(0,1), default=1)
    parser.add_argument('--port', type=int, default=28978)
    args = parser.parse_args()
    runtime, output = args.runtime_dir.resolve(), args.output_dir.resolve()
    verify_runtime_is_current(runtime)
    exe = runtime / ('openQ4-client_x64.exe' if os.name == 'nt' else 'openQ4-client_x64')
    if output.exists() or not exe.is_file() or not (args.basepath / 'q4base').is_dir():
        parser.error('use a new output directory, staged client and installed retail assets')
    output.mkdir(parents=True)
    paths = [exe, *sorted((runtime / 'baseoq4').glob('game-mp*')), *sorted(runtime.glob('renderer-*')),
             runtime / 'baseoq4/pak0.pk4', runtime / 'baseoq4/pak1.pk4', runtime / 'baseoq4/mod.json']
    paths = [p for p in paths if p.is_file() and p.suffix not in ('.pdb', '.lib', '.exp')]
    def identity():
        return {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}
    before = identity()
    games = {role: output / role / 'baseoq4' for role in ('host', 'observer')}
    for game in games.values():
        game.mkdir(parents=True)
    host = ['set g_autoExecAfterMapLoad ""', 'notarget', 'god', 'set g_debugLiquid 1',
            'addbot Sledge 2 exact', 'echo LIQUID_AIR_HOST_READY', 'waitMsec 8000',
            'recordMVD air_state', 'echo LIQUID_AIR_DRY', 'openq4_reportMPState', 'waitMsec 2000',
            'setviewpos -720 420 -200 0', 'waitMsec 3000', 'echo LIQUID_AIR_SUBMERGED',
            'openq4_reportMPState', 'ui_hudTest guis/hud.gui get player_air',
            's_soundWorlds', 'screenshot screenshots/submerged.tga',
            'waitMsec 4000', 'echo LIQUID_AIR_DRAINED', 'openq4_reportMPState',
            'setviewpos 0 0 128 0', 'waitMsec 3000', 'echo LIQUID_AIR_SURFACED',
            'openq4_reportMPState', 's_soundWorlds', 'screenshot screenshots/surfaced.tga',
            'waitMsec 1500', 'stopMVD', 'disconnect', 'playMVD air_state', 'waitMsec 2000',
            'mvdFollow 0', 'mvdPause 1']
    for i, seconds in enumerate((1, 7, 1, 7)):
        host += [f'mvdSeek {seconds}', 'waitMsec 1500', f'echo LIQUID_AIR_SEEK {i}',
                 'mvdStatus', 'openq4_reportMPState', 'ui_hudTest guis/hud.gui get player_air',
                 f'screenshot screenshots/seek_{i}.tga']
    host += ['stopMVD', 'echo LIQUID_AIR_HOST_COMPLETE', 'quit']
    observer = ['set g_autoExecAfterMapLoad ""', 'follow 0']
    for i in range(20):
        observer += ['waitMsec 1000', f'echo LIQUID_AIR_REMOTE {i}', 'openq4_reportMPState']
        if i in (7, 12):
            observer += [f'screenshot screenshots/remote_{i}.tga']
    observer += ['echo LIQUID_AIR_OBSERVER_COMPLETE', 'quit']
    write_script(games['host'] / 'air_smoke.cfg', host)
    write_script(games['observer'] / 'air_smoke.cfg', observer)
    processes, streams = {}, []
    def launch(role):
        game = games[role]
        cvars = {'win_allowMultipleInstances' if os.name == 'nt' else 'sys_allowMultipleInstances':'1',
            'sys_consoleWindow':'0', 'in_tty':'0', 'in_mouse':'0', 'in_joystick':'0',
            'r_fullscreen':'0', 'r_borderless':'0', 'r_fullscreenDesktop':'0', 'r_borderlessDefaultMigrated':'1',
            'r_hiddenWindow':'1', 'r_renderApi':args.renderer, 'r_mode':'-1', 'r_customWidth':'960', 'r_customHeight':'540',
            'r_windowWidth':'960', 'r_windowHeight':'540', 'r_swapInterval':'0', 'com_maxfps':'60', 's_noSound':'1',
            'ui_retained':str(args.retained), 'ui_hudLayout':str(args.hud_layout), 'ui_autoJoin':'1',
            'ui_spectate':'Play' if role=='host' else 'Spectate',
            'ui_name':'AirHost' if role=='host' else 'AirObserver', 'fs_game':'baseoq4', 'com_gameMode':'MP',
            'com_nextGameModule':'game_mp', 'fs_basepath':str(args.basepath.resolve()),
            'fs_savepath':str(game.parent), 'fs_devpath':str(game.parent),
            'logFile':'2', 'logFileName':'logs/openq4.log', 'developer':'1', 'net_allowCheats':'1',
            'net_serverDedicated':'0', 'net_LANServer':'1', 'net_port':str(args.port if role=='host' else args.port+1),
            # Loopback only: no firewall prompt for an unregistered runtime path.
            'net_ip':'127.0.0.1', 'net_enableIPv6':'0',
            'si_pure':'0', 'si_gameType':'DM', 'si_minPlayers':'1', 'si_maxPlayers':'4',
            'si_warmup':'0', 'si_useReady':'0', 'si_fragLimit':'999', 'si_timeLimit':'0', 'g_matchProfile':'casual',
            'bot_minPlayers':'0', 'bot_chat':'0', 'com_skipLoadingContinue':'1',
            'g_autoExecAfterMapLoad':'air_smoke.cfg', 'g_autoExecAfterMapLoadDelayMs':'500'}
        assert len(cvars)+1 <= 64
        command = [str(exe), *[part for k,v in cvars.items() for part in ('+set',k,v)]]
        command += ['+spawnServer','mp/liquid_lab'] if role=='host' else ['+connect',f'127.0.0.1:{args.port}']
        (output / f'{role}-launch.json').write_text(json.dumps(command,indent=2))
        stream = (output / f'{role}-console.log').open('w'); streams.append(stream)
        processes[role] = subprocess.Popen(command,cwd=runtime,stdout=stream,stderr=subprocess.STDOUT,
            creationflags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0)
    failures = []
    try:
        launch('host')
        until = time.monotonic()+60
        while 'LIQUID_AIR_HOST_READY' not in read_log(games['host']/'logs/openq4.log'):
            if time.monotonic()>until or processes['host'].poll() is not None:
                raise RuntimeError('host did not enter the liquid lab')
            time.sleep(.25)
        launch('observer')
        for role in ('observer','host'):
            try:
                if processes[role].wait(timeout=150) != 0:
                    failures.append(f'{role} exited abnormally')
            except subprocess.TimeoutExpired:
                failures.append(f'{role} timed out')
    except (OSError,RuntimeError) as error:
        failures.append(str(error))
    finally:
        for process in processes.values():
            if process.poll() is None:
                process.kill(); process.wait()
        for stream in streams:
            stream.close()
    logs = {role:re.sub(r'\^[0-9A-Za-z]','',read_log(game/'logs/openq4.log')) for role,game in games.items()}
    remote = re.findall(r'MP_AIR slot=0 reservoir=(\d+) percent=(\d+) serverTime=(-?\d+)',logs['observer'])
    if len(remote)<5 or not any(int(p)<80 and int(t)>0 for _,p,t in remote):
        failures.append('remote spectator did not receive the host air authority')
    seeks = []
    hud_values = []
    for i in range(4):
        body = logs['host'].partition(f'LIQUID_AIR_SEEK {i}')[2].partition('LIQUID_AIR_SEEK ')[0]
        match = re.search(r'MP_AIR slot=0 reservoir=(\d+) percent=(\d+) serverTime=(-?\d+)',body)
        seeks.append(int(match[2]) if match else None)
        visible = re.findall(r'HUD_STATE source=guis/hud.gui key=player_air value=(\d+)',body)
        hud_values.append(int(visible[-1]) if visible else None)
    if None in seeks or not (seeks[0]==seeks[2]==100 and seeks[1]==seeks[3] and seeks[1]<80):
        failures.append(f'paused air seek replay lost state: {seeks}')
    if hud_values != seeks:
        failures.append(f'follow HUD air does not match the restored authority: {hud_values} versus {seeks}')
    for shot in ('submerged', 'surfaced', 'seek_0', 'seek_1', 'seek_2', 'seek_3'):
        path = games['host']/f'screenshots/{shot}.tga'
        if not path.is_file() or path.stat().st_size < 1024:
            failures.append(f'missing engine capture: {shot}')
    for role,log in logs.items():
        if f'LIQUID_AIR_{role.upper()}_COMPLETE' not in log or re.search(r'ERROR:|FATAL:|Unknown command',log):
            failures.append(f'{role} did not complete cleanly')
        if not re.search(rf'Renderer API: {args.renderer} \(',log):
            failures.append(f'{role} renderer did not activate')
    if identity()!=before:
        failures.append('runtime changed during the workflow')
    report = {'status':'fail' if failures else 'pass','identity':before,'remote_air':remote,'paused_seek_air':seeks,
              'hud_air':hud_values,'hud_layout':args.hud_layout,'retained':args.retained,'renderer':args.renderer,
              'failures':failures,'logs':{role:str(game/'logs/openq4.log') for role,game in games.items()}}
    (output/'report.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({'status':report['status'],'failures':failures,'report':str(output/'report.json')},indent=2))
    return bool(failures)


if __name__=='__main__':
    raise SystemExit(main())
