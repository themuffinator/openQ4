// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#pragma once

#include "SettingsTransaction.h"

namespace openq4::ui {

enum SystemSettingEffect : unsigned {
	SystemSettingImmediate = 0,
	SystemSettingDisplayRestart = 1 << 0,
	SystemSettingImageReload = 1 << 1,
	SystemSettingAudioRestart = 1 << 2,
	SystemSettingNextMap = 1 << 3,
	SystemSettingRendererResources = 1 << 4,
	SystemSettingPresetExpansion = 1 << 5,
	SystemSettingEngineRestart = 1 << 6 // Saved preference; consumed only at next engine startup.
};

// How an Apply runs. Immediate settings ride along with any class, as does the
// preset marker, which only names the profile the draft was filled from.
// A display change alone needs the user's confirmation; the renderer fallback
// alone and the next-map light-grid preload alone complete automatically once
// their effect is proved. Every other batch is Combined: an image, audio,
// renderer-resource or load-policy change (what a Performance Preset writes),
// or any two of the classes above together. One executor journals it, writes
// it, restarts the renderer device and the sound device once each as the
// batch needs, and asks for confirmation only when the display changed.
enum class SystemApplyClass { None, Immediate, Display, Renderer, Deferred, Combined };

struct SystemSettingDescriptor {
	std::string key;
	size_t type = 0; // StateValue index: number, Boolean, string.
	double minimum = 0, maximum = 0;
	bool integer = false;
	std::vector<double> numberChoices;
	std::vector<std::string> stringChoices;
	std::string label; // Existing #str key, or empty for a preset-only target.
	unsigned effects = SystemSettingImmediate;
};

// Engine-private catalog adapter. Successful Write proves exact CVar readback,
// never renderer/audio readiness. The application service must reject/defer
// RequiresDeviceWork until its typed device recovery route is available.
class SystemSettingsHost final : public SettingsHost {
public:
	static const std::vector<SystemSettingDescriptor>& Catalog();
	static const std::map<std::string, size_t>& Schema();
	static unsigned ChangedEffects(const StateValues& before, const StateValues& target);
	static bool ResolveModeDimensions(int mode, int customWidth, int customHeight,
		int desktopPixelWidth, int desktopPixelHeight, int& width, int& height);
	// The persistent legacy r_mode for an exact pixel size, or -1.
	static int LegacyModeForSize(int width, int height);
	static bool ChangedRequiresDisplayRestart(const StateValues& before, const StateValues& target);
	static SystemApplyClass ApplyClassOf(const StateValues& before, const StateValues& target);
	static bool RequiresDeviceWork(const StateValues& before, const StateValues& target);
	// The changed settings that a Combined apply writes but this session cannot
	// finish: image_ignoreHighQuality and s_maxSoundsPerShader are read when a
	// material or a sound shader is parsed, so declarations already parsed keep
	// the old value until the game next starts.
	static bool ChangedNeedsNextStart(const StateValues& before, const StateValues& target);

 // Pure typed profile expansion. Caller publishes with EditGenerated so
 // ownership and complete merged validation enclose capability observation.
 bool BuildPreset(const std::string& name,StateValues& patch,std::string& error);
 bool BuildDetectedPreset(StateValues& patch,std::string& error);
	bool Read(StateValues& values, std::string& error) override;
	// One catalog setting's live value, read and parsed as Read reads it.
	bool ReadValue(const std::string& key, StateValue& value, std::string& error);
	bool Defaults(StateValues& values, std::string& error) override;
	bool Validate(const StateValues& baseline, const StateValues& candidate, std::string& error) override;
	bool ValidateDraft(const StateValues& baseline, const StateValues& candidate, std::string& error) override;
	// Validate the recorded original edit using registered/catalog constraints.
	// Its historical display topology is verified separately by recovery records;
	// an unused historical monitor must not be required on the current host.
	bool ValidateSavedTarget(const StateValues& baseline, const StateValues& candidate, std::string& error);
	bool ValidateRollback(const StateValues& original, const StateValues& current,
		const StateValues& target, std::string& error) override;
	bool Write(const StateValues& changes, std::string& error) override;
	bool NeedsConfirmation(const StateValues& before, const StateValues& target) const override;
};

} // namespace openq4::ui
