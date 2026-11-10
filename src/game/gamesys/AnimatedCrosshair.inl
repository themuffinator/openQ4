// Shared SP/MP gameplay presentation. Quake 4 SDK EULA; see src/game/LICENSE.
//
// openQ4 animated crosshair.  Included once per game tree at the end of
// HitMarker.cpp: the crosshair owns the hit marker's pulse while it is drawn,
// so it reads that pulse where it lives instead of keeping a second one.

/*
===============================================================================

	Units.

	Every design is laid out on the stock 64 texel master, centred on the aim
	point, y down.  The stock art puts its ink on the axes or the diagonals,
	so angles are compass bearings: 0 is up and they run clockwise.

===============================================================================
*/
static const float	XH_MASTER				= 64.0f;
static const float	XH_HALF					= 32.0f;

// deploy and stand-down
static const float	XH_ASSEMBLE_TIME		= 0.26f;	// seconds a raised weapon's design takes to assemble
static const float	XH_COLLAPSE_TIME		= 0.11f;	// stand-down for a context cursor or a lowered weapon
static const float	XH_SWAP_TIME			= 0.07f;	// the old design leaving on a weapon change

// the shot spring: stiff enough to be back before the next round of anything
// slower than the hyperblaster, loose enough to overshoot once
static const float	XH_KICK_FREQUENCY		= 30.0f;	// rad/s
static const float	XH_KICK_DAMPING			= 0.52f;
static const float	XH_KICK_IMPULSE			= 56.0f;	// velocity a shot adds: a kick of about 1 at its peak
static const float	XH_KICK_LIMIT			= 2.2f;

// only weapons that make the player wait show the wait
static const int	XH_CYCLE_MIN_INTERVAL	= 450;

static const float	XH_BUSY_SPEED			= 300.0f;	// degrees a second, reload sweep
static const float	XH_READY_FLASH_TIME		= 0.28f;
static const float	XH_STANDBY_DIM			= 0.45f;	// alpha lost while reloading or empty

// hit marks, in master units at the reference size: the marker's own numbers,
// which are in virtual units against a 32 unit crosshair
static const float	XH_HIT_UNIT				= XH_MASTER / HITMARKER_REFERENCE_CROSSHAIR;

static const idVec4	colorCrosshairAcquire( 0.97f, 0.75f, 0.0f, 1.0f );	// the stock nailgun guide ring
static const idVec4	colorCrosshairLocked( 0.90f, 0.10f, 0.05f, 1.0f );	// the stock lock guide

typedef enum {
	XH_NONE = 0,			// not animated: the cursor GUI keeps its picture
	XH_GENERIC,				// any other stock crosshair, as one piece
	XH_BLASTER,
	XH_MACHINEGUN,
	XH_SHOTGUN,
	XH_GRENADE,
	XH_NAILGUN,
	XH_ROCKET,
	XH_RAILGUN,
	XH_LIGHTNING,
	XH_NAPALM,
	XH_GAUNTLET,
	XH_CANNON,
	XH_DESIGN_COUNT
} xhDesign_t;

/*
================
Where each design keeps its clear space.  The hit marks dock on the diagonals
at hitDock and bloom outward from there, scaled so they stay inside a ring
that surrounds them; the gauge radius carries the reload sweep and the clip
pips; outer is where the design ends, for the kill ring and the lock corners.
================
*/
typedef struct xhStyle_s {
	float	hitDock;		// 0: the design's own pieces are the hit marks
	float	hitScale;
	float	gauge;
	float	outer;
	float	kick;			// how hard a shot lands on this design
} xhStyle_t;

static const xhStyle_t xhStyles[ XH_DESIGN_COUNT ] = {
	{  0.0f, 1.00f,  0.0f,  0.0f, 0.0f },	// none
	{ 17.0f, 0.90f, 20.0f, 16.0f, 1.0f },	// generic
	{ 14.5f, 1.00f, 16.5f, 13.0f, 1.0f },	// blaster: marks outside the arcs
	{ 14.5f, 0.80f, 16.0f, 19.0f, 0.8f },	// machinegun: marks between the ticks
	{  9.5f, 0.90f, 11.0f,  7.5f, 1.5f },	// shotgun
	{  5.5f, 0.62f, 13.5f, 22.0f, 1.2f },	// grenade launcher, dark matter gun: marks inside the ring
	{  5.0f, 0.60f, 11.5f, 30.0f, 0.5f },	// nailgun: marks between the brackets
	{  5.0f, 0.55f, 25.5f, 29.0f, 1.3f },	// rocket launcher: marks inside the ring, gauge on the halo
	{  8.5f, 0.85f, 20.0f, 17.5f, 1.4f },	// railgun: marks between the bars
	{  0.0f, 1.00f, 10.0f, 28.0f, 0.6f },	// lightning gun: the darts are the marks
	{  6.0f, 0.60f, 13.5f, 24.5f, 1.0f },	// napalm launcher
	{ 10.5f, 0.85f, 12.5f,  8.5f, 0.6f },	// gauntlet
	{  5.0f, 0.60f, 12.5f, 23.0f, 1.5f }	// vehicle cannon
};

/*
===============================================================================

	Canvas.

	Everything is a parallelogram handed to rvHudPrimitives.  The retained
	renderer rasterises each one with exact pixel coverage: a "_white" quad is
	an antialiased box at any angle, and a quad that names a crosshair
	material draws the window of that design's vector art its texture
	coordinates select, clipped to the quad.  That clip is what lets a piece
	of a stock design move on its own.

===============================================================================
*/
class xhCanvas {
public:
	void				Begin( float size, const idVec4 &tint );
	void				End( void ) { prims.Draw( "native/crosshair" ); }

	idVec4				Ink( float alpha ) const { return idVec4( tint.x, tint.y, tint.z, tint.w * alpha ); }
	idVec4				Shade( float alpha ) const { return idVec4( 0.0f, 0.0f, 0.0f, tint.w * alpha ); }
	static idVec2		Bearing( float degrees ) { const float a = DEG2RAD( degrees ); return idVec2( idMath::Sin( a ), -idMath::Cos( a ) ); }

	void				Box( const idVec2 &center, const idVec2 &along, float halfLength, float halfWidth, const idVec4 &color );
	void				Line( const idVec2 &from, const idVec2 &to, float width, const idVec4 &color );
	void				Spoke( float bearing, float inner, float outer, float width, const idVec4 &color );
	void				Arc( float radius, float thickness, float from, float to, const idVec4 &color );
	void				Disc( const idVec2 &center, float radius, const idVec4 &color );
	void				Art( const idMaterial *material, float x0, float y0, float x1, float y1,
							 float degrees, float scale, const idVec2 &offset, const idVec4 &color );

	float				pixel;			// master units to one physical pixel

private:
	idVec2				ToScreen( const idVec2 &point ) const {
		return idVec2( HITMARKER_CENTER_X + point.x * unit * xScale, HITMARKER_CENTER_Y + point.y * unit * yScale );
	}

	rvHudPrimitives		prims;
	idVec4				tint;
	float				unit;			// virtual units to one master unit
	float				xScale;
	float				yScale;
	const idMaterial *	white;
};

/*
================
xhCanvas::Begin
================
*/
void xhCanvas::Begin( float size, const idVec4 &crosshairColor ) {
	tint = crosshairColor;
	unit = size / XH_MASTER;
	HitMarkerAspect( xScale, yScale );

	const float screenHeight = Max( 1.0f, (float)renderSystem->GetScreenHeight() );
	pixel = (float)SCREEN_HEIGHT / ( screenHeight * unit * yScale );

	white = declManager->FindMaterial( "_white" );
}

