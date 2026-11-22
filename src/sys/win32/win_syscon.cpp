/*
===========================================================================

Doom 3 GPL Source Code
Copyright (C) 1999-2011 id Software LLC, a ZeniMax Media company.

This file is part of the Doom 3 GPL Source Code (?Doom 3 Source Code?).

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

In addition, the Doom 3 Source Code is also subject to certain additional terms. You should have received a copy of these additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the Doom 3 Source Code.  If not, please request a copy in writing from id Software at the address below.

If you have questions concerning this license or the applicable additional terms, you may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.

===========================================================================
*/




#include <errno.h>
#include <float.h>
#include <fcntl.h>
#include <stdio.h>
#include <direct.h>
#include <io.h>
#include <conio.h>

#include "win_local.h"
#include "../sys_console_theme.h"
#include "rc/AFEditor_resource.h"
#include "rc/doom_resource.h"

#define COPY_ID			1
#define QUIT_ID			2
#define CLEAR_ID		3

#define BUTTON_COUNT	3

// Design metrics in sys_console_theme.h are authored at 96 DPI.
#define SYSCON_REFERENCE_DPI	96

// Shared palette entry as a COLORREF. SysCon_Color is a function rather than
// RGB() directly because MSVC cannot expand SYSCON_RGB into the arguments of a
// function-like macro; see the note in sys_console_theme.h.
#define SYSCON_WIN_RGB( name )	SysCon_Color( SYSCON_RGB( name ) )

// Windows 8.1 SDK and newer; declared here so the console builds against the
// older SDK headers the rest of the tree targets.
#ifndef WM_DPICHANGED
#define WM_DPICHANGED	0x02E0
#endif

#define ERRORBOX_ID		10
#define ERRORTEXT_ID	11

#define EDIT_ID			100
#define INPUT_ID		101

#define	COMMAND_HISTORY	64

typedef struct {
	HWND		hWnd;
	HWND		hWndSplash;
	HWND		hwndBuffer;

	HWND		hwndButtonClear;
	HWND		hwndButtonCopy;
	HWND		hwndButtonQuit;

	HWND		hwndErrorBox;
	HWND		hwndErrorText;

	HBITMAP		hbmLogo;
	HBITMAP		hbmClearBitmap;

	HBRUSH		hbrWindowBackground;
	HBRUSH		hbrEditBackground;
	HBRUSH		hbrErrorBackground;
	HBRUSH		hbrAlertBackground;
	HBRUSH		hbrInputBackground;

	HFONT		hfBufferFont;
	HFONT		hfButtonFont;

	HWND		hwndInputLine;

	char		errorString[512];
	bool		errorIsFatal;

	// Design metrics are scaled by this; the process is per-monitor DPI aware,
	// so Windows hands us raw pixels and every metric has to be scaled by hand.
	int			dpi;

	// Owner-drawn buttons have no built-in hot state, so hover is tracked here
	// to match the hover feedback a themed native push button would give.
	int			hotButton;

	char		consoleText[512], returnedText[512];
	bool		quitOnClose;
	int			windowWidth, windowHeight;

	WNDPROC		SysInputLineWndProc;
	WNDPROC		SysButtonWndProc;

	idEditField	historyEditLines[COMMAND_HISTORY];

	int			nextHistoryLine;// the last line in the history buffer, not masked
	int			historyLine;	// the line being displayed from history buffer
								// will be <= nextHistoryLine

	idEditField	consoleField;

} WinConData;

static WinConData s_wcd;

static COLORREF SysCon_Color(int red, int green, int blue) {
	return RGB(red, green, blue);
}

/*
** SysCon_QueryDpi
**
** Per-monitor DPI for the console window. GetDpiForWindow is Windows 10 1607+,
** so fall back to the device context's logical pixel density, then to the
** 96 DPI the design metrics are authored against.
*/
static int SysCon_QueryDpi(HWND hWnd) {
	HMODULE user32 = GetModuleHandleA("user32.dll");
	if (user32 != NULL && hWnd != NULL) {
		typedef UINT (WINAPI *GetDpiForWindowFn)(HWND);
		GetDpiForWindowFn getDpiForWindow =
			reinterpret_cast<GetDpiForWindowFn>( GetProcAddress(user32, "GetDpiForWindow") );
		if (getDpiForWindow != NULL) {
			const UINT dpi = getDpiForWindow(hWnd);
			if (dpi >= 48) {
				return static_cast<int>( dpi );
			}
		}
	}

	HDC hDC = GetDC(hWnd);
	if (hDC != NULL) {
		const int dpi = GetDeviceCaps(hDC, LOGPIXELSY);
		ReleaseDC(hWnd, hDC);
		if (dpi >= 48) {
			return dpi;
		}
	}

	return SYSCON_REFERENCE_DPI;
}

// Converts a 96-DPI design unit from sys_console_theme.h to device pixels.
static int SysCon_Scale(int designUnits) {
	const int dpi = s_wcd.dpi > 0 ? s_wcd.dpi : SYSCON_REFERENCE_DPI;
	return MulDiv(designUnits, dpi, SYSCON_REFERENCE_DPI);
}

/*
** SysCon_ApplyDarkWindowFrame
**
** Opts the title bar into the dark frame so it stops reading as a white cap on
** a dark window, and puts the log control's scrollbar into the dark theme so
** it stops reading as a light stripe down the side of the log. Both are best
** effort: on builds without support the calls fail and the window keeps the
** light frame, which is still correct, just less cohesive.
*/
static void SysCon_ApplyDarkWindowFrame(HWND hWnd) {
	HMODULE dwmapi = LoadLibraryA("dwmapi.dll");
	if (dwmapi != NULL) {
		typedef HRESULT (WINAPI *DwmSetWindowAttributeFn)(HWND, DWORD, LPCVOID, DWORD);
		DwmSetWindowAttributeFn dwmSetWindowAttribute =
			reinterpret_cast<DwmSetWindowAttributeFn>( GetProcAddress(dwmapi, "DwmSetWindowAttribute") );
		if (dwmSetWindowAttribute != NULL) {
			static const DWORD USE_IMMERSIVE_DARK_MODE = 20;
			static const DWORD USE_IMMERSIVE_DARK_MODE_PRE_20H1 = 19;
			BOOL useDarkMode = TRUE;
			if (FAILED(dwmSetWindowAttribute(hWnd, USE_IMMERSIVE_DARK_MODE, &useDarkMode, sizeof(useDarkMode)))) {
				(void)dwmSetWindowAttribute(hWnd, USE_IMMERSIVE_DARK_MODE_PRE_20H1, &useDarkMode, sizeof(useDarkMode));
			}
		}
		FreeLibrary(dwmapi);
	}
}

