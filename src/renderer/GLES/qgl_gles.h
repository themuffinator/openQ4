// Copyright (C) 2026 DarkMatter Productions
//
/*
===============================================================================

	OpenGL ES dispatch for the renderer-gles module.

	GLEW cannot be used here: its loader is desktop GL, resolving through
	WGL/GLX. This header replaces it for an ES 3.0 context, and the shape it
	chooses matters more than it looks.

	Three kinds of symbol, deliberately handled differently:

	1. ES 3.0 core comes from the Khronos headers as ordinary link-time
	   symbols, so they bind to libGLESv2. That is the arrangement
	   src/renderer/GLES/gles_clear_probe.cpp proved: openQ4 calls GL 1.x
	   entry points as plain externs, so the *only* way they reach an ES
	   driver is by linking that driver instead of desktop GL.

	2. Everything above ES 3.0 is a function pointer initialised to NULL,
	   optionally filled from eglGetProcAddress. This is load-bearing. The
	   renderer front-end guards every one of these with `!= NULL` checks
	   inherited from GLEW's dispatch -- ModernGLExecutor alone tests
	   glBindVertexBuffer, glVertexAttribFormat, glVertexAttribBinding,
	   glVertexBindingDivisor and glGetBufferSubData that way. Declaring them
	   as link-time symbols would satisfy the linker and silently arm code
	   paths ES cannot run; leaving them NULL keeps the existing fallbacks.

	3. The *ARB spellings the front-end still uses are aliased onto their ES
	   core names. ES has no ARB suffixes; the functions are the same.

	Fixed-function and ARB-assembly entry points are NOT here. They live in
	gles_GLStubs.cpp as no-ops, mirroring renderer/Vulkan/vk_GLStubs.cpp: a
	handful of mixed front-end translation units still carry those call sites
	(RenderSystem_init.cpp, tr_trace.cpp, RenderWorld_portals.cpp,
	GuiModel.cpp) and must link, but glConfig-driven guards keep them cold.

===============================================================================
*/

#ifndef __QGL_GLES_H__
#define __QGL_GLES_H__

#include <GLES3/gl3.h>
#include <GLES2/gl2ext.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
===============================================================================
	Desktop-GL types the ES headers do not define.

	The shared front-end is written against desktop GL signatures. These keep
	those declarations compiling; the ES shims below narrow to the ES types.
===============================================================================
*/

typedef double				GLdouble;
typedef double				GLclampd;
typedef unsigned int		GLhandleARB;
typedef char				GLcharARB;
typedef ptrdiff_t			GLsizeiptrARB;
typedef ptrdiff_t			GLintptrARB;

#ifndef GL_BGRA
#define GL_BGRA					0x80E1
#endif
#ifndef GL_BGR
#define GL_BGR					0x80E0
#endif

// KHR_debug is an extension in ES; the desktop spelling is what the front end uses.
#ifndef GLDEBUGPROC
typedef void ( GL_APIENTRY *GLDEBUGPROC )( GLenum source, GLenum type, GLuint id,
	GLenum severity, GLsizei length, const GLchar *message, const void *userParam );
#endif

// The front end declares its debug callbacks with the desktop calling-convention
// macros; ES spells the same thing GL_APIENTRY.
#ifndef GLAPIENTRY
#define GLAPIENTRY							GL_APIENTRY
#endif
#ifndef APIENTRY
#define APIENTRY							GL_APIENTRY
#endif

/*
===============================================================================
	GLEW extension-presence booleans.

	The front end tests GLEW's per-extension globals directly. On ES the
	answer is a compile-time constant, not a runtime query, because these are
	all either core (and so always present) or absent at any ES version.

	Anything mapped to 0 here is a feature whose fallback path the front end
	already has -- the same NULL-pointer discipline as the entry points above,
	expressed for extensions.
===============================================================================
*/

#define GLEW_OK								0

// Core in ES 3.0: framebuffer objects, MRT, sRGB textures, PBOs, blit.
#define GLEW_ARB_framebuffer_object			1
#define GLEW_EXT_framebuffer_object			1
#define GLEW_EXT_framebuffer_blit			1
#define GLEW_ARB_draw_buffers				1
#define GLEW_ARB_pixel_buffer_object		1
#define GLEW_EXT_pixel_buffer_object		1
#define GLEW_EXT_texture_sRGB				1

