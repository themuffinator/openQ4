// Copyright (C) 2026 DarkMatter Productions
//
/*
===============================================================================

	OpenGL ES dispatch definitions for the renderer-gles module.

	Three things live here, all declared by qgl_gles.h:

	1. The optional entry-point pointers, defined NULL. Everything above ES
	   3.0 stays NULL unless the driver actually exposes it, which is what
	   keeps the front end's `!= NULL` fallbacks selecting ES-capable paths.

	2. Real ES shims for desktop calls with an ES equivalent under a different
	   name or signature (glClearDepth, glDepthRange, glDrawBuffer). These are
	   implementations, not stubs -- the shared front-end keeps calling the
	   desktop spelling and gets correct ES behaviour.

	3. No-op fixed-function entry points, so the mixed front-end translation
	   units link. Mirrors renderer/Vulkan/vk_GLStubs.cpp, but far shorter:
	   the fixed-function-heavy translation units are excluded from this
	   module rather than stubbed around.

===============================================================================
*/

#ifdef OPENQ4_RENDERER_GLES_MODULE

#include "../../idlib/precompiled.h"
#pragma hdrstop

#include "qgl_gles.h"

extern void *GLimp_ExtensionPointer( const char *name );

/*
===============================================================================
	Optional entry points, NULL until resolved.
===============================================================================
*/

PFN_glBindVertexBuffer				glBindVertexBuffer = NULL;
PFN_glVertexAttribFormat			glVertexAttribFormat = NULL;
PFN_glVertexAttribBinding			glVertexAttribBinding = NULL;
PFN_glVertexBindingDivisor			glVertexBindingDivisor = NULL;
PFN_glDispatchCompute				glDispatchCompute = NULL;
PFN_glMemoryBarrier					glMemoryBarrier = NULL;
PFN_glGetProgramResourceIndex		glGetProgramResourceIndex = NULL;
PFN_glMultiDrawElementsIndirect		glMultiDrawElementsIndirect = NULL;
PFN_glBindBuffersBase				glBindBuffersBase = NULL;
PFN_glBindSamplers					glBindSamplers = NULL;
PFN_glBindTextures					glBindTextures = NULL;
PFN_glBufferStorage					glBufferStorage = NULL;
PFN_glBindTextureUnit				glBindTextureUnit = NULL;
PFN_glBindMultiTextureEXT			glBindMultiTextureEXT = NULL;
PFN_glCreateBuffers					glCreateBuffers = NULL;
PFN_glCreateFramebuffers			glCreateFramebuffers = NULL;
PFN_glCreateSamplers				glCreateSamplers = NULL;
PFN_glCreateTextures				glCreateTextures = NULL;
PFN_glNamedBufferData				glNamedBufferData = NULL;
PFN_glNamedBufferSubData			glNamedBufferSubData = NULL;
PFN_glNamedFramebufferDrawBuffer	glNamedFramebufferDrawBuffer = NULL;
PFN_glNamedFramebufferDrawBuffers	glNamedFramebufferDrawBuffers = NULL;
PFN_glNamedFramebufferReadBuffer	glNamedFramebufferReadBuffer = NULL;
PFN_glNamedFramebufferTexture		glNamedFramebufferTexture = NULL;
PFN_glCheckNamedFramebufferStatus	glCheckNamedFramebufferStatus = NULL;
PFN_glTextureParameteri				glTextureParameteri = NULL;
PFN_glTextureStorage2D				glTextureStorage2D = NULL;
PFN_glTextureStorage2DMultisample	glTextureStorage2DMultisample = NULL;
PFN_glTexImage2DMultisample			glTexImage2DMultisample = NULL;
PFN_glGetBufferSubData				glGetBufferSubData = NULL;
PFN_glGetQueryObjectiv				glGetQueryObjectiv = NULL;
PFN_glGetQueryObjectui64v			glGetQueryObjectui64v = NULL;
PFN_glQueryCounter					glQueryCounter = NULL;
PFN_glDebugMessageCallback			glDebugMessageCallback = NULL;
PFN_glDebugMessageControl			glDebugMessageControl = NULL;
PFN_glObjectLabel					glObjectLabel = NULL;
PFN_glPushDebugGroup				glPushDebugGroup = NULL;
PFN_glPopDebugGroup					glPopDebugGroup = NULL;

