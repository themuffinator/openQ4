// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
//

/*
===============================================================================

	OpenXR virtual reality.

	One OpenXR session drives presentation, tracking and controller input:

	- The session binds to the renderer's OpenGL context
	  (idRenderSystem::GetVRGraphicsBinding). It opens after the renderer
	  reports a live device and closes before the device is destroyed, so a
	  vid_restart or renderer shutdown never pulls a context out from under
	  the runtime.
	- BeginFrame/EndFrame bracket every presented screen update. xrWaitFrame
	  paces the main loop; the eye and virtual-screen swapchain images are
	  acquired before the renderer's frame and released after it, and the
	  layers submitted to xrEndFrame describe exactly what the renderer drew
	  (GetVRFrameResult) with the poses it drew it from.
	- Controllers reach the engine the way a gamepad does: buttons become the
	  SDL gamepad JOY keys (so default.cfg binds and menu navigation apply),
	  sticks become locomotion and turning, and the aim pose becomes the
	  usercmd angles, which keeps multiplayer authoritative and unchanged.

	See docs/dev/archive/2026-10-18/plans/2026-10-02-openxr-vr.md.

===============================================================================
*/

#include "../../idlib/precompiled.h"
#pragma hdrstop

#include "../../framework/VRSystemLocal.h"
#include "../../framework/VRMathCore.h"
#include "../../framework/PresentationClock.h"
#include "../../ui/RetainedUI.h"

#if defined( _WIN32 )
#include <windows.h>
#include <unknwn.h>		// openxr_platform.h's Win32 section names IUnknown
#define XR_USE_PLATFORM_WIN32
#endif
#define XR_USE_GRAPHICS_API_OPENGL
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

// The Xlib OpenGL binding, declared without X11 or GLX headers. The layout is
// XrGraphicsBindingOpenGLXlibKHR's: Display *, VisualID (uint32_t),
// GLXFBConfig and GLXContext pointers and a GLXDrawable XID.
typedef struct vrXlibGraphicsBinding_s {
	XrStructureType			type;
	const void *			next;
	void *					xDisplay;
	uint32_t				visualid;
	void *					glxFBConfig;
	unsigned long			glxDrawable;
	void *					glxContext;
} vrXlibGraphicsBinding_t;

// XR_KHR_vulkan_enable2, declared without Vulkan headers like the Xlib binding
// above: the engine passes the renderer's Vulkan objects through without
// using them. Dispatchable handles (VkInstance, VkPhysicalDevice, VkDevice)
// are pointers and VkImage a 64-bit handle; VkResult is a 32-bit enum. The
// layouts are XrGraphicsBindingVulkanKHR's, XrSwapchainImageVulkanKHR's,
// XrGraphicsRequirementsVulkanKHR's, XrVulkanInstanceCreateInfoKHR's,
// XrVulkanDeviceCreateInfoKHR's and XrVulkanGraphicsDeviceGetInfoKHR's.
#define VR_VULKAN_ENABLE2_EXTENSION_NAME	"XR_KHR_vulkan_enable2"

typedef struct vrVulkanGraphicsBinding_s {
	XrStructureType			type;
	const void *			next;
	void *					instance;
	void *					physicalDevice;
	void *					device;
	uint32_t				queueFamilyIndex;
	uint32_t				queueIndex;
} vrVulkanGraphicsBinding_t;

typedef struct vrVulkanSwapchainImage_s {
	XrStructureType			type;
	void *					next;
	uint64_t				image;
} vrVulkanSwapchainImage_t;

typedef struct vrVulkanRequirements_s {
	XrStructureType			type;
	void *					next;
	XrVersion				minApiVersionSupported;
	XrVersion				maxApiVersionSupported;
} vrVulkanRequirements_t;

typedef struct vrVulkanInstanceCreateInfo_s {
	XrStructureType			type;
	const void *			next;
	XrSystemId				systemId;
	XrFlags64				createFlags;
	void *					pfnGetInstanceProcAddr;
	const void *			vulkanCreateInfo;
	const void *			vulkanAllocator;
} vrVulkanInstanceCreateInfo_t;

typedef struct vrVulkanDeviceCreateInfo_s {
	XrStructureType			type;
	const void *			next;
	XrSystemId				systemId;
	XrFlags64				createFlags;
	void *					pfnGetInstanceProcAddr;
	void *					vulkanPhysicalDevice;
	const void *			vulkanCreateInfo;
	const void *			vulkanAllocator;
} vrVulkanDeviceCreateInfo_t;

typedef struct vrVulkanDeviceGetInfo_s {
	XrStructureType			type;
	const void *			next;
	XrSystemId				systemId;
	void *					vulkanInstance;
} vrVulkanDeviceGetInfo_t;

typedef XrResult ( XRAPI_PTR *vrGetVulkanRequirements_t )( XrInstance instance, XrSystemId systemId, vrVulkanRequirements_t *requirements );
typedef XrResult ( XRAPI_PTR *vrCreateVulkanInstance_t )( XrInstance instance, const vrVulkanInstanceCreateInfo_t *createInfo,
	void **vulkanInstance, int32_t *vulkanResult );
typedef XrResult ( XRAPI_PTR *vrGetVulkanDevice_t )( XrInstance instance, const vrVulkanDeviceGetInfo_t *getInfo, void **vulkanPhysicalDevice );
typedef XrResult ( XRAPI_PTR *vrCreateVulkanDevice_t )( XrInstance instance, const vrVulkanDeviceCreateInfo_t *createInfo,
	void **vulkanDevice, int32_t *vulkanResult );

// XR_MNDX_egl_enable, declared without EGL headers: XrGraphicsBindingEGLMNDX's
// layout, PFN_xrEglGetProcAddressMNDX then EGLDisplay, EGLConfig and
// EGLContext pointers. It binds an OpenGL context on EGL (SDL on native
// Wayland) and uses XR_KHR_opengl_enable's requirements and images.
#define VR_EGL_ENABLE_EXTENSION_NAME		"XR_MNDX_egl_enable"

// XR_EXT_local_floor: LOCAL with its origin dropped to the floor, for standing
// play on a runtime without a STAGE space (vr_playSpace)
#define VR_LOCAL_FLOOR_EXTENSION_NAME		"XR_EXT_local_floor"
static const XrReferenceSpaceType VR_REFERENCE_SPACE_TYPE_LOCAL_FLOOR = static_cast<XrReferenceSpaceType>( 1000426000 );

typedef struct vrEGLGraphicsBinding_s {
	XrStructureType			type;
	const void *			next;
	void *					getProcAddress;
	void *					display;
	void *					config;
	void *					context;
} vrEGLGraphicsBinding_t;

#ifndef GL_SRGB8_ALPHA8
#define GL_SRGB8_ALPHA8		0x8C43
#endif
#ifndef GL_RGBA8
#define GL_RGBA8			0x8058
#endif

// VkFormat values for the swapchain, in order of preference: sRGB, so the
// compositor reads the engine's gamma-encoded bytes as they are
static const int64_t VR_VK_FORMAT_R8G8B8A8_UNORM	= 37;
static const int64_t VR_VK_FORMAT_R8G8B8A8_SRGB		= 43;
static const int64_t VR_VK_FORMAT_B8G8R8A8_UNORM	= 44;
static const int64_t VR_VK_FORMAT_B8G8R8A8_SRGB		= 50;

enum vrGraphicsApi_t {
	VR_GRAPHICS_NONE,
	VR_GRAPHICS_OPENGL,
	VR_GRAPHICS_VULKAN
};

static const int VR_SCREEN_WIDTH = 1920;
static const int VR_SCREEN_HEIGHT = 1080;
static const int VR_SYSTEM_RETRY_MSEC = 3000;
// how long a queued renderer restart may wait behind a running script before
// the session retry counts it as failed
static const int VR_RESTART_PENDING_MSEC = 60000;
static const int VR_INPUT_CRITICAL_SECTION = CRITICAL_SECTION_TWO;
static const uint32_t VR_MAX_SWAPCHAIN_IMAGES = 64;
static const XrDuration VR_IMAGE_WAIT_NSEC = 100000000; // 100 ms, then recover on the desktop

enum vrActionId_t {
	VR_ACTION_AIM_POSE,
	VR_ACTION_GRIP_POSE,
	VR_ACTION_TRIGGER,
	VR_ACTION_SQUEEZE,
	VR_ACTION_THUMBSTICK,
	VR_ACTION_THUMBSTICK_CLICK,
	VR_ACTION_PRIMARY,
	VR_ACTION_SECONDARY,
	VR_ACTION_MENU,
	VR_ACTION_HAPTIC,
	VR_ACTION_COUNT
};

static const char *const vrActionNames[VR_ACTION_COUNT] = {
	"aim_pose", "grip_pose", "trigger", "squeeze", "thumbstick",
	"thumbstick_click", "primary", "secondary", "menu", "haptic"
};

// localized action names are shown by runtime binding editors
static const char *const vrActionLocalizedNames[VR_ACTION_COUNT] = {
	"#str_230100", "#str_230101", "#str_230102", "#str_230103", "#str_230104",
	"#str_230105", "#str_230106", "#str_230107", "#str_230108", "#str_230109"
};

static const XrActionType vrActionTypes[VR_ACTION_COUNT] = {
	XR_ACTION_TYPE_POSE_INPUT, XR_ACTION_TYPE_POSE_INPUT, XR_ACTION_TYPE_FLOAT_INPUT,
	XR_ACTION_TYPE_FLOAT_INPUT, XR_ACTION_TYPE_VECTOR2F_INPUT, XR_ACTION_TYPE_BOOLEAN_INPUT,
	XR_ACTION_TYPE_BOOLEAN_INPUT, XR_ACTION_TYPE_BOOLEAN_INPUT, XR_ACTION_TYPE_BOOLEAN_INPUT,
	XR_ACTION_TYPE_VIBRATION_OUTPUT
};

// One suggested binding: an action and the path under /user/hand/<hand>.
typedef struct vrBinding_s {
	int						action;
	int						hand;		// VR_HAND_LEFT / VR_HAND_RIGHT
	const char *			component;
} vrBinding_t;

typedef struct vrProfile_s {
	const char *			path;
	const vrBinding_t *		bindings;
	int						numBindings;
} vrProfile_t;

#define VR_BOTH_HANDS( action, component ) { action, VR_HAND_LEFT, component }, { action, VR_HAND_RIGHT, component }

static const vrBinding_t vrTouchBindings[] = {
	VR_BOTH_HANDS( VR_ACTION_AIM_POSE, "input/aim/pose" ),
	VR_BOTH_HANDS( VR_ACTION_GRIP_POSE, "input/grip/pose" ),
	VR_BOTH_HANDS( VR_ACTION_TRIGGER, "input/trigger/value" ),
	VR_BOTH_HANDS( VR_ACTION_SQUEEZE, "input/squeeze/value" ),
	VR_BOTH_HANDS( VR_ACTION_THUMBSTICK, "input/thumbstick" ),
	VR_BOTH_HANDS( VR_ACTION_THUMBSTICK_CLICK, "input/thumbstick/click" ),
	{ VR_ACTION_PRIMARY, VR_HAND_LEFT, "input/x/click" },
	{ VR_ACTION_PRIMARY, VR_HAND_RIGHT, "input/a/click" },
	{ VR_ACTION_SECONDARY, VR_HAND_LEFT, "input/y/click" },
	{ VR_ACTION_SECONDARY, VR_HAND_RIGHT, "input/b/click" },
	{ VR_ACTION_MENU, VR_HAND_LEFT, "input/menu/click" },
	VR_BOTH_HANDS( VR_ACTION_HAPTIC, "output/haptic" ),
};

// Index has no menu button outside the reserved system button, so the left
// B button opens the menu.
static const vrBinding_t vrIndexBindings[] = {
	VR_BOTH_HANDS( VR_ACTION_AIM_POSE, "input/aim/pose" ),
	VR_BOTH_HANDS( VR_ACTION_GRIP_POSE, "input/grip/pose" ),
	VR_BOTH_HANDS( VR_ACTION_TRIGGER, "input/trigger/value" ),
	VR_BOTH_HANDS( VR_ACTION_SQUEEZE, "input/squeeze/value" ),
	VR_BOTH_HANDS( VR_ACTION_THUMBSTICK, "input/thumbstick" ),
	VR_BOTH_HANDS( VR_ACTION_THUMBSTICK_CLICK, "input/thumbstick/click" ),
	VR_BOTH_HANDS( VR_ACTION_PRIMARY, "input/a/click" ),
	{ VR_ACTION_SECONDARY, VR_HAND_RIGHT, "input/b/click" },
	{ VR_ACTION_MENU, VR_HAND_LEFT, "input/b/click" },
	VR_BOTH_HANDS( VR_ACTION_HAPTIC, "output/haptic" ),
};

static const vrBinding_t vrViveBindings[] = {
	VR_BOTH_HANDS( VR_ACTION_AIM_POSE, "input/aim/pose" ),
	VR_BOTH_HANDS( VR_ACTION_GRIP_POSE, "input/grip/pose" ),
	VR_BOTH_HANDS( VR_ACTION_TRIGGER, "input/trigger/value" ),
	VR_BOTH_HANDS( VR_ACTION_SQUEEZE, "input/squeeze/click" ),
	VR_BOTH_HANDS( VR_ACTION_THUMBSTICK, "input/trackpad" ),
	VR_BOTH_HANDS( VR_ACTION_THUMBSTICK_CLICK, "input/trackpad/click" ),
	{ VR_ACTION_MENU, VR_HAND_LEFT, "input/menu/click" },
	{ VR_ACTION_SECONDARY, VR_HAND_RIGHT, "input/menu/click" },
	VR_BOTH_HANDS( VR_ACTION_HAPTIC, "output/haptic" ),
};

static const vrBinding_t vrMotionControllerBindings[] = {
	VR_BOTH_HANDS( VR_ACTION_AIM_POSE, "input/aim/pose" ),
	VR_BOTH_HANDS( VR_ACTION_GRIP_POSE, "input/grip/pose" ),
	VR_BOTH_HANDS( VR_ACTION_TRIGGER, "input/trigger/value" ),
	VR_BOTH_HANDS( VR_ACTION_SQUEEZE, "input/squeeze/click" ),
	VR_BOTH_HANDS( VR_ACTION_THUMBSTICK, "input/thumbstick" ),
	VR_BOTH_HANDS( VR_ACTION_THUMBSTICK_CLICK, "input/thumbstick/click" ),
	VR_BOTH_HANDS( VR_ACTION_PRIMARY, "input/trackpad/click" ),
	{ VR_ACTION_MENU, VR_HAND_LEFT, "input/menu/click" },
	{ VR_ACTION_SECONDARY, VR_HAND_RIGHT, "input/menu/click" },
	VR_BOTH_HANDS( VR_ACTION_HAPTIC, "output/haptic" ),
};

static const vrBinding_t vrSimpleBindings[] = {
	VR_BOTH_HANDS( VR_ACTION_AIM_POSE, "input/aim/pose" ),
	VR_BOTH_HANDS( VR_ACTION_GRIP_POSE, "input/grip/pose" ),
	VR_BOTH_HANDS( VR_ACTION_TRIGGER, "input/select/click" ),
	{ VR_ACTION_MENU, VR_HAND_LEFT, "input/menu/click" },
	{ VR_ACTION_SECONDARY, VR_HAND_RIGHT, "input/menu/click" },
	VR_BOTH_HANDS( VR_ACTION_HAPTIC, "output/haptic" ),
};

static const vrProfile_t vrProfiles[] = {
	{ "/interaction_profiles/oculus/touch_controller", vrTouchBindings, sizeof( vrTouchBindings ) / sizeof( vrTouchBindings[0] ) },
	{ "/interaction_profiles/valve/index_controller", vrIndexBindings, sizeof( vrIndexBindings ) / sizeof( vrIndexBindings[0] ) },
	{ "/interaction_profiles/htc/vive_controller", vrViveBindings, sizeof( vrViveBindings ) / sizeof( vrViveBindings[0] ) },
	{ "/interaction_profiles/microsoft/motion_controller", vrMotionControllerBindings, sizeof( vrMotionControllerBindings ) / sizeof( vrMotionControllerBindings[0] ) },
	{ "/interaction_profiles/khr/simple_controller", vrSimpleBindings, sizeof( vrSimpleBindings ) / sizeof( vrSimpleBindings[0] ) },
};

/*
===============================================================================

	idVRSystemOpenXR

===============================================================================
*/

class idVRSystemOpenXR : public idVRSystem {
public:
							idVRSystemOpenXR( void );

	virtual void			Init( void );
	virtual void			Shutdown( void );
	virtual void			RendererStarted( void );
	virtual void			RendererStopping( void );
	virtual void			BeginFrame( void );
	virtual void			EndFrame( void );
	virtual bool			IsPacing( void ) const;
	virtual bool			IsActive( void ) const;
	virtual void			GetFrameState( vrFrameState_t &state ) const;
	virtual bool			GetUsercmdInput( float otherYawDelta, float currentYaw, vrUsercmdInput_t &input );
	virtual void			DrawMenuPointer( void );
	virtual void			Vibrate( int hand, float amplitude, int durationMsec );
	virtual void			ShiftTrackingOrigin( const idVec3 &trackingDelta );

	void					RequestRestart( void ) { restartRequested = true; }
	void					RequestRecenter( void ) { recenterRequested = true; }
	void					RequestCalibrateHeight( void ) { calibrateRequested = true; }
	bool					ConsumeFocusLoss( void ) { const bool lost = focusLost; focusLost = false; return lost; }
	void					ResetBodyYaw( void );
	void					PrintStatus( void ) const;

	// OpenXR on Vulkan: the renderer's instance, physical device and device
	bool					CreateVulkanInstance( void *getInstanceProcAddr, const void *createInfo, void **outInstance, int *outResult );
	bool					GetVulkanPhysicalDevice( void *vulkanInstanceHandle, void **outPhysicalDevice );
	bool					CreateVulkanDevice( void *getInstanceProcAddr, void *physicalDevice, const void *createInfo, void **outDevice, int *outResult );

private:
	struct swapchain_t {
		XrSwapchain			handle;
		int					width;
		int					height;
		idList<uint64_t>	images;			// GL texture names or VkImage handles
		uint32_t			acquiredIndex;
		bool				acquired;
	};

	struct handInput_t {
		float				trigger;
		float				squeeze;
		float				stickX, stickY;
		bool				stickClick;
		bool				primary;
		bool				secondary;
		bool				menu;
	};

