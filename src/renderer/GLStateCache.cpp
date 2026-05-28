// Copyright (C) 2004 Id Software, Inc.
//

#include "tr_local.h"
#include "GLDebugScope.h"
#include "GLStateCache.h"

static idGLStateCache rg_glStateCache;

idGLStateCache::idGLStateCache() {
	memset( this, 0, sizeof( *this ) );
}

void idGLStateCache::ResetCachedState( void ) {
	memset( &program, 0, sizeof( program ) );
	memset( &vertexArray, 0, sizeof( vertexArray ) );
	memset( &arrayBuffer, 0, sizeof( arrayBuffer ) );
	memset( &elementArrayBuffer, 0, sizeof( elementArrayBuffer ) );
	memset( &uniformBuffer, 0, sizeof( uniformBuffer ) );
	memset( &shaderStorageBuffer, 0, sizeof( shaderStorageBuffer ) );
	memset( &drawIndirectBuffer, 0, sizeof( drawIndirectBuffer ) );
	memset( uniformBufferBase, 0, sizeof( uniformBufferBase ) );
	memset( shaderStorageBufferBase, 0, sizeof( shaderStorageBufferBase ) );
	memset( textures, 0, sizeof( textures ) );
	memset( samplers, 0, sizeof( samplers ) );
	memset( &framebuffer, 0, sizeof( framebuffer ) );
	memset( &readFramebuffer, 0, sizeof( readFramebuffer ) );
	memset( &drawFramebuffer, 0, sizeof( drawFramebuffer ) );
	memset( &blendEnabled, 0, sizeof( blendEnabled ) );
	memset( &blendSrcFactor, 0, sizeof( blendSrcFactor ) );
	memset( &blendDstFactor, 0, sizeof( blendDstFactor ) );
	memset( &depthTestEnabled, 0, sizeof( depthTestEnabled ) );
	memset( &depthFunc, 0, sizeof( depthFunc ) );
	memset( &depthMask, 0, sizeof( depthMask ) );
	memset( &stencilTestEnabled, 0, sizeof( stencilTestEnabled ) );
	memset( &stencilFunc, 0, sizeof( stencilFunc ) );
	stencilFuncValid = false;
	stencilRef = 0;
	stencilValueMask = 0;
	memset( &stencilFail, 0, sizeof( stencilFail ) );
	memset( &stencilZFail, 0, sizeof( stencilZFail ) );
	memset( &stencilZPass, 0, sizeof( stencilZPass ) );
	memset( &stencilWriteMask, 0, sizeof( stencilWriteMask ) );
	memset( &scissorTestEnabled, 0, sizeof( scissorTestEnabled ) );
	memset( &cullFaceEnabled, 0, sizeof( cullFaceEnabled ) );
	memset( &cullFaceMode, 0, sizeof( cullFaceMode ) );
	memset( &frontFaceMode, 0, sizeof( frontFaceMode ) );
	polygonModeValid = false;
	polygonModeFace = GL_FRONT_AND_BACK;
	polygonModeValue = GL_FILL;
	viewportValid = false;
	memset( viewport, 0, sizeof( viewport ) );
	scissorValid = false;
	memset( scissor, 0, sizeof( scissor ) );
	colorMaskValid = false;
	memset( colorMask, 0, sizeof( colorMask ) );
	activeTextureValid = false;
	activeTextureUnit = 0;
}

void idGLStateCache::Init( const renderBackendCaps_t &caps ) {
	Shutdown();
	initialized = caps.contextCreated;
	textureUnits = Max( 1, Min( GL_STATE_CACHE_MAX_TEXTURE_UNITS, caps.maxTextureImageUnits > 0 ? caps.maxTextureImageUnits : caps.maxTextureUnits ) );
	uniformBufferBindings = GL_STATE_CACHE_MAX_BUFFER_BINDINGS;
	shaderStorageBufferBindings = GL_STATE_CACHE_MAX_BUFFER_BINDINGS;
	BeginFrame();
	stats.initialized = initialized;
	stats.debugGroupsAvailable = R_GLDebugScope_Available();
	stats.objectLabelsAvailable = R_GLDebugObjectLabels_Available();
	stats.textureUnits = textureUnits;
	stats.uniformBufferBindings = uniformBufferBindings;
	stats.shaderStorageBufferBindings = shaderStorageBufferBindings;
	ResetCachedState();
	SetInvalidationReason( "init" );
}

void idGLStateCache::Shutdown( void ) {
	memset( &stats, 0, sizeof( stats ) );
	initialized = false;
	textureUnits = 0;
	uniformBufferBindings = 0;
	shaderStorageBufferBindings = 0;
	ResetCachedState();
}

