// Copyright (C) 2026 DarkMatter Productions
//

#include "tr_local.h"
#include "ClassicSpecialFrameDomain.h"

#include <cstring>

typedef struct classicSpecialFrameDomainState_s {
	classicSpecialFrameDomainView_t	views[
		CLASSIC_SPECIAL_FRAME_DOMAIN_MAX_VIEWS ];
	int						viewCount;
	classicSpecialFrameDomainStats_t	stats;
} classicSpecialFrameDomainState_t;

static classicSpecialFrameDomainState_t rg_classicSpecialFrameDomain;

static std::uint64_t R_ClassicSpecialFrameDomain_HashBytes(
		std::uint64_t hash, const void *data, size_t bytes ) {
	const byte *source = static_cast<const byte *>( data );
	for ( size_t i = 0; i < bytes; ++i ) {
		hash ^= source[ i ];
		hash *= UINT64_C( 1099511628211 );
	}
	return hash;
}

static std::uint64_t R_ClassicSpecialFrameDomain_HashView(
		const classicSpecialFrameDomainView_t &view ) {
	std::uint64_t hash = UINT64_C( 1469598103934665603 );
	hash = R_ClassicSpecialFrameDomain_HashBytes( hash, &view.scope,
		sizeof( view.scope ) );
	hash = R_ClassicSpecialFrameDomain_HashBytes( hash,
		&view.scenePacketIndex, sizeof( view.scenePacketIndex ) );
	hash = R_ClassicSpecialFrameDomain_HashBytes( hash,
		&view.passPacketIndex, sizeof( view.passPacketIndex ) );
	hash = R_ClassicSpecialFrameDomain_HashBytes( hash,
		&view.sourceSurfaceCount, sizeof( view.sourceSurfaceCount ) );
	hash = R_ClassicSpecialFrameDomain_HashBytes( hash,
		&view.packetDrawCount, sizeof( view.packetDrawCount ) );
	hash = R_ClassicSpecialFrameDomain_HashBytes( hash,
		&view.passPacketCount, sizeof( view.passPacketCount ) );
	hash = R_ClassicSpecialFrameDomain_HashBytes( hash,
		&view.renderDemoVersion, sizeof( view.renderDemoVersion ) );
	hash = R_ClassicSpecialFrameDomain_HashBytes( hash,
		&view.specialEffectsMask, sizeof( view.specialEffectsMask ) );
	return hash;
}

static void R_ClassicSpecialFrameDomain_ResetStats( void ) {
	memset( &rg_classicSpecialFrameDomain, 0,
		sizeof( rg_classicSpecialFrameDomain ) );
	idStr::Copynz( rg_classicSpecialFrameDomain.stats.status, "empty",
		sizeof( rg_classicSpecialFrameDomain.stats.status ) );
}

void R_ClassicSpecialFrameDomain_ResetFrame( void ) {
	R_ClassicSpecialFrameDomain_ResetStats();
}

static void R_ClassicSpecialFrameDomain_Fail(
		classicSpecialFrameDomainView_t &view,
		classicSpecialFrameDomainFailure_t failure, int detail ) {
	view.ready = false;
	view.failure = failure;
	view.failureDetail = detail;
	if ( failure > CLASSIC_SPECIAL_FRAME_FAILURE_NONE
			&& failure < CLASSIC_SPECIAL_FRAME_FAILURE_COUNT ) {
		rg_classicSpecialFrameDomain.stats.failureCounts[ failure ]++;
	}
}

static void R_ClassicSpecialFrameDomain_Commit(
		classicSpecialFrameDomainView_t &view ) {
	view.ready = true;
	view.failure = CLASSIC_SPECIAL_FRAME_FAILURE_NONE;
	view.failureDetail = 0;
	view.hash = R_ClassicSpecialFrameDomain_HashView( view );
	rg_classicSpecialFrameDomain.stats.readyViews++;
	rg_classicSpecialFrameDomain.stats.hash ^=
		view.hash + UINT64_C( 0x9e3779b97f4a7c15 )
		+ ( rg_classicSpecialFrameDomain.stats.hash << 6 )
		+ ( rg_classicSpecialFrameDomain.stats.hash >> 2 );
}

