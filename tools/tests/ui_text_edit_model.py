#!/usr/bin/env python3
"""Compile actual text-edit models and reject behavioral source mutations.

No game, native IME, clipboard, input injection or renderer is used. This checks
the reusable buffer/number semantics, not a live text-field product.
"""
from __future__ import annotations
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def changed(source: str, before: str, after: str) -> str:
    if source.count(before) != 1:
        raise AssertionError(f"mutation anchor must occur exactly once: {before}")
    return source.replace(before, after, 1)


def main() -> None:
    paths = [ROOT / name for name in (
        'src/ui/retained/TextEdit.cpp', 'src/ui/retained/TextEdit.h',
        'src/ui/retained/TextInput.cpp', 'src/ui/retained/TextInput.h',
        'tools/tests/native/UiTextEditTest.cpp')]
    snapshots = {p.relative_to(ROOT).as_posix(): p.read_bytes() for p in paths}
    source = snapshots['src/ui/retained/TextEdit.cpp'].decode('utf-8').replace('\r\n','\n')
    variants = {
        'production': source,
        'mutant-unicode-line-policy': changed(source,'if (!policy.multiline && (text.find(', 'if (false && (text.find('),
        'mutant-exponent-format': changed(source, 'policy.exponent ? std::chars_format::general : std::chars_format::fixed', 'std::chars_format::general'),
        'mutant-empty-commit-delete': changed(source, 'event.kind == TextInputKind::CancelComposition || event.text.empty()', 'event.kind == TextInputKind::CancelComposition'),
        'mutant-unbounded-history': changed(source, 'while (HistoryEntries() > MaxHistoryEntries || HistoryTextBytes() > MaxHistoryTextBytes)', 'while (false)'),
        'mutant-quantized-number': changed(source, 'value = candidate; return TextNumberStatus::Valid;', 'value = std::round(candidate); return TextNumberStatus::Valid;'),
        'mutant-reset-composition-loss': changed(source, 'if (!ValidPolicy(candidatePolicy,error)', 'composition.reset();\n\tif (!ValidPolicy(candidatePolicy,error)'),
        'mutant-no-scalar-selection-check': changed(source,
            'if (!Boundary(state.text,anchor) || !Boundary(state.text,caret)) return Fail(error, "Selection splits a scalar or exceeds the buffer");', ''),
        'mutant-range-undo-selection': changed(source, 'if (candidate.text != state.text) { undo.push_back(state); redo.clear(); }',
            'if (candidate.text != state.text) { state.anchor=anchor; state.caret=caret; undo.push_back(state); redo.clear(); }'),
    }
    compiler = next((found for name in ('clang++','g++','c++') if (found := shutil.which(name))), None)
    if not compiler:
        raise RuntimeError('C++20 compiler required')
    (ROOT/'.tmp').mkdir(exist_ok=True)
    output = Path(tempfile.mkdtemp(prefix='ui-text-edit-',dir=ROOT/'.tmp'))
    env = dict(os.environ, TEMP=str(output), TMP=str(output), TMPDIR=str(output))
    results = {}
    for name, text in variants.items():
        cpp, exe = output/f'{name}.cpp', output/f'{name}.exe'
        cpp.write_text(text,encoding='utf-8')
        command = [compiler,'-std=c++20','-I'+str(ROOT),'-I'+str(ROOT/'src/ui/retained'),
                   str(ROOT/'tools/tests/native/UiTextEditTest.cpp'),
                   str(ROOT/'src/ui/retained/TextInput.cpp'),str(cpp),'-o',str(exe)]
        build = subprocess.run(command,env=env,capture_output=True,text=True)
        build_log = output/f'{name}-compile.log'
        build_log.write_text(build.stdout+build.stderr,encoding='utf-8')
        if build.returncode:
            raise AssertionError(f'{name} compile failed; {build_log}')
        run = subprocess.run([str(exe)],env=env,capture_output=True,text=True)
        run_log = output/f'{name}-run.log'
        run_log.write_text(run.stdout+run.stderr,encoding='utf-8')
        if (run.returncode == 0) != (name == 'production'):
            raise AssertionError(f'{name} unexpected result {run.returncode}; {run_log}')
        results[name] = {'compile_exit':build.returncode,'run_exit':run.returncode,'output':run.stdout+run.stderr,
            'artifacts':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in (cpp,exe,build_log,run_log)}}
    for name, original in snapshots.items():
        if (ROOT/name).read_bytes() != original:
            raise AssertionError(f'Source changed during test: {name}')
    report = {'passed':True,'compiler':compiler,'test_sha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        'sources':{name:hashlib.sha256(data).hexdigest() for name,data in snapshots.items()},'results':results,
        'limitations':'Actual pure buffer/numeric model only; no live editor, shaping, clipboard, input ownership or native IME qualification.'}
    result = output/'result.json'
    result.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(results['production']['output'].strip())
    print(f'Rejected {len(results)-1} compiled source mutations; evidence: {result}')


if __name__ == '__main__':
    main()
