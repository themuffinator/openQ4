// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "src/ui/retained/Runtime.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <functional>
#include <new>
#include <limits>
#include <thread>
#include <json/json.h>
#include <RmlUi/Core.h>
using namespace openq4::ui;
static unsigned checks=0;
static bool deny=false;
static int ContextCount(){return Rml::GetSystemInterface()?Rml::GetNumContexts():0;}
void* operator new(std::size_t size){if(deny)throw std::bad_alloc();if(auto* p=std::malloc(size?size:1))return p;throw std::bad_alloc();}
void operator delete(void* p) noexcept{std::free(p);}
void operator delete(void* p,std::size_t) noexcept{std::free(p);}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete[](void* p) noexcept{std::free(p);}
void operator delete[](void* p,std::size_t) noexcept{std::free(p);}
static void Check(bool value,const char* why){++checks;if(!value){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
struct CanvasHost final:Host {
    unsigned calls=0,draws=0,resets=0;std::function<void()> callback,onLog;bool failFont=false;
    void Reset(){Call();Check(ContextCount()==0,"all candidate and live contexts close before host reset");++resets;}
    void Call(){++calls;if(callback){auto copied=callback;callback={};copied();}}
    std::vector<Vertex> ink;
    bool ReadFile(const std::string&,std::string&) override{Call();return false;}
    std::string Translate(const std::string& text) override{Call();return text=="#str_options"?"Alpha;Bravo;Charlie;Delta;Echo;Foxtrot":text;}
    bool ReadCVar(const std::string&,size_t,StateValue& out) override{Call();out=1.0;return true;}
    void Log(bool error,const std::string& message) override{Call();if(onLog){auto copied=onLog;onLog={};copied();}if(error)std::fprintf(stderr,"Rml: %s\n",message.c_str());}
    std::uintptr_t LoadMaterial(const std::string&,int& w,int& h) override{Call();w=h=64;return 1;}
    void Draw(const std::vector<Vertex>& vertices,const std::vector<int>&,std::uintptr_t) override{Call();++draws;ink.insert(ink.end(),vertices.begin(),vertices.end());}
    std::uint64_t RenderFrame() const override{return 1;}
    bool BeginLayer(std::uint32_t,int,int) override{Call();return false;}
    void CompositeLayer(std::uint32_t,std::uint32_t,float,const Bounds&) override{Call();}
    void MaskLayer(std::uint32_t,std::uint32_t,const Bounds&) override{Call();}
    void EndLayer(std::uint32_t) override{Call();}
    FontMetrics GetFontMetrics(const std::string&,int size) override{Call();if(failFont)return {};return {size*.8f,size*.2f,size*1.2f,size*.5f};}
    Glyph GetGlyph(const std::string&,int size,std::uint32_t) override{Call();return {size*.6f,0,-size*.8f,size*.6f,float(size),0,0,1,1,"font"};}
};
static const char* Source=R"json({"format":"openq4-ui","version":1,"id":"canvas","state":{"host":{"type":"number","initial":1,"cvar":"fixture"}},"root":{"id":"root","type":"group","children":[
 {"id":"panel","type":"group","properties":{"position":{"type":"keyword","value":"absolute"},"left":{"type":"length","unit":"dp","value":10},"top":{"type":"length","unit":"dp","value":20},"width":{"type":"length","unit":"dp","value":100},"height":{"type":"length","unit":"dp","value":40},"background-color":{"type":"color","value":[1,0,0,1]}}}
]}})json";
static Json::Value Typed(const char* type,Json::Value value,const char* unit=nullptr) {
	Json::Value result;result["type"]=type;result["value"]=std::move(value);if(unit)result["unit"]=unit;return result;
}
static Json::Value Array(std::initializer_list<double> list) { Json::Value result(Json::arrayValue);for(auto v:list)result.append(v);return result; }
static Json::Value Box(const char* id,double x,double y,double width,double height,const char* type="group") {
	Json::Value result;result["id"]=id;result["type"]=type;auto& p=result["properties"];
	p["position"]=Typed("keyword","absolute");p["display"]=Typed("keyword","block");p["opacity"]=Typed("number",1);
	p["left"]=Typed("length",x,"dp");p["top"]=Typed("length",y,"dp");p["width"]=Typed("length",width,"dp");p["height"]=Typed("length",height,"dp");
	if(std::string(type)=="text")p["text"]=Typed("text","#str_field");else result["children"]=Json::Value(Json::arrayValue);return result;
}
static void Colour(Json::Value& node,std::initializer_list<double> colour) { node["properties"]["background-color"]=Typed("color",Array(colour)); }
static std::string NumberSource(bool transformed=false,bool framed=false,bool hostSource=false) {
	Json::Value source;source["format"]="openq4-ui";source["version"]=1;source["id"]="number-runtime";
	source["state"]["value"]["type"]="number";source["state"]["value"]["initial"]=1.05;
	if(hostSource)source["state"]["value"]["cvar"]="r_number_test";
	source["actions"]["edit"]["input"]="number";source["actions"]["edit"]["operation"]="test.edit";source["actions"]["edit"]["arguments"]["value"]["input"]="value";
	source["aliases"]["viewport::rect"]["node"]="viewport";source["aliases"]["viewport::rect"]["property"]="rect";
	auto root=Box("root",0,0,640,360);root["properties"]["font-family"]=Typed("font","number-font");
	root["properties"]["font-size"]=Typed("length",20,"dp");root["properties"]["line-height"]=Typed("length",28,"dp");
	root["properties"]["letter-spacing"]=Typed("length",.25,"dp");root["properties"]["color"]=Typed("color",Array({1,1,1,1}));
	auto number=Box("number",40,35,290,95);auto& c=number["control"];
	c["role"]="number";c["action"]="edit";c["label"]="#str_field";c["value"]["state"]="value";c["minimum"]=-100;c["maximum"]=100;c["maxBytes"]=256;
	for(const auto* state:{"default","hover","focus","pressed","disabled"})c["states"][state]="feedback";
	for(const auto* part:{"viewport","text","selection","caret","composition","validation"})c["parts"][part]=part;
	auto viewport=Box("viewport",12,8,90,36);viewport["properties"]["overflow"]=Typed("keyword","hidden");
	if(transformed)viewport["properties"]["transform"]=Typed("transform",Array({9,7,1.1,.85,17}),"dp");
	if(framed) {
		viewport["properties"]["padding-left"]=Typed("length",5,"dp");viewport["properties"]["padding-top"]=Typed("length",3,"dp");
		viewport["properties"]["padding-right"]=Typed("length",4,"dp");viewport["properties"]["padding-bottom"]=Typed("length",2,"dp");
		viewport["properties"]["border-width"]=Typed("length",2,"dp");
	}
	auto text=Box("text",3,2,600,28,"text");text["properties"]["white-space"]=Typed("keyword","pre");text["properties"]["text-align"]=Typed("keyword","left");
	if(framed) { text["properties"]["margin-left"]=Typed("length",2,"dp");text["properties"]["padding-left"]=Typed("length",3,"dp");text["properties"]["padding-top"]=Typed("length",1,"dp"); }
	// Selection is authored behind text, caret and preedit above it. These are
	// real independent Rml boxes, not drawing primitives injected by the test.
	auto selection=Box("selection",0,0,0,20);Colour(selection,{1,0,0,1});
	auto caret=Box("caret",0,0,1,20);Colour(caret,{0,0,1,1});
	auto composition=Box("composition",0,0,0,1);Colour(composition,{0,1,0,1});
	if(framed)for(auto* node:{&selection,&caret,&composition}) {
		(*node)["properties"]["margin-left"]=Typed("length",2,"dp");(*node)["properties"]["margin-top"]=Typed("length",3,"dp");
		(*node)["properties"]["box-sizing"]=Typed("keyword","border-box");
	}
	viewport["children"].append(selection);viewport["children"].append(text);viewport["children"].append(caret);viewport["children"].append(composition);
	number["children"].append(viewport);auto validation=Box("validation",0,53,560,30,"text");validation["properties"]["font-family"]=Typed("font","validation-font");
	validation["properties"]["text"]=Typed("text","#str_invalid_number");number["children"].append(validation);root["children"].append(number);source["root"]=root;
	Json::Value timeline,track;timeline["id"]="feedback";timeline["durationMs"]=1;track["node"]="number";track["property"]="opacity";
	for(unsigned t:{0u,1u}){Json::Value key;key["atMs"]=t;key["value"]=Typed("number",1);track["keys"].append(key);}timeline["tracks"].append(track);source["timelines"].append(timeline);
	Json::StreamWriterBuilder writer;writer["indentation"]="";writer["precision"]=17;return Json::writeString(writer,source);
}

