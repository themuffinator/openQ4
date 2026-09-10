// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "ScrollGeometry.h"
#include <algorithm>
#include <cmath>

namespace openq4::ui {
namespace {
bool Extent(double value) noexcept { return std::isfinite(value) && value >= 0; }
bool Geometry(const ScrollGeometry& g) noexcept {
    return Extent(g.viewport) && Extent(g.range) && Extent(g.offset) && g.offset <= g.range &&
        Extent(g.track) && Extent(g.thumb) && g.thumb <= g.track &&
        Extent(g.position) && Extent(g.travel) && g.travel == g.track-g.thumb && g.position <= g.travel &&
        g.usable == (g.viewport > 0 && g.range > 0 && g.thumb > 0 && g.travel > 0);
}
}
bool MeasureScroll(const ScrollMetrics& m, ScrollGeometry& out) noexcept {
    if (!Extent(m.viewport) || !Extent(m.content) || !std::isfinite(m.offset) ||
        !Extent(m.track) || !Extent(m.minimumThumb)) return false;
    ScrollGeometry g;
    g.viewport = m.viewport;
    g.range = m.content > m.viewport ? m.content-m.viewport : 0;
    g.offset = std::clamp(m.offset,0.0,g.range);
    g.track = m.track;
    // Divide before multiplying: finite layout inputs need not have a finite
    // product. The fraction is bounded even for very large content extents.
    const double ratio = m.content > 0 ? std::min(1.0,m.viewport/m.content) : 1;
    g.thumb = std::clamp(m.track*ratio,std::min(m.minimumThumb,m.track),m.track);
    g.travel = m.track-g.thumb;
    g.position = g.range > 0 ? (g.offset/g.range)*g.travel : 0;
    g.usable = g.viewport > 0 && g.range > 0 && g.thumb > 0 && g.travel > 0;
    out = g;
    return true;
}
bool ScrollDragOffset(const ScrollGeometry& g, double pointer, double grab, double& out) noexcept {
    if (!Geometry(g) || !g.usable || !std::isfinite(pointer) ||
        !std::isfinite(grab) || grab < 0 || grab > 1) return false;
    // Clamp before subtracting so extreme finite pointer values cannot overflow.
    const double start = std::clamp(pointer,0.0,g.track)-grab*g.thumb;
    out = (std::clamp(start,0.0,g.travel)/g.travel)*g.range;
    return true;
}
bool ScrollStepOffset(const ScrollGeometry& g, ScrollStep step, double line, double& out) noexcept {
    if (!Geometry(g) || !Extent(line) || line == 0 || g.viewport <= 0) return false;
    double distance = line;
    switch (step) {
        case ScrollStep::Start: out = 0; return true;
        case ScrollStep::End: out = g.range; return true;
        case ScrollStep::PageBackward:
        case ScrollStep::PageForward: distance = std::max(g.viewport-std::min(line,g.viewport),std::min(line,g.viewport)); break;
        case ScrollStep::LineBackward:
        case ScrollStep::LineForward: break;
        default: return false;
    }
    // Saturating addition/subtraction also handles steps larger than the range.
    out = step == ScrollStep::LineBackward || step == ScrollStep::PageBackward ?
        g.offset-std::min(distance,g.offset) : g.offset+std::min(distance,g.range-g.offset);
    return true;
}
} // namespace openq4::ui
