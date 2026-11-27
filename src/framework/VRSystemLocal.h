// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#ifndef __VRSYSTEMLOCAL_H__
#define __VRSYSTEMLOCAL_H__

/*
===============================================================================

	Engine-only VR declarations: the settings every engine target registers
	(VRSystem.cpp), shared by the OpenXR implementation (src/sys/openxr) and
	the usercmd generator. Game modules see only VRSystem.h.

===============================================================================
*/

extern idCVar vr_enable;
extern idCVar vr_offerAnswered;
extern idCVar vr_setupDone;
extern idCVar vr_renderScale;
extern idCVar vr_worldScale;
extern idCVar vr_aimMode;
extern idCVar vr_leftHanded;
extern idCVar vr_moveDirection;
extern idCVar vr_turnMode;
extern idCVar vr_snapTurnAngle;
extern idCVar vr_smoothTurnSpeed;
extern idCVar vr_stickDeadzone;
extern idCVar vr_headOffsetLimit;
extern idCVar vr_hudDistance;
extern idCVar vr_hudWidth;
extern idCVar vr_hudHeightOffset;
extern idCVar vr_hudOnSight;
extern idCVar vr_screenDistance;
extern idCVar vr_screenWidth;
extern idCVar vr_mirror;
extern idCVar vr_weaponOffsetX;
extern idCVar vr_weaponOffsetY;
extern idCVar vr_weaponOffsetZ;
extern idCVar vr_weaponPitch;
extern idCVar vr_weaponScale;
extern idCVar vr_wheelStickX;
extern idCVar vr_wheelStickY;
extern idCVar vr_twoHanded;
extern idCVar vr_physicalCrouch;
extern idCVar vr_roomScale;
extern idCVar vr_hapticStrength;
extern idCVar vr_aimLaser;
extern idCVar vr_body;
extern idCVar vr_bodyAhead;
extern idCVar vr_playSpace;
extern idCVar vr_standingHeight;
extern idCVar vr_pauseOnFocusLoss;
extern idCVar vr_debug;

// The OpenXR implementation, when the client was built with it (OPENQ4_OPENXR).
idVRSystem *		VR_GetOpenXRSystem( void );

/*
	VR mode and headset detection (VR-08).

	vr_enable is the player's VR Mode: Off never asks a runtime for anything;
	On plays in VR whenever a headset is there (the behaviour of vr_enable 1
	since VR began); Automatic asks the runtime at launch and plays in VR
	when a headset answers, else on the desktop, and offers a headset that
	turns up later instead of switching to it mid-game.

	Detection has two stages. Stage one (VR_RuntimeInstalled) reads which
	runtime the OpenXR loader would load without loading it, since creating
	an instance can start a runtime's server (SteamVR). Stage two asks that
	runtime for a headset; it runs only for On, Automatic or vr_detect.
*/
enum {
	VR_MODE_OFF,
	VR_MODE_ON,
	VR_MODE_AUTOMATIC
};
int					VR_Mode( void );

// Stage one, from the boot probe: an active OpenXR runtime is installed, and
// its name ("" when none).
bool				VR_RuntimeInstalled( void );
const char *		VR_RuntimeName( void );

// The renderer boot (R_RendererModule_BootEarly), once vr_enable is read: logs
// stage one and, for Automatic, runs stage two so the renderer can be chosen
// for VR. True when VR wants the renderer that is about to start.
bool				VR_BootDecide( void );
// The automatic renderer choice (r_renderApi best): VR wants a renderer now.
bool				VR_WantsRenderer( void );

// Automatic found a headset after it settled on the desktop: true once, for
// the session's notice that offers it.
bool				VR_ConsumeHeadsetOffer( void );
// A headset is offered and not taken yet (the notice's action is live).
bool				VR_HeadsetOffered( void );
// The player took the offer (Play in VR): Automatic plays in VR from now on
// in this session. False when no headset is offered.
bool				VR_TakeHeadsetOffer( void );
// vr_detect's answer, once: VR_DETECT_*, or -1 while none is new.
enum {
	VR_DETECT_NO_RUNTIME,
	VR_DETECT_NO_HEADSET,
	VR_DETECT_HEADSET
};
int					VR_ConsumeDetectResult( void );

// True once after a session that had the controllers lost them: the headset
// came off, the runtime's own menu opened over the game, or the runtime went
// away. The session pauses single player on it (vr_pauseOnFocusLoss).
bool				VR_ConsumeFocusLoss( void );

// The usercmd generator zeroed its view angles for a new map. The player
// spawns against a zeroed usercmd, so the body's yaw (snap turns and all) is
// taken again from the aim: the level starts facing the way its spawn point
// faces instead of turned by the level before.
void				VR_UsercmdAnglesCleared( void );

// OpenXR on Vulkan (XR_KHR_vulkan_enable2): the runtime creates the Vulkan
// renderer's instance and device, on the headset's GPU. The renderer module
// offers each creation through its services. False when VR does not want this
// renderer (VR off, no runtime or headset, no Vulkan support), and the module
// creates the object itself; true hands back the VkResult in *outResult.
bool				VR_CreateVulkanInstance( void *getInstanceProcAddr, const void *createInfo,
											 void **outInstance, int *outResult );
bool				VR_GetVulkanPhysicalDevice( void *instance, void **outPhysicalDevice );
bool				VR_CreateVulkanDevice( void *getInstanceProcAddr, void *physicalDevice, const void *createInfo,
										   void **outDevice, int *outResult );

#endif /* !__VRSYSTEMLOCAL_H__ */