// Desktop version predicates: ES 3.0 is feature-equivalent to GL 3.0 for
// everything the front end gates on here. 3.2 stays off -- it guards
// geometry shaders and seamless cube maps, neither of which ES 3.0 has.
#define GLEW_VERSION_2_1					1
#define GLEW_VERSION_3_0					1
#define GLEW_VERSION_3_2					0

// Absent at every ES version.
#define GLEW_ARB_framebuffer_sRGB			0	// ES has no sRGB write-enable toggle
#define GLEW_EXT_framebuffer_sRGB			0
#define GLEW_ARB_seamless_cube_map			0	// ES cube maps are always seamless
#define GLEW_ARB_texture_cube_map_array		0	// ES 3.2 / EXT only
#define GLEW_ARB_texture_multisample		0	// ES 3.1
#define GLEW_EXT_direct_state_access		0	// no ES equivalent

/*
===============================================================================
	Enum compatibility.

	Half the dispatch surface is enums, not functions. Three kinds:

	1. ARB/EXT spellings of tokens that are core in ES under the plain name.
	2. Tokens ES only gained at 3.1. Defined by value rather than by including
	   GLES3/gl31.h, because that header also declares the ES 3.1 *functions*
	   as link-time symbols, which would defeat the NULL-pointer discipline
	   above and silently arm compute and indirect paths on a 3.0 driver.
	3. Desktop-only tokens with no ES equivalent at any version. They exist so
	   cold code compiles; nothing on ES reaches the call sites.
===============================================================================
*/

// --- 1. ARB/EXT spellings of ES core tokens ---
#define GL_ARRAY_BUFFER_ARB					GL_ARRAY_BUFFER
#define GL_ELEMENT_ARRAY_BUFFER_ARB			GL_ELEMENT_ARRAY_BUFFER
#define GL_STATIC_DRAW_ARB					GL_STATIC_DRAW
#define GL_STREAM_DRAW_ARB					GL_STREAM_DRAW
#define GL_STREAM_READ_ARB					GL_STREAM_READ
#define GL_PIXEL_PACK_BUFFER_ARB			GL_PIXEL_PACK_BUFFER
#define GL_TEXTURE0_ARB						GL_TEXTURE0
#define GL_MAX_DRAW_BUFFERS_ARB				GL_MAX_DRAW_BUFFERS
#define GL_MAX_TEXTURE_IMAGE_UNITS_ARB		GL_MAX_TEXTURE_IMAGE_UNITS
#define GL_INCR_WRAP_EXT					GL_INCR_WRAP
#define GL_DECR_WRAP_EXT					GL_DECR_WRAP
#define GL_TEXTURE_CUBE_MAP_EXT				GL_TEXTURE_CUBE_MAP
#define GL_TEXTURE_CUBE_MAP_POSITIVE_X_EXT	GL_TEXTURE_CUBE_MAP_POSITIVE_X
#define GL_READ_ONLY_ARB					0x88B8	// ES maps buffers by range instead
#define GL_DEBUG_OUTPUT_SYNCHRONOUS_ARB		GL_DEBUG_OUTPUT_SYNCHRONOUS

// --- 2. ES 3.1 tokens, by value (see note above about gl31.h) ---
#define GL_COMPUTE_SHADER					0x91B9
#define GL_SHADER_STORAGE_BUFFER			0x90D2
#define GL_SHADER_STORAGE_BLOCK				0x92E6
#define GL_SHADER_STORAGE_BUFFER_OFFSET_ALIGNMENT	0x90DF
#define GL_UNIFORM_BLOCK					0x92E2
#define GL_DRAW_INDIRECT_BUFFER				0x8F3F
#define GL_TEXTURE_BINDING_2D_MULTISAMPLE	0x9104
#define GL_IMAGE_2D							0x904D
#define GL_VERTEX_ATTRIB_BINDING			0x82D4
#define GL_BUFFER_UPDATE_BARRIER_BIT		0x00000200
#define GL_COMMAND_BARRIER_BIT				0x00000040
#define GL_FRAMEBUFFER_BARRIER_BIT			0x00000400
#define GL_SHADER_STORAGE_BARRIER_BIT		0x00002000
#define GL_TEXTURE_FETCH_BARRIER_BIT		0x00000008

