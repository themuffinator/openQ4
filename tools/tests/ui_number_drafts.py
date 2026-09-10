#!/usr/bin/env python3
"""Compile real local Number draft guards plus production Runtime wrapper seams.

No native text document, SDL, OS clipboard, game, GPU, or settings host is used.
The Runtime unit counts fresh-source/eligibility/feedback/reveal boundaries while
running actual Interaction/TextEdit. Main native test links unchanged core code.
"""
from pathlib import Path
import hashlib,json,os,shutil,subprocess,tempfile
from filesystem_case_segments import function_body
ROOT=Path(__file__).resolve().parents[2]
CORE=ROOT/'src/ui/retained'
def runtime_unit():
    cpp=(CORE/'Runtime.cpp').read_text(encoding='utf-8')
    tests=(ROOT/'tools/tests/native/UiNumberDraftTest.cpp').read_text(encoding='utf-8')
    fixture=tests[:tests.index('static NumberDraftSummary Query(')]
    methods='\n'.join(function_body(cpp,'bool Runtime::'+name+'(') for name in ('QueryNumberDrafts','DiscardNumberDrafts','FocusNumberDraft'))
    prepare=function_body(cpp,'bool PrepareNumberEdit(')
    return fixture+'''\nstruct GuardImpl {
      bool canonical=true,document=true,pointerNavigation=true,valid=true;
      Interaction interaction; std::map<std::string,ControlReadback> readbacks;std::map<std::string,ControlBounds> bounds;
      explicit GuardImpl(Interaction&& owned):interaction(std::move(owned)){}
      std::string stateError;unsigned reads=0,updates=0,reveals=0,feedback=0,focusLayouts=0;
      bool layoutValid=true;
      void ReadStateSources(){++reads;if(!valid){stateError="bad host value";return;}stateError.clear();interaction.SetReadbacks(readbacks,stateError);}
      void UpdateInteraction(double){++updates;interaction.SetBounds(bounds);}
      bool RefreshNumberFocusLayout(std::string& error){++focusLayouts;if(!layoutValid){error="bad layout";return false;}interaction.SetBounds(bounds,true);return true;}
      void RevealFocus(){++reveals;}void Feedback(double){++feedback;}
    '''+prepare+'''};
    class Runtime {public:GuardImpl* impl;
      bool QueryNumberDrafts(NumberDraftSummary&,std::string&,double);
      bool DiscardNumberDrafts(const NumberDraftBarrier&,std::string&,double);
      bool FocusNumberDraft(const NumberDraftBarrier&,const std::string&,std::string&,double);
    };
    '''+methods+'''
    int main(){
      Fixture f;f.Begin();f.Text("-");f.input.Cancel();GuardImpl impl{std::move(f.input)};impl.readbacks=f.readbacks;impl.bounds=f.bounds;Runtime runtime{&impl};
      NumberDraftSummary result;std::string error;
      Check(runtime.QueryNumberDrafts(result,error,1)&&result.blocking.size()==1&&impl.reads==1&&impl.updates==1,"query observes host before inventory");
      const auto before=result;impl.valid=false;
      Check(!runtime.QueryNumberDrafts(result,error,2)&&result.barrier==before.barrier&&impl.updates==1,"failed host query preserves output and skips eligibility mutation");
      Check(!runtime.DiscardNumberDrafts(before.barrier,error,2)&&impl.interaction.Widget("n0")->number,"bad host blocks discard");
      Check(!runtime.FocusNumberDraft(before.barrier,"n0",error,2)&&!impl.reveals,"bad host blocks focus");
      impl.valid=true;for(double time:{-1.0,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}){
        auto reads=impl.reads;Check(!runtime.QueryNumberDrafts(result,error,time)&&impl.reads==reads&&result.barrier==before.barrier,"invalid time rejected before host read");}
      impl.canonical=false;Check(!runtime.QueryNumberDrafts(result,error,3),"raw RML has no local numeric guard");impl.canonical=true;
      impl.readbacks["n0"].value=1.5;
      Check(!runtime.DiscardNumberDrafts(before.barrier,error,3)&&impl.interaction.Widget("n0")->number,"fresh external value invalidates stale barrier before deletion");
      Check(runtime.QueryNumberDrafts(result,error,3)&&result.blocking[0].conflict,"fresh conflict reported");
      impl.layoutValid=false;Check(!runtime.FocusNumberDraft(result.barrier,"n0",error,3)&&!impl.reveals,"unusable layout blocks explicit focus");impl.layoutValid=true;
      Check(runtime.FocusNumberDraft(result.barrier,"n0",error,3)&&impl.reveals==1&&!impl.pointerNavigation&&impl.focusLayouts==2,"successful exact focus refreshes layout, reveals and takes keyboard navigation");
      Check(impl.interaction.Widget("n0")->number->state.text=="-"&&impl.interaction.Widget("n0")->number->conflict,"focus does not replace conflicted text");
      Check(runtime.QueryNumberDrafts(result,error,3)&&runtime.DiscardNumberDrafts(result.barrier,error,3),"fresh exact discard");
      Check(!impl.interaction.Widget("n0")->number&&std::get<double>(impl.interaction.Widget("n0")->accepted)==1.5,"discard preserves host value");
      std::printf("Number Runtime guard wrappers: %u checks passed (counted host/layout, actual local core).\\n",checks);
    }
    '''
