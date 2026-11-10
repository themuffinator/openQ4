// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "src/ui/retained/Runtime.h"
#include "src/ui/retained/CoverageRaster.h"
#include "src/ui/GuiStateNumber.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <fstream>
#include <iterator>
#include "src/ui/retained/Input.h"
#include "src/ui/retained/State.h"
#include "src/framework/InitializationPresentation.h"

using namespace openq4::ui;
static void Check(bool condition, const char* message) {
	if (!condition) { std::fprintf(stderr,"FAIL: %s\n",message); std::exit(1); }
}
static bool Near(float a, float b) { return std::abs(a-b) < .1f; }
static void CheckGuiStateNumbers() {
	for (int number:{0,1,-1,720,1280,std::numeric_limits<int>::min(),std::numeric_limits<int>::max()}) {
		char expected[64],actual[64];
		std::snprintf(expected,sizeof(expected),"%i",number);
		Check(std::strcmp(expected,GuiStateInteger(number,actual))==0,"integer GUI publication preserves the printf dictionary string");
	}
	const auto check=[](float number) {
		char expected[64],actual[64];
		std::snprintf(expected,sizeof(expected),"%f",static_cast<double>(number));
		const char* integral=GuiStateIntegralFloat(number,actual);
		const bool fits=number>=-2147483648.f && number<2147483648.f && number==std::trunc(number);
		Check(bool(integral)==fits,"fractional, large and exceptional GUI values retain printf");
		if (integral) Check(std::strcmp(expected,integral)==0,"integral GUI publication preserves the complete printf dictionary string");
	};
	for (float number:{0.f,-0.f,1.f,-1.f,720.f,1280.f,.1f,.25f,.9999998f,16777218.f,
		-2147483648.f,2147483648.f,std::numeric_limits<float>::max(),-std::numeric_limits<float>::max(),
		std::numeric_limits<float>::infinity(),-std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}) check(number);
	std::uint32_t bits=0x146135;
	for (int i=0;i<65536;++i) {
		bits=bits*1664525u+1013904223u;
		float number; std::memcpy(&number,&bits,sizeof(number)); check(number);
	}
}
static void CheckCoverageRaster() {
	std::vector<Vertex> vertices;
	std::vector<int> indices;
	const auto rectangle=[&](float x,float y,float w,float h,float red,float green,float alpha) {
		const int base=int(vertices.size());
		for (const auto& p:std::array<std::array<float,2>,4>{{{x,y},{x+w,y},{x+w,y+h},{x,y+h}}})
			vertices.push_back({p[0],p[1],0,0,red,green,0,alpha});
		indices.insert(indices.end(),{base,base+1,base+2,base,base+2,base+3});
	};
	rectangle(10,20,1,2,1,0,1);
	rectangle(11,20,2,1,0,.5f,.5f);
	rectangle(11,21,1,1,0,.25f,0); // Pure additive light retains zero coverage.
	CoverageBitmap bitmap;
	Check(RasterizeCoverage(vertices,indices,bitmap),"coverage cells rasterize");
	Check(bitmap.x==10 && bitmap.y==20 && bitmap.width==3 && bitmap.height==2,"coverage extent excludes empty margins");
	Check(bitmap.pixels[0]==255 && bitmap.pixels[4]==0 && bitmap.pixels[5]==128 && bitmap.pixels[7]==128,
		"coverage raster preserves premultiplied colour and alpha");
	Check(bitmap.pixels[16]==0 && bitmap.pixels[17]==64 && bitmap.pixels[19]==0,"coverage raster preserves additive light");
	const auto saved=bitmap.pixels;
	rectangle(11,20,1,1,.25f,0,.5f);
	Check(!RasterizeCoverage(vertices,indices,bitmap) && bitmap.pixels==saved,"overlapping cells preserve GPU blend order and destination rounding");
	vertices.resize(vertices.size()-4); indices.resize(indices.size()-6);
	vertices[0].x+=.25f;
	Check(!RasterizeCoverage(vertices,indices,bitmap) && bitmap.pixels==saved,"subpixel geometry retains original triangles without partial publication");
	vertices[0].x-=.25f; vertices[1].r=.5f;
	Check(!RasterizeCoverage(vertices,indices,bitmap),"gradient cells retain their original samples");
	vertices[1].r=1;
	Check(!RasterizeCoverage(vertices,indices,bitmap,2),"coverage allocation obeys extent budget");
	indices[0]=1;
	Check(!RasterizeCoverage(vertices,indices,bitmap),"arbitrary topology retains its original triangles");
	vertices.clear(); indices.clear();
	for (int i=0;i<20;++i) rectangle(0,0,500,500,.5f,0,.5f);
	Check(!RasterizeCoverage(vertices,indices,bitmap),"overlapping coverage obeys its work budget");
	vertices.clear(); indices.clear();
	rectangle(0,0,1280,2,.5f,0,.5f);
	Check(!RasterizeCoverage(vertices,indices,bitmap) && RasterizeCoverage(vertices,indices,bitmap,8192),
		"wide coverage can be admitted for tiled storage without changing the default extent");
	Check(bitmap.width==1280 && bitmap.height==2 && bitmap.pixels[510*4]==128 && bitmap.pixels[1020*4+3]==128,
		"wide coverage retains the exact samples on both sides of tile boundaries");
	vertices.clear(); indices.clear();
	rectangle(0,0,1,1,1,0,1); rectangle(8000,8000,1,1,1,0,1);
	Check(!RasterizeCoverage(vertices,indices,bitmap,8192),"sparse oversized bounds obey the independent bitmap storage budget");
}
static void CheckInitializationPacing() {
	openq4::InitializationPresentation pacing;
	Check(!pacing.Due(1000,60),"startup cannot redraw before its first frame");
	pacing.Presented(0,100); // Cold compilation is paid once.
	Check(!pacing.Due(116,0) && pacing.Due(117,0),"startup caps uncapped presentation at 60 Hz");
	Check(!pacing.Due(116,240) && pacing.Due(117,240),"startup caps high presentation at 60 Hz");
	Check(!pacing.Due(149,20) && pacing.Due(150,20),"startup respects a lower presentation cap");
	pacing.Presented(150,152);
	Check(!pacing.Due(166,60) && pacing.Due(167,60),"warm inexpensive frames keep a 60 Hz start-to-start cadence");
	pacing.Presented(167,207); // 40 ms: smoothed to 11.5 ms of cost.
	Check(!pacing.Due(218,60) && pacing.Due(219,60),"one blocked present is smoothed instead of stalling the entrance");
	for (int i = 0; i < 40; ++i) pacing.Presented(2000+i*100,2020+i*100);
	Check(!pacing.Due(5995,60) && pacing.Due(6001,60),"slow startup redraws reserve four fifths of later time for loading");
	Check(!pacing.Due(5900,60),"a backwards clock cannot cause a redraw burst");
	pacing.Presented(9000,9001);
	Check(!pacing.Due(9001,60),"a late checkpoint never produces catch-up frames");
}
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
	std::vector<Vertex> imageVertices;
	std::vector<std::string> imageSources;
	struct Layer { std::vector<Vertex> vertices; std::vector<Vertex> pixels; int width=0,height=0; std::uint64_t frame=0; };
	std::uint64_t renderFrame = 1;
	std::uint64_t RenderFrame() const override { return renderFrame; }
	std::vector<Layer> layers{1};
	std::uint32_t activeLayer = 0;
	std::vector<Bounds> samplePoints;
	bool failLayer = false;
	bool cacheLayers = false;
	bool maskedSourceLayers = false;
	bool linearMasks = false;
	int linearMaskDraws=0;
	bool replayFrames = true;
	bool collectTimings = true;
	bool CollectTimings() const override { return collectTimings; }
	bool CanCacheLayers() const override { return cacheLayers; }
	bool CanReplayFrames() const override { return replayFrames; }
	int allocationsUntilFailure = -1;
	std::uint32_t maxLayerId = 48;
	std::uint64_t layerBudget=0;
	unsigned trimmedLayers=0;
	static void Over(Vertex& destination, const Vertex& source, float opacity = 1) {
		const float remain = 1-source.a*opacity;
		destination.r = source.r*opacity+destination.r*remain;
		destination.g = source.g*opacity+destination.g*remain;
		destination.b = source.b*opacity+destination.b*remain;
		destination.a = source.a*opacity+destination.a*remain;
	}
	void ClearSamples(bool newFrame = true) {
		if (newFrame) ++renderFrame;
		layers[0].pixels.assign(samplePoints.size(),Vertex{0,0,0,0,0,0,0,0}); drawn.clear(); imageVertices.clear();
	}
	bool ReadFile(const std::string&, std::string&) override { return false; }
	StateValues cvars;
	bool ReadCVar(const std::string& name, size_t, StateValue& value) override {
		const auto found = cvars.find(name); if (found == cvars.end()) return false;
		value = found->second; return true;
	}
	std::string Translate(const std::string& s) override { return s == "#str_test" ? "Localised" : s == "#str_live_binding" ? "^k_attack" : s; }
	void Log(bool error, const std::string& s) override { if (error) { ++errors; std::fprintf(stderr,"RmlUi: %s\n",s.c_str()); } }
	std::uintptr_t LoadMaterial(const std::string& source, int& w, int& h) override {
		imageSources.push_back(source); w=h=256; return source.starts_with("q4-add/") ? 2 : 1;
	}
	void Draw(const std::vector<Vertex>& vertices, const std::vector<int>& indices, std::uintptr_t texture) override {
		++drawCalls;
		if (texture) imageVertices.insert(imageVertices.end(),vertices.begin(),vertices.end());
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
				if (texture==2) source.a=0; // A white additive image, as at the engine boundary.
				Over(layers[activeLayer].pixels[p],source);
			}
		}
	}
	bool BeginLayer(std::uint32_t id, int width, int height) override {
		if (failLayer || allocationsUntilFailure == 0 || id > maxLayerId) return false;
		std::uint64_t bytes=std::uint64_t(width)*height*4;
		for (size_t i=1; i<layers.size(); ++i) if (i!=id) bytes+=std::uint64_t(layers[i].width)*layers[i].height*4;
		if (layerBudget && bytes>layerBudget) return false;
		if (allocationsUntilFailure > 0) --allocationsUntilFailure;
		if (layers.size() <= id) layers.resize(id+1);
		const auto& previous = layers[id];
		Check(previous.width == 0 || (previous.width == width && previous.height == height) || previous.frame != renderFrame,
			"target dimensions remain stable for every deferred draw in the same host frame");
		layers[id] = {}; layers[id].width=width; layers[id].height=height; layers[id].frame=renderFrame;
		layers[id].pixels.assign(samplePoints.size(),Vertex{0,0,0,0,0,0,0,0}); activeLayer = id; return true;
	}
	bool TrimLayer(std::uint32_t id) override {
		if (!layerBudget || id>=layers.size() || !layers[id].width || id==activeLayer) return false;
		Check(layers[id].frame!=renderFrame,"memory pressure never retires a target referenced by this submission frame");
		layers[id]={}; ++trimmedLayers; return true;
	}
	void CompositeLayer(std::uint32_t source, std::uint32_t destination, float opacity, const Bounds& clip) override {
		Check(source != destination,"composition never samples its destination");
		layers[source].frame=renderFrame;
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
	bool CanCompositeMaskedLayer() const override { return maskedSourceLayers; }
	bool CanDrawLinearMask() const override { return linearMasks; }
	void DrawLinearMask(std::uint32_t source,const LinearMask& mask,const Bounds& clip,float opacity) override {
		++linearMaskDraws;
		if (source) { layers[source].frame=renderFrame; activeLayer=0; }
		for (size_t i=0;i<samplePoints.size();++i) {
			const auto point=samplePoints[i];
			if (point.x<clip.x || point.x>=clip.x+clip.width || point.y<clip.y || point.y>=clip.y+clip.height) continue;
			const double x=std::floor(point.x),y=std::floor(point.y);
			std::vector<VectorPoint> polygon{{x,y},{x+1,y},{x+1,y+1},{x,y+1}};
			for (size_t e=0;e<4 && !polygon.empty();++e) {
				const auto a=mask.points[e],b=mask.points[(e+1)%4];
				const auto distance=[&](VectorPoint p){return (b.x-a.x)*(p.y-a.y)-(b.y-a.y)*(p.x-a.x);};
				std::vector<VectorPoint> clipped; auto before=polygon.back(); double bd=distance(before);
				for (const auto p:polygon) {
					const double d=distance(p);
					if ((d>0 && bd<0) || (d<0 && bd>0)) {
						const double t=bd/(bd-d); clipped.push_back({before.x+(p.x-before.x)*t,before.y+(p.y-before.y)*t});
					}
					if (d>=0) clipped.push_back(p);
					before=p; bd=d;
				}
				polygon=std::move(clipped);
			}
			double area=0;
			for (size_t j=0;j<polygon.size();++j) {
				const auto a=polygon[j],b=polygon[(j+1)%polygon.size()];
				area+=(a.x-x)*(b.y-y)-(a.y-y)*(b.x-x);
			}
			const double t=std::clamp(mask.gradient[0]*(x+.5)+mask.gradient[1]*(y+.5)+mask.gradient[2],0.,1.);
			const double alpha=t<=mask.middle ? mask.alpha[0]+(mask.alpha[1]-mask.alpha[0])*t/mask.middle :
				mask.alpha[1]+(mask.alpha[2]-mask.alpha[1])*(t-mask.middle)/(1-mask.middle);
			const float coverage=float(std::round(alpha*std::clamp(area*.5,0.,1.)*255)/255);
			const Vertex colour=source ? layers[source].pixels[i] : Vertex{0,0,0,0,1,1,1,1};
			Over(layers[activeLayer].pixels[i],colour,coverage*opacity);
		}
	}
	void CompositeMaskedLayer(std::uint32_t source,const std::vector<Vertex>& vertices,
		const std::vector<int>& indices,float opacity) override {
		Check(source && source<layers.size(),"masked composition samples an owned layer");
		layers[source].frame=renderFrame; activeLayer=0;
		auto background=layers[0].pixels;
		layers[0].pixels.assign(samplePoints.size(),Vertex{0,0,0,0,0,0,0,0});
		Draw(vertices,indices,0);
		const auto mask=std::move(layers[0].pixels); layers[0].pixels=std::move(background);
		for(size_t i=0;i<samplePoints.size();++i) Over(layers[0].pixels[i],layers[source].pixels[i],mask[i].a*opacity);
	}
	void EndLayer(std::uint32_t restore) override { activeLayer = restore; }
	// Retained submission: each drawn mesh's identity and revision, and the
	// identities retired with their geometry.
	std::vector<std::pair<std::uint64_t,std::uint64_t>> meshes;
	std::vector<std::uint64_t> releasedMeshes;
	void DrawMesh(std::uint64_t mesh, std::uint64_t revision, const std::vector<Vertex>& vertices,
		const std::vector<int>& indices, std::uintptr_t material) override {
		Check(mesh != 0 && revision != 0,"retained meshes carry an identity and revision");
		meshes.emplace_back(mesh,revision); Draw(vertices,indices,material);
	}
	void ReleaseMesh(std::uint64_t mesh) override { releasedMeshes.push_back(mesh); }
	struct Softened { std::uint32_t destination; float sigma, saturation; Bounds region; };
	std::vector<Softened> softened;
	bool softFocus = true;
	bool directBackdrop = false;
	bool sampleUniformBackdrop = false;
	bool CanSoftenBackdropDirect(float, float, const Bounds& region) const override {
		return directBackdrop && softFocus && region.x<=0 && region.y<=0 && region.x+region.width>=1280 && region.y+region.height>=720;
	}
	bool SoftenBackdropDirect(float sigma, float saturation, const Bounds& region) override {
		return SoftenBackdrop(0,sigma,saturation,region);
	}
	bool SoftenBackdrop(std::uint32_t destination, float sigma, float saturation, const Bounds& region) override {
		softened.push_back({destination,sigma,saturation,region});
		if (softFocus) {
			activeLayer = destination;
			if (sampleUniformBackdrop) for (size_t i=0;i<samplePoints.size();++i) {
				auto colour=layers[0].pixels[i];
				const float luma=.2126f*colour.r+.7152f*colour.g+.0722f*colour.b;
				colour.r=luma+(colour.r-luma)*saturation; colour.g=luma+(colour.g-luma)*saturation;
				colour.b=luma+(colour.b-luma)*saturation; colour.a=1;
				layers[destination].pixels[i]=colour;
			}
		}
		return softFocus;
	}
	struct Streaked { std::uint32_t destination; float strength; Bounds region; };
	std::vector<Streaked> streaked;
	bool StreakBackdrop(std::uint32_t destination, float strength, const Bounds& region) override {
		streaked.push_back({destination,strength,region});
		if (softFocus) activeLayer = destination;
		return softFocus;
	}
	FontMetrics GetFontMetrics(const std::string&, int size) override { return {size*.8f,size*.2f,size*1.2f,size*.5f}; }
	Glyph GetGlyph(const std::string&, int size, std::uint32_t) override {
		return {size*.6f,0,-size*.8f,size*.6f,static_cast<float>(size),0,0,1,1,"test-font"};
	}
};