// KHR_debug: gl2ext.h carries these with a _KHR suffix.
#define GL_DEBUG_OUTPUT						GL_DEBUG_OUTPUT_KHR
#define GL_DEBUG_OUTPUT_SYNCHRONOUS			GL_DEBUG_OUTPUT_SYNCHRONOUS_KHR
#define GL_DEBUG_SOURCE_API					GL_DEBUG_SOURCE_API_KHR
#define GL_DEBUG_SOURCE_WINDOW_SYSTEM		GL_DEBUG_SOURCE_WINDOW_SYSTEM_KHR
#define GL_DEBUG_SOURCE_SHADER_COMPILER		GL_DEBUG_SOURCE_SHADER_COMPILER_KHR
#define GL_DEBUG_SOURCE_THIRD_PARTY			GL_DEBUG_SOURCE_THIRD_PARTY_KHR
#define GL_DEBUG_SOURCE_APPLICATION			GL_DEBUG_SOURCE_APPLICATION_KHR
#define GL_DEBUG_SOURCE_OTHER				GL_DEBUG_SOURCE_OTHER_KHR
#define GL_DEBUG_TYPE_ERROR					GL_DEBUG_TYPE_ERROR_KHR
#define GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR	GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR_KHR
#define GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR	GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR_KHR
#define GL_DEBUG_TYPE_PORTABILITY			GL_DEBUG_TYPE_PORTABILITY_KHR
#define GL_DEBUG_TYPE_PERFORMANCE			GL_DEBUG_TYPE_PERFORMANCE_KHR
#define GL_DEBUG_TYPE_OTHER					GL_DEBUG_TYPE_OTHER_KHR
#define GL_DEBUG_TYPE_MARKER				GL_DEBUG_TYPE_MARKER_KHR
#define GL_DEBUG_TYPE_PUSH_GROUP			GL_DEBUG_TYPE_PUSH_GROUP_KHR
#define GL_DEBUG_TYPE_POP_GROUP				GL_DEBUG_TYPE_POP_GROUP_KHR
#define GL_DEBUG_SEVERITY_HIGH				GL_DEBUG_SEVERITY_HIGH_KHR
#define GL_DEBUG_SEVERITY_MEDIUM			GL_DEBUG_SEVERITY_MEDIUM_KHR
#define GL_DEBUG_SEVERITY_LOW				GL_DEBUG_SEVERITY_LOW_KHR
#define GL_DEBUG_SEVERITY_NOTIFICATION		GL_DEBUG_SEVERITY_NOTIFICATION_KHR
#define GL_MAX_LABEL_LENGTH					GL_MAX_LABEL_LENGTH_KHR
#define GL_BUFFER							GL_BUFFER_KHR
#define GL_PROGRAM							GL_PROGRAM_KHR
#define GL_STACK_OVERFLOW					GL_STACK_OVERFLOW_KHR
#define GL_STACK_UNDERFLOW					GL_STACK_UNDERFLOW_KHR

// EXT_buffer_storage / EXT_disjoint_timer_query
#define GL_MAP_PERSISTENT_BIT				0x0040
#define GL_MAP_COHERENT_BIT					0x0080
#define GL_DYNAMIC_STORAGE_BIT				0x0100
#define GL_TIME_ELAPSED						0x88BF

