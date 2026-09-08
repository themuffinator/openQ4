// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "VectorElement.h"
#include "Runtime.h"
#include <RmlUi/Core/ComputedValues.h>
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/RenderManager.h>
#include <algorithm>
#include <cmath>

namespace openq4::ui {
void VectorElement::Configure(const std::vector<VectorPath>& source, Host& owner) {
	paths = source; host = &owner; geometry.clear(); valid = false;
}
void VectorElement::OnRender() {
	auto* manager = GetRenderManager();
	if (!host || !manager || !GetContext()) return;
	const float density = GetContext()->GetDensityIndependentPixelRatio();
	const auto size = GetBox().GetSize(Rml::BoxArea::Border);
	if (density <= 0 || size.x <= 0 || size.y <= 0) return;
	const auto offset = GetAbsoluteOffset(Rml::BoxArea::Border);
	const auto state = manager->GetState();
	auto project = [&](float x, float y) {
		const auto p = state.transform*Rml::Vector4f(offset.x+x,offset.y+y,0,1);
		return p;
	};
	const auto origin = project(0,0), x = project(density,0), y = project(0,density);
	// Canonical transforms are currently affine. Perspective world surfaces
	// will require projected subdivision and their own surface-density contract.
	if (origin.w <= 0 || std::abs(x.w-origin.w) > 1e-6f || std::abs(y.w-origin.w) > 1e-6f) return;
	VectorOptions options;
	options.widthDp = size.x/density; options.heightDp = size.y/density;
	options.transform = {(x.x-origin.x)/origin.w,(x.y-origin.y)/origin.w,
		(y.x-origin.x)/origin.w,(y.y-origin.y)/origin.w,origin.x/origin.w,origin.y/origin.w};
	const double opacity = std::clamp(static_cast<double>(GetComputedValues().opacity()),0.0,1.0);
	const std::array<double,9> signature{options.widthDp,options.heightDp,options.transform.a,options.transform.b,
		options.transform.c,options.transform.d,options.transform.tx,options.transform.ty,opacity};
	if (!valid || signature != previous) {
		geometry.clear(); previous = signature; valid = true;
		for (const auto& path : paths) {
			VectorMesh compiled; std::string error;
			if (!TessellatePath(path,options,compiled,error)) { host->Log(true,"Vector "+GetId()+"/"+error); continue; }
			if (compiled.indices.empty()) continue;
			Rml::Mesh mesh;
			mesh.vertices.reserve(compiled.vertices.size());
			auto channel = [&](double value) { return static_cast<Rml::byte>(std::round(std::clamp(value*opacity,0.0,1.0)*255)); };
			for (const auto& v : compiled.vertices) mesh.vertices.push_back({
				{static_cast<float>(v.x),static_cast<float>(v.y)},
				Rml::ColourbPremultiplied(channel(v.r),channel(v.g),channel(v.b),channel(v.a)),{0,0}});
			mesh.indices.assign(compiled.indices.begin(),compiled.indices.end());
			geometry.push_back(manager->MakeGeometry(std::move(mesh)));
		}
	}
	// Geometry is already in output pixels. Submit through the render manager
	// to retain its clipping/lifetime/order contract without a second transform.
	manager->SetTransform(nullptr);
	for (const auto& mesh : geometry) mesh.Render({0,0});
	manager->SetState(state);
}
} // namespace openq4::ui
