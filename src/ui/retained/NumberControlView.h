// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#pragma once
#include "Runtime.h"
#include <functional>
#include <string_view>

namespace Rml { class Element; class ElementDocument; }
namespace openq4::ui {
struct TextRun;
using NumberTextRunQuery = std::function<std::shared_ptr<const TextRun>(std::uintptr_t,std::string_view,float)>;
using NumberValidationText = std::function<std::string(const NumberSpec&,const NumberEditView&,bool)>;
struct NumberTextHit {
	std::string control;
	NumberEditIdentity identity;
	std::size_t byteOffset = 0;
};
// Derived field ink and hit geometry. The Interaction buffer is the only text
// authority. No native input, clipboard, settings, focus or action is changed.
class NumberControlView {
public:
	NumberControlView();
	~NumberControlView();
	void Reset();
	bool Initialize(const DocumentModel&, Rml::ElementDocument&, NumberTextRunQuery,
		std::function<std::string(const std::string&)> translate,
		NumberValidationText validation, std::string& error);
	// Inactive drafts retain their text/validation, with no editing ink. Failed
	// or pending font/layout queries hide stale ink; Hit/Geometry additionally
	// require the actual Rml line and boxes to match the settled painted run.
	bool Paint(const Interaction&, float dpRatio, double seconds, bool steadyCaret);
	// Captured control permits dragging outside its viewport; ordinary hits use
	// RmlUi's actual clipped element and must lie inside the field viewport.
	std::optional<NumberTextHit> Hit(Rml::Element* actualHit,float x,float y,
		const Interaction&,const std::string& captured = {}) const;
	std::optional<NumberTextGeometry> Geometry(const std::string&,const Interaction&) const;
private:
	struct Impl;
	std::unique_ptr<Impl> impl;
};
} // namespace openq4::ui
