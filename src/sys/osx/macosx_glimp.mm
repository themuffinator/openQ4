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


===========================================================================
*/

// -*- mode: objc -*-
#import "../../idlib/precompiled.h"

#import "../../renderer/tr_local.h"

#import "macosx_glimp.h"

#import "macosx_local.h"
#import "macosx_sys.h"
#import "macosx_display.h"

#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>

#import <mach-o/dyld.h>
#import <mach/mach.h>
#import <mach/mach_error.h>

static idCVar r_minDisplayRefresh( "r_minDisplayRefresh", "0", CVAR_ARCHIVE | CVAR_INTEGER, "" );
static idCVar r_maxDisplayRefresh( "r_maxDisplayRefresh", "0", CVAR_ARCHIVE | CVAR_INTEGER, "" );
static idCVar r_screen( "r_screen", "-1", CVAR_ARCHIVE | CVAR_INTEGER, "which display to use" );

static void				GLW_InitExtensions( void );
static bool				CreateGameWindow( glimpParms_t parms );
static unsigned long	Sys_QueryVideoMemory();
CGDisplayErr		Sys_CaptureActiveDisplays(void);

#ifndef NSOpenGLPFAOpenGLProfile
#define NSOpenGLPFAOpenGLProfile 99
#endif
#ifndef NSOpenGLProfileVersion3_2Core
#define NSOpenGLProfileVersion3_2Core 0x3200
#endif
#ifndef NSOpenGLProfileVersion4_1Core
#define NSOpenGLProfileVersion4_1Core 0x4100
#endif

glwstate_t glw_state;
static bool isHidden = false;

/*
================
Sys_GetDesktopResolution
================
*/
bool Sys_GetDesktopResolution( int *width, int *height ) {
	if ( width == NULL || height == NULL ) {
		return false;
	}

	const CGDirectDisplayID display = Sys_DisplayToUse();
	const int desktopWidth = static_cast<int>( CGDisplayPixelsWide( display ) );
	const int desktopHeight = static_cast<int>( CGDisplayPixelsHigh( display ) );
	if ( desktopWidth <= 0 || desktopHeight <= 0 ) {
		return false;
	}

	*width = desktopWidth;
	*height = desktopHeight;
	return true;
}

@interface NSOpenGLContext (CGLContextAccess)
- (CGLContextObj) cglContext;
@end

@implementation NSOpenGLContext (CGLContextAccess)
- (CGLContextObj) cglContext;
{
}
@end

/*
============
CheckErrors
============
*/
void CheckErrors( void ) {		
	GLenum   err;

	err = glGetError();
	if ( err != GL_NO_ERROR ) {
		common->Error( "glGetError: %s\n", glGetString( err ) );
	}
}

#if !defined(NDEBUG) && defined(QGL_CHECK_GL_ERRORS)

unsigned int QGLBeginStarted = 0;

void QGLErrorBreak(void) { }

void QGLCheckError( const char *message ) {
	GLenum        error;
	static unsigned int errorCount = 0;
    
	error = _glGetError();
	if (error != GL_NO_ERROR) {
		if (errorCount == 100) {
			common->Printf("100 GL errors printed ... disabling further error reporting.\n");
		} else if (errorCount < 100) {
			if (errorCount == 0) {
				common->Warning("BREAK ON QGLErrorBreak to stop at the GL errors\n");
			}
			#if defined(GLEW_NO_GLU)
				common->Warning("OpenGL Error(%s): 0x%04x\n", message, (int)error);
			#else
				common->Warning("OpenGL Error(%s): 0x%04x -- %s\n", message, (int)error, gluErrorString(error));
			#endif
			QGLErrorBreak();
		}
		errorCount++;
	}
}
#endif

/*
** GLimp_SetMode
*/

bool GLimp_SetMode(  glimpParms_t parms ) {
	if ( !CreateGameWindow( parms ) ) {
		common->Printf( "GLimp_SetMode: window could not be created!\n" );
		return false;
	}

	glConfig.vidWidth = parms.width;
	glConfig.vidHeight = parms.height;
	glConfig.isFullscreen = parms.fullScreen;
    
	// draw something to show that GL is alive	
	glClearColor( 0.5, 0.5, 0.7, 0 );
	glClear( GL_COLOR_BUFFER_BIT );
	GLimp_SwapBuffers();
        
	glClearColor( 0.5, 0.5, 0.7, 0 );
	glClear( GL_COLOR_BUFFER_BIT );
	GLimp_SwapBuffers();

	Sys_UnfadeScreen( Sys_DisplayToUse(), NULL );
    
	CheckErrors();

	return true;
}

/*
=================
GetPixelAttributes
=================
*/

