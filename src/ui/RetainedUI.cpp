// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "RetainedUI.h"

#ifndef ID_DEDICATED
#include "retained/Runtime.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <map>

namespace {
idCVar ui_retainedScale("ui_retainedScale", "1", CVAR_GUI | CVAR_FLOAT | CVAR_ARCHIVE,
	"retained UI density multiplier", .75f, 2.f);
idCVar ui_retainedDensity("ui_retainedDensity", "0", CVAR_GUI | CVAR_FLOAT,
	"retained UI test density override; zero uses the window display scale", 0.f, 8.f);
idCVar ui_retainedReducedMotion("ui_retainedReducedMotion", "0", CVAR_GUI | CVAR_BOOL | CVAR_ARCHIVE,
	"reduce decorative motion in the retained UI preview");

class EngineHost final : public openq4::ui::Host {
public:
	bool ReadFile(const std::string& path, std::string& contents) override {
		// UI documents and resources resolve only through the engine VFS.
		if (path.empty() || path.find("..") != std::string::npos || path.find(':') != std::string::npos || path[0] == '/' || path[0] == '\\') return false;
		void* data = NULL;
		const int length = fileSystem->ReadFile(path.c_str(), &data);
		if (length < 0 || data == NULL) return false;
		contents.assign(static_cast<const char*>(data), length);
		fileSystem->FreeFile(data);
		return true;
	}
	std::string Translate(const std::string& text) override {
		return text.compare(0,5,"#str_") == 0 ? common->GetLanguageDict()->GetString(text.c_str()) : text;
	}
	void Log(bool error, const std::string& message) override {
		if (error) common->Warning("retained UI: %s", message.c_str());
		else common->DPrintf("retained UI: %s\n", message.c_str());
	}
	std::uintptr_t LoadMaterial(const std::string& name, int& width, int& height) override {
		const std::string source = name.compare(0,9,"material:") == 0 ? name.substr(9) : name;
		// The spike's images are direct image paths/generated font pages. Full
		// legacy multi-stage materials and movies need their own draw operation.
		const idMaterial* material = declManager->FindMaterial(("_retained/" + source).c_str());
		if (material == NULL || material->GetState() == DS_DEFAULTED) return 0;
		width = material->GetImageWidth(); height = material->GetImageHeight();
		return reinterpret_cast<std::uintptr_t>(material);
	}
	void Draw(const std::vector<openq4::ui::Vertex>& vertices, const std::vector<int>& indices, std::uintptr_t handle) override {
		const idMaterial* material = handle ? reinterpret_cast<const idMaterial*>(handle) : declManager->FindMaterial("_retainedSolid");
		// Chunk whole triangles below the engine's surface vertex limit. This
		// also works when a large vector mesh uses indices wider than glIndex_t.
		idList<idDrawVert> converted;
		idList<glIndex_t> localIndices;
		const float sx = 640.f / viewportWidth, sy = 480.f / viewportHeight;
		for (size_t start = 0; start < indices.size(); start += 12000) {
			const int count = static_cast<int>(Min<size_t>(12000, indices.size() - start));
			converted.SetNum(count); localIndices.SetNum(count);
			for (int i = 0; i < count; ++i) {
				const auto& source = vertices[indices[start+i]];
				idDrawVert& v = converted[i];
				v.Clear();
				v.xyz.Set(source.x * sx, source.y * sy, 0);
				v.st.Set(source.u, source.v);
				v.normal.Set(0,0,1); v.tangents[0].Set(1,0,0); v.tangents[1].Set(0,1,0);
				// Untextured vectors use premultiplied blending all the way to
				// the target. Current engine font images store straight coverage;
				// their uniform text tint is unpremultiplied at this boundary.
				const float inverseAlpha = handle ? (source.a > 0 ? 1.f/source.a : 0) : 1.f;
				const float components[4] = {source.r*inverseAlpha, source.g*inverseAlpha, source.b*inverseAlpha, source.a};
				for (int channel = 0; channel < 4; ++channel) {
					v.color[channel] = static_cast<byte>(idMath::ClampFloat(0,1,components[channel]) * 255.f + .5f);
					v.color2[channel] = 255;
				}
				localIndices[i] = static_cast<glIndex_t>(i);
			}
			renderSystem->SetColor4(1,1,1,1);
			renderSystem->DrawStretchPic(converted.Ptr(), localIndices.Ptr(), count, count, material, false);
		}
	}
	openq4::ui::FontMetrics GetFontMetrics(const std::string& family, int size) override {
		const fontInfo_t* font = Font(family);
		if (!font || font->pointSize <= 0) return {};
		const float scale = size / font->pointSize;
		const glyphInfo_t* x = R_GlyphForCodePoint(font,'x',NULL);
		return {font->ascender*scale, font->descender*scale, font->fontHeight*scale, x ? x->height*scale : size*.5f};
	}
	openq4::ui::Glyph GetGlyph(const std::string& family, int size, std::uint32_t codepoint) override {
		const fontInfo_t* font = Font(family);
		if (!font || font->pointSize <= 0) return {};
		const idMaterial* material = NULL;
		const glyphInfo_t* glyph = R_GlyphForCodePoint(font,codepoint,&material);
		if (!glyph) glyph = R_GlyphForCodePoint(font,'?',&material);
		if (!glyph) return {};
		const float scale = size / font->pointSize;
		return {glyph->horiAdvance*scale, glyph->horiBearingX*scale, -glyph->horiBearingY*scale,
			glyph->width*scale, glyph->height*scale, glyph->s,glyph->t,glyph->s2,glyph->t2,
			material ? material->GetName() : ""};
	}
	void Reset() { fonts.clear(); }
	int viewportWidth = 1280, viewportHeight = 720;
private:
	const fontInfo_t* Font(const std::string& family) {
		const std::string key = family == "marine" ? "marine" : family == "lowpixel" ? "lowpixel" : "chain";
		auto found = fonts.find(key);
		if (found == fonts.end()) {
			auto font = std::make_unique<fontInfoEx_t>();
			std::memset(font.get(),0,sizeof(*font));
			idStr path = va("fonts/%s/%s", cvarSystem->GetCVarString("sys_lang"),key.c_str());
			if (!renderSystem->RegisterFont(path.c_str(),*font)) {
				path = va("fonts/english/%s",key.c_str());
				if (!renderSystem->RegisterFont(path.c_str(),*font)) return NULL;
			}
			found = fonts.emplace(key,std::move(font)).first;
		}
		return &found->second->fontInfoLarge;
	}
	std::map<std::string,std::unique_ptr<fontInfoEx_t>> fonts;
};

EngineHost host;
std::unique_ptr<openq4::ui::Runtime> runtime;
std::string currentPath, currentMarkup;
int restartGeneration = -1, languageGeneration = -1;
std::chrono::steady_clock::time_point epoch;
struct ProfileSample { openq4::ui::RuntimeStatistics statistics; double engineMilliseconds; };
std::vector<ProfileSample> profile;
int profileFrames = 0;

void RecordProfile(double engineMilliseconds) {
	if (!profileFrames) return;
	profile.push_back({runtime->Statistics(),engineMilliseconds});
	if (static_cast<int>(profile.size()) < profileFrames) return;
	std::vector<double> times;
	double compileMilliseconds = 0;
	unsigned long long paths = 0, uploads = 0, hits = 0, peakBytes = 0;
	for (const auto& sample : profile) {
		times.push_back(sample.engineMilliseconds);
		compileMilliseconds += sample.statistics.vectorCompileMilliseconds;
		paths += sample.statistics.vectorPathsCompiled; uploads += sample.statistics.vectorUploads; hits += sample.statistics.vectorCacheHits;
		peakBytes = Max(peakBytes,static_cast<unsigned long long>(sample.statistics.residentGeometryBytes+sample.statistics.visibleVectorCacheBytes));
	}
	std::sort(times.begin(),times.end());
	auto percentile = [&](double fraction) { return times[static_cast<size_t>(std::ceil(fraction*times.size()))-1]; };
	common->Printf("Retained UI profile: {\"frames\":%d,\"engine_cpu_p50_ms\":%.6f,\"engine_cpu_p95_ms\":%.6f,\"engine_cpu_max_ms\":%.6f,\"vector_compile_ms\":%.6f,\"paths_compiled\":%llu,\"vector_uploads\":%llu,\"cache_hits\":%llu,\"tracked_peak_bytes\":%llu}\n",
		profileFrames,percentile(.5),percentile(.95),percentile(1),compileMilliseconds,paths,uploads,hits,peakBytes);
	profileFrames = 0; profile.clear();
}

double PresentationTime() { return std::chrono::duration<double>(std::chrono::steady_clock::now()-epoch).count(); }
bool LoadPreview(const std::string& source, const std::string& path) {
	if (!idStr::CheckExtension(path.c_str(),"q4ui")) return runtime->LoadMarkup(source,path);
	std::vector<openq4::ui::Diagnostic> diagnostics;
	if (runtime->LoadDocument(source,path,diagnostics)) return true;
	for (const auto& d : diagnostics) common->Warning("retained UI: %s:%u:%u %s: %s",path.c_str(),
		static_cast<unsigned>(d.line),static_cast<unsigned>(d.column),d.pointer.c_str(),d.message.c_str());
	return false;
}

void Close() {
	profileFrames = 0; profile.clear();
	runtime.reset();
	host.Reset();
	currentPath.clear(); currentMarkup.clear();
}
void Preview_f(const idCmdArgs& args) {
	if (args.Argc() != 2) { common->Printf("usage: ui_retainedPreview <VFS path.q4ui or path.rml>\n"); return; }
	std::string markup;
	if (!host.ReadFile(args.Argv(1),markup)) { common->Warning("retained UI: cannot read %s",args.Argv(1)); return; }
	if (!runtime) {
		runtime = std::make_unique<openq4::ui::Runtime>(host);
		epoch = std::chrono::steady_clock::now();
	}
	if (!LoadPreview(markup,args.Argv(1))) { common->Warning("retained UI: cannot load %s",args.Argv(1)); return; }
	currentPath = args.Argv(1); currentMarkup = std::move(markup);
	restartGeneration = renderSystem->GetVideoRestartCount();
	languageGeneration = LangDict_GetCodePageGeneration();
	common->Printf("Retained UI preview loaded: %s (integration spike)\n",currentPath.c_str());
}
void Close_f(const idCmdArgs&) { Close(); }
void Play_f(const idCmdArgs& args) {
	if (args.Argc() != 2) { common->Printf("usage: ui_retainedPlay <timeline ID>\n"); return; }
	if (runtime) runtime->SetReducedMotion(ui_retainedReducedMotion.GetBool(),PresentationTime());
	if (!runtime || !runtime->PlayTimeline(args.Argv(1),PresentationTime())) common->Warning("retained UI: unknown timeline %s",args.Argv(1));
	else common->Printf("Retained UI timeline played: %s\n",args.Argv(1));
}
void Profile_f(const idCmdArgs& args) {
	const int frames = args.Argc() == 2 ? atoi(args.Argv(1)) : 0;
	if (frames < 1 || frames > 3600) { common->Printf("usage: ui_retainedProfile <1..3600 frames>\n"); return; }
	if (!runtime || !runtime->IsLoaded()) { common->Warning("retained UI: profiling requires a loaded document"); return; }
	profile.clear(); profile.reserve(frames); profileFrames = frames;
}
}

