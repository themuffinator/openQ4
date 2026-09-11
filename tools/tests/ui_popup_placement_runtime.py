#!/usr/bin/env python3
"""Compile bounded, framed Choice popup placement with the real Runtime and RmlUi geometry TU against existing
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
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repository', type=Path, default=ROOT)
    parser.add_argument('--test-source', type=Path)
    parser.add_argument('--test-argument', action='append', help='Repeat to override the default canonical-document test argument')
    parser.add_argument('--document', type=Path, help='Canonical SYSTEM source to validate; defaults to the repository document')
    parser.add_argument('--reuse', type=Path, help='Reuse exactly hash-bound unchanged objects from an earlier result directory')
    parser.add_argument('--mutations', action='store_true')
    args = parser.parse_args()
    repository = args.repository.resolve()
    candidate=(args.document or ROOT/'content/baseoq4/pak0/guis/menu/settings/system.q4ui').resolve()
    rml = repository / 'subprojects/RmlUi-6.3'
    scratch = ROOT / '.tmp'
    scratch.mkdir(exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix='popup-placement-runtime-', dir=scratch))
    env = os.environ.copy()
    env['TEMP'] = env['TMP'] = str(out)
    sources = sorted((ROOT / 'src/ui/retained').glob('*.cpp'))
    geometry = rml / 'Source/Core/ElementUtilities.cpp'
    test = args.test_source.resolve() if args.test_source else ROOT / 'tools/tests/native/UiPopupPlacementRuntimeTest.cpp'
    sources += [geometry, test]
    json_include = repository / 'subprojects/jsoncpp-1.9.6/include'
    tess_include = repository / 'subprojects/libtess2-8dbd6483e920311a58c9af10a10beb278efebc36/Include'
    includes = [ROOT, ROOT / 'src/ui/retained', rml / 'Include', rml / 'Source/Core', json_include, tess_include]
    libraries = [repository / 'builddir/subprojects/RmlUi-6.3/librmlui-core.a',
                 repository / 'builddir/subprojects/jsoncpp-1.9.6/libjsoncpp.a',
                 repository / 'builddir/subprojects/libtess2-8dbd6483e920311a58c9af10a10beb278efebc36/libtess2.a']
    headers = [path for base in [ROOT / 'src', rml / 'Include', rml / 'Source', json_include, tess_include]
               for path in base.rglob('*') if path.suffix in ('.h', '.hpp', '.inl', '.tpp')]
    locales=sorted((ROOT/'content/baseoq4/pak0/strings').glob('*_openq4.lang'))+sorted((ROOT/'content/baseoq4/pak0/strings').glob('*_guis.lang'))
    bound = sources + headers + libraries + locales + [Path(__file__).resolve(), candidate, ROOT / "tools/tests/native/UiValueRuntimeTest.cpp"]
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
    commands = []; durations=[]

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
        started=time.monotonic()
        result = subprocess.run(['cmd', '/c', str(batch)], cwd=directory, env=env, text=True,
                                encoding='utf-8', errors='replace', capture_output=True)
        (directory / 'compile.log').write_text(result.stdout + result.stderr, encoding='utf-8')
        commands.append({'name': name, 'command': command, 'exit_code': result.returncode,'elapsed_seconds':time.monotonic()-started})
        if result.returncode:
            raise RuntimeError('Compilation failed: ' + str(directory / 'compile.log'))
        return exe

    def run(exe, argv=()):
        started=time.monotonic()
        with (exe.parent / 'run.log').open('w',encoding='utf-8') as log:
            result = subprocess.run([str(exe), *argv], cwd=exe.parent, env=env, text=True,
                                    encoding='utf-8', errors='replace', stdout=log, stderr=subprocess.STDOUT,
                                    timeout=1200)
        durations.append({'name':exe.parent.name,'argv':list(map(str,argv)),'elapsed_seconds':time.monotonic()-started})
        result.stdout = (exe.parent / 'run.log').read_text(encoding='utf-8')
        result.stderr = ''
        return result

    error = None
    run_result = None
    mutations = []
    try:
        exe = build('positive', [source for source in sources if source not in reused], reused.values())
        run_result = run(exe, args.test_argument if args.test_argument is not None else [str(candidate),str(ROOT/'content/baseoq4/pak0/strings')])
        print(run_result.stdout + run_result.stderr, flush=True)
        if args.mutations and run_result.returncode == 0:
            rules=[
                ('omit_ancestor_inset','ValueControlView.cpp','4*double(dp)+(reserveRounding?1:0)','0*double(dp)+(reserveRounding?1:0)'),
                ('ignore_placement_change','ValueControlView.cpp','interaction.InvalidateChoicePopup(id,view->popupToken);','(void)view;'),
                ('compress_row','ValueControlView.cpp','minimumWidth,std::max(desired,minimumRow+chrome),minimumRow+chrome,placement','minimumWidth,std::max(desired,minimumRow+chrome),1,placement'),
                ('ignore_row_width','ValueControlView.cpp','minimumRowWidth+frame.x+viewportFrame.x+gutter','36*ratio+frame.x+viewportFrame.x+gutter'),
                ('ignore_formatted_width','ValueControlView.cpp','output = width; return true;','output = 24; return true;'),
                ('popup_padding','ValueControlView.cpp','popup->GetBox().GetFrameSize(Rml::BoxArea::Padding).y+viewportFrame.y+viewportTop','0+viewportFrame.y+viewportTop'),
                ('allow_nonancestor_bounds','Document.cpp','Descendant(owner.id,choice->placementBounds),value,path+"/placementBounds"','true,value,path+"/placementBounds"'),
                ('allow_popup_transform','Document.cpp','Require(!model.FindNode(choice->popup)->properties.contains("transform"),value,path,','Require(true,value,path,'),
            ]
            objects={source:reused.get(source,out/'positive'/(source.stem+'.obj')) for source in sources}
            for name,filename,old,new in rules:
                original=ROOT/'src/ui/retained'/filename
                source_text=original.read_text(encoding='utf-8')
                if source_text.count(old)!=1:raise RuntimeError('Nonunique mutation anchor '+name)
                mutation=out/(name+'.cpp');mutation.write_text(source_text.replace(old,new),encoding='utf-8',newline='\n')
                binary=build(name,[mutation],[obj for source,obj in objects.items() if source!=original])
                observed=run(binary,[]) # Focused actual-Rml cases reject mutations; SYSTEM is separately in the positive baseline.
                rejected=observed.returncode!=0 and ('FAIL ' in observed.stdout or 'FAIL:' in observed.stdout)
                mutations.append({'name':name,'compiled':True,'rejected':rejected,'exit_code':observed.returncode,'source_sha256':sha(mutation)})
                print(('Rejected ' if rejected else 'INVALID ')+'compiled mutation '+name,flush=True)
                if not rejected:raise RuntimeError('Compiled mutation escaped '+name)

    except Exception as failure:
        error = str(failure)
    stable = before == {str(path): sha(path) for path in bound}
    data = {'status': 'passed' if not error and stable and run_result and run_result.returncode == 0 else 'failed',
            'test_source':str(test),'document':str(candidate), 'commands': commands, 'error': error, 'inputs_unchanged': stable,
            'sources': before, 'reused_objects': {str(source): str(obj) for source, obj in reused.items()}, 'mutations': mutations,'execution_durations':durations,
            'artifacts': {str(path.relative_to(out)): sha(path) for path in out.rglob('*')
                          if path.is_file() and path.suffix in ('.log', '.exe', '.cpp', '.cmd', '.obj')},
            'scope': 'Actual bounded popup schema/Runtime/Rml layout/geometry and editable SYSTEM framing; counted fonts/layer host, no native/GPU mask-pixel or platform input qualification.'}
    match = re.search(r'(?:PASS |UiNumberRuntimeTest: |UiPopupPlacementRuntimeTest: )(\d+) checks', run_result.stdout) if run_result else None
    data['checks'] = int(match[1]) if match else None
    (out / 'result.json').write_text(json.dumps(data, indent=2) + '\n', encoding='utf-8')
    print(out / 'result.json', flush=True)
    if error:
        print(error, flush=True)
    return 0 if data['status'] == 'passed' else 1


if __name__ == '__main__':
    raise SystemExit(main())
