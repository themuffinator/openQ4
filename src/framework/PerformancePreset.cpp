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
		2, 0, 24 },
	{ "lowpower", 0, "low",
		75, 0, 0, 30,
		1, 1024, 1, 1, 1, 1,
		0, 512, 1, 0, 0, 0, 0, 0, 1, 8, 3,
		2, 0, 32 },
	{ "performance", 1, "baseline",
		85, 0, 1, 60,
		2, 0, 0, 0, 1, 0,
		0, 1024, 2, 0, 0, 0, 0, 0, 1, 16, 4,
		2, 0, 40 },
	{ "balanced", 2, "baseline",
		100, 2, 1, 120,
		4, 0, 0, 0, 1, 0,
		0, 1024, 0, 0, 0, 0, 0, 0, 1, 16, 4,
		6, 1, 48 },
	{ "quality", 3, "modern",
		100, 4, 1, 144,
		8, 0, 0, 0, 1, 0,
		0, 1024, 0, 0, 0, 0, 0, 0, 1, 32, 4,
		6, 1, 48 },
	// image_usePrecompressedTextures stays at 1 here even though retail's top
	// machine spec used 0. In openQ4 that cvar also gates user-supplied DDS
	// replacement packs, so 0 silently discarded a player's high-resolution BC7
	// art the moment they touched the settings menu - the reverse of what the
	// highest preset should do.
	{ "ultra", 3, "high-end",
		100, 8, 1, 240,
		16, 0, 0, 0, 1, 0,
		0, 2048, 0, 0, 0, 0, 0, 0, 1, 32, 4,
		6, 1, 48 }
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
	"s_maxEmitterChannels"
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
 const char* name=PerformancePresetDefaultName;
 if(signals.explicitLowPower){name="lowpower";result.reason=PerformancePresetReason::ExplicitLowPower;}
 else if(signals.raspberryPi){name="lowpower";result.reason=PerformancePresetReason::RaspberryPi;}
 else if(signals.steamDeck){name="performance";result.reason=PerformancePresetReason::SteamDeck;}
 else if(signals.legacyRenderer){name="minimum";result.reason=PerformancePresetReason::LegacyRenderer;}
 else if(signals.arm64){
  if(result.systemRamMB<=4096||result.videoRamMB<=1024){name="lowpower";result.reason=PerformancePresetReason::ArmLowMemory;}
  else{name="performance";result.reason=PerformancePresetReason::Arm;}
 }
 else if(result.systemRamMB<=4096||result.videoRamMB<=1024){name="lowpower";result.reason=PerformancePresetReason::LowMemory;}
 else if(result.systemRamMB<=8192||result.videoRamMB<=2048){name="performance";result.reason=PerformancePresetReason::ModestMemory;}
 else if(result.systemRamMB>=16384&&result.videoRamMB>=6144){name="quality";result.reason=PerformancePresetReason::HighMemory;}
 else result.reason=PerformancePresetReason::Desktop;
 result.preset=FindPerformancePreset(name);return result;
}
} // namespace openq4
