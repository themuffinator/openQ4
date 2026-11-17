// Copyright (C) 2026 DarkMatter Productions
#include "../../../src/framework/VRMathCore.h"
#include <cstdio>
#include <cstdlib>
#include <limits>

static void Check( bool condition, const char *message ) {
	if ( !condition ) {
		std::fprintf( stderr, "VR math: %s\n", message );
		std::exit( 1 );
	}
}

static bool Near( float a, float b, float tolerance = 0.0001f ) {
	return std::fabs( a - b ) <= tolerance;
}

static bool NearVec( const vrVec3_t &a, const vrVec3_t &b, float tolerance = 0.0001f ) {
	return Near( a.x, b.x, tolerance ) && Near( a.y, b.y, tolerance ) && Near( a.z, b.z, tolerance );
}

static vrQuat_t AxisAngle( const vrVec3_t &axis, float degrees ) {
	const float half = degrees * VR_DEG2RAD * 0.5f;
	const float s = std::sin( half );
	vrQuat_t q = { axis.x * s, axis.y * s, axis.z * s, std::cos( half ) };
	return q;
}

static void TestBasis( void ) {
	Check( NearVec( VR_XrToEngineDirection( VR_Vec3( 0, 0, -1 ) ), VR_Vec3( 1, 0, 0 ) ), "OpenXR forward is engine +X" );
	Check( NearVec( VR_XrToEngineDirection( VR_Vec3( 1, 0, 0 ) ), VR_Vec3( 0, -1, 0 ) ), "OpenXR right is engine -Y" );
	Check( NearVec( VR_XrToEngineDirection( VR_Vec3( 0, 1, 0 ) ), VR_Vec3( 0, 0, 1 ) ), "OpenXR up is engine +Z" );
	const vrVec3_t sample = VR_Vec3( 0.25f, -1.5f, 3.0f );
	Check( NearVec( VR_EngineToXrDirection( VR_XrToEngineDirection( sample ) ), sample ), "direction round trip" );
	Check( NearVec( VR_XrToEnginePosition( VR_Vec3( 0, 1.7f, 0 ), 40.0f ), VR_Vec3( 0, 0, 68.0f ) ), "standing eye height scales to units" );
}

static void TestOrientation( void ) {
	// Turning the head 90 degrees to the left in OpenXR is a positive rotation
	// about +Y; the engine sees the same turn as +90 yaw.
	const vrQuat_t left = VR_XrToEngineOrientation( AxisAngle( VR_Vec3( 0, 1, 0 ), 90.0f ) );
	Check( Near( VR_QuatYawDegrees( left ), 90.0f, 0.01f ), "left turn is positive engine yaw" );
	vrVec3_t axis[3];
	VR_QuatToAxis( left, axis );
	Check( NearVec( axis[0], VR_Vec3( 0, 1, 0 ) ), "turned forward faces the old left" );
	Check( NearVec( axis[2], VR_Vec3( 0, 0, 1 ) ), "yaw keeps up" );

	// Looking up 30 degrees is a positive rotation about OpenXR +X and a
	// negative engine pitch.
	const vrQuat_t up = VR_XrToEngineOrientation( AxisAngle( VR_Vec3( 1, 0, 0 ), 30.0f ) );
	VR_QuatToAxis( up, axis );
	Check( Near( VR_PitchDegrees( axis[0] ), -30.0f, 0.01f ), "looking up is negative engine pitch" );
	Check( Near( VR_YawDegrees( axis[0] ), 0.0f, 0.01f ), "looking up keeps yaw" );

	const vrQuat_t sample = VR_QuatNormalize( AxisAngle( VR_Vec3( 0.3f, 0.8f, -0.52f ), 47.0f ) );
	const vrQuat_t back = VR_EngineToXrOrientation( VR_XrToEngineOrientation( sample ) );
	Check( Near( back.x, sample.x ) && Near( back.y, sample.y ) && Near( back.z, sample.z ) && Near( back.w, sample.w ),
		"orientation round trip" );

	// The converted quaternion rotates converted vectors exactly as the
	// original rotates the originals.
	const vrVec3_t v = VR_Vec3( 0.2f, -0.7f, 0.4f );
	const vrVec3_t expected = VR_XrToEngineDirection( VR_RotateVector( sample, v ) );
	const vrVec3_t actual = VR_RotateVector( VR_XrToEngineOrientation( sample ), VR_XrToEngineDirection( v ) );
	Check( NearVec( expected, actual ), "conversion commutes with rotation" );

	const vrQuat_t nan = { std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f, 1.0f };
	const vrQuat_t safe = VR_QuatNormalize( nan );
	Check( safe.x == 0.0f && safe.y == 0.0f && safe.z == 0.0f && safe.w == 1.0f, "non-finite orientation becomes identity" );
	const vrQuat_t zero = { 0.0f, 0.0f, 0.0f, 0.0f };
	Check( VR_QuatNormalize( zero ).w == 1.0f, "zero orientation becomes identity" );

	Check( Near( VR_NormalizeDegrees180( 190.0f ), -170.0f ), "wrap above 180" );
	Check( Near( VR_NormalizeDegrees180( -540.0f ), -180.0f ), "wrap far below" );
	Check( VR_NormalizeDegrees180( std::numeric_limits<float>::infinity() ) == 0.0f, "non-finite angle wraps to zero" );
}

