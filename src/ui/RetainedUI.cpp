// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "RetainedUI.h"
#include "../framework/NativeInputPublications.h"

#ifndef ID_DEDICATED
#include "LegacyGuiImport.h"
#include "DeviceContext.h"
#include "Window.h"
#include "UserInterfaceLocal.h"
#include "RenderWindow.h"
extern idUserInterfaceManagerLocal uiManagerLocal;
#include "UserInterfaceManaged.h"
#include "retained/Runtime.h"
#include "retained/CoverageRaster.h"
#include "retained/GeneratedImagePool.h"
#include "retained/Input.h"
#include "../renderer/RendererModule.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <map>
#include <unordered_map>
#include <atomic>
#include <limits>
#include <thread>
#if defined(USE_SDL3)
bool Sys_SDL_IsGameWindowFocused(void);
int Sys_SDL_GamepadGlyphFamily(int faceLabels[4]);
#endif

struct retainedUIPreparedEditData;
struct retainedUIEditControl {
    std::uint64_t lifetime=0,revision=1;
    bool alive=true,destroyPending=false;
    std::weak_ptr<retainedUIPreparedEditData> pending;
};
struct retainedUIView_t {
    std::shared_ptr<retainedUIEditControl> edit;

	std::unique_ptr<openq4::ui::Runtime> runtime;
	std::string source, path;
	retainedUIViewCallback_t callback = nullptr;
	void* owner = nullptr;
	bool canonical = true, failed = false;
};

struct retainedUIPreparedEditData {
    std::shared_ptr<retainedUIEditControl> owner;
    retainedUIEditIdentity_t expected;
    openq4::ui::DocumentEdit* history=nullptr;
    std::unique_ptr<openq4::ui::DocumentEdit::Prepared> source;
    std::unique_ptr<openq4::ui::Runtime::PreparedDocument> canvas;
    std::string text,path;
    bool ready=true;
};
struct retainedUIPreparedEdit_t {
    std::shared_ptr<retainedUIPreparedEditData> data;
    retainedUIPreparedEdit_t* next=nullptr;
    bool destroyPending=false;
};