// --- 3. Desktop-only tokens: cold code only ---
// Fixed-function state reached by RB_SetDefaultGLState and the ARB2 probe.
#define GL_MODELVIEW						0x1700
#define GL_PROJECTION						0x1701
#define GL_S								0x2000
#define GL_T								0x2001
#define GL_R								0x2002
#define GL_Q								0x2003
#define GL_TEXTURE_ENV						0x2300
#define GL_TEXTURE_ENV_MODE					0x2200
#define GL_TEXTURE_GEN_MODE					0x2500
#define GL_OBJECT_LINEAR					0x2401
#define GL_ADD								0x0104
#define GL_DECAL							0x2101
#define GL_COMBINE_EXT						0x8570
#define GL_ALPHA_TEST						0x0BC0
#define GL_LIGHTING							0x0B50
#define GL_LINE								0x1B01
#define GL_LINE_STIPPLE						0x0B24
#define GL_COLOR_ARRAY						0x8076
#define GL_SMOOTH							0x1D01
#define GL_FILL								0x1B02
#define GL_POLYGON							0x0009
#define GL_VERTEX_ARRAY						0x8074
#define GL_TEXTURE_COORD_ARRAY				0x8078
#define GL_MODULATE							0x2100
#define GL_MAX_TEXTURE_UNITS				0x84E2
#define GL_MAX_TEXTURE_UNITS_ARB			0x84E2
#define GL_MAX_TEXTURE_COORDS				0x8871
#define GL_MAX_TEXTURE_COORDS_ARB			0x8871
#define GL_TEXTURE_1D						0x0DE0
#define GL_TEXTURE_1D_ARRAY					0x8C18
#define GL_TEXTURE_RECTANGLE				0x84F5
#define GL_TEXTURE_BUFFER					0x8C2A
#define GL_TEXTURE_CUBE_MAP_ARRAY			0x9009
#define GL_TEXTURE_CUBE_MAP_SEAMLESS		0x884F
#define GL_TEXTURE_BORDER_COLOR				0x1004
#define GL_TEXTURE_LOD_BIAS_EXT				0x8501
#define GL_CLAMP_TO_BORDER					0x812D
#define GL_ALPHA8							0x803C
#define GL_INTENSITY8						0x804B
#define GL_INTENSITY16						0x804D
#define GL_LUMINANCE8						0x8040
#define GL_LUMINANCE8_ALPHA8				0x8045
#define GL_LUMINANCE16_ALPHA16				0x8048
#define GL_DEPTH_COMPONENT32				0x81A7
#define GL_STENCIL_INDEX					0x1901
#define GL_COMPRESSED_RGBA_BPTC_UNORM		0x8E8C
#define GL_UNPACK_SWAP_BYTES				0x0CF0
#define GL_DRAW_BUFFER						0x0C01
#define GL_CONTEXT_PROFILE_MASK				0x9126
#define GL_CONTEXT_CORE_PROFILE_BIT			0x00000001
#define GL_CONTEXT_COMPATIBILITY_PROFILE_BIT	0x00000002
#define GL_CONTEXT_FLAGS					0x821E
#define GL_CONTEXT_FLAG_FORWARD_COMPATIBLE_BIT	0x00000001
#define GL_CONTEXT_FLAG_DEBUG_BIT			0x00000002
#define GL_FRAGMENT_PROGRAM_ARB				0x8804
#define GL_VERTEX_PROGRAM_ARB				0x8620
#define GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER		0x8CDB
#define GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER		0x8CDC
#define GL_FRAMEBUFFER_INCOMPLETE_LAYER_TARGETS		0x8DA8
#define GL_FRAMEBUFFER_INCOMPLETE_LAYER_COUNT_ARB	0x8DA9
#define GL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS_EXT	0x8CD9
#define GL_FRAMEBUFFER_INCOMPLETE_FORMATS_EXT		0x8CDA

/*
===============================================================================
	ARB spellings -> ES 3.0 core.

	Same functions, no suffix in ES.
===============================================================================
*/

#define glActiveTextureARB				glActiveTexture
#define glAttachObjectARB				glAttachShader
#define glBindAttribLocationARB			glBindAttribLocation
#define glBindBufferARB					glBindBuffer
#define glBufferDataARB					glBufferData
#define glBufferSubDataARB				glBufferSubData
#define glCompileShaderARB				glCompileShader
#define glCompressedTexImage2DARB		glCompressedTexImage2D
#define glCompressedTexSubImage2DARB	glCompressedTexSubImage2D
#define glCreateProgramObjectARB		glCreateProgram
#define glCreateShaderObjectARB			glCreateShader
#define glDeleteBuffersARB				glDeleteBuffers
#define glDetachObjectARB				glDetachShader
#define glDisableVertexAttribArrayARB	glDisableVertexAttribArray
#define glEnableVertexAttribArrayARB	glEnableVertexAttribArray
#define glGenBuffersARB					glGenBuffers
#define glGetUniformLocationARB			glGetUniformLocation
#define glLinkProgramARB				glLinkProgram
#define glShaderSourceARB				glShaderSource
#define glUniform1fARB					glUniform1f
#define glUniform1fvARB					glUniform1fv
#define glUniform1iARB					glUniform1i
#define glUniform2fvARB					glUniform2fv
#define glUniform3fvARB					glUniform3fv
#define glUniform4fvARB					glUniform4fv
#define glUniformMatrix4fvARB			glUniformMatrix4fv
#define glUnmapBufferARB				glUnmapBuffer
#define glUseProgramObjectARB			glUseProgram
#define glVertexAttribPointerARB		glVertexAttribPointer

