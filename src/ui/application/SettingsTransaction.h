// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#pragma once

#include "SettingsValue.h"
#include <cstddef>
#include <cstdint>
#include <functional>

namespace openq4::ui {

enum class SettingsPhase { Closed, Editing, Confirming, RecoveryRequired, Applying, Restoring };
enum class SettingsCode { Ok, Busy, NotOpen, Invalid, Conflict, ApplyFailed, RollbackFailed };
enum class SettingsCompletion { UserConfirmation, Automatic };
struct SettingsResult {
	SettingsCode code = SettingsCode::Ok;
	std::string diagnostic; // Bounded developer diagnostic; localize user text by code.
};

// A value copy for a serialized coordinator, never a mutable view of transaction
// state. Restore.baseline is the fresh pre-restore frame; the transaction keeps
// its original baseline until the device restoration is explicitly completed.
struct SettingsAttempt {
	std::uint64_t owner = 0, request = 0;
	StateValues baseline, target, patch;
	SettingsCompletion completion = SettingsCompletion::UserConfirmation;
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
 // A synchronous read-only producer builds a partial edit under this owner's
 // Editing/reentrancy guard. Generation, allocation, validation or attempted
 // reentry failure preserves the live draft; no callback may retain references.
 using EditGenerator=std::function<bool(StateValues& partial,std::string& error)>;
 SettingsResult EditGenerated(std::uint64_t owner,const EditGenerator& generator);
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

	// Asynchronous device path. Preparing freezes values without host writes;
	// execution changes only CVars; completion is the coordinator's assertion
	// that the requested device and owning view have actually presented. Request
	// IDs are process-wide and never reused, including across Close/Begin. Each
	// successful prepare returns a new ID; restore/confirm take the current ID
	// to authorize the transition, then return the replacement in attempt.request.
	SettingsResult PrepareApply(std::uint64_t owner, double now, SettingsAttempt& attempt,
		SettingsCompletion completion = SettingsCompletion::UserConfirmation);
	SettingsResult ExecuteApply(std::uint64_t owner, std::uint64_t request);
	SettingsResult CompleteApply(std::uint64_t owner, std::uint64_t request, double now, double timeout = 15.0);
	// For a frozen Automatic request only, after the coordinator has verified
	// every actual effect. Rechecks that the host needs no user confirmation and
	// freezes a new commit identity without entering Confirming or inventing Keep.
	SettingsResult PrepareAutomaticCommit(std::uint64_t owner, std::uint64_t request, double now, SettingsAttempt& attempt);
	SettingsResult CompleteAutomaticCommit(std::uint64_t owner, std::uint64_t request);
	SettingsResult CancelPreparedApply(std::uint64_t owner, std::uint64_t request);
	// Restore freezes a fresh conflict-safe patch. Execution can restore safe
	// owned keys while reporting Conflict for divergent ones; such a result must
	// never be completed as recovery. Explicitly prepare again after resolving
	// the conflict. No failed operation discards the original ownership data.
	SettingsResult PrepareRestore(std::uint64_t owner, std::uint64_t request, SettingsAttempt& attempt);
	SettingsResult ExecuteRestore(std::uint64_t owner, std::uint64_t request);
	SettingsResult CompleteRestore(std::uint64_t owner, std::uint64_t request, bool preserveDraft = false,
		SettingsCode recoveredCode = SettingsCode::Ok, const std::string& reason = {});
	// Prepare validates confirmation before durable journal/configuration work.
	// Complete rechecks the frozen host frame before releasing recovery ownership.
	SettingsResult PrepareConfirm(std::uint64_t owner, std::uint64_t request, double now, SettingsAttempt& attempt);
	SettingsResult CompleteConfirm(std::uint64_t owner, std::uint64_t request);
	SettingsResult CancelPreparedConfirm(std::uint64_t owner, std::uint64_t request);

	std::uint64_t Owner() const noexcept { return owner; }
	SettingsPhase Phase() const noexcept { return phase; }
	const StateValues& Baseline() const noexcept { return baseline; }
	const StateValues& Draft() const noexcept { return draft; }
	const StateValues& LastApplied() const noexcept { return lastApplied; }
	double Deadline() const noexcept { return deadline; }
	const SettingsResult& LastResult() const noexcept { return lastResult; }
	std::uint64_t Request() const noexcept { return pending.request; }
	bool AsyncPending() const noexcept { return pending.request != 0; }
	bool Dirty() const noexcept { return !SettingsValuesEqual(draft,baseline); }

private:
	SettingsResult Result(SettingsCode code, std::string diagnostic = {});
	SettingsResult Access(std::uint64_t requestedOwner);
 SettingsResult RejectReentry();
 bool MergeEdit(const StateValues& partial,StateValues& candidate,std::string& error);
	SettingsResult AccessAttempt(std::uint64_t requestedOwner, std::uint64_t request);
	bool Read(StateValues& values, std::string& error, bool requireSchema = true);
	bool Validate(const StateValues& candidate, std::string& error);
	SettingsResult Rollback(bool preserveDraft, SettingsCode recoveredCode = SettingsCode::Ok,
		const std::string& reason = {});
	void Close();
	void ClearAttempt();
	SettingsResult CompletePreparedCommit();
	enum class AttemptStage { None, ApplyPrepared, ApplyExecuted, ApplyWritten, Confirming, ConfirmPrepared, AutomaticCommitPrepared,
		RestorePrepared, RestoreExecuted, RestoreWritten };
	SettingsHost& host;
	std::uint64_t owner = 0;
	SettingsPhase phase = SettingsPhase::Closed;
	StateValues baseline, draft, lastApplied, written;
	double deadline = 0, lastTime = -1;
	bool busy = false;
 bool* generatedReentry=nullptr; // Stack-scoped observer only during EditGenerated.
	SettingsAttempt pending;
	StateValues attemptedEdits;
	AttemptStage attemptStage = AttemptStage::None;
	SettingsResult lastResult;
};

} // namespace openq4::ui
