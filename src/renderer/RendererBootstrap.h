// Copyright (C) 2004 Id Software, Inc.
//

#ifndef __RENDERER_BOOTSTRAP_H__
#define __RENDERER_BOOTSTRAP_H__

typedef struct rendererDefaultPromotionState_s {
	bool						requestedAutoTier;
	bool						rendererRequestAllowsPromotion;
	bool						selectedModernTier;
	bool						compatibilityGatesPassed;
	bool						modernExecutorAvailable;
	bool						legacyEscapeAvailable;
	bool						validationEvidencePresent;
	bool						validationEvidenceReady;
	bool						validationWarningFree;
	bool						validationVisualCoverage;
	bool						validationGameplayCoverage;
	bool						validationRenderDocCoverage;
	bool						validationPerformanceCoverage;
	bool						validationPresentationCoverage;
	bool						validationRollbackCoverage;
	bool						validationDebugClean;
	bool						manualSignoffEnabled;
	bool						eligible;
	bool						active;
	char						reason[128];
	char						validationEvidenceMissing[256];
} rendererDefaultPromotionState_t;

typedef struct rendererBootstrapState_s {
	rendererTierPreference_t	requestedPreference;
	rendererTier_t				selectedTier;
	renderFeatureSet_t			features;
	bool						legacyBridgeActive;
	bool						modernExecutorAvailable;
	rendererDefaultPromotionState_t	defaultPromotion;
	char						summary[512];
} rendererBootstrapState_t;

void RendererBootstrap_BeginOpenGL( const renderBackendCaps_t &caps, const char *tierPreference );
void RendererBootstrap_FinalizeLegacyBridge( bool allowARB2Path );
void RendererBootstrap_SetModernExecutorAvailable( bool available );
bool RendererBootstrap_ShouldAutoPromoteModernVisible( void );
void RendererBootstrap_PrintGfxInfo( void );
void RendererBootstrap_Shutdown( void );
const rendererBootstrapState_t &RendererBootstrap_GetState( void );
bool RendererDefaultSafety_RunSelfTest( void );
bool RendererDefaultPromotion_RunSelfTest( void );

#endif /* !__RENDERER_BOOTSTRAP_H__ */