/*
================
xhCanvas::Box

Never thinner than a physical pixel: the stock's one texel strokes fall below
that at the small sizes and shimmer away.
================
*/
void xhCanvas::Box( const idVec2 &center, const idVec2 &along, float halfLength, float halfWidth, const idVec4 &color ) {
	if ( color.w <= 0.004f || halfLength <= 0.0f ) {
		return;
	}
	halfWidth = Max( halfWidth, 0.5f * pixel );

	const idVec2	lengthways = along * halfLength;
	const idVec2	across = idVec2( -along.y, along.x ) * halfWidth;
	idVec2			corners[ 4 ];

	corners[ 0 ] = ToScreen( center - lengthways - across );
	corners[ 1 ] = ToScreen( center + lengthways - across );
	corners[ 2 ] = ToScreen( center + lengthways + across );
	corners[ 3 ] = ToScreen( center - lengthways + across );

	prims.Quad( corners, idVec4( 0.0f, 0.0f, 0.0f, 0.0f ), color, white );
}

/*
================
xhCanvas::Line
================
*/
void xhCanvas::Line( const idVec2 &from, const idVec2 &to, float width, const idVec4 &color ) {
	idVec2 along = to - from;
	const float length = along.Length();
	if ( length < 0.001f ) {
		return;
	}
	along *= 1.0f / length;
	Box( ( from + to ) * 0.5f, along, 0.5f * length, 0.5f * width, color );
}

/*
================
xhCanvas::Spoke

A radial stroke between two radii on a bearing.
================
*/
void xhCanvas::Spoke( float bearing, float inner, float outer, float width, const idVec4 &color ) {
	if ( outer <= inner ) {
		return;
	}
	const idVec2 direction = Bearing( bearing );
	Box( direction * ( 0.5f * ( inner + outer ) ), direction, 0.5f * ( outer - inner ), 0.5f * width, color );
}

/*
================
xhCanvas::Arc

A chain of boxes laid along the circle.  Each one is as long as the chord of
the outer edge, so the outside never shows a notch; the price is a sliver of
overlap on the inside, which only a translucent stroke can show, so those are
cut finer until the sliver is under half a pixel.
================
*/
void xhCanvas::Arc( float radius, float thickness, float from, float to, const idVec4 &color ) {
	const float sweep = to - from;
	if ( color.w <= 0.004f || sweep <= 0.05f || radius <= 0.0f ) {
		return;
	}

	const float radiusPixels = Max( 1.0f, radius / pixel );
	float step = RAD2DEG( 2.0f * idMath::ACos( 1.0f - 0.2f / radiusPixels ) );
	step = idMath::ClampFloat( 5.0f, 30.0f, step );
	if ( color.w < 0.98f ) {
		step = Min( step, 8.0f );
	}

	const int count = Max( 1, (int)idMath::Ceil( sweep / step ) );
	step = sweep / (float)count;

	const float halfLength = ( radius + 0.5f * thickness ) * idMath::Tan( DEG2RAD( 0.5f * step ) );
	for ( int i = 0; i < count; i++ ) {
		const float		middle = from + ( (float)i + 0.5f ) * step;
		const idVec2	direction = Bearing( middle );
		const idVec2	tangent( -direction.y, direction.x );

		Box( direction * radius, tangent, halfLength, 0.5f * thickness, color );
	}
}

/*
================
xhCanvas::Disc

Four boxes across the octagon's opposite edges, or two crossed squares once
it is too small for the difference to show.
================
*/
void xhCanvas::Disc( const idVec2 &center, float radius, const idVec4 &color ) {
	if ( radius <= 0.0f ) {
		return;
	}
	if ( radius < 2.5f * pixel ) {
		radius = Max( radius, 0.75f * pixel );
		Box( center, idVec2( 1.0f, 0.0f ), radius * 0.86f, radius * 0.86f, color );
		Box( center, idVec2( HITMARKER_DIAGONAL, HITMARKER_DIAGONAL ), radius * 0.86f, radius * 0.86f, color );
		return;
	}
	const float edge = radius * 0.41421356f;	// tan( 22.5 )
	for ( int i = 0; i < 4; i++ ) {
		Box( center, Bearing( 45.0f * (float)i ), radius, edge, color );
	}
}

/*
================
xhCanvas::Art

One window of a design's art, turned and scaled about the aim point and then
moved.  The window is in master units from the centre.
================
*/
void xhCanvas::Art( const idMaterial *material, float x0, float y0, float x1, float y1,
					float degrees, float scale, const idVec2 &offset, const idVec4 &color ) {
	if ( material == NULL || color.w <= 0.004f || scale <= 0.0f ) {
		return;
	}

	const float		angle = DEG2RAD( degrees );
	const float		c = idMath::Cos( angle ) * scale;
	const float		s = idMath::Sin( angle ) * scale;
	const idVec2	source[ 4 ] = { idVec2( x0, y0 ), idVec2( x1, y0 ), idVec2( x1, y1 ), idVec2( x0, y1 ) };
	idVec2			corners[ 4 ];

	for ( int i = 0; i < 4; i++ ) {
		corners[ i ] = ToScreen( idVec2( source[ i ].x * c - source[ i ].y * s, source[ i ].x * s + source[ i ].y * c ) + offset );
	}

	prims.Quad( corners, idVec4( ( x0 + XH_HALF ) / XH_MASTER, ( y0 + XH_HALF ) / XH_MASTER,
								 ( x1 + XH_HALF ) / XH_MASTER, ( y1 + XH_HALF ) / XH_MASTER ), color, material );
}

/*
===============================================================================

	State.

	One crosshair on screen, so one record.  The weapon's signals are levels;
	everything the eye follows is this record easing toward them, which is
	what turns a state change into a transition.

===============================================================================
*/
typedef struct xhState_s {
	int					lastTime;			// presentation clock of the last drawn frame
	xhDesign_t			design;				// the design on screen
	const idMaterial *	material;			// its stock art
	int					weaponKey;
	float				deploy;				// 0 folded away .. 1 assembled

	float				kick;				// the shot spring
	float				kickVelocity;
	int					lastNextAttack;
	int					shotTime;			// game time of the last shot
	int					shotInterval;		// how long that shot makes the weapon wait
	bool				cycling;
	float				readyFlash;

	float				fire;
	float				reload;
	float				empty;
	float				zoom;
	float				charge;
	float				lastCharge;			// as the weapon last reported it
	float				spin;
	float				lock;
	float				lockFlash;
	float				spinPhase;			// degrees
	float				busyPhase;			// degrees
	int					lastClip;
	float				clipPop;
	float				seconds;			// running clock for the few things that pulse
} xhState_t;

static xhState_t	xhState;
static bool			xhOwnsMarker = false;	// the crosshair drew the hit marker's pulse this frame
static bool			xhWasEnabled = false;
static bool			xhToggled = false;
static idDict		xhTest;					// testCrosshair overrides
static int			xhTestShot = 0;

/*
================
Easing
================
*/
static float XH_Approach( float value, float target, float step ) {
	if ( value < target ) {
		return Min( target, value + step );
	}
	return Max( target, value - step );
}

static float XH_EaseOut( float x ) {
	const float r = 1.0f - idMath::ClampFloat( 0.0f, 1.0f, x );
	return 1.0f - r * r * r;
}

static float XH_EaseInOut( float x ) {
	x = idMath::ClampFloat( 0.0f, 1.0f, x );
	return x * x * ( 3.0f - 2.0f * x );
}

// overshoots by about a tenth before it settles: the snap of a part seating
static float XH_EaseBack( float x ) {
	const float r = idMath::ClampFloat( 0.0f, 1.0f, x ) - 1.0f;
	return 1.0f + r * r * ( 2.70158f * r + 1.70158f );
}

static float XH_Lerp( float from, float to, float f ) {
	return from + ( to - from ) * f;
}

static idVec4 XH_Mix( const idVec4 &from, const idVec4 &to, float f ) {
	return idVec4( XH_Lerp( from.x, to.x, f ), XH_Lerp( from.y, to.y, f ), XH_Lerp( from.z, to.z, f ), XH_Lerp( from.w, to.w, f ) );
}