static bool R_ClassicSpecialFrameDomain_AddRecord(
		classicSpecialFrameDomainView_t *&result, const viewDef_t *viewDef,
		classicSpecialFrameDomainScope_t scope, int sceneIndex ) {
	result = NULL;
	if ( rg_classicSpecialFrameDomain.viewCount
			>= CLASSIC_SPECIAL_FRAME_DOMAIN_MAX_VIEWS ) {
		rg_classicSpecialFrameDomain.stats.overflow = true;
		return false;
	}
	classicSpecialFrameDomainView_t &view =
		rg_classicSpecialFrameDomain.views[
			rg_classicSpecialFrameDomain.viewCount++ ];
	memset( &view, 0, sizeof( view ) );
	view.viewDef = viewDef;
	view.scope = scope;
	view.scenePacketIndex = sceneIndex;
	view.passPacketIndex = -1;
	view.failure = CLASSIC_SPECIAL_FRAME_FAILURE_UNAVAILABLE;
	result = &view;
	rg_classicSpecialFrameDomain.stats.views++;
	if ( scope == CLASSIC_SPECIAL_FRAME_SCOPE_RENDER_DEMO ) {
		rg_classicSpecialFrameDomain.stats.renderDemoViews++;
	} else {
		rg_classicSpecialFrameDomain.stats.ravenEffectsViews++;
	}
	return true;
}

static bool R_ClassicSpecialFrameDomain_ValidateSceneRange(
		const idScenePacketFrame &packetFrame, int sceneIndex,
		const scenePacket_t &scene, classicSpecialFrameDomainView_t &view,
		bool commandOnly ) {
	if ( sceneIndex < 0 || sceneIndex >= packetFrame.NumScenes()
			|| scene.viewDef == NULL || scene.passPacketCount <= 0
			|| scene.firstPassPacket < 0
			|| scene.firstPassPacket + scene.passPacketCount
				> packetFrame.NumPasses() || scene.drawPacketCount < 0 ) {
		R_ClassicSpecialFrameDomain_Fail( view,
			CLASSIC_SPECIAL_FRAME_FAILURE_INVALID_SCENE_RANGE, sceneIndex );
		return false;
	}
	int packetDraws = 0;
	for ( int passOffset = 0; passOffset < scene.passPacketCount; ++passOffset ) {
		const int passIndex = scene.firstPassPacket + passOffset;
		const passPacket_t &pass = packetFrame.Pass( passIndex );
		if ( pass.firstDrawPacket < 0 || pass.drawPacketCount < 0
				|| pass.firstDrawPacket + pass.drawPacketCount
					> packetFrame.NumDrawPackets()
				|| ( commandOnly && !pass.commandOnly ) ) {
			R_ClassicSpecialFrameDomain_Fail( view,
				CLASSIC_SPECIAL_FRAME_FAILURE_INVALID_PASS_RANGE, passIndex );
			return false;
		}
		packetDraws += pass.drawPacketCount;
		for ( int drawOffset = 0; drawOffset < pass.drawPacketCount; ++drawOffset ) {
			const drawPacket_t &packet = packetFrame.DrawPacket(
				pass.firstDrawPacket + drawOffset );
			if ( packet.viewDef != scene.viewDef || packet.legacyDrawSurf == NULL ) {
				R_ClassicSpecialFrameDomain_Fail( view,
					CLASSIC_SPECIAL_FRAME_FAILURE_SOURCE_PACKET_MISMATCH,
					pass.firstDrawPacket + drawOffset );
				return false;
			}
		}
	}
	if ( packetDraws != scene.drawPacketCount ) {
		R_ClassicSpecialFrameDomain_Fail( view,
			CLASSIC_SPECIAL_FRAME_FAILURE_SOURCE_PACKET_MISMATCH, packetDraws );
		return false;
	}
	view.packetDrawCount = packetDraws;
	view.passPacketCount = scene.passPacketCount;
	return true;
}

