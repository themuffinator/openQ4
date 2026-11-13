// Copyright (C) 2026 DarkMatter Productions
//

#include "tr_local.h"
#include "ClassicCinematicPostDomain.h"
#include "ClassicSubviewDomain.h"

#include <cmath>
#include <cstring>
#include <limits>

static_assert( static_cast<int>( CLASSIC_CINEMATIC_POST_BACKEND_GL )
		== static_cast<int>( CLASSIC_SUBVIEW_DOMAIN_BACKEND_GL ),
	"classic cinematic/post and subview GL backend ordinals must match" );
static_assert( static_cast<int>( CLASSIC_CINEMATIC_POST_BACKEND_VULKAN )
		== static_cast<int>( CLASSIC_SUBVIEW_DOMAIN_BACKEND_VULKAN ),
	"classic cinematic/post and subview Vulkan backend ordinals must match" );
static_assert( static_cast<int>( CLASSIC_CINEMATIC_POST_BACKEND_COUNT )
		== static_cast<int>( CLASSIC_SUBVIEW_DOMAIN_BACKEND_COUNT ),
	"classic cinematic/post and subview backend counts must match" );

typedef struct classicCinematicPostDomainState_s {
	classicCinematicPostDomainView_t	views[
		CLASSIC_CINEMATIC_POST_DOMAIN_MAX_VIEWS ];
	int									viewCount;
	classicCinematicPostDomainStats_t			stats;
} classicCinematicPostDomainState_t;

static classicCinematicPostDomainState_t rg_classicCinematicPostDomain;

static bool R_ClassicCinematicPostDomain_RangeFits(
		int first, int count, int capacity ) {
	return first >= 0 && count >= 0 && first <= capacity
		&& count <= capacity - first;
}

static bool R_ClassicCinematicPostDomain_RangeContains(
		int outerFirst, int outerCount, int first, int count ) {
	if ( outerFirst < 0 || outerCount < 0 || first < outerFirst || count < 0 ) {
		return false;
	}
	const int offset = first - outerFirst;
	return offset <= outerCount && count <= outerCount - offset;
}

static std::uint64_t R_ClassicCinematicPostDomain_HashBytes(
		std::uint64_t hash, const void *data, size_t bytes ) {
	const byte *source = static_cast<const byte *>( data );
	for ( size_t i = 0; i < bytes; ++i ) {
		hash ^= source[ i ];
		hash *= UINT64_C( 1099511628211 );
	}
	return hash;
}

static std::uint64_t R_ClassicCinematicPostDomain_HashView(
		const classicCinematicPostDomainView_t &view ) {
	std::uint64_t hash = UINT64_C( 1469598103934665603 );
	hash = R_ClassicCinematicPostDomain_HashBytes( hash, &view.scope,
		sizeof( view.scope ) );
	hash = R_ClassicCinematicPostDomain_HashBytes( hash, &view.scenePacketIndex,
		sizeof( view.scenePacketIndex ) );
	hash = R_ClassicCinematicPostDomain_HashBytes( hash,
		&view.specialRootScenePacketIndex,
		sizeof( view.specialRootScenePacketIndex ) );
	hash = R_ClassicCinematicPostDomain_HashBytes( hash,
		&view.specialNestingDepth, sizeof( view.specialNestingDepth ) );
	hash = R_ClassicCinematicPostDomain_HashBytes( hash,
		&view.nestedInSpecialView, sizeof( view.nestedInSpecialView ) );
	hash = R_ClassicCinematicPostDomain_HashBytes( hash, &view.passPacketIndex,
		sizeof( view.passPacketIndex ) );
	hash = R_ClassicCinematicPostDomain_HashBytes( hash, &view.firstSourceSurface,
		sizeof( view.firstSourceSurface ) );
	hash = R_ClassicCinematicPostDomain_HashBytes( hash, &view.sourceSurfaceCount,
		sizeof( view.sourceSurfaceCount ) );
	hash = R_ClassicCinematicPostDomain_HashBytes( hash, &view.packetDrawCount,
		sizeof( view.packetDrawCount ) );
	hash = R_ClassicCinematicPostDomain_HashBytes( hash, &view.cinematicStageCount,
		sizeof( view.cinematicStageCount ) );
	hash = R_ClassicCinematicPostDomain_HashBytes( hash, &view.currentRenderStageCount,
		sizeof( view.currentRenderStageCount ) );
	hash = R_ClassicCinematicPostDomain_HashBytes( hash, &view.currentDepthStageCount,
		sizeof( view.currentDepthStageCount ) );
	hash = R_ClassicCinematicPostDomain_HashBytes( hash,
		&view.cinematicTimeMilliseconds, sizeof( view.cinematicTimeMilliseconds ) );
	return hash;
}

static bool R_ClassicCinematicPostDomain_IsCurrentRenderImage( const idImage *image ) {
	if ( image == NULL || globalImages == NULL ) {
		return false;
	}
	if ( image == globalImages->currentRenderImage
			|| image == globalImages->originalCurrentRenderImage ) {
		return true;
	}
	const char *name = image->GetName();
	return name != NULL && idStr::Icmpn( name, "_currentRender", 14 ) == 0;
}

static bool R_ClassicCinematicPostDomain_IsCurrentDepthImage( const idImage *image ) {
	if ( image == NULL || globalImages == NULL ) {
		return false;
	}
	if ( image == globalImages->currentDepthImage ) {
		return true;
	}
	const char *name = image->GetName();
	return name != NULL && idStr::Icmpn( name, "_currentDepth", 13 ) == 0;
}

static bool R_ClassicCinematicPostDomain_IsRootCinematicView(
		const viewDef_t *viewDef ) {
	return viewDef != NULL && viewDef->viewEntitys == NULL
		&& !viewDef->isSubview && !viewDef->isMirror && !viewDef->isXraySubview
		&& !viewDef->isEditor && viewDef->superView == NULL
		&& viewDef->subviewSurface == NULL && viewDef->numClipPlanes == 0
		&& viewDef->renderWorld == NULL && viewDef->viewLights == NULL
		&& viewDef->renderFlags == 0 && viewDef->renderView.globalMaterial == NULL
		&& viewDef->numOutlineDrawSurfs == 0;
}

