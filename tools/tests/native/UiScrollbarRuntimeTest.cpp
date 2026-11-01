// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "src/ui/retained/Runtime.h"
#include "src/ui/retained/State.h"
#include "src/ui/retained/ScrollGeometry.h"
#include <RmlUi/Core.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/ElementUtilities.h>
#include <json/json.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <sstream>
#include <tuple>
using namespace openq4::ui;
static unsigned checks=0;
static void Check(bool value,const char* why){++checks;if(!value){std::fprintf(stderr,"FAIL %s\n",why);std::exit(1);}}
static bool Near(double a,double b,double tolerance=1.01){return std::abs(a-b)<=tolerance;}
static Json::Value Typed(const char* type,Json::Value value,const char* unit=nullptr){Json::Value v;v["type"]=type;v["value"]=std::move(value);if(unit)v["unit"]=unit;return v;}
static Json::Value Box(const char* id,double x,double y,double w,double h){Json::Value n;n["id"]=id;n["type"]="group";n["children"]=Json::Value(Json::arrayValue);auto& p=n["properties"];
 p["position"]=Typed("keyword","absolute");p["display"]=Typed("keyword","block");p["opacity"]=Typed("number",1);p["box-sizing"]=Typed("keyword","border-box");
 p["left"]=Typed("length",x,"dp");p["top"]=Typed("length",y,"dp");p["width"]=Typed("length",w,"dp");p["height"]=Typed("length",h,"dp");return n;}
static void Feedback(Json::Value& doc,Json::Value& node){const auto id=node["id"].asString();for(auto* state:{"default","hover","focus","pressed","disabled"})node["control"]["states"][state]=id+"-feedback";
 Json::Value timeline,track;timeline["id"]=id+"-feedback";timeline["durationMs"]=1;track["node"]=id;track["property"]="opacity";for(unsigned time:{0u,1u}){Json::Value k;k["atMs"]=time;k["value"]=Typed("number",1);track["keys"].append(k);}timeline["tracks"].append(track);doc["timelines"].append(timeline);}
static Json::Value Source(bool horizontal=false,bool transform=false,bool framed=false){Json::Value d;d["format"]="openq4-ui";d["version"]=1;d["id"]="scroll-runtime";d["actions"]["button"]["operation"]="test.button";d["actions"]["button"]["arguments"]=Json::Value(Json::objectValue);
 auto root=Box("root",0,0,640,360),frame=Box("frame",90,60,380,245),viewport=Box("viewport",0,0,280,180),content=Box("content",0,0,horizontal?1000:280,horizontal?180:1000);
 viewport["properties"]["overflow"]=Typed("keyword","auto");
 if(framed){viewport["properties"]["border-width"]=Typed("length",2,"dp");viewport["properties"]["padding-top"]=Typed("length",6,"dp");viewport["properties"]["padding-left"]=Typed("length",5,"dp");}
 for(auto item:{std::pair<const char*,double>{"first",0},{"last",850}}){auto button=Box(item.first,horizontal?item.second:8,horizontal?8:item.second,80,32);button["control"]["role"]="button";button["control"]["action"]="button";button["control"]["label"]="#str_test";Feedback(d,button);content["children"].append(button);}
 viewport["children"].append(content);frame["children"].append(viewport);
 auto bar=Box("bar",horizontal?0:300,horizontal?195:0,horizontal?280:36,horizontal?36:180);auto& c=bar["control"];c["role"]="scrollbar";c["label"]="#str_test";c["viewport"]="viewport";c["orientation"]=horizontal?"horizontal":"vertical";c["lineStep"]=36;c["minimumThumb"]=36;c["parts"]["track"]="track";c["parts"]["thumb"]="thumb";Feedback(d,bar);
 auto track=Box("track",0,0,horizontal?280:36,horizontal?36:180),thumb=Box("thumb",0,0,36,36);
 if(framed){track["properties"]["padding-top"]=Typed("length",3,"dp");track["properties"]["padding-left"]=Typed("length",2,"dp");track["properties"]["border-width"]=Typed("length",1,"dp");thumb["properties"]["margin-top"]=Typed("length",2,"dp");thumb["properties"]["margin-left"]=Typed("length",1,"dp");}
 Json::Value colour(Json::arrayValue);for(double value:{1.,.3,.1,1.})colour.append(value);thumb["properties"]["background-color"]=Typed("color",colour);track["children"].append(thumb);bar["children"].append(track);frame["children"].append(bar);
 auto modal=Box("modal",0,0,100,100);frame["children"].append(modal);
 if(transform){Json::Value t(Json::arrayValue);for(double v:{12.,8.,1.1,.85,13.})t.append(v);frame["properties"]["transform"]=Typed("transform",t,"dp");}
 root["children"].append(frame);d["root"]=root;return d;}
