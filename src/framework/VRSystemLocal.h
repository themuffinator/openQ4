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
extern idCVar vr_twoHanded;
extern idCVar vr_physicalCrouch;
extern idCVar vr_roomScale;
extern idCVar vr_hapticStrength;
extern idCVar vr_aimLaser;
extern idCVar vr_playSpace;
extern idCVar vr_standingHeight;
extern idCVar vr_pauseOnFocusLoss;
extern idCVar vr_debug;

// The OpenXR implementation, when the client was built with it (OPENQ4_OPENXR).
idVRSystem *		VR_GetOpenXRSystem( void );

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