static void TestProjection( void ) {
	const vrFovTangents_t symmetric = VR_FovTangentsFromAngles( -45.0f * VR_DEG2RAD, 45.0f * VR_DEG2RAD, 45.0f * VR_DEG2RAD, -45.0f * VR_DEG2RAD );
	Check( VR_FovTangentsValid( symmetric ), "symmetric frustum is valid" );
	float xmin, xmax, ymin, ymax;
	VR_ProjectionExtents( symmetric, 3.0f, xmin, xmax, ymin, ymax );
	Check( Near( xmin, -3.0f ) && Near( xmax, 3.0f ) && Near( ymin, -3.0f ) && Near( ymax, 3.0f ), "90 degree extents equal the near distance" );

	// A typical left eye: more field towards the nose side is narrower.
	const vrFovTangents_t eye = VR_FovTangentsFromAngles( -0.94f, 0.75f, 0.84f, -0.90f );
	Check( VR_FovTangentsValid( eye ), "off-axis eye is valid" );
	float fovX, fovY;
	VR_EnclosingFovDegrees( eye, fovX, fovY );
	Check( Near( fovX, 2.0f * 0.94f * VR_RAD2DEG, 0.01f ), "enclosing horizontal field uses the wider side" );
	Check( Near( fovY, 2.0f * 0.90f * VR_RAD2DEG, 0.01f ), "enclosing vertical field uses the wider side" );

	vrFovTangents_t inverted = eye;
	inverted.left = eye.right;
	inverted.right = eye.left;
	Check( !VR_FovTangentsValid( inverted ), "inverted frustum rejected" );
	vrFovTangents_t broken = eye;
	broken.up = std::numeric_limits<float>::quiet_NaN();
	Check( !VR_FovTangentsValid( broken ), "non-finite frustum rejected" );
	vrFovTangents_t degenerate = eye;
	degenerate.right = 1000.0f;
	Check( !VR_FovTangentsValid( degenerate ), "near-90 degree frustum rejected" );
}

static void TestEyeImage( void ) {
	const vrFovTangents_t symmetric = VR_FovTangentsFromAngles( -45.0f * VR_DEG2RAD, 45.0f * VR_DEG2RAD, 45.0f * VR_DEG2RAD, -45.0f * VR_DEG2RAD );
	float u, v;
	const vrEyeTangent_t ahead = VR_EyeTangentOf( VR_Vec3( 10.0f, 0.0f, 0.0f ) );
	Check( ahead.inFront && Near( ahead.x, 0.0f ) && Near( ahead.y, 0.0f ) && Near( ahead.depth, 10.0f ), "a point dead ahead lies on the view axis" );
	VR_TangentToImage( symmetric, ahead.x, ahead.y, u, v );
	Check( Near( u, 0.5f ) && Near( v, 0.5f ), "the view axis is the centre of a symmetric image" );
	const vrEyeTangent_t corner = VR_EyeTangentOf( VR_Vec3( 2.0f, -2.0f, 2.0f ) );
	VR_TangentToImage( symmetric, corner.x, corner.y, u, v );
	Check( Near( u, 1.0f ) && Near( v, 0.0f ), "45 degrees right and up is the top right corner" );

	const vrFovTangents_t eye = { -1.2f, 0.8f, 1.0f, -1.0f };
	VR_TangentToImage( eye, 0.0f, 0.0f, u, v );
	Check( Near( u, 0.6f ) && Near( v, 0.5f ), "an off-axis eye's view axis sits off the image centre" );

	Check( !VR_EyeTangentOf( VR_Vec3( -1.0f, 0.0f, 0.0f ) ).inFront, "a point behind the eye does not project" );
	Check( !VR_EyeTangentOf( VR_Vec3( 0.0f, 1.0f, 0.0f ) ).inFront, "a point on the eye plane does not project" );
	Check( !VR_EyeTangentOf( VR_Vec3( std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f ) ).inFront, "a non-finite point does not project" );

	// Eyes 64 units apart (the left one on +Y) both see a point 1000 units
	// ahead of their midpoint; its disparity gives back that depth.
	const float ipd = 64.0f;
	const float depth = 1000.0f;
	const vrEyeTangent_t fromLeft = VR_EyeTangentOf( VR_Vec3( depth, -ipd * 0.5f, 0.0f ) );
	const vrEyeTangent_t fromRight = VR_EyeTangentOf( VR_Vec3( depth, ipd * 0.5f, 0.0f ) );
	Check( fromLeft.x > fromRight.x, "the left eye sees a point further right than the right eye does" );
	Check( Near( ipd / ( fromLeft.x - fromRight.x ), depth, 0.01f ), "disparity gives back the point's depth" );
}

static void TestRotationBetween( void ) {
	// a blaster's barrel: 10 degrees left of the model's forward and a little up
	const vrVec3_t barrel = VR_Scale( VR_Vec3( 0.984f, 0.176f, 0.029f ), 1.0f / VR_Length( VR_Vec3( 0.984f, 0.176f, 0.029f ) ) );
	const vrVec3_t forward = VR_Vec3( 1.0f, 0.0f, 0.0f );
	vrVec3_t rows[3];
	VR_RotationBetween( barrel, forward, rows );
	Check( NearVec( VR_MultiplyRows( barrel, rows ), forward, 0.0005f ), "the barrel turns onto the forward axis" );
	for ( int i = 0; i < 3; i++ ) {
		Check( Near( VR_Length( rows[i] ), 1.0f, 0.0005f ), "the turn keeps lengths" );
		for ( int j = i + 1; j < 3; j++ ) {
			Check( Near( VR_Dot( rows[i], rows[j] ), 0.0f, 0.0005f ), "the turn keeps right angles" );
		}
	}
	Check( Near( VR_Dot( VR_Cross( rows[0], rows[1] ), rows[2] ), 1.0f, 0.0005f ), "the turn is a rotation, not a reflection" );
	// the axis it turns about stays put
	const vrVec3_t axis = VR_Cross( barrel, forward );
	Check( NearVec( VR_MultiplyRows( axis, rows ), axis, 0.0005f ), "the turning axis stays where it is" );

	VR_RotationBetween( forward, forward, rows );
	Check( NearVec( VR_MultiplyRows( VR_Vec3( 0.2f, 0.3f, 0.4f ), rows ), VR_Vec3( 0.2f, 0.3f, 0.4f ) ), "parallel vectors leave everything alone" );
}