static std::string Encode(const Json::Value& d){Json::StreamWriterBuilder w;w["indentation"]="";w["precision"]=17;return Json::writeString(w,d);}
static Json::Value& Frame(Json::Value& d){return d["root"]["children"][0];}
static Json::Value& Bar(Json::Value& d){return Frame(d)["children"][1];}
struct HostFixture final:Host {unsigned errors=0,draws=0;std::uint64_t frame=0;
 bool ReadFile(const std::string&,std::string&)override{return false;}bool softFocus=false;bool ReadCVar(const std::string& name,size_t type,StateValue& value)override{if(name=="sys_lang"&&type==2){value=std::string("english");return true;}if(name!="ui_retainedSoftFocus"||type!=1)return false;value=softFocus;return true;}unsigned softened=0;float sigma=0,saturation=1;bool SoftenBackdrop(std::uint32_t destination,float s,float sat,const Bounds&)override{Check(destination!=0,"SYSTEM backdrop into a pushed layer");++softened;sigma=s;saturation=sat;return softFocus;}
 std::string Translate(const std::string& value)override{return value.starts_with("#str_")?"Localized":value;}
 void Log(bool bad,const std::string& message)override{if(bad){++errors;std::fprintf(stderr,"Runtime: %s\n",message.c_str());}}
 std::uintptr_t LoadMaterial(const std::string&,int& w,int& h)override{w=h=64;return 1;}
 void Draw(const std::vector<Vertex>& v,const std::vector<int>& i,std::uintptr_t)override{++draws;bool finite=i.size()%3==0;for(auto n:i)finite&=n>=0&&size_t(n)<v.size();for(auto p:v)finite&=std::isfinite(p.x)&&std::isfinite(p.y);Check(finite,"actual drawn geometry finite");}
 FontMetrics GetFontMetrics(const std::string&,int s)override{return {s*.8f,s*.2f,s*1.4f,s*.5f};}Glyph GetGlyph(const std::string&,int s,std::uint32_t)override{return {s*.5f,0,-s*.8f,s*.5f,float(s),0,0,1,1,"test-font"};}
 std::uint64_t RenderFrame()const override{return frame;}bool BeginLayer(std::uint32_t,int,int)override{return true;}void CompositeLayer(std::uint32_t,std::uint32_t,float,const Bounds&)override{}void MaskLayer(std::uint32_t,std::uint32_t,const Bounds&)override{}void EndLayer(std::uint32_t)override{}
};
struct View {HostFixture host;Runtime runtime{host};Viewport viewport;double time=1;std::string error,source;bool horizontal;
 View(float ratio=1,bool horizontal=false,bool transform=false,bool framed=false):horizontal(horizontal){viewport.width=1280;viewport.height=720;viewport.displayScale=ratio;source=Encode(Source(horizontal,transform,framed));std::vector<Diagnostic> ds;bool ok=runtime.LoadDocument(source,"scroll.q4ui",ds);for(auto& d:ds)std::fprintf(stderr,"%s %s\n",d.pointer.c_str(),d.message.c_str());Check(ok,"scrollbar source loads");runtime.SetReducedMotion(true,time);Frame();}
 void Frame(){time+=.02;++host.frame;runtime.Frame(viewport,time);Check(!host.errors,"runtime no diagnostics");}
 Rml::Element* Element(const char* id){auto* c=Rml::GetContext(0);Check(c&&c->GetDocument(0),"actual Rml document");auto* e=c->GetDocument(0)->GetElementById(id);Check(e!=nullptr,id);return e;}
 double Offset(){auto* e=Element("viewport");return horizontal?e->GetScrollLeft():e->GetScrollTop();}
 ScrollReadback Read(){auto v=runtime.GetWidgetState("bar");Check(v&&v->scroll,"actual scrollbar readback");return *v->scroll;}
 void Key(MenuInput k){runtime.MenuAction(k,true,time);runtime.MenuAction(k,false,time);}
 Rml::Vector2f Point(double fraction){auto* e=Element("track");Rml::Array<Rml::Vector2f,4> q;Check(Rml::ElementUtilities::GetBorderBoxQuad(q,e),"projected track quad");const auto border=e->GetBox().GetSize(Rml::BoxArea::Border),content=e->GetBox().GetSize(Rml::BoxArea::Content);const auto delta=e->GetAbsoluteOffset(Rml::BoxArea::Content)-e->GetAbsoluteOffset(Rml::BoxArea::Border);
  const float x=float((delta.x+(horizontal?fraction:.5)*content.x)/border.x),y=float((delta.y+(horizontal?.5:fraction)*content.y)/border.y);return q[0]+(q[1]-q[0])*x+(q[3]-q[0])*y;}
 void Move(double fraction){const auto p=Point(fraction);runtime.PointerMove(p.x,p.y,time);}
 void Geometry(){auto r=Read();auto* track=Element("track");auto* thumb=Element("thumb");const auto size=thumb->GetBox().GetSize(Rml::BoxArea::Border);Check(Near(horizontal?size.x:size.y,r.geometry.thumb,.01),"authored thumb represents measured range");const auto start=thumb->GetAbsoluteOffset(Rml::BoxArea::Border)-track->GetAbsoluteOffset(Rml::BoxArea::Content);Check(Near(horizontal?start.x:start.y,r.geometry.position,.01),"thumb position equals actual viewport readback");}
 void NoActions(){Check(runtime.TakeActions().empty(),"scrollbar never emits application actions");}
};
static void Schema(){for(bool horizontal:{false,true}){auto source=Source(horizontal);Document d;std::vector<Diagnostic> ds;const bool ok=d.Load(Encode(source),ds);for(auto& diagnostic:ds)std::fprintf(stderr,"%s %s\n",diagnostic.pointer.c_str(),diagnostic.message.c_str());Check(ok,"canonical scrollbar schema accepts");State state;std::string error;Check(state.Reset(d.Model(),error)&&state.ControlValues().empty(),"scrollbar has no application value readback");
 for(int fault=0;fault<15;++fault){auto bad=source;auto& bar=Bar(bad);auto& c=bar["control"];switch(fault){case 0:c["action"]="button";break;case 1:c["event"]="back";break;case 2:c["value"]=0;break;case 3:c["viewport"]="root";break;case 4:c["viewport"]="first";break;case 5:c["viewport"]="bar";break;case 6:c["parts"]["thumb"]="track";break;case 7:c["lineStep"]=0;break;case 8:c["minimumThumb"]=-1;break;case 9:bar["children"][0]["children"][0]["properties"]["position"]=Typed("keyword","relative");break;
 case 10:{auto& p=bar["children"][0]["children"][0]["properties"];p[horizontal?"min-width":"min-height"]=Typed("length",90,"dp");break;}
 case 11:Frame(bad)["children"][0]["modal"]["initialFocus"]="first";break;
 case 12:Frame(bad)["children"][0]["properties"]["overflow"]=Typed("keyword","visible");break;
 case 13:{Json::Value binding;binding["id"]="collision";binding["node"]="thumb";binding["property"]=horizontal?"width":"height";binding["value"]=15;bad["bindings"].append(binding);break;}
 case 14:{auto duplicate=bar;duplicate["id"]="duplicate";duplicate["control"]["parts"]["track"]="duplicate-track";duplicate["control"]["parts"]["thumb"]="duplicate-thumb";duplicate["children"][0]["id"]="duplicate-track";duplicate["children"][0]["children"][0]["id"]="duplicate-thumb";Feedback(bad,duplicate);Frame(bad)["children"].append(duplicate);break;}}
 Check(!d.Load(Encode(bad),ds),"invalid scrollbar schema rejected");}}
}
static void Gestures(){for(float ratio:{1.f,1.25f,2.f})for(bool horizontal:{false,true})for(bool framed:{false,true}){
 View v(ratio,horizontal,true,framed);v.Geometry();Check(v.runtime.FocusControl("first",v.time),"content can focus");const auto initial=v.Offset();auto g=v.Read().geometry;
 v.Move(.9);v.runtime.PointerButton(true,v.time);v.runtime.PointerButton(false,v.time);v.Frame();Check(v.runtime.FocusedControl()=="first","track paging preserves content focus");double expected=0;Check(ScrollStepOffset(g,ScrollStep::PageForward,36*ratio,expected)&&Near(v.Offset(),expected),"track pages by actual viewport with overlap");Check(v.Offset()>initial,"track page moves viewport");v.Geometry();v.NoActions();
 const auto beforeWheel=v.Offset();v.Move(.9);v.runtime.PointerWheel(1,v.time);v.Frame();Check(Near(v.Offset(),beforeWheel+36*ratio),"authored wheel line step");Check(v.runtime.FocusedControl()=="first","wheel preserves content focus");v.NoActions();
 Check(v.runtime.FocusControl("bar",v.time),"explicit navigation focuses scrollbar");v.Key(MenuInput::Home);v.Frame();Check(v.Offset()==0,"Home reaches actual start");v.Key(horizontal?MenuInput::Right:MenuInput::Down);v.Frame();Check(Near(v.Offset(),36*ratio),"axis key line step");v.Key(MenuInput::End);v.Frame();Check(Near(v.Offset(),v.Read().geometry.range),"End reaches exact real range");v.Key(MenuInput::Home);v.Frame();
 g=v.Read().geometry;const double grab=.2;v.Move((g.position+g.thumb*grab)/g.track);v.runtime.PointerButton(true,v.time);v.Frame();Check(v.Offset()==0,"thumb press does not jump to center");
 v.Move(.6);v.Frame();g=v.Read().geometry;Check(ScrollDragOffset(g,g.track*.6,grab,expected)&&Near(v.Offset(),expected),"drag preserves original thumb grab fraction");v.Geometry();
 // Current measured resize changes travel and thumb while the same grab survives.
 auto* track=v.Element("track");Check(track->SetProperty(horizontal?"width":"height",horizontal?"220dp":"140dp"),"resize track");Check(v.Element("content")->SetProperty(horizontal?"width":"height","1500dp"),"resize content while captured");v.Frame();v.Move(.72);v.Frame();g=v.Read().geometry;Check(ScrollDragOffset(g,g.track*.72,grab,expected)&&Near(v.Offset(),expected),"captured resize uses current geometry and original grab");
 v.Move(1.4);v.runtime.PointerButton(false,v.time);v.Frame();Check(Near(v.Offset(),v.Read().geometry.range),"outside release retains final clamped drag");v.NoActions();
 v.Key(MenuInput::Home);v.Frame();g=v.Read().geometry;v.Move(g.thumb*.5/g.track);v.runtime.PointerButton(true,v.time);v.Move(.5);v.Key(MenuInput::Back);const auto canceled=v.Offset();v.Move(.8);v.runtime.PointerButton(false,v.time);v.Frame();Check(v.Offset()==canceled,"Back cancels capture without later release motion");v.NoActions();
 }}