/*
===============================================================================
	ES shims for desktop entry points that have an ES equivalent with a
	different name or signature.

	Implemented for real in gles_GLStubs.cpp rather than stubbed, and provided
	here rather than edited at ~30 call sites so the shared front-end stays
	identical between the GL and GLES modules.
===============================================================================
*/

void GLES_ClearDepth( GLclampd depth );					// -> glClearDepthf
void GLES_DepthRange( GLclampd zNear, GLclampd zFar );	// -> glDepthRangef
void GLES_DrawBuffer( GLenum buffer );					// -> glDrawBuffers( 1, &buffer )

#define glClearDepth					GLES_ClearDepth
#define glDepthRange					GLES_DepthRange
#define glDrawBuffer					GLES_DrawBuffer

// ES has no glGetTexImage at all. The light-image atlas
// (ModernLightImageAtlas.cpp) probes the entry point for NULL before its CPU
// readback path -- written for pointer-loaded GL, where a driver without the
// export presents exactly this way. A null pointer here lets that probe do the
// disabling; the call after it compiles as an (unreachable) indirect call.
typedef void ( *PFN_GLES_GetTexImage )( GLenum target, GLint level, GLenum format, GLenum type, void *pixels );
#define glGetTexImage					( ( PFN_GLES_GetTexImage )0 )

/*
===============================================================================
	ARB object-model calls with no 1:1 ES mapping.

	ES dropped the GLhandleARB object model: shaders and programs are distinct
	GLuint namespaces with separate query and delete entry points, so these
	cannot be aliased. Only the legacy GLSL bring-up path in
	RenderSystem_init.cpp still calls them and it never runs on ES; they exist
	to satisfy the linker.
===============================================================================
*/

void		GL_APIENTRY glDeleteObjectARB( GLhandleARB obj );
void *		GL_APIENTRY glMapBufferARB( GLenum target, GLenum access );

/*
===============================================================================
	Fixed-function entry points reached by mixed front-end translation units.

	tr_trace.cpp, RenderWorld_portals.cpp and GuiModel.cpp draw debug and
	portal visualisation through immediate mode. None of it executes on ES --
	the call sites sit behind r_showPortals / r_showTrace style guards -- but
	the module has to link. Defined as no-ops in gles_GLStubs.cpp.

	This is a much shorter list than the desktop GL 1.1 surface Vulkan had to
	stub, because the fixed-function-heavy translation units (draw_arb2,
	draw_common, tr_backend, tr_render, tr_rendertools) are excluded from the
	module outright.
===============================================================================
*/

void		GL_APIENTRY glBegin( GLenum mode );
void		GL_APIENTRY glEnd( void );
void		GL_APIENTRY glMatrixMode( GLenum mode );
void		GL_APIENTRY glLoadIdentity( void );
void		GL_APIENTRY glEnableClientState( GLenum array );
void		GL_APIENTRY glShadeModel( GLenum mode );
void		GL_APIENTRY glTexEnvi( GLenum target, GLenum pname, GLint param );
void		GL_APIENTRY glTexGenf( GLenum coord, GLenum pname, GLfloat param );
void		GL_APIENTRY glAlphaFunc( GLenum func, GLclampf ref );
// No ES equivalent at any version. A stub rather than a NULL pointer because
// nothing tests it for availability -- RB_SetDefaultGLState calls it directly.
// Wireframe (r_showTris) is unavailable on this backend.
void		GL_APIENTRY glPolygonMode( GLenum face, GLenum mode );
// Called unconditionally by GL_SelectTexture; needs a real signature.
void		GL_APIENTRY glClientActiveTextureARB( GLenum texture );
void		GL_APIENTRY glColor3f( GLfloat r, GLfloat g, GLfloat b );
void		GL_APIENTRY glColor4f( GLfloat r, GLfloat g, GLfloat b, GLfloat a );
void		GL_APIENTRY glVertex3f( GLfloat x, GLfloat y, GLfloat z );
void		GL_APIENTRY glVertex3fv( const GLfloat *v );
void		GL_APIENTRY glTexCoord2f( GLfloat s, GLfloat t );
void		GL_APIENTRY glLoadMatrixf( const GLfloat *m );
void		GL_APIENTRY glDisableClientState( GLenum array );
void		GL_APIENTRY glOrtho( GLdouble l, GLdouble r, GLdouble b, GLdouble t, GLdouble n, GLdouble f );