// a repeatable flicker for the lightning gun's darts: no state, no idRandom
static float XH_Flicker( int slot, int time ) {
	unsigned int h = (unsigned int)( time / 33 ) * 2654435761u + (unsigned int)slot * 40503u;
	h ^= h >> 13;
	h *= 2246822519u;
	h ^= h >> 16;
	return ( (float)( h & 0xffff ) / 32767.5f ) - 1.0f;
}

/*
================
XH_DesignForMaterial
================
*/
static xhDesign_t XH_DesignForMaterial( const char *name ) {
	static const struct {
		const char *	suffix;
		xhDesign_t		design;
	} known[] = {
		{ "crosshair_blaster",			XH_BLASTER },
		{ "crosshair_machinegun",		XH_MACHINEGUN },
		{ "crosshair_shotgun",			XH_SHOTGUN },
		{ "crosshair_grenadelauncher",	XH_GRENADE },
		{ "crosshair_nailgun",			XH_NAILGUN },
		{ "crosshair_rocketlauncher",	XH_ROCKET },
		{ "crosshair_railgun",			XH_RAILGUN },
		{ "crosshair_lightninggun",		XH_LIGHTNING },
		{ "crosshair_napalm",			XH_NAPALM },
		{ "crosshair_gauntlet",			XH_GAUNTLET },
		{ "vehicle_cannon",				XH_CANNON }
	};
	static const char *folder = "gfx/guis/crosshairs/";

	if ( name == NULL || idStr::Icmpn( name, folder, idStr::Length( folder ) ) != 0 ) {
		// a mod's own picture: the retained renderer has no vector art for it
		return XH_NONE;
	}

	idStr leaf = name + idStr::Length( folder );
	leaf.StripFileExtension();

	for ( int i = 0; i < (int)( sizeof( known ) / sizeof( known[ 0 ] ) ); i++ ) {
		if ( !leaf.Icmp( known[ i ].suffix ) ) {
			return known[ i ].design;
		}
	}
	// The Awakening's lock-on reticule changes its own art with the lock
	if ( !idStr::Icmpn( leaf.c_str(), "aa_space", 8 ) || !idStr::Icmpn( leaf.c_str(), "aa-space", 8 ) ) {
		return XH_NONE;
	}
	if ( !idStr::Icmpn( leaf.c_str(), "crosshair_", 10 ) ) {
		return XH_GENERIC;
	}
	return XH_NONE;
}

/*
===============================================================================

	The frame a design draws from: the eased state, the hit pulse and the
	colors, worked out once.

===============================================================================
*/
typedef struct xhFrame_s {
	float	open;			// deploy, linear
	float	seat;			// deploy with the overshoot of a part seating
	float	slide;			// deploy eased out, for travel
	float	alpha;			// overall
	float	kick;
	float	fire;
	float	reload;
	float	empty;
	float	standby;		// the larger of reload and empty
	float	zoom;
	float	charge;
	float	spin;
	float	spinPhase;
	float	lock;
	float	lockFlash;
	float	cycle;			// 1 ready .. 0 just fired, for weapons that make the player wait
	float	ready;			// flash as the wait ends
	float	punch;			// hit: a quick squeeze
	float	hitMix;			// hit: how far the ink goes to the hit color
	idVec4	hitColor;
	bool	hitMarks;		// hud_hitMarker: the docked marks as well as the reaction
	bool	kill;
	float	hitExpansion;
	float	hitAlpha;
	int		hitTier;
	float	motion;			// 1, or 0 under reduced motion
	float	pulse;			// 1, or 0 under reduced flashing or reduced motion
	int		time;			// game time
	float	seconds;
	int		shotAge;		// ms since the last shot
} xhFrame_t;

static idVec4 XH_Ink( const xhCanvas &canvas, const xhFrame_t &f, float alpha ) {
	idVec4 ink = canvas.Ink( alpha * f.alpha );
	if ( f.hitMix > 0.0f ) {
		const float a = ink.w;
		ink = XH_Mix( ink, f.hitColor, f.hitMix );
		ink.w = a;
	}
	return ink;
}

static idVec4 XH_Shade( const xhCanvas &canvas, const xhFrame_t &f, float alpha ) {
	return canvas.Shade( alpha * f.alpha );
}

/*
===============================================================================

	Designs.

===============================================================================
*/

/*
================
XH_DrawBlaster

Four arcs of a white rim over a dark band, a haloed dot.  The arcs are the
moving part: they sweep into place, jump apart on a shot, and wind shut into
an unbroken ring as the blaster charges.  The hyperblaster turns them with
its barrels.
================
*/
static void XH_DrawBlaster( xhCanvas &canvas, const xhFrame_t &f ) {
	const float charge = XH_EaseInOut( f.charge );
	const float halfGap = idMath::ClampFloat( 0.0f, 40.0f,
		4.8f * ( 1.0f - charge ) + 7.0f * Max( 0.0f, f.kick ) + 9.0f * f.standby );
	const float half = ( 45.0f - halfGap ) * f.slide;
	const float turn = -90.0f * ( 1.0f - f.slide ) + 90.0f * charge + f.spinPhase + 45.0f * XH_EaseInOut( f.standby );
	const float radius = 1.0f + 0.55f * ( 1.0f - f.slide ) + 0.14f * f.kick - 0.12f * charge
						 - 0.10f * f.punch + 0.06f * f.spin + 0.10f * f.standby;

	for ( int i = 0; i < 4; i++ ) {
		const float middle = 45.0f + 90.0f * (float)i + turn;

		canvas.Arc( 9.1f * radius, 3.2f, middle - half, middle + half, XH_Shade( canvas, f, 0.66f ) );
		canvas.Arc( 11.7f * radius, 2.0f, middle - half, middle + half, XH_Ink( canvas, f, 1.0f ) );
	}

	// held at full charge: one thin ring breathing outside the closed arcs
	if ( f.charge > 0.97f ) {
		const float breath = f.pulse * ( 0.5f + 0.5f * idMath::Sin( f.seconds * 12.0f ) );
		canvas.Arc( 13.9f + 0.7f * breath * f.motion, 0.9f, 0.0f, 360.0f, XH_Ink( canvas, f, 0.45f + 0.25f * breath ) );
	}

	canvas.Disc( vec2_origin, 3.0f * f.seat, XH_Shade( canvas, f, 0.45f ) );
	canvas.Disc( vec2_origin, 1.15f * f.seat, XH_Ink( canvas, f, 1.0f ) );
}

/*
================
XH_DrawMachinegun

A ring, four ticks crossing it and a dot.  The ring draws itself in; the
ticks are the bloom, walking out under sustained fire and back as it stops,
and drawing in to the ring when the weapon is sighted.  Out of ammunition
they turn onto the diagonals: an X where the cross was.
================
*/
static void XH_DrawMachinegun( xhCanvas &canvas, const xhFrame_t &f ) {
	const float radius = 1.0f + 0.05f * f.kick - 0.05f * f.punch - 0.16f * f.zoom + 0.06f * f.standby;
	const float sweep = 360.0f * f.slide;

	canvas.Arc( 9.25f * radius, 3.5f, 0.0f, sweep, XH_Shade( canvas, f, 0.79f ) );
	canvas.Arc( 12.0f * radius, 2.0f, 0.0f, sweep, XH_Ink( canvas, f, 1.0f ) );

	const float travel = 3.0f * f.kick + 2.4f * f.fire - 3.5f * f.zoom + 12.0f * ( 1.0f - f.slide );
	const float length = 12.0f * ( 1.0f - 0.45f * f.standby );
	const float turn = 45.0f * XH_EaseInOut( f.empty );

	for ( int i = 0; i < 4; i++ ) {
		canvas.Spoke( 90.0f * (float)i + turn, 7.0f + travel, 7.0f + travel + length, 2.0f, XH_Ink( canvas, f, f.open ) );
	}

	canvas.Disc( vec2_origin, 1.05f * f.seat, XH_Ink( canvas, f, 1.0f ) );
}

