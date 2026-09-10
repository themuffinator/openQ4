#!/usr/bin/env python3
"""Compile real retained Runtime and the patched RmlUi geometry TU against existing
compatible MSVC debug-runtime RmlUi/jsoncpp/libtess2 archives, read-only. No engine
build, native window, OS input, or game launch. --mutations checks focused rejected
behavioral variants; --baseline is available in the isolated review snapshot.
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
    parser.add_argument('--baseline', action='store_true')
    parser.add_argument('--mutations', action='store_true')
    parser.add_argument('--test-source', type=Path)
    parser.add_argument('--reuse', type=Path, help='Reuse exactly hash-bound unchanged objects from an earlier result directory')
    args = parser.parse_args()
    repository = args.repository.resolve()
    rml = ROOT / 'rml-projection'
    if not rml.is_dir():
        rml = repository / 'subprojects/RmlUi-6.3'
    scratch = ROOT / '.tmp'
    scratch.mkdir(exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix='focus-reveal-', dir=scratch))
    env = os.environ.copy()
    env['TEMP'] = env['TMP'] = str(out)
    sources = sorted((ROOT / 'src/ui/retained').glob('*.cpp'))
    runtime = ROOT / ('.tmp/Runtime-before.cpp' if args.baseline else 'src/ui/retained/Runtime.cpp')
    sources = [runtime if source.name == 'Runtime.cpp' else source for source in sources]
    geometry = rml / 'Source/Core/ElementUtilities.cpp'
    test = args.test_source.resolve() if args.test_source else ROOT / 'tools/tests/native/UiFocusRevealTest.cpp'
    sources += [geometry, test]
    json_include = repository / 'subprojects/jsoncpp-1.9.6/include'
    tess_include = repository / 'subprojects/libtess2-8dbd6483e920311a58c9af10a10beb278efebc36/Include'
    includes = [ROOT, ROOT / 'src/ui/retained', rml / 'Include', rml / 'Source/Core', json_include, tess_include]
    libraries = [repository / 'builddir/subprojects/RmlUi-6.3/librmlui-core.a',
                 repository / 'builddir/subprojects/jsoncpp-1.9.6/libjsoncpp.a',
                 repository / 'builddir/subprojects/libtess2-8dbd6483e920311a58c9af10a10beb278efebc36/libtess2.a']
    headers = [path for base in [ROOT / 'src', rml / 'Include', rml / 'Source', json_include, tess_include]
               for path in base.rglob('*') if path.suffix in ('.h', '.hpp', '.inl', '.tpp')]
    bound = sources + headers + libraries + [Path(__file__).resolve()]
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
        result = subprocess.run([str(exe), *argv], cwd=exe.parent, env=env, text=True,
                                encoding='utf-8', errors='replace', capture_output=True)
        (exe.parent / 'run.log').write_text(result.stdout + result.stderr, encoding='utf-8')
        return result

    error = None
    run_result = None
    mutations = []
    try:
        exe = build('baseline' if args.baseline else 'positive', [source for source in sources if source not in reused], reused.values())
        run_result = run(exe, ['baseline'] if args.baseline else [])
        print(run_result.stdout + run_result.stderr, flush=True)
        if run_result.returncode == 0 and args.mutations:
            assert not args.baseline and not args.test_source
            runtime_text = runtime.read_text(encoding='utf-8')
            geometry_text = geometry.read_text(encoding='utf-8')
            cases = [
                ('no-inset', runtime, 'const float margin = 4.f * viewport.DpRatio();', 'const float margin = .001f;'),
                ('ignore-density', runtime, 'const float margin = 4.f * viewport.DpRatio();', 'const float margin = 4.f;'),
                ('ignore-horizontal', runtime, 'if (scrollX) parent->SetScrollLeft(', 'if (false && scrollX) parent->SetScrollLeft('),
                ('skip-outer-scroll', runtime, 'changed = true;\n\t\t\t\tcontext->Update();', 'changed = true;\n\t\t\t\treturn changed;\n\t\t\t\tcontext->Update();'),
                ('no-resize-reveal', runtime, '(focusChanged || (pass == 0 && viewportChanged))', '(focusChanged || (pass == 0 && viewportChanged && false))'),
                ('hijack-pointer-scroll', runtime, '(focusChanged || (pass == 0 && viewportChanged))', '(focusChanged || (pass == 0 && (viewportChanged || true)))'),
                ('wrong-view-clock', runtime, 'ContextClock clock(*services,time);\n\t\tcontext->Update();', 'ContextClock clock(*services,0);\n\t\tcontext->Update();'),
                ('ignore-transform', geometry, 'transform ? (*transform) * point : point', 'transform && false ? (*transform) * point : point'),
                ('accept-negative-w', geometry,
                 'projected.w <= 0 || projected.z < -z_clip * projected.w || projected.z > z_clip * projected.w',
                 'projected.w == 0 || projected.z < -z_clip * std::abs(projected.w) || projected.z > z_clip * std::abs(projected.w)'),
            ]
            objects = [reused.get(source, exe.parent / (source.stem + '.obj')) for source in sources]
            for name, original, anchor, replacement in cases:
                text = runtime_text if original == runtime else geometry_text
                assert text.count(anchor) == 1, name + ': exact source anchor'
                source = out / (name + '.cpp')
                source.write_text(text.replace(anchor, replacement), encoding='utf-8', newline='\n')
                other_objects = [obj for obj in objects if obj.stem != original.stem]
                mutant = build(name, [source], other_objects)
                result = run(mutant)
                rejected = result.returncode != 0 and 'FAIL ' in result.stderr
                mutations.append({'name': name, 'source_sha256': sha(source), 'compiled': True,
                                  'rejected': rejected, 'exit_code': result.returncode})
                if not rejected:
                    raise RuntimeError('Behavioral mutation was not rejected: ' + name)
                print('Rejected ' + name, flush=True)
    except Exception as failure:
        error = str(failure)
    stable = before == {str(path): sha(path) for path in bound}
    data = {'status': 'passed' if not error and stable and run_result and run_result.returncode == 0 else 'failed',
            'baseline': args.baseline, 'commands': commands, 'error': error, 'inputs_unchanged': stable,
            'sources': before, 'reused_objects': {str(source): str(obj) for source, obj in reused.items()}, 'mutations': mutations,
            'artifacts': {str(path.relative_to(out)): sha(path) for path in out.rglob('*')
                          if path.is_file() and path.suffix in ('.log', '.exe', '.cpp', '.cmd', '.obj')},
            'scope': 'Actual Runtime/RmlUi layout, exact projected quad, CPU-clipped submitted geometry with bounded Host; no GPU, native window/input or gameplay qualification.'}
    match = re.search(r'(?:PASS |UiNumberRuntimeTest: )(\d+) checks', run_result.stdout) if run_result else None
    data['checks'] = int(match[1]) if match else None
    (out / 'result.json').write_text(json.dumps(data, indent=2) + '\n', encoding='utf-8')
    print(out / 'result.json', flush=True)
    if error:
        print(error, flush=True)
    return 0 if data['status'] == 'passed' else 1


if __name__ == '__main__':
    raise SystemExit(main())