static void TestZoomScale( void ) {
	Check( VR_ZoomTangentScale( 90.0f, 90.0f ) == 1.0f, "no zoom leaves the eye alone" );
	Check( VR_ZoomTangentScale( 100.0f, 90.0f ) == 1.0f, "a wider field never shrinks the view" );
	Check( Near( VR_ZoomTangentScale( 50.0f, 90.0f ), std::tan( 25.0f * VR_DEG2RAD ), 0.0001f ), "a 50 degree scope magnifies by the tangent ratio" );
	Check( Near( VR_ZoomTangentScale( 10.0f, 90.0f ), 1.0f / 3.0f ), "magnification stops at 3x" );
	Check( VR_ZoomTangentScale( 0.0f, 90.0f ) == 1.0f, "a missing zoom field is no zoom" );
}

static void TestComfortVignette( void ) {
	Check( VR_ComfortMotion( 0.0f, 0.0f ) == 0.0f, "standing still needs no vignette" );
	Check( VR_ComfortMotion( 10.0f, 5.0f ) == 0.0f, "a drift or a slow turn needs none" );
	Check( Near( VR_ComfortMotion( 160.0f, 0.0f ), 1.0f ), "a run is full strength" );
	Check( VR_ComfortMotion( 80.0f, 0.0f ) > 0.4f && VR_ComfortMotion( 80.0f, 0.0f ) < 0.5f, "a walk is about half" );
	Check( Near( VR_ComfortMotion( 0.0f, -120.0f ), 1.0f ), "a full smooth turn either way is full strength" );
	Check( Near( VR_ComfortMotion( 400.0f, 400.0f ), 1.0f ), "strength stops at 1" );
	Check( Near( VR_ComfortMotion( 80.0f, 120.0f ), 1.0f ), "the stronger motion wins" );
	float v = 0.0f;
	for ( int i = 0; i < 9; i++ ) {
		v = VR_ComfortEase( v, 1.0f, 1.0f / 90.0f );
	}
	Check( v > 0.6f, "the vignette is mostly in within a tenth of a second" );
	const float in = v;
	v = VR_ComfortEase( v, 0.0f, 0.1f );
	Check( v > in * 0.6f, "it eases out more slowly than in" );
	Check( VR_ComfortEase( 0.3f, 1.0f, 0.0f ) == 0.3f, "no time, no change" );
}

static void TestRoomScaleWalkInput( void ) {
	float forward, right;
	bool walking = false;
	Check( !VR_RoomScaleWalkInput( 4.0f, 3.0f, 0.0f, walking, forward, right ) && !walking && forward == 0.0f && right == 0.0f,
		"a lean inside the start radius walks nothing" );
	Check( VR_RoomScaleWalkInput( 8.0f, 0.0f, 0.0f, walking, forward, right ) && walking, "a head 8 units ahead starts a walk" );
	Check( Near( forward, 0.25f ) && Near( right, 0.0f ), "half the ramp out is half of the walk, straight ahead" );
	Check( VR_RoomScaleWalkInput( 4.0f, 0.0f, 0.0f, walking, forward, right ) && walking && forward > 0.0f,
		"once walking, the body keeps after a head inside the start radius" );
	Check( !VR_RoomScaleWalkInput( 1.0f, 0.0f, 0.0f, walking, forward, right ) && !walking, "the walk ends within the stop radius" );
	Check( !VR_RoomScaleWalkInput( -5.0f, 0.0f, 0.0f, walking, forward, right ) && !walking,
		"a coast past the head stays inside the free zone" );
	walking = false;
	VR_RoomScaleWalkInput( 0.0f, -30.0f, 0.0f, walking, forward, right );
	Check( Near( forward, 0.0f ) && Near( right, 0.5f ), "a head far to the right walks right at the strongest" );
	// aiming 90 degrees left, a head straight ahead in tracking space is on the aim's right
	walking = false;
	VR_RoomScaleWalkInput( 20.0f, 0.0f, 90.0f, walking, forward, right );
	Check( Near( forward, 0.0f, 0.0001f ) && Near( right, 0.5f, 0.0001f ), "the walk turns into the aim frame" );
}

static void TestPhysicalCrouch( void ) {
	Check( !VR_PhysicalCrouch( 0.1f, false ), "a nod is no crouch" );
	Check( !VR_PhysicalCrouch( 0.35f, false ), "a stoop above the line stays standing" );
	Check( VR_PhysicalCrouch( 0.45f, false ), "a head 45 cm down crouches" );
	Check( VR_PhysicalCrouch( 0.35f, true ), "rising a little keeps the crouch" );
	Check( !VR_PhysicalCrouch( 0.25f, true ), "rising past 30 cm stands again" );
}