static void SysCon_ApplyDarkScrollbars(HWND hWnd) {
	HMODULE uxtheme = LoadLibraryA("uxtheme.dll");
	if (uxtheme == NULL) {
		return;
	}

	typedef HRESULT (WINAPI *SetWindowThemeFn)(HWND, LPCWSTR, LPCWSTR);
	SetWindowThemeFn setWindowTheme =
		reinterpret_cast<SetWindowThemeFn>( GetProcAddress(uxtheme, "SetWindowTheme") );
	if (setWindowTheme != NULL) {
		(void)setWindowTheme(hWnd, L"DarkMode_Explorer", NULL);
	}
	FreeLibrary(uxtheme);
}

/*
** SysCon_CreateFonts
**
** Consolas at SYSCON_METRIC_MONO_PT has an ascent+descent of exactly one em,
** which lands its line height on SYSCON_METRIC_LINE_H: the same log pitch the
** SDL console draws. Segoe UI carries the chrome. Both request a family so
** font substitution stays in the right class if the face is missing.
*/
static void SysCon_DestroyFonts(void) {
	if (s_wcd.hfBufferFont) {
		DeleteObject(s_wcd.hfBufferFont);
		s_wcd.hfBufferFont = NULL;
	}
	if (s_wcd.hfButtonFont) {
		DeleteObject(s_wcd.hfButtonFont);
		s_wcd.hfButtonFont = NULL;
	}
}

static void SysCon_CreateFonts(void) {
	SysCon_DestroyFonts();

	const int dpi = s_wcd.dpi > 0 ? s_wcd.dpi : SYSCON_REFERENCE_DPI;

	s_wcd.hfBufferFont = CreateFont(-MulDiv(SYSCON_METRIC_MONO_PT, dpi, 72), 0, 0, 0, FW_NORMAL, 0, 0, 0,
		DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
		FF_MODERN | FIXED_PITCH, "Consolas");

	s_wcd.hfButtonFont = CreateFont(-MulDiv(SYSCON_METRIC_UI_PT, dpi, 72), 0, 0, 0, FW_NORMAL, 0, 0, 0,
		DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
		FF_SWISS | VARIABLE_PITCH, "Segoe UI");
}

static void SysCon_ApplyFonts(void) {
	if (s_wcd.hwndBuffer) {
		SendMessage(s_wcd.hwndBuffer, WM_SETFONT, (WPARAM)s_wcd.hfBufferFont, MAKELPARAM(TRUE, 0));
	}
	if (s_wcd.hwndInputLine) {
		SendMessage(s_wcd.hwndInputLine, WM_SETFONT, (WPARAM)s_wcd.hfBufferFont, MAKELPARAM(TRUE, 0));
	}
	if (s_wcd.hwndErrorBox) {
		SendMessage(s_wcd.hwndErrorBox, WM_SETFONT, (WPARAM)s_wcd.hfButtonFont, MAKELPARAM(TRUE, 0));
	}

	HWND buttons[BUTTON_COUNT] = { s_wcd.hwndButtonCopy, s_wcd.hwndButtonClear, s_wcd.hwndButtonQuit };
	for (int i = 0; i < BUTTON_COUNT; i++) {
		if (buttons[i]) {
			SendMessage(buttons[i], WM_SETFONT, (WPARAM)s_wcd.hfButtonFont, MAKELPARAM(TRUE, 0));
		}
	}
}

/*
** SysCon_FrameChild
**
** Draws a one-pixel themed frame in the parent's background, just outside a
** child control. The native WS_BORDER/SS_SUNKEN edges are system-coloured and
** read as light grey scratches on a dark console, so the panel outlines the SDL
** console draws are reproduced here by hand instead.
*/
static void SysCon_FrameChild(HDC hdc, HWND child, COLORREF frameColor) {
	if (!child) {
		return;
	}

	RECT rect;
	GetWindowRect(child, &rect);
	MapWindowPoints(NULL, s_wcd.hWnd, reinterpret_cast<POINT*>( &rect ), 2);
	InflateRect(&rect, 1, 1);

	HBRUSH brush = CreateSolidBrush(frameColor);
	if (brush != NULL) {
		FrameRect(hdc, &rect, brush);
		DeleteObject(brush);
	}
}

static void SysCon_DrawPanelFrames(HDC hdc) {
	SysCon_FrameChild(hdc, s_wcd.hwndErrorBox,
		s_wcd.errorIsFatal ? SYSCON_WIN_RGB(ALERT) : SYSCON_WIN_RGB(BORDER_LIT));
	SysCon_FrameChild(hdc, s_wcd.hwndBuffer, SYSCON_WIN_RGB(BORDER_LIT));
	// Focus ring on the command line, matching the SDL console.
	SysCon_FrameChild(hdc, s_wcd.hwndInputLine,
		GetFocus() == s_wcd.hwndInputLine ? SYSCON_WIN_RGB(TEXT) : SYSCON_WIN_RGB(BORDER));
}

static void SysCon_InvalidateInputFrame(void) {
	if (!s_wcd.hWnd || !s_wcd.hwndInputLine) {
		return;
	}

	RECT rect;
	GetWindowRect(s_wcd.hwndInputLine, &rect);
	MapWindowPoints(NULL, s_wcd.hWnd, reinterpret_cast<POINT*>( &rect ), 2);
	InflateRect(&rect, 2, 2);
	InvalidateRect(s_wcd.hWnd, &rect, TRUE);
}