static bool R_ClassicSpecialFrameDomain_HasPass(
		const idScenePacketFrame &packetFrame, const scenePacket_t &scene,
		renderPassCategory_t category ) {
	for ( int passOffset = 0; passOffset < scene.passPacketCount; ++passOffset ) {
		const passPacket_t &pass = packetFrame.Pass(
			scene.firstPassPacket + passOffset );
		if ( pass.passCategory == category ) {
			return true;
		}
	}
	return false;
}

static void R_ClassicSpecialFrameDomain_AddRenderDemo(
		const idScenePacketFrame &packetFrame, int sceneIndex,
		const scenePacket_t &scene ) {
	if ( !scene.renderDemoPlayback ) {
		return;
	}
	classicSpecialFrameDomainView_t *view = NULL;
	if ( !R_ClassicSpecialFrameDomain_AddRecord( view, scene.viewDef,
			CLASSIC_SPECIAL_FRAME_SCOPE_RENDER_DEMO, sceneIndex ) ) {
		return;
	}
	if ( view->viewDef == NULL || view->viewDef->viewEntitys == NULL
			|| view->viewDef->renderWorld == NULL || view->viewDef->isSubview
			|| view->viewDef->isMirror || view->viewDef->isXraySubview
			|| view->viewDef->isEditor || view->viewDef->superView != NULL
			|| view->viewDef->subviewSurface != NULL
			|| view->viewDef->numClipPlanes != 0
			|| view->viewDef->numDrawSurfs <= 0
			|| view->viewDef->drawSurfs == NULL ) {
		R_ClassicSpecialFrameDomain_Fail( *view,
			CLASSIC_SPECIAL_FRAME_FAILURE_UNSUPPORTED_VIEW, 0 );
		return;
	}
	if ( session == NULL || session->readDemo == NULL
			|| session->rw != view->viewDef->renderWorld
			|| session->renderdemoVersion <= 0 ) {
		R_ClassicSpecialFrameDomain_Fail( *view,
			CLASSIC_SPECIAL_FRAME_FAILURE_RENDER_DEMO_STATE, 0 );
		return;
	}
	if ( !R_ClassicSpecialFrameDomain_ValidateSceneRange( packetFrame,
			sceneIndex, scene, *view, false ) ) {
		return;
	}
	if ( !R_ClassicSpecialFrameDomain_HasPass( packetFrame, scene,
			RENDER_PASS_DEPTH )
			|| !R_ClassicSpecialFrameDomain_HasPass( packetFrame, scene,
				RENDER_PASS_ARB2_INTERACTION )
			|| !R_ClassicSpecialFrameDomain_HasPass( packetFrame, scene,
				RENDER_PASS_AMBIENT ) ) {
		R_ClassicSpecialFrameDomain_Fail( *view,
			CLASSIC_SPECIAL_FRAME_FAILURE_INVALID_PASS_RANGE, 0 );
		return;
	}
	view->sourceSurfaceCount = view->viewDef->numDrawSurfs;
	view->renderDemoVersion = session->renderdemoVersion;
	view->passPacketIndex = scene.firstPassPacket;
	R_ClassicSpecialFrameDomain_Commit( *view );
}

