#!/usr/bin/env python3
"""Actual Windows SDK store and native model through counted SDL collection hooks.
No SDL init, HWND/native input, TSF activation, engine build or game is run.
"""
from pathlib import Path
import argparse, hashlib, json, os, re, subprocess, tempfile
ROOT=Path(__file__).resolve().parents[2]
sha=lambda path:hashlib.sha256(path.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--compiler',default='clang++');p.add_argument('--sdl-include',type=Path,required=True)
    p.add_argument('--msvc-debug',action='store_true');p.add_argument('--no-mutations',action='store_true')
    p.add_argument('--unsupported',action='store_true')
    args=p.parse_args()
    args.sdl_include=args.sdl_include.resolve()
    if not (args.sdl_include/'SDL3/SDL_events.h').is_file():
        raise SystemExit('SDL include directory must contain SDL3/SDL_events.h')
    if os.name!='nt':raise SystemExit('Actual Windows SDK required; no Linux native COM claim.')
    folder=Path(tempfile.mkdtemp(prefix='windows-text-collection-bridge-',dir=ROOT/'.tmp'))
    paths=list((ROOT/'src').rglob('*.h'))+list((ROOT/'src').rglob('*.cpp'))+[
        ROOT/'subprojects/packagefiles/sdl3/include/SDL3/SDL_openq4_native_fence.h',
        ROOT/'tools/tests/native/WindowsTextCollectionBridgeTest.cpp',Path(__file__).resolve()]
    before={str(path.relative_to(ROOT)):sha(path) for path in paths}
    source=ROOT/'src/sys/sdl3/WindowsTextCollectionBridge.cpp';working=folder/source.name
    original=source.read_bytes();working.write_bytes(original)
    includes=[ROOT/'src',ROOT/'src/sys/sdl3',ROOT/'subprojects/packagefiles/sdl3/include',args.sdl_include]
    files=[working,ROOT/'src/sys/sdl3/WindowsTextStore.cpp',ROOT/'src/ui/retained/NativeTextDocument.cpp',ROOT/'src/ui/retained/TextInput.cpp',ROOT/'tools/tests/native/WindowsTextCollectionBridgeTest.cpp']
    if args.msvc_debug:
        command=[args.compiler,'/nologo','/std:c++20','/EHsc','/W3','/WX','/MTd','/D_ITERATOR_DEBUG_LEVEL=2','/D_CRT_SECURE_NO_WARNINGS','/DSDL_STATIC_LIB','/DUSE_SDL3','/DOPENQ4_SDL3_CHECKED_NATIVE_QUEUE=1']
        command += ['/I'+str(path) for path in includes]+[str(path) for path in files]+['/Fe:'+str(folder/'test.exe'),'/Fo:'+str(folder)+'\\']
    else:
        command=[args.compiler,'-std=c++20','-Wall','-Wextra','-Werror','-DSDL_STATIC_LIB','-DUSE_SDL3','-DOPENQ4_SDL3_CHECKED_NATIVE_QUEUE=1']
        for path in includes:command+=['-I',str(path)]
        command += [str(path) for path in files]+['-o',str(folder/'test.exe')]
    if args.unsupported:
        command=[arg.replace('OPENQ4_SDL3_CHECKED_NATIVE_QUEUE=1','OPENQ4_SDL3_CHECKED_NATIVE_QUEUE=0') for arg in command]
        if not args.msvc_debug:command+=['-Wno-unused-function']
    env=os.environ.copy();env['TEMP']=env['TMP']=env['TMPDIR']=str(folder)
    def run(command,name):
        r=subprocess.run(command,cwd=folder,env=env,text=True,encoding='utf-8',errors='replace',stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
        (folder/name).write_text(r.stdout,encoding='utf-8');return r
    compiled=run(command,'compile.log')
    if compiled.returncode:raise RuntimeError(compiled.stdout)
    baseline=run([str(folder/'test.exe')],'run.log')
    if baseline.returncode:raise RuntimeError(baseline.stdout)
    baseline_binary=sha(folder/'test.exe')
    outcomes={}
    mutations={
        'overwrite-pending-prepare':('if(busy || attempt || opened){Fault();return false;}','if(busy){Fault();return false;}'),
        'ignore-finish-context':('!input || !attempt || !SameContext(*input,*attempt)','!input || !attempt || (void(SameContext(*input,*attempt)),false)'),
        'release-older-failed-open':('if(!opened){Fault();return false;}','if(!opened){Retire();return false;}'),
        'wrong-provider-generation':('c.generation==generation && c.dispatch && Provider()','c.dispatch && Provider()'),
        'wrong-module-epoch':('epoch!=impl->epoch || !generation','(void(epoch),false) || !generation'),
        'forget-native-activity':('if(result.admittedCallbacks && !OQ4_WindowsNativeFenceMarkActivity(input))','if(false && result.admittedCallbacks && !OQ4_WindowsNativeFenceMarkActivity(input))'),
        'fabricate-empty-activity':('if(result.admittedCallbacks && !OQ4_WindowsNativeFenceMarkActivity(input))','if(!OQ4_WindowsNativeFenceMarkActivity(input))'),
        'ignore-mark-refusal':('if(result.admittedCallbacks && !OQ4_WindowsNativeFenceMarkActivity(input))','if(result.admittedCallbacks && !OQ4_WindowsNativeFenceMarkActivity(input) && false)'),
        'ignore-retired-mark':('if(terminal || !Context(*input) || store->QueryLifecycle','if(false || !Context(*input) || store->QueryLifecycle'),
        'discard-lifecycle-kind':('input->kind==OQ4_COLLECTION_PUMP?WindowsTextCollectionKind::Pump:WindowsTextCollectionKind::Lifecycle','WindowsTextCollectionKind::Pump'),
        'launder-lifecycle-engine-revision':('p.engineRevision!=engine || p.acknowledgedShadowRevision!=shadow','p.engineRevision<engine || p.acknowledgedShadowRevision<shadow'),
        'ignore-fresh-group':('initial.sequence || initial.group ||','initial.sequence ||'),
        'ignore-prior-scope':('!life.healthy || life.lastScopeSerial || life.retainedCompositions','!life.healthy || life.retainedCompositions'),
        'ignore-closed-scope-replacement':('life.lastScopeSerial==closed->collection.serial && life.lastDispatch==closed->collection.dispatch &&',''),
        'ignore-closed-seal-identity':('impl->Seal() && SameSeal(Portable(*impl->closed),expected)','impl->Seal() && (void(SameSeal(Portable(*impl->closed),expected)),true)'),
        'ignore-pending-editor-revision':('b.editor.revision==engine && b.sequence','b.sequence'),
        'ignore-ack-owner':('after.native!=impl->native || !SameOwner(impl->owner,after.editor) || after.editor.revision<impl->engine','after.native!=impl->native || after.editor.revision<impl->engine'),
        'skip-sync-native-check':('if(!impl->Seal(true)){impl->Retire();return Error(error,"Native settlement lost authority after synchronization");}','/* false success after native synchronization */'),
        'skip-exact-retire-id':('impl->Owner() && id==impl->native','impl->Owner() && (void(id),true)'),
        'worker-retires-owner':('impl->Owner() && id==impl->native','id==impl->native'),
        'skip-destructor-retirement':('WindowsTextCollectionBridge::~WindowsTextCollectionBridge(){impl->Retire();}','WindowsTextCollectionBridge::~WindowsTextCollectionBridge(){}'),
        'debug-default-offer-proxy':('NativeTextOffer candidate=out;','NativeTextOffer candidate;'),
    }
    if not args.msvc_debug:del mutations['debug-default-offer-proxy']
    if not args.no_mutations and not args.unsupported:
        for name,(old,new) in mutations.items():
            text=original.decode('utf-8').replace('\r\n','\n');assert old in text,name
            working.write_text(text.replace(old,new,1),encoding='utf-8',newline='\n')
            try:
                mutant_command=[arg.replace('test.exe','mutant.exe') for arg in command]
                r=run(mutant_command,name+'-compile.log')
                if r.returncode:raise RuntimeError('Uncompiled mutation '+name+'\n'+r.stdout)
                r=run([str(folder/'mutant.exe')],name+'-run.log')
                if not r.returncode:raise RuntimeError('Survived mutation '+name)
                outcomes[name]={'compiled':True,'rejected':True,'exit_code':r.returncode}
            finally:working.write_bytes(original)
    assert before=={str(path.relative_to(ROOT)):sha(path) for path in paths},'Source drift during validation'
    result={'status':'passed','checks':int(re.search(r'PASS (\d+) checks',baseline.stdout)[1]),'mutations':outcomes,
        'command':command,'sources':before,'test_binary_sha256':baseline_binary,'logs':{path.name:sha(path) for path in folder.glob('*.log')},
        'scope':'Actual Windows SDK WindowsTextStore/NativeTextDocument plus bridge and counted SDL provider functions. No native activation, OS input, actual message queue, engine/UI routing or game.'}
    (folder/'result.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8');print(baseline.stdout.strip());print(folder/'result.json')
if __name__=='__main__':main()
