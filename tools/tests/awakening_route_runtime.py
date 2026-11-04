#!/usr/bin/env python3
"""Interactive, file-driven SP route investigation; never certifies a route itself.

Write {"id": N, "commands": [...]} atomically to OUTPUT/request.json. Each
request is acknowledged in the engine log and retained in commands.jsonl.
Requires explicit user authorization before using test input. Only one controller
may submit requests to a session at a time. Game time pauses between requests;
explicit waits and input advance gameplay without tool latency stalling escorts.
"""
import argparse
import hashlib
import json
import math
import re
from pathlib import Path
import subprocess
import sys
import time
from campaign_runtime import ROOT, task_path
from savegame_cross_load import launch_command, client_path, stop_process, stage_input, CFG_REL, LOG_NAME
from mp_round_remote_smoke import verify_runtime_is_current, write_script


def read_json(path):
    try:
        return json.loads(path.read_text(encoding='utf-8-sig'))
    except (OSError, json.JSONDecodeError):
        return None


def requester(out):
    ack = out/'ack.json'
    seq = (read_json(ack) or {'id': 0})['id']
    pending = read_json(out/'request.json')
    if pending and pending['id'] > seq:
        raise RuntimeError('another route request is still pending')
    def request(commands):
        nonlocal seq
        seq += 1
        write_script(out/'request.json', [json.dumps({'id': seq, 'commands':
            [f'echo ROUTE_STEP_{seq}', 'set g_stopTime 0']+commands+['set g_stopTime 1']})])
        deadline = time.monotonic()+90
        while time.monotonic() < deadline:
            if (out/'failure.txt').exists():
                raise RuntimeError((out/'failure.txt').read_text())
            try:
                acknowledged = ack.exists() and json.loads(ack.read_text())['id'] == seq
            except (OSError, json.JSONDecodeError):
                acknowledged = False  # Windows may briefly hold the replaced file.
            if acknowledged:
                return (out/'q4xbase/logs'/LOG_NAME).read_text(errors='replace').split(f'ROUTE_STEP_{seq} ')[-1]
            time.sleep(.1)
        raise RuntimeError('route driver acknowledgement timed out')
    return request


def follow(out, goals, direct=False):
    """Follow AAS suggestions using bounded normal input; stop on obstruction."""
    request = requester(out)
    history_path = out/'navigation.json'
    history = json.loads(history_path.read_text()) if history_path.exists() else []
    try:
        for goal in goals:
            request(['saveGame route_leg_start'])
            stalled = 0
            best_distance = float("inf")
            for step in range(240):
                log = request([f'testMission path {goal}', 'testMission player1', 'openq4_viewReport', f'testMission {goal}'])
                if f'testMission: missing={goal}' in log:
                    if step == 0:
                        raise RuntimeError(f'{goal} was absent before approach')
                    break
                path = re.search(r'testMissionPath: goal=\S+ next=\(([^)]+)\) distance=([\d.]+)', log)
                state = re.search(r'name=player1 health=(-?\d+).*origin=\(([^)]+)\)', log)
                view = re.search(r'openq4_viewReport:.* angles ([\d.\-]+) ([\d.\-]+)', log)
                # Brush-trigger origins may sit above their traversable volume,
                # mapping onto an unrelated AAS floor. A nearby straight approach
                # still uses player collision and stops if it cannot make progress.
                target = re.search(r'name='+re.escape(goal)+r' health=.*origin=\(([^)]+)\)', log)
                if direct:
                    path = None
                if not path and state and target:
                    player_pos = tuple(map(float, state[2].split()))
                    goal_pos = tuple(map(float, target[1].split()))
                    distance = math.dist(player_pos[:2], goal_pos[:2])
                    if distance < 512 and abs(goal_pos[2] - player_pos[2]) <= 128:
                        path = [None, f'{goal_pos[0]} {goal_pos[1]} {player_pos[2]}', str(distance)]
                if not path or not state or not view or int(state[1]) <= 0:
                    raise RuntimeError(f'{goal}: missing path or live player')
                pos = tuple(map(float, state[2].split()))
                point = tuple(map(float, path[1].split()))
                history.append({'goal':goal, 'position':pos, 'next':point, 'distance':float(path[2])})
                if float(path[2]) < 20:
                    break
                distance = float(path[2])
                if distance < best_distance - 2:
                    stalled, best_distance = 0, distance
                else:
                    stalled += 1
                if stalled >= 8:
                    raise RuntimeError(f'{goal}: physically blocked at {pos}')
                # Conservative bursts avoid overshooting narrow catwalk turns.
                # Re-query collision/AAS after each half-second at most. The
                # minimum burst permits acceleration through close AAS corners.
                duration = max(80, min(500, int(math.dist(pos[:2],point[:2])/160*1000)))
                # A temporary non-solid point gives the existing aim input a
                # waypoint. It is removed before movement/saving; no actor pose
                # or collision geometry is changed. Yaw-rate input alone resets
                # when released, so cannot establish a persistent heading.
                request([f'spawn target_null name oq4_route_aim origin "{point[0]} {point[1]} {pos[2]+68}"',
                         'openq4_testInput 48 aim oq4_route_aim', 'wait 5',
                         'openq4_testInput 0', 'centerview', 'remove oq4_route_aim',
                         f'openq4_testInput {duration} forward 127', f'wait {math.ceil(duration/16)+3}',
                         'openq4_testInput 0'])
            else:
                raise RuntimeError(f'{goal}: approach step limit')
            request([f'echo ROUTE_CHECKPOINT {goal}', 'testMission tak', 'testMission darnell',
                     'testMission navajas', 'wait 180'])
    finally:
        history_path.write_text(json.dumps(history,indent=2))
        if not (out/'failure.txt').exists():
            request(['openq4_testInput 0', 'saveGame route_progress'])


