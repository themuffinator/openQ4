// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
//
/*
===============================================================================

	Built-in touch gameplay controls for the standalone Android app.

	The layout, the per-finger state machine, the input each control produces
	and the overlay's geometry, free of engine types: android_touch_controls.cpp
	feeds it SDL finger events and applies its output through the synchronized
	usercmd touch channel, and tools/tests/android_touch_controls.py
	compiles it on its own.

	docs/dev/ui-visual-design.md section 13.6 is the specification: a floating
	move stick on the leading half, drag-to-look on the trailing half, Fire at
	88 dp in the trailing thumb zone with jump, crouch, weapon, reload and zoom
	around it, and Pause at 48 dp in the top leading corner. In portrait the
	band below the center screen is the thumb deck: the stick floats in its
	leading half and the cluster gathers in its trailing half (PlacePortraitDeck);
	each orientation is laid out on its own, so turning the device swaps between
	the two arrangements and the player's preset and size apply to both. Stick bases are
	thin-railed rings and buttons cut plates around centered vector symbols,
	35% opaque at rest and 80% with the orange marker while pressed, without
	labels. Quake 4 has no use key: Fire clicks the terminal under the
	crosshair, as in the stock game.

	Coordinates are window pixels, origin top-left. One dp is the display's
	density-independent pixel (SDL_GetWindowDisplayScale).

===============================================================================
*/

#ifndef __ANDROID_TOUCH_CONTROLS_H__
#define __ANDROID_TOUCH_CONTROLS_H__

#include <cmath>
#include <cstdint>
#include <algorithm>
#include <vector>

