// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "src/ui/retained/Runtime.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <fstream>
#include <iterator>
#include "src/ui/retained/Input.h"

using namespace openq4::ui;
static void Check(bool condition, const char* message) {
	if (!condition) { std::fprintf(stderr,"FAIL: %s\n",message); std::exit(1); }
}
static bool Near(float a, float b) { return std::abs(a-b) < .1f; }
struct TestHost final : Host {
	int drawCalls = 0, errors = 0;
	std::vector<Vertex> drawn;
	struct Layer { std::vector<Vertex> vertices; std::vector<Vertex> pixels; };
	std::vector<Layer> layers{1};
	std::uint32_t activeLayer = 0;
	std::vector<Bounds> samplePoints;
	bool failLayer = false;
	int allocationsUntilFailure = -1;
	static void Over(Vertex& destination, const Vertex& source, float opacity = 1) {
		const float remain = 1-source.a*opacity;
		destination.r = source.r*opacity+destination.r*remain;
		destination.g = source.g*opacity+destination.g*remain;
		destination.b = source.b*opacity+destination.b*remain;
		destination.a = source.a*opacity+destination.a*remain;
	}
	void ClearSamples() { layers[0].pixels.assign(samplePoints.size(),Vertex{0,0,0,0,0,0,0,0}); drawn.clear(); }
	bool ReadFile(const std::string&, std::string&) override { return false; }
	std::string Translate(const std::string& s) override { return s == "#str_test" ? "Localised" : s; }
	void Log(bool error, const std::string& s) override { if (error) { ++errors; std::fprintf(stderr,"RmlUi: %s\n",s.c_str()); } }
	std::uintptr_t LoadMaterial(const std::string&, int& w, int& h) override { w=h=256; return 1; }
	void Draw(const std::vector<Vertex>& vertices, const std::vector<int>& indices, std::uintptr_t) override {
		++drawCalls;
		Check(indices.size()%3==0,"triangle topology");
		for (int index : indices) Check(index>=0 && static_cast<size_t>(index)<vertices.size(),"valid indices");
		for (const auto& v : vertices) Check(std::isfinite(v.x)&&std::isfinite(v.y),"finite output");
		auto& output = activeLayer ? layers[activeLayer].vertices : drawn;
		output.insert(output.end(),vertices.begin(),vertices.end());
		// Independently evaluate triangle interiors at selected off-edge points,
		// then source-over into the current transparent layer. This checks actual
		// overlap colors, separately from the flattened geometry trace above.
		for (size_t p = 0; p < samplePoints.size(); ++p) for (size_t i = 0; i < indices.size(); i += 3) {
			const auto& a = vertices[indices[i]]; const auto& b = vertices[indices[i+1]]; const auto& c = vertices[indices[i+2]];
			const double px = samplePoints[p].x, py = samplePoints[p].y;
			const double determinant = (b.y-c.y)*(a.x-c.x)+(c.x-b.x)*(a.y-c.y);
			if (std::abs(determinant) < 1e-12) continue;
			const double u = ((b.y-c.y)*(px-c.x)+(c.x-b.x)*(py-c.y))/determinant;
			const double v = ((c.y-a.y)*(px-c.x)+(a.x-c.x)*(py-c.y))/determinant;
			const double w = 1-u-v;
			if (u > 0 && v > 0 && w > 0) {
				Vertex source{0,0,0,0,float(u*a.r+v*b.r+w*c.r),float(u*a.g+v*b.g+w*c.g),float(u*a.b+v*b.b+w*c.b),float(u*a.a+v*b.a+w*c.a)};
				Over(layers[activeLayer].pixels[p],source);
			}
		}
	}
	bool BeginLayer(std::uint32_t id, int, int) override {
		if (failLayer || allocationsUntilFailure == 0) return false;
		if (allocationsUntilFailure > 0) --allocationsUntilFailure;
		if (layers.size() <= id) layers.resize(id+1);
		layers[id] = {}; layers[id].pixels.assign(samplePoints.size(),Vertex{0,0,0,0,0,0,0,0}); activeLayer = id; return true;
	}
	void CompositeLayer(std::uint32_t source, std::uint32_t destination, float opacity, const Bounds& clip) override {
		Check(source != destination,"composition never samples its destination");
		activeLayer = destination;
		auto& output = destination ? layers[destination].vertices : drawn;
		for (auto v : layers[source].vertices) { v.r *= opacity; v.g *= opacity; v.b *= opacity; v.a *= opacity; output.push_back(v); }
		for (size_t i = 0; i < samplePoints.size(); ++i) {
			const auto& p = samplePoints[i];
			if (p.x >= clip.x && p.x < clip.x+clip.width && p.y >= clip.y && p.y < clip.y+clip.height) Over(layers[destination].pixels[i],layers[source].pixels[i],opacity);
		}
	}
	void MaskLayer(std::uint32_t mask, std::uint32_t destination, const Bounds& clip) override {
		Check(mask && destination && mask != destination,"mask never samples its destination or base");
		activeLayer = destination;
		for (size_t i = 0; i < samplePoints.size(); ++i) {
			const auto& p = samplePoints[i];
			if (p.x >= clip.x && p.x < clip.x+clip.width && p.y >= clip.y && p.y < clip.y+clip.height) {
				auto& pixel = layers[destination].pixels[i]; const float alpha = layers[mask].pixels[i].a;
				pixel.r *= alpha; pixel.g *= alpha; pixel.b *= alpha; pixel.a *= alpha;
			}
		}
	}
	void EndLayer(std::uint32_t restore) override { activeLayer = restore; }
	FontMetrics GetFontMetrics(const std::string&, int size) override { return {size*.8f,size*.2f,size*1.2f,size*.5f}; }
	Glyph GetGlyph(const std::string&, int size, std::uint32_t) override {
		return {size*.6f,0,-size*.8f,size*.6f,static_cast<float>(size),0,0,1,1,"test-font"};
	}
};

