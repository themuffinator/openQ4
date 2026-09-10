// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "src/ui/retained/Runtime.h"
#include <RmlUi/Core.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/ElementUtilities.h>
#include <json/json.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <map>
using namespace openq4::ui;
static unsigned checks=0;
static void Check(bool ok,const char* reason){++checks;if(!ok){std::fprintf(stderr,"FAIL %s\n",reason);std::exit(1);}}
static bool Near(float a,float b,float epsilon=.08f){return std::abs(a-b)<epsilon;}
static Json::Value Typed(const char* type,Json::Value value,const char* unit=nullptr){Json::Value r;r["type"]=type;r["value"]=std::move(value);if(unit)r["unit"]=unit;return r;}
static Json::Value Array(std::initializer_list<double> values){Json::Value r(Json::arrayValue);for(auto v:values)r.append(v);return r;}
static Json::Value Box(const char* id,double x,double y,double w,double h){Json::Value r;r["id"]=id;r["type"]="group";r["children"]=Json::Value(Json::arrayValue);auto& p=r["properties"];
    p["position"]=Typed("keyword","absolute");p["display"]=Typed("keyword","block");p["opacity"]=Typed("number",1);
    p["left"]=Typed("length",x,"dp");p["top"]=Typed("length",y,"dp");p["width"]=Typed("length",w,"dp");p["height"]=Typed("length",h,"dp");return r;}
