/*
===========================================================================

Doom 3 GPL Source Code
Copyright (C) 1999-2011 id Software LLC, a ZeniMax Media company.

This file is part of the Doom 3 GPL Source Code ("Doom 3 Source Code").

Doom 3 Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Doom 3 Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Doom 3 Source Code.  If not, see <http://www.gnu.org/licenses/>.

===========================================================================
*/

#if defined(OPENQ4_SDL3_LINUX_HOST) || defined(OPENQ4_SDL3_DARWIN_HOST) || defined(OPENQ4_SDL3_ANDROID_HOST)
#define OPENQ4_SDL3_POSIX_HOST 1
#endif

#include "../../framework/NativeInputDispatch.h"
#if defined(OPENQ4_SDL3_POSIX_HOST)
#include "../posix/posix_public.h"
#else
#include "../win32/win_local.h"
#endif
#include "../../framework/NativeInputPublications.h"
#include "../sys_public.h"
#include "../WindowSettings.h"
#include "../KeyEventMetadata.h"
#include "../EventQueueContinuity.h"
#include "InputDisposition.h"
#include <limits>
#include "../../framework/Common.h"
#include "../../framework/Console.h"
#include "../../framework/FileSystem.h"
#include "../../framework/licensee.h"
#include "../../framework/Session.h"
#include "../../ui/RetainedUI.h"
#include "../../ui/Rectangle.h"
#include "../../renderer/tr_local.h"
#include "../../renderer/RenderModuleAPI.h"
#include "../../renderer/RendererModule.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <cmath>
#include <cstdint>

#if defined(OPENQ4_SDL3_DARWIN_HOST)
#include "../osx/macosx_common.h"
// SDL3_SetANGLEHintDefaults runs before SDL_Init, so it cannot use
// SDL_GetBasePath to find a staged ANGLE beside the executable.
#include <limits.h>
#include <mach-o/dyld.h>
#include <string.h>
#endif

#if defined(OPENQ4_SDL3_POSIX_HOST)
struct PosixSDL3Compat_t {
	void *hWnd;
	void *hDC;
	void *hGLRC;
	bool activeApp;
	bool mouseReleased;
	bool movingWindow;
	bool mouseGrabbed;
	int desktopBitsPixel;
	int desktopWidth;
	int desktopHeight;
	bool cdsFullscreen;
	void (*glimpRenderThread)(void);
	void *smpData;
	int wglErrors;
	idCVar in_mouse;
	idCVar win_xpos;
	idCVar win_ypos;

	PosixSDL3Compat_t()
		: hWnd(NULL)
		, hDC(NULL)
		, hGLRC(NULL)
		, activeApp(true)
		, mouseReleased(false)
		, movingWindow(false)
		, mouseGrabbed(false)
		, desktopBitsPixel(32)
		, desktopWidth(1920)
		, desktopHeight(1080)
		, cdsFullscreen(false)
		, glimpRenderThread(NULL)
		, smpData(NULL)
		, wglErrors(0)
		, in_mouse("in_mouse", "1", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_BOOL, "enable mouse input")
		, win_xpos("win_xpos", "50", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_INTEGER, "horizontal position of window")
		, win_ypos("win_ypos", "50", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_INTEGER, "vertical position of window") {
	}
};

static PosixSDL3Compat_t win32;

static inline void Sys_QueEventCompat(int time, sysEventType_t type, int value, int value2, int ptrLength, void *ptr) {
	(void)time;
	Posix_QueEvent(type, value, value2, ptrLength, ptr);
}

#define Sys_QueEvent Sys_QueEventCompat

static inline bool Sys_HandlePrintScreenHotkey(bool pressed) {
	(void)pressed;
	return false;
}
#endif

#if !defined(OPENQ4_SDL3_POSIX_HOST)
// WGL_ARB_extensions_string
PFNWGLGETEXTENSIONSSTRINGARBPROC wglGetExtensionsStringARB;

// WGL_EXT_swap_interval
PFNWGLSWAPINTERVALEXTPROC wglSwapIntervalEXT;

// WGL_ARB_pixel_format
PFNWGLGETPIXELFORMATATTRIBIVARBPROC wglGetPixelFormatAttribivARB;
PFNWGLGETPIXELFORMATATTRIBFVARBPROC wglGetPixelFormatAttribfvARB;
PFNWGLCHOOSEPIXELFORMATARBPROC wglChoosePixelFormatARB;

// WGL_ARB_pbuffer
PFNWGLCREATEPBUFFERARBPROC wglCreatePbufferARB;
PFNWGLGETPBUFFERDCARBPROC wglGetPbufferDCARB;
PFNWGLRELEASEPBUFFERDCARBPROC wglReleasePbufferDCARB;
PFNWGLDESTROYPBUFFERARBPROC wglDestroyPbufferARB;
PFNWGLQUERYPBUFFERARBPROC wglQueryPbufferARB;

// WGL_ARB_render_texture
PFNWGLBINDTEXIMAGEARBPROC wglBindTexImageARB;
PFNWGLRELEASETEXIMAGEARBPROC wglReleaseTexImageARB;
PFNWGLSETPBUFFERATTRIBARBPROC wglSetPbufferAttribARB;
#endif

static SDL_Window *s_sdlWindow = NULL;
// True only while the game renderer holds its own SDL_INIT_VIDEO reference.
// Splash and system-console windows hold separate references in posix_syscon.
static bool s_sdlVideoReferenceHeld = false;
static bool s_sdlTextInputActive = false;
static bool s_sdlTextInputFailureLogged = false;
static bool s_sdlUnsupportedTextInputLogged = false;
static bool s_sdlFocusInputReleased = false;
static bool s_sdlGamepadSubsystemActive = false;
static bool s_sdlJoystickSubsystemActive = false;
static SDL_Gamepad *s_sdlGamepad = NULL;
static SDL_Joystick *s_sdlJoystick = NULL;
static SDL_JoystickID s_sdlGamepadId = 0;
static SDL_JoystickID s_sdlJoystickId = 0;
static bool s_sdlDiagnosticCommandsRegistered = false;
static bool s_sdlDisplaySummaryLogged = false;
static bool s_sdlVideoDriverSummaryLogged = false;
static bool s_sdlGraphicsBridgeSummaryLogged = false;
static bool s_sdlAppMetadataAttempted = false;
static bool s_sdlLifecycleEventWatchRegistered = false;
static SDL_AtomicInt s_sdlLifecyclePending = { 0 };
static float s_sdlMouseWheelRemainderY = 0.0f;
static float s_sdlRelativeMouseRemainderX = 0.0f;
static float s_sdlRelativeMouseRemainderY = 0.0f;

static idCVar in_joystick("in_joystick", "1", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_BOOL, "enable joystick/gamepad input");
static idCVar in_joystickDeadZone("in_joystickDeadZone", "0.18", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_FLOAT, "joystick axis dead zone", 0.0f, 0.95f);
static idCVar in_joystickTriggerThreshold("in_joystickTriggerThreshold", "0.35", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_FLOAT, "trigger button press threshold", 0.0f, 1.0f);
static idCVar in_joystickLookSensitivity("in_joystickLookSensitivity", "0.75", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_FLOAT, "controller look sensitivity scale", 0.1f, 4.0f);
static idCVar in_joystickLookCurve("in_joystickLookCurve", "1.35", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_FLOAT, "controller look response curve", 1.0f, 3.0f);
static idCVar in_joystickMoveCurve("in_joystickMoveCurve", "1.0", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_FLOAT, "controller movement response curve", 1.0f, 3.0f);
static idCVar in_joystickInvertLook("in_joystickInvertLook", "0", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_BOOL, "invert controller look pitch");
static idCVar in_joystickSouthpaw("in_joystickSouthpaw", "0", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_BOOL, "swap controller movement and look sticks");
static idCVar in_joystickUseDedicatedLookAxes("in_joystickUseDedicatedLookAxes", "-1", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_INTEGER, "raw joystick look axes mode (-1 = auto, 0 = off, 1 = on)", -1, 1);
static idCVar in_joystickMoveAxisX("in_joystickMoveAxisX", "-1", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_INTEGER, "raw joystick horizontal movement axis (-1 = auto)", -1, 31);
static idCVar in_joystickMoveAxisY("in_joystickMoveAxisY", "-1", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_INTEGER, "raw joystick vertical movement axis (-1 = auto)", -1, 31);
static idCVar in_joystickLookAxisX("in_joystickLookAxisX", "-1", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_INTEGER, "raw joystick horizontal look axis (-1 = auto)", -1, 31);
static idCVar in_joystickLookAxisY("in_joystickLookAxisY", "-1", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_INTEGER, "raw joystick vertical look axis (-1 = auto)", -1, 31);
static idCVar in_joystickUpAxis("in_joystickUpAxis", "-1", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_INTEGER, "raw joystick positive vertical/throttle axis (-1 = auto)", -1, 31);
static idCVar in_joystickUpAxisNegative("in_joystickUpAxisNegative", "-1", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_INTEGER, "raw joystick negative vertical/throttle axis (-1 = auto)", -1, 31);
static idCVar in_joystickRumble("in_joystickRumble", "1", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_BOOL, "enable controller rumble/haptic feedback");
static idCVar in_joystickRumbleScale("in_joystickRumbleScale", "1.0", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_FLOAT, "controller rumble strength scale", 0.0f, 2.0f);
static idCVar in_joystickLowBatteryRumbleThreshold("in_joystickLowBatteryRumbleThreshold", "0", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_INTEGER, "controller battery percent where rumble output is capped (0 = disabled)", 0, 100);
static idCVar in_joystickLowBatteryRumbleScale("in_joystickLowBatteryRumbleScale", "0.75", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_FLOAT, "maximum effective controller rumble scale below the low-battery threshold", 0.0f, 2.0f);
static idCVar in_gyro("in_gyro", "0", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_BOOL, "enable SDL gamepad gyro as mouse-look input");
static idCVar in_gyroSensitivity("in_gyroSensitivity", "1.0", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_FLOAT, "SDL gamepad gyro mouse-look sensitivity", 0.0f, 8.0f);
static idCVar in_gyroDeadZone("in_gyroDeadZone", "0.015", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_FLOAT, "ignore SDL gamepad gyro rates below this radians-per-second threshold", 0.0f, 2.0f);
static idCVar in_gyroYawAxis("in_gyroYawAxis", "2", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_INTEGER, "SDL gamepad gyro data index used for yaw", 0, 2);
static idCVar in_gyroPitchAxis("in_gyroPitchAxis", "0", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_INTEGER, "SDL gamepad gyro data index used for pitch", 0, 2);
static idCVar in_gyroInvertYaw("in_gyroInvertYaw", "0", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_BOOL, "invert SDL gamepad gyro yaw");
static idCVar in_gyroInvertPitch("in_gyroInvertPitch", "0", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_BOOL, "invert SDL gamepad gyro pitch");
static idCVar in_touchpadMode("in_touchpadMode", "0", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_INTEGER, "SDL gamepad touchpad mode: 0=off, 1=menu cursor, 2=mouse-look, 3=button-only", 0, 3);
static idCVar in_touchpadSensitivity("in_touchpadSensitivity", "1.0", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_FLOAT, "SDL gamepad touchpad motion sensitivity", 0.0f, 8.0f);
static idCVar in_touchscreen("in_touchscreen", "1", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_BOOL, "route SDL touchscreen events to menu and loading UI mouse input");
static idCVar com_steamDeckAutoFrameCap("com_steamDeckAutoFrameCap", "1", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_BOOL, "apply a Steam Deck frame cap when com_maxfps is still at the global default");
static idCVar com_steamDeckFrameCap("com_steamDeckFrameCap", "0", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_INTEGER, "Steam Deck frame cap override (0 = detected display refresh, clamped for Deck)", 0, 1000);
static idCVar r_screen("r_screen", "-1", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_INTEGER, "SDL3 display index to target (-1 = auto/current display)");
static idCVar r_multiScreen("r_multiScreen", "0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_INTEGER, "multi-screen mode (0 = single display, 1 = span all displays)", 0, 1);

static const unsigned char s_scantokey[128] = {
	0,          27,    '1',       '2',        '3',    '4',         '5',      '6',
	'7',        '8',    '9',       '0',        '-',    '=',          K_BACKSPACE, 9,
	'q',        'w',    'e',       'r',        't',    'y',         'u',      'i',
	'o',        'p',    '[',       ']',        K_ENTER,K_CTRL,      'a',      's',
	'd',        'f',    'g',       'h',        'j',    'k',         'l',      ';',
	'\'',       '`',    K_SHIFT,   '\\',       'z',    'x',         'c',      'v',
	'b',        'n',    'm',       ',',        '.',    '/',         K_SHIFT,  K_KP_STAR,
	K_ALT,      ' ',    K_CAPSLOCK,K_F1,       K_F2,   K_F3,        K_F4,     K_F5,
	K_F6,       K_F7,   K_F8,      K_F9,       K_F10,  K_PAUSE,     K_SCROLL, K_HOME,
	K_UPARROW,  K_PGUP, K_KP_MINUS,K_LEFTARROW,K_KP_5, K_RIGHTARROW,K_KP_PLUS,K_END,
	K_DOWNARROW,K_PGDN, K_INS,     K_DEL,      0,      0,           0,        K_F11,
	K_F12,      0,      0,         K_LWIN,     K_RWIN, K_MENU,      0,        0,
	0,          0,      0,         0,          0,      0,           0,        0,
	0,          0,      0,         0,          0,      0,           0,        0,
	0,          0,      0,         0,          0,      0,           0,        0,
	0,          0,      0,         0,          0,      0,           0,        0
};

static const unsigned char s_scantoshift[128] = {
	0,           27,    '!',       '@',        '#',    '$',         '%',      '^',
	'&',        '*',    '(',       ')',        '_',    '+',         K_BACKSPACE, 9,
	'Q',        'W',    'E',       'R',        'T',    'Y',         'U',      'I',
	'O',        'P',    '{',       '}',        K_ENTER,K_CTRL,      'A',      'S',
	'D',        'F',    'G',       'H',        'J',    'K',         'L',      ':',
	'|',        '~',    K_SHIFT,   '\\',       'Z',    'X',         'C',      'V',
	'B',        'N',    'M',       '<',        '>',    '?',         K_SHIFT,  K_KP_STAR,
	K_ALT,      ' ',    K_CAPSLOCK,K_F1,       K_F2,   K_F3,        K_F4,     K_F5,
	K_F6,       K_F7,   K_F8,      K_F9,       K_F10,  K_PAUSE,     K_SCROLL, K_HOME,
	K_UPARROW,  K_PGUP, K_KP_MINUS,K_LEFTARROW,K_KP_5, K_RIGHTARROW,K_KP_PLUS,K_END,
	K_DOWNARROW,K_PGDN, K_INS,     K_DEL,      0,      0,           0,        K_F11,
	K_F12,      0,      0,         K_LWIN,     K_RWIN, K_MENU,      0,        0,
	0,          0,      0,         0,          0,      0,           0,        0,
	0,          0,      0,         0,          0,      0,           0,        0,
	0,          0,      0,         0,          0,      0,           0,        0,
	0,          0,      0,         0,          0,      0,           0,        0
};

static unsigned char s_rightAltKey = K_ALT;

typedef struct {
	int		key;
	bool	down;
	int		time;
} sdlKeyboardEvent_t;

typedef struct {
	int		action;
	int		value;
	int		time;
} sdlMouseEvent_t;

typedef struct {
	int		axis;
	int		value;
} sdlJoystickAxisEvent_t;

static const int SDL3_INPUT_QUEUE_SIZE = 512;
static const int SDL3_INPUT_QUEUE_MASK = SDL3_INPUT_QUEUE_SIZE - 1;
static const int SDL3_MAX_MOUSE_DELTA_PER_EVENT = 32767;
static const int SDL3_MAX_MOUSE_WHEEL_STEPS_PER_EVENT = 64;

static_assert((SDL3_INPUT_QUEUE_SIZE & SDL3_INPUT_QUEUE_MASK) == 0, "input queue size must be power-of-two");

static sdlKeyboardEvent_t s_keyboardQueue[SDL3_INPUT_QUEUE_SIZE];
static sdlMouseEvent_t s_mouseQueue[SDL3_INPUT_QUEUE_SIZE];
static int s_keyboardHead = 0;
static int s_keyboardTail = 0;
static int s_mouseHead = 0;
static int s_mouseTail = 0;

static sdlKeyboardEvent_t s_polledKeyboard[SDL3_INPUT_QUEUE_SIZE];
static sdlMouseEvent_t s_polledMouse[SDL3_INPUT_QUEUE_SIZE];
static int s_polledKeyboardCount = 0;
static int s_polledMouseCount = 0;

// Protected by CRITICAL_SECTION_ONE, including checked polled-slice ownership.
static sysKeyboardDisposition_t s_keyboardDisposition[SDL3_INPUT_QUEUE_SIZE];
static sysEventDispositionTag_t s_mouseDisposition[SDL3_INPUT_QUEUE_SIZE];
static sysKeyboardDisposition_t s_polledKeyboardDisposition[SDL3_INPUT_QUEUE_SIZE];
static sysEventDispositionTag_t s_polledMouseDisposition[SDL3_INPUT_QUEUE_SIZE];
struct sdlInputDispositionState_t {
    sysInputDispositionSlice_t slice;
    unsigned next = 0;
    bool checked = false;
};
static sdlInputDispositionState_t s_keyboardDispositionState, s_mouseDispositionState;
static std::uint64_t s_inputDispositionHighwater = 0;
// Ring admission and checked slice serials share one never-reset namespace.
static std::uint64_t s_keyboardRetirementSerials[SDL3_INPUT_QUEUE_SIZE];
static std::uint64_t s_mouseRetirementSerials[SDL3_INPUT_QUEUE_SIZE];
class SDL3_InputStorageLock {
public:
    SDL3_InputStorageLock() { Sys_EnterCriticalSection(CRITICAL_SECTION_ONE); }
    ~SDL3_InputStorageLock() { Sys_LeaveCriticalSection(CRITICAL_SECTION_ONE); }
    SDL3_InputStorageLock(const SDL3_InputStorageLock&) = delete;
    SDL3_InputStorageLock& operator=(const SDL3_InputStorageLock&) = delete;
};
static sdlJoystickAxisEvent_t s_polledJoystick[MAX_JOYSTICK_AXIS];
static int s_polledJoystickCount = 0;

static int s_joystickAxisState[MAX_JOYSTICK_AXIS] = { 0 };
#if defined(OPENQ4_SIGMATOUCH)
static int s_touchAxisState[MAX_JOYSTICK_AXIS] = { 0 };
static int SDL3_ReadJoystickAxis(int axis) {
	if (axis == AXIS_ROLL && (s_touchAxisState[AXIS_SIDE] != 0 ||
		s_touchAxisState[AXIS_FORWARD] != 0 || s_touchAxisState[AXIS_YAW] != 0 ||
		s_touchAxisState[AXIS_PITCH] != 0)) {
		return 127;
	}
	if (axis != AXIS_ROLL && s_touchAxisState[axis] != 0) {
		return s_touchAxisState[axis];
	}
	return s_joystickAxisState[axis];
}
#else
static int SDL3_ReadJoystickAxis(int axis) { return s_joystickAxisState[axis]; }
#endif
static bool s_gamepadButtonsDown[SDL_GAMEPAD_BUTTON_COUNT] = { false };
static bool s_gamepadLeftTriggerDown = false;
static bool s_gamepadRightTriggerDown = false;
static bool s_joystickRumbleActive = false;
static Uint16 s_joystickRumbleLow = 0;
static Uint16 s_joystickRumbleHigh = 0;
static int s_joystickRumbleUntilTime = 0;
static int s_joystickRumbleLastUpdateTime = 0;
static bool s_lowBatteryRumbleScaleActive = false;
static int s_lowBatteryRumblePercent = -1;
static const int SDL3_RUMBLE_DEBOUNCE_MSEC = 40;
static const int SDL3_MAX_JOYSTICK_BUTTONS = 48;
static bool s_joystickButtonsDown[SDL3_MAX_JOYSTICK_BUTTONS] = { false };
static Uint8 s_joystickHatState = SDL_HAT_CENTERED;
static bool s_gamepadGyroEnabled = false;
static Uint64 s_gamepadGyroLastTimestamp = 0;
static float s_gamepadGyroRemainderX = 0.0f;
static float s_gamepadGyroRemainderY = 0.0f;
static bool s_gamepadTouchpadFingerActive = false;
static int s_gamepadTouchpadIndex = -1;
static int s_gamepadTouchpadFinger = -1;
static float s_gamepadTouchpadLastX = 0.0f;
static float s_gamepadTouchpadLastY = 0.0f;
static float s_gamepadTouchpadRemainderX = 0.0f;
static float s_gamepadTouchpadRemainderY = 0.0f;
static bool s_touchscreenFingerActive = false;
static SDL_FingerID s_touchscreenFingerId = 0;
static bool s_sdlAppInBackground = false;

// This engine links one SDL module for its process lifetime; it never unloads
// and replaces that module. Its identity is not regenerated by video shutdown.
// Window and exclusive registration identities have independent saturating
// counters. An old claim is retained across window loss until explicit release.
static openq4::NativeWindowPublication s_nativeInputWindow, s_nativeInputClaim;
static std::uint64_t s_nativeWindowHighwater=0, s_nativeClaimHighwater=0;
static bool s_nativeWindowLive=false, s_nativeWindowExhausted=false;

static void SDL3_RetireNativeWindowPublication() noexcept {
    openq4::NativeInputBeforeWindowChange();
    s_nativeWindowLive=false;
}
static void SDL3_PublishNativeWindowIdentity() noexcept {
#if defined(_WIN32)
    const auto handle=reinterpret_cast<std::uintptr_t>(win32.hWnd);
    const auto window=s_sdlWindow ? static_cast<std::uint64_t>(SDL_GetWindowID(s_sdlWindow)) : 0;
    if(!handle || !window){SDL3_RetireNativeWindowPublication();return;}
    if(!s_nativeWindowLive || s_nativeInputWindow.handle!=handle || s_nativeInputWindow.window!=window){
        openq4::NativeInputBeforeWindowChange();
        if(s_nativeWindowHighwater==UINT64_MAX){s_nativeWindowExhausted=true;s_nativeWindowLive=false;return;}
        s_nativeInputWindow={};s_nativeInputWindow.handle=handle;s_nativeInputWindow.window=window;
        s_nativeInputWindow.lifetime=++s_nativeWindowHighwater;s_nativeInputWindow.module=1;
    }
    s_nativeWindowLive=!s_nativeWindowExhausted;
#endif
}
static bool SDL3_SameNativeWindowClaim(const openq4::NativeWindowPublication& a,const openq4::NativeWindowPublication& b) noexcept {
    return a.handle==b.handle && a.window==b.window && a.lifetime==b.lifetime && a.module==b.module &&
        a.association==b.association && a.registration==b.registration;
}
bool openq4::Sys_QueryNativeInputWindow(NativeWindowPublication& out) noexcept {
    if(!Sys_EventDispositionBoundThread())return false;
    auto candidate=s_nativeInputWindow;
    candidate.windowAllowed=!s_nativeWindowExhausted && s_nativeWindowLive && s_sdlWindow && s_sdlVideoReferenceHeld &&
        win32.activeApp && !s_sdlAppInBackground;
    candidate.providerOwned=s_nativeInputClaim.registration!=0;
    candidate.module=s_nativeInputClaim.module ? s_nativeInputClaim.module : s_nativeInputWindow.module;
    candidate.association=s_nativeInputClaim.association;candidate.registration=s_nativeInputClaim.registration;
    out=candidate;return true;
}
bool openq4::Sys_AcquireNativeInputWindowClaim(NativeWindowPublication& out) noexcept {
#if defined(_WIN32) && defined(OPENQ4_SDL3_CHECKED_NATIVE_QUEUE) && OPENQ4_SDL3_CHECKED_NATIVE_QUEUE
    NativeWindowPublication current;
    if(!NativeInputPublicationSlotEmpty() || s_nativeInputClaim.registration || !Sys_EventDispositionEpoch() ||
        !Sys_QueryNativeInputWindow(current) || !current.windowAllowed || !current.handle || !current.window ||
        !current.lifetime || !current.module || s_nativeClaimHighwater>UINT64_MAX-2)return false;
    NativeInputBeforeWindowChange();
    current.association=++s_nativeClaimHighwater;current.registration=++s_nativeClaimHighwater;current.providerOwned=true;
    s_nativeInputClaim=current;out=current;return true;
#else
    (void)out;return false;
#endif
}
bool openq4::Sys_ReleaseNativeInputWindowClaim(const NativeWindowPublication& expected) noexcept {
    if(!Sys_EventDispositionBoundThread() || !s_nativeInputClaim.registration ||
        !SDL3_SameNativeWindowClaim(expected,s_nativeInputClaim))return false;
    NativeInputBeforeWindowChange();s_nativeInputClaim={};return true;
}

static bool s_haveAbsoluteMousePosition = false;
static int s_absoluteMouseX = 0;
static int s_absoluteMouseY = 0;
static bool s_menuMouseRouteActive = false;
static idUserInterface *s_trackedMenuGui = NULL;
static bool s_trackedConsoleActive = false;
static bool s_haveMenuMousePosition = false;
static float s_menuMouseX = 0.0f;
static float s_menuMouseY = 0.0f;
static float s_menuMouseRemainderX = 0.0f;
static float s_menuMouseRemainderY = 0.0f;
static bool s_ignoreNextMenuWarpMotion = false;
static float s_menuWarpWindowX = 0.0f;
static float s_menuWarpWindowY = 0.0f;
static bool s_menuMouseInsideWindow = true;
static bool s_windowAspectSnapActive = false;
static float s_windowAspectSnapRatio = 0.0f;
static bool s_screenParmTransitionActive = false;
static bool s_preserveWindowOnShutdown = false;
static bool s_waylandSpanWarningLogged = false;

static const int SDL3_LIFECYCLE_PENDING_BACKGROUND = 1 << 0;
static const int SDL3_LIFECYCLE_PENDING_FOREGROUND = 1 << 1;
static const int SDL3_LIFECYCLE_PENDING_LOW_MEMORY = 1 << 2;

typedef enum {
	SDL3_VIDEO_DRIVER_UNKNOWN = 0,
	SDL3_VIDEO_DRIVER_WAYLAND,
	SDL3_VIDEO_DRIVER_X11,
	SDL3_VIDEO_DRIVER_WINDOWS,
	SDL3_VIDEO_DRIVER_COCOA
} sdl3VideoDriver_t;

static sdl3VideoDriver_t s_sdlVideoDriver = SDL3_VIDEO_DRIVER_UNKNOWN;
static char s_sdlVideoDriverName[32] = "unknown";

typedef struct {
	int x;
	int y;
	int width;
	int height;
	bool valid;
} sdl3WindowedPlacement_t;

static sdl3WindowedPlacement_t s_windowedPlacement = { 0, 0, 0, 0, false };
#ifndef ID_DEDICATED
static uint64_t s_windowPlacementLease = 0;
static sysWindowPlacementSnapshot_t s_windowPlacementBaseline;
#endif

void* GLimp_ExtensionPointer(const char* name);

bool QGL_Init(const char *dllname);
void QGL_Shutdown(void);


static int SDL3_EventMilliseconds(Uint64 timestampNs) {
	static const Uint64 SDL3_MAX_EVENT_MS = 0x7fffffffULL;
	if (timestampNs == 0) {
		return Sys_Milliseconds();
	}
	Uint64 timeMs = SDL_NS_TO_MS(timestampNs);
	if (timeMs > SDL3_MAX_EVENT_MS) {
		return static_cast<int>(SDL3_MAX_EVENT_MS);
	}
	return static_cast<int>(timeMs);
}

static void SDL3_ClearInputQueues(void) {
	Sys_EnterCriticalSection(CRITICAL_SECTION_ONE);
	Sys_InvalidateEventQueue(); // Invalidate before clearing even an empty slice.
	for (int i = 0; i < SDL3_INPUT_QUEUE_SIZE; ++i) {
		s_keyboardQueue[i] = {}; s_mouseQueue[i] = {};
		s_polledKeyboard[i] = {}; s_polledMouse[i] = {};
		s_keyboardDisposition[i] = {}; s_mouseDisposition[i] = {};
		s_polledKeyboardDisposition[i] = {}; s_polledMouseDisposition[i] = {};
        s_keyboardRetirementSerials[i] = 0; s_mouseRetirementSerials[i] = 0;
	}
	s_keyboardDispositionState = {}; s_mouseDispositionState = {};
	// s_inputDispositionHighwater deliberately survives clear/shutdown/re-init.
	s_keyboardHead = s_keyboardTail = 0;
	s_mouseHead = s_mouseTail = 0;
	s_polledKeyboardCount = 0;
	s_polledMouseCount = 0;
	s_polledJoystickCount = 0;
	Sys_LeaveCriticalSection(CRITICAL_SECTION_ONE);
	s_haveAbsoluteMousePosition = false;
	s_haveMenuMousePosition = false;
	s_menuMouseX = 0.0f;
	s_menuMouseY = 0.0f;
	s_menuMouseRemainderX = 0.0f;
	s_menuMouseRemainderY = 0.0f;
	s_sdlRelativeMouseRemainderX = 0.0f;
	s_sdlRelativeMouseRemainderY = 0.0f;
	s_gamepadGyroLastTimestamp = 0;
	s_gamepadGyroRemainderX = 0.0f;
	s_gamepadGyroRemainderY = 0.0f;
	s_gamepadTouchpadFingerActive = false;
	s_gamepadTouchpadIndex = -1;
	s_gamepadTouchpadFinger = -1;
	s_gamepadTouchpadLastX = 0.0f;
	s_gamepadTouchpadLastY = 0.0f;
	s_gamepadTouchpadRemainderX = 0.0f;
	s_gamepadTouchpadRemainderY = 0.0f;
	s_touchscreenFingerActive = false;
	s_touchscreenFingerId = 0;
	s_ignoreNextMenuWarpMotion = false;
	s_menuWarpWindowX = 0.0f;
	s_menuWarpWindowY = 0.0f;
}

static bool SDL3_IsMousePollActionValid(int action) {
	return action >= M_ACTION1 && action <= M_DELTAZ;
}

static bool SDL3_ShouldQueueMousePoll(int action, int value) {
	if (!SDL3_IsMousePollActionValid(action)) {
		return false;
	}
	if ((action == M_DELTAX || action == M_DELTAY || action == M_DELTAZ) && value == 0) {
		return false;
	}
	return true;
}

#if defined(OPENQ4_SDL3_LINUX_HOST)
static const char *SDL3_EnvString(const char *name) {
	const char *value = getenv(name);
	return (value != NULL && value[0] != '\0') ? value : "<unset>";
}
#endif

static const char *SDL3_HintString(const char *name) {
	const char *value = SDL_GetHint(name);
	return (value != NULL && value[0] != '\0') ? value : "<unset>";
}

// Also needed on Darwin: SDL3_SetANGLEHintDefaults uses it to leave an ANGLE
// path the user set in the environment alone.
#if defined(OPENQ4_SDL3_LINUX_HOST) || defined(OPENQ4_SDL3_DARWIN_HOST)
static bool SDL3_EnvHasValue(const char *name) {
	const char *value = getenv(name);
	return value != NULL && value[0] != '\0';
}
#endif

#if defined(OPENQ4_SDL3_LINUX_HOST)
static bool SDL3_EnvFlagEnabled(const char *name) {
	const char *value = getenv(name);
	return value != NULL && value[0] != '\0' && idStr::Icmp(value, "0") != 0 && idStr::Icmp(value, "false") != 0;
}
#endif

static bool SDL3_StringEquals(const char *a, const char *b) {
	return a != NULL && b != NULL && idStr::Icmp(a, b) == 0;
}

#if defined(OPENQ4_SDL3_DARWIN_HOST)
static void SDL3_SetHintDefaultLogged(const char *name, const char *value, const char *context) {
	if (name == NULL || name[0] == '\0' || value == NULL) {
		return;
	}
	if (!SDL_SetHintWithPriority(name, value, SDL_HINT_DEFAULT)) {
		// Sys_Printf instead of common->Printf: the hint defaults are also
		// applied before engine init by the early startup splash/console path.
		Sys_Printf("SDL3: failed to set %s hint %s=%s\n", context != NULL ? context : "default", name, value);
	}
}
#endif

static bool SDL3_IsMacOSMetalBridge(void) {
#if defined(OPENQ4_SDL3_DARWIN_HOST) && defined(OPENQ4_MACOS_METAL_BRIDGE)
	return true;
#else
	return false;
#endif
}

static const char *SDL3_GraphicsBridgeDescription(void) {
	return SDL3_IsMacOSMetalBridge()
		? "macOS Metal bridge (SDL3/Cocoa host, OpenGL renderer compatibility path)"
		: "OpenGL";
}

#if defined(OPENQ4_SDL3_DARWIN_HOST)
/*
===============
SDL3_SetANGLEHintDefaults

macOS ships no OpenGL ES, so SDL's cocoa EGL path is only usable with a
translation layer (ANGLE, GLES-on-Metal). SDL dlopens the bare leaf names
"libEGL.dylib" and "libGLESv2.dylib", which dyld does not resolve from the
executable's own directory, so a staged ANGLE would be invisible without an
absolute path. Look bundle-relative first, then beside the executable -- the
same order the Vulkan module uses to find MoltenVK.

Applied only when the selected renderer is the GLES module. SDL_HINT_OPENGL_LIBRARY
is consulted when creating *any* GL context, not just an ES one, so setting it
process-wide on macOS points desktop context creation at ANGLE as well: every
core-profile candidate then fails with "Failed getting OpenGL context version"
and the ladder silently falls back to 2.1 compatibility. That fallback still
reaches ARB2, so a desktop smoke test stays green while core 4.1 is quietly
unreachable -- gate on the API rather than trusting the comment that nothing
loads until an ES context is requested.

An explicit environment override always wins.

The executable directory comes from _NSGetExecutablePath, not SDL_GetBasePath:
this runs from Sys_SDL_ApplyVideoHintDefaults *before* SDL_Init, and
SDL_GetBasePath returns NULL until the video subsystem is up. Anchoring on the
uninitialised SDL call made the whole search bail on its first line, which is
why a staged ANGLE was invisible without SDL_EGL_LIBRARY set by hand.
_NSGetExecutablePath has no such ordering dependency and is what the engine's
own macOS Sys_EXEPath is built on.
===============
*/
/*
===============
SDL3_LoadedAngleImagePath

Returns the path of an already-loaded dylib with this leaf name, or NULL.

The renderer module links libGLESv2 and is loaded before the window exists, so
by the time the ES context is created ANGLE is already in the process -- and the
module resolved it through its own rpath list, which meson populates from the
staged ANGLE directory ahead of @loader_path. Handing SDL a path we merely
*guessed* (the executable directory) pointed it at a second, byte-identical copy
at a different path. dyld keys images by resolved path, so that is two ANGLE
images: SDL creates the context in one while the module's linked entry points
call into the other, which then reports no current context and returns NULL for
GL_VERSION.

Asking dyld what is actually loaded removes the guess.
===============
*/
static const char *SDL3_LoadedAngleImagePath(const char *leafName) {
	const uint32_t imageCount = _dyld_image_count();
	for (uint32_t i = 0; i < imageCount; ++i) {
		const char *imageName = _dyld_get_image_name(i);
		if (imageName == NULL) {
			continue;
		}
		const char *slash = strrchr(imageName, '/');
		const char *leaf = (slash != NULL) ? slash + 1 : imageName;
		if (idStr::Icmp(leaf, leafName) == 0) {
			return imageName;
		}
	}
	return NULL;
}

static const char *SDL3_DarwinExecutableDir(void) {
	static char exeDir[PATH_MAX];

	if (exeDir[0] != '\0') {
		return exeDir;
	}

	uint32_t bufferSize = (uint32_t)sizeof(exeDir);
	if (_NSGetExecutablePath(exeDir, &bufferSize) != 0) {
		exeDir[0] = '\0';
		return exeDir;
	}

	// Trim to the directory, keeping the trailing separator so callers can
	// concatenate a leaf name directly.
	char *lastSlash = strrchr(exeDir, '/');
	if (lastSlash == NULL) {
		exeDir[0] = '\0';
		return exeDir;
	}
	lastSlash[1] = '\0';
	return exeDir;
}

static void SDL3_SetANGLEHintDefaults(void) {
	static const struct {
		const char *hint;
		const char *leafName;
	} angleLibraries[] = {
		{ SDL_HINT_EGL_LIBRARY, "libEGL.dylib" },
		{ SDL_HINT_OPENGL_LIBRARY, "libGLESv2.dylib" },
	};

	// Sys_SDL_ApplyVideoHintDefaults is also called from the early splash/console
	// path, before the renderer module is booted, where the API is not yet known.
	// SDL3_WindowServices_PrepareWindowSystem calls it again before the game
	// window is created, which is the call that matters for the GL context.
	const rendererModuleStatus_t &moduleStatus = R_RendererModule_GetStatus();
	if (moduleStatus.activeApi != RENDER_MODULE_API_GLES &&
			moduleStatus.requestedApi != RENDER_MODULE_API_GLES) {
		return;
	}

	const char *basePath = SDL3_DarwinExecutableDir();
	if (basePath == NULL || basePath[0] == '\0') {
		return;
	}

	// Whatever ANGLE the renderer module already loaded wins: matching its exact
	// path is what keeps this to a single driver instance.
	const char *loadedGLES = SDL3_LoadedAngleImagePath("libGLESv2.dylib");
	char loadedAngleDir[1024] = { 0 };
	if (loadedGLES != NULL) {
		idStr::snPrintf(loadedAngleDir, sizeof(loadedAngleDir), "%s", loadedGLES);
		char *lastSlash = strrchr(loadedAngleDir, '/');
		if (lastSlash != NULL) {
			lastSlash[1] = '\0';
		} else {
			loadedAngleDir[0] = '\0';
		}
	}

	for (size_t i = 0; i < sizeof(angleLibraries) / sizeof(angleLibraries[0]); ++i) {
		if (SDL3_EnvHasValue(angleLibraries[i].hint)) {
			continue;
		}

		if (loadedAngleDir[0] != '\0') {
			char loadedCandidate[1024];
			SDL_PathInfo loadedInfo;
			idStr::snPrintf(loadedCandidate, sizeof(loadedCandidate), "%s%s", loadedAngleDir, angleLibraries[i].leafName);
			if (SDL_GetPathInfo(loadedCandidate, &loadedInfo)) {
				SDL3_SetHintDefaultLogged(angleLibraries[i].hint, loadedCandidate, "loaded ANGLE (GLES on Metal)");
				continue;
			}
		}

		const char *searchDirs[] = { "../Frameworks/", "" };
		for (size_t d = 0; d < sizeof(searchDirs) / sizeof(searchDirs[0]); ++d) {
			char candidate[1024];
			SDL_PathInfo info;

			idStr::snPrintf(candidate, sizeof(candidate), "%s%s%s",
				basePath, searchDirs[d], angleLibraries[i].leafName);
			if (!SDL_GetPathInfo(candidate, &info)) {
				continue;
			}

			SDL3_SetHintDefaultLogged(angleLibraries[i].hint, candidate, "staged ANGLE (GLES on Metal)");
			break;
		}
	}
}
#endif

static void SDL3_SetVideoHintDefaults(void) {
	// openQ4 consumes committed UTF-8 text but does not render composition or
	// candidate lists itself. Ask SDL to keep the platform-native IME UI so
	// IBus/Fcitx (and the native services on other SDL hosts) remain usable.
	(void)SDL_SetHintWithPriority(SDL_HINT_IME_IMPLEMENTED_UI, "none", SDL_HINT_DEFAULT);
#if defined(OPENQ4_SDL3_LINUX_HOST)
	const bool disableWaylandLibdecor = SDL3_EnvFlagEnabled("OPENQ4_WAYLAND_DISABLE_LIBDECOR");
	if (SDL3_EnvFlagEnabled("OPENQ4_FORCE_X11") &&
			!SDL3_EnvHasValue("SDL_VIDEO_DRIVER") &&
			!SDL3_EnvHasValue("SDL_VIDEODRIVER")) {
		(void)SDL_SetHintWithPriority(SDL_HINT_VIDEO_DRIVER, "x11", SDL_HINT_DEFAULT);
	}
	if (!disableWaylandLibdecor && SDL3_EnvFlagEnabled("OPENQ4_WAYLAND_PREFER_LIBDECOR")) {
		(void)SDL_SetHintWithPriority(SDL_HINT_VIDEO_WAYLAND_PREFER_LIBDECOR, "1", SDL_HINT_DEFAULT);
	}
	if (SDL3_EnvFlagEnabled("OPENQ4_WAYLAND_SYNC_WINDOW_OPS")) {
		(void)SDL_SetHintWithPriority(SDL_HINT_VIDEO_SYNC_WINDOW_OPERATIONS, "1", SDL_HINT_DEFAULT);
	}
	(void)SDL_SetHintWithPriority(SDL_HINT_VIDEO_WAYLAND_ALLOW_LIBDECOR, disableWaylandLibdecor ? "0" : "1", SDL_HINT_DEFAULT);
	(void)SDL_SetHintWithPriority(SDL_HINT_VIDEO_WAYLAND_MODE_EMULATION, "1", SDL_HINT_DEFAULT);
	(void)SDL_SetHintWithPriority(SDL_HINT_VIDEO_WAYLAND_MODE_SCALING, "aspect", SDL_HINT_DEFAULT);
	(void)SDL_SetHintWithPriority(SDL_HINT_VIDEO_WAYLAND_SCALE_TO_DISPLAY, "0", SDL_HINT_DEFAULT);
#endif
#if defined(OPENQ4_SDL3_DARWIN_HOST)
	if (SDL3_IsMacOSMetalBridge()) {
		SDL3_SetHintDefaultLogged(SDL_HINT_VIDEO_DRIVER, "cocoa", "macOS Metal bridge");
		// Metal-first with graceful fallback so NULL-name SDL_CreateRenderer
		// calls can never hard-fail when the Metal driver is unavailable.
		SDL3_SetHintDefaultLogged(SDL_HINT_RENDER_DRIVER, "metal,gpu,software", "macOS Metal bridge");
		SDL3_SetHintDefaultLogged(SDL_HINT_GPU_DRIVER, "metal", "macOS Metal bridge");
		SDL3_SetHintDefaultLogged(SDL_HINT_VIDEO_METAL_AUTO_RESIZE_DRAWABLE, "1", "macOS Metal bridge");
	}
	SDL3_SetANGLEHintDefaults();
#endif
}

static void SDL3_SetAppMetadataDefaults(void) {
	if (s_sdlAppMetadataAttempted) {
		return;
	}
	s_sdlAppMetadataAttempted = true;

	// Keep the Wayland app_id aligned with the installed openq4.desktop file so
	// compositors can group the window and select the packaged icon correctly.
	if (!SDL_SetAppMetadata(GAME_NAME, PROJECT_VERSION, "openq4")) {
		Sys_Printf("SDL3: failed to set application metadata: %s\n", SDL_GetError());
	}
}

/*
===============
Sys_SDL_ApplyVideoHintDefaults

Called by the early startup splash/console path before its
SDL_InitSubSystem(SDL_INIT_VIDEO)/SDL_CreateRenderer calls so application
metadata and platform hint defaults (including the macOS Metal bridge
render-driver selection) are in effect for the first SDL video use of the
process, not just for GLimp_Init. Safe to call repeatedly; the hints are set
at default priority and metadata is attempted once.
===============
*/
void Sys_SDL_ApplyVideoHintDefaults(void) {
	SDL3_SetAppMetadataDefaults();
	SDL3_SetVideoHintDefaults();
}

static void SDL3_UpdateVideoDriverProfile(void) {
	const char *driverName = SDL_GetCurrentVideoDriver();
	if (driverName == NULL || driverName[0] == '\0') {
		driverName = "unknown";
	}

	idStr::snPrintf(s_sdlVideoDriverName, sizeof(s_sdlVideoDriverName), "%s", driverName);

	if (SDL3_StringEquals(driverName, "wayland")) {
		s_sdlVideoDriver = SDL3_VIDEO_DRIVER_WAYLAND;
	} else if (SDL3_StringEquals(driverName, "x11")) {
		s_sdlVideoDriver = SDL3_VIDEO_DRIVER_X11;
	} else if (SDL3_StringEquals(driverName, "windows")) {
		s_sdlVideoDriver = SDL3_VIDEO_DRIVER_WINDOWS;
	} else if (SDL3_StringEquals(driverName, "cocoa")) {
		s_sdlVideoDriver = SDL3_VIDEO_DRIVER_COCOA;
	} else {
		s_sdlVideoDriver = SDL3_VIDEO_DRIVER_UNKNOWN;
	}
}

static bool SDL3_IsNativeWaylandVideoDriver(void) {
	return s_sdlVideoDriver == SDL3_VIDEO_DRIVER_WAYLAND;
}

bool Sys_SDL_IsGameWindowFocused(void) {
	if (s_sdlAppInBackground || !s_sdlVideoReferenceHeld || s_sdlWindow == NULL) {
		return false;
	}

	const SDL_WindowFlags flags = SDL_GetWindowFlags(s_sdlWindow);
	return (flags & SDL_WINDOW_INPUT_FOCUS) != 0 &&
		(flags & (SDL_WINDOW_HIDDEN | SDL_WINDOW_MINIMIZED)) == 0;
}

static bool SDL3_UseAbsoluteWindowPlacement(void) {
	return !SDL3_IsNativeWaylandVideoDriver();
}

static void SDL3_PrintVideoDriverSummary(void) {
	if (s_sdlVideoDriverSummaryLogged) {
		return;
	}

	common->Printf("SDL3: current video driver: %s\n", s_sdlVideoDriverName);

	const int driverCount = SDL_GetNumVideoDrivers();
	common->Printf("SDL3: available video drivers:");
	for (int i = 0; i < driverCount; ++i) {
		const char *driverName = SDL_GetVideoDriver(i);
		common->Printf(" %s", (driverName != NULL && driverName[0] != '\0') ? driverName : "<unknown>");
	}
	common->Printf("\n");

#if defined(OPENQ4_SDL3_LINUX_HOST)
	common->Printf(
		"SDL3: Linux video environment: OPENQ4_FORCE_X11=%s OPENQ4_WAYLAND_DISABLE_LIBDECOR=%s OPENQ4_WAYLAND_PREFER_LIBDECOR=%s OPENQ4_WAYLAND_SYNC_WINDOW_OPS=%s SDL_VIDEO_DRIVER=%s SDL_VIDEODRIVER=%s WAYLAND_DISPLAY=%s DISPLAY=%s\n",
		SDL3_EnvString("OPENQ4_FORCE_X11"),
		SDL3_EnvString("OPENQ4_WAYLAND_DISABLE_LIBDECOR"),
		SDL3_EnvString("OPENQ4_WAYLAND_PREFER_LIBDECOR"),
		SDL3_EnvString("OPENQ4_WAYLAND_SYNC_WINDOW_OPS"),
		SDL3_EnvString("SDL_VIDEO_DRIVER"),
		SDL3_EnvString("SDL_VIDEODRIVER"),
		SDL3_EnvString("WAYLAND_DISPLAY"),
		SDL3_EnvString("DISPLAY"));

	if (SDL3_IsNativeWaylandVideoDriver()) {
		common->Printf(
			"SDL3: native Wayland active; compositor-controlled window placement and Wayland-first OpenGL fallback ordering enabled.\n");
		common->Printf(
			"SDL3: Wayland hints: ALLOW_LIBDECOR=%s PREFER_LIBDECOR=%s MODE_EMULATION=%s MODE_SCALING=%s SCALE_TO_DISPLAY=%s SYNC_WINDOW_OPERATIONS=%s\n",
			SDL3_HintString(SDL_HINT_VIDEO_WAYLAND_ALLOW_LIBDECOR),
			SDL3_HintString(SDL_HINT_VIDEO_WAYLAND_PREFER_LIBDECOR),
			SDL3_HintString(SDL_HINT_VIDEO_WAYLAND_MODE_EMULATION),
			SDL3_HintString(SDL_HINT_VIDEO_WAYLAND_MODE_SCALING),
			SDL3_HintString(SDL_HINT_VIDEO_WAYLAND_SCALE_TO_DISPLAY),
			SDL3_HintString(SDL_HINT_VIDEO_SYNC_WINDOW_OPERATIONS));
	} else if (s_sdlVideoDriver == SDL3_VIDEO_DRIVER_X11 && getenv("WAYLAND_DISPLAY") != NULL) {
		common->Printf("SDL3: X11 video driver active in a Wayland session; running through XWayland.\n");
	}
#endif

	s_sdlVideoDriverSummaryLogged = true;
}

static void SDL3_PrintGraphicsBridgeSummary(void) {
	if (s_sdlGraphicsBridgeSummaryLogged) {
		return;
	}

	common->Printf("SDL3: graphics bridge: %s\n", SDL3_GraphicsBridgeDescription());

#if defined(OPENQ4_SDL3_DARWIN_HOST)
	if (SDL3_IsMacOSMetalBridge()) {
		common->Printf(
			"SDL3: macOS Metal bridge keeps rendering on the existing OpenGL compatibility path; no native Metal renderer rewrite is selected.\n");
		common->Printf(
			"SDL3: macOS Metal bridge hints: SDL_VIDEO_DRIVER=%s SDL_RENDER_DRIVER=%s SDL_GPU_DRIVER=%s SDL_VIDEO_METAL_AUTO_RESIZE_DRAWABLE=%s\n",
			SDL3_HintString(SDL_HINT_VIDEO_DRIVER),
			SDL3_HintString(SDL_HINT_RENDER_DRIVER),
			SDL3_HintString(SDL_HINT_GPU_DRIVER),
			SDL3_HintString(SDL_HINT_VIDEO_METAL_AUTO_RESIZE_DRAWABLE));
	}
#endif

	s_sdlGraphicsBridgeSummaryLogged = true;
}

static void SDL3_QueueKeyboardInput(int key, bool down, int time) {
	if (key <= 0 || key >= K_LAST_KEY) {
		return;
	}
	Sys_EnterCriticalSection(CRITICAL_SECTION_ONE);
	const int next = (s_keyboardHead + 1) & SDL3_INPUT_QUEUE_MASK;
	if (next == s_keyboardTail) {
		Sys_InvalidateEventQueue(); // Legacy eviction is observable before loss.
		s_keyboardDisposition[s_keyboardTail] = {};
        s_keyboardRetirementSerials[s_keyboardTail] = 0;
		s_keyboardTail = (s_keyboardTail + 1) & SDL3_INPUT_QUEUE_MASK;
	}
	s_keyboardDisposition[s_keyboardHead] = {};
    s_keyboardRetirementSerials[s_keyboardHead] = 0;
	s_keyboardQueue[s_keyboardHead].key = key;
	s_keyboardQueue[s_keyboardHead].down = down;
	s_keyboardQueue[s_keyboardHead].time = time;
	s_keyboardHead = next;
	Sys_LeaveCriticalSection(CRITICAL_SECTION_ONE);
}

static void SDL3_QueueMouseInput(int action, int value, int time) {
	if (!SDL3_ShouldQueueMousePoll(action, value)) {
		return;
	}
	Sys_EnterCriticalSection(CRITICAL_SECTION_ONE);
	const int next = (s_mouseHead + 1) & SDL3_INPUT_QUEUE_MASK;
	if (next == s_mouseTail) {
		Sys_InvalidateEventQueue(); // Legacy eviction is observable before loss.
		s_mouseDisposition[s_mouseTail] = {};
        s_mouseRetirementSerials[s_mouseTail] = 0;
		s_mouseTail = (s_mouseTail + 1) & SDL3_INPUT_QUEUE_MASK;
	}
	s_mouseDisposition[s_mouseHead] = {};
    s_mouseRetirementSerials[s_mouseHead] = 0;
	s_mouseQueue[s_mouseHead].action = action;
	s_mouseQueue[s_mouseHead].value = value;
	s_mouseQueue[s_mouseHead].time = time;
	s_mouseHead = next;
	Sys_LeaveCriticalSection(CRITICAL_SECTION_ONE);
}

#ifndef ID_DEDICATED
void RetainedUI_QueueInput(const retainedUIInput_t& event, int time) {
	if (!RetainedUI_IsOpen()) return;
	void* payload = Mem_Alloc(sizeof(event)); memcpy(payload,&event,sizeof(event));
	Sys_QueEvent(time,SE_RETAINED_UI,static_cast<int>(RetainedUI_InputGeneration()),0,sizeof(event),payload);
}
#endif
static bool s_retainedKeyOwners[K_LAST_KEY] = {};
static bool SDL3_QueueRetainedKey(int key, bool down, bool repeated, int source, int time) {
	if (key <= 0 || key >= K_LAST_KEY) return false;
	const bool previouslyOwned = s_retainedKeyOwners[key];
	if (!down) s_retainedKeyOwners[key] = false;
	const bool reserved = key == Sys_GetConsoleKey(false) || key == Sys_GetConsoleKey(true);
	if (RetainedUI_IsOpen() && !(console && console->Active()) && !reserved) {
		s_retainedKeyOwners[key] = down;
		retainedUIInput_t event;
		event.kind = retainedUIInput_t::KEY; event.key = key; event.source = source;
		event.down = down; event.repeated = repeated;
		RetainedUI_QueueInput(event,time);
		return true;
	}
	// A repeat belonging to the old menu must not run a gameplay binding.
	return previouslyOwned && down;
}
static void SDL3_QueueRetainedPointer(float x, float y, int time) {
	retainedUIInput_t event; event.kind = retainedUIInput_t::POINTER; event.x = x; event.y = y;
	RetainedUI_QueueInput(event,time);
}
static void SDL3_QueueRetainedFocus(bool focused, int time) {
	retainedUIInput_t event; event.kind = retainedUIInput_t::FOCUS; event.down = focused;
	RetainedUI_QueueInput(event,time);
}
static void SDL3_QueueRetainedCancel(int time) {
	retainedUIInput_t event; event.kind = retainedUIInput_t::CANCEL;
	RetainedUI_QueueInput(event,time);
}
static bool SDL3_ShouldRouteMenuMouse(void) {
	// Disabled input and hidden batch-render windows must never synchronize or
	// warp the host cursor when a GUI becomes active.
	if (!s_sdlWindow || !win32.in_mouse.GetBool() || (SDL_GetWindowFlags(s_sdlWindow) & SDL_WINDOW_HIDDEN)) {
		return false;
	}
	return ( session != NULL && session->IsGUIActive() ) || ( console != NULL && console->Active() );
}

static idUserInterface *SDL3_GetActiveMenuGui(void) {
	if (RetainedUI_IsOpen()) return NULL;
	return ( session != NULL ) ? session->GetActiveGUI() : NULL;
}

static bool SDL3_IsMouseCaptured(void) {
	if (!s_sdlWindow) {
		return false;
	}
	return SDL_GetWindowRelativeMouseMode(s_sdlWindow) || SDL_GetWindowMouseGrab(s_sdlWindow);
}

static void SDL3_SetMouseHintDefaults(void) {
	(void)SDL_SetHintWithPriority(SDL_HINT_MOUSE_RELATIVE_MODE_CENTER, "1", SDL_HINT_DEFAULT);
	(void)SDL_SetHintWithPriority(SDL_HINT_MOUSE_RELATIVE_SYSTEM_SCALE, "0", SDL_HINT_DEFAULT);
	(void)SDL_SetHintWithPriority(SDL_HINT_MOUSE_RELATIVE_WARP_MOTION, "0", SDL_HINT_DEFAULT);
	(void)SDL_SetHintWithPriority(SDL_HINT_MOUSE_TOUCH_EVENTS, "0", SDL_HINT_DEFAULT);
	(void)SDL_SetHintWithPriority(SDL_HINT_TOUCH_MOUSE_EVENTS, "0", SDL_HINT_DEFAULT);
}

static void SDL3_SetControllerHintDefaults(void) {
#if defined(OPENQ4_SDL3_POSIX_HOST)
	(void)SDL_SetHintWithPriority(SDL_HINT_JOYSTICK_HIDAPI, "1", SDL_HINT_DEFAULT);
	(void)SDL_SetHintWithPriority(SDL_HINT_JOYSTICK_ENHANCED_REPORTS, "auto", SDL_HINT_DEFAULT);
	(void)SDL_SetHintWithPriority(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "0", SDL_HINT_DEFAULT);
	(void)SDL_SetHintWithPriority(SDL_HINT_JOYSTICK_HIDAPI_PS4, "1", SDL_HINT_DEFAULT);
	(void)SDL_SetHintWithPriority(SDL_HINT_JOYSTICK_HIDAPI_PS5, "1", SDL_HINT_DEFAULT);
	(void)SDL_SetHintWithPriority(SDL_HINT_JOYSTICK_HIDAPI_STEAM, "1", SDL_HINT_DEFAULT);
	(void)SDL_SetHintWithPriority(SDL_HINT_JOYSTICK_HIDAPI_SWITCH, "1", SDL_HINT_DEFAULT);
	(void)SDL_SetHintWithPriority(SDL_HINT_JOYSTICK_HIDAPI_SWITCH2, "1", SDL_HINT_DEFAULT);
	(void)SDL_SetHintWithPriority(SDL_HINT_JOYSTICK_HIDAPI_XBOX, "1", SDL_HINT_DEFAULT);
#if defined(OPENQ4_SDL3_LINUX_HOST)
	(void)SDL_SetHintWithPriority(SDL_HINT_HIDAPI_UDEV, "1", SDL_HINT_DEFAULT);
	(void)SDL_SetHintWithPriority(SDL_HINT_JOYSTICK_LINUX_CLASSIC, "0", SDL_HINT_DEFAULT);
	(void)SDL_SetHintWithPriority(SDL_HINT_JOYSTICK_LINUX_DEADZONES, "0", SDL_HINT_DEFAULT);
	(void)SDL_SetHintWithPriority(SDL_HINT_JOYSTICK_HIDAPI_STEAMDECK, "1", SDL_HINT_DEFAULT);
#endif
#if defined(OPENQ4_SDL3_DARWIN_HOST)
	(void)SDL_SetHintWithPriority(SDL_HINT_JOYSTICK_IOKIT, "1", SDL_HINT_DEFAULT);
	(void)SDL_SetHintWithPriority(SDL_HINT_JOYSTICK_MFI, "1", SDL_HINT_DEFAULT);
#endif
#endif
}

static void SDL3_ResetMenuMouseTracking(void) {
	s_haveMenuMousePosition = false;
	s_menuMouseX = 0.0f;
	s_menuMouseY = 0.0f;
	s_menuMouseRemainderX = 0.0f;
	s_menuMouseRemainderY = 0.0f;
	s_ignoreNextMenuWarpMotion = false;
	s_menuWarpWindowX = 0.0f;
	s_menuWarpWindowY = 0.0f;
}

static void SDL3_SetMenuMouseTrackingPosition(float cursorX, float cursorY) {
	s_haveMenuMousePosition = true;
	s_menuMouseX = cursorX;
	s_menuMouseY = cursorY;
	s_menuMouseRemainderX = 0.0f;
	s_menuMouseRemainderY = 0.0f;
	s_haveAbsoluteMousePosition = false;
}

static void SDL3_InvalidateMenuMouseRouting(void) {
	SDL3_ResetMenuMouseTracking();
	s_menuMouseRouteActive = false;
}

static void SDL3_UpdateCursorVisibility(void) {
	if (!s_sdlWindow) {
		return;
	}

	if (SDL3_IsMouseCaptured()) {
		(void)SDL_HideCursor();
		return;
	}

	if (SDL3_ShouldRouteMenuMouse() && win32.activeApp && (win32.cdsFullscreen || s_menuMouseInsideWindow) &&
		!(RetainedUI_IsOpen() && !(console && console->Active()))) {
		(void)SDL_HideCursor();
		return;
	}

	(void)SDL_ShowCursor();
}

static int SDL3_ConsumeMouseDelta(float delta, float &remainder) {
	if (!std::isfinite(delta) || !std::isfinite(remainder)) {
		remainder = 0.0f;
		return 0;
	}
	const float accumulated = delta + remainder;
	if (!std::isfinite(accumulated)) {
		remainder = 0.0f;
		return 0;
	}
	const float clampedAccumulated = idMath::ClampFloat(
		-static_cast<float>(SDL3_MAX_MOUSE_DELTA_PER_EVENT),
		static_cast<float>(SDL3_MAX_MOUSE_DELTA_PER_EVENT),
		accumulated);
	const int whole = static_cast<int>(clampedAccumulated);
	remainder = clampedAccumulated - static_cast<float>(whole);
	return whole;
}

typedef struct {
	float guiWidth;
	float guiHeight;
	float pixelWidth;
	float pixelHeight;
	float drawAreaX;
	float drawAreaY;
	float drawAreaWidth;
	float drawAreaHeight;
	float windowToPixelX;
	float windowToPixelY;
	float pixelToWindowX;
	float pixelToWindowY;
	float xScale;
	float yScale;
	float xOffset;
	float yOffset;
} sdl3GuiMouseTransform_t;

static bool SDL3_BuildGuiMouseTransform(sdl3GuiMouseTransform_t &transform) {
	if (!s_sdlWindow) {
		return false;
	}

	int windowWidth = 0;
	int windowHeight = 0;
	int pixelWidth = 0;
	int pixelHeight = 0;

	if (!SDL_GetWindowSize(s_sdlWindow, &windowWidth, &windowHeight) || windowWidth <= 0 || windowHeight <= 0) {
		return false;
	}

	if (!SDL_GetWindowSizeInPixels(s_sdlWindow, &pixelWidth, &pixelHeight) || pixelWidth <= 0 || pixelHeight <= 0) {
		pixelWidth = windowWidth;
		pixelHeight = windowHeight;
	}

	transform.guiWidth = static_cast<float>(SCREEN_WIDTH);
	transform.guiHeight = static_cast<float>(SCREEN_HEIGHT);
	transform.pixelWidth = static_cast<float>(pixelWidth);
	transform.pixelHeight = static_cast<float>(pixelHeight);
	transform.windowToPixelX = static_cast<float>(pixelWidth) / static_cast<float>(windowWidth);
	transform.windowToPixelY = static_cast<float>(pixelHeight) / static_cast<float>(windowHeight);
	transform.pixelToWindowX = static_cast<float>(windowWidth) / static_cast<float>(pixelWidth);
	transform.pixelToWindowY = static_cast<float>(windowHeight) / static_cast<float>(pixelHeight);

	// Match the fullscreen-2D viewport region (selected monitor on multi-monitor spans).
	transform.drawAreaX = static_cast<float>(engineWindowState.uiViewportX);
	transform.drawAreaY = static_cast<float>(engineWindowState.uiViewportY);
	transform.drawAreaWidth = static_cast<float>(engineWindowState.uiViewportWidth);
	transform.drawAreaHeight = static_cast<float>(engineWindowState.uiViewportHeight);

	if (transform.drawAreaWidth <= 0.0f || transform.drawAreaHeight <= 0.0f) {
		transform.drawAreaX = 0.0f;
		transform.drawAreaY = 0.0f;
		transform.drawAreaWidth = transform.pixelWidth;
		transform.drawAreaHeight = transform.pixelHeight;
	} else {
		transform.drawAreaX = idMath::ClampFloat(0.0f, transform.pixelWidth, transform.drawAreaX);
		transform.drawAreaY = idMath::ClampFloat(0.0f, transform.pixelHeight, transform.drawAreaY);
		if (transform.drawAreaX + transform.drawAreaWidth > transform.pixelWidth) {
			transform.drawAreaWidth = transform.pixelWidth - transform.drawAreaX;
		}
		if (transform.drawAreaY + transform.drawAreaHeight > transform.pixelHeight) {
			transform.drawAreaHeight = transform.pixelHeight - transform.drawAreaY;
		}
		if (transform.drawAreaWidth <= 0.0f || transform.drawAreaHeight <= 0.0f) {
			transform.drawAreaX = 0.0f;
			transform.drawAreaY = 0.0f;
			transform.drawAreaWidth = transform.pixelWidth;
			transform.drawAreaHeight = transform.pixelHeight;
		}
	}

	const bool preserveAspect = cvarSystem->GetCVarBool("ui_aspectCorrection");
	if (preserveAspect) {
		const float scaleX = transform.drawAreaWidth / transform.guiWidth;
		const float scaleY = transform.drawAreaHeight / transform.guiHeight;
		const float uniformPhysicalScale = (scaleX < scaleY) ? scaleX : scaleY;
		const float drawWidth = transform.guiWidth * uniformPhysicalScale;
		const float drawHeight = transform.guiHeight * uniformPhysicalScale;
		const float virtualPerPhysicalX = transform.guiWidth / transform.drawAreaWidth;
		const float virtualPerPhysicalY = transform.guiHeight / transform.drawAreaHeight;
		transform.xScale = uniformPhysicalScale * virtualPerPhysicalX;
		transform.yScale = uniformPhysicalScale * virtualPerPhysicalY;
		transform.xOffset = (transform.drawAreaWidth - drawWidth) * 0.5f * virtualPerPhysicalX;
		transform.yOffset = (transform.drawAreaHeight - drawHeight) * 0.5f * virtualPerPhysicalY;
	} else {
		transform.xScale = 1.0f;
		transform.yScale = 1.0f;
		transform.xOffset = 0.0f;
		transform.yOffset = 0.0f;
	}

	if (transform.xScale <= 0.0f || transform.yScale <= 0.0f) {
		return false;
	}

	return true;
}

static bool SDL3_GetActiveGuiTextInputState(idRectangle &area, float &cursorOffset) {
	idUserInterface *activeGui = SDL3_GetActiveMenuGui();
	return activeGui != NULL && activeGui->GetTextInputState(area, cursorOffset);
}

static void SDL3_UpdateTextInputArea(void) {
	if (!s_sdlWindow || !s_sdlTextInputActive) {
		return;
	}

	int windowWidth = 0;
	int windowHeight = 0;
	if (!SDL_GetWindowSize(s_sdlWindow, &windowWidth, &windowHeight) ||
			windowWidth <= 0 || windowHeight <= 0) {
		return;
	}

	// Give the native IME a stable command-line area for the console. Focused
	// GUI edit controls expose their exact field rectangle and visible caret
	// offset below. Coordinates passed to SDL are logical window units, not
	// high-DPI drawable pixels.
	SDL_Rect textArea;
	int cursorOffset = 0;
	if (console != NULL && console->Active()) {
		const int margin = idMath::ClampInt(0, windowWidth / 4, 8);
		const int height = idMath::ClampInt(1, windowHeight, 32);
		textArea.x = margin;
		textArea.y = idMath::ClampInt(0, windowHeight - 1, windowHeight - height);
		textArea.w = idMath::ClampInt(1, windowWidth, windowWidth - (margin * 2));
		textArea.h = height;
		cursorOffset = idMath::ClampInt(0, textArea.w, 16);
	} else {
		idRectangle guiArea;
		float guiCursorOffset = 0.0f;
		sdl3GuiMouseTransform_t transform;
		if (!SDL3_GetActiveGuiTextInputState(guiArea, guiCursorOffset) ||
				!SDL3_BuildGuiMouseTransform(transform)) {
			return;
		}

		const float guiToPixelX = transform.xScale * (transform.drawAreaWidth / transform.guiWidth);
		const float guiToPixelY = transform.yScale * (transform.drawAreaHeight / transform.guiHeight);
		const float pixelX = transform.drawAreaX +
			((guiArea.x * transform.xScale) + transform.xOffset) * (transform.drawAreaWidth / transform.guiWidth);
		const float pixelY = transform.drawAreaY +
			((guiArea.y * transform.yScale) + transform.yOffset) * (transform.drawAreaHeight / transform.guiHeight);
		const int anchorX = idMath::ClampInt(0, windowWidth - 1, static_cast<int>(pixelX * transform.pixelToWindowX));
		const int anchorY = idMath::ClampInt(0, windowHeight - 1, static_cast<int>(pixelY * transform.pixelToWindowY));
		const int requestedWidth = static_cast<int>(guiArea.w * guiToPixelX * transform.pixelToWindowX);
		const int requestedHeight = static_cast<int>(guiArea.h * guiToPixelY * transform.pixelToWindowY);
		textArea.x = anchorX;
		textArea.y = anchorY;
		textArea.w = idMath::ClampInt(1, windowWidth - anchorX, requestedWidth);
		textArea.h = idMath::ClampInt(1, windowHeight - anchorY, requestedHeight);
		cursorOffset = idMath::ClampInt(
			0,
			textArea.w,
			static_cast<int>(guiCursorOffset * guiToPixelX * transform.pixelToWindowX));
	}

	if (!SDL_SetTextInputArea(s_sdlWindow, &textArea, cursorOffset)) {
		common->DPrintf("SDL3: text input candidate area could not be updated: %s\n", SDL_GetError());
	}
}

static void SDL3_UpdateTextInputState(void) {
	if (!s_sdlWindow) {
		s_sdlTextInputActive = false;
		return;
	}

	idRectangle guiTextArea;
	float guiCursorOffset = 0.0f;
	const bool consoleAcceptsText = console != NULL && console->Active();
	const bool guiAcceptsText = SDL3_GetActiveGuiTextInputState(guiTextArea, guiCursorOffset);
	const bool shouldAcceptText = win32.activeApp && (consoleAcceptsText || guiAcceptsText);
	if (shouldAcceptText && !s_sdlTextInputActive) {
		if (SDL_StartTextInput(s_sdlWindow)) {
			s_sdlTextInputActive = true;
			s_sdlTextInputFailureLogged = false;
		} else if (!s_sdlTextInputFailureLogged) {
			common->Printf("SDL3: text input could not be enabled: %s\n", SDL_GetError());
			s_sdlTextInputFailureLogged = true;
		}
	} else if (!shouldAcceptText && s_sdlTextInputActive) {
		(void)SDL_ClearComposition(s_sdlWindow);
		(void)SDL_StopTextInput(s_sdlWindow);
		s_sdlTextInputActive = false;
		s_sdlTextInputFailureLogged = false;
	}

	SDL3_UpdateTextInputArea();
}

static bool SDL3_MapWindowMouseToGuiCursor(float windowMouseX, float windowMouseY, float &cursorX, float &cursorY) {
	sdl3GuiMouseTransform_t transform;
	if (!SDL3_BuildGuiMouseTransform(transform)) {
		return false;
	}

	float pixelMouseX = windowMouseX * transform.windowToPixelX - transform.drawAreaX;
	float pixelMouseY = windowMouseY * transform.windowToPixelY - transform.drawAreaY;
	const float drawX = pixelMouseX * (transform.guiWidth / transform.drawAreaWidth);
	const float drawY = pixelMouseY * (transform.guiHeight / transform.drawAreaHeight);

	cursorX = (drawX - transform.xOffset) / transform.xScale;
	cursorY = (drawY - transform.yOffset) / transform.yScale;
	return true;
}

static bool SDL3_MapWindowMouseToConsoleCursor(float windowMouseX, float windowMouseY, float &cursorX, float &cursorY) {
	sdl3GuiMouseTransform_t transform;
	if (!SDL3_BuildGuiMouseTransform(transform)) {
		return false;
	}

	float pixelMouseX = windowMouseX * transform.windowToPixelX - transform.drawAreaX;
	float pixelMouseY = windowMouseY * transform.windowToPixelY - transform.drawAreaY;

	cursorX = pixelMouseX * (static_cast<float>(SCREEN_WIDTH) / transform.drawAreaWidth);
	cursorY = pixelMouseY * (static_cast<float>(SCREEN_HEIGHT) / transform.drawAreaHeight);

	return true;
}

static bool SDL3_MapWindowMouseToRoutedCursor(float windowMouseX, float windowMouseY, float &cursorX, float &cursorY) {
	if (console != NULL && console->Active()) {
		return SDL3_MapWindowMouseToConsoleCursor(windowMouseX, windowMouseY, cursorX, cursorY);
	}

	idUserInterface *activeGui = SDL3_GetActiveMenuGui();
	if (activeGui != NULL) {
		return SDL3_MapWindowMouseToGuiCursor(windowMouseX, windowMouseY, cursorX, cursorY);
	}

	return false;
}

static bool SDL3_UpdateRoutedMouseDelta(float menuMouseX, float menuMouseY, int &dx, int &dy) {
	float previousX = 0.0f;
	float previousY = 0.0f;

	if (s_haveMenuMousePosition) {
		previousX = s_menuMouseX;
		previousY = s_menuMouseY;
	} else {
		idUserInterface *activeGui = SDL3_GetActiveMenuGui();
		if (activeGui == NULL) {
			SDL3_SetMenuMouseTrackingPosition(menuMouseX, menuMouseY);
			return true;
		}
		previousX = activeGui->CursorX();
		previousY = activeGui->CursorY();
	}

	dx = SDL3_ConsumeMouseDelta(menuMouseX - previousX, s_menuMouseRemainderX);
	dy = SDL3_ConsumeMouseDelta(menuMouseY - previousY, s_menuMouseRemainderY);
	s_menuMouseX = menuMouseX;
	s_menuMouseY = menuMouseY;
	s_haveMenuMousePosition = true;
	s_haveAbsoluteMousePosition = false;
	return true;
}

static void SDL3_QueueMouseDelta(int dx, int dy, int eventTime) {
	if (dx == 0 && dy == 0) {
		return;
	}

	Sys_QueEvent(eventTime, SE_MOUSE, dx, dy, 0, NULL);
	if (dx != 0) {
		SDL3_QueueMouseInput(M_DELTAX, dx, eventTime);
	}
	if (dy != 0) {
		SDL3_QueueMouseInput(M_DELTAY, dy, eventTime);
	}
}

static void SDL3_QueueControllerActivity(int eventTime, int value) {
	// SE_JOYSTICK_AXIS is consumed by the framework's active-input tracker.  SDL
	// keeps gameplay axes on the polled path, so this lightweight event carries
	// only post-deadzone device activity and cannot alter a usercmd.
	if (idMath::Abs(value) >= 16) {
		Sys_QueEvent(eventTime, SE_JOYSTICK_AXIS, AXIS_SIDE, idMath::ClampChar(value), 0, NULL);
	}
}

static void SDL3_QueueMouseButtonEvent(int key, bool down, int eventTime, bool pollState) {
	if (key == 0) {
		return;
	}

	if (!SDL3_QueueRetainedKey(key,down,false,4096+key,eventTime))
		Sys_QueEvent(eventTime, SE_KEY, key, down ? 1 : 0, 0, NULL);
	if (pollState) {
		SDL3_QueueMouseInput(M_ACTION1 + (key - K_MOUSE1), down ? 1 : 0, eventTime);
	}
}

static bool SDL3_SetRoutedCursorFromWindowPosition(float windowMouseX, float windowMouseY, int &dx, int &dy) {
	if (RetainedUI_IsOpen() && !(console && console->Active())) {
		if (!std::isfinite(windowMouseX) || !std::isfinite(windowMouseY)) return false;
		dx = dy = 0;
		SDL3_QueueRetainedPointer(windowMouseX,windowMouseY,Sys_Milliseconds());
		return true;
	}
	float cursorX = 0.0f;
	float cursorY = 0.0f;
	if (!SDL3_MapWindowMouseToRoutedCursor(windowMouseX, windowMouseY, cursorX, cursorY)) {
		SDL3_ResetMenuMouseTracking();
		return false;
	}

	if (console != NULL && console->Active()) {
		console->SetMousePosition(cursorX, cursorY);
	} else {
		idUserInterface *activeGui = SDL3_GetActiveMenuGui();
		if (activeGui != NULL) {
			activeGui->SetCursor(cursorX, cursorY);
		}
	}

	s_menuMouseInsideWindow = true;
	return SDL3_UpdateRoutedMouseDelta(cursorX, cursorY, dx, dy);
}

static void SDL3_SyncSystemMouseToActiveCursor(void) {
	if (!SDL3_ShouldRouteMenuMouse() || !win32.activeApp) {
		return;
	}
	// Retained controls consume the absolute coordinates in queued SDL motion
	// and button events. They never move the OS cursor to a 640x480 GUI cursor.
	if (RetainedUI_IsOpen() && !(console && console->Active())) return;

	if (console != NULL && console->Active()) {
		float windowMouseX = 0.0f;
		float windowMouseY = 0.0f;
		(void)SDL_GetMouseState(&windowMouseX, &windowMouseY);

		float cursorX = 0.0f;
		float cursorY = 0.0f;
		if (!SDL3_MapWindowMouseToConsoleCursor(windowMouseX, windowMouseY, cursorX, cursorY)) {
			return;
		}

		console->SetMousePosition(cursorX, cursorY);
		s_ignoreNextMenuWarpMotion = false;
		s_menuMouseInsideWindow = true;
		SDL3_SetMenuMouseTrackingPosition(cursorX, cursorY);
		return;
	}

	idUserInterface *activeGui = SDL3_GetActiveMenuGui();
	if (activeGui != NULL) {
#if defined(OPENQ4_SDL3_POSIX_HOST)
		float windowMouseX = 0.0f;
		float windowMouseY = 0.0f;
		(void)SDL_GetMouseState(&windowMouseX, &windowMouseY);

		float cursorX = 0.0f;
		float cursorY = 0.0f;
		if (!SDL3_MapWindowMouseToGuiCursor(windowMouseX, windowMouseY, cursorX, cursorY)) {
			return;
		}

		activeGui->SetCursor(cursorX, cursorY);
		s_ignoreNextMenuWarpMotion = false;
		s_menuMouseInsideWindow = true;
		SDL3_SetMenuMouseTrackingPosition(cursorX, cursorY);
		return;
#else
		sdl3GuiMouseTransform_t transform;
		if (!SDL3_BuildGuiMouseTransform(transform)) {
			return;
		}

		const float cursorX = activeGui->CursorX();
		const float cursorY = activeGui->CursorY();
		const float drawX = (cursorX * transform.xScale) + transform.xOffset;
		const float drawY = (cursorY * transform.yScale) + transform.yOffset;
		const float pixelMouseX = transform.drawAreaX + drawX * (transform.drawAreaWidth / transform.guiWidth);
		const float pixelMouseY = transform.drawAreaY + drawY * (transform.drawAreaHeight / transform.guiHeight);
		const float windowMouseX = pixelMouseX * transform.pixelToWindowX;
		const float windowMouseY = pixelMouseY * transform.pixelToWindowY;

		SDL_WarpMouseInWindow(s_sdlWindow, windowMouseX, windowMouseY);
		s_ignoreNextMenuWarpMotion = true;
		s_menuWarpWindowX = windowMouseX;
		s_menuWarpWindowY = windowMouseY;
		s_menuMouseInsideWindow = true;
		SDL3_SetMenuMouseTrackingPosition(cursorX, cursorY);
		return;
#endif
	}
}

static int SDL3_ClampJoystickValue(int value) {
	if (value < -127) {
		return -127;
	}
	if (value > 127) {
		return 127;
	}
	return value;
}

static float SDL3_ClampRange(float value, float minValue, float maxValue) {
	if (!std::isfinite(value)) {
		return minValue;
	}
	if (value < minValue) {
		return minValue;
	}
	if (value > maxValue) {
		return maxValue;
	}
	return value;
}

static Uint16 SDL3_ClampRumbleValue(float value) {
	if (!std::isfinite(value) || value <= 0.0f) {
		return 0;
	}
	if (value >= 1.0f) {
		return 0xffff;
	}
	return static_cast<Uint16>(value * 65535.0f + 0.5f);
}

static float SDL3_ClampUnit(float value) {
	return SDL3_ClampRange(value, 0.0f, 1.0f);
}

static float SDL3_ClampRumbleScale(float value) {
	return SDL3_ClampRange(value, 0.0f, 2.0f);
}

static SDL_PowerState SDL3_GetControllerPowerInfo(int &percent) {
	percent = -1;
	if (s_sdlGamepad) {
		return SDL_GetGamepadPowerInfo(s_sdlGamepad, &percent);
	}
	if (s_sdlJoystick) {
		return SDL_GetJoystickPowerInfo(s_sdlJoystick, &percent);
	}
	return SDL_POWERSTATE_UNKNOWN;
}

static void SDL3_UpdateLowBatteryRumbleLog(bool active, int percent, float cap) {
	if (active == s_lowBatteryRumbleScaleActive && (!active || percent == s_lowBatteryRumblePercent)) {
		return;
	}

	if (active) {
		common->Printf("controller: battery at %d%%; capping effective rumble scale to %.2f\n", percent, cap);
	} else if (s_lowBatteryRumbleScaleActive) {
		common->Printf("controller: battery recovered; restoring configured rumble scale.\n");
	}

	s_lowBatteryRumbleScaleActive = active;
	s_lowBatteryRumblePercent = active ? percent : -1;
}

static float SDL3_GetEffectiveRumbleScale(void) {
	const float configuredScale = SDL3_ClampRumbleScale(in_joystickRumbleScale.GetFloat());
	const int threshold = in_joystickLowBatteryRumbleThreshold.GetInteger();
	const float lowBatteryCap = SDL3_ClampRumbleScale(in_joystickLowBatteryRumbleScale.GetFloat());

	if (configuredScale <= 0.0f || lowBatteryCap >= configuredScale || threshold <= 0) {
		SDL3_UpdateLowBatteryRumbleLog(false, -1, lowBatteryCap);
		return configuredScale;
	}

	int percent = -1;
	const SDL_PowerState powerState = SDL3_GetControllerPowerInfo(percent);
	const bool lowBattery =
		powerState == SDL_POWERSTATE_ON_BATTERY &&
		percent >= 0 &&
		percent <= threshold;

	SDL3_UpdateLowBatteryRumbleLog(lowBattery, percent, lowBatteryCap);
	return lowBattery ? lowBatteryCap : configuredScale;
}

static float SDL3_NormalizeSignedAxisFloat(Sint16 value) {
	float normalized = static_cast<float>(value) / 32767.0f;
	if (normalized < -1.0f) {
		normalized = -1.0f;
	} else if (normalized > 1.0f) {
		normalized = 1.0f;
	}
	return normalized;
}

static int SDL3_AxisFloatToJoystickValue(float value) {
	if (!std::isfinite(value)) {
		return 0;
	}
	return SDL3_ClampJoystickValue(static_cast<int>(roundf(value * 127.0f)));
}

static void SDL3_NormalizeStickAxes(Sint16 rawX, Sint16 rawY, float deadZone, float curve, float scale, int &outX, int &outY) {
	const float x = SDL3_NormalizeSignedAxisFloat(rawX);
	const float y = SDL3_NormalizeSignedAxisFloat(rawY);
	const float length = sqrtf(x * x + y * y);
	const float clampedDeadZone = SDL3_ClampRange(deadZone, 0.0f, 0.95f);

	if (length <= clampedDeadZone) {
		outX = 0;
		outY = 0;
		return;
	}

	const float normalizedLength = (length > 1.0f) ? 1.0f : length;
	float adjustedLength = (normalizedLength - clampedDeadZone) / (1.0f - clampedDeadZone);
	adjustedLength = SDL3_ClampUnit(adjustedLength);

	const float responseCurve = SDL3_ClampRange(curve, 1.0f, 3.0f);
	if (responseCurve != 1.0f && adjustedLength > 0.0f) {
		adjustedLength = powf(adjustedLength, responseCurve);
	}

	adjustedLength = SDL3_ClampUnit(adjustedLength * SDL3_ClampRange(scale, 0.1f, 4.0f));

	const float unitScale = adjustedLength / length;
	outX = SDL3_AxisFloatToJoystickValue(x * unitScale);
	outY = SDL3_AxisFloatToJoystickValue(y * unitScale);
}

static int SDL3_NormalizeSignedAxis(Sint16 value, float deadZone) {
	int x = 0;
	int y = 0;
	SDL3_NormalizeStickAxes(value, 0, deadZone, 1.0f, 1.0f, x, y);
	return x;
}

static int SDL3_NormalizeSignedAxis(Sint16 value, float deadZone, float curve, float scale) {
	int x = 0;
	int y = 0;
	SDL3_NormalizeStickAxes(value, 0, deadZone, curve, scale, x, y);
	return x;
}

static int SDL3_NormalizeTriggerAxis(Sint16 value) {
	float normalized = static_cast<float>(value) / 32767.0f;
	normalized = SDL3_ClampUnit(normalized);

	return SDL3_AxisFloatToJoystickValue(normalized);
}

static bool SDL3_IsJoystickAxisAvailable(int axis, int numAxes) {
	return axis >= 0 && axis < numAxes;
}

static int SDL3_ResolveJoystickAxis(const idCVar &axisCVar, int autoAxis, int numAxes) {
	const int configuredAxis = axisCVar.GetInteger();
	const int axis = (configuredAxis < 0) ? autoAxis : configuredAxis;
	return SDL3_IsJoystickAxisAvailable(axis, numAxes) ? axis : -1;
}

static bool SDL3_ShouldUseDedicatedJoystickLookAxes(int lookAxisX, int lookAxisY) {
	const int mode = in_joystickUseDedicatedLookAxes.GetInteger();
	if (mode == 0) {
		return false;
	}

	if (mode > 0) {
		return lookAxisX >= 0 || lookAxisY >= 0;
	}

	return lookAxisX >= 0 && lookAxisY >= 0;
}

static bool SDL3_ShouldUseDedicatedJoystickLookAxes(int numAxes) {
	return SDL3_ShouldUseDedicatedJoystickLookAxes(
		SDL3_ResolveJoystickAxis(in_joystickLookAxisX, 2, numAxes),
		SDL3_ResolveJoystickAxis(in_joystickLookAxisY, 3, numAxes));
}

static Sint16 SDL3_GetJoystickAxisOrZero(int axis) {
	return axis >= 0 ? SDL_GetJoystickAxis(s_sdlJoystick, axis) : 0;
}

static void SDL3_NormalizeJoystickAxisPair(int axisX, int axisY, float deadZone, float curve, float scale, int &outX, int &outY) {
	outX = 0;
	outY = 0;

	if (axisX >= 0 && axisY >= 0) {
		SDL3_NormalizeStickAxes(
			SDL3_GetJoystickAxisOrZero(axisX),
			SDL3_GetJoystickAxisOrZero(axisY),
			deadZone,
			curve,
			scale,
			outX,
			outY);
	} else if (axisX >= 0) {
		outX = SDL3_NormalizeSignedAxis(SDL3_GetJoystickAxisOrZero(axisX), deadZone, curve, scale);
	} else if (axisY >= 0) {
		outY = SDL3_NormalizeSignedAxis(SDL3_GetJoystickAxisOrZero(axisY), deadZone, curve, scale);
	}
}

static const int s_joyKeys[32] = {
	K_JOY1, K_JOY2, K_JOY3, K_JOY4, K_JOY5, K_JOY6, K_JOY7, K_JOY8,
	K_JOY9, K_JOY10, K_JOY11, K_JOY12, K_JOY13, K_JOY14, K_JOY15, K_JOY16,
	K_JOY17, K_JOY18, K_JOY19, K_JOY20, K_JOY21, K_JOY22, K_JOY23, K_JOY24,
	K_JOY25, K_JOY26, K_JOY27, K_JOY28, K_JOY29, K_JOY30, K_JOY31, K_JOY32
};

static const int s_auxKeys[16] = {
	K_AUX1, K_AUX2, K_AUX3, K_AUX4, K_AUX5, K_AUX6, K_AUX7, K_AUX8,
	K_AUX9, K_AUX10, K_AUX11, K_AUX12, K_AUX13, K_AUX14, K_AUX15, K_AUX16
};

static int SDL3_JoyKeyFromOrdinal(int ordinal) {
	if (ordinal < 0) {
		return 0;
	}

	if (ordinal < static_cast<int>(sizeof(s_joyKeys) / sizeof(s_joyKeys[0]))) {
		return s_joyKeys[ordinal];
	}

	ordinal -= static_cast<int>(sizeof(s_joyKeys) / sizeof(s_joyKeys[0]));
	if (ordinal < static_cast<int>(sizeof(s_auxKeys) / sizeof(s_auxKeys[0]))) {
		return s_auxKeys[ordinal];
	}

	return 0;
}

static int SDL3_MapGamepadButton(Uint8 button) {
	switch (button) {
		case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return K_JOY1;
		case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return K_JOY2;
		case SDL_GAMEPAD_BUTTON_SOUTH: return K_JOY3;
		case SDL_GAMEPAD_BUTTON_EAST: return K_JOY4;
		case SDL_GAMEPAD_BUTTON_NORTH: return K_JOY5;
		case SDL_GAMEPAD_BUTTON_WEST: return K_JOY6;
		case SDL_GAMEPAD_BUTTON_START: return K_JOY7;
		case SDL_GAMEPAD_BUTTON_BACK: return K_JOY8;
		case SDL_GAMEPAD_BUTTON_DPAD_UP: return K_JOY9;
		case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return K_JOY10;
		case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return K_JOY11;
		case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return K_JOY12;
		case SDL_GAMEPAD_BUTTON_LEFT_STICK: return K_JOY13;
		case SDL_GAMEPAD_BUTTON_RIGHT_STICK: return K_JOY14;
		case SDL_GAMEPAD_BUTTON_GUIDE: return K_JOY17;
		case SDL_GAMEPAD_BUTTON_TOUCHPAD: return K_JOY18;
		case SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1: return K_JOY19;
		case SDL_GAMEPAD_BUTTON_LEFT_PADDLE1: return K_JOY20;
		case SDL_GAMEPAD_BUTTON_RIGHT_PADDLE2: return K_JOY21;
		case SDL_GAMEPAD_BUTTON_LEFT_PADDLE2: return K_JOY22;
		case SDL_GAMEPAD_BUTTON_MISC1: return K_JOY23;
		case SDL_GAMEPAD_BUTTON_MISC2: return K_JOY24;
		case SDL_GAMEPAD_BUTTON_MISC3: return K_JOY25;
		case SDL_GAMEPAD_BUTTON_MISC4: return K_JOY26;
		case SDL_GAMEPAD_BUTTON_MISC5: return K_JOY27;
		case SDL_GAMEPAD_BUTTON_MISC6: return K_JOY28;
		default:
			break;
	}

	return 0;
}

static void SDL3_ResetRumbleState(void) {
	s_joystickRumbleActive = false;
	s_joystickRumbleLow = 0;
	s_joystickRumbleHigh = 0;
	s_joystickRumbleUntilTime = 0;
	s_joystickRumbleLastUpdateTime = 0;
}

static bool SDL3_HasRumbleTarget(void) {
	return s_sdlGamepad != NULL || s_sdlJoystick != NULL;
}

static bool SDL3_SendControllerRumble(Uint16 low, Uint16 high, Uint32 durationMsec) {
	if (s_sdlGamepad) {
		return SDL_RumbleGamepad(s_sdlGamepad, low, high, durationMsec);
	}
	if (s_sdlJoystick) {
		return SDL_RumbleJoystick(s_sdlJoystick, low, high, durationMsec);
	}
	return false;
}

static void SDL3_ResetExpiredRumbleState(int now) {
	if (s_joystickRumbleActive && now >= s_joystickRumbleUntilTime) {
		SDL3_ResetRumbleState();
	}
}

static void SDL3_StopControllerRumble(void) {
	if (SDL3_HasRumbleTarget()) {
		(void)SDL3_SendControllerRumble(0, 0, 0);
	}
	SDL3_ResetRumbleState();
}

static bool SDL3_ShouldSkipRumbleUpdate(Uint16 low, Uint16 high, int now, int untilTime) {
	return s_joystickRumbleActive &&
		s_joystickRumbleLow == low &&
		s_joystickRumbleHigh == high &&
		now < s_joystickRumbleLastUpdateTime + SDL3_RUMBLE_DEBOUNCE_MSEC &&
		untilTime <= s_joystickRumbleUntilTime + SDL3_RUMBLE_DEBOUNCE_MSEC;
}

static void SDL3_ClearJoystickAxisStateUnlocked(void) {
	memset(s_joystickAxisState, 0, sizeof(s_joystickAxisState));
}

static void SDL3_ClearJoystickState(void) {
	Sys_EnterCriticalSection(CRITICAL_SECTION_ONE);
	SDL3_ClearJoystickAxisStateUnlocked();
	Sys_LeaveCriticalSection(CRITICAL_SECTION_ONE);
}

static void SDL3_ClearControllerTrackingStateUnlocked(void) {
	SDL3_ClearJoystickAxisStateUnlocked();
	memset(s_gamepadButtonsDown, 0, sizeof(s_gamepadButtonsDown));
	s_gamepadLeftTriggerDown = false;
	s_gamepadRightTriggerDown = false;
	memset(s_joystickButtonsDown, 0, sizeof(s_joystickButtonsDown));
	s_joystickHatState = SDL_HAT_CENTERED;
}

static void SDL3_ClearControllerTrackingState(void) {
	Sys_EnterCriticalSection(CRITICAL_SECTION_ONE);
	SDL3_ClearControllerTrackingStateUnlocked();
	Sys_LeaveCriticalSection(CRITICAL_SECTION_ONE);
}

static void SDL3_PostControllerKeyEvent(int key, bool down, int eventTime) {
	if (key == 0) {
		return;
	}

	if (!SDL3_QueueRetainedKey(key,down,false,2048+key,eventTime))
		Sys_QueEvent(eventTime, SE_KEY, key, down ? 1 : 0, 0, NULL);
	SDL3_QueueKeyboardInput(key, down, eventTime);
}

static bool SDL3_TriggerButtonIsDown(int triggerValue, bool wasDown, int pressThreshold) {
	if (pressThreshold <= 0) {
		return triggerValue > 0;
	}

	const int releaseThreshold = idMath::ClampInt(1, pressThreshold, pressThreshold - 8);
	return wasDown ? triggerValue >= releaseThreshold : triggerValue >= pressThreshold;
}

static void SDL3_UpdateTriggerButtons(int leftTrigger, int rightTrigger, int eventTime) {
	const float threshold = SDL3_ClampUnit(in_joystickTriggerThreshold.GetFloat());
	const int pressThreshold = static_cast<int>(roundf(threshold * 127.0f));
	const bool leftDown = SDL3_TriggerButtonIsDown(leftTrigger, s_gamepadLeftTriggerDown, pressThreshold);
	const bool rightDown = SDL3_TriggerButtonIsDown(rightTrigger, s_gamepadRightTriggerDown, pressThreshold);

	if (leftDown != s_gamepadLeftTriggerDown) {
		s_gamepadLeftTriggerDown = leftDown;
		SDL3_PostControllerKeyEvent(K_JOY16, leftDown, eventTime);
	}
	if (rightDown != s_gamepadRightTriggerDown) {
		s_gamepadRightTriggerDown = rightDown;
		SDL3_PostControllerKeyEvent(K_JOY15, rightDown, eventTime);
	}
}

static void SDL3_UpdateGamepadAxes(int eventTime) {
	if (!s_sdlGamepad) {
		SDL3_ClearJoystickState();
		return;
	}

	const float deadZone = SDL3_ClampUnit(in_joystickDeadZone.GetFloat());

	const bool southpaw = in_joystickSouthpaw.GetBool();
	const SDL_GamepadAxis moveAxisX = southpaw ? SDL_GAMEPAD_AXIS_RIGHTX : SDL_GAMEPAD_AXIS_LEFTX;
	const SDL_GamepadAxis moveAxisY = southpaw ? SDL_GAMEPAD_AXIS_RIGHTY : SDL_GAMEPAD_AXIS_LEFTY;
	const SDL_GamepadAxis lookAxisX = southpaw ? SDL_GAMEPAD_AXIS_LEFTX : SDL_GAMEPAD_AXIS_RIGHTX;
	const SDL_GamepadAxis lookAxisY = southpaw ? SDL_GAMEPAD_AXIS_LEFTY : SDL_GAMEPAD_AXIS_RIGHTY;
	int moveX = 0;
	int moveY = 0;
	int lookX = 0;
	int lookY = 0;

	SDL3_NormalizeStickAxes(
		SDL_GetGamepadAxis(s_sdlGamepad, moveAxisX),
		SDL_GetGamepadAxis(s_sdlGamepad, moveAxisY),
		deadZone,
		in_joystickMoveCurve.GetFloat(),
		1.0f,
		moveX,
		moveY);
	SDL3_NormalizeStickAxes(
		SDL_GetGamepadAxis(s_sdlGamepad, lookAxisX),
		SDL_GetGamepadAxis(s_sdlGamepad, lookAxisY),
		deadZone,
		in_joystickLookCurve.GetFloat(),
		1.0f,
		lookX,
		lookY);

	moveY = -moveY;
	if (in_joystickInvertLook.GetBool()) {
		lookY = -lookY;
	}

	const int leftTrigger = SDL3_NormalizeTriggerAxis(SDL_GetGamepadAxis(s_sdlGamepad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER));
	const int rightTrigger = SDL3_NormalizeTriggerAxis(SDL_GetGamepadAxis(s_sdlGamepad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER));

	Sys_EnterCriticalSection(CRITICAL_SECTION_ONE);
	s_joystickAxisState[AXIS_SIDE] = lookX;
	s_joystickAxisState[AXIS_FORWARD] = lookY;
	// Keep SDL gamepad triggers available as modern digital binds (LT/RT) instead of
	// also folding them into the legacy vertical movement axis.
	s_joystickAxisState[AXIS_UP] = 0;
	s_joystickAxisState[AXIS_ROLL] = 127;
	s_joystickAxisState[AXIS_YAW] = moveX;
	s_joystickAxisState[AXIS_PITCH] = moveY;
	Sys_LeaveCriticalSection(CRITICAL_SECTION_ONE);

	SDL3_UpdateTriggerButtons(leftTrigger, rightTrigger, eventTime);
	const int activity = Max(Max(idMath::Abs(moveX), idMath::Abs(moveY)), Max(idMath::Abs(lookX), idMath::Abs(lookY)));
	SDL3_QueueControllerActivity(eventTime, activity);
}

static void SDL3_UpdateJoystickAxes(void) {
	if (!s_sdlJoystick) {
		SDL3_ClearJoystickState();
		return;
	}

	const float deadZone = SDL3_ClampUnit(in_joystickDeadZone.GetFloat());
	const int numAxes = SDL_GetNumJoystickAxes(s_sdlJoystick);
	int moveAxisX = SDL3_ResolveJoystickAxis(in_joystickMoveAxisX, 0, numAxes);
	int moveAxisY = SDL3_ResolveJoystickAxis(in_joystickMoveAxisY, 1, numAxes);
	int lookAxisX = SDL3_ResolveJoystickAxis(in_joystickLookAxisX, 2, numAxes);
	int lookAxisY = SDL3_ResolveJoystickAxis(in_joystickLookAxisY, 3, numAxes);
	const int upAxis = SDL3_ResolveJoystickAxis(in_joystickUpAxis, 4, numAxes);
	const int upAxisNegative = SDL3_ResolveJoystickAxis(in_joystickUpAxisNegative, 5, numAxes);
	const bool hasDedicatedLookAxis = SDL3_ShouldUseDedicatedJoystickLookAxes(lookAxisX, lookAxisY);

	if (hasDedicatedLookAxis && in_joystickSouthpaw.GetBool()) {
		const int oldMoveAxisX = moveAxisX;
		const int oldMoveAxisY = moveAxisY;
		moveAxisX = lookAxisX;
		moveAxisY = lookAxisY;
		lookAxisX = oldMoveAxisX;
		lookAxisY = oldMoveAxisY;
	}

	int moveX = 0;
	int moveY = 0;
	int lookX = 0;
	int lookY = 0;
	int up = 0;

	SDL3_NormalizeJoystickAxisPair(
		moveAxisX,
		moveAxisY,
		deadZone,
		in_joystickMoveCurve.GetFloat(),
		1.0f,
		moveX,
		moveY);
	moveY = -moveY;

	if (hasDedicatedLookAxis) {
		SDL3_NormalizeJoystickAxisPair(
			lookAxisX,
			lookAxisY,
			deadZone,
			in_joystickLookCurve.GetFloat(),
			1.0f,
			lookX,
			lookY);
		if (in_joystickInvertLook.GetBool()) {
			lookY = -lookY;
		}
	}

	if (upAxis >= 0 && upAxisNegative >= 0 && upAxis != upAxisNegative) {
		const int upPositive = SDL3_NormalizeSignedAxis(SDL3_GetJoystickAxisOrZero(upAxis), deadZone);
		const int upNegative = SDL3_NormalizeSignedAxis(SDL3_GetJoystickAxisOrZero(upAxisNegative), deadZone);
		up = SDL3_ClampJoystickValue(upPositive - upNegative);
	} else if (upAxis >= 0) {
		up = SDL3_NormalizeSignedAxis(SDL3_GetJoystickAxisOrZero(upAxis), deadZone);
	} else if (upAxisNegative >= 0) {
		up = -SDL3_NormalizeSignedAxis(SDL3_GetJoystickAxisOrZero(upAxisNegative), deadZone);
	}

	Sys_EnterCriticalSection(CRITICAL_SECTION_ONE);
	s_joystickAxisState[AXIS_SIDE] = lookX;
	s_joystickAxisState[AXIS_FORWARD] = lookY;
	s_joystickAxisState[AXIS_UP] = up;
	s_joystickAxisState[AXIS_ROLL] = hasDedicatedLookAxis ? 127 : 0;
	s_joystickAxisState[AXIS_YAW] = moveX;
	s_joystickAxisState[AXIS_PITCH] = moveY;
	Sys_LeaveCriticalSection(CRITICAL_SECTION_ONE);

	const int activity = Max(Max(idMath::Abs(moveX), idMath::Abs(moveY)),
		Max(Max(idMath::Abs(lookX), idMath::Abs(lookY)), idMath::Abs(up)));
	SDL3_QueueControllerActivity(Sys_Milliseconds(), activity);
}

static void SDL3_SetJoystickHat(Uint8 newHat, int eventTime) {
	const Uint8 oldHat = s_joystickHatState;
	const bool oldUp = (oldHat & SDL_HAT_UP) != 0;
	const bool oldDown = (oldHat & SDL_HAT_DOWN) != 0;
	const bool oldRight = (oldHat & SDL_HAT_RIGHT) != 0;
	const bool oldLeft = (oldHat & SDL_HAT_LEFT) != 0;
	const bool newUp = (newHat & SDL_HAT_UP) != 0;
	const bool newDown = (newHat & SDL_HAT_DOWN) != 0;
	const bool newRight = (newHat & SDL_HAT_RIGHT) != 0;
	const bool newLeft = (newHat & SDL_HAT_LEFT) != 0;

	if (oldUp != newUp) {
		SDL3_PostControllerKeyEvent(K_JOY9, newUp, eventTime);
	}
	if (oldDown != newDown) {
		SDL3_PostControllerKeyEvent(K_JOY10, newDown, eventTime);
	}
	if (oldRight != newRight) {
		SDL3_PostControllerKeyEvent(K_JOY11, newRight, eventTime);
	}
	if (oldLeft != newLeft) {
		SDL3_PostControllerKeyEvent(K_JOY12, newLeft, eventTime);
	}

	s_joystickHatState = newHat;
}

static void SDL3_ReleaseGamepadState(int eventTime) {
	SDL3_QueueRetainedCancel(eventTime);
	for (int i = 0; i < SDL_GAMEPAD_BUTTON_COUNT; ++i) {
		if (!s_gamepadButtonsDown[i]) {
			continue;
		}
		s_gamepadButtonsDown[i] = false;
		SDL3_PostControllerKeyEvent(SDL3_MapGamepadButton(static_cast<Uint8>(i)), false, eventTime);
	}

	if (s_gamepadLeftTriggerDown) {
		s_gamepadLeftTriggerDown = false;
		SDL3_PostControllerKeyEvent(K_JOY16, false, eventTime);
	}
	if (s_gamepadRightTriggerDown) {
		s_gamepadRightTriggerDown = false;
		SDL3_PostControllerKeyEvent(K_JOY15, false, eventTime);
	}
}

static void SDL3_ReleaseJoystickState(int eventTime) {
	SDL3_QueueRetainedCancel(eventTime);
	for (int i = 0; i < SDL3_MAX_JOYSTICK_BUTTONS; ++i) {
		if (!s_joystickButtonsDown[i]) {
			continue;
		}
		s_joystickButtonsDown[i] = false;
		SDL3_PostControllerKeyEvent(SDL3_JoyKeyFromOrdinal(i), false, eventTime);
	}

	SDL3_SetJoystickHat(SDL_HAT_CENTERED, eventTime);
}

static void SDL3_CloseGamepad(int eventTime) {
	if (!s_sdlGamepad) {
		return;
	}

	SDL3_StopControllerRumble();
	SDL3_ReleaseGamepadState(eventTime);
	if (s_gamepadGyroEnabled) {
		(void)SDL_SetGamepadSensorEnabled(s_sdlGamepad, SDL_SENSOR_GYRO, false);
	}
	SDL_CloseGamepad(s_sdlGamepad);
	s_sdlGamepad = NULL;
	s_sdlGamepadId = 0;
	s_gamepadGyroEnabled = false;
	s_gamepadGyroLastTimestamp = 0;
	s_gamepadTouchpadFingerActive = false;
	SDL3_ClearJoystickState();
}

static void SDL3_CloseJoystick(int eventTime) {
	if (!s_sdlJoystick) {
		return;
	}

	SDL3_StopControllerRumble();
	SDL3_ReleaseJoystickState(eventTime);
	SDL_CloseJoystick(s_sdlJoystick);
	s_sdlJoystick = NULL;
	s_sdlJoystickId = 0;
	SDL3_ClearJoystickState();
}

static int SDL3_ClampSensorAxisIndex(int axis) {
	if (axis < 0) {
		return 0;
	}
	if (axis > 2) {
		return 2;
	}
	return axis;
}

static float SDL3_ApplyDeadZone(float value, float deadZone) {
	if (fabsf(value) < deadZone) {
		return 0.0f;
	}
	return value;
}

static Uint64 SDL3_GamepadSensorTimestamp(const SDL_GamepadSensorEvent &event) {
	if (event.sensor_timestamp != 0) {
		return event.sensor_timestamp;
	}
	return event.timestamp;
}

static void SDL3_UpdateGamepadSensorState(bool logCapabilities) {
	s_gamepadGyroEnabled = false;
	s_gamepadGyroLastTimestamp = 0;
	s_gamepadGyroRemainderX = 0.0f;
	s_gamepadGyroRemainderY = 0.0f;

	if (!s_sdlGamepad) {
		return;
	}

	const int touchpadCount = SDL_GetNumGamepadTouchpads(s_sdlGamepad);
	const bool hasGyro = SDL_GamepadHasSensor(s_sdlGamepad, SDL_SENSOR_GYRO);
	if (logCapabilities) {
		common->Printf("controller: SDL gamepad capabilities: touchpads=%d gyro=%s\n", touchpadCount, hasGyro ? "yes" : "no");
	}

	if (!hasGyro) {
		return;
	}

	const bool wantsGyro = in_joystick.GetBool() && in_gyro.GetBool();
	if (!SDL_SetGamepadSensorEnabled(s_sdlGamepad, SDL_SENSOR_GYRO, wantsGyro)) {
		if (wantsGyro) {
			common->Printf("controller: failed to enable SDL gamepad gyro: %s\n", SDL_GetError());
		}
		return;
	}

	s_gamepadGyroEnabled = wantsGyro;
	if (logCapabilities && wantsGyro) {
		const float dataRate = SDL_GetGamepadSensorDataRate(s_sdlGamepad, SDL_SENSOR_GYRO);
		if (dataRate > 0.0f) {
			common->Printf("controller: SDL gamepad gyro enabled at %.1f Hz\n", dataRate);
		} else {
			common->Printf("controller: SDL gamepad gyro enabled\n");
		}
	}
}

static void SDL3_HandleGamepadGyroEvent(const SDL_GamepadSensorEvent &event, int eventTime) {
	if (!in_joystick.GetBool() || !in_gyro.GetBool() || !s_sdlGamepad || event.which != s_sdlGamepadId || event.sensor != SDL_SENSOR_GYRO) {
		return;
	}
	if (!s_gamepadGyroEnabled) {
		SDL3_UpdateGamepadSensorState(false);
		if (!s_gamepadGyroEnabled) {
			return;
		}
	}
	if (!SDL3_IsMouseCaptured() || SDL3_ShouldRouteMenuMouse()) {
		s_gamepadGyroLastTimestamp = SDL3_GamepadSensorTimestamp(event);
		s_gamepadGyroRemainderX = 0.0f;
		s_gamepadGyroRemainderY = 0.0f;
		return;
	}

	const Uint64 sensorTimestamp = SDL3_GamepadSensorTimestamp(event);
	if (sensorTimestamp == 0 || s_gamepadGyroLastTimestamp == 0 || sensorTimestamp <= s_gamepadGyroLastTimestamp) {
		s_gamepadGyroLastTimestamp = sensorTimestamp;
		return;
	}

	const Uint64 deltaNs = sensorTimestamp - s_gamepadGyroLastTimestamp;
	s_gamepadGyroLastTimestamp = sensorTimestamp;

	float deltaSeconds = static_cast<float>(static_cast<double>(deltaNs) / 1000000000.0);
	deltaSeconds = SDL3_ClampRange(deltaSeconds, 0.0f, 0.050f);
	if (deltaSeconds <= 0.0f) {
		return;
	}

	const int yawAxis = SDL3_ClampSensorAxisIndex(in_gyroYawAxis.GetInteger());
	const int pitchAxis = SDL3_ClampSensorAxisIndex(in_gyroPitchAxis.GetInteger());
	const float deadZone = SDL3_ClampRange(in_gyroDeadZone.GetFloat(), 0.0f, 2.0f);
	const float sensitivity = SDL3_ClampRange(in_gyroSensitivity.GetFloat(), 0.0f, 8.0f);
	if (sensitivity <= 0.0f) {
		return;
	}

	float yawRate = SDL3_ApplyDeadZone(event.data[yawAxis], deadZone);
	float pitchRate = SDL3_ApplyDeadZone(event.data[pitchAxis], deadZone);
	if (in_gyroInvertYaw.GetBool()) {
		yawRate = -yawRate;
	}
	if (in_gyroInvertPitch.GetBool()) {
		pitchRate = -pitchRate;
	}

	static const float RADIANS_TO_CLASSIC_MOUSE_COUNTS = 2600.0f;
	const float yawCounts = yawRate * deltaSeconds * RADIANS_TO_CLASSIC_MOUSE_COUNTS * sensitivity;
	const float pitchCounts = pitchRate * deltaSeconds * RADIANS_TO_CLASSIC_MOUSE_COUNTS * sensitivity;
	const int dx = SDL3_ConsumeMouseDelta(yawCounts, s_gamepadGyroRemainderX);
	const int dy = SDL3_ConsumeMouseDelta(pitchCounts, s_gamepadGyroRemainderY);
	SDL3_QueueMouseDelta(dx, dy, eventTime);
	if (dx != 0 || dy != 0) {
		SDL3_QueueControllerActivity(eventTime, 127);
	}
}

static bool SDL3_GamepadTouchpadEventMatchesTrackedFinger(const SDL_GamepadTouchpadEvent &event) {
	return s_gamepadTouchpadFingerActive &&
		event.touchpad == s_gamepadTouchpadIndex &&
		event.finger == s_gamepadTouchpadFinger;
}

static void SDL3_OpenFirstController(void);
static bool SDL3_IsSteamDeckPlatformProfile(void);

static void SDL3_HandleGamepadTouchpadEvent(const SDL_GamepadTouchpadEvent &event, int eventTime) {
	if (!in_joystick.GetBool() || !s_sdlGamepad || event.which != s_sdlGamepadId) {
		return;
	}

	const int mode = in_touchpadMode.GetInteger();
	if (mode <= 0 || mode >= 3) {
		if (event.type == SDL_EVENT_GAMEPAD_TOUCHPAD_UP && SDL3_GamepadTouchpadEventMatchesTrackedFinger(event)) {
			s_gamepadTouchpadFingerActive = false;
		}
		return;
	}

	if (event.type == SDL_EVENT_GAMEPAD_TOUCHPAD_DOWN) {
		if (s_gamepadTouchpadFingerActive) {
			return;
		}
		s_gamepadTouchpadFingerActive = true;
		s_gamepadTouchpadIndex = event.touchpad;
		s_gamepadTouchpadFinger = event.finger;
		s_gamepadTouchpadLastX = event.x;
		s_gamepadTouchpadLastY = event.y;
		s_gamepadTouchpadRemainderX = 0.0f;
		s_gamepadTouchpadRemainderY = 0.0f;
		return;
	}

	if (!SDL3_GamepadTouchpadEventMatchesTrackedFinger(event)) {
		return;
	}

	if (event.type == SDL_EVENT_GAMEPAD_TOUCHPAD_UP) {
		s_gamepadTouchpadFingerActive = false;
		return;
	}
	if (event.type != SDL_EVENT_GAMEPAD_TOUCHPAD_MOTION) {
		return;
	}

	bool shouldRoute = false;
	if (mode == 1) {
		shouldRoute = SDL3_ShouldRouteMenuMouse();
	} else if (mode == 2) {
		shouldRoute = SDL3_IsMouseCaptured() && !SDL3_ShouldRouteMenuMouse();
	}
	if (!shouldRoute) {
		s_gamepadTouchpadLastX = event.x;
		s_gamepadTouchpadLastY = event.y;
		s_gamepadTouchpadRemainderX = 0.0f;
		s_gamepadTouchpadRemainderY = 0.0f;
		return;
	}

	int windowWidth = SCREEN_WIDTH;
	int windowHeight = SCREEN_HEIGHT;
	if (s_sdlWindow) {
		(void)SDL_GetWindowSize(s_sdlWindow, &windowWidth, &windowHeight);
		if (windowWidth <= 0) {
			windowWidth = SCREEN_WIDTH;
		}
		if (windowHeight <= 0) {
			windowHeight = SCREEN_HEIGHT;
		}
	}

	const float sensitivity = SDL3_ClampRange(in_touchpadSensitivity.GetFloat(), 0.0f, 8.0f);
	const float deltaX = (event.x - s_gamepadTouchpadLastX) * static_cast<float>(windowWidth) * sensitivity;
	const float deltaY = (event.y - s_gamepadTouchpadLastY) * static_cast<float>(windowHeight) * sensitivity;
	s_gamepadTouchpadLastX = event.x;
	s_gamepadTouchpadLastY = event.y;

	const int dx = SDL3_ConsumeMouseDelta(deltaX, s_gamepadTouchpadRemainderX);
	const int dy = SDL3_ConsumeMouseDelta(deltaY, s_gamepadTouchpadRemainderY);
	SDL3_QueueMouseDelta(dx, dy, eventTime);
	if (dx != 0 || dy != 0) {
		SDL3_QueueControllerActivity(eventTime, 127);
	}
}

static void SDL3_HandleFingerEvent(const SDL_TouchFingerEvent &event, int eventTime) {
	if (!in_touchscreen.GetBool() || !s_sdlWindow) {
		return;
	}

	const SDL_WindowID windowId = SDL_GetWindowID(s_sdlWindow);
	if (event.windowID != 0 && windowId != 0 && event.windowID != windowId) {
		return;
	}

	const bool down = event.type == SDL_EVENT_FINGER_DOWN;
	const bool up = event.type == SDL_EVENT_FINGER_UP || event.type == SDL_EVENT_FINGER_CANCELED;
	const bool motion = event.type == SDL_EVENT_FINGER_MOTION;

	if (down) {
		if (s_touchscreenFingerActive) {
			return;
		}
		s_touchscreenFingerActive = true;
		s_touchscreenFingerId = event.fingerID;
	} else if (!s_touchscreenFingerActive || event.fingerID != s_touchscreenFingerId) {
		return;
	}

	int windowWidth = SCREEN_WIDTH;
	int windowHeight = SCREEN_HEIGHT;
	(void)SDL_GetWindowSize(s_sdlWindow, &windowWidth, &windowHeight);
	if (windowWidth <= 0) {
		windowWidth = SCREEN_WIDTH;
	}
	if (windowHeight <= 0) {
		windowHeight = SCREEN_HEIGHT;
	}

	const float windowMouseX = SDL3_ClampRange(event.x, 0.0f, 1.0f) * static_cast<float>(windowWidth);
	const float windowMouseY = SDL3_ClampRange(event.y, 0.0f, 1.0f) * static_cast<float>(windowHeight);
	int dx = 0;
	int dy = 0;
	const bool routed = SDL3_ShouldRouteMenuMouse() && SDL3_SetRoutedCursorFromWindowPosition(windowMouseX, windowMouseY, dx, dy);

	(void)motion;
	if (down || up) {
		const bool acceptsLoadingInput = openQ4_AcceptingLoadingContinueInput();
		if (routed || acceptsLoadingInput) {
			SDL3_QueueMouseButtonEvent(K_MOUSE1, down, eventTime, routed);
		}
	}

	if (up) {
		s_touchscreenFingerActive = false;
		s_touchscreenFingerId = 0;
	}
}

static void SDL3_ReleaseFocusInputState(int eventTime) {
	if (s_sdlFocusInputReleased) {
		return;
	}
	s_sdlFocusInputReleased = true;
	SDL3_QueueRetainedFocus(false,eventTime);
	memset(s_retainedKeyOwners,0,sizeof(s_retainedKeyOwners));

	// Desktop focus changes do not necessarily produce the app-level
	// background events used on handheld/mobile platforms. Release every
	// latched input path here too so Alt+Tab cannot leave fullscreen relative
	// mouse mode, controller buttons, triggers, hats, or axes stuck active.
#if defined(OPENQ4_SIGMATOUCH)
	Quake4_ClearTouchInput();
	Quake4_ResetTouchState();
#endif
	SDL3_ClearInputQueues();
	idKeyInput::ClearStates();
	Sys_GrabMouseCursor(false);
	SDL3_StopControllerRumble();
	SDL3_ReleaseGamepadState(eventTime);
	SDL3_ReleaseJoystickState(eventTime);
	SDL3_ClearJoystickState();
}

static bool SDL3_IsUserInputEvent(Uint32 eventType) {
	switch (eventType) {
		case SDL_EVENT_KEY_DOWN:
		case SDL_EVENT_KEY_UP:
		case SDL_EVENT_TEXT_EDITING:
		case SDL_EVENT_TEXT_EDITING_CANDIDATES:
		case SDL_EVENT_TEXT_INPUT:
		case SDL_EVENT_MOUSE_MOTION:
		case SDL_EVENT_MOUSE_BUTTON_DOWN:
		case SDL_EVENT_MOUSE_BUTTON_UP:
		case SDL_EVENT_MOUSE_WHEEL:
		case SDL_EVENT_GAMEPAD_REMAPPED:
		case SDL_EVENT_GAMEPAD_STEAM_HANDLE_UPDATED:
		case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
		case SDL_EVENT_GAMEPAD_BUTTON_UP:
		case SDL_EVENT_GAMEPAD_AXIS_MOTION:
		case SDL_EVENT_GAMEPAD_TOUCHPAD_DOWN:
		case SDL_EVENT_GAMEPAD_TOUCHPAD_MOTION:
		case SDL_EVENT_GAMEPAD_TOUCHPAD_UP:
		case SDL_EVENT_GAMEPAD_SENSOR_UPDATE:
		case SDL_EVENT_JOYSTICK_BUTTON_DOWN:
		case SDL_EVENT_JOYSTICK_BUTTON_UP:
		case SDL_EVENT_JOYSTICK_HAT_MOTION:
		case SDL_EVENT_JOYSTICK_AXIS_MOTION:
		case SDL_EVENT_FINGER_DOWN:
		case SDL_EVENT_FINGER_MOTION:
		case SDL_EVENT_FINGER_UP:
		case SDL_EVENT_FINGER_CANCELED:
			return true;
		default:
			return false;
	}
}

static void SDL3_SetLifecyclePendingFlag(int flag) {
	for (;;) {
		const int currentFlags = SDL_GetAtomicInt(&s_sdlLifecyclePending);
		const int newFlags = currentFlags | flag;
		if (newFlags == currentFlags || SDL_CompareAndSwapAtomicInt(&s_sdlLifecyclePending, currentFlags, newFlags)) {
			return;
		}
	}
}

static void SDL3_ClearLifecyclePendingFlag(int flag) {
	for (;;) {
		const int currentFlags = SDL_GetAtomicInt(&s_sdlLifecyclePending);
		const int newFlags = currentFlags & ~flag;
		if (newFlags == currentFlags || SDL_CompareAndSwapAtomicInt(&s_sdlLifecyclePending, currentFlags, newFlags)) {
			return;
		}
	}
}

static bool SDLCALL SDL3_LifecycleEventWatch(void *userdata, SDL_Event *event) {
	(void)userdata;

	if (event == NULL) {
		return true;
	}

	switch (event->type) {
		case SDL_EVENT_TERMINATING:
		case SDL_EVENT_WILL_ENTER_BACKGROUND:
		case SDL_EVENT_DID_ENTER_BACKGROUND:
			SDL3_SetLifecyclePendingFlag(SDL3_LIFECYCLE_PENDING_BACKGROUND);
			break;

		case SDL_EVENT_WILL_ENTER_FOREGROUND:
		case SDL_EVENT_DID_ENTER_FOREGROUND:
			SDL3_SetLifecyclePendingFlag(SDL3_LIFECYCLE_PENDING_FOREGROUND);
			break;

		case SDL_EVENT_LOW_MEMORY:
			SDL3_SetLifecyclePendingFlag(SDL3_LIFECYCLE_PENDING_LOW_MEMORY);
			break;

		default:
			break;
	}

	return true;
}

static void SDL3_RegisterLifecycleEventWatch(void) {
	if (s_sdlLifecycleEventWatchRegistered) {
		return;
	}

	SDL_SetAtomicInt(&s_sdlLifecyclePending, 0);
	if (SDL_AddEventWatch(SDL3_LifecycleEventWatch, NULL)) {
		s_sdlLifecycleEventWatchRegistered = true;
	} else {
		common->Printf("SDL3: could not install lifecycle event watch: %s\n", SDL_GetError());
	}
}

static void SDL3_UnregisterLifecycleEventWatch(void) {
	if (s_sdlLifecycleEventWatchRegistered) {
		SDL_RemoveEventWatch(SDL3_LifecycleEventWatch, NULL);
		s_sdlLifecycleEventWatchRegistered = false;
	}
	SDL_SetAtomicInt(&s_sdlLifecyclePending, 0);
}

static void SDL3_HandleAppBackgroundTransition(int eventTime, const char *reason) {
	if (s_sdlAppInBackground) {
		return;
	}

	openq4::NativeInputBeforeWindowChange();
	s_sdlAppInBackground = true;
	openq4::NativeInputBeforeWindowChange();
	win32.activeApp = false;
	win32.movingWindow = false;
	s_menuMouseInsideWindow = false;
	SDL3_InvalidateMenuMouseRouting();

	common->Printf("SDL3: application entering background (%s); releasing input and writing config.\n", reason);
	SDL3_ReleaseFocusInputState(eventTime);
	if (s_sdlGamepad && s_gamepadGyroEnabled) {
		(void)SDL_SetGamepadSensorEnabled(s_sdlGamepad, SDL_SENSOR_GYRO, false);
		s_gamepadGyroEnabled = false;
	}
	SDL3_UpdateCursorVisibility();
	if (session != NULL) {
		session->SetPlayingSoundWorld();
	}

	if (cvarSystem != NULL && cvarSystem->IsInitialized()) {
		cvarSystem->SetModifiedFlags(CVAR_ARCHIVE);
	}
	if (common != NULL && common->IsInitialized() && fileSystem != NULL && fileSystem->IsInitialized()) {
		common->WriteConfigToFile(CONFIG_FILE);
	}
}

static void SDL3_HandleAppForegroundTransition(int eventTime, const char *reason) {
	if (!s_sdlAppInBackground) {
		return;
	}

	openq4::NativeInputBeforeWindowChange();
	s_sdlAppInBackground = false;
	openq4::NativeInputBeforeWindowChange();
	win32.activeApp = Sys_SDL_IsGameWindowFocused();
	SDL3_QueueRetainedFocus(win32.activeApp,eventTime);
	win32.movingWindow = false;
	s_menuMouseInsideWindow = s_sdlWindow != NULL &&
		(SDL_GetWindowFlags(s_sdlWindow) & SDL_WINDOW_MOUSE_FOCUS) != 0;
	SDL3_InvalidateMenuMouseRouting();
	SDL3_ClearInputQueues();
	idKeyInput::ClearStates();

	common->Printf("SDL3: application returned to foreground (%s); reacquiring input.\n", reason);
	SDL3_OpenFirstController();
	if (s_sdlGamepad) {
		SDL3_UpdateGamepadSensorState(false);
		SDL3_UpdateGamepadAxes(eventTime);
	} else if (s_sdlJoystick) {
		SDL3_UpdateJoystickAxes();
	}
	if (win32.activeApp) {
		s_sdlFocusInputReleased = false;
		Sys_GrabMouseCursor(true);
	}
	SDL3_UpdateCursorVisibility();
	if (session != NULL) {
		session->SetPlayingSoundWorld();
	}
}

static void SDL3_ProcessPendingLifecycleEvents(int eventTime) {
	const int pendingFlags = SDL_SetAtomicInt(&s_sdlLifecyclePending, 0);
	if (pendingFlags == 0) {
		return;
	}

	if ((pendingFlags & SDL3_LIFECYCLE_PENDING_LOW_MEMORY) != 0) {
		common->Printf("SDL3: low-memory event received from lifecycle event watch.\n");
	}
	if ((pendingFlags & SDL3_LIFECYCLE_PENDING_BACKGROUND) != 0) {
		SDL3_HandleAppBackgroundTransition(eventTime, "event watch");
	}
	if ((pendingFlags & SDL3_LIFECYCLE_PENDING_FOREGROUND) != 0) {
		SDL3_HandleAppForegroundTransition(eventTime, "event watch");
	}
}

static bool SDL3_OpenGamepad(SDL_JoystickID instanceId) {
	SDL_Gamepad *pad = SDL_OpenGamepad(instanceId);
	if (!pad) {
		return false;
	}

	SDL3_CloseJoystick(Sys_Milliseconds());

	s_sdlGamepad = pad;
	s_sdlGamepadId = instanceId;
	SDL3_ClearControllerTrackingState();

	SDL3_UpdateGamepadAxes(Sys_Milliseconds());
	SDL3_UpdateGamepadSensorState(true);

	const char *name = SDL_GetGamepadName(pad);
	if (name && name[0] != '\0') {
		common->Printf("controller: opened SDL gamepad '%s'\n", name);
	} else {
		common->Printf("controller: opened SDL gamepad\n");
	}
	return true;
}

static bool SDL3_OpenJoystick(SDL_JoystickID instanceId) {
	if (SDL_IsGamepad(instanceId)) {
		return false;
	}

	SDL_Joystick *joystick = SDL_OpenJoystick(instanceId);
	if (!joystick) {
		return false;
	}

	s_sdlJoystick = joystick;
	s_sdlJoystickId = instanceId;
	SDL3_ClearControllerTrackingState();

	SDL3_UpdateJoystickAxes();

	const char *name = SDL_GetJoystickName(joystick);
	const int numAxes = SDL_GetNumJoystickAxes(joystick);
	const int numButtons = SDL_GetNumJoystickButtons(joystick);
	const int numHats = SDL_GetNumJoystickHats(joystick);
	const char *dedicatedLook = SDL3_ShouldUseDedicatedJoystickLookAxes(numAxes) ? "yes" : "no";
	if (name && name[0] != '\0') {
		common->Printf("controller: opened SDL joystick '%s' (axes=%d, buttons=%d, hats=%d, dedicatedLook=%s)\n", name, numAxes, numButtons, numHats, dedicatedLook);
	} else {
		common->Printf("controller: opened SDL joystick (axes=%d, buttons=%d, hats=%d, dedicatedLook=%s)\n", numAxes, numButtons, numHats, dedicatedLook);
	}
	return true;
}

static void SDL3_OpenFirstController(void) {
	if (!in_joystick.GetBool() || s_sdlGamepad || s_sdlJoystick) {
		return;
	}

	int gamepadCount = 0;
	SDL_JoystickID *gamepads = SDL_GetGamepads(&gamepadCount);
	if (gamepads) {
		for (int i = 0; i < gamepadCount; ++i) {
			if (SDL3_OpenGamepad(gamepads[i])) {
				break;
			}
		}
		SDL_free(gamepads);
	}

	if (s_sdlGamepad) {
		return;
	}

	int joystickCount = 0;
	SDL_JoystickID *joysticks = SDL_GetJoysticks(&joystickCount);
	if (joysticks) {
		for (int i = 0; i < joystickCount; ++i) {
			if (SDL3_OpenJoystick(joysticks[i])) {
				break;
			}
		}
		SDL_free(joysticks);
	}
}

static const char *SDL3_PowerStateName(SDL_PowerState state) {
	switch (state) {
		case SDL_POWERSTATE_ERROR: return "error";
		case SDL_POWERSTATE_UNKNOWN: return "unknown";
		case SDL_POWERSTATE_ON_BATTERY: return "on-battery";
		case SDL_POWERSTATE_NO_BATTERY: return "no-battery";
		case SDL_POWERSTATE_CHARGING: return "charging";
		case SDL_POWERSTATE_CHARGED: return "charged";
		default: return "unrecognized";
	}
}

static const char *SDL3_JoystickTypeName(SDL_JoystickType type) {
	switch (type) {
		case SDL_JOYSTICK_TYPE_UNKNOWN: return "unknown";
		case SDL_JOYSTICK_TYPE_GAMEPAD: return "gamepad";
		case SDL_JOYSTICK_TYPE_WHEEL: return "wheel";
		case SDL_JOYSTICK_TYPE_ARCADE_STICK: return "arcade-stick";
		case SDL_JOYSTICK_TYPE_FLIGHT_STICK: return "flight-stick";
		case SDL_JOYSTICK_TYPE_DANCE_PAD: return "dance-pad";
		case SDL_JOYSTICK_TYPE_GUITAR: return "guitar";
		case SDL_JOYSTICK_TYPE_DRUM_KIT: return "drum-kit";
		case SDL_JOYSTICK_TYPE_ARCADE_PAD: return "arcade-pad";
		case SDL_JOYSTICK_TYPE_THROTTLE: return "throttle";
		case SDL_JOYSTICK_TYPE_COUNT: return "count";
		default: return "unrecognized";
	}
}

static const char *SDL3_NonEmptyString(const char *value) {
	return (value != NULL && value[0] != '\0') ? value : "<unreported>";
}

static void SDL3_GUIDToText(SDL_GUID guid, char *buffer, int bufferSize) {
	if (buffer == NULL || bufferSize <= 0) {
		return;
	}
	buffer[0] = '\0';
	SDL_GUIDToString(guid, buffer, bufferSize);
	if (buffer[0] == '\0') {
		idStr::snPrintf(buffer, bufferSize, "<unreported>");
	}
}

static void SDL3_PrintGamepadSensorLine(SDL_Gamepad *pad, SDL_SensorType sensor, const char *label) {
	const bool hasSensor = SDL_GamepadHasSensor(pad, sensor);
	if (!hasSensor) {
		common->Printf("    %s: no\n", label);
		return;
	}

	const bool enabled = SDL_GamepadSensorEnabled(pad, sensor);
	const float dataRate = SDL_GetGamepadSensorDataRate(pad, sensor);
	if (dataRate > 0.0f) {
		common->Printf("    %s: yes enabled=%s rate=%.1f Hz\n", label, enabled ? "yes" : "no", dataRate);
	} else {
		common->Printf("    %s: yes enabled=%s rate=unreported\n", label, enabled ? "yes" : "no");
	}
}

static void SDL3_PrintControllerPowerLine(const char *prefix, SDL_PowerState state, int percent) {
	if (percent >= 0) {
		common->Printf("%s%s %d%%\n", prefix, SDL3_PowerStateName(state), percent);
	} else {
		common->Printf("%s%s\n", prefix, SDL3_PowerStateName(state));
	}
}

static void SDL3_PrintActiveGamepadDetails(void) {
	if (!s_sdlGamepad) {
		common->Printf("  active gamepad: none\n");
		return;
	}

	const SDL_GamepadType type = SDL_GetGamepadType(s_sdlGamepad);
	const SDL_GamepadType realType = SDL_GetRealGamepadType(s_sdlGamepad);
	const char *typeName = SDL_GetGamepadStringForType(type);
	const char *realTypeName = SDL_GetGamepadStringForType(realType);
	SDL_Joystick *joystick = SDL_GetGamepadJoystick(s_sdlGamepad);
	SDL_GUID padGuid = {};
	if (joystick != NULL) {
		padGuid = SDL_GetJoystickGUID(joystick);
	}
	char guid[64];
	SDL3_GUIDToText(padGuid, guid, sizeof(guid));

	common->Printf("  active gamepad: id=%u name=%s\n", static_cast<unsigned int>(s_sdlGamepadId), SDL3_NonEmptyString(SDL_GetGamepadName(s_sdlGamepad)));
	common->Printf("    path: %s\n", SDL3_NonEmptyString(SDL_GetGamepadPath(s_sdlGamepad)));
	common->Printf("    serial: %s\n", SDL3_NonEmptyString(SDL_GetGamepadSerial(s_sdlGamepad)));
	common->Printf("    guid: %s\n", guid);
	common->Printf("    type: %s realType=%s steamHandle=%s\n",
		SDL3_NonEmptyString(typeName),
		SDL3_NonEmptyString(realTypeName),
		SDL_GetGamepadSteamHandle(s_sdlGamepad) != 0 ? "yes" : "no");

	int powerPercent = -1;
	const SDL_PowerState powerState = SDL_GetGamepadPowerInfo(s_sdlGamepad, &powerPercent);
	SDL3_PrintControllerPowerLine("    power: ", powerState, powerPercent);

	const int touchpadCount = SDL_GetNumGamepadTouchpads(s_sdlGamepad);
	common->Printf("    touchpads: %d\n", touchpadCount);
	for (int i = 0; i < touchpadCount; ++i) {
		common->Printf("      [%d] fingers=%d\n", i, SDL_GetNumGamepadTouchpadFingers(s_sdlGamepad, i));
	}

	SDL3_PrintGamepadSensorLine(s_sdlGamepad, SDL_SENSOR_GYRO, "gyro");
	SDL3_PrintGamepadSensorLine(s_sdlGamepad, SDL_SENSOR_ACCEL, "accelerometer");
	common->Printf("    gyro route: cvar=%s enabled=%s sensitivity=%.3f deadZone=%.3f yawAxis=%d pitchAxis=%d invertYaw=%s invertPitch=%s\n",
		in_gyro.GetBool() ? "on" : "off",
		s_gamepadGyroEnabled ? "yes" : "no",
		in_gyroSensitivity.GetFloat(),
		in_gyroDeadZone.GetFloat(),
		SDL3_ClampSensorAxisIndex(in_gyroYawAxis.GetInteger()),
		SDL3_ClampSensorAxisIndex(in_gyroPitchAxis.GetInteger()),
		in_gyroInvertYaw.GetBool() ? "yes" : "no",
		in_gyroInvertPitch.GetBool() ? "yes" : "no");
	common->Printf("    touchpad route: mode=%d sensitivity=%.3f trackedFinger=%s\n",
		in_touchpadMode.GetInteger(),
		in_touchpadSensitivity.GetFloat(),
		s_gamepadTouchpadFingerActive ? "yes" : "no");
}

static void SDL3_PrintActiveJoystickDetails(void) {
	if (!s_sdlJoystick) {
		common->Printf("  active joystick: none\n");
		return;
	}

	char guid[64];
	SDL3_GUIDToText(SDL_GetJoystickGUID(s_sdlJoystick), guid, sizeof(guid));

	common->Printf("  active joystick: id=%u name=%s\n", static_cast<unsigned int>(s_sdlJoystickId), SDL3_NonEmptyString(SDL_GetJoystickName(s_sdlJoystick)));
	common->Printf("    path: %s\n", SDL3_NonEmptyString(SDL_GetJoystickPath(s_sdlJoystick)));
	common->Printf("    serial: %s\n", SDL3_NonEmptyString(SDL_GetJoystickSerial(s_sdlJoystick)));
	common->Printf("    guid: %s\n", guid);
	common->Printf("    type: %s axes=%d buttons=%d hats=%d dedicatedLook=%s\n",
		SDL3_JoystickTypeName(SDL_GetJoystickType(s_sdlJoystick)),
		SDL_GetNumJoystickAxes(s_sdlJoystick),
		SDL_GetNumJoystickButtons(s_sdlJoystick),
		SDL_GetNumJoystickHats(s_sdlJoystick),
		SDL3_ShouldUseDedicatedJoystickLookAxes(SDL_GetNumJoystickAxes(s_sdlJoystick)) ? "yes" : "no");

	int powerPercent = -1;
	const SDL_PowerState powerState = SDL_GetJoystickPowerInfo(s_sdlJoystick, &powerPercent);
	SDL3_PrintControllerPowerLine("    power: ", powerState, powerPercent);
}

static void SDL3_ListControllers_f(const idCmdArgs &args) {
	(void)args;

	common->Printf("SDL3 controller diagnostics:\n");
	common->Printf("  in_joystick=%s gamepadSubsystem=%s joystickSubsystem=%s\n",
		in_joystick.GetBool() ? "on" : "off",
		s_sdlGamepadSubsystemActive ? "active" : "inactive",
		s_sdlJoystickSubsystemActive ? "active" : "inactive");
	common->Printf("  Steam Deck profile: %s autoFrameCap=%s deckFrameCap=%d com_maxfps=%d\n",
		SDL3_IsSteamDeckPlatformProfile() ? "yes" : "no",
		com_steamDeckAutoFrameCap.GetBool() ? "on" : "off",
		com_steamDeckFrameCap.GetInteger(),
		com_maxfps.GetInteger());
	common->Printf("  touchscreen route: %s activeFinger=%s\n",
		in_touchscreen.GetBool() ? "on" : "off",
		s_touchscreenFingerActive ? "yes" : "no");
	common->Printf("  rumble: enabled=%s scale=%.3f lowBatteryThreshold=%d lowBatteryScale=%.3f lowBatteryCapActive=%s\n",
		in_joystickRumble.GetBool() ? "yes" : "no",
		in_joystickRumbleScale.GetFloat(),
		in_joystickLowBatteryRumbleThreshold.GetInteger(),
		in_joystickLowBatteryRumbleScale.GetFloat(),
		s_lowBatteryRumbleScaleActive ? "yes" : "no");
	common->Printf("  SDL controller hints: HIDAPI=%s enhancedReports=%s backgroundEvents=%s PS4=%s PS5=%s Steam=%s Switch=%s Switch2=%s Xbox=%s\n",
		SDL3_HintString(SDL_HINT_JOYSTICK_HIDAPI),
		SDL3_HintString(SDL_HINT_JOYSTICK_ENHANCED_REPORTS),
		SDL3_HintString(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS),
		SDL3_HintString(SDL_HINT_JOYSTICK_HIDAPI_PS4),
		SDL3_HintString(SDL_HINT_JOYSTICK_HIDAPI_PS5),
		SDL3_HintString(SDL_HINT_JOYSTICK_HIDAPI_STEAM),
		SDL3_HintString(SDL_HINT_JOYSTICK_HIDAPI_SWITCH),
		SDL3_HintString(SDL_HINT_JOYSTICK_HIDAPI_SWITCH2),
		SDL3_HintString(SDL_HINT_JOYSTICK_HIDAPI_XBOX));
#if defined(OPENQ4_SDL3_LINUX_HOST)
	common->Printf("  SDL Linux controller hints: HIDAPI_UDEV=%s LinuxClassic=%s LinuxDeadzones=%s SteamDeck=%s\n",
		SDL3_HintString(SDL_HINT_HIDAPI_UDEV),
		SDL3_HintString(SDL_HINT_JOYSTICK_LINUX_CLASSIC),
		SDL3_HintString(SDL_HINT_JOYSTICK_LINUX_DEADZONES),
		SDL3_HintString(SDL_HINT_JOYSTICK_HIDAPI_STEAMDECK));
#endif
#if defined(OPENQ4_SDL3_DARWIN_HOST)
	common->Printf("  SDL macOS controller hints: IOKit=%s MFi=%s\n",
		SDL3_HintString(SDL_HINT_JOYSTICK_IOKIT),
		SDL3_HintString(SDL_HINT_JOYSTICK_MFI));
#endif

	SDL3_PrintActiveGamepadDetails();
	SDL3_PrintActiveJoystickDetails();

	int gamepadCount = 0;
	SDL_JoystickID *gamepads = SDL_GetGamepads(&gamepadCount);
	common->Printf("  SDL gamepads (%d):\n", gamepadCount);
	if (gamepads != NULL) {
		for (int i = 0; i < gamepadCount; ++i) {
			const SDL_JoystickID id = gamepads[i];
			const char *typeName = SDL_GetGamepadStringForType(SDL_GetGamepadTypeForID(id));
			const char *realTypeName = SDL_GetGamepadStringForType(SDL_GetRealGamepadTypeForID(id));
			common->Printf("    [%d] id=%u%s name=%s type=%s realType=%s path=%s\n",
				i,
				static_cast<unsigned int>(id),
				(id == s_sdlGamepadId) ? " active" : "",
				SDL3_NonEmptyString(SDL_GetGamepadNameForID(id)),
				SDL3_NonEmptyString(typeName),
				SDL3_NonEmptyString(realTypeName),
				SDL3_NonEmptyString(SDL_GetGamepadPathForID(id)));
		}
		SDL_free(gamepads);
	}

	int joystickCount = 0;
	SDL_JoystickID *joysticks = SDL_GetJoysticks(&joystickCount);
	common->Printf("  SDL joysticks (%d):\n", joystickCount);
	if (joysticks != NULL) {
		for (int i = 0; i < joystickCount; ++i) {
			const SDL_JoystickID id = joysticks[i];
			common->Printf("    [%d] id=%u%s gamepad=%s name=%s type=%s path=%s\n",
				i,
				static_cast<unsigned int>(id),
				(id == s_sdlJoystickId) ? " active" : "",
				SDL_IsGamepad(id) ? "yes" : "no",
				SDL3_NonEmptyString(SDL_GetJoystickNameForID(id)),
				SDL3_JoystickTypeName(SDL_GetJoystickTypeForID(id)),
				SDL3_NonEmptyString(SDL_GetJoystickPathForID(id)));
		}
		SDL_free(joysticks);
	}
}

static void SDL3_InitControllerSubsystems(void) {
	SDL3_SetControllerHintDefaults();

	if (!in_joystick.GetBool()) {
		SDL3_CloseGamepad(Sys_Milliseconds());
		SDL3_CloseJoystick(Sys_Milliseconds());
		SDL3_ClearControllerTrackingState();
		return;
	}

	if (!s_sdlGamepadSubsystemActive) {
		if (SDL_InitSubSystem(SDL_INIT_GAMEPAD)) {
			s_sdlGamepadSubsystemActive = true;
			SDL_SetGamepadEventsEnabled(true);
		} else {
			common->Printf("SDL3: could not initialize gamepad subsystem: %s\n", SDL_GetError());
		}
	}

	if (!s_sdlJoystickSubsystemActive) {
		if (SDL_InitSubSystem(SDL_INIT_JOYSTICK)) {
			s_sdlJoystickSubsystemActive = true;
			SDL_SetJoystickEventsEnabled(true);
		} else {
			common->Printf("SDL3: could not initialize joystick subsystem: %s\n", SDL_GetError());
		}
	}

	SDL3_OpenFirstController();
}

static void SDL3_ShutdownControllerSubsystems(void) {
	SDL3_CloseGamepad(Sys_Milliseconds());
	SDL3_CloseJoystick(Sys_Milliseconds());

	if (s_sdlGamepadSubsystemActive) {
		SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
		s_sdlGamepadSubsystemActive = false;
	}
	if (s_sdlJoystickSubsystemActive) {
		SDL_QuitSubSystem(SDL_INIT_JOYSTICK);
		s_sdlJoystickSubsystemActive = false;
	}

	SDL3_ClearControllerTrackingState();
}

static int SDL3_MapPhysicalScancode(SDL_Scancode scancode) {
	switch (scancode) {
		case SDL_SCANCODE_ESCAPE: return K_ESCAPE;
		case SDL_SCANCODE_RETURN: return K_ENTER;
		case SDL_SCANCODE_TAB: return K_TAB;
		case SDL_SCANCODE_BACKSPACE: return K_BACKSPACE;
		case SDL_SCANCODE_SPACE: return K_SPACE;

		case SDL_SCANCODE_LCTRL:
		case SDL_SCANCODE_RCTRL: return K_CTRL;
		case SDL_SCANCODE_LSHIFT:
		case SDL_SCANCODE_RSHIFT: return K_SHIFT;
		case SDL_SCANCODE_LALT: return K_ALT;
		case SDL_SCANCODE_RALT: return s_rightAltKey;
		case SDL_SCANCODE_LGUI: return K_LWIN;
		case SDL_SCANCODE_RGUI: return K_RWIN;
		case SDL_SCANCODE_APPLICATION: return K_MENU;
		case SDL_SCANCODE_MENU: return K_MENU;
		case SDL_SCANCODE_POWER: return K_POWER;

		case SDL_SCANCODE_CAPSLOCK: return K_CAPSLOCK;
		case SDL_SCANCODE_SCROLLLOCK: return K_SCROLL;
		case SDL_SCANCODE_PAUSE: return K_PAUSE;
		case SDL_SCANCODE_PRINTSCREEN: return K_PRINT_SCR;

		case SDL_SCANCODE_UP: return K_UPARROW;
		case SDL_SCANCODE_DOWN: return K_DOWNARROW;
		case SDL_SCANCODE_LEFT: return K_LEFTARROW;
		case SDL_SCANCODE_RIGHT: return K_RIGHTARROW;
		case SDL_SCANCODE_INSERT: return K_INS;
		case SDL_SCANCODE_DELETE: return K_DEL;
		case SDL_SCANCODE_HOME: return K_HOME;
		case SDL_SCANCODE_END: return K_END;
		case SDL_SCANCODE_PAGEUP: return K_PGUP;
		case SDL_SCANCODE_PAGEDOWN: return K_PGDN;

		case SDL_SCANCODE_F1: return K_F1;
		case SDL_SCANCODE_F2: return K_F2;
		case SDL_SCANCODE_F3: return K_F3;
		case SDL_SCANCODE_F4: return K_F4;
		case SDL_SCANCODE_F5: return K_F5;
		case SDL_SCANCODE_F6: return K_F6;
		case SDL_SCANCODE_F7: return K_F7;
		case SDL_SCANCODE_F8: return K_F8;
		case SDL_SCANCODE_F9: return K_F9;
		case SDL_SCANCODE_F10: return K_F10;
		case SDL_SCANCODE_F11: return K_F11;
		case SDL_SCANCODE_F12: return K_F12;
		case SDL_SCANCODE_F13: return K_F13;
		case SDL_SCANCODE_F14: return K_F14;
		case SDL_SCANCODE_F15: return K_F15;

		case SDL_SCANCODE_NUMLOCKCLEAR: return K_KP_NUMLOCK;
		case SDL_SCANCODE_KP_DIVIDE: return K_KP_SLASH;
		case SDL_SCANCODE_KP_MULTIPLY: return K_KP_STAR;
		case SDL_SCANCODE_KP_MINUS: return K_KP_MINUS;
		case SDL_SCANCODE_KP_PLUS: return K_KP_PLUS;
		case SDL_SCANCODE_KP_ENTER: return K_KP_ENTER;
		case SDL_SCANCODE_RETURN2: return K_KP_ENTER;
		case SDL_SCANCODE_KP_1: return K_KP_END;
		case SDL_SCANCODE_KP_2: return K_KP_DOWNARROW;
		case SDL_SCANCODE_KP_3: return K_KP_PGDN;
		case SDL_SCANCODE_KP_4: return K_KP_LEFTARROW;
		case SDL_SCANCODE_KP_5: return K_KP_5;
		case SDL_SCANCODE_KP_6: return K_KP_RIGHTARROW;
		case SDL_SCANCODE_KP_7: return K_KP_HOME;
		case SDL_SCANCODE_KP_8: return K_KP_UPARROW;
		case SDL_SCANCODE_KP_9: return K_KP_PGUP;
		case SDL_SCANCODE_KP_0: return K_KP_INS;
		case SDL_SCANCODE_KP_PERIOD: return K_KP_DEL;
		case SDL_SCANCODE_KP_COMMA: return ',';
		case SDL_SCANCODE_KP_EQUALS: return K_KP_EQUALS;
		case SDL_SCANCODE_KP_EQUALSAS400: return K_KP_EQUALS;

		case SDL_SCANCODE_A: return 'a';
		case SDL_SCANCODE_B: return 'b';
		case SDL_SCANCODE_C: return 'c';
		case SDL_SCANCODE_D: return 'd';
		case SDL_SCANCODE_E: return 'e';
		case SDL_SCANCODE_F: return 'f';
		case SDL_SCANCODE_G: return 'g';
		case SDL_SCANCODE_H: return 'h';
		case SDL_SCANCODE_I: return 'i';
		case SDL_SCANCODE_J: return 'j';
		case SDL_SCANCODE_K: return 'k';
		case SDL_SCANCODE_L: return 'l';
		case SDL_SCANCODE_M: return 'm';
		case SDL_SCANCODE_N: return 'n';
		case SDL_SCANCODE_O: return 'o';
		case SDL_SCANCODE_P: return 'p';
		case SDL_SCANCODE_Q: return 'q';
		case SDL_SCANCODE_R: return 'r';
		case SDL_SCANCODE_S: return 's';
		case SDL_SCANCODE_T: return 't';
		case SDL_SCANCODE_U: return 'u';
		case SDL_SCANCODE_V: return 'v';
		case SDL_SCANCODE_W: return 'w';
		case SDL_SCANCODE_X: return 'x';
		case SDL_SCANCODE_Y: return 'y';
		case SDL_SCANCODE_Z: return 'z';

		case SDL_SCANCODE_1: return '1';
		case SDL_SCANCODE_2: return '2';
		case SDL_SCANCODE_3: return '3';
		case SDL_SCANCODE_4: return '4';
		case SDL_SCANCODE_5: return '5';
		case SDL_SCANCODE_6: return '6';
		case SDL_SCANCODE_7: return '7';
		case SDL_SCANCODE_8: return '8';
		case SDL_SCANCODE_9: return '9';
		case SDL_SCANCODE_0: return '0';

		case SDL_SCANCODE_MINUS: return '-';
		case SDL_SCANCODE_EQUALS: return '=';
		case SDL_SCANCODE_LEFTBRACKET: return '[';
		case SDL_SCANCODE_RIGHTBRACKET: return ']';
		case SDL_SCANCODE_BACKSLASH:
		case SDL_SCANCODE_NONUSHASH:
		case SDL_SCANCODE_NONUSBACKSLASH: return '\\';
		case SDL_SCANCODE_SEMICOLON: return ';';
		case SDL_SCANCODE_APOSTROPHE: return '\'';
		case SDL_SCANCODE_GRAVE: return '`';
		case SDL_SCANCODE_COMMA: return ',';
		case SDL_SCANCODE_PERIOD: return '.';
		case SDL_SCANCODE_SLASH: return '/';
		default: return 0;
	}
}

static int SDL3_MapKeycode(SDL_Keycode keycode) {
	switch (keycode) {
		case SDLK_ESCAPE: return K_ESCAPE;
		case SDLK_RETURN: return K_ENTER;
		case SDLK_TAB: return K_TAB;
		case SDLK_BACKSPACE: return K_BACKSPACE;
		case SDLK_SPACE: return K_SPACE;

		case SDLK_LCTRL:
		case SDLK_RCTRL: return K_CTRL;
		case SDLK_LSHIFT:
		case SDLK_RSHIFT: return K_SHIFT;
		case SDLK_LALT: return K_ALT;
		case SDLK_RALT: return s_rightAltKey;
		case SDLK_LGUI: return K_LWIN;
		case SDLK_RGUI: return K_RWIN;
		case SDLK_APPLICATION:
		case SDLK_MENU: return K_MENU;
		case SDLK_POWER: return K_POWER;

		case SDLK_CAPSLOCK: return K_CAPSLOCK;
		case SDLK_SCROLLLOCK: return K_SCROLL;
		case SDLK_PAUSE: return K_PAUSE;
		case SDLK_PRINTSCREEN: return K_PRINT_SCR;

		case SDLK_UP: return K_UPARROW;
		case SDLK_DOWN: return K_DOWNARROW;
		case SDLK_LEFT: return K_LEFTARROW;
		case SDLK_RIGHT: return K_RIGHTARROW;
		case SDLK_INSERT: return K_INS;
		case SDLK_DELETE: return K_DEL;
		case SDLK_HOME: return K_HOME;
		case SDLK_END: return K_END;
		case SDLK_PAGEUP: return K_PGUP;
		case SDLK_PAGEDOWN: return K_PGDN;

		case SDLK_F1: return K_F1;
		case SDLK_F2: return K_F2;
		case SDLK_F3: return K_F3;
		case SDLK_F4: return K_F4;
		case SDLK_F5: return K_F5;
		case SDLK_F6: return K_F6;
		case SDLK_F7: return K_F7;
		case SDLK_F8: return K_F8;
		case SDLK_F9: return K_F9;
		case SDLK_F10: return K_F10;
		case SDLK_F11: return K_F11;
		case SDLK_F12: return K_F12;
		case SDLK_F13: return K_F13;
		case SDLK_F14: return K_F14;
		case SDLK_F15: return K_F15;

		case SDLK_NUMLOCKCLEAR: return K_KP_NUMLOCK;
		case SDLK_KP_DIVIDE: return K_KP_SLASH;
		case SDLK_KP_MULTIPLY: return K_KP_STAR;
		case SDLK_KP_MINUS: return K_KP_MINUS;
		case SDLK_KP_PLUS: return K_KP_PLUS;
		case SDLK_KP_ENTER:
		case SDLK_RETURN2: return K_KP_ENTER;
		case SDLK_KP_1: return K_KP_END;
		case SDLK_KP_2: return K_KP_DOWNARROW;
		case SDLK_KP_3: return K_KP_PGDN;
		case SDLK_KP_4: return K_KP_LEFTARROW;
		case SDLK_KP_5: return K_KP_5;
		case SDLK_KP_6: return K_KP_RIGHTARROW;
		case SDLK_KP_7: return K_KP_HOME;
		case SDLK_KP_8: return K_KP_UPARROW;
		case SDLK_KP_9: return K_KP_PGUP;
		case SDLK_KP_0: return K_KP_INS;
		case SDLK_KP_PERIOD: return K_KP_DEL;
		case SDLK_KP_COMMA: return ',';
		case SDLK_KP_EQUALS:
		case SDLK_KP_EQUALSAS400: return K_KP_EQUALS;
		default:
			break;
	}

	if ((keycode & (SDLK_SCANCODE_MASK | SDLK_EXTENDED_MASK)) != 0) {
		return 0;
	}

	if (keycode > 0 && keycode < K_BACKSPACE) {
		return static_cast<int>(keycode);
	}

	// K_BACKSPACE..K_LAST_KEY is the engine's special-key space (mouse, joystick,
	// function keys, ...). Only the Latin-1 characters with dedicated keyNum_t
	// slots map through by character; other layout-specific keycodes fall back
	// to the caller's physical scancode mapping so they stay bindable without
	// colliding with special keys.
	switch (keycode) {
		case K_INVERTED_EXCLAMATION:
		case K_SUPERSCRIPT_TWO:
		case K_ACUTE_ACCENT:
		case K_MASCULINE_ORDINATOR:
		case K_GRAVE_A:
		case K_CEDILLA_C:
		case K_GRAVE_E:
		case K_GRAVE_I:
		case K_TILDE_N:
		case K_GRAVE_O:
		case K_GRAVE_U:
			return static_cast<int>(keycode);
		default:
			return 0;
	}
}

static int SDL3_MapScancode(SDL_Scancode scancode) {
	const SDL_Keycode translatedKeycode = SDL_GetKeyFromScancode(scancode, SDL_KMOD_NONE, false);
	const int translatedKey = SDL3_MapKeycode(translatedKeycode);
	if (translatedKey != 0) {
		return translatedKey;
	}

	return SDL3_MapPhysicalScancode(scancode);
}

static int SDL3_MapControlChar(int key, bool down, SDL_Keymod modState) {
	if (!down) {
		return 0;
	}

	// Keep SDL text handling aligned with legacy WM_CHAR behavior for control keys.
	switch (key) {
		case K_BACKSPACE: return '\b';
		case K_TAB: return '\t';
		case K_ENTER:
		case K_KP_ENTER: return '\r';
		default:
			break;
	}

	if ((modState & SDL_KMOD_CTRL) != 0 && (modState & SDL_KMOD_ALT) == 0) {
		if (key >= 'a' && key <= 'z') {
			return (key - 'a') + 1;
		}
	}

	return 0;
}

static int SDL3_MapMouseButton(Uint8 button) {
	switch (button) {
		case SDL_BUTTON_LEFT: return K_MOUSE1;
		case SDL_BUTTON_RIGHT: return K_MOUSE2;
		case SDL_BUTTON_MIDDLE: return K_MOUSE3;
		case SDL_BUTTON_X1: return K_MOUSE4;
		case SDL_BUTTON_X2: return K_MOUSE5;
		default:
			if (button >= 6 && button <= 8) {
				return K_MOUSE1 + (button - 1);
			}
			return 0;
	}
}

typedef struct {
	SDL_DisplayID id;
	int index;
} sdl3DisplaySelection_t;

static int SDL3_FindDisplayIndex(const SDL_DisplayID *displays, int displayCount, SDL_DisplayID displayId) {
	if (displayId == 0 || displays == NULL || displayCount <= 0) {
		return -1;
	}

	for (int i = 0; i < displayCount; ++i) {
		if (displays[i] == displayId) {
			return i;
		}
	}

	return -1;
}

static const char *SDL3_DisplayOrientationName(SDL_DisplayOrientation orientation) {
	switch (orientation) {
		case SDL_ORIENTATION_LANDSCAPE: return "landscape";
		case SDL_ORIENTATION_LANDSCAPE_FLIPPED: return "landscape-flipped";
		case SDL_ORIENTATION_PORTRAIT: return "portrait";
		case SDL_ORIENTATION_PORTRAIT_FLIPPED: return "portrait-flipped";
		default: return "unknown";
	}
}

static void SDL3_FormatDisplayMode(const SDL_DisplayMode *mode, char *buffer, int bufferSize) {
	if (buffer == NULL || bufferSize <= 0) {
		return;
	}
	if (mode == NULL || mode->w <= 0 || mode->h <= 0) {
		idStr::snPrintf(buffer, bufferSize, "unavailable");
		return;
	}
	if (mode->refresh_rate_numerator > 0 && mode->refresh_rate_denominator > 0) {
		idStr::snPrintf(buffer, bufferSize, "%dx%d @ %.2f Hz (%d/%d) pd=%.2f",
			mode->w,
			mode->h,
			mode->refresh_rate,
			mode->refresh_rate_numerator,
			mode->refresh_rate_denominator,
			mode->pixel_density);
	} else {
		idStr::snPrintf(buffer, bufferSize, "%dx%d @ %.2f Hz pd=%.2f",
			mode->w,
			mode->h,
			mode->refresh_rate,
			mode->pixel_density);
	}
}

static void SDL3_PrintDisplayList(void) {
	int displayCount = 0;
	SDL_DisplayID *displays = SDL_GetDisplays(&displayCount);
	if (displays == NULL || displayCount <= 0) {
		common->Printf("SDL3: no displays detected (%s)\n", SDL_GetError());
		if (displays != NULL) {
			SDL_free(displays);
		}
		return;
	}

	const SDL_DisplayID primaryDisplay = SDL_GetPrimaryDisplay();
	common->Printf("SDL3: detected %d display(s):\n", displayCount);

	for (int i = 0; i < displayCount; ++i) {
		const SDL_DisplayID display = displays[i];
		const char *name = SDL_GetDisplayName(display);
		if (name == NULL || name[0] == '\0') {
			name = "<unnamed>";
		}

		const float contentScale = SDL_GetDisplayContentScale(display);
		const SDL_DisplayOrientation naturalOrientation = SDL_GetNaturalDisplayOrientation(display);
		const SDL_DisplayOrientation currentOrientation = SDL_GetCurrentDisplayOrientation(display);
		const SDL_DisplayMode *desktopMode = SDL_GetDesktopDisplayMode(display);
		const SDL_DisplayMode *currentMode = SDL_GetCurrentDisplayMode(display);
		char desktopModeText[96];
		char currentModeText[96];
		SDL3_FormatDisplayMode(desktopMode, desktopModeText, sizeof(desktopModeText));
		SDL3_FormatDisplayMode(currentMode, currentModeText, sizeof(currentModeText));

		SDL_Rect bounds;
		if (SDL_GetDisplayBounds(display, &bounds)) {
			common->Printf("  [%d]%s %s (%dx%d @ %d,%d, contentScale %.2f, orientation %s/%s, desktop %s, current %s)\n",
				i,
				(display == primaryDisplay) ? " *" : "",
				name,
				bounds.w,
				bounds.h,
				bounds.x,
				bounds.y,
				contentScale,
				SDL3_DisplayOrientationName(naturalOrientation),
				SDL3_DisplayOrientationName(currentOrientation),
				desktopModeText,
				currentModeText);
		} else {
			common->Printf("  [%d]%s %s (bounds unavailable: %s, contentScale %.2f, orientation %s/%s, desktop %s, current %s)\n",
				i,
				(display == primaryDisplay) ? " *" : "",
				name,
				SDL_GetError(),
				contentScale,
				SDL3_DisplayOrientationName(naturalOrientation),
				SDL3_DisplayOrientationName(currentOrientation),
				desktopModeText,
				currentModeText);
		}
	}

	SDL_free(displays);
}

static sdl3DisplaySelection_t SDL3_ResolveTargetDisplay(bool warnOnInvalidScreenIndex) {
	sdl3DisplaySelection_t selection;
	selection.id = 0;
	selection.index = -1;

	const int requestedScreen = r_screen.GetInteger();
	const SDL_DisplayID currentDisplay = s_sdlWindow ? SDL_GetDisplayForWindow(s_sdlWindow) : 0;
	const SDL_DisplayID primaryDisplay = SDL_GetPrimaryDisplay();

	int displayCount = 0;
	SDL_DisplayID *displays = SDL_GetDisplays(&displayCount);

	if (displays != NULL && displayCount > 0) {
		if (requestedScreen >= 0) {
			if (requestedScreen < displayCount) {
				selection.id = displays[requestedScreen];
				selection.index = requestedScreen;
			} else {
				selection.id = (primaryDisplay != 0) ? primaryDisplay : displays[0];
				selection.index = SDL3_FindDisplayIndex(displays, displayCount, selection.id);
				if (warnOnInvalidScreenIndex) {
					common->Printf(
						"SDL3: r_screen %d is out of range for %d display(s); using display %d.\n",
						requestedScreen, displayCount, selection.index);
				}
			}
		} else {
			if (currentDisplay != 0) {
				selection.id = currentDisplay;
				selection.index = SDL3_FindDisplayIndex(displays, displayCount, currentDisplay);
			}

			if (selection.id == 0) {
				selection.id = (primaryDisplay != 0) ? primaryDisplay : displays[0];
				selection.index = SDL3_FindDisplayIndex(displays, displayCount, selection.id);
			}
		}
	} else {
		if (warnOnInvalidScreenIndex) {
			common->Printf("SDL3: could not enumerate displays; falling back to primary display.\n");
		}
		selection.id = primaryDisplay;
		selection.index = -1;
	}

	if (displays != NULL) {
		SDL_free(displays);
	}

	return selection;
}

#if defined(OPENQ4_SDL3_POSIX_HOST)
static bool SDL3_QueryDesktopResolution(int *width, int *height, const char *platformName) {
	if (width == NULL || height == NULL) {
		return false;
	}

	const sdl3DisplaySelection_t selectedDisplay = SDL3_ResolveTargetDisplay(false);
	const SDL_DisplayID display = (selectedDisplay.id != 0) ? selectedDisplay.id : SDL_GetPrimaryDisplay();
	const char *logPrefix = (platformName != NULL && platformName[0] != '\0') ? platformName : "SDL3";

	const SDL_DisplayMode *desktopMode = SDL_GetDesktopDisplayMode(display);
	if (desktopMode != NULL && desktopMode->w > 0 && desktopMode->h > 0) {
		*width = desktopMode->w;
		*height = desktopMode->h;
		return true;
	}

	const SDL_DisplayMode *currentMode = SDL_GetCurrentDisplayMode(display);
	if (currentMode != NULL && currentMode->w > 0 && currentMode->h > 0) {
		common->DPrintf("%s: desktop display mode unavailable, using current display mode %dx%d\n",
			logPrefix, currentMode->w, currentMode->h);
		*width = currentMode->w;
		*height = currentMode->h;
		return true;
	}

	SDL_Rect bounds;
	if (display != 0 && SDL_GetDisplayBounds(display, &bounds) && bounds.w > 0 && bounds.h > 0) {
		common->DPrintf("%s: desktop display mode unavailable, using display bounds %dx%d\n",
			logPrefix, bounds.w, bounds.h);
		*width = bounds.w;
		*height = bounds.h;
		return true;
	}

	return false;
}
#endif

static bool SDL3_IsSteamDeckPlatformProfile(void) {
	if (cvarSystem == NULL || !cvarSystem->IsInitialized()) {
		return false;
	}
	const char *profile = cvarSystem->GetCVarString("com_platformProfile");
	return profile != NULL && idStr::Icmp(profile, "steamdeck") == 0;
}

static int SDL3_DetectSteamDeckFrameCap(void) {
	const int configuredCap = com_steamDeckFrameCap.GetInteger();
	if (configuredCap > 0) {
		return idMath::ClampInt(1, 1000, configuredCap);
	}

	int detectedRefresh = 60;
	const sdl3DisplaySelection_t selectedDisplay = SDL3_ResolveTargetDisplay(false);
	SDL_DisplayID display = selectedDisplay.id;
	if (display == 0) {
		display = SDL_GetPrimaryDisplay();
	}

	if (display != 0) {
		const SDL_DisplayMode *desktopMode = SDL_GetDesktopDisplayMode(display);
		if (desktopMode != NULL && desktopMode->refresh_rate > 1.0f) {
			detectedRefresh = static_cast<int>(desktopMode->refresh_rate + 0.5f);
		}
	}

	return idMath::ClampInt(40, 90, detectedRefresh);
}

static void SDL3_ApplySteamDeckPerformanceDefaults(void) {
	static const int OPENQ4_GLOBAL_DEFAULT_MAXFPS = 240;

	if (!SDL3_IsSteamDeckPlatformProfile() || !com_steamDeckAutoFrameCap.GetBool()) {
		return;
	}

	const int currentMaxFps = com_maxfps.GetInteger();
	if (currentMaxFps != OPENQ4_GLOBAL_DEFAULT_MAXFPS) {
		common->Printf("Steam Deck performance defaults: preserving configured com_maxfps %d.\n", currentMaxFps);
		return;
	}

	const int deckFrameCap = SDL3_DetectSteamDeckFrameCap();
	com_maxfps.SetInteger(deckFrameCap);
	common->Printf("Steam Deck performance defaults: setting com_maxfps to %d.\n", deckFrameCap);
}

struct sdl3WindowBorders_t {
	int top;
	int left;
	int bottom;
	int right;
};

static int SDL3_SaturateWindowCoordinate(int64_t value) {
	if (value <= static_cast<int64_t>(idMath::INT_MIN)) {
		return idMath::INT_MIN;
	}
	if (value >= static_cast<int64_t>(idMath::INT_MAX)) {
		return idMath::INT_MAX;
	}
	return static_cast<int>(value);
}

static sdl3WindowBorders_t SDL3_GetWindowBorders(void) {
	sdl3WindowBorders_t borders = { 0, 0, 0, 0 };
	if (s_sdlWindow == NULL) {
		return borders;
	}
	if ((SDL_GetWindowFlags(s_sdlWindow) & SDL_WINDOW_BORDERLESS) != 0) {
		return borders;
	}

	bool haveBorders = SDL_GetWindowBordersSize(
		s_sdlWindow, &borders.top, &borders.left, &borders.bottom, &borders.right);
#if defined(OPENQ4_SDL3_DARWIN_HOST)
	if (!haveBorders) {
		haveBorders = Sys_SDL_GetNativeWindowBorders(
			s_sdlWindow, &borders.top, &borders.left, &borders.bottom, &borders.right);
		if (haveBorders) {
			SDL_ClearError();
		}
	}
#endif
	(void)haveBorders;

	// A backend that cannot report decorations is required by SDL to return
	// zeroes. Also reject negative or implausibly large values before any frame
	// arithmetic, since these dimensions come from the window server.
	borders.top = idMath::ClampInt(0, 16384, borders.top);
	borders.left = idMath::ClampInt(0, 16384, borders.left);
	borders.bottom = idMath::ClampInt(0, 16384, borders.bottom);
	borders.right = idMath::ClampInt(0, 16384, borders.right);
	return borders;
}

static void SDL3_ClientOriginToFrameOrigin(int clientX, int clientY, int &frameX, int &frameY) {
	const sdl3WindowBorders_t borders = SDL3_GetWindowBorders();
	frameX = SDL3_SaturateWindowCoordinate(static_cast<int64_t>(clientX) - borders.left);
	frameY = SDL3_SaturateWindowCoordinate(static_cast<int64_t>(clientY) - borders.top);
}

static void SDL3_FrameOriginToClientOrigin(int frameX, int frameY, int &clientX, int &clientY) {
	const sdl3WindowBorders_t borders = SDL3_GetWindowBorders();
	clientX = SDL3_SaturateWindowCoordinate(static_cast<int64_t>(frameX) + borders.left);
	clientY = SDL3_SaturateWindowCoordinate(static_cast<int64_t>(frameY) + borders.top);
}

static bool SDL3_GetDisplayWindowedPlacementBounds(SDL_DisplayID display, SDL_Rect &bounds) {
	if (display != 0 && SDL_GetDisplayUsableBounds(display, &bounds)) {
		return true;
	}
	if (display != 0 && SDL_GetDisplayBounds(display, &bounds)) {
		return true;
	}
	return false;
}

static bool SDL3_GetVirtualDisplayBounds(SDL_Rect &bounds) {
	int displayCount = 0;
	SDL_DisplayID *displays = SDL_GetDisplays(&displayCount);
	if (displays == NULL || displayCount <= 0) {
		if (displays != NULL) {
			SDL_free(displays);
		}
		return false;
	}

	bool hasBounds = false;
	for (int i = 0; i < displayCount; ++i) {
		SDL_Rect displayBounds;
		if (!SDL_GetDisplayBounds(displays[i], &displayBounds)) {
			continue;
		}

		if (!hasBounds) {
			bounds = displayBounds;
			hasBounds = true;
			continue;
		}

		const int left = (displayBounds.x < bounds.x) ? displayBounds.x : bounds.x;
		const int top = (displayBounds.y < bounds.y) ? displayBounds.y : bounds.y;
		const int right = ((displayBounds.x + displayBounds.w) > (bounds.x + bounds.w)) ? (displayBounds.x + displayBounds.w) : (bounds.x + bounds.w);
		const int bottom = ((displayBounds.y + displayBounds.h) > (bounds.y + bounds.h)) ? (displayBounds.y + displayBounds.h) : (bounds.y + bounds.h);

		bounds.x = left;
		bounds.y = top;
		bounds.w = right - left;
		bounds.h = bottom - top;
	}

	SDL_free(displays);
	return hasBounds;
}

static bool SDL3_RectsOverlap(const SDL_Rect &a, const SDL_Rect &b) {
	const int64_t aRight = static_cast<int64_t>(a.x) + a.w;
	const int64_t aBottom = static_cast<int64_t>(a.y) + a.h;
	const int64_t bRight = static_cast<int64_t>(b.x) + b.w;
	const int64_t bBottom = static_cast<int64_t>(b.y) + b.h;
	const int64_t overlapLeft = (a.x > b.x) ? a.x : b.x;
	const int64_t overlapTop = (a.y > b.y) ? a.y : b.y;
	const int64_t overlapRight = (aRight < bRight) ? aRight : bRight;
	const int64_t overlapBottom = (aBottom < bBottom) ? aBottom : bBottom;
	return overlapRight > overlapLeft && overlapBottom > overlapTop;
}

static bool SDL3_WindowRectIntersectsAnyDisplay(int frameX, int frameY, int clientWidth, int clientHeight) {
	if (clientWidth <= 0 || clientHeight <= 0) {
		return false;
	}

	const sdl3WindowBorders_t borders = SDL3_GetWindowBorders();
	SDL_Rect windowRect;
	windowRect.x = frameX;
	windowRect.y = frameY;
	windowRect.w = SDL3_SaturateWindowCoordinate(
		static_cast<int64_t>(clientWidth) + borders.left + borders.right);
	windowRect.h = SDL3_SaturateWindowCoordinate(
		static_cast<int64_t>(clientHeight) + borders.top + borders.bottom);

	int displayCount = 0;
	SDL_DisplayID *displays = SDL_GetDisplays(&displayCount);
	if (displays == NULL || displayCount <= 0) {
		if (displays != NULL) {
			SDL_free(displays);
		}
		return false;
	}

	bool intersects = false;
	for (int i = 0; i < displayCount; ++i) {
		SDL_Rect displayBounds;
		if (SDL_GetDisplayBounds(displays[i], &displayBounds) && SDL3_RectsOverlap(windowRect, displayBounds)) {
			intersects = true;
			break;
		}
	}

	SDL_free(displays);
	return intersects;
}

static int SDL3_ClampWindowDimension(int value, int minValue, int maxValue) {
	if (maxValue <= 0) {
		return 1;
	}
	const int effectiveMin = (minValue > maxValue) ? maxValue : minValue;
	return idMath::ClampInt(effectiveMin, maxValue, value);
}

static void SDL3_ConstrainWindowRectToBounds(int &frameX, int &frameY, int &clientWidth, int &clientHeight, const SDL_Rect &bounds, bool recenterIfOutside) {
	const sdl3WindowBorders_t borders = SDL3_GetWindowBorders();
	const int64_t availableClientWidth = static_cast<int64_t>(bounds.w) - borders.left - borders.right;
	const int64_t availableClientHeight = static_cast<int64_t>(bounds.h) - borders.top - borders.bottom;
	const int maxClientWidth = availableClientWidth > 1 ? static_cast<int>(availableClientWidth) : 1;
	const int maxClientHeight = availableClientHeight > 1 ? static_cast<int>(availableClientHeight) : 1;
	clientWidth = SDL3_ClampWindowDimension(clientWidth, 320, maxClientWidth);
	clientHeight = SDL3_ClampWindowDimension(clientHeight, 240, maxClientHeight);

	const int64_t frameWidth = static_cast<int64_t>(clientWidth) + borders.left + borders.right;
	const int64_t frameHeight = static_cast<int64_t>(clientHeight) + borders.top + borders.bottom;
	const int maxX = SDL3_SaturateWindowCoordinate(
		static_cast<int64_t>(bounds.x) + bounds.w - frameWidth);
	const int maxY = SDL3_SaturateWindowCoordinate(
		static_cast<int64_t>(bounds.y) + bounds.h - frameHeight);

	if (maxX < bounds.x) {
		frameX = bounds.x;
	} else {
		if (recenterIfOutside && (frameX < bounds.x || frameX > maxX)) {
			frameX = SDL3_SaturateWindowCoordinate(
				static_cast<int64_t>(bounds.x) + (static_cast<int64_t>(bounds.w) - frameWidth) / 2);
		}
		frameX = idMath::ClampInt(bounds.x, maxX, frameX);
	}

	if (maxY < bounds.y) {
		frameY = bounds.y;
	} else {
		if (recenterIfOutside && (frameY < bounds.y || frameY > maxY)) {
			frameY = SDL3_SaturateWindowCoordinate(
				static_cast<int64_t>(bounds.y) + (static_cast<int64_t>(bounds.h) - frameHeight) / 2);
		}
		frameY = idMath::ClampInt(bounds.y, maxY, frameY);
	}
}

static void SDL3_GetWindowPositionOnDisplay(SDL_DisplayID display, int width, int height, int &targetX, int &targetY) {
	targetX = win32.win_xpos.GetInteger();
	targetY = win32.win_ypos.GetInteger();

	if (display == 0) {
		return;
	}

	SDL_Rect bounds;
	if (!SDL_GetDisplayBounds(display, &bounds)) {
		return;
	}

	if (r_screen.GetInteger() >= 0) {
		const int maxX = bounds.x + bounds.w - width;
		const int maxY = bounds.y + bounds.h - height;

		if (maxX < bounds.x) {
			targetX = bounds.x;
		} else {
			targetX = idMath::ClampInt(bounds.x, maxX, targetX);
		}

		if (maxY < bounds.y) {
			targetY = bounds.y;
		} else {
			targetY = idMath::ClampInt(bounds.y, maxY, targetY);
		}
	}
}

static bool SDL3_SetWindowPositionCompat(int frameX, int frameY, SDL_DisplayID display, bool centerOnWayland, const char *description) {
	if (!s_sdlWindow) {
		return false;
	}

	if (SDL3_IsNativeWaylandVideoDriver()) {
		if (!centerOnWayland || display == 0) {
			return true;
		}

		const int centered = SDL_WINDOWPOS_CENTERED_DISPLAY(display);
		if (!SDL_SetWindowPosition(s_sdlWindow, centered, centered)) {
			common->DPrintf("SDL3: native Wayland could not apply %s display placement: %s\n",
				description != NULL ? description : "window", SDL_GetError());
			return false;
		}
		return true;
	}

	int clientX = frameX;
	int clientY = frameY;
	SDL3_FrameOriginToClientOrigin(frameX, frameY, clientX, clientY);
	if (!SDL_SetWindowPosition(s_sdlWindow, clientX, clientY)) {
		common->Printf("SDL3: failed to %s: %s\n",
			description != NULL ? description : "move window", SDL_GetError());
		return false;
	}

	return true;
}

static void SDL3_ListDisplays_f(const idCmdArgs &args) {
	(void)args;
	SDL3_PrintDisplayList();

	const sdl3DisplaySelection_t selectedDisplay = SDL3_ResolveTargetDisplay(false);
	const char *name = selectedDisplay.id ? SDL_GetDisplayName(selectedDisplay.id) : NULL;
	if (name == NULL || name[0] == '\0') {
		name = "<unnamed>";
	}

	common->Printf("SDL3: r_screen = %d, selected display = %d (%s)\n",
		r_screen.GetInteger(),
		selectedDisplay.index,
		name);
}

static void SDL3_ListDisplayModes_f(const idCmdArgs &args) {
	sdl3DisplaySelection_t selectedDisplay = SDL3_ResolveTargetDisplay(true);

	if (args.Argc() > 1 && idStr::IsNumeric(args.Argv(1))) {
		const int requestedIndex = atoi(args.Argv(1));
		int displayCount = 0;
		SDL_DisplayID *displays = SDL_GetDisplays(&displayCount);
		if (displays != NULL && requestedIndex >= 0 && requestedIndex < displayCount) {
			selectedDisplay.id = displays[requestedIndex];
			selectedDisplay.index = requestedIndex;
		}
		if (displays != NULL) {
			SDL_free(displays);
		}
	}

	SDL_DisplayID display = selectedDisplay.id;
	if (display == 0) {
		display = SDL_GetPrimaryDisplay();
		selectedDisplay.index = -1;
	}

	if (display == 0) {
		common->Printf("SDL3: no valid display found for mode listing.\n");
		return;
	}

	const char *name = SDL_GetDisplayName(display);
	if (name == NULL || name[0] == '\0') {
		name = "<unnamed>";
	}

	int modeCount = 0;
	SDL_DisplayMode **modes = SDL_GetFullscreenDisplayModes(display, &modeCount);
	if (modes == NULL || modeCount <= 0) {
		common->Printf("SDL3: no fullscreen modes reported for display %d (%s): %s\n",
			selectedDisplay.index, name, SDL_GetError());
		if (modes != NULL) {
			SDL_free(modes);
		}
		return;
	}

	const float contentScale = SDL_GetDisplayContentScale(display);
	common->Printf("SDL3: fullscreen modes for display %d (%s, contentScale %.2f):\n",
		selectedDisplay.index,
		name,
		contentScale);
	for (int i = 0; i < modeCount; ++i) {
		const SDL_DisplayMode *mode = modes[i];
		if (mode == NULL || mode->w <= 0 || mode->h <= 0) {
			continue;
		}
		char modeText[96];
		SDL3_FormatDisplayMode(mode, modeText, sizeof(modeText));
		common->Printf("  [%d] %s\n", i, modeText);
	}

	SDL_free(modes);
}

static float SDL3_FindNearestCommonAspectRatio(int width, int height) {
	if (width <= 0 || height <= 0) {
		return 0.0f;
	}

	static const float commonAspectRatios[] = {
		5.0f / 4.0f,
		4.0f / 3.0f,
		3.0f / 2.0f,
		16.0f / 10.0f,
		16.0f / 9.0f,
		21.0f / 9.0f,
		32.0f / 9.0f
	};

	const float currentAspect = static_cast<float>(width) / static_cast<float>(height);
	float nearestAspect = commonAspectRatios[0];
	float nearestDelta = fabsf(currentAspect - nearestAspect);

	for (int i = 1; i < static_cast<int>(sizeof(commonAspectRatios) / sizeof(commonAspectRatios[0])); ++i) {
		const float candidate = commonAspectRatios[i];
		const float delta = fabsf(currentAspect - candidate);
		if (delta < nearestDelta) {
			nearestDelta = delta;
			nearestAspect = candidate;
		}
	}

	return nearestAspect;
}

static void SDL3_DisableWindowAspectSnap(void) {
	if (!s_windowAspectSnapActive) {
		return;
	}

	if (s_sdlWindow && !SDL_SetWindowAspectRatio(s_sdlWindow, 0.0f, 0.0f)) {
		common->Printf("SDL3: failed to clear aspect ratio lock: %s\n", SDL_GetError());
	}

	s_windowAspectSnapActive = false;
	s_windowAspectSnapRatio = 0.0f;
}

static void SDL3_EnableWindowAspectSnapFromCurrentSize(void) {
	if (!s_sdlWindow || win32.cdsFullscreen || r_borderless.GetBool()) {
		return;
	}

	int width = 0;
	int height = 0;
	if (!SDL_GetWindowSize(s_sdlWindow, &width, &height) || width <= 0 || height <= 0) {
		return;
	}

	const float targetAspect = SDL3_FindNearestCommonAspectRatio(width, height);
	if (targetAspect <= 0.0f) {
		return;
	}

	if (s_windowAspectSnapActive && fabsf(s_windowAspectSnapRatio - targetAspect) < 0.0005f) {
		return;
	}

	if (!SDL_SetWindowAspectRatio(s_sdlWindow, targetAspect, targetAspect)) {
		common->Printf("SDL3: failed to set aspect ratio lock: %s\n", SDL_GetError());
		return;
	}

	s_windowAspectSnapActive = true;
	s_windowAspectSnapRatio = targetAspect;
}

static void SDL3_UpdateWindowAspectSnap(bool sawResizeEvent) {
	if (!s_sdlWindow || win32.cdsFullscreen || r_borderless.GetBool()) {
		SDL3_DisableWindowAspectSnap();
		return;
	}

	const bool shiftDown = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
	if (!shiftDown) {
		SDL3_DisableWindowAspectSnap();
		return;
	}

	if (sawResizeEvent) {
		SDL3_EnableWindowAspectSnapFromCurrentSize();
	}
}

static SDL_DisplayID SDL3_ResolveViewportDisplay(void) {
	const sdl3DisplaySelection_t selectedDisplay = SDL3_ResolveTargetDisplay(false);
	if (r_screen.GetInteger() >= 0 && selectedDisplay.id != 0) {
		return selectedDisplay.id;
	}

	const SDL_DisplayID primaryDisplay = SDL_GetPrimaryDisplay();
	if (primaryDisplay != 0) {
		return primaryDisplay;
	}

	return selectedDisplay.id;
}

static int SDL3_ClampViewportPixel(double value, int minValue, int maxValue) {
	if (!std::isfinite(value) || value <= static_cast<double>(minValue)) {
		return minValue;
	}
	if (value >= static_cast<double>(maxValue)) {
		return maxValue;
	}
	return static_cast<int>(value);
}

static void SDL3_SetUIViewport( int x, int y, int width, int height ) {
	engineWindowState.uiViewportX = x;
	engineWindowState.uiViewportY = y;
	engineWindowState.uiViewportWidth = width;
	engineWindowState.uiViewportHeight = height;
#ifndef OPENQ4_RENDERER_MODULE_ONLY
	// mirrored for the statically linked renderer; module renderers poll
	// this state through the window services each present
	glConfig.uiViewportX = x;
	glConfig.uiViewportY = y;
	glConfig.uiViewportWidth = width;
	glConfig.uiViewportHeight = height;
#endif
}

static void SDL3_SetVidSize( int width, int height ) {
	engineWindowState.vidWidth = width;
	engineWindowState.vidHeight = height;
#ifndef OPENQ4_RENDERER_MODULE_ONLY
	glConfig.vidWidth = width;
	glConfig.vidHeight = height;
#endif
}

static void SDL3_SetFullscreenState( bool fullscreen ) {
	engineWindowState.isFullscreen = fullscreen;
#ifndef OPENQ4_RENDERER_MODULE_ONLY
	glConfig.isFullscreen = fullscreen;
#endif
}

static void SDL3_UpdateDisplayViewport(SDL_DisplayID display, int windowX, int windowY, int windowWidth, int windowHeight, int pixelWidth, int pixelHeight) {
	SDL3_SetUIViewport( 0, 0, pixelWidth, pixelHeight );

	if (windowWidth <= 0 || windowHeight <= 0 || pixelWidth <= 0 || pixelHeight <= 0) {
		return;
	}

	if (display == 0) {
		return;
	}

	SDL_Rect displayBounds;
	if (!SDL_GetDisplayBounds(display, &displayBounds)) {
		return;
	}

	const int64_t windowLeft = static_cast<int64_t>(windowX);
	const int64_t windowTop = static_cast<int64_t>(windowY);
	const int64_t windowRight = windowLeft + static_cast<int64_t>(windowWidth);
	const int64_t windowBottom = windowTop + static_cast<int64_t>(windowHeight);
	const int64_t displayLeft = static_cast<int64_t>(displayBounds.x);
	const int64_t displayTop = static_cast<int64_t>(displayBounds.y);
	const int64_t displayRight = displayLeft + static_cast<int64_t>(displayBounds.w);
	const int64_t displayBottom = displayTop + static_cast<int64_t>(displayBounds.h);

	const int64_t overlapLeft = (windowLeft > displayLeft) ? windowLeft : displayLeft;
	const int64_t overlapTop = (windowTop > displayTop) ? windowTop : displayTop;
	const int64_t overlapRight = (windowRight < displayRight) ? windowRight : displayRight;
	const int64_t overlapBottom = (windowBottom < displayBottom) ? windowBottom : displayBottom;

	if (overlapRight <= overlapLeft || overlapBottom <= overlapTop) {
		return;
	}

	const double pixelScaleX = static_cast<double>(pixelWidth) / static_cast<double>(windowWidth);
	const double pixelScaleY = static_cast<double>(pixelHeight) / static_cast<double>(windowHeight);
	const double localLeft = static_cast<double>(overlapLeft - windowLeft);
	const double localTop = static_cast<double>(overlapTop - windowTop);
	const double localRight = static_cast<double>(overlapRight - windowLeft);
	const double localBottom = static_cast<double>(overlapBottom - windowTop);

	int pixelLeft = SDL3_ClampViewportPixel(std::floor(localLeft * pixelScaleX), 0, pixelWidth);
	int pixelTop = SDL3_ClampViewportPixel(std::floor(localTop * pixelScaleY), 0, pixelHeight);
	int pixelRight = SDL3_ClampViewportPixel(std::ceil(localRight * pixelScaleX), pixelLeft, pixelWidth);
	int pixelBottom = SDL3_ClampViewportPixel(std::ceil(localBottom * pixelScaleY), pixelTop, pixelHeight);

	pixelLeft = idMath::ClampInt(0, pixelWidth, pixelLeft);
	pixelTop = idMath::ClampInt(0, pixelHeight, pixelTop);
	pixelRight = idMath::ClampInt(pixelLeft, pixelWidth, pixelRight);
	pixelBottom = idMath::ClampInt(pixelTop, pixelHeight, pixelBottom);

	const int viewportWidth = pixelRight - pixelLeft;
	const int viewportHeight = pixelBottom - pixelTop;
	if (viewportWidth <= 0 || viewportHeight <= 0) {
		return;
	}

	SDL3_SetUIViewport( pixelLeft, pixelTop, viewportWidth, viewportHeight );
}

static void SDL3_UpdateFullWindowViewport(int pixelWidth, int pixelHeight) {
	SDL3_SetUIViewport( 0, 0, pixelWidth, pixelHeight );
}

static void SDL3_RecordWindowedPlacement(int x, int y, int width, int height) {
	if (Sys_WindowPlacementLeaseActive() || width <= 0 || height <= 0) {
		return;
	}

	s_windowedPlacement.x = x;
	s_windowedPlacement.y = y;
	s_windowedPlacement.width = width;
	s_windowedPlacement.height = height;
	s_windowedPlacement.valid = true;
}

static void SDL3_SnapshotCurrentWindowedPlacement(void) {
	if (!s_sdlWindow || win32.cdsFullscreen || !SDL3_UseAbsoluteWindowPlacement()) {
		return;
	}
	const SDL_WindowFlags windowFlags = SDL_GetWindowFlags(s_sdlWindow);
	if ((windowFlags & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_BORDERLESS)) != 0) {
		// OS-initiated fullscreen (not tracked by win32.cdsFullscreen) must
		// never be recorded as a windowed placement.
		return;
	}

	int x = 0;
	int y = 0;
	int width = 0;
	int height = 0;
	if (SDL_GetWindowPosition(s_sdlWindow, &x, &y) && SDL_GetWindowSize(s_sdlWindow, &width, &height)) {
		int frameX = x;
		int frameY = y;
		SDL3_ClientOriginToFrameOrigin(x, y, frameX, frameY);
		SDL3_RecordWindowedPlacement(frameX, frameY, width, height);
	}
}

static void SDL3_RefreshWindowPlacement(void) {
	if (!s_sdlWindow) {
		return;
	}

	int x = 0;
	int y = 0;
	int width = 0;
	int height = 0;
	int pixelWidth = 0;
	int pixelHeight = 0;
	const SDL_WindowFlags windowFlags = SDL_GetWindowFlags(s_sdlWindow);
	const bool windowIsHidden = (windowFlags & SDL_WINDOW_HIDDEN) != 0;
	// Check the actual SDL fullscreen flag as well as the engine's own
	// bookkeeping: OS-initiated transitions (macOS green traffic-light
	// button) never set win32.cdsFullscreen, and persisting the fullscreen
	// space's forced move/resize would corrupt the saved windowed placement.
	const bool windowIsFullscreen = (windowFlags & SDL_WINDOW_FULLSCREEN) != 0;
	const bool canPersistWindowedPlacement = !windowIsHidden && !windowIsFullscreen && !win32.cdsFullscreen && !s_screenParmTransitionActive && !Sys_WindowPlacementLeaseActive();
	const bool isWindowedResizable = (windowFlags & SDL_WINDOW_BORDERLESS) == 0;

	const bool haveWindowPosition = SDL_GetWindowPosition(s_sdlWindow, &x, &y);
	int frameX = x;
	int frameY = y;
	if (haveWindowPosition && SDL3_UseAbsoluteWindowPlacement()) {
		SDL3_ClientOriginToFrameOrigin(x, y, frameX, frameY);
	}
	if (haveWindowPosition && canPersistWindowedPlacement && SDL3_UseAbsoluteWindowPlacement()) {
		win32.win_xpos.SetInteger(frameX);
		win32.win_ypos.SetInteger(frameY);
		win32.win_xpos.ClearModified();
		win32.win_ypos.ClearModified();
	}

	const bool haveWindowSize = SDL_GetWindowSize(s_sdlWindow, &width, &height);
	if (haveWindowSize && canPersistWindowedPlacement) {
		if (isWindowedResizable && width > 0 && height > 0) {
			r_windowWidth.SetInteger(width);
			r_windowHeight.SetInteger(height);
			r_windowWidth.ClearModified();
			r_windowHeight.ClearModified();
		}
	}

	if (canPersistWindowedPlacement && isWindowedResizable && haveWindowPosition && haveWindowSize && SDL3_UseAbsoluteWindowPlacement()) {
		SDL3_RecordWindowedPlacement(frameX, frameY, width, height);
	}

	if (!SDL_GetWindowSizeInPixels(s_sdlWindow, &pixelWidth, &pixelHeight)) {
		pixelWidth = width;
		pixelHeight = height;
	}

	if (pixelWidth > 0 && pixelHeight > 0) {
		SDL3_SetVidSize( pixelWidth, pixelHeight );
	}
	const float uiDisplayScale = SDL_GetWindowDisplayScale(s_sdlWindow);
	engineWindowState.displayScale = std::isfinite(uiDisplayScale) && uiDisplayScale > 0 ? uiDisplayScale : 1.0f;
	engineWindowState.pixelDensityX = width > 0 && pixelWidth > 0 ? static_cast<float>(pixelWidth) / width : 1.0f;
	engineWindowState.pixelDensityY = height > 0 && pixelHeight > 0 ? static_cast<float>(pixelHeight) / height : 1.0f;

	if (SDL3_UseAbsoluteWindowPlacement()) {
		SDL3_UpdateDisplayViewport(SDL3_ResolveViewportDisplay(), x, y, width, height, pixelWidth, pixelHeight);
	} else {
		SDL3_UpdateFullWindowViewport(pixelWidth, pixelHeight);
	}
}

static void SDL3_PrintWaylandWindowState(const char *description) {
	if (!s_sdlWindow || !SDL3_IsNativeWaylandVideoDriver()) {
		return;
	}

	int x = 0;
	int y = 0;
	int width = 0;
	int height = 0;
	int pixelWidth = 0;
	int pixelHeight = 0;
	const bool havePosition = SDL_GetWindowPosition(s_sdlWindow, &x, &y);
	const bool haveSize = SDL_GetWindowSize(s_sdlWindow, &width, &height);
	const bool havePixelSize = SDL_GetWindowSizeInPixels(s_sdlWindow, &pixelWidth, &pixelHeight);
	const SDL_WindowFlags flags = SDL_GetWindowFlags(s_sdlWindow);
	const float pixelDensity = SDL_GetWindowPixelDensity(s_sdlWindow);
	const float displayScale = SDL_GetWindowDisplayScale(s_sdlWindow);
	const SDL_DisplayID display = SDL_GetDisplayForWindow(s_sdlWindow);

	int displayIndex = -1;
	int displayCount = 0;
	SDL_DisplayID *displays = SDL_GetDisplays(&displayCount);
	if (displays != NULL) {
		displayIndex = SDL3_FindDisplayIndex(displays, displayCount, display);
		SDL_free(displays);
	}

	const char *displayName = (display != 0) ? SDL_GetDisplayName(display) : NULL;
	if (displayName == NULL || displayName[0] == '\0') {
		displayName = "<unknown>";
	}

	common->Printf(
		"SDL3: native Wayland window state after %s: pos=%s%d,%d size=%s%dx%d pixels=%s%dx%d pixelDensity=%.2f displayScale=%.2f fullscreen=%s display=%d (%s)\n",
		description != NULL ? description : "screen change",
		havePosition ? "" : "<unreported> ",
		x,
		y,
		haveSize ? "" : "<unreported> ",
		width,
		height,
		havePixelSize ? "" : "<unreported> ",
		pixelWidth,
		pixelHeight,
		pixelDensity,
		displayScale,
		(flags & SDL_WINDOW_FULLSCREEN) != 0 ? "yes" : "no",
		displayIndex,
		displayName);
}

#if defined(OPENQ4_SDL3_DARWIN_HOST)
/*
===============
SDL3_FindExactPixelFullscreenMode

Selects the fullscreen mode whose PIXEL dimensions (w/h scaled by
pixel_density) exactly match the requested size. SDL_DisplayMode w/h are in
points, so on Retina displays the native pixel resolutions only appear as
high-density modes that point-based matching would misinterpret (a
1920x1080-point 2x mode is 3840x2160 pixels).
===============
*/
static bool SDL3_FindExactPixelFullscreenMode(SDL_DisplayID display, int pixelWidth, int pixelHeight, float requestedRefresh, SDL_DisplayMode &outMode) {
	int modeCount = 0;
	SDL_DisplayMode **modes = SDL_GetFullscreenDisplayModes(display, &modeCount);
	if (modes == NULL) {
		return false;
	}

	bool found = false;
	float bestScore = 0.0f;
	for (int i = 0; i < modeCount; ++i) {
		const SDL_DisplayMode *candidate = modes[i];
		if (candidate == NULL) {
			continue;
		}
		const float density = candidate->pixel_density > 0.0f ? candidate->pixel_density : 1.0f;
		const int candidatePixelWidth = (int)(candidate->w * density + 0.5f);
		const int candidatePixelHeight = (int)(candidate->h * density + 0.5f);
		if (candidatePixelWidth != pixelWidth || candidatePixelHeight != pixelHeight) {
			continue;
		}
		// Nearest refresh to the request, or the highest available when no
		// specific rate was requested.
		const float score = requestedRefresh > 0.0f
			? -fabsf(candidate->refresh_rate - requestedRefresh)
			: candidate->refresh_rate;
		if (!found || score > bestScore) {
			found = true;
			bestScore = score;
			outMode = *candidate;
		}
	}
	SDL_free(modes);
	return found;
}
#endif

static bool SDL3_LeaveFullscreenAndRestoreDesktopMode(void) {
	if (!s_sdlWindow) {
		return true;
	}

	const SDL_WindowFlags flags = SDL_GetWindowFlags(s_sdlWindow);
	if ((flags & SDL_WINDOW_FULLSCREEN) == 0) {
		return true;
	}

	if (!SDL_SetWindowFullscreenMode(s_sdlWindow, NULL)) {
		common->Printf("SDL3: failed to set desktop fullscreen mode before exit: %s\n", SDL_GetError());
	}

	if (!SDL_SetWindowFullscreen(s_sdlWindow, false)) {
		common->Printf("SDL3: failed to leave fullscreen: %s\n", SDL_GetError());
		return false;
	}

	if (!SDL_SyncWindow(s_sdlWindow)) {
		common->Printf("SDL3: failed to synchronize window after leaving fullscreen: %s\n", SDL_GetError());
	}

	return true;
}

static void SDL3_SyncWindowAfterScreenChange(const char *description) {
	if (!s_sdlWindow) {
		return;
	}

	if (!SDL_SyncWindow(s_sdlWindow)) {
		common->DPrintf("SDL3: failed to synchronize window after %s: %s\n",
			description != NULL ? description : "screen change",
			SDL_GetError());
	}
}

/*
===============
Sys_SDL_EmergencyReleaseGameWindow

Called from the fatal-error console path before it shows the error window.
A fatal error can arrive with the game window still in (exclusive)
fullscreen; without releasing it the user sees a frozen fullscreen frame
instead of the error text. Main-thread callers only.
===============
*/
void Sys_SDL_EmergencyReleaseGameWindow(void) {
	if (!s_sdlWindow) {
		return;
	}
	(void)SDL3_LeaveFullscreenAndRestoreDesktopMode();
	SDL_HideWindow(s_sdlWindow);
}

#if defined(_WIN32)
static void SDL3_MinimizeOnFullscreenFocusLoss(void) {
	if (!s_sdlWindow) {
		return;
	}

	const SDL_WindowFlags flags = SDL_GetWindowFlags(s_sdlWindow);
	if ((flags & SDL_WINDOW_FULLSCREEN) == 0) {
		return;
	}

	if (!SDL_MinimizeWindow(s_sdlWindow)) {
		common->DPrintf("SDL3: failed to minimize fullscreen window on focus loss: %s\n", SDL_GetError());
	}
}
#endif

static bool SDL3_ApplyScreenParms(glimpParms_t parms) {
	if (!s_sdlWindow) {
		return false;
	}

	if (parms.hiddenWindow) {
		parms.fullScreen = false;
		parms.borderless = false;
	}

	s_screenParmTransitionActive = true;

	SDL3_DisableWindowAspectSnap();

	const bool useBorderlessWindow = !parms.fullScreen && parms.borderless;
	bool spanDisplays = (r_multiScreen.GetInteger() == 1);
	const sdl3DisplaySelection_t selectedDisplay = SDL3_ResolveTargetDisplay(true);
	SDL_DisplayID display = selectedDisplay.id;
	if (display == 0) {
		display = SDL_GetPrimaryDisplay();
	}
	bool reapplyDecoratedWindowPlacement = false;
	bool reapplyRecenterIfOutside = false;
	int reapplyFrameX = 0;
	int reapplyFrameY = 0;
	int reapplyClientWidth = 0;
	int reapplyClientHeight = 0;

	if (spanDisplays && SDL3_IsNativeWaylandVideoDriver()) {
		if (!s_waylandSpanWarningLogged) {
			common->Printf(
				"SDL3: native Wayland does not expose absolute multi-display window placement; using the selected display instead of r_multiScreen spanning.\n");
			s_waylandSpanWarningLogged = true;
		}
		spanDisplays = false;
	}

	if (parms.fullScreen) {
		SDL3_SnapshotCurrentWindowedPlacement();

		if (!SDL_SetWindowBordered(s_sdlWindow, true)) {
			common->Printf("SDL3: failed to restore window borders: %s\n", SDL_GetError());
		}

		const bool useDesktopFullscreen = r_fullscreenDesktop.GetBool();
		int targetX = 0;
		int targetY = 0;

		if (useDesktopFullscreen && spanDisplays) {
			if (!SDL_SetWindowFullscreen(s_sdlWindow, false)) {
				common->Printf("SDL3: failed to leave fullscreen before spanned desktop mode: %s\n", SDL_GetError());
			}
			if (!SDL_SetWindowBordered(s_sdlWindow, false)) {
				common->Printf("SDL3: failed to disable window borders for spanned desktop mode: %s\n", SDL_GetError());
			}

			SDL_Rect bounds;
			if (SDL3_GetVirtualDisplayBounds(bounds)) {
				(void)SDL3_SetWindowPositionCompat(bounds.x, bounds.y, display, true, "place spanned desktop window");
				(void)SDL_SetWindowSize(s_sdlWindow, bounds.w, bounds.h);
			} else {
				(void)SDL3_SetWindowPositionCompat(targetX, targetY, display, true, "place desktop window");
				(void)SDL_SetWindowSize(s_sdlWindow, parms.width, parms.height);
			}
		} else if (useDesktopFullscreen) {
			SDL_Rect bounds;
			if (display != 0 && SDL_GetDisplayBounds(display, &bounds)) {
				targetX = bounds.x;
				targetY = bounds.y;
			} else {
				SDL3_GetWindowPositionOnDisplay(display, parms.width, parms.height, targetX, targetY);
			}
			(void)SDL3_SetWindowPositionCompat(targetX, targetY, display, true, "place desktop fullscreen window");

			if (!SDL_SetWindowFullscreenMode(s_sdlWindow, NULL)) {
				common->Printf("SDL3: failed to select desktop fullscreen mode: %s\n", SDL_GetError());
			}
		} else {
			SDL_DisplayMode mode;
			memset(&mode, 0, sizeof(mode));
			const float requestedRefresh = parms.displayHz > 0 ? static_cast<float>(parms.displayHz) : 0.0f;
			bool hasClosestMode = false;
#if defined(OPENQ4_SDL3_DARWIN_HOST)
			// The menu offers pixel resolutions, but Retina displays expose
			// their native pixel sizes only as high-density modes whose w/h
			// are points. Prefer an exact pixel-size match so the requested
			// resolution is honored in pixels; glConfig dimensions stay
			// correct because they are read back via SDL_GetWindowSizeInPixels.
			hasClosestMode = display != 0 &&
				SDL3_FindExactPixelFullscreenMode(display, parms.width, parms.height, requestedRefresh, mode);
#endif
			// Point-size matching over the standard (1x-density) mode list;
			// also the fallback when no exact Retina pixel match exists.
			if (!hasClosestMode) {
				hasClosestMode = display != 0 &&
					SDL_GetClosestFullscreenDisplayMode(display, parms.width, parms.height, requestedRefresh, false, &mode);
			}

			if (hasClosestMode && mode.displayID == 0) {
				mode.displayID = display;
			}

			const int modeWidth = hasClosestMode ? mode.w : parms.width;
			const int modeHeight = hasClosestMode ? mode.h : parms.height;
			SDL3_GetWindowPositionOnDisplay(display, modeWidth, modeHeight, targetX, targetY);
			(void)SDL3_SetWindowPositionCompat(targetX, targetY, display, true, "place fullscreen window");

			if (hasClosestMode && !SDL_SetWindowFullscreenMode(s_sdlWindow, &mode)) {
				common->Printf("SDL3: failed to set fullscreen mode %dx%d@%.2f: %s\n",
					mode.w, mode.h, mode.refresh_rate, SDL_GetError());
				(void)SDL_SetWindowFullscreenMode(s_sdlWindow, NULL);
			} else if (!hasClosestMode) {
				common->Printf("SDL3: no fullscreen mode matched %dx%d @ %.2f Hz on display %d; using desktop mode.\n",
					parms.width, parms.height, requestedRefresh, selectedDisplay.index);
				(void)SDL_SetWindowFullscreenMode(s_sdlWindow, NULL);
			}
		}

		if (!(useDesktopFullscreen && spanDisplays)) {
			if (!SDL_SetWindowFullscreen(s_sdlWindow, true)) {
				common->Printf("SDL3: failed to enter fullscreen: %s\n", SDL_GetError());
				s_screenParmTransitionActive = false;
				return false;
			}
		}
	} else {
		if (!SDL3_LeaveFullscreenAndRestoreDesktopMode()) {
			s_screenParmTransitionActive = false;
			return false;
		}
		(void)SDL_SetWindowFullscreenMode(s_sdlWindow, NULL);

		if (!SDL_SetWindowBordered(s_sdlWindow, !useBorderlessWindow)) {
			common->Printf("SDL3: failed to set border mode: %s\n", SDL_GetError());
		}

		if (useBorderlessWindow) {
			SDL_Rect bounds;
			if (spanDisplays && SDL3_GetVirtualDisplayBounds(bounds)) {
				if (!SDL_SetWindowSize(s_sdlWindow, bounds.w, bounds.h)) {
					common->Printf("SDL3: failed to resize borderless spanned window: %s\n", SDL_GetError());
				}
				(void)SDL3_SetWindowPositionCompat(bounds.x, bounds.y, display, true, "place borderless spanned window");
			} else if (display != 0 && SDL_GetDisplayBounds(display, &bounds)) {
				if (!SDL_SetWindowSize(s_sdlWindow, bounds.w, bounds.h)) {
					common->Printf("SDL3: failed to resize borderless window: %s\n", SDL_GetError());
				}
				(void)SDL3_SetWindowPositionCompat(bounds.x, bounds.y, display, true, "place borderless window");
			} else {
				if (!SDL_SetWindowSize(s_sdlWindow, parms.width, parms.height)) {
					common->Printf("SDL3: failed to resize window: %s\n", SDL_GetError());
				}
			}
		} else {
			int restoredWidth = parms.width;
			int restoredHeight = parms.height;
			int restoredX = win32.win_xpos.GetInteger();
			int restoredY = win32.win_ypos.GetInteger();

			if (s_windowedPlacement.valid) {
				restoredWidth = s_windowedPlacement.width;
				restoredHeight = s_windowedPlacement.height;
				restoredX = s_windowedPlacement.x;
				restoredY = s_windowedPlacement.y;
			}

			const bool needsRecoveryPlacement = !SDL3_WindowRectIntersectsAnyDisplay(restoredX, restoredY, restoredWidth, restoredHeight);
			bool constrainedToDisplay = false;
			if (display != 0) {
				SDL_Rect bounds;
				if (SDL3_GetDisplayWindowedPlacementBounds(display, bounds)) {
					const bool recenterIfOutside = (r_screen.GetInteger() >= 0) || needsRecoveryPlacement;
					SDL3_ConstrainWindowRectToBounds(restoredX, restoredY, restoredWidth, restoredHeight, bounds, recenterIfOutside);
					constrainedToDisplay = true;
				}
			}

			if (!constrainedToDisplay) {
				restoredWidth = idMath::ClampInt(320, 16384, restoredWidth);
				restoredHeight = idMath::ClampInt(240, 16384, restoredHeight);
			}
			reapplyDecoratedWindowPlacement = !parms.hiddenWindow && SDL3_UseAbsoluteWindowPlacement();
			reapplyRecenterIfOutside = (r_screen.GetInteger() >= 0) || needsRecoveryPlacement;
			reapplyFrameX = restoredX;
			reapplyFrameY = restoredY;
			reapplyClientWidth = restoredWidth;
			reapplyClientHeight = restoredHeight;

			if (!SDL_SetWindowSize(s_sdlWindow, restoredWidth, restoredHeight)) {
				common->Printf("SDL3: failed to resize window: %s\n", SDL_GetError());
			}

			int targetX = restoredX;
			int targetY = restoredY;

			(void)SDL3_SetWindowPositionCompat(
				targetX,
				targetY,
				display,
				r_screen.GetInteger() >= 0,
				"move window");
		}
	}

	win32.cdsFullscreen = parms.fullScreen;
	SDL3_SetFullscreenState( parms.fullScreen );

	if (parms.hiddenWindow) {
		if (!SDL_HideWindow(s_sdlWindow)) {
			common->DPrintf("SDL3: failed to hide render window: %s\n", SDL_GetError());
		}
	} else {
		Sys_DestroySplash();
		if (!SDL_ShowWindow(s_sdlWindow)) {
			common->DPrintf("SDL3: failed to show render window: %s\n", SDL_GetError());
		}
	}

	if (!parms.hiddenWindow) {
		SDL3_SyncWindowAfterScreenChange(parms.fullScreen ? "fullscreen change" : "windowed change");
	}

	// Some window servers cannot report decorations until the window has been
	// shown and composited. Repeat decorated placement after the first sync so
	// the requested frame origin and client size are corrected with the final
	// title-bar/border extents before the renderer presents its first frame.
	if (reapplyDecoratedWindowPlacement) {
		SDL_Rect bounds;
		if (display != 0 && SDL3_GetDisplayWindowedPlacementBounds(display, bounds)) {
			SDL3_ConstrainWindowRectToBounds(
				reapplyFrameX,
				reapplyFrameY,
				reapplyClientWidth,
				reapplyClientHeight,
				bounds,
				reapplyRecenterIfOutside);
		}
		if (!SDL_SetWindowSize(s_sdlWindow, reapplyClientWidth, reapplyClientHeight)) {
			common->Printf("SDL3: failed to apply chrome-aware client size: %s\n", SDL_GetError());
		}
		(void)SDL3_SetWindowPositionCompat(
			reapplyFrameX,
			reapplyFrameY,
			display,
			false,
			"apply chrome-aware window placement");
		SDL3_SyncWindowAfterScreenChange("chrome-aware window placement");
	}
	s_screenParmTransitionActive = false;
	SDL3_RefreshWindowPlacement();
	SDL3_PrintWaylandWindowState(parms.fullScreen ? "fullscreen change" : "windowed change");

	return true;
}



static void SDL3_InitDesktopMode(void) {
	const sdl3DisplaySelection_t selectedDisplay = SDL3_ResolveTargetDisplay(false);
	const SDL_DisplayID display = (selectedDisplay.id != 0) ? selectedDisplay.id : SDL_GetPrimaryDisplay();
	const SDL_DisplayMode *desktopMode = SDL_GetDesktopDisplayMode(display);
	if (desktopMode) {
		win32.desktopBitsPixel = SDL_BITSPERPIXEL(desktopMode->format);
		win32.desktopWidth = desktopMode->w;
		win32.desktopHeight = desktopMode->h;
	} else {
		win32.desktopBitsPixel = 32;
		win32.desktopWidth = 1920;
		win32.desktopHeight = 1080;
	}
}

static void SDL3_UpdateNativeWindowHandles(void) {
	SDL3_RetireNativeWindowPublication();
#if defined(OPENQ4_SDL3_POSIX_HOST)
	win32.hWnd = NULL;
	win32.hDC = NULL;
	// the rendering context handle is owned by the context half of the seam
	// (renderer/OpenGL/gl_ContextSDL3.cpp); the old reverse write is gone
	win32.hGLRC = NULL;
#else
	win32.hWnd = NULL;
	win32.hDC = NULL;
	win32.hGLRC = NULL;

	if (!s_sdlWindow) {
		return;
	}

	SDL_PropertiesID props = SDL_GetWindowProperties(s_sdlWindow);
	if (props == 0) {
		return;
	}

	win32.hWnd = (HWND)SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
	win32.hDC = (HDC)SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HDC_POINTER, NULL);
	SDL3_PublishNativeWindowIdentity();
#endif
}

static const char *SDL3_DisplayEventName(SDL_EventType eventType) {
	switch (eventType) {
		case SDL_EVENT_DISPLAY_ORIENTATION: return "orientation changed";
		case SDL_EVENT_DISPLAY_ADDED: return "added";
		case SDL_EVENT_DISPLAY_REMOVED: return "removed";
		case SDL_EVENT_DISPLAY_MOVED: return "moved";
		case SDL_EVENT_DISPLAY_DESKTOP_MODE_CHANGED: return "desktop mode changed";
		case SDL_EVENT_DISPLAY_CURRENT_MODE_CHANGED: return "current mode changed";
		case SDL_EVENT_DISPLAY_CONTENT_SCALE_CHANGED: return "content scale changed";
		case SDL_EVENT_DISPLAY_USABLE_BOUNDS_CHANGED: return "usable bounds changed";
		default: return "state changed";
	}
}

static void SDL3_HandleDisplayEvent(const SDL_DisplayEvent &event) {
	// Display events are process-wide, not associated with one SDL window. A
	// dock/undock, monitor hotplug, mode change, or compositor scale change can
	// therefore arrive without a matching window event. Refresh every derived
	// display/framebuffer value and invalidate logical-to-pixel input routing so
	// the next event cannot use stale monitor or high-DPI coordinates.
	common->Printf(
		"SDL3: display %s (id %u); refreshing desktop, window, and input state.\n",
		SDL3_DisplayEventName(event.type),
		static_cast<unsigned int>(event.displayID));
	SDL3_InitDesktopMode();
	SDL3_RefreshWindowPlacement();
	SDL3_InvalidateMenuMouseRouting();
	SDL3_UpdateCursorVisibility();
	SDL3_UpdateTextInputState();
}

static void SDL3_HandleWindowEvent(const SDL_WindowEvent &event, int eventTime) {
	switch (event.type) {
		case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
			cmdSystem->BufferCommandText(CMD_EXEC_APPEND, "quit\n");
			break;

		case SDL_EVENT_WINDOW_FOCUS_GAINED:
			openq4::NativeInputBeforeWindowChange();
			win32.activeApp = true;
			SDL3_QueueRetainedFocus(true,eventTime);
			s_sdlFocusInputReleased = false;
#if !defined(OPENQ4_SDL3_POSIX_HOST)
			win32.printScreenFocusReleaseUntil = 0;
#endif
			idKeyInput::ClearStates();
			com_editorActive = false;
			s_menuMouseInsideWindow = s_sdlWindow != NULL &&
				(SDL_GetWindowFlags(s_sdlWindow) & SDL_WINDOW_MOUSE_FOCUS) != 0;
			SDL3_InvalidateMenuMouseRouting();
			Sys_GrabMouseCursor(true);
			SDL3_UpdateCursorVisibility();
			if (session != NULL) {
				session->SetPlayingSoundWorld();
			}
			break;

		case SDL_EVENT_WINDOW_FOCUS_LOST:
			openq4::NativeInputBeforeWindowChange();
			win32.activeApp = false;
			win32.movingWindow = false;
			s_menuMouseInsideWindow = false;
			SDL3_InvalidateMenuMouseRouting();
			SDL3_ReleaseFocusInputState(eventTime);
			SDL3_UpdateCursorVisibility();
#if defined(_WIN32)
			// Match the classic Win32 behavior Warfork uses: minimizing true fullscreen on
			// deactivation lets system UI like Win+Shift+S / Print Screen take foreground cleanly.
			SDL3_MinimizeOnFullscreenFocusLoss();
#endif
			if (session != NULL) {
				session->SetPlayingSoundWorld();
			}
			break;

		case SDL_EVENT_WINDOW_MOVED:
			SDL3_RefreshWindowPlacement();
			SDL3_InvalidateMenuMouseRouting();
			break;

		case SDL_EVENT_WINDOW_ENTER_FULLSCREEN:
		case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN:
			// OS-initiated transitions (macOS green traffic-light button,
			// Window menu) never pass through the engine's screen-parm path;
			// keep derived state consistent with the actual window so
			// placement persistence and cursor routing behave.
			SDL3_SetFullscreenState( (event.type == SDL_EVENT_WINDOW_ENTER_FULLSCREEN) || win32.cdsFullscreen );
			SDL3_RefreshWindowPlacement();
			SDL3_InvalidateMenuMouseRouting();
			SDL3_UpdateCursorVisibility();
			break;

		case SDL_EVENT_WINDOW_DISPLAY_CHANGED:
		case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
			SDL3_InitDesktopMode();
			SDL3_RefreshWindowPlacement();
			SDL3_InvalidateMenuMouseRouting();
			SDL3_UpdateCursorVisibility();
			break;

		case SDL_EVENT_WINDOW_HIDDEN:
		case SDL_EVENT_WINDOW_MINIMIZED:
			openq4::NativeInputBeforeWindowChange();
			win32.activeApp = false;
			s_menuMouseInsideWindow = false;
			SDL3_InvalidateMenuMouseRouting();
			SDL3_ReleaseFocusInputState(eventTime);
			SDL3_UpdateCursorVisibility();
			if (session != NULL) {
				session->SetPlayingSoundWorld();
			}
			break;

		case SDL_EVENT_WINDOW_SHOWN:
		case SDL_EVENT_WINDOW_RESTORED:
			openq4::NativeInputBeforeWindowChange();
			win32.activeApp = Sys_SDL_IsGameWindowFocused();
			SDL3_QueueRetainedFocus(win32.activeApp,eventTime);
			s_menuMouseInsideWindow = s_sdlWindow != NULL &&
				(SDL_GetWindowFlags(s_sdlWindow) & SDL_WINDOW_MOUSE_FOCUS) != 0;
			SDL3_RefreshWindowPlacement();
			SDL3_InvalidateMenuMouseRouting();
			if (win32.activeApp) {
				s_sdlFocusInputReleased = false;
				Sys_GrabMouseCursor(true);
			}
			SDL3_UpdateCursorVisibility();
			if (session != NULL) {
				session->SetPlayingSoundWorld();
			}
			break;

		case SDL_EVENT_WINDOW_RESIZED:
		case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
			// Query the drawable size directly to avoid stale/asymmetric event payloads.
			SDL3_RefreshWindowPlacement();
			SDL3_InvalidateMenuMouseRouting();
			break;

		case SDL_EVENT_WINDOW_MOUSE_ENTER:
			s_menuMouseInsideWindow = true;
			if (SDL3_ShouldRouteMenuMouse() && !SDL3_IsMouseCaptured()) {
				SDL3_SyncSystemMouseToActiveCursor();
			}
			SDL3_UpdateCursorVisibility();
			break;

		case SDL_EVENT_WINDOW_MOUSE_LEAVE:
			{
				retainedUIInput_t leave; leave.kind = retainedUIInput_t::POINTER_LEAVE;
				RetainedUI_QueueInput(leave,eventTime);
			}
			s_menuMouseInsideWindow = false;
			SDL3_ResetMenuMouseTracking();
			SDL3_UpdateCursorVisibility();
			break;

		default:
			break;
	}

	SDL3_UpdateTextInputState();
	(void)eventTime;
}

static bool SDL3_EventWindowID(const SDL_Event &event, SDL_WindowID &windowID) {
	if (event.type >= SDL_EVENT_WINDOW_FIRST && event.type <= SDL_EVENT_WINDOW_LAST) {
		windowID = event.window.windowID;
		return true;
	}

	switch (event.type) {
		case SDL_EVENT_KEY_DOWN:
		case SDL_EVENT_KEY_UP:
			windowID = event.key.windowID;
			return true;
		case SDL_EVENT_TEXT_EDITING:
			windowID = event.edit.windowID;
			return true;
		case SDL_EVENT_TEXT_EDITING_CANDIDATES:
			windowID = event.edit_candidates.windowID;
			return true;
		case SDL_EVENT_TEXT_INPUT:
			windowID = event.text.windowID;
			return true;
		case SDL_EVENT_MOUSE_MOTION:
			windowID = event.motion.windowID;
			return true;
		case SDL_EVENT_MOUSE_BUTTON_DOWN:
		case SDL_EVENT_MOUSE_BUTTON_UP:
			windowID = event.button.windowID;
			return true;
		case SDL_EVENT_MOUSE_WHEEL:
			windowID = event.wheel.windowID;
			return true;
		case SDL_EVENT_FINGER_DOWN:
		case SDL_EVENT_FINGER_UP:
		case SDL_EVENT_FINGER_MOTION:
		case SDL_EVENT_FINGER_CANCELED:
			windowID = event.tfinger.windowID;
			return true;
		default:
			windowID = 0;
			return false;
	}
}

static bool SDL3_EventTargetsGameWindow(const SDL_Event &event) {
	SDL_WindowID eventWindowID = 0;
	if (!SDL3_EventWindowID(event, eventWindowID)) {
		return true;
	}

	const SDL_WindowID gameWindowID = s_sdlWindow != NULL ? SDL_GetWindowID(s_sdlWindow) : 0;
	return gameWindowID != 0 && eventWindowID == gameWindowID;
}

bool Sys_SDL_PumpEvents(void) {
    if (NativeInput_OwnsPump()) return true; // Before housekeeping and both legacy Poll loops.
#if defined(OPENQ4_SDL3_POSIX_HOST)
	if (!Posix_IsMainThread()) {
		// The async timer thread reaches this through Sys_PollMouseInputEvents
		// when com_asyncInput is enabled. SDL event pumping and window APIs
		// are main-thread-only on macOS (and not thread-safe anywhere), so
		// off-thread callers skip the pump; the input queues they drain are
		// already mutex-protected and the main loop keeps the pump fresh.
		return false;
	}
	const bool gameWindowReady = s_sdlVideoReferenceHeld && s_sdlWindow;
	if (!gameWindowReady) {
		SDL3_ProcessPendingLifecycleEvents(Sys_Milliseconds());
		if (!Posix_ConsoleNeedsEventPump()) {
			return false;
		}

		SDL_Event event;
		while (SDL_PollEvent(&event)) {
			if (Posix_ConsoleProcessEvent(&event)) {
				continue;
			}
			if (event.type == SDL_EVENT_QUIT) {
				common->Printf("SDL3: SDL_EVENT_QUIT received (console pump)\n");
				cmdSystem->BufferCommandText(CMD_EXEC_APPEND, "quit\n");
			}
		}
		return true;
	}
#else
	if (!s_sdlVideoReferenceHeld || !s_sdlWindow) {
		return false;
	}
#endif

	SDL3_ProcessPendingLifecycleEvents(Sys_Milliseconds());
	SDL3_UpdateTextInputState();

	if (in_joystick.IsModified()) {
		if (in_joystick.GetBool()) {
			SDL3_InitControllerSubsystems();
		} else {
			SDL3_ShutdownControllerSubsystems();
		}
		in_joystick.ClearModified();
	}
	if (in_joystickRumble.IsModified() || in_joystickRumbleScale.IsModified() ||
			in_joystickLowBatteryRumbleThreshold.IsModified() || in_joystickLowBatteryRumbleScale.IsModified()) {
		if (!in_joystickRumble.GetBool() || in_joystickRumbleScale.GetFloat() <= 0.0f) {
			SDL3_StopControllerRumble();
		}
		s_lowBatteryRumbleScaleActive = false;
		s_lowBatteryRumblePercent = -1;
		in_joystickRumble.ClearModified();
		in_joystickRumbleScale.ClearModified();
		in_joystickLowBatteryRumbleThreshold.ClearModified();
		in_joystickLowBatteryRumbleScale.ClearModified();
	}
	if (in_gyro.IsModified() || in_gyroSensitivity.IsModified() || in_gyroDeadZone.IsModified() ||
			in_gyroYawAxis.IsModified() || in_gyroPitchAxis.IsModified() ||
			in_gyroInvertYaw.IsModified() || in_gyroInvertPitch.IsModified() ||
			in_touchpadMode.IsModified() || in_touchpadSensitivity.IsModified() ||
			in_touchscreen.IsModified()) {
		SDL3_UpdateGamepadSensorState(false);
		s_gamepadGyroLastTimestamp = 0;
		s_gamepadGyroRemainderX = 0.0f;
		s_gamepadGyroRemainderY = 0.0f;
		s_gamepadTouchpadFingerActive = false;
		s_gamepadTouchpadRemainderX = 0.0f;
		s_gamepadTouchpadRemainderY = 0.0f;
		if (!in_touchscreen.GetBool()) {
			s_touchscreenFingerActive = false;
		}
		in_gyro.ClearModified();
		in_gyroSensitivity.ClearModified();
		in_gyroDeadZone.ClearModified();
		in_gyroYawAxis.ClearModified();
		in_gyroPitchAxis.ClearModified();
		in_gyroInvertYaw.ClearModified();
		in_gyroInvertPitch.ClearModified();
		in_touchpadMode.ClearModified();
		in_touchpadSensitivity.ClearModified();
		in_touchscreen.ClearModified();
	}
	if (in_joystickDeadZone.IsModified() || in_joystickTriggerThreshold.IsModified() ||
			in_joystickLookSensitivity.IsModified() || in_joystickLookCurve.IsModified() ||
			in_joystickMoveCurve.IsModified() || in_joystickInvertLook.IsModified() ||
			in_joystickSouthpaw.IsModified() || in_joystickUseDedicatedLookAxes.IsModified() ||
			in_joystickMoveAxisX.IsModified() || in_joystickMoveAxisY.IsModified() ||
			in_joystickLookAxisX.IsModified() || in_joystickLookAxisY.IsModified() ||
			in_joystickUpAxis.IsModified() || in_joystickUpAxisNegative.IsModified()) {
		const int eventTime = Sys_Milliseconds();
		if (s_sdlGamepad) {
			SDL3_UpdateGamepadAxes(eventTime);
		} else if (s_sdlJoystick) {
			SDL3_UpdateJoystickAxes();
		}
		in_joystickDeadZone.ClearModified();
		in_joystickTriggerThreshold.ClearModified();
		in_joystickLookSensitivity.ClearModified();
		in_joystickLookCurve.ClearModified();
		in_joystickMoveCurve.ClearModified();
		in_joystickInvertLook.ClearModified();
		in_joystickSouthpaw.ClearModified();
		in_joystickUseDedicatedLookAxes.ClearModified();
		in_joystickMoveAxisX.ClearModified();
		in_joystickMoveAxisY.ClearModified();
		in_joystickLookAxisX.ClearModified();
		in_joystickLookAxisY.ClearModified();
		in_joystickUpAxis.ClearModified();
		in_joystickUpAxisNegative.ClearModified();
	}

	bool sawResizeEvent = false;
	SDL_Event event;
	while (SDL_PollEvent(&event)) {
		const int eventTime = SDL3_EventMilliseconds(event.common.timestamp);

#if defined(OPENQ4_SDL3_POSIX_HOST)
		if (Posix_ConsoleProcessEvent(&event)) {
			continue;
		}
#endif
		if (event.type >= SDL_EVENT_DISPLAY_FIRST && event.type <= SDL_EVENT_DISPLAY_LAST) {
			SDL3_HandleDisplayEvent(event.display);
			continue;
		}
		// Support windows share SDL's process-wide event queue. Never route a
		// splash/console window event into the game's focus or input state.
		if (!SDL3_EventTargetsGameWindow(event)) {
			continue;
		}

		if (event.type >= SDL_EVENT_WINDOW_FIRST && event.type <= SDL_EVENT_WINDOW_LAST) {
			if (event.window.type == SDL_EVENT_WINDOW_RESIZED) {
				sawResizeEvent = true;
			}
			SDL3_HandleWindowEvent(event.window, eventTime);
			continue;
		}
		if (!win32.activeApp && SDL3_IsUserInputEvent(event.type)) {
			// Events already queued when focus was lost can arrive after the
			// focus event. Do not relatch input until the game window is active.
			continue;
		}

		switch (event.type) {
			case SDL_EVENT_QUIT:
				common->Printf("SDL3: SDL_EVENT_QUIT received\n");
				cmdSystem->BufferCommandText(CMD_EXEC_APPEND, "quit\n");
				break;

			case SDL_EVENT_TERMINATING:
				SDL3_ClearLifecyclePendingFlag(SDL3_LIFECYCLE_PENDING_BACKGROUND);
				SDL3_HandleAppBackgroundTransition(eventTime, "terminating");
				break;

			case SDL_EVENT_WILL_ENTER_BACKGROUND:
				SDL3_ClearLifecyclePendingFlag(SDL3_LIFECYCLE_PENDING_BACKGROUND);
				SDL3_HandleAppBackgroundTransition(eventTime, "will enter background");
				break;

			case SDL_EVENT_DID_ENTER_BACKGROUND:
				SDL3_ClearLifecyclePendingFlag(SDL3_LIFECYCLE_PENDING_BACKGROUND);
				SDL3_HandleAppBackgroundTransition(eventTime, "did enter background");
				break;

			case SDL_EVENT_WILL_ENTER_FOREGROUND:
				SDL3_ClearLifecyclePendingFlag(SDL3_LIFECYCLE_PENDING_FOREGROUND);
				SDL3_HandleAppForegroundTransition(eventTime, "will enter foreground");
				break;

			case SDL_EVENT_DID_ENTER_FOREGROUND:
				SDL3_ClearLifecyclePendingFlag(SDL3_LIFECYCLE_PENDING_FOREGROUND);
				SDL3_HandleAppForegroundTransition(eventTime, "did enter foreground");
				break;

			case SDL_EVENT_LOW_MEMORY:
				SDL3_ClearLifecyclePendingFlag(SDL3_LIFECYCLE_PENDING_LOW_MEMORY);
				common->Printf("SDL3: low-memory event received from OS.\n");
				break;

			case SDL_EVENT_KEY_DOWN:
			case SDL_EVENT_KEY_UP: {
				const bool down = (event.type == SDL_EVENT_KEY_DOWN) && event.key.down;
				const int key = SDL3_MapScancode(event.key.scancode);

				if (down && !event.key.repeat && key == K_ENTER && (event.key.mod & SDL_KMOD_ALT) != 0) {
					cvarSystem->SetCVarBool("r_fullscreen", !renderSystem->IsFullScreen());
					cmdSystem->BufferCommandText(CMD_EXEC_APPEND, "vid_restart partial\n");
					break;
				}

				if (key == K_PRINT_SCR && Sys_HandlePrintScreenHotkey(down)) {
					break;
				}

				if (key != 0) {
					const bool retained = SDL3_QueueRetainedKey(key,down,event.key.repeat,1024+static_cast<int>(event.key.scancode),eventTime);
					// Keep parity with the old Win32 path: these are queued from keyboard polling.
					if (!retained && key != K_PRINT_SCR && key != K_CTRL && key != K_ALT && key != K_RIGHT_ALT) {
						const auto metadata = openq4::EncodeKeyEventMetadata({
							(event.key.mod & SDL_KMOD_CTRL) != 0,(event.key.mod & SDL_KMOD_SHIFT) != 0,
							(event.key.mod & SDL_KMOD_ALT) != 0,event.key.repeat});
						void* payload = Mem_Alloc(static_cast<int>(metadata.size()));
						memcpy(payload,metadata.data(),metadata.size());
						Sys_QueEvent(eventTime, SE_KEY, key, down, static_cast<int>(metadata.size()), payload);
					}

					const int controlChar = SDL3_MapControlChar(key, down, event.key.mod);
					if (!retained && controlChar != 0) {
						Sys_QueEvent(eventTime, SE_CHAR, controlChar, 0, 0, NULL);
					}

					SDL3_QueueKeyboardInput(key, down, eventTime);
				}

				SDL3_UpdateWindowAspectSnap(false);
				break;
			}

			case SDL_EVENT_TEXT_EDITING:
			case SDL_EVENT_TEXT_EDITING_CANDIDATES:
				// SDL_HINT_IME_IMPLEMENTED_UI=none keeps preedit and candidate
				// rendering in the native IME. Only committed text is queued.
				break;

			case SDL_EVENT_TEXT_INPUT: {
				if (!s_sdlTextInputActive) {
					break;
				}
				const char *text = event.text.text;
				size_t remaining = SDL_strlen(text);
				while (remaining > 0) {
					const Uint32 codepoint = SDL_StepUTF8(&text, &remaining);
					if (codepoint == 0) {
						break;
					}
					// Quake 4 edit widgets, console fields, and stock bitmap fonts
					// share an 8-bit character contract. Never narrow an arbitrary
					// Unicode codepoint into a different visible byte; ignore commits
					// outside the asset-supported range until those consumers and
					// fonts gain an end-to-end Unicode representation.
					if (codepoint == SDL_INVALID_UNICODE_CODEPOINT || codepoint > 0xff ||
							!idStr::CharIsPrintable(static_cast<byte>(codepoint))) {
						if (!s_sdlUnsupportedTextInputLogged) {
							common->DPrintf("SDL3: ignoring committed text outside the stock single-byte font range.\n");
							s_sdlUnsupportedTextInputLogged = true;
						}
						continue;
					}
					Sys_QueEvent(eventTime, SE_CHAR, static_cast<int>(codepoint), 0, 0, NULL);
				}
				break;
			}

			case SDL_EVENT_MOUSE_MOTION:
			{
				if (RetainedUI_IsOpen() && !(console && console->Active())) {
					if (SDL3_ShouldRouteMenuMouse() && !SDL3_IsMouseCaptured())
						SDL3_QueueRetainedPointer(event.motion.x,event.motion.y,eventTime);
					break;
				}
				if (!std::isfinite(event.motion.x) || !std::isfinite(event.motion.y) ||
						!std::isfinite(event.motion.xrel) || !std::isfinite(event.motion.yrel)) {
					s_sdlRelativeMouseRemainderX = 0.0f;
					s_sdlRelativeMouseRemainderY = 0.0f;
					s_haveAbsoluteMousePosition = false;
					SDL3_ResetMenuMouseTracking();
					break;
				}
				int dx = 0;
				int dy = 0;
				const bool mouseCaptured = SDL3_IsMouseCaptured();

				if (mouseCaptured) {
					dx = SDL3_ConsumeMouseDelta(event.motion.xrel, s_sdlRelativeMouseRemainderX);
					dy = SDL3_ConsumeMouseDelta(event.motion.yrel, s_sdlRelativeMouseRemainderY);
					s_haveAbsoluteMousePosition = false;
					SDL3_ResetMenuMouseTracking();
				} else if (SDL3_ShouldRouteMenuMouse()) {
					s_sdlRelativeMouseRemainderX = 0.0f;
					s_sdlRelativeMouseRemainderY = 0.0f;
					float menuMouseX = 0.0f;
					float menuMouseY = 0.0f;
					const bool warpMotionEvent = s_ignoreNextMenuWarpMotion &&
						fabsf(event.motion.x - s_menuWarpWindowX) <= 1.0f &&
						fabsf(event.motion.y - s_menuWarpWindowY) <= 1.0f;

					if (warpMotionEvent) {
						s_ignoreNextMenuWarpMotion = false;
						if (SDL3_MapWindowMouseToRoutedCursor(event.motion.x, event.motion.y, menuMouseX, menuMouseY)) {
							SDL3_SetMenuMouseTrackingPosition(menuMouseX, menuMouseY);
						}
					} else {
						if (s_ignoreNextMenuWarpMotion) {
							s_ignoreNextMenuWarpMotion = false;
						}

						if (SDL3_MapWindowMouseToRoutedCursor(event.motion.x, event.motion.y, menuMouseX, menuMouseY)) {
							(void)SDL3_UpdateRoutedMouseDelta(menuMouseX, menuMouseY, dx, dy);
						} else {
							SDL3_ResetMenuMouseTracking();
						}
					}
				} else {
					const int absoluteX = static_cast<int>(event.motion.x);
					const int absoluteY = static_cast<int>(event.motion.y);
					s_sdlRelativeMouseRemainderX = 0.0f;
					s_sdlRelativeMouseRemainderY = 0.0f;
					SDL3_ResetMenuMouseTracking();
					if (!s_haveAbsoluteMousePosition) {
						s_absoluteMouseX = absoluteX;
						s_absoluteMouseY = absoluteY;
						s_haveAbsoluteMousePosition = true;
					} else {
						dx = absoluteX - s_absoluteMouseX;
						dy = absoluteY - s_absoluteMouseY;
						s_absoluteMouseX = absoluteX;
						s_absoluteMouseY = absoluteY;
					}
				}

				if ((mouseCaptured || SDL3_ShouldRouteMenuMouse()) && (dx != 0 || dy != 0)) {
					SDL3_QueueMouseDelta(dx, dy, eventTime);
				}
				break;
			}

			case SDL_EVENT_MOUSE_BUTTON_DOWN:
			case SDL_EVENT_MOUSE_BUTTON_UP: {
				const bool routedMouseInput = SDL3_IsMouseCaptured() || SDL3_ShouldRouteMenuMouse();
				if (RetainedUI_IsOpen() && !(console && console->Active()) && SDL3_ShouldRouteMenuMouse())
					SDL3_QueueRetainedPointer(event.button.x,event.button.y,eventTime);
				if (routedMouseInput || openQ4_AcceptingLoadingContinueInput()) {
					const int key = SDL3_MapMouseButton(event.button.button);
					if (key != 0) {
						const bool down = event.button.down;
						// Don't latch poll-path button state from the loading-continue
						// gate into the first gameplay usercmd frame.
						SDL3_QueueMouseButtonEvent(key, down, eventTime, routedMouseInput);
					}
				}
				break;
			}

			case SDL_EVENT_MOUSE_WHEEL: {
				float deltaY = event.wheel.y;
				if (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) {
					deltaY = -deltaY;
				}

				deltaY += s_sdlMouseWheelRemainderY;
				if (!std::isfinite(deltaY)) {
					s_sdlMouseWheelRemainderY = 0.0f;
					break;
				}
				deltaY = idMath::ClampFloat(
					-static_cast<float>(SDL3_MAX_MOUSE_WHEEL_STEPS_PER_EVENT),
					static_cast<float>(SDL3_MAX_MOUSE_WHEEL_STEPS_PER_EVENT),
					deltaY);
				int wheelSteps = static_cast<int>(deltaY);
				s_sdlMouseWheelRemainderY = deltaY - static_cast<float>(wheelSteps);
				if (wheelSteps != 0) {
					const bool wheelDown = wheelSteps < 0;
					int absSteps = wheelSteps == idMath::INT_MIN ? idMath::INT_MAX : (wheelDown ? -wheelSteps : wheelSteps);
					if (absSteps > SDL3_MAX_MOUSE_WHEEL_STEPS_PER_EVENT) {
						absSteps = SDL3_MAX_MOUSE_WHEEL_STEPS_PER_EVENT;
					}
					const int queuedWheelSteps = wheelDown ? -absSteps : absSteps;
					const int wheelKey = wheelDown ? K_MWHEELDOWN : K_MWHEELUP;
					for (int i = 0; i < absSteps; ++i) {
						Sys_QueEvent(eventTime, SE_KEY, wheelKey, true, 0, NULL);
						Sys_QueEvent(eventTime, SE_KEY, wheelKey, false, 0, NULL);
					}
					SDL3_QueueMouseInput(M_DELTAZ, queuedWheelSteps, eventTime);
				}
				break;
			}

			case SDL_EVENT_GAMEPAD_ADDED:
				// Prefer gamepads over plain joysticks. SDL3_OpenGamepad releases any
				// open joystick only after the gamepad opens, so a failed open keeps
				// the current device usable.
				if (in_joystick.GetBool() && !s_sdlGamepad) {
					(void)SDL3_OpenGamepad(event.gdevice.which);
				}
				break;

			case SDL_EVENT_GAMEPAD_REMOVED:
				if (s_sdlGamepad && event.gdevice.which == s_sdlGamepadId) {
					SDL3_CloseGamepad(eventTime);
					SDL3_OpenFirstController();
				}
				break;

			case SDL_EVENT_GAMEPAD_REMAPPED:
			case SDL_EVENT_GAMEPAD_STEAM_HANDLE_UPDATED:
				if (in_joystick.GetBool() && s_sdlGamepad && event.gdevice.which == s_sdlGamepadId) {
					SDL3_UpdateGamepadSensorState(true);
					SDL3_UpdateGamepadAxes(eventTime);
				}
				break;

			case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
			case SDL_EVENT_GAMEPAD_BUTTON_UP:
				if (in_joystick.GetBool() && s_sdlGamepad && event.gbutton.which == s_sdlGamepadId) {
					const int button = static_cast<int>(event.gbutton.button);
					if (button >= 0 && button < SDL_GAMEPAD_BUTTON_COUNT) {
						const bool down = event.gbutton.down;
						if (s_gamepadButtonsDown[button] != down) {
							s_gamepadButtonsDown[button] = down;
							SDL3_PostControllerKeyEvent(SDL3_MapGamepadButton(event.gbutton.button), down, eventTime);
						}
					}
				}
				break;

			case SDL_EVENT_GAMEPAD_AXIS_MOTION:
				if (in_joystick.GetBool() && s_sdlGamepad && event.gaxis.which == s_sdlGamepadId) {
					SDL3_UpdateGamepadAxes(eventTime);
				}
				break;

			case SDL_EVENT_GAMEPAD_TOUCHPAD_DOWN:
			case SDL_EVENT_GAMEPAD_TOUCHPAD_MOTION:
			case SDL_EVENT_GAMEPAD_TOUCHPAD_UP:
				SDL3_HandleGamepadTouchpadEvent(event.gtouchpad, eventTime);
				break;

			case SDL_EVENT_GAMEPAD_SENSOR_UPDATE:
				SDL3_HandleGamepadGyroEvent(event.gsensor, eventTime);
				break;

			case SDL_EVENT_JOYSTICK_ADDED:
				if (in_joystick.GetBool() && !s_sdlGamepad && !s_sdlJoystick && !SDL_IsGamepad(event.jdevice.which)) {
					(void)SDL3_OpenJoystick(event.jdevice.which);
				}
				break;

			case SDL_EVENT_JOYSTICK_REMOVED:
				if (s_sdlJoystick && event.jdevice.which == s_sdlJoystickId) {
					SDL3_CloseJoystick(eventTime);
					SDL3_OpenFirstController();
				}
				break;

			case SDL_EVENT_JOYSTICK_BUTTON_DOWN:
			case SDL_EVENT_JOYSTICK_BUTTON_UP:
				if (in_joystick.GetBool() && s_sdlJoystick && event.jbutton.which == s_sdlJoystickId) {
					const int button = static_cast<int>(event.jbutton.button);
					if (button >= 0 && button < SDL3_MAX_JOYSTICK_BUTTONS) {
						const bool down = event.jbutton.down;
						if (s_joystickButtonsDown[button] != down) {
							s_joystickButtonsDown[button] = down;
							SDL3_PostControllerKeyEvent(SDL3_JoyKeyFromOrdinal(button), down, eventTime);
						}
					}
				}
				break;

			case SDL_EVENT_JOYSTICK_HAT_MOTION:
				if (in_joystick.GetBool() && s_sdlJoystick && event.jhat.which == s_sdlJoystickId) {
					SDL3_SetJoystickHat(event.jhat.value, eventTime);
				}
				break;

			case SDL_EVENT_JOYSTICK_AXIS_MOTION:
				if (in_joystick.GetBool() && s_sdlJoystick && event.jaxis.which == s_sdlJoystickId) {
					SDL3_UpdateJoystickAxes();
				}
				break;

			case SDL_EVENT_FINGER_DOWN:
			case SDL_EVENT_FINGER_MOTION:
			case SDL_EVENT_FINGER_UP:
			case SDL_EVENT_FINGER_CANCELED:
#if !defined(OPENQ4_SIGMATOUCH)
				SDL3_HandleFingerEvent(event.tfinger, eventTime);
#endif
				break;

			default:
				break;
		}
	}

	// Keep render dimensions in sync even if the platform misses or coalesces resize events.
	SDL3_ProcessPendingLifecycleEvents(Sys_Milliseconds());
	SDL3_RefreshWindowPlacement();
	SDL3_UpdateWindowAspectSnap(sawResizeEvent);
#if defined(OPENQ4_SIGMATOUCH)
	// The POSIX guard above guarantees this runs on the engine thread. Process
	// lifecycle transitions before accepting a batch from the UI thread.
	window = s_sdlWindow;
	Quake4_UpdateTouchScreenMode();
	if (win32.activeApp && !s_sdlFocusInputReleased) {
		Quake4_DrainTouchInput();
	} else {
		Quake4_ClearTouchInput();
	}
#endif

	return true;
}

/*
====================
MapKey

Map from windows to Doom keynums
====================
*/
int MapKey(int key) {
	int result;
	int modified;
	bool isExtended;

	modified = (key >> 16) & 255;

	if (modified > 127) {
		return 0;
	}

	isExtended = ((key & (1 << 24)) != 0);

	if (isExtended) {
		switch (modified) {
			case 0x35:
				return K_KP_SLASH;
			default:
				break;
		}
	}

	const unsigned char *scanToKey = Sys_GetScanTable();
	result = scanToKey[modified];

	if (isExtended) {
		switch (result) {
			case K_PAUSE:
				return K_KP_NUMLOCK;
			case 0x0D:
				return K_KP_ENTER;
			case 0x2F:
				return K_KP_SLASH;
			case 0xAF:
				return K_KP_PLUS;
			case K_KP_STAR:
				return K_PRINT_SCR;
			case K_ALT:
				return K_RIGHT_ALT;
			default:
				break;
		}
	} else {
		switch (result) {
			case K_HOME:
				return K_KP_HOME;
			case K_UPARROW:
				return K_KP_UPARROW;
			case K_PGUP:
				return K_KP_PGUP;
			case K_LEFTARROW:
				return K_KP_LEFTARROW;
			case K_RIGHTARROW:
				return K_KP_RIGHTARROW;
			case K_END:
				return K_KP_END;
			case K_DOWNARROW:
				return K_KP_DOWNARROW;
			case K_PGDN:
				return K_KP_PGDN;
			case K_INS:
				return K_KP_INS;
			case K_DEL:
				return K_KP_DEL;
			default:
				break;
		}
	}

	return result;
}

void Sys_InitScanTable(void) {
	idStr lang = cvarSystem->GetCVarString("sys_lang");
	if (lang.Length() == 0) {
		lang = "english";
	}

	// Keep legacy Win32 behavior: English maps RightAlt to K_ALT for bind compatibility.
	// Polish joins the list because "Polish (Programmers)" carries the whole
	// accented alphabet on the AltGr layer.
	s_rightAltKey = K_ALT;
	if (lang.Icmp("spanish") == 0 ||
		lang.Icmp("french") == 0 ||
		lang.Icmp("german") == 0 ||
		lang.Icmp("italian") == 0 ||
		lang.Icmp("polish") == 0) {
		s_rightAltKey = K_RIGHT_ALT;
	}
}

const unsigned char *Sys_GetScanTable(void) {
	return s_scantokey;
}

unsigned char Sys_GetConsoleKey(bool shifted) {
	const SDL_Keymod modState = shifted ? SDL_KMOD_SHIFT : SDL_KMOD_NONE;
	const SDL_Keycode translatedKeycode = SDL_GetKeyFromScancode(SDL_SCANCODE_GRAVE, modState, false);
	const int translatedKey = SDL3_MapKeycode(translatedKeycode);

	if (translatedKey > 0 && translatedKey <= 255) {
		return static_cast<unsigned char>(translatedKey);
	}

	return shifted ? s_scantoshift[41] : s_scantokey[41];
}

unsigned char Sys_MapCharForKey(int key) {
	return static_cast<unsigned char>(key);
}

void IN_ActivateMouse(void) {
	if (!s_sdlWindow || !win32.in_mouse.GetBool() || SDL3_IsMouseCaptured()) {
		return;
	}

	if (SDL3_IsNativeWaylandVideoDriver()) {
		if (!SDL_SetWindowRelativeMouseMode(s_sdlWindow, true)) {
			common->Printf("SDL3: failed to enable relative mouse mode: %s\n", SDL_GetError());
			return;
		}
		if (!SDL_SetWindowMouseGrab(s_sdlWindow, true)) {
			common->DPrintf("SDL3: native Wayland could not confine mouse pointer; continuing with relative mouse mode: %s\n", SDL_GetError());
		}
		(void)SDL_HideCursor();
		(void)SDL_GetRelativeMouseState(NULL, NULL);
		s_sdlRelativeMouseRemainderX = 0.0f;
		s_sdlRelativeMouseRemainderY = 0.0f;
		win32.mouseGrabbed = SDL3_IsMouseCaptured();
		SDL3_ResetMenuMouseTracking();
		SDL3_UpdateCursorVisibility();
		return;
	}

	if (!SDL_SetWindowMouseGrab(s_sdlWindow, true)) {
		common->Printf("SDL3: failed to grab mouse: %s\n", SDL_GetError());
		return;
	}
	if (!SDL_SetWindowRelativeMouseMode(s_sdlWindow, true)) {
		common->Printf("SDL3: failed to enable relative mouse mode: %s\n", SDL_GetError());
		(void)SDL_SetWindowMouseGrab(s_sdlWindow, false);
		return;
	}

	(void)SDL_HideCursor();
	(void)SDL_GetRelativeMouseState(NULL, NULL);
	s_sdlRelativeMouseRemainderX = 0.0f;
	s_sdlRelativeMouseRemainderY = 0.0f;
	win32.mouseGrabbed = SDL3_IsMouseCaptured();
	SDL3_ResetMenuMouseTracking();
	SDL3_UpdateCursorVisibility();
}

void IN_DeactivateMouse(void) {
	if (!s_sdlWindow || !SDL3_IsMouseCaptured()) {
		return;
	}

	(void)SDL_SetWindowRelativeMouseMode(s_sdlWindow, false);
	(void)SDL_SetWindowMouseGrab(s_sdlWindow, false);
	if (SDL3_ShouldRouteMenuMouse()) {
		SDL3_SyncSystemMouseToActiveCursor();
	} else {
		SDL3_ResetMenuMouseTracking();
	}
	win32.mouseGrabbed = SDL3_IsMouseCaptured();
	s_haveAbsoluteMousePosition = false;
	s_sdlRelativeMouseRemainderX = 0.0f;
	s_sdlRelativeMouseRemainderY = 0.0f;
	SDL3_UpdateCursorVisibility();
}

static void SDL3_PrintMouseCaptureDiagnosticsLine(const char *label) {
	const bool hasWindow = s_sdlWindow != NULL;
	const bool relativeMode = hasWindow && SDL_GetWindowRelativeMouseMode(s_sdlWindow);
	const bool mouseGrab = hasWindow && SDL_GetWindowMouseGrab(s_sdlWindow);
	const bool captured = SDL3_IsMouseCaptured();

	common->Printf(
		"SDL3 mouse capture diagnostics %s: window=%s videoDriver=%s nativeWayland=%s in_mouse=%s relative=%s grab=%s captured=%s\n",
		label != NULL ? label : "state",
		hasWindow ? "yes" : "no",
		s_sdlVideoDriverName,
		SDL3_IsNativeWaylandVideoDriver() ? "yes" : "no",
		win32.in_mouse.GetBool() ? "on" : "off",
		relativeMode ? "on" : "off",
		mouseGrab ? "on" : "off",
		captured ? "yes" : "no");
}

static void SDL3_MouseCaptureDiagnostics_f(const idCmdArgs &args) {
	int iterations = 1;
	if (args.Argc() > 1) {
		if (idStr::IsNumeric(args.Argv(1))) {
			iterations = idMath::ClampInt(1, 8, atoi(args.Argv(1)));
		} else {
			common->Printf("SDL3 mouse capture diagnostics: ignoring non-numeric repeat count '%s'\n", args.Argv(1));
		}
	}

	common->Printf("SDL3 mouse capture diagnostics: begin repeat=%d\n", iterations);
	SDL3_PrintMouseCaptureDiagnosticsLine("before");
	if (!s_sdlWindow) {
		common->Printf("SDL3 mouse capture diagnostics: skipped; no SDL window is active.\n");
		return;
	}

	for (int iteration = 0; iteration < iterations; ++iteration) {
		common->Printf("SDL3 mouse capture diagnostics: iteration %d/%d\n", iteration + 1, iterations);
		IN_ActivateMouse();
		SDL3_PrintMouseCaptureDiagnosticsLine("after activate");
		IN_DeactivateMouse();
		SDL3_PrintMouseCaptureDiagnosticsLine("after deactivate");
	}
}

void IN_DeactivateMouseIfWindowed(void) {
	if (!win32.cdsFullscreen) {
		IN_DeactivateMouse();
	}
}

void IN_Frame(void) {
	win32.mouseGrabbed = SDL3_IsMouseCaptured();

	bool shouldGrab = true;
	const bool routeMenuMouse = SDL3_ShouldRouteMenuMouse();
	const bool consoleActive = console != NULL && console->Active();
	idUserInterface *activeMenuGui = routeMenuMouse ? SDL3_GetActiveMenuGui() : NULL;

	if (activeMenuGui != s_trackedMenuGui || consoleActive != s_trackedConsoleActive) {
		// Match the Alt+Tab recovery path when gameplay/menu transitions swap the
		// active GUI or console overlay without generating a focus event.
		SDL3_InvalidateMenuMouseRouting();
		s_trackedMenuGui = activeMenuGui;
		s_trackedConsoleActive = consoleActive;
	}

#if !defined(OPENQ4_SDL3_POSIX_HOST)
	if (win32.printScreenFocusReleaseUntil != 0) {
		const uint32_t now = static_cast<uint32_t>(GetTickCount());
		if (static_cast<int32_t>(win32.printScreenFocusReleaseUntil - now) > 0) {
			shouldGrab = false;
		} else {
			win32.printScreenFocusReleaseUntil = 0;
		}
	}
#endif

	if (!win32.in_mouse.GetBool()) {
		shouldGrab = false;
	}
	if (routeMenuMouse) {
		shouldGrab = false;
	}
	if (!win32.activeApp) {
		// Fullscreen must not bypass compositor focus. Session::Frame requests a
		// grab every tick during gameplay, including frames already queued after
		// focus loss, so keep relative mode disabled until focus is restored.
		shouldGrab = false;
	}

	if (!win32.cdsFullscreen) {
		if (win32.mouseReleased) {
			shouldGrab = false;
		}
		if (win32.movingWindow) {
			shouldGrab = false;
		}
	}

	if (shouldGrab != win32.mouseGrabbed) {
		if (win32.mouseGrabbed) {
			IN_DeactivateMouse();
		} else {
			IN_ActivateMouse();
		}
	}

	if (routeMenuMouse && !s_haveMenuMousePosition) {
		// Keep routed mouse state aligned whenever routing activates or is invalidated.
		SDL3_SyncSystemMouseToActiveCursor();
	} else if (!routeMenuMouse && s_menuMouseRouteActive) {
		SDL3_ResetMenuMouseTracking();
	}
	s_menuMouseRouteActive = routeMenuMouse;
	SDL3_UpdateCursorVisibility();
	SDL3_UpdateTextInputState();
}

void Sys_GrabMouseCursor(bool grabIt) {
#ifndef ID_DEDICATED
	if (grabIt && !win32.activeApp) {
		// Do not let the per-frame gameplay request consume the release marker;
		// the focus-gained path needs that transition to reacquire explicitly.
		IN_DeactivateMouse();
		return;
	}
	const bool wasMouseReleased = win32.mouseReleased;
	win32.mouseReleased = !grabIt;
	if (!grabIt) {
		// Explicit release requests must win even in fullscreen. IN_Frame keeps
		// normal fullscreen gameplay captured, but focus/background transitions
		// use this API specifically to hand the pointer back to the compositor.
		IN_DeactivateMouse();
		return;
	}
	if (wasMouseReleased != win32.mouseReleased) {
		IN_Frame();
	}
#else
	(void)grabIt;
#endif
}

void Sys_InitInput(void) {
	common->Printf("\n------- Input Initialization -------\n");
	openq4::NativeInputBeforeWindowChange();
	win32.activeApp = true;
	s_sdlFocusInputReleased = false;
	win32.mouseReleased = false;
	win32.movingWindow = false;
	win32.mouseGrabbed = SDL3_IsMouseCaptured();
	s_menuMouseRouteActive = false;
	s_trackedMenuGui = NULL;
	s_trackedConsoleActive = false;
	SDL3_ResetMenuMouseTracking();
	s_menuMouseInsideWindow = true;
	SDL3_UpdateCursorVisibility();

	SDL3_UpdateTextInputState();

	if (win32.in_mouse.GetBool()) {
		Sys_GrabMouseCursor(false);
		common->Printf("mouse: SDL3 initialized.\n");
	} else {
		common->Printf("Mouse control not active.\n");
	}

	SDL3_InitControllerSubsystems();
	if (in_joystick.GetBool()) {
		if (s_sdlGamepad || s_sdlJoystick) {
			common->Printf("joystick: SDL3 initialized.\n");
		} else {
			common->Printf("joystick: SDL3 initialized (no device detected).\n");
		}
	} else {
		common->Printf("Joystick control not active.\n");
	}

	win32.in_mouse.ClearModified();
	in_joystick.ClearModified();
	in_joystickLowBatteryRumbleThreshold.ClearModified();
	in_joystickLowBatteryRumbleScale.ClearModified();
	in_gyro.ClearModified();
	in_gyroSensitivity.ClearModified();
	in_gyroDeadZone.ClearModified();
	in_gyroYawAxis.ClearModified();
	in_gyroPitchAxis.ClearModified();
	in_gyroInvertYaw.ClearModified();
	in_gyroInvertPitch.ClearModified();
	in_touchpadMode.ClearModified();
	in_touchpadSensitivity.ClearModified();
	in_touchscreen.ClearModified();
	com_steamDeckAutoFrameCap.ClearModified();
	com_steamDeckFrameCap.ClearModified();
	SDL3_ClearInputQueues();
	common->Printf("------------------------------------\n");
}

void Sys_ShutdownInput(void) {
	openq4::NativeInputBeforeInputBlockerChange();
	IN_DeactivateMouse();

	if (s_sdlWindow && s_sdlTextInputActive) {
		(void)SDL_ClearComposition(s_sdlWindow);
		(void)SDL_StopTextInput(s_sdlWindow);
		s_sdlTextInputActive = false;
	}

	SDL3_ShutdownControllerSubsystems();
	SDL3_ClearInputQueues();
}

void Sys_ClearInputEvents(void) {
	SDL3_ClearInputQueues();
}

// All checked methods acquire SDL storage first, then observe the disposition
// mutex. No disposition method holds that mutex while acquiring SDL storage.
static bool SDL3_DeferredKeyboardKey(int key) {
    return key == K_PRINT_SCR || key == K_CTRL || key == K_ALT || key == K_RIGHT_ALT;
}
static bool SDL3_InputValid(const sdlKeyboardEvent_t& value, const sysKeyboardDisposition_t& tag) {
    return value.key > 0 && value.key < K_LAST_KEY && value.time >= 0 &&
        (tag.Empty() || (Sys_EventDispositionTagCurrent(tag.parent) &&
        (!tag.deferredEmission || (SDL3_DeferredKeyboardKey(value.key) && tag.deferredEmission != tag.parent.emission))));
}
static bool SDL3_InputValid(const sdlMouseEvent_t& value, const sysEventDispositionTag_t& tag) {
    return SDL3_ShouldQueueMousePoll(value.action, value.value) && value.time >= 0 &&
        (tag.Empty() || Sys_EventDispositionTagCurrent(tag));
}
static sysKeyboardInputDisposition_t SDL3_TrackedInput(const sdlKeyboardEvent_t& value, const sysKeyboardDisposition_t& tag) {
    return {value.key, value.down, value.time, tag};
}
static sysMouseInputDisposition_t SDL3_TrackedInput(const sdlMouseEvent_t& value, const sysEventDispositionTag_t& tag) {
    return {value.action, value.value, value.time, tag};
}

bool Sys_QueKeyboardInputWithDisposition(sysKeyboardInputDisposition_t& input) noexcept {
    SDL3_InputStorageLock lock;
    const sdlKeyboardEvent_t value{input.key, input.down, input.time};
    if (input.disposition.Empty() || !SDL3_InputValid(value, input.disposition)) return false;
    const int next = (s_keyboardHead + 1) & SDL3_INPUT_QUEUE_MASK;
    if (next == s_keyboardTail) {
        Sys_InvalidateEventQueue(); return false; // Preserve full ring and caller.
    }
    if (s_inputDispositionHighwater == (std::numeric_limits<std::uint64_t>::max)()) return false;
    s_keyboardQueue[s_keyboardHead] = value;
    s_keyboardDisposition[s_keyboardHead] = input.disposition;
    s_keyboardRetirementSerials[s_keyboardHead] = ++s_inputDispositionHighwater;
    s_keyboardHead = next;
    input = {};
    return true;
}
bool Sys_QueMouseInputWithDisposition(sysMouseInputDisposition_t& input) noexcept {
    SDL3_InputStorageLock lock;
    const sdlMouseEvent_t value{input.action, input.value, input.time};
    if (input.disposition.Empty() || !SDL3_InputValid(value, input.disposition)) return false;
    const int next = (s_mouseHead + 1) & SDL3_INPUT_QUEUE_MASK;
    if (next == s_mouseTail) {
        Sys_InvalidateEventQueue(); return false; // Preserve full ring and caller.
    }
    if (s_inputDispositionHighwater == (std::numeric_limits<std::uint64_t>::max)()) return false;
    s_mouseQueue[s_mouseHead] = value;
    s_mouseDisposition[s_mouseHead] = input.disposition;
    s_mouseRetirementSerials[s_mouseHead] = ++s_inputDispositionHighwater;
    s_mouseHead = next;
    input = {};
    return true;
}

template<class Value, class Tag>
static bool SDL3_PollInputWithDisposition(Value (&queue)[SDL3_INPUT_QUEUE_SIZE], Tag (&tags)[SDL3_INPUT_QUEUE_SIZE],
    std::uint64_t (&serials)[SDL3_INPUT_QUEUE_SIZE], const int& head, int& tail, Value (&polled)[SDL3_INPUT_QUEUE_SIZE], Tag (&polledTags)[SDL3_INPUT_QUEUE_SIZE],
    int& count, sdlInputDispositionState_t& state, sysInputDispositionLane_t lane, sysInputDispositionSlice_t& out) {
    SDL3_InputStorageLock lock;
    const auto epoch = Sys_EventDispositionEpoch(), token = Sys_EventQueueToken();
    if (!epoch || !token || state.checked || count != 0 ||
        s_inputDispositionHighwater == (std::numeric_limits<std::uint64_t>::max)()) return false;
    // Preflight the entire queued inventory before moving any ownership.
    for (int i = tail; i != head; i = (i + 1) & SDL3_INPUT_QUEUE_MASK) {
        if (!SDL3_InputValid(queue[i], tags[i])) return false;
    }
    if (epoch != Sys_EventDispositionEpoch() || token != Sys_EventQueueToken()) return false;
    while (tail != head) {
        polled[count] = queue[tail]; polledTags[count] = tags[tail];
        queue[tail] = {}; tags[tail] = {}; serials[tail] = 0;
        ++count; tail = (tail + 1) & SDL3_INPUT_QUEUE_MASK;
    }
    state.slice = {epoch, token, ++s_inputDispositionHighwater, lane, static_cast<unsigned>(count)};
    state.next = 0; state.checked = true;
    out = state.slice;
    return true;
}
bool Sys_PollKeyboardInputWithDisposition(sysInputDispositionSlice_t& out) noexcept {
    return SDL3_PollInputWithDisposition(s_keyboardQueue, s_keyboardDisposition, s_keyboardRetirementSerials, s_keyboardHead, s_keyboardTail,
        s_polledKeyboard, s_polledKeyboardDisposition, s_polledKeyboardCount, s_keyboardDispositionState,
        sysInputDispositionLane_t::Keyboard, out);
}
bool Sys_PollMouseInputWithDisposition(sysInputDispositionSlice_t& out) noexcept {
    return SDL3_PollInputWithDisposition(s_mouseQueue, s_mouseDisposition, s_mouseRetirementSerials, s_mouseHead, s_mouseTail,
        s_polledMouse, s_polledMouseDisposition, s_polledMouseCount, s_mouseDispositionState,
        sysInputDispositionLane_t::Mouse, out);
}

static bool SDL3_InputSliceCurrent(const sysInputDispositionSlice_t& expected, const sdlInputDispositionState_t& state) {
    return state.checked && expected == state.slice && expected.serial &&
        expected.epoch && expected.epoch == Sys_EventDispositionEpoch() &&
        expected.streamToken && expected.streamToken == Sys_EventQueueToken();
}
template<class Value, class Tag, class Output>
static sysEventTransfer_t SDL3_PeekInputWithDisposition(const sysInputDispositionSlice_t& expected,
    const sdlInputDispositionState_t& state, const Value (&polled)[SDL3_INPUT_QUEUE_SIZE],
    const Tag (&tags)[SDL3_INPUT_QUEUE_SIZE], sysInputDispositionSlot_t& slot, Output& out) {
    SDL3_InputStorageLock lock;
    if (!SDL3_InputSliceCurrent(expected, state)) return sysEventTransfer_t::Refused;
    if (state.next == state.slice.count) return sysEventTransfer_t::Empty;
    if (!SDL3_InputValid(polled[state.next], tags[state.next])) return sysEventTransfer_t::Refused;
    out = SDL3_TrackedInput(polled[state.next], tags[state.next]);
    slot = {state.slice, state.next};
    return sysEventTransfer_t::Ready;
}
template<class Value, class Tag, class Output>
static sysEventTransfer_t SDL3_TakeInputWithDisposition(const sysInputDispositionSlot_t& expected,
    sdlInputDispositionState_t& state, Value (&polled)[SDL3_INPUT_QUEUE_SIZE],
    Tag (&tags)[SDL3_INPUT_QUEUE_SIZE], Output& out) {
    SDL3_InputStorageLock lock;
    if (!SDL3_InputSliceCurrent(expected.slice, state) || expected.index != state.next ||
        state.next >= state.slice.count || !SDL3_InputValid(polled[state.next], tags[state.next]))
        return sysEventTransfer_t::Refused;
    out = SDL3_TrackedInput(polled[state.next], tags[state.next]);
    polled[state.next] = {}; tags[state.next] = {};
    ++state.next;
    return sysEventTransfer_t::Ready;
}
sysEventTransfer_t Sys_PeekKeyboardInputWithDisposition(const sysInputDispositionSlice_t& slice,
    sysInputDispositionSlot_t& slot, sysKeyboardInputDisposition_t& out) noexcept {
    return SDL3_PeekInputWithDisposition(slice, s_keyboardDispositionState, s_polledKeyboard, s_polledKeyboardDisposition, slot, out);
}
sysEventTransfer_t Sys_PeekMouseInputWithDisposition(const sysInputDispositionSlice_t& slice,
    sysInputDispositionSlot_t& slot, sysMouseInputDisposition_t& out) noexcept {
    return SDL3_PeekInputWithDisposition(slice, s_mouseDispositionState, s_polledMouse, s_polledMouseDisposition, slot, out);
}
sysEventTransfer_t Sys_TakeKeyboardInputWithDisposition(const sysInputDispositionSlot_t& slot,
    sysKeyboardInputDisposition_t& out) noexcept {
    return SDL3_TakeInputWithDisposition(slot, s_keyboardDispositionState, s_polledKeyboard, s_polledKeyboardDisposition, out);
}
sysEventTransfer_t Sys_TakeMouseInputWithDisposition(const sysInputDispositionSlot_t& slot,
    sysMouseInputDisposition_t& out) noexcept {
    return SDL3_TakeInputWithDisposition(slot, s_mouseDispositionState, s_polledMouse, s_polledMouseDisposition, out);
}
static bool SDL3_EndInputWithDisposition(const sysInputDispositionSlice_t& expected, sdlInputDispositionState_t& state, int& count) {
    SDL3_InputStorageLock lock;
    if (!SDL3_InputSliceCurrent(expected, state) || state.next != state.slice.count) return false;
    state = {}; count = 0;
    return true;
}
bool Sys_EndKeyboardInputWithDisposition(const sysInputDispositionSlice_t& expected) noexcept {
    return SDL3_EndInputWithDisposition(expected, s_keyboardDispositionState, s_polledKeyboardCount);
}
bool Sys_EndMouseInputWithDisposition(const sysInputDispositionSlice_t& expected) noexcept {
    return SDL3_EndInputWithDisposition(expected, s_mouseDispositionState, s_polledMouseCount);
}


static bool SDL3_RetirementValue(const sdlKeyboardEvent_t& value, const sysKeyboardDisposition_t& tag) {
    return value.key > 0 && value.key < K_LAST_KEY && value.time >= 0 && tag.parent.ShapeValid() &&
        (!tag.deferredEmission || (SDL3_DeferredKeyboardKey(value.key) && tag.deferredEmission != tag.parent.emission));
}
static bool SDL3_RetirementValue(const sdlMouseEvent_t& value, const sysEventDispositionTag_t& tag) {
    return SDL3_ShouldQueueMousePoll(value.action, value.value) && value.time >= 0 && tag.ShapeValid();
}
static openq4::NativeInputHead SDL3_RetirementHead(const sdlKeyboardEvent_t& value, const sysKeyboardDisposition_t& tag,
    openq4::NativeInputLane lane, std::uint64_t serial, unsigned slot) {
    return {lane, serial, slot, tag.parent, {SE_KEY, value.key, value.down ? 1 : 0, value.time, 0, 0, tag.deferredEmission}};
}
static openq4::NativeInputHead SDL3_RetirementHead(const sdlMouseEvent_t& value, const sysEventDispositionTag_t& tag,
    openq4::NativeInputLane lane, std::uint64_t serial, unsigned slot) {
    return {lane, serial, slot, tag, {SE_MOUSE, value.action, value.value, value.time, 0, 0, 0}};
}
// Caller already owns SDL storage. No Source lookup, active epoch requirement,
// native callback or mutation; legacy slices and untagged prefixes obstruct.
template<class Value, class Tag>
static sysEventTransfer_t SDL3_PeekRetirementInput(const sdlInputDispositionState_t& state, int count,
    const Value (&polled)[SDL3_INPUT_QUEUE_SIZE], const Tag (&polledTags)[SDL3_INPUT_QUEUE_SIZE],
    const Value (&queue)[SDL3_INPUT_QUEUE_SIZE], const Tag (&tags)[SDL3_INPUT_QUEUE_SIZE],
    const std::uint64_t (&serials)[SDL3_INPUT_QUEUE_SIZE], int head, int tail,
    openq4::NativeInputLane lane, openq4::NativeInputHead& out) {
    if (!Sys_EventDispositionBoundThread()) return sysEventTransfer_t::Refused;
    if (state.checked) {
        if (count < 0 || state.slice.count != static_cast<unsigned>(count) || state.slice.count > SDL3_INPUT_QUEUE_SIZE ||
            !state.slice.serial || state.next > state.slice.count) return sysEventTransfer_t::Refused;
        if (state.next == state.slice.count) return sysEventTransfer_t::Empty; // Exact empty End precedes ring access.
        if (!SDL3_RetirementValue(polled[state.next], polledTags[state.next])) return sysEventTransfer_t::Refused;
        const auto candidate = SDL3_RetirementHead(polled[state.next], polledTags[state.next], lane, state.slice.serial, state.next);
        const auto expectedLane = lane == openq4::NativeInputLane::Keyboard ? sysInputDispositionLane_t::Keyboard : sysInputDispositionLane_t::Mouse;
        if (state.slice.lane != expectedLane || state.slice.epoch != candidate.tag.dispatchEpoch ||
            state.slice.streamToken != candidate.tag.streamToken) return sysEventTransfer_t::Refused;
        out = candidate;
        return sysEventTransfer_t::Ready;
    }
    if (count != 0) return sysEventTransfer_t::Refused; // Unread legacy slice ownership is not inferred.
    if (head == tail) return sysEventTransfer_t::Empty;
    if (!serials[tail] || !SDL3_RetirementValue(queue[tail], tags[tail])) return sysEventTransfer_t::Refused;
    out = SDL3_RetirementHead(queue[tail], tags[tail], lane, serials[tail], static_cast<unsigned>(tail));
    return sysEventTransfer_t::Ready;
}
template<class Value, class Tag, class Output>
static sysEventTransfer_t SDL3_TakeRetirementInput(sdlInputDispositionState_t& state, int count,
    Value (&polled)[SDL3_INPUT_QUEUE_SIZE], Tag (&polledTags)[SDL3_INPUT_QUEUE_SIZE],
    Value (&queue)[SDL3_INPUT_QUEUE_SIZE], Tag (&tags)[SDL3_INPUT_QUEUE_SIZE],
    std::uint64_t (&serials)[SDL3_INPUT_QUEUE_SIZE], int head, int& tail, openq4::NativeInputLane lane,
    openq4::NativeInputRoute& route, const openq4::NativeInputRoute::CancellationPermit& permit, Output& out) {
    openq4::NativeInputHead actual;
    const auto status = SDL3_PeekRetirementInput(state,count,polled,polledTags,queue,tags,serials,head,tail,lane,actual);
    if (status != sysEventTransfer_t::Ready) return status;
    if (!route.AllowsCancellation(permit,actual)) return sysEventTransfer_t::Refused;
    if (state.checked) {
        const auto owned = SDL3_TrackedInput(polled[state.next],polledTags[state.next]);
        polled[state.next] = {}; polledTags[state.next] = {}; ++state.next;
        out = owned;
    } else {
        const auto owned = SDL3_TrackedInput(queue[tail],tags[tail]);
        queue[tail] = {}; tags[tail] = {}; serials[tail] = 0;
        tail = (tail + 1) & SDL3_INPUT_QUEUE_MASK;
        out = owned;
    }
    return sysEventTransfer_t::Ready;
}
sysEventTransfer_t Sys_PeekKeyboardInputForRetirement(openq4::NativeInputHead& out) noexcept {
    SDL3_InputStorageLock lock;
    return SDL3_PeekRetirementInput(s_keyboardDispositionState,s_polledKeyboardCount,s_polledKeyboard,s_polledKeyboardDisposition,
        s_keyboardQueue,s_keyboardDisposition,s_keyboardRetirementSerials,s_keyboardHead,s_keyboardTail,openq4::NativeInputLane::Keyboard,out);
}
sysEventTransfer_t Sys_PeekMouseInputForRetirement(openq4::NativeInputHead& out) noexcept {
    SDL3_InputStorageLock lock;
    return SDL3_PeekRetirementInput(s_mouseDispositionState,s_polledMouseCount,s_polledMouse,s_polledMouseDisposition,
        s_mouseQueue,s_mouseDisposition,s_mouseRetirementSerials,s_mouseHead,s_mouseTail,openq4::NativeInputLane::Mouse,out);
}
sysEventTransfer_t Sys_TakeKeyboardInputForRetirement(openq4::NativeInputRoute& route,
    const openq4::NativeInputRoute::CancellationPermit& permit, sysKeyboardInputDisposition_t& out) noexcept {
    SDL3_InputStorageLock lock;
    return SDL3_TakeRetirementInput(s_keyboardDispositionState,s_polledKeyboardCount,s_polledKeyboard,s_polledKeyboardDisposition,
        s_keyboardQueue,s_keyboardDisposition,s_keyboardRetirementSerials,s_keyboardHead,s_keyboardTail,openq4::NativeInputLane::Keyboard,route,permit,out);
}
sysEventTransfer_t Sys_TakeMouseInputForRetirement(openq4::NativeInputRoute& route,
    const openq4::NativeInputRoute::CancellationPermit& permit, sysMouseInputDisposition_t& out) noexcept {
    SDL3_InputStorageLock lock;
    return SDL3_TakeRetirementInput(s_mouseDispositionState,s_polledMouseCount,s_polledMouse,s_polledMouseDisposition,
        s_mouseQueue,s_mouseDisposition,s_mouseRetirementSerials,s_mouseHead,s_mouseTail,openq4::NativeInputLane::Mouse,route,permit,out);
}
static bool SDL3_EndRetirementInput(const sysInputDispositionSlice_t& expected, sdlInputDispositionState_t& state, int& count) {
    SDL3_InputStorageLock lock;
    if (!Sys_EventDispositionBoundThread() || !state.checked || !expected.serial || expected != state.slice ||
        count < 0 || expected.count != static_cast<unsigned>(count) || state.next != expected.count) return false;
    state = {}; count = 0;
    return true; // Empty metadata only, never a receipt for queued input.
}
bool Sys_EndKeyboardInputForRetirement(const sysInputDispositionSlice_t& expected) noexcept {
    return SDL3_EndRetirementInput(expected,s_keyboardDispositionState,s_polledKeyboardCount);
}
bool Sys_EndMouseInputForRetirement(const sysInputDispositionSlice_t& expected) noexcept {
    return SDL3_EndRetirementInput(expected,s_mouseDispositionState,s_polledMouseCount);
}

int Sys_PollKeyboardInputEvents(void) {
    SDL3_InputStorageLock lock;
    if (s_keyboardDispositionState.checked) { Sys_InvalidateEventQueue(); return 0; }
    s_polledKeyboardCount = 0;
    while (s_keyboardTail != s_keyboardHead && s_polledKeyboardCount < SDL3_INPUT_QUEUE_SIZE) {
        if (!s_keyboardDisposition[s_keyboardTail].Empty()) { Sys_InvalidateEventQueue(); break; }
        s_polledKeyboard[s_polledKeyboardCount] = s_keyboardQueue[s_keyboardTail];
        s_polledKeyboardDisposition[s_polledKeyboardCount] = {};
        s_keyboardQueue[s_keyboardTail] = {};
        s_keyboardRetirementSerials[s_keyboardTail] = 0;
        ++s_polledKeyboardCount;
        s_keyboardTail = (s_keyboardTail + 1) & SDL3_INPUT_QUEUE_MASK;
    }
    return s_polledKeyboardCount;
}

int Sys_ReturnKeyboardInputEvent(const int n, int &ch, bool &state) {
    sdlKeyboardEvent_t value{};
    {
        SDL3_InputStorageLock lock;
        if (s_keyboardDispositionState.checked) {
            Sys_InvalidateEventQueue(); ch = 0; state = false; return 0;
        }
        if (n < 0 || n >= s_polledKeyboardCount) { ch = 0; state = false; return 0; }
        if (!s_polledKeyboardDisposition[n].Empty()) {
            Sys_InvalidateEventQueue(); ch = 0; state = false; return 0;
        }
        value = s_polledKeyboard[n];
    }
    ch = value.key; state = value.down;
    if (ch <= 0 || ch >= K_LAST_KEY) { ch = 0; state = false; return 0; }
    if (SDL3_DeferredKeyboardKey(ch)) Sys_QueEvent(value.time, SE_KEY, ch, state, 0, NULL);
    return ch;
}

void Sys_EndKeyboardInputEvents(void) {
    SDL3_InputStorageLock lock;
    if (s_keyboardDispositionState.checked) { Sys_InvalidateEventQueue(); return; }
    s_polledKeyboardCount = 0;
}

int Sys_PollMouseInputEvents(void) {
#if defined(OPENQ4_SDL3_POSIX_HOST)
    (void)Sys_SDL_PumpEvents();
#endif
    SDL3_InputStorageLock lock;
    if (s_mouseDispositionState.checked) { Sys_InvalidateEventQueue(); return 0; }
    s_polledMouseCount = 0;
    while (s_mouseTail != s_mouseHead && s_polledMouseCount < SDL3_INPUT_QUEUE_SIZE) {
        if (!s_mouseDisposition[s_mouseTail].Empty()) { Sys_InvalidateEventQueue(); break; }
        s_polledMouse[s_polledMouseCount] = s_mouseQueue[s_mouseTail];
        s_polledMouseDisposition[s_polledMouseCount] = {};
        s_mouseQueue[s_mouseTail] = {};
        s_mouseRetirementSerials[s_mouseTail] = 0;
        ++s_polledMouseCount;
        s_mouseTail = (s_mouseTail + 1) & SDL3_INPUT_QUEUE_MASK;
    }
    return s_polledMouseCount;
}

int Sys_ReturnMouseInputEvent(const int n, int &action, int &value) {
    SDL3_InputStorageLock lock;
    if (s_mouseDispositionState.checked) {
        Sys_InvalidateEventQueue(); action = 0; value = 0; return 0;
    }
    if (n < 0 || n >= s_polledMouseCount) { action = 0; value = 0; return 0; }
    if (!s_polledMouseDisposition[n].Empty()) {
        Sys_InvalidateEventQueue(); action = 0; value = 0; return 0;
    }
    action = s_polledMouse[n].action; value = s_polledMouse[n].value;
    if (!SDL3_ShouldQueueMousePoll(action, value)) { action = 0; value = 0; return 0; }
    return 1;
}

void Sys_EndMouseInputEvents(void) {
    SDL3_InputStorageLock lock;
    if (s_mouseDispositionState.checked) { Sys_InvalidateEventQueue(); return; }
    s_polledMouseCount = 0;
}

int Sys_PollJoystickInputEvents(void) {
	if (!in_joystick.GetBool()) {
		s_polledJoystickCount = 0;
		return 0;
	}

	Sys_EnterCriticalSection(CRITICAL_SECTION_ONE);
	s_polledJoystickCount = 0;
	for (int axis = 0; axis < MAX_JOYSTICK_AXIS; ++axis) {
		s_polledJoystick[s_polledJoystickCount].axis = axis;
		s_polledJoystick[s_polledJoystickCount].value = SDL3_ReadJoystickAxis(axis);
		s_polledJoystickCount++;
	}
	Sys_LeaveCriticalSection(CRITICAL_SECTION_ONE);

	return s_polledJoystickCount;
}

int Sys_ReturnJoystickInputEvent(const int n, int &axis, int &value) {
	if (n < 0 || n >= s_polledJoystickCount) {
		axis = 0;
		value = 0;
		return 0;
	}

	axis = s_polledJoystick[n].axis;
	value = s_polledJoystick[n].value;
	return 1;
}

void Sys_EndJoystickInputEvents(void) {
}

bool Sys_GetJoystickAxisState(int axis, int &value) {
	if (!in_joystick.GetBool() || axis < 0 || axis >= MAX_JOYSTICK_AXIS) {
		value = 0;
		return false;
	}

	Sys_EnterCriticalSection(CRITICAL_SECTION_ONE);
	value = SDL3_ReadJoystickAxis(axis);
	Sys_LeaveCriticalSection(CRITICAL_SECTION_ONE);

	return true;
}

bool Sys_SetJoystickRumble(float lowFrequency, float highFrequency, int durationMsec) {
	const int now = Sys_Milliseconds();

	if (!in_joystick.GetBool() || !in_joystickRumble.GetBool() || durationMsec <= 0) {
		if (s_joystickRumbleActive) {
			SDL3_StopControllerRumble();
		}
		return false;
	}

	if (!SDL3_HasRumbleTarget()) {
		SDL3_ResetRumbleState();
		return false;
	}

	const float scale = SDL3_GetEffectiveRumbleScale();
	const Uint16 low = SDL3_ClampRumbleValue(SDL3_ClampUnit(lowFrequency) * scale);
	const Uint16 high = SDL3_ClampRumbleValue(SDL3_ClampUnit(highFrequency) * scale);
	if (low == 0 && high == 0) {
		if (s_joystickRumbleActive) {
			SDL3_StopControllerRumble();
		}
		return true;
	}

	SDL3_ResetExpiredRumbleState(now);

	const int untilTime = now + durationMsec;
	if (SDL3_ShouldSkipRumbleUpdate(low, high, now, untilTime)) {
		return true;
	}

	if (!SDL3_SendControllerRumble(low, high, static_cast<Uint32>(durationMsec))) {
		SDL3_ResetRumbleState();
		return false;
	}

	s_joystickRumbleActive = true;
	s_joystickRumbleLow = low;
	s_joystickRumbleHigh = high;
	s_joystickRumbleUntilTime = untilTime;
	s_joystickRumbleLastUpdateTime = now;
	return true;
}












void GLimp_PreserveWindowOnShutdown(bool preserve) {
	s_preserveWindowOnShutdown = preserve;
}





/*
===========================================================

SMP acceleration

===========================================================
*/

#if !defined(OPENQ4_SDL3_POSIX_HOST)
static void GLimp_RenderThreadWrapper(void) {
	win32.glimpRenderThread();
	(void)SDL_GL_MakeCurrent(s_sdlWindow, NULL);
}
#endif

bool GLimp_SpawnRenderThread(void (*function)(void)) {
#if defined(OPENQ4_SDL3_POSIX_HOST)
	(void)function;
	return false;
#else
	SYSTEM_INFO info;

	GetSystemInfo(&info);
	if (info.dwNumberOfProcessors < 2) {
		return false;
	}

	win32.renderCommandsEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	win32.renderCompletedEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	win32.renderActiveEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	win32.glimpRenderThread = function;

	win32.renderThreadHandle = CreateThread(
		NULL,
		0,
		(LPTHREAD_START_ROUTINE)GLimp_RenderThreadWrapper,
		0,
		0,
		&win32.renderThreadId);

	if (!win32.renderThreadHandle) {
		common->Error("GLimp_SpawnRenderThread: failed");
	}

	SetThreadPriority(win32.renderThreadHandle, THREAD_PRIORITY_ABOVE_NORMAL);
	return true;
#endif
}

void *GLimp_BackEndSleep(void) {
#if defined(OPENQ4_SDL3_POSIX_HOST)
	return NULL;
#else
	void *data;

	ResetEvent(win32.renderActiveEvent);
	SetEvent(win32.renderCompletedEvent);
	WaitForSingleObject(win32.renderCommandsEvent, INFINITE);

	ResetEvent(win32.renderCompletedEvent);
	ResetEvent(win32.renderCommandsEvent);
	data = win32.smpData;
	SetEvent(win32.renderActiveEvent);

	return data;
#endif
}

void GLimp_FrontEndSleep(void) {
#if !defined(OPENQ4_SDL3_POSIX_HOST)
	WaitForSingleObject(win32.renderCompletedEvent, INFINITE);
#endif
}

void GLimp_WakeBackEnd(void *data) {
#if defined(OPENQ4_SDL3_POSIX_HOST)
	(void)data;
#else
	int r;

	win32.smpData = data;
	r = WaitForSingleObject(win32.renderActiveEvent, 0);
	if (r == WAIT_OBJECT_0) {
		common->FatalError("GLimp_WakeBackEnd: already signaled");
	}

	r = WaitForSingleObject(win32.renderCommandsEvent, 0);
	if (r == WAIT_OBJECT_0) {
		common->FatalError("GLimp_WakeBackEnd: commands already signaled");
	}

	SetEvent(win32.renderCommandsEvent);

	r = WaitForSingleObject(win32.renderActiveEvent, 5000);
	if (r == WAIT_TIMEOUT) {
		common->FatalError("GLimp_WakeBackEnd: WAIT_TIMEOUT");
	}
#endif
}

/*
===============================================================================

	Phase B5b window services: the engine-owned half of the window/context
	seam (docs/dev/plans/2026-07-16-vulkan-renderer-phase-b.md). The context
	half lives in renderer/OpenGL/gl_ContextSDL3.cpp and drives these
	callbacks; at Phase B8 they cross the renderer-module boundary through
	renderImport_t.windowServices.

===============================================================================
*/

static bool s_teardownPreserveWindow = false;

static void SDL3_FillWindowInfo(renderModuleWindowInfo_t *outInfo) {
	memset(outInfo, 0, sizeof(*outInfo));
	outInfo->hasWindow = s_sdlWindow != NULL;
	outInfo->sdlWindow = s_sdlWindow;
#if !defined(OPENQ4_SDL3_POSIX_HOST)
	outInfo->nativeWindowHandle = win32.hWnd;
	outInfo->nativeDisplayHandle = win32.hDC;
#endif
	outInfo->pixelWidth = engineWindowState.vidWidth;
	outInfo->pixelHeight = engineWindowState.vidHeight;
	outInfo->uiViewportX = engineWindowState.uiViewportX;
	outInfo->uiViewportY = engineWindowState.uiViewportY;
	outInfo->uiViewportWidth = engineWindowState.uiViewportWidth;
	outInfo->uiViewportHeight = engineWindowState.uiViewportHeight;
}

static void SDL3_ParmsFromWindowParms(const renderWindowParms_t *src, glimpParms_t &dst) {
	memset(&dst, 0, sizeof(dst));
	dst.width = src->width;
	dst.height = src->height;
	dst.fullScreen = src->fullScreen;
	dst.borderless = src->borderless;
	dst.hiddenWindow = src->hiddenWindow;
	dst.stereo = src->stereo;
	dst.displayHz = src->displayHz;
	dst.multiSamples = src->multiSamples;
}

static void SDL3_ApplyFramebufferDesc(const renderFramebufferDesc_t *desc) {
	SDL_GL_ResetAttributes();
	(void)SDL_GL_SetAttribute(SDL_GL_RED_SIZE, desc->redBits);
	(void)SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, desc->greenBits);
	(void)SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, desc->blueBits);
	(void)SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, desc->alphaBits);
	(void)SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, desc->depthBits);
	(void)SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, desc->stencilBits);
	(void)SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, desc->doubleBuffer ? 1 : 0);
	if (desc->stereo) {
		(void)SDL_GL_SetAttribute(SDL_GL_STEREO, 1);
	}
	if (desc->multiSamples > 1) {
		(void)SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
		(void)SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, desc->multiSamples);
	}
	if (desc->glESProfile) {
		// An ES context is always an explicit version request: there is no
		// meaningful "unversioned ES" and SDL must know before it picks a
		// driver, because on desktop this routes through EGL/ANGLE rather
		// than the platform's desktop-GL path.
		(void)SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, desc->glMajor);
		(void)SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, desc->glMinor);
		(void)SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
	} else if (desc->explicitGLVersion) {
		(void)SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, desc->glMajor);
		(void)SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, desc->glMinor);
		(void)SDL_GL_SetAttribute(
			SDL_GL_CONTEXT_PROFILE_MASK,
			desc->glCoreProfile ? SDL_GL_CONTEXT_PROFILE_CORE : SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
	}
	(void)SDL_GL_SetAttribute(
		SDL_GL_CONTEXT_FLAGS,
		desc->glDebugContext ? SDL_GL_CONTEXT_DEBUG_FLAG : 0);
}

static bool SDL3_WindowServices_PrepareWindowSystem(void) {
	SDL3_SetMouseHintDefaults();
	Sys_SDL_ApplyVideoHintDefaults();

	if (!s_sdlVideoReferenceHeld) {
		// SDL video initialization is reference counted. The game renderer must
		// acquire its own reference instead of adopting the splash/console ref;
		// otherwise the window teardown can invalidate their cached SDL objects.
		if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
			common->Printf("SDL3: failed to initialize video subsystem: %s\n", SDL_GetError());
			return false;
		}
		openq4::NativeInputBeforeWindowChange();
		s_sdlVideoReferenceHeld = true;
	}
	SDL3_RegisterLifecycleEventWatch();
	SDL3_UpdateVideoDriverProfile();
	SDL3_PrintVideoDriverSummary();
	SDL3_PrintGraphicsBridgeSummary();
	SDL3_ApplySteamDeckPerformanceDefaults();

	if (!s_sdlDiagnosticCommandsRegistered) {
		cmdSystem->AddCommand("listDisplays", SDL3_ListDisplays_f, CMD_FL_SYSTEM, "lists SDL3 displays and monitor indices");
		cmdSystem->AddCommand("listDisplayModes", SDL3_ListDisplayModes_f, CMD_FL_SYSTEM, "lists SDL3 fullscreen display modes (optional display index)");
		cmdSystem->AddCommand("listControllers", SDL3_ListControllers_f, CMD_FL_SYSTEM, "lists SDL3 controller, sensor, touchpad, and battery diagnostics");
		cmdSystem->AddCommand("sdl3MouseCaptureDiagnostics", SDL3_MouseCaptureDiagnostics_f, CMD_FL_SYSTEM, "toggles SDL3 mouse capture and prints backend state; optional repeat count is clamped to 1..8");
		s_sdlDiagnosticCommandsRegistered = true;
	}

	if (!s_sdlDisplaySummaryLogged) {
		SDL3_PrintDisplayList();
		s_sdlDisplaySummaryLogged = true;
	}

	SDL3_InitDesktopMode();
	return true;
}

// which API the live window was created for; a preserved window of the
// wrong kind cannot be reused across a renderer-API switch
static int s_sdlWindowSurfaceKind = RENDER_SURFACE_GL;

#if defined(OPENQ4_SDL3_DARWIN_HOST)
// macOS has no system Vulkan driver; the renderer runs on MoltenVK, a
// Vulkan-on-Metal translation layer shipped inside the package. SDL's own
// search list already starts at @executable_path/../Frameworks/libMoltenVK.dylib,
// but loose (non-app-bundle) binaries may stage the library either beside the
// executable or in a Frameworks child directory. The renderer module must end
// up on the SAME image SDL creates the surface with. Pinning the resolved path
// removes both hazards. A system Vulkan loader, when the user has the SDK
// installed, still wins: the hint is only set when a bundled dylib is present.
static void SDL3_PinBundledMoltenVKLibrary(void) {
	if (SDL_GetHint(SDL_HINT_VULKAN_LIBRARY) != NULL) {
		return;
	}

	idStr exeDir = Sys_EXEPath();
	exeDir.StripFilename();

	const char *relativeCandidates[] = {
		"/../Frameworks/libMoltenVK.dylib",
		"/Frameworks/libMoltenVK.dylib",
		"/libMoltenVK.dylib",
	};
	for (int i = 0; i < (int)(sizeof(relativeCandidates) / sizeof(relativeCandidates[0])); i++) {
		idStr candidate = exeDir + relativeCandidates[i];
		FILE *probe = fopen(candidate.c_str(), "rb");
		if (probe == NULL) {
			continue;
		}
		fclose(probe);
		if (!SDL_SetHint(SDL_HINT_VULKAN_LIBRARY, candidate.c_str())) {
			common->Warning("SDL3: failed to pin bundled MoltenVK (%s)", SDL_GetError());
			return;
		}
		common->Printf("SDL3: bundled MoltenVK pinned at %s\n", candidate.c_str());
		return;
	}
}
#endif

static bool SDL3_WindowServices_CreateWindowForFramebuffer(const renderFramebufferDesc_t *desc, const renderWindowParms_t *parms,
														   renderModuleWindowInfo_t *outInfo, bool *outReusedPreservedWindow) {
	const int requestedSurfaceKind = desc != NULL ? desc->surfaceKind : RENDER_SURFACE_GL;
#if defined(OPENQ4_SDL3_DARWIN_HOST)
	if (requestedSurfaceKind == RENDER_SURFACE_VULKAN) {
		SDL3_PinBundledMoltenVKLibrary();
	}
#endif
	if (requestedSurfaceKind == RENDER_SURFACE_GL) {
		SDL3_ApplyFramebufferDesc(desc);
	}

	if (s_sdlWindow != NULL && s_sdlWindowSurfaceKind != requestedSurfaceKind) {
		common->Printf("SDL3: preserved window is for a different rendering API; recreating\n");
		SDL3_RetireNativeWindowPublication();
		SDL_DestroyWindow(s_sdlWindow);
		s_sdlWindow = NULL;
	}

	const bool reusePreserved = s_sdlWindow != NULL;
	if (outReusedPreservedWindow != NULL) {
		*outReusedPreservedWindow = reusePreserved;
	}

	if (reusePreserved) {
		common->Printf("SDL3: reusing preserved %s window\n", requestedSurfaceKind == RENDER_SURFACE_VULKAN ? "Vulkan" : "OpenGL");
	} else {
		SDL_WindowFlags flags = (requestedSurfaceKind == RENDER_SURFACE_VULKAN ? SDL_WINDOW_VULKAN : SDL_WINDOW_OPENGL)
				| SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_HIDDEN;
		if (!parms->fullScreen && parms->borderless) {
			flags |= SDL_WINDOW_BORDERLESS;
		}

		s_sdlWindow = SDL_CreateWindow(GAME_WINDOW_TITLE, parms->width, parms->height, flags);
		if (!s_sdlWindow) {
			return false;
		}
		s_sdlWindowSurfaceKind = requestedSurfaceKind;

		if (!parms->fullScreen && !parms->hiddenWindow) {
			int targetX = win32.win_xpos.GetInteger();
			int targetY = win32.win_ypos.GetInteger();
			int targetWidth = parms->width;
			int targetHeight = parms->height;

			const sdl3DisplaySelection_t selectedDisplay = SDL3_ResolveTargetDisplay(false);
			if (selectedDisplay.id != 0) {
				SDL_Rect bounds;
				if (SDL3_GetDisplayWindowedPlacementBounds(selectedDisplay.id, bounds)) {
					const bool needsRecoveryPlacement = !SDL3_WindowRectIntersectsAnyDisplay(targetX, targetY, targetWidth, targetHeight);
					const bool recenterIfOutside = (r_screen.GetInteger() >= 0) || needsRecoveryPlacement;
					SDL3_ConstrainWindowRectToBounds(targetX, targetY, targetWidth, targetHeight, bounds, recenterIfOutside);
				}
			}

			(void)SDL_SetWindowSize(s_sdlWindow, targetWidth, targetHeight);
			(void)SDL3_SetWindowPositionCompat(
				targetX,
				targetY,
				selectedDisplay.id,
				r_screen.GetInteger() >= 0,
				"place initial window");
		}
	}

	SDL3_FillWindowInfo(outInfo);
	return true;
}

static void SDL3_WindowServices_DestroyAttemptWindow(void) {
	if (s_sdlWindow != NULL) {
		SDL3_RetireNativeWindowPublication();
		SDL_DestroyWindow(s_sdlWindow);
		s_sdlWindow = NULL;
	}
}

static bool SDL3_WindowServices_ApplyScreenParms(const renderWindowParms_t *parms) {
	glimpParms_t glimpParms;
	SDL3_ParmsFromWindowParms(parms, glimpParms);
	return SDL3_ApplyScreenParms(glimpParms);
}

static void SDL3_WindowServices_RefreshNativeWindowHandles(renderModuleWindowInfo_t *outInfo) {
	SDL3_UpdateNativeWindowHandles();
	if (outInfo != NULL) {
		SDL3_FillWindowInfo(outInfo);
	}
}

static void SDL3_WindowServices_NotifyWindowReady(void) {
	openq4::NativeInputBeforeWindowChange();
	win32.activeApp = true;
	s_sdlFocusInputReleased = false;
	win32.wglErrors = 0;
}

static void SDL3_WindowServices_BeginWindowTeardown(void) {
	s_teardownPreserveWindow = s_preserveWindowOnShutdown && s_sdlWindow != NULL;

	SDL3_DisableWindowAspectSnap();
	IN_DeactivateMouse();
	SDL3_ShutdownControllerSubsystems();
	openq4::NativeInputBeforeWindowChange();
	s_sdlAppInBackground = false;
	if (!s_teardownPreserveWindow) {
		(void)SDL3_LeaveFullscreenAndRestoreDesktopMode();
	}
	if (s_sdlWindow && s_sdlTextInputActive) {
		(void)SDL_ClearComposition(s_sdlWindow);
		(void)SDL_StopTextInput(s_sdlWindow);
		s_sdlTextInputActive = false;
	}
}

static void SDL3_WindowServices_FinishWindowTeardown(void) {
	if (s_sdlWindow && !s_teardownPreserveWindow) {
		SDL3_RetireNativeWindowPublication();
		SDL_DestroyWindow(s_sdlWindow);
		s_sdlWindow = NULL;
	}

	if (s_sdlVideoReferenceHeld && !s_teardownPreserveWindow) {
		SDL3_UnregisterLifecycleEventWatch();
		SDL_QuitSubSystem(SDL_INIT_VIDEO);
		openq4::NativeInputBeforeWindowChange();
		s_sdlVideoReferenceHeld = false;
		s_sdlVideoDriver = SDL3_VIDEO_DRIVER_UNKNOWN;
		idStr::snPrintf(s_sdlVideoDriverName, sizeof(s_sdlVideoDriverName), "unknown");
		s_sdlGraphicsBridgeSummaryLogged = false;
	}

	if (!s_teardownPreserveWindow) {
		win32.hWnd = NULL;
	}
	win32.hDC = NULL;
	win32.hGLRC = NULL;
	if (!s_teardownPreserveWindow) {
		win32.cdsFullscreen = false;
		SDL3_SetFullscreenState( false );
	}

	SDL3_ClearInputQueues();
	s_teardownPreserveWindow = false;
}

static void SDL3_WindowServices_GetDesktopResolution(int *width, int *height) {
	(void)Sys_GetDesktopResolution(width, height);
}

static bool SDL3_WindowServices_PreferCompatibilityFallbackFirst(const char **outMessage) {
	if (SDL3_IsNativeWaylandVideoDriver()) {
		if (outMessage != NULL) {
			*outMessage = "SDL3: native Wayland r_glTier auto will try the unversioned compatibility fallback before explicit profile contexts.\n";
		}
		return true;
	}
	if (s_sdlVideoDriver == SDL3_VIDEO_DRIVER_COCOA) {
		// Apple OpenGL only offers 2.1 compatibility or 3.2+/4.1 core, so
		// every versioned compatibility candidate above 2.1 is guaranteed to
		// fail; try the unversioned compatibility fallback first.
		if (outMessage != NULL) {
			*outMessage = "SDL3: macOS r_glTier auto will try the unversioned compatibility fallback before explicit profile contexts.\n";
		}
		return true;
	}
	return false;
}

static void SDL3_WindowServices_CountContextError(void) {
	win32.wglErrors++;
}

static void *SDL3_WindowServices_CreateGLContext(void) {
	if (!s_sdlWindow) {
		return NULL;
	}
	return (void *)SDL_GL_CreateContext(s_sdlWindow);
}

static bool SDL3_WindowServices_MakeGLContextCurrent(void *context) {
	if (!s_sdlWindow) {
		return false;
	}
	return SDL_GL_MakeCurrent(s_sdlWindow, (SDL_GLContext)context);
}

static void SDL3_WindowServices_DestroyGLContext(void *context) {
	if (context != NULL) {
		(void)SDL_GL_DestroyContext((SDL_GLContext)context);
	}
}

static bool SDL3_WindowServices_IsGLContextCurrent(void *context) {
	return s_sdlWindow != NULL && context != NULL
		&& SDL_GL_GetCurrentWindow() == s_sdlWindow
		&& SDL_GL_GetCurrentContext() == (SDL_GLContext)context;
}

static bool SDL3_WindowServices_SwapGLWindow(void) {
	return s_sdlWindow != NULL && SDL_GL_SwapWindow(s_sdlWindow);
}

static bool SDL3_WindowServices_SetGLSwapInterval(int interval) {
	return SDL_GL_SetSwapInterval(interval);
}

static bool SDL3_WindowServices_GetGLSwapInterval(int *outInterval) {
	return SDL_GL_GetSwapInterval(outInterval);
}

static bool SDL3_WindowServices_GetGLAttribute(int attribute, int *outValue) {
	SDL_GLAttr sdlAttribute;
	switch (attribute) {
		case RENDER_GLATTR_CONTEXT_MAJOR_VERSION:	sdlAttribute = SDL_GL_CONTEXT_MAJOR_VERSION; break;
		case RENDER_GLATTR_CONTEXT_MINOR_VERSION:	sdlAttribute = SDL_GL_CONTEXT_MINOR_VERSION; break;
		case RENDER_GLATTR_CONTEXT_PROFILE_MASK:	sdlAttribute = SDL_GL_CONTEXT_PROFILE_MASK; break;
		case RENDER_GLATTR_CONTEXT_FLAGS:			sdlAttribute = SDL_GL_CONTEXT_FLAGS; break;
		case RENDER_GLATTR_MULTISAMPLE_BUFFERS:		sdlAttribute = SDL_GL_MULTISAMPLEBUFFERS; break;
		case RENDER_GLATTR_MULTISAMPLE_SAMPLES:		sdlAttribute = SDL_GL_MULTISAMPLESAMPLES; break;
		case RENDER_GLATTR_DEPTH_SIZE:				sdlAttribute = SDL_GL_DEPTH_SIZE; break;
		case RENDER_GLATTR_STENCIL_SIZE:			sdlAttribute = SDL_GL_STENCIL_SIZE; break;
		default: return false;
	}
	if (!SDL_GL_GetAttribute(sdlAttribute, outValue)) {
		return false;
	}
	if (attribute == RENDER_GLATTR_CONTEXT_PROFILE_MASK) {
		switch (*outValue) {
			case SDL_GL_CONTEXT_PROFILE_CORE:			*outValue = RENDER_GLPROFILE_CORE; break;
			case SDL_GL_CONTEXT_PROFILE_COMPATIBILITY:	*outValue = RENDER_GLPROFILE_COMPATIBILITY; break;
			case SDL_GL_CONTEXT_PROFILE_ES:				*outValue = RENDER_GLPROFILE_ES; break;
			default:									*outValue = RENDER_GLPROFILE_DEFAULT; break;
		}
	}
	return true;
}

static void *SDL3_WindowServices_GetGLProcAddress(const char *name) {
	return (void *)SDL_GL_GetProcAddress(name);
}

static const char *SDL3_WindowServices_GetVideoErrorString(void) {
	return SDL_GetError();
}

static void SDL3_WindowServices_InitInput(void) {
	Sys_InitInput();
}

static void SDL3_WindowServices_ShutdownInput(void) {
	Sys_ShutdownInput();
}

static void SDL3_WindowServices_GrabMouseCursor(bool grabIt) {
	Sys_GrabMouseCursor(grabIt);
}

static bool SDL3_WindowServices_GetVulkanInstanceExtensions(const char **outNames, int maxNames, int *outCount) {
	if (outCount != NULL) {
		*outCount = 0;
	}
	Uint32 sdlCount = 0;
	const char *const *names = SDL_Vulkan_GetInstanceExtensions(&sdlCount);
	if (names == NULL) {
		common->Warning("SDL3: SDL_Vulkan_GetInstanceExtensions failed: %s", SDL_GetError());
		return false;
	}
	if (outCount != NULL) {
		*outCount = (int)sdlCount;
	}
	if (outNames != NULL) {
		for (int i = 0; i < (int)sdlCount && i < maxNames; i++) {
			outNames[i] = names[i];
		}
	}
	return true;
}

static bool SDL3_WindowServices_CreateVulkanSurface(void *vkInstance, unsigned long long *outVkSurface) {
	if (outVkSurface != NULL) {
		*outVkSurface = 0;
	}
	if (s_sdlWindow == NULL || vkInstance == NULL || outVkSurface == NULL) {
		return false;
	}
	if (s_sdlWindowSurfaceKind != RENDER_SURFACE_VULKAN) {
		common->Warning("SDL3: CreateVulkanSurface on a window not created for Vulkan");
		return false;
	}
	VkSurfaceKHR surface = 0;
	if (!SDL_Vulkan_CreateSurface(s_sdlWindow, (VkInstance)vkInstance, NULL, &surface)) {
		common->Warning("SDL3: SDL_Vulkan_CreateSurface failed: %s", SDL_GetError());
		return false;
	}
	*outVkSurface = (unsigned long long)surface;
	return true;
}

#ifndef ID_DEDICATED
static bool SDL3_PlacementError(const char* reason, char* error, int size) {
	if (error && size>0) idStr::snPrintf(error,size,"%s",reason);
	return false;
}
static sysWindowPlacementSnapshot_t SDL3_ReadPlacementSettings() {
	sysWindowPlacementSnapshot_t value;
	value.x=win32.win_xpos.GetInteger(); value.y=win32.win_ypos.GetInteger();
	value.width=r_windowWidth.GetInteger(); value.height=r_windowHeight.GetInteger();
	value.normalX=s_windowedPlacement.x; value.normalY=s_windowedPlacement.y;
	value.normalWidth=s_windowedPlacement.width; value.normalHeight=s_windowedPlacement.height; value.normalValid=s_windowedPlacement.valid;
	return value;
}
static bool SDL3_SamePlacement(const sysWindowPlacementSnapshot_t& a, const sysWindowPlacementSnapshot_t& b) {
	return a.x==b.x && a.y==b.y && a.width==b.width && a.height==b.height &&
		a.normalX==b.normalX && a.normalY==b.normalY && a.normalWidth==b.normalWidth && a.normalHeight==b.normalHeight && a.normalValid==b.normalValid;
}
bool Sys_WindowPlacementLeaseActive() { return s_windowPlacementLease!=0; }
bool Sys_BeginWindowPlacementLease(uint64_t token, sysWindowPlacementSnapshot_t* baseline, char* error, int errorSize) {
	if (!token || !baseline || s_windowPlacementLease) return SDL3_PlacementError("Window placement is already leased or the request is invalid",error,errorSize);
	s_windowPlacementBaseline=SDL3_ReadPlacementSettings(); s_windowPlacementLease=token;
	*baseline=s_windowPlacementBaseline; if (error && errorSize>0) error[0]='\0'; return true;
}
bool Sys_ReadWindowPlacementLease(uint64_t token, sysWindowPlacementSnapshot_t* current, char* error, int errorSize) {
	if (!token || token!=s_windowPlacementLease || !current) return SDL3_PlacementError("Window placement lease is unavailable",error,errorSize);
	*current=SDL3_ReadPlacementSettings(); if (error && errorSize>0) error[0]='\0'; return true;
}
bool Sys_BuildWindowPlacementCommit(uint64_t token, const sysWindowPlacementSnapshot_t* expectedCurrent,
	const renderWindowState_s* actual, sysWindowPlacementSnapshot_t* committed, char* error, int errorSize) {
	if (!token || token!=s_windowPlacementLease || !expectedCurrent || !actual || !committed || !s_sdlWindow)
		return SDL3_PlacementError("Window placement commit requires a live lease and window",error,errorSize);
	if (!SDL3_SamePlacement(SDL3_ReadPlacementSettings(),*expectedCurrent))
		return SDL3_PlacementError("Window placement changed outside the settings attempt",error,errorSize);
	if (actual->logicalWidth<=0 || actual->logicalHeight<=0 || actual->minimized)
		return SDL3_PlacementError("Window placement commit requires valid actual dimensions",error,errorSize);
	sysWindowPlacementSnapshot_t result=*expectedCurrent;
	if (!actual->fullscreen && !actual->borderless && !actual->hidden && !actual->maximized) {
		result.width=actual->logicalWidth; result.height=actual->logicalHeight;
		if (actual->positionValid && SDL3_UseAbsoluteWindowPlacement()) {
			int top=0,left=0,bottom=0,right=0;
			bool borders=SDL_GetWindowBordersSize(s_sdlWindow,&top,&left,&bottom,&right);
#if defined(OPENQ4_SDL3_DARWIN_HOST)
			if (!borders) borders=Sys_SDL_GetNativeWindowBorders(s_sdlWindow,&top,&left,&bottom,&right);
#endif
			if (!borders || top<0 || left<0 || bottom<0 || right<0 || top>16384 || left>16384 || bottom>16384 || right>16384)
				return SDL3_PlacementError("Actual window frame offsets are unavailable",error,errorSize);
			result.x=SDL3_SaturateWindowCoordinate(int64_t(actual->windowX)-left);
			result.y=SDL3_SaturateWindowCoordinate(int64_t(actual->windowY)-top);
			result.normalX=result.x; result.normalY=result.y; result.normalWidth=result.width; result.normalHeight=result.height; result.normalValid=true;
		}
	}
	// Maximized/fullscreen/borderless/hidden observations never overwrite the
	// captured normal rectangle. SDL cannot expose its compositor-internal copy.
	*committed=result; if (error && errorSize>0) error[0]='\0'; return true;
}
bool Sys_ApplyWindowPlacementLease(uint64_t token, const sysWindowPlacementSnapshot_t* expectedCurrent,
	const sysWindowPlacementSnapshot_t* finalState, char* error, int errorSize) {
	if (!token || token!=s_windowPlacementLease || !expectedCurrent || !finalState)
		return SDL3_PlacementError("Window placement lease is unavailable",error,errorSize);
	if (!SDL3_SamePlacement(SDL3_ReadPlacementSettings(),*expectedCurrent))
		return SDL3_PlacementError("Window placement changed outside the settings attempt",error,errorSize);
	if (finalState->normalValid && (finalState->normalWidth<=0 || finalState->normalHeight<=0))
		return SDL3_PlacementError("The normal window restore rectangle is invalid",error,errorSize);
	// Validate all ownership before the first setter; a later setter refusal
	// remains a partial failure with the lease retained. No modified flags are
	// cleared, so unrelated/archive persistence remains the coordinator's choice.
	if (expectedCurrent->x!=finalState->x) win32.win_xpos.SetInteger(finalState->x);
	if (expectedCurrent->y!=finalState->y) win32.win_ypos.SetInteger(finalState->y);
	if (expectedCurrent->width!=finalState->width) r_windowWidth.SetInteger(finalState->width);
	if (expectedCurrent->height!=finalState->height) r_windowHeight.SetInteger(finalState->height);
	const auto observed=SDL3_ReadPlacementSettings();
	if (observed.x!=finalState->x || observed.y!=finalState->y || observed.width!=finalState->width || observed.height!=finalState->height)
		return SDL3_PlacementError("Window placement write was refused or normalized",error,errorSize);
	s_windowedPlacement.x=finalState->normalX; s_windowedPlacement.y=finalState->normalY;
	s_windowedPlacement.width=finalState->normalWidth; s_windowedPlacement.height=finalState->normalHeight; s_windowedPlacement.valid=finalState->normalValid;
	if (error && errorSize>0) error[0]='\0'; return true;
}
bool Sys_FinishWindowPlacementLease(uint64_t token, const sysWindowPlacementSnapshot_t* expectedCurrent,
	const sysWindowPlacementSnapshot_t* finalState, char* error, int errorSize) {
	if (!Sys_ApplyWindowPlacementLease(token,expectedCurrent,finalState,error,errorSize)) return false;
	s_windowPlacementLease=0; s_windowPlacementBaseline={}; return true;
}
#endif

// Strict settings operations are separate from legacy startup negotiation.
// Every observation comes from SDL; querying never persists placement CVars.
struct sdl3StrictDisplayList_t {
	int count = 0;
	SDL_DisplayID *ids = SDL_GetDisplays(&count);
	~sdl3StrictDisplayList_t() { if (ids) SDL_free(ids); }
	bool Valid() const { return ids && count > 0 && count <= 1024; }
	int Index(SDL_DisplayID display) const {
		if (Valid()) for (int i = 0; i < count; ++i) if (ids[i] == display) return i;
		return -1;
	}
};

static bool SDL3_StrictModePixels(const SDL_DisplayMode *mode, int &width, int &height) {
	if (!mode || mode->w <= 0 || mode->h <= 0 || !std::isfinite(mode->pixel_density) || mode->pixel_density <= 0) return false;
	const double w = std::floor(static_cast<double>(mode->w) * mode->pixel_density + 0.5);
	const double h = std::floor(static_cast<double>(mode->h) * mode->pixel_density + 0.5);
	if (!std::isfinite(w) || !std::isfinite(h) || w < 1 || h < 1 || w > idMath::INT_MAX || h > idMath::INT_MAX) return false;
	width = static_cast<int>(w); height = static_cast<int>(h);
	return true;
}

static void SDL3_QueryDisplayViewport(renderWindowState_t &state) {
	state.uiViewportX = state.uiViewportY = 0;
	state.uiViewportWidth = state.pixelWidth; state.uiViewportHeight = state.pixelHeight;
	if (!state.positionValid || !SDL3_UseAbsoluteWindowPlacement()) return;
	SDL_Rect bounds;
	const SDL_DisplayID display = SDL3_ResolveViewportDisplay();
	if (!display || !SDL_GetDisplayBounds(display, &bounds) || bounds.w <= 0 || bounds.h <= 0) return;
	// Same floor/ceil overlap policy as SDL3_UpdateDisplayViewport, evaluated
	// without publishing engine state or falling back to stale cached sizes.
	const int64_t left = state.windowX, top = state.windowY;
	const int64_t right = left + state.logicalWidth, bottom = top + state.logicalHeight;
	const int64_t displayRight = static_cast<int64_t>(bounds.x) + bounds.w;
	const int64_t displayBottom = static_cast<int64_t>(bounds.y) + bounds.h;
	const int64_t overlapLeft = left > bounds.x ? left : bounds.x;
	const int64_t overlapTop = top > bounds.y ? top : bounds.y;
	const int64_t overlapRight = right < displayRight ? right : displayRight;
	const int64_t overlapBottom = bottom < displayBottom ? bottom : displayBottom;
	if (overlapRight <= overlapLeft || overlapBottom <= overlapTop) return;
	const double scaleX = static_cast<double>(state.pixelWidth) / state.logicalWidth;
	const double scaleY = static_cast<double>(state.pixelHeight) / state.logicalHeight;
	const int x = SDL3_ClampViewportPixel(std::floor((overlapLeft-left)*scaleX), 0, state.pixelWidth);
	const int y = SDL3_ClampViewportPixel(std::floor((overlapTop-top)*scaleY), 0, state.pixelHeight);
	const int r = SDL3_ClampViewportPixel(std::ceil((overlapRight-left)*scaleX), x, state.pixelWidth);
	const int b = SDL3_ClampViewportPixel(std::ceil((overlapBottom-top)*scaleY), y, state.pixelHeight);
	if (r > x && b > y) {
		state.uiViewportX = x; state.uiViewportY = y;
		state.uiViewportWidth = r-x; state.uiViewportHeight = b-y;
	}
}

static bool SDL3_WindowServices_QueryWindowState(renderWindowState_t *outState) {
	if (!outState || !s_sdlWindow) return SDL_SetError("No SDL window or window-state output");
	renderWindowState_t state = {};
	state.displayId = SDL_GetDisplayForWindow(s_sdlWindow);
	sdl3StrictDisplayList_t displays;
	state.displayIndex = displays.Index(state.displayId);
	if (!state.displayId || state.displayIndex < 0) return SDL_SetError("The SDL window display is unavailable");
	const SDL_WindowFlags flags = SDL_GetWindowFlags(s_sdlWindow);
	state.windowFlags = static_cast<unsigned long long>(flags);
	state.fullscreen = (flags & SDL_WINDOW_FULLSCREEN) != 0;
	state.fullscreenDesktop = state.fullscreen && SDL_GetWindowFullscreenMode(s_sdlWindow) == NULL;
	state.borderless = (flags & SDL_WINDOW_BORDERLESS) != 0;
	state.hidden = (flags & SDL_WINDOW_HIDDEN) != 0;
	state.minimized = (flags & SDL_WINDOW_MINIMIZED) != 0;
	state.maximized = (flags & SDL_WINDOW_MAXIMIZED) != 0;
	state.focused = (flags & SDL_WINDOW_INPUT_FOCUS) != 0;
	state.positionValid = SDL3_UseAbsoluteWindowPlacement() && SDL_GetWindowPosition(s_sdlWindow, &state.windowX, &state.windowY);
	if (!SDL_GetWindowSize(s_sdlWindow, &state.logicalWidth, &state.logicalHeight) ||
		!SDL_GetWindowSizeInPixels(s_sdlWindow, &state.pixelWidth, &state.pixelHeight) ||
		state.logicalWidth <= 0 || state.logicalHeight <= 0 || state.pixelWidth <= 0 || state.pixelHeight <= 0)
		return SDL_SetError("The SDL window dimensions are unavailable or invalid");
	state.displayScale = SDL_GetWindowDisplayScale(s_sdlWindow);
	if (!std::isfinite(state.displayScale) || state.displayScale <= 0) return SDL_SetError("The SDL window display scale is invalid");
	state.pixelDensityX = static_cast<float>(state.pixelWidth) / state.logicalWidth;
	state.pixelDensityY = static_cast<float>(state.pixelHeight) / state.logicalHeight;
	const SDL_DisplayMode *mode = SDL_GetCurrentDisplayMode(state.displayId);
	if (SDL3_StrictModePixels(mode, state.modePixelWidth, state.modePixelHeight) &&
		std::isfinite(mode->refresh_rate) && mode->refresh_rate >= 0) {
		state.currentModeValid = true; state.modeWidth = mode->w; state.modeHeight = mode->h;
		state.refreshRate = mode->refresh_rate; state.modePixelDensity = mode->pixel_density;
	}
	SDL3_QueryDisplayViewport(state);
	*outState = state;
	return true;
}

static bool SDL3_StrictWindowFailure(const char *message, char *error, int errorSize, bool fromSDL = false) {
	if (error && errorSize > 0) idStr::snPrintf(error, errorSize, "%s%s%s", message, fromSDL ? ": " : "", fromSDL ? SDL_GetError() : "");
	return false;
}

static bool SDL3_StrictFullscreenMode(SDL_DisplayID display, int width, int height, int refresh, SDL_DisplayMode &selected) {
	int count = 0;
	SDL_DisplayMode **modes = SDL_GetFullscreenDisplayModes(display, &count);
	if (!modes) return false;
	bool found = false;
	for (int i = 0; i < count; ++i) {
		int w = 0, h = 0;
		const auto *mode = modes[i];
		if (!SDL3_StrictModePixels(mode,w,h) || w != width || h != height ||
			(mode->displayID && mode->displayID != display) || !std::isfinite(mode->refresh_rate) || mode->refresh_rate < 0) continue;
		// The existing menu stores integer Hz: 59.94 belongs to the 60-Hz
		// choice. No neighboring resolution or different refresh bucket is used.
		if (refresh > 0 && std::floor(mode->refresh_rate + 0.5f) != refresh) continue;
		if (!found || mode->refresh_rate > selected.refresh_rate) { selected = *mode; found = true; }
	}
	SDL_free(modes);
	if (found) selected.displayID = display;
	return found;
}

static bool SDL3_StrictDisplayBounds(const sdl3StrictDisplayList_t &displays, SDL_DisplayID selected,
	bool span, SDL_Rect &bounds) {
	if (!span) return SDL_GetDisplayBounds(selected, &bounds) && bounds.w > 0 && bounds.h > 0;
	int64_t left = 0, top = 0, right = 0, bottom = 0;
	for (int i = 0; i < displays.count; ++i) {
		SDL_Rect current;
		if (!SDL_GetDisplayBounds(displays.ids[i], &current) || current.w <= 0 || current.h <= 0) return false;
		const int64_t r = static_cast<int64_t>(current.x) + current.w;
		const int64_t b = static_cast<int64_t>(current.y) + current.h;
		if (i == 0 || current.x < left) left = current.x;
		if (i == 0 || current.y < top) top = current.y;
		if (i == 0 || r > right) right = r;
		if (i == 0 || b > bottom) bottom = b;
	}
	if (right-left <= 0 || bottom-top <= 0 || right-left > 16384 || bottom-top > 16384) return false;
	bounds = {static_cast<int>(left),static_cast<int>(top),static_cast<int>(right-left),static_cast<int>(bottom-top)};
	return true;
}

static bool SDL3_WindowServices_ApplyScreenParmsStrict(const renderWindowRequest_t *request,
	renderWindowState_t *outState, char *error, int errorSize) {
	if (!request || !outState || !s_sdlWindow) return SDL3_StrictWindowFailure("No SDL window, request or result",error,errorSize);
	const renderWindowParms_t parms = request->parms;
	if (parms.width < 320 || parms.width > 16384 || parms.height < 240 || parms.height > 16384 ||
		parms.displayHz < 0 || parms.displayHz > 1000 || (!request->displayId && request->displayIndex < -1) ||
		(parms.hiddenWindow && (parms.fullScreen || parms.borderless)))
		return SDL3_StrictWindowFailure("Invalid strict window dimensions, display or hidden-window policy",error,errorSize);
	if (s_screenParmTransitionActive) return SDL3_StrictWindowFailure("A window transition is already active",error,errorSize);
	if (request->restorePlacement && !SDL3_UseAbsoluteWindowPlacement())
		return SDL3_StrictWindowFailure("Absolute restore placement is unavailable on this window system",error,errorSize);
	sdl3StrictDisplayList_t displays;
	if (!displays.Valid()) return SDL3_StrictWindowFailure("Display enumeration failed",error,errorSize,true);
	SDL_DisplayID display = request->displayId;
	if (!display && request->displayIndex >= 0) {
		if (request->displayIndex >= displays.count) return SDL3_StrictWindowFailure("Requested display index is unavailable",error,errorSize);
		display = displays.ids[request->displayIndex];
	}
	if (!display) {
		display = SDL_GetDisplayForWindow(s_sdlWindow);
		if (displays.Index(display) < 0) display = SDL_GetPrimaryDisplay();
	}
	if (displays.Index(display) < 0) return SDL3_StrictWindowFailure("Requested display identity is unavailable",error,errorSize);
	const bool spannedDesktop = parms.fullScreen && request->fullscreenDesktop && request->spanDisplays;
	const bool borderless = (!parms.fullScreen && parms.borderless) || spannedDesktop;
	const bool span = borderless && request->spanDisplays;
	if (span && !SDL3_UseAbsoluteWindowPlacement()) return SDL3_StrictWindowFailure("Spanned windows are unavailable on this window system",error,errorSize);
	SDL_Rect bounds;
	if (!SDL3_StrictDisplayBounds(displays,display,span,bounds)) return SDL3_StrictWindowFailure("Requested display bounds are unavailable",error,errorSize,true);
	SDL_DisplayMode selectedMode = {};
	const bool exclusive = parms.fullScreen && !request->fullscreenDesktop;
	if (exclusive && !SDL3_StrictFullscreenMode(display,parms.width,parms.height,parms.displayHz,selectedMode))
		return SDL3_StrictWindowFailure("No exact fullscreen pixel mode and refresh matches the request",error,errorSize);
	int desktopPixelWidth = 0, desktopPixelHeight = 0;
	const bool desktopFullscreen = parms.fullScreen && request->fullscreenDesktop && !span;
	if (desktopFullscreen && !SDL3_StrictModePixels(SDL_GetDesktopDisplayMode(display),desktopPixelWidth,desktopPixelHeight))
		return SDL3_StrictWindowFailure("The requested desktop fullscreen mode is unavailable",error,errorSize);
	renderWindowState_t before = {};
	if (!SDL3_WindowServices_QueryWindowState(&before)) return SDL3_StrictWindowFailure("Cannot observe the existing window",error,errorSize,true);
	const int targetWidth = borderless ? bounds.w : parms.width;
	const int targetHeight = borderless ? bounds.h : parms.height;
	if (targetWidth > 16384 || targetHeight > 16384) return SDL3_StrictWindowFailure("Requested window exceeds supported dimensions",error,errorSize);
	// Transition suppression prevents incidental SDL events from persisting
	// partial geometry. All returns release it; a failure is not a rollback.
	struct Transition {
		Transition() { s_screenParmTransitionActive = true; }
		~Transition() { s_screenParmTransitionActive = false; }
	} transition;
	if (s_windowAspectSnapActive) {
		if (!SDL_SetWindowAspectRatio(s_sdlWindow,0,0)) return SDL3_StrictWindowFailure("Cannot clear the window aspect constraint",error,errorSize,true);
		s_windowAspectSnapActive = false; s_windowAspectSnapRatio = 0;
	}
	if (before.fullscreen) {
		if (!SDL_SetWindowFullscreen(s_sdlWindow,false) || !SDL_SyncWindow(s_sdlWindow))
			return SDL3_StrictWindowFailure("Cannot leave the previous fullscreen mode",error,errorSize,true);
	}
	if (before.maximized || before.minimized) {
		if (!SDL_RestoreWindow(s_sdlWindow) || !SDL_SyncWindow(s_sdlWindow))
			return SDL3_StrictWindowFailure("Cannot restore the previous window state",error,errorSize,true);
	}
	if (!SDL_SetWindowFullscreenMode(s_sdlWindow,exclusive ? &selectedMode : NULL))
		return SDL3_StrictWindowFailure("Cannot select the requested fullscreen mode",error,errorSize,true);
	if (!SDL_SetWindowBordered(s_sdlWindow,!borderless)) return SDL3_StrictWindowFailure("Cannot apply window borders",error,errorSize,true);
	int x = bounds.x, y = bounds.y;
	if (!borderless && !parms.fullScreen) {
		if (before.positionValid && before.displayId == display) { x = before.windowX; y = before.windowY; }
		else {
			x = SDL3_SaturateWindowCoordinate(static_cast<int64_t>(bounds.x) + (static_cast<int64_t>(bounds.w)-targetWidth)/2);
			y = SDL3_SaturateWindowCoordinate(static_cast<int64_t>(bounds.y) + (static_cast<int64_t>(bounds.h)-targetHeight)/2);
		}
	}
	if (request->restorePlacement) { x = request->windowX; y = request->windowY; }
	else if (!SDL3_UseAbsoluteWindowPlacement()) x = y = SDL_WINDOWPOS_CENTERED_DISPLAY(display);
	if (!SDL_SetWindowPosition(s_sdlWindow,x,y)) return SDL3_StrictWindowFailure("Cannot place the window on the requested display",error,errorSize,true);
	if (parms.fullScreen && !spannedDesktop) {
		if (!SDL_SetWindowFullscreen(s_sdlWindow,true)) return SDL3_StrictWindowFailure("Cannot enter the requested fullscreen mode",error,errorSize,true);
	} else if (!SDL_SetWindowSize(s_sdlWindow,targetWidth,targetHeight)) {
		return SDL3_StrictWindowFailure("Cannot apply the requested logical window size",error,errorSize,true);
	}
	if (parms.hiddenWindow ? !SDL_HideWindow(s_sdlWindow) : !SDL_ShowWindow(s_sdlWindow))
		return SDL3_StrictWindowFailure("Cannot apply window visibility",error,errorSize,true);
	if (request->maximized && !SDL_MaximizeWindow(s_sdlWindow))
		return SDL3_StrictWindowFailure("Cannot restore the maximized window state",error,errorSize,true);
	if (!SDL_SyncWindow(s_sdlWindow)) return SDL3_StrictWindowFailure("Requested window transition did not synchronize",error,errorSize,true);
	renderWindowState_t actual = {};
	if (!SDL3_WindowServices_QueryWindowState(&actual)) return SDL3_StrictWindowFailure("Cannot observe the applied window",error,errorSize,true);
	const bool fullscreen = parms.fullScreen && !spannedDesktop;
	if (actual.fullscreen != fullscreen || (fullscreen && actual.fullscreenDesktop != request->fullscreenDesktop) ||
		(!fullscreen && actual.borderless != borderless) || actual.hidden != parms.hiddenWindow || actual.minimized || actual.maximized != request->maximized ||
		(!span && actual.displayId != display))
		return SDL3_StrictWindowFailure("Observed window flags or display do not match the request",error,errorSize);
	if (!fullscreen && (actual.logicalWidth != targetWidth || actual.logicalHeight != targetHeight))
		return SDL3_StrictWindowFailure("Observed logical window dimensions do not match the request",error,errorSize);
	if (request->restorePlacement && (!actual.positionValid || actual.windowX != request->windowX || actual.windowY != request->windowY))
		return SDL3_StrictWindowFailure("Observed window placement does not match the explicit restore coordinates",error,errorSize);
	if (borderless && SDL3_UseAbsoluteWindowPlacement() && (!actual.positionValid || actual.windowX != bounds.x || actual.windowY != bounds.y))
		return SDL3_StrictWindowFailure("Observed borderless placement does not match the requested bounds",error,errorSize);
	if (exclusive && (!actual.currentModeValid || actual.modePixelWidth != parms.width || actual.modePixelHeight != parms.height ||
		actual.pixelWidth != parms.width || actual.pixelHeight != parms.height ||
		(parms.displayHz > 0 && std::floor(actual.refreshRate + 0.5f) != parms.displayHz)))
		return SDL3_StrictWindowFailure("Observed exclusive display mode does not match the request",error,errorSize);
	if (desktopFullscreen && (!actual.currentModeValid || actual.modePixelWidth != desktopPixelWidth || actual.modePixelHeight != desktopPixelHeight ||
		actual.pixelWidth != desktopPixelWidth || actual.pixelHeight != desktopPixelHeight))
		return SDL3_StrictWindowFailure("Observed desktop fullscreen mode does not match the display",error,errorSize);
	win32.cdsFullscreen = parms.fullScreen;
	SDL3_SetFullscreenState(parms.fullScreen);
	SDL3_SetVidSize(actual.pixelWidth,actual.pixelHeight);
	SDL3_SetUIViewport(actual.uiViewportX,actual.uiViewportY,actual.uiViewportWidth,actual.uiViewportHeight);
	engineWindowState.displayScale = actual.displayScale;
	engineWindowState.pixelDensityX = actual.pixelDensityX; engineWindowState.pixelDensityY = actual.pixelDensityY;
	// SDL's observation exposes the current maximized size, not its hidden
	// normal restore rectangle. Do not replace our normal placement cache with
	// maximized geometry or claim that the typed snapshot preserves that cache.
	if (!parms.fullScreen && !borderless && !parms.hiddenWindow && !actual.maximized && actual.positionValid) {
		int frameX = actual.windowX, frameY = actual.windowY;
		SDL3_ClientOriginToFrameOrigin(actual.windowX,actual.windowY,frameX,frameY);
		SDL3_RecordWindowedPlacement(frameX,frameY,actual.logicalWidth,actual.logicalHeight);
	}
	if (error && errorSize > 0) error[0] = '\0';
	*outState = actual;
	return true;
}

// A recoverable renderer restart must keep SDL's display enumeration alive:
// quitting its final video reference invalidates every saved SDL_DisplayID.
// The engine caller owns one matching release for each successful retain,
// including across failed attempts which temporarily have no renderer window.
static bool SDL3_WindowServices_RetainVideoSystem() {
	if ((SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO) == 0)
		return SDL_SetError("Cannot retain an inactive SDL video system");
	return SDL_InitSubSystem(SDL_INIT_VIDEO);
}

static void SDL3_WindowServices_ReleaseVideoSystem() {
	SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

static const renderWindowServices_t s_sdl3WindowServices = {
	SDL3_WindowServices_PrepareWindowSystem,
	SDL3_WindowServices_CreateWindowForFramebuffer,
	SDL3_WindowServices_DestroyAttemptWindow,
	SDL3_WindowServices_ApplyScreenParms,
	SDL3_WindowServices_RefreshNativeWindowHandles,
	SDL3_WindowServices_NotifyWindowReady,
	SDL3_WindowServices_BeginWindowTeardown,
	SDL3_WindowServices_FinishWindowTeardown,
	SDL3_WindowServices_GetDesktopResolution,
	SDL3_WindowServices_PreferCompatibilityFallbackFirst,
	SDL3_WindowServices_CountContextError,
	SDL3_WindowServices_CreateGLContext,
	SDL3_WindowServices_MakeGLContextCurrent,
	SDL3_WindowServices_DestroyGLContext,
	SDL3_WindowServices_IsGLContextCurrent,
	SDL3_WindowServices_SwapGLWindow,
	SDL3_WindowServices_SetGLSwapInterval,
	SDL3_WindowServices_GetGLSwapInterval,
	SDL3_WindowServices_GetGLAttribute,
	SDL3_WindowServices_GetGLProcAddress,
	SDL3_WindowServices_GetVideoErrorString,
	SDL3_WindowServices_InitInput,
	SDL3_WindowServices_ShutdownInput,
	SDL3_WindowServices_GrabMouseCursor,
	SDL3_WindowServices_GetVulkanInstanceExtensions,
	SDL3_WindowServices_CreateVulkanSurface,
	SDL3_WindowServices_QueryWindowState,
	SDL3_WindowServices_ApplyScreenParmsStrict,
	SDL3_WindowServices_RetainVideoSystem,
	SDL3_WindowServices_ReleaseVideoSystem,
};

const renderWindowServices_t *Sys_GetRenderWindowServices(void) {
	return &s_sdl3WindowServices;
}

/*
===============================================================================
	Context half of the seam. Physically renderer-owned; compiled into this
	translation unit only for build shapes that keep the statically linked
	renderer (macOS, legacy backends, dedicated). Module-only clients get the
	context half from the renderer module, which reaches this backend purely
	through renderWindowServices_t.
===============================================================================
*/
#ifndef OPENQ4_RENDERER_MODULE_ONLY
#define OPENQ4_SDL3_CONTEXT_IMPL 1
#define OPENQ4_SDL3_CONTEXT_ENCLOSED 1
#include "../../renderer/OpenGL/gl_ContextSDL3.cpp"
#endif