	// lifecycle
	enum recovery_t { RECOVERY_NONE, RECOVERY_SESSION, RECOVERY_INSTANCE };
	bool					Check( XrResult result, const char *what );
	bool					CheckFrame( XrResult result, const char *what );
	bool					RecoverIfNeeded( void );
	bool					CreateInstance( void );
	void					DestroyInstance( void );
	bool					AcquireSystem( void );
	bool					CreateSession( void );
	void					DestroySession( void );
	bool					CreateSwapchain( swapchain_t &swapchain, int width, int height );
	void					DestroySwapchain( swapchain_t &swapchain );
	bool					CreateActions( void );
	void					DestroyActions( void );
	XrPath					Path( const char *text );
	void					StartIfRequested( void );
	void					PollEvents( void );
	void					HandleSessionState( XrSessionState state );
	bool					PrepareVulkanSystem( void );
	void					RequestVulkanRendererRestart( void );
	bool					SelectSwapchainFormat( int rendererVulkanFormat );
	bool					CreateBaseSpace( void );
	void					UpdateStageBounds( void );
	void					ReleaseRendererImages( void );

	// per frame
	void					LocateFrame( XrTime time );
	void					SyncInput( float frameSeconds );
	void					PostKey( int key, bool down );
	void					UpdateKey( int slot, int key, bool down );
	bool					MenuMode( void ) const;
	bool					OffHandOnForegrip( int weaponHand ) const;
	void					ApplyTwoHandedAim( int weaponHand );
	void					UpdateMenuPointer( int weaponHand, bool menu );
	void					AnchorScreen( void );
	void					ScreenQuadSize( float &width, float &height ) const;
	void					ScreenSizeForWindow( int &width, int &height ) const;
	bool					ResizeScreenSwapchain( void );
	bool					AcquireImages( void );
	void					ReleaseImages( void );
	vrPose_t				ToEnginePose( const XrPosef &pose, bool valid ) const;
	XrPosef					RecenteredLocalPose( const XrPosef &pose ) const;

	// instance and session
	XrInstance				instance;
	XrSystemId				systemId;
	XrSession				session;
	XrSessionState			sessionState;
	// the base space every pose and layer is tracked in: LOCAL for seated
	// play, STAGE or LOCAL_FLOOR for standing play (vr_playSpace)
	XrSpace					localSpace;
	XrSpace					viewSpace;
	XrReferenceSpaceType	baseSpaceType;
	bool					haveLocalFloor;		// XR_EXT_local_floor is enabled
	bool					standingPlay;		// heights are measured from the base space's floor
	bool					playSpaceFallback;	// standing play was asked for and the runtime knows no floor
	bool					stageBoundsValid;
	XrExtent2Df				stageBounds;		// the STAGE play area, metres
	XrEnvironmentBlendMode	blendMode;
	PFN_xrGetOpenGLGraphicsRequirementsKHR getGLRequirements;
	bool					haveEGL;			// XR_MNDX_egl_enable is enabled
	vrGetVulkanRequirements_t getVulkanRequirements;	// XR_KHR_vulkan_enable2, when the runtime has it
	vrCreateVulkanInstance_t createVulkanInstance;
	vrGetVulkanDevice_t		getVulkanDevice;
	vrCreateVulkanDevice_t	createVulkanDevice;
	int						graphicsApi;		// vrGraphicsApi_t of the session
	// the Vulkan objects this instance's runtime created for the renderer; a
	// session binds only to a device the runtime made
	void *					vulkanInstance;
	void *					vulkanPhysicalDevice;
	void *					vulkanDevice;
	XrInstance				vulkanCreator;		// the XR instance that made them
	XrInstance				vulkanRestartFor;	// renderer restart already asked for this XR instance
	int						vulkanRestartQueued;	// when it was queued, until the renderer stops; 0 none
	bool					sessionRunning;
	bool					sessionFocused;
	bool					focusLost;			// a focused session lost the controllers (ConsumeFocusLoss)
	bool					rendererReady;
	bool					restartRequested;
	bool					recenterRequested;
	bool					calibrateRequested;	// vr_calibrateHeight: measure the standing eye height now
	bool					systemUnavailableLogged;
	bool					startFailureLogged;	// "no runtime" style warnings print once per start
	bool					runtimeStopped;		// the runtime ended VR: wait for vr_restart
	recovery_t				recovery;			// consumed only at a frame boundary
	int						nextInstanceRetryTime;	// no runtime answered: when to ask for one again
	int						nextSystemRetryTime;
	int						nextSessionRetryTime;
	// the runtime moves a reference space (the headset's own recentre, a redrawn
	// play area) from spaceChangeTime on; the origin follows from that frame
	bool					spaceChangePending;
	XrTime					spaceChangeTime;
	idStr					runtimeName;
	idStr					systemName;
	idStr					interactionProfile;

	swapchain_t				eyeSwapchains[VR_NUM_EYES];
	swapchain_t				screenSwapchain;
	int64_t					swapchainFormat;
	int						recommendedEyeWidth;
	int						recommendedEyeHeight;

	// actions
	XrActionSet				actionSet;
	XrAction				actions[VR_ACTION_COUNT];
	XrPath					handPaths[VR_NUM_HANDS];
	XrSpace					aimSpaces[VR_NUM_HANDS];
	XrSpace					gripSpaces[VR_NUM_HANDS];
	bool					actionsAttached;

	// frame
	bool					frameOpen;
	bool					frameShouldRender;
	XrTime					frameDisplayTime;
	XrDuration				frameDisplayPeriod;	// the headset's refresh interval, as the runtime last predicted it
	XrView					views[VR_NUM_EYES];
	bool					viewsValid;
	openq4::PresentationFrameDelta frameDelta;
	vrFrameState_t			frame;
	// tracking origin in the base space (yaw only); its height is the head's
	// at the recentre in seated play and the standing eye height above the
	// floor in standing play
	XrPosef					recenterPose;
	XrPosef					screenAnchor;		// virtual screen centre in the base space
	bool					screenAnchored;
	bool					lastFrameHud;
	int						screenResizeFailedWidth;
	int						screenResizeFailedHeight;
	XrPosef					aimLocalPoses[VR_NUM_HANDS];	// raw LOCAL space, for the menu pointer
	bool					aimLocalValid[VR_NUM_HANDS];
	bool					pointerOnScreen;	// a menu took the pointer at ( pointerU, pointerV )
	float					pointerU;
	float					pointerV;
	const idMaterial *		pointerMaterial;

	// input (shared with the async usercmd thread under VR_INPUT_CRITICAL_SECTION)
	handInput_t				hands[VR_NUM_HANDS];
	bool					inputValid;
	bool					bodyYawValid;
	float					bodyYaw;
	float					aimLocalYaw;
	float					aimLocalPitch;
	float					moveForward;
	float					moveRight;
	vrSnapTurnState_t		snapTurn;
	int						heldKeys[16];
	bool					twoHanded;		// the off hand holds the gun's foregrip (vr_twoHanded)
	bool					roomScaleStickIdle;	// the locomotion stick rests (multiplayer room scale)
	int						roomScaleWalkMsec;	// when the head last walked the body through the usercmd
	bool					roomScaleWalking;	// a usercmd walk is under way (VR_RoomScaleWalkInput)
	bool					physicalCrouch;		// the tracked head crouched (VR_PhysicalCrouch)
};

static idVRSystemOpenXR *	vrOpenXR = NULL;

idVRSystem *VR_GetOpenXRSystem( void ) {
	static idVRSystemOpenXR system;
	vrOpenXR = &system;
	return &system;
}

static XrPosef VR_IdentityPose( void ) {
	XrPosef pose;
	pose.orientation.x = pose.orientation.y = pose.orientation.z = 0.0f;
	pose.orientation.w = 1.0f;
	pose.position.x = pose.position.y = pose.position.z = 0.0f;
	return pose;
}

static vrQuat_t VR_FromXrQuat( const XrQuaternionf &q ) {
	vrQuat_t r = { q.x, q.y, q.z, q.w };
	return r;
}

static XrQuaternionf VR_ToXrQuat( const vrQuat_t &q ) {
	XrQuaternionf r;
	r.x = q.x;
	r.y = q.y;
	r.z = q.z;
	r.w = q.w;
	return r;
}

static vrVec3_t VR_FromXrVec( const XrVector3f &v ) {
	return VR_Vec3( v.x, v.y, v.z );
}

static XrVector3f VR_ToXrVec( const vrVec3_t &v ) {
	XrVector3f r;
	r.x = v.x;
	r.y = v.y;
	r.z = v.z;
	return r;
}

// a * b: b expressed in a's frame
static XrPosef VR_PoseMultiply( const XrPosef &a, const XrPosef &b ) {
	const vrQuat_t qa = VR_FromXrQuat( a.orientation );
	XrPosef r;
	r.orientation = VR_ToXrQuat( VR_QuatNormalize( VR_QuatMultiply( qa, VR_FromXrQuat( b.orientation ) ) ) );
	r.position = VR_ToXrVec( VR_Add( VR_FromXrVec( a.position ), VR_RotateVector( qa, VR_FromXrVec( b.position ) ) ) );
	return r;
}

static XrPosef VR_PoseInverse( const XrPosef &a ) {
	const vrQuat_t inverse = VR_QuatConjugate( VR_QuatNormalize( VR_FromXrQuat( a.orientation ) ) );
	XrPosef r;
	r.orientation = VR_ToXrQuat( inverse );
	r.position = VR_ToXrVec( VR_Scale( VR_RotateVector( inverse, VR_FromXrVec( a.position ) ), -1.0f ) );
	return r;
}

// The heading of an OpenXR orientation as a rotation about +Y only.
static XrQuaternionf VR_YawOnly( const XrQuaternionf &orientation ) {
	const vrVec3_t forward = VR_RotateVector( VR_QuatNormalize( VR_FromXrQuat( orientation ) ), VR_Vec3( 0.0f, 0.0f, -1.0f ) );
	const float yaw = std::atan2( -forward.x, -forward.z );
	XrQuaternionf q;
	q.x = 0.0f;
	q.y = std::sin( yaw * 0.5f );
	q.z = 0.0f;
	q.w = std::cos( yaw * 0.5f );
	return q;
}

idVRSystemOpenXR::idVRSystemOpenXR( void ) {
	instance = XR_NULL_HANDLE;
	systemId = XR_NULL_SYSTEM_ID;
	session = XR_NULL_HANDLE;
	sessionState = XR_SESSION_STATE_UNKNOWN;
	localSpace = XR_NULL_HANDLE;
	viewSpace = XR_NULL_HANDLE;
	blendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
	getGLRequirements = NULL;
	haveEGL = false;
	getVulkanRequirements = NULL;
	createVulkanInstance = NULL;
	getVulkanDevice = NULL;
	createVulkanDevice = NULL;
	graphicsApi = VR_GRAPHICS_NONE;
	vulkanInstance = NULL;
	vulkanPhysicalDevice = NULL;
	vulkanDevice = NULL;
	vulkanCreator = XR_NULL_HANDLE;
	vulkanRestartFor = XR_NULL_HANDLE;
	vulkanRestartQueued = 0;
	sessionRunning = false;
	sessionFocused = false;
	focusLost = false;
	rendererReady = false;
	restartRequested = false;
	recenterRequested = true;
	systemUnavailableLogged = false;
	startFailureLogged = false;
	runtimeStopped = false;
	recovery = RECOVERY_NONE;
	nextInstanceRetryTime = 0;
	nextSystemRetryTime = 0;
	nextSessionRetryTime = 0;
	spaceChangePending = false;
	spaceChangeTime = 0;
	swapchain_t *chains[3] = { &eyeSwapchains[0], &eyeSwapchains[1], &screenSwapchain };
	for ( int i = 0; i < 3; i++ ) {
		chains[i]->handle = XR_NULL_HANDLE;
		chains[i]->width = chains[i]->height = 0;
		chains[i]->acquiredIndex = 0;
		chains[i]->acquired = false;
	}
	swapchainFormat = 0;
	recommendedEyeWidth = recommendedEyeHeight = 0;
	actionSet = XR_NULL_HANDLE;
	for ( int i = 0; i < VR_ACTION_COUNT; i++ ) {
		actions[i] = XR_NULL_HANDLE;
	}
	for ( int i = 0; i < VR_NUM_HANDS; i++ ) {
		handPaths[i] = XR_NULL_PATH;
		aimSpaces[i] = XR_NULL_HANDLE;
		gripSpaces[i] = XR_NULL_HANDLE;
	}
	actionsAttached = false;
	frameOpen = false;
	frameShouldRender = false;
	frameDisplayTime = 0;
	frameDisplayPeriod = 0;
	memset( views, 0, sizeof( views ) );
	viewsValid = false;
	frameDelta.Reset();
	memset( &frame, 0, sizeof( frame ) );
	recenterPose = VR_IdentityPose();
	baseSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
	haveLocalFloor = false;
	standingPlay = false;
	playSpaceFallback = false;
	stageBoundsValid = false;
	stageBounds.width = stageBounds.height = 0.0f;
	calibrateRequested = false;
	screenAnchor = VR_IdentityPose();
	screenAnchored = false;
	lastFrameHud = false;
	screenResizeFailedWidth = screenResizeFailedHeight = 0;
	for ( int i = 0; i < VR_NUM_HANDS; i++ ) {
		aimLocalPoses[i] = VR_IdentityPose();
		aimLocalValid[i] = false;
	}
	pointerOnScreen = false;
	pointerU = pointerV = 0.0f;
	pointerMaterial = NULL;
	memset( hands, 0, sizeof( hands ) );
	inputValid = false;
	bodyYawValid = false;
	bodyYaw = 0.0f;
	aimLocalYaw = aimLocalPitch = 0.0f;
	moveForward = moveRight = 0.0f;
	snapTurn.latched = false;
	memset( heldKeys, 0, sizeof( heldKeys ) );
	twoHanded = false;
	roomScaleStickIdle = true;
	roomScaleWalkMsec = 0;
	roomScaleWalking = false;
	physicalCrouch = false;
}

/*
====================
Commands
====================
*/
static void VR_Restart_f( const idCmdArgs &args ) {
	(void)args;
	if ( vrOpenXR != NULL ) {
		vrOpenXR->RequestRestart();
	}
}

static void VR_Recenter_f( const idCmdArgs &args ) {
	(void)args;
	if ( vrOpenXR != NULL ) {
		vrOpenXR->RequestRecenter();
	}
}

static void VR_CalibrateHeight_f( const idCmdArgs &args ) {
	(void)args;
	if ( vrOpenXR != NULL ) {
		vrOpenXR->RequestCalibrateHeight();
	}
}

static void VR_Status_f( const idCmdArgs &args ) {
	(void)args;
	if ( vrOpenXR != NULL ) {
		vrOpenXR->PrintStatus();
	}
}

void idVRSystemOpenXR::Init( void ) {
	cmdSystem->AddCommand( "vr_restart", VR_Restart_f, CMD_FL_SYSTEM, "restarts the OpenXR session, applying vr_enable and vr_renderScale" );
	cmdSystem->AddCommand( "vr_recenter", VR_Recenter_f, CMD_FL_SYSTEM, "makes the current head position and heading the VR origin" );
	cmdSystem->AddCommand( "vr_calibrateHeight", VR_CalibrateHeight_f, CMD_FL_SYSTEM,
		"standing play: stand up straight; your eye height becomes vr_standingHeight and the VR origin is recentred" );
	cmdSystem->AddCommand( "vr_status", VR_Status_f, CMD_FL_SYSTEM, "prints the OpenXR runtime, session and tracking state" );
	vr_enable.ClearModified();
}

void idVRSystemOpenXR::Shutdown( void ) {
	DestroySession();
	DestroyInstance();
	cmdSystem->RemoveCommand( "vr_restart" );
	cmdSystem->RemoveCommand( "vr_recenter" );
	cmdSystem->RemoveCommand( "vr_calibrateHeight" );
	cmdSystem->RemoveCommand( "vr_status" );
}

void idVRSystemOpenXR::RendererStarted( void ) {
	rendererReady = true;
}

void idVRSystemOpenXR::RendererStopping( void ) {
	// the swapchain images live in this context's share group
	DestroySession();
	rendererReady = false;
	// a queued restart is under way
	vulkanRestartQueued = 0;
}

bool idVRSystemOpenXR::IsPacing( void ) const {
	return sessionRunning && recovery == RECOVERY_NONE;
}

bool idVRSystemOpenXR::IsActive( void ) const {
	return sessionRunning && recovery == RECOVERY_NONE && frame.active;
}

void idVRSystemOpenXR::PrintStatus( void ) const {
	common->Printf( "OpenXR: %s\n", instance != XR_NULL_HANDLE ? "instance created" : "no instance" );
	if ( runtimeStopped ) {
		common->Printf( "  the runtime ended VR; vr_restart starts it again\n" );
	}
	if ( instance != XR_NULL_HANDLE ) {
		common->Printf( "  runtime: %s\n", runtimeName.c_str() );
		common->Printf( "  system: %s\n", systemId != XR_NULL_SYSTEM_ID ? systemName.c_str() : "<no headset>" );
	}
	common->Printf( "  session: %s, state %d, %s\n", session != XR_NULL_HANDLE ? "created" : "none",
		static_cast<int>( sessionState ), sessionRunning ? ( sessionFocused ? "running, focused" : "running" ) : "not running" );
	if ( session != XR_NULL_HANDLE ) {
		common->Printf( "  graphics: %s\n", graphicsApi == VR_GRAPHICS_VULKAN ? "Vulkan" : "OpenGL" );
		common->Printf( "  eyes: %dx%d (recommended %dx%d), virtual screen %dx%d, format 0x%llx\n",
			eyeSwapchains[0].width, eyeSwapchains[0].height, recommendedEyeWidth, recommendedEyeHeight,
			screenSwapchain.width, screenSwapchain.height, static_cast<unsigned long long>( swapchainFormat ) );
		if ( frameDisplayPeriod > 0 ) {
			const double periodMsec = static_cast<double>( frameDisplayPeriod ) * 1.0e-6;
			common->Printf( "  refresh: %.1f Hz (a frame every %.2f ms)\n", 1000.0 / periodMsec, periodMsec );
		} else {
			common->Printf( "  refresh: not reported yet\n" );
		}
		common->Printf( "  interaction profile: %s\n", interactionProfile.Length() ? interactionProfile.c_str() : "<none>" );
		const char *const baseName = baseSpaceType == XR_REFERENCE_SPACE_TYPE_STAGE ? "STAGE"
			: baseSpaceType == VR_REFERENCE_SPACE_TYPE_LOCAL_FLOOR ? "LOCAL_FLOOR" : "LOCAL";
		if ( standingPlay ) {
			common->Printf( "  play space: standing on %s, standing eye height %.2f m%s\n", baseName, recenterPose.position.y,
				stageBoundsValid ? va( ", play area %.2f x %.2f m", stageBounds.width, stageBounds.height ) : "" );
		} else {
			common->Printf( "  play space: seated on %s, origin %.2f m%s\n", baseName, recenterPose.position.y,
				playSpaceFallback ? " (standing asked for; the runtime knows no floor)" : "" );
		}
		common->Printf( "  frame %d, head %s ( %.1f %.1f %.1f ), body yaw %.2f%s\n", frame.frameNumber, frame.head.valid ? "tracked" : "lost",
			frame.head.origin.x, frame.head.origin.y, frame.head.origin.z, bodyYaw, bodyYawValid ? "" : " (not yet driving input)" );
	}
}

