// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "src/ui/retained/Vector.h"
#include "src/ui/retained/Document.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>

using namespace openq4::ui;
static void Check(bool condition, const char* message) {
	if (!condition) { std::fprintf(stderr,"FAIL: %s\n",message); std::exit(1); }
}
static bool Near(double a, double b, double tolerance = .001) { return std::abs(a-b) <= tolerance; }
static PathPoint P(double x, double y) { return {{0,x},{0,y}}; }
static PathCommand Command(PathOperation op, std::initializer_list<PathPoint> points) {
	PathCommand result; result.op = op;
	std::copy(points.begin(),points.end(),result.points.begin());
	return result;
}
static void Polygon(VectorPath& path, const std::vector<PathPoint>& points) {
	path.commands.push_back(Command(PathOperation::Move,{points.front()}));
	for (size_t i = 1; i < points.size(); ++i) path.commands.push_back(Command(PathOperation::Line,{points[i]}));
	path.commands.push_back(Command(PathOperation::Close,{}));
}
static VectorPath Rectangle() {
	VectorPath path; path.id = "rectangle"; path.fill.type = PaintType::Solid;
	path.fill.colour = {.545,.588,.294,.88};
	Polygon(path,{P(0,0),P(100,0),P(100,80),P(0,80)});
	return path;
}
static double Area(const VectorMesh& mesh) {
	double result = 0;
	for (size_t i = 0; i < mesh.indices.size(); i += 3) {
		const auto& a = mesh.vertices[mesh.indices[i]];
		const auto& b = mesh.vertices[mesh.indices[i+1]];
		const auto& c = mesh.vertices[mesh.indices[i+2]];
		result += .5*std::abs((b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x));
	}
	return result;
}
static double AlphaMass(const VectorMesh& mesh) {
	double result = 0;
	for (size_t i = 0; i < mesh.indices.size(); i += 3) {
		const auto& a = mesh.vertices[mesh.indices[i]]; const auto& b = mesh.vertices[mesh.indices[i+1]]; const auto& c = mesh.vertices[mesh.indices[i+2]];
		result += .5*std::abs((b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x))*(a.a+b.a+c.a)/3;
	}
	return result;
}
static double SampleAlpha(const VectorMesh& mesh, double x, double y) {
	double result = 0;
	for (size_t i = 0; i < mesh.indices.size(); i += 3) {
		const auto& a = mesh.vertices[mesh.indices[i]]; const auto& b = mesh.vertices[mesh.indices[i+1]]; const auto& c = mesh.vertices[mesh.indices[i+2]];
		const double det = (b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x);
		const double u = ((x-a.x)*(c.y-a.y)-(y-a.y)*(c.x-a.x))/det;
		const double v = ((b.x-a.x)*(y-a.y)-(b.y-a.y)*(x-a.x))/det;
		if (u > 0 && v > 0 && u+v < 1) result += (1-u-v)*a.a+u*b.a+v*c.a;
	}
	return result;
}
// Independent oracle: clip raw triangles to a pixel square and sum their areas.
// This deliberately does not use the boundary-integral implementation.
static double PixelArea(const VectorMesh& mesh, int x, int y) {
	double result = 0;
	for (size_t i = 0; i < mesh.indices.size(); i += 3) {
		std::vector<VectorPoint> polygon;
		for (int j = 0; j < 3; ++j) { const auto& v = mesh.vertices[mesh.indices[i+j]]; polygon.push_back({v.x,v.y}); }
		for (int side = 0; side < 4 && !polygon.empty(); ++side) {
			auto distance = [&](VectorPoint p) { return side == 0 ? p.x-x : side == 1 ? x+1-p.x : side == 2 ? p.y-y : y+1-p.y; };
			std::vector<VectorPoint> clipped;
			auto previous = polygon.back(); double before = distance(previous);
			for (auto p : polygon) {
				const double after = distance(p);
				if ((before >= 0) != (after >= 0)) { const double t = before/(before-after); clipped.push_back({previous.x+(p.x-previous.x)*t,previous.y+(p.y-previous.y)*t}); }
				if (after >= 0) clipped.push_back(p);
				previous = p; before = after;
			}
			polygon.swap(clipped);
		}
		double area = 0;
		for (size_t j = 0; j < polygon.size(); ++j) { const auto& a = polygon[j]; const auto& b = polygon[(j+1)%polygon.size()]; area += a.x*b.y-a.y*b.x; }
		result += .5*std::abs(area);
	}
	return result;
}
static void CoverageChecks() {
	VectorOptions options; VectorMesh mesh; std::string error;
	auto build = [&](const VectorPath& path) {
		if (!TessellatePath(path,options,mesh,error)) { std::fprintf(stderr,"%s\n",error.c_str()); Check(false,"compile coverage geometry"); }
	};
	auto path = Rectangle(); options.transform.tx = .25; options.transform.ty = .5;
	build(path);
	Check(Near(AlphaMass(mesh),8000*.88,.002),"fractional translation preserves alpha-weighted area");
	Check(Near(SampleAlpha(mesh,.381,.619),.75*.5*.88),"fractional corner has analytic quarter-cell coverage");
	Check(Near(SampleAlpha(mesh,50.381,40.619),.88),"interior has no triangulation seams");
	bool hit = true;
	Check(HitTestPath(path,options,{.1,.75},hit,error) && !hit,"antialiased boundary cell does not enlarge geometric hit target");
	options.transform = {};
	path = {}; path.fill.type = PaintType::Solid;
	Polygon(path,{P(0,0),P(8,0),P(0,8)}); build(path);
	Check(Near(SampleAlpha(mesh,3.381,4.619),.5),"45-degree boundary has half-pixel coverage");
	Check(Near(AlphaMass(mesh),32),"diagonal coverage conserves triangle area");
	auto compare = [&](VectorPath shape, VectorTransform transform, double alpha) {
		options.transform = transform; build(shape);
		auto rawOptions = options; rawOptions.antialias = false;
		VectorMesh raw; Check(TessellatePath(shape,rawOptions,raw,error),"compile independent reference triangles");
		Check(Near(AlphaMass(mesh),Area(raw)*alpha,.0002),"coverage conserves normalized fill or stroke area");
		for (int y = -3; y < 18; ++y) for (int x = -3; x < 18; ++x)
			Check(Near(SampleAlpha(mesh,x+.381,y+.619),PixelArea(raw,x,y)*alpha,.00001),"pixel coverage matches independently clipped triangles");
	};
	path = {}; path.fill.type = PaintType::Solid; path.fillRule = FillRule::EvenOdd;
	Polygon(path,{P(0,0),P(12,0),P(12,12),P(0,12)});
	Polygon(path,{P(3,3),P(9,3),P(9,9),P(3,9)});
	compare(path,{1,.13,-.21,1,.25,.75},1);
	compare(path,{-1,0,0,1,12.25,.375},1);
	path.fillRule = FillRule::NonZero;
	compare(path,{1,0,0,1,.375,.625},1); // Same winding fills the inner contour.
	path = {}; path.fill.type = PaintType::Solid;
	Polygon(path,{P(0,0),P(12,12),P(0,12),P(12,0)});
	compare(path,{1,0,0,1,.375,.125},1);
	path = {}; path.stroke.paint.type = PaintType::Solid; path.stroke.paint.colour = {.5,.8,.2,.5};
	path.stroke.widthDp = 1.25; path.stroke.cap = StrokeCap::Round; path.stroke.join = StrokeJoin::Round;
	path.commands = {Command(PathOperation::Move,{P(1,1)}),Command(PathOperation::Line,{P(10,10)}),Command(PathOperation::Line,{P(1,10)}),Command(PathOperation::Line,{P(10,1)})};
	compare(path,{1,0,0,1,.2,.6},.5);
	path.stroke.widthDp = .1; path.stroke.cap = StrokeCap::Butt;
	path.commands = {Command(PathOperation::Move,{P(0,0)}),Command(PathOperation::Line,{P(10,0)})};
	options.transform = {1,0,0,1,0,.5}; build(path);
	Check(Near(AlphaMass(mesh),.5,.00001) && Near(SampleAlpha(mesh,4.381,.619),.05),"subpixel stroke remains visible with proportional coverage");
	path = {}; path.fill.type = PaintType::Solid;
	Polygon(path,{P(0,0),P(.4,0),P(.4,1),P(0,1)});
	Polygon(path,{P(.6,0),P(1,0),P(1,1),P(.6,1)});
	options.transform = {}; build(path);
	Check(Near(SampleAlpha(mesh,.381,.619),.8),"disjoint pieces in one pixel resolve coverage before blending");
	path = Rectangle(); path.fill.type = PaintType::Linear; path.fill.from = P(0,0); path.fill.to = P(100,0);
	path.fill.stops = {{0,{1,0,0,1}},{.5,{0,1,0,1}},{1,{0,0,1,0}}};
	options.transform = {1,0,0,1,.25,.5}; build(path);
	bool middle = false;
	for (const auto& v : mesh.vertices) {
		Check(v.b < 1e-10 && v.r <= v.a+1e-10 && v.g <= v.a+1e-10,"coverage keeps gradient colors premultiplied at transparent endpoints");
		if (Near(v.x,50.25) && v.g > .99 && v.a > .99) middle = true;
	}
	Check(middle,"coverage geometry retains authored middle gradient stop");
	path = {}; path.id = "fractional-chamfer-rail"; path.stroke.paint.type = PaintType::Solid;
	path.stroke.widthDp = 1; path.stroke.minimumPixels = 1;
	Polygon(path,{P(12.5,.5),P(395.5,.5),P(407.5,12.5),P(407.5,237.5),P(399.5,245.5),P(8.5,245.5),P(.5,237.5),P(.5,12.5)});
	for (int i = 1; i <= 10; ++i) {
		const double position = static_cast<float>(48+i*.3);
		options.transform = {2,0,0,2,position-std::floor(position),0};
		build(path);
		auto rawOptions = options; rawOptions.antialias = false;
		VectorMesh raw; Check(TessellatePath(path,rawOptions,raw,error),"compile near-vertical edge reference");
		Check(Near(AlphaMass(mesh),Area(raw),.001),"fractional chamfer rail conserves coverage despite almost-coincident edge coordinates");
		Check(Near(SampleAlpha(mesh,791.381,.619),PixelArea(raw,791,0),.00001),"thin rail corner matches independent clipped area at troublesome fractional phases");
	}
	path = Rectangle(); options.transform = {1000,0,0,1000,-40000,-40000};
	options.pixelBounds = VectorPixelBounds{0,0,4,3}; build(path);
	Check(Near(AlphaMass(mesh),12*.88) && mesh.vertices.size() == 4,"large offscreen fill clips work and merges interior into one quad");
	options.pixelBounds = VectorPixelBounds{0,0,0,0}; build(path);
	Check(mesh.indices.empty(),"empty viewport produces empty coverage");
	options.pixelBounds = VectorPixelBounds{0,0,4,3}; build(path);
	const auto previous = mesh.vertices.size(); options.maximumVertices = 3;
	Check(!TessellatePath(path,options,mesh,error) && mesh.vertices.size() == previous,"coverage budget failure preserves previous mesh");
	std::puts("UI coverage: analytic area, independent per-pixel oracle, holes/intersections, translucent stroke unions, subpixel rails, clipping and exact hit geometry passed");
}
int main() {
	VectorOptions options; options.widthDp = 100; options.heightDp = 80;
	options.antialias = false; // These checks measure geometric area, before pixel coverage.
	VectorMesh mesh; std::string error;
	auto build = [&](const VectorPath& path) {
		if (!TessellatePath(path,options,mesh,error)) { std::fprintf(stderr,"%s\n",error.c_str()); Check(false,"compile vector geometry"); }
		Check(mesh.indices.size()%3 == 0,"triangle topology");
		for (int i : mesh.indices) Check(i >= 0 && static_cast<size_t>(i) < mesh.vertices.size(),"valid vector indices");
		for (const auto& v : mesh.vertices) Check(std::isfinite(v.x) && std::isfinite(v.y) && v.a >= 0 && v.a <= 1 && v.r <= v.a+1e-10 && v.g <= v.a+1e-10 && v.b <= v.a+1e-10,"finite premultiplied vector payload");
	};
	auto path = Rectangle(); build(path);
	Check(Near(Area(mesh),8000),"rectangle fill area without triangle overlap");
	Check(Near(mesh.vertices.front().r,.545*.88) && Near(mesh.vertices.front().a,.88),"paint premultiplied exactly once");
	Polygon(path,{P(30,20),P(30,60),P(70,60),P(70,20)});
	build(path); Check(Near(Area(mesh),6400),"opposite-winding hole survives nonzero fill");
	bool hit = true;
	Check(HitTestPath(path,options,{50,40},hit,error) && !hit,"hole excluded from hit geometry");
	Check(HitTestPath(path,options,{10,40},hit,error) && hit,"solid included in hit geometry");
	path = Rectangle(); Polygon(path,{P(30,20),P(70,20),P(70,60),P(30,60)});
	build(path); Check(Near(Area(mesh),8000),"same winding remains filled under nonzero rule");
	path.fillRule = FillRule::EvenOdd;
	build(path); Check(Near(Area(mesh),6400),"even-odd cuts same-winding hole");
	path = {}; path.id = "intersection"; path.fill.type = PaintType::Solid;
	Polygon(path,{P(0,0),P(100,100),P(0,100),P(100,0)});
	build(path); Check(Near(Area(mesh),5000),"self-intersecting fill creates crossing vertices without overlap");
	path = Rectangle();
	options.transform = {2,.5,.25,3,17,-8};
	build(path); Check(Near(Area(mesh),8000*(6-.125),.02),"affine geometry area matches determinant");
	VectorTransform inverse;
	Check(options.transform.Inverse(inverse),"invert input mapping");
	const auto projected = options.transform.Apply({30,20}); const auto restored = inverse.Apply(projected);
	Check(Near(restored.x,30) && Near(restored.y,20),"inverse uses actual composed transform");
	Check(HitTestPath(path,options,projected,hit,error) && hit,"hit geometry agrees under scale, shear and translation");
	options.transform = {-2,0,0,2,200,0}; build(path);
	Check(Near(Area(mesh),32000),"mirrored shape preserves fill winding semantics");
	options.transform = {};
	path = {}; path.id = "circle"; path.fill.type = PaintType::Solid;
	constexpr double k = 27.61423749153967;
	path.commands = {Command(PathOperation::Move,{P(100,50)}),
		Command(PathOperation::Cubic,{P(100,50+k),P(50+k,100),P(50,100)}),
		Command(PathOperation::Cubic,{P(50-k,100),P(0,50+k),P(0,50)}),
		Command(PathOperation::Cubic,{P(0,50-k),P(50-k,0),P(50,0)}),
		Command(PathOperation::Cubic,{P(50+k,0),P(100,50-k),P(100,50)}),Command(PathOperation::Close,{})};
	build(path); const size_t lowDensityVertices = mesh.vertices.size();
	Check(Near(Area(mesh),3.141592653589793*2500,20),"adaptive cubic circle area");
	options.transform = {8,0,0,8,0,0}; build(path);
	Check(mesh.vertices.size() > lowDensityVertices,"higher output density receives more curve segments");
	Check(Near(Area(mesh)/64,3.141592653589793*2500,5),"high-density curve accuracy is measured after scaling");
	options.transform = {};
	path = {}; path.id = "quadratic"; path.fill.type = PaintType::Solid;
	path.commands = {Command(PathOperation::Move,{P(0,0)}),Command(PathOperation::Quadratic,{P(50,100),P(100,0)}),Command(PathOperation::Close,{})};
	build(path); Check(Near(Area(mesh),10000.0/3,12),"quadratic curve integral");
	path = {}; path.id = "line"; path.stroke.paint.type = PaintType::Solid; path.stroke.widthDp = 10;
	path.commands = {Command(PathOperation::Move,{P(0,0)}),Command(PathOperation::Line,{P(100,0)})};
	build(path); Check(Near(Area(mesh),1000),"butt stroke width and cap area");
	path.stroke.cap = StrokeCap::Square; build(path); Check(Near(Area(mesh),1100),"square caps extend by half width");
	path.stroke.cap = StrokeCap::Round; build(path); Check(Near(Area(mesh),1000+3.141592653589793*25,5),"round caps union without translucent overdraw");
	path = Rectangle(); path.fill.type = PaintType::None; path.stroke.paint.type = PaintType::Solid; path.stroke.widthDp = 10;
	build(path); Check(Near(Area(mesh),3600),"miter joins form a closed stroke ring without overlap");
	path.stroke.join = StrokeJoin::Bevel; build(path); Check(Near(Area(mesh),3550),"bevel join geometry");
	path.stroke.join = StrokeJoin::Round; build(path); Check(Near(Area(mesh),3500+3.141592653589793*25,5),"round joins preserve stroke area");
	options.transform = {.5,0,0,.5,0,0}; path.stroke.widthDp = .2; path.stroke.minimumPixels = 1; path.stroke.join = StrokeJoin::Miter;
	build(path); Check(Near(Area(mesh),180),"essential stroke keeps one output pixel minimum at low scale");
	options.transform = {};
	path = Rectangle(); path.fill.type = PaintType::Linear; path.fill.from = P(0,0); path.fill.to = P(100,0);
	path.fill.stops = {{0,{1,0,0,1}},{.5,{0,1,0,1}},{1,{0,0,1,1}}};
	build(path); Check(Near(Area(mesh),8000),"gradient stop subdivision preserves fill area");
	bool green = false;
	for (const auto& v : mesh.vertices) if (Near(v.x,50) && v.g > .99 && v.r < .01 && v.b < .01) green = true;
	Check(green,"middle gradient stop becomes real geometry even without an original vertex there");
	path.fill.stops = {{0,{1,0,0,1}},{1,{0,0,1,0}}}; build(path);
	for (const auto& v : mesh.vertices) Check(Near(v.b,0) && Near(v.r,v.a),"transparent gradient endpoint cannot tint visible premultiplied colour");
	const auto previousVertices = mesh.vertices.size();
	options.maximumVertices = 3;
	Check(!TessellatePath(path,options,mesh,error) && !error.empty() && mesh.vertices.size() == previousVertices,"budget failure retains previous complete mesh");
	options.maximumVertices = 262144;
	options.transform.tx = std::numeric_limits<double>::infinity();
	Check(!TessellatePath(path,options,mesh,error),"reject non-finite transform");
	options.transform = {0,0,0,0,0,0}; build(path); Check(mesh.indices.empty(),"collapsed scale produces no stale geometry");
	Document document; std::vector<Diagnostic> diagnostics;
	const char* source = R"json({"format":"openq4-ui","version":1,"id":"vector-document",
	 "tokens":{"rail":{"type":"color","value":[0.5,0.6,0.3,1]}},
	 "root":{"id":"panel","type":"vector","paths":[{"id":"plate","fill":{"type":"solid","color":{"type":"token","value":"rail"}},
	 "commands":[{"id":"start","op":"move","points":[[12,0]]},
	 {"id":"top-right","op":"line","points":[[{"fraction":1,"dp":-12},0]]},
	 {"id":"right","op":"line","points":[[{"fraction":1},12]]},
	 {"id":"bottom-right","op":"line","points":[[{"fraction":1},{"fraction":1}]]},
	 {"id":"bottom-left","op":"line","points":[[0,{"fraction":1}]]},
	 {"id":"left","op":"line","points":[[0,12]]},{"id":"close","op":"close"}]}]}})json";
	Check(document.Load(source,diagnostics),"parse editable path commands, anchors and paint token");
	options.transform = {}; options.widthDp = 100; options.heightDp = 80;
	path = document.Model().root.paths[0]; build(path); Check(Near(Area(mesh),8000-144),"12 dp chamfers preserve their measured cuts");
	options.widthDp = 300; build(path); Check(Near(Area(mesh),24000-144),"aspect expansion lengthens straight spans without stretching corner cuts");
	Check(document.ReplaceValue("/root/paths/0/commands/0/points/0/0","16",diagnostics),"path control point shares transactional source editing");
	Check(!document.ReplaceValue("/root/paths/0/commands/1/id","\"start\"",diagnostics),"reject duplicate editable command IDs");
	Check(!document.ReplaceValue("/root/paths/0/commands/1/op","\"arc\"",diagnostics),"unsupported path operations fail explicitly");
	CoverageChecks();
	std::puts("UI vector: fill rules, holes/intersections, adaptive curves, transforms, strokes/joins/caps, gradients, hit geometry, budgets and editable responsive paths passed");
}
