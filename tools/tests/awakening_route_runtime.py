#!/usr/bin/env python3
"""Interactive, file-driven SP route investigation; never certifies a route itself.

Write {"id": N, "commands": [...]} atomically to OUTPUT/request.json. Each
request is acknowledged in the engine log and retained in commands.jsonl.
Requires explicit user authorization before using test input. Only one controller
may submit requests to a session at a time. Game time pauses between requests
by default; --no-pause-between-requests enables continuous combat timing.
"""
import argparse
import hashlib
import json
import math
import os
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


def requester(out, timeout=180):
    settings = read_json(out/'controller.json')
    cfg = out/'q4xbase'/CFG_REL
    pause = settings['pause_between_requests'] if settings else (cfg.exists() and 'set g_stopTime 1' in cfg.read_text())
    ack = out/'ack.json'
    seq = (read_json(ack) or {'id': 0})['id']
    pending = read_json(out/'request.json')
    if pending and pending['id'] > seq:
        raise RuntimeError('another route request is still pending')
    def request(commands):
        nonlocal seq
        logpath = out/'q4xbase/logs'/LOG_NAME
        log = logpath.read_text(errors='replace') if logpath.exists() else ''
        if 'ROUTE_STEP_' in log and log.rfind('Game Map Shutdown') > log.rfind('Map Initialization') and not any(
                command.split()[0].lower() in ('loadgame', 'quit') for command in commands if command.split()):
            raise RuntimeError('No active map; restore a checkpoint before route input')
        seq = max(seq, (read_json(ack) or {'id': 0})['id'])
        seq += 1
        payload = [f'echo ROUTE_STEP_{seq}'] + (['set g_stopTime 0'] if pause else []) + commands
        if pause:
            payload += ['set g_stopTime 1']
        write_script(out/'request.json', [json.dumps({'id': seq, 'commands': payload})])
        deadline = time.monotonic()+timeout
        while time.monotonic() < deadline:
            if (out/'failure.txt').exists():
                raise RuntimeError((out/'failure.txt').read_text())
            if (out/'session.json').exists():
                raise RuntimeError('Route session has ended')
            try:
                acknowledged = ack.exists() and json.loads(ack.read_text())['id'] == seq
            except (OSError, json.JSONDecodeError):
                acknowledged = False  # Windows may briefly hold the replaced file.
            if acknowledged:
                return (out/'q4xbase/logs'/LOG_NAME).read_text(errors='replace').split(f'ROUTE_STEP_{seq} ')[-1]
            time.sleep(.1)
        raise RuntimeError(f'route request {seq} acknowledgement timed out; inspect the pending request before continuing')
    return request


def select_weapon(request, slot, movement=''):
    """Verify selection and raise completion before a measured firing burst."""
    duration = 800 if movement else 250
    log = request([f'openq4_testInput {duration} impulse {slot} {movement}', 'wait 52',
                   'openq4_testInput 0', 'testMission player1'])
    state = re.search(r'testMissionPlayer:.* weapon=(-?\d+)', log)
    if not state or int(state[1]) != slot:
        raise RuntimeError(f'weapon {slot} was not selected; combat attempt is unqualified')
    # A restored/switching weapon can report its slot before its Raise state
    # accepts fire or reload. Keep optional evasive movement during this wait.
    for _ in range(24):
        player = re.search(r'testMission: name=player1 health=(-?\d+)', log)
        if not player or int(player[1]) <= 0:
            raise RuntimeError('player died during weapon selection; combat attempt is unqualified')
        weapon = re.search(r'testMissionWeapon:.* ready=(\d+)', log)
        if weapon and weapon[1] == '1':
            return
        log = request(([f'openq4_testInput 160 {movement}'] if movement else []) +
                      ['wait 10', 'openq4_testInput 0', 'testMission player1'])
    raise RuntimeError(f'weapon {slot} did not become ready; combat attempt is unqualified')


