// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#pragma once
#include "SettingsTransaction.h"

namespace openq4::ui {

struct SettingsDisplayObservation {
	std::uint64_t epoch = 0, generation = 0, submitted = 0, presented = 0, failures = 0;
	bool ready = false, hidden = false, focused = false, minimized = false;
	// A ready device can still await a subsequent normal audio/resource update.
	// False means pending, not successful completion or an observed failure.
	bool effectsReady = true;
};
enum class SettingsDisplayStage {
	Idle, QueuedApply, AwaitApply, Confirming, QueuedKeep, QueuedRestore,
	AwaitRestore, FinalizeKeep, FinalizeRestore, Recovery, QueuedAutomaticCommit
};

// Serialized engine adapter. Prepare captures actual baseline/policy, obtains
// the geometry and process leases, and durably journals BEFORE ExecuteApply.
// Failure may leave durable evidence; CancelPreparation must resolve only this
// attempt, never remove another process's journal. Observe verifies exact actual
// parameters, independently of archived intent. Restart returns its new device
// identity/counters only after resource reconstruction. None of these methods
// execute GUI commands or inject input.
class SettingsDisplayHost {
public:
	virtual ~SettingsDisplayHost() = default;
	virtual bool Prepare(const SettingsAttempt&, std::string& error) = 0;
	virtual bool CancelPreparation(std::string& error) = 0;
	virtual bool Restart(bool restoring, SettingsDisplayObservation&, std::string& error) = 0;
	virtual bool Observe(bool restoring, SettingsDisplayObservation&, std::string& error) = 0;
	// Checks the frozen target again, durably marks Confirmed, then commits config.
	// Failure is uncertain publication: retry finalization, never roll back Keep.
	virtual bool PersistConfirmation(const SettingsAttempt&, std::string& error) = 0;
	// Called after transaction readback completion. Clears the exact journal and
	// releases leases only after checked completion; failures retain recovery.
	virtual bool Finish(bool restoring, std::string& error) = 0;
};

// This class owns sequencing; the host owns platform effects. Actions only
// queue work. A full engine Frame performs it; nested/loading frames may poll
// and queue recovery but never recurse into a device restart or persistence.
class SettingsDisplayController {
public:
	SettingsDisplayController(SettingsTransaction& transaction, SettingsDisplayHost& host)
		: transaction(transaction), host(host) {}
	SettingsResult Apply(std::uint64_t owner, double now,
		SettingsCompletion completion = SettingsCompletion::UserConfirmation);
	SettingsResult Keep(std::uint64_t owner, std::uint64_t request, double now);
	SettingsResult Revert(std::uint64_t owner, std::uint64_t request);
	SettingsResult Retry(std::uint64_t owner, std::uint64_t request);
	void Close(std::uint64_t owner);
	void Frame(double now, bool ownerAlive, bool allowWork = true);
	// Must follow a successful draw of the active owning confirmation view.
	bool OwnerDrawn(std::uint64_t owner, std::uint64_t request, SettingsDisplayObservation* acknowledged = nullptr);
	bool Active() const noexcept { return stage != SettingsDisplayStage::Idle; }
	bool CanConfirm(double now) const noexcept;
	bool CanRevert() const noexcept { return Active() && !commitIntent; }
	bool CanRetry() const noexcept { return stage == SettingsDisplayStage::Recovery; }
	bool Approved() const noexcept { return commitIntent; }
	bool ConfirmationVisible() const noexcept;
	std::uint64_t Owner() const noexcept { return attempt.owner; }
	std::uint64_t Request() const noexcept { return attempt.request; }
	SettingsDisplayStage Stage() const noexcept { return stage; }
	const SettingsResult& LastResult() const noexcept { return result; }
	double Remaining(double now) const noexcept;
	bool Closing() const noexcept { return closing; }

private:
	SettingsResult SetResult(SettingsCode code, std::string diagnostic = {});
	void Restore(SettingsCode code, const std::string& reason, bool retainDraft);
	void Recover(SettingsCode code, const std::string& reason);
	void Reset();
	bool Observe(bool restoring, SettingsDisplayObservation& observed, std::string& error);
	bool Fresh(const SettingsDisplayObservation&, const SettingsDisplayObservation&) const;
	SettingsTransaction& transaction;
	SettingsDisplayHost& host;
	SettingsDisplayStage stage = SettingsDisplayStage::Idle;
	SettingsResult result;
	SettingsAttempt attempt;
	SettingsDisplayObservation device, ownerDraw;
	double lastTime = -1, waitDeadline = 0;
	bool busy = false, prepared = false, executed = false, drawn = false;
	bool closing = false, commitIntent = false, confirmPrepared = false, completed = false;
	bool preserveDraft = false;
	SettingsCode restoreCode = SettingsCode::Ok;
	std::string restoreReason;
};

} // namespace openq4::ui
