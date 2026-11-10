//----------------------------------------------------------------
// HitMarker.cpp
//
// openQ4 crosshair hit marker: four angled marks that bloom out of the
// crosshair and fade when a hit lands, in the manner of Quake Champions.
//----------------------------------------------------------------

#include "../idlib/precompiled.h"
#pragma hdrstop

#include "Game_local.h"
#include "../game/gamesys/HudPrimitives.h"

/*
===============================================================================

	Presentation constants.

	Sized in the 640x480 virtual screen against the default 32 unit crosshair,
	so the marks frame the crosshair rather than covering it.  The marker is
	deliberately smaller and shorter lived than a Quake Champions hit marker:
	Quake 4's crosshairs are larger, and a cue that has to survive a lightning
	gun at ten hits a second cannot afford to be loud.

===============================================================================
*/
static const float	HITMARKER_CENTER_X			= 320.0f;
static const float	HITMARKER_CENTER_Y			= 240.0f;

static const float	HITMARKER_BASE_GAP			= 4.6f;		// inner radius at rest
static const float	HITMARKER_BASE_SPREAD		= 3.4f;		// how far the marks travel out
static const float	HITMARKER_BASE_LENGTH		= 6.4f;
static const float	HITMARKER_BASE_WIDTH		= 1.7f;
static const float	HITMARKER_MIN_WIDTH			= 1.0f;		// never thinner than a hairline

// The marks thin and shorten as they fly out, which is what stops the bloom
// from reading as a growing box.
static const float	HITMARKER_LENGTH_TAPER		= 0.25f;
static const float	HITMARKER_WIDTH_TAPER		= 0.20f;

// A dark sheath under the marks, so the cue survives a bright wall or a white
// muzzle flash without having to be drawn brighter.
static const float	HITMARKER_OUTLINE_INFLATE	= 0.75f;
static const float	HITMARKER_OUTLINE_ALPHA		= 0.55f;

// Sustained fire re-arms the pulse before it has finished.  Restarting the
// bloom from part of the way out keeps it lively instead of strobing back to
// the centre on every hit.
static const float	HITMARKER_REARM_CARRY		= 0.45f;

// The hit-info message and the snapshot hit pulse describe the same hit and
// arrive a few frames apart.  The message knows more, so it suppresses the
// fallback for as long as one could plausibly belong to the same hit.
static const int	HITMARKER_PRECISE_WINDOW	= 250;

static const int	HITMARKER_LIGHT_DAMAGE		= 15;		// splash, a graze
static const int	HITMARKER_HEAVY_DAMAGE		= 50;		// rail, rocket, direct dark matter

// Team damage and self damage are acknowledged, not celebrated: they keep their
// own colour and sit a little quieter than a hit on an opponent.
static const float	HITMARKER_MISTAKE_ALPHA		= 0.8f;

// g_crosshairSize this presentation was tuned against
static const float	HITMARKER_REFERENCE_CROSSHAIR	= 32.0f;

static const float	HITMARKER_DIAGONAL			= 0.70710678f;

static const idVec4	colorHitMarker( 0.95f, 0.17f, 0.14f, 1.0f );		// a hit that landed on flesh
static const idVec4	colorHitMarkerKill( 1.00f, 0.88f, 0.82f, 1.0f );	// white hot: the victim is down
static const idVec4	colorHitMarkerTeam( 1.00f, 0.72f, 0.20f, 1.0f );	// a warning, not a reward
static const idVec4	colorHitMarkerSelf( 0.55f, 0.62f, 0.72f, 1.0f );	// own splash: acknowledge, do not celebrate
static const idVec4	colorHitMarkerOutline( 0.0f, 0.0f, 0.0f, 1.0f );

