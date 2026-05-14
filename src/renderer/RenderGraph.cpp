// Copyright (C) 2004 Id Software, Inc.
//

#include "tr_local.h"
#include "RenderGraph.h"

idRenderGraph::idRenderGraph()
	: numPasses( 0 ),
	  numResources( 0 ),
	  numResourceAccesses( 0 ) {
	memset( &stats, 0, sizeof( stats ) );
}

void idRenderGraph::Clear( void ) {
	memset( passes, 0, sizeof( passes ) );
	memset( resources, 0, sizeof( resources ) );
	memset( resourceAccesses, 0, sizeof( resourceAccesses ) );
	memset( &stats, 0, sizeof( stats ) );
	numPasses = 0;
	numResources = 0;
	numResourceAccesses = 0;
}

bool idRenderGraph::AddPass( renderPassCategory_t category, const char *name, bool enabled, bool legacyWrapped ) {
	if ( numPasses >= RENDER_GRAPH_MAX_PASSES ) {
		stats.overflow = true;
		return false;
	}
	renderGraphPass_t &pass = passes[numPasses];
	memset( &pass, 0, sizeof( pass ) );
	pass.category = category;
	pass.name = name;
	pass.firstResourceAccess = -1;
	pass.resourceAccessCount = 0;
	pass.enabled = enabled;
	pass.legacyWrapped = legacyWrapped;
	pass.packetBacked = false;
	pass.resourceBacked = false;
	numPasses++;
	stats.graphPasses = numPasses;
	return true;
}

bool idRenderGraph::AddPacketPass( renderPassCategory_t category, const char *name, int drawPackets, int commandPackets ) {
	if ( !AddPass( category, name, true, true ) ) {
		return false;
	}

	renderGraphPass_t &pass = passes[numPasses - 1];
	pass.packetBacked = true;
	pass.passPacketCount = 1;
	pass.drawPacketCount = drawPackets;
	pass.commandPacketCount = commandPackets;
	pass.scenePacketCount = 1;
	stats.passPackets++;
	stats.drawPackets += drawPackets;
	stats.commandPackets += commandPackets;
	return true;
}

int idRenderGraph::AddResource( const char *name, renderGraphResourceType_t type, bool imported, bool transient, bool presentable, int aliasGroup ) {
	if ( numResources >= RENDER_GRAPH_MAX_RESOURCES ) {
		stats.overflow = true;
		return -1;
	}

	renderGraphResource_t &resource = resources[numResources];
	memset( &resource, 0, sizeof( resource ) );
	resource.name = name;
	resource.type = type;
	resource.widthScale = 1;
	resource.heightScale = 1;
	resource.samples = 1;
	resource.imported = imported;
	resource.transient = transient;
	resource.presentable = presentable;
	resource.aliasGroup = aliasGroup;
	resource.firstPass = -1;
	resource.lastPass = -1;
	resource.producerPass = -1;
	resource.lastWriterPass = -1;

	numResources++;
	stats.resources = numResources;
	if ( imported ) {
		stats.importedResources++;
	}
	if ( transient ) {
		stats.transientResources++;
		if ( aliasGroup > 0 ) {
			stats.aliasableTransientResources++;
		}
	}
	return numResources - 1;
}

int idRenderGraph::FindResource( const char *name ) const {
	if ( name == NULL || name[0] == '\0' ) {
		return -1;
	}
	for ( int i = 0; i < numResources; ++i ) {
		if ( resources[i].name != NULL && idStr::Icmp( resources[i].name, name ) == 0 ) {
			return i;
		}
	}
	return -1;
}

bool idRenderGraph::AddPassResource( int passIndex, int resourceIndex, unsigned int access, const char *usage ) {
	if ( passIndex < 0 || passIndex >= numPasses || resourceIndex < 0 || resourceIndex >= numResources ) {
		stats.overflow = true;
		return false;
	}
	if ( numResourceAccesses >= RENDER_GRAPH_MAX_RESOURCE_ACCESSES ) {
		stats.overflow = true;
		return false;
	}

	renderGraphResourceAccess_t &resourceAccess = resourceAccesses[numResourceAccesses];
	memset( &resourceAccess, 0, sizeof( resourceAccess ) );
	resourceAccess.passIndex = passIndex;
	resourceAccess.resourceIndex = resourceIndex;
	resourceAccess.access = access;
	resourceAccess.usage = usage;

	renderGraphPass_t &pass = passes[passIndex];
	if ( pass.firstResourceAccess < 0 ) {
		pass.firstResourceAccess = numResourceAccesses;
	}
	pass.resourceAccessCount++;
	pass.resourceBacked = true;

	renderGraphResource_t &resource = resources[resourceIndex];
	if ( resource.firstPass < 0 || passIndex < resource.firstPass ) {
		resource.firstPass = passIndex;
	}
	if ( passIndex > resource.lastPass ) {
		resource.lastPass = passIndex;
	}

	stats.resourceAccesses++;
	if ( ( access & RENDER_GRAPH_ACCESS_READ ) != 0 ) {
		pass.readResourceCount++;
		stats.readAccesses++;
		resource.read = true;
	}
	if ( ( access & RENDER_GRAPH_ACCESS_WRITE ) != 0 ) {
		pass.writeResourceCount++;
		stats.writeAccesses++;
		resource.written = true;
		resource.lastWriterPass = passIndex;
		if ( resource.producerPass < 0 ) {
			resource.producerPass = passIndex;
		}
	}
	if ( ( access & RENDER_GRAPH_ACCESS_CLEAR ) != 0 ) {
		pass.clearCount++;
		stats.clearOps++;
		resource.cleared = true;
	}
	if ( ( access & RENDER_GRAPH_ACCESS_RESOLVE ) != 0 ) {
		pass.resolveCount++;
		stats.resolveOps++;
		resource.resolved = true;
	}
	if ( ( access & RENDER_GRAPH_ACCESS_PRESENT ) != 0 ) {
		pass.presentCount++;
		stats.presentOps++;
		resource.presented = true;
	}
	if ( ( access & RENDER_GRAPH_ACCESS_INVALIDATE ) != 0 ) {
		pass.invalidateCount++;
		stats.invalidateOps++;
		resource.invalidated = true;
	}

	numResourceAccesses++;
	return true;
}

void idRenderGraph::SetPacketFrameStats( int scenePackets, int commandPackets, bool overflow ) {
	stats.scenePackets = scenePackets;
	stats.commandPackets = commandPackets;
	stats.overflow = stats.overflow || overflow;
}

int idRenderGraph::NumPasses( void ) const {
	return numPasses;
}

int idRenderGraph::NumResources( void ) const {
	return numResources;
}

int idRenderGraph::NumResourceAccesses( void ) const {
	return numResourceAccesses;
}

