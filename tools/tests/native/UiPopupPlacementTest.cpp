// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "PopupPlacement.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <random>

using namespace openq4::ui;
static unsigned checks;
static void Check(bool value,const char* reason) {++checks;if(!value){std::fprintf(stderr,"FAIL %u: %s\n",checks,reason);std::exit(1);}}
static bool Near(double a,double b) {return std::abs(a-b)<1e-5;}
static std::array<PopupPoint,4> Quad(double x,double y,double w,double h) {return {{{x,y},{x+w,y},{x+w,y+h},{x,y+h}}};}
static void Inside(const std::array<PopupPoint,4>& q,const PopupRect& r,const PopupRect& screen,double inset) {
    const auto corners=Quad(r.x,r.y,r.width,r.height);
    const double direction=(q[1].x-q[0].x)*(q[2].y-q[1].y)-(q[1].y-q[0].y)*(q[2].x-q[1].x);
    for (auto c:corners) {
        Check(c.x>=screen.x+inset-1e-5 && c.x<=screen.x+screen.width-inset+1e-5 &&
            c.y>=screen.y+inset-1e-5 && c.y<=screen.y+screen.height-inset+1e-5,"every corner inside actual drawable inset");
        for(unsigned i=0;i<4;++i) {
            const auto a=q[i],b=q[(i+1)%4]; const double dx=b.x-a.x,dy=b.y-a.y;
            const double distance=((c.x-a.x)*(-dy)+(c.y-a.y)*dx)/std::hypot(dx,dy)*(direction>0?1:-1);
            Check(distance>=inset-1e-5,"every corner inside actual transformed edge inset");
        }
    }
}
int main() {
    const PopupRect screen{0,0,1280,720};PopupRegion region;PopupPlacement result;
    auto quad=Quad(40,92,1140,520);
    Check(MeasurePopupRegion(quad,screen,5,region),"measure interior body");
    Check(PlacePopup(region,{622,508,562,102},562,100,104,52,result),"VSync fits above inside body");
    Check(result.above&&!result.overlapsAnchor&&Near(result.rectangle.y,404)&&Near(result.rectangle.height,104),"footer exclusion flips the full two-row popup above");
    Inside(quad,result.rectangle,screen,5);
    Check(PlacePopup(region,{80,120,300,80},300,100,160,52,result)&&!result.above&&!result.overlapsAnchor,"room below keeps ordinary direction");
    Check(Near(result.rectangle.x,80)&&Near(result.rectangle.y,200)&&Near(result.rectangle.height,160),"full requested below geometry");
    Inside(quad,result.rectangle,screen,5);
    auto small=Quad(10,10,180,90);Check(MeasurePopupRegion(small,screen,4,region),"small body");
    Check(PlacePopup(region,{20,35,150,40},150,80,100,48,result)&&result.overlapsAnchor,"whole region fallback retains a readable row");
    Check(result.rectangle.height>=48,"fallback never compresses row");Inside(small,result.rectangle,screen,4);
    auto narrow=Quad(10,10,90,200);Check(MeasurePopupRegion(narrow,screen,4,region),"narrow body");
    Check(PlacePopup(region,{15,20,150,40},150,80,96,48,result)&&Near(result.rectangle.width,82),"width shrinks only to actual fit");
    Inside(narrow,result.rectangle,screen,4);
    const auto saved=result;
    Check(!PlacePopup(region,{15,20,150,40},150,83,96,48,result),"minimum readable width cannot be squeezed");
    Check(Near(result.rectangle.x,saved.rectangle.x)&&Near(result.rectangle.height,saved.rectangle.height),"failed placement preserves output");
    Check(!PlacePopup(region,{15,20,150,40},150,80,48,193,result),"invalid minimum/request pair refuses");
    Check(!MeasurePopupRegion(Quad(0,0,0,100),screen,4,region),"degenerate quad refuses");
    auto bad=Quad(0,0,100,100);bad[1].x=std::numeric_limits<double>::infinity();
    Check(!MeasurePopupRegion(bad,screen,4,region),"nonfinite corner refuses");
    bad=Quad(0,0,100,100);std::swap(bad[1],bad[2]);
    Check(!MeasurePopupRegion(bad,screen,4,region),"crossed quad refuses");
    Check(!MeasurePopupRegion(quad,screen,-1,region),"negative inset refuses");
    Check(!MeasurePopupRegion(quad,{0,0,0,1},1,region),"empty drawable refuses");
    Check(MeasurePopupRegion(Quad(-100,-100,1600,1000),screen,4,region),"bounds can extend beyond actual window");
    Check(PlacePopup(region,{1200,650,300,60},300,80,120,50,result),"window intersection constrains oversized source bounds");
    Inside(Quad(-100,-100,1600,1000),result.rectangle,screen,4);
    // Independently verify projected edge distances, including skewed and
    // perspective trapezoids whose loose AABB would admit outside corners.
    for(const auto shape:{std::array<PopupPoint,4>{{{260,30},{610,240},{400,590},{50,380}}},
        std::array<PopupPoint,4>{{{100,100},{620,150},{530,600},{180,500}}},
        std::array<PopupPoint,4>{{{650,70},{900,50},{1080,620},{400,580}}}}) {
        for(bool mirrored:{false,true}) {
            auto actual=shape;if(mirrored)std::reverse(actual.begin(),actual.end());
            Check(MeasurePopupRegion(actual,screen,5,region),"real projected convex bounds");
            Check(PlacePopup(region,{250,280,260,80},260,72,200,52,result),"projected region preserves one or more whole rows");
            Inside(actual,result.rectangle,screen,5);
        }
    }
    std::mt19937 random(1729);std::uniform_real_distribution<double> angle(-1.2,1.2),size(190,440),offset(-20,20);
    for(unsigned i=0;i<600;++i) {
        const double a=angle(random),w=size(random),h=size(random),cx=500+offset(random),cy=350+offset(random);
        auto q=Quad(-w/2,-h/2,w,h);
        for(auto& p:q) {const double x=p.x,y=p.y;p={cx+x*std::cos(a)-y*std::sin(a),cy+x*std::sin(a)+y*std::cos(a)};}
        Check(MeasurePopupRegion(q,screen,4,region),"rotated bounds measure");
        Check(PlacePopup(region,{cx-45,cy-18,90,36},90,60,110,40,result),"bounded rotated body remains reachable");
        Inside(q,result.rectangle,screen,4);
        Check(result.rectangle.width>=60 && result.rectangle.height>=40,"random placement preserves declared readability");
    }
    std::printf("PASS %u popup placement checks\n",checks);
}
