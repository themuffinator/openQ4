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

#ifndef __PRECOMPILED_H__
#define __PRECOMPILED_H__

#ifdef __cplusplus

typedef unsigned char		byte;		// 8 bits
typedef unsigned short		word;		// 16 bits
typedef unsigned int		dword;		// 32 bits
typedef unsigned int		uint;
// typedef unsigned long		ulong; // DG: long should be avoided.

typedef signed char			int8;
typedef unsigned char		uint8;
typedef short int			int16;
typedef unsigned short int	uint16;
typedef int					int32;
typedef unsigned int		uint32;
typedef long long			int64;
typedef unsigned long long	uint64;

class ThreadedAlloc;		// class that is only used to expand the AutoCrit template to tag allocs/frees called from inside the R_AddModelSurfaces call graph
#define PC_CVAR_ARCHIVE CVAR_ARCHIVE

#ifdef _WINDOWS

	// _WIN32 always defined
	// _WIN64 also defined for x64 target
	#if !defined( _WIN64 )
		#define ID_WIN_X86_ASM
		#define ID_WIN_X86_MMX
		#define ID_WIN_X86_SSE
		//#define ID_WIN_X86_SSE2
	#endif

	// we should never rely on this define in our code. this is here so dodgy external libraries don't get confused
	#ifndef WIN32
		#define WIN32
	#endif

	#undef _XBOX
	#undef _CONSOLE								// Used to comment out code that can't be used on a console
	#define _OPENGL
	#define _LITTLE_ENDIAN
	#undef _CASE_SENSITIVE_FILESYSTEM
	#define _USE_OPENAL
	#define _USE_VOICECHAT
	#define __WITH_PB__
	//#define _RV_MEM_SYS_SUPPORT
	// when using the PC to make Xenon builds, enable _MD5R_SUPPORT / _MD5R_WRITE_SUPPORT and run with fs_game q4baseXenon
	#ifdef Q4SDK
		// the SDK can't be compiled with _MD5R_SUPPORT, but since the PC version is we need to maintain ABI
		// to make things worse, only the windows version was compiled with _MD5R enabled, the Linux and Mac builds didn't
		//#define Q4SDK_MD5R
	#else	// Q4SDK
		//#define Q4SDK_MD5R
		//#define _MD5R_SUPPORT
		//#define _MD5R_WRITE_SUPPORT
	#endif	// !Q4SDK
	#define _GLVAS_SUPPPORT
	//#define RV_BINARYDECLS
	//#define RV_SINGLE_DECL_FILE
	// this can't be used with _RV_MEM_SYS_SUPPORT and actually shouldn't be used at all on the Xenon at present
	#if !defined(_RV_MEM_SYS_SUPPORT) && !defined(ID_REDIRECT_NEWDELETE)
		//#define RV_UNIFIED_ALLOCATOR
	#endif

	// SMP support for running the backend on a 2nd thread
	#define ENABLE_INTEL_SMP
	// Enables the batching of vertex cache request in SMP mode.
	// Note (TTimo): is tied to ENABLE_INTEL_SMP
	#define ENABLE_INTEL_VERTEXCACHE_OPT
	
	// Empty define for Xbox 360 compatibility
	#define RESTRICT
	#define TIME_THIS_SCOPE(x)

	#define NEWLINE				"\r\n"

	#pragma warning( disable : 4100 )			// unreferenced formal parameter
	#pragma warning( disable : 4127 )			// conditional expression is constant
	#pragma warning( disable : 4201 )			// non standard extension nameless struct or union
	#pragma warning( disable : 4244 )			// conversion to smaller type, possible loss of data
	#pragma warning( disable : 4245 )			// signed/unsigned mismatch
	#pragma warning( disable : 4389 )			// signed/unsigned mismatch
	#pragma warning( disable : 4714 )			// function marked as __forceinline not inlined
	#pragma warning( disable : 4800 )			// forcing value to bool 'true' or 'false' (performance warning)
// jmarshall
	#pragma warning( disable : 4458 )			//  hides class member
// jmarshall end

	class AlignmentChecker
	{
	public:
		static void UpdateCount(void const * const ptr) {}
		static void ClearCount() {}
		static void Print() {}
	};

