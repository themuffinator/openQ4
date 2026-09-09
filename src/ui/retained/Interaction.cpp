// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "Interaction.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace openq4::ui {
void Interaction::Reset(const DocumentModel& model) {
	document = model.id; focused.clear(); hovered.clear(); armed.clear();
	items.clear(); order.clear(); parents.clear(); modals.clear(); feedback.clear(); actions.clear(); overflowed = false;
	std::vector<std::pair<const Node*,std::string>> pending{{&model.root,{}}};
	while (!pending.empty()) {
		const auto [node,parent] = pending.back(); pending.pop_back(); parents[node->id] = parent;
		if (node->control) { items[node->id].control = *node->control; order.push_back(node->id); }
		for (auto it = node->children.rbegin(); it != node->children.rend(); ++it) pending.push_back({&*it,node->id});
	}
	Refresh();
}
bool Interaction::Within(const std::string& id, const std::string& root) const {
	if (root.empty()) return true;
	for (std::string current = id; !current.empty();) {
		if (current == root) return true;
		const auto parent = parents.find(current);
		if (parent == parents.end()) break;
		current = parent->second;
	}
	return false;
}
bool Interaction::Eligible(const std::string& id) const {
	const auto found = items.find(id);
	return found != items.end() && found->second.control.enabled && found->second.bounds.visible && Within(id,Modal());
}
std::string Interaction::First() const { for (const auto& id : order) if (Eligible(id)) return id; return {}; }
void Interaction::SetBounds(const std::map<std::string,ControlBounds>& bounds) {
	for (auto& [id,item] : items) {
		const auto found = bounds.find(id); item.bounds = found == bounds.end() ? ControlBounds{} : found->second;
		const auto& b = item.bounds;
		item.bounds.visible = b.visible && std::isfinite(b.x) && std::isfinite(b.y) && std::isfinite(b.width) && std::isfinite(b.height) && b.width > 0 && b.height > 0;
	}
	if (!modals.empty() && !Eligible(focused)) focused = First();
	Refresh();
}
bool Interaction::SetEnabled(const std::string& id, bool enabled) {
	auto found = items.find(id); if (found == items.end()) return false;
	found->second.control.enabled = enabled;
	if (!modals.empty() && !Eligible(focused)) focused = First();
	Refresh(); return true;
}
bool Interaction::Focus(const std::string& id) {
	if (!id.empty() && !Eligible(id)) return false;
	if (focused != id) { armed.clear(); focused = id; }
	Refresh(); return true;
}
void Interaction::Hover(const std::string& id) { hovered = Eligible(id) ? id : std::string{}; Refresh(); }
void Interaction::Pointer(bool down) {
	if (down) {
		if (pointerHeld) return;
		pointerHeld = true;
		if (Eligible(hovered) && armed.empty()) { Focus(hovered); armed = hovered; pointerArm = true; }
	} else {
		if (!pointerHeld) return;
		pointerHeld = false;
		if (pointerArm && !armed.empty()) {
			if (armed == hovered && Eligible(armed)) Queue({ControlAction::Kind::Activate,document,armed,items.at(armed).control.action});
			armed.clear();
		}
	}
	Refresh();
}
void Interaction::Input(MenuInput input, bool down) {
	if (input == MenuInput::Accept) {
		if (down) {
			if (acceptHeld) return;
			acceptHeld = true;
			if (Eligible(focused) && armed.empty()) { armed = focused; pointerArm = false; }
		} else {
			if (!acceptHeld) return;
			acceptHeld = false;
			if (!pointerArm && !armed.empty()) {
				if (armed == focused && Eligible(armed)) Queue({ControlAction::Kind::Activate,document,armed,items.at(armed).control.action});
				armed.clear();
			}
		}
	} else if (input == MenuInput::Back) {
		if (down && !backHeld) { armed.clear(); Queue({ControlAction::Kind::Back,document,Modal(),{}}); }
		backHeld = down;
	} else if (down) Navigate(input);
	Refresh();
}
void Interaction::Cancel() { armed.clear(); hovered.clear(); Refresh(); }
void Interaction::ReleaseInputSources() { pointerHeld = acceptHeld = backHeld = false; }
bool Interaction::PushModal(const std::string& root) {
	if (!parents.contains(root) || !Within(root,Modal()) || root == Modal()) return false;
	modals.push_back({root,focused}); armed.clear(); hovered.clear(); focused = First(); Refresh(); return true;
}
bool Interaction::PopModal() {
	if (modals.empty()) return false;
	const auto restore = modals.back().restore; modals.pop_back(); armed.clear(); hovered.clear();
	focused = Eligible(restore) ? restore : First(); Refresh(); return true;
}
void Interaction::Navigate(MenuInput input) {
	static const char* names[] = {"next","previous","up","down","left","right"};
	if (input < MenuInput::Next || input > MenuInput::Right) return;
	if (!Eligible(focused)) {
		if (input == MenuInput::Previous) { for (auto it = order.rbegin(); it != order.rend(); ++it) if (Eligible(*it)) { Focus(*it); return; } }
		Focus(First()); return;
	}
	const auto& item = items.at(focused);
	const auto explicitTarget = item.control.navigation.find(names[static_cast<unsigned>(input)]);
	if (explicitTarget != item.control.navigation.end() && Eligible(explicitTarget->second)) { Focus(explicitTarget->second); return; }
	if (input == MenuInput::Next || input == MenuInput::Previous) {
		const auto current = std::find(order.begin(),order.end(),focused)-order.begin();
		for (size_t n = 1; n <= order.size(); ++n) {
			const auto i = (current+order.size()+(input == MenuInput::Next ? n : order.size()-n))%order.size();
			if (Eligible(order[i])) { Focus(order[i]); break; }
		}
		return;
	}
	const auto& a = item.bounds;
	const double ax = a.x+a.width*.5, ay = a.y+a.height*.5;
	double best = std::numeric_limits<double>::infinity(); std::string next;
	for (const auto& id : order) if (id != focused && Eligible(id)) {
		const auto& b = items.at(id).bounds;
		const double dx = b.x+b.width*.5-ax, dy = b.y+b.height*.5-ay;
		const bool horizontal = input == MenuInput::Left || input == MenuInput::Right;
		const double forward = (horizontal ? dx : dy)*(input == MenuInput::Left || input == MenuInput::Up ? -1 : 1);
		if (forward <= .001) continue;
		const double lateral = std::abs(horizontal ? dy : dx);
		const double score = forward*forward+4*lateral*lateral;
		if (score < best) { best = score; next = id; }
	}
	if (!next.empty()) Focus(next);
}
void Interaction::Refresh() {
	if (!Eligible(focused)) focused.clear();
	if (!Eligible(hovered)) hovered.clear();
	if (!Eligible(armed)) armed.clear();
	for (auto& [id,item] : items) {
		const auto state = !item.control.enabled ? ControlState::Disabled :
			armed == id && (!pointerArm || hovered == id) ? ControlState::Pressed :
			focused == id ? ControlState::Focus : hovered == id ? ControlState::Hover : ControlState::Default;
		if (!item.known || item.state != state) { item.known = true; item.state = state; feedback.push_back({id,item.control.states.at(state),state}); }
	}
}
std::optional<ControlState> Interaction::State(const std::string& id) const { const auto found = items.find(id); return found == items.end() ? std::nullopt : std::optional(found->second.state); }
void Interaction::Queue(ControlAction action) { if (actions.size() < 256) actions.push_back(std::move(action)); else overflowed = true; }
std::vector<ControlFeedback> Interaction::TakeFeedback() { std::vector<ControlFeedback> result; result.swap(feedback); return result; }
std::vector<ControlAction> Interaction::TakeActions() { std::vector<ControlAction> result; result.swap(actions); overflowed = false; return result; }
InteractionSnapshot Interaction::Capture() const {
	InteractionSnapshot result;
	result.focus = focused;
	for (const auto& scope : modals) result.modals.push_back({scope.root,scope.restore});
	for (const auto& [id,item] : items) { result.enabled[id] = item.control.enabled; result.presented[id] = item.state; }
	return result;
}
bool Interaction::Restore(const InteractionSnapshot& snapshot, std::string& error) {
	error.clear();
	auto reject = [&](const char* message) { error = message; return false; };
	if (snapshot.enabled.size() != items.size() || snapshot.presented.size() != items.size()) return reject("Restored controls do not match the document");
	for (const auto& [id,enabled] : snapshot.enabled) if (!items.contains(id)) return reject("Unknown restored control");
	for (const auto& [id,state] : snapshot.presented) {
		if (!items.contains(id) || (state != ControlState::Default && state != ControlState::Focus && state != ControlState::Disabled) ||
			(state == ControlState::Focus) != (id == snapshot.focus)) return reject("Invalid restored persistent control presentation");
	}
	if (!snapshot.focus.empty() && !items.contains(snapshot.focus)) return reject("Unknown restored focus control");
	std::string previous;
	for (const auto& scope : snapshot.modals) {
		if (scope.root.empty() || !parents.contains(scope.root) || scope.root == previous || !Within(scope.root,previous))
			return reject("Invalid restored modal ancestry");
		if (!scope.restore.empty() && (!items.contains(scope.restore) || !Within(scope.restore,previous)))
			return reject("Invalid restored modal return focus");
		previous = scope.root;
	}
	if (!snapshot.focus.empty() && !Within(snapshot.focus,previous)) return reject("Restored focus is outside its modal");
	Interaction candidate = *this;
	candidate.focused = snapshot.focus;
	candidate.modals.clear();
	for (const auto& scope : snapshot.modals) candidate.modals.push_back({scope.root,scope.restore});
	candidate.armed.clear(); candidate.hovered.clear(); candidate.pointerArm = false;
	candidate.feedback.clear(); candidate.actions.clear(); candidate.overflowed = false;
	for (auto& [id,item] : candidate.items) item.control.enabled = snapshot.enabled.at(id);
	// A host-source update may disable the saved selection. Fresh projected
	// bounds choose the fallback; stale pre-restore geometry is not consulted.
	if (!candidate.focused.empty() && !candidate.items.at(candidate.focused).control.enabled) candidate.focused.clear();
	for (auto& [id,item] : candidate.items) {
		item.state = !item.control.enabled ? ControlState::Disabled : candidate.focused == id ? ControlState::Focus : ControlState::Default;
		item.known = true;
		if (item.state != snapshot.presented.at(id)) candidate.feedback.push_back({id,item.control.states.at(item.state),item.state});
	}
	*this = std::move(candidate);
	return true;
}
} // namespace openq4::ui
