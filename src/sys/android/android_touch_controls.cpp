// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.

/*
===============================================================================

	Built-in touch gameplay controls for the standalone Android app: the
	engine half.

	android_touch_controls.h is the layout, the per-finger state machine and
	the overlay's geometry. This file decides when the controls own the
	screen, offers them SDL's finger events (SDL3_HandleFingerEvent), applies
	their output through a separate usercmd motion channel and named actions
	once per event pump, and draws the overlay with the GUI model from
	idSessionLocal::Draw. All of it runs on the engine thread.

	The controls own the screen only while a level is being played: no menu,
	message box, console, loading screen, retained screen or demo. Everywhere
	else a finger stays the menu pointer SDL3_HandleFingerEvent makes of it.

	A Sigma Touch build keeps its host's existing bridge and overlay; there
	this file is only the empty entry points at the bottom.

===============================================================================
*/

#include "../../idlib/precompiled.h"
#include "android_public.h"
#include "../posix/posix_public.h"

#if !defined( OPENQ4_SIGMATOUCH )

#include "../../framework/Session_local.h"
#include "../../ui/RetainedUI.h"
#include "android_touch_controls.h"
#include "android_interface_settings.h"
#include <SDL3/SDL_system.h>
#include <jni.h>

static idCVar in_touchControls( "in_touchControls", "1", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_BOOL,
	"show the built-in touch gameplay controls while a level is played; a controller, keyboard or mouse press hides them until the next touch" );
static idCVar in_touchControlsScale( "in_touchControlsScale", "1", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_FLOAT,
	"size of the touch gameplay controls", 0.75f, 1.5f );
static idCVar in_touchLayoutPreset( "in_touchLayoutPreset", "0", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_INTEGER,
	"touch layout: 0 default, 1 compact, 2 left-handed", 0, 2 );
static idCVar in_touchLookSensitivity( "in_touchLookSensitivity", "1", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_FLOAT,
	"touch drag-to-look speed: 1 turns a quarter of a degree for each dp dragged", 0.1f, 4.0f );
static idCVar in_touchLayout( "in_touchLayout", "", CVAR_SYSTEM | CVAR_ARCHIVE,
	"the touch layout editor's landscape and portrait edits (android_touch_controls.h); empty uses the preset" );
static idCVar ui_radialTouchActive( "ui_radialTouchActive", "0", CVAR_SYSTEM | CVAR_BOOL | CVAR_ROM | CVAR_NOCHEAT,
	"the radial selector is steered by a touch gesture" );
static idCVar ui_radialTouchX( "ui_radialTouchX", "0", CVAR_SYSTEM | CVAR_FLOAT | CVAR_ROM | CVAR_NOCHEAT,
	"touch radial selector horizontal position" );
static idCVar ui_radialTouchY( "ui_radialTouchY", "0", CVAR_SYSTEM | CVAR_FLOAT | CVAR_ROM | CVAR_NOCHEAT,
	"touch radial selector vertical position" );
static idCVar ui_touchCanReload( "ui_touchCanReload", "0", CVAR_SYSTEM | CVAR_BOOL | CVAR_ROM | CVAR_NOCHEAT,
	"the local player's current weapon supports reloading" );
static idCVar ui_touchCanZoom( "ui_touchCanZoom", "0", CVAR_SYSTEM | CVAR_BOOL | CVAR_ROM | CVAR_NOCHEAT,
	"the local player's current weapon supports zoom" );

// in_touchLookSensitivity 1: a 400 dp drag, about half a phone's width, turns 100 degrees.
static const float TOUCH_LOOK_DEGREES_PER_DP = 0.25f;

static openq4::touch::Controls	touchControls;
static openq4::touch::Contacts	touchContacts;
static openq4::touch::Mesh		touchMesh;
static idList<idDrawVert>		touchDrawVerts;
static idList<glIndex_t>		touchDrawIndexes;
static float					touchDisplayScale = 1.0f;
static float					touchWheelX = 0.0f, touchWheelY = 0.0f;
static bool						touchWheelOwned = false;
static float					touchPlayerScale = -1.0f;
static int						touchPlayerPreset = -1;
static idStr					touchLayoutRecord;		// in_touchLayout as last parsed
static openq4::touch::OrientationEdits	touchLayoutEdits;
static bool						touchLayoutValid = false;
static bool						touchAppActive = false;
static bool						touchPosted = false;	// some input reached the bridge since the last let-go
static bool						touchOverlayHidden = false;