static bool R_ClassicCinematicPostDomain_IsAuthoredPostView(
		const viewDef_t *viewDef ) {
	if ( viewDef == NULL || viewDef->viewEntitys == NULL || viewDef->isEditor
			|| viewDef->renderWorld == NULL || viewDef->renderView.viewID < 0
			|| ( viewDef->renderFlags & RF_PORTAL_SKY ) != 0
			|| viewDef->renderView.globalMaterial != NULL ) {
		return false;
	}
	if ( viewDef->isSubview ) {
		// A dynamic post/video tail inside a special view is admissible only
		// when that view belongs to the independently sealed subview tree. The
		// two domains will publish or roll back the shared root transaction
		// together after every child range and capture edge completes.
		return r_rendererSharedSubview.GetBool()
			&& viewDef->superView != NULL && viewDef->subviewSurface != NULL;
	}
	return !viewDef->isMirror && !viewDef->isXraySubview
		&& viewDef->superView == NULL && viewDef->subviewSurface == NULL
		&& viewDef->numClipPlanes == 0;
}

static bool R_ClassicCinematicPostDomain_StageCounts(
		const drawSurf_t *drawSurf, int &cinematicStages, int &currentRenderStages,
		int &currentDepthStages ) {
	if ( drawSurf == NULL || drawSurf->material == NULL ) {
		return false;
	}
	const idMaterial *material = drawSurf->material;
	const int stageCount = material->GetNumStages();
	for ( int stageIndex = 0; stageIndex < stageCount; ++stageIndex ) {
		const shaderStage_t *stage = material->GetStage( stageIndex );
		if ( stage == NULL ) {
			return false;
		}
		if ( stage->texture.cinematic != NULL ) {
			cinematicStages++;
		}
		if ( material->TestMaterialFlag( MF_NEED_CURRENT_RENDER )
				|| R_ClassicCinematicPostDomain_IsCurrentRenderImage(
					stage->texture.image ) ) {
			currentRenderStages++;
		}
		if ( R_ClassicCinematicPostDomain_IsCurrentDepthImage(
				stage->texture.image ) ) {
			currentDepthStages++;
		}
	}
	return true;
}

// Keep root 2D packet reconciliation in lockstep with the GUI packet builder.
// The dynamic stage walker remains free to retain legacy no-op behavior for
// non-drawable inputs, but every packet-admitted source must retain its exact
// original drawSurf identity and order before this domain can commit.
static bool R_ClassicCinematicPostDomain_IsRootGUIPacketEligible(
		const drawSurf_t *drawSurf ) {
	return drawSurf != NULL && drawSurf->geo != NULL
		&& drawSurf->geo->numIndexes > 0 && drawSurf->space != NULL
		&& drawSurf->material != NULL && drawSurf->material->HasAmbient()
		&& !drawSurf->material->IsPortalSky();
}

static int R_ClassicCinematicPostDomain_FindPass(
		const idScenePacketFrame &packetFrame, const scenePacket_t &scene,
		renderPassCategory_t category ) {
	if ( !R_ClassicCinematicPostDomain_RangeFits( scene.firstPassPacket,
			scene.passPacketCount, packetFrame.NumPasses() ) ) {
		return -1;
	}
	for ( int passOffset = 0; passOffset < scene.passPacketCount; ++passOffset ) {
		const int passIndex = scene.firstPassPacket + passOffset;
		const passPacket_t &pass = packetFrame.Pass( passIndex );
		if ( pass.passCategory == category && !pass.commandOnly ) {
			return passIndex;
		}
	}
	return -1;
}

static void R_ClassicCinematicPostDomain_Fail(
		classicCinematicPostDomainView_t &view,
		classicCinematicPostDomainFailure_t failure, int detail ) {
	view.ready = false;
	view.failure = failure;
	view.failureDetail = detail;
	if ( failure > CLASSIC_CINEMATIC_POST_FAILURE_NONE
			&& failure < CLASSIC_CINEMATIC_POST_FAILURE_COUNT ) {
		rg_classicCinematicPostDomain.stats.failureCounts[ failure ]++;
	}
}

static void R_ClassicCinematicPostDomain_CommitView(
		classicCinematicPostDomainView_t &view ) {
	view.ready = true;
	view.failure = CLASSIC_CINEMATIC_POST_FAILURE_NONE;
	view.failureDetail = 0;
	view.hash = R_ClassicCinematicPostDomain_HashView( view );
	rg_classicCinematicPostDomain.stats.readyViews++;
	rg_classicCinematicPostDomain.stats.cinematicStages += view.cinematicStageCount;
	rg_classicCinematicPostDomain.stats.currentRenderStages += view.currentRenderStageCount;
	rg_classicCinematicPostDomain.stats.currentDepthStages += view.currentDepthStageCount;
}