namespace openq4 {
namespace touch {

enum ButtonId {
	BUTTON_FIRE = 0,
	BUTTON_JUMP,
	BUTTON_CROUCH,
	BUTTON_WEAPON,
	BUTTON_RELOAD,
	BUTTON_ZOOM,
	BUTTON_PAUSE,
	BUTTON_SCORES,		// multiplayer only
	BUTTON_CHAT,		// multiplayer only
	BUTTON_COUNT,
	BUTTON_NONE = -1
};

static const unsigned kAllButtons = ( 1u << BUTTON_COUNT ) - 1u;
static const unsigned kMultiplayerButtons = ( 1u << BUTTON_SCORES ) | ( 1u << BUTTON_CHAT );
static const unsigned kSinglePlayerButtons = kAllButtons & ~kMultiplayerButtons;

enum LayoutPreset {
	PRESET_DEFAULT = 0,
	PRESET_COMPACT,
	PRESET_LEFT_HANDED
};

struct Circle {
	float x;
	float y;
	float radius;
};

/*
====================
Layout

Button centers are placed in dp from a corner: the gameplay cluster from the
trailing-bottom one, so it sits under the right thumb on every aspect ratio,
Pause from the top leading corner, and the multiplayer scoreboard and chat
from the top trailing one, clear of a camera cutout at the top center. Each
control keeps at least 8 dp from its neighbours and a hit area of at least
48 dp, and every control stays clear of the crosshair at the center.
====================
*/
struct Layout {
	float width = 0.0f;
	float height = 0.0f;
	float nativeDp = 1.0f;			// pixels per physical dp, independent of control size
	float dp = 1.0f;					// pixels per dp, times the player's scale
	bool available = false;			// all targets fit, with their minimum spacing
	bool leftHanded = false;			// movement on the right; look/fire on the left
	bool edited = false;				// the player's layout edits are in effect
	bool editsRejected = false;		// edits were given but did not fit; the preset is shown
	unsigned hidden = 0;				// controls the player hid (1 << ButtonId)
	unsigned moved = 0;					// controls the player placed (1 << ButtonId)
	Circle buttons[BUTTON_COUNT] = {};
	float restOpacity[BUTTON_COUNT] = {};	// 0 means the default, kRestOpacity
	Circle stickHome = {};				// the faint ring a resting stick shows
	float stickBaseRadius = 0.0f;		// full deflection
	float stickKnobRadius = 0.0f;
	float leadingEdge = 0.0f;			// a free finger left of this starts the stick
	float stickTop = 0.0f;			// portrait: the leading thumb deck, not the view above it
	float tapSlop = 0.0f;				// maximum movement for a tap, in physical dp
};

enum Anchor {
	ANCHOR_TRAILING_BOTTOM,
	ANCHOR_LEADING_TOP,
	ANCHOR_TRAILING_TOP
};

struct ButtonPlacement {
	Anchor anchor;
	float fromSide;			// dp from the anchor's left or right edge to the center
	float fromEnd;			// dp from the anchor's top or bottom edge to the center
	float diameter;			// dp
};

// Diameters follow section 13.6: Fire 88 dp, the cluster 64 dp, the two
// controls beside Fire 56 dp, Pause, the scoreboard and chat 48 dp, which in
// their corners leaves a 16 dp margin to the screen's edges.
static const ButtonPlacement kButtonPlacements[BUTTON_COUNT] = {
	{ ANCHOR_TRAILING_BOTTOM, 128.0f, 120.0f, 88.0f },	// BUTTON_FIRE
	{ ANCHOR_TRAILING_BOTTOM, 52.0f, 196.0f, 64.0f },	// BUTTON_JUMP
	{ ANCHOR_TRAILING_BOTTOM, 52.0f, 60.0f, 64.0f },	// BUTTON_CROUCH
	{ ANCHOR_TRAILING_BOTTOM, 148.0f, 236.0f, 64.0f },	// BUTTON_WEAPON
	{ ANCHOR_TRAILING_BOTTOM, 224.0f, 88.0f, 56.0f },	// BUTTON_RELOAD: above the default status readouts
	{ ANCHOR_TRAILING_BOTTOM, 232.0f, 160.0f, 56.0f },	// BUTTON_ZOOM
	{ ANCHOR_LEADING_TOP, 40.0f, 40.0f, 48.0f },		// BUTTON_PAUSE
	{ ANCHOR_TRAILING_TOP, 40.0f, 40.0f, 48.0f },		// BUTTON_SCORES
	{ ANCHOR_TRAILING_TOP, 120.0f, 40.0f, 48.0f },		// BUTTON_CHAT
};

// Section 13.6: 35% opaque at rest, 80% while pressed.
static const float kRestOpacity = 0.35f;
static const float kPressedOpacity = 0.80f;
static const float kStickBaseRadiusDp = 64.0f;
static const float kStickKnobRadiusDp = 28.0f;
static const float kStickDeadZone = 0.08f;
static const float kHitSlopDp = 8.0f;
static const int64_t kWeaponWheelHoldMs = 300;

inline float DistanceSquared( float ax, float ay, float bx, float by ) {
	const float dx = ax - bx;
	const float dy = ay - by;
	return dx * dx + dy * dy;
}

inline float HitRadius( const Layout &layout, int button ) {
	// Resizing the artwork never reduces a target below 48 physical dp.
	return std::max( layout.buttons[button].radius, 24.0f * layout.nativeDp );
}

// free: a control the player placed, which may sit anywhere on the screen. It
// keeps 24 dp from the side and bottom edges' system gestures and 8 dp from
// the top, as the top actions do.
inline bool PlaceButton( Layout &layout, int button, unsigned placed, bool free = false ) {
	const float radius = HitRadius( layout, button );
	const float gap = 8.0f * layout.nativeDp;
	const bool top = !free && kButtonPlacements[button].anchor != ANCHOR_TRAILING_BOTTOM;
	// The gameplay controls support swipes; keep them clear of system gestures.
	const float edge = ( top ? 8.0f : 24.0f ) * layout.nativeDp;
	const float topEdge = ( top || free ? 8.0f : 24.0f ) * layout.nativeDp;
	const float left = edge + radius;
	const float right = layout.width - left;
	const float bottom = layout.height - edge - radius;
	const float yMin = topEdge + radius;
	const float yMax = top ? yMin : bottom;
	if ( right < left || yMax < yMin ) return false;
	const Circle desired = layout.buttons[button];
	const auto fits = [&]( float x, float y ) {
		// Keep the crosshair and a small area around it usable, including in
		// portrait and while the player changes the size of the controls.
		const float centerClearance = radius + 24.0f * layout.nativeDp;
		if ( DistanceSquared( x, y, layout.width * 0.5f, layout.height * 0.5f )
				< centerClearance * centerClearance ) return false;
		for ( int other = 0; other < BUTTON_COUNT; ++other ) {
			if ( !( placed & ( 1u << other ) ) ) continue;
			const float separation = radius + HitRadius( layout, other ) + gap;
			if ( DistanceSquared( x, y, layout.buttons[other].x, layout.buttons[other].y )
					< separation * separation ) return false;
		}
		return true;
	};
	const float desiredX = std::max( left, std::min( right, desired.x ) );
	const float desiredY = std::max( yMin, std::min( yMax, desired.y ) );
	if ( fits( desiredX, desiredY ) ) {
		layout.buttons[button].x = desiredX;
		layout.buttons[button].y = desiredY;
		return true;
	}
	// On short windows or at larger sizes, keep the authored corner preference
	// and choose the nearest free grid point. Top actions stay on the top edge.
	// This runs only when geometry/settings change, not for each finger event.
	const float step = std::max( 4.0f * layout.nativeDp,
		std::max( layout.width, layout.height ) / 256.0f );
	float best = -1.0f;
	for ( float y = yMin; y <= yMax; y += step ) {
		for ( float x = left; x <= right; x += step ) {
			const float cost = DistanceSquared( x, y, desired.x, desired.y );
			if ( ( best >= 0.0f && cost >= best ) || !fits( x, y ) ) continue;
			best = cost;
			layout.buttons[button].x = x;
			layout.buttons[button].y = y;
		}
	}
	return best >= 0.0f;
}

/*
====================
DeckTop

Portrait (section 13.6): the 640 u center screen spans the width, 480 u tall,
and the band below it is the thumb deck. Its top edge, in window pixels, or 0
in landscape, where the whole height is available to the stick.
====================
*/
inline float DeckTop( float width, float height ) {
	return height > width ? height * 0.5f + width * 0.375f : 0.0f;
}

// How far b must rise above a, in whole pixels, for two circles `separation`
// apart whose centers are `across` apart horizontally.
inline float DeckRise( float separation, float across ) {
	return across >= separation ? 0.0f : std::ceil( std::sqrt( separation * separation - across * across ) );
}

inline float HitRadius( const Layout &layout, int button );

/*
====================
PlacePortraitDeck

Section 13.6's portrait arrangement in the deck's trailing half: Fire in its
middle with the weapon control above it, jump and crouch outside it and zoom
and reload inside it. At the 720 dp reference the columns keep the landscape
cluster's spacing. A narrower half (a 360-412 dp phone has 180-206 dp) brings
the outer and inner columns in to keep every target inside the half, 24 dp
clear of the trailing edge, and raises the rows instead, so sizes and the 8 dp
gaps stay as in landscape. Only the desired centers are set here; PlaceButton
still resolves anything that does not fit.
====================
*/
inline void PlacePortraitDeck( Layout &layout, float dp ) {
	// Pixels throughout, from the actual hit regions: the compact preset's
	// smaller artwork keeps 48 physical dp targets, as PlaceButton measures.
	// A pixel of slack keeps float error from failing PlaceButton's 8 dp test.
	const float gap = 8.0f * layout.nativeDp + 1.0f, edge = 24.0f * layout.nativeDp;
	const float fireR = HitRadius( layout, BUTTON_FIRE ), weaponR = HitRadius( layout, BUTTON_WEAPON );
	const float sideR = std::max( HitRadius( layout, BUTTON_JUMP ), HitRadius( layout, BUTTON_CROUCH ) );
	const float innerR = std::max( HitRadius( layout, BUTTON_ZOOM ), HitRadius( layout, BUTTON_RELOAD ) );
	const float center = layout.width * 0.25f;	// Fire, from the trailing edge
	const float outside = std::max( 0.0f, std::min( 76.0f * dp, center - edge - sideR ) );
	const float inside = std::max( 0.0f, std::min( 96.0f * dp, center - innerR ) );
	const float low = std::max( 60.0f * dp, edge + std::max( sideR, innerR ) );	// crouch and reload
	const float fireToSide = fireR + sideR + gap, fireToInner = fireR + innerR + gap;
	const float fire = std::max( { 128.0f * dp, low + DeckRise( fireToSide, outside ), low + DeckRise( fireToInner, inside ) } );
	const float jump = std::max( fire + DeckRise( fireToSide, outside ), low + 2.0f * sideR + gap );
	const float zoom = std::max( fire + DeckRise( fireToInner, inside ), low + 2.0f * innerR + gap );
	const float weapon = std::max( { fire + fireR + weaponR + gap, jump + DeckRise( sideR + weaponR + gap, outside ),
		zoom + DeckRise( innerR + weaponR + gap, inside ) } );
	const struct { int button; float fromSide; float fromBottom; } deck[] = {
		{ BUTTON_FIRE, center, fire },
		{ BUTTON_JUMP, center - outside, jump },
		{ BUTTON_CROUCH, center - outside, low },
		{ BUTTON_WEAPON, center, weapon },
		{ BUTTON_RELOAD, center + inside, low },
		{ BUTTON_ZOOM, center + inside, zoom },
	};
	for ( const auto &place : deck ) {
		layout.buttons[place.button].x = layout.width - place.fromSide;
		layout.buttons[place.button].y = layout.height - place.fromBottom;
	}
}

// The preset's arrangement, before any player edits.
inline Layout ComputePresetLayout( float width, float height, float dpScale, float playerScale, int preset = PRESET_DEFAULT ) {
	Layout layout;
	if ( !std::isfinite( width ) || !std::isfinite( height )
			|| width <= 1.0f || height <= 1.0f || width > 65536.0f || height > 65536.0f ) return layout;
	layout.width = width;
	layout.height = height;
	const float scale = std::isfinite( playerScale ) ? std::max( 0.75f, std::min( 1.5f, playerScale ) ) : 1.0f;
	layout.nativeDp = std::isfinite( dpScale ) && dpScale >= 0.25f && dpScale <= 16.0f ? dpScale : 1.0f;
	const float dp = layout.nativeDp * scale * ( preset == PRESET_COMPACT ? 0.8f : 1.0f );
	layout.dp = dp;
	for ( int i = 0; i < BUTTON_COUNT; i++ ) {
		const ButtonPlacement &place = kButtonPlacements[i];
		Circle &button = layout.buttons[i];
		button.radius = place.diameter * 0.5f * dp;
		button.x = place.anchor == ANCHOR_LEADING_TOP ? place.fromSide * dp : layout.width - place.fromSide * dp;
		button.y = place.anchor == ANCHOR_TRAILING_BOTTOM ? layout.height - place.fromEnd * dp : place.fromEnd * dp;
	}
	const bool portrait = height > width;
	if ( portrait ) PlacePortraitDeck( layout, dp );
	// Reserve the gameplay cluster first. Enlarged controls on a short phone
	// can require the two multiplayer actions to move along the top edge.
	unsigned placed = 0;
	for ( int i = 0; i < BUTTON_COUNT; ++i ) {
		if ( !PlaceButton( layout, i, placed ) ) return layout;
		placed |= 1u << i;
	}
	layout.available = true;
	layout.stickBaseRadius = kStickBaseRadiusDp * dp;
	layout.stickKnobRadius = kStickKnobRadiusDp * dp;
	// In portrait the resting ring sits in the middle of the deck's leading half.
	layout.stickHome.x = portrait ? layout.width * 0.25f : std::min( ( kStickBaseRadiusDp + 56.0f ) * dp, layout.width * 0.25f );
	layout.stickHome.y = layout.height - ( kStickBaseRadiusDp + 48.0f ) * dp;
	layout.stickHome.radius = layout.stickBaseRadius;
	layout.leadingEdge = layout.width * 0.5f;
	layout.stickTop = DeckTop( width, height );
	layout.stickHome.y = std::max( layout.stickHome.y, ( layout.stickTop + height ) * 0.5f );
	layout.tapSlop = kHitSlopDp * layout.nativeDp;
	if ( preset == PRESET_LEFT_HANDED ) {
		// Mirror the completed collision-safe arrangement, including the top
		// actions. Thumb movement still has the same world-space direction.
		layout.leftHanded = true;
		for ( Circle &button : layout.buttons ) button.x = width - button.x;
		layout.stickHome.x = width - layout.stickHome.x;
	}
	return layout;
}

/*
====================
Layout edits

Section 13.6's layout editor moves, resizes (75-150%), fades and hides each
control, and each orientation keeps its own edits. The Android setup's editor
(mobile/android/.../TouchLayout.java) writes them as one record and previews
them with a port of this file's placement; the engine keeps the applied
record in in_touchLayout.

Positions are whole physical dp from the window's nearest edges, so a layout
survives a resize, a cutout or another phone of the same shape: x from the
left edge, or when negative from the right; y from the top, or when negative
from the bottom. Pause cannot be hidden: it is also a cinematic's skip.

Record: "1|<landscape>|<portrait>", each orientation nine entries in ButtonId
order separated by ';', each entry "x,y,size,opacity,hidden" with x and y
both '*' for a control left where the preset puts it.
====================
*/
static const int kEditSizeDefault = 100, kEditSizeMin = 75, kEditSizeMax = 150;
static const int kEditOpacityDefault = 35, kEditOpacityMin = 10, kEditOpacityMax = 80;
static const int kEditStep = 5;				// size and opacity percentages
static const int kEditGridDp = 8;				// the editor's snapping grid
static const int kEditPositionLimit = 8192;	// dp
static const int kEditRecordLimit = 512;		// characters

struct ControlEdit {
	bool moved = false;
	int x = 0;
	int y = 0;
	int size = kEditSizeDefault;
	int opacity = kEditOpacityDefault;
	bool hidden = false;
	bool IsDefault() const {
		return !moved && size == kEditSizeDefault && opacity == kEditOpacityDefault && !hidden;
	}
};

struct LayoutEdits {
	ControlEdit controls[BUTTON_COUNT];
	bool IsDefault() const {
		for ( const ControlEdit &edit : controls ) if ( !edit.IsDefault() ) return false;
		return true;
	}
};

struct OrientationEdits {
	LayoutEdits landscape;
	LayoutEdits portrait;
	const LayoutEdits &For( float width, float height ) const { return height > width ? portrait : landscape; }
};

namespace detail {
inline bool ParseEditInt( const char *&cursor, int &value, bool allowSign ) {
	const bool negative = allowSign && *cursor == '-';
	if ( negative ) ++cursor;
	int digits = 0, result = 0;
	while ( *cursor >= '0' && *cursor <= '9' ) {
		if ( ++digits > 4 ) return false;
		result = result * 10 + ( *cursor++ - '0' );
	}
	if ( digits == 0 ) return false;
	value = negative ? -result : result;
	return true;
}

inline bool ParseEditPosition( const char *&cursor, bool &moved, int &value ) {
	if ( *cursor == '*' ) {
		++cursor;
		moved = false;
		value = 0;
		return true;
	}
	moved = true;
	return ParseEditInt( cursor, value, true ) && value != 0 && value >= -kEditPositionLimit && value <= kEditPositionLimit;
}

inline bool ParseEditPercent( const char *&cursor, int &value, int low, int high ) {
	return ParseEditInt( cursor, value, false ) && value >= low && value <= high && value % kEditStep == 0;
}

inline bool ParseOrientation( const char *&cursor, LayoutEdits &out ) {
	for ( int i = 0; i < BUTTON_COUNT; ++i ) {
		ControlEdit edit;
		bool movedY = false;
		int hidden = 0;
		if ( !ParseEditPosition( cursor, edit.moved, edit.x ) || *cursor++ != ','
				|| !ParseEditPosition( cursor, movedY, edit.y ) || movedY != edit.moved || *cursor++ != ','
				|| !ParseEditPercent( cursor, edit.size, kEditSizeMin, kEditSizeMax ) || *cursor++ != ','
				|| !ParseEditPercent( cursor, edit.opacity, kEditOpacityMin, kEditOpacityMax ) || *cursor++ != ','
				|| !ParseEditInt( cursor, hidden, false ) || hidden > 1 || ( hidden && i == BUTTON_PAUSE ) ) {
			return false;
		}
		edit.hidden = hidden != 0;
		out.controls[i] = edit;
		if ( i + 1 < BUTTON_COUNT && *cursor++ != ';' ) return false;
	}
	return true;
}
}	// namespace detail

// False, leaving out untouched, for a missing, malformed or oversized record.
inline bool ParseLayoutEdits( const char *record, OrientationEdits &out ) {
	if ( record == nullptr ) return false;
	for ( int length = 0; record[length] != '\0'; ++length ) {
		if ( length + 1 >= kEditRecordLimit ) return false;	// records are shorter than the limit
	}
	const char *cursor = record;
	if ( *cursor++ != '1' || *cursor++ != '|' ) return false;
	OrientationEdits candidate;
	if ( !detail::ParseOrientation( cursor, candidate.landscape ) || *cursor++ != '|'
			|| !detail::ParseOrientation( cursor, candidate.portrait ) || *cursor != '\0' ) return false;
	out = candidate;
	return true;
}

/*
====================
ComputeLayout

The preset's arrangement with the player's edits for this orientation.
Placed controls are reserved first, where the player put them, then the rest
take their preset places or the nearest free ones; the 48 dp targets, 8 dp
gaps, edge margins and crosshair clearance hold for every control. Edits that
cannot all fit (a smaller window, larger controls) fall back to the preset,
and editsRejected says so.
====================
*/
inline Layout ComputeLayout( float width, float height, float dpScale, float playerScale, int preset = PRESET_DEFAULT,
		const LayoutEdits *edits = nullptr ) {
	Layout layout = ComputePresetLayout( width, height, dpScale, playerScale, preset );
	if ( edits == nullptr || edits->IsDefault() || !layout.available ) return layout;
	Layout edited = layout;
	edited.edited = true;
	for ( int i = 0; i < BUTTON_COUNT; ++i ) {
		const ControlEdit &edit = edits->controls[i];
		edited.restOpacity[i] = static_cast<float>( edit.opacity ) / 100.0f;
		if ( edit.hidden && i != BUTTON_PAUSE ) edited.hidden |= 1u << i;
		edited.buttons[i].radius = layout.buttons[i].radius * static_cast<float>( edit.size ) / 100.0f;
		if ( edit.moved ) {
			edited.moved |= 1u << i;
			edited.buttons[i].x = edit.x > 0 ? edit.x * layout.nativeDp : width + edit.x * layout.nativeDp;
			edited.buttons[i].y = edit.y > 0 ? edit.y * layout.nativeDp : height + edit.y * layout.nativeDp;
		}
	}
	unsigned placed = 0;
	for ( int pass = 0; pass < 2; ++pass ) {
		for ( int i = 0; i < BUTTON_COUNT; ++i ) {
			const bool moved = ( edited.moved & ( 1u << i ) ) != 0;
			if ( ( edited.hidden & ( 1u << i ) ) || moved != ( pass == 0 ) ) continue;
			if ( !PlaceButton( edited, i, placed, moved ) ) {
				layout.editsRejected = true;
				return layout;
			}
			placed |= 1u << i;
		}
	}
	return edited;
}

inline float RestOpacity( const Layout &layout, int button ) {
	const float value = layout.restOpacity[button];
	return value > 0.0f ? value : kRestOpacity;
}

/*
====================
HitButton

The plate a press at (x, y) lands on, or BUTTON_NONE. The nearest plate wins
if an edited layout overlaps. Only the controls in visible (a mask of
1 << ButtonId) can be hit. An unusably small window has no active targets.
====================
*/
inline int HitButton( const Layout &layout, float x, float y, unsigned visible = kAllButtons ) {
	if ( !layout.available || !std::isfinite( x ) || !std::isfinite( y ) ) return BUTTON_NONE;
	int best = BUTTON_NONE;
	float bestDistance = 0.0f;
	for ( int i = 0; i < BUTTON_COUNT; i++ ) {
		if ( ( visible & ( 1u << i ) ) == 0 ) {
			continue;
		}
		const Circle &button = layout.buttons[i];
		const float reach = HitRadius( layout, i );
		const float distance = DistanceSquared( x, y, button.x, button.y );
		if ( distance <= reach * reach && ( best == BUTTON_NONE || distance < bestDistance ) ) {
			best = i;
			bestDistance = distance;
		}
	}
	return best;
}

// Whether a control sits along the top edge: a top action where its preset
// puts it, or any control the player placed in the window's top quarter.
inline bool TopControl( const Layout &layout, int button ) {
	if ( layout.moved & ( 1u << button ) ) return layout.buttons[button].y < layout.height * 0.25f;
	return kButtonPlacements[button].anchor != ANCHOR_TRAILING_BOTTOM;
}

// Window-coordinate lower edge of the top controls, including their touch
// clearance. HUDs in an inset viewport may already start below this edge.
inline float HudTop( const Layout &layout, unsigned visible ) {
	if ( !layout.available ) return 0.0f;
	float bottom = 0.0f;
	for ( int i = 0; i < BUTTON_COUNT; ++i ) {
		if ( !( visible & ( 1u << i ) ) || !TopControl( layout, i ) ) continue;
		bottom = std::max( bottom, layout.buttons[i].y + HitRadius( layout, i ) + 8.0f * layout.nativeDp );
	}
	return bottom;
}

// Portrait: the window-coordinate edge the bottom readouts stay above, the
// deck's top (section 13.6 raises them to the center screen's bottom edge);
// 0 in landscape, where the readouts keep their place beside the cluster.
inline float HudBottomEdge( const Layout &layout ) {
	return layout.available ? layout.stickTop : 0.0f;
}

/*
====================
Output

What the controls ask of the engine. The levels describe the current state;
the counts and the look delta accumulate until TakeOutput and are then
cleared, so nothing is lost between two engine frames.
====================
*/
struct Output {
	float moveSide = 0.0f;			// -1 left .. 1 right
	float moveForward = 0.0f;		// -1 back .. 1 forward
	bool attack = false;
	bool jump = false;
	bool crouch = false;
	bool zoom = false;
	bool weaponWheel = false;
	bool cancelWeaponWheel = false;	// cancellation must not select the hovered weapon
	bool scores = false;
	float lookX = 0.0f;				// pixels dragged right since the last take
	float lookY = 0.0f;				// pixels dragged down
	int nextWeapon = 0;				// taps on the weapon control
	int reload = 0;
	int pause = 0;
	int chat = 0;
	unsigned canceledButtons = 0;
};

enum FingerRole {
	ROLE_NONE = 0,
	ROLE_IGNORED,		// owned during a cinematic; never becomes a menu pointer
	ROLE_STICK,
	ROLE_LOOK,
	ROLE_BUTTON
};

// SDL finger identifiers are scoped to their touch device. Keeping both
// identities prevents a second touchscreen from moving/releasing the first.
struct FingerId {
	int64_t device = 0;
	int64_t finger = 0;
	FingerId() = default;
	FingerId( int64_t finger ) : finger( finger ) {}
	FingerId( int64_t device, int64_t finger ) : device( device ), finger( finger ) {}
	bool operator==( const FingerId &other ) const { return device == other.device && finger == other.finger; }
};

enum ContactOwner { CONTACT_NONE, CONTACT_GAME, CONTACT_MENU, CONTACT_DISCARDED };

// Ownership lasts for the complete contact, independently of the control's
// lifetime. A pause/rotation may release the actions, but its remaining finger
// events must not turn into clicks on the menu that just opened.
class Contacts {
public:
	ContactOwner Begin( FingerId id, bool gameplay ) {
		if ( Find( id ) != nullptr ) return CONTACT_DISCARDED;
		Entry *free = nullptr;
		bool menuOwned = false;
		for ( auto &entry : entries ) {
			if ( entry.owner == CONTACT_MENU ) menuOwned = true;
			if ( !free && entry.owner == CONTACT_NONE ) free = &entry;
		}
		if ( !free ) return CONTACT_DISCARDED;
		free->id = id;
		free->owner = gameplay ? CONTACT_GAME : menuOwned ? CONTACT_DISCARDED : CONTACT_MENU;
		return free->owner;
	}
	ContactOwner Get( FingerId id ) {
		Entry *entry = Find( id );
		return entry ? entry->owner : CONTACT_DISCARDED;
	}
	void End( FingerId id ) { if ( Entry *entry = Find( id ) ) *entry = {}; }
	void Clear() { for ( auto &entry : entries ) entry = {}; }
private:
	struct Entry { FingerId id; ContactOwner owner = CONTACT_NONE; } entries[32];
	Entry *Find( FingerId id ) {
		for ( auto &entry : entries ) if ( entry.owner != CONTACT_NONE && entry.id == id ) return &entry;
		return nullptr;
	}
};

struct Finger {
	FingerId id;
	FingerRole role = ROLE_NONE;
	int button = BUTTON_NONE;
	float originX = 0.0f;			// the stick's center, or the press point
	float originY = 0.0f;
	float lastX = 0.0f;
	float lastY = 0.0f;
	int64_t downMs = 0;
	bool inside = true;				// a button finger still over its plate
	bool tapEligible = true;			// an excursion cannot turn back into a tap
};

static const int kMaxFingers = 10;

class Controls {
public:
	void SetLayout( const Layout &newLayout ) {
		bool changed = newLayout.width != layout.width || newLayout.height != layout.height || newLayout.dp != layout.dp
			|| newLayout.nativeDp != layout.nativeDp || newLayout.available != layout.available || newLayout.leftHanded != layout.leftHanded
			|| newLayout.leadingEdge != layout.leadingEdge || newLayout.stickTop != layout.stickTop || newLayout.tapSlop != layout.tapSlop
			|| newLayout.stickBaseRadius != layout.stickBaseRadius || newLayout.stickKnobRadius != layout.stickKnobRadius;
		for ( int i = 0; i < BUTTON_COUNT; ++i ) {
			changed = changed || newLayout.buttons[i].x != layout.buttons[i].x || newLayout.buttons[i].y != layout.buttons[i].y
				|| newLayout.buttons[i].radius != layout.buttons[i].radius;
		}
		if ( changed ) {
			// a resize or a rescale under held fingers would leave them on
			// controls that moved; let go of everything instead
			Reset();
		}
		layout = newLayout;
	}
	const Layout &GetLayout() const { return layout; }
	// Which controls exist: kSinglePlayerButtons, or kAllButtons in
	// multiplayer. A change lets go of everything, as a resize does.
	void SetVisibleButtons( unsigned mask ) {
		if ( mask != visibleButtons ) {
			Reset();
			visibleButtons = mask;
		}
	}
	unsigned VisibleButtons() const { return visibleButtons; }
	void SetGesturesAllowed( bool allowed ) {
		if ( gesturesAllowed != allowed ) { Reset(); gesturesAllowed = allowed; }
	}