int idRenderGraph::FindPass( renderPassCategory_t category ) const {
	for ( int i = 0; i < numPasses; ++i ) {
		if ( passes[i].category == category ) {
			return i;
		}
	}
	return -1;
}

const renderGraphPass_t &idRenderGraph::Pass( int index ) const {
	return passes[index];
}

const renderGraphResource_t &idRenderGraph::Resource( int index ) const {
	return resources[index];
}

const renderGraphResourceAccess_t &idRenderGraph::ResourceAccess( int index ) const {
	return resourceAccesses[index];
}

const renderGraphStats_t &idRenderGraph::Stats( void ) const {
	return stats;
}

static const char *R_RenderGraph_ResourceTypeName( renderGraphResourceType_t type ) {
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

static bool R_RenderGraph_HasPass( const idRenderGraph &graph, renderPassCategory_t category ) {
	for ( int i = 0; i < graph.NumPasses(); ++i ) {
		if ( graph.Pass( i ).category == category ) {
			return true;
		}
	}
	return false;
}

static bool R_RenderGraph_ShouldModelModernVisible( void );

static const char *R_RenderGraph_LegacyPassName( renderPassCategory_t category ) {
	switch ( category ) {
	case RENDER_PASS_DEPTH:
		return "legacyDepth";
	case RENDER_PASS_STENCIL_SHADOW:
		return "legacyStencilShadow";
	case RENDER_PASS_SHADOW_MAP:
		return "legacyShadowMap";
	case RENDER_PASS_ARB2_INTERACTION:
		return "legacyARB2Interaction";
	case RENDER_PASS_LIGHT_GRID:
		return "legacyLightGrid";
	case RENDER_PASS_AMBIENT:
		return "legacyAmbient";
	case RENDER_PASS_DEFERRED_RESOLVE:
		return "modernDeferredResolve";
	case RENDER_PASS_FORWARD_PLUS:
		return "modernForwardPlus";
	case RENDER_PASS_FOG_BLEND:
		return "legacyFogBlend";
	case RENDER_PASS_SSAO:
		return "legacySSAO";
	case RENDER_PASS_MOTION_BLUR:
		return "legacyMotionBlur";
	case RENDER_PASS_LENS_FLARE:
		return "legacyLensFlare";
	case RENDER_PASS_BLOOM:
		return "legacyBloom";
	case RENDER_PASS_AUTHORED_POST:
		return "legacyPostProcess";
	case RENDER_PASS_SPECIAL_EFFECTS:
		return "legacySpecialEffects";
	case RENDER_PASS_GUI:
		return "legacyGUI";
	case RENDER_PASS_PRESENT:
		return R_RenderGraph_ShouldModelModernVisible() ? "modernPresent" : "legacyPresent";
	default:
		return RenderPassCategory_Name( category );
	}
}

static int R_RenderGraph_EnsureResource( idRenderGraph &graph, const char *name, renderGraphResourceType_t type, bool imported, bool transient, bool presentable, int aliasGroup ) {
	const int existing = graph.FindResource( name );
	if ( existing >= 0 ) {
		return existing;
	}
	return graph.AddResource( name, type, imported, transient, presentable, aliasGroup );
}

static bool R_RenderGraph_ResourceUnused( const idRenderGraph &graph, int resourceIndex ) {
	return resourceIndex >= 0 && graph.Resource( resourceIndex ).firstPass < 0;
}

static void R_RenderGraph_AddAccess( idRenderGraph &graph, int passIndex, int resourceIndex, unsigned int access, const char *usage ) {
	if ( resourceIndex >= 0 ) {
		graph.AddPassResource( passIndex, resourceIndex, access, usage );
	}
}

static int R_RenderGraph_EnsureSceneColor( idRenderGraph &graph ) {
	return R_RenderGraph_EnsureResource( graph, "sceneColor", RENDER_GRAPH_RESOURCE_COLOR, false, true, false, 1 );
}

static int R_RenderGraph_EnsureSceneDepth( idRenderGraph &graph ) {
	return R_RenderGraph_EnsureResource( graph, "sceneDepth", RENDER_GRAPH_RESOURCE_DEPTH_STENCIL, false, true, false, 2 );
}

static int R_RenderGraph_EnsureBackBuffer( idRenderGraph &graph ) {
	return R_RenderGraph_EnsureResource( graph, "backBuffer", RENDER_GRAPH_RESOURCE_COLOR, true, false, true, 0 );
}

static int R_RenderGraph_EnsurePostA( idRenderGraph &graph ) {
	return R_RenderGraph_EnsureResource( graph, "postA", RENDER_GRAPH_RESOURCE_COLOR, false, true, false, 1 );
}

static bool R_RenderGraph_ShouldModelGBuffer( void ) {
	return r_rendererModernVisible.GetBool()
		|| r_rendererModernOpaque.GetBool()
		|| r_rendererModernGBufferDebug.GetInteger() > 0
		|| r_rendererModernDeferred.GetBool()
		|| r_rendererModernDeferredDebug.GetInteger() > 0;
}

static bool R_RenderGraph_ShouldModelDeferredResolve( void ) {
	return r_rendererModernVisible.GetBool() || r_rendererModernDeferred.GetBool() || r_rendererModernDeferredDebug.GetInteger() > 0;
}

static bool R_RenderGraph_ShouldModelForwardPlus( void ) {
	return r_rendererModernVisible.GetBool() || r_rendererForwardPlus.GetBool();
}

static bool R_RenderGraph_ShouldModelModernVisible( void ) {
	return r_rendererModernVisible.GetBool();
}

static void R_RenderGraph_AddSceneColorReadWrite( idRenderGraph &graph, int passIndex, const char *usage );

static void R_RenderGraph_AddShadowReceiverReads(
	idRenderGraph &graph,
	int passIndex,
	const char *descriptorUsage,
	const char *projectedUsage,
	const char *pointUsage,
	const char *cascadeUsage,
	const char *translucentUsage ) {
	const int shadowDescriptors = graph.FindResource( "shadowDescriptors" );
	const int projectedShadowAtlas = graph.FindResource( "projectedShadowAtlas" );
	const int pointShadowAtlas = graph.FindResource( "pointShadowAtlas" );
	const int cascadeShadowAtlas = graph.FindResource( "cascadeShadowAtlas" );
	const int translucentShadowMoments = graph.FindResource( "translucentShadowMoments" );
	R_RenderGraph_AddAccess( graph, passIndex, shadowDescriptors, RENDER_GRAPH_ACCESS_READ, descriptorUsage );
	R_RenderGraph_AddAccess( graph, passIndex, projectedShadowAtlas, RENDER_GRAPH_ACCESS_READ, projectedUsage );
	R_RenderGraph_AddAccess( graph, passIndex, pointShadowAtlas, RENDER_GRAPH_ACCESS_READ, pointUsage );
	R_RenderGraph_AddAccess( graph, passIndex, cascadeShadowAtlas, RENDER_GRAPH_ACCESS_READ, cascadeUsage );
	R_RenderGraph_AddAccess( graph, passIndex, translucentShadowMoments, RENDER_GRAPH_ACCESS_READ, translucentUsage );
}

static void R_RenderGraph_AddGBufferWrites( idRenderGraph &graph, int passIndex ) {
	const int gbufferAlbedo = R_RenderGraph_EnsureResource( graph, "gbufferAlbedo", RENDER_GRAPH_RESOURCE_COLOR, false, true, false, 3 );
	const int gbufferNormal = R_RenderGraph_EnsureResource( graph, "gbufferNormal", RENDER_GRAPH_RESOURCE_COLOR, false, true, false, 3 );
	const int gbufferMaterial = R_RenderGraph_EnsureResource( graph, "gbufferMaterial", RENDER_GRAPH_RESOURCE_COLOR, false, true, false, 3 );
	const int gbufferEmissive = R_RenderGraph_EnsureResource( graph, "gbufferEmissive", RENDER_GRAPH_RESOURCE_COLOR, false, true, false, 3 );
	R_RenderGraph_AddAccess( graph, passIndex, gbufferAlbedo, RENDER_GRAPH_ACCESS_WRITE | RENDER_GRAPH_ACCESS_CLEAR | RENDER_GRAPH_ACCESS_INVALIDATE, "gbuffer-albedo-write" );
	R_RenderGraph_AddAccess( graph, passIndex, gbufferNormal, RENDER_GRAPH_ACCESS_WRITE | RENDER_GRAPH_ACCESS_CLEAR | RENDER_GRAPH_ACCESS_INVALIDATE, "gbuffer-normal-write" );
	R_RenderGraph_AddAccess( graph, passIndex, gbufferMaterial, RENDER_GRAPH_ACCESS_WRITE | RENDER_GRAPH_ACCESS_CLEAR | RENDER_GRAPH_ACCESS_INVALIDATE, "gbuffer-material-write" );
	R_RenderGraph_AddAccess( graph, passIndex, gbufferEmissive, RENDER_GRAPH_ACCESS_WRITE | RENDER_GRAPH_ACCESS_CLEAR | RENDER_GRAPH_ACCESS_INVALIDATE, "gbuffer-emissive-write" );
}

static void R_RenderGraph_AddDeferredResolvePass( idRenderGraph &graph ) {
	if ( !R_RenderGraph_ShouldModelDeferredResolve() || R_RenderGraph_HasPass( graph, RENDER_PASS_DEFERRED_RESOLVE ) ) {
		return;
	}
	if ( !graph.AddPass( RENDER_PASS_DEFERRED_RESOLVE, "modernDeferredResolve", true, false ) ) {
		return;
	}
	const int passIndex = graph.NumPasses() - 1;
	const int sceneDepth = R_RenderGraph_EnsureSceneDepth( graph );
	const int gbufferAlbedo = R_RenderGraph_EnsureResource( graph, "gbufferAlbedo", RENDER_GRAPH_RESOURCE_COLOR, false, true, false, 3 );
	const int gbufferNormal = R_RenderGraph_EnsureResource( graph, "gbufferNormal", RENDER_GRAPH_RESOURCE_COLOR, false, true, false, 3 );
	const int gbufferMaterial = R_RenderGraph_EnsureResource( graph, "gbufferMaterial", RENDER_GRAPH_RESOURCE_COLOR, false, true, false, 3 );
	const int gbufferEmissive = R_RenderGraph_EnsureResource( graph, "gbufferEmissive", RENDER_GRAPH_RESOURCE_COLOR, false, true, false, 3 );
	const int clusterGrid = R_RenderGraph_EnsureResource( graph, "clusterGrid", RENDER_GRAPH_RESOURCE_BUFFER, true, false, false, 0 );
	const int lightGrid = R_RenderGraph_EnsureResource( graph, "lightGrid", RENDER_GRAPH_RESOURCE_BUFFER, true, false, false, 0 );
	const int deferredLight = R_RenderGraph_EnsureResource( graph, "deferredLight", RENDER_GRAPH_RESOURCE_COLOR, false, true, false, 4 );

	R_RenderGraph_AddAccess( graph, passIndex, sceneDepth, RENDER_GRAPH_ACCESS_READ, "deferred-depth-read" );
	R_RenderGraph_AddAccess( graph, passIndex, gbufferAlbedo, RENDER_GRAPH_ACCESS_READ, "deferred-albedo-read" );
	R_RenderGraph_AddAccess( graph, passIndex, gbufferNormal, RENDER_GRAPH_ACCESS_READ, "deferred-normal-read" );
	R_RenderGraph_AddAccess( graph, passIndex, gbufferMaterial, RENDER_GRAPH_ACCESS_READ, "deferred-material-read" );
	R_RenderGraph_AddAccess( graph, passIndex, gbufferEmissive, RENDER_GRAPH_ACCESS_READ, "deferred-lightgrid-read" );
	R_RenderGraph_AddAccess( graph, passIndex, clusterGrid, RENDER_GRAPH_ACCESS_READ, "deferred-cluster-read" );
	R_RenderGraph_AddAccess( graph, passIndex, lightGrid, RENDER_GRAPH_ACCESS_READ, "deferred-baked-light-grid-read" );
	R_RenderGraph_AddShadowReceiverReads(
		graph,
		passIndex,
		"deferred-shadow-descriptor-read",
		"deferred-projected-shadow-read",
		"deferred-point-shadow-read",
		"deferred-cascade-shadow-read",
		"deferred-translucent-shadow-read" );
	R_RenderGraph_AddAccess( graph, passIndex, deferredLight, RENDER_GRAPH_ACCESS_WRITE | RENDER_GRAPH_ACCESS_CLEAR | RENDER_GRAPH_ACCESS_RESOLVE | RENDER_GRAPH_ACCESS_INVALIDATE, "deferred-light-write" );
}

static void R_RenderGraph_AddForwardPlusPass( idRenderGraph &graph ) {
	if ( !R_RenderGraph_ShouldModelForwardPlus() || R_RenderGraph_HasPass( graph, RENDER_PASS_FORWARD_PLUS ) ) {
		return;
	}
	if ( !graph.AddPass( RENDER_PASS_FORWARD_PLUS, "modernForwardPlus", true, false ) ) {
		return;
	}
	const int passIndex = graph.NumPasses() - 1;
	const int sceneDepth = R_RenderGraph_EnsureSceneDepth( graph );
	const int clusterGrid = R_RenderGraph_EnsureResource( graph, "clusterGrid", RENDER_GRAPH_RESOURCE_BUFFER, true, false, false, 0 );
	const int lightGrid = R_RenderGraph_EnsureResource( graph, "lightGrid", RENDER_GRAPH_RESOURCE_BUFFER, true, false, false, 0 );
	const int deferredLight = graph.FindResource( "deferredLight" );

	R_RenderGraph_AddAccess( graph, passIndex, sceneDepth, RENDER_GRAPH_ACCESS_READ, "forward-plus-depth-read" );
	if ( deferredLight >= 0 ) {
		R_RenderGraph_AddAccess( graph, passIndex, deferredLight, RENDER_GRAPH_ACCESS_READ, "forward-plus-deferred-read" );
	}
	R_RenderGraph_AddAccess( graph, passIndex, clusterGrid, RENDER_GRAPH_ACCESS_READ, "forward-plus-cluster-read" );
	R_RenderGraph_AddAccess( graph, passIndex, lightGrid, RENDER_GRAPH_ACCESS_READ, "forward-plus-light-grid-read" );
	R_RenderGraph_AddShadowReceiverReads(
		graph,
		passIndex,
		"forward-plus-shadow-descriptor-read",
		"forward-plus-projected-shadow-read",
		"forward-plus-point-shadow-read",
		"forward-plus-cascade-shadow-read",
		"forward-plus-translucent-shadow-read" );
	R_RenderGraph_AddSceneColorReadWrite( graph, passIndex, "forward-plus-scene-color" );
}

static void R_RenderGraph_AddSceneColorWrite( idRenderGraph &graph, int passIndex, const char *usage ) {
	const int sceneColor = R_RenderGraph_EnsureSceneColor( graph );
	unsigned int access = RENDER_GRAPH_ACCESS_WRITE;
	if ( R_RenderGraph_ResourceUnused( graph, sceneColor ) ) {
		access |= RENDER_GRAPH_ACCESS_CLEAR;
	}
	R_RenderGraph_AddAccess( graph, passIndex, sceneColor, access, usage );
}

static void R_RenderGraph_AddSceneColorReadWrite( idRenderGraph &graph, int passIndex, const char *usage ) {
	const int sceneColor = R_RenderGraph_EnsureSceneColor( graph );
	unsigned int access = RENDER_GRAPH_ACCESS_READ | RENDER_GRAPH_ACCESS_WRITE;
	if ( R_RenderGraph_ResourceUnused( graph, sceneColor ) ) {
		access |= RENDER_GRAPH_ACCESS_CLEAR;
	}
	R_RenderGraph_AddAccess( graph, passIndex, sceneColor, access, usage );
}

static void R_RenderGraph_AddPassResources( idRenderGraph &graph, int passIndex, renderPassCategory_t category ) {
	switch ( category ) {
	case RENDER_PASS_DEPTH: {
		const int sceneDepth = R_RenderGraph_EnsureSceneDepth( graph );
		R_RenderGraph_AddAccess( graph, passIndex, sceneDepth, RENDER_GRAPH_ACCESS_WRITE | RENDER_GRAPH_ACCESS_CLEAR, "depth-write" );
		break;
	}
	case RENDER_PASS_STENCIL_SHADOW: {
		const int sceneDepth = R_RenderGraph_EnsureSceneDepth( graph );
		const int shadowStencil = R_RenderGraph_EnsureResource( graph, "shadowStencil", RENDER_GRAPH_RESOURCE_DEPTH_STENCIL, false, true, false, 2 );
		R_RenderGraph_AddAccess( graph, passIndex, sceneDepth, RENDER_GRAPH_ACCESS_READ, "shadow-depth-read" );
		R_RenderGraph_AddAccess( graph, passIndex, shadowStencil, RENDER_GRAPH_ACCESS_WRITE | RENDER_GRAPH_ACCESS_CLEAR, "stencil-shadow-write" );
		break;
	}
	case RENDER_PASS_SHADOW_MAP: {
		const int shadowMap = R_RenderGraph_EnsureResource( graph, "shadowMap", RENDER_GRAPH_RESOURCE_DEPTH, false, true, false, 2 );
		const int projectedShadowAtlas = R_RenderGraph_EnsureResource( graph, "projectedShadowAtlas", RENDER_GRAPH_RESOURCE_DEPTH, false, true, false, 2 );
		const int pointShadowAtlas = R_RenderGraph_EnsureResource( graph, "pointShadowAtlas", RENDER_GRAPH_RESOURCE_DEPTH, false, true, false, 2 );
		const int cascadeShadowAtlas = R_RenderGraph_EnsureResource( graph, "cascadeShadowAtlas", RENDER_GRAPH_RESOURCE_DEPTH, false, true, false, 2 );
		const int shadowDescriptors = R_RenderGraph_EnsureResource( graph, "shadowDescriptors", RENDER_GRAPH_RESOURCE_BUFFER, false, true, false, 0 );
		R_RenderGraph_AddAccess( graph, passIndex, shadowMap, RENDER_GRAPH_ACCESS_WRITE | RENDER_GRAPH_ACCESS_CLEAR, "shadow-map-write" );
		R_RenderGraph_AddAccess( graph, passIndex, projectedShadowAtlas, RENDER_GRAPH_ACCESS_WRITE | RENDER_GRAPH_ACCESS_CLEAR, "shadow-projected-atlas-write" );
		R_RenderGraph_AddAccess( graph, passIndex, pointShadowAtlas, RENDER_GRAPH_ACCESS_WRITE | RENDER_GRAPH_ACCESS_CLEAR, "shadow-point-atlas-write" );
		R_RenderGraph_AddAccess( graph, passIndex, cascadeShadowAtlas, RENDER_GRAPH_ACCESS_WRITE | RENDER_GRAPH_ACCESS_CLEAR, "shadow-cascade-atlas-write" );
		R_RenderGraph_AddAccess( graph, passIndex, shadowDescriptors, RENDER_GRAPH_ACCESS_WRITE, "shadow-descriptor-write" );
		if ( r_shadowMapTranslucentMoments.GetBool() ) {
			const int translucentShadowMoments = R_RenderGraph_EnsureResource( graph, "translucentShadowMoments", RENDER_GRAPH_RESOURCE_COLOR, false, true, false, 5 );
			R_RenderGraph_AddAccess( graph, passIndex, translucentShadowMoments, RENDER_GRAPH_ACCESS_WRITE | RENDER_GRAPH_ACCESS_CLEAR, "shadow-translucent-moment-write" );
		}
		break;
	}
	case RENDER_PASS_ARB2_INTERACTION: {
		const int sceneDepth = R_RenderGraph_EnsureSceneDepth( graph );
		R_RenderGraph_AddAccess( graph, passIndex, sceneDepth, RENDER_GRAPH_ACCESS_READ, "interaction-depth-read" );
		R_RenderGraph_AddSceneColorWrite( graph, passIndex, "interaction-color-write" );
		break;
	}
	case RENDER_PASS_LIGHT_GRID: {
		const int sceneDepth = R_RenderGraph_EnsureSceneDepth( graph );
		const int lightGrid = R_RenderGraph_EnsureResource( graph, "lightGrid", RENDER_GRAPH_RESOURCE_BUFFER, true, false, false, 0 );
		R_RenderGraph_AddAccess( graph, passIndex, sceneDepth, RENDER_GRAPH_ACCESS_READ, "light-grid-depth-read" );
		R_RenderGraph_AddAccess( graph, passIndex, lightGrid, RENDER_GRAPH_ACCESS_READ, "light-grid-sample-read" );
		R_RenderGraph_AddSceneColorReadWrite( graph, passIndex, "light-grid-color" );
		break;
	}
	case RENDER_PASS_AMBIENT: {
		const int sceneDepth = R_RenderGraph_EnsureSceneDepth( graph );
		R_RenderGraph_AddAccess( graph, passIndex, sceneDepth, RENDER_GRAPH_ACCESS_READ, "ambient-depth-read" );
		R_RenderGraph_AddSceneColorReadWrite( graph, passIndex, "ambient-color" );
		if ( R_RenderGraph_ShouldModelGBuffer() ) {
			R_RenderGraph_AddGBufferWrites( graph, passIndex );
		}
		break;
	}
	case RENDER_PASS_FOG_BLEND: {
		const int sceneDepth = R_RenderGraph_EnsureSceneDepth( graph );
		R_RenderGraph_AddAccess( graph, passIndex, sceneDepth, RENDER_GRAPH_ACCESS_READ, "fog-depth-read" );
		R_RenderGraph_AddSceneColorReadWrite( graph, passIndex, "fog-color" );
		break;
	}
	case RENDER_PASS_SSAO: {
		const int sceneDepth = R_RenderGraph_EnsureSceneDepth( graph );
		const int ssao = R_RenderGraph_EnsureResource( graph, "ssao", RENDER_GRAPH_RESOURCE_COLOR, false, true, false, 1 );
		R_RenderGraph_AddAccess( graph, passIndex, sceneDepth, RENDER_GRAPH_ACCESS_READ, "ssao-depth-read" );
		R_RenderGraph_AddAccess( graph, passIndex, ssao, RENDER_GRAPH_ACCESS_WRITE | RENDER_GRAPH_ACCESS_CLEAR | RENDER_GRAPH_ACCESS_INVALIDATE, "ssao-write" );
		break;
	}
	case RENDER_PASS_MOTION_BLUR: {
		const int sceneDepth = R_RenderGraph_EnsureSceneDepth( graph );
		const int sceneColor = R_RenderGraph_EnsureSceneColor( graph );
		const int motionVectors = R_RenderGraph_EnsureResource( graph, "motionVectors", RENDER_GRAPH_RESOURCE_COLOR, false, true, false, 1 );
		const int postA = R_RenderGraph_EnsurePostA( graph );
		R_RenderGraph_AddAccess( graph, passIndex, sceneColor, RENDER_GRAPH_ACCESS_READ, "motion-color-read" );
		R_RenderGraph_AddAccess( graph, passIndex, sceneDepth, RENDER_GRAPH_ACCESS_READ, "motion-depth-read" );
		R_RenderGraph_AddAccess( graph, passIndex, motionVectors, RENDER_GRAPH_ACCESS_READ, "motion-vector-read" );
		R_RenderGraph_AddAccess( graph, passIndex, postA, RENDER_GRAPH_ACCESS_WRITE | RENDER_GRAPH_ACCESS_CLEAR | RENDER_GRAPH_ACCESS_INVALIDATE, "motion-post-write" );
		break;
	}
	case RENDER_PASS_LENS_FLARE: {
		const int sceneDepth = R_RenderGraph_EnsureSceneDepth( graph );
		R_RenderGraph_AddAccess( graph, passIndex, sceneDepth, RENDER_GRAPH_ACCESS_READ, "lens-depth-read" );
		R_RenderGraph_AddSceneColorReadWrite( graph, passIndex, "lens-color" );
		break;
	}
	case RENDER_PASS_BLOOM: {
		const int sceneColor = R_RenderGraph_EnsureSceneColor( graph );
		const int bloomChain = R_RenderGraph_EnsureResource( graph, "bloomChain", RENDER_GRAPH_RESOURCE_COLOR, false, true, false, 1 );
		R_RenderGraph_AddAccess( graph, passIndex, sceneColor, RENDER_GRAPH_ACCESS_READ, "bloom-source-read" );
		R_RenderGraph_AddAccess( graph, passIndex, bloomChain, RENDER_GRAPH_ACCESS_WRITE | RENDER_GRAPH_ACCESS_CLEAR | RENDER_GRAPH_ACCESS_INVALIDATE, "bloom-chain-write" );
		R_RenderGraph_AddAccess( graph, passIndex, sceneColor, RENDER_GRAPH_ACCESS_WRITE | RENDER_GRAPH_ACCESS_RESOLVE, "bloom-resolve" );
		break;
	}
	case RENDER_PASS_AUTHORED_POST: {
		const int sceneColor = R_RenderGraph_EnsureSceneColor( graph );
		const int postA = R_RenderGraph_EnsurePostA( graph );
		R_RenderGraph_AddAccess( graph, passIndex, sceneColor, RENDER_GRAPH_ACCESS_READ, "post-source-read" );
		R_RenderGraph_AddAccess( graph, passIndex, postA, RENDER_GRAPH_ACCESS_WRITE | RENDER_GRAPH_ACCESS_CLEAR | RENDER_GRAPH_ACCESS_INVALIDATE, "post-transient-write" );
		R_RenderGraph_AddAccess( graph, passIndex, sceneColor, RENDER_GRAPH_ACCESS_WRITE | RENDER_GRAPH_ACCESS_RESOLVE, "post-resolve" );
		break;
	}
	case RENDER_PASS_SPECIAL_EFFECTS: {
		const int sceneDepth = R_RenderGraph_EnsureSceneDepth( graph );
		R_RenderGraph_AddSceneColorReadWrite( graph, passIndex, "special-effects-color" );
		R_RenderGraph_AddAccess( graph, passIndex, sceneDepth, RENDER_GRAPH_ACCESS_READ | RENDER_GRAPH_ACCESS_INVALIDATE, "special-effects-depth-read" );
		break;
	}
	case RENDER_PASS_GUI: {
		const int backBuffer = R_RenderGraph_EnsureBackBuffer( graph );
		unsigned int access = RENDER_GRAPH_ACCESS_WRITE;
		if ( R_RenderGraph_ResourceUnused( graph, backBuffer ) ) {
			access |= RENDER_GRAPH_ACCESS_CLEAR;
		}
		R_RenderGraph_AddAccess( graph, passIndex, backBuffer, access, "gui-backbuffer-write" );
		break;
	}
	case RENDER_PASS_PRESENT: {
		const int sceneColor = graph.FindResource( "sceneColor" );
		const int deferredLight = graph.FindResource( "deferredLight" );
		const int postA = graph.FindResource( "postA" );
		const int backBuffer = R_RenderGraph_EnsureBackBuffer( graph );
		if ( R_RenderGraph_ShouldModelModernVisible() ) {
			if ( deferredLight >= 0 && graph.Resource( deferredLight ).written ) {
				R_RenderGraph_AddAccess( graph, passIndex, deferredLight, RENDER_GRAPH_ACCESS_READ | RENDER_GRAPH_ACCESS_INVALIDATE, "modern-present-deferred-read" );
			}
			if ( sceneColor >= 0 && graph.Resource( sceneColor ).written ) {
				R_RenderGraph_AddAccess( graph, passIndex, sceneColor, RENDER_GRAPH_ACCESS_READ | RENDER_GRAPH_ACCESS_INVALIDATE, "modern-present-forward-read" );
			}
			if ( postA >= 0 && graph.Resource( postA ).written ) {
				R_RenderGraph_AddAccess( graph, passIndex, postA, RENDER_GRAPH_ACCESS_READ | RENDER_GRAPH_ACCESS_INVALIDATE, "modern-present-post-read" );
			}
			R_RenderGraph_AddAccess( graph, passIndex, backBuffer, RENDER_GRAPH_ACCESS_WRITE | RENDER_GRAPH_ACCESS_RESOLVE, "modern-present-resolve" );
		} else if ( sceneColor >= 0 && graph.Resource( sceneColor ).written ) {
			R_RenderGraph_AddAccess( graph, passIndex, sceneColor, RENDER_GRAPH_ACCESS_READ | RENDER_GRAPH_ACCESS_INVALIDATE, "present-scene-read" );
			R_RenderGraph_AddAccess( graph, passIndex, backBuffer, RENDER_GRAPH_ACCESS_WRITE | RENDER_GRAPH_ACCESS_RESOLVE, "present-resolve" );
		}
		R_RenderGraph_AddAccess( graph, passIndex, backBuffer, RENDER_GRAPH_ACCESS_READ | RENDER_GRAPH_ACCESS_PRESENT, "present" );
		break;
	}
	default:
		break;
	}
}

static void R_RenderGraph_AddPassOnce( idRenderGraph &graph, renderPassCategory_t category, const char *name ) {
	if ( !R_RenderGraph_HasPass( graph, category ) && graph.AddPass( category, name, true, true ) ) {
		R_RenderGraph_AddPassResources( graph, graph.NumPasses() - 1, category );
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
		case RC_COPY_RENDER:
			R_RenderGraph_AddPassOnce( graph, RENDER_PASS_AUTHORED_POST, "legacyRenderTargetOps" );
			break;
		case RC_SWAP_BUFFERS:
			if ( !R_RenderGraph_ShouldModelModernVisible() ) {
				R_RenderGraph_AddPassOnce( graph, RENDER_PASS_PRESENT, "legacyPresent" );
			}
			break;
		default:
			break;
		}
	}

	R_RenderGraph_AddDeferredResolvePass( graph );
	R_RenderGraph_AddForwardPlusPass( graph );
	if ( R_RenderGraph_ShouldModelModernVisible() ) {
		R_RenderGraph_AddPassOnce( graph, RENDER_PASS_PRESENT, "modernPresent" );
	}
}

