#!/usr/bin/env python3
"""Actual native expression/fixup/evaluation and observation/command methods.

The native parser token queue, window construction/registration, register-table
transport and VFS are counted doubles. Full GUI Parse/Init and engine rendering
are not executed. Their exact hook placements are separately source checked.
No OS input, installed assets, engine launch or proprietary source is required.
"""
from pathlib import Path
import argparse, hashlib, importlib.util, json, os, shutil, subprocess, tempfile, time
ROOT=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('legacy_expression',ROOT/'tools/tests/ui_legacy_expression.py')
legacy=importlib.util.module_from_spec(spec);spec.loader.exec_module(legacy)

EXTRA=r'''
struct idCmdArgs {std::vector<std::string> values;int Argc()const{return int(values.size());}const char* Argv(int i)const{return values.at(i).c_str();}};
struct FileSystem {
 std::string root="synthetic source",written,output;unsigned reads=0,frees=0,writes=0;bool changed=false,shortWrite=false,reenter=false;
 int ReadFile(const char*,void** out){++reads;if(reenter){reenter=false;UI_ObserveLegacy(idCmdArgs{{"ui_observeLegacy","guis/synthetic.gui","Desktop/p_scanbar/scan","ui-import/nested.json"}});}std::string text=changed&&reads>1?root+"changed":root;*out=std::malloc(text.size()+1);if(!*out)throw std::bad_alloc();std::memcpy(*out,text.c_str(),text.size()+1);return int(text.size());}
 void FreeFile(void* p){++frees;std::free(p);}int WriteFile(const char* path,const void* bytes,int size){++writes;output=path;written.assign((const char*)bytes,size);return shortWrite?size-1:size;}
} fs;static FileSystem* fileSystem=&fs;
static bool failParse=false,duplicateTarget=false,disabledAlpha=false;
static void DeleteTree(idWindow* w){if(!w)return;for(auto* c:w->children)DeleteTree(c);UI_LegacyObservationDestroyed(w);delete w;}
idUserInterfaceLocal::~idUserInterfaceLocal(){if(owns)DeleteTree(desktop);}
static idWindow* NewWindow(idUserInterfaceLocal& gui,const char* name){auto* w=new idWindow;w->gui=&gui;w->name=name;UI_LegacyObservationParse(w,true,false);return w;}
static void SetAlphaRegister(idWindow& w,int reg){w.regList.entry.var=&w.matColor;w.regList.entry.regs[3]=reg;w.regList.present=true;}
bool idUserInterfaceLocal::InitFromFile(const char* path){
 idParser parser{};Check(UI_LegacyObservationLoad(this,parser,path),"diagnostic uses copied native parser root");
 if(!parser.loaded){UI_LegacyObservationLoaded(this,false);return true;}
 owns=true;desktop=NewWindow(*this,"Desktop");auto* pane=NewWindow(*this,"p_scanbar");pane->parent=desktop;desktop->children.Append(pane);
 const auto add=[&](){auto* scan=NewWindow(*this,"scan");scan->parent=pane;pane->children.Append(scan);idParser p{{"\\",TT_PUNCTUATION,635}};auto r=scan->ParseExpression(&p);SetAlphaRegister(*scan,int(r));scan->EvalRegs(-1,true);UI_LegacyObservationParse(scan,false,!failParse);return scan;};
 auto* scan=add();if(duplicateTarget)add();UI_LegacyObservationParse(pane,false,true);UI_LegacyObservationParse(desktop,false,true);desktop->FixupParms();
 if(disabledAlpha){scan->regList.entry.enabled=false;scan->matColor.data=.625f;}
 UI_LegacyObservationLoaded(this,true);return true;
}
'''