	// Lets go of every finger and every held control. Counts already queued
	// for the engine are dropped too: they belong to the play that stopped.
	void Reset() {
		const bool cancelWheel = pending.weaponWheel || pending.cancelWeaponWheel;
		for ( int i = 0; i < kMaxFingers; i++ ) {
			fingers[i] = Finger();
		}
		pending = Output();
		pending.cancelWeaponWheel = cancelWheel;
		pending.canceledButtons = kAllButtons;
	}

	bool HasFingers() const {
		for ( int i = 0; i < kMaxFingers; i++ ) {
			if ( fingers[i].role != ROLE_NONE ) {
				return true;
			}
		}
		return false;
	}

	// Each returns true when the controls consumed the finger.
	bool FingerDown( FingerId id, float x, float y, int64_t timeMs ) {
		if ( Find( id ) != nullptr ) {
			return true;	// a repeated down for a finger already held
		}
		if ( !layout.available || !std::isfinite( x ) || !std::isfinite( y ) || x < 0.0f || y < 0.0f || x > layout.width || y > layout.height ) return false;
		Finger *finger = FreeSlot();
		if ( finger == nullptr ) {
			return false;
		}
		*finger = Finger();
		finger->id = id;
		finger->originX = finger->lastX = x;
		finger->originY = finger->lastY = y;
		finger->downMs = timeMs;
		const int button = HitButton( layout, x, y, visibleButtons );
		if ( button != BUTTON_NONE ) {
			const Finger *previous = ButtonFinger( button );
			const bool alreadyHeld = previous != nullptr;
			if ( alreadyHeld && button == BUTTON_WEAPON ) finger->downMs = previous->downMs;
			finger->role = ROLE_BUTTON;
			finger->button = button;
			if ( !alreadyHeld ) PressButton( button );
		} else if ( !gesturesAllowed ) {
			finger->role = ROLE_IGNORED;
		} else if ( ( layout.leftHanded ? x > layout.leadingEdge : x < layout.leadingEdge )
				&& y >= layout.stickTop && StickFinger() == nullptr ) {
			finger->role = ROLE_STICK;
			// The thumb's landing point is neutral, including near an edge.
			// Clamping the origin would move the player on touch-down alone.
		} else {
			finger->role = ROLE_LOOK;
		}
		return true;
	}

