// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
//
// OpenXR presentation, OpenGL backend.
//
// While a VR frame presents, an eye or the virtual-screen render texture
// stands in for the window (R_SetDefaultRenderTarget), so every path that
// "presents to the back buffer" lands in the headset's targets unchanged. A
// target gets the window's back-buffer passes (CRT, brightness/gamma) when the
// frame leaves it, and the swap blits each finished target into the swapchain
// image the engine acquired for it, then draws the desktop mirror.
// See docs/dev/archive/2026-10-18/plans/2026-10-02-openxr-vr.md.

#include "../tr_local.h"
#include "../TemporalPresentation.h"

#if defined( OPENQ4_RENDERER_GL_MODULE ) && defined( _WIN32 )
#include <windows.h>
#endif
#if defined( OPENQ4_RENDERER_GL_MODULE ) && defined( __linux__ )
#include <dlfcn.h>
#endif

#ifndef GL_FRAMEBUFFER_SRGB
#define GL_FRAMEBUFFER_SRGB 0x8DB9
#endif

// Presentation needs desktop GL: the module that owns a WGL or GLX context.
// The GLES module and the static renderer of dedicated and macOS builds never
// present to OpenXR; the Vulkan module has its own backend
// (Vulkan/vk_VRPresentation.cpp).
#if defined( OPENQ4_RENDERER_GL_MODULE ) && !defined( OPENQ4_RENDERER_GLES_MODULE )
#define OPENQ4_VR_GL_PRESENTATION 1
#endif

// GL presents through the front end's render textures (tr.vrTargets), and its
// driver retires deleted textures itself.
bool R_VR_BackendOwnsTargets( void ) {
	return false;
}

bool R_VR_BackendPrepareTargets( renderVRFrame_t &frame ) {
	(void)frame;
	return false;
}

void R_VR_BackendReleaseImages( void ) {
}

#if defined( OPENQ4_VR_GL_PRESENTATION )

static int		rb_vrTarget = -2;			// -2 none, -1 virtual screen, 0/1 eye
static int		rb_vrFrameCount = -1;		// renderer frame the backend VR state belongs to
static bool		rb_vrCleared[3];			// target index cleared this frame
static GLuint	rb_vrBlitFramebuffer = 0;
static int		rb_vrBlitFramebufferGeneration = -1;

/*
====================
R_VR_BackendCanPresent
====================
*/
bool R_VR_BackendCanPresent( void ) {
	return glConfig.isInitialized
		&& glConfig.backendCaps.profile != RENDERER_CONTEXT_PROFILE_ES
		&& glBindFramebuffer != NULL && glGenFramebuffers != NULL
		&& glFramebufferTexture2D != NULL && glBlitFramebuffer != NULL;
}

/*
====================
R_VR_QueryGraphicsBinding

The OpenXR graphics binding names the context the runtime shares images
with. The module owns that context and it is current on this thread.
====================
*/
static void R_VR_ContextVersion( renderVRGraphicsBinding_t &binding ) {
	GLint major = 0, minor = 0;
	glGetIntegerv( GL_MAJOR_VERSION, &major );
	glGetIntegerv( GL_MINOR_VERSION, &minor );
	if ( glGetError() != GL_NO_ERROR || major <= 0 ) {
		// pre-3.0 contexts do not answer GL_MAJOR_VERSION
		major = static_cast<int>( glConfig.glVersion );
		minor = static_cast<int>( ( glConfig.glVersion - major ) * 10.0f + 0.5f );
	}
	binding.glMajor = major;
	binding.glMinor = minor;
}

#if defined( __linux__ )
void *GLimp_ExtensionPointer( const char *name );

// GLX is resolved at run time so the module needs no X11 or GLX headers; the
// process already has libGL (and libX11 through it) loaded.
static void *R_VR_GLXSymbol( const char *name ) {
	void *symbol = dlsym( RTLD_DEFAULT, name );
	if ( symbol == NULL ) {
		symbol = GLimp_ExtensionPointer( name );
	}
	return symbol;
}
#endif

