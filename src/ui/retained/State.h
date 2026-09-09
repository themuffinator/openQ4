// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#pragma once
#include "Motion.h"
#include <cstdint>

namespace openq4::ui {

// Evaluate a compiled expression as bounded typed data, without changing its
// variables or the previous result on failure. Unselected branches stay lazy.
bool EvaluateStateExpression(const Expression& expression, const StateValues& variables,
	StateValue& value, std::string& error, const PresentationLookup& presentation = {},
	const StateValue* input = nullptr);

struct PresentationCell {
	PresentationValue value;
	bool expressionDisabled = false;
	bool pending = false; // Transient write awaiting the next expression evaluation.
};
struct PresentationPropertyOverride {
	Value value;
	bool expressionDisabled = false;
};
struct StatePresentationSnapshot {
	std::map<std::string,PresentationCell> variables;
	std::map<PropertyKey,PresentationPropertyOverride> properties;
};

// Application and host batches commit together with all derived properties.
// Evaluating expressions never executes commands or changes authored source.
class State {
public:
	bool Reset(const DocumentModel& model, std::string& error);
	bool Set(const StateValues& changes, std::string& error, bool hostSources = false);
	// Event entry refreshes pending application and current host values in one
	// evaluation; neither batch can expose an invalid intermediate binding.
	bool SetCombined(const StateValues& application, const StateValues& hostSources, std::string& error);
	// Full instance restore evaluates application and current host snapshots in
	// one transaction, without an invalid intermediate binding evaluation.
	bool Restore(const StateValues& application, const StateValues& hostSources, std::string& error,
		const StatePresentationSnapshot* presentation = nullptr);
	bool WritePresentationVariable(const std::string& id, const PresentationValue& value,
		bool overrideExpression, std::string& error);
	// All members must be binding-owned properties. Stage multi-property writes
	// together; overrideExpression=false never revives a disabled expression.
	bool OverrideProperties(const PropertyValues& values, bool overrideExpression, std::string& error);
	const StatePresentationSnapshot& Presentation() const { return presentation; }
	const StateValues& Variables() const { return variables; }
	const PropertyValues& Properties() const { return properties; }
	const std::map<std::string,bool>& Enabled() const { return enabled; }
	const std::map<std::string,ControlReadback>& ControlValues() const { return controlValues; }
	const std::map<std::string,StateDeclaration>& Declarations() const { return declarations; }
	std::uint64_t Revision() const { return revision; }
private:
	bool Evaluate(const StateValues& candidate, PropertyValues& props, std::map<std::string,bool>& controls,
		StatePresentationSnapshot& output, std::map<std::string,ControlReadback>& readbacks, std::string& error) const;
	std::map<std::string,StateDeclaration> declarations;
	std::vector<Binding> bindings;
	std::map<std::string,PresentationVariable> presentationDeclarations;
	StatePresentationSnapshot presentation;
	StateValues variables;
	PropertyValues properties;
	std::map<std::string,bool> enabled;
	std::map<std::string,Control> controlDeclarations;
	std::map<std::string,ControlReadback> controlValues;
	std::uint64_t revision = 0;
	bool presentationDirty = false;
};

} // namespace openq4::ui
