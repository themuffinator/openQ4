// Copyright (C) 2026 DarkMatter Productions
//
// Backend-neutral lens model and pass plan for the depth of field
// post-process. OpenGL (draw_common.cpp, glprogs/dof.fs) and Vulkan
// (vk_PostProcess.cpp, post_dof.frag) keep their own targets and GPU work;
// both take every decision and every uniform from these functions, so the
// two backends cannot drift apart.
//
// Two clients use it:
//   - r_dof (cheat cvars, no menu entry): a thin-lens camera for promotional
//     captures. Focal length follows the view's field of view on a 36 mm
//     sensor unless r_dofFocalLength sets one, so zooming in narrows the
//     depth of field the way a real lens does.
//   - the game (idRenderSystem::SetViewDepthOfField): the weapon wheel keeps
//     the view weapon sharp and defocuses the world behind it, a focused
//     in-world screen (a terminal or door panel under the cursor) is the
//     focus plane of a thin lens that leaves the view weapon sharp, and map
//     scripts (sys.setDepthOfField and friends) drive the same thin lens for
//     cinematics. The player's Depth of Field setting gates both.
// The weapon wheel wins over everything, then the r_dof camera, then a script.
//
// The circle of confusion is measured in view pixels and signed: negative in
// front of the focus plane, positive behind it.
//   coc(d) = K * (1 - S / d)
// with S the focus distance, d the pixel's distance, and for the lens
//   K = f^2 / (N * (S - f)) / sensorWidth * viewWidth
// (f focal length, N f-number, S and f in millimetres).

#ifndef __DEPTH_OF_FIELD_CORE_H__
#define __DEPTH_OF_FIELD_CORE_H__

#include <cmath>

// The passes, in order. The shader takes the pass number as a uniform.
enum depthOfFieldPass_t {
	DOF_PASS_FOCUS = 0,		// 1x1: the autofocus distance, eased toward the focus point's depth
	DOF_PASS_PREFILTER,		// gather resolution: colour + signed CoC
	DOF_PASS_TILE_MAX,		// 1/8 of that: the largest near and overall CoC per tile
	DOF_PASS_TILE_DILATE,	// the near CoC spread to the tiles it can reach
	DOF_PASS_GATHER,		// the bokeh gather
	DOF_PASS_POSTFILTER,	// a tent over the gather to settle its sampling
	DOF_PASS_COMPOSITE,		// full resolution, into the scene
	DOF_PASS_COUNT
};

static const int DOF_TILE_SIZE = 8;				// gather texels per tile side
static const int DOF_MAX_DILATE_TILES = 4;		// glprogs/dof.fs and post_dof.frag loop bound
static const int DOF_MAX_SAMPLES = 192;			// ditto
static const int DOF_BLOCK_VECTORS = 10;		// vec4s in the uniform block
static const float DOF_UNITS_TO_MM = 25.4f;		// one world unit is an inch
static const float DOF_SENSOR_WIDTH_MM = 36.0f;	// full-frame
static const float DOF_REFERENCE_HEIGHT = 1080.0f;	// r_dofMaxBlur is in pixels of this height
// The weapon wheel's focus: the view weapon and the world within
// DOF_WEAPON_FOCUS_DISTANCE + DOF_WEAPON_FOCUS_RANGE (128 units, about three
// metres) stay sharp; beyond, the blur grows gradually with distance.
static const float DOF_WEAPON_FOCUS_DISTANCE = 48.0f;
static const float DOF_WEAPON_FOCUS_RANGE = 80.0f;
static const float DOF_WEAPON_MAX_BLUR = 14.0f;			// pixels at the reference height
static const float DOF_SCRIPTED_MAX_BLUR = 24.0f;		// a script's default largest blur
static const int DOF_SCRIPTED_QUALITY = 2;
// The game's own focus (the weapon wheel, a focused screen, a map script)
// takes the samples its blur needs: over each pixel's disc they sit
// DOF_GATHER_SPACING gather texels apart, or as far apart as the quality's
// count puts them on the largest disc, whichever is wider, and number at
// least DOF_MIN_GATHER_SAMPLES. Closer samples only overlap the bilinear taps.
// The r_dof camera, a capture tool, gives every blurred pixel its full count,
// as does r_dofFixedSamples, the reference for comparisons.
static const float DOF_GATHER_SPACING = 1.5f;
static const int DOF_MIN_GATHER_SAMPLES = 16;

