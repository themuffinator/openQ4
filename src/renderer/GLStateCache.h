// Copyright (C) 2004 Id Software, Inc.
//

#ifndef __GL_STATE_CACHE_H__
#define __GL_STATE_CACHE_H__

#include "RendererCaps.h"
#include "qgl.h"

const int GL_STATE_CACHE_MAX_TEXTURE_UNITS = 32;
const int GL_STATE_CACHE_MAX_BUFFER_BINDINGS = 32;

typedef struct glStateCacheStats_s {
	bool	initialized;
	bool	debugGroupsAvailable;
	bool	objectLabelsAvailable;
	int		textureUnits;
	int		uniformBufferBindings;
	int		shaderStorageBufferBindings;
	int		hits;
	int		misses;
	int		forcedInvalidations;
	int		legacyHandoffResets;
	int		programMisses;
	int		vertexArrayMisses;
	int		bufferMisses;
	int		textureMisses;
	int		samplerMisses;
	int		textureMultiBindBatches;
	int		samplerMultiBindBatches;
	int		framebufferMisses;
	int		blendMisses;
	int		depthMisses;
	int		stencilMisses;
	int		rasterMisses;
	int		viewportMisses;
	int		scissorMisses;
	int		colorMaskMisses;
	char	lastInvalidationReason[64];
} glStateCacheStats_t;

class idGLStateCache {
public:
	idGLStateCache();

	void Init( const renderBackendCaps_t &caps );
	void Shutdown( void );
	void BeginFrame( void );
	void InvalidateAll( const char *reason );
	void InvalidateBufferBinding( GLenum target, const char *reason );
	void LegacyHandoffReset( const char *reason );

	bool UseProgram( GLuint program );
	bool BindVertexArray( GLuint vertexArray );
	bool BindBuffer( GLenum target, GLuint buffer );
	bool BindBufferBase( GLenum target, GLuint index, GLuint buffer );
	bool BindBufferRange( GLenum target, GLuint index, GLuint buffer, GLintptr offset, GLsizeiptr size );
	bool BindBuffersBase( GLenum target, GLuint first, GLsizei count, const GLuint *buffers );
	bool ActiveTextureUnit( int unit );
	bool BindTexture( int unit, GLenum target, GLuint texture );
	bool BindSampler( int unit, GLuint sampler );
	bool BindTextures( GLuint first, GLsizei count, const GLuint *textureNames );
	bool BindSamplers( GLuint first, GLsizei count, const GLuint *samplerNames );
	bool BindFramebuffer( GLenum target, GLuint framebuffer );

	bool SetBlendEnabled( bool enabled );
	bool SetBlendFunc( GLenum srcFactor, GLenum dstFactor );
	bool SetDepthTestEnabled( bool enabled );
	bool SetDepthFunc( GLenum func );
	bool SetDepthMask( GLboolean mask );
	bool SetStencilTestEnabled( bool enabled );
	bool SetStencilFunc( GLenum func, GLint ref, GLuint mask );
	bool SetStencilOp( GLenum fail, GLenum zfail, GLenum zpass );
	bool SetStencilMask( GLuint mask );
	bool SetScissorTestEnabled( bool enabled );
	bool SetCullFaceEnabled( bool enabled );
	bool SetCullFace( GLenum mode );
	bool SetFrontFace( GLenum mode );
	bool SetPolygonMode( GLenum face, GLenum mode );
	bool SetViewport( GLint x, GLint y, GLsizei width, GLsizei height );
	bool SetScissor( GLint x, GLint y, GLsizei width, GLsizei height );
	bool SetColorMask( GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha );

	const glStateCacheStats_t &Stats( void ) const;

private:
	struct cachedGLuint_t {
		bool valid;
		GLuint value;
	};
	struct cachedGLenum_t {
		bool valid;
		GLenum value;
	};
	struct cachedBool_t {
		bool valid;
		bool value;
	};
	struct cachedTextureBinding_t {
		bool valid;
		GLuint value;
	};
	struct cachedBufferBaseBinding_t {
		bool valid;
		GLuint buffer;
		GLintptr offset;
		GLsizeiptr size;
		bool range;
	};

