// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "src/sys/android/android_touch_controls.h"
#include "src/framework/TouchInput.h"

#include <cstdio>
#include <cstdlib>
#include <limits>

using namespace openq4::touch;

static void Check( bool condition, const char *message ) {
	if ( !condition ) {
		std::fprintf( stderr, "FAIL: %s\n", message );
		std::exit( 1 );
	}
}

static bool Near( float a, float b ) { return std::fabs( a - b ) < 0.001f; }

static void CheckLayout( float width, float height, float density, float scale, int preset ) {
	const Layout layout = ComputeLayout( width * density, height * density, density, scale, preset );
	if ( !layout.available ) {
		std::fprintf( stderr, "Layout unavailable: %.0fx%.0f dp, density %.2f, scale %.2f\n", width, height, density, scale );
	}
	Check( layout.available, "supported phone/tablet layout must fit" );
	Check( HudTop( layout, 0 ) == 0, "no hidden top controls reserve HUD space" );
	const float hudTop = HudTop( layout, kAllButtons );
	for ( int button : { BUTTON_PAUSE, BUTTON_SCORES, BUTTON_CHAT } ) {
		Check( hudTop >= layout.buttons[button].y + HitRadius( layout, button ) + 8 * density - .001f,
			"top HUD stays below visible targets and their 8 dp clearance" );
	}
	for ( int button = 0; button < BUTTON_COUNT; ++button ) {
		const Circle &circle = layout.buttons[button];
		const float reach = HitRadius( layout, button );
		Check( std::isfinite( circle.x ) && std::isfinite( circle.y ) && std::isfinite( reach ), "finite targets" );
		Check( reach >= 24.0f * density, "48 physical dp target at every player scale" );
		Check( circle.x - reach >= 0 && circle.y - reach >= 0
			&& circle.x + reach <= layout.width && circle.y + reach <= layout.height, "entire hit region on screen" );
		Check( HitButton( layout, circle.x, circle.y ) == button, "each target owns its center" );
		Check( HitButton( layout, circle.x, circle.y, kAllButtons & ~( 1u << button ) ) == BUTTON_NONE, "hidden target cannot act" );
		for ( const bool pressed : { false, true } ) {
			Mesh plate;
			AddPlate( plate, circle, layout.dp, pressed );
			AddSymbol( plate, button, circle.x, circle.y, circle.radius * 1.1f, layout.dp, pressed );
			for ( const auto &vertex : plate.vertices ) {
				Check( HitButton( layout, vertex.x, vertex.y ) == button,
					"every visible plate, rail, symbol and press marker belongs to its target" );
			}
		}
		const float crosshairGap = reach + 24 * density;
		Check( DistanceSquared( circle.x, circle.y, layout.width / 2, layout.height / 2 )
			>= crosshairGap * crosshairGap - 0.01f, "controls leave the crosshair clear" );
		for ( int other = 0; other < button; ++other ) {
			const float gap = std::sqrt( DistanceSquared( circle.x, circle.y, layout.buttons[other].x, layout.buttons[other].y ) )
				- reach - HitRadius( layout, other );
			Check( gap >= 8.0f * density - 0.01f, "8 physical dp between hit regions" );
		}
	}
}

// An edited layout keeps the rules among the controls it shows: 48 dp targets
// wholly on screen, 8 dp gaps and the crosshair clearance.
static void CheckEditedRules( const Layout &layout, float density ) {
	Check( layout.available, "edited layout available" );
	for ( int button = 0; button < BUTTON_COUNT; ++button ) {
		if ( layout.hidden & ( 1u << button ) ) continue;
		const Circle &circle = layout.buttons[button];
		const float reach = HitRadius( layout, button );
		Check( reach >= 24.0f * density - .001f, "edited target keeps 48 dp" );
		Check( circle.x - reach >= 0 && circle.y - reach >= 0 && circle.x + reach <= layout.width
			&& circle.y + reach <= layout.height, "edited target on screen" );
		const float crosshairGap = reach + 24 * density;
		Check( DistanceSquared( circle.x, circle.y, layout.width / 2, layout.height / 2 ) >= crosshairGap * crosshairGap - 0.01f,
			"edited controls leave the crosshair clear" );
		for ( int other = 0; other < button; ++other ) {
			if ( layout.hidden & ( 1u << other ) ) continue;
			const float gap = std::sqrt( DistanceSquared( circle.x, circle.y, layout.buttons[other].x, layout.buttons[other].y ) )
				- reach - HitRadius( layout, other );
			Check( gap >= 8.0f * density - 0.01f, "edited controls keep 8 dp gaps" );
		}
	}
}