static void TestPlaySpace( void ) {
	vrPlaySpaceChoice_t c = VR_ChoosePlaySpace( VR_PLAY_SPACE_AUTO, true, true );
	Check( c.space == VR_BASE_SPACE_STAGE && c.standing && !c.fallback, "auto stands on the stage when the runtime has one" );
	c = VR_ChoosePlaySpace( VR_PLAY_SPACE_AUTO, false, true );
	Check( c.space == VR_BASE_SPACE_LOCAL_FLOOR && c.standing, "auto stands on LOCAL_FLOOR without a stage" );
	c = VR_ChoosePlaySpace( VR_PLAY_SPACE_AUTO, false, false );
	Check( c.space == VR_BASE_SPACE_LOCAL && !c.standing && !c.fallback, "auto sits when no floor is known, and that is no fallback" );
	c = VR_ChoosePlaySpace( VR_PLAY_SPACE_STANDING, false, false );
	Check( c.space == VR_BASE_SPACE_LOCAL && !c.standing && c.fallback, "standing without a floor falls back to head-relative play" );
	c = VR_ChoosePlaySpace( VR_PLAY_SPACE_SEATED, true, true );
	Check( c.space == VR_BASE_SPACE_LOCAL && !c.standing && !c.fallback, "seated stays head-relative on LOCAL even with a stage" );
	c = VR_ChoosePlaySpace( 7, true, false );
	Check( c.space == VR_BASE_SPACE_STAGE && c.standing, "an unknown mode behaves as auto" );

	bool learned = true;
	Check( VR_StandingReference( false, 1.15f, 1.7f, learned ) == 1.15f && !learned, "seated play takes the head at the recentre" );
	Check( VR_StandingReference( true, 1.15f, 1.7f, learned ) == 1.7f && !learned, "a calibrated standing height outlives a seated recentre" );
	Check( VR_StandingReference( true, 1.62f, 0.0f, learned ) == 1.62f && learned, "an uncalibrated standing height is learned from the head" );
	Check( VR_StandingReference( true, 0.75f, 0.0f, learned ) == 0.75f && !learned, "a headset on a desk stands where it is and teaches no height" );
	Check( VR_StandingReference( true, 1.0f, 0.0f, learned ) == 1.0f && learned, "a metre is the lowest standing eye learned" );
	Check( VR_StandingReference( true, 2.9f, 5.0f, learned ) == VR_STANDING_HEIGHT_MAX && learned, "an implausible calibration is learned again, clamped" );
}

static void TestTwoHandedAim( void ) {
	const float upm = 40.0f;	// units per metre
	const vrVec3_t palm = VR_Vec3( 10.0f, -8.0f, -14.0f );
	const vrVec3_t forward = VR_Vec3( 1.0f, 0.0f, 0.0f );
	// a foregrip 30 cm ahead and a hand's width up
	Check( VR_OffHandOnForegrip( palm, forward, VR_Add( palm, VR_Vec3( 0.3f * upm, 0.0f, 0.08f * upm ) ), upm ), "a palm on the foregrip steadies the gun" );
	Check( !VR_OffHandOnForegrip( palm, forward, VR_Add( palm, VR_Vec3( -0.1f * upm, 0.0f, 0.0f ) ), upm ), "a palm behind the gun does not" );
	Check( !VR_OffHandOnForegrip( palm, forward, VR_Add( palm, VR_Vec3( 0.3f * upm, 0.35f * upm, 0.0f ) ), upm ), "a palm out to the side does not" );
	Check( !VR_OffHandOnForegrip( palm, forward, VR_Add( palm, VR_Vec3( 0.8f * upm, 0.0f, 0.0f ) ), upm ), "a palm beyond a foregrip's reach does not" );
	Check( !VR_OffHandOnForegrip( palm, forward, VR_Add( palm, VR_Vec3( 0.02f * upm, 0.0f, 0.0f ) ), upm ), "a palm against the weapon hand does not" );

	// the front palm 30 cm ahead and 5 cm up tilts the aim up by atan( 5 / 30 )
	vrVec3_t rows[3];
	const vrVec3_t front = VR_Add( palm, VR_Vec3( 0.3f * upm, 0.0f, 0.05f * upm ) );
	Check( VR_TwoHandedAxis( palm, front, VR_Vec3( 0.0f, 0.0f, 1.0f ), rows ), "the two-handed axis builds" );
	Check( Near( VR_PitchDegrees( rows[0] ), -std::atan2( 0.05f, 0.3f ) * VR_RAD2DEG, 0.01f ), "the gun points through the front palm" );
	for ( int i = 0; i < 3; i++ ) {
		Check( Near( VR_Length( rows[i] ), 1.0f, 0.0005f ), "two-handed rows are unit length" );
	}
	Check( Near( VR_Dot( rows[0], rows[1] ), 0.0f, 0.0005f ) && Near( VR_Dot( rows[1], rows[2] ), 0.0f, 0.0005f ), "two-handed rows are square" );
	Check( Near( VR_Dot( VR_Cross( rows[0], rows[1] ), rows[2] ), 1.0f, 0.0005f ), "two-handed rows are right-handed" );
	Check( rows[2].z > 0.9f, "the gun stays upright" );

	// a rear wrist canted 30 degrees cants the gun the same way
	const float cant = 30.0f * VR_DEG2RAD;
	VR_TwoHandedAxis( palm, VR_Add( palm, VR_Vec3( 0.3f * upm, 0.0f, 0.0f ) ), VR_Vec3( 0.0f, -std::sin( cant ), std::cos( cant ) ), rows );
	Check( Near( rows[2].y, -std::sin( cant ), 0.001f ) && Near( rows[2].z, std::cos( cant ), 0.001f ), "the rear wrist's roll carries over" );

	// an up running along the gun falls back to the world's
	Check( VR_TwoHandedAxis( palm, VR_Add( palm, VR_Vec3( 0.3f * upm, 0.0f, 0.0f ) ), VR_Vec3( 1.0f, 0.0f, 0.0f ), rows ) && rows[2].z > 0.99f, "a degenerate up still gives an upright gun" );
	Check( !VR_TwoHandedAxis( palm, palm, VR_Vec3( 0.0f, 0.0f, 1.0f ), rows ), "coinciding palms give no axis" );
}

