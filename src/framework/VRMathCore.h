// Copyright (C) 2026 DarkMatter Productions
#ifndef __VR_MATH_CORE_H__
#define __VR_MATH_CORE_H__

#include <cmath>

/*
===============================================================================

	Dependency-light head-mounted display math shared by the OpenXR layer, the
	renderer and the game modules.

	OpenXR reports poses in a right-handed space with +X right, +Y up and -Z
	forward, in metres. The engine works in a right-handed space with +X
	forward, +Y left and +Z up, in game units. The two bases are related by a
	proper rotation, so a direction converts by permuting its components, an
	orientation by permuting its quaternion's vector part, and a position by
	the permutation plus the world scale.

	Engine angles follow idAngles: yaw turns counter-clockwise seen from above
	(towards +Y, the player's left), and a positive pitch looks down.

===============================================================================
*/

struct vrVec3_t {
	float x, y, z;
};

struct vrQuat_t {
	float x, y, z, w;
};

// A frustum as the four half-angle tangents measured from the view axis. A
// frustum containing its own axis has negative left and down tangents, which
// is what an OpenXR XrFovf converts to; the eyes of most headsets are not
// symmetric about their axis.
struct vrFovTangents_t {
	float left, right, up, down;
};

const float VR_PI = 3.14159265358979323846f;
const float VR_DEG2RAD = VR_PI / 180.0f;
const float VR_RAD2DEG = 180.0f / VR_PI;

inline vrVec3_t VR_Vec3( float x, float y, float z ) {
	vrVec3_t v = { x, y, z };
	return v;
}

inline vrVec3_t VR_Add( const vrVec3_t &a, const vrVec3_t &b ) {
	return VR_Vec3( a.x + b.x, a.y + b.y, a.z + b.z );
}

inline vrVec3_t VR_Sub( const vrVec3_t &a, const vrVec3_t &b ) {
	return VR_Vec3( a.x - b.x, a.y - b.y, a.z - b.z );
}

inline vrVec3_t VR_Scale( const vrVec3_t &v, float s ) {
	return VR_Vec3( v.x * s, v.y * s, v.z * s );
}

