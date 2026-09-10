#!/usr/bin/env python3
"""Actual platform/pushed/SDL retirement storage plus the real route predicate.

The projection copies queue globals, actual EventLoop class, production methods,
enum definitions and input helpers. Logging/allocation/locks/pump are counted;
retirement/translator facts are explicit trusted fixtures. No OS/native input.
"""
from __future__ import annotations
import argparse, hashlib, json, os, platform, re, shutil, subprocess, tempfile
from pathlib import Path
from filesystem_case_segments import function_body
import sys_event_queue_ownership as queues
import sdl3_input_disposition as sdl

ROOT=Path(__file__).resolve().parents[2]
FILES=['src/sys/EventRetirement.h','src/sys/win32/win_main.cpp','src/sys/posix/posix_main.cpp',
    'src/framework/EventLoop.h','src/framework/EventLoop.cpp','src/sys/sdl3/InputDisposition.h','src/sys/sdl3/sdl3_backend.cpp',
    'src/framework/NativeInputRoute.h','src/framework/NativeInputRoute.cpp','src/ui/retained/TextInputBroker.h',
    'src/ui/retained/TextInput.h','src/ui/retained/NativeTextDocument.h','src/sys/EventDisposition.h','src/sys/EventDisposition.cpp',
    'src/sys/EventQueueContinuity.h','src/sys/EventQueueContinuity.cpp','src/sys/KeyEventMetadata.h','src/sys/sys_public.h','src/framework/KeyInput.h',
    'tools/tests/native/NativeEventRetirementTest.cpp','tools/tests/native_event_retirement.py','tools/tests/sys_event_queue_ownership.py',
    'tools/tests/filesystem_case_segments.py','tools/tests/sdl3_input_disposition.py']
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def read(name):return (ROOT/name).read_text(encoding='utf-8')
def replace(source,old,new,count=1):
    if source.count(old)!=count:raise RuntimeError(f'Nonunique mutation {old!r}: {source.count(old)} != {count}')
    return source.replace(old,new)