void R_RenderGraph_BuildFromScenePackets( const idScenePacketFrame &packetFrame, idRenderGraph &graph ) {
	graph.Clear();

	for ( int i = 0; i < packetFrame.NumPasses(); ++i ) {
		const passPacket_t &pass = packetFrame.Pass( i );
		if ( pass.passCategory == RENDER_PASS_PRESENT && R_RenderGraph_ShouldModelModernVisible() ) {
			continue;
		}
		const int commandPackets = pass.commandOnly ? 1 : 0;
		if ( graph.AddPacketPass( pass.passCategory, R_RenderGraph_LegacyPassName( pass.passCategory ), pass.drawPacketCount, commandPackets ) ) {
			R_RenderGraph_AddPassResources( graph, graph.NumPasses() - 1, pass.passCategory );
		}
	}
	R_RenderGraph_AddDeferredResolvePass( graph );
	R_RenderGraph_AddForwardPlusPass( graph );
	if ( R_RenderGraph_ShouldModelModernVisible() ) {
		R_RenderGraph_AddPassOnce( graph, RENDER_PASS_PRESENT, "modernPresent" );
	}

	const scenePacketFrameStats_t &packetStats = packetFrame.Stats();
	graph.SetPacketFrameStats( packetStats.scenePackets, packetStats.commandPackets, packetStats.overflow );
}