static void TestTurning( void ) {
	vrSnapTurnState_t snap = { false };
	Check( VR_SnapTurn( snap, 0.5f, 45.0f ) == 0.0f, "below the press threshold does nothing" );
	Check( VR_SnapTurn( snap, 0.9f, 45.0f ) == -45.0f, "pushing right turns right once" );
	Check( VR_SnapTurn( snap, 1.0f, 45.0f ) == 0.0f, "holding does not repeat" );
	Check( VR_SnapTurn( snap, 0.5f, 45.0f ) == 0.0f, "hysteresis keeps the latch" );
	Check( VR_SnapTurn( snap, 0.1f, 45.0f ) == 0.0f, "release re-arms" );
	Check( VR_SnapTurn( snap, -0.95f, 30.0f ) == 30.0f, "pushing left turns left" );
	Check( VR_SnapTurn( snap, std::numeric_limits<float>::quiet_NaN(), 30.0f ) == 0.0f, "non-finite stick ignored" );

	Check( VR_SmoothTurn( 0.1f, 120.0f, 0.5f, 0.2f ) == 0.0f, "deadzone swallows drift" );
	Check( Near( VR_SmoothTurn( 1.0f, 120.0f, 0.5f, 0.2f ), -60.0f ), "full right deflection turns right at speed" );
	Check( Near( VR_SmoothTurn( -0.6f, 100.0f, 1.0f, 0.2f ), 50.0f ), "partial left deflection rescales past the deadzone" );
	Check( VR_SmoothTurn( 1.0f, 120.0f, -1.0f, 0.2f ) == 0.0f, "negative time step ignored" );
}

static void TestOffsets( void ) {
	const vrVec3_t inside = VR_ClampHorizontalOffset( VR_Vec3( 3.0f, 4.0f, 9.0f ), 10.0f );
	Check( NearVec( inside, VR_Vec3( 3.0f, 4.0f, 9.0f ) ), "small offsets pass" );
	const vrVec3_t outside = VR_ClampHorizontalOffset( VR_Vec3( 30.0f, 40.0f, -6.0f ), 10.0f );
	Check( NearVec( outside, VR_Vec3( 6.0f, 8.0f, -6.0f ) ), "horizontal clamp keeps direction and height" );
	const vrVec3_t broken = VR_ClampHorizontalOffset( VR_Vec3( std::numeric_limits<float>::infinity(), 0.0f, 0.0f ), 10.0f );
	Check( NearVec( broken, VR_Vec3( 0.0f, 0.0f, 0.0f ) ), "non-finite offset collapses" );

	vrVec3_t position;
	vrQuat_t orientation;
	VR_ComposeWorldPose( VR_Vec3( 100.0f, 200.0f, 64.0f ), 90.0f, VR_Vec3( 10.0f, 0.0f, 2.0f ), VR_QuatIdentity(), position, orientation );
	Check( NearVec( position, VR_Vec3( 100.0f, 210.0f, 66.0f ) ), "body yaw rotates the tracked offset" );
	Check( Near( VR_QuatYawDegrees( orientation ), 90.0f, 0.01f ), "body yaw rotates the orientation" );
	VR_ComposeWorldPose( VR_Vec3( 0.0f, 0.0f, 0.0f ), 30.0f, VR_Vec3( 0.0f, 0.0f, 0.0f ), VR_YawQuat( 15.0f ), position, orientation );
	Check( Near( VR_QuatYawDegrees( orientation ), 45.0f, 0.01f ), "head yaw adds to body yaw" );
}

static void TestQuad( void ) {
	const vrVec3_t center = VR_Vec3( 0.0f, 0.0f, -2.0f );
	const vrQuat_t facing = VR_QuatIdentity();
	vrQuadHit_t hit = VR_RayQuadIntersect( VR_Vec3( 0, 0, 0 ), VR_Vec3( 0, 0, -1 ), center, facing, 2.0f, 1.5f );
	Check( hit.hit && Near( hit.u, 0.5f ) && Near( hit.v, 0.5f ) && Near( hit.distance, 2.0f ), "centre hit" );
	hit = VR_RayQuadIntersect( VR_Vec3( -0.5f, 0.375f, 0.0f ), VR_Vec3( 0, 0, -1 ), center, facing, 2.0f, 1.5f );
	Check( hit.hit && Near( hit.u, 0.25f ) && Near( hit.v, 0.25f ), "upper-left quadrant maps to small u and v" );
	hit = VR_RayQuadIntersect( VR_Vec3( 1.5f, 0.0f, 0.0f ), VR_Vec3( 0, 0, -1 ), center, facing, 2.0f, 1.5f );
	Check( !hit.hit, "outside the quad misses" );
	hit = VR_RayQuadIntersect( VR_Vec3( 0, 0, -4.0f ), VR_Vec3( 0, 0, 1 ), center, facing, 2.0f, 1.5f );
	Check( !hit.hit, "the back face does not take input" );
	hit = VR_RayQuadIntersect( VR_Vec3( 0, 0, 0 ), VR_Vec3( 0, 0, 1 ), center, facing, 2.0f, 1.5f );
	Check( !hit.hit, "pointing away misses" );
	// a quad turned 90 degrees to the right of the viewer
	const vrQuat_t turned = AxisAngle( VR_Vec3( 0, 1, 0 ), -90.0f );
	hit = VR_RayQuadIntersect( VR_Vec3( 0, 0, 0 ), VR_Vec3( 1, 0, 0 ), VR_Vec3( 3.0f, 0.0f, 0.0f ), turned, 2.0f, 1.5f );
	Check( hit.hit && Near( hit.u, 0.5f ) && Near( hit.v, 0.5f ) && Near( hit.distance, 3.0f ), "rotated quad centre hit" );
}

