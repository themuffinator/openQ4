#!/usr/bin/env python3
"""Focused M05/M06 mission probes; no input injection or desktop capture.

Requires user-supplied retail/Awakening assets and the current staged SP build.
These exercise authored encounters and fresh-process saves, not full traversal.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import platform
from pathlib import Path
import re
import subprocess
import sys
import time

from campaign_runtime import ROOT, task_path
from mp_round_remote_smoke import verify_runtime_is_current
from savegame_cross_load import CFG_REL, LOG_NAME, client_path, launch_command, stage_input, stop_process

MAPS = {'boss':'m05_bio', 'arrival':'m06_mcc', 'defense':'m06_mcc_invasion',
        'failure':'m06_mcc_invasion', 'speeder':'m06_mcc_invasion', 'checkpoint':'m05_bio',
        'processing':'m05_bio'}
ERROR = re.compile(r'(?mi)^(?:\^\d)?(?:ERROR:|FATAL ERROR:)|could not restore saved state|'
                   r'WriteObject FindIndex failed|Unknown command|unknown command|expected an existing zero-argument|screenshot: bad dimensions')

def commands(case, restore):
    lines = ['set g_autoExecAfterMapLoad ""', 'set g_stopTime 0']
    if not restore:
        lines += ['god']
    if case == 'checkpoint':
        return lines + ['wait 2400', 'testMission player1', 'saveGame mission', 'wait 5']
    if case == 'arrival':
        return lines + ['wait 2400', 'testMission mitchell_intro', 'testMission player1',
                        'screenshot "screenshots/arrival.tga"', 'saveGame mission', 'wait 5']
    if case == 'processing':
        return lines + ['wait 2400', 'trigger trigger_relay_1', 'wait 2',
                        'testMission call oq4_m05::prebossCinematic', 'wait 1800',
                        'wait 14400', 'testMission strauss', 'saveGame forbidden', 'wait 5', 'listThreads']
    if case == 'boss':
        if not restore:
            lines += ['wait 2400', 'trigger trigger_relay_1', 'wait 2',
                      'testMission call oq4_m05::prebossCinematic', 'wait 1800',
                      'damage theboss 10000 damage_moverCrush head', 'wait 120',
                      'echo M56_FIRST_STUN', 'testMission theboss',
                      'screenshot "screenshots/first-stun.tga"',
                      'saveGame phase', 'wait 5']
        lines += ['damage theboss 800 damage_moverCrush dl_spine', 'wait 600',
                  'damage theboss 10000 damage_moverCrush head', 'wait 120',
                  'echo M56_SECOND_STUN', 'testMission theboss',
                  'damage theboss 800 damage_moverCrush dr_spine', 'wait 600',
                  'echo M56_WEAK_POINTS_GONE', 'testMission theboss',
                  'damage theboss 10000 damage_moverCrush dl_spine',
                  'damage theboss 10000 damage_moverCrush dr_spine', 'wait 2',
                  'echo M56_REMOVED_WEAK_POINTS', 'testMission theboss',
                  'damage theboss 10000 damage_moverCrush head', 'wait 1800',
                  'echo M56_VICTORY', 'testMission theboss', 'testMission strauss',
                  'listThreads', 'screenshot "screenshots/rescue.tga"', 'saveGame mission', 'wait 5']
    elif case in ('defense', 'failure'):
        if not restore:
            lines += ['wait 120', 'trigger item_objective_2',
                      'testMission call oq4_m06b::turretSequence', 'wait 600',
                      'testVehicle turret', 'testVehicle turret exit', 'testVehicle turret',
                      'testVehicle turret fire', 'screenshot "screenshots/cannon.tga"',
                      'saveGame phase', 'wait 5']
        if case == 'failure':
            lines += ['damage mcc_2 1 damage_telefrag', 'damage mcc_2 1 damage_telefrag',
                      'damage mcc_2 1 damage_telefrag', 'wait 120', 'testMission mcc_2', 'saveGame forbidden', 'wait 5', 'listThreads']
        else:
            lines += ['wait 2400', 'damage monster_q4x_harvester_combat_5 10000', 'wait 2400',
                      'damage monster_q4x_harvester_combat_4 10000', 'wait 1500',
                      'echo M56_DEFENSE_COMPLETE', 'testVehicle turret', 'testMission player1',
                      'damage mcc_2 1 damage_telefrag', 'damage mcc_2 1 damage_telefrag',
                      'damage mcc_2 1 damage_telefrag', 'wait 120', 'testMission mcc_2', 'listThreads',
                      'screenshot "screenshots/escape.tga"', 'saveGame mission', 'wait 5']
    elif case == 'speeder':
        if not restore:
            lines += ['wait 120', 'testMission call oq4_m06b::enterSpeederHangar', 'wait 1800',
                      'trigger vehicle_speederbike_1', 'wait 120', 'testVehicle vehicle_speederbike_1',
                      'saveGame phase']
        lines += ['wait 1800', 'testMission player1',
                  'screenshot "screenshots/race.tga"', 'saveGame mission', 'wait 5']
    return lines

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--case',choices=tuple(MAPS),required=True)
    parser.add_argument('--retail',type=Path,required=True)
    parser.add_argument('--awakening',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--cache',type=Path,required=True)
    parser.add_argument('--input',type=Path)
    parser.add_argument('--baseline',action='store_true',help='Original scripts on a fresh checkpoint; AI fixes remain enabled')
    parser.add_argument('--renderer',choices=('gl','vulkan'),default='gl')
    parser.add_argument('--timeout',type=int,default=420)
    args=parser.parse_args()
    if args.baseline and (args.input or args.case!='checkpoint'):
        parser.error('--baseline is only for a fresh checkpoint')
    if args.input and args.case == 'processing':
        parser.error('processing starts a fresh timed rescue')
    output=task_path(args.output); cache=task_path(args.cache)
    output.mkdir(parents=True,exist_ok=False)
    game=output/'q4xbase';game.mkdir()
    if args.input:stage_input(args.input.resolve(strict=True),game/'savegames','mission_in')
    cfg=game/CFG_REL;cfg.parent.mkdir(parents=True,exist_ok=True)
    cfg.write_text('\n'.join(commands(args.case,bool(args.input))+['echo M56_DONE','quit'])+'\n',encoding='ascii')
    exe=client_path(ROOT/'.install')
    verify_runtime_is_current(ROOT/'.install',exe.name)
    settings=[('fs_awakeningpath',str(args.awakening.resolve(strict=True))),('fs_devpath',str(output)),
              ('fs_cachepath',str(cache)),('ui_retained','1'),('ui_autoJoin','0'),('com_gameMode','SP'),
              ('r_renderApi',args.renderer),('sys_consoleWindow','0'),('r_fullscreenDesktop','0'),('sys_lang','english'),
              ('r_windowWidth','960'),('r_windowHeight','540'),('g_autoSkipCinematics','1')]
    if args.baseline:settings += [('g_awakeningM05M06','0')]
    options=argparse.Namespace(mode='load' if args.input else 'write',basepath=args.retail.resolve(strict=True),
        game='q4xbase',tag='mission',map='game/'+MAPS[args.case],xvfb=False,set=settings)
    command=launch_command(options,exe,output)
    arch='arm64' if platform.machine().lower() in ('arm64','aarch64') else 'x64'
    suffix='.dll' if sys.platform=='win32' else '.dylib' if sys.platform=='darwin' else '.so'
    module_root=exe.parent.parent/'Frameworks' if sys.platform=='darwin' else exe.parent/'baseoq4'
    content_root=exe.parent.parent/'Resources/baseoq4' if sys.platform=='darwin' else exe.parent/'baseoq4'
    identities={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest()
                for p in (exe,module_root/('game-sp_'+arch+suffix),content_root/'pak0.pk4')}
    report=dict(case=args.case,renderer=args.renderer,command=command,identities=identities)
    (output/'plan.json').write_text(json.dumps(report,indent=2)+'\n')
    startup=None
    if sys.platform=='win32':
        startup=subprocess.STARTUPINFO();startup.dwFlags|=subprocess.STARTF_USESHOWWINDOW;startup.wShowWindow=0
    path=game/'logs'/LOG_NAME;failures=[];started=time.monotonic()
    with (output/'stdout.log').open('w') as stdout:
        process=subprocess.Popen(command,cwd=exe.parent,stdin=subprocess.DEVNULL,stdout=stdout,
            stderr=subprocess.STDOUT,startupinfo=startup,start_new_session=sys.platform!='win32')
        while process.poll() is None:
            current=path.read_text(errors='replace') if path.exists() else ''
            if ERROR.search(current) or time.monotonic()-started>args.timeout:
                failures.append('runtime error or timeout');stop_process(process);break
            time.sleep(.5)
    log=path.read_text(errors='replace') if path.exists() else ''
    if process.returncode!=0 or ERROR.search(log) or log.count('M56_DONE')!=1:
        failures.append('runtime error, exit status or missing completion marker')
    if not args.input and not args.baseline and 'Awakening mission corrections v1:' not in log:failures.append('corrections not enabled')
    if args.input and 'Game Map Init SaveGame' not in log:failures.append('input not restored')
    if args.case in ('failure', 'processing'):
        refused = "Can't save after failed mission" in log
        if args.case == 'processing':
            # Vital-NPC death returns to the menu after its failure screen.
            refused |= 'Not playing a game.' in log.split('STRAUSS DIES')[-1]
        if not refused or (game/'savegames/forbidden.save').exists():
            failures.append('objective failure did not end mission')
    else:
        if not (game/'savegames/mission.save').is_file():failures.append('final save missing')
        if "Can't save after failed mission" in log:failures.append('unexpected mission failure')
    if args.case=='arrival' and 'testMission: missing=mitchell_intro' not in log:
        failures.append('Mitchell still appears after his death')
    if args.case == 'processing':
        if log.count('PROCESSING HAS BEEN FINISHED') != 3 or 'STRAUSS DIES' not in log:
            failures.append('uninterrupted processing did not finish all three cycles')
    if args.case=='boss':
        if not args.input and 'testMissionObjective: Rescue Strauss' not in log.split('M56_FIRST_STUN')[-1].split('M56_SECOND_STUN')[0]:
            failures.append('rescue objective missing during fight')
        if 'health=2666' not in log:failures.append('second shield phase not reached')
        before = log.split('M56_WEAK_POINTS_GONE')[-1].split('M56_REMOVED_WEAK_POINTS')[0]
        removed = log.split('M56_REMOVED_WEAK_POINTS')[-1].split('M56_VICTORY')[0]
        health_before = re.search(r'testMission: name=theboss health=(\d+)', before)
        health_after = re.search(r'testMission: name=theboss health=(\d+)', removed)
        if not health_before or not health_after or health_before[1] != health_after[1]:
            failures.append('removed weak points still absorb hits')
        victory=log.split('M56_VICTORY')[-1]
        if not re.search(r'testMission: name=theboss health=-?\d+',victory):failures.append('boss state missing')
        match=re.search(r'testMission: name=theboss health=(-?\d+)',victory)
        if match and int(match[1])>0:failures.append('boss survived')
        if 'testMission: name=strauss health=300 hidden=0' not in victory:failures.append('Strauss not rescued')
        if 'testMissionObjective: Rescue Strauss' in victory:failures.append('rescue objective did not complete')
        if 'oq4_m05_painlord::processingLine' in victory:failures.append('processing survived victory')
    if args.case=='defense':
        after=log.split('M56_DEFENSE_COMPLETE')[-1]
        if not re.search(r'testVehicle: name=turret .*drivers=0 playerSeat=-1',after):failures.append('seat not released')
        if 'oq4_m06b::monitorShipHealth' in after:failures.append('hull monitoring survived defense')
    if args.case in ('defense','failure'):
        hull = re.search(r'testMission: name=mcc_2 health=(-?\d+)', log)
        if not hull or int(hull[1]) > 0:failures.append('hull damage did not destroy MCC')
    if args.case in ('defense','failure') and not args.input:
        if 'testVehicle: action=exit name=turret success=0' not in log:failures.append('combat seat not locked')
    if args.case=='speeder' and 'm07_race' not in log:failures.append('speeder did not reach race map')
    capture = {'boss':'rescue', 'arrival':'arrival', 'defense':'escape', 'speeder':'race'}.get(args.case)
    if capture and not (game/'screenshots'/f'{capture}.tga').is_file():
        failures.append('engine screenshot missing')
    for relative,expected in identities.items():
        if hashlib.sha256((ROOT/relative).read_bytes()).hexdigest()!=expected:failures.append('runtime changed during test')
    report.update(passed=not failures,failures=failures,seconds=round(time.monotonic()-started,2),log=str(path))
    (output/'report.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:report[k] for k in ('case','passed','failures','seconds','log')},indent=2))
    return int(bool(failures))

if __name__=='__main__':raise SystemExit(main())
