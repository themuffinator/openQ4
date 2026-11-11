// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#pragma once
#include "../DisplayPresentation.h"

// RenderSystem_init.cpp: this device's window was created to carry MSAA
// (r_glWindowMultisample, OpenGL ES). Otherwise it is single-sample and 3D
// views take their samples from render targets.
bool R_WindowCarriesMultisampling( void );
// gl_RenderTexture.cpp: the colour samples of the stand-in for the window, 0 for
// a single-sample one or none.
int R_DefaultRenderTargetSamples( void );

// Include after tr_local.h. Returns -1 when the context is unavailable. A
// render texture's samples belong to that image and must be queried separately.
inline int R_DefaultFramebufferSamples() {
	// A stand-in for the window (RenderTexture.h) carries its own count: a VR
	// frame's is single-sample, a window view's is multisampled.
	if ( R_DefaultFramebufferHandle() != 0 ) {
		return R_DefaultRenderTargetSamples();
	}
#if defined(USE_SDL3)
	renderDisplayPresentation_t display = {};
	R_GetDisplayPresentation( &display );
	return display.available && ( display.parametersValid & RDP_PARAMETER_SAMPLES ) ? display.samples : -1;
#else
	// Native comparison backends do not publish the SDL presentation POD. Read
	// the real default draw framebuffer without disturbing an active scene FBO.
	if ( !glConfig.isInitialized ) return -1;
	const bool splitBindings = GLEW_ARB_framebuffer_object || GLEW_VERSION_3_0;
	const bool extensionBinding = !splitBindings && GLEW_EXT_framebuffer_object;
	GLint previous = 0;
	if ( splitBindings ) {
		glGetIntegerv( GL_DRAW_FRAMEBUFFER_BINDING, &previous );
		glBindFramebuffer( GL_DRAW_FRAMEBUFFER, 0 );
	} else if ( extensionBinding ) {
		glGetIntegerv( GL_FRAMEBUFFER_BINDING_EXT, &previous );
		glBindFramebufferEXT( GL_FRAMEBUFFER_EXT, 0 );
	}
	GLint buffers = 0, samples = 0;
	glGetIntegerv( GL_SAMPLE_BUFFERS, &buffers );
	if ( buffers > 0 ) glGetIntegerv( GL_SAMPLES, &samples );
	if ( splitBindings ) glBindFramebuffer( GL_DRAW_FRAMEBUFFER, previous );
	else if ( extensionBinding ) glBindFramebufferEXT( GL_FRAMEBUFFER_EXT, previous );
	return buffers == 0 ? 0 : buffers > 0 && samples > 0 ? samples : -1;
#endif
}

// The samples a 3D view drawn "to the window" gets. Where the window carries
// MSAA that is the window's own count, which can differ from the setting after
// a strict device request or a driver fallback. A single-sample window leaves
// the count to r_multiSamples: such a view draws into a multisampled target
// (the renderer's scene target, or a window view's stand-in). A VR frame's
// stand-in stays single-sample.
inline int R_ViewFramebufferSamples() {
	const int window = R_DefaultFramebufferSamples();
	if ( window != 0 || R_DefaultFramebufferHandle() != 0 || R_WindowCarriesMultisampling() ) {
		return window;
	}
	return r_multiSamples.GetInteger() > 1 ? r_multiSamples.GetInteger() : 0;
}
