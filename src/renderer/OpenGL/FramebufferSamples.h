// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#pragma once
#include "../DisplayPresentation.h"

// Include after tr_local.h. Returns -1 when the context is unavailable. A
// render texture's samples belong to that image and must be queried separately.
inline int R_DefaultFramebufferSamples() {
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