static bool R_ClassicCinematicPostDomain_AddRootCinematicView(
		const idScenePacketFrame &packetFrame, int sceneIndex,
		const scenePacket_t &scene ) {
	const viewDef_t *viewDef = scene.viewDef;
	if ( !R_ClassicCinematicPostDomain_IsRootCinematicView( viewDef ) ) {
		return true;
	}
	if ( viewDef->numDrawSurfs <= 0 || viewDef->drawSurfs == NULL ) {
		return true;
	}
	int cinematicStages = 0;
	int currentRenderStages = 0;
	int currentDepthStages = 0;
	for ( int sourceIndex = 0; sourceIndex < viewDef->numDrawSurfs; ++sourceIndex ) {
		const drawSurf_t *drawSurf = viewDef->drawSurfs[ sourceIndex ];
		if ( drawSurf == NULL || drawSurf->material == NULL
			|| !R_ClassicCinematicPostDomain_StageCounts( drawSurf,
				cinematicStages, currentRenderStages, currentDepthStages ) ) {
			return true;
		}
		if ( drawSurf->material->GetSort() >= SS_POST_PROCESS ) {
			return true;
		}
	}
	if ( cinematicStages <= 0 ) {
		return true;
	}
	if ( rg_classicCinematicPostDomain.viewCount
			>= CLASSIC_CINEMATIC_POST_DOMAIN_MAX_VIEWS ) {
		rg_classicCinematicPostDomain.stats.overflow = true;
		return false;
	}
	classicCinematicPostDomainView_t &view =
		rg_classicCinematicPostDomain.views[
			rg_classicCinematicPostDomain.viewCount++ ];
	memset( &view, 0, sizeof( view ) );
	view.viewDef = viewDef;
	view.specialRootScenePacketIndex = -1;
	view.scope = CLASSIC_CINEMATIC_POST_SCOPE_ROOT_CINEMATIC;
	view.scenePacketIndex = sceneIndex;
	view.passPacketIndex = R_ClassicCinematicPostDomain_FindPass( packetFrame,
		scene, RENDER_PASS_GUI );
	view.firstSourceSurface = 0;
	view.sourceSurfaceCount = viewDef->numDrawSurfs;
	view.cinematicStageCount = cinematicStages;
	view.currentRenderStageCount = currentRenderStages;
	view.currentDepthStageCount = currentDepthStages;
	const double cinematicTime = 1000.0 * ( static_cast<double>( viewDef->floatTime )
		+ static_cast<double>( viewDef->renderView.shaderParms[ 11 ] ) );
	if ( !std::isfinite( cinematicTime )
			|| cinematicTime < static_cast<double>( ( std::numeric_limits<int>::min )() )
			|| cinematicTime > static_cast<double>( ( std::numeric_limits<int>::max )() ) ) {
		R_ClassicCinematicPostDomain_Fail( view,
			CLASSIC_CINEMATIC_POST_FAILURE_CINEMATIC_CLOCK, 0 );
		return true;
	}
	view.cinematicTimeMilliseconds = static_cast<int>( cinematicTime );
	if ( !R_ClassicCinematicPostDomain_RangeFits( scene.firstPassPacket,
			scene.passPacketCount, packetFrame.NumPasses() )
			|| !R_ClassicCinematicPostDomain_RangeFits( scene.firstDrawPacket,
				scene.drawPacketCount, packetFrame.NumDrawPackets() ) ) {
		R_ClassicCinematicPostDomain_Fail( view,
			CLASSIC_CINEMATIC_POST_FAILURE_INVALID_SCENE_RANGE, sceneIndex );
		return true;
	}
	if ( view.passPacketIndex < 0 ) {
		R_ClassicCinematicPostDomain_Fail( view,
			CLASSIC_CINEMATIC_POST_FAILURE_INVALID_PASS_RANGE, 0 );
		return true;
	}
	const passPacket_t &pass = packetFrame.Pass( view.passPacketIndex );
	if ( pass.packetCategory != SCENE_PACKET_CATEGORY_GUI
			|| !R_ClassicCinematicPostDomain_RangeFits( pass.firstDrawPacket,
				pass.drawPacketCount, packetFrame.NumDrawPackets() )
			|| !R_ClassicCinematicPostDomain_RangeContains( scene.firstDrawPacket,
				scene.drawPacketCount, pass.firstDrawPacket,
				pass.drawPacketCount ) ) {
		R_ClassicCinematicPostDomain_Fail( view,
			CLASSIC_CINEMATIC_POST_FAILURE_INVALID_PASS_RANGE, view.passPacketIndex );
		return true;
	}
	view.packetDrawCount = pass.drawPacketCount;
	int packetCursor = pass.firstDrawPacket;
	const int packetEnd = packetCursor + pass.drawPacketCount;
	for ( int sourceIndex = 0; sourceIndex < viewDef->numDrawSurfs;
			sourceIndex++ ) {
		const drawSurf_t *drawSurf = viewDef->drawSurfs[ sourceIndex ];
		if ( !R_ClassicCinematicPostDomain_IsRootGUIPacketEligible( drawSurf ) ) {
			continue;
		}
		if ( packetCursor >= packetEnd ) {
			R_ClassicCinematicPostDomain_Fail( view,
				CLASSIC_CINEMATIC_POST_FAILURE_SOURCE_PACKET_MISMATCH,
				sourceIndex );
			return true;
		}
		const drawPacket_t &packet = packetFrame.DrawPacket( packetCursor++ );
		if ( packet.legacyDrawSurf != drawSurf
				|| packet.passCategory != RENDER_PASS_GUI
				|| packet.packetCategory != SCENE_PACKET_CATEGORY_GUI ) {
			R_ClassicCinematicPostDomain_Fail( view,
				CLASSIC_CINEMATIC_POST_FAILURE_SOURCE_PACKET_MISMATCH,
				sourceIndex );
			return true;
		}
	}
	if ( packetCursor != packetEnd ) {
		R_ClassicCinematicPostDomain_Fail( view,
			CLASSIC_CINEMATIC_POST_FAILURE_SOURCE_PACKET_MISMATCH,
			packetEnd - packetCursor );
		return true;
	}
	R_ClassicCinematicPostDomain_CommitView( view );
	rg_classicCinematicPostDomain.stats.rootCinematicViews++;
	return true;
}