/*
===============================================================================
	Above ES 3.0: runtime-resolved, may be NULL.

	Every one of these is guarded by a `!= NULL` test in the front end. Keeping
	them NULL is what selects the ES-capable fallback path.

	GLES_ResolveOptionalEntryPoints() fills in whatever the driver actually
	exposes (ES 3.1+ or extensions); until it runs, everything is NULL and the
	front end behaves as though the feature is absent.
===============================================================================
*/

// GL 4.3 separate attribute format / ES 3.1 vertex attrib binding
typedef void ( GL_APIENTRY *PFN_glBindVertexBuffer )( GLuint bindingindex, GLuint buffer, GLintptr offset, GLsizei stride );
typedef void ( GL_APIENTRY *PFN_glVertexAttribFormat )( GLuint attribindex, GLint size, GLenum type, GLboolean normalized, GLuint relativeoffset );
typedef void ( GL_APIENTRY *PFN_glVertexAttribBinding )( GLuint attribindex, GLuint bindingindex );
typedef void ( GL_APIENTRY *PFN_glVertexBindingDivisor )( GLuint bindingindex, GLuint divisor );

// GL 4.3 / ES 3.1 compute
typedef void ( GL_APIENTRY *PFN_glDispatchCompute )( GLuint x, GLuint y, GLuint z );
typedef void ( GL_APIENTRY *PFN_glMemoryBarrier )( GLbitfield barriers );
typedef GLuint ( GL_APIENTRY *PFN_glGetProgramResourceIndex )( GLuint program, GLenum programInterface, const GLchar *name );

// GL 4.3 indirect
typedef void ( GL_APIENTRY *PFN_glMultiDrawElementsIndirect )( GLenum mode, GLenum type, const void *indirect, GLsizei drawcount, GLsizei stride );

// GL 4.4 multi-bind
typedef void ( GL_APIENTRY *PFN_glBindBuffersBase )( GLenum target, GLuint first, GLsizei count, const GLuint *buffers );
typedef void ( GL_APIENTRY *PFN_glBindSamplers )( GLuint first, GLsizei count, const GLuint *samplers );
typedef void ( GL_APIENTRY *PFN_glBindTextures )( GLuint first, GLsizei count, const GLuint *textures );

// GL 4.4 immutable storage
typedef void ( GL_APIENTRY *PFN_glBufferStorage )( GLenum target, GLsizeiptr size, const void *data, GLbitfield flags );

// GL 4.5 direct state access
typedef void ( GL_APIENTRY *PFN_glBindTextureUnit )( GLuint unit, GLuint texture );
typedef void ( GL_APIENTRY *PFN_glBindMultiTextureEXT )( GLenum texunit, GLenum target, GLuint texture );
typedef void ( GL_APIENTRY *PFN_glCreateBuffers )( GLsizei n, GLuint *buffers );
typedef void ( GL_APIENTRY *PFN_glCreateFramebuffers )( GLsizei n, GLuint *framebuffers );
typedef void ( GL_APIENTRY *PFN_glCreateSamplers )( GLsizei n, GLuint *samplers );
typedef void ( GL_APIENTRY *PFN_glCreateTextures )( GLenum target, GLsizei n, GLuint *textures );
typedef void ( GL_APIENTRY *PFN_glNamedBufferData )( GLuint buffer, GLsizeiptr size, const void *data, GLenum usage );
typedef void ( GL_APIENTRY *PFN_glNamedBufferSubData )( GLuint buffer, GLintptr offset, GLsizeiptr size, const void *data );
typedef void ( GL_APIENTRY *PFN_glNamedFramebufferDrawBuffer )( GLuint framebuffer, GLenum buf );
typedef void ( GL_APIENTRY *PFN_glNamedFramebufferDrawBuffers )( GLuint framebuffer, GLsizei n, const GLenum *bufs );
typedef void ( GL_APIENTRY *PFN_glNamedFramebufferReadBuffer )( GLuint framebuffer, GLenum src );
typedef void ( GL_APIENTRY *PFN_glNamedFramebufferTexture )( GLuint framebuffer, GLenum attachment, GLuint texture, GLint level );
typedef GLenum ( GL_APIENTRY *PFN_glCheckNamedFramebufferStatus )( GLuint framebuffer, GLenum target );
typedef void ( GL_APIENTRY *PFN_glTextureParameteri )( GLuint texture, GLenum pname, GLint param );
typedef void ( GL_APIENTRY *PFN_glTextureStorage2D )( GLuint texture, GLsizei levels, GLenum internalformat, GLsizei width, GLsizei height );
typedef void ( GL_APIENTRY *PFN_glTextureStorage2DMultisample )( GLuint texture, GLsizei samples, GLenum internalformat, GLsizei width, GLsizei height, GLboolean fixedsamplelocations );

