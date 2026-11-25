// Copyright (C) 2026 DarkMatter Productions
//
// DepthOfFieldCore.h: the thin-lens circle of confusion, the game's weapon
// focus request, the pass plan and the uniform block both back ends upload.

#include "src/renderer/DepthOfFieldCore.h"

#include <cmath>
#include <cstdio>

static int failures = 0;

static void Check( bool condition, const char *message ) {
	if ( condition ) {
		return;
	}
	std::fprintf( stderr, "DepthOfFieldCoreTest: %s\n", message );
	failures++;
}

static bool Near( float a, float b, float tolerance = 0.001f ) {
	return std::fabs( a - b ) <= tolerance;
}

static depthOfFieldInputs_t Camera() {
	depthOfFieldInputs_t in = {};
	in.enabled = true;
	in.focusDistance = 200.0f;
	in.focusX = 0.5f;
	in.focusY = 0.5f;
	in.fStop = 2.8f;
	in.focalLength = 50.0f;
	in.blurScale = 1.0f;
	in.nearBlur = 1.0f;
	in.farBlur = 1.0f;
	in.maxBlur = 32.0f;
	in.quality = 2;
	in.bladeRoundness = 0.25f;
	in.highlights = 1.0f;
	in.highlightThreshold = 0.7f;
	in.fovX = 90.0f;
	in.viewWidth = 1920;
	in.viewHeight = 1080;
	return in;
}

static void TestLens() {
	Check( Near( DepthOfFieldCore_FocalLengthForFov( 90.0f ), 18.0f, 0.01f ), "90 degrees on a 36 mm sensor is an 18 mm lens" );
	Check( Near( DepthOfFieldCore_FocalLengthForFov( 39.6f ), 50.0f, 0.1f ), "39.6 degrees is a 50 mm lens" );

	depthOfFieldSettings_t s;
	Check( DepthOfFieldCore_Resolve( Camera(), s ), "an enabled camera resolves" );
	Check( !s.autoFocus && Near( s.focusDistance, 200.0f ), "a set distance is manual focus" );
	// K = f^2 / (N * sensor) * width / (S - f), every length in millimetres
	const float expectedK = 50.0f * 50.0f / ( 2.8f * 36.0f ) * 1920.0f / ( 200.0f * 25.4f - 50.0f );
	Check( Near( DepthOfFieldCore_CocScale( s, 200.0f ), expectedK, 0.01f ), "the lens term follows the thin-lens formula" );
	Check( DepthOfFieldCore_CircleOfConfusion( s, 200.0f, 200.0f, false ) == 0.0f, "the focus plane is sharp" );
	Check( Near( DepthOfFieldCore_CircleOfConfusion( s, 1.0e6f, 200.0f, false ), expectedK, 0.01f ),
		"infinity blurs by K" );
	Check( Near( DepthOfFieldCore_CircleOfConfusion( s, 100.0f, 200.0f, false ), -expectedK, 0.01f ),
		"half the focus distance blurs by -K" );
	Check( DepthOfFieldCore_CircleOfConfusion( s, 2.0f, 200.0f, false ) == -s.maxRadius, "the near blur is clamped" );

	// Zooming in (a longer lens) narrows the depth of field.
	depthOfFieldInputs_t wide = Camera();
	wide.focalLength = 0.0f;
	depthOfFieldSettings_t wideSettings;
	DepthOfFieldCore_Resolve( wide, wideSettings );
	Check( Near( wideSettings.focalMm, 18.0f, 0.01f ), "focal length 0 follows the field of view" );
	Check( DepthOfFieldCore_CocScale( wideSettings, 200.0f ) < DepthOfFieldCore_CocScale( s, 200.0f ) * 0.2f,
		"a wide lens blurs far less" );
	depthOfFieldInputs_t open = Camera();
	open.fStop = 1.4f;
	depthOfFieldSettings_t openSettings;
	DepthOfFieldCore_Resolve( open, openSettings );
	Check( Near( DepthOfFieldCore_CocScale( openSettings, 200.0f ), expectedK * 2.0f, 0.02f ),
		"halving the f-number doubles the blur" );
}

