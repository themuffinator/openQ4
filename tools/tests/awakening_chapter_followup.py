#!/usr/bin/env python3
"""Input-free M03 lock/save/volley and M04 occupied-cellblock encounter probes.

The rescue probe observes the initial fight, then periodically kills attacking
Strogg through the AI event path to substitute for player covering fire. It
qualifies route handoff and evacuation sequencing, not encounter balance.
"""
from __future__ import annotations
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import time
from campaign_runtime import ROOT, task_path, sha256
from savegame_cross_load import CFG_REL, LOG_NAME, launch_command, stage_input, stop_process

SHIP = '''
#include "scripts/openq4/awakening/m03_airassault.script"
void main() {
    sys.wait(0.5);
    sys.killthread("map_m03aa::main");
    sys.firstPerson();
    map_m03aa::INTROCIN = false;
    map_m03aa::DRIVER = true;
    $topgun_driver.exitVehicle(1);
    sys.waitFrame();
    map_m03aa::DRIVER = false;
    $topgun.show(); $topgun.lock(0);
    while (sys.getcvar("oq4_lock_phase") != "target") { sys.waitFrame(); }
    sys.setSpawnArg("name", "oq4_lock_target");
    sys.setSpawnArg("health", "100000");
    sys.setSpawnArg("neverDormant", "1");
    sys.setSpawnArg("origin", $player1.getOrigin() + sys.angToForward($player1.getViewAngles()) * 1800);
    entity target = sys.spawn("monster_q4x_strogg_marine_mgun");
    sys.println("Q4_LOCK_TARGET_READY");
}
'''

# Test-only covering fire after the occupied approach has been observed. Use
# normal AI death/spawner notification; leave defenders and mission actors alone.
RESCUE_COVER = '''
void oq4_probe_cover() {
float pass;
float i;
sys.println("Q4_COVER_READY");
for (pass = 0; pass < 20; pass++) {
    for (i = 0; i < 4096; i++) {
        entity attacker = sys.getEntity("*" + i);
        if (isValidEntity(attacker)) {
            if (attacker.isA("monster_q4x_strogg_marine") && !attacker.isHidden() && attacker.getHealth() > 0) {
                if (sys.vecLength(attacker.getOrigin() - '80 -2740 350') < 500) {
                    sys.println("Q4_COVER " + attacker.getName());
                    attacker.kill();
                }
            }
        }
        if (i % 128 == 0) { sys.waitFrame(); }
    }
    sys.wait(8);
}
}
'''


def fov_queries(phase):
    return [f'echo Q4_FOV_{phase}', 'g_fov', 'listSpawnArgs player1', 'openq4_viewReport',
            'echo Q4_FOV_END']


