// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#pragma once
#include "Interaction.h"
#include <cstdint>

namespace openq4::ui {
struct RoutedInput {
	enum class Kind { Menu, PointerButton, Cancel } kind = Kind::Menu;
	MenuInput menu = MenuInput::Accept;
	bool down = false;
};

// Device-independent source aggregation and presentation-clock navigation
// repeat. Platform adapters supply stable source IDs and actual edge/repeat
// flags; this class neither polls devices nor synthesizes platform events.
class Input {
public:
	void Menu(std::uint32_t source, MenuInput action, bool down, bool repeated, double seconds);
	void Pointer(std::uint32_t source, bool down, double seconds);
	void Advance(double seconds);
	// Cancellation precedes releases, so no release can activate an armed target.
	// Keep held sources across document changes; forget them on focus loss where
	// releases may occur outside the application. Unpaired OS repeats are ignored.
	void Cancel(bool forgetSources = false);
	std::vector<RoutedInput> Take();
private:
	struct Source { RoutedInput::Kind kind; MenuInput action; bool blocked = false; std::uint64_t serial = 0; };
	void Button(std::uint32_t source, RoutedInput::Kind kind, MenuInput action, bool down, bool repeated, double seconds);
	void Emit(const Source& source, bool down);
	void ChooseRepeat(double seconds);
	bool Held(RoutedInput::Kind kind, MenuInput action) const;
	std::map<std::uint32_t,Source> sources;
	std::vector<RoutedInput> events;
	std::uint64_t serial = 0, repeatSerial = 0;
	double clock = 0, nextRepeat = 0;
};
} // namespace openq4::ui
