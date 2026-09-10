#!/usr/bin/env python3
"""Run actual NativeTextDocument + UTF-8 methods and compiled behavioral mutants.

Pure model only: no COM, native messages, engine build, clipboard or input calls.
All compiler outputs and source mutations stay in a fresh repository .tmp path.
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

ROOT = Path(__file__).resolve().parents[2]

def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()

def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', default='clang++')
    parser.add_argument('--sanitize', action='store_true')
    parser.add_argument('--no-mutations', action='store_true')
    args = parser.parse_args()
    scratch = Path(tempfile.mkdtemp(prefix='native-text-document-', dir=ROOT / '.tmp'))
    sources = [ROOT / 'src/ui/retained/NativeTextDocument.cpp', ROOT / 'src/ui/retained/NativeTextDocument.h',
               ROOT / 'src/ui/retained/TextInput.cpp', ROOT / 'src/ui/retained/TextInput.h',
               ROOT / 'tools/tests/native/UiNativeTextDocumentTest.cpp', Path(__file__)]
    raw = sources[0].read_bytes()
    decoded = raw.decode('utf-8').replace('\r\n', '\n')
    working = scratch / 'NativeTextDocument.cpp'
    working.write_bytes(raw)
    command = [args.compiler, '-std=c++20', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'src'),
               '-I', str(ROOT / 'src/ui/retained'), str(working), str(sources[2]), str(sources[4]),
               '-o', str(scratch / 'test.exe')]
    if args.sanitize:
        command[1:1] = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-no-pie']
    env = os.environ.copy()
    env['TEMP'] = env['TMP'] = env['TMPDIR'] = str(scratch)
    def run(argv: list[str], log: str) -> subprocess.CompletedProcess[str]:
        result = subprocess.run(argv, cwd=ROOT, env=env, text=True, encoding='utf-8', errors='replace',
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        (scratch / log).write_text(result.stdout, encoding='utf-8')
        return result
    compiled = run(command, 'compile.log')
    if compiled.returncode:
        raise RuntimeError(compiled.stdout)
    tested = run([str(scratch / 'test.exe')], 'run.log')
    if tested.returncode:
        raise RuntimeError(tested.stdout)
    mutations = {
        'metadata-acked-observation-reused': [('observed.acknowledgedSequence!=impl->transactionSequence-impl->pending.size()', 'false')],
        'metadata-stale-engine-observation': [('observed.engineRevision!=impl->engineRevision ||', 'false ||')],
        'metadata-stale-shadow-observation': [('observed.shadowRevision!=impl->shadowRevision ||', 'false ||')],
        'metadata-stale-sequence-observation': [('observed.transactionSequence!=impl->transactionSequence ||', 'false ||')],
        'metadata-marked-document-change': [('transaction.documentChanged=false;', 'transaction.documentChanged=true;')],
        'metadata-loses-classification': [('transaction.classification=NativeTextClassification::CompositionRelated;', 'transaction.classification=NativeTextClassification::Unclassified;')],
        'metadata-dispatch-lost': [('transaction.nativeDispatch=observed.dispatch;', 'transaction.nativeDispatch=0;')],
        'metadata-text-payload-admitted': [('|| !operation.text.empty() ||', '|| false ||')],
        'metadata-capture-during-read': [('if (impl->frame || impl->deferredWrite || !dispatch || impl->pending.size()>32', 'if (impl->deferredWrite || !dispatch || impl->pending.size()>32')],
        'metadata-capture-invents-lock-serial': [('out=candidate;error.clear();return true;\n}\nbool NativeTextDocument::PublishCompositionObservation', '++impl->lockSequence;out=candidate;error.clear();return true;\n}\nbool NativeTextDocument::PublishCompositionObservation')],
        'metadata-ended-identities-unbudgeted': [('if (!impl->Retained(candidate)) return Fail(error,"Native retained composition budget exhausted");', 'if (false) return Fail(error,"Native retained composition budget exhausted");')],
        'pending-query-stale-engine': [('expectedEngine!=impl->engineRevision)', '(void(expectedEngine),false))')],
        'pending-query-stale-ack': [('expectedAcknowledged!=acknowledged ||', '(void(expectedAcknowledged),false) ||')],
        'pending-query-stale-shadow': [('expectedAcknowledgedShadow!=acknowledgedShadow)', '(void(expectedAcknowledgedShadow),false))')],
        'pending-query-mixed-dispatch': [('transaction.nativeDispatch!=dispatch ||', 'false ||')],
        'pending-query-empty-zero-dispatch': [('impl->deferredWrite || !dispatch || count>32', 'impl->deferredWrite || count>32')],
        'pending-query-during-read': [('if (impl->frame || impl->deferredWrite || !dispatch || count>32', 'if (impl->deferredWrite || !dispatch || count>32')],
        'pending-query-during-deferred-write': [('impl->frame || impl->deferredWrite || !dispatch || count>32', 'impl->frame || !dispatch || count>32')],
        'pending-query-wrong-count': [('impl->transactionSequence,static_cast<std::uint32_t>(count)', 'impl->transactionSequence,static_cast<std::uint32_t>(count+1)')],
        'surrogate-split-accepted': [('found==map.end() || found->acp!=acp', 'found==map.end()')],
        'partial-candidate-published': [('if (impl->frame->poisoned) { impl->frame.reset();',
                                        'if (impl->frame->poisoned) { impl->state=std::move(impl->frame->working); impl->frame.reset();')],
        'owner-check-removed': [('!opened || retired || expected!=identity', '!opened || retired || (void(expected),false)')],
        'stale-callback-accepted': [('!frame || !expected.serial || expected!=frame->scope', '!frame || !expected.serial')],
        'changed-ack-revision-reused': [('(impl->pending.front().documentChanged && accepted==expected)', 'false')],
        'wrong-front-sequence-accepted': [('impl->pending.front().sequence!=transaction ||', '(void(transaction),false) ||')],
        'early-insertion-unclassified': [('frame.related ? NativeTextClassification::CompositionRelated',
                                          'false ? NativeTextClassification::CompositionRelated')],
        'ended-identities-unbudgeted': [('for (const auto& operation:operations) if (operation.composition) ids.insert(operation.composition);',
                                        '(void)operations;')],
        'pending-sync-overwrite': [('impl->frame || impl->deferredWrite || !impl->pending.empty() || !impl->state.compositions.empty()',
                                    'impl->frame || impl->deferredWrite || !impl->state.compositions.empty()')],
        'retired-owner-reopened': [('!opened || retired || expected!=identity', '!opened || expected!=identity')],
        'upgrade-before-callback-exit': [('if (impl->frame || !impl->deferredWrite)', 'if (!impl->deferredWrite)')],
        'queued-byte-limit-removed': [('std::size_t cost;\n\tif (!Cost(transaction,impl->limits.pendingBytes-impl->pendingBytes,cost))',
                                      'std::size_t cost=0;\n\tif (false && !Cost(transaction,impl->limits.pendingBytes-impl->pendingBytes,cost))')],
    }
    mutation_results = {}
    if not args.no_mutations:
        for name, replacements in mutations.items():
            changed = decoded
            for old, new in replacements:
                if old not in changed:
                    raise RuntimeError(f'Mutation anchor absent: {name}')
                changed = changed.replace(old, new)
            working.write_text(changed, encoding='utf-8')
            try:
                compiled = run(command, name + '-compile.log')
                if compiled.returncode:
                    raise RuntimeError(f'Mutation failed to compile: {name}\n{compiled.stdout}')
                result = run([str(scratch / 'test.exe')], name + '-run.log')
                if result.returncode == 0:
                    raise RuntimeError(f'Invalid mutation accepted: {name}')
                mutation_results[name] = {'compiled': True, 'rejected': True, 'exit_code': result.returncode}
            finally:
                working.write_bytes(raw)
    record = {'status': 'passed', 'checks': int(re.search(r'PASS (\d+) checks', tested.stdout)[1]),
              'command': command, 'mutations': mutation_results, 'sanitizers': args.sanitize,
              'sources': {str(path.relative_to(ROOT)): sha(path) for path in sources},
              'scope': 'Actual pure document/UTF-8 methods. No COM callback, installed IME, native pump, GUI, shaping or engine transport qualification.'}
    (scratch / 'result.json').write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
    print(tested.stdout.strip())
    print(scratch / 'result.json')
    return 0

if __name__ == '__main__':
    raise SystemExit(main())
