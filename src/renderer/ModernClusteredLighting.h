// Copyright (C) 2004 Id Software, Inc.
//

#ifndef __MODERN_CLUSTERED_LIGHTING_H__
#define __MODERN_CLUSTERED_LIGHTING_H__

#include "RendererCaps.h"

class idScenePacketFrame;

enum rendererClusterDebugMode_t {
	RENDERER_CLUSTER_DEBUG_OFF = 0,
	RENDERER_CLUSTER_DEBUG_OCCUPANCY,
	RENDERER_CLUSTER_DEBUG_LIGHT_COUNT,
	RENDERER_CLUSTER_DEBUG_OVERFLOW
};

typedef struct rendererClusteredLightingStats_s {
	bool	available;
	bool	requested;
	bool	initialized;
	bool	frameValid;
	bool	buffersReady;
	bool	uboFallbackReady;
	bool	debugOverlayReady;
	bool	debugTextureReady;
	bool	shaderStorageReady;
	bool	overflow;
	int		gridCount;
	int		sceneCount;
	int		scenesWithLights;
	int		lightCount;
	int		pointLights;
	int		projectedLights;
	int		fogLights;
	int		ambientLights;
	int		specialLights;
	int		shadowMappedLights;
	int		shadowFallbackLights;
	int		shadowSkippedLights;
	int		shadowDescriptorCount;
	int		culledLights;
	int		clippedLights;
	int		overflowLights;
	int		clusterCount;
	int		activeClusters;
	int		overflowClusters;
	int		lightReferences;
	int		uploadedLights;
	int		uploadedClusters;
	int		uploadedReferences;
	int		spillClusters;
	int		spillReferences;
	int		overflowReferences;
	int		maxLightsInCluster;
	int		maxLightsPerCluster;
	int		indexGroupsPerCluster;
	int		lightCapacity;
	int		indexRecordCapacity;
	int		tileCountX;
	int		tileCountY;
	int		sliceCountZ;
	int		nearZ;
	int		farZ;
	int		buildMsec;
	int		bufferUploads;
	int		paramsUBOBytes;
	int		lightsUBOBytes;
	int		indicesUBOBytes;
	int		debugMode;
	int		debugOverlayDraws;
	int		debugStringTruncations;
	char	debugStringTruncationSource[64];
	char	status[96];
} rendererClusteredLightingStats_t;

void R_ModernClusteredLighting_Init( const renderBackendCaps_t &caps, const renderFeatureSet_t &features );
void R_ModernClusteredLighting_Shutdown( void );
void R_ModernClusteredLighting_PrepareFrame( const idScenePacketFrame &packetFrame, bool requested );
void R_ModernClusteredLighting_DrawDebugOverlay( void );
void R_ModernClusteredLighting_PrintGfxInfo( void );
const rendererClusteredLightingStats_t &R_ModernClusteredLighting_Stats( void );
bool RendererClusterGrid_RunSelfTest( void );

#endif /* !__MODERN_CLUSTERED_LIGHTING_H__ */