static void DensityKeepsContentFocus(){View v;Check(v.runtime.FocusControl("first",v.time),"focus content before wheel scroll");for(unsigned i=0;i<12;++i){v.Move(.9);v.runtime.PointerWheel(1,v.time);}v.Frame();const double offset=v.Offset();Check(offset>300&&v.runtime.FocusedControl()=="first","wheel leaves earlier focus and scrolls independently");v.viewport.displayScale=2;v.Frame();Check(Near(v.Offset(),offset*2)&&v.runtime.FocusedControl()=="first","density must not reveal unchanged earlier focus");v.viewport.width=1100;v.viewport.height=680;v.Frame();Check(Near(v.Offset(),offset*2),"resize clamps/reflows without revealing unchanged focus");Check(v.runtime.FocusControl("first",v.time),"explicit same focus request");v.Frame();Check(v.Offset()==0,"explicit new focus request reveals earlier content");}
static void MixedViewportReveal(){for(int relation=0;relation<4;++relation)for(float ratio:{1.25f,2.f}){
 View v;auto source=Source();const char* target="mixed-target";const char* legacy="legacy";
 auto button=Box(target,350,400,70,32);button["control"]["role"]="button";button["control"]["action"]="button";button["control"]["label"]="#str_test";if(relation<2)Feedback(source,button);
 auto pane=Box(legacy,relation==0?400:5,relation==0?0:400,200,120),content=Box("legacy-content",0,0,900,900);pane["properties"]["overflow"]=Typed("keyword","auto");content["children"].append(button);pane["children"].append(content);
 if(relation==0)source["root"]["children"].append(pane);
 else if(relation==1)Frame(source)["children"][0]["children"][0]["children"].append(pane);
 else if(relation==2){auto frame=Frame(source);frame["properties"]["left"]=Typed("length",350,"dp");frame["properties"]["top"]=Typed("length",350,"dp");
  pane=Box(legacy,10,10,400,250);pane["properties"]["overflow"]=Typed("keyword","auto");pane["children"].append(frame);source["root"]["children"][0]=pane;target="last";
 }else{Frame(source)["children"][0]["children"][0]["properties"]["width"]=Typed("length",1000,"dp");Frame(source)["children"][0]["children"][0]["children"][1]["properties"]["left"]=Typed("length",850,"dp");target="last";legacy="viewport";}
 v.source=Encode(source);v.runtime.CloseDocument();std::vector<Diagnostic> ds;Check(v.runtime.LoadDocument(v.source,"mixed-scroll.q4ui",ds),"mixed authored and legacy scrollers load");v.Frame();Check(v.runtime.FocusControl(target,v.time),"mixed control focus");v.Frame();auto* owned=v.Element("viewport");auto* old=v.Element(legacy);
 owned->SetScrollTop(300);old->SetScrollLeft(0);if(relation!=3)old->SetScrollTop(0);v.Frame();Check(v.Offset()==300,"ordinary frame leaves authored and legacy manual offsets");
 v.viewport.displayScale=ratio;v.Frame();Check(Near(v.Offset(),300*ratio),"density preserves only authored vertical offset");Check(old->GetScrollLeft()>0&&(relation==3||old->GetScrollTop()>0),"mixed legacy ancestors and perpendicular axis still reveal after density");Check(v.runtime.FocusedControl()==target,"mixed resize preserves focus identity");
 std::string snapshot;Check(v.runtime.SaveSnapshot(snapshot,v.error,v.time),"save mixed focused layout");v.runtime.CloseDocument();Check(v.runtime.LoadDocument(v.source,"mixed-scroll.q4ui",ds)&&v.runtime.RestoreSnapshot(snapshot,v.error,v.time),"restore mixed layout into fresh document");v.Frame();owned=v.Element("viewport");old=v.Element(legacy);Check(Near(v.Offset(),300*ratio),"first restored layout preserves authored vertical offset");Check(old->GetScrollLeft()>0&&(relation==3||old->GetScrollTop()>0),"first restored layout reveals legacy axes alongside authored snapshot");Check(v.runtime.FocusedControl()==target,"mixed first layout retains saved content focus");v.NoActions();
}}
static void Persistence(){View v;Check(v.runtime.FocusControl("bar",v.time),"bar focus");v.Key(MenuInput::PageDown);v.Frame();const auto logical=v.Offset();std::string snapshot;Check(v.runtime.SaveSnapshot(snapshot,v.error,v.time),"save actual scroll snapshot");Json::Value saved;Json::CharReaderBuilder reader;std::istringstream input(snapshot);Check(Json::parseFromStream(reader,input,&saved,&v.error),"decode saved snapshot");Check(saved["widgets"]["version"]==3&&saved["widgets"]["controls"]["bar"]["scrollOffsetDp"].asDouble()==logical,"versioned logical offset persisted");
 v.viewport.displayScale=2;v.Frame();Check(Near(v.Offset(),logical*2),"live density preserves logical scroll offset");v.Frame();Check(Near(v.Offset(),logical*2),"ordinary frame does not replay a density correction");std::string scaledSnapshot;Check(v.runtime.SaveSnapshot(scaledSnapshot,v.error,v.time),"save at changed density");Json::Value scaled;std::istringstream scaledInput(scaledSnapshot);Check(Json::parseFromStream(reader,scaledInput,&scaled,&v.error)&&Near(scaled["widgets"]["controls"]["bar"]["scrollOffsetDp"].asDouble(),logical,.01),"snapshot serializes dp rather than physical pixels");v.Key(MenuInput::End);v.Frame();Check(v.runtime.RestoreSnapshot(snapshot,v.error,v.time),"restore exact source scroll snapshot");v.runtime.ReleaseInputSources();v.Frame();Check(Near(v.Offset(),logical*2),"restored dp offset waits for fresh layout and density");Check(v.runtime.FocusedControl()=="bar","restored scrollbar focus waits for offset and bounds together");v.Geometry();
 auto invalid=saved;invalid["widgets"]["controls"]["bar"]["scrollOffsetDp"]=-1;const auto prior=v.Offset();Check(!v.runtime.RestoreSnapshot(Encode(invalid),v.error,v.time)&&v.Offset()==prior,"invalid snapshot preserves viewport");
 Check(v.Element("content")->SetProperty("height","100dp"),"shrink content container");v.Frame();Check(v.Read().geometry.range>0,"outlying authored child still contributes actual overflow");Check(v.Element("last")->SetProperty("display","none"),"hide outlying child so actual content fits");v.Frame();Check(v.Offset()==0&&!v.Read().geometry.usable&&v.runtime.GetControlState("bar")==ControlState::Disabled,"fitting content has inert authored gutter");
 // Same source replacement retains only offsets from a supported snapshot.
 v.runtime.CloseDocument();std::vector<Diagnostic> ds;Check(v.runtime.LoadDocument(v.source,"scroll.q4ui",ds),"replace document");Check(v.runtime.RestoreSnapshot(snapshot,v.error,v.time),"restore into fresh same-source document");v.Frame();Check(Near(v.Offset(),logical*2),"replacement restores current-density offset");Check(v.runtime.FocusedControl()=="bar","replacement retains restored scrollbar focus after first measured layout");
 Check(v.Element("viewport")->SetProperty("display","none"),"hide viewport");v.Frame();Check(!v.Read().available&&v.runtime.GetControlState("bar")==ControlState::Disabled,"hidden viewport leaves scrollbar inert");Check(v.Element("bar")->GetBox().GetSize(Rml::BoxArea::Border).x>0,"hidden viewport preserves gutter width");
}
static void Lifecycle(){View v;v.Move(.03);v.runtime.PointerButton(true,v.time);v.Move(.5);const auto old=v.Offset();Check(old>0,"captured pointer moves before modal");Check(v.runtime.PushModal("modal",v.time),"modal opens over bar");v.Move(.9);v.runtime.PointerWheel(1,v.time);v.runtime.PointerButton(false,v.time);v.Frame();Check(v.Offset()==old,"modal prevents drag/wheel and release replay");Check(v.runtime.PopModal(v.time),"modal closes");v.Frame();v.NoActions();
 Check(v.runtime.FocusControl("bar",v.time),"focus recovered bar");v.Key(MenuInput::Home);v.Frame();v.Move(.03);v.runtime.PointerButton(true,v.time);Check(v.Element("track")->SetProperty("height","20dp"),"track becomes zero travel during drag");v.Frame();v.Move(.9);v.runtime.PointerButton(false,v.time);v.Frame();Check(v.Offset()==0&&!v.Read().geometry.usable,"zero travel retires drag");Check(v.Element("track")->SetProperty("height","180dp"),"restore track extent");v.Frame();v.Move(.8);v.runtime.PointerButton(false,v.time);Check(v.Offset()==0,"restored track cannot replay unmatched release");
 v.viewport.width=0;v.Frame();Bounds untouched{1,2,3,4};Check(!v.runtime.GetBounds("bar",untouched)&&untouched.x==1&&untouched.width==3,"zero viewport geometry query preserves output");Check(!v.Read().available&&!v.runtime.CanActivateControl("bar",v.time),"zero viewport copied diagnostic is unavailable");v.runtime.MenuAction(MenuInput::Back,true,v.time);v.runtime.MenuAction(MenuInput::Down,true,v.time);v.NoActions();v.Move(.5);v.runtime.PointerButton(true,v.time);v.runtime.PointerButton(false,v.time);v.viewport.width=1280;v.Frame();Check(v.Offset()==0,"zero viewport cannot carry a new pointer gesture");Check(v.runtime.FocusControl("bar",v.time),"bar can explicitly focus after valid recovery");v.runtime.MenuAction(MenuInput::Down,true,v.time);v.runtime.MenuAction(MenuInput::Back,true,v.time);v.Frame();Check(v.Offset()==0,"held navigation cannot revive after zero viewport");v.NoActions();v.runtime.MenuAction(MenuInput::Down,false,v.time);v.runtime.MenuAction(MenuInput::Back,false,v.time);v.Key(MenuInput::Down);v.Frame();Check(v.Offset()==36,"fresh source after release navigates normally");v.NoActions();
}
static void TwoAxes(){View v;auto source=Source();auto horizontal=Source(true);auto bar=Bar(horizontal);bar["id"]="a-horizontal";bar["control"]["parts"]["track"]="horizontal-track";bar["control"]["parts"]["thumb"]="horizontal-thumb";bar["children"][0]["id"]="horizontal-track";bar["children"][0]["children"][0]["id"]="horizontal-thumb";Feedback(source,bar);Frame(source)["children"].append(bar);Frame(source)["children"][0]["children"][0]["properties"]["width"]=Typed("length",1000,"dp");
 v.runtime.CloseDocument();std::vector<Diagnostic> ds;Check(v.runtime.LoadDocument(Encode(source),"dual.q4ui",ds),"same viewport permits unique orthogonal owners");v.Frame();Check(v.runtime.FocusControl("first",v.time),"dual axis content focus");auto* viewport=v.Element("viewport");auto wheel=[&](Rml::Element* element){auto point=element->GetAbsoluteOffset(Rml::BoxArea::Content)+element->GetBox().GetSize(Rml::BoxArea::Content)*.7f;v.runtime.PointerMove(point.x,point.y,v.time);v.runtime.PointerWheel(1,v.time);v.Frame();};
 wheel(viewport);Check(viewport->GetScrollTop()==36&&viewport->GetScrollLeft()==0,"content wheel prefers vertical despite horizontal ID sort order");Check(v.runtime.FocusedControl()=="first","dual content wheel preserves field focus");
 wheel(v.Element("horizontal-track"));Check(viewport->GetScrollTop()==36&&viewport->GetScrollLeft()==36,"direct horizontal artwork selects horizontal axis");wheel(v.Element("track"));Check(viewport->GetScrollTop()==72&&viewport->GetScrollLeft()==36,"direct vertical artwork selects vertical axis");
 Check(v.Element("content")->SetProperty("height","100dp")&&v.Element("last")->SetProperty("display","none"),"vertical content becomes fitting");v.Frame();Check(!v.Read().geometry.usable&&v.runtime.GetWidgetState("a-horizontal")->scroll->geometry.usable,"only horizontal range remains usable");wheel(viewport);Check(viewport->GetScrollTop()==0&&viewport->GetScrollLeft()==72,"content wheel falls back to horizontal when vertical fitting");v.NoActions();
}
static void ThumbFrames(){for(float ratio:{1.f,1.25f,2.f})for(bool contentBox:{false,true}){View v(ratio,false,true,true);auto source=Source(false,true,true);auto& props=Bar(source)["children"][0]["children"][0]["properties"];props["box-sizing"]=Typed("keyword",contentBox?"content-box":"border-box");props["border-width"]=Typed("length",20,"dp");props["padding-top"]=Typed("length",10,"dp");props["padding-bottom"]=Typed("length",10,"dp");v.runtime.CloseDocument();std::vector<Diagnostic> ds;Check(v.runtime.LoadDocument(Encode(source),"thumb-frames.q4ui",ds),"authored thumb border and padding source accepts");v.Frame();auto read=v.Read();Check(read.available&&Near(read.geometry.thumb,60*ratio,.01),"actual thumb frame is minimum measured border extent");v.Geometry();Check(v.runtime.FocusControl("bar",v.time),"framed thumb bar focus");v.Key(MenuInput::End);v.Frame();v.Geometry();Check(Near(v.Read().geometry.position+v.Read().geometry.thumb,v.Read().geometry.track,.01),"framed thumb ends at real track edge");v.Key(MenuInput::Home);v.Frame();auto g=v.Read().geometry;v.Move(g.thumb*.2/g.track);v.runtime.PointerButton(true,v.time);v.Move(.6);v.Frame();double expected=0;g=v.Read().geometry;Check(ScrollDragOffset(g,g.track*.6,.2,expected)&&Near(v.Offset(),expected),"thumb-frame drag uses actual measured extent");
 auto* thumb=v.Element("thumb");Check(thumb->SetProperty("border-width","100dp"),"thumb frame exceeds real track");v.Frame();Check(!v.Read().available&&!v.runtime.CanActivateControl("bar",v.time),"oversized thumb frame is inert");const auto offset=v.Offset();v.Move(.9);v.runtime.PointerButton(false,v.time);v.Frame();Check(v.Offset()==offset,"oversized frame cancels drag before next movement");Check(thumb->SetProperty("border-width","20dp"),"thumb frame fits again");v.Frame();Check(v.Read().available,"fitting thumb frame recovers");v.Geometry();v.NoActions();}}