def run(args, leg, save=None):
    home = args.output / leg
    game = home / 'q4xbase'
    game.mkdir(parents=True, exist_ok=False)
    lines = ['set g_autoExecAfterMapLoad ""', 'god', 'notarget']
    if args.case == 'lock':
        script = game/'maps/game/m03_airassault.script'
        script.parent.mkdir(parents=True)
        script.write_text(SHIP, encoding='ascii')
        if not save:
            lines += ['wait 90', 'teleport topgun', 'trigger topgun', 'wait 120',
                      'testVehicle topgun select 1', 'wait 180', 'set ai_disable 1',
                      'set oq4_lock_phase target', 'wait 5', 'echo Q4_ACQUIRING',
                      'testVehicle topgun', 'screenshot screenshots/acquiring.tga',
                      'wait 30', 'echo Q4_LOCKED', 'testVehicle topgun',
                      'screenshot screenshots/locked.tga', *fov_queries('ENTRY'),
                      'set g_stopTime 1', 'saveGame lock', 'wait 3']
        else:
            lines += [*fov_queries('RESTORED'), 'echo Q4_LOCK_RESTORED', 'testVehicle topgun',
                      'screenshot screenshots/restored.tga', 'set g_stopTime 0',
                      'echo Q4_FULL_VOLLEY', 'testVehicle topgun fire', 'testVehicle topgun fire',
                      'wait 27', 'echo Q4_EMPTY_VOLLEY', 'testVehicle topgun fire',
                      'wait 13', 'echo Q4_PARTIAL_VOLLEY',
                      'testVehicle topgun', 'testVehicle topgun fire', 'remove oq4_lock_target',
                      'wait 30', 'echo Q4_LOCK_LOST', 'testVehicle topgun',
                      'screenshot screenshots/lost.tga', 'testVehicle topgun exit',
                      'wait 90', *fov_queries('EXIT')]
    else:
        cover = game/'scripts/openq4_probe_rescue.inc'
        cover.parent.mkdir(parents=True)
        cover.write_text(RESCUE_COVER, encoding='ascii')
        # Activate the real defenders and rescue sequence, skipping earlier
        # chapters. Their movement and evacuation callbacks remain authored AI.
        setup = '''
            aiScriptedStop($tak);
            $tak.setOrigin($tak_topLoop_CB1_1.getOrigin());
            sys.trigger($CB1_marine_cover_upr); sys.trigger($CB1_marine_cover_lwr);
            sys.trigger($CB1_marine_medic_upr); sys.trigger($CB1_marine_medic_lwr);
            thread cellblock1_prisoners(); thread readyCellBreak();
        '''
        # Let the initial gift/squad sequence finish before moving Tak. Keep
        # the observer above the corridor, clear of prisoner escape markers.
        # Four normal simulation tics per rendered frame keep this long AI
        # encounter bounded without changing physics or script time steps.
        lines += ['wait 450', 'noclip', 'setviewpos 640 -3120 520 20 145 0',
                  'script "'+' '.join(setup.split())+'"',
                  'wait 450', 'echo Q4_RESCUE_UPPER', 'aiDumpState',
                  'screenshot screenshots/upper.tga',
                  # The script compiler requires includes at file scope.
                  # Close the console wrapper and provide its trailing body.
                  'script "} #include <scripts/openq4_probe_rescue.inc> void oq4_probe_tail() {"',
                  'testMission call oq4_probe_cover',
                  'wait 2250', 'echo Q4_RESCUE_END',
                  'aiDumpState', 'aiDumpState CB1_marine_medic_lwr -v',
                  'echo Q4_GRID8', 'script "sys.println($CB1_grid_8.isHidden())"']
        for number in ('01','02','03','05','07','08'):
            # Resolve by name: the script entity number can be reused by a
            # reinforcement after the prisoner has been removed.
            lines += [f'testMission cellblock1_prisoner_{number}']
        lines += ['screenshot screenshots/rescue.tga']
    lines += ['echo Q4_FOLLOWUP_DONE', 'quit']
    cfg = game/CFG_REL
    cfg.parent.mkdir(parents=True)
    cfg.write_text('\n'.join(lines)+'\n', encoding='ascii')
    if save:
        stage_input(save, game/'savegames', 'lock_in')
    options = argparse.Namespace(mode='load' if save else 'write', basepath=args.retail, game='q4xbase',
        tag='lock', map='game/m03_airassault' if args.case=='lock' else 'game/m04_prison', xvfb=False,
        set=[('fs_awakeningpath',str(args.awakening_root)), ('fs_devpath',str(home)),
             ('fs_cachepath',str(args.cache)), ('com_gameMode','SP'), ('ui_autoJoin','0'),
             ('ui_retained','1'), ('ui_hudLayout','1'), ('g_autoSkipCinematics','0'),
             ('g_debugVehicle','1'), ('sys_consoleWindow','0'), ('r_fullscreenDesktop','0'), ('g_fov','110'),
             ('r_renderApi',args.renderer), ('com_fixedTic','4' if args.case=='rescue' else '1')])
    exe = ROOT/'.install/openQ4-client_x64.exe'
    command = launch_command(options, exe, home)
    startup = None
    if os.name == 'nt':
        startup = subprocess.STARTUPINFO()
        startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        startup.wShowWindow = subprocess.SW_HIDE
    (home/'plan.json').write_text(json.dumps({'command':command,'input_control':False},indent=2))
    started = time.monotonic()
    logpath = game/'logs'/LOG_NAME
    with (home/'stdout.log').open('w') as output:
        process = subprocess.Popen(command,cwd=exe.parent,stdin=subprocess.DEVNULL,stdout=output,
                                   stderr=subprocess.STDOUT,startupinfo=startup)
        while process.poll() is None and time.monotonic()-started < args.timeout:
            time.sleep(0.5)
            log = logpath.read_text(errors='replace') if logpath.exists() else ''
            if (re.search(r'ERROR:|Compile failed|script runtime error|Assertion failed',log) or
                    any((exe.parent/'crashes').glob(f'*_{process.pid}_*.log'))):
                break
        stop_process(process)
    log = logpath.read_text(errors='replace') if logpath.exists() else ''
    log = re.sub(r'\^[0-9]', '', log)
    failures = re.findall(r'(?mi)^.*(?:ERROR:|Compile failed|script runtime error|Assertion failed|Unknown command).*$',log)
    if process.returncode != 0 or 'Q4_FOLLOWUP_DONE' not in log:
        failures.append(f'incomplete run / exit {process.returncode}')
    if args.case == 'lock':
        for phase, offset in ((('RESTORED', '-10'), ('EXIT', '0')) if save else (('ENTRY', '-10'),)):
            block = re.search(r'Q4_FOV_'+phase+r'\s*\n(.*?)Q4_FOV_END', log, re.S)
            if not block or not re.search(r'"g_fov" is:"110"', block.group(1)) or not re.search(
                    r'"openq4_scriptFovOffset"\s+"'+offset+r'(?:\.0*)?"', block.group(1)):
                failures.append(f'{phase}: custom FOV or turret offset lost')
        for marker in (('Q4_LOCK_RESTORED','Q4_LOCK_LOST') if save else ('Q4_ACQUIRING','Q4_LOCKED')):
            if marker not in log:
                failures.append('missing '+marker)
        if save:
            if re.findall(r'riVCWMissileTurret: fired (\d+) loaded rockets',log) != ['5','1']:
                failures.append('volley size or empty-magazine rejection does not match the HUD')
            if not re.search(r'testVehicleScreen: name=topgun index=1 .*locked=0',log.split('Q4_LOCK_LOST')[-1]):
                failures.append('lock display did not clear')
            empty = log.split('Q4_EMPTY_VOLLEY')[-1].split('Q4_PARTIAL_VOLLEY')[0]
            if 'action=fire name=topgun success=0' not in empty:
                failures.append('empty magazine fired after the weapon cooldown')
            phases = (('Q4_LOCK_RESTORED', '1', '0', '11111'),
                      ('Q4_EMPTY_VOLLEY', '1', '0', '00000'),
                      ('Q4_PARTIAL_VOLLEY', '1', '0', '10000'),
                      ('Q4_LOCK_LOST', '0', '0', None))
        elif not re.search(r'testVehicleScreen: name=topgun index=1 .*locked=1',log.split('Q4_LOCKED')[-1]):
            failures.append('target never locked')
        if not save:
            phases = (('Q4_ACQUIRING', '0', '1', '11111'), ('Q4_LOCKED', '1', '0', '11111'))
        for marker, locked, acquiring, loaded in phases:
            section = log.split(marker)[-1].split('Q4_',1)[0]
            screen = re.search(r'testVehicleScreen: name=topgun index=1 .*locked=(\d).*acquiring=(\d) loaded=(\d+)',section)
            if not screen or screen.group(1,2) != (locked,acquiring) or (loaded is not None and screen.group(3) != loaded):
                failures.append(f'{marker}: wrong lock/magazine screen state')
    else:
        if 'Q4_COVER ' not in log:
            failures.append('covering-fire fixture did not run')
        if re.search(r'scriptedMove:.*(?:made no progress|could not start)',log):
            failures.append('rescue or reinforcement movement failed')
        if not re.search(r'Q4_GRID8\s+1(?:\.0*)?\s',log):
            failures.append('last cell did not open')
        for number in ('01','02','03','05','07','08'):
            if f'testMission: missing=cellblock1_prisoner_{number}' not in log:
                failures.append(f'prisoner {number} did not escape')
        fighters = re.findall(r'name=rvMonster\S+ def=monster_q4x_\S+.*?(?=\n  #|AI dump complete)', log, re.S)
        if not any('idAI.aifl.scripted = false' in actor and
                   re.search(r'idAI.lastAttackTime = [1-9]\d*', actor) for actor in fighters):
            failures.append('reinforcements did not leave scripted movement and attack')
    result = dict(passed=not failures,failures=failures,leg=leg,seconds=round(time.monotonic()-started,2))
    (home/'result.json').write_text(json.dumps(result,indent=2))
    print(json.dumps(result),flush=True)
    return result,game/'savegames/lock.save'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--retail',type=Path,required=True)
    parser.add_argument('--awakening-root',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--case',choices=('lock','rescue'),required=True)
    parser.add_argument('--renderer',choices=('gl','vulkan'),default='gl')
    parser.add_argument('--timeout',type=float,default=420)
    args = parser.parse_args()
    args.retail=args.retail.resolve(strict=True)
    args.awakening_root=args.awakening_root.resolve(strict=True)
    args.output=task_path(args.output)
    args.cache=task_path(ROOT/'.tmp/awakening-m03-m04/gl/cache')
    args.output.mkdir(parents=True,exist_ok=True)
    runtime=ROOT/'.install'
    identity={name:sha256(runtime/name) for name in ('openQ4-client_x64.exe','baseoq4/game-sp_x64.dll','baseoq4/pak0.pk4')}
    for name in ('openQ4-client_x64.exe','baseoq4/game-sp_x64.dll'):
        assert identity[name]==sha256(ROOT/'builddir'/name), 'stage the current build first'
    first,save=run(args,'write')
    results=[first]
    if args.case=='lock' and first['passed']:
        second,_=run(args,'load',save)
        results.append(second)
    passed=all(r['passed'] for r in results) and all(sha256(runtime/name)==value for name,value in identity.items())
    (args.output/'report.json').write_text(json.dumps(dict(passed=passed,results=results,identity=identity),indent=2))
    return 0 if passed else 1


if __name__=='__main__':
    raise SystemExit(main())
