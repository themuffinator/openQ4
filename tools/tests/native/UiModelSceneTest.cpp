// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "src/ui/retained/Runtime.h"
#include "src/ui/retained/State.h"
#include <json/json.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <map>

using namespace openq4::ui;
static unsigned checks=0;
static void Check(bool value,const char* message) {
    ++checks;
    if (!value) { std::fprintf(stderr,"FAIL: %s\n",message); std::exit(1); }
}
static std::string Encode(const Json::Value& value) {
    Json::StreamWriterBuilder writer; writer["indentation"]="";
    return Json::writeString(writer,value);
}
static Json::Value Source() {
    Json::Value source,root,model;
    source["format"]="retained-ui"; source["version"]=1; source["id"]="model-scene-test";
    source["state"]["body"]["type"]="string"; source["state"]["body"]["initial"]="";
    source["state"]["yaw"]["type"]="number"; source["state"]["yaw"]["initial"]=200;
    source["state"]["shown"]["type"]="boolean"; source["state"]["shown"]["initial"]=true;
    root["id"]="root"; root["type"]="group";
    model["id"]="preview"; model["type"]="model";
    auto& properties=model["properties"];
    properties["display"]["type"]="keyword"; properties["display"]["value"]="block";
    for (const auto& [key,value]:std::map<std::string,double>{{"width",320},{"height",200}}) {
        properties[key]["type"]="length"; properties[key]["value"]=value; properties[key]["unit"]="dp";
    }
    auto& scene=model["scene"];
    scene["parts"][0]["model"]["state"]="body";
    scene["parts"][0]["anim"]="idle"; scene["parts"][0]["animClass"]="player_ui_anims";
    scene["parts"][0]["skin"]="skins/marine";
    scene["parts"][1]["model"]="head_ui"; scene["parts"][1]["joint"]="chest"; scene["parts"][1]["skin"]="skins/head";
    scene["parts"][2]["model"]="weapon_machinegun_world"; scene["parts"][2]["joint"]="PST_ATTACHER";
    scene["rotation"].append(0); Json::Value yaw; yaw["state"]="yaw"; scene["rotation"].append(yaw); scene["rotation"].append(0);
    root["children"].append(model); source["root"]=root;
    auto& binding=source["bindings"][0];
    binding["id"]="preview-visible"; binding["node"]="preview"; binding["property"]="display";
    binding["value"]["op"]="select"; binding["value"]["args"][0]["state"]="shown";
    binding["value"]["args"][1]="block"; binding["value"]["args"][2]="none";
    return source;
}
struct ModelHost final:Host {
    struct Call { std::uint64_t lease; ModelScene scene; int width,height; double seconds; };
    std::vector<Call> calls;
    std::vector<std::uint64_t> released;
    std::vector<std::uintptr_t> painted;
    bool modelOrientation=false;
    unsigned composites=0;
    std::uint64_t frame=1;
    bool refuse=false;
    bool ReadFile(const std::string&,std::string&) override { return false; }
    std::string Translate(const std::string& text) override { return text; }
    bool ReadCVar(const std::string&,std::size_t,StateValue&) override { return false; }
    void Log(bool,const std::string&) override {}
    std::uintptr_t LoadMaterial(const std::string& name,int& width,int& height) override {
        width=height=4; return name.find("q4-model/")==std::string::npos?1:11;
    }
    void Draw(const std::vector<Vertex>& vertices,const std::vector<int>&,std::uintptr_t material) override {
        painted.push_back(material);
        if (material==11 && vertices.size()>=4) modelOrientation=vertices[0].v>vertices[2].v;
    }
    std::uint64_t RenderFrame() const override { return frame; }
    bool BeginLayer(std::uint32_t,int,int) override { return true; }
    void CompositeLayer(std::uint32_t,std::uint32_t,float,const Bounds&) override { ++composites; }
    void MaskLayer(std::uint32_t,std::uint32_t,const Bounds&) override {}
    void EndLayer(std::uint32_t) override {}
    FontMetrics GetFontMetrics(const std::string&,int size) override { return {float(size),0,float(size),float(size/2)}; }
    Glyph GetGlyph(const std::string&,int size,std::uint32_t) override { Glyph result; result.advance=float(size/2); return result; }
    bool RenderModelScene(std::uint64_t lease,const ModelScene& scene,int width,int height,double seconds,
        std::string& image,std::string& error) override {
        calls.push_back({lease,scene,width,height,seconds});
        if (refuse) { error="Injected model target failure"; return false; }
        image="q4-model/1"; return true;
    }
    void ReleaseModelScene(std::uint64_t lease) override { released.push_back(lease); }
};
static void SchemaAndTransactions() {
    auto source=Source();
    Document document; std::vector<Diagnostic> diagnostics;
    const bool loaded=document.Load(Encode(source),diagnostics);
    for (const auto& diagnostic:diagnostics) std::fprintf(stderr,"%s: %s\n",diagnostic.pointer.c_str(),diagnostic.message.c_str());
    Check(loaded,"source-bound model, head and weapon validate");
    State state=document.InitialState(); std::string error;
    Check(state.Set({{"body",std::string("player_marine_ui")}},error),"application publishes the selected asset before presentation");
    const auto* node=document.Model().FindNode("preview");
    ModelScene resolved;
    Check(node && node->model && ResolveModelScene(*node->model,state.Variables(),resolved,error),"compiled model scene resolves from canonical state");
    Check(resolved.parts.size()==3 && resolved.parts[1].skin=="skins/head" && resolved.parts[2].joint=="PST_ATTACHER" &&
        resolved.rotation[1]==200 && resolved.lights.size()==1,"body/head skins, attachments, rotation and default light survive parsing");
    const auto revision=state.Revision();
    for (const auto& bad:StateValues{{"body",std::string("../outside/model")},{"yaw",400000.0}}) {
        Check(!state.Set({bad},error) && state.Revision()==revision && state.Variables().at("body")==StateValue(std::string("player_marine_ui")),
            "invalid model state refuses the complete transaction");
    }
    Check(state.Set({{"body",std::string("player_strogg_ui")},{"yaw",215.0}},error) &&
        ResolveModelScene(*node->model,state.Variables(),resolved,error) && resolved.rotation[1]==215,
        "valid model and camera edits publish together");
    for (unsigned which=0;which<8;++which) {
        auto bad=source;
        auto& preview=bad["root"]["children"][0];
        switch(which) {
            case 0: preview["scene"]["parts"].append(preview["scene"]["parts"][1]); break;
            case 1: preview["scene"]["parts"][0]["model"]=5; break;
            case 2: preview["scene"]["parts"][0]["joint"]="chest"; break;
            case 3: preview["scene"]["fov"]=180; break;
            case 4: preview["scene"]["camera"].append(1); break;
            case 5: preview["scene"]["parts"][1]["model"]="C:/asset"; break;
            case 6: preview["scene"]["outlineWidth"]=7; break;
            case 7: preview["type"]="image"; preview["properties"]["image"]["type"]="image"; preview["properties"]["image"]["value"]=""; break;
        }
        Document rejected;
        Check(!rejected.Load(Encode(bad),diagnostics),"malformed or unbounded scene is rejected, not silently rendered");
    }
}
static void ActualRuntime() {
    ModelHost host;
    Runtime runtime(host); std::vector<Diagnostic> diagnostics;
    Check(runtime.Initialize() && runtime.LoadDocument(Encode(Source()),"models.rui",diagnostics),"model document loads into actual Rml runtime");
    std::string error;
    Check(runtime.SetState({{"body",std::string("player_marine_ui")}},error,.1),"session publishes selected model before first frame");
    Viewport viewport; viewport.width=1280; viewport.height=720;
    Check(runtime.Frame(viewport,.1) && host.calls.size()==1,"visible scene is prepared before first paint");
    const auto first=host.calls.back();
    Check(first.width==320 && first.height==200 && first.lease && first.scene.parts.size()==3,"model target follows the measured box");
    Check(std::find(host.painted.begin(),host.painted.end(),11)!=host.painted.end(),"model uses retained textured geometry");
    Check(host.modelOrientation,"render-target texture coordinates preserve upright model output");
    host.painted.clear(); ++host.frame;
    Check(runtime.Frame(viewport,.2) && host.calls.size()==2 && host.calls.back().lease==first.lease && !host.painted.empty(),
        "animation refreshes one live owner instead of replaying stale pixels");
    Check(runtime.SetState({{"shown",false}},error,.3),"model can be hidden through canonical state");
    Check(runtime.Frame(viewport,.3) && host.calls.size()==2,"hidden scene submits no world view");
    Check(runtime.SetState({{"shown",true},{"body",std::string("player_strogg_ui")}},error,.4),"model can change while hidden");
    viewport.displayScale=2;
    Check(runtime.Frame(viewport,.4) && host.calls.back().width==640 && host.calls.back().height==400 &&
        host.calls.back().scene.parts[0].model=="player_strogg_ui","new asset and projected density reach the same live scene");
    runtime.SetReducedMotion(true,.5);
    Check(runtime.Frame(viewport,.5) && host.calls.back().seconds==0,"reduced motion holds model animation without suppressing state edits");
    host.refuse=true; host.painted.clear(); ++host.frame;
    Check(!runtime.Frame(viewport,.6) && host.painted.empty(),"failed target preparation leaves all UI paint to fallback");
    runtime.CloseDocument();
    Check(host.released==std::vector<std::uint64_t>{first.lease},"document close retires the exact model owner once");
    host.refuse=false;
    Check(runtime.LoadDocument(Encode(Source()),"again.rui",diagnostics) && runtime.Frame(viewport,.7) &&
        host.calls.back().lease!=first.lease,"reopening never reuses a retired model owner");
    runtime.Shutdown();
    Check(host.released.size()==2 && host.released[0]!=host.released[1],"shutdown releases the remaining scene");
}
int main() {
    SchemaAndTransactions(); ActualRuntime();
    std::printf("retained model scene: %u checks passed\n",checks);
}