static void Basic(){
    CanvasHost host;Runtime runtime(host);DocumentEdit history;std::vector<Diagnostic> diagnostics;
    Check(history.Open(Source,{},diagnostics),"open history");Check(runtime.LoadDocument(Source,"canvas.q4ui",diagnostics),"load actual runtime");
    Viewport viewport;runtime.Frame(viewport,1);Bounds original;Check(runtime.GetBounds("panel",original),"initial bounds");
    const auto identity=history.Identity();const std::array<DocumentEditOperation,1> edits{ReplaceDocumentValue{"panel","/properties/left/value","70"}};
    auto source=history.PrepareEdit(identity,edits,diagnostics);Check(bool(source),"prepare source");
    const auto draws=host.draws;RuntimeDocumentOptions options;options.sourcePath="canvas.q4ui";options.viewport=viewport;options.seconds=1;
    auto candidate=runtime.PrepareDocument(runtime.CanvasIdentity(),*source,options,diagnostics);Check(bool(candidate),"prepare real Rml context");
    Check(host.draws==draws,"preparation submits no canvas draw");Check(history.Identity()==identity,"history not published during layout");
    Bounds bounds;Check(runtime.GetBounds("panel",bounds)&&bounds.x==original.x,"old actual canvas preserved before publication");
    DocumentEditReceipt receipt;const auto calls=host.calls;bool accepted=false;
    deny=true;accepted=runtime.PublishDocument(history,*source,*candidate,receipt);deny=false;
    Check(accepted&&host.calls==calls,"joint publication has no allocation or host callback");
    Check(receipt.before==identity&&receipt.after==history.Identity()&&history.UndoCount()==1,"exact history receipt");
    Check(runtime.GetBounds("panel",bounds)&&std::abs(bounds.x-70)<.1f,"new actual layout publishes with history");
    Check(runtime.Statistics().activeContexts==2,"old context retained through publication");
    candidate.reset();source.reset();Check(runtime.Statistics().activeContexts==1,"old context eventually cleaned");
    host.ink.clear();runtime.Frame(viewport,1);const auto first=host.ink;
    Runtime fresh(host);Check(fresh.LoadDocument(history.Current()->Source(),"canvas.q4ui",diagnostics),"fresh source reference");
    host.ink.clear();fresh.Frame(viewport,1);Check(first.size()==host.ink.size(),"same normal renderer geometry count");
    for(size_t i=0;i<first.size();++i)Check(first[i].x==host.ink[i].x&&first[i].y==host.ink[i].y&&first[i].r==host.ink[i].r,"same normal renderer vertices");
    auto undo=history.PrepareUndo(history.Identity(),false,diagnostics);Check(bool(undo),"prepare canvas undo");
    auto old=runtime.PrepareDocument(runtime.CanvasIdentity(),*undo,options,diagnostics);Check(bool(old),"prepare previous real layout");
    Check(runtime.PublishDocument(history,*undo,*old,receipt),"joint canvas undo");
    Check(runtime.GetBounds("panel",bounds)&&bounds.x==original.x,"undo restores exact old geometry");
}
static void StaleAndLifetime(){
    CanvasHost host;std::vector<Diagnostic> diagnostics;auto history=std::make_unique<DocumentEdit>();auto runtime=std::make_unique<Runtime>(host);
    Check(history->Open(Source,{},diagnostics)&&runtime->LoadDocument(Source,"canvas.q4ui",diagnostics),"lifetime fixture");
    const std::array<DocumentEditOperation,1> edits{ReplaceDocumentValue{"panel","/properties/left/value","70"}};
    RuntimeDocumentOptions options;options.sourcePath="canvas.q4ui";
    auto source=history->PrepareEdit(history->Identity(),edits,diagnostics);auto candidate=runtime->PrepareDocument(runtime->CanvasIdentity(),*source,options,diagnostics);
    Check(bool(candidate),"candidate before rejected mutation");Check(!runtime->FocusControl("missing",1),"rejected live mutation");
    DocumentEditReceipt out;Check(!runtime->PublishDocument(*history,*source,*candidate,out)&&!out.after.document,"rejected mutation invalidates proposal without output");
    candidate.reset();source.reset();
    source=history->PrepareEdit(history->Identity(),edits,diagnostics);
    host.callback=[&]{runtime->CancelInput(1);};
    candidate=runtime->PrepareDocument(runtime->CanvasIdentity(),*source,options,diagnostics);
    Check(!candidate&&history->UndoCount()==0,"foreign callback cannot mutate and then publish");
    source.reset();source=history->PrepareEdit(history->Identity(),edits,diagnostics);
    candidate=runtime->PrepareDocument(runtime->CanvasIdentity(),*source,options,diagnostics);Check(bool(candidate),"retry after rejected reentry");
    runtime.reset();history.reset();candidate.reset();source.reset();Check(true,"prepared destruction after both original owners died");
}

