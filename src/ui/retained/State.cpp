// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "State.h"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <stdexcept>

namespace openq4::ui {
namespace {
StateValue EvaluateExpression(const Expression& e, const StateValues& variables, const PresentationLookup& presentation = {}) {
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
			const auto found = variables.find(e.state);
			if (found == variables.end()) throw std::runtime_error("Missing state variable '"+e.state+"'");
			value = &found->second;
		}
		if (value->index() != e.type || !ValidStateValue(*value))
			throw std::runtime_error(e.state.empty() ? "Invalid compiled expression literal" : "Invalid state type/value for '"+e.state+"'");
		return *value;
	}
	auto arg = [&](size_t i) { return EvaluateExpression(e.args.at(i),variables,presentation); };
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
		const auto result = std::to_chars(buffer,buffer+sizeof(buffer),number(0),std::chars_format::fixed,e.decimals);
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
}
bool EvaluateStateExpression(const Expression& expression, const StateValues& variables, StateValue& value, std::string& error,
	const PresentationLookup& presentation) {
	error.clear();
	try {
		StateValue candidate = EvaluateExpression(expression,variables,presentation);
		if (candidate.index() != expression.type || !ValidStateValue(candidate))
			throw std::runtime_error("Expression result violates its compiled type or supported value range");
		value = std::move(candidate); return true;
	} catch (const std::exception& problem) {
		error = problem.what(); return false;
	}
}
bool DocumentModel::ResolveAction(const std::string& id, const StateValues& variables, ActionInvocation& invocation, std::string& error,
	const PresentationLookup& presentation) const {
	error.clear();
	const auto found = actions.find(id);
	if (found == actions.end()) { error = "Unknown action descriptor '"+id+"'"; return false; }
	ActionInvocation candidate; candidate.action = id; candidate.operation = found->second.operation;
	for (const auto& [name,expression] : found->second.arguments) {
		StateValue value;
		if (!EvaluateStateExpression(expression,variables,value,error,presentation)) {
			error = "Action '"+id+"' argument '"+name+"': "+error; return false;
		}
		candidate.arguments.emplace(name,std::move(value));
	}
	invocation = std::move(candidate); return true;
}
bool State::Evaluate(const StateValues& candidate, PropertyValues& props, std::map<std::string,bool>& controls,
	StatePresentationSnapshot& output, std::string& error) const {
	output = presentation;
	for (const auto& binding : bindings) {
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
	for (const auto& [id,declaration] : presentationDeclarations) {
		auto& cell = output.variables.at(id);
		if (cell.expressionDisabled || declaration.expressions.empty()) continue;
		PresentationValue value = declaration.initial;
		for (size_t i = 0; i < declaration.expressions.size(); ++i) {
			StateValue result;
			if (!EvaluateStateExpression(declaration.expressions[i],candidate,result,error)) {
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
	return true;
}
bool State::Reset(const DocumentModel& model, std::string& error) {
	State candidate;
	candidate.declarations = model.state; candidate.bindings = model.bindings;
	candidate.presentationDeclarations = model.presentationVariables;
	for (const auto& [id,declaration] : model.presentationVariables)
		candidate.presentation.variables[id] = {declaration.initial,false};
	for (const auto& [id,declaration] : model.state) candidate.variables[id] = declaration.initial;
	error.clear();
	StatePresentationSnapshot presentation;
	if (!candidate.Evaluate(candidate.variables,candidate.properties,candidate.enabled,presentation,error)) return false;
	candidate.presentation = std::move(presentation);
	candidate.revision = 1; *this = std::move(candidate); return true;
}
bool State::Set(const StateValues& changes, std::string& error, bool hostSources) {
	return hostSources ? SetCombined({},changes,error) : SetCombined(changes,{},error);
}
bool State::SetCombined(const StateValues& application, const StateValues& hostSources, std::string& error) {
	error.clear();
	bool changed = false;
	const auto validate = [&](const StateValues& batch, bool host) {
		for (const auto& [id,value] : batch) {
			auto declaration = declarations.find(id);
			if (declaration == declarations.end()) { error = "Unknown state variable '"+id+"'"; return false; }
			if (declaration->second.cvar.empty() == host) { error = "State source ownership mismatch for '"+id+"'"; return false; }
			if (value.index() != declaration->second.initial.index() || !ValidStateValue(value)) {
				error = "Invalid state type/value for '"+id+"'"; return false;
			}
			changed = changed || variables.at(id) != value;
		}
		return true;
	};
	if (!validate(application,false) || !validate(hostSources,true)) return false;
	if (!changed && !presentationDirty) return true;
	StateValues candidate = variables;
	for (const auto* batch : {&application,&hostSources}) for (const auto& [id,value] : *batch) candidate[id] = value;
	PropertyValues props;
	std::map<std::string,bool> controls;
	StatePresentationSnapshot nextPresentation;
	if (!Evaluate(candidate,props,controls,nextPresentation,error)) return false;
	variables = std::move(candidate); properties = std::move(props); enabled = std::move(controls); ++revision;
	presentation = std::move(nextPresentation); presentationDirty = false;
	return true;
}
bool State::Restore(const StateValues& application, const StateValues& hostSources, std::string& error,
	const StatePresentationSnapshot* restoredPresentation) {
	error.clear();
	StateValues candidate;
	for (const auto& [id,declaration] : declarations) {
		const auto& source = declaration.cvar.empty() ? application : hostSources;
		const auto found = source.find(id);
		if (found == source.end() || found->second.index() != declaration.initial.index() || !ValidStateValue(found->second)) {
			error = "Missing or invalid restored state '"+id+"'"; return false;
		}
		candidate.emplace(id,found->second);
	}
	if (application.size()+hostSources.size() != candidate.size()) { error = "Unexpected or wrong-owner restored state"; return false; }
	PropertyValues props;
	std::map<std::string,bool> controls;
	State staged = *this;
	// A v1 instance has no presentation table. Use declared defaults, not
	// overrides left in the receiving live instance.
	staged.presentation = {};
	for (const auto& [id,declaration] : presentationDeclarations)
		staged.presentation.variables[id] = {declaration.initial,false};
	if (restoredPresentation) {
		if (restoredPresentation->variables.size() != presentationDeclarations.size()) { error = "Incomplete restored presentation variables"; return false; }
		for (const auto& [id,cell] : restoredPresentation->variables) {
			const auto declaration = presentationDeclarations.find(id);
			if (declaration == presentationDeclarations.end() || cell.value.type != declaration->second.initial.type || !ValidPresentationValue(cell.value) ||
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
	if (!staged.Evaluate(candidate,props,controls,evaluated,error)) return false;
	// Keep saved transient writes until the next successful evaluation, just
	// as saving before StateChanged preserves the caller's pending dictionary.
	if (restoredPresentation) {
		for (const auto& [key,item] : restoredPresentation->properties) props[key] = item.value;
		evaluated.properties = restoredPresentation->properties;
		for (const auto& [id,cell] : restoredPresentation->variables) if (cell.pending) evaluated.variables[id] = cell;
	}
	staged.variables = std::move(candidate); staged.properties = std::move(props); staged.enabled = std::move(controls);
	staged.presentation = std::move(evaluated); ++staged.revision;
	staged.presentationDirty = false;
	for (const auto& [id,cell] : staged.presentation.variables) staged.presentationDirty = staged.presentationDirty || cell.pending;
	for (const auto& [key,item] : staged.presentation.properties) staged.presentationDirty = staged.presentationDirty || !item.expressionDisabled;
	*this = std::move(staged);
	return true;
}
bool State::WritePresentationVariable(const std::string& id, const PresentationValue& value,
	bool overrideExpression, std::string& error) {
	error.clear();
	const auto declaration = presentationDeclarations.find(id);
	if (declaration == presentationDeclarations.end() || value.type != declaration->second.initial.type || !ValidPresentationValue(value)) {
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
		const auto binding = std::find_if(bindings.begin(),bindings.end(),[&](const Binding& b) { return b.node == key.first && b.property == key.second && b.property != "enabled"; });
		if (binding == bindings.end() || value.type != binding->prototype.type || value.unit != binding->prototype.unit || !ValidProperty(key.second,value)) {
			error = "Invalid bound presentation property '"+key.first+"."+key.second+"'"; return false;
		}
	}
	for (const auto& [key,value] : values) {
		auto& item = presentation.properties[key]; item.value = value;
		item.expressionDisabled = item.expressionDisabled || overrideExpression;
		presentationDirty = presentationDirty || !item.expressionDisabled;
		properties[key] = value;
	}
	if (!values.empty()) ++revision;
	return true;
}
} // namespace openq4::ui
