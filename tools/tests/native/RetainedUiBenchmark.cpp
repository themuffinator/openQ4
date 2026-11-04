// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "src/ui/retained/Runtime.h"
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#endif
#include <json/json.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>

using namespace openq4::ui;
namespace {
struct BenchmarkHost final : Host {
	bool ReadCVar(const std::string&, size_t, StateValue&) override { return false; }
	bool ReadFile(const std::string&, std::string&) override { return false; }
	std::string Translate(const std::string& text) override { return text; }
	void Log(bool error, const std::string& text) override { if (error) throw std::runtime_error(text); }
	std::uintptr_t LoadMaterial(const std::string&, int& width, int& height) override { width = height = 256; return 1; }
	void Draw(const std::vector<Vertex>&, const std::vector<int>&, std::uintptr_t) override {}
	std::uint64_t RenderFrame() const override { return 0; } // Immediate CPU sink; fixed dimensions.
	bool BeginLayer(std::uint32_t, int, int) override { return true; }
	void CompositeLayer(std::uint32_t, std::uint32_t, float, const Bounds&) override {}
	void MaskLayer(std::uint32_t, std::uint32_t, const Bounds&) override {}
	void EndLayer(std::uint32_t) override {}
	FontMetrics GetFontMetrics(const std::string&, int size) override { return {size*.8f,size*.2f,size*1.2f,size*.5f}; }
	Glyph GetGlyph(const std::string&, int size, std::uint32_t) override { return {size*.6f,0,-size*.8f,size*.6f,static_cast<float>(size),0,0,1,1,"benchmark-font"}; }
};
void Edit(Document& document, const std::string& pointer, const std::string& value) {
	std::vector<Diagnostic> diagnostics;
	if (!document.ReplaceValue(pointer,value,diagnostics)) throw std::runtime_error("Benchmark fixture edit failed: "+pointer);
}
double Percentile(std::vector<double> values, double percentile) {
	std::sort(values.begin(),values.end());
	return values[static_cast<size_t>(std::ceil(percentile*values.size()))-1];
}
// A production screen reads its declared CVar sources; each reads its type's
// default, as a fresh configuration does. Draws are counted, not converted.
struct ScreenHost final : Host {
	std::string error;
	bool ReadCVar(const std::string&, size_t type, StateValue& value) override {
		if (type == 1) value = false; else if (type == 2) value = std::string(); else value = 0.0;
		return true;
	}
	bool ReadFile(const std::string&, std::string&) override { return false; }
	std::string Translate(const std::string& text) override { return text; }
	void Log(bool failed, const std::string& text) override { if (failed && error.empty()) error = text; }
	std::uintptr_t LoadMaterial(const std::string&, int& width, int& height) override { width = height = 256; return 1; }
	void Draw(const std::vector<Vertex>&, const std::vector<int>&, std::uintptr_t) override {}
	std::uint64_t RenderFrame() const override { return 0; }
	bool BeginLayer(std::uint32_t, int, int) override { return true; }
	void CompositeLayer(std::uint32_t, std::uint32_t, float, const Bounds&) override {}
	void MaskLayer(std::uint32_t, std::uint32_t, const Bounds&) override {}
	void EndLayer(std::uint32_t) override {}
	FontMetrics GetFontMetrics(const std::string&, int size) override { return {size*.8f,size*.2f,size*1.2f,size*.5f}; }
	Glyph GetGlyph(const std::string&, int size, std::uint32_t) override { return {size*.6f,0,-size*.8f,size*.6f,static_cast<float>(size),0,0,1,1,"benchmark-font"}; }
};
// The session's presentation of a root screen: its runtime load (timed),
// onInit, the open timeline, a four second settle, then timed 60 Hz frames on
// its view-height canvas. With `event`, the event runs after the settle and
// the timed frames show what it opens (first_ms is the frame that does).
void Screen(const std::string& path, int frames, int width, int height, const std::string& event, bool scope = false) {
	std::ifstream file(path,std::ios::binary);
	if (!file) throw std::runtime_error("Cannot read screen document");
	const std::string source{std::istreambuf_iterator<char>(file),{}};
	Document model; std::vector<Diagnostic> diagnostics;
	if (!model.Load(source,diagnostics)) throw std::runtime_error("Invalid screen document");
	ScreenHost host; Runtime runtime(host);
	const auto loadStart = std::chrono::steady_clock::now();
	if (!runtime.LoadDocument(source,path,diagnostics)) throw std::runtime_error("Cannot load screen runtime document");
	const double loadMs = std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-loadStart).count();
	Viewport viewport; viewport.width = width; viewport.height = height;
	viewport.canvasHeight = static_cast<float>(model.Model().canvasHeight);
	const double start = 1;
	Runtime::EventEffects effects; std::string error;
	if (runtime.HasEvent("onInit") && !runtime.RunEvent("onInit",start,effects,error)) throw std::runtime_error("onInit: "+error);
	runtime.PlayTimeline("open",start);
	runtime.Frame(viewport,start);
	const double cold = runtime.Statistics().frameMilliseconds;
	for (int i = 1; i <= 240; ++i) runtime.Frame(viewport,start+i/60.0);
	if (!event.empty() && !runtime.RunEvent(event,start+4,effects,error)) throw std::runtime_error(event+": "+error);
	std::vector<double> frame, update, render, compile, paths, uploads, draws, vertices, layers, elisions;
	for (int i = 1; i <= frames; ++i) {
		// Exercise the game-driven motion as well as authored idle timelines.
		// A stationary nailgun compass otherwise hides its worst rendering cost.
		if (scope) {
			const double now=start+4+i/60.0;
			if (!runtime.SetState({{"playerYaw",std::fmod(i*.7,360.0)}},error,now)) throw std::runtime_error(error);
			if (model.Model().state.contains("lockYaw") &&
				!runtime.SetState({{"lockYaw",std::fmod(i*1.3,360.0)},{"lockStatus",.5+.5*std::sin(i*.03)}},error,now))
				throw std::runtime_error(error);
		}
		runtime.Frame(viewport,start+4+i/60.0);
		const auto stats = runtime.Statistics();
		frame.push_back(stats.frameMilliseconds); update.push_back(stats.updateMilliseconds); render.push_back(stats.renderMilliseconds);
		compile.push_back(stats.vectorCompileMilliseconds); paths.push_back(static_cast<double>(stats.vectorPathsCompiled));
		uploads.push_back(static_cast<double>(stats.vectorUploads)); draws.push_back(static_cast<double>(stats.drawCalls));
		vertices.push_back(static_cast<double>(stats.submittedVertices)); layers.push_back(static_cast<double>(stats.layerPushes));
		elisions.push_back(static_cast<double>(stats.layerElisions));
	}
	if (!host.error.empty()) throw std::runtime_error("Screen logged an error: "+host.error);
	std::printf("{\"scenario\":\"screen\",\"document\":\"%s\",\"event\":\"%s\",\"width\":%d,\"height\":%d,\"frames\":%d,\"load_ms\":%.6f,\"cold_ms\":%.6f,"
		"\"first_ms\":%.6f,\"p50_ms\":%.6f,\"p95_ms\":%.6f,\"max_ms\":%.6f,"
		"\"update_p50_ms\":%.6f,\"render_p50_ms\":%.6f,\"vector_compile_p50_ms\":%.6f,\"paths_compiled_p50\":%.0f,\"uploads_p50\":%.0f,"
		"\"draw_calls_p50\":%.0f,\"submitted_vertices_p50\":%.0f,\"layer_pushes_p50\":%.0f,\"layer_elisions_p50\":%.0f}\n",
		path.c_str(),event.c_str(),width,height,frames,loadMs,cold,frame.front(),Percentile(frame,.5),Percentile(frame,.95),Percentile(frame,1),
		Percentile(update,.5),Percentile(render,.5),
		Percentile(compile,.5),Percentile(paths,.5),Percentile(uploads,.5),Percentile(draws,.5),Percentile(vertices,.5),Percentile(layers,.5),Percentile(elisions,.5));
	runtime.CloseDocument(); runtime.Shutdown();
}

