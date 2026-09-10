#!/usr/bin/env python3
"""Compile actual SDL polled storage methods for both host branches, without input.

Owns only temporary projections. Queue declarations, key/action enums, Clear,
admission, Poll/Return/End and checked helpers come from production source.
Critical sections, pump and legacy SE_KEY emission are counted boundaries.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import tempfile
from filesystem_case_segments import function_body

ROOT=Path(__file__).resolve().parents[2]
FILES=['src/sys/sdl3/sdl3_backend.cpp','src/sys/sdl3/InputDisposition.h',
       'src/sys/EventDisposition.h','src/sys/EventDisposition.cpp',
       'src/sys/EventQueueContinuity.h','src/sys/EventQueueContinuity.cpp',
       'src/sys/sys_public.h','src/framework/KeyInput.h',
       'tools/tests/native/InputDispositionStorageTest.cpp','tools/tests/sdl3_input_disposition.py',
       'tools/tests/filesystem_case_segments.py']
def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()
def replace(text,old,new,count=1):
    if text.count(old)!=count: raise RuntimeError(f'Nonunique mutation {old!r}: {text.count(old)} != {count}')
    return text.replace(old,new)
def enum(source,name):
    end=source.index('} '+name+';')+len('} '+name+';')
    start=source.rfind('typedef enum {',0,end)
    if start<0: raise RuntimeError(name+' enum missing')
    return source[start:end]
def projection(source):
    keys=(ROOT/'src/framework/KeyInput.h').read_text(encoding='utf-8')
    public=(ROOT/'src/sys/sys_public.h').read_text(encoding='utf-8')
    constants='\n'.join([enum(keys,'keyNum_t'),enum(public,'sys_mEvents'),enum(public,'sysEventType_t')])
    begin=source.index('typedef struct {\n\tint\t\tkey;')
    end=source.index('static sdlJoystickAxisEvent_t s_polledJoystick',begin)
    globals=source[begin:end]
    clear=function_body(source,'static void SDL3_ClearInputQueues(')
    # Unrelated pointer/gyro/touch reset scalars are stand-ins; the queue arrays,
    # counts, tags and identities above are the production declarations.
    ignored={'s_keyboardHead','s_mouseHead','s_polledKeyboardCount','s_polledMouseCount','s_polledJoystickCount',
             's_keyboardDispositionState','s_inputDispositionHighwater'}
    scalar_names=sorted(set(re.findall(r'^\t(s_\w+) =',clear,re.M))-ignored)
    resets='\n'.join('static double '+n+' = 1;' for n in scalar_names)
    methods=source[source.index('// All checked methods acquire SDL storage first,'):source.index('int Sys_PollJoystickInputEvents(void) {')]
    pieces=[function_body(source,sig) for sig in ('static bool SDL3_IsMousePollActionValid(',
        'static bool SDL3_ShouldQueueMousePoll(', 'static void SDL3_QueueKeyboardInput(',
        'static void SDL3_QueueMouseInput(', 'void Sys_ClearInputEvents(')]
    prelude=r'''
#include <array>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <limits>
#include <mutex>
#include <thread>
#include <vector>
#include "src/sys/sdl3/InputDisposition.h"
#include "src/sys/EventQueueContinuity.h"
static unsigned checks=0;
static void Check(bool value,const char* message) {
    ++checks;
    if(!value){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}
}
static constexpr int CRITICAL_SECTION_ONE=1;
static std::recursive_mutex storageMutex;
static thread_local int lockDepth=0;
static std::function<void()> beforeLock;
static void Sys_EnterCriticalSection(int section) {
    Check(section==CRITICAL_SECTION_ONE,"correct SDL storage section");
    if(beforeLock){auto call=std::move(beforeLock);beforeLock={};call();}
    storageMutex.lock();++lockDepth;
}
static void Sys_LeaveCriticalSection(int section) {
    Check(section==CRITICAL_SECTION_ONE&&lockDepth>0,"balanced SDL storage lock");
    --lockDepth;storageMutex.unlock();
}
static std::size_t pumps=0;
static bool Sys_SDL_PumpEvents(){Check(lockDepth==0,"legacy pump precedes storage lock");++pumps;return true;}
struct Emission{int time,key;bool down;};
static std::vector<Emission> emissions;
'''
    emission=r'''
static void Sys_QueEvent(int time,sysEventType_t type,int key,int down,int length,void* pointer){
    Check(lockDepth==0,"legacy deferred emission occurs after storage unlock");
    Check(type==SE_KEY&&length==0&&pointer==nullptr,"legacy modifier emission shape preserved");
    emissions.push_back({time,key,down!=0});
}
'''
    return '\n'.join([prelude,constants,emission,globals,'static int s_polledJoystickCount=0;',resets,clear,*pieces,methods,
        '#include "tools/tests/native/InputDispositionStorageTest.cpp"'])
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler')
    parser.add_argument('--sanitizers',action='store_true')
    parser.add_argument('--msvc-debug',action='store_true')
    parser.add_argument('--no-mutations',action='store_true')
    args=parser.parse_args()
    compiler=args.compiler or next((p for name in ('clang++','g++','c++') if (p:=shutil.which(name))),None)
    if not compiler: raise RuntimeError('C++17 compiler required')
    msvc=Path(compiler).name.lower() in ('cl','cl.exe')
    if args.msvc_debug and not msvc: raise RuntimeError('--msvc-debug requires cl.exe')
    (ROOT/'.tmp').mkdir(exist_ok=True)
    scratch=Path(tempfile.mkdtemp(prefix='sdl-input-disposition-',dir=ROOT/'.tmp'))
    env=dict(os.environ,TEMP=str(scratch),TMP=str(scratch),TMPDIR=str(scratch))
    if args.sanitizers:
        env.update(ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
    original={name:sha(ROOT/name) for name in FILES}
    source=(ROOT/FILES[0]).read_text(encoding='utf-8')
    service=(ROOT/'src/sys/EventDisposition.cpp').read_text(encoding='utf-8')
    mutants=[] if args.no_mutations else [
        ('key-tag-lost','source','s_keyboardDisposition[s_keyboardHead] = input.disposition;','s_keyboardDisposition[s_keyboardHead] = {};',1),
        ('mouse-tag-lost','source','s_mouseDisposition[s_mouseHead] = input.disposition;','s_mouseDisposition[s_mouseHead] = {};',1),
        ('child-lost','source','return {value.key, value.down, value.time, tag};','auto altered=tag; altered.deferredEmission=0; return {value.key, value.down, value.time, altered};',1),
        ('full-caller-lost','source','Sys_InvalidateEventQueue(); return false; // Preserve full ring and caller.','Sys_InvalidateEventQueue(); input = {}; return false; // Deliberate caller loss.',2),
        ('unchecked-parent','source','Sys_EventDispositionTagCurrent(tag.parent)','tag.parent.ShapeValid()',1),
        ('unchecked-current','source','if (!SDL3_InputValid(queue[i], tags[i])) return false;','if (false && !SDL3_InputValid(queue[i], tags[i])) return false;',1),
        ('early-head-copy','source','const int& head, int& tail, Value (&polled)','int head, int& tail, Value (&polled)',1),
        ('slice-overwrite','source','state.checked || count != 0 ||','false ||',1),
        ('slice-serial-unchecked','source','state.checked && expected == state.slice && expected.serial &&','state.checked && expected.epoch == state.slice.epoch && expected.streamToken == state.slice.streamToken && expected.lane == state.slice.lane && expected.count == state.slice.count && expected.serial &&',1),
        ('slice-lane-unchecked','source','state.checked && expected == state.slice && expected.serial &&','state.checked && expected.epoch == state.slice.epoch && expected.streamToken == state.slice.streamToken && expected.serial == state.slice.serial && expected.count == state.slice.count && expected.serial &&',1),
        ('slot-order-unchecked','source','expected.index != state.next ||','false ||',1),
        ('peek-consumes','source','slot = {state.slice, state.next};','slot = {state.slice, state.next}; ++const_cast<sdlInputDispositionState_t&>(state).next;',1),
        ('end-before-takes','source','state.next != state.slice.count','false',1),
        ('legacy-key-bypass','source','if (!s_keyboardDisposition[s_keyboardTail].Empty()) {','if (false && !s_keyboardDisposition[s_keyboardTail].Empty()) {',1),
        ('legacy-mouse-bypass','source','if (!s_mouseDisposition[s_mouseTail].Empty()) {','if (false && !s_mouseDisposition[s_mouseTail].Empty()) {',1),
        ('legacy-end-clears','source','if (s_keyboardDispositionState.checked) { Sys_InvalidateEventQueue(); return; }','if (s_keyboardDispositionState.checked) { Sys_InvalidateEventQueue(); s_keyboardDispositionState = {}; return; }',1),
        ('clear-keeps-serial-reuse','source','// s_inputDispositionHighwater deliberately survives clear/shutdown/re-init.','s_inputDispositionHighwater = 0;',1),
        ('clear-continuity-lost','source','Sys_InvalidateEventQueue(); // Invalidate before clearing even an empty slice.','// Deliberate continuity omission.',1),
        ('eviction-continuity-lost','source','Sys_InvalidateEventQueue(); // Legacy eviction is observable before loss.','// Deliberate eviction continuity omission.',2),
        ('worker-bound','service','return dispositionBound && dispositionThread == std::this_thread::get_id();','return dispositionBound;',1),
        ('bound-lost-on-retire','service','dispositionEpoch = 0; return true;','dispositionEpoch = 0; dispositionBound = false; return true;',1),
    ]
    cases=[('windows','windows',None,None,None,0),('posix','posix',None,None,None,0)]+[
        (name,'posix' if 'mouse' in name else 'windows',part,old,new,count) for name,part,old,new,count in mutants]
    evidence={'passed':False,'sources':original,'platform':platform.platform(),'cases':[],
        'scope':'Actual SDL ring/polled scalar methods, real disposition/continuity services, counted section/pump/SE_KEY boundaries; no full backend build, device input, delivery, native activation or stale-retirement qualification'}
    logpath=scratch/'test.log'
    with logpath.open('w',encoding='utf-8',newline='\n') as log:
        def run(command):
            result=subprocess.run(command,cwd=scratch,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,encoding='utf-8',errors='replace',timeout=120)
            log.write(json.dumps(command)+'\n'+result.stdout+f'\nexit_code={result.returncode}\n');log.flush()
            return result
        try:
            for name,host,part,old,new,count in cases:
                variant=scratch/name;variant.mkdir()
                selected=replace(source,old,new,count) if part=='source' else source
                selectedService=replace(service,old,new,count) if part=='service' else service
                unit=variant/'unit.cpp';unit.write_text(projection(selected),encoding='utf-8',newline='\n')
                for relative in ('src/sys/EventDisposition.h','src/sys/EventQueueContinuity.h','src/sys/sdl3/InputDisposition.h'):
                    path=variant/relative;path.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(ROOT/relative,path)
                servicepath=variant/'src/sys/EventDisposition.cpp';servicepath.write_text(selectedService,encoding='utf-8',newline='\n')
                output=variant/('test.exe' if os.name=='nt' else 'test')
                sources=[str(unit),str(servicepath),str(ROOT/'src/sys/EventQueueContinuity.cpp')]
                if msvc:
                    command=[compiler,'/nologo','/std:c++17','/EHsc','/W4','/WX','/wd4505','/MTd' if args.msvc_debug else '/MT','/Od' if args.msvc_debug else '/O2']
                    if host=='posix':command+=['/DOPENQ4_SDL3_POSIX_HOST=1']
                    command+=['/I'+str(variant),'/I'+str(ROOT),*sources,'/Fo'+str(variant)+os.sep,'/Fe'+str(output)]
                else:
                    command=[compiler,'-std=c++17','-O1' if args.sanitizers else '-O2','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-const-variable']
                    if host=='posix':command+=['-DOPENQ4_SDL3_POSIX_HOST=1']
                    if args.sanitizers:command+=['-fsanitize=address,undefined','-fno-omit-frame-pointer','-g']
                    if os.name!='nt':command+=['-pthread']
                    command+=['-I',str(variant),'-I',str(ROOT),*sources,'-o',str(output)]
                compiled=run(command)
                if compiled.returncode:raise RuntimeError(name+' compile failed:\n'+compiled.stdout)
                result=run([str(output)])
                evidence['cases'].append({'name':name,'host':host,'exit_code':result.returncode,'output':result.stdout,'command':command,'projection_sha256':sha(unit),'binary_sha256':sha(output)})
                print(name+': '+result.stdout.strip(),flush=True)
                if not part and result.returncode:raise RuntimeError(name+' production failure')
                if part and (result.returncode==0 or 'FAIL:' not in result.stdout):raise RuntimeError(name+' did not fail a behavioral assertion')
            evidence.update(passed=True,mutation_count=len(mutants))
        except Exception as error:
            evidence['failure']=str(error);print(error,flush=True)
    evidence['sources_unchanged']=original=={name:sha(ROOT/name) for name in FILES}
    evidence['passed'] &= evidence['sources_unchanged']
    evidence['log_sha256']=sha(logpath)
    resultpath=scratch/'result.json';resultpath.write_text(json.dumps(evidence,indent=2)+'\n',encoding='utf-8',newline='\n')
    print('Evidence: '+str(resultpath),flush=True)
    return 0 if evidence['passed'] else 1
if __name__=='__main__':raise SystemExit(main())