// The SYSTEM page holds its rows on tabs without a scrolling body; its dialogs' bodies scroll a text that outgrows them.
// A finger pressed on the viewport's content drags it once it moves past 8 dp
// (section 13.6): the content follows and the release activates nothing. A
// shorter move is still a tap, a sideways one past 8 dp no longer is, a finger
// on the thumb drags the bar as a mouse does, and a mouse never drags content.
static void TouchDrag(){for(float ratio:{1.f,2.f}){
 // The fixture finds elements in the first context: one view at a time, each
 // without the test modal's box, which lies over the first button.
 const auto open=[](View& at){auto source=Source();Json::Value kept(Json::arrayValue);for(const auto& child:Frame(source)["children"])if(child["id"]!="modal")kept.append(child);
  Frame(source)["children"]=kept;at.runtime.CloseDocument();std::vector<Diagnostic> ds;Check(at.runtime.LoadDocument(Encode(source),"touch.q4ui",ds),"touch source loads");at.Frame();};
 Rml::Vector2f centre;
 const auto press=[&](View& at){at.runtime.PointerMove(centre.x,centre.y,at.time);at.Frame();at.runtime.PointerButton(true,at.time);};
 {View v(ratio);open(v);auto* first=v.Element("first");
  centre=first->GetAbsoluteOffset(Rml::BoxArea::Border)+first->GetBox().GetSize(Rml::BoxArea::Border)*.5f;
  v.runtime.SetTouchPointer(true);press(v);v.runtime.PointerMove(centre.x,centre.y-6*ratio,v.time);v.runtime.PointerButton(false,v.time);v.Frame();
  Check(v.Offset()==0&&v.runtime.TakeActions().size()==1,"a touch that moves under 8 dp is a tap");
  press(v);v.runtime.PointerMove(centre.x+20*ratio,centre.y,v.time);v.runtime.PointerButton(false,v.time);v.Frame();
  Check(v.Offset()==0&&v.runtime.TakeActions().empty(),"a touch moved 20 dp across the button is no longer a tap, nor a scroll");
  press(v);v.runtime.PointerMove(centre.x,centre.y-100*ratio,v.time);v.Frame();Check(Near(v.Offset(),100*ratio),"the dragged content follows the finger");
  v.runtime.PointerMove(centre.x,centre.y-140*ratio,v.time);v.Frame();Check(Near(v.Offset(),140*ratio),"and keeps following it");
  v.time+=.15;v.runtime.PointerMove(centre.x,centre.y-140*ratio,v.time);
  v.runtime.PointerButton(false,v.time);v.Frame();Check(v.runtime.TakeActions().empty()&&Near(v.Offset(),140*ratio),"a resting drag's release activates nothing and leaves the content there");}
 {View thumb(ratio);thumb.runtime.SetTouchPointer(true);thumb.Move(.03);thumb.runtime.PointerButton(true,thumb.time);thumb.Move(.5);thumb.Frame();
  Check(thumb.Offset()>0,"a finger on the thumb drags the bar");thumb.runtime.PointerButton(false,thumb.time);thumb.NoActions();}
 {View mouse(ratio);open(mouse);press(mouse);mouse.runtime.PointerMove(centre.x,centre.y-100*ratio,mouse.time);mouse.Frame();
  Check(mouse.Offset()==0,"a mouse does not drag content");mouse.runtime.PointerButton(false,mouse.time);mouse.Frame();mouse.NoActions();
  press(mouse);mouse.runtime.PointerMove(centre.x+20*ratio,centre.y,mouse.time);mouse.runtime.PointerButton(false,mouse.time);mouse.Frame();
  Check(mouse.runtime.TakeActions().size()==1,"a mouse released on the button it pressed still activates it");}}}
