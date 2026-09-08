// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#pragma once
#include "Vector.h"
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/Geometry.h>

namespace openq4::ui {
class Host;
class VectorElement final : public Rml::Element {
public:
	explicit VectorElement(const Rml::String& tag) : Rml::Element(tag) {}
	void Configure(const std::vector<VectorPath>& paths, Host& host);
protected:
	void OnRender() override;
private:
	std::vector<VectorPath> paths;
	std::vector<Rml::Geometry> geometry;
	Host* host = nullptr;
	std::array<double,9> previous{};
	bool valid = false;
};
} // namespace openq4::ui