static OrientationEdits ParseOrFail( const char *record ) {
	OrientationEdits edits;
	Check( ParseLayoutEdits( record, edits ), "valid layout record refused" );
	return edits;
}

// Section 13.6's layout editor: move, resize, fade and hide each control, a
// layout per orientation, and a preset fallback for edits that do not fit.
static void CheckLayoutEdits() {
	const char *untouched = "*,*,100,35,0;*,*,100,35,0;*,*,100,35,0;*,*,100,35,0;*,*,100,35,0;*,*,100,35,0;*,*,100,35,0;*,*,100,35,0;*,*,100,35,0";
	char record[512];
	std::snprintf( record, sizeof record, "1|%s|%s", untouched, untouched );
	OrientationEdits edits = ParseOrFail( record );
	Check( edits.landscape.IsDefault() && edits.portrait.IsDefault(), "untouched record is the preset" );
	const Layout preset = ComputeLayout( 720, 360, 1, 1 );
	const Layout same = ComputeLayout( 720, 360, 1, 1, PRESET_DEFAULT, &edits.landscape );
	Check( !same.edited && Near( same.buttons[BUTTON_FIRE].x, preset.buttons[BUTTON_FIRE].x ), "default edits change nothing" );

	// Fire moved and enlarged, jump hidden, zoom faded; portrait edits differ.
	std::snprintf( record, sizeof record,
		"1|-160,-120,150,35,0;*,*,100,35,1;*,*,100,35,0;*,*,100,35,0;*,*,100,35,0;*,*,100,10,0;48,-80,100,80,0;*,*,100,35,0;*,*,100,35,0|%s",
		untouched );
	edits = ParseOrFail( record );
	const Layout edited = ComputeLayout( 720, 360, 1, 1, PRESET_DEFAULT, &edits.For( 720, 360 ) );
	Check( edited.edited && !edited.editsRejected, "edits applied" );
	Check( Near( edited.buttons[BUTTON_FIRE].x, 560 ) && Near( edited.buttons[BUTTON_FIRE].y, 240 )
		&& Near( edited.buttons[BUTTON_FIRE].radius, 66 ), "moved, enlarged Fire sits where the player put it" );
	Check( Near( edited.buttons[BUTTON_PAUSE].x, 48 ) && Near( edited.buttons[BUTTON_PAUSE].y, 280 ),
		"a top action can be placed anywhere" );
	Check( edited.hidden == ( 1u << BUTTON_JUMP ), "hidden mask" );
	Check( Near( RestOpacity( edited, BUTTON_ZOOM ), 0.10f ) && Near( RestOpacity( edited, BUTTON_FIRE ), 0.35f )
		&& Near( RestOpacity( preset, BUTTON_FIRE ), kRestOpacity ), "faded control opacity" );
	Check( Near( PlateAlpha( true, 0.10f ), kPressedOpacity ) && Near( PlateAlpha( false, 0.80f ), 0.80f ),
		"a faded control still shows pressed at full press opacity" );
	CheckEditedRules( edited, 1 );
	Check( HitButton( edited, edited.buttons[BUTTON_JUMP].x, edited.buttons[BUTTON_JUMP].y, kAllButtons & ~edited.hidden )
		!= BUTTON_JUMP, "a hidden control cannot be pressed" );
	Check( HudTop( edited, kAllButtons & ~edited.hidden ) <= edited.buttons[BUTTON_SCORES].y + HitRadius( edited, BUTTON_SCORES )
		+ 8.0f + .01f, "Pause moved to the bottom no longer reserves the top" );
	Mesh faded;
	AddPlate( faded, edited.buttons[BUTTON_ZOOM], 1, false, RestOpacity( edited, BUTTON_ZOOM ) );
	Check( !faded.vertices.empty() && faded.vertices[0].rgba[3] == ToByte( 0.10f * kPlateColor.a ), "faded plate draws fainter" );
	const Layout portrait = ComputeLayout( 360, 720, 1, 1, PRESET_DEFAULT, &edits.For( 360, 720 ) );
	Check( !portrait.edited && portrait.hidden == 0, "each orientation keeps its own edits" );

	// The editor's placements resolve collisions: a control dropped on another
	// moves to the nearest free spot, never overlapping.
	std::snprintf( record, sizeof record,
		"1|*,*,100,35,0;-128,-120,100,35,0;*,*,100,35,0;*,*,100,35,0;*,*,100,35,0;*,*,100,35,0;*,*,100,35,0;*,*,100,35,0;*,*,100,35,0|%s",
		untouched );
	edits = ParseOrFail( record );
	for ( const float density : { 1.0f, 2.75f } ) {
		for ( int preset : { PRESET_DEFAULT, PRESET_COMPACT, PRESET_LEFT_HANDED } ) {
			const Layout overlap = ComputeLayout( 720 * density, 360 * density, density, 1, preset, &edits.landscape );
			CheckEditedRules( overlap, density );
			Check( Near( overlap.buttons[BUTTON_JUMP].x, 592 * density ) && Near( overlap.buttons[BUTTON_JUMP].y, 240 * density ),
				"the placed control keeps its spot; the preset control moves aside" );
		}
	}

	// Everything at 150% on a small phone at the largest size cannot fit: the
	// preset is shown instead, and editsRejected says so.
	const char *huge = "*,*,150,35,0;*,*,150,35,0;*,*,150,35,0;*,*,150,35,0;*,*,150,35,0;*,*,150,35,0;*,*,150,35,0;*,*,150,35,0;*,*,150,35,0";
	std::snprintf( record, sizeof record, "1|%s|%s", huge, huge );
	edits = ParseOrFail( record );
	const Layout small = ComputeLayout( 640, 360, 1, 1.5f, PRESET_DEFAULT, &edits.landscape );
	const Layout smallPreset = ComputeLayout( 640, 360, 1, 1.5f );
	Check( small.available && small.editsRejected && !small.edited, "edits that cannot fit fall back to the preset" );
	for ( int i = 0; i < BUTTON_COUNT; ++i ) {
		Check( Near( small.buttons[i].x, smallPreset.buttons[i].x ) && Near( small.buttons[i].radius, smallPreset.buttons[i].radius ),
			"the fallback is exactly the preset" );
	}

	// Refused records leave the previous edits untouched.
	OrientationEdits kept = ParseOrFail( record );
	for ( const char *bad : { "", "1", "1|", "2|x|y", "1|*,*,100,35,0|*,*,100,35,0" } ) {
		Check( !ParseLayoutEdits( bad, kept ) && kept.landscape.controls[0].size == 150, "malformed record refused" );
	}
	std::snprintf( record, sizeof record,
		"1|*,*,100,35,0;*,*,100,35,0;*,*,100,35,0;*,*,100,35,0;*,*,100,35,0;*,*,100,35,0;*,*,100,35,1;*,*,100,35,0;*,*,100,35,0|%s",
		untouched );
	Check( !ParseLayoutEdits( record, kept ), "Pause cannot be hidden" );
	Check( !ParseLayoutEdits( nullptr, kept ), "null record refused" );
}