#endif // _WINDOWS

#ifdef __linux__

// for offsetof
#include <stddef.h>
// FLT_MAX and such
#include <limits.h>
#include <float.h>

	#define __WITH_PB__
	#undef WIN32
	#undef _XBOX
	#undef _CONSOLE
	#define _OPENGL
	#define _LITTLE_ENDIAN
	#define _CASE_SENSITIVE_FILESYSTEM
	#define _USE_OPENAL

	#define NEWLINE				"\n"

	#define _GLVAS_SUPPPORT

	class AlignmentChecker
	{
	public:
		static void UpdateCount(void const * const ptr) {}
		static void ClearCount() {}
		static void Print() {}
	};

	#define RESTRICT
	#define TIME_THIS_SCOPE(x)

	// Enables the batching of vertex cache request in SMP mode.
	// Note (TTimo): is tied to ENABLE_INTEL_SMP
	#ifdef ENABLE_INTEL_SMP
	#define ENABLE_INTEL_VERTEXCACHE_OPT
	#endif

#endif

#ifdef MACOS_X

// for offsetof
#include <stddef.h>
#include <limits.h>
#include <float.h>				// for FLT_MIN

	// SMP support for running the backend on a 2nd thread
	#ifndef ENABLE_INTEL_SMP
	#define ENABLE_INTEL_SMP
	#endif
	// Enables the batching of vertex cache request in SMP mode.
	// Note (TTimo): is tied to ENABLE_INTEL_SMP
	#define ENABLE_INTEL_VERTEXCACHE_OPT

	#define __WITH_PB__
	#undef WIN32
	#undef _XBOX
	#undef _CONSOLE
	#define _OPENGL
	#ifdef __ppc__
	#undef _LITTLE_ENDIAN
	#else
	#define _LITTLE_ENDIAN
	#endif
	#define _CASE_SENSITIVE_FILESYSTEM
	#define _USE_OPENAL
	#define ID_INLINE inline
	#define NEWLINE				"\n"

	#define _GLVAS_SUPPPORT

	class AlignmentChecker
	{
	public:
		static void UpdateCount(void const * const ptr) {}
		static void ClearCount() {}
		static void Print() {}
	};

	#define RESTRICT
	#define TIME_THIS_SCOPE(x)

#endif

#ifndef RESTRICT
	#define RESTRICT
#endif

#ifndef TIME_THIS_SCOPE
	#define TIME_THIS_SCOPE(x)
#endif

#ifndef BIT
#define BIT( num )				BITT< num >::VALUE
#endif

template< unsigned int B >
class BITT {
public:
	typedef enum bitValue_e {
		VALUE = 1 << B,
	} bitValue_t;
};

#include <stdint.h>

//-----------------------------------------------------

#define ID_TIME_T time_t

#ifdef _WIN32

#define _ATL_CSTRING_EXPLICIT_CONSTRUCTORS	// prevent auto literal to string conversion

#ifndef Q4SDK
#ifndef GAME_DLL

#define WINVER				0x501

#if 0
// Dedicated server hits unresolved when trying to link this way now. Likely because of the 2010/Win7 transition? - TTimo

#ifdef	ID_DEDICATED
// dedicated sets windows version here
#define	_WIN32_WINNT WINVER
#define	WIN32_LEAN_AND_MEAN
#else
// non-dedicated includes MFC and sets windows version here
#include "../tools/comafx/StdAfx.h"			// this will go away when MFC goes away
#endif

#else

//#include "../tools/comafx/StdAfx.h"

#endif

#include <winsock2.h>
#include <mmsystem.h>
#include <mmreg.h>

#define DIRECTINPUT_VERSION  0x0800			// was 0x0700 with the old mssdk
#define DIRECTSOUND_VERSION  0x0800

#include <dsound.h>
#include <dinput.h>

#endif /* !GAME_DLL */
#endif /* !Q4SDK */

#pragma warning(disable : 4100)				// unreferenced formal parameter
#pragma warning(disable : 4244)				// conversion to smaller type, possible loss of data
#pragma warning(disable : 4714)				// function marked as __forceinline not inlined
#pragma warning(disable : 4996)				// unsafe string operations

