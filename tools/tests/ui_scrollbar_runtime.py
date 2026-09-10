#!/usr/bin/env python3
"""Compile authored Scrollbar controls with the real Runtime and RmlUi geometry TU against existing
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
    parser.add_argument('--test-argument', action='append', help='Repeat to override the default canonical-document test argument')
    parser.add_argument('--document', type=Path, help='Canonical SYSTEM source to validate; the isolated frozen candidate takes precedence by default')
    parser.add_argument('--reuse', type=Path, help='Reuse exactly hash-bound unchanged objects from an earlier result directory')
    parser.add_argument('--mutations', action='store_true')
    args = parser.parse_args()
    repository = args.repository.resolve()
    candidate=(args.document or (ROOT/'.tmp/system-candidate.q4ui' if (ROOT/'.tmp/system-candidate.q4ui').is_file() else ROOT/'content/baseoq4/pak0/guis/menu/settings/system.q4ui')).resolve()
    rml = ROOT / 'rml-projection'
    if not rml.is_dir():
        rml = repository / 'subprojects/RmlUi-6.3'
    scratch = ROOT / '.tmp'
    scratch.mkdir(exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix='scrollbar-runtime-', dir=scratch))
    env = os.environ.copy()
    env['TEMP'] = env['TMP'] = str(out)
    sources = sorted((ROOT / 'src/ui/retained').glob('*.cpp'))
    runtime = ROOT / ('.tmp/Runtime-before.cpp' if args.baseline else 'src/ui/retained/Runtime.cpp')
    sources = [runtime if source.name == 'Runtime.cpp' else source for source in sources]
    geometry = rml / 'Source/Core/ElementUtilities.cpp'
    test = args.test_source.resolve() if args.test_source else ROOT / 'tools/tests/native/UiScrollbarRuntimeTest.cpp'
    sources += [geometry, test]
    json_include = repository / 'subprojects/jsoncpp-1.9.6/include'
    tess_include = repository / 'subprojects/libtess2-8dbd6483e920311a58c9af10a10beb278efebc36/Include'
    includes = [ROOT, ROOT / 'src/ui/retained', rml / 'Include', rml / 'Source/Core', json_include, tess_include]
    libraries = [repository / 'builddir/subprojects/RmlUi-6.3/librmlui-core.a',
                 repository / 'builddir/subprojects/jsoncpp-1.9.6/libjsoncpp.a',
                 repository / 'builddir/subprojects/libtess2-8dbd6483e920311a58c9af10a10beb278efebc36/libtess2.a']
    headers = [path for base in [ROOT / 'src', rml / 'Include', rml / 'Source', json_include, tess_include]
               for path in base.rglob('*') if path.suffix in ('.h', '.hpp', '.inl', '.tpp')]
    bound = sources + headers + libraries + [Path(__file__).resolve(), candidate]
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
            obj = Path(prior.get('reused_objects', {}).get(str(source), str(prior_dir / 'positive' / (source.stem + '.obj'))))
            expected = prior['artifacts'].get(str(obj.relative_to(prior_dir))) if obj.is_relative_to(prior_dir) else prior['sources'].get(str(obj))
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
        run_result = run(exe, args.test_argument if args.test_argument is not None else [str(candidate)])
        print(run_result.stdout + run_result.stderr, flush=True)
        if args.mutations and run_result.returncode == 0:
            rules=[
                ('density_focus','Runtime.cpp','preserveAuthoredAxes && scrollView.OwnsAxis(parent,true)', 'false && scrollView.OwnsAxis(parent,true)'),
                ('restore_refresh','Runtime.cpp','freshLayout,scrollError,true)','freshLayout,scrollError,false)'),
                ('unrelated_viewport','ScrollbarView.cpp','e.viewport==viewport && e.spec.vertical==vertical','viewport && e.spec.vertical==vertical'),
                ('perpendicular_axis','ScrollbarView.cpp','e.viewport==viewport && e.spec.vertical==vertical','e.viewport==viewport && (vertical || !vertical)'),
                ('restore_legacy','Runtime.cpp','(pass == 0 && (viewportChanged || impl->preserveRestoredScroll))','(pass == 0 && viewportChanged)'),
                ('capture_density','ScrollbarView.cpp','e.densityOffset=Impl::Offset(e)/impl->ratio','e.densityOffset=Impl::Offset(e)*impl->ratio'),
                ('restore_once','ScrollbarView.cpp','e.densityOffset.reset();Impl::Observe(e,ratio);','Impl::Observe(e,ratio);'),
                ('physical_line_step','ScrollbarView.cpp','value.lineStep=e.spec.lineStep*ratio','value.lineStep=e.spec.lineStep'),
                ('thumb_position','ScrollbarView.cpp','-margin+g.position','-margin+0*g.position'),
                ('drag_fraction','ScrollbarView.cpp','command.pointer,command.grabFraction,offset','command.pointer,.5,offset'),
                ('snapshot_dp','ScrollbarView.cpp','impl->ratio>0?Impl::Offset(e)/impl->ratio:0','impl->ratio>0?Impl::Offset(e):0'),
                ('restore_offset','ScrollbarView.cpp','if(fresh && desired &&','if(false && fresh && desired &&'),
                ('owned_thumb','Document.cpp','Require(!owned.contains({binding.node,binding.property}),value,path,','Require(true || !owned.contains({binding.node,binding.property}),value,path,'),
                ('thumb_frame_minimum','ScrollbarView.cpp','std::max(e.spec.minimumThumb*ratio,frameExtent)','e.spec.minimumThumb*ratio'),
                ('wheel_axis_priority','ScrollbarView.cpp','if(e.spec.vertical)return id;','if(!horizontal.empty())return horizontal; if(e.spec.vertical)return id;'),
                ('invalid_viewport','Runtime.cpp','impl->viewport.width=viewport.width;impl->viewport.height=viewport.height;','(void)viewport;'),
            ]
            objects={source:reused.get(source,out/'positive'/(source.stem+'.obj')) for source in sources}
            for name,filename,old,new in rules:
                original=ROOT/'src/ui/retained'/filename
                source_text=original.read_text(encoding='utf-8')
                if source_text.count(old)!=1:raise RuntimeError('Nonunique mutation anchor '+name)
                mutation=out/(name+'.cpp');mutation.write_text(source_text.replace(old,new),encoding='utf-8',newline='\n')
                binary=build(name,[mutation],[obj for source,obj in objects.items() if source!=original])
                observed=run(binary,[str(candidate)])
                rejected=observed.returncode!=0 and 'FAIL ' in observed.stdout
                mutations.append({'name':name,'compiled':True,'rejected':rejected,'exit_code':observed.returncode,'source_sha256':sha(mutation)})
                print(('Rejected ' if rejected else 'INVALID ')+'compiled mutation '+name,flush=True)
                if not rejected:raise RuntimeError('Compiled mutation escaped '+name)

    except Exception as failure:
        error = str(failure)
    stable = before == {str(path): sha(path) for path in bound}
    data = {'status': 'passed' if not error and stable and run_result and run_result.returncode == 0 else 'failed',
            'baseline': args.baseline, 'test_source':str(test),'document':str(candidate), 'commands': commands, 'error': error, 'inputs_unchanged': stable,
            'sources': before, 'reused_objects': {str(source): str(obj) for source, obj in reused.items()}, 'mutations': mutations,
            'artifacts': {str(path.relative_to(out)): sha(path) for path in out.rglob('*')
                          if path.is_file() and path.suffix in ('.log', '.exe', '.cpp', '.cmd', '.obj')},
            'scope': 'Actual canonical Scrollbar schema/Interaction/Runtime/RmlUi, transformed geometry and snapshots; counted host, no engine adapter/native input/GPU/touch qualification.'}
    match = re.search(r'(?:PASS |UiNumberRuntimeTest: )(\d+) checks', run_result.stdout) if run_result else None
    data['checks'] = int(match[1]) if match else None
    (out / 'result.json').write_text(json.dumps(data, indent=2) + '\n', encoding='utf-8')
    print(out / 'result.json', flush=True)
    if error:
        print(error, flush=True)
    return 0 if data['status'] == 'passed' else 1


if __name__ == '__main__':
    raise SystemExit(main())