	bool FingerMove( FingerId id, float x, float y, int64_t timeMs ) {
		Finger *finger = Find( id );
		if ( finger == nullptr ) {
			return false;
		}
		if ( !std::isfinite( x ) || !std::isfinite( y ) ) return FingerCancel( id );
		x = Clamp( x, 0.0f, layout.width );
		y = Clamp( y, 0.0f, layout.height );
		(void)timeMs;
		const float dx = x - finger->lastX;
		const float dy = y - finger->lastY;
		finger->lastX = x;
		finger->lastY = y;
		switch ( finger->role ) {
			case ROLE_LOOK:
				pending.lookX += dx;
				pending.lookY += dy;
				break;
			case ROLE_BUTTON:
				if ( finger->button == BUTTON_WEAPON || finger->button == BUTTON_PAUSE
						|| finger->button == BUTTON_CHAT || finger->button == BUTTON_RELOAD ) {
					const float slop = layout.tapSlop;
					finger->tapEligible = finger->tapEligible && DistanceSquared( x, y, finger->originX, finger->originY ) <= slop * slop;
				}
				// Dragging from Fire keeps turning while firing, and dragging
				// the held weapon control steers its wheel.
				if ( finger->button == BUTTON_FIRE || ( finger->button == BUTTON_WEAPON && pending.weaponWheel ) ) {
					pending.lookX += dx;
					pending.lookY += dy;
				}
				if ( finger->button == BUTTON_PAUSE || finger->button == BUTTON_CHAT || finger->button == BUTTON_RELOAD ) {
					// these open a menu, so only a release over the plate counts
					const Circle &plate = layout.buttons[finger->button];
					const float reach = HitRadius( layout, finger->button );
					finger->inside = finger->inside && DistanceSquared( x, y, plate.x, plate.y ) <= reach * reach;
				}
				break;
			default:
				break;
		}
		return true;
	}