void R_RenderGraph_LogIfVerbose( const idRenderGraph &graph ) {
	if ( r_rendererMetrics.GetInteger() < 2 ) {
		return;
	}

	const renderGraphStats_t &stats = graph.Stats();
	common->Printf(
		"renderGraph resources passes=%d passPackets=%d scenes=%d draws=%d cmds=%d resources=%d imported=%d transient=%d aliasable=%d accesses=%d read=%d write=%d clear=%d resolve=%d invalidate=%d present=%d overflow=%d:",
		graph.NumPasses(),
		stats.passPackets,
		stats.scenePackets,
		stats.drawPackets,
		stats.commandPackets,
		stats.resources,
		stats.importedResources,
		stats.transientResources,
		stats.aliasableTransientResources,
		stats.resourceAccesses,
		stats.readAccesses,
		stats.writeAccesses,
		stats.clearOps,
		stats.resolveOps,
		stats.invalidateOps,
		stats.presentOps,
		stats.overflow ? 1 : 0 );
	for ( int i = 0; i < graph.NumPasses(); ++i ) {
		const renderGraphPass_t &pass = graph.Pass( i );
		common->Printf(
			" %s%s[p=%d d=%d r=%d w=%d c=%d x=%d i=%d]",
			pass.name ? pass.name : RenderPassCategory_Name( pass.category ),
			pass.enabled ? "" : "(off)",
			pass.passPacketCount,
			pass.drawPacketCount,
			pass.readResourceCount,
			pass.writeResourceCount,
			pass.clearCount,
			pass.resolveCount,
			pass.invalidateCount );
	}
	common->Printf( "\n" );

	for ( int i = 0; i < graph.NumResources() && i < 12; ++i ) {
		const renderGraphResource_t &resource = graph.Resource( i );
		common->Printf(
			"renderGraph resource[%d]=%s type=%s imported=%d transient=%d presentable=%d alias=%d lifetime=%d..%d producer=%d lastWriter=%d read=%d write=%d clear=%d resolve=%d invalidate=%d present=%d\n",
			i,
			resource.name ? resource.name : "<unnamed>",
			R_RenderGraph_ResourceTypeName( resource.type ),
			resource.imported ? 1 : 0,
			resource.transient ? 1 : 0,
			resource.presentable ? 1 : 0,
			resource.aliasGroup,
			resource.firstPass,
			resource.lastPass,
			resource.producerPass,
			resource.lastWriterPass,
			resource.read ? 1 : 0,
			resource.written ? 1 : 0,
			resource.cleared ? 1 : 0,
			resource.resolved ? 1 : 0,
			resource.invalidated ? 1 : 0,
			resource.presented ? 1 : 0 );
	}
}

