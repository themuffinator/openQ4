// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "../../../src/ui/retained/ScrollGeometry.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <limits>

using namespace openq4::ui;
namespace {
int checks = 0, failures = 0;
void Check(bool condition, int line) {
    ++checks;
    if (!condition) { ++failures; std::fprintf(stderr,"Scroll geometry assertion at line %d\n",line); }
}
#define CHECK(value) Check((value),__LINE__)
bool Near(double a, double b) { return std::abs(a-b) <= 1e-9*std::max(1.0,std::abs(b)); }
void MeasureCases() {
    struct Case { ScrollMetrics input; double range, offset, thumb, position; bool usable; };
    const Case cases[] = {
        {{200,1000,0,200,36},800,0,40,0,true},
        {{200,1000,400,200,36},800,400,40,80,true},
        {{200,1000,800,200,36},800,800,40,160,true},
        {{200,1000,-40,200,36},800,0,40,0,true},
        {{200,1000,900,200,36},800,800,40,160,true},
        {{200,100000,0,200,36},99800,0,36,0,true},
        {{200,200,12,200,36},0,0,200,0,false},
        {{200,150,12,200,36},0,0,200,0,false},
        {{200,0,12,200,36},0,0,200,0,false},
        {{0,1000,0,200,36},1000,0,36,0,false},
        {{200,1000,0,0,36},800,0,0,0,false},
        {{200,1000,400,20,36},800,400,20,0,false},
    };
    for (const auto& c : cases) {
        ScrollGeometry g;
        CHECK(MeasureScroll(c.input,g)); CHECK(Near(g.range,c.range)); CHECK(Near(g.offset,c.offset));
        CHECK(Near(g.thumb,c.thumb)); CHECK(Near(g.position,c.position)); CHECK(g.usable == c.usable);
        CHECK(Near(g.travel,g.track-g.thumb));
    }
    const double bad[] = {-1,std::numeric_limits<double>::infinity(),-std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()};
    for (int field=0;field<5;++field) for (double value:bad) {
        if (field==2 && value==-1) continue; // An out-of-range finite offset clamps.
        ScrollMetrics m{200,1000,400,200,36};
        double* fields[]={&m.viewport,&m.content,&m.offset,&m.track,&m.minimumThumb}; *fields[field]=value;
        ScrollGeometry g; g.position=123;
        CHECK(!MeasureScroll(m,g)); CHECK(g.position==123 && g.track==0);
    }
}
void DragCases() {
    ScrollGeometry g;
    CHECK(MeasureScroll({200,1000,400,200,36},g));
    for (double grab : {0.0,0.25,0.5,0.75,1.0}) {
        double value=-1;
        CHECK(ScrollDragOffset(g,g.position+g.thumb*grab,grab,value)); CHECK(Near(value,400));
        CHECK(ScrollDragOffset(g,-1e300,grab,value)); CHECK(value==0);
        CHECK(ScrollDragOffset(g,1e300,grab,value)); CHECK(value==g.range);
        double previous=-1;
        for (int position=0;position<=200;++position) {
            CHECK(ScrollDragOffset(g,position,grab,value)); CHECK(value>=previous && value>=0 && value<=g.range);
            previous=value;
        }
    }
    for (double scale:{0.75,1.0,1.25,1.5,2.0,2.75}) {
        ScrollGeometry scaled;
        CHECK(MeasureScroll({200*scale,1000*scale,400*scale,200*scale,36*scale},scaled));
        double value=-1;
        CHECK(ScrollDragOffset(scaled,100*scale,0.5,value)); CHECK(Near(value,400*scale));
        CHECK(Near(scaled.position,g.position*scale)); CHECK(Near(scaled.thumb,g.thumb*scale));
    }
    // After reflow the same relative grab remains at the pointer, including a
    // minimum-size thumb becoming a proportional thumb.
    ScrollGeometry resized;
    CHECK(MeasureScroll({500,1000,250,500,36},resized));
    double value=-1;
    CHECK(ScrollDragOffset(resized,resized.position+resized.thumb*0.25,0.25,value)); CHECK(Near(value,250));
    for (double bad:{-0.1,1.1,std::numeric_limits<double>::quiet_NaN()}) {
        value=123; CHECK(!ScrollDragOffset(g,100,bad,value)); CHECK(value==123);
    }
    value=123; CHECK(!ScrollDragOffset(g,std::numeric_limits<double>::infinity(),0.5,value)); CHECK(value==123);
    g.travel=99; CHECK(!ScrollDragOffset(g,100,0.5,value)); CHECK(value==123);
}
void StepsAndExtremes() {
    ScrollGeometry g;
    CHECK(MeasureScroll({200,1000,400,200,36},g));
    double value=-1;
    struct Step { ScrollStep command; double result; };
    for (auto s : {Step{ScrollStep::LineBackward,364},Step{ScrollStep::LineForward,436},
            Step{ScrollStep::PageBackward,236},Step{ScrollStep::PageForward,564},Step{ScrollStep::Start,0},Step{ScrollStep::End,800}}) {
        CHECK(ScrollStepOffset(g,s.command,36,value)); CHECK(value==s.result);
    }
    CHECK(ScrollStepOffset(g,ScrollStep::LineForward,1e300,value)); CHECK(value==800);
    CHECK(ScrollStepOffset(g,ScrollStep::LineBackward,1e300,value)); CHECK(value==0);
    CHECK(MeasureScroll({200,1000,790,200,36},g));
    CHECK(ScrollStepOffset(g,ScrollStep::PageForward,36,value)); CHECK(value==800);
    for (double bad : {0.0,-1.0,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}) {
        value=123; CHECK(!ScrollStepOffset(g,ScrollStep::PageForward,bad,value)); CHECK(value==123);
    }
    CHECK(!ScrollStepOffset(g,static_cast<ScrollStep>(100),36,value)); CHECK(value==123);
    const double huge=std::numeric_limits<double>::max();
    CHECK(MeasureScroll({huge/4,huge,huge/2,huge/2,36},g));
    CHECK(std::isfinite(g.position) && std::isfinite(g.thumb));
    CHECK(ScrollDragOffset(g,g.track,1,value)); CHECK(Near(value,g.range));
    CHECK(ScrollStepOffset(g,ScrollStep::LineForward,huge,value)); CHECK(value==g.range);
    CHECK(MeasureScroll({0,0,0,0,0},g));
    value=123; CHECK(!ScrollDragOffset(g,0,0,value)); CHECK(value==123);
    CHECK(!ScrollStepOffset(g,ScrollStep::PageForward,36,value)); CHECK(value==123);
}
}
int main() {
    MeasureCases(); DragCases(); StepsAndExtremes();
    std::printf("UiScrollGeometryTest checks=%d failures=%d\n",checks,failures);
    return failures ? 1 : 0;
}
