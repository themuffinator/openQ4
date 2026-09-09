// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#pragma once

#include "../retained/Document.h"
#include <cstddef>
#include <cstdint>

namespace openq4::ui {

enum class SettingsPhase { Closed, Editing, Confirming, RecoveryRequired };
enum class SettingsCode { Ok, Busy, NotOpen, Invalid, Conflict, ApplyFailed, RollbackFailed };
struct SettingsResult {
	SettingsCode code = SettingsCode::Ok;
	std::string diagnostic; // Bounded developer diagnostic; localize user text by code.
};

// The host supplies one stable, complete, typed catalog. Read/Defaults/Validate
// must not write settings. Write accepts a PATCH and must leave all other keys
// alone; false or an exception may mean a partial write. Successful writes must
// be exact: normalization/refusal is detected by readback. Calls are synchronous
// on the host's serialized settings thread; this interface cannot arbitrate
// concurrent writes inside a callback or distinguish an identical external value.
// No callback may retain references to arguments. Device restart/rollback is a
// separate host responsibility; verifying CVars does not verify device state.
class SettingsHost {
public:
	virtual ~SettingsHost() = default;
	virtual bool Read(StateValues& values, std::string& error) = 0;
	virtual bool Defaults(StateValues& values, std::string& error) = 0;
	virtual bool Validate(const StateValues& baseline, const StateValues& candidate, std::string& error) = 0;
	// Hosts with editor-only ranges may allow an original custom value here,
	// while still enforcing writable types, device limits and coupled settings.
	virtual bool ValidateRollback(const StateValues& original, const StateValues& current,
		const StateValues& target, std::string& error) { (void)original; return Validate(current,target,error); }
	virtual bool Write(const StateValues& changes, std::string& error) = 0;
	virtual bool NeedsConfirmation(const StateValues& before, const StateValues& target) const = 0;
};

class SettingsTransaction {
public:
	static constexpr std::size_t MaxSettings = 1024;
	static constexpr std::size_t MaxKeyBytes = 256;
	static constexpr std::size_t MaxSnapshotBytes = 1024 * 1024;
	explicit SettingsTransaction(SettingsHost& host) : host(host) {}
	SettingsTransaction(const SettingsTransaction&) = delete;
	SettingsTransaction& operator=(const SettingsTransaction&) = delete;

	SettingsResult Begin(std::uint64_t owner);
	SettingsResult Edit(std::uint64_t owner, const StateValues& partial);
	SettingsResult Defaults(std::uint64_t owner);
	// Cancel closes an editing session; during confirmation/recovery it first
	// reverts to Editing. Abandon also closes after a successful revert.
	SettingsResult Cancel(std::uint64_t owner);
	SettingsResult Apply(std::uint64_t owner, double now, double timeout = 15.0);
	SettingsResult Confirm(std::uint64_t owner);
	// In Editing this only discards the draft. Otherwise restores written keys
	// that still equal our write (or are already original), never divergent ones.
	// Successful rollback rebases to fresh live state, preserving unrelated host
	// changes. A failed Apply preserves the attempted edits, while refreshing all
	// untouched draft keys from live state so a retry cannot overwrite them.
	SettingsResult Revert(std::uint64_t owner);
	// Finite, nonnegative time must be monotonic within an open session. Recovery
	// is explicit: Tick never retries failed rollback each frame. A non-action
	// tick preserves LastResult so errors remain visible until another operation.
	SettingsResult Tick(double now);
	SettingsResult Abandon(std::uint64_t owner);

	std::uint64_t Owner() const noexcept { return owner; }
	SettingsPhase Phase() const noexcept { return phase; }
	const StateValues& Baseline() const noexcept { return baseline; }
	const StateValues& Draft() const noexcept { return draft; }
	const StateValues& LastApplied() const noexcept { return lastApplied; }
	double Deadline() const noexcept { return deadline; }
	const SettingsResult& LastResult() const noexcept { return lastResult; }

private:
	SettingsResult Result(SettingsCode code, std::string diagnostic = {});
	SettingsResult Access(std::uint64_t requestedOwner);
	bool Read(StateValues& values, std::string& error, bool requireSchema = true);
	bool Validate(const StateValues& candidate, std::string& error);
	SettingsResult Rollback(bool preserveDraft, SettingsCode recoveredCode = SettingsCode::Ok,
		const std::string& reason = {});
	void Close();
	SettingsHost& host;
	std::uint64_t owner = 0;
	SettingsPhase phase = SettingsPhase::Closed;
	StateValues baseline, draft, lastApplied, written;
	double deadline = 0, lastTime = -1;
	bool busy = false;
	SettingsResult lastResult;
};

} // namespace openq4::ui