/*
================
The four marks point out of the crosshair along the diagonals, which keeps them
clear of Quake 4's crosshairs - every stock crosshair puts its detail on the
axes - and clear of the damage indicators, which arc around the same centre.
================
*/
static const idVec2 hitMarkerDirections[ 4 ] = {
	idVec2( -HITMARKER_DIAGONAL, -HITMARKER_DIAGONAL ),
	idVec2(  HITMARKER_DIAGONAL, -HITMARKER_DIAGONAL ),
	idVec2(  HITMARKER_DIAGONAL,  HITMARKER_DIAGONAL ),
	idVec2( -HITMARKER_DIAGONAL,  HITMARKER_DIAGONAL )
};

/*
===============================================================================

	Severity.

	Ordered weakest to strongest.  The order is the whole of the transition
	rule: a pulse already on screen keeps the strongest hit it has seen, so a
	splash landing behind a rail confirm cannot dull the confirm.

===============================================================================
*/
typedef enum {
	HITMARKER_TIER_LIGHT = 0,
	HITMARKER_TIER_NORMAL,
	HITMARKER_TIER_HEAVY,
	HITMARKER_TIER_KILL,
	HITMARKER_TIER_COUNT
} hitMarkerTier_t;

typedef struct hitMarkerTierStyle_s {
	int		duration;		// ms the pulse lives
	float	hold;			// share of that life held at full alpha
	float	alpha;
	float	gapScale;
	float	spreadScale;
	float	lengthScale;
	float	widthScale;
} hitMarkerTierStyle_t;

static const hitMarkerTierStyle_t hitMarkerTierStyles[ HITMARKER_TIER_COUNT ] = {
	{ 180, 0.20f, 0.62f, 0.90f, 0.80f, 0.85f, 0.85f },	// light:  a small, quiet tick
	{ 240, 0.28f, 0.85f, 1.00f, 1.00f, 1.00f, 1.00f },	// normal: the reference pulse
	{ 320, 0.34f, 1.00f, 1.05f, 1.35f, 1.20f, 1.15f },	// heavy:  wider, brighter, a beat longer
	{ 420, 0.38f, 1.00f, 1.10f, 1.70f, 1.35f, 1.25f }	// kill:   unmistakable, still under half a second
};

typedef struct hitMarkerPulse_s {
	int		startTime;		// 0 when nothing is on screen
	int		preciseTime;	// last precise trigger, for fallback de-duplication
	int		tier;
	int		flags;
	bool	precise;		// whether the live tier came from a precise trigger
	float	carry;			// expansion the pulse restarts from when re-armed
} hitMarkerPulse_t;

static hitMarkerPulse_t		hitMarkerPulse = { 0, 0, HITMARKER_TIER_NORMAL, 0, false, 0.0f };
static const idMaterial *	hitMarkerMaterial = NULL;

// openQ4: while the animated crosshair is drawing the weapon's design it owns
// this pulse and docks it into that design; see gamesys/AnimatedCrosshair.inl,
// included at the end of this file.
static bool AnimatedCrosshair_OwnsMarker( void );

/*
================
HitMarkerTierForHit
================
*/
static int HitMarkerTierForHit( int damage, int flags ) {
	if ( flags & HITMARKER_KILL ) {
		return HITMARKER_TIER_KILL;
	}
	if ( damage == HITMARKER_DAMAGE_UNKNOWN ) {
		// the server withheld the amount, or this came off the snapshot pulse
		return HITMARKER_TIER_NORMAL;
	}
	if ( damage >= HITMARKER_HEAVY_DAMAGE ) {
		return HITMARKER_TIER_HEAVY;
	}
	if ( damage <= HITMARKER_LIGHT_DAMAGE ) {
		return HITMARKER_TIER_LIGHT;
	}
	return HITMARKER_TIER_NORMAL;
}

