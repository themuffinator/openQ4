// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#pragma once
#include <array>
#include <string>
#include <string_view>
#include <variant>
#include "../renderer/RendererHardware.h"

namespace openq4 {
struct PerformancePreset {
	const char *name;
	int machineSpec;
	const char *rendererBenchmarkPreset;
	int screenFraction;
	int multiSamples;
	int postAA;
	int maxFps;
	int anisotropy;
	int downSizeLimit;
	int downSize;
	int ignoreHighQuality;
	int usePrecompressedTextures;
	int maxSoundsPerShader;
	int useShadowMap;
	int shadowMapSize;
	int shadowMapMaxUpdates;
	int bloom;
	int ssao;
	int hdrToneMap;
	int motionBlur;
	int crt;
	int useLightGrid;
	int uploadMegs;
	int uploadFrameBuffers;
	int numberOfSpeakers;
	int useEAXReverb;
	int maxEmitterChannels;
	int shadowMapPointSize;
	int shadowMapFilterTaps;
	int shadowMapPointFilterTaps;
	int shadowMapFilterMode;
	int shadowMapPointFilterMode;
	int shadowMapAtlasSize;
	int ssaoSamples;
	int bloomMipCount;
	int motionBlurSamples;
	int shadowMapCSM;
	// 0 leaves point lights on the stencil shadows retail used; spot and
	// parallel lights keep their shadow maps (FPS-02 Enhanced).
	int shadowMapPointLights;
	// 0 draws no shadows at all: Minimum and Low Power, for hardware below
	// the documented minimum. Every tier from Low up keeps them.
	int shadows;
};
inline constexpr const char* PerformancePresetDefaultName = "balanced";
inline constexpr int PerformancePresetCount = 9;
inline constexpr int PerformancePresetTargetCount = 55;
const std::array<PerformancePreset,PerformancePresetCount>& PerformancePresets() noexcept;
const std::array<const char*,PerformancePresetTargetCount>& PerformancePresetTargets() noexcept;
const PerformancePreset* FindPerformancePreset(std::string_view name) noexcept;
// Compatibility for the four stock quality choices; all use the same recipes:
// Low, Performance, Balanced and Ultra.
const PerformancePreset* PerformancePresetForMachineSpec(int machineSpec) noexcept;

struct PerformancePresetAssignment {
 const char* key = nullptr; // Static canonical key; values below own their strings.
 std::variant<int,std::string> value = 0;
};
using PerformancePresetAssignments = std::array<PerformancePresetAssignment,PerformancePresetTargetCount>;
// Preserves legacy setter order, ending with the public selection marker.
// Maps the supplied record without applying hardware clamps or touching CVars.
// Names must be bounded; the consumer validates numeric types/ranges/readbacks.
// Output is published only after the complete expansion succeeds.
bool ExpandPerformancePreset(const PerformancePreset&,PerformancePresetAssignments&,std::string& error);

struct PerformancePresetSignals {
 // legacyRenderer means insufficient classic material/texture capability,
 // not merely an API version that lacks optional modern post effects.
 bool explicitLowPower=false, raspberryPi=false, steamDeck=false, arm64=false, legacyRenderer=false, mobile=false;
 int systemRamMB=0, videoRamMB=0;
 RendererGpuClass gpuClass=RendererGpuClass::Unknown;
 bool modernEffects=false, postAAAvailable=false;
 int logicalCpus=0, drawableWidth=0, drawableHeight=0;
 // Whole-hertz refresh of the display the window is on, 0 when unknown.
 int displayRefreshHz=0;
};
enum class PerformancePresetReason { ExplicitLowPower, RaspberryPi, SteamDeck, Mobile, LegacyRenderer,
 Software, Virtual, LowMemory, ModestMemory, Dedicated, Shared, Unknown, Resolution, Cpu };
// Verified dedicated memory of the active GPU, as capacity classes. Each floor
// is 15/16 of the nominal size, because drivers report a little less than the
// card's label (an 8 GiB card reports 7956-8188 MiB).
inline constexpr int PerformancePresetVram8GiB=7680;
inline constexpr int PerformancePresetVram12GiB=11520;
inline constexpr int PerformancePresetVram16GiB=15360;
inline constexpr int PerformancePresetVram24GiB=23040;
// Phones report less than their label too: a 12 GiB flagship is above 10 GiB.
inline constexpr int PerformancePresetMobileFlagshipRamMB=10240;
// The scene pixels of a resolution class named by its 16:9 height. A class
// spans shapes up to 20.25:9, so 16:10 and 3:2 laptop panels and 20:9 phones
// share the class of their 16:9 sibling, and the next class up fits at 75%.
constexpr long long PerformancePresetClassPixels(int height) noexcept{return 9LL*height*height/4;}
struct PerformancePresetDetection {
 const PerformancePreset* preset=nullptr; // Immutable shared profile, never a mutable host view.
 PerformancePresetReason reason=PerformancePresetReason::Unknown;
 int systemRamMB=0, videoRamMB=0;
 // Automatic recommendations can reduce the template's scene scale and omit
 // unsupported post AA. Manual presets remain immutable and hardware-neutral.
 int screenFraction=100, postAA=0, scenePixelBudget=0;
 // A first launch presents at the display's rate rather than the tier's
 // manual-choice cap (FPS-02); the tier's cap when the rate is unknown. A
 // power-limited device (phone, Deck, Pi, explicit request) keeps the lower.
 int maxFps=0;
};
inline constexpr int PerformancePresetUnknownSystemRamMB=0;
inline constexpr int PerformancePresetUnknownVideoRamMB=0;
inline constexpr int PerformancePresetMaxSystemRamMB=4*1024*1024;
inline constexpr int PerformancePresetMaxVideoRamMB=256*1024;
// Display rates a detected frame cap follows; the com_maxfps range bounds the top.
inline constexpr int PerformancePresetMinDisplayRefreshHz=30;
inline constexpr int PerformancePresetMaxDisplayRefreshHz=1000;
int SanitizePerformancePresetMemoryMB(int raw,int fallback,int maximum) noexcept;
PerformancePresetDetection DetectPerformancePreset(const PerformancePresetSignals&) noexcept;
bool ResolveDetectedPerformancePreset(const PerformancePresetDetection&,PerformancePreset&) noexcept;
} // namespace openq4

// Engine-private observation, implemented by Common. No setters, commands,
// renderer restart or settings publication. The pure model needs no engine.
bool Common_CapturePerformancePresetSignals(openq4::PerformancePresetSignals&,std::string& error);