	bool FingerUp( FingerId id, float x, float y, int64_t timeMs ) {
		Finger *finger = Find( id );
		if ( finger == nullptr ) {
			return false;
		}
		FingerMove( id, x, y, timeMs );
		const Finger released = *finger;
		*finger = Finger();
		if ( released.role == ROLE_BUTTON && !Held( released.button ) ) ReleaseButton( released, timeMs );
		return true;
	}

	// The system took the finger (a gesture, a dialog): let go of it without
	// the action its release would perform. A wheel it held closes.
	bool FingerCancel( FingerId id ) {
		Finger *finger = Find( id );
		if ( finger == nullptr ) {
			return false;
		}
		const int button = finger->role == ROLE_BUTTON ? finger->button : BUTTON_NONE;
		*finger = Finger();
		if ( button != BUTTON_NONE && !Held( button ) ) {
			pending.canceledButtons |= 1u << button;
			if ( button == BUTTON_FIRE ) pending.attack = false;
			if ( button == BUTTON_JUMP ) pending.jump = false;
			if ( button == BUTTON_CROUCH ) pending.crouch = false;
			if ( button == BUTTON_ZOOM ) pending.zoom = false;
			if ( button == BUTTON_SCORES ) pending.scores = false;
		}
		if ( button == BUTTON_WEAPON && !Held( BUTTON_WEAPON ) ) {
			pending.cancelWeaponWheel = pending.cancelWeaponWheel || pending.weaponWheel;
			pending.weaponWheel = false;
		}
		return true;
	}

