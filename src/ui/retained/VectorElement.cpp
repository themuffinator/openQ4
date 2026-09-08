// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "VectorElement.h"
#include "Runtime.h"
#include <RmlUi/Core/ComputedValues.h>
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/RenderManager.h>
#include <algorithm>
#include <chrono>
#include <cmath>

namespace openq4::ui {
void VectorElement::Configure(const std::vector<VectorPath>& source, Host& owner, RuntimeStatistics& measurements) {
	paths = source; host = &owner; statistics = &measurements;
	compiled.clear(); geometry.clear(); valid = false; previousOpacity = -1;
}
void VectorElement::OnRender() {
	auto* manager = GetRenderManager();
	if (!host || !manager || !GetContext()) return;
	++statistics->vectorElements;
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
	for (double value : {options.transform.a,options.transform.b,options.transform.c,options.transform.d,options.transform.tx,options.transform.ty}) {
		if (!std::isfinite(value)) { host->Log(true,"Vector "+GetId()+": non-finite output transform"); return; }
	}
	const auto viewport = manager->GetViewport();
	VectorPixelBounds bounds{0,0,viewport.x,viewport.y};
	if (state.scissor_region.Valid()) {
		bounds.left = std::clamp(state.scissor_region.Left(),0,viewport.x);
		bounds.top = std::clamp(state.scissor_region.Top(),0,viewport.y);
		bounds.right = std::clamp(state.scissor_region.Right(),bounds.left,viewport.x);
		bounds.bottom = std::clamp(state.scissor_region.Bottom(),bounds.top,viewport.y);
	}
	if (bounds.left == bounds.right || bounds.top == bounds.bottom) return;
	// Whole-pixel translation preserves coverage. Keep only the fractional
	// phase in the compiled mesh and apply its integer part at submission.
	const double offsetX = std::floor(options.transform.tx), offsetY = std::floor(options.transform.ty);
	options.transform.tx -= offsetX; options.transform.ty -= offsetY;
	// Cache a padded, quantized region in that translated coordinate space.
	// This prevents every small movement from changing the viewport cache key.
	// The render manager still clips to the exact current scissor rectangle.
	auto lower = [](double value) { return static_cast<int>(std::clamp(std::floor(value/256)*256,-1000000.0,1000000.0)); };
	auto upper = [](double value) { return static_cast<int>(std::clamp(std::ceil(value/256)*256,-1000000.0,1000000.0)); };
	const VectorPixelBounds cachedBounds{lower(bounds.left-offsetX),lower(bounds.top-offsetY),upper(bounds.right-offsetX),upper(bounds.bottom-offsetY)};
	options.pixelBounds = cachedBounds;
	const double opacity = std::clamp(static_cast<double>(GetComputedValues().opacity()),0.0,1.0);
	const std::array<double,12> signature{options.widthDp,options.heightDp,options.transform.a,options.transform.b,
		options.transform.c,options.transform.d,options.transform.tx,options.transform.ty,
		static_cast<double>(cachedBounds.left),static_cast<double>(cachedBounds.top),static_cast<double>(cachedBounds.right),static_cast<double>(cachedBounds.bottom)};
	const bool rebuild = !valid || signature != previous;
	if (rebuild) {
		compiled.clear(); previous = signature; valid = true;
		for (const auto& path : paths) {
			VectorMesh result; std::string error;
			const auto start = std::chrono::steady_clock::now();
			const bool success = TessellatePath(path,options,result,error);
			statistics->vectorCompileMilliseconds += std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
			++statistics->vectorPathsCompiled;
			if (!success) { host->Log(true,"Vector "+GetId()+"/"+error); continue; }
			if (!result.indices.empty()) compiled.push_back(std::move(result));
		}
	} else ++statistics->vectorCacheHits;
	for (const auto& mesh : compiled) statistics->visibleVectorCacheBytes += mesh.vertices.capacity()*sizeof(VectorVertex)+mesh.indices.capacity()*sizeof(int);
	if (rebuild || opacity != previousOpacity) {
		geometry.clear(); previousOpacity = opacity;
		for (const auto& cached : compiled) {
			const auto upload = std::chrono::steady_clock::now();
			Rml::Mesh mesh;
			mesh.vertices.reserve(cached.vertices.size());
			auto channel = [&](double value) { return static_cast<Rml::byte>(std::round(std::clamp(value*opacity,0.0,1.0)*255)); };
			for (const auto& v : cached.vertices) mesh.vertices.push_back({
				{static_cast<float>(v.x),static_cast<float>(v.y)},
				Rml::ColourbPremultiplied(channel(v.r),channel(v.g),channel(v.b),channel(v.a)),{0,0}});
			mesh.indices.assign(cached.indices.begin(),cached.indices.end());
			geometry.push_back(manager->MakeGeometry(std::move(mesh)));
			++statistics->vectorUploads;
			statistics->vectorUploadMilliseconds += std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-upload).count();
		}
	}
	// Geometry is already in output pixels. Submit through the render manager
	// to retain its clipping/lifetime/order contract without a second transform.
	manager->SetTransform(nullptr);
	for (const auto& mesh : geometry) mesh.Render({static_cast<float>(offsetX),static_cast<float>(offsetY)});
	manager->SetState(state);
}
} // namespace openq4::ui
