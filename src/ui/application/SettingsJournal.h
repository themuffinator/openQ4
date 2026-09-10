// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#pragma once

#include "SettingsTransaction.h"
#include "SettingsEffectPlan.h"

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

// Schema 2 is an envelope for the same recovery file, not a second journal.
// Domain maps are bounded typed opaque records. The caller must separately
// validate their portable semantics, capture provenance, selected-direction
// reconstruction and readiness before any startup/write/finish operation.
// A decoded map is never evidence of hardware support or effect completion.
struct SettingsEffectRecoveryJournal {
	SettingsJournalState state = SettingsJournalState::Pending;
	std::string attempt = std::string(0,'\0');
	SettingsEffectPlan plan;
	StateValues baseline, target, patch;
	StateValues displayRestore, displayTarget, placement;
	StateValues imageRestore, imageTarget, resourceRestore, resourceTarget;
	StateValues audioRestore, audioTarget, deferredRestore, deferredTarget;
};
using SettingsJournalValue = std::variant<SettingsRecoveryJournal,SettingsEffectRecoveryJournal>;

// Immutable, noncopyable decoded value. Moving transfers ownership; a moved or
// default object is empty (Schema()==0). Pointer-swap publication cannot allocate
// even when a different schema replaces the old one under MSVC debug iterators.
class SettingsJournalRecord {
public:
	SettingsJournalRecord() noexcept = default;
	SettingsJournalRecord(SettingsJournalRecord&&) noexcept = default;
	SettingsJournalRecord& operator=(SettingsJournalRecord&&) noexcept = default;
	SettingsJournalRecord(const SettingsJournalRecord&) = delete;
	SettingsJournalRecord& operator=(const SettingsJournalRecord&) = delete;
	const SettingsJournalValue* Value() const noexcept { return value.get(); }
	unsigned Schema() const noexcept { return value ? unsigned(value->index()+1) : 0; }
private:
	std::unique_ptr<const SettingsJournalValue> value;
	friend bool DecodeSettingsJournalRecord(const std::string&,const std::map<std::string,std::size_t>&,
		SettingsJournalRecord&,std::string&);
};
constexpr std::size_t SettingsEffectMetadataMaxEntries = 512;
constexpr std::size_t SettingsEffectMetadataMaxBytes = 64 * 1024;
constexpr std::size_t SettingsEffectMetadataMaxKeyBytes = 96;

// Variant dispatch is explicit. Old DecodeSettingsJournal still rejects schema
// 2; no map-presence inference or downgrade to the schema-1 display route occurs.
// These new entry points preserve outputs on returned false, including caught
// allocation refusal. They cannot recover an upstream noexcept process termination.
bool EncodeSettingsEffectJournal(const SettingsEffectRecoveryJournal& journal,
	const std::map<std::string,std::size_t>& catalog, std::string& bytes, std::string& error);
bool EncodeSettingsJournalRecord(const SettingsJournalRecord& journal,
	const std::map<std::string,std::size_t>& catalog, std::string& bytes, std::string& error);
bool DecodeSettingsJournalRecord(const std::string& bytes,
	const std::map<std::string,std::size_t>& catalog, SettingsJournalRecord& journal, std::string& error);

} // namespace openq4::ui