/*
================
XH_DrawShotgun

A small ring with a clear centre.  A shot throws it wide and sends a thin
echo out to the edge of the pattern; the pump brings it back.
================
*/
static void XH_DrawShotgun( xhCanvas &canvas, const xhFrame_t &f ) {
	const float radius = f.seat * ( 1.0f + 0.55f * f.kick + 0.12f * f.standby ) - 0.10f * f.punch;

	canvas.Arc( 4.0f * radius, 2.0f, 0.0f, 360.0f, XH_Shade( canvas, f, 0.80f ) );
	canvas.Arc( 6.0f * radius, 2.0f, 0.0f, 360.0f, XH_Ink( canvas, f, 1.0f ) );

	if ( f.shotAge >= 0 && f.shotAge < 380 && f.motion > 0.5f ) {
		const float age = (float)f.shotAge / 380.0f;
		canvas.Arc( 7.5f + 11.0f * XH_EaseOut( age ), 1.0f, 0.0f, 360.0f, XH_Ink( canvas, f, 0.5f * ( 1.0f - age ) * ( 1.0f - age ) ) );
	}
}

/*
================
XH_DrawRocket

A ring over a dark band, four outer ticks, a dot and a faint halo.  The halo
is the stock design's own, put to work: it sweeps round as the launcher
cycles and closes when the next rocket is ready.
================
*/
static void XH_DrawRocket( xhCanvas &canvas, const xhFrame_t &f ) {
	const float radius = 1.0f + 0.05f * f.kick - 0.04f * f.punch + 0.04f * f.standby;
	const float half = 180.0f * f.slide;

	canvas.Arc( 21.2f * radius, 0.9f, -half, half, XH_Shade( canvas, f, 0.45f ) );
	canvas.Arc( 17.0f * radius, 4.0f, -half, half, XH_Shade( canvas, f, 0.78f ) );
	canvas.Arc( 20.0f * radius, 2.0f, -half, half, XH_Ink( canvas, f, 1.0f ) );

	const float travel = 3.0f * f.kick + 10.0f * ( 1.0f - f.slide );
	const float turn = 45.0f * XH_EaseInOut( f.empty );
	for ( int i = 0; i < 4; i++ ) {
		canvas.Spoke( 90.0f * (float)i + turn, 21.0f + travel, 29.0f + travel, 2.0f, XH_Ink( canvas, f, f.open ) );
	}

	if ( f.reload < 0.02f ) {
		if ( f.cycle < 1.0f ) {
			canvas.Arc( 25.5f, 1.1f, 0.0f, 360.0f * f.cycle, XH_Ink( canvas, f, 0.55f ) );
		} else {
			canvas.Arc( 25.5f, 1.1f, 0.0f, 360.0f * f.slide, XH_Ink( canvas, f, 0.14f + 0.5f * f.ready ) );
		}
	}

	canvas.Disc( vec2_origin, 1.05f * f.seat, XH_Ink( canvas, f, 0.7f ) );
}

/*
================
XH_DrawRailgun

Four outlined bars and a small plus.  The shot throws the bars out and the
capacitor draws them back: they are home when the next slug is.
================
*/
static void XH_DrawRailgun( xhCanvas &canvas, const xhFrame_t &f ) {
	const float wait = 1.0f - f.cycle;
	const float travel = 7.0f * wait * idMath::Sqrt( wait ) + 2.5f * f.kick + 14.0f * ( 1.0f - f.slide )
						 - 1.5f * f.punch + 2.0f * f.standby;
	const float inner = 7.0f + travel;
	const float outer = inner + 10.0f + 2.0f * f.ready;
	const float turn = 45.0f * XH_EaseInOut( f.empty ) + 45.0f * XH_EaseInOut( f.zoom );
	const idVec4 ink = XH_Mix( XH_Ink( canvas, f, f.open ), idVec4( 1.0f, 1.0f, 1.0f, f.open * f.alpha ), 0.6f * f.ready );

	for ( int i = 0; i < 4; i++ ) {
		const float bearing = 90.0f * (float)i + turn;

		canvas.Spoke( bearing, 3.0f, inner, 1.0f, XH_Shade( canvas, f, 0.45f * f.open ) );
		canvas.Spoke( bearing, inner - 1.0f, outer + 1.0f, 4.0f, XH_Shade( canvas, f, 0.52f * f.open ) );
		canvas.Spoke( bearing, inner, outer, 2.0f, ink );
	}

	const float arm = 3.0f * f.seat;
	canvas.Box( vec2_origin, idVec2( 1.0f, 0.0f ), arm + 0.9f, 1.9f, XH_Shade( canvas, f, 0.5f ) );
	canvas.Box( vec2_origin, idVec2( 0.0f, 1.0f ), arm + 0.9f, 1.9f, XH_Shade( canvas, f, 0.5f ) );
	canvas.Box( vec2_origin, idVec2( 1.0f, 0.0f ), arm, 1.0f, XH_Ink( canvas, f, 1.0f ) );
	canvas.Box( vec2_origin, idVec2( 0.0f, 1.0f ), arm, 1.0f, XH_Ink( canvas, f, 1.0f ) );
}

/*
================
XH_DrawGauntlet

A dark disc with a bright core.  While the blade turns, three teeth turn
round it.
================
*/
static void XH_DrawGauntlet( xhCanvas &canvas, const xhFrame_t &f ) {
	canvas.Disc( vec2_origin, 8.0f * f.seat, XH_Shade( canvas, f, 0.9f ) );
	canvas.Disc( vec2_origin, 3.0f * f.seat * ( 1.0f + 0.35f * f.punch ), XH_Ink( canvas, f, 0.63f + 0.37f * f.hitMix ) );

	if ( f.spin > 0.01f ) {
		const float radius = 11.5f + 1.5f * f.punch;
		for ( int i = 0; i < 3; i++ ) {
			const float from = f.spinPhase + 120.0f * (float)i;
			canvas.Arc( radius, 1.6f, from, from + 62.0f, XH_Ink( canvas, f, 0.9f * f.spin ) );
		}
	}
}

/*
================
XH_DrawGrenade

The stock art in its four quarters: a quarter of the bevelled ring and a wing
each.  They close in from the corners, break open top from bottom on a shot
like a breech, and stand apart left from right while the next round cycles.
The dark matter gun wears the same design and draws it tight as it charges.
================
*/
static void XH_DrawGrenade( xhCanvas &canvas, const xhFrame_t &f, const idMaterial *material ) {
	const float wait = 1.0f - f.cycle;
	const float charge = XH_EaseInOut( f.charge );
	const float scale = 1.0f - 0.24f * charge - 0.05f * f.punch;
	const float apart = 3.2f * wait + 4.0f * f.reload + 1.5f * f.empty;
	const float breech = 2.6f * f.kick;
	const float away = 12.0f * ( 1.0f - f.slide );
	const float turn = 32.0f * ( 1.0f - f.slide ) + 45.0f * charge;
	const idVec4 ink = XH_Ink( canvas, f, f.open );

	for ( int i = 0; i < 4; i++ ) {
		const float sx = ( i & 1 ) ? 1.0f : -1.0f;
		const float sy = ( i & 2 ) ? 1.0f : -1.0f;
		const idVec2 offset( sx * ( apart + away * HITMARKER_DIAGONAL ), sy * ( breech + away * HITMARKER_DIAGONAL ) );

		canvas.Art( material, Min( 0.0f, sx * XH_HALF ), Min( 0.0f, sy * XH_HALF ), Max( 0.0f, sx * XH_HALF ), Max( 0.0f, sy * XH_HALF ),
					turn, scale, offset, ink );
	}
}

