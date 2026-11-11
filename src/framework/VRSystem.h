// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#ifndef __VRSYSTEM_H__
#define __VRSYSTEM_H__

/*
===============================================================================

	Virtual reality presentation and input.

	The engine owns the OpenXR session (src/sys/openxr) and brackets every
	presented screen update with BeginFrame/EndFrame. The renderer module
	presents into the session's swapchains (idRenderSystem::SetVRFrame and
	SetVRRenderTarget), and the game modules read the tracked poses below to
	build one view per eye. Builds without OpenXR, and dedicated servers, run
	the null implementation, which never activates.

	Everything here crosses the game-module boundary, so it stays POD apart
	from the idVec3/idMat3 members renderView_t already carries.
	See docs/dev/archive/2026-10-18/plans/2026-10-02-openxr-vr.md.

===============================================================================
*/

#include "VRMathCore.h"

const int VR_NUM_EYES = 2;
const int VR_NUM_HANDS = 2;

typedef enum {
	VR_HAND_LEFT = 0,
	VR_HAND_RIGHT = 1
} vrHand_t;

typedef enum {
	VR_AIM_HEAD = 0,		// gaze aim: the usercmd follows the head
	VR_AIM_HAND = 1			// the weapon hand's controller aims
} vrAimMode_t;

// What marks the weapon hand's aim (vr_aimLaser).
typedef enum {
	VR_AIM_LASER_OFF = 0,
	VR_AIM_LASER_DOT = 1,	// a dot where the shot lands
	VR_AIM_LASER_BEAM = 2	// the dot and a beam from the weapon hand
} vrAimLaser_t;

// A tracked pose relative to the recentred tracking origin, in engine axes
// (forward, left, up) and game units. The origin is where the player's eye
// sits when the head is at the recentre position.
typedef struct vrPose_s {
	bool					valid;
	idVec3					origin;
	idMat3					axis;		// rows: forward, left, up (renderView_t::viewaxis layout)
} vrPose_t;

// One eye: its pose and the half-angle tangents of its off-axis frustum
// (left and down negative), exactly as rendered and submitted.
typedef struct vrEyeView_s {
	vrPose_t				pose;
	float					tanLeft, tanRight, tanUp, tanDown;
} vrEyeView_t;

typedef struct vrFrameState_s {
	int						frameNumber;	// counts XR frames opened by the engine
	bool					active;			// a session is presenting to a headset this frame
	bool					stereo;			// eye targets exist: render the first-person 3D pass per eye
	bool					focused;		// the runtime routes controller input to openQ4
	vrPose_t				head;
	vrEyeView_t				eyes[VR_NUM_EYES];
	vrPose_t				grip[VR_NUM_HANDS];	// where the hand holds the controller
	vrPose_t				aim[VR_NUM_HANDS];	// the controller's pointing ray (+X forward)

	// The body's yaw in usercmd angle space. The world yaw of the tracking
	// space is bodyYaw + the player's deltaViewAngles yaw.
	float					bodyYaw;

	int						aimMode;		// vrAimMode_t
	int						weaponHand;		// vrHand_t
	float					unitsPerMetre;
	float					headOffsetLimit;	// horizontal head travel from the body, units

	// fine-tuning of the held view weapon, in the weapon hand's aim axes
	// (units, degrees); VR_WeaponTransform holds it by the model's own grip
	idVec3					weaponOffset;
	float					weaponPitch;

	int						aimLaser;		// vrAimLaser_t
	bool					roomScale;		// the body walks after the head (vr_roomScale)
} vrFrameState_t;

// What the usercmd generator applies for one tic while VR drives input.
typedef struct vrUsercmdInput_s {
	float					aimYaw;			// absolute usercmd yaw, degrees (body yaw + local aim)
	float					aimPitch;		// usercmd pitch, degrees (positive looks down)
	float					forward;		// locomotion, -1..1, in the aim frame
	float					right;
	bool					crouch;			// the tracked head crouched (vr_physicalCrouch)
} vrUsercmdInput_t;

class idVRSystem {
public:
	virtual					~idVRSystem( void ) {}

	// Registers commands; makes no OpenXR call.
	virtual void			Init( void ) = 0;
	virtual void			Shutdown( void ) = 0;

	// The renderer has a live device (after InitOpenGL and every vid_restart).
	// Starts a session when vr_enable is set.
	virtual void			RendererStarted( void ) = 0;
	// The renderer is about to destroy its device: the session and its
	// swapchains are torn down first.
	virtual void			RendererStopping( void ) = 0;