#if defined( __linux__ )
// EGL as well, which SDL loads for native Wayland, possibly as a local library.
static void *R_VR_EGLSymbol( const char *name ) {
	static void *library = dlopen( "libEGL.so.1", RTLD_NOW | RTLD_NOLOAD );
	void *symbol = dlsym( RTLD_DEFAULT, name );
	if ( symbol == NULL && library != NULL ) {
		symbol = dlsym( library, name );
	}
	return symbol;
}

/*
====================
R_VR_QueryEGLBinding

An OpenGL context on EGL (SDL on native Wayland): XR_MNDX_egl_enable binds
its display, config and context.
====================
*/
static bool R_VR_QueryEGLBinding( renderVRGraphicsBinding_t &binding ) {
	typedef void *( *getCurrent_t )( void );
	typedef unsigned int ( *queryContext_t )( void *display, void *context, int attribute, int *value );
	typedef unsigned int ( *chooseConfig_t )( void *display, const int *attributes, void **configs, int size, int *count );
	const int eglConfigId = 0x3028;		// EGL_CONFIG_ID
	const int eglNone = 0x3038;			// EGL_NONE

	getCurrent_t getCurrentDisplay = (getCurrent_t)R_VR_EGLSymbol( "eglGetCurrentDisplay" );
	getCurrent_t getCurrentContext = (getCurrent_t)R_VR_EGLSymbol( "eglGetCurrentContext" );
	queryContext_t queryContext = (queryContext_t)R_VR_EGLSymbol( "eglQueryContext" );
	chooseConfig_t chooseConfig = (chooseConfig_t)R_VR_EGLSymbol( "eglChooseConfig" );
	void *getProcAddress = R_VR_EGLSymbol( "eglGetProcAddress" );
	if ( getCurrentDisplay == NULL || getCurrentContext == NULL || queryContext == NULL || chooseConfig == NULL
			|| getProcAddress == NULL ) {
		return false;
	}
	binding.eglDisplay = getCurrentDisplay();
	binding.eglContext = getCurrentContext();
	if ( binding.eglDisplay == NULL || binding.eglContext == NULL ) {
		return false;
	}
	int configId = 0;
	if ( !queryContext( binding.eglDisplay, binding.eglContext, eglConfigId, &configId ) ) {
		return false;
	}
	// EGL_CONFIG_ID alone selects exactly that config
	const int attributes[] = { eglConfigId, configId, eglNone };
	void *config = NULL;
	int count = 0;
	if ( !chooseConfig( binding.eglDisplay, attributes, &config, 1, &count ) || count < 1 || config == NULL ) {
		return false;
	}
	binding.eglConfig = config;
	binding.eglGetProcAddress = getProcAddress;
	binding.kind = RENDER_VR_BINDING_EGL;
	return true;
}
#endif