// Multisample textures: ES 3.1, not ES 3.0
typedef void ( GL_APIENTRY *PFN_glTexImage2DMultisample )( GLenum target, GLsizei samples, GLenum internalformat, GLsizei width, GLsizei height, GLboolean fixedsamplelocations );

// Readback of buffer contents: no ES equivalent (map the range instead)
typedef void ( GL_APIENTRY *PFN_glGetBufferSubData )( GLenum target, GLintptr offset, GLsizeiptr size, void *data );

// Timer queries: EXT_disjoint_timer_query in ES
typedef void ( GL_APIENTRY *PFN_glGetQueryObjectiv )( GLuint id, GLenum pname, GLint *params );
typedef void ( GL_APIENTRY *PFN_glGetQueryObjectui64v )( GLuint id, GLenum pname, GLuint64 *params );

// KHR_debug
typedef void ( GL_APIENTRY *PFN_glDebugMessageCallback )( GLDEBUGPROC callback, const void *userParam );
typedef void ( GL_APIENTRY *PFN_glDebugMessageControl )( GLenum source, GLenum type, GLenum severity, GLsizei count, const GLuint *ids, GLboolean enabled );
typedef void ( GL_APIENTRY *PFN_glObjectLabel )( GLenum identifier, GLuint name, GLsizei length, const GLchar *label );
typedef void ( GL_APIENTRY *PFN_glPushDebugGroup )( GLenum source, GLuint id, GLsizei length, const GLchar *message );
typedef void ( GL_APIENTRY *PFN_glPopDebugGroup )( void );


extern PFN_glBindVertexBuffer				glBindVertexBuffer;
extern PFN_glVertexAttribFormat				glVertexAttribFormat;
extern PFN_glVertexAttribBinding			glVertexAttribBinding;
extern PFN_glVertexBindingDivisor			glVertexBindingDivisor;
extern PFN_glDispatchCompute				glDispatchCompute;
extern PFN_glMemoryBarrier					glMemoryBarrier;
extern PFN_glGetProgramResourceIndex		glGetProgramResourceIndex;
extern PFN_glMultiDrawElementsIndirect		glMultiDrawElementsIndirect;
extern PFN_glBindBuffersBase				glBindBuffersBase;
extern PFN_glBindSamplers					glBindSamplers;
extern PFN_glBindTextures					glBindTextures;
extern PFN_glBufferStorage					glBufferStorage;
extern PFN_glBindTextureUnit				glBindTextureUnit;
extern PFN_glBindMultiTextureEXT			glBindMultiTextureEXT;
extern PFN_glCreateBuffers					glCreateBuffers;
extern PFN_glCreateFramebuffers				glCreateFramebuffers;
extern PFN_glCreateSamplers					glCreateSamplers;
extern PFN_glCreateTextures					glCreateTextures;
extern PFN_glNamedBufferData				glNamedBufferData;
extern PFN_glNamedBufferSubData				glNamedBufferSubData;
extern PFN_glNamedFramebufferDrawBuffer		glNamedFramebufferDrawBuffer;
extern PFN_glNamedFramebufferDrawBuffers	glNamedFramebufferDrawBuffers;
extern PFN_glNamedFramebufferReadBuffer		glNamedFramebufferReadBuffer;
extern PFN_glNamedFramebufferTexture		glNamedFramebufferTexture;
extern PFN_glCheckNamedFramebufferStatus	glCheckNamedFramebufferStatus;
extern PFN_glTextureParameteri				glTextureParameteri;
extern PFN_glTextureStorage2D				glTextureStorage2D;
extern PFN_glTextureStorage2DMultisample	glTextureStorage2DMultisample;
extern PFN_glTexImage2DMultisample			glTexImage2DMultisample;
extern PFN_glGetBufferSubData				glGetBufferSubData;
extern PFN_glGetQueryObjectiv				glGetQueryObjectiv;
extern PFN_glGetQueryObjectui64v			glGetQueryObjectui64v;
extern PFN_glDebugMessageCallback			glDebugMessageCallback;
extern PFN_glDebugMessageControl			glDebugMessageControl;
extern PFN_glObjectLabel					glObjectLabel;
extern PFN_glPushDebugGroup					glPushDebugGroup;
extern PFN_glPopDebugGroup					glPopDebugGroup;