// Soft focus: a node's backdrop-blur and backdrop-saturate become one RmlUi
// backdrop-filter, and the renderer asks the host to soften the base surface
// into the pushed layer: sigma in physical pixels, the saturation, and the
// node's box extended by the blur's reach. An unsupported host is counted and
// changes nothing. Zero blur at full saturation draws no backdrop at all.
static void CheckSoftFocus(TestHost& host) {
	const std::string source = R"json({"format":"openq4-ui","version":1,"id":"soft-focus",
	 "root":{"id":"root","type":"group","properties":{"width":{"type":"length","value":100,"unit":"%"},"height":{"type":"length","value":100,"unit":"%"}},
	  "children":[{"id":"veil","type":"group","properties":{"position":{"type":"keyword","value":"absolute"},
	   "left":{"type":"length","value":100,"unit":"dp"},"top":{"type":"length","value":50,"unit":"dp"},
	   "width":{"type":"length","value":200,"unit":"dp"},"height":{"type":"length","value":100,"unit":"dp"},
	   "backdrop-blur":{"type":"length","value":0,"unit":"dp"},"backdrop-saturate":{"type":"number","value":1}}}]},
	 "timelines":[{"id":"focus","durationMs":200,"tracks":[
	   {"node":"veil","property":"backdrop-blur","keys":[{"atMs":0,"value":{"type":"length","value":0,"unit":"dp"}},{"atMs":200,"value":{"type":"length","value":7.5,"unit":"dp"}}]},
	   {"node":"veil","property":"backdrop-saturate","keys":[{"atMs":0,"value":{"type":"number","value":1}},{"atMs":200,"value":{"type":"number","value":0.8}}]}]}],
	 "events":{"soften":[{"op":"playTimeline","timeline":"focus"}]}})json";
	Runtime runtime(host);
	std::vector<Diagnostic> diagnostics;
	Check(runtime.Initialize() && runtime.LoadDocument(source,"soft-focus.q4ui",diagnostics),"soft-focus document loads");
	Viewport viewport; viewport.width = 1280; viewport.height = 720; viewport.displayScale = 2;
	host.softened.clear();
	runtime.Frame(viewport,1);
	Check(host.softened.empty() && runtime.Statistics().backdropComposites == 0,"no blur at full saturation draws no backdrop");
	Runtime::EventEffects effects; std::string error;
	Check(runtime.RunEvent("soften",1,effects,error),"start the soft focus");
	runtime.Frame(viewport,1.1);
	Check(host.softened.size() == 1 && Near(host.softened[0].sigma,3.75f*2) && Near(host.softened[0].saturation,.9f) &&
		host.softened[0].destination != 0,"half way in: sigma in physical pixels, saturation ramping, into a pushed layer");
	host.softened.clear();
	runtime.Frame(viewport,1.3);
	const auto& region = host.softened.at(0).region;
	Check(host.softened.size() == 1 && Near(host.softened[0].sigma,15) && Near(host.softened[0].saturation,.8f) &&
		region.x <= 200 && region.y <= 100 && region.x+region.width >= 600 && region.y+region.height >= 300 &&
		runtime.Statistics().backdropComposites == 1 && runtime.Statistics().backdropFallbacks == 0,
		"settled: 7.5 dp at 2x is a 15 px sigma over the box and its blur reach");
	const auto captureRegion=region;
	for (int frame=0;frame<3;++frame) { host.ClearSamples(); runtime.Frame(viewport,1.31+frame*.01); }
	host.softened.clear(); host.ClearSamples(); runtime.Frame(viewport,1.35);
	Check(runtime.Statistics().frameReplays==1 && host.softened.size()==1 && host.activeLayer==0 &&
		host.softened[0].region.x==captureRegion.x && host.softened[0].region.y==captureRegion.y &&
		host.softened[0].region.width==captureRegion.width && host.softened[0].region.height==captureRegion.height,
		"settled backdrop replays commands and captures the current underlying frame over the original region");
	host.sampleUniformBackdrop=true; host.samplePoints={{210,110}};
	for (int frame=0;frame<2;++frame) {
		host.ClearSamples(); host.layers[0].pixels[0]=Vertex{0,0,0,0,float(frame==0),0,float(frame==1),1};
		Check(runtime.Frame(viewport,1.36+frame*.01) && runtime.Statistics().frameReplays==1,"replay a live uniform backdrop");
		const auto& pixel=host.layers[0].pixels[0];
		Check(frame==0 ? pixel.r>.8f && pixel.b<.1f : pixel.b>.8f && pixel.r<.1f,
			"replayed blur samples this frame's background instead of retained pixels");
	}
	host.sampleUniformBackdrop=false; host.samplePoints.clear();
	host.softFocus = false; host.softened.clear();
	runtime.Frame(viewport,1.4);
	Check(host.softened.size() == 1 && runtime.Statistics().backdropComposites == 0 && runtime.Statistics().backdropFallbacks == 1 &&
		host.errors == 0,"a host without soft focus is counted and logs nothing");
	host.softFocus = true;
	viewport.width=960; host.ClearSamples();
	Check(runtime.Frame(viewport,1.5) && runtime.Statistics().frameReplays==0,"resize rebuilds backdrop commands before drawing");
}

static void CheckDirectBackdrop(TestHost& host) {
	const char* source=R"json({"format":"openq4-ui","version":1,"id":"full-backdrop",
	 "root":{"id":"root","type":"group","properties":{"width":{"type":"length","unit":"%","value":100},"height":{"type":"length","unit":"%","value":100}},
	 "children":[{"id":"soften","type":"group","properties":{"width":{"type":"length","unit":"%","value":100},"height":{"type":"length","unit":"%","value":100},
	 "opacity":{"type":"number","value":1},"backdrop-blur":{"type":"length","unit":"dp","value":4},"backdrop-saturate":{"type":"number","value":0.8}}}]}})json";
	Runtime runtime(host);std::vector<Diagnostic> diagnostics;
	Check(runtime.LoadDocument(source,"full-backdrop.q4ui",diagnostics),"load full-view backdrop fixture");
	host.sampleUniformBackdrop=true;host.samplePoints={{100,100}};
	for(int frame=0;frame<3;++frame){host.ClearSamples();Check(runtime.Frame({},frame),"settle full-view backdrop recording");}
	int frame=3;
	for(bool direct:{false,true,true,false}) {
		host.directBackdrop=direct;host.ClearSamples();host.softened.clear();
		const bool red=frame%2==0;
		host.layers[0].pixels[0]=Vertex{0,0,0,0,float(red),0,float(!red),1};
		Check(runtime.Frame({},frame++),"replay full-view backdrop");
		Check(runtime.Statistics().frameReplays==1 && host.softened.size()==1 &&
			(host.softened[0].destination==0)==direct && host.activeLayer==0,"direct backdrop is selected only for a supported host");
		Check(runtime.Statistics().layerPushes==unsigned(!direct),"direct backdrop avoids a transient target and composite");
		const auto& pixel=host.layers[0].pixels[0];
		Check(red ? pixel.r>.8f && pixel.b<.1f : pixel.b>.8f && pixel.r<.1f,"direct and layered backdrop keep this frame's live colour");
	}
	host.directBackdrop=true;host.softFocus=false;host.ClearSamples();host.softened.clear();
	Check(runtime.Frame({},frame++) && runtime.Statistics().backdropFallbacks==1 && host.softened[0].destination!=0,
		"unavailable direct backdrop retains the ordinary transparent fallback");
	host.softFocus=true;
	for(bool partial:{false,true}) {
		Document layered;Check(layered.Load(source,diagnostics),"load layered backdrop reference");
		Check(layered.ReplaceValue(partial ? "/root/children/0/properties/height/value" : "/root/children/0/properties/opacity/value",
			partial ? "50" : "0.5",diagnostics),"set partial or translucent backdrop");
		Check(runtime.LoadDocument(layered.Source(),"layered-backdrop.q4ui",diagnostics),"load constrained backdrop runtime");
		for(int i=0;i<3;++i){host.ClearSamples();Check(runtime.Frame({},frame++),"settle constrained backdrop");}
		host.ClearSamples();host.softened.clear();host.layers[0].pixels[0]=Vertex{0,0,0,0,1,0,0,1};
		Check(runtime.Frame({},frame++) && host.softened.size()==1 && host.softened[0].destination!=0,
			"partial and translucent backdrops retain their intermediate target");
		if(!partial)Check(std::abs(host.layers[0].pixels[0].r-.92126f)<.001f,"translucent soft focus retains its group opacity");
	}
	host.directBackdrop=false;host.sampleUniformBackdrop=false;host.samplePoints.clear();
}

// The speed streak: a node's backdrop-streak becomes RmlUi's backdrop-filter
// streak(), and the renderer asks the host to streak the view into the pushed
// layer at that strength over the node's box, instead of soft focus. No
// strength draws nothing.
static void CheckSpeedStreak(TestHost& host) {
	const std::string source = R"json({"format":"openq4-ui","version":1,"id":"speed-streak",
	 "root":{"id":"root","type":"group","properties":{"width":{"type":"length","value":100,"unit":"%"},"height":{"type":"length","value":100,"unit":"%"}},
	  "children":[{"id":"streak","type":"group","properties":{"position":{"type":"keyword","value":"absolute"},
	   "left":{"type":"length","value":0,"unit":"dp"},"top":{"type":"length","value":0,"unit":"dp"},
	   "width":{"type":"length","value":100,"unit":"%"},"height":{"type":"length","value":100,"unit":"%"},
	   "backdrop-blur":{"type":"length","value":4,"unit":"dp"},"backdrop-streak":{"type":"number","value":0}}}]},
	 "timelines":[{"id":"boost","durationMs":200,"tracks":[
	   {"node":"streak","property":"backdrop-streak","keys":[{"atMs":0,"value":{"type":"number","value":0}},{"atMs":200,"value":{"type":"number","value":0.5}}]}]}],
	 "events":{"boost":[{"op":"playTimeline","timeline":"boost"}]}})json";
	Document document; std::vector<Diagnostic> diagnostics;
	Check(document.Load(source,diagnostics),"a streak document compiles");
	{
		// Shared vector art: a vector node draws the symbol it names, and a
		// document refuses an unknown symbol, a node with both a symbol and
		// its own paths, and a symbol on a node that is no vector.
		const auto symbols = [](const char* node) {
			return std::string(R"json({"format":"openq4-ui","version":1,"id":"symbols","symbols":{"dash":[{"id":"line","commands":[
				{"id":"a","op":"move","points":[[0,0]]},{"id":"b","op":"line","points":[[{"fraction":1},0]]}],
				"stroke":{"paint":{"type":"solid","color":{"type":"color","value":[1,1,1,1]}},"widthDp":1,"join":"miter","cap":"butt"}}]},
				"root":{"id":"root","type":"group","children":[)json")+node+R"json(]}})json";
		};
		Document shared; std::vector<Diagnostic> found;
		Check(shared.Load(symbols(R"json({"id":"one","type":"vector","symbol":"dash"},{"id":"two","type":"vector","symbol":"dash"})json"),found) &&
			found.empty(),"a document with shared symbols loads");
		const auto* one = shared.Model().FindNode("one"); const auto* two = shared.Model().FindNode("two");
		Check(one && two && one->paths.size() == 1 && two->paths.size() == 1 && one->paths[0].id == "line" && two->paths[0].id == "line",
			"each node draws the symbol's paths");
		for (const char* bad : {R"json({"id":"one","type":"vector","symbol":"dot"})json",
			R"json({"id":"one","type":"vector","symbol":"dash","paths":[]})json", R"json({"id":"one","type":"group","symbol":"dash"})json"}) {
			Document refused; found.clear();
			Check(!refused.Load(symbols(bad),found) && !found.empty(),"an unknown or misplaced symbol refuses the document");
		}
	}
	Check(!document.Load(R"json({"format":"openq4-ui","version":1,"id":"speed-streak-range","root":{"id":"root","type":"group",
		"properties":{"backdrop-streak":{"type":"number","value":1.5}}}})json",diagnostics),"a streak beyond full strength is rejected");
	Runtime runtime(host);
	Check(runtime.Initialize() && runtime.LoadDocument(source,"speed-streak.q4ui",diagnostics),"streak document loads");
	Viewport viewport; viewport.width = 1280; viewport.height = 720;
	host.softened.clear(); host.streaked.clear();
	runtime.Frame(viewport,1);
	Check(host.streaked.empty() && host.softened.size() == 1,"without strength the node keeps its soft focus");
	Runtime::EventEffects effects; std::string error;
	Check(runtime.RunEvent("boost",1,effects,error),"start the streak");
	host.softened.clear();
	runtime.Frame(viewport,1.3);
	Check(host.streaked.size() == 1 && host.softened.empty() && Near(host.streaked[0].strength,.5f) && host.streaked[0].destination != 0 &&
		Near(host.streaked[0].region.width,1280) && Near(host.streaked[0].region.height,720) && host.errors == 0,
		"a streak replaces the soft focus at its strength over the node's box");
}