// Private committed bytes of this process, for the memory a load retains.
// Zero where the platform query is not implemented.
std::uint64_t PrivateBytes() {
#ifdef _WIN32
	PROCESS_MEMORY_COUNTERS_EX counters{}; counters.cb = sizeof(counters);
	if (GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),sizeof(counters))) return counters.PrivateUsage;
#endif
	return 0;
}
void CountNodes(const Node& node, std::uint64_t& nodes, std::uint64_t& properties, std::uint64_t& paths) {
	++nodes; properties += node.properties.size(); paths += node.paths.size();
	for (const auto& child : node.children) CountNodes(child,nodes,properties,paths);
}
// A production document's load, split into the phases a GUI and its view pay:
// the JSON parse alone (jsoncpp, with the document reader's settings), the
// Document's parse+validation+index, its derived markup, and the runtime's
// view (markup, elements, bindings). Times are the minimum over `repeats`;
// memory deltas are from the first repeat, while each phase's result lives.
void Load(const std::string& path, int repeats) {
	std::ifstream file(path,std::ios::binary);
	if (!file) throw std::runtime_error("Cannot read document");
	const std::string source{std::istreambuf_iterator<char>(file),{}};
	double parseMs = 1e30, documentMs = 1e30, markupMs = 1e30, viewMs = 1e30;
	std::uint64_t parseBytes = 0, documentBytes = 0, viewBytes = 0, markupBytes = 0;
	std::uint64_t nodes = 0, properties = 0, paths = 0, bindings = 0, timelines = 0, tracks = 0, events = 0, controls = 0;
	for (int repeat = 0; repeat < repeats; ++repeat) {
		{
			Json::CharReaderBuilder builder;
			builder["collectComments"] = true; builder["allowComments"] = true; builder["allowTrailingCommas"] = false;
			builder["strictRoot"] = false; builder["failIfExtra"] = true; builder["rejectDupKeys"] = true;
			builder["allowSpecialFloats"] = false; builder["stackLimit"] = 128; builder["skipBom"] = false;
			std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
			Json::Value root;
			const auto before = PrivateBytes();
			const auto start = std::chrono::steady_clock::now();
			if (!reader->parse(source.data(),source.data()+source.size(),&root,nullptr)) throw std::runtime_error("JSON parse failed");
			parseMs = std::min(parseMs,std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());
			if (repeat == 0) parseBytes = PrivateBytes()-before;
		}
		Document document; std::vector<Diagnostic> diagnostics;
		{
			const auto before = PrivateBytes();
			const auto start = std::chrono::steady_clock::now();
			if (!document.Load(source,diagnostics)) throw std::runtime_error("Invalid document: "+(diagnostics.empty() ? std::string("?") : diagnostics.front().message));
			documentMs = std::min(documentMs,std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());
			if (repeat == 0) documentBytes = PrivateBytes()-before;
		}
		{
			const auto start = std::chrono::steady_clock::now();
			const auto markup = document.BuildMarkup();
			markupMs = std::min(markupMs,std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());
			markupBytes = markup.size();
		}
		{
			ScreenHost host; Runtime runtime(host);
			const auto before = PrivateBytes();
			const auto start = std::chrono::steady_clock::now();
			if (!runtime.LoadDocument(document,path,diagnostics)) throw std::runtime_error("Cannot load the view: "+(diagnostics.empty() ? host.error : diagnostics.front().message));
			viewMs = std::min(viewMs,std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());
			if (repeat == 0) viewBytes = PrivateBytes()-before;
			if (!host.error.empty()) throw std::runtime_error("View logged an error: "+host.error);
			runtime.CloseDocument(); runtime.Shutdown();
		}
		if (repeat == 0) {
			const auto& model = document.Model();
			CountNodes(model.root,nodes,properties,paths);
			bindings = model.bindings.size(); timelines = model.timelines.size(); events = model.events.size();
			for (const auto& timeline : model.timelines) tracks += timeline.tracks.size();
			std::vector<const Node*> pending{&model.root};
			while (!pending.empty()) { const auto* node = pending.back(); pending.pop_back(); if (node->control) ++controls; for (const auto& child : node->children) pending.push_back(&child); }
		}
	}
	std::printf("{\"scenario\":\"load\",\"document\":\"%s\",\"bytes\":%llu,\"repeats\":%d,\"parse_ms\":%.3f,\"document_ms\":%.3f,\"markup_ms\":%.3f,\"view_ms\":%.3f,"
		"\"parse_kb\":%llu,\"document_kb\":%llu,\"view_kb\":%llu,\"markup_bytes\":%llu,"
		"\"nodes\":%llu,\"properties\":%llu,\"paths\":%llu,\"controls\":%llu,\"bindings\":%llu,\"timelines\":%llu,\"tracks\":%llu,\"events\":%llu}\n",
		path.c_str(),static_cast<unsigned long long>(source.size()),repeats,parseMs,documentMs,markupMs,viewMs,
		static_cast<unsigned long long>(parseBytes/1024),static_cast<unsigned long long>(documentBytes/1024),static_cast<unsigned long long>(viewBytes/1024),
		static_cast<unsigned long long>(markupBytes),static_cast<unsigned long long>(nodes),static_cast<unsigned long long>(properties),
		static_cast<unsigned long long>(paths),static_cast<unsigned long long>(controls),static_cast<unsigned long long>(bindings),
		static_cast<unsigned long long>(timelines),static_cast<unsigned long long>(tracks),static_cast<unsigned long long>(events));
}
}
int main(int argc, char** argv) {
	try {
		if (argc >= 3 && std::string(argv[1]) == "--load") {
			if (argc > 4) throw std::runtime_error("usage: openq4-retained-ui-benchmark --load <document.q4ui> [repeats=3]");
			const int repeats = argc > 3 ? std::stoi(argv[3]) : 3;
			if (repeats < 1 || repeats > 100) throw std::runtime_error("Invalid repeat count");
			Load(argv[2],repeats);
			return 0;
		}
		if (argc >= 3 && (std::string(argv[1]) == "--screen" || std::string(argv[1]) == "--scope")) {
			if (argc > 7) throw std::runtime_error("usage: openq4-retained-ui-benchmark --screen|--scope <document.q4ui> [frames=120] [width=1280] [height=720] [event]");
			const int frames = argc > 3 ? std::stoi(argv[3]) : 120, width = argc > 4 ? std::stoi(argv[4]) : 1280, height = argc > 5 ? std::stoi(argv[5]) : 720;
			if (frames < 2 || frames > 3600 || width < 64 || width > 8192 || height < 64 || height > 8192) throw std::runtime_error("Invalid screen frame count or size");
			Screen(argv[2],frames,width,height,argc > 6 ? argv[6] : "",std::string(argv[1]) == "--scope");
			return 0;
		}
		if (argc < 2 || argc > 4) throw std::runtime_error("usage: openq4-retained-ui-benchmark <vector-smoke.q4ui> [density=1.25] [frames=60] | --screen|--scope <document.q4ui> [frames] [width] [height] [event] | --load <document.q4ui> [repeats]");
		Viewport viewport;
		viewport.displayScale = argc > 2 ? std::stof(argv[2]) : 1.25f;
		const int frames = argc > 3 ? std::stoi(argv[3]) : 60;
		if (!std::isfinite(viewport.displayScale) || viewport.displayScale <= 0 || viewport.displayScale > 8 || frames < 2 || frames > 600) throw std::runtime_error("Invalid benchmark density/frame count");
		std::ifstream file(argv[1],std::ios::binary);
		if (!file) throw std::runtime_error("Cannot read benchmark fixture");
		const std::string source{std::istreambuf_iterator<char>(file),{}};
		BenchmarkHost host; Runtime runtime(host);
		for (const std::string scenario : {"static","opacity","integer-translation","fractional-translation"}) {
			Document document; std::vector<Diagnostic> diagnostics;
			if (!document.Load(source,diagnostics)) throw std::runtime_error("Invalid benchmark fixture");
			Edit(document,"/timelines","[]");
			Edit(document,"/root/children/0/properties/opacity/value","1");
			Edit(document,"/root/children/0/properties/transform/value","[0,0,1,1,0]");
			Edit(document,"/root/children/0/properties/transform/unit","\"px\"");
			std::string tracks;
			if (scenario == "static" || scenario == "opacity") tracks = std::string(R"({"node":"panel","property":"opacity","keys":[{"atMs":0,"value":{"type":"number","value":1}},{"atMs":1000,"value":{"type":"number","value":)")+
				(scenario == "static" ? "1" : "0.2")+R"(}}]})";
			else if (scenario != "static") tracks = std::string(R"({"node":"panel","property":"transform","keys":[{"atMs":0,"value":{"type":"transform","unit":"px","value":[0,0,1,1,0]}},{"atMs":1000,"value":{"type":"transform","unit":"px","value":[)")+
				std::to_string(scenario == "integer-translation" ? frames : frames*.3)+R"(,0,1,1,0]}}]})";
			Edit(document,"/timelines","[{\"id\":\"profile\",\"durationMs\":1000,\"tracks\":["+tracks+"]}]");
			if (!runtime.LoadDocument(document.Source(),argv[1],diagnostics)) throw std::runtime_error("Cannot load benchmark runtime document");
			// Recreate time ownership for each scenario, matching a fresh preview.
			const double base = scenario == "static" ? 10 : scenario == "opacity" ? 20 : scenario == "integer-translation" ? 30 : 40;
			runtime.Frame(viewport,base);
			const auto cold = runtime.Statistics();
			if (!runtime.PlayTimeline("profile",base)) throw std::runtime_error("Cannot start benchmark timeline");
			std::vector<double> samples;
			double compileMs = 0, uploadMs = 0;
			std::uint64_t paths = 0, uploads = 0, hits = 0, peakBytes = 0;
			for (int i = 1; i <= frames; ++i) {
				try { runtime.Frame(viewport,base+static_cast<double>(i)/frames); }
				catch (const std::exception& error) { throw std::runtime_error(scenario+" frame "+std::to_string(i)+": "+error.what()); }
				const auto stats = runtime.Statistics();
				samples.push_back(stats.frameMilliseconds); compileMs += stats.vectorCompileMilliseconds; uploadMs += stats.vectorUploadMilliseconds;
				paths += stats.vectorPathsCompiled; uploads += stats.vectorUploads; hits += stats.vectorCacheHits;
				peakBytes = std::max(peakBytes,stats.visibleVectorCacheBytes+stats.residentGeometryBytes);
			}
			std::printf("{\"scenario\":\"%s\",\"density\":%.3f,\"frames\":%d,\"cold_ms\":%.6f,\"p50_ms\":%.6f,\"p95_ms\":%.6f,\"max_ms\":%.6f,\"vector_compile_ms\":%.6f,\"vector_upload_ms\":%.6f,\"paths_compiled\":%llu,\"uploads\":%llu,\"cache_hits\":%llu,\"tracked_peak_bytes\":%llu}\n",
				scenario.c_str(),viewport.displayScale,frames,cold.frameMilliseconds,Percentile(samples,.5),Percentile(samples,.95),Percentile(samples,1),compileMs,uploadMs,
				static_cast<unsigned long long>(paths),static_cast<unsigned long long>(uploads),static_cast<unsigned long long>(hits),static_cast<unsigned long long>(peakBytes));
		}
		runtime.CloseDocument(); runtime.Shutdown();
		if (runtime.Statistics().residentGeometryCount || runtime.Statistics().residentGeometryBytes) throw std::runtime_error("Retained geometry survived shutdown");
	} catch (const std::exception& error) { std::fprintf(stderr,"Benchmark failed: %s\n",error.what()); return 1; }
}
