// Copyright (C) 2004 Id Software, Inc.
//

#include "tr_local.h"
#include "RenderGraphResources.h"
#include "GLDebugScope.h"
#include "GLStateCache.h"
#include "RendererBenchmarks.h"

typedef struct renderGraphPhysicalAllocation_s {
	bool					valid;
	bool					inUseThisFrame;
	int						id;
	renderGraphResourceType_t type;
	int						width;
	int						height;
	int						samples;
	GLenum					target;
	GLenum					internalFormat;
	GLenum					format;
	GLenum					dataType;
	GLenum					attachment;
	int						aliasGroup;
	int						firstPass;
	int						lastPass;
	GLuint					texture;
	GLuint					framebuffer;
	GLenum					framebufferStatus;
	bool					dsaTexture;
	bool					dsaFramebuffer;
	int						textureParameterUpdates;
	char					debugLabel[96];
} renderGraphPhysicalAllocation_t;

static renderGraphResourceManagerStats_t rg_renderGraphResourceStats;
static renderBackendCaps_t rg_renderGraphResourceCaps;
static renderFeatureSet_t rg_renderGraphResourceFeatures;
static renderGraphResourceHandle_t rg_renderGraphResourceHandles[RENDER_GRAPH_RESOURCE_MAX_HANDLES];
static renderGraphResourcePassRecord_t rg_renderGraphResourcePasses[RENDER_GRAPH_RESOURCE_MAX_PASS_RECORDS];
static renderGraphPhysicalAllocation_t rg_renderGraphPhysicalAllocations[RENDER_GRAPH_RESOURCE_MAX_PHYSICAL_ALLOCATIONS];
static int rg_renderGraphResourceHandleCount = 0;
static int rg_renderGraphResourcePassCount = 0;
static bool rg_renderGraphResourceInitialized = false;

static void R_RenderGraphResources_FormatDebugLabel( char *dest, int destSize, const char *fmt, ... ) {
	va_list argptr;
	va_start( argptr, fmt );
	idStr::vsnPrintf( dest, destSize, fmt, argptr );
	va_end( argptr );
}

static const char *R_RenderGraphResources_TypeName( renderGraphResourceType_t type ) {
	switch ( type ) {
	case RENDER_GRAPH_RESOURCE_COLOR:
		return "color";
	case RENDER_GRAPH_RESOURCE_DEPTH_STENCIL:
		return "depthStencil";
	case RENDER_GRAPH_RESOURCE_DEPTH:
		return "depth";
	case RENDER_GRAPH_RESOURCE_BUFFER:
		return "buffer";
	default:
		return "unknown";
	}
}

static const char *R_RenderGraphResources_FboStatusName( GLenum status ) {
	switch ( status ) {
	case GL_FRAMEBUFFER_COMPLETE:
		return "complete";
	case 0:
		return "none";
	case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT:
		return "incomplete_attachment";
	case GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT:
		return "missing_attachment";
	case GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER:
		return "incomplete_draw_buffer";
	case GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER:
		return "incomplete_read_buffer";
	case GL_FRAMEBUFFER_UNSUPPORTED:
		return "unsupported";
	case GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE:
		return "incomplete_multisample";
	default:
		return "0x%x";
	}
}

static void R_RenderGraphResources_SetStatus( const char *status ) {
	idStr::Copynz( rg_renderGraphResourceStats.lastFailure, status ? status : "", sizeof( rg_renderGraphResourceStats.lastFailure ) );
}

static unsigned int R_RenderGraphResources_HashStep( unsigned int hash, unsigned int value ) {
	hash ^= value & 0xff;
	hash *= 16777619u;
	hash ^= ( value >> 8 ) & 0xff;
	hash *= 16777619u;
	hash ^= ( value >> 16 ) & 0xff;
	hash *= 16777619u;
	hash ^= ( value >> 24 ) & 0xff;
	hash *= 16777619u;
	return hash;
}

static unsigned int R_RenderGraphResources_StableId( const char *name, renderGraphResourceType_t type ) {
	unsigned int hash = 2166136261u;
	if ( name != NULL ) {
		for ( const unsigned char *c = reinterpret_cast<const unsigned char *>( name ); *c != '\0'; ++c ) {
			hash ^= *c;
			hash *= 16777619u;
		}
	}
	hash = R_RenderGraphResources_HashStep( hash, static_cast<unsigned int>( type ) );
	if ( hash == 0 ) {
		hash = 1;
	}
	return hash;
}

static bool R_RenderGraphResources_IsTextureResource( renderGraphResourceType_t type ) {
	return type == RENDER_GRAPH_RESOURCE_COLOR
		|| type == RENDER_GRAPH_RESOURCE_DEPTH
		|| type == RENDER_GRAPH_RESOURCE_DEPTH_STENCIL;
}

static bool R_RenderGraphResources_IsShadowAtlasResource( const char *name ) {
	return name != NULL
		&& ( !idStr::Icmp( name, "shadowMap" )
			|| !idStr::Icmp( name, "projectedShadowAtlas" )
			|| !idStr::Icmp( name, "pointShadowAtlas" )
			|| !idStr::Icmp( name, "cascadeShadowAtlas" )
			|| !idStr::Icmp( name, "translucentShadowMoments" ) );
}

static int R_RenderGraphResources_ShadowResourceSize( void ) {
	const rendererBenchmarkBudget_t &budget = RendererBenchmarks_CurrentBudget();
	int size = idMath::ClampInt( 128, 4096, r_shadowMapSize.GetInteger() );
	size = Min( size, idMath::ClampInt( 128, 4096, budget.shadowMapSize ) );
	if ( rg_renderGraphResourceCaps.maxTextureSize > 0 ) {
		size = Min( size, rg_renderGraphResourceCaps.maxTextureSize );
	}
	return Max( 1, size );
}

static bool R_RenderGraphResources_FormatForType( const char *name, renderGraphResourceType_t type, GLenum &internalFormat, GLenum &format, GLenum &dataType, GLenum &attachment ) {
	switch ( type ) {
	case RENDER_GRAPH_RESOURCE_COLOR:
		if ( name != NULL && ( !idStr::Icmp( name, "gbufferEmissive" ) || !idStr::Icmp( name, "deferredLight" ) ) && ( rg_renderGraphResourceFeatures.modernGL41 || rg_renderGraphResourceFeatures.gpuDriven || rg_renderGraphResourceFeatures.lowOverhead ) ) {
			internalFormat = GL_RGBA16F;
			format = GL_RGBA;
			dataType = GL_HALF_FLOAT;
			attachment = GL_COLOR_ATTACHMENT0;
			return true;
		}
		if ( name != NULL && !idStr::Icmp( name, "translucentShadowMoments" ) && ( rg_renderGraphResourceFeatures.modernGL41 || rg_renderGraphResourceFeatures.gpuDriven || rg_renderGraphResourceFeatures.lowOverhead ) ) {
			internalFormat = GL_RGBA16F;
			format = GL_RGBA;
			dataType = GL_HALF_FLOAT;
			attachment = GL_COLOR_ATTACHMENT0;
			return true;
		}
		internalFormat = GL_RGBA8;
		format = GL_RGBA;
		dataType = GL_UNSIGNED_BYTE;
		attachment = GL_COLOR_ATTACHMENT0;
		return true;
	case RENDER_GRAPH_RESOURCE_DEPTH:
		internalFormat = GL_DEPTH_COMPONENT24;
		format = GL_DEPTH_COMPONENT;
		dataType = GL_UNSIGNED_INT;
		attachment = GL_DEPTH_ATTACHMENT;
		return true;
	case RENDER_GRAPH_RESOURCE_DEPTH_STENCIL:
		internalFormat = GL_DEPTH24_STENCIL8;
		format = GL_DEPTH_STENCIL;
		dataType = GL_UNSIGNED_INT_24_8;
		attachment = GL_DEPTH_STENCIL_ATTACHMENT;
		return true;
	case RENDER_GRAPH_RESOURCE_BUFFER:
		internalFormat = GL_NONE;
		format = GL_NONE;
		dataType = GL_NONE;
		attachment = GL_NONE;
		return true;
	default:
		internalFormat = GL_NONE;
		format = GL_NONE;
		dataType = GL_NONE;
		attachment = GL_NONE;
		return false;
	}
}