void idGLStateCache::BeginFrame( void ) {
	const bool wasInitialized = initialized;
	const bool debugGroupsAvailable = R_GLDebugScope_Available();
	const bool objectLabelsAvailable = R_GLDebugObjectLabels_Available();
	const int previousTextureUnits = textureUnits;
	const int previousUniformBindings = uniformBufferBindings;
	const int previousSSBOBindings = shaderStorageBufferBindings;
	char previousReason[sizeof( stats.lastInvalidationReason )];
	idStr::Copynz( previousReason, stats.lastInvalidationReason, sizeof( previousReason ) );
	memset( &stats, 0, sizeof( stats ) );
	stats.initialized = wasInitialized;
	stats.debugGroupsAvailable = debugGroupsAvailable;
	stats.objectLabelsAvailable = objectLabelsAvailable;
	stats.textureUnits = previousTextureUnits;
	stats.uniformBufferBindings = previousUniformBindings;
	stats.shaderStorageBufferBindings = previousSSBOBindings;
	idStr::Copynz( stats.lastInvalidationReason, previousReason, sizeof( stats.lastInvalidationReason ) );
}

void idGLStateCache::SetInvalidationReason( const char *reason ) {
	idStr::Copynz( stats.lastInvalidationReason, reason != NULL ? reason : "unspecified", sizeof( stats.lastInvalidationReason ) );
}

void idGLStateCache::InvalidateAll( const char *reason ) {
	ResetCachedState();
	stats.forcedInvalidations++;
	SetInvalidationReason( reason );
}

void idGLStateCache::InvalidateBufferBinding( GLenum target, const char *reason ) {
	cachedGLuint_t *cached = BufferBindingForTarget( target );
	if ( cached == NULL ) {
		InvalidateAll( reason );
		return;
	}
	memset( cached, 0, sizeof( *cached ) );
	stats.forcedInvalidations++;
	SetInvalidationReason( reason );
}

void idGLStateCache::LegacyHandoffReset( const char *reason ) {
	InvalidateAll( reason != NULL ? reason : "legacy-handoff" );
	stats.legacyHandoffResets++;
}

void idGLStateCache::RecordHit( void ) {
	stats.hits++;
}

void idGLStateCache::RecordMiss( int &bucket ) {
	stats.misses++;
	bucket++;
}

int idGLStateCache::TextureUnitCount( void ) const {
	return Max( 1, textureUnits );
}

int idGLStateCache::UniformBindingCount( void ) const {
	return Max( 1, uniformBufferBindings );
}

int idGLStateCache::ShaderStorageBindingCount( void ) const {
	return Max( 1, shaderStorageBufferBindings );
}

int idGLStateCache::TextureTargetSlot( GLenum target ) const {
	switch ( target ) {
	case GL_TEXTURE_2D:
		return 0;
	case GL_TEXTURE_3D:
		return 1;
	case GL_TEXTURE_CUBE_MAP:
		return 2;
	default:
		return -1;
	}
}

idGLStateCache::cachedGLuint_t *idGLStateCache::BufferBindingForTarget( GLenum target ) {
	switch ( target ) {
	case GL_ARRAY_BUFFER:
		return &arrayBuffer;
	case GL_ELEMENT_ARRAY_BUFFER:
		return &elementArrayBuffer;
	case GL_UNIFORM_BUFFER:
		return &uniformBuffer;
	case GL_SHADER_STORAGE_BUFFER:
		return &shaderStorageBuffer;
	case GL_DRAW_INDIRECT_BUFFER:
		return &drawIndirectBuffer;
	default:
		return NULL;
	}
}

idGLStateCache::cachedBufferBaseBinding_t *idGLStateCache::BufferBaseBindingForTarget( GLenum target, GLuint index ) {
	switch ( target ) {
	case GL_UNIFORM_BUFFER:
		if ( index < static_cast<GLuint>( UniformBindingCount() ) ) {
			return &uniformBufferBase[index];
		}
		break;
	case GL_SHADER_STORAGE_BUFFER:
		if ( index < static_cast<GLuint>( ShaderStorageBindingCount() ) ) {
			return &shaderStorageBufferBase[index];
		}
		break;
	default:
		break;
	}
	return NULL;
}

int *idGLStateCache::BufferMissBucketForTarget( GLenum target ) {
	switch ( target ) {
	case GL_ARRAY_BUFFER:
	case GL_ELEMENT_ARRAY_BUFFER:
	case GL_UNIFORM_BUFFER:
	case GL_SHADER_STORAGE_BUFFER:
	case GL_DRAW_INDIRECT_BUFFER:
		return &stats.bufferMisses;
	default:
		return &stats.bufferMisses;
	}
}

