// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "Behavior.h"
#include <algorithm>
#include <cmath>
#include <exception>

namespace openq4::ui {
namespace {
std::optional<Value> Property(const State& state, const Motion& motion, const PropertyKey& key) {
	const auto bound = state.Properties().find(key);
	if (bound != state.Properties().end()) return bound->second;
	const auto animated = motion.Values().find(key);
	return animated == motion.Values().end() ? std::nullopt : std::optional<Value>(animated->second);
}
std::vector<std::string> AliasProperties(const PresentationAlias& alias) {
	if (alias.property == "rect") return {"left","top","width","height"};
	if (alias.property == "visible") return {"display"};
	if (alias.property == "noevents") return {"pointer-events"};
	return {alias.property};
}
size_t Components(PresentationType type) {
	switch (type) {
		case PresentationType::Vector2: return 2;
		case PresentationType::Vector3: return 3;
		case PresentationType::Vector4: return 4;
		default: return 1;
	}
}
}

bool ReadPresentationAlias(const DocumentModel& model, const State& state, const Motion& motion,
	const std::string& name, PresentationValue& value) {
	const auto found = model.aliases.find(PresentationAliasKey(name));
	if (found == model.aliases.end()) return false;
	const auto& alias = found->second;
	PresentationValue candidate;
	if (!alias.variable.empty()) {
		const auto variable = state.Presentation().variables.find(alias.variable);
		if (variable == state.Presentation().variables.end()) return false;
		candidate = variable->second.value;
	} else if (alias.property == "rect") {
		candidate.type = PresentationType::Vector4;
		const auto properties = AliasProperties(alias);
		for (size_t i = 0; i < properties.size(); ++i) {
			const auto part = Property(state,motion,{alias.node,properties[i]});
			if (!part || part->type != ValueType::Length || part->unit != "dp") return false;
			candidate.data[i] = part->data[0];
		}
	} else {
		const auto property = Property(state,motion,{alias.node,AliasProperties(alias).front()});
		if (!property) return false;
		if (alias.property == "visible" || alias.property == "noevents") {
			candidate.type = PresentationType::Boolean;
			candidate.data[0] = (alias.property == "visible" ? property->text != "none" : property->text == "none") ? 1 : 0;
		} else if (property->type == ValueType::Number || property->type == ValueType::Length) {
			candidate.type = PresentationType::Number; candidate.data[0] = property->data[0];
		} else if (property->type == ValueType::Colour) {
			candidate.type = PresentationType::Vector4; std::copy_n(property->data.begin(),4,candidate.data.begin());
		} else if (property->type == ValueType::Text || property->type == ValueType::Keyword || property->type == ValueType::Font) {
			candidate.type = PresentationType::String; candidate.text = property->text;
		} else return false;
	}
	if (!ValidPresentationValue(candidate)) return false;
	value = std::move(candidate); return true;
}

bool WritePresentationAlias(const DocumentModel& model, State& state, Motion& motion,
	const std::string& name, const PresentationValue& value, bool overrideExpression, std::string& error) {
	error.clear();
	const auto found = model.aliases.find(PresentationAliasKey(name));
	if (found == model.aliases.end()) { error = "Unknown presentation alias '"+name+"'"; return false; }
	const auto& alias = found->second;
	PresentationValue current;
	if (!ReadPresentationAlias(model,state,motion,name,current)) { error = "Unavailable presentation target"; return false; }
	if (current.type != value.type || !ValidPresentationValue(value)) { error = "Invalid presentation value type/range"; return false; }
	if (!alias.variable.empty()) return state.WritePresentationVariable(alias.variable,value,overrideExpression,error);
	PropertyValues bound, unbound;
	const auto properties = AliasProperties(alias);
	for (size_t i = 0; i < properties.size(); ++i) {
		const PropertyKey key{alias.node,properties[i]};
		auto property = Property(state,motion,key);
		if (!property) { error = "Unavailable presentation property"; return false; }
		auto& target = *property;
		if (alias.property == "rect") target.data[0] = value.data[i];
		else if (alias.property == "visible") target.text = value.data[0] != 0 ? alias.shown : "none";
		else if (alias.property == "noevents") target.text = value.data[0] != 0 ? "none" : "auto";
		else if (value.type == PresentationType::String) target.text = value.text;
		else if (value.type == PresentationType::Vector4) std::copy_n(value.data.begin(),4,target.data.begin());
		else target.data[0] = value.data[0];
		(state.Properties().contains(key) ? bound : unbound)[key] = std::move(target);
	}
	State candidateState = state; Motion candidateMotion = motion;
	if (!candidateState.OverrideProperties(bound,overrideExpression,error) || !candidateMotion.WriteValues(unbound,error)) return false;
	state = std::move(candidateState); motion = std::move(candidateMotion); return true;
}

PresentationLookup MakePresentationLookup(const DocumentModel& model, const State& state, const Motion& motion) {
	return [&model,&state,&motion](const std::string& name, int component, StateValue& result, std::string& error) {
		PresentationValue value;
		if (!ReadPresentationAlias(model,state,motion,name,value)) { error = "Unavailable presentation alias '"+name+"'"; return false; }
		const auto count = Components(value.type);
		if ((count == 1 && component != -1) || (count > 1 && (component < 0 || static_cast<size_t>(component) >= count))) {
			error = "Invalid presentation component for '"+name+"'"; return false;
		}
		if (value.type == PresentationType::String) result = value.text;
		else if (value.type == PresentationType::Boolean) result = value.data[0] != 0;
		else result = value.data[count == 1 ? 0 : static_cast<size_t>(component)];
		return true;
	};
}

namespace {
class Evaluator {
public:
	Evaluator(const DocumentModel& model, EventResult& candidate, double seconds, size_t maxActions)
		: model(model), candidate(candidate), seconds(seconds), maxActions(maxActions),
		  lookup(MakePresentationLookup(model,candidate.state,candidate.motion)) {}
	bool Call(const std::string& name, unsigned depth, std::string& error) {
		if (depth >= 32) { error = "Event call/branch depth exceeded 32"; return false; }
		const auto found = model.events.find(PresentationAliasKey(name));
		if (found == model.events.end()) { error = "Unknown event '"+name+"'"; return false; }
		if (Steps(found->second.steps,depth+1,error)) return true;
		error = "Event '"+found->second.name+"': "+error; return false;
	}
private:
	bool Eval(const Expression& expression, StateValue& value, std::string& error) const {
		return EvaluateStateExpression(expression,candidate.state.Variables(),value,error,lookup);
	}
	bool Steps(const std::vector<EventStep>& steps, unsigned depth, std::string& error) {
		if (depth > 32) { error = "Event call/branch depth exceeded 32"; return false; }
		for (const auto& step : steps) {
			if (++visited > 16384) { error = "Event execution exceeded 16384 instructions"; return false; }
			switch (step.op) {
				case EventOp::SetState: {
					StateValues changes;
					for (const auto& [id,expression] : step.values) {
						StateValue value;
						if (!Eval(expression,value,error)) return false;
						changes.emplace(id,std::move(value));
					}
					if (!candidate.state.Set(changes,error)) return false;
					for (const auto& [id,value] : changes) candidate.stateChanges[id] = value;
					break;
				}
				case EventOp::SetPresentation: {
					const auto type = PresentationAliasType(model,step.target);
					if (!type || Components(*type) != step.presentation.size()) { error = "Invalid event presentation target/component count"; return false; }
					PresentationValue value; value.type = *type;
					for (size_t i = 0; i < step.presentation.size(); ++i) {
						StateValue part;
						if (!Eval(step.presentation[i],part,error)) return false;
						if (*type == PresentationType::String) value.text = std::get<std::string>(part);
						else if (*type == PresentationType::Boolean) value.data[0] = std::get<bool>(part) ? 1 : 0;
						else value.data[i] = std::get<double>(part);
					}
					if (!WritePresentationAlias(model,candidate.state,candidate.motion,step.target,value,step.overrideExpression,error)) return false;
					break;
				}
				case EventOp::Action: {
					if (candidate.actions.size() >= maxActions) { error = "Event application action budget exceeded"; return false; }
					ActionInvocation action;
					if (!model.ResolveAction(step.target,candidate.state.Variables(),action,error,lookup)) return false;
					candidate.actions.push_back(std::move(action)); break;
				}
				case EventOp::Call:
					if (!Call(step.target,depth,error)) return false;
					break;
				case EventOp::If: {
					StateValue condition;
					if (!Eval(step.condition,condition,error)) return false;
					if (!Steps(std::get<bool>(condition) ? step.thenSteps : step.elseSteps,depth+1,error)) return false;
					break;
				}
				case EventOp::PlayTimeline:
				case EventOp::PauseTimeline:
				case EventOp::ResumeTimeline:
				case EventOp::CancelTimeline: {
					if (std::none_of(model.timelines.begin(),model.timelines.end(),[&](const Timeline& timeline) { return timeline.id == step.target; })) {
						error = "Unknown event timeline '"+step.target+"'"; return false;
					}
					if (step.op == EventOp::PlayTimeline) {
						if (!candidate.motion.Play(step.target,seconds)) { error = "Event timeline could not start"; return false; }
					} else if (step.op == EventOp::PauseTimeline) candidate.motion.Pause(step.target,seconds);
					else if (step.op == EventOp::ResumeTimeline) candidate.motion.Resume(step.target,seconds);
					else candidate.motion.Cancel(step.target,step.restoreBase ? CancelPolicy::RestoreBase : CancelPolicy::Hold,seconds);
					break;
				}
				default: error = "Unknown compiled event instruction"; return false;
			}
		}
		return true;
	}
	const DocumentModel& model;
	EventResult& candidate;
	double seconds;
	size_t maxActions, visited = 0;
	PresentationLookup lookup;
};
}

bool EvaluateEvent(const DocumentModel& model, const State& state, const Motion& motion,
	const std::string& name, double seconds, EventResult& result, std::string& error,
	const ActionValidator& validate, size_t maxActions) {
	error.clear();
	if (!std::isfinite(seconds) || seconds < 0 || maxActions > 256) { error = "Invalid event time or application action budget"; return false; }
	try {
		EventResult candidate; candidate.state = state; candidate.motion = motion;
		candidate.motion.Advance(seconds);
		Evaluator evaluator(model,candidate,seconds,maxActions);
		if (!evaluator.Call(name,0,error)) return false;
		if (validate) for (const auto& action : candidate.actions) {
			if (!validate(action,error)) { if (error.empty()) error = "Application action validation rejected '"+action.action+"'"; return false; }
		}
		result = std::move(candidate); return true;
	} catch (const std::exception& problem) { error = std::string("Cannot evaluate event: ")+problem.what(); return false; }
}

} // namespace openq4::ui