// The rest of section 13.6's touch rules. The source without the test
// modal's box, and a finger pressed at `from` and moved by `by` every 16 ms.
static Json::Value TouchSource(){auto source=Source();Json::Value kept(Json::arrayValue);for(const auto& child:Frame(source)["children"])if(child["id"]!="modal")kept.append(child);Frame(source)["children"]=kept;return source;}
static void Load(View& at,const Json::Value& source,const char* path){at.runtime.CloseDocument();std::vector<Diagnostic> ds;const bool ok=at.runtime.LoadDocument(Encode(source),path,ds);for(auto& d:ds)std::fprintf(stderr,"%s %s\n",d.pointer.c_str(),d.message.c_str());Check(ok,path);at.Frame();}
static Rml::Vector2f Centre(Rml::Element* e){return e->GetAbsoluteOffset(Rml::BoxArea::Border)+e->GetBox().GetSize(Rml::BoxArea::Border)*.5f;}
static void Stroke(View& v,Rml::Vector2f from,Rml::Vector2f by,int steps,bool lift=true){v.runtime.SetTouchPointer(true);v.runtime.PointerMove(from.x,from.y,v.time);v.runtime.PointerButton(true,v.time);
 for(int i=1;i<=steps;++i){v.time+=.016;v.runtime.PointerMove(from.x+by.x*i,from.y+by.y*i,v.time);}if(lift)v.runtime.PointerButton(false,v.time);}
