// Copyright (C) 2004 Id Software, Inc.
//

#ifndef __RENDER_GRAPH_RESOURCES_H__
#define __RENDER_GRAPH_RESOURCES_H__

#include "RendererCaps.h"
#include "RenderGraph.h"
#include "qgl.h"

const int RENDER_GRAPH_RESOURCE_MAX_HANDLES = RENDER_GRAPH_MAX_RESOURCES + 4;
const int RENDER_GRAPH_RESOURCE_MAX_PHYSICAL_ALLOCATIONS = 64;
const int RENDER_GRAPH_RESOURCE_MAX_PASS_RECORDS = RENDER_GRAPH_MAX_PASSES;

enum renderGraphResourceHandleFlags_t {
	RENDER_GRAPH_RESOURCE_HANDLE_IMPORTED = 1 << 0,
	RENDER_GRAPH_RESOURCE_HANDLE_TRANSIENT = 1 << 1,
	RENDER_GRAPH_RESOURCE_HANDLE_PRESENTABLE = 1 << 2,
	RENDER_GRAPH_RESOURCE_HANDLE_TEXTURE = 1 << 3,
	RENDER_GRAPH_RESOURCE_HANDLE_BUFFER = 1 << 4,
	RENDER_GRAPH_RESOURCE_HANDLE_BACKBUFFER = 1 << 5,
	RENDER_GRAPH_RESOURCE_HANDLE_LEGACY_IMPORT = 1 << 6,
	RENDER_GRAPH_RESOURCE_HANDLE_FBO_COMPLETE = 1 << 7
};

typedef struct renderGraphResourceHandle_s {
	unsigned int			stableId;
	char					name[64];
	char					debugLabel[96];
	renderGraphResourceType_t type;
	int						graphResourceIndex;
	int						width;
	int						height;
	int						samples;
	int						mipLevels;
	GLenum					target;
	GLenum					internalFormat;
	GLenum					format;
	GLenum					dataType;
	GLenum					attachment;
	unsigned int			flags;
	int						firstPass;
	int						lastPass;
	int						aliasGroup;
	int						physicalAllocationId;
	GLuint					texture;
	GLuint					framebuffer;
	GLenum					framebufferStatus;
	bool					imported;
	bool					transient;
	bool					presentable;
	bool					allocated;
	bool					framebufferComplete;
} renderGraphResourceHandle_t;

typedef struct renderGraphResourcePassRecord_s {
	int						passIndex;
	renderPassCategory_t	category;
	char					name[64];
	int						resourceAccesses;
	int						readResources;
	int						writeResources;
	int						clearOps;
	int						resolveOps;
	int						invalidateOps;
	int						presentOps;
	bool					enabled;
	bool					legacyWrapped;
	bool					packetBacked;
	bool					resourceBacked;
} renderGraphResourcePassRecord_t;

typedef struct renderGraphResourceManagerStats_s {
	bool					initialized;
	bool					available;
	bool					supported;
	bool					prepared;
	bool					lowOverheadReady;
	int						width;
	int						height;
	int						graphPasses;
	int						graphResources;
	int						passRecords;
	int						handles;
	int						importedHandles;
	int						transientHandles;
	int						textureHandles;
	int						bufferHandles;
	int						physicalAllocations;
	int						newPhysicalAllocations;
	int						reusedPhysicalAllocations;
	int						aliasReusedPhysicalAllocations;
	int						releasedPhysicalAllocations;
	int						dsaTextureAllocations;
	int						dsaTextureParameterUpdates;
	int						dsaFramebufferAllocations;
	int						classicTextureAllocations;
	int						classicFramebufferAllocations;
	int						mipmappedTextures;
	int						totalMipLevels;
	int						framebufferCount;
	int						completeFramebuffers;
	int						incompleteFramebuffers;
	int						skippedImported;
	int						skippedBuffers;
	int						aliasGroups;
	int						lifetimeValidationFailures;
	bool					overflow;
	char					lastFailure[128];
} renderGraphResourceManagerStats_t;

void R_RenderGraphResources_Init( const renderBackendCaps_t &caps, const renderFeatureSet_t &features );
void R_RenderGraphResources_Shutdown( void );
void R_RenderGraphResources_PrepareFrame( const idRenderGraph &graph );
const renderGraphResourceManagerStats_t &R_RenderGraphResources_Stats( void );
const renderGraphResourceHandle_t *R_RenderGraphResources_FindHandle( const char *name );
const renderGraphResourceHandle_t *R_RenderGraphResources_HandleForGraphResource( int graphResourceIndex );
void R_RenderGraphResources_PrintGfxInfo( void );
void R_RenderGraphResources_DumpLatest( void );
bool RendererRenderGraphResource_RunSelfTest( void );

#endif /* !__RENDER_GRAPH_RESOURCES_H__ */