static void TestShaping() {
	depthOfFieldInputs_t in = Camera();
	in.focusRange = 50.0f;
	in.nearBlur = 0.5f;
	in.farBlur = 2.0f;
	depthOfFieldSettings_t s;
	DepthOfFieldCore_Resolve( in, s );
	Check( DepthOfFieldCore_CircleOfConfusion( s, 240.0f, 200.0f, false ) == 0.0f, "the focus range stays sharp" );
	Check( DepthOfFieldCore_CircleOfConfusion( s, 160.0f, 200.0f, false ) == 0.0f, "on both sides" );
	const float far = DepthOfFieldCore_CircleOfConfusion( s, 400.0f, 200.0f, false );
	const float expectedFar = 2.0f * DepthOfFieldCore_CocScale( s, 200.0f ) * ( 1.0f - 200.0f / 350.0f );
	Check( Near( far, expectedFar, 0.01f ), "beyond the range the blur starts from zero, scaled by r_dofFarBlur" );
	const float near = DepthOfFieldCore_CircleOfConfusion( s, 120.0f, 200.0f, false );
	const float expectedNear = 0.5f * DepthOfFieldCore_CocScale( s, 200.0f ) * ( 1.0f - 200.0f / 170.0f );
	Check( Near( near, expectedNear, 0.01f ), "and in front, scaled by r_dofNearBlur" );
	Check( DepthOfFieldCore_CircleOfConfusion( s, 12.0f, 200.0f, true ) < 0.0f,
		"the r_dof camera treats the view weapon as near geometry" );
}

static void TestWeaponRequest() {
	depthOfFieldInputs_t in = Camera();
	in.requestAmount = 0.5f;
	in.requestFlags = DOF_REQUEST_FOCUS_VIEW_WEAPON;
	depthOfFieldSettings_t s;
	Check( DepthOfFieldCore_Resolve( in, s ), "the game's request resolves" );
	Check( s.weaponFocus && !s.autoFocus, "the request focuses the view weapon" );
	Check( Near( s.fixedScale, DOF_WEAPON_MAX_BLUR * 0.5f ) && Near( s.maxRadius, s.fixedScale ),
		"the blur fades in with the request" );
	Check( DepthOfFieldCore_CircleOfConfusion( s, 12.0f, s.focusDistance, true ) == 0.0f, "the weapon stays sharp" );
	Check( DepthOfFieldCore_CircleOfConfusion( s, 8.0f, s.focusDistance, false ) == 0.0f
		&& DepthOfFieldCore_CircleOfConfusion( s, 120.0f, s.focusDistance, false ) == 0.0f,
		"the nearby world stays clear" );
	const float mid = DepthOfFieldCore_CircleOfConfusion( s, 300.0f, s.focusDistance, false );
	Check( mid > s.fixedScale * 0.5f && mid < s.fixedScale * 0.9f, "the blur grows past the clear band" );
	Check( DepthOfFieldCore_CircleOfConfusion( s, 4096.0f, s.focusDistance, false ) > s.fixedScale * 0.95f,
		"the distant world defocuses fully" );
	in.enabled = false;
	Check( DepthOfFieldCore_Resolve( in, s ), "the request needs no cheat" );
	in.requestAmount = 0.0f;
	Check( !DepthOfFieldCore_Resolve( in, s ), "no request and no r_dof draws nothing" );
	in.requestAmount = 1.0f;
	in.viewHeight = 2160;
	in.viewWidth = 3840;
	DepthOfFieldCore_Resolve( in, s );
	Check( Near( s.fixedScale, DOF_WEAPON_MAX_BLUR * 2.0f ), "the blur keeps its share of a taller view" );
}

static void TestScriptedLens() {
	depthOfFieldInputs_t in = Camera();
	in.enabled = false;
	in.requestAmount = 0.5f;
	in.requestFocusDistance = 200.0f;
	in.requestFStop = 2.8f;
	in.requestFocalLength = 50.0f;
	depthOfFieldSettings_t s;
	Check( DepthOfFieldCore_Resolve( in, s ), "a script's lens resolves without the cheat" );
	Check( !s.weaponFocus && !s.autoFocus && Near( s.focusDistance, 200.0f ), "a script focuses where it says" );
	depthOfFieldSettings_t full;
	in.requestAmount = 1.0f;
	DepthOfFieldCore_Resolve( in, full );
	Check( Near( DepthOfFieldCore_CocScale( s, 200.0f ), DepthOfFieldCore_CocScale( full, 200.0f ) * 0.5f, 0.01f ),
		"a script's amount scales the blur, so a blend fades it" );
	Check( Near( s.maxRadius, DOF_SCRIPTED_MAX_BLUR ) && s.samples == DepthOfFieldCore_SampleCount( DOF_SCRIPTED_QUALITY ),
		"a script gets the default blur limit and quality" );
	in.requestFocusDistance = 0.0f;
	in.requestFocalLength = 0.0f;
	in.requestMaxBlur = 40.0f;
	DepthOfFieldCore_Resolve( in, s );
	Check( s.autoFocus && Near( s.focalMm, 18.0f, 0.01f ) && Near( s.maxRadius, 40.0f ),
		"distance 0 autofocuses, focal length 0 follows the field of view" );
	in.enabled = true;
	DepthOfFieldCore_Resolve( in, s );
	Check( Near( s.focusDistance, 200.0f ) && Near( s.focalMm, 50.0f ), "the r_dof camera wins over a script" );
	in.requestFlags = DOF_REQUEST_FOCUS_VIEW_WEAPON;
	DepthOfFieldCore_Resolve( in, s );
	Check( s.weaponFocus, "the weapon wheel wins over both" );
}

