// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
// The "text" control role on the real Runtime and Rml document: a single-line
// field whose value is its text, edited by the Number editor without numeric
// syntax or bounds.
#include "src/ui/retained/Runtime.h"
#include <RmlUi/Core.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/ElementText.h>
#include <json/json.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

using namespace openq4::ui;
static unsigned checks=0;
static void Check(bool value,const char* message) { ++checks;if(!value){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);} }
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
// A save-name field: its own document state, written by a string action.
// Authored string state starts empty; the name arrives as a state update.
// A host-bound name reads back from the host instead. `masked` and
// `allowEmpty` make it a password field's.
static Json::Value Source(bool hostBound=false,bool masked=false,bool allowEmpty=false) {
	Json::Value source;source["format"]="retained-ui";source["version"]=1;source["id"]="text-field";
	source["state"]["name"]["type"]="string";source["state"]["name"]["initial"]="";
	if(hostBound)source["state"]["name"]["cvar"]="ui_text_test";
	source["actions"]["rename"]["input"]="string";source["actions"]["rename"]["operation"]="test.rename";source["actions"]["rename"]["arguments"]["value"]["input"]="value";
	auto root=Box("root",0,0,640,360);root["properties"]["font-family"]=Typed("font","field-font");
	root["properties"]["font-size"]=Typed("length",20,"dp");root["properties"]["line-height"]=Typed("length",28,"dp");
	root["properties"]["color"]=Typed("color",Array({1,1,1,1}));
	auto field=Box("field",40,35,290,95);auto& c=field["control"];
	c["role"]="text";c["action"]="rename";c["label"]="#str_field";c["value"]["state"]="name";c["maxBytes"]=32;
	if(masked)c["masked"]=true;if(allowEmpty)c["allowEmpty"]=true;
	for(const auto* state:{"default","hover","focus","pressed","disabled"})c["states"][state]="feedback";
	for(const auto* part:{"viewport","text","selection","caret","composition","validation"})c["parts"][part]=part;
	auto viewport=Box("viewport",12,8,240,36);viewport["properties"]["overflow"]=Typed("keyword","hidden");
	auto text=Box("text",3,2,600,28,"text");text["properties"]["white-space"]=Typed("keyword","pre");text["properties"]["text-align"]=Typed("keyword","left");
	auto selection=Box("selection",0,0,0,20);Colour(selection,{1,0,0,1});
	auto caret=Box("caret",0,0,1,20);Colour(caret,{0,0,1,1});
	auto composition=Box("composition",0,0,0,1);Colour(composition,{0,1,0,1});
	viewport["children"].append(selection);viewport["children"].append(text);viewport["children"].append(caret);viewport["children"].append(composition);
	field["children"].append(viewport);auto validation=Box("validation",0,53,560,30,"text");
	validation["properties"]["text"]=Typed("text","#str_invalid_name");field["children"].append(validation);root["children"].append(field);source["root"]=root;
	Json::Value timeline,track;timeline["id"]="feedback";timeline["durationMs"]=1;track["node"]="field";track["property"]="opacity";
	for(unsigned t:{0u,1u}){Json::Value key;key["atMs"]=t;key["value"]=Typed("number",1);track["keys"].append(key);}timeline["tracks"].append(track);source["timelines"].append(timeline);
	return source;
}
static std::string Encode(const Json::Value& value) { Json::StreamWriterBuilder writer;writer["indentation"]="";writer["precision"]=17;return Json::writeString(writer,value); }
static Json::Value Parse(const std::string& source) {
	Json::CharReaderBuilder builder;std::unique_ptr<Json::CharReader> reader(builder.newCharReader());Json::Value result;std::string error;
	Check(reader->parse(source.data(),source.data()+source.size(),&result,&error),"snapshot JSON parses");return result;
}
struct TestHost final:Host {
	std::map<std::string,std::uint32_t> glyphs;std::map<std::string,std::uintptr_t> handles;std::map<std::uintptr_t,std::uint32_t> codepoints;
	std::set<std::uint32_t> drawn;std::uint64_t frame=1;unsigned errors=0;bool quiet=false;std::string hostName="Quicksave";
	bool ReadFile(const std::string&,std::string&)override{return false;}
	bool ReadCVar(const std::string& name,size_t type,StateValue& value)override{if(name!="ui_text_test"||type!=2)return false;value=hostName;return true;}
	std::string Translate(const std::string& value)override{
		return value=="#str_field"?"Name":value=="#str_231129"?"No copies":value.starts_with("#str_")?"Enter a name":value;
	}
	void Log(bool error,const std::string& message)override{if(error&&!quiet){++errors;std::fprintf(stderr,"Runtime: %s\n",message.c_str());}}
	std::uintptr_t LoadMaterial(const std::string& name,int& w,int& h)override{
		w=h=256;auto& handle=handles[name];if(!handle){handle=handles.size();const auto glyph=glyphs.find(name);codepoints[handle]=glyph!=glyphs.end()?glyph->second:0;}return handle;
	}
	void Draw(const std::vector<Vertex>&,const std::vector<int>& indices,std::uintptr_t material)override{
		Check(indices.size()%3==0,"triangle draw indices");const auto cp=codepoints.find(material);if(cp!=codepoints.end()&&cp->second)drawn.insert(cp->second);
	}
	std::uint64_t RenderFrame()const override{return frame;}bool BeginLayer(std::uint32_t,int,int)override{return true;}
	void CompositeLayer(std::uint32_t,std::uint32_t,float,const Bounds&)override{}void MaskLayer(std::uint32_t,std::uint32_t,const Bounds&)override{}void EndLayer(std::uint32_t)override{}
	FontMetrics GetFontMetrics(const std::string&,int size)override{return {size*.775f,size*.225f,size*1.4f,size*.5f};}
	Glyph GetGlyph(const std::string& family,int size,std::uint32_t cp)override{
		const auto name="font-"+family+"-"+std::to_string(size)+"-"+std::to_string(cp);glyphs[name]=cp;
		const float advance=size*.47f+.1875f;return {advance,.375f,-size*.775f,advance*.8f,float(size),0,0,1,1,name};
	}
};
struct View {
	TestHost host;Runtime runtime{host};Viewport viewport;double time=1;std::string error,source;
	explicit View(bool hostBound=false,bool masked=false,bool allowEmpty=false){
		viewport.width=1280;viewport.height=720;viewport.displayScale=1;source=Encode(Source(hostBound,masked,allowEmpty));
		std::vector<Diagnostic> diagnostics;const bool loaded=runtime.LoadDocument(source,"text-field.rui",diagnostics);
		for(const auto& d:diagnostics)std::fprintf(stderr,"%s: %s\n",d.pointer.c_str(),d.message.c_str());Check(loaded,"authored text field fixture loads");
		if(!hostBound)Check(runtime.SetState({{"name",std::string("Quicksave")}},error,time),"the name arrives as state");
		runtime.SetReducedMotion(true,time);Frame();
	}
	void Frame(double delta=.01){time+=delta;host.drawn.clear();++host.frame;runtime.Frame(viewport,time);Check(host.errors==0,"runtime reports no errors");}
	WidgetViewState Widget()const{auto w=runtime.GetWidgetState("field");Check(w.has_value(),"text field widget is available");return *w;}
	NumberEditIdentity Identity()const{Check(Widget().number.has_value(),"text editor is active");return Widget().number->identity;}
	std::string Accepted()const{const auto w=Widget();Check(std::holds_alternative<std::string>(w.accepted),"text field reads back a string");return std::get<std::string>(w.accepted);}
	void Begin(){Check(runtime.FocusControl("field",time)&&runtime.BeginNumberEdit("field",error,time),"focus and begin the text editor");Frame();}
	bool Replace(std::string_view text){
		const auto size=Widget().number->state.text.size();
		return runtime.SetNumberSelection("field",Identity(),0,size,error,time)&&runtime.ReplaceNumberSelection("field",Identity(),text,error,time);
	}
	void Text(std::string_view text){Check(Replace(text),"replace the text field's local buffer");}
	bool Commit(){return runtime.CommitNumberEdit("field",Identity(),error,time);}
	Rml::Element* Element(const char* id){
		auto* context=Rml::GetContext(0);Check(context!=nullptr,"actual Rml context available");auto* doc=context->GetDocument(0);Check(doc!=nullptr,"actual Rml document available");
		auto* element=doc->GetElementById(id);Check(element!=nullptr,"authored text field element available");return element;
	}
	// The field's painted text, as one literal Rml text node.
	std::string Painted(){
		auto* text=Element("text");if(text->GetNumChildren()!=1)return "<"+std::to_string(text->GetNumChildren())+" children>";
		auto* node=dynamic_cast<Rml::ElementText*>(text->GetChild(0));return node?node->GetText():"<markup>";
	}
};

