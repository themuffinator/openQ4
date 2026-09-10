#!/usr/bin/env python3
"""Compile actual schema-1/2 journal, effect plan, display and typed JSON methods.
No engine, hardware, persistence mutation or startup route executes. Domain maps
remain opaque; successful decoding certifies their envelope only.
"""
from pathlib import Path
import argparse,hashlib,json,os,subprocess,tempfile
from filesystem_case_segments import function_body
ROOT=Path(__file__).resolve().parents[2]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
 ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--repository',type=Path);ap.add_argument('--compiler',default='clang++');ap.add_argument('--sanitize',action='store_true');ap.add_argument('--no-mutations',action='store_true');a=ap.parse_args()
 repo=(a.repository or ROOT).resolve();(ROOT/'.tmp').mkdir(exist_ok=True);out=Path(tempfile.mkdtemp(prefix='settings-effect-journal-',dir=ROOT/'.tmp'))
 env=dict(os.environ,TEMP=str(out),TMP=str(out),TMPDIR=str(out))
 app=ROOT/'src/ui/application';doc=ROOT/'src/ui/retained/Document.cpp';jsonroot=repo/'subprojects/jsoncpp-1.9.6'
 paths=[Path(__file__),ROOT/'tools/tests/filesystem_case_segments.py',doc]+[p for p in (ROOT/'src/ui/retained').glob('*.h')]+[app/(name+ext) for name in ('SettingsJournal','SettingsEffectPlan','SystemDisplay','SystemSettingsHost','SettingsTransaction') for ext in ('.h','.cpp')]+[app/'SettingsValue.h']+[ROOT/'tools/tests/native'/n for n in ('UiSettingsEffectJournalTest.cpp','UiSettingsJournalTest.cpp','UiSettingsJournalExactTest.cpp')]+list((ROOT/'src/renderer').glob('*.h'))
 dependencies=[p for folder in ('include','src/lib_json') for p in (jsonroot/folder).rglob('*') if p.is_file() and p.suffix in ('.h','.cpp','.inl')]
 record={'passed':False,'sources':{str(p.relative_to(ROOT)):sha(p) for p in paths},'dependencies':{str(p.relative_to(repo)):sha(p) for p in dependencies},'runs':[],'mutants':{},'scope':__doc__}
 def run(name,cmd):
  p=subprocess.run([str(v) for v in cmd],cwd=out,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,encoding='utf-8',errors='replace')
  log=out/(name+'.log');log.write_text(p.stdout,encoding='utf-8');record['runs'].append({'name':name,'command':[str(v) for v in cmd],'exit':p.returncode,'log':str(log.relative_to(ROOT))});print(name,p.returncode,p.stdout[-250:],flush=True);return p
 msvc=Path(a.compiler).name.lower() in ('cl','cl.exe');includes=[ROOT,app,jsonroot/'include']
 flags=([a.compiler,'/nologo','/std:c++20','/EHsc','/W4','/WX','/wd4458','/permissive-','/MTd']+['/I'+str(p) for p in includes]) if msvc else [a.compiler,'-std=c++20','-O1','-Wall','-Wextra','-Werror','-Wno-unused-function']+sum((['-I',str(p)] for p in includes),[])
 if a.sanitize:flags+=['-fsanitize=address,undefined','-fno-omit-frame-pointer','-g0','-fno-pie']
 def compile_obj(label,source,upstream=False):
  obj=out/(label+('.obj' if msvc else '.o'));opts=flags[:]
  if upstream:opts=[x for x in opts if x not in ('/WX','-Werror')]
  cmd=opts+(['/c',str(source),'/Fo:'+str(obj)] if msvc else ['-c',source,'-o',obj])
  if run(label+'-compile',cmd).returncode:raise RuntimeError('Compilation: '+label)
  return obj
 def link_run(label,objs,mutant=False):
  exe=out/(label+'.exe');cmd=flags+list(objs)+(['/Fe:'+str(exe)] if msvc else ['-o',exe]+(['-no-pie'] if a.sanitize else []))
  if run(label+'-link',cmd).returncode:raise RuntimeError('Unlinked '+label)
  p=run(label+'-run',[exe]);
  if mutant:
   if not p.returncode or 'FAIL ' not in p.stdout:raise RuntimeError('Not a behavioral rejection: '+label)
   record['mutants'][label]={'exit':p.returncode,'output':p.stdout}
  elif p.returncode:raise RuntimeError('Runtime: '+label)
  return p.stdout
 try:
  document=doc.read_text(encoding='utf-8');names=('bool Identifier(','std::string PointerPart(','void Diagnose(','bool Utf8(','bool LexicalForms(','bool Parse(','bool ValidStateValue(','bool ParseStateValues(')
  parser='\n'.join(document[document.index(n):document.index('bool Parse(',document.index(n))] if n=='bool LexicalForms(' else function_body(document,n) for n in names)
  parsed=out/'parsed.cpp';parsed.write_text('#include "src/ui/retained/Document.h"\n#include <json/json.h>\n#include <cmath>\n#include <algorithm>\n#include <memory>\nnamespace openq4::ui { constexpr size_t MaxSourceBytes=16*1024*1024;\n'+parser+'\n}\n',encoding='utf-8')
  host=(app/'SystemSettingsHost.cpp').read_text(encoding='utf-8');legacy=host[host.index('constexpr int LegacyModes'):host.index('#if defined(USE_SDL3)',host.index('constexpr int LegacyModes'))]
  helpers=host[host.index('SystemSettingDescriptor Number('):host.index('bool Fail(')]+'\n'+'\n'.join(function_body(host,n) for n in ('bool SameValue(','bool Changed('))
  bodies='\n'.join(function_body(host,n) for n in ('const std::vector<SystemSettingDescriptor>& SystemSettingsHost::Catalog()','const std::map<std::string, size_t>& SystemSettingsHost::Schema()','unsigned SystemSettingsHost::ChangedEffects(','bool SystemSettingsHost::ResolveModeDimensions('))
  catalog=out/'catalog.cpp';catalog.write_text('#include "src/ui/application/SystemSettingsHost.h"\nnamespace openq4::ui { namespace {\n'+helpers+'\n'+legacy+'\n}\n'+bodies+'\n}\n',encoding='utf-8')
  display=(app/'SystemDisplay.cpp').read_text(encoding='utf-8').replace('#include "../../idlib/precompiled.h"','')
  display_path=out/'display.cpp';display_path.write_text(display,encoding='utf-8')
  journal=(app/'SettingsJournal.cpp').read_text(encoding='utf-8');plan=(app/'SettingsEffectPlan.cpp').read_text(encoding='utf-8')
  # Compile the preserved schema-1 implementation again under separate symbols.
  # This excludes all schema-2 bodies and uses the unchanged old version gate.
  old=journal[:journal.index('\nnamespace {\nconstexpr const char* EffectMagic')]+ '\n} // namespace openq4::ui\n'
  old=old.replace('EncodeSettingsJournal(', 'BaselineEncodeSettingsJournal(').replace('DecodeSettingsJournal(', 'BaselineDecodeSettingsJournal(')
  baseline=out/'baseline.cpp';baseline.write_text(old,encoding='utf-8')
  fixed=[compile_obj('parsed',parsed),compile_obj('catalog',catalog),compile_obj('baseline',baseline)]
  fixed += [compile_obj('json-'+name,jsonroot/'src/lib_json'/('json_'+name+'.cpp'),True) for name in ('reader','value','writer')]
  core={name:compile_obj(name,path) for name,path in [('journal',app/'SettingsJournal.cpp'),('plan',app/'SettingsEffectPlan.cpp'),('display',display_path)]}
  test=compile_obj('test',ROOT/'tools/tests/native/UiSettingsEffectJournalTest.cpp');record['positive']=link_run('positive',[test,*core.values(),*fixed])
  tx=compile_obj('transaction',app/'SettingsTransaction.cpp')
  for name in ('UiSettingsJournalTest','UiSettingsJournalExactTest'):
   t=compile_obj(name,ROOT/'tools/tests/native'/(name+'.cpp'));link_run(name,[t,tx,core['journal'],core['plan'],*fixed])
  dedicated=out/'dedicated.cpp';dedicated.write_text('#define ID_DEDICATED\n'+display+'\nint main(){openq4::ui::StateValues v;std::string error;return openq4::ui::ValidateDisplayPreserveActualPair(v,v,v,v,error)||error.empty()?1:0;}\n',encoding='utf-8')
  stub=compile_obj('dedicated',dedicated);link_run('dedicated',[stub])
  changes={
   'plan-empty':('plan','if (!changed) return Fail(error,"Settings effect plan has no changed setting");','if (false && !changed) return Fail(error,"Settings effect plan has no changed setting");'),
   'plan-floating-change':('plan','!SettingsValueEqual(old->second,next->second)','old->second!=next->second'),
   'plan-display-automatic':('plan','(candidate.domainMask & 1u)?','(candidate.domainMask & 0u)?'),
   'plan-resources-no-renderer':('plan','candidate.domainMask & 19u','candidate.domainMask & 3u'),
   'plan-preset-executor':('plan','candidate.changeMask & 31u','candidate.changeMask & 63u'),
   'journal-plan-unchecked':('journal','if (!ValidateSettingsEffectPlan(j.plan,j.baseline,j.target,catalog,error))','if (false && !ValidateSettingsEffectPlan(j.plan,j.baseline,j.target,catalog,error))'),
   'journal-patch-floating':('journal','!SettingsValuesEqual(expected,j.patch)','expected!=j.patch'),
   'journal-missing-metadata':('journal','if (required==item.map->empty())','if (false && required==item.map->empty())'),
   'journal-map-entry-budget':('journal','ValidMap(map,SettingsEffectMetadataMaxEntries,error)','ValidMap(map,1024,error)'),
   'journal-map-byte-budget':('journal','bytes>SettingsEffectMetadataMaxBytes','bytes>SettingsTransaction::MaxSnapshotBytes'),
   'journal-map-key-budget':('journal','key.size()>SettingsEffectMetadataMaxKeyBytes','key.size()>113'),
   'journal-placement-catalog':('journal','if (!SettingsValueEqual(fields.at(prefix+"width"),values.at("r_windowWidth")) ||','if (false && (!SettingsValueEqual(fields.at(prefix+"width"),values.at("r_windowWidth")) ||'),
   'journal-canonicality':('journal','if (canonical!=bytes)','if (false && canonical!=bytes)'),
   'journal-daz-zero':('journal','SettingsValueEqual(value,StateValue(0.0))?0.0:*number','*number==0.0?0.0:*number'),
   'journal-early-publication':('journal','std::unique_ptr<SettingsJournalValue> candidate;','journal.value.reset(); std::unique_ptr<SettingsJournalValue> candidate;'),
   'journal-early-output':('journal','if (!Valid2(journal,catalog,error))','bytes.clear(); if (!Valid2(journal,catalog,error))'),
   'display-preservation':('display','if (!SettingsValuesEqual(savedRestore,savedTarget))','if (false && !SettingsValuesEqual(savedRestore,savedTarget))'),
   'display-catalog-change':('display','if ((item.effects & SystemSettingDisplayRestart) &&','if (false && (item.effects & SystemSettingDisplayRestart) &&'),
  }
  if not a.no_mutations:
   for label,(unit,old,new) in changes.items():
    source={'journal':journal,'plan':plan,'display':display}[unit]
    if source.count(old)!=1:raise RuntimeError('Mutation anchor: '+label)
    source=source.replace(old,new)
    if label=='journal-placement-catalog':source=source.replace('values.at("r_windowHeight")))','values.at("r_windowHeight"))))')
    path=out/(label+'.cpp');path.write_text(source,encoding='utf-8');obj=compile_obj(label,path)
    link_run(label,[test,*[obj if k==unit else v for k,v in core.items()],*fixed],True)
  record['passed']=True
 finally:
  record['source_unchanged']=all(sha(ROOT/p)==digest for p,digest in record['sources'].items());record['artifacts']={str(p.relative_to(ROOT)):sha(p) for p in out.rglob('*') if p.is_file()}
  (out/'result.json').write_text(json.dumps(record,indent=2)+'\n',encoding='utf-8');print(out/'result.json',flush=True)
  if not record['source_unchanged']:raise RuntimeError('Source changed during validation')
if __name__=='__main__':main()
