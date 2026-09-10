// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "src/ui/retained/Runtime.h"
#include "src/ui/retained/NumberControlView.h"
#include "src/ui/retained/TextRun.h"
#include "src/ui/retained/VectorElement.h"
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
// The context/font/render services are real Runtime/RmlUi services. Native
// operations below are pure model offers; there is no OS editor or input pump.
static Json::Value Parse(const std::string& text) {
 Json::CharReaderBuilder builder;Json::Value out;std::string error;
 auto reader=std::unique_ptr<Json::CharReader>(builder.newCharReader());Check(reader->parse(text.data(),text.data()+text.size(),&out,&error),"fixture JSON parses");return out;
}
static Json::Value VectorPart(const char* id) {
 auto part=Box(id,0,0,100,100,"vector");part["properties"]["width"]=Typed("length",100,"%");part["properties"]["height"]=Typed("length",100,"%");
 Json::Value path;path["id"]="underline";
 for(unsigned i=0;i<4;++i) {Json::Value command;command["id"]="p"+std::to_string(i);command["op"]=i?"line":"move";Json::Value point(Json::arrayValue);
  for(double f:{i==1||i==2?1.:0.,i>=2?1.:0.}){Json::Value coordinate;coordinate["fraction"]=f;coordinate["dp"]=0;point.append(coordinate);}command["points"].append(point);path["commands"].append(command);}
 Json::Value close;close["id"]="close";close["op"]="close";path["commands"].append(close);path["fill"]["type"]="solid";path["fill"]["color"]=Typed("color",Array({0,1,0,1}));part["paths"].append(path);return part;
}
static std::string NativeSource(bool transform,bool framed) {
 auto json=Parse(Source(transform,framed));auto& viewport=json["root"]["children"][0]["children"][0];viewport["properties"]["width"]=Typed("length",160,"dp");
 auto& composition=viewport["children"][3];composition["properties"].removeMember("background-color");composition["properties"]["height"]=Typed("length",2,"dp");
 auto group=Box("composition-art-group",0,0,100,100);group["properties"]["width"]=Typed("length",100,"%");group["properties"]["height"]=Typed("length",100,"%");
 group["children"].append(VectorPart("composition-art"));composition["children"].append(group);
 // A later authored sibling witnesses clone insertion order.
 auto marker=Box("after-composition",0,0,1,1);viewport["children"].append(marker);
 Json::StreamWriterBuilder writer;writer["indentation"]="";return Json::writeString(writer,json);
}
struct NativeView {
 TestHost host;Runtime runtime{host};Document model;Interaction input;NumberControlView paint;
 Rml::Context* context=nullptr;Rml::ElementDocument* document=nullptr;
 std::string error,source;float ratio;double time=1;NativeTextIdentity nativeId{200,300};NativeTextDocument native;NativeTextEditorBarrier barrier;
 std::uint64_t dispatch=10,fence=20;
 NativeView(float density=1,bool transform=false,bool framed=false):ratio(density) {
  source=NativeSource(transform,framed);std::vector<Diagnostic> diagnostics;
  const bool loaded=model.Load(source,diagnostics)&&runtime.LoadDocument(source,"native-number.q4ui",diagnostics);
  for(const auto& d:diagnostics)std::fprintf(stderr,"%s: %s\n",d.pointer.c_str(),d.message.c_str());Check(loaded,"real authored vector Number fixture loads");
  Viewport viewport;viewport.width=1280;viewport.height=720;viewport.displayScale=ratio;runtime.Frame(viewport,time);
  context=Rml::GetContext(0);Check(context!=nullptr,"actual Rml context exists");document=context->GetDocument(0);Check(document!=nullptr,"actual Rml document exists");
  input.Reset(model.Model());Check(input.SetReadbacks({{"number",{1.05,false,{}}}},error),"authoritative typed readback");input.SetBounds({{"number",{0,0,1280,720,true}}});
  Check(input.Focus("number")&&input.BeginNumberEdit("number",error),"eligible pure Number editor begins");Initialize();Frame();
 }
 ~NativeView(){paint.Reset();}
 void Initialize(){Check(paint.Initialize(model.Model(),*document,[&](uintptr_t font,std::string_view text,float spacing){
   const auto size=static_cast<int>(std::lround(Rml::GetFontEngineInterface()->GetFontMetrics(font).ascent/.775f));
   return MeasureTextRun(text,spacing,[&](uint32_t cp){return host.GetGlyph("number-font",size,cp);},error);
  },[&](const std::string& key){return host.Translate(key);},[](const NumberSpec&,const NumberEditView&,bool){return "Invalid <value> & retry";},error),"initialize actual Number view");}
 Rml::Element* E(const char* id)const {auto* element=document->GetElementById(id);Check(element!=nullptr,"canonical element remains available");return element;}
 NumberEditView Edit()const {auto view=input.Widget("number");Check(view&&view->number,"Number edit view exists");return *view->number;}
 void Frame(){time+=.01;context->Update();for(int pass=0;pass<3;++pass){context->GetRootElement()->UpdateGeometryForProjection();if(!paint.Paint(input,ratio,time,true))break;context->Update();}
  context->GetRootElement()->UpdateGeometryForProjection();host.draws.clear();++host.frame;context->Render();Check(host.errors==0,"actual Rml rendering reports no errors");}
 void Settled(){context->GetRootElement()->UpdateGeometryForProjection();Check(!paint.Paint(input,ratio,time,true),"repeated Paint has no continuing geometry/style mutations");}
 void Attach(){const auto v=Edit();TextEditorIdentity owner{1,2,3,input.ModalToken(),5,v.identity.session,v.identity.revision,"number"};
  Check(input.AttachNumberNative("number",v.identity,owner,nativeId,barrier,error),"attach exact native owner");Check(native.Open(nativeId,owner.revision,v.state.text,v.state.anchor,v.state.caret,error),"open real pure native shadow");}
 void Change(const std::function<void(const NativeTextLockScope&)>& op){
  NativeTextLockScope lock;Check(native.RequestLock(nativeId,NativeTextAccess::ReadWrite,true,lock,error)==NativeTextLockResult::Granted,"real native write scope");op(lock);
  uint64_t sequence=0;Check(native.FinishLock(lock,++dispatch,sequence,error)&&sequence,"publish native transaction");NativeTextOffer offer;Check(native.PeekOffer(nativeId,offer,error),"real native FIFO offer");
  NativeTextEditorBarrier opened;Check(input.BeginNumberNativeCollection(barrier,{dispatch,++fence,sequence},opened,error),"checked collection starts");barrier=opened;
  NativeTextEditorReceipt receipt;Check(input.ApplyNumberNative(barrier,offer,receipt,error),"apply exact native offer to Interaction");
  Check(native.Acknowledge(nativeId,receipt.after.sequence,receipt.after.shadowRevision,receipt.before.editor.revision,receipt.after.editor.revision,error),"matching pure native acknowledgement");barrier=receipt.after;
  Check(input.CompleteNumberNativeCollection(barrier,barrier.collection,opened,error),"checked collection finishes");barrier=opened;
 }
 void Retire(){NativeTextEditorReceipt receipt;Check(input.RetireNumberNative(barrier,receipt,error),"explicit native retirement");Check(native.Retire(nativeId,error),"retire native shadow");barrier=receipt.after;}
 std::shared_ptr<const TextRun> Run()const {auto* text=E("text");auto* ink=text->GetNumChildren()?text->GetChild(0):text;const auto size=static_cast<int>(std::lround(Rml::GetFontEngineInterface()->GetFontMetrics(ink->GetFontFaceHandle()).ascent/.775f));std::string diagnostic;
  return MeasureTextRun(Edit().nativePresentation?Edit().nativePresentation->text:Edit().state.text,ink->GetComputedValues().letter_spacing(),[&](uint32_t cp){Glyph g;g.advance=TestHost::Advance(size,cp);return g;},diagnostic);}
 Rml::Vector2f Baseline()const {auto* text=static_cast<Rml::ElementText*>(E("text")->GetChild(0));return text->GetAbsoluteOffset(Rml::BoxArea::Content).Round()+text->GetLines()[0].position;}
 std::vector<Rml::Element*> Parts()const {std::vector<Rml::Element*> result{E("composition")};for(auto* next=result.back()->GetNextSibling();next&&next!=E("after-composition");next=next->GetNextSibling())result.push_back(next);return result;}
 unsigned GreenDraws()const {unsigned result=0;for(const auto& draw:host.draws){bool green=false;for(const auto& vertex:draw.vertices)green|=vertex.g>.9f&&vertex.r<.01f&&vertex.b<.01f&&vertex.a>.9f;if(green)++result;}return result;}
 void NoDuplicateIds()const {std::set<std::string> ids;std::vector<Rml::Element*> pending{document};while(!pending.empty()){auto* element=pending.back();pending.pop_back();if(!element->GetId().empty())Check(ids.insert(element->GetId()).second,"no duplicated canonical or descendant IDs");for(int i=0;i<element->GetNumChildren();++i)pending.push_back(element->GetChild(i));}}
 void CheckRange(Rml::Element* part,size_t first,size_t last){const auto run=Run();float x=0,y=0;Check(run&&run->CaretPosition(first,x)&&run->CaretPosition(last,y),"independent fractional scalar range positions");const auto p=part->GetAbsoluteOffset(Rml::BoxArea::Border),size=part->GetBox().GetSize(Rml::BoxArea::Border);
  Check(Near(p.x,Baseline().x+x,.011f)&&Near(size.x,y-x,.011f),"each authored underline follows exact current native UTF8 range and fractional run");Check(part->GetParentNode()==E("viewport"),"every composition is in canonical clipped viewport");}
};
static void ConcurrentRangesAndDirectionalSelection(){
 for(float ratio:{1.f,1.25f,2.f})for(bool transformed:{false,true}){
  NativeView view(ratio,transformed,true);view.Attach();const auto stable=view.Edit().state;
  view.Change([&](auto lock){Check(view.native.ReplaceACP(lock,0,4,"1\xC3\xA9\xF0\x9F\x99\x82" "45",view.error),"full multibyte native text");Check(view.native.BeginComposition(lock,1,0,4,view.error),"first native range");Check(view.native.BeginComposition(lock,2,2,6,view.error),"overlapping concurrent native range");Check(view.native.BeginComposition(lock,3,6,6,view.error),"empty native range");Check(view.native.SelectACP(lock,6,1,view.error),"reversed native selection");});
  Check(!view.paint.Geometry("number",view.input),"new native bytes/revision reject stale caret before paint");view.Frame();view.Settled();
  const auto edit=view.Edit();Check(edit.state.text==stable.text&&edit.nativePresentation->text=="1\xC3\xA9\xF0\x9F\x99\x82" "45"&&edit.nativePresentation->anchor==9&&edit.nativePresentation->caret==1,"stable draft is distinct from complete byte-indexed native presentation");
  auto* text=static_cast<Rml::ElementText*>(view.E("text")->GetChild(0));Check(text->GetText()==edit.nativePresentation->text,"Rml glyph string equals complete native presentation");
  const auto parts=view.Parts();Check(parts.size()==2,"two nonempty ranges produce exactly two authored artwork roots");view.CheckRange(parts[0],0,7);view.CheckRange(parts[1],3,9);view.NoDuplicateIds();
  Check(parts[1]->GetId().empty()&&parts[1]->GetChild(0)->GetId().empty()&&parts[1]->GetChild(0)->GetChild(0)->GetId().empty(),"derived artwork has no root or descendant identifiers");
  Check(view.GreenDraws()==2,"both concurrent vector subtrees submit actual geometry");const auto run=view.Run();float caret=0,anchor=0;Check(run->CaretPosition(1,caret)&&run->CaretPosition(9,anchor),"native directional scalar carets");
  const auto cp=view.E("caret")->GetAbsoluteOffset(Rml::BoxArea::Border);const auto sp=view.E("selection")->GetAbsoluteOffset(Rml::BoxArea::Border),ss=view.E("selection")->GetBox().GetSize(Rml::BoxArea::Border);
  Check(Near(cp.x,view.Baseline().x+caret,.011f)&&Near(sp.x,cp.x,.011f)&&Near(ss.x,anchor-caret,.011f),"reversed native selection places caret at directional endpoint");
  Check(view.paint.Geometry("number",view.input).has_value(),"settled transformed native caret geometry available");
  Check(!view.paint.CommandRun("number",edit.identity,view.input,view.error),"ordinary command rejects active native-owned field");
  view.E("composition-art")->SetProperty("opacity","0.4");view.Frame();view.Settled();Check(Near(parts[1]->GetChild(0)->GetChild(0)->GetComputedValues().opacity(),.4f),"derived vector styling follows authored timeline change");
  view.E("composition-art")->RemoveProperty("opacity");view.Frame();view.Settled();Check(!parts[1]->GetChild(0)->GetChild(0)->GetLocalProperty("opacity"),"removed authored style does not linger on replica");
  view.Retire();view.Frame();view.Settled();Check(view.GreenDraws()==0&&!parts[0]->IsVisible()&&!parts[1]->IsVisible(),"native retirement hides every stale composition subtree");Check(view.Edit().state.text==stable.text&&!view.Edit().nativePresentation,"retirement restores stable local draft");
  Check(view.paint.CommandRun("number",view.Edit().identity,view.input,view.error)!=nullptr,"explicitly retired editor permits ordinary command measurement");
 }
}
static void PoolClippingAndLifetime(){
 NativeView view;view.Attach();std::string text(80,'1');
 view.Change([&](auto lock){Check(view.native.ReplaceACP(lock,0,4,text,view.error),"long native text");for(unsigned i=1;i<=32;++i)Check(view.native.BeginComposition(lock,i,(i-1)*2,(i-1)*2+1,view.error),"bounded concurrent range");Check(view.native.SelectACP(lock,0,64,view.error),"caret drives native presentation scroll");});
 view.Frame();view.Settled();const auto parts=view.Parts();Check(parts.size()==32,"all 32 composition ranges have bounded authored replicas");view.NoDuplicateIds();auto geometry=view.paint.Geometry("number",view.input);Check(geometry&&geometry->scroll>0,"scroll follows native caret rather than stable buffer");
 auto* viewport=view.E("viewport");const auto origin=viewport->GetAbsoluteOffset(Rml::BoxArea::Content),size=viewport->GetBox().GetSize(Rml::BoxArea::Content);unsigned green=0;
 for(const auto& draw:view.host.draws)for(const auto& vertex:draw.vertices)if(vertex.g>.1f&&vertex.r<.01f&&vertex.b<.01f){++green;Check(vertex.x>=std::floor(origin.x)-.01f&&vertex.x<=std::ceil(origin.x+size.x)+.01f&&vertex.y>=std::floor(origin.y)-.01f&&vertex.y<=std::ceil(origin.y+size.y)+.01f,"actual vector submissions are clipped by canonical field viewport");}Check(green>0&&view.GreenDraws()<32,"clipping retains visible ranges and culls offscreen ranges");
 const auto revision=view.Edit().identity;view.Change([&](auto lock){for(unsigned i=2;i<=32;++i)Check(view.native.EndComposition(lock,i,view.error),"retire surplus range");Check(view.native.UpdateComposition(lock,1,0,1,view.error),"preserve first range");Check(view.native.SelectACP(lock,0,0,view.error),"scroll returns to first native character");});
 view.Frame();view.Settled();Check(view.Parts().size()==32&&view.GreenDraws()==1,"pool reuses bounded members while hiding retired ranges");for(size_t i=1;i<parts.size();++i)Check(!parts[i]->IsVisible(),"unused derived part hidden");
 Check(!view.paint.CommandRun("number",revision,view.input,view.error),"earlier command token remains stale");
 auto saved=view.input.CaptureWidgets();view.input.Cancel();view.Frame();Check(!view.Edit().active&&!view.Edit().nativePresentation&&view.GreenDraws()==0&&!view.paint.Geometry("number",view.input),"focus quarantine hides all native ink and geometry");
 Check(view.input.RestoreWidgets(saved,view.error),"durable local restore succeeds");view.Frame();Check(!view.Edit().active&&!view.Edit().nativePresentation&&view.GreenDraws()==0,"restore never resurrects native ranges or input authority");
 view.paint.Reset();Check(view.E("viewport")->GetNumChildren()==5,"reset removes derived roots from live document");view.Initialize();view.Frame();Check(view.E("viewport")->GetNumChildren()==5,"resource-style reinitialization does not duplicate artwork");
 // Expired observers also protect destruction when the caller has already
 // released the document; the context remains alive until Runtime shutdown.
 Check(view.input.Focus("number")&&view.input.BeginNumberEdit("number",view.error),"resume local buffer after restore");view.Frame();
 view.document->Close();view.context->Update();view.document=nullptr;view.paint.Reset();Check(true,"reset safely tolerates already destroyed DOM");
}
static void EmptyRangeAndSettledNative(){
 NativeView view;view.Attach();
 view.Change([&](auto lock){Check(view.native.ReplaceACP(lock,0,4,"1e-",view.error),"native invalid intermediate stays text");Check(view.native.BeginComposition(lock,1,3,3,view.error),"empty native composition");});
 view.Frame();Check(view.Edit().nativeUnsettled&&view.GreenDraws()==0&&view.paint.Geometry("number",view.input),"zero extent composition keeps caret and ownership without false underline");
 view.Change([&](auto lock){Check(view.native.EndComposition(lock,1,view.error),"end empty native composition");});NativeTextEditorReceipt receipt;Check(view.input.SettleNumberNative(view.barrier,receipt,view.error),"explicit checked native settlement");view.barrier=receipt.after;view.Frame();
 Check(!view.Edit().nativeUnsettled&&view.Edit().nativePresentation&&view.Edit().state.text=="1e-","settled native snapshot remains attached to invalid local draft");Check(!view.paint.CommandRun("number",view.Edit().identity,view.input,view.error),"settled attached native field still rejects ordinary command path");
 Check(view.E("validation")->GetInnerRML().find("&lt;value&gt;")!=std::string::npos,"localized invalid text remains escaped");
}

