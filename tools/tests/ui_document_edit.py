#!/usr/bin/env python3
"""Actual portable canonical Document/DocumentEdit source/history tests. No UI shell, OS input or rendering."""
from pathlib import Path
import argparse,hashlib,json,os,subprocess,tempfile,sys
sys.stdout.reconfigure(encoding="utf-8",errors="replace")
ROOT=Path(__file__).resolve().parents[2]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
 a=argparse.ArgumentParser(description=__doc__);a.add_argument('--repository',type=Path,default=ROOT);a.add_argument('--compiler',default='clang++');a.add_argument('--msvc',action='store_true');a.add_argument('--sanitize',action='store_true');a.add_argument('--mutations',action='store_true');args=a.parse_args()
 repository=args.repository.resolve();out=Path(tempfile.mkdtemp(prefix='document-edit-',dir=ROOT/'.tmp'));core=ROOT/'src/ui/retained';jsonroot=repository/'subprojects/jsoncpp-1.9.6'
 tessroot=repository/'subprojects/libtess2-8dbd6483e920311a58c9af10a10beb278efebc36'
 env={**os.environ,'TEMP':str(out),'TMP':str(out),'TMPDIR':str(out)}
 sources=[core/(name+'.cpp') for name in ['Document','DocumentEdit','State','Vector','Motion','Presentation']]+[jsonroot/'src/lib_json'/name for name in ['json_reader.cpp','json_value.cpp','json_writer.cpp']]+[ROOT/'tools/tests/native/UiDocumentEditTest.cpp']
 sources+=sorted((tessroot/'Source').glob('*.c'))
 bound=sources+[Path(__file__).resolve()]+list(core.glob('*.h'))+list((jsonroot/'include').rglob('*.h'))+list((jsonroot/'src/lib_json').glob('*.h'));bound+=list((tessroot/'Include').glob('*.h'))+list((tessroot/'Source').glob('*.h'));before={str(p):sha(p) for p in bound}
 flags=[args.compiler,'/nologo','/std:c++20','/EHsc','/MTd','/utf-8','/W3','/I'+str(core),'/I'+str(jsonroot/'include')] if args.msvc else [args.compiler,'-std=c++20','-I',str(core),'-I',str(jsonroot/'include')]
 flags+=['/I'+str(tessroot/'Include')] if args.msvc else ['-I',str(tessroot/'Include')]
 if args.sanitize:flags+=['-fsanitize=address,undefined','-fno-omit-frame-pointer','-g0','-no-pie']
 cached={};commands=[];mutations=[]
 def execute(name,units):
  folder=out/name;folder.mkdir();binary=folder/'test.exe';objects=[];log=[];steps=[]
  for unit in units:
   if unit in cached:objects.append(cached[unit]);continue
   obj=folder/(unit.stem+('.obj' if args.msvc else '.o'));unit_flags=flags
   if unit.suffix=='.c':unit_flags=[v for v in flags if v not in ['/std:c++20','-std=c++20','/EHsc']]+(['/TC','/std:c17'] if args.msvc else ['-x','c','-std=c17'])
   command=unit_flags+(['/c',str(unit),'/Fo:'+str(obj)] if args.msvc else ['-c',str(unit),'-o',str(obj)])
   c=subprocess.run(command,cwd=folder,env=env,text=True,encoding='utf-8',errors='replace',capture_output=True);steps.append(command);log.append(c.stdout+c.stderr)
   if c.returncode:break
   objects.append(obj);cached[unit]=obj
  else:
   command=flags+[str(p) for p in objects]+(['/Fe:'+str(binary)] if args.msvc else ['-o',str(binary)])
   c=subprocess.run(command,cwd=folder,env=env,text=True,encoding='utf-8',errors='replace',capture_output=True);steps.append(command);log.append(c.stdout+c.stderr)
  (folder/'compile.log').write_text(''.join(log),encoding='utf-8');r=None
  if c.returncode==0:
   r=subprocess.run([str(binary)],cwd=folder,env=env,text=True,encoding='utf-8',errors='replace',capture_output=True);(folder/'run.log').write_text(r.stdout+r.stderr,encoding='utf-8')
  commands.append({'name':name,'command':steps,'compile_exit':c.returncode,'run_exit':None if r is None else r.returncode});return c,r
 c,r=execute('positive',sources);print((r.stdout+r.stderr) if r else (c.stdout+c.stderr),flush=True);error=None
 if args.mutations and r is not None and r.returncode==0:
  rules=[
   ('exact_identity','Current() && expected==impl->identity','Current()'),
   ('publish_revision','data->receipt={expected,{expected.document,expected.revision+1},true,impl->cursor+1,0,bytes};','data->receipt={expected,expected,true,impl->cursor+1,0,bytes};'),
   ('history_revision','if(redo)++impl->cursor;else --impl->cursor;++impl->identity.revision;','if(redo)++impl->cursor;else --impl->cursor;'),
   ('no_op','if(source==Current()->Source())','if(false)'),
   ('document_identity','candidate->Model().id==Current()->Model().id','true'),
   ('count_budget','operations.size()<=impl->limits.steps','true'),
   ('state_budget','impl->cursor+2<=impl->limits.states','true'),
   ('history_budget','Add(bytes,impl->states[i]->Source().size(),impl->limits.historyBytes)','Add(bytes,impl->states[i]->Source().size(),impl->limits.workingSourceBytes)'),
   ('redo_branch','impl->states.begin()+impl->cursor+1','impl->states.end()'),
   ('array_trivia','Splice(source,node,{},budget);Splice(source,{comma,comma+1},{},budget);','Splice(source,{comma,node.end},{},budget);'),
   ('fragment_span','return supplied.substr(span.start,span.end-span.start);','return supplied;'),
   ('revision_exhaustion','impl->identity.revision<impl->limits.revision','true'),
   ('prepared_origin','data.receipt.before==impl->identity','true'),
   ('prepared_consumed','data.ready && data.owner.lock()==impl','true && data.owner.lock()==impl'),
   ('prepared_cursor','impl->cursor=data.receipt.undo;','impl->cursor=0;'),
   ('prepared_retention','impl->outstanding.expired()','true'),
   ('prepared_owner_death','const auto owner=data->owner.lock();','const auto owner=data->owner.lock();if(!owner)return true;'),
  ]
  original=(core/'DocumentEdit.cpp').read_text()
  try:
   for name,old,new in rules:
    if original.count(old)!={'exact_identity':2,'revision_exhaustion':2,'prepared_retention':2}.get(name,1):raise RuntimeError('Nonunique mutation anchor '+name)
    altered=out/(name+'.cpp');altered.write_text(original.replace(old,new,1),newline='\n')
    mc,m=execute(name,[altered if p==core/'DocumentEdit.cpp' else p for p in sources]);rejected=mc.returncode==0 and m is not None and m.returncode!=0 and 'FAIL ' in m.stderr
    mutations.append({'name':name,'compiled':mc.returncode==0,'rejected':rejected,'source_sha256':sha(altered)});print(('Rejected ' if rejected else 'INVALID ')+'compiled mutation '+name,flush=True)
    if not rejected:raise RuntimeError('Mutation escaped '+name)
  except Exception as failure:error=str(failure)
 token_output=None
 if not error and r is not None and r.returncode==0:
  original=(core/'DocumentEdit.cpp').read_text();anchor='std::uint64_t DocumentToken() noexcept {\n'
  if original.count(anchor)!=1:raise RuntimeError('Lifetime token fault anchor changed')
  fault=out/'token-fault.cpp';fault.write_text('extern bool DocumentEditTokenFault;\n'+original.replace(anchor,anchor+'    if (::DocumentEditTokenFault) return 0;\n'),newline='\n')
  test=out/'token-test.cpp';test.write_text('#define main OriginalDocumentEditMain\n#include "'+(ROOT/'tools/tests/native/UiDocumentEditTest.cpp').as_posix()+'"\n#undef main\nbool DocumentEditTokenFault=false;\nint main(){Fixture before;DocumentEdit e;std::vector<Diagnostic>d;DocumentEditTokenFault=true;CHECK(!e.Open(Source(),{},d));CHECK(!e.Current()&&e.Identity()==DocumentEditIdentity{}&&e.HistorySourceBytes()==0);CHECK(!e.Open(Source(),{},d));DocumentEditTokenFault=false;CHECK(e.Open(Source(),{},d));CHECK(e.Identity().document>before.edit.Identity().document&&e.Identity().revision==1);std::printf("PASS %u lifetime-token checks\\n",checks);return 0;}\n',newline='\n')
  test_source=ROOT/'tools/tests/native/UiDocumentEditTest.cpp'
  units=[fault if p==core/'DocumentEdit.cpp' else test if p==test_source else p for p in sources]
  tc,tr=execute('token_exhaustion',units);token_output=tr.stdout if tr else None
  if tc.returncode or tr is None or tr.returncode:error='Lifetime token regression failed'
  else:
   print(tr.stdout,flush=True)
   if args.mutations:
    anchor='Require(token!=0,"Document lifetime tokens exhausted");'
    if original.count(anchor)!=1:raise RuntimeError('Token guard changed')
    mutant=out/'token-guard.cpp';mutant.write_text(fault.read_text().replace(anchor,''),newline='\n')
    mc,m=execute('token_guard',[mutant if p==fault else p for p in units]);rejected=mc.returncode==0 and m is not None and m.returncode!=0 and 'FAIL ' in m.stderr
    mutations.append({'name':'token_guard','compiled':mc.returncode==0,'rejected':rejected,'source_sha256':sha(mutant)})
    print(('Rejected ' if rejected else 'INVALID ')+'compiled mutation token_guard',flush=True)
    if not rejected:error='Token guard mutation escaped'
 preflight_output=None
 if not error and r is not None and r.returncode==0:
  actual=core/'DocumentEdit.cpp'
  prefix='#define main OriginalDocumentEditMain\n#include "'+(ROOT/'tools/tests/native/UiDocumentEditTest.cpp').as_posix()+'"\n#undef main\n'
  body="int main(){std::string source=\"[]\",value(4096,'x');DocumentEditLimits limits;limits.workingSourceBytes=4;Budget budget{limits,0};bool refused=false,allocated=false;deny=true;try{InsertAdded(source,1,\"\",value,\",\",budget);}catch(const Refused&){refused=true;}catch(const std::bad_alloc&){allocated=true;}deny=false;CHECK(refused&&!allocated&&source==\"[]\");limits.workingSourceBytes=128*1024;InsertAdded(source,1,\"\",value,\",\",budget);CHECK(source.size()==4099&&source[1]=='x'&&source[source.size()-2]==',');std::printf(\"PASS %u syntax-preflight checks\\n\",checks);return 0;}\n"
  test=out/'preflight-test.cpp';test.write_text(prefix+'#include "'+actual.as_posix()+'"\n'+body,newline='\n')
  units=[test if p==ROOT/'tools/tests/native/UiDocumentEditTest.cpp' else p for p in sources if p!=actual]
  pc,pr=execute('syntax_preflight',units);preflight_output=pr.stdout if pr else None
  if pc.returncode or pr is None or pr.returncode:error='Syntax source budget regression failed'
  else:
   print(pr.stdout,flush=True)
   if args.mutations:
    raw=actual.read_text();anchor='    budget.Check(next,addedSize);'
    if raw.count(anchor)!=1:raise RuntimeError('Preflight budget anchor changed')
    mutant=out/'preflight-mutant.cpp';mutant.write_text(raw.replace(anchor,''),newline='\n')
    mt=out/'preflight-mutant-test.cpp';mt.write_text(prefix+'#include "'+mutant.as_posix()+'"\n'+body,newline='\n')
    mc,m=execute('syntax_budget',[mt if p==test else p for p in units]);rejected=mc.returncode==0 and m is not None and m.returncode!=0 and 'FAIL ' in m.stderr
    mutations.append({'name':'syntax_budget','compiled':mc.returncode==0,'rejected':rejected,'source_sha256':sha(mutant)});print(('Rejected ' if rejected else 'INVALID ')+'compiled mutation syntax_budget',flush=True)
    if not rejected:error='Syntax preflight mutation escaped'
 stable=before=={str(p):sha(p) for p in bound};record={'passed':not error and stable and c.returncode==0 and r is not None and r.returncode==0,'error':error,'commands':commands,'mutations':mutations,'output':r.stdout if r else None,'token_output':token_output,'preflight_output':preflight_output,'sources':before,'unchanged':stable,'artifacts':{str(p.relative_to(out)):sha(p) for p in out.rglob('*') if p.is_file()},'scope':__doc__}
 (out/'result.json').write_text(json.dumps(record,indent=2)+'\n');print(out/'result.json');return 0 if record['passed'] else 1
if __name__=='__main__':raise SystemExit(main())
