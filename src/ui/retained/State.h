// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#pragma once
#include "Motion.h"
#include <cstdint>

namespace openq4::ui {

// Evaluate a compiled expression as bounded typed data, without changing its
// variables or the previous result on failure. Unselected branches stay lazy.
bool EvaluateStateExpression(const Expression& expression, const StateValues& variables,
	StateValue& value, std::string& error);

// Application and host batches commit together with all derived properties.
// Evaluating expressions never executes commands or changes authored source.
class State {
public:
	bool Reset(const DocumentModel& model, std::string& error);
	bool Set(const StateValues& changes, std::string& error, bool hostSources = false);
	// Full instance restore evaluates application and current host snapshots in
	// one transaction, without an invalid intermediate binding evaluation.
	bool Restore(const StateValues& application, const StateValues& hostSources, std::string& error);
	const StateValues& Variables() const { return variables; }
	const PropertyValues& Properties() const { return properties; }
	const std::map<std::string,bool>& Enabled() const { return enabled; }
	const std::map<std::string,StateDeclaration>& Declarations() const { return declarations; }
	std::uint64_t Revision() const { return revision; }
private:
	bool Evaluate(const StateValues& candidate, PropertyValues& props, std::map<std::string,bool>& controls, std::string& error) const;
	std::map<std::string,StateDeclaration> declarations;
	std::vector<Binding> bindings;
	StateValues variables;
	PropertyValues properties;
	std::map<std::string,bool> enabled;
	std::uint64_t revision = 0;
};

} // namespace openq4::ui