/*
================
HitMarkerLifeFraction

Returns the share of the pulse's life that has elapsed, or -1 when there is
nothing to draw.
================
*/
static float HitMarkerLifeFraction( int now ) {
	if ( hitMarkerPulse.startTime == 0 ) {
		return -1.0f;
	}

	const hitMarkerTierStyle_t &style = hitMarkerTierStyles[ hitMarkerPulse.tier ];
	const int elapsed = now - hitMarkerPulse.startTime;

	// a negative elapsed means the clock went backwards under us: a map load,
	// a demo scrub, a rewound client time
	if ( elapsed < 0 || elapsed >= style.duration ) {
		return -1.0f;
	}

	return (float)elapsed / (float)style.duration;
}

/*
================
HitMarkerExpansion

Ease out cubic: nearly all of the travel happens in the first third, which is
what makes the marks read as a snap rather than a slide.
================
*/
static float HitMarkerExpansion( float fraction, float carry ) {
	const float remaining = 1.0f - fraction;
	const float eased = 1.0f - remaining * remaining * remaining;

	return carry + ( 1.0f - carry ) * eased;
}

/*
================
HitMarkerAlpha

Full for the hold, then a squared fade so the marker leaves promptly and still
arrives at zero with no visible step.
================
*/
static float HitMarkerAlpha( float fraction, float hold ) {
	if ( fraction <= hold ) {
		return 1.0f;
	}

	const float remaining = 1.0f - ( fraction - hold ) / ( 1.0f - hold );

	return idMath::ClampFloat( 0.0f, 1.0f, remaining * remaining );
}

/*
================
HitMarkerScale

The marker frames the crosshair, so it follows g_crosshairSize before the
player's own multiplier is applied.
================
*/
static float HitMarkerScale( void ) {
	const float crosshair = (float)idMath::ClampInt( 16, 48, cvarSystem->GetCVarInteger( "g_crosshairSize" ) );
	const float player = idMath::ClampFloat( 0.25f, 4.0f, hud_hitMarkerScale.GetFloat() );

	return player * ( crosshair / HITMARKER_REFERENCE_CROSSHAIR );
}

/*
================
HitMarkerAspect

The crosshair is drawn through the gui device context, which fits the 640x480
canvas into the window without stretching it when ui_aspectCorrection is set.
These draws go straight to the gui model instead, so the same correction has to
be applied by hand or the marks would sit wider than the crosshair they frame
and the diagonals would not be diagonal.

Yields the scale to apply to an offset from the screen centre.
================
*/
static void HitMarkerAspect( float &xScale, float &yScale ) {
	xScale = 1.0f;
	yScale = 1.0f;

	if ( !cvarSystem->GetCVarBool( "ui_aspectCorrection" ) ) {
		return;
	}

	// The device context prefers the ui viewport where the two differ; the game
	// only sees the video size, which is the same value in every stock setup.
	const float windowWidth = (float)renderSystem->GetScreenWidth();
	const float windowHeight = (float)renderSystem->GetScreenHeight();
	if ( windowWidth <= 0.0f || windowHeight <= 0.0f ) {
		return;
	}

	const float targetAspect = (float)SCREEN_WIDTH / (float)SCREEN_HEIGHT;
	const float windowAspect = windowWidth / windowHeight;

	if ( windowAspect >= targetAspect ) {
		xScale = targetAspect / windowAspect;
	} else {
		yScale = windowAspect / targetAspect;
	}
}

/*
================
HitMarkerColorForFlags
================
*/
static void HitMarkerColorForFlags( int flags, idVec4 &color ) {
	if ( flags & HITMARKER_SELF ) {
		color = colorHitMarkerSelf;
		return;
	}
	if ( flags & HITMARKER_TEAM ) {
		color = colorHitMarkerTeam;
		return;
	}
	if ( flags & HITMARKER_KILL ) {
		color = colorHitMarkerKill;
		return;
	}

	color = colorHitMarker;
	if ( flags & HITMARKER_ARMOR ) {
		// armour soaked part of it: lift the core towards white so the hit still
		// reads without claiming it landed on health
		color.Lerp( colorHitMarker, colorWhite, 0.35f );
		color.w = 1.0f;
	}
}