/*
** SysCon_LayoutChildren
**
** Reflows every child from the current client rect. Rows stack
** margin / status / log / input / buttons / margin separated by one gutter,
** exactly as sys/posix/posix_syscon.cpp lays out the SDL console.
*/
static void SysCon_LayoutChildren(void) {
	if (!s_wcd.hWnd) {
		return;
	}

	RECT client;
	GetClientRect(s_wcd.hWnd, &client);

	const int width = client.right - client.left;
	const int height = client.bottom - client.top;
	if (width <= 0 || height <= 0) {
		return;
	}

	const int margin = SysCon_Scale(SYSCON_METRIC_MARGIN);
	const int gutter = SysCon_Scale(SYSCON_METRIC_GUTTER);
	const int statusHeight = SysCon_Scale(SYSCON_METRIC_STATUS_H);
	const int inputHeight = SysCon_Scale(SYSCON_METRIC_INPUT_H);
	const int buttonWidth = SysCon_Scale(SYSCON_METRIC_BUTTON_W);
	const int buttonHeight = SysCon_Scale(SYSCON_METRIC_BUTTON_H);
	const int contentWidth = Max(1, width - margin * 2);

	const int buttonY = height - margin - buttonHeight;
	const int inputY = buttonY - gutter - inputHeight;
	const int bufferY = margin + statusHeight + gutter;
	const int bufferHeight = Max(SysCon_Scale(SYSCON_METRIC_LINE_H), inputY - gutter - bufferY);

	HDWP defer = BeginDeferWindowPos(6);
	const UINT flags = SWP_NOZORDER | SWP_NOACTIVATE;

	struct {
		HWND hWnd;
		int x, y, cx, cy;
	} placements[6] = {
		{ s_wcd.hwndErrorBox,     margin, margin,   contentWidth, statusHeight },
		{ s_wcd.hwndBuffer,       margin, bufferY,  contentWidth, bufferHeight },
		{ s_wcd.hwndInputLine,    margin, inputY,   contentWidth, inputHeight },
		{ s_wcd.hwndButtonCopy,   margin, buttonY,  buttonWidth,  buttonHeight },
		{ s_wcd.hwndButtonClear,  margin + buttonWidth + gutter, buttonY, buttonWidth, buttonHeight },
		// Quit is destructive, so it is pushed to the far corner away from the
		// two log buttons rather than sitting next to them.
		{ s_wcd.hwndButtonQuit,   width - margin - buttonWidth, buttonY, buttonWidth, buttonHeight },
	};

	for (int i = 0; i < 6; i++) {
		if (!placements[i].hWnd) {
			continue;
		}
		if (defer != NULL) {
			defer = DeferWindowPos(defer, placements[i].hWnd, NULL,
				placements[i].x, placements[i].y, placements[i].cx, placements[i].cy, flags);
		} else {
			SetWindowPos(placements[i].hWnd, NULL,
				placements[i].x, placements[i].y, placements[i].cx, placements[i].cy, flags);
		}
	}

	if (defer != NULL) {
		EndDeferWindowPos(defer);
	}

	InvalidateRect(s_wcd.hWnd, NULL, TRUE);
}

static LRESULT CALLBACK SplashWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	switch (uMsg) {
	case WM_ERASEBKGND:
		// Avoid background clears that can cause visible flicker on startup.
		return 1;
	case WM_PAINT:
	{
		PAINTSTRUCT ps;
		HDC hdc = BeginPaint(hWnd, &ps);
		if (s_wcd.hbmLogo) {
			BITMAP bitmapInfo;
			if (GetObject(s_wcd.hbmLogo, sizeof(bitmapInfo), &bitmapInfo) == sizeof(bitmapInfo)) {
				HDC hdcBitmap = CreateCompatibleDC(hdc);
				if (hdcBitmap) {
					HGDIOBJ oldBitmap = SelectObject(hdcBitmap, s_wcd.hbmLogo);
					if (oldBitmap) {
						RECT clientRect;
						GetClientRect(hWnd, &clientRect);
						const int clientWidth = clientRect.right - clientRect.left;
						const int clientHeight = clientRect.bottom - clientRect.top;
						int drawX = 0;
						int drawY = 0;
						if (clientWidth > bitmapInfo.bmWidth) {
							drawX = (clientWidth - bitmapInfo.bmWidth) / 2;
						}
						if (clientHeight > bitmapInfo.bmHeight) {
							drawY = (clientHeight - bitmapInfo.bmHeight) / 2;
						}
						BitBlt(
							hdc,
							drawX,
							drawY,
							bitmapInfo.bmWidth,
							bitmapInfo.bmHeight,
							hdcBitmap,
							0,
							0,
							SRCCOPY
						);
						SelectObject(hdcBitmap, oldBitmap);
					}
					DeleteDC(hdcBitmap);
				}
			}
		}
		EndPaint(hWnd, &ps);
		return 0;
	}
	case WM_CLOSE:
		return 0;
	default:
		break;
	}

	return DefWindowProc(hWnd, uMsg, wParam, lParam);
}

void Sys_ShowSplash(void) {
	if (s_wcd.hWndSplash) {
		return;
	}

	if (!s_wcd.hbmLogo) {
		s_wcd.hbmLogo = LoadBitmap(win32.hInstance, MAKEINTRESOURCE(IDB_BITMAP_LOGO));
	}
	if (!s_wcd.hbmLogo) {
		return;
	}

	BITMAP bitmapInfo;
	if (GetObject(s_wcd.hbmLogo, sizeof(bitmapInfo), &bitmapInfo) != sizeof(bitmapInfo)) {
		return;
	}

	WNDCLASS wc;
	memset(&wc, 0, sizeof(wc));
	wc.style = 0;
	wc.lpfnWndProc = SplashWndProc;
	wc.cbClsExtra = 0;
	wc.cbWndExtra = 0;
	wc.hInstance = win32.hInstance;
	wc.hIcon = LoadIcon(win32.hInstance, MAKEINTRESOURCE(IDI_ICON1));
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.hbrBackground = (struct HBRUSH__*)COLOR_WINDOW;
	wc.lpszMenuName = 0;
	wc.lpszClassName = WIN32_SPLASH_CLASS;

	if (!RegisterClass(&wc)) {
		DWORD errorCode = GetLastError();
		if (errorCode != ERROR_CLASS_ALREADY_EXISTS) {
			return;
		}
	}

	const int swidth = GetSystemMetrics(SM_CXSCREEN);
	const int sheight = GetSystemMetrics(SM_CYSCREEN);
	int splashX = (swidth - bitmapInfo.bmWidth) / 2;
	int splashY = (sheight - bitmapInfo.bmHeight) / 2;
	if (splashX < 0) {
		splashX = 0;
	}
	if (splashY < 0) {
		splashY = 0;
	}

	s_wcd.hWndSplash = CreateWindowEx(
		WS_EX_TOOLWINDOW,
		WIN32_SPLASH_CLASS,
		GAME_NAME,
		WS_POPUP,
		splashX,
		splashY,
		bitmapInfo.bmWidth,
		bitmapInfo.bmHeight,
		NULL,
		NULL,
		win32.hInstance,
		NULL
	);

	if (!s_wcd.hWndSplash) {
		return;
	}

	ShowWindow(s_wcd.hWndSplash, SW_SHOWNORMAL);
	UpdateWindow(s_wcd.hWndSplash);
}

