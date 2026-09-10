#!/usr/bin/env python3
"""Actual Windows SDK COM boundary tests, without COM activation or native input.

Requires a Windows C++20 compiler and installed Windows SDK. Non-Windows hosts
report unsupported; that is not native/IME qualification. NativeTextDocument and
TextInput are production dependencies, not doubles. Only SDK callback objects in
the test are counted stand-ins. No builddir, stage, GUI, native window or pump.
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
    parser.add_argument('--no-mutations', action='store_true')
    args = parser.parse_args()
    if os.name != 'nt':
        raise SystemExit('Unsupported: actual Windows SDK compilation is required.')
    scratch = Path(tempfile.mkdtemp(prefix='windows-text-store-', dir=ROOT / '.tmp'))
    paths = [ROOT / name for name in (
        'src/sys/sdl3/WindowsTextStore.cpp', 'src/sys/sdl3/WindowsTextStore.h',
        'src/ui/retained/NativeTextDocument.cpp', 'src/ui/retained/NativeTextDocument.h',
        'src/ui/retained/TextInput.cpp', 'src/ui/retained/TextInput.h',
        'tools/tests/native/WindowsTextStoreTest.cpp', 'tools/tests/windows_text_store.py')]
    raw = paths[0].read_bytes()
    initial_hashes = {str(p.relative_to(ROOT)): sha(p) for p in paths}
    original = raw.decode('utf-8').replace('\r\n', '\n')
    working = scratch / 'WindowsTextStore.cpp'
    working.write_bytes(raw)
    command = [args.compiler, '-std=c++20', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'src'),
               '-I', str(ROOT / 'src/sys/sdl3'), str(working), str(paths[2]), str(paths[4]), str(paths[6]),
               '-o', str(scratch / 'test.exe')]
    env = os.environ.copy()
    env['TEMP'] = env['TMP'] = env['TMPDIR'] = str(scratch)
    def run(argv: list[str], name: str) -> subprocess.CompletedProcess[str]:
        result = subprocess.run(argv, cwd=ROOT, env=env, text=True, encoding='utf-8', errors='replace',
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        (scratch / name).write_text(result.stdout, encoding='utf-8')
        return result
    def compile_test(name: str) -> None:
        result = run(command, name)
        if result.returncode:
            raise RuntimeError(result.stdout)
    compile_test('compile.log')
    baseline = run([str(scratch / 'test.exe')], 'run.log')
    if baseline.returncode:
        raise RuntimeError(baseline.stdout)
    mutations = {
        'idle-begin-declined': [('if(impl->notifying || impl->upgrading || impl->notificationWrite ||', 'if(!impl->scope || impl->notifying || impl->upgrading || impl->notificationWrite ||')],
        'idle-end-queries-terminated-range': [('// Terminated views may no longer provide GetRange.', 'LONG first,last;if(FAILED(impl->Extent(source,nullptr,first,last))) return E_FAIL;\n\t// Terminated views may no longer provide GetRange.')],
        'idle-metadata-callback-quarantine-removed': [('CounterScope callback(impl->compositionCallback);', '/* callback quarantine omitted */')],
        'idle-metadata-without-observed-dispatch': [('impl->identity,impl->dispatch,observed,e)', 'impl->identity,77,observed,e)')],
        'idle-metadata-publication-failure-ignored': [('if(!changed) {impl->Fault();return E_FAIL;}', 'if(!changed) {return S_OK;}')],
        'idle-metadata-during-application-notice': [('if(impl->notifying || impl->upgrading || impl->notificationWrite ||', 'if(impl->upgrading || impl->notificationWrite ||')],
        'pending-query-during-foreign-callback': [('return scope || foreign || compositionCallback || nativeCallback || notifying || upgrading || notificationWrite;', 'return scope || compositionCallback || nativeCallback || notifying || upgrading || notificationWrite;')],
        'pending-query-during-notification': [('return scope || foreign || compositionCallback || nativeCallback || notifying || upgrading || notificationWrite;', 'return scope || foreign || compositionCallback || nativeCallback || upgrading || notificationWrite;')],
        'pending-query-lock-HRESULT-lost': [('return scope || foreign || compositionCallback || nativeCallback || notifying || upgrading || notificationWrite;', 'return foreign || compositionCallback || notifying || upgrading || notificationWrite;')],
        'pending-query-engine-not-forwarded': [('QueryPendingCollection(id,expectedEngine,expectedAcknowledged,expectedAcknowledgedShadow,dispatch,out,error)', 'QueryPendingCollection(id,(void(expectedEngine),impl->document.EngineRevision()),expectedAcknowledged,expectedAcknowledgedShadow,dispatch,out,error)')],
        'pending-query-dispatch-not-forwarded': [('QueryPendingCollection(id,expectedEngine,expectedAcknowledged,expectedAcknowledgedShadow,dispatch,out,error)', 'QueryPendingCollection(id,expectedEngine,expectedAcknowledged,expectedAcknowledgedShadow,(void(dispatch),77),out,error)')],
        'application-notifications-omitted': [('return Notify(mask,&change);', '(void)change;return S_OK;')],
        'layout-notification-omitted': [('return Notify(TS_AS_LAYOUT_CHANGE,nullptr);', 'return S_OK;')],
        'application-stale-revision-adopted': [('document.SyncEngine(id,expectedEngine,expectedShadow,revision,candidate,anchor,caret,e)',
                                             'document.SyncEngine(id,(void(expectedEngine),impl->document.EngineRevision()),expectedShadow,revision,candidate,anchor,caret,e)')],
        'application-write-granted-mid-notice': [('if(impl->notifying && access==NativeTextAccess::ReadWrite)',
                                                 'if(false && impl->notifying && access==NativeTextAccess::ReadWrite)')],
        'application-deferred-write-lost': [('if(impl->notificationWrite) {', 'if(false && impl->notificationWrite) {')],
        'application-notice-failure-ignored': [('if(FAILED(hr) || !Healthy() || impl->sink!=sink.p)',
                                              'if(!Healthy() || impl->sink!=sink.p)')],
        'application-acp-reports-bytes': [('static_cast<LONG>(ToWide(candidate).size())', 'static_cast<LONG>(candidate.size())')],
        'application-selection-notice-omitted': [('if(before.anchor!=anchor || before.caret!=caret) mask|=TS_AS_SEL_CHANGE;',
                                               '/* selection notification omitted */')],
        'write-lock-gate-removed': [('(write && scope->access!=NativeTextAccess::ReadWrite)', '(void(write),false)')],
        'post-callback-publication-refusal-ignored': [('if(!impl->document.FinishLock(scope,impl->dispatch,published,e))',
                                                    'if(!impl->document.FinishLock(scope,impl->dispatch,published,e) && false)')],
        'deferred-write-never-granted': [('if(impl->upgrading && Healthy())', 'if(false && impl->upgrading && Healthy())')],
        'sink-replacement-uses-old-upgrade': [('impl->sink!=sink.p || !impl->document.GrantDeferredWrite', '!impl->document.GrantDeferredWrite')],
        'composition-context-affinity-removed': [('rangeIdentity.p!=context', '(void(rangeIdentity),false)')],
        'stale-candidate-layout-accepted': [('layout.reset(); *out={first,last,end};', '*out={first,last,end};'),
                                          [('if(!Snapshot(snapshot) || snapshot.text!=layout->text)', 'if(!Snapshot(snapshot))')][0]],
        'ack-revision-not-forwarded': [('document.Acknowledge(id,tx,shadow,expected,accepted,e)',
                                       'document.Acknowledge(id,tx,shadow,expected,(void(accepted),expected+1),e)')],
        'query-only-inserts': [('if(flags&TS_IAS_QUERYONLY) {*first=', 'if(false && (flags&TS_IAS_QUERYONLY)) {*first=')],
        'begin-reuses-ended-identity': [('for(const auto& c:impl->compositions) if(c.identity==identity.p) return S_OK;',
                                        'for(const auto& c:impl->compositions) if(c.identity==identity.p && !c.ended) return S_OK;')],
        'candidate-context-range-not-updated': [('if(supplied) {supplied->AddRef();range.p=supplied;} else hr=composition->GetRange(&range.p);',
                                               '(void)supplied; hr=composition->GetRange(&range.p);')],
        'scope-less-write-admitted': [('if(access==NativeTextAccess::ReadWrite && !impl->collection)', 'if(false && access==NativeTextAccess::ReadWrite && !impl->collection)')],
        'scope-less-metadata-admitted': [('if(!impl->collection) return S_OK;', '/* last dispatch incorrectly acts as authority */')],
        'scope-close-reuses-wrong-identity': [('scope!=*impl->collection) return E_UNEXPECTED;', '(void(scope),false)) return E_UNEXPECTED;')],
        'scope-abort-reuses-wrong-identity': [('scope!=*impl->collection) return E_INVALIDARG;', '(void(scope),false)) return E_INVALIDARG;')],
        'scope-close-keeps-authority': [('impl->collection.reset();out=candidate;return S_OK;', 'out=candidate;return S_OK;')],
        'scope-dispatch-reused': [('dispatch<=impl->dispatchHigh ||', 'false ||')],
        'scope-open-stale-engine': [('QueryPendingCollection(id,engine,acknowledged,shadow,dispatch,pending,error)', 'QueryPendingCollection(id,(void(engine),impl->document.EngineRevision()),acknowledged,shadow,dispatch,pending,error)')],
        'scope-open-with-pending-offers': [
            ('|| pending.count) return E_INVALIDARG;', ') return E_INVALIDARG;'),
            ('QueryPendingCollection(id,engine,acknowledged,shadow,dispatch,pending,error)',
             'QueryPendingCollection(id,engine,acknowledged,shadow,impl->document.PendingCount()?impl->dispatchHigh:dispatch,pending,error)')],
        'scope-activity-not-recorded': [('++admittedCallbacks;return true;', 'return true;')],
        'scope-refused-quota-closes-empty': [('if(impl->renewalRequired) {impl->Fault();return E_FAIL;}', '/* declined quota incorrectly treated as empty success */')],
        'scope-pump-allows-app-mutation': [('return !collection || collection->kind==WindowsTextCollectionKind::Lifecycle;', 'return true;')],
        'scope-renewal-ignores-pending': [('&& !candidate.pending && !candidate.liveCompositions;', '&& !candidate.liveCompositions;')],
        'scope-renewal-ignores-live-composition': [('&& !candidate.pending && !candidate.liveCompositions;', '&& !candidate.pending;')],
        'scope-notice-keeps-stale-acked-shadow': [('impl->acknowledgedShadow=impl->document.ShadowRevision();', '/* application-origin acknowledged shadow lost */')],
        'scope-reference-reentry-allowed': [(' || (impl->nativeCallback && !impl->scope)', '')],
        'scope-retired-reference-still-granted': [('if(!Healthy() || impl->sink!=sink.p) {impl->Fault();return TF_E_DISCONNECTED;}', '/* foreign AddRef retirement ignored */')],
    }
    outcomes = {}
    if not args.no_mutations:
        for name, replacements in mutations.items():
            changed = original
            for old, new in replacements:
                if old not in changed:
                    raise RuntimeError(f'Missing mutation anchor: {name}')
                changed = changed.replace(old, new)
            working.write_text(changed, encoding='utf-8')
            try:
                compile_test(name + '-compile.log')
                result = run([str(scratch / 'test.exe')], name + '-run.log')
                if not result.returncode:
                    raise RuntimeError(f'Production mutation accepted: {name}')
                outcomes[name] = {'compiled': True, 'rejected': True, 'exit_code': result.returncode}
            finally:
                working.write_bytes(raw)
    # Ask the compiler which real headers were used; retain exact SDK paths and
    # hashes without exposing general environment or assuming a particular SDK.
    dependencies = run([args.compiler, '-std=c++20', '-MM', '-I', str(ROOT / 'src'),
                        str(paths[6])], 'dependencies.log')
    if dependencies.returncode:
        raise RuntimeError(dependencies.stdout)
    # System headers are intentionally excluded by -MM; -H identifies their
    # resolved paths. It emits preprocessing only and performs no native calls.
    includes = run([args.compiler, '-std=c++20', '-E', '-H', '-I', str(ROOT / 'src'),
                    str(paths[6]), '-o', os.devnull], 'sdk-includes.log')
    if includes.returncode:
        raise RuntimeError(includes.stdout)
    sdk = {}
    for line in includes.stdout.splitlines():
        match = re.match(r'^\.+ (.*(?:textstor|msctf|olectl)\.h)$', line.strip(), re.I)
        if match:
            file = Path(match[1])
            if file.is_file():
                sdk[str(file)] = sha(file)
    if len(sdk) != 3:
        raise RuntimeError('Actual SDK textstor/msctf/olectl headers were not identified.')
    if initial_hashes != {str(p.relative_to(ROOT)): sha(p) for p in paths}:
        raise RuntimeError('Source or test changed during the run; preserve logs, but do not publish mixed-source evidence.')
    record = {'status': 'passed', 'checks': int(re.search(r'PASS (\d+) checks', baseline.stdout)[1]),
              'command': command, 'mutations': outcomes, 'sdk_headers': sdk,
              'sources': {str(p.relative_to(ROOT)): sha(p) for p in paths},
              'scope': 'Actual SDK COM vtables and production store/model/UTF-8 methods; counted callback objects. No native COM activation, installed TIP, OS input, HWND calls, renderer, engine delivery or candidate-window qualification.',
              'logs': {p.name: sha(p) for p in sorted(scratch.glob('*.log'))}}
    (scratch / 'result.json').write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
    print(baseline.stdout.strip())
    print(scratch / 'result.json')
    return 0

if __name__ == '__main__':
    raise SystemExit(main())
