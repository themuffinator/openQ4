// Copyright (C) 2026 DarkMatter Productions
// The wrist displays' decisions (src/game/VRWristCore.h, VR-09).
#include "../../../src/game/VRWristCore.h"
#include <cstdio>
#include <cstdlib>

static void Check( bool condition, const char *message ) {
	if ( !condition ) {
		std::fprintf( stderr, "VR wrist: %s\n", message );
		std::exit( 1 );
	}
}

static bool Near( float a, float b, float tolerance = 0.0001f ) {
	return std::fabs( a - b ) <= tolerance;
}

static void TestAmmo( void ) {
	// the blaster and the gauntlet: full, nothing to warn of
	vrWristAmmo_t ammo = VRWrist_Ammo( true, false, 0, 0, -1, 0 );
	Check( Near( ammo.fill, 1.0f ) && ammo.tone == VR_WRIST_TONE_NORMAL && ammo.label == VR_WRIST_LABEL_NONE, "infinite ammunition reads full" );
	// a machinegun's magazine of 40, full
	ammo = VRWrist_Ammo( false, true, 40, 40, 200, 300 );
	Check( Near( ammo.fill, 1.0f ) && ammo.tone == VR_WRIST_TONE_NORMAL, "a full magazine fills the bar" );
	// a quarter left warns
	ammo = VRWrist_Ammo( false, true, 10, 40, 170, 300 );
	Check( Near( ammo.fill, 0.25f ) && ammo.tone == VR_WRIST_TONE_WARN && ammo.label == VR_WRIST_LABEL_NONE, "a quarter of the magazine warns" );
	ammo = VRWrist_Ammo( false, true, 11, 40, 170, 300 );
	Check( ammo.tone == VR_WRIST_TONE_NORMAL, "more than a quarter does not" );
	// empty magazine, more carried: reload
	ammo = VRWrist_Ammo( false, true, 0, 40, 160, 300 );
	Check( Near( ammo.fill, 0.0f ) && ammo.tone == VR_WRIST_TONE_LOW && ammo.label == VR_WRIST_LABEL_RELOAD, "an empty magazine asks for a reload" );
	// nothing at all
	ammo = VRWrist_Ammo( false, true, 0, 40, 0, 300 );
	Check( ammo.tone == VR_WRIST_TONE_LOW && ammo.label == VR_WRIST_LABEL_NO_AMMO, "nothing left reads no ammo" );
	// without a magazine (or in multiplayer): by the maximum, warning at an eighth
	ammo = VRWrist_Ammo( false, false, 0, 0, 25, 50 );
	Check( Near( ammo.fill, 0.5f ) && ammo.tone == VR_WRIST_TONE_NORMAL, "a weapon without a magazine fills by its maximum" );
	ammo = VRWrist_Ammo( false, false, 0, 0, 6, 50 );
	Check( ammo.tone == VR_WRIST_TONE_WARN && ammo.label == VR_WRIST_LABEL_NONE, "an eighth of the maximum warns" );
	ammo = VRWrist_Ammo( false, false, 0, 0, 0, 50 );
	Check( ammo.label == VR_WRIST_LABEL_NO_AMMO, "an empty weapon without a magazine reads no ammo" );
	// more than the maximum (a pickup over it) never overfills
	ammo = VRWrist_Ammo( false, false, 0, 0, 80, 50 );
	Check( Near( ammo.fill, 1.0f ), "the bar never overfills" );
}

static void TestHealth( void ) {
	Check( VRWrist_LowHealth( 25, 100 ), "a quarter of health is low" );
	Check( !VRWrist_LowHealth( 26, 100 ), "more than a quarter is not" );
	Check( !VRWrist_LowHealth( 0, 100 ), "the dead are not low" );
	Check( VRWrist_LowHealth( 50, 200 ) && !VRWrist_LowHealth( 51, 200 ), "the quarter follows the maximum" );
}

static void TestNearFade( void ) {
	Check( Near( VRWrist_NearFade( 0.5f ), 1.0f ), "at arm's length the display is whole" );
	Check( Near( VRWrist_NearFade( 0.30f ), 1.0f ), "the fade starts at 30 cm" );
	Check( Near( VRWrist_NearFade( 0.18f ), 0.0f ) && Near( VRWrist_NearFade( 0.05f ), 0.0f ), "it is gone by 18 cm" );
	Check( Near( VRWrist_NearFade( 0.24f ), 0.5f ), "half way between" );
}

static void TestHitRing( void ) {
	const float quarter = 1.5707963f;
	Check( Near( VRWrist_HitAngle( 1.0f, 0.0f ), 0.0f ), "a hit from ahead shows at the top" );
	Check( Near( VRWrist_HitAngle( 0.0f, -1.0f ), quarter ), "a hit from the right shows at the right" );
	Check( Near( VRWrist_HitAngle( 0.0f, 1.0f ), -quarter ), "a hit from the left shows at the left" );
	Check( Near( std::fabs( VRWrist_HitAngle( -1.0f, 0.0f ) ), 2.0f * quarter ), "a hit from behind shows at the bottom" );
	Check( Near( VRWrist_HitAlpha( 0, 900, false ), 0.9f ), "a fresh hit shows strongly" );
	Check( Near( VRWrist_HitAlpha( 0, 900, true ), 0.4f ), "reduced flashing caps it at 0.40" );
	Check( VRWrist_HitAlpha( 450, 900, false ) < VRWrist_HitAlpha( 100, 900, false ), "it fades" );
	Check( Near( VRWrist_HitAlpha( 900, 900, false ), 0.0f ) && Near( VRWrist_HitAlpha( -5, 900, false ), 0.0f ), "and is gone after its time" );
}

int main() {
	TestAmmo();
	TestHealth();
	TestNearFade();
	TestHitRing();
	std::printf( "VR wrist core: all checks passed\n" );
	return 0;
}
