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

#include "win_local.h"
#include "../../framework/Common.h"
#include "../../framework/Session.h"
#include "../../renderer/tr_local.h"

#include <SDL3/SDL.h>

#include <cmath>
#include <cstdint>

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

static SDL_Window *s_sdlWindow = NULL;
static SDL_GLContext s_sdlContext = NULL;
static bool s_sdlVideoActive = false;
static bool s_sdlTextInputActive = false;
static bool s_sdlGamepadSubsystemActive = false;
static bool s_sdlJoystickSubsystemActive = false;
static SDL_Gamepad *s_sdlGamepad = NULL;
static SDL_Joystick *s_sdlJoystick = NULL;
static SDL_JoystickID s_sdlGamepadId = 0;
static SDL_JoystickID s_sdlJoystickId = 0;
static bool s_sdlDisplayCommandRegistered = false;
static bool s_sdlDisplaySummaryLogged = false;

static idCVar in_joystick("in_joystick", "1", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_BOOL, "enable joystick/gamepad input");
static idCVar in_joystickDeadZone("in_joystickDeadZone", "0.18", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_FLOAT, "joystick axis dead zone", 0.0f, 0.95f);
static idCVar in_joystickTriggerThreshold("in_joystickTriggerThreshold", "0.35", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_FLOAT, "trigger button press threshold", 0.0f, 1.0f);
static idCVar r_screen("r_screen", "-1", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_INTEGER, "SDL3 display index to target (-1 = auto/current display)");

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
static sdlJoystickAxisEvent_t s_polledJoystick[MAX_JOYSTICK_AXIS];
static int s_polledJoystickCount = 0;

static int s_joystickAxisState[MAX_JOYSTICK_AXIS] = { 0 };
static bool s_gamepadButtonsDown[SDL_GAMEPAD_BUTTON_COUNT] = { false };
static bool s_gamepadLeftTriggerDown = false;
static bool s_gamepadRightTriggerDown = false;
static const int SDL3_MAX_JOYSTICK_BUTTONS = 48;
static bool s_joystickButtonsDown[SDL3_MAX_JOYSTICK_BUTTONS] = { false };
static Uint8 s_joystickHatState = SDL_HAT_CENTERED;
static bool s_haveAbsoluteMousePosition = false;
static int s_absoluteMouseX = 0;
static int s_absoluteMouseY = 0;
static bool s_menuMouseRouteActive = false;
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

typedef struct {
	int x;
	int y;
	int width;
	int height;
	bool valid;
} sdl3WindowedPlacement_t;

static sdl3WindowedPlacement_t s_windowedPlacement = { 0, 0, 0, 0, false };

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
	s_ignoreNextMenuWarpMotion = false;
	s_menuWarpWindowX = 0.0f;
	s_menuWarpWindowY = 0.0f;
	s_menuMouseInsideWindow = true;
}

static void SDL3_QueueKeyboardInput(int key, bool down, int time) {
	const int next = (s_keyboardHead + 1) & SDL3_INPUT_QUEUE_MASK;

	Sys_EnterCriticalSection(CRITICAL_SECTION_ONE);
	if (next == s_keyboardTail) {
		s_keyboardTail = (s_keyboardTail + 1) & SDL3_INPUT_QUEUE_MASK;
	}
	s_keyboardQueue[s_keyboardHead].key = key;
	s_keyboardQueue[s_keyboardHead].down = down;
	s_keyboardQueue[s_keyboardHead].time = time;
	s_keyboardHead = next;
	Sys_LeaveCriticalSection(CRITICAL_SECTION_ONE);
}

static void SDL3_QueueMouseInput(int action, int value, int time) {
	const int next = (s_mouseHead + 1) & SDL3_INPUT_QUEUE_MASK;

	Sys_EnterCriticalSection(CRITICAL_SECTION_ONE);
	if (next == s_mouseTail) {
		s_mouseTail = (s_mouseTail + 1) & SDL3_INPUT_QUEUE_MASK;
	}
	s_mouseQueue[s_mouseHead].action = action;
	s_mouseQueue[s_mouseHead].value = value;
	s_mouseQueue[s_mouseHead].time = time;
	s_mouseHead = next;
	Sys_LeaveCriticalSection(CRITICAL_SECTION_ONE);
}

static bool SDL3_ShouldRouteMenuMouse(void) {
	return (session != NULL) && session->IsGUIActive();
}

static bool SDL3_IsMouseCaptured(void) {
	if (!s_sdlWindow) {
		return false;
	}
	return SDL_GetWindowRelativeMouseMode(s_sdlWindow) || SDL_GetWindowMouseGrab(s_sdlWindow);
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

	if (SDL3_ShouldRouteMenuMouse() && win32.activeApp && (win32.cdsFullscreen || s_menuMouseInsideWindow)) {
		(void)SDL_HideCursor();
		return;
	}

	(void)SDL_ShowCursor();
}

