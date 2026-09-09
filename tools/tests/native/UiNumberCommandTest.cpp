// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "src/ui/retained/Runtime.h"
#include "src/ui/retained/State.h"
#include <json/json.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <map>
#include <set>
#include <sstream>

using namespace openq4::ui;
static unsigned checks=0;

static void Check(bool value,const char* message) { ++checks;if(!value){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);} }
static bool Near(float a,float b,float tolerance=.035f) { return std::abs(a-b)<=tolerance; }
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
static std::string Source(bool transformed=false,bool framed=false,bool hostSource=false) {
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
	source["state"]["enabled"]["type"]="boolean";source["state"]["enabled"]["initial"]=true;
	Json::Value enabledBinding;enabledBinding["id"]="availability";enabledBinding["node"]="number";enabledBinding["property"]="enabled";enabledBinding["value"]["state"]="enabled";source["bindings"].append(enabledBinding);
	source["state"]["fontSize"]["type"]="number";source["state"]["fontSize"]["initial"]=20;source["state"]["fontSize"]["cvar"]="r_font_test";
	Json::Value fontBinding;fontBinding["id"]="font";fontBinding["node"]="root";fontBinding["property"]="font-size";fontBinding["value"]["state"]="fontSize";source["bindings"].append(fontBinding);
	source["aliases"]["root::spacing"]["node"]="root";source["aliases"]["root::spacing"]["property"]="letter-spacing";
	Json::Value timeline,track;timeline["id"]="feedback";timeline["durationMs"]=1;track["node"]="number";track["property"]="opacity";
	for(unsigned t:{0u,1u}){Json::Value key;key["atMs"]=t;key["value"]=Typed("number",1);track["keys"].append(key);}timeline["tracks"].append(track);source["timelines"].append(timeline);
	Json::StreamWriterBuilder writer;writer["indentation"]="";writer["precision"]=17;return Json::writeString(writer,source);
}
#ifndef OPENQ4_NUMBER_COMMAND_INTERACTION_ONLY
struct TestHost final:Host {
	unsigned draws=0,layers=0,errors=0;std::uint64_t frame=1;double readback=1.05,fontSize=20;
	bool missingFont=false,badGlyph=false;std::set<std::pair<int,std::uint32_t>> glyphs;
	bool ReadFile(const std::string&,std::string&)override{return false;}
	std::string Translate(const std::string& text)override{return text=="#str_field"?"Field":text.starts_with("#str_")?"Invalid number":text;}
	bool ReadCVar(const std::string& name,size_t type,StateValue& value)override{
		if(type!=0)return false;if(name=="r_number_test")value=readback;else if(name=="r_font_test")value=fontSize;else return false;return true;
	}
	void Log(bool error,const std::string&)override{if(error)++errors;}
	std::uintptr_t LoadMaterial(const std::string&,int& w,int& h)override{w=h=256;return 1;}
	void Draw(const std::vector<Vertex>&,const std::vector<int>&,std::uintptr_t)override{++draws;}
	std::uint64_t RenderFrame()const override{return frame;}
	bool BeginLayer(std::uint32_t,int,int)override{++layers;return true;}
	void CompositeLayer(std::uint32_t,std::uint32_t,float,const Bounds&)override{}
	void MaskLayer(std::uint32_t,std::uint32_t,const Bounds&)override{}void EndLayer(std::uint32_t)override{}
	FontMetrics GetFontMetrics(const std::string&,int size)override{return {size*.775f,size*.225f,missingFont?0:size*1.4f,size*.5f};}
	static float Advance(int size,std::uint32_t cp){return size*(cp=='1'?.31f:cp=='.'?.17f:.47f)+.1875f;}
	Glyph GetGlyph(const std::string&,int size,std::uint32_t cp)override{
		glyphs.emplace(size,cp);return {badGlyph?std::numeric_limits<float>::quiet_NaN():Advance(size,cp),0,-size*.775f,10,float(size),0,0,1,1,"font"};
	}
};
struct View {
	TestHost host;Runtime runtime{host};Viewport viewport;double time=1;std::string error,source;
	explicit View(bool cvar=false){source=Source(false,false,cvar);std::vector<Diagnostic> diagnostics;
		const bool loaded=runtime.LoadDocument(source,"number-command.q4ui",diagnostics);
		for(const auto& d:diagnostics)std::fprintf(stderr,"%s: %s\n",d.pointer.c_str(),d.message.c_str());
		Check(loaded,"actual command fixture loads");runtime.SetReducedMotion(true,time);Frame();Begin();}
	void Frame(){++host.frame;time+=.01;runtime.Frame(viewport,time);}
	WidgetViewState Widget(){auto v=runtime.GetWidgetState("number");Check(v&&v->number,"local Number editor exists");return *v;}
	NumberEditIdentity Id(){return Widget().number->identity;}
	void Begin(){Check(runtime.FocusControl("number",time)&&runtime.BeginNumberEdit("number",error,time),"begin eligible editor");}
	void Select(size_t a,size_t c){Check(runtime.SetNumberSelection("number",Id(),a,c,error,time),"set explicit selection");}
	void Text(std::string_view text){Select(0,Widget().number->state.text.size());Check(runtime.ReplaceNumberSelection("number",Id(),text,error,time),"replace local draft");}
	bool Command(TextEditCommand command,bool extend=false){const auto draws=host.draws,layers=host.layers;
		const bool result=runtime.NumberCommand("number",Id(),command,extend,error,time);
		Check(host.draws==draws&&host.layers==layers,"logical command performs no render or layer submission");return result;}
	void Expect(std::string_view text,size_t anchor,size_t caret){const auto v=Widget();
		Check(v.number->state.text==text&&v.number->state.anchor==anchor&&v.number->state.caret==caret,"exact local text and directional selection");}
};
static void SameBatchAndUndo(){
	View v;v.Frame();v.Text("12345");Check(!v.runtime.GetNumberGeometry("number"),"painted geometry is stale after edit");
	const auto old=v.Id();Check(v.Command(TextEditCommand::Left),"Left works before next Frame");v.Expect("12345",4,4);
	Check(!v.runtime.NumberCommand("number",old,TextEditCommand::Left,false,v.error,v.time),"old edit revision cannot navigate");
	Check(v.Command(TextEditCommand::Home,true),"extend to run start");v.Expect("12345",4,0);
	Check(v.Command(TextEditCommand::Right),"reverse selection collapses toward right");v.Expect("12345",4,4);
	Check(v.Command(TextEditCommand::Left,true),"extend left");v.Expect("12345",4,3);
	Check(v.Command(TextEditCommand::Left),"reverse selection collapses toward left");v.Expect("12345",3,3);
	Check(v.Command(TextEditCommand::Backspace),"delete preceding run unit");v.Expect("1245",2,2);
	Check(v.runtime.UndoNumberEdit("number",v.Id(),false,v.error,v.time),"undo command deletion");v.Expect("12345",3,3);
	v.Select(4,1);Check(v.Command(TextEditCommand::Delete),"delete reversed selection atomically");v.Expect("15",1,1);
	Check(v.runtime.UndoNumberEdit("number",v.Id(),false,v.error,v.time),"undo restores original reversed selection");v.Expect("12345",4,1);
	Check(v.runtime.UndoNumberEdit("number",v.Id(),true,v.error,v.time),"redo command deletion");v.Expect("15",1,1);
	Check(v.Command(TextEditCommand::SelectAll)&&v.Command(TextEditCommand::Delete),"select-all deletion leaves editable empty text");v.Expect("",0,0);
	const auto empty=v.Id();Check(v.Command(TextEditCommand::Left)&&v.Command(TextEditCommand::Delete)&&v.Id()==empty,"empty draft with old nonempty ink supports revision-preserving edge no-ops");
	v.Frame();v.Text("12");Check(v.Command(TextEditCommand::Home),"new nonempty buffer navigates while old ink is empty");v.Expect("12",0,0);
	const auto edge=v.Id();Check(v.Command(TextEditCommand::Backspace)&&v.Id()==edge,"start backspace is a no-op");
	Check(v.Command(TextEditCommand::End,true),"extend to run end");v.Expect("12",0,2);
	Check(std::get<double>(v.Widget().accepted)==1.05&&v.runtime.TakeActions().empty(),"commands never commit or queue accepted values");
}
static void ScalarBoundariesAndInk(){
	View v;v.viewport.displayScale=1.25f;v.Frame();
	const std::string text="1\xC3\xA9\xF0\x9F\x99\x82" "Z";v.Text(text);
	Check(v.Command(TextEditCommand::Left),"multi-byte current buffer uses run boundaries before paint");v.Expect(text,7,7);
	Check(v.host.glyphs.contains({25,0x1F642}),"new buffer is measured by resolved density/font provider");
	Check(v.Command(TextEditCommand::Backspace),"delete whole supplied scalar, never UTF-8 byte");v.Expect("1\xC3\xA9Z",3,3);
	Check(v.runtime.UndoNumberEdit("number",v.Id(),false,v.error,v.time),"undo multi-byte deletion");v.Expect(text,7,7);
	v.Select(1,1);Check(v.Command(TextEditCommand::Delete),"forward deletion uses scalar end");v.Expect("1\xF0\x9F\x99\x82Z",1,1);
	// This backend explicitly has scalar boundaries, not grapheme boundaries.
	v.Text("e\xCC\x81");Check(v.Command(TextEditCommand::Left),"unshaped run exposes combining scalar stop");v.Expect("e\xCC\x81",1,1);
	v.Text("123");Check(v.Command(TextEditCommand::Home),"caret at beginning before rendering");v.Frame();
	const auto first=v.runtime.GetNumberGeometry("number");
	Check(first.has_value(),"fresh rendered command caret exists");
	Check(v.Command(TextEditCommand::Right),"one scalar advance");Check(!v.runtime.GetNumberGeometry("number"),"new command never advertises old painted geometry");v.Frame();
	const auto next=v.runtime.GetNumberGeometry("number");Check(next.has_value(),"new rendered command caret exists");
	Check(Near(next->caret.x-first->caret.x,TestHost::Advance(25,'1')+.3125f),"command caret displacement matches drawn fractional font run");
}
static void FreshTypographyAndFailures(){
	View v;v.Frame();v.Text("789");v.host.fontSize=32;
	Check(v.runtime.SetPresentationAlias("root::spacing","0.375",true,v.error),"change inherited spacing without rendering");
	Check(v.Command(TextEditCommand::Home),"fresh host font binding resolves before command");
	Check(v.host.glyphs.contains({32,'9'}),"command measured current font, not cached painted face");
	for(const auto command:{TextEditCommand::WordLeft,TextEditCommand::WordRight,static_cast<TextEditCommand>(999)}){
		const auto id=v.Id();Check(!v.Command(command)&&v.Id()==id,"absent word table or unknown command fails without edit");v.Expect("789",0,0);}
	const auto id=v.Id();Check(!v.Command(TextEditCommand::Delete,true)&&v.Id()==id,"unsupported extension is atomic");
	v.host.missingFont=true;v.host.fontSize=43;
	Check(!v.Command(TextEditCommand::Right)&&v.Id()==id,"unavailable new font fails without changing edit history or selection");
	v.host.missingFont=false;v.host.fontSize=44;Check(v.Command(TextEditCommand::Right),"new available font recovers logical editing");
	v.Text("~");v.host.badGlyph=true;v.host.fontSize=45;const auto before=v.Id();
	Check(!v.Command(TextEditCommand::Home)&&v.Id()==before,"malformed current run metrics fail atomically");
}
static void OwnershipAndReadback(){
	View v(true);v.Text("1e");const auto stable=v.Id();
	Check(v.Command(TextEditCommand::Home),"invalid numeric draft remains navigable");v.Expect("1e",0,0);
	v.host.readback=1.75;const auto before=v.Id();Check(!v.Command(TextEditCommand::End),"fresh external readback blocks command before next Frame");
	Check(v.Widget().number->conflict&&v.Id()!=before&&std::get<double>(v.Widget().accepted)==1.75,"fresh accepted value marks preserved draft conflict and retires old revision");v.Expect("1e",0,0);
	Check(v.runtime.ResolveNumberConflict("number",v.Id(),true,v.error,v.time),"explicit keep resolves conflict without accepting invalid draft");
	TextInputEvent preedit;Check(MakeTextInputPreedit("2",TextIndexUnit::Utf8Bytes,0,1,preedit,v.error),"create preedit value");
	Check(v.runtime.ApplyNumberInput("number",v.Id(),preedit,v.error,v.time),"install local preedit");const auto composing=v.Id();
	Check(!v.Command(TextEditCommand::Delete)&&v.Id()==composing&&v.Widget().number->composition.has_value(),"command cannot erase or cancel composition");
	TextInputEvent cancel;MakeTextInputCancel(cancel);Check(v.runtime.ApplyNumberInput("number",v.Id(),cancel,v.error,v.time),"explicit composition cancellation");
	v.Text("1.5");Check(v.runtime.CommitNumberEdit("number",v.Id(),v.error,v.time),"queue explicit numeric proposal");const auto pending=v.Widget();
	Check(!v.Command(TextEditCommand::Left)&&v.Id()==pending.number->identity&&v.Widget().pending==pending.pending,"outstanding proposal blocks navigation");
	Check(v.runtime.AcknowledgeControlProposal("number",pending.proposalToken,false),"reject proposal without accepted mutation");
	const auto rejected=v.Id();Check(v.Command(TextEditCommand::End)&&v.Id()==rejected&&v.Widget().rejected.has_value(),"edge no-op preserves rejected proposal feedback");
	Check(v.runtime.SetState({{"enabled",false}},v.error,v.time),"disable focused editor");
	Check(!v.runtime.NumberCommand("number",rejected,TextEditCommand::Home,false,v.error,v.time)&&!v.Widget().number->active,"disabled retained draft has no command authority");
	Check(v.runtime.SetState({{"enabled",true}},v.error,v.time),"re-enable field");v.Frame();v.Begin();
	Check(v.Id()!=stable&&!v.runtime.NumberCommand("number",stable,TextEditCommand::Home,false,v.error,v.time),"resumed edit retires old session");
	const auto active=v.Id();Check(v.runtime.PushModal("viewport",v.time),"open scope excluding number owner");
	Check(!v.runtime.NumberCommand("number",active,TextEditCommand::Home,false,v.error,v.time),"modal ownership loss rejects old command");
	Check(v.runtime.PopModal(v.time),"close temporary scope");v.Frame();v.Begin();
	v.host.readback=std::numeric_limits<double>::quiet_NaN();const auto invalid=v.Id();
	Check(!v.Command(TextEditCommand::Home)&&v.Id()==invalid,"invalid fresh host read fails closed");
}
static void RestoreAndIndependentInstances(){
	TestHost host;Runtime first(host),second(host);std::string error,snapshot;std::vector<Diagnostic> diagnostics;Viewport viewport;
	const auto source=Source();Check(first.LoadDocument(source,"same.q4ui",diagnostics)&&second.LoadDocument(source,"same.q4ui",diagnostics),"independent contexts load same source");
	first.Frame(viewport,1);second.Frame(viewport,1);Check(first.FocusControl("number",1)&&first.BeginNumberEdit("number",error,1),"first instance begins");
	Check(second.FocusControl("number",1)&&second.BeginNumberEdit("number",error,1),"peer has its own active edit lease");
	auto id=first.GetWidgetState("number")->number->identity;Check(first.ReplaceNumberSelection("number",id,"1e",error,1),"first instance dirty draft");id=first.GetWidgetState("number")->number->identity;
	Check(!second.NumberCommand("number",id,TextEditCommand::Home,false,error,1),"sibling cannot use first instance edit lease");
	Check(first.SaveSnapshot(snapshot,error,1),"save source-bound local edit");first.Shutdown();Check(first.LoadDocument(source,"same.q4ui",diagnostics)&&first.RestoreSnapshot(snapshot,error,2),"recreate resource and restore draft");
	Check(!first.NumberCommand("number",id,TextEditCommand::Home,false,error,2),"old lease cannot navigate restored draft before layout");
	++host.frame;first.Frame(viewport,2);Check(first.BeginNumberEdit("number",error,2),"explicit resume restored draft");
	const auto fresh=first.GetWidgetState("number")->number->identity;Check(fresh!=id&&first.NumberCommand("number",fresh,TextEditCommand::Home,false,error,2),"new restored lease navigates current draft");
	Check(first.GetWidgetState("number")->number->state.text=="1e"&&second.GetWidgetState("number")->number->state.text=="1.05","restored invalid text preserved and peer untouched");
}
static void FontServiceRecreation(){
	View v;v.Frame();v.Text("8e");const auto old=v.Id();std::string snapshot;std::vector<Diagnostic> diagnostics;
	Check(v.runtime.SaveSnapshot(snapshot,v.error,v.time),"save before final-context font teardown");v.runtime.Shutdown();v.host.fontSize=37;v.host.glyphs.clear();
	Check(v.runtime.LoadDocument(v.source,"number-command.q4ui",diagnostics)&&v.runtime.RestoreSnapshot(snapshot,v.error,v.time),"rebuild document with fresh font service");
	v.Frame();v.Begin();Check(!v.runtime.NumberCommand("number",old,TextEditCommand::Home,false,v.error,v.time),"font-resource recreation cannot resurrect edit identity");
	Check(v.Command(TextEditCommand::Home)&&v.host.glyphs.contains({37,'8'}),"recreated command queries new service font");v.Expect("8e",0,0);
}
#endif
static void AtomicOperationBoundary(){
	Document document;std::vector<Diagnostic> diagnostics;std::string error;
	Check(document.Load(Source(),diagnostics),"pure boundary uses actual authored model");State state;Check(state.Reset(document.Model(),error),"pure state initializes");
	Interaction input;input.Reset(document.Model());Check(input.SetReadbacks(state.ControlValues(),error),"pure readbacks initialize");input.SetBounds({{"number",{0,0,120,30,true}}});
	Check(input.Focus("number")&&input.BeginNumberEdit("number",error),"pure editor begins");const auto identity=[&]{return input.Widget("number")->number->identity;};
	Check(input.ReplaceNumberSelection("number",identity(),"1234",error)&&input.SetNumberSelection("number",identity(),3,1,error),"directional draft established");
	const auto stable=identity();
	Check(input.ApplyNumberOperation("number",stable,{TextEditOperation::Kind::Selection,3,1,"",false},error)&&identity()==stable,"checked no-op does not retire edit revision");
	const auto wrong=NumberEditIdentity{stable.session+1,stable.revision};
	Check(!input.ApplyNumberOperation("number",wrong,{TextEditOperation::Kind::Selection,0,0,"",true},error)&&identity()==stable,"exact active owner identity guards application boundary");
	for(const auto operation:std::vector<TextEditOperation>{
		{static_cast<TextEditOperation::Kind>(99),0,0,"",true},
		{TextEditOperation::Kind::Selection,0,0,"insert",true},
		{TextEditOperation::Kind::Selection,0,0,"",false},
		{TextEditOperation::Kind::Selection,3,1,"",true},
		{TextEditOperation::Kind::Selection,5,0,"",true},
		{TextEditOperation::Kind::Replace,1,1,"",true},
		{TextEditOperation::Kind::Replace,3,1,"",true},
		{TextEditOperation::Kind::Replace,0,4,"",false}}){
		Check(!input.ApplyNumberOperation("number",stable,operation,error)&&identity()==stable,"malformed or dishonest operation rejected atomically");
		const auto view=input.Widget("number");Check(view->number->state.text=="1234"&&view->number->state.anchor==3&&view->number->state.caret==1,"failed operation preserves text and selection");}
	Check(input.ApplyNumberOperation("number",stable,{TextEditOperation::Kind::Replace,1,3,"",true},error),"checked range deletes atomically");
	Check(input.UndoNumberEdit("number",identity(),false,error),"undo checked range");const auto restored=input.Widget("number");
	Check(restored->number->state.text=="1234"&&restored->number->state.anchor==3&&restored->number->state.caret==1,"range command undo preserves original direction");
}
int main(){
#ifndef OPENQ4_NUMBER_COMMAND_INTERACTION_ONLY
	SameBatchAndUndo();ScalarBoundariesAndInk();FreshTypographyAndFailures();OwnershipAndReadback();RestoreAndIndependentInstances();FontServiceRecreation();
#endif
	AtomicOperationBoundary();
	std::printf("Number command: %u checks passed (scalar LTR only; no native input or clipboard)\n",checks);}