inline float VR_Dot( const vrVec3_t &a, const vrVec3_t &b ) {
	return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline vrVec3_t VR_Cross( const vrVec3_t &a, const vrVec3_t &b ) {
	return VR_Vec3( a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x );
}

inline float VR_Length( const vrVec3_t &v ) {
	return std::sqrt( VR_Dot( v, v ) );
}

inline bool VR_IsFinite( const vrVec3_t &v ) {
	return std::isfinite( v.x ) && std::isfinite( v.y ) && std::isfinite( v.z );
}

inline bool VR_IsFinite( const vrQuat_t &q ) {
	return std::isfinite( q.x ) && std::isfinite( q.y ) && std::isfinite( q.z ) && std::isfinite( q.w );
}

inline vrQuat_t VR_QuatIdentity( void ) {
	vrQuat_t q = { 0.0f, 0.0f, 0.0f, 1.0f };
	return q;
}

// Returns identity for a zero or non-finite quaternion, so a runtime that
// reports garbage for a lost device never poisons the view.
inline vrQuat_t VR_QuatNormalize( const vrQuat_t &q ) {
	const float lengthSq = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
	if ( !std::isfinite( lengthSq ) || lengthSq < 1e-12f ) {
		return VR_QuatIdentity();
	}
	const float inv = 1.0f / std::sqrt( lengthSq );
	vrQuat_t n = { q.x * inv, q.y * inv, q.z * inv, q.w * inv };
	return n;
}

// a * b applies b first, then a
inline vrQuat_t VR_QuatMultiply( const vrQuat_t &a, const vrQuat_t &b ) {
	vrQuat_t r;
	r.x = a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y;
	r.y = a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x;
	r.z = a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w;
	r.w = a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z;
	return r;
}

inline vrQuat_t VR_QuatConjugate( const vrQuat_t &q ) {
	vrQuat_t c = { -q.x, -q.y, -q.z, q.w };
	return c;
}

inline vrVec3_t VR_RotateVector( const vrQuat_t &q, const vrVec3_t &v ) {
	const vrVec3_t u = VR_Vec3( q.x, q.y, q.z );
	const vrVec3_t t = VR_Scale( VR_Cross( u, v ), 2.0f );
	return VR_Add( VR_Add( v, VR_Scale( t, q.w ) ), VR_Cross( u, t ) );
}

// rotation about the engine's +Z (up) axis; positive yaw turns left
inline vrQuat_t VR_YawQuat( float yawDegrees ) {
	const float half = yawDegrees * VR_DEG2RAD * 0.5f;
	vrQuat_t q = { 0.0f, 0.0f, std::sin( half ), std::cos( half ) };
	return q;
}

/*
====================
OpenXR to engine conversion

The engine basis expressed in OpenXR axes: forward = -Z, left = -X, up = +Y.
====================
*/
inline vrVec3_t VR_XrToEngineDirection( const vrVec3_t &xr ) {
	return VR_Vec3( -xr.z, -xr.x, xr.y );
}

inline vrVec3_t VR_EngineToXrDirection( const vrVec3_t &engine ) {
	return VR_Vec3( -engine.y, engine.z, -engine.x );
}

inline vrVec3_t VR_XrToEnginePosition( const vrVec3_t &xrMetres, float unitsPerMetre ) {
	return VR_Scale( VR_XrToEngineDirection( xrMetres ), unitsPerMetre );
}

// The same rotation about the converted axis, so only the vector part moves.
inline vrQuat_t VR_XrToEngineOrientation( const vrQuat_t &xr ) {
	const vrQuat_t n = VR_QuatNormalize( xr );
	vrQuat_t q = { -n.z, -n.x, n.y, n.w };
	return q;
}

inline vrQuat_t VR_EngineToXrOrientation( const vrQuat_t &engine ) {
	const vrQuat_t n = VR_QuatNormalize( engine );
	vrQuat_t q = { -n.y, n.z, -n.x, n.w };
	return q;
}

// Rows of an idMat3 view axis: forward, left, up.
inline void VR_QuatToAxis( const vrQuat_t &q, vrVec3_t axis[3] ) {
	axis[0] = VR_RotateVector( q, VR_Vec3( 1.0f, 0.0f, 0.0f ) );
	axis[1] = VR_RotateVector( q, VR_Vec3( 0.0f, 1.0f, 0.0f ) );
	axis[2] = VR_RotateVector( q, VR_Vec3( 0.0f, 0.0f, 1.0f ) );
}

inline float VR_YawDegrees( const vrVec3_t &forward ) {
	if ( forward.x == 0.0f && forward.y == 0.0f ) {
		return 0.0f;
	}
	return std::atan2( forward.y, forward.x ) * VR_RAD2DEG;
}

// Engine convention: positive pitch looks down.
inline float VR_PitchDegrees( const vrVec3_t &forward ) {
	const float horizontal = std::sqrt( forward.x * forward.x + forward.y * forward.y );
	return -std::atan2( forward.z, horizontal ) * VR_RAD2DEG;
}

// Yaw of the direction an orientation faces, in engine degrees.
inline float VR_QuatYawDegrees( const vrQuat_t &engineOrientation ) {
	return VR_YawDegrees( VR_RotateVector( engineOrientation, VR_Vec3( 1.0f, 0.0f, 0.0f ) ) );
}

// Wraps an angle into [-180, 180).
inline float VR_NormalizeDegrees180( float degrees ) {
	if ( !std::isfinite( degrees ) ) {
		return 0.0f;
	}
	degrees = std::fmod( degrees + 180.0f, 360.0f );
	if ( degrees < 0.0f ) {
		degrees += 360.0f;
	}
	return degrees - 180.0f;
}

/*
====================
Projection

OpenXR XrFovf angles are radians from the view axis, negative to the left
and downwards. The renderer builds its projection from near-plane extents;
the horizontal extent maps to clip-space +X (screen right), which is the
engine's -Y, and the vertical extent to clip-space +Y (screen up).
====================
*/
inline vrFovTangents_t VR_FovTangentsFromAngles( float angleLeft, float angleRight, float angleUp, float angleDown ) {
	vrFovTangents_t fov;
	fov.left = std::tan( angleLeft );
	fov.right = std::tan( angleRight );
	fov.up = std::tan( angleUp );
	fov.down = std::tan( angleDown );
	return fov;
}

// Tangents beyond about 88 degrees off axis would make the projection
// degenerate; a runtime never reports them for a real display.
inline bool VR_FovTangentsValid( const vrFovTangents_t &fov ) {
	const float limit = 30.0f;
	if ( !std::isfinite( fov.left ) || !std::isfinite( fov.right ) || !std::isfinite( fov.up ) || !std::isfinite( fov.down ) ) {
		return false;
	}
	if ( std::fabs( fov.left ) > limit || std::fabs( fov.right ) > limit || std::fabs( fov.up ) > limit || std::fabs( fov.down ) > limit ) {
		return false;
	}
	return fov.left < fov.right && fov.down < fov.up;
}

// The symmetric field of view, in degrees, that encloses an off-axis one.
// Consumers that only estimate (LOD, effects, the light grid) keep working
// from fov_x/fov_y while culling and projection use the exact tangents.
inline void VR_EnclosingFovDegrees( const vrFovTangents_t &fov, float &fovX, float &fovY ) {
	const float halfX = std::fabs( fov.left ) > std::fabs( fov.right ) ? std::fabs( fov.left ) : std::fabs( fov.right );
	const float halfY = std::fabs( fov.down ) > std::fabs( fov.up ) ? std::fabs( fov.down ) : std::fabs( fov.up );
	fovX = 2.0f * std::atan( halfX ) * VR_RAD2DEG;
	fovY = 2.0f * std::atan( halfY ) * VR_RAD2DEG;
}

inline void VR_ProjectionExtents( const vrFovTangents_t &fov, float zNear, float &xmin, float &xmax, float &ymin, float &ymax ) {
	xmin = zNear * fov.left;
	xmax = zNear * fov.right;
	ymin = zNear * fov.down;
	ymax = zNear * fov.up;
}

/*
====================
Eye-image projection

A point seen from an eye, given in the eye's own engine axes (x forward,
y left, z up), lies along tangents x right and y up of the view axis; the
eye's signed fov tangents then place it on that eye's image, with u running
left to right and v top to bottom over 0..1 (the 640x480 GUI convention).
Each eye projecting the same world point gives the stereo pair that fuses
at the point's depth. Points at or behind the eye plane do not project.
====================
*/
struct vrEyeTangent_t {
	bool	inFront;
	float	x, y;		// tangents right and up of the view axis
	float	depth;		// distance along the view axis
};

inline vrEyeTangent_t VR_EyeTangentOf( const vrVec3_t &eyeLocal ) {
	vrEyeTangent_t result = { false, 0.0f, 0.0f, 0.0f };
	const float minimumDepth = 1e-3f;
	if ( !VR_IsFinite( eyeLocal ) || eyeLocal.x < minimumDepth ) {
		return result;
	}
	result.inFront = true;
	result.x = -eyeLocal.y / eyeLocal.x;
	result.y = eyeLocal.z / eyeLocal.x;
	result.depth = eyeLocal.x;
	return result;
}

inline void VR_TangentToImage( const vrFovTangents_t &fov, float tangentX, float tangentY, float &u, float &v ) {
	u = ( tangentX - fov.left ) / ( fov.right - fov.left );
	v = ( fov.up - tangentY ) / ( fov.up - fov.down );
}

/*
====================
Rotation between directions

The shortest rotation that turns unit vector a onto unit vector b, about
a x b, as the rows of a matrix in the engine's row-vector convention, so
a * m == b. Nearly parallel vectors give the identity; opposite ones have no
single shortest turn and also give the identity, which callers reject.
====================
*/
inline void VR_RotationBetween( const vrVec3_t &a, const vrVec3_t &b, vrVec3_t rows[3] ) {
	const vrVec3_t k = VR_Cross( a, b );
	const float sineSquared = VR_Dot( k, k );
	const float cosine = VR_Dot( a, b );
	float m[3][3] = { { 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } };
	if ( sineSquared > 1e-12f && cosine > -0.9999f ) {
		// column form R = I + [k]x + [k]x^2 (1 - cos) / sin^2; the rows are its transpose
		const float f = ( 1.0f - cosine ) / sineSquared;
		const float kv[3] = { k.x, k.y, k.z };
		const float cross[3][3] = { { 0.0f, -k.z, k.y }, { k.z, 0.0f, -k.x }, { -k.y, k.x, 0.0f } };
		for ( int i = 0; i < 3; i++ ) {
			for ( int j = 0; j < 3; j++ ) {
				const float squared = kv[i] * kv[j] - ( i == j ? sineSquared : 0.0f );
				m[j][i] = ( i == j ? 1.0f : 0.0f ) + cross[i][j] + f * squared;
			}
		}
	}
	for ( int i = 0; i < 3; i++ ) {
		rows[i] = VR_Vec3( m[i][0], m[i][1], m[i][2] );
	}
}

// v * rows, in the engine's row-vector convention
inline vrVec3_t VR_MultiplyRows( const vrVec3_t &v, const vrVec3_t rows[3] ) {
	return VR_Add( VR_Add( VR_Scale( rows[0], v.x ), VR_Scale( rows[1], v.y ) ), VR_Scale( rows[2], v.z ) );
}

/*
====================
Comfort vignette

How much artificial motion should narrow the view, 0..1: from the player's
speed in units per second (Quake 4 walks at 80 and runs at 160, so a run is
full strength and a slow drift nothing) and from a smooth turn's rate in
degrees per second (120, the default smooth-turn speed, is full strength).
====================
*/
inline float VR_ComfortMotion( float speed, float turnDegreesPerSecond ) {
	const float move = ( speed - 15.0f ) / 145.0f;
	const float turn = ( std::fabs( turnDegreesPerSecond ) - 10.0f ) / 110.0f;
	const float strongest = move > turn ? move : turn;
	return strongest < 0.0f ? 0.0f : ( strongest > 1.0f ? 1.0f : strongest );
}

/*
====================
Zoom

A headset's field of view is fixed, so a weapon's zoom magnifies by narrowing
each eye's frustum instead: its tangents scale by tan(zoomed / 2) over
tan(normal / 2), both horizontal fields of view in degrees. 1 leaves the eye
alone; the scale stops at a third, 3x magnification.
====================
*/
inline float VR_ZoomTangentScale( float zoomedFovDegrees, float normalFovDegrees ) {
	if ( !( zoomedFovDegrees > 0.0f ) || !( normalFovDegrees > 0.0f ) || zoomedFovDegrees >= normalFovDegrees ) {
		return 1.0f;
	}
	const float scale = std::tan( zoomedFovDegrees * 0.5f * VR_DEG2RAD ) / std::tan( normalFovDegrees * 0.5f * VR_DEG2RAD );
	return scale < ( 1.0f / 3.0f ) ? ( 1.0f / 3.0f ) : scale;
}

// Eases the vignette towards its target: in within about a tenth of a second,
// so it is there as motion starts, and out over about a third, so a stop does
// not flash the edges open.
inline float VR_ComfortEase( float current, float target, float seconds ) {
	if ( seconds <= 0.0f ) {
		return current;
	}
	const float settle = target > current ? 0.08f : 0.35f;
	return current + ( target - current ) * ( 1.0f - std::exp( -seconds / settle ) );
}

/*
====================
Room-scale walking through the usercmd

A multiplayer server moves bodies only from usercmds, so there the body
walks after the head the way a stick would move it: towards the head's
horizontal offset from the body (tracking space, units), at up to half of
full input 12 units out. A body moved by usercmds coasts after its input
stops, so the walk starts only beyond 6 units and runs until the head is
within 2: the coast then ends inside the free zone instead of setting off a
walk back. walking carries that state between calls. forward and right are
in the aim frame the usercmd moves along, given the aim's yaw in tracking
space.
====================
*/
inline bool VR_RoomScaleWalkInput( float headX, float headY, float aimYawDegrees, bool &walking, float &forward, float &right ) {
	forward = right = 0.0f;
	const float startRadius = 6.0f;
	const float stopRadius = 2.0f;
	const float ramp = 12.0f;
	const float strongest = 0.5f;
	const float distance = std::sqrt( headX * headX + headY * headY );
	if ( !( distance == distance ) ) {
		walking = false;
		return false;
	}
	if ( walking ? distance < stopRadius : !( distance > startRadius ) ) {
		walking = false;
		return false;
	}
	walking = true;
	float strength = ( distance - stopRadius ) / ramp;
	strength = ( strength > 1.0f ? 1.0f : strength ) * strongest;
	const float dx = headX / distance;
	const float dy = headY / distance;
	const float c = std::cos( aimYawDegrees * VR_DEG2RAD );
	const float s = std::sin( aimYawDegrees * VR_DEG2RAD );
	// the engine's left is +y; the usercmd moves right
	forward = strength * ( dx * c + dy * s );
	right = -strength * ( -dx * s + dy * c );
	return true;
}

/*
====================
Physical crouch

The tracked head lowered well below where it was recentred crouches the
body: from 0.4 m down, and it stands again above 0.3 m, so a head hovering
near the line does not flicker between the two. headDropMetres is positive
downwards.
====================
*/
inline bool VR_PhysicalCrouch( float headDropMetres, bool crouching ) {
	return crouching ? headDropMetres > 0.3f : headDropMetres > 0.4f;
}

/*
====================
Play space (vr_playSpace)

Seated play measures the head from where it was recentred: the head's
height at the recentre becomes the game's standing eye. Standing play
measures it from the floor of a floor-level reference space (STAGE, or
XR_EXT_local_floor's LOCAL_FLOOR) against the player's standing eye height,
so recentring turns and moves the origin but never moves the floor: a
recentre while crouched, sitting or leaning keeps the standing height. A
runtime with neither floor-level space leaves standing play head-relative,
as seated play is. Auto stands when a floor is known and sits otherwise.
====================
*/
enum vrPlaySpaceMode_t {
	VR_PLAY_SPACE_AUTO = 0,
	VR_PLAY_SPACE_SEATED = 1,
	VR_PLAY_SPACE_STANDING = 2
};

enum vrBaseSpace_t {
	VR_BASE_SPACE_LOCAL = 0,		// head-height origin, no floor
	VR_BASE_SPACE_LOCAL_FLOOR,		// XR_EXT_local_floor: LOCAL with its origin on the floor
	VR_BASE_SPACE_STAGE				// the room's floor-level play area
};

typedef struct vrPlaySpaceChoice_s {
	vrBaseSpace_t	space;
	bool			standing;		// heights measured from the floor
	bool			fallback;		// standing was asked for and no floor is known
} vrPlaySpaceChoice_t;

inline vrPlaySpaceChoice_t VR_ChoosePlaySpace( int mode, bool haveStage, bool haveLocalFloor ) {
	vrPlaySpaceChoice_t choice;
	choice.space = VR_BASE_SPACE_LOCAL;
	choice.standing = false;
	choice.fallback = false;
	if ( mode == VR_PLAY_SPACE_SEATED ) {
		return choice;
	}
	if ( haveStage ) {
		choice.space = VR_BASE_SPACE_STAGE;
	} else if ( haveLocalFloor ) {
		choice.space = VR_BASE_SPACE_LOCAL_FLOOR;
	} else {
		choice.fallback = mode == VR_PLAY_SPACE_STANDING;
		return choice;
	}
	choice.standing = true;
	return choice;
}

// The plausible range of a standing eye height, in metres.
static const float VR_STANDING_HEIGHT_MIN = 0.8f;
static const float VR_STANDING_HEIGHT_MAX = 2.3f;

/*
====================
VR_StandingReference

The height, in the base space's metres, that the game's standing eye stands
for. Head-relative play (no floor) takes the head's height at the recentre.
Standing play takes the calibrated standing eye height (vr_standingHeight);
an uncalibrated one (outside the plausible range, 0 by default) is learned
from the head at this recentre, clamped to the plausible range, and *learned
reports that the caller should store it.
====================
*/
inline float VR_StandingReference( bool standing, float headHeight, float calibrated, bool &learned ) {
	learned = false;
	if ( !standing ) {
		return headHeight;
	}
	if ( calibrated >= VR_STANDING_HEIGHT_MIN && calibrated <= VR_STANDING_HEIGHT_MAX ) {
		return calibrated;
	}
	learned = true;
	return headHeight < VR_STANDING_HEIGHT_MIN ? VR_STANDING_HEIGHT_MIN
		: headHeight > VR_STANDING_HEIGHT_MAX ? VR_STANDING_HEIGHT_MAX : headHeight;
}

/*
====================
Two-handed aim

The off hand steadies the gun when its palm closes in front of the weapon
hand, close to the line the gun points along: a foregrip's reach, up to
0.65 m ahead and within 0.2 m of the aim (both in metres, scaled to units).
====================
*/
inline bool VR_OffHandOnForegrip( const vrVec3_t &weaponPalm, const vrVec3_t &aimForward,
		const vrVec3_t &offPalm, float unitsPerMetre ) {
	const vrVec3_t reach = VR_Sub( offPalm, weaponPalm );
	const float along = VR_Dot( reach, aimForward );
	if ( along < 0.05f * unitsPerMetre || along > 0.65f * unitsPerMetre ) {
		return false;
	}
	return VR_Length( VR_Sub( reach, VR_Scale( aimForward, along ) ) ) < 0.2f * unitsPerMetre;
}

// The gun held in both hands points from the rear palm through the front
// one. It keeps the weapon hand's roll: left comes from that hand's up, so
// tilting the rear wrist still cants the gun. Rows are forward, left, up.
// False when the palms coincide.
inline bool VR_TwoHandedAxis( const vrVec3_t &rearPalm, const vrVec3_t &frontPalm, const vrVec3_t &rearUp, vrVec3_t rows[3] ) {
	vrVec3_t forward = VR_Sub( frontPalm, rearPalm );
	const float length = VR_Length( forward );
	if ( length < 1e-3f ) {
		return false;
	}
	forward = VR_Scale( forward, 1.0f / length );
	vrVec3_t left = VR_Cross( rearUp, forward );
	float leftLength = VR_Length( left );
	if ( leftLength < 1e-3f ) {
		// the rear hand's up runs along the gun: take the world's
		left = VR_Cross( VR_Vec3( 0.0f, 0.0f, 1.0f ), forward );
		leftLength = VR_Length( left );
		if ( leftLength < 1e-3f ) {
			left = VR_Vec3( 0.0f, 1.0f, 0.0f );
			leftLength = 1.0f;
		}
	}
	left = VR_Scale( left, 1.0f / leftLength );
	rows[0] = forward;
	rows[1] = left;
	rows[2] = VR_Cross( forward, left );
	return true;
}

/*
====================
Turning

A snap turn fires once when the stick passes the press threshold and re-arms
only after it falls back below the release threshold, so holding the stick
turns exactly once. Pushing the stick right (positive X) turns right, which
is a negative engine yaw.
====================
*/
struct vrSnapTurnState_t {
	bool latched;
};

inline float VR_SnapTurn( vrSnapTurnState_t &state, float stickX, float stepDegrees,
		float pressThreshold = 0.75f, float releaseThreshold = 0.35f ) {
	if ( !std::isfinite( stickX ) ) {
		return 0.0f;
	}
	const float magnitude = std::fabs( stickX );
	if ( state.latched ) {
		if ( magnitude < releaseThreshold ) {
			state.latched = false;
		}
		return 0.0f;
	}
	if ( magnitude < pressThreshold ) {
		return 0.0f;
	}
	state.latched = true;
	return stickX > 0.0f ? -stepDegrees : stepDegrees;
}

inline float VR_ApplyDeadzone( float value, float deadzone ) {
	if ( !std::isfinite( value ) ) {
		return 0.0f;
	}
	const float magnitude = std::fabs( value );
	if ( magnitude <= deadzone || deadzone >= 1.0f ) {
		return 0.0f;
	}
	const float scaled = ( magnitude - deadzone ) / ( 1.0f - deadzone );
	const float clamped = scaled > 1.0f ? 1.0f : scaled;
	return value < 0.0f ? -clamped : clamped;
}

inline float VR_SmoothTurn( float stickX, float degreesPerSecond, float deltaSeconds, float deadzone ) {
	if ( !std::isfinite( deltaSeconds ) || deltaSeconds <= 0.0f ) {
		return 0.0f;
	}
	return -VR_ApplyDeadzone( stickX, deadzone ) * degreesPerSecond * deltaSeconds;
}

/*
====================
Head offset

Limits how far the tracked head may move horizontally away from the
player's collision centre, in engine units; vertical motion (leaning,
crouching in place) is kept.
====================
*/
inline vrVec3_t VR_ClampHorizontalOffset( const vrVec3_t &offset, float maxRadius ) {
	if ( !VR_IsFinite( offset ) ) {
		return VR_Vec3( 0.0f, 0.0f, 0.0f );
	}
	const float radius = std::sqrt( offset.x * offset.x + offset.y * offset.y );
	if ( maxRadius < 0.0f || radius <= maxRadius || radius <= 0.0f ) {
		return offset;
	}
	const float scale = maxRadius / radius;
	return VR_Vec3( offset.x * scale, offset.y * scale, offset.z );
}

/*
====================
Eye composition

Places a tracked pose (engine axes and units, relative to the tracking
origin) in the world, given the world position of the tracking origin and
the yaw of the player's body.
====================
*/
inline void VR_ComposeWorldPose( const vrVec3_t &originWorld, float bodyYawDegrees,
		const vrVec3_t &poseOffset, const vrQuat_t &poseOrientation,
		vrVec3_t &outPosition, vrQuat_t &outOrientation ) {
	const vrQuat_t body = VR_YawQuat( bodyYawDegrees );
	outPosition = VR_Add( originWorld, VR_RotateVector( body, poseOffset ) );
	outOrientation = VR_QuatNormalize( VR_QuatMultiply( body, poseOrientation ) );
}

/*
====================
Quad intersection

Intersects a ray with a quad layer, both in the same space (OpenXR axes).
The quad faces its local +Z; u runs left to right and v top to bottom, which
is the 640x480 GUI convention.
====================
*/
struct vrQuadHit_t {
	bool	hit;
	float	u, v;
	float	distance;
};

inline vrQuadHit_t VR_RayQuadIntersect( const vrVec3_t &rayOrigin, const vrVec3_t &rayDirection,
		const vrVec3_t &quadCenter, const vrQuat_t &quadOrientation, float width, float height ) {
	vrQuadHit_t result = { false, 0.0f, 0.0f, 0.0f };
	if ( !VR_IsFinite( rayOrigin ) || !VR_IsFinite( rayDirection ) || width <= 0.0f || height <= 0.0f ) {
		return result;
	}
	const vrQuat_t orientation = VR_QuatNormalize( quadOrientation );
	const vrVec3_t normal = VR_RotateVector( orientation, VR_Vec3( 0.0f, 0.0f, 1.0f ) );
	const float denominator = VR_Dot( rayDirection, normal );
	// only rays travelling into the visible face count
	if ( denominator > -1e-6f ) {
		return result;
	}
	const float t = VR_Dot( VR_Sub( quadCenter, rayOrigin ), normal ) / denominator;
	if ( !std::isfinite( t ) || t < 0.0f ) {
		return result;
	}
	const vrVec3_t point = VR_Add( rayOrigin, VR_Scale( rayDirection, t ) );
	const vrVec3_t local = VR_RotateVector( VR_QuatConjugate( orientation ), VR_Sub( point, quadCenter ) );
	const float u = local.x / width + 0.5f;
	const float v = 0.5f - local.y / height;
	if ( u < 0.0f || u > 1.0f || v < 0.0f || v > 1.0f ) {
		return result;
	}
	result.hit = true;
	result.u = u;
	result.v = v;
	result.distance = t;
	return result;
}

#endif /* !__VR_MATH_CORE_H__ */