// Archived beside ui_retainedScale/ui_retainedTextScale so a restart, which
// reloads both from the config, never re-applies an already applied record.
static idCVar ui_androidInterfaceApplied("ui_androidInterfaceApplied", "", CVAR_SYSTEM | CVAR_ARCHIVE,
	"last Android interface setup transaction applied to the game");

// Java resolves the viewing profile (ui-visual-design.md 13.2) and the system
// font scale into the retained UI and text scales. Apply each new transaction
// once on foreground entry, so later in-game size changes survive a return.
static void Interface_LoadSetup(void) {
	JNIEnv* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
	if (!env) return;
	jobject activity = static_cast<jobject>(SDL_GetAndroidActivity());
	if (!activity) return;
	jclass type = env->GetObjectClass(activity);
	jmethodID getter = type ? env->GetMethodID(type, "getInterfaceSetup", "()Ljava/lang/String;") : NULL;
	jstring record = getter ? static_cast<jstring>(env->CallObjectMethod(activity, getter)) : NULL;
	openq4::android::InterfaceSetup settings;
	bool pending = false;
	if (!env->ExceptionCheck() && record && env->GetStringUTFLength(record) <= 48) {
		const char* bytes = env->GetStringUTFChars(record, NULL);
		if (bytes) {
			pending = openq4::android::PendingInterfaceSetup(bytes, ui_androidInterfaceApplied.GetString(), settings);
			env->ReleaseStringUTFChars(record, bytes);
		}
	}
	if (env->ExceptionCheck()) {
		env->ExceptionClear(); pending = false;
		common->Warning("Could not read Android interface setup");
	}
	if (record) env->DeleteLocalRef(record);
	if (type) env->DeleteLocalRef(type);
	env->DeleteLocalRef(activity);
	if (!pending) return;
	cvarSystem->SetCVarFloat("ui_retainedScale", settings.scale);
	cvarSystem->SetCVarFloat("ui_retainedTextScale", settings.textScale);
	ui_androidInterfaceApplied.SetString(settings.revision);
	common->Printf("ANDROID_INTERFACE_SETUP profile=%d uiScale=%.2f textScale=%.2f\n",
		settings.profile, settings.scale, settings.textScale);
}

// The layout editor's applied record, read with the touch settings it was
// applied with. A malformed record never replaces the engine's last good one.
static void TouchControls_LoadLayout( void ) {
	JNIEnv *env = static_cast<JNIEnv *>( SDL_GetAndroidJNIEnv() );
	if ( env == NULL ) return;
	jobject activity = static_cast<jobject>( SDL_GetAndroidActivity() );
	if ( activity == NULL ) return;
	jclass type = env->GetObjectClass( activity );
	jmethodID getter = type != NULL ? env->GetMethodID( type, "getTouchLayout", "()Ljava/lang/String;" ) : NULL;
	jstring record = getter != NULL ? static_cast<jstring>( env->CallObjectMethod( activity, getter ) ) : NULL;
	idStr value;
	bool valid = false;
	if ( !env->ExceptionCheck() && record != NULL && env->GetStringUTFLength( record ) < openq4::touch::kEditRecordLimit ) {
		const char *bytes = env->GetStringUTFChars( record, NULL );
		if ( bytes != NULL ) {
			openq4::touch::OrientationEdits edits;
			valid = openq4::touch::ParseLayoutEdits( bytes, edits );
			value = bytes;
			env->ReleaseStringUTFChars( record, bytes );
		}
	}
	if ( env->ExceptionCheck() ) {
		env->ExceptionClear();
		valid = false;
		common->Warning( "Could not read Android touch layout" );
	}
	if ( record != NULL ) env->DeleteLocalRef( record );
	if ( type != NULL ) env->DeleteLocalRef( type );
	env->DeleteLocalRef( activity );
	if ( valid ) in_touchLayout.SetString( value.c_str() );
}