// Timeline completion programs: a fade hides its box when it plays to its end,
// once. Cancellation suppresses it, replay restarts it, pause delays it, and
// reduced motion completes a spatial timeline inside the event that plays it.
static void CheckCompletionPrograms(TestHost& host) {
	const std::string source = R"json({"format":"openq4-ui","version":1,"id":"completion-programs",
	 "state":{"shown":{"type":"boolean","initial":true},"phase":{"type":"number","initial":0}},
	 "root":{"id":"root","type":"group","properties":{"width":{"type":"length","value":100,"unit":"%"},"height":{"type":"length","value":100,"unit":"%"}},
	  "children":[{"id":"box","type":"group","properties":{"position":{"type":"keyword","value":"absolute"},
	   "left":{"type":"length","value":10,"unit":"dp"},"top":{"type":"length","value":10,"unit":"dp"},
	   "width":{"type":"length","value":50,"unit":"dp"},"height":{"type":"length","value":50,"unit":"dp"},
	   "display":{"type":"keyword","value":"block"},"opacity":{"type":"number","value":1},
	   "transform":{"type":"transform","unit":"dp","value":[0,0,1,1,0]}}}]},
	 "bindings":[{"id":"shown","node":"box","property":"display","value":{"op":"select","args":[{"state":"shown"},"block","none"]}}],
	 "timelines":[
	  {"id":"fade","durationMs":300,"complete":"Faded","tracks":[{"node":"box","property":"opacity","keys":[
	   {"atMs":0,"value":{"type":"number","value":1}},{"atMs":300,"value":{"type":"number","value":0}}]}]},
	  {"id":"slide","durationMs":200,"complete":"slid","tracks":[{"node":"box","property":"transform","keys":[
	   {"atMs":0,"value":{"type":"transform","unit":"dp","value":[0,0,1,1,0]}},{"atMs":200,"value":{"type":"transform","unit":"dp","value":[20,0,1,1,0]}}]}]}],
	 "events":{
	  "show":[{"op":"setState","values":{"shown":true}}],
	  "hide":[{"op":"playTimeline","timeline":"fade"}],
	  "faded":[{"op":"setState","values":{"shown":false,"phase":{"op":"+","args":[{"state":"phase"},1]}}}],
	  "stop":[{"op":"cancelTimeline","timeline":"fade","policy":"hold"}],
	  "pause":[{"op":"pauseTimeline","timeline":"fade"}],
	  "resume":[{"op":"resumeTimeline","timeline":"fade"}],
	  "move":[{"op":"playTimeline","timeline":"slide"}],
	  "slid":[{"op":"setState","values":{"phase":{"op":"+","args":[{"state":"phase"},10]}}}]}})json";
	Runtime runtime(host);
	std::vector<Diagnostic> diagnostics;
	Check(runtime.Initialize() && runtime.LoadDocument(source,"completion.q4ui",diagnostics),"completion document loads");
	Viewport viewport; viewport.width = 640; viewport.height = 480;
	const auto state = [&](const char* key) { return runtime.GetState(false).at(key); };
	const auto run = [&](const char* name, double seconds) {
		Runtime::EventEffects effects; std::string error;
		Check(runtime.RunEvent(name,seconds,effects,error),"completion test event runs");
		return effects;
	};
	runtime.Frame(viewport,1);
	Check(run("hide",1).stateChanges.empty(),"playing a fade completes nothing yet");
	runtime.Frame(viewport,1.1);
	Check(std::get<bool>(state("shown")) && runtime.TakeCompletionWrites().empty(),"no completion before the end");
	runtime.Frame(viewport,1.31);
	const auto writes = runtime.TakeCompletionWrites();
	const auto display = runtime.PresentedValue("box","display");
	Check(!std::get<bool>(state("shown")) && writes.size() == 2 && !std::get<bool>(writes.at("shown")) &&
		std::get<double>(writes.at("phase")) == 1 && display && display->text == "none","the end of the fade runs its program and hides the box");
	runtime.Frame(viewport,1.4);
	Check(runtime.TakeCompletionWrites().empty() && std::get<double>(state("phase")) == 1,"a completion runs once");
	run("show",2); run("hide",2); run("stop",2.1);
	runtime.Frame(viewport,2.5);
	Check(std::get<bool>(state("shown")) && std::get<double>(state("phase")) == 1,"cancellation never completes");
	run("hide",3); run("hide",3.2);
	runtime.Frame(viewport,3.35);
	Check(std::get<bool>(state("shown")),"a replay restarts the run");
	runtime.Frame(viewport,3.55);
	Check(!std::get<bool>(state("shown")) && std::get<double>(state("phase")) == 2,"the replayed run completes");
	run("show",4); run("hide",4); run("pause",4.1);
	runtime.Frame(viewport,4.5);
	Check(std::get<bool>(state("shown")),"a paused run does not complete");
	run("resume",5);
	runtime.Frame(viewport,5.25);
	Check(!std::get<bool>(state("shown")) && std::get<double>(state("phase")) == 3,"a resumed run completes on its remaining time");
	runtime.TakeCompletionWrites();
	runtime.SetReducedMotion(true,6);
	const auto moved = run("move",6);
	Check(moved.stateChanges.contains("phase") && std::get<double>(moved.stateChanges.at("phase")) == 13 &&
		runtime.TakeCompletionWrites().empty(),"reduced motion completes a spatial timeline inside its event, with the event's writes");
	runtime.SetReducedMotion(false,6.1);
	// A snapshot taken after a completion became due, before any frame ran it,
	// carries its effect, and the restored instance never runs it again.
	run("show",7); run("hide",7);
	std::string snapshot, error;
	Check(runtime.SaveSnapshot(snapshot,error,7.4),"snapshot after the fade ended, before a frame ran its completion");
	Check(std::get<bool>(state("shown")) && std::get<double>(state("phase")) == 13,"saving does not run the completion on the live instance");
	Runtime restored(host);
	Check(restored.Initialize() && restored.LoadDocument(source,"completion.q4ui",diagnostics) && restored.RestoreSnapshot(snapshot,error,7.4),
		"restore the snapshot");
	restored.Frame(viewport,7.5); restored.Frame(viewport,7.8);
	const auto after = restored.GetState(false);
	Check(!std::get<bool>(after.at("shown")) && std::get<double>(after.at("phase")) == 14 && restored.TakeCompletionWrites().empty(),
		"the snapshot carries the due completion and the restore never replays it");
	Check(host.errors == 0,"completion programs log no errors");
}

// A static frame resubmits each mesh with its identity and revision unchanged,
// so a host may reuse what it derived from it; a whole-pixel move changes the
// revision, and every identity is released with its geometry. A subtree
// composited at zero opacity takes no layer and draws nothing, while partial
// opacity still composites in isolation.
static void CheckRetainedMeshes(TestHost& host) {
	Runtime runtime(host);
	std::vector<Diagnostic> diagnostics;
	const char* document = R"json({"format":"openq4-ui","version":1,"id":"mesh-test",
	 "root":{"id":"root","type":"group","properties":{"position":{"type":"keyword","value":"absolute"},"left":{"type":"length","value":0,"unit":"dp"},"top":{"type":"length","value":0,"unit":"dp"},"width":{"type":"length","value":400,"unit":"dp"},"height":{"type":"length","value":200,"unit":"dp"}},
	  "children":[{"id":"faded","type":"vector","properties":{"position":{"type":"keyword","value":"absolute"},"left":{"type":"length","value":10,"unit":"dp"},"top":{"type":"length","value":10,"unit":"dp"},"width":{"type":"length","value":50,"unit":"dp"},"height":{"type":"length","value":50,"unit":"dp"},"opacity":{"type":"number","value":0.5},"transform":{"type":"transform","unit":"px","value":[0,0,1,1,0]}},"paths":[{"id":"rectangle","fill":{"type":"solid","color":{"type":"color","value":[0.8,0.4,0.2,0.8]}},"commands":[{"id":"p0","op":"move","points":[[0,0]]},{"id":"p1","op":"line","points":[[{"fraction":1},0]]},{"id":"p2","op":"line","points":[[{"fraction":1},{"fraction":1}]]},{"id":"p3","op":"line","points":[[0,{"fraction":1}]]},{"id":"close","op":"close"}]}]},
	   {"id":"hidden","type":"vector","properties":{"position":{"type":"keyword","value":"absolute"},"left":{"type":"length","value":100,"unit":"dp"},"top":{"type":"length","value":10,"unit":"dp"},"width":{"type":"length","value":50,"unit":"dp"},"height":{"type":"length","value":50,"unit":"dp"},"opacity":{"type":"number","value":0},"transform":{"type":"transform","unit":"px","value":[0,0,1,1,0]}},"paths":[{"id":"rectangle","fill":{"type":"solid","color":{"type":"color","value":[0.8,0.4,0.2,0.8]}},"commands":[{"id":"p0","op":"move","points":[[0,0]]},{"id":"p1","op":"line","points":[[{"fraction":1},0]]},{"id":"p2","op":"line","points":[[{"fraction":1},{"fraction":1}]]},{"id":"p3","op":"line","points":[[0,{"fraction":1}]]},{"id":"close","op":"close"}]}]}]},
	 "timelines":[{"id":"move","durationMs":1000,"tracks":[{"node":"faded","property":"transform","keys":[{"atMs":0,"value":{"type":"transform","unit":"px","value":[0,0,1,1,0]}},{"atMs":1000,"value":{"type":"transform","unit":"px","value":[10,0,1,1,0]}}]}]}]})json";
	Check(runtime.LoadDocument(document,"meshes.q4ui",diagnostics),"load the retained mesh fixture");
	Viewport viewport;
	runtime.Frame(viewport,1);
	host.meshes.clear(); host.ClearSamples(); runtime.Frame(viewport,2);
	const auto first = host.meshes;
	host.meshes.clear(); host.ClearSamples(); runtime.Frame(viewport,3);
	Check(!first.empty() && host.meshes == first,"a static frame resubmits every mesh with its identity and revision");
	const auto statistics = runtime.Statistics();
	Check(statistics.layerElisions == 1 && statistics.layerPushes == 1 && statistics.layerComposites == 1,
		"zero opacity takes no layer while partial opacity still composites");
	bool hiddenDrawn = false;
	for (const auto& v : host.drawn) hiddenDrawn |= v.x >= 100;
	Check(!host.drawn.empty() && !hiddenDrawn,"a zero-opacity subtree draws nothing");
	Check(runtime.PlayTimeline("move",3),"move the faded rectangle");
	host.meshes.clear(); host.ClearSamples(); runtime.Frame(viewport,3.5);
	bool revised = false;
	for (const auto& [mesh,revision] : host.meshes)
		for (const auto& [before,old] : first) revised |= mesh == before && revision > old;
	Check(revised,"a moved mesh keeps its identity with a new revision");
	runtime.Shutdown();
	for (const auto& [mesh,revision] : first)
		Check(std::find(host.releasedMeshes.begin(),host.releasedMeshes.end(),mesh) != host.releasedMeshes.end(),
			"every mesh identity is released with its geometry");
}
static void CheckFrameReplay(TestHost& host) {
	const char* source=R"json({"format":"openq4-ui","version":1,"id":"frame-replay",
	 "state":{"left":{"type":"number","initial":10}},
	 "bindings":[{"id":"move","node":"panel","property":"left","value":{"state":"left"}}],
	 "root":{"id":"root","type":"group","children":[{"id":"panel","type":"group","properties":{
	  "position":{"type":"keyword","value":"absolute"},"left":{"type":"length","unit":"dp","value":10},
	  "top":{"type":"length","unit":"dp","value":10},"width":{"type":"length","unit":"dp","value":50},
	  "height":{"type":"length","unit":"dp","value":50},"background-color":{"type":"color","value":[1,0,0,1]}}}]}})json";
	Runtime runtime(host); std::vector<Diagnostic> diagnostics; std::string error;
	Check(runtime.LoadDocument(source,"frame-replay.q4ui",diagnostics),"load static presentation replay fixture");
	Viewport viewport;
	for (int frame=0;frame<3;++frame) { host.ClearSamples(); Check(runtime.Frame(viewport,frame),"render static presentation"); }
	Check(runtime.Statistics().frameReplays==1 && !host.drawn.empty(),"unchanged presentation replays its submitted draws");
	const auto previous=host.drawn;
	host.collectTimings=false;
	host.ClearSamples(); runtime.Frame(viewport,3);
	Check(!runtime.Statistics().timingsEnabled && runtime.Statistics().frameMilliseconds==0 &&
		runtime.Statistics().hostDrawMilliseconds==0 && runtime.Statistics().drawCalls>0,
		"disabled profiling skips time measurements while retaining draw counts");
	host.collectTimings=true;
	Check(host.drawn.size()==previous.size() && std::equal(host.drawn.begin(),host.drawn.end(),previous.begin(),
		[](const auto& a,const auto& b){return a.x==b.x && a.y==b.y && a.r==b.r && a.g==b.g && a.b==b.b && a.a==b.a;}),
		"replay preserves geometry, colour and order");
	Check(runtime.SetState({{"left",80.0}},error,4),"change a cached document's bound geometry");
	host.ClearSamples(); runtime.Frame(viewport,4);
	Check(runtime.Statistics().frameReplays==0 && !host.drawn.empty() && host.drawn.front().x>=80,
		"bound property updates cannot replay obsolete geometry");
	runtime.Frame(viewport,5); runtime.Frame(viewport,6);
	Check(runtime.Statistics().frameReplays==1,"changed presentation becomes reusable after settling");
	viewport.width=640; viewport.height=480; runtime.Frame(viewport,7);
	Check(runtime.Statistics().frameReplays==0,"viewport changes invalidate the submitted frame");
	Document liveText;
	Check(liveText.Load(source,diagnostics) && liveText.ReplaceValue("/bindings","[]",diagnostics) && liveText.ReplaceValue("/root/children",R"json([
	 {"id":"binding","type":"text","legacyText":"window","properties":{
	  "width":{"type":"length","unit":"dp","value":100},"height":{"type":"length","unit":"dp","value":50},
	  "text":{"type":"text","value":"#str_live_binding"}}}])json",diagnostics) &&
	 runtime.LoadDocument(liveText.Source(),"live-binding.q4ui",diagnostics),"replace cached presentation with live binding text");
	for (int frame=8;frame<12;++frame) runtime.Frame(viewport,frame);
	Check(runtime.Statistics().frameReplays==0 && runtime.Statistics().liveTextRuns==1,
		"live binding text always reaches the host even without a document property change");
	const char* movingImage=R"json({"format":"openq4-ui","version":1,"id":"sampling-replay",
	 "root":{"id":"image","type":"image","properties":{
	  "width":{"type":"length","unit":"dp","value":100},"height":{"type":"length","unit":"dp","value":50},
	  "image":{"type":"image","value":"gfx/test/trace.tga"},"image-blend":{"type":"keyword","value":"additive"},
	  "image-offset-x":{"type":"number","value":0},"image-scale-x":{"type":"number","value":0.2},"opacity":{"type":"number","value":0.5}}},
	 "timelines":[{"id":"scroll","durationMs":1000,"tracks":[
	 {"node":"image","property":"image-offset-x","keys":[{"atMs":0,"value":{"type":"number","value":0}},{"atMs":1000,"value":{"type":"number","value":1}}]},
	 {"node":"image","property":"image-scale-x","keys":[{"atMs":0,"value":{"type":"number","value":0.2}},{"atMs":1000,"value":{"type":"number","value":2}}]}]}]})json";
	Check(runtime.LoadDocument(movingImage,"sampling-replay.q4ui",diagnostics) && runtime.PlayTimeline("scroll",12),
		"load an image whose sampling offset and scale animate");
	for (const double seconds:{12.0,12.1,12.2}) { host.ClearSamples(); runtime.Frame(viewport,seconds); }
	Check(runtime.Statistics().frameReplays==1 && !host.imageVertices.empty(),"UV-only animation replays stationary geometry");
	Check(runtime.Statistics().layerPushes==0,"a disjoint image fades without an intermediate target");
	for (const auto& v:host.imageVertices) Check(std::abs(v.u-(.42f+v.x/100*.56f))<.00001f && std::abs(v.r-.5f)<.00001f,
		"replay samples the current offset and scale with the isolated fade");
	runtime.Frame(viewport,12.3); host.ClearSamples(); runtime.Frame(viewport,13);
	for (const auto& v:host.imageVertices) Check(std::abs(v.u-(.5f+v.x/100*2))<.00001f,"replay reaches the final image sampling phase");
	const auto replayed=host.imageVertices;
	host.replayFrames=false; host.ClearSamples(); runtime.Frame(viewport,13);
	Check(runtime.Statistics().frameReplays==0 && host.imageVertices.size()==replayed.size(),"replay diagnostic switch restores normal rendering");
	for (size_t i=0;i<replayed.size();++i) Check(std::abs(replayed[i].u-host.imageVertices[i].u)<.00001f && std::abs(replayed[i].v-host.imageVertices[i].v)<.00001f,
		"live sampling replay matches normal rendering at the same time");
	host.replayFrames=true;
	std::string fading=movingImage;
	fading.insert(fading.find("\"root\""),R"json("state":{"alpha":{"type":"number","initial":0.5}},
	 "bindings":[{"id":"fade","node":"image","property":"opacity","value":{"state":"alpha"}}],)json");
	Check(runtime.LoadDocument(fading,"paint-opacity.q4ui",diagnostics),"load isolated opacity presentation");
	Check(runtime.PlayTimeline("scroll",20),"animate UV sampling while opacity changes");
	runtime.Frame(viewport,20); runtime.Frame(viewport,20.1);
	double previousOpacity=.5;
	double fadeTime=20.15;
	// Revisit the last installed CSS value after live-only updates, then cross
	// the zero/full-opacity boundaries that change the layer topology.
	for (const double opacity:{.25,.75,.5,.25,.125,.875,0.0,.5,1.0,.5,.3}) {
		fadeTime+=.05;
		Check(runtime.SetState({{"alpha",opacity}},error,fadeTime),"change isolated opacity");
		host.ClearSamples(); runtime.Frame(viewport,fadeTime);
		const bool reusable=previousOpacity>0 && previousOpacity<1 && opacity>0 && opacity<1;
		Check(runtime.Statistics().layoutUpdates==0 && runtime.Statistics().frameReplays==(reusable ? 1 : 0),
			"leaf opacity replays only while its visible layer topology stays unchanged");
		Check((opacity==0)==host.imageVertices.empty(),"zero opacity suppresses and nonzero opacity restores image paint");
		for (const auto& v:host.imageVertices) Check(std::abs(v.r-opacity)<.00001,
			"paint-only opacity preserves the current premultiplied tint");
		const auto replay=host.imageVertices;
		host.replayFrames=false; host.ClearSamples(); runtime.Frame(viewport,fadeTime);
		Check(replay.size()==host.imageVertices.size(),"full paint and live opacity replay have the same geometry");
		for (size_t i=0;i<replay.size();++i) {
			const auto& a=replay[i]; const auto& b=host.imageVertices[i];
			Check(a.x==b.x && a.y==b.y && std::abs(a.u-b.u)<.000001f && std::abs(a.v-b.v)<.000001f &&
				a.r==b.r && a.g==b.g && a.b==b.b && a.a==b.a,
				"replayed opacity matches full paint without cumulative colour drift");
		}
		host.replayFrames=true; runtime.Frame(viewport,fadeTime);
		previousOpacity=opacity;
	}
	Document textFade;
	const bool textLoaded=textFade.Load(R"json({"format":"openq4-ui","version":1,"id":"text-fade",
	 "state":{"alpha":{"type":"number","initial":0.5}},
	 "bindings":[{"id":"fade","node":"image","property":"opacity","value":{"state":"alpha"}}],
	 "root":{"id":"image","type":"text","properties":{
		 "width":{"type":"length","unit":"dp","value":200},"height":{"type":"length","unit":"dp","value":50},
		 "font-family":{"type":"font","value":"test"},"font-size":{"type":"length","unit":"dp","value":20},
		 "text":{"type":"text","value":"#str_test"},"opacity":{"type":"number","value":0.5},
		 "letter-spacing":{"type":"length","unit":"dp","value":0}}}})json",diagnostics);
	if (!textLoaded) for (const auto& diagnostic:diagnostics) std::fprintf(stderr,"%s\n",diagnostic.message.c_str());
	Check(textLoaded,"build leaf text fade fixture");
	for (const int mode:{0,1,2}) {
		const bool overlap=mode!=0;
		host.cacheLayers=mode==2;
		host.samplePoints={{9.5f,8.5f,0,0}};
		if (overlap) Check(textFade.ReplaceValue("/root/properties/letter-spacing/value","-5",diagnostics),"overlap glyphs");
		Check(runtime.LoadDocument(textFade.Source(),"text-opacity.q4ui",diagnostics),"load leaf text fade fixture");
		if (mode==2) {
			host.ClearSamples(); runtime.Frame(viewport,22);
			host.ClearSamples(false); runtime.Frame(viewport,22);
			Check(runtime.Statistics().layerCacheHits==0 && runtime.Statistics().frameReplays==0,
				"a cold text submission cannot be cached again in the same host frame before its upload");
		}
		for (const double time:{22.,22.05,22.1,22.15}) { host.ClearSamples(); runtime.Frame(viewport,time); }
		Check(runtime.SetState({{"alpha",.3}},error,22.2),"fade leaf text");
		host.ClearSamples(); runtime.Frame(viewport,22.2);
		Check(runtime.Statistics().frameReplays==(mode==1 ? 0 : 1) && !host.drawn.empty(),
			"text replays live opacity only with disjoint quads or a retained isolated composition");
		const auto replay=host.drawn;
		const auto pixels=host.layers[0].pixels;
		host.replayFrames=false; host.cacheLayers=false; host.ClearSamples(); runtime.Frame(viewport,22.2);
		Check(replay.size()==host.drawn.size(),"faded text has unchanged draw topology");
		for (size_t i=0;i<replay.size();++i) Check(replay[i].r==host.drawn[i].r && replay[i].a==host.drawn[i].a,
			"text replay and full paint preserve premultiplied colour");
		for (size_t i=0;i<pixels.size();++i) Check(pixels[i].r==host.layers[0].pixels[i].r && pixels[i].a==host.layers[0].pixels[i].a,
			"retained text preserves overlapping glyph composition");
		host.replayFrames=true;
	}
	host.cacheLayers=false; host.samplePoints.clear();
}