/*
====================
idVRSystemOpenXR::Check

Record loss without destroying resources underneath the caller. A temporary
swapchain, an acquired image or a partially created action set must unwind
before RecoverIfNeeded consumes the request at a frame boundary.
====================
*/
bool idVRSystemOpenXR::Check( XrResult result, const char *what ) {
	if ( XR_SUCCEEDED( result ) && result != XR_SESSION_LOSS_PENDING && result != XR_TIMEOUT_EXPIRED ) {
		return true;
	}
	char name[XR_MAX_RESULT_STRING_SIZE];
	name[0] = '\0';
	if ( instance == XR_NULL_HANDLE || xrResultToString( instance, result, name ) != XR_SUCCESS ) {
		idStr::snPrintf( name, sizeof( name ), "%d", static_cast<int>( result ) );
	}
	common->Warning( "OpenXR: %s failed: %s", what, name );
	if ( result == XR_ERROR_SESSION_LOST || result == XR_SESSION_LOSS_PENDING ) {
		if ( recovery == RECOVERY_NONE ) {
			recovery = RECOVERY_SESSION;
		}
	} else if ( result == XR_ERROR_INSTANCE_LOST || result == XR_ERROR_RUNTIME_FAILURE ) {
		recovery = RECOVERY_INSTANCE;
		runtimeStopped = true;
	}
	if ( recovery != RECOVERY_NONE ) {
		Sys_EnterCriticalSection( VR_INPUT_CRITICAL_SECTION );
		inputValid = false;
		frame.active = frame.stereo = false;
		Sys_LeaveCriticalSection( VR_INPUT_CRITICAL_SECTION );
	}
	return false;
}

bool idVRSystemOpenXR::CheckFrame( XrResult result, const char *what ) {
	if ( Check( result, what ) ) {
		return true;
	}
	// A frame call that failed cannot establish valid image, pose or input
	// ownership. Retire the session even for a non-loss error, preserving a
	// stronger instance-loss request from Check.
	if ( recovery == RECOVERY_NONE ) {
		recovery = RECOVERY_SESSION;
	}
	Sys_EnterCriticalSection( VR_INPUT_CRITICAL_SECTION );
	inputValid = false;
	frame.active = frame.stereo = false;
	Sys_LeaveCriticalSection( VR_INPUT_CRITICAL_SECTION );
	return false;
}

bool idVRSystemOpenXR::RecoverIfNeeded( void ) {
	if ( recovery == RECOVERY_NONE ) {
		return false;
	}
	const recovery_t requested = recovery;
	recovery = RECOVERY_NONE;
	// the headset is gone from under a player who was using it
	focusLost = focusLost || sessionFocused;
	DestroySession();
	if ( requested == RECOVERY_INSTANCE ) {
		DestroyInstance();
	}
	// A persistent runtime fault must not create/destroy a session every frame.
	nextSessionRetryTime = Sys_Milliseconds() + VR_SYSTEM_RETRY_MSEC;
	return true;
}

XrPath idVRSystemOpenXR::Path( const char *text ) {
	XrPath path = XR_NULL_PATH;
	if ( recovery != RECOVERY_NONE || !Check( xrStringToPath( instance, text, &path ), "xrStringToPath" ) ) {
		return XR_NULL_PATH;
	}
	return path;
}

/*
====================
idVRSystemOpenXR::CreateInstance
====================
*/
bool idVRSystemOpenXR::CreateInstance( void ) {
	uint32_t extensionCount = 0;
	XrResult result = xrEnumerateInstanceExtensionProperties( NULL, 0, &extensionCount, NULL );
	if ( XR_FAILED( result ) || extensionCount == 0 || extensionCount > 4096 ) {
		if ( !startFailureLogged ) {
			common->Warning( "OpenXR: no runtime is available (%d); install and activate one (SteamVR, Meta Quest Link, Monado...)", static_cast<int>( result ) );
			startFailureLogged = true;
		}
		return false;
	}
	idList<XrExtensionProperties> extensions;
	extensions.SetNum( static_cast<int>( extensionCount ) );
	for ( int i = 0; i < extensions.Num(); i++ ) {
		extensions[i].type = XR_TYPE_EXTENSION_PROPERTIES;
		extensions[i].next = NULL;
	}
	result = xrEnumerateInstanceExtensionProperties( NULL, extensionCount, &extensionCount, extensions.Ptr() );
	if ( XR_FAILED( result ) || extensionCount != static_cast<uint32_t>( extensions.Num() ) ) {
		return false;
	}
	// every graphics API the runtime and the renderers share: the renderer
	// running when the session opens picks its binding
	bool haveOpenGL = false;
	bool haveVulkan = false;
	bool haveEGLBinding = false;
	bool haveLocalFloorExtension = false;
	for ( int i = 0; XR_SUCCEEDED( result ) && i < extensions.Num(); i++ ) {
		if ( idStr::Cmp( extensions[i].extensionName, XR_KHR_OPENGL_ENABLE_EXTENSION_NAME ) == 0 ) {
			haveOpenGL = true;
		} else if ( idStr::Cmp( extensions[i].extensionName, VR_VULKAN_ENABLE2_EXTENSION_NAME ) == 0 ) {
			haveVulkan = true;
		} else if ( idStr::Cmp( extensions[i].extensionName, VR_EGL_ENABLE_EXTENSION_NAME ) == 0 ) {
			haveEGLBinding = true;
		} else if ( idStr::Cmp( extensions[i].extensionName, VR_LOCAL_FLOOR_EXTENSION_NAME ) == 0 ) {
			haveLocalFloorExtension = true;
		}
	}
	if ( !haveOpenGL && !haveVulkan ) {
		if ( !startFailureLogged ) {
			common->Warning( "OpenXR: the active runtime supports neither OpenGL (%s) nor Vulkan (%s); VR stays off",
				XR_KHR_OPENGL_ENABLE_EXTENSION_NAME, VR_VULKAN_ENABLE2_EXTENSION_NAME );
			startFailureLogged = true;
		}
		return false;
	}

	const char *enabledExtensions[4];
	uint32_t enabledCount = 0;
	if ( haveOpenGL ) {
		enabledExtensions[enabledCount++] = XR_KHR_OPENGL_ENABLE_EXTENSION_NAME;
		// an OpenGL context on EGL (native Wayland) binds through it
		if ( haveEGLBinding ) {
			enabledExtensions[enabledCount++] = VR_EGL_ENABLE_EXTENSION_NAME;
		}
	}
	if ( haveVulkan ) {
		enabledExtensions[enabledCount++] = VR_VULKAN_ENABLE2_EXTENSION_NAME;
	}
	// standing play's floor where the runtime has no STAGE
	if ( haveLocalFloorExtension ) {
		enabledExtensions[enabledCount++] = VR_LOCAL_FLOOR_EXTENSION_NAME;
	}
	XrInstanceCreateInfo createInfo;
	memset( &createInfo, 0, sizeof( createInfo ) );
	createInfo.type = XR_TYPE_INSTANCE_CREATE_INFO;
	idStr::Copynz( createInfo.applicationInfo.applicationName, "openQ4", sizeof( createInfo.applicationInfo.applicationName ) );
	createInfo.applicationInfo.applicationVersion = 1;
	idStr::Copynz( createInfo.applicationInfo.engineName, "openQ4", sizeof( createInfo.applicationInfo.engineName ) );
	createInfo.applicationInfo.engineVersion = 1;
	createInfo.applicationInfo.apiVersion = XR_API_VERSION_1_0;
	createInfo.enabledExtensionCount = enabledCount;
	createInfo.enabledExtensionNames = enabledExtensions;
	if ( !Check( xrCreateInstance( &createInfo, &instance ), "xrCreateInstance" ) ) {
		instance = XR_NULL_HANDLE;
		return false;
	}
	haveLocalFloor = haveLocalFloorExtension;

	XrInstanceProperties properties;
	memset( &properties, 0, sizeof( properties ) );
	properties.type = XR_TYPE_INSTANCE_PROPERTIES;
	if ( Check( xrGetInstanceProperties( instance, &properties ), "xrGetInstanceProperties" ) ) {
		runtimeName = va( "%s %u.%u.%u", properties.runtimeName,
			static_cast<unsigned>( XR_VERSION_MAJOR( properties.runtimeVersion ) ),
			static_cast<unsigned>( XR_VERSION_MINOR( properties.runtimeVersion ) ),
			static_cast<unsigned>( XR_VERSION_PATCH( properties.runtimeVersion ) ) );
	} else {
		runtimeName = "unknown";
		if ( recovery != RECOVERY_NONE ) {
			return false;
		}
	}

	PFN_xrVoidFunction function = NULL;
	if ( haveOpenGL ) {
		if ( !Check( xrGetInstanceProcAddr( instance, "xrGetOpenGLGraphicsRequirementsKHR", &function ), "xrGetInstanceProcAddr( xrGetOpenGLGraphicsRequirementsKHR )" ) ) {
			DestroyInstance();
			return false;
		}
		getGLRequirements = reinterpret_cast<PFN_xrGetOpenGLGraphicsRequirementsKHR>( function );
		haveEGL = haveEGLBinding;
	}
	if ( haveVulkan ) {
		PFN_xrVoidFunction functions[4] = { NULL, NULL, NULL, NULL };
		const char *const names[4] = { "xrGetVulkanGraphicsRequirements2KHR", "xrCreateVulkanInstanceKHR",
			"xrGetVulkanGraphicsDevice2KHR", "xrCreateVulkanDeviceKHR" };
		bool resolved = true;
		for ( int i = 0; i < 4; i++ ) {
			resolved = resolved && Check( xrGetInstanceProcAddr( instance, names[i], &functions[i] ), names[i] ) && functions[i] != NULL;
		}
		if ( resolved ) {
			getVulkanRequirements = reinterpret_cast<vrGetVulkanRequirements_t>( functions[0] );
			createVulkanInstance = reinterpret_cast<vrCreateVulkanInstance_t>( functions[1] );
			getVulkanDevice = reinterpret_cast<vrGetVulkanDevice_t>( functions[2] );
			createVulkanDevice = reinterpret_cast<vrCreateVulkanDevice_t>( functions[3] );
		} else if ( !haveOpenGL || instance == XR_NULL_HANDLE ) {
			DestroyInstance();
			return false;
		}
	}
	common->Printf( "OpenXR: runtime %s (%s%s%s%s)\n", runtimeName.c_str(), getGLRequirements != NULL ? "OpenGL" : "",
		haveEGL ? " on GLX or EGL" : "",
		getGLRequirements != NULL && getVulkanRequirements != NULL ? ", " : "", getVulkanRequirements != NULL ? "Vulkan" : "" );
	systemUnavailableLogged = false;
	nextSystemRetryTime = 0;
	return true;
}

void idVRSystemOpenXR::DestroyInstance( void ) {
	DestroySession();
	const XrInstance oldInstance = instance;
	instance = XR_NULL_HANDLE;
	if ( oldInstance != XR_NULL_HANDLE ) {
		xrDestroyInstance( oldInstance );
	}
	systemId = XR_NULL_SYSTEM_ID;
	getGLRequirements = NULL;
	haveEGL = false;
	haveLocalFloor = false;
	getVulkanRequirements = NULL;
	createVulkanInstance = NULL;
	getVulkanDevice = NULL;
	createVulkanDevice = NULL;
	// the renderer keeps its Vulkan objects; they belong to no runtime now
	vulkanCreator = XR_NULL_HANDLE;
	vulkanRestartFor = XR_NULL_HANDLE;
	for ( int i = 0; i < VR_NUM_HANDS; i++ ) {
		handPaths[i] = XR_NULL_PATH;
	}
}

/*
====================
idVRSystemOpenXR::AcquireSystem

A headset that is switched off or unplugged is not an error: the system is
asked for again every few seconds while VR stays enabled.
====================
*/
bool idVRSystemOpenXR::AcquireSystem( void ) {
	if ( systemId != XR_NULL_SYSTEM_ID ) {
		return true;
	}
	if ( Sys_Milliseconds() < nextSystemRetryTime ) {
		return false;
	}
	XrSystemGetInfo getInfo;
	memset( &getInfo, 0, sizeof( getInfo ) );
	getInfo.type = XR_TYPE_SYSTEM_GET_INFO;
	getInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
	const XrResult result = xrGetSystem( instance, &getInfo, &systemId );
	if ( result == XR_ERROR_FORM_FACTOR_UNAVAILABLE ) {
		systemId = XR_NULL_SYSTEM_ID;
		nextSystemRetryTime = Sys_Milliseconds() + VR_SYSTEM_RETRY_MSEC;
		if ( !systemUnavailableLogged ) {
			common->Printf( "OpenXR: no headset is connected yet; waiting for one\n" );
			systemUnavailableLogged = true;
		}
		return false;
	}
	if ( !Check( result, "xrGetSystem" ) ) {
		systemId = XR_NULL_SYSTEM_ID;
		nextSystemRetryTime = Sys_Milliseconds() + VR_SYSTEM_RETRY_MSEC;
		return false;
	}

	XrSystemProperties properties;
	memset( &properties, 0, sizeof( properties ) );
	properties.type = XR_TYPE_SYSTEM_PROPERTIES;
	if ( Check( xrGetSystemProperties( instance, systemId, &properties ), "xrGetSystemProperties" ) ) {
		systemName = properties.systemName;
	}
	if ( recovery != RECOVERY_NONE ) {
		return false;
	}
	common->Printf( "OpenXR: headset %s\n", systemName.c_str() );
	return true;
}

/*
===============================================================================

	OpenXR on Vulkan

	XR_KHR_vulkan_enable2 has the runtime create the renderer's instance and
	device: it adds the extensions it needs and picks the GPU the headset is
	attached to. The Vulkan renderer offers both creations here while it
	starts (renderModuleServices_t, render API 32). When VR is enabled and a
	headset is there they go through the runtime, and the session later binds
	to that device. A device made before the headset appeared, or for an
	earlier runtime instance, cannot be bound: the renderer is restarted once
	so the runtime can make it.

===============================================================================
*/

/*
====================
idVRSystemOpenXR::PrepareVulkanSystem

An instance whose runtime supports Vulkan, and its headset. Runs while the
renderer starts, before the per-frame start, so it creates both on the spot.
====================
*/
bool idVRSystemOpenXR::PrepareVulkanSystem( void ) {
	if ( !vr_enable.GetBool() || runtimeStopped ) {
		return false;
	}
	if ( instance == XR_NULL_HANDLE && !CreateInstance() ) {
		return false;
	}
	if ( createVulkanInstance == NULL ) {
		static bool warned = false;
		if ( !warned ) {
			common->Warning( "OpenXR: the active runtime does not support Vulkan (%s); VR needs r_renderApi gl with it",
				VR_VULKAN_ENABLE2_EXTENSION_NAME );
			warned = true;
		}
		return false;
	}
	// a headset that is not there yet is asked for again by the frame loop
	nextSystemRetryTime = 0;
	return AcquireSystem();
}

bool idVRSystemOpenXR::CreateVulkanInstance( void *getInstanceProcAddr, const void *createInfo, void **outInstance, int *outResult ) {
	if ( getInstanceProcAddr == NULL || createInfo == NULL || outInstance == NULL || outResult == NULL || !PrepareVulkanSystem() ) {
		return false;
	}
	vrVulkanRequirements_t requirements;
	memset( &requirements, 0, sizeof( requirements ) );
	requirements.type = XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN_KHR;
	if ( !Check( getVulkanRequirements( instance, systemId, &requirements ), "xrGetVulkanGraphicsRequirements2KHR" ) ) {
		return false;
	}
	common->Printf( "OpenXR: the runtime creates the Vulkan renderer's instance (it supports Vulkan %u.%u to %u.%u)\n",
		static_cast<unsigned>( XR_VERSION_MAJOR( requirements.minApiVersionSupported ) ),
		static_cast<unsigned>( XR_VERSION_MINOR( requirements.minApiVersionSupported ) ),
		static_cast<unsigned>( XR_VERSION_MAJOR( requirements.maxApiVersionSupported ) ),
		static_cast<unsigned>( XR_VERSION_MINOR( requirements.maxApiVersionSupported ) ) );

	vrVulkanInstanceCreateInfo_t info;
	memset( &info, 0, sizeof( info ) );
	info.type = XR_TYPE_VULKAN_INSTANCE_CREATE_INFO_KHR;
	info.systemId = systemId;
	info.pfnGetInstanceProcAddr = getInstanceProcAddr;
	info.vulkanCreateInfo = createInfo;
	void *created = NULL;
	int32_t vulkanResult = 0;
	if ( !Check( createVulkanInstance( instance, &info, &created, &vulkanResult ), "xrCreateVulkanInstanceKHR" ) ) {
		// the renderer makes its own instance; VR then stays off on it
		return false;
	}
	*outInstance = created;
	*outResult = static_cast<int>( vulkanResult );
	if ( vulkanResult == 0 && created != NULL ) {
		vulkanInstance = created;
		vulkanPhysicalDevice = NULL;
		vulkanDevice = NULL;
		vulkanCreator = instance;
	}
	return true;
}

bool idVRSystemOpenXR::GetVulkanPhysicalDevice( void *vulkanInstanceHandle, void **outPhysicalDevice ) {
	if ( vulkanInstanceHandle == NULL || outPhysicalDevice == NULL || vulkanInstanceHandle != vulkanInstance
			|| vulkanCreator == XR_NULL_HANDLE || vulkanCreator != instance || getVulkanDevice == NULL ) {
		return false;
	}
	vrVulkanDeviceGetInfo_t getInfo;
	memset( &getInfo, 0, sizeof( getInfo ) );
	getInfo.type = XR_TYPE_VULKAN_GRAPHICS_DEVICE_GET_INFO_KHR;
	getInfo.systemId = systemId;
	getInfo.vulkanInstance = vulkanInstanceHandle;
	void *physicalDevice = NULL;
	if ( !Check( getVulkanDevice( instance, &getInfo, &physicalDevice ), "xrGetVulkanGraphicsDevice2KHR" ) || physicalDevice == NULL ) {
		return false;
	}
	vulkanPhysicalDevice = physicalDevice;
	*outPhysicalDevice = physicalDevice;
	return true;
}