// Java owns the standalone setup preference. Read its one atomic applied
// record on startup/foreground only, after SDL has restored the Activity.
// Drafts never reach this boundary; CVar and input changes stay on this thread.
static void TouchControls_LoadSetup( void ) {
	JNIEnv *env = static_cast<JNIEnv *>( SDL_GetAndroidJNIEnv() );
	if ( env == NULL ) return;
	jobject activity = static_cast<jobject>( SDL_GetAndroidActivity() );
	if ( activity == NULL ) return;
	jclass type = env->GetObjectClass( activity );
	jmethodID getter = type != NULL ? env->GetMethodID( type, "getTouchSetup", "()[F" ) : NULL;
	jfloatArray values = getter != NULL ? static_cast<jfloatArray>( env->CallObjectMethod( activity, getter ) ) : NULL;
	jfloat settings[3] = {};
	bool valid = !env->ExceptionCheck() && values != NULL && env->GetArrayLength( values ) == 3;
	if ( valid ) {
		env->GetFloatArrayRegion( values, 0, 3, settings );
		valid = !env->ExceptionCheck();
	}
	if ( env->ExceptionCheck() ) {
		env->ExceptionClear();
		common->Warning( "Could not read Android touch setup" );
	}
	if ( values != NULL ) env->DeleteLocalRef( values );
	if ( type != NULL ) env->DeleteLocalRef( type );
	env->DeleteLocalRef( activity );
	if ( !valid || !std::isfinite( settings[0] ) || settings[0] < 0 || settings[0] > 2
			|| settings[0] != std::floor( settings[0] ) || !std::isfinite( settings[1] )
			|| settings[1] < 0.1f || settings[1] > 4.0f || !std::isfinite( settings[2] )
			|| settings[2] < 0.75f || settings[2] > 1.5f ) return;
	in_touchLayoutPreset.SetInteger( static_cast<int>( settings[0] ) );
	in_touchLookSensitivity.SetFloat( settings[1] );
	in_touchControlsScale.SetFloat( settings[2] );
	TouchControls_LoadLayout();
}

/*
====================
TouchControls_Active

The same test idSessionLocal::Draw uses for the frame it draws the overlay
on, plus everything that takes the screen from it without being a session
GUI.
====================
*/
static bool TouchControls_Active( void ) {
	if ( !touchAppActive || !in_touchControls.GetBool() || !cvarSystem->GetCVarBool( "in_touchscreen" ) || session == NULL ) {
		return false;
	}
	if ( !sessLocal.mapSpawned || sessLocal.insideExecuteMapChange || sessLocal.guiActive != NULL
			|| sessLocal.guiTest != NULL || sessLocal.IsDemoPlaybackActive() ) {
		return false;
	}
	if ( ( console != NULL && console->Active() ) || RetainedUI_IsOpen() || openQ4_AcceptingLoadingContinueInput() ) {
		return false;
	}
	return true;
}

static bool TouchControls_InCinematic( void ) {
	return game != NULL && game->InCinematic();
}

/*
====================
TouchControls_LetGo

Every finger, held action and stick level, including look deltas queued for
the next usercmd.
====================
*/
static void TouchControls_LetGo( void ) {
	touchControls.Reset();
	if ( touchWheelOwned ) cvarSystem->SetCVarBool( "ui_radialCancel", true );
	touchWheelOwned = false;
	touchWheelX = touchWheelY = 0;
	ui_radialTouchActive.SetBool( false );
	Sys_ResetTouchMotion();
	touchPosted = false;
}

