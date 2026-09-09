// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "SettingsDisplayController.h"
#include <algorithm>
#include <cmath>
#include <exception>

namespace openq4::ui {
namespace {
constexpr double DeviceTimeout = 20.0, ConfirmationTimeout = 15.0;
bool Clock(double now, double previous) { return std::isfinite(now) && now >= 0 && now >= previous; }
struct Guard { bool& flag; Guard(bool& flag) : flag(flag) { flag = true; } ~Guard() { flag = false; } };
template<class Call> bool Invoke(Call&& call, std::string& error) {
	try { return call(); }
	catch (const std::exception& failure) { error=std::string("Display host exception: ")+failure.what(); error.resize((std::min)(error.size(),std::size_t(512))); }
	catch (...) { error="Display host raised an unknown exception"; }
	return false;
}
}
SettingsResult SettingsDisplayController::SetResult(SettingsCode code, std::string diagnostic) {
	result = {code,std::move(diagnostic)}; return result;
}
void SettingsDisplayController::Reset() {
	stage = SettingsDisplayStage::Idle; attempt = {}; device = {}; ownerDraw = {};
	prepared = executed = drawn = closing = commitIntent = confirmPrepared = completed = preserveDraft = false;
	lastTime = -1; waitDeadline = 0; restoreCode = SettingsCode::Ok; restoreReason.clear();
}
void SettingsDisplayController::Recover(SettingsCode code, const std::string& reason) {
	stage = SettingsDisplayStage::Recovery; SetResult(code,reason);
}
void SettingsDisplayController::Restore(SettingsCode code, const std::string& reason, bool retainDraft) {
	if (commitIntent) { Recover(code,reason); return; }
	restoreCode = code; restoreReason = reason; preserveDraft = retainDraft;
	stage = SettingsDisplayStage::QueuedRestore; drawn = false; SetResult(code,reason);
}
SettingsResult SettingsDisplayController::Apply(std::uint64_t owner, double now) {
	if (busy || Active()) return {SettingsCode::Busy,"A display operation is pending"};
	if (!Clock(now,-1) || !std::isfinite(now+DeviceTimeout)) return SetResult(SettingsCode::Invalid,"Invalid display request time");
	SettingsAttempt candidate;
	const auto preparedResult = transaction.PrepareApply(owner,now,candidate);
	if (preparedResult.code != SettingsCode::Ok) { result = preparedResult; return result; }
	if (candidate.patch.empty()) {
		transaction.CancelPreparedApply(owner,candidate.request);
		return SetResult(SettingsCode::Ok);
	}
	attempt = std::move(candidate); lastTime = now; waitDeadline = now+DeviceTimeout;
	stage = SettingsDisplayStage::QueuedApply; return SetResult(SettingsCode::Ok);
}
SettingsResult SettingsDisplayController::Keep(std::uint64_t owner, std::uint64_t request, double now) {
	if (busy || !owner || owner != attempt.owner || !request || request != attempt.request)
		return {SettingsCode::Busy,"The display confirmation identity is stale"};
	if (!Clock(now,lastTime) || !CanConfirm(now)) return {SettingsCode::Busy,"Display confirmation is unavailable or expired"};
	lastTime = now; stage = SettingsDisplayStage::QueuedKeep;
	return SetResult(SettingsCode::Ok);
}
SettingsResult SettingsDisplayController::Revert(std::uint64_t owner, std::uint64_t request) {
	if (busy || !owner || owner != attempt.owner || !request || request != attempt.request)
		return {SettingsCode::Busy,"The display recovery identity is stale"};
	if (commitIntent || stage == SettingsDisplayStage::AwaitRestore || stage == SettingsDisplayStage::QueuedRestore)
		return {SettingsCode::Busy,"The display operation is already completing"};
	if (completed) stage = SettingsDisplayStage::FinalizeRestore;
	else Restore(SettingsCode::Ok,{},false);
	return SetResult(SettingsCode::Ok);
}
SettingsResult SettingsDisplayController::Retry(std::uint64_t owner, std::uint64_t request) {
	if (busy || !CanRetry() || !owner || owner != attempt.owner || !request || request != attempt.request)
		return {SettingsCode::Busy,"No matching display recovery can be retried"};
	if (commitIntent) stage = completed ? SettingsDisplayStage::FinalizeKeep : SettingsDisplayStage::QueuedKeep;
	else if (completed) stage = SettingsDisplayStage::FinalizeRestore;
	else stage = SettingsDisplayStage::QueuedRestore;
	return SetResult(SettingsCode::Ok);
}
void SettingsDisplayController::Close(std::uint64_t owner) {
	if (!Active() || !owner || owner != attempt.owner) return;
	closing = true;
	if (!commitIntent && stage != SettingsDisplayStage::AwaitRestore && stage != SettingsDisplayStage::QueuedRestore &&
		stage != SettingsDisplayStage::FinalizeRestore && stage != SettingsDisplayStage::Recovery)
		Restore(SettingsCode::Ok,{},false);
}
bool SettingsDisplayController::CanConfirm(double now) const noexcept {
	return stage == SettingsDisplayStage::Confirming && !closing && Clock(now,lastTime) && now < transaction.Deadline();
}
bool SettingsDisplayController::ConfirmationVisible() const noexcept {
	return stage == SettingsDisplayStage::AwaitApply || stage == SettingsDisplayStage::Confirming || stage == SettingsDisplayStage::QueuedKeep || stage == SettingsDisplayStage::Recovery;
}
double SettingsDisplayController::Remaining(double now) const noexcept {
	return CanConfirm(now) ? (std::max)(0.0,transaction.Deadline()-now) : 0.0;
}
bool SettingsDisplayController::Fresh(const SettingsDisplayObservation& observed, const SettingsDisplayObservation& baseline) const {
	return observed.submitted > baseline.submitted && observed.presented > baseline.presented;
}
bool SettingsDisplayController::Observe(bool restoring, SettingsDisplayObservation& observed, std::string& error) {
	if (!Invoke([&]{ return host.Observe(restoring,observed,error); },error)) return false;
	if (!observed.ready || !observed.epoch || !observed.generation || observed.epoch != device.epoch ||
		observed.generation != device.generation || observed.failures != device.failures ||
		observed.submitted < device.submitted || observed.presented < device.presented) {
		error = "Display readiness, device identity or presentation failed"; return false;
	}
	return true;
}
bool SettingsDisplayController::OwnerDrawn(std::uint64_t owner, std::uint64_t request, SettingsDisplayObservation* acknowledged) {
	if (busy || stage != SettingsDisplayStage::AwaitApply || closing || owner != attempt.owner || request != attempt.request) return false;
	SettingsDisplayObservation observation; std::string error;
	if (!Observe(false,observation,error)) return false;
	ownerDraw = observation; drawn = true;
	if (acknowledged) *acknowledged=observation;
	return true;
}
void SettingsDisplayController::Frame(double now, bool ownerAlive, bool allowWork) {
	if (busy || !Active()) return;
	Guard guard(busy);
	if (!Clock(now,lastTime)) {
		if (stage != SettingsDisplayStage::Recovery) Restore(SettingsCode::Invalid,"Display clock moved backwards or is invalid",true);
		return;
	}
	lastTime = now;
	if (!ownerAlive) Close(attempt.owner);
	std::string error;
	if (stage == SettingsDisplayStage::AwaitApply || stage == SettingsDisplayStage::AwaitRestore ||
		stage == SettingsDisplayStage::Confirming || stage == SettingsDisplayStage::QueuedKeep) {
		const bool restoring = stage == SettingsDisplayStage::AwaitRestore;
		SettingsDisplayObservation observed;
		if (!Observe(restoring,observed,error)) {
			if (restoring) Recover(SettingsCode::RollbackFailed,error);
			else Restore(SettingsCode::ApplyFailed,error,true);
		} else if (!restoring && (!observed.hidden && (!observed.focused || observed.minimized))) {
			Restore(SettingsCode::ApplyFailed,"The confirmation window lost focus or became minimized",true);
		} else if ((stage == SettingsDisplayStage::AwaitApply || restoring) && now >= waitDeadline) {
			if (restoring) Recover(SettingsCode::RollbackFailed,"Restored display did not present before its deadline");
			else Restore(SettingsCode::ApplyFailed,"Owning confirmation view did not present before its deadline",true);
		} else if (restoring && Fresh(observed,device)) {
			stage = SettingsDisplayStage::FinalizeRestore;
		} else if (stage == SettingsDisplayStage::AwaitApply && drawn && Fresh(observed,ownerDraw)) {
			const auto complete = transaction.CompleteApply(attempt.owner,attempt.request,now,ConfirmationTimeout);
			if (complete.code == SettingsCode::Ok) { stage = SettingsDisplayStage::Confirming; result = complete; }
			else Restore(complete.code,complete.diagnostic,true);
		}
		if ((stage == SettingsDisplayStage::Confirming || stage == SettingsDisplayStage::QueuedKeep) &&
			!commitIntent && now >= transaction.Deadline()) Restore(SettingsCode::Ok,"Display confirmation expired",false);
	}
	if (!allowWork) return;
	if (stage == SettingsDisplayStage::QueuedApply) {
		if (now >= waitDeadline) {
			Restore(SettingsCode::ApplyFailed,"Display request expired before execution",true); return;
		}
		// Even false can have published an uncertain journal. Keep all ownership
		// until the host explicitly cancels the unwritten preparation.
		prepared = true;
		if (!Invoke([&]{ return host.Prepare(attempt,error); },error)) { Recover(SettingsCode::ApplyFailed,error); return; }
		if (closing) { Restore(SettingsCode::Ok,{},false); return; }
		executed = true;
		const auto write = transaction.ExecuteApply(attempt.owner,attempt.request);
		if (write.code != SettingsCode::Ok) { Restore(write.code,write.diagnostic,true); return; }
		if (!Invoke([&]{ return host.Restart(false,device,error); },error) || !device.ready || !device.epoch || !device.generation) {
			Restore(SettingsCode::ApplyFailed,error.empty()?"Display restart did not return a ready device":error,true); return;
		}
		if (closing) { Restore(SettingsCode::Ok,{},false); return; }
		drawn = false; waitDeadline = now+DeviceTimeout; stage = SettingsDisplayStage::AwaitApply;
	} else if (stage == SettingsDisplayStage::QueuedRestore) {
		if (!executed) {
			if (prepared && !Invoke([&]{ return host.CancelPreparation(error); },error)) { Recover(SettingsCode::RollbackFailed,error); return; }
			const auto cancel = transaction.CancelPreparedApply(attempt.owner,attempt.request);
			if (cancel.code != SettingsCode::Ok) { Recover(cancel.code,cancel.diagnostic); return; }
			if (closing) transaction.Abandon(attempt.owner);
			const auto savedResult = SettingsResult{restoreCode,restoreReason}; Reset(); result = savedResult; return;
		}
		SettingsAttempt restore;
		const auto preparation = transaction.PrepareRestore(attempt.owner,attempt.request,restore);
		if (preparation.code != SettingsCode::Ok) { Recover(preparation.code,preparation.diagnostic); return; }
		attempt = std::move(restore);
		const auto write = transaction.ExecuteRestore(attempt.owner,attempt.request);
		if (write.code != SettingsCode::Ok) { Recover(write.code,write.diagnostic); return; }
		if (!Invoke([&]{ return host.Restart(true,device,error); },error) || !device.ready || !device.epoch || !device.generation) {
			Recover(SettingsCode::RollbackFailed,error.empty()?"Display restoration did not return a ready device":error); return;
		}
		waitDeadline = now+DeviceTimeout; stage = SettingsDisplayStage::AwaitRestore;
	} else if (stage == SettingsDisplayStage::QueuedKeep) {
		if (!confirmPrepared) {
			SettingsAttempt confirmation;
			const auto preparation = transaction.PrepareConfirm(attempt.owner,attempt.request,now,confirmation);
			if (preparation.code != SettingsCode::Ok) { Restore(preparation.code,preparation.diagnostic,true); return; }
			attempt = std::move(confirmation); confirmPrepared = true;
		}
		commitIntent = true;
		if (!Invoke([&]{ return host.PersistConfirmation(attempt,error); },error)) { Recover(SettingsCode::ApplyFailed,error); return; }
		stage = SettingsDisplayStage::FinalizeKeep;
	}
	if (stage == SettingsDisplayStage::FinalizeKeep || stage == SettingsDisplayStage::FinalizeRestore) {
		const bool restoring = stage == SettingsDisplayStage::FinalizeRestore;
		if (!completed) {
			const auto completion = restoring ? transaction.CompleteRestore(attempt.owner,attempt.request,preserveDraft,restoreCode,restoreReason)
				: transaction.CompleteConfirm(attempt.owner,attempt.request);
			if (transaction.AsyncPending()) { Recover(completion.code,completion.diagnostic); return; }
			completed = true; result = completion;
		}
		if (!Invoke([&]{ return host.Finish(restoring,error); },error)) { Recover(SettingsCode::RollbackFailed,error); return; }
		if (closing) transaction.Abandon(attempt.owner);
		const auto completion = result; Reset(); result = completion;
	}
}
} // namespace openq4::ui