namespace {
idCVar ui_retainedScale("ui_retainedScale", "1", CVAR_GUI | CVAR_FLOAT | CVAR_ARCHIVE,
	"retained UI density multiplier", .75f, 2.f);
idCVar ui_retainedTextScale("ui_retainedTextScale", "1", CVAR_GUI | CVAR_FLOAT | CVAR_ARCHIVE,
	"retained UI text size multiplier, independent of furniture", 1.f, 2.f);
// The unarchived test settings are CVAR_NOCHEAT, as the session's opt-ins are,
// so joining or hosting a server without cheats does not reset them.
idCVar ui_retainedDensity("ui_retainedDensity", "0", CVAR_GUI | CVAR_FLOAT | CVAR_NOCHEAT,
	"retained UI test density override; zero uses the window display scale", 0.f, 8.f);
idCVar ui_retainedSafeArea("ui_retainedSafeArea", "", CVAR_GUI | CVAR_NOCHEAT,
	"retained UI test safe area: 'left top right bottom' pixel insets the menus keep clear, at least the window's own");
idCVar ui_retainedReducedMotion("ui_retainedReducedMotion", "0", CVAR_GUI | CVAR_BOOL | CVAR_ARCHIVE,
	"reduce decorative motion in the retained UI preview");
idCVar ui_retainedTrace("ui_retainedTrace", "0", CVAR_GUI | CVAR_BOOL | CVAR_NOCHEAT,
	"trace retained event commits and typed application dispatch for semantic validation");
idCVar ui_retainedAtlas("ui_retainedAtlas", "1", CVAR_GUI | CVAR_BOOL | CVAR_NOCHEAT,
	"cache unchanged solid vector coverage in bounded runtime texture pages");

// A render demo records 2D drawing as geometry and material names; the
// coverage atlas, cached composition layers and replayed frames hold pixels
// only this process has. While one records, the HUD draws its vectors directly
// so playback shows the whole HUD.
static bool RetainedRecordingRenderDemo() { return session != NULL && session->writeDemo != NULL; }
static bool RetainedAtlasActive() { return ui_retainedAtlas.GetBool() && !RetainedRecordingRenderDemo(); }
idCVar ui_retainedSourceCache("ui_retainedSourceCache", "1", CVAR_GUI | CVAR_BOOL | CVAR_NOCHEAT,
	"reuse unchanged vector artwork beneath moving analytic masks");
idCVar ui_retainedLinearMasks("ui_retainedLinearMasks", "1", CVAR_GUI | CVAR_BOOL | CVAR_NOCHEAT,
	"evaluate moving convex linear-alpha vector masks in the material shader");
idCVar ui_retainedReplay("ui_retainedReplay", "1", CVAR_GUI | CVAR_BOOL | CVAR_NOCHEAT,
	"replay settled presentation geometry while preserving live image sampling");
idCVar ui_retainedDirectBackdrop("ui_retainedDirectBackdrop", "1", CVAR_GUI | CVAR_BOOL | CVAR_NOCHEAT,
	"finish settled opaque full-view soft focus directly on the base surface");
idCVar ui_retainedHalfBlur("ui_retainedHalfBlur", "1", CVAR_GUI | CVAR_BOOL | CVAR_NOCHEAT,
	"evaluate broad full-view soft focus at half resolution along each blurred axis");
idCVar ui_retainedOpaqueBacking("ui_retainedOpaqueBacking", "0", CVAR_GUI | CVAR_BOOL | CVAR_ARCHIVE,
	"draw the opaque scrim behind retained modals instead of softening the screen beneath");
// Documents read this to choose between soft focus and its scrim fallback.
idCVar ui_retainedSoftFocus("ui_retainedSoftFocus", "0", CVAR_GUI | CVAR_BOOL | CVAR_ROM,
	"whether retained modals soften the screen beneath: no opaque backing and a renderer that can blur it");

class EngineHost final : public openq4::ui::Host {
public:
	bool timingRequested=false;
	bool CollectTimings() const override { return timingRequested; }
	// Root submission establishes the target before any retained drawing.
	// Within that submission an unchanged target needs no GUI flush or render
	// pass boundary; icons and cached panels can remain in the same batch.
	void BeginRoot() {
		CollectGeneratedImages();
		CollectModels();
		renderSystem->BeginGuiRoot(); boundTarget=nullptr;
		rasterDraws=rasterSavedVertices=0;
	}
	unsigned long long rasterDraws=0,rasterSavedVertices=0;
	size_t RasterPages() const { return atlasPages.size(); }
	void DumpGeometry() const {
		const auto frame=RenderFrame();
		std::vector<std::pair<std::uint64_t,const HostMesh*>> active;
		for (const auto& [id,mesh]:meshes) if (frame>=mesh.lastFrame && frame-mesh.lastFrame<=2) active.push_back({id,&mesh});
		std::sort(active.begin(),active.end(),[](const auto& a,const auto& b){return a.second->vertices.size()>b.second->vertices.size();});
		for (const auto& [id,mesh]:active) common->Printf("Retained mesh %llu: verts=%u revision=%llu stable=%u tiles=%u material=%s\n",
			static_cast<unsigned long long>(id),unsigned(mesh->vertices.size()),static_cast<unsigned long long>(mesh->revision),
			mesh->stableFrames,unsigned(mesh->rasterTiles.size()),Material(mesh->handle)->GetName());
	}
	void EndRoot() {
		std::vector<byte> upload;
		for (size_t first=0;first<atlasPages.size();first+=AtlasGroup) {
			const size_t end=(std::min)(first+AtlasGroup,atlasPages.size());
			if (std::none_of(atlasPages.begin()+first,atlasPages.begin()+end,[](const auto& page){return page.dirty;})) continue;
			// Allocation and retirement still operate on independent 512-square
			// pages. Sharing an image only lets adjacent draws batch. Reassemble
			// unchanged neighbours too; never clear a live page during upload.
			upload.assign(AtlasImageSize*AtlasImageSize*4,0);
			for (size_t i=first;i<end;++i) {
				const int x=int((i-first)%AtlasGrid)*AtlasSize,y=int((i-first)/AtlasGrid)*AtlasSize;
				for (int row=0;row<AtlasSize;++row)
					std::memcpy(upload.data()+((y+row)*AtlasImageSize+x)*4,atlasPages[i].pixels.data()+row*AtlasSize*4,AtlasSize*4);
			}
			const bool ok=renderSystem->UploadImage(va("_retainedAtlasImage%u",unsigned(first/AtlasGroup)),upload.data(),AtlasImageSize,AtlasImageSize);
			for (size_t i=first;i<end;++i) if (atlasPages[i].dirty) {
				auto& page=atlasPages[i];
				if (ok) { page.dirty=false; page.uploadFailed=false; }
				else if (!page.uploadFailed) {
					page.uploadFailed=true; common->Warning("retained UI: coverage upload unavailable; using original geometry");
				}
			}
		}
	}
	void BindTarget(idRenderTexture* target, bool flush=false) {
		if (flush || target!=boundTarget) { renderSystem->BindRenderTexture(target,nullptr); boundTarget=target; }
	}
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
		std::string source = name.compare(0,9,"material:") == 0 ? name.substr(9) : name;
		const std::string modelSource=source.compare(0,8,"_repeat/")==0 ? source.substr(8) : source;
		if (modelSource.compare(0,9,"q4-model/")==0) {
			unsigned slot=0;
			for (const char ch:modelSource.substr(9)) {
				if (ch<'0' || ch>'9' || slot>ModelCapacity) return 0;
				slot=slot*10+unsigned(ch-'0');
			}
			if (!slot || slot>ModelCapacity || !modelScenes[slot-1].owner || !modelScenes[slot-1].ready) return 0;
			const auto& model=modelScenes[slot-1]; width=model.width; height=model.height;
			return reinterpret_cast<std::uintptr_t>(model.material);
		}
		// The spike's images are direct image paths/generated font pages. Full
		// legacy multi-stage materials and movies need their own draw operation.
		// Additive pictures (image-blend additive) add their light to the target.
		const bool additive = source.compare(0,7,"q4-add/") == 0;
		if (additive) source = source.substr(7);
		const idMaterial* material = declManager->FindMaterial(((additive ? "_retainedAdd/" : "_retained/") + source).c_str());
		if (material == NULL || material->GetState() == DS_DEFAULTED) return 0;
		width = material->GetImageWidth(); height = material->GetImageHeight();
		if (width <= 0 || height <= 0) {
			// Inside a level load the image manager defers file reads to the first
			// bind. Fitted pictures (a levelshot covering the view) need their real
			// dimensions now, so bind the stage once to load it.
			materialImageInfo_t info;
			renderSystem->GetMaterialStageImageInfo(material,0,info);
			width = material->GetImageWidth(); height = material->GetImageHeight();
			if (width <= 0 || height <= 0) return 0;
		}
		return reinterpret_cast<std::uintptr_t>(material);
	}
	std::uintptr_t GenerateTexture(std::span<const std::uint8_t> pixels, int width, int height) override {
		if (!renderSystem || !pixels.data()) return 0;
		const int maximum=renderSystem->GetGLConfig().maxTextureSize;
		if (maximum<=0 || width>maximum || height>maximum) return 0;
		CollectGeneratedImages();
		const unsigned slot=generatedImages.Reserve(width,height,pixels.size());
		if (!slot) return 0;
		idImageOpts options; options.width=width; options.height=height;
		options.format=FMT_RGBA8; options.numLevels=1; options.isPersistant=true;
		const idStr imageName=va("_retainedGeneratedImage%u",slot);
		const idMaterial* material=nullptr;
		if (renderSystem->CreateImage(imageName.c_str(),&options,TF_LINEAR) &&
			renderSystem->UploadImage(imageName.c_str(),pixels.data(),width,height)) {
			material=declManager->FindMaterial(va("_retainedGenerated/%u",slot));
		}
		if (!material || material->GetState()==DS_DEFAULTED) {
			// A failed upload may already own storage. Count it until the next
			// safe frame shrinks it, just like a successfully submitted image.
			generatedImages.Retire(slot,RenderFrame()); return 0;
		}
		generatedMaterials[slot-1]=material;
		generatedImages.Publish(slot);
		return reinterpret_cast<std::uintptr_t>(material);
	}
	void ReleaseGeneratedTexture(std::uintptr_t texture) override {
		for (unsigned i=0;i<generatedMaterials.size();++i) {
			if (reinterpret_cast<std::uintptr_t>(generatedMaterials[i])==texture && generatedImages.IsLive(i+1)) {
				generatedImages.Retire(i+1,RenderFrame()); return;
			}
		}
	}
	bool RenderModelScene(std::uint64_t owner,const openq4::ui::ModelScene& scene,int width,int height,double seconds,
		std::string& image,std::string& error) override {
		if (!renderSystem || !gameEdit || !owner || !openq4::ui::ValidModelScene(scene,error)) {
			if (error.empty()) error="The model preview renderer is unavailable";
			return false;
		}
		if (session->writeDemo) { error="Model preview output is unavailable while recording a renderer demo"; return false; }
		const auto frame=RenderFrame();
		CollectModels();
		unsigned slot=ModelCapacity;
		for (unsigned i=0;i<ModelCapacity;++i) if (modelScenes[i].owner==owner) { slot=i; break; }
		std::unique_ptr<idUserInterfaceLocal> gui;
		std::unique_ptr<idRenderWindow> window;
		if (slot<ModelCapacity) {
			auto& previous=modelScenes[slot];
			if (previous.ready && previous.width==width && previous.height==height && previous.scene==scene && previous.seconds==seconds && previous.frame==frame) {
				image="q4-model/"+std::to_string(slot+1); return true;
			}
			// A changed size or second submission in one front-end frame needs a
			// fresh target; queued geometry must keep sampling its original pixels.
			if (previous.width!=width || previous.height!=height || previous.frame==frame) {
				gui=std::move(previous.gui); window=std::move(previous.window);
				ReleaseModelScene(owner); slot=ModelCapacity;
			}
		}
		if (slot==ModelCapacity) {
			const int maximum=(std::min)(4096,renderSystem->GetGLConfig().maxTextureSize);
			const std::uint64_t pixels=width>0 && height>0 ? std::uint64_t(width)*height : 0;
			if (!pixels || width>maximum || height>maximum || pixels>ModelPixelBudget-modelPixels) { error="Model preview target exceeds the shared device/memory budget"; return false; }
			for (unsigned i=0;i<ModelCapacity;++i) if (!modelScenes[i].width) { slot=i; break; }
			if (slot==ModelCapacity) { error="Model preview target pool is exhausted"; return false; }
			auto& model=modelScenes[slot];
			model.owner=owner; model.width=width; model.height=height; modelPixels+=pixels;
			model.gui=std::move(gui); model.window=std::move(window);
			idImageOpts options; options.width=width; options.height=height; options.numLevels=1; options.isPersistant=true;
			options.format=FMT_RGBA8;
			idImage* colour=renderSystem->CreateImage(va("_retainedModelColour%u",slot+1),&options,TF_NEAREST);
			idImage* resolved=renderSystem->CreateImage(va("_retainedModelImage%u",slot+1),&options,TF_LINEAR);
			options.format=FMT_DEPTH_STENCIL;
			idImage* depth=renderSystem->CreateImage(va("_retainedModelDepth%u",slot+1),&options,TF_NEAREST);
			if (colour && depth) model.target=renderSystem->CreateRenderTexture(colour,depth);
			if (resolved) model.resolved=renderSystem->CreateRenderTexture(resolved,nullptr);
			model.material=declManager->FindMaterial(va("_retainedModel/%u",slot+1));
			model.resolveMaterial=declManager->FindMaterial(va("_retainedModelResolve/%u",slot+1));
			if (!model.target || !model.resolved || !model.material || model.material->GetState()==DS_DEFAULTED || !model.resolveMaterial ||
				model.resolveMaterial->GetState()==DS_DEFAULTED || !renderSystem->ValidateMaterialArbPrograms(model.resolveMaterial)) {
				ReleaseModelScene(owner); error="Model preview target or coverage shader could not be prepared"; return false;
			}
		}
		auto& model=modelScenes[slot];
		const int time=int(std::fmod((std::max)(0.0,seconds),86400.0)*1000.0);
		if (model.window && time<model.time) model.window.reset();
		if (!model.gui) model.gui=std::make_unique<idUserInterfaceLocal>(false);
		if (!model.window) model.window=std::make_unique<idRenderWindow>(model.gui.get());
		model.window->ConfigureModelScene(scene);
		if (!model.window->PrepareModelScene(time,error)) return false;
		const bool oldViewport=renderSystem->GetUseUIViewportFor2D();
		idRenderTexture* oldTarget=boundTarget;
		renderSystem->SetUseUIViewportFor2D(false);
		BindTarget(model.target,true);
		renderSystem->CropRenderSize(width,height,false,true);
		renderSystem->ClearRenderTarget(true,true,1,0,0,0,0);
		model.window->DrawModelScene(time,float(scene.fov),width,height);
		BindTarget(model.resolved);
		renderSystem->SetColor4(1,1,1,1);
		renderSystem->DrawStretchPic(0,0,640,480,0,1,1,0,model.resolveMaterial);
		// Flush the resolve while its crop is still active, then restore every
		// surrounding output state before retained composition can begin.
		BindTarget(oldTarget);
		renderSystem->UnCrop();
		renderSystem->SetUseUIViewportFor2D(oldViewport);
		model.scene=scene; model.seconds=seconds; model.time=time; model.frame=frame; model.ready=true;
		image="q4-model/"+std::to_string(slot+1);
		if (ui_retainedTrace.GetBool()) common->Printf("RETAINED_MODEL slot=%u size=%dx%d parts=%u time=%d\n",slot+1,width,height,unsigned(scene.parts.size()),time);
		return true;
	}
	void ReleaseModelScene(std::uint64_t owner) override {
		if (!owner) return;
		for (auto& model:modelScenes) if (model.owner==owner) { model.owner=0; model.frame=RenderFrame(); model.ready=false; }
	}
	void ClearModelWorlds() {
		for (auto& model:modelScenes) { model.window.reset(); model.gui.reset(); model.ready=false; }
	}
	void PrecacheModelWorlds() {
		if (!gameEdit || !renderSystem) return;
		for (unsigned i=0;i<ModelCapacity;++i) {
			auto& model=modelScenes[i];
			if (!model.owner || model.scene.parts.empty()) continue;
			model.material=declManager->FindMaterial(va("_retainedModel/%u",i+1));
			model.resolveMaterial=declManager->FindMaterial(va("_retainedModelResolve/%u",i+1));
			model.window.reset(); // Its idWindow destructor still consults the GUI.
			model.gui=std::make_unique<idUserInterfaceLocal>(false);
			model.window=std::make_unique<idRenderWindow>(model.gui.get());
			model.window->ConfigureModelScene(model.scene);
			std::string error;
			if (!model.window->PrepareModelScene(model.time,error)) { model.window.reset(); model.gui.reset(); }
		}
	}
	void SetGeneratedImageProbe(bool enabled) {
		if (generatedProbe) ReleaseGeneratedTexture(generatedProbe);
		generatedProbe=0; generatedProbeEnabled=enabled;
	}
	bool DrawGeneratedImageProbe() {
		if (!generatedProbeEnabled) return false;
		if (!generatedProbe) {
			// A deliberately tall 1:6 RGBA8 image exercises the old cinematic
			// cube heuristic. Quadrants cover half-alpha, opaque, quarter-alpha
			// and transparent pixels. This is engine-generated diagnostic art.
			std::array<byte,2*12*4> pixels{};
			for (int y=0;y<12;++y) for (int x=0;x<2;++x) {
				byte* p=pixels.data()+(y*2+x)*4;
				if (y<6 && x==0) { p[0]=128; p[3]=128; }
				if (y<6 && x==1) { p[1]=255; p[3]=255; }
				if (y>=6 && x==0) { p[2]=64; p[3]=64; }
			}
			generatedProbe=GenerateTexture(pixels,2,12);
			if (!generatedProbe) { generatedProbeEnabled=false; common->Warning("Retained generated-image probe allocation failed"); return false; }
			common->Printf("Retained generated-image probe ready: 2x12 premultiplied RGBA8\n");
		}
		const int oldWidth=viewportWidth,oldHeight=viewportHeight;
		const bool oldViewport=renderSystem->GetUseUIViewportFor2D();
		viewportWidth=renderSystem->GetScreenWidth(); viewportHeight=renderSystem->GetScreenHeight();
		BeginRoot(); renderSystem->SetUseUIViewportFor2D(false);
		const auto quad=[this](float x,float tint,std::uintptr_t material) {
			Draw({{x,16,0,0,tint,tint,tint,material ? tint : 1.f},
				{x+128,16,1,0,tint,tint,tint,material ? tint : 1.f},
				{x+128,208,1,1,tint,tint,tint,material ? tint : 1.f},
				{x,208,0,1,tint,tint,tint,material ? tint : 1.f}}, {0,1,2,0,2,3},material);
		};
		quad(16,.25f,0); quad(160,.25f,0);
		quad(16,1.f,generatedProbe); quad(160,.5f,generatedProbe);
		EndRoot(); renderSystem->SetUseUIViewportFor2D(oldViewport); renderSystem->SetColor4(1,1,1,1);
		viewportWidth=oldWidth; viewportHeight=oldHeight;
		return true;
	}
	std::uintptr_t MultiplyMaterial() override {
		const idMaterial* material = declManager->FindMaterial("_retainedMultiply");
		return material != NULL && material->GetState() != DS_DEFAULTED ? reinterpret_cast<std::uintptr_t>(material) : 0;
	}
	void Draw(const std::vector<openq4::ui::Vertex>& vertices, const std::vector<int>& indices, std::uintptr_t handle) override {
		const idMaterial* material = Material(handle);
		Convert(vertices,indices,material,handle,scratchMesh);
		Submit(scratchMesh,material);
	}
	// Static retained geometry reaches the host unchanged every frame, so it
	// converts once for each revision, material and view size.
	void DrawMesh(std::uint64_t mesh, std::uint64_t revision, const std::vector<openq4::ui::Vertex>& vertices,
		const std::vector<int>& indices, std::uintptr_t handle) override {
		const idMaterial* material = Material(handle);
		auto& cached = meshes[mesh];
		const auto frame = RenderFrame();
		if (cached.revision != revision || cached.handle != handle || cached.width != viewportWidth || cached.height != viewportHeight) {
			DropRaster(cached);
			Convert(vertices,indices,material,handle,cached);
			cached.revision = revision; cached.handle = handle; cached.width = viewportWidth; cached.height = viewportHeight;
			cached.stableFrames=0; cached.rasterTried=false;
		} else if (cached.lastFrame!=frame) {
			++cached.stableFrames;
		}
		cached.lastFrame=frame;
		if (RetainedAtlasActive() && rasterNewMeshes && (!handle || idStr::Icmp(material->GetName(),"_retainedMultiply")==0) &&
			!cached.rasterTried && cached.stableFrames>=2 && vertices.size()>=64) {
			cached.rasterTried=true;
			CacheRaster(cached,vertices,indices);
		}
		if (RetainedAtlasActive() && !cached.rasterTiles.empty() &&
			std::none_of(cached.rasterTiles.begin(),cached.rasterTiles.end(),[&](const auto& tile){return atlasPages[tile.page].dirty;})) {
			DrawRaster(cached,1);
		} else Submit(cached,material);
	}
	bool CanDrawMeshOpacity(std::uint64_t mesh,std::uint64_t revision) const override {
		const auto found=meshes.find(mesh);
		if (!RetainedAtlasActive() || found==meshes.end()) return false;
		const auto& cached=found->second;
		if (cached.revision!=revision || cached.width!=viewportWidth || cached.height!=viewportHeight ||
			cached.handle || cached.rasterTiles.empty() ||
			std::any_of(cached.rasterTiles.begin(),cached.rasterTiles.end(),[&](const auto& tile){return atlasPages[tile.page].dirty;})) return false;
		return true;
	}
	bool DrawMeshOpacity(std::uint64_t mesh,std::uint64_t revision,float opacity) override {
		if (!CanDrawMeshOpacity(mesh,revision)) return false;
		DrawRaster(meshes.at(mesh),opacity); return true;
	}
	void ReleaseMesh(std::uint64_t mesh) override {
		const auto found=meshes.find(mesh);
		if (found!=meshes.end()) { DropRaster(found->second); meshes.erase(found); }
	}
	std::uint64_t RenderFrame() const override {
		if (!renderSystem) return 0;
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
		BindTarget(layer.target);
		renderSystem->ClearRenderTarget(true,false,1,0,0,0,0);
		return true;
	}
	bool CanCacheLayers() const override { return !RetainedRecordingRenderDemo(); }
	bool CanReplayFrames() const override { return ui_retainedReplay.GetBool() && !RetainedRecordingRenderDemo(); }
	bool TrimLayer(std::uint32_t id) override {
		if (!id || id>layers.size()) return false;
		auto& layer=layers[id-1];
		if (!layer.target || layer.target==boundTarget) return false;
		// Scratch images are owned by the image manager. Retire the FBO through
		// the normal deferred path and shrink its named image through that same
		// cross-backend allocation API; no renderer-specific deletion here.
		renderSystem->DestroyRenderTexture(layer.target); layer.target=nullptr;
		idImageOpts options;
		options.width=options.height=1; options.format=FMT_RGBA8;
		options.numLevels=1; options.isPersistant=true;
		renderSystem->CreateImage(va("_retainedLayerImage%u",id),&options,TF_NEAREST);
		layer.width=layer.height=1;
		return true;
	}
	void CompositeLayer(std::uint32_t source, std::uint32_t destination, float opacity, const openq4::ui::Bounds& clip) override {
		DrawLayer(layers[source-1].material,destination,opacity,clip);
	}
	bool CanCompositeMaskedLayer() const override { return ui_retainedSourceCache.GetBool() && !RetainedRecordingRenderDemo(); }
	void CompositeMaskedLayer(std::uint32_t source, const std::vector<openq4::ui::Vertex>& mask,
		const std::vector<int>& indices, float opacity) override {
		std::vector<openq4::ui::Vertex> vertices; vertices.reserve(mask.size());
		for (const auto& v:mask) {
			const float alpha=v.a*opacity;
			vertices.push_back({v.x,v.y,v.x/viewportWidth,1-v.y/viewportHeight,alpha,alpha,alpha,alpha});
		}
		BindTarget(nullptr);
		Draw(vertices,indices,reinterpret_cast<std::uintptr_t>(layers[source-1].material));
	}
	bool CanDrawLinearMask() const override {
		if (!ui_retainedLinearMasks.GetBool()) return false;
		const auto api=R_RendererModule_GetStatus().activeApi;
		return api==RENDER_MODULE_API_VULKAN || ((api==RENDER_MODULE_API_GL || api==RENDER_MODULE_API_GL_MODULE) &&
			renderSystem->GetGLConfig().GLSLProgramAvailable);
	}
	void DrawLinearMask(std::uint32_t source, const openq4::ui::LinearMask& mask,
		const openq4::ui::Bounds& clip, float opacity) override {
		if (clip.width<=0 || clip.height<=0) return;
		float left=viewportWidth,top=viewportHeight,right=0,bottom=0;
		for (const auto& point:mask.points) {
			left=Min(left,float(std::floor(point.x))); top=Min(top,float(std::floor(point.y)));
			right=Max(right,float(std::ceil(point.x))); bottom=Max(bottom,float(std::ceil(point.y)));
		}
		left=Max(left,clip.x); top=Max(top,clip.y);
		right=Min(right,clip.x+clip.width); bottom=Min(bottom,clip.y+clip.height);
		if (left>=right || top>=bottom) return;
		const idMaterial*& material=source ? layers[source-1].linearMaskMaterial : linearMaskMaterial;
		if (!material) material=declManager->FindMaterial(va("_retainedLinearMask/%u",source));
		if (source) BindTarget(nullptr);
		const float xs[4]={left,right,right,left},ys[4]={top,top,bottom,bottom};
		idDrawVert vertices[4];
		for (int i=0;i<4;++i) {
			auto& v=vertices[i]; v.Clear();
			v.xyz.Set(xs[i]*640.f/viewportWidth,ys[i]*480.f/viewportHeight,0);
			v.tangents[0].Set(float(mask.points[0].x),float(mask.points[0].y),float(mask.points[1].x));
			v.tangents[1].Set(float(mask.points[1].y),float(mask.points[2].x),float(mask.points[2].y));
			v.st.Set(float(mask.points[3].x),float(mask.points[3].y));
			v.normal.Set(float(mask.gradient[0]),float(mask.gradient[1]),float(mask.gradient[2]));
			for (int c=0;c<3;++c) v.color[c]=byte(std::round(mask.alpha[c]*255));
			v.color[3]=255;
		}
		const glIndex_t indices[6]={0,1,2,0,2,3};
		renderSystem->SetColor4(float(viewportWidth),float(viewportHeight),opacity,float(mask.middle));
		renderSystem->DrawStretchPic(vertices,indices,4,6,material,false);
		renderSystem->SetColor4(1,1,1,1);
	}
	void MaskLayer(std::uint32_t mask, std::uint32_t destination, const openq4::ui::Bounds& clip) override {
		DrawLayer(layers[mask-1].maskMaterial,destination,1,clip);
	}
	void DrawLayer(const idMaterial* material, std::uint32_t destination, float opacity, const openq4::ui::Bounds& clip) {
		BindTarget(destination ? layers[destination-1].target : nullptr);
		const float x0 = clip.x, y0 = clip.y, x1 = x0+clip.width, y1 = y0+clip.height;
		// Render textures keep GL row order on every backend: the Vulkan
		// executor records them lower-origin and marks those writes canonical,
		// and flips any image written the other way when a GUI stage samples it.
		// Sampling a layer as top-origin there mirrors it onto empty rows.
		auto vertex = [&](float x, float y) -> openq4::ui::Vertex {
			return {x,y,x/viewportWidth,1-y/viewportHeight,opacity,opacity,opacity,opacity};
		};
		Draw({vertex(x0,y0),vertex(x1,y0),vertex(x1,y1),vertex(x0,y1)}, {0,1,2,0,2,3},
			reinterpret_cast<std::uintptr_t>(material));
	}
	void EndLayer(std::uint32_t restore) override {
		BindTarget(restore ? layers[restore-1].target : nullptr);
	}
	// Soft focus (REN-016): the base surface behind a modal, blurred by two
	// separable Gaussian passes and desaturated into the destination layer.
	// The capture and both targets keep GL row order on every backend, so
	// each pass samples v = 1-y/h as the layer composites do.
	bool SoftenBackdrop(std::uint32_t destination, float sigma, float saturation, const openq4::ui::Bounds& region) override {
		if (!destination || destination > layers.size() || !layers[destination-1].target) return false;
		return DrawSoftenedBackdrop(layers[destination-1].target,sigma,saturation,region);
	}
	bool CanSoftenBackdropDirect(float sigma, float saturation, const openq4::ui::Bounds& region) const override {
		return ui_retainedDirectBackdrop.GetBool() && softFocus && blurScratch && backdropCreated &&
			blurWidth==viewportWidth && blurHeight==viewportHeight && blurBackdropMaterial && blurScratchMaterial &&
			viewportWidth>0 && viewportHeight>0 && captureWidth>0 && captureHeight>0 &&
			sigma>=0 && sigma<=256 && saturation>=0 && saturation<=1 &&
			region.x<=0 && region.y<=0 && region.x+region.width>=viewportWidth && region.y+region.height>=viewportHeight;
	}
	bool SoftenBackdropDirect(float sigma, float saturation, const openq4::ui::Bounds& region) override {
		if (!CanSoftenBackdropDirect(sigma,saturation,region)) return false;
		return DrawSoftenedBackdrop(nullptr,sigma,saturation,region);
	}
	bool DrawSoftenedBackdrop(idRenderTexture* destination, float sigma, float saturation, const openq4::ui::Bounds& region) {
		if (!softFocus || !(sigma>=0 && sigma<=256) || !(saturation>=0 && saturation<=1)) return false;
		const float right = static_cast<float>(viewportWidth), bottom = static_cast<float>(viewportHeight);
		const float x0 = Max(region.x,0.f), x1 = Min(region.x+region.width,right);
		const float y0 = Max(region.y,0.f), y1 = Min(region.y+region.height,bottom);
		if (!(x0 < x1 && y0 < y1) || captureWidth <= 0 || captureHeight <= 0 || !EnsureBlurTargets()) return false;
		// Decimate an axis only after filtering it: the horizontal target keeps
		// every source row, so fine vertical detail cannot alias before the
		// vertical pass. Small, wide-spaced, partial and offset kernels retain
		// the ordinary full-resolution path.
		const bool reduced = ui_retainedHalfBlur.GetBool() && !destination && sigma>=6 && sigma<=8 &&
			x0==0 && y0==0 && x1==right && y1==bottom && viewportX==0 && viewportY==0 &&
			viewportWidth==captureWidth && viewportHeight==captureHeight &&
			viewportWidth>=2 && viewportHeight>=2 && !(viewportWidth%2) && !(viewportHeight%2) && EnsureReducedBlurTargets();
		// The copy reads the whole window; the view sits at its origin within it.
		BindTarget(nullptr,true);
		renderSystem->CaptureRenderToImage("_retainedBackdrop");
		if (reduced) {
			BindTarget(reducedBlurScratch);
			DrawBlurPass(blurBackdropMaterial,{0,0,right,bottom},{0,1,1,0},{1/right,0,-sigma,1});
			BindTarget(reducedBlurResult);
			DrawBlurPass(reducedBlurMaterial,{0,0,right,bottom},{0,1,1,0},{0,1/bottom,-sigma,saturation});
			BindTarget(nullptr);
			DrawBlurPass(reducedBlurResolve,{0,0,right,bottom},{0,1,1,0},{1,1,1,1});
			return true;
		}
		// The horizontal pass covers every row the vertical pass reads.
		const float reach = std::ceil(3*sigma), top = Max(y0-reach,0.f), end = Min(y1+reach,bottom);
		const float w = static_cast<float>(captureWidth), h = static_cast<float>(captureHeight);
		BindTarget(blurScratch);
		// A full-view horizontal pass overwrites every scratch pixel opaquely.
		if (x0!=0 || x1!=right || top!=0 || end!=bottom) renderSystem->ClearRenderTarget(true,false,1,0,0,0,0);
		DrawBlurPass(blurBackdropMaterial,{x0,top,x1-x0,end-top},
			{(viewportX+x0)/w,1-(viewportY+top)/h,(viewportX+x1)/w,1-(viewportY+end)/h},{1/w,0,sigma,1});
		BindTarget(destination);
		DrawBlurPass(blurScratchMaterial,{x0,y0,x1-x0,y1-y0},{x0/right,1-y0/bottom,x1/right,1-y1/bottom},
			{0,1/bottom,sigma,saturation});
		return true;
	}
	// The speed streak: The Awakening's hs/SpeedBlur over the view. The window
	// capture holds more than the view, so an unblurred pass first copies the
	// whole view into the scratch target; the streak pass then samples it in
	// the view's own coordinates, toward its centre, into the destination layer.
	bool StreakBackdrop(std::uint32_t destination, float strength, const openq4::ui::Bounds& region) override {
		if (!softFocus || !destination || destination > layers.size() || !layers[destination-1].target ||
			!(strength > 0 && strength <= 1)) return false;
		const float right = static_cast<float>(viewportWidth), bottom = static_cast<float>(viewportHeight);
		const float x0 = Max(region.x,0.f), x1 = Min(region.x+region.width,right);
		const float y0 = Max(region.y,0.f), y1 = Min(region.y+region.height,bottom);
		if (!(x0 < x1 && y0 < y1) || captureWidth <= 0 || captureHeight <= 0 || !EnsureBlurTargets()) return false;
		if (!blurStreakMaterial) {
			const idMaterial* material = declManager->FindMaterial("_retainedBlur/streak");
			if (!material || material->GetState() == DS_DEFAULTED) return false;
			blurStreakMaterial = material;
		}
		BindTarget(nullptr,true);
		renderSystem->CaptureRenderToImage("_retainedBackdrop");
		const float w = static_cast<float>(captureWidth), h = static_cast<float>(captureHeight);
		BindTarget(blurScratch);
		DrawBlurPass(blurBackdropMaterial,{0,0,right,bottom},
			{viewportX/w,1-viewportY/h,(viewportX+right)/w,1-(viewportY+bottom)/h},{1/w,0,0,1});
		BindTarget(layers[destination-1].target);
		DrawBlurPass(blurStreakMaterial,{x0,y0,x1-x0,y1-y0},{x0/right,1-y0/bottom,x1/right,1-y1/bottom},{strength,0,0,0});
		return true;
	}
	bool EnsureBlurTargets() {
		if (blurScratch && blurWidth == viewportWidth && blurHeight == viewportHeight) return true;
		renderSystem->DestroyRenderTexture(blurScratch); blurScratch = nullptr;
		idImageOpts options;
		options.format = FMT_RGBA8; options.numLevels = 1; options.isPersistant = true;
		if (!backdropCreated) {
			// Each capture sizes this image to the window; creating it first
			// makes it a linear, clamped scratch image rather than a file.
			options.width = captureWidth; options.height = captureHeight;
			if (!renderSystem->CreateImage("_retainedBackdrop",&options,TF_LINEAR)) return false;
			backdropCreated = true;
		}
		options.width = viewportWidth; options.height = viewportHeight;
		idImage* image = renderSystem->CreateImage("_retainedBlurScratch",&options,TF_LINEAR);
		if (!image || !(blurScratch = renderSystem->CreateRenderTexture(image,nullptr))) return false;
		blurWidth = viewportWidth; blurHeight = viewportHeight;
		blurBackdropMaterial = declManager->FindMaterial("_retainedBlur/backdrop");
		blurScratchMaterial = declManager->FindMaterial("_retainedBlur/scratch");
		if (!blurBackdropMaterial || blurBackdropMaterial->GetState() == DS_DEFAULTED ||
			!blurScratchMaterial || blurScratchMaterial->GetState() == DS_DEFAULTED) {
			renderSystem->DestroyRenderTexture(blurScratch); blurScratch = nullptr; return false;
		}
		return true;
	}
	bool EnsureReducedBlurTargets() {
		if (reducedBlurScratch && reducedBlurResult && reducedBlurWidth==viewportWidth && reducedBlurHeight==viewportHeight) return true;
		renderSystem->DestroyRenderTexture(reducedBlurScratch); reducedBlurScratch=nullptr;
		renderSystem->DestroyRenderTexture(reducedBlurResult); reducedBlurResult=nullptr;
		idImageOpts options;
		options.width=(viewportWidth+1)/2; options.height=viewportHeight;
		options.format=FMT_RGBA8; options.numLevels=1; options.isPersistant=true;
		idImage* scratch=renderSystem->CreateImage("_retainedReducedBlurScratch",&options,TF_LINEAR);
		if (!scratch || !(reducedBlurScratch=renderSystem->CreateRenderTexture(scratch,nullptr))) return false;
		options.height=(viewportHeight+1)/2;
		idImage* result=renderSystem->CreateImage("_retainedReducedBlurResult",&options,TF_LINEAR);
		if (!result || !(reducedBlurResult=renderSystem->CreateRenderTexture(result,nullptr))) return false;
		reducedBlurMaterial=declManager->FindMaterial("_retainedBlur/reducedScratch");
		reducedBlurResolve=declManager->FindMaterial("_retainedBlur/reducedResult");
		if (!reducedBlurMaterial || reducedBlurMaterial->GetState()==DS_DEFAULTED ||
			!reducedBlurResolve || reducedBlurResolve->GetState()==DS_DEFAULTED) return false;
		reducedBlurWidth=viewportWidth; reducedBlurHeight=viewportHeight;
		return true;
	}
	// One blur pass over a view-space box. The program reads the step along its
	// axis (in the source's UV), the sigma in pixels and the saturation from
	// parm0..3, which the GUI colour supplies unclamped.
	void DrawBlurPass(const idMaterial* material, const openq4::ui::Bounds& box, const float (&uv)[4], const float (&parms)[4]) {
		const float sx = 640.f / viewportWidth, sy = 480.f / viewportHeight;
		const float xs[4] = {box.x,box.x+box.width,box.x+box.width,box.x}, ys[4] = {box.y,box.y,box.y+box.height,box.y+box.height};
		const float us[4] = {uv[0],uv[2],uv[2],uv[0]}, vs[4] = {uv[1],uv[1],uv[3],uv[3]};
		idDrawVert vertices[4];
		for (int i = 0; i < 4; ++i) {
			idDrawVert& v = vertices[i];
			v.Clear();
			v.xyz.Set(xs[i]*sx,ys[i]*sy,0);
			v.st.Set(us[i],vs[i]);
			v.normal.Set(0,0,1); v.tangents[0].Set(1,0,0); v.tangents[1].Set(0,1,0);
			for (int channel = 0; channel < 4; ++channel) v.color[channel] = v.color2[channel] = 255;
		}
		const glIndex_t indices[6] = {0,1,2,0,2,3};
		renderSystem->SetColor4(parms[0],parms[1],parms[2],parms[3]);
		renderSystem->DrawStretchPic(vertices,indices,4,6,material,false);
		renderSystem->SetColor4(1,1,1,1);
	}
	bool LayoutLegacyText(const std::string& text, const std::string& family, float scale,
		float width, float height, int align, bool clip, std::vector<openq4::ui::HudPrimitive>& output) override {
		uiManagerLocal.CaptureLegacyText(text.c_str(),("fonts/"+FontFamily(family)).c_str(),scale,width,height,align,clip,output);
		return true;
	}
	openq4::ui::FontMetrics GetFontMetrics(const std::string& family, int size) override {
		renderFontMetrics_t metrics;
		const std::string key = FontFamily(family);
		std::string path = va("fonts/%s/%s",cvarSystem->GetCVarString("sys_lang"),key.c_str());
		bool scalable = renderSystem->GetRetainedFontMetrics(path.c_str(),size,metrics);
		if (!scalable) {
			path = "fonts/english/"+key;
			scalable = renderSystem->GetRetainedFontMetrics(path.c_str(),size,metrics);
		}
		if (scalable) {
			scalableFaces[key] = path;
			return {metrics.ascent,metrics.descent,metrics.lineSpacing,metrics.xHeight};
		}
		ReportFontFallback();
		const fontInfo_t* font = Font(family);
		if (!font || font->pointSize <= 0) return {};
		const float scale = size / font->pointSize;
		const glyphInfo_t* x = R_GlyphForCodePoint(font,'x',NULL);
		return {font->ascender*scale, font->descender*scale, font->fontHeight*scale, x ? x->height*scale : size*.5f};
	}
	openq4::ui::Glyph GetGlyph(const std::string& family, int size, std::uint32_t codepoint) override {
		const std::string key = FontFamily(family);
		if (key == "strogg") codepoint = RuneScalar(codepoint);
		const auto found = scalableFaces.find(key);
		renderFontGlyph_t scaled;
		if (found != scalableFaces.end() && renderSystem->GetRetainedFontGlyph(found->second.c_str(),size,codepoint,scaled))
			return {scaled.advance,scaled.left,scaled.top,scaled.width,scaled.height,
				scaled.u0,scaled.v0,scaled.u1,scaled.v1,scaled.image};
		ReportFontFallback();
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
	// A fatal error in startup tears everything down before the renderer module
	// loads, so there may be no renderer: its font cache and targets are then
	// gone with it, and only the handles are dropped.
	void Reset() {
		meshes.clear();
		for (unsigned i=0;i<ModelCapacity;++i) FreeModel(i);
		generatedImages.Reset([this](unsigned slot){ return ShrinkGeneratedImage(slot); });
		generatedMaterials.fill(nullptr);
		generatedProbe=0;
		if (renderSystem) for (size_t i=0;i<atlasPages.size();i+=AtlasGroup) {
			idImageOpts options; options.width=options.height=1; options.format=FMT_RGBA8;
			options.numLevels=1; options.isPersistant=true;
			renderSystem->CreateImage(va("_retainedAtlasImage%u",unsigned(i/AtlasGroup)),&options,TF_NEAREST);
		}
		atlasPages.clear();
		if (renderSystem) renderSystem->ResetRetainedFontCache();
		scalableFaces.clear(); fontFallbackReported = false;
		fonts.clear();
		ClearLayers();
	}
	void ClearLayers() {
		if (renderSystem) {
			for (auto& layer : layers) renderSystem->DestroyRenderTexture(layer.target);
			renderSystem->DestroyRenderTexture(blurScratch);
			renderSystem->DestroyRenderTexture(reducedBlurScratch);
			renderSystem->DestroyRenderTexture(reducedBlurResult);
		}
		layers.clear(); blurScratch = nullptr;
		blurBackdropMaterial = blurScratchMaterial = blurStreakMaterial = nullptr;
		linearMaskMaterial=nullptr;
		backdropCreated = false; blurWidth = blurHeight = 0;
		reducedBlurScratch=reducedBlurResult=nullptr;
		reducedBlurMaterial=reducedBlurResolve=nullptr;
		reducedBlurWidth=reducedBlurHeight=0;
	}
	int viewportWidth = 1280, viewportHeight = 720;
	// The view's origin in the window and the window's size, which the
	// backdrop capture copies; set for each drawn view.
	float viewportX = 0, viewportY = 0;
	int captureWidth = 0, captureHeight = 0;
	bool softFocus = false;
	// Startup's screen lives seconds: a CPU raster of its large meshes stalls the
	// frame it lands in and saves less submission than it costs.
	bool rasterNewMeshes = true;
private:
	static constexpr unsigned ModelCapacity=64;
	static constexpr std::uint64_t ModelPixelBudget=16u*1024u*1024u; // Colour/resolved RGBA plus depth/stencil: at most 256 MiB.
	struct ModelTarget {
		std::uint64_t owner=0, frame=0;
		int width=0,height=0,time=0;
		double seconds=0;
		bool ready=false;
		openq4::ui::ModelScene scene;
		std::unique_ptr<idUserInterfaceLocal> gui;
		std::unique_ptr<idRenderWindow> window;
		idRenderTexture* target=nullptr;
		idRenderTexture* resolved=nullptr;
		const idMaterial* material=nullptr;
		const idMaterial* resolveMaterial=nullptr;
	};
	std::array<ModelTarget,ModelCapacity> modelScenes;
	std::uint64_t modelPixels=0;
	void FreeModel(unsigned slot) {
		auto& model=modelScenes[slot];
		if (!model.width) return;
		model.window.reset(); model.gui.reset();
		if (renderSystem) {
			renderSystem->DestroyRenderTexture(model.target); renderSystem->DestroyRenderTexture(model.resolved);
			idImageOpts options; options.width=options.height=1; options.numLevels=1; options.isPersistant=true; options.format=FMT_RGBA8;
			renderSystem->CreateImage(va("_retainedModelColour%u",slot+1),&options,TF_NEAREST);
			renderSystem->CreateImage(va("_retainedModelImage%u",slot+1),&options,TF_LINEAR);
			options.format=FMT_DEPTH_STENCIL;
			renderSystem->CreateImage(va("_retainedModelDepth%u",slot+1),&options,TF_NEAREST);
		}
		modelPixels-=std::uint64_t(model.width)*model.height;
		model={};
	}
	void CollectModels() {
		const auto frame=RenderFrame();
		for (unsigned i=0;i<ModelCapacity;++i) if (modelScenes[i].width && !modelScenes[i].owner && modelScenes[i].frame!=frame) FreeModel(i);
	}
	openq4::ui::GeneratedImagePool generatedImages;
	std::array<const idMaterial*,openq4::ui::GeneratedImagePool::Capacity> generatedMaterials{};
	std::uintptr_t generatedProbe=0;
	bool generatedProbeEnabled=false;
	bool ShrinkGeneratedImage(unsigned slot) {
		if (!renderSystem) return true; // Renderer/module teardown already owns the images.
		idImageOpts options; options.width=options.height=1; options.format=FMT_RGBA8;
		options.numLevels=1; options.isPersistant=true;
		return renderSystem->CreateImage(va("_retainedGeneratedImage%u",slot),&options,TF_LINEAR)!=nullptr;
	}
	void CollectGeneratedImages() {
		generatedImages.Collect(RenderFrame(),[this](unsigned slot){ return ShrinkGeneratedImage(slot); });
	}
	// Engine vertices and the chunks that submit them, for one mesh.
	struct HostMesh {
		struct Chunk { int first, count; size_t index, indices; };
		std::vector<idDrawVert> vertices;
		std::vector<glIndex_t> indices;
		std::vector<Chunk> chunks;
		std::uint64_t revision = 0;
		std::uintptr_t handle = 0;
		int width = 0, height = 0;
		struct RasterTile { int page; std::array<idDrawVert,4> quad; };
		std::vector<RasterTile> rasterTiles;
		unsigned stableFrames=0;
		std::uint64_t lastFrame=0;
		bool rasterTried=false;
	};
	// Fixed-size pages keep CPU/GPU storage bounded at 32 MiB each. Append-only
	// packing never changes a live mesh's pixels, including queued SMP draws.
	// Only an entirely unused page, retired for four frames, can be recycled.
	static constexpr int AtlasSize=512, AtlasPages=32;
	static constexpr int AtlasGrid=4, AtlasGroup=AtlasGrid*AtlasGrid, AtlasImageSize=AtlasGrid*AtlasSize;
	struct AtlasPage {
		std::vector<byte> pixels;
		const idMaterial* material=nullptr;
		const idMaterial* multiplyMaterial=nullptr;
		int x=0,y=0,rowHeight=0,references=0;
		std::uint64_t lastFrame=0;
		bool dirty=false,uploadFailed=false;
	};
	std::vector<AtlasPage> atlasPages;
	void DrawRaster(HostMesh& mesh,float opacity) {
		renderSystem->SetColor4(1,1,1,1);
		++rasterDraws; rasterSavedVertices+=mesh.vertices.size()-mesh.rasterTiles.size()*4;
		const auto frame=RenderFrame();
		mesh.lastFrame=frame;
		const byte alpha=static_cast<byte>(std::clamp(opacity,0.f,1.f)*255.f+.5f);
		for (const auto& tile:mesh.rasterTiles) {
			auto& page=atlasPages[tile.page]; page.lastFrame=frame;
			const glIndex_t quad[6]={0,1,2,0,2,3};
			const auto* material=mesh.handle ? page.multiplyMaterial : page.material;
			// Settled HUD panels already have opaque white modulation. Submit
			// their cached quads directly; only a fade needs a coloured copy.
			if (alpha==255) renderSystem->DrawStretchPic(tile.quad.data(),quad,4,6,material,false);
			else {
				auto vertices=tile.quad;
				for (auto& vertex:vertices) for (int c=0;c<4;++c) vertex.color[c]=vertex.color2[c]=alpha;
				renderSystem->DrawStretchPic(vertices.data(),quad,4,6,material,false);
			}
		}
	}
	void DropRaster(HostMesh& mesh) {
		const auto frame=RenderFrame();
		for (const auto& tile:mesh.rasterTiles) {
			auto& page=atlasPages[tile.page]; --page.references; page.lastFrame=frame;
		}
		mesh.rasterTiles.clear();
	}
	void CacheRaster(HostMesh& mesh,const std::vector<openq4::ui::Vertex>& vertices,const std::vector<int>& indices) {
		openq4::ui::CoverageBitmap bitmap;
		if (!openq4::ui::RasterizeCoverage(vertices,indices,bitmap,8192,mesh.handle!=0)) return;
		// Wide frames and tall rails keep the same exact pixel coverage, split
		// across the existing bounded pages. Never publish a partial mesh or a
		// tile whose page upload has not succeeded.
		constexpr int extent=AtlasSize-2;
		if (size_t((bitmap.width+extent-1)/extent)*((bitmap.height+extent-1)/extent)*4>=vertices.size()) return;
		for (int y=0;y<bitmap.height;y+=extent) for (int x=0;x<bitmap.width;x+=extent) {
			const int width=Min(extent,bitmap.width-x),height=Min(extent,bitmap.height-y);
			// Rails and disconnected shapes often touch only a small part of
			// a tile. Trim identity borders before packing: transparent for
			// premultiplied blending, white RGBA for multiplication. Preserve
			// additive colour with zero alpha and multiplication of alpha itself.
			int left=width,top=height,right=0,bottom=0;
			for (int row=0;row<height;++row) {
				const byte* pixels=bitmap.pixels.data()+((y+row)*bitmap.width+x)*4;
				for (int column=0;column<width;++column) {
					const byte* pixel=pixels+column*4;
					if (mesh.handle ? (pixel[0]&pixel[1]&pixel[2]&pixel[3])==255 : !(pixel[0]|pixel[1]|pixel[2]|pixel[3])) continue;
					left=Min(left,column);right=Max(right,column+1);
					top=Min(top,row);bottom=Max(bottom,row+1);
				}
			}
			if (left<right && top<bottom && !CacheRasterTile(mesh,bitmap,x+left,y+top,right-left,bottom-top)) {
				DropRaster(mesh); return;
			}
		}
	}
	bool CacheRasterTile(HostMesh& mesh,const openq4::ui::CoverageBitmap& bitmap,int tileX,int tileY,int width,int height) {
		const int w=width+2,h=height+2;
		int chosen=-1,x=0,y=0;
		const auto frame=RenderFrame();
		for (int i=0;i<=int(atlasPages.size()) && i<AtlasPages;++i) {
			if (i==int(atlasPages.size())) {
				if (i%AtlasGroup==0) {
					idImageOpts options; options.width=options.height=AtlasImageSize; options.format=FMT_RGBA8;
					options.numLevels=1; options.isPersistant=true;
					if (!renderSystem->CreateImage(va("_retainedAtlasImage%u",unsigned(i/AtlasGroup)),&options,TF_NEAREST)) return false;
				}
				AtlasPage page; page.pixels.resize(AtlasSize*AtlasSize*4);
				page.material=declManager->FindMaterial(va("_retainedAtlas/%u",unsigned(i/AtlasGroup)));
				if (!page.material || page.material->GetState()==DS_DEFAULTED) return false;
				atlasPages.push_back(std::move(page));
			}
			auto& page=atlasPages[i];
			if (!page.references && frame>page.lastFrame && frame-page.lastFrame>4) {
				std::fill(page.pixels.begin(),page.pixels.end(),0); page.x=page.y=page.rowHeight=0; page.dirty=true;
			}
			x=page.x; y=page.y;
			if (x+w>AtlasSize) { x=0; y+=page.rowHeight; }
			if (y+h>AtlasSize) continue;
			if (mesh.handle && !page.multiplyMaterial) {
				page.multiplyMaterial=declManager->FindMaterial(va("_retainedMultiplyAtlas/%u",unsigned(i/AtlasGroup)));
				if (!page.multiplyMaterial || page.multiplyMaterial->GetState()==DS_DEFAULTED) return false;
			}
			chosen=i;
			if (y!=page.y) page.rowHeight=0;
			page.x=x+w; page.y=y; page.rowHeight=(std::max)(page.rowHeight,h);
			++page.references; page.lastFrame=frame; page.dirty=true;
			// Atlas pages can contain both blend modes. The padding must use
			// this tile's identity even when its neighbours use another mode.
			for (int row=0;row<h;++row)
				std::memset(page.pixels.data()+((y+row)*AtlasSize+x)*4,mesh.handle ? 255 : 0,w*4);
			for (int row=0;row<height;++row)
				std::memcpy(page.pixels.data()+((y+1+row)*AtlasSize+x+1)*4,bitmap.pixels.data()+((tileY+row)*bitmap.width+tileX)*4,width*4);
			break;
		}
		if (chosen<0) return false;
		HostMesh::RasterTile tile; tile.page=chosen;
		const float sx=640.f/viewportWidth,sy=480.f/viewportHeight;
		for (int i=0;i<4;++i) {
			const bool right=i==1 || i==2,bottom=i>=2;
			auto& vertex=tile.quad[i]; vertex.Clear();
			vertex.xyz.Set((bitmap.x+tileX+(right?width:0))*sx,(bitmap.y+tileY+(bottom?height:0))*sy,0);
			vertex.st.Set(float((chosen%AtlasGrid)*AtlasSize+x+1+(right?width:0))/AtlasImageSize,
				float(((chosen%AtlasGroup)/AtlasGrid)*AtlasSize+y+1+(bottom?height:0))/AtlasImageSize);
			vertex.normal.Set(0,0,1); vertex.tangents[0].Set(1,0,0); vertex.tangents[1].Set(0,1,0);
			for (int channel=0;channel<4;++channel) vertex.color[channel]=vertex.color2[channel]=255;
		}
		mesh.rasterTiles.push_back(std::move(tile));
		return true;
	}
	HostMesh scratchMesh;
	std::unordered_map<std::uint64_t,HostMesh> meshes;
	const idMaterial* Material(std::uintptr_t handle) const {
		return handle ? reinterpret_cast<const idMaterial*>(handle) : declManager->FindMaterial("_retainedSolid");
	}
	void Convert(const std::vector<openq4::ui::Vertex>& vertices, const std::vector<int>& indices, const idMaterial* material,
		std::uintptr_t handle, HostMesh& mesh) const {
		// Untextured vectors use premultiplied blending all the way to
		// the target. Current engine font images store straight coverage;
		// their uniform text tint is unpremultiplied at this boundary.
		// Multiply factors are plain colours for a dst*src blend.
		// Additive pictures keep premultiplied tints: the tint scales their light.
		// Light carries no coverage: its zero alpha keeps an add blend from
		// writing a composition layer's alpha, which would otherwise
		// composite the picture's whole rectangle as an opaque box.
		const bool additiveImage = handle && idStr::Icmpn(material->GetName(),"_retainedAdd/",13) == 0;
		const bool straightImage = handle && idStr::Icmpn(material->GetName(),"_retainedLayer/",15) != 0 &&
			idStr::Icmpn(material->GetName(),"_retainedGenerated/",19) != 0 &&
			idStr::Icmp(material->GetName(),"_retainedMultiply") != 0 && !additiveImage;
		const float sx = 640.f / viewportWidth, sy = 480.f / viewportHeight;
		mesh.vertices.resize(vertices.size());
		for (size_t i = 0; i < vertices.size(); ++i) {
			const auto& source = vertices[i];
			idDrawVert& v = mesh.vertices[i];
			v.xyz.Set(source.x * sx, source.y * sy, 0);
			v.st.Set(source.u, source.v);
			v.normal.Set(0,0,1); v.tangents[0].Set(1,0,0); v.tangents[1].Set(0,1,0);
			const float inverseAlpha = straightImage ? (source.a > 0 ? 1.f/source.a : 0) : 1.f;
			const float components[4] = {source.r*inverseAlpha, source.g*inverseAlpha, source.b*inverseAlpha, additiveImage ? 0.f : source.a};
			for (int channel = 0; channel < 4; ++channel) {
				v.color[channel] = static_cast<byte>(idMath::ClampFloat(0,1,components[channel]) * 255.f + .5f);
				v.color2[channel] = 255;
			}
		}
		// Chunk whole triangles below the engine's surface vertex limit and a
		// frame allocation block of indices. A chunk passes the contiguous range
		// of vertices its triangles use, which tessellated meshes fill in order,
		// so local indices also work when a large vector mesh uses indices wider
		// than glIndex_t. A triangle spanning more than a chunk gets its own copy.
		constexpr int chunkVertices = 12000;
		constexpr size_t chunkIndices = 36000;
		mesh.indices.clear(); mesh.chunks.clear();
		size_t start = 0;
		int low = 0, high = -1; // An empty range.
		const auto flush = [&](size_t end) {
			if (end <= start) return;
			mesh.chunks.push_back({low,high-low+1,mesh.indices.size(),end-start});
			for (size_t i = start; i < end; ++i) mesh.indices.push_back(static_cast<glIndex_t>(indices[i]-low));
		};
		const size_t count = indices.size()-indices.size()%3;
		for (size_t i = 0; i < count; i += 3) {
			const int a = indices[i], b = indices[i+1], c = indices[i+2];
			const int triangleLow = Min(a,Min(b,c)), triangleHigh = Max(a,Max(b,c));
			if (triangleHigh-triangleLow >= chunkVertices) {
				flush(i);
				const idDrawVert corners[3] = {mesh.vertices[a],mesh.vertices[b],mesh.vertices[c]};
				mesh.chunks.push_back({static_cast<int>(mesh.vertices.size()),3,mesh.indices.size(),3});
				mesh.vertices.insert(mesh.vertices.end(),corners,corners+3);
				mesh.indices.insert(mesh.indices.end(),{0,1,2});
				start = i+3; high = -1; continue;
			}
			const bool empty = high < low;
			const int nextLow = empty ? triangleLow : Min(low,triangleLow), nextHigh = empty ? triangleHigh : Max(high,triangleHigh);
			if (!empty && (nextHigh-nextLow >= chunkVertices || i+3-start > chunkIndices)) {
				flush(i); start = i; low = triangleLow; high = triangleHigh;
			} else { low = nextLow; high = nextHigh; }
		}
		flush(count);
	}
	void Submit(const HostMesh& mesh, const idMaterial* material) {
		renderSystem->SetColor4(1,1,1,1);
		for (const auto& chunk : mesh.chunks) {
			renderSystem->DrawStretchPic(mesh.vertices.data()+chunk.first,mesh.indices.data()+chunk.index,chunk.count,
				static_cast<int>(chunk.indices),material,false);
		}
	}
	struct Layer { idRenderTexture* target = nullptr; const idMaterial* material = nullptr; const idMaterial* maskMaterial = nullptr; int width = 0, height = 0; const idMaterial* linearMaskMaterial=nullptr; };
	const idMaterial* linearMaskMaterial=nullptr;
	std::vector<Layer> layers;
	idRenderTexture* boundTarget=nullptr;
	idRenderTexture* blurScratch = nullptr;
	const idMaterial* blurBackdropMaterial = nullptr;
	const idMaterial* blurScratchMaterial = nullptr;
	const idMaterial* blurStreakMaterial = nullptr;
	bool backdropCreated = false;
	int blurWidth = 0, blurHeight = 0;
	idRenderTexture *reducedBlurScratch=nullptr, *reducedBlurResult=nullptr;
	const idMaterial *reducedBlurMaterial=nullptr, *reducedBlurResolve=nullptr;
	int reducedBlurWidth=0, reducedBlurHeight=0;
	static std::string FontFamily(const std::string& family) {
		// The faces a document may name; anything else draws in Chain.
		static const char* const faces[] = {"marine","lowpixel","profont","r_strogg","strogg"};
		for (const char* face : faces) if (family == face) return face;
		return "chain";
	}
	// The rune face maps the Latin alphabet onto Strogg runes and leaves most
	// punctuation blank (visual specification section 5). Text in any other
	// script still reads as runes: its letters fold onto the alphabet and its
	// punctuation becomes a space, so a translated label never shows the
	// face's '?' in the rune copy. Of the letters past ASCII the face carries
	// only Latin-1's, less the six folded here; every other letter folds.
	static std::uint32_t RuneScalar(std::uint32_t scalar) {
		if (scalar < 0x20 || scalar == 0x7f) return ' ';   // controls
		if (scalar < 0x80) {
			static const char blank[] = "!#$%&*+<=>@^_`~";
			for (const char* c = blank; *c; ++c) if (scalar == static_cast<unsigned char>(*c)) return ' ';
			return scalar;
		}
		switch (scalar) {
		case 0xdf: return 'S';                       // sharp s
		case 0xc6: case 0xe6: return 'E';            // ae
		case 0xd8: case 0xf8: return 'O';            // o with stroke
		case 0xd0: case 0xf0: return 'D';            // eth
		case 0xde: case 0xfe: return 'P';            // thorn
		case 0x141: case 0x142: return 'L';          // l with stroke
		default: break;
		}
		if (scalar < 0xc0 || scalar == 0xd7 || scalar == 0xf7) return ' ';
		if (scalar < 0x100) return scalar;           // Latin-1 letters
		// Combining marks, general and CJK punctuation, and the variation
		// selectors and specials carry no letter of their own.
		if ((scalar >= 0x300 && scalar < 0x370) || (scalar >= 0x2000 && scalar < 0x2070) ||
			(scalar >= 0x3000 && scalar < 0x3040) || (scalar >= 0xfe00 && scalar < 0xfe10) || scalar >= 0xfff0) return ' ';
		return 'A'+scalar%26;
	}
	void ReportFontFallback() {
		if (fontFallbackReported) return;
		fontFallbackReported = true;
		common->Warning("retained UI: output-size font unavailable or cache limit reached; using legacy font fallback");
	}
	bool fontFallbackReported = false;
	std::map<std::string,std::string> scalableFaces;
	const fontInfo_t* Font(const std::string& family) {
		const std::string key = FontFamily(family);
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
retainedUIView_t* initializationView = nullptr;
bool initializationEntered = false; // onEnter ran: the first frame is on screen.
std::uint64_t languageRevision = 0, loadedLanguageRevision = (std::numeric_limits<std::uint64_t>::max)();
const std::chrono::steady_clock::time_point epoch = std::chrono::steady_clock::now();
struct ProfileSample { openq4::ui::RuntimeStatistics statistics; double engineMilliseconds; };
std::vector<ProfileSample> profile;
int profileFrames = 0;
// Without a preview document the profile covers the session-owned root views
// (title, pause, SYSTEM, loading), per rendered frame: their summed CPU
// submission and statistics, and the wall interval to the next frame, which
// an uncapped frame rate turns into the whole frame's cost.
struct RootProfileSample {
	std::uint64_t frame = 0;
	std::chrono::steady_clock::time_point at;
	double cpu = 0, update = 0, render = 0, presentation = 0, layout = 0;
	double sources = 0, motion = 0, apply = 0, hostDraw = 0;
	double vectorCompile = 0, vectorUpload = 0;
	unsigned long long pathsCompiled = 0, vectorUploads = 0;
	int views = 0;
	unsigned long long layerComposites = 0, backdropComposites = 0, backdropFallbacks = 0;
	unsigned long long draws = 0, vertices = 0, cachedLayers = 0, directMasks = 0;
	unsigned long long layoutUpdates = 0, motionInputs = 0, frameReplays = 0;
	unsigned long long rasterDraws = 0, rasterSavedVertices = 0;
	struct View { double cpu=0,apply=0,render=0; unsigned long long vertices=0; };
	std::map<std::string,View> documents;
};
std::vector<RootProfileSample> rootProfile;
int rootProfileFrames = 0;
// Counts the frames submitted since startup, one per EndFrame, so a profile
// groups its views' draws by the frame that presented them.
std::uint64_t submittedFrames = 0;
std::thread::id editThread=std::this_thread::get_id();
unsigned editWorkDepth=0,editCallDepth=0;
bool editDraining=false,editShutdownPending=false;
retainedUIPreparedEdit_t* editPrepared=nullptr;
std::uint64_t editResources=1;
std::uint64_t NextEditLifetime() noexcept {
    static std::uint64_t next=1;
    return next==(std::numeric_limits<std::uint64_t>::max)()?0:next++;
}
void TouchEdit(retainedUIView_t& view) noexcept {
    if(view.edit->revision)view.edit->revision=view.edit->revision==(std::numeric_limits<std::uint64_t>::max)()?0:view.edit->revision+1;
    if(auto pending=view.edit->pending.lock())pending->ready=false;
}
void TouchEditResources() noexcept {
    if(editResources)editResources=editResources==(std::numeric_limits<std::uint64_t>::max)()?0:editResources+1;
    for(auto* view:views)TouchEdit(*view);
}
bool BusyEditCanvas() noexcept {
    for(const auto* view:views)if(view->runtime->HasActiveCanvasCallback())return true;
    return false;
}
void DrainEditViews() {
    if(editWorkDepth || editCallDepth || editDraining || BusyEditCanvas())return;
    editDraining=true;
    for(;;) {
        auto** prepared=&editPrepared;
        while(*prepared && !(*prepared)->destroyPending)prepared=&(*prepared)->next;
        if(*prepared){auto* value=*prepared;*prepared=value->next;delete value;continue;}
        const auto found=std::find_if(views.begin(),views.end(),[](const auto* view){return view->edit->destroyPending;});
        if(found==views.end())break;
        auto* view=*found;views.erase(found);view->edit->alive=false;delete view;
    }
    editDraining=false;
    if(editShutdownPending){editShutdownPending=false;RetainedUI_Shutdown();}
}
// Service calls retain registered views until all nested Runtime callbacks return.
// This is independent of EditWork: ordinary calls may still refresh resources.
struct EditCall {
    EditCall(){++editCallDepth;}
    ~EditCall(){--editCallDepth;DrainEditViews();}
};
struct EditWork {
    EditWork(){++editWorkDepth;}
    ~EditWork(){--editWorkDepth;DrainEditViews();}
};
// These existing engine getters read cached scalar generations. They perform
// no Host/Rml callbacks or resource work. Final mutation begins after this.
bool EditResourcesCurrent() noexcept {
    return renderSystem && !resourcesRefreshing && !editDraining && editResources &&
        restartGeneration==renderSystem->GetVideoRestartCount() &&
        languageGeneration==LangDict_GetCodePageGeneration() && loadedLanguageRevision==languageRevision;
}


void RecordProfile(double engineMilliseconds) {
	if (!profileFrames) return;
	profile.push_back({runtime->Statistics(),engineMilliseconds});
	if (static_cast<int>(profile.size()) < profileFrames) return;
	std::vector<double> times;
	double compileMilliseconds = 0;
	unsigned long long paths = 0, uploads = 0, hits = 0, peakBytes = 0;
	unsigned long long layerPushes = 0, layerComposites = 0, peakLayerDepth = 0;
	unsigned long long maskSnapshots = 0, maskApplications = 0, peakLayerTargets = 0;
	unsigned long long backdropComposites = 0, backdropFallbacks = 0;
	for (const auto& sample : profile) {
		times.push_back(sample.engineMilliseconds);
		compileMilliseconds += sample.statistics.vectorCompileMilliseconds;
		paths += sample.statistics.vectorPathsCompiled; uploads += sample.statistics.vectorUploads; hits += sample.statistics.vectorCacheHits;
		peakBytes = Max(peakBytes,static_cast<unsigned long long>(sample.statistics.residentGeometryBytes+sample.statistics.visibleVectorCacheBytes));
		layerPushes += sample.statistics.layerPushes; layerComposites += sample.statistics.layerComposites;
		peakLayerDepth = Max(peakLayerDepth,static_cast<unsigned long long>(sample.statistics.peakLayerDepth));
		maskSnapshots += sample.statistics.maskSnapshots; maskApplications += sample.statistics.maskApplications;
		peakLayerTargets = Max(peakLayerTargets,static_cast<unsigned long long>(sample.statistics.peakLayerTargets));
		backdropComposites += sample.statistics.backdropComposites; backdropFallbacks += sample.statistics.backdropFallbacks;
	}
	std::sort(times.begin(),times.end());
	auto percentile = [&](double fraction) { return times[static_cast<size_t>(std::ceil(fraction*times.size()))-1]; };
	common->Printf("Retained UI profile: {\"frames\":%d,\"engine_cpu_p50_ms\":%.6f,\"engine_cpu_p95_ms\":%.6f,\"engine_cpu_max_ms\":%.6f,\"vector_compile_ms\":%.6f,\"paths_compiled\":%llu,\"vector_uploads\":%llu,\"cache_hits\":%llu,\"tracked_peak_bytes\":%llu,\"layer_pushes\":%llu,\"layer_composites\":%llu,\"peak_layer_depth\":%llu,\"mask_snapshots\":%llu,\"mask_applications\":%llu,\"peak_layer_targets\":%llu,\"backdrop_composites\":%llu,\"backdrop_fallbacks\":%llu}\n",
		profileFrames,percentile(.5),percentile(.95),percentile(1),compileMilliseconds,paths,uploads,hits,peakBytes,layerPushes,layerComposites,peakLayerDepth,maskSnapshots,maskApplications,peakLayerTargets,backdropComposites,backdropFallbacks);
	profileFrames = 0; profile.clear();
}
void ReportRootProfile() {
	// Each interval runs from one frame's first root draw to the next frame's.
	std::vector<double> cpu, intervals;
	unsigned long long layerComposites = 0, backdropComposites = 0, backdropFallbacks = 0;
	int views = 0;
	double total = 0, update = 0, render = 0;
	double presentation = 0, layout = 0;
	double sources = 0, motion = 0, apply = 0, hostDraw = 0;
	double vectorCompile = 0, vectorUpload = 0;
	unsigned long long pathsCompiled = 0, vectorUploads = 0;
	unsigned long long draws = 0, vertices = 0, cachedLayers = 0, directMasks = 0;
	unsigned long long layoutUpdates = 0, motionInputs = 0, frameReplays = 0;
	unsigned long long rasterDraws = 0, rasterSavedVertices = 0;
	std::map<std::string,RootProfileSample::View> documents;
	for (int i = 0; i < rootProfileFrames; ++i) {
		const auto& sample = rootProfile[i];
		for (const auto& [path,view]:sample.documents) {
			auto& sum=documents[path]; sum.cpu+=view.cpu; sum.apply+=view.apply; sum.render+=view.render; sum.vertices+=view.vertices;
		}
		cpu.push_back(sample.cpu);
		intervals.push_back(std::chrono::duration<double,std::milli>(rootProfile[i+1].at-sample.at).count());
		total += intervals.back();
		update+=sample.update; render+=sample.render; draws+=sample.draws; vertices+=sample.vertices;
		presentation+=sample.presentation; layout+=sample.layout; layoutUpdates+=sample.layoutUpdates; motionInputs+=sample.motionInputs;
		sources+=sample.sources; motion+=sample.motion; apply+=sample.apply; hostDraw+=sample.hostDraw;
		vectorCompile+=sample.vectorCompile; vectorUpload+=sample.vectorUpload;
		pathsCompiled+=sample.pathsCompiled; vectorUploads+=sample.vectorUploads;
		cachedLayers+=sample.cachedLayers; directMasks+=sample.directMasks;
		frameReplays+=sample.frameReplays;
		rasterDraws+=sample.rasterDraws; rasterSavedVertices+=sample.rasterSavedVertices;
		layerComposites += sample.layerComposites; backdropComposites += sample.backdropComposites; backdropFallbacks += sample.backdropFallbacks;
		views = Max(views,sample.views);
	}
	std::sort(cpu.begin(),cpu.end()); std::sort(intervals.begin(),intervals.end());
	auto percentile = [](const std::vector<double>& values, double fraction) { return values[static_cast<size_t>(std::ceil(fraction*values.size()))-1]; };
	common->Printf("Retained UI root profile: {\"frames\":%d,\"views\":%d,\"retained_cpu_p50_ms\":%.6f,\"retained_cpu_p95_ms\":%.6f,\"retained_cpu_p99_ms\":%.6f,\"retained_cpu_max_ms\":%.6f,\"frame_interval_mean_ms\":%.6f,\"frame_interval_p50_ms\":%.6f,\"frame_interval_p95_ms\":%.6f,\"frame_interval_p99_ms\":%.6f,\"frame_interval_max_ms\":%.6f,\"layer_composites\":%llu,\"backdrop_composites\":%llu,\"backdrop_fallbacks\":%llu}\n",
		rootProfileFrames,views,percentile(cpu,.5),percentile(cpu,.95),percentile(cpu,.99),cpu.back(),total/rootProfileFrames,percentile(intervals,.5),percentile(intervals,.95),percentile(intervals,.99),intervals.back(),
		layerComposites,backdropComposites,backdropFallbacks);
	common->Printf("Retained UI root work: {\"update_mean_ms\":%.6f,\"render_mean_ms\":%.6f,\"draws_mean\":%.2f,\"vertices_mean\":%.2f,\"cached_layers_mean\":%.2f,\"direct_masks_mean\":%.2f}\n",
		update/rootProfileFrames,render/rootProfileFrames,double(draws)/rootProfileFrames,double(vertices)/rootProfileFrames,
		double(cachedLayers)/rootProfileFrames,double(directMasks)/rootProfileFrames);
	common->Printf("Retained UI layout work: {\"presentation_mean_ms\":%.6f,\"layout_mean_ms\":%.6f,\"layout_updates_mean\":%.2f,\"motion_inputs_mean\":%.2f}\n",
		presentation/rootProfileFrames,layout/rootProfileFrames,double(layoutUpdates)/rootProfileFrames,double(motionInputs)/rootProfileFrames);
	common->Printf("Retained UI phase work: {\"sources_ms\":%.6f,\"motion_ms\":%.6f,\"apply_ms\":%.6f,\"host_draw_ms\":%.6f}\n",
		sources/rootProfileFrames,motion/rootProfileFrames,apply/rootProfileFrames,hostDraw/rootProfileFrames);
	common->Printf("Retained UI atlas work: {\"draws_mean\":%.2f,\"saved_vertices_mean\":%.2f,\"pages\":%u}\n",
		double(rasterDraws)/rootProfileFrames,double(rasterSavedVertices)/rootProfileFrames,unsigned(host.RasterPages()));
	common->Printf("Retained UI frame replay: {\"views_mean\":%.2f}\n",double(frameReplays)/rootProfileFrames);
	common->Printf("Retained UI vector work: {\"compile_ms\":%.6f,\"upload_ms\":%.6f,\"paths_mean\":%.2f,\"uploads_mean\":%.2f}\n",
		vectorCompile/rootProfileFrames,vectorUpload/rootProfileFrames,double(pathsCompiled)/rootProfileFrames,double(vectorUploads)/rootProfileFrames);
	for (const auto& [path,view]:documents)
		common->Printf("Retained UI document work: path=%s cpu_ms=%.6f apply_ms=%.6f render_ms=%.6f vertices=%.2f\n",
			path.c_str(),view.cpu/rootProfileFrames,view.apply/rootProfileFrames,view.render/rootProfileFrames,double(view.vertices)/rootProfileFrames);
	rootProfileFrames = 0; rootProfile.clear();
}
void RecordRootProfile(std::uint64_t frame, std::chrono::steady_clock::time_point started, double cpu, const openq4::ui::RuntimeStatistics& statistics, const std::string& path) {
	if (!rootProfileFrames) return;
	if (rootProfile.empty() || rootProfile.back().frame != frame) {
		// The frame after the last measured one only closes its interval.
		if (static_cast<int>(rootProfile.size()) == rootProfileFrames+1) { ReportRootProfile(); return; }
		RootProfileSample sample; sample.frame = frame; sample.at = started;
		rootProfile.push_back(sample);
	}
	auto& sample = rootProfile.back();
	auto& document=sample.documents[path];
	document.cpu+=cpu; document.apply+=statistics.applyMilliseconds; document.render+=statistics.renderMilliseconds;
	document.vertices+=statistics.submittedVertices;
	sample.cpu += cpu; ++sample.views;
	sample.update+=statistics.updateMilliseconds; sample.render+=statistics.renderMilliseconds;
	sample.presentation+=statistics.presentationMilliseconds; sample.layout+=statistics.layoutMilliseconds;
	sample.sources+=statistics.sourceMilliseconds; sample.motion+=statistics.motionMilliseconds;
	sample.apply+=statistics.applyMilliseconds; sample.hostDraw+=statistics.hostDrawMilliseconds;
	sample.vectorCompile+=statistics.vectorCompileMilliseconds; sample.vectorUpload+=statistics.vectorUploadMilliseconds;
	sample.pathsCompiled+=statistics.vectorPathsCompiled; sample.vectorUploads+=statistics.vectorUploads;
	sample.layoutUpdates+=statistics.layoutUpdates; sample.motionInputs+=statistics.motionInputsScanned;
	sample.draws+=statistics.drawCalls; sample.vertices+=statistics.submittedVertices;
	sample.cachedLayers+=statistics.layerCacheHits; sample.directMasks+=statistics.directMaskDraws;
	sample.frameReplays+=statistics.frameReplays;
	sample.rasterDraws+=host.rasterDraws; sample.rasterSavedVertices+=host.rasterSavedVertices;
	sample.layerComposites += statistics.layerComposites;
	sample.backdropComposites += statistics.backdropComposites; sample.backdropFallbacks += statistics.backdropFallbacks;
}

double PresentationTime() { return std::chrono::duration<double>(std::chrono::steady_clock::now()-epoch).count(); }
// Soft focus runs an authored GLSL material program: Vulkan compiles it at
// run time, GL needs GLSL support, and GLES keeps the scrim fallback.
bool SoftFocusSupported() {
	const rendererModuleApi_t api = R_RendererModule_GetStatus().activeApi;
	if (api == RENDER_MODULE_API_VULKAN) return true;
	return (api == RENDER_MODULE_API_GL || api == RENDER_MODULE_API_GL_MODULE) && renderSystem->GetGLConfig().GLSLProgramAvailable;
}
bool UpdateSoftFocus() {
	const bool active = !ui_retainedOpaqueBacking.GetBool() && SoftFocusSupported();
	if (ui_retainedSoftFocus.GetBool() != active) ui_retainedSoftFocus.SetBool(active);
	return active;
}
bool WindowFocused() {
#if defined(USE_SDL3)
	return Sys_SDL_IsGameWindowFocused();
#else
	return true;
#endif
}
// A headset session with input focus drives menus from its controllers, while
// the desktop window the player cannot see may hold no focus at all.
bool InputFocused() { return inputFocused || VR_HasInputFocus(); }
void SetApplicationOpen(bool value) {
	if (RetainedUI_IsOpen() == value) return;
	openq4::NativeInputBeforeInputBlockerChange();
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
	// Confirm and Back may trade the south and east buttons (section 13.5).
	switch (idKeyInput::MenuControllerKey(key)) {
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
    TouchEdit(view);
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
    if(editWorkDepth || editDraining || BusyEditCanvas()){
        TouchEditResources();
        for(auto* view:views)view->runtime->AbortPreparedDocument();
        return false;
    }
    DrainEditViews();
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
    TouchEditResources();
    for(auto* view:views)view->runtime->AbortPreparedDocument();
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
	if (!PreviewReady()) {
		rootProfile.clear(); rootProfile.reserve(frames+1); rootProfileFrames = frames;
		common->Printf("retained UI: profiling the root views for %d frames\n",frames);
		return;
	}
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
    if(std::this_thread::get_id()!=editThread || editWorkDepth || editDraining || BusyEditCanvas())return nullptr;
    DrainEditViews();
	if (resourcesRefreshing) { common->Warning("retained UI: cannot register a view during resource callbacks"); return nullptr; }
	auto view = std::make_unique<retainedUIView_t>();
    view->edit=std::make_shared<retainedUIEditControl>();view->edit->lifetime=NextEditLifetime();
    if(!view->edit->lifetime)return nullptr;
	view->runtime = std::make_unique<openq4::ui::Runtime>(host);
	view->callback = callback; view->owner = owner;
	views.push_back(view.get());
	return view.release();
}
void RetainedUI_DestroyView(retainedUIView_t* view) {
    if(std::this_thread::get_id()!=editThread || !RegisteredView(view))return;
    if(resourcesRefreshing){common->FatalError("Retained UI views cannot be destroyed during resource callbacks");return;}
    TouchEdit(*view);view->runtime->RetireCanvasOwner();view->edit->destroyPending=true;view->failed=true;
    // The caller may destroy its callback owner as soon as this returns.
    view->callback=nullptr;view->owner=nullptr;
    DrainEditViews();
}
openq4::ui::Runtime* RetainedUI_ViewRuntime(retainedUIView_t* view) {
	return RegisteredView(view) && !view->edit->destroyPending ? view->runtime.get() : nullptr;
}
bool RetainedUI_LoadView(retainedUIView_t* view, const std::string& source, const std::string& path,
	std::vector<openq4::ui::Diagnostic>& diagnostics) {
	EditCall call;
	diagnostics.clear();
	if (!RegisteredView(view) || !RefreshResources() || !RegisteredView(view) || view->edit->destroyPending) {
		diagnostics.push_back({"","Retained view is unavailable during resource lifecycle changes"}); return false;
	}
	return LoadViewDocument(*view,source,path,true,diagnostics);
}
bool RetainedUI_LoadParsedView(retainedUIView_t* view, const openq4::ui::Document& document, const std::string& path,
	std::vector<openq4::ui::Diagnostic>& diagnostics) {
	diagnostics.clear();
	if (!RegisteredView(view) || !RefreshResources() || !RegisteredView(view) || view->edit->destroyPending) {
		diagnostics.push_back({"","Retained view is unavailable during resource lifecycle changes"}); return false;
	}
	TouchEdit(*view);
	if (!view->runtime->LoadDocument(document,path,diagnostics)) return false;
	view->source = document.Source(); view->path = path; view->canonical = true; view->failed = false;
	return true;
}
bool RetainedUI_PrepareView(retainedUIView_t* view) {
	return RegisteredView(view) && RefreshResources() && RegisteredView(view) && !view->edit->destroyPending && !view->failed && view->runtime->IsLoaded();
}

bool RetainedUI_QueryEditIdentity(retainedUIView_t* view,retainedUIEditIdentity_t& out) noexcept {
    if(std::this_thread::get_id()!=editThread || editWorkDepth || !RegisteredView(view) ||
        !view->edit->alive || view->edit->destroyPending || !view->edit->lifetime || !view->edit->revision ||
        !view->canonical || view->failed || !EditResourcesCurrent() || view->runtime->HasActiveCanvasCallback() || !view->runtime->IsLoaded())return false;
    const auto canvas=view->runtime->CanvasIdentity();
    if(!canvas.lifetime || !canvas.revision)return false;
    out={view->edit->lifetime,view->edit->revision,editResources,canvas.lifetime,canvas.revision};return true;
}
namespace {
retainedUIPreparedEdit_t* PrepareViewEdit(retainedUIView_t* view,retainedUIEditIdentity_t expected,
    openq4::ui::DocumentEdit& history,openq4::ui::DocumentEditIdentity identity,
    std::span<const openq4::ui::DocumentEditOperation> operations,bool travel,bool redo,
    const openq4::ui::RuntimeDocumentOptions& supplied,std::vector<openq4::ui::Diagnostic>& diagnostics) {
    if(std::this_thread::get_id()!=editThread)return nullptr;
    if(editWorkDepth || editDraining){if(RegisteredView(view))TouchEdit(*view);return nullptr;}
    retainedUIEditIdentity_t current;
    if(!RetainedUI_QueryEditIdentity(view,current) || current!=expected)return nullptr;
    if(!view->edit->pending.expired() || !history.Current() || history.Identity()!=identity ||
        history.Current()->Source()!=view->source || !view->runtime->SourceMatches(view->source,view->path) ||
        view->runtime->NativeVacancy()!=openq4::ui::RuntimeNativeVacancy::Vacant){TouchEdit(*view);return nullptr;}
    EditWork work;
    TouchEdit(*view);const auto revision=view->edit->revision;const auto owner=view->edit;
    try {
        auto options=supplied; // All caller-owned strings/maps copied before a Host call.
        if(options.sourcePath.empty() || options.sourcePath.size()>4096 || options.sourcePath.find('\0')!=std::string::npos ||
            options.sourcePath.find("..")!=std::string::npos || options.sourcePath.find(':')!=std::string::npos ||
            options.sourcePath.front()=='/' || options.sourcePath.front()=='\\')return nullptr;
        auto data=std::make_shared<retainedUIPreparedEditData>();data->owner=owner;data->history=&history;
        data->source=travel?history.PrepareUndo(identity,redo,diagnostics):history.PrepareEdit(identity,operations,diagnostics);
        if(!data->source)return nullptr;
        data->text=data->source->Target().Source();data->path=options.sourcePath;
        data->canvas=view->runtime->PrepareDocument(view->runtime->CanvasIdentity(),*data->source,std::move(options),diagnostics);
        if(!data->canvas || !owner->alive || owner->destroyPending || owner->revision!=revision || !EditResourcesCurrent() ||
            !data->source->OwnerCurrent() || !view->runtime->CanPublishDocument(history,*data->source,*data->canvas))return nullptr;
        const auto canvas=view->runtime->CanvasIdentity();
        data->expected={owner->lifetime,revision,editResources,canvas.lifetime,canvas.revision};
        auto result=std::make_unique<retainedUIPreparedEdit_t>();result->data=data;
        owner->pending=data;result->next=editPrepared;editPrepared=result.get();return result.release();
    }catch(...){return nullptr;}
}
}
retainedUIPreparedEdit_t* RetainedUI_PrepareEdit(retainedUIView_t* view,retainedUIEditIdentity_t expected,
    openq4::ui::DocumentEdit& history,openq4::ui::DocumentEditIdentity identity,
    std::span<const openq4::ui::DocumentEditOperation> operations,const openq4::ui::RuntimeDocumentOptions& options,
    std::vector<openq4::ui::Diagnostic>& diagnostics) {
    return PrepareViewEdit(view,expected,history,identity,operations,false,false,options,diagnostics);
}
retainedUIPreparedEdit_t* RetainedUI_PrepareHistory(retainedUIView_t* view,retainedUIEditIdentity_t expected,
    openq4::ui::DocumentEdit& history,openq4::ui::DocumentEditIdentity identity,bool redo,
    const openq4::ui::RuntimeDocumentOptions& options,std::vector<openq4::ui::Diagnostic>& diagnostics) {
    return PrepareViewEdit(view,expected,history,identity,{},true,redo,options,diagnostics);
}
bool RetainedUI_PublishEdit(retainedUIView_t* view,retainedUIPreparedEdit_t* prepared,openq4::ui::DocumentEditReceipt& out) noexcept {
    if(std::this_thread::get_id()!=editThread)return false;
    if(editWorkDepth || editDraining){if(RegisteredView(view))TouchEdit(*view);return false;}
    if(!prepared || !prepared->data)return false;
    auto& data=*prepared->data;retainedUIEditIdentity_t current;
    if(!data.ready || !data.owner->alive || !data.source->OwnerCurrent() || !RetainedUI_QueryEditIdentity(view,current) ||
        current!=data.expected || view->edit!=data.owner || view->edit->pending.lock()!=prepared->data ||
        view->edit->revision==(std::numeric_limits<std::uint64_t>::max)() ||
        !view->runtime->CanPublishDocument(*data.history,*data.source,*data.canvas))return false;
    const bool changed=data.source->Receipt().sourceChanged;
    // Checked local primitives cannot fail or call foreign code after this.
    if(!view->runtime->PublishDocument(*data.history,*data.source,*data.canvas,out))return false;
    if(changed){view->source.swap(data.text);view->path.swap(data.path);view->canonical=true;view->failed=false;}
    ++view->edit->revision;data.ready=false;return true;
}
void RetainedUI_DestroyPreparedEdit(retainedUIPreparedEdit_t* prepared) {
    if(std::this_thread::get_id()!=editThread)return;
    for(auto* item=editPrepared;item;item=item->next)if(item==prepared){item->destroyPending=true;item->data->ready=false;break;}
    DrainEditViews();
}
bool RetainedUI_DefaultViewport(openq4::ui::Viewport& viewport) {
	viewport = {};
	viewport.width = engineWindowState.uiViewportWidth;
	viewport.height = engineWindowState.uiViewportHeight;
	viewport.displayScale = ui_retainedDensity.GetFloat() > 0 ? ui_retainedDensity.GetFloat() : engineWindowState.displayScale;
	viewport.userScale = ui_retainedScale.GetFloat();
	viewport.textScale = ui_retainedTextScale.GetFloat();
	// Keep root-menu decisions and the size reset reachable. The requested
	// CVars remain intact, and larger windows automatically restore that size.
	viewport.FitToMinimum(640, 480);
	viewport.pixelDensityX = engineWindowState.pixelDensityX; viewport.pixelDensityY = engineWindowState.pixelDensityY;
	viewport.originX = static_cast<float>(engineWindowState.uiViewportX); viewport.originY = static_cast<float>(engineWindowState.uiViewportY);
	// The window's safe area within the UI viewport (which may itself lie
	// inside the framebuffer), or the test insets where they reach further.
	float test[4] = {};
	if (ui_retainedSafeArea.GetString()[0] && sscanf(ui_retainedSafeArea.GetString(),"%f %f %f %f",&test[0],&test[1],&test[2],&test[3]) != 4)
		test[0] = test[1] = test[2] = test[3] = 0;
	const auto inset = [](float window, float test, float clear) {
		return (std::max)({0.f,window-(std::max)(clear,0.f),std::isfinite(test) ? test : 0.f});
	};
	viewport.safeLeft = inset(engineWindowState.safeAreaLeft,test[0],viewport.originX);
	viewport.safeTop = inset(engineWindowState.safeAreaTop,test[1],viewport.originY);
	viewport.safeRight = inset(engineWindowState.safeAreaRight,test[2],
		static_cast<float>(engineWindowState.vidWidth-engineWindowState.uiViewportX-viewport.width));
	viewport.safeBottom = inset(engineWindowState.safeAreaBottom,test[3],
		static_cast<float>(engineWindowState.vidHeight-engineWindowState.uiViewportY-viewport.height));
	return viewport.width > 0 && viewport.height > 0;
}
double RetainedUI_PresentationTime() { return PresentationTime(); }
void RetainedUI_FrameSubmitted() { rootSubmissionPending = false; ++submittedFrames; }
void RetainedUI_ClearModelWorlds() { host.ClearModelWorlds(); }
void RetainedUI_PrecacheModelWorlds() { host.PrecacheModelWorlds(); }
void RetainedUI_EndInitialization() {
	RetainedUI_DestroyView(initializationView); initializationView = nullptr;
}
bool RetainedUI_CanUpdateInitialization() {
	return std::this_thread::get_id() == editThread && !editCallDepth && !editWorkDepth &&
		!resourcesRefreshing && !editDraining && initializationView && !initializationView->failed &&
		cvarSystem->GetCVarBool("ui_retained") && !ui_retainedReducedMotion.GetBool() &&
		renderSystem && renderSystem->IsOpenGLRunning();
}
bool RetainedUI_DrawInitialization(const char* message) {
	if (!cvarSystem->GetCVarBool("ui_retained")) { RetainedUI_EndInitialization(); return false; }
	static int failedGeneration = -1;
	const int generation = renderSystem->GetVideoRestartCount();
	if (failedGeneration == generation) return false;
	static bool drawing = false;
	if (drawing) return false;
	struct Guard { bool& flag; Guard(bool& f):flag(f){flag=true;} ~Guard(){flag=false;} } guard(drawing);
	if (!initializationView) {
		const char* path = "guis/loading/initialization.q4ui";
		void* bytes = nullptr; const int size = fileSystem->ReadFile(path,&bytes);
		if (size <= 0 || !bytes) return false;
		initializationView = RetainedUI_CreateView();
		std::vector<openq4::ui::Diagnostic> diagnostics;
		const bool loaded = RetainedUI_LoadView(initializationView,std::string(static_cast<const char*>(bytes),size),path,diagnostics);
		fileSystem->FreeFile(bytes);
		if (!loaded) { ReportViewDiagnostics(path,diagnostics); RetainedUI_EndInitialization(); failedGeneration = generation; return false; }
		openq4::ui::Runtime::EventEffects effects; std::string error;
		if (!initializationView->runtime->RunEvent("onInit",PresentationTime(),effects,error)) {
			RetainedUI_EndInitialization(); return false;
		}
		initializationEntered = false;
	} else if (!initializationEntered) {
		// The first frame pays document and geometry creation behind the
		// entrance's veiled opening pose. Restart the entrance once that frame
		// is on screen, so its cost cannot skip the entrance's first frames.
		initializationEntered = true;
		openq4::ui::Runtime::EventEffects effects; std::string error;
		if (!initializationView->runtime->RunEvent("onEnter",PresentationTime(),effects,error) && ui_retainedTrace.GetBool())
			common->Printf("RETAINED_INITIALIZATION onEnter failed: %s\n",error.c_str());
	}
	if (!RetainedUI_PrepareView(initializationView)) return false;
	std::string error;
	const auto& previous = initializationView->runtime->GetState();
	const auto oldMessage = previous.find("message");
	const bool changed = oldMessage == previous.end() || oldMessage->second != openq4::ui::StateValue(std::string(message));
	if (changed) {
		int step = 0;
		for (int i=0; i<6; ++i) {
			if (!idStr::Icmp(message,common->GetLanguageDict()->GetString(va("#str_%d",104346+i)))) step=i+1;
		}
		if (!initializationView->runtime->SetState({{"message",std::string(message)}, {"step",double(step)}},error,PresentationTime())) return false;
		openq4::ui::Runtime::EventEffects effects;
		if (!initializationView->runtime->RunEvent("stepChanged",PresentationTime(),effects,error)) return false;
		if (ui_retainedTrace.GetBool()) common->Printf("RETAINED_INITIALIZATION step=%d message=%s\n",step,message);
	}
	openq4::ui::Viewport viewport;
	if (!RetainedUI_DefaultViewport(viewport)) return false;
	viewport.canvasHeight = 720; viewport.canvasMinWidth = 960; viewport.fitScale = 1;
	host.rasterNewMeshes = false;
	const bool drawn = RetainedUI_DrawViewRoot(initializationView,viewport);
	host.rasterNewMeshes = true;
	if (ui_retainedTrace.GetBool()) {
		const auto& s = initializationView->runtime->Statistics();
		common->Printf("RETAINED_INITIALIZATION_FRAME paths=%llu vertices=%llu draws=%llu layers=%llu\n",
			(unsigned long long)s.vectorPathsCompiled,
			(unsigned long long)s.submittedVertices,(unsigned long long)s.drawCalls,(unsigned long long)s.layerComposites);
	}
	return drawn;
}
bool RetainedUI_DrawViewRoot(retainedUIView_t* view, const openq4::ui::Viewport& viewport) {
	const auto started = std::chrono::steady_clock::now();
    EditCall call;
	if (!renderSystem || !renderSystem->IsOpenGLRunning() || viewport.width <= 0 || viewport.height <= 0 || !RetainedUI_PrepareView(view)) return false;
	const int oldWidth = host.viewportWidth, oldHeight = host.viewportHeight;
	const float oldX = host.viewportX, oldY = host.viewportY;
	const bool oldViewport = renderSystem->GetUseUIViewportFor2D();
	host.BeginRoot();
	renderSystem->SetUseUIViewportFor2D(true);
	host.viewportWidth = viewport.width; host.viewportHeight = viewport.height;
	host.viewportX = viewport.originX; host.viewportY = viewport.originY;
	host.captureWidth = renderSystem->GetScreenWidth(); host.captureHeight = renderSystem->GetScreenHeight();
	host.softFocus = UpdateSoftFocus();
	rootSubmissionPending = true; rootSubmissionFrame = host.RenderFrame();
	const double now = PresentationTime();
	view->runtime->SetReducedMotion(ui_retainedReducedMotion.GetBool(),now);
	host.timingRequested=rootProfileFrames>0 || profileFrames>0;
	const bool drawn = view->runtime->Frame(viewport,now);
	host.EndRoot();
	renderSystem->SetUseUIViewportFor2D(oldViewport);
	renderSystem->SetColor4(1,1,1,1);
	host.viewportWidth = oldWidth; host.viewportHeight = oldHeight;
	host.viewportX = oldX; host.viewportY = oldY;
	if (rootProfileFrames && view != previewView)
		RecordRootProfile(submittedFrames,started,std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count(),
			view->runtime->Statistics(),view->path);
	return drawn;
}

void RetainedUI_Init() {
	// Android loads native libraries on the Java thread, then initializes the
	// engine on SDLThread. Bind the view owner at engine startup, not dlopen.
	editThread = std::this_thread::get_id();
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
	cmdSystem->AddCommand("ui_retainedGeometry",[](const idCmdArgs&){host.DumpGeometry();},CMD_FL_SYSTEM,"inspect active retained meshes and coverage-cache residency");
	cmdSystem->AddCommand("ui_retainedImageProbe",[](const idCmdArgs& args){
		if (args.Argc()!=2 || (idStr::Cmp(args.Argv(1),"0") && idStr::Cmp(args.Argv(1),"1"))) {
			common->Printf("usage: ui_retainedImageProbe <0|1>\n"); return;
		}
		host.SetGeneratedImageProbe(args.Argv(1)[0]=='1');
	},CMD_FL_SYSTEM,"draw generated RGBA8 alpha and lifetime diagnostics for engine screenshots");
	cmdSystem->AddCommand("ui_retainedFocus",Focus_f,CMD_FL_SYSTEM,"focus a semantic retained control without reading or moving a device");
	cmdSystem->AddCommand("ui_retainedMenu",Menu_f,CMD_FL_SYSTEM,"submit a semantic menu action to the retained preview");
	cmdSystem->AddCommand("ui_retainedEnabled",Enabled_f,CMD_FL_SYSTEM,"set a retained control's instance enabled state");
	cmdSystem->AddCommand("ui_retainedModal",Modal_f,CMD_FL_SYSTEM,"push or pop a retained modal input scope");
	cmdSystem->AddCommand("ui_retainedState",State_f,CMD_FL_SYSTEM,"inspect retained control feedback and focus");
	cmdSystem->AddCommand("ui_retainedEvents",Events_f,CMD_FL_SYSTEM,"read and drain semantic retained action requests");
}
void RetainedUI_Shutdown() {
    if(editWorkDepth || editDraining || BusyEditCanvas()){
        editShutdownPending=true;TouchEditResources();
        for(auto* view:views)view->runtime->AbortPreparedDocument();
        return;
    }
    editShutdownPending=false;
    RetainedUI_EndInitialization();
	host.SetGeneratedImageProbe(false);
    TouchEditResources();
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
	cmdSystem->RemoveCommand("ui_retainedGeometry");
	cmdSystem->RemoveCommand("ui_retainedImageProbe");
	cmdSystem->RemoveCommand("ui_retainedFocus");
	cmdSystem->RemoveCommand("ui_retainedMenu");
	cmdSystem->RemoveCommand("ui_retainedEnabled");
	cmdSystem->RemoveCommand("ui_retainedModal");
	cmdSystem->RemoveCommand("ui_retainedState");
	cmdSystem->RemoveCommand("ui_retainedEvents");
}
void RetainedUI_Draw() {
	if (!renderSystem || !renderSystem->IsOpenGLRunning()) return;
	if (host.DrawGeneratedImageProbe()) { rootSubmissionPending=true; rootSubmissionFrame=host.RenderFrame(); }
	if (!runtime || !runtime->IsLoaded()) return;
	openq4::ui::Viewport viewport;
	if (!RetainedUI_DefaultViewport(viewport)) return;
	const auto profileStart = std::chrono::steady_clock::now();
	if (RetainedUI_DrawViewRoot(previewView,viewport)) RecordProfile(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-profileStart).count());
}
bool RetainedUI_IsOpen() { return applicationOpen.load(std::memory_order_acquire); }
// Prompts follow the active device (section 13.5): 0 shows the keyboard's
// keycaps, 1-5 a controller family's face buttons (Xbox, PlayStation,
// Nintendo, Steam Deck, generic). A controller used last shows the family
// the HUD's prompts use, which in_promptStyle forces when a remapping layer
// hides the real device (Sys_SDL_GamepadGlyphFamily: 0 generic, 1-4 a family).
bool RetainedUI_PromptSwapped() { return idKeyInput::MenuConfirmSwapped(); }
int RetainedUI_PromptGlyphs() {
	if (!idKeyInput::LastInputWasController()) return 0;
	int family = 0;
#if defined(USE_SDL3)
	int faces[4] = {0,0,0,0};
	family = Sys_SDL_GamepadGlyphFamily(faces);
#endif
	return family >= 1 && family <= 4 ? family : 5;
}
int RetainedUI_ViewCount() { return static_cast<int>(views.size()); }
void RetainedUI_PrecacheImage(const std::string& source, bool additive) {
	if (source.empty() || !openq4::ui::ValidImageSource(source)) return;
	declManager->FindMaterial(((additive ? "_retainedAdd/" : "_retained/") + source).c_str());
}
void RetainedUI_ReloadImage(const std::string& source, bool additive) {
	if (source.empty() || !openq4::ui::ValidImageSource(source)) return;
	if (const idMaterial* material = declManager->FindMaterial(((additive ? "_retainedAdd/" : "_retained/") + source).c_str()))
		material->ReloadImages(false);
}
bool RetainedUI_ViewFailed(retainedUIView_t* view) { return !RegisteredView(view) || view->failed; }
void RetainedUI_LanguageChanged() { ++languageRevision;TouchEditResources(); }
unsigned RetainedUI_InputGeneration() { return inputGeneration; }
void RetainedUI_Close() { Close(); }
#if !defined(USE_SDL3)
void RetainedUI_QueueInput(const retainedUIInput_t&, int) {}
#endif
void RetainedUI_FrameInput() {
    EditCall call;
	if (!RetainedUI_IsOpen()) return;
	const bool ready = PreviewInputReady();
	SuspendInput(!InputFocused() || (console && console->Active()) || engineWindowState.uiViewportWidth <= 0 || engineWindowState.uiViewportHeight <= 0);
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
    EditCall call;
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
		SuspendInput(!InputFocused() || (console && console->Active()) || engineWindowState.uiViewportWidth <= 0 || engineWindowState.uiViewportHeight <= 0);
		ReleaseQuarantinedInput(decoded);
		return true;
	}
	// Device removal/disable queues cancellation before artificial key-ups.
	// Do not advance navigation repeat or activate a pending release first.
	if (decoded.kind == retainedUIInput_t::CANCEL) { CancelInput(); return true; }
	RetainedUI_FrameInput();
	if (inputSuspended || inputResourceSuspended) { ReleaseQuarantinedInput(decoded); return true; }
	// A finger may drag a list's content (section 13.6); a mouse does not.
	runtime->SetTouchPointer(idKeyInput::LastPointerWasTouch());
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
bool RetainedUI_LoadParsedView(retainedUIView_t*,const openq4::ui::Document&,const std::string&,std::vector<openq4::ui::Diagnostic>&) { return false; }
bool RetainedUI_PrepareView(retainedUIView_t*) { return false; }
bool RetainedUI_DrawViewRoot(retainedUIView_t*,const openq4::ui::Viewport&) { return false; }
bool RetainedUI_QueryEditIdentity(retainedUIView_t*,retainedUIEditIdentity_t&) noexcept{return false;}
retainedUIPreparedEdit_t* RetainedUI_PrepareEdit(retainedUIView_t*,retainedUIEditIdentity_t,openq4::ui::DocumentEdit&,openq4::ui::DocumentEditIdentity,std::span<const openq4::ui::DocumentEditOperation>,const openq4::ui::RuntimeDocumentOptions&,std::vector<openq4::ui::Diagnostic>&){return nullptr;}
retainedUIPreparedEdit_t* RetainedUI_PrepareHistory(retainedUIView_t*,retainedUIEditIdentity_t,openq4::ui::DocumentEdit&,openq4::ui::DocumentEditIdentity,bool,const openq4::ui::RuntimeDocumentOptions&,std::vector<openq4::ui::Diagnostic>&){return nullptr;}
bool RetainedUI_PublishEdit(retainedUIView_t*,retainedUIPreparedEdit_t*,openq4::ui::DocumentEditReceipt&) noexcept{return false;}
void RetainedUI_DestroyPreparedEdit(retainedUIPreparedEdit_t*){}
bool RetainedUI_DefaultViewport(openq4::ui::Viewport&) { return false; }
double RetainedUI_PresentationTime() { return 0; }
void RetainedUI_FrameSubmitted() {}
void RetainedUI_ClearModelWorlds() {}
void RetainedUI_PrecacheModelWorlds() {}
bool RetainedUI_DrawInitialization(const char*) { return false; }
bool RetainedUI_CanUpdateInitialization() { return false; }
void RetainedUI_EndInitialization() {}
void RetainedUI_Init() {}
void RetainedUI_Shutdown() {}
void RetainedUI_Draw() {}
void RetainedUI_Close() {}
void RetainedUI_LanguageChanged() {}
bool RetainedUI_IsOpen() { return false; }
int RetainedUI_ViewCount() { return 0; }
void RetainedUI_PrecacheImage(const std::string&, bool) {}
void RetainedUI_ReloadImage(const std::string&, bool) {}
bool RetainedUI_ViewFailed(retainedUIView_t*) { return true; }
unsigned RetainedUI_InputGeneration() { return 0; }
void RetainedUI_FrameInput() {}
bool RetainedUI_ProcessEvent(const sysEvent_s*) { return false; }
void RetainedUI_QueueInput(const retainedUIInput_t&, int) {}
#endif