bool R_VR_QueryGraphicsBinding( renderVRGraphicsBinding_t &binding ) {
	memset( &binding, 0, sizeof( binding ) );
	if ( !R_VR_BackendCanPresent() ) {
		return false;
	}
	R_VR_ContextVersion( binding );

#if defined( _WIN32 )
	binding.kind = RENDER_VR_BINDING_WGL;
	binding.wglDC = wglGetCurrentDC();
	binding.wglContext = wglGetCurrentContext();
	return binding.wglDC != NULL && binding.wglContext != NULL;
#elif defined( __linux__ )
	typedef void *( *getCurrentContext_t )( void );
	typedef void *( *getCurrentDisplay_t )( void );
	typedef unsigned long ( *getCurrentDrawable_t )( void );
	typedef int ( *queryContext_t )( void *display, void *context, int attribute, int *value );
	typedef void **( *chooseFBConfig_t )( void *display, int screen, const int *attributes, int *count );
	typedef int ( *getFBConfigAttrib_t )( void *display, void *config, int attribute, int *value );
	typedef int ( *xFree_t )( void *data );
	const int glxScreen = 0x800C;		// GLX_SCREEN
	const int glxVisualId = 0x800B;		// GLX_VISUAL_ID
	const int glxFBConfigId = 0x8013;	// GLX_FBCONFIG_ID

	getCurrentContext_t getCurrentContext = (getCurrentContext_t)R_VR_GLXSymbol( "glXGetCurrentContext" );
	getCurrentDisplay_t getCurrentDisplay = (getCurrentDisplay_t)R_VR_GLXSymbol( "glXGetCurrentDisplay" );
	getCurrentDrawable_t getCurrentDrawable = (getCurrentDrawable_t)R_VR_GLXSymbol( "glXGetCurrentDrawable" );
	queryContext_t queryContext = (queryContext_t)R_VR_GLXSymbol( "glXQueryContext" );
	chooseFBConfig_t chooseFBConfig = (chooseFBConfig_t)R_VR_GLXSymbol( "glXChooseFBConfig" );
	getFBConfigAttrib_t getFBConfigAttrib = (getFBConfigAttrib_t)R_VR_GLXSymbol( "glXGetFBConfigAttrib" );
	xFree_t xFree = (xFree_t)dlsym( RTLD_DEFAULT, "XFree" );
	if ( getCurrentContext == NULL || getCurrentDisplay == NULL || getCurrentDrawable == NULL
			|| queryContext == NULL || chooseFBConfig == NULL || getFBConfigAttrib == NULL ) {
		return R_VR_QueryEGLBinding( binding );
	}

	// an EGL context (native Wayland) leaves these empty: no Xlib binding
	binding.glxContext = getCurrentContext();
	binding.glxDisplay = getCurrentDisplay();
	binding.glxDrawable = getCurrentDrawable();
	if ( binding.glxContext == NULL || binding.glxDisplay == NULL || binding.glxDrawable == 0 ) {
		binding.glxContext = NULL;
		binding.glxDisplay = NULL;
		binding.glxDrawable = 0;
		return R_VR_QueryEGLBinding( binding );
	}

	int configId = 0, screen = 0;
	if ( queryContext( binding.glxDisplay, binding.glxContext, glxFBConfigId, &configId ) != 0
			|| queryContext( binding.glxDisplay, binding.glxContext, glxScreen, &screen ) != 0 ) {
		return false;
	}
	const int attributes[] = { glxFBConfigId, configId, 0 };
	int count = 0;
	void **configs = chooseFBConfig( binding.glxDisplay, screen, attributes, &count );
	if ( configs == NULL || count <= 0 ) {
		return false;
	}
	binding.glxFBConfig = configs[0];
	int visual = 0;
	getFBConfigAttrib( binding.glxDisplay, binding.glxFBConfig, glxVisualId, &visual );
	binding.glxVisualId = static_cast<unsigned int>( visual );
	if ( xFree != NULL ) {
		xFree( configs );	// the config itself belongs to the display
	}
	binding.kind = RENDER_VR_BINDING_GLX;
	return true;
#else
	return false;
#endif
}

/*
====================
RB_VR_BeginBackEnd

Every command list starts on the window; RC_VR_TARGET moves a VR frame onto
its targets.
====================
*/
void RB_VR_BeginBackEnd( void ) {
	if ( tr.vrFrame.active && rb_vrTarget != -2 && rb_vrFrameCount == tr.frameCount ) {
		// a synchronous flush split this frame's commands: stay on the current target
		idRenderTexture *target = tr.vrTargets[rb_vrTarget + 1];
		R_SetDefaultRenderTarget( target );
		if ( R_GetDefaultRenderTarget() == target ) {
			// setting the target forgot that the virtual screen blends
			// premultiplied; the rest of its 2D still needs that
			R_SetDefaultRenderTargetPremultiplied( rb_vrTarget < 0 );
			return;
		}
	}
	R_SetDefaultRenderTarget( NULL );
	rb_vrTarget = -2;
	rb_vrFrameCount = tr.frameCount;
	memset( rb_vrCleared, 0, sizeof( rb_vrCleared ) );
}

