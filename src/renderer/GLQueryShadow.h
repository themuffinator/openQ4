// Copyright (C) 2026 DarkMatter Productions
//

#ifndef __GLQUERYSHADOW_H__
#define __GLQUERYSHADOW_H__

/*
===============================================================================

	OpenGL state the renderer set, kept so that it need not be asked for

	A state query (glIsEnabled, glGetIntegerv, ...) is answered from the
	driver's own state. A driver that queues its commands for a worker
	thread (NVIDIA's threaded optimisation, Mesa's glthread) has to finish
	every command queued so far before it can answer, so each query makes
	the calling thread wait for the driver. The back end asked about
	fifty times a frame: to turn the scissor test off and back on around
	a clear, a resolve or a copy, to put the viewport back after a pass
	into another target, to restore blending and the stencil test after a
	shadow map.

	Every one of those values was set by the renderer itself. In the
	OpenGL renderer module the macros below replace the setters and the
	queries of a handful of states for every source file, which is every
	caller there is: the setters remember what they set, a pushed attribute
	group is remembered and comes back with its pop, and a query of a
	remembered value is answered without reaching the driver. A value
	nothing has set yet (a new context) is asked for once and remembered.

	Tracked: the enables GL_BLEND, GL_SCISSOR_TEST, GL_STENCIL_TEST,
	GL_CULL_FACE and GL_DEPTH_TEST, the scissor box, the viewport, the
	clear colour, and the array buffer binding the vertex cache already
	tracks. Everything else is passed through and counted.

	r_glStateQueries 0 asks the driver for everything, as before; 1
	answers what is remembered; 2 answers and asks, compares the two and
	reports a difference (qualification). r_glStateQueryReport N prints
	every N frames how many queries were answered and which call sites
	still reached the driver.

	Only the OpenGL module: the ES module resolves these names through its
	own table, the Vulkan module never calls them, and a renderer linked
	into the executable shares the context with tools that set state
	themselves.

===============================================================================
*/

#if defined( OPENQ4_RENDERER_GL_MODULE )

void		R_GLShadow_Enable( GLenum cap );
void		R_GLShadow_Disable( GLenum cap );
GLboolean	R_GLShadow_IsEnabled( GLenum cap, const char *file, int line );
void		R_GLShadow_Scissor( GLint x, GLint y, GLsizei width, GLsizei height );
void		R_GLShadow_Viewport( GLint x, GLint y, GLsizei width, GLsizei height );
void		R_GLShadow_ClearColor( GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha );
void		R_GLShadow_PushAttrib( GLbitfield mask );
void		R_GLShadow_PopAttrib( void );
void		R_GLShadow_GetIntegerv( GLenum pname, GLint *data, const char *file, int line );
void		R_GLShadow_GetFloatv( GLenum pname, GLfloat *data, const char *file, int line );
void		R_GLShadow_GetBooleanv( GLenum pname, GLboolean *data, const char *file, int line );
GLenum		R_GLShadow_GetError( const char *file, int line );

// nothing is remembered any more: state was set where the wrappers cannot see it
void		R_GLShadow_Forget( void );
// once a frame, for the report
void		R_GLShadow_EndFrame( void );

// The function-like forms leave the bare names alone, so the wrappers reach
// the real entry points as (glEnable)( cap ).
#undef glEnable
#define glEnable( cap )								R_GLShadow_Enable( cap )
#undef glDisable
#define glDisable( cap )							R_GLShadow_Disable( cap )
#undef glIsEnabled
#define glIsEnabled( cap )							R_GLShadow_IsEnabled( cap, __FILE__, __LINE__ )
#undef glScissor
#define glScissor( x, y, width, height )			R_GLShadow_Scissor( x, y, width, height )
#undef glViewport
#define glViewport( x, y, width, height )			R_GLShadow_Viewport( x, y, width, height )
#undef glClearColor
#define glClearColor( red, green, blue, alpha )		R_GLShadow_ClearColor( red, green, blue, alpha )
#undef glPushAttrib
#define glPushAttrib( mask )						R_GLShadow_PushAttrib( mask )
#undef glPopAttrib
#define glPopAttrib()								R_GLShadow_PopAttrib()
#undef glGetIntegerv
#define glGetIntegerv( pname, data )				R_GLShadow_GetIntegerv( pname, data, __FILE__, __LINE__ )
#undef glGetFloatv
#define glGetFloatv( pname, data )					R_GLShadow_GetFloatv( pname, data, __FILE__, __LINE__ )
#undef glGetBooleanv
#define glGetBooleanv( pname, data )				R_GLShadow_GetBooleanv( pname, data, __FILE__, __LINE__ )
#undef glGetError
#define glGetError()								R_GLShadow_GetError( __FILE__, __LINE__ )

#endif /* OPENQ4_RENDERER_GL_MODULE */

#endif /* !__GLQUERYSHADOW_H__ */
