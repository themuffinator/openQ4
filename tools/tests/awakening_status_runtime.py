#!/usr/bin/env python3
"""Opt-in frozen-AI save/load regression with independently installed assets.

Launches the stable staged SP client in a hidden window. Uses only engine
commands, and writes all profiles/logs/saves under the supplied project .tmp.
"""
from __future__ import annotations
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import struct
import time

from campaign_runtime import ROOT, task_path, sha256


def main() -> int:
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--retail', type=Path, required=True)
    parser.add_argument('--awakening-root', type=Path, required=True)
    parser.add_argument('--output', type=Path, default=ROOT/'.tmp/awakening-status-runtime')
    parser.add_argument('--operation', choices=('save','load'), required=True)
    parser.add_argument('--slot', default='frozen_v4')
    parser.add_argument('--legacy-writer', action='store_true', help='record a frozen v3 fixture before staging the candidate')
    args=parser.parse_args()
    if not re.fullmatch('[a-zA-Z0-9_]+',args.slot):
        parser.error('slot must contain only letters, digits and underscores')
    output=task_path(args.output)
    profile=output/'q4xbase';profile.mkdir(parents=True,exist_ok=True)
    exe=ROOT/'.install/openQ4-client_x64.exe'
    tag=args.slot+'_'+args.operation
    cfg=profile/(tag+'.cfg')
    if args.operation=='save':
        cfg.write_text('\n'.join([
            'waitMsec 500','god','openq4_assertMapState game/m04_prison',
            # Existing v3 code only enters ice above 1 HP. Loading also tests
            # the separate one-HP case after this saved enemy shatters.
            'spawn monster_q4x_grunt name awakening_frozen health 20 neverDormant 1 ignoreplayer 1',
            'waitMsec 100','damage awakening_frozen 100 damage_freezegun',
            'waitMsec 500','echo AWAKENING_STATUS_BEFORE_SAVE',
            'aiDumpState awakening_frozen -v',f'saveGame {args.slot}',
            'waitMsec 100','echo AWAKENING_STATUS_DONE','quit'])+'\n',encoding='ascii')
    else:
        cfg.write_text('\n'.join([
            'echo AWAKENING_STATUS_AFTER_LOAD','aiDumpState awakening_frozen -v',
            'waitMsec 4500','echo AWAKENING_STATUS_AFTER_SHATTER',
            'aiDumpState awakening_frozen -v',
            'spawn monster_q4x_grunt name awakening_onehp health 1 neverDormant 1 ignoreplayer 1',
            'waitMsec 100','damage awakening_onehp 1 damage_freezegun',
            'echo AWAKENING_STATUS_ONE_HP','aiDumpState awakening_onehp -v',
            'echo AWAKENING_STATUS_DONE','quit'])+'\n',encoding='ascii')
    log_relative=f'logs/{tag}.log';log_path=profile/log_relative
    # A rerun must not mistake the previous attempt's fatal line for this run.
    log_path.unlink(missing_ok=True)
    settings={
        'fs_basepath':str(args.retail.resolve(strict=True)),
        'fs_awakeningpath':str(args.awakening_root.resolve(strict=True)),
        'fs_savepath':str(output),'fs_devpath':str(output),'fs_cachepath':str(output/'cache'),
        'fs_game':'q4xbase','si_gameType':'singleplayer','ui_autoJoin':'1','ui_retained':'1',
        'r_fullscreen':'0','r_fullscreenDesktop':'0','r_borderless':'0','r_borderlessDefaultMigrated':'1',
        'r_hiddenWindow':'1','r_renderApi':'gl','s_noSound':'1','in_mouse':'0','in_joystick':'0',
        'developer':'1','logFile':'1','logFileName':log_relative,'com_skipLoadingContinue':'1',
        'g_autoSkipCinematics':'1','g_debugDamage':'1','ai_debugActions':'awakening_frozen',
        'g_autoExecAfterMapLoad':cfg.name,'g_autoExecAfterMapLoadDelayMs':'0',
        'com_allowConsole':'1','r_swapInterval':'0',
        'win_allowMultipleInstances' if os.name=='nt' else 'sys_allowMultipleInstances':'1',
    }
    command=[str(exe)]
    for key,value in settings.items():command += ['+set',key,value]
    command += ['+map','game/m04_prison'] if args.operation=='save' else ['+loadGame',args.slot]
    options={}
    if os.name=='nt':
        startup=subprocess.STARTUPINFO();startup.dwFlags|=subprocess.STARTF_USESHOWWINDOW;startup.wShowWindow=0
        options={'startupinfo':startup,'creationflags':subprocess.CREATE_NO_WINDOW}
    env={**os.environ,'TMP':str(output),'TEMP':str(output),'TMPDIR':str(output)}
    module=ROOT/'.install/baseoq4/game_x64.dll'
    identity={'executable_sha256':sha256(exe),'module_sha256':sha256(module)}
    started=time.monotonic()
    with (output/(tag+'.stdout')).open('w') as stdout:
        process=subprocess.Popen(command,cwd=exe.parent,stdin=subprocess.DEVNULL,stdout=stdout,stderr=subprocess.STDOUT,env=env,**options)
        while True:
            try:
                exit_code=process.wait(timeout=1)
                break
            except subprocess.TimeoutExpired:
                live=log_path.read_text(encoding='latin1') if log_path.exists() else ''
                fatal=bool(re.search(r'(?m)^(?:\^\d)?(?:ERROR:|FATAL ERROR:)',live))
                if fatal or time.monotonic()-started>240:
                    process.kill();process.wait();exit_code='runtime error' if fatal else 'timeout'
                    break
    log=log_path.read_text(encoding='latin1') if log_path.exists() else ''
    failures=[]
    if identity!={'executable_sha256':sha256(exe),'module_sha256':sha256(module)}:
        failures.append('staged runtime changed during the test')
    if exit_code!=0:failures.append(f'exit {exit_code}')
    if 'AWAKENING_STATUS_DONE' not in log:failures.append('end marker absent')
    if re.search(r'(?m)^(?:\^\d)?(?:ERROR:|FATAL ERROR:)',log):failures.append('runtime error')
    if args.operation=='save':
        saved=profile/'savegames'/(args.slot+'.save')
        if not saved.is_file():failures.append('save absent')
        else:
            payload=saved.read_bytes();magic=payload.find(b'OQ4S')
            version=struct.unpack_from('<i',payload,magic+4)[0] if magic>=0 else -1
            if version!=(3 if args.legacy_writer else 4):failures.append(f'unexpected save format {version}')
        if not args.legacy_writer and 'idAI.freezeFactor = 1' not in log:failures.append('save was not frozen')
        if 'awakening_frozen: frozen solid' not in log:failures.append('lethal ice state absent')
    else:
        before=log.split('AWAKENING_STATUS_AFTER_LOAD')[-1].split('AWAKENING_STATUS_AFTER_SHATTER')[0]
        onehp=log.split('AWAKENING_STATUS_ONE_HP')[-1]
        if 'idAI.freezeFactor = 1' not in before:failures.append('freeze state missing after restore')
        if before.count('awakening_frozen: shatters')!=1:failures.append('expected exactly one delayed shatter')
        if 'idAI.freezeFactor = 1' not in onehp:failures.append('one-HP target did not freeze')
        if 'common/freeze_death_overlay' not in onehp:failures.append('one-HP ice overlay absent')
    result={'passed':not failures,'failures':failures,'exit':exit_code,'seconds':round(time.monotonic()-started,2),
            'log':str(log_path),'operation':args.operation,'slot':args.slot,
            **identity}
    (output/(tag+'.json')).write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result,indent=2))
    return 1 if failures else 0


if __name__=='__main__':raise SystemExit(main())