def projection(target,sources):
    queues.ROOT=ROOT
    platform_source=sources['platform'];loop=sources['loop'];backend=sources['sdl']
    result=queues.SUPPORT+queues.event_types()+'''\n#include <functional>
#include <mutex>
#include <thread>
#include "src/sys/EventRetirement.h"
#include "src/sys/sdl3/InputDisposition.h"
#include "src/sys/KeyEventMetadata.h"
static constexpr int CRITICAL_SECTION_ONE=1;
static std::recursive_mutex storageMutex;
static thread_local int lockDepth=0;
static unsigned pumps=0, deferred=0, journalReads=0, realEventReads=0;
static std::function<void()> beforeLock;
static void Sys_EnterCriticalSection(int section) {
    Check(section==CRITICAL_SECTION_ONE,"owning SDL section");
    if(beforeLock){auto call=std::move(beforeLock);beforeLock={};call();}
    storageMutex.lock();++lockDepth;
}
static void Sys_LeaveCriticalSection(int section) {Check(section==CRITICAL_SECTION_ONE&&lockDepth>0,"balanced SDL section");--lockDepth;storageMutex.unlock();}
[[maybe_unused]] static bool Sys_SDL_PumpEvents(){Check(lockDepth==0,"legacy POSIX pump is outside storage");++pumps;return true;}
'''
    result+=sdl.enum(read('src/framework/KeyInput.h'),'keyNum_t')+'\n'+sdl.enum(read('src/sys/sys_public.h'),'sys_mEvents')+'\n'
    result+=platform_source[platform_source.index('#define\tMAX_QUED_EVENTS'):platform_source.index('// Only pending queue entries')]
    signatures=['static void Sys_DiscardQueuedEvent(', 'void Sys_QueEvent(' if target=='windows' else 'void Posix_QueEvent(',
        'sysEvent_t Sys_GetEvent(', 'void Sys_ClearEvents(', 'bool Sys_QueTrackedEvent(',
        'sysEventTransfer_t Sys_TakeEventWithDisposition(', 'sysEventTransfer_t Sys_PeekEventForRetirement(', 'sysEventTransfer_t Sys_TakeEventForRetirement(']
    result+='\n'.join(function_body(platform_source,name) for name in signatures)+'\n'
    if target=='posix':result+='void Sys_QueEvent(int,sysEventType_t type,int value,int value2,int length,void* ptr){++deferred;Posix_QueEvent(type,value,value2,length,ptr);}\n'
    result+='''
static void QueueLegacy(sysEventType_t type,int value,int length=0,void* ptr=nullptr) { Sys_QueEvent(0,type,value,-value,length,ptr); }
class idFile {};
class idCVar { int value=0; public: int GetInteger()const {++journalReads;return value;} void SetInteger(int v){value=v;} };
static int Sys_Milliseconds(){return 0;}
struct Cvars {bool IsInitialized()const{return true;} bool CommandContainsPrivateCVar(const char*)const{return false;}} cvars,*cvarSystem=&cvars;
#define private public
#include "src/framework/EventLoop.h"
#undef private
idCVar idEventLoop::com_journal;
sysEvent_t idEventLoop::GetRealEvent(){++realEventReads;return Sys_GetEvent();}
'''
    result+=re.search(r'static std::uint64_t pushedRetirementHighwater[^;]+;',loop).group()+'\n'
    result+=re.search(r'static const int MAX_JOURNAL_EVENT_PAYLOAD[^;]+;',loop).group()+'\n'
    for name in ['static bool EventLoop_IsPrivateConsoleEvent(', 'static int EventLoop_EventType(',
                 'static const char *EventLoop_ValidateHeader(', 'static const char *EventLoop_ValidatePayload(']:
        result+=function_body(loop,name)+'\n'
    result+=loop[loop.index('class idScopedEventPayload {'):loop.index('static const char *EventLoop_ReadJournalEvent(')]
    for name in ['idEventLoop::idEventLoop(', 'idEventLoop::~idEventLoop(', 'void idEventLoop::PushEvent(',
        'bool idEventLoop::PushEventWithDisposition(', 'sysEventTransfer_t idEventLoop::TakeEventWithDisposition(',
        'sysEventTransfer_t idEventLoop::PeekEventForRetirement(', 'sysEventTransfer_t idEventLoop::TakeEventForRetirement(',
        'void idEventLoop::ClearPushedEvents(', 'sysEvent_t idEventLoop::GetEvent(']:result+=function_body(loop,name)+'\n'
    result+=backend[backend.index('typedef struct {\n\tint\t\tkey;'):backend.index('static sdlJoystickAxisEvent_t s_polledJoystick')]
    clear=function_body(backend,'static void SDL3_ClearInputQueues(')
    ignored={'s_keyboardHead','s_mouseHead','s_polledKeyboardCount','s_polledMouseCount','s_polledJoystickCount',
             's_keyboardDispositionState','s_inputDispositionHighwater'}
    result+='\nstatic int s_polledJoystickCount=0;\n'
    result+='\n'.join('static double '+n+'=1;' for n in sorted(set(re.findall(r'^\t(s_\w+) =',clear,re.M))-ignored))+'\n'
    for name in ['static bool SDL3_IsMousePollActionValid(', 'static bool SDL3_ShouldQueueMousePoll(',
        'static void SDL3_QueueKeyboardInput(', 'static void SDL3_QueueMouseInput(']:result+=function_body(backend,name)+'\n'
    result+=clear+'\n'
    result+=backend[backend.index('// All checked methods acquire SDL storage first,'):backend.index('int Sys_PollJoystickInputEvents(void) {')]
    result+='\n#include "tools/tests/native/NativeEventRetirementTest.cpp"\n'
    return result

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler');parser.add_argument('--msvc-debug',action='store_true')
    parser.add_argument('--sanitizers',action='store_true');parser.add_argument('--no-mutations',action='store_true')
    args=parser.parse_args();compiler=args.compiler or shutil.which('clang++') or shutil.which('g++')
    if not compiler:raise RuntimeError('C++20 compiler required')
    msvc=Path(compiler).name.lower() in ('cl','cl.exe')
    if args.msvc_debug and not msvc:raise RuntimeError('--msvc-debug requires cl.exe')
    scratch=Path(tempfile.mkdtemp(prefix='native-event-retirement-',dir=ROOT/'.tmp'))
    env=dict(os.environ,TEMP=str(scratch),TMP=str(scratch),TMPDIR=str(scratch))
    if args.sanitizers:env.update(ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
    original={name:sha(ROOT/name) for name in FILES};results=[]
    cases=[('windows','windows',None,None,None,1),('posix','posix',None,None,None,1)]
    if not args.no_mutations:
        cases += [
            ('platform-thread','windows','platform','if (!Sys_EventDispositionBoundThread()) return sysEventTransfer_t::Refused;','if (false) return sysEventTransfer_t::Refused;',1),
            ('platform-permit','windows','platform','if (!route.AllowsCancellation(permit, head)) return sysEventTransfer_t::Refused;','if (false && !route.AllowsCancellation(permit, head)) return sysEventTransfer_t::Refused;',1),
            ('platform-reused-id','posix','platform','eventRetirementSerials[slot] = ++eventRetirementHighwater;','eventRetirementSerials[slot] = 1; ++eventRetirementHighwater;',1),
            ('platform-saturation','windows','platform','if (eventRetirementHighwater == (std::numeric_limits<std::uint64_t>::max)()) return false;','if (false) return false;',1),
            ('platform-reset-counter','posix','platform','eventHead = eventTail = 0;','eventHead = eventTail = 0; eventRetirementHighwater=0;',1),
            ('platform-early-output','windows','platform','const auto status = Sys_PeekEventForRetirement(head);','event = {}; tag = {}; const auto status = Sys_PeekEventForRetirement(head);',1),
            ('pushed-permit','windows','loop','if (!route.AllowsCancellation(permit, head)) return sysEventTransfer_t::Refused;','if (false && !route.AllowsCancellation(permit, head)) return sysEventTransfer_t::Refused;',1),
            ('pushed-bypass','posix','loop','if (com_pushedEventsHead <= com_pushedEventsTail) return Sys_TakeEventForRetirement(route, permit, event, tag);','return Sys_TakeEventForRetirement(route, permit, event, tag);',1),
            ('pushed-reused-id','windows','loop','com_pushedRetirementSerials[slot] = ++pushedRetirementHighwater;','com_pushedRetirementSerials[slot] = 1; ++pushedRetirementHighwater;',1),
            ('pushed-saturation','posix','loop','if (pushedRetirementHighwater == (std::numeric_limits<std::uint64_t>::max)()) return false;','if (false) return false;',1),
            ('pushed-reset-counter','windows','loop','com_pushedEventsHead = com_pushedEventsTail = 0;','com_pushedEventsHead = com_pushedEventsTail = 0; pushedRetirementHighwater=0;',2),
            ('pushed-journal-cleanup-block','posix','loop','if (!Sys_EventDispositionBoundThread()) return sysEventTransfer_t::Refused;','if (!Sys_EventDispositionBoundThread() || com_journal.GetInteger()!=0) return sysEventTransfer_t::Refused;',2),
            ('sdl-permit','windows','sdl','if (!route.AllowsCancellation(permit,actual)) return sysEventTransfer_t::Refused;','if (false && !route.AllowsCancellation(permit,actual)) return sysEventTransfer_t::Refused;',1),
            ('sdl-thread','posix','sdl','if (!Sys_EventDispositionBoundThread()) return sysEventTransfer_t::Refused;','if (false) return sysEventTransfer_t::Refused;',1),
            ('sdl-retired-epoch-block','windows','sdl','if (!Sys_EventDispositionBoundThread()) return sysEventTransfer_t::Refused;','if (!Sys_EventDispositionBoundThread() || !Sys_EventDispositionEpoch()) return sysEventTransfer_t::Refused;',1),
            ('sdl-ring-key-reuse','windows','sdl','s_keyboardRetirementSerials[s_keyboardHead] = ++s_inputDispositionHighwater;','s_keyboardRetirementSerials[s_keyboardHead] = 1; ++s_inputDispositionHighwater;',1),
            ('sdl-ring-mouse-reuse','posix','sdl','s_mouseRetirementSerials[s_mouseHead] = ++s_inputDispositionHighwater;','s_mouseRetirementSerials[s_mouseHead] = 1; ++s_inputDispositionHighwater;',1),
            ('sdl-slice-ring-id-alias','windows','sdl','state.slice = {epoch, token, ++s_inputDispositionHighwater, lane,','state.slice = {epoch, token, s_inputDispositionHighwater++, lane,',1),
            ('sdl-saturation','posix','sdl','if (s_inputDispositionHighwater == (std::numeric_limits<std::uint64_t>::max)()) return false;','if (false) return false;',2),
            ('sdl-clear-counter-reuse','windows','sdl','// s_inputDispositionHighwater deliberately survives clear/shutdown/re-init.','s_inputDispositionHighwater=0;',1),
            ('sdl-legacy-slice-bypass','posix','sdl','if (count != 0) return sysEventTransfer_t::Refused;','if (false) return sysEventTransfer_t::Refused;',1),
            ('sdl-slice-epoch-unchecked','windows','sdl','state.slice.epoch != candidate.tag.dispatchEpoch ||','false ||',1),
            ('sdl-slice-token-unchecked','posix','sdl','state.slice.streamToken != candidate.tag.streamToken','false',1),
            ('sdl-slice-lane-unchecked','windows','sdl','state.slice.lane != expectedLane ||','(false && state.slice.lane != expectedLane) ||',1),
            ('sdl-slice-count-unchecked','posix','sdl','state.slice.count != static_cast<unsigned>(count) ||','false ||',1),
            ('sdl-child-footprint-lost','windows','sdl','value.time, 0, 0, tag.deferredEmission}};','value.time, 0, 0, 0}};',1),
            ('sdl-child-transfer-lost','posix','sdl','return {value.key, value.down, value.time, tag};','auto changed=tag; changed.deferredEmission=0; return {value.key, value.down, value.time, changed};',1),
            ('sdl-empty-end-nonempty','windows','sdl','state.next != expected.count) return false;','false) return false;',1),
            ('sdl-empty-end-wrong-slice','posix','sdl','expected != state.slice ||','false ||',1),
            ('sdl-empty-end-retired-block','windows','sdl','if (!Sys_EventDispositionBoundThread() || !state.checked || !expected.serial ||','if (!Sys_EventDispositionEpoch() || !state.checked || !expected.serial ||',1),
        ]
    for name,target,part,old,new,count in cases:
        folder=scratch/name;folder.mkdir()
        for relative in FILES:
            dest=folder/relative;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(ROOT/relative,dest)
        sources={'platform':read('src/sys/win32/win_main.cpp' if target=='windows' else 'src/sys/posix/posix_main.cpp'),
                 'loop':read('src/framework/EventLoop.cpp'),'sdl':read('src/sys/sdl3/sdl3_backend.cpp')}
        if part:sources[part]=replace(sources[part],old,new,count)
        (folder/'unit.cpp').write_text(projection(target,sources),encoding='utf-8',newline='\n')
        exe=folder/('test.exe' if os.name=='nt' else 'test')
        units=['unit.cpp','src/framework/NativeInputRoute.cpp','src/sys/EventDisposition.cpp','src/sys/EventQueueContinuity.cpp']
        if msvc:
            command=[compiler,'/nologo','/std:c++20','/EHsc','/W4','/WX','/wd4100','/MTd' if args.msvc_debug else '/MT',
                '/I.','/Fo'+str(folder)+os.sep,'/Fe'+str(exe),*units]
            if args.msvc_debug:command+=['/D_DEBUG']
            else:command+=['/D_ITERATOR_DEBUG_LEVEL=0']
            if target=='posix':command+=['/DOPENQ4_SDL3_POSIX_HOST']
        else:
            command=[compiler,'-std=c++20','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-pthread','-I.',*units,'-o',str(exe)]
            if target=='posix':command+=['-DOPENQ4_SDL3_POSIX_HOST']
            if args.sanitizers:command[1:1]=['-fsanitize=address,undefined','-fno-omit-frame-pointer','-fno-pie','-no-pie']
        compiled=subprocess.run(command,cwd=folder,env=env,capture_output=True,text=True,timeout=120)
        (folder/'compile.log').write_text(compiled.stdout+compiled.stderr,encoding='utf-8')
        if compiled.returncode:raise RuntimeError(name+' failed compile: '+str(folder/'compile.log'))
        run=subprocess.run([str(exe)],cwd=folder,env=env,capture_output=True,text=True,timeout=60)
        (folder/'run.log').write_text(run.stdout+run.stderr,encoding='utf-8')
        if (run.returncode==0)!=(part is None):raise RuntimeError(name+' unexpected run result: '+str(folder/'run.log'))
        if part and 'FAIL:' not in run.stdout+run.stderr:raise RuntimeError(name+' failed without a behavioral assertion: '+str(folder/'run.log'))
        results.append({'case':name,'returncode':run.returncode,'output':run.stdout,'command':command,
            'unit_sha256':sha(folder/'unit.cpp'),'exe_sha256':sha(exe),'compile_log_sha256':sha(folder/'compile.log'),'run_log_sha256':sha(folder/'run.log')})
        print(name+': '+('compiled mutation rejected' if part else run.stdout.strip()),flush=True)
    assert original=={name:sha(ROOT/name) for name in FILES},'Source drift during run'
    (scratch/'result.json').write_text(json.dumps({'passed':True,'sources':original,'platform':platform.platform(),
        'sanitizers':args.sanitizers,'msvc_debug':args.msvc_debug,'cases':results},indent=2)+'\n',encoding='utf-8')
    print(scratch/'result.json')
if __name__=='__main__':main()