static void AddControl(Json::Value& source,Json::Value& node){const auto id=node["id"].asString();auto& c=node["control"];c["role"]="toggle";c["label"]="#str_label";c["action"]=id+"-edit";c["value"]["state"]="flag";c["parts"]["checked"]=id+"-checked";
    for(const char* state:{"default","hover","focus","pressed","disabled"})c["states"][state]=id+"-feedback";
    auto checked=Box((id+"-checked").c_str(),4,4,12,12);node["children"].append(checked);
    auto& action=source["actions"][id+"-edit"];action["operation"]="test.edit";action["input"]="boolean";action["arguments"]["value"]["input"]="value";
    Json::Value timeline,track;timeline["id"]=id+"-feedback";timeline["durationMs"]=1;track["node"]=id;track["property"]="opacity";
    for(unsigned time:{0u,1u}){Json::Value key;key["atMs"]=time;key["value"]=Typed("number",1);track["keys"].append(key);}timeline["tracks"].append(track);source["timelines"].append(timeline);
}
static std::string Source(bool nested=false,bool rotated=false,bool oversized=false,bool atEnd=false){
    Json::Value source;source["format"]="openq4-ui";source["version"]=1;source["id"]="focus-reveal";source["state"]["flag"]["type"]="boolean";source["state"]["flag"]["initial"]=false;
    auto root=Box("root",0,0,640,360),outer=Box("outer",40,40,280,150),content=Box("outer-content",0,0,700,700);
    outer["properties"]["overflow"]=Typed("keyword","auto");outer["properties"]["border-width"]=Typed("length",nested?2:0,"dp");outer["properties"]["padding"]=Typed("length",nested?3:0,"dp");
    auto upper=Box("upper",8,8,80,36);AddControl(source,upper);content["children"].append(upper);
    auto lower=Box("lower",nested?35:180,atEnd?660:280,oversized?400:90,oversized?230:40);AddControl(source,lower);
    lower["properties"]["background-color"]=Typed("color",Array({1,0,0,1}));
    if(rotated){outer["properties"]["transform"]=Typed("transform",Array({0,0,1.05,.95,18}),"dp");lower["properties"]["transform"]=Typed("transform",Array({2,1,1,.9,-11}),"dp");}
    if(nested){auto inner=Box("inner",120,240,200,100),inside=Box("inner-content",0,0,700,700);inner["properties"]["overflow"]=Typed("keyword","auto");
        inner["properties"]["border-width"]=Typed("length",3,"dp");inner["properties"]["padding"]=Typed("length",2,"dp");
        if(rotated)inner["properties"]["transform"]=Typed("transform",Array({0,0,.9,1.1,-7}),"dp");
        inside["children"].append(lower);inner["children"].append(inside);content["children"].append(inner);
    }else content["children"].append(lower);
    outer["children"].append(content);root["children"].append(outer);source["root"]=root;
    Json::StreamWriterBuilder writer;writer["indentation"]="";return Json::writeString(writer,source);
}
struct TestHost final:Host {
    unsigned errors=0;std::uint64_t frame=0;std::vector<Vertex> red;std::vector<double> fontTimes;
    bool ReadFile(const std::string&,std::string&)override{return false;}bool ReadCVar(const std::string&,size_t,StateValue&)override{return false;}
    std::string Translate(const std::string&)override{return "Label";}void Log(bool error,const std::string& m)override{if(error){++errors;std::fprintf(stderr,"Runtime %s\n",m.c_str());}}
    std::uintptr_t LoadMaterial(const std::string&,int& w,int& h)override{w=h=1;return 1;}
    void Draw(const std::vector<Vertex>& v,const std::vector<int>& indices,std::uintptr_t)override{for(int i:indices){Check(i>=0&&static_cast<size_t>(i)<v.size(),"bounded submitted geometry");const auto& p=v[i];Check(std::isfinite(p.x)&&std::isfinite(p.y),"finite submitted geometry");if(p.r>.99&&p.g<.01&&p.b<.01&&p.a>.99)red.push_back(p);}}
    std::uint64_t RenderFrame()const override{return frame;}bool BeginLayer(std::uint32_t,int,int)override{return true;}
    void CompositeLayer(std::uint32_t,std::uint32_t,float,const Bounds&)override{}void MaskLayer(std::uint32_t,std::uint32_t,const Bounds&)override{}void EndLayer(std::uint32_t)override{}
    FontMetrics GetFontMetrics(const std::string&,int size)override{fontTimes.push_back(Rml::GetSystemInterface()->GetElapsedTime());return {size*.8f,size*.2f,size*1.2f,size*.5f};}
    Glyph GetGlyph(const std::string&,int size,std::uint32_t)override{return {size*.5f,0,-size*.8f,size*.5f,float(size),0,0,1,1,"font"};}
};
struct View {
    TestHost host;Runtime runtime{host};Viewport viewport;double time=1;
    explicit View(float density,bool nested=false,bool rotated=false,bool oversized=false,bool atEnd=false){viewport.width=1280;viewport.height=720;viewport.displayScale=density;std::vector<Diagnostic> diagnostics;
        const bool loaded=runtime.LoadDocument(Source(nested,rotated,oversized,atEnd),"focus-reveal.q4ui",diagnostics);for(auto& d:diagnostics)std::fprintf(stderr,"%s %s\n",d.pointer.c_str(),d.message.c_str());Check(loaded,"real authored fixture parses");Frame();}
    void Frame(){host.red.clear();++host.frame;time+=.01;runtime.Frame(viewport,time);}
    Rml::Element* Element(const char* id){auto* context=Rml::GetContext(0);Check(context!=nullptr,"actual Rml context");auto* element=context->GetDocument(0)->GetElementById(id);Check(element!=nullptr,"actual authored element");return element;}
    void Focus(){Check(runtime.FocusControl("lower",time),"lower control focused");Frame();Check(runtime.FocusedControl()=="lower"&&runtime.TakeActions().empty(),"reveal does not activate control");}
};
static void FullInset(View& v,const char* parent){
    auto* target=v.Element("lower");auto* scroll=v.Element(parent);Rml::Array<Rml::Vector2f,4> quad;Check(Rml::ElementUtilities::GetBorderBoxQuad(quad,target),"exact projected control quad");
    const auto origin=scroll->GetAbsoluteOffset(Rml::BoxArea::Border)+Rml::Vector2f(scroll->GetClientLeft(),scroll->GetClientTop());
    const float inset=4*v.viewport.DpRatio();float left=1e20f,top=1e20f,right=-1e20f,bottom=-1e20f;
    for(auto point:quad){Check(scroll->Project(point),"project quad into scroll plane");left=std::min(left,point.x-origin.x);top=std::min(top,point.y-origin.y);right=std::max(right,point.x-origin.x);bottom=std::max(bottom,point.y-origin.y);}
    std::fprintf(stderr,"density %.2f %s bounds %.3f %.3f %.3f %.3f client %.3f %.3f scroll %.3f %.3f inset %.3f\n",v.viewport.displayScale,parent,left,top,right,bottom,scroll->GetClientWidth(),scroll->GetClientHeight(),scroll->GetScrollLeft(),scroll->GetScrollTop(),inset);
    Check(left>=inset-.08f&&top>=inset-.08f&&right<=scroll->GetClientWidth()-inset+.08f&&bottom<=scroll->GetClientHeight()-inset+.08f,"entire focused control has 4dp scroll inset");
}
static void BasicAndNested(){for(float density:{1.f,1.25f,2.f})for(bool nested:{false,true})for(bool rotated:{false,true}){
    View v(density,nested,rotated);v.Focus();FullInset(v,"outer");if(nested)FullInset(v,"inner");
    auto* body=v.Element("outer");const auto scroll=Rml::Vector2f(body->GetScrollLeft(),body->GetScrollTop());v.Frame();v.Frame();Check(scroll==Rml::Vector2f(body->GetScrollLeft(),body->GetScrollTop()),"unchanged frames do not creep focus scroll");
    if(!rotated){Rml::Rectanglef expected;Check(Rml::ElementUtilities::GetBoundingBox(expected,v.Element("lower"),Rml::BoxArea::Border),"paint comparison bounds");Check(!v.host.red.empty(),"focused artwork submitted");float lo=1e20f,hi=-1e20f;for(auto p:v.host.red){lo=std::min(lo,p.y);hi=std::max(hi,p.y);}Check(Near(lo,expected.Top(),1.01f)&&Near(hi,expected.Bottom(),1.01f),"full target painted through actual runtime clipping");}
    Check(v.host.errors==0,"no runtime diagnostics");}}