/*
================
HitMarkerDrawMark

One mark, as a quad rotated onto its radial direction.  Wound the way the gui
device context winds a quad, so a front sided material is not culled away.
================
*/
static void HitMarkerDrawMark( const idVec2 &direction, float radius, float halfLength, float halfWidth,
							   float xScale, float yScale, const idMaterial *material, const idVec4& color, rvHudPrimitives& presentation ) {
	const idVec2	perpendicular( -direction.y, direction.x );
	const idVec2	center = direction * ( radius + halfLength );
	const idVec2	along = direction * halfLength;
	const idVec2	across = perpendicular * halfWidth;
	idVec2			corners[ 4 ];
	int				i;

	corners[ 0 ] = center - along - across;
	corners[ 1 ] = center + along - across;
	corners[ 2 ] = center + along + across;
	corners[ 3 ] = center - along + across;

	for ( i = 0; i < 4; i++ ) {
		corners[ i ].x = HITMARKER_CENTER_X + corners[ i ].x * xScale;
		corners[ i ].y = HITMARKER_CENTER_Y + corners[ i ].y * yScale;
	}

	presentation.Quad(corners,idVec4(0,0,0,0),color,material);
}

/*
================
rvHitMarker::Clear
================
*/
void rvHitMarker::Clear( void ) {
	memset( &hitMarkerPulse, 0, sizeof( hitMarkerPulse ) );
	hitMarkerPulse.tier = HITMARKER_TIER_NORMAL;
	hitMarkerMaterial = NULL;
	rvAnimatedCrosshair::Clear();
}

/*
================
rvHitMarker::CrosshairFlashEnabled
================
*/
bool rvHitMarker::CrosshairFlashEnabled( void ) {
	// the animated crosshair is the combat picture, and answers the hit itself
	if ( AnimatedCrosshair_OwnsMarker() ) {
		return false;
	}
	// with the marker off there is no other cue on the crosshair, so stock
	// Quake 4's flash comes back on its own rather than leaving the player with
	// nothing
	return hud_crosshairHitFlash.GetBool() || !hud_hitMarker.GetBool();
}

/*
================
rvHitMarker::Trigger

Stages, or re-arms, the single pulse.  Callers have already decided that the hit
belongs to the player holding this screen.
================
*/
void rvHitMarker::Trigger( int damage, int flags, hitMarkerSource_t source ) {
	int		now;
	int		tier;
	float	fraction;

	if ( !hud_hitMarker.GetBool() && !rvAnimatedCrosshair::Enabled() ) {
		return;
	}

	// a zero start time is the free marker for the pulse, so never store one
	now = Max( 1, gameLocal.time );

	if ( source == HITMARKER_COARSE ) {
		if ( hitMarkerPulse.preciseTime != 0 &&
			 now - hitMarkerPulse.preciseTime < HITMARKER_PRECISE_WINDOW ) {
			// the hit-info pipeline is already covering this hit and knows more
			// about it than this trigger does
			return;
		}
	} else {
		hitMarkerPulse.preciseTime = now;
	}

	tier = HitMarkerTierForHit( damage, flags );
	fraction = HitMarkerLifeFraction( now );

	if ( fraction >= 0.0f ) {
		const float expansion = HitMarkerExpansion( fraction, hitMarkerPulse.carry );
		// A precise trigger describing a hit a fallback has already put on screen
		// - the listen server host gets both, in that order - is an upgrade of
		// that pulse rather than a second hit, so it replaces the guess outright.
		// Between two known hits the stronger one wins: a splash landing behind a
		// rail confirm must not dull the confirm.
		const bool replace = ( source == HITMARKER_PRECISE && !hitMarkerPulse.precise ) ||
							 tier >= hitMarkerPulse.tier;
		const int mistake = flags & ( HITMARKER_SELF | HITMARKER_TEAM );

		if ( replace ) {
			hitMarkerPulse.tier = tier;
			hitMarkerPulse.flags = flags;
			hitMarkerPulse.precise = ( source == HITMARKER_PRECISE );
		} else if ( mistake ) {
			// The colour lane is a category, not a severity.  Catching a team
			// mate or yourself while an opponent's pulse is still up has to stop
			// reading as a confirm, even when the confirm was the bigger hit, so
			// the lane changes while the timing keeps the stronger shape.
			hitMarkerPulse.flags = ( hitMarkerPulse.flags &
									 ~( HITMARKER_SELF | HITMARKER_TEAM ) ) | mistake;
		}
		hitMarkerPulse.carry = expansion * HITMARKER_REARM_CARRY;
	} else {
		hitMarkerPulse.tier = tier;
		hitMarkerPulse.flags = flags;
		hitMarkerPulse.precise = ( source == HITMARKER_PRECISE );
		hitMarkerPulse.carry = 0.0f;
	}

	hitMarkerPulse.startTime = now;
}

