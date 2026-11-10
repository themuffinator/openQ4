// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
//

/*
===============================================================================

	VR settings and the null VR system.

	Every engine target registers the vr_* settings so configs stay portable
	between builds. Clients built with OpenXR (OPENQ4_OPENXR) run the
	implementation in src/sys/openxr; everything else, dedicated servers
	included, runs the null system, which never activates.
	See docs/dev/archive/2026-10-18/plans/2026-10-02-openxr-vr.md.

===============================================================================
*/

#include "../idlib/precompiled.h"
#pragma hdrstop

#include "VRSystemLocal.h"

idCVar vr_enable( "vr_enable", "0", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_BOOL,
	"play in virtual reality through the system's OpenXR runtime (OpenGL or Vulkan renderer); applies on vr_restart" );
idCVar vr_renderScale( "vr_renderScale", "1.0", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_FLOAT,
	"multiplier on the headset's recommended per-eye resolution; applies on vr_restart", 0.5f, 2.0f );
idCVar vr_worldScale( "vr_worldScale", "39.37", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_FLOAT,
	"game units per real-world metre", 20.0f, 80.0f );
idCVar vr_aimMode( "vr_aimMode", "1", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_INTEGER,
	"0 = aim with the head, 1 = aim with the weapon-hand controller", 0, 1, idCmdSystem::ArgCompletion_Integer<0,1> );
idCVar vr_leftHanded( "vr_leftHanded", "0", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_BOOL,
	"hold the weapon in the left hand and move with the right stick" );
idCVar vr_moveDirection( "vr_moveDirection", "0", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_INTEGER,
	"0 = the movement stick is relative to the head, 1 = relative to the off-hand controller", 0, 1, idCmdSystem::ArgCompletion_Integer<0,1> );
idCVar vr_turnMode( "vr_turnMode", "0", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_INTEGER,
	"0 = snap turn, 1 = smooth turn with the weapon-hand stick", 0, 1, idCmdSystem::ArgCompletion_Integer<0,1> );
idCVar vr_snapTurnAngle( "vr_snapTurnAngle", "45", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_FLOAT,
	"degrees per snap turn", 10.0f, 90.0f );
idCVar vr_smoothTurnSpeed( "vr_smoothTurnSpeed", "120", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_FLOAT,
	"smooth turn speed in degrees per second", 30.0f, 360.0f );
idCVar vr_stickDeadzone( "vr_stickDeadzone", "0.2", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_FLOAT,
	"thumbstick deadzone for movement and smooth turning", 0.0f, 0.9f );
idCVar vr_headOffsetLimit( "vr_headOffsetLimit", "16", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_FLOAT,
	"how far the tracked head may move horizontally away from the player's body, in game units", 0.0f, 64.0f );
idCVar vr_hudDistance( "vr_hudDistance", "1.4", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_FLOAT,
	"distance of the head-locked HUD in metres", 0.5f, 5.0f );
idCVar vr_hudWidth( "vr_hudWidth", "1.2", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_FLOAT,
	"width of the head-locked HUD in metres", 0.3f, 4.0f );
idCVar vr_hudHeightOffset( "vr_hudHeightOffset", "-0.1", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_FLOAT,
	"vertical offset of the head-locked HUD from eye level in metres", -1.0f, 1.0f );
idCVar vr_screenDistance( "vr_screenDistance", "2.5", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_FLOAT,
	"distance of the virtual screen for menus, loading and cinematics in metres", 0.8f, 10.0f );
idCVar vr_screenWidth( "vr_screenWidth", "3.0", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_FLOAT,
	"width of the virtual screen in metres", 0.5f, 12.0f );
idCVar vr_mirror( "vr_mirror", "1", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_INTEGER,
	"desktop window while in VR: 0 = black, 1 = left eye, 2 = virtual screen", 0, 2, idCmdSystem::ArgCompletion_Integer<0,2> );
idCVar vr_weaponOffsetX( "vr_weaponOffsetX", "0", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_FLOAT,
	"moves the held view weapon along the controller's aim, in game units (0 holds it by its own grip)" );
idCVar vr_weaponOffsetY( "vr_weaponOffsetY", "0", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_FLOAT,
	"moves the held view weapon to the controller's left, in game units" );
idCVar vr_weaponOffsetZ( "vr_weaponOffsetZ", "0", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_FLOAT,
	"moves the held view weapon up, in game units" );
idCVar vr_weaponPitch( "vr_weaponPitch", "0", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_FLOAT,
	"view weapon pitch relative to the controller's aim, in degrees (positive tilts down)", -60.0f, 60.0f );
idCVar vr_comfortVignette( "vr_comfortVignette", "0.5", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_FLOAT,
	"comfort vignette while the stick moves or smoothly turns you: 0 off, up to 1 for a black edge and a 60 degree clear centre", 0.0f, 1.0f );
idCVar vr_physicalCrouch( "vr_physicalCrouch", "1", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_BOOL,
	"1 = crouching in your room (your head 40 cm below where you recentred) crouches your character" );