void RetainedUI_Init() {
	cmdSystem->AddCommand("ui_retainedPreview",Preview_f,CMD_FL_SYSTEM,"preview a retained UI integration document");
	cmdSystem->AddCommand("ui_retainedClose",Close_f,CMD_FL_SYSTEM,"close the retained UI integration preview");
	cmdSystem->AddCommand("ui_retainedPlay",Play_f,CMD_FL_SYSTEM,"play a canonical retained UI timeline");
	cmdSystem->AddCommand("ui_retainedProfile",Profile_f,CMD_FL_SYSTEM,"measure retained UI CPU submission over bounded rendered frames");
}
void RetainedUI_Shutdown() {
	Close();
	cmdSystem->RemoveCommand("ui_retainedPreview");
	cmdSystem->RemoveCommand("ui_retainedClose");
	cmdSystem->RemoveCommand("ui_retainedPlay");
	cmdSystem->RemoveCommand("ui_retainedProfile");
}
void RetainedUI_Draw() {
	if (!runtime || !runtime->IsLoaded() || !renderSystem || !renderSystem->IsOpenGLRunning()) return;
	if (restartGeneration != renderSystem->GetVideoRestartCount() || languageGeneration != LangDict_GetCodePageGeneration()) {
		// Geometry owns font UVs and material handles. Recreate the preview
		// before using any of them after an image/font generation change.
		runtime->Shutdown(); host.Reset();
		restartGeneration = renderSystem->GetVideoRestartCount();
		languageGeneration = LangDict_GetCodePageGeneration();
		if (!LoadPreview(currentMarkup,currentPath)) return;
		epoch = std::chrono::steady_clock::now();
	}
	openq4::ui::Viewport viewport;
	viewport.width = engineWindowState.uiViewportWidth;
	viewport.height = engineWindowState.uiViewportHeight;
	if (viewport.width <= 0 || viewport.height <= 0) return;
	viewport.displayScale = ui_retainedDensity.GetFloat() > 0 ? ui_retainedDensity.GetFloat() : engineWindowState.displayScale;
	viewport.userScale = ui_retainedScale.GetFloat();
	viewport.pixelDensityX = engineWindowState.pixelDensityX; viewport.pixelDensityY = engineWindowState.pixelDensityY;
	viewport.originX = static_cast<float>(engineWindowState.uiViewportX); viewport.originY = static_cast<float>(engineWindowState.uiViewportY);
	host.viewportWidth = viewport.width; host.viewportHeight = viewport.height;
	const auto profileStart = std::chrono::steady_clock::now();
	const bool oldViewport = renderSystem->GetUseUIViewportFor2D();
	renderSystem->FlushGui();
	renderSystem->SetUseUIViewportFor2D(true);
	const double now = PresentationTime();
	runtime->SetReducedMotion(ui_retainedReducedMotion.GetBool(),now);
	runtime->Frame(viewport,now);
	renderSystem->FlushGui();
	renderSystem->SetUseUIViewportFor2D(oldViewport);
	renderSystem->SetColor4(1,1,1,1);
	RecordProfile(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-profileStart).count());
}
#else
void RetainedUI_Init() {}
void RetainedUI_Shutdown() {}
void RetainedUI_Draw() {}
#endif
