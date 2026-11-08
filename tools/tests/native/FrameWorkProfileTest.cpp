// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "src/framework/FrameWorkProfile.h"
#include <cstdio>
#include <cstdlib>
#include <limits>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"line %d: %s\n",__LINE__,#x); std::exit(1); } } while (0)
using namespace openq4;
static bool Frame(FrameWorkProfile& profile, int frame, double start) {
	profile.Begin(start);
	for (int p=1; p<static_cast<int>(FrameWorkPhase::Tail); ++p) profile.Mark(static_cast<FrameWorkPhase>(p),start+p);
	return profile.End(frame,start+7);
}
int main() {
	FrameWorkProfile profile;
	CHECK(!profile.Active() && !Frame(profile,0,0));
	CHECK(!profile.Reset(-1) && !profile.Reset(60001));
	CHECK(profile.Reset(2));
	CHECK(!Frame(profile,10,100));
	CHECK(Frame(profile,11,110) && !profile.Active());
	CHECK(profile.Samples().size()==2 && profile.Samples()[0].frame==10);
	CHECK(profile.Samples()[0].Total()==7 && profile.Samples()[1].Total()==10);
	CHECK(profile.Samples()[1].milliseconds[0]==3);
	// Each frame keeps the clock reading its Wait phase began at, for external samplers.
	CHECK(profile.Samples()[0].startMs==100 && profile.Samples()[1].startMs==110);
	CHECK(!Frame(profile,12,120) && profile.Samples().size()==2);
	CHECK(profile.Reset(1)); profile.Begin(200); profile.Mark(FrameWorkPhase::Wait,201);
	CHECK(profile.Reset(1)); // Command restarts in the middle of a frame.
	profile.Mark(FrameWorkPhase::Events,202); CHECK(!profile.End(20,207));
	CHECK(Frame(profile,21,210) && profile.Samples()[0].Total()==7);
	CHECK(profile.Reset(1)); profile.Begin(0); profile.Mark(FrameWorkPhase::Wait,-1);
	CHECK(!profile.End(0,7) && profile.Samples().empty());
	profile.Begin(0); profile.Mark(FrameWorkPhase::Events,2); // Missing phase.
	CHECK(!profile.End(0,7));
	profile.Begin(0); profile.Mark(FrameWorkPhase::Wait,std::numeric_limits<double>::quiet_NaN());
	CHECK(!profile.End(0,7) && Frame(profile,1,10));
	// Stop ends an incomplete capture early and keeps the frames recorded so far.
	CHECK(profile.Reset(3) && !Frame(profile,30,300) && !Frame(profile,31,310) && profile.Active());
	CHECK(profile.Stop() && !profile.Active() && profile.Samples().size()==2 && profile.Samples()[1].frame==31);
	CHECK(!Frame(profile,32,320) && profile.Samples().size()==2);
	CHECK(profile.Reset(2) && !profile.Stop() && !profile.Active() && profile.Samples().empty());
	CHECK(profile.Reset(0) && !profile.Active() && profile.Samples().empty());
	std::puts("frame work profile: exact phases, gaps, frame start times, bounded capture, reset, stop and invalid-clock controls passed");
}