// The game window is shown while the splash still holds the foreground, so the
// game window takes it over instead of Windows handing it to another program,
// and the splash is then owned by the game window: an owned window always sits
// above its owner, so the splash stays visible over the not yet drawn game
// window without being raised above other programs.
void Sys_KeepSplashAboveWindow(void *nativeWindow) {
	if (!s_wcd.hWndSplash || !IsWindow(s_wcd.hWndSplash)) {
		return;
	}
	HWND owner = static_cast<HWND>(nativeWindow);
	if (owner != NULL && !IsWindow(owner)) {
		owner = NULL;
	}
	SetWindowLongPtr(s_wcd.hWndSplash, GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR>(owner));
	if (owner != NULL) {
		SetWindowPos(s_wcd.hWndSplash, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
	}
}

void Sys_DestroySplash(void) {
	if (s_wcd.hWndSplash) {
		// Windows destroys an owned splash along with its owner.
		if (IsWindow(s_wcd.hWndSplash)) {
			ShowWindow(s_wcd.hWndSplash, SW_HIDE);
			DestroyWindow(s_wcd.hWndSplash);
		}
		s_wcd.hWndSplash = NULL;
	}
}

/*
** SysConButtonWndProc
**
** Owner-drawn buttons get ODS_SELECTED for free but have no hot state, so the
** hover highlight is tracked here. TrackMouseEvent is re-armed on every move
** because WM_MOUSELEAVE cancels the request.
*/
static LRESULT CALLBACK SysConButtonWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	const int buttonId = GetDlgCtrlID(hWnd);

	switch (uMsg) {
	case WM_MOUSEMOVE:
		if (s_wcd.hotButton != buttonId) {
			s_wcd.hotButton = buttonId;
			InvalidateRect(hWnd, NULL, TRUE);
		}
		{
			TRACKMOUSEEVENT track;
			memset(&track, 0, sizeof(track));
			track.cbSize = sizeof(track);
			track.dwFlags = TME_LEAVE;
			track.hwndTrack = hWnd;
			TrackMouseEvent(&track);
		}
		break;
	case WM_MOUSELEAVE:
		if (s_wcd.hotButton == buttonId) {
			s_wcd.hotButton = 0;
			InvalidateRect(hWnd, NULL, TRUE);
		}
		break;
	default:
		break;
	}

	return CallWindowProc(s_wcd.SysButtonWndProc, hWnd, uMsg, wParam, lParam);
}