/*
================
XH_DrawNailgun

The stock art's two brackets.  They draw together as the drum comes up to
speed, tremble at full rate, and clamp shut on a seeker lock, where four
pips mark the lock between them.
================
*/
static void XH_DrawNailgun( xhCanvas &canvas, const xhFrame_t &f, const idMaterial *material ) {
	const float locked = XH_EaseInOut( idMath::ClampFloat( 0.0f, 1.0f, ( f.lock - 0.5f ) * 2.0f ) );
	const float seeking = ( f.lock > 0.02f && f.lock < 0.98f ) ? 0.8f * idMath::Sin( f.seconds * 18.0f ) * f.motion : 0.0f;
	const float tremble = ( f.spin > 0.6f ) ? 0.35f * ( f.spin - 0.6f ) * 2.5f * idMath::Sin( f.seconds * 190.0f ) * f.motion : 0.0f;
	// never closer than the hooks nearly touching
	const float apart = Max( -6.2f, 11.0f * ( 1.0f - f.slide ) - 4.5f * f.spin - 6.2f * locked + 1.2f * f.kick
									+ 5.0f * f.reload + 2.0f * f.empty - 1.2f * f.punch + seeking + tremble );

	idVec4 ink = XH_Ink( canvas, f, f.open );
	if ( f.lock > 0.02f ) {
		const float a = ink.w;
		ink = XH_Mix( ink, locked > 0.5f ? colorCrosshairLocked : colorCrosshairAcquire, Min( 1.0f, f.lock * 2.0f ) * 0.85f );
		ink.w = a;
	}

	canvas.Art( material, -XH_HALF, -XH_HALF, 0.0f, XH_HALF, 0.0f, 1.0f, idVec2( -apart, 0.0f ), ink );
	canvas.Art( material, 0.0f, -XH_HALF, XH_HALF, XH_HALF, 0.0f, 1.0f, idVec2( apart, 0.0f ), ink );

	if ( locked > 0.02f ) {
		idVec4 pip = colorCrosshairLocked;
		pip.w = f.alpha * locked;
		const float reach = 6.0f + 5.0f * ( 1.0f - locked ) + 2.5f * f.lockFlash;
		for ( int i = 0; i < 4; i++ ) {
			canvas.Spoke( 45.0f + 90.0f * (float)i, reach - 0.6f, reach + 3.6f, 2.6f, canvas.Shade( 0.5f * pip.w ) );
			canvas.Spoke( 45.0f + 90.0f * (float)i, reach, reach + 3.0f, 1.4f, pip );
		}
	}
}

/*
================
XH_DrawLightning

The stock art's four darts, which already sit where a hit marker would.  So
they are the hit marker: the beam draws them in and sets them crackling, and
every hit drives them home in the hit's color.
================
*/
static void XH_DrawLightning( xhCanvas &canvas, const xhFrame_t &f, const idMaterial *material ) {
	const float wait = 1.0f - f.cycle;
	const float travel = 15.0f * ( 1.0f - f.slide ) - 3.5f * f.fire - 5.0f * f.punch + 9.0f * wait * idMath::Sqrt( wait )
						 + 2.0f * f.kick + 4.0f * f.standby;
	idVec4 ink = canvas.Ink( f.alpha * f.open );
	const float mix = Max( f.hitMix, 0.0f );
	if ( mix > 0.0f ) {
		const float a = ink.w;
		ink = XH_Mix( ink, f.hitColor, Min( 1.0f, mix * 1.15f ) );
		ink.w = a;
	}

	for ( int i = 0; i < 4; i++ ) {
		const float sx = ( i & 1 ) ? 1.0f : -1.0f;
		const float sy = ( i & 2 ) ? 1.0f : -1.0f;
		const float crackle = 0.55f * f.fire * f.motion * XH_Flicker( i, f.time );
		const float distance = ( travel + crackle ) * HITMARKER_DIAGONAL;

		canvas.Art( material, Min( 0.0f, sx * XH_HALF ), Min( 0.0f, sy * XH_HALF ), Max( 0.0f, sx * XH_HALF ), Max( 0.0f, sy * XH_HALF ),
					0.0f, 1.0f, idVec2( sx * distance, sy * distance ), ink );
	}
}

/*
================
XH_DrawCannon

openQ4's heavy shell reticle: four arcs with ticks in their gaps, a dot and
two drop chevrons.  The arcs turn into place and spread on the shot, and the
chevrons fall with the shell and climb back as the breech reloads.
================
*/
static void XH_DrawCannon( xhCanvas &canvas, const xhFrame_t &f, const idMaterial *material ) {
	const float wait = 1.0f - f.cycle;
	const idVec4 ink = XH_Ink( canvas, f, f.open );
	const float spread = ( 2.2f * f.kick + 3.0f * wait * idMath::Sqrt( wait ) + 2.0f * f.standby ) * HITMARKER_DIAGONAL;
	const float turn = 45.0f * ( 1.0f - f.slide );
	const float tick = 8.0f * ( 1.0f - f.slide ) + 3.0f * f.kick;

	for ( int i = 0; i < 4; i++ ) {
		const float sx = ( i & 1 ) ? 1.0f : -1.0f;
		const float sy = ( i & 2 ) ? 1.0f : -1.0f;
		const float lo = 1.3f, hi = 24.0f;

		canvas.Art( material, sx > 0.0f ? lo : -hi, sy > 0.0f ? lo : -hi, sx > 0.0f ? hi : -lo, sy > 0.0f ? hi : -lo,
					turn, 1.0f - 0.04f * f.punch, idVec2( sx * spread, sy * spread ), ink );
	}

	canvas.Art( material, -1.3f, -24.0f, 1.3f, -15.0f, 0.0f, 1.0f, idVec2( 0.0f, -tick ), ink );
	canvas.Art( material, -1.3f, 15.0f, 1.3f, 24.0f, 0.0f, 1.0f, idVec2( 0.0f, tick ), ink );
	canvas.Art( material, -24.0f, -1.3f, -15.0f, 1.3f, 0.0f, 1.0f, idVec2( -tick, 0.0f ), ink );
	canvas.Art( material, 15.0f, -1.3f, 24.0f, 1.3f, 0.0f, 1.0f, idVec2( tick, 0.0f ), ink );

	// the dot stays on the aim point; the chevrons below it carry the drop
	canvas.Art( material, -3.0f, -3.0f, 3.0f, 3.0f, 0.0f, 1.0f, vec2_origin, ink );
	canvas.Art( material, -6.0f, 3.2f, 6.0f, 14.0f, 0.0f, 1.0f, idVec2( 0.0f, 4.0f * f.kick + 5.0f * wait ),
				XH_Ink( canvas, f, f.open * ( 1.0f - 0.6f * wait ) ) );
}

/*
================
XH_DrawWhole

A design drawn as the single piece it is: the napalm launcher's splatter and
the simple marks of the custom set.  It turns and settles into place, and
swells with the shot the way the stock crosshair swells with a hit.
================
*/
static void XH_DrawWhole( xhCanvas &canvas, const xhFrame_t &f, const idMaterial *material, bool turns ) {
	const float scale = ( 1.0f + 0.4f * ( 1.0f - f.slide ) ) * ( 1.0f + 0.10f * f.kick - 0.06f * f.punch + 0.06f * f.standby );
	const float turn = turns ? ( -60.0f * ( 1.0f - f.slide ) + 9.0f * f.kick ) : 0.0f;

	canvas.Art( material, -XH_HALF, -XH_HALF, XH_HALF, XH_HALF, turn, scale, vec2_origin, XH_Ink( canvas, f, f.open ) );
}

/*
===============================================================================

	Overlays every design shares.

===============================================================================
*/