// The game's request (idRenderSystem::SetViewDepthOfField flags).
static const int DOF_REQUEST_FOCUS_VIEW_WEAPON = 1 << 0;
static const int DOF_REQUEST_SHARP_VIEW_WEAPON = 1 << 1;	// a lens request that leaves the view weapon sharp

// Plain values: cvars, the game's request and the view. Nothing here reads a
// cvar, so the unit test can drive every path.
struct depthOfFieldInputs_t {
	bool	enabled;				// r_dof
	float	requestAmount;			// the game's request, 0..1; 0 = none
	int		requestFlags;
	float	requestFocusDistance;	// a script's lens (renderViewDepthOfField_t); 0 = autofocus
	float	requestFStop;
	float	requestFocalLength;		// 0 = from the field of view
	float	requestFocusRange;
	float	requestMaxBlur;			// 0 = DOF_SCRIPTED_MAX_BLUR
	float	focusDistance;			// r_dofFocusDistance, world units; 0 = autofocus
	float	focusX;					// r_dofFocusX, 0 left .. 1 right
	float	focusY;					// r_dofFocusY, 0 top .. 1 bottom
	float	fStop;					// r_dofFStop
	float	focalLength;			// r_dofFocalLength, mm; 0 = from the field of view
	float	blurScale;				// r_dofBlurScale
	float	focusRange;				// r_dofFocusRange, world units kept fully sharp each side
	float	nearBlur;				// r_dofNearBlur
	float	farBlur;				// r_dofFarBlur
	float	maxBlur;				// r_dofMaxBlur, pixels at 1080 lines
	int		quality;				// r_dofQuality 0..3
	int		blades;					// r_dofBlades, 0 = round
	float	bladeRotation;			// r_dofBladeRotation, degrees
	float	bladeRoundness;			// r_dofBladeRoundness 0..1
	float	highlights;				// r_dofHighlights
	float	highlightThreshold;		// r_dofHighlightThreshold
	int		debug;					// r_dofDebug
	bool	fixedSamples;			// r_dofFixedSamples: the game's focus takes the full count everywhere
	float	fovX;					// the view's horizontal field of view, degrees
	int		viewWidth;
	int		viewHeight;
};

struct depthOfFieldSettings_t {
	bool	active;
	bool	weaponFocus;			// the weapon depth range stays sharp
	bool	autoFocus;				// the focus pass measures the focus point
	bool	fullResolution;			// gather at view resolution instead of half
	int		samples;				// the gather's samples on the largest disc
	int		minSamples;				// the fewest a blurred pixel takes; = samples: every one takes them all
	int		gatherWidth;
	int		gatherHeight;
	int		tileWidth;
	int		tileHeight;
	int		dilateTiles;
	float	gatherScale;			// gather texels per view pixel
	float	focusDistance;			// world units; only meaningful without autoFocus
	float	focusUV[2];				// texture space, v up
	float	lensScale;				// f^2 / (N * sensor) * viewWidth * blurScale, px*mm
	float	focalMm;
	float	fixedScale;				// > 0: K directly, in view pixels (the game's request)
	float	focusRange;
	float	nearScale;
	float	farScale;
	float	maxRadius;				// view pixels
	int		blades;
	float	bladeRotation;			// radians
	float	bladeRoundness;
	float	highlightBoost;
	float	highlightThreshold;
	int		debug;
};

struct depthOfFieldBlock_t {
	float	v[ DOF_BLOCK_VECTORS ][ 4 ];
};

inline float DepthOfFieldCore_Clamp( float value, float low, float high ) {
	if ( !( value == value ) ) {
		return low;
	}
	return value < low ? low : ( value > high ? high : value );
}

inline int DepthOfFieldCore_ClampInt( int value, int low, int high ) {
	return value < low ? low : ( value > high ? high : value );
}

// Gather samples per quality level; quality 3 also gathers at full resolution.
inline int DepthOfFieldCore_SampleCount( int quality ) {
	static const int counts[ 4 ] = { 32, 64, 128, DOF_MAX_SAMPLES };
	return counts[ DepthOfFieldCore_ClampInt( quality, 0, 3 ) ];
}