def check_input_restore(out):
    """Run in a quiet checkpoint with weapons 1 and 3; no combat qualification."""
    request = requester(out)
    select_weapon(request, 1)
    request(['openq4_testInput 0', 'saveGame route_input_selection', 'wait 5'])
    checks = []
    for slot in (None, 3, 3):
        if slot is not None:
            select_weapon(request, slot)
        log = request(['loadGame route_input_selection', 'wait 50', 'testMission player1'])
        state = re.search(r'testMissionPlayer:.* weapon=(-?\d+)', log)
        checks.append(dict(selected_before_restore=slot, restored=int(state[1]) if state else None))
        if not state or int(state[1]) != 1:
            break
    passed = len(checks) == 3 and all(check['restored'] == 1 for check in checks)
    report = dict(passed=passed, checks=checks, qualification=False)
    (out/'input-restore.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report))
    return int(not passed)


def follow(out, goals, direct=False, engage=False, jump=False, strafe=False):
    """Follow AAS suggestions using bounded normal input; stop on obstruction."""
    request = requester(out)
    history_path = out/'navigation.json'
    history = json.loads(history_path.read_text()) if history_path.exists() else []
    bursts = 0
    try:
        for goal in goals:
            request(['saveGame route_leg_start'])
            last_pos = None
            stalled = 0
            jumps = 0
            recent_distances = []
            for step in range(240):
                log = request([f'testMission path {goal}', 'testMission player1', 'openq4_viewReport', f'testMission {goal}', 'testMission enemies'])
                state = re.search(r'name=player1 health=(-?\d+).*origin=\(([^)]+)\)', log)
                if not state or int(state[1]) <= 0:
                    raise RuntimeError(f'{goal}: player missing or dead')
                if engage:
                    enemies = re.findall(r'testMissionEnemy: name=(\S+) health=(\d+) distance=([\d.]+) visible=1', log)
                    # Scripted Valkaryne appearances and repair bots are not
                    # route combat targets. The final boss needs its own phase
                    # observations rather than generic corridor fire.
                    enemies = [e for e in enemies if not e[0].startswith('valk') and not
                               re.search(r'testMissionEnemy: name='+re.escape(e[0])+r' .* def=monster_repair_bot(?:\s|$)', log)]
                    if enemies:
                        target, health, distance = min(enemies, key=lambda e: (not bool(re.search(r'testMissionEnemy: name='+re.escape(e[0])+r' .* def=\S*railgun', log)), float(e[2])))
                        ammo = dict((name, int(value)) for name, value in re.findall(r'testMissionAmmo: type=(\w+) amount=(\d+)', log))
                        owned = re.search(r'testMissionPlayer:.* weapons=(\d+)', log)
                        weapons = int(owned[1]) if owned else 1
                        rail_threat = bool(re.search(r'testMissionEnemy: name='+re.escape(target)+r' .* def=\S*railgun', log))
                        heavy = bool(re.search(r'testMissionEnemy: name='+re.escape(target)+r' .* def=\S*(?:iron_maiden|tank)(?:\s|$)', log))
                        if rail_threat and ammo.get('ammo_railgun', 0):
                            choices = [(7, 'railgun')]
                        elif (len(enemies) >= 3 or heavy) and float(distance) > 250 and ammo.get('ammo_rocketlauncher', 0) > 2:
                            choices = [(6, 'rocketlauncher')]
                        else:
                            choices = [(7, 'railgun')] if float(distance) > 200 and ammo.get('ammo_railgun', 0) > 4 else [(3, 'hyperblaster'), (1, 'machinegun')]
                        choices += [(1, 'machinegun'), (2, 'shotgun'), (0, 'blaster')]
                        weapon = next(slot for slot, name in choices if weapons & (1 << slot) and (slot == 0 or ammo.get('ammo_'+name, 0)))
                        movement = (' right 32' if bursts % 2 else ' left 32') if strafe else ''
                        current = re.search(r'testMissionPlayer:.* weapon=(-?\d+)', log)
                        if not current or int(current[1]) != weapon:
                            select_weapon(request, weapon, f'{movement} aim {target}' if movement else '')
                        aim = 'aimEye' if rail_threat or re.search(r'testMissionEnemy: name='+re.escape(target)+r' .* aimEye=1', log) else 'aim'
                        joint = ' joint head_1' if re.search(r'testMissionEnemy: name='+re.escape(target)+r' .* def=monster_q4x_tank(?:\s|$)', log) else ''
                        request([f'openq4_testInput 250 {aim} {target}{joint}', 'wait 18',
                                          f'openq4_testInput 1400 attack {aim} {target}{joint}{movement}', 'wait 90', 'openq4_testInput 0'])
                        bursts += 1
                        continue
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
                    if distance < 512:
                        path = [None, f'{goal_pos[0]} {goal_pos[1]} {player_pos[2]}', str(distance)]
                if not path or not state or not view or int(state[1]) <= 0:
                    raise RuntimeError(f'{goal}: missing path or live player')
                pos = tuple(map(float, state[2].split()))
                point = tuple(map(float, path[1].split()))
                history.append({'goal':goal, 'position':pos, 'next':point, 'distance':float(path[2])})
                if float(path[2]) < 20:
                    break
                recent_distances.append(float(path[2]))
                recent_distances = recent_distances[-12:]
                stalled = stalled+1 if last_pos and math.dist(pos,last_pos)<2 else 0
                if len(recent_distances) == 12 and min(recent_distances[-6:]) >= min(recent_distances[:6])-5:
                    stalled = max(stalled, 3 if jump and jumps < 2 else 8)
                if jump and stalled >= 3 and jumps < 2:
                    request(['openq4_testInput 700 forward 127 jump', 'wait 50', 'openq4_testInput 0'])
                    jumps += 1
                    stalled = 0
                    continue
                if stalled >= 8:
                    raise RuntimeError(f'{goal}: physically blocked at {pos}')
                last_pos = pos
                duration = max(80, min(1000, int(math.dist(pos[:2],point[:2])/180*1000)))
                # A temporary non-solid point gives the existing aim input a
                # waypoint. Keep aiming during movement so a short initial aim
                # pulse cannot expire before a slow frame processes it. Remove
                # the non-solid marker before saving; player collision is normal.
                request([f'spawn target_null name oq4_route_aim origin "{point[0]} {point[1]} {pos[2]+68}"',
                         f'openq4_testInput {duration} forward 127 aim oq4_route_aim', f'wait {math.ceil(duration/16)+3}',
                         'openq4_testInput 0', 'remove oq4_route_aim'])
            else:
                raise RuntimeError(f'{goal}: approach step limit')
            request([f'echo ROUTE_CHECKPOINT {goal}', 'testMission tak', 'testMission darnell',
                     'testMission navajas', 'testMission player1', f'saveGame route_{goal}', 'wait 15'])
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
    p.add_argument('--map')
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--retail', type=Path)
    p.add_argument('--awakening', type=Path)
    p.add_argument('--runtime-dir', type=Path, default=ROOT/'.install',
                   help='fixed staged runtime directory; defaults to the project install')
    p.add_argument('--pause-between-requests', action=argparse.BooleanOptionalAction, default=True,
                   help='pause game time between requests; disable for continuous combat timing')
    p.add_argument('--stop', action='store_true', help='stop an idle controller without certifying the route')
    p.add_argument('--drive', nargs='+', help='drive an occupied walker through normal steering input')
    p.add_argument('--vehicle', default='m04_maint_tunnel_unit1_uni_vehicle_walker_player')
    p.add_argument('--aim-height', type=float, help='aim above the target origin using short bursts')
    p.add_argument('--timeout', type=int, default=1800)
    p.add_argument('--cache', type=Path)
    p.add_argument('--renderer', choices=('gl', 'vulkan'), default='gl')
    p.add_argument('--skill', type=int, choices=(0,1,2,3), default=1)
    p.add_argument('--input', type=Path)
    p.add_argument('--follow', nargs='+', help='drive an already running authorized session toward these entities')
    p.add_argument('--point', nargs=3, type=float, metavar=('X','Y','Z'), help='walk to a non-solid diagnostic waypoint')
    p.add_argument('--direct', action='store_true', help='use collision-respecting straight approaches within 512 units, without AAS guidance')
    p.add_argument('--engage', action='store_true', help='fire normally at visible nearby hostile AI during approach; no ammunition refills')
    p.add_argument('--jump-obstacles', action='store_true', help='try at most two ordinary forward jumps per goal after stalled movement')
    p.add_argument('--combat-strafe', action='store_true', help='alternate quarter-speed lateral movement during firing bursts')
    p.add_argument('--commands', nargs='+', help='send engine commands to an existing session and wait for acknowledgement')
    p.add_argument('--check-input-restore', action='store_true', help='check weapon impulse state across in-process restores in a quiet authorized session')
    p.add_argument('--shoot', nargs='+', help='aim and fire normally at named targets until dead, stopping on no damage')
    p.add_argument('--refill-ammo', action='store_true', help='refill ammo between firing bursts; excludes ammunition/balance qualification')
    a = p.parse_args()
    out = task_path(a.output)
    if a.stop:
        requester(out)  # Refuse to interrupt an unacknowledged request.
        write_script(out/'stop.json', [json.dumps({'reason': 'Checkpointed session stopped by controller'})])
        return 0
    if a.drive:
        drive(out, a.vehicle, a.drive)
        return 0
    if a.check_input_restore:
        return check_input_restore(out)
    if a.commands:
        print(requester(out)(a.commands))
        return 0
    if a.point:
        request = requester(out)
        # An interrupted point approach can leave its diagnostic marker in a
        # checkpoint. Remove only our reserved marker before reusing its name.
        request(['remove oq4_route_destination', 'spawn target_null name oq4_route_destination origin "'+' '.join(map(str,a.point))+'"'])
        try:
            follow(out, ['oq4_route_destination'], a.direct, a.engage, a.jump_obstacles, a.combat_strafe)
        finally:
            request(['remove oq4_route_destination', 'saveGame route_progress'])
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
        follow(out, a.follow, a.direct, a.engage, a.jump_obstacles, a.combat_strafe)
        return 0
    if not a.map or not a.retail or not a.awakening:
        p.error('launching requires --map, --retail and --awakening')
    out.mkdir(parents=True, exist_ok=False)
    (out/'controller.json').write_text(json.dumps({'pause_between_requests': a.pause_between_requests})+'\n')
    game = out/'q4xbase'
    cfg = game/CFG_REL
    cfg.parent.mkdir(parents=True)
    if a.input:
        stage_input(a.input.resolve(), game/'savegames', 'route_in')
        (out/'input.json').write_text(json.dumps(dict(path=str(a.input.resolve()),
            sha256=hashlib.sha256(a.input.read_bytes()).hexdigest()), indent=2)+'\n')
    poll = game/'route_poll.cfg'
    # Keep the recursive loop immutable: Windows can transiently deny an exec
    # while its command file is replaced. A missed dispatch must not end polling.
    poll.write_text('wait 15\nexec route_dispatch.cfg\nwait 15\nexec route_poll.cfg\n')
    dispatch = game/'route_dispatch.cfg'
    payload = game/'route_payload.cfg'
    idle = '// no request\n'
    dispatch.write_text(idle)
    cfg.write_text(f'set g_autoExecAfterMapLoad ""\nset g_stopTime {int(a.pause_between_requests)}\nexec route_poll.cfg\n')
    runtime = a.runtime_dir.resolve()
    exe = client_path(runtime)
    verify_runtime_is_current(runtime, exe.name)
    opts = argparse.Namespace(mode='load' if a.input else 'write', basepath=a.retail.resolve(), game='q4xbase',
        tag='route', map=a.map, xvfb=False, set=[
        ('fs_awakeningpath', str(a.awakening.resolve())), ('fs_devpath', str(out)),
        ('fs_cachepath', str(task_path(a.cache) if a.cache else ROOT/'.tmp/awakening-route-cache')),
        ('r_renderApi', a.renderer), ('g_skill', str(a.skill)),
        ('r_fullscreenDesktop', '0'), ('r_windowWidth', '960'), ('r_windowHeight', '540'),
        ('developer', '0'),
        ('g_autoSkipCinematics', '0'), ('com_gameMode', 'SP'), ('ui_autoJoin', '0'),
        ('ui_retained', '1'), ('sys_consoleWindow', '0')])
    cmd = launch_command(opts, exe, out)
    (out/'launch.json').write_text(json.dumps(cmd, indent=2))
    module_root = exe.parent.parent/'Frameworks' if sys.platform == 'darwin' else exe.parent/'baseoq4'
    binaries = [exe, *module_root.glob('game-sp_*'), *exe.parent.glob('renderer-*')]
    content_package = exe.parent/'baseoq4/pak0.pk4'
    if content_package.is_file():
        binaries.append(content_package)
    def identity():
        return {str(path.relative_to(exe.parent.parent if sys.platform == 'darwin' else exe.parent)):
                hashlib.sha256(path.read_bytes()).hexdigest()
                for path in binaries if path.suffix in ('.exe', '.dll', '.so', '.dylib', '.pk4') or path == exe}
    before = identity()
    (out/'identity.json').write_text(json.dumps(before, indent=2))
    startup = None
    if sys.platform == 'win32':
        startup = subprocess.STARTUPINFO()
        startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        startup.wShowWindow = subprocess.SW_HIDE
    def publish(text):
        write_script(dispatch, text.splitlines())
    last = None
    pending = None
    direct_load = False
    initial_load = True
    stopped = False
    log_offset = 0
    log = ''
    start = time.monotonic()
    with (out/'stdout.log').open('w') as stream:
        proc = subprocess.Popen(cmd, cwd=exe.parent, stdin=subprocess.DEVNULL,
            stdout=stream, stderr=subprocess.STDOUT, startupinfo=startup,
            env={**os.environ, 'TMP': str(out), 'TEMP': str(out), 'TMPDIR': str(out)},
            start_new_session=sys.platform != 'win32')
        try:
            while proc.poll() is None and time.monotonic()-start < a.timeout:
                if (out/'stop.json').exists():
                    stopped = True
                    break
                logpath = game/'logs'/LOG_NAME
                if logpath.exists():
                    if logpath.stat().st_size < log_offset:
                        log_offset, log = 0, ''
                    with logpath.open('rb') as engine_log:
                        engine_log.seek(log_offset)
                        log += engine_log.read().decode('utf-8', errors='replace')
                        log_offset = engine_log.tell()
                if ('could not restore saved state' in log or
                    (a.input and initial_load and 'loadingSaveGame=0' in log.split('AutoExecAfterMapLoad:', 1)[0]) or
                    re.search(r'(?mi)^(?:\^\d)?(?:ERROR:|FATAL(?: ERROR)?:)', log)):
                    (out/'failure.txt').write_text('Engine load/runtime failure; see engine log.')
                    break
                if 'AutoExecAfterMapLoad:' in log:
                    initial_load = False
                if pending is not None and f'ROUTE_ACK_{pending} ' in log:
                    publish(idle)
                    write_script(out/'ack.json', [json.dumps({'id':pending})])
                    pending = None
                    direct_load = False
                elif direct_load and f'ROUTE_STEP_{pending} ' in log:
                    # testMission needs a player. A load from the death/menu
                    # screen therefore enters through exec once; stop publishing
                    # it as soon as execution begins, before map loading ends.
                    publish(idle)
                    direct_load = False
                elif (pending is not None and not direct_load
                      and f'ROUTE_STEP_{pending} ' not in log
                      and log.rfind('Game Map Shutdown') > log.rfind('Map Initialization')
                      and any(command.split()[0].lower() == 'loadgame'
                              for command in data['commands'] if command.split())):
                    # Death can unload the map after a restore was published
                    # through testMission, which requires a player. Promote
                    # only an unstarted restore to the menu-safe exec path.
                    publish('exec route_payload.cfg\n')
                    direct_load = True
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
                        write_script(payload, data['commands'] + [f'echo ROUTE_ACK_{pending}'])
                        direct_load = log.rfind('Game Map Shutdown') > log.rfind('Map Initialization') and any(
                            command.split()[0].lower() in ('loadgame', 'quit') for command in data['commands'] if command.split())
                        publish('exec route_payload.cfg\n' if direct_load else f'testMission dispatch {pending} route_payload.cfg\n')
                time.sleep(.05)
        except BaseException as error:
            (out/'failure.txt').write_text(f'Test controller error: {error}')
            raise
        finally:
            stop_process(proc)
    logpath = game/'logs'/LOG_NAME
    log = logpath.read_text(errors='replace') if logpath.exists() else ''
    if not stopped and (proc.returncode != 0 or time.monotonic()-start >= a.timeout):
        (out/'failure.txt').write_text(f'Route process ended with code {proc.returncode}, or timed out; see engine log.')
    steps = re.findall(r'(?m)^ROUTE_STEP_(\d+)\s*$', log)
    duplicates = sorted({step for step in steps if steps.count(step) > 1}, key=int)
    report = dict(qualification=False, stopped=stopped, pause_between_requests=a.pause_between_requests, seconds=round(time.monotonic()-start, 1),
                  reason='Interactive investigation; completion and balance require separate evidence.',
                  timed_out=time.monotonic()-start >= a.timeout, runtime_unchanged=before == identity(),
                  duplicate_requests=duplicates, executed_requests=len(steps))
    report['status'] = 'stopped' if stopped else 'exited'
    report['route_verdict'] = None
    report['process_exit_code'] = proc.returncode
    report['failed'] = (out/'failure.txt').exists()
    (out/'session.json').write_text(json.dumps(report, indent=2))
    return int(report['failed'])


if __name__ == '__main__':
    raise SystemExit(main())
