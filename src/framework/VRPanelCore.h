// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#ifndef __VR_PANEL_CORE_H__
#define __VR_PANEL_CORE_H__

/*
===============================================================================

	The menu panel on the off hand (VR-09), engine-free so the native test
	(tools/tests/native/VRMathCoreTest.cpp) runs it directly.

	In OpenXR axes: +Y up, -Z forward, metres. A quad layer shows its face
	along its own +Z, so a panel's orientation turns +Z toward the head.

	The menu panel grows out of the off hand and stands on it: its centre
	half its height and a few centimetres above the hand, its face turned to
	the head by yaw and pitch, never roll, so it reads level however the hand
	is held. It follows the hand only once the hand has moved past a small
	dead zone, then catches up within about a tenth of a second: a steady
	hand's tremor never moves what the pointer aims at. Opening, it grows from
	the hand to its place over 180 ms.

===============================================================================
*/

#include "VRMathCore.h"

const float VR_PANEL_LIFT = 0.06f;			// metres between the hand and the panel's lower edge
const float VR_PANEL_DEAD_ZONE = 0.012f;	// metres the hand moves before the panel follows
const float VR_PANEL_DEAD_TURN = 3.0f;		// degrees the facing turns before the panel follows
const float VR_PANEL_SETTLE = 0.05f;		// seconds: the follow's time constant
const float VR_PANEL_OPEN = 0.18f;			// seconds to grow out of the hand

struct vrHandPanel_t {
	vrVec3_t	position;		// where the panel rests, its centre
	vrQuat_t	orientation;	// its +Z toward the head
	float		open;			// 0 just opened .. 1 grown
	bool		following;		// catching up with the hand
	bool		valid;			// open; false re-grows it from the hand
};

// An orientation whose +Z points from `from` to `to`, by yaw about +Y and
// pitch about +X only: no roll. Facing straight up or down keeps a yaw of 0.
inline vrQuat_t VR_FacingQuat( const vrVec3_t &from, const vrVec3_t &to ) {
	const vrVec3_t d = VR_Sub( to, from );
	const float flat = std::sqrt( d.x * d.x + d.z * d.z );
	if ( flat < 1.0e-6f && std::fabs( d.y ) < 1.0e-6f ) {
		return VR_QuatIdentity();
	}
	const float yaw = flat < 1.0e-6f ? 0.0f : std::atan2( d.x, d.z );
	const float pitch = std::atan2( d.y, flat );
	const vrQuat_t aboutY = { 0.0f, std::sin( yaw * 0.5f ), 0.0f, std::cos( yaw * 0.5f ) };
	const vrQuat_t aboutX = { std::sin( -pitch * 0.5f ), 0.0f, 0.0f, std::cos( -pitch * 0.5f ) };
	return VR_QuatNormalize( VR_QuatMultiply( aboutY, aboutX ) );
}

// The angle between two orientations, degrees.
inline float VR_QuatAngle( const vrQuat_t &a, const vrQuat_t &b ) {
	float dot = std::fabs( a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w );
	dot = dot > 1.0f ? 1.0f : dot;
	return 2.0f * std::acos( dot ) / VR_DEG2RAD;
}

// a towards b by t, the short way round, normalized.
inline vrQuat_t VR_QuatNlerp( const vrQuat_t &a, const vrQuat_t &b, float t ) {
	const float sign = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w < 0.0f ? -1.0f : 1.0f;
	const vrQuat_t r = { a.x + ( sign * b.x - a.x ) * t, a.y + ( sign * b.y - a.y ) * t, a.z + ( sign * b.z - a.z ) * t,
		a.w + ( sign * b.w - a.w ) * t };
	return VR_QuatNormalize( r );
}

// Where a panel of `height` stands on a hand at `hand`.
inline vrVec3_t VR_HandPanelRest( const vrVec3_t &hand, float height ) {
	return VR_Add( hand, VR_Vec3( 0.0f, height * 0.5f + VR_PANEL_LIFT, 0.0f ) );
}

// One frame of the panel over a hand at `hand`, the head at `head`.
// Returns true on the frame it opens.
inline bool VR_HandPanelStep( vrHandPanel_t &panel, const vrVec3_t &hand, const vrVec3_t &head, float height, float seconds ) {
	const vrVec3_t rest = VR_HandPanelRest( hand, height );
	const vrQuat_t facing = VR_FacingQuat( rest, head );
	if ( !panel.valid ) {
		panel.position = rest;
		panel.orientation = facing;
		panel.open = 0.0f;
		panel.following = false;
		panel.valid = true;
		return true;
	}
	seconds = seconds < 0.0f ? 0.0f : ( seconds > 0.1f ? 0.1f : seconds );
	const float distance = VR_Length( VR_Sub( rest, panel.position ) );
	const float turn = VR_QuatAngle( facing, panel.orientation );
	if ( panel.following || distance > VR_PANEL_DEAD_ZONE || turn > VR_PANEL_DEAD_TURN ) {
		const float follow = 1.0f - std::exp( -seconds / VR_PANEL_SETTLE );
		panel.position = VR_Add( panel.position, VR_Scale( VR_Sub( rest, panel.position ), follow ) );
		panel.orientation = VR_QuatNlerp( panel.orientation, facing, follow );
		// it settles once close; the dead zone then holds it still again
		panel.following = VR_Length( VR_Sub( rest, panel.position ) ) > 0.002f || VR_QuatAngle( facing, panel.orientation ) > 0.5f;
	}
	panel.open += seconds / VR_PANEL_OPEN;
	panel.open = panel.open > 1.0f ? 1.0f : panel.open;
	return false;
}

// How far the panel has grown, eased out: 0 at the hand, 1 in place.
inline float VR_HandPanelGrowth( float open ) {
	const float t = open < 0.0f ? 0.0f : ( open > 1.0f ? 1.0f : open );
	const float rest = 1.0f - t;
	return 1.0f - rest * rest * rest;
}

// The panel as drawn while it grows: its centre between the hand and its
// rest, and its scale from a tenth of its size.
inline vrVec3_t VR_HandPanelDrawnCentre( const vrHandPanel_t &panel, const vrVec3_t &hand ) {
	return VR_Add( hand, VR_Scale( VR_Sub( panel.position, hand ), VR_HandPanelGrowth( panel.open ) ) );
}
inline float VR_HandPanelDrawnScale( const vrHandPanel_t &panel ) {
	return 0.1f + 0.9f * VR_HandPanelGrowth( panel.open );
}

#endif /* !__VR_PANEL_CORE_H__ */
