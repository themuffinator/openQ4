// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "PopupPlacement.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace openq4::ui {
namespace {
constexpr double Limit = 10000000, Epsilon = 1e-8;
bool Finite(double v) { return std::isfinite(v) && std::abs(v) <= Limit; }
bool Valid(const PopupRect& r) {
    return Finite(r.x) && Finite(r.y) && Finite(r.width) && Finite(r.height) &&
        r.width > 0 && r.height > 0 && Finite(r.x+r.width) && Finite(r.y+r.height);
}
double Distance(const PopupHalfPlane& p, const PopupPoint& v) { return p.x*v.x+p.y*v.y-p.limit; }
struct Polygon { std::array<PopupPoint,16> points{}; std::size_t count = 0; };
bool Clip(Polygon& polygon, const PopupHalfPlane& plane) {
    Polygon next;
    if (!polygon.count) return false;
    const auto append = [&](PopupPoint p) {
        if (next.count == next.points.size() || !Finite(p.x) || !Finite(p.y)) return false;
        next.points[next.count++] = p; return true;
    };
    auto before = polygon.points[polygon.count-1]; double a = Distance(plane,before);
    for (std::size_t i=0; i<polygon.count; ++i) {
        const auto after = polygon.points[i]; const double b = Distance(plane,after);
        if ((a <= 0) != (b <= 0)) {
            const double t = a/(a-b);
            if (!append({before.x+t*(after.x-before.x),before.y+t*(after.y-before.y)})) return false;
        }
        if (b <= 0 && !append(after)) return false;
        before=after; a=b;
    }
    polygon=next; return polygon.count != 0;
}
bool Origins(const PopupRegion& region, double width, double height, const PopupRect& anchor,
    int side, Polygon& out) {
    const auto& v=region.viewport;
    Polygon p{{PopupPoint{v.x,v.y}, {v.x+v.width,v.y}, {v.x+v.width,v.y+v.height}, {v.x,v.y+v.height}},4};
    for (std::size_t i=0; i<region.count; ++i) {
        auto plane=region.planes[i];
        plane.limit-=std::max(0.0,plane.x*width)+std::max(0.0,plane.y*height);
        if (!Clip(p,plane)) return false;
    }
    if (side > 0 && !Clip(p,{0,-1,-anchor.y-anchor.height})) return false;
    if (side < 0 && !Clip(p,{0,1,anchor.y-height})) return false;
    out=p; return true;
}
PopupPoint Nearest(const Polygon& p, PopupPoint wanted) {
    // Projection onto a convex polygon includes its interior. Degenerate line
    // or point feasible sets still yield an exact bounded placement.
    bool positive=false,negative=false;
    for (std::size_t i=0; i<p.count; ++i) {
        const auto a=p.points[i],b=p.points[(i+1)%p.count];
        const double cross=(b.x-a.x)*(wanted.y-a.y)-(b.y-a.y)*(wanted.x-a.x);
        positive |= cross > Epsilon; negative |= cross < -Epsilon;
    }
    if (p.count>=3 && !(positive && negative) && (positive || negative)) return wanted;
    PopupPoint result=p.points[0]; double best=std::numeric_limits<double>::infinity();
    for (std::size_t i=0; i<p.count; ++i) {
        const auto a=p.points[i],b=p.points[(i+1)%p.count]; const double x=b.x-a.x,y=b.y-a.y;
        const double length=x*x+y*y;
        const double t=length>0?std::clamp(((wanted.x-a.x)*x+(wanted.y-a.y)*y)/length,0.0,1.0):0;
        const PopupPoint point{a.x+t*x,a.y+t*y};
        const double distance=(point.x-wanted.x)*(point.x-wanted.x)+(point.y-wanted.y)*(point.y-wanted.y);
        if (distance<best) {best=distance;result=point;}
    }
    return result;
}
double Height(const PopupRegion& region, const PopupRect& anchor, double width,
    double desired, double minimum, int side) {
    Polygon p;
    if (!Origins(region,width,minimum,anchor,side,p)) return 0;
    if (Origins(region,width,desired,anchor,side,p)) return desired;
    double low=minimum,high=desired;
    for (unsigned i=0; i<48; ++i) {
        const double middle=(low+high)*.5;
        if (Origins(region,width,middle,anchor,side,p)) low=middle; else high=middle;
    }
    return low;
}
bool Valid(const PopupRegion& r) {
    if (r.count != 8 || !Valid(r.viewport)) return false;
    for (const auto& p:r.planes) if (!Finite(p.x) || !Finite(p.y) || !Finite(p.limit) ||
        std::abs(p.x*p.x+p.y*p.y-1)>1e-6) return false;
    return true;
}
}
bool MeasurePopupRegion(const std::array<PopupPoint,4>& quad, const PopupRect& viewport,
    double inset, PopupRegion& out) noexcept {
    if (!Valid(viewport) || !Finite(inset) || inset<0) return false;
    double winding=0;
    for (std::size_t i=0; i<4; ++i) {
        const auto a=quad[i],b=quad[(i+1)%4],c=quad[(i+2)%4];
        if (!Finite(a.x) || !Finite(a.y)) return false;
        const double cross=(b.x-a.x)*(c.y-b.y)-(b.y-a.y)*(c.x-b.x);
        if (!std::isfinite(cross) || std::abs(cross)<=Epsilon || (winding && cross*winding<0)) return false;
        winding=cross;
    }
    PopupRegion candidate; candidate.viewport=viewport;
    for (std::size_t i=0; i<4; ++i) {
        const auto a=quad[i],b=quad[(i+1)%4]; const double dx=b.x-a.x,dy=b.y-a.y,length=std::hypot(dx,dy);
        const double sign=winding>0?1:-1;
        const double x=sign*dy/length,y=-sign*dx/length;
        candidate.planes[candidate.count++]={x,y,x*a.x+y*a.y-inset};
    }
    candidate.planes[candidate.count++]={-1,0,-viewport.x-inset};
    candidate.planes[candidate.count++]={1,0,viewport.x+viewport.width-inset};
    candidate.planes[candidate.count++]={0,-1,-viewport.y-inset};
    candidate.planes[candidate.count++]={0,1,viewport.y+viewport.height-inset};
    if (!Valid(candidate)) return false;
    out=candidate;return true;
}
bool PopupRegionContains(const PopupRegion& region, const PopupRect& rect, double tolerance) noexcept {
    if (!Valid(region) || !Valid(rect) || !std::isfinite(tolerance) || tolerance<0) return false;
    for (const auto& p:region.planes) {
        const double far=p.x*rect.x+p.y*rect.y+std::max(0.0,p.x*rect.width)+std::max(0.0,p.y*rect.height);
        if (far>p.limit+tolerance) return false;
    }
    return true;
}
bool PlacePopup(const PopupRegion& region, const PopupRect& anchor, double preferredWidth,
    double minWidth, double desiredHeight, double minHeight, PopupPlacement& out) noexcept {
    if (!Valid(region) || !Valid(anchor) || !Finite(preferredWidth) || !Finite(minWidth) ||
        !Finite(desiredHeight) || !Finite(minHeight) || minWidth<=0 || minHeight<=0 ||
        preferredWidth<minWidth || desiredHeight<minHeight) return false;
    Polygon feasible; double width=preferredWidth;
    if (!Origins(region,width,minHeight,anchor,0,feasible)) {
        if (!Origins(region,minWidth,minHeight,anchor,0,feasible)) return false;
        double low=minWidth,high=width;
        for (unsigned i=0;i<48;++i) {
            const double middle=(low+high)*.5;
            if (Origins(region,middle,minHeight,anchor,0,feasible)) low=middle; else high=middle;
        }
        width=low;
    }
    const double below=Height(region,anchor,width,desiredHeight,minHeight,1);
    const double above=Height(region,anchor,width,desiredHeight,minHeight,-1);
    int side=1;double height=below;
    if (below<desiredHeight && above>below) {side=-1;height=above;}
    if (!height) {side=0;height=Height(region,anchor,width,desiredHeight,minHeight,0);}
    if (!height || !Origins(region,width,height,anchor,side,feasible)) return false;
    const auto at=Nearest(feasible,{anchor.x,side<0?anchor.y-height:anchor.y+anchor.height});
    const PopupPlacement candidate{{at.x,at.y,width,height},side<0,side==0};
    if (!PopupRegionContains(region,candidate.rectangle,1e-6)) return false;
    out=candidate;return true;
}
} // namespace openq4::ui