static void CheckHiddenPresentation(TestHost& host) {
	const char* source=R"json({"format":"openq4-ui","version":1,"id":"hidden-layout",
	 "state":{"shown":{"type":"boolean","initial":false}},
	 "bindings":[{"id":"show","node":"branch","property":"display","value":{"op":"select","args":[{"state":"shown"},"block","none"]}}],
	 "root":{"id":"root","type":"group","children":[{"id":"branch","type":"group",
	  "properties":{"display":{"type":"keyword","value":"none"}},"children":[{"id":"growing","type":"group",
	  "properties":{"width":{"type":"length","unit":"dp","value":10},"height":{"type":"length","unit":"dp","value":20}}}]}]},
	 "timelines":[{"id":"grow","durationMs":1000,"tracks":[{"node":"growing","property":"width","keys":[
	  {"atMs":0,"value":{"type":"length","unit":"dp","value":10}},
	  {"atMs":1000,"value":{"type":"length","unit":"dp","value":110}}]}]}]})json";
	Runtime runtime(host); std::vector<Diagnostic> diagnostics; std::string error; Bounds bounds;
	Check(runtime.LoadDocument(source,"hidden-layout.q4ui",diagnostics),"load hidden presentation fixture");
	runtime.Frame({},0);
	Check(runtime.PlayTimeline("grow",0),"start hidden size animation");
	runtime.Frame({},.25);
	runtime.Frame({},.5);
	Check(runtime.Statistics().layoutUpdates==0 && runtime.Statistics().motionInputsScanned==0 &&
		Near(float(runtime.PresentedValue("growing","width")->data[0]),60),
		"hidden animation advances without repeating property or layout work");
	Check(runtime.SetState({{"shown",true}},error,.5),"reveal animated branch");
	runtime.Frame({},.5);
	Check(runtime.Statistics().layoutUpdates==1 && runtime.GetBounds("growing",bounds) && Near(bounds.width,60),
		"reveal resolves every deferred property at the current phase");
	runtime.Frame({},.75);
	Check(runtime.Statistics().layoutUpdates==1 && runtime.GetBounds("growing",bounds) && Near(bounds.width,85),
		"visible size changes still update layout");
	Check(runtime.SetState({{"shown",false}},error,.75),"hide animated branch again");
	runtime.Frame({},.75); runtime.Frame({},1);
	Check(runtime.Statistics().layoutUpdates==0,"hidden completion does not relayout visible siblings");
	Check(runtime.SetState({{"shown",true}},error,1.1),"reveal completed hidden animation");
	runtime.Frame({},1.1);
	Check(runtime.GetBounds("growing",bounds) && Near(bounds.width,110),"completed hidden animation keeps its final layout");
	Check(runtime.LoadDocument(source,"replacement.q4ui",diagnostics) && runtime.SetState({{"shown",true}},error,2),
		"replace a document after caching its layout readbacks");
	Check(runtime.Frame({},2) && runtime.GetBounds("growing",bounds) && Near(bounds.width,10),
		"layout readbacks belong to the replacement document, not retired elements");
	Check(host.errors==0,"hidden presentation updates produce no diagnostics");
}
static void CheckIdentityTransformReuse(TestHost& host) {
	const char* source=R"json({"format":"openq4-ui","version":1,"id":"identity-reuse",
	 "root":{"id":"parent","type":"group","properties":{"width":{"type":"length","unit":"dp","value":100},"height":{"type":"length","unit":"dp","value":100},"transform":{"type":"transform","unit":"dp","value":[0,0,1,1,0]}},
	 "children":[{"id":"child","type":"vector","properties":{"width":{"type":"length","unit":"dp","value":100},"height":{"type":"length","unit":"dp","value":100},"transform":{"type":"transform","unit":"dp","value":[0,0,1,1,0]}},
	 "paths":[{"id":"rectangle","commands":[{"id":"a","op":"move","points":[[10,10]]},{"id":"b","op":"line","points":[[90,10]]},{"id":"c","op":"line","points":[[90,90]]},{"id":"d","op":"line","points":[[10,90]]},{"id":"z","op":"close"}],"fill":{"type":"solid","color":{"type":"color","value":[1,0,0,1]}}}]}]},
	 "timelines":[{"id":"turn","durationMs":9000,"tracks":[
	 {"node":"parent","property":"transform","keys":[{"atMs":0,"value":{"type":"transform","unit":"dp","value":[0,0,1,1,0]}},{"atMs":9000,"value":{"type":"transform","unit":"dp","value":[0,0,1,1,360]}}]},
	 {"node":"child","property":"transform","keys":[{"atMs":0,"value":{"type":"transform","unit":"dp","value":[0,0,1,1,0]}},{"atMs":9000,"value":{"type":"transform","unit":"dp","value":[0,0,1,1,-360]}}]}]}]})json";
	Runtime runtime(host);std::vector<Diagnostic> diagnostics;
	Check(runtime.LoadDocument(source,"identity-reuse.q4ui",diagnostics) && runtime.PlayTimeline("turn",0),"load counter-rotating transform fixture");
	std::vector<std::pair<std::uint64_t,std::uint64_t>> stationary;
	for(int frame=0;frame<=240;++frame){
		host.meshes.clear();host.ClearSamples();Check(runtime.Frame({},frame/60.0),"render counter-rotating transform");
		Check(!host.meshes.empty(),"stationary counter-rotating artwork remains visible");
		for(const auto& [mesh,revision]:host.meshes)Check(revision==1,"identity and null transforms preserve the same submission revision");
		if(!frame) stationary=host.meshes;
		else Check(host.meshes==stationary,"counter-rotation and integer-phase noise preserve mesh identities");
	}
}
static void CheckMovingMaskSourceCache(TestHost& host,bool solid=false) {
	const char* source=R"json({"format":"openq4-ui","version":1,"id":"moving-mask-cache",
	 "state":{"alpha":{"type":"number","initial":0.75}},
	 "bindings":[{"id":"alpha","node":"parent","property":"opacity","value":{"state":"alpha"}}],
	 "root":{"id":"root","type":"group","properties":{"width":{"type":"length","unit":"dp","value":100},"height":{"type":"length","unit":"dp","value":100},"background-color":{"type":"color","value":[0,1,0,1]},"transform":{"type":"transform","unit":"dp","value":[100,100,1,1,0]}},
	 "children":[{"id":"parent","type":"group","properties":{"width":{"type":"length","unit":"dp","value":100},"height":{"type":"length","unit":"dp","value":100},"transform":{"type":"transform","unit":"dp","value":[0,0,1,1,0]},"opacity":{"type":"number","value":0.75}},
	 "mask":{"paths":[{"id":"wedge","commands":[{"id":"a","op":"move","points":[[50,50]]},{"id":"b","op":"line","points":[[0,0]]},{"id":"c","op":"line","points":[[100,0]]},{"id":"z","op":"close"}],"fill":{"type":"linear","from":[0,0],"to":[100,0],"stops":[{"at":0,"color":{"type":"color","value":[1,1,1,0]}},{"at":0.5,"color":{"type":"color","value":[1,1,1,1]}},{"at":1,"color":{"type":"color","value":[1,1,1,0]}}]}}]},
	 "children":[{"id":"child","type":"vector","properties":{"width":{"type":"length","unit":"dp","value":100},"height":{"type":"length","unit":"dp","value":100},"transform":{"type":"transform","unit":"dp","value":[0,0,1,1,0]}},
	 "paths":[{"id":"red","commands":[{"id":"a","op":"move","points":[[10,10]]},{"id":"b","op":"line","points":[[90,10]]},{"id":"c","op":"line","points":[[90,90]]},{"id":"d","op":"line","points":[[10,90]]},{"id":"z","op":"close"}],"fill":{"type":"solid","color":{"type":"color","value":[1,0,0,0.6]}}},
	 {"id":"blue","commands":[{"id":"a","op":"move","points":[[40,10]]},{"id":"b","op":"line","points":[[90,10]]},{"id":"c","op":"line","points":[[90,90]]},{"id":"d","op":"line","points":[[40,90]]},{"id":"z","op":"close"}],"fill":{"type":"solid","color":{"type":"color","value":[0,0,1,0.7]}}}]}]}]},
	 "timelines":[{"id":"turn","durationMs":9000,"tracks":[
	 {"node":"parent","property":"transform","keys":[{"atMs":0,"value":{"type":"transform","unit":"dp","value":[0,0,1,1,0]}},{"atMs":9000,"value":{"type":"transform","unit":"dp","value":[0,0,1,1,360]}}]},
	 {"node":"child","property":"transform","keys":[{"atMs":0,"value":{"type":"transform","unit":"dp","value":[0,0,1,1,0]}},{"atMs":9000,"value":{"type":"transform","unit":"dp","value":[0,0,1,1,-360]}}]}]}]})json";
	Document artwork; std::vector<Diagnostic> artworkDiagnostics;
	Check(artwork.Load(source,artworkDiagnostics),"parse moving mask fixture");
	if (solid) Check(artwork.ReplaceValue("/root/children/0/mask/paths/0/fill",
		R"json({"type":"solid","color":{"type":"color","value":[1,1,1,1]}})json",artworkDiagnostics),"prepare constant-coverage mask");
	std::vector<std::vector<Vertex>> reference;
	host.samplePoints.clear();
	for(int y=0;y<100;y+=10)for(int x=0;x<100;x+=10)host.samplePoints.push_back({x+100.5f,y+100.5001f});
	for(int mode:{0,1,2,3,4}) {
		if (solid && mode>=2) break;
		const bool cached=mode==1 || mode==2 || mode==4;
		host.cacheLayers=cached;host.maskedSourceLayers=cached;host.replayFrames=mode==4;
		host.linearMaskDraws=0;
		Runtime runtime(host);std::vector<Diagnostic> diagnostics;std::string error;
		Check(runtime.LoadDocument(artwork.Source(),"moving-mask-cache.q4ui",diagnostics) && runtime.PlayTimeline("turn",0),"load moving mask over stable overlapping artwork");
		int direct=0,hits=0,replays=0;const int initialErrors=host.errors;
		for(int frame=0;frame<30;++frame) {
			host.linearMasks=mode>=2 && frame!=15;
			if(frame==12)Check(runtime.SetState({{"alpha",.25}},error,frame*.09),"change live masked group opacity");
			Viewport viewport; if(frame>=20){viewport.width=1024;viewport.height=768;}
			host.ClearSamples();host.meshes.clear();Check(runtime.Frame(viewport,frame*.09),"render changing mask source cache");
			if (frame==5) Check(runtime.Statistics().layoutUpdates==0,"decorative counterrotation updates projection without relaying out the document");
			direct+=int(runtime.Statistics().directMaskDraws);
			hits+=int(runtime.Statistics().layerCacheHits);
			replays+=int(runtime.Statistics().frameReplays);
			if(mode==0)reference.push_back(host.layers[0].pixels);
			else for(size_t i=0;i<host.samplePoints.size();++i) {
				const auto& actual=host.layers[0].pixels[i];const auto& expected=reference[frame][i];
				const float tolerance=mode>=2 ? 1.01f/255 : .002f;
				Check(std::abs(actual.r-expected.r)<tolerance && std::abs(actual.g-expected.g)<tolerance &&
					std::abs(actual.b-expected.b)<tolerance && std::abs(actual.a-expected.a)<tolerance,
					"cached source preserves moving gradient mask, overlap, live alpha and resize");
			}
		}
		Check(host.errors==initialErrors,"moving mask cache and resize produce no allocation errors");
		Check(!(cached && (solid || mode==2)) || (direct>=24 && hits>=22),"eligible moving masks reuse stable source pixels across frames");
		Check(solid || mode!=1 || direct==0,"CPU gradient subdivisions retain their mask target for MSAA parity");
		Check(mode<2 || host.linearMaskDraws>=24,"moving masks use analytic host coverage with and without a source cache");
		Check(mode!=4 || replays>=18,"moving masks replay after checking their stationary source projection");
	}
	Check(std::any_of(reference[0].begin(),reference[0].end(),[](const auto& pixel){return pixel.r>.01f || pixel.b>.01f;}),
		"moving-mask reference contains painted overlap");
	host.cacheLayers=false;host.maskedSourceLayers=false;host.linearMasks=false;host.replayFrames=true;host.samplePoints.clear();
}
static void CheckQuietButtonLayout(TestHost& host) {
	const char* source=R"json({"format":"openq4-ui","version":1,"id":"quiet-buttons",
	 "state":{"left":{"type":"number","initial":10}},
	 "bindings":[{"id":"move","node":"button","property":"left","value":{"state":"left"}}],
	 "root":{"id":"root","type":"group","children":[{"id":"button","type":"group",
	 "properties":{"position":{"type":"keyword","value":"absolute"},"left":{"type":"length","unit":"dp","value":10},
	 "top":{"type":"length","unit":"dp","value":10},"width":{"type":"length","unit":"dp","value":50},
	 "height":{"type":"length","unit":"dp","value":50},"background-color":{"type":"color","value":[1,0,0,1]}},
	 "control":{"role":"button","action":"fixture.activate","label":"#str_200013","states":{"default":"rest","hover":"hover","focus":"rest","pressed":"rest","disabled":"rest"}}}]},
	 "timelines":[{"id":"hover","durationMs":100,"tracks":[{"node":"button","property":"background-color","keys":[
	 {"atMs":0,"value":{"type":"color","value":[1,0,0,1]}},{"atMs":100,"value":{"type":"color","value":[0,1,0,1]}}]}]},
	 {"id":"rest","durationMs":1,"tracks":[{"node":"button","property":"background-color","keys":[
	 {"atMs":0,"value":{"type":"color","value":[1,0,0,1]}},{"atMs":1,"value":{"type":"color","value":[1,0,0,1]}}]}]}]})json";
	Runtime runtime(host); std::vector<Diagnostic> diagnostics; std::string error;
	const bool loaded=runtime.LoadDocument(source,"quiet-buttons.q4ui",diagnostics);
	if (!loaded) for (const auto& diagnostic:diagnostics) std::fprintf(stderr,"%s\n",diagnostic.message.c_str());
	Check(loaded,"load quiet button layout fixture");
	for (int frame=0;frame<3;++frame) Check(runtime.Frame({},frame),"settle button layout");
	Check(runtime.Statistics().interactionUpdates==0,"settled buttons reuse their unchanged interaction geometry");
	Check(runtime.Statistics().layoutUpdates==0 && runtime.CanActivateControl("button",2),"idle buttons skip layout without losing eligibility");
	Check(runtime.Statistics().frameReplays==1,"settled interactive buttons replay their paint commands");
	runtime.PointerMove(20,20,3); runtime.Frame({},3.1);
	Check(runtime.Statistics().layoutUpdates==1,"hover feedback invalidates quiet button layout");
	host.ClearSamples(); runtime.Frame({},4);
	Check(runtime.Statistics().layoutUpdates==0 && !host.drawn.empty() && host.drawn.front().g==1,
		"completed hover retains its colour with a stationary pointer");
	host.ClearSamples(); runtime.Frame({},4.1);
	Check(runtime.Statistics().frameReplays==1 && !host.drawn.empty() && host.drawn.front().g==1,
		"stationary pointer preserves current hover while replaying");
	Check(runtime.Statistics().interactionUpdates==0,"stationary pointer does not rebuild unchanged hit geometry");
	Check(runtime.SetState({{"left",100.0}},error,5) && runtime.Frame({},5),"move quiet button through bound state");
	Bounds bounds;
	Check(runtime.Statistics().layoutUpdates==1 && runtime.GetBounds("button",bounds) && Near(bounds.x,100),
		"bound geometry refreshes both layout and interaction");
	Check(runtime.SetControlEnabled("button",false,6) && runtime.Frame({},6) && !runtime.CanActivateControl("button",6),
		"quiet geometry never preserves eligibility after a control is disabled");
}
static void CheckMenuFadeReplay(TestHost& host) {
	const char* source=R"json({"format":"openq4-ui","version":1,"id":"menu-fade",
	 "state":{"alpha":{"type":"number","initial":0.5},"buttonAlpha":{"type":"number","initial":1}},
	 "bindings":[{"id":"fade","node":"picture","property":"opacity","value":{"state":"alpha"}},
	 {"id":"buttonFade","node":"button","property":"opacity","value":{"state":"buttonAlpha"}}],
	 "root":{"id":"root","type":"group","children":[
	 {"id":"button","type":"group","properties":{"background-color":{"type":"color","value":[1,0,0,1]},"position":{"type":"keyword","value":"absolute"},"left":{"type":"length","unit":"dp","value":10},"top":{"type":"length","unit":"dp","value":10},"width":{"type":"length","unit":"dp","value":50},"height":{"type":"length","unit":"dp","value":50},"opacity":{"type":"number","value":1}},"control":{"role":"button","action":"fixture.activate","label":"#str_200013","states":{"default":"rest","hover":"rest","focus":"rest","pressed":"rest","disabled":"rest"}}},
	 {"id":"picture","type":"image","properties":{"position":{"type":"keyword","value":"absolute"},"left":{"type":"length","unit":"dp","value":100},"top":{"type":"length","unit":"dp","value":10},"width":{"type":"length","unit":"dp","value":50},"height":{"type":"length","unit":"dp","value":50},"image":{"type":"image","value":"gfx/test/fade.tga"},"opacity":{"type":"number","value":0.5}}}]},
	 "timelines":[{"id":"rest","durationMs":1,"tracks":[{"node":"button","property":"background-color","keys":[{"atMs":0,"value":{"type":"color","value":[1,0,0,1]}},{"atMs":1,"value":{"type":"color","value":[1,0,0,1]}}]}]}]})json";
	Runtime runtime(host); std::vector<Diagnostic> diagnostics; std::string error;
	Check(runtime.LoadDocument(source,"menu-fade.q4ui",diagnostics),"load a decorative image beside an interactive button");
	runtime.PointerMove(20,20,0);
	for (int frame=0;frame<4;++frame) { host.ClearSamples(); Check(runtime.Frame({},frame*.1),"settle the hovered menu and image"); }
	double time=.4,previousAlpha=.5;
	for (double alpha:{.25,.75,.125,.5,0.0,.25,1.0,.5}) {
		Check(runtime.SetState({{"alpha",alpha}},error,time),"change decorative opacity with a stationary pointer");
		host.ClearSamples(); Check(runtime.Frame({},time),"draw a live menu image fade");
		const bool reusable=previousAlpha>0 && previousAlpha<1 && alpha>0 && alpha<1;
		Check(runtime.Statistics().layoutUpdates==0 && runtime.Statistics().frameReplays==(reusable ? 1 : 0),"decorative menu alpha replays without relaying out controls");
		Check(host.imageVertices.empty()==(alpha==0),"menu image fade preserves zero-opacity suppression");
		for (const auto& v:host.imageVertices) Check(std::abs(v.r-alpha)<.00001 && std::abs(v.a-alpha)<.00001,"replayed image uses the current opacity");
		Check(runtime.GetControlState("button")==ControlState::Hover,"decorative fades preserve hover state");
		const auto replayed=host.imageVertices;
		host.replayFrames=false; host.ClearSamples(); Check(runtime.Frame({},time),"render the menu fade without replay");
		Check(replayed.size()==host.imageVertices.size(),"replay and normal image fade have identical topology");
		for (size_t i=0;i<replayed.size();++i) {
			const auto& a=replayed[i]; const auto& b=host.imageVertices[i];
			Check(a.x==b.x && a.y==b.y && a.u==b.u && a.v==b.v && a.r==b.r && a.g==b.g && a.b==b.b && a.a==b.a,
				"replay and normal fitted image fade preserve geometry and colour");
		}
		host.replayFrames=true; host.ClearSamples(); Check(runtime.Frame({},time),"record the current menu fade again");
		previousAlpha=alpha;
		time+=.1;
	}
	Check(runtime.SetState({{"buttonAlpha",0.0}},error,time),"change the interactive button's opacity");
	Check(runtime.Frame({},time) && runtime.Statistics().layoutUpdates>0,"interactive opacity keeps full layout and interaction validation");
}
static void CheckDecorativeTransforms(TestHost& host) {
	const char* source=R"json({"format":"openq4-ui","version":1,"id":"decorative-transforms",
	 "root":{"id":"root","type":"group","children":[
	 {"id":"button","type":"group","properties":{"background-color":{"type":"color","value":[1,0,0,1]},"position":{"type":"keyword","value":"absolute"},"left":{"type":"length","unit":"dp","value":10},"top":{"type":"length","unit":"dp","value":10},"width":{"type":"length","unit":"dp","value":50},"height":{"type":"length","unit":"dp","value":50},"transform":{"type":"transform","unit":"dp","value":[0,0,1,1,0]}},"control":{"role":"button","action":"fixture.activate","label":"#str_200013","states":{"default":"rest","hover":"rest","focus":"rest","pressed":"rest","disabled":"rest"}}},
	 {"id":"decoration","type":"group","properties":{"position":{"type":"keyword","value":"absolute"},"left":{"type":"length","unit":"dp","value":200},"top":{"type":"length","unit":"dp","value":10},"width":{"type":"length","unit":"dp","value":50},"height":{"type":"length","unit":"dp","value":50},"background-color":{"type":"color","value":[0,0,1,1]},"transform":{"type":"transform","unit":"dp","value":[0,0,1,1,0]}}}]},
	 "timelines":[{"id":"rest","durationMs":1,"tracks":[{"node":"button","property":"background-color","keys":[{"atMs":0,"value":{"type":"color","value":[1,0,0,1]}},{"atMs":1,"value":{"type":"color","value":[1,0,0,1]}}]}]},{"id":"decor","durationMs":1000,"tracks":[{"node":"decoration","property":"transform","keys":[{"atMs":0,"value":{"type":"transform","unit":"dp","value":[0,0,1,1,0]}},{"atMs":1000,"value":{"type":"transform","unit":"dp","value":[120,0,1,1,0]}}]}]},
	 {"id":"control","durationMs":1000,"tracks":[{"node":"button","property":"transform","keys":[{"atMs":0,"value":{"type":"transform","unit":"dp","value":[0,0,1,1,0]}},{"atMs":1000,"value":{"type":"transform","unit":"dp","value":[40,0,1,1,0]}}]}]}]})json";
	Runtime runtime(host); std::vector<Diagnostic> diagnostics;
	const auto left=[&](bool blue) { float x=1e6f; for(const auto& v:host.drawn) if(blue ? v.b>.99f && v.r<.01f : v.r>.99f && v.b<.01f) x=std::min(x,v.x); return x; };
	const bool loaded=runtime.LoadDocument(source,"decorative-transforms.q4ui",diagnostics);
	if (!loaded) for (const auto& diagnostic:diagnostics) std::fprintf(stderr,"%s\n",diagnostic.message.c_str());
	Check(loaded,"load decorative transform beside interactive button");
	Check(runtime.Frame({},0) && runtime.Frame({},1),"settle decorative transform fixture");
	Check(runtime.PlayTimeline("decor",2) && runtime.Frame({},2.1),"start decorative projection");
	host.ClearSamples(); Check(runtime.Frame({},2.5),"advance decorative projection");
	Check(runtime.Statistics().layoutUpdates==0 && Near(left(true),260),
		"decorative motion updates projected geometry without relaying out the document");
	Check(runtime.Statistics().frameReplays==0,"a projected decoration redraws instead of replaying stale geometry");
	Check(runtime.Statistics().interactionUpdates==0,"decorative projection preserves unrelated control hit bounds");
	runtime.PointerMove(20,20,2.5);
	Check(runtime.GetControlState("button")==ControlState::Hover,"unchanged control remains hittable beside decorative motion");
	Check(runtime.PlayTimeline("control",3) && runtime.Frame({},3.1),"start interactive projection");
	host.ClearSamples(); Check(runtime.Frame({},3.5),"advance interactive projection");
	Check(runtime.Statistics().layoutUpdates==1 && runtime.Statistics().interactionUpdates>0 &&
		Near(left(false),30),"interactive motion still refreshes layout and hit geometry");
	runtime.PointerMove(75,20,3.5);
	Check(runtime.GetControlState("button")==ControlState::Hover,"pointer hits the translated control outside its old bounds");
	// Compare the typed animation property against an independently parsed
	// CSS pose, including density, reflection, nonuniform scale and rotation.
	for (const auto* unit:{"dp","px"}) for (float density:{1.f,1.5f,2.f}) {
		std::string typedSource=source;
		const std::string from="\"type\":\"transform\",\"unit\":\"dp\"",to=std::string("\"type\":\"transform\",\"unit\":\"")+unit+"\"";
		for(size_t at=0;(at=typedSource.find(from,at))!=std::string::npos;at+=to.size()) typedSource.replace(at,from.size(),to);
		Document fixture; Check(fixture.Load(typedSource,diagnostics),"load typed transform fixture with consistent timeline units");
		Check(fixture.ReplaceValue("/timelines/1/tracks/0/keys/1/value/value","[36.5,-15,-2.5,1.5,74]",diagnostics),"set reflected transform motion");
		Runtime animated(host),reference(host); Viewport viewport; viewport.displayScale=density;
		Check(animated.LoadDocument(fixture.Source(),"typed-transform.q4ui",diagnostics),"load typed transform runtime");
		Check(animated.Frame(viewport,0) && animated.Frame(viewport,1) && animated.PlayTimeline("decor",2) &&
			animated.Frame(viewport,2.1),"settle typed transform runtime");
		host.ClearSamples(); Check(animated.Frame(viewport,2.5),"advance typed reflected transform");
		Check(animated.Statistics().layoutUpdates==0,"typed transform preserves the projection-only update");
		const auto blue=[&] { std::vector<Vertex> result; for(const auto& v:host.drawn) if(v.b>.99f && v.r<.01f) result.push_back(v); return result; };
		const auto painted=blue(); Check(!painted.empty(),"typed transformed artwork stays visible");
		Check(fixture.ReplaceValue("/root/children/1/properties/transform/value","[18.25,-7.5,-0.75,1.25,37]",diagnostics),"pose CSS transform reference");
		Check(reference.LoadDocument(fixture.Source(),"css-transform.q4ui",diagnostics),"load independent CSS transform reference");
		host.ClearSamples(); Check(reference.Frame(viewport,2.5),"render independent CSS transform reference");
		const auto compare=[&] {
			const auto expected=blue(); Check(expected.size()==painted.size(),"typed and CSS transform topology matches");
			for(size_t i=0;i<expected.size();++i) Check(std::abs(expected[i].x-painted[i].x)<.001f &&
				std::abs(expected[i].y-painted[i].y)<.001f,"typed and CSS transform units/order/float projection match");
		};
		compare();
		std::string snapshot,error;
		Check(animated.SaveSnapshot(snapshot,error,2.5) && animated.Frame(viewport,2.8) &&
			animated.RestoreSnapshot(snapshot,error,3),"restore typed motion through ordinary CSS adoption");
		host.ClearSamples(); Check(animated.Frame(viewport,3),"render restored typed transform"); compare();
	}
}
static void CheckCompoundPlateCache(TestHost& host) {
	const char* source=R"json({"format":"openq4-ui","version":1,"id":"plate-cache",
	 "state":{"alpha":{"type":"number","initial":0.5}},
	 "bindings":[{"id":"alpha","node":"plate","property":"opacity","value":{"state":"alpha"}}],
	 "root":{"id":"root","type":"group","properties":{"width":{"type":"length","unit":"dp","value":200},"height":{"type":"length","unit":"dp","value":100},"background-color":{"type":"color","value":[0,1,0,1]}},
	 "children":[{"id":"plate","type":"vector","properties":{"width":{"type":"length","unit":"dp","value":100},"height":{"type":"length","unit":"dp","value":50},"opacity":{"type":"number","value":0.5}},
	 "paths":[
	 {"id":"fill","commands":[{"id":"a","op":"move","points":[[10,10]]},{"id":"b","op":"line","points":[[90,10]]},{"id":"c","op":"line","points":[[90,40]]},{"id":"d","op":"line","points":[[10,40]]},{"id":"z","op":"close"}],"fill":{"type":"solid","color":{"type":"color","value":[1,0,0,1]}}},
	 {"id":"rail","commands":[{"id":"a","op":"move","points":[[40,10]]},{"id":"b","op":"line","points":[[90,10]]},{"id":"c","op":"line","points":[[90,40]]},{"id":"d","op":"line","points":[[40,40]]},{"id":"z","op":"close"}],"fill":{"type":"solid","color":{"type":"color","value":[0,0,1,1]}}}
	 ]}]}})json";
	Runtime runtime(host); std::vector<Diagnostic> diagnostics; std::string error;
	const auto near=[](float actual,float expected){return std::abs(actual-expected)<.002f;};
	Check(runtime.LoadDocument(source,"plate-cache.q4ui",diagnostics),"load overlapping plate cache fixture");
	host.cacheLayers=true; host.replayFrames=false; host.samplePoints={{20.25f,20.25f},{60.25f,20.25f}};
	for (int frame=0;frame<3;++frame) { host.ClearSamples(); Check(runtime.Frame({},frame),"render compound plate"); }
	Check(runtime.Statistics().layerCacheHits==1 && runtime.Statistics().layerPushes==0,"stable compound plate reuses its completed target");
	Check(near(host.layers[0].pixels[0].r,.5f) && near(host.layers[0].pixels[0].g,.5f) &&
		near(host.layers[0].pixels[1].r,0) && near(host.layers[0].pixels[1].g,.5f) && near(host.layers[0].pixels[1].b,.5f),
		"overlapping plate applies isolated alpha once");
	Check(runtime.SetState({{"alpha",.25}},error,3),"change cached plate alpha");
	host.ClearSamples(); runtime.Frame({},3);
	Check(runtime.Statistics().layerCacheHits==1 && near(host.layers[0].pixels[1].g,.75f) && near(host.layers[0].pixels[1].b,.25f),
		"cached plate uses live alpha without accumulating tint");
	host.cacheLayers=false; host.replayFrames=true; host.samplePoints.clear();
}

