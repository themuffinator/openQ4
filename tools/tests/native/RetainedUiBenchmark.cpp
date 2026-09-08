// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "src/ui/retained/Runtime.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <stdexcept>

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
}
int main(int argc, char** argv) {
	try {
		if (argc < 2 || argc > 4) throw std::runtime_error("usage: openq4-retained-ui-benchmark <vector-smoke.q4ui> [density=1.25] [frames=60]");
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