	// Bracket one presented screen update. While a session runs, BeginFrame
	// waits on the headset's frame timing, so it is also the frame pacer.
	virtual void			BeginFrame( void ) = 0;
	virtual void			EndFrame( void ) = 0;

	// True while the headset paces frames: com_maxfps and window vsync stand down.
	virtual bool			IsPacing( void ) const = 0;
	// True while a session presents.
	virtual bool			IsActive( void ) const = 0;

	// Pose and presentation state for the screen update in progress.
	virtual void			GetFrameState( vrFrameState_t &state ) const = 0;

	// Called by the usercmd generator (possibly on the async tic thread).
	// otherYawDelta is the yaw the mouse, keys or a gamepad turned this tic;
	// it turns the body. Returns false while VR does not drive input.
	virtual bool			GetUsercmdInput( float otherYawDelta, float currentYaw, vrUsercmdInput_t &input ) = 0;

	// Menus: marks where the controller points on the virtual screen. Called
	// once all 2D drawing is queued, before the renderer's EndFrame.
	virtual void			DrawMenuPointer( void ) = 0;

	// Haptics: vibrates one controller (vrHand_t) at a strength of 0..1,
	// scaled by vr_hapticStrength, for durationMsec. Nothing happens without
	// a focused session.
	virtual void			Vibrate( int hand, float amplitude, int durationMsec ) = 0;

	// Room scale: the body walked after the head by this horizontal distance
	// in tracking space (engine axes, game units). The tracking origin moves
	// with it, so every pose read afterwards is that much nearer the origin.
	// True when it moved: in multiplayer only a room-scale walk's tics move it.
	virtual bool			ShiftTrackingOrigin( const idVec3 &trackingDelta ) = 0;
};

extern idVRSystem *			vrSystem;

// True while a session presents and the runtime routes controller input to
// openQ4. Menus then take that input whether or not the desktop window, which
// the player cannot see, holds focus (engine only).
bool						VR_HasInputFocus( void );

/*
===============================================================================

	Placing tracked poses in the world (both game modules).

	The tracking origin sits at the player's eye and faces trackingYaw, the
	world yaw of the tracking space: vrFrameState_t::bodyYaw plus the
	player's deltaViewAngles yaw (the same mapping that turns usercmd angles
	into view angles). A tracked pose then lands at
	eyeOrigin + pose.origin * yaw, with axis pose.axis * yaw.

===============================================================================
*/

ID_INLINE idMat3 VR_TrackingAxis( float trackingYaw ) {
	return idAngles( 0.0f, trackingYaw, 0.0f ).ToMat3();
}

ID_INLINE void VR_PoseToWorld( const vrPose_t &pose, const idVec3 &eyeOrigin, float trackingYaw, idVec3 &origin, idMat3 &axis ) {
	const idMat3 tracking = VR_TrackingAxis( trackingYaw );
	origin = eyeOrigin + pose.origin * tracking;
	axis = pose.axis * tracking;
}

// Horizontal head travel past the limit is cancelled, so leaning walks the
// camera no further than the body's edge; height changes always pass.
ID_INLINE idVec3 VR_HeadOffsetCorrection( const vrFrameState_t &frame ) {
	if ( !frame.head.valid || frame.headOffsetLimit < 0.0f ) {
		return vec3_origin;
	}
	const idVec3 &head = frame.head.origin;
	const float radius = idMath::Sqrt( head.x * head.x + head.y * head.y );
	if ( radius <= frame.headOffsetLimit || radius <= 0.0f ) {
		return vec3_origin;
	}
	const float scale = ( radius - frame.headOffsetLimit ) / radius;
	return idVec3( -head.x * scale, -head.y * scale, 0.0f );
}

// One eye of the headset as a render view: the tracked eye pose placed in
// the world, and its off-axis frustum, with fov_x/fov_y the enclosing field
// of view for anything that only estimates.
ID_INLINE void VR_BuildEyeView( const vrFrameState_t &frame, int eye, const idVec3 &eyeOrigin, float trackingYaw, renderView_t &view ) {
	const vrEyeView_t &eyeView = frame.eyes[eye];
	vrPose_t pose = eyeView.pose;
	pose.origin += VR_HeadOffsetCorrection( frame );
	VR_PoseToWorld( pose, eyeOrigin, trackingYaw, view.vieworg, view.viewaxis );
	view.asymmetricFov = true;
	view.fovTanLeft = eyeView.tanLeft;
	view.fovTanRight = eyeView.tanRight;
	view.fovTanUp = eyeView.tanUp;
	view.fovTanDown = eyeView.tanDown;
	const float halfX = Max( idMath::Fabs( eyeView.tanLeft ), idMath::Fabs( eyeView.tanRight ) );
	const float halfY = Max( idMath::Fabs( eyeView.tanUp ), idMath::Fabs( eyeView.tanDown ) );
	view.fov_x = RAD2DEG( 2.0f * idMath::ATan( halfX ) );
	view.fov_y = RAD2DEG( 2.0f * idMath::ATan( halfY ) );
}