	// Promotes a held weapon control to the weapon wheel.
	void Update( int64_t timeMs ) {
		for ( int i = 0; i < kMaxFingers; i++ ) {
			const Finger &finger = fingers[i];
			if ( finger.role == ROLE_BUTTON && finger.button == BUTTON_WEAPON && !pending.weaponWheel
					&& timeMs >= finger.downMs && timeMs - finger.downMs >= kWeaponWheelHoldMs ) {
				pending.weaponWheel = true;
			}
		}
	}

	// The current levels, plus everything counted since the last take.
	Output TakeOutput() {
		Output result = pending;
		const Finger *stick = StickFinger();
		result.moveSide = 0.0f;
		result.moveForward = 0.0f;
		if ( stick != nullptr ) {
			StickDeflection( *stick, result.moveSide, result.moveForward );
		}
		result.attack = pending.attack || Held( BUTTON_FIRE );
		result.jump = pending.jump || Held( BUTTON_JUMP );
		// in_toggleCrouch and in_toggleZoom turn these into toggles, as for a key
		result.crouch = pending.crouch || Held( BUTTON_CROUCH );
		result.zoom = pending.zoom || Held( BUTTON_ZOOM );
		result.scores = pending.scores || Held( BUTTON_SCORES );
		pending.attack = pending.jump = pending.crouch = pending.zoom = pending.scores = false;
		pending.lookX = pending.lookY = 0.0f;
		pending.cancelWeaponWheel = false;
		pending.canceledButtons = 0;
		pending.nextWeapon = pending.reload = pending.pause = pending.chat = 0;
		return result;
	}

	bool ButtonPressed( int button ) const {
		return Held( button );
	}

	// The stick's base center and knob position, when a thumb holds it.
	bool StickState( float &baseX, float &baseY, float &knobX, float &knobY ) const {
		const Finger *stick = StickFinger();
		if ( stick == nullptr ) {
			return false;
		}
		float side = 0.0f, forward = 0.0f;
		StickOffset( *stick, side, forward );
		baseX = stick->originX;
		baseY = stick->originY;
		knobX = baseX + side * layout.stickBaseRadius;
		knobY = baseY - forward * layout.stickBaseRadius;
		return true;
	}

private:
	static float Clamp( float value, float low, float high ) {
		if ( high < low ) {
			return ( low + high ) * 0.5f;
		}
		return value < low ? low : ( value > high ? high : value );
	}

	Finger *Find( FingerId id ) {
		for ( int i = 0; i < kMaxFingers; i++ ) {
			if ( fingers[i].role != ROLE_NONE && fingers[i].id == id ) {
				return &fingers[i];
			}
		}
		return nullptr;
	}

	Finger *FreeSlot() {
		for ( int i = 0; i < kMaxFingers; i++ ) {
			if ( fingers[i].role == ROLE_NONE ) {
				return &fingers[i];
			}
		}
		return nullptr;
	}

	const Finger *StickFinger() const {
		for ( int i = 0; i < kMaxFingers; i++ ) {
			if ( fingers[i].role == ROLE_STICK ) {
				return &fingers[i];
			}
		}
		return nullptr;
	}

	bool Held( int button ) const {
		return ButtonFinger( button ) != nullptr;
	}

	const Finger *ButtonFinger( int button ) const {
		for ( int i = 0; i < kMaxFingers; i++ ) {
			if ( fingers[i].role == ROLE_BUTTON && fingers[i].button == button ) {
				return &fingers[i];
			}
		}
		return nullptr;
	}

	// The thumb's offset from the stick's center, in base radii, clamped to the
	// unit circle; side is positive to the right, forward positive upwards.
	void StickOffset( const Finger &stick, float &side, float &forward ) const {
		const float radius = layout.stickBaseRadius > 0.0f ? layout.stickBaseRadius : 1.0f;
		side = ( stick.lastX - stick.originX ) / radius;
		forward = ( stick.originY - stick.lastY ) / radius;
		const float length = std::sqrt( side * side + forward * forward );
		if ( length > 1.0f ) {
			side /= length;
			forward /= length;
		}
	}

	// StickOffset with the dead zone taken out and the rest rescaled, so a
	// thumb resting on the stick does not creep and full deflection stays 1.
	void StickDeflection( const Finger &stick, float &side, float &forward ) const {
		StickOffset( stick, side, forward );
		const float length = std::sqrt( side * side + forward * forward );
		if ( length <= kStickDeadZone ) {
			side = forward = 0.0f;
			return;
		}
		const float scale = ( length - kStickDeadZone ) / ( 1.0f - kStickDeadZone ) / length;
		side *= scale;
		forward *= scale;
	}

	void PressButton( int button ) {
		pending.canceledButtons &= ~( 1u << button );
		// A valid quick tap must survive when DOWN and UP share one SDL pump.
		// Cancellation clears these edges; ordinary release delivers one tic.
		if ( button == BUTTON_FIRE ) pending.attack = true;
		if ( button == BUTTON_JUMP ) pending.jump = true;
		if ( button == BUTTON_CROUCH ) pending.crouch = true;
		if ( button == BUTTON_ZOOM ) pending.zoom = true;
		if ( button == BUTTON_SCORES ) pending.scores = true;
	}

	void ReleaseButton( const Finger &finger, int64_t timeMs ) {
		switch ( finger.button ) {
			case BUTTON_RELOAD:
				if ( finger.inside && finger.tapEligible ) pending.reload++;
				break;
			case BUTTON_WEAPON:
				if ( pending.weaponWheel ) {
					// releasing the wheel selects; any other weapon finger keeps it
					pending.weaponWheel = false;
				} else if ( finger.tapEligible && timeMs >= finger.downMs && timeMs - finger.downMs < kWeaponWheelHoldMs ) {
					pending.nextWeapon++;
				}
				break;
			case BUTTON_PAUSE:
				// activation on release, and only over the plate (section 13.6)
				if ( finger.inside && finger.tapEligible ) {
					pending.pause++;
				}
				break;
			case BUTTON_CHAT:
				if ( finger.inside && finger.tapEligible ) {
					pending.chat++;
				}
				break;
			default:
				break;
		}
	}

	Layout layout;
	Finger fingers[kMaxFingers];
	Output pending;
	unsigned visibleButtons = kSinglePlayerButtons;
	bool gesturesAllowed = true;
};

/*
===============================================================================

	Overlay geometry: triangles in window pixels with a colour per vertex,
	which android_touch_controls.cpp hands to the renderer's GUI model.

===============================================================================
*/

struct Vertex {
	float x;
	float y;
	uint8_t rgba[4];
};

struct Mesh {
	std::vector<Vertex> vertices;
	std::vector<uint32_t> indexes;