// the stencil clear value the backend uses (tr_backend.cpp)
static GLint RB_VR_StencilClearValue( void ) {
	const int stencilBits = idMath::ClampInt( 1, 30, ( glConfig.stencilBits > 0 ) ? glConfig.stencilBits : 8 );
	return 1 << ( stencilBits - 1 );
}

static void RB_VR_ClearBound( float alpha ) {
	const GLboolean scissorWasEnabled = glIsEnabled( GL_SCISSOR_TEST );
	if ( scissorWasEnabled ) {
		glDisable( GL_SCISSOR_TEST );
	}
	glColorMask( GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE );
	glDepthMask( GL_TRUE );
	glStencilMask( 0xff );
	glClearColor( 0.0f, 0.0f, 0.0f, alpha );
	glClearDepth( 1.0f );
	glClearStencil( RB_VR_StencilClearValue() );
	glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT );
	if ( scissorWasEnabled ) {
		glEnable( GL_SCISSOR_TEST );
	}
	GL_ClearStateDelta();
}

// The back-buffer passes a desktop frame runs at swap time, applied to the
// bound VR target.
static void RB_VR_FinishBoundTarget( void ) {
	RB_ApplyCRTToBackBuffer();
	RB_ApplyColorMappingsToBackBuffer();
}

/*
====================
RB_VR_SetTarget
====================
*/
void RB_VR_SetTarget( const void *data ) {
	const vrTargetCommand_t *cmd = (const vrTargetCommand_t *)data;
	const int index = cmd->target + 1;
	if ( !tr.vrFrame.active || index < 0 || index > 2 || cmd->width <= 0 || cmd->height <= 0 ) {
		return;
	}
	idRenderTexture *target = tr.vrTargets[index];
	if ( target == NULL ) {
		return;
	}

	// an eye is finished when the frame leaves it
	if ( rb_vrTarget >= 0 && rb_vrTarget != cmd->target ) {
		RB_VR_FinishBoundTarget();
	}

	R_SetDefaultRenderTarget( target );
	if ( R_GetDefaultRenderTarget() != target ) {
		static bool warned = false;
		if ( !warned ) {
			common->Warning( "VR: render target %d is unusable; the frame stays on the previous target", cmd->target );
			warned = true;
		}
		return;
	}

	rb_vrTarget = cmd->target;
	R_SetDefaultRenderTargetPremultiplied( cmd->target < 0 );
	glConfig.vidWidth = cmd->width;
	glConfig.vidHeight = cmd->height;
	R_TemporalPresentation_SetVRTargetState( cmd->width, cmd->height, cmd->temporalAA );
	backEnd.renderTexture = NULL;
	backEnd.feedbackRenderTexture = NULL;
	idRenderTexture::BindNull();
	R_SetDefaultDrawAndReadBuffers();
	glViewport( 0, 0, cmd->width, cmd->height );
	glScissor( 0, 0, cmd->width, cmd->height );

	if ( !rb_vrCleared[index] ) {
		// the virtual screen is a transparent layer over the world; an eye is opaque
		RB_VR_ClearBound( index == 0 ? 0.0f : 1.0f );
		rb_vrCleared[index] = true;
	}
}

static bool RB_VR_EnsureBlitFramebuffer( void ) {
	if ( rb_vrBlitFramebuffer != 0 && rb_vrBlitFramebufferGeneration == tr.glContextGeneration ) {
		return true;
	}
	rb_vrBlitFramebuffer = 0;
	glGenFramebuffers( 1, &rb_vrBlitFramebuffer );
	rb_vrBlitFramebufferGeneration = tr.glContextGeneration;
	return rb_vrBlitFramebuffer != 0;
}

