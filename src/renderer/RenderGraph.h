// Copyright (C) 2004 Id Software, Inc.
//

#ifndef __RENDER_GRAPH_H__
#define __RENDER_GRAPH_H__

#include "ScenePackets.h"

const int RENDER_GRAPH_MAX_PASSES = SCENE_PACKET_MAX_PASSES;
const int RENDER_GRAPH_MAX_RESOURCES = 32;
const int RENDER_GRAPH_MAX_RESOURCE_ACCESSES = 1024;

enum renderGraphResourceType_t {
	RENDER_GRAPH_RESOURCE_COLOR = 0,
	RENDER_GRAPH_RESOURCE_DEPTH_STENCIL,
	RENDER_GRAPH_RESOURCE_DEPTH,
	RENDER_GRAPH_RESOURCE_BUFFER
};

enum renderGraphResourceAccessBits_t {
	RENDER_GRAPH_ACCESS_READ = 1 << 0,
	RENDER_GRAPH_ACCESS_WRITE = 1 << 1,
	RENDER_GRAPH_ACCESS_CLEAR = 1 << 2,
	RENDER_GRAPH_ACCESS_RESOLVE = 1 << 3,
	RENDER_GRAPH_ACCESS_PRESENT = 1 << 4,
	RENDER_GRAPH_ACCESS_INVALIDATE = 1 << 5
};

typedef struct renderGraphResource_s {
	const char				*name;
	renderGraphResourceType_t type;
	int						widthScale;
	int						heightScale;
	int						samples;
	bool					imported;
	bool					transient;
	bool					presentable;
	int						aliasGroup;
	int						firstPass;
	int						lastPass;
	int						producerPass;
	int						lastWriterPass;
	bool					read;
	bool					written;
	bool					cleared;
	bool					resolved;
	bool					presented;
	bool					invalidated;
} renderGraphResource_t;

typedef struct renderGraphResourceAccess_s {
	int						passIndex;
	int						resourceIndex;
	unsigned int			access;
	const char				*usage;
} renderGraphResourceAccess_t;

typedef struct renderGraphPass_s {
	renderPassCategory_t	category;
	const char				*name;
	int						firstResourceAccess;
	int						resourceAccessCount;
	int						passPacketCount;
	int						drawPacketCount;
	int						scenePacketCount;
	int						commandPacketCount;
	int						readResourceCount;
	int						writeResourceCount;
	int						clearCount;
	int						resolveCount;
	int						presentCount;
	int						invalidateCount;
	bool					enabled;
	bool					legacyWrapped;
	bool					packetBacked;
	bool					resourceBacked;
} renderGraphPass_t;

typedef struct renderGraphStats_s {
	int						graphPasses;
	int						passPackets;
	int						scenePackets;
	int						drawPackets;
	int						commandPackets;
	int						resources;
	int						importedResources;
	int						transientResources;
	int						aliasableTransientResources;
	int						resourceAccesses;
	int						readAccesses;
	int						writeAccesses;
	int						clearOps;
	int						resolveOps;
	int						presentOps;
	int						invalidateOps;
	bool					overflow;
} renderGraphStats_t;

class idRenderGraph {
public:
	idRenderGraph();
	void Clear( void );
	bool AddPass( renderPassCategory_t category, const char *name, bool enabled, bool legacyWrapped );
	bool AddPacketPass( renderPassCategory_t category, const char *name, int drawPackets, int commandPackets );
	int AddResource( const char *name, renderGraphResourceType_t type, bool imported, bool transient, bool presentable, int aliasGroup );
	int FindResource( const char *name ) const;
	bool AddPassResource( int passIndex, int resourceIndex, unsigned int access, const char *usage );
	void SetPacketFrameStats( int scenePackets, int commandPackets, bool overflow );
	int NumPasses( void ) const;
	int NumResources( void ) const;
	int NumResourceAccesses( void ) const;
	int FindPass( renderPassCategory_t category ) const;
	const renderGraphPass_t &Pass( int index ) const;
	const renderGraphResource_t &Resource( int index ) const;
	const renderGraphResourceAccess_t &ResourceAccess( int index ) const;
	const renderGraphStats_t &Stats( void ) const;

private:
	renderGraphPass_t passes[RENDER_GRAPH_MAX_PASSES];
	renderGraphResource_t resources[RENDER_GRAPH_MAX_RESOURCES];
	renderGraphResourceAccess_t resourceAccesses[RENDER_GRAPH_MAX_RESOURCE_ACCESSES];
	renderGraphStats_t stats;
	int numPasses;
	int numResources;
	int numResourceAccesses;
};

void R_RenderGraph_BuildLegacyFrameGraph( const emptyCommand_t *cmds, idRenderGraph &graph );
void R_RenderGraph_BuildFromScenePackets( const idScenePacketFrame &packetFrame, idRenderGraph &graph );
void R_RenderGraph_LogIfVerbose( const idRenderGraph &graph );
bool RendererRenderGraph_RunSelfTest( void );

#endif /* !__RENDER_GRAPH_H__ */