bool idGLStateCache::UseProgram( GLuint newProgram ) {
	if ( glUseProgram == NULL ) {
		return false;
	}
	if ( program.valid && program.value == newProgram ) {
		RecordHit();
		return false;
	}
	glUseProgram( newProgram );
	program.valid = true;
	program.value = newProgram;
	RecordMiss( stats.programMisses );
	return true;
}

bool idGLStateCache::BindVertexArray( GLuint newVertexArray ) {
	if ( glBindVertexArray == NULL ) {
		return false;
	}
	if ( vertexArray.valid && vertexArray.value == newVertexArray ) {
		RecordHit();
		return false;
	}
	glBindVertexArray( newVertexArray );
	vertexArray.valid = true;
	vertexArray.value = newVertexArray;
	RecordMiss( stats.vertexArrayMisses );
	return true;
}

bool idGLStateCache::BindBuffer( GLenum target, GLuint buffer ) {
	if ( glBindBuffer == NULL ) {
		return false;
	}
	cachedGLuint_t *cached = BufferBindingForTarget( target );
	if ( cached != NULL && cached->valid && cached->value == buffer ) {
		RecordHit();
		return false;
	}
	glBindBuffer( target, buffer );
	if ( cached != NULL ) {
		cached->valid = true;
		cached->value = buffer;
	}
	RecordMiss( *BufferMissBucketForTarget( target ) );
	return true;
}

bool idGLStateCache::BindBufferBase( GLenum target, GLuint index, GLuint buffer ) {
	if ( glBindBufferBase == NULL ) {
		return false;
	}
	cachedBufferBaseBinding_t *cached = BufferBaseBindingForTarget( target, index );
	if ( cached != NULL && cached->valid && !cached->range && cached->buffer == buffer ) {
		RecordHit();
		return false;
	}
	glBindBufferBase( target, index, buffer );
	if ( cached != NULL ) {
		cached->valid = true;
		cached->buffer = buffer;
		cached->offset = 0;
		cached->size = 0;
		cached->range = false;
	}
	cachedGLuint_t *generic = BufferBindingForTarget( target );
	if ( generic != NULL ) {
		generic->valid = true;
		generic->value = buffer;
	}
	RecordMiss( *BufferMissBucketForTarget( target ) );
	return true;
}

bool idGLStateCache::BindBufferRange( GLenum target, GLuint index, GLuint buffer, GLintptr offset, GLsizeiptr size ) {
	if ( glBindBufferRange == NULL || size <= 0 ) {
		return false;
	}
	cachedBufferBaseBinding_t *cached = BufferBaseBindingForTarget( target, index );
	if ( cached != NULL && cached->valid && cached->range && cached->buffer == buffer && cached->offset == offset && cached->size == size ) {
		RecordHit();
		return false;
	}
	glBindBufferRange( target, index, buffer, offset, size );
	if ( cached != NULL ) {
		cached->valid = true;
		cached->buffer = buffer;
		cached->offset = offset;
		cached->size = size;
		cached->range = true;
	}
	cachedGLuint_t *generic = BufferBindingForTarget( target );
	if ( generic != NULL ) {
		generic->valid = true;
		generic->value = buffer;
	}
	RecordMiss( *BufferMissBucketForTarget( target ) );
	return true;
}

bool idGLStateCache::BindBuffersBase( GLenum target, GLuint first, GLsizei count, const GLuint *buffers ) {
	if ( count <= 0 || buffers == NULL ) {
		return false;
	}
	if ( glBindBuffersBase == NULL ) {
		bool issued = false;
		for ( GLsizei i = 0; i < count; ++i ) {
			issued = BindBufferBase( target, first + i, buffers[i] ) || issued;
		}
		return issued;
	}

	bool allCached = true;
	for ( GLsizei i = 0; i < count; ++i ) {
		cachedBufferBaseBinding_t *cached = BufferBaseBindingForTarget( target, first + i );
		if ( cached == NULL || !cached->valid || cached->range || cached->buffer != buffers[i] ) {
			allCached = false;
			break;
		}
	}
	if ( allCached ) {
		stats.hits += count;
		return false;
	}

	glBindBuffersBase( target, first, count, buffers );
	for ( GLsizei i = 0; i < count; ++i ) {
		cachedBufferBaseBinding_t *cached = BufferBaseBindingForTarget( target, first + i );
		if ( cached != NULL ) {
			if ( cached->valid && !cached->range && cached->buffer == buffers[i] ) {
				stats.hits++;
			} else {
				RecordMiss( *BufferMissBucketForTarget( target ) );
			}
			cached->valid = true;
			cached->buffer = buffers[i];
			cached->offset = 0;
			cached->size = 0;
			cached->range = false;
		} else {
			RecordMiss( *BufferMissBucketForTarget( target ) );
		}
	}
	return true;
}