// Resolve against the engine-owned SDL GL context. A process-wide dlsym can
// pick a different GL driver and does not expose EGL-only vendor extensions.
static void *GLES_LookupEntryPoint( const char *name ) {
	return GLimp_ExtensionPointer( name );
}

// glewInit runs before the shared capability probe has built its extension
// cache, so query this ES 3.0 context directly here.
static bool GLES_HasExtension( const char *name ) {
	GLint count = 0;
	glGetIntegerv( GL_NUM_EXTENSIONS, &count );
	for ( GLint i = 0; i < count; i++ ) {
		const GLubyte *extension = glGetStringi( GL_EXTENSIONS, i );
		if ( extension != NULL && strcmp( (const char *)extension, name ) == 0 ) {
			return true;
		}
	}
	return false;
}

void GLES_ResolveOptionalEntryPoints( void ) {
	GLint major = 0;
	GLint minor = 0;
	glGetIntegerv( GL_MAJOR_VERSION, &major );
	glGetIntegerv( GL_MINOR_VERSION, &minor );
	const bool es31 = major > 3 || ( major == 3 && minor >= 1 );
	const bool es32 = major > 3 || ( major == 3 && minor >= 2 );
	const bool bufferStorage = GLES_HasExtension( "GL_EXT_buffer_storage" );
	const bool multiDrawIndirect = GLES_HasExtension( "GL_EXT_multi_draw_indirect" );
	const bool timerQuery = GLES_HasExtension( "GL_EXT_disjoint_timer_query" );
	const bool debugOutput = GLES_HasExtension( "GL_KHR_debug" );

	// Driver exports are not capability evidence. Reset every optional pointer
	// on context recreation so a lower-version fallback cannot inherit the old
	// context's dispatch, even when both contexts use the same driver library.
	#define GLES_RESOLVE( supported, type, fn, symbol ) \
		fn = ( supported ) ? ( type )GLES_LookupEntryPoint( symbol ) : NULL;

	GLES_RESOLVE( es31, PFN_glBindVertexBuffer, glBindVertexBuffer, "glBindVertexBuffer" )
	GLES_RESOLVE( es31, PFN_glVertexAttribFormat, glVertexAttribFormat, "glVertexAttribFormat" )
	GLES_RESOLVE( es31, PFN_glVertexAttribBinding, glVertexAttribBinding, "glVertexAttribBinding" )
	GLES_RESOLVE( es31, PFN_glVertexBindingDivisor, glVertexBindingDivisor, "glVertexBindingDivisor" )
	GLES_RESOLVE( es31, PFN_glDispatchCompute, glDispatchCompute, "glDispatchCompute" )
	GLES_RESOLVE( es31, PFN_glMemoryBarrier, glMemoryBarrier, "glMemoryBarrier" )
	GLES_RESOLVE( es31, PFN_glGetProgramResourceIndex, glGetProgramResourceIndex, "glGetProgramResourceIndex" )

	GLES_RESOLVE( bufferStorage, PFN_glBufferStorage, glBufferStorage, "glBufferStorageEXT" )
	GLES_RESOLVE( multiDrawIndirect, PFN_glMultiDrawElementsIndirect, glMultiDrawElementsIndirect, "glMultiDrawElementsIndirectEXT" )
	GLES_RESOLVE( timerQuery, PFN_glGetQueryObjectiv, glGetQueryObjectiv, "glGetQueryObjectivEXT" )
	GLES_RESOLVE( timerQuery, PFN_glGetQueryObjectui64v, glGetQueryObjectui64v, "glGetQueryObjectui64vEXT" )
	GLES_RESOLVE( timerQuery, PFN_glQueryCounter, glQueryCounter, "glQueryCounterEXT" )

	GLES_RESOLVE( es32 || debugOutput, PFN_glDebugMessageCallback, glDebugMessageCallback, es32 ? "glDebugMessageCallback" : "glDebugMessageCallbackKHR" )
	GLES_RESOLVE( es32 || debugOutput, PFN_glDebugMessageControl, glDebugMessageControl, es32 ? "glDebugMessageControl" : "glDebugMessageControlKHR" )
	GLES_RESOLVE( es32 || debugOutput, PFN_glObjectLabel, glObjectLabel, es32 ? "glObjectLabel" : "glObjectLabelKHR" )
	GLES_RESOLVE( es32 || debugOutput, PFN_glPushDebugGroup, glPushDebugGroup, es32 ? "glPushDebugGroup" : "glPushDebugGroupKHR" )
	GLES_RESOLVE( es32 || debugOutput, PFN_glPopDebugGroup, glPopDebugGroup, es32 ? "glPopDebugGroup" : "glPopDebugGroupKHR" )

	#undef GLES_RESOLVE

	// DSA, multi-bind, desktop multisample allocation and glGetBufferSubData
	// have no ES 3.0 equivalents. Their pointers remain NULL.
}