idCVar vr_twoHanded( "vr_twoHanded", "1", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_BOOL,
	"1 = squeezing the off-hand grip on the gun's foregrip, in front of the weapon hand, holds the gun in both hands; elsewhere that squeeze opens the weapon wheel" );
idCVar vr_vehicleStereo( "vr_vehicleStereo", "1", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_BOOL,
	"single player vehicles and turrets: 1 = in stereo around your head, the turret following your aim; 0 = flat on the floating screen" );
idCVar vr_hudOnSight( "vr_hudOnSight", "0", CVAR_SYSTEM | CVAR_BOOL | CVAR_ROM,
	"set by the game each frame: the head aims (a vehicle's turret follows it), so the HUD hangs on the line of sight as in head aim" );
idCVar vr_roomScale( "vr_roomScale", "1", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_BOOL,
	"1 = the body walks after your head as you move about the room (in multiplayer through its movement input); 0 = it stays put and leaning stops at vr_headOffsetLimit" );
idCVar vr_hapticStrength( "vr_hapticStrength", "1", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_FLOAT,
	"controller vibration strength, from 0 (off) to 1", 0.0f, 1.0f );
idCVar vr_aimLaser( "vr_aimLaser", "1", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_INTEGER,
	"while aiming with the controller: 0 = no marker, 1 = a dot where the shot lands, 2 = the dot and a laser beam", 0, 2, idCmdSystem::ArgCompletion_Integer<0,2> );
idCVar vr_playSpace( "vr_playSpace", "0", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_INTEGER,
	"0 = standing when the headset knows your floor, else seated; 1 = seated, heights from where you recentred; "
	"2 = standing, heights from the floor (seated when the runtime knows no floor)", 0, 2, idCmdSystem::ArgCompletion_Integer<0,2> );
idCVar vr_standingHeight( "vr_standingHeight", "0", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_FLOAT,
	"standing play: your eye height above the floor in metres, which your character's eye stands for; 0 = learn it at "
	"the next recentre (vr_calibrateHeight measures it now)", 0.0f, 2.5f );
idCVar vr_pauseOnFocusLoss( "vr_pauseOnFocusLoss", "1", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_BOOL,
	"single player: 1 = open the pause menu when the headset comes off or its own menu takes the controllers" );
idCVar vr_debug( "vr_debug", "0", CVAR_SYSTEM | CVAR_INTEGER,
	"1 = log OpenXR session and frame events, 2 = also log every submitted frame", 0, 2, idCmdSystem::ArgCompletion_Integer<0,2> );

/*
===============================================================================

	idVRSystemNull

===============================================================================
*/

class idVRSystemNull : public idVRSystem {
public:
	virtual void			Init( void ) {}
	virtual void			Shutdown( void ) {}
	virtual void			RendererStarted( void ) {}
	virtual void			RendererStopping( void ) {}
	virtual void			BeginFrame( void ) {}
	virtual void			EndFrame( void ) {}
	virtual bool			IsPacing( void ) const { return false; }
	virtual bool			IsActive( void ) const { return false; }
	virtual void			GetFrameState( vrFrameState_t &state ) const {
		memset( &state, 0, sizeof( state ) );
	}
	virtual bool			GetUsercmdInput( float otherYawDelta, float currentYaw, vrUsercmdInput_t &input ) {
		(void)otherYawDelta;
		(void)currentYaw;
		memset( &input, 0, sizeof( input ) );
		return false;
	}
	virtual void			DrawMenuPointer( void ) {}
	virtual void			Vibrate( int hand, float amplitude, int durationMsec ) {
		(void)hand;
		(void)amplitude;
		(void)durationMsec;
	}
	virtual void			ShiftTrackingOrigin( const idVec3 &trackingDelta ) {
		(void)trackingDelta;
	}
};

#if defined( OPENQ4_OPENXR )
idVRSystem *vrSystem = VR_GetOpenXRSystem();
#else
static idVRSystemNull vrSystemNull;
idVRSystem *vrSystem = &vrSystemNull;

// without OpenXR the Vulkan renderer always creates its own instance and device
bool VR_CreateVulkanInstance( void *getInstanceProcAddr, const void *createInfo, void **outInstance, int *outResult ) {
	(void)getInstanceProcAddr;
	(void)createInfo;
	(void)outInstance;
	(void)outResult;
	return false;
}

bool VR_GetVulkanPhysicalDevice( void *instance, void **outPhysicalDevice ) {
	(void)instance;
	(void)outPhysicalDevice;
	return false;
}

bool VR_CreateVulkanDevice( void *getInstanceProcAddr, void *physicalDevice, const void *createInfo, void **outDevice, int *outResult ) {
	(void)getInstanceProcAddr;
	(void)physicalDevice;
	(void)createInfo;
	(void)outDevice;
	(void)outResult;
	return false;
}

// without OpenXR no session ever has the controllers to lose
bool VR_ConsumeFocusLoss( void ) {
	return false;
}

void VR_UsercmdAnglesCleared( void ) {
}
#endif

bool VR_HasInputFocus( void ) {
	if ( vrSystem == NULL ) {
		return false;
	}
	vrFrameState_t state;
	vrSystem->GetFrameState( state );
	return state.active && state.focused;
}
