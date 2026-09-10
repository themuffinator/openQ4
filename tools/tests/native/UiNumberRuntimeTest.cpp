// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "src/ui/retained/Runtime.h"
#include "src/ui/retained/NumberControlView.h"
#include <RmlUi/Core.h>
#include <RmlUi/Core/Box.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/ElementText.h>
#include <RmlUi/Core/RenderBox.h>
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
static const char* currentCase="startup";
static void Check(bool value,const char* message) { ++checks;if(!value){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);} }
static bool Near(float a,float b,float tolerance=.035f) { return std::abs(a-b)<=tolerance; }
static bool Same(const Bounds& a,const Bounds& b,float tolerance=.035f) {
	return Near(a.x,b.x,tolerance)&&Near(a.y,b.y,tolerance)&&Near(a.width,b.width,tolerance)&&Near(a.height,b.height,tolerance);
}
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
	Json::Value timeline,track;timeline["id"]="feedback";timeline["durationMs"]=1;track["node"]="number";track["property"]="opacity";
	for(unsigned t:{0u,1u}){Json::Value key;key["atMs"]=t;key["value"]=Typed("number",1);track["keys"].append(key);}timeline["tracks"].append(track);source["timelines"].append(timeline);
	Json::StreamWriterBuilder writer;writer["indentation"]="";writer["precision"]=17;return Json::writeString(writer,source);
}
struct TestHost final:Host {
	struct Material { std::string name,family;int size=0;std::uint32_t codepoint=0; };
	struct Drawn { std::uintptr_t material;std::vector<Vertex> vertices; };
	std::map<std::string,Material> descriptions;std::map<std::string,std::uintptr_t> handles;std::map<std::uintptr_t,Material> materials;
	std::vector<Drawn> draws;std::map<std::uint32_t,std::vector<Drawn>> layers;std::uint32_t layer=0;std::uint64_t frame=1;unsigned errors=0;double readback=1.05;
	bool ReadFile(const std::string&,std::string&)override{return false;}bool ReadCVar(const std::string& name,size_t type,StateValue& value)override{if(name!="r_number_test"||type!=0)return false;value=readback;return true;}
	std::string Translate(const std::string& value)override{return value=="#str_field"?"Field":value.starts_with("#str_")?"Invalid <value> & retry":value;}
	void Log(bool error,const std::string& message)override{if(error){++errors;std::fprintf(stderr,"Runtime: %s\n",message.c_str());}}
	std::uintptr_t LoadMaterial(const std::string& name,int& w,int& h)override{
		w=h=256;auto& handle=handles[name];if(!handle){handle=handles.size();materials[handle]=descriptions.contains(name)?descriptions.at(name):Material{name,{}};}return handle;
	}
	void Draw(const std::vector<Vertex>& vertices,const std::vector<int>& indices,std::uintptr_t material)override{
		Check(indices.size()%3==0,"triangle draw indices");for(auto index:indices)Check(index>=0&&static_cast<size_t>(index)<vertices.size(),"bounded draw index");
		for(auto v:vertices)Check(std::isfinite(v.x)&&std::isfinite(v.y),"finite actual Rml draw vertex");
		(layer?layers[layer]:draws).push_back({material,vertices});
	}
	std::uint64_t RenderFrame()const override{return frame;}bool BeginLayer(std::uint32_t id,int,int)override{layers[id].clear();layer=id;return true;}
	void CompositeLayer(std::uint32_t source,std::uint32_t destination,float opacity,const Bounds&)override{
		auto& out=destination?layers[destination]:draws;for(auto draw:layers.at(source)){for(auto& v:draw.vertices){v.r*=opacity;v.g*=opacity;v.b*=opacity;v.a*=opacity;}out.push_back(std::move(draw));}layer=destination;
	}
	void MaskLayer(std::uint32_t,std::uint32_t,const Bounds&)override{}void EndLayer(std::uint32_t restore)override{layer=restore;}
	FontMetrics GetFontMetrics(const std::string&,int size)override{return {size*.775f,size*.225f,size*1.4f,size*.5f};}
	static float Advance(int size,std::uint32_t cp){return size*(cp=='1'?.31f:cp=='.'?.17f:cp=='0'?.49f:.47f)+(cp=='0'?.0625f:.1875f);}
	Glyph GetGlyph(const std::string& family,int size,std::uint32_t cp)override{
		const auto name="font-"+family+"-"+std::to_string(size)+"-"+std::to_string(cp);descriptions[name]={name,family,size,cp};
		return {Advance(size,cp),.375f,-size*.775f,Advance(size,cp)*.8f,float(size),0,0,1,1,name};
	}
	std::optional<Bounds> Extent(const std::function<bool(const Drawn&,const Vertex&)>& keep)const{
		float left=std::numeric_limits<float>::infinity(),top=left,right=-left,bottom=-left;
		for(const auto& draw:draws)for(const auto& v:draw.vertices)if(keep(draw,v)){left=std::min(left,v.x);right=std::max(right,v.x);top=std::min(top,v.y);bottom=std::max(bottom,v.y);}
		if(!std::isfinite(left))return {};return Bounds{left,top,right-left,bottom-top};
	}
	std::optional<Bounds> Ink(std::uint32_t cp)const{return Extent([&](const Drawn& d,const Vertex&){const auto m=materials.find(d.material);return m!=materials.end()&&m->second.family=="number-font"&&m->second.codepoint==cp;});}
	std::optional<Bounds> Colour(float r,float g,float b)const{return Extent([&](const Drawn&,const Vertex& v){return Near(v.r,r,.002f)&&Near(v.g,g,.002f)&&Near(v.b,b,.002f)&&v.a>.99f;});}
	int FontSize()const{for(const auto& draw:draws){const auto m=materials.find(draw.material);if(m!=materials.end()&&m->second.family=="number-font")return m->second.size;}return 0;}
};
struct View {
	TestHost host;Runtime runtime{host};Viewport viewport;double time=1;std::string error,source;
	View(float ratio=1,bool transformed=false,bool framed=false,bool hostSource=false){
		viewport.width=1280;viewport.height=720;viewport.displayScale=ratio;
		source=Source(transformed,framed,hostSource);std::vector<Diagnostic> diagnostics;const bool loaded=runtime.LoadDocument(source,"number-runtime.q4ui",diagnostics);
		for(const auto& d:diagnostics)std::fprintf(stderr,"%s: %s\n",d.pointer.c_str(),d.message.c_str());Check(loaded,"authored Number runtime fixture loads");
		runtime.SetReducedMotion(true,time);Frame();
	}
	void Frame(double delta=.01){time+=delta;host.draws.clear();++host.frame;runtime.Frame(viewport,time);Check(host.errors==0,"runtime reports no errors");}
	WidgetViewState Widget()const{auto w=runtime.GetWidgetState("number");Check(w.has_value(),"Number widget is available");return *w;}
	NumberEditIdentity Identity()const{Check(Widget().number.has_value(),"Number editor is active");return Widget().number->identity;}
	void Begin(){Check(runtime.FocusControl("number",time)&&runtime.BeginNumberEdit("number",error,time),"focus and begin real Number editor");Frame();}
	void Selection(size_t anchor,size_t caret){Check(runtime.SetNumberSelection("number",Identity(),anchor,caret,error,time),"set real Number selection");}
	void Text(std::string_view text){Selection(0,Widget().number->state.text.size());Check(runtime.ReplaceNumberSelection("number",Identity(),text,error,time),"replace real Number local buffer");}
	NumberTextGeometry Geometry(){const auto g=runtime.GetNumberGeometry("number");
		if(!g){
			const auto w=Widget();std::fprintf(stderr,"missing geometry case=%s density=%g time=%g active=%d text=%s\n",currentCase,viewport.DpRatio(),time,w.number?w.number->active:false,w.number?w.number->state.text.c_str():"<none>");
			for(const auto* id:{"viewport","text","caret","selection","composition"}){auto* e=Element(id);const auto p=e->GetAbsoluteOffset(Rml::BoxArea::Border),s=e->GetBox().GetSize(Rml::BoxArea::Border);std::fprintf(stderr,"  %s visible=%d border=%g,%g,%g,%g\n",id,e->IsVisible(true),p.x,p.y,s.x,s.y);}
		}
		Check(g.has_value(),"current Number geometry available after one frame");Check(g->identity==Identity(),"geometry belongs to exact edit revision");return *g;}
	Rml::Element* Element(const char* id){auto* context=Rml::GetContext(0);Check(context!=nullptr,"actual Rml context available");auto* doc=context->GetDocument(0);Check(doc!=nullptr,"actual Rml document available");auto* element=doc->GetElementById(id);Check(element!=nullptr,"actual authored Number element available");return element;}
	Bounds BoxOf(const char* id){Bounds b;Check(runtime.GetBounds(id,b),"layout bounds available");return b;}
};
static Bounds SnappedPaintBox(Rml::Element* element){
	// Rml Geometry::Render rounds the local translation; GetRenderBox provides
	// its separately rounded mesh extents. Reconstruct that documented path,
	// then forward-project through this fixture's common affine transform.
	// Element::Project supplies its inverse without exposing private matrices.
	const auto render=element->GetRenderBox(Rml::BoxArea::Border);
	const auto origin=element->GetAbsoluteOffset(Rml::BoxArea::Border).Round()+render.GetBorderOffset(),size=render.GetFillSize();
	Rml::Vector2f o(0,0),x(1,0),y(0,1);Check(element->Project(o)&&element->Project(x)&&element->Project(y),"invertible fixture affine transform");
	x-=o;y-=o;const double determinant=double(x.x)*y.y-double(y.x)*x.y;Check(std::abs(determinant)>1e-8,"nondegenerate fixture projection");
	float left=std::numeric_limits<float>::infinity(),top=left,right=-left,bottom=-left;
	for(const auto& local:std::vector<Rml::Vector2f>{origin,origin+Rml::Vector2f(size.x,0),origin+size,origin+Rml::Vector2f(0,size.y)}){
		const auto delta=local-o;const float px=static_cast<float>((double(delta.x)*y.y-double(y.x)*delta.y)/determinant);
		const float py=static_cast<float>((double(x.x)*delta.y-double(delta.x)*x.y)/determinant);
		left=std::min(left,px);right=std::max(right,px);top=std::min(top,py);bottom=std::max(bottom,py);
	}
	return {left,top,right-left,bottom-top};
}
static void FractionalDrawingAndDensity(){
	for(float ratio:{1.f,1.25f,2.f}){
		View view(ratio);Check(!view.Widget().number&&!view.host.Colour(0,0,1),"inactive field paints accepted text without caret");view.Begin();
		view.Selection(2,2);Check(!view.runtime.GetNumberGeometry("number"),"edit revision invalidates old native caret geometry before frame");view.Frame();
		const auto geometry=view.Geometry();const auto caret=view.host.Colour(0,0,1),one=view.host.Ink('1'),zero=view.host.Ink('0');
		Check(caret&&one&&zero,"real glyph and caret geometry submitted");
		const auto snapped=SnappedPaintBox(view.Element("caret"));
		if(!Same(*caret,snapped)){
			const auto b=view.BoxOf("caret");const auto t=view.BoxOf("text");const auto v=view.BoxOf("viewport");
			std::fprintf(stderr,"caret mismatch density=%g raw=%g,%g,%g,%g api=%g,%g,%g,%g layout=%g,%g,%g,%g text=%g,%g,%g,%g viewport=%g,%g,%g,%g glyph1=%g,%g glyph0=%g,%g scroll=%g\n",ratio,caret->x,caret->y,caret->width,caret->height,geometry.caret.x,geometry.caret.y,geometry.caret.width,geometry.caret.height,b.x,b.y,b.width,b.height,t.x,t.y,t.width,t.height,v.x,v.y,v.width,v.height,one->x,one->y,zero->x,zero->y,geometry.scroll);
		}
		Check(Same(*caret,snapped),"actual drawn caret follows exact Rml snapped box path");
		const auto size=view.host.FontSize();Check(size>0,"actual font size observed");
		const float prefix=TestHost::Advance(size,'1')+TestHost::Advance(size,'.')+.5f*ratio;
		Check(Near(zero->x-one->x,prefix),"glyph pens preserve fractional advances plus exact letter spacing");
		if(!Near(geometry.caret.x,zero->x-.375f)){
			auto* ink=view.Element("text")->GetChild(0);const auto origin=ink->GetAbsoluteOffset(Rml::BoxArea::Content);
			std::fprintf(stderr,"fractional pen mismatch density=%g size=%d apiCaret=%g glyph0Pen=%g glyph1=%g glyph0=%g prefix=%g textOrigin=%g,%g roundedOrigin=%g,%g\n",ratio,size,geometry.caret.x,zero->x-.375f,one->x,zero->x,prefix,origin.x,origin.y,origin.Round().x,origin.Round().y);
		}
		Check(Near(geometry.caret.x,zero->x-.375f),"caret uses exact glyph pen rather than rounded prefix width");
		Check(Near(geometry.viewport.width,90*ratio),"field viewport follows effective density");
		const auto before=geometry;view.Frame(0);Check(Same(before.caret,view.Geometry().caret)&&view.runtime.Statistics().geometryCompiles==0,"repeated same-time paint settles without geometry rebuild");
	}
}
static void FramedReversedSelectionAndTransforms(){
	for(bool transformed:{false,true}){
		View view(1.25f,transformed,true);view.Begin();view.Selection(4,1);view.Frame();
		const auto geometry=view.Geometry();const auto caret=view.host.Colour(0,0,1),selection=view.host.Colour(1,0,0);
		Check(caret&&selection&&Same(*caret,SnappedPaintBox(view.Element("caret"))),"framed transformed caret paint follows exact Rml snapped box path");
		if(!transformed){
			const int size=view.host.FontSize();const float spacing=.25f*view.viewport.DpRatio();
			const float expected=TestHost::Advance(size,'.')+TestHost::Advance(size,'0')+TestHost::Advance(size,'5')+3*spacing;
			const auto logicalSelection=view.BoxOf("selection");
			Check(Near(logicalSelection.x,geometry.caret.x)&&Near(logicalSelection.width,expected)&&Same(*selection,SnappedPaintBox(view.Element("selection"))),"reversed selection uses shared fractional run and explicitly snapped common paint origin");
		}
		view.Frame(0);Check(Same(geometry.caret,view.Geometry().caret)&&view.runtime.Statistics().geometryCompiles==0,"common transform and CSS frames settle without drift");
	}
}
static void ScrollEmptyAndPreedit(){
	View view;view.Begin();view.Text("1.012345678901234567890123456789");const auto end=view.Widget().number->state.text.size();view.Selection(end,end);view.Frame();
	auto geometry=view.Geometry();Check(geometry.scroll>0&&geometry.caret.x>=geometry.viewport.x-.04f&&geometry.caret.x+geometry.caret.width<=geometry.viewport.x+geometry.viewport.width+.04f,"long local text scroll reveals its caret");
	const auto ink=view.host.Extent([&](const TestHost::Drawn& draw,const Vertex&){const auto m=view.host.materials.find(draw.material);return m!=view.host.materials.end()&&m->second.family=="number-font";});
	Check(ink&&ink->x>=geometry.viewport.x-.04f&&ink->x+ink->width<=geometry.viewport.x+geometry.viewport.width+.04f,"actual long glyph geometry is clipped to field viewport");
	const auto endScroll=geometry.scroll;view.Selection(end,0);view.Frame();geometry=view.Geometry();
	Check(geometry.scroll<endScroll&&geometry.caret.x>=geometry.viewport.x&&geometry.caret.x+geometry.caret.width<=geometry.viewport.x+geometry.viewport.width,"reversed selection reveals its first-byte caret with reduced scroll");
	const auto first=view.host.Ink('1');Check(first&&Near(geometry.caret.x,first->x-.375f),"first-byte caret still matches actual glyph origin after reverse scroll");
	view.Text("");Check(!view.runtime.GetNumberGeometry("number"),"empty replacement invalidates prior geometry immediately");view.Frame();geometry=view.Geometry();
	Check(view.Widget().number->state.text.empty()&&view.host.Colour(0,0,1)&&!view.host.Ink('1'),"empty field retains measured caret without stale glyph ink");
	Check(view.Element("validation")->IsVisible(true),"invalid intermediate empty text shows authored validation");
	view.Text("1.05");view.Selection(1,3);TextInputEvent event;Check(MakeTextInputPreedit("75",TextIndexUnit::Utf8Bytes,1,1,event,view.error),"build preedit with selection");
	Check(view.runtime.ApplyNumberInput("number",view.Identity(),event,view.error,view.time),"real Runtime accepts preedit presentation");view.Frame();geometry=view.Geometry();
	Check(view.Widget().number->state.text=="1.05"&&view.Widget().number->composition&&view.host.Colour(0,1,0)&&view.host.Colour(1,0,0),"preedit underline/selection render without rewriting local accepted text");
	Check(!view.runtime.CommitNumberEdit("number",view.Identity(),view.error,view.time)&&view.runtime.TakeActions().empty(),"rendered preedit cannot become a numeric setting");
	MakeTextInputCancel(event);Check(view.runtime.ApplyNumberInput("number",view.Identity(),event,view.error,view.time),"cancel composition through Runtime");view.Frame();
	Check(!view.host.Colour(0,1,0)&&view.Widget().number->state.text=="1.05","cancellation removes only composition paint");
}
static void ValidationProposalsAndIdentity(){
	View view;view.Begin();view.Text("<bad&value>");view.Frame();
	Check(view.Element("text")->GetNumChildren()==1&&view.Element("text")->GetChild(0)->GetTagName()=="#text","local markup metacharacters remain literal glyph text");
	Check(view.Element("validation")->IsVisible(true)&&view.Element("validation")->GetNumChildren()==1&&view.Element("validation")->GetChild(0)->GetTagName()=="#text","localized validation metacharacters cannot create markup");
	Check(view.Widget().number->status==TextNumberStatus::Invalid&&view.runtime.TakeActions().empty(),"invalid text remains editable without proposals");
	view.Text("1.15");view.Frame();const auto prior=view.Geometry();
	Check(view.runtime.CommitNumberEdit("number",view.Identity(),view.error,view.time),"explicit Number Runtime commit succeeds");const auto actions=view.runtime.TakeActions();
	Check(actions.size()==1&&actions[0].proposal&&std::get<double>(*actions[0].proposal)==1.15&&std::get<double>(view.Widget().accepted)==1.05,"one immutable exact proposal leaves readback authoritative");
	Check(view.runtime.AcknowledgeControlProposal("number",actions[0].proposalToken,false),"reject runtime numeric proposal");
	Check(!view.runtime.GetNumberGeometry("number"),"acknowledgement revision retires old candidate rectangle");view.Frame();
	Check(view.Widget().number->state.text=="1.15"&&view.Element("validation")->IsVisible(true),"rejected explicit text remains editable and visibly invalid");
	view.Text("1.25");Check(view.runtime.CommitNumberEdit("number",view.Identity(),view.error,view.time),"edited correction commits");const auto corrected=view.runtime.TakeActions().at(0);
	Check(view.runtime.SetState({{"value",1.25}},view.error,view.time)&&view.runtime.AcknowledgeControlProposal("number",corrected.proposalToken,true),"matching authoritative state precedes acknowledgement");view.Frame();
	Check(!view.Widget().number->dirty&&!view.Element("validation")->IsVisible(true),"accepted correction clears validation and rebases editor");
	const auto stale=view.Identity();Check(view.runtime.CancelNumberEdit("number",stale,view.time),"cancel real editor");
	Check(!view.runtime.GetNumberGeometry("number")&&!view.Widget().number,"cancellation immediately removes native geometry ownership");view.Frame();Check(!view.host.Colour(0,0,1),"canceled caret is no longer drawn");
	Check(view.runtime.BeginNumberEdit("number",view.error,view.time),"rebegin after cancellation");Check(!view.runtime.ReplaceNumberSelection("number",stale,"2",view.error,view.time),"old edit identity cannot enter reopened field");view.Frame();
	Check(view.Geometry().identity.session!=prior.identity.session,"fresh view geometry belongs to a new session");
}
static void DetachedDraftAndNativeAffinity(){
	View view;view.Begin();view.Text("1e-");view.Selection(3,1);const auto identity=view.Identity();
	TextInputEvent event;Check(MakeTextInputPreedit("5",TextIndexUnit::Utf8Bytes,1,0,event,view.error),"build detached-draft preedit");
	Check(view.runtime.ApplyNumberInput("number",identity,event,view.error,view.time),"install pending composition before focus loss");
	const auto compositionIdentity=view.Identity();Check(view.runtime.FocusControl("",view.time),"focus leaves Number field");
	const auto detached=view.Widget();Check(detached.number&&!detached.number->active&&detached.number->state.text=="1e-"&&!detached.number->composition&&
		detached.number->identity==NumberEditIdentity{}&&detached.number->canUndo,"focus loss retains invalid local draft/history while retiring composition and native identity");
	Check(!view.runtime.GetNumberGeometry("number")&&!view.runtime.ApplyNumberInput("number",compositionIdentity,event,view.error,view.time),"old native input and geometry cannot cross focus loss");
	view.Frame();Check(!view.host.Colour(0,0,1)&&view.Element("validation")->IsVisible(true),"inactive draft stays visible without active caret");
	view.Begin();Check(view.Widget().number->active&&view.Widget().number->state.text=="1e-"&&view.Identity().session!=identity.session,"rebegin resumes retained draft with fresh native identity");
	Check(view.runtime.UndoNumberEdit("number",view.Identity(),false,view.error,view.time)&&view.Widget().number->state.text=="1.05","retained inactive draft preserves undo history");
	view.Text("1.35");const auto cancelIdentity=view.Identity();view.runtime.CancelInput(view.time);
	Check(view.Widget().number&&!view.Widget().number->active&&view.Widget().number->state.text=="1.35"&&!view.runtime.GetNumberGeometry("number"),"window-input quarantine detaches rather than loses the draft");
	Check(!view.runtime.CommitNumberEdit("number",cancelIdentity,view.error,view.time)&&view.runtime.TakeActions().empty(),"quarantined input cannot submit retained draft");
	Check(view.runtime.CancelNumberEdit("number",{},view.time)&&!view.Widget().number,"explicit engine-owner cancel discards detached draft");
}
static void UnavailableLayoutHidesStaleInk(){
	View view;view.Begin();view.Selection(1,3);TextInputEvent event;Check(MakeTextInputPreedit("75",TextIndexUnit::Utf8Bytes,0,1,event,view.error),"build visible composition");
	Check(view.runtime.ApplyNumberInput("number",view.Identity(),event,view.error,view.time),"install visible composition");view.Frame();
	Check(view.host.Colour(0,0,1)&&view.host.Colour(1,0,0)&&view.host.Colour(0,1,0),"all editing ink initially visible");
	MakeTextInputCancel(event);Check(view.runtime.ApplyNumberInput("number",view.Identity(),event,view.error,view.time),"cancel old preedit before changing layout");view.Text("1.75");
	Check(view.runtime.SetPresentationAlias("viewport::rect","12 8 0 36",true,view.error),"collapse viewport through canonical presentation alias");view.Frame();
	Check(!view.runtime.GetNumberGeometry("number")&&!view.host.Colour(0,0,1)&&!view.host.Colour(1,0,0)&&!view.host.Colour(0,1,0),"changed text plus unavailable layout cannot reuse old editing ink or candidate geometry");
	Check(view.runtime.SetPresentationAlias("viewport::rect","12 8 90 36",true,view.error),"restore viewport width");view.Frame();Check(view.Geometry().identity==view.Identity(),"fresh layout returns only current edit geometry");
}
static Json::Value Parse(const std::string& source){
	Json::CharReaderBuilder builder;std::unique_ptr<Json::CharReader> reader(builder.newCharReader());Json::Value result;std::string error;
	Check(reader->parse(source.data(),source.data()+source.size(),&result,&error),"snapshot JSON parses");return result;
}
static std::string Encode(const Json::Value& value){Json::StreamWriterBuilder writer;return Json::writeString(writer,value);}
static void SnapshotDraftsAndResourceRecreation(){
	View view;view.Begin();view.Text("1.25");view.Text("1e-");view.Text("bad");
	Check(view.runtime.UndoNumberEdit("number",view.Identity(),false,view.error,view.time),"prepare invalid draft with both history directions");view.Selection(2,0);
	TextInputEvent event;Check(MakeTextInputPreedit("2",TextIndexUnit::Utf8Bytes,1,0,event,view.error),"prepare transient preedit before snapshot");
	Check(view.runtime.ApplyNumberInput("number",view.Identity(),event,view.error,view.time),"install transient preedit before snapshot");view.Frame();const auto old=view.Identity();
	std::string saved;Check(view.runtime.SaveSnapshot(saved,view.error,view.time),"save real Number instance snapshot");const auto data=Parse(saved);
	Check(data["version"].asUInt()==3&&data["widgets"]["version"].asUInt()==2,"Number uses existing outer snapshot and nested widget v2");
	const auto& number=data["widgets"]["controls"]["number"]["number"];
	Check(number["state"]["text"]=="1e-"&&number["state"]["anchor"].asUInt()==2&&number["state"]["caret"].asUInt()==0&&number["undo"].size()==2&&number["redo"].size()==1,"snapshot retains invalid text, reversed selection and bounded undo/redo");
	Check(number.getMemberNames().size()==6&&!number.isMember("composition")&&!number.isMember("identity")&&!number.isMember("proposal"),"Number snapshot contains only durable editor fields");
	Check(view.runtime.RestoreSnapshot(saved,view.error,view.time),"restore Number draft into actual Runtime");
	Check(view.Widget().number&&!view.Widget().number->active&&!view.Widget().number->composition&&view.Widget().number->identity==NumberEditIdentity{}&&!view.runtime.GetNumberGeometry("number"),"restored draft owns no active input/preedit/candidate rectangle");
	view.Frame();Check(view.Widget().number->state.text=="1e-"&&!view.host.Colour(0,0,1)&&!view.host.Colour(1,0,0)&&!view.host.Colour(0,1,0),"restored inactive text paints without stale editor ink");
	view.Begin();Check(view.Identity().session!=old.session&&!view.runtime.ApplyNumberInput("number",old,event,view.error,view.time),"restored field requires a new native edit session");
	Check(view.runtime.UndoNumberEdit("number",view.Identity(),true,view.error,view.time)&&view.Widget().number->state.text=="bad","redo survives JSON instance restore");
	Check(view.runtime.UndoNumberEdit("number",view.Identity(),false,view.error,view.time)&&view.runtime.UndoNumberEdit("number",view.Identity(),false,view.error,view.time)&&view.Widget().number->state.text=="1.25","undo survives JSON instance restore");view.Frame();
	for(unsigned mutation=0;mutation<8;++mutation){
		auto bad=data;auto& n=bad["widgets"]["controls"]["number"]["number"];
		if(mutation==0)bad["widgets"]["version"]=3;
		if(mutation==1)n["state"]["caret"]=10000;
		if(mutation==2)n["state"]["anchor"]=-1;
		if(mutation==3)n["baselineValue"]="1.05";
		if(mutation==4)n["baselineText"]="different";
		if(mutation==5)n["undo"][0]["text"]=std::string("bad\0text",8);
		if(mutation==6)n["identity"]=1;
		if(mutation==7)bad["widgets"]["controls"]["number"]["role"]=unsigned(ControlRole::Slider);
		const auto identity=view.Identity();const auto before=view.Widget().number->state;const auto geometry=view.Geometry();
		Check(!view.runtime.RestoreSnapshot(Encode(bad),view.error,view.time)&&!view.error.empty(),"malformed durable Number snapshot rejected");
		Check(view.Identity()==identity&&view.Widget().number->state.text==before.text&&Same(view.Geometry().caret,geometry.caret)&&view.runtime.TakeActions().empty(),"late snapshot failure preserves live editor, geometry and action state");
	}
	view.runtime.Shutdown();std::vector<Diagnostic> diagnostics;Check(view.runtime.LoadDocument(view.source,"number-runtime.q4ui",diagnostics),"resource-style recreation reloads exact source");
	Check(view.runtime.RestoreSnapshot(saved,view.error,view.time),"restore durable Number draft after complete Runtime resource recreation");view.Frame();
	Check(view.Widget().number&&!view.Widget().number->active&&view.Widget().number->state.text=="1e-"&&!view.runtime.GetNumberGeometry("number"),"recreated field retains draft without old native ownership");
	view.Begin();Check(view.Geometry().identity.session!=old.session,"recreated active field uses fresh rendered geometry and session");
}
static void SnapshotFreshHostConflict(){
	View view(1,false,false,true);view.Begin();view.Text("1.25");view.Frame();std::string snapshot;
	Check(view.runtime.SaveSnapshot(snapshot,view.error,view.time),"save Number draft based on host source");
	view.host.readback=1.75;Check(view.runtime.RestoreSnapshot(snapshot,view.error,view.time),"restore with changed fresh host readback");view.Frame();
	Check(std::get<double>(view.Widget().accepted)==1.75&&view.Widget().number&&view.Widget().number->state.text=="1.25"&&view.Widget().number->conflict&&!view.Widget().number->active,"fresh host readback wins while restored conflicting draft stays visible/inactive");
	Check(!view.runtime.GetNumberGeometry("number")&&view.runtime.TakeActions().empty(),"restored conflict cannot reactivate native input or replay setting actions");
	view.Begin();const auto conflict=view.Identity();Check(view.Widget().number->conflict&&view.Widget().number->state.text=="1.25","begin preserves conflicted text until explicit decision");
	Check(!view.runtime.CommitNumberEdit("number",conflict,view.error,view.time),"active unresolved conflict cannot commit");
	Check(view.runtime.ResolveNumberConflict("number",conflict,true,view.error,view.time)&&!view.Widget().number->conflict&&view.Widget().number->state.text=="1.25"&&view.Widget().number->dirty&&view.Identity()!=conflict,"Keep draft adopts current baseline with a fresh revision and preserves text/history");
	Check(!view.runtime.ResolveNumberConflict("number",conflict,false,view.error,view.time)&&view.runtime.TakeActions().empty(),"stale conflict decision cannot reload or submit current draft");
	Check(view.runtime.CommitNumberEdit("number",view.Identity(),view.error,view.time),"explicit commit after Keep draft");const auto action=view.runtime.TakeActions().at(0);
	Check(std::get<double>(*action.proposal)==1.25&&std::get<double>(view.Widget().accepted)==1.75,"conflict decision itself never overwrites authoritative host setting");
	Check(view.runtime.AcknowledgeControlProposal("number",action.proposalToken,false),"reject corrected host proposal for reload case");
	view.host.readback=1.5;view.Frame();Check(view.Widget().number->conflict,"new external change creates another visible conflict");
	Check(view.runtime.ResolveNumberConflict("number",view.Identity(),false,view.error,view.time),"explicit Reload resolves current conflict");view.Frame();
	Check(view.Widget().number->state.text=="1.5"&&!view.Widget().number->dirty&&!view.Widget().number->conflict&&!view.Widget().number->canUndo&&!view.Widget().number->canRedo&&view.runtime.TakeActions().empty(),"Reload adopts exact host readback and clears local history without submitting");
}
static void SnapshotPendingProposalDoesNotReplay(){
	View view;view.Begin();view.Text("1.25");Check(view.runtime.CommitNumberEdit("number",view.Identity(),view.error,view.time),"queue pending proposal before save");const auto old=view.runtime.TakeActions().at(0);
	std::string snapshot;Check(view.runtime.SaveSnapshot(snapshot,view.error,view.time)&&view.runtime.RestoreSnapshot(snapshot,view.error,view.time),"roundtrip pending Number edit as durable local draft");view.Frame();
	Check(std::get<double>(view.Widget().accepted)==1.05&&view.Widget().number&&view.Widget().number->state.text=="1.25"&&!view.Widget().number->active&&!view.Widget().pending&&view.Widget().proposalToken==0,"snapshot retains attempted text but no acceptance or queued proposal");
	Check(!view.runtime.CanDispatchControlAction(old,view.time)&&!view.runtime.AcknowledgeControlProposal("number",old.proposalToken,true)&&view.runtime.TakeActions().empty(),"old taken proposal and acknowledgement cannot replay after restore");
	view.Begin();Check(view.runtime.CommitNumberEdit("number",view.Identity(),view.error,view.time),"resumed draft requires a new explicit commit");const auto fresh=view.runtime.TakeActions();
	Check(fresh.size()==1&&fresh[0].proposalToken!=old.proposalToken&&fresh[0].editSession!=old.editSession&&std::get<double>(*fresh[0].proposal)==1.25,"resumed draft dispatches once with fresh proposal and edit identity");
}
static void HostAcknowledgementBetweenFrames(){
	View view(1,false,false,true);view.Begin();view.Text("1.25");Check(view.runtime.CommitNumberEdit("number",view.Identity(),view.error,view.time),"commit host-backed Number candidate");
	const auto action=view.runtime.TakeActions().at(0);view.host.readback=1.25;
	Check(view.runtime.AcknowledgeControlProposal("number",action.proposalToken,true),"acknowledgement observes fresh host value before a presentation Frame");
	Check(std::get<double>(view.Widget().accepted)==1.25&&view.Widget().number->state.text=="1.25"&&!view.Widget().number->dirty&&!view.Widget().number->conflict&&!view.Widget().pending,"host-backed acknowledgement rebases exact readback without false conflict");
	const auto previous=view.Identity();view.host.readback=1.5;TextInputEvent event;Check(MakeTextInputCommit("2",event,view.error),"build input before fresh host observation");
	Check(!view.runtime.ApplyNumberInput("number",previous,event,view.error,view.time),"pre-edit host observation retires stale clean-editor identity before input is applied");
	Check(std::get<double>(view.Widget().accepted)==1.5&&view.Widget().number->state.text=="1.5"&&!view.Widget().number->dirty&&view.runtime.TakeActions().empty(),"fresh external host value remains authoritative between Frames");view.Frame();
}
static void HostChangeBeforeDispatch(){
	{
	View view(1,false,false,true);view.Begin();view.Text("1.25");
	Check(view.runtime.CommitNumberEdit("number",view.Identity(),view.error,view.time),"queue host-backed proposal before delayed dispatch");
	const auto action=view.runtime.TakeActions().at(0);
	Check(view.runtime.CanDispatchControlAction(action,view.time),"unchanged authoritative baseline permits current proposal");
	view.host.readback=1.75;
	Check(!view.runtime.CanDispatchControlAction(action,view.time),"external host change between Frames rejects taken proposal before host write");
	Check(std::get<double>(view.Widget().accepted)==1.75&&view.Widget().number->state.text=="1.25"&&view.Widget().number->conflict&&!view.Widget().pending,"dispatch guard observes actual accepted value while preserving conflicting local text");
	}
	View unavailable(1,false,false,true);unavailable.Begin();unavailable.Text("1.25");
	Check(unavailable.runtime.CommitNumberEdit("number",unavailable.Identity(),unavailable.error,unavailable.time),"queue proposal before unavailable host source");
	const auto blocked=unavailable.runtime.TakeActions().at(0);unavailable.host.readback=std::numeric_limits<double>::quiet_NaN();
	Check(!unavailable.runtime.CanDispatchControlAction(blocked,unavailable.time),"invalid fresh host source fails closed before delayed dispatch");
	Check(std::get<double>(unavailable.Widget().accepted)==1.05&&unavailable.Widget().number->state.text=="1.25","invalid readback cannot publish accepted data or destroy unconfirmed text");
}
static void AuthoredFixture(const char* path){
	std::ifstream file(path,std::ios::binary);Check(file.good(),"authored Number fixture path opens");std::ostringstream bytes;bytes<<file.rdbuf();Check(!file.bad(),"authored Number fixture read succeeds");
	Document document;std::vector<Diagnostic> diagnostics;const bool loaded=document.Load(bytes.str(),diagnostics);
	for(const auto& d:diagnostics)std::fprintf(stderr,"fixture %s: %s\n",d.pointer.c_str(),d.message.c_str());Check(loaded,"exact first-party numeric smoke fixture compiles canonically");
	std::vector<const Node*> nodes{&document.Model().root};bool number=false;while(!nodes.empty()){const auto* node=nodes.back();nodes.pop_back();number|=node->control&&node->control->role==ControlRole::Number;for(const auto& child:node->children)nodes.push_back(&child);}
	Check(number,"authored smoke fixture actually contains a Number control");
}
static void DraftFocusAfterModalProgram(){
	for(float density:{1.25f,2.f})for(bool hideField:{false,true}){
		auto source=Parse(Source());
		source["state"]["dialogOpen"]["type"]="boolean";source["state"]["dialogOpen"]["initial"]=false;
		source["state"]["fieldVisible"]["type"]="boolean";source["state"]["fieldVisible"]["initial"]=true;
		auto modal=Box("draftDialog",20,160,400,100);modal["modal"]["initialFocus"]="continue";
		auto button=Box("continue",10,10,160,40);auto& control=button["control"];
		control["role"]="button";control["event"]="close";control["label"]="#str_continue";
		for(const auto* state:{"default","hover","focus","pressed","disabled"})control["states"][state]="dialogFeedback";
		auto timeline=source["timelines"][0];timeline["id"]="dialogFeedback";timeline["tracks"][0]["node"]="continue";source["timelines"].append(timeline);
		modal["children"].append(button);source["root"]["children"].append(modal);
		for(const auto& [node,state]:std::vector<std::pair<const char*,const char*>>{{"draftDialog","dialogOpen"},{"number","fieldVisible"}}){
			Json::Value binding;binding["id"]=std::string(node)+"Display";binding["node"]=node;binding["property"]="display";
			binding["value"]["op"]="select";Json::Value ref;ref["state"]=state;
			binding["value"]["args"].append(ref);binding["value"]["args"].append("block");binding["value"]["args"].append("none");source["bindings"].append(binding);
		}
		Json::Value open;open["op"]="setState";open["values"]["dialogOpen"]=true;source["events"]["open"].append(open);
		auto close=open;close["values"]["dialogOpen"]=false;close["values"]["fieldVisible"]=!hideField;source["events"]["close"].append(close);
		TestHost host;Runtime runtime(host);Viewport viewport;viewport.displayScale=density;std::vector<Diagnostic> diagnostics;std::string error;
		const bool loaded=runtime.LoadDocument(Encode(source),"number-modal.q4ui",diagnostics);
		for(const auto& d:diagnostics)std::fprintf(stderr,"%s: %s\n",d.pointer.c_str(),d.message.c_str());Check(loaded,"numeric modal fixture loads");
		runtime.Frame(viewport,1);Check(runtime.FocusControl("number",1)&&runtime.BeginNumberEdit("number",error,1),"edit number before modal");
		auto edit=runtime.GetWidgetState("number")->number.value();
		Check(runtime.SetNumberSelection("number",edit.identity,0,edit.state.text.size(),error,1),"select draft before modal");
		Check(runtime.ReplaceNumberSelection("number",runtime.GetWidgetState("number")->number->identity,"1.375",error,1),"precise local modal draft");
		Runtime::EventEffects effects;Check(runtime.RunEvent("open",1,effects,error),"open authored modal through program");runtime.Frame(viewport,1.1);
		NumberDraftSummary drafts;Check(runtime.QueryNumberDrafts(drafts,error,1.1)&&drafts.blocking.size()==1,"modal preserves detached draft");
		Check(!runtime.FocusNumberDraft(drafts.barrier,"number",error,1.1),"visible modal still prevents field focus");
		Check(runtime.RunEvent("close",1.1,effects,error)&&runtime.QueryNumberDrafts(drafts,error,1.1),"close program immediately followed by draft query");
		const auto drawCount=host.draws.size();
		Check(runtime.FocusNumberDraft(drafts.barrier,"number",error,1.1)==!hideField,"same-dispatch focus uses fresh projected visibility");
		const auto after=runtime.GetWidgetState("number")->number.value();
		Check(after.state.text=="1.375"&&after.dirty&&after.active==!hideField&&after.canUndo,"draft text/history survive modal focus decision");
		Check(host.draws.size()==drawCount&&runtime.TakeActions().empty()&&host.errors==0,"layout synchronization neither renders nor submits a value");
		if(!hideField){runtime.Frame(viewport,1.2);Check(runtime.FocusedControl()=="number"&&runtime.GetNumberGeometry("number").has_value(),"resumed editor has current caret on next frame");}
	}
}
static void GrowingValidationRevealsField(){
	for(float density:{1.25f,2.f}){
		auto source=Parse(Source());auto number=source["root"]["children"][0];
		number["properties"]["height"]=Typed("keyword","auto");number["properties"]["top"]=Typed("length",90,"dp");
		auto& viewport=number["children"][0];viewport["properties"]["position"]=Typed("keyword","relative");viewport["properties"]["top"]=Typed("length",0,"dp");
		auto& validation=number["children"][1];validation["properties"]["position"]=Typed("keyword","relative");validation["properties"]["top"]=Typed("length",0,"dp");
		validation["properties"]["width"]=Typed("length",150,"dp");validation["properties"]["height"]=Typed("keyword","auto");
		auto scroller=Box("scroller",20,20,400,150);scroller["properties"]["overflow"]=Typed("keyword","auto");scroller["children"].append(number);
		source["root"]["children"].clear();source["root"]["children"].append(scroller);
		TestHost host;Runtime runtime(host);Viewport screen;screen.displayScale=density;std::vector<Diagnostic> diagnostics;std::string error;
		const bool loaded=runtime.LoadDocument(Encode(source),"number-validation-scroll.q4ui",diagnostics);
		for(const auto& d:diagnostics)std::fprintf(stderr,"%s: %s\n",d.pointer.c_str(),d.message.c_str());Check(loaded,"growing validation fixture loads");
		runtime.Frame(screen,1);Check(runtime.FocusControl("number",1)&&runtime.BeginNumberEdit("number",error,1),"focus number in scrolling body");runtime.Frame(screen,1.1);
		auto edit=runtime.GetWidgetState("number")->number.value();
		Check(runtime.SetNumberSelection("number",edit.identity,0,edit.state.text.size(),error,1.1),"select valid field text before invalid edit");
		Check(runtime.ReplaceNumberSelection("number",runtime.GetWidgetState("number")->number->identity,"-",error,1.1),"unfinished value remains a local draft");runtime.Frame(screen,1.2);
		auto* doc=Rml::GetContext(0)->GetDocument(0);auto* scroll=doc->GetElementById("scroller");
		Bounds body,message;Check(runtime.GetBounds("scroller",body)&&runtime.GetBounds("validation",message),"new validation has actual Rml bounds");
		Check(scroll->GetScrollTop()>0&&message.y+message.height<=body.y+body.height+.1f,"new validation is fully revealed in the same frame");
		Check(runtime.FocusedControl()=="number"&&runtime.GetWidgetState("number")->number->state.text=="-"&&runtime.TakeActions().empty(),"scroll correction preserves focus and local value");
		scroll->SetScrollTop(0);runtime.Frame(screen,1.3);
		Check(scroll->GetScrollTop()==0&&host.errors==0,"unchanged field does not override later deliberate scrolling");
	}
}
int main(int argc,char** argv){
	Check(argc<=2,"only optional authored fixture path accepted");if(argc==2)AuthoredFixture(argv[1]);
	for(const auto& [name,test]:std::vector<std::pair<const char*,void(*)()>>{{"fractional-density",FractionalDrawingAndDensity},{"framed-transform",FramedReversedSelectionAndTransforms},{"scroll-empty-preedit",ScrollEmptyAndPreedit},{"validation-proposals",ValidationProposalsAndIdentity},{"detached-draft",DetachedDraftAndNativeAffinity},{"unavailable-layout",UnavailableLayoutHidesStaleInk},{"snapshot-recreation",SnapshotDraftsAndResourceRecreation},{"snapshot-conflict",SnapshotFreshHostConflict},{"snapshot-pending",SnapshotPendingProposalDoesNotReplay},{"host-acknowledgement",HostAcknowledgementBetweenFrames},{"host-before-dispatch",HostChangeBeforeDispatch}}){currentCase=name;test();}
	currentCase="modal-draft-focus";DraftFocusAfterModalProgram();
	currentCase="validation-scroll";GrowingValidationRevealsField();
	std::printf("UiNumberRuntimeTest: %u checks passed (actual Runtime/Rml, no devices or GPU).\n",checks);return 0;
}
