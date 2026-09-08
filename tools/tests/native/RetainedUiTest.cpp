// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "src/ui/retained/Runtime.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>

using namespace openq4::ui;
static void Check(bool condition, const char* message) {
	if (!condition) { std::fprintf(stderr,"FAIL: %s\n",message); std::exit(1); }
}
static bool Near(float a, float b) { return std::abs(a-b) < .1f; }
struct TestHost final : Host {
	int drawCalls = 0, errors = 0;
	std::vector<Vertex> drawn;
	bool ReadFile(const std::string&, std::string&) override { return false; }
	std::string Translate(const std::string& s) override { return s == "#str_test" ? "Localised" : s; }
	void Log(bool error, const std::string& s) override { if (error) { ++errors; std::fprintf(stderr,"RmlUi: %s\n",s.c_str()); } }
	std::uintptr_t LoadMaterial(const std::string&, int& w, int& h) override { w=h=256; return 1; }
	void Draw(const std::vector<Vertex>& vertices, const std::vector<int>& indices, std::uintptr_t) override {
		++drawCalls;
		Check(indices.size()%3==0,"triangle topology");
		for (int index : indices) Check(index>=0 && static_cast<size_t>(index)<vertices.size(),"valid indices");
		for (const auto& v : vertices) Check(std::isfinite(v.x)&&std::isfinite(v.y),"finite output");
		drawn.insert(drawn.end(),vertices.begin(),vertices.end());
	}
	FontMetrics GetFontMetrics(const std::string&, int size) override { return {size*.8f,size*.2f,size*1.2f,size*.5f}; }
	Glyph GetGlyph(const std::string&, int size, std::uint32_t) override {
		return {size*.6f,0,-size*.8f,size*.6f,static_cast<float>(size),0,0,1,1,"test-font"};
	}
};

