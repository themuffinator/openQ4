// Copyright (C) 2004 Id Software, Inc.
//

#include "tr_local.h"
#include "RenderGraph.h"

idRenderGraph::idRenderGraph()
	: numPasses( 0 ) {
}

void idRenderGraph::Clear( void ) {
	numPasses = 0;
}

bool idRenderGraph::AddPass( renderPassCategory_t category, const char *name, bool enabled, bool legacyWrapped ) {
	if ( numPasses >= RENDER_GRAPH_MAX_PASSES ) {
		return false;
	}
	passes[numPasses].category = category;
	passes[numPasses].name = name;
	passes[numPasses].enabled = enabled;
	passes[numPasses].legacyWrapped = legacyWrapped;
	numPasses++;
	return true;
}

int idRenderGraph::NumPasses( void ) const {
	return numPasses;
}

const renderGraphPass_t &idRenderGraph::Pass( int index ) const {
	return passes[index];
}

static bool R_RenderGraph_HasPass( const idRenderGraph &graph, renderPassCategory_t category ) {
	for ( int i = 0; i < graph.NumPasses(); ++i ) {
		if ( graph.Pass( i ).category == category ) {
			return true;
		}
	}
	return false;
}

static void R_RenderGraph_AddPassOnce( idRenderGraph &graph, renderPassCategory_t category, const char *name ) {
	if ( !R_RenderGraph_HasPass( graph, category ) ) {
		graph.AddPass( category, name, true, true );
	}
}

void R_RenderGraph_BuildLegacyFrameGraph( const emptyCommand_t *cmds, idRenderGraph &graph ) {
	graph.Clear();

	for ( const emptyCommand_t *cmd = cmds; cmd != NULL; cmd = reinterpret_cast<const emptyCommand_t *>( cmd->next ) ) {
		switch ( cmd->commandId ) {
		case RC_DRAW_VIEW:
			R_RenderGraph_AddPassOnce( graph, RENDER_PASS_DEPTH, "legacyDepth" );
			R_RenderGraph_AddPassOnce( graph, RENDER_PASS_ARB2_INTERACTION, "legacyARB2Interaction" );
			R_RenderGraph_AddPassOnce( graph, RENDER_PASS_LIGHT_GRID, "legacyLightGrid" );
			R_RenderGraph_AddPassOnce( graph, RENDER_PASS_AMBIENT, "legacyAmbient" );
			R_RenderGraph_AddPassOnce( graph, RENDER_PASS_FOG_BLEND, "legacyFogBlend" );
			R_RenderGraph_AddPassOnce( graph, RENDER_PASS_AUTHORED_POST, "legacyPostProcess" );
			break;
		case RC_DRAW_SPECIAL_EFFECTS:
			R_RenderGraph_AddPassOnce( graph, RENDER_PASS_SPECIAL_EFFECTS, "legacySpecialEffects" );
			break;
		case RC_SET_RENDERTEXTURE:
		case RC_RESOLVE_MSAA:
		case RC_CLEAR_RENDERTARGET:
			R_RenderGraph_AddPassOnce( graph, RENDER_PASS_AUTHORED_POST, "legacyRenderTargetOps" );
			break;
		case RC_SWAP_BUFFERS:
			R_RenderGraph_AddPassOnce( graph, RENDER_PASS_PRESENT, "legacyPresent" );
			break;
		default:
			break;
		}
	}
}

void R_RenderGraph_LogIfVerbose( const idRenderGraph &graph ) {
	if ( r_rendererMetrics.GetInteger() < 2 ) {
		return;
	}

	common->Printf( "renderGraph legacy passes=%d:", graph.NumPasses() );
	for ( int i = 0; i < graph.NumPasses(); ++i ) {
		const renderGraphPass_t &pass = graph.Pass( i );
		common->Printf( " %s%s", pass.name ? pass.name : RenderPassCategory_Name( pass.category ), pass.enabled ? "" : "(off)" );
	}
	common->Printf( "\n" );
}