static void TestHeadsetServices( void ) {
	const float quest3[] = { 120.0f, 72.0f, 80.0f, 90.0f };	// a runtime may list them in any order
	Check( VR_ChooseRefreshRate( 0.0f, quest3, 4 ) == 0.0f, "0 keeps the headset's own rate" );
	Check( VR_ChooseRefreshRate( 90.0f, quest3, 4 ) == 90.0f, "an offered rate is taken exactly" );
	Check( VR_ChooseRefreshRate( 100.0f, quest3, 4 ) == 90.0f, "between rates, the fastest one below the choice" );
	Check( VR_ChooseRefreshRate( 144.0f, quest3, 4 ) == 120.0f, "above every rate, the fastest" );
	Check( VR_ChooseRefreshRate( 60.0f, quest3, 4 ) == 72.0f, "below every rate, the slowest" );
	const float drift[] = { 72.0f, 90.0004f };
	Check( VR_ChooseRefreshRate( 90.0f, drift, 2 ) == 90.0004f, "a runtime's 90.0004 is the 90 the player asked for" );
	const float broken[] = { -1.0f, std::nanf( "" ), 0.0f };
	Check( VR_ChooseRefreshRate( 90.0f, broken, 3 ) == 0.0f, "no usable rate asks for nothing" );
	Check( VR_ChooseRefreshRate( std::nanf( "" ), quest3, 4 ) == 0.0f, "a broken choice asks for nothing" );
	Check( VR_ChooseRefreshRate( 90.0f, quest3, 0 ) == 0.0f, "no offered rates ask for nothing" );

	Check( VR_PerformanceSettingsLevel( 0 ) == -1, "0 leaves the clocks to the runtime" );
	Check( VR_PerformanceSettingsLevel( 1 ) == 0, "1 is power savings" );
	Check( VR_PerformanceSettingsLevel( 2 ) == 25, "2 is sustained low" );
	Check( VR_PerformanceSettingsLevel( 3 ) == 50, "3 is sustained high" );
	Check( VR_PerformanceSettingsLevel( 4 ) == -1 && VR_PerformanceSettingsLevel( -2 ) == -1, "boost or nonsense is never held" );
}

