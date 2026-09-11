#!/usr/bin/env python3
"""Decisive compiled mutations against a passing exact actual Runtime/Rml/service record."""
from pathlib import Path
import argparse,hashlib,json,os,subprocess,tempfile,sys
from filesystem_case_segments import function_body
ROOT=Path(__file__).resolve().parents[2]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
 a=argparse.ArgumentParser();a.add_argument('--repository',type=Path,required=True);a.add_argument('--baseline',type=Path,required=True);a.add_argument('--select',action='append');args=a.parse_args()
 repo=args.repository.resolve();base=args.baseline.resolve();record=json.loads((base/'result.json').read_text())
 if record['status']!='passed':raise RuntimeError('Passing baseline required')
 for name,expected in record['sources'].items():
  if sha(Path(name))!=expected:raise RuntimeError('Changed baseline dependency: '+name)
 out=Path(tempfile.mkdtemp(prefix='document-canvas-mutations-',dir=ROOT/'.tmp'));env={**os.environ,'TEMP':str(out),'TMP':str(out)}
 runtime=ROOT/'src/ui/retained/Runtime.cpp';interaction=ROOT/'src/ui/retained/Interaction.cpp';view=ROOT/'src/ui/RetainedUI.cpp';test=ROOT/'tools/tests/native/UiDocumentViewTest.cpp'
 units=[Path(v) for v in record['sources'] if Path(v).suffix=='.cpp' and Path(v).parent in [ROOT/'src/ui/retained',repo/'subprojects/RmlUi-6.3/Source/Core',ROOT/'tools/tests/native'] and Path(v).name!='UiDocumentCanvasTest.cpp']
 objects={}
 for unit in units:
  obj=Path(record.get('reused_objects',{}).get(str(unit),str(base/(unit.stem+'.obj'))))
  expected=record['artifacts'].get(str(obj.relative_to(base))) if obj.is_relative_to(base) else record['sources'].get(str(obj))
  if not expected or sha(obj)!=expected:raise RuntimeError('Unbound object: '+str(obj))
  objects[unit]=obj
 rml=repo/'subprojects/RmlUi-6.3';jsonroot=repo/'subprojects/jsoncpp-1.9.6';tess=repo/'subprojects/libtess2-8dbd6483e920311a58c9af10a10beb278efebc36'
 libs=[repo/'builddir/subprojects/RmlUi-6.3/librmlui-core.a',repo/'builddir/subprojects/jsoncpp-1.9.6/libjsoncpp.a',repo/'builddir/subprojects'/tess.name/'libtess2.a']
 cases=[]
 for method in ['FocusControl','CancelInput','SetState','TakeActions','SetPresentationAlias','PointerWheel','BeginNumberNativeCollection','RestoreSnapshot','Frame']:
  body=function_body(runtime.read_text(),'Runtime::'+method+'(')
  # function_body starts at the qualified name; replacing its entry guard keeps the real method.
  guard='if(!Mutate())return;'
  if guard not in body:guard='if(!Mutate())return false;'
  if guard not in body:guard='if(!Mutate())return {};'
  if body.count(guard)!=1:raise RuntimeError('Missing independent guard: '+method)
  cases.append(('guard_'+method,runtime,body,body.replace(guard,''),False))
 cases += [
  ('canvas_identity',runtime,'CanvasIdentity()==data.expected &&','true &&',False),
  ('source_association',runtime,'data.source.get()==source.data.get() &&','true &&',False),
  ('source_lifetime',runtime,'!source.OwnerCurrent() ||','',False),
  ('canvas_publication',runtime,'if(!data.noOp) {\n        impl.swap(data.candidate->impl);','if(false) {\n        impl.swap(data.candidate->impl);',False),
  ('history_publication',runtime,'history.PublishPrepared(source,out);','(void)out;',False),
  ('candidate_layout',runtime,'!candidate.Layout(options.viewport,options.seconds) ||','false ||',False),
  ('candidate_diagnostic',runtime,'|| candidate.impl->preparationFailed','',False),
  ('context_diagnostic_owner',runtime,'system.time = time;system.preparationFailure=failure;','system.time = time;if(failure)system.preparationFailure=failure;',False),
  ('native_vacancy',interaction,'if(nativeModel)return DocumentVacancy::Bound;','if(false)return DocumentVacancy::Bound;',False),
  ('cache_source_path',view,'if(changed){view->source.swap(data.text);view->path.swap(data.path);view->canonical=true;view->failed=false;}','if(false){view->source.swap(data.text);view->path.swap(data.path);view->canonical=true;view->failed=false;}',False),
  ('resource_generation',view,'restartGeneration==renderSystem->GetVideoRestartCount() &&','true &&',False),
  ('resource_candidate_cleanup',view,'TouchEditResources();\n    for(auto* view:views)view->runtime->AbortPreparedDocument();','TouchEditResources();',False),
  ('deferred_shutdown',view,'if(editShutdownPending){editShutdownPending=false;RetainedUI_Shutdown();}','',False),
  ('owner_callback_clear',view,'view->callback=nullptr;view->owner=nullptr;','',False),
  ('callback_apply_current',runtime,'if(!current())return false;\n\t\t\tauto previous = applied.find(key);','auto previous = applied.find(key);',False),
  ('callback_control_association',runtime,'impl->ownerCanvas=canvas;','',False),
  ('callback_busy',runtime,'owner->preparing=true;','owner->preparing=false;',False),
  ('service_unwind_cleanup',view,'~EditCall(){--editCallDepth;DrainEditViews();}','~EditCall(){--editCallDepth;}',False),
  ('retire_runtime_owner',view,'view->runtime->RetireCanvasOwner();','',False),
  ('busy_identity',view,'view->runtime->HasActiveCanvasCallback() || !view->runtime->IsLoaded()','!view->runtime->IsLoaded()',False),
  ('legacy_layout',runtime,'if (!canonical) return true; // Valid legacy RML has no canonical interaction work.','if (!canonical) return false;',False),

 ]
 if args.select:
  missing=set(args.select)-{case[0] for case in cases}
  if missing:raise RuntimeError('Unknown mutation selections: '+repr(missing))
  cases=[case for case in cases if case[0] in args.select]
 results=[]
 for name,path,old,new,crash in cases:
  original=path.read_text();assert original.count(old)==1,(name,original.count(old))
  folder=out/name;folder.mkdir();altered=original.replace(old,new);projected=base/'ViewMethods.inc'
  if path==view:
   text=projected.read_text();assert text.count(old)==1,(name,'projection');(folder/'ViewMethods.inc').write_text(text.replace(old,new),newline='\n');unit=test;compile_unit=test
  else:
   unit=path;compile_unit=folder/path.name;compile_unit.write_text(altered,newline='\n')
  includes=[ROOT,folder,ROOT/'src/ui/retained',rml/'Include',rml/'Source/Core',jsonroot/'include',tess/'Include']
  others=[obj for source,obj in objects.items() if source!=unit]
  cmd='cl /nologo /MTd /DNDEBUG /DRMLUI_STATIC_LIB /DRMLUI_NO_THIRDPARTY_CONTAINERS /std:c++20 /EHsc /utf-8 /W3 /WX '+' '.join('/I"'+str(v)+'"' for v in includes)+' /Fo"'+str(folder)+'/" "'+str(compile_unit)+'" '+' '.join('"'+str(v)+'"' for v in others)+' /Fe"'+str(folder/'test.exe')+'" /link '+' '.join('"'+str(v)+'"' for v in libs)+' /SUBSYSTEM:CONSOLE user32.lib'
  batch=folder/'compile.cmd';batch.write_text('@echo off\ncall "'+str(repo/'tools/build/openq4_devcmd.cmd')+'"\nif errorlevel 1 exit /b %errorlevel%\n'+cmd+'\n')
  c=subprocess.run(['cmd','/c',str(batch)],cwd=folder,env=env,capture_output=True,text=True,encoding='utf-8',errors='replace');(folder/'compile.log').write_text(c.stdout+c.stderr);r=None
  if not c.returncode:
   r=subprocess.run([str(folder/'test.exe')],cwd=folder,env=env,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=120);(folder/'run.log').write_text(r.stdout+r.stderr)
  rejected=not c.returncode and r is not None and r.returncode!=0 and ('FAIL:' in r.stderr or crash)
  results.append({'name':name,'source':str(path),'compile_exit':c.returncode,'run_exit':None if r is None else r.returncode,'rejected':rejected,'command':cmd})
  print(('Rejected ' if rejected else 'INVALID ')+name,flush=True)
  if not rejected:break
 unchanged=all(sha(Path(name))==expected for name,expected in record['sources'].items())
 result={'passed':len(results)==len(cases) and all(v['rejected'] for v in results) and unchanged,'baseline':str(base/'result.json'),'baseline_sha256':sha(base/'result.json'),'runner_sha256':sha(Path(__file__)),'inputs_unchanged':unchanged,'mutations':results,'artifacts':{str(p.relative_to(out)):sha(p) for p in out.rglob('*') if p.is_file()},'scope':'Actual SDK-free Runtime/Rml/service CPU methods and copied exact baseline objects; no GPU/native activation.'}
 (out/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(out/'result.json',flush=True);return 0 if result['passed'] else 1
if __name__=='__main__':raise SystemExit(main())
