#!/usr/bin/env python3
"""Actual route/publication/inventory/queue code. No native input activation.

Only the engine enum declarations and inventory's PCH include boundary are
projected. Actual enum bytes are pinned to KeyInput.h/sys_public.h; no copied
numeric guesses. The ordinary queue source/GUI facts remain counted interfaces.
"""
from pathlib import Path
import argparse,configparser,hashlib,json,os,re,shutil,subprocess,tempfile
import native_event_disposition as disposition_test
import sdl3_clipboard_status as sdl_source
ROOT=Path(__file__).resolve().parents[2]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def change(body,old,new):
    expected=2 if old in ('!owner.MatchesOriginalBinding(expectedRoute,b)','!r.MatchesOriginalBinding(id,*frozen)') else 1
    if body.count(old)!=expected:raise RuntimeError('Nonunique mutation '+old)
    return body.replace(old,new)
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--compiler',default='clang++');p.add_argument('--sdl-include',type=Path)
    p.add_argument('--msvc-debug',action='store_true');p.add_argument('--sanitizers',action='store_true');p.add_argument('--no-mutations',action='store_true')
    a=p.parse_args();msvc=Path(a.compiler).name.lower() in ('cl','cl.exe')
    (ROOT/'.tmp').mkdir(exist_ok=True)
    scratch=Path(tempfile.mkdtemp(prefix='native-input-facts-',dir=ROOT/'.tmp'))
    provision={'kind':'explicit-include'}
    if a.sdl_include:
        sdl=a.sdl_include.resolve()
    else:
        config=configparser.ConfigParser();config.read(ROOT/'subprojects/sdl3.wrap',encoding='utf-8')
        specification=dict(config['wrap-file'])
        assert specification['directory']=='SDL3-3.4.10','Review SDL public header closure on dependency update'
        sdl_source.ROOT=ROOT
        sdl_source.FILES=['include/SDL3/'+name for name in disposition_test.SDL_HEADERS]
        prepared,provision,_=sdl_source.provision_source(None,specification,scratch)
        sdl=prepared/'include'
    if not (sdl/'SDL3/SDL_events.h').is_file():raise RuntimeError('Explicit SDL headers required')
    sources=['src/ui/retained/TextInput.cpp','src/framework/NativeInputRoute.cpp','src/framework/NativeInputPublications.cpp','src/sys/EventDisposition.cpp',
        'src/sys/EventQueueContinuity.cpp','src/sys/sdl3/NativeQueueBatch.cpp','src/sys/sdl3/NativeEventDisposition.cpp']
    inventory=ROOT/'src/sys/sdl3/NativeInputEmissionInventory.cpp';body=inventory.read_text(encoding='utf-8-sig')
    files=[ROOT/n for n in sources]+[inventory,Path(__file__).resolve(),ROOT/'tools/tests/native/NativeInputFactsTest.cpp',
        ROOT/'tools/tests/native/NativeEventDispositionTest.cpp',ROOT/'src/framework/KeyInput.h',ROOT/'src/sys/sys_public.h']
    files+=list((ROOT/'src/framework').glob('NativeInput*.h'))+list((ROOT/'src/sys/sdl3').glob('Native*.h'))
    files += [ROOT/n for n in ['src/sys/EventDisposition.h','src/sys/EventQueueContinuity.h','src/ui/retained/NativeTextDocument.h',
        'src/ui/retained/TextInput.h','src/ui/retained/TextInputBroker.h','subprojects/packagefiles/sdl3/include/SDL3/SDL_openq4_native_fence.h',
        'tools/tests/native_event_disposition.py','tools/tests/sdl3_clipboard_status.py','subprojects/sdl3.wrap']]
    sdl_headers={name:sha(sdl/'SDL3'/name) for name in disposition_test.SDL_HEADERS}
    original={str(n.relative_to(ROOT)):sha(n) for n in files}
    key=(ROOT/'src/framework/KeyInput.h').read_text(encoding='utf-8-sig')
    system=(ROOT/'src/sys/sys_public.h').read_text(encoding='utf-8-sig')
    enums=re.search(r'typedef enum \{\s*K_TAB.*?\}\s*keyNum_t;',key,re.S)[0]
    enums+='\n'+re.search(r'typedef enum \{\s*SE_NONE.*?\}\s*sysEventType_t;',system,re.S)[0]
    enums+='\n'+re.search(r'typedef enum \{\s*M_ACTION1.*?\}\s*sys_mEvents;',system,re.S)[0]+'\n'
    (scratch/'engine_enums.inc').write_text(enums,encoding='utf-8')
    projected=change(change(body,'#include "../../idlib/precompiled.h"\n',''),
        '#include "../../framework/KeyInput.h"','#include "engine_enums.inc"')
    working=scratch/'NativeInputEmissionInventory.cpp';working.write_text(projected)
    publication=scratch/'NativeInputPublications.cpp';pub=(ROOT/'src/framework/NativeInputPublications.cpp').read_text(encoding='utf-8-sig');publication.write_text(pub)
    includes=[scratch,ROOT,ROOT/'src/framework',ROOT/'src/sys/sdl3',ROOT/'subprojects/packagefiles/sdl3/include',sdl]
    flags=['/nologo','/std:c++20','/EHsc','/W4','/WX','/permissive-','/MTd' if a.msvc_debug else '/MT','/D_ITERATOR_DEBUG_LEVEL='+('2' if a.msvc_debug else '0')] if msvc else ['-std=c++20','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-pthread']
    if os.name=='nt' and not msvc:flags.remove('-pthread')
    if a.msvc_debug:flags+=['/D_DEBUG']
    if a.sanitizers:flags+=['-fsanitize=address,undefined','-fno-omit-frame-pointer','-fno-pie','-no-pie']
    flags += [('/I' if msvc else '-I')+str(n) for n in includes]
    env={**os.environ,'TEMP':str(scratch),'TMP':str(scratch),'TMPDIR':str(scratch)}
    if a.sanitizers:env.update(ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
    report={'passed':False,'sources':original,'cases':[],'compiler':a.compiler,'msvc_debug':a.msvc_debug,'sanitizers':a.sanitizers,
        'sdl_headers':sdl_headers,'sdl_provision':provision}
    def command(args,name):
        r=subprocess.run(args,cwd=scratch,env=env,text=True,encoding='utf-8',errors='replace',capture_output=True,timeout=150)
        (scratch/(name+'.log')).write_text(r.stdout+r.stderr)
        return r
    def compile(path,name):
        obj=scratch/(name+'.obj');r=command([a.compiler]+flags+(['/c',str(path),'/Fo:'+str(obj)] if msvc else ['-c',str(path),'-o',str(obj)]),name+'-compile')
        if r.returncode:raise RuntimeError(str(scratch/(name+'-compile.log'))+'\n'+r.stdout+r.stderr)
        return obj
    try:
        common=[compile(ROOT/n,'common-'+str(i)) for i,n in enumerate(sources) if n!='src/framework/NativeInputPublications.cpp']
        common+=[compile(ROOT/'tools/tests/native/NativeInputFactsTest.cpp','test')]
        def case(name,mutant=False,exhaust=False):
            objects=common+[compile(working,name+'-inventory'),compile(publication,name+'-publication')]
            exe=scratch/(name+('.exe' if os.name=='nt' else ''))
            r=command([a.compiler]+flags+[str(n) for n in objects]+(['/Fe:'+str(exe)] if msvc else ['-o',str(exe)]),name+'-link')
            if r.returncode:raise RuntimeError(r.stdout+r.stderr)
            r=command([str(exe)]+(['--exhaust'] if exhaust else []),name+'-run')
            report['cases'].append({'name':name,'exit':r.returncode,'sha256':sha(exe),'inventory':sha(working),'publication':sha(publication),'output':r.stdout+r.stderr})
            if mutant:
                if not r.returncode or 'FAIL' not in r.stderr:raise RuntimeError('Mutation not assertion rejected '+name+'\n'+r.stdout+r.stderr)
            elif r.returncode:raise RuntimeError(name+'\n'+r.stdout+r.stderr)
            print(name+': '+('rejected' if mutant else r.stdout.strip()),flush=True)
        case('baseline')
        publication.write_text(change(pub,'transition = 1;','transition = UINT64_MAX-1;'));case('exhaustion',exhaust=True);publication.write_text(pub)
        mutations=[
            ('publication-partial-binding','pub','!owner.MatchesOriginalBinding(expectedRoute,b)','false'),
            ('inventory-partial-binding','inventory','!r.MatchesOriginalBinding(id,*frozen)','false'),
            ('missing-ui-revoke','pub','if (p.owner && ((allocation && allocation==p.allocation) || (backend && backend==p.backend))) Revoke(p);','(void)allocation;(void)backend;'),
            ('cleanup-fault-refused','pub','return Sys_EventDispositionBoundThread() && p.owner==&owner && p.route==route;','return Sys_EventDispositionBoundThread() && !p.wrongThread && !p.exhausted && p.owner==&owner && p.route==route;'),
            ('terminal-ticket-reclassified','inventory','actual.terminal,actual.inFlight','false,actual.inFlight'),
            ('admission-value-unchecked','inventory','entry->emission.value!=frozen.value','false'),
            ('wrong-sibling-admission','inventory','ledger->Admit(frozen.ticket,candidate,error)','ledger->Admit(entries.front().emission.ticket,candidate,error)'),
            ('deferred-key-omitted','inventory','key==K_CTRL || key==K_ALT || key==K_RIGHT_ALT || key==K_PRINT_SCR','key==K_CTRL || key==K_ALT || key==K_RIGHT_ALT'),
            ('deferred-child-value-lost','inventory','entries[entries.size()-2].emission.value.deferredEmission=candidate.deferred.ticket.emission;','entries[entries.size()-2].emission.value.deferredEmission=0;'),
            ('post-issue-owner-unchecked','inventory','!tag.ShapeValid() || !Current()','!tag.ShapeValid()'),
            ('post-admit-owner-unchecked','inventory','!ledger->Admit(frozen.ticket,candidate,error) || !Current()','!ledger->Admit(frozen.ticket,candidate,error)'),
            ('inspect-editor-unchecked','inventory','b.editor!=original->editor ||',''),
        ]
        if not a.no_mutations:
            for name,target,old,new in mutations:
                working.write_text(change(projected,old,new) if target=='inventory' else projected)
                publication.write_text(change(pub,old,new) if target=='pub' else pub)
                case(name,True)
        report['passed']=original=={str(n.relative_to(ROOT)):sha(n) for n in files} and sdl_headers=={name:sha(sdl/'SDL3'/name) for name in disposition_test.SDL_HEADERS}
    finally:
        report['logs']={n.name:sha(n) for n in scratch.glob('*.log')}
        (scratch/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(scratch/'result.json',flush=True)
    if not report['passed']:raise RuntimeError('Sources changed during run')
if __name__=='__main__':main()