static void TestBody( void ) {
	// an arm of 14 and 11 units reaching a hand 20 units ahead, elbow down
	const vrVec3_t shoulder = VR_Vec3( 0.0f, 0.0f, 0.0f );
	vrTwoBone_t arm = VR_SolveTwoBone( shoulder, VR_Vec3( 20.0f, 0.0f, 0.0f ), VR_Vec3( 0.0f, 0.0f, -1.0f ), 14.0f, 11.0f );
	Check( arm.reached, "a hand within reach is reached" );
	Check( Near( VR_Length( VR_Sub( arm.joint, shoulder ) ), 14.0f, 0.001f ), "the upper arm keeps its length" );
	Check( Near( VR_Length( VR_Sub( VR_Vec3( 20.0f, 0.0f, 0.0f ), arm.joint ) ), 11.0f, 0.001f ), "the forearm keeps its length" );
	Check( arm.joint.z < -1.0f && Near( arm.joint.y, 0.0f ), "the elbow bends towards the pole" );
	// the pole's part along the limb is ignored
	vrTwoBone_t skewed = VR_SolveTwoBone( shoulder, VR_Vec3( 20.0f, 0.0f, 0.0f ), VR_Vec3( 5.0f, 0.0f, -1.0f ), 14.0f, 11.0f );
	Check( NearVec( skewed.joint, arm.joint, 0.001f ), "only the pole's sideways part bends the limb" );
	// out of reach: the forearm stays whole at the hand and the upper arm stretches
	arm = VR_SolveTwoBone( shoulder, VR_Vec3( 40.0f, 0.0f, 0.0f ), VR_Vec3( 0.0f, 0.0f, -1.0f ), 14.0f, 11.0f );
	Check( !arm.reached && NearVec( arm.joint, VR_Vec3( 29.0f, 0.0f, 0.0f ), 0.001f ), "a hand out of reach keeps its forearm" );
	// too near to fold: the forearm still ends at the hand
	arm = VR_SolveTwoBone( shoulder, VR_Vec3( 1.0f, 0.0f, 0.0f ), VR_Vec3( 0.0f, 0.0f, -1.0f ), 14.0f, 11.0f );
	Check( !arm.reached && Near( VR_Length( VR_Sub( VR_Vec3( 1.0f, 0.0f, 0.0f ), arm.joint ) ), 11.0f, 0.01f ), "a hand at the shoulder keeps its forearm" );
	// a pole along the limb, and coinciding ends, still give a finite joint
	arm = VR_SolveTwoBone( shoulder, VR_Vec3( 0.0f, 0.0f, -20.0f ), VR_Vec3( 0.0f, 0.0f, -1.0f ), 14.0f, 11.0f );
	Check( VR_IsFinite( arm.joint ) && Near( VR_Length( VR_Sub( arm.joint, shoulder ) ), 14.0f, 0.001f ), "a pole along the limb still bends it" );
	arm = VR_SolveTwoBone( shoulder, shoulder, VR_Vec3( 0.0f, 0.0f, 0.0f ), 14.0f, 11.0f );
	Check( VR_IsFinite( arm.joint ) && !arm.reached, "coinciding ends give a finite joint" );

	// the torso leaves the neck free and follows beyond it
	Check( Near( VR_BodyYawFollow( 0.0f, 30.0f, 50.0f, 0.0f, 0.011f ), 0.0f ), "a glance leaves the torso alone" );
	Check( Near( VR_BodyYawFollow( 0.0f, 80.0f, 50.0f, 0.0f, 0.011f ), 30.0f ), "past the neck's turn the torso follows" );
	Check( Near( VR_BodyYawFollow( 0.0f, -80.0f, 50.0f, 0.0f, 0.011f ), -30.0f ), "to either side" );
	Check( Near( VR_BodyYawFollow( 170.0f, -170.0f, 50.0f, 0.0f, 0.011f ), 170.0f ), "across the wrap a glance is still a glance" );
	Check( Near( VR_BodyYawFollow( 0.0f, 30.0f, 50.0f, 300.0f, 0.05f ), 15.0f ), "walking lines the torso up at its rate" );
	Check( Near( VR_BodyYawFollow( 0.0f, 10.0f, 50.0f, 300.0f, 0.05f ), 10.0f ), "and stops on the head's heading" );
	Check( VR_BodyYawFollow( std::nanf( "" ), 10.0f, 50.0f, 0.0f, 0.0f ) == 0.0f, "a broken heading resets" );

	// level, the neck is behind and below the eyes; looking straight down it is behind and above
	const float upm = 39.37f;
	vrVec3_t neck = VR_NeckFromHead( VR_Vec3( 0.0f, 0.0f, 68.0f ), VR_Vec3( 1.0f, 0.0f, 0.0f ), VR_Vec3( 0.0f, 0.0f, 1.0f ), upm );
	Check( neck.x < -3.0f && neck.z < 68.0f - 2.9f, "the neck sits behind and below level eyes" );
	neck = VR_NeckFromHead( VR_Vec3( 0.0f, 0.0f, 68.0f ), VR_Vec3( 0.0f, 0.0f, -1.0f ), VR_Vec3( 1.0f, 0.0f, 0.0f ), upm );
	Check( neck.x < -2.9f && neck.z > 68.0f + 3.0f, "eyes looking straight down swing ahead of and below the neck" );

	Check( Near( VR_BodyScale( 68.0f, 77.5f ), 68.0f / 77.5f ), "the model shrinks to the game's eye height" );

	// the body stands a little back, and further back under a head that looks down at it
	Check( Near( VR_BodySetback( VR_Vec3( 1.0f, 0.0f, 0.0f ) ), VR_BODY_SETBACK_METRES ), "a level head keeps the body close" );
	Check( Near( VR_BodySetback( VR_Vec3( 0.0f, 0.0f, 1.0f ) ), VR_BODY_SETBACK_METRES ), "so does a head that looks up" );
	Check( Near( VR_BodySetback( VR_Vec3( 0.0f, 0.0f, -1.0f ) ), VR_BODY_SETBACK_DOWN_METRES ), "a head that looks down sees past its chest" );
	float lastSetback = VR_BODY_SETBACK_METRES;
	for ( int pitch = 0; pitch <= 90; pitch++ ) {
		const float setback = VR_BodySetback( VR_Vec3( std::cos( pitch * VR_DEG2RAD ), 0.0f, -std::sin( pitch * VR_DEG2RAD ) ) );
		Check( setback >= lastSetback && setback - lastSetback < 0.012f, "the body eases back as the head tips down, never jumping" );
		lastSetback = setback;
	}
	Check( Near( VR_BodySetback( VR_Vec3( 0.0f, 0.0f, std::nanf( "" ) ) ), VR_BODY_SETBACK_METRES ), "a broken axis keeps the body close" );

	// a heading holds through the vertical, where a forward axis alone has none
	Check( Near( VR_HeadingDegrees( VR_Vec3( 1.0f, 0.0f, 0.0f ), VR_Vec3( 0.0f, 0.0f, 1.0f ) ), 0.0f ), "a level head heads where it faces" );
	Check( Near( VR_HeadingDegrees( VR_Vec3( 0.0f, 1.0f, 0.0f ), VR_Vec3( 0.0f, 0.0f, 1.0f ) ), 90.0f ), "to the left is a quarter turn" );
	Check( Near( VR_HeadingDegrees( VR_Vec3( 0.0f, 1.0f, 0.0f ), VR_Vec3( 1.0f, 0.0f, 0.0f ) ), 90.0f ), "a rolled head still heads where it faces" );
	Check( Near( VR_HeadingDegrees( VR_Vec3( 0.0f, 0.0f, -1.0f ), VR_Vec3( 0.0f, 1.0f, 0.0f ) ), 90.0f ), "looking straight down, the heading is where the top of the head points" );
	Check( Near( VR_HeadingDegrees( VR_Vec3( 0.0f, 0.0f, 1.0f ), VR_Vec3( 0.0f, -1.0f, 0.0f ) ), 90.0f ), "looking straight up, where the chin points" );
	for ( int pitch = -90; pitch <= 90; pitch++ ) {
		// a head nodding from straight up, through level, to straight down
		const float c = std::cos( pitch * VR_DEG2RAD ), s = std::sin( pitch * VR_DEG2RAD );
		Check( Near( VR_HeadingDegrees( VR_Vec3( c, 0.0f, -s ), VR_Vec3( s, 0.0f, c ) ), 0.0f, 0.01f ), "a nod keeps its heading all the way up and down" );
	}
	Check( VR_HeadingDegrees( VR_Vec3( 0.0f, 0.0f, -1.0f ), VR_Vec3( 0.0f, 0.0f, 1.0f ) ) == 0.0f, "nonsense axes still give a heading" );
	Check( VR_BodyScale( 68.0f, 0.0f ) == 1.0f && VR_BodyScale( 10.0f, 77.5f ) == 0.6f && VR_BodyScale( 200.0f, 77.5f ) == 1.2f, "a nonsense height stays within bounds" );
}