static void R_ClassicSpecialFrameDomain_AddRavenEffects(
		const idScenePacketFrame &packetFrame, int sceneIndex,
		const scenePacket_t &scene ) {
	if ( scene.passPacketCount != 1 || scene.firstPassPacket < 0
			|| scene.firstPassPacket >= packetFrame.NumPasses() ) {
		return;
	}
	const passPacket_t &pass = packetFrame.Pass( scene.firstPassPacket );
	if ( pass.passCategory != RENDER_PASS_SPECIAL_EFFECTS ) {
		return;
	}
	classicSpecialFrameDomainView_t *view = NULL;
	if ( !R_ClassicSpecialFrameDomain_AddRecord( view, scene.viewDef,
			CLASSIC_SPECIAL_FRAME_SCOPE_RAVEN_EFFECTS, sceneIndex ) ) {
		return;
	}
	if ( view->viewDef == NULL || view->viewDef->viewEntitys == NULL
			|| view->viewDef->renderWorld == NULL || view->viewDef->isSubview
			|| view->viewDef->isMirror || view->viewDef->isXraySubview
			|| view->viewDef->isEditor || view->viewDef->superView != NULL
			|| view->viewDef->subviewSurface != NULL
			|| view->viewDef->numClipPlanes != 0
			|| view->viewDef->renderView.viewID < 0 ) {
		R_ClassicSpecialFrameDomain_Fail( *view,
			CLASSIC_SPECIAL_FRAME_FAILURE_UNSUPPORTED_VIEW, 0 );
		return;
	}
	if ( !R_ClassicSpecialFrameDomain_ValidateSceneRange( packetFrame,
			sceneIndex, scene, *view, true ) || !pass.commandOnly ) {
		if ( view->failure == CLASSIC_SPECIAL_FRAME_FAILURE_UNAVAILABLE ) {
			R_ClassicSpecialFrameDomain_Fail( *view,
				CLASSIC_SPECIAL_FRAME_FAILURE_INVALID_PASS_RANGE, 0 );
		}
		return;
	}
	view->specialEffectsMask = scene.specialEffectsMask
		& ( SPECIAL_EFFECT_BLUR | SPECIAL_EFFECT_AL );
	if ( view->specialEffectsMask == 0 ) {
		R_ClassicSpecialFrameDomain_Fail( *view,
			CLASSIC_SPECIAL_FRAME_FAILURE_SPECIAL_EFFECT_STATE, 0 );
		return;
	}
	view->passPacketIndex = scene.firstPassPacket;
	rg_classicSpecialFrameDomain.stats.specialEffectsMask
		|= view->specialEffectsMask;
	R_ClassicSpecialFrameDomain_Commit( *view );
}

void R_ClassicSpecialFrameDomain_PrepareFrame(
		const idScenePacketFrame &packetFrame ) {
	R_ClassicSpecialFrameDomain_ResetStats();
	classicSpecialFrameDomainStats_t &stats = rg_classicSpecialFrameDomain.stats;
	stats.prepared = true;
	stats.sourceScenes = packetFrame.NumScenes();
	if ( packetFrame.Stats().overflow || !packetFrame.ValidateSortKeys() ) {
		stats.overflow = packetFrame.Stats().overflow;
		stats.frameValid = false;
		stats.failureCounts[ CLASSIC_SPECIAL_FRAME_FAILURE_SCENE_PACKET_OVERFLOW ]++;
		idStr::Copynz( stats.status, "packet-invalid", sizeof( stats.status ) );
		return;
	}
	stats.frameValid = true;
	for ( int sceneIndex = 0; sceneIndex < packetFrame.NumScenes(); ++sceneIndex ) {
		const scenePacket_t &scene = packetFrame.Scene( sceneIndex );
		R_ClassicSpecialFrameDomain_AddRenderDemo( packetFrame, sceneIndex, scene );
		R_ClassicSpecialFrameDomain_AddRavenEffects( packetFrame, sceneIndex, scene );
	}
	if ( stats.overflow ) {
		stats.frameValid = false;
		idStr::Copynz( stats.status, "view-overflow", sizeof( stats.status ) );
	} else if ( stats.views == 0 ) {
		idStr::Copynz( stats.status, "prepared", sizeof( stats.status ) );
	} else if ( stats.readyViews == stats.views ) {
		idStr::Copynz( stats.status, "ready", sizeof( stats.status ) );
	} else {
		idStr::Copynz( stats.status, "fallback", sizeof( stats.status ) );
	}
}