static bool R_ClassicCinematicPostDomain_AddAuthoredPostView(
		const idScenePacketFrame &packetFrame, int sceneIndex,
		const scenePacket_t &scene ) {
	const viewDef_t *viewDef = scene.viewDef;
	if ( !R_ClassicCinematicPostDomain_IsAuthoredPostView( viewDef )
			|| viewDef->numDrawSurfs <= 0 || viewDef->drawSurfs == NULL ) {
		return true;
	}
	int firstSource = viewDef->numDrawSurfs;
	for ( int sourceIndex = 0; sourceIndex < viewDef->numDrawSurfs; ++sourceIndex ) {
		const drawSurf_t *drawSurf = viewDef->drawSurfs[ sourceIndex ];
		if ( drawSurf != NULL && drawSurf->material != NULL
				&& drawSurf->material->GetSort() >= SS_POST_PROCESS ) {
			firstSource = sourceIndex;
			break;
		}
	}
	if ( firstSource >= viewDef->numDrawSurfs ) {
		return true;
	}
	if ( rg_classicCinematicPostDomain.viewCount
			>= CLASSIC_CINEMATIC_POST_DOMAIN_MAX_VIEWS ) {
		rg_classicCinematicPostDomain.stats.overflow = true;
		return false;
	}
	classicCinematicPostDomainView_t &view =
		rg_classicCinematicPostDomain.views[
			rg_classicCinematicPostDomain.viewCount++ ];
	memset( &view, 0, sizeof( view ) );
	view.viewDef = viewDef;
	view.specialRootScenePacketIndex = -1;
	view.scope = CLASSIC_CINEMATIC_POST_SCOPE_AUTHORED_POST;
	view.scenePacketIndex = sceneIndex;
	if ( viewDef->isSubview ) {
		const classicSubviewDomainView_t *specialView =
			R_ClassicSubviewDomain_FindView( viewDef );
		const classicSubviewDomainView_t *specialRoot = specialView != NULL
			? R_ClassicSubviewDomain_ViewByIndex( specialView->rootViewIndex )
			: NULL;
		if ( specialView == NULL || specialRoot == NULL || !specialView->ready
				|| !specialRoot->ready || specialRoot->viewDef == NULL ) {
			R_ClassicCinematicPostDomain_Fail( view,
				CLASSIC_CINEMATIC_POST_FAILURE_MISSING_SPECIAL_VIEW,
				sceneIndex );
			return true;
		}
		view.nestedInSpecialView = true;
		view.specialRootViewDef = specialRoot->viewDef;
		view.specialRootScenePacketIndex = specialRoot->scenePacketIndex;
		view.specialNestingDepth = specialView->nestingDepth;
	}
	view.passPacketIndex = R_ClassicCinematicPostDomain_FindPass( packetFrame,
		scene, RENDER_PASS_AUTHORED_POST );
	view.firstSourceSurface = firstSource;
	view.sourceSurfaceCount = viewDef->numDrawSurfs - firstSource;
	if ( !R_ClassicCinematicPostDomain_RangeFits( scene.firstPassPacket,
			scene.passPacketCount, packetFrame.NumPasses() )
			|| !R_ClassicCinematicPostDomain_RangeFits( scene.firstDrawPacket,
				scene.drawPacketCount, packetFrame.NumDrawPackets() ) ) {
		R_ClassicCinematicPostDomain_Fail( view,
			CLASSIC_CINEMATIC_POST_FAILURE_INVALID_SCENE_RANGE, sceneIndex );
		return true;
	}
	if ( view.passPacketIndex < 0 ) {
		R_ClassicCinematicPostDomain_Fail( view,
			CLASSIC_CINEMATIC_POST_FAILURE_INVALID_PASS_RANGE, 0 );
		return true;
	}
	const passPacket_t &pass = packetFrame.Pass( view.passPacketIndex );
	if ( pass.packetCategory != SCENE_PACKET_CATEGORY_POST_PROCESS
			|| !R_ClassicCinematicPostDomain_RangeFits( pass.firstDrawPacket,
				pass.drawPacketCount, packetFrame.NumDrawPackets() )
			|| !R_ClassicCinematicPostDomain_RangeContains( scene.firstDrawPacket,
				scene.drawPacketCount, pass.firstDrawPacket,
				pass.drawPacketCount ) ) {
		R_ClassicCinematicPostDomain_Fail( view,
			CLASSIC_CINEMATIC_POST_FAILURE_INVALID_PASS_RANGE, view.passPacketIndex );
		return true;
	}
	view.packetDrawCount = pass.drawPacketCount;
	int packetCursor = pass.firstDrawPacket;
	const int packetEnd = packetCursor + pass.drawPacketCount;
	for ( int sourceIndex = firstSource; sourceIndex < viewDef->numDrawSurfs;
			++sourceIndex ) {
		const drawSurf_t *drawSurf = viewDef->drawSurfs[ sourceIndex ];
		if ( drawSurf == NULL || drawSurf->material == NULL
				|| drawSurf->material->GetSort() < SS_POST_PROCESS ) {
			R_ClassicCinematicPostDomain_Fail( view,
				CLASSIC_CINEMATIC_POST_FAILURE_INVALID_SOURCE_SURFACE, sourceIndex );
			return true;
		}
		if ( !R_ClassicCinematicPostDomain_StageCounts( drawSurf,
				view.cinematicStageCount, view.currentRenderStageCount,
				view.currentDepthStageCount ) ) {
			R_ClassicCinematicPostDomain_Fail( view,
				CLASSIC_CINEMATIC_POST_FAILURE_INVALID_SOURCE_SURFACE, sourceIndex );
			return true;
		}
		if ( drawSurf->material->HasAmbient() && !drawSurf->material->IsPortalSky()
				&& !drawSurf->material->SuppressInSubview() ) {
			if ( packetCursor >= packetEnd ) {
				R_ClassicCinematicPostDomain_Fail( view,
					CLASSIC_CINEMATIC_POST_FAILURE_SOURCE_PACKET_MISMATCH,
					sourceIndex );
				return true;
			}
			const drawPacket_t &packet = packetFrame.DrawPacket( packetCursor++ );
			if ( packet.legacyDrawSurf != drawSurf
					|| packet.passCategory != RENDER_PASS_AUTHORED_POST
					|| packet.packetCategory != SCENE_PACKET_CATEGORY_POST_PROCESS ) {
				R_ClassicCinematicPostDomain_Fail( view,
					CLASSIC_CINEMATIC_POST_FAILURE_SOURCE_PACKET_MISMATCH,
					sourceIndex );
				return true;
			}
		}
	}
	if ( packetCursor != packetEnd ) {
		R_ClassicCinematicPostDomain_Fail( view,
			CLASSIC_CINEMATIC_POST_FAILURE_SOURCE_PACKET_MISMATCH,
			packetEnd - packetCursor );
		return true;
	}
	R_ClassicCinematicPostDomain_CommitView( view );
	rg_classicCinematicPostDomain.stats.authoredPostViews++;
	return true;
}

void R_ClassicCinematicPostDomain_ResetFrame( void ) {
	memset( &rg_classicCinematicPostDomain, 0,
		sizeof( rg_classicCinematicPostDomain ) );
	idStr::Copynz( rg_classicCinematicPostDomain.stats.status, "reset",
		sizeof( rg_classicCinematicPostDomain.stats.status ) );
}

