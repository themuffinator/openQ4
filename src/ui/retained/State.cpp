// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "State.h"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <stdexcept>

namespace openq4::ui {
namespace {
StateValue EvaluateExpression(const Expression& e, const StateValues& variables) {
	if (e.op.empty()) return e.state.empty() ? e.literal : variables.at(e.state);
	auto arg = [&](size_t i) { return EvaluateExpression(e.args.at(i),variables); };
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
bool State::Evaluate(const StateValues& candidate, PropertyValues& props, std::map<std::string,bool>& controls, std::string& error) const {
	for (const auto& binding : bindings) {
		try {
			if (binding.property == "enabled") {
				controls[binding.node] = std::get<bool>(EvaluateExpression(binding.values.front(),candidate)); continue;
			}
			Value value = binding.prototype;
			for (size_t i = 0; i < binding.values.size(); ++i) {
				const auto result = EvaluateExpression(binding.values[i],candidate);
				if (result.index() == 2) value.text = std::get<std::string>(result);
				else value.data[i] = std::get<double>(result);
			}
			if (!ValidProperty(binding.property,value)) throw std::runtime_error("Result violates the target property's type or range");
			props[{binding.node,binding.property}] = std::move(value);
		} catch (const std::exception& problem) {
			error = "Binding '"+binding.id+"' ("+binding.node+"."+binding.property+"): "+problem.what(); return false;
		}
	}
	return true;
}
bool State::Reset(const DocumentModel& model, std::string& error) {
	State candidate;
	candidate.declarations = model.state; candidate.bindings = model.bindings;
	for (const auto& [id,declaration] : model.state) candidate.variables[id] = declaration.initial;
	error.clear();
	if (!candidate.Evaluate(candidate.variables,candidate.properties,candidate.enabled,error)) return false;
	candidate.revision = 1; *this = std::move(candidate); return true;
}
bool State::Set(const StateValues& changes, std::string& error, bool hostSources) {
	error.clear();
	bool changed = false;
	for (const auto& [id,value] : changes) {
		auto declaration = declarations.find(id);
		if (declaration == declarations.end()) { error = "Unknown state variable '"+id+"'"; return false; }
		if (declaration->second.cvar.empty() == hostSources) { error = "State source ownership mismatch for '"+id+"'"; return false; }
		if (value.index() != declaration->second.initial.index() || !ValidStateValue(value)) {
			error = "Invalid state type/value for '"+id+"'"; return false;
		}
		changed = changed || variables.at(id) != value;
	}
	if (!changed) return true;
	StateValues candidate = variables;
	for (const auto& [id,value] : changes) candidate[id] = value;
	PropertyValues props;
	std::map<std::string,bool> controls;
	if (!Evaluate(candidate,props,controls,error)) return false;
	variables = std::move(candidate); properties = std::move(props); enabled = std::move(controls); ++revision;
	return true;
}
bool State::Restore(const StateValues& application, const StateValues& hostSources, std::string& error) {
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
	if (!Evaluate(candidate,props,controls,error)) return false;
	if (variables != candidate) ++revision;
	variables = std::move(candidate); properties = std::move(props); enabled = std::move(controls);
	return true;
}
} // namespace openq4::ui