const classicSpecialFrameDomainStats_t &R_ClassicSpecialFrameDomain_Stats( void ) {
	return rg_classicSpecialFrameDomain.stats;
}

static const classicSpecialFrameDomainView_t *R_ClassicSpecialFrameDomain_Find(
		const viewDef_t *viewDef, classicSpecialFrameDomainScope_t scope ) {
	for ( int i = 0; i < rg_classicSpecialFrameDomain.viewCount; ++i ) {
		const classicSpecialFrameDomainView_t &view =
			rg_classicSpecialFrameDomain.views[ i ];
		if ( view.viewDef == viewDef && view.scope == scope ) {
			return &view;
		}
	}
	return NULL;
}

const classicSpecialFrameDomainView_t *R_ClassicSpecialFrameDomain_FindRenderDemoView(
		const viewDef_t *viewDef ) {
	return R_ClassicSpecialFrameDomain_Find( viewDef,
		CLASSIC_SPECIAL_FRAME_SCOPE_RENDER_DEMO );
}

const classicSpecialFrameDomainView_t *R_ClassicSpecialFrameDomain_FindRavenEffectsView(
		const viewDef_t *viewDef ) {
	return R_ClassicSpecialFrameDomain_Find( viewDef,
		CLASSIC_SPECIAL_FRAME_SCOPE_RAVEN_EFFECTS );
}

bool R_ClassicSpecialFrameDomain_ReadyForBackend( const viewDef_t *viewDef,
		classicSpecialFrameDomainScope_t scope,
		classicSpecialFrameDomainBackend_t backend ) {
	const classicSpecialFrameDomainView_t *view =
		R_ClassicSpecialFrameDomain_Find( viewDef, scope );
	if ( ( r_rendererSharedFaultInject.GetInteger()
			& SHARED_FAULT_INJECT_SPECIAL_FRAME ) != 0 ) {
		// Qualification rollback: every backend takes its classic path.
		return false;
	}
	return view != NULL && view->ready
		&& backend >= CLASSIC_SPECIAL_FRAME_BACKEND_GL
		&& backend < CLASSIC_SPECIAL_FRAME_BACKEND_COUNT
		&& view->backendOutcome[ backend ]
			!= CLASSIC_SPECIAL_FRAME_BACKEND_FALLBACK;
}

void R_ClassicSpecialFrameDomain_RecordBackendFallback(
		const viewDef_t *viewDef, classicSpecialFrameDomainScope_t scope,
		classicSpecialFrameDomainBackend_t backend,
		classicSpecialFrameDomainFailure_t failure, int detail ) {
	classicSpecialFrameDomainView_t *view =
		const_cast<classicSpecialFrameDomainView_t *>(
			R_ClassicSpecialFrameDomain_Find( viewDef, scope ) );
	if ( view == NULL || backend < CLASSIC_SPECIAL_FRAME_BACKEND_GL
			|| backend >= CLASSIC_SPECIAL_FRAME_BACKEND_COUNT ) {
		return;
	}
	classicSpecialFrameDomainBackendCoverage_t &coverage =
		rg_classicSpecialFrameDomain.stats.backend[ backend ];
	if ( view->backendOutcome[ backend ]
			!= CLASSIC_SPECIAL_FRAME_BACKEND_UNRECORDED ) {
		coverage.duplicateReports++;
		return;
	}
	view->backendOutcome[ backend ] = CLASSIC_SPECIAL_FRAME_BACKEND_FALLBACK;
	view->backendFailure[ backend ] = failure;
	view->backendFailureDetail[ backend ] = detail;
	coverage.fallbackViews++;
	coverage.fallbackSurfaces += view->sourceSurfaceCount;
	rg_classicSpecialFrameDomain.stats.fallbackViews++;
}