TEST=r'''
static idWinFloat* AddFloat(idWindow& w,const char* name,float value){auto* v=new idWinFloat;v->SetName(name);*v=value;w.definedVars.Append(v);return v;}
static void Setup(LegacyObservation& state,idWindow& w){state.gui=w.GetGui();state.source="synthetic.gui";state.sourceBytes="abc";state.gui->desktop=&w;}
static void FinishParse(idWindow& w){UI_LegacyObservationParse(&w,false,true);w.FixupParms();}
static void MissingAndPhases(){
 LegacyObservation state;idWindow w;Setup(state,w);LegacyObservationScope scope(state);UI_LegacyObservationParse(&w,true,false);
 const auto lookups=declObject.lookups;idParser p{{"\\",TT_PUNCTUATION,635},{"tail",TT_NAME}};auto r=w.ParseExpression(&p);SetAlphaRegister(w,int(r));w.EvalRegs(-1,true);
 Check(state.terms.size()==1&&state.terms[0].spelling=="\\"&&state.terms[0].line==635&&state.terms[0].parserSource=="synthetic.gui","copied exact original token provenance");
 Check(state.terms[0].marker==-2&&!state.terms[0].resolved,"original deferred operation remains unresolved before fixup");
 Check(state.evaluations.size()==1&&!state.evaluations[0].desktopFixed&&!state.evaluations[0].forced,"native parse evaluation distinguished from completed fixup");
 Check(state.evaluations[0].mapped&&state.evaluations[0].reg==r&&state.evaluations[0].alpha==0,"actual native evaluation maps exact alpha register");
 FinishParse(w);state.forced=true;w.EvalRegs(-1,true);state.forced=false;
 Check(state.terms[0].fixupLookup&&state.terms[0].resolved&&state.terms[0].marker==-1&&state.terms[0].variableType=="null","original null fixup and actual finalized marker retained");
 Check(state.desktopFixed&&state.evaluations.back().desktopFixed&&state.evaluations.back().forced,"forced post-fixup evaluation is explicit");
 Check(state.evaluations.back().registerValue==0&&state.evaluations.back().alpha==0&&!state.failed,"observed register and actual alpha are zero");
 Check(declObject.lookups==lookups+1&&w.disabled==0,"collector never replays declaration or mutating variable lookup");
 const auto count=state.evaluations.size();w.EvalRegs(int(r),false);Check(state.evaluations.size()==count,"cached query is never a fresh observation");
 const auto json=LegacyObservationJson(state,"Desktop/p_scanbar/scan",1,true);Check(json.find("\"replacement_acceptance\":false")!=std::string::npos&&json.find("\"diagnostic_interval_required\":true")!=std::string::npos&&json.find("\"include_closure_qualified\":false")!=std::string::npos,"receipt retains diagnostic and include acceptance limits");
}
static void DefinitionsAndTable(){
 for(bool late:{false,true}){LegacyObservation state;idWindow w;Setup(state,w);LegacyObservationScope scope(state);UI_LegacyObservationParse(&w,true,false);if(!late)AddFloat(w,"\\",.625f);idParser p{{"\\",TT_PUNCTUATION}};auto r=w.ParseExpression(&p);SetAlphaRegister(w,int(r));if(late)AddFloat(w,"\\",.375f);FinishParse(w);w.EvalRegs(-1,true);
 Check(state.terms[0].variableType=="float"&&state.terms[0].fixupLookup==late&&!state.failed,"immediate and original late bindings classified separately");Check(state.evaluations.back().alpha==(late?.375f:0.f),"actual Init versus late fixup semantics retained");}
 {LegacyObservation state;idWindow w;Setup(state,w);LegacyObservationScope scope(state);UI_LegacyObservationParse(&w,true,false);declObject.tableAvailable=true;idParser p{{"\\",TT_PUNCTUATION},{"[",TT_PUNCTUATION},{"2",TT_NUMBER},{"]",TT_PUNCTUATION}};auto r=w.ParseExpression(&p);SetAlphaRegister(w,int(r));FinishParse(w);w.EvalRegs(-1,true);Check(state.terms[0].table==7&&state.terms[0].type==WOP_TYPE_TABLE&&!state.terms[0].fixupLookup,"original table decision retained");Check(state.evaluations.back().alpha==2.25f&&!state.failed,"table result is not lowered to zero");declObject.tableAvailable=false;}
}
static void Refusals(){
 for(int variant=0;variant<11;++variant){LegacyObservation state;idWindow w,foreign;Setup(state,w);LegacyObservationScope scope(state);UI_LegacyObservationParse(&w,true,false);idParser p{{"\\",TT_PUNCTUATION}};auto r=w.ParseExpression(&p);SetAlphaRegister(w,int(r));
  if(variant==0){UI_LegacyObservationParse(&w,true,false);Check(state.failed,"duplicate parse cannot reuse identity");}
  if(variant==1){UI_LegacyObservationDestroyed(&w);UI_LegacyObservationParse(&w,true,false);Check(state.failed&&state.windows[0].destroyed,"destroyed address cannot revive observer identity");}
  if(variant==2){UI_LegacyObservationParse(&w,false,false);Check(state.failed,"parse refusal cannot be qualified by Init true");}
  if(variant==3){UI_LegacyObservationFixup(&w,0,"other",nullptr);Check(state.failed,"fixup spelling must match copied term");}
  if(variant==4){UI_LegacyObservationResolved(&w,0,0,-1);Check(state.failed,"final marker without original lookup is refused");}
  if(variant==5){UI_LegacyObservationFixup(&w,0,"\\",nullptr);UI_LegacyObservationResolved(&w,0,0,-2);Check(state.failed,"unfinished actual native marker rejected");}
  if(variant==6){UI_LegacyObservationOp(&w,1,0,int(r),WOP_TYPE_VAR,0,-2);Check(state.failed,"duplicate operation cannot overwrite original receipt");}
  if(variant==7){UI_LegacyObservationFixed(&w);Check(state.failed,"fixup before parse completion refused");}
  if(variant==8){UI_LegacyObservationParse(&foreign,true,false);idParser q{{"\\",TT_PUNCTUATION}};foreign.ParseExpression(&q);Check(state.windows.size()==1&&state.terms.size()==1&&!state.failed,"foreign window with same spelling has no authority");}
  if(variant==9){state.terms.resize(LegacyObservation::MaxTerms);idParser q{{"\\",TT_PUNCTUATION}};w.ParseExpression(&q);Check(state.failed,"term budget refuses observation without changing native parse");}
  if(variant==10){FinishParse(w);state.evaluations.resize(LegacyObservation::MaxEvaluations);w.EvalRegs(-1,true);Check(state.failed&&w.matColor.data==0,"evaluation budget does not alter native publication");}
 }
 {LegacyObservation state;idWindow w;Setup(state,w);LegacyObservationScope scope(state);UI_LegacyObservationParse(&w,true,false);std::string longSource(200,'x');idParser p{{"\\",TT_PUNCTUATION}};p.filename=longSource;
  denyAllocation=true;const auto token=UI_LegacyObservationTerm(&w,&p,p.tokens[0],-1);denyAllocation=false;Check(token==0&&state.failed,"hook allocation failure is contained without native exception");}
}
static void MappingAndInactive(){
 {const auto before=declObject.table.indexCalls;declObject.tableAvailable=true;idWindow w;idParser p{{"\\",TT_PUNCTUATION},{"[",TT_PUNCTUATION},{"2",TT_NUMBER},{"]",TT_PUNCTUATION}};w.ParseExpression(&p);Check(declObject.table.indexCalls==before+1,"inactive observation performs no additional table query");declObject.tableAvailable=false;}
 {idWindow w;idParser p{{"\\",TT_PUNCTUATION}};UI_LegacyObservationParse(&w,true,false);Check(!UI_LegacyObservationActive(&w)&&UI_LegacyObservationTerm(&w,&p,p.tokens[0],-1)==0,"inactive hooks do not allocate or gain authority");denyAllocation=true;UI_LegacyObservationFixed(&w);UI_LegacyObservationDestroyed(&w);denyAllocation=false;}
 for(int variant=0;variant<5;++variant){LegacyObservation state;idWindow w;Setup(state,w);LegacyObservationScope scope(state);UI_LegacyObservationParse(&w,true,false);idParser p{{"\\",TT_PUNCTUATION}};auto r=w.ParseExpression(&p);SetAlphaRegister(w,int(r));FinishParse(w);
  if(variant==0)w.regList.entry.enabled=false;if(variant==1)w.matColor.eval=false;if(variant==2)w.matColor.guiDict=&w.owner.dict;if(variant==3)w.regList.entry.regs[3]=MAX_EXPRESSION_REGISTERS;if(variant==4)w.regList.entry.var=nullptr;
  w.matColor.data=.5f;if(variant>=3){float regs[MAX_EXPRESSION_REGISTERS]{};UI_LegacyObservationEvaluation(&w,regs,MAX_EXPRESSION_REGISTERS,&w.matColor,.5f);}else w.EvalRegs(-1,true);
  const auto& last=state.evaluations.back();if(variant<3)Check(last.alpha==.5f&&last.registerValue==0&&(!last.enabled||!last.eval||last.dictionary),"register evaluation does not imply disabled bound property changed");else Check(!last.mapped,"wrong binding or invalid register index is unavailable");
 }
}
static void CacheTeardown(){
 for(bool foreignLater:{false,true}){LegacyObservation state;idWindow w,foreign;Setup(state,w);{
  LegacyObservationScope scope(state);UI_LegacyObservationParse(&w,true,false);idParser p{{"\\",TT_PUNCTUATION}};auto r=w.ParseExpression(&p);SetAlphaRegister(w,int(r));FinishParse(w);w.EvalRegs(-1,true);
  Check(state.evalCache&&*state.evalCache==&w,"actual shared evaluation cache slot captured");if(foreignLater)foreign.EvalRegs(-1,true);
  }
  Check(state.evalCache&&*state.evalCache==(foreignLater?&foreign:nullptr),"scope clears only its own cached window identity");*state.evalCache=nullptr;
 }
}
static void CommandsAndTrees(){
 const idCmdArgs valid{{"ui_observeLegacy","guis/synthetic.gui","Desktop/p_scanbar/scan","ui-import/probe.json"}};
 for(int variant=0;variant<8;++variant){fs=FileSystem{};fs.reenter=variant==7;commonObject.log.clear();failParse=variant==1;duplicateTarget=variant==2;fs.changed=variant==3;disabledAlpha=variant==4;fs.shortWrite=variant==5;auto args=valid;if(variant==6)args.values[2]="Desktop/missing/scan";UI_ObserveLegacy(args);
  Check(!legacyObservation&&!legacyObservationCommand&&fs.reads==fs.frees,"command scope and VFS ownership released");Check(fs.writes==1,"command exports one bounded observation receipt");const bool complete=variant==0||variant==4||variant==5||variant==7;if((fs.written.find("\"structural_complete\":true")!=std::string::npos)!=complete)std::cerr<<"variant="<<variant<<" receipt="<<fs.written<<"\n";Check((fs.written.find("\"structural_complete\":true")!=std::string::npos)==complete,"command refuses parse, duplicate, stale root or missing target");
  if(variant==5)Check(commonObject.log.find("UI_LEGACY_OBSERVATION_END failed")!=std::string::npos,"short receipt write never reports observed success");
  if(variant==4)Check(fs.written.find("\"matcolor_alpha\":0.625")!=std::string::npos&&fs.written.find("\"enabled\":false")!=std::string::npos,"disabled target reports actual property independently");
 }
 failParse=duplicateTarget=disabledAlpha=false;fs=FileSystem{};auto bad=valid;bad.values[3]="../bad.json";UI_ObserveLegacy(bad);Check(!fs.reads&&!fs.writes,"invalid command path performs no native load");
 {LegacyObservation state;idWindow w;Setup(state,w);LegacyObservationScope scope(state);idParser p{};Check(UI_LegacyObservationLoad(state.gui,p,state.source.c_str())&&p.memory=="abc","parser consumes exactly frozen root bytes");Check(UI_LegacyObservationLoad(state.gui,p,state.source.c_str())&&state.failed&&p.loads==1,"reentrant load cannot replace observed source");}
 {idWindow root,a,b,c;root.name="D";a.name=b.name="P";c.name="T";root.children.Append(&a);root.children.Append(&b);a.children.Append(&c);b.children.Append(&c);unsigned visited=0;Check(!LegacyExactWindow(&root,"D/P/T",visited),"duplicate complete ancestry refused even through separate parent branches");}
}
int main(){try{MissingAndPhases();DefinitionsAndTable();Refusals();MappingAndInactive();CacheTeardown();CommandsAndTrees();std::cout<<"PASS "<<checks<<" native observation checks\n";return 0;}catch(const std::exception& e){denyAllocation=false;std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
'''