// The game's focus on its largest disc (radius in gather texels): samples
// DOF_GATHER_SPACING apart, at most the quality's count and at least
// DOF_MIN_GATHER_SAMPLES.
inline int DepthOfFieldCore_SpacedSamples( float gatherRadius, int qualitySamples ) {
	const float area = 3.14159265358979f * gatherRadius * gatherRadius;
	const float spaced = std::ceil( area / ( DOF_GATHER_SPACING * DOF_GATHER_SPACING ) );
	const int count = spaced < static_cast<float>( qualitySamples ) ? static_cast<int>( spaced ) : qualitySamples;
	return DepthOfFieldCore_ClampInt( count, DOF_MIN_GATHER_SAMPLES < qualitySamples ? DOF_MIN_GATHER_SAMPLES : qualitySamples,
		qualitySamples );
}

// The focal length that gives this horizontal field of view on the sensor.
inline float DepthOfFieldCore_FocalLengthForFov( float fovXDegrees ) {
	const float fov = DepthOfFieldCore_Clamp( fovXDegrees, 1.0f, 170.0f );
	return 0.5f * DOF_SENSOR_WIDTH_MM / std::tan( fov * 0.5f * 3.14159265358979f / 180.0f );
}

// K * (S - f): the part of the lens term that does not depend on the focus
// distance, so autofocus can finish it on the GPU.
inline float DepthOfFieldCore_LensScale( float focalMm, float fStop, int viewWidth, float blurScale ) {
	return focalMm * focalMm / ( DepthOfFieldCore_Clamp( fStop, 0.5f, 64.0f ) * DOF_SENSOR_WIDTH_MM )
		* static_cast<float>( viewWidth > 0 ? viewWidth : 1 ) * blurScale;
}

// K for a focus distance in world units (mirrors the shader).
inline float DepthOfFieldCore_CocScale( const depthOfFieldSettings_t &settings, float focusDistance ) {
	if ( settings.fixedScale > 0.0f ) {
		return settings.fixedScale;
	}
	const float denom = focusDistance * DOF_UNITS_TO_MM - settings.focalMm;
	return settings.lensScale / ( denom > 1.0f ? denom : 1.0f );
}

// The signed circle of confusion of a pixel at distance d, in view pixels
// (mirrors CircleOfConfusion in the shaders).
inline float DepthOfFieldCore_CircleOfConfusion( const depthOfFieldSettings_t &settings,
		float distance, float focusDistance, bool weaponPixel ) {
	if ( weaponPixel && settings.weaponFocus ) {
		return 0.0f;
	}
	const float delta = distance - focusDistance;
	const float outside = std::fabs( delta ) - settings.focusRange;
	if ( outside <= 0.0f ) {
		return 0.0f;
	}
	const float d = focusDistance + ( delta < 0.0f ? -outside : outside );
	float coc = DepthOfFieldCore_CocScale( settings, focusDistance )
		* ( 1.0f - focusDistance / ( d > 0.01f ? d : 0.01f ) );
	coc *= coc < 0.0f ? settings.nearScale : settings.farScale;
	return DepthOfFieldCore_Clamp( coc, -settings.maxRadius, settings.maxRadius );
}

// The samples a blurred pixel takes for a disc of this radius in gather
// texels (mirrors GatherCount in the shaders): the largest disc's count by
// this disc's share of its area, so the spacing holds, within
// minSamples..samples.
inline int DepthOfFieldCore_GatherCount( const depthOfFieldSettings_t &settings, float radius ) {
	const float largest = settings.maxRadius * settings.gatherScale;
	const float share = radius / ( largest > 0.5f ? largest : 0.5f );
	const float count = std::ceil( static_cast<float>( settings.samples ) * share * share );
	if ( !( count < static_cast<float>( settings.samples ) ) ) {
		return settings.samples;
	}
	return count > static_cast<float>( settings.minSamples ) ? static_cast<int>( count ) : settings.minSamples;
}

// How far this frame's autofocus moves toward the measured distance.
inline float DepthOfFieldCore_FocusBlend( float speed, float elapsedSeconds ) {
	if ( speed <= 0.0f ) {
		return 1.0f;
	}
	const float dt = DepthOfFieldCore_Clamp( elapsedSeconds, 0.0f, 0.25f );
	return 1.0f - std::exp( -speed * dt );
}

