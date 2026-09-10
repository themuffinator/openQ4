#!/usr/bin/env python3
"""Compile canonical SYSTEM preset controls with the real Runtime, profile model,
transaction and RmlUi geometry TU against existing
compatible MSVC debug-runtime RmlUi/jsoncpp/libtess2 archives, read-only. No engine
build, native window, OS input, or game launch.
"""
import argparse
import hashlib
import json
import os
import re
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repository', type=Path, default=ROOT)
    parser.set_defaults(baseline=False)
    parser.add_argument('--test-source', type=Path)
    parser.add_argument('--reuse', type=Path, help='Reuse exactly hash-bound unchanged objects from an earlier result directory')
    args = parser.parse_args()
    repository = args.repository.resolve()
    rml = ROOT / 'rml-projection'
    if not rml.is_dir():
        rml = repository / 'subprojects/RmlUi-6.3'
    scratch = ROOT / '.tmp'
    scratch.mkdir(exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix='system-presets-runtime-', dir=scratch))
    env = os.environ.copy()
    env['TEMP'] = env['TMP'] = str(out)
    sources = sorted((ROOT / 'src/ui/retained').glob('*.cpp'))
    runtime = ROOT / ('.tmp/Runtime-before.cpp' if args.baseline else 'src/ui/retained/Runtime.cpp')
    sources = [runtime if source.name == 'Runtime.cpp' else source for source in sources]
    geometry = rml / 'Source/Core/ElementUtilities.cpp'
    test = args.test_source.resolve() if args.test_source else ROOT / 'tools/tests/native/UiSystemPresetRuntimeTest.cpp'
    sources += [geometry, test, ROOT/'src/ui/application/SettingsTransaction.cpp', ROOT/'src/framework/PerformancePreset.cpp']
    json_include = repository / 'subprojects/jsoncpp-1.9.6/include'
    tess_include = repository / 'subprojects/libtess2-8dbd6483e920311a58c9af10a10beb278efebc36/Include'
    includes = [ROOT, ROOT / 'src/ui/retained', rml / 'Include', rml / 'Source/Core', json_include, tess_include]
    libraries = [repository / 'builddir/subprojects/RmlUi-6.3/librmlui-core.a',
                 repository / 'builddir/subprojects/jsoncpp-1.9.6/libjsoncpp.a',
                 repository / 'builddir/subprojects/libtess2-8dbd6483e920311a58c9af10a10beb278efebc36/libtess2.a']
    headers = [path for base in [ROOT / 'src', rml / 'Include', rml / 'Source', json_include, tess_include]
               for path in base.rglob('*') if path.suffix in ('.h', '.hpp', '.inl', '.tpp')]
    bound = sources + headers + libraries + [Path(__file__).resolve(), ROOT/'content/baseoq4/pak0/guis/menu/settings/system.q4ui'] + list((ROOT/'content/baseoq4/pak0/strings').glob('*_openq4.lang'))
    before = {str(path): sha(path) for path in bound}
    reused = {}
    if args.reuse:
        prior_dir = args.reuse.resolve()
        prior = json.loads((prior_dir / 'result.json').read_text(encoding='utf-8'))
        if not all(prior['sources'].get(str(path)) == sha(path) for path in headers + libraries):
            raise RuntimeError('Changed or unbound shared compile dependencies')
        for source in sources:
            if source == test or prior['sources'].get(str(source)) != sha(source):
                continue
            obj = prior_dir / 'positive' / (source.stem + '.obj')
            expected = prior['artifacts'].get(str(obj.relative_to(prior_dir)))
            if expected is not None and obj.is_file() and sha(obj) == expected:
                reused[source] = obj
        bound += list(reused.values())
        before.update({str(obj): sha(obj) for obj in reused.values()})
    commands = []

    def build(name, compile_sources, objects=()):
        directory = out / name
        directory.mkdir()
        exe = directory / 'test.exe'
        command = 'cl /nologo /MTd /DNDEBUG /DRMLUI_STATIC_LIB /DRMLUI_NO_THIRDPARTY_CONTAINERS /std:c++20 /EHsc /utf-8 /W3 /WX '
        command += ' '.join('/I"' + str(path) + '"' for path in includes)
        command += ' /Fo"' + str(directory) + '/" '
        command += ' '.join('"' + str(path) + '"' for path in list(compile_sources) + list(objects))
        command += ' /Fe"' + str(exe) + '" /link '
        command += ' '.join('"' + str(path) + '"' for path in libraries) + ' /SUBSYSTEM:CONSOLE user32.lib'
        batch = directory / 'compile.cmd'
        batch.write_text('@echo off\ncall "' + str(repository / 'tools/build/openq4_devcmd.cmd') +
                         '"\nif errorlevel 1 exit /b %errorlevel%\n' + command + '\n', encoding='utf-8')
        result = subprocess.run(['cmd', '/c', str(batch)], cwd=directory, env=env, text=True,
                                encoding='utf-8', errors='replace', capture_output=True)
        (directory / 'compile.log').write_text(result.stdout + result.stderr, encoding='utf-8')
        commands.append({'name': name, 'command': command, 'exit_code': result.returncode})
        if result.returncode:
            raise RuntimeError('Compilation failed: ' + str(directory / 'compile.log'))
        return exe

    def run(exe, argv=()):
        with (exe.parent / 'run.log').open('w',encoding='utf-8') as log:
            result = subprocess.run([str(exe), *argv], cwd=exe.parent, env=env, text=True,
                                    encoding='utf-8', errors='replace', stdout=log, stderr=subprocess.STDOUT,
                                    timeout=1200)
        result.stdout = (exe.parent / 'run.log').read_text(encoding='utf-8')
        result.stderr = ''
        return result

    error = None
    run_result = None
    mutations = []
    try:
        exe = build('baseline' if args.baseline else 'positive', [source for source in sources if source not in reused], reused.values())
        run_result = run(exe, [str(ROOT/'content/baseoq4/pak0/guis/menu/settings/system.q4ui'), str(ROOT/'content/baseoq4/pak0/strings')])
        print(run_result.stdout + run_result.stderr, flush=True)
    except Exception as failure:
        error = str(failure)
    stable = before == {str(path): sha(path) for path in bound}
    data = {'status': 'passed' if not error and stable and run_result and run_result.returncode == 0 else 'failed',
            'baseline': args.baseline, 'commands': commands, 'error': error, 'inputs_unchanged': stable,
            'sources': before, 'reused_objects': {str(source): str(obj) for source, obj in reused.items()}, 'mutations': mutations,
            'artifacts': {str(path.relative_to(out)): sha(path) for path in out.rglob('*')
                          if path.is_file() and path.suffix in ('.log', '.exe', '.cpp', '.cmd', '.obj')},
            'scope': 'Actual canonical SYSTEM/Runtime/RmlUi and shared profile/transaction model with counted service fixture. Six real localized labels and 40% glyph-width stress; no actual engine adapter/host, stock font shaping, GPU, native input or gameplay qualification.'}
    match = re.search(r'(?:PASS |UiNumberRuntimeTest: )(\d+) checks', run_result.stdout) if run_result else None
    data['checks'] = int(match[1]) if match else None
    (out / 'result.json').write_text(json.dumps(data, indent=2) + '\n', encoding='utf-8')
    print(out / 'result.json', flush=True)
    if error:
        print(error, flush=True)
    return 0 if data['status'] == 'passed' else 1


if __name__ == '__main__':
    raise SystemExit(main())