// The view weapon held in the weapon hand at true scale. barrel is the
// direction the model's barrel points and grip the point its hand holds, both
// in model space: the barrel turns onto the hand's aim, by the shortest turn,
// and the grip point lands on the hand's grip pose. The view-weapon offsets
// and pitch then fine-tune the hold in the aim axes.
ID_INLINE bool VR_WeaponTransform( const vrFrameState_t &frame, const idVec3 &eyeOrigin, float trackingYaw,
		const idVec3 &barrel, const idVec3 &grip, idVec3 &origin, idMat3 &axis ) {
	const int hand = frame.weaponHand == VR_HAND_LEFT ? VR_HAND_LEFT : VR_HAND_RIGHT;
	if ( !frame.aim[hand].valid ) {
		return false;
	}
	const idVec3 correction = VR_HeadOffsetCorrection( frame );
	vrPose_t pose = frame.aim[hand];
	pose.origin += correction;
	idVec3 aimOrigin;
	idMat3 aimAxis;
	VR_PoseToWorld( pose, eyeOrigin, trackingYaw, aimOrigin, aimAxis );

	// the palm is at the grip pose; without one, at the aim's origin
	idVec3 holdOrigin = aimOrigin;
	if ( frame.grip[hand].valid ) {
		pose = frame.grip[hand];
		pose.origin += correction;
		idMat3 gripAxis;
		VR_PoseToWorld( pose, eyeOrigin, trackingYaw, holdOrigin, gripAxis );
	}

	const idMat3 held = idAngles( frame.weaponPitch, 0.0f, 0.0f ).ToMat3() * aimAxis;
	vrVec3_t turn[3];
	VR_RotationBetween( VR_Vec3( barrel.x, barrel.y, barrel.z ), VR_Vec3( 1.0f, 0.0f, 0.0f ), turn );
	const idMat3 align( turn[0].x, turn[0].y, turn[0].z, turn[1].x, turn[1].y, turn[1].z, turn[2].x, turn[2].y, turn[2].z );
	axis = align * held;
	origin = holdOrigin + frame.weaponOffset * held - grip * axis;
	return true;
}

/*
===============================================================================

	The aim marker (vr_aimLaser), drawn into each eye.

	The game module that fires the shot traces its path once per frame. Each
	eye then draws the marker at its own projection of the hit point, so the
	pair fuses at the target's depth instead of at a fixed distance. It is 2D
	over the eye's finished 3D pass, which the trace makes safe: the shot's
	path ends at the first surface, so nothing should hide the dot from the
	weapon. When nearer geometry hides it from the head, it dims instead.

===============================================================================
*/

typedef struct vrAimMarker_s {
	idVec3					muzzle;		// where a beam leaves: the drawn weapon's muzzle
	idVec3					target;		// where the shot lands, or the end of its range
	bool					hit;		// a surface stops the shot within range
	bool					hidden;		// nearer geometry hides the target from the head
} vrAimMarker_t;

const float VR_AIM_RANGE = 8192.0f;

// A world point as seen from an eye view: tangents of the view axis.
ID_INLINE vrEyeTangent_t VR_EyeTangent( const renderView_t &view, const idVec3 &point ) {
	const idVec3 offset = point - view.vieworg;
	return VR_EyeTangentOf( VR_Vec3( offset * view.viewaxis[0], offset * view.viewaxis[1], offset * view.viewaxis[2] ) );
}

// Eye tangents on the 640x480 canvas an eye target's 2D pass stretches over
// the eye's whole image.
ID_INLINE idVec2 VR_EyeCanvasPoint( const renderView_t &view, float tangentX, float tangentY ) {
	vrFovTangents_t fov;
	fov.left = view.fovTanLeft;
	fov.right = view.fovTanRight;
	fov.up = view.fovTanUp;
	fov.down = view.fovTanDown;
	float u, v;
	VR_TangentToImage( fov, tangentX, tangentY, u, v );
	return idVec2( u * SCREEN_WIDTH, v * SCREEN_HEIGHT );
}