struct CanvasFixture {
    CanvasHost host;Runtime runtime{host};DocumentEdit history;std::vector<Diagnostic> diagnostics;RuntimeDocumentOptions options;
    std::array<DocumentEditOperation,1> edits{ReplaceDocumentValue{"panel","/properties/left/value","70"}};
    CanvasFixture(){Check(history.Open(Source,{},diagnostics)&&runtime.LoadDocument(Source,"canvas.q4ui",diagnostics),"fixture source and runtime");options.sourcePath="canvas.q4ui";runtime.Frame(options.viewport,1);}
};
static void IndependentMutations(){
    using Change=std::function<void(Runtime&)>;
    const std::vector<std::pair<const char*,Change>> changes={
        {"Frame",[](Runtime& r){r.Frame({},2);}}, {"SetState",[](Runtime& r){std::string e;r.SetState({},e,2);}},
        {"SetProperty",[](Runtime& r){r.SetProperty("missing","width","1px");}}, {"SetText",[](Runtime& r){r.SetText("missing","x");}},
        {"FocusControl",[](Runtime& r){r.FocusControl("missing",2);}}, {"CancelInput",[](Runtime& r){r.CancelInput(2);}},
        {"ReleaseInputSources",[](Runtime& r){r.ReleaseInputSources();}}, {"TakeActions",[](Runtime& r){r.TakeActions();}},
        {"PointerMove",[](Runtime& r){r.PointerMove(1,2,2);}}, {"PointerButton",[](Runtime& r){r.PointerButton(false,2);}},
        {"PointerWheel",[](Runtime& r){r.PointerWheel(0,2);}}, {"MenuAction",[](Runtime& r){r.MenuAction(MenuInput::Accept,false,2);}},
        {"SetControlEnabled",[](Runtime& r){r.SetControlEnabled("missing",false,2);}}, {"PushModal",[](Runtime& r){r.PushModal("missing",2);}},
        {"PopModal",[](Runtime& r){r.PopModal(2);}}, {"CanDispatchModalBack",[](Runtime& r){r.CanDispatchModalBack({},2);}},
        {"CanDispatchControlAction",[](Runtime& r){r.CanDispatchControlAction({},2);}},
        {"OpenChoicePopup",[](Runtime& r){r.OpenChoicePopup("missing",2);}}, {"CloseChoicePopup",[](Runtime& r){r.CloseChoicePopup("missing",1,2);}},
        {"ScrollChoicePopup",[](Runtime& r){r.ScrollChoicePopup("missing",1,ScrollStep::End,2);}},
        {"PlayTimeline",[](Runtime& r){r.PlayTimeline("missing",2);}}, {"PauseTimeline",[](Runtime& r){r.PauseTimeline("missing",2);}},
        {"ResumeTimeline",[](Runtime& r){r.ResumeTimeline("missing",2);}}, {"CancelTimeline",[](Runtime& r){r.CancelTimeline("missing",CancelPolicy::Hold,2);}},
        {"SetReducedMotion",[](Runtime& r){r.SetReducedMotion(false,2);}},
        {"SetPresentationAlias",[](Runtime& r){std::string e;r.SetPresentationAlias("missing","",false,e);}},
        {"RunEvent",[](Runtime& r){std::string e;Runtime::EventEffects effects;r.RunEvent("missing",2,effects,e);}},
        {"RestoreSnapshot",[](Runtime& r){std::string e;r.RestoreSnapshot("",e,2);}},
        {"QueryNumberEditor",[](Runtime& r){std::string e;r.QueryNumberEditor(e,2);}},
        {"BeginNumberNativeCollection",[](Runtime& r){std::string e;NativeTextEditorBarrier out;r.BeginNumberNativeCollection({}, {},out,e);}},
        {"ApplyNumberNative",[](Runtime& r){std::string e;NativeTextEditorReceipt out;r.ApplyNumberNative({}, {},out,e);}},
        {"CompleteNumberNativeCollection",[](Runtime& r){std::string e;NativeTextEditorBarrier out;r.CompleteNumberNativeCollection({}, {},out,e);}},
        {"SettleNumberNative",[](Runtime& r){std::string e;NativeTextEditorReceipt out;r.SettleNumberNative({},out,e);}},
        {"RetireNumberNativeExact",[](Runtime& r){r.RetireNumberNativeExact({},{});}},
        {"AbortPreparedDocument",[](Runtime& r){r.AbortPreparedDocument();}},
        {"CloseDocument",[](Runtime& r){r.CloseDocument();}}, {"Shutdown",[](Runtime& r){r.Shutdown();}},
        {"Initialize",[](Runtime& r){r.Initialize();}},
    };
    for(const auto& [name,change]:changes){
        CanvasFixture f;auto source=f.history.PrepareEdit(f.history.Identity(),f.edits,f.diagnostics);
        auto prepared=f.runtime.PrepareDocument(f.runtime.CanvasIdentity(),*source,f.options,f.diagnostics);Check(bool(prepared),name);
        const auto stamp=f.runtime.CanvasIdentity();const auto history=f.history.Identity();change(f.runtime);
        Check(f.runtime.CanvasIdentity()!=stamp,name);DocumentEditReceipt out;out.before={99,98};const auto before=out;
        Check(!f.runtime.PublishDocument(f.history,*source,*prepared,out),name);
        Check(out.before==before.before&&out.after==before.after&&f.history.Identity()==history&&f.history.Current()->Source()==Source,"stale refusal preserves exact history and caller output");
    }
}
static void PreparationFailures(){
    CanvasFixture f;auto source=f.history.PrepareEdit(f.history.Identity(),f.edits,f.diagnostics);
    for(int fault=0;fault<10;++fault){auto options=f.options;const auto nan=std::numeric_limits<float>::quiet_NaN();
        switch(fault){case 0:options.seconds=-1;break;case 1:options.viewport.width=0;break;case 2:options.viewport.height=-1;break;case 3:options.viewport.displayScale=nan;break;case 4:options.viewport.userScale=0;break;case 5:options.viewport.pixelDensityX=0;break;case 6:options.viewport.pixelDensityY=nan;break;case 7:options.viewport.originX=nan;break;case 8:options.viewport.originY=nan;break;case 9:options.seconds=std::numeric_limits<double>::infinity();break;}
        const auto calls=f.host.calls;Check(!f.runtime.PrepareDocument(f.runtime.CanvasIdentity(),*source,options,f.diagnostics),"invalid viewport/time refused");Check(f.host.calls==calls,"invalid layout rejected before host work");
    }
    f.host.callback=[] { Rml::GetSystemInterface()->LogMessage(Rml::Log::LT_WARNING,"candidate-owned warning"); };
    Check(!f.runtime.PrepareDocument(f.runtime.CanvasIdentity(),*source,f.options,f.diagnostics),"candidate diagnostic refuses replacement");
    Check(f.history.Current()->Source()==Source&&f.runtime.SourceMatches(Source,"canvas.q4ui"),"diagnostic failure keeps old source/canvas");
    Runtime peer(f.host);Check(peer.LoadDocument(Source,"peer.q4ui",f.diagnostics),"peer context for warning ownership");
    f.host.callback=[&]{Check(peer.LoadMarkup("<rml><body style='unregistered-property: 1'>peer</body></rml>","peer.rml"),"nested peer markup survives legacy warning");};
    auto prepared=f.runtime.PrepareDocument(f.runtime.CanvasIdentity(),*source,f.options,f.diagnostics);Check(bool(prepared),"peer warning cannot impersonate candidate failure");prepared.reset();
    f.host.callback=[] { Rml::GetSystemInterface()->LogMessage(Rml::Log::LT_WARNING,"candidate warning with reentry"); };
    f.host.onLog=[&]{f.runtime.CancelInput(2);};
    Check(!f.runtime.PrepareDocument(f.runtime.CanvasIdentity(),*source,f.options,f.diagnostics),"warning callback mutation cannot publish");
    f.host.callback=[] {throw std::runtime_error("host refusal");};
    Check(!f.runtime.PrepareDocument(f.runtime.CanvasIdentity(),*source,f.options,f.diagnostics),"host exception refuses candidate");
    prepared=f.runtime.PrepareDocument(f.runtime.CanvasIdentity(),*source,f.options,f.diagnostics);Check(bool(prepared),"exception cleanup permits retry");prepared.reset();
    const auto stamp=f.runtime.CanvasIdentity();bool refused=false;std::thread wrong([&]{refused=!f.runtime.PrepareDocument(stamp,*source,f.options,f.diagnostics);});wrong.join();
    Check(refused&&f.runtime.CanvasIdentity()==stamp,"wrong thread cannot enter candidate or mutate identity");
}
static void NumberCanvasAndNative(){
    CanvasHost host;Runtime runtime(host);DocumentEdit history;std::vector<Diagnostic> diagnostics;std::string error;
    const auto text=NumberSource(true,true);Check(history.Open(text,{},diagnostics)&&runtime.LoadDocument(text,"number.q4ui",diagnostics),"number actual source");
    RuntimeDocumentOptions options;options.sourcePath="number.q4ui";options.viewport.displayScale=1.25f;runtime.Frame(options.viewport,1);
    Check(runtime.FocusControl("number",1)&&runtime.BeginNumberEdit("number",error,1),"number editor before native");
    auto editor=runtime.QueryNumberEditor(error,1);Check(bool(editor),"number editor identity");
    TextEditorIdentity owner{1,2,3,editor->modalToken,5,editor->editor.identity.session,editor->editor.identity.revision,"number"};NativeTextIdentity native{11,12};NativeTextEditorBarrier barrier;
    NumberNativeCandidate observed;const auto observedCalls=host.calls;
    Check(runtime.ObserveNumberNativeCandidate(observed)&&observed.control=="number"&&observed.identity==editor->editor.identity&&observed.modalToken==editor->modalToken&&host.calls==observedCalls,"stored native candidate copies exact editor without host work");
    Check(runtime.AttachNumberNative(owner,native,barrier,error,1),"attach actual native model");
    const auto prior=observed;Check(!runtime.ObserveNumberNativeCandidate(observed)&&observed.control==prior.control&&observed.identity==prior.identity,"bound candidate refuses with unchanged output");
    Check(runtime.NativeVacancy()==RuntimeNativeVacancy::Bound,"stored native model is not vacancy");
    const std::array<DocumentEditOperation,1> edits{ReplaceDocumentValue{"number","/properties/left/value","60"}};
    auto source=history.PrepareEdit(history.Identity(),edits,diagnostics);const auto calls=host.calls;
    Check(!runtime.PrepareDocument(runtime.CanvasIdentity(),*source,options,diagnostics)&&host.calls==calls,"native ownership refuses canvas replacement before callbacks");
    Check(runtime.RetireNumberNativeExact(native,owner)&&runtime.NativeVacancy()==RuntimeNativeVacancy::Vacant,"exact retirement permits later replacement");
    auto prepared=runtime.PrepareDocument(runtime.CanvasIdentity(),*source,options,diagnostics);Check(bool(prepared),"prepare number canvas after native retirement");
    DocumentEditReceipt receipt;Check(runtime.PublishDocument(history,*source,*prepared,receipt),"publish number canvas");
    Bounds published;Check(runtime.GetBounds("number",published)&&std::abs(published.x-75)<.1f,"candidate layout uses final density before first draw");
    prepared.reset();source.reset();
    Check(runtime.FocusControl("number",1)&&runtime.BeginNumberEdit("number",error,1),"published number can acquire local editing");
    auto current=runtime.GetWidgetState("number")->number;Check(runtime.ReplaceNumberSelection("number",current->identity,"2.5",error,1),"published callback resolves moved Impl");
    runtime.Frame(options.viewport,1);Check(runtime.GetNumberGeometry("number").has_value(),"actual framed transformed number geometry after candidate wrapper destruction");
    source=history.PrepareEdit(history.Identity(),std::array<DocumentEditOperation,1>{ReplaceDocumentValue{"root","/properties/font-family/value","\"unavailable-font\""}},diagnostics);
    host.failFont=true;Check(!runtime.PrepareDocument(runtime.CanvasIdentity(),*source,options,diagnostics),"unavailable candidate font refuses");host.failFont=false;
    Check(runtime.GetWidgetState("number")->number->state.text=="2.5","font refusal preserves live editing draft");
}


