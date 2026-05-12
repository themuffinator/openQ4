// Copyright (C) 2004 Id Software, Inc.
//

#include "tr_local.h"
#include "RendererBootstrap.h"

static rendererBootstrapState_t rg_bootstrapState;

static const char *RendererBootstrap_PreferenceName( rendererTierPreference_t preference ) {
	switch ( preference ) {
	case RENDERER_TIER_PREF_LEGACY:
		return "legacy";
	case RENDERER_TIER_PREF_GL33:
		return "gl33";
	case RENDERER_TIER_PREF_GL41:
		return "gl41";
	case RENDERER_TIER_PREF_GL43:
		return "gl43";
	case RENDERER_TIER_PREF_GL45:
		return "gl45";
	case RENDERER_TIER_PREF_GL46:
		return "gl46";
	case RENDERER_TIER_PREF_AUTO:
	default:
		return "auto";
	}
}

static void RendererBootstrap_UpdateSummary( const renderBackendCaps_t &caps ) {
	char capsSummary[384];
	RendererCaps_FormatSummary( caps, capsSummary, sizeof( capsSummary ) );
	idStr::snPrintf(
		rg_bootstrapState.summary,
		sizeof( rg_bootstrapState.summary ),
		"selected=%s requested=%s bridge=%s modernExecutor=%s caps={%s}",
		RendererTier_Name( rg_bootstrapState.selectedTier ),
		RendererBootstrap_PreferenceName( rg_bootstrapState.requestedPreference ),
		rg_bootstrapState.legacyBridgeActive ? "ARB2" : "none",
		rg_bootstrapState.modernExecutorAvailable ? "yes" : "no",
		capsSummary );
}

void RendererBootstrap_BeginOpenGL( const renderBackendCaps_t &caps, const char *tierPreference ) {
	memset( &rg_bootstrapState, 0, sizeof( rg_bootstrapState ) );
	rg_bootstrapState.requestedPreference = RendererTierPreference_FromString( tierPreference );
	rg_bootstrapState.selectedTier = RendererTier_Select( caps, rg_bootstrapState.requestedPreference );
	rg_bootstrapState.features = RendererFeatureSet_Build( caps, rg_bootstrapState.selectedTier );
	rg_bootstrapState.modernExecutorAvailable = false;
	RendererBootstrap_UpdateSummary( caps );
}

void RendererBootstrap_FinalizeLegacyBridge( bool allowARB2Path ) {
	rg_bootstrapState.legacyBridgeActive = allowARB2Path;
	rg_bootstrapState.features.legacyARB2Bridge = allowARB2Path;
	RendererBootstrap_UpdateSummary( glConfig.backendCaps );
	common->Printf( "Renderer bootstrap: %s\n", rg_bootstrapState.summary );
	if ( RendererTier_IsModern( rg_bootstrapState.selectedTier ) && !rg_bootstrapState.modernExecutorAvailable ) {
		common->Printf( "Renderer bootstrap: modern tier is available, but execution is currently routed through the ARB2 compatibility bridge\n" );
	}
}

void RendererBootstrap_Shutdown( void ) {
	memset( &rg_bootstrapState, 0, sizeof( rg_bootstrapState ) );
}

const rendererBootstrapState_t &RendererBootstrap_GetState( void ) {
	return rg_bootstrapState;
}
