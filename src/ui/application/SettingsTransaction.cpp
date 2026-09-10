// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "SettingsTransaction.h"
#include <atomic>
#include <cmath>
#include <exception>
#include <utility>

namespace openq4::ui {
namespace {
std::atomic<std::uint64_t> lastRequest{0};
std::uint64_t NewRequest() {
	std::uint64_t previous = lastRequest.load(std::memory_order_relaxed);
	while (previous != UINT64_MAX) {
		if (lastRequest.compare_exchange_weak(previous,previous+1,std::memory_order_relaxed)) return previous+1;
	}
	return 0;
}

struct Operation {
	bool& busy;
	explicit Operation(bool& busy) : busy(busy) { busy = true; }
	~Operation() { busy = false; }
};
SettingsResult Reentrant() { return {SettingsCode::Busy,"A settings operation is already in progress"}; }

template<class Callback>
bool Invoke(Callback&& callback, std::string& error) {
	error.clear();
	try {
		if (callback()) return true;
		if (error.empty()) error = "The settings host refused the operation";
	} catch (const std::exception& exception) {
		error = std::string("Settings host exception: ") + exception.what();
	} catch (...) {
		error = "Unknown settings host exception";
	}
	return false;
}

bool ValidSnapshot(const StateValues& values, const StateValues* schema, std::string& error) {
	if (values.size() > SettingsTransaction::MaxSettings || (schema && values.size() != schema->size())) {
		error = "The settings catalog size is invalid"; return false;
	}
	std::size_t bytes = 0;
	for (const auto& [key,value] : values) {
		if (key.empty() || key.size() > SettingsTransaction::MaxKeyBytes ||
			!ValidStateValue(StateValue(key)) || value.valueless_by_exception() || !ValidStateValue(value)) {
			error = "The settings catalog contains an invalid key or value"; return false;
		}
		if (schema) {
			const auto declaration = schema->find(key);
			if (declaration == schema->end() || declaration->second.index() != value.index()) {
				error = "The settings catalog keys or types changed"; return false;
			}
		}
		bytes += key.size() + (std::holds_alternative<std::string>(value) ? std::get<std::string>(value).size() : sizeof(double));
		if (bytes > SettingsTransaction::MaxSnapshotBytes) {
			error = "The settings catalog exceeds its byte budget"; return false;
		}
	}
	return true;
}

StateValues Changes(const StateValues& before, const StateValues& target) {
	StateValues changes;
	for (const auto& [key,value] : target) if (!SettingsValueEqual(before.at(key),value)) changes.emplace(key,value);
	return changes;
}

bool ValidTime(double now, double previous) {
	return std::isfinite(now) && now >= 0 && now >= previous;
}

std::string Because(const std::string& reason, const std::string& detail) {
	return reason.empty() ? detail : reason + "; " + detail;
}
} // namespace

SettingsResult SettingsTransaction::Result(SettingsCode code, std::string diagnostic) {
	if (diagnostic.size() > 2048) diagnostic.resize(2048);
	lastResult = {code,std::move(diagnostic)};
	return lastResult;
}

SettingsResult SettingsTransaction::Access(std::uint64_t requestedOwner) {
	if (!requestedOwner) return Result(SettingsCode::Invalid,"A settings owner must be nonzero");
	if (phase == SettingsPhase::Closed) return Result(SettingsCode::NotOpen,"No settings session is open");
	if (requestedOwner != owner) return Result(SettingsCode::Busy,"Another view owns the settings session");
	return {};
}

SettingsResult SettingsTransaction::AccessAttempt(std::uint64_t requestedOwner, std::uint64_t request) {
	if (auto access = Access(requestedOwner); access.code != SettingsCode::Ok) return access;
	if (!request || request != pending.request)
		return Result(SettingsCode::Invalid,"The settings request is stale or invalid");
	return {};
}

bool SettingsTransaction::Read(StateValues& values, std::string& error, bool requireSchema) {
	StateValues candidate;
	if (!Invoke([&] { return host.Read(candidate,error); },error) ||
		!ValidSnapshot(candidate,requireSchema ? &baseline : nullptr,error)) return false;
	values = std::move(candidate);
	return true;
}

bool SettingsTransaction::Validate(const StateValues& candidate, std::string& error) {
	return ValidSnapshot(candidate,&baseline,error) && Invoke([&] { return host.Validate(baseline,candidate,error); },error);
}

void SettingsTransaction::Close() {
	phase = SettingsPhase::Closed; owner = 0; deadline = 0; lastTime = -1;
	baseline.clear(); draft.clear(); lastApplied.clear(); written.clear();
	ClearAttempt();
}

void SettingsTransaction::ClearAttempt() {
	pending = {}; attemptedEdits.clear(); attemptStage = AttemptStage::None;
}

SettingsResult SettingsTransaction::Begin(std::uint64_t requestedOwner) {
	if (busy) return RejectReentry();
	Operation operation(busy);
	if (!requestedOwner) return Result(SettingsCode::Invalid,"A settings owner must be nonzero");
	if (phase != SettingsPhase::Closed) {
		if (owner == requestedOwner && phase == SettingsPhase::Editing) return Result(SettingsCode::Ok);
		return Result(SettingsCode::Busy,"A settings session is already open");
	}
	StateValues current; std::string error;
	if (!Read(current,error,false)) return Result(SettingsCode::Invalid,std::move(error));
	baseline = current; draft = std::move(current); lastApplied.clear(); written.clear();
	owner = requestedOwner; phase = SettingsPhase::Editing; deadline = 0; lastTime = -1;
	return Result(SettingsCode::Ok);
}

SettingsResult SettingsTransaction::RejectReentry() {
 if(generatedReentry)*generatedReentry=true;
 return Reentrant();
}
bool SettingsTransaction::MergeEdit(const StateValues& partial,StateValues& output,std::string& error) {
 if(!ValidSnapshot(partial,nullptr,error))return false;
 StateValues candidate=draft;
 for(const auto& [key,value]:partial){
  const auto declaration=baseline.find(key);
  if(declaration==baseline.end()||declaration->second.index()!=value.index()){
   error="An edit has an unknown key or incorrect type";return false;
  }
  candidate[key]=value;
 }
 if(!Validate(candidate,error))return false;
 output.swap(candidate);return true;
}
SettingsResult SettingsTransaction::Edit(std::uint64_t requestedOwner,const StateValues& partial) {
 if(busy)return RejectReentry();
 Operation operation(busy);
 if(auto access=Access(requestedOwner);access.code!=SettingsCode::Ok)return access;
 if(phase!=SettingsPhase::Editing)return Result(SettingsCode::Busy,"Settings are awaiting confirmation or recovery");
 StateValues candidate;std::string error;
 if(!MergeEdit(partial,candidate,error))return Result(SettingsCode::Invalid,std::move(error));
 draft.swap(candidate);return Result(SettingsCode::Ok);
}
SettingsResult SettingsTransaction::EditGenerated(std::uint64_t requestedOwner,const EditGenerator& generator) {
 if(busy)return RejectReentry();
 Operation operation(busy);
 try {
  if(auto access=Access(requestedOwner);access.code!=SettingsCode::Ok)return access;
  if(phase!=SettingsPhase::Editing)return Result(SettingsCode::Busy,"Settings are awaiting confirmation or recovery");
  if(!generator)return Result(SettingsCode::Invalid,"A settings edit generator is required");
  bool reentered=false;
  struct Guard{bool*& slot;Guard(bool*& slot,bool& observed):slot(slot){slot=&observed;}~Guard(){slot=nullptr;}} guard(generatedReentry,reentered);
  const SettingsResult success{SettingsCode::Ok,{}};
  StateValues partial,candidate;std::string error;
  const bool accepted=Invoke([&]{return generator(partial,error)&&MergeEdit(partial,candidate,error);},error);
  if(reentered)return Result(SettingsCode::Busy,"Generated settings edit was interrupted by reentry");
  if(!accepted)return Result(SettingsCode::Invalid,std::move(error));
  // Construct the caller's actual return storage before publishing. Even an
  // empty string can allocate a debug-STL proxy, and a named return may move
  // through another allocating proxy. This explicit prvalue is guaranteed to
  // be elided; unwinding its construction leaves the draft unchanged.
  static_assert(noexcept(draft.swap(candidate)));
  struct Publish {
   StateValues& draft;StateValues& candidate;SettingsResult& last;
   int exceptions=std::uncaught_exceptions();
   ~Publish() noexcept {
    if(std::uncaught_exceptions()!=exceptions)return;
    draft.swap(candidate);last.code=SettingsCode::Ok;last.diagnostic.clear();
   }
  } publish{draft,candidate,lastResult};
  return SettingsResult(success);
 } catch (const std::exception&) {
  // Covers construction of empty map sentinels and success return storage,
  // in addition to the generator and complete merged candidate validation.
  return Result(SettingsCode::Invalid,"Cannot allocate generated settings edit");
 }
}

SettingsResult SettingsTransaction::Defaults(std::uint64_t requestedOwner) {
	if (busy) return RejectReentry();
	Operation operation(busy);
	if (auto access = Access(requestedOwner); access.code != SettingsCode::Ok) return access;
	if (phase != SettingsPhase::Editing) return Result(SettingsCode::Busy,"Settings are awaiting confirmation or recovery");
	StateValues candidate; std::string error;
	if (!Invoke([&] { return host.Defaults(candidate,error); },error) || !Validate(candidate,error))
		return Result(SettingsCode::Invalid,std::move(error));
	draft = std::move(candidate);
	return Result(SettingsCode::Ok);
}

SettingsResult SettingsTransaction::Cancel(std::uint64_t requestedOwner) {
	if (busy) return RejectReentry();
	Operation operation(busy);
	if (auto access = Access(requestedOwner); access.code != SettingsCode::Ok) return access;
	if (AsyncPending()) return Result(SettingsCode::Busy,"The display coordinator must finish or restore this request");
	if (phase != SettingsPhase::Editing) return Rollback(false);
	Close(); return Result(SettingsCode::Ok);
}

SettingsResult SettingsTransaction::Apply(std::uint64_t requestedOwner, double now, double timeout) {
	if (busy) return RejectReentry();
	Operation operation(busy);
	if (auto access = Access(requestedOwner); access.code != SettingsCode::Ok) return access;
	if (phase != SettingsPhase::Editing) return Result(SettingsCode::Busy,"Settings are awaiting confirmation or recovery");
	if (!ValidTime(now,lastTime) || !std::isfinite(timeout) || timeout <= 0 ||
		!std::isfinite(now+timeout) || now+timeout <= now)
		return Result(SettingsCode::Invalid,"Settings confirmation time is invalid or moved backwards");
	lastTime = now;
	StateValues current; std::string error;
	if (!Read(current,error)) return Result(SettingsCode::ApplyFailed,std::move(error));
	if (!SettingsValuesEqual(current,baseline)) return Result(SettingsCode::Conflict,"Settings changed outside this session; reopen before applying");
	if (!Validate(draft,error)) return Result(SettingsCode::Invalid,std::move(error));
	StateValues patch = Changes(current,draft);
	if (patch.empty()) {
		baseline = current; draft = current; lastApplied = std::move(current);
		return Result(SettingsCode::Ok);
	}
	bool confirmation = false;
	if (!Invoke([&] { confirmation = host.NeedsConfirmation(current,draft); return true; },error))
		return Result(SettingsCode::ApplyFailed,std::move(error));
	written = std::move(patch); lastApplied = draft;
	// Record ownership before entering the host: even an exception may follow a
	// partial write. Never retry or roll back blindly when readback is unavailable.
	if (!Invoke([&] { return host.Write(written,error); },error))
		return Rollback(true,SettingsCode::ApplyFailed,error);
	if (!Read(current,error)) return Rollback(true,SettingsCode::ApplyFailed,error);
	if (!SettingsValuesEqual(current,lastApplied))
		return Rollback(true,SettingsCode::ApplyFailed,"Settings readback did not match the requested values");
	if (confirmation) {
		phase = SettingsPhase::Confirming; deadline = now + timeout;
	} else {
		baseline = current; draft = std::move(current); written.clear(); deadline = 0;
	}
	return Result(SettingsCode::Ok);
}

SettingsResult SettingsTransaction::PrepareApply(std::uint64_t requestedOwner, double now, SettingsAttempt& attempt) {
	if (busy) return RejectReentry();
	Operation operation(busy);
	if (auto access = Access(requestedOwner); access.code != SettingsCode::Ok) return access;
	if (phase != SettingsPhase::Editing || AsyncPending()) return Result(SettingsCode::Busy,"A settings request is already pending");
	if (!ValidTime(now,lastTime)) return Result(SettingsCode::Invalid,"Settings preparation time is invalid or moved backwards");
	StateValues current; std::string error;
	if (!Read(current,error)) return Result(SettingsCode::ApplyFailed,std::move(error));
	if (!SettingsValuesEqual(current,baseline)) return Result(SettingsCode::Conflict,"Settings changed outside this session; reopen before applying");
	if (!Validate(draft,error)) return Result(SettingsCode::Invalid,std::move(error));
	SettingsAttempt prepared{owner,0,current,draft,Changes(current,draft)};
	prepared.request = NewRequest();
	if (!prepared.request) return Result(SettingsCode::Invalid,"Settings request identities are exhausted");
	attempt = prepared; pending = std::move(prepared);
	attemptedEdits = pending.patch;
	phase = SettingsPhase::Applying; attemptStage = AttemptStage::ApplyPrepared; lastTime = now; deadline = 0;
	return Result(SettingsCode::Ok);
}

SettingsResult SettingsTransaction::ExecuteApply(std::uint64_t requestedOwner, std::uint64_t request) {
	if (busy) return RejectReentry();
	Operation operation(busy);
	if (auto access = AccessAttempt(requestedOwner,request); access.code != SettingsCode::Ok) return access;
	if (phase != SettingsPhase::Applying || attemptStage != AttemptStage::ApplyPrepared)
		return Result(SettingsCode::Busy,"This settings apply cannot execute again");
	attemptStage = AttemptStage::ApplyExecuted;
	StateValues current; std::string error;
	if (!Read(current,error)) return Result(SettingsCode::ApplyFailed,std::move(error));
	if (!SettingsValuesEqual(current,pending.baseline)) return Result(SettingsCode::Conflict,"Settings changed after apply was prepared");
	if (!Validate(pending.target,error)) return Result(SettingsCode::Invalid,std::move(error));
	// Ownership precedes the callback: false/throw can follow a partial write.
	written = pending.patch; lastApplied = pending.target;
	const bool wrote = written.empty() || Invoke([&] { return host.Write(written,error); },error);
	const std::string writeError = error;
	if (!Read(current,error)) return Result(SettingsCode::ApplyFailed,Because(writeError,error));
	if (!wrote) return Result(SettingsCode::ApplyFailed,writeError);
	if (!SettingsValuesEqual(current,pending.target)) return Result(SettingsCode::ApplyFailed,"Settings apply readback did not match the frozen target");
	attemptStage = AttemptStage::ApplyWritten;
	return Result(SettingsCode::Ok);
}

SettingsResult SettingsTransaction::CompleteApply(std::uint64_t requestedOwner, std::uint64_t request, double now, double timeout) {
	if (busy) return RejectReentry();
	Operation operation(busy);
	if (auto access = AccessAttempt(requestedOwner,request); access.code != SettingsCode::Ok) return access;
	if (phase != SettingsPhase::Applying || attemptStage != AttemptStage::ApplyWritten)
		return Result(SettingsCode::Busy,"The settings apply has not executed successfully");
	if (!ValidTime(now,lastTime) || !std::isfinite(timeout) || timeout <= 0 ||
		!std::isfinite(now+timeout) || now+timeout <= now)
		return Result(SettingsCode::Invalid,"Settings confirmation time is invalid or moved backwards");
	StateValues current; std::string error;
	if (!Read(current,error)) return Result(SettingsCode::ApplyFailed,std::move(error));
	if (!SettingsValuesEqual(current,pending.target)) return Result(SettingsCode::Conflict,"Settings changed before device completion");
	lastTime = now; deadline = now+timeout; phase = SettingsPhase::Confirming; attemptStage = AttemptStage::Confirming;
	return Result(SettingsCode::Ok);
}

SettingsResult SettingsTransaction::CancelPreparedApply(std::uint64_t requestedOwner, std::uint64_t request) {
	if (busy) return RejectReentry();
	Operation operation(busy);
	if (auto access = AccessAttempt(requestedOwner,request); access.code != SettingsCode::Ok) return access;
	if (phase != SettingsPhase::Applying || attemptStage != AttemptStage::ApplyPrepared)
		return Result(SettingsCode::Busy,"Only a queued, unexecuted apply can be cancelled");
	ClearAttempt(); phase = SettingsPhase::Editing;
	return Result(SettingsCode::Ok);
}

SettingsResult SettingsTransaction::PrepareRestore(std::uint64_t requestedOwner, std::uint64_t request, SettingsAttempt& attempt) {
	if (busy) return RejectReentry();
	Operation operation(busy);
	if (auto access = AccessAttempt(requestedOwner,request); access.code != SettingsCode::Ok) return access;
	if (attemptStage == AttemptStage::RestorePrepared)
		return Result(SettingsCode::Busy,"A prepared settings restore is already pending");
	StateValues current; std::string error;
	if (!Read(current,error)) return Result(SettingsCode::RollbackFailed,std::move(error));
	StateValues candidate = current, patch;
	for (const auto& [key,target] : written) {
		const auto& original = baseline.at(key);
		if (!SettingsValueEqual(current.at(key),original) && SettingsValueEqual(current.at(key),target)) {
			candidate[key] = original; patch.emplace(key,original);
		}
	}
	if (!ValidSnapshot(candidate,&baseline,error) || (!patch.empty() &&
		!Invoke([&] { return host.ValidateRollback(baseline,current,candidate,error); },error)))
		return Result(SettingsCode::RollbackFailed,std::move(error));
	SettingsAttempt prepared{owner,NewRequest(),std::move(current),std::move(candidate),std::move(patch)};
	if (!prepared.request) return Result(SettingsCode::Invalid,"Settings request identities are exhausted");
	attempt = prepared; pending = std::move(prepared);
	phase = SettingsPhase::Restoring; attemptStage = AttemptStage::RestorePrepared; deadline = 0;
	return Result(SettingsCode::Ok);
}

SettingsResult SettingsTransaction::ExecuteRestore(std::uint64_t requestedOwner, std::uint64_t request) {
	if (busy) return RejectReentry();
	Operation operation(busy);
	if (auto access = AccessAttempt(requestedOwner,request); access.code != SettingsCode::Ok) return access;
	if (phase != SettingsPhase::Restoring || attemptStage != AttemptStage::RestorePrepared)
		return Result(SettingsCode::Busy,"This settings restore cannot execute again");
	attemptStage = AttemptStage::RestoreExecuted;
	StateValues current; std::string error;
	if (!Read(current,error)) return Result(SettingsCode::RollbackFailed,std::move(error));
	if (!SettingsValuesEqual(current,pending.baseline)) return Result(SettingsCode::Conflict,"Settings changed after restore was prepared");
	if (!pending.patch.empty() && !Invoke([&] { return host.ValidateRollback(baseline,current,pending.target,error); },error))
		return Result(SettingsCode::RollbackFailed,std::move(error));
	const bool wrote = pending.patch.empty() || Invoke([&] { return host.Write(pending.patch,error); },error);
	const std::string writeError = error;
	if (!Read(current,error)) return Result(SettingsCode::RollbackFailed,Because(writeError,error));
	bool incomplete = false, conflict = false;
	for (const auto& [key,target] : written) {
		if (SettingsValueEqual(current.at(key),baseline.at(key))) continue;
		incomplete = true;
		if (!SettingsValueEqual(current.at(key),target)) conflict = true;
	}
	if (conflict) return Result(SettingsCode::Conflict,"An externally changed setting cannot be restored safely");
	if (incomplete) return Result(SettingsCode::RollbackFailed,writeError.empty() ? "Settings restore readback did not match" : writeError);
	if (!SettingsValuesEqual(current,pending.target)) return Result(SettingsCode::Conflict,"Settings changed during restore execution");
	// A refused/throwing callback can still have completed its entire patch.
	// Fresh exact readback is authoritative, but proves no device restoration.
	(void)wrote;
	attemptStage = AttemptStage::RestoreWritten;
	return Result(SettingsCode::Ok);
}

SettingsResult SettingsTransaction::CompleteRestore(std::uint64_t requestedOwner, std::uint64_t request,
	bool preserveDraft, SettingsCode recoveredCode, const std::string& reason) {
	if (busy) return RejectReentry();
	Operation operation(busy);
	if (auto access = AccessAttempt(requestedOwner,request); access.code != SettingsCode::Ok) return access;
	if (phase != SettingsPhase::Restoring || attemptStage != AttemptStage::RestoreWritten)
		return Result(SettingsCode::Busy,"The settings restore has not executed successfully");
	StateValues current; std::string error;
	if (!Read(current,error)) return Result(SettingsCode::RollbackFailed,Because(reason,error));
	if (!SettingsValuesEqual(current,pending.target)) return Result(SettingsCode::Conflict,Because(reason,"Settings changed before restoration completed"));
	StateValues refreshedDraft = current;
	if (preserveDraft) for (const auto& [key,target] : attemptedEdits) refreshedDraft[key] = target;
	if (!ValidSnapshot(refreshedDraft,&baseline,error)) return Result(SettingsCode::RollbackFailed,Because(reason,error));
	baseline = current; draft = std::move(refreshedDraft); lastApplied = std::move(current);
	written.clear(); deadline = 0; phase = SettingsPhase::Editing; ClearAttempt();
	return Result(recoveredCode,reason);
}

SettingsResult SettingsTransaction::PrepareConfirm(std::uint64_t requestedOwner, std::uint64_t request,
	double now, SettingsAttempt& attempt) {
	if (busy) return RejectReentry();
	Operation operation(busy);
	if (auto access = AccessAttempt(requestedOwner,request); access.code != SettingsCode::Ok) return access;
	if (phase != SettingsPhase::Confirming || attemptStage != AttemptStage::Confirming)
		return Result(SettingsCode::Busy,"No settings confirmation can be prepared");
	if (!ValidTime(now,lastTime) || now >= deadline)
		return Result(SettingsCode::Invalid,"The settings confirmation deadline has expired or time is invalid");
	StateValues current; std::string error;
	if (!Read(current,error)) return Result(SettingsCode::ApplyFailed,std::move(error));
	if (!SettingsValuesEqual(current,lastApplied)) return Result(SettingsCode::Conflict,"Settings changed before confirmation");
	SettingsAttempt prepared{owner,NewRequest(),baseline,std::move(current),written};
	if (!prepared.request) return Result(SettingsCode::Invalid,"Settings request identities are exhausted");
	attempt = prepared; pending = std::move(prepared);
	attemptStage = AttemptStage::ConfirmPrepared; lastTime = now;
	return Result(SettingsCode::Ok);
}

SettingsResult SettingsTransaction::CompleteConfirm(std::uint64_t requestedOwner, std::uint64_t request) {
	if (busy) return RejectReentry();
	Operation operation(busy);
	if (auto access = AccessAttempt(requestedOwner,request); access.code != SettingsCode::Ok) return access;
	if (phase != SettingsPhase::Confirming || attemptStage != AttemptStage::ConfirmPrepared)
		return Result(SettingsCode::Busy,"Settings confirmation has not been prepared");
	StateValues current; std::string error;
	if (!Read(current,error)) return Result(SettingsCode::ApplyFailed,std::move(error));
	if (!SettingsValuesEqual(current,pending.target)) return Result(SettingsCode::Conflict,"Settings changed during confirmation persistence");
	baseline = current; draft = std::move(current); written.clear(); deadline = 0; phase = SettingsPhase::Editing;
	ClearAttempt(); return Result(SettingsCode::Ok);
}

SettingsResult SettingsTransaction::CancelPreparedConfirm(std::uint64_t requestedOwner, std::uint64_t request) {
	if (busy) return RejectReentry();
	Operation operation(busy);
	if (auto access = AccessAttempt(requestedOwner,request); access.code != SettingsCode::Ok) return access;
	if (phase != SettingsPhase::Confirming || attemptStage != AttemptStage::ConfirmPrepared)
		return Result(SettingsCode::Busy,"Settings confirmation has not been prepared");
	attemptStage = AttemptStage::Confirming;
	return Result(SettingsCode::Ok);
}

SettingsResult SettingsTransaction::Rollback(bool preserveDraft, SettingsCode recoveredCode, const std::string& reason) {
	StateValues current; std::string error;
	if (!Read(current,error)) {
		phase = SettingsPhase::RecoveryRequired; deadline = 0;
		return Result(SettingsCode::RollbackFailed,Because(reason,error));
	}
	StateValues restore, candidate = current;
	bool conflict = false;
	for (const auto& [key,target] : written) {
		const auto& original = baseline.at(key);
		if (SettingsValueEqual(current.at(key),original)) continue;
		if (!SettingsValueEqual(current.at(key),target)) { conflict = true; continue; }
		restore.emplace(key,original); candidate[key] = original;
	}
	if (!restore.empty()) {
		// Coupled constraints must also hold when an unrelated setting changed
		// externally. Validate against the fresh state, then touch only owned keys.
		if (!ValidSnapshot(candidate,&baseline,error) ||
			!Invoke([&] { return host.ValidateRollback(baseline,current,candidate,error); },error)) {
			phase = SettingsPhase::RecoveryRequired; deadline = 0;
			return Result(SettingsCode::RollbackFailed,Because(reason,error));
		}
		std::string writeError;
		const bool writeOk = Invoke([&] { return host.Write(restore,writeError); },writeError);
		if (!Read(current,error)) {
			phase = SettingsPhase::RecoveryRequired; deadline = 0;
			return Result(SettingsCode::RollbackFailed,Because(reason,Because(writeError,error)));
		}
		// A refused/throwing partial rollback may nevertheless have restored all
		// values. Verified state is authoritative; otherwise retain recovery.
		for (const auto& [key,target] : written) {
			if (SettingsValueEqual(current.at(key),baseline.at(key))) continue;
			if (!SettingsValueEqual(current.at(key),target)) conflict = true;
			else if (!writeOk) error = writeError;
		}
	}
	bool restored = true;
	for (const auto& [key,target] : written) if (!SettingsValueEqual(current.at(key),baseline.at(key))) restored = false;
	if (!restored) {
		phase = SettingsPhase::RecoveryRequired; deadline = 0;
		if (conflict) return Result(SettingsCode::Conflict,Because(reason,"An externally changed setting cannot be rolled back safely"));
		return Result(SettingsCode::RollbackFailed,Because(reason,error.empty() ? "Settings rollback readback did not match" : error));
	}
	StateValues refreshedDraft = current;
	if (preserveDraft) for (const auto& [key,target] : written) refreshedDraft[key] = draft.at(key);
	if (!ValidSnapshot(refreshedDraft,&baseline,error)) {
		phase = SettingsPhase::RecoveryRequired; deadline = 0;
		return Result(SettingsCode::RollbackFailed,Because(reason,error));
	}
	baseline = current; draft = std::move(refreshedDraft); lastApplied = std::move(current);
	written.clear(); deadline = 0; phase = SettingsPhase::Editing;
	return Result(recoveredCode,reason);
}

SettingsResult SettingsTransaction::Confirm(std::uint64_t requestedOwner) {
	if (busy) return RejectReentry();
	Operation operation(busy);
	if (auto access = Access(requestedOwner); access.code != SettingsCode::Ok) return access;
	if (AsyncPending()) return Result(SettingsCode::Busy,"The display coordinator must persist this confirmation");
	if (phase != SettingsPhase::Confirming) return Result(SettingsCode::Busy,"No settings confirmation is pending");
	StateValues current; std::string error;
	if (!Read(current,error)) return Rollback(false,SettingsCode::ApplyFailed,error);
	if (!SettingsValuesEqual(current,lastApplied)) return Rollback(false,SettingsCode::Conflict,"Settings changed before confirmation");
	baseline = current; draft = std::move(current); written.clear(); deadline = 0; phase = SettingsPhase::Editing;
	return Result(SettingsCode::Ok);
}

SettingsResult SettingsTransaction::Revert(std::uint64_t requestedOwner) {
	if (busy) return RejectReentry();
	Operation operation(busy);
	if (auto access = Access(requestedOwner); access.code != SettingsCode::Ok) return access;
	if (AsyncPending()) return Result(SettingsCode::Busy,"The display coordinator must restore this request");
	if (phase == SettingsPhase::Editing) { draft = baseline; return Result(SettingsCode::Ok); }
	return Rollback(false);
}

SettingsResult SettingsTransaction::Tick(double now) {
	if (busy) return RejectReentry();
	Operation operation(busy);
	if (!ValidTime(now,phase == SettingsPhase::Closed ? -1 : lastTime))
		return Result(SettingsCode::Invalid,"Settings presentation time is invalid or moved backwards");
	if (phase == SettingsPhase::Closed) return lastResult;
	lastTime = now;
	if (AsyncPending()) return {SettingsCode::Busy,"The display coordinator owns this pending request"};
	if (phase == SettingsPhase::RecoveryRequired) return lastResult;
	if (phase == SettingsPhase::Confirming && now >= deadline) return Rollback(false);
	return lastResult;
}

SettingsResult SettingsTransaction::Abandon(std::uint64_t requestedOwner) {
	if (busy) return RejectReentry();
	Operation operation(busy);
	if (auto access = Access(requestedOwner); access.code != SettingsCode::Ok) return access;
	if (AsyncPending()) return Result(SettingsCode::Busy,"The display coordinator must restore or cancel this request");
	if (phase != SettingsPhase::Editing) {
		const auto result = Rollback(false);
		if (result.code != SettingsCode::Ok) return result;
	}
	Close(); return Result(SettingsCode::Ok);
}

} // namespace openq4::ui
