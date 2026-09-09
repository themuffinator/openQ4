// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "src/ui/retained/TextInputBroker.h"
#include "src/ui/retained/TextEdit.h"
#include <cstdlib>
#include <iostream>
#include <map>
#include <stdexcept>

using namespace openq4::ui;
namespace {
int checks = 0;
void Check(bool ok, const char* message) {
	++checks; if (!ok) throw std::runtime_error(message);
}
using Kind = TextNativeKind;
using Origin = TextNativeOrigin;
using Disposition = TextBrokerDisposition;
TextBrokerContext Editor(std::uint64_t allocation = 1, std::string control = "number_a") {
	return {TextBrokerRoute::Retained, 1, 1,
		TextEditorIdentity{allocation, 1, 1, 1, 1, allocation, 1, std::move(control)}};
}
struct Driver {
	TextInputBroker broker;
	TextBrokerContext context = Editor();
	std::map<std::uint64_t, TextEditBuffer> fields;
	std::string error, legacy;
	std::uint64_t sequence = 0;
	int applications = 0, legacyCalls = 0;
	explicit Driver(TextUnmarkedPolicy policy = TextUnmarkedPolicy::Reject) : broker(policy) {
		Check(broker.Synchronize(context, error), "initial editor context");
		Check(Send(Kind::SessionBegin).disposition == Disposition::Ignored, "native session starts");
	}
	TextEditBuffer& Buffer(std::uint64_t id = 1) {
		auto [it, inserted] = fields.try_emplace(id);
		if (inserted) Check(it->second.Reset("", {}, error), "initialize real text buffer");
		return it->second;
	}
	void Sync(TextBrokerContext next) {
		Check(broker.Synchronize(next, error), "synchronize ordered context"); context = std::move(next);
	}
	TextNativeRecord Record(Kind kind, std::uint64_t composition = 0,
		Origin origin = Origin::Unknown, std::string text = "") {
		TextNativeRecord record; record.kind = kind; record.sequence = ++sequence;
		record.window = context.nativeWindow ? context.nativeWindow : 1;
		record.session = context.nativeSession ? context.nativeSession : 1;
		record.composition = composition; record.origin = origin;
		if (kind == Kind::Commit) Check(MakeTextInputCommit(text, record.input, error), "build native commit");
		else if (kind == Kind::Preedit) Check(MakeTextInputPreedit(text, TextIndexUnit::UnicodeScalars, -1, -1, record.input, error), "build native preedit");
		return record;
	}
	TextBrokerResult Process(const TextNativeRecord& record, bool apply = true, bool noChange = false) {
		auto result = broker.Process(record);
		if (result.disposition == Disposition::Retained && apply) {
			Check(result.delivery && context.editor && result.delivery->target == *context.editor, "delivery binds exact editor");
			Check(Buffer(context.editor->allocation).Apply(result.delivery->input, error), "real buffer accepts event");
			++applications;
			if (!noChange) ++context.editor->revision;
			Check(broker.Complete(result.delivery->token, noChange ? TextDeliveryOutcome::AppliedNoChange : TextDeliveryOutcome::AppliedChanged,
				context, error), "checked editor mutation receipt");
		} else if (result.disposition == Disposition::Legacy) {
			Check(result.legacy && !result.delivery && !broker.Pending(), "legacy disposition is separate from retained delivery");
			legacy += result.legacy->text; ++legacyCalls;
		}
		return result;
	}
	TextBrokerResult Send(Kind kind, std::uint64_t composition = 0, Origin origin = Origin::Unknown,
		std::string text = "", bool apply = true, bool noChange = false) {
		return Process(Record(kind, composition, origin, std::move(text)), apply, noChange);
	}
	void Echo(std::uint64_t original) {
		TextNativeRecord echo; echo.kind = Kind::Echo; echo.echoOf = original;
		Check(broker.Process(echo).disposition == Disposition::Ignored, "canonical public echo consumed once");
		Check(broker.Process(echo).disposition == Disposition::Dropped, "duplicate public echo never re-delivered");
	}
	void Begin(std::uint64_t composition = 1) {
		Check(Send(Kind::Begin, composition, Origin::ObservedComposition).disposition == Disposition::Ignored, "composition begins at ordered editor");
	}
	void FreshSession(std::uint64_t session) {
		Check(Send(Kind::SessionEnd).disposition == Disposition::Ignored, "session ends before fresh barrier");
		context.nativeSession = session;
		Check(Send(Kind::SessionBegin).disposition == Disposition::Ignored, "new native session barrier");
		Sync(context);
	}
};

void OrderedOwnership() {
	Driver d; d.Begin();
	Check(d.Send(Kind::Preedit, 1, Origin::ObservedComposition, "old").disposition == Disposition::Retained, "original preedit accepted");
	Check(d.Buffer().Composition().has_value() && d.Buffer().State().text.empty(), "preedit never inserts committed text");
	d.Sync(Editor(2, "number_b")); // Tab processed by Session between records in one pump.
	d.Buffer(1).CancelComposition(); // Adapter's focus-loss cancellation is separate.
	Check(d.Send(Kind::Preedit, 1, Origin::ObservedComposition, "").disposition == Disposition::Dropped, "late empty A cannot target B");
	Check(d.Send(Kind::Result, 1, Origin::ObservedComposition).disposition == Disposition::Ignored, "result is metadata only");
	Check(d.Send(Kind::Commit, 1, Origin::ObservedComposition, "A").disposition == Disposition::Dropped, "late A result cannot enter B");
	d.Echo(d.sequence);
	Check(d.Send(Kind::Commit, 0, Origin::ProvenDirect, "B").disposition == Disposition::Retained, "qualified new direct B works in same batch");
	d.Echo(d.sequence);
	Check(d.Buffer(1).State().text.empty() && d.Buffer(2).State().text == "B" && d.applications == 2, "only original preedit and new direct B mutated buffers");
	Check(d.Send(Kind::End, 1, Origin::ObservedComposition).disposition == Disposition::Ignored, "old origin ends");
	d.Begin(2);
	Check(d.Send(Kind::Commit, 2, Origin::ObservedComposition, "雪").disposition == Disposition::Retained, "new B composition binds once");
	d.Echo(d.sequence);
	Check(d.Buffer(2).State().text == "B雪", "Unicode committed without byte splitting");
}

void WindowsUnmarkedPolicy() {
	Driver strict;
	Check(strict.Send(Kind::Commit, 0, Origin::UnmarkedCharacter, "x").disposition == Disposition::Dropped, "unmarked is rejected by default");
	strict.Echo(strict.sequence);
	Driver qualified(TextUnmarkedPolicy::QualifiedCleanSession);
	Check(qualified.Send(Kind::Commit, 0, Origin::UnmarkedCharacter, "a").disposition == Disposition::Retained, "explicit partial qualification permits clean-session unmarked");
	qualified.Echo(qualified.sequence);
	qualified.Begin();
	qualified.Send(Kind::Preedit, 1, Origin::ObservedComposition, "old");
	qualified.Sync(Editor(2, "number_b"));
	qualified.Send(Kind::End, 1, Origin::ObservedComposition);
	Check(qualified.Send(Kind::Commit, 0, Origin::UnmarkedCharacter, "b").disposition == Disposition::Dropped, "retirement taints unmarked even after native End");
	qualified.Echo(qualified.sequence);
	qualified.Begin(2);
	qualified.Send(Kind::End, 2, Origin::ObservedComposition);
	Check(qualified.Send(Kind::Commit, 0, Origin::UnmarkedCharacter, "b").disposition == Disposition::Dropped, "fresh composition cannot clear session taint");
	qualified.Echo(qualified.sequence);
	qualified.FreshSession(2);
	Check(qualified.Send(Kind::Commit, 0, Origin::UnmarkedCharacter, "b").disposition == Disposition::Retained, "explicit end/begin barrier permits newly qualified unmarked stream");
	qualified.Echo(qualified.sequence);
	Check(qualified.Send(Kind::Commit, 0, Origin::Unknown, "old").disposition == Disposition::Dropped, "policy never upgrades Unknown");
	qualified.Echo(qualified.sequence);
	Check(qualified.Buffer(2).State().text == "b", "old uncertain text absent");
	Driver missing(TextUnmarkedPolicy::QualifiedCleanSession);
	Check(missing.Send(Kind::Commit, 88, Origin::ObservedComposition, "old").disposition == Disposition::Dropped, "missing native Begin creates no affinity");
	missing.Echo(missing.sequence);
	Check(missing.Send(Kind::Commit, 0, Origin::UnmarkedCharacter, "new").disposition == Disposition::Dropped, "missing-Begin result taints qualified unmarked input");
	missing.Echo(missing.sequence);
	for (Origin uncertain : {Origin::Unknown, Origin::ObservedComposition}) {
		Driver background(TextUnmarkedPolicy::QualifiedCleanSession);
		auto noEditor = background.context; noEditor.editor.reset(); background.Sync(noEditor);
		Check(background.Send(Kind::Commit, uncertain == Origin::Unknown ? 0 : 55, uncertain, "old").disposition == Disposition::Dropped,
			"ambiguous admission while a button is focused is dropped");
		background.Echo(background.sequence); background.Sync(Editor(2));
		Check(background.Send(Kind::Commit, 0, Origin::UnmarkedCharacter, "new").disposition == Disposition::Dropped,
			"newly focused editor cannot clear session uncertainty received without an editor");
		background.Echo(background.sequence);
		Check(background.Send(Kind::Commit, 0, Origin::ProvenDirect, "new").disposition == Disposition::Retained,
			"proven new direct input remains separate from partial Unmarked qualification");
		background.Echo(background.sequence);
	}
	// The actual Windows observer may keep stronger native-window taint and
	// continue publishing Unknown after this barrier. This model cannot turn
	// those records into Unmarked/ProvenDirect or qualify Windows losslessness.
}

void EmptyResultChunksAndNoOps() {
	Driver d; d.Begin();
	d.Send(Kind::Preedit, 1, Origin::ObservedComposition, "候補");
	Check(d.Buffer().PresentedText() == "候補", "real buffer shows preedit");
	d.Send(Kind::Preedit, 1, Origin::ObservedComposition, "");
	Check(!d.Buffer().Composition(), "empty preedit clears presentation");
	const auto revision = d.context.editor->revision;
	Check(d.Send(Kind::Preedit, 1, Origin::ObservedComposition, "", true, true).disposition == Disposition::Retained, "successful no-op has explicit unchanged receipt");
	Check(d.context.editor->revision == revision, "no-op receipt does not invent revision");
	const int applied = d.applications;
	d.Send(Kind::Result, 1, Origin::ObservedComposition);
	Check(d.applications == applied && d.Buffer().State().text.empty(), "Result metadata inserts nothing");
	for (const auto& chunk : {"雪", "🧪", "!"}) {
		Check(d.Send(Kind::Commit, 1, Origin::ObservedComposition, chunk).disposition == Disposition::Retained, "each admitted native result chunk accepted once");
		d.Echo(d.sequence);
	}
	d.Send(Kind::End, 1, Origin::ObservedComposition);
	Check(d.Buffer().State().text == "雪🧪!", "split native result assembled once in order");
	Check(d.Send(Kind::Commit, 1, Origin::ObservedComposition, "late").disposition == Disposition::Dropped, "ended origin cannot receive more observed chunks");
	d.Echo(d.sequence);
	d.Begin(2);
	Check(d.Send(Kind::Cancel, 2, Origin::ObservedComposition, "", true, true).disposition == Disposition::Retained, "empty cancel accepts exact no-op receipt");
	Check(d.Send(Kind::Commit, 2, Origin::ObservedComposition, "late").disposition == Disposition::Dropped, "cancel never commits old composition");
	d.Echo(d.sequence);
	d.Begin(3); d.Send(Kind::Preedit, 3, Origin::ObservedComposition, "temporary");
	d.Send(Kind::Result, 3, Origin::ObservedComposition);
	d.Send(Kind::Preedit, 3, Origin::ObservedComposition, "");
	Check(d.Send(Kind::Commit, 3, Origin::ObservedComposition, "z").disposition == Disposition::Retained,
		"empty clear after Result metadata retains the same original affinity too");
	d.Echo(d.sequence);
	Check(d.Buffer().State().text == "雪🧪!z", "both result/empty-clear orders preserve exactly-once committed content");
}

void IdentityChanges() {
	for (int field = 0; field < 10; ++field) {
		Driver d; d.Begin(); d.Send(Kind::Preedit, 1, Origin::ObservedComposition, "old");
		auto original = d.context, changed = original;
		switch (field) {
			case 0: ++changed.editor->allocation; break;
			case 1: ++changed.editor->backend; break;
			case 2: ++changed.editor->document; break;
			case 3: ++changed.editor->modal; break;
			case 4: ++changed.editor->session; break;
			case 5: ++changed.editor->revision; break;
			case 6: changed.editor->control = "replacement"; break;
			case 7: changed.editor.reset(); break;
			case 8: changed = {}; break;
			case 9: ++changed.nativeWindow; changed.editor->window = changed.nativeWindow; break;
		}
		d.Sync(changed); d.Sync(original); // ABA, focus regain, or command replacement.
		Check(d.Send(Kind::Commit, 1, Origin::ObservedComposition, "stale").disposition == Disposition::Dropped, "identity retirement survives restoration of previous values");
		d.Echo(d.sequence);
		Check(d.Buffer().State().text.empty(), "retired native origin never edits restored owner");
		Check(d.Send(Kind::Commit, 0, Origin::ProvenDirect, "fresh").disposition == Disposition::Retained, "independent direct origin can target current owner");
		d.Echo(d.sequence);
	}
	Driver d; d.Begin();
	auto unavailable = d.context; unavailable.editor.reset(); d.Sync(unavailable);
	Check(d.Send(Kind::Commit, 0, Origin::ProvenDirect, "lost").disposition == Disposition::Dropped, "retained with no field is never legacy fallback");
	d.Echo(d.sequence);
	Check(d.legacyCalls == 0, "no legacy sink inferred from missing retained editor");
}

void LegacyRouting() {
	Driver d; d.Begin(); d.Send(Kind::Preedit, 1, Origin::ObservedComposition, "private");
	d.Sync({TextBrokerRoute::Legacy, 1, 1, {}});
	Check(d.Send(Kind::Commit, 1, Origin::ObservedComposition, "private").disposition == Disposition::Dropped, "retained origin must not leak into console/legacy");
	d.Echo(d.sequence);
	Check(d.Send(Kind::Commit, 1, Origin::Unknown, "private").disposition == Disposition::Dropped, "known retired composition cannot shed ownership as Unknown");
	d.Echo(d.sequence);
	d.Send(Kind::End, 1, Origin::ObservedComposition);
	d.Begin(2); d.Send(Kind::Result, 2, Origin::ObservedComposition);
	Check(d.Send(Kind::Commit, 2, Origin::ObservedComposition, "legacy").disposition == Disposition::Legacy, "original legacy composition follows legacy route once");
	d.Echo(d.sequence);
	Check(d.Send(Kind::Commit, 0, Origin::Unknown, "!").disposition == Disposition::Legacy, "ordinary unassociated legacy admission remains compatible");
	d.Echo(d.sequence);
	Check(d.legacy == "legacy!" && d.legacyCalls == 2, "exact legacy result with no echo duplicates");
	d.Sync(Editor(2));
	Check(d.Send(Kind::Commit, 2, Origin::ObservedComposition, "old").disposition == Disposition::Dropped, "legacy native origin cannot become retained");
	d.Echo(d.sequence);
	d.Sync({TextBrokerRoute::Legacy, 1, 1, {}});
	Check(d.Send(Kind::Commit, 2, Origin::ObservedComposition, "old").disposition == Disposition::Dropped, "legacy affinity also retires on route loss");
	d.Echo(d.sequence);
}

void BadReceipts() {
	for (int variant = 0; variant < 9; ++variant) {
		Driver d; d.Begin();
		auto result = d.Send(Kind::Preedit, 1, Origin::ObservedComposition, "x", false);
		Check(result.delivery && d.broker.Pending(), "one pending exact delivery");
		auto after = d.context; auto token = result.delivery->token;
		auto outcome = TextDeliveryOutcome::AppliedChanged;
		++after.editor->revision;
		switch (variant) {
			case 0: ++token; break;
			case 1: ++after.editor->allocation; break;
			case 2: ++after.editor->session; break;
			case 3: ++after.nativeSession; break;
			case 4: --after.editor->revision; break;
			case 5: outcome = TextDeliveryOutcome::AppliedNoChange; break;
			case 6: outcome = TextDeliveryOutcome::Rejected; break;
			case 7: after = {}; break;
			case 8: outcome = static_cast<TextDeliveryOutcome>(99); break;
		}
		Check(!d.broker.Complete(token, outcome, after, d.error), "bad receipt rejected");
		Check(!d.broker.Healthy() && !d.broker.Pending() && !d.error.empty(), "bad receipt poisons ownership without relabeling");
		Check(d.Buffer().State().text.empty() && !d.Buffer().Composition(), "broker never invokes a sink itself");
		Check(!d.broker.Complete(result.delivery->token, TextDeliveryOutcome::AppliedNoChange, d.context, d.error), "late original receipt cannot revive delivery");
	}
	for (int reentrant = 0; reentrant < 3; ++reentrant) {
		Driver d; auto result = d.Send(Kind::Commit, 0, Origin::ProvenDirect, "x", false);
		if (reentrant == 0) Check(!d.broker.Synchronize(d.context, d.error), "even same-context reentry cancels pending delivery");
		else if (reentrant == 1) Check(d.Send(Kind::Commit, 0, Origin::ProvenDirect, "y").disposition == Disposition::Failed, "new record before receipt is fail-closed");
		else Check(!d.broker.Reset(2, d.error), "reset cannot skip pending receipt");
		Check(!d.broker.Complete(result.delivery->token, TextDeliveryOutcome::AppliedNoChange, d.context, d.error), "abandoned delivery cannot complete");
		Check(d.applications == 0 && d.Buffer().State().text.empty(), "reentry adds no host side effect");
	}
	Driver rejected; rejected.Begin();
	auto result = rejected.Send(Kind::Preedit, 1, Origin::ObservedComposition, "refused", false);
	Check(rejected.broker.Complete(result.delivery->token, TextDeliveryOutcome::Rejected, rejected.context, rejected.error), "atomic sink refusal has valid receipt");
	Check(rejected.broker.Healthy(), "ordinary field rejection is not provider failure");
	Check(rejected.Send(Kind::Commit, 1, Origin::ObservedComposition, "late").disposition == Disposition::Dropped, "rejected preedit retires subsequent result");
	rejected.Echo(rejected.sequence);
	Driver fieldPolicy; fieldPolicy.Begin();
	auto multiline = fieldPolicy.Send(Kind::Commit, 1, Origin::ObservedComposition, "bad\nline", false);
	Check(multiline.delivery && !fieldPolicy.Buffer().Apply(multiline.delivery->input, fieldPolicy.error),
		"real single-line field can reject a transport-valid Unicode commit atomically");
	Check(fieldPolicy.broker.Complete(multiline.delivery->token, TextDeliveryOutcome::Rejected, fieldPolicy.context, fieldPolicy.error),
		"actual field policy rejection retires native affinity through explicit receipt");
	fieldPolicy.Echo(fieldPolicy.sequence);
	Check(fieldPolicy.Buffer().State().text.empty() && !fieldPolicy.Buffer().CanUndo(), "failed native edit adds neither text nor undo history");
}

void UnknownAndMalformed() {
	Driver unscoped; unscoped.Begin(); unscoped.Send(Kind::Preedit, 1, Origin::ObservedComposition, "original");
	const auto owner = unscoped.context;
	for (Kind kind : {Kind::Begin, Kind::Preedit, Kind::Result, Kind::End, Kind::Cancel}) {
		auto record = unscoped.Record(kind, 33, Origin::Unknown, kind == Kind::Preedit ? "unscoped" : "");
		record.session = 0;
		Check(unscoped.Process(record).disposition == Disposition::Dropped, "zero-session Unknown composition metadata advances stream without acquiring owner");
		Check(unscoped.broker.Healthy() && unscoped.context == owner && unscoped.Buffer().PresentedText() == "original",
			"unscoped metadata leaves another active native session and editor untouched");
	}
	Check(unscoped.Send(Kind::Commit, 1, Origin::ObservedComposition, "actual").disposition == Disposition::Retained,
		"valid original composition survives unrelated unscoped metadata");
	unscoped.Echo(unscoped.sequence);
	for (Kind kind : {Kind::Begin, Kind::Preedit, Kind::Result, Kind::End, Kind::Cancel, Kind::Commit}) {
		Driver invalid; auto record = invalid.Record(kind, 1, Origin::ObservedComposition); record.session = 0;
		Check(invalid.Process(record).disposition == Disposition::Failed, "observed provenance cannot exist without a native session");
	}
	for (Kind kind : {Kind::Begin, Kind::Preedit, Kind::Result, Kind::End, Kind::Cancel}) {
		Driver d;
		Check(d.Send(kind, 0, Origin::Unknown).disposition == Disposition::Dropped, "conservative Unknown native metadata is a drop, not invalid wire data");
		Check(d.broker.Healthy(), "Unknown metadata does not invent transport corruption");
		Check(d.Send(Kind::Commit, 0, Origin::Unknown, "x").disposition == Disposition::Dropped, "Unknown never enters retained");
		d.Echo(d.sequence);
	}
	Driver missing;
	Check(missing.Send(Kind::Commit, 22, Origin::ObservedComposition, "x").disposition == Disposition::Dropped, "observed result requires actual Begin");
	missing.Echo(missing.sequence);
	Driver overlap; overlap.Begin();
	Check(overlap.Send(Kind::Begin, 2, Origin::ObservedComposition).disposition == Disposition::Dropped, "overlapping Begin has no new binding");
	for (auto composition : {1, 2}) {
		Check(overlap.Send(Kind::Commit, composition, Origin::ObservedComposition, "x").disposition == Disposition::Dropped, "neither ambiguous origin can edit");
		overlap.Echo(overlap.sequence);
	}
	for (int variant = 0; variant < 9; ++variant) {
		Driver d; auto record = d.Record(Kind::Commit, 0, Origin::ProvenDirect, "x");
		switch (variant) {
			case 0: record.input.text = std::string("x\0y", 3); break;
			case 1: record.input.text = "\xc0\xaf"; break;
			case 2: record.input.text.assign(TextInputMaxBytes + 1, 'x'); break;
			case 3: record.composition = 1; break;
			case 4: record.input.selectionStart = 0; break;
			case 5: record.origin = static_cast<Origin>(999); break;
			case 6: record.kind = Kind::Result; record.origin = Origin::ObservedComposition; record.composition = 1; break;
			case 7: record.sequence = 0; break;
			case 8: record.kind = static_cast<Kind>(999); break;
		}
		Check(d.Process(record).disposition == Disposition::Failed, "malformed/overbudget record fails closed");
		Check(!d.broker.Healthy() && d.applications == 0 && d.legacyCalls == 0, "malformed text cannot reach any sink");
	}
	Driver boundary;
	Check(boundary.Send(Kind::Commit, 0, Origin::ProvenDirect, std::string(TextInputMaxBytes, 'x')).disposition == Disposition::Retained, "exact codec maximum remains valid");
	boundary.Echo(boundary.sequence);
	Check(boundary.Buffer().State().text.size() == TextInputMaxBytes, "bounded full write is never truncated");
	for (int field = 0; field < 5; ++field) {
		Driver d; auto bad = d.context;
		if (field == 0) bad.editor->allocation = 0;
		if (field == 1) bad.editor->control.assign(TextInputBroker::MaxControlBytes + 1, 'a');
		if (field == 2) bad.editor->control = "unsafe id";
		if (field == 3) bad.nativeWindow = 22;
		if (field == 4) bad.route = static_cast<TextBrokerRoute>(999);
		Check(!d.broker.Synchronize(bad, d.error) && !d.broker.Healthy(), "invalid editor identity cannot authorize input");
	}
}

void BoundsAndRecovery() {
	Driver d(TextUnmarkedPolicy::QualifiedCleanSession); d.Begin();
	d.Send(Kind::Preedit, 1, Origin::ObservedComposition, "old");
	TextNativeRecord poison;
	Check(d.broker.Process(poison).disposition == Disposition::Failed && !d.broker.Healthy(), "native ring failure independently poisons retained delivery");
	Check(d.Send(Kind::Commit, 0, Origin::ProvenDirect, "x").disposition == Disposition::Dropped, "even proven direct is blocked after native failure");
	d.Echo(d.sequence);
	Check(d.broker.Reset(2, d.error), "new provider generation permits explicit recovery protocol");
	d.Sync(d.context);
	Check(d.Send(Kind::Commit, 0, Origin::UnmarkedCharacter, "x").disposition == Disposition::Failed, "Reset requires fresh lifecycle before any further input");
	Check(d.broker.Reset(3, d.error), "failed premature input requires explicit recovery again");
	d.Sync(d.context);
	d.FreshSession(2);
	Check(d.Send(Kind::Commit, 0, Origin::UnmarkedCharacter, "new").disposition == Disposition::Retained, "healthy provider plus fresh session restores qualified admission");
	d.Echo(d.sequence);
	Check(!d.broker.Reset(3, d.error), "provider generation never reused");
	Driver replay;
	auto commit = replay.Record(Kind::Commit, 0, Origin::ProvenDirect, "once");
	replay.Process(commit); replay.Echo(commit.sequence);
	Check(replay.Process(commit).disposition == Disposition::Failed, "replayed canonical sequence never delivers twice");
	Check(replay.Buffer().State().text == "once", "replay retains exact buffer");
	Driver gap; gap.Begin();
	++gap.sequence; // A filtered private marker never reached EventLoop.
	Check(gap.Send(Kind::Commit, 1, Origin::ObservedComposition, "lost").disposition == Disposition::Failed,
		"filtered private marker detected even if native health query stayed true");
	Check(gap.broker.Reset(2, gap.error), "caller reconciles filtered marker failure explicitly");
	gap.Sync(gap.context); gap.FreshSession(2);
	Check(gap.Send(Kind::Commit, 0, Origin::ProvenDirect, "fresh").disposition == Disposition::Retained,
		"only lifecycle recovery bridges discarded native sequences");
	gap.Echo(gap.sequence);
	Driver echoes;
	for (std::size_t i = 0; i < TextInputBroker::MaxEchoes; ++i)
		Check(echoes.Send(Kind::Commit, 0, Origin::ProvenDirect, "", true, true).disposition == Disposition::Retained, "bounded unacknowledged public associations accepted");
	Check(echoes.Send(Kind::Commit, 0, Origin::ProvenDirect, "overflow").disposition == Disposition::Failed, "echo association overflow poisons rather than evicts uncertain text");
	Check(echoes.Buffer().State().text.empty(), "association overflow never partially applies input");
	Driver legacyOverflow;
	legacyOverflow.Sync({TextBrokerRoute::Legacy, 1, 1, {}});
	legacyOverflow.Begin();
	for (std::size_t i = 0; i < TextInputBroker::MaxEchoes + 8; ++i)
		Check(legacyOverflow.Send(Kind::Commit, 1, Origin::ObservedComposition, "x").disposition == Disposition::Legacy, "association exhaustion preserves already admitted valid legacy-origin commit");
	Check(!legacyOverflow.broker.Healthy() && legacyOverflow.legacyCalls == TextInputBroker::MaxEchoes + 8,
		"legacy compatibility and retained provider health remain distinct");
	TextNativeRecord invalidEcho; invalidEcho.kind = Kind::Echo; invalidEcho.echoOf = legacyOverflow.sequence + 100;
	Check(legacyOverflow.broker.Process(invalidEcho).disposition == Disposition::Dropped, "forged public association never becomes legacy text");
	Check(legacyOverflow.legacy.size() == TextInputBroker::MaxEchoes + 8, "echo forgery adds no text");
	legacyOverflow.Sync(Editor(2)); legacyOverflow.Sync({TextBrokerRoute::Legacy, 1, 1, {}});
	Check(legacyOverflow.Send(Kind::Commit, 1, Origin::ObservedComposition, "old").disposition != Disposition::Legacy,
		"provider failure never preserves legacy affinity across owner-route replacement");
	Driver repeated;
	for (int i = 0; i < 1024; ++i) {
		Check(repeated.Send(Kind::Commit, 0, Origin::ProvenDirect, "", true, true).disposition == Disposition::Retained, "echo release permits sustained bounded stream");
		repeated.Echo(repeated.sequence);
	}
	Driver windows;
	for (std::uint64_t id = 2; id <= TextInputBroker::MaxWindows; ++id) {
		auto record = windows.Record(Kind::SessionBegin); record.window = id;
		Check(windows.Process(record).disposition == Disposition::Ignored, "bounded simultaneous native window accepted");
	}
	auto ninth = windows.Record(Kind::SessionBegin); ninth.window = TextInputBroker::MaxWindows + 1;
	Check(windows.Process(ninth).disposition == Disposition::Failed, "window budget fails closed");
	Driver reordered;
	auto laterLifetime = reordered.Record(Kind::SessionBegin); laterLifetime.window = 9;
	Check(reordered.Process(laterLifetime).disposition == Disposition::Ignored, "later allocated window may activate text first");
	auto earlierLifetime = reordered.Record(Kind::SessionBegin); earlierLifetime.window = 4;
	Check(reordered.Process(earlierLifetime).disposition == Disposition::Ignored, "first text observation may arrive in a different order than native lifetime allocation");
	Driver neverText;
	auto closed = neverText.Record(Kind::WindowEnd); closed.window = 77; closed.session = 0;
	Check(neverText.Process(closed).disposition == Disposition::Ignored, "non-text native window lifetime can end without SessionBegin");
	closed.sequence = ++neverText.sequence;
	Check(neverText.Process(closed).disposition == Disposition::Ignored, "duplicate native WindowEnd consumes no extra retired slot");
	auto obsolete = neverText.Record(Kind::SessionBegin); obsolete.window = 77;
	Check(neverText.Process(obsolete).disposition == Disposition::Failed, "ended non-text native window never gains a future text session");
	Driver lifetimes;
	for (std::uint64_t id = 1; id <= 32; ++id) {
		lifetimes.Send(Kind::SessionEnd);
		auto end = lifetimes.Record(Kind::WindowEnd); end.session = 0;
		Check(lifetimes.Process(end).disposition == Disposition::Ignored, "WindowEnd after stopped session releases bounded slot");
		auto next = lifetimes.Record(Kind::SessionBegin); next.window = id + 1;
		Check(lifetimes.Process(next).disposition == Disposition::Ignored, "new never-reused window lifetime can reuse storage");
		lifetimes.context.nativeWindow = id + 1; lifetimes.context.editor->window = id + 1;
		lifetimes.Sync(lifetimes.context);
	}
	auto stale = lifetimes.Record(Kind::SessionBegin); stale.window = 1; stale.session = 999;
	Check(lifetimes.Process(stale).disposition == Disposition::Failed, "old native window cannot be recreated with a different session");
	Driver retiredBound;
	for (std::uint64_t id = 1; id <= TextInputBroker::MaxRetiredWindows; ++id) {
		Check(retiredBound.Send(Kind::WindowEnd).disposition == Disposition::Ignored, "bounded retired lifetime recorded exactly");
		auto next = retiredBound.Record(Kind::SessionBegin); next.window = id + 1;
		Check(retiredBound.Process(next).disposition == Disposition::Ignored, "new lifetime fits current window slots");
		retiredBound.context.nativeWindow = id + 1; retiredBound.context.editor->window = id + 1;
		retiredBound.Sync(retiredBound.context);
	}
	Check(retiredBound.Send(Kind::WindowEnd).disposition == Disposition::Failed,
		"retired identity exhaustion fails closed rather than forgetting old ownership");
}
}
int main() {
	try {
		OrderedOwnership(); WindowsUnmarkedPolicy(); EmptyResultChunksAndNoOps(); IdentityChanges();
		LegacyRouting(); BadReceipts(); UnknownAndMalformed(); BoundsAndRecovery();
		std::cout << "UiTextInputBrokerTest: " << checks << " checks passed\n";
		return EXIT_SUCCESS;
	} catch (const std::exception& error) {
		std::cerr << "UiTextInputBrokerTest after " << checks << " checks: " << error.what() << '\n';
		return EXIT_FAILURE;
	}
}