// A disc of a fixed angular radius (as a tangent), so it looks the same size
// at any range, round in the eye however the canvas stretches.
ID_INLINE void VR_DrawEyeDisc( const renderView_t &view, float tangentX, float tangentY, float radius, const idMaterial *material ) {
	const int segments = 16;
	const idVec2 centre = VR_EyeCanvasPoint( view, tangentX, tangentY );
	idVec2 previous = VR_EyeCanvasPoint( view, tangentX + radius, tangentY );
	for ( int i = 1; i <= segments; i++ ) {
		float s, c;
		idMath::SinCos( idMath::TWO_PI * static_cast<float>( i ) / static_cast<float>( segments ), s, c );
		const idVec2 next = VR_EyeCanvasPoint( view, tangentX + radius * c, tangentY + radius * s );
		renderSystem->DrawStretchTri( centre, previous, next, vec2_origin, vec2_origin, vec2_origin, material );
		previous = next;
	}
}

// The beam leaves the muzzle, fades in, and fades out again within a few
// metres: it shows the line of fire without striping the scene. Its quads
// narrow with depth like a thin rod would.
ID_INLINE void VR_DrawAimBeam( const renderView_t &view, const vrAimMarker_t &marker, const idMaterial *material ) {
	const float clearOfWeapon = 0.5f;			// units out of the muzzle
	const float fullStrength = 6.0f;
	const float radius = 0.12f;					// units, about 3 mm
	const float minimumHalfWidth = 0.0008f;		// tangent: about a pixel at typical headset density
	const int segments = 12;
	idVec3 direction = marker.target - marker.muzzle;
	const float length = direction.Normalize();
	const float fadeEnd = Min( length, 160.0f );
	if ( fadeEnd <= fullStrength ) {
		return;
	}
	for ( int i = 0; i < segments; i++ ) {
		const float nearDistance = clearOfWeapon + ( fadeEnd - clearOfWeapon ) * static_cast<float>( i ) / static_cast<float>( segments );
		const float farDistance = clearOfWeapon + ( fadeEnd - clearOfWeapon ) * static_cast<float>( i + 1 ) / static_cast<float>( segments );
		const float middle = 0.5f * ( nearDistance + farDistance );
		const float fadeIn = idMath::ClampFloat( 0.0f, 1.0f, ( middle - clearOfWeapon ) / ( fullStrength - clearOfWeapon ) );
		const float fadeOut = idMath::ClampFloat( 0.0f, 1.0f, ( fadeEnd - middle ) / ( fadeEnd - fullStrength ) );
		const float alpha = 0.55f * fadeIn * fadeOut;
		const vrEyeTangent_t a = VR_EyeTangent( view, marker.muzzle + direction * nearDistance );
		const vrEyeTangent_t b = VR_EyeTangent( view, marker.muzzle + direction * farDistance );
		if ( alpha <= 0.0f || !a.inFront || !b.inFront ) {
			continue;
		}
		idVec2 along( b.x - a.x, b.y - a.y );
		if ( along.Normalize() <= 0.0f ) {
			continue;
		}
		const idVec2 across( -along.y, along.x );
		const float halfA = Max( radius / a.depth, minimumHalfWidth );
		const float halfB = Max( radius / b.depth, minimumHalfWidth );
		const idVec2 a0 = VR_EyeCanvasPoint( view, a.x + across.x * halfA, a.y + across.y * halfA );
		const idVec2 a1 = VR_EyeCanvasPoint( view, a.x - across.x * halfA, a.y - across.y * halfA );
		const idVec2 b0 = VR_EyeCanvasPoint( view, b.x + across.x * halfB, b.y + across.y * halfB );
		const idVec2 b1 = VR_EyeCanvasPoint( view, b.x - across.x * halfB, b.y - across.y * halfB );
		// the 2D pass culls by winding: keep the disc's (counter-clockwise in tangent space)
		renderSystem->SetColor4( 1.0f, 0.15f, 0.1f, alpha );
		renderSystem->DrawStretchTri( a0, b1, b0, vec2_origin, vec2_origin, vec2_origin, material );
		renderSystem->DrawStretchTri( a0, a1, b1, vec2_origin, vec2_origin, vec2_origin, material );
	}
}

