// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "src/ui/retained/Runtime.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <fstream>
#include <iterator>
#include "src/ui/retained/Input.h"
#include "src/ui/retained/State.h"

using namespace openq4::ui;
static void Check(bool condition, const char* message) {
	if (!condition) { std::fprintf(stderr,"FAIL: %s\n",message); std::exit(1); }
}
static bool Near(float a, float b) { return std::abs(a-b) < .1f; }
static void CheckTypedActionDescriptors() {
	const std::string source = R"json({
	 "format":"openq4-ui","version":1,"id":"application-actions",
	 "state":{
	  "brightness":{"type":"number","initial":1},
	  "shadows":{"type":"boolean","initial":true},
	  "divisor":{"type":"number","initial":0},
	  "chooseDivision":{"type":"boolean","initial":false},
	  "playerData":{"type":"string","initial":""}
	 },
	 "actions":{
	  "brighten":{"operation":"settings.brightness.set","arguments":{"value":{"op":"+","args":[{"state":"brightness"},0.25]}}},
	  "shadows":{"operation":"settings.shadows.set","arguments":{"value":{"op":"!","args":[{"state":"shadows"}]}}},
	  "lazy":{"operation":"settings.brightness.set","arguments":{"value":{"op":"select","args":[{"state":"chooseDivision"},{"op":"/","args":[1,{"state":"divisor"}]},1.25]}}},
	  "failure":{"operation":"fixture.failure","arguments":{"first":1,"last":{"op":"/","args":[1,{"state":"divisor"}]}}},
	  "overflow":{"operation":"fixture.overflow","arguments":{"value":{"op":"*","args":[1000000000000,1000000000000]}}},
	  "data":{"operation":"fixture.data","arguments":{"literal":"set r_fullscreen 1; quit","player":{"state":"playerData"}}},
	  "none":{"operation":"fixture.none","arguments":{},"extensions":{"future":{"retained":true}}}
	 },
	 "root":{"id":"root","type":"group"},
	 "extensions":{"sourceComment":"retained outside edited action"}
	})json";
	Document document; std::vector<Diagnostic> diagnostics; std::string error;
	Check(document.Load(source,diagnostics),"compile generic typed application descriptors without dispatching them");
	Check(document.Source()==source && document.Model().actions.size()==7,"action source round trip retains extensions and all descriptors");
	Check(document.Model().actions.at("brighten").arguments.at("value").type==0 &&
		document.Model().actions.at("shadows").arguments.at("value").type==1,"action argument expressions retain compiler-resolved numeric and boolean types");
	State state; Check(state.Reset(document.Model(),error),"invalid uninvoked arithmetic does not prevent document initialization");
	ActionInvocation invocation;
	Check(document.Model().ResolveAction("brighten",state.Variables(),invocation,error) &&
		invocation.action=="brighten" && invocation.operation=="settings.brightness.set" &&
		std::get<double>(invocation.arguments.at("value"))==1.25,"action resolves typed numeric value from one state snapshot");
	Check(document.Model().ResolveAction("shadows",state.Variables(),invocation,error) &&
		!std::get<bool>(invocation.arguments.at("value")),"boolean application operation resolves without numeric coercion");
	Check(document.Model().ResolveAction("lazy",state.Variables(),invocation,error) &&
		std::get<double>(invocation.arguments.at("value"))==1.25,"unused invalid action expression branches remain lazy");
	Check(state.Set({{"playerData",std::string("Player; \"quit\" <tag>")}},error),"supply application-owned string data");
	Check(document.Model().ResolveAction("data",state.Variables(),invocation,error) &&
		std::get<std::string>(invocation.arguments.at("literal"))=="set r_fullscreen 1; quit" &&
		std::get<std::string>(invocation.arguments.at("player"))=="Player; \"quit\" <tag>","argument strings remain data, without tokenization or command execution");
	Check(document.Model().ResolveAction("none",state.Variables(),invocation,error) && invocation.arguments.empty(),"argument-free descriptor replaces the complete invocation");
	const auto variables = state.Variables(); const auto revision = state.Revision();
	const ActionInvocation unchanged{"previous","previous.operation",{{"keep",std::string("unchanged")}}};
	invocation = unchanged;
	auto rejectResolution = [&](const std::string& id, const StateValues& candidate) {
		Check(!document.Model().ResolveAction(id,candidate,invocation,error) && !error.empty(),"invalid action resolution returns a diagnostic");
		Check(invocation.action==unchanged.action && invocation.operation==unchanged.operation && invocation.arguments==unchanged.arguments,
			"failed resolution preserves the entire previous invocation even after earlier arguments evaluated");
		Check(state.Variables()==variables && state.Revision()==revision,"resolving an action never mutates application state or revision");
	};
	rejectResolution("missing",variables);
	rejectResolution("failure",variables); Check(error.find("argument 'last'")!=std::string::npos,"action failure identifies its argument");
	rejectResolution("overflow",variables);
	StateValues candidate=variables; candidate.erase("brightness"); rejectResolution("brighten",candidate);
	candidate=variables; candidate["brightness"]=true; rejectResolution("brighten",candidate);
	candidate=variables; candidate["brightness"]=std::numeric_limits<double>::infinity(); rejectResolution("brighten",candidate);
	candidate=variables; candidate["brightness"]=1000000000001.0; rejectResolution("brighten",candidate);
	candidate=variables; candidate["shadows"]=1.0; rejectResolution("shadows",candidate);
	candidate=variables; candidate["playerData"]=std::string("a\0b",3); rejectResolution("data",candidate);
	candidate=variables; candidate["playerData"]=std::string(65537,'a'); rejectResolution("data",candidate);
	candidate=variables; candidate["chooseDivision"]=true; rejectResolution("lazy",candidate);
	StateValue previous=std::string("previous result");
	Check(!EvaluateStateExpression(document.Model().actions.at("failure").arguments.at("last"),variables,previous,error) &&
		std::get<std::string>(previous)=="previous result","shared expression helper is transactional on failure");
	Check(EvaluateStateExpression(document.Model().actions.at("lazy").arguments.at("value"),variables,previous,error) &&
		std::get<double>(previous)==1.25 && error.empty(),"shared expression helper preserves lazy evaluation and clears stale error");
	auto rejectSource = [&](const std::string& pointer, const std::string& value) {
		Check(!document.ReplaceValue(pointer,value,diagnostics) && !diagnostics.empty(),"invalid action schema edit diagnosed");
		Check(document.Source()==source,"invalid action schema edit preserves the last valid document");
		Check(diagnostics.front().pointer.starts_with("/actions"),"action schema diagnostic carries an actionable source pointer");
	};
	rejectSource("/actions","[]");
	rejectSource("/actions/brighten/operation","\"settings.brightness.set;quit\"");
	rejectSource("/actions/brighten/operation","false");
	rejectSource("/actions/brighten/arguments","[]");
	rejectSource("/actions/brighten/arguments","{\"bad name\":1}");
	rejectSource("/actions/brighten/arguments/value","{\"state\":\"missing\"}");
	rejectSource("/actions/brighten/arguments/value","{\"op\":\"+\",\"args\":[true,1]}");
	rejectSource("/actions/brighten/arguments/value","{\"op\":\"exec\",\"args\":[]}");
	rejectSource("/actions/brighten/arguments/value","{\"op\":\"!\",\"args\":[true,false]}");
	rejectSource("/actions/brighten","{\"operation\":\"fixture.operation\",\"arguments\":{},\"command\":\"quit\"}");
	rejectSource("/actions/brighten","{\"operation\":\"fixture.operation\"}");
	rejectSource("/actions","{\"bad action\":{\"operation\":\"fixture.operation\",\"arguments\":{}}}");
	std::string tooMany="{";
	for (int i=0;i<33;++i) tooMany+=(i?",":"")+std::string("\"arg")+std::to_string(i)+"\":0";
	rejectSource("/actions/brighten/arguments",tooMany+"}");
	tooMany="{";
	for (int i=0;i<4097;++i) tooMany+=(i?",":"")+std::string("\"action")+std::to_string(i)+"\":{\"operation\":\"fixture.operation\",\"arguments\":{}}";
	rejectSource("/actions",tooMany+"}");
	Check(document.ReplaceValue("/actions/brighten/arguments/value/args/1","0.5",diagnostics),"action expression is editable through canonical source transactions");
	Check(document.Model().ResolveAction("brighten",variables,invocation,error) && std::get<double>(invocation.arguments.at("value"))==1.5,
		"edited canonical action uses recompiled expression without host execution");
}
struct TestHost final : Host {
	int drawCalls = 0, errors = 0;
	std::vector<Vertex> drawn;
	struct Layer { std::vector<Vertex> vertices; std::vector<Vertex> pixels; int width=0,height=0; std::uint64_t frame=0; };
	std::uint64_t renderFrame = 1;
	std::uint64_t RenderFrame() const override { return renderFrame; }
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
	void ClearSamples(bool newFrame = true) {
		if (newFrame) ++renderFrame;
		layers[0].pixels.assign(samplePoints.size(),Vertex{0,0,0,0,0,0,0,0}); drawn.clear();
	}
	bool ReadFile(const std::string&, std::string&) override { return false; }
	StateValues cvars;
	bool ReadCVar(const std::string& name, size_t, StateValue& value) override {
		const auto found = cvars.find(name); if (found == cvars.end()) return false;
		value = found->second; return true;
	}
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
	bool BeginLayer(std::uint32_t id, int width, int height) override {
		if (failLayer || allocationsUntilFailure == 0) return false;
		if (allocationsUntilFailure > 0) --allocationsUntilFailure;
		if (layers.size() <= id) layers.resize(id+1);
		const auto& previous = layers[id];
		Check(previous.width == 0 || (previous.width == width && previous.height == height) || previous.frame != renderFrame,
			"target dimensions remain stable for every deferred draw in the same host frame");
		layers[id] = {}; layers[id].width=width; layers[id].height=height; layers[id].frame=renderFrame;
		layers[id].pixels.assign(samplePoints.size(),Vertex{0,0,0,0,0,0,0,0}); activeLayer = id; return true;
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
	CheckTypedActionDescriptors();
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
		Check(other.Initialize(),"share process services with a second independent context");
		TestHost otherHost;
		Runtime competing(otherHost);
		Check(!competing.Initialize(),"reject competing host services while contexts are live");
		Check(otherHost.errors == 1,"competing host gets an explicit diagnostic");
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
	std::ifstream bindingFile(argc > 2 ? argv[2] : "tools/ui/fixtures/binding-smoke.q4ui",std::ios::binary);
	Check(bindingFile.good(),"live binding fixture exists");
	const std::string bindingSource((std::istreambuf_iterator<char>(bindingFile)),std::istreambuf_iterator<char>());
	host.cvars["ui_retainedScale"] = 1.25;
	Check(runtime.LoadDocument(bindingSource,"bindings.q4ui",diagnostics),"load live state bindings");
	viewport = {1280,720,1,1}; runtime.Frame(viewport,110);
	Bounds track, fill; runtime.GetBounds("progress-track",track); runtime.GetBounds("progress-fill",fill);
	Check(Near(fill.width,track.width*.25f),"binding sets actual layout width");
	Check(runtime.PresentedValue("scale-reading","text")->text == "1.25","host CVar feeds runtime text");
	std::string stateError;
	Check(runtime.SetState({{"progress",75.0},{"heading",std::string("<img id='injected'> & Player")}},stateError,111),"application batch updates data");
	runtime.Frame(viewport,111); runtime.GetBounds("progress-fill",fill);
	Check(Near(fill.width,track.width*.75f),"live width changes without rebuilding document");
	Bounds injected;
	Check(!runtime.GetBounds("injected",injected),"dynamic text cannot inject retained elements");
	Check(runtime.PresentedValue("reading","text")->text == "75","numeric text follows live state");
	Check(runtime.FocusControl("reference-controls",111),"focus live bound control");
	runtime.MenuAction(MenuInput::Accept,true,111);
	Check(runtime.SetState({{"available",false}},stateError,111),"binding disables a pending activation");
	runtime.MenuAction(MenuInput::Accept,false,111);
	Check(runtime.TakeActions().empty() && runtime.GetControlState("reference-controls") == ControlState::Disabled,"state invalidation cancels the armed control");
	Check(!runtime.SetControlEnabled("reference-controls",true,111),"manual mutation cannot bypass bound availability");
	const auto boundRevision = runtime.StateRevision();
	Check(!runtime.SetState({{"progress",5.0},{"limit",0.0}},stateError,112),"invalid derived state is rejected");
	Check(runtime.StateRevision()==boundRevision && runtime.PresentedValue("reading","text")->text == "75","invalid batch keeps previous rendered values");
	host.cvars["ui_retainedScale"] = 2.0; runtime.Frame(viewport,112);
	Check(runtime.PresentedValue("scale-reading","text")->text == "2.00","external source refreshes existing document");
	const auto stableRevision=runtime.StateRevision(); runtime.Frame(viewport,113);
	Check(runtime.StateRevision()==stableRevision,"unchanged frame does not reevaluate bindings");
	host.cvars["ui_retainedScale"] = std::numeric_limits<double>::quiet_NaN(); runtime.Frame(viewport,113);
	Check(host.errors == 1 && runtime.StateRevision()==stableRevision && runtime.PresentedValue("scale-reading","text")->text == "2.00","invalid host state reports an error and retains the last snapshot");
	runtime.Frame(viewport,113); Check(host.errors == 1,"a persistent host error is not logged every frame");
	--host.errors; host.cvars["ui_retainedScale"] = 2.0; runtime.Frame(viewport,113);
	Check(runtime.SetState({{"shown",false}},stateError,113),"hide state"); runtime.Frame(viewport,113);
	Check(!runtime.FocusControl("reference-controls",113),"hidden bound panel has no eligible controls");
	Check(runtime.SetState({{"shown",true},{"available",true}},stateError,114),"restore bound panel"); runtime.Frame(viewport,114);
	const auto savedState=runtime.GetState(false);
	Check(!savedState.contains("scale"),"application snapshot excludes host sources");
	runtime.Shutdown(); Check(runtime.LoadDocument(bindingSource,"bindings-restart.q4ui",diagnostics),"restart live document");
	Check(runtime.SetState(savedState,stateError,115),"restore application state through validated batch");
	runtime.Frame(viewport,115);
	Check(runtime.PresentedValue("reading","text")->text == "75" && runtime.PresentedValue("scale-reading","text")->text == "2.00","restart preserves application state and rereads current host state");
	runtime.CloseDocument();
	Check(!runtime.IsLoaded() && runtime.GetState().empty(),"close releases document state");
	runtime.Shutdown();
	Check(runtime.Statistics().residentGeometryCount == 0 && runtime.Statistics().residentGeometryBytes == 0,"geometry accounting returns to zero after shutdown");
	Check(runtime.Initialize(),"restart lifetime without stale services");
	runtime.Shutdown();
	{
		auto replace = [](std::string source, const std::string& before, const std::string& after) {
			const auto at = source.find(before); Check(at != std::string::npos,"snapshot mutation target exists");
			source.replace(at,before.size(),after); return source;
		};
		Runtime first(host), second(host);
		std::string snapshot, error;
		std::string actionBindingSource=bindingSource;
		actionBindingSource.insert(actionBindingSource.rfind('}'),R"json(,
		 "actions":{"menu.controls":{"operation":"fixture.capture","arguments":{
		  "progress":{"state":"progress"},"scale":{"state":"scale"}
		 }}})json");
		Document snapshotActions;
		Check(snapshotActions.Load(actionBindingSource,diagnostics),"compile action descriptor for existing opaque semantic control");
		Check(first.LoadDocument(actionBindingSource,"snapshot-bindings.q4ui",diagnostics),"load durable state fixture");
		Check(second.LoadDocument(actionBindingSource,"snapshot-bindings.q4ui",diagnostics),"load independent snapshot recipient");
		Check(first.SetState({{"progress",37.0},{"heading",std::string("#str_test")}},error,1),"set snapshot application values");
		first.Frame({},1); Check(first.FocusControl("reference-controls",1),"focus snapshot fixture");
		first.MenuAction(MenuInput::Accept,true,1); first.MenuAction(MenuInput::Accept,false,1);
		first.MenuAction(MenuInput::Accept,true,1);
		Check(first.SaveSnapshot(snapshot,error,1.025),"serialize versioned durable snapshot");
		Check(first.GetControlState("reference-controls")==ControlState::Pressed && first.TakeActions().size()==1,"capture leaves live press and action queue unchanged");
		const auto savedApplication = first.GetState(false);
		host.cvars["ui_retainedScale"] = 1.5;
		Check(second.SetState({{"progress",80.0}},error,10),"independent recipient begins with different state");
		Check(second.RestoreSnapshot(snapshot,error,10),"restore application, focus and current host sources together");
		Check(second.GetState(false)==savedApplication && std::get<double>(second.GetState().at("scale"))==1.5,"snapshot never overwrites current CVar sources");
		ActionInvocation restoredAction;
		Check(snapshotActions.Model().ResolveAction("menu.controls",second.GetState(),restoredAction,error) &&
			std::get<double>(restoredAction.arguments.at("progress"))==37 && std::get<double>(restoredAction.arguments.at("scale"))==1.5,
			"action parameters resolve saved application data with current authoritative host sources after restore");
		Check(second.TakeActions().empty(),"descriptor resolution and snapshot restore cannot replay an application activation");
		Check(second.FocusedControl()=="reference-controls" && second.GetControlState("reference-controls")==ControlState::Focus,"restored press is cancelled into persistent focus before layout");
		second.MenuAction(MenuInput::Accept,false,10);
		Check(second.TakeActions().empty(),"orphan release cannot activate restored instance");
		second.Frame({},10.1);
		Check(second.FocusedControl()=="reference-controls" && second.PresentedValue("reading","text")->text=="37","first post-restore layout retains valid focus and derived values");
		Check(first.RestoreSnapshot(snapshot,error,2),"restore into currently held receiving instance");
		first.MenuAction(MenuInput::Accept,true,2); first.MenuAction(MenuInput::Accept,false,2);
		Check(first.TakeActions().empty(),"restore quarantines receiving-instance repeat and release");
		first.Frame({},2.1); first.MenuAction(MenuInput::Accept,true,2.1); first.MenuAction(MenuInput::Accept,false,2.1);
		Check(first.TakeActions().size()==1,"fresh complete activation works once after restored release");
		std::string stable;
		Check(second.SaveSnapshot(stable,error,10.2),"capture recipient before corrupt restore attempts");
		const auto revision = second.StateRevision();
		auto reject = [&](const std::string& corrupt) {
			Check(!second.RestoreSnapshot(corrupt,error,20) && !error.empty(),"invalid snapshot rejects with diagnostic");
			std::string after;
			Check(second.SaveSnapshot(after,error,10.2) && after==stable && second.StateRevision()==revision,"failed restore preserves state, focus, playback and presentation clock atomically");
		};
		reject("{}"); reject(snapshot.substr(0,snapshot.size()-1)); reject(snapshot+" {}");
		reject(replace(snapshot,"\"format\":\"openq4-ui-instance\"","\"format\":\"openq4-ui-instance-next\""));
		reject(replace(snapshot,"\"version\":2","\"version\":3"));
		reject(replace(snapshot,"\"path\":\"snapshot-bindings.q4ui\"","\"path\":\"different.q4ui\""));
		reject(replace(snapshot,"\"progress\":37.0","\"progress\":true"));
		reject(replace(snapshot,"\"limit\":100.0","\"limit\":0.0"));
		reject(replace(snapshot,"\"progress\":37.0","\"progress\":1e999"));
		reject(replace(snapshot,"\"progress\":37.0","\"progress\":037"));
		reject(replace(snapshot,"\"progress\":37.0","\"progress\":37."));
		reject(replace(snapshot,"\"progress\":37.0","\"progress\":37.0,\"progress\":20.0"));
		reject(replace(snapshot,"\"heading\":\"#str_test\"","\"heading\":\"\\ud800\\u0041\""));
		reject(replace(snapshot,"\"application\":{","\"application\":{\"scale\":9.0,"));
		reject(replace(snapshot,"\"widgets\":{}","\"widgets\":{\"scroll\":10}"));
		reject(replace(snapshot,"\"focus\":\"reference-controls\"","\"focus\":\"unknown\""));
		host.cvars["ui_retainedScale"] = std::numeric_limits<double>::quiet_NaN(); reject(snapshot);
		host.cvars["ui_retainedScale"] = 1.5;
		Check(second.LoadDocument(bindingSource+"\n","snapshot-bindings.q4ui",diagnostics),"load source revision with unchanged IDs");
		Check(!second.RestoreSnapshot(snapshot,error,30),"exact source changes require an explicit snapshot migration");
		Check(second.LoadMarkup("<rml><body/></rml>","raw.rml"),"load raw markup snapshot exclusion fixture");
		std::string unchanged="unchanged";
		Check(!second.SaveSnapshot(unchanged,error,30) && unchanged=="unchanged" && !second.RestoreSnapshot(snapshot,error,30),"raw markup cannot claim canonical snapshot support");
	}
	{
		Runtime first(host), second(host);
		std::string snapshot, error;
		Check(first.LoadDocument(canonical,"snapshot-motion.q4ui",diagnostics) && second.LoadDocument(canonical,"snapshot-motion.q4ui",diagnostics),"load timeline snapshot pair");
		Check(first.PlayTimeline("slide",1),"start durable timeline");
		Check(first.SaveSnapshot(snapshot,error,1.25) && second.RestoreSnapshot(snapshot,error,100),"reanchor active playback to independent time domain");
		Check(Near(float(second.PresentedValue("canonical-panel","transform")->data[0]),25),"restore preserves quarter progress immediately");
		std::string corrupt=snapshot;
		auto at=corrupt.find("\"durationMs\":1000.0"); Check(at!=std::string::npos,"serialized duration exists");
		corrupt.replace(at,std::string("\"durationMs\":1000.0").size(),"\"durationMs\":0.0");
		Check(!second.RestoreSnapshot(corrupt,error,1000),"zero-duration playback cannot enter evaluator");
		corrupt=snapshot; at=corrupt.find("\"owner\":\"slide\""); Check(at!=std::string::npos,"serialized track owner exists");
		corrupt.replace(at,std::string("\"owner\":\"slide\"").size(),"\"owner\":\"unknown\"");
		Check(!second.RestoreSnapshot(corrupt,error,1000),"unknown playback owner rejects atomically");
		second.Frame({},100.25);
		Check(Near(float(second.PresentedValue("canonical-panel","transform")->data[0]),50),"restored timeline continues at original duration");
		Check(second.PlayTimeline("slide",100.25),"retarget restored timeline from current presentation");
		Check(second.SaveSnapshot(snapshot,error,100.5) && first.RestoreSnapshot(snapshot,error,200),"restore partially retargeted track");
		first.Frame({},200.25);
		Check(Near(float(first.PresentedValue("canonical-panel","transform")->data[0]),75),"retarget source value survives round trip");
		first.PauseTimeline("slide",200.25);
		Check(first.SaveSnapshot(snapshot,error,300) && second.RestoreSnapshot(snapshot,error,1000),"round trip paused timeline across long idle gap");
		second.Frame({},1100);
		Check(Near(float(second.PresentedValue("canonical-panel","transform")->data[0]),75),"paused presentation does not consume restored wall time");
		second.ResumeTimeline("slide",1100); second.Frame({},1100.25);
		Check(Near(float(second.PresentedValue("canonical-panel","transform")->data[0]),87.5f),"resumed timeline consumes only unpaused time");
		second.CancelTimeline("slide",CancelPolicy::Hold,1100.25);
		Check(second.SaveSnapshot(snapshot,error,1100.5) && first.RestoreSnapshot(snapshot,error,2000),"round trip held presentation after cancel");
		first.Frame({},2001);
		Check(Near(float(first.PresentedValue("canonical-panel","transform")->data[0]),87.5f),"cancelled hold value persists without resurrecting playback");
	}
	{
		std::string repeatSource=canonical;
		const auto at=repeatSource.find("\"durationMs\":1000");
		Check(at!=std::string::npos,"repeat test timeline exists");
		repeatSource.insert(at,"\"iterations\":0,");
		Runtime first(host),second(host); std::string snapshot,error;
		Check(first.LoadDocument(repeatSource,"snapshot-repeat.q4ui",diagnostics) && second.LoadDocument(repeatSource,"snapshot-repeat.q4ui",diagnostics),"load indefinitely repeating snapshot pair");
		first.PlayTimeline("slide",1);
		Check(first.SaveSnapshot(snapshot,error,1000000.25) && second.RestoreSnapshot(snapshot,error,2),"unbounded repeat stores bounded phase across unrelated clocks");
		Check(Near(float(second.PresentedValue("canonical-panel","transform")->data[0]),25),"repeat phase survives large original uptime");
		second.Frame({},2.25);
		Check(Near(float(second.PresentedValue("canonical-panel","transform")->data[0]),50),"restored repeat advances from authored cycle start");
		std::string unchanged="unchanged";
		Check(!first.SaveSnapshot(unchanged,error,1e308) && unchanged=="unchanged","overflowing repeated playback cannot produce an unrestorable snapshot");
		Check(first.LoadDocument(vectorDocument,"snapshot-reduced.q4ui",diagnostics) && second.LoadDocument(vectorDocument,"snapshot-reduced.q4ui",diagnostics),"load reduced motion snapshot pair");
		first.SetReducedMotion(true,10); first.PlayTimeline("fade",10);
		Check(first.SaveSnapshot(snapshot,error,10.04) && second.RestoreSnapshot(snapshot,error,20),"restore bounded reduced-motion fade");
		Check(Near(float(second.PresentedValue("shape","opacity")->data[0]),.375f),"reduced fade resumes at its shortened midpoint");
		host.ClearSamples(); second.Frame({240,160,1,1},20.04);
		Check(Near(float(second.PresentedValue("shape","opacity")->data[0]),.25f),"reduced fade completes within its original 80 ms limit");
	}
	{
		const char* combined=R"json({"format":"openq4-ui","version":1,"id":"atomic-snapshot",
		 "root":{"id":"root","type":"group","properties":{"opacity":{"type":"number","value":1}}},
		 "state":{"amount":{"type":"number","initial":1},"host":{"type":"number","initial":1,"cvar":"snapshot_denominator"}},
		 "bindings":[{"id":"ratio","node":"root","property":"opacity","value":{"op":"/","args":[{"state":"amount"},{"state":"host"}]}}]})json";
		Runtime first(host),second(host); std::string snapshot,error;
		host.cvars["snapshot_denominator"]=1.0;
		Check(first.LoadDocument(combined,"snapshot-atomic.q4ui",diagnostics) && second.LoadDocument(combined,"snapshot-atomic.q4ui",diagnostics),"load atomic host/application fixture");
		Check(first.SetState({{"amount",.5}},error,1) && first.SaveSnapshot(snapshot,error,1),"save application amount independently from host denominator");
		host.cvars["snapshot_denominator"]=.5;
		Check(second.RestoreSnapshot(snapshot,error,2) && Near(float(second.PresentedValue("root","opacity")->data[0]),1),"restore evaluates valid combined snapshot without invalid host-first intermediate");
		Document availability;
		Check(availability.Load(bindingSource,diagnostics) && availability.ReplaceValue("/state/available",R"({"type":"boolean","initial":true,"cvar":"snapshot_available"})",diagnostics),"compile host-owned availability fixture");
		host.cvars["snapshot_available"]=true;
		Check(first.LoadDocument(availability.Source(),"snapshot-availability.q4ui",diagnostics) && second.LoadDocument(availability.Source(),"snapshot-availability.q4ui",diagnostics),"load availability snapshot pair");
		first.Frame({},3); first.FocusControl("reference-controls",3); first.Frame({},3.2);
		Check(first.SaveSnapshot(snapshot,error,3.2),"save focused control while host permits interaction");
		host.cvars["snapshot_available"]=false;
		Check(second.RestoreSnapshot(snapshot,error,10),"current host may invalidate saved focus on restore");
		Check(second.FocusedControl().empty() && second.GetControlState("reference-controls")==ControlState::Disabled,"current availability wins over saved selection");
		second.Frame({},10.2);
		Check(second.PresentedValue("reference-controls-focus","opacity")->data[0]<.001,"host invalidation transitions saved focus ink to disabled presentation");
	}
	{
		Runtime first(host), second(host);
		std::string snapshot,error;
		Check(first.LoadDocument(interactionSource,"snapshot-modal.q4ui",diagnostics) && second.LoadDocument(interactionSource,"snapshot-modal.q4ui",diagnostics),"load modal snapshot pair");
		first.Frame({},1); Check(first.FocusControl("reference-controls",1),"choose modal return focus");
		Check(first.SetControlEnabled("reference-system",false,1),"set persistent unbound availability override");
		Check(first.PushModal("modal-panel",1) && first.PushModal("modal-controls",1),"open nested modal scopes");
		first.MenuAction(MenuInput::Accept,true,1);
		Check(first.SaveSnapshot(snapshot,error,1.02) && second.RestoreSnapshot(snapshot,error,100),"restore nested modal snapshot before recipient first layout");
		second.Frame({},100.1);
		Check(second.FocusedControl()=="modal-controls" && !second.FocusControl("reference-controls",100.1),"restored modal constrains current focus scope");
		Check(second.GetControlState("reference-system")==ControlState::Disabled,"unbound availability override survives restore");
		Check(second.PopModal(100.1) && second.FocusedControl()=="modal-controls","inner modal restores its prior focus");
		Check(second.PopModal(100.1) && second.FocusedControl()=="reference-controls","outer modal restores pre-dialog focus");
		second.MenuAction(MenuInput::Accept,false,100.1); Check(second.TakeActions().empty(),"restored modal teardown cannot release a saved press");
	}
	for (int device = 0; device < 3; ++device) {
		Runtime restored(host); Input adapter;
		std::string snapshot,error,before,after;
		Check(restored.LoadDocument(interactionSource,"snapshot-routed-input.q4ui",diagnostics),"load composed restore/source-quarantine fixture");
		restored.Frame({},1); Check(restored.FocusControl("reference-controls",1),"focus composed restore target");
		Bounds target; Check(restored.GetBounds("reference-controls",target),"get internal fixture pointer coordinates");
		const float x=target.x+target.width*.5f,y=target.y+target.height*.5f;
		const MenuInput action=device==1 ? MenuInput::Back : MenuInput::Accept;
		double now=1;
		auto route=[&]() {
			const auto events=adapter.Take();
			for (const auto& event : events) {
				if (event.kind==RoutedInput::Kind::Cancel) restored.CancelInput(now);
				else if (event.kind==RoutedInput::Kind::PointerButton) restored.PointerButton(event.down,now);
				else restored.MenuAction(event.menu,event.down,now);
			}
			return events.size();
		};
		auto edge=[&](bool down,bool repeated=false) {
			if (device==2) adapter.Pointer(7,down,now);
			else adapter.Menu(7,action,down,repeated,now);
		};
		if (device==2) restored.PointerMove(x,y,now);
		edge(true); Check(route()==1,"adapter delivers original logical down edge");
		Check(restored.SaveSnapshot(snapshot,error,1.025) && restored.RestoreSnapshot(snapshot,error,10),"restore while real adapter source remains held");
		Check(restored.SaveSnapshot(before,error,10),"capture presentation before logical source release");
		adapter.Cancel(false); adapter.Take(); restored.ReleaseInputSources();
		Check(restored.SaveSnapshot(after,error,10) && after==before,"source release preserves exact restored focus, modal and timeline state");
		now=10.1;
		edge(true,true); Check(route()==0,"quarantined source repeat cannot rearm restored view");
		edge(false); Check(route()==0 && restored.TakeActions().empty(),"quarantined release emits no event or duplicate activation");
		now=10.2; restored.Frame({},now);
		if (device==2) restored.PointerMove(x,y,now);
		edge(true); Check(route()==1,"first fresh physical press reaches released logical latches");
		edge(true,true); Check(route()==0,"fresh held repeat remains aggregated");
		edge(false); Check(route()==1,"fresh matching release reaches restored view");
		const auto result=restored.TakeActions();
		Check(result.size()==1 && result[0].kind==(device==1 ? ControlAction::Kind::Back : ControlAction::Kind::Activate),"first fresh accept, back or pointer action after restore fires exactly once");
		if (device!=1) Check(result[0].node=="reference-controls","restored activation retains the intended control");
	}
	for (bool pointer : {false,true}) {
		Input adapter;
		const std::uint32_t source=pointer ? 65536u : 7u;
		auto edge=[&](bool down) {
			if (pointer) adapter.Pointer(source,down,1);
			else adapter.Menu(source,MenuInput::Accept,down,false,1);
		};
		edge(true); Check(adapter.Take().size()==1,"quarantine-release fixture acquires original source");
		adapter.Cancel(false); adapter.Take();
		adapter.ReleaseQuarantined(source); adapter.ReleaseQuarantined(source);
		Check(adapter.Take().empty(),"stale and duplicate stale releases retire quarantine without routed events");
		edge(true);
		auto events=adapter.Take();
		Check(events.size()==1 && events[0].down,"first fresh press works after old-generation quarantine release");
		adapter.ReleaseQuarantined(source);
		Check(adapter.Take().empty(),"late stale release does not emit events for a fresh source hold");
		edge(true); Check(adapter.Take().empty(),"late stale release cannot erase fresh hold and permit a duplicate down");
		edge(false); events=adapter.Take();
		Check(events.size()==1 && !events[0].down,"fresh hold still receives exactly its matching release");
		adapter.ReleaseQuarantined(source); edge(false);
		Check(adapter.Take().empty(),"stale release after completed fresh hold is harmless");
	}
	{
		// Duplicate document/node IDs are local to their view. Different density,
		// data and input must survive interleaved frames and arbitrary close order.
		auto first = std::make_unique<Runtime>(host);
		Runtime second(host);
		Check(first->LoadDocument(bindingSource,"first.q4ui",diagnostics),"first independent live document");
		Check(second.LoadDocument(bindingSource,"second.q4ui",diagnostics),"second independent live document");
		Check(first->SetState({{"progress",20.0}},stateError,1),"first instance state");
		Check(second.SetState({{"progress",80.0}},stateError,100),"second instance state");
		const Viewport firstViewport{900,650,1,1}, secondViewport{1200,900,2,1};
		first->Frame(firstViewport,1); second.Frame(secondViewport,100);
		Bounds firstTrack,firstFill,secondTrack,secondFill;
		first->GetBounds("progress-track",firstTrack); first->GetBounds("progress-fill",firstFill);
		second.GetBounds("progress-track",secondTrack); second.GetBounds("progress-fill",secondFill);
		Check(Near(firstFill.width,firstTrack.width*.2f) && Near(secondFill.width,secondTrack.width*.8f),"same IDs retain separate live values and layouts");
		Check(Near(secondTrack.width,firstTrack.width*2),"contexts retain independent density");
		Check(first->FocusControl("reference-controls",1),"focus first view");
		Check(second.FocusedControl().empty(),"focus does not cross view boundaries");
		first->MenuAction(MenuInput::Accept,true,1);
		Check(second.SetState({{"available",false}},stateError,100),"disable only second view");
		first->MenuAction(MenuInput::Accept,false,1);
		Check(first->TakeActions().size()==1 && second.TakeActions().empty(),"activation and availability are independent");
		const auto secondRevision = second.StateRevision();
		Check(!first->LoadDocument("{}","bad.q4ui",diagnostics),"failed replacement is local");
		Check(second.StateRevision()==secondRevision && first->PresentedValue("reading","text")->text=="20","failed load preserves both documents");
		const auto secondGeometry = second.Statistics().residentGeometryCount;
		first.reset(); // The initial RmlUi service creator may leave first.
		Check(second.IsLoaded() && second.Statistics().activeContexts==1,"survivor retains shared services");
		Check(second.Statistics().residentGeometryCount==secondGeometry,"closing a neighbor does not flush the survivor's text geometry");
		for (int iteration=0; iteration<24; ++iteration) {
			Runtime temporary(host);
			Check(temporary.LoadDocument(bindingSource,"temporary.q4ui",diagnostics),"reacquire an idle render backend");
			temporary.Frame(firstViewport,iteration);
			Check(temporary.Statistics().residentBackends==2 && second.Statistics().activeContexts==2,"backend residency is bounded by peak concurrent views");
		}
		second.Frame(secondViewport,101);
		Check(second.PresentedValue("reading","text")->text=="80" && second.GetControlState("reference-controls")==ControlState::Disabled,"survivor keeps data/input after context churn");
		second.Shutdown();
		Check(second.Statistics().residentGeometryCount==0,"last context releases its geometry");
	}
	{
		Runtime first(host),second(host);
		Check(first.LoadDocument(maskedDocument,"mask-first.q4ui",diagnostics),"first masked view");
		Check(second.LoadDocument(maskedDocument,"mask-second.q4ui",diagnostics),"second masked view");
		Check(first.PlayTimeline("fade",1) && second.PlayTimeline("fade",100),"separate playback epochs");
		host.samplePoints={{20.37f,20.63f},{70.37f,50.63f}};
		const Viewport small{240,160,1,1},large{800,600,1,1};
		host.ClearSamples(); first.Frame(small,1.25);
		pixel(0,.375f*maskAlpha,1-.375f*maskAlpha,0); pixel(1,0,1,0);
		host.ClearSamples(false); second.Frame(large,100.75);
		pixel(0,.125f*maskAlpha,1-.125f*maskAlpha,0); pixel(1,0,1,0);
		host.ClearSamples(false); first.Frame(small,1.5);
		pixel(0,.25f*maskAlpha,1-.25f*maskAlpha,0); pixel(1,0,1,0);
		Check(first.Statistics().maskSnapshots==2 && second.Statistics().maskSnapshots==2,"each view composites its own masks");
		second.Shutdown();
		host.ClearSamples(); first.Frame(small,1.75);
		pixel(0,.125f*maskAlpha,1-.125f*maskAlpha,0); pixel(1,0,1,0);
		Check(first.Statistics().activeContexts==1,"closing the newer view also preserves the older view");
	}
	{
		Runtime limited(host);
		Check(limited.LoadDocument(maskedDocument,"bounded-frames.q4ui",diagnostics),"load frame-fence exhaustion fixture");
		host.samplePoints={{20.37f,20.63f}}; ++host.renderFrame;
		int exhausted = 0;
		for (int i=0; i<60; ++i) {
			host.ClearSamples(false);
			const int errors = host.errors;
			limited.Frame({300+i,200,1,1},200+i*.001);
			if (host.errors != errors) {
				Check(host.errors==errors+1 && host.activeLayer==0,"exhausted frame reports once and restores base output");
				host.errors=errors; ++exhausted;
			}
		}
		Check(exhausted>0,"many different targets in one frame obey the shared slot cap");
		host.ClearSamples(); limited.Frame({450,200,1,1},201);
		pixel(0,.5f*maskAlpha,1-.5f*maskAlpha,0);
		Check(host.errors==0 && host.activeLayer==0,"next host frame recycles targets and recovers drawing");
	}
	host.samplePoints.clear();
	Check(host.errors==0,"no library warnings or errors");
	std::puts("Retained UI: density, layout, input, clipping, motion, bindings, independent contexts, bounded backend reuse, transactional instance snapshots and restart passed");
}
