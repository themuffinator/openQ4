// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "src/ui/retained/Runtime.h"
#include "src/ui/retained/Hud.h"
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
std::uint64_t PrivateBytes();
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
	const auto bytesBefore = PrivateBytes();
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
	const auto coldStats = runtime.Statistics();
	const double cold = coldStats.frameMilliseconds;
	for (int i = 1; i <= 240; ++i) runtime.Frame(viewport,start+i/60.0);
	const auto eventStart = std::chrono::steady_clock::now();
	if (!event.empty() && !runtime.RunEvent(event,start+4,effects,error)) throw std::runtime_error(event+": "+error);
	const double eventMs = std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-eventStart).count();
	std::vector<double> frame, update, render, compile, paths, uploads, draws, vertices, layers, elisions;
	std::vector<double> sources, motion, apply, layout, layoutUpdates, interactionUpdates, inputsScanned, replays;
	double layouts = 0; // Style and layout passes: a frame that only moves decoration needs none.
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
		sources.push_back(stats.sourceMilliseconds); motion.push_back(stats.motionMilliseconds); apply.push_back(stats.applyMilliseconds);
		layout.push_back(stats.layoutMilliseconds); layoutUpdates.push_back(static_cast<double>(stats.layoutUpdates));
		interactionUpdates.push_back(static_cast<double>(stats.interactionUpdates)); inputsScanned.push_back(static_cast<double>(stats.motionInputsScanned));
		replays.push_back(static_cast<double>(stats.frameReplays));
		layouts += static_cast<double>(stats.layoutUpdates);
	}
	if (!host.error.empty()) throw std::runtime_error("Screen logged an error: "+host.error);
	// What the shown document holds after its frames (the view, its state and motion).
	const auto heldKb = (PrivateBytes()-bytesBefore)/1024;
	std::printf("{\"scenario\":\"screen\",\"held_kb\":%llu,\"document\":\"%s\",\"event\":\"%s\",\"width\":%d,\"height\":%d,\"frames\":%d,\"load_ms\":%.6f,\"cold_ms\":%.6f,\"event_ms\":%.6f,"
		"\"cold_update_ms\":%.6f,\"cold_vector_compile_ms\":%.6f,\"cold_paths_compiled\":%llu,\"cold_geometry_compiles\":%llu,"
		"\"first_ms\":%.6f,\"p50_ms\":%.6f,\"p95_ms\":%.6f,\"max_ms\":%.6f,"
		"\"update_p50_ms\":%.6f,\"render_p50_ms\":%.6f,\"vector_compile_p50_ms\":%.6f,\"paths_compiled_p50\":%.0f,\"uploads_p50\":%.0f,"
		"\"draw_calls_p50\":%.0f,\"submitted_vertices_p50\":%.0f,\"layer_pushes_p50\":%.0f,\"layer_elisions_p50\":%.0f,"
		"\"sources_p50_ms\":%.6f,\"motion_p50_ms\":%.6f,\"apply_p50_ms\":%.6f,\"layout_p50_ms\":%.6f,"
		"\"layout_updates_p50\":%.0f,\"interaction_updates_p50\":%.0f,\"motion_inputs_scanned_p50\":%.0f,\"replays_p50\":%.0f,\"layout_updates_mean\":%.3f}\n",
		static_cast<unsigned long long>(heldKb),path.c_str(),event.c_str(),width,height,frames,loadMs,cold,eventMs,
		coldStats.updateMilliseconds,coldStats.vectorCompileMilliseconds,static_cast<unsigned long long>(coldStats.vectorPathsCompiled),
		static_cast<unsigned long long>(coldStats.geometryCompiles),frame.front(),Percentile(frame,.5),Percentile(frame,.95),Percentile(frame,1),
		Percentile(update,.5),Percentile(render,.5),
		Percentile(compile,.5),Percentile(paths,.5),Percentile(uploads,.5),Percentile(draws,.5),Percentile(vertices,.5),Percentile(layers,.5),Percentile(elisions,.5),
		Percentile(sources,.5),Percentile(motion,.5),Percentile(apply,.5),Percentile(layout,.5),
		Percentile(layoutUpdates,.5),Percentile(interactionUpdates,.5),Percentile(inputsScanned,.5),Percentile(replays,.5),layouts/frames);
	runtime.CloseDocument(); runtime.Shutdown();
}