static Rml::Vector2f ProjectForward(Rml::Element* element,Rml::Vector2f local){
 Rml::Vector2f o(0,0),x(1,0),y(0,1);Check(element->Project(o)&&element->Project(x)&&element->Project(y),"invert actual transformed viewport");x-=o;y-=o;const double det=double(x.x)*y.y-double(y.x)*x.y;Check(std::abs(det)>1e-8,"finite invertible common transform");const auto d=local-o;
 return {static_cast<float>((double(d.x)*y.y-double(y.x)*d.y)/det),static_cast<float>((double(x.x)*d.y-double(d.x)*x.y)/det)};
}
static void NativeHitAndMetadata(){
 NativeView view(1.25f,true,true);view.Attach();view.Frame();auto before=view.Edit();Check(view.paint.Geometry("number",view.input).has_value(),"attached settled presentation has current caret");
 NativeTextEditorBarrier opened;Check(view.input.BeginNumberNativeCollection(view.barrier,{10,20,0},opened,view.error),"empty collection opens without content change");view.barrier=opened;
 Check(view.Edit().identity==before.identity&&view.Edit().nativeUnsettled,"collection changes protocol while keeping text identity");Check(!view.paint.Geometry("number",view.input),"native-unsettled metadata invalidates pre-collection geometry");view.Frame();view.Settled();Check(view.paint.Geometry("number",view.input).has_value(),"fresh unchanged-byte native geometry settles");
 Check(view.input.CompleteNumberNativeCollection(view.barrier,view.barrier.collection,opened,view.error),"empty native collection completes");view.barrier=opened;
 view.Change([&](auto lock){Check(view.native.ReplaceACP(lock,0,4,"1\xC3\xA9\xF0\x9F\x99\x82" "45",view.error),"native hit string");Check(view.native.BeginComposition(lock,1,0,6,view.error),"native hit range");Check(view.native.BeginComposition(lock,2,0,1,view.error),"second native hit range");Check(view.native.SelectACP(lock,1,1,view.error),"native hit caret");});view.Frame();
 auto run=view.Run();float x=0;Check(run->CaretPosition(7,x),"hit targets UTF8 scalar after supplementary character");auto point=ProjectForward(view.E("viewport"),view.Baseline()+Rml::Vector2f(x,-5));
 const auto hit=view.paint.Hit(view.E("text"),point.x,point.y,view.input);Check(hit&&hit->byteOffset==7&&hit->identity==view.Edit().identity,"transformed hit indexes exact full native bytes");
 Check(!view.input.SetNumberSelection("number",view.Edit().identity,7,7,view.error),"ordinary hit selection cannot mutate an attached native editor");
 auto* viewport=view.E("viewport");const auto origin=viewport->GetAbsoluteOffset(Rml::BoxArea::Content);auto outside=ProjectForward(viewport,{origin.x-30,origin.y+10});Check(!view.paint.Hit(view.E("text"),outside.x,outside.y,view.input),"ordinary native hit outside viewport is rejected");
 auto* caret=view.E("caret");view.E("viewport")->SetProperty("width","0px");view.Frame();Check(!caret->IsVisible()&&view.GreenDraws()==0&&!view.paint.Geometry("number",view.input),"zero layout hides every composition and stale caret");for(auto* part:view.Parts())Check(!part->IsVisible(),"failed layout hides each derived composition root");view.E("viewport")->SetProperty("width","160dp");view.Frame();view.Settled();Check(view.paint.Geometry("number",view.input).has_value(),"viewport recovery repaints fresh native geometry");view.input.Cancel();view.Frame();Check(view.GreenDraws()==0,"focus loss removes all simultaneous composition draws");for(auto* part:view.Parts())Check(!part->IsVisible(),"focus loss hides each live derived root");
}
static void CopiedGeometryAndDestroyedDocument(){
 RuntimeStatistics statistics;NativeView view;view.Attach();
 view.Change([&](auto lock){Check(view.native.BeginComposition(lock,1,0,2,view.error),"first cache range");Check(view.native.BeginComposition(lock,2,2,4,view.error),"second cache range");Check(view.native.SelectACP(lock,0,0,view.error),"remove selection ink");});view.Frame();Check(view.GreenDraws()==2,"both original and copied vector geometry have compiled");
 auto* original=static_cast<VectorElement*>(view.E("composition-art"));auto* copy=static_cast<VectorElement*>(view.Parts()[1]->GetChild(0)->GetChild(0));Node changed=*view.model.Model().FindNode("composition-art");changed.paths[0].fill.colour={1,1,0,1};
 original->Configure(changed,view.host,statistics);copy->CopyArtworkFrom(*original);view.host.draws.clear();++view.host.frame;view.context->Render();Check(view.GreenDraws()==0&&view.host.Colour(1,1,0).has_value(),"copying artwork invalidates previously compiled path and uploaded mesh");Check(statistics.vectorPathsCompiled==2,"original and copied path both rebuild after artwork replacement");
 view.NoDuplicateIds();Check(view.Parts().size()==2,"live derived observer exists before document destruction");view.document->Close();view.context->Update();view.document=nullptr;view.paint.Reset();Check(true,"derived observer reset tolerates document-first destruction");
}
int main(){ConcurrentRangesAndDirectionalSelection();PoolClippingAndLifetime();EmptyRangeAndSettledNative();NativeHitAndMetadata();CopiedGeometryAndDestroyedDocument();std::printf("UiNumberNativeViewTest: %u checks passed\n",checks);}
