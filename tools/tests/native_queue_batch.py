#!/usr/bin/env python3
"""Actual owned native ingress/codec/source with pinned SDL headers; no OS input."""
from pathlib import Path
import argparse, configparser, hashlib, importlib.util, json, os, re, subprocess, tempfile
ROOT=Path(__file__).resolve().parents[2]
PACKAGE=ROOT/'subprojects/packagefiles/sdl3'
SDL_FILES=['src/events/SDL_events.c','src/events/SDL_keyboard.c',
           'src/video/windows/SDL_windowskeyboard.c','src/video/windows/SDL_windowsevents.c',
           'src/video/windows/SDL_windowswindow.c','src/video/windows/SDL_windowsvideo.c']
# Exact public include closure for SDL_events.h and SDL_init.h in pinned 3.4.10.
# The shared archive verifier extracts only these regular, bounded members.
SDL_HEADERS=('SDL_atomic.h SDL_audio.h SDL_begin_code.h SDL_blendmode.h SDL_camera.h '
    'SDL_close_code.h SDL_endian.h SDL_error.h SDL_events.h SDL_gamepad.h SDL_guid.h '
    'SDL_init.h SDL_iostream.h SDL_joystick.h SDL_keyboard.h SDL_keycode.h SDL_mouse.h '
    'SDL_mutex.h SDL_pen.h SDL_pixels.h SDL_platform_defines.h SDL_power.h SDL_properties.h '
    'SDL_rect.h SDL_scancode.h SDL_sensor.h SDL_stdinc.h SDL_surface.h SDL_thread.h '
    'SDL_touch.h SDL_video.h').split()