static void Leave(View& v){v.runtime.PointerMove(std::numeric_limits<float>::quiet_NaN(),0,v.time);}
// Touch-down shows the pressed treatment (hover and focus with the press
// inset) in the same input, though the pointer presentation shows no focus
// otherwise; a finger moved past 8 dp drops it and a lifted one leaves the
// control unlit.
static void TouchPress(){View v;Load(v,TouchSource(),"press.q4ui");const auto at=Centre(v.Element("first"));
 v.runtime.SetTouchPointer(true);v.runtime.PointerMove(at.x,at.y,v.time);v.runtime.PointerButton(true,v.time);
 Check(v.runtime.GetControlState("first")==ControlState::Pressed&&v.runtime.FocusedControl()=="first","touch-down presses and focuses the control before any frame");
 v.runtime.PointerMove(at.x,at.y-12,v.time);Check(v.runtime.GetControlState("first")!=ControlState::Pressed,"a finger moved past 8 dp drops the press");
 v.runtime.PointerButton(false,v.time);Leave(v);v.Frame();v.runtime.TakeActions();
 v.runtime.PointerMove(at.x,at.y,v.time);v.runtime.PointerButton(true,v.time);v.Frame();Check(v.runtime.GetControlState("first")==ControlState::Pressed,"a resting finger keeps the press");
 v.runtime.PointerButton(false,v.time);Leave(v);v.Frame();Check(v.runtime.TakeActions().size()==1&&v.runtime.GetControlState("first")==ControlState::Default,"a tap activates and leaves the control unlit");}
// A released drag keeps its velocity and slows over its 325 ms decay until
// it stops; a resting finger leaves none, and a new touch, a focus move or a
// modal stops it at once.
static void TouchInertia(){for(float ratio:{1.f,2.f}){View v(ratio);auto* viewport=v.Element("viewport");const auto centre=Centre(viewport);
 // 20 dp every 16 ms: 1250 dp a second.
 Stroke(v,centre,{0,-20*ratio},5);const double released=v.Offset();Check(Near(released,100*ratio),"the drag follows the finger to its release");
 v.Frame();double step=v.Offset()-released,last=v.Offset();Check(step>10*ratio,"a released drag keeps moving");
 for(int i=0;i<200;++i){v.Frame();const double moved=v.Offset()-last;Check(moved>=0&&moved<=step+1.01,"inertia slows down");last=v.Offset();if(moved>0)step=moved;}
 // v·τ = 1250 dp/s × 0.325 s, less the 6.5 dp left below 20 dp a second.
 Check(Near(v.Offset()-released,400*ratio,24*ratio),"inertia carries the release velocity over its decay");
 viewport->SetScrollTop(0);v.Frame();Stroke(v,centre,{0,-20*ratio},5,false);v.time+=.15;v.runtime.PointerMove(centre.x,centre.y-100*ratio,v.time);v.runtime.PointerButton(false,v.time);
 const auto held=v.Offset();v.Frame();v.Frame();Check(v.Offset()==held,"a finger that rests before lifting leaves no inertia");
 const auto stops=[&](const char* why,auto interrupt){viewport->SetScrollTop(0);v.Frame();Stroke(v,centre,{0,-20*ratio},5);v.Frame();Check(v.Offset()>100*ratio,"the list flings");
  interrupt();const auto at=v.Offset();v.Frame();v.Frame();Check(v.Offset()==at,why);};
 stops("a new touch stops inertia",[&]{v.runtime.PointerMove(centre.x,centre.y,v.time);v.runtime.PointerButton(true,v.time);v.runtime.PointerButton(false,v.time);Leave(v);});
 stops("a modal stops inertia",[&]{Check(v.runtime.PushModal("modal",v.time),"a modal opens over the flinging list");});Check(v.runtime.PopModal(v.time),"the modal closes");
 stops("a focus move stops inertia",[&]{Check(v.runtime.FocusControl("bar",v.time),"focus moves to the bar");});
 v.NoActions();}}
// At either end a list's rim light brightens with the pull, the content
// staying put, and fades over 250 ms once the finger lifts; a fling that
// reaches an end lights it by the speed it arrives with.
static void TouchRim(){for(float ratio:{1.f,2.f}){const auto lit=[&](Rml::Element* rim){return rim->GetComputedValues().display()!=Rml::Style::Display::None;};
 {View v(ratio);auto* viewport=v.Element("viewport");const auto centre=Centre(viewport);
 const auto top=viewport->GetAbsoluteOffset(Rml::BoxArea::Padding);const float client=viewport->GetClientHeight();
 Stroke(v,centre,{0,8*ratio},4,false);v.Frame();auto* rim=v.Element("q4-touch-rim");
 Check(v.Offset()==0,"the content does not stretch past its start");Check(lit(rim)&&Near(rim->GetComputedValues().opacity(),.5,.02),"a 32 dp pull lights the start's rim at half strength");
 auto at=rim->GetAbsoluteOffset(Rml::BoxArea::Border);const auto size=rim->GetBox().GetSize(Rml::BoxArea::Border);
 Check(Near(at.x,top.x)&&Near(at.y,top.y)&&Near(size.x,viewport->GetClientWidth())&&Near(size.y,28.5*ratio),"the rim spans the list's start, 19 u deep");
 v.runtime.PointerMove(centre.x,centre.y+90*ratio,v.time);v.Frame();Check(Near(rim->GetComputedValues().opacity(),1,.01)&&v.Offset()==0,"a 64 dp pull lights it fully");
 v.runtime.PointerButton(false,v.time);Leave(v);v.Frame();Check(Near(rim->GetComputedValues().opacity(),1-.02/.25,.02),"let go, it fades");
 for(int i=0;i<13;++i)v.Frame();Check(!lit(rim),"over 250 ms");
 viewport->SetScrollTop(1e5f);v.Frame();const double range=v.Offset();Check(range>500*ratio,"the list reaches its end");
 Stroke(v,centre,{0,-12*ratio},4,false);v.Frame();at=rim->GetAbsoluteOffset(Rml::BoxArea::Border);
 Check(Near(v.Offset(),range),"nor past its end");Check(lit(rim)&&Near(at.y,top.y+client-28.5*ratio)&&Near(rim->GetComputedValues().opacity(),.75,.02),"a 48 dp pull past the end lights the end's rim");
 v.runtime.PointerButton(false,v.time);Leave(v);for(int i=0;i<15;++i)v.Frame();Check(!lit(rim)&&Near(v.Offset(),range),"a drag held against its end leaves no inertia and its rim fades");
 viewport->SetScrollTop(static_cast<float>(range-300*ratio));v.Frame();Stroke(v,centre,{0,-20*ratio},5);Leave(v);
 for(int i=0;i<100&&!lit(rim);++i)v.Frame();
 // About 635 dp a second remains of 1250 after 200 dp: a third of full strength.
 Check(Near(v.Offset(),range)&&lit(rim)&&rim->GetComputedValues().opacity()>.2f&&rim->GetComputedValues().opacity()<.45f,"a fling reaching the end lights its rim by its speed");
 for(int i=0;i<15;++i)v.Frame();Check(!lit(rim),"which fades in turn");}
 {View mouse(ratio);auto* list=mouse.Element("viewport");const auto c=Centre(list);mouse.runtime.PointerMove(c.x,c.y,mouse.time);mouse.runtime.PointerButton(true,mouse.time);mouse.runtime.PointerMove(c.x,c.y+90*ratio,mouse.time);mouse.Frame();
  mouse.runtime.PointerButton(false,mouse.time);mouse.Frame();auto* document=Rml::GetContext(0)->GetDocument(0);Check(!document->GetElementById("q4-touch-rim"),"a mouse lights no rim");}}}
