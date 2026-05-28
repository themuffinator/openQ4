// Copyright (C) 2004 Id Software, Inc.
//

#ifndef __MODERN_CLUSTERED_LIGHTING_H__
#define __MODERN_CLUSTERED_LIGHTING_H__

#include "RendererCaps.h"

class idScenePacketFrame;
typedef struct viewDef_s viewDef_t;

enum rendererClusterDebugMode_t {
	RENDERER_CLUSTER_DEBUG_OFF = 0,
	RENDERER_CLUSTER_DEBUG_OCCUPANCY,
	RENDERER_CLUSTER_DEBUG_LIGHT_COUNT,
	RENDERER_CLUSTER_DEBUG_OVERFLOW
};

enum rendererModernLightType_t {
	RENDERER_MODERN_LIGHT_POINT = 0,
	RENDERER_MODERN_LIGHT_PROJECTED,
	RENDERER_MODERN_LIGHT_FOG,
	RENDERER_MODERN_LIGHT_AMBIENT,
	RENDERER_MODERN_LIGHT_BLEND,
	RENDERER_MODERN_LIGHT_SPECIAL
};

typedef struct rendererModernLightDescriptor_s {
	char	debugName[64];
	rendererModernLightType_t type;
	int		descriptorIndex;
	int		sceneIndex;
	int		lightDefIndex;
	int		areaNum;
	int		flags;
	int		shadowDescriptorIndex;
	int		shadowPolicy;
	int		shadowFallbackReason;
	bool	portalVisible;
	bool	fullDepthRange;
	float	worldOrigin[4];
	float	viewOriginRadius[4];
	float	color[4];
	float	scissorDepth[4];
	float	depthRange[4];
	float	falloff[4];
	float	projectS[4];
	float	projectT[4];
	float	projectQ[4];
	unsigned int projectionImageHandle;
	unsigned int falloffImageHandle;
	unsigned int cubeImageHandle;
	int		projectionFilter;
	int		projectionRepeat;
	int		falloffFilter;
	int		falloffRepeat;
} rendererModernLightDescriptor_t;

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
	bool	csrReady;
	bool	computeBinningReady;
	bool	computeBinningExecuted;
	bool	lossless;
	bool	overflow;
	int		gridCount;
	int		sceneCount;
	int		scenesWithLights;
	int		lightCount;
	int		pointLights;
	int		projectedLights;
	int		fogLights;
	int		ambientLights;
	int		blendLights;
	int		specialLights;
	int		shadowMappedLights;
	int		shadowFallbackLights;
	int		shadowSkippedLights;
	int		shadowDescriptorCount;
	int		shadowReceiverBlockedLights;
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
	int		unsampledSpillReferences;
	int		overflowReferences;
	int		lossyClusters;
	int		lossyReferences;
	int		maxLightsInCluster;
	int		maxLightsPerCluster;
	int		indexGroupsPerCluster;
	int		lightCapacity;
	int		indexRecordCapacity;
	int		clusterRecordCount;
	int		flatIndexRecordCount;
	int		flatIndexReferenceCapacity;
	int		uploadedGridIndexRecords;
	int		computeBinningDispatches;
	int		gridSwitches;
	int		gridBindFailures;
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
bool R_ModernClusteredLighting_FrameLossless( void );
bool R_ModernClusteredLighting_BindGridForView( const viewDef_t *viewDef );
int R_ModernClusteredLighting_NumLightDescriptors( void );
const rendererModernLightDescriptor_t *R_ModernClusteredLighting_LightDescriptor( int index );
bool RendererClusterGrid_RunSelfTest( void );

#endif /* !__MODERN_CLUSTERED_LIGHTING_H__ */
