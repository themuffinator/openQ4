#!/usr/bin/env python3
"""Actual portable native/editor reconciliation, producer and history tests.

No COM, native provider, GUI, renderer, input or setting application. Compiles the
real NativeTextDocument producer alongside the editor consumer; no copied fake
transaction implementation. Optional Linux ASan/UBSan and compiled mutations.
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
def sha(path: Path)->str:return hashlib.sha256(path.read_bytes()).hexdigest()

def main()->int:
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler',default='clang++')
    parser.add_argument('--sanitize',action='store_true')
    parser.add_argument('--no-mutations',action='store_true')
    args=parser.parse_args()
    scratch=Path(tempfile.mkdtemp(prefix='native-text-editor-',dir=ROOT/'.tmp'))
    names=['src/ui/retained/NativeTextEditor.cpp','src/ui/retained/NativeTextEditor.h',
           'src/ui/retained/NativeTextDocument.cpp','src/ui/retained/NativeTextDocument.h',
           'src/ui/retained/TextEdit.cpp','src/ui/retained/TextEdit.h',
           'src/ui/retained/TextInput.cpp','src/ui/retained/TextInput.h','src/ui/retained/TextInputBroker.h',
           'tools/tests/native/UiNativeTextEditorTest.cpp','tools/tests/ui_native_text_editor.py']
    files=[ROOT/name for name in names]
    raw=files[0].read_bytes();original=raw.decode('utf-8').replace('\r\n','\n')
    working=scratch/'NativeTextEditor.cpp';working.write_bytes(raw)
    command=[args.compiler,'-std=c++20','-Wall','-Wextra','-Werror','-I',str(ROOT/'src'),
             '-I',str(ROOT/'src/ui/retained'),str(working),str(files[2]),str(files[4]),str(files[6]),str(files[9]),
             '-o',str(scratch/'test.exe')]
    if args.sanitize:command[1:1]=['-fsanitize=address,undefined','-fno-omit-frame-pointer','-no-pie']
    env=os.environ.copy();env['TEMP']=env['TMP']=env['TMPDIR']=str(scratch)
    def run(argv:list[str],log:str)->subprocess.CompletedProcess[str]:
        result=subprocess.run(argv,cwd=ROOT,env=env,text=True,encoding='utf-8',errors='replace',
                              stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
        (scratch/log).write_text(result.stdout,encoding='utf-8');return result
    def compile_case(log:str)->None:
        result=run(command,log)
        if result.returncode:raise RuntimeError(result.stdout)
    compile_case('compile.log');baseline=run([str(scratch/'test.exe')],'run.log')
    if baseline.returncode:raise RuntimeError(baseline.stdout)
    mutations={
        'owner-lease-check-removed':[('active && expected==barrier.editor','active && (void(expected),true)')],
        'native-sequence-not-checked':[('tx.sequence!=impl->barrier.sequence+1','false')],
        'snapshot-authority-trusted':[(' || tx.after!=candidate','')],
        'classification-not-checked':[('tx.classification!=(related?NativeTextClassification::CompositionRelated:NativeTextClassification::Unclassified)',
                                       '(void(related),false)')],
        'surrogate-middle-accepted':[('at==map.end() || at->acp!=acp','at==map.end()')],
        'selection-direction-lost':[('candidate.anchor=first;candidate.caret=last;',
                                    'candidate.anchor=std::min(first,last);candidate.caret=std::max(first,last);')],
        'composition-start-gravity-wrong':[('range.first=Shift(range.first,first,last,op.text.size(),false)',
                                           'range.first=Shift(range.first,first,last,op.text.size(),true)')],
        'intermediate-field-validation-skipped':[('if(!Field(candidate,policy,error)) return false;',
                                                 'if(false && !Field(candidate,policy,error)) return false;')],
        'end-adopts-draft-early':[('candidate->awaiting=!impl->collectionMode && presentation.compositions.empty();',
                                  'candidate->awaiting=!impl->collectionMode && presentation.compositions.empty(); if(candidate->awaiting && !Adopt(candidate->stable,presentation,error)) return false;')],
        'settle-advances-native-shadow':[('candidate->barrier.group=0;candidate->awaiting=false;candidate->barrier.editor.revision=revision;',
                                         'candidate->barrier.group=0;candidate->awaiting=false;candidate->barrier.editor.revision=revision; ++candidate->barrier.shadowRevision;')],
        'unchanged-group-pushes-undo':[('if(snapshot.text!=stable.State().text)', 'if(true)')],
        'history-byte-bound-ignored':[('bytes>TextEditBuffer::MaxHistoryTextBytes','(void(bytes),false)')],
        'publication-stale-live-barrier':[('expected!=impl->barrier ||','false ||')],
        'publication-stale-clone-origin':[('*candidate.impl->cloneOrigin!=expected','false')],
        'stale-settle-barrier-accepted':[('active && expected==barrier ? true:', 'active && (void(expected),true) ? true:')],
        'changed-receipt-reuses-revision':[('(!changed || revision>barrier.editor.revision)', '(void(changed),true)')],
        'abandon-keeps-native-composition-text':[('impl->presentation.text.swap(impl->stablePresentation.text);', '')],
        'new-draft-abandon-cache-stale':[('candidate->stablePresentation=Snapshot(candidate->stable.State());','')],
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
                compile_case(name+'-compile.log');result=run([str(scratch/'test.exe')],name+'-run.log')
                if not result.returncode:raise RuntimeError('Invalid production mutation accepted: '+name)
                outcomes[name]={'compiled':True,'rejected':True,'exit_code':result.returncode}
            finally:working.write_bytes(raw)
    record={'status':'passed','checks':int(re.search(r'PASS (\d+) checks',baseline.stdout)[1]),
            'command':command,'mutations':outcomes,'sanitizers':args.sanitize,
            'sources':{name:sha(ROOT/name) for name in names},
            'logs':{path.name:sha(path) for path in sorted(scratch.glob('*.log'))},
            'scope':'Actual native producer/editor/history/UTF-8 methods. No native COM, TSF, event fence, GUI, rendering, shaping, input or accepted setting application.'}
    output=scratch/'result.json';output.write_text(json.dumps(record,indent=2)+'\n',encoding='utf-8')
    print(baseline.stdout.strip());print(output);return 0
if __name__=='__main__':raise SystemExit(main())