static void InputAndResize(){View v(1);v.Focus();v.viewport.displayScale=2;v.Frame();FullInset(v,"outer");
    auto* body=v.Element("outer");Rml::Rectanglef box;Check(Rml::ElementUtilities::GetBoundingBox(box,body,Rml::BoxArea::Border),"pointer scroll body bounds");
    v.runtime.PointerMove(box.Left()+30,box.Top()+30,v.time);const float before=body->GetScrollTop();v.runtime.PointerWheel(-1,v.time);const float user=body->GetScrollTop();Check(user<before,"semantic pointer wheel scrolls body");v.Frame();v.Frame();Check(body->GetScrollTop()==user,"pointer scrolling is not hijacked by unchanged focus");
    Check(v.runtime.FocusControl("lower",v.time),"explicit focus can reveal again");v.Frame();FullInset(v,"outer");}
static void LimitsAndHelper(){
    {View v(2,false,false,true);v.Focus();auto* body=v.Element("outer");const float y=body->GetScrollTop(),x=body->GetScrollLeft();v.Frame();v.Frame();Check(y==body->GetScrollTop()&&x==body->GetScrollLeft(),"oversized target does not oscillate");Check(y>=0&&y<=body->GetScrollHeight()-body->GetClientHeight()+1,"oversized scroll stays in authored range");}
    {View v(2,false,false,false,true);v.Focus();auto* body=v.Element("outer");Check(Near(body->GetScrollTop(),std::round(body->GetScrollHeight()-body->GetClientHeight()),1.01f),"end target clamps to existing authored overflow");const float height=body->GetScrollHeight();v.Frame();Check(body->GetScrollHeight()==height,"reveal never fabricates extra scroll range");}
    {View v(2);auto* lower=v.Element("lower");auto* root=Rml::GetContext(0)->GetRootElement();Rml::Array<Rml::Vector2f,4> out;for(auto& p:out)p={7,9};const auto before=out;
        Check(!Rml::ElementUtilities::GetBorderBoxQuad(out,nullptr)&&out==before,"null quad preserves output");
        Check(lower->SetProperty("transform","matrix3d(1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,-1)"),"negative-w test transform");Rml::GetContext(0)->Update();root->UpdateGeometryForProjection();
        Check(!Rml::ElementUtilities::GetBorderBoxQuad(out,lower)&&out==before,"negative-w projection preserves output");}
}
static void HorizontalAndProjection(){
    for(float density:{1.25f,2.f}){
        View v(density);auto* lower=v.Element("lower");auto* body=v.Element("outer");
        // Change actual Rml layout solely for this geometry case. The canonical
        // source remains immutable and Runtime supplies the production reveal.
        lower->SetProperty("left","600dp");v.Frame();v.Focus();FullInset(v,"outer");
        Check(body->GetScrollLeft()>0,"focus reveals horizontal overflow");
        v.Element("upper")->SetProperty("left","0dp");v.Element("upper")->SetProperty("top","0dp");
        Check(v.runtime.FocusControl("upper",v.time),"reverse focus traverses existing scroll range");v.Frame();
        Check(body->GetScrollLeft()==0&&body->GetScrollTop()==0,"upper target reaches native origin without invented negative range");
        lower->SetProperty("width","277dp");lower->SetProperty("left","180dp");v.Frame();v.Focus();
        Rml::Array<Rml::Vector2f,4> quad;Check(Rml::ElementUtilities::GetBorderBoxQuad(quad,lower),"almost-full-size quad");
        const auto origin=body->GetAbsoluteOffset(Rml::BoxArea::Border);const float left=quad[0].x-origin.x,right=quad[1].x-origin.x;
        Check(left>=-.02f&&right<=body->GetClientWidth()+.02f,"nearly full-width control preserves its full border with reduced inset");
        const auto old=Rml::Vector2f(body->GetScrollLeft(),body->GetScrollTop());v.Frame();v.Frame();
        Check(old==Rml::Vector2f(body->GetScrollLeft(),body->GetScrollTop()),"reduced margin does not oscillate");
    }
    {View v(1);auto* lower=v.Element("lower");auto* root=Rml::GetContext(0)->GetRootElement();
        Check(lower->SetProperty("transform-origin","0px 0px"),"explicit rotation origin");
        Check(lower->SetProperty("transform","rotate(90deg)"),"exact independent quarter-turn fixture");
        Rml::GetContext(0)->Update();root->UpdateGeometryForProjection();
        const auto origin=lower->GetAbsoluteOffset(Rml::BoxArea::Border),size=lower->GetBox().GetSize(Rml::BoxArea::Border);
        Rml::Array<Rml::Vector2f,4> quad;Check(Rml::ElementUtilities::GetBorderBoxQuad(quad,lower),"rotated quad available");
        const Rml::Array<Rml::Vector2f,4> expected={origin,origin+Rml::Vector2f(0,size.x),origin+Rml::Vector2f(-size.y,size.x),origin+Rml::Vector2f(-size.y,0)};
        for(size_t i=0;i<quad.size();++i)Check(Near(quad[i].x,expected[i].x)&&Near(quad[i].y,expected[i].y),"quad retains exact ordered transformed corners");
        const auto before=quad;Check(lower->SetProperty("transform","translateZ(20000px)"),"depth-clipped fixture");Rml::GetContext(0)->Update();root->UpdateGeometryForProjection();
        Check(!Rml::ElementUtilities::GetBorderBoxQuad(quad,lower)&&quad==before,"depth-clipped quad fails unchanged");
    }
    {View v(2);auto* body=v.Element("outer");body->SetProperty("transform","scaleX(0)");v.Frame();const float old=body->GetScrollTop();
        Check(!v.runtime.FocusControl("lower",v.time),"singular projected control cannot acquire fabricated focus");
        Check(body->GetScrollTop()==old,"singular scroll plane never triggers guessed scrolling");}
    {View v(1);auto* lower=v.Element("lower");lower->SetProperty("font-size","33px");v.host.fontTimes.clear();
        Check(v.runtime.FocusControl("lower",v.time),"focus-triggered layout handles dirty typography");
        Check(!v.host.fontTimes.empty(),"focus update queried newly sized actual font");
        for(double observed:v.host.fontTimes)Check(observed==v.time,"focus-triggered Rml callbacks use this view's presentation clock");}
}
int main(int argc,char**){if(argc>1){View v(2);v.Focus();FullInset(v,"outer");return 0;}BasicAndNested();InputAndResize();LimitsAndHelper();HorizontalAndProjection();std::printf("PASS %u checks\n",checks);}
