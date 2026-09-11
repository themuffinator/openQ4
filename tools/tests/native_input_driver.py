#!/usr/bin/env python3
"""Actual owned Session/poll sinks, stores, driver, ledger and native coordinator.

Engine allocator/logging/Session endpoints and SDL queue provider are counted;
Interaction/native document/reconciliation and all queue transfers are real.
No native OS activation, pumping, GUI, game or shared build is performed.
"""
from pathlib import Path
import argparse,configparser,hashlib,json,os,subprocess,tempfile,re
import native_event_disposition as disposition_test
import sdl3_clipboard_status as sdl_source
from filesystem_case_segments import function_body
import native_event_retirement as storage
ROOT=Path(__file__).resolve().parents[2]
def read(name):return (ROOT/name).read_text(encoding='utf-8-sig')
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--compiler',default='clang++');p.add_argument('--sdl-include',type=Path);p.add_argument('--msvc-debug',action='store_true');p.add_argument('--sanitize',action='store_true');p.add_argument('--mutations',action='store_true');a=p.parse_args()
 out=Path(tempfile.mkdtemp(prefix='native-input-driver-',dir=ROOT/'.tmp'));msvc=Path(a.compiler).name.lower() in ('cl','cl.exe')
 provision={'kind':'explicit-include'}
 if a.sdl_include:sdl=a.sdl_include.resolve()
 else:
  config=configparser.ConfigParser();config.read(ROOT/'subprojects/sdl3.wrap',encoding='utf-8');specification=dict(config['wrap-file'])
  assert specification['directory']=='SDL3-3.4.10','Review the SDL public header closure on update'
  sdl_source.ROOT=ROOT;sdl_source.FILES=['include/SDL3/'+name for name in disposition_test.SDL_HEADERS]
  prepared,provision,_=sdl_source.provision_source(None,specification,out);sdl=prepared/'include'
 if not (sdl/'SDL3/SDL_events.h').is_file():raise RuntimeError('Explicit SDL public header tree is absent')

 flags=['/nologo','/std:c++20','/EHsc','/W4','/WX','/wd4100','/wd4505','/wd4458','/MTd' if a.msvc_debug else '/MT'] if msvc else ['-std=c++20','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-parameter','-Wno-unused-variable','-Wno-misleading-indentation','-Wno-missing-field-initializers']
 includes=[out,ROOT,ROOT/'src/sys/sdl3',ROOT/'src/framework',ROOT/'src/ui/retained',ROOT/'src/ui/application',ROOT/'subprojects/packagefiles/sdl3/include',sdl]
 flags+=[('/I' if msvc else '-I')+str(p) for p in includes]
 if a.sanitize:flags+=['-fsanitize=address,undefined','-fno-omit-frame-pointer','-fno-pie','-no-pie']
 env={**os.environ,'TMP':str(out),'TEMP':str(out),'TMPDIR':str(out)}
 def bindings():return {str(p.relative_to(ROOT)):sha(p) for folder in ['src/framework','src/sys','src/ui/retained','src/ui/application','tools/tests'] for p in (ROOT/folder).rglob('*') if p.is_file() and p.suffix in ('.cpp','.h','.inc','.py')}
 report={'passed':False,'cases':[],'mutations':[],'sources':bindings(),'sdl_provision':provision,'sdl_headers':{str(p.relative_to(sdl)):sha(p) for p in (sdl/'SDL3').glob('*.h')}}
 def run(cmd,name):
  r=subprocess.run(cmd,cwd=out,env=env,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=180);(out/(name+'.log')).write_text(r.stdout+r.stderr);return r
 def obj(path,name):
  target=out/(name+'.obj');r=run([a.compiler]+flags+(['/c',str(path),'/Fo:'+str(target)] if msvc else ['-c',str(path),'-o',str(target)]),name)
  if r.returncode:raise RuntimeError(r.stdout+r.stderr)
  return target
 try:
  storage.ROOT=ROOT;storage.queues.ROOT=ROOT
  sources=['src/framework/NativeInputRoute.cpp','src/sys/EventDisposition.cpp','src/sys/EventQueueContinuity.cpp','src/sys/sdl3/NativeQueueBatch.cpp','src/sys/sdl3/NativeEventDisposition.cpp','src/ui/application/NativeTextCollectionCoordinator.cpp']
  sources+=['src/ui/retained/'+n+'.cpp' for n in ['Interaction','TextInput','TextEdit','NativeTextDocument','NativeTextEditor','Input','ScrollGeometry']]
  objects=[obj(ROOT/n,'core-'+str(i)) for i,n in enumerate(sources)]
  valid=out/'valid.cpp';doc=read('src/ui/retained/Document.cpp');valid.write_text('#include "Interaction.h"\n#include <cmath>\nnamespace openq4::ui {\n'+function_body(doc,'bool Utf8(')+function_body(doc,'bool ValidStateValue(')+'}\n');objects.append(obj(valid,'valid'))
  enums=storage.sdl.enum(read('src/framework/KeyInput.h'),'keyNum_t')+'\n'+storage.queues.event_types()+'\n'+storage.sdl.enum(read('src/sys/sys_public.h'),'sys_mEvents');(out/'engine_enums.inc').write_text(enums)
  inv=out/'inventory.cpp';inv.write_text(read('src/sys/sdl3/NativeInputEmissionInventory.cpp').replace('#include "../../idlib/precompiled.h"\n','').replace('#include "../../framework/KeyInput.h"','#include "engine_enums.inc"'));objects.append(obj(inv,'inventory'))
  nativeFixture=read('tools/tests/native/UiNativeCollectionCoordinatorTest.cpp');fixture=nativeFixture[nativeFixture.index('struct Fixture {'):nativeFixture.index('static void Success()')]
  for platform in ['windows','posix']:
   loop=read('src/framework/EventLoop.cpp');sdl=read('src/sys/sdl3/sdl3_backend.cpp');sys=read('src/sys/win32/win_main.cpp' if platform=='windows' else 'src/sys/posix/posix_main.cpp')
   body=storage.projection(platform,{'platform':sys,'loop':loop,'sdl':sdl}).replace('#include "tools/tests/native/NativeEventRetirementTest.cpp"','')
   body=body.replace('static void Mem_Free(void* pointer) {','static std::function<void()> onFree;\nstatic void Mem_Free(void* pointer) {\n auto call=std::move(onFree);if(call)call();')
   body=body.replace('void SetInteger(int v){value=v;}','bool GetBool()const{return value!=0;} void SetInteger(int v){value=v;}')
   body=body.replace('struct Common {','struct Common {\n    int GetPresentationTime()const{return 100;}')
   body='#include <functional>\n#include <deque>\n'+body+'\n#include "src/sys/sdl3/NativeInputDriver.h"\n#define CHECK(...) Check((__VA_ARGS__),#__VA_ARGS__)\n'
   body+='static long failAfter=-1;static bool noAllocation=false;\nnamespace native_model {\nusing namespace openq4;using namespace openq4::ui;\nstatic bool Same(const TextEditState&a,const TextEditState&b){return a.text==b.text&&a.anchor==b.anchor&&a.caret==b.caret;}\n'+fixture+'}\n'
   body+='\nusing namespace openq4;\nstatic idCVar com_asyncInput;\nint idEventLoop::JournalLevel()const{return com_journal.GetInteger();}\n'
   body+=function_body(sys,'sysEventTransfer_t Sys_PeekEventDispositionTag(')+function_body(loop,'sysEventTransfer_t idEventLoop::PeekEventDispositionTag(')
   body+=read('tools/tests/native/NativeInputDriverSupport.inc')
   body+=function_body(read('src/framework/KeyInput.cpp'),'static bool Key_IsValidKeyNum(')+'\n'
   body+=function_body(read('src/framework/KeyInput.cpp'),'void idKeyInput::PreliminaryKeyEvent(')+'\n'
   body+=function_body(read('src/framework/Session.cpp'),'bool idSessionLocal::ProcessEvent(')+'\n'
   body+=function_body(read('src/framework/Session_menu.cpp'),'void idSessionLocal::MenuEvent(')+'\n'
   for name in ['void idEventLoop::ProcessEvent(', 'void idEventLoop::ContinueNativeInput(', 'int idEventLoop::RunEventLoop(']:body+=function_body(loop,name)+'\n'
   user=read('src/framework/UsercmdGen.cpp')
   for name in ['bool idUsercmdGenLocal::Inhibited(', 'void idUsercmdGenLocal::Key(', 'void idUsercmdGenLocal::MouseEvent(', 'void idUsercmdGenLocal::Mouse(', 'void idUsercmdGenLocal::Keyboard(', 'bool idUsercmdGenLocal::NativeKeyBlocked(', 'void idUsercmdGenLocal::NativeInputSource(', 'void idUsercmdGenLocal::NativeInputChanged(', 'void Usercmd_NativeInputChanged(', 'void Usercmd_NativeInputSource(', 'usercmd_t idUsercmdGenLocal::GetDirectUsercmd(']:body+=function_body(user,name)+'\n'
   body+=read('src/sys/sdl3/NativeInputTransfers.cpp').replace('#include "../../idlib/precompiled.h"\n','')
   body+=read('src/sys/sdl3/NativeInputDriver.cpp').replace('#include "../../idlib/precompiled.h"\n','')
   body+='\n#include "tools/tests/native/NativeInputDriverTest.cpp"\n'
   source=out/(platform+'.cpp');source.write_text(body,newline='\n');o=obj(source,platform);exe=out/(platform+'.exe');r=run([a.compiler]+flags+[str(x) for x in objects+[o]]+(['/Fe:'+str(exe)] if msvc else ['-o',str(exe)]),platform+'-link')
   if r.returncode:raise RuntimeError(r.stdout+r.stderr)
   r=run([str(exe)],platform+'-run');report['cases'].append({'platform':platform,'exit':r.returncode,'source':sha(source),'binary':sha(exe),'output':r.stdout+r.stderr})
   if r.returncode:raise RuntimeError(r.stdout+r.stderr)
   print(platform+': '+r.stdout,flush=True)
   if a.mutations and platform=='windows':
    mutations=[
     ('async-refusal','com_asyncInput.GetBool() ||','false ||',1),
     ('lost-original-fault','if(!route.MatchesOriginalBinding(routeId,original))return Fail();','if(!route.MatchesOriginalBinding(routeId,original))return false;',1),
     ('incomplete-checkpoint','if(installed && !detached && !fault)(void)Current();','',1),
     ('console-command-owner','if (!NativeInput_SessionCurrent()) return;\n\t\tcmdSystem->BufferCommandText','cmdSystem->BufferCommandText',1),
     ('escape-console-owner','console->Close();\n\t\tif (!NativeInput_SessionCurrent()) return true;','console->Close();',1),
     ('escape-game-owner','op = game->HandleESC( &gui );\n\t\t\tif (!NativeInput_SessionCurrent()) return true;','op = game->HandleESC( &gui );',1),
     ('taken-reentry','if(inFlight){Fail();return nativeInputSessionResult_t::Stop;}','if(inFlight){return nativeInputSessionResult_t::Stop;}',1),
     ('session-suffix','deferredOnly || sessionNativeSeen?','deferredOnly?',1),
     ('polled-suffix','if(keyboard?keyboardNativeSeen:mouseNativeSeen)return false;','',1),
     ('frame-budget','work>=WorkPerFrame','false',3),
     ('frame-stale','if(!presentation || presentation<=budgetFrame)return;','if(false)return;',1),
     ('same-frame-prepare','finishedAtFrame>=nativeInputBudgetFrame || inFlight','false || inFlight',1),
     ('source-hold','held.source==source','true',1),
     ('preliminary-release','idKeyInput::PreliminaryKeyEvent(key,false);','(void)key;',1),
     ('preliminary-unknown','!NativeKeyBlocked(key) && NativeInput_HeldSourceCurrent','true && NativeInput_HeldSourceCurrent',1),
     ('preliminary-owner','NativeInput_HeldSourceCurrent(route,window))','true)',1),
     ('replacement-hold','held.route==route','true',1),
     ('console-owner','if (!NativeInput_SessionCurrent()) return true;\n\t\tRetainedUI_FrameInput();','RetainedUI_FrameInput();',1),
     ('menu-owner','menuCommand = guiActive->HandleEvent( event, common->GetPresentationTime() );\n    if (!NativeInput_SessionCurrent()) return;','menuCommand = guiActive->HandleEvent( event, common->GetPresentationTime() );',1),
     ('payload-abort','catch (...) { NativeInput_AbortDelivery(); (void)NativeInput_CompleteSession(); throw; }','catch (...) { (void)NativeInput_CompleteSession(); throw; }',2),
     ('inhibit-session','// Deliberately preserve inhibitCommands, including INHIBIT_SESSION.','inhibitCommands=0;',1),
     ('source-window','event.key.windowID!=original.window.window','false',1),
    ]
    for label,old,new,count in mutations:
     if body.count(old)!=count:raise RuntimeError('mutation anchor mismatch: '+label+' '+str(body.count(old)))
     changed=out/(label+'.cpp');changed.write_text(body.replace(old,new),newline='\n');mut=obj(changed,'mut-'+label);target=out/(label+'.exe')
     linked=run([a.compiler]+flags+[str(x) for x in objects+[mut]]+(['/Fe:'+str(target)] if msvc else ['-o',str(target)]),'mut-'+label+'-link')
     if linked.returncode:raise RuntimeError('mutation failed to link: '+label)
     result=run([str(target)],'mut-'+label+'-run');report['mutations'].append({'name':label,'exit':result.returncode,'output':result.stdout+result.stderr,'source':sha(changed),'binary':sha(target)})
     if not result.returncode or 'FAIL' not in result.stdout+result.stderr:raise RuntimeError('mutation not assertion-rejected: '+label)
     print('rejected '+label,flush=True)
  win=read('src/sys/win32/win_main.cpp')
  fatal=function_body(win,'void Sys_Error(').replace('__fastfail(', 'TestFastFail(')
  pump=function_body(win,'void Sys_PumpEvents(')
  edge=out/'pump.cpp';edge.write_text(read('tools/tests/native/NativeInputPumpTest.cpp').replace('// PRODUCTION_METHODS',fatal+'\n'+pump),newline='\n')
  binary=out/'pump.exe';compiled=run([a.compiler]+flags+[str(edge)]+(['/Fe:'+str(binary)] if msvc else ['-o',str(binary)]),'pump-compile')
  if compiled.returncode:raise RuntimeError(compiled.stdout+compiled.stderr)
  checked=run([str(binary)],'pump-run');report['cases'].append({'platform':'Win32-counted','exit':checked.returncode,'source':sha(edge),'binary':sha(binary),'output':checked.stdout+checked.stderr})
  if checked.returncode:raise RuntimeError(checked.stdout+checked.stderr)
  print(checked.stdout,flush=True)
  if a.mutations:
   for label,old,new in [('fatal-retirement','if (!NativeInput_FatalRetire())','if (false)'),('fatal-no-failfast','TestFastFail(FAST_FAIL_FATAL_APP_EXIT);',';'),('fatal-unbounded-diagnostic','length < 2048','length < 4096'),('raw-pump-held','if (NativeInput_OwnsPump()) return;','if (false) return;')]:
    text=edge.read_text();assert text.count(old)==1,label
    changed=out/(label+'.cpp');changed.write_text(text.replace(old,new),newline='\n');target=out/(label+'.exe')
    compiled=run([a.compiler]+flags+[str(changed)]+(['/Fe:'+str(target)] if msvc else ['-o',str(target)]),label+'-compile')
    if compiled.returncode:raise RuntimeError(compiled.stdout+compiled.stderr)
    result=run([str(target)],label+'-run');report['mutations'].append({'name':label,'exit':result.returncode,'source':sha(changed),'binary':sha(target),'output':result.stdout+result.stderr})
    if not result.returncode or 'FAIL' not in result.stdout+result.stderr:raise RuntimeError('pump mutation not assertion-rejected: '+label)
  # Whole backend syntax is qualified separately; this audit pins the gate
  # ahead of every legacy helper, for BOTH actual SDL PollEvent loops.
  sdlpump=function_body(read('src/sys/sdl3/sdl3_backend.cpp'),'bool Sys_SDL_PumpEvents(')
  if not sdlpump.split('{',1)[1].lstrip().startswith('if (NativeInput_OwnsPump()) return true;') or sdlpump.count('SDL_PollEvent(')!=2:raise RuntimeError('SDL pump gate/loop audit drift')
  report['sdl_pump_sha256']=hashlib.sha256(sdlpump.encode()).hexdigest()
  disabled=out/'disabled.exe';result=run([a.compiler]+flags+(['/std:c++17'] if msvc else ['-std=c++17'])+[str(ROOT/'src/framework/NativeInputDispatchDisabled.cpp'),str(ROOT/'tools/tests/native/NativeInputDisabledTest.cpp')]+(['/Fe:'+str(disabled)] if msvc else ['-o',str(disabled)]),'disabled-compile')
  if result.returncode:raise RuntimeError(result.stdout+result.stderr)
  checked=run([str(disabled)],'disabled-run');report['cases'].append({'platform':'C++17-disabled','exit':checked.returncode,'binary':sha(disabled),'output':checked.stdout+checked.stderr})
  if checked.returncode:raise RuntimeError(checked.stdout+checked.stderr)

  report['sources_unchanged']=report['sources']==bindings()
  if not report['sources_unchanged']:raise RuntimeError('source drift during qualification')
  report['passed']=True
 except Exception as e:report['failure']=str(e);print(e,flush=True)
 report['logs']={p.name:sha(p) for p in out.glob('*.log')};(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(out/'result.json',flush=True);return 0 if report['passed'] else 1
if __name__=='__main__':raise SystemExit(main())