/*
** SysCon_DrawButton
**
** Flat themed push button. The native control would paint a light face that
** fights the dark log, so the face, border and label all come from the shared
** palette and match what the SDL console draws for the same three buttons.
*/
static void SysCon_DrawButton(const DRAWITEMSTRUCT* item) {
	if (item == NULL || item->CtlType != ODT_BUTTON) {
		return;
	}

	const bool isPressed = ( item->itemState & ODS_SELECTED ) != 0;
	const bool isHot = ( s_wcd.hotButton == static_cast<int>( item->CtlID ) );

	COLORREF face = SYSCON_WIN_RGB(BUTTON);
	if (isPressed) {
		face = SYSCON_WIN_RGB(BUTTON_DOWN);
	} else if (isHot) {
		face = SYSCON_WIN_RGB(BUTTON_HOT);
	}

	RECT rect = item->rcItem;
	HBRUSH faceBrush = CreateSolidBrush(face);
	if (faceBrush != NULL) {
		FillRect(item->hDC, &rect, faceBrush);
		DeleteObject(faceBrush);
	}

	HBRUSH borderBrush = CreateSolidBrush(isHot ? SYSCON_WIN_RGB(TEXT) : SYSCON_WIN_RGB(BORDER_LIT));
	if (borderBrush != NULL) {
		FrameRect(item->hDC, &rect, borderBrush);
		DeleteObject(borderBrush);
	}

	char label[64];
	label[0] = '\0';
	GetWindowText(item->hwndItem, label, sizeof(label));

	// A pressed button nudges its label down a pixel, the same tactile cue the
	// SDL console gives.
	if (isPressed) {
		OffsetRect(&rect, 0, 1);
	}

	const int oldMode = SetBkMode(item->hDC, TRANSPARENT);
	const COLORREF oldColor = SetTextColor(item->hDC, SYSCON_WIN_RGB(TEXT));
	HGDIOBJ oldFont = SelectObject(item->hDC, s_wcd.hfButtonFont);
	DrawText(item->hDC, label, -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	if (oldFont != NULL) {
		SelectObject(item->hDC, oldFont);
	}
	SetTextColor(item->hDC, oldColor);
	SetBkMode(item->hDC, oldMode);

	if (( item->itemState & ODS_FOCUS ) != 0) {
		RECT focusRect = rect;
		InflateRect(&focusRect, -SysCon_Scale(3), -SysCon_Scale(3));
		DrawFocusRect(item->hDC, &focusRect);
	}
}

static LRESULT CALLBACK ConWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	char* cmdString;

	switch (uMsg) {
	case WM_ACTIVATE:
		if (LOWORD(wParam) != WA_INACTIVE) {
			SetFocus(s_wcd.hwndInputLine);
		}
		break;
	case WM_CLOSE:
		if (cvarSystem->IsInitialized() && com_skipRenderer.GetBool()) {
			cmdString = Mem_CopyString("quit");
			Sys_QueEvent(0, SE_CONSOLE, 0, 0,
				idLib::SizeToInt( strlen( cmdString ) + 1, "ConWndProc quit command" ), cmdString);
		}
		else if (s_wcd.quitOnClose) {
			PostQuitMessage(0);
		}
		else {
			Sys_ShowConsole(0, false);
			win32.win_viewlog.SetBool(false);
		}
		return 0;
	case WM_CTLCOLORSTATIC:
		// A read-only edit reports itself as static, which is why the log
		// control lands here rather than in WM_CTLCOLOREDIT.
		if ((HWND)lParam == s_wcd.hwndBuffer) {
			SetBkColor((HDC)wParam, SYSCON_WIN_RGB(PANEL));
			SetTextColor((HDC)wParam, SYSCON_WIN_RGB(TEXT));
			return reinterpret_cast<LRESULT>( s_wcd.hbrEditBackground );
		}
		else if ((HWND)lParam == s_wcd.hwndErrorBox) {
			if (s_wcd.errorIsFatal) {
				SetBkColor((HDC)wParam, SYSCON_WIN_RGB(ALERT_PANEL));
				SetTextColor((HDC)wParam, SYSCON_WIN_RGB(ALERT));
				return reinterpret_cast<LRESULT>( s_wcd.hbrAlertBackground );
			}
			SetBkColor((HDC)wParam, SYSCON_WIN_RGB(PANEL));
			SetTextColor((HDC)wParam, SYSCON_WIN_RGB(TEXT_DIM));
			return reinterpret_cast<LRESULT>( s_wcd.hbrErrorBackground );
		}
		break;
	case WM_CTLCOLOREDIT:
		// Without this the writable command line keeps the default white
		// system field and reads as a foreign control on a dark console.
		if ((HWND)lParam == s_wcd.hwndInputLine) {
			SetBkColor((HDC)wParam, SYSCON_WIN_RGB(INPUT));
			SetTextColor((HDC)wParam, SYSCON_WIN_RGB(TEXT));
			return reinterpret_cast<LRESULT>( s_wcd.hbrInputBackground );
		}
		break;
	case WM_DRAWITEM:
		SysCon_DrawButton(reinterpret_cast<const DRAWITEMSTRUCT*>( lParam ));
		return TRUE;
	case WM_ERASEBKGND:
		{
			RECT client;
			GetClientRect(hWnd, &client);
			FillRect((HDC)wParam, &client, s_wcd.hbrWindowBackground);
			SysCon_DrawPanelFrames((HDC)wParam);
		}
		return 1;
	case WM_SIZE:
		SysCon_LayoutChildren();
		break;
	case WM_GETMINMAXINFO:
		{
			RECT minRect;
			minRect.left = 0;
			minRect.top = 0;
			minRect.right = SysCon_Scale(SYSCON_METRIC_MIN_W);
			minRect.bottom = SysCon_Scale(SYSCON_METRIC_MIN_H);
			AdjustWindowRect(&minRect, static_cast<DWORD>( GetWindowLongPtr(hWnd, GWL_STYLE) ), FALSE);

			MINMAXINFO* minMax = reinterpret_cast<MINMAXINFO*>( lParam );
			minMax->ptMinTrackSize.x = minRect.right - minRect.left;
			minMax->ptMinTrackSize.y = minRect.bottom - minRect.top;
		}
		return 0;
	case WM_DPICHANGED:
		{
			// Windows hands us the frame it wants on the new monitor; take it,
			// then rebuild the fonts and re-run the layout at the new scale.
			s_wcd.dpi = HIWORD(wParam) >= 48 ? HIWORD(wParam) : SysCon_QueryDpi(hWnd);
			SysCon_CreateFonts();
			SysCon_ApplyFonts();

			const RECT* suggested = reinterpret_cast<const RECT*>( lParam );
			if (suggested != NULL) {
				SetWindowPos(hWnd, NULL,
					suggested->left, suggested->top,
					suggested->right - suggested->left,
					suggested->bottom - suggested->top,
					SWP_NOZORDER | SWP_NOACTIVATE);
			}
			SysCon_LayoutChildren();
		}
		return 0;
	case WM_SYSCOMMAND:
		if (wParam == SC_CLOSE) {
			PostQuitMessage(0);
		}
		break;
	case WM_COMMAND:
		// Repaint the command line's focus ring; the ring lives in the parent's
		// background, so the edit control cannot redraw it itself.
		if (LOWORD(wParam) == INPUT_ID &&
			( HIWORD(wParam) == EN_SETFOCUS || HIWORD(wParam) == EN_KILLFOCUS )) {
			SysCon_InvalidateInputFrame();
			break;
		}
		if (wParam == COPY_ID) {
			SendMessage(s_wcd.hwndBuffer, EM_SETSEL, 0, -1);
			SendMessage(s_wcd.hwndBuffer, WM_COPY, 0, 0);
		}
		else if (wParam == QUIT_ID) {
			if (s_wcd.quitOnClose) {
				PostQuitMessage(0);
			}
			else {
				cmdString = Mem_CopyString("quit");
				Sys_QueEvent(0, SE_CONSOLE, 0, 0,
					idLib::SizeToInt( strlen( cmdString ) + 1, "ConWndProc quit button" ), cmdString);
			}
		}
		else if (wParam == CLEAR_ID) {
			SendMessage(s_wcd.hwndBuffer, EM_SETSEL, 0, -1);
			SendMessage(s_wcd.hwndBuffer, EM_REPLACESEL, FALSE, (LPARAM)"");
			UpdateWindow(s_wcd.hwndBuffer);
		}
		break;
	case WM_CREATE:
		s_wcd.hbrWindowBackground = CreateSolidBrush(SYSCON_WIN_RGB(WINDOW));
		s_wcd.hbrEditBackground = CreateSolidBrush(SYSCON_WIN_RGB(PANEL));
		s_wcd.hbrErrorBackground = CreateSolidBrush(SYSCON_WIN_RGB(PANEL));
		s_wcd.hbrAlertBackground = CreateSolidBrush(SYSCON_WIN_RGB(ALERT_PANEL));
		s_wcd.hbrInputBackground = CreateSolidBrush(SYSCON_WIN_RGB(INPUT));
		break;
		/*
				case WM_ERASEBKGND:
					HGDIOBJ oldObject;
					HDC hdcScaled;
					hdcScaled = CreateCompatibleDC( ( HDC ) wParam );
					assert( hdcScaled != 0 );
					if ( hdcScaled ) {
						oldObject = SelectObject( ( HDC ) hdcScaled, s_wcd.hbmLogo );
						assert( oldObject != 0 );
						if ( oldObject )
						{
							StretchBlt( ( HDC ) wParam, 0, 0, s_wcd.windowWidth, s_wcd.windowHeight,
								hdcScaled, 0, 0, 512, 384,
								SRCCOPY );
						}
						DeleteDC( hdcScaled );
						hdcScaled = 0;
					}
					return 1;
		*/
	}

	return DefWindowProc(hWnd, uMsg, wParam, lParam);
}

