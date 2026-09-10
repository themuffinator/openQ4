#!/usr/bin/env python3
"""Compile actual native GUI expression/fixup methods against counted surrounding services.

This proves parser/register semantics, not native window or asset acceptance.
The token queue is a double; original preprocessed stock token provenance is
qualified separately. No proprietary source is embedded in this test.
"""
from pathlib import Path
import argparse,hashlib,json,os,re,shutil,subprocess,tempfile,time
ROOT=Path(__file__).resolve().parents[2]

def function(source,signature):
    start=source.index(signature);brace=source.index('{',start);level=0
    # Ignore braces in source strings/comments so extraction remains exact.
    token=re.compile(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[{}]',re.S)
    for match in token.finditer(source,brace):
        if match.group()=='{':level+=1
        elif match.group()=='}':
            level-=1
            if level==0:return source[start:match.end()]
    raise AssertionError('unclosed production method '+signature)

SUPPORT=r'''
#include <algorithm>
#include <array>
#include <cassert>
#include <cctype>
#include <cstdint>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>
static unsigned checks=0;
static void Check(bool b,const char* why){++checks;if(!b)throw std::runtime_error(why);}
struct idStr {
 std::string value;idStr()=default;idStr(const char* s):value(s?s:""){} idStr(std::string s):value(std::move(s)){}
 operator const char*()const{return value.c_str();}const char* c_str()const{return value.c_str();}int Length()const{return int(value.size());}char operator[](int i)const{return value.at(i);}
 static int Icmp(const char* a,const char* b){for(;;++a,++b){int x=std::tolower((unsigned char)*a),y=std::tolower((unsigned char)*b);if(x!=y||!x)return x-y;}}
 int Icmp(const char* s)const{return Icmp(c_str(),s);}static int ToLower(int c){return std::tolower((unsigned char)c);}void ToLower(){for(char& c:value)c=char(ToLower(c));}
 static void Copynz(char* out,const char* s,int size){Check(size>0,"bounded name copy");std::snprintf(out,size,"%s",s);}
 int Find(const char* s)const{auto p=value.find(s);return p==std::string::npos?-1:int(p);}idStr Left(int n)const{return value.substr(0,n);}idStr Right(int n)const{return value.substr(value.size()-n);}
 idStr& operator+=(const char* s){value+=s;return *this;}
 bool operator==(const char* s)const{return value==s;}
};
static const char* va(const char*,const char* s){return s;}
constexpr int TT_NUMBER=3,TT_NAME=4,TT_PUNCTUATION=5,TT_INTEGER=1,TT_FLOAT=2;
struct idToken:idStr {int type=TT_NAME,subtype=0,line=1;idToken()=default;idToken(const char* s,int t=TT_NAME,int l=1):idStr(s),type(t),subtype(t==TT_NUMBER?TT_FLOAT:0),line(l){} double GetFloatValue()const{return std::atof(c_str());}};
struct idParser {
 std::vector<idToken> tokens;size_t at=0;int warnings=0;explicit idParser(std::initializer_list<idToken> t):tokens(t){}
 bool ReadToken(idToken* t){if(at==tokens.size())return false;*t=tokens[at++];return true;}
 void UnreadToken(const idToken*){Check(at>0,"unread owned token");--at;}
 void ExpectTokenString(const char* s){idToken t;Check(ReadToken(&t)&&t==s,"expected token");}
 void Warning(const char*,...){++warnings;}
 void ParseRestOfLine(idStr& out){out.value.clear();while(at<tokens.size()&&tokens[at].line==tokens[at-1].line){if(!out.value.empty())out+=" ";out+=tokens[at++].c_str();}}
};
template<class T>struct idList:std::vector<T>{int Num()const{return int(this->size());}int Append(T v){int n=Num();this->push_back(v);return n;}};
struct idWindow;
struct idDict{std::map<std::string,float> values;float GetFloat(const char* key)const{auto i=values.find(key);return i==values.end()?0:i->second;}void SetFloat(const char* key,float value){values[key]=value;}};
struct idWinVar {idDict* guiDict=nullptr;idStr name;int initializations=0;virtual ~idWinVar()=default;virtual float x()const{return 0;}virtual void Init(const char*,idWindow*);void SetGuiInfo(idDict* gd,const char* n){guiDict=gd;SetName(n);}virtual void Set(const char*){}const char* GetName()const{return name.c_str();}void SetName(const char* n){name=n;}};
struct idWinFloat:idWinVar{float data=0;operator float()const{return data;}idWinFloat& operator=(float f){data=f;return *this;}
// FLOAT_METHODS
};
struct idWinInt:idWinVar{int data=0;float x()const override{return float(data);}operator int()const{return data;}idWinInt& operator=(int f){data=f;return *this;}};
struct idWinBool:idWinVar{bool data=false;float x()const override{return data?1.f:0.f;}operator bool()const{return data;}idWinBool& operator=(bool f){data=f;return *this;}};
struct idWinStr:idWinVar{idStr data;const char* c_str()const{return data.c_str();}idWinStr& operator=(const char* f){data=f;return *this;}};
struct idVec4{float data[4]{};float& operator[](int i){return data[i];}};
struct idWinVec4:idWinVar{idVec4 data;operator idVec4&(){return data;}float x()const override{return data.data[0];}};
struct idDecl {virtual ~idDecl()=default;};
struct idDeclTable:idDecl{int Index()const{return 7;}float TableLookup(float f)const{return f+.25f;}};
struct idDeclManagerLocal{static void MakeNameCanonical(const char*,char*,int);};
struct DeclManager {
 bool tableAvailable=false;unsigned lookups=0;std::string last;idDeclTable table;
 const idDecl* FindType(int,const char* name,bool makeDefault){Check(!makeDefault,"term lookup must not default a table");++lookups;char normalized[1024];idDeclManagerLocal::MakeNameCanonical(name,normalized,1023);last=normalized;return tableAvailable&&last=="/"?&table:nullptr;}
 const idDecl* DeclByIndex(int,int index){Check(index==7,"table identity retained");return &table;}
} declObject;static DeclManager* declManager=&declObject;constexpr int DECL_TABLE=0;
struct Common {int warnings=0;void Warning(const char*,...){++warnings;}void FatalError(const char*,...){throw std::runtime_error("unexpected native fatal");}} commonObject;static Common* common=&commonObject;
constexpr int MAX_EXPRESSION_OPS=256,MAX_EXPRESSION_REGISTERS=512,SCRIPT_COUNT=2,WIN_DESKTOP=1;
#define VAR_GUIPREFIX "gui::"
#define VAR_GUIPREFIX_LEN 5
#define TOP_PRIORITY 4
// ENUMS
struct Simple {idWinVar* GetWinVarByName(const char*){return nullptr;}};
struct drawWin_t{idWindow* win=nullptr;Simple* simp=nullptr;};
struct Gui {idDict dict;idDict* GetStateDict(){return &dict;}idWindow* desktop=nullptr;int GetTime()const{return 42;}const char* GetSourceFile()const{return "synthetic.gui";}idWindow* GetDesktop(){return desktop;}};
struct Script{void FixupParms(idWindow*){}};struct TimeEvent{Script* event;};struct NamedEvent{Script* mEvent;};
struct idRegister{enum{FLOAT,VEC4,NUMTYPES};};
struct RegisterEntry{const char* name;int type;};
struct RegisterList {void AddReg(const char*,int,idParser*,idWindow*,idWinVar*){throw std::runtime_error("unexpected predefined property in custom probe");}};
struct idWindow {
 // FIELDS
 idList<idWinVar*> definedVars;idList<idWindow*> children;idList<TimeEvent*> timeLineEvents;idList<NamedEvent*> namedEvents;Script* scripts[SCRIPT_COUNT]{};
 idWindow* parent=nullptr;idStr name="synthetic";int flags=0;int disabled=0;RegisterList regList;static constexpr int NumRegisterVars=0;static RegisterEntry RegisterVars[1];
 Gui owner;Gui* gui=&owner;std::map<std::string,drawWin_t> namedWindows;int numOps=0;wexpOp_t ops[MAX_EXPRESSION_OPS]{};idList<float> expressionRegisters;bool registerIsTemporary[MAX_EXPRESSION_REGISTERS]{};
 idWindow(){owner.desktop=this;expressionRegisters.Append(0);}~idWindow(){for(auto* p:definedVars)delete p;for(int i=0;i<numOps;++i)if(ops[i].b==-2)delete[]reinterpret_cast<char*>(ops[i].a);}
 const char* GetName()const{return name.c_str();}Gui* GetGui(){return gui;}void DisableRegister(const char*){++disabled;}void AddUpdateVar(idWinVar*){}void CalcRects(int,int){}
 drawWin_t* FindChildByName(const char* n){auto i=namedWindows.find(n);return i==namedWindows.end()?nullptr:&i->second;}
 idWinVar* GetWinVarByName(const char*,bool=false,drawWin_t**=nullptr);
 bool ParseRegEntry(const char*,idParser*);
 int ExpressionConstant(float);int ExpressionTemporary();wexpOp_t* ExpressionOp();int EmitOp(intptr_t,intptr_t,wexpOpType_t,wexpOp_t**=nullptr);
 intptr_t ParseEmitOp(idParser*,intptr_t,wexpOpType_t,int,wexpOp_t**=nullptr);intptr_t ParseTerm(idParser*,idWinVar*=nullptr,intptr_t=0);intptr_t ParseExpressionPriority(idParser*,int,idWinVar*=nullptr,intptr_t=0);intptr_t ParseExpression(idParser*,idWinVar*=nullptr,intptr_t=0);
 void FixupParms();void EvaluateRegisters(float*);
};
RegisterEntry idWindow::RegisterVars[1]{};
'''

TEST=r'''
static idWinFloat* AddFloat(idWindow& w,const char* name,float value){auto* v=new idWinFloat;v->SetName(name);*v=value;w.definedVars.Append(v);return v;}
static float Evaluate(idWindow& w,intptr_t r){float registers[MAX_EXPRESSION_REGISTERS];std::fill(std::begin(registers),std::end(registers),99.f);w.EvaluateRegisters(registers);return registers[r];}
static void Missing(){idWindow w;idParser p{{"\\",TT_PUNCTUATION},{"next",TT_NAME}};auto r=w.ParseExpression(&p);Check(p.at==1,"backslash consumes one term, preserving next declaration");Check(w.ops[0].opType==WOP_TYPE_VAR&&w.ops[0].b==-2&&w.ops[0].a,"missing name is deferred, not constant");Check(w.GetWinVarByName("\\",true)==nullptr,"bare slash does not manufacture GUI binding");Check(declObject.last=="/","decl lookup normalizes slash without creating table");Check(Evaluate(w,r)==0,"unfixed temporary register remains initial zero");w.FixupParms();Check(w.ops[0].a==0&&w.ops[0].b==-1,"fixup stores null pointer and completed marker");Check(Evaluate(w,r)==0,"unbound post-fixup evaluates zero");AddFloat(w,"\\",.75f);Check(Evaluate(w,r)==0,"later definition does not implicitly rerun completed fixup");Check(w.disabled==0,"missing name does not disable unrelated registers");}
static void Defined(){for(bool late:{false,true}){idWindow w;if(!late)AddFloat(w,"\\",.625f);idParser p{{"\\",TT_PUNCTUATION}};auto r=w.ParseExpression(&p);if(late)AddFloat(w,"\\",.375f);w.FixupParms();Check(Evaluate(w,r)==(late?.375f:0.f),"native immediate float Init differs from late fixup without Init");Check(w.disabled==1,"resolved expression disables exactly its own register");}}
static void OtherScopes(){idWindow parent,w;AddFloat(parent,"\\",.75f);w.parent=&parent;idParser p{{"\\",TT_PUNCTUATION}};auto r=w.ParseExpression(&p);w.FixupParms();Check(Evaluate(w,r)==0,"unqualified term does not inherit parent defined variable");idWindow child;AddFloat(child,"gain",.875f);w.namedWindows["child"]={&child,nullptr};idParser q{{"child::gain",TT_NAME}};auto s=w.ParseExpression(&q);w.FixupParms();Check(w.ops[1].a==reinterpret_cast<intptr_t>(child.definedVars[0]),"qualified named window retains exact native pointer lookup");Check(Evaluate(w,s)==0,"qualified immediate float Init consumes its token spelling");}
static void Table(){declObject.tableAvailable=true;idWindow w;AddFloat(w,"\\",.9f);idParser p{{"\\",TT_PUNCTUATION},{"[",TT_PUNCTUATION},{"2",TT_NUMBER},{"]",TT_PUNCTUATION}};auto r=w.ParseExpression(&p);Check(w.ops[0].opType==WOP_TYPE_TABLE&&w.disabled==0,"decl table takes precedence over local variable");Check(Evaluate(w,r)==2.25f,"observed table cannot be replaced by zero");declObject.tableAvailable=false;}
static void Fragments(){for(bool split:{false,true}){idWindow w;idParser p{{"\\",TT_PUNCTUATION,1},{"n",TT_NAME,split?2:1},{"matscalex",TT_NAME,split?2:1},{"-",TT_PUNCTUATION,split?3:1},{"1",TT_NUMBER,split?3:1},{"}",TT_PUNCTUATION,4}};auto r=w.ParseExpression(&p);idToken name;Check(p.ReadToken(&name)&&name=="n","literal n remains declaration, not escaped newline");Check(w.ParseRegEntry(name,&p),"native custom declaration parsed");auto* n=dynamic_cast<idWinStr*>(w.GetWinVarByName("n"));Check(n&&std::string(n->c_str())==(split?"matscalex":"matscalex - 1"),"native rest-of-line custom value preserved");if(split){Check(p.ReadToken(&name)&&name=="-"&&w.ParseRegEntry(name,&p),"split negative punctuation remains separate custom property");Check(w.GetWinVarByName("-")!=nullptr,"split custom punctuation identity retained");}Check(p.ReadToken(&name)&&name=="}","following closing brace remains unread");w.FixupParms();Check(Evaluate(w,r)==0,"literal n fragments do not define backslash alpha");}}
int main(){try{Missing();Defined();OtherScopes();Table();Fragments();Check(commonObject.warnings==0,"no inferred native warning");std::cout<<"PASS "<<checks<<" native expression/fixup checks\n";return 0;}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
'''

def build_source():
    w=(ROOT/'src/ui/Window.cpp').read_text();h=(ROOT/'src/ui/Window.h').read_text();decl=(ROOT/'src/framework/DeclManager.cpp').read_text();winvar=(ROOT/'src/ui/Winvar.cpp').read_text();wh=(ROOT/'src/ui/Winvar.h').read_text();float_class=wh[wh.index('class idWinFloat :'):wh.index('class idWinRectangle :')]
    float_methods='\n'.join(function(float_class,s) for s in ['virtual void Init(', 'virtual void Set(', 'virtual float x('])
    signatures=['idWinVar *idWindow::GetWinVarByName(', 'bool idWindow::ParseRegEntry(', 'int idWindow::ExpressionConstant(', 'int idWindow::ExpressionTemporary(', 'wexpOp_t *idWindow::ExpressionOp(', 'int idWindow::EmitOp(', 'intptr_t idWindow::ParseEmitOp(', 'intptr_t idWindow::ParseTerm(', 'intptr_t idWindow::ParseExpressionPriority(', 'intptr_t idWindow::ParseExpression(', 'void idWindow::FixupParms(', 'void idWindow::EvaluateRegisters(']
    bodies=[function(w,s) for s in signatures]
    fields=sorted(set(re.findall(r'retVar = &(\w+);',bodies[0])))
    enums=h[h.index('typedef enum {',h.index('DEFAULT_TEXTSCALE')):h.index('} wexpOp_t;')+len('} wexpOp_t;')]
    support=SUPPORT.replace('// FLOAT_METHODS',float_methods).replace('// ENUMS',enums).replace('// FIELDS','idWinFloat '+','.join(fields)+';').replace('**=','** =').replace('*=','* =')
    # Match actual registered expression invocation (no target variable supplied).
    reg=(ROOT/'src/ui/RegExp.cpp').read_text();assert reg.count('win->ParseExpression(src, NULL)')==1 and reg.count('win->ParseExpression( src, NULL )')==1
    return support+'\n'+function(winvar,'void idWinVar::Init(')+'\n'+function(decl,'void idDeclManagerLocal::MakeNameCanonical(')+'\n'+'\n'.join(bodies)+'\n'+TEST

def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--compiler');ap.add_argument('--mutations',action='store_true');ap.add_argument('--sanitizers',action='store_true');args=ap.parse_args()
    (ROOT/'.tmp').mkdir(exist_ok=True)
    out=Path(tempfile.mkdtemp(prefix='legacy-alpha-',dir=ROOT/'.tmp'));sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    deps=[ROOT/f for f in ['src/ui/Window.cpp','src/ui/Window.h','src/ui/RegExp.cpp','src/ui/Winvar.cpp','src/ui/Winvar.h','src/framework/DeclManager.cpp','tools/tests/ui_legacy_expression.py']];before={str(p):sha(p) for p in deps};source=build_source();cases=[('positive',source,None)]
    if args.mutations:
        edits=[('null-is-one','registers[op->c] = 0.0f;','registers[op->c] = 1.0f;'),('missing-is-constant','return EmitOp(a, b, WOP_TYPE_VAR);\n\t}\n\n}', 'delete []p; return ExpressionConstant(0);\n\t}\n\n}'),('skip-fixup','if (ops[i].b == -2)', 'if (false)'),('fixup-rebinds','ops[i].b = -1;', 'ops[i].b = -2;'),('drop-table','if ( table ) {','if ( false ) {'),('lose-custom-tail','if (remainder.Length()) {','if (false) {'),('skip-immediate-init','var->Init(token, this);','/* omitted immediate variable initialization */'),('reinit-late-fixup','idWinVar *var = GetWinVarByName(p, true);','idWinVar *var = GetWinVarByName(p, true); if(var) var->Init(p,this);')]
        for name,old,new in edits:assert source.count(old)==1,(name,source.count(old));cases.append((name,source.replace(old,new),True))
    compiler=args.compiler or shutil.which('clang++') or shutil.which('g++');assert compiler,'C++ compiler unavailable';records=[];env=dict(os.environ,TEMP=str(out),TMP=str(out),TMPDIR=str(out))
    expected={'null-is-one':'unbound post-fixup evaluates zero','missing-is-constant':'missing name is deferred, not constant','skip-fixup':'fixup stores null pointer and completed marker','fixup-rebinds':'fixup stores null pointer and completed marker','drop-table':'decl table takes precedence over local variable','lose-custom-tail':'native rest-of-line custom value preserved','skip-immediate-init':'native immediate float Init differs from late fixup without Init','reinit-late-fixup':'native immediate float Init differs from late fixup without Init'}
    for name,text,negative in cases:
        case=out/name;case.mkdir();cpp=case/'probe.cpp';cpp.write_text(text,newline='\n');exe=case/('probe.exe' if os.name=='nt' else 'probe');cmd=[compiler,'-std=c++20','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-unused-variable','-Wno-unused-function','-Wno-overloaded-virtual','-Wno-missing-field-initializers',*(['-Wno-unused-private-field','-Wno-null-conversion'] if 'clang' in Path(compiler).name else ['-Wno-conversion-null']),*(['-fsanitize=address,undefined','-fno-omit-frame-pointer','-g'] if args.sanitizers else []),str(cpp),'-o',str(exe)]
        start=time.monotonic();c=subprocess.run(cmd,cwd=case,env=env,capture_output=True,text=True);(case/'compile.log').write_text(c.stdout+c.stderr);entry={'name':name,'compile_exit':c.returncode,'command':cmd}
        if c.returncode==0:
            run=subprocess.run([str(exe)],cwd=case,env=env,capture_output=True,text=True,timeout=30);(case/'run.log').write_text(run.stdout+run.stderr);entry.update(run_exit=run.returncode,passed=(run.returncode!=0 and 'FAIL '+expected[name] in run.stderr if negative else run.returncode==0 and 'PASS 63 native expression/fixup checks' in run.stdout));print(name,run.stdout,run.stderr,flush=True)
        entry['seconds']=time.monotonic()-start;records.append(entry)
    stable=before=={str(p):sha(p) for p in deps};passed=stable and all(v.get('passed') for v in records);(out/'result.json').write_text(json.dumps({'status':'passed' if passed else 'failed','sources':before,'inputs_unchanged':stable,'cases':records,'files':{str(p):sha(p) for p in out.rglob('*') if p.is_file()}},indent=2)+'\n');print(out/'result.json');raise SystemExit(0 if passed else 1)
if __name__=='__main__':main()
