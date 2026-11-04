// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "State.h"
#include "FloatChars.h"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <stdexcept>

namespace openq4::ui {
namespace {
// A state variable read: a pending change (the overlay) before the committed value.
const StateValue* LookupState(const StateValues& variables, const StateValues* overlay, const std::string& name) {
	if (overlay) {
		const auto changed = overlay->find(name);
		if (changed != overlay->end()) return &changed->second;
	}
	const auto found = variables.find(name);
	return found == variables.end() ? nullptr : &found->second;
}
StateValue EvaluateExpression(const Expression& e, const StateValues& variables, const PresentationLookup& presentation = {},
	const StateValue* input = nullptr, const StateValues* overlay = nullptr) {
	if (e.inputValue) {
		if (!e.op.empty() || !e.state.empty() || !e.presentation.empty() || !e.args.empty() ||
			!input || input->index() != e.type || !ValidStateValue(*input))
			throw std::runtime_error("Missing or invalid invocation-local input");
		return *input;
	}
	if (e.op.empty()) {
		if (!e.presentation.empty()) {
			StateValue value; std::string error;
			if (!presentation || !presentation(e.presentation,e.component,value,error))
				throw std::runtime_error(error.empty() ? "Unavailable presentation alias '"+e.presentation+"'" : error);
			if (value.index() != e.type || !ValidStateValue(value))
				throw std::runtime_error("Invalid presentation type/value for '"+e.presentation+"'");
			return value;
		}
		const StateValue* value = &e.literal;
		if (!e.state.empty()) {
			value = LookupState(variables,overlay,e.state);
			if (!value) throw std::runtime_error("Missing state variable '"+e.state+"'");
		}
		if (value->index() != e.type || !ValidStateValue(*value))
			throw std::runtime_error(e.state.empty() ? "Invalid compiled expression literal" : "Invalid state type/value for '"+e.state+"'");
		return *value;
	}
	auto arg = [&](size_t i) { return EvaluateExpression(e.args.at(i),variables,presentation,input,overlay); };
	auto number = [&](size_t i) { return std::get<double>(arg(i)); };
	auto boolean = [&](size_t i) { return std::get<bool>(arg(i)); };
	// Selection and boolean operations short-circuit. An unused branch cannot
	// invalidate an otherwise valid state with e.g. division by zero.
	if (e.op == "select") return arg(boolean(0) ? 1 : 2);
	if (e.op == "&&") return boolean(0) && boolean(1);
	if (e.op == "||") return boolean(0) || boolean(1);
	if (e.op == "!") return !boolean(0);
	if (e.op == "==") return arg(0) == arg(1);
	if (e.op == "!=") return arg(0) != arg(1);
	if (e.op == "<") return number(0) < number(1);
	if (e.op == "<=") return number(0) <= number(1);
	if (e.op == ">") return number(0) > number(1);
	if (e.op == ">=") return number(0) >= number(1);
	if (e.op == "numberText") {
		char buffer[96];
		const auto result = FloatToChars(buffer,buffer+sizeof(buffer),number(0),std::chars_format::fixed,e.decimals);
		if (result.ec != std::errc{}) throw std::runtime_error("Number text exceeds the supported range");
		return std::string(buffer,result.ptr);
	}
	double value = 0;
	if (e.op == "+") value = number(0)+number(1);
	else if (e.op == "-") value = number(0)-number(1);
	else if (e.op == "*") value = number(0)*number(1);
	else if (e.op == "/" || e.op == "%") {
		const double divisor = number(1);
		if (divisor == 0) throw std::runtime_error("Division or remainder by zero");
		value = e.op == "/" ? number(0)/divisor : std::fmod(number(0),divisor);
	} else if (e.op == "min") value = std::min(number(0),number(1));
	else if (e.op == "max") value = std::max(number(0),number(1));
	else if (e.op == "abs") value = std::abs(number(0));
	else if (e.op == "floor") value = std::floor(number(0));
	else if (e.op == "ceil") value = std::ceil(number(0));
	else if (e.op == "round") value = std::round(number(0));
	else if (e.op == "clamp") {
		const double low = number(1), high = number(2);
		if (low > high) throw std::runtime_error("Clamp bounds are reversed");
		value = std::clamp(number(0),low,high);
	} else throw std::runtime_error("Unknown compiled expression operation");
	if (!std::isfinite(value)) throw std::runtime_error("Expression produced a non-finite number");
	return value;
}
bool EvaluateTyped(const Expression& expression, const StateValues& variables, const StateValues* overlay, StateValue& value,
	std::string& error) {
	error.clear();
	try {
		StateValue candidate = EvaluateExpression(expression,variables,{},nullptr,overlay);
		if (candidate.index() != expression.type || !ValidStateValue(candidate))
			throw std::runtime_error("Expression result violates its compiled type or supported value range");
		value = std::move(candidate); return true;
	} catch (const std::exception& problem) {
		error = problem.what(); return false;
	}
}
void CollectStateReads(const Expression& expression, std::vector<std::string>& names) {
	if (!expression.state.empty()) names.push_back(expression.state);
	for (const auto& arg : expression.args) CollectStateReads(arg,names);
}
bool SameValue(const Value& a, const Value& b) {
	return a.type == b.type && a.data == b.data && a.unit == b.unit && a.text == b.text;
}
}
bool EvaluateStateExpression(const Expression& expression, const StateValues& variables, StateValue& value, std::string& error,
	const PresentationLookup& presentation, const StateValue* input) {
	error.clear();
	try {
		StateValue candidate = EvaluateExpression(expression,variables,presentation,input);
		if (candidate.index() != expression.type || !ValidStateValue(candidate))
			throw std::runtime_error("Expression result violates its compiled type or supported value range");
		value = std::move(candidate); return true;
	} catch (const std::exception& problem) {
		error = problem.what(); return false;
	}
}
bool DocumentModel::ResolveAction(const std::string& id, const StateValues& variables, ActionInvocation& invocation, std::string& error,
	const PresentationLookup& presentation, const StateValue* input) const {
	error.clear();
	const auto found = actions.find(id);
	if (found == actions.end()) { error = "Unknown action descriptor '"+id+"'"; return false; }
	const auto type = found->second.inputType;
	if (type ? (!input || *type > 2 || input->index() != *type || !ValidStateValue(*input)) : input != nullptr) {
		error = "Action '"+id+"' requires exactly its declared invocation input"; return false;
	}
	ActionInvocation candidate; candidate.action = id; candidate.operation = found->second.operation;
	for (const auto& [name,expression] : found->second.arguments) {
		StateValue value;
		if (!EvaluateStateExpression(expression,variables,value,error,presentation,input)) {
			error = "Action '"+id+"' argument '"+name+"': "+error; return false;
		}
		candidate.arguments.emplace(name,std::move(value));
	}
	invocation = std::move(candidate); return true;
}
bool State::Evaluate(const StateValues& candidate, PropertyValues& props, std::map<std::string,bool>& controls,
	StatePresentationSnapshot& output, std::map<std::string,ControlReadback>& readbacks, std::string& error) const {
	output = presentation;
	for (const auto& binding : *bindings) {
		try {
			if (binding.property == "enabled") {
				controls[binding.node] = std::get<bool>(EvaluateExpression(binding.values.front(),candidate)); continue;
			}
			const PropertyKey key{binding.node,binding.property};
			const auto override = presentation.properties.find(key);
			if (override != presentation.properties.end() && override->second.expressionDisabled) {
				props[key] = override->second.value; continue;
			}
			Value value = binding.prototype;
			for (size_t i = 0; i < binding.values.size(); ++i) {
				const auto result = EvaluateExpression(binding.values[i],candidate);
				if (result.index() == 2) value.text = std::get<std::string>(result);
				else value.data[i] = std::get<double>(result);
			}
			if (!ValidProperty(binding.property,value)) throw std::runtime_error("Result violates the target property's type or range");
			props[key] = std::move(value);
			output.properties.erase(key); // Successful expression evaluation replaces a transient write.
		} catch (const std::exception& problem) {
			error = "Binding '"+binding.id+"' ("+binding.node+"."+binding.property+"): "+problem.what(); return false;
		}
	}
	return EvaluateDerived(candidate,nullptr,output,readbacks,error);
}
bool State::EvaluateDerived(const StateValues& base, const StateValues* overlay, StatePresentationSnapshot& output,
	std::map<std::string,ControlReadback>& readbacks, std::string& error) const {
	if (!modelDeclarations->empty()) {
		StateValues combined;
		const StateValues* values=&base;
		if (overlay) {
			combined=base;
			for (const auto& [id,value]:*overlay) combined[id]=value;
			values=&combined;
		}
		for (const auto* spec:*modelDeclarations) {
			ModelScene resolved;
			if (!ResolveModelScene(*spec,*values,resolved,error)) return false;
		}
	}
	for (const auto& [id,declaration] : *presentationDeclarations) {
		auto& cell = output.variables.at(id);
		if (cell.expressionDisabled || declaration.expressions.empty()) continue;
		PresentationValue value = declaration.initial;
		for (size_t i = 0; i < declaration.expressions.size(); ++i) {
			StateValue result;
			if (!EvaluateTyped(declaration.expressions[i],base,overlay,result,error)) {
				error = "Presentation variable '"+id+"': "+error; return false;
			}
			if (const auto* text = std::get_if<std::string>(&result)) value.text = *text;
			else if (const auto* flag = std::get_if<bool>(&result)) value.data[0] = *flag ? 1 : 0;
			else value.data[i] = std::get<double>(result);
		}
		if (!ValidPresentationValue(value)) { error = "Invalid presentation variable result '"+id+"'"; return false; }
		cell.value = std::move(value);
		cell.pending = false;
	}
	for (const auto& [id,declared] : *controlDeclarations) {
		const Control& control = *declared;
		ControlReadback value;
		const auto reject = [&]() { error = "Control '"+id+"': "+error; return false; };
		if (control.role == ControlRole::Button) {
			if (!control.context || !EvaluateTyped(*control.context,base,overlay,value.value,error) ||
				!std::holds_alternative<std::string>(value.value)) {
				if (error.empty()) error = "Missing or invalid button context";
				return reject();
			}
			readbacks.emplace(id,std::move(value));
			continue;
		}
		if (!control.value || !EvaluateTyped(*control.value,base,overlay,value.value,error)) {
			if (error.empty()) error = "Missing authoritative value expression";
			return reject();
		}
		if (control.role == ControlRole::Toggle) {
			const auto* spec = std::get_if<ToggleSpec>(&control.widget);
			if (!spec || !std::holds_alternative<bool>(value.value)) { error = "Invalid toggle readback type"; return reject(); }
			if (spec->mixed) {
				StateValue mixed;
				if (!EvaluateTyped(*spec->mixed,base,overlay,mixed,error)) return reject();
				const auto* flag = std::get_if<bool>(&mixed);
				if (!flag) { error = "Mixed state must be Boolean"; return reject(); }
				value.mixed = *flag;
			}
		} else if (control.role == ControlRole::Slider) {
			if (!std::holds_alternative<SliderSpec>(control.widget) || !std::holds_alternative<double>(value.value)) {
				error = "Invalid slider readback type"; return reject();
			}
		} else if (control.role == ControlRole::Number) {
			// A text field (role "text") reads back a string.
			const auto* spec = std::get_if<NumberSpec>(&control.widget);
			if (!spec || !(spec->textField ? std::holds_alternative<std::string>(value.value) : std::holds_alternative<double>(value.value))) {
				error = "Invalid number readback type"; return reject();
			}
		} else if (control.role == ControlRole::Choice) {
			if (control.context) {
				StateValue context;
				if (!EvaluateTyped(*control.context,base,overlay,context,error) || !std::holds_alternative<std::string>(context)) return reject();
				value.context = std::get<std::string>(context);
			}
			const auto* spec = std::get_if<ChoiceSpec>(&control.widget);
			if (!spec) { error = "Invalid choice readback descriptor"; return reject(); }
			if (spec->optionCount) {
				StateValue count;
				if (!EvaluateTyped(*spec->optionCount,base,overlay,count,error)) return reject();
				const auto* number = std::get_if<double>(&count);
				if (!number || !std::isfinite(*number)) { error = "A choice's option count must be a finite number"; return reject(); }
				value.optionCount = static_cast<std::size_t>(std::clamp(std::floor(*number),0.0,static_cast<double>(spec->options.size())));
			}
			for (const auto& option : spec->options) {
				if (option.value.index() != value.value.index() || !ValidStateValue(option.value)) {
					error = "Choice option differs from the authoritative value type"; return reject();
				}
				StateValue available;
				if (!EvaluateTyped(option.enabled,base,overlay,available,error)) return reject();
				const auto* flag = std::get_if<bool>(&available);
				if (!flag) { error = "Choice availability must be Boolean"; return reject(); }
				value.enabledOptions.push_back(*flag);
				std::string label;
				if (!option.labelState.empty()) {
					const StateValue* found = LookupState(base,overlay,option.labelState);
					const auto* text = found == nullptr ? nullptr : std::get_if<std::string>(found);
					if (!text) { error = "A choice option's label state must hold a string"; return reject(); }
					label = *text;
				}
				value.optionLabels.push_back(std::move(label));
			}
		} else { error = "Unsupported value control role"; return reject(); }
		// Bounds/list membership constrain proposals, not authoritative reads:
		// opening a custom configuration must preserve its exact typed value.
		readbacks.emplace(id,std::move(value));
	}
	return true;
}
bool State::Reset(const DocumentModel& model, std::string& error) { return Reset(std::make_shared<const DocumentModel>(model),error); }
bool State::Reset(std::shared_ptr<const DocumentModel> shared, std::string& error) {
	State candidate;
	const DocumentModel& model = *shared;
	candidate.declarations = std::shared_ptr<const std::map<std::string,StateDeclaration>>(shared,&model.state);
	candidate.bindings = std::shared_ptr<const std::vector<Binding>>(shared,&model.bindings);
	candidate.presentationDeclarations = std::shared_ptr<const std::map<std::string,PresentationVariable>>(shared,&model.presentationVariables);
	auto controls = std::make_shared<std::map<std::string,const Control*>>();
	auto scenes = std::make_shared<std::vector<const ModelSceneSpec*>>();
	const auto collect = [&](const auto& self, const Node& node) -> void {
		if (node.model) scenes->push_back(&*node.model);
		if (node.control && node.control->role != ControlRole::Scrollbar &&
			(node.control->role != ControlRole::Button || node.control->context)) controls->emplace(node.id,&*node.control);
		for (const auto& child : node.children) self(self,child);
	};
	collect(collect,model.root);
	candidate.controlDeclarations = std::move(controls);
	candidate.modelDeclarations = std::move(scenes);
	candidate.model = std::move(shared);
	// openQ4: index the bindings by the state they read, so a change
	// re-evaluates only its dependents.
	auto dependents = std::make_shared<std::map<std::string,std::vector<std::size_t>>>();
	for (std::size_t index = 0; index < model.bindings.size(); ++index) {
		std::vector<std::string> names;
		for (const auto& value : model.bindings[index].values) CollectStateReads(value,names);
		std::sort(names.begin(),names.end());
		names.erase(std::unique(names.begin(),names.end()),names.end());
		for (const auto& name : names) (*dependents)[name].push_back(index);
	}
	candidate.dependents = std::move(dependents);
	for (const auto& [id,declaration] : model.presentationVariables)
		candidate.presentation.variables[id] = {declaration.initial,false};
	for (const auto& [id,declaration] : model.state) candidate.variables[id] = declaration.initial;
	error.clear();
	StatePresentationSnapshot presentation;
	if (!candidate.Evaluate(candidate.variables,candidate.properties,candidate.enabled,presentation,candidate.controlValues,error)) return false;
	candidate.presentation = std::move(presentation);
	candidate.revision = 1; *this = std::move(candidate); return true;
}
bool State::Set(const StateValues& changes, std::string& error, bool hostSources) {
	return hostSources ? SetCombined({},changes,error) : SetCombined(changes,{},error);
}
bool State::WouldChange(const StateValues& changes, bool hostSources) const {
	if (presentationDirty) return true;
	for (const auto& [id,value] : changes) {
		const auto declaration = declarations->find(id);
		if (declaration == declarations->end() || !declaration->second.HostSourced() == hostSources ||
			value.index() != declaration->second.initial.index() || !ValidStateValue(value)) return true;
		const auto current = variables.find(id);
		if (current == variables.end() || current->second != value) return true;
	}
	return false;
}
bool State::SetCombined(const StateValues& application, const StateValues& hostSources, std::string& error) {
	error.clear();
	bool changed = false;
	const auto validate = [&](const StateValues& batch, bool host) {
		for (const auto& [id,value] : batch) {
			auto declaration = declarations->find(id);
			if (declaration == declarations->end()) { error = "Unknown state variable '"+id+"'"; return false; }
			if (!declaration->second.HostSourced() == host) { error = "State source ownership mismatch for '"+id+"'"; return false; }
			if (value.index() != declaration->second.initial.index() || !ValidStateValue(value)) {
				error = "Invalid state type/value for '"+id+"'"; return false;
			}
			changed = changed || variables.at(id) != value;
		}
		return true;
	};
	if (!validate(application,false) || !validate(hostSources,true)) return false;
	if (!changed && !presentationDirty) return true;
	if (!presentationDirty) {
		// No transient write awaits evaluation: re-evaluate the dependents of
		// the variables that changed, in place once all of them succeed.
		StateValues overlay;
		for (const auto* batch : {&application,&hostSources}) for (const auto& [id,value] : *batch)
			if (variables.at(id) != value) overlay[id] = value;
		return SetIncremental(overlay,error);
	}
	StateValues candidate = variables;
	for (const auto* batch : {&application,&hostSources}) for (const auto& [id,value] : *batch) candidate[id] = value;
	PropertyValues props;
	std::map<std::string,bool> controls;
	std::map<std::string,ControlReadback> readbacks;
	StatePresentationSnapshot nextPresentation;
	if (!Evaluate(candidate,props,controls,nextPresentation,readbacks,error)) return false;
	variables = std::move(candidate); properties = std::move(props); enabled = std::move(controls); ++revision;
	controlValues = std::move(readbacks);
	presentation = std::move(nextPresentation); presentationDirty = false;
	MarkAllChanged();
	return true;
}
bool State::SetIncremental(const StateValues& overlay, std::string& error) {
	std::vector<std::size_t> affected;
	for (const auto& [id,value] : overlay) {
		const auto found = dependents->find(id);
		if (found != dependents->end()) affected.insert(affected.end(),found->second.begin(),found->second.end());
	}
	std::sort(affected.begin(),affected.end());
	affected.erase(std::unique(affected.begin(),affected.end()),affected.end());
	std::vector<std::pair<PropertyKey,Value>> updates;
	std::vector<std::pair<std::string,bool>> controlUpdates;
	for (const auto index : affected) {
		const auto& binding = (*bindings)[index];
		try {
			if (binding.property == "enabled") {
				controlUpdates.emplace_back(binding.node,std::get<bool>(EvaluateExpression(binding.values.front(),variables,{},nullptr,&overlay)));
				continue;
			}
			PropertyKey key{binding.node,binding.property};
			// A disabled expression keeps its written value (no transient write
			// is pending on this path).
			const auto override = presentation.properties.find(key);
			if (override != presentation.properties.end() && override->second.expressionDisabled) continue;
			Value value = binding.prototype;
			for (size_t i = 0; i < binding.values.size(); ++i) {
				const auto result = EvaluateExpression(binding.values[i],variables,{},nullptr,&overlay);
				if (result.index() == 2) value.text = std::get<std::string>(result);
				else value.data[i] = std::get<double>(result);
			}
			if (!ValidProperty(binding.property,value)) throw std::runtime_error("Result violates the target property's type or range");
			updates.emplace_back(std::move(key),std::move(value));
		} catch (const std::exception& problem) {
			error = "Binding '"+binding.id+"' ("+binding.node+"."+binding.property+"): "+problem.what(); return false;
		}
	}
	const bool derived = !presentationDeclarations->empty() || !controlDeclarations->empty() || !modelDeclarations->empty();
	StatePresentationSnapshot nextPresentation;
	std::map<std::string,ControlReadback> readbacks;
	if (derived) {
		nextPresentation = presentation;
		if (!EvaluateDerived(variables,&overlay,nextPresentation,readbacks,error)) return false;
	}
	for (const auto& [id,value] : overlay) variables[id] = value;
	for (auto& [key,value] : updates) {
		auto& current = properties[key];
		if (SameValue(current,value)) continue;
		current = std::move(value);
		if (!allPropertiesChanged) changedProperties.push_back(key);
	}
	for (const auto& [node,flag] : controlUpdates) enabled[node] = flag;
	if (derived) { presentation = std::move(nextPresentation); controlValues = std::move(readbacks); }
	++revision;
	if (changedProperties.size() > bindings->size()) MarkAllChanged();
	return true;
}
bool State::TakeChangedProperties(std::vector<PropertyKey>& keys) {
	const bool precise = !allPropertiesChanged;
	if (precise) keys.insert(keys.end(),changedProperties.begin(),changedProperties.end());
	changedProperties.clear(); allPropertiesChanged = false;
	return precise;
}
bool State::Restore(const StateValues& application, const StateValues& hostSources, std::string& error,
	const StatePresentationSnapshot* restoredPresentation) {
	error.clear();
	StateValues candidate;
	for (const auto& [id,declaration] : *declarations) {
		const auto& source = declaration.HostSourced() ? hostSources : application;
		const auto found = source.find(id);
		if (found == source.end() || found->second.index() != declaration.initial.index() || !ValidStateValue(found->second)) {
			error = "Missing or invalid restored state '"+id+"'"; return false;
		}
		candidate.emplace(id,found->second);
	}
	if (application.size()+hostSources.size() != candidate.size()) { error = "Unexpected or wrong-owner restored state"; return false; }
	PropertyValues props;
	std::map<std::string,bool> controls;
	std::map<std::string,ControlReadback> readbacks;
	State staged = *this;
	// A v1 instance has no presentation table. Use declared defaults, not
	// overrides left in the receiving live instance.
	staged.presentation = {};
	for (const auto& [id,declaration] : *presentationDeclarations)
		staged.presentation.variables[id] = {declaration.initial,false};
	if (restoredPresentation) {
		if (restoredPresentation->variables.size() != presentationDeclarations->size()) { error = "Incomplete restored presentation variables"; return false; }
		for (const auto& [id,cell] : restoredPresentation->variables) {
			const auto declaration = presentationDeclarations->find(id);
			if (declaration == presentationDeclarations->end() || cell.value.type != declaration->second.initial.type || !ValidPresentationValue(cell.value) ||
				(cell.pending && (cell.expressionDisabled || declaration->second.expressions.empty()))) {
				error = "Invalid restored presentation variable '"+id+"'"; return false;
			}
		}
		for (const auto& [key,item] : restoredPresentation->properties) {
			if (!staged.OverrideProperties({{key,item.value}},item.expressionDisabled,error)) return false;
		}
		staged.presentation = *restoredPresentation;
	}
	StatePresentationSnapshot evaluated;
	if (!staged.Evaluate(candidate,props,controls,evaluated,readbacks,error)) return false;
	// Keep saved transient writes until the next successful evaluation, just
	// as saving before StateChanged preserves the caller's pending dictionary.
	if (restoredPresentation) {
		for (const auto& [key,item] : restoredPresentation->properties) props[key] = item.value;
		evaluated.properties = restoredPresentation->properties;
		for (const auto& [id,cell] : restoredPresentation->variables) if (cell.pending) evaluated.variables[id] = cell;
	}
	staged.variables = std::move(candidate); staged.properties = std::move(props); staged.enabled = std::move(controls);
	staged.controlValues = std::move(readbacks);
	staged.presentation = std::move(evaluated); ++staged.revision;
	staged.presentationDirty = false;
	staged.MarkAllChanged();
	for (const auto& [id,cell] : staged.presentation.variables) staged.presentationDirty = staged.presentationDirty || cell.pending;
	for (const auto& [key,item] : staged.presentation.properties) staged.presentationDirty = staged.presentationDirty || !item.expressionDisabled;
	*this = std::move(staged);
	return true;
}
bool State::WritePresentationVariable(const std::string& id, const PresentationValue& value,
	bool overrideExpression, std::string& error) {
	error.clear();
	const auto declaration = presentationDeclarations->find(id);
	if (declaration == presentationDeclarations->end() || value.type != declaration->second.initial.type || !ValidPresentationValue(value)) {
		error = "Invalid presentation variable write '"+id+"'"; return false;
	}
	auto& cell = presentation.variables.at(id);
	cell.value = value; cell.expressionDisabled = cell.expressionDisabled || overrideExpression;
	cell.pending = !cell.expressionDisabled && !declaration->second.expressions.empty();
	presentationDirty = presentationDirty || cell.pending;
	++revision; return true;
}
bool State::OverrideProperties(const PropertyValues& values, bool overrideExpression, std::string& error) {
	error.clear();
	for (const auto& [key,value] : values) {
		const auto binding = std::find_if(bindings->begin(),bindings->end(),[&](const Binding& b) { return b.node == key.first && b.property == key.second && b.property != "enabled"; });
		if (binding == bindings->end() || value.type != binding->prototype.type || value.unit != binding->prototype.unit || !ValidProperty(key.second,value)) {
			error = "Invalid bound presentation property '"+key.first+"."+key.second+"'"; return false;
		}
	}
	for (const auto& [key,value] : values) {
		auto& item = presentation.properties[key]; item.value = value;
		item.expressionDisabled = item.expressionDisabled || overrideExpression;
		presentationDirty = presentationDirty || !item.expressionDisabled;
		properties[key] = value;
		if (!allPropertiesChanged) changedProperties.push_back(key);
	}
	if (changedProperties.size() > bindings->size()) MarkAllChanged();
	if (!values.empty()) ++revision;
	return true;
}
} // namespace openq4::ui