static void ExactSourceAssociation(){
    CanvasFixture f;DocumentEdit other;Check(other.Open(Source,{},f.diagnostics),"distinct equal-byte history owner");
    auto a=f.history.PrepareEdit(f.history.Identity(),f.edits,f.diagnostics);
    auto b=other.PrepareEdit(other.Identity(),f.edits,f.diagnostics);
    auto prepared=f.runtime.PrepareDocument(f.runtime.CanvasIdentity(),*a,f.options,f.diagnostics);Check(bool(prepared),"source-association candidate");
    DocumentEditReceipt out;out.before={33,44};
    Check(!f.runtime.PublishDocument(other,*b,*prepared,out)&&out.before==DocumentEditIdentity{33,44},"equal target bytes never replace exact prepared source association");
    Check(f.history.UndoCount()==0&&other.UndoCount()==0,"cross-source failure preserves both histories");
    prepared.reset();a.reset();b.reset();
    auto owner=std::make_unique<DocumentEdit>();Check(owner->Open(Source,{},f.diagnostics),"source owner before death");
    a=owner->PrepareEdit(owner->Identity(),f.edits,f.diagnostics);owner.reset();const auto calls=f.host.calls;
    Check(!f.runtime.PrepareDocument(f.runtime.CanvasIdentity(),*a,f.options,f.diagnostics)&&f.host.calls==calls,"dead source rejected before candidate callbacks");
}

