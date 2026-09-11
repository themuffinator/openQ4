#!/usr/bin/env python3
"""Actual retained Runtime/history with pinned read-only MSVC Rml archives; no engine build or input."""
from pathlib import Path
import argparse,hashlib,json,os,subprocess,tempfile
from ui_document_view_projection import project_view
ROOT=Path(__file__).resolve().parents[2]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--repository',type=Path,required=True)
    parser.add_argument('--test-source',type=Path);parser.add_argument('--reuse',type=Path);parser.add_argument('--view',action='store_true');parser.add_argument('--arg',action='append',default=[]);parser.add_argument('--bind-file',action='append',type=Path,default=[])
    args=parser.parse_args();repo=args.repository.resolve();scratch=ROOT/'.tmp';scratch.mkdir(exist_ok=True)
    out=Path(tempfile.mkdtemp(prefix='document-canvas-',dir=scratch));env=os.environ.copy();env['TEMP']=env['TMP']=str(out)
    rml=repo/'subprojects/RmlUi-6.3';jsonroot=repo/'subprojects/jsoncpp-1.9.6';tess=repo/'subprojects/libtess2-8dbd6483e920311a58c9af10a10beb278efebc36'
    extra=[]
    if args.view:
        original=ROOT/'src/ui/RetainedUI.cpp';source=original.read_text(encoding='utf-8')
        generated=out/'ViewMethods.inc';generated.write_text(project_view(source),encoding='utf-8',newline='\n')
        extra=[original,ROOT/'src/ui/RetainedUI.h',ROOT/'tools/tests/native/UiDocumentCanvasTest.cpp',ROOT/'tools/tests/filesystem_case_segments.py',ROOT/'tools/tests/ui_document_view_projection.py',generated]
    sources=sorted((ROOT/'src/ui/retained').glob('*.cpp'))+[rml/'Source/Core/ElementUtilities.cpp',args.test_source.resolve() if args.test_source else ROOT/('tools/tests/native/UiDocumentViewTest.cpp' if args.view else 'tools/tests/native/UiDocumentCanvasTest.cpp')]
    includes=[ROOT,out,ROOT/'src/ui/retained',rml/'Include',rml/'Source/Core',jsonroot/'include',tess/'Include']
    libraries=[repo/'builddir/subprojects/RmlUi-6.3/librmlui-core.a',repo/'builddir/subprojects/jsoncpp-1.9.6/libjsoncpp.a',repo/'builddir/subprojects'/tess.name/'libtess2.a']
    headers=[p for base in [ROOT/'src',rml/'Include',rml/'Source',jsonroot/'include',tess/'Include'] for p in base.rglob('*') if p.suffix in ('.h','.hpp','.inl','.tpp')]
    bound=sources+headers+libraries+[Path(__file__).resolve(),ROOT/'tools/tests/native/UiDocumentPopupFixture.h']+extra+[p.resolve() for p in args.bind_file];before={str(p):sha(p) for p in bound};reused={}
    if args.reuse:
        previous=args.reuse.resolve();record=json.loads((previous/'result.json').read_text())
        if all(record['sources'].get(str(p))==sha(p) for p in headers+libraries):
            for source in sources:
                if source==sources[-1]:continue # generated/other included actual method bodies can change
                obj=Path(record.get('reused_objects',{}).get(str(source),str(previous/(source.stem+'.obj'))))
                expected=record['artifacts'].get(str(obj.relative_to(previous))) if obj.is_relative_to(previous) else record['sources'].get(str(obj))
                if record['sources'].get(str(source))==sha(source) and obj.is_file() and sha(obj)==expected:reused[source]=obj
        bound+=list(reused.values());before.update({str(p):sha(p) for p in reused.values()})
    command='cl /nologo /MTd /DNDEBUG /DRMLUI_STATIC_LIB /DRMLUI_NO_THIRDPARTY_CONTAINERS /std:c++20 /EHsc /utf-8 /W3 /WX '
    command+=' '.join('/I"'+str(p)+'"' for p in includes)+' /Fo"'+str(out)+'/" '
    command+=' '.join('"'+str(p)+'"' for p in [s for s in sources if s not in reused]+list(reused.values()))
    command+=' /Fe"'+str(out/'test.exe')+'" /link '+' '.join('"'+str(p)+'"' for p in libraries)+' /SUBSYSTEM:CONSOLE user32.lib'
    batch=out/'compile.cmd';batch.write_text('@echo off\ncall "'+str(repo/'tools/build/openq4_devcmd.cmd')+'"\nif errorlevel 1 exit /b %errorlevel%\n'+command+'\n')
    compile=subprocess.run(['cmd','/c',str(batch)],cwd=out,env=env,text=True,encoding='utf-8',errors='replace',capture_output=True)
    (out/'compile.log').write_text(compile.stdout+compile.stderr,encoding='utf-8');run=None
    if not compile.returncode:
        run=subprocess.run([str(out/'test.exe')]+args.arg,cwd=out,env=env,text=True,encoding='utf-8',errors='replace',capture_output=True,timeout=1200)
        (out/'run.log').write_text(run.stdout+run.stderr,encoding='utf-8');print(run.stdout+run.stderr,flush=True)
    stable=before=={str(p):sha(p) for p in bound}
    result={'status':'passed' if not compile.returncode and run and not run.returncode and stable else 'failed','sources':before,'inputs_unchanged':stable,
        'compile_exit':compile.returncode,'run_exit':run.returncode if run else None,'command':command,'reused_objects':{str(k):str(v) for k,v in reused.items()},
        'artifacts':{str(p.relative_to(out)):sha(p) for p in out.rglob('*') if p.is_file()},'scope':__doc__}
    (out/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(out/'result.json',flush=True)
    return 0 if result['status']=='passed' else 1
if __name__=='__main__':raise SystemExit(main())