// A finger resting 500 ms on a button that names a detail opens that detail
// instead of activating it, and the card stays, placed as for focus, after
// the finger lifts, until the next press. A tap, a drag, a button without a
// detail and a mouse keep their rules.
static void TouchLongPress(){for(float ratio:{1.f,2.f}){auto source=TouchSource();
 source["state"]["detail"]["type"]="number";source["state"]["detail"]["initial"]=-1;source["detail"]["state"]="detail";source["detail"]["card"]="card";
 Frame(source)["children"][0]["children"][0]["children"][0]["control"]["detail"]=3;
 auto plain=Box("plain",500,100,80,32);plain["control"]["role"]="button";plain["control"]["action"]="button";plain["control"]["label"]="#str_test";Feedback(source,plain);
 source["root"]["children"].append(plain);source["root"]["children"].append(Box("card",500,400,100,40));
 View v(ratio);Load(v,source,"detail.q4ui");auto* first=v.Element("first");const auto at=Centre(first),other=Centre(v.Element("plain"));
 const auto detail=[&]{StateValue value;Check(v.runtime.GetStateValue("detail",value),"detail state");return std::get<double>(value);};
 const auto hold=[&](Rml::Vector2f point,int frames){v.runtime.PointerMove(point.x,point.y,v.time);v.runtime.PointerButton(true,v.time);for(int i=0;i<frames;++i)v.Frame();v.runtime.PointerButton(false,v.time);Leave(v);v.Frame();};
 v.runtime.SetTouchPointer(true);hold(at,5);Check(v.runtime.TakeActions().size()==1&&detail()==-1,"a 100 ms tap activates and leaves no detail");
 hold(at,30);Check(v.runtime.TakeActions().empty(),"a 500 ms long-press activates nothing");Check(detail()==3,"it opens the button's detail, which stays after the finger lifts");
 auto* card=v.Element("card");const auto below=first->GetAbsoluteOffset(Rml::BoxArea::Border).y+first->GetBox().GetSize(Rml::BoxArea::Border).y+6*ratio;
 Check(Near(card->GetAbsoluteOffset(Rml::BoxArea::Border).y,below),"the card stands 6 dp under the button, as for focus");
 v.runtime.PointerMove(other.x,other.y,v.time);v.runtime.PointerButton(true,v.time);v.Frame();Check(detail()==-1,"the next press closes it");v.runtime.PointerButton(false,v.time);Leave(v);v.Frame();v.runtime.TakeActions();
 v.runtime.PointerMove(at.x,at.y,v.time);v.runtime.PointerButton(true,v.time);v.time+=.55;v.runtime.PointerButton(false,v.time);Leave(v);v.Frame();
 Check(v.runtime.TakeActions().empty()&&detail()==3,"a lift 500 ms after the press long-presses with no frame between");
 v.Key(MenuInput::Down);v.Frame();Check(detail()==(v.runtime.FocusedControl()=="first"?3:-1),"navigation hands the detail back to focus");
 v.runtime.SetTouchPointer(true);hold(at,30);v.runtime.TakeActions();const auto offset=v.Offset();
 v.runtime.PointerMove(at.x,at.y,v.time);v.runtime.PointerButton(true,v.time);for(int i=0;i<30;++i)v.Frame();v.runtime.PointerMove(at.x,at.y-60*ratio,v.time);v.Frame();
 Check(v.Offset()==offset,"a finger moving after its long-press drags nothing");v.runtime.PointerButton(false,v.time);Leave(v);v.Frame();Check(v.runtime.TakeActions().empty(),"nor activates");
 Stroke(v,at,{0,-10*ratio},5,false);for(int i=0;i<30;++i)v.Frame();Check(detail()==-1,"a dragging finger hovers nothing and long-presses nothing");v.runtime.PointerButton(false,v.time);Leave(v);v.Frame();
 hold(other,30);Check(v.runtime.TakeActions().size()==1,"a button without a detail keeps its tap however long the press");
 v.Element("viewport")->SetScrollTop(0);v.Frame();v.runtime.SetTouchPointer(false);v.runtime.PointerMove(at.x,at.y,v.time);v.runtime.PointerButton(true,v.time);for(int i=0;i<30;++i)v.Frame();v.runtime.PointerButton(false,v.time);v.Frame();
 Check(v.runtime.TakeActions().size()==1,"a mouse never long-presses");v.runtime.PointerMove(other.x,other.y,v.time);v.Frame();Check(detail()==-1,"its hover card follows the mouse");}}
// A swipe 48 dp across a tab strip asks for the next tab (toward the leading
// edge) or the previous one, as Q and E do, and activates nothing; a shorter
// or upright one, one off the strip and a mouse ask for nothing.
static void TouchSwipe(){for(float ratio:{1.f,2.f}){auto source=TouchSource();source["state"]["tab"]["type"]="number";source["state"]["tab"]["initial"]=0;
 for(const auto& [name,value]:std::vector<std::pair<const char*,int>>{{"onTabPrevious",0},{"onTabNext",1},{"tab_a",0},{"tab_b",1}}){Json::Value op;op["op"]="setState";op["values"]["tab"]=value;source["events"][name].append(op);}
 for(const auto& [id,event,left]:std::vector<std::tuple<const char*,const char*,double>>{{"tab-a","tab_a",480},{"tab-b","tab_b",620}}){auto tab=Box(id,left,20,120,36);tab["control"]["role"]="button";tab["control"]["event"]=event;tab["control"]["label"]="#str_test";Feedback(source,tab);source["root"]["children"].append(tab);}
 View v(ratio);Load(v,source,"tabs.q4ui");const auto a=Centre(v.Element("tab-a")),row=Centre(v.Element("first"));
 const auto events=[&]{return v.runtime.TakeGestureEvents();};using Events=std::vector<std::string>;
 Stroke(v,a,{-12*ratio,0},5);Check(events()==Events{"onTabNext"}&&v.runtime.TakeActions().empty(),"a 60 dp swipe toward the leading edge asks for the next tab and activates nothing");
 Stroke(v,a,{12*ratio,0},5);Check(events()==Events{"onTabPrevious"}&&v.runtime.TakeActions().empty(),"and toward the trailing edge the previous one");
 Stroke(v,a,{-8*ratio,0},4);Check(events().empty()&&v.runtime.TakeActions().empty(),"a 32 dp swipe asks for nothing and is no tap");
 Stroke(v,a,{2*ratio,-12*ratio},5);Check(events().empty(),"an upright move on the strip is no swipe");
 Stroke(v,row,{-12*ratio,0},5);Check(events().empty()&&v.runtime.TakeActions().empty(),"a swipe off the strip asks for nothing");
 v.runtime.SetTouchPointer(false);v.runtime.PointerMove(a.x,a.y,v.time);v.runtime.PointerButton(true,v.time);v.runtime.PointerMove(a.x-60*ratio,a.y,v.time);v.runtime.PointerButton(false,v.time);
 Check(events().empty(),"a mouse never swipes");}}