	void Clear() {
		vertices.clear();
		indexes.clear();
	}
};

struct Color {
	float r, g, b, a;
};

// docs/dev/ui-visual-design.md: the Marine HUD gauge green, a panel fill from
// surface.panel, and marine.orange for the pressed marker.
static const Color kRailColor = { 0.702f, 0.816f, 0.431f, 1.0f };		// #B3D06E
static const Color kSymbolColor = { 0.867f, 0.922f, 0.765f, 1.0f };		// #DDEBC3
static const Color kPlateColor = { 0.090f, 0.110f, 0.070f, 1.0f };		// #171C12
static const Color kMarkerColor = { 0.890f, 0.537f, 0.0f, 1.0f };		// #E38900

inline uint8_t ToByte( float value ) {
	if ( !( value > 0.0f ) ) {
		return 0;
	}
	if ( value >= 1.0f ) {
		return 255;
	}
	return static_cast<uint8_t>( value * 255.0f + 0.5f );
}

inline uint32_t AddVertex( Mesh &mesh, float x, float y, const Color &color, float alpha ) {
	Vertex vertex;
	vertex.x = x;
	vertex.y = y;
	vertex.rgba[0] = ToByte( color.r );
	vertex.rgba[1] = ToByte( color.g );
	vertex.rgba[2] = ToByte( color.b );
	vertex.rgba[3] = ToByte( color.a * alpha );
	mesh.vertices.push_back( vertex );
	return static_cast<uint32_t>( mesh.vertices.size() - 1 );
}

inline void AddTriangle( Mesh &mesh, uint32_t a, uint32_t b, uint32_t c ) {
	mesh.indexes.push_back( a );
	mesh.indexes.push_back( b );
	mesh.indexes.push_back( c );
}

inline void AddQuad( Mesh &mesh, const float corners[8], const Color &color, float alpha ) {
	const uint32_t a = AddVertex( mesh, corners[0], corners[1], color, alpha );
	const uint32_t b = AddVertex( mesh, corners[2], corners[3], color, alpha );
	const uint32_t c = AddVertex( mesh, corners[4], corners[5], color, alpha );
	const uint32_t d = AddVertex( mesh, corners[6], corners[7], color, alpha );
	AddTriangle( mesh, a, b, c );
	AddTriangle( mesh, a, c, d );
}

// A ring between inner and outer, as a strip of quads.
inline void AddRing( Mesh &mesh, float x, float y, float inner, float outer, const Color &color, float alpha,
		int segments = 40, float startAngle = 0.0f, float sweep = 6.2831853f ) {
	const uint32_t first = static_cast<uint32_t>( mesh.vertices.size() );
	for ( int i = 0; i <= segments; i++ ) {
		const float angle = startAngle + sweep * static_cast<float>( i ) / static_cast<float>( segments );
		const float c = std::cos( angle );
		const float s = std::sin( angle );
		AddVertex( mesh, x + c * outer, y + s * outer, color, alpha );
		AddVertex( mesh, x + c * inner, y + s * inner, color, alpha );
	}
	for ( int i = 0; i < segments; i++ ) {
		const uint32_t o0 = first + i * 2;
		const uint32_t i0 = o0 + 1;
		const uint32_t o1 = o0 + 2;
		const uint32_t i1 = o0 + 3;
		AddTriangle( mesh, o0, o1, i1 );
		AddTriangle( mesh, o0, i1, i0 );
	}
}

inline void AddDisc( Mesh &mesh, float x, float y, float radius, const Color &color, float alpha, int segments = 32 ) {
	const uint32_t center = AddVertex( mesh, x, y, color, alpha );
	const uint32_t first = static_cast<uint32_t>( mesh.vertices.size() );
	for ( int i = 0; i <= segments; i++ ) {
		const float angle = 6.2831853f * static_cast<float>( i ) / static_cast<float>( segments );
		AddVertex( mesh, x + std::cos( angle ) * radius, y + std::sin( angle ) * radius, color, alpha );
	}
	for ( int i = 0; i < segments; i++ ) {
		AddTriangle( mesh, center, first + i, first + i + 1 );
	}
}

// A thick line from (x0, y0) to (x1, y1).
inline void AddLine( Mesh &mesh, float x0, float y0, float x1, float y1, float width, const Color &color, float alpha ) {
	float dx = x1 - x0;
	float dy = y1 - y0;
	const float length = std::sqrt( dx * dx + dy * dy );
	if ( length <= 0.0f ) {
		return;
	}
	const float nx = -dy / length * width * 0.5f;
	const float ny = dx / length * width * 0.5f;
	const float corners[8] = { x0 + nx, y0 + ny, x1 + nx, y1 + ny, x1 - nx, y1 - ny, x0 - nx, y0 - ny };
	AddQuad( mesh, corners, color, alpha );
}

/*
====================
AddPlate

A square plate with its upper-trailing corner cut at 45 degrees: the panel
fill, a thin rail around the outline, and while pressed the orange marker,
the solid right triangle the Marine family puts in the corner the cut leaves.
====================
*/
// restAlpha: the control's opacity at rest, which the layout editor fades;
// pressed controls never show fainter than kPressedOpacity.
inline float PlateAlpha( bool pressed, float restAlpha ) {
	return pressed ? std::max( kPressedOpacity, restAlpha ) : restAlpha;
}

inline void AddPlate( Mesh &mesh, const Circle &plate, float dp, bool pressed, float restAlpha = kRestOpacity ) {
	const float alpha = PlateAlpha( pressed, restAlpha );
	// Inscribe the entire plate, including its rail, in the circular hit
	// region. Otherwise a visible corner can start a look/stick gesture.
	const float half = plate.radius * 0.70710678f - dp;
	const float cut = half * 0.5f;
	const float left = plate.x - half, right = plate.x + half;
	const float top = plate.y - half, bottom = plate.y + half;
	// outline: top-left, top-right minus the cut, the cut, bottom-right, bottom-left
	const float outline[10] = { left, top, right - cut, top, right, top + cut, right, bottom, left, bottom };
	const uint32_t center = AddVertex( mesh, plate.x, plate.y, kPlateColor, alpha );
	uint32_t corner[5];
	for ( int i = 0; i < 5; i++ ) {
		corner[i] = AddVertex( mesh, outline[i * 2], outline[i * 2 + 1], kPlateColor, alpha );
	}
	for ( int i = 0; i < 5; i++ ) {
		AddTriangle( mesh, center, corner[i], corner[( i + 1 ) % 5] );
	}
	const float rail = 2.0f * dp;
	for ( int i = 0; i < 5; i++ ) {
		const int next = ( i + 1 ) % 5;
		AddLine( mesh, outline[i * 2], outline[i * 2 + 1], outline[next * 2], outline[next * 2 + 1], rail, kRailColor, alpha );
	}
	if ( pressed ) {
		// the marker fills the corner the cut leaves, set off from the rail
		// by a gap of one and a half rails measured across the cut
		const float leg = cut - rail * 1.5f * 1.41421356f;
		if ( leg > 0.0f ) {
			const uint32_t a = AddVertex( mesh, right - leg, top, kMarkerColor, kPressedOpacity );
			const uint32_t b = AddVertex( mesh, right, top, kMarkerColor, kPressedOpacity );
			const uint32_t c = AddVertex( mesh, right, top + leg, kMarkerColor, kPressedOpacity );
			AddTriangle( mesh, a, b, c );
		}
	}
}

// The symbol each control carries, in a box of `size` pixels around (x, y).
inline void AddSymbol( Mesh &mesh, int button, float x, float y, float size, float dp, bool pressed,
		float restAlpha = kRestOpacity ) {
	const float alpha = PlateAlpha( pressed, restAlpha );
	const float stroke = 3.0f * dp;
	const float h = size * 0.5f;
	switch ( button ) {
		case BUTTON_FIRE: {
			// a crosshair: a ring and four ticks
			AddRing( mesh, x, y, h * 0.42f - stroke * 0.5f, h * 0.42f + stroke * 0.5f, kSymbolColor, alpha );
			AddLine( mesh, x, y - h * 0.9f, x, y - h * 0.55f, stroke, kSymbolColor, alpha );
			AddLine( mesh, x, y + h * 0.55f, x, y + h * 0.9f, stroke, kSymbolColor, alpha );
			AddLine( mesh, x - h * 0.9f, y, x - h * 0.55f, y, stroke, kSymbolColor, alpha );
			AddLine( mesh, x + h * 0.55f, y, x + h * 0.9f, y, stroke, kSymbolColor, alpha );
			break;
		}
		case BUTTON_JUMP:
		case BUTTON_CROUCH: {
			// a chevron pointing up (jump) or down (crouch), with a floor line
			const float direction = button == BUTTON_JUMP ? -1.0f : 1.0f;
			const float tipY = y + direction * h * 0.5f;
			const float armY = y - direction * h * 0.1f;
			AddLine( mesh, x - h * 0.55f, armY, x, tipY, stroke, kSymbolColor, alpha );
			AddLine( mesh, x, tipY, x + h * 0.55f, armY, stroke, kSymbolColor, alpha );
			const float floorY = y - direction * h * 0.55f;
			AddLine( mesh, x - h * 0.45f, floorY, x + h * 0.45f, floorY, stroke, kSymbolColor, alpha );
			break;
		}
		case BUTTON_WEAPON: {
			// two chevrons pointing to the next weapon
			for ( int i = 0; i < 2; i++ ) {
				const float cx = x - h * 0.3f + i * h * 0.5f;
				AddLine( mesh, cx - h * 0.2f, y - h * 0.45f, cx + h * 0.2f, y, stroke, kSymbolColor, alpha );
				AddLine( mesh, cx + h * 0.2f, y, cx - h * 0.2f, y + h * 0.45f, stroke, kSymbolColor, alpha );
			}
			break;
		}
		case BUTTON_RELOAD: {
			// a three-quarter turn with an arrowhead at its end
			const float radius = h * 0.5f;
			const float start = -1.5707963f;
			const float sweep = 4.712389f;
			AddRing( mesh, x, y, radius - stroke * 0.5f, radius + stroke * 0.5f, kSymbolColor, alpha, 24, start, sweep );
			const float endAngle = start + sweep;
			const float ex = x + std::cos( endAngle ) * radius;
			const float ey = y + std::sin( endAngle ) * radius;
			const uint32_t a = AddVertex( mesh, ex - h * 0.25f, ey - h * 0.18f, kSymbolColor, alpha );
			const uint32_t b = AddVertex( mesh, ex + h * 0.25f, ey - h * 0.18f, kSymbolColor, alpha );
			const uint32_t c = AddVertex( mesh, ex, ey + h * 0.22f, kSymbolColor, alpha );
			AddTriangle( mesh, a, b, c );
			break;
		}
		case BUTTON_ZOOM: {
			// a lens and its handle
			const float radius = h * 0.42f;
			const float lx = x - h * 0.12f;
			const float ly = y - h * 0.12f;
			AddRing( mesh, lx, ly, radius - stroke * 0.5f, radius + stroke * 0.5f, kSymbolColor, alpha );
			AddLine( mesh, lx + radius * 0.7f, ly + radius * 0.7f, x + h * 0.6f, y + h * 0.6f, stroke * 1.3f, kSymbolColor, alpha );
			break;
		}
		case BUTTON_PAUSE: {
			AddLine( mesh, x - h * 0.22f, y - h * 0.45f, x - h * 0.22f, y + h * 0.45f, stroke * 1.4f, kSymbolColor, alpha );
			AddLine( mesh, x + h * 0.22f, y - h * 0.45f, x + h * 0.22f, y + h * 0.45f, stroke * 1.4f, kSymbolColor, alpha );
			break;
		}
		case BUTTON_SCORES: {
			// three ranked rows, each shorter than the one above
			for ( int i = 0; i < 3; i++ ) {
				const float rowY = y - h * 0.4f + static_cast<float>( i ) * h * 0.4f;
				AddLine( mesh, x - h * 0.45f, rowY, x + h * ( 0.45f - static_cast<float>( i ) * 0.2f ), rowY, stroke, kSymbolColor, alpha );
			}
			break;
		}
		case BUTTON_CHAT: {
			// a speech bubble with its tail at the lower left
			const float left = x - h * 0.5f;
			const float right = x + h * 0.5f;
			const float top = y - h * 0.4f;
			const float bottom = y + h * 0.2f;
			const float tailTip = bottom + h * 0.35f;
			AddLine( mesh, left, top, right, top, stroke, kSymbolColor, alpha );
			AddLine( mesh, right, top, right, bottom, stroke, kSymbolColor, alpha );
			AddLine( mesh, right, bottom, left + h * 0.4f, bottom, stroke, kSymbolColor, alpha );
			AddLine( mesh, left + h * 0.4f, bottom, left + h * 0.1f, tailTip, stroke, kSymbolColor, alpha );
			AddLine( mesh, left + h * 0.1f, tailTip, left + h * 0.15f, bottom, stroke, kSymbolColor, alpha );
			AddLine( mesh, left + h * 0.15f, bottom, left, bottom, stroke, kSymbolColor, alpha );
			AddLine( mesh, left, bottom, left, top, stroke, kSymbolColor, alpha );
			break;
		}
		default:
			break;
	}
}

/*
====================
BuildOverlay

The whole overlay for one frame: every visible plate with its symbol, and
the stick, either where a thumb holds it (base ring, full-deflection ring and
knob) or as the faint ring that shows where it can be found. pauseOnly draws
Pause alone, for a cinematic, which Pause skips and nothing else acts on.
====================
*/
inline void BuildOverlay( const Controls &controls, Mesh &mesh, bool pauseOnly = false ) {
	mesh.Clear();
	const Layout &layout = controls.GetLayout();
	if ( !layout.available ) return;
	const float dp = layout.dp;
	const unsigned visible = pauseOnly ? ( 1u << BUTTON_PAUSE ) : controls.VisibleButtons();
	for ( int i = 0; i < BUTTON_COUNT; i++ ) {
		if ( ( visible & ( 1u << i ) ) == 0 ) {
			continue;
		}
		const bool pressed = controls.ButtonPressed( i );
		const Circle &plate = layout.buttons[i];
		const float rest = RestOpacity( layout, i );
		AddPlate( mesh, plate, dp, pressed, rest );
		AddSymbol( mesh, i, plate.x, plate.y, plate.radius * 1.1f, dp, pressed, rest );
	}
	if ( pauseOnly ) {
		return;
	}
	const float rail = 2.0f * dp;
	float baseX, baseY, knobX, knobY;
	if ( controls.StickState( baseX, baseY, knobX, knobY ) ) {
		AddRing( mesh, baseX, baseY, layout.stickBaseRadius - rail, layout.stickBaseRadius, kRailColor, kPressedOpacity );
		// the outer ring marks full deflection, the stick's full speed
		AddRing( mesh, baseX, baseY, layout.stickBaseRadius + 6.0f * dp, layout.stickBaseRadius + 6.0f * dp + rail * 0.5f,
				kRailColor, kRestOpacity );
		AddDisc( mesh, knobX, knobY, layout.stickKnobRadius, kPlateColor, kPressedOpacity );
		AddRing( mesh, knobX, knobY, layout.stickKnobRadius - rail, layout.stickKnobRadius, kMarkerColor, kPressedOpacity );
	} else {
		const Circle &home = layout.stickHome;
		AddRing( mesh, home.x, home.y, home.radius - rail, home.radius, kRailColor, kRestOpacity );
		AddRing( mesh, home.x, home.y, layout.stickKnobRadius - rail, layout.stickKnobRadius, kRailColor, kRestOpacity );
	}
}

} // namespace touch
} // namespace openq4

#endif /* !__ANDROID_TOUCH_CONTROLS_H__ */
