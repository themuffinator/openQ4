#!/usr/bin/env python3
"""Compile exact settings transactions/recovery against production sources.

The native suites use real typed validation and JsonCpp parsing. Existing host,
service and display-service harnesses exercise extracted production methods with
counted renderer/SDL/persistence boundaries; no actual devices or input run.
--sanitize instruments the native suites and real CVar normalization suite.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import subprocess
import sys
import tempfile

from filesystem_case_segments import function_body

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', default='clang++')
    parser.add_argument('--sanitize', action='store_true')
    parser.add_argument('--mutations', action='store_true')
    options = parser.parse_args()
    r = ROOT
    (r / '.tmp').mkdir(exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix='settings-exact-values-', dir=r / '.tmp'))
    env = dict(os.environ, TEMP=str(out), TMP=str(out), TMPDIR=str(out))
    native_names = ['UiSettingsTransactionTest', 'UiSettingsDisplayControllerTest',
                    'UiSettingsExactValueTest', 'UiSettingsJournalTest', 'UiSettingsJournalExactTest']
    paths = [r / 'tools/tests' / n for n in ('ui_settings_exact_values.py', 'ui_settings_service.py',
             'ui_system_settings_host.py', 'ui_settings_decimal_precision.py',
             'ui_settings_display_service.py', 'ui_system_display.py', 'filesystem_case_segments.py')]
    paths += [r / 'tools/tests/native' / (n + '.cpp') for n in native_names]
    paths += [r / 'src/ui/application' / (n + ext) for n in (
        'SettingsTransaction', 'SettingsDisplayController', 'SettingsJournal', 'SettingsEffectPlan',
        'SystemSettingsHost', 'SystemDisplay') for ext in ('.h', '.cpp')]
    paths += [r / 'src/ui/application/SettingsValue.h']
    paths += [r / 'src/ui' / (n + ext) for n in ('SettingsService', 'SettingsDisplayService') for ext in ('.h', '.cpp')]
    paths += [r / 'src/ui/retained' / n for n in ('Document.h', 'Document.cpp', 'Vector.h', 'Presentation.cpp')]
    paths += [r / n for n in ('src/framework/Common.cpp', 'src/framework/CVarSystem.cpp',
        'src/framework/Session.cpp', 'src/framework/SettingsPersistence.h', 'src/framework/DurableFile.h',
        'src/idlib/Str.h', 'src/idlib/NumericString.h', 'src/sys/WindowSettings.h',
        'src/renderer/RendererModule.h', 'src/renderer/RenderModuleAPI.h', 'src/renderer/DisplayPresentation.h',
        'src/renderer/RenderSystem.cpp', 'src/renderer/Vulkan/vk_GuiExecutor.cpp', 'src/renderer/OpenGL/gl_ContextSDL3.cpp')]
    jsoncpp = r / 'subprojects/jsoncpp-1.9.6'
    paths += [p for folder in ('include', 'src/lib_json') for p in (jsoncpp / folder).rglob('*')
              if p.is_file() and p.suffix in ('.h', '.cpp', '.inl')]
    sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
    before = {str(p.relative_to(r)): sha(p) for p in paths}
    record = {'passed': False, 'sanitized_native_and_cvar_suites': options.sanitize,
              'sources_before': before, 'runs': [], 'mutants': [],
              'scope': __doc__, 'limitations': 'No renderer/device restart, native persistence durability, OS input or game execution. Existing service harnesses compile with their independently detected compiler without sanitizer flags.'}
    flags = [options.compiler, '-std=c++20', '-O2', '-Wall', '-Wextra', '-Werror',
             '-I', str(r), '-I', str(r / 'src/ui/application'), '-I', str(jsoncpp / 'include')]
    if options.sanitize:
        flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-g0']

    def run(label, args, mutant=False):
        result = subprocess.run([str(a) for a in args], env=env, capture_output=True, text=True)
        text = result.stdout + result.stderr
        log = out / (label + '.log')
        log.write_text(text, encoding='utf-8')
        row = {'label': label, 'command': [str(a) for a in args], 'exit': result.returncode,
               'log': str(log.relative_to(r)), 'output': text}
        record['mutants' if mutant else 'runs'].append(row)
        print(label, result.returncode, text[-500:], flush=True)
        return row

    def build(label, sources, mutant=False, extra_flags=()):
        exe = out / (label + ('.exe' if os.name == 'nt' else ''))
        row = run(label + '-compile', [flags[0], *extra_flags, *flags[1:], *sources, '-o', exe])
        if row['exit']:
            raise RuntimeError('Compilation failed: ' + label)
        row = run(label + '-run', [exe], mutant)
        if (row['exit'] == 0) == mutant:
            raise RuntimeError('Unexpected test result: ' + label)

    def mutation(source, old, new, label):
        if source.count(old) != {'sync-conflict-missed': 2, 'async-restore-false-success': 2}.get(label, 1):
            raise RuntimeError('Nonunique mutation anchor: ' + label)
        return source.replace(old, new)

    try:
        for name in ('ui_settings_service.py', 'ui_system_settings_host.py',
                     'ui_settings_decimal_precision.py', 'ui_settings_display_service.py'):
            args = [sys.executable, r / 'tools/tests' / name]
            if name == 'ui_settings_decimal_precision.py':
                args += ['--compiler', options.compiler] + (['--sanitize'] if options.sanitize else [])
            if run(name, args)['exit']:
                raise RuntimeError(name)
        document = (r / 'src/ui/retained/Document.cpp').read_text(encoding='utf-8')
        valid = out / 'valid.cpp'
        valid.write_text('#include "src/ui/retained/Document.h"\n#include <cmath>\nnamespace openq4::ui {\n' +
                         function_body(document, 'bool Utf8(') + function_body(document, 'bool ValidStateValue(') + '}\n', encoding='utf-8')
        names = ('bool Identifier(', 'std::string PointerPart(', 'void Diagnose(', 'bool Utf8(',
                 'bool LexicalForms(', 'bool Parse(', 'bool ValidStateValue(', 'bool ParseStateValues(')
        validation = '\n'.join(document[document.index(n):document.index('bool Parse(', document.index(n))]
                               if n == 'bool LexicalForms(' else function_body(document, n) for n in names)
        parsed = out / 'parsed.cpp'
        parsed.write_text('#include "src/ui/retained/Document.h"\n#include <json/json.h>\n#include <cmath>\n#include <algorithm>\n#include <memory>\nnamespace openq4::ui { constexpr size_t MaxSourceBytes=16*1024*1024;\n' + validation + '\n}\n', encoding='utf-8')
        tx = r / 'src/ui/application/SettingsTransaction.cpp'
        controller = r / 'src/ui/application/SettingsDisplayController.cpp'
        journal = r / 'src/ui/application/SettingsJournal.cpp'
        effect_plan = r / 'src/ui/application/SettingsEffectPlan.cpp'
        exact_test = r / 'tools/tests/native/UiSettingsExactValueTest.cpp'
        journal_test = r / 'tools/tests/native/UiSettingsJournalExactTest.cpp'
        json_sources = [jsoncpp / 'src/lib_json' / ('json_' + n + '.cpp') for n in ('reader', 'value', 'writer')]
        for name in native_names:
            sources = [r / 'tools/tests/native' / (name + '.cpp'), tx]
            sources += [journal, effect_plan, parsed, *json_sources] if 'Journal' in name else [valid]
            if name == 'UiSettingsDisplayControllerTest':
                sources += [controller]
            build(name, sources)
        if options.mutations:
            source = tx.read_text(encoding='utf-8')
            changes = [
                ('lost-zero-patch', 'if (!SettingsValueEqual(before.at(key),value))', 'if (before.at(key)!=value)'),
                ('sync-conflict-missed', 'if (!SettingsValuesEqual(current,baseline)) return Result(SettingsCode::Conflict,"Settings changed outside this session; reopen before applying");', 'if (current!=baseline) return Result(SettingsCode::Conflict,"Settings changed outside this session; reopen before applying");'),
                ('async-readback-missed', 'if (!SettingsValuesEqual(current,pending.target)) return Result(SettingsCode::ApplyFailed,"Settings apply readback did not match the frozen target");', 'if (current!=pending.target) return Result(SettingsCode::ApplyFailed,"Settings apply readback did not match the frozen target");'),
                ('async-restore-ownership', 'if (!SettingsValueEqual(current.at(key),original) && SettingsValueEqual(current.at(key),target))', 'if (current.at(key)!=original && current.at(key)==target)'),
                ('async-restore-false-success', 'if (SettingsValueEqual(current.at(key),baseline.at(key))) continue;', 'if (current.at(key)==baseline.at(key)) continue;'),
                ('sync-rollback-ownership', 'if (!SettingsValueEqual(current.at(key),target)) { conflict = true; continue; }', 'if (current.at(key)!=target) { conflict = true; continue; }'),
                ('confirm-drift-missed', 'if (!SettingsValuesEqual(current,pending.target)) return Result(SettingsCode::Conflict,"Settings changed during confirmation persistence");', 'if (current!=pending.target) return Result(SettingsCode::Conflict,"Settings changed during confirmation persistence");')]
            for label, old, new in changes:
                path = out / (label + '.cpp')
                path.write_text(mutation(source, old, new, label), encoding='utf-8')
                build(label, [exact_test, path, valid], True)

            header = (r / 'src/ui/application/SettingsValue.h').read_text(encoding='utf-8')
            tx_header = (r / 'src/ui/application/SettingsTransaction.h').read_text(encoding='utf-8')
            header_changes = [
                ('daz-floating-equality', 'SettingsValue.h', function_body(header, 'inline bool SettingsValueEqual('), 'inline bool SettingsValueEqual(const StateValue& a,const StateValue& b) noexcept { return a==b; }'),
                ('signed-zero-distinguished', 'SettingsValue.h', 'return left == right || (leftMagnitude == 0 && rightMagnitude == 0);', 'return left == right;'),
                ('invalid-number-equal', 'SettingsValue.h', 'if (leftMagnitude >= 0x7ff0000000000000ULL || rightMagnitude >= 0x7ff0000000000000ULL) return false;', ''),
                ('dirty-daz-equality', 'SettingsTransaction.h', '!SettingsValuesEqual(draft,baseline)', 'draft!=baseline'),
                ('general-truncates', 'SettingsValue.h', "bool increment = digits[precision] > '5';", 'bool increment = false;')]
            for label, filename, old, new in header_changes:
                shadow = out / (label + '-include')
                app = shadow / 'src/ui/application'
                retained = shadow / 'src/ui/retained'
                app.mkdir(parents=True)
                retained.mkdir(parents=True)
                for p in (r / 'src/ui/retained').glob('*.h'):
                    (retained / p.name).write_bytes(p.read_bytes())
                for p in (r / 'src/ui/application').glob('*.h'):
                    (app / p.name).write_bytes(p.read_bytes())
                (app / filename).write_text(mutation(header if filename == 'SettingsValue.h' else tx_header, old, new, label), encoding='utf-8')
                (app / 'SettingsTransaction.cpp').write_bytes(tx.read_bytes())
                if label == 'general-truncates':
                    (app / 'SettingsJournal.cpp').write_bytes(journal.read_bytes())
                    (app / 'SettingsEffectPlan.cpp').write_bytes(effect_plan.read_bytes())
                    sources = [journal_test, app / 'SettingsTransaction.cpp', app / 'SettingsJournal.cpp', app / 'SettingsEffectPlan.cpp', parsed, *json_sources]
                else:
                    sources = [exact_test, app / 'SettingsTransaction.cpp', valid]
                build(label, sources, True, ['-I', str(shadow)])

            journal_source = journal.read_text(encoding='utf-8')
            journal_changes = [
                ('journal-omits-tiny-patch', '!SettingsValueEqual(old->second,target->second)', 'old->second!=target->second'),
                ('journal-allows-wrong-zero', '!SettingsValuesEqual(expected,journal.patch)', 'expected!=journal.patch'),
                ('journal-serializes-daz-zero', 'SettingsValueEqual(value,StateValue(0.0)) ? 0.0 : *number', '*number==0.0 ? 0.0 : *number'),
                ('journal-negative-zero', 'SettingsValueEqual(value,StateValue(0.0)) ? 0.0 : *number', '*number')]
            for label, old, new in journal_changes:
                path = out / (label + '.cpp')
                path.write_text(mutation(journal_source, old, new, label), encoding='utf-8')
                build(label, [journal_test, path, tx, effect_plan, parsed, *json_sources], True)

            # Existing production-body harnesses publish no state when each of
            # these exact comparison guards is removed. All other cases remain.
            service_changes = [
                ('service-exit-dirty', 'service.transaction.Dirty()', 'service.transaction.Draft()!=service.transaction.Baseline()'),
                ('service-trace-dirty', 'transaction.Dirty() ? 1 : 0', '(transaction.Draft()!=transaction.Baseline()) ? 1 : 0'),
                ('service-exposed-dirty', 'own && transaction.Dirty()', 'own && transaction.Draft()!=transaction.Baseline()')]
            display_changes = [
                ('display-catalog-freeze', 'SettingsValuesEqual(current,target)', 'current==target'),
                ('display-replay-ownership', '!SettingsValueEqual(live.at(key),journal.baseline.at(key)) && !SettingsValueEqual(live.at(key),value)', 'live.at(key)!=journal.baseline.at(key) && live.at(key)!=value'),
                ('display-replay-patch', '!SettingsValueEqual(live.at(key),desired.at(key))', 'live.at(key)!=desired.at(key)')]
            for module, changes in (('ui_settings_service', service_changes), ('ui_settings_display_service', display_changes)):
                for label, old, new in changes:
                    script = out / (label + '.py')
                    script.write_text('import sys\nsys.path.insert(0,' + repr(str(r / 'tools/tests')) + ')\nimport ' + module + ' as test\n' +
                                      ('test.SCENARIOS=("exact_dirty_apply","exact_exit_guard","exact_conflict")\n' if module == 'ui_settings_service' else '') +
                                      'test.main(' + repr(((old, new),)) + ')\n', encoding='utf-8')
                    row = run(label, [sys.executable, script], True)
                    # A compile/anchor error is not a rejected behavioral mutant.
                    if row['exit'] == 0 or 'FAIL' not in row['output'] or 'error:' in row['output']:
                        raise RuntimeError('Mutant was not rejected by a runtime assertion: ' + label)
        record['passed'] = True
    finally:
        after = {str(p.relative_to(r)): sha(p) for p in paths}
        record['unchanged_sources'] = before == after
        record['sources_after'] = after
        record['files'] = {str(p.relative_to(r)): sha(p) for p in out.rglob('*') if p.is_file()}
        (out / 'result.json').write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
        print(out / 'result.json', flush=True)
        if before != after:
            raise RuntimeError('Sources changed during validation')


if __name__ == '__main__':
    main()