/*
====================
TouchControls_Apply

Movement and look use their own usercmd channel. Physical controller enable,
mouse CPI/acceleration/smoothing and mouse pitch settings have no effect.
The radial selector receives position directly, without turning the view.
====================
*/
static void TouchControls_Apply( const openq4::touch::Output &out ) {
	if ( out.cancelWeaponWheel ) cvarSystem->SetCVarBool( "ui_radialCancel", true );
	if ( out.weaponWheel && !touchWheelOwned ) {
		touchWheelOwned = true;
		touchWheelX = touchWheelY = 0;
	}
	const bool wheelInput = touchWheelOwned;
	if ( wheelInput ) {
		// 96 dp from the hold point reaches the edge of the selector.
		touchWheelX = idMath::ClampFloat( -1.0f, 1.0f, touchWheelX + out.lookX / touchDisplayScale / 96.0f );
		touchWheelY = idMath::ClampFloat( -1.0f, 1.0f, touchWheelY + out.lookY / touchDisplayScale / 96.0f );
		ui_radialTouchX.SetFloat( touchWheelX );
		ui_radialTouchY.SetFloat( touchWheelY );
	}
	ui_radialTouchActive.SetBool( wheelInput );
	const float requestedSensitivity = in_touchLookSensitivity.GetFloat();
	const float sensitivity = std::isfinite( requestedSensitivity ) ? idMath::ClampFloat( 0.1f, 4.0f, requestedSensitivity ) : 1.0f;
	const float degreesPerPixel = TOUCH_LOOK_DEGREES_PER_DP * sensitivity / touchDisplayScale;
	const unsigned buttons = ( out.attack ? 1u << QUAKE4_BTN_ATTACK : 0 ) |
		( out.jump ? 1u << QUAKE4_BTN_MOVE_UP : 0 ) | ( out.crouch ? 1u << QUAKE4_BTN_MOVE_DOWN : 0 ) |
		( out.zoom ? 1u << QUAKE4_BTN_ZOOM : 0 ) | ( out.weaponWheel ? 1u << QUAKE4_BTN_WEAPON_WHEEL : 0 ) |
		( out.scores ? 1u << QUAKE4_BTN_SCORES : 0 );
	const int controlButtons[] = { openq4::touch::BUTTON_FIRE, openq4::touch::BUTTON_ZOOM,
		openq4::touch::BUTTON_JUMP, openq4::touch::BUTTON_CROUCH, -1, -1,
		openq4::touch::BUTTON_WEAPON, openq4::touch::BUTTON_SCORES };
	unsigned canceled = 0;
	for ( int i = 0; i < QUAKE4_BTN_COUNT; ++i ) {
		if ( controlButtons[i] >= 0 && ( out.canceledButtons & ( 1u << controlButtons[i] ) ) ) canceled |= 1u << i;
	}
	Sys_SetTouchMotion( out.moveSide, out.moveForward, wheelInput ? 0.0f : -out.lookX * degreesPerPixel,
		wheelInput ? 0.0f : out.lookY * degreesPerPixel, touchControls.HasFingers(), buttons, canceled, out.nextWeapon, out.reload );
	touchPosted = true;
	if ( !out.weaponWheel && cvarSystem->GetCVarInteger( "ui_radialOwner" ) != 1 ) touchWheelOwned = false;

	if ( out.pause > 0 ) {
		// Escape is the pause menu, and during a cinematic the skip.
		Posix_QueEvent( SE_KEY, K_ESCAPE, 1, 0, NULL );
		Posix_QueEvent( SE_KEY, K_ESCAPE, 0, 0, NULL );
	}
	if ( out.chat > 0 ) {
		// The in-game chat line, which brings up the on-screen keyboard. Inserted
		// rather than appended so that text already queued,
		// such as a script waiting out a `wait`, cannot hold the tap back.
		cmdSystem->BufferCommandText( CMD_EXEC_INSERT, "clientMessageMode\n" );
	}
}

void AndroidTouchControls_Frame( int windowWidth, int windowHeight, float displayScale, bool appActive ) {
	if ( appActive && !touchAppActive ) {
		TouchControls_LoadSetup();
		Interface_LoadSetup();
	}
	touchAppActive = appActive;
	const openq4::touch::Layout &previous = touchControls.GetLayout();
	const float requestedScale = in_touchControlsScale.GetFloat();
	const float scale = std::isfinite( requestedScale ) ? idMath::ClampFloat( 0.75f, 1.5f, requestedScale ) : 1.0f;
	const int preset = in_touchLayoutPreset.GetInteger();
	const float density = std::isfinite( displayScale ) && displayScale >= 0.25f && displayScale <= 16.0f ? displayScale : 1.0f;
	const bool layoutChanged = touchLayoutRecord.Cmp( in_touchLayout.GetString() ) != 0;
	if ( layoutChanged ) {
		touchLayoutRecord = in_touchLayout.GetString();
		touchLayoutEdits = openq4::touch::OrientationEdits();
		touchLayoutValid = touchLayoutRecord.Length() > 0
			&& openq4::touch::ParseLayoutEdits( touchLayoutRecord.c_str(), touchLayoutEdits );
		if ( touchLayoutRecord.Length() > 0 && !touchLayoutValid ) {
			common->Warning( "in_touchLayout is not a valid touch layout; using the preset" );
		}
	}
	if ( layoutChanged || previous.width != windowWidth || previous.height != windowHeight || density != touchDisplayScale
			|| scale != touchPlayerScale || preset != touchPlayerPreset ) {
		TouchControls_LetGo();
		touchDisplayScale = density;
		touchPlayerScale = scale;
		touchPlayerPreset = preset;
		const float width = static_cast<float>( windowWidth ), height = static_cast<float>( windowHeight );
		const openq4::touch::Layout layout = openq4::touch::ComputeLayout( width, height, density, scale, preset,
			touchLayoutValid ? &touchLayoutEdits.For( width, height ) : NULL );
		if ( layout.editsRejected ) {
			common->Printf( "Touch layout edits do not fit a %dx%d window; using the preset\n", windowWidth, windowHeight );
		}
		touchControls.SetLayout( layout );
	}
	const bool cinematic = TouchControls_InCinematic();
	unsigned visible = session != NULL && session->IsMultiplayer() ? openq4::touch::kAllButtons : openq4::touch::kSinglePlayerButtons;
	if ( !ui_touchCanReload.GetBool() ) visible &= ~( 1u << openq4::touch::BUTTON_RELOAD );
	if ( !ui_touchCanZoom.GetBool() ) visible &= ~( 1u << openq4::touch::BUTTON_ZOOM );
	visible &= ~touchControls.GetLayout().hidden;
	touchControls.SetVisibleButtons( cinematic ? ( 1u << openq4::touch::BUTTON_PAUSE ) : visible );
	touchControls.SetGesturesAllowed( !cinematic );
	if ( !TouchControls_Active() || !touchControls.GetLayout().available ) {
		if ( touchPosted || touchControls.HasFingers() || touchWheelOwned ) {
			TouchControls_LetGo();
		}
		Sys_ResetTouchMotion();
		return;
	}
	touchControls.Update( Sys_Milliseconds() );
	TouchControls_Apply( touchControls.TakeOutput() );
}

