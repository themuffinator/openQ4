// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "TextInputBroker.h"
#include <algorithm>
#include <limits>

namespace openq4::ui {
namespace {
bool ValidIdentity(const TextEditorIdentity& id) {
	if (!id.allocation || !id.backend || !id.document || !id.modal || !id.window ||
		!id.session || !id.revision || id.control.empty() ||
		id.control.size() > TextInputBroker::MaxControlBytes) return false;
	return std::all_of(id.control.begin(), id.control.end(), [](unsigned char c) {
		return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
			(c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
	});
}
bool ValidContext(const TextBrokerContext& context) {
	switch (context.route) {
		case TextBrokerRoute::Unavailable:
			return !context.editor && ((!context.nativeWindow && !context.nativeSession) ||
				(context.nativeWindow && context.nativeSession));
		case TextBrokerRoute::Legacy:
			return context.nativeWindow && context.nativeSession && !context.editor;
		case TextBrokerRoute::Retained:
			return context.nativeWindow && context.nativeSession && (!context.editor ||
				(ValidIdentity(*context.editor) && context.editor->window == context.nativeWindow));
	}
	return false;
}
bool SameLease(const TextEditorIdentity& a, const TextEditorIdentity& b) {
	return a.allocation == b.allocation && a.backend == b.backend && a.document == b.document &&
		a.modal == b.modal && a.window == b.window && a.session == b.session && a.control == b.control;
}
bool MetadataOnly(const TextInputEvent& input) {
	return input.kind == TextInputKind::CancelComposition && input.text.empty() &&
		!input.selectionStart && !input.selectionLength;
}
bool ValidRecord(const TextNativeRecord& record, std::string& error) {
	if (record.origin != TextNativeOrigin::Unknown && record.origin != TextNativeOrigin::UnmarkedCharacter &&
		record.origin != TextNativeOrigin::ObservedComposition && record.origin != TextNativeOrigin::ProvenDirect) {
		error = "Invalid native text origin"; return false;
	}
	if (record.kind == TextNativeKind::Echo) {
		if (record.sequence || record.window || record.session || record.composition || !record.echoOf ||
			record.origin != TextNativeOrigin::Unknown || !MetadataOnly(record.input)) {
			error = "Invalid text echo association"; return false;
		}
		return true;
	}
	if (record.kind == TextNativeKind::Poison) {
		if (record.sequence || record.window || record.session || record.composition || record.echoOf ||
			record.origin != TextNativeOrigin::Unknown || !MetadataOnly(record.input)) {
			error = "Invalid native failure record"; return false;
		}
		return true;
	}
	const bool unscopedMetadata = record.origin == TextNativeOrigin::Unknown &&
		(record.kind == TextNativeKind::Begin || record.kind == TextNativeKind::Preedit || record.kind == TextNativeKind::Result ||
		 record.kind == TextNativeKind::End || record.kind == TextNativeKind::Cancel);
	if (!record.sequence || !record.window || (!record.session && record.kind != TextNativeKind::WindowEnd && !unscopedMetadata) || record.echoOf) {
		error = "Native text record lacks an ordered window/session identity"; return false;
	}
	switch (record.kind) {
		case TextNativeKind::Commit:
			if (record.input.kind != TextInputKind::Commit || !ValidateTextInputEvent(record.input, error)) {
				if (error.empty()) error = "Commit requires committed UTF-8";
				return false;
			}
			if ((record.origin == TextNativeOrigin::ObservedComposition && !record.composition) ||
				((record.origin == TextNativeOrigin::ProvenDirect || record.origin == TextNativeOrigin::UnmarkedCharacter) && record.composition)) {
				error = "Commit origin contradicts its composition identity"; return false;
			}
			return true;
		case TextNativeKind::Preedit:
			if (record.input.kind != TextInputKind::Preedit || !ValidateTextInputEvent(record.input, error)) {
				if (error.empty()) error = "Preedit requires normalized UTF-8 offsets";
				return false;
			}
			break;
		case TextNativeKind::Begin: case TextNativeKind::Result:
		case TextNativeKind::End: case TextNativeKind::Cancel:
			if (!MetadataOnly(record.input)) { error = "Native lifecycle/result must not insert text"; return false; }
			break;
		case TextNativeKind::SessionBegin: case TextNativeKind::SessionEnd: case TextNativeKind::WindowEnd:
			if (record.composition || record.origin != TextNativeOrigin::Unknown || !MetadataOnly(record.input)) {
				error = "Invalid native session lifecycle"; return false;
			}
			return true;
		default: error = "Unknown native text record kind"; return false;
	}
	if (record.origin != TextNativeOrigin::Unknown &&
		(record.origin != TextNativeOrigin::ObservedComposition || !record.composition)) {
		error = "Composition record requires observed native provenance"; return false;
	}
	return true;
}
TextBrokerResult Dropped(const char* diagnostic) {
	TextBrokerResult result; result.disposition = TextBrokerDisposition::Dropped;
	result.diagnostic = diagnostic; return result;
}
}

TextInputBroker::TextInputBroker(TextUnmarkedPolicy policy) : policy(policy) {
	if (policy != TextUnmarkedPolicy::Reject && policy != TextUnmarkedPolicy::QualifiedCleanSession) healthy = false;
}
TextInputBroker::Window* TextInputBroker::Find(std::uint64_t id) {
	for (auto& window : windows) if (window.id == id) return &window;
	return nullptr;
}
void TextInputBroker::Retire(Window& window) {
	if (window.composition) window.tainted = true;
	window.binding.reset(); window.legacyOrigin = false;
}
void TextInputBroker::RetireMismatched() {
	for (auto& window : windows) {
		if (window.binding && (context.route != TextBrokerRoute::Retained || !context.editor ||
			context.nativeWindow != window.id || context.nativeSession != window.session ||
			*context.editor != *window.binding)) Retire(window);
		// Existing legacy behavior is kept only while routing remains legacy in
		// this native session. It provides no within-legacy-field identity proof.
		if (window.legacyOrigin && (context.route != TextBrokerRoute::Legacy ||
			context.nativeWindow != window.id || context.nativeSession != window.session)) Retire(window);
	}
}
void TextInputBroker::Poison() {
	healthy = false; pending.reset();
	for (auto& window : windows) if (window.id) {
		// Provider/retained failure must not suppress the established legacy
		// text path. Preserve only a composition already begun in this unchanged
		// legacy route; a retired retained binding can never become one.
		const bool legacy = window.legacyOrigin;
		Retire(window); window.legacyOrigin = legacy;
		window.tainted = true; window.barrierRequired = true;
	}
}
TextBrokerResult TextInputBroker::Fail(const char* diagnostic) {
	Poison(); TextBrokerResult result; result.disposition = TextBrokerDisposition::Failed;
	result.diagnostic = diagnostic; return result;
}
bool TextInputBroker::Synchronize(const TextBrokerContext& value, std::string& error) {
	if (pending) { Poison(); error = "Editor synchronization interrupted a pending text delivery"; return false; }
	if (!ValidContext(value)) { Poison(); error = "Invalid text editor context"; return false; }
	context = value; RetireMismatched(); error.clear(); return true;
}
bool TextInputBroker::Reset(std::uint64_t generation, std::string& error) {
	if (pending) { Poison(); error = "Cannot reset a pending text delivery"; return false; }
	if (generation <= providerGeneration) { Poison(); error = "Text provider generation must advance"; return false; }
	Poison(); providerGeneration = generation; healthy = true;
	context = {}; RetireMismatched(); echoes.fill(0); sequenceBoundary = true; error.clear(); return true;
}
bool TextInputBroker::RememberCommit(std::uint64_t sequence) {
	for (auto& echo : echoes) if (!echo) { echo = sequence; return true; }
	return false;
}
TextBrokerResult TextInputBroker::Deliver(const TextNativeRecord& record, const TextInputEvent& input,
	const TextEditorIdentity& target, std::uint64_t composition) {
	if (nextDelivery == (std::numeric_limits<std::uint64_t>::max)()) return Fail("Text delivery identity exhausted");
	TextBrokerResult result;
	result.disposition = TextBrokerDisposition::Retained;
	result.delivery = TextBrokerDelivery{++nextDelivery, record.sequence, target, input};
	pending = PendingDelivery{nextDelivery, record.window, composition, context};
	return result;
}

TextBrokerResult TextInputBroker::Process(const TextNativeRecord& record) {
	if (pending) return Fail("A new record interrupted a pending text delivery");
	std::string error;
	if (!ValidRecord(record, error)) return Fail(error.c_str());
	if (record.kind == TextNativeKind::Poison) return Fail("Native text stream reported a failure or overflow");
	if (record.kind == TextNativeKind::Echo) {
		for (auto& echo : echoes) if (echo == record.echoOf) { echo = 0; return {}; }
		// Unknown/replayed associations are consumed, never a legacy fallback or
		// newly authorized commit. No text comparison can establish an association.
		return Dropped("Unknown or already consumed canonical text association");
	}
	if (record.sequence <= lastSequence) return Fail("Native text sequence regressed or repeated");
	if (sequenceBoundary) {
		if (record.kind != TextNativeKind::SessionBegin && record.kind != TextNativeKind::SessionEnd && record.kind != TextNativeKind::WindowEnd)
			return Fail("Text recovery requires an ordered native lifecycle boundary");
		sequenceBoundary = false;
	} else if (lastSequence && record.sequence != lastSequence + 1) {
		return Fail("Native text sequence has a missing record");
	}
	lastSequence = record.sequence;
	Window* window = Find(record.window);
	if (record.kind == TextNativeKind::SessionBegin) {
		if (!window) {
			if (std::find(retiredWindows.begin(), retiredWindows.end(), record.window) != retiredWindows.end())
				return Fail("Native window lifetime was reused");
			for (auto& slot : windows) if (!slot.id) { window = &slot; break; }
			if (!window) return Fail("Native text window budget exceeded");
			window->id = record.window;
		}
		if (window->open || record.session <= window->session) return Fail("Native text session lacks an end/begin barrier");
		window->session = record.session; window->open = true; window->composition = 0;
		window->binding.reset(); window->legacyOrigin = false;
		window->tainted = false; window->barrierRequired = false;
		return {};
	}
	if (record.kind == TextNativeKind::WindowEnd) {
		if (window && record.session && record.session != window->session) return Dropped("Native window session is obsolete");
		if (std::find(retiredWindows.begin(), retiredWindows.end(), record.window) != retiredWindows.end()) return {};
		auto retired = std::find(retiredWindows.begin(), retiredWindows.end(), 0);
		if (retired == retiredWindows.end()) return Fail("Retired native window budget exceeded");
		// A window may be destroyed without ever starting text input. Its native
		// lifetime is still retired, even though no SessionBegin created a slot.
		*retired = record.window;
		if (window) { Retire(*window); *window = {}; }
		return {};
	}
	if (!window || !window->open || record.session != window->session) return Dropped("Native text window/session is unavailable or obsolete");
	if (record.kind == TextNativeKind::SessionEnd) {
		Retire(*window); window->composition = 0; window->open = false; return {};
	}
	const bool currentSession = context.nativeWindow == record.window && context.nativeSession == record.session;
	if (record.kind != TextNativeKind::Commit && record.origin == TextNativeOrigin::Unknown) {
		Retire(*window); window->tainted = true;
		// Unknown End/Cancel cannot close an unrelated newer known composition.
		// A fresh native session is the conservative recovery boundary.
		window->barrierRequired = true;
		return Dropped("Native composition metadata has no trusted provenance");
	}
	if (record.kind == TextNativeKind::Begin) {
		if (record.composition <= window->lastComposition) {
			Retire(*window); return Dropped("Native composition identity was reused");
		}
		const bool overlap = window->composition != 0;
		Retire(*window); window->composition = record.composition; window->lastComposition = record.composition;
		if (overlap) return Dropped("Overlapping native composition is ambiguous");
		if (healthy && !window->barrierRequired && currentSession) {
			if (context.route == TextBrokerRoute::Retained && context.editor) window->binding = context.editor;
			else if (context.route == TextBrokerRoute::Legacy) window->legacyOrigin = true;
		}
		return {};
	}
	if (record.kind == TextNativeKind::Commit) {
		const bool legacyOrigin = !record.composition || (record.composition == window->composition && window->legacyOrigin);
		const bool legacy = currentSession && context.route == TextBrokerRoute::Legacy && legacyOrigin;
		const bool remembered = RememberCommit(record.sequence);
		if (!remembered) Poison();
		// Uncertainty belongs to the native session, even while no retained
		// editor is eligible. Focusing a field later must not wash away a missing
		// Begin or unproven result received while a button/console owned input.
		if (record.origin == TextNativeOrigin::Unknown) {
			window->tainted = true;
			if (window->binding) Retire(*window);
		} else if (record.origin == TextNativeOrigin::ObservedComposition &&
			(record.composition != window->composition || (!window->binding && !window->legacyOrigin))) {
			Retire(*window); window->tainted = true;
		}
		// Known retained-owned, retired, missing or ended composition never moves
		// into a newly focused legacy sink. Unknown records with a nonzero origin
		// are equally unable to shed that origin via a fallback.
		if (legacy) {
			TextBrokerResult result; result.disposition = TextBrokerDisposition::Legacy; result.legacy = record.input;
			if (!remembered) result.diagnostic = "Text association budget exceeded; retained delivery disabled";
			return result;
		}
		if (!remembered) return Fail("Text association budget exceeded");
		if (!healthy || window->barrierRequired || !currentSession || context.route != TextBrokerRoute::Retained || !context.editor)
			return Dropped("No authorized retained text editor");
		if (record.origin == TextNativeOrigin::ObservedComposition) {
			if (record.composition != window->composition || !window->binding || *window->binding != *context.editor) {
				Retire(*window); window->tainted = true;
				return Dropped("Composition no longer belongs to this editor");
			}
			return Deliver(record, record.input, *window->binding, record.composition);
		}
		if (record.origin == TextNativeOrigin::Unknown) { Retire(*window); window->tainted = true; return Dropped("Unproven native text origin"); }
		if (record.origin == TextNativeOrigin::UnmarkedCharacter &&
			(policy != TextUnmarkedPolicy::QualifiedCleanSession || window->tainted || window->composition))
			return Dropped("Unmarked character is not qualified in this native session");
		// Proven direct input is independent of old native composition affinity.
		// It may retire that affinity but can never transfer it to the new owner.
		Retire(*window);
		return Deliver(record, record.input, *context.editor, 0);
	}
	if (record.composition != window->composition) {
		window->tainted = true; return Dropped("Native composition is missing or obsolete");
	}
	if (record.kind == TextNativeKind::End) {
		window->binding.reset(); window->legacyOrigin = false; window->composition = 0; return {};
	}
	if (record.kind == TextNativeKind::Result) return {}; // Metadata only, affinity remains.
	if (record.kind == TextNativeKind::Cancel) {
		auto target = window->binding;
		Retire(*window); window->composition = 0;
		if (healthy && !window->barrierRequired && currentSession && context.editor && target && *target == *context.editor) {
			TextInputEvent cancel; MakeTextInputCancel(cancel);
			return Deliver(record, cancel, *target, 0);
		}
		return Dropped("Canceled native composition has no eligible original editor");
	}
	if (!healthy || window->barrierRequired || !currentSession || !context.editor || !window->binding || *window->binding != *context.editor)
		return Dropped("Preedit no longer belongs to an eligible original editor");
	return Deliver(record, record.input, *window->binding, record.composition);
}

bool TextInputBroker::Complete(std::uint64_t token, TextDeliveryOutcome outcome,
	const TextBrokerContext& after, std::string& error) {
	if (!pending || token != pending->token || !ValidContext(after) || context != pending->before || !context.editor) {
		Poison(); error = "Text delivery receipt does not match the pending owner"; return false;
	}
	bool valid = false;
	if (outcome == TextDeliveryOutcome::AppliedChanged) {
		valid = after.route == context.route && after.nativeWindow == context.nativeWindow &&
			after.nativeSession == context.nativeSession && after.editor && SameLease(*after.editor, *context.editor) &&
			after.editor->revision > context.editor->revision;
	} else if (outcome == TextDeliveryOutcome::AppliedNoChange || outcome == TextDeliveryOutcome::Rejected) {
		valid = after == context;
	}
	if (!valid) { Poison(); error = "Text delivery receipt changed an owner or contradicted its mutation outcome"; return false; }
	Window* window = Find(pending->window);
	if (pending->composition && (!window || window->composition != pending->composition ||
		!window->binding || *window->binding != *context.editor)) {
		Poison(); error = "Text delivery composition was retired before receipt"; return false;
	}
	if (outcome == TextDeliveryOutcome::Rejected) {
		if (window) { Retire(*window); window->tainted = true; }
	} else if (pending->composition) window->binding = after.editor;
	context = after; pending.reset(); error.clear(); return true;
}

} // namespace openq4::ui