void R_ClassicCinematicPostDomain_PrepareFrame(
		const idScenePacketFrame &packetFrame ) {
	R_ClassicCinematicPostDomain_ResetFrame();
	classicCinematicPostDomainStats_t &stats = rg_classicCinematicPostDomain.stats;
	stats.prepared = true;
	stats.frameValid = packetFrame.Stats().frontEndDerived
		|| packetFrame.Stats().backendDerived;
	stats.sourceScenes = packetFrame.NumScenes();
	if ( !stats.frameValid || packetFrame.Stats().overflow ) {
		stats.overflow = packetFrame.Stats().overflow;
		idStr::Copynz( stats.status, "packet frame unavailable", sizeof( stats.status ) );
		return;
	}
	for ( int sceneIndex = 0; sceneIndex < packetFrame.NumScenes(); ++sceneIndex ) {
		const scenePacket_t &scene = packetFrame.Scene( sceneIndex );
		if ( scene.viewDef == NULL ) {
			continue;
		}
		if ( !R_ClassicCinematicPostDomain_AddRootCinematicView( packetFrame,
				sceneIndex, scene )
				|| !R_ClassicCinematicPostDomain_AddAuthoredPostView( packetFrame,
				sceneIndex, scene ) ) {
			stats.overflow = true;
			break;
		}
	}
	stats.views = rg_classicCinematicPostDomain.viewCount;
	for ( int viewIndex = 0; viewIndex < rg_classicCinematicPostDomain.viewCount;
			++viewIndex ) {
		const classicCinematicPostDomainView_t &view =
			rg_classicCinematicPostDomain.views[ viewIndex ];
		if ( !view.ready ) {
			stats.fallbackViews++;
		}
		if ( view.ready && view.nestedInSpecialView ) {
			stats.nestedSpecialViews++;
			stats.nestedCinematicStages += view.cinematicStageCount;
			bool firstForRoot = true;
			for ( int earlier = 0; earlier < viewIndex; ++earlier ) {
				const classicCinematicPostDomainView_t &prior =
					rg_classicCinematicPostDomain.views[ earlier ];
				if ( prior.ready && prior.nestedInSpecialView
						&& prior.specialRootViewDef == view.specialRootViewDef ) {
					firstForRoot = false;
					break;
				}
			}
			if ( firstForRoot ) {
				stats.nestedSpecialTransactions++;
			}
		}
		stats.hash ^= view.hash;
	}
	idStr::Copynz( stats.status, stats.overflow ? "overflow" : "prepared",
		sizeof( stats.status ) );
}

const classicCinematicPostDomainStats_t &R_ClassicCinematicPostDomain_Stats( void ) {
	return rg_classicCinematicPostDomain.stats;
}

static const classicCinematicPostDomainView_t *R_ClassicCinematicPostDomain_Find(
		const viewDef_t *viewDef, classicCinematicPostDomainScope_t scope ) {
	for ( int viewIndex = 0; viewIndex < rg_classicCinematicPostDomain.viewCount;
			++viewIndex ) {
		const classicCinematicPostDomainView_t &view =
			rg_classicCinematicPostDomain.views[ viewIndex ];
		if ( view.viewDef == viewDef && view.scope == scope ) {
			return &view;
		}
	}
	return NULL;
}

const classicCinematicPostDomainView_t *R_ClassicCinematicPostDomain_FindRootCinematicView(
		const viewDef_t *viewDef ) {
	return R_ClassicCinematicPostDomain_Find( viewDef,
		CLASSIC_CINEMATIC_POST_SCOPE_ROOT_CINEMATIC );
}

const classicCinematicPostDomainView_t *R_ClassicCinematicPostDomain_FindAuthoredPostView(
		const viewDef_t *viewDef ) {
	return R_ClassicCinematicPostDomain_Find( viewDef,
		CLASSIC_CINEMATIC_POST_SCOPE_AUTHORED_POST );
}

bool R_ClassicCinematicPostDomain_ReadyForBackend( const viewDef_t *viewDef,
		classicCinematicPostDomainScope_t scope,
		classicCinematicPostDomainBackend_t backend ) {
	const classicCinematicPostDomainStats_t &stats =
		rg_classicCinematicPostDomain.stats;
	if ( !stats.prepared || !stats.frameValid || stats.overflow ) {
		return false;
	}
	if ( ( r_rendererSharedFaultInject.GetInteger()
			& SHARED_FAULT_INJECT_CINEMATIC_POST ) != 0 ) {
		// Qualification rollback: every backend takes its classic path.
		return false;
	}
	const classicCinematicPostDomainView_t *view =
		R_ClassicCinematicPostDomain_Find( viewDef, scope );
	if ( view == NULL || !view->ready ) {
		return false;
	}
	if ( backend < CLASSIC_CINEMATIC_POST_BACKEND_GL
			|| backend >= CLASSIC_CINEMATIC_POST_BACKEND_COUNT
			|| view->backendOutcome[ backend ]
				== CLASSIC_CINEMATIC_POST_BACKEND_FALLBACK ) {
		return false;
	}
	if ( !view->nestedInSpecialView ) {
		return true;
	}
	const classicSubviewDomainView_t *specialView =
		R_ClassicSubviewDomain_FindView( viewDef );
	return specialView != NULL && specialView->ready
		&& specialView->viewDef == viewDef
		&& specialView->rootViewIndex >= 0
		&& R_ClassicSubviewDomain_ViewByIndex( specialView->rootViewIndex ) != NULL
		&& R_ClassicSubviewDomain_ViewByIndex(
			specialView->rootViewIndex )->viewDef == view->specialRootViewDef
		&& specialView->backendOutcome[ backend ]
			== CLASSIC_SUBVIEW_DOMAIN_BACKEND_UNRECORDED
		&& R_ClassicSubviewDomain_ViewSemanticsMatch( *specialView );
}

static const classicSubviewDomainView_t *
R_ClassicCinematicPostDomain_SubviewRoot( const viewDef_t *memberViewDef ) {
	const classicSubviewDomainView_t *member =
		R_ClassicSubviewDomain_FindView( memberViewDef );
	return member != NULL && member->rootViewIndex >= 0
		? R_ClassicSubviewDomain_ViewByIndex( member->rootViewIndex ) : NULL;
}

static bool R_ClassicCinematicPostDomain_IsSubviewTransactionMember(
		const classicCinematicPostDomainView_t &view,
		const classicSubviewDomainView_t *root ) {
	return root != NULL && view.nestedInSpecialView
		&& view.specialRootViewDef == root->viewDef;
}