/*
================
XH_DrawGauge

Reloading.  A weapon fed a round at a time shows the rounds: one pip each
round the clip holds, lit as they go in.  Anything else shows a sweep, a
measured one where the weapon knows how far along it is.
================
*/
static void XH_DrawGauge( xhCanvas &canvas, const xhFrame_t &f, const xhStyle_t &style, const crosshairSignals_t &signals ) {
	if ( f.reload <= 0.02f || style.gauge <= 0.0f ) {
		return;
	}

	const float alpha = f.reload * f.open;
	const idVec4 lit = canvas.Ink( 0.95f * alpha );
	const idVec4 dark = canvas.Shade( 0.5f * alpha );

	if ( signals.reloadProgress >= 0.0f ) {
		const float sweep = 360.0f * idMath::ClampFloat( 0.0f, 1.0f, signals.reloadProgress );
		canvas.Arc( style.gauge, 2.4f, 0.0f, sweep, dark );
		canvas.Arc( style.gauge, 1.2f, 0.0f, sweep, lit );
		return;
	}

	if ( signals.clipSize >= 2 && signals.clipSize <= 12 && signals.clip >= 0 ) {
		for ( int i = 0; i < signals.clipSize; i++ ) {
			const float bearing = 360.0f * ( (float)i + 0.5f ) / (float)signals.clipSize;
			const bool loaded = i < signals.clip;
			const float pop = ( i == signals.clip - 1 ) ? 1.6f * xhState.clipPop : 0.0f;

			canvas.Spoke( bearing, style.gauge - 1.7f, style.gauge + 1.7f + pop, 2.8f, dark );
			canvas.Spoke( bearing, style.gauge - 1.1f, style.gauge + 1.1f + pop, 1.6f, loaded ? lit : canvas.Ink( 0.3f * alpha ) );
		}
		return;
	}

	canvas.Arc( style.gauge, 2.4f, xhState.busyPhase, xhState.busyPhase + 110.0f, dark );
	canvas.Arc( style.gauge, 1.2f, xhState.busyPhase, xhState.busyPhase + 110.0f, lit );
	canvas.Arc( style.gauge, 2.4f, xhState.busyPhase + 180.0f, xhState.busyPhase + 290.0f, dark );
	canvas.Arc( style.gauge, 1.2f, xhState.busyPhase + 180.0f, xhState.busyPhase + 290.0f, lit );
}

/*
================
XH_DrawLock

A vehicle gun with a target: four corners close on the design.  The nailgun
shows its lock in its own brackets.
================
*/
static void XH_DrawLock( xhCanvas &canvas, const xhFrame_t &f, const xhStyle_t &style ) {
	if ( f.lock <= 0.02f ) {
		return;
	}

	const float locked = XH_EaseOut( f.lock );
	const float reach = style.outer + 3.0f + 7.0f * ( 1.0f - locked ) + 2.0f * f.lockFlash;
	idVec4 ink = ( f.lock > 0.98f ) ? colorCrosshairLocked : colorCrosshairAcquire;
	ink.w = f.alpha * f.open * Min( 1.0f, f.lock * 2.0f );

	for ( int i = 0; i < 4; i++ ) {
		const idVec2	corner = xhCanvas::Bearing( 45.0f + 90.0f * (float)i ) * reach;
		const idVec2	horizontal( corner.x > 0.0f ? -1.0f : 1.0f, 0.0f );
		const idVec2	vertical( 0.0f, corner.y > 0.0f ? -1.0f : 1.0f );

		canvas.Line( corner, corner + horizontal * 4.5f, 1.4f, ink );
		canvas.Line( corner, corner + vertical * 4.5f, 1.4f, ink );
	}
}

/*
================
XH_DrawHit

The hit marker's pulse, docked.  Same tiers, colors and timing as the marker
it replaces, but placed in the clear space of the design in hand, so it reads
as a part of the crosshair that was waiting there.  A kill also sends one thin
ring out from the design's edge.
================
*/
static void XH_DrawHit( xhCanvas &canvas, const xhFrame_t &f, const xhStyle_t &style ) {
	if ( f.hitAlpha <= 0.0f ) {
		return;
	}

	idVec4 color = f.hitColor;
	color.w = f.hitAlpha * f.open;

	if ( f.kill ) {
		const float reach = style.outer + 2.0f + 9.0f * f.hitExpansion;
		canvas.Arc( reach, 1.3f * ( 1.0f - 0.5f * f.hitExpansion ), 0.0f, 360.0f,
					idVec4( color.x, color.y, color.z, color.w * 0.8f ) );
	}

	if ( !f.hitMarks || style.hitDock <= 0.0f ) {
		return;
	}

	const hitMarkerTierStyle_t &tier = hitMarkerTierStyles[ f.hitTier ];
	const float size = style.hitScale * XH_HIT_UNIT * idMath::ClampFloat( 0.25f, 4.0f, hud_hitMarkerScale.GetFloat() );
	const float inner = style.hitDock + HITMARKER_BASE_SPREAD * tier.spreadScale * f.hitExpansion * size;
	const float length = HITMARKER_BASE_LENGTH * tier.lengthScale * ( 1.0f - HITMARKER_LENGTH_TAPER * f.hitExpansion ) * size;
	const float width = HITMARKER_BASE_WIDTH * tier.widthScale * ( 1.0f - HITMARKER_WIDTH_TAPER * f.hitExpansion ) * size;
	const float sheath = HITMARKER_OUTLINE_INFLATE * XH_HIT_UNIT * style.hitScale;
	const idVec4 outline( 0.0f, 0.0f, 0.0f, color.w * HITMARKER_OUTLINE_ALPHA );

	for ( int i = 0; i < 4; i++ ) {
		const float bearing = 45.0f + 90.0f * (float)i;

		canvas.Spoke( bearing, inner - sheath, inner + length + sheath, width + 2.0f * sheath, outline );
		canvas.Spoke( bearing, inner, inner + length, width, color );
	}
}

/*
===============================================================================

	Update.

===============================================================================
*/

/*
================
XH_ReadSignals

On foot the weapon in hand; in a vehicle the gun of the seat.  testCrosshair
overrides are laid over whatever the weapon reports.
================
*/
static void XH_ReadSignals( const rvWeapon *weapon, const rvVehicleWeapon *vehicleWeapon, crosshairSignals_t &signals ) {
	signals.Clear();
	if ( vehicleWeapon != NULL ) {
		vehicleWeapon->GetCrosshairSignals( signals );
	} else if ( weapon != NULL ) {
		weapon->GetCrosshairSignals( signals );
	}

	if ( xhTest.GetNumKeyVals() == 0 ) {
		return;
	}
	if ( xhTest.FindKey( "design" ) ) {
		signals.design = xhTest.GetString( "design" );
		signals.weaponKey = idStr::Hash( signals.design );
	}
	signals.raised = xhTest.GetBool( "raised", signals.raised ? "1" : "0" );
	signals.firing = xhTest.GetBool( "fire", signals.firing ? "1" : "0" );
	signals.reloading = xhTest.GetBool( "reload", signals.reloading ? "1" : "0" );
	signals.empty = xhTest.GetBool( "empty", signals.empty ? "1" : "0" );
	signals.zoomed = xhTest.GetBool( "zoom", signals.zoomed ? "1" : "0" );
	signals.clip = xhTest.GetInt( "clip", va( "%d", signals.clip ) );
	signals.clipSize = xhTest.GetInt( "clipsize", va( "%d", signals.clipSize ) );
	signals.reloadProgress = xhTest.GetFloat( "progress", va( "%f", signals.reloadProgress ) );
	signals.charge = xhTest.GetFloat( "charge", va( "%f", signals.charge ) );
	signals.spin = xhTest.GetFloat( "spin", va( "%f", signals.spin ) );
	signals.lock = xhTest.GetFloat( "lock", va( "%f", signals.lock ) );
	if ( xhTestShot != 0 ) {
		signals.nextAttackTime = xhTestShot;
	}
}

