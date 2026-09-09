// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#pragma once
#include "State.h"

namespace openq4::ui {

// Validation is pure: no host writes, dispatch, callbacks into the runtime or
// retained references to the candidate invocation. Host effects happen only
// after a successful event transaction is published by the owning adapter.
using ActionValidator = std::function<bool(const ActionInvocation&, std::string&)>;
struct EventResult {
	State state;
	Motion motion;
	StateValues stateChanges; // Final values of explicitly written application keys.
	std::vector<ActionInvocation> actions;
};

bool ReadPresentationAlias(const DocumentModel& model, const State& state, const Motion& motion,
	const std::string& name, PresentationValue& value);
bool WritePresentationAlias(const DocumentModel& model, State& state, Motion& motion,
	const std::string& name, const PresentationValue& value, bool overrideExpression, std::string& error);
// The returned lookup borrows its arguments; use it only during synchronous
// expression evaluation while those objects remain alive and at stable addresses.
PresentationLookup MakePresentationLookup(const DocumentModel& model, const State& state, const Motion& motion);

// Execute ordered local changes and capture application arguments at their
// instruction's state snapshot. Nested events run inline. Failed evaluation,
// budgets or validation leave both inputs and the previous output unchanged.
// No drawing, device access, application dispatch or event replay occurs here.
bool EvaluateEvent(const DocumentModel& model, const State& state, const Motion& motion,
	const std::string& name, double seconds, EventResult& result, std::string& error,
	const ActionValidator& validate = {}, size_t maxActions = 256);

} // namespace openq4::ui