static void CandidateObservationCopies(){
    CanvasHost host;Runtime runtime(host);std::vector<Diagnostic> diagnostics;std::string error;
    auto text=NumberSource();const std::string control="number-"+std::string(55,'x');
    for(size_t at=0;(at=text.find("\"number\"",at))!=std::string::npos;at+=control.size()+2)text.replace(at,8,"\""+control+"\"");
    // The role spelling is grammar, not an object ID.
    const std::string role="\"role\":\""+control+"\"";const auto at=text.find(role);Check(at!=std::string::npos,"long-ID fixture preserves exact role grammar");text.replace(at,role.size(),"\"role\":\"number\"");
    // Action input and state type are likewise grammar, independent of stable IDs.
    for(const char* key:{"input","type"}){const std::string from="\""+std::string(key)+"\":\""+control+"\"";for(size_t pos=0;(pos=text.find(from,pos))!=std::string::npos;)text.replace(pos,from.size(),"\""+std::string(key)+"\":\"number\"");}
    Check(runtime.LoadDocument(text,"candidate.q4ui",diagnostics),"long original control loads");runtime.Frame({},1);
    Check(runtime.FocusControl(control,1)&&runtime.BeginNumberEdit(control,error,1),"long-ID active editor");
    NumberNativeCandidate out;Check(runtime.ObserveNumberNativeCandidate(out),"long candidate owned copy");const auto prior=out;const auto calls=host.calls;
    deny=true;const bool observed=runtime.ObserveNumberNativeCandidate(out);deny=false;
    Check(!observed&&out.control==prior.control&&out.identity==prior.identity&&out.modalToken==prior.modalToken&&host.calls==calls,"candidate allocation failure preserves complete caller output without callbacks");
    bool wrong=true;std::thread thread([&]{wrong=runtime.ObserveNumberNativeCandidate(out);});thread.join();Check(!wrong&&out.control==prior.control,"foreign thread cannot observe runtime authority");
    DocumentEdit history;Check(history.Open(text,{},diagnostics),"candidate copy history");const std::array<DocumentEditOperation,1> edits{ReplaceDocumentValue{control,"/properties/left/value","60"}};
    auto source=history.PrepareEdit(history.Identity(),edits,diagnostics);RuntimeDocumentOptions options;options.sourcePath="candidate.q4ui";bool called=false;
    host.callback=[&]{called=true;Check(!runtime.ObserveNumberNativeCandidate(out)&&out.control==prior.control&&out.identity==prior.identity,"in-flight canvas preparation refuses candidate observation unchanged");};
    auto prepared=runtime.PrepareDocument(runtime.CanvasIdentity(),*source,options,diagnostics);Check(called&&bool(prepared),"read-only refusal does not invalidate independent canvas preparation");
}