LRESULT CALLBACK InputLineWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	int key, cursor;
	switch (uMsg) {
	case WM_KILLFOCUS:
		if ((HWND)wParam == s_wcd.hWnd || (HWND)wParam == s_wcd.hwndErrorBox) {
			SetFocus(hWnd);
			return 0;
		}
		break;

	case WM_KEYDOWN:
		key = MapKey(lParam);

		// command history
		if ((key == K_UPARROW) || (key == K_KP_UPARROW)) {
			if (s_wcd.nextHistoryLine - s_wcd.historyLine < COMMAND_HISTORY && s_wcd.historyLine > 0) {
				s_wcd.historyLine--;
			}
			s_wcd.consoleField = s_wcd.historyEditLines[s_wcd.historyLine % COMMAND_HISTORY];

			SetWindowText(s_wcd.hwndInputLine, s_wcd.consoleField.GetBuffer());
			SendMessage(s_wcd.hwndInputLine, EM_SETSEL, s_wcd.consoleField.GetCursor(), s_wcd.consoleField.GetCursor());
			return 0;
		}

		if ((key == K_DOWNARROW) || (key == K_KP_DOWNARROW)) {
			if (s_wcd.historyLine == s_wcd.nextHistoryLine) {
				return 0;
			}
			s_wcd.historyLine++;
			s_wcd.consoleField = s_wcd.historyEditLines[s_wcd.historyLine % COMMAND_HISTORY];

			SetWindowText(s_wcd.hwndInputLine, s_wcd.consoleField.GetBuffer());
			SendMessage(s_wcd.hwndInputLine, EM_SETSEL, s_wcd.consoleField.GetCursor(), s_wcd.consoleField.GetCursor());
			return 0;
		}
		break;

	case WM_CHAR:
		key = MapKey(lParam);

		GetWindowText(s_wcd.hwndInputLine, s_wcd.consoleField.GetBuffer(), MAX_EDIT_LINE);
		SendMessage(s_wcd.hwndInputLine, EM_GETSEL, (WPARAM)NULL, (LPARAM)&cursor);
		s_wcd.consoleField.SetCursor(cursor);

		// enter the line
		if (key == K_ENTER || key == K_KP_ENTER) {
			const char *inputText = s_wcd.consoleField.GetBuffer();
			const bool privateCommand = cvarSystem != NULL && cvarSystem->IsInitialized() &&
				cvarSystem->CommandContainsPrivateCVar( inputText );
			size_t used = strlen( s_wcd.consoleText );
			if ( used < sizeof( s_wcd.consoleText ) - 1 ) {
				const size_t available = sizeof( s_wcd.consoleText ) - used - 1;
				const size_t copyLength = Min( strlen( inputText ), available );
				memcpy( s_wcd.consoleText + used, inputText, copyLength );
				used += copyLength;
				s_wcd.consoleText[used] = '\0';
			}
			if ( used < sizeof( s_wcd.consoleText ) - 1 ) {
				s_wcd.consoleText[used++] = '\n';
				s_wcd.consoleText[used] = '\0';
			}
			SetWindowText(s_wcd.hwndInputLine, "");

			if ( privateCommand ) {
				Sys_Printf("]<private command redacted>\n");
			} else {
				Sys_Printf("]%s\n", s_wcd.consoleField.GetBuffer());
			}

			// copy line to history buffer
			if ( !privateCommand ) {
				s_wcd.historyEditLines[s_wcd.nextHistoryLine % COMMAND_HISTORY] = s_wcd.consoleField;
				s_wcd.nextHistoryLine++;
				s_wcd.historyLine = s_wcd.nextHistoryLine;
			}

			s_wcd.consoleField.Clear();

			return 0;
		}

		// command completion
		if (key == K_TAB) {
			s_wcd.consoleField.AutoComplete();

			SetWindowText(s_wcd.hwndInputLine, s_wcd.consoleField.GetBuffer());
			//s_wcd.consoleField.SetWidthInChars( strlen( s_wcd.consoleField.GetBuffer() ) );
			SendMessage(s_wcd.hwndInputLine, EM_SETSEL, s_wcd.consoleField.GetCursor(), s_wcd.consoleField.GetCursor());

			return 0;
		}

		// clear autocompletion buffer on normal key input
		if ((key >= K_SPACE && key <= K_BACKSPACE) ||
			(key >= K_KP_SLASH && key <= K_KP_PLUS) || (key >= K_KP_STAR && key <= K_KP_EQUALS)) {
			s_wcd.consoleField.ClearAutoComplete();
		}
		break;
	}

	return CallWindowProc(s_wcd.SysInputLineWndProc, hWnd, uMsg, wParam, lParam);
}