bool idVRSystemOpenXR::CreateVulkanDevice( void *getInstanceProcAddr, void *physicalDevice, const void *createInfo, void **outDevice, int *outResult ) {
	if ( getInstanceProcAddr == NULL || physicalDevice == NULL || createInfo == NULL || outDevice == NULL || outResult == NULL
			|| physicalDevice != vulkanPhysicalDevice || vulkanCreator == XR_NULL_HANDLE || vulkanCreator != instance
			|| createVulkanDevice == NULL ) {
		return false;
	}
	vrVulkanDeviceCreateInfo_t info;
	memset( &info, 0, sizeof( info ) );
	info.type = XR_TYPE_VULKAN_DEVICE_CREATE_INFO_KHR;
	info.systemId = systemId;
	info.pfnGetInstanceProcAddr = getInstanceProcAddr;
	info.vulkanPhysicalDevice = physicalDevice;
	info.vulkanCreateInfo = createInfo;
	void *created = NULL;
	int32_t vulkanResult = 0;
	if ( !Check( createVulkanDevice( instance, &info, &created, &vulkanResult ), "xrCreateVulkanDeviceKHR" ) ) {
		return false;
	}
	*outDevice = created;
	*outResult = static_cast<int>( vulkanResult );
	if ( vulkanResult == 0 && created != NULL ) {
		vulkanDevice = created;
		common->Printf( "OpenXR: the runtime created the Vulkan renderer's device\n" );
	}
	return true;
}

/*
====================
idVRSystemOpenXR::RequestVulkanRendererRestart

Once per runtime instance: a renderer whose device a restart cannot route
through the runtime (the runtime refused it) must not restart forever. The
restart goes ahead of whatever the command buffer holds, but a script that
is waiting holds the buffer, so until the renderer stops the session retry
leaves it alone.
====================
*/
void idVRSystemOpenXR::RequestVulkanRendererRestart( void ) {
	if ( vulkanRestartQueued != 0 && Sys_Milliseconds() - vulkanRestartQueued < VR_RESTART_PENDING_MSEC ) {
		return;
	}
	vulkanRestartQueued = 0;
	if ( vulkanRestartFor == instance ) {
		static XrInstance warnedFor = XR_NULL_HANDLE;
		if ( warnedFor != instance ) {
			common->Warning( "OpenXR: the runtime did not create the Vulkan renderer's device; VR stays off (vr_restart tries again)" );
			warnedFor = instance;
		}
		return;
	}
	vulkanRestartFor = instance;
	vulkanRestartQueued = Max( 1, Sys_Milliseconds() );
	common->Printf( "OpenXR: restarting the Vulkan renderer so the runtime can create its device for the headset\n" );
	cmdSystem->BufferCommandText( CMD_EXEC_INSERT, "vid_restart\n" );
}

bool VR_ConsumeFocusLoss( void ) {
	return vrOpenXR != NULL && vrOpenXR->ConsumeFocusLoss();
}

void VR_UsercmdAnglesCleared( void ) {
	if ( vrOpenXR != NULL ) {
		vrOpenXR->ResetBodyYaw();
	}
}

bool VR_CreateVulkanInstance( void *getInstanceProcAddr, const void *createInfo, void **outInstance, int *outResult ) {
	return vrOpenXR != NULL && vrOpenXR->CreateVulkanInstance( getInstanceProcAddr, createInfo, outInstance, outResult );
}

bool VR_GetVulkanPhysicalDevice( void *instance, void **outPhysicalDevice ) {
	return vrOpenXR != NULL && vrOpenXR->GetVulkanPhysicalDevice( instance, outPhysicalDevice );
}

bool VR_CreateVulkanDevice( void *getInstanceProcAddr, void *physicalDevice, const void *createInfo, void **outDevice, int *outResult ) {
	return vrOpenXR != NULL && vrOpenXR->CreateVulkanDevice( getInstanceProcAddr, physicalDevice, createInfo, outDevice, outResult );
}

/*
====================
idVRSystemOpenXR::CreateSession
====================
*/
bool idVRSystemOpenXR::CreateSession( void ) {
	renderVRGraphicsBinding_t binding;
	if ( !renderSystem->GetVRGraphicsBinding( binding ) ) {
		static bool warned = false;
		if ( !warned ) {
			common->Warning( "OpenXR: VR needs the OpenGL or the Vulkan renderer (r_renderApi gl or vulkan); VR stays off" );
			warned = true;
		}
		return false;
	}

	XrSessionCreateInfo createInfo;
	memset( &createInfo, 0, sizeof( createInfo ) );
	createInfo.type = XR_TYPE_SESSION_CREATE_INFO;
	createInfo.systemId = systemId;
#if defined( _WIN32 )
	XrGraphicsBindingOpenGLWin32KHR win32Binding;
#else
	vrXlibGraphicsBinding_t xlibBinding;
	vrEGLGraphicsBinding_t eglBinding;
#endif
	vrVulkanGraphicsBinding_t vulkanBinding;

	if ( binding.kind == RENDER_VR_BINDING_VULKAN ) {
		if ( getVulkanRequirements == NULL ) {
			static bool warned = false;
			if ( !warned ) {
				common->Warning( "OpenXR: the active runtime does not support Vulkan (%s); VR needs r_renderApi gl with it",
					VR_VULKAN_ENABLE2_EXTENSION_NAME );
				warned = true;
			}
			return false;
		}
		if ( binding.vkDevice == NULL || binding.vkDevice != vulkanDevice || vulkanCreator != instance ) {
			// the renderer made its device before VR asked for one, or for an
			// earlier runtime instance; only a device the runtime creates binds
			RequestVulkanRendererRestart();
			return false;
		}
		// the requirements call must precede the session on this instance
		vrVulkanRequirements_t requirements;
		memset( &requirements, 0, sizeof( requirements ) );
		requirements.type = XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN_KHR;
		if ( !Check( getVulkanRequirements( instance, systemId, &requirements ), "xrGetVulkanGraphicsRequirements2KHR" ) ) {
			return false;
		}
		memset( &vulkanBinding, 0, sizeof( vulkanBinding ) );
		vulkanBinding.type = XR_TYPE_GRAPHICS_BINDING_VULKAN_KHR;
		vulkanBinding.instance = binding.vkInstance;
		vulkanBinding.physicalDevice = binding.vkPhysicalDevice;
		vulkanBinding.device = binding.vkDevice;
		vulkanBinding.queueFamilyIndex = binding.vkQueueFamilyIndex;
		vulkanBinding.queueIndex = binding.vkQueueIndex;
		createInfo.next = &vulkanBinding;
		graphicsApi = VR_GRAPHICS_VULKAN;
	} else {
		XrGraphicsRequirementsOpenGLKHR requirements;
		memset( &requirements, 0, sizeof( requirements ) );
		requirements.type = XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_KHR;
		if ( getGLRequirements == NULL ) {
			static bool warned = false;
			if ( !warned ) {
				common->Warning( "OpenXR: the active runtime does not support OpenGL (%s); VR needs r_renderApi vulkan with it",
					XR_KHR_OPENGL_ENABLE_EXTENSION_NAME );
				warned = true;
			}
			return false;
		}
		if ( binding.kind == RENDER_VR_BINDING_EGL && !haveEGL ) {
			static bool warned = false;
			if ( !warned ) {
				common->Warning( "OpenXR: the OpenGL renderer runs on EGL (native Wayland) and the active runtime cannot bind it (%s); "
					"use r_renderApi vulkan, or start openQ4 with OPENQ4_FORCE_X11=1", VR_EGL_ENABLE_EXTENSION_NAME );
				warned = true;
			}
			return false;
		}
		if ( !Check( getGLRequirements( instance, systemId, &requirements ), "xrGetOpenGLGraphicsRequirementsKHR" ) ) {
			return false;
		}
		const XrVersion contextVersion = XR_MAKE_VERSION( binding.glMajor, binding.glMinor, 0 );
		if ( contextVersion < requirements.minApiVersionSupported ) {
			common->Warning( "OpenXR: the runtime needs OpenGL %u.%u, the renderer's context is %d.%d; VR stays off",
				static_cast<unsigned>( XR_VERSION_MAJOR( requirements.minApiVersionSupported ) ),
				static_cast<unsigned>( XR_VERSION_MINOR( requirements.minApiVersionSupported ) ),
				binding.glMajor, binding.glMinor );
			return false;
		}
#if defined( _WIN32 )
		memset( &win32Binding, 0, sizeof( win32Binding ) );
		win32Binding.type = XR_TYPE_GRAPHICS_BINDING_OPENGL_WIN32_KHR;
		win32Binding.hDC = static_cast<HDC>( binding.wglDC );
		win32Binding.hGLRC = static_cast<HGLRC>( binding.wglContext );
		createInfo.next = &win32Binding;
#else
		if ( binding.kind == RENDER_VR_BINDING_EGL ) {
			memset( &eglBinding, 0, sizeof( eglBinding ) );
			eglBinding.type = XR_TYPE_GRAPHICS_BINDING_EGL_MNDX;
			eglBinding.getProcAddress = binding.eglGetProcAddress;
			eglBinding.display = binding.eglDisplay;
			eglBinding.config = binding.eglConfig;
			eglBinding.context = binding.eglContext;
			createInfo.next = &eglBinding;
		} else {
			memset( &xlibBinding, 0, sizeof( xlibBinding ) );
			xlibBinding.type = XR_TYPE_GRAPHICS_BINDING_OPENGL_XLIB_KHR;
			xlibBinding.xDisplay = binding.glxDisplay;
			xlibBinding.visualid = binding.glxVisualId;
			xlibBinding.glxFBConfig = binding.glxFBConfig;
			xlibBinding.glxDrawable = binding.glxDrawable;
			xlibBinding.glxContext = binding.glxContext;
			createInfo.next = &xlibBinding;
		}
#endif
		graphicsApi = VR_GRAPHICS_OPENGL;
	}
	if ( !Check( xrCreateSession( instance, &createInfo, &session ), "xrCreateSession" ) ) {
		session = XR_NULL_HANDLE;
		graphicsApi = VR_GRAPHICS_NONE;
		return false;
	}
	sessionState = XR_SESSION_STATE_UNKNOWN;

	// spaces: the base space (vr_playSpace) carries tracking, projection and
	// the virtual screen; VIEW the HUD
	if ( !CreateBaseSpace() ) {
		DestroySession();
		return false;
	}
	XrReferenceSpaceCreateInfo spaceInfo;
	memset( &spaceInfo, 0, sizeof( spaceInfo ) );
	spaceInfo.type = XR_TYPE_REFERENCE_SPACE_CREATE_INFO;
	spaceInfo.poseInReferenceSpace = VR_IdentityPose();
	spaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
	if ( !Check( xrCreateReferenceSpace( session, &spaceInfo, &viewSpace ), "xrCreateReferenceSpace( VIEW )" ) ) {
		DestroySession();
		return false;
	}

	// views
	uint32_t viewCount = 0;
	if ( !Check( xrEnumerateViewConfigurationViews( instance, systemId, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &viewCount, NULL ), "xrEnumerateViewConfigurationViews" )
			|| viewCount != VR_NUM_EYES ) {
		common->Warning( "OpenXR: the headset does not offer a two-view stereo configuration" );
		DestroySession();
		return false;
	}
	XrViewConfigurationView configViews[VR_NUM_EYES];
	for ( int i = 0; i < VR_NUM_EYES; i++ ) {
		memset( &configViews[i], 0, sizeof( configViews[i] ) );
		configViews[i].type = XR_TYPE_VIEW_CONFIGURATION_VIEW;
	}
	if ( !Check( xrEnumerateViewConfigurationViews( instance, systemId, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, VR_NUM_EYES, &viewCount, configViews ), "xrEnumerateViewConfigurationViews" ) || viewCount != VR_NUM_EYES ) {
		DestroySession();
		return false;
	}
	for ( int i = 0; i < VR_NUM_EYES; i++ ) {
		if ( configViews[i].recommendedImageRectWidth == 0 || configViews[i].recommendedImageRectHeight == 0
				|| configViews[i].recommendedImageRectWidth > configViews[i].maxImageRectWidth
				|| configViews[i].recommendedImageRectHeight > configViews[i].maxImageRectHeight
				|| configViews[i].maxImageRectWidth > 16384 || configViews[i].maxImageRectHeight > 16384 ) {
			common->Warning( "OpenXR: invalid or unsupported eye image dimensions" );
			DestroySession();
			return false;
		}
	}
	recommendedEyeWidth = static_cast<int>( configViews[0].recommendedImageRectWidth );
	recommendedEyeHeight = static_cast<int>( configViews[0].recommendedImageRectHeight );
	const float scale = idMath::ClampFloat( 0.5f, 2.0f, vr_renderScale.GetFloat() );
	const int maxWidth = static_cast<int>( Min( configViews[0].maxImageRectWidth, configViews[1].maxImageRectWidth ) );
	const int maxHeight = static_cast<int>( Min( configViews[0].maxImageRectHeight, configViews[1].maxImageRectHeight ) );
	const int eyeWidth = idMath::ClampInt( 64, Max( 64, maxWidth ), static_cast<int>( recommendedEyeWidth * scale + 0.5f ) );
	const int eyeHeight = idMath::ClampInt( 64, Max( 64, maxHeight ), static_cast<int>( recommendedEyeHeight * scale + 0.5f ) );

	// blend mode: an opaque display; anything else blends with the real world
	uint32_t blendCount = 0;
	blendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
	if ( Check( xrEnumerateEnvironmentBlendModes( instance, systemId, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &blendCount, NULL ), "xrEnumerateEnvironmentBlendModes" ) && blendCount > 0 && blendCount <= 16 ) {
		idList<XrEnvironmentBlendMode> modes;
		modes.SetNum( static_cast<int>( blendCount ) );
		if ( Check( xrEnumerateEnvironmentBlendModes( instance, systemId, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, blendCount, &blendCount, modes.Ptr() ), "xrEnumerateEnvironmentBlendModes" ) && blendCount == static_cast<uint32_t>( modes.Num() ) ) {
			blendMode = modes[0];
			for ( int i = 0; i < modes.Num(); i++ ) {
				if ( modes[i] == XR_ENVIRONMENT_BLEND_MODE_OPAQUE ) {
					blendMode = modes[i];
				}
			}
		}
	}
	if ( recovery != RECOVERY_NONE ) {
		return false;
	}

	if ( !SelectSwapchainFormat( binding.vkColorFormat ) ) {
		common->Warning( "OpenXR: the runtime offers no usable 8-bit colour swapchain format" );
		DestroySession();
		return false;
	}

	for ( int i = 0; i < VR_NUM_EYES; i++ ) {
		if ( !CreateSwapchain( eyeSwapchains[i], eyeWidth, eyeHeight ) ) {
			DestroySession();
			return false;
		}
	}
	int screenWidth = VR_SCREEN_WIDTH, screenHeight = VR_SCREEN_HEIGHT;
	ScreenSizeForWindow( screenWidth, screenHeight );
	if ( !CreateSwapchain( screenSwapchain, screenWidth, screenHeight ) ) {
		DestroySession();
		return false;
	}
	screenResizeFailedWidth = screenResizeFailedHeight = 0;

	if ( !CreateActions() ) {
		// tracking and presentation still work without controllers
		common->Warning( "OpenXR: controller input is unavailable" );
		DestroyActions();
		if ( recovery != RECOVERY_NONE ) {
			return false;
		}
	}

	recenterRequested = true;
	screenAnchored = false;
	bodyYawValid = false;
	common->Printf( "OpenXR: session created, eyes %dx%d (recommended %dx%d), virtual screen %dx%d\n",
		eyeWidth, eyeHeight, recommendedEyeWidth, recommendedEyeHeight, screenWidth, screenHeight );
	return true;
}

void idVRSystemOpenXR::DestroySession( void ) {
	// Session destruction abandons any outstanding frame and images. Do not
	// release here: a failed wait never granted permission to release, and a
	// release failure must not recursively enter teardown.
	frameOpen = false;
	frameShouldRender = false;
	Sys_EnterCriticalSection( VR_INPUT_CRITICAL_SECTION );
	inputValid = false;
	bodyYawValid = false;
	moveForward = moveRight = 0.0f;
	physicalCrouch = false;
	roomScaleWalking = false;
	roomScaleStickIdle = true;
	memset( hands, 0, sizeof( hands ) );
	memset( &frame, 0, sizeof( frame ) );
	Sys_LeaveCriticalSection( VR_INPUT_CRITICAL_SECTION );
	memset( aimLocalValid, 0, sizeof( aimLocalValid ) );
	if ( pointerOnScreen ) {
		pointerOnScreen = false;
		Sys_PostVRPointer( false, 0.0f, 0.0f );
	}
	ReleaseRendererImages();
	DestroyActions();
	for ( int i = 0; i < VR_NUM_EYES; i++ ) {
		DestroySwapchain( eyeSwapchains[i] );
	}
	DestroySwapchain( screenSwapchain );
	if ( localSpace != XR_NULL_HANDLE ) {
		xrDestroySpace( localSpace );
		localSpace = XR_NULL_HANDLE;
	}
	if ( viewSpace != XR_NULL_HANDLE ) {
		xrDestroySpace( viewSpace );
		viewSpace = XR_NULL_HANDLE;
	}
	const XrSession oldSession = session;
	session = XR_NULL_HANDLE;
	if ( oldSession != XR_NULL_HANDLE ) {
		xrDestroySession( oldSession );
		common->Printf( "OpenXR: session closed\n" );
	}
	sessionRunning = false;
	sessionFocused = false;
	sessionState = XR_SESSION_STATE_UNKNOWN;
	graphicsApi = VR_GRAPHICS_NONE;
	viewsValid = false;
	frameDisplayPeriod = 0;
	spaceChangePending = false;
	interactionProfile.Clear();

	for ( int i = 0; i < 16; i++ ) {
		UpdateKey( i, heldKeys[i], false );
	}
	twoHanded = false;
}