// A focused in-world screen: a fast lens focused on the panel that leaves
// the view weapon sharp in front of it.
static void TestGuiFocusLens() {
	depthOfFieldInputs_t in = Camera();
	in.enabled = false;
	in.requestAmount = 1.0f;
	in.requestFlags = DOF_REQUEST_SHARP_VIEW_WEAPON;
	in.requestFocusDistance = 40.0f;
	in.requestFStop = 1.4f;
	in.requestFocalLength = 0.0f;
	in.requestFocusRange = 12.0f;
	in.requestMaxBlur = 10.0f;
	depthOfFieldSettings_t s;
	Check( DepthOfFieldCore_Resolve( in, s ), "a screen's lens resolves" );
	Check( s.weaponFocus && !s.autoFocus && Near( s.focusDistance, 40.0f ) && s.fixedScale == 0.0f,
		"the screen is the focus plane of a thin lens, with the weapon kept sharp" );
	Check( DepthOfFieldCore_CircleOfConfusion( s, 50.0f, 40.0f, false ) == 0.0f, "the panel's depth stays sharp" );
	Check( DepthOfFieldCore_CircleOfConfusion( s, 15.0f, 40.0f, true ) == 0.0f, "the view weapon stays sharp" );
	Check( DepthOfFieldCore_CircleOfConfusion( s, 15.0f, 40.0f, false ) < 0.0f, "the world in front softens" );
	Check( DepthOfFieldCore_CircleOfConfusion( s, 1024.0f, 40.0f, false ) > s.maxRadius * 0.5f, "the room behind softens" );
	in.requestFlags = 0;
	DepthOfFieldCore_Resolve( in, s );
	Check( !s.weaponFocus, "a plain script lens blurs the weapon with the rest" );
}

static void TestPlan() {
	depthOfFieldSettings_t s;
	depthOfFieldInputs_t in = Camera();
	in.viewWidth = 1919;
	in.viewHeight = 1079;
	DepthOfFieldCore_Resolve( in, s );
	Check( s.gatherWidth == 960 && s.gatherHeight == 540 && !s.fullResolution, "quality 2 gathers at half resolution" );
	Check( s.tileWidth == 120 && s.tileHeight == 68, "8x8 tiles round up" );
	Check( s.samples == 128, "quality 2 takes 128 samples" );
	Check( s.dilateTiles == 2, "a 32 px radius at half resolution reaches two tiles" );
	in.quality = 3;
	DepthOfFieldCore_Resolve( in, s );
	Check( s.fullResolution && s.gatherWidth == 1919 && Near( s.gatherScale, 1.0f ), "quality 3 gathers at full resolution" );
	Check( s.samples == DOF_MAX_SAMPLES && s.dilateTiles == 4, "with the most samples and the widest reach" );
	in.quality = 9;
	in.blades = 2;
	in.maxBlur = 0.0f;
	DepthOfFieldCore_Resolve( in, s );
	Check( s.samples == DOF_MAX_SAMPLES && s.blades == 0 && Near( s.maxRadius, 1.0f * 1079.0f / 1080.0f ),
		"out-of-range cvars clamp" );

	in = Camera();
	in.focusDistance = 0.0f;
	in.focusX = 0.25f;
	in.focusY = 0.1f;
	DepthOfFieldCore_Resolve( in, s );
	Check( s.autoFocus && Near( s.focusUV[0], 0.25f ) && Near( s.focusUV[1], 0.9f ), "autofocus samples the point, v up" );
	Check( DepthOfFieldCore_FocusBlend( 0.0f, 0.5f ) == 1.0f, "speed 0 focuses at once" );
	Check( Near( DepthOfFieldCore_FocusBlend( 4.0f, 0.1f ), 1.0f - std::exp( -0.4f ) ), "the pull eases exponentially" );
	Check( Near( DepthOfFieldCore_FocusBlend( 4.0f, 10.0f ), 1.0f - std::exp( -1.0f ) ), "a long stall pulls at most a quarter second" );

	depthOfFieldBlock_t block;
	const float depthProjection[2] = { -0.999f, -6.0f };
	const float one[2] = { 1.0f, 1.0f };
	DepthOfFieldCore_FillBlock( block, s, DOF_PASS_GATHER, 1920, 1080, depthProjection, one, one, 0.3f, true );
	Check( block.v[2][2] == static_cast<float>( DOF_PASS_GATHER ), "the block names the pass" );
	Check( block.v[3][0] == 0.0f && block.v[7][2] == 0.3f && block.v[7][3] == 1.0f, "autofocus reads the focus target" );
	Check( Near( block.v[0][0], 1.0f / 960.0f, 1.0e-7f ) && Near( block.v[0][2], 1.0f / 960.0f, 1.0e-7f ),
		"the gather reads and writes the gather resolution" );
	Check( Near( block.v[9][0], 960.0f / 960.0f ) && Near( block.v[9][1], 540.0f / 544.0f ),
		"the tile grid's overhang is scaled out" );
	DepthOfFieldCore_FillBlock( block, s, DOF_PASS_COMPOSITE, 1920, 1080, depthProjection, one, one, 1.0f, false );
	Check( Near( block.v[0][0], 1.0f / 1920.0f, 1.0e-7f ), "the composite writes the view" );
	Check( block.v[9][2] == static_cast<float>( s.samples ), "the r_dof camera sends its full count as the fewest" );
}

