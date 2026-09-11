#!/usr/bin/env python3
"""Actual Windows SDK session/store/bridge, Interaction/coordinator and owned queue.

Only COM factory/SDK objects and SDL provider admission are counted doubles.
No CoInitialize/CoCreateInstance factory is invoked, no native HWND, SDL pump,
OS input, live TIP, Session activation, shared engine build or game is run.
"""
from pathlib import Path
import argparse, hashlib, json, os, re, subprocess, tempfile
from filesystem_case_segments import function_body
ROOT=Path(__file__).resolve().parents[2]
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--compiler',default='clang++');p.add_argument('--sdl-include',type=Path,required=True)
    p.add_argument('--msvc-debug',action='store_true');p.add_argument('--no-mutations',action='store_true')
    p.add_argument('--route-facts',action='store_true',help='Qualify the copied production route/controller facts without activation.')
    a=p.parse_args()
    if os.name!='nt':raise SystemExit('Actual Windows SDK required; no portable COM qualification.')
    sdl=a.sdl_include.resolve()
    if not (sdl/'SDL3/SDL_events.h').is_file():raise SystemExit('Missing explicit SDL include tree')
    folder=Path(tempfile.mkdtemp(prefix='windows-text-session-',dir=ROOT/'.tmp'))
    core=ROOT/'src/ui/retained';sys=ROOT/'src/sys/sdl3'
    originals=[sys/'WindowsTextSession.cpp',sys/'WindowsTextStore.cpp',sys/'WindowsTextCollectionBridge.cpp',
        sys/'NativeQueueBatch.cpp',ROOT/'src/ui/application/NativeTextCollectionCoordinator.cpp']
    originals += [core/n for n in ['Interaction.cpp','ScrollGeometry.cpp','TextInput.cpp','TextEdit.cpp','NativeTextDocument.cpp','NativeTextEditor.cpp','Input.cpp']]
    test=ROOT/('tools/tests/native/WindowsNativeInputFactsTest.cpp' if a.route_facts else 'tools/tests/native/WindowsTextSessionTest.cpp')
    paths=originals+[test,Path(__file__).resolve(),ROOT/'tools/tests/filesystem_case_segments.py',core/'Document.cpp']
    paths+=list((ROOT/'src').rglob('*.h'))+[ROOT/'subprojects/packagefiles/sdl3/include/SDL3/SDL_openq4_native_fence.h']
    before={str(path.relative_to(ROOT)):sha(path) for path in paths}
    sdk_headers={str(path.relative_to(sdl)):sha(path) for path in sdl.glob('SDL3/*.h')}
    valid=folder/'valid.cpp';doc=(core/'Document.cpp').read_text(encoding='utf-8-sig')
    valid.write_text('#include "Interaction.h"\n#include <cmath>\nnamespace openq4::ui {\n'+
        function_body(doc,'bool Utf8(')+function_body(doc,'bool ValidStateValue(')+'}\n',encoding='utf-8',newline='\n')
    working=folder/'WindowsTextSession.cpp';body=originals[0].read_text(encoding='utf-8-sig');working.write_text(body,encoding='utf-8',newline='\n')
    if a.route_facts:
        key=(ROOT/'src/framework/KeyInput.h').read_text(encoding='utf-8-sig')
        system=(ROOT/'src/sys/sys_public.h').read_text(encoding='utf-8-sig')
        enums=re.search(r'typedef enum \{\s*K_TAB.*?\}\s*keyNum_t;',key,re.S)[0]
        enums+='\n'+re.search(r'typedef enum \{\s*SE_NONE.*?\}\s*sysEventType_t;',system,re.S)[0]
        enums+='\n'+re.search(r'typedef enum \{\s*M_ACTION1.*?\}\s*sys_mEvents;',system,re.S)[0]
        (folder/'engine_enums.inc').write_text(enums,encoding='utf-8')
        inventory=folder/'NativeInputEmissionInventory.cpp'
        inventory.write_text((sys/'NativeInputEmissionInventory.cpp').read_text(encoding='utf-8-sig').replace('#include "../../idlib/precompiled.h"\n','').replace('#include "../../framework/KeyInput.h"','#include "engine_enums.inc"'),encoding='utf-8')
        fact_source=(sys/'WindowsNativeInputRouteSource.cpp').read_text(encoding='utf-8-sig')
        fact_working=folder/'WindowsNativeInputRouteSource.cpp';fact_working.write_text(fact_source,encoding='utf-8')
        originals += [ROOT/'src/framework/NativeInputRoute.cpp',sys/'NativeEventDisposition.cpp',inventory]
        paths += [sys/'WindowsNativeInputRouteSource.cpp',ROOT/'src/framework/NativeInputRoute.cpp',sys/'NativeEventDisposition.cpp',
            sys/'NativeInputEmissionInventory.cpp',ROOT/'src/framework/KeyInput.h',ROOT/'src/sys/sys_public.h',
            ROOT/'tools/tests/native/WindowsTextSessionTest.cpp']
        before={str(path.relative_to(ROOT)):sha(path) for path in paths}
    includes=[ROOT,ROOT/'src',core,sys,ROOT/'subprojects/packagefiles/sdl3/include',sdl]
    flags=[a.compiler,'-std=c++20','-Wall','-Wextra','-Werror','-Wno-missing-field-initializers',
        '-DSDL_STATIC_LIB','-DUSE_SDL3','-DOPENQ4_SDL3_CHECKED_NATIVE_QUEUE=1']
    flags += ['-I'+str(n) for n in includes]
    if a.msvc_debug:
        flags=[a.compiler,'/nologo','/std:c++20','/EHsc','/W3','/WX','/wd4458','/MTd','/D_ITERATOR_DEBUG_LEVEL=2',
            '/D_CRT_SECURE_NO_WARNINGS','/DSDL_STATIC_LIB','/DUSE_SDL3','/DOPENQ4_SDL3_CHECKED_NATIVE_QUEUE=1']+['/I'+str(n) for n in includes]
    env={**os.environ,'TEMP':str(folder),'TMP':str(folder),'TMPDIR':str(folder)}
    report={'passed':False,'scope':__doc__,'sources':before,'sdl_headers':sdk_headers,'cases':[]}
    def run(command,name):
        r=subprocess.run(command,cwd=folder,env=env,text=True,encoding='utf-8',errors='replace',capture_output=True,timeout=180)
        log=folder/(name+'.log');log.write_text(r.stdout+r.stderr,encoding='utf-8')
        return r,log
    def compile(source,name):
        obj=folder/(name+'.obj')
        command=flags+(['-c',str(source),'-o',str(obj)] if not a.msvc_debug else ['/c',str(source),'/Fo:'+str(obj)])
        r,log=run(command,name+'-compile')
        if r.returncode:raise RuntimeError(str(log)+'\n'+r.stdout+r.stderr)
        return obj
    try:
        common=[compile(path,'common-'+str(i)) for i,path in enumerate(originals[1:]+[valid,test])]
        def case(name,mutant=False):
            obj=compile(working,name);binary=folder/(name+'.exe')
            extra=[compile(fact_working,name+'-facts')] if a.route_facts else []
            command=[a.compiler]+[str(n) for n in common+extra]+[str(obj)]+(['ole32.lib','uuid.lib'] if a.msvc_debug else ['-lole32','-luuid'])
            command+=['-o',str(binary)] if not a.msvc_debug else ['/nologo','/Fe:'+str(binary)]
            r,log=run(command,name+'-link')
            if r.returncode:raise RuntimeError(str(log)+'\n'+r.stdout+r.stderr)
            r,log=run([str(binary)],name+'-run')
            entry={'name':name,'exit':r.returncode,'source_sha256':sha(working),'binary_sha256':sha(binary),'log_sha256':sha(log)}
            if a.route_facts:entry['facts_source_sha256']=sha(fact_working)
            report['cases'].append(entry)
            if mutant:
                if not r.returncode or 'FAIL' not in r.stdout+r.stderr:raise RuntimeError('Mutation not assertion-rejected: '+name+'\n'+r.stdout+r.stderr)
                entry['rejected']=True
            elif r.returncode:raise RuntimeError(str(log)+'\n'+r.stdout+r.stderr)
            else:entry['checks']=int(re.search(r'PASS (\d+) checks',r.stdout)[1]);print(r.stdout.strip(),flush=True)
        case('baseline')
        mutations={
            's-false-unbalanced':('if(SUCCEEDED(hr))apartment=true;','if(hr==S_OK)apartment=true;'),
            'early-uninitialize':('if(!After(hr) || client==TF_CLIENTID_NULL)return false;','if(!After(hr) || client==TF_CLIENTID_NULL)return false; platform.UninitializeSta();'),
            'missing-unadvise':('if(editSource && sinkCookie!=TF_INVALID_COOKIE){','if(false && editSource && sinkCookie!=TF_INVALID_COOKIE){'),
            'missing-pop':('pushAttempted=true;hr=document->Push(context);','hr=document->Push(context);'),
            'unguarded-association-restore':('if(!Window()){\n                associationLost','if(false){\n                associationLost'),
            'reentry-not-latched':('if(busy)return Fail(error,"Reentrant Windows text session operation");','if(busy)return false;'),
            'ignored-termination-failure':('terminated=SUCCEEDED(hr);\n            if(!After(hr))return false;','terminated=SUCCEEDED(hr);\n            if(false)return false;'),
            'old-owner-not-retired':('p.current=std::move(current);p.RetireOwner();','p.current=std::move(current);'),
            'cleanup-seal-unchecked':('expected!=WindowsTextSessionCleanup{p.identity,p.closed}','(void(expected),false)'),
            'cleanup-ack-unchecked':('!checked.Acknowledge(batch.Status(),error)','(checked.Acknowledge(batch.Status(),error),false)'),
            'cleanup-ack-bypasses-provider-claim':('!checked.Acknowledge(batch.Status(),error)','(void(checked),!fence.Acknowledge(batch.Status(),error))'),
            'unregister-result-ignored':('!OQ4_WindowsNativeFenceRegisterHooks(nullptr)','(OQ4_WindowsNativeFenceRegisterHooks(nullptr),false)'),
            'worker-emergency-admitted':('if(!p.Thread())return {};','if(false)return {};'),
            'mutable-native-completion':('const auto completion=requestedCompletion;','const auto& completion=requestedCompletion;'),
            'mutable-cleanup-disposition':('const auto expected=requestedCleanup;\n    const auto token=requestedDisposition;','const auto expected=requestedCleanup;\n    const auto& token=requestedDisposition;'),
            'disposition-not-checked-at-ack':('if(!session.Provider() || !ledger.Current(token) || session.fault)return false;','if(!session.Provider() || session.fault)return false;'),
            'destroy-retains-engine-lease':('p.phase!=Phase::Released || p.hooks || p.apartment','p.hooks || p.apartment'),
            'lost-provider-queried':('if(p.hooks && p.ProviderClaim()){','if(p.hooks){'),
            'replacement-generation-retired':('(!p.baseline.generation || !active || active==p.baseline.generation) && p.ProviderClaim()','(void(active),p.ProviderClaim())'),
        }
        if a.route_facts:
            mutations={
                'retirement-owner-unchecked':('!SameOwner(owner,p.original.editor) ||',''),
                'retirement-thread-unchecked':('if(!p.Thread() || p.busy || native!=p.original.native','if(p.busy || native!=p.original.native'),
                'retirement-window-unchecked':('!owner.revision || window!=p.window','!owner.revision'),
                'provider-never-registered-fabricated':('candidate.providerRetired=p.providerWasRetired;','candidate.providerRetired=p.providerWasRetired || !p.hooks;'),
                'store-retirement-forgotten':('candidate.storeRetired=p.storeWasRetired;','candidate.storeRetired=false;'),
                'query-busy-accepted':('if(!p.Thread() || p.busy || native!=p.original.native','if(!p.Thread() || native!=p.original.native'),
            }
        if not a.no_mutations:
            for name,(old,new) in mutations.items():
                if body.count(old)!=1:raise RuntimeError('Mutation anchor is not unique: '+name)
                working.write_text(body.replace(old,new),encoding='utf-8',newline='\n')
                case(name,True)
            if a.route_facts:
                working.write_text(body,encoding='utf-8',newline='\n')
                for name,old,new in [
                    ('registry-loss-blocks-cleanup','const bool managed=session.current && UI_QueryNativeInputAllocation(session.current,candidate.allocation);','const bool managed=session.current && UI_QueryNativeInputAllocation(session.current,candidate.allocation);if(!managed)return false;'),
                    ('missing-managed-live-gate','candidate.inputAllowed=managed && session.inputAllowed','candidate.inputAllowed=(void(managed),session.inputAllowed)'),
                    ('provider-registration-unchecked','current.registration==expected.registration','true'),
                    ('window-lifetime-unchecked','NativeWindow(Window(current))==expected','current.handle==reinterpret_cast<std::uintptr_t>(expected.hwnd)'),
                    ('source-backlog-fabricated','candidate.backlogDisposed=inventory && inventory->MatchesBinding(*route,routeId,*binding) && inventory->BacklogDisposed();','candidate.backlogDisposed=true;'),
                    ('source-release-skips-route','if(route->State()!=NativeInputRoute::Phase::Empty && !route->Release(routeId))return false;',''),
                    ('source-release-skips-unbind','if(!NativeInputUnbindPublications(*route,routeId))return false;',''),
                    ('source-release-loses-retry','if(!NativeInputUnbindPublications(*route,routeId))return false;','if(!NativeInputUnbindPublications(*route,routeId)){finished=true;return false;}'),
                    ('source-release-reentry-not-latched','if(releasing){(void)route->Revoke(routeId);return false;}','if(releasing)return false;'),
                    ('source-inventory-unbound-accepted','!binding || !value.MatchesBinding(*route,routeId,*binding)','!binding'),
                    ('original-binding-unchecked','id==routeId && Same(expected,*binding)','id==routeId && (void(expected),Same(*binding,*binding))'),
                ]:
                    if fact_source.count(old)!=1:raise RuntimeError('Facts mutation anchor '+name)
                    fact_working.write_text(fact_source.replace(old,new),encoding='utf-8',newline='\n');case(name,True)
        report['sources_unchanged']=before=={str(path.relative_to(ROOT)):sha(path) for path in paths}
        report['passed']=report['sources_unchanged']
    finally:
        working.write_text(body,encoding='utf-8',newline='\n')
        report['logs']={p.name:sha(p) for p in folder.glob('*.log')}
        report['objects']={p.name:sha(p) for p in folder.glob('common-*.obj')}
        (folder/'result.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
        print(folder/'result.json',flush=True)
    if not report['passed']:raise SystemExit('Source binding failed')
if __name__=='__main__':main()
