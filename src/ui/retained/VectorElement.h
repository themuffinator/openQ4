// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#pragma once
#include "Vector.h"
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/Geometry.h>
#include <RmlUi/Core/Decorator.h>

namespace openq4::ui {
class Host;
struct Node;
struct RuntimeStatistics;
class VectorGeometry {
public:
	void Configure(const std::vector<VectorPath>& paths, Host& host, RuntimeStatistics& statistics);
	void Render(Rml::Element& element, bool inheritOpacity);
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
class VectorElement final : public Rml::Element {
public:
	explicit VectorElement(const Rml::String& tag) : Rml::Element(tag) {}
	void Configure(const Node& node, Host& host, RuntimeStatistics& statistics);
	void RenderMask() { mask.Render(*this,false); }
protected:
	void OnRender() override { paint.Render(*this,true); }
private:
	VectorGeometry paint, mask;
};
class VectorMaskInstancer final : public Rml::DecoratorInstancer {
public:
	VectorMaskInstancer();
	Rml::SharedPtr<Rml::Decorator> InstanceDecorator(const Rml::String&, const Rml::PropertyDictionary&,
		const Rml::DecoratorInstancerInterface&) override;
};
} // namespace openq4::ui