bool idGLStateCache::ActiveTextureUnit( int unit ) {
	if ( unit < 0 || unit >= TextureUnitCount() || glActiveTextureARB == NULL ) {
		return false;
	}
	if ( activeTextureValid && activeTextureUnit == unit ) {
		RecordHit();
		return false;
	}
	glActiveTextureARB( GL_TEXTURE0_ARB + unit );
	activeTextureValid = true;
	activeTextureUnit = unit;
	RecordMiss( stats.textureMisses );
	return true;
}

bool idGLStateCache::BindTexture( int unit, GLenum target, GLuint texture ) {
	const int slot = TextureTargetSlot( target );
	if ( unit < 0 || unit >= TextureUnitCount() || slot < 0 ) {
		return false;
	}
	ActiveTextureUnit( unit );
	if ( textures[unit][slot].valid && textures[unit][slot].value == texture ) {
		RecordHit();
		return false;
	}
	glBindTexture( target, texture );
	textures[unit][slot].valid = true;
	textures[unit][slot].value = texture;
	RecordMiss( stats.textureMisses );
	return true;
}

bool idGLStateCache::BindSampler( int unit, GLuint sampler ) {
	if ( unit < 0 || unit >= TextureUnitCount() || glBindSampler == NULL ) {
		return false;
	}
	if ( samplers[unit].valid && samplers[unit].value == sampler ) {
		RecordHit();
		return false;
	}
	glBindSampler( unit, sampler );
	samplers[unit].valid = true;
	samplers[unit].value = sampler;
	RecordMiss( stats.samplerMisses );
	return true;
}

bool idGLStateCache::BindTextures( GLuint first, GLsizei count, const GLuint *textureNames ) {
	if ( count <= 0 || textureNames == NULL || first >= static_cast<GLuint>( TextureUnitCount() ) ) {
		return false;
	}
	const GLsizei clampedCount = Min( count, static_cast<GLsizei>( TextureUnitCount() - first ) );
	if ( clampedCount <= 0 ) {
		return false;
	}
	if ( glBindTextures == NULL ) {
		bool issued = false;
		for ( GLsizei i = 0; i < clampedCount; ++i ) {
			issued = BindTexture( static_cast<int>( first + i ), GL_TEXTURE_2D, textureNames[i] ) || issued;
		}
		return issued;
	}

	const int texture2DSlot = TextureTargetSlot( GL_TEXTURE_2D );
	bool allCached = texture2DSlot >= 0;
	for ( GLsizei i = 0; i < clampedCount && allCached; ++i ) {
		const int unit = static_cast<int>( first + i );
		if ( !textures[unit][texture2DSlot].valid || textures[unit][texture2DSlot].value != textureNames[i] ) {
			allCached = false;
		}
	}
	if ( allCached ) {
		stats.hits += clampedCount;
		return false;
	}

	glBindTextures( first, clampedCount, textureNames );
	stats.textureMultiBindBatches++;
	for ( GLsizei i = 0; i < clampedCount; ++i ) {
		const int unit = static_cast<int>( first + i );
		bool unitChanged = texture2DSlot < 0 || !textures[unit][texture2DSlot].valid || textures[unit][texture2DSlot].value != textureNames[i];
		memset( textures[unit], 0, sizeof( textures[unit] ) );
		if ( texture2DSlot >= 0 ) {
			textures[unit][texture2DSlot].valid = true;
			textures[unit][texture2DSlot].value = textureNames[i];
		}
		if ( unitChanged ) {
			RecordMiss( stats.textureMisses );
		} else {
			stats.hits++;
		}
	}
	return true;
}