/*
================
XH_Advance

Eases the record toward the signals by one drawn frame.
================
*/
static void XH_Advance( const crosshairSignals_t &signals, bool shown, xhDesign_t wanted, const idMaterial *material, bool calm, bool steady ) {
	xhState_t &st = xhState;

	const int now = gameLocal.GetPresentationTimeMsec();
	float dt = ( st.lastTime != 0 ) ? (float)( now - st.lastTime ) * 0.001f : 0.0f;
	if ( dt < 0.0f || dt > 0.25f ) {
		// a map load, a demo scrub, a long hitch: start again from where we are
		dt = 0.0f;
	}
	st.lastTime = now;
	st.seconds += dt;

	// deploy.  A different weapon folds the old design away before the new
	// one is let on screen, so two designs are never drawn across each other.
	const bool swapped = ( wanted != st.design || signals.weaponKey != st.weaponKey || material != st.material );
	if ( swapped ) {
		st.deploy = ( st.design == XH_NONE ) ? 0.0f : XH_Approach( st.deploy, 0.0f, dt / XH_SWAP_TIME );
		if ( st.deploy <= 0.0f ) {
			const float seconds = st.seconds;
			const int lastTime = st.lastTime;

			memset( &st, 0, sizeof( st ) );
			st.seconds = seconds;
			st.lastTime = lastTime;
			st.design = wanted;
			st.material = material;
			st.weaponKey = signals.weaponKey;
			st.lastNextAttack = signals.nextAttackTime;
			st.lastClip = signals.clip;
		}
	} else if ( shown && signals.raised ) {
		st.deploy = XH_Approach( st.deploy, 1.0f, dt / XH_ASSEMBLE_TIME );
	} else {
		st.deploy = XH_Approach( st.deploy, 0.0f, dt / XH_COLLAPSE_TIME );
	}

	// shots.  Every weapon steps the time of its next attack forward when it
	// fires and never otherwise while it is raised, so the step is the shot,
	// and its size is how long the weapon will make the player wait.
	if ( signals.nextAttackTime > st.lastNextAttack && signals.nextAttackTime > gameLocal.time &&
		 signals.raised && !signals.reloading && !swapped ) {
		st.shotTime = gameLocal.time;
		st.shotInterval = signals.nextAttackTime - gameLocal.time;
		st.cycling = ( st.shotInterval >= XH_CYCLE_MIN_INTERVAL );
		st.kickVelocity += XH_KICK_IMPULSE * xhStyles[ st.design ].kick * ( calm ? 0.3f : 1.0f );
	}
	st.lastNextAttack = signals.nextAttackTime;

	if ( st.cycling && gameLocal.time >= signals.nextAttackTime ) {
		st.cycling = false;
		st.readyFlash = steady ? 0.0f : 1.0f;
	}
	if ( gameLocal.time < st.shotTime ) {
		// the clock went back under us
		st.shotTime = 0;
		st.cycling = false;
	}

	// the shot spring, stepped finely enough to stay stable through a hitch
	for ( float left = dt; left > 0.0f; left -= 0.004f ) {
		const float h = Min( left, 0.004f );
		st.kickVelocity += ( -XH_KICK_FREQUENCY * XH_KICK_FREQUENCY * st.kick
							 - 2.0f * XH_KICK_DAMPING * XH_KICK_FREQUENCY * st.kickVelocity ) * h;
		st.kick += st.kickVelocity * h;
	}
	st.kick = idMath::ClampFloat( -0.5f, XH_KICK_LIMIT, st.kick );

	st.fire = XH_Approach( st.fire, signals.firing ? 1.0f : 0.0f, dt / ( signals.firing ? 0.08f : 0.25f ) );
	st.empty = XH_Approach( st.empty, signals.empty ? 1.0f : 0.0f, dt / 0.2f );
	st.zoom = XH_Approach( st.zoom, signals.zoomed ? 1.0f : 0.0f, dt / 0.15f );
	// the gauntlet's blade turns for as long as the trigger is held
	const float spin = ( st.design == XH_GAUNTLET ) ? ( signals.firing ? 1.0f : 0.0f ) : signals.spin;
	st.spin = XH_Approach( st.spin, idMath::ClampFloat( 0.0f, 1.0f, spin ), dt / 0.12f );

	const float wasReloading = st.reload;
	st.reload = XH_Approach( st.reload, signals.reloading ? 1.0f : 0.0f, dt / ( signals.reloading ? 0.15f : 0.2f ) );
	if ( !signals.reloading && wasReloading >= 1.0f && !signals.empty && !steady ) {
		st.readyFlash = 1.0f;
	}
	st.readyFlash = XH_Approach( st.readyFlash, 0.0f, dt / XH_READY_FLASH_TIME );
	st.busyPhase = idMath::AngleNormalize360( st.busyPhase + XH_BUSY_SPEED * dt * ( calm ? 0.4f : 1.0f ) );

	if ( signals.clip != st.lastClip ) {
		if ( signals.clip > st.lastClip && signals.reloading ) {
			st.clipPop = 1.0f;
		}
		st.lastClip = signals.clip;
	}
	st.clipPop = XH_Approach( st.clipPop, 0.0f, dt / 0.18f );

	// A charge builds as fast as the weapon builds it and lets go at once.  A
	// held charge that is gone was fired: the weapon stepped its attack time
	// when the trigger went down, so the release is the only sign of the shot.
	const float charge = Max( 0.0f, signals.charge );
	if ( st.lastCharge > 0.2f && charge <= 0.0f && signals.raised && !swapped ) {
		st.shotTime = gameLocal.time;
		st.kickVelocity += XH_KICK_IMPULSE * xhStyles[ st.design ].kick * ( 1.0f + st.lastCharge ) * ( calm ? 0.3f : 1.0f );
	}
	st.lastCharge = charge;
	st.charge = XH_Approach( st.charge, idMath::ClampFloat( 0.0f, 1.0f, charge ), dt / ( charge > st.charge ? 0.05f : 0.14f ) );

	const float wasLocked = st.lock;
	st.lock = XH_Approach( st.lock, idMath::ClampFloat( 0.0f, 1.0f, signals.lock ), dt / 0.12f );
	if ( st.lock >= 1.0f && wasLocked < 1.0f && !steady ) {
		st.lockFlash = 1.0f;
	}
	st.lockFlash = XH_Approach( st.lockFlash, 0.0f, dt / 0.22f );

	// Whatever turns keeps turning while its weapon does, then coasts on to
	// the next place the design looks the same, so it never snaps back.
	const float symmetry = ( st.design == XH_GAUNTLET ) ? 120.0f : 90.0f;
	const float speed = ( st.design == XH_GAUNTLET ) ? 620.0f : 300.0f;
	if ( calm ) {
		st.spinPhase = 0.0f;
	} else if ( st.spin > 0.02f ) {
		st.spinPhase = fmodf( st.spinPhase + speed * st.spin * dt, symmetry );
	} else if ( st.spinPhase > 0.0f ) {
		st.spinPhase += Max( 40.0f, speed * 0.25f ) * dt;
		if ( st.spinPhase >= symmetry ) {
			st.spinPhase = 0.0f;
		}
	}
}

/*
================
XH_BuildFrame
================
*/
static void XH_BuildFrame( xhFrame_t &f, bool calm, bool steady ) {
	const xhState_t &st = xhState;

	memset( &f, 0, sizeof( f ) );
	f.motion = calm ? 0.0f : 1.0f;
	f.pulse = steady ? 0.0f : 1.0f;
	f.open = st.deploy;
	// reduced motion: the design fades in where it belongs instead of flying there
	f.slide = calm ? 1.0f : XH_EaseOut( st.deploy );
	f.seat = calm ? 1.0f : XH_EaseBack( st.deploy );
	f.kick = st.kick;
	f.fire = st.fire;
	f.reload = st.reload;
	f.empty = st.empty;
	f.standby = Max( st.reload, st.empty );
	f.zoom = st.zoom;
	f.charge = st.charge;
	f.spin = st.spin;
	f.spinPhase = st.spinPhase;
	f.lock = st.lock;
	f.lockFlash = st.lockFlash;
	f.ready = st.readyFlash;
	f.time = gameLocal.time;
	f.seconds = st.seconds;
	f.shotAge = ( st.shotTime != 0 ) ? gameLocal.time - st.shotTime : -1;
	f.alpha = ( calm ? st.deploy : 1.0f ) * ( 1.0f - XH_STANDBY_DIM * f.standby );

	f.cycle = 1.0f;
	if ( st.cycling && st.shotInterval > 0 ) {
		f.cycle = idMath::ClampFloat( 0.0f, 1.0f, (float)( gameLocal.time - st.shotTime ) / (float)st.shotInterval );
	}

	// the hit marker's pulse
	f.hitColor = colorHitMarker;
	f.hitMarks = hud_hitMarker.GetBool();
	const float life = HitMarkerLifeFraction( gameLocal.time );
	if ( life >= 0.0f ) {
		static const float tierPunch[ HITMARKER_TIER_COUNT ] = { 0.45f, 0.7f, 1.0f, 1.25f };
		const hitMarkerTierStyle_t &tier = hitMarkerTierStyles[ hitMarkerPulse.tier ];
		const float fade = HitMarkerAlpha( life, tier.hold );
		const bool mistake = ( hitMarkerPulse.flags & ( HITMARKER_SELF | HITMARKER_TEAM ) ) != 0;

		HitMarkerColorForFlags( hitMarkerPulse.flags, f.hitColor );
		f.hitTier = hitMarkerPulse.tier;
		f.kill = ( hitMarkerPulse.flags & HITMARKER_KILL ) != 0 && !mistake;
		f.hitExpansion = HitMarkerExpansion( life, hitMarkerPulse.carry );
		f.hitAlpha = tier.alpha * fade * ( mistake ? HITMARKER_MISTAKE_ALPHA : 1.0f );
		f.punch = ( 1.0f - life ) * ( 1.0f - life ) * tierPunch[ hitMarkerPulse.tier ] * ( calm ? 0.3f : 1.0f );
		f.hitMix = fade * tier.alpha * ( cvarSystem->GetCVarBool( "ui_reducedFlashing" ) ? 0.45f : 0.9f );
	}
}