bool R_ClassicSpecialFrameDomain_RecordOwned( const viewDef_t *viewDef,
		classicSpecialFrameDomainScope_t scope,
		classicSpecialFrameDomainBackend_t backend, int coverageValue ) {
	classicSpecialFrameDomainView_t *view =
		const_cast<classicSpecialFrameDomainView_t *>(
			R_ClassicSpecialFrameDomain_Find( viewDef, scope ) );
	if ( view == NULL || !view->ready
			|| backend < CLASSIC_SPECIAL_FRAME_BACKEND_GL
			|| backend >= CLASSIC_SPECIAL_FRAME_BACKEND_COUNT ) {
		return false;
	}
	classicSpecialFrameDomainBackendCoverage_t &coverage =
		rg_classicSpecialFrameDomain.stats.backend[ backend ];
	if ( view->backendOutcome[ backend ] == CLASSIC_SPECIAL_FRAME_BACKEND_FALLBACK ) {
		coverage.coverageMismatches++;
		return false;
	}
	if ( scope == CLASSIC_SPECIAL_FRAME_SCOPE_RENDER_DEMO
			&& coverageValue != view->sourceSurfaceCount ) {
		coverage.coverageMismatches++;
		return false;
	}
	if ( scope == CLASSIC_SPECIAL_FRAME_SCOPE_RAVEN_EFFECTS
			&& ( coverageValue <= 0
				|| ( coverageValue & ~view->specialEffectsMask ) != 0 ) ) {
		coverage.coverageMismatches++;
		return false;
	}
	if ( view->backendOutcome[ backend ] == CLASSIC_SPECIAL_FRAME_BACKEND_OWNED ) {
		coverage.duplicateReports++;
		return true;
	}
	if ( scope == CLASSIC_SPECIAL_FRAME_SCOPE_RAVEN_EFFECTS ) {
		view->backendCoverage[ backend ] |= coverageValue;
		if ( ( view->backendCoverage[ backend ] & view->specialEffectsMask )
				!= view->specialEffectsMask ) {
			return true;
		}
	} else {
		view->backendCoverage[ backend ] = coverageValue;
	}
	view->backendOutcome[ backend ] = CLASSIC_SPECIAL_FRAME_BACKEND_OWNED;
	coverage.ownedViews++;
	coverage.ownedSurfaces += scope == CLASSIC_SPECIAL_FRAME_SCOPE_RENDER_DEMO
		? coverageValue : 1;
	return true;
}

void R_ClassicSpecialFrameDomain_FinalizeBackendFrame(
		classicSpecialFrameDomainBackend_t backend ) {
	if ( backend < CLASSIC_SPECIAL_FRAME_BACKEND_GL
			|| backend >= CLASSIC_SPECIAL_FRAME_BACKEND_COUNT ) {
		return;
	}
	for ( int i = 0; i < rg_classicSpecialFrameDomain.viewCount; ++i ) {
		classicSpecialFrameDomainView_t &view =
			rg_classicSpecialFrameDomain.views[ i ];
		if ( view.backendOutcome[ backend ]
			== CLASSIC_SPECIAL_FRAME_BACKEND_UNRECORDED ) {
			R_ClassicSpecialFrameDomain_RecordBackendFallback( view.viewDef,
				view.scope, backend,
				view.ready ? CLASSIC_SPECIAL_FRAME_FAILURE_BACKEND_REJECTED
					: view.failure,
				view.ready ? 0 : view.failureDetail );
		}
	}
}