def source():
    cpp=(ROOT/'src/ui/LegacyGuiImport.cpp').read_text();window=(ROOT/'src/ui/Window.cpp').read_text();manager=(ROOT/'src/ui/UserInterface.cpp').read_text()
    # These containing methods are not replaced by synthetic production code.
    for text in ['UI_LegacyObservationParse(this,true,false);','UI_LegacyObservationParse(this,false,ret);','UI_LegacyObservationDestroyed(this);']:
        assert window.count(text)==1,text
    assert 'if (!UI_LegacyObservationLoad(this,src,qpath)) src.LoadFile( qpath );' in manager
    assert manager.count('UI_LegacyObservationLoaded(this,src.IsLoaded());')==1
    assert manager.count('AddCommand("ui_observeLegacy",UI_ObserveLegacy')==1 and manager.count('RemoveCommand("ui_observeLegacy")')==1
    generated=legacy.build_source().replace(legacy.TEST,'').replace(legacy.OBSERVATION_STUBS,(ROOT/'src/ui/LegacyGuiImport.h').read_text().replace('#pragma once','').replace('class id','struct id'))
    generated=generated.replace('struct Gui {','struct idUserInterfaceLocal {').replace('Gui owner;Gui* gui=', 'idUserInterfaceLocal owner;idUserInterfaceLocal* gui=').replace('Gui* GetGui()', 'idUserInterfaceLocal* GetGui()')
    generated=generated.replace('struct idRegister{enum{FLOAT,VEC4,NUMTYPES};};','struct idRegister{enum{FLOAT,VEC4,NUMTYPES};int type=VEC4,regCount=4;unsigned short regs[4]{};idWinVar* var=nullptr;bool enabled=true;};')
    generated=generated.replace('struct RegisterList {','struct RegisterList {idRegister entry;bool present=false;idRegister* FindReg(const char*){return present?&entry:nullptr;}void SetToRegs(float*){} void GetFromRegs(float* r){if(present&&entry.enabled&&entry.var&&entry.var->GetEval()&&!entry.var->GetDict()) static_cast<idWinFloat*>(entry.var)->data=r[entry.regs[3]];}')
    generated=generated.replace('int initializations=0;', 'int initializations=0;bool eval=true;bool GetEval()const{return eval;}idDict* GetDict()const{return guiDict;}')
    generated=generated.replace('operator float()const{return data;}', 'float w()const{return data;}operator float()const{return data;}')
    generated=generated.replace('bool ReadToken(idToken* t)', 'std::string filename="synthetic.gui",memory;bool loaded=false;int loads=0;int GetFlags()const{return 65;}const char* GetFileName()const{return filename.c_str();}bool LoadMemory(const char* p,int n,const char* name){++loads;memory.assign(p,n);filename=name;return loaded=true;}bool ReadToken(idToken* t)')
    generated=generated.replace('idDict dict;idDict* GetStateDict()', 'bool owns=false;~idUserInterfaceLocal();bool InitFromFile(const char*);void SetUniqued(bool){}idDict dict;idDict* GetStateDict()')
    generated=generated.replace('void FixupParms();void EvaluateRegisters(float*);','void FixupParms();void EvaluateRegisters(float*);float EvalRegs(int=-1,bool=false);RegisterList* RegList(){return &regList;}int GetChildCount(){return children.Num();}idWindow* GetChild(int i){return children[i];}')
    generated=generated.replace('static int Icmp(const char* a,const char* b)', 'static int Icmpn(const char* a,const char* b,int n){return std::strncmp(a,b,n);}static bool CheckExtension(const char* s,const char* ext){std::string x=s,y=".";y+=ext;return x.size()>=y.size()&&x.substr(x.size()-y.size())==y;}static int Icmp(const char* a,const char* b)')
    generated=generated.replace('struct idDeclTable:idDecl{int Index()const{return 7;}', 'struct idDeclTable:idDecl{mutable unsigned indexCalls=0;int Index()const{++indexCalls;return 7;}')
    generated=generated.replace('struct Common {int warnings=0;', 'struct Common {int warnings=0;std::string log;void Printf(const char* fmt,...){char b[1024];va_list args;va_start(args,fmt);std::vsnprintf(b,sizeof(b),fmt,args);va_end(args);log+=b;}')
    includes='''#include <memory>\n#include <charconv>\n#include <limits>\n#include <cmath>\n#include <new>\nstatic bool denyAllocation=false;\nvoid* operator new(std::size_t n){if(denyAllocation)throw std::bad_alloc();if(auto* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}\nvoid* operator new[](std::size_t n){return ::operator new(n);}\nvoid operator delete(void* p)noexcept{std::free(p);}\nvoid operator delete[](void* p)noexcept{std::free(p);}\nvoid operator delete(void* p,std::size_t)noexcept{std::free(p);}\nvoid operator delete[](void* p,std::size_t)noexcept{std::free(p);}\n'''
    generated=generated.replace('static unsigned checks=0;',includes+'static unsigned checks=0;')
    helpers=cpp[cpp.index('namespace {'):cpp.index('\nvoid RetainedUI_ExportLegacy')]
    core=cpp[cpp.index('// LEGACY_OBSERVATION_CORE_BEGIN'):]
    return generated+'\n'+legacy.function(window,'float idWindow::EvalRegs(')+'\n'+EXTRA+'\n'+helpers+'\n'+core+'\n'+TEST