// A readout the game changes while a document rests: `key` alternates between
// `a` and `b` on every frame, the way a HUD's clock, health or ammunition does
// (worst case: a change each frame). A HUD document (one with hud.* layout
// state) takes the peer's layout for the viewport and its join event first.
// `event` runs after the settle. Reports the changed frames' costs.
void StateChanges(const std::string& path, const std::string& key, const std::string& a, const std::string& b, int frames, const std::string& event) {
	std::ifstream file(path,std::ios::binary);
	if (!file) throw std::runtime_error("Cannot read state document");
	const std::string source{std::istreambuf_iterator<char>(file),{}};
	Document model; std::vector<Diagnostic> diagnostics;
	if (!model.Load(source,diagnostics)) throw std::runtime_error("Invalid state document");
	const auto declared = model.Model().state.find(key);
	if (declared == model.Model().state.end()) throw std::runtime_error("The document declares no state "+key);
	const auto parse = [&](const std::string& text) -> StateValue {
		if (std::holds_alternative<bool>(declared->second.initial)) return text == "true" || text == "1";
		if (std::holds_alternative<double>(declared->second.initial)) return std::stod(text);
		return text;
	};
	const StateValue first = parse(a), second = parse(b);
	ScreenHost host; Runtime runtime(host);
	if (!runtime.LoadDocument(source,path,diagnostics)) throw std::runtime_error("Cannot load state runtime document");
	Viewport viewport; viewport.width = 1280; viewport.height = 720;
	viewport.canvasHeight = static_cast<float>(model.Model().canvasHeight);
	const bool hud = model.Model().state.contains("hud.width");
	Runtime::EventEffects effects; std::string error;
	const double start = 1;
	if (hud) {
		viewport.canvasMinWidth = 960; viewport.fitScale = 1;
		const auto layout = HudLayoutFor(1280.f,720.f,viewport.DpRatio(),{0,0,0,0},1,0,1);
		StateValues values{{"hud.width",double(layout.width)},{"hud.height",double(layout.height)},{"hud.left",double(layout.left)},
			{"hud.right",double(layout.right)},{"hud.top",double(layout.top)},{"hud.bottom",double(layout.bottom)},
			{"hud.scale",double(layout.scale)},{"hud.centerScale",double(layout.centerScale)}};
		if (model.Model().state.contains("gametype")) values.insert({"gametype",1.0});
		if (!runtime.SetState(values,error,start)) throw std::runtime_error("HUD layout: "+error);
		if (runtime.HasEvent("hudJoin") && !runtime.RunEvent("hudJoin",start,effects,error)) throw std::runtime_error("hudJoin: "+error);
	} else {
		if (runtime.HasEvent("onInit") && !runtime.RunEvent("onInit",start,effects,error)) throw std::runtime_error("onInit: "+error);
		runtime.PlayTimeline("open",start);
	}
	for (int i = 0; i <= 240; ++i) runtime.Frame(viewport,start+i/60.0);
	if (!event.empty() && !runtime.RunEvent(event,start+4,effects,error)) throw std::runtime_error(event+": "+error);
	for (int i = 1; i <= 60; ++i) runtime.Frame(viewport,start+4+i/60.0);
	std::vector<double> total, set, frame, update, render, compile, paths, draws;
	double layouts = 0, scanned = 0;
	for (int i = 1; i <= frames; ++i) {
		const double now = start+5+i/60.0;
		const auto before = std::chrono::steady_clock::now();
		if (!runtime.SetState({{key,i%2 ? second : first}},error,now)) throw std::runtime_error(key+": "+error);
		const auto changed = std::chrono::steady_clock::now();
		runtime.Frame(viewport,now);
		const auto after = std::chrono::steady_clock::now();
		const auto stats = runtime.Statistics();
		total.push_back(std::chrono::duration<double,std::milli>(after-before).count());
		set.push_back(std::chrono::duration<double,std::milli>(changed-before).count());
		frame.push_back(stats.frameMilliseconds); update.push_back(stats.updateMilliseconds); render.push_back(stats.renderMilliseconds);
		compile.push_back(stats.vectorCompileMilliseconds); paths.push_back(static_cast<double>(stats.vectorPathsCompiled));
		draws.push_back(static_cast<double>(stats.drawCalls));
		layouts += static_cast<double>(stats.layoutUpdates); scanned += static_cast<double>(stats.motionInputsScanned);
	}
	if (!host.error.empty()) throw std::runtime_error("State document logged an error: "+host.error);
	std::printf("{\"scenario\":\"state\",\"document\":\"%s\",\"key\":\"%s\",\"frames\":%d,\"total_p50_ms\":%.6f,\"total_p95_ms\":%.6f,\"set_p50_ms\":%.6f,"
		"\"frame_p50_ms\":%.6f,\"update_p50_ms\":%.6f,\"render_p50_ms\":%.6f,\"vector_compile_p50_ms\":%.6f,\"paths_compiled_p50\":%.0f,"
		"\"draw_calls_p50\":%.0f,\"layout_updates_mean\":%.3f,\"motion_inputs_scanned_mean\":%.1f}\n",
		path.c_str(),key.c_str(),frames,Percentile(total,.5),Percentile(total,.95),Percentile(set,.5),Percentile(frame,.5),Percentile(update,.5),
		Percentile(render,.5),Percentile(compile,.5),Percentile(paths,.5),Percentile(draws,.5),layouts/frames,scanned/frames);
	runtime.CloseDocument(); runtime.Shutdown();
}