static void LegacyCanvasFrame(){
    CanvasHost host;Runtime runtime(host);Check(runtime.LoadMarkup(R"(<rml><head><style>body{margin:0;width:100%;height:100%;font-family:test;font-size:12px;}#legacy{position:absolute;left:10dp;top:20dp;width:100dp;height:40dp;background-color:red;}</style></head><body><div id="legacy">legacy</div></body></rml>)","legacy.rml"),"legacy markup loads without canonical history");
    Viewport viewport;viewport.displayScale=1.25f;runtime.Frame(viewport,1);Bounds bounds;
    Check(host.draws>0&&runtime.GetBounds("legacy",bounds)&&std::abs(bounds.x-12.5f)<.01f,"valid legacy document renders through common layout and density path");
    const auto draws=host.draws;viewport.width=0;runtime.Frame(viewport,2);Check(host.draws==draws&&!runtime.GetBounds("legacy",bounds),"invalid legacy viewport remains inert");
    viewport.width=1280;runtime.Frame(viewport,3);Check(host.draws>draws&&runtime.GetBounds("legacy",bounds),"legacy zero to valid viewport recovers current geometry");
}

int main(){LegacyCanvasFrame();ExactSourceAssociation();Basic();StaleAndLifetime();IndependentMutations();PreparationFailures();NumberCanvasAndNative();CandidateObservationCopies();std::printf("PASS %u checks; prepared history + actual Runtime/Rml, no engine or OS activation.\n",checks);return 0;}