static bool R_RenderGraphResources_CanUseGLObjects( const renderBackendCaps_t &caps, const renderFeatureSet_t &features ) {
	if ( !features.renderGraph || !caps.hasFBO || caps.maxTextureSize <= 0 ) {
		return false;
	}
	if ( glGenTextures == NULL || glDeleteTextures == NULL || glBindTexture == NULL || glTexImage2D == NULL || glTexParameteri == NULL ) {
		return false;
	}
	if ( glGenFramebuffers == NULL || glDeleteFramebuffers == NULL || glBindFramebuffer == NULL || glFramebufferTexture2D == NULL || glCheckFramebufferStatus == NULL ) {
		return false;
	}
	return true;
}

static bool R_RenderGraphResources_CanUseLowOverheadObjects( void ) {
	if ( !rg_renderGraphResourceFeatures.lowOverhead || !rg_renderGraphResourceFeatures.directStateAccess || !rg_renderGraphResourceCaps.hasDSA ) {
		return false;
	}
	if ( !rg_renderGraphResourceStats.available ) {
		return false;
	}
	if ( glCreateTextures == NULL || glTextureStorage2D == NULL || glTextureParameteri == NULL ) {
		return false;
	}
	if ( glCreateFramebuffers == NULL || glNamedFramebufferTexture == NULL || glCheckNamedFramebufferStatus == NULL || glNamedFramebufferDrawBuffer == NULL || glNamedFramebufferReadBuffer == NULL ) {
		return false;
	}
	return true;
}

static int R_RenderGraphResources_FrameWidth( const renderGraphResource_t &resource ) {
	if ( R_RenderGraphResources_IsShadowAtlasResource( resource.name ) ) {
		return R_RenderGraphResources_ShadowResourceSize();
	}
	const int scale = Max( 1, resource.widthScale );
	return Max( 1, glConfig.vidWidth / scale );
}

static int R_RenderGraphResources_FrameHeight( const renderGraphResource_t &resource ) {
	if ( R_RenderGraphResources_IsShadowAtlasResource( resource.name ) ) {
		return R_RenderGraphResources_ShadowResourceSize();
	}
	const int scale = Max( 1, resource.heightScale );
	return Max( 1, glConfig.vidHeight / scale );
}

static bool R_RenderGraphResources_RangesOverlap( int firstA, int lastA, int firstB, int lastB ) {
	if ( firstA < 0 || lastA < 0 || firstB < 0 || lastB < 0 ) {
		return true;
	}
	return firstA <= lastB && firstB <= lastA;
}

static bool R_RenderGraphResources_CompatiblePhysicalSpec( const renderGraphPhysicalAllocation_t &allocation, const renderGraphResourceHandle_t &handle ) {
	return allocation.valid
		&& allocation.type == handle.type
		&& allocation.width == handle.width
		&& allocation.height == handle.height
		&& allocation.samples == handle.samples
		&& allocation.target == handle.target
		&& allocation.internalFormat == handle.internalFormat
		&& allocation.format == handle.format
		&& allocation.dataType == handle.dataType
		&& allocation.attachment == handle.attachment;
}

static void R_RenderGraphResources_UpdateAllocationLifetime( renderGraphPhysicalAllocation_t &allocation, const renderGraphResourceHandle_t &handle ) {
	if ( allocation.firstPass < 0 || ( handle.firstPass >= 0 && handle.firstPass < allocation.firstPass ) ) {
		allocation.firstPass = handle.firstPass;
	}
	if ( handle.lastPass > allocation.lastPass ) {
		allocation.lastPass = handle.lastPass;
	}
}

static bool R_RenderGraphResources_AllocationLifetimeOverlaps( int allocationId, int firstPass, int lastPass ) {
	for ( int i = 0; i < rg_renderGraphResourceHandleCount; ++i ) {
		const renderGraphResourceHandle_t &handle = rg_renderGraphResourceHandles[i];
		if ( handle.physicalAllocationId == allocationId && R_RenderGraphResources_RangesOverlap( handle.firstPass, handle.lastPass, firstPass, lastPass ) ) {
			return true;
		}
	}
	return false;
}

static void R_RenderGraphResources_ResetFrameRecords( const idRenderGraph &graph ) {
	rg_renderGraphResourceHandleCount = 0;
	rg_renderGraphResourcePassCount = 0;
	memset( rg_renderGraphResourceHandles, 0, sizeof( rg_renderGraphResourceHandles ) );
	memset( rg_renderGraphResourcePasses, 0, sizeof( rg_renderGraphResourcePasses ) );

	const bool initialized = rg_renderGraphResourceInitialized;
	const bool supported = rg_renderGraphResourceStats.supported;
	const bool available = rg_renderGraphResourceStats.available;
	memset( &rg_renderGraphResourceStats, 0, sizeof( rg_renderGraphResourceStats ) );
	rg_renderGraphResourceStats.initialized = initialized;
	rg_renderGraphResourceStats.supported = supported;
	rg_renderGraphResourceStats.available = available;
	rg_renderGraphResourceStats.lowOverheadReady = R_RenderGraphResources_CanUseLowOverheadObjects();
	rg_renderGraphResourceStats.width = glConfig.vidWidth;
	rg_renderGraphResourceStats.height = glConfig.vidHeight;
	rg_renderGraphResourceStats.graphPasses = graph.NumPasses();
	rg_renderGraphResourceStats.graphResources = graph.NumResources();
	R_RenderGraphResources_SetStatus( available ? "ready" : "unavailable" );

	for ( int i = 0; i < RENDER_GRAPH_RESOURCE_MAX_PHYSICAL_ALLOCATIONS; ++i ) {
		renderGraphPhysicalAllocation_t &allocation = rg_renderGraphPhysicalAllocations[i];
		allocation.inUseThisFrame = false;
		allocation.firstPass = -1;
		allocation.lastPass = -1;
		if ( allocation.valid ) {
			rg_renderGraphResourceStats.physicalAllocations++;
			if ( allocation.dsaTexture ) {
				rg_renderGraphResourceStats.dsaTextureAllocations++;
				rg_renderGraphResourceStats.dsaTextureParameterUpdates += allocation.textureParameterUpdates;
			} else {
				rg_renderGraphResourceStats.classicTextureAllocations++;
			}
			if ( allocation.dsaFramebuffer ) {
				rg_renderGraphResourceStats.dsaFramebufferAllocations++;
			} else {
				rg_renderGraphResourceStats.classicFramebufferAllocations++;
			}
		}
	}
}