// The ARB spellings of the debug entry points alias the core ones; the front
// end uses both depending on how the extension was detected.
#define glDebugMessageCallbackARB			glDebugMessageCallback
#define glDebugMessageControlARB			glDebugMessageControl

/*
===============================================================================
	ARB-assembly, ATI and legacy EXT entry points: NULL pointers, never resolved.

	These MUST be pointers rather than no-op functions. RenderSystem_init.cpp
	decides whether a feature exists by testing the entry point against NULL
	(`glProgramStringARB != NULL`, `glMultiTexCoord2fARB != NULL`, ...). A
	no-op stub is a non-NULL address, so the probe would report the ARB2
	assembly path and the R200 fragment-shader path as *available* on an ES
	driver and then take them. Left NULL, every one of those probes correctly
	answers "absent" and the guarded call sites never execute.
===============================================================================
*/

typedef void ( GL_APIENTRY *PFN_glLegacyVoid )( void );

extern PFN_glLegacyVoid	glProgramStringARB;
extern PFN_glLegacyVoid	glBindProgramARB;
extern PFN_glLegacyVoid	glProgramEnvParameter4fvARB;
extern PFN_glLegacyVoid	glProgramLocalParameter4fvARB;
extern PFN_glLegacyVoid	glMultiTexCoord2fARB;
extern PFN_glLegacyVoid	glGetInfoLogARB;
extern PFN_glLegacyVoid	glGetObjectParameterivARB;
extern PFN_glLegacyVoid	glColorTableEXT;
extern PFN_glLegacyVoid	glActiveStencilFaceEXT;
extern PFN_glLegacyVoid	glStencilFuncSeparateATI;
extern PFN_glLegacyVoid	glStencilOpSeparateATI;
extern PFN_glLegacyVoid	glGenFragmentShadersATI;
extern PFN_glLegacyVoid	glBindFragmentShaderATI;
extern PFN_glLegacyVoid	glDeleteFragmentShaderATI;
extern PFN_glLegacyVoid	glBeginFragmentShaderATI;
extern PFN_glLegacyVoid	glEndFragmentShaderATI;
extern PFN_glLegacyVoid	glPassTexCoordATI;
extern PFN_glLegacyVoid	glSampleMapATI;
extern PFN_glLegacyVoid	glSetFragmentShaderConstantATI;
extern PFN_glLegacyVoid	glColorFragmentOp1ATI;
extern PFN_glLegacyVoid	glColorFragmentOp2ATI;
extern PFN_glLegacyVoid	glColorFragmentOp3ATI;
extern PFN_glLegacyVoid	glAlphaFragmentOp1ATI;
extern PFN_glLegacyVoid	glAlphaFragmentOp2ATI;
extern PFN_glLegacyVoid	glAlphaFragmentOp3ATI;

// Fills the pointers above from eglGetProcAddress. Safe to call more than once;
// anything the driver does not expose stays NULL.
void GLES_ResolveOptionalEntryPoints( void );

/*
===============================================================================
	GLEW initialisation shim.

	RenderSystem_init.cpp drives GL loading through glewInit(). Keeping that
	call site identical means the ES module resolves its optional entry points
	at exactly the same point in startup as the GL module resolves GLEW's.
===============================================================================
*/

extern GLboolean glewExperimental;
GLenum glewInit( void );
const GLubyte *glewGetErrorString( GLenum error );

#ifdef __ANDROID__
/*
===============================================================================
	Android touch-overlay state handoff.

	The host app draws its touch controls from inside SDL_GL_SwapWindow, i.e.
	between engine frames and behind the renderer's back. Defined in
	gles_Backend.cpp, called from GLimp_SwapBuffers once the swap returns.
===============================================================================
*/
void RB_GLES_RestoreStateAfterOverlay( void );
#endif

#ifdef __cplusplus
}
#endif

#endif /* __QGL_GLES_H__ */