// Section 13.6 portrait: the cluster gathers in the deck's trailing half, Fire
// in its middle with the weapon control above it, jump and crouch outside it
// and zoom and reload inside it; every other drag looks and the stick floats
// in the deck's leading half.
static void CheckPortraitDeck( float width, float height, float density, int preset, bool wholeDeck ) {
	const Layout layout = ComputeLayout( width * density, height * density, density, 1, preset );
	Check( layout.available, "portrait deck fits" );
	const float w = layout.width, h = layout.height;
	const float deckTop = DeckTop( w, h );
	Check( Near( layout.stickTop, deckTop ) && deckTop > h * 0.5f && deckTop < h, "stick limited to the deck below the center screen" );
	const auto &b = layout.buttons;
	const auto mirror = [&]( float x ) { return preset == PRESET_LEFT_HANDED ? w - x : x; };
	const auto outward = [&]( float a, float c ) { return preset == PRESET_LEFT_HANDED ? a < c : a > c; };
	const int cluster[] = { BUTTON_FIRE, BUTTON_JUMP, BUTTON_CROUCH, BUTTON_WEAPON, BUTTON_RELOAD, BUTTON_ZOOM };
	for ( int button : cluster ) {
		const float reach = HitRadius( layout, button );
		const float trailing = mirror( b[button].x );
		Check( trailing - reach >= w * 0.5f - 0.01f, "portrait cluster stays in the deck's trailing half" );
		if ( wholeDeck ) Check( b[button].y - reach >= deckTop - 0.01f, "portrait cluster fits the deck at the reference" );
	}
	// The authored geometry holds without PlaceButton's fallback moving it.
	Check( Near( mirror( b[BUTTON_FIRE].x ), w * 0.75f ), "Fire in the middle of the trailing half" );
	Check( Near( b[BUTTON_WEAPON].x, b[BUTTON_FIRE].x ) && b[BUTTON_WEAPON].y < b[BUTTON_FIRE].y, "weapon control above Fire" );
	Check( outward( b[BUTTON_JUMP].x, b[BUTTON_FIRE].x ) && outward( b[BUTTON_CROUCH].x, b[BUTTON_FIRE].x )
		&& b[BUTTON_JUMP].y < b[BUTTON_CROUCH].y, "jump and crouch outside Fire, jump uppermost" );
	Check( outward( b[BUTTON_FIRE].x, b[BUTTON_ZOOM].x ) && outward( b[BUTTON_FIRE].x, b[BUTTON_RELOAD].x )
		&& b[BUTTON_ZOOM].y < b[BUTTON_RELOAD].y, "zoom and reload inside Fire, zoom uppermost" );
	Check( Near( layout.stickHome.x, mirror( w * 0.25f ) ) && layout.stickHome.y > deckTop, "resting stick in the deck's leading half" );
	// The top actions keep their landscape corners, clear of a camera cutout.
	Check( mirror( b[BUTTON_PAUSE].x ) < w * 0.25f && mirror( b[BUTTON_SCORES].x ) > w * 0.75f
		&& mirror( b[BUTTON_CHAT].x ) > w * 0.5f + 24 * density, "pause top leading; scoreboard and chat top trailing" );

	Controls controls;
	controls.SetLayout( layout );
	controls.SetVisibleButtons( kAllButtons );
	const float deckY = ( deckTop + h ) * 0.5f;
	const float leading = mirror( 24 * density ), trailing = mirror( w * 0.5f + 4 * density );
	Check( HitButton( layout, leading, deckY, kAllButtons ) == BUTTON_NONE, "deck's leading edge is free for the stick" );
	controls.FingerDown( { 1, 1 }, leading, deckY, 0 );
	controls.FingerMove( { 1, 1 }, mirror( 24 * density ) + ( preset == PRESET_LEFT_HANDED ? -40 : 40 ) * density, deckY, 10 );
	auto output = controls.TakeOutput();
	Check( std::fabs( output.moveSide ) > 0.5f && Near( output.lookX, 0 ), "deck's leading half moves" );
	controls.Reset();
	controls.TakeOutput();
	if ( HitButton( layout, trailing, deckTop + 2 * density, kAllButtons ) == BUTTON_NONE ) {
		controls.FingerDown( { 1, 2 }, trailing, deckTop + 2 * density, 0 );
		controls.FingerMove( { 1, 2 }, trailing + 10, deckTop + 2 * density, 10 );
		output = controls.TakeOutput();
		Check( Near( output.moveSide, 0 ) && Near( output.lookX, 10 ), "deck's trailing half looks" );
	}
	controls.Reset();
	controls.TakeOutput();
	controls.FingerDown( { 1, 3 }, leading, deckTop - 4 * density, 0 );
	controls.FingerMove( { 1, 3 }, leading, deckTop - 4 * density + 6, 10 );
	output = controls.TakeOutput();
	Check( Near( output.moveForward, 0 ) && Near( output.lookY, 6 ), "the view above the deck looks" );
}