static void SystemCandidate(const std::string& text){Document document;std::vector<Diagnostic> ds;Check(document.Load(text,ds),"actual SYSTEM candidate parses");State state;std::string error;Check(state.Reset(document.Model(),error),"SYSTEM candidate typed state resets");
 std::string message;for(int i=0;i<40;++i)message+="The display change waits for your answer before it reverts by itself. ";
 // The engine lays the page out on its 720 dp canvas: the view's size sets the dp ratio.
 const std::pair<int,int> sizes[]={{1280,720},{1920,1080},{800,600}};
 for(const auto& [width,height]:sizes){HostFixture host;Runtime runtime(host);Check(runtime.LoadDocument(text,"system-candidate.q4ui",ds),"actual SYSTEM candidate Runtime loads");double time=1;Viewport vp;vp.width=width;vp.height=height;vp.canvasHeight=720;vp.canvasMinWidth=960;runtime.SetReducedMotion(true,time);
 Check(runtime.SetState({{"settings.open",true},{"settings.phase",1.},{"settings.busy",false},{"settings.confirmationVisible",false},{"settings.message",message}},error,time),"SYSTEM active Editing projection");
 auto frame=[&]{time+=.02;++host.frame;runtime.Frame(vp,time);Check(host.errors==0,"SYSTEM no runtime diagnostics");};frame();auto* doc=Rml::GetContext(0)->GetDocument(0);
 Check(!doc->GetElementById("settings_body_scrollbar")&&!doc->GetElementById("settings_header_scrollbar")&&!runtime.GetWidgetState("settings_body_scrollbar"),"the pages hold their rows without a scrolling body");
 Check(doc->GetElementById("settings-body")->GetScrollHeight()<=doc->GetElementById("settings-body")->GetClientHeight()+1,"the pages' bounds never overflow");
 // The display confirmation's body scrolls the long message.
 Check(runtime.SetState({{"settings.confirmationVisible",true},{"settings.canRevert",true},{"settings.canConfirm",true}},error,time),"SYSTEM confirmation shows");frame();frame();
 auto* body=doc->GetElementById("confirmation-body");auto* track=doc->GetElementById("confirmation_body_scrollbar-track");auto* thumb=doc->GetElementById("confirmation_body_scrollbar-thumb");Check(body&&track&&thumb,"real authored SYSTEM dialog parts");
 auto read=runtime.GetWidgetState("confirmation_body_scrollbar")->scroll;Check(read&&read->available&&read->geometry.usable,"SYSTEM dialog body has scrolling readback");Check(Near(thumb->GetBox().GetSize(Rml::BoxArea::Border).y,read->geometry.thumb,.01),"SYSTEM vector thumb derives from body extent");Check(doc->GetElementById("confirmation_body_scrollbar-thumb-base")->GetTagName()=="q4-vector","SYSTEM authored thumb retained");
 const auto initial=runtime.FocusedControl();Check(initial=="settings_revert","the dialog opens on Revert");const auto point=track->GetAbsoluteOffset(Rml::BoxArea::Content)+track->GetBox().GetSize(Rml::BoxArea::Content)*.9f;runtime.PointerMove(point.x,point.y,time);runtime.PointerWheel(1,time);frame();Check(body->GetScrollTop()>0&&runtime.FocusedControl()==initial,"SYSTEM wheel scrolls the dialog body and keeps the decision's focus");const double logical=body->GetScrollTop()/vp.DpRatio();
 vp.width=width==1920?1280:1920;vp.height=width==1920?720:1080;frame();Check(Near(body->GetScrollTop(),std::min(logical*vp.DpRatio(),double(body->GetScrollHeight()-body->GetClientHeight()))),"SYSTEM resize preserves the dialog's scroll or clamps a smaller range");
 Check(runtime.FocusControl("confirmation_body_scrollbar",time),"SYSTEM dialog bar explicit focus");runtime.MenuAction(MenuInput::End,true,time);runtime.MenuAction(MenuInput::End,false,time);frame();Check(Near(body->GetScrollTop(),body->GetScrollHeight()-body->GetClientHeight()),"SYSTEM End reaches the message's last line");Check(runtime.TakeActions().empty(),"SYSTEM scrollbar no settings operation");Check(host.draws>0,"SYSTEM actual CPU geometry submitted");
 if(width==1280){
 // The dialogs keep the screen beneath in soft focus where the host can soften it, and their 0.6 backing otherwise.
 auto shown=[&](const char* id){auto value=runtime.PresentedValue(id,"display");return value&&value->text!="none";};
 Check(shown("confirmation-panel-scrim")&&!shown("confirmation-panel-softfocus")&&host.softened==0&&runtime.Statistics().backdropFallbacks==0,"SYSTEM dialog keeps its backing without soft focus");
 host.softFocus=true;frame();
 Check(!shown("confirmation-panel-scrim")&&shown("confirmation-panel-softfocus")&&host.softened==1&&runtime.Statistics().backdropComposites==1&&Near(host.sigma,7.5*vp.DpRatio(),.01)&&Near(host.saturation,.8,.001),"SYSTEM dialog softens the screen beneath");
 Check(runtime.SetState({{"settings.confirmationVisible",false}},error,time),"SYSTEM confirmation hides");frame();
 Check(host.softened==1&&runtime.Statistics().backdropComposites==0,"a closed SYSTEM dialog softens nothing");host.softFocus=false;
 }
 }}
int main(int argc,char** argv){Schema();Gestures();DensityKeepsContentFocus();MixedViewportReveal();Persistence();Lifecycle();TwoAxes();ThumbFrames();TouchDrag();TouchPress();TouchInertia();TouchRim();TouchLongPress();TouchSwipe();if(argc>1){std::ifstream file(argv[1],std::ios::binary);std::string text{std::istreambuf_iterator<char>(file),{}};SystemCandidate(text);}
 std::printf("PASS %u checks; actual canonical schema/Interaction/Runtime/Rml layout; counted host, no native input or GPU qualification.\n",checks);}