// Called between an eye's 3D pass and its fade, with the "_white" material.
ID_INLINE void VR_DrawAimMarker( const renderView_t &view, const vrAimMarker_t &marker, int laser, const idMaterial *material ) {
	if ( laser == VR_AIM_LASER_OFF || material == NULL || !view.asymmetricFov ) {
		return;
	}
	const bool previousUIViewportMode = renderSystem->GetUseUIViewportFor2D();
	renderSystem->SetUseUIViewportFor2D( false );
	if ( laser == VR_AIM_LASER_BEAM ) {
		VR_DrawAimBeam( view, marker, material );
	}
	const vrEyeTangent_t spot = VR_EyeTangent( view, marker.target );
	if ( spot.inFront ) {
		// dimmer when the head can't see the target, or the shot meets nothing
		const float alpha = marker.hidden ? 0.35f : ( marker.hit ? 1.0f : 0.6f );
		renderSystem->SetColor4( 0.0f, 0.0f, 0.0f, 0.5f * alpha );
		VR_DrawEyeDisc( view, spot.x, spot.y, 0.0080f, material );
		renderSystem->SetColor4( 1.0f, 0.15f, 0.1f, alpha );
		VR_DrawEyeDisc( view, spot.x, spot.y, 0.0050f, material );
		renderSystem->SetColor4( 1.0f, 0.8f, 0.7f, alpha );
		VR_DrawEyeDisc( view, spot.x, spot.y, 0.0020f, material );
	}
	renderSystem->SetColor4( 1.0f, 1.0f, 1.0f, 1.0f );
	renderSystem->SetUseUIViewportFor2D( previousUIViewportMode );
}

/*
===============================================================================

	The comfort vignette (vr_comfortVignette), drawn into each eye.

	While the stick moves the player or turns them smoothly, each eye darkens
	towards its edges, where the eye notices motion the body does not feel,
	around a clear centre on the eye's own axis; both eyes' axes are parallel,
	so the pair fuses at infinity. strength is 0..1: at 1 the clear centre
	narrows to 60 degrees across and the edge goes black.

===============================================================================
*/
ID_INLINE void VR_DrawComfortVignette( const renderView_t &view, float strength, const idMaterial *material ) {
	if ( material == NULL || strength <= 0.01f ) {
		return;
	}
	strength = Min( strength, 1.0f );
	// tangents of the angle off the eye's axis: where the darkening starts, how
	// far it ramps, and an outer edge beyond any lens's corner
	const float clear = idMath::Tan( DEG2RAD( 58.0f - 28.0f * strength ) );
	const float ramp = 0.5f;
	const float outside = 6.0f;
	const int rings = 24;
	const int segments = 32;
	for ( int ring = 0; ring <= rings; ring++ ) {
		const float inner = clear + ramp * static_cast<float>( ring ) / static_cast<float>( rings );
		const float outer = ring < rings ? clear + ramp * static_cast<float>( ring + 1 ) / static_cast<float>( rings ) : outside;
		// a smoothstep ramp in fine rings, so bright skies show no bands
		const float t = ( static_cast<float>( ring ) + 0.5f ) / static_cast<float>( rings );
		const float alpha = ring < rings ? strength * t * t * ( 3.0f - 2.0f * t ) : strength;
		renderSystem->SetColor4( 0.0f, 0.0f, 0.0f, alpha );
		float s0 = 0.0f, c0 = 1.0f;
		for ( int i = 1; i <= segments; i++ ) {
			float s1, c1;
			idMath::SinCos( idMath::TWO_PI * static_cast<float>( i ) / static_cast<float>( segments ), s1, c1 );
			// counter-clockwise in tangent space, which the 2D pass keeps
			const idVec2 a = VR_EyeCanvasPoint( view, inner * c0, inner * s0 );
			const idVec2 b = VR_EyeCanvasPoint( view, outer * c0, outer * s0 );
			const idVec2 c = VR_EyeCanvasPoint( view, outer * c1, outer * s1 );
			const idVec2 d = VR_EyeCanvasPoint( view, inner * c1, inner * s1 );
			renderSystem->DrawStretchTri( a, b, c, vec2_origin, vec2_origin, vec2_origin, material );
			renderSystem->DrawStretchTri( a, c, d, vec2_origin, vec2_origin, vec2_origin, material );
			s0 = s1;
			c0 = c1;
		}
	}
	renderSystem->SetColor4( 1.0f, 1.0f, 1.0f, 1.0f );
}

#endif /* !__VRSYSTEM_H__ */