bool idGLStateCache::BindSamplers( GLuint first, GLsizei count, const GLuint *samplerNames ) {
	if ( count <= 0 || samplerNames == NULL || first >= static_cast<GLuint>( TextureUnitCount() ) ) {
		return false;
	}
	const GLsizei clampedCount = Min( count, static_cast<GLsizei>( TextureUnitCount() - first ) );
	if ( clampedCount <= 0 ) {
		return false;
	}
	if ( glBindSamplers == NULL ) {
		bool issued = false;
		for ( GLsizei i = 0; i < clampedCount; ++i ) {
			issued = BindSampler( static_cast<int>( first + i ), samplerNames[i] ) || issued;
		}
		return issued;
	}

	bool allCached = true;
	for ( GLsizei i = 0; i < clampedCount; ++i ) {
		const int unit = static_cast<int>( first + i );
		if ( !samplers[unit].valid || samplers[unit].value != samplerNames[i] ) {
			allCached = false;
			break;
		}
	}
	if ( allCached ) {
		stats.hits += clampedCount;
		return false;
	}

	glBindSamplers( first, clampedCount, samplerNames );
	stats.samplerMultiBindBatches++;
	for ( GLsizei i = 0; i < clampedCount; ++i ) {
		const int unit = static_cast<int>( first + i );
		if ( !samplers[unit].valid || samplers[unit].value != samplerNames[i] ) {
			RecordMiss( stats.samplerMisses );
		} else {
			stats.hits++;
		}
		samplers[unit].valid = true;
		samplers[unit].value = samplerNames[i];
	}
	return true;
}

bool idGLStateCache::BindFramebuffer( GLenum target, GLuint newFramebuffer ) {
	if ( glBindFramebuffer == NULL ) {
		return false;
	}
	cachedGLuint_t *cached = NULL;
	if ( target == GL_FRAMEBUFFER ) {
		if ( framebuffer.valid && framebuffer.value == newFramebuffer && readFramebuffer.valid && readFramebuffer.value == newFramebuffer && drawFramebuffer.valid && drawFramebuffer.value == newFramebuffer ) {
			RecordHit();
			return false;
		}
		glBindFramebuffer( target, newFramebuffer );
		framebuffer.valid = true;
		framebuffer.value = newFramebuffer;
		readFramebuffer.valid = true;
		readFramebuffer.value = newFramebuffer;
		drawFramebuffer.valid = true;
		drawFramebuffer.value = newFramebuffer;
		RecordMiss( stats.framebufferMisses );
		return true;
	} else if ( target == GL_READ_FRAMEBUFFER ) {
		cached = &readFramebuffer;
	} else if ( target == GL_DRAW_FRAMEBUFFER ) {
		cached = &drawFramebuffer;
	}
	if ( cached != NULL && cached->valid && cached->value == newFramebuffer ) {
		RecordHit();
		return false;
	}
	glBindFramebuffer( target, newFramebuffer );
	if ( cached != NULL ) {
		cached->valid = true;
		cached->value = newFramebuffer;
	}
	framebuffer.valid = false;
	RecordMiss( stats.framebufferMisses );
	return true;
}

bool idGLStateCache::SetCapability( GLenum capability, bool enabled, cachedBool_t &cached, int &bucket ) {
	if ( cached.valid && cached.value == enabled ) {
		RecordHit();
		return false;
	}
	if ( enabled ) {
		glEnable( capability );
	} else {
		glDisable( capability );
	}
	cached.valid = true;
	cached.value = enabled;
	RecordMiss( bucket );
	return true;
}

bool idGLStateCache::SetBlendEnabled( bool enabled ) {
	return SetCapability( GL_BLEND, enabled, blendEnabled, stats.blendMisses );
}

bool idGLStateCache::SetBlendFunc( GLenum srcFactor, GLenum dstFactor ) {
	if ( blendSrcFactor.valid && blendDstFactor.valid && blendSrcFactor.value == srcFactor && blendDstFactor.value == dstFactor ) {
		RecordHit();
		return false;
	}
	glBlendFunc( srcFactor, dstFactor );
	blendSrcFactor.valid = true;
	blendSrcFactor.value = srcFactor;
	blendDstFactor.valid = true;
	blendDstFactor.value = dstFactor;
	RecordMiss( stats.blendMisses );
	return true;
}

bool idGLStateCache::SetDepthTestEnabled( bool enabled ) {
	return SetCapability( GL_DEPTH_TEST, enabled, depthTestEnabled, stats.depthMisses );
}

bool idGLStateCache::SetDepthFunc( GLenum func ) {
	if ( depthFunc.valid && depthFunc.value == func ) {
		RecordHit();
		return false;
	}
	glDepthFunc( func );
	depthFunc.valid = true;
	depthFunc.value = func;
	RecordMiss( stats.depthMisses );
	return true;
}

bool idGLStateCache::SetDepthMask( GLboolean mask ) {
	const bool enabled = mask == GL_TRUE;
	if ( depthMask.valid && depthMask.value == enabled ) {
		RecordHit();
		return false;
	}
	glDepthMask( mask );
	depthMask.valid = true;
	depthMask.value = enabled;
	RecordMiss( stats.depthMisses );
	return true;
}

bool idGLStateCache::SetStencilTestEnabled( bool enabled ) {
	return SetCapability( GL_STENCIL_TEST, enabled, stencilTestEnabled, stats.stencilMisses );
}

