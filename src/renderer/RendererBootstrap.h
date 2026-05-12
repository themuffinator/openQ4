// Copyright (C) 2004 Id Software, Inc.
//

#ifndef __RENDERER_BOOTSTRAP_H__
#define __RENDERER_BOOTSTRAP_H__

typedef struct rendererBootstrapState_s {
	rendererTierPreference_t	requestedPreference;
	rendererTier_t				selectedTier;
	renderFeatureSet_t			features;
	bool						legacyBridgeActive;
	bool						modernExecutorAvailable;
	char						summary[512];
} rendererBootstrapState_t;

void RendererBootstrap_BeginOpenGL( const renderBackendCaps_t &caps, const char *tierPreference );
void RendererBootstrap_FinalizeLegacyBridge( bool allowARB2Path );
void RendererBootstrap_Shutdown( void );
const rendererBootstrapState_t &RendererBootstrap_GetState( void );

#endif /* !__RENDERER_BOOTSTRAP_H__ */
