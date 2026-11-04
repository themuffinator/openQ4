// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#pragma once

#include <array>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <variant>
#include "Vector.h"
#include "ModelScene.h"

namespace openq4::ui {

enum class ValueType { Number, Length, Colour, Keyword, Font, Text, Transform, Image };
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
	// Optional read-only view source: "aspect" is the window safe area's width
	// over its height (the view's, without insets), "text" the text size it
	// applies (1 to 2) and "safeLeft", "safeTop", "safeRight" and "safeBottom"
	// the safe area's insets from the view's edges in dp, all refreshed as the
	// runtime lays the document out.
	std::string view;
	// Application string refreshed by the public GUI SetKeyBindingNames call.
	// This is a binding lookup, never a command to execute. Applications can
	// temporarily replace it (for example, with the key-capture instruction).
	std::string keyBinding;
	bool HostSourced() const { return !cvar.empty() || !view.empty(); }
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
inline Expression ModelNumber(double value) { Expression result; result.literal=value; return result; }
inline Expression ModelText() { Expression result; result.literal=std::string(); result.type=2; return result; }
template<std::size_t N>
std::array<Expression,N> ModelNumbers(const std::array<double,N>& values) {
    std::array<Expression,N> result;
    for (std::size_t i=0;i<N;++i) result[i]=ModelNumber(values[i]);
    return result;
}
struct ModelPartSpec {
    Expression model=ModelText(), skin=ModelText(), animation=ModelText(), animationClass=ModelText();
    std::string joint;
};
struct ModelLightSpec {
    std::array<Expression,3> origin=ModelNumbers<3>({-128,0,0}), colour=ModelNumbers<3>({1,1,1});
};
struct ModelSceneSpec {
    std::vector<ModelPartSpec> parts;
    std::vector<ModelLightSpec> lights;
    std::array<Expression,3> origin{}, rotation{}, camera=ModelNumbers<3>({-128,0,0});
    std::array<Expression,4> outline{}, rimlight{}, brightSkin{};
    Expression fov=ModelNumber(90), outlineWidth=ModelNumber(2);
};
bool ResolveModelScene(const ModelSceneSpec&, const StateValues&, ModelScene&, std::string& error);
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
enum class EventOp { SetState, SetPresentation, Action, Call, If, PlayTimeline, PauseTimeline, ResumeTimeline, CancelTimeline, Focus };
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
// Image sources are engine VFS image names: empty (no picture) or a relative
// path of letters, digits, '_', '-', '.' and '/', without '..' or '//'.
bool ValidImageSource(const std::string& source);
// RmlUi backdrop-filter for a node's soft focus: `backdrop-blur` (a dp/px
// sigma) and `backdrop-saturate` (0..1), or its speed streak `backdrop-streak`
// (0..1, The Awakening's speeder blur); "none" when none applies.
std::string BackdropFilterCss(const Value* blur, const Value* saturate, const Value* streak = nullptr);
enum class ControlState { Default, Hover, Focus, Pressed, Disabled };
enum class ControlRole { Button, Toggle, Slider, Choice, Number, Scrollbar };
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
	// A declared string state whose value is the option's text: a display's
	// name, a formatted mode. Like all retained text, a #str_ key in it reads
	// in the player's language, so the application keeps untrusted names from
	// starting with one. Empty means the authored #str label and list index.
	std::string labelState;
	StateValue value;
	Expression enabled = [] { Expression value; value.literal = true; value.type = 1; return value; }();
	// An option whose availability can change. While it is unavailable its
	// label, when it has an authored colour, reads at the disabled-text
	// strength, and the runtime owns that colour.
	bool Conditional() const {
		return !enabled.op.empty() || !enabled.state.empty() ||
			!(std::holds_alternative<bool>(enabled.literal) && std::get<bool>(enabled.literal));
	}
	std::string labelPart, selectedPart, highlightPart;
	std::optional<unsigned> labelIndex; // Index in a translated semicolon list; absent means the whole label.
};
struct ScrollSpec {
    std::string viewport, track, thumb;
    bool vertical = true;
    double lineStep = 36, minimumThumb = 36; // Authored dp, converted once by layout.
};
struct ChoiceSpec {
	std::string popup, viewport, content, valueText;
	unsigned visibleRows = 8;
	std::vector<ChoiceOption> options;
	std::optional<ScrollSpec> scrollbar; // Local popup artwork; never a nested semantic control.
	std::string placementBounds; // Optional canonical proper ancestor; empty retains window placement.
	// How many options lead the list. The rest are hidden, ineligible and
	// unmeasured, so one authored list carries a catalog of varying length.
	// Absent means every option.
	std::optional<Expression> optionCount;
};
struct NumberSpec {
	double minimum = 0, maximum = 1;
	bool exponent = true;
	bool integer = false; // New proposals must represent an exact decimal integer.
	int decimals = -1;    // New proposals have at most this many decimal places; -1 has no limit.
	// A text field (role "text"): the same editor, but its value is the
	// draft's text itself, a non-empty single line within maxBytes, and it
	// has no numeric bounds or syntax.
	bool textField = false;
	unsigned maxBytes = 1024;
	// Text fields only. `allowEmpty` makes an empty text a value. A `masked`
	// field (a password) presents one '*' per character, never hands its text
	// to the clipboard or the native text services, and otherwise edits as any
	// text field; its text is still its value, in document state and snapshots.
	bool allowEmpty = false, masked = false;
	// A positioned/clipped viewport owns direct absolute text and paint parts.
	// Their transform is shared through the viewport; validation stays outside.
	std::string viewport, text, selection, caret, composition, validation;
};
struct Control {
	std::string action, label;
	bool enabled = true;
	std::map<ControlState,std::string> states;
	std::map<std::string,std::string> navigation;
	std::string event; // Case-folded event name; mutually exclusive with action.
	ControlRole role = ControlRole::Button;
	std::optional<Expression> value;
	std::variant<std::monostate, ToggleSpec, SliderSpec, ChoiceSpec, NumberSpec, ScrollSpec> widget;
	// Buttons only: the detail this control names (DocumentModel::detail).
	std::optional<double> detail;
	// Buttons and choices may name the current subject/catalog of an activation. A changed context
	// cancels a held press and invalidates queued work, even if it changes back.
	std::optional<Expression> context;
	// Buttons may name the verb the prompt bar shows for Accept while they hold
	// focus (section 13.4): a localization key, or a string expression whose
	// result is one. Without it the bar keeps its own verb.
	std::optional<Expression> prompt;
};
// A document's detail card (section 7's hover card). While the pointer moved
// last, the control it is over is the subject; otherwise the focused control
// is. The runtime writes the subject's detail, or `none` without one, to the
// numeric `state` and places `card` beside the subject: 6 dp from the
// pointer, its trailing edge 28 dp past the pointer once the pointer is more
// than 555 dp into the card's containing block; 6 dp under a focused control,
// over it when the card would cross the view's edge; always 16 dp inside the
// view. The card holds no controls and the runtime owns its left and top.
// Without a card (empty) only the state follows the subject: the document's
// bindings show what it names in place, as a page section's summary does.
struct DetailSpec {
	std::string state, card;
	double none = -1;
};
struct ControlReadback {
	StateValue value;
	bool mixed = false;
	std::vector<bool> enabledOptions;
	// Choices only: no labels, or each option's published text (empty for an
	// authored label), and how many options lead the list, clamped to the
	// options. An absent count means every option.
	std::vector<std::string> optionLabels;
	std::optional<std::size_t> optionCount;
	std::optional<std::string> context; // Choice catalog identity, independent of its selected value.
	bool operator==(const ControlReadback&) const = default;
};
struct ModalSpec {
	std::string initialFocus;
	std::string backEvent; // Empty consumes Back; otherwise a canonical event name.
};
struct Node {
	// Use the original 640x480 GUI text layout inside this fixed text window.
	bool legacyText = false, legacyTextClip = false;
	std::string id, type;
	std::map<std::string, Value> properties;
	std::vector<Node> children;
	std::vector<VectorPath> paths;
	// Alpha mask of this completed subtree, in the node's border-box space.
	// An explicitly empty mask hides the subtree; absence leaves it unmasked.
	std::optional<std::vector<VectorPath>> mask;
	std::optional<Control> control;
	std::optional<ModalSpec> modal;
	std::optional<ModelSceneSpec> model;
};
// UV translation and centre scaling use normalized texture coordinates.
bool HasImageCoordinates(const Node& node);
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
	// Case-folded event run when this timeline plays to its end; empty for none.
	// The program cannot invoke actions or lead back into its own completion.
	std::string complete;
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
	std::optional<DetailSpec> detail;
	// Reference height in dp of a view-height canvas. A stock-composition screen
	// (title, pause, loading) lays its 4:3 canvas out at this height and scales
	// with the view's height; zero keeps display-density layout.
	double canvasHeight = 0;
	// Optional minimum width: narrow views fit the center screen to the width
	// and expose more vertical space instead of clipping the authored canvas.
	double canvasMinWidth = 0;
	// The root `extensions` object as compact JSON, so a tool reads its own
	// vendor data from the loaded model instead of parsing the source again;
	// empty when the document has none.
	std::string extensions;
	const Node* FindNode(const std::string& id) const;
	// Index the tree's node IDs so FindNode answers without a search. Only a
	// tree that no longer changes may be indexed (Document indexes the model
	// it validated); a copy or move starts unindexed and FindNode searches.
	void IndexNodes();
	struct NodeIndex {
		std::unordered_map<std::string_view, const Node*> nodes;
		NodeIndex() = default;
		NodeIndex(const NodeIndex&) {}
		NodeIndex(NodeIndex&&) noexcept {}
		NodeIndex& operator=(const NodeIndex&) { nodes.clear(); return *this; }
		NodeIndex& operator=(NodeIndex&&) noexcept { nodes.clear(); return *this; }
	} nodeIndex;
	// Resolve a compiled descriptor against one supplied state snapshot.
	// No side effects; failure preserves the caller's invocation unchanged.
	bool ResolveAction(const std::string& id, const StateValues& variables,
		ActionInvocation& invocation, std::string& error, const PresentationLookup& presentation = {},
		const StateValue* input = nullptr) const;
};
std::optional<PresentationType> PresentationAliasType(const DocumentModel& model, const std::string& name);
// RmlUi decorator for an image node's source with its authored fit/alignment.
std::string ImageDecorator(const Node& node, const std::string& source);
struct Diagnostic {
	std::string pointer, message;
	size_t byte = 0, line = 1, column = 1;
};
bool ParseStateValues(const std::string& source, StateValues& values, std::vector<Diagnostic>& diagnostics);

