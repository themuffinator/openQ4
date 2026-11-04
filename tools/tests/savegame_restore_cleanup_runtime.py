#!/usr/bin/env python3
"""Windowed SP restore/early-rejection/reload probe using a current SP save.

The synthetic fixture changes only the script checksum and recomputes the whole
file CRC. It exercises the established fresh-map fallback after object creation,
not arbitrary deep object corruption or historical-format compatibility.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import struct
import subprocess
import time
import zlib

ROOT=Path(__file__).resolve().parents[2]

def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()
def checksum_fixture(data):
    magic,version,protected,crc=struct.unpack_from('<4sIII',data,len(data)-16)
    assert (magic,version,protected)==(b'OQ4I',1,len(data)-16)
    assert zlib.crc32(data[:protected])==crc
    position=data.index(b'OQ4S')
    def integer():
        nonlocal position
        value=struct.unpack_from('<i',data,position)[0]; position+=4; return value
    def string():
        nonlocal position
        length=integer(); assert 0<=length<4096
        result=data[position:position+length].decode('ascii'); position+=length; return result
    assert integer()==int.from_bytes(b'OQ4S','little') and integer()==5
    build=integer(); snapshot=string(); source_count=integer(); abi=string()
    assert abi=='le64-portable1' and len(snapshot)==64
    objects=integer(); assert 0<objects<20000
    classes=[string() for _ in range(objects)]
    assert all(re.fullmatch(r'[A-Za-z_][A-Za-z0-9_]*',name) for name in classes)
    files=integer(); assert 0<=files<4096
    scripts=[string() for _ in range(files)]
    filename=string(); filenum=integer()
    changed=0
    while True:
        index=integer()
        if index==-1: break
        assert 0<=index<1048576
        position+=1; changed+=1; assert changed<1048576
    variables=integer(); assert 0<=variables<1048576
    # The first sync is the server-info dictionary after the program's five
    # watermarks, checksum and skill. Validate both boundaries and its contents.
    sync=data.index(b'OQ4Y',position)
    assert struct.unpack_from('<i',data,sync+4)[0]==0 and sync-position>=28
    watermarks=struct.unpack_from('<5i',data,sync-28)
    assert all(0<=value<1048576 for value in watermarks)
    skill=struct.unpack_from('<i',data,sync-4)[0]; assert 0<=skill<=3
    checksum_offset=sync-8
    position=sync+8
    count=integer(); assert 0<count<256
    server={}
    for _ in range(count):
        key=string(); server[key.lower()]=string()
    assert re.fullmatch(r'game/[A-Za-z0-9_/]+', server['si_map']) and server['si_gametype']=='singleplayer'
    result=bytearray(data); result[checksum_offset]^=1
    struct.pack_into('<I',result,len(result)-4,zlib.crc32(result[:protected]))
    return bytes(result),dict(build=build,snapshot=snapshot,source_count=source_count,abi=abi,map=server['si_map'],
        objects=objects,client_effects=classes.count('rvClientEffect'),scripts=scripts,filename=filename,
        filenum=filenum,changed_variables=changed,variables=variables,watermarks=watermarks,
        checksum_offset=checksum_offset,server_info_sync=sync)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-save',required=True,type=Path,help='Current Windows x64 v5 SP save; read only.')
    parser.add_argument('--basepath',required=True,type=Path,help='Retail asset root with q4base PK4s.')
    parser.add_argument('--output',required=True,type=Path,help='New evidence directory below the project .tmp directory.')
    parser.add_argument('--timeout',type=int,default=180)
    args=parser.parse_args()
    if os.name!='nt': parser.error('This gameplay probe targets the staged Windows x64 runtime; native cleanup tests also run on Linux.')
    OUT=args.output.resolve(); SOURCE=args.source_save.resolve(); RUNTIME=ROOT/'.install'
    if (ROOT/'.tmp').resolve() not in OUT.parents: parser.error('Output must be a new directory below project .tmp.')
    SAVE=OUT/'save'; GAME=SAVE/'baseoq4'
    for relative in ('openQ4-client_x64.exe','baseoq4/game-sp_x64.dll','baseoq4/game-mp_x64.dll'):
        if sha(ROOT/'builddir'/relative)!=sha(RUNTIME/relative): parser.error('Stage the current build before running this probe: '+relative)
    OUT.mkdir(parents=True,exist_ok=False)
    (GAME/'savegames').mkdir(parents=True)
    original=SOURCE.read_bytes(); changed,metadata=checksum_fixture(original)
    good=GAME/'savegames/Good.save'; bad=GAME/'savegames/ScriptMismatch.save'
    good.write_bytes(original); bad.write_bytes(changed)
    for name,commands in {
        'initial.cfg':['echo CLEANUP_VALID_RESTORED','screenshot screenshots/good.tga',
            'set g_autoExecAfterMapLoad recovered.cfg','loadGame ScriptMismatch'],
        'recovered.cfg':['echo CLEANUP_SCRIPT_RECOVERY','screenshot screenshots/recovered.tga',
            'set g_autoExecAfterMapLoad final.cfg','loadGame Good'],
        'final.cfg':['echo CLEANUP_VALID_AGAIN','screenshot screenshots/final.tga','quit'],
    }.items(): (GAME/name).write_text('\n'.join(commands)+'\n')
    settings=dict(fs_game='baseoq4',fs_basepath=str(args.basepath.resolve()),fs_savepath=str(SAVE),
        fs_devpath=str(SAVE),com_gameMode='SP',si_gameType='singleplayer',ui_retained='1',ui_autoJoin='0',
        win_allowMultipleInstances='1',sys_consoleWindow='0',in_tty='0',in_mouse='0',in_joystick='0',
        s_noSound='1',r_fullscreen='0',r_fullscreenDesktop='0',r_borderless='0',r_borderlessDefaultMigrated='1',
        r_hiddenWindow='1',r_mode='-1',r_customWidth='640',r_customHeight='480',r_windowWidth='640',r_windowHeight='480',
        r_renderApi='gl',r_swapInterval='0',com_maxfps='60',com_skipLoadingContinue='1',com_loadingContinueAutoAdvance='1',
        g_autoSkipCinematics='1',g_autoExecAfterMapLoad='initial.cfg',g_autoExecAfterMapLoadDelayMs='750',
        logFile='2',logFileName='logs/openq4.log',developer='1')
    executable=RUNTIME/'openQ4-client_x64.exe'
    command=[str(executable),*[part for key,value in settings.items() for part in ('+set',key,value)],'+loadGame','Good']
    report=dict(fixture=metadata,source_save=dict(path=str(SOURCE),sha256=sha(SOURCE)),command=command,
        harness_sha256=sha(Path(__file__)),
        runtime={str(p.relative_to(RUNTIME)):sha(p) for p in [executable,RUNTIME/'baseoq4/game-sp_x64.dll',RUNTIME/'baseoq4/game-mp_x64.dll']},
        before={p.name:sha(p) for p in [good,bad]},scope='Current v5 script-checksum rejection after object creation, automatic fresh-map fallback and valid load again. Hidden window; input disabled; engine screenshots only.')
    (OUT/'plan.json').write_text(json.dumps(report,indent=2)+'\n')
    startup=subprocess.STARTUPINFO(); startup.dwFlags|=subprocess.STARTF_USESHOWWINDOW; startup.wShowWindow=subprocess.SW_HIDE
    begin=time.monotonic()
    with (OUT/'stdout.log').open('w') as stream:
        process=subprocess.Popen(command,cwd=RUNTIME,stdout=stream,stderr=subprocess.STDOUT,startupinfo=startup)
        try: code=process.wait(timeout=args.timeout)
        except subprocess.TimeoutExpired:
            process.terminate(); code=process.wait(timeout=15); report['timed_out']=True
    report.update(exit_code=code,elapsed=time.monotonic()-begin,after={p.name:sha(p) for p in [good,bad]})
    log=GAME/'logs/openq4.log'; text=log.read_text(errors='replace')
    markers=['CLEANUP_VALID_RESTORED','CLEANUP_SCRIPT_RECOVERY','CLEANUP_VALID_AGAIN']
    positions=[text.find(value) for value in markers]
    report['markers']={value:text.count(value) for value in markers}
    report['restore_attempts']=text.count('Game Map Init SaveGame')
    report['screenshots']={name:sha(GAME/f'screenshots/{name}.tga') for name in ('good','recovered','final') if (GAME/f'screenshots/{name}.tga').is_file()}
    report['engine_log_sha256']=sha(log)
    report['passed']=code==0 and not report.get('timed_out') and all(report['markers'][m]==1 for m in markers) and positions==sorted(positions) and report['restore_attempts']==3 and report['before']==report['after'] and len(report['screenshots'])==3 and 'ERROR:' not in text
    (OUT/'report.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(dict(passed=report['passed'],elapsed=report['elapsed'],markers=report['markers'],fixture=metadata),indent=2))
    return 0 if report['passed'] else 1

if __name__=='__main__': raise SystemExit(main())