int main() {
	// Portrait at the 720x1280 dp reference and on 9:16 to 20:9 phones (S25 is
	// about 384x832 dp). The reference holds the whole cluster in the deck.
	for ( const float density : { 1.0f, 2.8125f } ) {
		for ( int preset : { PRESET_DEFAULT, PRESET_COMPACT, PRESET_LEFT_HANDED } ) {
			CheckPortraitDeck( 720, 1280, density, preset, true );
			for ( const auto &size : { std::pair<float,float>( 360.0f, 640.0f ), { 360.0f, 745.667f }, { 384.0f, 832.0f }, { 412.0f, 915.0f } } ) {
				CheckPortraitDeck( size.first, size.second, density, preset, false );
			}
		}
	}
	{
		// 20:9 phones and wider fit everything but the weapon control in the deck.
		const Layout s25 = ComputeLayout( 384, 832, 1, 1 );
		for ( int button : { BUTTON_FIRE, BUTTON_JUMP, BUTTON_CROUCH, BUTTON_RELOAD, BUTTON_ZOOM } ) {
			Check( s25.buttons[button].y - HitRadius( s25, button ) >= DeckTop( 384, 832 ), "S25-sized deck holds the cluster" );
		}
		const Layout landscape = ComputeLayout( 720, 360, 1, 1 );
		Check( Near( landscape.buttons[BUTTON_FIRE].x, 592 ) && Near( landscape.buttons[BUTTON_FIRE].y, 240 )
			&& Near( landscape.buttons[BUTTON_ZOOM].x, 488 ) && Near( landscape.stickTop, 0 ), "landscape layout unchanged by the portrait deck" );
	}
	Contacts contacts;
	Check( contacts.Begin( {1, 1}, true ) == CONTACT_GAME, "gameplay claims initial contact" );
	Check( contacts.Begin( {2, 1}, false ) == CONTACT_MENU, "separate device may own menu pointer" );
	Check( contacts.Get( {1, 1} ) == CONTACT_GAME, "opening a menu never steals the gameplay contact" );
	Check( contacts.Begin( {2, 2}, false ) == CONTACT_DISCARDED, "only one menu finger" );
	contacts.End( {2, 1} );
	Check( contacts.Get( {2, 2} ) == CONTACT_DISCARDED, "secondary finger never inherits menu ownership" );
	Check( contacts.Begin( {1, 1}, false ) == CONTACT_DISCARDED, "duplicate down cannot reassign ownership" );
	contacts.Clear();
	Check( contacts.Get( {1, 1} ) == CONTACT_DISCARDED, "post-focus orphan events discarded" );
	for ( int i = 0; i < 32; ++i ) contacts.Begin( {1, i}, true );
	Check( contacts.Begin( {2, 99}, false ) == CONTACT_DISCARDED
		&& contacts.Get( {2, 99} ) == CONTACT_DISCARDED, "overflow cannot click a menu" );
	MotionState motion;
	motion.Submit( { 1, -1, 0.25f, -0.5f, true } );
	motion.Submit( { 0.5f, -1, 0.75f, -0.5f, true } );
	auto sample = motion.Take();
	Check( Near( sample.side, 0.5f ) && Near( sample.yaw, 1 ) && Near( sample.pitch, -1 ), "motion accumulates between usercmd ticks" );
	sample = motion.Take();
	Check( Near( sample.side, 0.5f ) && Near( sample.yaw, 0 ), "motion levels persist, deltas consumed once" );
	motion.Block();
	motion.Submit( { 1, 1, 30, 30, true } );
	Check( Near( motion.Take().yaw, 0 ), "menu handoff blocks the held gesture" );
	motion.Submit( {} );
	motion.Submit( { 1, 0, 1, 0, true } );
	Check( Near( motion.Take().yaw, 1 ), "fresh gesture works after neutral" );
	motion.Submit( { 0, 0, 10, 0, false } );
	Check( Near( motion.Take().yaw, 10 ), "last release delta is retained" );
	motion.Submit( { 0, 0, 0, 0, true, 1 } );
	motion.Submit( {} );
	Check( motion.Take().buttons == 1 && motion.Take().buttons == 0, "quick button edge survives slower usercmd sampling once" );
	motion.Submit( { 0, 0, 0, 0, true, 1 } );
	motion.Submit( { 0, 0, 0, 0, false, 0, 1 } );
	Check( motion.Take().buttons == 0, "system cancellation drops unsampled button edges" );
	motion.Submit( { 0, 0, 0, 0, false, 0, 0, 2, 1 } );
	Check( motion.Take().impulse == 14 && motion.Take().impulse == 14 && motion.Take().impulse == 13
		&& motion.Take().impulse == -1, "rapid taps each get a separate usercmd impulse sequence" );
	for ( const float density : { 1.0f, 1.5f, 3.0f } ) {
		for ( const float scale : { 0.75f, 1.0f, 1.5f } ) {
			for ( const auto &size : { std::pair<float,float>( 640.0f, 360.0f ), { 720.0f, 360.0f }, { 745.667f, 360.0f },
					{ 360.0f, 745.667f }, { 720.0f, 1280.0f }, { 1280.0f, 800.0f } } ) {
				for ( int preset : { PRESET_DEFAULT, PRESET_COMPACT, PRESET_LEFT_HANDED } ) {
					CheckLayout( size.first, size.second, density, scale, preset );
				}
			}
		}
	}
	const auto inf = std::numeric_limits<float>::infinity();
	const auto nan = std::numeric_limits<float>::quiet_NaN();
	for ( float invalid : { inf, -inf, nan, -1.0f, 0.0f } ) {
		const Layout fallback = ComputeLayout( 720, 360, invalid, invalid );
		Check( fallback.available && std::isfinite( fallback.dp ), "invalid density/scale has finite defaults" );
		const Layout badSize = ComputeLayout( invalid, 360, 1, 1 );
		Check( !badSize.available, "invalid viewport rejected" );
	}
	Check( !ComputeLayout( 32, 32, 1, 1 ).available, "too-small viewport is explicit" );
	const Layout right = ComputeLayout( 720, 360, 1, 1 );
	const Layout left = ComputeLayout( 720, 360, 1, 1, PRESET_LEFT_HANDED );
	Check( left.leftHanded && Near( left.stickHome.x, 720 - right.stickHome.x ), "left-handed movement home mirrored" );
	for ( int i = 0; i < BUTTON_COUNT; ++i ) {
		Check( Near( left.buttons[i].x, 720 - right.buttons[i].x ) && Near( left.buttons[i].y, right.buttons[i].y ),
			"left-handed buttons mirror the fitted layout" );
	}
	Check( ComputeLayout( 720, 360, 1, 1, PRESET_COMPACT ).dp < right.dp, "compact preset reduces control artwork" );
	Check( Near( ComputeLayout( 720, 360, 1, 1, 99 ).dp, right.dp ), "unknown preset falls back to default" );
	Controls handed;
	handed.SetLayout( left );
	const float hx = left.stickHome.x, hy = left.stickHome.y;
	handed.FingerDown( {1, 1}, hx, hy, 0 );
	handed.FingerMove( {1, 1}, hx + 32, hy - 32, 10 );
	auto handedOut = handed.TakeOutput();
	Check( handedOut.moveSide > 0 && handedOut.moveForward > 0 && Near( handedOut.lookX, 0 ),
		"left-handed right thumb moves in physical drag direction" );
	handed.FingerDown( {1, 2}, 80, 120, 10 );
	handed.FingerMove( {1, 2}, 90, 126, 20 );
	handedOut = handed.TakeOutput();
	Check( Near( handedOut.lookX, 10 ) && Near( handedOut.lookY, 6 ), "left thumb looks without axis inversion" );
	handed.SetLayout( right );
	Check( !handed.HasFingers(), "preset change releases existing contacts" );
	handedOut = handed.TakeOutput();
	Check( Near( handedOut.moveSide, 0 ) && Near( handedOut.lookX, 0 ) && !handedOut.attack,
		"preset change clears queued movement and look" );

	Controls controls;
	const Layout layout = ComputeLayout( 720, 360, 1, 1 );
	controls.SetLayout( layout );
	controls.SetVisibleButtons( kAllButtons );
	const auto down = [&]( FingerId id, int button, int64_t time = 0 ) {
		const Circle &at = controls.GetLayout().buttons[button];
		Check( controls.FingerDown( id, at.x, at.y, time ), "control down accepted" );
	};
	const auto up = [&]( FingerId id, int button, int64_t time = 100 ) {
		const Circle &at = controls.GetLayout().buttons[button];
		Check( controls.FingerUp( id, at.x, at.y, time ), "control up accepted" );
	};
	down( {1, 7}, BUTTON_FIRE );
	down( {2, 7}, BUTTON_FIRE );
	up( {1, 7}, BUTTON_FIRE );
	Check( controls.TakeOutput().attack, "device-scoped finger identity retains second owner" );
	controls.FingerCancel( {2, 7} );
	Check( !controls.TakeOutput().attack, "last owner releases fire" );
	down( 1, BUTTON_FIRE );
	up( 1, BUTTON_FIRE );
	Check( controls.TakeOutput().attack && !controls.TakeOutput().attack, "quick tap within one pump produces one attack edge" );
	down( 1, BUTTON_FIRE );
	controls.FingerCancel( 1 );
	Check( !controls.TakeOutput().attack, "cancel before a pump produces no attack edge" );

	down( 1, BUTTON_PAUSE );
	const Circle pause = layout.buttons[BUTTON_PAUSE];
	controls.FingerMove( 1, pause.x + 9, pause.y, 10 );
	up( 1, BUTTON_PAUSE );
	Check( controls.TakeOutput().pause == 0, "dragging away and back never re-arms a tap" );
	down( 1, BUTTON_RELOAD );
	Check( controls.TakeOutput().reload == 0, "reload waits for release" );
	up( 1, BUTTON_RELOAD );
	Check( controls.TakeOutput().reload == 1 && controls.TakeOutput().reload == 0, "reload tap delivered once" );
	down( 1, BUTTON_CHAT );
	controls.FingerCancel( 1 );
	Check( controls.TakeOutput().chat == 0, "system cancellation never opens chat" );

	down( 1, BUTTON_WEAPON );
	down( 2, BUTTON_WEAPON, 100 );
	controls.Update( 301 );
	Check( controls.TakeOutput().weaponWheel, "weapon hold opens wheel" );
	controls.FingerCancel( 1 );
	Check( controls.TakeOutput().weaponWheel, "cancel preserves second weapon owner" );
	controls.FingerCancel( 2 );
	auto output = controls.TakeOutput();
	Check( !output.weaponWheel && output.cancelWeaponWheel && !output.nextWeapon, "last owner cancels without selecting" );
	Check( !controls.TakeOutput().cancelWeaponWheel, "wheel cancellation delivered once" );
	down( 1, BUTTON_WEAPON );
	controls.Update( 301 );
	controls.Reset();
	controls.Reset();
	Check( controls.TakeOutput().cancelWeaponWheel, "repeated resets preserve pending cancellation" );
	down( 1, BUTTON_WEAPON, 500 );
	up( 1, BUTTON_WEAPON, 600 );
	Check( controls.TakeOutput().nextWeapon == 1, "weapon tap advances once" );

	Check( controls.FingerDown( 3, 2, 280, 0 ), "edge stick accepted" );
	output = controls.TakeOutput();
	Check( Near( output.moveSide, 0 ) && Near( output.moveForward, 0 ), "edge touchdown is neutral" );
	controls.FingerMove( 3, 130, 280, 10 );
	Check( Near( controls.TakeOutput().moveSide, 1 ), "full stick deflection" );
	down( 4, BUTTON_FIRE );
	const Circle fire = layout.buttons[BUTTON_FIRE];
	controls.FingerMove( 4, fire.x + 12, fire.y - 6, 20 );
	output = controls.TakeOutput();
	Check( output.attack && Near( output.moveSide, 1 ) && Near( output.lookX, 12 ) && Near( output.lookY, -6 ),
		"independent move, fire and drag-to-look" );
	Check( Near( controls.TakeOutput().lookX, 0 ), "look consumed once, held movement persists" );
	controls.SetLayout( ComputeLayout( 360, 745.667f, 1, 1 ) );
	output = controls.TakeOutput();
	Check( !controls.HasFingers() && !output.attack && Near( output.moveSide, 0 ), "rotation releases every owner" );
	Check( !controls.FingerMove( 4, 200, 200, 30 ), "old owner cannot reappear after rotation" );
	Check( controls.FingerDown( 5, 90, 200, 40 ), "portrait upper view touch" );
	controls.FingerMove( 5, 100, 210, 50 );
	output = controls.TakeOutput();
	Check( Near( output.moveSide, 0 ) && Near( output.lookX, 10 ), "portrait view above deck looks, never moves" );
	controls.Reset();
	controls.SetLayout( layout );
	controls.SetGesturesAllowed( false );
	controls.SetVisibleButtons( 1u << BUTTON_PAUSE );
	down( 6, BUTTON_FIRE );
	controls.FingerMove( 6, 200, 200, 50 );
	output = controls.TakeOutput();
	Check( !output.attack && Near( output.moveSide, 0 ) && Near( output.lookX, 0 ), "cinematic hides and disables gameplay controls" );
	controls.Reset();
	controls.SetGesturesAllowed( true );
	controls.SetVisibleButtons( kAllButtons );
	for ( int i = 0; i < kMaxFingers; ++i ) down( { 3, i }, BUTTON_FIRE );
	Check( !controls.FingerDown( { 4, 99 }, 200, 200, 0 ), "overflow is explicit for the routing owner" );
	controls.FingerMove( { 3, 0 }, nan, inf, 10 );
	Check( controls.TakeOutput().attack, "malformed owner released without cancelling other owners" );
	controls.SetVisibleButtons( 1u << BUTTON_PAUSE );
	Check( !controls.HasFingers() && !controls.TakeOutput().attack, "capability change clears held controls" );

	Mesh mesh;
	BuildOverlay( controls, mesh, true );
	Check( !mesh.vertices.empty() && mesh.indexes.size() % 3 == 0, "cinematic pause mesh exists" );
	for ( const auto index : mesh.indexes ) Check( index < mesh.vertices.size(), "mesh indices valid" );
	for ( const auto &v : mesh.vertices ) Check( std::isfinite( v.x ) && std::isfinite( v.y ), "mesh finite" );
	controls.SetLayout( ComputeLayout( 16, 16, 1, 1 ) );
	BuildOverlay( controls, mesh );
	Check( mesh.vertices.empty(), "unsupported viewport never draws invalid geometry" );
	CheckLayoutEdits();
	std::puts( "PASS: physical-dp layouts, independent fingers, taps, wheel cancellation, rotation, portrait, cinematics, mesh and layout edits" );
}