class State;
// Canonical JSONC source is retained verbatim. DOM source offsets make edits
// transactional and preserve comments/extension fields outside the edited span.
// JsonCpp and its DOM do not cross this library boundary.
// A load or an edit replaces the parsed document and never changes it, so a
// copy shares it: a GUI and its view validate one document once.
class Document {
public:
	Document();
	~Document();
	Document(Document&&) noexcept;
	Document& operator=(Document&&) noexcept;
	Document(const Document&) = default;
	Document& operator=(const Document&) = default;
	bool Load(const std::string& source, std::vector<Diagnostic>& diagnostics);
	bool ReplaceValue(const std::string& jsonPointer, const std::string& jsonValue,
		std::vector<Diagnostic>& diagnostics);
	const std::string& Source() const;
	const DocumentModel& Model() const;
	// The model with its document's lifetime: a runtime part that keeps it
	// references the loaded document instead of copying its declarations.
	std::shared_ptr<const DocumentModel> SharedModel() const;
	// The document's initial state as the load evaluated and validated it,
	// shared by copies: a runtime starts from a copy of it instead of
	// evaluating every binding again. Empty for a document never loaded.
	const State& InitialState() const;
	// Derived layout only. Text is inserted via Runtime's escaped/localized
	// text API; neither translation strings nor extensions become markup.
	std::string BuildMarkup() const;
private:
	struct Impl;
	std::shared_ptr<Impl> impl;
	std::shared_ptr<const State> initialState;
};

} // namespace openq4::ui