// Resolves the inputs into this view's settings. Returns settings.active.
// The game's request, when present, replaces the r_dof camera for the frame.
inline bool DepthOfFieldCore_Resolve( const depthOfFieldInputs_t &in, depthOfFieldSettings_t &out ) {
	out = depthOfFieldSettings_t();
	if ( in.viewWidth <= 0 || in.viewHeight <= 0 ) {
		return false;
	}
	const float heightScale = static_cast<float>( in.viewHeight ) / DOF_REFERENCE_HEIGHT;
	const float request = DepthOfFieldCore_Clamp( in.requestAmount, 0.0f, 1.0f );
	const bool weaponRequest = request > 0.001f && ( in.requestFlags & DOF_REQUEST_FOCUS_VIEW_WEAPON ) != 0;
	bool spacedSamples = false;		// the game's focus; the r_dof camera keeps its full count
	if ( weaponRequest ) {
		spacedSamples = !in.fixedSamples;
		out.weaponFocus = true;
		out.autoFocus = false;
		out.focusDistance = DOF_WEAPON_FOCUS_DISTANCE;
		out.focusRange = DOF_WEAPON_FOCUS_RANGE;
		out.fixedScale = DOF_WEAPON_MAX_BLUR * heightScale * request;
		out.maxRadius = out.fixedScale;
		out.nearScale = 1.0f;
		out.farScale = 1.0f;
		out.samples = DepthOfFieldCore_SampleCount( 1 );
		out.highlightBoost = 0.5f;
		out.highlightThreshold = 0.75f;
		out.bladeRoundness = 1.0f;
	} else if ( in.enabled ) {
		out.autoFocus = in.focusDistance <= 0.0f;
		out.focusDistance = out.autoFocus ? 0.0f : DepthOfFieldCore_Clamp( in.focusDistance, 1.0f, 65536.0f );
		out.focalMm = in.focalLength > 0.0f ? DepthOfFieldCore_Clamp( in.focalLength, 4.0f, 1200.0f )
			: DepthOfFieldCore_FocalLengthForFov( in.fovX );
		out.lensScale = DepthOfFieldCore_LensScale( out.focalMm, in.fStop, in.viewWidth,
			DepthOfFieldCore_Clamp( in.blurScale, 0.0f, 16.0f ) );
		out.focusRange = DepthOfFieldCore_Clamp( in.focusRange, 0.0f, 65536.0f );
		out.nearScale = DepthOfFieldCore_Clamp( in.nearBlur, 0.0f, 4.0f );
		out.farScale = DepthOfFieldCore_Clamp( in.farBlur, 0.0f, 4.0f );
		out.maxRadius = DepthOfFieldCore_Clamp( in.maxBlur, 1.0f, 64.0f ) * heightScale;
		out.samples = DepthOfFieldCore_SampleCount( in.quality );
		out.fullResolution = in.quality >= 3;
		out.blades = in.blades >= 3 ? DepthOfFieldCore_ClampInt( in.blades, 3, 12 ) : 0;
		out.bladeRotation = DepthOfFieldCore_Clamp( in.bladeRotation, -360.0f, 360.0f ) * 3.14159265358979f / 180.0f;
		out.bladeRoundness = DepthOfFieldCore_Clamp( in.bladeRoundness, 0.0f, 1.0f );
		out.highlightBoost = DepthOfFieldCore_Clamp( in.highlights, 0.0f, 8.0f );
		out.highlightThreshold = DepthOfFieldCore_Clamp( in.highlightThreshold, 0.0f, 16.0f );
		out.debug = DepthOfFieldCore_ClampInt( in.debug, 0, 1 );
		if ( out.lensScale <= 0.0f || ( out.nearScale <= 0.0f && out.farScale <= 0.0f ) ) {
			if ( out.debug == 0 ) {
				return false;
			}
		}
	} else if ( request > 0.001f ) {
		// A script's lens: the r_dof camera's model with the script's values,
		// its blur scaled by the request so a blend fades it in and out.
		spacedSamples = !in.fixedSamples;
		out.autoFocus = in.requestFocusDistance <= 0.0f;
		out.focusDistance = out.autoFocus ? 0.0f : DepthOfFieldCore_Clamp( in.requestFocusDistance, 1.0f, 65536.0f );
		out.focalMm = in.requestFocalLength > 0.0f ? DepthOfFieldCore_Clamp( in.requestFocalLength, 4.0f, 1200.0f )
			: DepthOfFieldCore_FocalLengthForFov( in.fovX );
		out.lensScale = DepthOfFieldCore_LensScale( out.focalMm, in.requestFStop, in.viewWidth, request );
		out.focusRange = DepthOfFieldCore_Clamp( in.requestFocusRange, 0.0f, 65536.0f );
		out.nearScale = 1.0f;
		out.farScale = 1.0f;
		out.maxRadius = DepthOfFieldCore_Clamp( in.requestMaxBlur > 0.0f ? in.requestMaxBlur : DOF_SCRIPTED_MAX_BLUR,
			1.0f, 64.0f ) * heightScale;
		out.samples = DepthOfFieldCore_SampleCount( DOF_SCRIPTED_QUALITY );
		out.bladeRoundness = 1.0f;
		out.highlightBoost = 1.0f;
		out.highlightThreshold = 0.7f;
		// A focused in-world screen: the weapon in front of it is part of the
		// player, not the scene, so it keeps its detail.
		out.weaponFocus = ( in.requestFlags & DOF_REQUEST_SHARP_VIEW_WEAPON ) != 0;
	} else {
		return false;
	}
	out.focusUV[0] = DepthOfFieldCore_Clamp( in.focusX, 0.0f, 1.0f );
	out.focusUV[1] = 1.0f - DepthOfFieldCore_Clamp( in.focusY, 0.0f, 1.0f );
	if ( out.maxRadius < 0.5f ) {
		return false;
	}
	out.gatherWidth = out.fullResolution ? in.viewWidth : ( in.viewWidth + 1 ) / 2;
	out.gatherHeight = out.fullResolution ? in.viewHeight : ( in.viewHeight + 1 ) / 2;
	out.gatherScale = static_cast<float>( out.gatherWidth ) / static_cast<float>( in.viewWidth );
	if ( spacedSamples ) {
		out.samples = DepthOfFieldCore_SpacedSamples( out.maxRadius * out.gatherScale, out.samples );
	}
	out.minSamples = spacedSamples && DOF_MIN_GATHER_SAMPLES < out.samples ? DOF_MIN_GATHER_SAMPLES : out.samples;
	out.tileWidth = ( out.gatherWidth + DOF_TILE_SIZE - 1 ) / DOF_TILE_SIZE;
	out.tileHeight = ( out.gatherHeight + DOF_TILE_SIZE - 1 ) / DOF_TILE_SIZE;
	const float reach = out.maxRadius * out.gatherScale / static_cast<float>( DOF_TILE_SIZE );
	out.dilateTiles = DepthOfFieldCore_ClampInt( static_cast<int>( std::ceil( reach ) ), 1, DOF_MAX_DILATE_TILES );
	out.active = true;
	return true;
}