static void R_RenderGraphResources_CopyPassRecords( const idRenderGraph &graph ) {
	const int passCount = Min( graph.NumPasses(), RENDER_GRAPH_RESOURCE_MAX_PASS_RECORDS );
	rg_renderGraphResourcePassCount = passCount;
	rg_renderGraphResourceStats.passRecords = passCount;
	if ( graph.NumPasses() > RENDER_GRAPH_RESOURCE_MAX_PASS_RECORDS ) {
		rg_renderGraphResourceStats.overflow = true;
	}
	for ( int i = 0; i < passCount; ++i ) {
		const renderGraphPass_t &pass = graph.Pass( i );
		renderGraphResourcePassRecord_t &record = rg_renderGraphResourcePasses[i];
		memset( &record, 0, sizeof( record ) );
		record.passIndex = i;
		record.category = pass.category;
		idStr::Copynz( record.name, pass.name ? pass.name : RenderPassCategory_Name( pass.category ), sizeof( record.name ) );
		record.resourceAccesses = pass.resourceAccessCount;
		record.readResources = pass.readResourceCount;
		record.writeResources = pass.writeResourceCount;
		record.clearOps = pass.clearCount;
		record.resolveOps = pass.resolveCount;
		record.invalidateOps = pass.invalidateCount;
		record.presentOps = pass.presentCount;
		record.enabled = pass.enabled;
		record.legacyWrapped = pass.legacyWrapped;
		record.packetBacked = pass.packetBacked;
		record.resourceBacked = pass.resourceBacked;
	}
}

static void R_RenderGraphResources_CountHandle( const renderGraphResourceHandle_t &handle ) {
	rg_renderGraphResourceStats.handles++;
	if ( handle.imported ) {
		rg_renderGraphResourceStats.importedHandles++;
	}
	if ( handle.transient ) {
		rg_renderGraphResourceStats.transientHandles++;
	}
	if ( ( handle.flags & RENDER_GRAPH_RESOURCE_HANDLE_TEXTURE ) != 0 ) {
		rg_renderGraphResourceStats.textureHandles++;
	}
	if ( ( handle.flags & RENDER_GRAPH_RESOURCE_HANDLE_BUFFER ) != 0 ) {
		rg_renderGraphResourceStats.bufferHandles++;
	}
	if ( handle.imported ) {
		rg_renderGraphResourceStats.skippedImported++;
	}
	if ( handle.type == RENDER_GRAPH_RESOURCE_BUFFER ) {
		rg_renderGraphResourceStats.skippedBuffers++;
	}
	if ( handle.aliasGroup > 0 ) {
		bool seen = false;
		for ( int i = 0; i < rg_renderGraphResourceHandleCount - 1; ++i ) {
			if ( rg_renderGraphResourceHandles[i].aliasGroup == handle.aliasGroup ) {
				seen = true;
				break;
			}
		}
		if ( !seen ) {
			rg_renderGraphResourceStats.aliasGroups++;
		}
	}
}

static renderGraphResourceHandle_t *R_RenderGraphResources_AddHandle( void ) {
	if ( rg_renderGraphResourceHandleCount >= RENDER_GRAPH_RESOURCE_MAX_HANDLES ) {
		rg_renderGraphResourceStats.overflow = true;
		R_RenderGraphResources_SetStatus( "handle overflow" );
		return NULL;
	}
	renderGraphResourceHandle_t &handle = rg_renderGraphResourceHandles[rg_renderGraphResourceHandleCount++];
	memset( &handle, 0, sizeof( handle ) );
	handle.graphResourceIndex = -1;
	handle.firstPass = -1;
	handle.lastPass = -1;
	handle.samples = 1;
	handle.framebufferStatus = 0;
	return &handle;
}

static void R_RenderGraphResources_FinalizeHandle( renderGraphResourceHandle_t &handle ) {
	if ( R_RenderGraphResources_IsTextureResource( handle.type ) ) {
		handle.flags |= RENDER_GRAPH_RESOURCE_HANDLE_TEXTURE;
	}
	if ( handle.type == RENDER_GRAPH_RESOURCE_BUFFER ) {
		handle.flags |= RENDER_GRAPH_RESOURCE_HANDLE_BUFFER;
	}
	if ( handle.imported ) {
		handle.flags |= RENDER_GRAPH_RESOURCE_HANDLE_IMPORTED;
	}
	if ( handle.transient ) {
		handle.flags |= RENDER_GRAPH_RESOURCE_HANDLE_TRANSIENT;
	}
	if ( handle.presentable ) {
		handle.flags |= RENDER_GRAPH_RESOURCE_HANDLE_PRESENTABLE;
	}
	if ( handle.framebufferComplete ) {
		handle.flags |= RENDER_GRAPH_RESOURCE_HANDLE_FBO_COMPLETE;
	}
	handle.stableId = R_RenderGraphResources_StableId( handle.name, handle.type );
	R_RenderGraphResources_CountHandle( handle );
}

static renderGraphResourceHandle_t *R_RenderGraphResources_AddImportedHandle( const char *name, renderGraphResourceType_t type, int width, int height, unsigned int extraFlags ) {
	renderGraphResourceHandle_t *handle = R_RenderGraphResources_AddHandle();
	if ( handle == NULL ) {
		return NULL;
	}
	idStr::Copynz( handle->name, name, sizeof( handle->name ) );
	R_RenderGraphResources_FormatDebugLabel( handle->debugLabel, sizeof( handle->debugLabel ), "OpenQ4 graph import: %s", name );
	handle->type = type;
	handle->width = width;
	handle->height = height;
	handle->samples = 1;
	handle->imported = true;
	handle->transient = false;
	handle->presentable = ( extraFlags & RENDER_GRAPH_RESOURCE_HANDLE_BACKBUFFER ) != 0;
	handle->allocated = false;
	handle->framebufferComplete = handle->presentable;
	handle->framebufferStatus = handle->presentable ? GL_FRAMEBUFFER_COMPLETE : 0;
	handle->flags |= extraFlags | RENDER_GRAPH_RESOURCE_HANDLE_IMPORTED;
	R_RenderGraphResources_FormatForType( name, type, handle->internalFormat, handle->format, handle->dataType, handle->attachment );
	R_RenderGraphResources_FinalizeHandle( *handle );
	return handle;
}