def drive(out, vehicle, goals):
    """Use the walker's ordinary steering mode; stop on collision or no progress."""
    request = requester(out)
    request(['set g_vehicleMode 0'])
    history = []
    try:
        for goal in goals:
            last_pos, stalled = None, 0
            for step in range(180):
                log = request([f'testMission {vehicle}', f'testMission {goal}', 'openq4_viewReport'])
                if 'vehicle 1' not in log:
                    raise RuntimeError('player is no longer driving')
                if f'testMission: missing={goal}' in log:
                    if step == 0:
                        raise RuntimeError(f'{goal} was absent before approach')
                    break
                state = re.search(r'name='+re.escape(vehicle)+r' health=(-?\d+).*origin=\(([^)]+)\)', log)
                target = re.search(r'name='+re.escape(goal)+r' health=.*origin=\(([^)]+)\)', log)
                axis = re.search(r'testMissionAxis: name='+re.escape(vehicle)+r' angles=\(([^)]+)\)', log)
                if not state or not target or not axis or int(state[1]) <= 0:
                    raise RuntimeError('missing live vehicle, orientation or goal')
                pos = tuple(map(float, state[2].split()))
                dest = tuple(map(float, target[1].split()))
                distance = math.dist(pos[:2], dest[:2])
                yaw = float(axis[1].split()[1])
                error = (math.degrees(math.atan2(dest[1]-pos[1], dest[0]-pos[0]))-yaw+180)%360-180
                history.append({'goal':goal, 'position':pos, 'yaw':yaw, 'distance':distance})
                if distance < 60:
                    break
                if abs(error) > 3:
                    duration = max(32, min(500, int(abs(error)/120*1000)))
                    control = 'left' if error > 0 else 'right'
                    request([f'openq4_testInput {duration} {control} 127', f'wait {math.ceil(duration/16)+3}', 'openq4_testInput 0'])
                    continue
                stalled = stalled + 1 if last_pos and math.dist(pos, last_pos) < 2 else 0
                if stalled >= 8:
                    raise RuntimeError(f'{goal}: walker physically blocked at {pos}')
                last_pos = pos
                duration = max(100, min(1000, int((distance-45)/100*1000)))
                request([f'openq4_testInput {duration} forward 127', f'wait {math.ceil(duration/16)+3}', 'openq4_testInput 0'])
            else:
                raise RuntimeError(f'{goal}: walker approach step limit')
            request([f'echo ROUTE_DRIVEN {goal}', 'wait 120'])
    finally:
        with (out/'driving.jsonl').open('a') as stream:
            for entry in history:
                stream.write(json.dumps(entry)+'\n')
        request(['openq4_testInput 0', 'saveGame route_progress'])


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--map', required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--retail', type=Path, required=True)
    p.add_argument('--awakening', type=Path, required=True)
    p.add_argument('--runtime-dir', type=Path, default=ROOT/'.install',
                   help='fixed staged runtime directory; defaults to the project install')
    p.add_argument('--timeout', type=int, default=1800)
    p.add_argument('--input', type=Path)
    p.add_argument('--stop', action='store_true', help='stop an idle controller session after preserving its checkpoints; does not certify the route')
    p.add_argument('--follow', nargs='+', help='drive an already running authorized session toward these entities')
    p.add_argument('--direct', action='store_true', help='use collision-respecting straight approaches within 512 units, without AAS guidance')
    p.add_argument('--drive', nargs='+', help='drive an occupied walker toward named waypoints using normal steering input')
    p.add_argument('--vehicle', default='m04_maint_tunnel_unit1_uni_vehicle_walker_player')
    p.add_argument('--commands', nargs='+', help='send engine commands to an existing session and wait for acknowledgement')
    p.add_argument('--shoot', nargs='+', help='aim and fire normally at named targets until dead, stopping on no damage')
    p.add_argument('--refill-ammo', action='store_true', help='refill ammo between firing bursts; excludes ammunition/balance qualification')
    p.add_argument('--aim-height', type=float,
                   help='aim at this height above the target origin using short bursts, for authored weak points')
    a = p.parse_args()
    out = task_path(a.output)
    if a.stop:
        requester(out)  # Refuse to interrupt an unacknowledged request.
        write_script(out/'stop.json', [json.dumps({'reason': 'Checkpointed session stopped by controller'})])
        return 0
    if a.drive:
        drive(out, a.vehicle, a.drive)
        return 0
    if a.commands:
        print(requester(out)(a.commands))
        return 0
    if a.shoot:
        request = requester(out)
        try:
            for target in a.shoot:
                last_health, stalled = None, 0
                for step in range(60):
                    log = request([f'testMission {target}'])
                    state = re.search(r'name='+re.escape(target)+r' health=(-?\d+) hidden=(\d)', log)
                    if not state or int(state[1]) <= 0:
                        outcome = 'absent before firing (no kill claim)' if not state and step == 0 else 'dead or removed after observation'
                        print(f'{target}: {outcome}', flush=True)
                        break
                    if state[2] == '1':
                        raise RuntimeError(f'{target}: still hidden')
                    health = int(state[1])
                    stalled = stalled+1 if health == last_health else 0
                    if stalled >= 5:
                        raise RuntimeError(f'{target}: shots are not damaging it')
                    last_health = health
                    commands = ['give ammo'] if a.refill_ammo else []
                    if a.aim_height is not None:
                        origin = re.search(r'origin=\(([^)]+)\)', log)
                        if not origin:
                            raise RuntimeError(f'{target}: missing aim origin')
                        x, y, z = map(float, origin[1].split())
                        commands += [f'spawn target_null name oq4_shoot_aim origin "{x} {y} {z+a.aim_height}"',
                                     'openq4_testInput 500 attack aim oq4_shoot_aim', 'wait 35',
                                     'openq4_testInput 0', 'remove oq4_shoot_aim']
                    else:
                        commands += [f'openq4_testInput 2000 attack aim {target}', 'wait 130', 'openq4_testInput 0']
                    request(commands)
                else:
                    raise RuntimeError(f'{target}: firing step limit')
        finally:
            request(['openq4_testInput 0', 'saveGame route_progress'])
        return 0
    if a.follow:
        follow(out, a.follow, a.direct)
        return 0
    runtime = a.runtime_dir.resolve()
    exe = client_path(runtime)
    verify_runtime_is_current(runtime, exe.name)
    out.mkdir(parents=True, exist_ok=False)
    game = out/'q4xbase'
    cfg = game/CFG_REL
    cfg.parent.mkdir(parents=True)
    if a.input:
        stage_input(a.input.resolve(), game/'savegames', 'route_in')
    poll = game/'route_poll.cfg'
    # Keep the recursive loop immutable: Windows can transiently deny an exec
    # while its command file is replaced. A missed dispatch must not end polling.
    poll.write_text('wait 15\nexec route_dispatch.cfg\nwait 15\nexec route_poll.cfg\n')
    dispatch = game/'route_dispatch.cfg'
    idle = '// no request\n'
    dispatch.write_text(idle)
    cfg.write_text('set g_autoExecAfterMapLoad ""\nset g_stopTime 1\nexec route_poll.cfg\n')
    opts = argparse.Namespace(mode='load' if a.input else 'write', basepath=a.retail.resolve(), game='q4xbase',
        tag='route', map=a.map, xvfb=False, set=[
        ('fs_awakeningpath', str(a.awakening.resolve())), ('fs_devpath', str(out)),
        ('fs_cachepath', str(ROOT/'.tmp/awakening-m01-m02/cache')),
        ('g_autoSkipCinematics', '0'), ('com_gameMode', 'SP'), ('ui_autoJoin', '0'),
        ('ui_retained', '1'), ('sys_consoleWindow', '0')])
    cmd = launch_command(opts, exe, out)
    (out/'launch.json').write_text(json.dumps(cmd, indent=2))
    (out/'binaries.json').write_text(json.dumps({str(path): hashlib.sha256(path.read_bytes()).hexdigest()
        for path in (exe, exe.parent/'baseoq4/game-sp_x64.dll', exe.parent/'baseoq4/pak0.pk4')}, indent=2))
    startup = None
    if sys.platform == 'win32':
        startup = subprocess.STARTUPINFO()
        startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        startup.wShowWindow = subprocess.SW_HIDE
    def publish(text):
        write_script(dispatch, text.splitlines())
    last = None
    pending = None
    log_offset = 0
    log_tail = ''
    initial_load = True
    stopped = False
    start = time.monotonic()
    with (out/'stdout.log').open('w') as stream:
        proc = subprocess.Popen(cmd, cwd=exe.parent, stdin=subprocess.DEVNULL,
            stdout=stream, stderr=subprocess.STDOUT, startupinfo=startup,
            start_new_session=sys.platform != 'win32')
        try:
            while proc.poll() is None and time.monotonic()-start < a.timeout:
                if (out/'stop.json').exists():
                    stopped = True
                    (out/'session.json').write_text(json.dumps({'status': 'stopped', 'route_verdict': None}))
                    break
                logpath = game/'logs'/LOG_NAME
                chunk = b''
                if logpath.exists():
                    if logpath.stat().st_size < log_offset:
                        log_offset, log_tail = 0, ''
                    with logpath.open('rb') as engine_log:
                        engine_log.seek(log_offset)
                        chunk = engine_log.read()
                        log_offset = engine_log.tell()
                log = log_tail + chunk.decode('utf-8', errors='replace')
                log_tail = log[-1024:]
                if ('could not restore saved state' in log or
                    (a.input and initial_load and 'loadingSaveGame=0' in log.split('AutoExecAfterMapLoad:', 1)[0]) or
                    re.search(r'(?mi)^(?:\^\d)?(?:ERROR:|FATAL ERROR:)', log)):
                    (out/'failure.txt').write_text('Engine load/runtime failure; see engine log.')
                    break
                if 'AutoExecAfterMapLoad:' in log:
                    initial_load = False
                if pending is not None and f'ROUTE_ACK_{pending} ' in log:
                    publish(idle)
                    write_script(out/'ack.json', [json.dumps({'id':pending})])
                    pending = None
                request = out/'request.json'
                if pending is None and request.exists():
                    data = read_json(request)
                    if data is None:
                        time.sleep(.05)
                        continue
                    if last is None or int(data['id']) > last:
                        last = pending = int(data['id'])
                        with (out/'commands.jsonl').open('a') as history:
                            history.write(json.dumps(data)+'\n')
                        publish('\n'.join(data['commands'])+f'\necho ROUTE_ACK_{pending}\n'+idle)
                time.sleep(.05)
            if not stopped and not (out/'failure.txt').exists():
                reason = 'Session time limit reached' if proc.poll() is None else f'Engine exited ({proc.returncode})'
                (out/'failure.txt').write_text(reason + '; this investigation does not certify route completion.')
        except BaseException as error:
            (out/'failure.txt').write_text(f'Test controller error: {error}')
            raise
        finally:
            stop_process(proc)
    return 1 if (out/'failure.txt').exists() else 0


if __name__ == '__main__':
    raise SystemExit(main())
