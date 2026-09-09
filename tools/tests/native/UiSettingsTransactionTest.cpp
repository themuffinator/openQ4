// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "src/ui/application/SettingsTransaction.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <limits>
#include <set>
#include <stdexcept>
#include <utility>

using namespace openq4::ui;
namespace {
void Check(bool condition, const char* message) {
	if (!condition) { std::fprintf(stderr,"FAIL: %s\n",message); std::exit(1); }
}
void Expect(const SettingsResult& result, SettingsCode code, const char* message) {
	if (result.code != code) {
		std::fprintf(stderr,"FAIL: %s (code %d, expected %d): %s\n",message,
			static_cast<int>(result.code),static_cast<int>(code),result.diagnostic.c_str());
		std::exit(1);
	}
}
StateValues Initial() {
	return {{"brightness",1.0},{"gamma",1.0},{"mode",0.0},{"volume",0.5},{"shadows",true},{"label",std::string("Player")}};
}

struct FakeHost final : SettingsHost {
	StateValues live = Initial(), defaults = Initial();
	int reads = 0, defaultReads = 0, validations = 0, rollbackValidations = 0;
	mutable int confirmations = 0;
	std::vector<StateValues> writes;
	std::function<void(const char*)> callback;
	std::function<bool(StateValues&,std::string&)> readHook, defaultsHook;
	std::function<bool(const StateValues&,const StateValues&,std::string&)> validateHook;
	std::function<bool(const StateValues&,const StateValues&,const StateValues&,std::string&)> rollbackHook;
	std::function<bool(const StateValues&,std::string&)> writeHook;
	std::function<bool(const StateValues&,const StateValues&)> confirmationHook;
	void Touch(const char* operation) const { if (callback) callback(operation); }
	void Patch(const StateValues& values) { for (const auto& [key,value] : values) live.at(key) = value; }
	bool Read(StateValues& values, std::string& error) override {
		++reads; Touch("Read");
		if (readHook) return readHook(values,error);
		values = live; return true;
	}
	bool Defaults(StateValues& values, std::string& error) override {
		++defaultReads; Touch("Defaults");
		if (defaultsHook) return defaultsHook(values,error);
		values = defaults; return true;
	}
	bool Validate(const StateValues& before, const StateValues& target, std::string& error) override {
		++validations; Touch("Validate");
		if (validateHook) return validateHook(before,target,error);
		const double brightness = std::get<double>(target.at("brightness"));
		const double gamma = std::get<double>(target.at("gamma"));
		const double mode = std::get<double>(target.at("mode"));
		const double volume = std::get<double>(target.at("volume"));
		if (brightness < 0.5 || brightness > 2 || gamma < 0.5 || gamma > 3 ||
			mode < 0 || mode > 2 || mode != std::floor(mode) || volume < 0 || volume > 1) {
			error = "Outside the editor range"; return false;
		}
		return true;
	}
	bool ValidateRollback(const StateValues& original, const StateValues& current,
		const StateValues& target, std::string& error) override {
		++rollbackValidations; Touch("ValidateRollback");
		if (rollbackHook) return rollbackHook(original,current,target,error);
		return SettingsHost::ValidateRollback(original,current,target,error);
	}
	bool Write(const StateValues& values, std::string& error) override {
		writes.push_back(values); Touch("Write");
		if (writeHook) return writeHook(values,error);
		Patch(values); return true;
	}
	bool NeedsConfirmation(const StateValues& before, const StateValues& target) const override {
		++confirmations; Touch("NeedsConfirmation");
		if (confirmationHook) return confirmationHook(before,target);
		return before.at("mode") != target.at("mode");
	}
};

void Begin(FakeHost& host, SettingsTransaction& transaction, std::uint64_t owner = 7) {
	Expect(transaction.Begin(owner),SettingsCode::Ok,"begin settings session");
	Check(transaction.Phase() == SettingsPhase::Editing && transaction.Owner() == owner &&
		transaction.Baseline() == host.live && transaction.Draft() == host.live,"begin captures a complete independent draft");
}
void Pending(FakeHost& host, SettingsTransaction& transaction) {
	Begin(host,transaction);
	Expect(transaction.Edit(7,{{"brightness",1.5},{"mode",1.0}}),SettingsCode::Ok,"edit pending display values");
	Expect(transaction.Apply(7,10),SettingsCode::Ok,"apply pending display values");
	Check(transaction.Phase() == SettingsPhase::Confirming && transaction.Deadline() == 25,"display change requires bounded confirmation");
}

void OwnershipAndDrafts() {
	FakeHost host; SettingsTransaction transaction(host);
	Expect(transaction.Edit(7,{}),SettingsCode::NotOpen,"closed edit rejected");
	Expect(transaction.Begin(0),SettingsCode::Invalid,"zero owner rejected");
	Begin(host,transaction);
	Expect(transaction.Edit(7,{{"brightness",1.25}}),SettingsCode::Ok,"draft edit");
	const auto draft = transaction.Draft(); const int reads = host.reads;
	Expect(transaction.Begin(7),SettingsCode::Ok,"same owner reopens existing draft");
	Check(transaction.Draft() == draft && host.reads == reads,"repeated begin preserves unsaved work without rereading");
	Expect(transaction.Begin(8),SettingsCode::Busy,"second owner cannot begin");
	Expect(transaction.Edit(8,{}),SettingsCode::Busy,"second owner cannot edit");
	Expect(transaction.Defaults(8),SettingsCode::Busy,"second owner cannot reset defaults");
	Expect(transaction.Apply(8,1),SettingsCode::Busy,"second owner cannot apply");
	Expect(transaction.Confirm(8),SettingsCode::Busy,"second owner cannot confirm");
	Expect(transaction.Revert(8),SettingsCode::Busy,"second owner cannot revert");
	Expect(transaction.Cancel(8),SettingsCode::Busy,"second owner cannot cancel");
	Expect(transaction.Abandon(8),SettingsCode::Busy,"second owner cannot abandon");
	Check(transaction.Owner() == 7 && transaction.Draft() == draft && host.writes.empty(),"foreign calls preserve owner and draft");
	host.live["volume"] = 0.7;
	Expect(transaction.Cancel(7),SettingsCode::Ok,"editing cancellation closes without restoring external values");
	Check(transaction.Owner() == 0 && transaction.Phase() == SettingsPhase::Closed && transaction.Baseline().empty() &&
		transaction.Draft().empty() && transaction.LastApplied().empty() && host.writes.empty(),"close releases all owned snapshots without host writes");
	Begin(host,transaction,std::numeric_limits<std::uint64_t>::max());
	Expect(transaction.Edit(transaction.Owner(),{{"gamma",1.4}}),SettingsCode::Ok,"new owner edits fresh state");
	Expect(transaction.Revert(transaction.Owner()),SettingsCode::Ok,"editing revert discards draft only");
	Check(transaction.Draft() == host.live && host.writes.empty(),"editing revert is read-only to host");
	Expect(transaction.Abandon(transaction.Owner()),SettingsCode::Ok,"editing abandon closes");
}

void ValidationAndBudgets() {
	FakeHost host; SettingsTransaction transaction(host); Begin(host,transaction);
	const StateValues original = transaction.Draft();
	const std::vector<StateValues> invalid = {
		{{"unknown",1.0}}, {{"shadows",1.0}}, {{"brightness",true}}, {{"brightness",3.0}},
		{{"brightness",std::numeric_limits<double>::quiet_NaN()}}, {{"brightness",std::numeric_limits<double>::infinity()}},
		{{"brightness",1e13}}, {{"label",std::string(65537,'x')}}, {{"label",std::string("a\0b",3)}},
		{{"label",std::string("\xc0\xaf",2)}}, {{std::string(257,'k'),1.0}}, {{"brightness",1.5},{"volume",2.0}}
	};
	for (const auto& patch : invalid) {
		Expect(transaction.Edit(7,patch),SettingsCode::Invalid,"invalid edit rejected");
		Check(transaction.Draft() == original && transaction.Baseline() == original && host.writes.empty(),"invalid edit is atomic");
	}
	Expect(transaction.Edit(7,{{"brightness",1.5},{"volume",0.75},{"label",std::string("literal %s; data")}}),SettingsCode::Ok,"complete merged candidate is validated once");
	const auto edited = transaction.Draft();
	host.defaults.erase("gamma");
	Expect(transaction.Defaults(7),SettingsCode::Invalid,"partial defaults rejected");
	host.defaults = Initial(); host.defaults["gamma"] = true;
	Expect(transaction.Defaults(7),SettingsCode::Invalid,"mistyped defaults rejected");
	host.defaults = Initial(); host.defaults["new"] = 0.0;
	Expect(transaction.Defaults(7),SettingsCode::Invalid,"extra defaults key rejected");
	host.defaultsHook = [](StateValues&,std::string&) -> bool { throw std::runtime_error("defaults failed"); };
	Expect(transaction.Defaults(7),SettingsCode::Invalid,"defaults exception converted");
	Check(transaction.Draft() == edited,"all failed defaults preserve edited draft");
	host.defaultsHook = {}; host.defaults = Initial();
	host.validateHook = [](const StateValues&,const StateValues&,std::string&) -> bool { throw 23; };
	Expect(transaction.Edit(7,{{"gamma",1.3}}),SettingsCode::Invalid,"nonstandard validation exception converted");
	Check(transaction.Draft() == edited,"throwing validation preserves draft");
	host.validateHook = {};
	Expect(transaction.Defaults(7),SettingsCode::Ok,"valid complete defaults replace draft");
	Check(transaction.Draft() == Initial() && host.writes.empty(),"defaults never apply settings");

	FakeHost bounded; SettingsTransaction budget(bounded);
	bounded.live.clear();
	for (std::size_t i = 0; i < SettingsTransaction::MaxSettings; ++i) bounded.live.emplace("key"+std::to_string(i),0.0);
	Expect(budget.Begin(1),SettingsCode::Ok,"maximum catalog count accepted");
	Expect(budget.Cancel(1),SettingsCode::Ok,"close count boundary session");
	bounded.live["overflow"] = 0.0;
	Expect(budget.Begin(1),SettingsCode::Invalid,"catalog count is bounded");
	bounded.live.clear(); bounded.live.emplace(std::string(256,'k'),std::string(65536,'x'));
	Expect(budget.Begin(1),SettingsCode::Ok,"individual key and value bounds accepted");
	Expect(budget.Cancel(1),SettingsCode::Ok,"close individual boundary session");
	bounded.live.emplace("",false);
	Expect(budget.Begin(1),SettingsCode::Invalid,"empty catalog key rejected");
	bounded.live.clear();
	for (int i = 0; i < 16; ++i) bounded.live.emplace("large"+std::to_string(i),std::string(65536,'x'));
	Expect(budget.Begin(1),SettingsCode::Invalid,"aggregate strings have a separate byte budget");
	Check(budget.Phase() == SettingsPhase::Closed && budget.Owner() == 0,"invalid initial snapshot never acquires ownership");
}

void ApplyAndConflicts() {
	FakeHost host; SettingsTransaction transaction(host); Begin(host,transaction);
	Expect(transaction.Apply(7,1),SettingsCode::Ok,"unchanged apply succeeds");
	Check(host.writes.empty() && host.confirmations == 0,"neutral apply invokes no write or confirmation classification");
	Expect(transaction.Edit(7,{{"brightness",1.5},{"shadows",false}}),SettingsCode::Ok,"edit ordinary values");
	Expect(transaction.Apply(7,2),SettingsCode::Ok,"ordinary apply commits immediately");
	Check(host.writes.size() == 1 && host.writes[0] == StateValues({{"brightness",1.5},{"shadows",false}}),"host receives only the changed patch");
	Check(transaction.Phase() == SettingsPhase::Editing && transaction.Baseline() == host.live && transaction.Draft() == host.live &&
		transaction.LastApplied() == host.live,"ordinary apply rebases from exact readback");
	Expect(transaction.Edit(7,{{"gamma",1.3}}),SettingsCode::Ok,"edit before external conflict");
	const auto draft = transaction.Draft(); const auto writes = host.writes.size();
	host.live["volume"] = 0.8;
	Expect(transaction.Apply(7,3),SettingsCode::Conflict,"external pre-apply update rejected");
	Check(host.writes.size() == writes && transaction.Draft() == draft,"pre-apply conflict writes nothing and preserves draft");
	Expect(transaction.Tick(4),SettingsCode::Conflict,"non-action tick retains visible conflict");
	Check(transaction.LastResult().code == SettingsCode::Conflict,"frame tick cannot erase error status");

	FakeHost failure; SettingsTransaction safe(failure); Begin(failure,safe);
	Expect(safe.Edit(7,{{"gamma",1.3}}),SettingsCode::Ok,"edit for pre-write failures");
	failure.readHook = [](StateValues&,std::string&) -> bool { throw std::runtime_error("read failed"); };
	Expect(safe.Apply(7,1),SettingsCode::ApplyFailed,"pre-write read exception converted");
	failure.readHook = {};
	failure.confirmationHook = [](const StateValues&,const StateValues&) -> bool { throw std::runtime_error("classification failed"); };
	Expect(safe.Apply(7,2),SettingsCode::ApplyFailed,"classification exception converted before writing");
	Check(failure.writes.empty() && safe.Phase() == SettingsPhase::Editing,"pre-write exceptions require no rollback");
	failure.confirmationHook = {};
	failure.live["gamma"] = true;
	Expect(safe.Apply(7,3),SettingsCode::ApplyFailed,"changed host catalog type rejected");
	Check(failure.writes.empty(),"schema failure never writes");
}

void ConfirmationAndTime() {
	FakeHost host; SettingsTransaction transaction(host); Pending(host,transaction);
	const auto writes = host.writes.size();
	Expect(transaction.Edit(7,{}),SettingsCode::Busy,"confirmation freezes edits");
	Expect(transaction.Defaults(7),SettingsCode::Busy,"confirmation freezes defaults");
	Expect(transaction.Apply(7,11),SettingsCode::Busy,"confirmation cannot apply twice");
	Expect(transaction.Begin(7),SettingsCode::Busy,"begin cannot bypass confirmation");
	Expect(transaction.Tick(24.99),SettingsCode::Busy,"non-action tick preserves prior result without expiring early");
	Check(host.writes.size() == writes && transaction.Phase() == SettingsPhase::Confirming,"no write before timeout");
	Expect(transaction.Confirm(7),SettingsCode::Ok,"explicit confirmation accepts readback");
	Check(transaction.Phase() == SettingsPhase::Editing && transaction.Baseline() == host.live && transaction.Deadline() == 0,"confirmation commits fresh baseline");
	Expect(transaction.Confirm(7),SettingsCode::Busy,"confirmation is consumed once");
	Expect(transaction.Edit(7,{{"mode",2.0}}),SettingsCode::Ok,"edit another display value");
	Expect(transaction.Apply(7,30,5),SettingsCode::Ok,"custom timeout");
	Expect(transaction.Tick(34.99),SettingsCode::Ok,"before deadline");
	Expect(transaction.Tick(35),SettingsCode::Ok,"exact deadline reverts");
	Check(std::get<double>(host.live.at("mode")) == 1 && transaction.Phase() == SettingsPhase::Editing && transaction.Draft() == host.live,"timeout restores previously confirmed values");
	const int reads = host.reads;
	const double nan = std::numeric_limits<double>::quiet_NaN(), infinity = std::numeric_limits<double>::infinity();
	for (double time : {-1.0,34.0,nan,infinity}) Expect(transaction.Apply(7,time),SettingsCode::Invalid,"invalid or backwards apply time rejected");
	for (double timeout : {0.0,-1.0,nan,infinity}) Expect(transaction.Apply(7,36,timeout),SettingsCode::Invalid,"invalid timeout rejected");
	Expect(transaction.Apply(7,std::numeric_limits<double>::max(),std::numeric_limits<double>::max()),SettingsCode::Invalid,"overflowing deadline rejected");
	Expect(transaction.Apply(7,1e100,1),SettingsCode::Invalid,"unrepresentable deadline increment rejected");
	Check(host.reads == reads,"invalid time is rejected before host callbacks");
	Expect(transaction.Tick(34),SettingsCode::Invalid,"tick cannot move backwards");
	Expect(transaction.Tick(36),SettingsCode::Invalid,"valid idle tick preserves preceding error");
	Expect(transaction.Edit(7,{{"mode",2.0}}),SettingsCode::Ok,"resume edit after invalid timer");
	Expect(transaction.Apply(7,37),SettingsCode::Ok,"apply resumes with valid monotonic time");
	Expect(transaction.Cancel(7),SettingsCode::Ok,"cancel confirmation returns to editing after rollback");
	Check(transaction.Phase() == SettingsPhase::Editing && transaction.Owner() == 7,"cancel confirmation keeps session open");
	Expect(transaction.Cancel(7),SettingsCode::Ok,"cancel editing closes");
	Expect(transaction.Tick(0),SettingsCode::Ok,"closed session has no old time domain");
}

void PartialWritesAndReadback() {
	FakeHost host; SettingsTransaction transaction(host); Begin(host,transaction);
	Expect(transaction.Edit(7,{{"brightness",1.5},{"mode",1.0}}),SettingsCode::Ok,"edit partial-write batch");
	host.writeHook = [&](const StateValues& patch, std::string& error) {
		if (host.writes.size() == 1) {
			host.live["brightness"] = patch.at("brightness"); host.live["volume"] = 0.75;
			error = "second write refused"; return false;
		}
		host.Patch(patch); return true;
	};
	Expect(transaction.Apply(7,1),SettingsCode::ApplyFailed,"partially failed apply rolls back");
	Check(host.writes.size() == 2 && host.writes[1] == StateValues({{"brightness",1.0}}),"rollback touches only the value actually changed");
	Check(transaction.Phase() == SettingsPhase::Editing && transaction.Baseline() == host.live &&
		transaction.Draft().at("brightness") == StateValue(1.5) && transaction.Draft().at("mode") == StateValue(1.0) &&
		transaction.Draft().at("volume") == StateValue(0.75),"recovered failure preserves attempted edits and refreshes untouched external values");
	host.writeHook = {};
	Expect(transaction.Apply(7,2),SettingsCode::Ok,"retry after rollback uses refreshed baseline");
	Check(!host.writes.back().contains("volume"),"retry cannot overwrite unrelated external update");
	Expect(transaction.Abandon(7),SettingsCode::Ok,"abandon pending retry restores and closes");
	Check(host.live.at("volume") == StateValue(0.75) && transaction.Phase() == SettingsPhase::Closed,"abandon preserves external update");

	for (int exceptionKind : {0,1}) {
		FakeHost throwing; SettingsTransaction guard(throwing); Begin(throwing,guard);
		Expect(guard.Edit(7,{{"brightness",1.5}}),SettingsCode::Ok,"edit for throwing write");
		throwing.writeHook = [&](const StateValues& patch,std::string&) -> bool {
			throwing.Patch(patch);
			if (throwing.writes.size() == 1) {
				if (exceptionKind == 0) throw std::runtime_error("partial mutation");
				throw 42;
			}
			return true;
		};
		Expect(guard.Apply(7,1),SettingsCode::ApplyFailed,"throw after write triggers verified rollback");
		Check(throwing.live == Initial() && throwing.writes.size() == 2 && guard.Phase() == SettingsPhase::Editing,"all host exception forms roll back partial mutation");
	}

	FakeHost readback; SettingsTransaction reader(readback); Begin(readback,reader);
	Expect(reader.Edit(7,{{"brightness",1.5}}),SettingsCode::Ok,"edit for readback failure");
	int calls = 0;
	readback.readHook = [&](StateValues& values,std::string& error) {
		if (++calls == 2) { error = "one readback failed"; return false; }
		values = readback.live; return true;
	};
	Expect(reader.Apply(7,1),SettingsCode::ApplyFailed,"transient readback failure still attempts rollback");
	Check(calls == 4 && readback.live == Initial() && readback.writes.size() == 2,"rollback requires fresh pre-write and post-write reads");

	FakeHost refused; SettingsTransaction untouched(refused); Begin(refused,untouched);
	Expect(untouched.Edit(7,{{"brightness",1.5}}),SettingsCode::Ok,"edit refused write");
	refused.writeHook = [](const StateValues&,std::string&) { return false; };
	Expect(untouched.Apply(7,1),SettingsCode::ApplyFailed,"fully refused apply recovers without writes to original values");
	Check(refused.writes.size() == 1 && refused.live == Initial() && untouched.Phase() == SettingsPhase::Editing,"no redundant rollback patch if already original");
}

void RecoveryAndExternalOwnership() {
	FakeHost host; SettingsTransaction transaction(host); Pending(host,transaction);
	host.live["brightness"] = 1.8;
	Expect(transaction.Revert(7),SettingsCode::Conflict,"external update to written value prevents claiming complete rollback");
	Check(host.live.at("brightness") == StateValue(1.8) && host.live.at("mode") == StateValue(0.0) &&
		host.writes.back() == StateValues({{"mode",0.0}}),"safe owned keys restore while divergent external keys remain untouched");
	Check(transaction.Phase() == SettingsPhase::RecoveryRequired && transaction.Owner() == 7 && transaction.Baseline() == Initial(),"unresolved conflict retains original recovery information");
	const int reads = host.reads; const auto writes = host.writes.size();
	Expect(transaction.Tick(50),SettingsCode::Conflict,"recovery tick preserves conflict");
	Check(host.reads == reads && host.writes.size() == writes,"recovery never repeats host operations every frame");
	Expect(transaction.Begin(8),SettingsCode::Busy,"recovery cannot be stolen by another view");
	Expect(transaction.Abandon(7),SettingsCode::Conflict,"failed abandon keeps owner and unresolved external value");
	host.live["brightness"] = 1.0;
	Expect(transaction.Revert(7),SettingsCode::Ok,"explicit retry recognizes externally restored original");
	Check(transaction.Phase() == SettingsPhase::Editing && transaction.Draft() == host.live,"recovery exits only after written values verify original");

	FakeHost unavailable; SettingsTransaction pending(unavailable); Begin(unavailable,pending);
	Expect(pending.Edit(7,{{"mode",1.0}}),SettingsCode::Ok,"edit before readback becomes unavailable");
	bool changed = false;
	unavailable.writeHook = [&](const StateValues& patch,std::string&) { unavailable.Patch(patch); changed = true; return true; };
	unavailable.readHook = [&](StateValues& values,std::string& error) {
		if (changed) { error = "unavailable"; return false; }
		values = unavailable.live; return true;
	};
	Expect(pending.Apply(7,1),SettingsCode::RollbackFailed,"persistent missing readback enters recovery");
	Check(unavailable.writes.size() == 1 && pending.Phase() == SettingsPhase::RecoveryRequired,"unknown ownership never triggers blind write");
	unavailable.readHook = {}; unavailable.writeHook = {};
	Expect(pending.Revert(7),SettingsCode::Ok,"readback recovery permits explicit rollback retry");
	Check(unavailable.live == Initial(),"retry restores original pending value");

	FakeHost refusal; SettingsTransaction blocked(refusal); Pending(refusal,blocked);
	refusal.writeHook = [](const StateValues&,std::string& error) { error = "restore refused"; return false; };
	Expect(blocked.Abandon(7),SettingsCode::RollbackFailed,"rollback refusal prevents abandoning live changes");
	Check(blocked.Owner() == 7 && blocked.Phase() == SettingsPhase::RecoveryRequired,"rollback refusal retains owner");
	refusal.writeHook = {};
	Expect(blocked.Abandon(7),SettingsCode::Ok,"successful recovery abandon closes");
	Check(blocked.Owner() == 0 && refusal.live == Initial(),"successful abandon releases ownership after verification");

	FakeHost normalized; SettingsTransaction exact(normalized); Begin(normalized,exact);
	Expect(exact.Edit(7,{{"brightness",1.5}}),SettingsCode::Ok,"edit for normalized readback");
	normalized.writeHook = [&](const StateValues&,std::string&) { normalized.live["brightness"] = 1.49; return true; };
	Expect(exact.Apply(7,1),SettingsCode::Conflict,"unrecognized normalized value is not silently claimed as our owned write");
	Check(normalized.writes.size() == 1 && normalized.live.at("brightness") == StateValue(1.49) && exact.Phase() == SettingsPhase::RecoveryRequired,"readback mismatch cannot be blindly overwritten");
	normalized.writeHook = {}; normalized.live["brightness"] = 1.5;
	Expect(exact.Revert(7),SettingsCode::Ok,"explicit retry can restore a recognized owned value");
}

void ConfirmationConflictAndRollbackPolicy() {
	FakeHost host; SettingsTransaction transaction(host); Pending(host,transaction);
	host.live["volume"] = 0.9;
	Expect(transaction.Confirm(7),SettingsCode::Conflict,"confirmation never silently accepts a changed full snapshot");
	Check(transaction.Phase() == SettingsPhase::Editing && host.live.at("mode") == StateValue(0.0) &&
		host.live.at("brightness") == StateValue(1.0) && host.live.at("volume") == StateValue(0.9) &&
		transaction.Baseline() == host.live && transaction.Draft() == host.live,"unrelated external changes survive rollback and rebase without trapping the owner");

	FakeHost coupled; SettingsTransaction safe(coupled); Pending(coupled,safe);
	coupled.live["volume"] = 0.9;
	coupled.rollbackHook = [&](const StateValues& original,const StateValues& current,const StateValues& target,std::string& error) {
		Check(original == Initial() && current == coupled.live,"rollback validator receives original and fresh live snapshots");
		Check(target.at("volume") == current.at("volume"),"rollback candidate preserves unrelated host values");
		if (target.at("mode") == StateValue(0.0) && target.at("volume") == StateValue(0.9)) {
			error = "coupled target is unavailable"; return false;
		}
		return true;
	};
	const auto writes = coupled.writes.size();
	Expect(safe.Revert(7),SettingsCode::RollbackFailed,"invalid merged rollback is rejected before writes");
	Check(coupled.writes.size() == writes && safe.Phase() == SettingsPhase::RecoveryRequired,"coupled rollback validation is atomic");
	coupled.live["volume"] = 0.5;
	Expect(safe.Revert(7),SettingsCode::Ok,"coupled conflict can be resolved and retried");

	FakeHost custom; custom.live["brightness"] = 2.5;
	SettingsTransaction original(custom); Begin(custom,original);
	Expect(original.Edit(7,{{"brightness",3.0}}),SettingsCode::Invalid,"ordinary edit retains strict UI range");
	Expect(original.Edit(7,{{"brightness",1.5},{"mode",1.0}}),SettingsCode::Ok,"custom baseline may be edited into supported choices");
	Expect(original.Apply(7,1),SettingsCode::Ok,"apply supported choice over custom original");
	custom.rollbackHook = [](const StateValues& before,const StateValues&,const StateValues& target,std::string&) {
		return before.at("brightness") == StateValue(2.5) && target.at("brightness") == before.at("brightness");
	};
	Expect(original.Revert(7),SettingsCode::Ok,"rollback-specific validation can restore typed custom originals");
	Check(custom.live.at("brightness") == StateValue(2.5) && original.Baseline() == custom.live,"rollback preserves custom original exactly");
}

void ReentrancyAndIndependentInstances() {
	FakeHost host; SettingsTransaction transaction(host);
	std::set<std::string> callbacks;
	host.callback = [&](const char* operation) {
		callbacks.insert(operation);
		const auto phase = transaction.Phase(); const auto owner = transaction.Owner();
		const auto baseline = transaction.Baseline(), draft = transaction.Draft(), applied = transaction.LastApplied();
		const auto result = transaction.LastResult(); const double deadline = transaction.Deadline();
		Expect(transaction.Begin(9),SettingsCode::Busy,"reentrant begin blocked");
		Expect(transaction.Edit(7,{}),SettingsCode::Busy,"reentrant edit blocked");
		Expect(transaction.Defaults(7),SettingsCode::Busy,"reentrant defaults blocked");
		Expect(transaction.Cancel(7),SettingsCode::Busy,"reentrant cancel blocked");
		Expect(transaction.Apply(7,0),SettingsCode::Busy,"reentrant apply blocked");
		Expect(transaction.Confirm(7),SettingsCode::Busy,"reentrant confirm blocked");
		Expect(transaction.Revert(7),SettingsCode::Busy,"reentrant revert blocked");
		Expect(transaction.Tick(0),SettingsCode::Busy,"reentrant tick blocked");
		Expect(transaction.Abandon(7),SettingsCode::Busy,"reentrant abandon blocked");
		Check(transaction.Phase() == phase && transaction.Owner() == owner && transaction.Baseline() == baseline &&
			transaction.Draft() == draft && transaction.LastApplied() == applied && transaction.Deadline() == deadline &&
			transaction.LastResult().code == result.code && transaction.LastResult().diagnostic == result.diagnostic,
			"reentrant operations cannot change state or replace the outer operation's result");
	};
	Begin(host,transaction);
	Expect(transaction.Defaults(7),SettingsCode::Ok,"guard defaults callback");
	Expect(transaction.Edit(7,{{"mode",1.0}}),SettingsCode::Ok,"guard validation callback");
	Expect(transaction.Apply(7,1),SettingsCode::Ok,"guard classification, patch and readback callbacks");
	Expect(transaction.Revert(7),SettingsCode::Ok,"guard rollback callbacks");
	Check(callbacks == std::set<std::string>({"Read","Defaults","Validate","NeedsConfirmation","Write","ValidateRollback"}),"every host callback boundary exercises all mutating reentry guards");
	Expect(transaction.Cancel(7),SettingsCode::Ok,"operation guard releases after callbacks");

	FakeHost shared; SettingsTransaction first(shared), second(shared);
	Begin(shared,first,1); Begin(shared,second,2);
	Expect(first.Edit(1,{{"brightness",1.5}}),SettingsCode::Ok,"first instance draft");
	Expect(second.Edit(2,{{"gamma",1.2}}),SettingsCode::Ok,"second instance draft");
	Check(first.Draft().at("gamma") == StateValue(1.0) && second.Draft().at("brightness") == StateValue(1.0),"separate instances do not share drafts");
	Expect(first.Apply(1,1),SettingsCode::Ok,"first instance commits");
	Expect(second.Apply(2,1),SettingsCode::Conflict,"second instance detects shared-host external commit");
	Check(shared.writes.size() == 1 && second.Draft().at("gamma") == StateValue(1.2),"shared host conflict prevents lost updates");
}

void MergedRecoveryBudgets() {
	FakeHost host;
	StateValues edits{{"mode",1.0}};
	for (int i = 0; i < 8; ++i) {
		host.live.emplace("original"+std::to_string(i),std::string(65536,'a'));
		host.live.emplace("external"+std::to_string(i),std::string());
		edits.emplace("original"+std::to_string(i),std::string());
	}
	SettingsTransaction transaction(host); Begin(host,transaction);
	Expect(transaction.Edit(7,edits),SettingsCode::Ok,"large but bounded draft accepted");
	Expect(transaction.Apply(7,1),SettingsCode::Ok,"apply bounded large draft");
	for (int i = 0; i < 8; ++i) host.live["external"+std::to_string(i)] = std::string(65536,'b');
	const auto writes = host.writes.size();
	Expect(transaction.Revert(7),SettingsCode::RollbackFailed,"merged rollback remains inside aggregate budget");
	Check(host.writes.size() == writes && host.rollbackValidations == 0,"oversized merged rollback is rejected before host validation or writes");
	for (int i = 0; i < 8; ++i) host.live["external"+std::to_string(i)] = std::string();
	Expect(transaction.Revert(7),SettingsCode::Ok,"bounded external state permits rollback retry");

	FakeHost retry;
	StateValues attempt;
	for (int i = 0; i < 8; ++i) {
		retry.live.emplace("attempt"+std::to_string(i),std::string());
		retry.live.emplace("external"+std::to_string(i),std::string());
		attempt.emplace("attempt"+std::to_string(i),std::string(65536,'a'));
	}
	SettingsTransaction retained(retry); Begin(retry,retained);
	Expect(retained.Edit(7,attempt),SettingsCode::Ok,"bounded attempted edits");
	retry.writeHook = [&](const StateValues&,std::string&) {
		for (int i = 0; i < 8; ++i) retry.live["external"+std::to_string(i)] = std::string(65536,'b');
		return false;
	};
	Expect(retained.Apply(7,1),SettingsCode::RollbackFailed,"oversized preserved retry draft requires explicit recovery");
	Check(retained.Phase() == SettingsPhase::RecoveryRequired && retry.writes.size() == 1,"bounded snapshots remain intact when attempted and external values cannot fit together");
	Expect(retained.Revert(7),SettingsCode::Ok,"explicit revert discards attempted draft and adopts bounded live state");
	Check(retained.Baseline() == retry.live && retained.Draft() == retry.live,"retry budget recovery preserves all external values");
}
} // namespace

int main() {
	OwnershipAndDrafts();
	ValidationAndBudgets();
	ApplyAndConflicts();
	ConfirmationAndTime();
	PartialWritesAndReadback();
	RecoveryAndExternalOwnership();
	ConfirmationConflictAndRollbackPolicy();
	ReentrancyAndIndependentInstances();
	MergedRecoveryBudgets();
	std::puts("UI settings: owned drafts, bounded typed edits, patch apply, conflict-safe rollback, confirmation, recovery and reentrancy passed");
}