/*
================
AnimatedCrosshair_OwnsMarker

Declared ahead of rvHitMarker, which stands aside while this is true.
================
*/
static bool AnimatedCrosshair_OwnsMarker( void ) {
	return xhOwnsMarker && rvAnimatedCrosshair::Enabled();
}

/*
================
rvAnimatedCrosshair::Enabled
================
*/
bool rvAnimatedCrosshair::Enabled( void ) {
	static idCVar *retained = NULL;
	if ( retained == NULL ) {
		retained = cvarSystem->Find( "ui_retained" );
	}
	return hud_crosshairAnimated.GetBool() && retained != NULL && retained->GetBool();
}

/*
================
rvAnimatedCrosshair::Clear
================
*/
void rvAnimatedCrosshair::Clear( void ) {
	memset( &xhState, 0, sizeof( xhState ) );
	xhOwnsMarker = false;
	xhTestShot = 0;
}

/*
================
rvAnimatedCrosshair::CursorColor
================
*/
const char *rvAnimatedCrosshair::CursorColor( const char *design ) {
	if ( !Enabled() || XH_DesignForMaterial( design ) == XH_NONE ) {
		return g_crosshairColor.GetString();
	}
	idVec4 color( 1.0f, 1.0f, 1.0f, 1.0f );
	sscanf( g_crosshairColor.GetString(), "%f %f %f %f", &color.x, &color.y, &color.z, &color.w );
	return va( "%g %g %g 0", color.x, color.y, color.z );
}

/*
================
rvAnimatedCrosshair::ConsumeToggle
================
*/
bool rvAnimatedCrosshair::ConsumeToggle( void ) {
	const bool enabled = Enabled();
	if ( enabled != xhWasEnabled ) {
		xhWasEnabled = enabled;
		xhToggled = true;
	}
	const bool toggled = xhToggled;
	xhToggled = false;
	return toggled;
}

/*
================
rvAnimatedCrosshair::Draw
================
*/
void rvAnimatedCrosshair::Draw( idUserInterface *cursor, const rvWeapon *weapon, const rvVehicleWeapon *vehicleWeapon ) {
	xhOwnsMarker = false;
	if ( !Enabled() || cursor == NULL ) {
		if ( xhState.design != XH_NONE ) {
			Clear();
		}
		return;
	}

	crosshairSignals_t signals;
	XH_ReadSignals( weapon, vehicleWeapon, signals );

	const xhDesign_t wanted = XH_DesignForMaterial( signals.design );
	const idMaterial *material = ( wanted != XH_NONE ) ? declManager->FindMaterial( signals.design ) : NULL;

	// The cursor GUI still decides which cursor is showing: the combat
	// crosshair gives way to the use hand, the talk frame and the rest.
	idStr combat;
	const bool shown = cursor->GetPresentationValue( "crossCombat::visible", combat ) && combat.Icmp( "0" ) != 0;
	// reduced motion fades the design in place; reduced flashing keeps the
	// motion and drops what flashes
	const bool calm = cvarSystem->GetCVarBool( "ui_retainedReducedMotion" );
	const bool steady = calm || cvarSystem->GetCVarBool( "ui_reducedFlashing" );

	XH_Advance( signals, shown && wanted != XH_NONE && material != NULL, wanted, material, calm, steady );

	const xhState_t &st = xhState;
	if ( st.design == XH_NONE ) {
		return;
	}
	// the marker's pulse belongs to the design from here on, drawn or not
	xhOwnsMarker = true;
	if ( st.deploy <= 0.0f ) {
		return;
	}

	xhFrame_t f;
	XH_BuildFrame( f, calm, steady );

	idVec4 color( 1.0f, 1.0f, 1.0f, 1.0f );
	sscanf( g_crosshairColor.GetString(), "%f %f %f %f", &color.x, &color.y, &color.z, &color.w );

	xhCanvas canvas;
	canvas.Begin( (float)idMath::ClampInt( 16, 48, cvarSystem->GetCVarInteger( "g_crosshairSize" ) ), color );

	const xhStyle_t &style = xhStyles[ st.design ];
	switch ( st.design ) {
		case XH_BLASTER:	XH_DrawBlaster( canvas, f ); break;
		case XH_MACHINEGUN:	XH_DrawMachinegun( canvas, f ); break;
		case XH_SHOTGUN:	XH_DrawShotgun( canvas, f ); break;
		case XH_ROCKET:		XH_DrawRocket( canvas, f ); break;
		case XH_RAILGUN:	XH_DrawRailgun( canvas, f ); break;
		case XH_GAUNTLET:	XH_DrawGauntlet( canvas, f ); break;
		case XH_GRENADE:	XH_DrawGrenade( canvas, f, st.material ); break;
		case XH_NAILGUN:	XH_DrawNailgun( canvas, f, st.material ); break;
		case XH_LIGHTNING:	XH_DrawLightning( canvas, f, st.material ); break;
		case XH_CANNON:		XH_DrawCannon( canvas, f, st.material ); break;
		case XH_NAPALM:		XH_DrawWhole( canvas, f, st.material, true ); break;
		default:			XH_DrawWhole( canvas, f, st.material, false ); break;
	}

	XH_DrawGauge( canvas, f, style, signals );
	if ( st.design != XH_NAILGUN ) {
		XH_DrawLock( canvas, f, style );
	}
	XH_DrawHit( canvas, f, style );

	canvas.End();
}

/*
================
rvAnimatedCrosshair::Test

testCrosshair: holds a design in a state so it can be looked at, tuned and
captured without a weapon in that state.  With no arguments it lets go.
================
*/
void rvAnimatedCrosshair::Test( const idCmdArgs &args ) {
	if ( args.Argc() < 2 ) {
		xhTest.Clear();
		xhTestShot = 0;
		gameLocal.Printf( "testCrosshair: released\n"
						  "usage: testCrosshair [design <material>] [raised|fire|reload|empty|zoom 0/1] [clip n] [clipsize n]\n"
						  "       [progress|charge|spin|lock 0..1] [shot <ms>]\n" );
		return;
	}
	for ( int i = 1; i < args.Argc(); i++ ) {
		const char *key = args.Argv( i );
		if ( !idStr::Icmp( key, "shot" ) ) {
			const int wait = ( i + 1 < args.Argc() && idStr::IsNumeric( args.Argv( i + 1 ) ) ) ? atoi( args.Argv( ++i ) ) : 100;
			xhTestShot = gameLocal.time + Max( 1, wait );
			continue;
		}
		if ( i + 1 >= args.Argc() ) {
			break;
		}
		xhTest.Set( key, args.Argv( ++i ) );
	}
}
