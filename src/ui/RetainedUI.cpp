// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "RetainedUI.h"

#ifndef ID_DEDICATED
#include "LegacyGuiImport.h"
#include "UserInterfaceManaged.h"
#include "retained/Runtime.h"
#include "retained/Input.h"
#include "../renderer/RendererModule.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <map>
#include <atomic>
#include <limits>
#if defined(USE_SDL3)
bool Sys_SDL_IsGameWindowFocused(void);
#endif

struct retainedUIView_t {
	std::unique_ptr<openq4::ui::Runtime> runtime;
	std::string source, path;
	retainedUIViewCallback_t callback = nullptr;
	void* owner = nullptr;
	bool canonical = true, failed = false;
};

namespace {
idCVar ui_retainedScale("ui_retainedScale", "1", CVAR_GUI | CVAR_FLOAT | CVAR_ARCHIVE,
	"retained UI density multiplier", .75f, 2.f);
idCVar ui_retainedDensity("ui_retainedDensity", "0", CVAR_GUI | CVAR_FLOAT,
	"retained UI test density override; zero uses the window display scale", 0.f, 8.f);
idCVar ui_retainedReducedMotion("ui_retainedReducedMotion", "0", CVAR_GUI | CVAR_BOOL | CVAR_ARCHIVE,
	"reduce decorative motion in the retained UI preview");
idCVar ui_retainedTrace("ui_retainedTrace", "0", CVAR_GUI | CVAR_BOOL,
	"trace retained event commits and typed application dispatch for semantic validation");

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
	bool ReadCVar(const std::string& name, size_t type, openq4::ui::StateValue& value) override {
		const idCVar* cvar = cvarSystem->Find(name.c_str());
		if (!cvar) return false;
		if (type == 2) { value = std::string(cvar->GetString()); return true; }
		if (type == 1) {
			if (!(cvar->GetFlags() & CVAR_BOOL)) return false;
			value = cvar->GetBool(); return true;
		}
		// Preserve the engine's numeric value and avoid raising the libc++
		// requirement to its newer floating-point from_chars implementation.
		if (!(cvar->GetFlags() & (CVAR_FLOAT | CVAR_INTEGER | CVAR_BOOL))) return false;
		const double number = (cvar->GetFlags() & CVAR_INTEGER) ? static_cast<double>(cvar->GetInteger()) : static_cast<double>(cvar->GetFloat());
		if (!std::isfinite(number)) return false;
		value = number; return true;
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
				const bool straightImage = handle && idStr::Icmpn(material->GetName(),"_retainedLayer/",15) != 0;
				const float inverseAlpha = straightImage ? (source.a > 0 ? 1.f/source.a : 0) : 1.f;
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
	std::uint64_t RenderFrame() const override {
		renderPresentationState_t state;
		renderSystem->GetPresentationState(state);
		return (static_cast<std::uint64_t>(static_cast<unsigned>(renderSystem->GetVideoRestartCount())) << 32) |
			static_cast<unsigned>(state.frameNumber);
	}
	bool BeginLayer(std::uint32_t id, int width, int height) override {
		// Bound the full-size transient pool to 256 MiB of RGBA8 storage.
		// Runtime leases cover stack layers, mask snapshots and filter scratch.
		if (!id || id > 48 || width <= 0 || height <= 0) return false;
		std::uint64_t bytes = static_cast<std::uint64_t>(width)*height*4;
		if (bytes > 256*1024*1024) return false;
		for (size_t i = 0; i < layers.size(); ++i) if (i != id-1 && layers[i].target) {
			bytes += static_cast<std::uint64_t>(layers[i].width)*layers[i].height*4;
		}
		if (bytes > 256*1024*1024) return false;
		// Other contexts may own mask snapshots at different dimensions.
		// Resize only this unleased slot; never discard their render targets.
		if (layers.size() < id) layers.resize(id);
		auto& layer = layers[id-1];
		if (layer.target && (layer.width != width || layer.height != height)) {
			renderSystem->DestroyRenderTexture(layer.target); layer.target = nullptr;
		}
		if (!layer.target) {
			idImageOpts options;
			options.width = width; options.height = height; options.format = FMT_RGBA8;
			options.numLevels = 1; options.isPersistant = true;
			idImage* image = renderSystem->CreateImage(va("_retainedLayerImage%u",id),&options,TF_NEAREST);
			if (!image) return false;
			layer.target = renderSystem->CreateRenderTexture(image,nullptr);
			if (!layer.target) return false;
			layer.width = width; layer.height = height;
			layer.material = declManager->FindMaterial(va("_retainedLayer/%u",id));
			layer.maskMaterial = declManager->FindMaterial(va("_retainedMask/%u",id));
			if (!layer.material || layer.material->GetState() == DS_DEFAULTED || !layer.maskMaterial || layer.maskMaterial->GetState() == DS_DEFAULTED) {
				renderSystem->DestroyRenderTexture(layer.target); layer.target = nullptr; return false;
			}
		}
		renderSystem->BindRenderTexture(layer.target,nullptr);
		renderSystem->ClearRenderTarget(true,false,1,0,0,0,0);
		return true;
	}
	void CompositeLayer(std::uint32_t source, std::uint32_t destination, float opacity, const openq4::ui::Bounds& clip) override {
		DrawLayer(layers[source-1].material,destination,opacity,clip);
	}
	void MaskLayer(std::uint32_t mask, std::uint32_t destination, const openq4::ui::Bounds& clip) override {
		DrawLayer(layers[mask-1].maskMaterial,destination,1,clip);
	}
	void DrawLayer(const idMaterial* material, std::uint32_t destination, float opacity, const openq4::ui::Bounds& clip) {
		renderSystem->BindRenderTexture(destination ? layers[destination-1].target : nullptr,nullptr);
		const float x0 = clip.x, y0 = clip.y, x1 = x0+clip.width, y1 = y0+clip.height;
		// Vulkan attachments store the top row at v=0. GL/GLES render targets
		// have the opposite origin; resolve this once at the host boundary.
		const bool topOrigin = R_RendererModule_GetStatus().activeApi == RENDER_MODULE_API_VULKAN;
		auto vertex = [&](float x, float y) -> openq4::ui::Vertex {
			return {x,y,x/viewportWidth,topOrigin ? y/viewportHeight : 1-y/viewportHeight,opacity,opacity,opacity,opacity};
		};
		Draw({vertex(x0,y0),vertex(x1,y0),vertex(x1,y1),vertex(x0,y1)}, {0,1,2,0,2,3},
			reinterpret_cast<std::uintptr_t>(material));
	}
	void EndLayer(std::uint32_t restore) override {
		renderSystem->BindRenderTexture(restore ? layers[restore-1].target : nullptr,nullptr);
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
	void Reset() {
		fonts.clear();
		ClearLayers();
	}
	void ClearLayers() {
		for (auto& layer : layers) renderSystem->DestroyRenderTexture(layer.target);
		layers.clear();
	}
	int viewportWidth = 1280, viewportHeight = 720;
private:
	struct Layer { idRenderTexture* target = nullptr; const idMaterial* material = nullptr; const idMaterial* maskMaterial = nullptr; int width = 0, height = 0; };
	std::vector<Layer> layers;
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
std::vector<retainedUIView_t*> views;
retainedUIView_t* previewView = nullptr;
openq4::ui::Runtime* runtime = nullptr; // Non-owning alias for preview commands.
bool resourcesRefreshing = false;
bool rootSubmissionPending = false;
std::uint64_t rootSubmissionFrame = 0;
openq4::ui::Input input;
std::atomic<bool> applicationOpen{false};
unsigned inputGeneration = 0;
bool inputFocused = true, inputSuspended = false, inputResourceSuspended = false;
int analogDirection = -1;
bool analogNeedsNeutral = true;
std::map<int,int> inputKeys;
std::vector<openq4::ui::ControlAction> applicationRequests;
std::string currentPath;
// One bounded developer checkpoint; never a second owner of the document.
std::string savedCheckpoint;
int restartGeneration = -1, languageGeneration = -1;
std::uint64_t languageRevision = 0, loadedLanguageRevision = (std::numeric_limits<std::uint64_t>::max)();
const std::chrono::steady_clock::time_point epoch = std::chrono::steady_clock::now();
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
	unsigned long long layerPushes = 0, layerComposites = 0, peakLayerDepth = 0;
	unsigned long long maskSnapshots = 0, maskApplications = 0, peakLayerTargets = 0;
	for (const auto& sample : profile) {
		times.push_back(sample.engineMilliseconds);
		compileMilliseconds += sample.statistics.vectorCompileMilliseconds;
		paths += sample.statistics.vectorPathsCompiled; uploads += sample.statistics.vectorUploads; hits += sample.statistics.vectorCacheHits;
		peakBytes = Max(peakBytes,static_cast<unsigned long long>(sample.statistics.residentGeometryBytes+sample.statistics.visibleVectorCacheBytes));
		layerPushes += sample.statistics.layerPushes; layerComposites += sample.statistics.layerComposites;
		peakLayerDepth = Max(peakLayerDepth,static_cast<unsigned long long>(sample.statistics.peakLayerDepth));
		maskSnapshots += sample.statistics.maskSnapshots; maskApplications += sample.statistics.maskApplications;
		peakLayerTargets = Max(peakLayerTargets,static_cast<unsigned long long>(sample.statistics.peakLayerTargets));
	}
	std::sort(times.begin(),times.end());
	auto percentile = [&](double fraction) { return times[static_cast<size_t>(std::ceil(fraction*times.size()))-1]; };
	common->Printf("Retained UI profile: {\"frames\":%d,\"engine_cpu_p50_ms\":%.6f,\"engine_cpu_p95_ms\":%.6f,\"engine_cpu_max_ms\":%.6f,\"vector_compile_ms\":%.6f,\"paths_compiled\":%llu,\"vector_uploads\":%llu,\"cache_hits\":%llu,\"tracked_peak_bytes\":%llu,\"layer_pushes\":%llu,\"layer_composites\":%llu,\"peak_layer_depth\":%llu,\"mask_snapshots\":%llu,\"mask_applications\":%llu,\"peak_layer_targets\":%llu}\n",
		profileFrames,percentile(.5),percentile(.95),percentile(1),compileMilliseconds,paths,uploads,hits,peakBytes,layerPushes,layerComposites,peakLayerDepth,maskSnapshots,maskApplications,peakLayerTargets);
	profileFrames = 0; profile.clear();
}

double PresentationTime() { return std::chrono::duration<double>(std::chrono::steady_clock::now()-epoch).count(); }
bool WindowFocused() {
#if defined(USE_SDL3)
	return Sys_SDL_IsGameWindowFocused();
#else
	return true;
#endif
}
void SetApplicationOpen(bool value) {
	if (RetainedUI_IsOpen() == value) return;
	Sys_EnterCriticalSection();
	applicationOpen.store(value,std::memory_order_release);
	Usercmd_RetainedInputChanged();
	// These queued poll samples belong to the previous owner. SDL's cached
	// physical axes remain available for release gating and menu navigation.
	Sys_ClearInputEvents();
	Sys_LeaveCriticalSection();
}
void ApplyInput() {
	for (const auto& event : input.Take()) if (runtime) {
		if (event.kind == openq4::ui::RoutedInput::Kind::Cancel) runtime->CancelInput(PresentationTime());
		else if (event.kind == openq4::ui::RoutedInput::Kind::PointerButton) runtime->PointerButton(event.down,PresentationTime());
		else runtime->MenuAction(event.menu,event.down,PresentationTime());
	}
}
void CancelInput(bool forgetSources = false, bool cancelDocument = true) {
	input.Cancel(forgetSources);
	// The adapter owns this virtual source and will require physical neutral
	// before pressing it again. Remove its quarantined hold before resetting
	// the remembered direction, or it could never receive its paired release.
	input.Menu(60000,openq4::ui::MenuInput::Next,false,false,PresentationTime());
	if (cancelDocument) ApplyInput();
	else input.Take(); // RestoreSnapshot already cancelled document transients.
	analogDirection = -1; analogNeedsNeutral = true;
}
void SuspendInput(bool suspend) {
	if (inputSuspended == suspend) return;
	inputSuspended = suspend;
	CancelInput(true); inputKeys.clear();
}
void ReleaseQuarantinedInput(const retainedUIInput_t& event) {
	if (event.down) return;
	if (event.kind == retainedUIInput_t::KEY) input.ReleaseQuarantined(event.source);
	else if (event.kind == retainedUIInput_t::POINTER_BUTTON) input.ReleaseQuarantined(65536);
}
bool MapMenuKey(int key, openq4::ui::MenuInput& action) {
	using openq4::ui::MenuInput;
	switch (key) {
		case K_TAB: {
			bool shift = idKeyInput::IsDown(K_SHIFT);
			for (const auto& held : inputKeys) if (held.second == K_SHIFT) shift = true;
			action = shift ? MenuInput::Previous : MenuInput::Next; return true;
		}
		case K_UPARROW: case K_JOY9: action = MenuInput::Up; return true;
		case K_DOWNARROW: case K_JOY10: action = MenuInput::Down; return true;
		case K_LEFTARROW: case K_JOY12: action = MenuInput::Left; return true;
		case K_RIGHTARROW: case K_JOY11: action = MenuInput::Right; return true;
		case K_ENTER: case K_KP_ENTER: case K_SPACE: case K_JOY3: action = MenuInput::Accept; return true;
		case K_ESCAPE: case K_JOY4: case K_JOY7: case K_JOY8: action = MenuInput::Back; return true;
		default: return false;
	}
}
bool RegisteredView(const retainedUIView_t* view) {
	return view && std::find(views.begin(),views.end(),view) != views.end();
}
bool LoadViewDocument(retainedUIView_t& view, const std::string& source, const std::string& path,
	bool canonical, std::vector<openq4::ui::Diagnostic>& diagnostics) {
	diagnostics.clear();
	if (canonical ? !view.runtime->LoadDocument(source,path,diagnostics) : !view.runtime->LoadMarkup(source,path)) return false;
	view.source = source; view.path = path; view.canonical = canonical; view.failed = false;
	return true;
}
void ReportViewDiagnostics(const std::string& path, const std::vector<openq4::ui::Diagnostic>& diagnostics) {
	for (const auto& d : diagnostics) common->Warning("retained UI: %s:%u:%u %s: %s",path.c_str(),
		static_cast<unsigned>(d.line),static_cast<unsigned>(d.column),d.pointer.c_str(),d.message.c_str());
}
bool LoadPreview(const std::string& source, const std::string& path) {
	std::vector<openq4::ui::Diagnostic> diagnostics;
	if (LoadViewDocument(*previewView,source,path,UI_IsRetainedPath(path.c_str()),diagnostics)) return true;
	ReportViewDiagnostics(path,diagnostics); return false;
}
void PreviewResourceEvent(void*, retainedUIViewEvent_t event) {
	if (event == retainedUIViewEvent_t::BeforeResourceReset) {
		// Save already cancelled transient presentation on its private copy.
		// Quarantine only the transport here; replaying CancelInput would start
		// fresh feedback that is discarded by the coordinated document rebuild.
		CancelInput(false,false); inputKeys.clear(); applicationRequests.clear(); ++inputGeneration;
	} else if (event == retainedUIViewEvent_t::Failed) {
		SetApplicationOpen(false); profileFrames = 0; profile.clear();
	}
	inputFocused = WindowFocused();
}

void Close() {
	SetApplicationOpen(false); ++inputGeneration;
	CancelInput(true); inputKeys.clear(); applicationRequests.clear();
	input = openq4::ui::Input{}; analogDirection = -1; analogNeedsNeutral = true;
	inputSuspended = inputResourceSuspended = false;
	profileFrames = 0; profile.clear();
	RetainedUI_DestroyView(previewView); previewView = nullptr; runtime = nullptr;
	// Shared fonts and targets belong to the host, not the preview. Their
	// bounded cache survives closing a view; shutdown/generation reset owns it.
	currentPath.clear(); savedCheckpoint.clear();
}
bool RefreshResources() {
	if (!renderSystem || resourcesRefreshing) return false;
	if (restartGeneration == renderSystem->GetVideoRestartCount() && languageGeneration == LangDict_GetCodePageGeneration() &&
		loadedLanguageRevision == languageRevision) return true;
	if (rootSubmissionPending) {
		// ClearLayers/CreateImage may reallocate names still referenced by this
		// frame's queued draws. FlushGui only emits commands; it does not execute
		// them. Defer a dirty generation until EndFrame has consumed that chain,
		// or until a later frame token proves the old front-end frame has ended.
		if (rootSubmissionFrame == host.RenderFrame()) return false;
		rootSubmissionPending = false;
	}
	resourcesRefreshing = true;
	struct SavedView { retainedUIView_t* view; bool loaded = false, valid = true, reloaded = false; std::string snapshot; };
	std::vector<SavedView> saved;
	const double savedTime = PresentationTime();
	// Capture every document before any callbacks, shutdown or shared resource
	// mutation. One failed instance must not prevent healthy peers rebuilding.
	for (auto* view : views) {
		SavedView entry{view,view->runtime->IsLoaded()};
		std::string error;
		if (entry.loaded && view->canonical && !view->runtime->SaveSnapshot(entry.snapshot,error,savedTime)) {
			entry.valid = false;
			common->Warning("retained UI: cannot preserve %s before renderer/language change: %s",view->path.c_str(),error.c_str());
		}
		saved.push_back(std::move(entry));
	}
	for (const auto& entry : saved) {
		if (entry.loaded && entry.view->callback) entry.view->callback(entry.view->owner,retainedUIViewEvent_t::BeforeResourceReset);
	}
	for (const auto& entry : saved) entry.view->runtime->Shutdown();
	// All contexts and their RmlUi resource managers are gone before the host
	// invalidates cached fonts or render targets. Keep Runtime object addresses.
	host.Reset();
	restartGeneration = renderSystem->GetVideoRestartCount();
	languageGeneration = LangDict_GetCodePageGeneration();
	loadedLanguageRevision = languageRevision;
	for (auto& entry : saved) {
		if (!entry.loaded) continue;
		auto& view = *entry.view;
		std::vector<openq4::ui::Diagnostic> diagnostics;
		entry.reloaded = entry.valid && LoadViewDocument(view,view.source,view.path,view.canonical,diagnostics);
		if (!diagnostics.empty()) ReportViewDiagnostics(view.path,diagnostics);
	}
	// Reanchor only after every document has loaded. Slow compilation of a
	// neighboring view must not consume another view's saved transition time.
	const double restoredTime = PresentationTime();
	for (const auto& entry : saved) {
		if (!entry.loaded) continue;
		auto& view = *entry.view;
		std::string error;
		bool restored = entry.reloaded;
		if (restored && view.canonical) restored = view.runtime->RestoreSnapshot(entry.snapshot,error,restoredTime);
		if (restored) {
			view.runtime->ReleaseInputSources();
			if (view.canonical) common->Printf("Retained UI instance restored after renderer/language change: %s\n",view.path.c_str());
		} else {
			view.runtime->Shutdown(); view.failed = true;
			common->Warning("retained UI: cannot restore %s after renderer/language change%s%s",view.path.c_str(),error.empty() ? "" : ": ",error.c_str());
		}
		if (view.callback) view.callback(view.owner,restored ? retainedUIViewEvent_t::Restored : retainedUIViewEvent_t::Failed);
	}
	resourcesRefreshing = false;
	return true;
}
bool Preview(const idCmdArgs& args) {
	if (args.Argc() != 2) { common->Printf("usage: ui_retainedPreview <VFS path.q4ui or path.rml>\n"); return false; }
	std::string markup;
	if (!host.ReadFile(args.Argv(1),markup)) { common->Warning("retained UI: cannot read %s",args.Argv(1)); return false; }
	if (!RefreshResources()) return false;
	if (!runtime) {
		previewView = RetainedUI_CreateView(PreviewResourceEvent,nullptr);
		if (!previewView) return false;
		runtime = RetainedUI_ViewRuntime(previewView);
	}
	if (!LoadPreview(markup,args.Argv(1))) { common->Warning("retained UI: cannot load %s",args.Argv(1)); return false; }
	SetApplicationOpen(idStr::Icmp(args.Argv(0),"ui_retainedOpen") == 0);
	CancelInput(); inputKeys.clear(); applicationRequests.clear(); ++inputGeneration;
	currentPath = args.Argv(1);
	restartGeneration = renderSystem->GetVideoRestartCount();
	languageGeneration = LangDict_GetCodePageGeneration();
	common->Printf("Retained UI preview loaded: %s (integration spike)\n",currentPath.c_str());
	return true;
}
void Preview_f(const idCmdArgs& args) { Preview(args); }
void Open_f(const idCmdArgs& args) {
	if (args.Argc() != 2 || !UI_IsRetainedPath(args.Argv(1))) {
		common->Printf("usage: ui_retainedOpen <VFS path.q4ui>\n"); return;
	}
	if (!Preview(args)) return;
	if (console) console->Close();
	inputFocused = WindowFocused();
	common->Printf("Retained UI application opened: %s\n",currentPath.c_str());
}
void Ownership_f(const idCmdArgs&) {
	common->Printf("Retained UI ownership: open=%d suspended=%d session_gui=%d game_time=%d requests=%u\n",
		RetainedUI_IsOpen(),inputSuspended,session && session->IsGUIActive(),gameEdit ? gameEdit->GetGameTime() : -1,
		static_cast<unsigned>(applicationRequests.size()));
}
void Close_f(const idCmdArgs&) { Close(); }
bool PreviewReady() { return RetainedUI_PrepareView(previewView); }
bool PreviewInputReady() {
	const bool ready = PreviewReady();
	if (!ready && !inputResourceSuspended) {
		// Resource waits can span several events in the same frame. Disarm once
		// while preserving held-source quarantine until real releases arrive.
		CancelInput(false); inputKeys.clear();
	}
	// Recovery must not cancel the presentation restored by the coordinator,
	// or forget physical holds which have yet to release.
	inputResourceSuspended = !ready;
	return ready;
}
void Checkpoint_f(const idCmdArgs& args) {
	if (args.Argc() != 2 || (idStr::Cmp(args.Argv(1),"save") && idStr::Cmp(args.Argv(1),"restore"))) {
		common->Printf("usage: ui_retainedCheckpoint <save|restore>\n"); return;
	}
	if (!PreviewReady()) { common->Warning("retained UI: checkpoint requires a loaded canonical document"); return; }
	std::string error;
	const bool save = idStr::Cmp(args.Argv(1),"save") == 0;
	const bool result = save ? runtime->SaveSnapshot(savedCheckpoint,error,PresentationTime()) :
		runtime->RestoreSnapshot(savedCheckpoint,error,PresentationTime());
	if (!result) { common->Warning("retained UI: cannot %s checkpoint: %s",args.Argv(1),error.c_str()); return; }
	if (!save) {
		CancelInput(false,false); inputKeys.clear(); applicationRequests.clear(); ++inputGeneration;
		runtime->ReleaseInputSources();
	}
	common->Printf("Retained UI checkpoint: %s bytes=%u\n",args.Argv(1),static_cast<unsigned>(savedCheckpoint.size()));
}
void Play_f(const idCmdArgs& args) {
	if (args.Argc() != 2) { common->Printf("usage: ui_retainedPlay <timeline ID>\n"); return; }
	const bool ready = PreviewReady();
	if (ready) runtime->SetReducedMotion(ui_retainedReducedMotion.GetBool(),PresentationTime());
	if (!ready || !runtime->PlayTimeline(args.Argv(1),PresentationTime())) common->Warning("retained UI: unknown timeline %s",args.Argv(1));
	else common->Printf("Retained UI timeline played: %s\n",args.Argv(1));
}
void Profile_f(const idCmdArgs& args) {
	const int frames = args.Argc() == 2 ? atoi(args.Argv(1)) : 0;
	if (frames < 1 || frames > 3600) { common->Printf("usage: ui_retainedProfile <1..3600 frames>\n"); return; }
	if (!PreviewReady()) { common->Warning("retained UI: profiling requires a loaded document"); return; }
	profile.clear(); profile.reserve(frames); profileFrames = frames;
}
void Focus_f(const idCmdArgs& args) {
	if (args.Argc() != 2) { common->Printf("usage: ui_retainedFocus <control ID>\n"); return; }
	if (!PreviewReady() || !runtime->FocusControl(args.Argv(1),PresentationTime())) common->Warning("retained UI: cannot focus %s",args.Argv(1));
}
void Menu_f(const idCmdArgs& args) {
	const std::map<std::string,openq4::ui::MenuInput> inputs = {{"next",openq4::ui::MenuInput::Next},{"previous",openq4::ui::MenuInput::Previous},
		{"up",openq4::ui::MenuInput::Up},{"down",openq4::ui::MenuInput::Down},{"left",openq4::ui::MenuInput::Left},{"right",openq4::ui::MenuInput::Right},
		{"accept",openq4::ui::MenuInput::Accept},{"back",openq4::ui::MenuInput::Back}};
	const auto found = args.Argc() == 3 ? inputs.find(args.Argv(1)) : inputs.end();
	if (found == inputs.end() || (idStr::Cmp(args.Argv(2),"0") && idStr::Cmp(args.Argv(2),"1"))) {
		common->Printf("usage: ui_retainedMenu <next|previous|up|down|left|right|accept|back> <0|1>\n"); return;
	}
	if (PreviewReady()) runtime->MenuAction(found->second,args.Argv(2)[0] == '1',PresentationTime());
}
void Enabled_f(const idCmdArgs& args) {
	if (args.Argc() != 3 || (idStr::Cmp(args.Argv(2),"0") && idStr::Cmp(args.Argv(2),"1"))) {
		common->Printf("usage: ui_retainedEnabled <control ID> <0|1>\n"); return;
	}
	if (!PreviewReady() || !runtime->SetControlEnabled(args.Argv(1),args.Argv(2)[0] == '1',PresentationTime())) common->Warning("retained UI: unknown control %s",args.Argv(1));
}
void Modal_f(const idCmdArgs& args) {
	bool result = false;
	const bool ready = PreviewReady();
	if (ready && args.Argc() == 3 && idStr::Cmp(args.Argv(1),"push") == 0) result = runtime->PushModal(args.Argv(2),PresentationTime());
	else if (ready && args.Argc() == 2 && idStr::Cmp(args.Argv(1),"pop") == 0) result = runtime->PopModal(PresentationTime());
	if (!result) common->Warning("retained UI: expected a valid ui_retainedModal push <scope ID> or pop");
}
void State_f(const idCmdArgs& args) {
	if (!PreviewReady() || args.Argc() != 2) { common->Printf("usage: ui_retainedState <control ID>\n"); return; }
	const auto state = runtime->GetControlState(args.Argv(1));
	if (!state) { common->Warning("retained UI: unknown control %s",args.Argv(1)); return; }
	const char* names[] = {"default","hover","focus","pressed","disabled"};
	openq4::ui::Bounds bounds; runtime->GetBounds(args.Argv(1),bounds);
	common->Printf("Retained UI control: %s state=%s focus=%s bounds=%.3f,%.3f,%.3f,%.3f\n",args.Argv(1),names[static_cast<unsigned>(*state)],runtime->FocusedControl().c_str(),bounds.x,bounds.y,bounds.width,bounds.height);
}
void Events_f(const idCmdArgs&) {
	if (!PreviewReady()) return;
	auto events = runtime->TakeActions();
	events.insert(events.begin(),applicationRequests.begin(),applicationRequests.end()); applicationRequests.clear();
	common->Printf("Retained UI actions: %u\n",static_cast<unsigned>(events.size()));
	for (const auto& event : events) common->Printf("Retained UI action: %s document=%s node=%s action=%s\n",
		event.kind == openq4::ui::ControlAction::Kind::Activate ? "activate" : "back",event.document.c_str(),event.node.c_str(),event.action.c_str());
}
void Data_f(const idCmdArgs& args) {
	if (!PreviewReady() || args.Argc() != 2) { common->Printf("usage: ui_retainedData <VFS state.json>\n"); return; }
	std::string source, error; openq4::ui::StateValues values; std::vector<openq4::ui::Diagnostic> diagnostics;
	if (!host.ReadFile(args.Argv(1),source)) { common->Warning("retained UI: cannot read state data %s",args.Argv(1)); return; }
	if (!openq4::ui::ParseStateValues(source,values,diagnostics)) {
		for (const auto& d : diagnostics) common->Warning("retained UI: state %s:%u:%u %s: %s",args.Argv(1),static_cast<unsigned>(d.line),static_cast<unsigned>(d.column),d.pointer.c_str(),d.message.c_str()); return;
	}
	if (!runtime->SetState(values,error,PresentationTime())) { common->Warning("retained UI: %s",error.c_str()); return; }
	common->Printf("Retained UI data: %s revision=%llu keys=%u\n",args.Argv(1),static_cast<unsigned long long>(runtime->StateRevision()),static_cast<unsigned>(values.size()));
}
void Value_f(const idCmdArgs& args) {
	if (!PreviewReady() || args.Argc() != 3) { common->Printf("usage: ui_retainedValue <node ID> <property>\n"); return; }
	const auto value = runtime->PresentedValue(args.Argv(1),args.Argv(2));
	if (!value) { common->Warning("retained UI: unknown presented value %s.%s",args.Argv(1),args.Argv(2)); return; }
	// Text remains data even in developer traces; escape controls so it cannot
	// create another apparent log record. No parsed text becomes a command.
	idStr text = value->type == openq4::ui::ValueType::Text ? value->text.c_str() : value->Css().c_str();
	text.Replace("\\","\\\\"); text.Replace("\r","\\r"); text.Replace("\n","\\n"); text.Replace("\t","\\t");
	common->Printf("Retained UI value: %s.%s=%s\n",args.Argv(1),args.Argv(2),text.c_str());
	openq4::ui::Bounds bounds;
	if (runtime->GetBounds(args.Argv(1),bounds)) common->Printf("Retained UI bounds: %s=%.3f,%.3f,%.3f,%.3f\n",args.Argv(1),bounds.x,bounds.y,bounds.width,bounds.height);
}
}

retainedUIView_t* RetainedUI_CreateView(retainedUIViewCallback_t callback, void* owner) {
	if (resourcesRefreshing) { common->Warning("retained UI: cannot register a view during resource callbacks"); return nullptr; }
	auto view = std::make_unique<retainedUIView_t>();
	view->runtime = std::make_unique<openq4::ui::Runtime>(host);
	view->callback = callback; view->owner = owner;
	views.push_back(view.get());
	return view.release();
}
void RetainedUI_DestroyView(retainedUIView_t* view) {
	if (!RegisteredView(view)) return;
	if (resourcesRefreshing) { common->FatalError("Retained UI views cannot be destroyed during resource callbacks"); return; }
	views.erase(std::find(views.begin(),views.end(),view));
	delete view;
}
openq4::ui::Runtime* RetainedUI_ViewRuntime(retainedUIView_t* view) {
	return RegisteredView(view) ? view->runtime.get() : nullptr;
}
bool RetainedUI_LoadView(retainedUIView_t* view, const std::string& source, const std::string& path,
	std::vector<openq4::ui::Diagnostic>& diagnostics) {
	diagnostics.clear();
	if (!RegisteredView(view) || !RefreshResources()) {
		diagnostics.push_back({"","Retained view is unavailable during resource lifecycle changes"}); return false;
	}
	return LoadViewDocument(*view,source,path,true,diagnostics);
}
bool RetainedUI_PrepareView(retainedUIView_t* view) {
	return RegisteredView(view) && RefreshResources() && !view->failed && view->runtime->IsLoaded();
}
bool RetainedUI_DefaultViewport(openq4::ui::Viewport& viewport) {
	viewport = {};
	viewport.width = engineWindowState.uiViewportWidth;
	viewport.height = engineWindowState.uiViewportHeight;
	viewport.displayScale = ui_retainedDensity.GetFloat() > 0 ? ui_retainedDensity.GetFloat() : engineWindowState.displayScale;
	viewport.userScale = ui_retainedScale.GetFloat();
	viewport.pixelDensityX = engineWindowState.pixelDensityX; viewport.pixelDensityY = engineWindowState.pixelDensityY;
	viewport.originX = static_cast<float>(engineWindowState.uiViewportX); viewport.originY = static_cast<float>(engineWindowState.uiViewportY);
	return viewport.width > 0 && viewport.height > 0;
}
double RetainedUI_PresentationTime() { return PresentationTime(); }
void RetainedUI_FrameSubmitted() { rootSubmissionPending = false; }
bool RetainedUI_DrawViewRoot(retainedUIView_t* view, const openq4::ui::Viewport& viewport) {
	if (!renderSystem || !renderSystem->IsOpenGLRunning() || viewport.width <= 0 || viewport.height <= 0 || !RetainedUI_PrepareView(view)) return false;
	const int oldWidth = host.viewportWidth, oldHeight = host.viewportHeight;
	const bool oldViewport = renderSystem->GetUseUIViewportFor2D();
	renderSystem->FlushGui();
	renderSystem->BindRenderTexture(nullptr,nullptr);
	renderSystem->SetUseUIViewportFor2D(true);
	host.viewportWidth = viewport.width; host.viewportHeight = viewport.height;
	rootSubmissionPending = true; rootSubmissionFrame = host.RenderFrame();
	const double now = PresentationTime();
	view->runtime->SetReducedMotion(ui_retainedReducedMotion.GetBool(),now);
	view->runtime->Frame(viewport,now);
	renderSystem->FlushGui();
	renderSystem->SetUseUIViewportFor2D(oldViewport);
	renderSystem->SetColor4(1,1,1,1);
	host.viewportWidth = oldWidth; host.viewportHeight = oldHeight;
	return true;
}

void RetainedUI_Init() {
	cmdSystem->AddCommand("ui_retainedCheckpoint",Checkpoint_f,CMD_FL_SYSTEM,"save or restore one bounded canonical instance checkpoint in memory");
	cmdSystem->AddCommand("ui_retainedData",Data_f,CMD_FL_SYSTEM,"apply a validated retained state batch from VFS JSON");
	cmdSystem->AddCommand("ui_retainedValue",Value_f,CMD_FL_SYSTEM,"inspect a retained bound or animated presentation value");
	cmdSystem->AddCommand("ui_exportLegacy",RetainedUI_ExportLegacy,CMD_FL_SYSTEM,"export native-preprocessed GUI tokens for translation without executing scripts");
	cmdSystem->AddCommand("ui_retainedPreview",Preview_f,CMD_FL_SYSTEM,"preview a retained UI integration document");
	cmdSystem->AddCommand("ui_retainedOpen",Open_f,CMD_FL_SYSTEM,"open a canonical retained document with application input ownership");
	cmdSystem->AddCommand("ui_retainedOwnership",Ownership_f,CMD_FL_SYSTEM,"inspect retained application input ownership");
	cmdSystem->AddCommand("ui_retainedClose",Close_f,CMD_FL_SYSTEM,"close the retained UI integration preview");
	cmdSystem->AddCommand("ui_retainedPlay",Play_f,CMD_FL_SYSTEM,"play a canonical retained UI timeline");
	cmdSystem->AddCommand("ui_retainedProfile",Profile_f,CMD_FL_SYSTEM,"measure retained UI CPU submission over bounded rendered frames");
	cmdSystem->AddCommand("ui_retainedFocus",Focus_f,CMD_FL_SYSTEM,"focus a semantic retained control without reading or moving a device");
	cmdSystem->AddCommand("ui_retainedMenu",Menu_f,CMD_FL_SYSTEM,"submit a semantic menu action to the retained preview");
	cmdSystem->AddCommand("ui_retainedEnabled",Enabled_f,CMD_FL_SYSTEM,"set a retained control's instance enabled state");
	cmdSystem->AddCommand("ui_retainedModal",Modal_f,CMD_FL_SYSTEM,"push or pop a retained modal input scope");
	cmdSystem->AddCommand("ui_retainedState",State_f,CMD_FL_SYSTEM,"inspect retained control feedback and focus");
	cmdSystem->AddCommand("ui_retainedEvents",Events_f,CMD_FL_SYSTEM,"read and drain semantic retained action requests");
}
void RetainedUI_Shutdown() {
	Close();
	// The manager normally destroys its owners first. Explicitly release any
	// remaining registered contexts before engine resource teardown, without
	// deleting owner-held view handles or leaving a live shared font consumer.
	resourcesRefreshing = true;
	for (auto* view : views) {
		if (view->runtime->IsLoaded() && view->callback) view->callback(view->owner,retainedUIViewEvent_t::BeforeResourceReset);
	}
	for (auto* view : views) { view->runtime->Shutdown(); view->failed = true; }
	host.Reset();
	for (auto* view : views) if (view->callback) view->callback(view->owner,retainedUIViewEvent_t::Failed);
	resourcesRefreshing = false;
	rootSubmissionPending = false;
	restartGeneration = languageGeneration = -1;
	loadedLanguageRevision = (std::numeric_limits<std::uint64_t>::max)();
	cmdSystem->RemoveCommand("ui_retainedCheckpoint");
	cmdSystem->RemoveCommand("ui_retainedData");
	cmdSystem->RemoveCommand("ui_retainedValue");
	cmdSystem->RemoveCommand("ui_exportLegacy");
	cmdSystem->RemoveCommand("ui_retainedPreview");
	cmdSystem->RemoveCommand("ui_retainedOpen");
	cmdSystem->RemoveCommand("ui_retainedOwnership");
	cmdSystem->RemoveCommand("ui_retainedClose");
	cmdSystem->RemoveCommand("ui_retainedPlay");
	cmdSystem->RemoveCommand("ui_retainedProfile");
	cmdSystem->RemoveCommand("ui_retainedFocus");
	cmdSystem->RemoveCommand("ui_retainedMenu");
	cmdSystem->RemoveCommand("ui_retainedEnabled");
	cmdSystem->RemoveCommand("ui_retainedModal");
	cmdSystem->RemoveCommand("ui_retainedState");
	cmdSystem->RemoveCommand("ui_retainedEvents");
}
void RetainedUI_Draw() {
	if (!runtime || !runtime->IsLoaded() || !renderSystem || !renderSystem->IsOpenGLRunning()) return;
	openq4::ui::Viewport viewport;
	if (!RetainedUI_DefaultViewport(viewport)) return;
	const auto profileStart = std::chrono::steady_clock::now();
	if (RetainedUI_DrawViewRoot(previewView,viewport)) RecordProfile(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-profileStart).count());
}
bool RetainedUI_IsOpen() { return applicationOpen.load(std::memory_order_acquire); }
void RetainedUI_LanguageChanged() { ++languageRevision; }
unsigned RetainedUI_InputGeneration() { return inputGeneration; }
void RetainedUI_Close() { Close(); }
#if !defined(USE_SDL3)
void RetainedUI_QueueInput(const retainedUIInput_t&, int) {}
#endif
void RetainedUI_FrameInput() {
	if (!RetainedUI_IsOpen()) return;
	const bool ready = PreviewInputReady();
	SuspendInput(!inputFocused || (console && console->Active()) || engineWindowState.uiViewportWidth <= 0 || engineWindowState.uiViewportHeight <= 0);
	if (!ready || inputSuspended) return;
	int x = 0, y = 0;
	Sys_GetJoystickAxisState(AXIS_YAW,x); Sys_GetJoystickAxisState(AXIS_PITCH,y);
	const int extent = Max(idMath::Abs(x),idMath::Abs(y));
	if (extent < 38) analogNeedsNeutral = false;
	int direction = -1;
	if (!analogNeedsNeutral && extent >= (analogDirection < 0 ? 50 : 38)) {
		using openq4::ui::MenuInput;
		direction = static_cast<int>(idMath::Abs(x) > idMath::Abs(y) ?
			(x > 0 ? MenuInput::Right : MenuInput::Left) : (y > 0 ? MenuInput::Up : MenuInput::Down));
	}
	if (direction != analogDirection) {
		if (analogDirection >= 0) input.Menu(60000,static_cast<openq4::ui::MenuInput>(analogDirection),false,false,PresentationTime());
		if (direction >= 0) input.Menu(60000,static_cast<openq4::ui::MenuInput>(direction),true,false,PresentationTime());
		analogDirection = direction;
	}
	input.Advance(PresentationTime()); ApplyInput();
}
bool RetainedUI_ProcessEvent(const sysEvent_s* event) {
	const bool transport = event->evType == SE_RETAINED_UI;
	// Refresh before checking transport generation, so input queued for a
	// pre-restart view cannot be delivered after callbacks quarantine its owner.
	const bool ready = RetainedUI_IsOpen() && PreviewInputReady();
	retainedUIInput_t decoded;
	if (transport) {
		if (!event->evPtr || event->evPtrLength != sizeof(decoded)) return true;
		std::memcpy(&decoded,event->evPtr,sizeof(decoded));
		if (decoded.kind < retainedUIInput_t::KEY || decoded.kind > retainedUIInput_t::CANCEL ||
			decoded.down < 0 || decoded.down > 1 || decoded.repeated < 0 || decoded.repeated > 1 ||
			decoded.source < 0 || decoded.source > 65535) return true;
		// Preserve physical key tracking even when a queued release belongs to
		// a document which was closed or replaced earlier in this event batch.
		if (decoded.kind == retainedUIInput_t::KEY && decoded.key > 0 && decoded.key < K_LAST_KEY)
			idKeyInput::PreliminaryKeyEvent(decoded.key,decoded.down);
		if (decoded.kind == retainedUIInput_t::FOCUS && !decoded.down) idKeyInput::ClearStates();
		if (static_cast<unsigned>(event->evValue) != inputGeneration) {
			// A release already queued by the previous owner still ends its
			// quarantine. It must never release a fresh source or reach the view.
			ReleaseQuarantinedInput(decoded);
			return true;
		}
	} else if (event->evType == SE_KEY) {
		// Non-SDL fallback and events already queued when a menu was opened.
		decoded.kind = retainedUIInput_t::KEY; decoded.key = event->evValue;
		decoded.source = event->evValue; decoded.down = event->evValue2 != 0;
	} else return RetainedUI_IsOpen();
	if (!RetainedUI_IsOpen() || !runtime) return transport;
	if (decoded.kind == retainedUIInput_t::FOCUS) inputFocused = decoded.down;
	if (!ready) {
		// Keep focus/console suspension policy, but a resource wait alone must
		// retain quarantine. Releases still retire it without reaching the view.
		SuspendInput(!inputFocused || (console && console->Active()) || engineWindowState.uiViewportWidth <= 0 || engineWindowState.uiViewportHeight <= 0);
		ReleaseQuarantinedInput(decoded);
		return true;
	}
	// Device removal/disable queues cancellation before artificial key-ups.
	// Do not advance navigation repeat or activate a pending release first.
	if (decoded.kind == retainedUIInput_t::CANCEL) { CancelInput(); return true; }
	RetainedUI_FrameInput();
	if (inputSuspended || inputResourceSuspended) { ReleaseQuarantinedInput(decoded); return true; }
	if (decoded.kind == retainedUIInput_t::KEY) {
		if (decoded.key <= 0 || decoded.key >= K_LAST_KEY || decoded.source < 0 || decoded.source > 65535) return true;
		if (decoded.down && !decoded.repeated && inputKeys.size() < 1024) inputKeys[decoded.source] = decoded.key;
		else if (!decoded.down) inputKeys.erase(decoded.source);
		openq4::ui::MenuInput action;
		if (decoded.key == K_MOUSE1) input.Pointer(decoded.source,decoded.down,PresentationTime());
		else if (MapMenuKey(decoded.key,action)) input.Menu(decoded.source,action,decoded.down,decoded.repeated,PresentationTime());
	} else if (decoded.kind == retainedUIInput_t::POINTER) {
		runtime->PointerMove(decoded.x,decoded.y,PresentationTime());
	} else if (decoded.kind == retainedUIInput_t::POINTER_BUTTON) {
		runtime->PointerMove(decoded.x,decoded.y,PresentationTime());
		input.Pointer(65536,decoded.down,PresentationTime());
	} else if (decoded.kind == retainedUIInput_t::POINTER_LEAVE) {
		runtime->PointerMove(std::numeric_limits<float>::quiet_NaN(),0,PresentationTime());
	}
	ApplyInput();
	for (const auto& action : runtime->TakeActions()) {
		if (action.kind == openq4::ui::ControlAction::Kind::Back) {
			if (!runtime->PopModal(PresentationTime())) { Close(); break; }
		} else if (applicationRequests.size() < 256) applicationRequests.push_back(action);
		else common->Warning("retained UI: application request queue overflow");
	}
	return true;
}
#else
retainedUIView_t* RetainedUI_CreateView(retainedUIViewCallback_t,void*) { return nullptr; }
void RetainedUI_DestroyView(retainedUIView_t*) {}
openq4::ui::Runtime* RetainedUI_ViewRuntime(retainedUIView_t*) { return nullptr; }
bool RetainedUI_LoadView(retainedUIView_t*,const std::string&,const std::string&,std::vector<openq4::ui::Diagnostic>&) { return false; }
bool RetainedUI_PrepareView(retainedUIView_t*) { return false; }
bool RetainedUI_DrawViewRoot(retainedUIView_t*,const openq4::ui::Viewport&) { return false; }
bool RetainedUI_DefaultViewport(openq4::ui::Viewport&) { return false; }
double RetainedUI_PresentationTime() { return 0; }
void RetainedUI_FrameSubmitted() {}
void RetainedUI_Init() {}
void RetainedUI_Shutdown() {}
void RetainedUI_Draw() {}
void RetainedUI_Close() {}
void RetainedUI_LanguageChanged() {}
bool RetainedUI_IsOpen() { return false; }
unsigned RetainedUI_InputGeneration() { return 0; }
void RetainedUI_FrameInput() {}
bool RetainedUI_ProcessEvent(const sysEvent_s*) { return false; }
void RetainedUI_QueueInput(const retainedUIInput_t&, int) {}
#endif
