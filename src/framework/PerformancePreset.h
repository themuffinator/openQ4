// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#pragma once
#include <array>
#include <string>
#include <string_view>
#include <variant>

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
};
inline constexpr const char* PerformancePresetDefaultName = "balanced";
inline constexpr int PerformancePresetCount = 6;
inline constexpr int PerformancePresetTargetCount = 32;
const std::array<PerformancePreset,PerformancePresetCount>& PerformancePresets() noexcept;
const std::array<const char*,PerformancePresetTargetCount>& PerformancePresetTargets() noexcept;
const PerformancePreset* FindPerformancePreset(std::string_view name) noexcept;

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
 bool explicitLowPower=false, raspberryPi=false, steamDeck=false, arm64=false, legacyRenderer=false;
 int systemRamMB=0, videoRamMB=0;
};
enum class PerformancePresetReason { ExplicitLowPower, RaspberryPi, SteamDeck, LegacyRenderer,
 ArmLowMemory, Arm, LowMemory, ModestMemory, HighMemory, Desktop };
struct PerformancePresetDetection {
 const PerformancePreset* preset=nullptr; // Immutable shared profile, never a mutable host view.
 PerformancePresetReason reason=PerformancePresetReason::Desktop;
 int systemRamMB=0, videoRamMB=0;
};
inline constexpr int PerformancePresetUnknownSystemRamMB=8192;
inline constexpr int PerformancePresetUnknownVideoRamMB=2048;
inline constexpr int PerformancePresetMaxSystemRamMB=4*1024*1024;
inline constexpr int PerformancePresetMaxVideoRamMB=256*1024;
int SanitizePerformancePresetMemoryMB(int raw,int fallback,int maximum) noexcept;
PerformancePresetDetection DetectPerformancePreset(const PerformancePresetSignals&) noexcept;
} // namespace openq4

// Engine-private observation, implemented by Common. No setters, commands,
// renderer restart or settings publication. The pure model needs no engine.
bool Common_CapturePerformancePresetSignals(openq4::PerformancePresetSignals&,std::string& error);