static void TestAuditLeads( void ) {
	// room scale: a lean is free, a step walks, a long way catches up at a capped pace
	Check( VR_RoomScaleCatchUp( 3.0f, 4.0f, 3.3f ) == 0.0f, "a lean inside the free radius walks nothing" );
	Check( Near( VR_RoomScaleCatchUp( 6.0f, 4.0f, 3.3f ), 2.0f, 1e-5f ), "a short step walks what lies beyond the lean" );
	Check( Near( VR_RoomScaleCatchUp( 120.0f, 4.0f, 3.3f ), 3.3f, 1e-5f ), "metres walked while paused catch up a tic's step at a time" );
	Check( Near( VR_RoomScaleCatchUp( 120.0f, 4.0f, 0.0f ), 116.0f, 1e-4f ), "no cap walks the whole way" );
	float walked = 0.0f;
	int tics = 0;
	for ( float distance = 84.0f; distance > 4.0f && tics < 1000; tics++ ) {
		const float step = VR_RoomScaleCatchUp( distance, 4.0f, 3.3f );
		walked += step;
		distance -= step;
	}
	Check( Near( walked, 80.0f, 1e-3f ) && tics >= 25 && tics <= 26, "two metres behind, the body is back under the head in under half a second" );

	// the third-person model under a tracked aim
	Check( Near( VR_ModelYawFollow( 10.0f, 40.0f, 30.0f ), 40.0f, 1e-4f ), "below 55 degrees of pitch the model faces the aim" );
	Check( Near( VR_ModelYawFollow( 10.0f, -170.0f, 85.0f ), 10.0f, 1e-4f ), "aimed near straight down the model holds its heading" );
	Check( Near( VR_ModelYawFollow( 170.0f, -170.0f, -30.0f ), -170.0f, 1e-4f ), "the follow takes the short way round" );
	const float partial = VR_ModelYawFollow( 0.0f, 90.0f, 67.5f );
	Check( partial > 40.0f && partial < 50.0f, "between 55 and 80 degrees the model follows part of the way" );
	float model = 0.0f;
	for ( int tic = 0; tic < 600; tic++ ) {
		// a hand pointing at the floor, its yaw spinning with the roll
		model = VR_ModelYawFollow( model, static_cast<float>( ( tic * 37 ) % 360 ) - 180.0f, 88.0f );
	}
	Check( model == 0.0f, "a spinning yaw at the floor never turns the model" );

	// touchpads pressed by region
	Check( VR_TrackpadRegion( 0.1f, 0.2f, 0.5f ) == VR_PAD_CENTRE, "a press near the middle is the centre" );
	Check( VR_TrackpadRegion( 0.1f, 0.9f, 0.5f ) == VR_PAD_UP && VR_TrackpadRegion( -0.2f, -0.7f, 0.5f ) == VR_PAD_DOWN, "up and down" );
	Check( VR_TrackpadRegion( 0.8f, 0.3f, 0.5f ) == VR_PAD_RIGHT && VR_TrackpadRegion( -0.8f, -0.3f, 0.5f ) == VR_PAD_LEFT, "left and right" );
	Check( VR_TrackpadRegion( 0.1f, 0.2f, 0.0f ) == VR_PAD_UP && VR_TrackpadRegion( 0.0f, -0.05f, 0.0f ) == VR_PAD_DOWN, "with no centre every press has a side" );
	Check( VR_TrackpadRegion( std::nanf( "" ), 0.5f, 0.0f ) == VR_PAD_CENTRE, "a nonsense pad reads as the centre" );
}

int main() {
	TestBasis();
	TestOrientation();
	TestProjection();
	TestEyeImage();
	TestRotationBetween();
	TestTwoHandedAim();
	TestPhysicalCrouch();
	TestPlaySpace();
	TestRoomScaleWalkInput();
	TestComfortVignette();
	TestZoomScale();
	TestTurning();
	TestOffsets();
	TestQuad();
	TestHeadsetServices();
	TestBody();
	TestAuditLeads();
	std::printf( "VR math core: all checks passed\n" );
	return 0;
}