static int SDL3_RoundToInt(float value) {
	return static_cast<int>(value >= 0.0f ? (value + 0.5f) : (value - 0.5f));
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

static void SDL3_GetGuiCursorBounds(const sdl3GuiMouseTransform_t &transform, float &minX, float &maxX, float &minY, float &maxY) {
	minX = 0.0f;
	maxX = transform.guiWidth;
	minY = 0.0f;
	maxY = transform.guiHeight;

	if (transform.xScale != 0.0f) {
		minX = (0.0f - transform.xOffset) / transform.xScale;
		maxX = (transform.guiWidth - transform.xOffset) / transform.xScale;
		if (minX > maxX) {
			const float tmp = minX;
			minX = maxX;
			maxX = tmp;
		}
	}

	if (transform.yScale != 0.0f) {
		minY = (0.0f - transform.yOffset) / transform.yScale;
		maxY = (transform.guiHeight - transform.yOffset) / transform.yScale;
		if (minY > maxY) {
			const float tmp = minY;
			minY = maxY;
			maxY = tmp;
		}
	}
}

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

	// Match the fullscreen-2D viewport region (primary monitor on multi-monitor spans).
	transform.drawAreaX = static_cast<float>(glConfig.uiViewportX);
	transform.drawAreaY = static_cast<float>(glConfig.uiViewportY);
	transform.drawAreaWidth = static_cast<float>(glConfig.uiViewportWidth);
	transform.drawAreaHeight = static_cast<float>(glConfig.uiViewportHeight);

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

static bool SDL3_MapWindowMouseToGuiCursor(float windowMouseX, float windowMouseY, float &cursorX, float &cursorY) {
	sdl3GuiMouseTransform_t transform;
	if (!SDL3_BuildGuiMouseTransform(transform)) {
		return false;
	}

	float pixelMouseX = windowMouseX * transform.windowToPixelX - transform.drawAreaX;
	float pixelMouseY = windowMouseY * transform.windowToPixelY - transform.drawAreaY;
	pixelMouseX = idMath::ClampFloat(0.0f, transform.drawAreaWidth, pixelMouseX);
	pixelMouseY = idMath::ClampFloat(0.0f, transform.drawAreaHeight, pixelMouseY);
	const float drawX = pixelMouseX * (transform.guiWidth / transform.drawAreaWidth);
	const float drawY = pixelMouseY * (transform.guiHeight / transform.drawAreaHeight);

	cursorX = (drawX - transform.xOffset) / transform.xScale;
	cursorY = (drawY - transform.yOffset) / transform.yScale;
	float minX = 0.0f;
	float maxX = transform.guiWidth;
	float minY = 0.0f;
	float maxY = transform.guiHeight;
	SDL3_GetGuiCursorBounds(transform, minX, maxX, minY, maxY);
	cursorX = idMath::ClampFloat(minX, maxX, cursorX);
	cursorY = idMath::ClampFloat(minY, maxY, cursorY);
	return true;
}

static void SDL3_SyncSystemMouseToActiveGUICursor(void) {
	if (!SDL3_ShouldRouteMenuMouse() || !s_sdlWindow) {
		return;
	}

	idUserInterface *activeGui = session->GetActiveGUI();
	if (activeGui == NULL) {
		return;
	}

	sdl3GuiMouseTransform_t transform;
	if (!SDL3_BuildGuiMouseTransform(transform)) {
		return;
	}

	float minX = 0.0f;
	float maxX = transform.guiWidth;
	float minY = 0.0f;
	float maxY = transform.guiHeight;
	SDL3_GetGuiCursorBounds(transform, minX, maxX, minY, maxY);
	const float clampedCursorX = idMath::ClampFloat(minX, maxX, activeGui->CursorX());
	const float clampedCursorY = idMath::ClampFloat(minY, maxY, activeGui->CursorY());
	const float drawX = (clampedCursorX * transform.xScale) + transform.xOffset;
	const float drawY = (clampedCursorY * transform.yScale) + transform.yOffset;
	const float pixelMouseX = transform.drawAreaX + drawX * (transform.drawAreaWidth / transform.guiWidth);
	const float pixelMouseY = transform.drawAreaY + drawY * (transform.drawAreaHeight / transform.guiHeight);
	const float windowMouseX = pixelMouseX * transform.pixelToWindowX;
	const float windowMouseY = pixelMouseY * transform.pixelToWindowY;

	SDL_WarpMouseInWindow(s_sdlWindow, windowMouseX, windowMouseY);
	s_ignoreNextMenuWarpMotion = true;
	s_menuWarpWindowX = windowMouseX;
	s_menuWarpWindowY = windowMouseY;
	s_menuMouseInsideWindow = true;
	s_haveMenuMousePosition = true;
	s_menuMouseX = clampedCursorX;
	s_menuMouseY = clampedCursorY;
	s_menuMouseRemainderX = 0.0f;
	s_menuMouseRemainderY = 0.0f;
	activeGui->SetCursor(clampedCursorX, clampedCursorY);
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

static float SDL3_ClampUnit(float value) {
	if (value < 0.0f) {
		return 0.0f;
	}
	if (value > 1.0f) {
		return 1.0f;
	}
	return value;
}

static int SDL3_NormalizeSignedAxis(Sint16 value, float deadZone) {
	float normalized = static_cast<float>(value) / 32767.0f;
	if (normalized < -1.0f) {
		normalized = -1.0f;
	} else if (normalized > 1.0f) {
		normalized = 1.0f;
	}

	const float absValue = fabsf(normalized);
	if (absValue <= deadZone) {
		return 0;
	}

	const float adjusted = (absValue - deadZone) / (1.0f - deadZone);
	const float signedAdjusted = (normalized < 0.0f) ? -adjusted : adjusted;
	return SDL3_ClampJoystickValue(static_cast<int>(roundf(signedAdjusted * 127.0f)));
}

static int SDL3_NormalizeTriggerAxis(Sint16 value, float deadZone) {
	float normalized = static_cast<float>(value) / 32767.0f;
	normalized = SDL3_ClampUnit(normalized);

	if (normalized <= deadZone) {
		return 0;
	}

	const float adjusted = (normalized - deadZone) / (1.0f - deadZone);
	return SDL3_ClampJoystickValue(static_cast<int>(roundf(adjusted * 127.0f)));
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

static void SDL3_ClearJoystickStateUnlocked(void) {
	memset(s_joystickAxisState, 0, sizeof(s_joystickAxisState));
}

static void SDL3_ClearJoystickState(void) {
	Sys_EnterCriticalSection(CRITICAL_SECTION_ONE);
	SDL3_ClearJoystickStateUnlocked();
	Sys_LeaveCriticalSection(CRITICAL_SECTION_ONE);
}

static void SDL3_PostControllerKeyEvent(int key, bool down, int eventTime) {
	if (key == 0) {
		return;
	}

	Sys_QueEvent(eventTime, SE_KEY, key, down ? 1 : 0, 0, NULL);
	SDL3_QueueKeyboardInput(key, down, eventTime);
}

static void SDL3_UpdateTriggerButtons(int leftTrigger, int rightTrigger, int eventTime) {
	const float threshold = SDL3_ClampUnit(in_joystickTriggerThreshold.GetFloat());
	const int pressThreshold = static_cast<int>(roundf(threshold * 127.0f));
	const bool leftDown = leftTrigger >= pressThreshold;
	const bool rightDown = rightTrigger >= pressThreshold;

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

	const int moveX = SDL3_NormalizeSignedAxis(SDL_GetGamepadAxis(s_sdlGamepad, SDL_GAMEPAD_AXIS_LEFTX), deadZone);
	const int moveY = -SDL3_NormalizeSignedAxis(SDL_GetGamepadAxis(s_sdlGamepad, SDL_GAMEPAD_AXIS_LEFTY), deadZone);
	const int lookX = SDL3_NormalizeSignedAxis(SDL_GetGamepadAxis(s_sdlGamepad, SDL_GAMEPAD_AXIS_RIGHTX), deadZone);
	const int lookY = SDL3_NormalizeSignedAxis(SDL_GetGamepadAxis(s_sdlGamepad, SDL_GAMEPAD_AXIS_RIGHTY), deadZone);
	const int leftTrigger = SDL3_NormalizeTriggerAxis(SDL_GetGamepadAxis(s_sdlGamepad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER), deadZone);
	const int rightTrigger = SDL3_NormalizeTriggerAxis(SDL_GetGamepadAxis(s_sdlGamepad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER), deadZone);

	Sys_EnterCriticalSection(CRITICAL_SECTION_ONE);
	s_joystickAxisState[AXIS_SIDE] = lookX;
	s_joystickAxisState[AXIS_FORWARD] = lookY;
	s_joystickAxisState[AXIS_UP] = SDL3_ClampJoystickValue(rightTrigger - leftTrigger);
	s_joystickAxisState[AXIS_ROLL] = 127;
	s_joystickAxisState[AXIS_YAW] = moveX;
	s_joystickAxisState[AXIS_PITCH] = moveY;
	Sys_LeaveCriticalSection(CRITICAL_SECTION_ONE);

	SDL3_UpdateTriggerButtons(leftTrigger, rightTrigger, eventTime);
}

static void SDL3_UpdateJoystickAxes(void) {
	if (!s_sdlJoystick) {
		SDL3_ClearJoystickState();
		return;
	}

	const float deadZone = SDL3_ClampUnit(in_joystickDeadZone.GetFloat());
	const int numAxes = SDL_GetNumJoystickAxes(s_sdlJoystick);
	const bool hasLookAxis = numAxes >= 4;

	int moveX = 0;
	int moveY = 0;
	int lookX = 0;
	int lookY = 0;
	int up = 0;

	if (numAxes > 0) {
		moveX = SDL3_NormalizeSignedAxis(SDL_GetJoystickAxis(s_sdlJoystick, 0), deadZone);
	}
	if (numAxes > 1) {
		moveY = -SDL3_NormalizeSignedAxis(SDL_GetJoystickAxis(s_sdlJoystick, 1), deadZone);
	}
	if (numAxes > 2) {
		lookX = SDL3_NormalizeSignedAxis(SDL_GetJoystickAxis(s_sdlJoystick, 2), deadZone);
	}
	if (numAxes > 3) {
		lookY = SDL3_NormalizeSignedAxis(SDL_GetJoystickAxis(s_sdlJoystick, 3), deadZone);
	}
	if (numAxes > 5) {
		const int axis4 = SDL3_NormalizeSignedAxis(SDL_GetJoystickAxis(s_sdlJoystick, 4), deadZone);
		const int axis5 = SDL3_NormalizeSignedAxis(SDL_GetJoystickAxis(s_sdlJoystick, 5), deadZone);
		up = SDL3_ClampJoystickValue(axis4 - axis5);
	} else if (numAxes > 4) {
		up = SDL3_NormalizeSignedAxis(SDL_GetJoystickAxis(s_sdlJoystick, 4), deadZone);
	}

	Sys_EnterCriticalSection(CRITICAL_SECTION_ONE);
	s_joystickAxisState[AXIS_SIDE] = lookX;
	s_joystickAxisState[AXIS_FORWARD] = lookY;
	s_joystickAxisState[AXIS_UP] = up;
	s_joystickAxisState[AXIS_ROLL] = hasLookAxis ? 127 : 0;
	s_joystickAxisState[AXIS_YAW] = moveX;
	s_joystickAxisState[AXIS_PITCH] = moveY;
	Sys_LeaveCriticalSection(CRITICAL_SECTION_ONE);
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

	SDL3_ReleaseGamepadState(eventTime);
	SDL_CloseGamepad(s_sdlGamepad);
	s_sdlGamepad = NULL;
	s_sdlGamepadId = 0;
	SDL3_ClearJoystickState();
}

static void SDL3_CloseJoystick(int eventTime) {
	if (!s_sdlJoystick) {
		return;
	}

	SDL3_ReleaseJoystickState(eventTime);
	SDL_CloseJoystick(s_sdlJoystick);
	s_sdlJoystick = NULL;
	s_sdlJoystickId = 0;
	SDL3_ClearJoystickState();
}

static bool SDL3_OpenGamepad(SDL_JoystickID instanceId) {
	SDL_Gamepad *pad = SDL_OpenGamepad(instanceId);
	if (!pad) {
		return false;
	}

	SDL3_CloseJoystick(Sys_Milliseconds());

	s_sdlGamepad = pad;
	s_sdlGamepadId = instanceId;
	memset(s_gamepadButtonsDown, 0, sizeof(s_gamepadButtonsDown));
	s_gamepadLeftTriggerDown = false;
	s_gamepadRightTriggerDown = false;

	SDL3_UpdateGamepadAxes(Sys_Milliseconds());

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
	memset(s_joystickButtonsDown, 0, sizeof(s_joystickButtonsDown));
	s_joystickHatState = SDL_HAT_CENTERED;

	SDL3_UpdateJoystickAxes();

	const char *name = SDL_GetJoystickName(joystick);
	if (name && name[0] != '\0') {
		common->Printf("controller: opened SDL joystick '%s'\n", name);
	} else {
		common->Printf("controller: opened SDL joystick\n");
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

static void SDL3_InitControllerSubsystems(void) {
	if (!in_joystick.GetBool()) {
		SDL3_CloseGamepad(Sys_Milliseconds());
		SDL3_CloseJoystick(Sys_Milliseconds());
		SDL3_ClearJoystickState();
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

	SDL3_ClearJoystickState();
}

static int SDL3_MapScancode(SDL_Scancode scancode) {
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
		default: return 0;
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

		SDL_Rect bounds;
		if (SDL_GetDisplayBounds(display, &bounds)) {
			common->Printf("  [%d]%s %s (%dx%d @ %d,%d)\n",
				i,
				(display == primaryDisplay) ? " *" : "",
				name,
				bounds.w,
				bounds.h,
				bounds.x,
				bounds.y);
		} else {
			common->Printf("  [%d]%s %s (bounds unavailable: %s)\n",
				i,
				(display == primaryDisplay) ? " *" : "",
				name,
				SDL_GetError());
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

static bool SDL3_GetDisplayWindowedPlacementBounds(SDL_DisplayID display, SDL_Rect &bounds) {
	if (display != 0 && SDL_GetDisplayUsableBounds(display, &bounds)) {
		return true;
	}
	if (display != 0 && SDL_GetDisplayBounds(display, &bounds)) {
		return true;
	}
	return false;
}

static bool SDL3_RectsOverlap(const SDL_Rect &a, const SDL_Rect &b) {
	const int overlapLeft = (a.x > b.x) ? a.x : b.x;
	const int overlapTop = (a.y > b.y) ? a.y : b.y;
	const int overlapRight = ((a.x + a.w) < (b.x + b.w)) ? (a.x + a.w) : (b.x + b.w);
	const int overlapBottom = ((a.y + a.h) < (b.y + b.h)) ? (a.y + a.h) : (b.y + b.h);
	return overlapRight > overlapLeft && overlapBottom > overlapTop;
}

static bool SDL3_WindowRectIntersectsAnyDisplay(int x, int y, int width, int height) {
	if (width <= 0 || height <= 0) {
		return false;
	}

	SDL_Rect windowRect;
	windowRect.x = x;
	windowRect.y = y;
	windowRect.w = width;
	windowRect.h = height;

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
		return minValue;
	}
	const int effectiveMin = (minValue > maxValue) ? maxValue : minValue;
	return idMath::ClampInt(effectiveMin, maxValue, value);
}

static void SDL3_ConstrainWindowRectToBounds(int &x, int &y, int &width, int &height, const SDL_Rect &bounds, bool recenterIfOutside) {
	width = SDL3_ClampWindowDimension(width, 320, bounds.w);
	height = SDL3_ClampWindowDimension(height, 240, bounds.h);

	const int maxX = bounds.x + bounds.w - width;
	const int maxY = bounds.y + bounds.h - height;

	if (maxX < bounds.x) {
		x = bounds.x;
	} else {
		if (recenterIfOutside && (x < bounds.x || x > maxX)) {
			x = bounds.x + ((bounds.w - width) / 2);
		}
		x = idMath::ClampInt(bounds.x, maxX, x);
	}

	if (maxY < bounds.y) {
		y = bounds.y;
	} else {
		if (recenterIfOutside && (y < bounds.y || y > maxY)) {
			y = bounds.y + ((bounds.h - height) / 2);
		}
		y = idMath::ClampInt(bounds.y, maxY, y);
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

	common->Printf("SDL3: fullscreen modes for display %d (%s):\n", selectedDisplay.index, name);
	for (int i = 0; i < modeCount; ++i) {
		const SDL_DisplayMode *mode = modes[i];
		if (mode == NULL || mode->w <= 0 || mode->h <= 0) {
			continue;
		}
		common->Printf("  [%d] %dx%d @ %.2f Hz\n", i, mode->w, mode->h, mode->refresh_rate);
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

static void SDL3_UpdatePrimaryDisplayViewport(int windowX, int windowY, int windowWidth, int windowHeight, int pixelWidth, int pixelHeight) {
	glConfig.uiViewportX = 0;
	glConfig.uiViewportY = 0;
	glConfig.uiViewportWidth = pixelWidth;
	glConfig.uiViewportHeight = pixelHeight;

	if (windowWidth <= 0 || windowHeight <= 0 || pixelWidth <= 0 || pixelHeight <= 0) {
		return;
	}

	const SDL_DisplayID primaryDisplay = SDL_GetPrimaryDisplay();
	if (primaryDisplay == 0) {
		return;
	}

	SDL_Rect primaryBounds;
	if (!SDL_GetDisplayBounds(primaryDisplay, &primaryBounds)) {
		return;
	}

	const int windowRight = windowX + windowWidth;
	const int windowBottom = windowY + windowHeight;
	const int primaryRight = primaryBounds.x + primaryBounds.w;
	const int primaryBottom = primaryBounds.y + primaryBounds.h;

	const int overlapLeft = (windowX > primaryBounds.x) ? windowX : primaryBounds.x;
	const int overlapTop = (windowY > primaryBounds.y) ? windowY : primaryBounds.y;
	const int overlapRight = (windowRight < primaryRight) ? windowRight : primaryRight;
	const int overlapBottom = (windowBottom < primaryBottom) ? windowBottom : primaryBottom;

	if (overlapRight <= overlapLeft || overlapBottom <= overlapTop) {
		return;
	}

	const float pixelScaleX = static_cast<float>(pixelWidth) / static_cast<float>(windowWidth);
	const float pixelScaleY = static_cast<float>(pixelHeight) / static_cast<float>(windowHeight);
	const float localLeft = static_cast<float>(overlapLeft - windowX);
	const float localTop = static_cast<float>(overlapTop - windowY);
	const float localRight = static_cast<float>(overlapRight - windowX);
	const float localBottom = static_cast<float>(overlapBottom - windowY);

	int pixelLeft = static_cast<int>(floorf(localLeft * pixelScaleX));
	int pixelTop = static_cast<int>(floorf(localTop * pixelScaleY));
	int pixelRight = static_cast<int>(ceilf(localRight * pixelScaleX));
	int pixelBottom = static_cast<int>(ceilf(localBottom * pixelScaleY));

	pixelLeft = idMath::ClampInt(0, pixelWidth, pixelLeft);
	pixelTop = idMath::ClampInt(0, pixelHeight, pixelTop);
	pixelRight = idMath::ClampInt(pixelLeft, pixelWidth, pixelRight);
	pixelBottom = idMath::ClampInt(pixelTop, pixelHeight, pixelBottom);

	const int viewportWidth = pixelRight - pixelLeft;
	const int viewportHeight = pixelBottom - pixelTop;
	if (viewportWidth <= 0 || viewportHeight <= 0) {
		return;
	}

	glConfig.uiViewportX = pixelLeft;
	glConfig.uiViewportY = pixelTop;
	glConfig.uiViewportWidth = viewportWidth;
	glConfig.uiViewportHeight = viewportHeight;
}

static void SDL3_RecordWindowedPlacement(int x, int y, int width, int height) {
	if (width <= 0 || height <= 0) {
		return;
	}

	s_windowedPlacement.x = x;
	s_windowedPlacement.y = y;
	s_windowedPlacement.width = width;
	s_windowedPlacement.height = height;
	s_windowedPlacement.valid = true;
}

static void SDL3_SnapshotCurrentWindowedPlacement(void) {
	if (!s_sdlWindow || win32.cdsFullscreen || r_borderless.GetBool()) {
		return;
	}

	int x = 0;
	int y = 0;
	int width = 0;
	int height = 0;
	if (SDL_GetWindowPosition(s_sdlWindow, &x, &y) && SDL_GetWindowSize(s_sdlWindow, &width, &height)) {
		SDL3_RecordWindowedPlacement(x, y, width, height);
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
	const bool canPersistWindowedPlacement = !win32.cdsFullscreen && !s_screenParmTransitionActive;
	const bool isWindowedResizable = !r_borderless.GetBool();

	const bool haveWindowPosition = SDL_GetWindowPosition(s_sdlWindow, &x, &y);
	if (haveWindowPosition && canPersistWindowedPlacement) {
		win32.win_xpos.SetInteger(x);
		win32.win_ypos.SetInteger(y);
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

	if (canPersistWindowedPlacement && isWindowedResizable && haveWindowPosition && haveWindowSize) {
		SDL3_RecordWindowedPlacement(x, y, width, height);
	}

	if (!SDL_GetWindowSizeInPixels(s_sdlWindow, &pixelWidth, &pixelHeight)) {
		pixelWidth = width;
		pixelHeight = height;
	}

	if (pixelWidth > 0 && pixelHeight > 0) {
		glConfig.vidWidth = pixelWidth;
		glConfig.vidHeight = pixelHeight;
	}

	SDL3_UpdatePrimaryDisplayViewport(x, y, width, height, pixelWidth, pixelHeight);
}

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

static bool SDL3_ApplyScreenParms(glimpParms_t parms) {
	if (!s_sdlWindow) {
		return false;
	}

	s_screenParmTransitionActive = true;

	SDL3_DisableWindowAspectSnap();

	const bool useBorderlessWindow = !parms.fullScreen && parms.borderless;
	const sdl3DisplaySelection_t selectedDisplay = SDL3_ResolveTargetDisplay(true);
	SDL_DisplayID display = selectedDisplay.id;
	if (display == 0) {
		display = SDL_GetPrimaryDisplay();
	}

	if (parms.fullScreen) {
		SDL3_SnapshotCurrentWindowedPlacement();

		if (!SDL_SetWindowBordered(s_sdlWindow, true)) {
			common->Printf("SDL3: failed to restore window borders: %s\n", SDL_GetError());
		}

		const bool useDesktopFullscreen = r_fullscreenDesktop.GetBool();
		int targetX = 0;
		int targetY = 0;

		if (useDesktopFullscreen) {
			SDL_Rect bounds;
			if (display != 0 && SDL_GetDisplayBounds(display, &bounds)) {
				targetX = bounds.x;
				targetY = bounds.y;
			} else {
				SDL3_GetWindowPositionOnDisplay(display, parms.width, parms.height, targetX, targetY);
			}
			(void)SDL_SetWindowPosition(s_sdlWindow, targetX, targetY);

			if (!SDL_SetWindowFullscreenMode(s_sdlWindow, NULL)) {
				common->Printf("SDL3: failed to select desktop fullscreen mode: %s\n", SDL_GetError());
			}
		} else {
			SDL_DisplayMode mode;
			memset(&mode, 0, sizeof(mode));
			const float requestedRefresh = parms.displayHz > 0 ? static_cast<float>(parms.displayHz) : 0.0f;
			const bool hasClosestMode = display != 0 &&
				SDL_GetClosestFullscreenDisplayMode(display, parms.width, parms.height, requestedRefresh, false, &mode);

			if (hasClosestMode && mode.displayID == 0) {
				mode.displayID = display;
			}

			const int modeWidth = hasClosestMode ? mode.w : parms.width;
			const int modeHeight = hasClosestMode ? mode.h : parms.height;
			SDL3_GetWindowPositionOnDisplay(display, modeWidth, modeHeight, targetX, targetY);
			(void)SDL_SetWindowPosition(s_sdlWindow, targetX, targetY);

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

		if (!SDL_SetWindowFullscreen(s_sdlWindow, true)) {
			common->Printf("SDL3: failed to enter fullscreen: %s\n", SDL_GetError());
			s_screenParmTransitionActive = false;
			return false;
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
			if (display != 0 && SDL_GetDisplayBounds(display, &bounds)) {
				if (!SDL_SetWindowSize(s_sdlWindow, bounds.w, bounds.h)) {
					common->Printf("SDL3: failed to resize borderless window: %s\n", SDL_GetError());
				}
				if (!SDL_SetWindowPosition(s_sdlWindow, bounds.x, bounds.y)) {
					common->Printf("SDL3: failed to place borderless window: %s\n", SDL_GetError());
				}
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

			if (!SDL_SetWindowSize(s_sdlWindow, restoredWidth, restoredHeight)) {
				common->Printf("SDL3: failed to resize window: %s\n", SDL_GetError());
			}

			int targetX = restoredX;
			int targetY = restoredY;

			if (!SDL_SetWindowPosition(s_sdlWindow, targetX, targetY)) {
				common->Printf("SDL3: failed to move window: %s\n", SDL_GetError());
			}
		}
	}

	win32.cdsFullscreen = parms.fullScreen;
	glConfig.isFullscreen = parms.fullScreen;
	s_screenParmTransitionActive = false;
	SDL3_RefreshWindowPlacement();

	return true;
}

static void SDL3_LoadWGLExtensions(void) {
	wglGetExtensionsStringARB = (PFNWGLGETEXTENSIONSSTRINGARBPROC)GLimp_ExtensionPointer("wglGetExtensionsStringARB");
	if (wglGetExtensionsStringARB && win32.hDC) {
		glConfig.wgl_extensions_string = (const char *)wglGetExtensionsStringARB(win32.hDC);
	} else {
		glConfig.wgl_extensions_string = "";
	}

	wglSwapIntervalEXT = (PFNWGLSWAPINTERVALEXTPROC)GLimp_ExtensionPointer("wglSwapIntervalEXT");
	r_swapInterval.SetModified();

	wglGetPixelFormatAttribivARB = (PFNWGLGETPIXELFORMATATTRIBIVARBPROC)GLimp_ExtensionPointer("wglGetPixelFormatAttribivARB");
	wglGetPixelFormatAttribfvARB = (PFNWGLGETPIXELFORMATATTRIBFVARBPROC)GLimp_ExtensionPointer("wglGetPixelFormatAttribfvARB");
	wglChoosePixelFormatARB = (PFNWGLCHOOSEPIXELFORMATARBPROC)GLimp_ExtensionPointer("wglChoosePixelFormatARB");

	wglCreatePbufferARB = (PFNWGLCREATEPBUFFERARBPROC)GLimp_ExtensionPointer("wglCreatePbufferARB");
	wglGetPbufferDCARB = (PFNWGLGETPBUFFERDCARBPROC)GLimp_ExtensionPointer("wglGetPbufferDCARB");
	wglReleasePbufferDCARB = (PFNWGLRELEASEPBUFFERDCARBPROC)GLimp_ExtensionPointer("wglReleasePbufferDCARB");
	wglDestroyPbufferARB = (PFNWGLDESTROYPBUFFERARBPROC)GLimp_ExtensionPointer("wglDestroyPbufferARB");
	wglQueryPbufferARB = (PFNWGLQUERYPBUFFERARBPROC)GLimp_ExtensionPointer("wglQueryPbufferARB");

	wglBindTexImageARB = (PFNWGLBINDTEXIMAGEARBPROC)GLimp_ExtensionPointer("wglBindTexImageARB");
	wglReleaseTexImageARB = (PFNWGLRELEASETEXIMAGEARBPROC)GLimp_ExtensionPointer("wglReleaseTexImageARB");
	wglSetPbufferAttribARB = (PFNWGLSETPBUFFERATTRIBARBPROC)GLimp_ExtensionPointer("wglSetPbufferAttribARB");
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
}

static void SDL3_HandleWindowEvent(const SDL_WindowEvent &event, int eventTime) {
	switch (event.type) {
		case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
			cmdSystem->BufferCommandText(CMD_EXEC_APPEND, "quit\n");
			break;

		case SDL_EVENT_WINDOW_FOCUS_GAINED:
			win32.activeApp = true;
			idKeyInput::ClearStates();
			com_editorActive = false;
			s_menuMouseInsideWindow = true;
			SDL3_InvalidateMenuMouseRouting();
			Sys_GrabMouseCursor(true);
			SDL3_UpdateCursorVisibility();
			if (session != NULL) {
				session->SetPlayingSoundWorld();
			}
			break;

		case SDL_EVENT_WINDOW_FOCUS_LOST:
			win32.activeApp = false;
			win32.movingWindow = false;
			s_menuMouseInsideWindow = false;
			SDL3_InvalidateMenuMouseRouting();
			SDL3_UpdateCursorVisibility();
			if (session != NULL) {
				session->SetPlayingSoundWorld();
			}
			break;

		case SDL_EVENT_WINDOW_MOVED:
			SDL3_RefreshWindowPlacement();
			SDL3_InvalidateMenuMouseRouting();
			break;

		case SDL_EVENT_WINDOW_MINIMIZED:
			win32.activeApp = false;
			s_menuMouseInsideWindow = false;
			SDL3_InvalidateMenuMouseRouting();
			SDL3_UpdateCursorVisibility();
			if (session != NULL) {
				session->SetPlayingSoundWorld();
			}
			break;

		case SDL_EVENT_WINDOW_RESTORED:
			win32.activeApp = true;
			s_menuMouseInsideWindow = true;
			SDL3_RefreshWindowPlacement();
			SDL3_InvalidateMenuMouseRouting();
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
				SDL3_SyncSystemMouseToActiveGUICursor();
			}
			SDL3_UpdateCursorVisibility();
			break;

		case SDL_EVENT_WINDOW_MOUSE_LEAVE:
			s_menuMouseInsideWindow = false;
			SDL3_ResetMenuMouseTracking();
			SDL3_UpdateCursorVisibility();
			break;

		default:
			break;
	}

	(void)eventTime;
}

bool Sys_SDL_PumpEvents(void) {
	if (!s_sdlVideoActive || !s_sdlWindow) {
		return false;
	}

	if (in_joystick.IsModified()) {
		if (in_joystick.GetBool()) {
			SDL3_InitControllerSubsystems();
		} else {
			SDL3_ShutdownControllerSubsystems();
		}
		in_joystick.ClearModified();
	}

	bool sawResizeEvent = false;
	SDL_Event event;
	while (SDL_PollEvent(&event)) {
		const int eventTime = SDL3_EventMilliseconds(event.common.timestamp);

		if (event.type >= SDL_EVENT_WINDOW_FIRST && event.type <= SDL_EVENT_WINDOW_LAST) {
			if (event.window.type == SDL_EVENT_WINDOW_RESIZED) {
				sawResizeEvent = true;
			}
			SDL3_HandleWindowEvent(event.window, eventTime);
			continue;
		}

		switch (event.type) {
			case SDL_EVENT_QUIT:
				cmdSystem->BufferCommandText(CMD_EXEC_APPEND, "quit\n");
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

				if (key != 0) {
					// Keep parity with the old Win32 path: these are queued from keyboard polling.
					if (key != K_PRINT_SCR && key != K_CTRL && key != K_ALT && key != K_RIGHT_ALT) {
						Sys_QueEvent(eventTime, SE_KEY, key, down, 0, NULL);
					}

					const int controlChar = SDL3_MapControlChar(key, down, event.key.mod);
					if (controlChar != 0) {
						Sys_QueEvent(eventTime, SE_CHAR, controlChar, 0, 0, NULL);
					}

					SDL3_QueueKeyboardInput(key, down, eventTime);
				}

				SDL3_UpdateWindowAspectSnap(false);
				break;
			}

			case SDL_EVENT_TEXT_INPUT: {
				const char *text = event.text.text;
				for (int i = 0; text[i] != '\0'; ++i) {
					const unsigned char ch = static_cast<unsigned char>(text[i]);
					Sys_QueEvent(eventTime, SE_CHAR, ch, 0, 0, NULL);
				}
				break;
			}

			case SDL_EVENT_MOUSE_MOTION:
			{
				int dx = 0;
				int dy = 0;
				const bool mouseCaptured = SDL3_IsMouseCaptured();

				if (mouseCaptured) {
					dx = static_cast<int>(event.motion.xrel);
					dy = static_cast<int>(event.motion.yrel);
					s_haveAbsoluteMousePosition = false;
					SDL3_ResetMenuMouseTracking();
				} else if (SDL3_ShouldRouteMenuMouse()) {
					float menuMouseX = 0.0f;
					float menuMouseY = 0.0f;
					const bool warpMotionEvent = s_ignoreNextMenuWarpMotion &&
						fabsf(event.motion.x - s_menuWarpWindowX) <= 1.0f &&
						fabsf(event.motion.y - s_menuWarpWindowY) <= 1.0f;

					if (warpMotionEvent) {
						s_ignoreNextMenuWarpMotion = false;
						if (SDL3_MapWindowMouseToGuiCursor(event.motion.x, event.motion.y, menuMouseX, menuMouseY)) {
							s_menuMouseX = menuMouseX;
							s_menuMouseY = menuMouseY;
							s_haveMenuMousePosition = true;
							s_menuMouseRemainderX = 0.0f;
							s_menuMouseRemainderY = 0.0f;
							s_haveAbsoluteMousePosition = false;
						}
					} else {
						if (s_ignoreNextMenuWarpMotion) {
							s_ignoreNextMenuWarpMotion = false;
						}

						if (SDL3_MapWindowMouseToGuiCursor(event.motion.x, event.motion.y, menuMouseX, menuMouseY)) {
							if (!s_haveMenuMousePosition) {
								s_menuMouseX = menuMouseX;
								s_menuMouseY = menuMouseY;
								s_haveMenuMousePosition = true;
							} else {
								const float deltaX = (menuMouseX - s_menuMouseX) + s_menuMouseRemainderX;
								const float deltaY = (menuMouseY - s_menuMouseY) + s_menuMouseRemainderY;
								dx = SDL3_RoundToInt(deltaX);
								dy = SDL3_RoundToInt(deltaY);
								s_menuMouseRemainderX = deltaX - static_cast<float>(dx);
								s_menuMouseRemainderY = deltaY - static_cast<float>(dy);
								s_menuMouseX = menuMouseX;
								s_menuMouseY = menuMouseY;
							}
							s_haveAbsoluteMousePosition = false;
						} else {
							SDL3_ResetMenuMouseTracking();
						}
					}
				} else {
					const int absoluteX = static_cast<int>(event.motion.x);
					const int absoluteY = static_cast<int>(event.motion.y);
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
					Sys_QueEvent(eventTime, SE_MOUSE, dx, dy, 0, NULL);
					if (dx != 0) {
						SDL3_QueueMouseInput(M_DELTAX, dx, eventTime);
					}
					if (dy != 0) {
						SDL3_QueueMouseInput(M_DELTAY, dy, eventTime);
					}
				}
				break;
			}

			case SDL_EVENT_MOUSE_BUTTON_DOWN:
			case SDL_EVENT_MOUSE_BUTTON_UP:
				if (SDL3_IsMouseCaptured() || SDL3_ShouldRouteMenuMouse()) {
					const int key = SDL3_MapMouseButton(event.button.button);
					if (key != 0) {
						const bool down = event.button.down;
						Sys_QueEvent(eventTime, SE_KEY, key, down, 0, NULL);
						SDL3_QueueMouseInput(M_ACTION1 + (key - K_MOUSE1), down ? 1 : 0, eventTime);
					}
				}
				break;

			case SDL_EVENT_MOUSE_WHEEL: {
				float deltaY = event.wheel.y;
				if (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) {
					deltaY = -deltaY;
				}

				int wheelSteps = static_cast<int>(deltaY);
				if (wheelSteps != 0) {
					const int wheelKey = wheelSteps < 0 ? K_MWHEELDOWN : K_MWHEELUP;
					const int absSteps = abs(wheelSteps);
					for (int i = 0; i < absSteps; ++i) {
						Sys_QueEvent(eventTime, SE_KEY, wheelKey, true, 0, NULL);
						Sys_QueEvent(eventTime, SE_KEY, wheelKey, false, 0, NULL);
					}
					SDL3_QueueMouseInput(M_DELTAZ, wheelSteps, eventTime);
				}
				break;
			}

			case SDL_EVENT_GAMEPAD_ADDED:
				if (in_joystick.GetBool()) {
					if (s_sdlJoystick) {
						SDL3_CloseJoystick(eventTime);
					}
					if (!s_sdlGamepad) {
						(void)SDL3_OpenGamepad(event.gdevice.which);
					}
				}
				break;

			case SDL_EVENT_GAMEPAD_REMOVED:
				if (s_sdlGamepad && event.gdevice.which == s_sdlGamepadId) {
					SDL3_CloseGamepad(eventTime);
					SDL3_OpenFirstController();
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

			default:
				break;
		}
	}

	// Keep render dimensions in sync even if the platform misses or coalesces resize events.
	SDL3_RefreshWindowPlacement();
	SDL3_UpdateWindowAspectSnap(sawResizeEvent);

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
	s_rightAltKey = K_ALT;
	if (lang.Icmp("spanish") == 0 ||
		lang.Icmp("french") == 0 ||
		lang.Icmp("german") == 0 ||
		lang.Icmp("italian") == 0) {
		s_rightAltKey = K_RIGHT_ALT;
	}
}

const unsigned char *Sys_GetScanTable(void) {
	return s_scantokey;
}

unsigned char Sys_GetConsoleKey(bool shifted) {
	return shifted ? s_scantoshift[41] : s_scantokey[41];
}

unsigned char Sys_MapCharForKey(int key) {
	return static_cast<unsigned char>(key);
}

void IN_ActivateMouse(void) {
	if (!s_sdlWindow || !win32.in_mouse.GetBool() || SDL3_IsMouseCaptured()) {
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
		SDL3_SyncSystemMouseToActiveGUICursor();
	} else {
		SDL3_ResetMenuMouseTracking();
	}
	win32.mouseGrabbed = SDL3_IsMouseCaptured();
	s_haveAbsoluteMousePosition = false;
	SDL3_UpdateCursorVisibility();
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

	if (!win32.in_mouse.GetBool()) {
		shouldGrab = false;
	}
	if (routeMenuMouse) {
		shouldGrab = false;
	}

	if (!win32.cdsFullscreen) {
		if (win32.mouseReleased) {
			shouldGrab = false;
		}
		if (win32.movingWindow) {
			shouldGrab = false;
		}
		if (!win32.activeApp) {
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

	if (routeMenuMouse && !s_menuMouseRouteActive && !s_haveMenuMousePosition) {
		// Match WORR-style behavior: align OS cursor to menu cursor when menu routing starts.
		SDL3_SyncSystemMouseToActiveGUICursor();
	} else if (!routeMenuMouse && s_menuMouseRouteActive) {
		SDL3_ResetMenuMouseTracking();
	}
	s_menuMouseRouteActive = routeMenuMouse;
	SDL3_UpdateCursorVisibility();
}

void Sys_GrabMouseCursor(bool grabIt) {
#ifndef ID_DEDICATED
	win32.mouseReleased = !grabIt;
	if (!grabIt) {
		IN_Frame();
	}
#else
	(void)grabIt;
#endif
}

void Sys_InitInput(void) {
	common->Printf("\n------- Input Initialization -------\n");
	win32.activeApp = true;
	win32.mouseReleased = false;
	win32.movingWindow = false;
	win32.mouseGrabbed = SDL3_IsMouseCaptured();
	s_menuMouseRouteActive = false;
	SDL3_ResetMenuMouseTracking();
	s_menuMouseInsideWindow = true;
	SDL3_UpdateCursorVisibility();

	if (s_sdlWindow && !s_sdlTextInputActive) {
		if (SDL_StartTextInput(s_sdlWindow)) {
			s_sdlTextInputActive = true;
		} else {
			common->Printf("SDL3: text input could not be enabled: %s\n", SDL_GetError());
		}
	}

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
	SDL3_ClearInputQueues();
	common->Printf("------------------------------------\n");
}

void Sys_ShutdownInput(void) {
	IN_DeactivateMouse();

	if (s_sdlWindow && s_sdlTextInputActive) {
		(void)SDL_StopTextInput(s_sdlWindow);
		s_sdlTextInputActive = false;
	}

	SDL3_ShutdownControllerSubsystems();
	SDL3_ClearInputQueues();
}

int Sys_PollKeyboardInputEvents(void) {
	Sys_EnterCriticalSection(CRITICAL_SECTION_ONE);

	s_polledKeyboardCount = 0;
	while (s_keyboardTail != s_keyboardHead && s_polledKeyboardCount < SDL3_INPUT_QUEUE_SIZE) {
		s_polledKeyboard[s_polledKeyboardCount] = s_keyboardQueue[s_keyboardTail];
		s_polledKeyboardCount++;
		s_keyboardTail = (s_keyboardTail + 1) & SDL3_INPUT_QUEUE_MASK;
	}

	Sys_LeaveCriticalSection(CRITICAL_SECTION_ONE);
	return s_polledKeyboardCount;
}

int Sys_ReturnKeyboardInputEvent(const int n, int &ch, bool &state) {
	if (n < 0 || n >= s_polledKeyboardCount) {
		ch = 0;
		state = false;
		return 0;
	}

	ch = s_polledKeyboard[n].key;
	state = s_polledKeyboard[n].down;

	if (ch == K_PRINT_SCR || ch == K_CTRL || ch == K_ALT || ch == K_RIGHT_ALT) {
		Sys_QueEvent(s_polledKeyboard[n].time, SE_KEY, ch, state, 0, NULL);
	}

	return ch;
}

void Sys_EndKeyboardInputEvents(void) {
}

int Sys_PollMouseInputEvents(void) {
	Sys_EnterCriticalSection(CRITICAL_SECTION_ONE);

	s_polledMouseCount = 0;
	while (s_mouseTail != s_mouseHead && s_polledMouseCount < SDL3_INPUT_QUEUE_SIZE) {
		s_polledMouse[s_polledMouseCount] = s_mouseQueue[s_mouseTail];
		s_polledMouseCount++;
		s_mouseTail = (s_mouseTail + 1) & SDL3_INPUT_QUEUE_MASK;
	}

	Sys_LeaveCriticalSection(CRITICAL_SECTION_ONE);
	return s_polledMouseCount;
}

int Sys_ReturnMouseInputEvent(const int n, int &action, int &value) {
	if (n < 0 || n >= s_polledMouseCount) {
		return 0;
	}

	action = s_polledMouse[n].action;
	value = s_polledMouse[n].value;

	if (action == M_DELTAZ && value == 0) {
		return 0;
	}

	return 1;
}

void Sys_EndMouseInputEvents(void) {
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
		s_polledJoystick[s_polledJoystickCount].value = s_joystickAxisState[axis];
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

void GLimp_SetGamma(unsigned short red[256], unsigned short green[256], unsigned short blue[256]) {
	(void)red;
	(void)green;
	(void)blue;
}

bool GLimp_Init(glimpParms_t parms) {
	const char *driverName;

	common->Printf("Initializing OpenGL subsystem (SDL3 backend)\n");

	if (!s_sdlVideoActive) {
		if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
			common->Printf("SDL3: failed to initialize video subsystem: %s\n", SDL_GetError());
			return false;
		}
		s_sdlVideoActive = true;
	}

	if (!s_sdlDisplayCommandRegistered) {
		cmdSystem->AddCommand("listDisplays", SDL3_ListDisplays_f, CMD_FL_SYSTEM, "lists SDL3 displays and monitor indices");
		cmdSystem->AddCommand("listDisplayModes", SDL3_ListDisplayModes_f, CMD_FL_SYSTEM, "lists SDL3 fullscreen display modes (optional display index)");
		s_sdlDisplayCommandRegistered = true;
	}

	if (!s_sdlDisplaySummaryLogged) {
		SDL3_PrintDisplayList();
		s_sdlDisplaySummaryLogged = true;
	}

	SDL3_InitDesktopMode();

	SDL_GL_ResetAttributes();
	(void)SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
	(void)SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
	(void)SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
	(void)SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
	(void)SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
	(void)SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
	(void)SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	if (parms.stereo) {
		(void)SDL_GL_SetAttribute(SDL_GL_STEREO, 1);
	}
	if (parms.multiSamples > 1) {
		(void)SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
		(void)SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, parms.multiSamples);
	}

	SDL_WindowFlags flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
	if (!parms.fullScreen && parms.borderless) {
		flags |= SDL_WINDOW_BORDERLESS;
	}
	s_sdlWindow = SDL_CreateWindow(GAME_NAME, parms.width, parms.height, flags);
	if (!s_sdlWindow) {
		common->Printf("SDL3: could not create window: %s\n", SDL_GetError());
		return false;
	}

	if (!parms.fullScreen) {
		int targetX = win32.win_xpos.GetInteger();
		int targetY = win32.win_ypos.GetInteger();
		int targetWidth = parms.width;
		int targetHeight = parms.height;

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
		(void)SDL_SetWindowPosition(s_sdlWindow, targetX, targetY);
	}

	s_sdlContext = SDL_GL_CreateContext(s_sdlWindow);
	if (!s_sdlContext) {
		common->Printf("SDL3: could not create OpenGL context: %s\n", SDL_GetError());
		SDL_DestroyWindow(s_sdlWindow);
		s_sdlWindow = NULL;
		return false;
	}

	if (!SDL_GL_MakeCurrent(s_sdlWindow, s_sdlContext)) {
		common->Printf("SDL3: could not make context current: %s\n", SDL_GetError());
		(void)SDL_GL_DestroyContext(s_sdlContext);
		SDL_DestroyWindow(s_sdlWindow);
		s_sdlContext = NULL;
		s_sdlWindow = NULL;
		return false;
	}

	driverName = r_glDriver.GetString()[0] ? r_glDriver.GetString() : "opengl32";
	if (!QGL_Init(driverName)) {
		common->Printf("^3GLimp_Init() could not load r_glDriver \"%s\"^0\n", driverName);
		GLimp_Shutdown();
		return false;
	}

	if (!SDL3_ApplyScreenParms(parms)) {
		GLimp_Shutdown();
		return false;
	}

	SDL3_UpdateNativeWindowHandles();
	SDL3_LoadWGLExtensions();

	win32.activeApp = true;
	win32.wglErrors = 0;
	GLimp_EnableLogging((r_logFile.GetInteger() != 0));

	return true;
}

bool GLimp_SetScreenParms(glimpParms_t parms) {
	if (!SDL3_ApplyScreenParms(parms)) {
		return false;
	}

	if (s_sdlWindow && s_sdlContext && !SDL_GL_MakeCurrent(s_sdlWindow, s_sdlContext)) {
		common->Printf("SDL3: failed to reactivate GL context after screen parm change: %s\n", SDL_GetError());
		win32.wglErrors++;
		return false;
	}

	SDL3_UpdateNativeWindowHandles();

	return true;
}

void GLimp_Shutdown(void) {
	common->Printf("Shutting down OpenGL subsystem (SDL3 backend)\n");

	SDL3_DisableWindowAspectSnap();
	IN_DeactivateMouse();
	SDL3_ShutdownControllerSubsystems();
	(void)SDL3_LeaveFullscreenAndRestoreDesktopMode();
	if (s_sdlWindow && s_sdlTextInputActive) {
		(void)SDL_StopTextInput(s_sdlWindow);
		s_sdlTextInputActive = false;
	}

	if (s_sdlContext) {
		(void)SDL_GL_MakeCurrent(s_sdlWindow, NULL);
		(void)SDL_GL_DestroyContext(s_sdlContext);
		s_sdlContext = NULL;
	}

	if (s_sdlWindow) {
		SDL_DestroyWindow(s_sdlWindow);
		s_sdlWindow = NULL;
	}

	if (s_sdlVideoActive) {
		SDL_QuitSubSystem(SDL_INIT_VIDEO);
		s_sdlVideoActive = false;
	}

	win32.hWnd = NULL;
	win32.hDC = NULL;
	win32.hGLRC = NULL;
	win32.cdsFullscreen = false;
	glConfig.isFullscreen = false;

	SDL3_ClearInputQueues();
	QGL_Shutdown();
}

void GLimp_SwapBuffers(void) {
	if (r_swapInterval.IsModified()) {
		r_swapInterval.ClearModified();
		if (!SDL_GL_SetSwapInterval(r_swapInterval.GetInteger())) {
			common->Printf("SDL3: failed to set swap interval: %s\n", SDL_GetError());
		}
	}

	if (s_sdlWindow && !SDL_GL_SwapWindow(s_sdlWindow)) {
		common->Printf("SDL3: failed to swap window buffers: %s\n", SDL_GetError());
	}
}

/*
===========================================================

SMP acceleration

===========================================================
*/

static void GLimp_RenderThreadWrapper(void) {
	win32.glimpRenderThread();
	(void)SDL_GL_MakeCurrent(s_sdlWindow, NULL);
}

bool GLimp_SpawnRenderThread(void (*function)(void)) {
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
}

void *GLimp_BackEndSleep(void) {
	void *data;

	ResetEvent(win32.renderActiveEvent);
	SetEvent(win32.renderCompletedEvent);
	WaitForSingleObject(win32.renderCommandsEvent, INFINITE);

	ResetEvent(win32.renderCompletedEvent);
	ResetEvent(win32.renderCommandsEvent);
	data = win32.smpData;
	SetEvent(win32.renderActiveEvent);

	return data;
}

void GLimp_FrontEndSleep(void) {
	WaitForSingleObject(win32.renderCompletedEvent, INFINITE);
}

void GLimp_WakeBackEnd(void *data) {
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
}

void GLimp_ActivateContext(void) {
	if (!s_sdlWindow || !s_sdlContext) {
		return;
	}

	if (!SDL_GL_MakeCurrent(s_sdlWindow, s_sdlContext)) {
		win32.wglErrors++;
	}
}

void GLimp_DeactivateContext(void) {
	glFinish();
	if (!s_sdlWindow) {
		return;
	}

	if (!SDL_GL_MakeCurrent(s_sdlWindow, NULL)) {
		win32.wglErrors++;
	}
}

void *GLimp_ExtensionPointer(const char *name) {
	void *proc = (void *)SDL_GL_GetProcAddress(name);
	if (!proc && qwglGetProcAddress) {
		proc = (void *)qwglGetProcAddress(name);
	}

	if (!proc) {
		common->Printf("Couldn't find proc address for: %s\n", name);
	}
	return proc;
}
