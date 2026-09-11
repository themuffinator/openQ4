// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "DocumentEdit.h"
#include <cstdio>
#include <cstdlib>
#include <new>
#include <limits>

using namespace openq4::ui;
static unsigned checks=0;
static bool deny=false;
static long failAfter=-1;
void* operator new(std::size_t n){if(deny || (failAfter>=0 && failAfter--==0))throw std::bad_alloc();if(void* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p) noexcept{std::free(p);}
void operator delete[](void* p) noexcept{std::free(p);}
void operator delete(void* p,std::size_t) noexcept{std::free(p);}
void operator delete[](void* p,std::size_t) noexcept{std::free(p);}
#define CHECK(v) do{++checks;if(!(v)){std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#v);std::exit(1);}}while(false)
static std::string NodeSource(const char* id){return std::string("{\"id\":\"")+id+"\",\"type\":\"group\",\"properties\":{\"opacity\":{\"type\":\"number\",\"value\":1}},\"extensions\":{\"kept\":\"é😀\"}}";}
static std::string Source(){return "\xef\xbb\xbf{\r\n// untouched prefix\r\n\"format\":\"openq4-ui\",\"version\":1,\"id\":\"edit\",\"root\":{\"id\":\"root\",\"type\":\"group\",\"children\":[/*leading,*/"+NodeSource("a")+",// between, stays\r\n"+NodeSource("b")+",/*last,*/{\"id\":\"nest\",\"type\":\"group\",\"children\":[]}]},\"aliases\":{\"alpha\":{\"node\":\"a\",\"property\":\"opacity\"}},\"extensions\":{\"tail\":\"untouched\"}\r\n}";}
struct Fixture {
    DocumentEdit edit;std::vector<Diagnostic> diagnostics;DocumentEditReceipt receipt;
    explicit Fixture(DocumentEditLimits limits={},const std::string& source=Source()){CHECK(edit.Open(source,limits,diagnostics));}
    bool Apply(std::initializer_list<DocumentEditOperation> operations){return edit.Apply(edit.Identity(),operations,receipt,diagnostics);}
    const std::string& Text(){return edit.Current()->Source();}
    void Valid(){Document d;CHECK(d.Load(Text(),diagnostics));CHECK(d.Source()==Text());CHECK(d.Model().id=="edit");}
    void Refuse(std::initializer_list<DocumentEditOperation> operations){const auto before=Text();const auto id=edit.Identity();const auto undo=edit.UndoCount(),redo=edit.RedoCount(),bytes=edit.HistorySourceBytes();const auto prior=receipt;CHECK(!Apply(operations));CHECK(Text()==before&&edit.Identity()==id&&edit.UndoCount()==undo&&edit.RedoCount()==redo&&edit.HistorySourceBytes()==bytes);CHECK(receipt.before==prior.before&&receipt.after==prior.after&&receipt.sourceChanged==prior.sourceChanged);}
};
static void AtomicHistory(){
    Fixture f;const auto original=f.Text();const auto identity=f.edit.Identity();const auto a=NodeSource("a");
    CHECK(f.Apply({InsertDocumentNode{"root",1,NodeSource("c")},MoveDocumentNode{"b","nest",0},ReplaceDocumentValue{"c","/properties/opacity/value","0.5"}}));
    CHECK(f.edit.Identity().document==identity.document&&f.edit.Identity().revision==identity.revision+1&&f.edit.UndoCount()==1);
    CHECK(f.edit.Current()->Model().FindNode("nest")->children.at(0).id=="b");CHECK(f.Text().find(a)!=std::string::npos);
    CHECK(f.Text().starts_with("\xef\xbb\xbf{\r\n// untouched prefix\r\n"));CHECK(f.Text().ends_with("\"extensions\":{\"tail\":\"untouched\"}\r\n}"));
    CHECK(f.Text().find("// between, stays\r\n")!=std::string::npos);f.Valid();const auto edited=f.Text();
    CHECK(!f.edit.Undo(identity,f.receipt,f.diagnostics));CHECK(f.Text()==edited);
    const auto prior=f.edit.Identity();deny=true;const bool undone=f.edit.Undo(prior,f.receipt,f.diagnostics);deny=false;
    CHECK(undone&&f.Text()==original&&f.edit.Identity().revision==prior.revision+1&&f.edit.RedoCount()==1);
    const auto undoneId=f.edit.Identity();deny=true;const bool redone=f.edit.Redo(undoneId,f.receipt,f.diagnostics);deny=false;
    CHECK(redone&&f.Text()==edited&&f.edit.Identity().revision==undoneId.revision+1);
    CHECK(f.edit.Undo(f.edit.Identity(),f.receipt,f.diagnostics));
    CHECK(f.Apply({ReplaceDocumentValue{"a","/properties/opacity/value","0.25"}}));CHECK(f.edit.RedoCount()==0);
    CHECK(!f.edit.Redo(f.edit.Identity(),f.receipt,f.diagnostics));f.Valid();
}
static void RepairAndRefusal(){
    Fixture f;
    f.Refuse({RemoveDocumentNode{"a"}}); // live alias would dangle
    CHECK(f.Apply({RemoveDocumentNode{"a"},ReplaceDocumentValue{"","/aliases","{}"}}));CHECK(!f.edit.Current()->Model().FindNode("a"));f.Valid();
    f.Refuse({InsertDocumentNode{"root",0,NodeSource("b")}});
    f.Refuse({MoveDocumentNode{"root","nest",0}});f.Refuse({RemoveDocumentNode{"root"}});
    f.Refuse({MoveDocumentNode{"nest","nest",0}});
    f.Refuse({InsertDocumentNode{"missing",0,NodeSource("x")}});f.Refuse({InsertDocumentNode{"root",999,NodeSource("x")}});
    f.Refuse({InsertDocumentNode{"root",0,NodeSource("x")},ReplaceDocumentValue{"missing","/id","\"y\""}});
    f.Refuse({ReplaceDocumentValue{"","/id","\"other\""}});
    f.Refuse({ReplaceDocumentValue{"b","/properties/opacity/value","1e999"}});
    f.Refuse({ReplaceDocumentValue{"b","/bad~2pointer","0"}});
    f.Refuse({ReplaceDocumentValue{"b","/properties/opacity/value","1 /*not swallowed*/ garbage"}});
    CHECK(f.Apply({MoveDocumentNode{"b","nest",0}}));f.Refuse({MoveDocumentNode{"nest","b",0}});
    f.Refuse({ReplaceDocumentValue{"b","/properties/opacity/value","2"}});f.Valid();
}
static void Boundaries(){
    for(int index=0;index<3;++index){Fixture f;CHECK(f.Apply({ReplaceDocumentValue{"","/aliases","{}"},RemoveDocumentNode{index==0?"a":index==1?"b":"nest"}}));f.Valid();CHECK(f.Text().find("/*leading,*/")!=std::string::npos&&f.Text().find("// between, stays\r\n")!=std::string::npos&&f.Text().find("/*last,*/")!=std::string::npos);}
    Fixture f;CHECK(f.Apply({InsertDocumentNode{"a",0,NodeSource("child")}}));f.Valid();CHECK(f.edit.Current()->Model().FindNode("a")->children.size()==1);
    CHECK(f.Apply({RemoveDocumentNode{"child"},InsertDocumentNode{"a",0,"/*outer ignored*/"+NodeSource("again")+" // trailing ignored"}}));f.Valid();
    CHECK(f.Text().find("outer ignored")==std::string::npos&&f.Text().find("trailing ignored")==std::string::npos);
    CHECK(f.Apply({MoveDocumentNode{"a","root",2}}));CHECK(f.edit.Current()->Model().root.children[2].id=="a");
    CHECK(f.Apply({MoveDocumentNode{"a","root",0}}));CHECK(f.edit.Current()->Model().root.children[0].id=="a");
    const auto before=f.Text();const auto identity=f.edit.Identity();const auto undo=f.edit.UndoCount();
    CHECK(f.Apply({MoveDocumentNode{"a","root",0},ReplaceDocumentValue{"a","/properties/opacity/value","1"}}));
    CHECK(f.Text()==before&&f.edit.Identity()==identity&&f.edit.UndoCount()==undo&&!f.receipt.sourceChanged);
    CHECK(f.Apply({}));CHECK(f.edit.Identity()==identity);DocumentEditReceipt out;
    CHECK(!f.edit.Apply({identity.document,identity.revision-1},{},out,f.diagnostics));
    CHECK(f.Apply({ReplaceDocumentValue{"a","/extensions","{\"slash/key\":1,\"til~de\":2}"},ReplaceDocumentValue{"a","/extensions/slash~1key","3"},ReplaceDocumentValue{"a","/extensions/til~0de","4"}}));f.Valid();
    Fixture other;CHECK(other.edit.Identity().document!=f.edit.Identity().document);CHECK(!other.edit.Undo(f.edit.Identity(),out,f.diagnostics));
}
static void Budgets(){
    DocumentEditLimits limits;limits.states=2;Fixture states(limits);CHECK(states.Apply({InsertDocumentNode{"nest",0,NodeSource("x")}}));states.Refuse({InsertDocumentNode{"nest",1,NodeSource("y")}});
    limits={};limits.steps=1;Fixture steps(limits);steps.Refuse({RemoveDocumentNode{"b"},RemoveDocumentNode{"nest"}});
    limits={};limits.revision=2;Fixture revisions(limits);CHECK(revisions.Apply({RemoveDocumentNode{"b"}}));revisions.Refuse({RemoveDocumentNode{"nest"}});CHECK(!revisions.edit.Undo(revisions.edit.Identity(),revisions.receipt,revisions.diagnostics));CHECK(revisions.Apply({}));
    limits={};limits.historyBytes=Source().size();Fixture bytes(limits);bytes.Refuse({RemoveDocumentNode{"b"}});
    limits={};limits.sourceBytes=Source().size();Fixture source(limits);source.Refuse({InsertDocumentNode{"root",0,NodeSource("x")}});
    limits={};limits.workingSourceBytes=3*Source().size();Fixture working(limits);working.Refuse({RemoveDocumentNode{"b"}});
    for(int fault=0;fault<6;++fault){DocumentEdit e;limits={};if(fault==0)limits.states=0;if(fault==1)limits.steps=0;if(fault==2)limits.sourceBytes=17*1024*1024;if(fault==3)limits.historyBytes=0;if(fault==4)limits.workingSourceBytes=0;if(fault==5)limits.revision=0;std::vector<Diagnostic> d;CHECK(!e.Open(Source(),limits,d)&&!e.Current()&&e.Identity()==DocumentEditIdentity{});}
}
static void ParserCompatibility(){
    for(const auto& source:{std::string("{}"),Source()+"0",std::string("{\"format\":\"openq4-ui\",\"format\":\"other\"}"),Source()+std::string(1,'\0')}){
        Document d;DocumentEdit e;std::vector<Diagnostic> direct,edited;CHECK(!d.Load(source,direct));CHECK(!e.Open(source,{},edited));CHECK(!direct.empty()&&!edited.empty());CHECK(direct[0].message==edited[0].message&&direct[0].byte==edited[0].byte);
    }
    auto duplicate=Source();const auto position=duplicate.find("\"version\":1");duplicate.insert(position,"\"version\":1,");
    DocumentEdit e;std::vector<Diagnostic> diagnostics;CHECK(!e.Open(duplicate,{},diagnostics));
}
static void SeparatorGrammar(){
    for(const auto* separator:{","," /*before,*/ , /*after,*/ "," //before,\n , //after,\n ","/*,]}*/ ,\r\n"}) {
        const auto source=std::string("{\"format\":\"openq4-ui\",\"version\":1,\"id\":\"edit\",\"root\":{\"id\":\"root\",\"type\":\"group\",\"children\":[/*prefix,*/")+NodeSource("a")+separator+NodeSource("b")+" //suffix,\n]}}";
        for(std::size_t index=0;index<=2;++index){Fixture f({},source);CHECK(f.Apply({InsertDocumentNode{"root",index,NodeSource("c")}}));f.Valid();CHECK(f.edit.Current()->Model().root.children[index].id=="c");CHECK(f.Apply({RemoveDocumentNode{"c"}}));f.Valid();CHECK(f.Text().find("/*prefix,*/")!=std::string::npos&&f.Text().find("//suffix,\n")!=std::string::npos);}
        for(const auto* id:{"a","b"}){Fixture f({},source);CHECK(f.Apply({RemoveDocumentNode{id}}));f.Valid();CHECK(f.Text().find("/*prefix,*/")!=std::string::npos&&f.Text().find("//suffix,\n")!=std::string::npos);}
    }
    auto source=std::string("{\"format\":\"openq4-ui\",\"version\":1,\"id\":\"edit\",\"root\":{\"id\":\"root\",\"type\":\"group\" //parent-tail\n}} ");
    Fixture missing({},source);CHECK(missing.Apply({InsertDocumentNode{"root",0,NodeSource("a")}}));missing.Valid();CHECK(missing.Text().find("//parent-tail\n")!=std::string::npos);
    CHECK(missing.Apply({RemoveDocumentNode{"a"}}));missing.Valid();CHECK(missing.Apply({InsertDocumentNode{"root",0,NodeSource("a")}}));missing.Valid();
}
static void AllocationFailures(){
#if !defined(_WIN32)
    bool completed=false;unsigned refused=0;
    for(long cutoff=0;cutoff<4096&&!completed;++cutoff){
        Fixture f;const auto source=f.Text();const auto identity=f.edit.Identity();const auto bytes=f.edit.HistorySourceBytes();
        const std::array<DocumentEditOperation,1> operations{InsertDocumentNode{"nest",0,NodeSource("x")}};
        failAfter=cutoff;const bool accepted=f.edit.Apply(identity,operations,f.receipt,f.diagnostics);failAfter=-1;
        if(accepted){completed=true;CHECK(f.edit.UndoCount()==1&&f.edit.Identity().revision==identity.revision+1);}
        else{++refused;CHECK(f.Text()==source&&f.edit.Identity()==identity&&f.edit.HistorySourceBytes()==bytes&&f.edit.UndoCount()==0);CHECK(f.receipt.after==DocumentEditIdentity{});CHECK(f.edit.Apply(identity,operations,f.receipt,f.diagnostics));}
    }
    CHECK(completed&&refused>100);std::printf("Allocation-denial points refused atomically: %u\n",refused);
#endif
}
int main(){AtomicHistory();RepairAndRefusal();Boundaries();Budgets();ParserCompatibility();SeparatorGrammar();AllocationFailures();std::printf("PASS %u checks; actual Document/DocumentEdit JSONC source and history; no editor shell or native renderer.\n",checks);return 0;}