PATCHES=['text-provenance.patch','dispatch-fence.patch','queue-consumer.patch','queue-generation.patch']
def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()
def body(text,signature):
    assert text.count(signature)==1
    start=text.index(signature); opening=text.index('{',start); depth=0
    tokens=re.compile(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[{}]',re.S)
    for token in tokens.finditer(text,opening):
        if token.group()=='{': depth+=1
        elif token.group()=='}':
            depth-=1
            if not depth:return text[start:token.end()]
    raise AssertionError('Unterminated function')
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdl-source',type=Path,help='Existing SDL tree; an explicit missing tree is an error')
    parser.add_argument('--compiler',default='clang++');parser.add_argument('--no-mutations',action='store_true')
    parser.add_argument('--sanitizers',action='store_true',help='Address/undefined sanitizers for the portable source/double executable')
    args=parser.parse_args();(ROOT/'.tmp').mkdir(exist_ok=True)
    out=Path(tempfile.mkdtemp(prefix='native-queue-batch-',dir=ROOT/'.tmp'))
    env=os.environ.copy();env['TEMP']=env['TMP']=str(out)
    helper_path=ROOT/'tools/tests/sdl3_clipboard_status.py'
    spec=importlib.util.spec_from_file_location('sdl_source_helper',helper_path)
    assert spec and spec.loader
    helper=importlib.util.module_from_spec(spec);spec.loader.exec_module(helper)
    helper.ROOT=ROOT;helper.FILES=SDL_FILES+['include/SDL3/'+name for name in SDL_HEADERS]
    wrap_path=ROOT/'subprojects/sdl3.wrap'
    wrap=configparser.ConfigParser();wrap.read(wrap_path,encoding='utf-8')
    specification=dict(wrap['wrap-file'])
    assert specification['directory']=='SDL3-3.4.10','Review SDL header whitelist before updating the pinned release'
    assert all('sdl3/'+name in specification.get('diff_files','').split(', ') for name in PATCHES),'Required SDL patch stack is missing'
    sdl_source,provision,_=helper.provision_source(args.sdl_source,specification,out)
    engine=ROOT/'src/sys/sdl3/sdl3_backend.cpp'
    pump=body(engine.read_text(encoding='utf-8'),'bool Sys_SDL_PumpEvents(void)')
    handled=set(re.findall(r'case\s+(SDL_EVENT_[A-Z0-9_]+)\s*:',pump))
    ingress=(ROOT/'src/sys/sdl3/NativeQueueBatch.cpp').read_text()
    ignored=ingress.split('// Audited current engine switch ignores these families.',1)[1].split('default:',1)[0]
    ignored=set(re.findall(r'case\s+(SDL_EVENT_[A-Z0-9_]+)\s*:',ignored))
    assert ignored and not ignored.intersection(handled),'Previously ignored SDL family gained engine handling; review ingress policy'
    def run(cmd,name):
        r=subprocess.run(cmd,cwd=out,env=env,capture_output=True,text=True,encoding='utf-8',errors='replace')
        (out/name).write_text(r.stdout+r.stderr,encoding='utf-8');return r
    projection_root=out/'sdl'
    for name in SDL_FILES:
        target=projection_root/name;target.parent.mkdir(parents=True,exist_ok=True)
        target.write_text((sdl_source/name).read_text(encoding='utf-8'),encoding='utf-8',newline='\n')
    r=run(['git','-C',str(projection_root),'init','--quiet'],'git-init.log');assert not r.returncode,r.stderr
    # Later owned patches change earlier context: peel any applied suffix in
    # reverse order, then replay the complete reviewed stack on the projection.
    # Fresh archives, intermediate prefixes and fully patched trees are valid;
    # unrelated/malformed context fails instead of guessing an extraction.
    for name in reversed(PATCHES):
        patch=PACKAGE/name
        reverse=run(['git','-C',str(projection_root),'apply','--reverse','--check',str(patch)],name+'-reverse.log')
        if not reverse.returncode:
            r=run(['git','-C',str(projection_root),'apply','--reverse',str(patch)],name+'-normalize.log')
            assert not r.returncode,r.stderr
    for name in PATCHES:
        patch=PACKAGE/name
        check=run(['git','-C',str(projection_root),'apply','--check',str(patch)],name+'-check.log')
        assert not check.returncode,(name,check.stderr)
        r=run(['git','-C',str(projection_root),'apply',str(patch)],name+'-apply.log');assert not r.returncode,r.stderr
    # git apply may materialize checkout-configured CRLF. Keep the final
    # source projection byte-identical on Windows and Linux as well.
    for name in SDL_FILES:
        path=projection_root/name
        path.write_text(path.read_text(encoding='utf-8'),encoding='utf-8',newline='\n')
    projection=projection_root/SDL_FILES[0]
    patch=PACKAGE/'queue-generation.patch'
    generation=out/'queue-generation.inc'
    generation.write_text(body(projection.read_text(),'Uint64 OQ4_WindowsNativeFenceQueueGeneration(void)'),newline='\n')
    names=['src/sys/sdl3/NativeQueueBatch.h','src/sys/sdl3/NativeQueueBatch.cpp','src/sys/sdl3/NativeQueueSource.h','src/sys/sdl3/NativeQueueSource.cpp',
           'src/ui/retained/TextInput.h','src/ui/retained/TextInput.cpp','tools/tests/native/NativeQueueBatchTest.cpp']
    for n in names:
        p=out/n;p.parent.mkdir(parents=True,exist_ok=True);p.write_text((ROOT/n).read_text(),newline='\n')
    flags=[args.compiler,'-std=c++20','-Wall','-Wextra','-Werror','-DSDL_STATIC_LIB',
           '-I'+str(out),'-I'+str(ROOT/'subprojects/packagefiles/sdl3/include'),'-I'+str(sdl_source/'include')]
    if os.name!='nt':flags+=['-pthread']
    if args.sanitizers:flags+=['-fsanitize=address,undefined','-fno-omit-frame-pointer','-g0']
    files=[str(out/n) for n in names if n.endswith('.cpp')]
    command=flags+['-DUSE_SDL3','-DOPENQ4_SDL3_CHECKED_NATIVE_QUEUE=1']+files+['-o',str(out/'test.exe')]
    r=run(command,'compile.log');assert not r.returncode,r.stdout+r.stderr
    r=run([str(out/'test.exe')],'run.log');assert not r.returncode,r.stdout+r.stderr
    checks=int(re.search(r'PASS (\d+) checks',r.stdout)[1])
    # Unsupported providers make no SDL or engine token calls: no symbols are supplied.
    stub=out/'unsupported.cpp'
    stub.write_text('#include "src/sys/sdl3/NativeQueueSource.h"\n#include <cassert>\nint main(){openq4::SdlNativeQueueSource s(1);openq4::NativeQueueStatus v;std::string e;SDL_Event event{};OQ4_NativeQueueRecord r{};OQ4_NativeFence f{};assert(!s.Observe(v,e));assert(s.Poll(event,r)==-1);assert(!s.CopyFence(event,f));}\n')
    for label,defines in [('absent',[]),('zero',['-DUSE_SDL3','-DOPENQ4_SDL3_CHECKED_NATIVE_QUEUE=0'])]:
        r=run(flags+defines+[str(stub),str(out/'src/sys/sdl3/NativeQueueSource.cpp'),'-o',str(out/(label+'.exe'))],label+'-compile.log');assert not r.returncode,r.stderr
        r=run([str(out/(label+'.exe'))],label+'-run.log');assert not r.returncode,r.stderr
    mutations={
        'skip-generation':('src/sys/sdl3/NativeQueueBatch.cpp','record.generation!=status.generation ||','false ||'),
        'skip-ordinal':('src/sys/sdl3/NativeQueueBatch.cpp','record.ordinal!=ordinal+1 ||','false ||'),
        'skip-fence-count':('src/sys/sdl3/NativeQueueBatch.cpp','record.ordinal!=status.pending->event_count ||','false ||'),
        'skip-fence-copy':('src/sys/sdl3/NativeQueueBatch.cpp','!source.CopyFence(event,proof) || !SameFence(proof,status.pending) ||','(proof=*status.pending,false) ||'),
        'skip-engine-token':('src/sys/sdl3/NativeQueueBatch.cpp','a.engineToken==b.engineToken &&','true &&'),
        'retain-text-pointer':('src/sys/sdl3/NativeQueueBatch.cpp','out.header.text.text=nullptr;','/* removed */'),
        'lose-owned-text':('src/sys/sdl3/NativeQueueBatch.cpp','out.assign(view);','out.clear();'),
        'allow-generation-reuse':('src/sys/sdl3/NativeQueueBatch.cpp','requireFreshGeneration && status.generation<=lastGeneration','false && status.generation<=lastGeneration'),
        'silent-provider-replacement':('src/sys/sdl3/NativeQueueBatch.cpp','(lastEpoch && !requireFreshGeneration && (status.providerEpoch!=lastEpoch || status.generation!=lastGeneration)) ||','false ||'),
        'reset-live-provider':('src/sys/sdl3/NativeQueueBatch.cpp','status.healthy || status.generation || status.pending','false'),
        'reuse-published-slot':('src/sys/sdl3/NativeQueueBatch.cpp','if(phase==Phase::Published) { error=','if(false) { error='),
        'allow-cross-instance':('src/sys/sdl3/NativeQueueBatch.cpp','identity=value;','identity=value ? 1 : 0;'),
        'skip-readiness-generation':('queue-generation.inc','SDL_EventQ.active && oq4_queue_active && OQ4_WIN_QueueHealthy()','OQ4_WIN_QueueHealthy()'),
        'skip-main-generation':('queue-generation.inc','if (!SDL_IsMainThread()) return 0;','/* removed */'),
        'skip-source-token-race':('src/sys/sdl3/NativeQueueSource.cpp','token!=Sys_EventQueueToken() ||','(Sys_EventQueueToken(),false) ||'),
    }
    results={}
    if not args.no_mutations:
        for label,(name,old,new) in mutations.items():
            p=out/name;before=p.read_text();assert before.count(old)==1,(label,before.count(old))
            p.write_text(before.replace(old,new),newline='\n')
            try:
                r=run(command[:-1]+[str(out/'mutant.exe')],label+'-compile.log');assert not r.returncode,(label,r.stderr)
                r=run([str(out/'mutant.exe')],label+'-run.log');assert r.returncode,(label,'survived')
                results[label]={'compiled':True,'rejected':True,'exit_code':r.returncode}
            finally:p.write_text(before,newline='\n')
    record={'status':'passed','checks':checks,'mutations':results,'unsupported_no_symbols':['absent','zero'],
            'engine_switch':{'path':str(engine),'source_sha256':sha(engine),'pump_sha256':hashlib.sha256(pump.encode()).hexdigest(),'audited_ignored':sorted(ignored)},
            'sources':{n:sha(ROOT/n) for n in names},'runner_sha256':sha(Path(__file__)),
            'source_provision':provision,'provisioner_sha256':sha(helper_path),'wrap_sha256':sha(wrap_path),
            'patches':{name:sha(PACKAGE/name) for name in PATCHES},
            'sdl_inputs':{name:sha(sdl_source/name) for name in [*SDL_FILES,'LICENSE.txt']},
            'sdl_projection':{name:sha(projection_root/name) for name in SDL_FILES},
            'generation_patch_sha256':sha(patch),'generation_projection_sha256':sha(projection),
            'sdk_headers':{name:sha(sdl_source/'include/SDL3'/name) for name in SDL_HEADERS},
            'test_binary_sha256':sha(out/'test.exe'),'logs':{p.name:sha(p) for p in sorted(out.glob('*.log'))},
            'command':command,'limits':'Actual ingress, UTF-8 codec, SDL source adapter and generation query execute with actual pinned public SDL types. Poll/Copy/provider health/continuity are counted source doubles; no actual borrowed allocator lifecycle or OS/native input qualification.'}
    (out/'result.json').write_text(json.dumps(record,indent=2)+'\n');print(out/'result.json')
if __name__=='__main__':main()
