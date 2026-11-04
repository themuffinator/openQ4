#!/usr/bin/env python3
"""Focused, input-free Awakening ship/cellblock checks with installed assets.

Test-only map scripts select encounters; the production callbacks, HUD events,
weapons and save path still run. This is not a complete campaign playthrough.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import time

from campaign_runtime import ROOT, task_path
from savegame_cross_load import CFG_REL, LOG_NAME, launch_command, stage_input, stop_process

TURRET = 'guis/vehicles/airassault/turretplayerhud.gui'
PLAYER = 'guis/q4xhud.gui'
MAIN = 'guis/vehicles/airassault/spaceturret_main.gui'
HEAT = 'guis/vehicles/airassault/spaceturret_guns.gui'
M03 = '''
#include "scripts/openq4/awakening/m03_airassault.script"
void main() {
    sys.threadname("oq4_m03_probe");
    sys.wait(0.5);
    sys.killthread("map_m03aa::main");
    sys.firstPerson();
    map_m03aa::INTROCIN = false;
    map_m03aa::DRIVER = true;
    $topgun_driver.exitVehicle(1);
    sys.waitFrame();
    map_m03aa::DRIVER = false;
    $bottomgun.show();
    $topgun.show();
    $bottomgun.lock(0);
    $topgun.lock(0);
    sys.println("Q4_M03_READY");
    while (sys.getcvar("oq4_probe_phase") != "damage") { sys.waitFrame(); }
    $drop_ship.setHealth(9000);
    sys.sendNamedEvent(GUI_VEHICLEHUD, "ship_target_02_on");
    sys.wait(0.5);
    if (!(map_m03aa::GUI_80 && map_m03aa::GUI_60 && map_m03aa::GUI_40)) {
        sys.println("Q4_CHAPTER_FAILURE damage flags");
    }
    sys.println("Q4_M03_DAMAGE");
    while (sys.getcvar("oq4_probe_phase") != "repair") { sys.waitFrame(); }
    map_m03aa::DSHEALTHADD = 3000;
    sys.wait(0.5);
    if ($drop_ship.getHealth() != 12000 || map_m03aa::DSHEALTHADD != 0 || map_m03aa::GUI_40) {
        sys.println("Q4_CHAPTER_FAILURE hull repair");
    }
    sys.println("Q4_M03_REPAIRED");
    while (sys.getcvar("oq4_probe_phase") != "engine") { sys.waitFrame(); }
    map_m03aa::EVENT_ENGINE = true;
    map_m03aa::ENGINE_STEP_1 = true;
    map_m03aa::ENGINE_STEP_2 = false;
    // Use the rewritten callback actually stored on the installed terminal.
    sys.println("Q4_M03_ENGINE_CALLBACK " + $forwardgui.getKey("gui_Parm3"));
    float job = thread oq4_m03::restartEngine();
    sys.wait(0.1);
    thread oq4_m03::restartEngine();
    sys.waitForThread(job);
    if (!(map_m03aa::EVENT_ENGINE && map_m03aa::ENGINE_STEP_2)) {
        sys.println("Q4_CHAPTER_FAILURE engine step");
    }
    thread oq4_m03::restartEngine();
    sys.wait(12);
    if (map_m03aa::EVENT_ENGINE || !map_m03aa::ENGINE_FIRE_OUT || $drop_ship.getHealth() != 13000) {
        sys.println("Q4_CHAPTER_FAILURE engine completion");
    }
    sys.println("Q4_M03_ENGINE_ONCE");
}
'''

M04 = '''
    sys.trigger($cellblock_1_spawner);
    sys.trigger($cellblock_1_spawner2);
    sys.wait(6);
    sys.trigger($cellblock_1_spawner);
    sys.trigger($cellblock_1_spawner2);
'''


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def hud_queries():
    return [f'ui_hudTest {source} get {key}' for source in (TURRET, PLAYER) for key in
            ('oq4.ship.damage1', 'oq4.ship.damage2', 'oq4.ship.damage3', 'oq4.ship.target2')]


def run(args, case, leg='write', source=None):
    home = args.output / case / leg
    game = home / 'q4xbase'
    game.mkdir(parents=True, exist_ok=False)
    scripts = game / 'maps/game'
    scripts.mkdir(parents=True)
    if case == 'm03':
        # A map sidecar is independently named in the saved script-file list.
        # Avoid the unsaveable developer console-script source.
        (scripts/'m03_airassault.script').write_text(M03, encoding='ascii')
    lines = ['set g_autoExecAfterMapLoad ""', 'god', 'notarget']
    if case == 'm03' and leg == 'write':
        lines += ['wait 90', 'teleport bottomgun', 'trigger bottomgun', 'wait 150',
                  'testVehicle bottomgun', 'screenshot screenshots/bottom.tga',
                  'set oq4_probe_phase damage', 'wait 90', 'echo Q4_DAMAGE_HUD', *hud_queries(),
                  'screenshot screenshots/damaged.tga', 'testVehicle bottomgun exit', 'wait 90',
                  'set oq4_probe_phase repair', 'wait 90', 'echo Q4_REPAIR_HUD', *hud_queries(),
                  'screenshot screenshots/foot.tga', 'teleport topgun', 'trigger topgun', 'wait 150',
                  'testVehicle topgun', 'screenshot screenshots/top.tga', 'set g_stopTime 1',
                  'saveGame ship', 'wait 3']
    elif case == 'm03':
        lines += ['echo Q4_RESTORED_HUD', *hud_queries(), 'testVehicle topgun',
                  'screenshot screenshots/restored.tga', 'set g_stopTime 0', 'wait 10']
        lines += ['testVehicle topgun select 0', 'wait 30']
        lines += ['testVehicle topgun fire', 'wait 7'] * 68
        lines += ['wait 1', f'ui_hudTest {HEAT} get HeatPercentage',
                  'testVehicle topgun', 'testVehicle bottomgun', 'screenshot screenshots/overheat.tga', 'wait 90',
                  'testVehicle topgun select 1', 'wait 30', 'testVehicle topgun fire',
                  'screenshot screenshots/missiles.tga', 'set oq4_probe_phase engine', 'wait 1500']
    else:
        # M04 is not saved here; the console fixture does not replace its map
        # script (the external asset mount deliberately has highest priority).
        lines += ['script "'+' '.join(M04.split())+'"',
                  'listSpawnArgs target_null_41', 'listSpawnArgs target_null_47',
                  'listSpawnArgs tunnel_bot', 'listSpawnArgs strogg_ctrlRm_jumper_1_pos_1',
                  'echo Q4_BRIDGE_ALIAS',
                  'script "sys.println($gunner_3_toBridge == $strogg_ctrlRm_jumper_1_pos_1)"',
                  # The approach trigger removes the authored squad AAS barrier
                  # before the player can reach the bridge ambush trigger.
                  'trigger tunnel_bot', 'trigger trigger_once_35',
                  'script "thread bridgeGunnerGo()"',
                  'wait 180', 'teleport CB1_upr_escape_pos',
                  'aiDumpState',
                  'screenshot screenshots/cellblock.tga', 'wait 900',
                  'aiDumpState',
                  'aiDumpState monster_gunner_3 -v', 'aiDumpState tunnel_bot -v',
                  'noclip', 'setviewpos 978 450 300 90', 'wait 1', 'screenshot screenshots/bridge.tga',
                  'setviewpos 688 -3470 390 90', 'wait 30', 'screenshot screenshots/vent.tga',
                  'echo Q4_FIRE_BURNING', 'script "sys.println($func_fire_volume_1.getHealth())"',
                  "script \"sys.trace('600 -3330 360','740 -3330 360','0 0 0','0 0 0',265,$player1); sys.assert(sys.getTraceEntity() != $func_fire_volume_1)\"",
                  "script \"sys.trace('600 -3330 360','740 -3330 360','0 0 0','0 0 0',2561,$player1); sys.assert(sys.getTraceEntity() == $func_fire_volume_1)\""]
        lines += ['damage func_fire_volume_1 1 damage_freezegun'] * 25
        lines += ['wait 30', 'echo Q4_FIRE_OUT', 'script "sys.assert($func_fire_volume_1.getHealth() <= 0)"',
                  "script \"sys.trace('600 -3330 360','740 -3330 360','0 0 0','0 0 0',2561,$player1); sys.assert(sys.getTraceEntity() != $func_fire_volume_1)\"",
                  'screenshot screenshots/extinguished.tga']
    lines += ['echo Q4_CHAPTER_CHECK_DONE', 'quit']
    cfg = game / CFG_REL
    cfg.parent.mkdir(parents=True)
    cfg.write_text('\n'.join(lines)+'\n', encoding='ascii')
    if source:
        stage_input(source, game/'savegames', 'ship_in')
    options = argparse.Namespace(mode='load' if source else 'write', basepath=args.retail, game='q4xbase',
        tag='ship', map='game/m03_airassault' if case=='m03' else 'game/m04_prison', xvfb=False,
        set=[('fs_awakeningpath', str(args.awakening_root)), ('ui_autoJoin', '0'), ('com_gameMode', 'SP'),
             ('ui_retained','1'), ('ui_hudLayout','1'), ('r_renderApi',args.renderer), ('sys_consoleWindow','0'),
             ('fs_devpath',str(home)), ('fs_cachepath',str(args.cache)), ('g_debugVehicle','1'),
             ('g_autoSkipCinematics','0'), ('r_fullscreenDesktop','0'), ('com_maxfps','60')])
    exe = ROOT/'.install/openQ4-client_x64.exe'
    command = launch_command(options, exe, home)
    startup = None
    if os.name == 'nt':
        startup = subprocess.STARTUPINFO()
        startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        startup.wShowWindow = subprocess.SW_HIDE
    (home/'plan.json').write_text(json.dumps(dict(command=command, fixture_sha256=hashlib.sha256((M03 if case=='m03' else M04).encode()).hexdigest()), indent=2))
    started = time.monotonic()
    logpath = game/'logs'/LOG_NAME
    with (home/'stdout.log').open('w') as output:
        process = subprocess.Popen(command, cwd=exe.parent, stdin=subprocess.DEVNULL, stdout=output,
                                   stderr=subprocess.STDOUT, startupinfo=startup)
        while process.poll() is None and time.monotonic()-started < args.timeout:
            time.sleep(0.5)
            if logpath.exists() and re.search(r'(?mi)^.*(?:ERROR:|Compile failed|script runtime error|Assertion failed)', logpath.read_text(errors='replace')):
                break
        stop_process(process)
    log = logpath.read_text(errors='replace') if logpath.exists() else ''
    failures = re.findall(r'(?mi)^.*(?:ERROR:|Compile failed|script runtime error|Assertion failed|Unknown command|Q4_CHAPTER_FAILURE).*$', log)
    if 'Q4_CHAPTER_CHECK_DONE' not in log or process.returncode != 0:
        failures.append(f'incomplete run / exit {process.returncode}')
    if case == 'm03':
        required = ('Q4_M03_READY', 'Q4_M03_DAMAGE', 'Q4_M03_REPAIRED') if leg=='write' else ('Q4_M03_ENGINE_ONCE', 'Game Map Init SaveGame')
        failures += [f'missing {marker}' for marker in required if marker not in log]
        for key, expected in [('damage1','1'), ('damage2','1'), ('damage3','0'), ('target2','1')]:
            section = log.split('Q4_REPAIR_HUD' if leg=='write' else 'Q4_RESTORED_HUD')[-1]
            states = re.findall(rf'HUD_STATE source=\S+ key=oq4.ship.{key} value=(\S*)', section)
            if len(states) < 2 or any(v != expected for v in states):
                failures.append(f'wrong restored/repaired {key}: {states}')
        vehicle = 'topgun'
        if not re.search(r'testVehicle: name=topgun .*drivers=1 playerSeat=0', log):
            failures.append('not in top gun')
    else:
        if not re.search(r'Q4_BRIDGE_ALIAS\s+1(?:\.0*)?\s', log):
            failures.append('bridge destination alias is missing')
        for marker in ('oq4_m04_v2::upperReinforcement', 'oq4_m04_v2::lowerReinforcement'):
            if marker not in log:
                failures.append('missing '+marker)
    if case == 'm03':
        if 'HUD coverage: guis/cursor.gui: unmapped material gfx/guis/q4x_crosshairs/' in log:
            failures.append('cockpit reticle fell back to legacy artwork')
        screens = re.findall(r'testVehicleScreen: name=topgun index=([0-2]) unique=(\d) heat=([\d.]+) locked=(\d*) guns=(\d*) rockets=(\d*)', log)
        if {s[0] for s in screens if s[1]=='1'} != {'0','1','2'}:
            failures.append('cockpit screens are not unique')
        if not any(s[0]=='0' and s[4:]==('1','0') for s in screens):
            failures.append('cannon selection is not shown')
        if any(s[0]=='1' and s[3]!='0' for s in screens):
            failures.append('false missile lock without a target')
        if leg == 'load':
            if 'riVCWPulseCannon: overheated' not in log or 'riVCWPulseCannon: cooled' not in log:
                failures.append('missing overheat/cool transition')
            if not any(s[0]=='2' and float(s[2])>0.95 for s in screens):
                failures.append('heat screen does not track the firing cannon')
            if not any(s[0]=='0' and s[4:]==('0','1') for s in screens):
                failures.append('missile selection is not shown')
    else:
        rows = re.findall(r'name=rvMonster\S+ def=monster_q4x_\S+ class=\S+ hidden=0 origin=\(([^)]+)\)', log)
        positions = [list(map(float,row.split())) for row in rows]
        if not any((x-318)**2+(y+2766)**2 < 160**2 and 400<z<430 for x,y,z in positions):
            failures.append('upper reinforcement did not reach its landing')
        if not any((x-402)**2+(y+2670)**2 < 160**2 and 260<z<290 for x,y,z in positions):
            failures.append('lower reinforcement did not reach its landing')
        if "scriptedMove: 'monster_gunner_3' could not start" in log:
            failures.append('bridge gunner route is unreachable')
        gunner = re.findall(r'name=monster_gunner_3 .* hidden=0 origin=\(([^)]+)\)', log)
        if not gunner or sum((v-e)**2 for v,e in zip(map(float,gunner[-1].split()), (978,654,272))) > 40**2:
            failures.append('bridge gunner did not reach the upper bridge approach')
        gunner_state = log.rsplit('name=monster_gunner_3',1)[-1].split('AI dump state',1)[0]
        if 'idAI.move.status = MOVE_STATUS_DONE' not in gunner_state:
            failures.append('bridge gunner did not finish its scripted move')
        bots = re.findall(r'name=tunnel_bot .* hidden=0 origin=\(([^)]+)\)', log)
        if not bots or min(sum((v-e)**2 for v,e in zip(map(float,bots[-1].split()), stop))
                           for stop in ((1744,-3472,362),(1704,-3368,334))) > 40**2:
            failures.append('repair bot did not reach its own workstation')
        if "Function 'playCycle' not supported on entity 'prison_camera" in log:
            failures.append('prison camera initializer still calls mover animation')
    captures = list((game/'screenshots').glob('*.tga'))
    if not captures:
        failures.append('no engine captures')
    result = dict(case=case, leg=leg, passed=not failures, failures=failures,
                  seconds=round(time.monotonic()-started, 2), captures=[str(p) for p in captures])
    (home/'result.json').write_text(json.dumps(result,indent=2))
    print(json.dumps(result), flush=True)
    return result, game/'savegames/ship.save'


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--retail',type=Path,required=True)
    parser.add_argument('--awakening-root',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--cache',type=Path,default=ROOT/'.tmp/awakening-m03-m04/gl/cache')
    parser.add_argument('--case',choices=('m03','m04','all'),default='all')
    parser.add_argument('--renderer',choices=('gl','vulkan'),default='gl')
    parser.add_argument('--timeout',type=float,default=240)
    args=parser.parse_args()
    args.retail=args.retail.resolve(strict=True)
    args.awakening_root=args.awakening_root.resolve(strict=True)
    args.output=task_path(args.output)
    args.cache=task_path(args.cache)
    args.output.mkdir(parents=True,exist_ok=True)
    runtime=ROOT/'.install'
    identity={name:digest(runtime/name) for name in ('openQ4-client_x64.exe','baseoq4/game-sp_x64.dll','baseoq4/pak0.pk4')}
    for name in ('openQ4-client_x64.exe','baseoq4/game-sp_x64.dll'):
        assert identity[name]==digest(ROOT/'builddir'/name), 'stage the current build first'
    results=[]
    for case in ('m03','m04') if args.case=='all' else (args.case,):
        result,save=run(args,case)
        results.append(result)
        if case=='m03' and result['passed']:
            result,_=run(args,case,'load',save)
            results.append(result)
    report=dict(passed=all(r['passed'] for r in results), results=results, identity=identity,
                scope=__doc__, input_control=False)
    report['passed'] &= all(digest(runtime/name)==sha for name,sha in identity.items())
    (args.output/'report.json').write_text(json.dumps(report,indent=2))
    return 0 if report['passed'] else 1


if __name__=='__main__':
    raise SystemExit(main())