bool AndroidTouchControls_HandleFinger( androidTouchPhase_t phase, long long device, long long finger, float x, float y ) {
	const openq4::touch::FingerId id( device, finger );
	const bool active = TouchControls_Active();
	const auto owner = phase == ANDROID_TOUCH_DOWN ? touchContacts.Begin( id, active ) : touchContacts.Get( id );
	if ( phase == ANDROID_TOUCH_UP || phase == ANDROID_TOUCH_CANCEL ) touchContacts.End( id );
	if ( owner == openq4::touch::CONTACT_MENU ) return false;
	if ( owner != openq4::touch::CONTACT_GAME ) return true;
	if ( !active || !touchControls.GetLayout().available ) { TouchControls_LetGo(); return true; }
	const openq4::touch::Layout &layout = touchControls.GetLayout();
	const float px = x * layout.width;
	const float py = y * layout.height;
	const int64_t now = Sys_Milliseconds();
	switch ( phase ) {
		case ANDROID_TOUCH_DOWN:
			touchOverlayHidden = false;
			touchControls.FingerDown( id, px, py, now );
			break;
		case ANDROID_TOUCH_MOVE:
			touchControls.FingerMove( id, px, py, now );
			break;
		case ANDROID_TOUCH_UP:
			touchControls.FingerUp( id, px, py, now );
			break;
		case ANDROID_TOUCH_CANCEL:
			touchControls.FingerCancel( id );
			break;
	}
	// Overflow, unknown and cancelled gameplay fingers never fall through to
	// the menu pointer. A fresh DOWN decides ownership for the next contact.
	return true;
}

void AndroidTouchControls_OtherInput( void ) {
	touchOverlayHidden = true;
	TouchControls_LetGo();
}

void AndroidTouchControls_Release( void ) {
	TouchControls_LetGo();
	touchContacts.Clear();
	touchAppActive = false;
}

float AndroidTouchControls_HudTop( void ) {
	if ( touchOverlayHidden || !TouchControls_Active() ) return 0.0f;
	return openq4::touch::HudTop( touchControls.GetLayout(), touchControls.VisibleButtons() );
}

float AndroidTouchControls_HudBottomEdge( void ) {
	if ( touchOverlayHidden || !TouchControls_Active() ) return 0.0f;
	return openq4::touch::HudBottomEdge( touchControls.GetLayout() );
}