/*
===============================================================================
	GLEW initialisation shim.

	glewInit() is where the GL module resolves its dispatch, so it is where the
	ES module resolves its optional entry points -- same call site, same point
	in startup, no change to RenderSystem_init.cpp.
===============================================================================
*/

GLboolean glewExperimental = GL_FALSE;

GLenum glewInit( void ) {
	GLES_ResolveOptionalEntryPoints();
	return GLEW_OK;
}

const GLubyte *glewGetErrorString( GLenum error ) {
	(void)error;
	return reinterpret_cast<const GLubyte *>( "no error" );
}

/*
===============================================================================
	ES shims.
===============================================================================
*/

void GLES_ClearDepth( GLclampd depth ) {
	glClearDepthf( static_cast<GLfloat>( depth ) );
}

void GLES_DepthRange( GLclampd zNear, GLclampd zFar ) {
	glDepthRangef( static_cast<GLfloat>( zNear ), static_cast<GLfloat>( zFar ) );
}

void GLES_DrawBuffer( GLenum buffer ) {
	// ES has no single-buffer glDrawBuffer. glDrawBuffers is core in ES 3.0 and
	// accepts GL_NONE and GL_BACK for the default framebuffer, and
	// GL_COLOR_ATTACHMENTi for an FBO, which covers every call site here.
	const GLenum buffers[1] = { buffer };
	glDrawBuffers( 1, buffers );
}

/*
===============================================================================
	ARB object-model calls with no ES mapping.

	Only the legacy GLSL bring-up path calls these, and it never runs on ES.
===============================================================================
*/

void GL_APIENTRY glDeleteObjectARB( GLhandleARB obj ) { (void)obj; }
void * GL_APIENTRY glMapBufferARB( GLenum target, GLenum access ) { (void)target; (void)access; return NULL; }

/*
===============================================================================
	Fixed-function no-ops.

	Debug and portal visualisation only; guarded off at every call site.
===============================================================================
*/

void GL_APIENTRY glBegin( GLenum mode ) { (void)mode; }
void GL_APIENTRY glEnd( void ) { }
void GL_APIENTRY glMatrixMode( GLenum mode ) { (void)mode; }
void GL_APIENTRY glLoadIdentity( void ) { }
void GL_APIENTRY glEnableClientState( GLenum array ) { (void)array; }
void GL_APIENTRY glShadeModel( GLenum mode ) { (void)mode; }
void GL_APIENTRY glTexEnvi( GLenum target, GLenum pname, GLint param ) { (void)target; (void)pname; (void)param; }
void GL_APIENTRY glTexGenf( GLenum coord, GLenum pname, GLfloat param ) { (void)coord; (void)pname; (void)param; }
void GL_APIENTRY glAlphaFunc( GLenum func, GLclampf ref ) { (void)func; (void)ref; }
void GL_APIENTRY glClientActiveTextureARB( GLenum texture ) { (void)texture; }
void GL_APIENTRY glPolygonMode( GLenum face, GLenum mode ) { (void)face; (void)mode; }

/*
===============================================================================
	ARB-assembly, ATI and legacy EXT entry points.

	Deliberately NULL and never resolved -- see the comment in qgl_gles.h.
	RenderSystem_init.cpp probes these against NULL to decide whether the ARB2
	assembly and R200 fragment-shader paths exist; a no-op stub would be a
	non-NULL address and would turn those probes into false positives.
===============================================================================
*/