// The cost of an event a player's input raises (a focus move, a tab, a
// modal), on a settled screen: the event runs `repeats` times with a frame
// after each, and event_ms is RunEvent alone.
void EventRepeats(const std::string& path, const std::string& event, int repeats) {
	std::ifstream file(path,std::ios::binary);
	if (!file) throw std::runtime_error("Cannot read event document");
	const std::string source{std::istreambuf_iterator<char>(file),{}};
	Document model; std::vector<Diagnostic> diagnostics;
	if (!model.Load(source,diagnostics)) throw std::runtime_error("Invalid event document");
	ScreenHost host; Runtime runtime(host);
	if (!runtime.LoadDocument(source,path,diagnostics)) throw std::runtime_error("Cannot load event runtime document");
	Viewport viewport; viewport.width = 1280; viewport.height = 720;
	viewport.canvasHeight = static_cast<float>(model.Model().canvasHeight);
	Runtime::EventEffects effects; std::string error;
	const double start = 1;
	if (runtime.HasEvent("onInit") && !runtime.RunEvent("onInit",start,effects,error)) throw std::runtime_error("onInit: "+error);
	runtime.PlayTimeline("open",start);
	for (int i = 0; i <= 240; ++i) runtime.Frame(viewport,start+i/60.0);
	std::vector<double> eventMs, frameMs;
	for (int i = 1; i <= repeats; ++i) {
		const double now = start+4+i/60.0;
		const auto before = std::chrono::steady_clock::now();
		if (!runtime.RunEvent(event,now,effects,error)) throw std::runtime_error(event+": "+error);
		eventMs.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-before).count());
		runtime.Frame(viewport,now);
		frameMs.push_back(runtime.Statistics().frameMilliseconds);
	}
	if (!host.error.empty()) throw std::runtime_error("Event document logged an error: "+host.error);
	std::printf("{\"scenario\":\"event\",\"document\":\"%s\",\"event\":\"%s\",\"repeats\":%d,\"event_p50_ms\":%.6f,\"event_min_ms\":%.6f,\"event_max_ms\":%.6f,\"frame_p50_ms\":%.6f}\n",
		path.c_str(),event.c_str(),repeats,Percentile(eventMs,.5),Percentile(eventMs,0.0001),Percentile(eventMs,1),Percentile(frameMs,.5));
	runtime.CloseDocument(); runtime.Shutdown();
}