static bool R_RenderGraph_CheckPass( const idRenderGraph &graph, int index, renderPassCategory_t category, int passPackets, int drawPackets ) {
	if ( index < 0 || index >= graph.NumPasses() ) {
		return false;
	}
	const renderGraphPass_t &pass = graph.Pass( index );
	return pass.category == category && pass.passPacketCount == passPackets && pass.drawPacketCount == drawPackets && pass.packetBacked && pass.resourceBacked;
}

static bool R_RenderGraph_CheckResourceStats( const idRenderGraph &graph, int resources, int imported, int transient, int aliasable, int accesses, int reads, int writes, int clears, int resolves, int invalidates, int presents ) {
	const renderGraphStats_t &stats = graph.Stats();
	return stats.resources == resources
		&& stats.importedResources == imported
		&& stats.transientResources == transient
		&& stats.aliasableTransientResources == aliasable
		&& stats.resourceAccesses == accesses
		&& stats.readAccesses == reads
		&& stats.writeAccesses == writes
		&& stats.clearOps == clears
		&& stats.resolveOps == resolves
		&& stats.invalidateOps == invalidates
		&& stats.presentOps == presents;
}

static bool R_RenderGraph_CheckResource( const idRenderGraph &graph, const char *name, renderGraphResourceType_t type, bool imported, bool transient, bool presentable, int aliasGroup ) {
	const int resourceIndex = graph.FindResource( name );
	if ( resourceIndex < 0 ) {
		return false;
	}
	const renderGraphResource_t &resource = graph.Resource( resourceIndex );
	return resource.type == type
		&& resource.imported == imported
		&& resource.transient == transient
		&& resource.presentable == presentable
		&& resource.aliasGroup == aliasGroup;
}

