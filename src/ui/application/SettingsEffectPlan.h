// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#pragma once

#include "SettingsValue.h"

namespace openq4::ui {

// Versioned, read-only SYSTEM catalog projection. The effect bits match
// SystemSettingEffect. No CVar, renderer, audio or persistence calls occur.
struct SettingsEffectField {
	std::size_t type = 0;
	unsigned effects = 0;
	bool operator==(const SettingsEffectField&) const = default;
};
const std::map<std::string,SettingsEffectField>& SettingsEffectCatalogV1();

enum class SettingsEffectCompletion { DisplayConfirmed, Automatic };
enum class SettingsRendererStrategy { None, CoalescedDevice };
struct SettingsEffectPlan {
	unsigned version = 1, changeMask = 0, domainMask = 0;
	SettingsEffectCompletion completion = SettingsEffectCompletion::Automatic;
	SettingsRendererStrategy rendererStrategy = SettingsRendererStrategy::None;
	bool operator==(const SettingsEffectPlan&) const = default;
};

// Full typed snapshots are required; at least one key must change. Preset bit
// 32 records expanded draft metadata, never an instruction to re-expand it.
// Immediate-only changes can have both masks zero. This does not validate
// editor ranges, portable domain descriptors, hardware support or readiness.
// All outputs remain unchanged on failure, including allocation refusal.
bool BuildSettingsEffectPlan(const StateValues& baseline, const StateValues& target,
	const std::map<std::string,std::size_t>& catalog, SettingsEffectPlan& output, std::string& error);
bool ValidateSettingsEffectPlan(const SettingsEffectPlan& plan, const StateValues& baseline,
	const StateValues& target, const std::map<std::string,std::size_t>& catalog, std::string& error);

} // namespace openq4::ui
