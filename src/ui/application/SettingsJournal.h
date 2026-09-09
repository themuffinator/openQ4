// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#pragma once

#include "SettingsTransaction.h"

namespace openq4::ui {

enum class SettingsJournalState { Pending, Confirmed };
struct SettingsRecoveryJournal {
	SettingsJournalState state = SettingsJournalState::Pending;
	std::string attempt; // 128 random bits as lowercase hex; never a process owner.
	StateValues baseline, target, patch;
	StateValues displayRestore, displayTarget, placement;
};

// Fixed schema, canonical encoding and checksum detect accidental corruption.
// The checksum is not authentication. Exact save-root selection and the host's
// catalog/display validators are required before acting on a decoded journal.
constexpr std::size_t SettingsJournalMaxBytes = 4 * 1024 * 1024;
bool EncodeSettingsJournal(const SettingsRecoveryJournal& journal,
	const std::map<std::string,std::size_t>& catalog, std::string& bytes, std::string& error);
bool DecodeSettingsJournal(const std::string& bytes,
	const std::map<std::string,std::size_t>& catalog, SettingsRecoveryJournal& journal, std::string& error);

} // namespace openq4::ui
