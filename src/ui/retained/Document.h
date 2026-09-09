// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#pragma once

#include <array>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <variant>
#include "Vector.h"

namespace openq4::ui {

enum class ValueType { Number, Length, Colour, Keyword, Font, Text, Transform };
// Colours are straight sRGB RGBA, 0..1. Transform is translation x/y,
// scale x/y and rotation in degrees, composed in that order by the renderer.
struct Value {
	ValueType type = ValueType::Number;
	std::array<double, 5> data{};
	std::string unit, text;
	bool CanInterpolate(const Value& other) const;
	Value Interpolate(const Value& other, double fraction) const;
	std::string Css() const;
};
using StateValue = std::variant<double, bool, std::string>;
using StateValues = std::map<std::string, StateValue>;
using PresentationLookup = std::function<bool(const std::string&, int, StateValue&, std::string&)>;
struct StateDeclaration {
	StateValue initial;
	std::string cvar; // Optional read-only host source; empty means application-owned.
};
struct Expression {
	std::string op, state;
	StateValue literal;
	size_t type = 0; // StateValue variant index, resolved by the document compiler.
	unsigned decimals = 0;
	std::vector<Expression> args;
	std::string presentation; // Public alias; legal only in actions and events.
	int component = -1; // Scalar aliases have no component; vectors require one.
	bool inputValue = false; // Invocation-local operand; never application state.
};
// Typed public presentation values are independent of CSS and application
// State(). Boolean values occupy data[0] as 0/1; unused components stay zero.
enum class PresentationType { Number, Boolean, String, Vector2, Vector3, Vector4 };
struct PresentationValue {
	PresentationType type = PresentationType::Number;
	std::array<double,4> data{};
	std::string text;
};
struct PresentationVariable {
	PresentationValue initial;
	std::vector<Expression> expressions;
};
struct PresentationAlias {
	std::string node, property, variable, shown;
};
bool ValidPresentationValue(const PresentationValue& value);
std::string PresentationAliasKey(const std::string& name);
bool ParsePresentationValue(PresentationType type, const std::string& text,
	PresentationValue& value, std::string& error);
std::string FormatPresentationValue(const PresentationValue& value);
// Application operations are typed data. The engine validates the supported
// operation/argument contract before loading a production document and before
// dispatch; the shared model neither interprets commands nor writes CVars.
struct Action {
	std::string operation;
	std::map<std::string, Expression> arguments;
	std::optional<size_t> inputType; // Declared StateValue index; absent for existing actions.
};
struct ActionInvocation {
	std::string action, operation;
	StateValues arguments;
};
enum class EventOp { SetState, SetPresentation, Action, Call, If, PlayTimeline, PauseTimeline, ResumeTimeline, CancelTimeline };
struct EventStep {
	EventOp op = EventOp::SetState;
	std::string target;
	std::map<std::string,Expression> values;
	std::vector<Expression> presentation;
	bool overrideExpression = true;
	Expression condition;
	std::vector<EventStep> thenSteps, elseSteps;
	bool restoreBase = false;
};
struct EventProgram {
	std::string name; // Authored name retained for diagnostics.
	std::vector<EventStep> steps;
};
struct Binding {
	std::string id, node, property;
	Value prototype;
	std::vector<Expression> values;
};
bool ValidProperty(const std::string& name, const Value& value);
bool ValidStateValue(const StateValue& value);
enum class ControlState { Default, Hover, Focus, Pressed, Disabled };
enum class ControlRole { Button, Toggle, Slider, Choice };
struct ToggleSpec {
	std::optional<Expression> mixed;
	std::string checkedPart, mixedPart;
};
struct SliderSpec {
	double minimum = 0, maximum = 1, step = 1;
	unsigned decimals = 0;
	bool vertical = false;
	std::string track, fill, thumb, valueText;
};
struct ChoiceOption {
	std::string id, node, label;
	StateValue value;
	Expression enabled = [] { Expression value; value.literal = true; value.type = 1; return value; }();
	std::string labelPart, selectedPart, highlightPart;
	std::optional<unsigned> labelIndex; // Index in a translated semicolon list; absent means the whole label.
};
struct ChoiceSpec {
	std::string popup, viewport, content, valueText;
	unsigned visibleRows = 8;
	std::vector<ChoiceOption> options;
};
struct Control {
	std::string action, label;
	bool enabled = true;
	std::map<ControlState,std::string> states;
	std::map<std::string,std::string> navigation;
	std::string event; // Case-folded event name; mutually exclusive with action.
	ControlRole role = ControlRole::Button;
	std::optional<Expression> value;
	std::variant<std::monostate, ToggleSpec, SliderSpec, ChoiceSpec> widget;
};
struct ControlReadback {
	StateValue value;
	bool mixed = false;
	std::vector<bool> enabledOptions;
};
struct Node {
	std::string id, type;
	std::map<std::string, Value> properties;
	std::vector<Node> children;
	std::vector<VectorPath> paths;
	// Alpha mask of this completed subtree, in the node's border-box space.
	// An explicitly empty mask hides the subtree; absence leaves it unmasked.
	std::optional<std::vector<VectorPath>> mask;
	std::optional<Control> control;
};
struct Easing {
	double x1 = 0, y1 = 0, x2 = 1, y2 = 1;
	double Evaluate(double fraction) const;
};
struct Keyframe {
	double atMs = 0;
	Value value;
	Easing easing; // Easing of the interval beginning at this key.
};
struct Track {
	std::string node, property;
	std::vector<Keyframe> keys;
};
struct Timeline {
	std::string id;
	double durationMs = 0;
	unsigned iterations = 1; // Zero repeats until cancelled.
	bool essential = false;
	std::vector<Track> tracks;
};
struct DocumentModel {
	std::string id;
	Node root;
	std::map<std::string, Value> tokens;
	std::vector<Timeline> timelines;
	std::map<std::string, StateDeclaration> state;
	std::vector<Binding> bindings;
	std::map<std::string, Action> actions;
	std::map<std::string, PresentationVariable> presentationVariables;
	// Alias keys are ASCII case-folded public names; targets retain exact IDs.
	std::map<std::string, PresentationAlias> aliases;
	std::map<std::string, EventProgram> events; // Case-folded public event names.
	const Node* FindNode(const std::string& id) const;
	// Resolve a compiled descriptor against one supplied state snapshot.
	// No side effects; failure preserves the caller's invocation unchanged.
	bool ResolveAction(const std::string& id, const StateValues& variables,
		ActionInvocation& invocation, std::string& error, const PresentationLookup& presentation = {},
		const StateValue* input = nullptr) const;
};
std::optional<PresentationType> PresentationAliasType(const DocumentModel& model, const std::string& name);
struct Diagnostic {
	std::string pointer, message;
	size_t byte = 0, line = 1, column = 1;
};
bool ParseStateValues(const std::string& source, StateValues& values, std::vector<Diagnostic>& diagnostics);

// Canonical JSONC source is retained verbatim. DOM source offsets make edits
// transactional and preserve comments/extension fields outside the edited span.
// JsonCpp and its DOM do not cross this library boundary.
class Document {
public:
	Document();
	~Document();
	Document(Document&&) noexcept;
	Document& operator=(Document&&) noexcept;
	Document(const Document&) = delete;
	Document& operator=(const Document&) = delete;
	bool Load(const std::string& source, std::vector<Diagnostic>& diagnostics);
	bool ReplaceValue(const std::string& jsonPointer, const std::string& jsonValue,
		std::vector<Diagnostic>& diagnostics);
	const std::string& Source() const;
	const DocumentModel& Model() const;
	// Derived layout only. Text is inserted via Runtime's escaped/localized
	// text API; neither translation strings nor extensions become markup.
	std::string BuildMarkup() const;
private:
	struct Impl;
	std::unique_ptr<Impl> impl;
};

} // namespace openq4::ui
