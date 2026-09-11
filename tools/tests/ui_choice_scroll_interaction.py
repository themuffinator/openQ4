#!/usr/bin/env python3
"""Run actual portable Choice scrollbar Interaction and ScrollGeometry. No layout, input device or renderer."""
from pathlib import Path
import argparse,hashlib,json,os,subprocess,tempfile
from filesystem_case_segments import function_body
ROOT=Path(__file__).resolve().parents[2]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
 a=argparse.ArgumentParser(description=__doc__);a.add_argument('--compiler',default='clang++');a.add_argument('--msvc',action='store_true');a.add_argument('--sanitize',action='store_true');a.add_argument('--mutations',action='store_true');args=a.parse_args()
 out=Path(tempfile.mkdtemp(prefix='choice-scroll-interaction-',dir=ROOT/'.tmp'));core=ROOT/'src/ui/retained';env={**os.environ,'TEMP':str(out),'TMP':str(out),'TMPDIR':str(out)}
 doc=(core/'Document.cpp').read_text(encoding='utf-8');valid=out/'valid.cpp';valid.write_text('#include "Interaction.h"\n#include <cmath>\nnamespace openq4::ui {\n'+function_body(doc,'bool Utf8(')+function_body(doc,'bool ValidStateValue(')+'}\n',newline='\n')
 sources=[core/(name+'.cpp') for name in ['Interaction','ScrollGeometry','TextInput','TextEdit','TextEditCommand','NativeTextDocument','NativeTextEditor','Input']]+[valid,ROOT/'tools/tests/native/UiChoiceScrollInteractionTest.cpp']
 bound=sources+[Path(__file__).resolve(),core/'Document.cpp']+list(core.glob('*.h'))
 before={str(p):sha(p) for p in bound};exe=out/'test.exe'
 flags=[args.compiler,'/nologo','/std:c++20','/EHsc','/MTd','/W3','/I'+str(core),'/I'+str(ROOT)] if args.msvc else [args.compiler,'-std=c++20','-I',str(core),'-I',str(ROOT)]
 if args.sanitize:flags+=['-fsanitize=address,undefined','-fno-omit-frame-pointer','-g0','-no-pie']
 commands=[];mutations=[];cached={}
 def execute(name,units):
  folder=out/name;folder.mkdir();binary=folder/'test.exe';objects=[];logs=[];steps=[];c=None
  for index,unit in enumerate(units):
   if unit in cached:objects.append(cached[unit]);continue
   obj=folder/(str(index)+('.obj' if args.msvc else '.o'))
   command=flags+(['/c',str(unit),'/Fo:'+str(obj)] if args.msvc else ['-c',str(unit),'-o',str(obj)])
   c=subprocess.run(command,cwd=folder,env=env,text=True,encoding='utf-8',errors='replace',capture_output=True);logs.append(c.stdout+c.stderr);steps.append(command)
   if c.returncode:break
   cached[unit]=obj;objects.append(obj)
  else:
   command=flags+[str(p) for p in objects]+(['/Fe:'+str(binary)] if args.msvc else ['-o',str(binary)])
   c=subprocess.run(command,cwd=folder,env=env,text=True,encoding='utf-8',errors='replace',capture_output=True);logs.append(c.stdout+c.stderr);steps.append(command)
  (folder/'compile.log').write_text(''.join(logs),encoding='utf-8');r=None
  if c.returncode==0:
   r=subprocess.run([str(binary)],cwd=folder,env=env,text=True,encoding='utf-8',errors='replace',capture_output=True);(folder/'run.log').write_text(r.stdout+r.stderr,encoding='utf-8')
  commands.append({'name':name,'command':steps,'compile_exit':c.returncode,'run_exit':None if r is None else r.returncode})
  return c,r
 compile,r=execute('positive',sources);command=commands[0]['command']
 if r:print(r.stdout+r.stderr,flush=True)
 else:print(compile.stdout+compile.stderr,flush=True)
 error=None
 if args.mutations and r is not None and r.returncode==0:
  rules=[
   ('semantic_focus', 'focusPending || focused != id || !Eligible(id) || !popup.empty()', 'focusPending || !Eligible(id) || !popup.empty()'),
   ('semantic_held', 'return !pointerHeld && !acceptHeld && !backHeld && !pointerArm &&', 'return !pointerHeld && !backHeld && !pointerArm &&'),
   ('semantic_close_identity', 'if (!expected || expected != popupToken || popup != id || !PopupInputIdle()) return false;', 'if (!expected || popup != id || !PopupInputIdle()) return false;'),
   ('semantic_scroll_identity', 'if (!expected || expected != popupToken || popup != id || !PopupInputIdle() || !ChoiceScrollReady(id)) return false;', 'if (!expected || popup != id || !PopupInputIdle() || !ChoiceScrollReady(id)) return false;'),
   ('semantic_geometry', 'if (!geometryToken || geometryToken != readback.geometryToken) return false;', 'if (!geometryToken) return false;'),
   ('semantic_legacy_opening', 'view.highlight = highlight; view.popupToken = popupToken;', 'view.highlight = highlight;'),
   ('open_identity', 'token!=popupToken ||', 'false ||'),
   ('revision_identity', 'revision!=found->second.choiceRevision ||', 'false ||'),
   ('fresh_layout', 'found->second.choiceLayoutRevision==found->second.choiceRevision &&', 'true &&'),
   ('monotonic_layout', 'value.geometryToken<found->second.scroll->geometryToken', 'false'),
   ('immutable_layout', 'if(found->second.scroll && value.geometryToken==found->second.scroll->geometryToken)', 'if(false && found->second.scroll)'),
   ('grab_fraction', 'scrollGrabFraction=std::clamp((pointer-g.position)/g.thumb,0.0,1.0);dragging=popup;', 'scrollGrabFraction=.5;dragging=popup;'),
   ('matching_release', 'if(choiceScrollArm) {EndChoiceScrollGesture();Refresh();return;}', 'if(false) {EndChoiceScrollGesture();Refresh();return;}'),
   ('source_cancel', 'if(popup==id && HasChoiceBar(item.control) && (changed || item.readback->enabledOptions!=value.enabledOptions))CancelGesture();', 'if(false)CancelGesture();'),
  ]
  original=(core/'Interaction.cpp').read_text()
  try:
   for name,old,new in rules:
    if original.count(old)!=1:raise RuntimeError('Mutation anchor is not unique: '+name)
    altered=out/(name+'.cpp');altered.write_text(original.replace(old,new),newline='\n')
    c,m=execute(name,[altered if p==core/'Interaction.cpp' else p for p in sources]);rejected=c.returncode==0 and m is not None and m.returncode!=0 and 'FAIL ' in m.stderr
    mutations.append({'name':name,'rejected':rejected,'source_sha256':sha(altered),'compile_exit':c.returncode,'run_exit':None if m is None else m.returncode})
    print(('Rejected ' if rejected else 'INVALID ')+'compiled mutation '+name,flush=True)
    if not rejected:raise RuntimeError('Mutation did not fail behavioral assertion: '+name)
  except Exception as failure:error=str(failure)
 # Fault only the real monotonic token allocator's return boundary. All caller
 # methods remain actual source; this is separate from the unchanged positive suite.
 fault_output=None
 if not error and r is not None and r.returncode==0:
  original=(core/'Interaction.cpp').read_text()
  anchor='std::uint64_t ProposalToken() {\n'
  if original.count(anchor)!=1:raise RuntimeError('Token allocator fault anchor changed')
  fault=out/'token-fault.cpp';fault.write_text('extern bool ChoiceTokenFault;\n'+original.replace(anchor,anchor+'    if (::ChoiceTokenFault) return 0;\n'),newline='\n')
  test=out/'token-test.cpp'
  test.write_text('#define main OriginalChoiceMain\n#include "'+(ROOT/'tools/tests/native/UiChoiceScrollInteractionTest.cpp').as_posix()+'"\n#undef main\nbool ChoiceTokenFault=false;\nint main(){Fixture f;f.Layout();auto before=f.View();ChoiceTokenFault=true;f.Key(MenuInput::End);CHECK(!f.View().popupOpen);CHECK(f.View().popupRevision==before.popupRevision&&f.View().popupRevision!=0);CHECK(!f.input.ChoiceScrollReady("choice"));CHECK(f.input.TakeActions().empty());f.Key(MenuInput::Accept);CHECK(!f.View().popupOpen);std::printf("PASS %u token exhaustion checks\\n",checks);return 0;}\n',newline='\n')
  units=[fault if p==core/'Interaction.cpp' else test if p==sources[-1] else p for p in sources]
  c,f=execute('token-exhaustion',units);fault_output=f.stdout if f else None
  if c.returncode or f is None or f.returncode:error='Token exhaustion actual-caller regression failed'
  else:
   print(f.stdout,flush=True)
   if args.mutations:
    old='const auto revision=ProposalToken();if(!revision){CancelGesture();return;}'
    if original.count(old)!=1:raise RuntimeError('Choice reveal guard anchor changed')
    mutant=out/'token-guard-mutant.cpp';mutant.write_text(fault.read_text().replace(old,'const auto revision=ProposalToken();'),newline='\n')
    c,m=execute('token_guard',[mutant if p==fault else p for p in units]);rejected=c.returncode==0 and m is not None and m.returncode!=0 and 'FAIL ' in m.stderr
    mutations.append({'name':'token_guard','rejected':rejected,'source_sha256':sha(mutant),'compile_exit':c.returncode,'run_exit':None if m is None else m.returncode})
    print(('Rejected ' if rejected else 'INVALID ')+'compiled mutation token_guard',flush=True)
    if not rejected:error='Token exhaustion guard mutation escaped'
 stable=before=={str(p):sha(p) for p in bound};result={'passed':not error and stable and compile.returncode==0 and r is not None and r.returncode==0,'commands':commands,'mutations':mutations,'error':error,'command':command,'compile_exit':compile.returncode,'run_exit':r.returncode if r else None,'output':r.stdout if r else None,'token_fault_output':fault_output,'sources':before,'unchanged':stable,'artifacts':{str(p.relative_to(out)):sha(p) for p in out.rglob('*') if p.is_file()},'scope':__doc__};(out/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(out/'result.json');return 0 if result['passed'] else 1
if __name__=='__main__':raise SystemExit(main())