// A detail card. The subject is the control the pointer is over after the
// pointer moved, the focused control after keyboard or controller input, and
// none on a control without a detail; its detail goes to the detail state,
// whose binding shows the card. The card sits 6 dp under a focused control,
// over it near the view's edge, 6 dp from the pointer, with its trailing edge
// 28 dp past the pointer beyond 555 dp, and always 16 dp inside the view.
static void CheckDetailCard(TestHost& host) {
	const auto box = [](const char* id, double left, double top, double width, double height) {
		return std::string(R"({"id":")")+id+R"(","type":"group","properties":{"position":{"type":"keyword","value":"absolute"},)"
			R"("opacity":{"type":"number","value":1},"left":{"type":"length","value":)"+std::to_string(left)+R"(,"unit":"dp"},"top":{"type":"length","value":)"+std::to_string(top)+
			R"(,"unit":"dp"},"width":{"type":"length","value":)"+std::to_string(width)+R"(,"unit":"dp"},"height":{"type":"length","value":)"+
			std::to_string(height)+R"(,"unit":"dp"})";
	};
	// A button with its feedback timeline (every state lights its own box).
	const auto button = [](const char* id, const char* detail) {
		const std::string lit = std::string(id)+"-lit";
		return std::string(R"(,"control":{"role":"button","event":"pick","label":"#str_)")+id+R"(","states":{"default":")"+lit+
			R"(","hover":")"+lit+R"(","focus":")"+lit+R"(","pressed":")"+lit+R"(","disabled":")"+lit+R"("})"+detail+"}}";
	};
	const auto lit = [](const char* id) {
		return std::string(R"({"id":")")+id+R"(-lit","durationMs":100,"tracks":[{"node":")"+id+R"(","property":"opacity","keys":[)"
			R"({"atMs":0,"value":{"type":"number","value":1}},{"atMs":100,"value":{"type":"number","value":0.8}}]}]})";
	};
	const std::string source = R"json({"format":"openq4-ui","version":1,"id":"detail-card",
	 "state":{"subject":{"type":"number","initial":-1},"picked":{"type":"boolean","initial":false}},
	 "root":{"id":"root","type":"group","properties":{"width":{"type":"length","value":100,"unit":"%"},"height":{"type":"length","value":100,"unit":"%"}},
	  "children":[)json"+box("upper",40,40,1220,30)+"}"+button("upper",R"(,"detail":0)")+","+
	   box("lower",40,650,300,30)+"}"+button("lower",R"(,"detail":1)")+","+
	   box("plain",40,300,300,30)+"}"+button("plain","")+","+
	   box("card",0,0,200,100)+R"json(,"display":{"type":"keyword","value":"none"},"pointer-events":{"type":"keyword","value":"none"}}}]},
	 "timelines":[)json"+lit("upper")+","+lit("lower")+","+lit("plain")+R"json(],
	 "bindings":[{"id":"shown","node":"card","property":"display","value":{"op":"select","args":[{"op":">=","args":[{"state":"subject"},0]},"block","none"]}}],
	 "events":{"pick":[{"op":"setState","values":{"picked":true}}]},
	 "detail":{"state":"subject","card":"card"}})json";
	Runtime runtime(host);
	std::vector<Diagnostic> diagnostics;
	const bool loaded = runtime.Initialize() && runtime.LoadDocument(source,"detail-card.q4ui",diagnostics);
	for (const auto& d : diagnostics) std::fprintf(stderr,"%s %s\n",d.pointer.c_str(),d.message.c_str());
	Check(loaded,"the detail card document loads");
	Viewport viewport; viewport.width = 1280; viewport.height = 720;
	const auto subject = [&] { StateValue value; return runtime.GetStateValue("subject",value) ? std::get<double>(value) : -99.0; };
	const auto card = [&] { Bounds b{}; runtime.GetBounds("card",b); return b; };
	// A hidden element keeps its last box; whether it shows is its presented display.
	const auto shown = [&] { const auto display = runtime.PresentedValue("card","display"); return display && display->text != "none"; };
	Check(runtime.FocusControl("upper",1),"focus the upper control");
	runtime.Frame(viewport,1.1);
	Check(subject() == 0 && shown() && Near(card().x,40) && Near(card().y,76) && Near(card().width,200),
		"the focused control's detail shows the card 6 dp under it, at its leading edge");
	Check(runtime.FocusControl("lower",1.2),"focus the lower control");
	runtime.Frame(viewport,1.3);
	Check(subject() == 1 && Near(card().x,40) && Near(card().y,650-6-100),"near the view's edge the card sits over the control");
	Check(runtime.FocusControl("plain",1.4),"focus a control without a detail");
	runtime.Frame(viewport,1.5);
	Check(subject() == -1 && !shown(),"a control without a detail hides the card");
	runtime.PointerMove(100,50,1.6); runtime.Frame(viewport,1.7);
	Check(subject() == 0 && Near(card().x,106) && Near(card().y,56),"the pointer's control is the subject; the card sits 6 dp from the pointer");
	runtime.PointerMove(700,55,1.8); runtime.Frame(viewport,1.9);
	Check(subject() == 0 && Near(card().x,700+28-200),"past 555 dp the card's trailing edge sits 28 dp past the pointer");
	runtime.PointerMove(1250,60,2.0); runtime.Frame(viewport,2.1);
	Check(subject() == 0 && Near(card().x,1280-16-200),"and stays 16 dp inside the view");
	runtime.PointerMove(900,500,2.2); runtime.Frame(viewport,2.3);
	Check(subject() == -1 && !shown(),"the pointer over no control hides the card");
	Check(runtime.FocusedControl() == "plain","the card never takes the focus");
	runtime.MenuAction(MenuInput::Up,true,2.4); runtime.MenuAction(MenuInput::Up,false,2.41); runtime.Frame(viewport,2.5);
	Check(runtime.FocusedControl() != "plain" && subject() == (runtime.FocusedControl() == "upper" ? 0 : 1),
		"keyboard or controller input makes the focused control the subject again");
	StateValues values = runtime.GetState(false);
	Check(values.contains("subject") && runtime.GetStateValue("picked",values["picked"]) && !runtime.GetStateValue("absent",values["subject"]),
		"one state reads without copying the rest; an undeclared one is refused");
}
// Accept's verb follows focus (section 13.4): the focused button's prompt,
// a key or a string expression over state, the caller's fallback for a
// control that names none, and nothing while no focused control can act.
static void CheckAcceptPrompts(TestHost& host) {
	const auto button = [](const char* id, double top, const std::string& extra) {
		return std::string(R"({"id":")")+id+R"(","type":"group","properties":{"position":{"type":"keyword","value":"absolute"},)"
			R"("left":{"type":"length","value":40,"unit":"dp"},"top":{"type":"length","value":)"+std::to_string(top)+R"(,"unit":"dp"},)"
			R"("width":{"type":"length","value":300,"unit":"dp"},"height":{"type":"length","value":30,"unit":"dp"},)"
			R"("opacity":{"type":"number","value":1}},)"
			R"("control":{"role":"button","event":"pick","label":"#str_200013","states":{"default":")"+id+R"(-rest","hover":")"+id+
			R"(-rest","focus":")"+id+R"(-rest","pressed":")"+id+R"(-rest","disabled":")"+id+R"(-rest"})"+extra+"}}";
	};
	const auto rest = [](const char* id) {
		return std::string(R"({"id":")")+id+R"(-rest","durationMs":1,"tracks":[{"node":")"+id+R"(","property":"opacity","keys":[)"
			R"({"atMs":0,"value":{"type":"number","value":1}},{"atMs":1,"value":{"type":"number","value":1}}]}]})";
	};
	const auto document = [&](const std::string& extra) {
		return std::string(R"json({"format":"openq4-ui","version":1,"id":"accept-prompts",
		 "state":{"chosen":{"type":"boolean","initial":false},"verb":{"type":"string","initial":"#str_231521"},
		  "picked":{"type":"boolean","initial":false}},
		 "root":{"id":"root","type":"group","properties":{"width":{"type":"length","value":100,"unit":"%"},"height":{"type":"length","value":100,"unit":"%"}},
		  "children":[)json")+button("named",40,R"(,"prompt":"#str_231520")")+","+button("plain",80,"")+","+
			button("chosen",120,R"(,"prompt":{"op":"select","args":[{"state":"chosen"},"#str_231522","#str_231042"]})")+","+
			button("stated",160,R"(,"prompt":{"state":"verb"})"+extra)+R"json(]},
		 "timelines":[)json"+rest("named")+","+rest("plain")+","+rest("chosen")+","+rest("stated")+R"json(],
		 "events":{"pick":[{"op":"setState","values":{"picked":true}}]}})json";
	};
	const auto loads = [](const std::string& source) { Document parsed; std::vector<Diagnostic> found; return parsed.Load(source,found); };
	Runtime runtime(host);
	std::vector<Diagnostic> diagnostics; std::string error;
	const bool loaded = runtime.Initialize() && runtime.LoadDocument(document(""),"accept-prompts.q4ui",diagnostics);
	for (const auto& d : diagnostics) std::fprintf(stderr,"%s %s\n",d.pointer.c_str(),d.message.c_str());
	Check(loaded,"the accept prompt document loads");
	Viewport viewport; viewport.width = 1280; viewport.height = 720;
	runtime.Frame(viewport,1);
	const std::string select = "#str_231042";
	Check(runtime.FocusedControl().empty() && runtime.AcceptPrompt(select,1.1).empty(),"nothing focused leaves nothing to accept");
	Check(runtime.FocusControl("named",1.2) && runtime.AcceptPrompt(select,1.2) == "#str_231520","a button names its own verb");
	Check(runtime.FocusControl("plain",1.3) && runtime.AcceptPrompt(select,1.3) == select,"a button without one keeps the bar's verb");
	Check(runtime.FocusControl("chosen",1.4) && runtime.AcceptPrompt(select,1.4) == select,"an expression chooses the verb from state");
	Check(runtime.SetState({{"chosen",true}},error,1.5) && runtime.AcceptPrompt(select,1.5) == "#str_231522","and follows it as it changes");
	Check(runtime.FocusControl("stated",1.6) && runtime.AcceptPrompt(select,1.6) == "#str_231521","a verb can come from a string state");
	Check(runtime.SetState({{"verb",std::string("Load")}},error,1.7) && runtime.AcceptPrompt(select,1.7) == select,
		"a state that holds no localization key keeps the bar's verb");
	Check(runtime.FocusControl("named",1.8) && runtime.SetControlEnabled("named",false,1.8) && runtime.AcceptPrompt(select,1.8).empty(),
		"a focused control that cannot act leaves nothing to accept");
	// Only buttons take a prompt, and a literal one must be a localization key.
	Check(!loads(document(R"(,"extra":1)")),"unknown control fields stay refused");
	std::string literal = document(""); literal.replace(literal.find("\"#str_231520\""),13,"\"Load\"");
	Check(!loads(literal),"a literal prompt must be a localization key");
	std::string numeric = document(""); numeric.replace(numeric.find("\"#str_231520\""),13,"7");
	Check(!loads(numeric),"a prompt must be text");
}
// A document parsed once serves both a GUI and its view: a copy shares the
// parsed source and model, an edit of one copy replaces only its own, and a
// runtime loaded from the parsed document presents as one loaded from the
// source.
static void CheckSharedDocument(TestHost& host) {
	const std::string source = R"json({"format":"openq4-ui","version":1,"id":"shared",
	 "state":{"wide":{"type":"boolean","initial":true}},
	 "root":{"id":"root","type":"group","properties":{"width":{"type":"length","value":100,"unit":"%"},"height":{"type":"length","value":100,"unit":"%"}},
	  "children":[{"id":"bar","type":"group","properties":{"position":{"type":"keyword","value":"absolute"},
	   "left":{"type":"length","value":20,"unit":"dp"},"top":{"type":"length","value":30,"unit":"dp"},
	   "width":{"type":"length","value":100,"unit":"dp"},"height":{"type":"length","value":10,"unit":"dp"},"opacity":{"type":"number","value":1}}}]},
	 "bindings":[{"id":"barFade","node":"bar","property":"opacity","value":{"op":"select","args":[{"state":"wide"},0.5,1]}}]})json";
	std::vector<Diagnostic> diagnostics;
	Document parsed;
	Check(parsed.Load(source,diagnostics),"the shared document loads");
	Document copy = parsed;
	Check(&copy.Model() == &parsed.Model() && copy.Source() == parsed.Source(),"a copy shares the parsed source and model");
	Check(copy.ReplaceValue("/root/children/0/properties/left/value","40",diagnostics) &&
		&copy.Model() != &parsed.Model() && parsed.Source() == source && parsed.Model().FindNode("bar") != nullptr,
		"an edit replaces only the edited copy's document");
	Runtime fromSource(host), fromDocument(host), fromNothing(host);
	Check(fromSource.Initialize() && fromSource.LoadDocument(source,"shared.q4ui",diagnostics),"load the shared document from its source");
	Check(fromDocument.Initialize() && fromDocument.LoadDocument(parsed,"shared.q4ui",diagnostics) && diagnostics.empty(),
		"load the shared document as parsed");
	Check(fromNothing.Initialize() && !fromNothing.LoadDocument(Document(),"empty.q4ui",diagnostics) && !fromNothing.IsLoaded(),
		"a document that never loaded is refused");
	Viewport viewport; viewport.width = 1280; viewport.height = 720;
	fromSource.Frame(viewport,1); fromDocument.Frame(viewport,1);
	Bounds a{}, b{};
	const auto fadeA = fromSource.PresentedValue("bar","opacity"), fadeB = fromDocument.PresentedValue("bar","opacity");
	Check(fromSource.GetBounds("bar",a) && fromDocument.GetBounds("bar",b) && Near(a.x,b.x) && Near(a.y,b.y) && Near(a.width,b.width) &&
		fadeA && fadeB && std::abs(fadeA->data[0]-0.5) < 0.01 && std::abs(fadeB->data[0]-0.5) < 0.01 && fromSource.GetState() == fromDocument.GetState(),
		"a runtime loaded from the parsed document presents as one loaded from the source");
}