/*
====================
AndroidTouchControls_Draw

Called over a level being played. The mesh is in window pixels and goes out
in the GUI model's 640x480 frame across the whole window, not the UI viewport
the HUD keeps to, so the controls reach the screen's corners on any aspect
ratio. _retainedSolid is the retained UI's vertex-coloured fill, which blends
premultiplied.
====================
*/
void AndroidTouchControls_Draw( void ) {
	if ( touchOverlayHidden || !TouchControls_Active() ) {
		return;
	}
	// The selector occupies the trailing thumb area. Its gesture keeps the
	// contact, but the ordinary control plates must not cover its slots.
	const bool pauseOnly = TouchControls_InCinematic() || touchWheelOwned
		|| cvarSystem->GetCVarInteger( "ui_radialOwner" ) == 1;
	openq4::touch::BuildOverlay( touchControls, touchMesh, pauseOnly );
	const int numVerts = static_cast<int>( touchMesh.vertices.size() );
	const int numIndexes = static_cast<int>( touchMesh.indexes.size() );
	const idMaterial *material = declManager->FindMaterial( "_retainedSolid" );
	if ( numVerts == 0 || numIndexes == 0 || material == NULL ) {
		return;
	}

	const openq4::touch::Layout &layout = touchControls.GetLayout();
	const float scaleX = static_cast<float>( SCREEN_WIDTH ) / layout.width;
	const float scaleY = static_cast<float>( SCREEN_HEIGHT ) / layout.height;
	touchDrawVerts.SetNum( numVerts, false );
	for ( int i = 0; i < numVerts; i++ ) {
		const openq4::touch::Vertex &source = touchMesh.vertices[i];
		idDrawVert &v = touchDrawVerts[i];
		v.Clear();
		v.xyz.Set( source.x * scaleX, source.y * scaleY, 0.0f );
		v.st.Set( 0.5f, 0.5f );
		v.normal.Set( 0.0f, 0.0f, 1.0f );
		v.tangents[0].Set( 1.0f, 0.0f, 0.0f );
		v.tangents[1].Set( 0.0f, 1.0f, 0.0f );
		const int alpha = source.rgba[3];
		for ( int channel = 0; channel < 3; channel++ ) {
			v.color[channel] = static_cast<byte>( ( source.rgba[channel] * alpha + 127 ) / 255 );
		}
		v.color[3] = static_cast<byte>( alpha );
		v.color2[0] = v.color2[1] = v.color2[2] = v.color2[3] = 255;
	}
	touchDrawIndexes.SetNum( numIndexes, false );
	for ( int i = 0; i < numIndexes; i++ ) {
		touchDrawIndexes[i] = static_cast<glIndex_t>( touchMesh.indexes[i] );
	}

	const bool previousUIViewportMode = renderSystem->GetUseUIViewportFor2D();
	renderSystem->SetUseUIViewportFor2D( false );
	renderSystem->SetColor4( 1.0f, 1.0f, 1.0f, 1.0f );
	renderSystem->DrawStretchPic( touchDrawVerts.Ptr(), touchDrawIndexes.Ptr(), numVerts, numIndexes, material, false );
	renderSystem->SetUseUIViewportFor2D( previousUIViewportMode );
}

/*
====================
TouchControls_Test_f

Drives the controls the way SDL's finger events do, for headless tests on
an emulator: nothing reaches the operating system's input.
====================
*/
static const char *const touchControlNames[openq4::touch::BUTTON_COUNT] = {
	"fire", "jump", "crouch", "weapon", "reload", "zoom", "pause", "scores", "chat"
};