// A page's arrival and departure as the session plays them: `enter` (the
// page's opening, such as onActivate) and then `leave` (its closing, such as
// onDeactivate), each followed by `frames` 60 Hz frames, `cycles` times. The
// frames measured are the ones that animate, the page's opening and closing
// transitions, where the menus stutter if they cost too much.
void Transition(const std::string& path, const std::string& enter, const std::string& leave, int cycles, int frames) {
	std::ifstream file(path,std::ios::binary);
	if (!file) throw std::runtime_error("Cannot read transition document");
	const std::string source{std::istreambuf_iterator<char>(file),{}};
	Document model; std::vector<Diagnostic> diagnostics;
	if (!model.Load(source,diagnostics)) throw std::runtime_error("Invalid transition document");
	ScreenHost host; Runtime runtime(host);
	if (!runtime.LoadDocument(source,path,diagnostics)) throw std::runtime_error("Cannot load transition runtime document");
	Viewport viewport; viewport.width = 1280; viewport.height = 720;
	viewport.canvasHeight = static_cast<float>(model.Model().canvasHeight);
	Runtime::EventEffects effects; std::string error;
	double now = 1;
	if (runtime.HasEvent("onInit") && !runtime.RunEvent("onInit",now,effects,error)) throw std::runtime_error("onInit: "+error);
	for (int i = 0; i <= 60; ++i) runtime.Frame(viewport,now += 1/60.0);
	std::vector<double> enterMs, leaveMs, layout, render, compile, paths, layoutUpdates;
	for (int cycle = 0; cycle < cycles; ++cycle) {
		for (const auto* event : {&enter,&leave}) {
			if (!runtime.RunEvent(*event,now,effects,error)) throw std::runtime_error(*event+": "+error);
			for (int i = 0; i < frames; ++i) {
				runtime.Frame(viewport,now += 1/60.0);
				// The first cycle compiles the frame's artwork at each size.
				if (cycle == 0) continue;
				const auto stats = runtime.Statistics();
				(event == &enter ? enterMs : leaveMs).push_back(stats.frameMilliseconds);
				layout.push_back(stats.layoutMilliseconds); render.push_back(stats.renderMilliseconds);
				compile.push_back(stats.vectorCompileMilliseconds); paths.push_back(static_cast<double>(stats.vectorPathsCompiled));
				layoutUpdates.push_back(static_cast<double>(stats.layoutUpdates));
			}
		}
	}
	if (!host.error.empty()) throw std::runtime_error("Transition document logged an error: "+host.error);
	if (enterMs.empty()) throw std::runtime_error("Transition needs at least two cycles");
	double updates = 0; for (double value : layoutUpdates) updates += value;
	std::printf("{\"scenario\":\"transition\",\"document\":\"%s\",\"enter\":\"%s\",\"leave\":\"%s\",\"cycles\":%d,\"frames\":%d,"
		"\"enter_p50_ms\":%.6f,\"enter_p95_ms\":%.6f,\"enter_max_ms\":%.6f,\"leave_p50_ms\":%.6f,\"leave_p95_ms\":%.6f,\"leave_max_ms\":%.6f,"
		"\"layout_p50_ms\":%.6f,\"render_p50_ms\":%.6f,\"vector_compile_p50_ms\":%.6f,\"paths_compiled_p50\":%.0f,\"layout_updates_mean\":%.3f}\n",
		path.c_str(),enter.c_str(),leave.c_str(),cycles,frames,Percentile(enterMs,.5),Percentile(enterMs,.95),Percentile(enterMs,1),
		Percentile(leaveMs,.5),Percentile(leaveMs,.95),Percentile(leaveMs,1),Percentile(layout,.5),Percentile(render,.5),
		Percentile(compile,.5),Percentile(paths,.5),updates/layoutUpdates.size());
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
			if (argc > 4) throw std::runtime_error("usage: openq4-retained-ui-benchmark --load <document.rui> [repeats=3]");
			const int repeats = argc > 3 ? std::stoi(argv[3]) : 3;
			if (repeats < 1 || repeats > 100) throw std::runtime_error("Invalid repeat count");
			Load(argv[2],repeats);
			return 0;
		}
		if (argc >= 3 && (std::string(argv[1]) == "--screen" || std::string(argv[1]) == "--scope")) {
			if (argc > 7) throw std::runtime_error("usage: openq4-retained-ui-benchmark --screen|--scope <document.rui> [frames=120] [width=1280] [height=720] [event]");
			const int frames = argc > 3 ? std::stoi(argv[3]) : 120, width = argc > 4 ? std::stoi(argv[4]) : 1280, height = argc > 5 ? std::stoi(argv[5]) : 720;
			if (frames < 2 || frames > 3600 || width < 64 || width > 8192 || height < 64 || height > 8192) throw std::runtime_error("Invalid screen frame count or size");
			Screen(argv[2],frames,width,height,argc > 6 ? argv[6] : "",std::string(argv[1]) == "--scope");
			return 0;
		}
		if (argc >= 4 && std::string(argv[1]) == "--event") {
			if (argc > 5) throw std::runtime_error("usage: openq4-retained-ui-benchmark --event <document.rui> <event> [repeats=60]");
			const int repeats = argc > 4 ? std::stoi(argv[4]) : 60;
			if (repeats < 1 || repeats > 100000) throw std::runtime_error("Invalid event repeat count");
			EventRepeats(argv[2],argv[3],repeats);
			return 0;
		}
		if (argc >= 5 && std::string(argv[1]) == "--transition") {
			if (argc > 7) throw std::runtime_error("usage: openq4-retained-ui-benchmark --transition <document.rui> <enter> <leave> [cycles=8] [frames=45]");
			const int cycles = argc > 5 ? std::stoi(argv[5]) : 8, frames = argc > 6 ? std::stoi(argv[6]) : 45;
			if (cycles < 2 || cycles > 10000 || frames < 1 || frames > 600) throw std::runtime_error("Invalid transition cycle or frame count");
			Transition(argv[2],argv[3],argv[4],cycles,frames);
			return 0;
		}
		if (argc >= 6 && std::string(argv[1]) == "--state") {
			if (argc > 8) throw std::runtime_error("usage: openq4-retained-ui-benchmark --state <document.rui> <key> <a> <b> [frames=240] [event]");
			const int frames = argc > 6 ? std::stoi(argv[6]) : 240;
			if (frames < 2 || frames > 36000) throw std::runtime_error("Invalid state frame count");
			StateChanges(argv[2],argv[3],argv[4],argv[5],frames,argc > 7 ? argv[7] : "");
			return 0;
		}
		if (argc < 2 || argc > 4) throw std::runtime_error("usage: openq4-retained-ui-benchmark <vector-smoke.rui> [density=1.25] [frames=60] | --screen|--scope <document.rui> [frames] [width] [height] [event] | --state <document.rui> <key> <a> <b> [frames] [event] | --event <document.rui> <event> [repeats] | --transition <document.rui> <enter> <leave> [cycles] [frames] | --load <document.rui> [repeats]");
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