bool R_ClassicCinematicPostDomain_SubviewTransactionReady(
		const viewDef_t *memberViewDef,
		classicCinematicPostDomainBackend_t backend ) {
	if ( backend < CLASSIC_CINEMATIC_POST_BACKEND_GL
			|| backend >= CLASSIC_CINEMATIC_POST_BACKEND_COUNT ) {
		return false;
	}
	if ( r_rendererSharedCinematicPost.GetBool() ) {
		const classicCinematicPostDomainStats_t &stats =
			rg_classicCinematicPostDomain.stats;
		if ( !stats.prepared || !stats.frameValid || stats.overflow ) {
			return false;
		}
	}
	const classicSubviewDomainView_t *root =
		R_ClassicCinematicPostDomain_SubviewRoot( memberViewDef );
	if ( root == NULL ) {
		return false;
	}
	for ( int viewIndex = 0; viewIndex < rg_classicCinematicPostDomain.viewCount;
			++viewIndex ) {
		const classicCinematicPostDomainView_t &view =
			rg_classicCinematicPostDomain.views[ viewIndex ];
		if ( !R_ClassicCinematicPostDomain_IsSubviewTransactionMember( view, root ) ) {
			continue;
		}
		if ( !view.ready || view.backendOutcome[ backend ]
				== CLASSIC_CINEMATIC_POST_BACKEND_FALLBACK ) {
			return false;
		}
	}
	return true;
}

bool R_ClassicCinematicPostDomain_SubviewTransactionCompleted(
		const viewDef_t *memberViewDef,
		classicCinematicPostDomainBackend_t backend ) {
	if ( !R_ClassicCinematicPostDomain_SubviewTransactionReady( memberViewDef,
			backend ) ) {
		return false;
	}
	const classicSubviewDomainView_t *root =
		R_ClassicCinematicPostDomain_SubviewRoot( memberViewDef );
	for ( int viewIndex = 0; viewIndex < rg_classicCinematicPostDomain.viewCount;
			++viewIndex ) {
		const classicCinematicPostDomainView_t &view =
			rg_classicCinematicPostDomain.views[ viewIndex ];
		if ( !R_ClassicCinematicPostDomain_IsSubviewTransactionMember( view, root ) ) {
			continue;
		}
		// Publication is a second, non-failing pass.  Require the complete
		// transaction to remain unpublished and coverage-complete before any
		// member can become visible as shared-owned.
		if ( view.backendOutcome[ backend ]
				!= CLASSIC_CINEMATIC_POST_BACKEND_UNRECORDED
				|| !view.backendCompleted[ backend ]
				|| view.backendDrawnSurfaces[ backend ] != view.sourceSurfaceCount ) {
			return false;
		}
	}
	return true;
}

static void R_ClassicCinematicPostDomain_RecordSingleFallback(
		classicCinematicPostDomainView_t &view,
		classicCinematicPostDomainBackend_t backend,
		classicCinematicPostDomainFailure_t failure, int detail,
		bool countDuplicate ) {
	classicCinematicPostDomainBackendCoverage_t &coverage =
		rg_classicCinematicPostDomain.stats.backend[ backend ];
	if ( view.backendOutcome[ backend ] != CLASSIC_CINEMATIC_POST_BACKEND_UNRECORDED ) {
		if ( countDuplicate ) {
			coverage.duplicateReports++;
		}
		return;
	}
	view.backendOutcome[ backend ] = CLASSIC_CINEMATIC_POST_BACKEND_FALLBACK;
	view.backendFailure[ backend ] = failure;
	view.backendFailureDetail[ backend ] = detail;
	coverage.fallbackViews++;
	coverage.fallbackSurfaces += view.sourceSurfaceCount;
}

static bool R_ClassicCinematicPostDomain_PublishSingleOwned(
		classicCinematicPostDomainView_t &view,
		classicCinematicPostDomainBackend_t backend ) {
	classicCinematicPostDomainBackendCoverage_t &coverage =
		rg_classicCinematicPostDomain.stats.backend[ backend ];
	if ( view.backendOutcome[ backend ] == CLASSIC_CINEMATIC_POST_BACKEND_FALLBACK ) {
		coverage.coverageMismatches++;
		return false;
	}
	if ( view.backendOutcome[ backend ] == CLASSIC_CINEMATIC_POST_BACKEND_OWNED ) {
		coverage.duplicateReports++;
		return true;
	}
	if ( view.backendDrawnSurfaces[ backend ] != view.sourceSurfaceCount ) {
		coverage.coverageMismatches++;
		return false;
	}
	view.backendOutcome[ backend ] = CLASSIC_CINEMATIC_POST_BACKEND_OWNED;
	coverage.ownedViews++;
	coverage.ownedSurfaces += view.backendDrawnSurfaces[ backend ];
	return true;
}

void R_ClassicCinematicPostDomain_RecordBackendFallback(
		const viewDef_t *viewDef, classicCinematicPostDomainScope_t scope,
		classicCinematicPostDomainBackend_t backend,
		classicCinematicPostDomainFailure_t failure, int detail ) {
	classicCinematicPostDomainView_t *view = const_cast<classicCinematicPostDomainView_t *>(
		R_ClassicCinematicPostDomain_Find( viewDef, scope ) );
	if ( view == NULL || backend < CLASSIC_CINEMATIC_POST_BACKEND_GL
			|| backend >= CLASSIC_CINEMATIC_POST_BACKEND_COUNT ) {
		return;
	}
	R_ClassicCinematicPostDomain_RecordSingleFallback( *view, backend,
		failure, detail, true );
	if ( view->nestedInSpecialView ) {
		R_ClassicSubviewDomain_RecordBackendFallback( viewDef,
			static_cast<classicSubviewDomainBackend_t>( backend ),
			CLASSIC_SUBVIEW_DOMAIN_FAILURE_NESTED_CINEMATIC_POST_FALLBACK,
			detail );
	}
}

