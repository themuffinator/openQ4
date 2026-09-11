#!/usr/bin/env python3
"""Actual portable native collection coordinator and models; no SDL pump, COM activation or GUI.

Compiles the complete Interaction, NativeTextEditor, NativeTextDocument and text
models. Only Document's existing UTF-8/StateValue validation bodies are extracted
to avoid an unrelated JSON link. No fake native transaction/undo implementation.
"""
from pathlib import Path
import argparse,hashlib,json,os,re,subprocess,tempfile,configparser,importlib.util
from filesystem_case_segments import function_body
ROOT=Path(__file__).resolve().parents[2]
CORE=ROOT/'src/ui/retained'
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler',default='clang++')
    parser.add_argument('--sdl-source',type=Path)
    parser.add_argument('--msvc',action='store_true')
    parser.add_argument('--debug-crt',action='store_true')
    parser.add_argument('--sanitize',action='store_true')
    parser.add_argument('--no-mutations',action='store_true')
    args=parser.parse_args();(ROOT/'.tmp').mkdir(exist_ok=True)
    out=Path(tempfile.mkdtemp(prefix='native-collection-',dir=ROOT/'.tmp'))
    env={**os.environ,'TEMP':str(out),'TMP':str(out),'TMPDIR':str(out)}
    names=['Interaction.h','Interaction.cpp','ScrollGeometry.h','ScrollGeometry.cpp','Document.h','Document.cpp','Vector.h','TextInput.h','TextInput.cpp',
           'TextEdit.h','TextEdit.cpp','TextEditCommand.h','NativeTextDocument.h','NativeTextDocument.cpp',
           'NativeTextEditor.h','NativeTextEditor.cpp','TextInputBroker.h','Input.h','Input.cpp']
    tests=['UiNativeCollectionCoordinatorTest.cpp']
    paths=[CORE/n for n in names]+[ROOT/'tools/tests/native'/n for n in tests]+[Path(__file__),ROOT/'tools/tests/filesystem_case_segments.py']
    extra=[ROOT/'src/ui/application/NativeTextCollectionCoordinator.h',ROOT/'src/ui/application/NativeTextCollectionCoordinator.cpp',ROOT/'src/sys/sdl3/NativeQueueBatch.h',ROOT/'src/sys/sdl3/NativeQueueBatch.cpp',ROOT/'subprojects/packagefiles/sdl3/include/SDL3/SDL_openq4_native_fence.h']
    paths+=extra
    helper_path=ROOT/'tools/tests/sdl3_clipboard_status.py'
    spec=importlib.util.spec_from_file_location('sdl_source_helper',helper_path)
    helper=importlib.util.module_from_spec(spec);spec.loader.exec_module(helper)
    helper.ROOT=ROOT;helper.FILES=['include/SDL3/SDL_atomic.h', 'include/SDL3/SDL_audio.h', 'include/SDL3/SDL_begin_code.h', 'include/SDL3/SDL_blendmode.h', 'include/SDL3/SDL_camera.h', 'include/SDL3/SDL_close_code.h', 'include/SDL3/SDL_endian.h', 'include/SDL3/SDL_error.h', 'include/SDL3/SDL_events.h', 'include/SDL3/SDL_gamepad.h', 'include/SDL3/SDL_guid.h', 'include/SDL3/SDL_init.h', 'include/SDL3/SDL_iostream.h', 'include/SDL3/SDL_joystick.h', 'include/SDL3/SDL_keyboard.h', 'include/SDL3/SDL_keycode.h', 'include/SDL3/SDL_mouse.h', 'include/SDL3/SDL_mutex.h', 'include/SDL3/SDL_pen.h', 'include/SDL3/SDL_pixels.h', 'include/SDL3/SDL_platform_defines.h', 'include/SDL3/SDL_power.h', 'include/SDL3/SDL_properties.h', 'include/SDL3/SDL_rect.h', 'include/SDL3/SDL_scancode.h', 'include/SDL3/SDL_sensor.h', 'include/SDL3/SDL_stdinc.h', 'include/SDL3/SDL_surface.h', 'include/SDL3/SDL_thread.h', 'include/SDL3/SDL_touch.h', 'include/SDL3/SDL_video.h']
    wrap=configparser.ConfigParser();wrap.read(ROOT/'subprojects/sdl3.wrap',encoding='utf-8')
    assert wrap['wrap-file']['directory']=='SDL3-3.4.10','Review SDL include closure for a new version'
    sdl_source,provision,_=helper.provision_source(args.sdl_source,dict(wrap['wrap-file']),out)
    sdl=sdl_source/'include'
    paths += [helper_path,ROOT/'subprojects/sdl3.wrap']
    sdl_paths=[sdl_source/name for name in helper.FILES]+[sdl_source/'LICENSE.txt']
    sdl_before={str(p.relative_to(sdl_source)):sha(p) for p in sdl_paths}
    before={str(p.relative_to(ROOT)):sha(p) for p in paths}
    doc=(CORE/'Document.cpp').read_text(encoding='utf-8')
    valid=out/'valid.cpp';valid.write_text('#include "Interaction.h"\n#include <cmath>\nnamespace openq4::ui {\n'+
        function_body(doc,'bool Utf8(')+function_body(doc,'bool ValidStateValue(')+'}\n',encoding='utf-8',newline='\n')
    working=out/'Interaction.cpp';source=(CORE/'Interaction.cpp').read_text(encoding='utf-8');working.write_text(source,encoding='utf-8',newline='\n')
    flags=[args.compiler,'-std=c++20','-Wall','-Wextra','-Werror','-Wno-missing-field-initializers','-I',str(ROOT),'-I',str(CORE)]
    flags+=['-I',str(ROOT/'subprojects/packagefiles/sdl3/include'),'-I',str(sdl)]
    if args.msvc:
        includes=[str(ROOT),str(CORE),str(ROOT/'src/ui/application'),str(ROOT/'subprojects/packagefiles/sdl3/include'),str(sdl)]
        flags=[args.compiler,'/nologo','/std:c++20','/EHsc','/W4','/WX','/wd4458','/MTd' if args.debug_crt else '/MT']+['/I'+n for n in includes]
    if args.sanitize:flags+=['-fsanitize=address,undefined','-fno-omit-frame-pointer','-g0','-no-pie']
    core=[str(working),str(valid)]+[str(CORE/n) for n in ['ScrollGeometry.cpp','TextInput.cpp','TextEdit.cpp','NativeTextDocument.cpp','NativeTextEditor.cpp','Input.cpp']]
    controller=out/'NativeTextCollectionCoordinator.cpp'
    controller_source=(ROOT/'src/ui/application/NativeTextCollectionCoordinator.cpp').read_text()
    controller.write_text(controller_source,encoding='utf-8',newline='\n')
    core+=[str(ROOT/'src/sys/sdl3/NativeQueueBatch.cpp'),str(controller)]
    if not args.msvc:flags+=['-I',str(ROOT/'src/ui/application')]
    report={'sdl_source':provision,'sdl_headers':sdl_before,'passed':False,'scope':__doc__,'sources':before,'cases':[]}
    def run(cmd,name):
        r=subprocess.run(cmd,env=env,cwd=out,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=180)
        log=out/(name+'.log');log.write_text(r.stdout+r.stderr,encoding='utf-8');return r,log
    def case(name,test,mutant=False):
        binary=out/(name+'.exe');command=flags+core+[str(ROOT/'tools/tests/native'/test),'-o',str(binary)]
        if args.msvc:command=flags+core+[str(ROOT/'tools/tests/native'/test),'/Fe:'+str(binary)]
        r,log=run(command,name+'-compile');entry={'name':name,'command':command,'compile_exit':r.returncode,'compile_log_sha256':sha(log),'interaction_sha256':sha(working),'controller_sha256':sha(controller)};report['cases'].append(entry)
        if r.returncode:raise RuntimeError(r.stdout+r.stderr)
        r,log=run([str(binary)],name+'-run');entry.update(exit=r.returncode,run_log_sha256=sha(log),binary_sha256=sha(binary))
        if mutant:
            if not r.returncode or 'FAIL' not in r.stdout+r.stderr:raise RuntimeError('Mutation survived or did not reach a test assertion: '+name+'\n'+r.stdout+r.stderr)
            entry['rejected']=True
        else:
            if r.returncode:raise RuntimeError(r.stdout+r.stderr)
            entry['output']=r.stdout.strip();print(r.stdout.strip(),flush=True)
    mutations={
        'barrier-query-worker-accepted':('controller','if (std::this_thread::get_id()!=thread) return reject("Native barrier query requires its engine thread");','if (false) return reject("Native barrier query requires its engine thread");'),
        'barrier-query-busy-accepted':('controller','calling || retired || (phase!=Phase::Ready && phase!=Phase::AwaitingFence) ||','false ||'),
        'barrier-query-partial-output':('controller','auto snapshot=current;\n\t\tstatic_assert(std::is_nothrow_move_assignable_v<NativeTextEditorBarrier>);\n\t\tout=std::move(snapshot);error.clear();return true;','out=current;error.clear();return true;'),
        'accept-unclosed-store':('controller','!owner.Current(current) || !store.StillClosed(closed)', '!owner.Current(current) || (void(store),false)'),
        'reentry-no-latch':('controller','if (calling) return Fail(error,"Reentrant native collection reconciliation");','if (calling) return false;'),
        'sync-result-ignored':('controller','!store.Sync(receipt,prepared->Presentation(),error)', '(store.Sync(receipt,prepared->Presentation(),error),false)'),
        'native-ack-ignored':('controller','!store.Acknowledge(receipt,error)', '(store.Acknowledge(receipt,error),false)'),
        'completion-token-ignored':('controller','expected!=completion', '(void(expected),false)'),
        'fence-ack-ignored':('controller','!fence.Acknowledge(*queue,error)', '(fence.Acknowledge(*queue,error),false)'),
        'unclassified-native-participation':('controller','(seal.pending.count && !seal.admittedCallbacks)', 'false'),
        'lifecycle-accepted':('controller','seal.kind!=NativeClosedCollectionKind::Pump', 'false'),
        'lifecycle-receipt-ignored':('controller','requested ? seal!=*requested :', 'requested ? false :'),
        'lifecycle-kind-ignored':('controller','(requested && requested->kind!=NativeClosedCollectionKind::Lifecycle)', 'false'),
        'lifecycle-receipt-forgotten':('controller','return ReconcileKind(completed,ingress,source,batch,owner,store,out,error);', '(void)completed;return ReconcileKind({},ingress,source,batch,owner,store,out,error);'),
        'pending-watermark-ignored':('controller','pending.lastSequence!=closed.pending.lastSequence', 'false'),
        'sync-allocating-owner-copy':('controller','!ingress.Validate(source,completion.batch,error) || phase==Phase::RetireRequired ||\n\t\t\t\t!owner.Current(current) || !store.StillClosed(closed)', '!Check(ingress,source,owner,store,error)'),
        'omit-final-model-swap':('interaction','!nativeModel->Swap(before,*data.model)', 'false'),
        'omit-prepared-history':('interaction','data.editor.buffer.RestoreHistory(view->draft,view->history,view->policy,error)', 'data.editor.buffer.RestoreHistory(view->draft,{},view->policy,error)'),
        'omit-final-editor-publication':('interaction','found->second.number = std::move(data.editor);', '(void)data.editor;'),
    }
    if args.msvc and args.debug_crt:
        del mutations['barrier-query-partial-output']
        report['unsupported_mutations']=['barrier-query-partial-output: debug STL iterator proxy allocation sweep is unsupported']
    try:
        for test in tests:case(test.removesuffix('.cpp'),test)
        if not args.no_mutations:
            for name,(target,old,new) in mutations.items():
                original=source if target=='interaction' else controller_source
                path=working if target=='interaction' else controller
                # The shared final predicate intentionally appears at three independent
                # effect barriers. Mutating all occurrences exercises its entire contract.
                count=original.count(old)
                if count!=1 and not (name=='accept-unclosed-store' and count==4):raise RuntimeError('Mutation anchor count '+str(count)+': '+name)
                path.write_text(original.replace(old,new),encoding='utf-8',newline='\n')
                try:case(name,tests[0],True);print('Rejected '+name,flush=True)
                finally:path.write_text(original,encoding='utf-8',newline='\n')
        report['passed']=True
    except Exception as e:report['failure']=str(e);print(e,flush=True)
    report['sources_unchanged']=before=={str(p.relative_to(ROOT)):sha(p) for p in paths}
    report['sdl_headers_unchanged']=sdl_before=={str(p.relative_to(sdl_source)):sha(p) for p in sdl_paths}
    report['passed'] &= report['sources_unchanged'] and report['sdl_headers_unchanged'];report['validation_extraction_sha256']=sha(valid)
    result=out/'result.json';result.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8');print(result,flush=True)
    return 0 if report['passed'] else 1
if __name__=='__main__':raise SystemExit(main())
