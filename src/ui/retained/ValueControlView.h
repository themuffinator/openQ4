// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#pragma once
#include "Interaction.h"
#include <functional>
#include <memory>

namespace Rml { class Element; class ElementDocument; }
namespace openq4::ui {
struct PointerPartResult {
	std::string control, option;
	std::optional<double> fraction;
	bool invalidProjection = false;
};
// Derived RmlUi presentation only. Canonical node IDs and source remain intact;
// no widget readback, action, binding or persistent value is changed here.
class ValueControlView {
public:
	ValueControlView();
	~ValueControlView();
	void Reset();
	bool Initialize(const DocumentModel& model, Rml::ElementDocument& document,
		std::function<std::string(const std::string&)> translate, std::string& error);
	// Returns true when derived properties changed and another layout is needed.
	bool Paint(const Interaction& interaction, const std::map<std::string,ControlReadback>& readbacks,
		int width, int height, float dpRatio, const std::function<double(const std::string&)>& opacity);
	// Use the actual clipped Rml hit, except a captured slider projects outside
	// its track. Coordinates are the same physical viewport pixels as RmlUi.
	PointerPartResult PointerPart(Rml::Element* actualHit, float x, float y, const Interaction& interaction) const;
private:
	struct Impl;
	std::unique_ptr<Impl> impl;
};
} // namespace openq4::ui