// Copies a finished target into a swapchain image. The image belongs to the
// runtime: it is attached only for the copy, and framebuffer sRGB stays off so
// the gamma-encoded pixels arrive unchanged in an sRGB swapchain format.
static bool RB_VR_BlitToImage( idRenderTexture *source, GLuint image, int width, int height ) {
	if ( source == NULL || image == 0 || width <= 0 || height <= 0 || !RB_VR_EnsureBlitFramebuffer() ) {
		return false;
	}
	const GLuint sourceFramebuffer = source->GetDeviceHandle();
	if ( sourceFramebuffer == 0 ) {
		return false;
	}
	if ( glConfig.framebufferSRGBAvailable ) {
		glDisable( GL_FRAMEBUFFER_SRGB );
	}
	const GLboolean scissorWasEnabled = glIsEnabled( GL_SCISSOR_TEST );
	if ( scissorWasEnabled ) {
		glDisable( GL_SCISSOR_TEST );
	}

	glBindFramebuffer( GL_READ_FRAMEBUFFER, sourceFramebuffer );
	glReadBuffer( GL_COLOR_ATTACHMENT0 );
	glBindFramebuffer( GL_DRAW_FRAMEBUFFER, rb_vrBlitFramebuffer );
	glFramebufferTexture2D( GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, image, 0 );
	glDrawBuffer( GL_COLOR_ATTACHMENT0 );
	const GLenum status = glCheckFramebufferStatus( GL_DRAW_FRAMEBUFFER );
	const bool complete = status == GL_FRAMEBUFFER_COMPLETE;
	if ( complete ) {
		glBlitFramebuffer( 0, 0, width, height, 0, 0, width, height, GL_COLOR_BUFFER_BIT, GL_NEAREST );
	} else {
		static int reports = 0;
		if ( reports++ < 4 ) {
			common->Warning( "VR: swapchain image %u is not a complete color attachment (0x%04x)", image, status );
		}
	}
	glFramebufferTexture2D( GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0 );
	glBindFramebuffer( GL_FRAMEBUFFER, 0 );

	if ( scissorWasEnabled ) {
		glEnable( GL_SCISSOR_TEST );
	}
	return complete;
}

// Draws the left eye or the virtual screen into the window, cropped to the
// window's aspect so nothing is stretched.
static void RB_VR_DrawMirror( void ) {
	const int windowWidth = tr.vrWindowWidth;
	const int windowHeight = tr.vrWindowHeight;
	glBindFramebuffer( GL_FRAMEBUFFER, 0 );
	glDrawBuffer( GL_BACK );
	glReadBuffer( GL_BACK );
	glConfig.vidWidth = windowWidth;
	glConfig.vidHeight = windowHeight;
	if ( windowWidth <= 0 || windowHeight <= 0 ) {
		return;
	}
	glViewport( 0, 0, windowWidth, windowHeight );
	glScissor( 0, 0, windowWidth, windowHeight );
	RB_VR_ClearBound( 1.0f );

	idRenderTexture *source = NULL;
	if ( tr.vrFrame.mirror == RENDER_VR_MIRROR_EYE && tr.vrFrameResult.eyeRendered[0] && rb_vrCleared[1] ) {
		source = tr.vrTargets[1];
	} else if ( tr.vrFrame.mirror != RENDER_VR_MIRROR_NONE ) {
		source = tr.vrTargets[0];
	}
	if ( source == NULL || source->GetNumColorImages() <= 0 ) {
		return;
	}
	idImage *image = source->GetColorImage( 0 );
	const int sourceWidth = source->GetWidth();
	const int sourceHeight = source->GetHeight();
	if ( image == NULL || sourceWidth <= 0 || sourceHeight <= 0 ) {
		return;
	}

	// crop the source to the window aspect around its centre
	float s0 = 0.0f, t0 = 0.0f, s1 = 1.0f, t1 = 1.0f;
	const float windowAspect = static_cast<float>( windowWidth ) / static_cast<float>( windowHeight );
	const float sourceAspect = static_cast<float>( sourceWidth ) / static_cast<float>( sourceHeight );
	if ( sourceAspect > windowAspect ) {
		const float keep = windowAspect / sourceAspect;
		s0 = 0.5f - 0.5f * keep;
		s1 = 0.5f + 0.5f * keep;
	} else if ( sourceAspect < windowAspect ) {
		const float keep = sourceAspect / windowAspect;
		t0 = 0.5f - 0.5f * keep;
		t1 = 0.5f + 0.5f * keep;
	}
	RB_VR_DrawImageRect( image, 0, 0, windowWidth, windowHeight, s0, t0, s1, t1 );
}