	void ResetCachedState( void );
	void RecordHit( void );
	void RecordMiss( int &bucket );
	void SetInvalidationReason( const char *reason );
	int TextureUnitCount( void ) const;
	int UniformBindingCount( void ) const;
	int ShaderStorageBindingCount( void ) const;
	int TextureTargetSlot( GLenum target ) const;
	cachedGLuint_t *BufferBindingForTarget( GLenum target );
	cachedBufferBaseBinding_t *BufferBaseBindingForTarget( GLenum target, GLuint index );
	int *BufferMissBucketForTarget( GLenum target );
	bool SetCapability( GLenum capability, bool enabled, cachedBool_t &cached, int &bucket );

	glStateCacheStats_t stats;
	bool initialized;
	int textureUnits;
	int uniformBufferBindings;
	int shaderStorageBufferBindings;

	cachedGLuint_t program;
	cachedGLuint_t vertexArray;
	cachedGLuint_t arrayBuffer;
	cachedGLuint_t elementArrayBuffer;
	cachedGLuint_t uniformBuffer;
	cachedGLuint_t shaderStorageBuffer;
	cachedGLuint_t drawIndirectBuffer;
	cachedBufferBaseBinding_t uniformBufferBase[GL_STATE_CACHE_MAX_BUFFER_BINDINGS];
	cachedBufferBaseBinding_t shaderStorageBufferBase[GL_STATE_CACHE_MAX_BUFFER_BINDINGS];
	cachedTextureBinding_t textures[GL_STATE_CACHE_MAX_TEXTURE_UNITS][3];
	cachedGLuint_t samplers[GL_STATE_CACHE_MAX_TEXTURE_UNITS];
	cachedGLuint_t framebuffer;
	cachedGLuint_t readFramebuffer;
	cachedGLuint_t drawFramebuffer;
	cachedBool_t blendEnabled;
	cachedGLenum_t blendSrcFactor;
	cachedGLenum_t blendDstFactor;
	cachedBool_t depthTestEnabled;
	cachedGLenum_t depthFunc;
	cachedBool_t depthMask;
	cachedBool_t stencilTestEnabled;
	cachedGLenum_t stencilFunc;
	bool stencilFuncValid;
	GLint stencilRef;
	GLuint stencilValueMask;
	cachedGLenum_t stencilFail;
	cachedGLenum_t stencilZFail;
	cachedGLenum_t stencilZPass;
	cachedGLuint_t stencilWriteMask;
	cachedBool_t scissorTestEnabled;
	cachedBool_t cullFaceEnabled;
	cachedGLenum_t cullFaceMode;
	cachedGLenum_t frontFaceMode;
	bool polygonModeValid;
	GLenum polygonModeFace;
	GLenum polygonModeValue;
	bool viewportValid;
	GLint viewport[4];
	bool scissorValid;
	GLint scissor[4];
	bool colorMaskValid;
	GLboolean colorMask[4];
	bool activeTextureValid;
	int activeTextureUnit;
};

idGLStateCache &R_GLStateCache( void );
void R_GLStateCache_Init( const renderBackendCaps_t &caps );
void R_GLStateCache_Shutdown( void );
void R_GLStateCache_BeginFrame( void );
void R_GLStateCache_InvalidateAll( const char *reason );
void R_GLStateCache_InvalidateBufferBinding( GLenum target, const char *reason );
void R_GLStateCache_LegacyHandoffReset( const char *reason );
const glStateCacheStats_t &R_GLStateCache_Stats( void );
void R_GLStateCache_PrintGfxInfo( void );
bool RendererGLStateCache_RunSelfTest( void );

#endif /* !__GL_STATE_CACHE_H__ */
