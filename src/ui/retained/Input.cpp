// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "Input.h"
#include <algorithm>
#include <cmath>

namespace openq4::ui {
bool Input::Held(RoutedInput::Kind kind, MenuInput action) const {
	for (const auto& [id,source] : sources)
		if (!source.blocked && source.kind == kind && source.action == action) return true;
	return false;
}
void Input::Emit(const Source& source, bool down) { events.push_back({source.kind,source.action,down}); }
void Input::Menu(std::uint32_t source, MenuInput action, bool down, bool repeated, double seconds) {
	if (action < MenuInput::Next || action > MenuInput::Back) return;
	Button(source,RoutedInput::Kind::Menu,action,down,repeated,seconds);
}
void Input::Pointer(std::uint32_t source, bool down, double seconds) {
	Button(source,RoutedInput::Kind::PointerButton,MenuInput::Accept,down,false,seconds);
}
void Input::Button(std::uint32_t id, RoutedInput::Kind kind, MenuInput action, bool down, bool repeated, double seconds) {
	if (std::isfinite(seconds)) clock = std::max(clock,seconds);
	auto found = sources.find(id);
	if (down) {
		if (found != sources.end() || repeated || sources.size() >= 1024) return;
		const bool held = Held(kind,action);
		auto [entry,inserted] = sources.emplace(id,Source{kind,action,false,++serial});
		if (!held) Emit(entry->second,true);
	} else {
		if (found == sources.end()) return;
		const Source previous = found->second; sources.erase(found);
		// The action is captured at key-down. Releasing Shift while Tab is held
		// must not release a different navigation action.
		if (!previous.blocked && !Held(previous.kind,previous.action)) Emit(previous,false);
	}
	ChooseRepeat(clock);
}
void Input::ChooseRepeat(double seconds) {
	std::uint64_t newest = 0;
	for (const auto& [id,source] : sources)
		if (!source.blocked && source.kind == RoutedInput::Kind::Menu && source.action <= MenuInput::Right)
			newest = std::max(newest,source.serial);
	if (newest != repeatSerial) { repeatSerial = newest; nextRepeat = seconds+.320; }
}
void Input::Advance(double seconds) {
	if (!std::isfinite(seconds)) return;
	clock = std::max(clock,seconds);
	if (!repeatSerial || clock < nextRepeat) return;
	for (const auto& [id,source] : sources) if (source.serial == repeatSerial && !source.blocked) {
		Emit(source,true);
		// One repeat at most per presented frame. A stall cannot run through an
		// entire list or replay elapsed input when the application resumes.
		nextRepeat = clock+.110;
		break;
	}
}
void Input::Cancel(bool forgetSources) {
	events.clear(); events.push_back({RoutedInput::Kind::Cancel,MenuInput::Accept,false});
	// Release the runtime's logical arms even if a development command or a
	// previous adapter supplied the down edge. Source quarantine still prevents
	// an old physical hold from rearming the replacement document.
	events.push_back({RoutedInput::Kind::Menu,MenuInput::Accept,false});
	events.push_back({RoutedInput::Kind::Menu,MenuInput::Back,false});
	events.push_back({RoutedInput::Kind::PointerButton,MenuInput::Accept,false});
	for (const auto& [id,source] : sources)
		if (!source.blocked && source.kind == RoutedInput::Kind::Menu && source.action <= MenuInput::Right) Emit(source,false);
	if (forgetSources) sources.clear();
	else for (auto& [id,source] : sources) source.blocked = true;
	repeatSerial = 0;
}
std::vector<RoutedInput> Input::Take() { std::vector<RoutedInput> result; result.swap(events); return result; }
} // namespace openq4::ui
