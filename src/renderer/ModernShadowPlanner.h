// Copyright (C) 2004 Id Software, Inc.
//

#ifndef __MODERN_SHADOW_PLANNER_H__
#define __MODERN_SHADOW_PLANNER_H__

#include "RendererCaps.h"
#include "ScenePackets.h"

enum modernShadowMapType_t {
	MODERN_SHADOW_MAP_NONE = 0,
	MODERN_SHADOW_MAP_PROJECTED,
	MODERN_SHADOW_MAP_POINT,
	MODERN_SHADOW_MAP_CASCADE
};

enum modernShadowPolicy_t {
	MODERN_SHADOW_POLICY_NONE = 0,
	MODERN_SHADOW_POLICY_MAPPED,
	MODERN_SHADOW_POLICY_STENCIL_FALLBACK,
	MODERN_SHADOW_POLICY_SKIPPED
};

enum modernShadowFallbackReason_t {
	MODERN_SHADOW_FALLBACK_NONE = 0,
	MODERN_SHADOW_FALLBACK_SHADOW_MAP_DISABLED,
	MODERN_SHADOW_FALLBACK_SHADOWS_DISABLED,
	MODERN_SHADOW_FALLBACK_NULL_LIGHT,
	MODERN_SHADOW_FALLBACK_NO_SHADOWS_FLAG,
	MODERN_SHADOW_FALLBACK_NO_DYNAMIC_SHADOWS_FLAG,
	MODERN_SHADOW_FALLBACK_AMBIENT_LIGHT,
	MODERN_SHADOW_FALLBACK_LIGHT_SHADER_NO_SHADOWS,
	MODERN_SHADOW_FALLBACK_TEXTURE_LIMIT,
	MODERN_SHADOW_FALLBACK_NO_RECEIVERS,
	MODERN_SHADOW_FALLBACK_CUBEMAP_UNAVAILABLE,
	MODERN_SHADOW_FALLBACK_BUDGET,
	MODERN_SHADOW_FALLBACK_RESOURCE_UNAVAILABLE,
	MODERN_SHADOW_FALLBACK_COUNT
};

typedef struct modernShadowLightDescriptor_s {
	const viewLight_t *	viewLight;
	int					descriptorIndex;
	int					sceneIndex;
	int					lightDefIndex;
	modernShadowMapType_t mapType;
	modernShadowPolicy_t policy;
	modernShadowFallbackReason_t fallbackReason;
	int					priority;
	int					updateModulo;
	int					resolution;
	int					tileCount;
	int					cascadeCount;
	int					estimatedPixels;
	int					localCasterCount;
	int					globalCasterCount;
	int					localReceiverCount;
	int					globalReceiverCount;
	int					translucentCasterCount;
	int					translucentReceiverCount;
	float				atlasRect[4];
	bool				pointLight;
	bool				parallel;
	bool				translucentMoments;
	bool				stencilFallback;
} modernShadowLightDescriptor_t;

typedef struct modernShadowPlannerStats_s {
	bool				initialized;
	bool				available;
	bool				requested;
	bool				frameValid;
	bool				shadowsEnabled;
	bool				shadowMapsEnabled;
	bool				csmRequested;
	bool				translucentRequested;
	bool				translucentEnabled;
	bool				debugOverlayRequested;
	bool				reportRequested;
	bool				overflow;
	int					sceneCount;
	int					viewLightCount;
	int					shadowRelevantLights;
	int					descriptorCount;
	int					mappedLights;
	int					fallbackLights;
	int					skippedLights;
	int					projectedLights;
	int					pointLights;
	int					parallelLights;
	int					cascadeLights;
	int					cascadeCount;
	int					mappedPasses;
	int					fallbackPasses;
	int					stencilFallbackPasses;
	int					localCasterCount;
	int					globalCasterCount;
	int					translucentCasterCount;
	int					localReceiverCount;
	int					globalReceiverCount;
	int					translucentReceiverCount;
	int					estimatedPixels;
	int					budgetedPixels;
	int					maxMappedLights;
	int					shadowMapSize;
	int					updateModulo;
	int					budgetThrottledLights;
	int					textureLimitFallbacks;
	int					cubemapFallbacks;
	int					noReceiverLights;
	int					buildMsec;
	char				status[96];
} modernShadowPlannerStats_t;

void R_ModernShadowPlanner_Init( const renderBackendCaps_t &caps, const renderFeatureSet_t &features );
void R_ModernShadowPlanner_Shutdown( void );
void R_ModernShadowPlanner_PrepareFrame( const idScenePacketFrame &packetFrame, bool requested );
const modernShadowPlannerStats_t &R_ModernShadowPlanner_Stats( void );
const modernShadowLightDescriptor_t *R_ModernShadowPlanner_DescriptorForLight( const viewLight_t *viewLight );
const modernShadowLightDescriptor_t *R_ModernShadowPlanner_DescriptorByIndex( int index );
int R_ModernShadowPlanner_NumDescriptors( void );
const char *ModernShadowMapType_Name( modernShadowMapType_t type );
const char *ModernShadowPolicy_Name( modernShadowPolicy_t policy );
const char *ModernShadowFallbackReason_Name( modernShadowFallbackReason_t reason );
void R_ModernShadowPlanner_PrintGfxInfo( void );
bool RendererShadowPlanner_RunSelfTest( void );

#endif /* !__MODERN_SHADOW_PLANNER_H__ */