PFN_glLegacyVoid	glProgramStringARB = NULL;
PFN_glLegacyVoid	glBindProgramARB = NULL;
PFN_glLegacyVoid	glProgramEnvParameter4fvARB = NULL;
PFN_glLegacyVoid	glProgramLocalParameter4fvARB = NULL;
PFN_glLegacyVoid	glMultiTexCoord2fARB = NULL;
PFN_glLegacyVoid	glGetInfoLogARB = NULL;
PFN_glLegacyVoid	glGetObjectParameterivARB = NULL;
PFN_glLegacyVoid	glColorTableEXT = NULL;
PFN_glLegacyVoid	glActiveStencilFaceEXT = NULL;
PFN_glLegacyVoid	glStencilFuncSeparateATI = NULL;
PFN_glLegacyVoid	glStencilOpSeparateATI = NULL;
PFN_glLegacyVoid	glGenFragmentShadersATI = NULL;
PFN_glLegacyVoid	glBindFragmentShaderATI = NULL;
PFN_glLegacyVoid	glDeleteFragmentShaderATI = NULL;
PFN_glLegacyVoid	glBeginFragmentShaderATI = NULL;
PFN_glLegacyVoid	glEndFragmentShaderATI = NULL;
PFN_glLegacyVoid	glPassTexCoordATI = NULL;
PFN_glLegacyVoid	glSampleMapATI = NULL;
PFN_glLegacyVoid	glSetFragmentShaderConstantATI = NULL;
PFN_glLegacyVoid	glColorFragmentOp1ATI = NULL;
PFN_glLegacyVoid	glColorFragmentOp2ATI = NULL;
PFN_glLegacyVoid	glColorFragmentOp3ATI = NULL;
PFN_glLegacyVoid	glAlphaFragmentOp1ATI = NULL;
PFN_glLegacyVoid	glAlphaFragmentOp2ATI = NULL;
PFN_glLegacyVoid	glAlphaFragmentOp3ATI = NULL;
void GL_APIENTRY glColor3f( GLfloat r, GLfloat g, GLfloat b ) { (void)r; (void)g; (void)b; }
void GL_APIENTRY glColor4f( GLfloat r, GLfloat g, GLfloat b, GLfloat a ) { (void)r; (void)g; (void)b; (void)a; }
void GL_APIENTRY glVertex3f( GLfloat x, GLfloat y, GLfloat z ) { (void)x; (void)y; (void)z; }
void GL_APIENTRY glVertex3fv( const GLfloat *v ) { (void)v; }
void GL_APIENTRY glTexCoord2f( GLfloat s, GLfloat t ) { (void)s; (void)t; }
void GL_APIENTRY glLoadMatrixf( const GLfloat *m ) { (void)m; }
void GL_APIENTRY glDisableClientState( GLenum array ) { (void)array; }
void GL_APIENTRY glOrtho( GLdouble l, GLdouble r, GLdouble b, GLdouble t, GLdouble n, GLdouble f ) {
	(void)l; (void)r; (void)b; (void)t; (void)n; (void)f;
}

// The underwater view is a GL back-end post-process pass built on an arbitrary
// GLSL program in the excluded draw_common.cpp. Same contract as the Vulkan
// module's stub (vk_GLStubs.cpp): report the effect unavailable and the game
// falls back to a flat wash.
bool RB_UnderwaterViewAvailable( void ) {
	return false;
}

// Ownership query for the modern light-grid pipeline, whose real answer lives
// in the excluded draw_common.cpp with a subtree of helpers behind it.
// Answering "not representable" keeps such surfaces on the per-surface
// fallback path, which is this module's standing contract for anything the
// standalone backend cannot own.
struct drawSurf_s;
bool RB_LightGridSurfaceModernRepresentable( const struct drawSurf_s *surf, const struct viewDef_s *viewDef, const char **reason ) {
	( void )surf; ( void )viewDef;
	if ( reason != NULL ) {
		*reason = "gles-module-no-lightgrid-classifier";
	}
	return false;
}

#endif /* OPENQ4_RENDERER_GLES_MODULE */