/*
====================
idVRSystemOpenXR::CreateBaseSpace

The reference space vr_playSpace asks for (VR_ChoosePlaySpace): seated play
on LOCAL, standing play on STAGE, else LOCAL_FLOOR, else head-relative LOCAL
with a warning. Replaces the current base space, so it runs only between
frames, and recentres: the new origin starts under the head, so the body
neither jumps nor walks the difference a second time.
====================
*/
bool idVRSystemOpenXR::CreateBaseSpace( void ) {
	vr_playSpace.ClearModified();
	bool haveStage = false;
	bool haveFloor = false;
	uint32_t count = 0;
	if ( Check( xrEnumerateReferenceSpaces( session, 0, &count, NULL ), "xrEnumerateReferenceSpaces" ) && count > 0 && count <= 64 ) {
		XrReferenceSpaceType types[64];
		if ( Check( xrEnumerateReferenceSpaces( session, count, &count, types ), "xrEnumerateReferenceSpaces" ) && count <= 64 ) {
			for ( uint32_t i = 0; i < count; i++ ) {
				haveStage = haveStage || types[i] == XR_REFERENCE_SPACE_TYPE_STAGE;
				haveFloor = haveFloor || ( haveLocalFloor && types[i] == VR_REFERENCE_SPACE_TYPE_LOCAL_FLOOR );
			}
		}
	}
	if ( recovery != RECOVERY_NONE ) {
		return false;
	}
	const vrPlaySpaceChoice_t choice = VR_ChoosePlaySpace( vr_playSpace.GetInteger(), haveStage, haveFloor );
	XrReferenceSpaceType type = choice.space == VR_BASE_SPACE_STAGE ? XR_REFERENCE_SPACE_TYPE_STAGE
		: choice.space == VR_BASE_SPACE_LOCAL_FLOOR ? VR_REFERENCE_SPACE_TYPE_LOCAL_FLOOR : XR_REFERENCE_SPACE_TYPE_LOCAL;
	bool standing = choice.standing;
	XrReferenceSpaceCreateInfo spaceInfo;
	memset( &spaceInfo, 0, sizeof( spaceInfo ) );
	spaceInfo.type = XR_TYPE_REFERENCE_SPACE_CREATE_INFO;
	spaceInfo.poseInReferenceSpace = VR_IdentityPose();
	spaceInfo.referenceSpaceType = type;
	XrSpace created = XR_NULL_HANDLE;
	if ( !Check( xrCreateReferenceSpace( session, &spaceInfo, &created ), "xrCreateReferenceSpace( base )" ) ) {
		if ( recovery != RECOVERY_NONE || type == XR_REFERENCE_SPACE_TYPE_LOCAL ) {
			return false;
		}
		// a floor-level space the runtime listed and would not make
		type = XR_REFERENCE_SPACE_TYPE_LOCAL;
		standing = false;
		spaceInfo.referenceSpaceType = type;
		created = XR_NULL_HANDLE;
		if ( !Check( xrCreateReferenceSpace( session, &spaceInfo, &created ), "xrCreateReferenceSpace( LOCAL )" ) ) {
			return false;
		}
	}
	if ( localSpace != XR_NULL_HANDLE ) {
		xrDestroySpace( localSpace );
	}
	localSpace = created;
	baseSpaceType = type;
	standingPlay = standing;
	playSpaceFallback = choice.fallback || ( choice.standing && !standing );
	UpdateStageBounds();
	// the new space starts from its own recentre; a move the runtime announced
	// for the old one no longer applies
	spaceChangePending = false;
	recenterRequested = true;
	screenAnchored = false;
	const char *const baseName = type == XR_REFERENCE_SPACE_TYPE_STAGE ? "STAGE"
		: type == VR_REFERENCE_SPACE_TYPE_LOCAL_FLOOR ? "LOCAL_FLOOR" : "LOCAL";
	common->Printf( "OpenXR: play space %s on %s\n", standingPlay ? "standing" : "seated", baseName );
	if ( playSpaceFallback ) {
		common->Warning( "OpenXR: the runtime offers no floor-level space (STAGE or %s); standing play measures your height "
			"from where you recentre, as seated play does", VR_LOCAL_FLOOR_EXTENSION_NAME );
	}
	return true;
}

// The play area of standing play on STAGE; the runtime may redraw it mid-session.
void idVRSystemOpenXR::UpdateStageBounds( void ) {
	stageBoundsValid = false;
	if ( session == XR_NULL_HANDLE || baseSpaceType != XR_REFERENCE_SPACE_TYPE_STAGE ) {
		return;
	}
	XrExtent2Df bounds;
	bounds.width = bounds.height = 0.0f;
	if ( xrGetReferenceSpaceBoundsRect( session, XR_REFERENCE_SPACE_TYPE_STAGE, &bounds ) == XR_SUCCESS
			&& bounds.width > 0.0f && bounds.height > 0.0f ) {
		stageBounds = bounds;
		stageBoundsValid = true;
	}
}

/*
====================
idVRSystemOpenXR::ReleaseRendererImages

Before swapchains are destroyed: OpenXR requires every submitted graphics
command that uses their images to have completed. A Vulkan renderer may
still have frames in flight, so it waits for them; the frame it presents
is cleared as SetVRFrame( NULL ) would.
====================
*/
void idVRSystemOpenXR::ReleaseRendererImages( void ) {
	if ( !rendererReady || renderSystem == NULL ) {
		return;
	}
	renderVRFrame_t release;
	memset( &release, 0, sizeof( release ) );
	release.releaseImages = true;
	renderSystem->SetVRFrame( &release );
}

bool idVRSystemOpenXR::CreateSwapchain( swapchain_t &swapchain, int width, int height ) {
	XrSwapchainCreateInfo createInfo;
	memset( &createInfo, 0, sizeof( createInfo ) );
	createInfo.type = XR_TYPE_SWAPCHAIN_CREATE_INFO;
	createInfo.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
	createInfo.format = swapchainFormat;
	createInfo.sampleCount = 1;
	createInfo.width = static_cast<uint32_t>( width );
	createInfo.height = static_cast<uint32_t>( height );
	createInfo.faceCount = 1;
	createInfo.arraySize = 1;
	createInfo.mipCount = 1;
	swapchain.handle = XR_NULL_HANDLE;
	swapchain.acquired = false;
	swapchain.acquiredIndex = 0;
	swapchain.images.Clear();
	if ( !Check( xrCreateSwapchain( session, &createInfo, &swapchain.handle ), "xrCreateSwapchain" ) ) {
		swapchain.handle = XR_NULL_HANDLE;
		return false;
	}
	uint32_t imageCount = 0;
	if ( !Check( xrEnumerateSwapchainImages( swapchain.handle, 0, &imageCount, NULL ), "xrEnumerateSwapchainImages" )
			|| imageCount == 0 || imageCount > VR_MAX_SWAPCHAIN_IMAGES ) {
		DestroySwapchain( swapchain );
		return false;
	}
	// a bounded count was checked above; each image must be filled and real
	const uint32_t capacity = imageCount;
	if ( graphicsApi == VR_GRAPHICS_VULKAN ) {
		idList<vrVulkanSwapchainImage_t> images;
		images.SetNum( static_cast<int>( imageCount ) );
		for ( int i = 0; i < images.Num(); i++ ) {
			images[i].type = XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR;
			images[i].next = NULL;
			images[i].image = 0;
		}
		if ( !Check( xrEnumerateSwapchainImages( swapchain.handle, capacity, &imageCount,
				reinterpret_cast<XrSwapchainImageBaseHeader *>( images.Ptr() ) ), "xrEnumerateSwapchainImages" ) || imageCount != capacity ) {
			DestroySwapchain( swapchain );
			return false;
		}
		for ( int i = 0; i < images.Num(); i++ ) {
			if ( images[i].image == 0 ) {
				DestroySwapchain( swapchain );
				return false;
			}
			swapchain.images.Append( images[i].image );
		}
	} else {
		idList<XrSwapchainImageOpenGLKHR> images;
		images.SetNum( static_cast<int>( imageCount ) );
		for ( int i = 0; i < images.Num(); i++ ) {
			images[i].type = XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR;
			images[i].next = NULL;
			images[i].image = 0;
		}
		if ( !Check( xrEnumerateSwapchainImages( swapchain.handle, capacity, &imageCount,
				reinterpret_cast<XrSwapchainImageBaseHeader *>( images.Ptr() ) ), "xrEnumerateSwapchainImages" ) || imageCount != capacity ) {
			DestroySwapchain( swapchain );
			return false;
		}
		for ( int i = 0; i < images.Num(); i++ ) {
			if ( images[i].image == 0 ) {
				DestroySwapchain( swapchain );
				return false;
			}
			swapchain.images.Append( images[i].image );
		}
	}
	swapchain.width = width;
	swapchain.height = height;
	return true;
}

/*
====================
idVRSystemOpenXR::SelectSwapchainFormat

sRGB, so the compositor reads the engine's gamma-encoded bytes as they are:
the renderer copies its targets into the images without converting them.
A Vulkan renderer's eyes share its window's pipelines when the images have the
window's channel order.
====================
*/
bool idVRSystemOpenXR::SelectSwapchainFormat( int rendererVulkanFormat ) {
	swapchainFormat = 0;
	uint32_t formatCount = 0;
	if ( !Check( xrEnumerateSwapchainFormats( session, 0, &formatCount, NULL ), "xrEnumerateSwapchainFormats" )
			|| formatCount == 0 || formatCount > 256 ) {
		return false;
	}
	idList<int64_t> formats;
	formats.SetNum( static_cast<int>( formatCount ) );
	if ( !Check( xrEnumerateSwapchainFormats( session, formatCount, &formatCount, formats.Ptr() ), "xrEnumerateSwapchainFormats" )
			|| formatCount != static_cast<uint32_t>( formats.Num() ) ) {
		return false;
	}
	const int64_t glPreferred[] = { GL_SRGB8_ALPHA8, GL_RGBA8 };
	const bool windowBGRA = rendererVulkanFormat == VR_VK_FORMAT_B8G8R8A8_UNORM || rendererVulkanFormat == VR_VK_FORMAT_B8G8R8A8_SRGB;
	const int64_t vulkanPreferred[] = {
		windowBGRA ? VR_VK_FORMAT_B8G8R8A8_SRGB : VR_VK_FORMAT_R8G8B8A8_SRGB,
		windowBGRA ? VR_VK_FORMAT_R8G8B8A8_SRGB : VR_VK_FORMAT_B8G8R8A8_SRGB,
		windowBGRA ? VR_VK_FORMAT_B8G8R8A8_UNORM : VR_VK_FORMAT_R8G8B8A8_UNORM,
		windowBGRA ? VR_VK_FORMAT_R8G8B8A8_UNORM : VR_VK_FORMAT_B8G8R8A8_UNORM };
	const bool vulkan = graphicsApi == VR_GRAPHICS_VULKAN;
	const int64_t *preferred = vulkan ? vulkanPreferred : glPreferred;
	const int numPreferred = vulkan ? 4 : 2;
	const int numSRGB = vulkan ? 2 : 1;
	for ( int p = 0; p < numPreferred && swapchainFormat == 0; p++ ) {
		for ( int i = 0; i < formats.Num(); i++ ) {
			if ( formats[i] == preferred[p] ) {
				swapchainFormat = formats[i];
				if ( p >= numSRGB ) {
					common->Warning( "OpenXR: the runtime offers no sRGB swapchain format; colours may look washed out" );
				}
				break;
			}
		}
	}
	return swapchainFormat != 0;
}

void idVRSystemOpenXR::DestroySwapchain( swapchain_t &swapchain ) {
	const XrSwapchain handle = swapchain.handle;
	swapchain.handle = XR_NULL_HANDLE;
	swapchain.images.Clear();
	swapchain.acquired = false;
	swapchain.acquiredIndex = 0;
	swapchain.width = swapchain.height = 0;
	if ( handle != XR_NULL_HANDLE ) {
		xrDestroySwapchain( handle );
	}
}

/*
====================
idVRSystemOpenXR::CreateActions
====================
*/
bool idVRSystemOpenXR::CreateActions( void ) {
	handPaths[VR_HAND_LEFT] = Path( "/user/hand/left" );
	handPaths[VR_HAND_RIGHT] = Path( "/user/hand/right" );
	if ( handPaths[VR_HAND_LEFT] == XR_NULL_PATH || handPaths[VR_HAND_RIGHT] == XR_NULL_PATH ) {
		return false;
	}

	const idLangDict *strings = common->GetLanguageDict();
	XrActionSetCreateInfo setInfo;
	memset( &setInfo, 0, sizeof( setInfo ) );
	setInfo.type = XR_TYPE_ACTION_SET_CREATE_INFO;
	idStr::Copynz( setInfo.actionSetName, "gameplay", sizeof( setInfo.actionSetName ) );
	idStr::Copynz( setInfo.localizedActionSetName, strings->GetString( "#str_230110" ), sizeof( setInfo.localizedActionSetName ) );
	if ( !Check( xrCreateActionSet( instance, &setInfo, &actionSet ), "xrCreateActionSet" ) ) {
		actionSet = XR_NULL_HANDLE;
		return false;
	}

	for ( int i = 0; i < VR_ACTION_COUNT; i++ ) {
		XrActionCreateInfo actionInfo;
		memset( &actionInfo, 0, sizeof( actionInfo ) );
		actionInfo.type = XR_TYPE_ACTION_CREATE_INFO;
		actionInfo.actionType = vrActionTypes[i];
		idStr::Copynz( actionInfo.actionName, vrActionNames[i], sizeof( actionInfo.actionName ) );
		idStr::Copynz( actionInfo.localizedActionName, strings->GetString( vrActionLocalizedNames[i] ), sizeof( actionInfo.localizedActionName ) );
		actionInfo.countSubactionPaths = VR_NUM_HANDS;
		actionInfo.subactionPaths = handPaths;
		if ( !Check( xrCreateAction( actionSet, &actionInfo, &actions[i] ), "xrCreateAction" ) ) {
			DestroyActions();
			return false;
		}
	}

	// every profile the runtime knows gets the same layout; a profile it
	// rejects (an extension profile on an older runtime) is simply skipped
	for ( size_t p = 0; p < sizeof( vrProfiles ) / sizeof( vrProfiles[0] ); p++ ) {
		const vrProfile_t &profile = vrProfiles[p];
		idList<XrActionSuggestedBinding> suggested;
		for ( int b = 0; b < profile.numBindings; b++ ) {
			const vrBinding_t &binding = profile.bindings[b];
			XrActionSuggestedBinding entry;
			entry.action = actions[binding.action];
			entry.binding = Path( va( "/user/hand/%s/%s", binding.hand == VR_HAND_LEFT ? "left" : "right", binding.component ) );
			if ( entry.binding != XR_NULL_PATH ) {
				suggested.Append( entry );
			}
		}
		XrInteractionProfileSuggestedBinding suggestion;
		memset( &suggestion, 0, sizeof( suggestion ) );
		suggestion.type = XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING;
		suggestion.interactionProfile = Path( profile.path );
		if ( recovery != RECOVERY_NONE ) {
			return false;
		}
		suggestion.countSuggestedBindings = static_cast<uint32_t>( suggested.Num() );
		suggestion.suggestedBindings = suggested.Ptr();
		const XrResult result = xrSuggestInteractionProfileBindings( instance, &suggestion );
		if ( result == XR_ERROR_INSTANCE_LOST || result == XR_ERROR_RUNTIME_FAILURE ) {
			Check( result, "xrSuggestInteractionProfileBindings" );
			return false;
		}
		if ( XR_FAILED( result ) && vr_debug.GetInteger() > 0 ) {
			common->Printf( "OpenXR: runtime declined bindings for %s (%d)\n", profile.path, static_cast<int>( result ) );
		}
	}

	for ( int hand = 0; hand < VR_NUM_HANDS; hand++ ) {
		XrActionSpaceCreateInfo spaceInfo;
		memset( &spaceInfo, 0, sizeof( spaceInfo ) );
		spaceInfo.type = XR_TYPE_ACTION_SPACE_CREATE_INFO;
		spaceInfo.subactionPath = handPaths[hand];
		spaceInfo.poseInActionSpace = VR_IdentityPose();
		spaceInfo.action = actions[VR_ACTION_AIM_POSE];
		if ( !Check( xrCreateActionSpace( session, &spaceInfo, &aimSpaces[hand] ), "xrCreateActionSpace( aim )" ) ) {
			return false;
		}
		spaceInfo.action = actions[VR_ACTION_GRIP_POSE];
		if ( !Check( xrCreateActionSpace( session, &spaceInfo, &gripSpaces[hand] ), "xrCreateActionSpace( grip )" ) ) {
			return false;
		}
	}

	XrSessionActionSetsAttachInfo attachInfo;
	memset( &attachInfo, 0, sizeof( attachInfo ) );
	attachInfo.type = XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO;
	attachInfo.countActionSets = 1;
	attachInfo.actionSets = &actionSet;
	actionsAttached = Check( xrAttachSessionActionSets( session, &attachInfo ), "xrAttachSessionActionSets" );
	return actionsAttached;
}

void idVRSystemOpenXR::DestroyActions( void ) {
	for ( int hand = 0; hand < VR_NUM_HANDS; hand++ ) {
		if ( aimSpaces[hand] != XR_NULL_HANDLE ) {
			xrDestroySpace( aimSpaces[hand] );
			aimSpaces[hand] = XR_NULL_HANDLE;
		}
		if ( gripSpaces[hand] != XR_NULL_HANDLE ) {
			xrDestroySpace( gripSpaces[hand] );
			gripSpaces[hand] = XR_NULL_HANDLE;
		}
	}
	if ( actionSet != XR_NULL_HANDLE ) {
		// destroying the set destroys its actions
		xrDestroyActionSet( actionSet );
		actionSet = XR_NULL_HANDLE;
	}
	for ( int i = 0; i < VR_ACTION_COUNT; i++ ) {
		actions[i] = XR_NULL_HANDLE;
	}
	actionsAttached = false;
}

/*
====================
idVRSystemOpenXR::StartIfRequested
====================
*/
void idVRSystemOpenXR::StartIfRequested( void ) {
	if ( restartRequested || vr_enable.IsModified() || vr_renderScale.IsModified() ) {
		const bool restart = restartRequested || vr_renderScale.IsModified();
		restartRequested = false;
		vr_enable.ClearModified();
		vr_renderScale.ClearModified();
		if ( !vr_enable.GetBool() || restart ) {
			DestroySession();
			// the instance whose runtime made the Vulkan renderer's device stays
			// for a restart: a new one could bind only after a renderer restart
			const bool keepForVulkan = vr_enable.GetBool() && !runtimeStopped
				&& vulkanCreator != XR_NULL_HANDLE && vulkanCreator == instance;
			if ( !keepForVulkan ) {
				DestroyInstance();
			}
		}
		runtimeStopped = false;
		startFailureLogged = false;
		// the player asked: look for the runtime and the headset now, not when a
		// retry that an earlier failure scheduled comes round
		nextInstanceRetryTime = 0;
		nextSystemRetryTime = 0;
		nextSessionRetryTime = 0;
	}
	// Once the runtime has ended VR, creating an instance could relaunch it
	// (SteamVR starts with its first client), so only vr_restart resumes.
	if ( runtimeStopped || !vr_enable.GetBool() || !rendererReady || !renderSystem->IsOpenGLRunning() ) {
		return;
	}
	if ( instance == XR_NULL_HANDLE ) {
		if ( Sys_Milliseconds() < nextInstanceRetryTime ) {
			return;
		}
		if ( !CreateInstance() ) {
			// no runtime: try again later rather than every frame
			nextInstanceRetryTime = Sys_Milliseconds() + VR_SYSTEM_RETRY_MSEC * 3;
			return;
		}
	}
	if ( !AcquireSystem() ) {
		return;
	}
	if ( session == XR_NULL_HANDLE ) {
		if ( Sys_Milliseconds() < nextSessionRetryTime ) {
			return;
		}
		if ( !CreateSession() ) {
			nextSessionRetryTime = Sys_Milliseconds() + VR_SYSTEM_RETRY_MSEC;
		}
	}
}