def main():
    compiler=shutil.which('clang++') or shutil.which('g++');assert compiler
    temp=Path(tempfile.mkdtemp(prefix='number-drafts-',dir=ROOT/'.tmp'))
    env={**os.environ,'TEMP':str(temp),'TMP':str(temp),'TMPDIR':str(temp)}
    digest=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    paths=[CORE/n for n in ('Interaction.h','Interaction.cpp','Runtime.h','Runtime.cpp','Document.h','Document.cpp','TextInput.h','TextInput.cpp','TextEdit.h','TextEdit.cpp','TextEditCommand.h','NativeTextDocument.h','NativeTextDocument.cpp','NativeTextEditor.h','NativeTextEditor.cpp')]+[ROOT/'tools/tests/native/UiNumberDraftTest.cpp',Path(__file__)]
    sources={p.relative_to(ROOT).as_posix():digest(p) for p in paths}
    doc=(CORE/'Document.cpp').read_text(encoding='utf-8');valid=temp/'valid.cpp'
    valid.write_text('#include "Interaction.h"\n#include <cmath>\nnamespace openq4::ui {\n'+function_body(doc,'bool Utf8(')+function_body(doc,'bool ValidStateValue(')+'}\n',encoding='utf-8',newline='\n')
    production=(CORE/'Interaction.cpp').read_text(encoding='utf-8');native=(ROOT/'tools/tests/native/UiNumberDraftTest.cpp').read_text(encoding='utf-8')
    cases=[('native',native,production,False),('runtime-wrappers',runtime_unit(),production,False)]
    mutations=[
      ('inactive-ignored','if (!item.number) continue;','if (!item.number || item.number->detached) continue;'),
      ('discard-stale-accepted','if (expected != current.barrier) { error = "Number draft barrier is stale"; return false; }','if (false) { error = "Number draft barrier is stale"; return false; }'),
      ('pending-not-blocking','if (!dirty && !editor.conflict && !item.pending && !composing && !nativeUnsettled) continue;','if (item.pending || (!dirty && !editor.conflict && !composing && !nativeUnsettled)) continue;'),
      ('readback-stamp-not-updated','if (changed && item.number) item.number->draftRevision = revisions.at(id);','if (false) item.number->draftRevision = revisions.at(id);'),
      ('proposal-stamp-not-updated','if (item.number) item.number->draftRevision = token;','if (false) item.number->draftRevision = token;'),
      ('detach-stamp-not-updated','item.number->draftRevision = ProposalToken();','item.number->draftRevision = item.number->draftRevision;'),
      ('inventory-aba','if (item.number) numberEpoch = ProposalToken();','if (false) numberEpoch = ProposalToken();'),
      ('restore-epoch-reused','candidate.numberEpoch = ProposalToken();','candidate.numberEpoch = numberEpoch;'),
      ('budget-truncated','candidate.barrier.editors.size() >= ValueWidgetSnapshot::MaxNumberEditors','candidate.barrier.editors.size() > ValueWidgetSnapshot::MaxNumberEditors'),
    ]
    for name,old,new in mutations:
      assert old in production,name;cases.append((name,native,production.replace(old,new),True))
    report={'passed':False,'scope':__doc__,'sources':sources,'cases':[]}
    try:
      for name,body,core,mutant in cases:
        source=temp/(name+'.cpp');source.write_text(body,encoding='utf-8',newline='\n');part=temp/(name+'-interaction.cpp');part.write_text(core,encoding='utf-8',newline='\n');binary=temp/(name+('.exe' if os.name=='nt' else '-test'))
        command=[compiler,'-std=c++20','-I',str(ROOT),'-I',str(CORE),str(source),str(part),str(valid),str(CORE/'TextInput.cpp'),str(CORE/'TextEdit.cpp'),str(CORE/'NativeTextDocument.cpp'),str(CORE/'NativeTextEditor.cpp'),'-o',str(binary)]
        if os.name!='nt':command[1:1]=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
        built=subprocess.run(command,env=env,capture_output=True,text=True,timeout=120);log=temp/(name+'-compile.log');log.write_text(built.stdout+built.stderr,encoding='utf-8')
        entry={'name':name,'compile_exit':built.returncode,'compile_log':str(log),'compile_log_sha256':digest(log),'command':command,'source_sha256':digest(source),'interaction_sha256':digest(part)};report['cases'].append(entry)
        if built.returncode:raise RuntimeError(built.stdout+built.stderr)
        run=subprocess.run([str(binary)],env=env,capture_output=True,text=True,timeout=90);log=temp/(name+'-run.log');log.write_text(run.stdout+run.stderr,encoding='utf-8');entry.update(exit=run.returncode,run_log=str(log),run_log_sha256=digest(log),binary_sha256=digest(binary))
        if mutant:
          if not run.returncode or 'FAIL:' not in run.stdout+run.stderr:raise RuntimeError('Mutation survived or failed without a check: '+name+' '+run.stdout+run.stderr)
          print('Rejected compiled mutation:',name,flush=True)
        else:
          if run.returncode:raise RuntimeError(run.stdout+run.stderr)
          print(run.stdout.strip(),flush=True)
      report['passed']=True
    except Exception as e:report['failure']=str(e);print(e,flush=True)
    report['sources_unchanged']=sources=={p.relative_to(ROOT).as_posix():digest(p) for p in paths};report['passed'] &= report['sources_unchanged']
    result=temp/'result.json';result.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8');print(result,flush=True);return 0 if report['passed'] else 1
if __name__=='__main__':raise SystemExit(main())