int main() {
	Viewport viewport;
	viewport.displayScale = 1.5f;
	viewport.userScale = 1.25f;
	viewport.pixelDensityX = viewport.pixelDensityY = 2;
	viewport.originX = 30; viewport.originY = 20;
	float x,y;
	viewport.WindowToDocument(100,80,x,y);
	Check(Near(x,170)&&Near(y,140),"input uses pixel density and viewport origin once");
	Check(Near(viewport.DpRatio(),1.875f),"layout scale independent of pixel density");
	viewport.displayScale = std::numeric_limits<float>::quiet_NaN();
	viewport.userScale = 0;
	Check(Near(viewport.DpRatio(),1),"invalid scales repaired");
	viewport = {};
	TestHost host;
	Runtime runtime(host);
	Check(runtime.Initialize(),"initialize real RmlUi core");
	{
		Runtime other(host);
		Check(!other.Initialize(),"reject a competing global owner");
	}
	const char* markup = R"(<rml><head><style>
		body { margin:0; width:100%; height:100%; font-family: test; font-size:20dp; }
		#panel { position:absolute; left:24dp; top:32dp; width:200dp; height:100dp; background-color:#8b964b; }
		#anchor { position:absolute; right:24dp; bottom:24dp; width:100dp; height:40dp; background-color:#e38900; }
	</style></head><body><div id="panel">#str_test</div><div id="anchor"/></body></rml>)";
	Check(runtime.LoadMarkup(markup,"test.rml"),"load retained layout");
	runtime.Frame(viewport,1);
	Bounds panel,anchor;
	Check(runtime.GetBounds("panel",panel)&&Near(panel.x,24)&&Near(panel.width,200),"dp layout at 100 percent");
	runtime.GetBounds("anchor",anchor);
	Check(Near(anchor.x,1156)&&Near(anchor.y,656),"anchor expands to 16:9");
	viewport.width=2560; viewport.height=1440; viewport.displayScale=2;
	runtime.Frame(viewport,2);
	Check(runtime.GetBounds("panel",panel)&&Near(panel.x,48)&&Near(panel.width,400),"DPI change relayouts geometry");
	Check(runtime.GetBounds("anchor",anchor)&&Near(anchor.x,2312)&&Near(anchor.y,1312),"DPI and anchors agree");
	viewport.width=3440; viewport.displayScale=1;
	runtime.Frame(viewport,3);
	Check(runtime.GetBounds("panel",panel)&&Near(panel.width,200),"ultrawide keeps component proportions");
	Check(runtime.GetBounds("anchor",anchor)&&Near(anchor.x,3316),"ultrawide adds layout space");
	Check(runtime.SetText("panel","<& Localised >"),"text mutation without markup injection");
	Check(runtime.SetProperty("panel","width","240dp"),"editable style property");
	runtime.Frame(viewport,4);
	Check(runtime.GetBounds("panel",panel)&&Near(panel.width,240),"property mutation relayouts");
	Check(host.drawCalls>0,"real retained geometry reaches host");

	// A gradient wider than its overflow clip must carry interpolated colour
	// to the clip boundary, with every emitted vertex inside the clip rectangle.
	Check(runtime.LoadMarkup(R"(<rml><head><style>
		body{margin:0;width:100%;height:100%;font-family:test;font-size:20dp;}
		div{display:block;}
		#clip{position:absolute;left:100px;top:100px;width:100px;height:100px;overflow:hidden;}
		#fill{width:200px;height:100px;decorator:horizontal-gradient(#ff0000 #0000ff);}
	</style></head><body><div id="clip"><div id="fill"/></div></body></rml>)","clip.rml"),"load clipped gradient");
	host.drawn.clear();
	runtime.Frame(viewport,5);
	bool interpolated=false;
	Check(!host.drawn.empty(),"clipped gradient emits geometry");
	for(const auto& v:host.drawn) {
		Check(v.x>=99.9f&&v.x<=200.1f&&v.y>=99.9f&&v.y<=200.1f,"scissor bounds respected");
		if(Near(v.x,200)&&v.r>.4f&&v.r<.6f&&v.b>.4f&&v.b<.6f) interpolated=true;
	}
	if (!interpolated) for (const auto& v : host.drawn) std::fprintf(stderr,"clip vertex: %.3f %.3f colour %.3f %.3f %.3f %.3f\n",v.x,v.y,v.r,v.g,v.b,v.a);
	Check(interpolated,"clipping interpolates red-blue gradient");
	Check(runtime.LoadMarkup(R"(<rml><head><style>
		body{margin:0;width:100%;height:100%;font-family:test;font-size:20dp;}
		#motion{position:absolute;left:10px;top:10px;width:20px;height:20px;background-color:#8b964b;
		 animation:1s slide linear-in;}
		@keyframes slide{from{transform:translateX(0px);}to{transform:translateX(100px);}}
	</style></head><body><div id="motion"/></body></rml>)","motion.rml"),"load continuous animation");
	auto leftAt = [&](double time) {
		host.drawn.clear(); runtime.Frame(viewport,time);
		Check(!host.drawn.empty(),"animated geometry emitted");
		float left=1e9f;
		for(const auto& v:host.drawn) if(v.x<left) left=v.x;
		return left;
	};
	Check(Near(leftAt(5),10),"animation starts at current clock");
	// RmlUi's CSS animation clock clamps gaps to 100 ms. This verifies dense
	// presentation sampling only; the canonical timeline must own stall policy.
	float animatedLeft = 10;
	for (int frame=1;frame<=36;++frame) animatedLeft=leftAt(5.0+frame/144.0);
	Check(Near(animatedLeft,35),"quarter-second transform at 144 Hz");
	for (int frame=37;frame<=72;++frame) animatedLeft=leftAt(5.0+frame/144.0);
	Check(Near(animatedLeft,60),"half-second transform at 144 Hz");
	Check(Near(leftAt(5.4),60),"backward clock does not reverse presentation");
	Check(Near(leftAt(5.5+1.0/144.0),60+100.f/144.f),"144 Hz sample advances between simulation ticks");
	runtime.CloseDocument();
	Check(!runtime.IsLoaded(),"close document");
	runtime.Shutdown();
	Check(runtime.Initialize(),"restart lifetime without stale services");
	runtime.Shutdown();
	Check(host.errors==0,"no library warnings or errors");
	std::puts("Retained UI: density, aspect layout, ownership, mutation, clipping, motion and restart passed");
}