/*
====================
idVRSystemOpenXR::PollEvents
====================
*/
void idVRSystemOpenXR::PollEvents( void ) {
	for ( int guard = 0; guard < 64 && instance != XR_NULL_HANDLE && recovery == RECOVERY_NONE; guard++ ) {
		XrEventDataBuffer event;
		memset( &event, 0, sizeof( event ) );
		event.type = XR_TYPE_EVENT_DATA_BUFFER;
		const XrResult result = xrPollEvent( instance, &event );
		if ( result != XR_SUCCESS ) {
			if ( XR_FAILED( result ) ) {
				Check( result, "xrPollEvent" );
			}
			return;
		}
		switch ( event.type ) {
			case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: {
				const XrEventDataSessionStateChanged *changed = reinterpret_cast<const XrEventDataSessionStateChanged *>( &event );
				if ( changed->session == session ) {
					HandleSessionState( changed->state );
				}
				break;
			}
			case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING:
				common->Warning( "OpenXR: the runtime is going away; vr_restart starts VR again once it is back" );
				recovery = RECOVERY_INSTANCE;
				runtimeStopped = true;
				return;
			case XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED: {
				interactionProfile.Clear();
				XrInteractionProfileState state;
				memset( &state, 0, sizeof( state ) );
				state.type = XR_TYPE_INTERACTION_PROFILE_STATE;
				const XrPath hand = vr_leftHanded.GetBool() ? handPaths[VR_HAND_LEFT] : handPaths[VR_HAND_RIGHT];
				if ( session != XR_NULL_HANDLE && hand != XR_NULL_PATH && Check( xrGetCurrentInteractionProfile( session, hand, &state ), "xrGetCurrentInteractionProfile" )
						&& state.interactionProfile != XR_NULL_PATH ) {
					char path[XR_MAX_PATH_LENGTH];
					uint32_t length = 0;
					if ( Check( xrPathToString( instance, state.interactionProfile, sizeof( path ), &length, path ), "xrPathToString" ) ) {
						interactionProfile = path;
					}
				}
				common->Printf( "OpenXR: controllers %s\n", interactionProfile.Length() ? interactionProfile.c_str() : "disconnected" );
				break;
			}
			case XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING: {
				// The user recentred in the runtime, or it redrew the play area:
				// follow it. The space only moves for frames displayed from
				// changeTime on, so a recentre taken before then would measure the
				// head in the old space and jump when the new one arrived.
				const XrEventDataReferenceSpaceChangePending *pending = reinterpret_cast<const XrEventDataReferenceSpaceChangePending *>( &event );
				if ( pending->session == session ) {
					spaceChangeTime = spaceChangePending ? Max( spaceChangeTime, pending->changeTime ) : pending->changeTime;
					spaceChangePending = true;
					if ( vr_debug.GetInteger() > 0 ) {
						common->Printf( "OpenXR: the runtime moves reference space %d in %.0f ms; the VR origin follows it then\n",
							static_cast<int>( pending->referenceSpaceType ),
							frameDisplayTime > 0 ? static_cast<double>( pending->changeTime - frameDisplayTime ) * 1.0e-6 : 0.0 );
					}
				}
				break;
			}
			default:
				break;
		}
	}
}

void idVRSystemOpenXR::HandleSessionState( XrSessionState state ) {
	sessionState = state;
	if ( vr_debug.GetInteger() > 0 ) {
		common->Printf( "OpenXR: session state %d\n", static_cast<int>( state ) );
	}
	switch ( state ) {
		case XR_SESSION_STATE_READY: {
			XrSessionBeginInfo beginInfo;
			memset( &beginInfo, 0, sizeof( beginInfo ) );
			beginInfo.type = XR_TYPE_SESSION_BEGIN_INFO;
			beginInfo.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
			if ( Check( xrBeginSession( session, &beginInfo ), "xrBeginSession" ) ) {
				sessionRunning = true;
				frameDelta.Reset();
				common->Printf( "OpenXR: presenting to the headset\n" );
			} else if ( recovery == RECOVERY_NONE ) {
				// READY is announced once: a session that would not begin never
				// presents, so open a new one after the usual pause
				recovery = RECOVERY_SESSION;
			}
			break;
		}
		case XR_SESSION_STATE_FOCUSED:
			sessionFocused = true;
			break;
		case XR_SESSION_STATE_VISIBLE:
		case XR_SESSION_STATE_SYNCHRONIZED:
			// the runtime's own menu took the controllers, or the headset came off
			focusLost = focusLost || sessionFocused;
			sessionFocused = false;
			break;
		case XR_SESSION_STATE_STOPPING:
			focusLost = focusLost || sessionFocused;
			sessionFocused = false;
			if ( sessionRunning ) {
				Check( xrEndSession( session ), "xrEndSession" );
				sessionRunning = false;
				common->Printf( "OpenXR: stopped presenting to the headset\n" );
			}
			break;
		case XR_SESSION_STATE_LOSS_PENDING:
			common->Warning( "OpenXR: the headset session was lost; it reopens when the headset returns" );
			recovery = RECOVERY_SESSION;
			break;
		case XR_SESSION_STATE_EXITING:
			// the runtime asked us to leave VR; the game keeps running on the
			// desktop and vr_enable keeps the player's choice for the next launch
			common->Printf( "OpenXR: the runtime ended the VR session; vr_restart starts it again\n" );
			recovery = RECOVERY_INSTANCE;
			runtimeStopped = true;
			break;
		default:
			break;
	}
}

/*
====================
Pose conversion

Poses arrive in the runtime's LOCAL space. The recentre pose (a heading and
a position) becomes the origin the game sees.
====================
*/
XrPosef idVRSystemOpenXR::RecenteredLocalPose( const XrPosef &pose ) const {
	return VR_PoseMultiply( VR_PoseInverse( recenterPose ), pose );
}

vrPose_t idVRSystemOpenXR::ToEnginePose( const XrPosef &localPose, bool valid ) const {
	vrPose_t out;
	memset( &out, 0, sizeof( out ) );
	out.axis.Identity();
	if ( !valid ) {
		return out;
	}
	const XrPosef pose = RecenteredLocalPose( localPose );
	const float unitsPerMetre = vr_worldScale.GetFloat();
	const vrVec3_t origin = VR_XrToEnginePosition( VR_FromXrVec( pose.position ), unitsPerMetre );
	vrVec3_t axis[3];
	VR_QuatToAxis( VR_XrToEngineOrientation( VR_FromXrQuat( pose.orientation ) ), axis );
	if ( !VR_IsFinite( origin ) || !VR_IsFinite( axis[0] ) ) {
		return out;
	}
	out.valid = true;
	out.origin.Set( origin.x, origin.y, origin.z );
	for ( int i = 0; i < 3; i++ ) {
		out.axis[i].Set( axis[i].x, axis[i].y, axis[i].z );
	}
	return out;
}

/*
====================
idVRSystemOpenXR::LocateFrame
====================
*/
void idVRSystemOpenXR::LocateFrame( XrTime time ) {
	viewsValid = false;
	if ( spaceChangePending && time >= spaceChangeTime ) {
		// the first frame located in the space the runtime moved
		spaceChangePending = false;
		recenterRequested = true;
		UpdateStageBounds();
	}
	XrViewLocateInfo locateInfo;
	memset( &locateInfo, 0, sizeof( locateInfo ) );
	locateInfo.type = XR_TYPE_VIEW_LOCATE_INFO;
	locateInfo.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
	locateInfo.displayTime = time;
	locateInfo.space = localSpace;
	XrViewState viewState;
	memset( &viewState, 0, sizeof( viewState ) );
	viewState.type = XR_TYPE_VIEW_STATE;
	for ( int i = 0; i < VR_NUM_EYES; i++ ) {
		memset( &views[i], 0, sizeof( views[i] ) );
		views[i].type = XR_TYPE_VIEW;
		views[i].pose = VR_IdentityPose();
	}
	uint32_t viewCount = 0;
	if ( !CheckFrame( xrLocateViews( session, &locateInfo, &viewState, VR_NUM_EYES, &viewCount, views ), "xrLocateViews" ) ) {
		return;
	}
	if ( viewCount == VR_NUM_EYES && ( viewState.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT ) != 0 ) {
		viewsValid = true;
		if ( ( viewState.viewStateFlags & XR_VIEW_STATE_POSITION_VALID_BIT ) == 0 ) {
			// orientation-only tracking: keep the eyes on a fixed head position
			for ( int i = 0; i < VR_NUM_EYES; i++ ) {
				views[i].pose.position.x = ( i == 0 ? -0.032f : 0.032f );
				views[i].pose.position.y = 0.0f;
				views[i].pose.position.z = 0.0f;
			}
		}
	}

	XrSpaceLocation head;
	memset( &head, 0, sizeof( head ) );
	head.type = XR_TYPE_SPACE_LOCATION;
	if ( !CheckFrame( xrLocateSpace( viewSpace, localSpace, time, &head ), "xrLocateSpace( head )" ) ) {
		return;
	}
	const bool headValid = ( head.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT ) != 0;
	if ( headValid && ( head.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT ) == 0 ) {
		head.pose.position.x = head.pose.position.y = head.pose.position.z = 0.0f;
	}

	if ( ( recenterRequested || calibrateRequested ) && headValid ) {
		recenterPose.orientation = VR_YawOnly( head.pose.orientation );
		recenterPose.position = head.pose.position;
		// standing play keeps the floor: the origin stands at the standing eye
		// height wherever the head is, so a recentre while crouched or seated
		// does not lift the floor
		if ( calibrateRequested ) {
			if ( standingPlay ) {
				vr_standingHeight.SetFloat( idMath::ClampFloat( VR_STANDING_HEIGHT_MIN, VR_STANDING_HEIGHT_MAX, head.pose.position.y ) );
				common->Printf( "OpenXR: standing eye height calibrated to %.2f m\n", vr_standingHeight.GetFloat() );
			} else {
				common->Printf( "OpenXR: seated play takes your height from the recentre\n" );
			}
		}
		bool learned = false;
		recenterPose.position.y = VR_StandingReference( standingPlay, head.pose.position.y, vr_standingHeight.GetFloat(), learned );
		if ( learned ) {
			vr_standingHeight.SetFloat( recenterPose.position.y );
			common->Printf( "OpenXR: standing eye height learned as %.2f m (vr_calibrateHeight measures it again)\n",
				recenterPose.position.y );
		}
		vr_standingHeight.ClearModified();
		recenterRequested = false;
		calibrateRequested = false;
		screenAnchored = false;
		Sys_EnterCriticalSection( VR_INPUT_CRITICAL_SECTION );
		bodyYawValid = false;
		Sys_LeaveCriticalSection( VR_INPUT_CRITICAL_SECTION );
		if ( vr_debug.GetInteger() > 0 ) {
			common->Printf( "OpenXR: recentred at ( %.2f %.2f %.2f ), origin height %.2f m\n", head.pose.position.x, head.pose.position.y,
				head.pose.position.z, recenterPose.position.y );
		}
	}

	frame.head = ToEnginePose( head.pose, headValid );
	for ( int i = 0; i < VR_NUM_EYES; i++ ) {
		frame.eyes[i].pose = ToEnginePose( views[i].pose, viewsValid );
		const vrFovTangents_t fov = VR_FovTangentsFromAngles( views[i].fov.angleLeft, views[i].fov.angleRight, views[i].fov.angleUp, views[i].fov.angleDown );
		frame.eyes[i].tanLeft = fov.left;
		frame.eyes[i].tanRight = fov.right;
		frame.eyes[i].tanUp = fov.up;
		frame.eyes[i].tanDown = fov.down;
		if ( !VR_FovTangentsValid( fov ) ) {
			viewsValid = false;
		}
	}

	for ( int hand = 0; hand < VR_NUM_HANDS; hand++ ) {
		XrSpaceLocation location;
		memset( &location, 0, sizeof( location ) );
		location.type = XR_TYPE_SPACE_LOCATION;
		const XrSpaceLocationFlags required = XR_SPACE_LOCATION_ORIENTATION_VALID_BIT | XR_SPACE_LOCATION_POSITION_VALID_BIT;
		if ( aimSpaces[hand] != XR_NULL_HANDLE && !CheckFrame( xrLocateSpace( aimSpaces[hand], localSpace, time, &location ), "xrLocateSpace( aim )" ) ) {
			return;
		}
		bool valid = aimSpaces[hand] != XR_NULL_HANDLE && ( location.locationFlags & required ) == required;
		frame.aim[hand] = ToEnginePose( location.pose, valid );
		aimLocalPoses[hand] = valid ? location.pose : VR_IdentityPose();
		aimLocalValid[hand] = valid;
		memset( &location, 0, sizeof( location ) );
		location.type = XR_TYPE_SPACE_LOCATION;
		if ( gripSpaces[hand] != XR_NULL_HANDLE && !CheckFrame( xrLocateSpace( gripSpaces[hand], localSpace, time, &location ), "xrLocateSpace( grip )" ) ) {
			return;
		}
		valid = gripSpaces[hand] != XR_NULL_HANDLE && ( location.locationFlags & required ) == required;
		frame.grip[hand] = ToEnginePose( location.pose, valid );
	}
}

/*
====================
Input
====================
*/
bool idVRSystemOpenXR::MenuMode( void ) const {
	return ( console != NULL && console->Active() ) || ( ::session != NULL && ::session->IsGUIActive() ) || RetainedUI_IsOpen();
}

// The off hand's palm is on the gun's foregrip: ahead of the weapon hand's
// palm and close to the line it aims along (VR_OffHandOnForegrip).
bool idVRSystemOpenXR::OffHandOnForegrip( int weaponHand ) const {
	const int offHand = 1 - weaponHand;
	const vrPose_t &aim = frame.aim[weaponHand];
	const vrPose_t &rear = frame.grip[weaponHand];
	const vrPose_t &front = frame.grip[offHand];
	if ( !aim.valid || !rear.valid || !front.valid ) {
		return false;
	}
	return VR_OffHandOnForegrip( VR_Vec3( rear.origin.x, rear.origin.y, rear.origin.z ),
		VR_Vec3( aim.axis[0].x, aim.axis[0].y, aim.axis[0].z ),
		VR_Vec3( front.origin.x, front.origin.y, front.origin.z ), vr_worldScale.GetFloat() );
}

// Two hands on the gun: the weapon hand's aim keeps its origin and roll and
// points from its palm through the other one, so the usercmd, the drawn gun,
// the laser and the shots all follow the steadier two-handed line.
void idVRSystemOpenXR::ApplyTwoHandedAim( int weaponHand ) {
	const int offHand = 1 - weaponHand;
	vrPose_t &aim = frame.aim[weaponHand];
	const vrPose_t &rear = frame.grip[weaponHand];
	const vrPose_t &front = frame.grip[offHand];
	if ( !aim.valid || !rear.valid || !front.valid ) {
		return;
	}
	vrVec3_t rows[3];
	if ( !VR_TwoHandedAxis( VR_Vec3( rear.origin.x, rear.origin.y, rear.origin.z ),
			VR_Vec3( front.origin.x, front.origin.y, front.origin.z ),
			VR_Vec3( aim.axis[2].x, aim.axis[2].y, aim.axis[2].z ), rows ) ) {
		return;
	}
	for ( int i = 0; i < 3; i++ ) {
		aim.axis[i].Set( rows[i].x, rows[i].y, rows[i].z );
	}
}

void idVRSystemOpenXR::PostKey( int key, bool down ) {
	if ( key > 0 ) {
		if ( vr_debug.GetInteger() > 0 ) {
			common->Printf( "OpenXR: %s %s\n", idKeyInput::KeyNumToString( key, false ), down ? "down" : "up" );
		}
		Sys_PostVRControllerKey( key, down );
	}
}

// A key slot remembers which key it pressed, so a release always matches its
// press even if the menu state changed the mapping in between.
void idVRSystemOpenXR::UpdateKey( int slot, int key, bool down ) {
	if ( slot < 0 || slot >= 16 ) {
		return;
	}
	if ( down ) {
		if ( heldKeys[slot] == key ) {
			return;
		}
		if ( heldKeys[slot] != 0 ) {
			PostKey( heldKeys[slot], false );
		}
		heldKeys[slot] = key;
		PostKey( key, true );
	} else if ( heldKeys[slot] != 0 ) {
		PostKey( heldKeys[slot], false );
		heldKeys[slot] = 0;
	}
}

enum vrKeySlot_t {
	VR_SLOT_WEAPON_TRIGGER,
	VR_SLOT_OFF_TRIGGER,
	VR_SLOT_WEAPON_SQUEEZE,
	VR_SLOT_OFF_SQUEEZE,
	VR_SLOT_RIGHT_PRIMARY,
	VR_SLOT_RIGHT_SECONDARY,
	VR_SLOT_LEFT_PRIMARY,
	VR_SLOT_LEFT_SECONDARY,
	VR_SLOT_MENU,
	VR_SLOT_WEAPON_STICK_CLICK,
	VR_SLOT_OFF_STICK_CLICK,
	VR_SLOT_WEAPON_STICK_Y,
	VR_SLOT_MENU_STICK_X,
	VR_SLOT_MENU_STICK_Y
};

