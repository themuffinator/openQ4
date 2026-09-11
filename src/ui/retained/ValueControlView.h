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
	bool invalidProjection = false, choiceTrack = false, thumb = false, pendingLayout = false;
};
// Derived RmlUi presentation. Canonical source and application values remain
// intact. Paint publishes measured local Choice scroll readback and consumes
// its pending offset restoration; it never emits application actions.
class ValueControlView {
public:
	ValueControlView();
	~ValueControlView();
	void Reset();
	bool Initialize(const DocumentModel& model, Rml::ElementDocument& document,
		std::function<std::string(const std::string&)> translate, std::string& error);
	// Returns true when derived properties changed and another layout is needed.
	bool Paint(Interaction& interaction, const std::map<std::string,ControlReadback>& readbacks,
		int width, int height, float dpRatio, const std::function<double(const std::string&)>& opacity);
	// Use the actual clipped Rml hit, except a captured slider projects outside
	// its track. Coordinates are the same physical viewport pixels as RmlUi.
	bool ChoiceScrollFresh(const std::string&, const Interaction&) const;
	// Read-only geometry check before every input arbitration. Retires only an
	// exact constrained opening when its previously measured placement changed.
    void ValidatePlacement(Interaction&, int width, int height, float dpRatio, bool requireReady = false) const;
    bool HasConstrainedPopup(const Interaction&) const;
	PointerPartResult PointerPart(Rml::Element* actualHit, float x, float y, const Interaction& interaction) const;
private:
	struct Impl;
	std::unique_ptr<Impl> impl;
};
} // namespace openq4::ui