/*
** Sys_CreateConsole
*/
void Sys_CreateConsole(void) {
	WNDCLASS wc;
	RECT rect;
	const char* DEDCLASS = WIN32_CONSOLE_CLASS;
	int swidth, sheight;
	// Resizable: a log window that cannot be widened is a log window you read
	// through a keyhole. The SDL console on the other platforms resizes too.
	int DEDSTYLE = WS_OVERLAPPEDWINDOW;
	int i;

	s_wcd.dpi = SYSCON_REFERENCE_DPI;
	s_wcd.hotButton = 0;

	memset(&wc, 0, sizeof(wc));

	wc.style = 0;
	wc.lpfnWndProc = ConWndProc;
	wc.cbClsExtra = 0;
	wc.cbWndExtra = 0;
	wc.hInstance = win32.hInstance;
	wc.hIcon = LoadIcon(win32.hInstance, MAKEINTRESOURCE(IDI_ICON1));
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	// WM_ERASEBKGND paints the themed background; a class brush of COLOR_WINDOW
	// would flash white behind it on every resize.
	wc.hbrBackground = NULL;
	wc.lpszMenuName = 0;
	wc.lpszClassName = DEDCLASS;

	if (!RegisterClass(&wc)) {
		return;
	}

	rect.left = 0;
	rect.right = SYSCON_METRIC_WINDOW_W;
	rect.top = 0;
	rect.bottom = SYSCON_METRIC_WINDOW_H;
	AdjustWindowRect(&rect, DEDSTYLE, FALSE);

	swidth = GetSystemMetrics(SM_CXSCREEN);
	sheight = GetSystemMetrics(SM_CYSCREEN);

	s_wcd.windowWidth = rect.right - rect.left;
	s_wcd.windowHeight = rect.bottom - rect.top;

	if (!s_wcd.hbmLogo) {
		s_wcd.hbmLogo = LoadBitmap(win32.hInstance, MAKEINTRESOURCE(IDB_BITMAP_LOGO));
	}

	s_wcd.hWnd = CreateWindowEx(0,
		DEDCLASS,
		GAME_NAME " Console",
		DEDSTYLE,
		(swidth - s_wcd.windowWidth) / 2, (sheight - s_wcd.windowHeight) / 2,
		s_wcd.windowWidth, s_wcd.windowHeight,
		NULL,
		NULL,
		win32.hInstance,
		NULL);

	if (s_wcd.hWnd == NULL) {
		return;
	}

	SysCon_ApplyDarkWindowFrame(s_wcd.hWnd);

	//
	// The window was created at the design size; now that it exists we can ask
	// which monitor it landed on and rebuild it at that monitor's scale.
	//
	s_wcd.dpi = SysCon_QueryDpi(s_wcd.hWnd);
	if (s_wcd.dpi != SYSCON_REFERENCE_DPI) {
		rect.left = 0;
		rect.top = 0;
		rect.right = SysCon_Scale(SYSCON_METRIC_WINDOW_W);
		rect.bottom = SysCon_Scale(SYSCON_METRIC_WINDOW_H);
		AdjustWindowRect(&rect, DEDSTYLE, FALSE);
		s_wcd.windowWidth = rect.right - rect.left;
		s_wcd.windowHeight = rect.bottom - rect.top;
		SetWindowPos(s_wcd.hWnd, NULL,
			(swidth - s_wcd.windowWidth) / 2, (sheight - s_wcd.windowHeight) / 2,
			s_wcd.windowWidth, s_wcd.windowHeight,
			SWP_NOZORDER | SWP_NOACTIVATE);
	}

	SysCon_CreateFonts();

	//
	// create the status line
	//
	idStr::Copynz(s_wcd.errorString, SYSCON_STATUS_READY_TEXT, sizeof(s_wcd.errorString));
	s_wcd.errorIsFatal = false;
	s_wcd.hwndErrorBox = CreateWindow("static", NULL, WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE | SS_ENDELLIPSIS,
		0, 0, 0, 0,
		s_wcd.hWnd,
		(HMENU)ERRORBOX_ID,
		win32.hInstance, NULL);
	SetWindowText(s_wcd.hwndErrorBox, s_wcd.errorString);

	//
	// create the input line
	//
	s_wcd.hwndInputLine = CreateWindow("edit", NULL, WS_CHILD | WS_VISIBLE |
		ES_LEFT | ES_AUTOHSCROLL,
		0, 0, 0, 0,
		s_wcd.hWnd,
		(HMENU)INPUT_ID,	// child window ID
		win32.hInstance, NULL);
	SendMessage(s_wcd.hwndInputLine, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
		MAKELPARAM(SysCon_Scale(SYSCON_METRIC_TEXT_PAD), SysCon_Scale(SYSCON_METRIC_TEXT_PAD)));

	//
	// create the buttons
	//
	// BS_OWNERDRAW so the faces come from the shared palette instead of the
	// system's light push-button face, which would fight the dark log.
	s_wcd.hwndButtonCopy = CreateWindow("button", NULL, BS_OWNERDRAW | WS_VISIBLE | WS_CHILD | WS_TABSTOP,
		0, 0, 0, 0,
		s_wcd.hWnd,
		(HMENU)COPY_ID,	// child window ID
		win32.hInstance, NULL);
	SendMessage(s_wcd.hwndButtonCopy, WM_SETTEXT, 0, (LPARAM)SYSCON_LABEL_COPY);

	s_wcd.hwndButtonClear = CreateWindow("button", NULL, BS_OWNERDRAW | WS_VISIBLE | WS_CHILD | WS_TABSTOP,
		0, 0, 0, 0,
		s_wcd.hWnd,
		(HMENU)CLEAR_ID,	// child window ID
		win32.hInstance, NULL);
	SendMessage(s_wcd.hwndButtonClear, WM_SETTEXT, 0, (LPARAM)SYSCON_LABEL_CLEAR);

	s_wcd.hwndButtonQuit = CreateWindow("button", NULL, BS_OWNERDRAW | WS_VISIBLE | WS_CHILD | WS_TABSTOP,
		0, 0, 0, 0,
		s_wcd.hWnd,
		(HMENU)QUIT_ID,	// child window ID
		win32.hInstance, NULL);
	SendMessage(s_wcd.hwndButtonQuit, WM_SETTEXT, 0, (LPARAM)SYSCON_LABEL_QUIT);

	//
	// create the scrollbuffer
	//
	s_wcd.hwndBuffer = CreateWindow("edit", NULL, WS_CHILD | WS_VISIBLE | WS_VSCROLL |
		ES_LEFT | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
		0, 0, 0, 0,
		s_wcd.hWnd,
		(HMENU)EDIT_ID,	// child window ID
		win32.hInstance, NULL);
	SendMessage(s_wcd.hwndBuffer, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
		MAKELPARAM(SysCon_Scale(SYSCON_METRIC_TEXT_PAD), SysCon_Scale(SYSCON_METRIC_TEXT_PAD)));
	// The log's own scrollbar is the one piece of native chrome inside the dark
	// panel, so opt it into the dark theme instead of leaving a light stripe.
	SysCon_ApplyDarkScrollbars(s_wcd.hwndBuffer);

	s_wcd.SysInputLineWndProc = reinterpret_cast<WNDPROC>( SetWindowLongPtr(
		s_wcd.hwndInputLine,
		GWLP_WNDPROC,
		reinterpret_cast<LONG_PTR>( InputLineWndProc ) ) );

	// All three buttons share one subclass; they are the same window class, so
	// the original procedure they return is the same pointer for each.
	s_wcd.SysButtonWndProc = reinterpret_cast<WNDPROC>( SetWindowLongPtr(
		s_wcd.hwndButtonCopy,
		GWLP_WNDPROC,
		reinterpret_cast<LONG_PTR>( SysConButtonWndProc ) ) );
	SetWindowLongPtr(s_wcd.hwndButtonClear, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>( SysConButtonWndProc ));
	SetWindowLongPtr(s_wcd.hwndButtonQuit, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>( SysConButtonWndProc ));

	SysCon_ApplyFonts();
	SysCon_LayoutChildren();


	// don't show it now that we have a splash screen up
	if (win32.win_viewlog.GetBool()) {
		Sys_DestroySplash();
		ShowWindow(s_wcd.hWnd, SW_SHOWDEFAULT);
		UpdateWindow(s_wcd.hWnd);
		SetForegroundWindow(s_wcd.hWnd);
		SetFocus(s_wcd.hwndInputLine);
	}



	s_wcd.consoleField.Clear();

	for (i = 0; i < COMMAND_HISTORY; i++) {
		s_wcd.historyEditLines[i].Clear();
	}
}

/*
** Sys_DestroyConsole
*/
void Sys_DestroyConsole(void) {
	Sys_DestroySplash();

	if (s_wcd.hWnd) {
		ShowWindow(s_wcd.hWnd, SW_HIDE);
		CloseWindow(s_wcd.hWnd);
		DestroyWindow(s_wcd.hWnd);
		s_wcd.hWnd = 0;
		s_wcd.hwndBuffer = NULL;
		s_wcd.hwndInputLine = NULL;
		s_wcd.hwndErrorBox = NULL;
		s_wcd.hwndButtonCopy = NULL;
		s_wcd.hwndButtonClear = NULL;
		s_wcd.hwndButtonQuit = NULL;
	}

	if (s_wcd.hbmLogo) {
		DeleteObject(s_wcd.hbmLogo);
		s_wcd.hbmLogo = NULL;
	}

	// The window is gone, so nothing can still be painting with these.
	SysCon_DestroyFonts();

	HBRUSH* brushes[] = {
		&s_wcd.hbrWindowBackground,
		&s_wcd.hbrEditBackground,
		&s_wcd.hbrErrorBackground,
		&s_wcd.hbrAlertBackground,
		&s_wcd.hbrInputBackground,
	};
	for (int i = 0; i < static_cast<int>( sizeof( brushes ) / sizeof( brushes[0] ) ); i++) {
		if (*brushes[i]) {
			DeleteObject(*brushes[i]);
			*brushes[i] = NULL;
		}
	}
}