#define ADD_ATTR(x)	\
	do {								   \
		if (attributeIndex >= attributeSize) {	\
			attributeSize *= 2;	\
			pixelAttributes = (NSOpenGLPixelFormatAttribute *)NSZoneRealloc(NULL, pixelAttributes, sizeof(*pixelAttributes) * attributeSize); \
		} \
		pixelAttributes[attributeIndex] = (NSOpenGLPixelFormatAttribute)x;	\
		attributeIndex++;						\
		if ( verbose ) {												\
			common->Printf( "Adding pixel attribute: %d (%s)\n", x, #x); \
		}																\
	} while(0)

static NSOpenGLPixelFormatAttribute *GetPixelAttributes( unsigned int multisamples, const rendererContextCandidate_t &candidate ) {
	NSOpenGLPixelFormatAttribute *pixelAttributes;
	unsigned int attributeIndex = 0;
	unsigned int attributeSize = 128;
	int verbose;
	unsigned int colorDepth;
	unsigned int desktopColorDepth;
	unsigned int depthBits;
	unsigned int stencilBits;
	unsigned int buffers;
    
	verbose = 0;
    
	pixelAttributes = (NSOpenGLPixelFormatAttribute *)NSZoneMalloc(NULL, sizeof(*pixelAttributes) * attributeSize);

	// only greater or equal attributes will be selected
	ADD_ATTR( NSOpenGLPFAMinimumPolicy );
	ADD_ATTR( 1 );

	if ( cvarSystem->GetCVarBool( "r_fullscreen" ) ) {
		ADD_ATTR(NSOpenGLPFAFullScreen);
	}

	ADD_ATTR(NSOpenGLPFAScreenMask);
	ADD_ATTR(CGDisplayIDToOpenGLDisplayMask(Sys_DisplayToUse()));
	
	// Require hardware acceleration
	ADD_ATTR(NSOpenGLPFAAccelerated);

	// Require double-buffer
	ADD_ATTR(NSOpenGLPFADoubleBuffer);

	if ( candidate.explicitVersion && candidate.profile == RENDERER_CONTEXT_PROFILE_CORE ) {
		ADD_ATTR( NSOpenGLPFAOpenGLProfile );
		ADD_ATTR( NSOpenGLProfileVersion4_1Core );
	}

	// color bits
	ADD_ATTR(NSOpenGLPFAColorSize);
	colorDepth = 32;
	if ( !cvarSystem->GetCVarBool( "r_fullscreen" ) ) {
		desktopColorDepth = [[glw_state.desktopMode objectForKey: (id)kCGDisplayBitsPerPixel] intValue];
		if ( desktopColorDepth != 32 ) {
		}
	}
	ADD_ATTR(colorDepth);

	// Specify the number of depth bits
	ADD_ATTR( NSOpenGLPFADepthSize );
	depthBits = 24;
	ADD_ATTR( depthBits );

	// Specify the number of stencil bits
	stencilBits = 8;
	ADD_ATTR( NSOpenGLPFAStencilSize );
	ADD_ATTR( stencilBits );

	// Specify destination alpha
	ADD_ATTR( NSOpenGLPFAAlphaSize );
	ADD_ATTR( 8 );

	if ( multisamples ) {
		buffers = 1;
		ADD_ATTR( NSOpenGLPFASampleBuffers );	
		ADD_ATTR( buffers );
		ADD_ATTR( NSOpenGLPFASamples );	
		ADD_ATTR( multisamples );
	}

	// Terminate the list
	ADD_ATTR(0);

	return pixelAttributes;
}

void Sys_UpdateWindowMouseInputRect(void) {		
	NSRect           windowRect, screenRect;
	NSScreen        *screen;

	/*

	// TTimo - I guess glw_state.window is bogus .. getting crappy data out of this

	// It appears we need to flip the coordinate system here.  This means we need
	// to know the size of the screen.
	screen = [glw_state.window screen];
	screenRect = [screen frame];
	windowRect = [glw_state.window frame];
	windowRect.origin.y = screenRect.size.height - (windowRect.origin.y + windowRect.size.height);

	Sys_SetMouseInputRect( CGRectMake( windowRect.origin.x, windowRect.origin.y, windowRect.size.width, windowRect.size.height ) );
	*/

	Sys_SetMouseInputRect( CGDisplayBounds( glw_state.display ) );
}							

// This is needed since CGReleaseAllDisplays() restores the gamma on the displays and we want to fade it up rather than just flickering all the displays
static void ReleaseAllDisplays() {
	CGDisplayCount displayIndex;

	common->Printf("Releasing displays\n");
	for (displayIndex = 0; displayIndex < glw_state.displayCount; displayIndex++) {
		CGDisplayRelease(glw_state.originalDisplayGammaTables[displayIndex].display);
	}
}

static bool OSX_ContextCandidateSupported( const rendererContextCandidate_t &candidate ) {
	if ( candidate.debugContext ) {
		common->Printf( "macOS: skipping OpenGL context %s because NSOpenGL does not expose debug-context creation\n", candidate.label );
		return false;
	}
	if ( candidate.explicitVersion && candidate.profile == RENDERER_CONTEXT_PROFILE_COMPATIBILITY ) {
		common->Printf( "macOS: skipping OpenGL context %s because versioned compatibility profiles are unavailable\n", candidate.label );
		return false;
	}
	if ( candidate.explicitVersion && candidate.profile == RENDERER_CONTEXT_PROFILE_CORE ) {
		if ( candidate.major > 4 || ( candidate.major == 4 && candidate.minor > 1 ) ) {
			common->Printf( "macOS: skipping OpenGL context %s because macOS OpenGL tops out at 4.1 core\n", candidate.label );
			return false;
		}
	}
	return true;
}

static NSOpenGLPixelFormat *OSX_CreatePixelFormatForCandidate( unsigned int &multisamples, const rendererContextCandidate_t &candidate ) {
	NSOpenGLPixelFormat *pixelFormat = nil;
	while ( !pixelFormat ) {
		NSOpenGLPixelFormatAttribute *pixelAttributes = GetPixelAttributes( multisamples, candidate );
		pixelFormat = [[[NSOpenGLPixelFormat alloc] initWithAttributes: pixelAttributes] autorelease];
		NSZoneFree(NULL, pixelAttributes);
		if ( pixelFormat || multisamples == 0 ) {
			break;
		}
		multisamples >>= 1;
	}
	return pixelFormat;
}

static bool OSX_CreateContextWithLadder( unsigned int &selectedMultisamples, NSOpenGLPixelFormat **selectedPixelFormat ) {
	rendererContextCandidate_t candidates[RENDERER_CONTEXT_LADDER_MAX_CANDIDATES];
	const rendererTierPreference_t preference = RendererTierPreference_FromString( r_glTier.GetString() );
	const bool keepAutoCompatibility = preference == RENDERER_TIER_PREF_AUTO;
	const int candidateCount = RendererContextLadder_Build(
		candidates,
		static_cast<int>( sizeof( candidates ) / sizeof( candidates[0] ) ),
		preference,
		r_glDebugContext.GetBool(),
		keepAutoCompatibility );
	if ( candidateCount <= 0 ) {
		common->Printf( "macOS: no OpenGL context candidates were generated for r_glTier %s\n", r_glTier.GetString() );
		return false;
	}

	for ( int i = 0; i < candidateCount; ++i ) {
		const rendererContextCandidate_t &candidate = candidates[i];
		if ( !OSX_ContextCandidateSupported( candidate ) ) {
			continue;
		}

		unsigned int candidateMultisamples = cvarSystem->GetCVarInteger( "r_multiSamples" );
		NSOpenGLPixelFormat *pixelFormat = OSX_CreatePixelFormatForCandidate( candidateMultisamples, candidate );
		if ( pixelFormat == nil ) {
			common->Printf( "macOS: OpenGL pixel format %s failed\n", candidate.label );
			continue;
		}

		common->Printf( "macOS: trying OpenGL context %s\n", candidate.label );
		NSOpenGLContext *context = [[NSOpenGLContext alloc] initWithFormat: pixelFormat shareContext: nil];
		if ( context != nil ) {
			OSX_SetGLContext( context );
			memset( &glConfig.contextRequest, 0, sizeof( glConfig.contextRequest ) );
			glConfig.contextRequest = candidate;
			selectedMultisamples = candidateMultisamples;
			*selectedPixelFormat = pixelFormat;
			common->Printf( "macOS: created OpenGL context %s\n", glConfig.contextRequest.label );
			return true;
		}

		common->Printf( "macOS: OpenGL context %s failed\n", candidate.label );
	}

	return false;
}

/*
=================
CreateGameWindow
=================
*/
static bool CreateGameWindow(  glimpParms_t parms ) {
	const char						*windowed[] = { "Windowed", "Fullscreen" };
	NSOpenGLPixelFormat				*pixelFormat;
	CGDisplayErr					err;
	unsigned int					multisamples;
	const long 						swap_limit = false;
	int 							nsOpenGLCPSwapLimit = 203;
            
	glw_state.display = Sys_DisplayToUse();
	glw_state.desktopMode = (NSDictionary *)CGDisplayCurrentMode( glw_state.display );
	if ( !glw_state.desktopMode ) {
		common->Error( "Could not get current graphics mode for display 0x%08x\n", glw_state.display );
	}

	common->Printf(  " %d %d %s\n", parms.width, parms.height, windowed[ parms.fullScreen ] );

	if (parms.fullScreen) {
        
		// We'll set up the screen resolution first in case that effects the list of pixel
		// formats that are available (for example, a smaller frame buffer might mean more
		// bits for depth/stencil buffers).  Allow stretched video modes if we are in fallback mode.
		glw_state.gameMode = Sys_GetMatchingDisplayMode(parms);
		if (!glw_state.gameMode) {
			common->Printf(  "Unable to find requested display mode.\n");
		}

		// Fade all screens to black
		//        Sys_FadeScreens();
        
		err = Sys_CaptureActiveDisplays();
		if ( err != CGDisplayNoErr ) {
			CGDisplayRestoreColorSyncSettings();
			common->Printf(  " Unable to capture displays err = %d\n", err );
		}

		err = CGDisplaySwitchToMode(glw_state.display, (CFDictionaryRef)glw_state.gameMode);
		if ( err != CGDisplayNoErr ) {
			CGDisplayRestoreColorSyncSettings();
			ReleaseAllDisplays();
			common->Printf(  " Unable to set display mode, err = %d\n", err );
		}
	} else {
		glw_state.gameMode = glw_state.desktopMode;
	}
    
	// Get the GL pixel format
	pixelFormat = nil;
	multisamples = 0;
	if ( !OSX_CreateContextWithLadder( multisamples, &pixelFormat ) ) {
		pixelFormat = nil;
	}
	cvarSystem->SetCVarInteger( "r_multiSamples", multisamples );			
    
	if (!pixelFormat) {
		CGDisplayRestoreColorSyncSettings();
		CGDisplaySwitchToMode(glw_state.display, (CFDictionaryRef)glw_state.desktopMode);
		ReleaseAllDisplays();
		common->Printf(  " No pixel format found\n");
		return false;
	}
#ifdef __ppc__
	long system_version = 0;
	Gestalt( gestaltSystemVersion, &system_version );
	if ( parms.width <= 1024 && parms.height <= 768 && system_version <= 0x1045 ) {
		[ OSX_GetNSGLContext() setValues: &swap_limit forParameter: (NSOpenGLContextParameter)nsOpenGLCPSwapLimit ];
	}
#endif
	
	if ( !parms.fullScreen ) {
		NSScreen*		 screen;
		NSRect           windowRect;
		int				 displayIndex;
		int				 displayCount;
		
		displayIndex = r_screen.GetInteger();
		displayCount = [[NSScreen screens] count];
		if ( displayIndex < 0 || displayIndex >= displayCount ) {
			screen = [NSScreen mainScreen];
		} else {
			screen = [[NSScreen screens] objectAtIndex:displayIndex];
		}

		NSRect r = [screen frame];
		windowRect.origin.x =  ((short)r.size.width - parms.width) / 2;
		windowRect.origin.y  = ((short)r.size.height - parms.height) / 2;
		windowRect.size.width = parms.width;
		windowRect.size.height = parms.height;
        
		glw_state.window = [NSWindow alloc];
		[glw_state.window initWithContentRect:windowRect styleMask:NSTitledWindowMask backing:NSBackingStoreRetained defer:NO screen:screen];
                                                           
		[glw_state.window setTitle: @GAME_NAME];

		[glw_state.window orderFront: nil];

		// the event system will filter them out.
		[glw_state.window setAcceptsMouseMovedEvents: YES];
        
		// Direct the context to draw in this window
		[OSX_GetNSGLContext() setView: [glw_state.window contentView]];

		// Sync input rect with where the window actually is...
		Sys_UpdateWindowMouseInputRect();
	} else {
		CGLError err;

		glw_state.window = NULL;
        
		err = CGLSetFullScreen(OSX_GetCGLContext());
		if (err) {
			CGDisplayRestoreColorSyncSettings();
			CGDisplaySwitchToMode(glw_state.display, (CFDictionaryRef)glw_state.desktopMode);
			ReleaseAllDisplays();
			common->Printf("CGLSetFullScreen -> %d (%s)\n", err, CGLErrorString(err));
		}

		Sys_SetMouseInputRect( CGDisplayBounds( glw_state.display ) );
	}

#ifndef USE_CGLMACROS
	// Make this the current context
	OSX_GLContextSetCurrent();
#endif

	// Store off the pixel format attributes that we actually got
	GLint pixelAttrib = 0;
	[pixelFormat getValues: &pixelAttrib forAttribute: NSOpenGLPFAColorSize forVirtualScreen: 0];
	glConfig.colorBits = pixelAttrib;
	[pixelFormat getValues: &pixelAttrib forAttribute: NSOpenGLPFADepthSize forVirtualScreen: 0];
	glConfig.depthBits = pixelAttrib;
	[pixelFormat getValues: &pixelAttrib forAttribute: NSOpenGLPFAStencilSize forVirtualScreen: 0];
	glConfig.stencilBits = pixelAttrib;

	glConfig.displayFrequency = [[glw_state.gameMode objectForKey: (id)kCGDisplayRefreshRate] intValue];
    
	common->Printf(  "ok\n" );

	return true;
}

// This can be used to temporarily disassociate the GL context from the screen so that CoreGraphics can be used to draw to the screen.
void Sys_PauseGL () {
	if (!glw_state.glPauseCount) {
		glFinish (); // must do this to ensure the queue is complete
        
		CGLClearDrawable(OSX_GetCGLContext());
		[OSX_GetNSGLContext() clearDrawable];
	}
	glw_state.glPauseCount++;
}

// This can be used to reverse the pausing caused by Sys_PauseGL()
void Sys_ResumeGL () {
	if (glw_state.glPauseCount) {
		glw_state.glPauseCount--;
		if (!glw_state.glPauseCount) {
			if (!glConfig.isFullscreen) {
				[OSX_GetNSGLContext() setView: [glw_state.window contentView]];
			} else {
				CGLError err;
                
				err = CGLSetFullScreen(OSX_GetCGLContext());
				if (err)
					common->Printf("CGLSetFullScreen -> %d (%s)\n", err, CGLErrorString(err));
			}
		}
	}
}

/*
===================
GLimp_Init

===================
*/

#ifdef OMNI_TIMER
static void GLImp_Dump_Stamp_List_f(void) {
	OTStampListDumpToFile(glThreadStampList, "/tmp/gl_stamps");
}
#endif

bool GLimp_Init( glimpParms_t parms ) {
	char *buf;

	common->Printf(  "Initializing OpenGL subsystem\n" );
	common->Printf(  "  fullscreen: %s\n", cvarSystem->GetCVarBool( "r_fullscreen" ) ? "yes" : "no" );

	Sys_StoreGammaTables();
    
	if ( !Sys_QueryVideoMemory() ) {
		common->Error(  "Could not initialize OpenGL.  There does not appear to be an OpenGL-supported video card in your system.\n" );
	}
    
	if ( !GLimp_SetMode( parms ) ) {
	}

	common->Printf(  "------------------\n" );

	// get our config strings
	glConfig.vendor_string = (const char *)glGetString( GL_VENDOR );
	glConfig.renderer_string = (const char *)glGetString( GL_RENDERER );
	glConfig.version_string = (const char *)glGetString( GL_VERSION );
	glConfig.extensions_string = (const char *)glGetString( GL_EXTENSIONS );

	//
	// chipset specific configuration
	//
	buf = (char *)malloc(strlen(glConfig.renderer_string) + 1);
	strcpy( buf, glConfig.renderer_string );

	//	Cvar_Set( "r_lastValidRenderer", glConfig.renderer_string );
	free(buf);

	GLW_InitExtensions();

	/*    
#ifndef USE_CGLMACROS
	if (!r_enablerender->integer)
		OSX_GLContextClearCurrent();
#endif
	*/
}


/*
** GLimp_SwapBuffers
** 
** Responsible for doing a swapbuffers and possibly for other stuff
** as yet to be determined.  Probably better not to make this a GLimp
** function and instead do a call to GLimp_SwapBuffers.
*/
void GLimp_SwapBuffers( void ) {
	if ( r_swapInterval.IsModified() ) {
		r_swapInterval.ClearModified();
	}

#if !defined(NDEBUG) && defined(QGL_CHECK_GL_ERRORS)
	QGLCheckError("GLimp_EndFrame");
#endif

	if (!glw_state.glPauseCount && !isHidden) {
		glw_state.bufferSwapCount++;
		[OSX_GetNSGLContext() flushBuffer];
	}

	/*    
	if (OSX_GLContextIsCurrent() != r_enablerender->integer) {
		if (r_enablerender->integer) {
			common->Printf("--- Enabling Renderer ---\n");
			OSX_GLContextSetCurrent();
		} else {
			common->Printf("--- Disabling Renderer ---\n");
			OSX_GLContextClearCurrent();
		}
	}
	*/
}

/*
** GLimp_Shutdown
**
** This routine does all OS specific shutdown procedures for the OpenGL
** subsystem.  Under OpenGL this means NULLing out the current DC and
** HGLRC, deleting the rendering context, and releasing the DC acquired
** for the window.  The state structure is also nulled out.
**
*/

static void _GLimp_RestoreOriginalVideoSettings() {
	CGDisplayErr err;
    
	// CGDisplayCurrentMode lies because we've captured the display and thus we won't
	// get any notifications about what the current display mode really is.  For now,
	// we just always force it back to what mode we remember the desktop being in.
	if (glConfig.isFullscreen) {
		err = CGDisplaySwitchToMode(glw_state.display, (CFDictionaryRef)glw_state.desktopMode);
		if ( err != CGDisplayNoErr )
			common->Printf(  " Unable to restore display mode!\n" );

		ReleaseAllDisplays();
	}
}

void GLimp_Shutdown( void ) {
	CGDisplayCount displayIndex;

	common->Printf("----- Shutting down GL -----\n");

	Sys_FadeScreen(Sys_DisplayToUse());
    
	if (OSX_GetNSGLContext()) {
#ifndef USE_CGLMACROS
		OSX_GLContextClearCurrent();
#endif
		CGLClearDrawable(OSX_GetCGLContext());
		[OSX_GetNSGLContext() clearDrawable];
        
		[OSX_GetNSGLContext() release];
		OSX_SetGLContext((id)nil);
	}

	_GLimp_RestoreOriginalVideoSettings();
    
	Sys_UnfadeScreens();

	// Restore the original gamma if needed.
	//    if (glConfig.deviceSupportsGamma) {
	//        common->Printf("Restoring ColorSync settings\n");
	//        CGDisplayRestoreColorSyncSettings();
	//    }

	if (glw_state.window) {
		[glw_state.window release];
		glw_state.window = nil;
	}

	if (glw_state.log_fp) {
		fclose(glw_state.log_fp);
		glw_state.log_fp = 0;
	}

	for (displayIndex = 0; displayIndex < glw_state.displayCount; displayIndex++) {
		free(glw_state.originalDisplayGammaTables[displayIndex].red);
		free(glw_state.originalDisplayGammaTables[displayIndex].blue);
		free(glw_state.originalDisplayGammaTables[displayIndex].green);
	}
	free(glw_state.originalDisplayGammaTables);
	if (glw_state.tempTable.red) {
		free(glw_state.tempTable.red);
		free(glw_state.tempTable.blue);
		free(glw_state.tempTable.green);
	}
	if (glw_state.inGameTable.red) {
		free(glw_state.inGameTable.red);
		free(glw_state.inGameTable.blue);
		free(glw_state.inGameTable.green);
	}
    
	memset(&glConfig, 0, sizeof(glConfig));
	//    memset(&glState, 0, sizeof(glState));
	memset(&glw_state, 0, sizeof(glw_state));

	common->Printf("----- Done shutting down GL -----\n");
}

/*
===============
GLimp_LogComment
===============
*/
void	GLimp_LogComment( char *comment ) { }

/*
===============
GLimp_SetGamma
===============
*/
void GLimp_SetGamma(unsigned short red[256],
                    unsigned short green[256],
                    unsigned short blue[256]) {
	CGGammaValue redGamma[256], greenGamma[256], blueGamma[256];
	uint32_t i;
	CGDisplayErr err;
        
	for (i = 0; i < 256; i++) {
		redGamma[i]   = red[i]   / 65535.0;
		greenGamma[i] = green[i] / 65535.0;
		blueGamma[i]  = blue[i]  / 65535.0;
	}
    
	err = CGSetDisplayTransferByTable(glw_state.display, 256, redGamma, greenGamma, blueGamma);
	if (err != CGDisplayNoErr) {
	}
    
	// Store the gamma table that we ended up using so we can reapply it later when unhiding or to work around the bug where if you leave the game sitting and the monitor sleeps, when it wakes, the gamma isn't reset.
	glw_state.inGameTable.display = glw_state.display;
	Sys_GetGammaTable(&glw_state.inGameTable);
}

/*****************************************************************************/

#pragma mark -
#pragma mark ? ATI_fragment_shader

#if 0

static GLuint sGeneratingProgram = 0;
static int sCurrentPass;
static char sConstString[4096];
static char sPassString[2][4096];
static int sOpUsed;
static int sConstUsed;
static int sConst[8];
static GLfloat sConstVal[8][4];

static void _endPass (void) {
	sOpUsed = 0;
	sCurrentPass ++;
}

GLuint glGenFragmentShadersATI (GLuint ID) {
	glGenProgramsARB(1, &ID);
}

void glBindFragmentShaderATI (GLuint ID) {
	glBindProgramARB(GL_TEXT_FRAGMENT_SHADER_ATI, ID);
}

void glDeleteFragmentShaderATI (GLuint ID) {
//	glDeleteProgramsARB(1, &ID);
}

void glBeginFragmentShaderATI (void) {
	int i;

	sConstString[0] = 0;
	for (i = 0; i < 8; i ++)
		sConst[i] = 0;
	
	sOpUsed = 0;
	sPassString[0][0] = 0;
	sPassString[1][0] = 0;
	
	sCurrentPass = 0;
	sGeneratingProgram = 1;
}

void glEndFragmentShaderATI (void) {
	GLint errPos;
	int i;
	char fragString[4096];
	
	sGeneratingProgram = 0;

	// header
	strcpy(fragString, "!!ATIfs1.0\n");
	
	// constants
	if (sConstString[0] || sConstUsed) {
		strcat (fragString, "StartConstants;\n");
		if (sConstUsed) {
			for (i = 0; i < 8; i ++) {
				if (sConst[i] == 1) {
					char str[128];
					sprintf (str, "    CONSTANT c%d = program.env[%d];\n", i, i);
					strcat (fragString, str);
				}
			}
		}
		if (sConstString[0]) {
			strcat (fragString, sConstString);
		}
		strcat (fragString, "EndConstants;\n\n");
	}

	if (sCurrentPass == 0) {
		strcat(fragString, "StartOutputPass;\n");
		strcat(fragString, sPassString[0]);
		strcat(fragString, "EndPass;\n");
	} else {
		strcat(fragString, "StartPrelimPass;\n");
		strcat(fragString, sPassString[0]);
		strcat(fragString, "EndPass;\n\n");

		strcat(fragString, "StartOutputPass;\n");
		strcat(fragString, sPassString[1]);
		strcat(fragString, "EndPass;\n");
	}

	glProgramStringARB(GL_TEXT_FRAGMENT_SHADER_ATI, GL_PROGRAM_FORMAT_ASCII_ARB, strlen(fragString), fragString);
	glGetIntegerv( GL_PROGRAM_ERROR_POSITION_ARB, &errPos );
	if(errPos != -1) {
		const GLubyte *errString = glGetString(GL_PROGRAM_ERROR_STRING_ARB);
	}
}


void glSetFragmentShaderConstantATI (GLuint num, const GLfloat *val) {
	int constNum = num-GL_CON_0_ATI;
	if (sGeneratingProgram) {
		char str[128];
		sprintf (str, "    CONSTANT c%d = { %f, %f, %f, %f };\n", constNum, val[0], val[1], val[2], val[3]);
		strcat (sConstString, str);
		sConst[constNum] = 2;
	}
	else {
		// According to Duane, frequent setting of fragment shader constants, even if they contain
		// the same value, will cause a performance hit.
		// According to Chris Bentley at ATI, this performance hit appears if you are using
		// many different fragment shaders in each scene.
		// So, we cache those values and only set the constants if they are different.
		if (memcmp (val, sConstVal[constNum], sizeof(GLfloat)*8*4) != 0)
		{
			glProgramEnvParameter4fvARB (GL_TEXT_FRAGMENT_SHADER_ATI, num-GL_CON_0_ATI, val);
			memcpy (sConstVal[constNum], val, sizeof(GLfloat)*8*4);
		}
	}
}

char *makeArgStr(GLuint arg) {
	// its value outside this routine. 
	static char str[128];
	
	strcpy (str, "");
	
	if ( arg >= GL_REG_0_ATI && arg <= GL_REG_5_ATI ) {
		sprintf(str, "r%d", arg - GL_REG_0_ATI);
	} else if(arg >= GL_CON_0_ATI && arg <= GL_CON_7_ATI) {
		if(!sConst[arg - GL_CON_0_ATI]) {
			sConstUsed = 1;
			sConst[arg - GL_CON_0_ATI] = 1;
		}
		sprintf(str, "c%d", arg - GL_CON_0_ATI);
	} else if( arg >= GL_TEXTURE0_ARB && arg <= GL_TEXTURE31_ARB ) {
		sprintf(str, "t%d", arg - GL_TEXTURE0_ARB);
	} else if( arg == GL_PRIMARY_COLOR_ARB ) {
		strcpy(str, "color0");	
	} else if(arg == GL_SECONDARY_INTERPOLATOR_ATI) {
		strcpy(str, "color1");
	} else if (arg == GL_ZERO) {
		strcpy(str, "0");
	} else if (arg == GL_ONE) {
		strcpy(str, "1");
	} else {
	}
}

void glPassTexCoordATI (GLuint dst, GLuint coord, GLenum swizzle) {
	char str[128] = "\0";
	_endPass();

	switch(swizzle) {
		case GL_SWIZZLE_STR_ATI:
			sprintf(str, "    PassTexCoord r%d, %s.str;\n", dst - GL_REG_0_ATI, makeArgStr(coord));
			break;
		case GL_SWIZZLE_STQ_ATI:
			sprintf(str, "    PassTexCoord r%d, %s.stq;\n", dst - GL_REG_0_ATI, makeArgStr(coord));
			break;
		case GL_SWIZZLE_STR_DR_ATI:
			sprintf(str, "    PassTexCoord r%d, %s.str_dr;\n", dst - GL_REG_0_ATI, makeArgStr(coord));
			break;
		case GL_SWIZZLE_STQ_DQ_ATI:
			sprintf(str, "    PassTexCoord r%d, %s.stq_dq;\n", dst - GL_REG_0_ATI, makeArgStr(coord));
			break;
		default:
			break;
	}
	strcat(sPassString[sCurrentPass], str);
}

void glSampleMapATI (GLuint dst, GLuint interp, GLenum swizzle) {
	char str[128] = "\0";
	_endPass();

	switch(swizzle) {
		case GL_SWIZZLE_STR_ATI:
			sprintf(str, "    SampleMap r%d, %s.str;\n", dst - GL_REG_0_ATI, makeArgStr(interp));
			break;
		case GL_SWIZZLE_STQ_ATI:
			sprintf(str, "    SampleMap r%d, %s.stq;\n", dst - GL_REG_0_ATI, makeArgStr(interp));
			break;
		case GL_SWIZZLE_STR_DR_ATI:
			sprintf(str, "    SampleMap r%d, %s.str_dr;\n", dst - GL_REG_0_ATI, makeArgStr(interp));
			break;
		case GL_SWIZZLE_STQ_DQ_ATI:
			sprintf(str, "    SampleMap r%d, %s.stq_dq;\n", dst - GL_REG_0_ATI, makeArgStr(interp));
			break;
		default:
			break;
	}
	strcat(sPassString[sCurrentPass], str);
}

char *makeMaskStr(GLuint mask) {
	// its value outside this routine. 
	static char str[128];
	
	strcpy (str, "");
	
	switch (mask) {
		case GL_NONE:
			str[0] = '\0';
			break;
		case GL_RGBA:
			strcpy(str, ".rgba");
			break;
		case GL_RGB:
			strcpy(str, ".rgb");
			break;
		case GL_RED:
			strcpy(str, ".r");
			break;
		case GL_GREEN:
			strcpy(str, ".g");
			break;
		case GL_BLUE:
			strcpy(str, ".b");
			break;
		case GL_ALPHA:
			strcpy(str, ".a");
			break;
		default:
			strcpy(str, ".");
			if( mask & GL_RED_BIT_ATI )
				strcat(str, "r");
			if( mask & GL_GREEN_BIT_ATI )
				strcat(str, "g");
			if( mask & GL_BLUE_BIT_ATI )
				strcat(str, "b");
			break;
	}
		
}

char *makeDstModStr(GLuint mod) {
	// its value outside this routine. 
	static char str[128];
	
	strcpy (str, "");
	
	if( mod == GL_NONE) {
		str[0] = '\0';
	}
	if( mod & GL_2X_BIT_ATI) {
		strcat(str, ".2x");
	}

	if( mod & GL_4X_BIT_ATI) {
		strcat(str, ".4x");
	}

	if( mod & GL_8X_BIT_ATI) {
		strcat(str, ".8x");
	}

	if( mod & GL_SATURATE_BIT_ATI) {
		strcat(str, ".sat");
	}

	if( mod & GL_HALF_BIT_ATI) {
		strcat(str, ".half");
	}
	
	if( mod & GL_QUARTER_BIT_ATI) {
		strcat(str, ".quarter");
	}

	if( mod & GL_EIGHTH_BIT_ATI) {
		strcat(str, ".eighth");
	}

}	

char *makeArgModStr(GLuint mod) {
	// its value outside this routine. 
	static char str[128];
	
	strcpy (str, "");
	
	if( mod == GL_NONE) {
		str[0] = '\0';
	}
	if( mod & GL_NEGATE_BIT_ATI) {
		strcat(str, ".neg");
	}

	if( mod & GL_2X_BIT_ATI) {
		strcat(str, ".2x");
	}

	if( mod & GL_BIAS_BIT_ATI) {
		strcat(str, ".bias");
	}
		
	if( mod & GL_COMP_BIT_ATI) {
		strcat(str, ".comp");
	}
		
}

void glColorFragmentOp1ATI (GLenum op, GLuint dst, GLuint dstMask, GLuint dstMod, GLuint arg1, GLuint arg1Rep, GLuint arg1Mod) {
	char str[128] = "\0";
	
	sOpUsed = 1;
	
	switch(op) {
		// Unary operators
		case GL_MOV_ATI:
			sprintf(str, "    MOV r%d", dst - GL_REG_0_ATI);
			break;
		default:
			break;
	}
	if(dstMask != GL_NONE)  {
		strcat(str, makeMaskStr(dstMask));
	}
	else {
		strcat(str, ".rgb" );
	}
	
	if(dstMod != GL_NONE) {
		strcat(str, makeDstModStr(dstMod));
	}
	strcat(str, ", ");
	
	strcat(str, makeArgStr(arg1));
	if(arg1Rep != GL_NONE)  {
		strcat(str, makeMaskStr(arg1Rep));
	}
	if(arg1Mod != GL_NONE) {
		strcat(str, makeArgModStr(arg1Mod));
	}
	strcat(str, ";\n");
	
	strcat(sPassString[sCurrentPass], str);
}

void glColorFragmentOp2ATI (GLenum op, GLuint dst, GLuint dstMask, GLuint dstMod, GLuint arg1, GLuint arg1Rep, GLuint arg1Mod, GLuint arg2, GLuint arg2Rep, GLuint arg2Mod) {
	char str[128] = "\0";
	
	if (!sOpUsed)
		sprintf(str,"\n");
	sOpUsed = 1;
		
	switch(op) {
		// Unary operators - fall back to Op1 routine.
		case GL_MOV_ATI:
			glColorFragmentOp1ATI(op, dst, dstMask, dstMod, arg1, arg1Rep, arg1Mod);

		// Binary operators
		case GL_ADD_ATI:
			sprintf(str, "    ADD r%d", dst - GL_REG_0_ATI);
			break;
		case GL_MUL_ATI:
			sprintf(str, "    MUL r%d", dst - GL_REG_0_ATI);
			break;
		case GL_SUB_ATI:
			sprintf(str, "    SUB r%d", dst - GL_REG_0_ATI);
			break;
		case GL_DOT3_ATI:
			sprintf(str, "    DOT3 r%d", dst - GL_REG_0_ATI);
			break;
		case GL_DOT4_ATI:
			sprintf(str, "    DOT4 r%d", dst - GL_REG_0_ATI);
			break;
		default:
			break;
	}
	if(dstMask != GL_NONE)  {
		strcat(str, makeMaskStr(dstMask));
	}
	else {
		strcat(str, ".rgb" );
	}
	if(dstMod != GL_NONE) {
		strcat(str, makeDstModStr(dstMod));
	}
	strcat(str, ", ");
	
	strcat(str, makeArgStr(arg1));
//	if(arg1Rep != GL_NONE) 
		strcat(str, makeMaskStr(arg1Rep));
	if(arg1Mod != GL_NONE) {
		strcat(str, makeArgModStr(arg1Mod));
	}
	strcat(str, ", ");
	
	strcat(str, makeArgStr(arg2));
//	if(arg2Rep != GL_NONE) 
		strcat(str, makeMaskStr(arg2Rep));
	if(arg2Mod != GL_NONE) {
		strcat(str, makeArgModStr(arg2Mod));			
	}
	strcat(str, ";\n");
	
	strcat(sPassString[sCurrentPass], str);
}

void glColorFragmentOp3ATI (GLenum op, GLuint dst, GLuint dstMask, GLuint dstMod, GLuint arg1, GLuint arg1Rep, GLuint arg1Mod, GLuint arg2, GLuint arg2Rep, GLuint arg2Mod, GLuint arg3, GLuint arg3Rep, GLuint arg3Mod) {
	char str[128] = "\0";
	
	sOpUsed = 1;
	
	switch(op) {
		// Unary operators - fall back to Op1 routine.
		case GL_MOV_ATI:
			glColorFragmentOp1ATI(op, dst, dstMask, dstMod, arg1, arg1Rep, arg1Mod);

		// Binary operators - fall back to Op2 routine.
		case GL_ADD_ATI:
		case GL_MUL_ATI:
		case GL_SUB_ATI:
		case GL_DOT3_ATI:
		case GL_DOT4_ATI:
			glColorFragmentOp2ATI(op, dst, dstMask, dstMod, arg1, arg1Rep, arg1Mod, arg2, arg2Rep, arg2Mod);
			break;

		case GL_MAD_ATI:
			sprintf(str, "    MAD r%d", dst - GL_REG_0_ATI);
			break;
		case GL_LERP_ATI:
			sprintf(str, "    LERP r%d", dst - GL_REG_0_ATI);
			break;
		case GL_CND_ATI:
			sprintf(str, "    CND r%d", dst - GL_REG_0_ATI);
			break;
		case GL_CND0_ATI:
			sprintf(str, "    CND0 r%d", dst - GL_REG_0_ATI);
			break;
		case GL_DOT2_ADD_ATI:
			sprintf(str, "    DOT2ADD r%d", dst - GL_REG_0_ATI);
			break;
		default:
			break;
	}

	if(dstMask != GL_NONE)  {
		strcat(str, makeMaskStr(dstMask));
	}
	else {
		strcat(str, ".rgb" );
	}
	if(dstMod != GL_NONE) {
		strcat(str, makeDstModStr(dstMod));
	}
	strcat(str, ", ");
	
	strcat(str, makeArgStr(arg1));
	if(arg1Rep != GL_NONE)  {
		strcat(str, makeMaskStr(arg1Rep));
	}
	if(arg1Mod != GL_NONE) {
		strcat(str, makeArgModStr(arg1Mod));
	}
	strcat(str, ", ");
	
	strcat(str, makeArgStr(arg2));
	if(arg2Rep != GL_NONE)  {
		strcat(str, makeMaskStr(arg2Rep));
	}
	if(arg2Mod != GL_NONE) {
		strcat(str, makeArgModStr(arg2Mod));
	}
	strcat(str, ", ");
		
	strcat(str, makeArgStr(arg3));
	if(arg3Rep != GL_NONE)  {
		strcat(str, makeMaskStr(arg3Rep));
	}
	if(arg3Mod != GL_NONE) {
		strcat(str, makeArgModStr(arg3Mod));
	}
	strcat(str, ";\n");
	
	strcat(sPassString[sCurrentPass], str);
}

void glAlphaFragmentOp1ATI (GLenum op, GLuint dst, GLuint dstMod, GLuint arg1, GLuint arg1Rep, GLuint arg1Mod) {
	glColorFragmentOp1ATI ( op, dst, GL_ALPHA, dstMod, arg1, arg1Rep, arg1Mod);
}

void glAlphaFragmentOp2ATI (GLenum op, GLuint dst, GLuint dstMod, GLuint arg1, GLuint arg1Rep, GLuint arg1Mod, GLuint arg2, GLuint arg2Rep, GLuint arg2Mod) {
	glColorFragmentOp2ATI ( op, dst, GL_ALPHA, dstMod, arg1, arg1Rep, arg1Mod, arg2, arg2Rep, arg2Mod);
}

void glAlphaFragmentOp3ATI (GLenum op, GLuint dst, GLuint dstMod, GLuint arg1, GLuint arg1Rep, GLuint arg1Mod, GLuint arg2, GLuint arg2Rep, GLuint arg2Mod, GLuint arg3, GLuint arg3Rep, GLuint arg3Mod) {
	glColorFragmentOp3ATI ( op, dst, GL_ALPHA, dstMod, arg1, arg1Rep, arg1Mod, arg2, arg2Rep, arg2Mod, arg3, arg3Rep, arg3Mod);
}
#endif
#pragma mark -

void *GLimp_ExtensionPointer(const char *name) {
	NSSymbol symbol;
	char *symbolName;

	// special case for ATI_fragment_shader calls to map to ATI_text_fragment_shader routines
	if (!strcmp(name, "glGenFragmentShadersATI")) {
	}
	if (!strcmp(name, "glBindFragmentShaderATI")) {
	}
	if (!strcmp(name, "glDeleteFragmentShaderATI")) {
	}
	if (!strcmp(name, "glBeginFragmentShaderATI")) {
	}
	if (!strcmp(name, "glEndFragmentShaderATI")) {
	}
	if (!strcmp(name, "glPassTexCoordATI")) {
	}
	if (!strcmp(name, "glSampleMapATI")) {
	}
	if (!strcmp(name, "glColorFragmentOp1ATI")) {
	}
	if (!strcmp(name, "glColorFragmentOp2ATI")) {
	}
	if (!strcmp(name, "glColorFragmentOp3ATI")) {
	}
	if (!strcmp(name, "glAlphaFragmentOp1ATI")) {
	}
	if (!strcmp(name, "glAlphaFragmentOp2ATI")) {
	}
	if (!strcmp(name, "glAlphaFragmentOp3ATI")) {
	}
	if (!strcmp(name, "glSetFragmentShaderConstantATI")) {
	}

	// Prepend a '_' for the Unix C symbol mangling convention
	symbolName = (char *)alloca(strlen(name) + 2);
	strcpy(symbolName + 1, name);
	symbolName[0] = '_';

	if ( !NSIsSymbolNameDefined( symbolName ) ) {
		return NULL;
	}

	symbol = NSLookupAndBindSymbol(symbolName);
	if ( !symbol ) {
		// shouldn't happen ...
		return NULL;
	}

	return NSAddressOfSymbol(symbol);
}

void * wglGetProcAddress(const char *name) {
	return GLimp_ExtensionPointer(name);
}


/*
** GLW_InitExtensions
*/
void GLW_InitExtensions( void ) { }

#define MAX_RENDERER_INFO_COUNT 128

unsigned long Sys_QueryVideoMemory() {
	CGLError err;
	CGLRendererInfoObj rendererInfo = NULL;
	GLint rendererCount = 0;
	GLint rendererIndex = 0;
	GLint maxVRAM = 0;
	GLint vram = 0;
	GLint accelerated = 0;
     
	err = CGLQueryRendererInfo(CGDisplayIDToOpenGLDisplayMask(Sys_DisplayToUse()), &rendererInfo, &rendererCount);
	if (err) {
		common->Printf("CGLQueryRendererInfo -> %d\n", err);
		return 0;
	}
     
	for (rendererIndex = 0; rendererIndex < rendererCount; rendererIndex++) {
		err = CGLDescribeRenderer(rendererInfo, rendererIndex, kCGLRPAccelerated, &accelerated);
		if (err || !accelerated) {
			continue;
		}

#if defined(kCGLRPVideoMemoryMegabytes)
		err = CGLDescribeRenderer(rendererInfo, rendererIndex, kCGLRPVideoMemoryMegabytes, &vram);
		if (!err) {
			vram *= 1024 * 1024;
		}
#else
		err = CGLDescribeRenderer(rendererInfo, rendererIndex, kCGLRPVideoMemory, &vram);
#endif
		if (err) {
			common->Printf("CGLDescribeRenderer -> %d\n", err);
			continue;
		}
		if (vram > maxVRAM) {
			maxVRAM = vram;
		}
	}

	(void)CGLDestroyRendererInfo(rendererInfo);
	return (unsigned long)maxVRAM;
}


// We will set the Sys_IsHidden global to cause input to be handle differently (we'll just let NSApp handle events in this case).  We also will unbind the GL context and restore the video mode.
bool Sys_Hide() {
	if ( isHidden ) {
		// Eh?
	}
    
	if ( !r_fullscreen.GetBool() ) {
		// We only support hiding in fullscreen mode right now
	}
    
	isHidden = true;

	// Don't need to store the current gamma since we always keep it around in glw_state.inGameTable.

	Sys_FadeScreen(Sys_DisplayToUse());

	// Disassociate the GL context from the screen
	CGLClearDrawable(OSX_GetCGLContext());
	[OSX_GetNSGLContext() clearDrawable];
    
	// Restore the original video mode
	_GLimp_RestoreOriginalVideoSettings();

	// Restore the original gamma if needed.
	//    if (glConfig.deviceSupportsGamma) {
	//        CGDisplayRestoreColorSyncSettings();
	//    }

	// Release the screen(s)
	ReleaseAllDisplays();
    
	Sys_UnfadeScreens();
    
	// Shut down the input system so the mouse and keyboard settings are restore to normal
	Sys_ShutdownInput();
    
	// Hide the application so that when the user clicks on our app icon, we'll get an unhide notification
	[NSApp hide: nil];
    
}

CGDisplayErr Sys_CaptureActiveDisplays(void) {
	CGDisplayErr err;
	CGDisplayCount displayIndex;
	for (displayIndex = 0; displayIndex < glw_state.displayCount; displayIndex++) {
		const glwgamma_t *table;
		table = &glw_state.originalDisplayGammaTables[displayIndex];
		err = CGDisplayCapture(table->display);
		if (err != CGDisplayNoErr) {
			return err;
		}
	}
	return CGDisplayNoErr;
}

bool Sys_Unhide() {
	CGDisplayErr err;
	CGLError glErr;
    
	if ( !isHidden) {
		// Eh?
	}
        
	Sys_FadeScreens();

	// Capture the screen(s)
	err = Sys_CaptureActiveDisplays();
	if (err != CGDisplayNoErr) {
		Sys_UnfadeScreens();
		common->Printf(  "Unhide failed -- cannot capture the display again.\n" );
	}
    
	// Restore the game mode
	err = CGDisplaySwitchToMode(glw_state.display, (CFDictionaryRef)glw_state.gameMode);
	if ( err != CGDisplayNoErr ) {
		ReleaseAllDisplays();
		Sys_UnfadeScreens();
		common->Printf(  "Unhide failed -- Unable to set display mode\n" );
	}

	// Reassociate the GL context and the screen
	glErr = CGLSetFullScreen(OSX_GetCGLContext());
	if (glErr) {
		ReleaseAllDisplays();
		Sys_UnfadeScreens();
		common->Printf(  "Unhide failed: CGLSetFullScreen -> %d (%s)\n", err, CGLErrorString(glErr));
	}

	// Restore the current context
	[OSX_GetNSGLContext() makeCurrentContext];
    
	// Restore the gamma that the game had set
	Sys_UnfadeScreen(Sys_DisplayToUse(), &glw_state.inGameTable);
    
	// Restore the input system (last so if something goes wrong we don't eat the mouse)
	Sys_InitInput();
    
	isHidden = false;
}

bool GLimp_SpawnRenderThread( void (*function)( void ) ) {
}

void *GLimp_RendererSleep(void) {
}

void GLimp_FrontEndSleep(void) { }

void GLimp_WakeRenderer( void *data ) { }

void *GLimp_BackEndSleep( void ) {
}

void GLimp_WakeBackEnd( void *data ) {
}

// enable / disable context is just for the r_skipRenderContext debug option
void GLimp_DeactivateContext( void ) {
	[NSOpenGLContext clearCurrentContext];
}

void GLimp_ActivateContext( void ) {
	[OSX_GetNSGLContext() makeCurrentContext];
}

void GLimp_EnableLogging(bool stat) { }

NSDictionary *Sys_GetMatchingDisplayMode( glimpParms_t parms ) {
	NSArray *displayModes;
	NSDictionary *mode;
	unsigned int modeIndex, modeCount, bestModeIndex;
	int verbose;
	//	cvar_t *cMinFreq, *cMaxFreq;
	int minFreq, maxFreq;
	unsigned int colorDepth;
    
	verbose = 0;

	colorDepth = 32;

	minFreq = r_minDisplayRefresh.GetInteger();
	maxFreq = r_maxDisplayRefresh.GetInteger();
	if ( minFreq > maxFreq ) {
		common->Error( "r_minDisplayRefresh must be less than or equal to r_maxDisplayRefresh" );
	}
    
	displayModes = (NSArray *)CGDisplayAvailableModes(glw_state.display);
	if (!displayModes) {
	}
    
	modeCount = [displayModes count];
	if (verbose) {
		common->Printf( "%d modes avaliable\n", modeCount);
		common->Printf( "Current mode is %s\n", [[(id)CGDisplayCurrentMode(glw_state.display) description] UTF8String]);
	}
    
	// Default to the current desktop mode
	bestModeIndex = 0xFFFFFFFF;
    
	for ( modeIndex = 0; modeIndex < modeCount; ++modeIndex ) {
		id object;
		int refresh;
        
		mode = [displayModes objectAtIndex: modeIndex];
		if (verbose) {
			common->Printf( " mode %d -- %s\n", modeIndex, [[mode description] UTF8String]);
		}

		// Make sure we get the right size
		if ([[mode objectForKey: (id)kCGDisplayWidth] intValue] != parms.width ||
				[[mode objectForKey: (id)kCGDisplayHeight] intValue] != parms.height) {
			if (verbose)
				common->Printf( " -- bad size\n");
			continue;
		}

		// Make sure that our frequency restrictions are observed
		refresh = [[mode objectForKey: (id)kCGDisplayRefreshRate] intValue];
		if (minFreq &&  refresh < minFreq) {
			if (verbose)
				common->Printf( " -- refresh too low\n");
			continue;
		}

		if (maxFreq && refresh > maxFreq) {
			if (verbose)
				common->Printf( " -- refresh too high\n");
			continue;
		}

		if ([[mode objectForKey: (id)kCGDisplayBitsPerPixel] intValue] != colorDepth) {
			if (verbose)
				common->Printf( " -- bad depth\n");
			continue;
		}

		object = [mode objectForKey: (id)kCGDisplayModeIsStretched];
		if ( object ) {
			if ( [object boolValue] != cvarSystem->GetCVarBool( "r_stretched" ) ) {
				if (verbose)
					common->Printf( " -- bad stretch setting\n");
				continue;
			}
		}
		else {
			if ( cvarSystem->GetCVarBool( "r_stretched" ) ) {
				if (verbose)
					common->Printf( " -- stretch requested, stretch property not available\n");
				continue;
			}
		}
		
		bestModeIndex = modeIndex;
		if (verbose)
			common->Printf( " -- OK\n", bestModeIndex);
	}

	if (verbose)
		common->Printf( " bestModeIndex = %d\n", bestModeIndex);

	if (bestModeIndex == 0xFFFFFFFF) {
		common->Printf( "No suitable display mode available.\n");
	}
    
}


#define MAX_DISPLAYS 128

void Sys_GetGammaTable(glwgamma_t *table) {
	uint32_t tableSize = 512;
	CGDisplayErr err;
    
	table->tableSize = tableSize;
	if (table->red)
		free(table->red);
	table->red = (float *)malloc(tableSize * sizeof(*table->red));
	if (table->green)
		free(table->green);
	table->green = (float *)malloc(tableSize * sizeof(*table->green));
	if (table->blue)
		free(table->blue);
	table->blue = (float *)malloc(tableSize * sizeof(*table->blue));
    
	// TJW: We _could_ loop here if we get back the same size as our table, increasing the table size.
	err = CGGetDisplayTransferByTable(table->display, tableSize, table->red, table->green, table->blue,
																		&table->tableSize);
	if (err != CGDisplayNoErr) {
		table->tableSize = 0;
	}
}

void Sys_SetGammaTable(glwgamma_t *table) { }

void Sys_StoreGammaTables() {
	// Store the original gamma for all monitors so that we can fade and unfade them all
	CGDirectDisplayID displays[MAX_DISPLAYS];
	CGDisplayCount displayIndex;
	CGDisplayErr err;

	err = CGGetActiveDisplayList(MAX_DISPLAYS, displays, &glw_state.displayCount);
	if (err != CGDisplayNoErr)
    
	glw_state.originalDisplayGammaTables = (glwgamma_t *)calloc(glw_state.displayCount, sizeof(*glw_state.originalDisplayGammaTables));
	for (displayIndex = 0; displayIndex < glw_state.displayCount; displayIndex++) {
		glwgamma_t *table;

		table = &glw_state.originalDisplayGammaTables[displayIndex];
		table->display = displays[displayIndex];
		Sys_GetGammaTable(table);
	}
}


//  This isn't a mathematically correct fade, but we don't care that much.
void Sys_SetScreenFade(glwgamma_t *table, float fraction) {
	uint32_t tableSize;
	CGGammaValue *red, *blue, *green;
	uint32_t gammaIndex;
    
	//    if (!glConfig.deviceSupportsGamma)

	if (!(tableSize = table->tableSize))
		// we couldn't get the table for this display for some reason
    
	//    common->Printf("0x%08x %f\n", table->display, fraction);
    
	red = glw_state.tempTable.red;
	green = glw_state.tempTable.green;
	blue = glw_state.tempTable.blue;
	if (glw_state.tempTable.tableSize < tableSize) {
		glw_state.tempTable.tableSize = tableSize;
		red = (float *)realloc(red, sizeof(*red) * tableSize);
		green = (float *)realloc(green, sizeof(*green) * tableSize);
		blue = (float *)realloc(blue, sizeof(*blue) * tableSize);
		glw_state.tempTable.red = red;
		glw_state.tempTable.green = green;
		glw_state.tempTable.blue = blue;
	}

	for (gammaIndex = 0; gammaIndex < table->tableSize; gammaIndex++) {
		red[gammaIndex] = table->red[gammaIndex] * fraction;
		blue[gammaIndex] = table->blue[gammaIndex] * fraction;
		green[gammaIndex] = table->green[gammaIndex] * fraction;
	}
    
	CGSetDisplayTransferByTable(table->display, table->tableSize, red, green, blue);
}

// Fades all the active displays at the same time.

#define FADE_DURATION 0.5
void Sys_FadeScreens() {
	CGDisplayCount displayIndex;
	int stepIndex;
	glwgamma_t *table;
	NSTimeInterval start, current;
	float time;
    
	//   if (!glConfig.deviceSupportsGamma)

	common->Printf("Fading all displays\n");
    
	start = [NSDate timeIntervalSinceReferenceDate];
	time = 0.0;
	while (time != FADE_DURATION) {
		current = [NSDate timeIntervalSinceReferenceDate];
		time = current - start;
		if (time > FADE_DURATION)
			time = FADE_DURATION;
            
		for (displayIndex = 0; displayIndex < glw_state.displayCount; displayIndex++) {            
			table = &glw_state.originalDisplayGammaTables[displayIndex];
			Sys_SetScreenFade(table, 1.0 - time / FADE_DURATION);
		}
	}
}

void Sys_FadeScreen(CGDirectDisplayID display) {
	CGDisplayCount displayIndex;
	glwgamma_t *table;
	int stepIndex;

	common->Printf( "FIXME: Sys_FadeScreen\n" );
    
    
	//    if (!glConfig.deviceSupportsGamma)

	common->Printf("Fading display 0x%08x\n", display);

	for (displayIndex = 0; displayIndex < glw_state.displayCount; displayIndex++) {
		if (display == glw_state.originalDisplayGammaTables[displayIndex].display) {
			NSTimeInterval start, current;
			float time;
            
			start = [NSDate timeIntervalSinceReferenceDate];
			time = 0.0;

			table = &glw_state.originalDisplayGammaTables[displayIndex];
			while (time != FADE_DURATION) {
				current = [NSDate timeIntervalSinceReferenceDate];
				time = current - start;
				if (time > FADE_DURATION)
					time = FADE_DURATION;

				Sys_SetScreenFade(table, 1.0 - time / FADE_DURATION);
			}
		}
	}

	common->Printf("Unable to find display to fade it\n");
}

void Sys_UnfadeScreens() {
	CGDisplayCount displayIndex;
	int stepIndex;
	glwgamma_t *table;
	NSTimeInterval start, current;
	float time;

	common->Printf( "FIXME: Sys_UnfadeScreens\n" );
    
    
	//    if (!glConfig.deviceSupportsGamma)
        
	common->Printf("Unfading all displays\n");

	start = [NSDate timeIntervalSinceReferenceDate];
	time = 0.0;
	while (time != FADE_DURATION) {
		current = [NSDate timeIntervalSinceReferenceDate];
		time = current - start;
		if (time > FADE_DURATION)
			time = FADE_DURATION;
            
		for (displayIndex = 0; displayIndex < glw_state.displayCount; displayIndex++) {            
			table = &glw_state.originalDisplayGammaTables[displayIndex];
			Sys_SetScreenFade(table, time / FADE_DURATION);
		}
	}
}

void Sys_UnfadeScreen(CGDirectDisplayID display, glwgamma_t *table) {
	CGDisplayCount displayIndex;
	int stepIndex;
	
	common->Printf( "FIXME: Sys_UnfadeScreen\n" );

	//    if (!glConfig.deviceSupportsGamma)
    
	common->Printf("Unfading display 0x%08x\n", display);

	if (table) {
		uint32_t i;
        
		common->Printf("Given table:\n");
		for (i = 0; i < table->tableSize; i++) {
			common->Printf("  %f %f %f\n", table->red[i], table->blue[i], table->green[i]);
		}
	}
    
	// Search for the original gamma table for the display
	if (!table) {
		for (displayIndex = 0; displayIndex < glw_state.displayCount; displayIndex++) {
			if (display == glw_state.originalDisplayGammaTables[displayIndex].display) {
				table = &glw_state.originalDisplayGammaTables[displayIndex];
				break;
			}
		}
	}
    
	if (table) {
		NSTimeInterval start, current;
		float time;
        
		start = [NSDate timeIntervalSinceReferenceDate];
		time = 0.0;

		while (time != FADE_DURATION) {
			current = [NSDate timeIntervalSinceReferenceDate];
			time = current - start;
			if (time > FADE_DURATION)
				time = FADE_DURATION;
			Sys_SetScreenFade(table, time / FADE_DURATION);
		}
	}
    
	common->Printf("Unable to find display to unfade it\n");
}

#define MAX_DISPLAYS 128

CGDirectDisplayID Sys_DisplayToUse(void) {
	static bool					gotDisplay =  NO;
	static CGDirectDisplayID	displayToUse;
    
	CGDisplayErr				err;
	CGDirectDisplayID			displays[MAX_DISPLAYS];
	CGDisplayCount				displayCount;
	int							displayIndex;
    
	if ( gotDisplay ) {
	}
	gotDisplay = YES;    
    
	err = CGGetActiveDisplayList( MAX_DISPLAYS, displays, &displayCount );
	if ( err != CGDisplayNoErr ) {
	}

	// -1, the default, means to use the main screen
	displayIndex = r_screen.GetInteger();
        
	if ( displayIndex < 0 || displayIndex >= displayCount ) {
		// This is documented (in CGDirectDisplay.h) to be the main display.  We want to
		// display IDs.
		displayToUse = displays[ 0 ];
	} else {
		displayToUse = displays[ displayIndex ];
	}

}

/*
===================
GLimp_SetScreenParms
===================
*/
bool GLimp_SetScreenParms( glimpParms_t parms ) {
}

/*
===================
Sys_GrabMouseCursor
===================
*/
void Sys_GrabMouseCursor( bool grabIt ) { }