static bool R_RenderGraphResources_InitHandleFromGraph( const idRenderGraph &graph, int resourceIndex, renderGraphResourceHandle_t &handle ) {
	if ( resourceIndex < 0 || resourceIndex >= graph.NumResources() ) {
		return false;
	}
	const renderGraphResource_t &resource = graph.Resource( resourceIndex );
	idStr::Copynz( handle.name, resource.name ? resource.name : "<unnamed>", sizeof( handle.name ) );
	R_RenderGraphResources_FormatDebugLabel( handle.debugLabel, sizeof( handle.debugLabel ), "OpenQ4 graph: %s", handle.name );
	handle.type = resource.type;
	handle.graphResourceIndex = resourceIndex;
	handle.width = R_RenderGraphResources_IsTextureResource( resource.type ) ? R_RenderGraphResources_FrameWidth( resource ) : 0;
	handle.height = R_RenderGraphResources_IsTextureResource( resource.type ) ? R_RenderGraphResources_FrameHeight( resource ) : 0;
	handle.samples = Max( 1, resource.samples );
	handle.imported = resource.imported;
	handle.transient = resource.transient;
	handle.presentable = resource.presentable;
	handle.firstPass = resource.firstPass;
	handle.lastPass = resource.lastPass;
	handle.aliasGroup = resource.aliasGroup;
	if ( !R_RenderGraphResources_FormatForType( handle.name, resource.type, handle.internalFormat, handle.format, handle.dataType, handle.attachment ) ) {
		return false;
	}
	handle.target = ( handle.samples > 1 ) ? GL_TEXTURE_2D_MULTISAMPLE : GL_TEXTURE_2D;
	if ( handle.imported && handle.presentable && idStr::Icmp( handle.name, "backBuffer" ) == 0 ) {
		handle.framebuffer = 0;
		handle.framebufferStatus = GL_FRAMEBUFFER_COMPLETE;
		handle.framebufferComplete = true;
		handle.flags |= RENDER_GRAPH_RESOURCE_HANDLE_BACKBUFFER;
	}
	return true;
}

static int R_RenderGraphResources_FindReusableAllocation( const renderGraphResourceHandle_t &handle ) {
	for ( int i = 0; i < RENDER_GRAPH_RESOURCE_MAX_PHYSICAL_ALLOCATIONS; ++i ) {
		renderGraphPhysicalAllocation_t &allocation = rg_renderGraphPhysicalAllocations[i];
		if ( !allocation.inUseThisFrame && R_RenderGraphResources_CompatiblePhysicalSpec( allocation, handle ) ) {
			return i;
		}
	}
	return -1;
}

static int R_RenderGraphResources_FindAliasAllocation( const renderGraphResourceHandle_t &handle ) {
	if ( handle.aliasGroup <= 0 || handle.firstPass < 0 || handle.lastPass < 0 ) {
		return -1;
	}
	for ( int i = 0; i < RENDER_GRAPH_RESOURCE_MAX_PHYSICAL_ALLOCATIONS; ++i ) {
		renderGraphPhysicalAllocation_t &allocation = rg_renderGraphPhysicalAllocations[i];
		if ( !allocation.inUseThisFrame || !R_RenderGraphResources_CompatiblePhysicalSpec( allocation, handle ) || allocation.aliasGroup != handle.aliasGroup ) {
			continue;
		}
		if ( !R_RenderGraphResources_AllocationLifetimeOverlaps( allocation.id, handle.firstPass, handle.lastPass ) ) {
			return i;
		}
	}
	return -1;
}

static int R_RenderGraphResources_FindFreeAllocation( void ) {
	for ( int i = 0; i < RENDER_GRAPH_RESOURCE_MAX_PHYSICAL_ALLOCATIONS; ++i ) {
		if ( !rg_renderGraphPhysicalAllocations[i].valid ) {
			return i;
		}
	}
	return -1;
}

