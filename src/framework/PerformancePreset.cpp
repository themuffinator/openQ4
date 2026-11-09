// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "PerformancePreset.h"
#include <exception>
#include <type_traits>

namespace openq4 {
namespace {
static const std::array<PerformancePreset,PerformancePresetCount> Presets{{
	{ "minimum", 0, "low",
		50, 0, 0, 30,
		1, 512, 1, 1, 1, 1,
		0, 512, 1, 0, 0, 0, 0, 0, 1, 8, 3,
		0, 1, 24, 512, 1, 1, 0, 0, 2048, 4, 1, 4, 0, 1 },
	{ "lowpower", 0, "low",
		75, 0, 0, 30,
		1, 1024, 1, 1, 1, 1,
		0, 512, 1, 0, 0, 0, 0, 0, 1, 8, 3,
		0, 1, 32, 512, 1, 1, 0, 0, 2048, 4, 1, 4, 0, 1 },
	{ "performance", 1, "baseline",
		85, 0, 1, 60,
		2, 0, 0, 0, 1, 0,
		0, 1024, 2, 0, 0, 0, 0, 0, 1, 16, 4,
		0, 1, 40, 512, 5, 5, 0, 0, 2048, 8, 3, 8, 0, 1 },
	{ "balanced", 2, "baseline",
		100, 2, 1, 120,
		4, 0, 0, 0, 1, 0,
		0, 1024, 0, 0, 0, 0, 0, 0, 1, 16, 4,
		0, 1, 48, 512, 5, 5, 0, 0, 2048, 8, 3, 8, 0, 1 },
	// The three top recipes are built from measured feature costs to hold a
	// refresh class on the reference machine (RTX 4060 Laptop, 2560x1440, both
	// backends, every timed scene; docs/dev/performance-report.md section 11):
	// Enhanced 144 Hz average / 90 Hz 1% low, Quality 100/60, Ultra 75/45.
	//
	// Enhanced spends its budget on the modern effects: projected and parallel
	// lights keep 1024 shadow maps, point lights keep the stencil shadows retail
	// used (their cube maps were its largest cost), and SMAA high replaces MSAA.
	// Keep unlimited updates: a low budget can make moving shadows stale.
	{ "enhanced", 2, "modern",
		100, 0, 2, 144,
		16, 0, 0, 0, 1, 0,
		1, 1024, 0, 1, 1, 1, 0, 0, 1, 16, 4,
		0, 1, 48, 512, 5, 5, 0, 0, 4096, 12, 5, 8, 0, 0 },
	// Quality shadow-maps every light class: 2048 projected maps with PCSS and
	// 512 point cubes, with 2x MSAA under SMAA high.
	{ "quality", 3, "modern",
		100, 2, 2, 144,
		16, 0, 0, 0, 1, 0,
		1, 2048, 0, 1, 1, 1, 0, 0, 1, 32, 4,
		0, 1, 48, 512, 9, 9, 2, 0, 4096, 20, 5, 8, 0, 1 },
	// image_usePrecompressedTextures stays at 1 here even though retail's top
	// machine spec used 0. In openQ4 that cvar also gates user-supplied DDS
	// replacement packs, so 0 silently discarded a player's high-resolution BC7
	// art the moment they touched the settings menu - the reverse of what the
	// highest preset should do.
	//
	// Ultra is the best image that still holds 75 Hz: 4x MSAA under SMAA ultra,
	// 4096 projected maps with 13-tap PCSS in an 8192 atlas, and 32 SSAO samples.
	// Point cubes stay at 512 (larger cubes cost more than every other upgrade
	// together), and cascades and motion blur are left to Maximum.
	{ "ultra", 3, "high-end",
		100, 4, 3, 240,
		16, 0, 0, 0, 1, 0,
		1, 4096, 0, 1, 1, 1, 0, 0, 1, 32, 4,
		0, 1, 48, 512, 13, 9, 2, 0, 8192, 32, 5, 8, 0, 1 },
	// Maximum requests every supported quality maximum and carries no frame-rate
	// target (FPS-02): it is the choice for players who want everything on.
	// Hardware clamps MSAA. Render scale stays native. CRT and exposure/strength
	// controls are artistic preferences, not quality levels; experimental
	// rendering modes stay opt-in.
	{ "maximum", 3, "high-end",
		100, 16, 3, 240,
		16, 0, 0, 0, 1, 0,
		1, 4096, 0, 1, 1, 1, 1, 0, 1, 32, 4,
		0, 1, 48, 2048, 13, 13, 2, 1, 16384, 32, 5, 16, 1, 1 }
}};
static const std::array<const char*,PerformancePresetTargetCount> Targets{{
	"com_performancePreset",
	"com_machineSpec",
	"r_rendererBenchmarkPreset",
	"r_screenFraction",
	"r_multiSamples",
	"r_postAA",
	"com_maxfps",
	"image_anisotropy",
	"image_usePrecompressedTextures",
	"image_downSize",
	"image_downSizeLimit",
	"image_downSizeSpecular",
	"image_downSizeBump",
	"image_downSizeSpecularLimit",
	"image_downSizeBumpLimit",
	"image_ignoreHighQuality",
	"image_writeGeneratedImages",
	"s_maxSoundsPerShader",
	"r_useShadowMap",
	"r_shadowMapSize",
	"r_shadowMapMaxUpdatesPerView",
	"r_bloom",
	"r_ssao",
	"r_hdrToneMap",
	"r_motionBlur",
	"r_crt",
	"r_useLightGrid",
	"r_rendererUploadMegs",
	"r_rendererUploadFrameBuffers",
	"s_numberOfSpeakers",
	"s_useEAXReverb",
	"s_maxEmitterChannels",
	"r_shadowMapPointSize", "r_shadowMapFilterTaps", "r_shadowMapPointFilterTaps",
	"r_shadowMapFilterMode", "r_shadowMapPointFilterMode", "r_shadowMapAtlasSize",
	"r_ssaoSamples", "r_bloomMipCount", "r_motionBlurSamples",
	"r_shadowMapPointLights", "r_hdrSceneTarget", "r_shadows",
	"r_skipBump", "r_skipSpecular", "r_skipSky", "image_picmip",
	"r_shadowMapCSM", "r_shadowMapCascadeCount", "r_shadowMapProjectedCSM",
	"r_shadowMapCascadeStabilize", "r_motionBlurObjectVectors",
	"r_shadowMapHashedAlpha", "r_shadowMapTranslucentReceivers"
}};
bool EqualAscii(std::string_view a,std::string_view b) noexcept {
 if(a.size()!=b.size())return false;
 for(size_t i=0;i<a.size();++i){const auto lower=[](unsigned char c){return c>='A'&&c<='Z'?c+('a'-'A'):c;};if(lower(a[i])!=lower(b[i]))return false;}
 return true;
}
bool BoundedName(const char* text) noexcept {
 if(!text || !*text)return false;
 for(size_t i=0;i<=64;++i)if(text[i]=='\0')return true;
 return false;
}
}
const std::array<PerformancePreset,PerformancePresetCount>& PerformancePresets() noexcept{return Presets;}
const std::array<const char*,PerformancePresetTargetCount>& PerformancePresetTargets() noexcept{return Targets;}
const PerformancePreset* FindPerformancePreset(std::string_view name) noexcept {
 for(const auto& preset:Presets)if(EqualAscii(name,preset.name))return &preset;
 return nullptr;
}
const PerformancePreset* PerformancePresetForMachineSpec(int machineSpec) noexcept {
 static constexpr int tiers[]={1,2,3,6}; // Low, Medium, High, Ultra.
 return machineSpec>=0&&machineSpec<4?&Presets[tiers[machineSpec]]:nullptr;
}
bool ExpandPerformancePreset(const PerformancePreset& preset,PerformancePresetAssignments& output,std::string& error){
 if(!BoundedName(preset.name)||!BoundedName(preset.rendererBenchmarkPreset)){error="Invalid performance profile name";return false;}
 try{
  PerformancePresetAssignments candidate{{
  {"com_machineSpec",preset.machineSpec},
  {"r_rendererBenchmarkPreset",std::string(preset.rendererBenchmarkPreset)},
  {"r_screenFraction",preset.screenFraction},
  {"r_multiSamples",preset.multiSamples},
  {"r_postAA",preset.postAA},
  {"com_maxfps",preset.maxFps},
  {"image_anisotropy",preset.anisotropy},
  {"image_usePrecompressedTextures",preset.usePrecompressedTextures},
  {"image_downSize",preset.downSize},
  {"image_downSizeLimit",preset.downSizeLimit},
  {"image_downSizeSpecular",preset.downSize},
  {"image_downSizeBump",preset.downSize},
  {"image_downSizeSpecularLimit",64},
  {"image_downSizeBumpLimit",preset.downSize != 0 ? 256 : 0},
  {"image_ignoreHighQuality",preset.ignoreHighQuality},
  {"image_writeGeneratedImages",1},
  {"s_maxSoundsPerShader",preset.maxSoundsPerShader},
  {"r_useShadowMap",preset.useShadowMap},
  {"r_shadowMapSize",preset.shadowMapSize},
  {"r_shadowMapMaxUpdatesPerView",preset.shadowMapMaxUpdates},
  {"r_bloom",preset.bloom},
  {"r_ssao",preset.ssao},
  {"r_hdrToneMap",preset.hdrToneMap},
  {"r_motionBlur",preset.motionBlur},
  {"r_crt",preset.crt},
  {"r_useLightGrid",preset.useLightGrid},
  {"r_rendererUploadMegs",preset.uploadMegs},
  {"r_rendererUploadFrameBuffers",preset.uploadFrameBuffers},
  {"s_numberOfSpeakers",preset.numberOfSpeakers},
  {"s_useEAXReverb",preset.useEAXReverb},
  {"s_maxEmitterChannels",preset.maxEmitterChannels},
  {"r_shadowMapPointSize",preset.shadowMapPointSize},
  {"r_shadowMapFilterTaps",preset.shadowMapFilterTaps},
  {"r_shadowMapPointFilterTaps",preset.shadowMapPointFilterTaps},
  {"r_shadowMapFilterMode",preset.shadowMapFilterMode},
  {"r_shadowMapPointFilterMode",preset.shadowMapPointFilterMode},
  {"r_shadowMapAtlasSize",preset.shadowMapAtlasSize},
  {"r_ssaoSamples",preset.ssaoSamples},
  {"r_bloomMipCount",preset.bloomMipCount},
  {"r_motionBlurSamples",preset.motionBlurSamples},
  {"r_shadowMapPointLights",preset.shadowMapPointLights},
  {"r_hdrSceneTarget",preset.hdrToneMap},
  {"r_shadows",1},
  {"r_skipBump",0},
  {"r_skipSpecular",0},
  {"r_skipSky",0},
  {"image_picmip",0},
  {"r_shadowMapCSM",preset.shadowMapCSM},
  {"r_shadowMapCascadeCount",4},
  {"r_shadowMapProjectedCSM",1},
  {"r_shadowMapCascadeStabilize",1},
  {"r_motionBlurObjectVectors",1},
  {"r_shadowMapHashedAlpha",1},
  {"r_shadowMapTranslucentReceivers",1},
  {"com_performancePreset",std::string(preset.name)}
  }};
  static_assert(std::is_nothrow_move_assignable_v<PerformancePresetAssignments>);
  output=std::move(candidate);error.clear();return true;
 }catch(const std::exception&){error="Cannot allocate performance profile expansion";return false;}
}
int SanitizePerformancePresetMemoryMB(int raw,int fallback,int maximum) noexcept{return raw<=0||raw>maximum?fallback:raw;}
PerformancePresetDetection DetectPerformancePreset(const PerformancePresetSignals& signals) noexcept{
 PerformancePresetDetection result;
 result.systemRamMB=SanitizePerformancePresetMemoryMB(signals.systemRamMB,PerformancePresetUnknownSystemRamMB,PerformancePresetMaxSystemRamMB);
 result.videoRamMB=SanitizePerformancePresetMemoryMB(signals.videoRamMB,PerformancePresetUnknownVideoRamMB,PerformancePresetMaxVideoRamMB);
 using Reason=PerformancePresetReason;
 const int ram=result.systemRamMB, vram=result.videoRamMB;
 int tier=2; // Missing telemetry is Performance, never invented desktop capacity.
 result.reason=Reason::Unknown;
 if(signals.gpuClass==RendererGpuClass::Software){tier=0;result.reason=Reason::Software;}
 else if(signals.legacyRenderer){tier=0;result.reason=Reason::LegacyRenderer;}
 else if((ram>0&&ram<=2048)||(signals.gpuClass==RendererGpuClass::Discrete&&vram>0&&vram<=512)){
  tier=0;result.reason=Reason::LowMemory;
 }
 else if((ram>0&&ram<=4096)||(signals.gpuClass==RendererGpuClass::Discrete&&vram>0&&vram<=1024)){
  tier=1;result.reason=Reason::LowMemory;
 }
 else if(signals.gpuClass==RendererGpuClass::Virtual){tier=2;result.reason=Reason::Virtual;}
 else if(ram==0||ram<8192){tier=2;result.reason=Reason::ModestMemory;}
 else if(signals.gpuClass==RendererGpuClass::Integrated){
  // UMA has no dedicated VRAM. Apple Silicon and x86 APUs follow the same rule;
  // CPU instruction set and a tiny firmware reservation are not GPU capacity.
  // Balanced uses classic lighting. Optional GLSL 1.30 post effects are not a
  // prerequisite for native-resolution textures and hardware MSAA (Apple GL).
  tier=3;result.reason=Reason::Shared;
 }
 else if(signals.gpuClass==RendererGpuClass::Discrete&&vram>0){
  tier=vram<=2048?2:3;result.reason=Reason::Dedicated;
  // Nominal 16/8 GiB devices can report less after firmware/driver reservations.
  // Keep raw telemetry intact; these are conservative capacity bands, not an
  // invented allocation budget or an upward-rounded memory observation.
  if(signals.modernEffects&&ram>=15360&&vram>=7680)tier=4;
 }
 // Platform hints are ceilings, never permission to bypass a smaller hardware
 // budget. In particular a Deck/low-power hint cannot promote a 512 MiB GPU.
 if(signals.explicitLowPower&&tier>1){tier=1;result.reason=Reason::ExplicitLowPower;}
 if(signals.raspberryPi&&tier>1){tier=1;result.reason=Reason::RaspberryPi;}
 if(signals.steamDeck&&tier>2){tier=2;result.reason=Reason::SteamDeck;}
 if(signals.mobile&&tier>2){tier=2;result.reason=Reason::Mobile;}
 // No unmeasured device is promoted to Quality/Ultra just for having memory.
 // Reduce the recommendation for the actual drawable, independently of the
 // currently selected render scale. Never change window size or display mode.
 const bool validSize=signals.drawableWidth>0&&signals.drawableWidth<=32768&&
  signals.drawableHeight>0&&signals.drawableHeight<=32768;
 const long long pixels=validSize?1LL*signals.drawableWidth*signals.drawableHeight:0;
 int ceiling=4;
 if(!validSize)ceiling=2;
 else if(signals.gpuClass==RendererGpuClass::Integrated&&pixels>1920LL*1080)ceiling=2;
 else if(pixels>3840LL*2160)ceiling=2;
 else if(pixels>2560LL*1440)ceiling=3;
 if(tier>ceiling){tier=ceiling;result.reason=Reason::Resolution;}
 if((signals.logicalCpus<4||signals.logicalCpus>4096)&&tier>2){tier=2;result.reason=Reason::Cpu;}
 result.preset=&Presets[tier];
 result.screenFraction=result.preset->screenFraction;
 // The tier's cap is a default for a manual choice; a detected start presents
 // at the display's own rate instead, so a 240 Hz panel is not held at 120.
 result.maxFps=result.preset->maxFps;
 if(signals.displayRefreshHz>=PerformancePresetMinDisplayRefreshHz&&signals.displayRefreshHz<=PerformancePresetMaxDisplayRefreshHz)
  result.maxFps=signals.displayRefreshHz;
 result.postAA=(signals.postAAAvailable||signals.modernEffects)?result.preset->postAA:0;
 // A tier change alone leaves Performance rendering 6 million pixels on a
 // 4K iGPU. Bound scene work as well, using existing selectable scale steps.
 // Capacity is only a conservative starting point, never a throughput claim.
 int budget=1920*1080;
 if(tier==0)budget=960*540;
 else if(tier==1||signals.mobile)budget=1280*720;
 else if(signals.gpuClass==RendererGpuClass::Discrete&&ram>=8192&&vram>2048&&!signals.steamDeck)
  budget=(ram>=15360&&vram>=7680)?3840*2160:2560*1440;
 result.scenePixelBudget=budget;
 if(validSize){
  static constexpr int scales[]={100,85,75,50,25,10};
  for(const int scale:scales){
   if(scale>result.preset->screenFraction)continue;
   // Products fit in int64 even at the largest accepted drawable (32768^2).
   if(pixels*scale*scale<=1LL*budget*10000||scale==10){result.screenFraction=scale;break;}
  }
  if(result.screenFraction<result.preset->screenFraction)result.reason=Reason::Resolution;
 }
 return result;
}
bool ResolveDetectedPerformancePreset(const PerformancePresetDetection& detection,PerformancePreset& output) noexcept{
 if(!detection.preset||detection.screenFraction<10||detection.screenFraction>detection.preset->screenFraction||
    detection.postAA<0||detection.postAA>detection.preset->postAA||
    detection.maxFps<0||detection.maxFps>PerformancePresetMaxDisplayRefreshHz)return false;
 auto resolved=*detection.preset;
 resolved.screenFraction=detection.screenFraction;resolved.postAA=detection.postAA;resolved.maxFps=detection.maxFps;
 output=resolved;return true;
}
} // namespace openq4
