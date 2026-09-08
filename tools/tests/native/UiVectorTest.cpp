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
int main() {
	VectorOptions options; options.widthDp = 100; options.heightDp = 80;
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
	std::puts("UI vector: fill rules, holes/intersections, adaptive curves, transforms, strokes/joins/caps, gradients, hit geometry, budgets and editable responsive paths passed");
}
