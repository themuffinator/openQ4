// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#ifndef __VR_TOUCH_CORE_H__
#define __VR_TOUCH_CORE_H__

/*
===============================================================================

	Pressing screens with a fingertip (VR-09), engine-free so the native test
	(tools/tests/native/VRMathCoreTest.cpp) runs it directly.

	A fingertip near a screen moves the screen's cursor to the point under it
	and clicks once on contact, as a mouse button pressed and released. One
	poke is one click:

	- within VR_TOUCH_HOVER in front of the face the cursor follows the
	  fingertip, and nothing else happens however long it stays;
	- the fingertip arms only while it is more than VR_TOUCH_RELEASE in front,
	  so a hand that rests close to a screen, or arrives already touching it,
	  never clicks;
	- an armed fingertip that comes within VR_TOUCH_CONTACT of the face, or
	  passes through it, presses; it releases once it is back VR_TOUCH_RELEASE
	  in front, leaves the screen, or goes VR_TOUCH_THROUGH past it;
	- a new press waits VR_TOUCH_REPEAT_MSEC after the last release, so the
	  tremor of a release cannot click twice.

	Distances are metres in front of the face along its normal: negative once
	the fingertip has gone through.

===============================================================================
*/

#include "VRMathCore.h"

const float VR_TOUCH_HOVER = 0.06f;			// metres in front of a screen the fingertip hovers its cursor
const float VR_TOUCH_CONTACT = 0.005f;		// metres in front that count as contact: a fingertip is round
const float VR_TOUCH_RELEASE = 0.02f;		// metres back out that release a press, and arm the next
const float VR_TOUCH_THROUGH = 0.08f;		// metres through a screen at which a hand has passed it
const int VR_TOUCH_REPEAT_MSEC = 120;		// the least time between a release and the next press
const float VR_FINGERTIP_REACH = 0.08f;		// metres from the grip to an extended index fingertip, along the aim

typedef enum {
	VR_TOUCH_NONE = 0,
	VR_TOUCH_DOWN,		// press the button
	VR_TOUCH_UP			// release it
} vrTouchEvent_t;

struct vrTouch_t {
	bool		armed;			// the fingertip came from far enough out to press
	bool		pressed;		// the button is down
	int			releasedMsec;	// when the last press let go
};

inline void VR_TouchReset( vrTouch_t &touch ) {
	touch.armed = false;
	touch.pressed = false;
	touch.releasedMsec = 0;
}

// The fingertip hovers a screen: in front of it within reach, or a little
// through it.
inline bool VR_TouchInRange( float distance ) {
	return std::isfinite( distance ) && distance <= VR_TOUCH_HOVER && distance >= -VR_TOUCH_THROUGH;
}

// One frame of a fingertip `distance` metres in front of a screen (onScreen
// false when it is over no screen). Returns the button event to send.
inline vrTouchEvent_t VR_TouchStep( vrTouch_t &touch, bool onScreen, float distance, int nowMsec ) {
	if ( !onScreen || !VR_TouchInRange( distance ) ) {
		const bool wasPressed = touch.pressed;
		touch.armed = false;
		touch.pressed = false;
		if ( wasPressed ) {
			touch.releasedMsec = nowMsec;
			return VR_TOUCH_UP;
		}
		return VR_TOUCH_NONE;
	}
	if ( touch.pressed ) {
		if ( distance > VR_TOUCH_RELEASE ) {
			touch.pressed = false;
			touch.armed = true;
			touch.releasedMsec = nowMsec;
			return VR_TOUCH_UP;
		}
		return VR_TOUCH_NONE;
	}
	if ( distance > VR_TOUCH_RELEASE ) {
		touch.armed = true;
		return VR_TOUCH_NONE;
	}
	if ( touch.armed && distance <= VR_TOUCH_CONTACT && nowMsec - touch.releasedMsec >= VR_TOUCH_REPEAT_MSEC ) {
		touch.armed = false;
		touch.pressed = true;
		return VR_TOUCH_DOWN;
	}
	return VR_TOUCH_NONE;
}

// Where the index fingertip of a hand is, from its grip position and its
// aim orientation (OpenXR axes: the aim points down -Z), when nothing better
// tells: the hand pointing along its controller.
inline vrVec3_t VR_FingertipPoint( const vrVec3_t &grip, const vrQuat_t &aimOrientation ) {
	return VR_Add( grip, VR_RotateVector( VR_QuatNormalize( aimOrientation ), VR_Vec3( 0.0f, 0.0f, -VR_FINGERTIP_REACH ) ) );
}

// A fingertip against a quad (centre, orientation with its face along +Z,
// width and height in metres), as VR_RayQuadIntersect places a ray on it:
// u and v from its top-left corner, distance in front of the face.
struct vrQuadTouch_t {
	bool	inReach;	// within the quad and in range (VR_TouchInRange)
	float	u, v;
	float	distance;
};

inline vrQuadTouch_t VR_QuadTouch( const vrVec3_t &tip, const vrVec3_t &quadCenter, const vrQuat_t &quadOrientation, float width, float height ) {
	vrQuadTouch_t result = { false, 0.0f, 0.0f, 0.0f };
	if ( !VR_IsFinite( tip ) || !VR_IsFinite( quadCenter ) || width <= 0.0f || height <= 0.0f ) {
		return result;
	}
	const vrVec3_t local = VR_RotateVector( VR_QuatConjugate( VR_QuatNormalize( quadOrientation ) ), VR_Sub( tip, quadCenter ) );
	result.u = local.x / width + 0.5f;
	result.v = 0.5f - local.y / height;
	result.distance = local.z;
	result.inReach = result.u >= 0.0f && result.u <= 1.0f && result.v >= 0.0f && result.v <= 1.0f && VR_TouchInRange( local.z );
	return result;
}

#endif /* !__VR_TOUCH_CORE_H__ */
