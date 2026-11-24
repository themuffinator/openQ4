// Copyright (C) 2026 DarkMatter Productions. Quake 4 SDK EULA.
#include "src/game/mp/ObjectiveMarkerLayout.h"
#include "src/framework/VRMathCore.h"
#include <cstdio>
#include <cstdlib>
#include <limits>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"line %d: %s\n",__LINE__,#x); std::exit(1); } } while (0)
static bool Near(float a,float b) { return std::abs(a-b)<.01f; }

int main() {
	using namespace objectiveMarker;
	Placement p;
	CHECK(Project(0,0,100,1,.75f,640,480,62,80,p));
	CHECK(!p.edge && Near(p.x,320) && Near(p.y,240) && Near(p.emphasis,.22f));
	CHECK(Project(50,25,100,1,.75f,640,480,62,80,p));
	CHECK(!p.edge && Near(p.x,480) && Near(p.y,160) && Near(p.emphasis,1));
	CHECK(Project(1000,0,100,1,.75f,640,480,62,80,p));
	CHECK(p.edge && Near(p.x,578) && Near(p.y,240) && Near(p.directionX,1));
	CHECK(Project(50,0,-100,1,.75f,640,480,62,80,p));
	CHECK(p.edge && Near(p.x,578)); // Behind on the right stays on the right.
	CHECK(Project(-50,0,-100,1,.75f,640,480,62,80,p));
	CHECK(p.edge && Near(p.x,62));
	CHECK(Project(0,0,-100,1,.75f,640,480,62,80,p));
	CHECK(p.edge && Near(p.y,400) && Near(p.directionY,1));
	CHECK(Project(0,100,0,1,.75f,640,480,62,80,p));
	CHECK(p.edge && Near(p.y,80));
	CHECK(!Project(std::numeric_limits<float>::quiet_NaN(),0,10,1,1,640,480,62,80,p));
	CHECK(!Project(1,1,10,0,1,640,480,62,80,p));
	CHECK(!Project(1,1,10,1,1,100,480,62,80,p));
	CHECK(!Project(1,1,10,1,1,std::numeric_limits<float>::infinity(),480,62,80,p));
	CHECK(!Project(1,1,10,1,1,640,480,-1,80,p));
	// Sweep every bearing through the camera plane, and every pitch, on all
	// supported aspect classes. Includes the zero vector and tiny near depth.
	for (float width : {270.f,640.f,853.3333f,1120.f,1920.f}) {
		for (int yaw=-180;yaw<=180;yaw+=3) for (int pitch=-90;pitch<=90;pitch+=9) {
			const float a=yaw*.01745329252f,b=pitch*.01745329252f;
			CHECK(Project(std::sin(a)*std::cos(b),std::sin(b),std::cos(a)*std::cos(b),
				width/640.f,.75f,width,480,62,80,p));
			CHECK(std::isfinite(p.x) && std::isfinite(p.y));
			CHECK(p.x>=61.99f && p.x<=width-61.99f && p.y>=79.99f && p.y<=400.01f);
			CHECK(p.emphasis>=.22f && p.emphasis<=1);
		}
	}
	// A forward gaze lands above the centre of the lowered controller HUD,
	// but remains subdued. Behind-head cues never inherit that displacement.
	CHECK(ProjectOnHud(0,0,1,1.4f,1.2f,-.1f,853.3333f,480,38,80,p));
	CHECK(!p.edge && Near(p.y,168.8889f) && Near(p.emphasis,.22f));
	CHECK(ProjectOnHud(0,0,-1,1.4f,1.2f,-.1f,853.3333f,480,38,80,p));
	CHECK(p.edge && Near(p.y,400) && Near(p.directionY,1));
	CHECK(ProjectOnHud(0,-.1f,1.4f,1.4f,1.2f,-.1f,853.3333f,480,38,80,p));
	CHECK(!p.edge && Near(p.y,240));
	CHECK(!ProjectOnHud(0,0,1,0,1,0,640,480,38,80,p));
	CHECK(!ProjectOnHud(0,0,1,1,0,0,640,480,38,80,p));
	CHECK(!ProjectOnHud(0,0,1,1,1,std::numeric_limits<float>::quiet_NaN(),640,480,38,80,p));
	int intersections=0;
	// Independently intersect each bearing with the compositor's physical
	// quad. Then rays from eyes 64 mm apart must converge on the same texel:
	// markers have HUD depth, even with asymmetric eye FOVs and a shifted HUD.
	for (float canvas : {640.f,853.3333f,1920.f})
	for (float distance : {.5f,1.4f,5.f}) for (float physicalWidth : {.3f,1.2f,4.f})
	for (float offset : {-1.f,-.1f,0.f,1.f})
	for (int x=-10;x<=10;++x) for (int y=-10;y<=10;++y) {
		const float right=x*.1f,up=y*.1f,physicalHeight=physicalWidth*480/canvas;
		CHECK(ProjectOnHud(right,up,1,distance,physicalWidth,offset,canvas,480,38,80,p));
		CHECK(p.x>=37.99f && p.x<=canvas-37.99f && p.y>=79.99f && p.y<=400.01f);
		if (p.edge) continue;
		const vrVec3_t center=VR_Vec3(0,offset,-distance);
		const vrQuat_t orientation={0,0,0,1};
		const auto hit=VR_RayQuadIntersect(VR_Vec3(0,0,0),VR_Vec3(right,up,-1),
			center,orientation,physicalWidth,physicalHeight);
		CHECK(hit.hit && Near(hit.u*canvas,p.x) && Near(hit.v*480,p.y));
		const vrVec3_t point=VR_Vec3(right*distance,up*distance,-distance);
		for (float eyeX : {-.032f,.032f}) {
			const vrVec3_t eye=VR_Vec3(eyeX,0,0);
			const auto eyeHit=VR_RayQuadIntersect(eye,VR_Sub(point,eye),center,orientation,physicalWidth,physicalHeight);
			CHECK(eyeHit.hit && Near(eyeHit.u*canvas,p.x) && Near(eyeHit.v*480,p.y));
		}
		++intersections;
	}
	CHECK(intersections>1000);
	CHECK(Overlaps(100,100,13,13,115,100,13,13));
	CHECK(!Overlaps(100,100,13,13,130,100,13,13));
	CHECK(Overlaps(100,100,60,28,200,100,60,28));
	CHECK(!Overlaps(100,100,60,28,100,160,60,28));
	std::printf("Objective marker projection: 12,705 desktop and 47,628 VR bearings, %d stereo quad intersections and boundary checks passed\n",intersections);
}
