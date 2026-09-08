// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#pragma once
#include "Vector.h"
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/Geometry.h>

namespace openq4::ui {
class Host;
struct RuntimeStatistics;
class VectorElement final : public Rml::Element {
public:
	explicit VectorElement(const Rml::String& tag) : Rml::Element(tag) {}
	void Configure(const std::vector<VectorPath>& paths, Host& host, RuntimeStatistics& statistics);
protected:
	void OnRender() override;
private:
	std::vector<VectorPath> paths;
	std::vector<VectorMesh> compiled;
	std::vector<Rml::Geometry> geometry;
	Host* host = nullptr;
	RuntimeStatistics* statistics = nullptr;
	std::array<double,12> previous{};
	double previousOpacity = -1;
	bool valid = false;
};
} // namespace openq4::ui