static void CheckPublicTextQueries(TestHost& host) {
	const std::string source=R"json({"format":"openq4-ui","version":1,"id":"text-fit",
	 "state":{"key":{"type":"string","initial":"","keyBinding":"savegame quick"}},
	 "root":{"id":"root","type":"group","children":[
	  {"id":"label","type":"text","properties":{"width":{"type":"length","value":25,"unit":"dp"},
	   "height":{"type":"length","value":30,"unit":"dp"},"padding":{"type":"length","value":10,"unit":"dp"},
	   "font-size":{"type":"length","value":20,"unit":"dp"},"text":{"type":"text","value":"#str_test"}}},
	  {"id":"narrow","type":"text","properties":{"width":{"type":"length","value":5,"unit":"dp"},
	   "font-size":{"type":"length","value":20,"unit":"dp"},"text":{"type":"text","value":"#str_test"}}},
	  {"id":"hidden","type":"text","properties":{"width":{"type":"length","value":25,"unit":"dp"},
	   "display":{"type":"keyword","value":"none"},"text":{"type":"text","value":"#str_test"}}}
	 ]}})json";
	std::vector<Diagnostic> diagnostics;
	Document document; const bool parsed=document.Load(source,diagnostics);
	if (!parsed) for (const auto& diagnostic:diagnostics) std::fprintf(stderr,"%s: %s\n",diagnostic.pointer.c_str(),diagnostic.message.c_str());
	Check(parsed,"key-binding text query document compiles");
	Check(document.Model().state.at("key").keyBinding=="savegame quick" && !document.Model().state.at("key").HostSourced(),
		"binding lookup is explicit application text, including argument-bearing commands");
	for (const auto& value:{"0","\"\"","\"bad\\nline\"","\"bad\\u0000tail\""}) {
		Document bad=document;
		Check(!bad.ReplaceValue("/state/key/keyBinding",value,diagnostics) && bad.Source()==source,"invalid binding declaration preserves source");
	}
	Document bad=document;
	Check(!bad.ReplaceValue("/state/key",R"({"type":"string","initial":"","cvar":"x","keyBinding":"_forward"})",diagnostics),
		"binding state cannot also be CVar sourced");
	Runtime runtime(host); Check(runtime.LoadDocument(document,"text-fit.q4ui",diagnostics),"load real query layout");
	Viewport viewport; Check(runtime.Frame(viewport,1),"lay out text queries");
	TextFit fit;
	Check(runtime.QueryTextFit("label","a b",fit) && fit.overflow && fit.maxIndex==2 && fit.lastWhitespace==1,
		"query uses content width excluding padding and preserves the last fitting space");
	Check(runtime.QueryTextFit("label","a\xc3\xa9\xf0\x9f\x98\x80",fit) && fit.overflow && fit.maxIndex==3 && fit.lastWhitespace==-1,
		"overflow returns whole UTF-8 source bytes, never a split multibyte scalar");
	Check(runtime.QueryTextFit("narrow","\xf0\x9f\x98\x80x",fit) && fit.overflow && fit.maxIndex==4,
		"first overwide scalar remains whole so a wrapping caller progresses");
	Check(runtime.QueryTextFit("label","ab",fit) && !fit.overflow && fit.maxIndex==-1,
		"fitting text resets an earlier overflow result");
	Check(runtime.QueryTextFit("label","",fit) && !fit.overflow && fit.lastWhitespace==-1,"empty text fits");
	fit={true,99,98};
	for (const auto* id:{"missing","root","hidden"}) Check(!runtime.QueryTextFit(id,"abc",fit) && fit.maxIndex==99 && fit.lastWhitespace==98,
		"missing, non-text and hidden nodes leave output untouched");
	for (const auto& text:{std::string("\xc0\x80",2),std::string("a\0b",3),std::string(65537,'x')})
		Check(!runtime.QueryTextFit("label",text,fit) && fit.maxIndex==99,"invalid and oversized query text leaves output untouched");
	viewport.displayScale=2; Check(runtime.Frame(viewport,2) && runtime.QueryTextFit("label","abc",fit) && fit.maxIndex==2,
		"density scales content and typography together");
	viewport.textScale=2; Check(runtime.Frame(viewport,3) && runtime.QueryTextFit("label","abc",fit) && fit.maxIndex==1,
		"text scaling changes the actual fitting byte index");
	Runtime other(host); Check(other.LoadDocument(document,"other-fit.q4ui",diagnostics) && other.Frame({},1),"second independent text context");
	Check(other.QueryTextFit("label","abc",fit) && fit.maxIndex==2 && runtime.QueryTextFit("label","abc",fit) && fit.maxIndex==1,
		"queries retain each view's density and text scale");
	runtime.Shutdown(); fit={true,99,98};
	Check(!runtime.QueryTextFit("label","abc",fit) && fit.maxIndex==99,"closed context cannot return stale metrics");
}