int main(int argc, char** argv) {
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
	const char* canonical = R"json({
	 "format":"openq4-ui","version":1,"id":"canonical-test",
	 "root":{"id":"root","type":"group","children":[
	  {"id":"canonical-panel","type":"group","properties":{
	   "position":{"type":"keyword","value":"absolute"},
	   "left":{"type":"length","value":10,"unit":"dp"},
	   "top":{"type":"length","value":20,"unit":"dp"},
	   "width":{"type":"length","value":100,"unit":"dp"},
	   "height":{"type":"length","value":40,"unit":"dp"},
	   "background-color":{"type":"color","value":[0.5,0.6,0.3,1]},
	   "transform":{"type":"transform","unit":"dp","value":[0,0,1,1,0]}
	  }}
	 ]},
	 "timelines":[{"id":"slide","durationMs":1000,"tracks":[
	  {"node":"canonical-panel","property":"transform","keys":[
	   {"atMs":0,"value":{"type":"transform","unit":"dp","value":[0,0,1,1,0]}},
	   {"atMs":1000,"value":{"type":"transform","unit":"dp","value":[100,0,1,1,0]}}
	  ]}
	 ]}]
	})json";
	std::vector<Diagnostic> diagnostics;
	Check(runtime.LoadDocument(canonical,"test.q4ui",diagnostics),"load canonical document through real layout adapter");
	Check(!runtime.SetProperty("canonical-panel","left","30dp"),"canonical source retains property ownership");
	Check(runtime.PlayTimeline("slide",6),"play canonical absolute timeline");
	Check(Near(leftAt(6),10),"canonical initial transform reaches renderer");
	Check(!host.drawn.empty() && host.drawn[0].a > .99f && Near(host.drawn[0].r,.5f),"normalized canonical colour converts to RmlUi alpha units");
	Check(Near(leftAt(6.25),35),"canonical renderer advances full 250 ms after a stall");
	viewport.displayScale=2;
	Check(Near(leftAt(6.5),120),"canonical transform and layout share dp scale during animation");
	Check(Near(leftAt(7),220),"canonical transform reaches exact final endpoint");
	Check(!runtime.LoadDocument("{\"version\":2}","bad.q4ui",diagnostics),"reject invalid replacement document");
	Check(Near(leftAt(7.25),220),"invalid replacement keeps previous rendered document");
	const char* vectorDocument = R"json({"format":"openq4-ui","version":1,"id":"vector-render-test",
	 "root":{"id":"clip","type":"group","properties":{
	  "position":{"type":"keyword","value":"absolute"},"overflow":{"type":"keyword","value":"hidden"},
	  "left":{"type":"length","value":100,"unit":"dp"},"top":{"type":"length","value":100,"unit":"dp"},
	  "width":{"type":"length","value":100,"unit":"dp"},"height":{"type":"length","value":100,"unit":"dp"},
	  "opacity":{"type":"number","value":0.5}
	 },"children":[{"id":"shape","type":"vector","properties":{
	  "width":{"type":"length","value":200,"unit":"dp"},"height":{"type":"length","value":100,"unit":"dp"},
	  "opacity":{"type":"number","value":0.5},"transform":{"type":"transform","unit":"px","value":[0,0,1,1,0]}
	 },"paths":[{"id":"rectangle","fill":{"type":"solid","color":{"type":"color","value":[0.8,0.4,0.2,0.8]}},
	  "commands":[{"id":"p0","op":"move","points":[[0,0]]},{"id":"p1","op":"line","points":[[{"fraction":1},0]]},
	   {"id":"p2","op":"line","points":[[{"fraction":1},{"fraction":1}]]},{"id":"p3","op":"line","points":[[0,{"fraction":1}]]},
	   {"id":"close","op":"close"}]}]}]},
	 "timelines":[
	  {"id":"fade","durationMs":1000,"tracks":[{"node":"shape","property":"opacity","keys":[{"atMs":0,"value":{"type":"number","value":0.5}},{"atMs":1000,"value":{"type":"number","value":0.25}}]}]},
	  {"id":"move","durationMs":1000,"tracks":[{"node":"shape","property":"transform","keys":[{"atMs":0,"value":{"type":"transform","unit":"px","value":[0,0,1,1,0]}},{"atMs":1000,"value":{"type":"transform","unit":"px","value":[10,0,1,1,0]}}]}]},
	  {"id":"fractional","durationMs":1000,"tracks":[{"node":"shape","property":"transform","keys":[{"atMs":0,"value":{"type":"transform","unit":"px","value":[10,0,1,1,0]}},{"atMs":1000,"value":{"type":"transform","unit":"px","value":[10.5,0,1,1,0]}}]}]}
	 ]})json";
	Check(runtime.LoadDocument(vectorDocument,"vector.q4ui",diagnostics),"load native vector element into retained tree");
	for (int density : {1,2}) {
		viewport.displayScale = static_cast<float>(density);
		host.drawn.clear(); runtime.Frame(viewport,8+density);
		Check(!host.drawn.empty(),"native path geometry reaches host through retained renderer");
		for (const auto& v : host.drawn) {
			Check(v.x >= 100*density-.1f && v.x <= 200*density+.1f && v.y >= 100*density-.1f && v.y <= 200*density+.1f,"native vectors inherit clipping with one DPI transform");
			Check(std::abs(v.a-.2f)<.01f && std::abs(v.r-.16f)<.01f,"vector paint alpha and inherited opacity are each applied once");
		}
	}
	runtime.Frame(viewport,11);
	Check(runtime.Statistics().vectorPathsCompiled == 0 && runtime.Statistics().vectorUploads == 0,"stationary frame reuses vector geometry");
	Check(runtime.PlayTimeline("fade",11),"start opacity cache regression");
	host.drawn.clear(); runtime.Frame(viewport,11.5);
	Check(runtime.Statistics().vectorPathsCompiled == 0 && runtime.Statistics().vectorUploads == 0,"isolated opacity reuses both coverage and vertex tint");
	Check(runtime.Statistics().layerComposites == 2 && runtime.Statistics().peakLayerDepth == 2,"parent and child opacity use nested composition");
	for (const auto& v : host.drawn) Check(std::abs(v.a-.15f)<.01f,"cached paint receives current animated opacity");
	runtime.Frame(viewport,12);
	Check(runtime.PlayTimeline("move",12),"start integer movement cache regression");
	runtime.Frame(viewport,12.1); // May enter another padded coverage region.
	host.drawn.clear(); runtime.Frame(viewport,12.2);
	Check(runtime.Statistics().vectorPathsCompiled == 0 && runtime.Statistics().vectorUploads == 0,"whole-pixel movement reuses coverage and tint");
	for (const auto& v : host.drawn) Check(v.x >= 202-.001f && v.x <= 400+.001f,"cached movement retains exact live clipping");
	runtime.Frame(viewport,13);
	Check(runtime.PlayTimeline("fractional",13),"start fractional coverage cache regression");
	host.drawn.clear(); runtime.Frame(viewport,13.5);
	Check(runtime.Statistics().vectorPathsCompiled == 1,"fractional phase changes rebuild coverage");
	const auto cachedDraw = host.drawn;
	Document direct;
	Check(direct.Load(vectorDocument,diagnostics),"load fresh reference for animated cached geometry");
	Check(direct.ReplaceValue("/root/children/0/properties/opacity/value","0.25",diagnostics),"set final reference opacity");
	Check(direct.ReplaceValue("/root/children/0/properties/transform/value","[10.25,0,1,1,0]",diagnostics),"set final reference position");
	Check(runtime.LoadDocument(direct.Source(),"fresh-vector.q4ui",diagnostics),"load fresh renderer reference");
	host.drawn.clear(); runtime.Frame(viewport,14);
	Check(host.drawn.size() == cachedDraw.size(),"fresh and cached vector submission topology agrees");
	for (size_t i = 0; i < host.drawn.size(); ++i) {
		const auto& a = host.drawn[i]; const auto& b = cachedDraw[i];
		Check(std::abs(a.x-b.x)<.0001f && std::abs(a.y-b.y)<.0001f && std::abs(a.r-b.r)<.0001f && std::abs(a.a-b.a)<.0001f,"fresh compilation matches animated cached pixels and premultiplied paint");
	}
	const char* compositionDocument = R"json({"format":"openq4-ui","version":1,"id":"composition",
	 "root":{"id":"root","type":"group","properties":{
	  "width":{"type":"length","value":100,"unit":"%"},"height":{"type":"length","value":100,"unit":"%"},
	  "background-color":{"type":"color","value":[0,1,0,1]}},"children":[
	  {"id":"fade-group","type":"group","properties":{
	   "position":{"type":"keyword","value":"absolute"},"left":{"type":"length","value":10,"unit":"px"},"top":{"type":"length","value":10,"unit":"px"},
	   "width":{"type":"length","value":120,"unit":"px"},"height":{"type":"length","value":80,"unit":"px"},
	   "opacity":{"type":"number","value":0.5},"background-color":{"type":"color","value":[1,0,0,1]}},"children":[
	   {"id":"nested","type":"group","properties":{
	    "position":{"type":"keyword","value":"absolute"},"left":{"type":"length","value":30,"unit":"px"},"top":{"type":"length","value":0,"unit":"px"},
	    "width":{"type":"length","value":80,"unit":"px"},"height":{"type":"length","value":80,"unit":"px"},
	    "opacity":{"type":"number","value":0.5},"background-color":{"type":"color","value":[0,0,1,1]}}}
	  ]}
	 ]},"timelines":[{"id":"fade","durationMs":1000,"tracks":[{"node":"fade-group","property":"opacity","keys":[
	 {"atMs":0,"value":{"type":"number","value":0.5}},{"atMs":1000,"value":{"type":"number","value":0}}]}]}]})json";
	Check(runtime.LoadDocument(compositionDocument,"composition.q4ui",diagnostics),"load nested isolated opacity fixture");
	host.samplePoints = {{20.37f,20.63f},{50.37f,20.63f},{200.37f,20.63f}};
	auto sampleFrame = [&](double time) { host.ClearSamples(); runtime.Frame(viewport,time); };
	auto pixel = [&](size_t index, float r, float g, float b) {
		const auto& v = host.layers[0].pixels[index];
		Check(std::abs(v.r-r)<.002f && std::abs(v.g-g)<.002f && std::abs(v.b-b)<.002f && std::abs(v.a-1)<.002f,"isolated source-over matches independent overlap color");
	};
	sampleFrame(15); pixel(0,.5f,.5f,0); pixel(1,.25f,.5f,.25f); pixel(2,0,1,0);
	Check(runtime.PlayTimeline("fade",15),"animate isolated parent opacity");
	sampleFrame(15.5); pixel(0,.25f,.75f,0); pixel(1,.125f,.75f,.125f); pixel(2,0,1,0);
	host.failLayer = true; sampleFrame(15.6);
	Check(host.errors == 1 && host.activeLayer == 0,"allocation failure diagnosed and output target restored");
	--host.errors; host.failLayer = false;
	sampleFrame(15.5); // Monotonic clock keeps the already advanced .6 sample.
	pixel(0,.2f,.8f,0); pixel(1,.1f,.8f,.1f);
	sampleFrame(16); pixel(0,0,1,0); pixel(1,0,1,0);
	// The same overlapping groups, now masked at both levels. Red and black
	// masks with equal alpha must behave alike: this is alpha, not luminance.
	std::string maskedDocument = compositionDocument;
	maskedDocument.insert(maskedDocument.find("\"id\":\"fade-group\""),R"json("mask":{"paths":[
	 {"id":"chamfer-hole","fillRule":"evenodd","fill":{"type":"solid","color":{"type":"color","value":[1,0,0,0.5]}},"commands":[
	  {"id":"a","op":"move","points":[[0,20]]},{"id":"b","op":"line","points":[[20,0]]},
	  {"id":"c","op":"line","points":[[120,0]]},{"id":"d","op":"line","points":[[120,80]]},
	  {"id":"e","op":"line","points":[[0,80]]},{"id":"f","op":"close"},
	  {"id":"g","op":"move","points":[[50,30]]},{"id":"h","op":"line","points":[[70,30]]},
	  {"id":"i","op":"line","points":[[70,50]]},{"id":"j","op":"line","points":[[50,50]]},{"id":"k","op":"close"}]
	 }]},)json");
	maskedDocument.insert(maskedDocument.find("\"id\":\"nested\""),R"json("mask":{"paths":[
	 {"id":"half","fill":{"type":"solid","color":{"type":"color","value":[0,0,0,0.5]}},"commands":[
	  {"id":"a","op":"move","points":[[0,0]]},{"id":"b","op":"line","points":[[80,0]]},
	  {"id":"c","op":"line","points":[[80,80]]},{"id":"d","op":"line","points":[[0,80]]},{"id":"e","op":"close"}]
	 }]},)json");
	Check(runtime.LoadDocument(maskedDocument,"masked.q4ui",diagnostics),"load nested alpha masks with a chamfer and hole");
	viewport = {}; // These mask coordinates are dp; paint coordinates are px.
	host.samplePoints = {{20.37f,20.63f},{50.37f,20.63f},{70.37f,50.63f},{12.37f,12.63f},{19.37f,20.63f}};
	sampleFrame(17);
	const float maskAlpha = 128.f/255;
	pixel(0,.5f*maskAlpha,1-.5f*maskAlpha,0);
	pixel(1,(1-.5f*maskAlpha)*.5f*maskAlpha,1-.5f*maskAlpha,.25f*maskAlpha*maskAlpha);
	pixel(2,0,1,0); pixel(3,0,1,0); pixel(4,32.f/255,1-32.f/255,0);
	Check(runtime.Statistics().maskSnapshots == 2 && runtime.Statistics().maskApplications == 2,"both masks reach isolated subtree composition");
	Check(runtime.Statistics().peakLayerTargets <= 4,"snapshots and scratch reuse a bounded slot pool");
	sampleFrame(17.1);
	Check(runtime.Statistics().vectorPathsCompiled == 0 && runtime.Statistics().vectorUploads == 0,"unchanged mask coverage stays cached");
	Check(runtime.PlayTimeline("fade",17.1),"animate opacity independently of alpha mask");
	sampleFrame(17.6); pixel(0,.25f*maskAlpha,1-.25f*maskAlpha,0); pixel(2,0,1,0);
	Check(runtime.Statistics().vectorPathsCompiled == 0,"fades do not rebuild mask coverage");
	Check(runtime.LoadDocument(maskedDocument,"mask-failure.q4ui",diagnostics),"restore authored mask opacity");
	for (int allocation = 0; allocation < 8; ++allocation) {
		host.allocationsUntilFailure = allocation;
		sampleFrame(17.7+allocation*.02);
		Check(host.errors == 1 && host.activeLayer == 0,"every mask allocation failure is diagnosed and restores base");
		pixel(0,0,1,0); pixel(1,0,1,0);
		--host.errors; host.allocationsUntilFailure = -1;
		sampleFrame(17.71+allocation*.02);
		pixel(0,.5f*maskAlpha,1-.5f*maskAlpha,0);
		Check(runtime.Statistics().peakLayerTargets <= 4,"failed mask allocations do not leak leases across frames");
	}
	Document maskEdit;
	Check(maskEdit.Load(maskedDocument,diagnostics),"load editable masked source");
	Check(maskEdit.Source() == maskedDocument,"mask source round trip preserves every byte");
	const std::string gradient = R"json({"type":"linear","from":[0,0],"to":[120,0],"stops":[
	 {"at":0,"color":{"type":"color","value":[1,0,0,0]}},
	 {"at":1,"color":{"type":"color","value":[0,0,0,1]}}]})json";
	Check(maskEdit.ReplaceValue("/root/children/0/mask/paths/0/fill",gradient,diagnostics),"edit mask paint to a soft reveal gradient");
	Check(runtime.LoadDocument(maskEdit.Source(),"gradient-mask.q4ui",diagnostics),"load gradient alpha mask");
	sampleFrame(17.9);
	const float gradientAlpha = (20.37f-10)/120;
	pixel(0,.5f*gradientAlpha,1-.5f*gradientAlpha,0); pixel(2,0,1,0);
	Check(maskEdit.ReplaceValue("/root/children/0/mask/paths","[]",diagnostics),"author an empty mask");
	Check(runtime.LoadDocument(maskEdit.Source(),"empty-mask.q4ui",diagnostics),"load explicitly empty mask");
	sampleFrame(18); pixel(0,0,1,0); pixel(1,0,1,0);
	Check(maskEdit.ReplaceValue("/root/children/0/mask",R"json({"paths":[{"id":"invalid","commands":[]}]})json",diagnostics) == false,"invalid mask edit is transactional");
	Check(diagnostics[0].pointer == "/root/children/0/mask/paths/0/commands","mask diagnostic identifies the source command array");
	std::string dpMasked = maskedDocument;
	for (size_t pos = 0; (pos = dpMasked.find("\"px\"",pos)) != std::string::npos; pos += 4) dpMasked.replace(pos,4,"\"dp\"");
	Check(runtime.LoadDocument(dpMasked,"mask-density.q4ui",diagnostics),"mask and contents share dp coordinates");
	for (float density : {1.25f,1.5f,2.f,1.f}) {
		viewport.displayScale = density;
		host.samplePoints = {{20.37f*density,20.63f*density},{50.37f*density,20.63f*density},{70.37f*density,50.63f*density},{12.37f*density,12.63f*density}};
		sampleFrame(19);
		pixel(0,.5f*maskAlpha,1-.5f*maskAlpha,0);
		pixel(1,(1-.5f*maskAlpha)*.5f*maskAlpha,1-.5f*maskAlpha,.25f*maskAlpha*maskAlpha);
		pixel(2,0,1,0); pixel(3,0,1,0);
	}
	host.samplePoints.clear();
	std::ifstream interactionFile(argc > 1 ? argv[1] : "tools/ui/fixtures/interaction-smoke.q4ui",std::ios::binary);
	Check(interactionFile.good(),"open canonical interaction qualification fixture");
	const std::string interactionSource((std::istreambuf_iterator<char>(interactionFile)),std::istreambuf_iterator<char>());
	Check(runtime.LoadDocument(interactionSource,"interaction.q4ui",diagnostics),"load semantic buttons and visual-state timelines");
	viewport = {}; runtime.Frame(viewport,20);
	auto menu = [&](MenuInput action, bool down = true) { runtime.MenuAction(action,down,20); };
	menu(MenuInput::Next); Check(runtime.FocusedControl() == "reference-controls","source-order navigation chooses the first eligible button");
	runtime.FocusControl("",20); menu(MenuInput::Previous); Check(runtime.FocusedControl() == "modal-system","reverse tab without focus starts at the last eligible control");
	menu(MenuInput::Next); Check(runtime.FocusedControl() == "reference-controls","forward tab wraps through document order");
	menu(MenuInput::Next); Check(runtime.FocusedControl() == "reference-system","tab skips the authored disabled button");
	menu(MenuInput::Up); Check(runtime.FocusedControl() == "reference-controls","spatial navigation stays in the nearest column");
	menu(MenuInput::Accept); menu(MenuInput::Accept);
	Check(runtime.GetControlState("reference-controls") == ControlState::Pressed && runtime.TakeActions().empty(),"accept-down owns pressed feedback without activation or repeats");
	menu(MenuInput::Accept,false); menu(MenuInput::Accept,false);
	auto actions = runtime.TakeActions();
	Check(actions.size() == 1 && actions[0].node == "reference-controls" && actions[0].action == "menu.controls" && actions[0].document == "interaction-qualification","one named activation after a matching release");
	host.drawn.clear();
	runtime.Frame(viewport,20.5);
	bool focusRail = false;
	for (const auto& vertex : host.drawn) if (vertex.r > .99f && vertex.g > .57f && vertex.g < .59f && vertex.b < .01f && vertex.a > .99f) focusRail = true;
	Check(focusRail,"focus timeline produces an opaque orange vector rail in the actual renderer");
	menu(MenuInput::Accept); menu(MenuInput::Down); menu(MenuInput::Accept,false);
	Check(runtime.TakeActions().empty(),"navigation cancels an armed activation instead of releasing onto the next button");
	Check(runtime.PushModal("modal-panel",21),"activate contained modal input scope");
	Check(runtime.FocusedControl() == "modal-controls","modal chooses its first eligible control");
	Check(!runtime.FocusControl("reference-controls",21),"modal rejects focus outside its subtree");
	menu(MenuInput::Previous); Check(runtime.FocusedControl() == "modal-system","modal tab wraps within its own controls");
	menu(MenuInput::Accept);
	Check(runtime.PopModal(21),"close modal scope");
	Check(runtime.FocusedControl() == "reference-system","modal close restores the previous eligible focus");
	menu(MenuInput::Accept,false); Check(runtime.TakeActions().empty(),"modal teardown prevents release click-through");
	menu(MenuInput::Back); menu(MenuInput::Back); menu(MenuInput::Back,false);
	actions = runtime.TakeActions(); Check(actions.size() == 1 && actions[0].kind == ControlAction::Kind::Back,"back requests are semantic and repeated key-downs cannot duplicate them");
	Check(runtime.SetControlEnabled("reference-system",false,21),"disable control through instance state");
	Check(runtime.FocusedControl().empty() && runtime.GetControlState("reference-system") == ControlState::Disabled,"disabled controls drop focus immediately");
	viewport.displayScale = 1.25f; viewport.pixelDensityX = viewport.pixelDensityY = 2; viewport.originX = 30; viewport.originY = 20;
	runtime.Frame(viewport,22);
	Bounds controlBounds; runtime.GetBounds("reference-controls",controlBounds);
	const float hitX = controlBounds.x+controlBounds.width*.5f, hitY = controlBounds.y+controlBounds.height*.5f;
	runtime.PointerMove((hitX+30)/2,(hitY+20)/2,22);
	Check(runtime.GetControlState("reference-controls") == ControlState::Hover,"window input density and viewport origin map to the rendered control once");
	runtime.PointerButton(true,22); runtime.PointerButton(true,22); runtime.PointerButton(false,22); runtime.PointerButton(false,22);
	actions = runtime.TakeActions(); Check(actions.size() == 1 && actions[0].node == "reference-controls","pointer press/release activates the containing button through its label");
	runtime.PointerButton(true,22); runtime.PointerMove(-100,-100,22); runtime.PointerButton(false,22);
	Check(runtime.TakeActions().empty(),"dragging off a pressed button cancels activation");
	runtime.MenuAction(MenuInput::Accept,true,22); runtime.MenuAction(MenuInput::Accept,false,22);
	runtime.MenuAction(MenuInput::Accept,true,22);
	Check(runtime.LoadDocument(interactionSource,"replacement.q4ui",diagnostics),"replace the document while accept is held");
	runtime.Frame(viewport,23); runtime.FocusControl("reference-controls",23);
	runtime.MenuAction(MenuInput::Accept,true,23); runtime.MenuAction(MenuInput::Accept,false,23);
	Check(runtime.TakeActions().empty(),"held input and repeats cannot activate a replacement document");
	runtime.MenuAction(MenuInput::Accept,true,23); runtime.CancelInput(23); runtime.MenuAction(MenuInput::Accept,false,23);
	Check(runtime.TakeActions().empty(),"input cancellation disarms a pending release");
	Check(!runtime.GetControlState("missing"),"unknown control queries remain explicit");
	runtime.FocusControl("",23);
	runtime.PointerMove((hitX+30)/2,(hitY+20)/2,23);
	if (runtime.GetControlState("reference-controls") != ControlState::Hover) {
		Bounds latest; runtime.GetBounds("reference-controls",latest);
		std::fprintf(stderr,"Hover diagnostic: point %.3f %.3f, control %.3f %.3f %.3f %.3f, state %d focus %s\n",hitX,hitY,latest.x,latest.y,latest.width,latest.height,int(*runtime.GetControlState("reference-controls")),runtime.FocusedControl().c_str());
	}
	Check(runtime.GetControlState("reference-controls") == ControlState::Hover,"stationary pointer begins over the control");
	viewport.originX = 10000; runtime.Frame(viewport,24);
	Check(runtime.GetControlState("reference-controls") == ControlState::Default,"viewport change reprojects stationary window input");
	viewport.originX = 30; runtime.Frame(viewport,24.1);
	Check(runtime.GetControlState("reference-controls") == ControlState::Hover,"restored viewport recovers hover without moving a device");
	Document interactionEdit;
	Check(interactionEdit.Load(interactionSource,diagnostics),"load editable control schema");
	Check(!interactionEdit.ReplaceValue("/root/children/0/children/1/control/action","\"quit; exec file\"",diagnostics),"actions are semantic IDs, not executable command text");
	Check(!interactionEdit.ReplaceValue("/root/children/0/children/1/control/states/focus","\"missing\"",diagnostics),"reject missing state feedback timeline");
	Check(!interactionEdit.ReplaceValue("/root/children/0/children/1/control/label","\"Hardcoded\"",diagnostics),"control labels remain localized");
	Check(!interactionEdit.ReplaceValue("/root/children/0/children/1/control/states/focus","\"modal-controls.focus\"",diagnostics),"feedback cannot animate a different control's subtree");
	Check(!interactionEdit.ReplaceValue("/timelines/1/tracks",R"json([{"node":"reference-controls-focus","property":"opacity","keys":[
	 {"atMs":0,"value":{"type":"number","value":0}},{"atMs":60,"value":{"type":"number","value":1}}]}])json",diagnostics),"incomplete feedback cannot leave pressed properties stuck on another state");
	Check(interactionEdit.Source() == interactionSource,"failed control edits preserve exact canonical source");
	Check(interactionEdit.ReplaceValue("/root/children/0/properties/transform/value","[30,0,1,1,25]",diagnostics),"edit the control ancestor's transform");
	Check(runtime.LoadDocument(interactionEdit.Source(),"rotated-controls.q4ui",diagnostics),"load rotated control hierarchy");
	viewport = {}; runtime.Frame(viewport,25);
	Bounds parentBounds; runtime.GetBounds("reference-panel",parentBounds); runtime.GetBounds("reference-controls",controlBounds);
	const double radians = 25*3.141592653589793/180;
	const double cx = parentBounds.x+parentBounds.width*.5, cy = parentBounds.y+parentBounds.height*.5;
	const double dx = controlBounds.x+controlBounds.width*.5-cx, dy = controlBounds.y+controlBounds.height*.5-cy;
	runtime.PointerMove(float(cx+30+dx*std::cos(radians)-dy*std::sin(radians)),float(cy+dx*std::sin(radians)+dy*std::cos(radians)),25);
	runtime.PointerButton(true,25); runtime.PointerButton(false,25);
	actions = runtime.TakeActions(); Check(actions.size() == 1 && actions[0].node == "reference-controls","pointer hit testing inverts the presented ancestor transform");
	runtime.FocusControl("reference-controls",25);
	runtime.MenuAction(MenuInput::Accept,true,25);
	viewport.width = 0; runtime.Frame(viewport,26); runtime.MenuAction(MenuInput::Accept,false,26);
	Check(runtime.TakeActions().empty() && runtime.FocusedControl().empty(),"invalid/minimized output disables stale hit targets and disarms input");
	Check(runtime.LoadDocument(interactionSource,"input-aggregation.q4ui",diagnostics),"load button document for platform-source aggregation");
	viewport = {}; runtime.Frame(viewport,30); runtime.FocusControl("reference-controls",30);
	Input routedInput;
	double inputTime = 30;
	auto route = [&]() {
		for (const auto& event : routedInput.Take()) {
			if (event.kind == RoutedInput::Kind::Cancel) runtime.CancelInput(inputTime);
			else if (event.kind == RoutedInput::Kind::PointerButton) runtime.PointerButton(event.down,inputTime);
			else runtime.MenuAction(event.menu,event.down,inputTime);
		}
	};
	auto button = [&](unsigned source, MenuInput action, bool down, bool repeated = false) {
		routedInput.Menu(source,action,down,repeated,inputTime); route();
	};
	button(10,MenuInput::Accept,true); button(11,MenuInput::Accept,true);
	button(10,MenuInput::Accept,false);
	Check(runtime.GetControlState("reference-controls") == ControlState::Pressed && runtime.TakeActions().empty(),"releasing one of two accept sources cannot release the aggregate press");
	button(11,MenuInput::Accept,false);
	Check(runtime.TakeActions().size() == 1,"the last matching accept source releases exactly one activation");
	button(99,MenuInput::Accept,true,true); button(99,MenuInput::Accept,false);
	Check(runtime.TakeActions().empty(),"an orphan OS repeat cannot arm a newly opened menu");
	button(1,MenuInput::Next,true);
	Check(runtime.FocusedControl() == "reference-system","a fresh navigation source moves immediately");
	routedInput.Advance(30.319); route();
	Check(runtime.FocusedControl() == "reference-system","navigation repeat waits for its initial delay");
	inputTime = 30.321; routedInput.Advance(inputTime); route();
	Check(runtime.FocusedControl() == "modal-controls","navigation repeats independently of OS key repeat");
	inputTime = 100; routedInput.Advance(inputTime); route();
	Check(runtime.FocusedControl() == "modal-game-options","a long presentation stall produces at most one navigation repeat");
	button(1,MenuInput::Next,true,true);
	Check(runtime.FocusedControl() == "modal-game-options","OS repeat cannot duplicate the presentation-clock repeat");
	button(1,MenuInput::Previous,false);
	inputTime = 101; routedInput.Advance(inputTime); route();
	Check(runtime.FocusedControl() == "modal-game-options","a modifier change releases the original key-down action");
	runtime.FocusControl("reference-controls",inputTime);
	button(10,MenuInput::Accept,true);
	routedInput.Cancel(); route();
	Check(runtime.TakeActions().empty(),"source cancellation disarms before releasing logical inputs");
	Check(runtime.LoadDocument(interactionSource,"input-replacement.q4ui",diagnostics),"replace a document with physical input held");
	runtime.Frame(viewport,102); runtime.FocusControl("reference-controls",102); inputTime = 102;
	button(10,MenuInput::Accept,true); button(10,MenuInput::Accept,false);
	Check(runtime.TakeActions().empty(),"a held physical source cannot reactivate after document replacement");
	button(10,MenuInput::Accept,true); button(10,MenuInput::Accept,false);
	Check(runtime.TakeActions().size() == 1,"a released physical source can arm a fresh activation");
	button(10,MenuInput::Accept,true); routedInput.Cancel(true); route();
	button(10,MenuInput::Accept,true,true); button(10,MenuInput::Accept,false);
	Check(runtime.TakeActions().empty(),"focus loss rejects orphan repeats even when release occurred outside the application");
	button(10,MenuInput::Accept,true); button(10,MenuInput::Accept,false);
	Check(runtime.TakeActions().size() == 1,"fresh non-repeat input works after focus recovery");
	button(20,MenuInput::Back,true); button(21,MenuInput::Back,true);
	actions = runtime.TakeActions();
	Check(actions.size() == 1 && actions[0].kind == ControlAction::Kind::Back,"back sources aggregate into one request");
	inputTime = 200; routedInput.Advance(inputTime); route();
	Check(runtime.TakeActions().empty(),"back and accept do not repeat on the navigation clock");
	button(20,MenuInput::Back,false); button(21,MenuInput::Back,false);
	runtime.GetBounds("reference-controls",controlBounds);
	runtime.PointerMove(controlBounds.x+controlBounds.width*.5f,controlBounds.y+controlBounds.height*.5f,inputTime);
	routedInput.Pointer(30,true,inputTime); route(); routedInput.Pointer(31,true,inputTime); route();
	routedInput.Pointer(30,false,inputTime); route();
	Check(runtime.TakeActions().empty(),"multiple primary-pointer sources share one logical held button");
	routedInput.Pointer(31,false,inputTime); route();
	Check(runtime.TakeActions().size() == 1,"last pointer source completes exactly one activation");
	runtime.MenuAction(MenuInput::Accept,true,inputTime);
	routedInput.Cancel(true); route();
	button(10,MenuInput::Accept,true); button(10,MenuInput::Accept,false);
	Check(runtime.TakeActions().size() == 1,"cancellation also releases logical arms created by a previous input adapter");
	runtime.CloseDocument();
	Check(!runtime.IsLoaded(),"close document");
	runtime.Shutdown();
	Check(runtime.Statistics().residentGeometryCount == 0 && runtime.Statistics().residentGeometryBytes == 0,"geometry accounting returns to zero after shutdown");
	Check(runtime.Initialize(),"restart lifetime without stale services");
	runtime.Shutdown();
	Check(host.errors==0,"no library warnings or errors");
	std::puts("Retained UI: density, aspect layout, ownership, mutation, clipping, motion and restart passed");
}