#include <malloc.h>							// no malloc.h on mac or unix
#include <windows.h>						// for qgl.h
#undef FindText								// stupid namespace poluting Microsoft monkeys

#endif /* _WIN32 */

//-----------------------------------------------------

#if !defined( _DEBUG ) && !defined( NDEBUG )
	// don't generate asserts
#define NDEBUG
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>
#include <time.h>
#include <ctype.h>
#include <typeinfo>
#include <errno.h>
#include <math.h>

//-----------------------------------------------------

// non-portable system services
#include "../sys/sys_public.h"

// id lib
#include "../idlib/Lib.h"

// framework
#include "../framework/BuildVersion.h"
#include "../framework/BuildDefines.h"
#include "../framework/licensee.h"
#include "../framework/CmdSystem.h"
#include "../framework/CVarSystem.h"
#include "../framework/Common.h"
#include "../framework/File.h"
#include "../framework/FileSystem.h"
#include "../framework/UsercmdGen.h"

// decls
#include "../framework/DeclManager.h"
#include "../framework/DeclTable.h"
#include "../framework/DeclSkin.h"
#include "../framework/DeclEntityDef.h"
#include "../framework/DeclAF.h"
// RAVEN BEGIN
// jscott: new decl types
#include "../framework/DeclPlayerModel.h"
#include "../framework/declMatType.h"
#include "../framework/declLipSync.h"
#include "../framework/declPlayback.h"
// RAVEN END

// We have expression parsing and evaluation code in multiple places:
// materials, sound shaders, and guis. We should unify them.
const int MAX_EXPRESSION_OPS = 4096;
const int MAX_EXPRESSION_REGISTERS = 4096;

// Sanity check for any axis in bounds
const float MAX_BOUND_SIZE = 65536.0f;

// renderer
#include "../renderer/qgl.h"
#include "../renderer/Cinematic.h"
#include "../renderer/Material.h"
#include "../renderer/Model.h"
#include "../renderer/ModelManager.h"
#include "../renderer/RenderSystem.h"
#include "../renderer/RenderWorld.h"

// sound engine
#include "../sound/sound.h"

// RAVEN BEGIN
// jscott: Effects system interface
#include "../bse_api/BSEInterface.h"
// RAVEN END

// asynchronous networking
#include "../framework/async/NetworkSystem.h"

// user interfaces
#include "../ui/ListGUI.h"
#include "../ui/UserInterface.h"

// collision detection system
#include "../cm/CollisionModel.h"

// AAS files and manager
#include "../aas/AASFile.h"
#include "../aas/AASFileManager.h"

// game
#include "../game/Game.h"

//-----------------------------------------------------

#ifdef GAME_DLL
#include "../game/Game_local.h"
#endif

#ifndef Q4SDK

#include "../framework/DemoChecksum.h"

// framework
#include "../framework/Compressor.h"
#include "../framework/EventLoop.h"
#include "../framework/KeyInput.h"
#include "../framework/EditField.h"
#include "../framework/Console.h"
#include "../framework/DemoFile.h"
#include "../framework/Session.h"

// asynchronous networking
#include "../framework/async/AsyncNetwork.h"

// The editor entry points are always declared, but may just be
// stubbed out on non-windows platforms.
#include "../tools/edit_public.h"

// Compilers for map, model, video etc. processing.
#include "../tools/compilers/compiler_public.h"

#endif /* !Q4SDK */

//-----------------------------------------------------

// RAVEN BEGIN
// jsinger: add AutoPtr and text-to-binary compiler support
#include "AutoPtr.h"
#include "LexerFactory.h"
#include "TextCompiler.h"
// jsinger: AutoCrit.h contains classes which aid in code synchronization
//          AutoAcquire.h contains a class that aids in thread acquisition of the direct3D device for xenon
//          Both compile out completely if the #define's above are not present
#include "threads/AutoCrit.h"
// RAVEN END

#endif	/* __cplusplus */

#endif /* !__PRECOMPILED_H__ */