const char *ClassicSpecialFrameDomainFailure_Name(
		classicSpecialFrameDomainFailure_t failure ) {
	switch ( failure ) {
	case CLASSIC_SPECIAL_FRAME_FAILURE_NONE: return "none";
	case CLASSIC_SPECIAL_FRAME_FAILURE_UNAVAILABLE: return "unavailable";
	case CLASSIC_SPECIAL_FRAME_FAILURE_SCENE_PACKET_OVERFLOW: return "scenePacketOverflow";
	case CLASSIC_SPECIAL_FRAME_FAILURE_VIEW_POOL_OVERFLOW: return "viewPoolOverflow";
	case CLASSIC_SPECIAL_FRAME_FAILURE_UNSUPPORTED_VIEW: return "unsupportedView";
	case CLASSIC_SPECIAL_FRAME_FAILURE_INVALID_SCENE_RANGE: return "invalidSceneRange";
	case CLASSIC_SPECIAL_FRAME_FAILURE_INVALID_PASS_RANGE: return "invalidPassRange";
	case CLASSIC_SPECIAL_FRAME_FAILURE_SOURCE_PACKET_MISMATCH: return "sourcePacketMismatch";
	case CLASSIC_SPECIAL_FRAME_FAILURE_RENDER_DEMO_STATE: return "renderDemoState";
	case CLASSIC_SPECIAL_FRAME_FAILURE_SPECIAL_EFFECT_STATE: return "specialEffectState";
	case CLASSIC_SPECIAL_FRAME_FAILURE_BACKEND_NOT_READY: return "backendNotReady";
	case CLASSIC_SPECIAL_FRAME_FAILURE_BACKEND_REJECTED: return "backendRejected";
	case CLASSIC_SPECIAL_FRAME_FAILURE_BACKEND_COVERAGE_MISMATCH: return "backendCoverageMismatch";
	default: return "unknown";
	}
}

