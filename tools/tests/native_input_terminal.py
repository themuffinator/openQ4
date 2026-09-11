#!/usr/bin/env python3
"""Actual platform/pushed/SDL storage, checked transfer owner and retired ledger.

Source/UI/provider facts and engine allocator are counted. There is no native
activation, OS input, GUI delivery, shared build or successful native ACK.
"""
from pathlib import Path
import argparse,configparser,hashlib,json,os,re,subprocess,tempfile
import native_event_retirement as storage
import native_event_disposition as disposition_test
import sdl3_clipboard_status as sdl_source
ROOT=Path(__file__).resolve().parents[2]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def read(n):return (ROOT/n).read_text(encoding='utf-8-sig')
def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--compiler',default='clang++');parser.add_argument('--sdl-include',type=Path)
    parser.add_argument('--sanitizers',action='store_true');parser.add_argument('--msvc-debug',action='store_true');parser.add_argument('--no-mutations',action='store_true')
    a=parser.parse_args();msvc=Path(a.compiler).name.lower() in ('cl','cl.exe')
    (ROOT/'.tmp').mkdir(exist_ok=True)
    out=Path(tempfile.mkdtemp(prefix='native-input-terminal-',dir=ROOT/'.tmp'))
    provision={'kind':'explicit-include'}
    if a.sdl_include:
        sdl=a.sdl_include.resolve()
    else:
        config=configparser.ConfigParser();config.read(ROOT/'subprojects/sdl3.wrap',encoding='utf-8')
        specification=dict(config['wrap-file'])
        assert specification['directory']=='SDL3-3.4.10','Review SDL public header closure on dependency update'
        sdl_source.ROOT=ROOT
        sdl_source.FILES=['include/SDL3/'+name for name in disposition_test.SDL_HEADERS]
        prepared,provision,_=sdl_source.provision_source(None,specification,out)
        sdl=prepared/'include'
    if not (sdl/'SDL3/SDL_events.h').is_file():raise RuntimeError('SDL public headers unavailable')
    sources=['src/framework/NativeInputRoute.cpp','src/sys/EventDisposition.cpp','src/sys/EventQueueContinuity.cpp','src/sys/sdl3/NativeQueueBatch.cpp','src/sys/sdl3/NativeEventDisposition.cpp','src/ui/retained/TextInput.cpp']
    files=list(dict.fromkeys(storage.FILES+sources+['src/sys/sdl3/NativeInputTransfers.h','src/sys/sdl3/NativeInputTransfers.cpp','src/sys/sdl3/NativeInputEmissionInventory.h','src/sys/sdl3/NativeInputEmissionInventory.cpp','tools/tests/native_input_terminal.py','tools/tests/native/NativeInputTerminalTest.cpp','tools/tests/native/NativeEventDispositionTest.cpp','src/sys/sdl3/NativeEventDisposition.h','src/sys/sdl3/NativeQueueBatch.h','src/ui/retained/TextInput.h','subprojects/packagefiles/sdl3/include/SDL3/SDL_openq4_native_fence.h']))
    files += ['tools/tests/sdl3_clipboard_status.py','subprojects/sdl3.wrap']
    before={n:sha(ROOT/n) for n in files}
    headers={name:sha(sdl/'SDL3'/name) for name in disposition_test.SDL_HEADERS}
    report={'passed':False,'sources':before,'sdl_headers':headers,'sdl_provision':provision,
        'compiler':a.compiler,'msvc_debug':a.msvc_debug,'sanitizers':a.sanitizers,'cases':[]}
    enums=storage.sdl.enum(read('src/framework/KeyInput.h'),'keyNum_t')+'\n'+storage.queues.event_types()+'\n'+storage.sdl.enum(read('src/sys/sys_public.h'),'sys_mEvents')
    # event_types also imports EventRetirement/type enum through actual source.
    (out/'engine_enums.inc').write_text(enums,newline='\n')
    flags=['/nologo','/std:c++20','/EHsc','/W4','/WX','/wd4100','/wd4505','/MTd' if a.msvc_debug else '/MT','/D_ITERATOR_DEBUG_LEVEL='+('2' if a.msvc_debug else '0')] if msvc else ['-std=c++20','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-unused-function','-Wno-misleading-indentation','-pthread']
    if a.msvc_debug:flags+=['/D_DEBUG']
    if os.name=='nt' and not msvc:flags.remove('-pthread')
    if a.sanitizers:flags+=['-fsanitize=address,undefined','-fno-omit-frame-pointer','-fno-pie','-no-pie']
    flags += [('/I' if msvc else '-I')+str(p) for p in [out,ROOT,ROOT/'src/sys/sdl3',ROOT/'src/framework',ROOT/'subprojects/packagefiles/sdl3/include',sdl]]
    env={**os.environ,'TEMP':str(out),'TMP':str(out),'TMPDIR':str(out)}
    if a.sanitizers:env.update(ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
    def run(args,name):
        r=subprocess.run(args,cwd=out,env=env,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=180);(out/(name+'.log')).write_text(r.stdout+r.stderr);return r
    def compile(p,name):
        obj=out/(name+'.obj');r=run([a.compiler]+flags+(['/c',str(p),'/Fo:'+str(obj)] if msvc else ['-c',str(p),'-o',str(obj)]),name+'-compile')
        if r.returncode:raise RuntimeError(r.stdout+r.stderr)
        return obj
    try:
        storage.ROOT=ROOT;storage.queues.ROOT=ROOT
        common=[compile(ROOT/n,'common-'+str(i)) for i,n in enumerate(sources)]
        inv=out/'NativeInputEmissionInventory.cpp';inv.write_text(read('src/sys/sdl3/NativeInputEmissionInventory.cpp').replace('#include "../../idlib/precompiled.h"\n','').replace('#include "../../framework/KeyInput.h"','#include "engine_enums.inc"'),newline='\n')
        common.append(compile(inv,'inventory'))
        invbody=inv.read_text();projections={};objects={}
        model=read('tools/tests/native/NativeEventDispositionTest.cpp')
        allocator=model[:model.index('using namespace openq4;')]
        fixture=model[model.index('struct Source final'):model.index('static void OrderedBatch(')]
        for target in ['windows','posix']:
            body=storage.projection(target,{'platform':read('src/sys/win32/win_main.cpp' if target=='windows' else 'src/sys/posix/posix_main.cpp'),'loop':read('src/framework/EventLoop.cpp'),'sdl':read('src/sys/sdl3/sdl3_backend.cpp')})
            body=body.replace('#include "tools/tests/native/NativeEventRetirementTest.cpp"','')
            body=body.replace('static void Mem_Free(void* pointer) {','static std::function<void()> onFree;\nstatic void Mem_Free(void* pointer) {\n    auto callback=std::move(onFree);if(callback)callback();')
            body='#include <functional>\n'+allocator+body+'\nusing namespace openq4;\n#define CHECK(...) Check((__VA_ARGS__),#__VA_ARGS__)\n'+fixture
            body+=read('src/sys/sdl3/NativeInputTransfers.cpp').replace('#include "../../idlib/precompiled.h"\n','')
            body+='\n#include "tools/tests/native/NativeInputTerminalTest.cpp"\n'
            path=out/(target+'.cpp');path.write_text(body,newline='\n');extra=['/DOPENQ4_SDL3_POSIX_HOST'] if msvc else ['-DOPENQ4_SDL3_POSIX_HOST']
            originalFlags=list(flags)
            if target=='posix':flags+=extra
            obj=compile(path,target);binary=out/(target+'.exe');r=run([a.compiler]+flags+[str(p) for p in common+[obj]]+(['/Fe:'+str(binary)] if msvc else ['-o',str(binary)]),target+'-link');flags[:]=originalFlags
            projections[target]=body;objects[target]=obj
            if r.returncode:raise RuntimeError(r.stdout+r.stderr)
            r=run([str(binary)],target+'-run');report['cases'].append({'name':target,'exit':r.returncode,'source_sha256':sha(path),'binary_sha256':sha(binary),'output':r.stdout+r.stderr})
            if r.returncode:raise RuntimeError(r.stdout+r.stderr)
            print(target+': '+r.stdout.strip(),flush=True)
        mutations=[
            ('stored-before-admit','transfer','entry->ownership=NativeInputEmissionInventory::Ownership::Stored;','entry->ownership=NativeInputEmissionInventory::Ownership::NeverTransferred;'),
            ('terminal-without-take','inventory','entry.ownership!=Ownership::TakenForDelivery || !actual.terminal','!actual.terminal'),
            ('external-issued-fabricated','inventory','if(entry.ownership==Ownership::NeverTransferred) {','if(entry.ownership==Ownership::External)return true;\n        if(entry.ownership==Ownership::NeverTransferred) {'),
            ('indeterminate-free-fabricated','inventory','if(entry.ownership==Ownership::Indeterminate)return false;','if(entry.ownership==Ownership::Indeterminate)return true;'),
            ('extra-issued-slot-lost','inventory','census.issued!=entries.size() ||',''),
            ('queue-callback-steals-issuance','inventory','if(transfers && !transferAuthorized)','if(false)'),
            ('cancel-does-not-free','transfer','try {if(event.evPtr)Mem_Free(event.evPtr);}','try {}'),
            ('cancel-not-recorded','transfer','entry->ownership=NativeInputEmissionInventory::Ownership::Cancelled;','entry->ownership=NativeInputEmissionInventory::Ownership::Stored;'),
            ('post-take-ledger-not-started','transfer','!inventory.ledger->BeginDelivery(emission.ticket,error)','false'),
            ('stored-caller-not-cleared','transfer','if(unchanged)event={};else Fail','if(unchanged){}else Fail'),
            ('stored-retirement-reported-unchanged','transfer','return NativeInputTransferStatus::StoredRetired;','return NativeInputTransferStatus::Unchanged;'),
            ('claim-reentry-not-latched','transfer','if(calling)return Fail(error,"Reentrant native transfer claim");','if(calling)return TransferError(error,"Reentrant native transfer claim");'),
            ('current-callback-prefix-consumed','transfer','if(freshStatus!=sysEventTransfer_t::Ready || fresh!=head || mutation!=before || interrupted)','if((void(freshStatus),false))'),
        ]
        if not a.no_mutations:
            for name,target,old,new in mutations:
                original=invbody if target=='inventory' else projections['windows']
                count=original.count(old)
                if count!=(2 if name=='stored-retirement-reported-unchanged' else 1):raise RuntimeError('Mutation anchor '+name+' count='+str(count))
                path=out/(name+'.cpp');path.write_text(original.replace(old,new),newline='\n');mut=compile(path,name)
                linked=common[:-1]+[mut,objects['windows']] if target=='inventory' else common+[mut]
                binary=out/(name+'.exe');r=run([a.compiler]+flags+[str(p) for p in linked]+(['/Fe:'+str(binary)] if msvc else ['-o',str(binary)]),name+'-link')
                if r.returncode:raise RuntimeError(r.stdout+r.stderr)
                r=run([str(binary)],name+'-run');report['cases'].append({'name':name,'exit':r.returncode,'rejected':r.returncode!=0 and 'FAIL' in r.stdout+r.stderr,'source_sha256':sha(path),'binary_sha256':sha(binary),'output':r.stdout+r.stderr})
                if not report['cases'][-1]['rejected']:raise RuntimeError('Mutation not assertion-rejected: '+name+'\n'+r.stdout+r.stderr)
                print('rejected: '+name,flush=True)
        report['passed']=True
    except Exception as error:report['failure']=str(error);print(error,flush=True)
    report['sources_unchanged']=before=={n:sha(ROOT/n) for n in files}
    report['headers_unchanged']=headers=={name:sha(sdl/'SDL3'/name) for name in disposition_test.SDL_HEADERS}
    report['passed'] &= report['sources_unchanged'] and report['headers_unchanged']
    report['logs']={p.name:sha(p) for p in out.glob('*.log')}
    (out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(out/'result.json',flush=True);return 0 if report['passed'] else 1
if __name__=='__main__':raise SystemExit(main())