// The pass's output and primary input sizes, in texels.
inline void DepthOfFieldCore_PassSizes( const depthOfFieldSettings_t &s, int viewWidth, int viewHeight,
		depthOfFieldPass_t pass, int &outWidth, int &outHeight, int &sourceWidth, int &sourceHeight ) {
	switch ( pass ) {
	case DOF_PASS_FOCUS:
		outWidth = outHeight = 1;
		sourceWidth = viewWidth; sourceHeight = viewHeight;
		break;
	case DOF_PASS_PREFILTER:
		outWidth = s.gatherWidth; outHeight = s.gatherHeight;
		sourceWidth = viewWidth; sourceHeight = viewHeight;
		break;
	case DOF_PASS_TILE_MAX:
		outWidth = s.tileWidth; outHeight = s.tileHeight;
		sourceWidth = s.gatherWidth; sourceHeight = s.gatherHeight;
		break;
	case DOF_PASS_TILE_DILATE:
		outWidth = s.tileWidth; outHeight = s.tileHeight;
		sourceWidth = s.tileWidth; sourceHeight = s.tileHeight;
		break;
	case DOF_PASS_GATHER:
	case DOF_PASS_POSTFILTER:
		outWidth = s.gatherWidth; outHeight = s.gatherHeight;
		sourceWidth = s.gatherWidth; sourceHeight = s.gatherHeight;
		break;
	case DOF_PASS_COMPOSITE:
	default:
		outWidth = viewWidth; outHeight = viewHeight;
		sourceWidth = s.gatherWidth; sourceHeight = s.gatherHeight;
		break;
	}
}