/*
================
rvHitMarker::Draw
================
*/
void rvHitMarker::Draw( void ) {
	float	fraction;
	float	expansion;
	float	scale;
	float	radius;
	float	halfLength;
	float	halfWidth;
	float	alpha;
	float	xScale;
	float	yScale;
	idVec4	color;
	idVec4	outline;
	int		pass;
	int		i;

	if ( AnimatedCrosshair_OwnsMarker() ) {
		// drawn already, docked in the design
		return;
	}

	if ( !hud_hitMarker.GetBool() ) {
		hitMarkerPulse.startTime = 0;
		return;
	}

	fraction = HitMarkerLifeFraction( gameLocal.time );
	if ( fraction < 0.0f ) {
		hitMarkerPulse.startTime = 0;
		return;
	}

	const hitMarkerTierStyle_t &style = hitMarkerTierStyles[ hitMarkerPulse.tier ];

	alpha = style.alpha * HitMarkerAlpha( fraction, style.hold );
	if ( hitMarkerPulse.flags & ( HITMARKER_SELF | HITMARKER_TEAM ) ) {
		alpha *= HITMARKER_MISTAKE_ALPHA;
	}
	if ( alpha <= 0.0f ) {
		return;
	}

	if ( hitMarkerMaterial == NULL ) {
		hitMarkerMaterial = declManager->FindMaterial( "_white" );
	}

	expansion = HitMarkerExpansion( fraction, hitMarkerPulse.carry );
	scale = HitMarkerScale();
	radius = ( HITMARKER_BASE_GAP * style.gapScale +
			   HITMARKER_BASE_SPREAD * style.spreadScale * expansion ) * scale;
	halfLength = 0.5f * HITMARKER_BASE_LENGTH * style.lengthScale *
				 ( 1.0f - HITMARKER_LENGTH_TAPER * expansion ) * scale;
	halfWidth = 0.5f * Max( HITMARKER_MIN_WIDTH,
							HITMARKER_BASE_WIDTH * style.widthScale *
							( 1.0f - HITMARKER_WIDTH_TAPER * expansion ) * scale );

	HitMarkerColorForFlags( hitMarkerPulse.flags, color );
	color.w = alpha;
	outline = colorHitMarkerOutline;
	outline.w = alpha * HITMARKER_OUTLINE_ALPHA;

	HitMarkerAspect( xScale, yScale );

	rvHudPrimitives presentation;
	// sheath first, then the coloured core over it
	for ( pass = 0; pass < 2; pass++ ) {
		const float inflate = ( pass == 0 ) ? HITMARKER_OUTLINE_INFLATE * scale : 0.0f;


		for ( i = 0; i < 4; i++ ) {
			HitMarkerDrawMark( hitMarkerDirections[ i ], radius - inflate,
							   halfLength + inflate, halfWidth + inflate,
							   xScale, yScale, hitMarkerMaterial, (pass == 0) ? outline : color, presentation );
		}
	}

	presentation.Draw("native/hit-marker");
}

#include "../game/gamesys/AnimatedCrosshair.inl"