def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--compiler');ap.add_argument('--sanitizers',action='store_true');ap.add_argument('--mutations',action='store_true');args=ap.parse_args()
    out=Path(tempfile.mkdtemp(prefix='legacy-observation-',dir=ROOT/'.tmp'));sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    deps=[ROOT/p for p in ['src/ui/Window.cpp','src/ui/Window.h','src/ui/RegExp.cpp','src/ui/Winvar.h','src/ui/Winvar.cpp','src/framework/DeclManager.cpp','src/ui/UserInterface.cpp','src/ui/LegacyGuiImport.h','src/ui/LegacyGuiImport.cpp','tools/tests/ui_legacy_expression.py','tools/tests/ui_legacy_observation.py']]
    before={str(p):sha(p) for p in deps};text=source();cases=[('positive',text,None)]
    if args.mutations:
        edits=[
            ('inactive-extra-query','const unsigned observation = UI_LegacyObservationActive(this) ?', 'const unsigned observation = true ?', 'inactive observation performs no additional table query'),
            ('allow-reentrant-command','if (legacyObservationCommand || legacyObservation || args.Argc()', 'if (legacyObservation || args.Argc()', 'command exports one bounded observation receipt'),
            ('omit-original-table','term.table = tableIndex;','term.table = -1;','original table decision retained'),
            ('lose-fixed-marker','term.resolved = true; term.marker = b;','term.resolved = true; term.marker = -2;','original null fixup and actual finalized marker retained'),
            ('pre-is-post','value.desktopFixed = state.desktopFixed;','value.desktopFixed = true;','native parse evaluation distinguished from completed fixup'),
            ('wrong-property','value.alpha = alpha;','value.alpha = 0;','actual Init versus late fixup semantics retained'),
            ('skip-fixup-identity','reinterpret_cast<intptr_t>(term.variable) != a || b != -1','reinterpret_cast<intptr_t>(term.variable) != a','unfinished actual native marker rejected'),
            ('allow-second-parse','if (found || state.windows.size()', 'if (false || state.windows.size()', 'duplicate parse cannot reuse identity'),
            ('skip-source-equality','after == state.sourceBytes; complete','true; complete','command refuses parse, duplicate, stale root or missing target'),
            ('allow-ambiguous-path','return matches == 1 && visited','return matches >= 1 && visited','command refuses parse, duplicate, stale root or missing target'),
            ('accept-parse-failure','if (!success) state.failed = true;','if (false) state.failed = true;','parse refusal cannot be qualified by Init true'),
            ('ignore-binding','reg->var == expected && count','true && count','wrong binding or invalid register index is unavailable'),
            ('reuse-load','if (state.loadAttempted || state.source != path)','if (state.source != path)','reentrant load cannot replace observed source'),
            ('skip-cache-retirement','if (state.evalCache && state.Find(*state.evalCache)) *state.evalCache = nullptr;','if (false) *state.evalCache = nullptr;','scope clears only its own cached window identity'),
            ('lose-destroy','owner->destroyed = true; state.failed = true;','owner->destroyed = false; state.failed = true;','destroyed address cannot revive observer identity'),
        ]
        for name,old,new,expected in edits:assert text.count(old)==1,(name,text.count(old));cases.append((name,text.replace(old,new),expected))
    compiler=args.compiler or shutil.which('clang++') or shutil.which('g++');assert compiler
    env=dict(os.environ,TEMP=str(out),TMP=str(out),TMPDIR=str(out));records=[]
    for name,body,expected in cases:
        case=out/name;case.mkdir();cpp=case/'probe.cpp';cpp.write_text(body,newline='\n');exe=case/('probe.exe' if os.name=='nt' else 'probe')
        cmd=[compiler,'-std=c++20','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-unused-variable','-Wno-unused-function','-Wno-overloaded-virtual','-Wno-missing-field-initializers','-Wno-misleading-indentation',*(['-Wno-null-conversion','-Wno-unused-private-field'] if 'clang' in Path(compiler).name else ['-Wno-conversion-null']),*(['-fsanitize=address,undefined','-fno-omit-frame-pointer','-g'] if args.sanitizers else []),str(cpp),'-o',str(exe)]
        if Path(compiler).name.lower() in ('cl','cl.exe'):
            cmd=[compiler,'/nologo','/std:c++20','/EHsc','/MTd','/Od','/W3','/wd4100','/wd4189','/wd4505',str(cpp),'/Fe:'+str(exe),'/Fo:'+str(case/'probe.obj')]
        start=time.monotonic();build=subprocess.run(cmd,env=env,cwd=case,capture_output=True,text=True);(case/'compile.log').write_text(build.stdout+build.stderr);entry={'name':name,'command':cmd,'compile_exit':build.returncode}
        if build.returncode==0:
            run=subprocess.run([str(exe)],env=env,cwd=case,capture_output=True,text=True,timeout=30);(case/'run.log').write_text(run.stdout+run.stderr);entry.update(run_exit=run.returncode,passed=(run.returncode!=0 and 'FAIL '+expected in run.stderr if expected else run.returncode==0 and ' native observation checks' in run.stdout));print(name,run.stdout,run.stderr,flush=True)
        else:print(name,build.stderr,flush=True)
        entry['seconds']=time.monotonic()-start;records.append(entry)
    stable=before=={str(p):sha(p) for p in deps};passed=stable and all(r.get('passed') for r in records)
    (out/'result.json').write_text(json.dumps({'status':'passed' if passed else 'failed','sources':before,'inputs_unchanged':stable,'cases':records,'files':{str(p):sha(p) for p in out.rglob('*') if p.is_file()}},indent=2)+'\n');print(out/'result.json');raise SystemExit(0 if passed else 1)
if __name__=='__main__':main()