bool idGLStateCache::SetStencilFunc( GLenum func, GLint ref, GLuint mask ) {
	if ( stencilFuncValid && stencilFunc.value == func && stencilRef == ref && stencilValueMask == mask ) {
		RecordHit();
		return false;
	}
	glStencilFunc( func, ref, mask );
	stencilFunc.valid = true;
	stencilFunc.value = func;
	stencilFuncValid = true;
	stencilRef = ref;
	stencilValueMask = mask;
	RecordMiss( stats.stencilMisses );
	return true;
}

bool idGLStateCache::SetStencilOp( GLenum fail, GLenum zfail, GLenum zpass ) {
	if ( stencilFail.valid && stencilZFail.valid && stencilZPass.valid && stencilFail.value == fail && stencilZFail.value == zfail && stencilZPass.value == zpass ) {
		RecordHit();
		return false;
	}
	glStencilOp( fail, zfail, zpass );
	stencilFail.valid = true;
	stencilFail.value = fail;
	stencilZFail.valid = true;
	stencilZFail.value = zfail;
	stencilZPass.valid = true;
	stencilZPass.value = zpass;
	RecordMiss( stats.stencilMisses );
	return true;
}

bool idGLStateCache::SetStencilMask( GLuint mask ) {
	if ( stencilWriteMask.valid && stencilWriteMask.value == mask ) {
		RecordHit();
		return false;
	}
	glStencilMask( mask );
	stencilWriteMask.valid = true;
	stencilWriteMask.value = mask;
	RecordMiss( stats.stencilMisses );
	return true;
}

bool idGLStateCache::SetScissorTestEnabled( bool enabled ) {
	return SetCapability( GL_SCISSOR_TEST, enabled, scissorTestEnabled, stats.scissorMisses );
}

bool idGLStateCache::SetCullFaceEnabled( bool enabled ) {
	return SetCapability( GL_CULL_FACE, enabled, cullFaceEnabled, stats.rasterMisses );
}

bool idGLStateCache::SetCullFace( GLenum mode ) {
	if ( cullFaceMode.valid && cullFaceMode.value == mode ) {
		RecordHit();
		return false;
	}
	glCullFace( mode );
	cullFaceMode.valid = true;
	cullFaceMode.value = mode;
	RecordMiss( stats.rasterMisses );
	return true;
}

bool idGLStateCache::SetFrontFace( GLenum mode ) {
	if ( frontFaceMode.valid && frontFaceMode.value == mode ) {
		RecordHit();
		return false;
	}
	glFrontFace( mode );
	frontFaceMode.valid = true;
	frontFaceMode.value = mode;
	RecordMiss( stats.rasterMisses );
	return true;
}

bool idGLStateCache::SetPolygonMode( GLenum face, GLenum mode ) {
	if ( polygonModeValid && polygonModeFace == face && polygonModeValue == mode ) {
		RecordHit();
		return false;
	}
	glPolygonMode( face, mode );
	polygonModeValid = true;
	polygonModeFace = face;
	polygonModeValue = mode;
	RecordMiss( stats.rasterMisses );
	return true;
}

bool idGLStateCache::SetViewport( GLint x, GLint y, GLsizei width, GLsizei height ) {
	if ( viewportValid && viewport[0] == x && viewport[1] == y && viewport[2] == width && viewport[3] == height ) {
		RecordHit();
		return false;
	}
	glViewport( x, y, width, height );
	viewportValid = true;
	viewport[0] = x;
	viewport[1] = y;
	viewport[2] = width;
	viewport[3] = height;
	RecordMiss( stats.viewportMisses );
	return true;
}

bool idGLStateCache::SetScissor( GLint x, GLint y, GLsizei width, GLsizei height ) {
	if ( scissorValid && scissor[0] == x && scissor[1] == y && scissor[2] == width && scissor[3] == height ) {
		RecordHit();
		return false;
	}
	glScissor( x, y, width, height );
	scissorValid = true;
	scissor[0] = x;
	scissor[1] = y;
	scissor[2] = width;
	scissor[3] = height;
	RecordMiss( stats.scissorMisses );
	return true;
}

bool idGLStateCache::SetColorMask( GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha ) {
	if ( colorMaskValid && colorMask[0] == red && colorMask[1] == green && colorMask[2] == blue && colorMask[3] == alpha ) {
		RecordHit();
		return false;
	}
	glColorMask( red, green, blue, alpha );
	colorMaskValid = true;
	colorMask[0] = red;
	colorMask[1] = green;
	colorMask[2] = blue;
	colorMask[3] = alpha;
	RecordMiss( stats.colorMaskMisses );
	return true;
}

