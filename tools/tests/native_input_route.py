#!/usr/bin/env python3
"""Qualify the actual portable route helper with copied engine-fact doubles.

No SDK, native, GUI, queue or input calls occur. The test includes the complete
production TU; required external lifecycle/translator facts are counted fixtures,
not production proof. Every mutation must compile and fail the behavioral suite.
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

ROOT=Path(__file__).resolve().parents[2]
HEADER='src/framework/NativeInputRoute.h'
SOURCE='src/framework/NativeInputRoute.cpp'
TEST='tools/tests/native/NativeInputRouteTest.cpp'
FILES=[HEADER,SOURCE,TEST,'tools/tests/native_input_route.py','src/sys/EventDisposition.h',
       'src/ui/retained/TextInputBroker.h','src/ui/retained/TextInput.h','src/ui/retained/NativeTextDocument.h']

def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()
def change(text,old,new,count=1):
    if text.count(old)!=count: raise RuntimeError(f'Nonunique mutation {old!r}: {text.count(old)} != {count}')
    return text.replace(old,new)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler')
    parser.add_argument('--msvc-debug',action='store_true')
    parser.add_argument('--sanitizers',action='store_true')
    parser.add_argument('--no-mutations',action='store_true')
    args=parser.parse_args()
    compiler=args.compiler or next((p for name in ('clang++','g++','c++') if (p:=shutil.which(name))),None)
    if not compiler: raise RuntimeError('C++20 compiler required')
    msvc=Path(compiler).name.lower() in ('cl','cl.exe')
    if args.msvc_debug and not msvc: raise RuntimeError('--msvc-debug requires cl.exe')
    if args.sanitizers and msvc: raise RuntimeError('Sanitizer run requires GCC/Clang')
    (ROOT/'.tmp').mkdir(exist_ok=True)
    scratch=Path(tempfile.mkdtemp(prefix='native-input-route-',dir=ROOT/'.tmp'))
    env=dict(os.environ,TEMP=str(scratch),TMP=str(scratch),TMPDIR=str(scratch))
    if args.sanitizers: env.update(ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
    original={name:sha(ROOT/name) for name in FILES}
    source=(ROOT/SOURCE).read_text(encoding='utf-8')
    header=(ROOT/HEADER).read_text(encoding='utf-8')
    mutations=[
        ('prepare-allocation', 'observed.current != b.outer || observed.allocation != b.editor.allocation ||', 'observed.current != b.outer ||'),
        ('probe-transition', 'observed.allocation != b.editor.allocation || observed.sessionTransition != b.sessionTransition ||', 'observed.allocation != b.editor.allocation ||'),
        ('provider-registration', 'o.window.registration == entry->binding.window.registration;', 'true;'),
        ('window-lifetime', 'o.window == entry->binding.window;', 'o.window.handle == entry->binding.window.handle;'),
        ('ui-alone-retirement', 'facts.store == NativeInputNativeRetirement::RetiredExact && facts.provider == NativeInputNativeRetirement::RetiredExact;', 'true;'),
        ('ui-proof-omitted', '(facts.ui == NativeInputUiRetirement::RetiredExact || facts.ui == NativeInputUiRetirement::AbsentOriginal) &&', 'true &&'),
        ('wrong-native-retirement', 'out.native == entry->binding.native &&', 'true &&'),
        ('wrong-provider-retirement', 'out.window == entry->binding.window;', 'true;'),
        ('borrowed-binding', 'const NativeInputBinding binding;', 'const NativeInputBinding& binding;'),
        ('issued-missing', '!issued.issued || issued.terminal ||', 'issued.terminal ||'),
        ('terminal-accepted', 'issued.terminal || issued.inFlight ||', 'issued.inFlight ||'),
        ('inflight-accepted', 'issued.inFlight || issued.tag != head.tag ||', 'issued.tag != head.tag ||'),
        ('issued-tag-unchecked', 'issued.tag != head.tag ||', ''),
        ('issued-value-unchecked', 'issued.value != head.value ||', ''),
        ('wrong-kind-lane', '!KindMatches(issued,head.lane)', '(false && !KindMatches(issued,head.lane))'),
        ('post-inspection-retirement', 'if (!ReadRetirement(facts) || !Retired(facts) || mutation != before) return false;', 'if (mutation != before) return false;'),
        ('failed-refresh-keeps-permit', 'Guard guard(*this);\n    permitSerial = 0;', 'Guard guard(*this);'),
        ('revoked-permit-serial-reused', '++mutation; permitSerial = 0;', '++mutation; permitSerial = 0; permitHighwater = 0;'),
        ('permit-serial-unchecked', 'permit.serial == permitSerial &&', 'true &&'),
        ('head-sequence-unchecked', 'permit.head == actual;', '[&]{ auto adjusted=actual; adjusted.sequence=permit.head.sequence; return permit.head==adjusted; }();'),
        ('head-storage-epoch-unchecked', 'permit.head == actual;', '[&]{ auto adjusted=actual; adjusted.tag.dispatchEpoch=permit.head.tag.dispatchEpoch; return permit.head==adjusted; }();'),
        ('head-emission-unchecked', 'permit.head == actual;', '[&]{ auto adjusted=actual; adjusted.tag.emission=permit.head.tag.emission; return permit.head==adjusted; }();'),
        ('head-payload-unchecked', 'permit.head == actual;', '[&]{ auto adjusted=actual; adjusted.value.payload=permit.head.value.payload; return permit.head==adjusted; }();'),
        ('repeated-reentry-unobserved', 'else ++mutation;', 'else (void)mutation;'),
        ('revocation-unobserved', '++mutation; permitSerial = 0;', 'permitSerial = 0;'),
        ('release-disposal-unchecked', '!facts.hooksRemoved || !facts.controllerReleased || !facts.backlogDisposed', 'false'),
        ('wrong-thread-accepted', 'return thread == std::this_thread::get_id();', 'return true;'),
        ('retired-epoch-rejected', '!observed.boundThread || mutation != before', '!observed.boundThread || !observed.dispatchEpoch || mutation != before'),
        ('route-identity-reused', 'entry.reset(); phase = Phase::Empty;', 'entry.reset(); nativeInputRouteHighwater=0; phase = Phase::Empty;'),
        ('prepare-output-early', 'auto candidate = std::make_unique<Entry>(requested, route);', 'auto candidate = std::make_unique<Entry>(requested, route); out=route;'),
        ('fault-blocks-drain', 'phase != Phase::Revoked) return false;', 'phase != Phase::Revoked || poisoned) return false;'),
        ('fault-blocks-cancellation', 'phase != Phase::DrainOnly || !HeadShape(requested)', 'phase != Phase::DrainOnly || poisoned || !HeadShape(requested)'),
        ('fault-blocks-provider-cleanup', 'return Observe(observed) && OriginalProvider(observed);', 'return !poisoned && Observe(observed) && OriginalProvider(observed);'),
    ]
    cases=[('baseline',None,None),('permit-exhaustion',None,None)]
    if not args.no_mutations: cases += mutations
    results=[]
    try:
        for name,old,new in cases:
            folder=scratch/name
            for relative in FILES:
                target=folder/relative;target.parent.mkdir(parents=True,exist_ok=True)
                shutil.copy2(ROOT/relative,target)
            if old is not None: (folder/SOURCE).write_text(change(source,old,new),encoding='utf-8',newline='\n')
            if name=='permit-exhaustion':
                (folder/HEADER).write_text(change(header,'permitHighwater = 0;','permitHighwater = (std::numeric_limits<std::uint64_t>::max)()-1;'),encoding='utf-8',newline='\n')
            exe=folder/('test.exe' if os.name=='nt' else 'test')
            if msvc:
                command=[compiler,'/nologo','/std:c++20','/EHsc','/W4','/WX','/permissive-', '/MTd' if args.msvc_debug else '/MT']
                if args.msvc_debug: command += ['/D_DEBUG']
                else: command += ['/D_ITERATOR_DEBUG_LEVEL=0']
                command += [TEST,'/Fe:'+str(exe),'/Fo:'+str(folder/'test.obj')]
            else:
                command=[compiler,'-std=c++20','-Wall','-Wextra','-Werror','-pthread',TEST,'-o',str(exe)]
                if args.sanitizers: command[1:1]=['-fsanitize=address,undefined','-fno-omit-frame-pointer','-fno-pie','-no-pie']
            compiled=subprocess.run(command,cwd=folder,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,timeout=120)
            (folder/'compile.log').write_text(compiled.stdout,encoding='utf-8')
            if compiled.returncode: raise RuntimeError(name+' failed to compile: '+str(folder/'compile.log'))
            run=subprocess.run([str(exe)]+(['--permit-exhaustion'] if name=='permit-exhaustion' else []),cwd=folder,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,timeout=60)
            (folder/'run.log').write_text(run.stdout,encoding='utf-8')
            expected_failure=old is not None
            if (run.returncode!=0)!=expected_failure: raise RuntimeError(name+' unexpected result: '+str(folder/'run.log'))
            match=re.search(r'NativeInputRouteTest checks=(\d+) failures=(\d+)',run.stdout)
            if not match or (expected_failure and int(match[2])==0): raise RuntimeError(name+' did not report checked behavioral result')
            results.append({'case':name,'checks':int(match[1]),'failures':int(match[2]),'returncode':run.returncode,
                'source_sha256':sha(folder/SOURCE),'header_sha256':sha(folder/HEADER),'executable_sha256':sha(exe),
                'compile_log_sha256':sha(folder/'compile.log'),'run_log_sha256':sha(folder/'run.log'),'command':command})
            print(name+': '+('mutation rejected' if expected_failure else 'passed '+match[1]+' checks'),flush=True)
        if original!={name:sha(ROOT/name) for name in FILES}: raise RuntimeError('Input sources changed during run')
        report={'status':'passed','platform':platform.platform(),'compiler':compiler,'msvc_debug':args.msvc_debug,
            'sanitizers':args.sanitizers,'source_hashes':original,'results':results,
            'limits':['Engine facts are counted trusted interfaces, not implemented manager/provider/translator proof.',
                'No queue take, delivery, held-source cleanup, provider activation or native/GUI effects.',
                'Permit exhaustion fixture changes only private counter initialization to MAX-1.',
                'Debug STL broad allocation sweep omitted; actual callback-free probe/permit/release paths run under allocation denial.']}
        (scratch/'result.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
        print(scratch/'result.json',flush=True)
    except Exception as failure:
        (scratch/'failure.json').write_text(json.dumps({'error':str(failure),'source_hashes':original,'completed':results},indent=2)+'\n',encoding='utf-8')
        raise

if __name__=='__main__': main()