bool RendererClassicSpecialFrameDomain_RunSelfTest( void ) {
	R_ClassicSpecialFrameDomain_ResetFrame();
	const classicSpecialFrameDomainStats_t &reset =
		R_ClassicSpecialFrameDomain_Stats();
	if ( CLASSIC_SPECIAL_FRAME_DOMAIN_MAX_VIEWS != SCENE_PACKET_MAX_SCENES
			|| reset.prepared || reset.frameValid || reset.views != 0
			|| idStr::Cmp( ClassicSpecialFrameDomainFailure_Name(
				CLASSIC_SPECIAL_FRAME_FAILURE_RENDER_DEMO_STATE ),
				"renderDemoState" )
			|| idStr::Cmp( ClassicSpecialFrameDomainFailure_Name(
				CLASSIC_SPECIAL_FRAME_FAILURE_SPECIAL_EFFECT_STATE ),
				"specialEffectState" )
			|| R_ClassicSpecialFrameDomain_ReadyForBackend( NULL,
				CLASSIC_SPECIAL_FRAME_SCOPE_RENDER_DEMO,
				CLASSIC_SPECIAL_FRAME_BACKEND_GL ) ) {
		return false;
	}

	viewDef_t demoView;
	viewDef_t effectsView;
	memset( &demoView, 0, sizeof( demoView ) );
	memset( &effectsView, 0, sizeof( effectsView ) );
	classicSpecialFrameDomainView_t &demo =
		rg_classicSpecialFrameDomain.views[ 0 ];
	classicSpecialFrameDomainView_t &effects =
		rg_classicSpecialFrameDomain.views[ 1 ];
	memset( &demo, 0, sizeof( demo ) );
	memset( &effects, 0, sizeof( effects ) );
	demo.viewDef = &demoView;
	demo.scope = CLASSIC_SPECIAL_FRAME_SCOPE_RENDER_DEMO;
	demo.scenePacketIndex = 1;
	demo.passPacketIndex = 2;
	demo.sourceSurfaceCount = 3;
	demo.packetDrawCount = 8;
	demo.passPacketCount = 6;
	demo.renderDemoVersion = 10;
	demo.ready = true;
	demo.hash = R_ClassicSpecialFrameDomain_HashView( demo );
	effects.viewDef = &effectsView;
	effects.scope = CLASSIC_SPECIAL_FRAME_SCOPE_RAVEN_EFFECTS;
	effects.scenePacketIndex = 4;
	effects.passPacketIndex = 5;
	effects.passPacketCount = 1;
	effects.specialEffectsMask = SPECIAL_EFFECT_BLUR | SPECIAL_EFFECT_AL;
	effects.ready = true;
	const std::uint64_t effectsHash = R_ClassicSpecialFrameDomain_HashView( effects );
	effects.specialEffectsMask = SPECIAL_EFFECT_BLUR;
	const bool hashSensitive = R_ClassicSpecialFrameDomain_HashView( effects )
		!= effectsHash;
	effects.specialEffectsMask = SPECIAL_EFFECT_BLUR | SPECIAL_EFFECT_AL;
	effects.hash = effectsHash;
	rg_classicSpecialFrameDomain.viewCount = 2;
	memset( &rg_classicSpecialFrameDomain.stats, 0,
		sizeof( rg_classicSpecialFrameDomain.stats ) );

	const bool demoOwned = R_ClassicSpecialFrameDomain_RecordOwned( &demoView,
		CLASSIC_SPECIAL_FRAME_SCOPE_RENDER_DEMO,
		CLASSIC_SPECIAL_FRAME_BACKEND_GL, 3 );
	const bool demoMismatch = !R_ClassicSpecialFrameDomain_RecordOwned( &demoView,
		CLASSIC_SPECIAL_FRAME_SCOPE_RENDER_DEMO,
		CLASSIC_SPECIAL_FRAME_BACKEND_VULKAN, 2 );
	const bool blurOwned = R_ClassicSpecialFrameDomain_RecordOwned( &effectsView,
		CLASSIC_SPECIAL_FRAME_SCOPE_RAVEN_EFFECTS,
		CLASSIC_SPECIAL_FRAME_BACKEND_VULKAN, SPECIAL_EFFECT_BLUR );
	const bool alOwned = R_ClassicSpecialFrameDomain_RecordOwned( &effectsView,
		CLASSIC_SPECIAL_FRAME_SCOPE_RAVEN_EFFECTS,
		CLASSIC_SPECIAL_FRAME_BACKEND_VULKAN, SPECIAL_EFFECT_AL );
	R_ClassicSpecialFrameDomain_RecordBackendFallback( &demoView,
		CLASSIC_SPECIAL_FRAME_SCOPE_RENDER_DEMO,
		CLASSIC_SPECIAL_FRAME_BACKEND_VULKAN,
		CLASSIC_SPECIAL_FRAME_FAILURE_BACKEND_COVERAGE_MISMATCH, 2 );
	const classicSpecialFrameDomainStats_t &stats =
		R_ClassicSpecialFrameDomain_Stats();
	const bool coverage = demoOwned && demoMismatch && blurOwned && alOwned
		&& demo.backendOutcome[ CLASSIC_SPECIAL_FRAME_BACKEND_GL ]
			== CLASSIC_SPECIAL_FRAME_BACKEND_OWNED
		&& demo.backendOutcome[ CLASSIC_SPECIAL_FRAME_BACKEND_VULKAN ]
			== CLASSIC_SPECIAL_FRAME_BACKEND_FALLBACK
		&& effects.backendOutcome[ CLASSIC_SPECIAL_FRAME_BACKEND_VULKAN ]
			== CLASSIC_SPECIAL_FRAME_BACKEND_OWNED
		&& effects.backendCoverage[ CLASSIC_SPECIAL_FRAME_BACKEND_VULKAN ]
			== ( SPECIAL_EFFECT_BLUR | SPECIAL_EFFECT_AL )
		&& stats.backend[ CLASSIC_SPECIAL_FRAME_BACKEND_GL ].ownedViews == 1
		&& stats.backend[ CLASSIC_SPECIAL_FRAME_BACKEND_VULKAN ].ownedViews == 1
		&& stats.backend[ CLASSIC_SPECIAL_FRAME_BACKEND_VULKAN ].fallbackViews == 1
		&& stats.backend[ CLASSIC_SPECIAL_FRAME_BACKEND_VULKAN ]
			.coverageMismatches == 1;
	R_ClassicSpecialFrameDomain_ResetFrame();
	return hashSensitive && coverage;
}
