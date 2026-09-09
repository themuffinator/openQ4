// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "SettingsTransaction.h"
#include <cmath>
#include <exception>
#include <utility>

namespace openq4::ui {
namespace {
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
	for (const auto& [key,value] : target) if (before.at(key) != value) changes.emplace(key,value);
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
}

SettingsResult SettingsTransaction::Begin(std::uint64_t requestedOwner) {
	if (busy) return Reentrant();
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

SettingsResult SettingsTransaction::Edit(std::uint64_t requestedOwner, const StateValues& partial) {
	if (busy) return Reentrant();
	Operation operation(busy);
	if (auto access = Access(requestedOwner); access.code != SettingsCode::Ok) return access;
	if (phase != SettingsPhase::Editing) return Result(SettingsCode::Busy,"Settings are awaiting confirmation or recovery");
	std::string error;
	if (!ValidSnapshot(partial,nullptr,error)) return Result(SettingsCode::Invalid,std::move(error));
	StateValues candidate = draft;
	for (const auto& [key,value] : partial) {
		const auto declaration = baseline.find(key);
		if (declaration == baseline.end() || declaration->second.index() != value.index())
			return Result(SettingsCode::Invalid,"An edit has an unknown key or incorrect type");
		candidate[key] = value;
	}
	if (!Validate(candidate,error)) return Result(SettingsCode::Invalid,std::move(error));
	draft = std::move(candidate);
	return Result(SettingsCode::Ok);
}

SettingsResult SettingsTransaction::Defaults(std::uint64_t requestedOwner) {
	if (busy) return Reentrant();
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
	if (busy) return Reentrant();
	Operation operation(busy);
	if (auto access = Access(requestedOwner); access.code != SettingsCode::Ok) return access;
	if (phase != SettingsPhase::Editing) return Rollback(false);
	Close(); return Result(SettingsCode::Ok);
}

SettingsResult SettingsTransaction::Apply(std::uint64_t requestedOwner, double now, double timeout) {
	if (busy) return Reentrant();
	Operation operation(busy);
	if (auto access = Access(requestedOwner); access.code != SettingsCode::Ok) return access;
	if (phase != SettingsPhase::Editing) return Result(SettingsCode::Busy,"Settings are awaiting confirmation or recovery");
	if (!ValidTime(now,lastTime) || !std::isfinite(timeout) || timeout <= 0 ||
		!std::isfinite(now+timeout) || now+timeout <= now)
		return Result(SettingsCode::Invalid,"Settings confirmation time is invalid or moved backwards");
	lastTime = now;
	StateValues current; std::string error;
	if (!Read(current,error)) return Result(SettingsCode::ApplyFailed,std::move(error));
	if (current != baseline) return Result(SettingsCode::Conflict,"Settings changed outside this session; reopen before applying");
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
	if (current != lastApplied)
		return Rollback(true,SettingsCode::ApplyFailed,"Settings readback did not match the requested values");
	if (confirmation) {
		phase = SettingsPhase::Confirming; deadline = now + timeout;
	} else {
		baseline = current; draft = std::move(current); written.clear(); deadline = 0;
	}
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
		if (current.at(key) == original) continue;
		if (current.at(key) != target) { conflict = true; continue; }
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
			if (current.at(key) == baseline.at(key)) continue;
			if (current.at(key) != target) conflict = true;
			else if (!writeOk) error = writeError;
		}
	}
	bool restored = true;
	for (const auto& [key,target] : written) if (current.at(key) != baseline.at(key)) restored = false;
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
	if (busy) return Reentrant();
	Operation operation(busy);
	if (auto access = Access(requestedOwner); access.code != SettingsCode::Ok) return access;
	if (phase != SettingsPhase::Confirming) return Result(SettingsCode::Busy,"No settings confirmation is pending");
	StateValues current; std::string error;
	if (!Read(current,error)) return Rollback(false,SettingsCode::ApplyFailed,error);
	if (current != lastApplied) return Rollback(false,SettingsCode::Conflict,"Settings changed before confirmation");
	baseline = current; draft = std::move(current); written.clear(); deadline = 0; phase = SettingsPhase::Editing;
	return Result(SettingsCode::Ok);
}

SettingsResult SettingsTransaction::Revert(std::uint64_t requestedOwner) {
	if (busy) return Reentrant();
	Operation operation(busy);
	if (auto access = Access(requestedOwner); access.code != SettingsCode::Ok) return access;
	if (phase == SettingsPhase::Editing) { draft = baseline; return Result(SettingsCode::Ok); }
	return Rollback(false);
}

SettingsResult SettingsTransaction::Tick(double now) {
	if (busy) return Reentrant();
	Operation operation(busy);
	if (!ValidTime(now,phase == SettingsPhase::Closed ? -1 : lastTime))
		return Result(SettingsCode::Invalid,"Settings presentation time is invalid or moved backwards");
	if (phase == SettingsPhase::Closed) return lastResult;
	lastTime = now;
	if (phase == SettingsPhase::RecoveryRequired) return lastResult;
	if (phase == SettingsPhase::Confirming && now >= deadline) return Rollback(false);
	return lastResult;
}

SettingsResult SettingsTransaction::Abandon(std::uint64_t requestedOwner) {
	if (busy) return Reentrant();
	Operation operation(busy);
	if (auto access = Access(requestedOwner); access.code != SettingsCode::Ok) return access;
	if (phase != SettingsPhase::Editing) {
		const auto result = Rollback(false);
		if (result.code != SettingsCode::Ok) return result;
	}
	Close(); return Result(SettingsCode::Ok);
}

} // namespace openq4::ui