const glStateCacheStats_t &idGLStateCache::Stats( void ) const {
	return stats;
}

idGLStateCache &R_GLStateCache( void ) {
	return rg_glStateCache;
}

void R_GLStateCache_Init( const renderBackendCaps_t &caps ) {
	rg_glStateCache.Init( caps );
}

void R_GLStateCache_Shutdown( void ) {
	rg_glStateCache.Shutdown();
}

void R_GLStateCache_BeginFrame( void ) {
	rg_glStateCache.BeginFrame();
}

void R_GLStateCache_InvalidateAll( const char *reason ) {
	rg_glStateCache.InvalidateAll( reason );
}

void R_GLStateCache_InvalidateBufferBinding( GLenum target, const char *reason ) {
	rg_glStateCache.InvalidateBufferBinding( target, reason );
}

void R_GLStateCache_LegacyHandoffReset( const char *reason ) {
	rg_glStateCache.LegacyHandoffReset( reason );
}

const glStateCacheStats_t &R_GLStateCache_Stats( void ) {
	return rg_glStateCache.Stats();
}

void R_GLStateCache_PrintGfxInfo( void ) {
	const glStateCacheStats_t &stats = R_GLStateCache_Stats();
	common->Printf(
		"Modern GL state cache: init=%d debugGroups=%d labels=%d units=%d uboBindings=%d ssboBindings=%d hits=%d misses=%d textureMultiBind=%d samplerMultiBind=%d invalidations=%d legacyResets=%d last='%s'\n",
		stats.initialized ? 1 : 0,
		stats.debugGroupsAvailable ? 1 : 0,
		stats.objectLabelsAvailable ? 1 : 0,
		stats.textureUnits,
		stats.uniformBufferBindings,
		stats.shaderStorageBufferBindings,
		stats.hits,
		stats.misses,
		stats.textureMultiBindBatches,
		stats.samplerMultiBindBatches,
		stats.forcedInvalidations,
		stats.legacyHandoffResets,
		stats.lastInvalidationReason );
}