// Packs the uniform block for one pass. Layout (glprogs/dof.fs dofParams[],
// post_dof.frag DofBlock):
//   0 xy 1/output size, zw 1/primary input size
//   1 xy scene UV scale, zw depth UV scale (OpenGL captures may be larger than the view)
//   2 x P[10], y P[14], z pass, w debug
//   3 x manual focus distance (0: read the focus target), y lens scale, z focal mm, w fixed K
//   4 x focus range, y near scale, z far scale, w max radius
//   5 x samples, y blades, z blade rotation, w blade roundness
//   6 x highlight boost, y highlight threshold, z weapon focus, w gather scale
//   7 xy focus point, z focus blend, w focus history valid
//   8 x tile size, y dilate tiles, zw 1/view size
//   9 xy tile UV scale (the tile grid overhangs the gather target), z the fewest gather samples
inline void DepthOfFieldCore_FillBlock( depthOfFieldBlock_t &block, const depthOfFieldSettings_t &s,
		depthOfFieldPass_t pass, int viewWidth, int viewHeight, const float depthProjection[2],
		const float sceneUVScale[2], const float depthUVScale[2], float focusBlend, bool focusHistoryValid ) {
	for ( int i = 0; i < DOF_BLOCK_VECTORS; i++ ) {
		block.v[i][0] = block.v[i][1] = block.v[i][2] = block.v[i][3] = 0.0f;
	}
	int outWidth = 1, outHeight = 1, sourceWidth = 1, sourceHeight = 1;
	DepthOfFieldCore_PassSizes( s, viewWidth, viewHeight, pass, outWidth, outHeight, sourceWidth, sourceHeight );
	block.v[0][0] = 1.0f / static_cast<float>( outWidth > 0 ? outWidth : 1 );
	block.v[0][1] = 1.0f / static_cast<float>( outHeight > 0 ? outHeight : 1 );
	block.v[0][2] = 1.0f / static_cast<float>( sourceWidth > 0 ? sourceWidth : 1 );
	block.v[0][3] = 1.0f / static_cast<float>( sourceHeight > 0 ? sourceHeight : 1 );
	block.v[1][0] = sceneUVScale[0];
	block.v[1][1] = sceneUVScale[1];
	block.v[1][2] = depthUVScale[0];
	block.v[1][3] = depthUVScale[1];
	block.v[2][0] = depthProjection[0];
	block.v[2][1] = depthProjection[1];
	block.v[2][2] = static_cast<float>( pass );
	block.v[2][3] = static_cast<float>( s.debug );
	block.v[3][0] = s.autoFocus ? 0.0f : s.focusDistance;
	block.v[3][1] = s.lensScale;
	block.v[3][2] = s.focalMm;
	block.v[3][3] = s.fixedScale;
	block.v[4][0] = s.focusRange;
	block.v[4][1] = s.nearScale;
	block.v[4][2] = s.farScale;
	block.v[4][3] = s.maxRadius;
	block.v[5][0] = static_cast<float>( s.samples );
	block.v[5][1] = static_cast<float>( s.blades );
	block.v[5][2] = s.bladeRotation;
	block.v[5][3] = s.bladeRoundness;
	block.v[6][0] = s.highlightBoost;
	block.v[6][1] = s.highlightThreshold;
	block.v[6][2] = s.weaponFocus ? 1.0f : 0.0f;
	block.v[6][3] = s.gatherScale;
	block.v[7][0] = s.focusUV[0];
	block.v[7][1] = s.focusUV[1];
	block.v[7][2] = focusBlend;
	block.v[7][3] = focusHistoryValid ? 1.0f : 0.0f;
	block.v[8][0] = static_cast<float>( DOF_TILE_SIZE );
	block.v[8][1] = static_cast<float>( s.dilateTiles );
	block.v[8][2] = 1.0f / static_cast<float>( viewWidth > 0 ? viewWidth : 1 );
	block.v[8][3] = 1.0f / static_cast<float>( viewHeight > 0 ? viewHeight : 1 );
	const float tileCoverWidth = static_cast<float>( ( s.tileWidth > 0 ? s.tileWidth : 1 ) * DOF_TILE_SIZE );
	const float tileCoverHeight = static_cast<float>( ( s.tileHeight > 0 ? s.tileHeight : 1 ) * DOF_TILE_SIZE );
	block.v[9][0] = static_cast<float>( s.gatherWidth ) / tileCoverWidth;
	block.v[9][1] = static_cast<float>( s.gatherHeight ) / tileCoverHeight;
	block.v[9][2] = static_cast<float>( s.minSamples );
}

#endif // __DEPTH_OF_FIELD_CORE_H__