void idVRSystemOpenXR::SyncInput( float frameSeconds ) {
	bool synced = false;
	if ( actionsAttached && sessionFocused ) {
		XrActiveActionSet active;
		active.actionSet = actionSet;
		active.subactionPath = XR_NULL_PATH;
		XrActionsSyncInfo syncInfo;
		memset( &syncInfo, 0, sizeof( syncInfo ) );
		syncInfo.type = XR_TYPE_ACTIONS_SYNC_INFO;
		syncInfo.countActiveActionSets = 1;
		syncInfo.activeActionSets = &active;
		const XrResult result = xrSyncActions( session, &syncInfo );
		synced = result == XR_SUCCESS;
		if ( result != XR_SUCCESS && result != XR_SESSION_NOT_FOCUSED ) {
			CheckFrame( result, "xrSyncActions" );
		}
	}
	if ( recovery != RECOVERY_NONE ) {
		return;
	}

	memset( hands, 0, sizeof( hands ) );
	if ( synced ) {
		for ( int hand = 0; hand < VR_NUM_HANDS; hand++ ) {
			XrActionStateGetInfo getInfo;
			memset( &getInfo, 0, sizeof( getInfo ) );
			getInfo.type = XR_TYPE_ACTION_STATE_GET_INFO;
			getInfo.subactionPath = handPaths[hand];

			XrActionStateFloat floatState;
			memset( &floatState, 0, sizeof( floatState ) );
			floatState.type = XR_TYPE_ACTION_STATE_FLOAT;
			getInfo.action = actions[VR_ACTION_TRIGGER];
			if ( !CheckFrame( xrGetActionStateFloat( session, &getInfo, &floatState ), "xrGetActionStateFloat( trigger )" ) ) {
				return;
			}
			if ( floatState.isActive ) {
				hands[hand].trigger = floatState.currentState;
			}
			getInfo.action = actions[VR_ACTION_SQUEEZE];
			if ( !CheckFrame( xrGetActionStateFloat( session, &getInfo, &floatState ), "xrGetActionStateFloat( squeeze )" ) ) {
				return;
			}
			if ( floatState.isActive ) {
				hands[hand].squeeze = floatState.currentState;
			}

			XrActionStateVector2f vectorState;
			memset( &vectorState, 0, sizeof( vectorState ) );
			vectorState.type = XR_TYPE_ACTION_STATE_VECTOR2F;
			getInfo.action = actions[VR_ACTION_THUMBSTICK];
			if ( !CheckFrame( xrGetActionStateVector2f( session, &getInfo, &vectorState ), "xrGetActionStateVector2f" ) ) {
				return;
			}
			if ( vectorState.isActive ) {
				hands[hand].stickX = vectorState.currentState.x;
				hands[hand].stickY = vectorState.currentState.y;
			}

			XrActionStateBoolean boolState;
			memset( &boolState, 0, sizeof( boolState ) );
			boolState.type = XR_TYPE_ACTION_STATE_BOOLEAN;
			getInfo.action = actions[VR_ACTION_THUMBSTICK_CLICK];
			if ( !CheckFrame( xrGetActionStateBoolean( session, &getInfo, &boolState ), "xrGetActionStateBoolean( stick )" ) ) {
				return;
			}
			hands[hand].stickClick = boolState.isActive && boolState.currentState;
			getInfo.action = actions[VR_ACTION_PRIMARY];
			if ( !CheckFrame( xrGetActionStateBoolean( session, &getInfo, &boolState ), "xrGetActionStateBoolean( primary )" ) ) {
				return;
			}
			hands[hand].primary = boolState.isActive && boolState.currentState;
			getInfo.action = actions[VR_ACTION_SECONDARY];
			if ( !CheckFrame( xrGetActionStateBoolean( session, &getInfo, &boolState ), "xrGetActionStateBoolean( secondary )" ) ) {
				return;
			}
			hands[hand].secondary = boolState.isActive && boolState.currentState;
			getInfo.action = actions[VR_ACTION_MENU];
			if ( !CheckFrame( xrGetActionStateBoolean( session, &getInfo, &boolState ), "xrGetActionStateBoolean( menu )" ) ) {
				return;
			}
			hands[hand].menu = boolState.isActive && boolState.currentState;
		}
	}

	const int weaponHand = vr_leftHanded.GetBool() ? VR_HAND_LEFT : VR_HAND_RIGHT;
	const int offHand = 1 - weaponHand;
	const handInput_t &weapon = hands[weaponHand];
	const handInput_t &off = hands[offHand];
	const bool menu = MenuMode();
	const float deadzone = idMath::ClampFloat( 0.0f, 0.9f, vr_stickDeadzone.GetFloat() );
	UpdateMenuPointer( weaponHand, menu );

	// Buttons are the SDL gamepad's JOY keys (content/baseoq4/pak0/default.cfg):
	// RT attack, LT zoom, A jump, B crouch, X reload, Y flashlight, LB weapon
	// wheel, RB last weapon, Start menu, L3 run, R3 centre view. Menus navigate
	// with the same keys and the D-pad keys below. A trigger pulled while the
	// pointer rests on a menu clicks there instead, and stays a click until it
	// is released.
	const int heldTrigger = heldKeys[VR_SLOT_WEAPON_TRIGGER];
	UpdateKey( VR_SLOT_WEAPON_TRIGGER, heldTrigger != 0 ? heldTrigger : ( pointerOnScreen ? K_MOUSE1 : K_JOY15 ),
		weapon.trigger > ( heldTrigger != 0 ? 0.55f : 0.75f ) );
	UpdateKey( VR_SLOT_OFF_TRIGGER, K_JOY16, off.trigger > ( heldKeys[VR_SLOT_OFF_TRIGGER] ? 0.55f : 0.75f ) );
	UpdateKey( VR_SLOT_WEAPON_SQUEEZE, K_JOY2, weapon.squeeze > ( heldKeys[VR_SLOT_WEAPON_SQUEEZE] ? 0.55f : 0.75f ) );
	// The off-hand squeeze opens the weapon wheel, unless it closes on the
	// gun's foregrip: then it steadies the gun in both hands until released.
	// Which one is decided when the squeeze closes and kept while it is held.
	const bool offSqueezed = off.squeeze > ( ( heldKeys[VR_SLOT_OFF_SQUEEZE] || twoHanded ) ? 0.55f : 0.75f );
	if ( !offSqueezed ) {
		if ( twoHanded && vr_debug.GetInteger() > 0 ) {
			common->Printf( "OpenXR: two-handed aim released\n" );
		}
		twoHanded = false;
	} else if ( !twoHanded && heldKeys[VR_SLOT_OFF_SQUEEZE] == 0 && !menu && vr_twoHanded.GetBool()
			&& vr_aimMode.GetInteger() == VR_AIM_HAND && OffHandOnForegrip( weaponHand ) ) {
		twoHanded = true;
		Vibrate( offHand, 0.35f, 20 );
		if ( recovery != RECOVERY_NONE ) {
			return;
		}
		if ( vr_debug.GetInteger() > 0 ) {
			common->Printf( "OpenXR: two-handed aim held\n" );
		}
	}
	UpdateKey( VR_SLOT_OFF_SQUEEZE, K_JOY1, offSqueezed && !twoHanded );
	UpdateKey( VR_SLOT_RIGHT_PRIMARY, K_JOY3, hands[VR_HAND_RIGHT].primary );
	UpdateKey( VR_SLOT_RIGHT_SECONDARY, K_JOY4, hands[VR_HAND_RIGHT].secondary );
	UpdateKey( VR_SLOT_LEFT_PRIMARY, K_JOY6, hands[VR_HAND_LEFT].primary );
	UpdateKey( VR_SLOT_LEFT_SECONDARY, K_JOY5, hands[VR_HAND_LEFT].secondary );
	UpdateKey( VR_SLOT_MENU, K_JOY7, hands[VR_HAND_LEFT].menu || hands[VR_HAND_RIGHT].menu );
	UpdateKey( VR_SLOT_WEAPON_STICK_CLICK, K_JOY14, weapon.stickClick );
	UpdateKey( VR_SLOT_OFF_STICK_CLICK, K_JOY13, off.stickClick );

	float turn = 0.0f;
	if ( menu ) {
		// either stick is a D-pad: up, down, right, left
		const float stickX = idMath::Fabs( weapon.stickX ) > idMath::Fabs( off.stickX ) ? weapon.stickX : off.stickX;
		const float stickY = idMath::Fabs( weapon.stickY ) > idMath::Fabs( off.stickY ) ? weapon.stickY : off.stickY;
		UpdateKey( VR_SLOT_WEAPON_STICK_Y, 0, false );
		const int heldY = heldKeys[VR_SLOT_MENU_STICK_Y];
		const int heldX = heldKeys[VR_SLOT_MENU_STICK_X];
		UpdateKey( VR_SLOT_MENU_STICK_Y, stickY > 0.0f ? K_JOY9 : K_JOY10, idMath::Fabs( stickY ) > ( heldY ? 0.35f : 0.75f ) );
		UpdateKey( VR_SLOT_MENU_STICK_X, stickX > 0.0f ? K_JOY11 : K_JOY12, idMath::Fabs( stickX ) > ( heldX ? 0.35f : 0.75f ) );
		snapTurn.latched = true;
	} else {
		UpdateKey( VR_SLOT_MENU_STICK_X, 0, false );
		UpdateKey( VR_SLOT_MENU_STICK_Y, 0, false );
		// weapon stick up / down pages weapons (JOY12 next, JOY11 previous)
		const int held = heldKeys[VR_SLOT_WEAPON_STICK_Y];
		UpdateKey( VR_SLOT_WEAPON_STICK_Y, weapon.stickY > 0.0f ? K_JOY12 : K_JOY11, idMath::Fabs( weapon.stickY ) > ( held ? 0.35f : 0.75f )
			&& idMath::Fabs( weapon.stickY ) > idMath::Fabs( weapon.stickX ) );
		if ( vr_turnMode.GetInteger() == 1 ) {
			turn = VR_SmoothTurn( weapon.stickX, vr_smoothTurnSpeed.GetFloat(), frameSeconds, deadzone );
		} else {
			turn = VR_SnapTurn( snapTurn, weapon.stickX, vr_snapTurnAngle.GetFloat() );
		}
	}

	// two hands on the gun: it points from the rear palm through the front one
	if ( twoHanded && !menu ) {
		ApplyTwoHandedAim( weaponHand );
	}

	// aim: the weapon hand's pointing ray, or the head
	const vrPose_t &aimPose = ( vr_aimMode.GetInteger() == VR_AIM_HAND && frame.aim[weaponHand].valid ) ? frame.aim[weaponHand] : frame.head;
	float localYaw = aimLocalYaw;
	float localPitch = aimLocalPitch;
	if ( aimPose.valid ) {
		const vrVec3_t forward = VR_Vec3( aimPose.axis[0].x, aimPose.axis[0].y, aimPose.axis[0].z );
		localYaw = VR_YawDegrees( forward );
		localPitch = idMath::ClampFloat( -89.0f, 89.0f, VR_PitchDegrees( forward ) );
	}

	// locomotion: the off-hand stick, relative to the head or the off-hand
	// controller, expressed in the aim frame the usercmd moves along
	float forwardMove = 0.0f, rightMove = 0.0f;
	if ( !menu ) {
		const float stickX = VR_ApplyDeadzone( off.stickX, deadzone );
		const float stickY = VR_ApplyDeadzone( off.stickY, deadzone );
		if ( stickX != 0.0f || stickY != 0.0f ) {
			const vrPose_t &reference = ( vr_moveDirection.GetInteger() == 1 && frame.aim[offHand].valid ) ? frame.aim[offHand] : frame.head;
			const float referenceYaw = reference.valid
				? VR_YawDegrees( VR_Vec3( reference.axis[0].x, reference.axis[0].y, reference.axis[0].z ) ) : localYaw;
			const float delta = DEG2RAD( referenceYaw - localYaw );
			const float c = idMath::Cos( delta ), s = idMath::Sin( delta );
			// stick +Y forward, +X right; the engine's left is -right
			const float forwardRef = stickY, leftRef = -stickX;
			const float forwardAim = c * forwardRef - s * leftRef;
			const float leftAim = s * forwardRef + c * leftRef;
			forwardMove = idMath::ClampFloat( -1.0f, 1.0f, forwardAim );
			rightMove = idMath::ClampFloat( -1.0f, 1.0f, -leftAim );
		}
	}

	// Multiplayer room scale: a server moves bodies only from usercmds, so
	// while the stick rests a head beyond the free radius walks the body after
	// it the way the stick would, and ShiftTrackingOrigin then takes the
	// body's predicted progress. Single player walks the body directly.
	const bool stickIdle = forwardMove == 0.0f && rightMove == 0.0f;
	if ( !menu && stickIdle && vr_roomScale.GetBool() && idAsyncNetwork::IsActive() && frame.head.valid ) {
		float walkForward, walkRight;
		if ( VR_RoomScaleWalkInput( frame.head.origin.x, frame.head.origin.y, localYaw, roomScaleWalking, walkForward, walkRight ) ) {
			forwardMove = walkForward;
			rightMove = walkRight;
			roomScaleWalkMsec = Sys_Milliseconds();
		}
	} else {
		roomScaleWalking = false;
	}
	roomScaleStickIdle = stickIdle;

	// physical crouch: the head well below where it was recentred crouches the body
	const float unitsPerMetre = vr_worldScale.GetFloat();
	const bool crouching = !menu && vr_physicalCrouch.GetBool() && frame.head.valid && unitsPerMetre > 0.0f
		&& VR_PhysicalCrouch( -frame.head.origin.z / unitsPerMetre, physicalCrouch );

	Sys_EnterCriticalSection( VR_INPUT_CRITICAL_SECTION );
	inputValid = sessionRunning && sessionFocused;
	physicalCrouch = crouching;
	if ( bodyYawValid ) {
		bodyYaw = VR_NormalizeDegrees180( bodyYaw + turn );
	}
	aimLocalYaw = localYaw;
	aimLocalPitch = localPitch;
	moveForward = forwardMove;
	moveRight = rightMove;
	frame.bodyYaw = bodyYaw;
	Sys_LeaveCriticalSection( VR_INPUT_CRITICAL_SECTION );
}

bool idVRSystemOpenXR::GetUsercmdInput( float otherYawDelta, float currentYaw, vrUsercmdInput_t &input ) {
	memset( &input, 0, sizeof( input ) );
	Sys_EnterCriticalSection( VR_INPUT_CRITICAL_SECTION );
	if ( !inputValid ) {
		Sys_LeaveCriticalSection( VR_INPUT_CRITICAL_SECTION );
		return false;
	}
	if ( !bodyYawValid ) {
		// keep the current aim continuous when VR takes over
		bodyYaw = VR_NormalizeDegrees180( currentYaw - aimLocalYaw );
		bodyYawValid = true;
	} else {
		// the mouse, keys or a gamepad turned this tic: that turns the body
		bodyYaw = VR_NormalizeDegrees180( bodyYaw + otherYawDelta );
	}
	input.aimYaw = bodyYaw + aimLocalYaw;
	input.aimPitch = aimLocalPitch;
	input.forward = moveForward;
	input.right = moveRight;
	input.crouch = physicalCrouch;
	Sys_LeaveCriticalSection( VR_INPUT_CRITICAL_SECTION );
	return true;
}

/*
====================
idVRSystemOpenXR::ResetBodyYaw

The body's yaw is absolute usercmd yaw, and a new map zeroes the generator's
angles and spawns the player against a zeroed usercmd. Left as it was, every
snap turn of the level before turned the next level's start. The next usercmd
takes the yaw again from the zeroed angles, as when VR first took over input.
====================
*/
void idVRSystemOpenXR::ResetBodyYaw( void ) {
	Sys_EnterCriticalSection( VR_INPUT_CRITICAL_SECTION );
	bodyYawValid = false;
	Sys_LeaveCriticalSection( VR_INPUT_CRITICAL_SECTION );
}

/*
====================
idVRSystemOpenXR::GetFrameState
====================
*/
void idVRSystemOpenXR::GetFrameState( vrFrameState_t &state ) const {
	Sys_EnterCriticalSection( VR_INPUT_CRITICAL_SECTION );
	state = frame;
	state.bodyYaw = bodyYaw;
	Sys_LeaveCriticalSection( VR_INPUT_CRITICAL_SECTION );
}

/*
====================
Frame loop
====================
*/
bool idVRSystemOpenXR::AcquireImages( void ) {
	swapchain_t *chains[3] = { &screenSwapchain, &eyeSwapchains[0], &eyeSwapchains[1] };
	for ( int i = 0; i < 3; i++ ) {
		swapchain_t &chain = *chains[i];
		if ( recovery != RECOVERY_NONE ) {
			return false;
		}
		if ( chain.handle == XR_NULL_HANDLE || chain.acquired ) {
			recovery = RECOVERY_SESSION;
			return false;
		}
		XrSwapchainImageAcquireInfo acquireInfo;
		memset( &acquireInfo, 0, sizeof( acquireInfo ) );
		acquireInfo.type = XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO;
		uint32_t index = 0;
		if ( !CheckFrame( xrAcquireSwapchainImage( chain.handle, &acquireInfo, &index ), "xrAcquireSwapchainImage" ) ) {
			return false;
		}
		if ( index >= static_cast<uint32_t>( chain.images.Num() ) ) {
			common->Warning( "OpenXR: acquired image index %u exceeds %d images", index, chain.images.Num() );
			recovery = RECOVERY_SESSION;
			return false;
		}
		XrSwapchainImageWaitInfo waitInfo;
		memset( &waitInfo, 0, sizeof( waitInfo ) );
		waitInfo.type = XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO;
		waitInfo.timeout = VR_IMAGE_WAIT_NSEC;
		if ( !CheckFrame( xrWaitSwapchainImage( chain.handle, &waitInfo ), "xrWaitSwapchainImage" ) ) {
			// A timeout is a positive OpenXR status, but never grants image
			// access. Cancel through session destruction instead of releasing
			// an image that has not successfully completed its wait.
			return false;
		}
		chain.acquiredIndex = index;
		chain.acquired = true;
	}
	return true;
}

void idVRSystemOpenXR::ReleaseImages( void ) {
	swapchain_t *chains[3] = { &screenSwapchain, &eyeSwapchains[0], &eyeSwapchains[1] };
	for ( int i = 0; i < 3; i++ ) {
		if ( recovery != RECOVERY_NONE ) {
			return;
		}
		swapchain_t &chain = *chains[i];
		if ( !chain.acquired || chain.handle == XR_NULL_HANDLE ) {
			chain.acquired = false;
			continue;
		}
		XrSwapchainImageReleaseInfo releaseInfo;
		memset( &releaseInfo, 0, sizeof( releaseInfo ) );
		releaseInfo.type = XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO;
		chain.acquired = false;
		if ( !CheckFrame( xrReleaseSwapchainImage( chain.handle, &releaseInfo ), "xrReleaseSwapchainImage" ) ) {
			return;
		}
	}
}

/*
====================
Virtual screen

Menus, the HUD and their pointer all lay out for the window's 2D viewport, so
the virtual screen takes its aspect (4:3 through 32:9) at a fixed width;
another shape would stretch them.
====================
*/
void idVRSystemOpenXR::ScreenSizeForWindow( int &width, int &height ) const {
	width = VR_SCREEN_WIDTH;
	height = VR_SCREEN_HEIGHT;
	if ( engineWindowState.uiViewportWidth > 0 && engineWindowState.uiViewportHeight > 0 ) {
		const float aspect = static_cast<float>( engineWindowState.uiViewportWidth ) / static_cast<float>( engineWindowState.uiViewportHeight );
		height = idMath::Ftoi( static_cast<float>( VR_SCREEN_WIDTH ) / idMath::ClampFloat( 4.0f / 3.0f, 32.0f / 9.0f, aspect ) ) & ~1;
	}
}