static void DocumentRules(){
	{ View view; }
	// Each fixture fails for the named reason, not an incidental one.
	const auto rejects=[](const Json::Value& document,const char* reason,const char* message){
		TestHost host;host.quiet=true;Runtime runtime{host};std::vector<Diagnostic> diagnostics;
		const bool loaded=runtime.LoadDocument(Encode(document),"text-field-bad.rui",diagnostics);bool named=false;
		for(const auto& d:diagnostics)named=named||d.message.find(reason)!=std::string::npos;
		if(!named)for(const auto& d:diagnostics)std::fprintf(stderr,"  %s: %s\n",d.pointer.c_str(),d.message.c_str());
		Check(!loaded&&named,message);
	};
	auto numeric=Source();numeric["state"]["name"]["type"]="number";numeric["state"]["name"]["initial"]=1;
	numeric["actions"]["rename"]["input"]="number";rejects(numeric,"Text values must be strings","a text field's value must be a string");
	for(const auto* field:{"minimum","maximum","exponent","integer"}){
		auto bounded=Source();auto& control=bounded["root"]["children"][0]["control"];
		control[field]=std::string(field)=="exponent"||std::string(field)=="integer"?Json::Value(true):Json::Value(0);
		rejects(bounded,"Unknown field","a text field has no numeric bounds or syntax");
	}
	auto mistyped=Source();mistyped["actions"]["rename"]["input"]="number";
	rejects(mistyped,"matching typed input","a text field's action takes a string");
	auto empty=Source();empty["root"]["children"][0]["control"]["maxBytes"]=0;rejects(empty,"1..65536 bytes","a text field holds at least one byte");
	for(const auto* policy:{"masked","allowEmpty"}){
		auto loose=Source();loose["root"]["children"][0]["control"][policy]=1;
		rejects(loose,"Text field policy must be Boolean","a text field's mask and empty policies are Boolean");
		auto number=Source();number["state"]["name"]["type"]="number";number["state"]["name"]["initial"]=1;number["actions"]["rename"]["input"]="number";
		auto& control=number["root"]["children"][0]["control"];control["role"]="number";control["minimum"]=0;control["maximum"]=10;control[policy]=true;
		rejects(number,"Unknown field","a number has no mask or empty policy");
	}
	auto unknown=Source();unknown["root"]["children"][0]["control"]["role"]="textarea";rejects(unknown,"Supported roles","unknown control roles are rejected");
}
static void EditingAndProposals(){
	View view;
	Check(view.Accepted()=="Quicksave"&&view.Painted()=="Quicksave"&&view.host.drawn.contains('Q'),"an idle text field paints its accepted text");
	view.Begin();auto edit=*view.Widget().number;
	Check(edit.state.text=="Quicksave"&&edit.state.anchor==0&&edit.state.caret==9&&edit.status==TextNumberStatus::Valid&&!edit.dirty,"editing starts from the accepted text, all selected");
	view.Text("");view.Frame();
	Check(view.Widget().number->status==TextNumberStatus::Empty&&!view.Commit()&&view.runtime.TakeActions().empty(),"an empty name cannot be committed");
	Check(view.Element("validation")->IsVisible(true),"an empty name shows the authored validation");
	view.Text("<b>Ash & Co</b>");view.Frame();
	Check(view.Widget().number->status==TextNumberStatus::Valid&&view.Painted()=="<b>Ash & Co</b>","markup characters are literal, valid text");
	Check(!view.Element("validation")->IsVisible(true),"valid text hides the validation");
	Check(!view.Replace(std::string(33,'x'))&&view.Widget().number->state.text=="<b>Ash & Co</b>","text beyond maxBytes is refused without change");
	Check(!view.Replace("one\ntwo")&&!view.Replace("one\ttwo")&&!view.Replace("one\xe2\x80\xa8two"),"a text field is a single line");
	view.Text("Krak\xc3\xb3w \xe2\x9c\x93");Check(view.Widget().number->status==TextNumberStatus::Valid,"UTF-8 text within the limit is valid");
	view.Text("My save");
	Check(view.Commit(),"a name commits");auto actions=view.runtime.TakeActions();
	Check(actions.size()==1&&actions[0].action=="rename"&&actions[0].proposal&&std::get<std::string>(*actions[0].proposal)=="My save"&&view.Accepted()=="Quicksave",
		"one string proposal leaves the readback authoritative");
	Check(view.runtime.AcknowledgeControlProposal("field",actions[0].proposalToken,false),"a refused name is acknowledged");view.Frame();
	Check(view.Widget().number->state.text=="My save"&&view.Widget().number->dirty&&!view.Widget().number->conflict,"a refused name stays editable");
	Check(view.Commit(),"a refused name commits again");actions=view.runtime.TakeActions();
	Check(view.runtime.SetState({{"name",std::string("My save")}},view.error,view.time)&&view.runtime.AcknowledgeControlProposal("field",actions.at(0).proposalToken,true),
		"the state reads the name back before acceptance");view.Frame();
	Check(view.Accepted()=="My save"&&!view.Widget().number->dirty&&!view.Widget().number->conflict,"an accepted name rebases the editor");
	view.Text("Unread");Check(view.Commit(),"a second name commits");actions=view.runtime.TakeActions();
	Check(view.runtime.AcknowledgeControlProposal("field",actions.at(0).proposalToken,true)&&view.Widget().number->conflict&&view.Accepted()=="My save",
		"acceptance without a readback cannot manufacture the name");
	Check(view.runtime.ResolveNumberConflict("field",view.Identity(),false,view.error,view.time)&&view.Widget().number->state.text=="My save"&&!view.Widget().number->conflict,
		"reloading a conflict adopts the accepted name");
	Check(view.runtime.CancelNumberEdit("field",view.Identity(),view.time),"cancel the editor");view.Frame();
	Check(!view.Widget().number&&view.Painted()=="My save","a closed field paints its accepted name");
	Check(view.runtime.SetState({{"name",std::string("Autosave")}},view.error,view.time),"the document can replace the name");view.Frame();
	Check(view.Painted()=="Autosave","an idle field follows its state");
}
// A masked field (a password) paints one * a character over the same editor:
// carets map onto the mask, moves and deletions step whole characters, an
// empty text is its value, a refused copy says so, and the native text
// services never get it.
static void MaskedField(){
	{
		View plain;plain.Begin();NumberNativeCandidate candidate;
		Check(plain.runtime.ObserveNumberNativeCandidate(candidate)&&candidate.control=="field","a plain text field is a native text candidate");
	}
	View view(false,true,true);
	Check(view.Accepted()=="Quicksave"&&view.Painted()=="*********"&&view.host.drawn.contains('*')&&!view.host.drawn.contains('Q'),
		"an idle masked field paints one * a character, never its text");
	view.Begin();NumberNativeCandidate candidate;
	Check(view.Widget().number->masked&&view.Painted()=="*********"&&!view.runtime.ObserveNumberNativeCandidate(candidate),
		"editing keeps the mask, and a masked field is never a native text candidate");
	// "Kraków ✓": 11 bytes, 8 characters.
	const std::string word="Krak\xc3\xb3w \xe2\x9c\x93";view.Text(word);view.Frame();view.Frame();
	Check(view.Widget().number->state.text==word&&view.Painted()=="********"&&!view.host.drawn.contains('K'),"eight characters paint eight *");
	const auto caretAt=[&](std::size_t offset){
		Check(view.runtime.SetNumberSelection("field",view.Identity(),offset,offset,view.error,view.time),"place the masked caret");view.Frame();view.Frame();
		const auto geometry=view.runtime.GetNumberGeometry("field");Check(geometry.has_value(),"a masked caret has geometry");return geometry->caret.x;
	};
	// After "Krakó" (6 bytes) stands the fifth *, not the sixth.
	const float start=caretAt(0),fifth=caretAt(6),end=caretAt(word.size());
	Check(end>start&&std::abs((fifth-start)/(end-start)-5.f/8.f)<.01f,"a plain offset's caret stands at its character's * in the mask");
	const auto command=[&](TextEditCommand which){
		Check(view.runtime.NumberCommand("field",view.Identity(),which,false,view.error,view.time),"a masked command runs");view.Frame();
		return view.Widget().number->state;
	};
	Check(command(TextEditCommand::Left).caret==8&&command(TextEditCommand::Left).caret==7&&command(TextEditCommand::Home).caret==0&&
		command(TextEditCommand::Right).caret==1&&command(TextEditCommand::End).caret==word.size(),"moves step whole characters");
	const auto left=command(TextEditCommand::Backspace);
	Check(left.text=="Krak\xc3\xb3w "&&left.caret==8,"Backspace deletes the whole last character");view.Frame();
	Check(view.Painted()=="*******","the mask follows the text");
	view.Text("");view.Frame();
	Check(view.Widget().number->status==TextNumberStatus::Valid&&!view.Element("validation")->IsVisible(true)&&view.Painted().find('*')==std::string::npos,
		"an empty password is a valid value");
	Check(view.Commit(),"an empty password commits");auto actions=view.runtime.TakeActions();
	Check(actions.size()==1&&actions[0].proposal&&std::get<std::string>(*actions[0].proposal).empty(),"one empty string proposal");
	Check(view.runtime.AcknowledgeControlProposal("field",actions[0].proposalToken,false),"the empty password is acknowledged");
	view.Text("secret");view.Frame();
	Check(view.runtime.SetNumberNotice("field",view.Identity(),NumberEditNotice::ClipboardMasked,view.error,view.time),"a refused copy is said");view.Frame();
	Check(view.Element("validation")->IsVisible(true)&&view.Element("validation")->GetInnerRML()=="No copies"&&view.Painted()=="******",
		"the masked notice shows its own message over the mask");
}
// Rml keeps one live host, so each case owns its view.
static void SnapshotDocumentName(){
	View view;view.Begin();view.Text("Draft name");view.Frame();
	std::string saved;Check(view.runtime.SaveSnapshot(saved,view.error,view.time),"save a text draft");const auto data=Parse(saved);
	const auto& number=data["widgets"]["controls"]["field"]["number"];
	Check(number["state"]["text"]=="Draft name"&&number["baselineText"]=="Quicksave"&&number["baselineValue"].asDouble()==0,"the snapshot keeps the draft and its baseline text");
	Check(view.runtime.RestoreSnapshot(saved,view.error,view.time),"restore the text draft");view.Frame();
	Check(view.Widget().number&&!view.Widget().number->active&&view.Widget().number->state.text=="Draft name"&&!view.Widget().number->conflict&&view.Painted()=="Draft name",
		"a restored draft is kept, inactive");
	auto oversized=data;oversized["widgets"]["controls"]["field"]["number"]["baselineText"]=std::string(40,'x');view.host.quiet=true;
	Check(!view.runtime.RestoreSnapshot(Encode(oversized),view.error,view.time),"a baseline beyond the field's size is rejected");view.host.quiet=false;
	// Document-owned state is part of the snapshot, so the saved name returns.
	Check(view.runtime.SetState({{"name",std::string("Elsewhere")}},view.error,view.time)&&view.runtime.RestoreSnapshot(saved,view.error,view.time),"restore over a changed document name");view.Frame();
	Check(view.Accepted()=="Quicksave"&&view.Widget().number->state.text=="Draft name"&&!view.Widget().number->conflict,"the snapshot restores the document's own name with its draft");
}
// A host-bound name is read fresh, so it can disagree with a restored draft.
static void SnapshotHostConflict(){
	View view(true);Check(view.Accepted()=="Quicksave","a host-bound name reads back from the host");
	view.Begin();view.Text("Draft name");view.Frame();std::string saved;Check(view.runtime.SaveSnapshot(saved,view.error,view.time),"save a draft over a host-bound name");
	view.host.hostName="Elsewhere";Check(view.runtime.RestoreSnapshot(saved,view.error,view.time),"restore over a changed host name");view.Frame();
	Check(view.Accepted()=="Elsewhere"&&view.Widget().number->state.text=="Draft name"&&view.Widget().number->conflict&&!view.Widget().number->active,
		"a changed host name conflicts with a restored draft");
}
static void SnapshotHostRebase(){
	View view(true);view.Begin();std::string saved;Check(view.runtime.SaveSnapshot(saved,view.error,view.time),"save an unedited text editor");
	view.host.hostName="Moved";Check(view.runtime.RestoreSnapshot(saved,view.error,view.time),"restore an unedited editor over a changed host name");view.Frame();
	Check(view.Accepted()=="Moved"&&(!view.Widget().number||(view.Widget().number->state.text=="Moved"&&!view.Widget().number->conflict)),"an unedited editor adopts the changed name");
}

int main(){
	DocumentRules();
	EditingAndProposals();
	MaskedField();
	SnapshotDocumentName();
	SnapshotHostConflict();
	SnapshotHostRebase();
	std::printf("UiTextFieldTest: %u checks passed (actual Runtime/Rml, no devices or GPU).\n",checks);return 0;
}
