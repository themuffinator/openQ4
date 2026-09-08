// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "Vector.h"
#include <tesselator.h>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <new>

namespace openq4::ui {
namespace {
constexpr double Pi = 3.14159265358979323846;
VectorPoint operator+(VectorPoint a, VectorPoint b) { return {a.x+b.x,a.y+b.y}; }
VectorPoint operator-(VectorPoint a, VectorPoint b) { return {a.x-b.x,a.y-b.y}; }
VectorPoint operator*(VectorPoint a, double b) { return {a.x*b,a.y*b}; }
double Dot(VectorPoint a, VectorPoint b) { return a.x*b.x+a.y*b.y; }
double Cross(VectorPoint a, VectorPoint b) { return a.x*b.y-a.y*b.x; }
double Length(VectorPoint a) { return std::hypot(a.x,a.y); }
VectorPoint Unit(VectorPoint a) { const double length = Length(a); return length > 1e-12 ? a*(1/length) : VectorPoint{}; }
VectorPoint Normal(VectorPoint a) { return {-a.y,a.x}; }
bool Finite(VectorPoint p) { return std::isfinite(p.x) && std::isfinite(p.y); }
double SegmentDistance(VectorPoint p, VectorPoint a, VectorPoint b) {
	const auto segment = b-a;
	const double length2 = Dot(segment,segment);
	const double t = length2 > 1e-24 ? std::clamp(Dot(p-a,segment)/length2,0.0,1.0) : 0;
	return Length(p-(a+segment*t));
}
struct Failure { std::string message; };
void Require(bool condition, const char* message) { if (!condition) throw Failure{message}; }
struct Contour { std::vector<VectorPoint> points; bool closed = false, drawn = false; };
using Contours = std::vector<Contour>;
class Flatten {
public:
	explicit Flatten(const VectorOptions& options) : options(options) {}
	Contours Run(const VectorPath& path) {
		Require(!path.commands.empty(),"path has no commands");
		for (const auto& command : path.commands) {
			auto point = [&](unsigned i) {
				const auto p = command.points[i].Resolve(options.widthDp,options.heightDp);
				Require(Finite(p) && Finite(options.transform.Apply(p)),"non-finite path coordinate or transform");
				return p;
			};
			if (command.op == PathOperation::Move) {
				contours.emplace_back();
				Append(point(0));
				current = start = point(0);
				continue;
			}
			Require(!contours.empty(),"path must begin with move");
			if (command.op == PathOperation::Close) {
				contours.back().closed = true;
				current = start;
				continue;
			}
			// A segment following close begins a new subpath at the closed start.
			if (contours.back().closed) { contours.emplace_back(); Append(current); start = current; }
			contours.back().drawn = true;
			if (command.op == PathOperation::Line) { current = point(0); Append(current); }
			else if (command.op == PathOperation::Quadratic) {
				const auto control = point(0), end = point(1);
				Curve(current,current+(control-current)*(2.0/3),end+(control-end)*(2.0/3),end,0);
				current = end;
			} else if (command.op == PathOperation::Cubic) {
				const auto end = point(2);
				Curve(current,point(0),point(1),end,0); current = end;
			} else throw Failure{"unknown path command"};
		}
		for (auto& contour : contours) if (contour.closed && contour.points.size() > 1 && Length(contour.points.front()-contour.points.back()) < 1e-10) {
			contour.points.pop_back();
		}
		return std::move(contours);
	}
private:
	void Append(VectorPoint p) {
		auto& points = contours.back().points;
		if (!points.empty() && Length(points.back()-p) < 1e-10) return;
		Require(++count <= options.maximumVertices,"adaptive path exceeds vertex budget");
		points.push_back(p);
	}
	void Curve(VectorPoint a, VectorPoint b, VectorPoint c, VectorPoint d, unsigned depth) {
		const auto pa = options.transform.Apply(a), pb = options.transform.Apply(b), pc = options.transform.Apply(c), pd = options.transform.Apply(d);
		if (std::max(SegmentDistance(pb,pa,pd),SegmentDistance(pc,pa,pd)) <= options.tolerancePixels) { Append(d); return; }
		Require(depth < 24,"curve exceeds adaptive subdivision depth");
		const auto ab = (a+b)*.5, bc = (b+c)*.5, cd = (c+d)*.5;
		const auto abc = (ab+bc)*.5, bcd = (bc+cd)*.5, middle = (abc+bcd)*.5;
		Curve(a,ab,abc,middle,depth+1); Curve(middle,bcd,cd,d,depth+1);
	}
	const VectorOptions& options;
	Contours contours;
	VectorPoint current, start;
	size_t count = 0;
};

// Bound tessellator allocations, including intersecting-contour work meshes.
struct AllocationBudget { size_t used = 0; static constexpr size_t limit = 64*1024*1024; };
// MSVC's max_align_t is only 8-byte aligned, while the tessellator's jmp_buf
// needs 16. Explicit aligned allocation also covers platforms whose ordinary
// allocation alignment is less than 16; the header preserves payload alignment.
struct alignas(16) alignas(std::max_align_t) Allocation { size_t bytes; };
void* Allocate(void* user, unsigned int bytes) {
	auto& budget = *static_cast<AllocationBudget*>(user);
	const size_t total = sizeof(Allocation)+bytes;
	if (total > budget.limit-budget.used) return nullptr;
	auto* allocation = static_cast<Allocation*>(::operator new(total,std::align_val_t{alignof(Allocation)},std::nothrow));
	if (!allocation) return nullptr;
	allocation->bytes = bytes; budget.used += total;
	return allocation+1;
}
void Deallocate(void* user, void* pointer) {
	if (!pointer) return;
	auto* allocation = static_cast<Allocation*>(pointer)-1;
	static_cast<AllocationBudget*>(user)->used -= sizeof(Allocation)+allocation->bytes;
	::operator delete(allocation,std::align_val_t{alignof(Allocation)});
}
void* Reallocate(void* user, void* pointer, unsigned int bytes) {
	if (!pointer) return Allocate(user,bytes);
	void* next = Allocate(user,bytes);
	if (next) {
		std::memcpy(next,pointer,std::min<size_t>(bytes,(static_cast<Allocation*>(pointer)-1)->bytes));
		Deallocate(user,pointer);
	}
	return next;
}
double SignedArea(const std::vector<VectorPoint>& points) {
	double area = 0;
	for (size_t i = 0; i < points.size(); ++i) area += Cross(points[i],points[(i+1)%points.size()]);
	return area*.5;
}
void AddPolygon(Contours& result, std::vector<VectorPoint> points, size_t& count, const VectorOptions& options) {
	if (points.size() < 3 || std::abs(SignedArea(points)) < 1e-18) return;
	if (SignedArea(points) < 0) std::reverse(points.begin(),points.end());
	count += points.size();
	Require(count <= options.maximumVertices,"stroke outline exceeds vertex budget");
	result.push_back({std::move(points),true,true});
}
unsigned ArcSteps(double radiusPixels, double angle, const VectorOptions& options) {
	if (radiusPixels <= options.tolerancePixels) return 1;
	const double step = 2*std::acos(std::clamp(1-options.tolerancePixels/radiusPixels,-1.0,1.0));
	const double count = std::ceil(std::abs(angle)/std::max(step,1e-9));
	Require(count <= options.maximumVertices,"round stroke exceeds vertex budget");
	return std::max(1u,static_cast<unsigned>(count));
}
Contours StrokeContours(const Contours& lines, const VectorStroke& stroke, const VectorOptions& options) {
	Contours result;
	size_t count = 0;
	const double maximumScale = options.transform.MaximumScale();
	const double minimumScale = std::abs(options.transform.a*options.transform.d-options.transform.b*options.transform.c)/maximumScale;
	const double half = .5*std::max(stroke.widthDp,stroke.minimumPixels/minimumScale);
	if (half <= 0) return result;
	auto circle = [&](VectorPoint p) {
		const unsigned steps = std::max(8u,ArcSteps(half*maximumScale,2*Pi,options));
		std::vector<VectorPoint> points;
		for (unsigned i = 0; i < steps; ++i) { const double angle = i*(2*Pi/steps); points.push_back(p+VectorPoint{std::cos(angle),std::sin(angle)}*half); }
		AddPolygon(result,std::move(points),count,options);
	};
	for (const auto& line : lines) {
		const auto& points = line.points;
		const size_t n = points.size();
		if (n == 1 && line.drawn) {
			if (stroke.cap == StrokeCap::Round) circle(points[0]);
			else if (stroke.cap == StrokeCap::Square) {
				const auto p = points[0];
				AddPolygon(result,{{p.x-half,p.y-half},{p.x+half,p.y-half},{p.x+half,p.y+half},{p.x-half,p.y+half}},count,options);
			}
			continue;
		}
		if (n < 2) continue;
		const size_t segments = line.closed ? n : n-1;
		for (size_t i = 0; i < segments; ++i) {
			auto a = points[i], b = points[(i+1)%n];
			const auto direction = Unit(b-a), normal = Normal(direction)*half;
			if (!line.closed && stroke.cap == StrokeCap::Square) {
				if (i == 0) a = a-direction*half;
				if (i+1 == segments) b = b+direction*half;
			}
			AddPolygon(result,{a+normal,a-normal,b-normal,b+normal},count,options);
		}
		for (size_t i = line.closed ? 0 : 1; i < (line.closed ? n : n-1); ++i) {
			const auto p = points[i], before = Unit(p-points[(i+n-1)%n]), after = Unit(points[(i+1)%n]-p);
			const double turn = Cross(before,after);
			if (std::abs(turn) < 1e-10) { if (Dot(before,after) < 0 && stroke.join == StrokeJoin::Round) circle(p); continue; }
			const double side = turn > 0 ? -1 : 1;
			const auto a = p+Normal(before)*(side*half), b = p+Normal(after)*(side*half);
			std::vector<VectorPoint> join{p,a};
			if (stroke.join == StrokeJoin::Round) {
				const double angle = std::atan2(turn,Dot(before,after));
				const double start = std::atan2(a.y-p.y,a.x-p.x);
				const unsigned steps = ArcSteps(half*maximumScale,angle,options);
				for (unsigned j = 1; j < steps; ++j) {
					const double t = start+angle*j/steps;
					join.push_back(p+VectorPoint{std::cos(t),std::sin(t)}*half);
				}
			} else if (stroke.join == StrokeJoin::Miter) {
				const auto miter = a+before*(Cross(b-a,after)/turn);
				if (Length(miter-p) <= half*stroke.miterLimit) join.push_back(miter);
			}
			join.push_back(b); AddPolygon(result,std::move(join),count,options);
		}
		if (!line.closed && stroke.cap == StrokeCap::Round) { circle(points.front()); circle(points.back()); }
	}
	return result;
}
VectorColour Premultiply(VectorColour colour) { return {colour.r*colour.a,colour.g*colour.a,colour.b*colour.a,colour.a}; }
VectorColour Blend(VectorColour a, VectorColour b, double t) {
	return {a.r+(b.r-a.r)*t,a.g+(b.g-a.g)*t,a.b+(b.b-a.b)*t,a.a+(b.a-a.a)*t};
}
class Paint {
public:
	Paint(const VectorPaint& source, const VectorOptions& options) : source(source) {
		Require(options.transform.Inverse(inverse),"paint transform is singular");
		from = source.from.Resolve(options.widthDp,options.heightDp);
		direction = source.to.Resolve(options.widthDp,options.heightDp)-from;
		length2 = Dot(direction,direction);
		if (source.type == PaintType::Linear) {
			Require(Finite(from) && Finite(direction) && length2 > 1e-20,"gradient endpoints coincide or are invalid");
			Require(source.stops.size() >= 2 && source.stops.front().at == 0 && source.stops.back().at == 1,"gradient requires endpoint stops at zero and one");
			double previous = -1;
			for (const auto& stop : source.stops) {
				Require(std::isfinite(stop.at) && stop.at >= 0 && stop.at <= 1 && stop.at > previous,"gradient stops must increase strictly");
				previous = stop.at;
				Validate(stop.colour);
			}
		} else Validate(source.colour);
	}
	double Parameter(VectorPoint point) const { return Dot(inverse.Apply(point)-from,direction)/length2; }
	VectorColour Colour(VectorPoint point) const {
		if (source.type != PaintType::Linear) return Premultiply(source.colour);
		const double t = std::clamp(Parameter(point),0.0,1.0);
		for (size_t i = 1; i < source.stops.size(); ++i) if (t <= source.stops[i].at) {
			const auto& a = source.stops[i-1]; const auto& b = source.stops[i];
			return Blend(Premultiply(a.colour),Premultiply(b.colour),(t-a.at)/(b.at-a.at));
		}
		return Premultiply(source.stops.back().colour);
	}
	void Shape(const std::vector<VectorPoint>& shape, VectorMesh& output, const VectorOptions& options, double coverage = 1) const {
		if (source.type != PaintType::Linear) { Polygon(shape,output,options,coverage); return; }
		// Split at every stop plane. A triangle spanning several stops cannot
		// reproduce them by interpolating only the triangle's corner colours.
		for (size_t i = 0; i <= source.stops.size(); ++i) {
			auto polygon = shape;
			if (i > 0) Clip(polygon,source.stops[i-1].at,true);
			if (i < source.stops.size()) Clip(polygon,source.stops[i].at,false);
			Polygon(polygon,output,options,coverage);
		}
	}
private:
	static void Validate(VectorColour c) {
		for (double value : {c.r,c.g,c.b,c.a}) Require(std::isfinite(value) && value >= 0 && value <= 1,"paint colour must be finite normalized RGBA");
	}
	void Clip(std::vector<VectorPoint>& polygon, double edge, bool greater) const {
		if (polygon.empty()) return;
		std::vector<VectorPoint> clipped;
		auto previous = polygon.back();
		double before = (Parameter(previous)-edge)*(greater ? 1 : -1);
		for (auto point : polygon) {
			const double distance = (Parameter(point)-edge)*(greater ? 1 : -1);
			if ((distance >= 0) != (before >= 0)) clipped.push_back(previous+(point-previous)*(before/(before-distance)));
			if (distance >= 0) clipped.push_back(point);
			previous = point; before = distance;
		}
		polygon.swap(clipped);
	}
	void Polygon(const std::vector<VectorPoint>& polygon, VectorMesh& output, const VectorOptions& options, double coverage) const {
		if (polygon.size() < 3 || std::abs(SignedArea(polygon)) < 1e-12) return;
		Require(output.vertices.size()+polygon.size() <= options.maximumVertices,"paint subdivision exceeds output vertex budget");
		const int base = static_cast<int>(output.vertices.size());
		for (auto p : polygon) { const auto c = Colour(p); output.vertices.push_back({p.x,p.y,c.r*coverage,c.g*coverage,c.b*coverage,c.a*coverage}); }
		for (int i = 2; i < static_cast<int>(polygon.size()); ++i) output.indices.insert(output.indices.end(),{base,base+i-1,base+i});
	}
	const VectorPaint& source;
	VectorTransform inverse;
	VectorPoint from, direction;
	double length2 = 1;
};

// Integrate normalized region boundaries over output pixel cells. For each
// oriented edge, Green's theorem gives coverage as integral(clamp(x-X,0,1) dy).
// Resolve all edges before painting, so holes and overlapping stroke segments
// cannot darken themselves and there are no internal triangulation seams.
class Coverage {
public:
	explicit Coverage(const VectorOptions& options) : options(options) {
		bounds = options.pixelBounds.value_or(VectorPixelBounds{-1000000,-1000000,1000000,1000000});
	}
	void Edge(VectorPoint a, VectorPoint b) {
		if (a.y == b.y || bounds.left == bounds.right || bounds.top == bounds.bottom) return;
		const double sign = a.y < b.y ? 1 : -1;
		if (a.y > b.y) std::swap(a,b);
		const int top = std::max(bounds.top,static_cast<int>(std::floor(a.y)));
		const int bottom = std::min(bounds.bottom,static_cast<int>(std::ceil(b.y)));
		Require(bottom-top <= 65536,"coverage edge exceeds row budget; provide viewport bounds");
		for (int row = top; row < bottom; ++row) {
			Charge();
			const double y0 = std::max(a.y,static_cast<double>(row));
			const double y1 = std::min(b.y,static_cast<double>(row+1));
			const double dy = sign*(y1-y0);
			const double x0 = a.x+(b.x-a.x)*((y0-a.y)/(b.y-a.y));
			const double x1 = a.x+(b.x-a.x)*((y1-a.y)/(b.y-a.y));
			const double low = std::min(x0,x1), high = std::max(x0,x1);
			const int first = static_cast<int>(std::floor(low));
			Range(row,bounds.left,first,dy);
			// Only columns crossed by this edge need a partial-cell integral.
			// A vertical edge has one such column, including integer-aligned edges.
			const int last = high == low ? first+1 : static_cast<int>(std::ceil(high));
			for (int column = std::max(first,bounds.left); column < std::min(last,bounds.right); ++column) {
				Charge();
				const double average = high-low < 1e-12 ? std::clamp(low-column,0.0,1.0)
					: (Integral(high-column)-Integral(low-column))/(high-low);
				Range(row,column,column+1,dy*average);
			}
		}
	}
	void Emit(const Paint& paint, VectorMesh& output) {
		std::sort(events.begin(),events.end(),[](const Event& a, const Event& b) {
			return a.y != b.y ? a.y < b.y : a.x < b.x;
		});
		struct Rectangle { int left, top, right, bottom; double coverage; };
		std::vector<Rectangle> rectangles;
		std::map<std::pair<int,int>,size_t> previous;
		size_t index = 0;
		while (index < events.size()) {
			const int row = events[index].y;
			std::map<std::pair<int,int>,size_t> current;
			auto emit = [&](int left, int right, double coverage) {
				if (left >= right || coverage == 0) return;
				const auto key = std::make_pair(left,right);
				auto found = previous.find(key);
				if (found != previous.end() && rectangles[found->second].bottom == row &&
					std::abs(rectangles[found->second].coverage-coverage) < 1e-12) {
					rectangles[found->second].bottom = row+1; current[key] = found->second;
				} else {
					Require(rectangles.size() < options.maximumVertices/4,"coverage exceeds output rectangle budget");
					current[key] = rectangles.size(); rectangles.push_back({left,row,right,row+1,coverage});
				}
			};
			double sum = 0, pendingCoverage = 0;
			int pendingLeft = events[index].x, pendingRight = pendingLeft;
			while (index < events.size() && events[index].y == row) {
				const int x = events[index].x;
				do { sum += events[index++].delta; } while (index < events.size() && events[index].y == row && events[index].x == x);
				Require(sum >= -1e-7 && sum <= 1+1e-7,"normalized coverage escaped zero-to-one range");
				const double coverage = std::abs(sum) < 1e-10 ? 0 : std::abs(sum-1) < 1e-10 ? 1 : std::clamp(sum,0.0,1.0);
				const int right = index < events.size() && events[index].y == row ? events[index].x : x;
				if (x == pendingRight && std::abs(coverage-pendingCoverage) < 1e-12) pendingRight = right;
				else { emit(pendingLeft,pendingRight,pendingCoverage); pendingLeft = x; pendingRight = right; pendingCoverage = coverage; }
			}
			emit(pendingLeft,pendingRight,pendingCoverage);
			Require(std::abs(sum) < 1e-7,"coverage row has an unclosed boundary");
			previous.swap(current);
		}
		for (const auto& r : rectangles) paint.Shape({
			{static_cast<double>(r.left),static_cast<double>(r.top)},
			{static_cast<double>(r.right),static_cast<double>(r.top)},
			{static_cast<double>(r.right),static_cast<double>(r.bottom)},
			{static_cast<double>(r.left),static_cast<double>(r.bottom)}},output,options,r.coverage);
	}
private:
	struct Event { int y, x; double delta; };
	static double Integral(double x) { return x <= 0 ? 0 : x >= 1 ? x-.5 : .5*x*x; }
	void Charge() { Require(++work <= 2097152,"coverage exceeds edge-work budget"); }
	void Range(int y, int left, int right, double value) {
		left = std::max(left,bounds.left); right = std::min(right,bounds.right);
		if (left >= right || value == 0) return;
		Require(events.size()+2 <= 1048576,"coverage exceeds event-memory budget");
		events.push_back({y,left,value}); events.push_back({y,right,-value});
	}
	const VectorOptions& options;
	VectorPixelBounds bounds;
	std::vector<Event> events;
	size_t work = 0;
};

void Fill(const Contours& contours, FillRule rule, const VectorPaint& paint, const VectorOptions& options, VectorMesh& output) {
	AllocationBudget budget;
	TESSalloc allocator{};
	allocator.memalloc = Allocate; allocator.memrealloc = Reallocate; allocator.memfree = Deallocate; allocator.userData = &budget;
	std::unique_ptr<TESStesselator,decltype(&tessDeleteTess)> tess(tessNewTess(&allocator),tessDeleteTess);
	Require(tess != nullptr,"cannot allocate polygon tessellator");
	size_t vertices = 0;
	for (const auto& contour : contours) {
		if (contour.points.size() < 3) continue;
		std::vector<TESSreal> points;
		for (auto p : contour.points) {
			p = options.transform.Apply(p);
			Require(Finite(p) && std::abs(p.x) <= 1000000 && std::abs(p.y) <= 1000000,"output path coordinate exceeds supported range");
			points.push_back(static_cast<TESSreal>(p.x)); points.push_back(static_cast<TESSreal>(p.y));
		}
		tessAddContour(tess.get(),2,points.data(),2*sizeof(TESSreal),static_cast<int>(contour.points.size()));
		Require(tessGetStatus(tess.get()) == TESS_STATUS_OK,"polygon input rejected or exceeds memory budget");
		vertices += contour.points.size();
	}
	if (!vertices) return;
	const TESSreal normal[3] = {0,0,1};
	Require(tessTesselate(tess.get(),rule == FillRule::EvenOdd ? TESS_WINDING_ODD : TESS_WINDING_NONZERO,options.antialias ? TESS_BOUNDARY_CONTOURS : TESS_POLYGONS,3,2,normal) != 0,"polygon tessellation failed or exceeds memory budget");
	const int vertexCount = tessGetVertexCount(tess.get()), elementCount = tessGetElementCount(tess.get());
	Require(vertexCount >= 0 && static_cast<size_t>(vertexCount) <= options.maximumVertices,"polygon intersections exceed vertex budget");
	const TESSreal* points = tessGetVertices(tess.get());
	const TESSindex* indices = tessGetElements(tess.get());
	Paint evaluator(paint,options);
	if (options.antialias) {
		Coverage coverage(options);
		for (int i = 0; i < elementCount; ++i) {
			const int base = indices[i*2], count = indices[i*2+1];
			Require(base >= 0 && count >= 0 && base <= vertexCount && count <= vertexCount-base,"invalid boundary contour output");
			for (int j = 0; j < count; ++j) {
				const int a = base+j, b = base+(j+1)%count;
				coverage.Edge({points[a*2],points[a*2+1]},{points[b*2],points[b*2+1]});
			}
		}
		coverage.Emit(evaluator,output); return;
	}
	for (int i = 0; i < elementCount; ++i) {
		VectorPoint triangle[3];
		for (int j = 0; j < 3; ++j) {
			const int index = indices[i*3+j];
			Require(index >= 0 && index < vertexCount,"invalid polygon output index");
			triangle[j] = {points[index*2],points[index*2+1]};
		}
		evaluator.Shape({triangle[0],triangle[1],triangle[2]},output,options);
	}
}
void Validate(const VectorOptions& options) {
	Require(std::isfinite(options.widthDp) && std::isfinite(options.heightDp) && options.widthDp >= 0 && options.heightDp >= 0,"invalid vector layout extent");
	Require(std::isfinite(options.tolerancePixels) && options.tolerancePixels >= .01 && options.tolerancePixels <= 1,"curve tolerance must be 0.01..1 output pixels");
	Require(options.maximumVertices >= 3 && options.maximumVertices <= 1048576,"invalid vector vertex budget");
	if (options.pixelBounds) {
		const auto& b = *options.pixelBounds;
		Require(b.left >= -1000000 && b.top >= -1000000 && b.right <= 1000000 && b.bottom <= 1000000 && b.left <= b.right && b.top <= b.bottom,"invalid coverage pixel bounds");
	}
	for (double value : {options.transform.a,options.transform.b,options.transform.c,options.transform.d,options.transform.tx,options.transform.ty})
		Require(std::isfinite(value),"non-finite vector transform");
}
} // namespace

bool VectorTransform::Inverse(VectorTransform& result) const {
	const double determinant = a*d-b*c;
	if (!std::isfinite(determinant) || std::abs(determinant) <= 1e-20) return false;
	result = {d/determinant,-b/determinant,-c/determinant,a/determinant,(c*ty-d*tx)/determinant,(b*tx-a*ty)/determinant};
	return true;
}
double VectorTransform::MaximumScale() const {
	const double s1 = a*a+b*b, s2 = c*c+d*d, cross = a*c+b*d;
	return std::sqrt(std::max(0.0,.5*(s1+s2+std::hypot(s1-s2,2*cross))));
}
bool TessellatePath(const VectorPath& path, const VectorOptions& options, VectorMesh& output, std::string& diagnostic) {
	diagnostic.clear();
	try {
		Validate(options);
		VectorMesh candidate;
		VectorTransform inverse;
		if (options.transform.Inverse(inverse)) {
			const auto contours = Flatten(options).Run(path);
			if (path.fill.type != PaintType::None) Fill(contours,path.fillRule,path.fill,options,candidate);
			if (path.stroke.paint.type != PaintType::None) {
				Require(std::isfinite(path.stroke.widthDp) && path.stroke.widthDp >= 0 && std::isfinite(path.stroke.minimumPixels) && path.stroke.minimumPixels >= 0 && std::isfinite(path.stroke.miterLimit) && path.stroke.miterLimit >= 1,"invalid stroke width, minimum pixel width or miter limit");
				const auto stroke = StrokeContours(contours,path.stroke,options);
				Fill(stroke,FillRule::NonZero,path.stroke.paint,options,candidate);
			}
		}
		output = std::move(candidate); return true;
	} catch (const Failure& error) { diagnostic = path.id+": "+error.message; }
	catch (const std::bad_alloc&) { diagnostic = path.id+": vector allocation failed"; }
	return false;
}
bool HitTestPath(const VectorPath& path, const VectorOptions& options, VectorPoint point, bool& hit, std::string& diagnostic) {
	VectorMesh mesh;
	if (!Finite(point)) { diagnostic = path.id+": invalid hit-test coordinate"; return false; }
	auto geometricOptions = options;
	geometricOptions.antialias = false; // Partial-coverage pixel quads are not hit targets.
	if (!TessellatePath(path,geometricOptions,mesh,diagnostic)) return false;
	hit = false;
	for (size_t i = 0; i < mesh.indices.size(); i += 3) {
		const auto& a = mesh.vertices[mesh.indices[i]]; const auto& b = mesh.vertices[mesh.indices[i+1]]; const auto& c = mesh.vertices[mesh.indices[i+2]];
		const double ab = Cross({b.x-a.x,b.y-a.y},point-VectorPoint{a.x,a.y});
		const double bc = Cross({c.x-b.x,c.y-b.y},point-VectorPoint{b.x,b.y});
		const double ca = Cross({a.x-c.x,a.y-c.y},point-VectorPoint{c.x,c.y});
		if ((ab >= -1e-9 && bc >= -1e-9 && ca >= -1e-9) || (ab <= 1e-9 && bc <= 1e-9 && ca <= 1e-9)) { hit = true; break; }
	}
	return true;
}
} // namespace openq4::ui
