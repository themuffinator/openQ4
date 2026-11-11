// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#define main ValueRuntimeFixtureMain
#include "UiValueRuntimeTest.cpp"
#undef main
#include <RmlUi/Core/ComputedValues.h>
#include <limits>

static Rml::Element* Element(const char* id) {
    auto* element=Rml::GetContext(0)->GetDocument(0)->GetElementById(id);
    Check(element!=nullptr,"real canonical element exists");return element;
}
static Json::Value TypographySource() {
    Json::Value source;source["format"]="retained-ui";source["version"]=1;source["id"]="text-scale";
    auto root=Box("root",0,0,640,400);
    root["properties"]["font-family"]=Typed("font","test-font");
    root["properties"]["font-size"]=Typed("length",16,"dp");
    root["properties"]["line-height"]=Typed("length",24,"dp");
    auto label=Box("label",20,20,160,80,"text");
    label["properties"]["font-size"]=Typed("length",20,"dp");
    label["properties"]["letter-spacing"]=Typed("length",1,"dp");
    root["children"].append(label);
    auto inherited=Box("inherited",20,120,160,80,"text");root["children"].append(inherited);
    auto relative=Box("relative",240,20,160,80,"text");
    relative["properties"]["font-size"]=Typed("length",.5,"em");
    relative["properties"]["line-height"]=Typed("length",1.5,"em");
    relative["properties"]["height"]=Typed("length",2,"em");
    root["children"].append(relative);
    auto pixels=Box("pixels",240,120,160,80,"text");
    pixels["properties"]["font-size"]=Typed("length",14,"px");
    root["children"].append(pixels);source["root"]=root;
    PropertyTimeline(source,"grow","label","font-size",Typed("length",20,"dp"),Typed("length",28,"dp"),1000);
    return source;
}
static void Scaling(TestHost& host) {
    const auto source=Text(TypographySource());View view(host,source);
    for(float density:{1.f,1.25f,1.5f,2.f})for(float text:{1.f,1.5f,2.f}) {
        view.viewport.displayScale=density;view.viewport.textScale=text;
        view.viewport.originX=31;view.viewport.originY=17;view.viewport.pixelDensityX=1.5f;view.viewport.pixelDensityY=2;
        view.Frame();const auto& label=Element("label")->GetComputedValues();
        Check(Near(label.font_size(),20*density*text),"explicit dp typography uses display and text scale exactly once");
        Check(Near(label.letter_spacing(),density*text),"authored tracking follows independent text scale");
        Check(Near(Element("inherited")->GetComputedValues().font_size(),16*density*text),"inherited typography does not compound text scale");
        Check(Near(Element("inherited")->GetComputedValues().line_height().value,24*density*text),"inherited absolute line height follows text");
        Check(Near(Element("relative")->GetComputedValues().font_size(),8*density*text),"relative font uses already scaled parent exactly once");
        Check(Near(view.BoxOf("relative").height,16*density*text),"em layout grows from the actual text metrics");
        Check(Near(Element("pixels")->GetComputedValues().font_size(),14*text),"explicit pixel text remains independent of display density");
        const auto box=view.BoxOf("label");
        Check(Near(box.x,20*density)&&Near(box.y,20*density)&&Near(box.width,160*density)&&Near(box.height,80*density),"fixed dp furniture is unchanged by text scale");
        float x,y;view.viewport.WindowToDocument(42,23,x,y);
        Check(Near(x,32)&&Near(y,29),"input coordinate conversion excludes text scaling");
        Check(view.runtime.PresentedValue("label","font-size")->data[0]==20,"presentation source remains unscaled");
    }
    view.viewport.displayScale=1;view.viewport.textScale=2;
    Check(view.runtime.PlayTimeline("grow",view.time),"font timeline starts");
    view.time+=.49;view.Frame();
    Check(Near(view.runtime.PresentedValue("label","font-size")->data[0],24),"motion evaluates in authored units");
    Check(Near(Element("label")->GetComputedValues().font_size(),48),"animation samples are scaled at presentation");
    view.viewport.textScale=1.5f;view.Frame();
    Check(Near(Element("label")->GetComputedValues().font_size(),view.runtime.PresentedValue("label","font-size")->data[0]*1.5),"changing text size preserves animation progress");
    std::string snapshot,error;Check(view.runtime.SaveSnapshot(snapshot,error,view.time),"scaled instance snapshots authored values");
    view.runtime.CloseDocument();std::vector<Diagnostic> diagnostics;
    Check(view.runtime.LoadDocument(source,"value-runtime.rui",diagnostics)&&view.runtime.RestoreSnapshot(snapshot,error,view.time),"source survives renderer-style recreation");
    view.Frame();Check(Near(Element("label")->GetComputedValues().font_size(),view.runtime.PresentedValue("label","font-size")->data[0]*1.5),"restored scale is not baked into saved animation");
}
int main() {
    Viewport viewport;
    viewport.width=1280;viewport.height=720;viewport.displayScale=2;viewport.userScale=2;
    viewport.FitToMinimum(640,480);
    Check(Near(viewport.DpRatio(),1.5)&&Near(viewport.fitScale,.375),"root-menu fitting keeps a usable logical area");
    Check(viewport.displayScale==2&&viewport.userScale==2,"fitting preserves requested density and user preference");
    viewport.width=2560;viewport.height=1920;viewport.FitToMinimum(640,480);
    Check(Near(viewport.DpRatio(),4)&&viewport.fitScale==1,"larger window restores requested size without cumulative fitting");
    viewport.width=640;viewport.height=480;viewport.FitToMinimum(640,480);
    Check(Near(viewport.DpRatio(),1),"smallest qualification viewport keeps reset accessible");
    viewport.FitToMinimum(0,360);Check(viewport.fitScale==1,"invalid fitting request leaves generic density unchanged");
    for(float input:{0.f,-1.f,std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()}) {
        viewport.textScale=input;Check(viewport.TextRatio()==1,"invalid text scale recovers to default");
    }
    viewport.textScale=.5;Check(viewport.TextRatio()==1,"text scale cannot reduce readability below 100 percent");
    viewport.textScale=3;Check(viewport.TextRatio()==2,"text scale clamps to supported 200 percent maximum");
    TestHost host;Scaling(host);Check(host.errors==0,"text scale produces no runtime errors");
    std::printf("Text scale: %u checks passed\n",checks);
}
