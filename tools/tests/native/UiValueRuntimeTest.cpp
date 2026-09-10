// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "src/ui/retained/Runtime.h"
#include <json/json.h>
#include <RmlUi/Core.h>
#include <RmlUi/Core/ElementDocument.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <set>

using namespace openq4::ui;
static unsigned checks = 0;
static void Check(bool value, const char* message) {
	++checks; if (!value) { std::fprintf(stderr,"FAIL: %s\n",message); std::exit(1); }
}
static bool Near(double a, double b) { return std::abs(a-b) < .05; }
static Json::Value Typed(const char* type, Json::Value value, const char* unit = nullptr) {
	Json::Value result; result["type"]=type; result["value"]=std::move(value); if (unit) result["unit"]=unit; return result;
}
static Json::Value Array(std::initializer_list<double> values) { Json::Value result(Json::arrayValue); for (auto value:values) result.append(value); return result; }
static Json::Value Ref(const char* state) { Json::Value result; result["state"]=state; return result; }
static std::string Text(const Json::Value& value) { Json::StreamWriterBuilder writer; writer["indentation"]=""; writer["precision"]=17; return Json::writeString(writer,value); }
static Json::Value Parse(const std::string& source) {
	Json::CharReaderBuilder builder; std::unique_ptr<Json::CharReader> reader(builder.newCharReader()); Json::Value result; std::string error;
	Check(reader->parse(source.data(),source.data()+source.size(),&result,&error),"snapshot JSON parses"); return result;
}
static Json::Value Box(const std::string& id, double x, double y, double width, double height, const char* type = "group") {
	Json::Value result; result["id"]=id; result["type"]=type; auto& p=result["properties"];
	p["position"]=Typed("keyword","absolute"); p["display"]=Typed("keyword","block"); p["opacity"]=Typed("number",1);
	p["left"]=Typed("length",x,"dp"); p["top"]=Typed("length",y,"dp"); p["width"]=Typed("length",width,"dp"); p["height"]=Typed("length",height,"dp");
	if (std::string(type)=="text") p["text"]=Typed("text","#str_label");
	else result["children"]=Json::Value(Json::arrayValue);
	return result;
}
static void Colour(Json::Value& node, std::initializer_list<double> colour) { node["properties"]["background-color"]=Typed("color",Array(colour)); }
static void AttachControl(Json::Value& source, Json::Value& node, const char* role, const char* state, const char* type) {
	const auto id=node["id"].asString(); auto& control=node["control"];
	control["role"]=role; control["label"]="#str_label"; control["value"]=Ref(state); control["action"]=id+"-edit";
	for (const auto* name:{"default","hover","focus","pressed","disabled"}) control["states"][name]=id+"-feedback";
	auto& action=source["actions"][id+"-edit"]; action["input"]=type; action["operation"]="test.edit"; action["arguments"]["value"]["input"]="value";
	Json::Value timeline, track; timeline["id"]=id+"-feedback"; timeline["durationMs"]=1;
	track["node"]=id; track["property"]="opacity";
	for (unsigned at:{0u,1u}) { Json::Value key; key["atMs"]=at; key["value"]=Typed("number",1); track["keys"].append(key); }
	timeline["tracks"].append(track); source["timelines"].append(timeline);
}
static void PropertyTimeline(Json::Value& source,const char* id,const char* node,const char* property,Json::Value from,Json::Value to,unsigned duration) {
	Json::Value timeline,track,first,last;timeline["id"]=id;timeline["durationMs"]=duration;track["node"]=node;track["property"]=property;
	first["atMs"]=0;first["value"]=std::move(from);last["atMs"]=duration;last["value"]=std::move(to);
	track["keys"].append(first);track["keys"].append(last);timeline["tracks"].append(track);source["timelines"].append(timeline);
}
static void TransformTimeline(Json::Value& source,const char* id,const char* node,Json::Value from,Json::Value to,unsigned duration) {
	PropertyTimeline(source,id,node,"transform",Typed("transform",from,"dp"),Typed("transform",to,"dp"),duration);
}
static Json::Value Slider(Json::Value& source, const char* id, const char* state, bool vertical, double x, double y) {
	const std::string prefix=id; auto node=Box(id,x,y,vertical?60:220,vertical?220:50); AttachControl(source,node,"slider",state,"number");
	auto& control=node["control"]; control["minimum"]=0; control["maximum"]=2; control["step"]=.1; control["decimals"]=2; control["orientation"]=vertical?"vertical":"horizontal";
	for (const auto* part:{"track","fill","thumb","value"}) control["parts"][part]=prefix+"-"+part;
	auto track=Box(prefix+"-track",10,10,vertical?20:200,vertical?200:20); Colour(track,{.2,.2,.2,1});
	auto fill=Box(prefix+"-fill",0,0,vertical?20:0,vertical?0:20); Colour(fill,{.8,.1,.1,1});
	if (vertical) { fill["properties"]["top"]=Typed("keyword","auto"); fill["properties"]["bottom"]=Typed("length",0,"dp"); }
	auto thumb=Box(prefix+"-thumb",0,0,20,20); Colour(thumb,{.3,.4,.9,1});
	track["children"].append(fill); track["children"].append(thumb); node["children"].append(track);
	node["children"].append(Box(prefix+"-value",vertical?30:10,vertical?10:32,vertical?30:180,18,"text")); return node;
}
// All semantic parts are real, explicitly authored canonical elements. None is
// a synthetic hit rectangle or a host widget; Runtime/RmlUi own layout and paint.
static std::string Source() {
	Json::Value source; source["format"]="openq4-ui"; source["version"]=1; source["id"]="value-runtime";
	for (const auto& [name,value]:std::map<std::string,Json::Value>{{"flag",false},{"mixed",false},{"level",1.05},{"verticalLevel",1.0},{"choice",0},{"thirdEnabled",false}}) {
		source["state"][name]["type"]=value.isBool()?"boolean":"number"; source["state"][name]["initial"]=value;
	}
	auto root=Box("root",0,0,640,400); root["properties"]["font-family"]=Typed("font","root-font"); root["properties"]["font-size"]=Typed("length",12,"dp");
	auto toggle=Box("toggle",20,20,140,40); AttachControl(source,toggle,"toggle","flag","boolean");
	toggle["control"]["mixed"]=Ref("mixed"); toggle["control"]["parts"]["checked"]="checked"; toggle["control"]["parts"]["mixed"]="mixed-mark";
	auto checked=Box("checked",5,5,20,20); Colour(checked,{0,1,0,1}); toggle["children"].append(checked);
	auto mixed=Box("mixed-mark",5,12,20,5); Colour(mixed,{1,1,0,1}); toggle["children"].append(mixed); root["children"].append(toggle);
	auto slider=Slider(source,"slider","level",false,40,80); slider["properties"]["transform"]=Typed("transform",Array({0,0,1,1,0}),"dp");
	slider["control"]["navigation"]["next"]="vertical"; root["children"].append(slider);
	root["children"].append(Slider(source,"vertical","verticalLevel",true,330,60));
	// Deliberately clipped canonical parent near the lower/right viewport edge.
	auto parent=Box("clipped-parent",480,340,180,40); parent["properties"]["overflow"]=Typed("keyword","hidden");
	parent["properties"]["pointer-events"]=Typed("keyword","auto");
	parent["properties"]["transform"]=Typed("transform",Array({0,0,1,1,0}),"dp");
	parent["properties"]["font-family"]=Typed("font","choice-font"); parent["properties"]["font-size"]=Typed("length",18,"dp");
	auto choice=Box("choice",0,0,180,30); AttachControl(source,choice,"choice","choice","number"); auto& c=choice["control"];
	c["visibleRows"]=2; c["parts"]["popup"]="popup"; c["parts"]["viewport"]="viewport"; c["parts"]["content"]="content"; c["parts"]["value"]="choice-value";
	auto popup=Box("popup",0,0,180,80), viewport=Box("viewport",0,0,180,80), content=Box("content",0,0,180,240);
	Colour(popup,{.08,.12,.16,1}); viewport["properties"]["overflow"]=Typed("keyword","hidden");
	for (unsigned i=0;i<6;++i) {
		const auto id="option-"+std::to_string(i); auto row=Box(id,0,i*40,180,40); Json::Value option;
		option["id"]=id; option["node"]=id; option["label"]="#str_options"; option["labelIndex"]=i; option["value"]=i;
		if (i==2) option["enabled"]=Ref("thirdEnabled");
		for (const auto* part:{"selected","highlight","label"}) {
			const auto partId=id+"-"+part; option["parts"][part]=partId;
			auto node=Box(partId,part==std::string("label")?12:0,0,part==std::string("label")?168:6,40,part==std::string("label")?"text":"group");
			if (part==std::string("selected")) Colour(node,{0,.7,.2,1});
			if (part==std::string("highlight")) Colour(node,{.9,.5,0,1}); row["children"].append(node);
		}
		c["options"].append(option); content["children"].append(row);
	}
	viewport["children"].append(content); popup["children"].append(viewport); choice["children"].append(popup);
	choice["children"].append(Box("choice-value",0,0,180,30,"text")); parent["children"].append(choice); root["children"].append(parent);
	source["root"]=root;
	TransformTimeline(source,"rotate-slider","slider",Array({30,20,1,1,90}),Array({30,20,1,1,90}),1);
	TransformTimeline(source,"move-choice-parent","clipped-parent",Array({0,0,1,1,0}),Array({-200,-200,1,1,0}),100);
	TransformTimeline(source,"reset-choice-parent","clipped-parent",Array({0,0,1,1,0}),Array({0,0,1,1,0}),1);
	PropertyTimeline(source,"fade-parent","clipped-parent","opacity",Typed("number",.5),Typed("number",.5),1);
	PropertyTimeline(source,"fade-popup","popup","opacity",Typed("number",.5),Typed("number",.5),1);
	PropertyTimeline(source,"unfade-parent","clipped-parent","opacity",Typed("number",1),Typed("number",1),1);
	PropertyTimeline(source,"unfade-popup","popup","opacity",Typed("number",1),Typed("number",1),1);
	source["aliases"]["parent::noevents"]["node"]="clipped-parent";
	source["aliases"]["parent::noevents"]["property"]="noevents";
	return Text(source);
}
static std::string ScrollSource(bool normalFlow, bool nested = false, bool clipped = false) {
	Json::Value source; source["format"]="openq4-ui"; source["version"]=1; source["id"]="scroll-runtime";
	source["state"]["flag"]["type"]="boolean"; source["state"]["flag"]["initial"]=false;
	auto root=Box("root",0,0,640,400), body=Box("body",20,30,240,100), content=Box("body-content",0,0,220,320);
	body["properties"]["overflow"]=Typed("keyword","auto"); Colour(body,{.1,.1,.1,1});
	for (const auto* id:{"upper","lower"}) {
		auto control=Box(id,10,std::string(id)=="upper"?10:250,160,35);
		Colour(control,std::string(id)=="upper"?std::initializer_list<double>{.7,.2,.3,1}:std::initializer_list<double>{.2,.6,.9,1});
		AttachControl(source,control,"toggle","flag","boolean");
		control["control"]["parts"]["checked"]=std::string(id)+"-checked";
		auto checked=Box(std::string(id)+"-checked",0,0,20,20); Colour(checked,{0,1,0,1}); control["children"].append(checked);
		if (std::string(id)=="upper") control["control"]["navigation"]["next"]="lower";
		content["children"].append(control);
	}
	if (normalFlow) {
		body["properties"]["display"]=Typed("keyword","flex"); body["properties"]["flex-direction"]=Typed("keyword","column");
		content["properties"]["position"]=Typed("keyword","relative"); content["properties"]["flex-shrink"]=Typed("number",0);
		content["properties"]["display"]=Typed("keyword","flex"); content["properties"]["flex-direction"]=Typed("keyword","column");
		content["properties"]["row-gap"]=Typed("length",200,"dp");
		for(auto& control:content["children"]) {
			control["properties"]["position"]=Typed("keyword","relative"); control["properties"]["top"]=Typed("length",0,"dp");
			control["properties"]["flex-shrink"]=Typed("number",0);
		}
	}
	if (nested) {
		auto wrapper=Box("flow-wrapper",0,0,220,40); wrapper["properties"]["position"]=Typed("keyword","relative");
		if (clipped) wrapper["properties"]["overflow"]=Typed("keyword","hidden");
		wrapper["children"].append(content); body["children"].append(wrapper);
	} else body["children"].append(content);
	for (const auto* id:{"body","body-content","lower"}) {
		source["aliases"][std::string(id)+"::rect"]["node"]=id; source["aliases"][std::string(id)+"::rect"]["property"]="rect";
	}
	root["children"].append(body); source["root"]=root; return Text(source);
}
struct TestHost final : Host {
	unsigned errors=0; std::uint64_t frame=1; std::vector<Vertex> vertices; std::set<std::pair<std::string,int>> fonts; std::set<unsigned> glyphs;
	std::map<std::uint32_t,std::vector<Vertex>> layers;std::uint32_t activeLayer=0;
	bool ReadFile(const std::string&,std::string&) override{return false;}
	bool ReadCVar(const std::string&,size_t,StateValue&) override{return false;}
	std::string Translate(const std::string& text) override { return text=="#str_options"?"Zero;One <tag>;Two;Three & more;Four;Five":text=="#str_label"?"Label":text; }
	void Log(bool error,const std::string& text) override {if(error){++errors;std::fprintf(stderr,"Runtime: %s\n",text.c_str());}}
	std::uintptr_t LoadMaterial(const std::string&,int& width,int& height) override{width=height=256;return 1;}
	void Draw(const std::vector<Vertex>& values,const std::vector<int>& indices,std::uintptr_t) override {
		Check(indices.size()%3==0,"triangle output"); for(auto index:indices) Check(index>=0 && static_cast<size_t>(index)<values.size(),"bounded geometry indices");
		for(auto value:values) Check(std::isfinite(value.x)&&std::isfinite(value.y),"finite widget geometry");
		auto& output=activeLayer?layers[activeLayer]:vertices;output.insert(output.end(),values.begin(),values.end());
	}
	std::uint64_t RenderFrame() const override{return frame;}
	bool BeginLayer(std::uint32_t id,int,int) override{layers[id].clear();activeLayer=id;return true;}
	void CompositeLayer(std::uint32_t a,std::uint32_t b,float opacity,const Bounds&) override{
		Check(a!=b,"distinct layer destinations");auto& output=b?layers[b]:vertices;
		for(auto v:layers.at(a)){v.r*=opacity;v.g*=opacity;v.b*=opacity;v.a*=opacity;output.push_back(v);}activeLayer=b;
	}
	void MaskLayer(std::uint32_t,std::uint32_t,const Bounds&) override{}
	void EndLayer(std::uint32_t restore) override{activeLayer=restore;}
	FontMetrics GetFontMetrics(const std::string& family,int size) override{fonts.emplace(family,size);return {size*.8f,size*.2f,size*1.2f,size*.5f};}
	Glyph GetGlyph(const std::string& family,int size,std::uint32_t codepoint) override{fonts.emplace(family,size);glyphs.insert(codepoint);return {size*.5f,0,-size*.8f,size*.5f,float(size),0,0,1,1,"font"};}
	bool HasColour(float r,float g,float b) const{return std::any_of(vertices.begin(),vertices.end(),[&](const auto& v){return Near(v.r,r)&&Near(v.g,g)&&Near(v.b,b)&&v.a>.95;});}
	bool HasInk(float r,float g,float b,float a) const{return std::any_of(vertices.begin(),vertices.end(),[&](const auto& v){return std::abs(v.r-r)<.01&&std::abs(v.g-g)<.01&&std::abs(v.b-b)<.01&&std::abs(v.a-a)<.01;});}
};
struct View {
	TestHost& host; Runtime runtime; Viewport viewport; double time=1;
	explicit View(TestHost& value,const std::string& source=Source()):host(value),runtime(value) {
		viewport.width=640;viewport.height=400;std::vector<Diagnostic> diagnostics;
		const bool loaded=runtime.LoadDocument(source,"value-runtime.q4ui",diagnostics);
		for(const auto& d:diagnostics)std::fprintf(stderr,"%s: %s\n",d.pointer.c_str(),d.message.c_str());
		Check(loaded,"real authored value-control fixture loads"); Frame();
	}
	void Frame(){host.vertices.clear();++host.frame;time+=.01;runtime.Frame(viewport,time);}
	void State(const StateValues& values){std::string error;Check(runtime.SetState(values,error,time)&&error.empty(),"authoritative state accepted");}
	void Key(MenuInput key){runtime.MenuAction(key,true,time);runtime.MenuAction(key,false,time);}
	void Point(float x,float y){runtime.PointerMove((x+viewport.originX)/viewport.pixelDensityX,(y+viewport.originY)/viewport.pixelDensityY,time);}
	void Click(float x,float y){Point(x,y);runtime.PointerButton(true,time);runtime.PointerButton(false,time);}
	WidgetViewState Widget(const char* id){auto result=runtime.GetWidgetState(id);Check(result.has_value(),"widget state available");return *result;}
	Bounds BoxOf(const char* id){Bounds result;Check(runtime.GetBounds(id,result),"authored part has real layout bounds");return result;}
	ControlAction Proposal(const char* id,double number){auto actions=runtime.TakeActions();Check(actions.size()==1 && actions[0].node==id && actions[0].proposal && std::holds_alternative<double>(*actions[0].proposal) && Near(std::get<double>(*actions[0].proposal),number) && actions[0].proposalToken!=0,"one exact typed proposal");return actions[0];}
	void Ack(const ControlAction& action,bool accepted){Check(runtime.AcknowledgeControlProposal(action.node,action.proposalToken,accepted),"current proposal acknowledged");}
};
static void ReadbackAndToggle(TestHost& host) {
	View view(host); Check(!host.HasColour(0,1,0)&&!host.HasColour(1,1,0),"unchecked authored marks are hidden");
	Check(Near(view.BoxOf("slider-fill").width,105),"off-grid accepted slider value paints without normalization");
	view.Click(30,30); auto actions=view.runtime.TakeActions();Check(actions.size()==1 && actions[0].proposal && std::get<bool>(*actions[0].proposal),"toggle proposes Boolean on matched release");
	view.Frame();Check(!std::get<bool>(view.Widget("toggle").accepted) && view.Widget("toggle").pending && !host.HasColour(0,1,0),"pending proposal does not paint as accepted");
	Check(!view.runtime.AcknowledgeControlProposal("toggle",actions[0].proposalToken+1,true),"stale acknowledgment cannot retire current proposal");
	view.Ack(actions[0],false);Check(view.Widget("toggle").rejected && !view.Widget("toggle").pending,"rejected value retained for feedback");
	view.Click(30,30);actions=view.runtime.TakeActions();view.State({{"flag",true}});view.Ack(actions[0],true);view.Frame();
	Check(host.HasColour(0,1,0) && std::get<bool>(view.Widget("toggle").accepted),"accepted host readback paints authored check");
	view.State({{"mixed",true}});view.Frame();Check(!host.HasColour(0,1,0)&&host.HasColour(1,1,0),"mixed mark excludes check mark");
	view.runtime.FocusControl("slider",view.time);view.Key(MenuInput::Right);auto proposal=view.Proposal("slider",1.1);view.Frame();
	Check(Near(view.BoxOf("slider-fill").width,105),"keyboard pending slider proposal preserves accepted paint");
	ActionInvocation invocation;std::string error;Check(view.runtime.ResolveAction(proposal.action,invocation,error,&*proposal.proposal)&&std::get<double>(invocation.arguments.at("value"))==1.1,"runtime resolves exact typed input operand");
	view.State({{"level",1.1}});view.Ack(proposal,true);view.Frame();Check(Near(view.BoxOf("slider-fill").width,110),"accepted slider readback changes fill");
}
static void ProjectedDragAndHandoff(TestHost& host) {
	View view(host);Check(view.runtime.PlayTimeline("rotate-slider",view.time),"set rotated/translated slider presentation");view.Frame();
	// Owner center (150,105), translated to (180,125). Track center y=100,
	// so a quarter-travel thumb center (105,100) projects to (185,80).
	view.Point(185,80);view.runtime.PointerButton(true,view.time);
	Check(view.Widget("slider").preview && Near(std::get<double>(*view.Widget("slider").preview),.5),"pointer fraction uses inverse projected track and thumb span");
	view.Point(350,170);Check(view.Widget("slider").preview && !view.Widget("vertical").preview,"captured slider retains ownership over competing slider");
	view.Point(185,575);view.runtime.PointerButton(false,view.time);auto proposal=view.Proposal("slider",2);
	Check(!view.Widget("slider").preview,"outside-track release clears preview and commits clamped endpoint once");view.Ack(proposal,false);
	view.Point(185,80);view.runtime.PointerButton(true,view.time);view.Key(MenuInput::Next);
	Check(view.runtime.FocusedControl()=="vertical" && !view.Widget("slider").preview,"keyboard focus handoff cancels pointer drag");
	view.runtime.PointerButton(false,view.time);Check(view.runtime.TakeActions().empty(),"old pointer release after navigation cannot commit");
	view.runtime.FocusControl("vertical",view.time);view.Key(MenuInput::End);proposal=view.Proposal("vertical",2);view.State({{"verticalLevel",2.0}});view.Ack(proposal,true);view.Frame();
	Check(Near(view.BoxOf("vertical-thumb").y,view.BoxOf("vertical-track").y)&&Near(view.BoxOf("vertical-fill").height,200),"vertical maximum paints top thumb and full bottom-anchored fill");
	view.Click(350,260);proposal=view.Proposal("vertical",0);view.Ack(proposal,false);
	view.viewport.pixelDensityX=2;view.viewport.pixelDensityY=1.5f;view.viewport.originX=30;view.viewport.originY=15;view.Frame();
	view.Point(185,80);view.runtime.PointerButton(true,view.time);view.runtime.PointerButton(false,view.time);proposal=view.Proposal("slider",.5);view.Ack(proposal,false);
}
static void PopupAndPersistence(TestHost& host) {
	View view(host);view.runtime.FocusControl("choice",view.time);view.Key(MenuInput::Accept);view.Frame();
	Check(view.Widget("choice").popupOpen && view.runtime.TakeActions().empty(),"opening choice does not propose a value");
	const auto popup=view.BoxOf("popup"), anchor=view.BoxOf("choice");
	Check(popup.y+popup.height<=anchor.y+.05 && popup.x>=0 && popup.x+popup.width<=640.05 && popup.height>=79,"popup escapes parent clip, flips above and clamps to viewport");
	Check(host.fonts.contains({"choice-font",18})&&host.glyphs.contains('<')&&host.glyphs.contains('>'),"portal retains inherited font and translated markup characters remain text");
	Check(view.runtime.PlayTimeline("move-choice-parent",view.time),"animate popup anchor presentation");
	view.time+=.04;view.Frame();const auto moved=view.BoxOf("popup");
	Check(Near(moved.x,380)&&Near(moved.y,270),"popup uses this frame's transformed anchor, flips below without a warmup render");
	Check(view.runtime.PlayTimeline("reset-choice-parent",view.time),"restore anchor transform");view.Frame();
	Check(Near(view.BoxOf("popup").x,popup.x)&&Near(view.BoxOf("popup").y,popup.y),"same-frame reverse movement restores clipped popup placement");
	Check(view.runtime.PlayTimeline("fade-parent",view.time),"fade canonical parent with open portal");view.Frame();
	Check(host.HasInk(.04f,.06f,.08f,.5f),"portal retains canonical ancestor group opacity");
	Check(view.runtime.PlayTimeline("fade-popup",view.time),"fade popup's own group");view.Frame();
	Check(host.HasInk(.02f,.03f,.04f,.25f),"portal own and ancestor opacity compose once");
	Check(view.runtime.PlayTimeline("unfade-parent",view.time)&&view.runtime.PlayTimeline("unfade-popup",view.time),"restore popup paint for interaction checks");view.Frame();
	// Row 2 lies immediately below the clipped popup and must never be chosen.
	view.State({{"thirdEnabled",true}});view.Frame();
	view.Click(popup.x+25,popup.y+popup.height+15);const auto outsideActions=view.runtime.TakeActions();
	if (!outsideActions.empty() || view.Widget("choice").popupOpen) {
		for (const auto* id:{"popup","viewport","content","choice","option-0","option-1","option-2"}) { const auto b=view.BoxOf(id);std::fprintf(stderr,"outside diagnostic %s bounds=%g,%g,%g,%g\n",id,b.x,b.y,b.width,b.height); }
		const auto widget=view.Widget("choice");std::fprintf(stderr,"outside diagnostic pointer=%g,%g actions=%zu open=%d highlight=%s first=%u focus=%s\n",popup.x+25,popup.y+popup.height+15,outsideActions.size(),widget.popupOpen,widget.highlight.c_str(),widget.firstVisible,view.runtime.FocusedControl().c_str());
		for(const auto& action:outsideActions)std::fprintf(stderr,"outside diagnostic action node=%s token=%llu\n",action.node.c_str(),static_cast<unsigned long long>(action.proposalToken));
	}
	Check(outsideActions.empty()&&!view.Widget("choice").popupOpen,"outside popup click cannot select an offscreen authored row");
	view.State({{"thirdEnabled",false}});
	view.runtime.FocusControl("choice",view.time);view.Key(MenuInput::Accept);view.Frame();
	const auto stationaryRow=view.BoxOf("option-0");const float stationaryX=stationaryRow.x+30,stationaryY=stationaryRow.y+15;
	view.Point(stationaryX,stationaryY);view.runtime.PointerWheel(1,view.time);view.Frame();view.Frame();
	Check(view.Widget("choice").highlight=="option-1","stationary pointer cannot undo wheel navigation across presentation frames");
	view.Point(stationaryX,stationaryY);Check(view.Widget("choice").highlight=="option-0","a real pointer move at the same coordinates reclaims popup hover");
	view.runtime.PointerWheel(1,view.time);view.Frame();view.runtime.PointerButton(true,view.time);view.runtime.PointerButton(false,view.time);
	auto pointerProposal=view.Proposal("choice",0);view.Ack(pointerProposal,false);
	Check(!view.Widget("choice").popupOpen,"fresh pointer button reclaims actual row selection after wheel navigation");
	view.runtime.FocusControl("choice",view.time);view.Key(MenuInput::Accept);view.Frame();
	view.runtime.PointerWheel(100,view.time);Check(view.Widget("choice").highlight=="option-1","wheel event remains one bounded row step");
	view.runtime.PointerWheel(1,view.time);Check(view.Widget("choice").highlight=="option-3","popup navigation skips disabled authored option");
	view.Frame();const auto row=view.BoxOf("option-3");view.Click(row.x+30,row.y+15);auto proposal=view.Proposal("choice",3);
	Check(std::get<double>(view.Widget("choice").accepted)==0 && !view.Widget("choice").popupOpen,"choice selection awaits authoritative host value");
	view.State({{"choice",3.0}});view.Ack(proposal,true);view.Key(MenuInput::Accept);view.Key(MenuInput::End);view.Frame();
	Check(view.Widget("choice").firstVisible==4,"scrolled choice uses stable bounded row index");
	std::string snapshot,error;Check(view.runtime.SaveSnapshot(snapshot,error,view.time),"v3 snapshot saves real value widgets");const auto parsed=Parse(snapshot);
	Check(parsed["version"].asUInt()==3 && parsed["widgets"]["controls"]["choice"]["firstVisible"].asUInt()==4,"v3 stores role and durable scroll");
	View peer(host);Check(peer.runtime.RestoreSnapshot(snapshot,error,20),"independent runtime restores widget snapshot");peer.time=20;peer.runtime.ReleaseInputSources();peer.Frame();
	Check(peer.Widget("choice").firstVisible==4 && !peer.Widget("choice").popupOpen && !peer.Widget("choice").pending && !peer.Widget("choice").preview && peer.runtime.TakeActions().empty(),"restore keeps scroll/readback but cancels popup, proposals and transient input");
	Check(view.Widget("choice").popupOpen,"restoring peer does not cancel original popup");
	for (int corruption=0;corruption<4;++corruption) {
		auto bad=parsed;
		if(corruption==0)bad["widgets"]["controls"]["choice"]["role"]=unsigned(ControlRole::Toggle);
		if(corruption==1)bad["widgets"]["controls"]["choice"]["firstVisible"]=999;
		if(corruption==2)bad["widgets"]["controls"].removeMember("slider");
		if(corruption==3)bad["version"]=2;
		const auto before=peer.runtime.GetState();const auto focus=peer.runtime.FocusedControl();
		Check(!peer.runtime.RestoreSnapshot(Text(bad),error,21)&&!error.empty(),"invalid role/scroll/count/version fails restore");
		Check(peer.runtime.GetState()==before && peer.runtime.FocusedControl()==focus && peer.Widget("choice").firstVisible==4,"failed widget restore is atomic");
	}
	peer.runtime.FocusControl("slider",peer.time);peer.Key(MenuInput::Right);auto pending=peer.Proposal("slider",1.1);
	Check(peer.runtime.SaveSnapshot(snapshot,error,peer.time),"save during a pending proposal");Check(peer.runtime.RestoreSnapshot(snapshot,error,22),"restore cancels pending proposal");peer.time=22;peer.runtime.ReleaseInputSources();peer.Frame();
	Check(!peer.Widget("slider").pending && !peer.runtime.AcknowledgeControlProposal("slider",pending.proposalToken,true),"old proposal token cannot acknowledge restored widget");
	Check(Near(peer.BoxOf("slider-fill").width,105),"restored authored base is repainted from accepted readback");
	Check(view.runtime.SetPresentationAlias("parent::noevents","1",true,error),"disable canonical ancestor while original popup remains open");view.Frame();
	Check(!view.Widget("choice").popupOpen && view.runtime.TakeActions().empty(),"same-frame eligibility update closes portal without an activation");
}
static void ScrollBodyAndFocusReveal(TestHost& host) {
	for (unsigned kind:{0u,1u,2u}) for (float scale:{1.f,2.f}) {
		const bool normalFlow=kind==0;
		View view(host,ScrollSource(normalFlow,kind==2)); view.viewport.userScale=scale; view.Frame();
		auto body=view.BoxOf("body"), lower=view.BoxOf("lower");
		Check(lower.y>=body.y+body.height,"lower authored control starts outside clipped scroll body");
		Check(view.runtime.FocusControl("lower",view.time),"explicit focus can select offscreen enabled control"); view.Frame();
		body=view.BoxOf("body"); lower=view.BoxOf("lower");
		if (!(lower.y>=body.y-.05 && lower.y+lower.height<=body.y+body.height+.05)) std::fprintf(stderr,"scroll diagnostic flow=%d scale=%g body=%g,%g,%g,%g lower=%g,%g,%g,%g focus=%s\n",normalFlow,scale,body.x,body.y,body.width,body.height,lower.x,lower.y,lower.width,lower.height,view.runtime.FocusedControl().c_str());
		Check(lower.y>=body.y-.05 && lower.y+lower.height<=body.y+body.height+.05,"explicit focus reveals complete lower control at normal and double density");
		Check(host.HasColour(.2f,.6f,.9f),"revealed lower control actually renders inside a nonempty scroll viewport");
		Check(view.runtime.FocusControl("upper",view.time),"restore upper focus"); view.Frame(); view.Key(MenuInput::Next); view.Frame();
		body=view.BoxOf("body"); lower=view.BoxOf("lower");
		Check(view.runtime.FocusedControl()=="lower" && lower.y>=body.y-.05 && lower.y+lower.height<=body.y+body.height+.05,"keyboard navigation reveals lower control through its actual scroll ancestor");
		Check(view.runtime.FocusControl("upper",view.time),"return to top for pointer wheel"); view.Frame();
		body=view.BoxOf("body"); const auto upper=view.BoxOf("upper"); view.Point(body.x+100*scale,body.y+60*scale);
		view.runtime.PointerWheel(100,view.time); view.Frame();
		Check(Near(view.BoxOf("upper").y,upper.y-36*scale) && Near(view.BoxOf("body").y,body.y),"one wheel event scrolls actual body content by bounded 36dp without moving the body");
		view.runtime.PointerWheel(-100,view.time); view.Frame();
		Check(Near(view.BoxOf("upper").y,upper.y),"opposite wheel event returns body content by one density-scaled step");
		Check(view.runtime.TakeActions().empty(),"focus reveal and body scrolling produce no value proposals");
	}
}
static void PositionedOverflowBoundaries(TestHost& host) {
	{
		View view(host,ScrollSource(false,true,true)); const auto upper=view.BoxOf("upper"), body=view.BoxOf("body");
		Check(!host.HasColour(.2f,.6f,.9f),"nested hidden wrapper clips its absolute lower control");
		view.Point(body.x+100,body.y+60);view.runtime.PointerWheel(1,view.time);view.Frame();
		Check(Near(view.BoxOf("upper").y,upper.y),"clipped nested overflow does not enlarge ancestor scroll range");
	}
	{
		View view(host,ScrollSource(false));std::string error;
		Check(view.runtime.FocusControl("lower",view.time),"focus absolute lower control before shrink");view.Frame();
		Check(view.BoxOf("upper").y<view.BoxOf("body").y,"absolute overflow starts with a real positive scroll offset");
		Check(view.runtime.SetPresentationAlias("lower::rect","10 40 160 35",true,error)&&
			view.runtime.SetPresentationAlias("body-content::rect","0 0 220 80",true,error),"shrink absolute content and move its lower child into the new extent");view.Frame();
		Check(Near(view.BoxOf("upper").y,view.BoxOf("body").y+10),"relayout shrinks scroll extent and clamps old offset instead of accumulating it");
		Check(view.runtime.SetPresentationAlias("body::rect","20 30 240 40",true,error),"resize scroll viewport smaller");view.Frame();
		auto body=view.BoxOf("body");view.Point(body.x+100,body.y+20);view.runtime.PointerWheel(1,view.time);view.Frame();
		Check(Near(view.BoxOf("upper").y,body.y+10-36),"smaller viewport exposes fresh bounded absolute scroll range");
		Check(view.runtime.SetPresentationAlias("body::rect","20 30 240 140",true,error),"resize scroll viewport larger");
		view.viewport.width=320;view.viewport.height=200;view.Frame();
		Check(Near(view.BoxOf("upper").y,view.BoxOf("body").y+10)&&host.HasColour(.2f,.6f,.9f),"larger body and resized host viewport clamp scrolling and render current content");
		Check(view.runtime.TakeActions().empty(),"scroll extent changes do not emit value proposals");
	}
	{
		// Fixed is intentionally raw RML: the canonical editor does not offer it.
		// Rml positions fixed elements against the nearest positioned ancestor.
		Runtime runtime(host);Viewport viewport;viewport.width=640;viewport.height=400;double time=1;
		const auto frame=[&]{host.vertices.clear();++host.frame;runtime.Frame(viewport,time+=.01);};
		Check(runtime.LoadMarkup(R"(<rml><head><style>
		 body { margin:0; } scrollbarvertical { width:0; } scrollbarhorizontal { height:0; }
		 #body { position:absolute; left:20px; top:30px; width:240px; height:100px; overflow:auto; }
		 #marker { position:absolute; left:10px; top:10px; width:20px; height:20px; background-color:#35ad56; }
		 #fixed { position:fixed; left:0; top:300px; width:40px; height:40px; }
		 </style></head><body><div id="body"><div id="marker"/><div id="fixed"/></div></body></rml>)","fixed-overflow.rml"),"load real Rml fixed overflow exclusion fixture");frame();
		Bounds before,after;Check(runtime.GetBounds("marker",before),"fixed fixture marker has actual layout");
		runtime.PointerMove(100,80,time);runtime.PointerWheel(1,time);frame();
		Check(runtime.GetBounds("marker",after)&&Near(after.y,before.y),"fixed child does not manufacture ancestor scroll range");
	}
}

static Json::Value* BoxFrameNode(Json::Value& node,const char* id) {
    if(node["id"].asString()==id)return &node;
    if(!node.isMember("children"))return nullptr;
    for(auto& child:node["children"])if(auto* found=BoxFrameNode(child,id))return found;
    return nullptr;
}
static void PaddedValueParts(TestHost& host) {
    for(float ratio:{1.f,1.25f,2.f})for(bool borderBox:{true,false}) {
        auto source=Parse(Source());
        for(const auto* id:{"slider-fill","vertical-fill","popup","viewport"}) {
            auto* node=BoxFrameNode(source["root"],id);Check(node!=nullptr,"framed value part exists");auto& p=(*node)["properties"];
            p["box-sizing"]=Typed("keyword",borderBox?"border-box":"content-box");
            p["border-width"]=Typed("length",1,"dp");
            p["padding-left"]=Typed("length",2,"dp");p["padding-right"]=Typed("length",3,"dp");
            p["padding-top"]=Typed("length",4,"dp");p["padding-bottom"]=Typed("length",5,"dp");
        }
        View view(host,Text(source));view.viewport.width=1280;view.viewport.height=800;view.viewport.displayScale=ratio;view.runtime.SetReducedMotion(true,view.time);view.Frame();
        Check(Near(view.BoxOf("slider-fill").width,105*ratio),"padded horizontal fill border extent matches exact accepted fraction");
        Check(Near(view.BoxOf("vertical-fill").height,100*ratio),"padded vertical fill border extent matches exact accepted fraction");
        view.State({{"level",1.5},{"verticalLevel",1.5}});view.Frame();
        Check(Near(view.BoxOf("slider-fill").width,150*ratio)&&Near(view.BoxOf("vertical-fill").height,150*ratio),"framed fills follow authoritative changes in both box models");
        Check(view.runtime.FocusControl("choice",view.time),"focus padded choice");view.Key(MenuInput::Accept);view.Frame();
        auto popup=view.BoxOf("popup"),anchor=view.BoxOf("choice");
        Check(Near(popup.width,anchor.width),"padded popup actual border width matches its anchor");
        auto* document=Rml::GetContext(0)->GetDocument(0);auto* clip=document->GetElementById("viewport");
        auto* first=document->GetElementById("option-0");auto* second=document->GetElementById("option-1");
        Check(clip&&first&&second,"padded choice keeps actual authored row parts");
        const float firstTop=first->GetAbsoluteOffset(Rml::BoxArea::Border).y;
        const float lastBottom=second->GetAbsoluteOffset(Rml::BoxArea::Border).y+second->GetBox().GetSize(Rml::BoxArea::Border).y;
        const float clipTop=clip->GetAbsoluteOffset(Rml::BoxArea::Padding).y;
        Check(Near(clip->GetClientHeight(),lastBottom-firstTop)&&firstTop>=clipTop-.05&&lastBottom<=clipTop+clip->GetClientHeight()+.05,
            "padded popup clipping area contains exactly the requested complete rows");
        Check(popup.x>=0&&popup.y>=0&&popup.x+popup.width<=view.viewport.width+.05&&popup.y+popup.height<=view.viewport.height+.05,"padded popup stays within actual host viewport");
        const auto before=popup;view.Frame();popup=view.BoxOf("popup");
        Check(Near(before.x,popup.x)&&Near(before.y,popup.y)&&Near(before.width,popup.width)&&Near(before.height,popup.height)&&view.runtime.Statistics().geometryCompiles==0,"framed popup settles without repeated geometry work");
        Check(view.runtime.TakeActions().empty(),"padding changes never create accepted value proposals");
        view.Key(MenuInput::Back);view.Frame();Check(!view.Widget("choice").popupOpen,"framed popup closes through existing ownership");
    }
}

int main() {
	TestHost host;PaddedValueParts(host);ReadbackAndToggle(host);ProjectedDragAndHandoff(host);PopupAndPersistence(host);ScrollBodyAndFocusReveal(host);PositionedOverflowBoundaries(host);
	Check(host.errors==0,"real Runtime/RmlUi reports no errors");
	std::printf("Value Runtime: %u checks passed\n",checks);
}