bool R_ClassicCinematicPostDomain_RecordOwned( const viewDef_t *viewDef,
		classicCinematicPostDomainScope_t scope,
		classicCinematicPostDomainBackend_t backend, int drawnSurfaces ) {
	classicCinematicPostDomainView_t *view = const_cast<classicCinematicPostDomainView_t *>(
		R_ClassicCinematicPostDomain_Find( viewDef, scope ) );
	if ( view == NULL || !view->ready || backend < CLASSIC_CINEMATIC_POST_BACKEND_GL
			|| backend >= CLASSIC_CINEMATIC_POST_BACKEND_COUNT
			|| drawnSurfaces != view->sourceSurfaceCount ) {
		if ( view != NULL && backend >= CLASSIC_CINEMATIC_POST_BACKEND_GL
				&& backend < CLASSIC_CINEMATIC_POST_BACKEND_COUNT ) {
			rg_classicCinematicPostDomain.stats.backend[ backend ].coverageMismatches++;
		}
		return false;
	}
	classicCinematicPostDomainBackendCoverage_t &coverage =
		rg_classicCinematicPostDomain.stats.backend[ backend ];
	if ( view->backendOutcome[ backend ]
			== CLASSIC_CINEMATIC_POST_BACKEND_FALLBACK ) {
		coverage.coverageMismatches++;
		return false;
	}
	if ( view->backendOutcome[ backend ]
			== CLASSIC_CINEMATIC_POST_BACKEND_OWNED ) {
		coverage.duplicateReports++;
		return view->backendDrawnSurfaces[ backend ] == drawnSurfaces;
	}
	if ( view->backendCompleted[ backend ] ) {
		coverage.duplicateReports++;
		return view->backendDrawnSurfaces[ backend ] == drawnSurfaces;
	}
	view->backendDrawnSurfaces[ backend ] = drawnSurfaces;
	view->backendCompleted[ backend ] = true;
	// Dynamic work nested inside a special view has executed, but ownership is
	// not visible until the outermost special-view transaction completes every
	// descendant draw range and capture/direct edge.
	return view->nestedInSpecialView
		|| R_ClassicCinematicPostDomain_PublishSingleOwned( *view, backend );
}

void R_ClassicCinematicPostDomain_RecordSubviewTransactionFallback(
		const viewDef_t *memberViewDef,
		classicCinematicPostDomainBackend_t backend,
		classicCinematicPostDomainFailure_t failure, int detail ) {
	if ( backend < CLASSIC_CINEMATIC_POST_BACKEND_GL
			|| backend >= CLASSIC_CINEMATIC_POST_BACKEND_COUNT ) {
		return;
	}
	const classicSubviewDomainView_t *root =
		R_ClassicCinematicPostDomain_SubviewRoot( memberViewDef );
	for ( int viewIndex = 0; root != NULL
			&& viewIndex < rg_classicCinematicPostDomain.viewCount; ++viewIndex ) {
		classicCinematicPostDomainView_t &view =
			rg_classicCinematicPostDomain.views[ viewIndex ];
		if ( R_ClassicCinematicPostDomain_IsSubviewTransactionMember( view, root ) ) {
			R_ClassicCinematicPostDomain_RecordSingleFallback( view, backend,
				failure, detail, false );
		}
	}
}

bool R_ClassicCinematicPostDomain_PublishSubviewTransactionOwned(
		const viewDef_t *memberViewDef,
		classicCinematicPostDomainBackend_t backend ) {
	if ( !R_ClassicCinematicPostDomain_SubviewTransactionCompleted(
			memberViewDef, backend ) ) {
		return false;
	}
	const classicSubviewDomainView_t *root =
		R_ClassicCinematicPostDomain_SubviewRoot( memberViewDef );
	for ( int viewIndex = 0; root != NULL
			&& viewIndex < rg_classicCinematicPostDomain.viewCount; ++viewIndex ) {
		classicCinematicPostDomainView_t &view =
			rg_classicCinematicPostDomain.views[ viewIndex ];
		if ( R_ClassicCinematicPostDomain_IsSubviewTransactionMember( view, root )
				&& !R_ClassicCinematicPostDomain_PublishSingleOwned( view, backend ) ) {
			return false;
		}
	}
	return true;
}

const char *ClassicCinematicPostDomainFailure_Name(
		classicCinematicPostDomainFailure_t failure ) {
	switch ( failure ) {
	case CLASSIC_CINEMATIC_POST_FAILURE_NONE: return "none";
	case CLASSIC_CINEMATIC_POST_FAILURE_UNAVAILABLE: return "unavailable";
	case CLASSIC_CINEMATIC_POST_FAILURE_SCENE_PACKET_OVERFLOW: return "scenePacketOverflow";
	case CLASSIC_CINEMATIC_POST_FAILURE_VIEW_POOL_OVERFLOW: return "viewPoolOverflow";
	case CLASSIC_CINEMATIC_POST_FAILURE_UNSUPPORTED_VIEW: return "unsupportedView";
	case CLASSIC_CINEMATIC_POST_FAILURE_INVALID_SCENE_RANGE: return "invalidSceneRange";
	case CLASSIC_CINEMATIC_POST_FAILURE_INVALID_PASS_RANGE: return "invalidPassRange";
	case CLASSIC_CINEMATIC_POST_FAILURE_SOURCE_PACKET_MISMATCH: return "sourcePacketMismatch";
	case CLASSIC_CINEMATIC_POST_FAILURE_INVALID_SOURCE_SURFACE: return "invalidSourceSurface";
	case CLASSIC_CINEMATIC_POST_FAILURE_CINEMATIC_CLOCK: return "cinematicClock";
	case CLASSIC_CINEMATIC_POST_FAILURE_MISSING_SPECIAL_VIEW: return "missingSpecialView";
	case CLASSIC_CINEMATIC_POST_FAILURE_SPECIAL_VIEW_TRANSACTION_REJECTED: return "specialViewTransactionRejected";
	case CLASSIC_CINEMATIC_POST_FAILURE_BACKEND_SPECIAL_VIEW_INCOMPLETE: return "backendSpecialViewIncomplete";
	case CLASSIC_CINEMATIC_POST_FAILURE_BACKEND_NOT_READY: return "backendNotReady";
	case CLASSIC_CINEMATIC_POST_FAILURE_BACKEND_REJECTED: return "backendRejected";
	case CLASSIC_CINEMATIC_POST_FAILURE_BACKEND_COVERAGE_MISMATCH: return "backendCoverageMismatch";
	default: return "unknown";
	}
}