static void TouchControls_Test_f( const idCmdArgs &args ) {
	const char *verb = args.Argv( 1 );
	const openq4::touch::Layout &layout = touchControls.GetLayout();
	if ( idStr::Icmp( verb, "layout" ) == 0 ) {
		common->Printf( "touch controls: %s, %.0fx%.0f px, %.3f px per dp%s\n",
			TouchControls_Active() ? "active" : "inactive", layout.width, layout.height, layout.dp,
			layout.edited ? ", edited" : layout.editsRejected ? ", edits rejected" : "" );
		for ( int i = 0; i < openq4::touch::BUTTON_COUNT; i++ ) {
			const openq4::touch::Circle &button = layout.buttons[i];
			common->Printf( "  %-6s at %.3f %.3f, radius %.3f, opacity %.2f%s\n", touchControlNames[i],
				button.x / layout.width, button.y / layout.height, button.radius / layout.width,
				openq4::touch::RestOpacity( layout, i ), ( layout.hidden & ( 1u << i ) ) ? ", hidden" : "" );
		}
		return;
	}
	if ( idStr::Icmp( verb, "state" ) == 0 ) {
		idStr pressed;
		for ( int i = 0; i < openq4::touch::BUTTON_COUNT; i++ ) {
			if ( touchControls.ButtonPressed( i ) ) {
				pressed += va( " %s", touchControlNames[i] );
			}
		}
		float baseX, baseY, knobX, knobY;
		const bool stick = touchControls.StickState( baseX, baseY, knobX, knobY );
		common->Printf( "touch controls: %s%s, pressed:%s, stick %s\n", TouchControls_Active() ? "active" : "inactive",
			touchOverlayHidden ? " (hidden)" : "", pressed.Length() ? pressed.c_str() : " none",
			stick ? va( "at %.3f %.3f knob %.3f %.3f", baseX / layout.width, baseY / layout.height,
				knobX / layout.width, knobY / layout.height ) : "free" );
		return;
	}

	androidTouchPhase_t phase;
	if ( idStr::Icmp( verb, "down" ) == 0 ) {
		phase = ANDROID_TOUCH_DOWN;
	} else if ( idStr::Icmp( verb, "move" ) == 0 ) {
		phase = ANDROID_TOUCH_MOVE;
	} else if ( idStr::Icmp( verb, "up" ) == 0 ) {
		phase = ANDROID_TOUCH_UP;
	} else if ( idStr::Icmp( verb, "cancel" ) == 0 ) {
		phase = ANDROID_TOUCH_CANCEL;
	} else {
		idStr names;
		for ( int i = 0; i < openq4::touch::BUTTON_COUNT; i++ ) {
			names += i == 0 ? "" : ( i + 1 == openq4::touch::BUTTON_COUNT ? " or " : ", " );
			names += touchControlNames[i];
		}
		common->Printf( "usage: touchControlsTest layout | state\n"
			"       touchControlsTest down|move|up|cancel <finger> <x> <y>    (x and y from 0 to 1 across the window)\n"
			"       touchControlsTest down|move|up|cancel <finger> <control>  (%s)\n", names.c_str() );
		return;
	}
	const long long finger = atoi( args.Argv( 2 ) );
	float x = 0.0f;
	float y = 0.0f;
	if ( args.Argc() >= 5 ) {
		x = atof( args.Argv( 3 ) );
		y = atof( args.Argv( 4 ) );
	} else {
		int control = 0;
		while ( control < openq4::touch::BUTTON_COUNT && idStr::Icmp( args.Argv( 3 ), touchControlNames[control] ) != 0 ) {
			control++;
		}
		if ( control == openq4::touch::BUTTON_COUNT ) {
			common->Printf( "touchControlsTest: no control named '%s'\n", args.Argv( 3 ) );
			return;
		}
		x = layout.buttons[control].x / layout.width;
		y = layout.buttons[control].y / layout.height;
	}
	const bool taken = AndroidTouchControls_HandleFinger( phase, -1, finger, x, y );
	common->Printf( "touchControlsTest: %s finger %lld at %.3f %.3f %s\n", verb, finger, x, y,
		taken ? "taken by the controls" : "not taken (the controls are not active, or do not know the finger)" );
}

void AndroidTouchControls_Init( void ) {
	// Not a cheat: it reaches nothing a finger on the screen cannot, as a bound
	// key's +attack does not, and multiplayer is where the scoreboard and chat
	// controls need testing.
	cmdSystem->AddCommand( "touchControlsTest", TouchControls_Test_f, CMD_FL_SYSTEM,
		"drives the built-in touch controls like finger events, for headless tests" );
}

#else	// OPENQ4_SIGMATOUCH: the host's overlay drives the bridge itself

void AndroidTouchControls_Init( void ) {
}

void AndroidTouchControls_Frame( int windowWidth, int windowHeight, float displayScale, bool appActive ) {
	(void)windowWidth;
	(void)windowHeight;
	(void)displayScale;
	(void)appActive;
}

bool AndroidTouchControls_HandleFinger( androidTouchPhase_t phase, long long device, long long finger, float x, float y ) {
	(void)phase;
	(void)device;
	(void)finger;
	(void)x;
	(void)y;
	return false;
}

void AndroidTouchControls_OtherInput( void ) {
}

void AndroidTouchControls_Release( void ) {
}

void AndroidTouchControls_Draw( void ) {
}

float AndroidTouchControls_HudTop( void ) {
	return 0.0f;
}

float AndroidTouchControls_HudBottomEdge( void ) {
	return 0.0f;
}

#endif