/*
** Sys_ShowConsole
*/
void Sys_ShowConsole(int visLevel, bool quitOnClose) {
	Sys_DestroySplash();

	s_wcd.quitOnClose = quitOnClose;

	if (!s_wcd.hWnd) {
		return;
	}

	switch (visLevel) {
	case 0:
		ShowWindow(s_wcd.hWnd, SW_HIDE);
		break;
	case 1:
		ShowWindow(s_wcd.hWnd, SW_SHOWNORMAL);
		SendMessage(s_wcd.hwndBuffer, EM_LINESCROLL, 0, 0xffff);
		break;
	case 2:
		ShowWindow(s_wcd.hWnd, SW_MINIMIZE);
		break;
	default:
		Sys_Error("Invalid visLevel %d sent to Sys_ShowConsole\n", visLevel);
		break;
	}
}

/*
** Sys_ConsoleInput
*/
char* Sys_ConsoleInput(void) {

	if (s_wcd.consoleText[0] == 0) {
		return NULL;
	}

	idStr::Copynz( s_wcd.returnedText, s_wcd.consoleText, sizeof( s_wcd.returnedText ) );
	memset( s_wcd.consoleText, 0, sizeof( s_wcd.consoleText ) );

	return s_wcd.returnedText;
}

/*
** Conbuf_AppendText
*/
void Conbuf_AppendText(const char* pMsg)
{
#define CONSOLE_BUFFER_SIZE		16384

	char buffer[CONSOLE_BUFFER_SIZE * 2];
	char* b = buffer;
	const char* msg;
	int bufLen;
	int i = 0;
	static unsigned long s_totalChars;

	//
	// if the message is REALLY long, use just the last portion of it
	//
	if (strlen(pMsg) > CONSOLE_BUFFER_SIZE - 1) {
		msg = pMsg + strlen(pMsg) - CONSOLE_BUFFER_SIZE + 1;
	}
	else {
		msg = pMsg;
	}

	//
	// copy into an intermediate buffer
	//
	const char *bufferEnd = buffer + sizeof( buffer ) - 1;
	while (msg[i] && b < bufferEnd) {
		if (msg[i] == '\n' && msg[i + 1] == '\r') {
			if ( b + 2 > bufferEnd ) {
				break;
			}
			b[0] = '\r';
			b[1] = '\n';
			b += 2;
			i++;
		}
		else if (msg[i] == '\r') {
			if ( b + 2 > bufferEnd ) {
				break;
			}
			b[0] = '\r';
			b[1] = '\n';
			b += 2;
		}
		else if (msg[i] == '\n') {
			if ( b + 2 > bufferEnd ) {
				break;
			}
			b[0] = '\r';
			b[1] = '\n';
			b += 2;
		}
		else {
			int escapeType = 0;
			const int escapeLength = idStr::IsEscape( &msg[i], &escapeType );
			if ( escapeLength > 0 ) {
				i += escapeLength - 1;
			} else {
				*b = msg[i];
				b++;
			}
		}
		i++;
	}
	*b = 0;
	bufLen = b - buffer;

	s_totalChars += bufLen;

	//
	// replace selection instead of appending if we're overflowing
	//
	if (s_totalChars > 0x7000) {
		SendMessage(s_wcd.hwndBuffer, EM_SETSEL, 0, -1);
		s_totalChars = bufLen;
	}

	//
	// put this text into the windows console
	//
	SendMessage(s_wcd.hwndBuffer, EM_LINESCROLL, 0, 0xffff);
	SendMessage(s_wcd.hwndBuffer, EM_SCROLLCARET, 0, 0);
	SendMessage(s_wcd.hwndBuffer, EM_REPLACESEL, 0, (LPARAM)buffer);
}

/*
** Conbuf_WriteText
**
** Writes the console log window's text to an open file, for the fatal error
** record. It runs after engine shutdown, so it allocates from the process
** heap rather than the engine's allocator.
*/
bool Conbuf_WriteText(HANDLE file) {
	if (file == INVALID_HANDLE_VALUE || s_wcd.hwndBuffer == NULL) {
		return false;
	}
	const int length = GetWindowTextLengthA(s_wcd.hwndBuffer);
	if (length <= 0) {
		return false;
	}
	char* text = static_cast<char*>(HeapAlloc(GetProcessHeap(), 0, static_cast<SIZE_T>(length) + 1));
	if (text == NULL) {
		return false;
	}
	const int copied = GetWindowTextA(s_wcd.hwndBuffer, text, length + 1);
	DWORD written = 0;
	const bool ok = copied > 0 && WriteFile(file, text, static_cast<DWORD>(copied), &written, NULL) && written == static_cast<DWORD>(copied);
	HeapFree(GetProcessHeap(), 0, text);
	return ok;
}

/*
** Win_SetErrorText
*/
void Win_SetErrorText(const char* buf) {
	idStr::Copynz(s_wcd.errorString, buf, sizeof(s_wcd.errorString));
	// Repaints the strip in the alert palette, matching the fatal status strip
	// the SDL console draws on Linux and macOS.
	s_wcd.errorIsFatal = ( s_wcd.errorString[0] != '\0' );
	if (!s_wcd.hwndErrorBox) {
		s_wcd.hwndErrorBox = CreateWindow("static", NULL, WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE | SS_ENDELLIPSIS,
			0, 0, 0, 0,
			s_wcd.hWnd,
			(HMENU)ERRORBOX_ID,	// child window ID
			win32.hInstance, NULL);
		SendMessage(s_wcd.hwndErrorBox, WM_SETFONT, (WPARAM)s_wcd.hfButtonFont, MAKELPARAM(TRUE, 0));
		SysCon_LayoutChildren();
	}
	SetWindowText(s_wcd.hwndErrorBox, s_wcd.errorString);
	InvalidateRect(s_wcd.hwndErrorBox, NULL, TRUE);
}

void Sys_SetDedicatedConsoleTitle(const char* txt) {
	SetWindowText(s_wcd.hWnd, txt);
}