// The gather's samples: the r_dof camera keeps its full count on every
// blurred pixel; the game's own focus spaces its samples DOF_GATHER_SPACING
// apart over each pixel's disc.
static void TestGatherSamples() {
	depthOfFieldSettings_t s;
	depthOfFieldInputs_t in = Camera();
	DepthOfFieldCore_Resolve( in, s );
	Check( s.samples == 128 && s.minSamples == 128, "the camera takes its quality's samples everywhere" );
	Check( DepthOfFieldCore_GatherCount( s, 1.0f ) == 128, "even on a small disc" );

	// The weapon wheel at 1080p: a 7-texel disc already spaces 64 samples
	// further apart than DOF_GATHER_SPACING, so the largest blur keeps them all.
	in.requestAmount = 1.0f;
	in.requestFlags = DOF_REQUEST_FOCUS_VIEW_WEAPON;
	DepthOfFieldCore_Resolve( in, s );
	Check( s.samples == 64 && s.minSamples == DOF_MIN_GATHER_SAMPLES, "the wheel keeps 64 samples at its largest blur" );
	const float largest = s.maxRadius * s.gatherScale;
	Check( DepthOfFieldCore_GatherCount( s, largest ) == 64, "the largest disc takes them all" );
	Check( DepthOfFieldCore_GatherCount( s, largest * 0.75f ) == 36, "a smaller disc keeps their spacing" );
	Check( DepthOfFieldCore_GatherCount( s, 0.5f ) == DOF_MIN_GATHER_SAMPLES, "never fewer than the floor" );
	in.requestAmount = 0.5f;
	DepthOfFieldCore_Resolve( in, s );
	Check( s.samples == 18, "while the wheel fades in, its smaller blur takes fewer samples" );

	// A focused screen at 1080p: a 6-texel disc would put 128 samples 0.94
	// texels apart; spaced 1.5 apart it takes 51. At 4K the disc is 12 texels
	// and needs all 128.
	in = Camera();
	in.enabled = false;
	in.requestAmount = 1.0f;
	in.requestFlags = DOF_REQUEST_SHARP_VIEW_WEAPON;
	in.requestFocusDistance = 48.0f;
	in.requestFStop = 2.0f;
	in.requestFocalLength = 50.0f;
	in.requestMaxBlur = 12.0f;
	DepthOfFieldCore_Resolve( in, s );
	Check( s.samples == 51 && s.minSamples == DOF_MIN_GATHER_SAMPLES, "a screen's small blur takes the samples it needs" );
	in.viewWidth = 3840;
	in.viewHeight = 2160;
	DepthOfFieldCore_Resolve( in, s );
	Check( s.samples == DepthOfFieldCore_SampleCount( DOF_SCRIPTED_QUALITY ), "a 4K view's larger blur takes them all" );

	in.fixedSamples = true;
	in.viewWidth = 1920;
	in.viewHeight = 1080;
	DepthOfFieldCore_Resolve( in, s );
	Check( s.samples == DepthOfFieldCore_SampleCount( DOF_SCRIPTED_QUALITY ) && s.minSamples == s.samples,
		"r_dofFixedSamples gives every blurred pixel the full count" );
	Check( DepthOfFieldCore_GatherCount( s, 1.0f ) == s.samples, "whatever its size" );
}

int main() {
	TestLens();
	TestShaping();
	TestWeaponRequest();
	TestScriptedLens();
	TestGuiFocusLens();
	TestPlan();
	TestGatherSamples();
	if ( failures != 0 ) {
		std::fprintf( stderr, "DepthOfFieldCoreTest: %d failure(s)\n", failures );
		return 1;
	}
	std::printf( "DepthOfFieldCoreTest: all checks passed\n" );
	return 0;
}
