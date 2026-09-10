#!/usr/bin/env python3
"""Actual NativeTextDocument/NativeTextEditor collection reconciliation tests.

Runs the existing editor suite as a compatibility baseline, then scoped collection
cases and compiled behavioral mutants. No native fence, COM, GUI or OS input.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[2]
def sha(path:Path)->str:return hashlib.sha256(path.read_bytes()).hexdigest()
def main()->int:
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler',default='clang++')
    parser.add_argument('--sanitize',action='store_true')
    parser.add_argument('--no-mutations',action='store_true')
    args=parser.parse_args()
    scratch=Path(tempfile.mkdtemp(prefix='native-text-collection-',dir=ROOT/'.tmp'))
    names=['src/ui/retained/'+name for name in ['NativeTextEditor.cpp','NativeTextEditor.h',
           'NativeTextDocument.cpp','NativeTextDocument.h','TextEdit.cpp','TextEdit.h',
           'TextInput.cpp','TextInput.h','TextInputBroker.h']]
    names+=['tools/tests/native/UiNativeTextEditorTest.cpp','tools/tests/native/UiNativeTextCollectionTest.cpp',
            'tools/tests/ui_native_text_editor.py','tools/tests/ui_native_text_collection.py']
    source=ROOT/names[0];raw=source.read_bytes();original=raw.decode('utf-8').replace('\r\n','\n')
    working=scratch/'NativeTextEditor.cpp';working.write_bytes(raw)
    command=[args.compiler,'-std=c++20','-Wall','-Wextra','-Werror','-I',str(ROOT/'src'),'-I',str(ROOT/'src/ui/retained'),
             str(working),str(ROOT/names[2]),str(ROOT/names[4]),str(ROOT/names[6]),str(ROOT/names[10]),'-o',str(scratch/'test.exe')]
    if args.sanitize:command[1:1]=['-fsanitize=address,undefined','-fno-omit-frame-pointer','-no-pie']
    env=os.environ.copy();env['TEMP']=env['TMP']=env['TMPDIR']=str(scratch)
    def run(argv:list[str],name:str)->subprocess.CompletedProcess[str]:
        result=subprocess.run(argv,cwd=ROOT,env=env,text=True,encoding='utf-8',errors='replace',stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
        (scratch/name).write_text(result.stdout,encoding='utf-8');return result
    def compile_case(argv:list[str],name:str)->None:
        result=run(argv,name)
        if result.returncode:raise RuntimeError(result.stdout)
    legacy_command=[str(ROOT/names[9]) if part==str(ROOT/names[10]) else part for part in command]
    compile_case(legacy_command,'legacy-compile.log');legacy=run([str(scratch/'test.exe')],'legacy-run.log')
    if legacy.returncode:raise RuntimeError(legacy.stdout)
    compile_case(command,'compile.log');baseline=run([str(scratch/'test.exe')],'run.log')
    if baseline.returncode:raise RuntimeError(baseline.stdout)
    mutations={
        'collection-scope-admission-removed':[('if(impl->collectionMode && (!impl->barrier.collectionOpen','if(false && impl->collectionMode && (!impl->barrier.collectionOpen')],
        'wrong-native-dispatch-accepted':[('tx.nativeDispatch!=impl->barrier.collection.dispatch','false')],
        'declared-last-sequence-ignored':[('tx.sequence>impl->barrier.collection.lastTransactionSequence','false')],
        'missing-offers-complete':[('impl->barrier.sequence!=collection.lastTransactionSequence','false')],
        'wrong-completion-descriptor-accepted':[('collection!=impl->barrier.collection','false')],
        'dispatch-watermark-reused':[('collection.dispatch<=impl->barrier.collection.dispatch','false')],
        'fence-watermark-reused':[('collection.fenceSequence<=impl->barrier.collection.fenceSequence','false')],
        'collection-count-unbounded':[('collection.lastTransactionSequence-impl->barrier.sequence>32','false')],
        'collection-can-overlap':[('if(impl->barrier.collectionOpen || impl->awaiting','if(false || impl->awaiting')],
        'end-blocks-later-lock':[('candidate->awaiting=!impl->collectionMode && presentation.compositions.empty();','candidate->awaiting=presentation.compositions.empty();')],
        'unclassified-lock-adopts-stable-draft':[('if(impl->collectionMode || tx.classification==NativeTextClassification::CompositionRelated)','if(tx.classification==NativeTextClassification::CompositionRelated)')],
        'empty-open-collection-prepares-number':[('if(impl->barrier.collectionOpen || impl->barrier.group || impl->awaiting','if(impl->barrier.group || impl->awaiting')],
        'completed-empty-ranges-never-settle':[('candidate->awaiting=candidate->barrier.group && candidate->presentation.compositions.empty();','candidate->awaiting=false;')],
        'completed-live-ranges-await-settlement':[('candidate->awaiting=candidate->barrier.group && candidate->presentation.compositions.empty();','candidate->awaiting=candidate->barrier.group!=0;')],
        'protocol-reuses-stale-clone':[('expected!=impl->barrier ||','(void(expected),false) ||'),('*candidate.impl->cloneOrigin!=expected','false')],
        'abandon-leaves-collection-open':[('impl->barrier.group=0;impl->barrier.collectionOpen=false;return true;','impl->barrier.group=0;return true;')],
    }
    outcomes={}
    if not args.no_mutations:
        for name,replacements in mutations.items():
            changed=original
            for old,new in replacements:
                if old not in changed:raise RuntimeError('Missing mutation anchor: '+name)
                changed=changed.replace(old,new)
            working.write_text(changed,encoding='utf-8')
            try:
                compile_case(command,name+'-compile.log');result=run([str(scratch/'test.exe')],name+'-run.log')
                if not result.returncode:raise RuntimeError('Invalid collection mutation survived: '+name)
                outcomes[name]={'compiled':True,'rejected':True,'exit_code':result.returncode}
            finally:working.write_bytes(raw)
    record={'status':'passed','legacy_checks':int(re.search(r'PASS (\d+) checks',legacy.stdout)[1]),
            'checks':int(re.search(r'PASS (\d+) checks',baseline.stdout)[1]),'mutations':outcomes,
            'command':command,'legacy_command':legacy_command,'sanitizers':args.sanitize,
            'sources':{name:sha(ROOT/name) for name in names},
            'logs':{p.name:sha(p) for p in sorted(scratch.glob('*.log'))},
            'scope':'Actual native producer, editor collection replay and history. Caller-supplied collection descriptors are not native fence proof or physical/direct-input authority. No COM, OS, native pump, GUI, renderer or setting application.'}
    output=scratch/'result.json';output.write_text(json.dumps(record,indent=2)+'\n',encoding='utf-8')
    print(legacy.stdout.strip()+' compatibility');print(baseline.stdout.strip()+' collection');print(output);return 0
if __name__=='__main__':raise SystemExit(main())
