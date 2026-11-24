// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "VectorElement.h"
#include "Runtime.h"
#include <RmlUi/Core/ComputedValues.h>
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/ElementUtilities.h>
#include <RmlUi/Core/RenderManager.h>
#include <RmlUi/Core/PropertyDefinition.h>
#include <RmlUi/Core/StringUtilities.h>
#include <RmlUi/Core/StyleSheetTypes.h>
#include <RmlUi/Core/Transform.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>

namespace openq4::ui {
namespace {
// Single-precision transform composition jitters a still element's sub-pixel
// phase by about 1e-4 px between frames, e.g. under a rotation its child turns
// back (the emblem glint's rim). Coverage moves no further than the phase, so
// a change under 1/1024 px (a quarter of one 8-bit coverage step) keeps the
// compiled mesh. Test linear-transform noise over the whole clipped region,
// including artwork outside its authored box, rather than rounding angles.
bool SameCompilation(const std::array<double,12>& current, const std::array<double,12>& compiled) {
	for (size_t i : {0u,1u,8u,9u,10u,11u}) if (current[i]!=compiled[i]) return false;
	constexpr double tolerance=1.0/1024;
	if (std::equal(current.begin()+2,current.begin()+6,compiled.begin()+2))
		return std::abs(current[6]-compiled[6])<tolerance && std::abs(current[7]-compiled[7])<tolerance;
	// Local meshes have no output-space coverage bounds. Their scale changes
	// still require recompilation to keep the curve subdivision tolerance.
	if (compiled[8]>=compiled[10] || compiled[9]>=compiled[11]) return false;
	const VectorTransform before{compiled[2],compiled[3],compiled[4],compiled[5],compiled[6],compiled[7]};
	const VectorTransform after{current[2],current[3],current[4],current[5],current[6],current[7]};
	VectorTransform inverse;
	if (!before.Inverse(inverse)) return false;
	for (double x:{compiled[8],compiled[10]}) for (double y:{compiled[9],compiled[11]}) {
		const auto moved=after.Apply(inverse.Apply({x,y}));
		if (!(std::abs(moved.x-x)<tolerance && std::abs(moved.y-y)<tolerance)) return false;
	}
	return true;
}
}
namespace {
VectorCompileBatch* deferredCompilations = nullptr;
// An element that keeps sliding (a looping drift such as falling snow) visits
// every quarter-pixel phase in both axes, sixteen in all, more than its cache
// keeps, so it recompiled on nearly every frame. After this many consecutive
// sliding renders it compiles at half-pixel phases instead: four, which the
// cache holds, so a steady drift compiles only as it crosses into another
// cache region. A short move (a step, a settle) keeps quarter-pixel phases.
constexpr int SlidingHalfPhaseRenders = 8;
// A vector of this many paths or more (a snow layer, a band's frost, a grid)
// uploads consecutive meshes that draw alike as one geometry: one draw rather
// than one per path. A smaller one (a plate of a few overlapping paths) keeps
// a geometry per path, which is how a layer recognises a compound plate that
// needs, and may cache, its isolated alpha (Runtime.cpp).
constexpr size_t MergedGeometryPaths = 16;
// One RmlUi geometry holds at most this many vertices (Runtime.cpp).
constexpr size_t MergedGeometryVertices = 262144;
}
void DeferVectorCompilations(VectorCompileBatch* batch) { deferredCompilations = batch; }
size_t VectorCompileBatch::Paths() const {
	size_t count = 0;
	for (const auto& request : requests) count += request.paths ? request.paths->size() : 0;
	return count;
}
void VectorCompileBatch::Run(size_t index) {
	auto& request = requests[index];
	for (const auto& path : *request.paths) {
		VectorMesh result; std::string error;
		if (!TessellatePath(path,request.options,result,error)) { request.errors.push_back("Vector "+request.element+"/"+error); continue; }
		if (!result.indices.empty()) { request.meshes.push_back(std::move(result)); request.blends.push_back(path.blend); }
	}
	request.compiled = true;
}
size_t VectorCompileBatch::Install() {
	size_t installed = 0;
	for (auto& request : requests) {
		const auto owner = request.owner.lock();
		if (!owner || !*owner || !request.compiled) continue;
		auto& geometry = **owner;
		// Drawn, compiled or reconfigured since: the element has its own.
		if (geometry.compileGeneration != request.generation || geometry.valid || geometry.renderReady || !geometry.cache.empty()) continue;
		if (geometry.host) for (const auto& error : request.errors) geometry.host->Log(true,error);
		VectorGeometry::Compilation fresh; fresh.signature = request.signature;
		fresh.meshes = std::move(request.meshes); fresh.blends = std::move(request.blends);
		geometry.cache.push_back(std::move(fresh));
		++installed;
	}
	requests.clear();
	return installed;
}
bool VectorGeometry::ArtworkExtent(const VectorOptions& options, double extent[4]) {
	auto& local = localExtent;
	if (local.generation != compileGeneration || local.width != options.widthDp || local.height != options.heightDp) {
		local = {}; local.generation = compileGeneration; local.width = options.widthDp; local.height = options.heightDp;
		double box[4] = {std::numeric_limits<double>::infinity(),std::numeric_limits<double>::infinity(),
			-std::numeric_limits<double>::infinity(),-std::numeric_limits<double>::infinity()};
		bool finite = true;
		for (const auto& path : paths) {
			if (path.stroke.paint.type != PaintType::None) {
				const auto& stroke = path.stroke;
				if (!std::isfinite(stroke.widthDp) || !std::isfinite(stroke.minimumPixels) || !std::isfinite(stroke.miterLimit)) finite = false;
				local.stroked = true;
				local.strokeDp = std::max(local.strokeDp,stroke.widthDp);
				local.strokePixels = std::max(local.strokePixels,stroke.minimumPixels);
				local.strokeMiter = std::max(local.strokeMiter,stroke.miterLimit);
			}
			for (const auto& command : path.commands) {
				const unsigned count = command.op == PathOperation::Cubic ? 3 : command.op == PathOperation::Quadratic ? 2 :
					command.op == PathOperation::Close ? 0 : 1;
				for (unsigned i = 0; i < count; ++i) {
					const auto p = command.points[i].Resolve(options.widthDp,options.heightDp);
					if (!std::isfinite(p.x) || !std::isfinite(p.y)) finite = false;
					box[0] = std::min(box[0],p.x); box[1] = std::min(box[1],p.y);
					box[2] = std::max(box[2],p.x); box[3] = std::max(box[3],p.y);
				}
			}
		}
		local.valid = finite && box[0] <= box[2] && box[1] <= box[3];
		local.left = box[0]; local.top = box[1]; local.right = box[2]; local.bottom = box[3];
	}
	if (!local.valid) return false;
	const auto& t = options.transform;
	const double maximumScale = t.MaximumScale();
	const double determinant = std::abs(t.a*t.d-t.b*t.c);
	if (!std::isfinite(maximumScale) || maximumScale <= 0 || !std::isfinite(determinant) || determinant <= 0) return false;
	const double minimumScale = determinant/maximumScale;
	// The tessellator's stroke half-width in dp (StrokeContours): a miter
	// join reaches miterLimit of it, a square cap sqrt(2), a round one 1.
	double reach = 1;
	if (local.stroked) {
		const double half = .5*std::max(local.strokeDp,local.strokePixels/minimumScale);
		reach += half*std::max(local.strokeMiter,std::sqrt(2.0))*maximumScale;
	}
	// An affine map keeps every point inside the image of the box's corners.
	extent[0] = extent[1] = std::numeric_limits<double>::infinity();
	extent[2] = extent[3] = -std::numeric_limits<double>::infinity();
	for (const double x : {local.left,local.right}) for (const double y : {local.top,local.bottom}) {
		const auto p = t.Apply({x,y});
		extent[0] = std::min(extent[0],p.x); extent[1] = std::min(extent[1],p.y);
		extent[2] = std::max(extent[2],p.x); extent[3] = std::max(extent[3],p.y);
	}
	extent[0] -= reach; extent[1] -= reach; extent[2] += reach; extent[3] += reach;
	return std::isfinite(extent[0]) && std::isfinite(extent[1]) && std::isfinite(extent[2]) && std::isfinite(extent[3]) &&
		extent[0] <= extent[2] && extent[1] <= extent[3];
}
void VectorGeometry::Configure(const std::vector<VectorPath>& source, Host& owner, RuntimeStatistics& measurements) {
	paths = source; host = &owner; statistics = &measurements;
	sharedPaths.reset(); ++compileGeneration;
	replayMask.reset();
	cache.clear(); valid = renderReady = false; slidingRenders = 0;
	hitGeometry.clear(); hitPrepared = hitValid = hitArea = false;
}
void VectorGeometry::CopyArtworkFrom(const VectorGeometry& source) {
	paths = source.paths; host = source.host; statistics = source.statistics; linearMasks=source.linearMasks; multiply = {}; coverage = {};
	self.reset(); sharedPaths.reset(); ++compileGeneration;
	replayMask.reset();
	cache.clear(); valid = renderReady = false; slidingRenders = 0;
	hitGeometry.clear(); hitPrepared = hitValid = hitArea = false;
}
void VectorElement::CopyArtworkFrom(const VectorElement& source) {
	measurements=source.measurements;
	paint.CopyArtworkFrom(source.paint); mask.CopyArtworkFrom(source.mask);
	hasMask = source.hasMask;
	replayProjection = source.replayProjection;
	legacyText=source.legacyText; legacyTextClip=source.legacyTextClip; textHost=source.textHost; text=source.text;
	textItems.clear(); textGeometry.clear(); textTextures.clear();
	textLayoutValid=false;
	coordinatesImage=source.coordinatesImage; additiveImage=source.additiveImage; imageSource=source.imageSource;
	imageCoordinates=std::make_shared<ImageCoordinates>(*source.imageCoordinates); imageSampling=source.imageSampling;
	replayOpacity=source.replayOpacity ? std::make_shared<float>(*source.replayOpacity) : nullptr;
	backdropFault=source.backdropFault ? std::make_shared<std::array<float,3>>(*source.backdropFault) : nullptr;
	replayText=source.replayText;
	imageTexture={}; imageGeometry={};
}
bool VectorGeometry::PrepareHitGeometry(Rml::Element& element, Rml::Vector2f& origin) {
	if (!host || !element.GetContext()) return false;
	const double density = element.GetContext()->GetDensityIndependentPixelRatio();
	const auto size = element.GetBox().GetSize(Rml::BoxArea::Border);
	Rml::Array<Rml::Vector2f,4> quad;
	if (!std::isfinite(density) || density <= 0 || size.x <= 0 || size.y <= 0 ||
		!Rml::ElementUtilities::GetBorderBoxQuad(quad,&element)) return false;
	// The renderer currently supports affine canonical surfaces only. Do not
	// invent a different projected hit surface for unsupported perspective.
	if ((quad[2]-(quad[1]+quad[3]-quad[0])).Magnitude() > .05f) return false;
	origin = quad[0];
	VectorOptions options;
	options.widthDp = size.x/density; options.heightDp = size.y/density;
	options.transform = {(quad[1].x-origin.x)/options.widthDp,(quad[1].y-origin.y)/options.widthDp,
		(quad[3].x-origin.x)/options.heightDp,(quad[3].y-origin.y)/options.heightDp,0,0};
	options.antialias = false; options.tolerancePixels = .025;
	const std::array<double,6> signature{options.widthDp,options.heightDp,options.transform.a,
		options.transform.b,options.transform.c,options.transform.d};
	if (hitPrepared && signature == hitSignature) { ++statistics->vectorHitCacheHits; return hitValid; }
	hitSignature = signature; hitPrepared = true; hitValid = hitArea = false; hitGeometry.clear();
	for (const auto& path : paths) {
		// Light and shadow layers are decoration; they never enlarge a target.
		if (path.blend != PathBlend::Normal) continue;
		VectorMesh mesh; std::string error;
		++statistics->vectorHitPathsCompiled;
		if (!TessellatePath(path,options,mesh,error)) {
			host->Log(true,"Vector mask input "+element.GetId()+"/"+error);
			hitGeometry.clear(); hitArea = false; return false;
		}
		for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
			const auto& a = mesh.vertices[mesh.indices[i]];
			const auto& b = mesh.vertices[mesh.indices[i+1]];
			const auto& c = mesh.vertices[mesh.indices[i+2]];
			if (std::abs((b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x)) > 1e-20 &&
				(a.a > 0 || b.a > 0 || c.a > 0)) hitArea = true;
		}
		hitGeometry.push_back(std::move(mesh));
	}
	hitValid = true; return true;
}
bool VectorGeometry::HasHitArea(Rml::Element& element) {
	Rml::Vector2f origin;
	return PrepareHitGeometry(element,origin) && hitArea;
}
bool VectorGeometry::HitTest(Rml::Element& element, Rml::Vector2f point) {
	Rml::Vector2f origin;
	if (!PrepareHitGeometry(element,origin) || !hitArea) return false;
	const VectorPoint local{point.x-origin.x,point.y-origin.y};
	for (const auto& mesh : hitGeometry) if (HitTestPaintedMesh(mesh,local)) return true;
	return false;
}
void VectorGeometry::Render(Rml::Element& element, bool inheritOpacity) {
	auto* manager = element.GetRenderManager();
	if (paths.empty() || !host || !manager || !element.GetContext()) return;
	// A frame drawn out of sight gathers the compilations of elements that have
	// never drawn; one that has keeps what it has.
	auto* const deferred = deferredCompilations;
	if (deferred && (valid || renderReady || !cache.empty())) return;
	if (!deferred) ++statistics->vectorElements;
	const float density = element.GetContext()->GetDensityIndependentPixelRatio();
	const auto size = element.GetBox().GetSize(Rml::BoxArea::Border);
	if (density <= 0 || size.x <= 0 || size.y <= 0) return;
	const auto offset = element.GetAbsoluteOffset(Rml::BoxArea::Border);
	const auto state = manager->GetState();
	const auto viewport = manager->GetViewport();
	const double opacity = inheritOpacity ? std::clamp(static_cast<double>(element.GetComputedValues().opacity()),0.0,1.0) : 1;
	// An element faded out at rest (a focus glow) compiles out of sight too:
	// its first fade-in then finds its meshes.
	if (opacity<=0 && !deferred) return;
	const std::array<float,6> inputs{density,size.x,size.y,offset.x,offset.y,float(opacity)};
	const auto drawCached=[&] {
		const auto& entry=cache.front();
		manager->SetTransform(cachedLocalMesh ? &cachedTransform : nullptr);
		for (size_t i=0;i<entry.geometry.size();++i) {
			if (entry.multiplied[i]) entry.geometry[i].Render(cachedTranslation,multiply);
			else if (cachedPartition) entry.geometry[i].Render(cachedTranslation,coverage);
			else entry.geometry[i].Render(cachedTranslation);
		}
		manager->SetState(state);
	};
	// Layout, output transform, clip and opacity fully determine a compiled
	// vector's submission. A settled instrument need not recompute coverage
	// phases, projected bounds or path classification for every frame.
	if (renderReady && !cache.empty() && inputs==renderInputs && inheritOpacity==renderInheritOpacity &&
		state.transform==renderTransform && state.scissor_region==renderScissor && viewport==renderViewport) {
		++statistics->vectorCacheHits;
		for (const auto& mesh:cache.front().meshes) statistics->visibleVectorCacheBytes+=mesh.vertices.capacity()*sizeof(VectorVertex)+mesh.indices.capacity()*sizeof(int);
		drawCached(); return;
	}
	renderReady=false;
	auto project = [&](float x, float y) {
		const auto p = state.transform*Rml::Vector4f(offset.x+x,offset.y+y,0,1);
		return p;
	};
	const auto origin = project(0,0);
	// Transform the basis as directions. Subtracting two translated float
	// positions loses precision for small elements far from the view origin,
	// changing coverage even when two identical shapes differ by whole pixels.
	const auto x = state.transform*Rml::Vector4f(density,0,0,0);
	const auto y = state.transform*Rml::Vector4f(0,density,0,0);
	// Canonical transforms are currently affine. Perspective world surfaces
	// will require projected subdivision and their own surface-density contract.
	if (origin.w <= 0 || std::abs(x.w) > 1e-6f || std::abs(y.w) > 1e-6f) return;
	VectorOptions options;
	// A mask edge can cut across high-contrast glyphs and every descendant.
	// Use a tighter physical-pixel curve bound before analytic area coverage.
	if (!inheritOpacity) options.tolerancePixels = .025;
	options.widthDp = size.x/density; options.heightDp = size.y/density;
	options.transform = {x.x/origin.w,x.y/origin.w,
		y.x/origin.w,y.y/origin.w,origin.x/origin.w,origin.y/origin.w};
	for (double value : {options.transform.a,options.transform.b,options.transform.c,options.transform.d,options.transform.tx,options.transform.ty}) {
		if (!std::isfinite(value)) { host->Log(true,"Vector "+element.GetId()+": non-finite output transform"); return; }
	}
	// Faint/soft artwork authored without pixel coverage has no screen-space
	// edges to rebuild. Compile it in local pixels and let the render manager
	// transform the cached triangles. Keep analytic masks and strokes (whose
	// width can depend on the output transform) on their exact output-space path.
	const bool localMesh = inheritOpacity && std::all_of(paths.begin(),paths.end(),[](const VectorPath& path) {
		return !path.antialias && path.stroke.paint.type == PaintType::None;
	});
	Rml::Matrix4f localTransform = Rml::Matrix4f::Identity();
	if (localMesh) {
		const auto output = options.transform;
		// Round upwards with headroom: sub-pixel float noise during rotation must
		// not invalidate a mesh, and curve error stays below tolerance at this DPI.
		const double needed = output.MaximumScale();
		double scale = (std::round(needed*64)+1)/64;
		// A scale in motion (a zoom) would compile every frame. Keep a compilation
		// while it is finer than needed by at most a sqrt(2) step; a growing scale
		// compiles one step ahead. A scale at rest compiles exactly again.
		if (cachedLocalMesh && !cache.empty() && std::abs(needed-localScale) > needed*1e-4) {
			const double compiled = cache.front().signature[2], step = std::sqrt(2.0);
			if (compiled >= scale && compiled <= scale*step) scale = compiled;
			else if (needed > localScale) scale = (std::round(needed*step*64)+1)/64;
		}
		localScale = needed;
		localTransform.SetColumns({float(output.a/scale),float(output.b/scale),0,0},
			{float(output.c/scale),float(output.d/scale),0,0}, {0,0,1,0},
			{float(output.tx),float(output.ty),0,1});
		options.transform = {scale,0,0,scale,0,0};
	}
	// Keep the existing sub-1/1024-pixel cache tolerance continuous across
	// integer phases: cancellation noise around zero must not alternate
	// between phase 0 at offset 0 and phase 1 at offset -1.
	for (double* translation:{&options.transform.tx,&options.transform.ty}) {
		const double integer=std::round(*translation);
		if (std::abs(*translation-integer)<1.0/1024) *translation=integer;
	}
	VectorPixelBounds bounds{0,0,viewport.x,viewport.y};
	if (state.scissor_region.Valid()) {
		bounds.left = std::clamp(state.scissor_region.Left(),0,viewport.x);
		bounds.top = std::clamp(state.scissor_region.Top(),0,viewport.y);
		bounds.right = std::clamp(state.scissor_region.Right(),bounds.left,viewport.x);
		bounds.bottom = std::clamp(state.scissor_region.Bottom(),bounds.top,viewport.y);
	}
	if (bounds.left == bounds.right || bounds.top == bounds.bottom) return;
	// Only moving masks need this path: stationary masks keep their completed
	// layer/coverage caches. Admission submits no pixels on failure.
	if (!inheritOpacity && linearMasks && paths.size()==1 && valid &&
		(replayMask || previous[2]!=options.transform.a || previous[3]!=options.transform.b ||
		 previous[4]!=options.transform.c || previous[5]!=options.transform.d)) {
		LinearMask mask;
		const bool resolved=ResolveLinearMask(paths.front(),options,mask);
		if (resolved) {
			if (!replayMask) replayMask=std::make_shared<LinearMask>();
			*replayMask=mask;
		}
		if (resolved && linearMasks->SubmitLinearMask(mask,replayMask)) {
			previous[0]=options.widthDp; previous[1]=options.heightDp;
			previous[2]=options.transform.a; previous[3]=options.transform.b;
			previous[4]=options.transform.c; previous[5]=options.transform.d;
			previousX=options.transform.tx; previousY=options.transform.ty;
			cache.clear(); return;
		}
	}
	replayMask.reset();
	// An element sliding by translation alone since its last render: same size
	// and linear transform, another position.
	const double absoluteX = options.transform.tx, absoluteY = options.transform.ty;
	const bool sliding = valid && previous[0] == options.widthDp && previous[1] == options.heightDp && previous[2] == options.transform.a &&
		previous[3] == options.transform.b && previous[4] == options.transform.c && previous[5] == options.transform.d &&
		(absoluteX != previousX || absoluteY != previousY);
	previousX = absoluteX; previousY = absoluteY;
	// Whole-pixel translation preserves coverage. Keep only the fractional
	// phase in the compiled mesh and apply its integer part at submission.
	const double offsetX = std::floor(options.transform.tx), offsetY = std::floor(options.transform.ty);
	options.transform.tx -= offsetX; options.transform.ty -= offsetY;
	// Cache a padded, quantized region in that translated coordinate space.
	// This prevents every small movement from changing the viewport cache key.
	// The render manager still clips to the exact current scissor rectangle.
	auto lower = [](double value) { return static_cast<int>(std::clamp(std::floor(value/256)*256,-1000000.0,1000000.0)); };
	auto upper = [](double value) { return static_cast<int>(std::clamp(std::ceil(value/256)*256,-1000000.0,1000000.0)); };
	VectorPixelBounds cachedBounds{lower(bounds.left-offsetX),lower(bounds.top-offsetY),upper(bounds.right-offsetX),upper(bounds.bottom-offsetY)};
	// Artwork wholly inside the visible region compiles the same mesh whatever
	// region bounds it, so it keys on its own extent instead. A clip that grows
	// or moves over still artwork (a page opening) otherwise recompiled it
	// whenever an edge crossed into another cache region, dropping its
	// sub-pixel phases too.
	double extent[4];
	if (!localMesh && ArtworkExtent(options,extent) && extent[0] >= bounds.left-offsetX && extent[1] >= bounds.top-offsetY &&
			extent[2] <= bounds.right-offsetX && extent[3] <= bounds.bottom-offsetY) {
		cachedBounds = {lower(extent[0]),lower(extent[1]),upper(extent[2]),upper(extent[3])};
	}
	// Only where the view alone clips it: a scissored element keeps the visible
	// region, so its compilation still matches a fresh one vertex for vertex.
	else if (sliding && !state.scissor_region.Valid()) {
		// The view's region slides across a moving element, and each new block
		// would recompile it (a scrolling data layer several views tall). Its
		// own box does not move in this space: cover that as well, while the
		// two are no larger than a few views, and keep the phases' compilations.
		// The view's region stays covered: a path may draw outside its box (a
		// framing band reaches far past the view so a dock slide never shows
		// its end), and the box alone cut the band where it had slid from.
		const auto& t = options.transform;
		const double xs[4] = {t.tx,t.tx+t.a*options.widthDp,t.tx+t.c*options.heightDp,t.tx+t.a*options.widthDp+t.c*options.heightDp};
		const double ys[4] = {t.ty,t.ty+t.b*options.widthDp,t.ty+t.d*options.heightDp,t.ty+t.b*options.widthDp+t.d*options.heightDp};
		const VectorPixelBounds own{lower(*std::min_element(xs,xs+4)),lower(*std::min_element(ys,ys+4)),
			upper(*std::max_element(xs,xs+4)),upper(*std::max_element(ys,ys+4))};
		const bool outgrows = own.left < cachedBounds.left || own.top < cachedBounds.top || own.right > cachedBounds.right ||
			own.bottom > cachedBounds.bottom;
		const VectorPixelBounds both{std::min(own.left,cachedBounds.left),std::min(own.top,cachedBounds.top),
			std::max(own.right,cachedBounds.right),std::max(own.bottom,cachedBounds.bottom)};
		const double bothArea = double(both.right-both.left)*double(both.bottom-both.top);
		if (outgrows && bothArea <= 4.0*std::max(1.0,double(viewport.x)*double(viewport.y))) cachedBounds = both;
	}
	if (localMesh) cachedBounds = {}; // Final clipping belongs to the render manager.
	else options.pixelBounds = cachedBounds;
	// Fully transparent geometry adds nothing under any blend: neither
	// tessellate nor submit it (a HUD keeps many faded nodes at zero).
	if (opacity <= 0 && !deferred) return;
	std::array<double,12> signature{options.widthDp,options.heightDp,options.transform.a,options.transform.b,
		options.transform.c,options.transform.d,options.transform.tx,options.transform.ty,
		static_cast<double>(cachedBounds.left),static_cast<double>(cachedBounds.top),static_cast<double>(cachedBounds.right),static_cast<double>(cachedBounds.bottom)};
	if (deferred) {
		// The compilation the element's first presented frame makes at rest,
		// with this signature and these options; nothing is submitted, and the
		// element keeps the state of one that has never drawn.
		previousX = previousY = 0; localScale = 0;
		if (!self) self = std::make_shared<VectorGeometry*>(this);
		if (!sharedPaths) sharedPaths = std::make_shared<const std::vector<VectorPath>>(paths);
		VectorCompileBatch::Request request;
		request.owner = self; request.generation = compileGeneration; request.paths = sharedPaths;
		request.options = options; request.signature = signature; request.element = element.GetId();
		deferred->requests.push_back(std::move(request));
		return;
	}
	// An element that moved by translation alone since its last render (a
	// scrolling trace, a sliding group) would compile for a new sub-pixel phase
	// every frame. While it moves it compiles at quarter-pixel phases (half-pixel
	// ones once it has kept sliding), keeps the last few, and places the
	// remaining fraction at submission. A resting element compiles at its exact
	// phase.
	bool moving = false;
	if (valid) {
		moving = !SameCompilation(signature,previous);
		// The region it compiles for (8 to 11) may change as it slides, under
		// a clip that moves with it or a region that follows its own box; it
		// is still moving, and keeps the compilations it made where that
		// region differed. An element that stays put under a changing clip
		// compiles exactly.
		moving = moving && sliding;
		for (size_t i = 0; moving && i < 6; ++i) if (signature[i] != previous[i]) moving = false;
	}
	previous = signature; valid = true;
	slidingRenders = moving ? std::min(slidingRenders+1,SlidingHalfPhaseRenders) : 0;
	double residualX = 0, residualY = 0;
	if (moving) {
		const double steps = slidingRenders >= SlidingHalfPhaseRenders ? 2 : 4;
		const double phaseX = std::floor(options.transform.tx*steps)/steps, phaseY = std::floor(options.transform.ty*steps)/steps;
		residualX = options.transform.tx-phaseX; residualY = options.transform.ty-phaseY;
		options.transform.tx = signature[6] = phaseX; options.transform.ty = signature[7] = phaseY;
	}
	auto entry = std::find_if(cache.begin(),cache.end(),[&](const Compilation& item) { return SameCompilation(signature,item.signature); });
	const bool rebuild = entry == cache.end();
	if (rebuild) {
		Compilation fresh; fresh.signature = signature;
		for (const auto& path : paths) {
			VectorMesh result; std::string error;
			const auto start = statistics->Now();
			const bool success = TessellatePath(path,options,result,error);
			statistics->vectorCompileMilliseconds += std::chrono::duration<double,std::milli>(statistics->Now()-start).count();
			++statistics->vectorPathsCompiled;
			if (!success) { host->Log(true,"Vector "+element.GetId()+"/"+error); continue; }
			if (!result.indices.empty()) { fresh.meshes.push_back(std::move(result)); fresh.blends.push_back(path.blend); }
		}
		cache.insert(cache.begin(),std::move(fresh));
		cache.resize(std::min<size_t>(cache.size(),moving ? 8 : 1));
		entry = cache.begin();
	} else {
		++statistics->vectorCacheHits;
		if (entry != cache.begin()) { std::rotate(cache.begin(),entry,entry+1); entry = cache.begin(); }
		if (!moving) cache.resize(1);
	}
	for (const auto& mesh : entry->meshes) statistics->visibleVectorCacheBytes += mesh.vertices.capacity()*sizeof(VectorVertex)+mesh.indices.capacity()*sizeof(int);
	if (opacity != entry->opacity) {
		entry->geometry.clear(); entry->multiplied.clear(); entry->opacity = opacity;
		// Paths paint in source order, and so do the triangles of one draw:
		// consecutive meshes that draw alike (normal and additive paths alike,
		// additive light being premultiplied with zero alpha) join one geometry.
		const bool merge = entry->meshes.size() >= MergedGeometryPaths;
		Rml::Mesh mesh;
		bool runMultiplied = false;
		auto flush = [&] {
			if (mesh.vertices.empty()) return;
			const auto upload = statistics->Now();
			entry->geometry.push_back(manager->MakeGeometry(std::move(mesh)));
			entry->multiplied.push_back(runMultiplied);
			mesh = Rml::Mesh();
			++statistics->vectorUploads;
			statistics->vectorUploadMilliseconds += std::chrono::duration<double,std::milli>(statistics->Now()-upload).count();
		};
		for (size_t index = 0; index < entry->meshes.size(); ++index) {
			const auto& cached = entry->meshes[index];
			const auto blend = entry->blends[index];
			const bool multiplied = blend == PathBlend::Multiply;
			if (cached.vertices.size() > MergedGeometryVertices) continue; // The runtime refuses it whole, as before.
			if (!mesh.vertices.empty() && (!merge || multiplied != runMultiplied || mesh.vertices.size()+cached.vertices.size() > MergedGeometryVertices)) flush();
			runMultiplied = multiplied;
			const auto upload = statistics->Now();
			const int base = static_cast<int>(mesh.vertices.size());
			mesh.vertices.reserve(mesh.vertices.size()+cached.vertices.size());
			auto channel = [&](double value) { return static_cast<Rml::byte>(std::round(std::clamp(value*opacity,0.0,1.0)*255)); };
			auto unit = [](double value) { return static_cast<Rml::byte>(std::round(std::clamp(value,0.0,1.0)*255)); };
			for (const auto& v : cached.vertices) {
				Rml::ColourbPremultiplied colour(channel(v.r),channel(v.g),channel(v.b),channel(v.a));
				// Premultiplied colour with zero alpha is pure addition under the
				// ordinary over operator, in the caller's target and in layers.
				if (blend == PathBlend::Additive) colour.alpha = 0;
				else if (blend == PathBlend::Multiply) {
					// Coverage and opacity lerp the factor from white toward the paint.
					const double alpha = std::clamp(v.a*opacity,0.0,1.0);
					colour = Rml::ColourbPremultiplied(unit(1-alpha+v.r*opacity),unit(1-alpha+v.g*opacity),unit(1-alpha+v.b*opacity),unit(alpha));
				}
				mesh.vertices.push_back({{static_cast<float>(v.x),static_cast<float>(v.y)},colour,{0,0}});
			}
			mesh.indices.reserve(mesh.indices.size()+cached.indices.size());
			for (const int i : cached.indices) mesh.indices.push_back(base+i);
			statistics->vectorUploadMilliseconds += std::chrono::duration<double,std::milli>(statistics->Now()-upload).count();
		}
		flush();
	}
	const auto& geometryBlends = entry->blends;
	if (!multiply && std::find(geometryBlends.begin(),geometryBlends.end(),PathBlend::Multiply) != geometryBlends.end())
		multiply = manager->LoadTexture(std::string("material:")+MultiplySource);
	const bool partition = !inheritOpacity && paths.size()==1 && paths[0].antialias &&
		paths[0].stroke.paint.type==PaintType::None && paths[0].blend==PathBlend::Normal;
	if (partition && !coverage) coverage=manager->LoadTexture(std::string("material:")+CoverageSource);
	// Geometry is already in output pixels. Submit through the render manager
	// to retain its clipping/lifetime/order contract without a second transform.
	renderInputs=inputs; renderTransform=state.transform; renderScissor=state.scissor_region; renderViewport=viewport;
	renderInheritOpacity=inheritOpacity; cachedLocalMesh=localMesh; cachedPartition=partition; cachedTransform=localTransform;
	cachedTranslation={static_cast<float>(offsetX+residualX),static_cast<float>(offsetY+residualY)};
	renderReady=true; drawCached();
}
bool VectorGeometry::UpdateReplayProjection(Rml::Element& element, const Rml::Matrix4f& transform) {
	if (paths.empty()) return true;
	const auto* context=element.GetContext();
	if (!context) return false;
	const float density=context->GetDensityIndependentPixelRatio();
	const auto size=element.GetBox().GetSize(Rml::BoxArea::Border);
	const auto offset=element.GetAbsoluteOffset(Rml::BoxArea::Border);
	if (!(density>0 && size.x>0 && size.y>0)) return false;
	const auto origin=transform*Rml::Vector4f(offset.x,offset.y,0,1);
	const auto x=transform*Rml::Vector4f(density,0,0,0), y=transform*Rml::Vector4f(0,density,0,0);
	if (!(origin.w>0) || std::abs(x.w)>1e-6f || std::abs(y.w)>1e-6f) return false;
	VectorOptions options;
	options.widthDp=size.x/density; options.heightDp=size.y/density;
	options.transform={x.x/origin.w,x.y/origin.w,y.x/origin.w,y.y/origin.w,origin.x/origin.w,origin.y/origin.w};
	for (double value:{options.transform.a,options.transform.b,options.transform.c,options.transform.d,options.transform.tx,options.transform.ty})
		if (!std::isfinite(value)) return false;
	for (double* translation:{&options.transform.tx,&options.transform.ty}) {
		const double integer=std::round(*translation);
		if (std::abs(*translation-integer)<1.0/1024) *translation=integer;
	}
	if (replayMask && paths.size()==1) {
		LinearMask next;
		if (!ResolveLinearMask(paths.front(),options,next)) return false;
		*replayMask=next; return true;
	}
	if (!renderReady || cache.empty() || cachedLocalMesh) return false;
	const auto& compiled=cache.front().signature;
	auto projected=compiled;
	projected[0]=options.widthDp; projected[1]=options.heightDp;
	projected[2]=options.transform.a; projected[3]=options.transform.b;
	projected[4]=options.transform.c; projected[5]=options.transform.d;
	projected[6]=options.transform.tx-cachedTranslation.x;
	projected[7]=options.transform.ty-cachedTranslation.y;
	// Compare against the recorded coverage, never the preceding replay, so
	// many tiny changes cannot accumulate into a visible displacement. The
	// compilation's padded bounds also cover paths outside their border box.
	return SameCompilation(projected,compiled);
}
bool VectorElement::UpdateReplayProjection() {
	if (!replayProjection || !GetRenderManager()) return false;
	// A timeline/binding can add a property absent from the initial node.
	// Such RmlUi paint or clipping also depends on projection and is not in
	// the vector compilation checked below.
	const auto& values=GetComputedValues();
	if (values.background_color().alpha || values.border_top_width() || values.border_right_width() ||
		values.border_bottom_width() || values.border_left_width() || values.has_decorator() || values.has_backdrop_filter() || values.has_box_shadow() ||
		values.overflow_x()!=Rml::Style::Overflow::Visible || values.overflow_y()!=Rml::Style::Overflow::Visible) return false;
	auto* manager=GetRenderManager();
	const auto state=manager->GetState();
	const bool transformed=Rml::ElementUtilities::ApplyTransform(*this);
	const auto transform=manager->GetState().transform;
	manager->SetState(state);
	if (!transformed || !paint.UpdateReplayProjection(*this,transform) ||
		(hasMask && !mask.UpdateReplayProjection(*this,transform))) return false;
	for (int i=0;i<GetNumChildren();++i) {
		auto* child=GetChild(i);
		if ((child->GetTagName()!="q4-node" && child->GetTagName()!="q4-vector") ||
			!static_cast<VectorElement*>(child)->UpdateReplayProjection()) return false;
	}
	return true;
}
void VectorElement::SetCanonicalText(const std::string& value) {
	if (legacyText) text=value;
	else SetInnerRML(Rml::StringUtilities::EncodeRml(value));
}
void VectorElement::OnRender() {
	paint.Render(*this,true);
	if (coordinatesImage) RenderImage();
	if (legacyText) RenderLegacyText();
}
bool VectorElement::IsRenderSuppressed() {
	// Canonical nodes own stacking contexts. Their isolated opacity filter
	// multiplies every descendant's premultiplied output, including additions
	// and backdrops. At exactly zero no paint can survive that composite.
	// Keep layout, animation and input state alive; only omit traversal/paint.
	if (!GetComputedValues().has_filter()) return false;
	const auto* property=GetProperty(Rml::PropertyId::Filter);
	if (!property || property->unit!=Rml::Unit::FILTER) return false;
	const auto filters=property->Get<Rml::FiltersPtr>();
	if (!filters || (filters->value!="opacity(0)" && filters->value!="q4-opacity(0)")) return false;
	if (measurements) ++measurements->layerElisions;
	return true;
}
bool VectorElement::SetImageSource(const std::string& source) {
	if (!coordinatesImage) return false;
	if (imageSource != source) { imageSource=source; imageTexture={}; imageGeometry={}; }
	return true;
}
void VectorElement::SetImageCoordinate(const std::string& property, double value) {
	const int index=property=="image-offset-x" ? 0 : property=="image-offset-y" ? 1 : property=="image-scale-x" ? 2 : 3;
	(*imageCoordinates)[index]=value;
}
bool VectorElement::SetPresentationOpacity(const std::string& filter, float density, Rml::Vector2f dimensions) {
	if (!SetProperty("filter",filter)) return false;
	UpdateProperties(density,dimensions);
	return true;
}
bool VectorElement::SetPresentationTransform(const Value& transform, float density, Rml::Vector2f dimensions) {
	if (transform.type!=ValueType::Transform || (transform.unit!="dp" && transform.unit!="px")) return false;
	const auto& v=transform.data;
	const auto unit=transform.unit=="dp" ? Rml::Unit::DP : Rml::Unit::PX;
	// Motion is already typed. Preserve the authored primitive order and
	// units without formatting and parsing the same CSS on every sample.
	if (!SetProperty(Rml::PropertyId::Transform,Rml::Transform::MakeProperty({
		Rml::Transforms::Translate2D(float(v[0]),float(v[1]),unit),
		Rml::Transforms::Scale2D(float(v[2]),float(v[3])),
		Rml::Transforms::Rotate2D(float(v[4]))}))) return false;
	UpdateProperties(density,dimensions);
	return true;
}
bool VectorElement::UpdateReplayOpacity(float opacity) {
	if (!replayOpacity) return false;
	const bool reusable=*replayOpacity>0 && *replayOpacity<1 && opacity>0 && opacity<1;
	*replayOpacity=opacity;
	return reusable;
}
void VectorElement::RenderImage() {
	auto* manager=GetRenderManager();
	if (!manager || imageSource.empty()) return;
	if (!imageTexture) imageTexture=manager->LoadTexture(std::string("material:")+(additiveImage ? "q4-add/" : "")+"_repeat/"+imageSource);
	if (!imageTexture) return;
	const auto size=GetBox().GetSize(Rml::BoxArea::Content);
	if (size.x<=0 || size.y<=0) return;
	const auto colour=GetComputedValues().image_color();
	const float opacity=GetComputedValues().opacity();
	const std::array<double,11> signature{size.x,size.y,(*imageCoordinates)[0],(*imageCoordinates)[1],(*imageCoordinates)[2],(*imageCoordinates)[3],
		double(colour.red),double(colour.green),double(colour.blue),double(colour.alpha),opacity};
	if (!imageGeometry || signature!=imageSignature) {
		imageSignature=signature;
		const auto tint=colour.ToPremultiplied(opacity);
		const float u0=float(.5+(*imageCoordinates)[0]-.5*(*imageCoordinates)[2]),v0=float(.5+(*imageCoordinates)[1]-.5*(*imageCoordinates)[3]);
		const float u1=float(u0+(*imageCoordinates)[2]),v1=float(v0+(*imageCoordinates)[3]);
		Rml::Mesh mesh;
		mesh.vertices={{{0,0},tint,{u0,v0}},{{size.x,0},tint,{u1,v0}},{{size.x,size.y},tint,{u1,v1}},{{0,size.y},tint,{u0,v1}}};
		mesh.indices={0,1,2,0,2,3};
		imageGeometry=manager->MakeGeometry(std::move(mesh));
	}
	if (imageSampling) imageSampling->SetImageSampling(imageCoordinates);
	imageGeometry.Render(GetAbsoluteOffset(Rml::BoxArea::Content),imageTexture);
	if (imageSampling) imageSampling->SetImageSampling({});
}
void VectorElement::RenderLegacyText() {
	if (!textHost || !GetContext()) return;
	if (measurements && text.find('^')!=std::string::npos) ++measurements->liveTextRuns;
	const float unit=1.5f*GetContext()->GetDensityIndependentPixelRatio();
	const auto size=GetBox().GetSize(Rml::BoxArea::Border);
	Rml::Array<Rml::Vector2f,4> quad;
	if (!(unit>0 && size.x>0 && size.y>0) || !Rml::ElementUtilities::GetBorderBoxQuad(quad,this)) return;
	const auto& css=GetComputedValues();
	const auto colour=css.color();
	const int align=css.text_align()==Rml::Style::TextAlign::Right ? 2 : css.text_align()==Rml::Style::TextAlign::Center ? 1 : 0;
	const float scale=css.font_size()/(48*unit);
	const std::array<float,5> layoutSignature{scale,size.x/unit,size.y/unit,float(align),legacyTextClip ? 1.f : 0.f};
	// Plain glyph layout depends only on these inputs. Escapes can refer to
	// live key bindings, so those continue to ask the host every frame.
	const bool relayout=!textLayoutValid || layoutText!=text || layoutFamily!=css.font_family() ||
		textLayoutSignature!=layoutSignature || text.find('^')!=std::string::npos;
	bool changed=false;
	if (relayout) {
		std::vector<HudPrimitive> items;
		if (!textHost->LayoutLegacyText(text,css.font_family(),scale,size.x/unit,size.y/unit,align,legacyTextClip,items)) return;
		changed=items.size()!=textItems.size() || !std::equal(items.begin(),items.end(),textItems.begin(),
			[](const auto& a,const auto& b) { return a.material==b.material && a.quad==b.quad && a.uv==b.uv && a.colour==b.colour; });
		textItems=std::move(items); layoutText=text; layoutFamily=css.font_family();
		textLayoutSignature=layoutSignature; textLayoutValid=true;
	}
	std::array<float,12> signature{quad[0].x,quad[0].y,quad[1].x,quad[1].y,quad[3].x,quad[3].y,
		size.x/unit,size.y/unit,float(colour.red),float(colour.green),float(colour.blue),float(colour.alpha)};
	const bool same=signature==textSignature && !changed;
	auto* manager=GetRenderManager(); if (!manager) return;
	if (!same) {
		textGeometry.clear(); textTextures.clear();
		std::vector<std::pair<std::string,Rml::Mesh>> batches;
		for (const auto& item:textItems) {
			if (batches.empty() || batches.back().first!=item.material) batches.emplace_back(item.material,Rml::Mesh{});
			auto& mesh=batches.back().second;
			const int base=static_cast<int>(mesh.vertices.size());
			const float alpha=item.colour[3]*colour.alpha/255.f;
			const auto byte=[](float v) { return static_cast<Rml::byte>(std::lround(std::clamp(v,0.f,255.f))); };
			const Rml::ColourbPremultiplied tint(byte(item.colour[0]*colour.red*alpha),byte(item.colour[1]*colour.green*alpha),
				byte(item.colour[2]*colour.blue*alpha),byte(alpha*255));
			for (int i=0;i<4;++i) {
				const float x=item.quad[2*i]*unit/size.x,y=item.quad[2*i+1]*unit/size.y;
				const auto pos=quad[0]+(quad[1]-quad[0])*x+(quad[3]-quad[0])*y;
				mesh.vertices.push_back({pos,tint,{item.uv[(i==1 || i==2)?2:0],item.uv[i>=2?3:1]}});
			}
			mesh.indices.insert(mesh.indices.end(),{base,base+3,base+1,base+1,base+3,base+2});
		}
		for (auto& [material,mesh]:batches) {
			textGeometry.push_back(manager->MakeGeometry(std::move(mesh)));
			textTextures.push_back(manager->LoadTexture("material:"+material));
		}
		textSignature=signature;
	}
	const auto state=manager->GetState(); manager->SetTransform(nullptr);
	for (size_t i=0;i<textGeometry.size();++i) textGeometry[i].Render({0,0},textTextures[i]);
	manager->SetState(state);
}
void VectorElement::Configure(const Node& node, Host& owner, RuntimeStatistics& measurements) {
	this->measurements=&measurements;
	hasMask = node.mask.has_value();
	// Restrict projection replay to plain vector branches. Native text,
	// images, overflow clips, borders, backdrops and controls keep traversal.
	replayProjection=(node.type=="group" || node.type=="vector") && !node.control && !node.modal && !node.legacyText &&
		std::all_of(node.properties.begin(),node.properties.end(),[](const auto& property) {
			// Told apart by the first letter before a comparison: a page's tens
			// of thousands of properties pass here on every load.
			const auto& name=property.first;
			switch (name.empty() ? 0 : name[0]) {
			case 'p': return name=="position";
			case 'l': return name=="left";
			case 't': return name=="top" || name=="transform";
			case 'r': return name=="right";
			case 'b': return name=="bottom";
			case 'w': return name=="width";
			case 'h': return name=="height";
			case 'd': return name=="display";
			case 'o': return name=="opacity";
			default: return false;
			}
		});
	legacyText=node.legacyText; legacyTextClip=node.legacyTextClip; textHost=&owner;
	textItems.clear(); textGeometry.clear(); textTextures.clear();
	textLayoutValid=false;
	paint.Configure(node.paths,owner,measurements);
	mask.Configure(node.mask ? *node.mask : std::vector<VectorPath>{},owner,measurements);
	coordinatesImage=HasImageCoordinates(node);
	// A leaf's opacity is its isolated layer's composite, which reads the live
	// value, so a fade need not restyle and relay out the document. That holds
	// for a picture (text, an image) and for plain artwork: a vector's paths
	// draw into the layer at full opacity, overlapping or not (a dialog's
	// pulsing glow beside its buttons).
	const bool leaf=node.children.empty() && !hasMask && !node.control;
	const bool picture=node.paths.empty() && (node.type=="text" || node.type=="image" || coordinatesImage);
	const bool artwork=node.type=="vector" && !node.paths.empty() && !node.legacyText && !coordinatesImage && !node.properties.contains("image");
	replayOpacity=leaf && (picture || artwork) ? std::make_shared<float>(1.f) : nullptr;
	replayText=replayOpacity && node.type=="text" && !node.properties.contains("image");
	additiveImage=node.properties.contains("image-blend") && node.properties.at("image-blend").text=="additive";
	imageSource=node.properties.contains("image") ? node.properties.at("image").text : "";
	imageCoordinates=std::make_shared<ImageCoordinates>(ImageCoordinates{0,0,1,1}); imageTexture={}; imageGeometry={};
	if (node.model) (*imageCoordinates)[3]=-1; // Engine targets have their origin at the lower edge.
	for (const auto* property : {"image-offset-x","image-offset-y","image-scale-x","image-scale-y"})
		if (const auto found=node.properties.find(property);found!=node.properties.end()) SetImageCoordinate(property,found->second.data[0]);
	backdropFault.reset();
	for (const auto* property : {"backdrop-dither","backdrop-glitch","backdrop-glitch-phase"})
		if (const auto found=node.properties.find(property);found!=node.properties.end()) {
			if (!backdropFault) backdropFault=std::make_shared<std::array<float,3>>(std::array<float,3>{0,0,0});
			SetBackdropFault(BackdropFaultIndex(property),float(found->second.data[0]));
		}
}
namespace {
class VectorMask final : public Rml::Decorator {
public:
	Rml::DecoratorDataHandle GenerateElementData(Rml::Element*, Rml::BoxArea) const override { return 1; }
	void ReleaseElementData(Rml::DecoratorDataHandle) const override {}
	void RenderElement(Rml::Element* element, Rml::DecoratorDataHandle) const override {
		if (element->GetTagName() == "q4-node" || element->GetTagName() == "q4-vector")
			static_cast<VectorElement*>(element)->RenderMask();
	}
};
}
VectorMaskInstancer::VectorMaskInstancer() {
	RegisterProperty("mode","alpha").AddParser("keyword","alpha");
	RegisterShorthand("decorator","mode",Rml::ShorthandType::FallThrough);
}
Rml::SharedPtr<Rml::Decorator> VectorMaskInstancer::InstanceDecorator(const Rml::String&,
	const Rml::PropertyDictionary&, const Rml::DecoratorInstancerInterface&) {
	return Rml::MakeShared<VectorMask>();
}
} // namespace openq4::ui