int main(int argc, char** argv) {
	CheckInitializationPacing();
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
	{
		std::string rigidSource = vectorDocument;
		rigidSource.insert(rigidSource.find("\"id\":\"rectangle\""),"\"antialias\":false,");
		Document rigid;
		Check(rigid.Load(rigidSource,diagnostics),"load local triangle cache fixture");
		Check(rigid.ReplaceValue("/root/children/0/properties/transform/value","[10,0,1,1,0]",diagnostics),"pose the rotation's starting position");
		Check(rigid.ReplaceValue("/timelines/2/tracks/0/keys/1/value/value","[10.5,0,1,1,70]",diagnostics),"rotate and translate cached artwork");
		Check(runtime.LoadDocument(rigid.Source(),"rigid.q4ui",diagnostics),"load rigid vector runtime");
		runtime.Frame(viewport,14);
		Check(runtime.PlayTimeline("fractional",14),"start rigid motion");
		for (int frame = 1; frame <= 30; ++frame) {
			host.drawn.clear(); runtime.Frame(viewport,14+frame/60.0);
			Check(runtime.Statistics().vectorPathsCompiled == 0 && runtime.Statistics().vectorUploads == 0,"rotation reuses soft geometry without recompilation or upload");
			for (const auto& v : host.drawn) Check(v.x >= 199.999f && v.x <= 400.001f && v.y >= 199.999f && v.y <= 400.001f,"cached rotations retain the live parent clip");
		}
		const auto rotated = host.drawn;
		Check(!rotated.empty(),"rotated artwork remains visible");
		Check(rigid.ReplaceValue("/root/children/0/properties/transform/value","[10.25,0,1,1,35]",diagnostics),"pose independent rotation reference");
		Check(runtime.LoadDocument(rigid.Source(),"rigid-reference.q4ui",diagnostics),"load fresh rotation reference");
		host.drawn.clear(); runtime.Frame(viewport,14.5);
		Check(host.drawn.size() == rotated.size(),"fresh and cached rotated topology matches");
		for (size_t i = 0; i < rotated.size(); ++i) {
			Check(Near(host.drawn[i].x,rotated[i].x) && Near(host.drawn[i].y,rotated[i].y) &&
				std::abs(host.drawn[i].a-rotated[i].a)<.0001f,"cached transform matches independent geometry and opacity");
		}
		viewport.displayScale = 3; runtime.Frame(viewport,14.75);
		Check(runtime.Statistics().vectorPathsCompiled > 0,"density changes recompile local artwork at the new curve precision");
		viewport.displayScale = 2;
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
	auto sampleFrame = [&](double time) { host.ClearSamples(); return runtime.Frame(viewport,time); };
	auto pixel = [&](size_t index, float r, float g, float b) {
		const auto& v = host.layers[0].pixels[index];
		Check(std::abs(v.r-r)<.002f && std::abs(v.g-g)<.002f && std::abs(v.b-b)<.002f && std::abs(v.a-1)<.002f,"isolated source-over matches independent overlap color");
	};
	sampleFrame(15); pixel(0,.5f,.5f,0); pixel(1,.25f,.5f,.25f); pixel(2,0,1,0);
	Check(runtime.PlayTimeline("fade",15),"animate isolated parent opacity");
	sampleFrame(15.5); pixel(0,.25f,.75f,0); pixel(1,.125f,.75f,.125f); pixel(2,0,1,0);
	host.failLayer = true;
	Check(!sampleFrame(15.6),"compositor failure is returned to the caller for same-frame fallback");
	Check(host.errors == 1 && host.activeLayer == 0,"allocation failure diagnosed and output target restored");
	--host.errors; host.failLayer = false;
	Check(sampleFrame(15.5),"composition recovers after target allocation becomes available"); // Monotonic clock keeps the already advanced .6 sample.
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
	for (int allocation = 0; allocation < 6; ++allocation) {
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
	// A completed leaf mask can reuse its target without freezing paint,
	// changing subtree alpha, or allowing a resized view to reuse old pixels.
	Document cachedMask;
	Check(cachedMask.Load(maskedDocument,diagnostics) &&
		cachedMask.ReplaceValue("/root/children/0/children","[]",diagnostics),"build leaf-mask cache fixture");
	Check(runtime.LoadDocument(cachedMask.Source(),"cached-mask.q4ui",diagnostics),"load leaf-mask cache fixture");
	viewport={}; host.cacheLayers=true;
	host.samplePoints={{20.37f,20.63f},{70.37f,50.63f},{12.37f,12.63f},{19.37f,20.63f}};
	sampleFrame(19); pixel(0,.5f*maskAlpha,1-.5f*maskAlpha,0); pixel(1,0,1,0); pixel(2,0,1,0);
	Check(runtime.Statistics().layerCacheHits==0,"first masked frame waits for stable geometry");
	sampleFrame(19.05); pixel(0,.5f*maskAlpha,1-.5f*maskAlpha,0);
	Check(runtime.Statistics().layerCacheHits==0,"stable masked frame populates cache");
	sampleFrame(19.1); pixel(0,.5f*maskAlpha,1-.5f*maskAlpha,0); pixel(3,32.f/255,1-32.f/255,0);
	Check(runtime.Statistics().layerCacheHits==1 && runtime.Statistics().layerPushes==0,"unchanged leaf mask avoids target clears and mask rendering");
	Check(runtime.PlayTimeline("fade",19.1),"cached subtree can still fade");
	sampleFrame(19.6); pixel(0,.25f*maskAlpha,1-.25f*maskAlpha,0); pixel(1,0,1,0);
	Check(runtime.Statistics().layerCacheHits==1,"opacity changes reuse masked pixels but composite with current alpha");
	viewport.width=640; viewport.height=480;
	sampleFrame(19.6); pixel(0,.25f*maskAlpha,1-.25f*maskAlpha,0);
	Check(runtime.Statistics().layerCacheHits==0,"resize invalidates completed targets");
	Check(cachedMask.ReplaceValue("/root/children/0/properties/background-color/value","[0,0,1,1]",diagnostics) &&
		runtime.LoadDocument(cachedMask.Source(),"cached-mask.q4ui",diagnostics),"replace cached paint");
	sampleFrame(19.7); pixel(0,0,1-.5f*maskAlpha,.5f*maskAlpha);
	Check(runtime.Statistics().layerCacheHits==0,"changed geometry cannot replay old paint");
	// Multiple contexts share a bounded host. A resting view's completed
	// panel must yield its cache lease when another view needs scratch space.
	Check(runtime.LoadDocument(cachedMask.Source(),"cache-pressure-a.q4ui",diagnostics),"reload cache pressure fixture");
	{
		Runtime other(host);
		Check(other.LoadDocument(cachedMask.Source(),"cache-pressure-b.q4ui",diagnostics),"load second cache pressure view");
		host.maxLayerId=4;
		const int errorsBefore=host.errors;
		for (int frame=0;frame<4;++frame) { host.ClearSamples(); runtime.Frame(viewport,19.7); }
		Check(runtime.Statistics().frameReplays==1,"masked presentation records a valid target lease before cross-view eviction");
		for (int frame=0; frame<3; ++frame) {
			host.ClearSamples(); runtime.Frame(viewport,19.7);
			pixel(0,0,1-.5f*maskAlpha,.5f*maskAlpha);
			host.ClearSamples(false); other.Frame(viewport,19.7);
			pixel(0,0,1-.5f*maskAlpha,.5f*maskAlpha);
		}
		Check(host.errors==errorsBefore,"shared cache pressure evicts idle pixels without losing composition");
		host.maxLayerId=48;
	}
	Check(runtime.LoadDocument(cachedMask.Source(),"cache-budget-a.q4ui",diagnostics),"reload shared cache budget fixture");
	{
		Runtime second(host), third(host);
		Check(second.LoadDocument(cachedMask.Source(),"cache-budget-b.q4ui",diagnostics) &&
			third.LoadDocument(cachedMask.Source(),"cache-budget-c.q4ui",diagnostics),"load shared 4K cache budget views");
		Viewport large; large.width=3840; large.height=2160;
		const int errorsBefore=host.errors;
		for (int frame=0; frame<3; ++frame) {
			host.ClearSamples(); runtime.Frame(large,19.7);
			host.ClearSamples(false); second.Frame(large,19.7);
			host.ClearSamples(false); third.Frame(large,19.7);
		}
		Check(runtime.Statistics().layerCacheHits==1 && second.Statistics().layerCacheHits==1 &&
			third.Statistics().layerCacheHits==0,"4K contexts share two cached targets and reserve transient space");
		Check(host.errors==errorsBefore,"shared 4K cache budget renders all views");
	}
	host.cacheLayers=false; host.samplePoints.clear(); viewport={};
	{
		// Repeated small view sizes leave unleased host allocations resident.
		// A larger view can require retiring several of them before one new
		// target fits; resizing a single slot is insufficient.
		const auto errorsBefore=host.errors;
		host.layerBudget=32*1024*1024;
		for (const auto size : {1000,1100,2048}) {
			viewport.width=size; viewport.height=size==2048 ? 1024 : 1000;
			Check(sampleFrame(19.75),"resizing retires obsolete free allocations under the host memory budget");
		}
		Check(host.trimmedLayers>0 && host.errors==errorsBefore,"target budget recovers without a failed HUD frame");
		host.layerBudget=0; viewport={};
	}
	// Repeating images keep UV motion out of layout. An additive trace can
	// texture a fill-only mask directly, including its hole and fractional edge.
	Document imageMask;
	Check(imageMask.Load(cachedMask.Source(),diagnostics) &&
		imageMask.ReplaceValue("/root/children/0/properties/background-color/value","[0,0,0,0]",diagnostics) &&
		imageMask.ReplaceValue("/root/children/0/children",R"json([{"id":"trace","type":"image","properties":{
		 "width":{"type":"length","value":120,"unit":"px"},"height":{"type":"length","value":80,"unit":"px"},
		 "image":{"type":"image","value":"gfx/test/trace.tga"},"image-blend":{"type":"keyword","value":"additive"},
		 "image-color":{"type":"color","value":[1,0,0,1]},"image-offset-x":{"type":"number","value":0.125},
		 "image-offset-y":{"type":"number","value":-0.2},"image-scale-x":{"type":"number","value":2},
		 "image-scale-y":{"type":"number","value":0.5}}}])json",diagnostics),"author repeating masked image");
	Check(runtime.LoadDocument(imageMask.Source(),"image-mask.q4ui",diagnostics),"load repeating image");
	host.samplePoints={{20.37f,20.63f},{70.37f,50.63f},{12.37f,12.63f},{19.37f,20.63f}};
	sampleFrame(19.8);
	pixel(0,.5f*maskAlpha,1,0); pixel(1,0,1,0); pixel(2,0,1,0); pixel(3,32.f/255,1,0);
	Check(runtime.Statistics().directMaskDraws==1 && runtime.Statistics().layerPushes==0,"trace mask needs no composition target");
	Check(!host.imageVertices.empty() && host.imageSources.back()=="q4-add/_repeat/gfx/test/trace.tga","repeating additive image resolves through installed VFS source");
	for (const auto& v:host.imageVertices) Check(std::abs(v.u-(-.375f+(v.x-10)/120*2))<.00001f &&
		std::abs(v.v-(.05f+(v.y-10)/80*.5f))<.00001f,"masked image UVs preserve offset and centred scaling across each coverage cell");
	{
		Document scrolling; Runtime animated(host);
		Check(scrolling.Load(imageMask.Source(),diagnostics) && scrolling.ReplaceValue("/timelines",R"json([
		 {"id":"scroll","durationMs":1000,"tracks":[{"node":"trace","property":"image-offset-x","keys":[
		 {"atMs":0,"value":{"type":"number","value":0.125}},{"atMs":1000,"value":{"type":"number","value":1.125}}]}]}])json",diagnostics) &&
		 animated.LoadDocument(scrolling.Source(),"masked-sampling-replay.q4ui",diagnostics) && animated.PlayTimeline("scroll",19.8),
		 "load animated sampling through a stationary mask");
		for (const double seconds:{19.8,19.9,20.0}) { host.ClearSamples(); animated.Frame(viewport,seconds); }
		Check(animated.Statistics().frameReplays==1 && !host.imageVertices.empty(),"masked image sampling advances through replay");
		const auto replayed=host.imageVertices;
		for (const auto& v:replayed) Check(std::abs(v.u-(-.175f+(v.x-10)/120*2))<.00001f,
			"every mask coverage cell retains the current bitmap phase");
		host.replayFrames=false; host.ClearSamples(); animated.Frame(viewport,20);
		Check(host.imageVertices.size()==replayed.size(),"masked replay preserves normal geometry topology");
		for (size_t i=0;i<replayed.size();++i) Check(std::abs(replayed[i].u-host.imageVertices[i].u)<.00001f &&
			std::abs(replayed[i].v-host.imageVertices[i].v)<.00001f && std::abs(replayed[i].a-host.imageVertices[i].a)<.00001f,
			"masked replay matches the full compositor at the same animation time");
		host.replayFrames=true;
	}
	Check(runtime.PlayTimeline("fade",19.8),"masked image fades independently of UVs");
	sampleFrame(19.9); pixel(0,.45f*maskAlpha,1,0);
	Check(!imageMask.ReplaceValue("/root/children/0/children/0/properties/image-scale-y/value","0",diagnostics),"zero image scale is rejected transactionally");
	Check(imageMask.ReplaceValue("/root/children/0/children/0/properties/width/value","50",diagnostics) &&
		runtime.LoadDocument(imageMask.Source(),"image-intersection.q4ui",diagnostics),"image covers only part of its mask");
	sampleFrame(19.9); pixel(0,.5f*maskAlpha,1,0); pixel(1,0,1,0);
	Check(runtime.Statistics().directMaskDraws==0 && runtime.Statistics().layerPushes>0,"partial intersection uses general masking without drawing outside the image");
	host.samplePoints.clear(); viewport={};
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
	menu(MenuInput::Previous); Check(runtime.FocusedControl() == "modal-controls","held pre-modal reverse tab cannot navigate the new scope");
	menu(MenuInput::Previous,false); menu(MenuInput::Previous); menu(MenuInput::Previous,false);
	Check(runtime.FocusedControl() == "modal-system","fresh modal tab wraps within its own controls");
	menu(MenuInput::Accept);
	Check(runtime.PopModal(21),"close modal scope");
	Check(runtime.FocusedControl() == "reference-system","modal close restores the previous eligible focus");
	menu(MenuInput::Accept,false); Check(runtime.TakeActions().empty(),"modal teardown prevents release click-through");
	// Finish earlier navigation holds before handing later scenarios to a new input aggregator.
	for (const auto action : {MenuInput::Next,MenuInput::Up,MenuInput::Down}) menu(action,false);
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
	// The most recent device sets the presentation (section 13.4): a program's
	// focus shows until the pointer moves, which then lights only the control
	// under it; the next navigation input resumes from the focus and shows it.
	Check(runtime.SetControlEnabled("reference-system",true,24.2) && runtime.FocusControl("reference-system",24.2),"focus a button by program");
	Check(runtime.GetControlState("reference-system") == ControlState::Focus && runtime.GetControlState("reference-controls") == ControlState::Default,
		"a program's focus leads: it shows, and the stationary pointer's hover clears");
	runtime.PointerMove((hitX+30)/2,(hitY+20)/2,24.3);
	Check(runtime.GetControlState("reference-controls") == ControlState::Hover && runtime.GetControlState("reference-system") == ControlState::Default &&
		runtime.FocusedControl() == "reference-system","moving the pointer lights only the control under it and keeps the focus");
	runtime.MenuAction(MenuInput::Up,true,24.4); runtime.MenuAction(MenuInput::Up,false,24.4);
	Check(runtime.FocusedControl() == "reference-controls" && runtime.GetControlState("reference-controls") == ControlState::Focus,
		"the next navigation input resumes from the focus and shows it");
	runtime.FocusControl("",24.5);
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
		{
			// A Windows checkout writes CRLF and the others LF; a save names the
			// same document on either (roadmap Q4-06).
			const bool crlf = actionBindingSource.find("\r\n") != std::string::npos;
			std::string otherEndings;
			for (char c : actionBindingSource) {
				if (crlf && c == '\r') continue;
				if (!crlf && c == '\n') otherEndings += '\r';
				otherEndings += c;
			}
			Runtime third(host);
			Check(otherEndings != actionBindingSource && third.LoadDocument(otherEndings,"snapshot-bindings.q4ui",diagnostics),
				"load the snapshot document with the other line endings");
			Check(third.RestoreSnapshot(snapshot,error,30),"another checkout's line endings keep the snapshot's document identity");
		}
		Check(second.LoadDocument(bindingSource+"\n","snapshot-bindings.q4ui",diagnostics),"load source revision with unchanged IDs");
		Check(!second.RestoreSnapshot(snapshot,error,30),"exact source changes require an explicit snapshot migration");
		std::string replacementSnapshot;
		Check(second.SaveSnapshot(replacementSnapshot,error,30) && second.RestoreSnapshot(replacementSnapshot,error,31),
			"document replacement refreshes cached snapshot source identity");
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
	CheckCompletionPrograms(host);
	CheckDetailCard(host);
	CheckAcceptPrompts(host);
	CheckSharedDocument(host);
	CheckPublicTextQueries(host);
	CheckSoftFocus(host);
	CheckDirectBackdrop(host);
	CheckSpeedStreak(host);
	CheckRetainedMeshes(host);
	CheckFrameReplay(host);
	CheckGuiStateNumbers();
	CheckIdentityTransformReuse(host);
	CheckMovingMaskSourceCache(host);
	CheckMovingMaskSourceCache(host,true);
	CheckQuietButtonLayout(host);
	CheckDecorativeTransforms(host);
	CheckMenuFadeReplay(host);
	CheckCoverageRaster();
	CheckHiddenPresentation(host);
	CheckCompoundPlateCache(host);
	std::puts("Retained UI: density, layout, input, clipping, motion, bindings, independent contexts, bounded backend reuse, transactional instance snapshots, restart, completion programs, detail cards, accept prompts, shared documents, soft focus and retained meshes passed");
}
