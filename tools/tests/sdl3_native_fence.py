#!/usr/bin/env python3
"""Production private fence + SDL queue/full Windows pump extraction, no OS input.

Only small translation-unit projections are copied. An optional pinned SDL
archive fallback shares the existing verified source provisioner.
"""
from __future__ import annotations
import argparse,configparser,hashlib,importlib.util,json,os,re,shutil,subprocess,tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
PACKAGE=ROOT/'subprojects/packagefiles/sdl3'
FILES=['src/events/SDL_events.c','src/events/SDL_keyboard.c','src/video/windows/SDL_windowskeyboard.c','src/video/windows/SDL_windowsevents.c','src/video/windows/SDL_windowswindow.c','src/video/windows/SDL_windowsvideo.c']
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
 parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--sdl-source',type=Path);parser.add_argument('--compiler',default='clang++');parser.add_argument('--no-mutations',action='store_true');parser.add_argument('--sdl-config',type=Path,help='Optional existing generated SDL config directory for Windows C17 syntax checks');parser.add_argument('--native-compiler',default='clang-cl');args=parser.parse_args()
 (ROOT/'.tmp').mkdir(exist_ok=True);scratch=Path(tempfile.mkdtemp(prefix='sdl-native-fence-',dir=ROOT/'.tmp'))
 spec=importlib.util.spec_from_file_location('source_helper',ROOT/'tools/tests/sdl3_clipboard_status.py');helper=importlib.util.module_from_spec(spec);spec.loader.exec_module(helper);helper.ROOT=ROOT;helper.FILES=FILES
 wrap=configparser.ConfigParser();wrap.read(ROOT/'subprojects/sdl3.wrap',encoding='utf-8');source,provision,_=helper.provision_source(args.sdl_source,dict(wrap['wrap-file']),scratch)
 env=os.environ.copy();env['TEMP']=env['TMP']=str(scratch)
 def run(command,log):
  result=subprocess.run(command,cwd=scratch,env=env,text=True,encoding='utf-8',errors='replace',stdout=subprocess.PIPE,stderr=subprocess.STDOUT);(scratch/log).write_text(result.stdout,encoding='utf-8');return result
 projection=scratch/'sdl'
 for name in FILES:
  path=projection/name;path.parent.mkdir(parents=True,exist_ok=True)
  # Owned patches use LF; normalize only this decoded projection so Windows
  # checkouts can also be tested by Linux git without autocrlf configuration.
  path.write_text((source/name).read_text(encoding='utf-8'),encoding='utf-8',newline='\n')
 run(['git','-C',str(projection),'init','--quiet'],'git-init.log')
 patches=['text-provenance.patch','dispatch-fence.patch','queue-consumer.patch','queue-generation.patch']
 # Normalize supported applied suffixes in reverse order before replaying the
 # exact owned patch stack. Later patches may change an earlier patch's context.
 for name in reversed(patches):
  patch=PACKAGE/name
  reverse=run(['git','-C',str(projection),'apply','--reverse','--check',str(patch)],name+'-reverse.log')
  if reverse.returncode==0:
   undone=run(['git','-C',str(projection),'apply','--reverse',str(patch)],name+'-normalize.log')
   if undone.returncode:raise RuntimeError(undone.stdout)
 for name in patches:
  patch=PACKAGE/name;check=run(['git','-C',str(projection),'apply','--check',str(patch)],name+'-check.log')
  if check.returncode:raise RuntimeError('source does not match owned patch stack: '+name+'\n'+check.stdout)
  applied=run(['git','-C',str(projection),'apply',str(patch)],name+'-apply.log')
  if applied.returncode:raise RuntimeError(applied.stdout)
 decoded={name:(projection/name).read_text(encoding='utf-8') for name in FILES}
 def write(name,text):(scratch/name).write_text(text,encoding='utf-8',newline='\n')
 public=PACKAGE/'include/SDL3/SDL_openq4_native_fence.h';private=PACKAGE/'src/video/windows/SDL_openq4_native_fence.h';implementation=private.with_suffix('.c')
 s=public.read_text();write('fence-api.inc',s[s.index('typedef enum OQ4_NativeQueueKind'):s.rindex('#ifdef __cplusplus')])
 s=private.read_text();write('fence-internal.inc',s[s.index('bool OQ4_WIN_'):s.rindex('#endif')])
 s=implementation.read_text();write('fence.inc',s[s.index('#define OQ4_FENCE_MAX_EVENTS'):s.rindex('#endif')])
 events_source=decoded[FILES[0]]
 type_start=events_source.index('typedef struct SDL_EventEntry')
 type_end=events_source.index('} SDL_EventQ =',type_start)
 type_end=events_source.index(';',type_end)+1
 write('queue-types.inc',events_source[type_start:type_end])
 state_start=events_source.index('/* openQ4 private queue guard.')
 state_end=events_source.index('static void SDL_CleanupTemporaryMemory',state_start)
 queue_state=events_source[state_start:state_end].rstrip()
 assert queue_state.endswith('#endif');queue_state=queue_state[:-len('#endif')]
 write('queue-state.inc',queue_state)
 signatures=['void SDL_StopEventLoop(void)','bool SDL_StartEventLoop(void)','static int SDL_AddEvent(SDL_Event *event)','static void SDL_CutEvent(SDL_EventEntry *entry)','int OQ4_WindowsNativeFencePoll(SDL_Event *event, OQ4_NativeQueueRecord *record)','static int SDL_PeepEventsInternal(SDL_Event *events, int numevents, SDL_EventAction action,','void SDL_FlushEvents(Uint32 minType, Uint32 maxType)','void SDL_SetEventFilter(SDL_EventFilter filter, void *userdata)','void SDL_FilterEvents(SDL_EventFilter filter, void *userdata)']
 write('queue-methods.inc','\n'.join(helper.body(events_source,signature) for signature in signatures))
 write('admission.inc',helper.body(decoded[FILES[0]],'bool SDL_PushEvent(SDL_Event *event)'))
 write('wait.inc',helper.body(decoded[FILES[0]],'static int SDL_WaitEventTimeout_Device(SDL_VideoDevice *_this, SDL_Window *wakeup_window, SDL_Event *event, Uint64 start, Sint64 timeoutNS)'))
 assert decoded[FILES[0]].count('if (OQ4_WIN_NativeFenceBlocked()) return false;')==2
 windows=decoded[FILES[3]]
 write('window-scope.inc',helper.body(windows,'LRESULT CALLBACK WIN_WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)'))
 write('pump.inc','\n'.join(helper.body(windows,signature) for signature in ['int WIN_WaitEventTimeout(SDL_VideoDevice *_this, Sint64 timeoutNS)','void WIN_PumpEventsForHWND(SDL_VideoDevice *_this, HWND hwnd)','static void WIN_PumpEventsInternal(SDL_VideoDevice *_this, bool one_collection, bool *removed_message)','void WIN_PumpEvents(SDL_VideoDevice *_this)']))
 # Lifecycle call sites retain their complete legacy body after the new first statement.
 for name,signature,hook in [(FILES[4],'bool WIN_CreateWindow(SDL_VideoDevice *_this, SDL_Window *window, SDL_PropertiesID create_props)','OQ4_WIN_NativeFenceLifecycle();'),(FILES[4],'void WIN_DestroyWindow(SDL_VideoDevice *_this, SDL_Window *window)','OQ4_WIN_NativeFenceLifecycle();'),(FILES[5],'void WIN_VideoQuit(SDL_VideoDevice *_this)\n{','OQ4_WIN_NativeFenceShutdown();')]:
  body=helper.body(decoded[name],signature);assert body.index(hook)<body.index('#endif')
  cleaned=body.replace('\n#if !defined(SDL_PLATFORM_XBOXONE) && !defined(SDL_PLATFORM_XBOXSERIES)\n    '+hook+'\n#endif','',1)
  original=helper.body((source/name).read_text(encoding='utf-8'),signature)
  original=original.replace('\n#if !defined(SDL_PLATFORM_XBOXONE) && !defined(SDL_PLATFORM_XBOXSERIES)\n    '+hook+'\n#endif','',1)
  assert cleaned==original
 assert 'if (OQ4_WIN_NativeFenceBlocked() && !OQ4_WIN_CurrentNativeDispatch()) return false;\n    if (PeekMessage(&next_msg' in windows
 syntax=[]
 if args.sdl_config:
  if os.name!='nt':raise RuntimeError('actual Windows header syntax mode requires a Windows toolchain')
  shutil.copyfile(private,projection/'src/video/windows'/private.name)
  includes=[PACKAGE/'include',private.parent,args.sdl_config,source,source/'include',source/'src',source/'src/video/windows',source/'src/events']
  native_paths=[implementation]+[projection/n for n in [FILES[0],FILES[3],FILES[4],FILES[5]]]
  for path in native_paths:
   native_command=[args.native_compiler,'/nologo','/TC','/std:c17','/Zs','/W3','/WX','/showIncludes','/DUSING_GENERATED_CONFIG_H','/DSDL_BUILD_MAJOR_VERSION=3','/DSDL_BUILD_MINOR_VERSION=4','/DSDL_BUILD_MICRO_VERSION=10']+['/I'+str(p) for p in includes]+[str(path)]
   native_result=run(native_command,path.name+'-syntax.log')
   if native_result.returncode:raise RuntimeError(native_result.stdout)
   headers={str(Path(line.split('including file:',1)[1].strip())) for line in native_result.stdout.splitlines() if 'including file:' in line}
   syntax.append({'command':native_command,'source_sha256':sha(path),'exit_code':0,'headers':{name:sha(Path(name)) for name in sorted(headers)}})
 support=ROOT/'tools/tests/native/SdlNativeFenceTest.cpp'
 command=[args.compiler,'-std=c++20','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-unused-function','-Wno-missing-braces','-Wno-unknown-pragmas','-I',str(scratch),str(support),'-o',str(scratch/'test.exe')]
 c=run(command,'compile.log')
 if c.returncode:raise RuntimeError(c.stdout)
 r=run([str(scratch/'test.exe')],'run.log')
 if r.returncode:raise RuntimeError(r.stdout)
 mutations={
 'allow-second-native-message':('pump.inc','(!one_collection || !peeked)','(!one_collection || !peeked || removed_message)'),
 'omit-held-pump-guard':('pump.inc','    if (OQ4_WIN_NativeFenceBlocked()) return;','    if (false) return;'),
 'omit-held-wait-guard':('pump.inc','    if (OQ4_WIN_NativeFenceBlocked()) return 0;','    if (false) return 0;'),
 'drop-lifecycle-retirement':('pump.inc','    OQ4_WIN_NativeFenceLifecycle();','    (void)hwnd;'),
 'pending-authorizes-ack':('fence.inc','!oq4_fence_published || !oq4_fence_copied ||','!oq4_fence_published ||'),
 'accept-filtered-group':('fence.inc','if (!accepted || !event)','if (!event)'),
 'skip-beforequeue-integrity':('admission.inc','!OQ4_WIN_ValidateFenceAdmission(event, oq4_fence) ||','(oq4_fence && false) ||'),
 'ignore-unsolicited-native-callback':('fence.inc','if (!oq4_fence_collecting || (!oq4_fence_healthy || !OQ4_WIN_QueueHealthy())) { OQ4_FenceFault(); return; }','if (!oq4_fence_collecting || (!oq4_fence_healthy || !OQ4_WIN_QueueHealthy())) return;'),
 'held-periodic-wait-spins':('wait.inc','if (OQ4_WIN_NativeFenceBlocked()) return 0;','if (false) return 0;'),
 'remove-event-budget':('queue-state.inc','oq4_queue_count == 4096','false'),
 'reuse-retired-dispatch':('fence.inc','    SDL_zero(oq4_fence_pending);\n    return true;','    SDL_zero(oq4_fence_pending);\n    oq4_fence_dispatch = 0;\n    return true;'),
 'foreign-removal-accepted':('queue-state.inc','if (!authorized || entry->oq4_record.generation != oq4_queue_generation)','if ((authorized && false) || entry->oq4_record.generation != oq4_queue_generation)'),
 'disabled-error-overwritten':('queue-state.inc','if (oq4_queue_active) {\n        SDL_SetAtomicInt(&oq4_queue_health, 0);','if (true) {\n        SDL_SetAtomicInt(&oq4_queue_health, 0);'),
 'queued-filter-mutation-accepted':('queue-state.inc','SDL_memcmp(&before, &entry->event, sizeof(before))','false'),
 'queue-generation-reused':('queue-state.inc','++oq4_queue_generation;','oq4_queue_generation = 1;'),
 'forged-marker-copy-authorized':('fence.inc','!OQ4_WIN_QueueVerified(oq4_fence_pending.dispatch, oq4_fence_pending.sequence)','false'),
 'direct-peep-marker-admitted':('queue-state.inc','if (out->fence < 0) { OQ4_QueueFault(); return false; }','if (out->fence < 0) { out->fence = 0; }'),
 'wrong-group-ordinal-consumed':('queue-methods.inc','receipt.ordinal != oq4_queue_consumed + 1','false'),
 'wrong-fence-sequence-consumed':('queue-methods.inc','receipt.fence_sequence != oq4_queue_fence_sequence','false'),
 'fence-before-complete-group':('queue-methods.inc','oq4_queue_consumed != oq4_queue_count','false'),
 'stop-keeps-lease':('queue-methods.inc','    OQ4_QueueRetireLocked();','    (void)entry;'),
 'reentrant-poll-allowed':('queue-methods.inc','|| oq4_queue_polling) goto done;','|| false) goto done;'),
 'postcut-fault-publishes-output':('queue-methods.inc','if (!oq4_queue_active || !OQ4_WIN_QueueHealthy()) goto done;','if (!oq4_queue_active) goto done;'),
 'queue-counter-wrap-accepted':('queue-state.inc','oq4_queue_sequence < SDL_MAX_UINT64 &&',''),
 'forget-abnormal-scope-cleanup':('window-scope.inc','        OQ4_WIN_RestoreTextScope(previous);','        (void)previous;'),
 'forget-abnormal-pump-cleanup':('pump.inc','else OQ4_WIN_AbortNativeCollection();','else (void)completed;'),
 }
 outcomes={}
 skipped={}
 if "SEH cleanup enabled" not in r.stdout:
  for name in ["forget-abnormal-scope-cleanup","forget-abnormal-pump-cleanup"]:
   del mutations[name];skipped[name]="Compiler has no MSVC SEH path; no unwind qualification."
 if not args.no_mutations:
  for name,(file,old,new) in mutations.items():
   p=scratch/file;before=p.read_text();assert old in before,(name,old)
   p.write_text(before.replace(old,new,1),newline='\n')
   try:
    mutant_command=command[:-1]+[str(scratch/'mutant.exe')]
    c=run(mutant_command,name+'-compile.log')
    if c.returncode:raise RuntimeError('mutation did not compile '+name+'\n'+c.stdout)
    failed=run([str(scratch/'mutant.exe')],name+'-run.log')
    if not failed.returncode:raise RuntimeError('mutation survived '+name)
    outcomes[name]={'compiled':True,'rejected':True,'exit_code':failed.returncode}
   finally:p.write_text(before,newline='\n')
 paths=[public,private,implementation,PACKAGE/'dispatch-fence.patch',PACKAGE/'queue-consumer.patch',PACKAGE/'text-provenance.patch',support,Path(__file__),ROOT/'tools/tests/sdl3_clipboard_status.py',ROOT/'subprojects/sdl3.wrap']
 result={'status':'passed','native_syntax':syntax,'checks':int(re.search(r'PASS (\d+) checks',r.stdout)[1]),'mutations':outcomes,'skipped_mutations':skipped,'test_binary_sha256':sha(scratch/'test.exe'),'logs':{p.name:sha(p) for p in sorted(scratch.glob('*.log'))},'sources':{str(p.relative_to(ROOT)):sha(p) for p in paths},'projection':{n:sha(projection/n) for n in FILES},'source_input':{n:sha(source/n) for n in FILES},'provision':provision,'command':command,'scope':'Actual native fence, SDL queue admission, complete pump/internal/lifecycle drain/wait/window wrapper with counted Windows/SDL doubles. No real message queue, native TSF/IME, engine owner bridge or frame qualification. MSVC abnormal unwind tested with synthetic exception only.'}
 (scratch/'result.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8');print(r.stdout.strip());print(scratch/'result.json');return 0
if __name__=='__main__':raise SystemExit(main())