bool RendererClassicCinematicPostDomain_RunSelfTest( void ) {
	R_ClassicCinematicPostDomain_ResetFrame();
	const classicCinematicPostDomainStats_t &resetStats =
		R_ClassicCinematicPostDomain_Stats();
	if ( CLASSIC_CINEMATIC_POST_DOMAIN_MAX_VIEWS != SCENE_PACKET_MAX_SCENES
			|| !R_ClassicCinematicPostDomain_RangeFits( 7, 1, 8 )
			|| R_ClassicCinematicPostDomain_RangeFits( 8, 1, 8 )
			|| R_ClassicCinematicPostDomain_RangeFits(
				( std::numeric_limits<int>::max )(), 1,
				( std::numeric_limits<int>::max )() )
			|| !R_ClassicCinematicPostDomain_RangeContains( 2, 3, 4, 1 )
			|| R_ClassicCinematicPostDomain_RangeContains( 2, 3, 5, 1 )
			|| idStr::Cmp( ClassicCinematicPostDomainFailure_Name(
				CLASSIC_CINEMATIC_POST_FAILURE_SOURCE_PACKET_MISMATCH ),
				"sourcePacketMismatch" )
			|| idStr::Cmp( ClassicCinematicPostDomainFailure_Name(
				CLASSIC_CINEMATIC_POST_FAILURE_CINEMATIC_CLOCK ),
				"cinematicClock" )
			|| idStr::Cmp( ClassicCinematicPostDomainFailure_Name(
				CLASSIC_CINEMATIC_POST_FAILURE_SPECIAL_VIEW_TRANSACTION_REJECTED ),
				"specialViewTransactionRejected" )
			|| resetStats.prepared || resetStats.frameValid || resetStats.overflow
			|| resetStats.views != 0 || resetStats.readyViews != 0
			|| R_ClassicCinematicPostDomain_ReadyForBackend( NULL,
				CLASSIC_CINEMATIC_POST_SCOPE_ROOT_CINEMATIC,
				CLASSIC_CINEMATIC_POST_BACKEND_GL )
			|| R_ClassicCinematicPostDomain_RecordOwned( NULL,
				CLASSIC_CINEMATIC_POST_SCOPE_AUTHORED_POST,
				CLASSIC_CINEMATIC_POST_BACKEND_VULKAN, 0 ) ) {
		return false;
	}

	viewDef_t viewDef;
	memset( &viewDef, 0, sizeof( viewDef ) );
	classicCinematicPostDomainView_t &view =
		rg_classicCinematicPostDomain.views[ 0 ];
	memset( &view, 0, sizeof( view ) );
	view.viewDef = &viewDef;
	view.scope = CLASSIC_CINEMATIC_POST_SCOPE_ROOT_CINEMATIC;
	view.scenePacketIndex = 3;
	view.passPacketIndex = 7;
	view.firstSourceSurface = 0;
	view.sourceSurfaceCount = 3;
	view.packetDrawCount = 2;
	view.cinematicStageCount = 1;
	view.cinematicTimeMilliseconds = 1000;
	view.ready = true;
	const std::uint64_t firstHash = R_ClassicCinematicPostDomain_HashView( view );
	view.cinematicTimeMilliseconds++;
	const bool clockHashSensitive = R_ClassicCinematicPostDomain_HashView( view )
		!= firstHash;
	view.cinematicTimeMilliseconds--;
	view.nestedInSpecialView = true;
	view.specialRootScenePacketIndex = 11;
	view.specialNestingDepth = 2;
	const bool nestingHashSensitive =
		R_ClassicCinematicPostDomain_HashView( view ) != firstHash;
	view.nestedInSpecialView = false;
	view.specialRootScenePacketIndex = 0;
	view.specialNestingDepth = 0;
	view.hash = firstHash;
	rg_classicCinematicPostDomain.viewCount = 1;
	memset( &rg_classicCinematicPostDomain.stats, 0,
		sizeof( rg_classicCinematicPostDomain.stats ) );

	const bool owned = R_ClassicCinematicPostDomain_RecordOwned( &viewDef,
		CLASSIC_CINEMATIC_POST_SCOPE_ROOT_CINEMATIC,
		CLASSIC_CINEMATIC_POST_BACKEND_GL, 3 );
	const bool duplicate = R_ClassicCinematicPostDomain_RecordOwned( &viewDef,
		CLASSIC_CINEMATIC_POST_SCOPE_ROOT_CINEMATIC,
		CLASSIC_CINEMATIC_POST_BACKEND_GL, 3 );
	const bool mismatch = !R_ClassicCinematicPostDomain_RecordOwned( &viewDef,
		CLASSIC_CINEMATIC_POST_SCOPE_ROOT_CINEMATIC,
		CLASSIC_CINEMATIC_POST_BACKEND_VULKAN, 2 );
	R_ClassicCinematicPostDomain_RecordBackendFallback( &viewDef,
		CLASSIC_CINEMATIC_POST_SCOPE_ROOT_CINEMATIC,
		CLASSIC_CINEMATIC_POST_BACKEND_VULKAN,
		CLASSIC_CINEMATIC_POST_FAILURE_BACKEND_COVERAGE_MISMATCH, 2 );
	const classicCinematicPostDomainStats_t &stats =
		R_ClassicCinematicPostDomain_Stats();
	const bool coverageContract = owned && duplicate && mismatch
		&& view.backendOutcome[ CLASSIC_CINEMATIC_POST_BACKEND_GL ]
			== CLASSIC_CINEMATIC_POST_BACKEND_OWNED
		&& view.backendOutcome[ CLASSIC_CINEMATIC_POST_BACKEND_VULKAN ]
			== CLASSIC_CINEMATIC_POST_BACKEND_FALLBACK
		&& !R_ClassicCinematicPostDomain_ReadyForBackend( &viewDef,
			CLASSIC_CINEMATIC_POST_SCOPE_ROOT_CINEMATIC,
			CLASSIC_CINEMATIC_POST_BACKEND_VULKAN )
		&& stats.backend[ CLASSIC_CINEMATIC_POST_BACKEND_GL ].ownedViews == 1
		&& stats.backend[ CLASSIC_CINEMATIC_POST_BACKEND_GL ].duplicateReports == 1
		&& stats.backend[ CLASSIC_CINEMATIC_POST_BACKEND_VULKAN ]
			.coverageMismatches == 1
		&& stats.backend[ CLASSIC_CINEMATIC_POST_BACKEND_VULKAN ].fallbackViews == 1;
	R_ClassicCinematicPostDomain_ResetFrame();
	return clockHashSensitive && nestingHashSensitive && coverageContract;
}
