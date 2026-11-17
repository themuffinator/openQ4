// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#pragma once

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace openq4::ui {

struct VectorPoint { double x = 0, y = 0; };
struct VectorCoordinate {
	double fraction = 0, dp = 0;
	double Resolve(double extent) const { return fraction*extent+dp; }
};
struct PathPoint {
	VectorCoordinate x, y;
	VectorPoint Resolve(double width, double height) const { return {x.Resolve(width),y.Resolve(height)}; }
};
// Affine local-dp to output-pixel map. Layout, DPI and inherited transforms
// are composed once before tessellation and inverse hit testing.
struct VectorTransform {
	double a = 1, b = 0, c = 0, d = 1, tx = 0, ty = 0;
	VectorPoint Apply(VectorPoint p) const { return {a*p.x+c*p.y+tx,b*p.x+d*p.y+ty}; }
	bool Inverse(VectorTransform& result) const;
	double MaximumScale() const;
};
enum class PathOperation { Move, Line, Quadratic, Cubic, Close };
struct PathCommand {
	std::string id;
	PathOperation op = PathOperation::Move;
	std::array<PathPoint,3> points;
};
enum class FillRule { NonZero, EvenOdd };
struct VectorColour { double r = 1, g = 1, b = 1, a = 1; };
struct GradientStop { double at = 0; VectorColour colour; };
enum class PaintType { None, Solid, Linear, Radial, Diamond, Conic };
struct VectorPaint {
	PaintType type = PaintType::None;
	VectorColour colour;
	PathPoint from, to;
	std::vector<GradientStop> stops;
};
enum class StrokeCap { Butt, Square, Round };
enum class StrokeJoin { Miter, Bevel, Round };
struct VectorStroke {
	VectorPaint paint;
	double widthDp = 1, minimumPixels = 0, miterLimit = 4;
	StrokeCap cap = StrokeCap::Butt;
	StrokeJoin join = StrokeJoin::Miter;
};
// How a path's paint composes with what is already drawn (specification
// section 4). Additive light brightens the destination by the premultiplied
// paint; multiply darkens it toward the paint colour, as the stock emblem does.
enum class PathBlend { Normal, Additive, Multiply };
struct VectorPath {
	std::string id;
	std::vector<PathCommand> commands;
	FillRule fillRule = FillRule::NonZero;
	VectorPaint fill;
	VectorStroke stroke;
	PathBlend blend = PathBlend::Normal;
	// False draws hard pixel edges: a few triangles instead of per-pixel edge
	// coverage, for large soft or faint decorative shapes.
	bool antialias = true;
};
struct VectorVertex {
	double x = 0, y = 0;
	// Premultiplied sRGB. Floating point is retained through gradient clipping.
	double r = 1, g = 1, b = 1, a = 1;
};
struct VectorMesh { std::vector<VectorVertex> vertices; std::vector<int> indices; };
// Bounds of output pixel cells, with exclusive right/bottom edges. These limit
// coverage work; the renderer still owns its final clip and composition state.
struct VectorPixelBounds { int left = 0, top = 0, right = 0, bottom = 0; };
struct VectorOptions {
	double widthDp = 0, heightDp = 0;
	VectorTransform transform;
	double tolerancePixels = .15;
	size_t maximumVertices = 262144;
	bool antialias = true;
	std::optional<VectorPixelBounds> pixelBounds;
};

// A small convex, fill-only mask can be evaluated directly in output pixels.
// Clockwise points include a repeated final corner for triangles. Paint is a
// three-stop linear alpha ramp; RGB has no effect on a mask.
struct LinearMask {
	std::array<VectorPoint,4> points;
	std::array<double,3> gradient, alpha;
	double middle = .5;
};
bool ResolveLinearMask(const VectorPath& path, const VectorOptions& options, LinearMask& result);

// Backend-independent path compiler. No GPU/window, engine, RmlUi or JSON API.
// Failure leaves the previous mesh intact and reports the offending path.
bool TessellatePath(const VectorPath& path, const VectorOptions& options,
	VectorMesh& output, std::string& diagnostic);
// One convex triangle or quadrilateral of one solid colour, already in output pixels, with the
// same exact pixel coverage TessellatePath gives it but without the general
// path compiler: a convex polygon is its own boundary. Either winding. Leaves
// the output empty for a degenerate polygon. False with the output untouched:
// with no diagnostic for a polygon that is not convex or not finite, which the
// caller compiles as a path instead; with one when coverage itself failed.
bool CoverConvexPolygon(const VectorPoint* points, size_t count, const VectorColour& colour,
	const VectorOptions& options, VectorMesh& output, std::string& diagnostic);
bool HitTestPath(const VectorPath& path, const VectorOptions& options,
	VectorPoint outputPoint, bool& hit, std::string& diagnostic);
// Sample geometric paint coverage, excluding transparent paint and degenerate
// triangles. Use non-antialiased meshes so edge fringes do not enlarge targets.
bool HitTestPaintedMesh(const VectorMesh& mesh, VectorPoint point);

} // namespace openq4::ui
