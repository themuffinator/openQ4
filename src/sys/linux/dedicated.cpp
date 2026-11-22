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
#include "../../idlib/precompiled.h"
#include "../../renderer/tr_local.h"
#include "../posix/posix_public.h"

#if !defined( __linux__ ) || !defined( ID_DEDICATED )
	#error "linux/dedicated.cpp is only for Linux dedicated-server builds"
#endif

/*
==========
input
==========
*/

void Sys_InitInput( void ) { }

void Sys_ShutdownInput( void ) { }
void Sys_ClearInputEvents( void ) { }

void Sys_GrabMouseCursor( bool ) { }

int Sys_PollMouseInputEvents( void ) { return 0; }

void Sys_EndMouseInputEvents( void ) { }

int Sys_ReturnMouseInputEvent( const int n, int &action, int &value ) { action = 0; value = 0; return 0; }

int Sys_PollKeyboardInputEvents( void ) { return 0; }

void Sys_EndKeyboardInputEvents( void ) { }

int Sys_ReturnKeyboardInputEvent( const int n, int &action, bool &state ) { action = 0; state = false; return 0; }

int Sys_PollJoystickInputEvents( void ) { return 0; }

int Sys_ReturnJoystickInputEvent( const int n, int &axis, int &value ) { axis = 0; value = 0; return 0; }

void Sys_EndJoystickInputEvents( void ) { }

bool Sys_GetJoystickAxisState( int axis, int &value ) { value = 0; return false; }

bool Sys_SetJoystickRumble( float lowFrequency, float highFrequency, int durationMsec ) { return false; }

bool Sys_GetGyroLook( float &yaw, float &pitch, bool consume ) { yaw = 0.0f; pitch = 0.0f; return false; }

bool Sys_SetJoystickTriggerRumble( float left, float right, int durationMsec ) { return false; }

void Sys_SetControllerPlayerState( int weaponFeel, int health, int team ) { }

bool Sys_GetControllerTestState( controllerTestState_t &state ) { memset( &state, 0, sizeof( state ) ); return false; }

void Sys_StartControllerRumbleTest( void ) { }

bool Sys_HasScreenKeyboardSupport( void ) { return false; }

unsigned char Sys_MapCharForKey( int key ) { return (unsigned char)key; }

/*
================
Sys_GetVideoRam
returns in megabytes
================
*/
int Sys_GetVideoRam( void ) {
	return 64;
}

bool Sys_GetDesktopResolution( int *width, int *height ) {
	if ( width != NULL ) {
		*width = 0;
	}
	if ( height != NULL ) {
		*height = 0;
	}
	return false;
}