static bool R_RenderGraph_CheckAccess( const idRenderGraph &graph, int passIndex, const char *resourceName, unsigned int requiredAccess ) {
	if ( passIndex < 0 || passIndex >= graph.NumPasses() ) {
		return false;
	}
	const int resourceIndex = graph.FindResource( resourceName );
	if ( resourceIndex < 0 ) {
		return false;
	}
	const renderGraphPass_t &pass = graph.Pass( passIndex );
	for ( int i = 0; i < pass.resourceAccessCount; ++i ) {
		const int accessIndex = pass.firstResourceAccess + i;
		if ( accessIndex < 0 || accessIndex >= graph.NumResourceAccesses() ) {
			return false;
		}
		const renderGraphResourceAccess_t &access = graph.ResourceAccess( accessIndex );
		if ( access.resourceIndex == resourceIndex && ( access.access & requiredAccess ) == requiredAccess ) {
			return true;
		}
	}
	return false;
}

static bool R_RenderGraph_RunWorldPacketSelfTest( void ) {
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
	idRenderGraph graph;
	R_RenderGraph_BuildFromScenePackets( packetFrame, graph );
	const renderGraphStats_t &stats = graph.Stats();
	if ( graph.NumPasses() != 8 || stats.passPackets != 8 || stats.scenePackets != 3 || stats.drawPackets != 12 || stats.commandPackets != 2 || stats.overflow ) {
		common->Printf(
			"RendererRenderGraph self-test detail: world pass stats passes=%d passPackets=%d scenes=%d draws=%d cmds=%d overflow=%d\n",
			graph.NumPasses(),
			stats.passPackets,
			stats.scenePackets,
			stats.drawPackets,
			stats.commandPackets,
			stats.overflow ? 1 : 0 );
		return false;
	}
	if ( !R_RenderGraph_CheckResourceStats( graph, 5, 2, 3, 3, 18, 13, 9, 3, 2, 3, 1 ) ) {
		common->Printf(
			"RendererRenderGraph self-test detail: world resources res=%d imported=%d transient=%d aliasable=%d access=%d read=%d write=%d clear=%d resolve=%d invalidate=%d present=%d\n",
			stats.resources,
			stats.importedResources,
			stats.transientResources,
			stats.aliasableTransientResources,
			stats.resourceAccesses,
			stats.readAccesses,
			stats.writeAccesses,
			stats.clearOps,
			stats.resolveOps,
			stats.invalidateOps,
			stats.presentOps );
		return false;
	}
	if ( !R_RenderGraph_CheckResource( graph, "sceneDepth", RENDER_GRAPH_RESOURCE_DEPTH_STENCIL, false, true, false, 2 )
		|| !R_RenderGraph_CheckResource( graph, "sceneColor", RENDER_GRAPH_RESOURCE_COLOR, false, true, false, 1 )
		|| !R_RenderGraph_CheckResource( graph, "postA", RENDER_GRAPH_RESOURCE_COLOR, false, true, false, 1 )
		|| !R_RenderGraph_CheckResource( graph, "lightGrid", RENDER_GRAPH_RESOURCE_BUFFER, true, false, false, 0 )
		|| !R_RenderGraph_CheckResource( graph, "backBuffer", RENDER_GRAPH_RESOURCE_COLOR, true, false, true, 0 ) ) {
		common->Printf( "RendererRenderGraph self-test detail: world resource declaration mismatch\n" );
		return false;
	}
	const int sceneColor = graph.FindResource( "sceneColor" );
	const int backBuffer = graph.FindResource( "backBuffer" );
	if ( sceneColor < 0 || backBuffer < 0 || graph.Resource( sceneColor ).firstPass != 1 || graph.Resource( sceneColor ).lastPass != 7 || !graph.Resource( backBuffer ).presented ) {
		common->Printf(
			"RendererRenderGraph self-test detail: world lifetime sceneColor=%d backBuffer=%d sceneColorLife=%d..%d backBufferPresented=%d\n",
			sceneColor,
			backBuffer,
			sceneColor >= 0 ? graph.Resource( sceneColor ).firstPass : -1,
			sceneColor >= 0 ? graph.Resource( sceneColor ).lastPass : -1,
			backBuffer >= 0 && graph.Resource( backBuffer ).presented ? 1 : 0 );
		return false;
	}
	if ( !R_RenderGraph_CheckAccess( graph, 0, "sceneDepth", RENDER_GRAPH_ACCESS_WRITE | RENDER_GRAPH_ACCESS_CLEAR )
		|| !R_RenderGraph_CheckAccess( graph, 1, "sceneDepth", RENDER_GRAPH_ACCESS_READ )
		|| !R_RenderGraph_CheckAccess( graph, 1, "sceneColor", RENDER_GRAPH_ACCESS_WRITE | RENDER_GRAPH_ACCESS_CLEAR )
		|| !R_RenderGraph_CheckAccess( graph, 2, "lightGrid", RENDER_GRAPH_ACCESS_READ )
		|| !R_RenderGraph_CheckAccess( graph, 5, "postA", RENDER_GRAPH_ACCESS_WRITE | RENDER_GRAPH_ACCESS_CLEAR )
		|| !R_RenderGraph_CheckAccess( graph, 5, "postA", RENDER_GRAPH_ACCESS_INVALIDATE )
		|| !R_RenderGraph_CheckAccess( graph, 5, "sceneColor", RENDER_GRAPH_ACCESS_WRITE | RENDER_GRAPH_ACCESS_RESOLVE )
		|| !R_RenderGraph_CheckAccess( graph, 6, "sceneDepth", RENDER_GRAPH_ACCESS_INVALIDATE )
		|| !R_RenderGraph_CheckAccess( graph, 7, "sceneColor", RENDER_GRAPH_ACCESS_READ )
		|| !R_RenderGraph_CheckAccess( graph, 7, "sceneColor", RENDER_GRAPH_ACCESS_INVALIDATE )
		|| !R_RenderGraph_CheckAccess( graph, 7, "backBuffer", RENDER_GRAPH_ACCESS_WRITE | RENDER_GRAPH_ACCESS_RESOLVE )
		|| !R_RenderGraph_CheckAccess( graph, 7, "backBuffer", RENDER_GRAPH_ACCESS_READ | RENDER_GRAPH_ACCESS_PRESENT ) ) {
		common->Printf( "RendererRenderGraph self-test detail: world access edge mismatch\n" );
		return false;
	}

	return
		R_RenderGraph_CheckPass( graph, 0, RENDER_PASS_DEPTH, 1, 2 ) &&
		R_RenderGraph_CheckPass( graph, 1, RENDER_PASS_ARB2_INTERACTION, 1, 2 ) &&
		R_RenderGraph_CheckPass( graph, 2, RENDER_PASS_LIGHT_GRID, 1, 2 ) &&
		R_RenderGraph_CheckPass( graph, 3, RENDER_PASS_AMBIENT, 1, 2 ) &&
		R_RenderGraph_CheckPass( graph, 4, RENDER_PASS_FOG_BLEND, 1, 2 ) &&
		R_RenderGraph_CheckPass( graph, 5, RENDER_PASS_AUTHORED_POST, 1, 2 ) &&
		R_RenderGraph_CheckPass( graph, 6, RENDER_PASS_SPECIAL_EFFECTS, 1, 0 ) &&
		R_RenderGraph_CheckPass( graph, 7, RENDER_PASS_PRESENT, 1, 0 );
}