/*
====================
RB_VR_PresentFrame

Runs in place of the window's back-buffer passes at swap time. False when
the frame was not a VR frame.
====================
*/
bool RB_VR_PresentFrame( void ) {
	if ( rb_vrTarget == -2 || !tr.vrFrame.active ) {
		return false;
	}

	// EndFrame returned the frame to the virtual screen; finish it like a back buffer
	if ( rb_vrTarget != -1 ) {
		RB_VR_FinishBoundTarget();
		R_SetDefaultRenderTarget( tr.vrTargets[0] );
		idRenderTexture::BindNull();
		R_SetDefaultDrawAndReadBuffers();
		glConfig.vidWidth = tr.vrFrame.screenWidth;
		glConfig.vidHeight = tr.vrFrame.screenHeight;
	}
	RB_VR_FinishBoundTarget();
	R_SetDefaultRenderTarget( NULL );

	RB_VR_BlitToImage( tr.vrTargets[0], tr.vrFrame.screenImage, tr.vrFrame.screenWidth, tr.vrFrame.screenHeight );
	for ( int eye = 0; eye < 2; eye++ ) {
		if ( tr.vrFrameResult.eyeRendered[eye] && rb_vrCleared[eye + 1] ) {
			if ( !RB_VR_BlitToImage( tr.vrTargets[eye + 1], tr.vrFrame.eyeImages[eye], tr.vrFrame.eyeWidth, tr.vrFrame.eyeHeight ) ) {
				tr.vrFrameResult.eyeRendered[eye] = false;
			}
		}
	}

	RB_VR_DrawMirror();
	rb_vrTarget = -2;
	backEnd.renderTexture = NULL;
	backEnd.feedbackRenderTexture = NULL;
	return true;
}

void R_VR_ShutdownTargets( void ) {
	if ( rb_vrBlitFramebuffer != 0 && rb_vrBlitFramebufferGeneration == tr.glContextGeneration && glDeleteFramebuffers != NULL ) {
		glDeleteFramebuffers( 1, &rb_vrBlitFramebuffer );
	}
	rb_vrBlitFramebuffer = 0;
	rb_vrBlitFramebufferGeneration = -1;
	R_SetDefaultRenderTarget( NULL );
	rb_vrTarget = -2;
}

#else // !OPENQ4_VR_GL_PRESENTATION

bool R_VR_BackendCanPresent( void ) {
	return false;
}

bool R_VR_QueryGraphicsBinding( renderVRGraphicsBinding_t &binding ) {
	memset( &binding, 0, sizeof( binding ) );
	return false;
}

void RB_VR_BeginBackEnd( void ) {
	R_SetDefaultRenderTarget( NULL );
}

void RB_VR_SetTarget( const void *data ) {
	(void)data;
}

bool RB_VR_PresentFrame( void ) {
	return false;
}

void R_VR_ShutdownTargets( void ) {
	R_SetDefaultRenderTarget( NULL );
}

#endif // OPENQ4_VR_GL_PRESENTATION
