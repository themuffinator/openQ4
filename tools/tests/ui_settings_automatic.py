#!/usr/bin/env python3
"""Qualify real automatic/manual settings transactions and the shared controller.

Native production classes use counted host/device/persistence boundaries. No real
devices, durable file operations, game execution or input are performed here.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile

from filesystem_case_segments import function_body

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', default='clang++')
    parser.add_argument('--msvc-debug', action='store_true')
    parser.add_argument('--sanitizers', action='store_true')
    parser.add_argument('--no-mutations', action='store_true')
    options = parser.parse_args()
    msvc = Path(options.compiler).name.lower() in ('cl', 'cl.exe')
    if options.msvc_debug and not msvc:
        parser.error('--msvc-debug requires cl')
    if options.sanitizers and msvc:
        parser.error('--sanitizers requires Clang or GCC')
    app = ROOT / 'src/ui/application'
    tx = app / 'SettingsTransaction.cpp'
    controller = app / 'SettingsDisplayController.cpp'
    names = ('UiSettingsDisplayControllerTest', 'UiSettingsTransactionTest', 'UiSettingsExactValueTest')
    paths = [app / (name + ext) for name in ('SettingsTransaction', 'SettingsDisplayController') for ext in ('.h', '.cpp')]
    paths += [app / 'SettingsValue.h', ROOT / 'src/ui/retained/Document.cpp', ROOT / 'src/ui/retained/Document.h', ROOT / 'src/ui/retained/Vector.h']
    paths += [ROOT / 'tools/tests/native' / (name + '.cpp') for name in names]
    paths += [Path(__file__), ROOT / 'tools/tests/filesystem_case_segments.py']
    sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
    hashes = lambda: {str(p.relative_to(ROOT)): sha(p) for p in paths}
    (ROOT / '.tmp').mkdir(exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix='settings-automatic-', dir=ROOT / '.tmp'))
    env = dict(os.environ, TEMP=str(out), TMP=str(out), TMPDIR=str(out))
    record = dict(passed=False, sources_before=hashes(), runs=[], mutations=[], scope=__doc__)
    document = (ROOT / 'src/ui/retained/Document.cpp').read_text(encoding='utf-8')
    valid = out / 'validation.cpp'
    valid.write_text('#include "src/ui/retained/Document.h"\n#include <cmath>\nnamespace openq4::ui {\n' +
                     ''.join(function_body(document, signature) for signature in ('bool Utf8(', 'bool ValidStateValue(')) + '}\n', encoding='utf-8')

    def execute(label, command, expected_failure=False):
        completed = subprocess.run([str(x) for x in command], cwd=out, env=env, capture_output=True, text=True)
        log = out / (label + '.log')
        log.write_text(completed.stdout + completed.stderr, encoding='utf-8')
        row = dict(label=label, command=[str(x) for x in command], exit=completed.returncode, log=str(log), sha256=sha(log))
        record['mutations' if expected_failure else 'runs'].append(row)
        print(label, completed.returncode, (completed.stdout + completed.stderr)[-400:], flush=True)
        if (completed.returncode != 0) != expected_failure:
            raise RuntimeError('Unexpected result: ' + label)

    def build(label, test, transaction=tx, display=controller, mutant=False):
        exe = out / (label + ('.exe' if os.name == 'nt' else ''))
        sources = [transaction, valid, ROOT / 'tools/tests/native' / (test + '.cpp')]
        if test == names[0]:
            sources.append(display)
        if msvc:
            # Existing retained constructors intentionally share member names.
            flags = ['/nologo', '/std:c++20', '/EHsc', '/W4', '/WX', '/wd4458', '/MTd' if options.msvc_debug else '/MT',
                     '/I' + str(ROOT), '/I' + str(app), '/Fe:' + str(exe)]
        else:
            flags = ['-std=c++20', '-Wall', '-Wextra', '-Werror', '-O1', '-I' + str(ROOT), '-I' + str(app), '-o', str(exe)]
            if options.sanitizers:
                flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
        execute(label + '-compile', [options.compiler, *flags, *sources])
        execute(label + '-run', [exe], mutant)

    try:
        for name in names:
            build(name, name)
        if not options.no_mutations:
            tx_source = tx.read_text(encoding='utf-8')
            display_source = controller.read_text(encoding='utf-8')
            changes = [
                ('automatic-display-bypass', tx, 'if (completion == SettingsCompletion::Automatic &&\n\t\t!Invoke', 'if (false &&\n\t\t!Invoke', 1),
                ('automatic-execution-policy', tx, 'pending.completion == SettingsCompletion::Automatic &&\n\t\t!Invoke', 'false &&\n\t\t!Invoke', 1),
                ('automatic-acceptance-policy', tx, 'if (!Invoke([&] { return !host.NeedsConfirmation(pending.baseline,pending.target); },error))', 'if (false && !Invoke([&] { return !host.NeedsConfirmation(pending.baseline,pending.target); },error))', 1),
                ('automatic-acceptance-conflict', tx, 'if (!SettingsValuesEqual(current,pending.target)) return Result(SettingsCode::Conflict,"Settings changed before automatic completion");', '', 1),
                ('automatic-acceptance-clock', tx, 'if (!ValidTime(now,lastTime)) return Result(SettingsCode::Invalid,"Automatic completion time is invalid");', '', 1),
                ('automatic-completion-conflict', tx, 'if (!SettingsValuesEqual(current,pending.target)) return Result(SettingsCode::Conflict,"Settings changed during confirmation persistence");', '', 1),
                ('automatic-no-fresh-present', controller, 'attempt.completion == SettingsCompletion::Automatic && Fresh(observed,device)', 'attempt.completion == SettingsCompletion::Automatic', 1),
                ('automatic-no-effect-proof', controller, 'stage == SettingsDisplayStage::AwaitApply && observed.effectsReady &&', 'stage == SettingsDisplayStage::AwaitApply &&', 2),
                ('automatic-restore-no-effect-proof', controller, 'restoring && observed.effectsReady && Fresh(observed,device)', 'restoring && Fresh(observed,device)', 1),
                ('automatic-lost-queued-proof', controller, '!observed.effectsReady && (stage == SettingsDisplayStage::Confirming ||', 'false && (stage == SettingsDisplayStage::Confirming ||', 1),
                ('automatic-nested-effects', controller, 'if (!allowWork) return;', '(void)allowWork;', 1),
                ('commit-preparation-close-lost', controller, 'if (closing && !commitIntent) { Restore(SettingsCode::Ok,{},false); return; }', '', 1),
                ('automatic-commit-intent-lost', controller, 'commitIntent = true;', 'commitIntent = false;', 1),
            ]
            for label, source, old, new, count in changes:
                text = tx_source if source == tx else display_source
                if text.count(old) != count:
                    raise RuntimeError('Mutation anchor changed: ' + label)
                path = out / (label + '.cpp')
                path.write_text(text.replace(old, new), encoding='utf-8')
                build(label, names[0], transaction=path if source == tx else tx,
                      display=path if source == controller else controller, mutant=True)
        record['passed'] = True
    finally:
        record['sources_after'] = hashes()
        record['unchanged_sources'] = record['sources_before'] == record['sources_after']
        record['passed'] = record['passed'] and record['unchanged_sources']
        (out / 'result.json').write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
        print('Evidence:', out / 'result.json', flush=True)
    return 0 if record['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
