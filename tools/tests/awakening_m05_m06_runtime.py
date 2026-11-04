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
        'processing':'m05_bio', 'teleporters':'m06_mcc_invasion',
        'combat-save':'m06_mcc_invasion', 'teleporter-state':'m06_mcc_invasion',
        'medic':'m05_bio', 'freight':'m05_bio', 'freight-board':'m05_bio', 'freight-ride':'m05_bio'}
ERROR = re.compile(r'(?mi)^(?:\^\d)?(?:ERROR:|FATAL ERROR:)|could not restore saved state|'
                   r'WriteObject FindIndex failed|Unknown command|unknown command|expected an existing zero-argument|screenshot: bad dimensions|usage: setviewpos|Entity not found for event')

def commands(case, restore):
    lines = ['set g_autoExecAfterMapLoad ""', 'set g_stopTime 0']
    if not restore:
        lines += ['god']
    if case == 'medic':
        return lines + ['testMission player1', 'wait 60', 'saveGame phase', 'wait 5',
                        'wait 1200', 'echo M56_MEDIC_COMPLETE', 'testMission player1',
                        'aiDumpState tak verbose', 'aiDumpState strauss verbose',
                        'testMission strauss', 'testMission navajas', 'testMission darnell',
                        'screenshot "screenshots/medic.tga"', 'saveGame mission', 'wait 5']
    if case in ('freight', 'freight-board', 'freight-ride'):
        if case == 'freight':
            lines += ['testMission call oq4_m05::penisWaving', 'wait 7200']
        if case in ('freight', 'freight-board'):
            lines += ['echo M56_FREIGHT_BOARDING', 'testMission elevA',
                      'aiDumpState tak verbose', 'aiDumpState strauss verbose', 'listThreads',
                      'setviewpos 3080 1872 -440 180', 'wait 600',
                      'saveGame phase', 'wait 5']
        lines += ['wait 4800', 'echo M56_FREIGHT_TOP', 'testMission elevA']
        for entity in ('player1', 'tak', 'strauss', 'navajas', 'darnell', 'elevACall_2'):
            lines += ['testMission '+entity]
        return lines + ['listThreads', 'screenshot "screenshots/freight.tga"', 'saveGame mission', 'wait 5']
    if case == 'checkpoint':
        return lines + ['wait 2400', 'testMission player1', 'saveGame mission', 'wait 5']
    if case == 'arrival':
        return lines + ['wait 2400', 'testMission mitchell_intro', 'testMission player1',
                        'screenshot "screenshots/arrival.tga"', 'saveGame mission', 'wait 5']
    if case == 'combat-save':
        if not restore:
            lines += ['wait 120', 'trigger dummyTurret', 'wait 600', 'saveGame phase', 'wait 5']
        return lines + ['wait 120', 'testVehicle turret', 'saveGame mission', 'wait 5']
    if case in ('teleporters', 'teleporter-state'):
        if not restore:
            lines += ['wait 120', 'testMission func_static_54157', 'testMission func_static_54159',
                      'testMission func_static_54160', 'testMission func_static_54168',
                      'testMission call oq4_m06b::setupRouteToHangar', 'wait 360',
                      'trigger trig_hall_teleporter', 'testMission call oq4_m06b::controlRoomCrash',
                      'wait 180', 'saveGame phase', 'wait 5']
        if case == 'teleporters':
            for monitor in ('monitorTeleHealth', 'monitorHallTeleHealth', 'monitorCommTeleHealth'):
                lines += ['testMission call oq4_m06b::'+monitor]  # re-entry while active
            lines += ['wait 2']
            for core in ('rear_tele_ball', 'rear_tele_ball_1', 'comm_tele_ball'):
                lines += ['damage '+core+' 1 damage_telefrag']
            lines += ['wait 1', 'saveGame breaking']
        lines += ['wait 120']
        for monitor in ('monitorTeleHealth', 'monitorHallTeleHealth', 'monitorCommTeleHealth'):
            lines += ['testMission call oq4_m06b::'+monitor]  # re-entry after removal
        lines += ['wait 120', 'echo M56_TELEPORTERS_STOPPED']
        for entity in ('rear_tele_ball', 'rear_tele_ball_1', 'comm_tele_ball',
                       'func_spawner_enemy_light_3', 'func_spawner_enemy_light_4', 'func_spawner_command_dropin',
                       'rear_tele_broken', 'rear_tele_broken_1', 'comm_tele_broken'):
            lines += ['testMission '+entity]
        return lines + ['listThreads', 'setviewpos 240 -1880 155 25 270 0', 'wait 2',
                        'screenshot "screenshots/mcc-monitors.tga"', 'saveGame mission', 'wait 5']
    if case == 'processing':
        return lines + ['wait 2400', 'trigger trigger_relay_1', 'wait 2',
                        'testMission call oq4_m05::prebossCinematic', 'wait 1800',
                        'wait 14400', 'testMission strauss', 'saveGame forbidden', 'wait 5', 'listThreads']
    if case == 'boss':
        if not restore:
            lines += ['wait 2400', 'testMission light_8861', 'testMission elevACall_1',
                      'testMission elevACall_2', 'testMission elevAControls',
                      'testMission light_8571', 'testMission light_8570',
                      'testMission light_8569', 'testMission light_6180', 'trigger trigger_relay_1', 'wait 2',
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
                      'trigger dummyTurret', 'wait 600',
                      'testVehicle turret', 'testVehicle turret exit', 'testVehicle turret',
                      'testVehicle turret fire', 'screenshot "screenshots/cannon.tga"',
                      'saveGame phase', 'wait 5', 'listThreads']
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
    if args.case in ('medic', 'freight', 'freight-board', 'freight-ride') and not args.input:
        parser.error('medic/freight probes require a rescued-squad or lift checkpoint')
    if args.input and args.case == 'processing':
        parser.error('processing starts a fresh timed rescue')
    if args.case == 'teleporter-state' and not args.input:
        parser.error('teleporter-state requires a core-destruction save')
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
    version = 3 if MAPS[args.case] == 'm06_mcc_invasion' else 2
    if not args.input and not args.baseline and f'Awakening mission corrections v{version}:' not in log:failures.append('corrections not enabled')
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
    if not args.input and args.case in ('boss', 'teleporters'):
        for missing in ('downsize( models/monsters/a4x_tactical/q4x_tt_body_s, 1)',
                        'gfx/effects/fire/p_fire2a',
                        'models/monsters/q4x_strogg_marine/q4x_smv_arms3_lo', 'lights/p_light1b'):
            if "Couldn't load image: "+missing in log:
                failures.append('repaired media reference still defaults: '+missing)
    if not args.input and args.case == 'teleporters':
        for monitor in ('func_static_54157', 'func_static_54159', 'func_static_54160', 'func_static_54168'):
            if not re.search(r'testMissionGui: name='+monitor+r' .*source=guis/monitors/marine/mcc/q4x_ambient_mcc1.gui', log):
                failures.append('MCC monitor did not load its ambient display: '+monitor)
        if "Couldn't load gui: 'guis/monitors/marine/mcc1.gui'" in log:
            failures.append('missing MCC monitor GUI is still precached')
    if args.case == 'medic':
        after = log.split('M56_MEDIC_COMPLETE')[-1]
        if not re.search(r'testMissionPlayer: name=player1 armor=100\b', after):
            failures.append('Tak did not complete the armor repair')
        if "medic 'tak' made no progress" in log:
            failures.append('Tak still stalls on the heal approach')
        for entity in ('strauss', 'navajas', 'darnell'):
            if not re.search(r'testMission: name='+entity+r' health=300 hidden=0', after):
                failures.append('squad member lost during repair: '+entity)
    if args.case in ('freight', 'freight-board', 'freight-ride'):
        after = log.split('M56_FREIGHT_TOP')[-1]
        for entity in ('elevA', 'player1', 'tak', 'strauss', 'navajas', 'darnell'):
            match = re.search(r'testMission: name='+entity+r' .*origin=\([^ ]+ [^ ]+ ([^ )]+)\)', after)
            if not match or float(match[1]) < 3000:
                failures.append('freight passenger/platform did not reach upper floor: '+entity)
    if args.case=='arrival' and 'testMission: missing=mitchell_intro' not in log:
        failures.append('Mitchell still appears after his death')
    if args.case == 'processing':
        if log.count('PROCESSING HAS BEEN FINISHED') != 3 or 'STRAUSS DIES' not in log:
            failures.append('uninterrupted processing did not finish all three cycles')
    if args.case=='boss':
        corrected_boss = not args.input or 'm05_bio.openq4-v2.script' in log
        if corrected_boss and "telefragged 'dekard_boss_cinematic'" in log:
            failures.append('rescue teleport hit the cinematic stand-in')
        if not args.input:
            for lamp in ('light_8571', 'light_8570', 'light_8569', 'light_6180'):
                if not re.search(r'testMissionLight: name='+lamp+r' .*material=lights/p_light1xb', log):
                    failures.append('M05 lamp did not load its authored light image: '+lamp)
            for panel in ('elevACall_1', 'elevACall_2', 'elevAControls'):
                if 'testMissionGui: name='+panel+' interactive=0 target=none' not in log:
                    failures.append('scripted freight panel is still interactive or has a stale target: '+panel)
        if not args.input and not re.search(r'testMission: name=light_8861 .*bind=elevator2',log):
            failures.append('service-lift lamp did not bind to its platform')
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
        if corrected_boss and 'PAINLORD AT PROCESSING STATION' in log.split('JUBILATION ABOUNDS')[-1]:
            failures.append('station approach survived victory')
    if args.case=='defense':
        after=log.split('M56_DEFENSE_COMPLETE')[-1]
        if not re.search(r'testVehicle: name=turret .*drivers=0 playerSeat=-1',after):failures.append('seat not released')
        if 'oq4_m06b::monitorShipHealth' in after:failures.append('hull monitoring survived defense')
    if args.case in ('defense','failure'):
        hull = re.search(r'testMission: name=mcc_2 health=(-?\d+)', log)
        if not hull or int(hull[1]) > 0:failures.append('hull damage did not destroy MCC')
    if args.case in ('defense','failure') and not args.input:
        if 'testVehicle: action=exit name=turret success=0' not in log:failures.append('combat seat not locked')
        if not re.search(r'(?m)^\s*\d+: oq4_m06b::turretSequence\s+:.*state=waiting$', log):
            failures.append('vehicle entry did not name the running cannon callback')
    if args.case == 'failure':
        # A timed waiter remains registered until its queued wake-up. The failed
        # save opens a modal, so registration alone cannot establish cancellation.
        after = log.split('testMission: name=mcc_2')[-1]
        for thread in ('turretSequence', 'startTurretSpawners', 'skyboxDroppods', 'fighterGroup1'):
            for entry in re.findall(r'(?m)^\s*\d+: oq4_m06b::'+thread+r'\s+:.*$', after):
                if not entry.endswith('state=cancelled'):
                    failures.append('uncancelled cannon thread after failure: '+thread)
    if args.case == 'combat-save' and not re.search(r'testVehicle: name=turret .*drivers=1 playerSeat=0 locked=1',log):
        failures.append('not in active cannon')
    if args.case in ('teleporters', 'teleporter-state'):
        after=log.split('M56_TELEPORTERS_STOPPED')[-1]
        for entity in ('rear_tele_ball', 'rear_tele_ball_1', 'comm_tele_ball',
                       'func_spawner_enemy_light_3', 'func_spawner_enemy_light_4', 'func_spawner_command_dropin'):
            if 'testMission: missing='+entity+'\n' not in after:failures.append('surviving teleporter entity: '+entity)
        for entity in ('rear_tele_broken', 'rear_tele_broken_1', 'comm_tele_broken'):
            if not re.search(r'testMission: name='+entity+r' health=-?\d+ hidden=0',after):
                failures.append('broken model missing: '+entity)
        if re.search(r'oq4_m06b::(?:spin(?:Hall|Comm)?TeleBall|monitor(?:Hall|Comm)?TeleHealth|controlRoomCrash) :',after):
            failures.append('teleporter/crash threads survived destruction')
    if args.case=='speeder' and 'm07_race' not in log:failures.append('speeder did not reach race map')
    capture = {'boss':'rescue', 'arrival':'arrival', 'defense':'escape', 'speeder':'race',
               'medic':'medic', 'freight':'freight', 'freight-board':'freight', 'freight-ride':'freight',
               'teleporters':'mcc-monitors', 'teleporter-state':'mcc-monitors'}.get(args.case)
    if capture and not (game/'screenshots'/f'{capture}.tga').is_file():
        failures.append('engine screenshot missing')
    for relative,expected in identities.items():
        if hashlib.sha256((ROOT/relative).read_bytes()).hexdigest()!=expected:failures.append('runtime changed during test')
    report.update(passed=not failures,failures=failures,seconds=round(time.monotonic()-started,2),log=str(path))
    (output/'report.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:report[k] for k in ('case','passed','failures','seconds','log')},indent=2))
    return int(bool(failures))

if __name__=='__main__':raise SystemExit(main())
