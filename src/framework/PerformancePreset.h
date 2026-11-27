// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <variant>
#include "../renderer/RendererHardware.h"

namespace openq4 {
// One shadow setting for the presets and the settings menu. A level is a whole
// recipe: every shadow cvar a preset writes takes its value from this table,
// and the menu's Shadows choice offers the same six levels, so a preset is
// nothing but the levels a player could pick by hand.
//   off       no shadows (the three tiers for minimum hardware and below)
//   original  Quake 4's stencil shadows (Performance, Balanced)
//   low       shadow maps for spotlights and sunlight; point lights, most of
//             Quake 4's lights, keep the stencil shadows (Enhanced)
//   medium    every light shadow-mapped: 2048 maps with PCSS, 512 point cubes,
//             sunlight cascaded (Quality)
//   high      4096 maps with 13-tap PCSS in an 8192 atlas (Ultra)
//   ultra     1024 point cubes, spotlights cascaded too, 16384 atlas (Maximum)
// The values under off and original only wait for shadow maps to be turned on.
struct ShadowQuality {
	const char *name;
	int shadows;             // r_shadows
	int useShadowMap;        // r_useShadowMap
	int pointLights;         // r_shadowMapPointLights
	int size;                // r_shadowMapSize
	int pointSize;           // r_shadowMapPointSize
	int filterTaps;          // r_shadowMapFilterTaps
	int pointFilterTaps;     // r_shadowMapPointFilterTaps
	int filterMode;          // r_shadowMapFilterMode
	int pointFilterMode;     // r_shadowMapPointFilterMode
	int atlasSize;           // r_shadowMapAtlasSize
	int csm;                 // r_shadowMapCSM
	int projectedCSM;        // r_shadowMapProjectedCSM
};
inline constexpr int ShadowQualityCount = 6;
enum ShadowQualityLevel : int { ShadowQualityOff, ShadowQualityOriginal, ShadowQualityLow, ShadowQualityMedium,
	ShadowQualityHigh, ShadowQualityUltra };
const std::array<ShadowQuality,ShadowQualityCount>& ShadowQualities() noexcept;
// The level whose recipe these values are: off and original by their switches
// alone, a shadow-map level by every value. -1 when they match none (a custom
// mix), which the menu shows as Custom.
int ShadowQualityFor(const ShadowQuality& values) noexcept;

struct PerformancePreset {
	const char *name;
	int machineSpec;
	const char *rendererBenchmarkPreset;
	// No render scale: presets never set r_screenFraction or the display mode,
	// so the scene stays at the player's choice (native by default).
	int multiSamples;
	int postAA;
	int maxFps;
	int anisotropy;
	int downSizeLimit;
	int downSize;
	int ignoreHighQuality;
	int usePrecompressedTextures;
	int maxSoundsPerShader;
	// A ShadowQualityLevel: the preset's whole shadow recipe.
	int shadowQuality;
	// Shadow-map passes a view may render, 0 unlimited. It waits for shadow
	// maps on the tiers without them; every tier that draws them leaves it
	// unlimited, because a budget makes moving shadows stale, and so does the
	// menu when it turns a shadow-map level on.
	int shadowMapMaxUpdates;
	int bloom;
	int ssao;
	int hdrToneMap;
	int motionBlur;
	int crt;
	int useLightGrid;
	// 1 lets the game's depth of field draw (the weapon wheel's focus and
	// map scripts' cinematic focus): Enhanced and up.
	int depthOfField;
	int ssaoSamples;
	int bloomMipCount;
	int motionBlurSamples;
	int uploadMegs;
	int uploadFrameBuffers;
	int numberOfSpeakers;
	int useEAXReverb;
	int maxEmitterChannels;
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
 // A desktop Linux host (not Android, which reports mobile) and an Apple GPU
 // by its active-renderer name. With arm64 they tell a small ARM board, whose
 // GPU openQ4 cannot rank, from Apple Silicon (macOS or Asahi Linux) and from
 // Windows on ARM; see PerformancePresetIsArmBoard.
 bool linuxHost=false, appleGpu=false;
 int systemRamMB=0, videoRamMB=0;
 RendererGpuClass gpuClass=RendererGpuClass::Unknown;
 bool modernEffects=false, postAAAvailable=false;
 int logicalCpus=0, drawableWidth=0, drawableHeight=0;
 // Whole-hertz refresh of the display the window is on, 0 when unknown.
 int displayRefreshHz=0;
};
enum class PerformancePresetReason { ExplicitLowPower, RaspberryPi, SteamDeck, Mobile, LegacyRenderer,
 Software, Virtual, LowMemory, ModestMemory, Dedicated, Shared, Unknown, Resolution, Cpu, ArmBoard };
// An ARM board: an arm64 desktop Linux host whose GPU is not discrete and not
// Apple's. Rockchip/Mali, Allwinner, Amlogic, MediaTek, Qualcomm and NVIDIA
// Jetson boards all land here without a name list, so a board openQ4 has never
// seen still gets the ceiling. The Raspberry Pi is one, with its own stricter hint.
constexpr bool PerformancePresetIsArmBoard(const PerformancePresetSignals& signals) noexcept{
 return signals.arm64&&signals.linuxHost&&!signals.mobile&&!signals.appleGpu&&
  signals.gpuClass!=RendererGpuClass::Discrete;
}
// Verified dedicated memory of the active GPU, as capacity classes. Each floor
// is 15/16 of the nominal size, because drivers report a little less than the
// card's label (an 8 GiB card reports 7956-8188 MiB).
inline constexpr int PerformancePresetVram8GiB=7680;
inline constexpr int PerformancePresetVram12GiB=11520;
inline constexpr int PerformancePresetVram16GiB=15360;
inline constexpr int PerformancePresetVram24GiB=23040;
// The pixels of a resolution class named by its 16:9 height. A class spans
// shapes up to 20.25:9, so 16:10 and 3:2 laptop panels and 20:9 phones share
// the class of their 16:9 sibling. Detection uses them to pick the tier.
constexpr long long PerformancePresetClassPixels(int height) noexcept{return 9LL*height*height/4;}
struct PerformancePresetDetection {
 const PerformancePreset* preset=nullptr; // Immutable shared profile, never a mutable host view.
 PerformancePresetReason reason=PerformancePresetReason::Unknown;
 int systemRamMB=0, videoRamMB=0;
 // Automatic recommendations can omit unsupported post AA. They never lower
 // the render scale or the display resolution: a large drawable lowers the
 // tier instead. Manual presets remain immutable and hardware-neutral.
 int postAA=0;
 // A first launch presents at the display's rate rather than the tier's
 // manual-choice cap (FPS-02); the tier's cap when the rate is unknown. A
 // power-limited device (phone, Deck, Pi, ARM board, explicit request) keeps
 // the lower.
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

// The preset system's version (docs/dev/ui/performance-presets.md, "Preset
// system version"). A configuration records the version it last checked in
// com_performancePresetCheck; a different version brings up the system check
// before the main menu, so a player whose settings came from older presets is
// offered the current recommendation.
//
// The fingerprint moves by itself with any change to the presets: it hashes
// every preset's expansion (each target and value, in order) and the
// detector's answer for a fixed matrix of machines, so a new tier, a changed
// value or a moved detection threshold all change it. Bump the revision for a
// change the fingerprint cannot see, such as a new detection signal read from
// the live device, that should still ask players to check again.
inline constexpr int PerformancePresetSystemRevision = 1;
// FNV-1a 32 over the preset expansions and the detection matrix; computed
// once. Pure: no engine state, the same on every host and build.
std::uint32_t PerformancePresetSystemFingerprint() noexcept;
// "<revision>.<fingerprint as 8 lower-case hex digits>", e.g. "1.0a1b2c3d".
std::string PerformancePresetSystemVersion();
// The number of machines the fingerprint's detection matrix asks about.
int PerformancePresetFingerprintMachines() noexcept;
} // namespace openq4

// Engine-private observation, implemented by Common. No setters, commands,
// renderer restart or settings publication. The pure model needs no engine.
bool Common_CapturePerformancePresetSignals(openq4::PerformancePresetSignals&,std::string& error);