bool RendererGLStateCache_RunSelfTest( void ) {
	if ( !glConfig.backendCaps.contextCreated || !glConfig.renderFeatures.modernBaseline || glUseProgram == NULL || glBindBuffer == NULL ) {
		common->Printf( "RendererGLStateCache self-test skipped: modern GL entry points are unavailable\n" );
		return true;
	}

	idGLStateCache &cache = R_GLStateCache();
	cache.BeginFrame();
	cache.InvalidateAll( "self-test" );
	cache.UseProgram( 0 );
	cache.UseProgram( 0 );
	if ( glBindVertexArray != NULL ) {
		cache.BindVertexArray( 0 );
		cache.BindVertexArray( 0 );
	}
	cache.BindBuffer( GL_ARRAY_BUFFER, 0 );
	cache.BindBuffer( GL_ARRAY_BUFFER, 0 );
	{
		const glStateCacheStats_t beforeTargetInvalidate = cache.Stats();
		cache.InvalidateBufferBinding( GL_ARRAY_BUFFER, "self-test buffer target" );
		cache.BindBuffer( GL_ARRAY_BUFFER, 0 );
		if ( cache.Stats().misses <= beforeTargetInvalidate.misses ) {
			common->Printf( "RendererGLStateCache self-test failed: buffer-target invalidation did not force a cache miss\n" );
			return false;
		}
	}
	if ( glBindBufferBase != NULL ) {
		cache.BindBufferBase( GL_UNIFORM_BUFFER, 0, 0 );
		cache.BindBufferBase( GL_UNIFORM_BUFFER, 0, 0 );
	}
	if ( glBindBufferRange != NULL && glGenBuffers != NULL && glBufferData != NULL && glDeleteBuffers != NULL ) {
		GLuint rangeBuffer = 0;
		glGenBuffers( 1, &rangeBuffer );
		if ( rangeBuffer != 0 ) {
			cache.BindBuffer( GL_UNIFORM_BUFFER, rangeBuffer );
			glBufferData( GL_UNIFORM_BUFFER, 64, NULL, GL_STREAM_DRAW );
			cache.BindBufferRange( GL_UNIFORM_BUFFER, 0, rangeBuffer, 0, 16 );
			cache.BindBufferRange( GL_UNIFORM_BUFFER, 0, rangeBuffer, 0, 16 );
			cache.BindBufferBase( GL_UNIFORM_BUFFER, 0, 0 );
			cache.BindBuffer( GL_UNIFORM_BUFFER, 0 );
			glDeleteBuffers( 1, &rangeBuffer );
		}
	}
	cache.SetBlendEnabled( false );
	cache.SetBlendEnabled( false );
	cache.SetBlendFunc( GL_ONE, GL_ZERO );
	cache.SetBlendFunc( GL_ONE, GL_ZERO );
	cache.SetDepthTestEnabled( true );
	cache.SetDepthTestEnabled( true );
	cache.SetDepthFunc( GL_LEQUAL );
	cache.SetDepthFunc( GL_LEQUAL );
	cache.SetDepthMask( GL_TRUE );
	cache.SetDepthMask( GL_TRUE );
	cache.SetStencilTestEnabled( false );
	cache.SetStencilTestEnabled( false );
	cache.SetStencilFunc( GL_ALWAYS, 0, 0xff );
	cache.SetStencilFunc( GL_ALWAYS, 0, 0xff );
	cache.SetStencilOp( GL_KEEP, GL_KEEP, GL_KEEP );
	cache.SetStencilOp( GL_KEEP, GL_KEEP, GL_KEEP );
	cache.SetStencilMask( 0xff );
	cache.SetStencilMask( 0xff );
	cache.SetScissorTestEnabled( true );
	cache.SetScissorTestEnabled( true );
	cache.SetCullFaceEnabled( true );
	cache.SetCullFaceEnabled( true );
	cache.SetCullFace( GL_BACK );
	cache.SetCullFace( GL_BACK );
	cache.SetFrontFace( GL_CCW );
	cache.SetFrontFace( GL_CCW );
	cache.SetPolygonMode( GL_FRONT_AND_BACK, GL_FILL );
	cache.SetPolygonMode( GL_FRONT_AND_BACK, GL_FILL );
	cache.SetViewport( 0, 0, Max( 1, glConfig.vidWidth ), Max( 1, glConfig.vidHeight ) );
	cache.SetViewport( 0, 0, Max( 1, glConfig.vidWidth ), Max( 1, glConfig.vidHeight ) );
	cache.SetScissor( 0, 0, Max( 1, glConfig.vidWidth ), Max( 1, glConfig.vidHeight ) );
	cache.SetScissor( 0, 0, Max( 1, glConfig.vidWidth ), Max( 1, glConfig.vidHeight ) );
	cache.SetColorMask( GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE );
	cache.SetColorMask( GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE );
	cache.BindTexture( 0, GL_TEXTURE_2D, 0 );
	cache.BindTexture( 0, GL_TEXTURE_2D, 0 );
	if ( glBindSampler != NULL ) {
		cache.BindSampler( 0, 0 );
		cache.BindSampler( 0, 0 );
	}
	if ( glBindTextures != NULL ) {
		GLuint textureNames[2] = { 0, 0 };
		cache.BindTextures( 0, 2, textureNames );
		cache.BindTextures( 0, 2, textureNames );
	}
	if ( glBindSamplers != NULL ) {
		GLuint samplerNames[2] = { 0, 0 };
		cache.BindSamplers( 0, 2, samplerNames );
		cache.BindSamplers( 0, 2, samplerNames );
	}
	if ( glBindFramebuffer != NULL ) {
		cache.BindFramebuffer( GL_FRAMEBUFFER, 0 );
		cache.BindFramebuffer( GL_FRAMEBUFFER, 0 );
	}

	const glStateCacheStats_t beforeLegacy = cache.Stats();
	cache.LegacyHandoffReset( "self-test legacy handoff" );
	cache.UseProgram( 0 );
	const glStateCacheStats_t &afterLegacy = cache.Stats();

	if ( beforeLegacy.hits <= 0 || beforeLegacy.misses <= 0 ) {
		common->Printf( "RendererGLStateCache self-test failed: redundant state did not produce hits and misses\n" );
		return false;
	}
	if ( afterLegacy.legacyHandoffResets <= 0 || afterLegacy.misses <= beforeLegacy.misses ) {
		common->Printf( "RendererGLStateCache self-test failed: legacy handoff did not force a cache miss\n" );
		return false;
	}

	cache.InvalidateAll( "self-test complete" );
	GL_ClearStateDelta();
	common->Printf(
		"RendererGLStateCache self-test passed (hits=%d misses=%d textureMultiBind=%d samplerMultiBind=%d invalidations=%d legacyResets=%d)\n",
		afterLegacy.hits,
		afterLegacy.misses,
		beforeLegacy.textureMultiBindBatches,
		beforeLegacy.samplerMultiBindBatches,
		afterLegacy.forcedInvalidations,
		afterLegacy.legacyHandoffResets );
	return true;
}
