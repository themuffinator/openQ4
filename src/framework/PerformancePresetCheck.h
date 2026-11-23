// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#pragma once
// The system check: the screen before the main menu that offers the detected
// performance preset whenever the preset system's version differs from the
// one this configuration last checked (docs/dev/ui/performance-presets.md,
// "System check"). Common owns the version record and the preset apply; the
// session presents the screen (Session_presetcheck.cpp).
#include <string>
#include "PerformancePreset.h"

namespace openq4 {
struct PerformancePresetCheck {
	std::string stored;            // com_performancePresetCheck as recorded
	std::string version;           // PerformancePresetSystemVersion()
	bool neverChecked = false;     // nothing recorded: a configuration from before the check
	std::string current;           // com_performancePreset
	std::string recommended;       // the detected preset's name
	int currentTier = -1;          // index into PerformancePresets(), -1 when unknown
	int recommendedTier = -1;
	bool matches = false;          // applying the recommendation would change no setting
	int differences = 0;           // settings the recommendation would change
	std::string adapter;           // the renderer's device name
	RendererGpuClass gpuClass = RendererGpuClass::Unknown;
	PerformancePresetReason reason = PerformancePresetReason::Unknown;
	int systemRamMB = 0, videoRamMB = 0, logicalCpus = 0;
	int drawableWidth = 0, drawableHeight = 0, displayRefreshHz = 0;
	int maxFps = 0, postAA = 0;
	std::string summary;           // the detection's reasoning, for the log
};
} // namespace openq4

// Whether the check should present before the main menu: the recorded version
// differs from the current one and nothing holds the check back (a dedicated
// server, safe mode, a pending settings recovery, tools).
bool Common_PerformancePresetCheckDue();
// Observes the device and describes the recommendation against the current
// settings. Fails while no renderer can be observed.
bool Common_DescribePerformancePresetCheck( openq4::PerformancePresetCheck &check, std::string &error );
// Applies the detected preset and records the version. `changed` says whether
// any setting moved, so the caller restarts the renderer and sound only then.
bool Common_ApplyPerformancePresetCheck( bool &changed, std::string &error );
// Records the current version without changing a setting ("keep").
void Common_RecordPerformancePresetCheck( const char *answer );
// Forgets the record, so the next start asks again.
void Common_ClearPerformancePresetCheck();