static bool R_RenderGraphResources_CreateTextureAndFramebufferDSA( renderGraphPhysicalAllocation_t &allocation, const renderGraphResourceHandle_t &handle ) {
	allocation.texture = 0;
	allocation.framebuffer = 0;
	allocation.framebufferStatus = 0;
	allocation.dsaTexture = false;
	allocation.dsaFramebuffer = false;
	allocation.textureParameterUpdates = 0;

	if ( handle.width <= 0 || handle.height <= 0 || handle.width > rg_renderGraphResourceCaps.maxTextureSize || handle.height > rg_renderGraphResourceCaps.maxTextureSize ) {
		R_RenderGraphResources_SetStatus( "texture dimensions unsupported" );
		return false;
	}
	if ( handle.samples > 1 && glTextureStorage2DMultisample == NULL ) {
		R_RenderGraphResources_SetStatus( "multisample texture DSA allocation unavailable" );
		return false;
	}

	glCreateTextures( handle.target, 1, &allocation.texture );
	if ( allocation.texture == 0 ) {
		R_RenderGraphResources_SetStatus( "glCreateTextures failed" );
		return false;
	}

	if ( handle.target == GL_TEXTURE_2D_MULTISAMPLE ) {
		glTextureStorage2DMultisample( allocation.texture, handle.samples, handle.internalFormat, handle.width, handle.height, GL_TRUE );
	} else {
		const GLint filter = ( handle.type == RENDER_GRAPH_RESOURCE_COLOR ) ? GL_LINEAR : GL_NEAREST;
		glTextureStorage2D( allocation.texture, 1, handle.internalFormat, handle.width, handle.height );
		glTextureParameteri( allocation.texture, GL_TEXTURE_MIN_FILTER, filter );
		glTextureParameteri( allocation.texture, GL_TEXTURE_MAG_FILTER, filter );
		glTextureParameteri( allocation.texture, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
		glTextureParameteri( allocation.texture, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
		allocation.textureParameterUpdates = 4;
	}

	glCreateFramebuffers( 1, &allocation.framebuffer );
	if ( allocation.framebuffer == 0 ) {
		glDeleteTextures( 1, &allocation.texture );
		allocation.texture = 0;
		R_RenderGraphResources_SetStatus( "glCreateFramebuffers failed" );
		return false;
	}

	glNamedFramebufferTexture( allocation.framebuffer, handle.attachment, allocation.texture, 0 );
	if ( handle.type == RENDER_GRAPH_RESOURCE_COLOR ) {
		glNamedFramebufferDrawBuffer( allocation.framebuffer, GL_COLOR_ATTACHMENT0 );
		glNamedFramebufferReadBuffer( allocation.framebuffer, GL_COLOR_ATTACHMENT0 );
	} else {
		glNamedFramebufferDrawBuffer( allocation.framebuffer, GL_NONE );
		glNamedFramebufferReadBuffer( allocation.framebuffer, GL_NONE );
	}
	allocation.framebufferStatus = glCheckNamedFramebufferStatus( allocation.framebuffer, GL_FRAMEBUFFER );

	R_GLDebug_LabelTexture( allocation.texture, handle.debugLabel );
	R_GLDebug_LabelFramebuffer( allocation.framebuffer, handle.debugLabel );

	if ( allocation.framebufferStatus != GL_FRAMEBUFFER_COMPLETE ) {
		R_RenderGraphResources_SetStatus( "DSA framebuffer incomplete" );
		return false;
	}

	allocation.dsaTexture = true;
	allocation.dsaFramebuffer = true;
	rg_renderGraphResourceStats.dsaTextureAllocations++;
	rg_renderGraphResourceStats.dsaTextureParameterUpdates += allocation.textureParameterUpdates;
	rg_renderGraphResourceStats.dsaFramebufferAllocations++;
	return true;
}

static bool R_RenderGraphResources_CreateTextureAndFramebufferClassic( renderGraphPhysicalAllocation_t &allocation, const renderGraphResourceHandle_t &handle ) {
	allocation.texture = 0;
	allocation.framebuffer = 0;
	allocation.framebufferStatus = 0;
	allocation.dsaTexture = false;
	allocation.dsaFramebuffer = false;
	allocation.textureParameterUpdates = 0;

	if ( handle.width <= 0 || handle.height <= 0 || handle.width > rg_renderGraphResourceCaps.maxTextureSize || handle.height > rg_renderGraphResourceCaps.maxTextureSize ) {
		R_RenderGraphResources_SetStatus( "texture dimensions unsupported" );
		return false;
	}
	if ( handle.samples > 1 && glTexImage2DMultisample == NULL ) {
		R_RenderGraphResources_SetStatus( "multisample texture allocation unavailable" );
		return false;
	}

	GLint previousTexture2D = 0;
	GLint previousTextureMSAA = 0;
	GLint previousReadFramebuffer = 0;
	GLint previousDrawFramebuffer = 0;
	GLint previousReadBuffer = GL_BACK;
	GLint previousDrawBuffer = GL_BACK;
	glGetIntegerv( GL_TEXTURE_BINDING_2D, &previousTexture2D );
	if ( handle.target == GL_TEXTURE_2D_MULTISAMPLE ) {
		glGetIntegerv( GL_TEXTURE_BINDING_2D_MULTISAMPLE, &previousTextureMSAA );
	}
	glGetIntegerv( GL_READ_FRAMEBUFFER_BINDING, &previousReadFramebuffer );
	glGetIntegerv( GL_DRAW_FRAMEBUFFER_BINDING, &previousDrawFramebuffer );
	glGetIntegerv( GL_READ_BUFFER, &previousReadBuffer );
	glGetIntegerv( GL_DRAW_BUFFER, &previousDrawBuffer );

	glGenTextures( 1, &allocation.texture );
	if ( allocation.texture == 0 ) {
		R_RenderGraphResources_SetStatus( "glGenTextures failed" );
		return false;
	}

	glBindTexture( handle.target, allocation.texture );
	if ( handle.target == GL_TEXTURE_2D_MULTISAMPLE ) {
		glTexImage2DMultisample( GL_TEXTURE_2D_MULTISAMPLE, handle.samples, handle.internalFormat, handle.width, handle.height, GL_TRUE );
	} else {
		glTexImage2D( GL_TEXTURE_2D, 0, handle.internalFormat, handle.width, handle.height, 0, handle.format, handle.dataType, NULL );
		const GLint filter = ( handle.type == RENDER_GRAPH_RESOURCE_COLOR ) ? GL_LINEAR : GL_NEAREST;
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
	}

	glGenFramebuffers( 1, &allocation.framebuffer );
	if ( allocation.framebuffer == 0 ) {
		glBindTexture( handle.target, handle.target == GL_TEXTURE_2D_MULTISAMPLE ? previousTextureMSAA : previousTexture2D );
		glDeleteTextures( 1, &allocation.texture );
		allocation.texture = 0;
		R_RenderGraphResources_SetStatus( "glGenFramebuffers failed" );
		return false;
	}

	glBindFramebuffer( GL_FRAMEBUFFER, allocation.framebuffer );
	glFramebufferTexture2D( GL_FRAMEBUFFER, handle.attachment, handle.target, allocation.texture, 0 );
	if ( handle.type == RENDER_GRAPH_RESOURCE_COLOR ) {
		glDrawBuffer( GL_COLOR_ATTACHMENT0 );
		glReadBuffer( GL_COLOR_ATTACHMENT0 );
	} else {
		glDrawBuffer( GL_NONE );
		glReadBuffer( GL_NONE );
	}
	allocation.framebufferStatus = glCheckFramebufferStatus( GL_FRAMEBUFFER );

	R_GLDebug_LabelTexture( allocation.texture, handle.debugLabel );
	R_GLDebug_LabelFramebuffer( allocation.framebuffer, handle.debugLabel );

	glBindTexture( GL_TEXTURE_2D, previousTexture2D );
	if ( handle.target == GL_TEXTURE_2D_MULTISAMPLE ) {
		glBindTexture( GL_TEXTURE_2D_MULTISAMPLE, previousTextureMSAA );
	}
	glBindFramebuffer( GL_READ_FRAMEBUFFER, previousReadFramebuffer );
	glBindFramebuffer( GL_DRAW_FRAMEBUFFER, previousDrawFramebuffer );
	glReadBuffer( previousReadBuffer );
	glDrawBuffer( previousDrawBuffer );

	R_GLStateCache_InvalidateAll( "render graph resource allocation" );

	if ( allocation.framebufferStatus != GL_FRAMEBUFFER_COMPLETE ) {
		R_RenderGraphResources_SetStatus( "framebuffer incomplete" );
		return false;
	}
	rg_renderGraphResourceStats.classicTextureAllocations++;
	rg_renderGraphResourceStats.classicFramebufferAllocations++;
	return true;
}

static bool R_RenderGraphResources_CreateTextureAndFramebuffer( renderGraphPhysicalAllocation_t &allocation, const renderGraphResourceHandle_t &handle ) {
	if ( R_RenderGraphResources_CanUseLowOverheadObjects() ) {
		return R_RenderGraphResources_CreateTextureAndFramebufferDSA( allocation, handle );
	}
	return R_RenderGraphResources_CreateTextureAndFramebufferClassic( allocation, handle );
}

static bool R_RenderGraphResources_AssignPhysicalAllocation( renderGraphResourceHandle_t &handle ) {
	if ( handle.imported || handle.type == RENDER_GRAPH_RESOURCE_BUFFER || !handle.transient ) {
		return true;
	}
	if ( !rg_renderGraphResourceStats.available ) {
		return false;
	}

	int allocationIndex = R_RenderGraphResources_FindAliasAllocation( handle );
	if ( allocationIndex >= 0 ) {
		rg_renderGraphResourceStats.aliasReusedPhysicalAllocations++;
		rg_renderGraphResourceStats.reusedPhysicalAllocations++;
	} else {
		allocationIndex = R_RenderGraphResources_FindReusableAllocation( handle );
		if ( allocationIndex >= 0 ) {
			rg_renderGraphResourceStats.reusedPhysicalAllocations++;
		}
	}

	if ( allocationIndex < 0 ) {
		allocationIndex = R_RenderGraphResources_FindFreeAllocation();
		if ( allocationIndex < 0 ) {
			rg_renderGraphResourceStats.overflow = true;
			R_RenderGraphResources_SetStatus( "physical allocation overflow" );
			return false;
		}
		renderGraphPhysicalAllocation_t &allocation = rg_renderGraphPhysicalAllocations[allocationIndex];
		memset( &allocation, 0, sizeof( allocation ) );
		allocation.valid = true;
		allocation.id = allocationIndex + 1;
		allocation.type = handle.type;
		allocation.width = handle.width;
		allocation.height = handle.height;
		allocation.samples = handle.samples;
		allocation.target = handle.target;
		allocation.internalFormat = handle.internalFormat;
		allocation.format = handle.format;
		allocation.dataType = handle.dataType;
		allocation.attachment = handle.attachment;
		allocation.aliasGroup = handle.aliasGroup;
		allocation.firstPass = -1;
		allocation.lastPass = -1;
		idStr::Copynz( allocation.debugLabel, handle.debugLabel, sizeof( allocation.debugLabel ) );
		if ( !R_RenderGraphResources_CreateTextureAndFramebuffer( allocation, handle ) ) {
			if ( allocation.framebuffer != 0 ) {
				glDeleteFramebuffers( 1, &allocation.framebuffer );
			}
			if ( allocation.texture != 0 ) {
				glDeleteTextures( 1, &allocation.texture );
			}
			memset( &allocation, 0, sizeof( allocation ) );
			return false;
		}
		rg_renderGraphResourceStats.newPhysicalAllocations++;
		rg_renderGraphResourceStats.physicalAllocations++;
	}

	renderGraphPhysicalAllocation_t &allocation = rg_renderGraphPhysicalAllocations[allocationIndex];
	allocation.inUseThisFrame = true;
	R_RenderGraphResources_UpdateAllocationLifetime( allocation, handle );
	handle.physicalAllocationId = allocation.id;
	handle.texture = allocation.texture;
	handle.framebuffer = allocation.framebuffer;
	handle.framebufferStatus = allocation.framebufferStatus;
	handle.allocated = allocation.texture != 0;
	handle.framebufferComplete = allocation.framebufferStatus == GL_FRAMEBUFFER_COMPLETE;
	if ( handle.framebufferComplete ) {
		handle.flags |= RENDER_GRAPH_RESOURCE_HANDLE_FBO_COMPLETE;
	}
	return handle.allocated && handle.framebufferComplete;
}

static void R_RenderGraphResources_ValidateHandleLifetime( const renderGraphResourceHandle_t &handle ) {
	if ( handle.graphResourceIndex < 0 || handle.imported ) {
		return;
	}
	if ( handle.firstPass < 0 || handle.lastPass < handle.firstPass ) {
		rg_renderGraphResourceStats.lifetimeValidationFailures++;
	}
}

static void R_RenderGraphResources_AddGraphHandles( const idRenderGraph &graph ) {
	for ( int i = 0; i < graph.NumResources(); ++i ) {
		renderGraphResourceHandle_t *handle = R_RenderGraphResources_AddHandle();
		if ( handle == NULL ) {
			return;
		}
		if ( !R_RenderGraphResources_InitHandleFromGraph( graph, i, *handle ) ) {
			rg_renderGraphResourceStats.lifetimeValidationFailures++;
			R_RenderGraphResources_SetStatus( "resource handle format failed" );
			R_RenderGraphResources_FinalizeHandle( *handle );
			continue;
		}
		R_RenderGraphResources_ValidateHandleLifetime( *handle );
		if ( rg_renderGraphResourceStats.available && !R_RenderGraphResources_AssignPhysicalAllocation( *handle ) && !handle->imported && handle->type != RENDER_GRAPH_RESOURCE_BUFFER ) {
			rg_renderGraphResourceStats.incompleteFramebuffers++;
		}
		R_RenderGraphResources_FinalizeHandle( *handle );
		if ( handle->framebuffer != 0 ) {
			rg_renderGraphResourceStats.framebufferCount++;
			if ( handle->framebufferComplete ) {
				rg_renderGraphResourceStats.completeFramebuffers++;
			} else {
				rg_renderGraphResourceStats.incompleteFramebuffers++;
			}
		}
	}
}

static void R_RenderGraphResources_AddLegacyImports( const idRenderGraph &graph ) {
	const int width = Max( 1, glConfig.vidWidth );
	const int height = Max( 1, glConfig.vidHeight );
	if ( graph.FindResource( "legacySceneColor" ) < 0 ) {
		R_RenderGraphResources_AddImportedHandle( "legacySceneColor", RENDER_GRAPH_RESOURCE_COLOR, width, height, RENDER_GRAPH_RESOURCE_HANDLE_LEGACY_IMPORT );
	}
	if ( graph.FindResource( "legacySceneDepth" ) < 0 ) {
		R_RenderGraphResources_AddImportedHandle( "legacySceneDepth", RENDER_GRAPH_RESOURCE_DEPTH_STENCIL, width, height, RENDER_GRAPH_RESOURCE_HANDLE_LEGACY_IMPORT );
	}
}

void R_RenderGraphResources_Init( const renderBackendCaps_t &caps, const renderFeatureSet_t &features ) {
	R_RenderGraphResources_Shutdown();

	memset( &rg_renderGraphResourceStats, 0, sizeof( rg_renderGraphResourceStats ) );
	memset( &rg_renderGraphResourceCaps, 0, sizeof( rg_renderGraphResourceCaps ) );
	memset( &rg_renderGraphResourceFeatures, 0, sizeof( rg_renderGraphResourceFeatures ) );
	memset( rg_renderGraphResourceHandles, 0, sizeof( rg_renderGraphResourceHandles ) );
	memset( rg_renderGraphResourcePasses, 0, sizeof( rg_renderGraphResourcePasses ) );
	memset( rg_renderGraphPhysicalAllocations, 0, sizeof( rg_renderGraphPhysicalAllocations ) );

	rg_renderGraphResourceCaps = caps;
	rg_renderGraphResourceFeatures = features;
	rg_renderGraphResourceInitialized = true;
	rg_renderGraphResourceStats.initialized = true;
	rg_renderGraphResourceStats.supported = features.renderGraph && caps.hasFBO;
	rg_renderGraphResourceStats.available = R_RenderGraphResources_CanUseGLObjects( caps, features );
	R_RenderGraphResources_SetStatus( rg_renderGraphResourceStats.available ? "ready" : "unsupported or missing GL FBO functions" );
}

void R_RenderGraphResources_Shutdown( void ) {
	if ( rg_renderGraphResourceInitialized ) {
		for ( int i = 0; i < RENDER_GRAPH_RESOURCE_MAX_PHYSICAL_ALLOCATIONS; ++i ) {
			renderGraphPhysicalAllocation_t &allocation = rg_renderGraphPhysicalAllocations[i];
			if ( allocation.framebuffer != 0 && glDeleteFramebuffers != NULL ) {
				glDeleteFramebuffers( 1, &allocation.framebuffer );
				rg_renderGraphResourceStats.releasedPhysicalAllocations++;
			}
			if ( allocation.texture != 0 && glDeleteTextures != NULL ) {
				glDeleteTextures( 1, &allocation.texture );
			}
		}
	}
	memset( rg_renderGraphPhysicalAllocations, 0, sizeof( rg_renderGraphPhysicalAllocations ) );
	rg_renderGraphResourceHandleCount = 0;
	rg_renderGraphResourcePassCount = 0;
	rg_renderGraphResourceInitialized = false;
}

void R_RenderGraphResources_PrepareFrame( const idRenderGraph &graph ) {
	idGLDebugScope scope( "RenderGraphResources::PrepareFrame" );
	R_RenderGraphResources_ResetFrameRecords( graph );
	R_RenderGraphResources_CopyPassRecords( graph );
	R_RenderGraphResources_AddLegacyImports( graph );

	if ( !rg_renderGraphResourceStats.available ) {
		R_RenderGraphResources_AddGraphHandles( graph );
		return;
	}

	R_RenderGraphResources_AddGraphHandles( graph );
	rg_renderGraphResourceStats.prepared =
		rg_renderGraphResourceStats.lifetimeValidationFailures == 0
		&& rg_renderGraphResourceStats.incompleteFramebuffers == 0
		&& !rg_renderGraphResourceStats.overflow;
	if ( rg_renderGraphResourceStats.prepared ) {
		R_RenderGraphResources_SetStatus( "ready" );
	}
}

const renderGraphResourceManagerStats_t &R_RenderGraphResources_Stats( void ) {
	return rg_renderGraphResourceStats;
}

const renderGraphResourceHandle_t *R_RenderGraphResources_FindHandle( const char *name ) {
	if ( name == NULL || name[0] == '\0' ) {
		return NULL;
	}
	for ( int i = 0; i < rg_renderGraphResourceHandleCount; ++i ) {
		if ( idStr::Icmp( rg_renderGraphResourceHandles[i].name, name ) == 0 ) {
			return &rg_renderGraphResourceHandles[i];
		}
	}
	return NULL;
}

const renderGraphResourceHandle_t *R_RenderGraphResources_HandleForGraphResource( int graphResourceIndex ) {
	for ( int i = 0; i < rg_renderGraphResourceHandleCount; ++i ) {
		if ( rg_renderGraphResourceHandles[i].graphResourceIndex == graphResourceIndex ) {
			return &rg_renderGraphResourceHandles[i];
		}
	}
	return NULL;
}

void R_RenderGraphResources_PrintGfxInfo( void ) {
	common->Printf(
		"Renderer graph resources: initialized=%d available=%d supported=%d lowOverhead=%d handles=%d imported=%d transient=%d textures=%d buffers=%d physical=%d dsa(tex=%d params=%d fbo=%d) classic(tex=%d fbo=%d) fbo=%d/%d status='%s'\n",
		rg_renderGraphResourceStats.initialized ? 1 : 0,
		rg_renderGraphResourceStats.available ? 1 : 0,
		rg_renderGraphResourceStats.supported ? 1 : 0,
		rg_renderGraphResourceStats.lowOverheadReady ? 1 : 0,
		rg_renderGraphResourceStats.handles,
		rg_renderGraphResourceStats.importedHandles,
		rg_renderGraphResourceStats.transientHandles,
		rg_renderGraphResourceStats.textureHandles,
		rg_renderGraphResourceStats.bufferHandles,
		rg_renderGraphResourceStats.physicalAllocations,
		rg_renderGraphResourceStats.dsaTextureAllocations,
		rg_renderGraphResourceStats.dsaTextureParameterUpdates,
		rg_renderGraphResourceStats.dsaFramebufferAllocations,
		rg_renderGraphResourceStats.classicTextureAllocations,
		rg_renderGraphResourceStats.classicFramebufferAllocations,
		rg_renderGraphResourceStats.completeFramebuffers,
		rg_renderGraphResourceStats.framebufferCount,
		rg_renderGraphResourceStats.lastFailure );
}

void R_RenderGraphResources_DumpLatest( void ) {
	common->Printf(
		"RenderGraphResource dump: prepared=%d lowOverhead=%d handles=%d graphResources=%d passes=%d physical=%d new=%d reused=%d aliasReused=%d dsa(tex=%d params=%d fbo=%d) classic(tex=%d fbo=%d) fbo=%d/%d failures=%d overflow=%d status='%s'\n",
		rg_renderGraphResourceStats.prepared ? 1 : 0,
		rg_renderGraphResourceStats.lowOverheadReady ? 1 : 0,
		rg_renderGraphResourceStats.handles,
		rg_renderGraphResourceStats.graphResources,
		rg_renderGraphResourceStats.passRecords,
		rg_renderGraphResourceStats.physicalAllocations,
		rg_renderGraphResourceStats.newPhysicalAllocations,
		rg_renderGraphResourceStats.reusedPhysicalAllocations,
		rg_renderGraphResourceStats.aliasReusedPhysicalAllocations,
		rg_renderGraphResourceStats.dsaTextureAllocations,
		rg_renderGraphResourceStats.dsaTextureParameterUpdates,
		rg_renderGraphResourceStats.dsaFramebufferAllocations,
		rg_renderGraphResourceStats.classicTextureAllocations,
		rg_renderGraphResourceStats.classicFramebufferAllocations,
		rg_renderGraphResourceStats.completeFramebuffers,
		rg_renderGraphResourceStats.framebufferCount,
		rg_renderGraphResourceStats.lifetimeValidationFailures,
		rg_renderGraphResourceStats.overflow ? 1 : 0,
		rg_renderGraphResourceStats.lastFailure );

	for ( int i = 0; i < rg_renderGraphResourcePassCount; ++i ) {
		const renderGraphResourcePassRecord_t &pass = rg_renderGraphResourcePasses[i];
		common->Printf(
			"  pass[%d] %s category=%s accesses=%d read=%d write=%d clear=%d resolve=%d invalidate=%d present=%d enabled=%d legacy=%d packet=%d resource=%d\n",
			pass.passIndex,
			pass.name,
			RenderPassCategory_Name( pass.category ),
			pass.resourceAccesses,
			pass.readResources,
			pass.writeResources,
			pass.clearOps,
			pass.resolveOps,
			pass.invalidateOps,
			pass.presentOps,
			pass.enabled ? 1 : 0,
			pass.legacyWrapped ? 1 : 0,
			pass.packetBacked ? 1 : 0,
			pass.resourceBacked ? 1 : 0 );
	}

	for ( int i = 0; i < rg_renderGraphResourceHandleCount; ++i ) {
		const renderGraphResourceHandle_t &handle = rg_renderGraphResourceHandles[i];
		if ( handle.framebufferStatus == 0 || handle.framebufferStatus == GL_FRAMEBUFFER_COMPLETE ) {
			common->Printf(
				"  resource[%d] id=0x%08x graph=%d %s type=%s %dx%d samples=%d fmt=0x%x flags=0x%x life=%d..%d alias=%d phys=%d tex=%u fbo=%u fbo=%s label='%s'\n",
				i,
				handle.stableId,
				handle.graphResourceIndex,
				handle.name,
				R_RenderGraphResources_TypeName( handle.type ),
				handle.width,
				handle.height,
				handle.samples,
				handle.internalFormat,
				handle.flags,
				handle.firstPass,
				handle.lastPass,
				handle.aliasGroup,
				handle.physicalAllocationId,
				handle.texture,
				handle.framebuffer,
				R_RenderGraphResources_FboStatusName( handle.framebufferStatus ),
				handle.debugLabel );
		} else {
			common->Printf(
				"  resource[%d] id=0x%08x graph=%d %s type=%s %dx%d samples=%d fmt=0x%x flags=0x%x life=%d..%d alias=%d phys=%d tex=%u fbo=%u fbo=0x%x label='%s'\n",
				i,
				handle.stableId,
				handle.graphResourceIndex,
				handle.name,
				R_RenderGraphResources_TypeName( handle.type ),
				handle.width,
				handle.height,
				handle.samples,
				handle.internalFormat,
				handle.flags,
				handle.firstPass,
				handle.lastPass,
				handle.aliasGroup,
				handle.physicalAllocationId,
				handle.texture,
				handle.framebuffer,
				handle.framebufferStatus,
				handle.debugLabel );
		}
	}

	for ( int i = 0; i < RENDER_GRAPH_RESOURCE_MAX_PHYSICAL_ALLOCATIONS; ++i ) {
		const renderGraphPhysicalAllocation_t &allocation = rg_renderGraphPhysicalAllocations[i];
		if ( !allocation.valid ) {
			continue;
		}
		common->Printf(
			"  physical[%d] id=%d type=%s %dx%d samples=%d alias=%d life=%d..%d tex=%u fbo=%u dsa=%d/%d params=%d fbo=%s inUse=%d label='%s'\n",
			i,
			allocation.id,
			R_RenderGraphResources_TypeName( allocation.type ),
			allocation.width,
			allocation.height,
			allocation.samples,
			allocation.aliasGroup,
			allocation.firstPass,
			allocation.lastPass,
			allocation.texture,
			allocation.framebuffer,
			allocation.dsaTexture ? 1 : 0,
			allocation.dsaFramebuffer ? 1 : 0,
			allocation.textureParameterUpdates,
			allocation.framebufferStatus == GL_FRAMEBUFFER_COMPLETE ? "complete" : "incomplete",
			allocation.inUseThisFrame ? 1 : 0,
			allocation.debugLabel );
	}
}

static bool R_RenderGraphResources_BuildWorldSelfTestGraph( idRenderGraph &graph ) {
	drawSurf_t drawSurfs[2];
	memset( drawSurfs, 0, sizeof( drawSurfs ) );
	drawSurf_t *drawSurfPtrs[2] = { &drawSurfs[0], &drawSurfs[1] };
	viewEntity_t viewEntity;
	memset( &viewEntity, 0, sizeof( viewEntity ) );
	viewDef_t worldView;
	memset( &worldView, 0, sizeof( worldView ) );
	worldView.viewEntitys = &viewEntity;
	worldView.drawSurfs = drawSurfPtrs;
	worldView.numDrawSurfs = 2;

	drawSurfsCommand_t drawCmd;
	memset( &drawCmd, 0, sizeof( drawCmd ) );
	drawCmd.commandId = RC_DRAW_VIEW;
	drawCmd.viewDef = &worldView;
	drawSurfsCommand_t fxCmd;
	memset( &fxCmd, 0, sizeof( fxCmd ) );
	fxCmd.commandId = RC_DRAW_SPECIAL_EFFECTS;
	fxCmd.viewDef = &worldView;
	emptyCommand_t swapCmd;
	memset( &swapCmd, 0, sizeof( swapCmd ) );
	swapCmd.commandId = RC_SWAP_BUFFERS;
	drawCmd.next = &fxCmd.commandId;
	fxCmd.next = &swapCmd.commandId;
	swapCmd.next = NULL;

	idScenePacketFrame packetFrame;
	R_ScenePackets_BuildLegacyCommandStream( reinterpret_cast<const emptyCommand_t *>( &drawCmd ), packetFrame );
	R_RenderGraph_BuildFromScenePackets( packetFrame, graph );
	return graph.NumResources() > 0 && !graph.Stats().overflow;
}

bool RendererRenderGraphResource_RunSelfTest( void ) {
	idRenderGraph graph;
	if ( !R_RenderGraphResources_BuildWorldSelfTestGraph( graph ) ) {
		common->Printf( "RendererRenderGraphResource self-test failed: could not build world graph\n" );
		return false;
	}

	if ( !rg_renderGraphResourceStats.initialized || !rg_renderGraphResourceStats.available ) {
		common->Printf( "RendererRenderGraphResource self-test skipped: graph resource owner unavailable (%s)\n", rg_renderGraphResourceStats.lastFailure );
		return true;
	}

	R_RenderGraphResources_PrepareFrame( graph );
	const renderGraphResourceManagerStats_t &stats = R_RenderGraphResources_Stats();
	if ( !stats.prepared || stats.graphResources != 5 || stats.handles < 7 || stats.importedHandles < 4 || stats.transientHandles < 3 || stats.completeFramebuffers < 3 || stats.lifetimeValidationFailures != 0 || stats.overflow ) {
		common->Printf(
			"RendererRenderGraphResource self-test detail: prepared=%d graphResources=%d handles=%d imported=%d transient=%d fbo=%d/%d lifetimeFailures=%d overflow=%d status='%s'\n",
			stats.prepared ? 1 : 0,
			stats.graphResources,
			stats.handles,
			stats.importedHandles,
			stats.transientHandles,
			stats.completeFramebuffers,
			stats.framebufferCount,
			stats.lifetimeValidationFailures,
			stats.overflow ? 1 : 0,
			stats.lastFailure );
		return false;
	}

	const renderGraphResourceHandle_t *sceneColor = R_RenderGraphResources_FindHandle( "sceneColor" );
	const renderGraphResourceHandle_t *sceneDepth = R_RenderGraphResources_FindHandle( "sceneDepth" );
	const renderGraphResourceHandle_t *postA = R_RenderGraphResources_FindHandle( "postA" );
	const renderGraphResourceHandle_t *backBuffer = R_RenderGraphResources_FindHandle( "backBuffer" );
	const renderGraphResourceHandle_t *lightGrid = R_RenderGraphResources_FindHandle( "lightGrid" );
	if ( sceneColor == NULL || sceneDepth == NULL || postA == NULL || backBuffer == NULL || lightGrid == NULL ) {
		common->Printf( "RendererRenderGraphResource self-test failed: missing expected handles\n" );
		return false;
	}
	if ( !sceneColor->allocated || !sceneColor->framebufferComplete || sceneColor->physicalAllocationId <= 0
		|| !sceneDepth->allocated || !sceneDepth->framebufferComplete || sceneDepth->physicalAllocationId <= 0
		|| !postA->allocated || !postA->framebufferComplete || postA->physicalAllocationId <= 0
		|| !backBuffer->imported || !backBuffer->presentable || !backBuffer->framebufferComplete
		|| !lightGrid->imported || lightGrid->type != RENDER_GRAPH_RESOURCE_BUFFER ) {
		common->Printf( "RendererRenderGraphResource self-test failed: handle allocation/import mismatch\n" );
		return false;
	}

	common->Printf(
		"RendererRenderGraphResource self-test passed (handles=%d imported=%d transient=%d physical=%d lowOverhead=%d dsaTex=%d dsaFbo=%d fbo=%d/%d)\n",
		stats.handles,
		stats.importedHandles,
		stats.transientHandles,
		stats.physicalAllocations,
		stats.lowOverheadReady ? 1 : 0,
		stats.dsaTextureAllocations,
		stats.dsaFramebufferAllocations,
		stats.completeFramebuffers,
		stats.framebufferCount );
	return true;
}