// Follows a window resize between frames; a failed size is not retried until
// the window changes again.
bool idVRSystemOpenXR::ResizeScreenSwapchain( void ) {
	int width = 0, height = 0;
	ScreenSizeForWindow( width, height );
	if ( screenSwapchain.handle != XR_NULL_HANDLE && width == screenSwapchain.width && height == screenSwapchain.height ) {
		return true;
	}
	if ( width == screenResizeFailedWidth && height == screenResizeFailedHeight ) {
		return screenSwapchain.handle != XR_NULL_HANDLE;
	}
	swapchain_t resized;
	resized.handle = XR_NULL_HANDLE;
	resized.width = resized.height = 0;
	resized.acquiredIndex = 0;
	resized.acquired = false;
	if ( !CreateSwapchain( resized, width, height ) ) {
		screenResizeFailedWidth = width;
		screenResizeFailedHeight = height;
		return recovery == RECOVERY_NONE && screenSwapchain.handle != XR_NULL_HANDLE;
	}
	ReleaseRendererImages();
	DestroySwapchain( screenSwapchain );
	screenSwapchain.handle = resized.handle;
	screenSwapchain.width = resized.width;
	screenSwapchain.height = resized.height;
	screenSwapchain.images = resized.images;
	screenSwapchain.acquiredIndex = 0;
	screenSwapchain.acquired = false;
	screenResizeFailedWidth = screenResizeFailedHeight = 0;
	if ( vr_debug.GetInteger() > 0 ) {
		common->Printf( "OpenXR: virtual screen %dx%d\n", width, height );
	}
	return true;
}

void idVRSystemOpenXR::ScreenQuadSize( float &width, float &height ) const {
	width = idMath::ClampFloat( 0.5f, 12.0f, vr_screenWidth.GetFloat() );
	height = width * static_cast<float>( screenSwapchain.height ) / static_cast<float>( Max( 1, screenSwapchain.width ) );
}

/*
====================
idVRSystemOpenXR::UpdateMenuPointer

In menus the weapon hand points at the virtual screen like a mouse. A small
dead band keeps hand tremor from re-hovering the menu every frame, which
would also take focus from stick navigation.
====================
*/
void idVRSystemOpenXR::UpdateMenuPointer( int weaponHand, bool menu ) {
	const bool wasOnScreen = pointerOnScreen;
	pointerOnScreen = false;
	// the screen re-anchors on the first menu frame: point from the next one
	if ( !menu || !screenAnchored || lastFrameHud || !aimLocalValid[weaponHand] ) {
		if ( wasOnScreen && menu ) {
			Sys_PostVRPointer( false, 0.0f, 0.0f );
		}
		return;
	}
	float width = 0.0f, height = 0.0f;
	ScreenQuadSize( width, height );
	const XrPosef &aim = aimLocalPoses[weaponHand];
	const vrVec3_t direction = VR_RotateVector( VR_FromXrQuat( aim.orientation ), VR_Vec3( 0.0f, 0.0f, -1.0f ) );
	const vrQuadHit_t hit = VR_RayQuadIntersect( VR_FromXrVec( aim.position ), direction,
		VR_FromXrVec( screenAnchor.position ), VR_FromXrQuat( screenAnchor.orientation ), width, height );
	if ( !hit.hit ) {
		if ( wasOnScreen ) {
			Sys_PostVRPointer( false, 0.0f, 0.0f );
		}
		return;
	}
	const float deadBand = 0.0015f;
	if ( wasOnScreen && idMath::Fabs( hit.u - pointerU ) < deadBand && idMath::Fabs( hit.v - pointerV ) < deadBand ) {
		pointerOnScreen = true;
		return;
	}
	if ( Sys_PostVRPointer( true, hit.u, hit.v ) ) {
		pointerOnScreen = true;
		pointerU = hit.u;
		pointerV = hit.v;
	}
}

/*
====================
idVRSystemOpenXR::ShiftTrackingOrigin

The recentre pose (a heading and a position in LOCAL space) moves by the
distance the body walked, horizontally only, and the poses already handed
out for this frame move with it, so the camera stays on the head.
====================
*/
void idVRSystemOpenXR::ShiftTrackingOrigin( const idVec3 &trackingDelta ) {
	const float unitsPerMetre = vr_worldScale.GetFloat();
	if ( !sessionRunning || unitsPerMetre <= 0.0f || !VR_IsFinite( VR_Vec3( trackingDelta.x, trackingDelta.y, trackingDelta.z ) ) ) {
		return;
	}
	// In multiplayer the local player reports every tic's movement; only a
	// room-scale walk's, or its coast to a stop, belongs to the tracking space.
	if ( idAsyncNetwork::IsActive() && ( !roomScaleStickIdle || Sys_Milliseconds() - roomScaleWalkMsec > 400 ) ) {
		return;
	}
	const idVec3 horizontal( trackingDelta.x, trackingDelta.y, 0.0f );
	const vrVec3_t xr = VR_Scale( VR_EngineToXrDirection( VR_Vec3( horizontal.x, horizontal.y, 0.0f ) ), 1.0f / unitsPerMetre );
	const vrVec3_t local = VR_RotateVector( VR_QuatNormalize( VR_FromXrQuat( recenterPose.orientation ) ), xr );
	recenterPose.position.x += local.x;
	recenterPose.position.y += local.y;
	recenterPose.position.z += local.z;
	Sys_EnterCriticalSection( VR_INPUT_CRITICAL_SECTION );
	frame.head.origin -= horizontal;
	for ( int i = 0; i < VR_NUM_EYES; i++ ) {
		frame.eyes[i].pose.origin -= horizontal;
	}
	for ( int i = 0; i < VR_NUM_HANDS; i++ ) {
		frame.grip[i].origin -= horizontal;
		frame.aim[i].origin -= horizontal;
	}
	Sys_LeaveCriticalSection( VR_INPUT_CRITICAL_SECTION );
}

/*
====================
idVRSystemOpenXR::Vibrate
====================
*/
void idVRSystemOpenXR::Vibrate( int hand, float amplitude, int durationMsec ) {
	if ( recovery != RECOVERY_NONE || !sessionRunning || !sessionFocused || !actionsAttached || hand < 0 || hand >= VR_NUM_HANDS || durationMsec <= 0 ) {
		return;
	}
	const float strength = idMath::ClampFloat( 0.0f, 1.0f, amplitude ) * idMath::ClampFloat( 0.0f, 1.0f, vr_hapticStrength.GetFloat() );
	if ( strength <= 0.0f ) {
		return;
	}
	XrHapticActionInfo info;
	memset( &info, 0, sizeof( info ) );
	info.type = XR_TYPE_HAPTIC_ACTION_INFO;
	info.action = actions[VR_ACTION_HAPTIC];
	info.subactionPath = handPaths[hand];
	XrHapticVibration vibration;
	memset( &vibration, 0, sizeof( vibration ) );
	vibration.type = XR_TYPE_HAPTIC_VIBRATION;
	vibration.duration = static_cast<XrDuration>( Min( durationMsec, 2000 ) ) * 1000000;
	vibration.frequency = XR_FREQUENCY_UNSPECIFIED;
	vibration.amplitude = strength;
	Check( xrApplyHapticFeedback( session, &info, reinterpret_cast<const XrHapticBaseHeader *>( &vibration ) ), "xrApplyHapticFeedback" );
}

/*
====================
idVRSystemOpenXR::DrawMenuPointer

A ringed dot over everything else in the 2D pass; stock menus also move their
own cursor to it.
====================
*/
void idVRSystemOpenXR::DrawMenuPointer( void ) {
	if ( !frame.active || !pointerOnScreen || screenSwapchain.width <= 0 || screenSwapchain.height <= 0 ) {
		return;
	}
	if ( pointerMaterial == NULL ) {
		pointerMaterial = declManager->FindMaterial( "_white" );
	}
	// the 640x480 canvas stretches over the screen: keep the dot square
	const float aspect = static_cast<float>( screenSwapchain.width ) / static_cast<float>( screenSwapchain.height );
	const float height = 7.0f;
	const float width = height * ( 4.0f / 3.0f ) / aspect;
	const float x = pointerU * static_cast<float>( SCREEN_WIDTH );
	const float y = pointerV * static_cast<float>( SCREEN_HEIGHT );
	renderSystem->SetColor4( 0.0f, 0.0f, 0.0f, 0.8f );
	renderSystem->DrawStretchPic( x - width, y - height, width * 2.0f, height * 2.0f, 0.0f, 0.0f, 1.0f, 1.0f, pointerMaterial );
	renderSystem->SetColor4( 1.0f, 1.0f, 1.0f, 1.0f );
	renderSystem->DrawStretchPic( x - width * 0.5f, y - height * 0.5f, width, height, 0.0f, 0.0f, 1.0f, 1.0f, pointerMaterial );
}

// The virtual screen hangs straight ahead of the head's current heading, at
// eye height, whenever the view leaves gameplay.
void idVRSystemOpenXR::AnchorScreen( void ) {
	XrSpaceLocation head;
	memset( &head, 0, sizeof( head ) );
	head.type = XR_TYPE_SPACE_LOCATION;
	XrPosef base = recenterPose;
	if ( frameDisplayTime > 0 && !CheckFrame( xrLocateSpace( viewSpace, localSpace, frameDisplayTime, &head ), "xrLocateSpace( screen )" ) ) {
		return;
	}
	if ( ( head.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT ) != 0 ) {
		base.orientation = VR_YawOnly( head.pose.orientation );
		if ( ( head.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT ) != 0 ) {
			base.position = head.pose.position;
		}
	}
	XrPosef offset = VR_IdentityPose();
	offset.position.z = -idMath::ClampFloat( 0.8f, 10.0f, vr_screenDistance.GetFloat() );
	screenAnchor = VR_PoseMultiply( base, offset );
	screenAnchored = true;
}

void idVRSystemOpenXR::BeginFrame( void ) {
	renderSystem->SetVRFrame( NULL );
	frame.active = false;
	frame.stereo = false;

	if ( RecoverIfNeeded() ) {
		return;
	}
	StartIfRequested();
	if ( RecoverIfNeeded() ) {
		return;
	}
	if ( instance != XR_NULL_HANDLE ) {
		PollEvents();
	}
	if ( RecoverIfNeeded() || session == XR_NULL_HANDLE || !sessionRunning ) {
		return;
	}
	// a changed play space takes effect between frames, recentred
	if ( vr_playSpace.IsModified() && !CreateBaseSpace() ) {
		RecoverIfNeeded();
		return;
	}
	if ( vr_standingHeight.IsModified() ) {
		vr_standingHeight.ClearModified();
		recenterRequested = true;
	}

	XrFrameWaitInfo waitInfo;
	memset( &waitInfo, 0, sizeof( waitInfo ) );
	waitInfo.type = XR_TYPE_FRAME_WAIT_INFO;
	XrFrameState frameState;
	memset( &frameState, 0, sizeof( frameState ) );
	frameState.type = XR_TYPE_FRAME_STATE;
	if ( !CheckFrame( xrWaitFrame( session, &waitInfo, &frameState ), "xrWaitFrame" ) ) {
		RecoverIfNeeded();
		return;
	}
	XrFrameBeginInfo beginInfo;
	memset( &beginInfo, 0, sizeof( beginInfo ) );
	beginInfo.type = XR_TYPE_FRAME_BEGIN_INFO;
	if ( !CheckFrame( xrBeginFrame( session, &beginInfo ), "xrBeginFrame" ) ) {
		RecoverIfNeeded();
		return;
	}
	frameOpen = true;
	frameShouldRender = frameState.shouldRender == XR_TRUE;
	frameDisplayTime = frameState.predictedDisplayTime;
	if ( frameState.predictedDisplayPeriod > 0 ) {
		frameDisplayPeriod = frameState.predictedDisplayPeriod;
	}

	const float frameSeconds = frameDelta.Sample( Sys_GetClockTicks(), Sys_ClockTicksPerSecond() );

	LocateFrame( frameDisplayTime );
	if ( RecoverIfNeeded() ) {
		return;
	}
	SyncInput( frameSeconds );
	if ( RecoverIfNeeded() ) {
		return;
	}

	frame.frameNumber++;
	frame.focused = sessionFocused;
	frame.aimMode = vr_aimMode.GetInteger();
	frame.weaponHand = vr_leftHanded.GetBool() ? VR_HAND_LEFT : VR_HAND_RIGHT;
	frame.unitsPerMetre = vr_worldScale.GetFloat();
	frame.headOffsetLimit = vr_headOffsetLimit.GetFloat();
	frame.weaponOffset.Set( vr_weaponOffsetX.GetFloat(), vr_weaponOffsetY.GetFloat(), vr_weaponOffsetZ.GetFloat() );
	frame.weaponPitch = vr_weaponPitch.GetFloat();
	frame.aimLaser = idMath::ClampInt( VR_AIM_LASER_OFF, VR_AIM_LASER_BEAM, vr_aimLaser.GetInteger() );
	frame.roomScale = vr_roomScale.GetBool();

	if ( !frameShouldRender || !ResizeScreenSwapchain() || !AcquireImages() ) {
		RecoverIfNeeded();
		return;
	}

	renderVRFrame_t renderFrame;
	memset( &renderFrame, 0, sizeof( renderFrame ) );
	renderFrame.active = true;
	renderFrame.stereo = viewsValid;
	renderFrame.eyeWidth = eyeSwapchains[0].width;
	renderFrame.eyeHeight = eyeSwapchains[0].height;
	const uint64_t screenImage = screenSwapchain.images[ static_cast<int>( screenSwapchain.acquiredIndex ) ];
	renderFrame.screenWidth = screenSwapchain.width;
	renderFrame.screenHeight = screenSwapchain.height;
	if ( graphicsApi == VR_GRAPHICS_VULKAN ) {
		for ( int i = 0; i < VR_NUM_EYES; i++ ) {
			renderFrame.vkEyeImages[i] = eyeSwapchains[i].images[ static_cast<int>( eyeSwapchains[i].acquiredIndex ) ];
		}
		renderFrame.vkScreenImage = screenImage;
		renderFrame.vkImageFormat = static_cast<int>( swapchainFormat );
	} else {
		for ( int i = 0; i < VR_NUM_EYES; i++ ) {
			renderFrame.eyeImages[i] = static_cast<unsigned int>( eyeSwapchains[i].images[ static_cast<int>( eyeSwapchains[i].acquiredIndex ) ] );
		}
		renderFrame.screenImage = static_cast<unsigned int>( screenImage );
	}
	renderFrame.mirror = idMath::ClampInt( RENDER_VR_MIRROR_NONE, RENDER_VR_MIRROR_SCREEN, vr_mirror.GetInteger() );
	renderSystem->SetVRFrame( &renderFrame );

	frame.active = true;
	frame.stereo = viewsValid;
}

void idVRSystemOpenXR::EndFrame( void ) {
	if ( RecoverIfNeeded() || !frameOpen ) {
		return;
	}
	frameOpen = false;
	ReleaseImages();
	renderSystem->SetVRFrame( NULL );
	if ( RecoverIfNeeded() ) {
		return;
	}

	renderVRFrameResult_t result;
	memset( &result, 0, sizeof( result ) );
	if ( frame.active ) {
		renderSystem->GetVRFrameResult( result );
	}

	XrCompositionLayerProjectionView projectionViews[VR_NUM_EYES];
	XrCompositionLayerProjection projection;
	XrCompositionLayerQuad quad;
	const XrCompositionLayerBaseHeader *layers[2];
	uint32_t layerCount = 0;

	const bool stereo = result.presented && result.eyeRendered[0] && result.eyeRendered[1] && viewsValid;
	if ( stereo ) {
		for ( int i = 0; i < VR_NUM_EYES; i++ ) {
			memset( &projectionViews[i], 0, sizeof( projectionViews[i] ) );
			projectionViews[i].type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
			projectionViews[i].pose = views[i].pose;
			projectionViews[i].fov = views[i].fov;
			projectionViews[i].subImage.swapchain = eyeSwapchains[i].handle;
			projectionViews[i].subImage.imageRect.offset.x = 0;
			projectionViews[i].subImage.imageRect.offset.y = 0;
			projectionViews[i].subImage.imageRect.extent.width = eyeSwapchains[i].width;
			projectionViews[i].subImage.imageRect.extent.height = eyeSwapchains[i].height;
		}
		memset( &projection, 0, sizeof( projection ) );
		projection.type = XR_TYPE_COMPOSITION_LAYER_PROJECTION;
		projection.space = localSpace;
		projection.viewCount = VR_NUM_EYES;
		projection.views = projectionViews;
		layers[layerCount++] = reinterpret_cast<const XrCompositionLayerBaseHeader *>( &projection );
	}

	if ( result.presented ) {
		// gameplay keeps the HUD in view; menus, loading and cinematics get a
		// screen that stays where it was hung
		const bool hud = stereo && !MenuMode();
		float screenWidth = 0.0f, screenHeight = 0.0f;
		ScreenQuadSize( screenWidth, screenHeight );
		memset( &quad, 0, sizeof( quad ) );
		quad.type = XR_TYPE_COMPOSITION_LAYER_QUAD;
		quad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
		quad.subImage.swapchain = screenSwapchain.handle;
		quad.subImage.imageRect.extent.width = screenSwapchain.width;
		quad.subImage.imageRect.extent.height = screenSwapchain.height;
		if ( hud ) {
			quad.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
			quad.space = viewSpace;
			quad.pose = VR_IdentityPose();
			// head aim draws the HUD crosshair, and so do the sights of a
			// vehicle whose turret follows the head: keep them on the line of sight
			quad.pose.position.y = ( vr_aimMode.GetInteger() == VR_AIM_HAND && !vr_hudOnSight.GetBool() ) ? vr_hudHeightOffset.GetFloat() : 0.0f;
			quad.pose.position.z = -idMath::ClampFloat( 0.5f, 5.0f, vr_hudDistance.GetFloat() );
			quad.size.width = idMath::ClampFloat( 0.3f, 4.0f, vr_hudWidth.GetFloat() );
			quad.size.height = quad.size.width * screenHeight / screenWidth;
		} else {
			if ( !screenAnchored || lastFrameHud ) {
				AnchorScreen();
				if ( RecoverIfNeeded() ) {
					return;
				}
			}
			quad.space = localSpace;
			quad.pose = screenAnchor;
			quad.size.width = screenWidth;
			quad.size.height = screenHeight;
		}
		layers[layerCount++] = reinterpret_cast<const XrCompositionLayerBaseHeader *>( &quad );
		lastFrameHud = hud;
	}

	XrFrameEndInfo endInfo;
	memset( &endInfo, 0, sizeof( endInfo ) );
	endInfo.type = XR_TYPE_FRAME_END_INFO;
	endInfo.displayTime = frameDisplayTime;
	endInfo.environmentBlendMode = blendMode;
	endInfo.layerCount = layerCount;
	endInfo.layers = layerCount > 0 ? layers : NULL;
	if ( !CheckFrame( xrEndFrame( session, &endInfo ), "xrEndFrame" ) ) {
		RecoverIfNeeded();
		return;
	}
	if ( vr_debug.GetInteger() > 1 ) {
		common->Printf( "OpenXR: frame %d submitted %u layers (stereo %d)\n", frame.frameNumber, layerCount, stereo ? 1 : 0 );
	}
}
