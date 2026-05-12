// Copyright (C) 2004 Id Software, Inc.
//

#ifndef __RENDER_GRAPH_H__
#define __RENDER_GRAPH_H__

#include "ScenePackets.h"

const int RENDER_GRAPH_MAX_PASSES = 32;

typedef struct renderGraphPass_s {
	renderPassCategory_t	category;
	const char				*name;
	bool					enabled;
	bool					legacyWrapped;
} renderGraphPass_t;

class idRenderGraph {
public:
	idRenderGraph();
	void Clear( void );
	bool AddPass( renderPassCategory_t category, const char *name, bool enabled, bool legacyWrapped );
	int NumPasses( void ) const;
	const renderGraphPass_t &Pass( int index ) const;

private:
	renderGraphPass_t passes[RENDER_GRAPH_MAX_PASSES];
	int numPasses;
};

void R_RenderGraph_BuildLegacyFrameGraph( const emptyCommand_t *cmds, idRenderGraph &graph );
void R_RenderGraph_LogIfVerbose( const idRenderGraph &graph );

#endif /* !__RENDER_GRAPH_H__ */