static bool R_RenderGraph_RunGuiPacketSelfTest( void ) {
	drawSurf_t drawSurf;
	memset( &drawSurf, 0, sizeof( drawSurf ) );
	drawSurf_t *drawSurfPtrs[1] = { &drawSurf };
	viewDef_t guiView;
	memset( &guiView, 0, sizeof( guiView ) );
	guiView.viewEntitys = NULL;
	guiView.drawSurfs = drawSurfPtrs;
	guiView.numDrawSurfs = 1;

	drawSurfsCommand_t drawCmd;
	memset( &drawCmd, 0, sizeof( drawCmd ) );
	drawCmd.commandId = RC_DRAW_VIEW;
	drawCmd.viewDef = &guiView;
	emptyCommand_t swapCmd;
	memset( &swapCmd, 0, sizeof( swapCmd ) );
	swapCmd.commandId = RC_SWAP_BUFFERS;
	drawCmd.next = &swapCmd.commandId;
	swapCmd.next = NULL;

	idScenePacketFrame packetFrame;
	R_ScenePackets_BuildLegacyCommandStream( reinterpret_cast<const emptyCommand_t *>( &drawCmd ), packetFrame );
	idRenderGraph graph;
	R_RenderGraph_BuildFromScenePackets( packetFrame, graph );
	const renderGraphStats_t &stats = graph.Stats();
	if ( graph.NumPasses() != 2 || stats.passPackets != 2 || stats.scenePackets != 2 || stats.drawPackets != 1 || stats.commandPackets != 1 || stats.overflow ) {
		return false;
	}
	if ( !R_RenderGraph_CheckResourceStats( graph, 1, 1, 0, 0, 2, 1, 1, 1, 0, 0, 1 ) ) {
		common->Printf(
			"RendererRenderGraph self-test detail: gui resources res=%d imported=%d transient=%d aliasable=%d access=%d read=%d write=%d clear=%d resolve=%d invalidate=%d present=%d\n",
			stats.resources,
			stats.importedResources,
			stats.transientResources,
			stats.aliasableTransientResources,
			stats.resourceAccesses,
			stats.readAccesses,
			stats.writeAccesses,
			stats.clearOps,
			stats.resolveOps,
			stats.invalidateOps,
			stats.presentOps );
		return false;
	}
	if ( !R_RenderGraph_CheckResource( graph, "backBuffer", RENDER_GRAPH_RESOURCE_COLOR, true, false, true, 0 )
		|| !R_RenderGraph_CheckAccess( graph, 0, "backBuffer", RENDER_GRAPH_ACCESS_WRITE | RENDER_GRAPH_ACCESS_CLEAR )
		|| !R_RenderGraph_CheckAccess( graph, 1, "backBuffer", RENDER_GRAPH_ACCESS_READ | RENDER_GRAPH_ACCESS_PRESENT ) ) {
		common->Printf( "RendererRenderGraph self-test detail: gui resource edge mismatch\n" );
		return false;
	}

	return
		R_RenderGraph_CheckPass( graph, 0, RENDER_PASS_GUI, 1, 1 ) &&
		R_RenderGraph_CheckPass( graph, 1, RENDER_PASS_PRESENT, 1, 0 );
}

bool RendererRenderGraph_RunSelfTest( void ) {
	if ( !R_RenderGraph_RunWorldPacketSelfTest() ) {
		common->Printf( "RendererRenderGraph self-test failed: world resource graph mismatch\n" );
		return false;
	}
	if ( !R_RenderGraph_RunGuiPacketSelfTest() ) {
		common->Printf( "RendererRenderGraph self-test failed: gui resource graph mismatch\n" );
		return false;
	}
	common->Printf( "RendererRenderGraph self-test passed (resource graph, 2 cases)\n" );
	return true;
}
