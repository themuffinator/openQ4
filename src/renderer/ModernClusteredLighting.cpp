// Copyright (C) 2004 Id Software, Inc.
//

#include "tr_local.h"
#include "ModernClusteredLighting.h"
#include "GLDebugScope.h"
#include "GLStateCache.h"
#include "ModernShadowPlanner.h"
#include "RendererBenchmarks.h"
#include "RendererMetrics.h"

const int MODERN_CLUSTER_MAX_GRIDS = 16;
const int MODERN_CLUSTER_MAX_LIGHTS_UBO = 256;
const int MODERN_CLUSTER_MAX_LIGHTS_SSBO = 1024;
const int MODERN_CLUSTER_MAX_TILES_X = 16;
const int MODERN_CLUSTER_MAX_TILES_Y = 9;
const int MODERN_CLUSTER_MAX_SLICES_Z = 24;
const int MODERN_CLUSTER_MAX_LIGHTS_PER_CLUSTER = 32;
const int MODERN_CLUSTER_MAX_INDEX_RECORDS_UBO = 1024;
const int MODERN_CLUSTER_MAX_INDEX_RECORDS_SSBO = 32768;
const int MODERN_CLUSTER_MAX_CLUSTERS = 8192;
const int MODERN_CLUSTER_COMPUTE_WORKGROUP_SIZE = 64;
const int MODERN_CLUSTER_UBO_BINDING_PARAMS = 3;
const int MODERN_CLUSTER_UBO_BINDING_LIGHTS = 4;
const int MODERN_CLUSTER_UBO_BINDING_INDICES = 5;
const int MODERN_CLUSTER_SSBO_BINDING_LIGHTS = 6;
const int MODERN_CLUSTER_SSBO_BINDING_INDICES = 7;
const int MODERN_CLUSTER_LIGHT_FLAG_VIEW_INSIDE = 1 << 0;
const int MODERN_CLUSTER_LIGHT_FLAG_GLOBAL_ORIGIN_VISIBLE = 1 << 1;
const int MODERN_CLUSTER_LIGHT_FLAG_PARALLEL = 1 << 2;
const int MODERN_CLUSTER_LIGHT_FLAG_FULL_DEPTH = 1 << 3;
const int MODERN_CLUSTER_LIGHT_FLAG_SHADOW_MAPPED = 1 << 4;
const int MODERN_CLUSTER_LIGHT_FLAG_SHADOW_FALLBACK = 1 << 5;
const int MODERN_CLUSTER_LIGHT_FLAG_SHADOW_CASCADE = 1 << 6;
const int MODERN_CLUSTER_LIGHT_FLAG_SHADOW_POINT = 1 << 7;
const int MODERN_CLUSTER_LIGHT_FLAG_BLEND = 1 << 8;

typedef struct modernClusterGridRecord_s {
	const viewDef_t *	viewDef;
	char				debugName[64];
	int					sceneIndex;
	int					tileCountX;
	int					tileCountY;
	int					sliceCountZ;
	int					clusterOffset;
	int					clusterRecordOffset;
	int					clusterCount;
	int					indexRecordOffset;
	int					firstLightReference;
	int					uploadedLightReferences;
	int					maxLightsPerCluster;
	int					indexGroupsPerCluster;
	int					firstLight;
	int					lightCount;
	int					width;
	int					height;
	float				nearZ;
	float				farZ;
} modernClusterGridRecord_t;

typedef struct modernClusterLightRecord_s {
	char				debugName[64];
	rendererModernLightType_t type;
	int					sceneIndex;
	int					lightDefIndex;
	int					areaNum;
	int					flags;
	int					shadowDescriptorIndex;
	int					shadowPolicy;
	int					shadowFallbackReason;
	rendererModernLightDescriptor_t descriptor;
	idVec3				worldOrigin;
	idVec3				cameraOrigin;
	idVec3				color;
	float				radius;
	float				depthMin;
	float				depthMax;
	float				falloffScale;
	float				falloffBias;
	idPlane				viewLightProject[4];
	idScreenRect		scissor;
	bool				fullDepthRange;
} modernClusterLightRecord_t;

typedef struct modernClusterRecord_s {
	int					firstLightIndex;
	int					lightCount;
	int					uploadedLightCount;
	int					writeLightCount;
	bool				overflow;
} modernClusterRecord_t;

typedef struct modernClusterGridGpuParams_s {
	float				grid[4];
	float				depth[4];
	float				viewport[4];
	float				counts[4];
} modernClusterGridGpuParams_t;

typedef struct modernClusterLightGpuRecord_s {
	float				positionRadius[4];
	float				colorType[4];
	float				scissorDepth[4];
	float				flags[4];
	float				depthRange[4];
	float				falloff[4];
	float				projectS[4];
	float				projectT[4];
	float				projectQ[4];
} modernClusterLightGpuRecord_t;

typedef struct modernClusterIndexGpuRecord_s {
	GLuint				indices[4];
} modernClusterIndexGpuRecord_t;

typedef struct modernClusterDebugPixel_s {
	unsigned char		r;
	unsigned char		g;
	unsigned char		b;
	unsigned char		a;
} modernClusterDebugPixel_t;

typedef struct modernClusterFrameData_s {
	idList<modernClusterGridRecord_t> grids;
	idList<modernClusterLightRecord_t> lights;
	idList<modernClusterRecord_t> clusters;
	idList<GLuint> clusterLightIndices;
	idList<modernClusterLightGpuRecord_t> lightGpuRecords;
	idList<modernClusterIndexGpuRecord_t> indexGpuRecords;
	int					gridCount;
	int					lightCount;
	int					clusterCount;
	int					clusterRecordCount;
	int					flatIndexRecordCount;
	int					flatIndexReferenceCapacity;
	int					indexRecordCount;
	int					lightCapacity;
	int					indexRecordCapacity;
	bool				shaderStoragePath;
} modernClusterFrameData_t;

static rendererClusteredLightingStats_t rg_clusteredLightingStats;
static modernClusterFrameData_t rg_clusteredLightingFrame;
static renderBackendCaps_t rg_clusteredLightingCaps;
static renderFeatureSet_t rg_clusteredLightingFeatures;
static GLuint rg_clusteredLightingParamsUBO = 0;
static GLuint rg_clusteredLightingLightsUBO = 0;
static GLuint rg_clusteredLightingIndicesUBO = 0;
static GLuint rg_clusteredLightingLightsSSBO = 0;
static GLuint rg_clusteredLightingIndicesSSBO = 0;
static GLuint rg_clusteredLightingComputeProgram = 0;
static GLuint rg_clusteredLightingDebugTexture = 0;
static GLuint rg_clusteredLightingDebugProgram = 0;
static GLuint rg_clusteredLightingDebugVAO = 0;
static GLint rg_clusteredLightingComputeModeLocation = -1;
static GLint rg_clusteredLightingComputeLightRangeLocation = -1;
static GLint rg_clusteredLightingComputeFlatRangeLocation = -1;
static GLint rg_clusteredLightingDebugTextureLocation = -1;
static GLint rg_clusteredLightingDebugParamsLocation = -1;
static int rg_clusteredLightingDebugTextureWidth = 0;
static int rg_clusteredLightingDebugTextureHeight = 0;
static bool rg_clusteredLightingInitialized = false;
static bool rg_clusteredLightingAvailable = false;
static bool rg_clusteredLightingShaderStorageAvailable = false;
static int rg_clusteredLightingBoundGridIndex = -1;

static void R_ModernClusteredLighting_SetStatus( rendererClusteredLightingStats_t &stats, const char *status ) {
	idStr::Copynz( stats.status, status ? status : "unknown", sizeof( stats.status ) );
}

static void R_ModernClusteredLighting_RecordDebugStringTruncation( rendererClusteredLightingStats_t &stats, const char *source ) {
	stats.debugStringTruncations++;
	if ( stats.debugStringTruncationSource[0] == '\0' ) {
		idStr::Copynz( stats.debugStringTruncationSource, source ? source : "unknown", sizeof( stats.debugStringTruncationSource ) );
	}
}

static bool R_ModernClusteredLighting_FormatDebugString( char *dest, int destSize, const char *fmt, ... ) {
	va_list argptr;
	va_start( argptr, fmt );
	const int result = idStr::vsnPrintf( dest, destSize, fmt, argptr );
	va_end( argptr );
	return result >= 0;
}

static bool R_ModernClusteredLighting_UseShaderStoragePath( void ) {
	return rg_clusteredLightingShaderStorageAvailable
		&& rg_clusteredLightingLightsSSBO != 0
		&& rg_clusteredLightingIndicesSSBO != 0
		&& glBindBufferBase != NULL;
}

static bool R_ModernClusteredLighting_UseComputeBinningPath( void ) {
	return R_ModernClusteredLighting_UseShaderStoragePath()
		&& rg_clusteredLightingCaps.hasCompute
		&& rg_clusteredLightingComputeProgram != 0
		&& glDispatchCompute != NULL
		&& glMemoryBarrier != NULL;
}

static int R_ModernClusteredLighting_LightCapacity( void ) {
	return R_ModernClusteredLighting_UseShaderStoragePath() ? MODERN_CLUSTER_MAX_LIGHTS_SSBO : MODERN_CLUSTER_MAX_LIGHTS_UBO;
}

static int R_ModernClusteredLighting_IndexRecordCapacity( void ) {
	return R_ModernClusteredLighting_UseShaderStoragePath() ? MODERN_CLUSTER_MAX_INDEX_RECORDS_SSBO : MODERN_CLUSTER_MAX_INDEX_RECORDS_UBO;
}

static bool R_ModernClusteredLighting_UseDirectBufferUpdates( void ) {
	return rg_clusteredLightingFeatures.directStateAccess
		&& rg_clusteredLightingCaps.hasDSA
		&& glNamedBufferSubData != NULL;
}

static void R_ModernClusteredLighting_UpdateBuffer( GLenum target, GLuint buffer, GLintptr offset, GLsizeiptr bytes, const void *data ) {
	if ( buffer == 0 || bytes <= 0 ) {
		return;
	}
	if ( R_ModernClusteredLighting_UseDirectBufferUpdates() ) {
		glNamedBufferSubData( buffer, offset, bytes, data );
		return;
	}
	R_GLStateCache().BindBuffer( target, buffer );
	glBufferSubData( target, offset, bytes, data );
}

static void R_ModernClusteredLighting_BindGpuBuffers( bool useShaderStorage ) {
	if ( useShaderStorage ) {
		R_GLStateCache().BindBufferBase( GL_UNIFORM_BUFFER, MODERN_CLUSTER_UBO_BINDING_PARAMS, rg_clusteredLightingParamsUBO );
		const GLuint ssboBuffers[2] = { rg_clusteredLightingLightsSSBO, rg_clusteredLightingIndicesSSBO };
		R_GLStateCache().BindBuffersBase( GL_SHADER_STORAGE_BUFFER, MODERN_CLUSTER_SSBO_BINDING_LIGHTS, 2, ssboBuffers );
		return;
	}

	const GLuint uboBuffers[3] = { rg_clusteredLightingParamsUBO, rg_clusteredLightingLightsUBO, rg_clusteredLightingIndicesUBO };
	R_GLStateCache().BindBuffersBase( GL_UNIFORM_BUFFER, MODERN_CLUSTER_UBO_BINDING_PARAMS, 3, uboBuffers );
}

static void R_ModernClusteredLighting_ResetFrameData( void ) {
	rg_clusteredLightingFrame.grids.SetNum( 0, false );
	rg_clusteredLightingFrame.lights.SetNum( 0, false );
	rg_clusteredLightingFrame.clusters.SetNum( 0, false );
	rg_clusteredLightingFrame.clusterLightIndices.SetNum( 0, false );
	rg_clusteredLightingFrame.lightGpuRecords.SetNum( 0, false );
	rg_clusteredLightingFrame.indexGpuRecords.SetNum( 0, false );
	rg_clusteredLightingFrame.gridCount = 0;
	rg_clusteredLightingFrame.lightCount = 0;
	rg_clusteredLightingFrame.clusterCount = 0;
	rg_clusteredLightingFrame.clusterRecordCount = 0;
	rg_clusteredLightingFrame.flatIndexRecordCount = 0;
	rg_clusteredLightingFrame.flatIndexReferenceCapacity = 0;
	rg_clusteredLightingFrame.indexRecordCount = 0;
	rg_clusteredLightingFrame.shaderStoragePath = R_ModernClusteredLighting_UseShaderStoragePath();
	rg_clusteredLightingFrame.lightCapacity = R_ModernClusteredLighting_LightCapacity();
	rg_clusteredLightingFrame.indexRecordCapacity = R_ModernClusteredLighting_IndexRecordCapacity();
	rg_clusteredLightingBoundGridIndex = -1;
}

static const char *R_ModernClusteredLighting_TypeName( rendererModernLightType_t type ) {
	switch ( type ) {
	case RENDERER_MODERN_LIGHT_POINT: return "point";
	case RENDERER_MODERN_LIGHT_PROJECTED: return "projected";
	case RENDERER_MODERN_LIGHT_FOG: return "fog";
	case RENDERER_MODERN_LIGHT_AMBIENT: return "ambient";
	case RENDERER_MODERN_LIGHT_BLEND: return "blend";
	case RENDERER_MODERN_LIGHT_SPECIAL: return "special";
	default: return "unknown";
	}
}

static int R_ModernClusteredLighting_ViewWidth( const viewDef_t *viewDef ) {
	if ( viewDef == NULL ) {
		return Max( 1, glConfig.vidWidth );
	}
	const int viewportWidth = viewDef->viewport.x2 >= viewDef->viewport.x1 ? viewDef->viewport.x2 + 1 - viewDef->viewport.x1 : 0;
	if ( viewportWidth > 0 ) {
		return viewportWidth;
	}
	return Max( 1, viewDef->renderView.width );
}

static int R_ModernClusteredLighting_ViewHeight( const viewDef_t *viewDef ) {
	if ( viewDef == NULL ) {
		return Max( 1, glConfig.vidHeight );
	}
	const int viewportHeight = viewDef->viewport.y2 >= viewDef->viewport.y1 ? viewDef->viewport.y2 + 1 - viewDef->viewport.y1 : 0;
	if ( viewportHeight > 0 ) {
		return viewportHeight;
	}
	return Max( 1, viewDef->renderView.height );
}

static void R_ModernClusteredLighting_ClampRectToView( idScreenRect &rect, const viewDef_t *viewDef ) {
	const int width = R_ModernClusteredLighting_ViewWidth( viewDef );
	const int height = R_ModernClusteredLighting_ViewHeight( viewDef );
	if ( rect.IsEmpty() ) {
		rect.x1 = 0;
		rect.y1 = 0;
		rect.x2 = static_cast<short>( width - 1 );
		rect.y2 = static_cast<short>( height - 1 );
		return;
	}
	rect.x1 = static_cast<short>( idMath::ClampInt( 0, width - 1, rect.x1 ) );
	rect.x2 = static_cast<short>( idMath::ClampInt( 0, width - 1, rect.x2 ) );
	rect.y1 = static_cast<short>( idMath::ClampInt( 0, height - 1, rect.y1 ) );
	rect.y2 = static_cast<short>( idMath::ClampInt( 0, height - 1, rect.y2 ) );
}

static int R_ModernClusteredLighting_CeilDiv( int value, int divisor ) {
	return divisor > 0 ? ( value + divisor - 1 ) / divisor : value;
}

static int R_ModernClusteredLighting_DepthSliceForZ( const modernClusterGridRecord_t &grid, float z ) {
	if ( grid.sliceCountZ <= 1 ) {
		return 0;
	}
	const float nearZ = Max( 0.01f, grid.nearZ );
	const float farZ = Max( nearZ + 1.0f, grid.farZ );
	const float clampedZ = idMath::ClampFloat( nearZ, farZ, z );
	const float denom = Max( 0.0001f, idMath::Log( farZ / nearZ ) );
	const float normalized = idMath::ClampFloat( 0.0f, 1.0f, idMath::Log( clampedZ / nearZ ) / denom );
	return idMath::ClampInt( 0, grid.sliceCountZ - 1, idMath::FtoiFast( normalized * static_cast<float>( grid.sliceCountZ ) ) );
}

static void R_ModernClusteredLighting_CameraPoint( const viewDef_t *viewDef, const idVec3 &worldPoint, idVec3 &cameraPoint ) {
	if ( viewDef == NULL ) {
		cameraPoint = worldPoint;
		return;
	}
	const idVec3 delta = worldPoint - viewDef->renderView.vieworg;
	cameraPoint.x = delta * viewDef->renderView.viewaxis[1];
	cameraPoint.y = delta * viewDef->renderView.viewaxis[2];
	cameraPoint.z = delta * viewDef->renderView.viewaxis[0];
}

static float R_ModernClusteredLighting_LightRadius( const viewLight_t *vLight ) {
	if ( vLight == NULL ) {
		return 1.0f;
	}
	float radius = vLight->lightRadius.Length();
	if ( radius <= 1.0f && vLight->lightDef != NULL ) {
		radius = vLight->lightDef->parms.lightRadius.Length();
	}
	if ( radius <= 1.0f ) {
		radius = vLight->pointLight ? 256.0f : 1024.0f;
	}
	return idMath::ClampFloat( 1.0f, 32768.0f, radius );
}

static rendererModernLightType_t R_ModernClusteredLighting_ClassifyLight( const viewLight_t *vLight ) {
	if ( vLight == NULL ) {
		return RENDERER_MODERN_LIGHT_SPECIAL;
	}
	const idMaterial *shader = vLight->lightShader;
	if ( shader == NULL && vLight->lightDef != NULL ) {
		shader = vLight->lightDef->lightShader;
	}
	if ( shader != NULL ) {
		if ( shader->IsFogLight() ) {
			return RENDERER_MODERN_LIGHT_FOG;
		}
		if ( shader->IsAmbientLight() ) {
			return RENDERER_MODERN_LIGHT_AMBIENT;
		}
		if ( shader->IsBlendLight() ) {
			return RENDERER_MODERN_LIGHT_BLEND;
		}
	}
	if ( vLight->pointLight ) {
		return RENDERER_MODERN_LIGHT_POINT;
	}
	if ( vLight->parallel ) {
		return RENDERER_MODERN_LIGHT_SPECIAL;
	}
	return RENDERER_MODERN_LIGHT_PROJECTED;
}

static idVec3 R_ModernClusteredLighting_LightColor( const viewLight_t *vLight ) {
	idVec3 color( 1.0f, 1.0f, 1.0f );
	if ( vLight != NULL && vLight->lightDef != NULL ) {
		color.x = vLight->lightDef->parms.shaderParms[SHADERPARM_RED];
		color.y = vLight->lightDef->parms.shaderParms[SHADERPARM_GREEN];
		color.z = vLight->lightDef->parms.shaderParms[SHADERPARM_BLUE];
	}
	if ( color.x == 0.0f && color.y == 0.0f && color.z == 0.0f ) {
		color.Set( 1.0f, 1.0f, 1.0f );
	}
	return color;
}

static bool R_ModernClusteredLighting_LightAreaVisible( const viewDef_t *viewDef, const viewLight_t *vLight ) {
	if ( viewDef == NULL || vLight == NULL || vLight->lightDef == NULL || viewDef->connectedAreas == NULL ) {
		return true;
	}
	const int areaNum = vLight->lightDef->areaNum;
	if ( areaNum < 0 ) {
		return true;
	}
	return viewDef->connectedAreas[areaNum];
}

static void R_ModernClusteredLighting_CountLightType( rendererModernLightType_t type, rendererClusteredLightingStats_t &stats ) {
	switch ( type ) {
	case RENDERER_MODERN_LIGHT_POINT: stats.pointLights++; break;
	case RENDERER_MODERN_LIGHT_PROJECTED: stats.projectedLights++; break;
	case RENDERER_MODERN_LIGHT_FOG: stats.fogLights++; break;
	case RENDERER_MODERN_LIGHT_AMBIENT: stats.ambientLights++; break;
	case RENDERER_MODERN_LIGHT_BLEND: stats.blendLights++; break;
	case RENDERER_MODERN_LIGHT_SPECIAL: stats.specialLights++; break;
	default: stats.specialLights++; break;
	}
}

static void R_ModernClusteredLighting_ApplyShadowDescriptor( modernClusterLightRecord_t &record, const viewLight_t *vLight, rendererClusteredLightingStats_t &stats ) {
	record.shadowDescriptorIndex = -1;
	record.shadowPolicy = MODERN_SHADOW_POLICY_NONE;
	record.shadowFallbackReason = MODERN_SHADOW_FALLBACK_NONE;

	const modernShadowLightDescriptor_t *shadow = R_ModernShadowPlanner_DescriptorForLight( vLight );
	if ( shadow == NULL ) {
		return;
	}

	record.shadowDescriptorIndex = shadow->descriptorIndex;
	record.shadowPolicy = shadow->policy;
	record.shadowFallbackReason = shadow->fallbackReason;
	stats.shadowDescriptorCount = Max( stats.shadowDescriptorCount, shadow->descriptorIndex + 1 );

	if ( shadow->policy == MODERN_SHADOW_POLICY_MAPPED && !shadow->modernReceiverSamplingReady ) {
		record.shadowPolicy = MODERN_SHADOW_POLICY_STENCIL_FALLBACK;
		record.shadowFallbackReason = MODERN_SHADOW_FALLBACK_RECEIVER_SAMPLING_UNAVAILABLE;
		record.flags |= MODERN_CLUSTER_LIGHT_FLAG_SHADOW_FALLBACK;
		stats.shadowFallbackLights++;
		stats.shadowReceiverBlockedLights++;
	} else if ( shadow->policy == MODERN_SHADOW_POLICY_MAPPED ) {
		record.flags |= MODERN_CLUSTER_LIGHT_FLAG_SHADOW_MAPPED;
		stats.shadowMappedLights++;
	} else if ( shadow->policy == MODERN_SHADOW_POLICY_STENCIL_FALLBACK ) {
		record.flags |= MODERN_CLUSTER_LIGHT_FLAG_SHADOW_FALLBACK;
		stats.shadowFallbackLights++;
	} else if ( shadow->policy == MODERN_SHADOW_POLICY_SKIPPED ) {
		stats.shadowSkippedLights++;
	}

	if ( shadow->mapType == MODERN_SHADOW_MAP_CASCADE ) {
		record.flags |= MODERN_CLUSTER_LIGHT_FLAG_SHADOW_CASCADE;
	} else if ( shadow->mapType == MODERN_SHADOW_MAP_POINT ) {
		record.flags |= MODERN_CLUSTER_LIGHT_FLAG_SHADOW_POINT;
	}
}

static int R_ModernClusteredLighting_CountViewLights( const viewDef_t *viewDef ) {
	int count = 0;
	if ( viewDef == NULL ) {
		return count;
	}
	for ( const viewLight_t *vLight = viewDef->viewLights; vLight != NULL; vLight = vLight->next ) {
		count++;
	}
	return count;
}

static void R_ModernClusteredLighting_ClampGridToIndexBudget( int &tilesX, int &tilesY, int &slicesZ, int targetIndexGroups, int indexRecordCapacity ) {
	targetIndexGroups = Max( 1, targetIndexGroups );
	indexRecordCapacity = Max( 1, indexRecordCapacity );
	while ( tilesX * tilesY * slicesZ * targetIndexGroups > indexRecordCapacity && ( tilesX > 1 || tilesY > 1 || slicesZ > 1 ) ) {
		if ( slicesZ >= tilesX && slicesZ >= tilesY && slicesZ > 1 ) {
			slicesZ--;
		} else if ( tilesX >= tilesY && tilesX > 1 ) {
			tilesX--;
		} else if ( tilesY > 1 ) {
			tilesY--;
		} else if ( slicesZ > 1 ) {
			slicesZ--;
		}
	}
}

static int R_ModernClusteredLighting_TargetLightsPerCluster( int estimatedLights, const rendererBenchmarkBudget_t &budget ) {
	int target = 8;
	if ( estimatedLights > budget.lightBatchTarget ) {
		target = 16;
	} else if ( estimatedLights > Max( 8, budget.lightBatchTarget / 2 ) ) {
		target = 12;
	}
	if ( rg_clusteredLightingFeatures.gpuDriven ) {
		target = Max( target, 16 );
	}
	if ( rg_clusteredLightingFeatures.lowOverhead ) {
		target = Max( target, 24 );
	}
	return idMath::ClampInt( 4, MODERN_CLUSTER_MAX_LIGHTS_PER_CLUSTER, target );
}

static void R_ModernClusteredLighting_InitGrid( modernClusterGridRecord_t &grid, const viewDef_t *viewDef, int sceneIndex, int clusterOffset, int availableIndexRecords, int estimatedLights, const rendererBenchmarkBudget_t &budget ) {
	memset( &grid, 0, sizeof( grid ) );
	grid.viewDef = viewDef;
	grid.sceneIndex = sceneIndex;
	grid.width = R_ModernClusteredLighting_ViewWidth( viewDef );
	grid.height = R_ModernClusteredLighting_ViewHeight( viewDef );
	int targetTilesX = idMath::ClampInt( 1, MODERN_CLUSTER_MAX_TILES_X, budget.clusterTilesX );
	int targetTilesY = idMath::ClampInt( 1, MODERN_CLUSTER_MAX_TILES_Y, budget.clusterTilesY );
	int targetSlicesZ = idMath::ClampInt( 1, MODERN_CLUSTER_MAX_SLICES_Z, budget.clusterSlicesZ );
	if ( RendererBenchmarks_AdaptiveClusterGridEnabled() ) {
		targetTilesX = idMath::ClampInt( targetTilesX, MODERN_CLUSTER_MAX_TILES_X, Max( targetTilesX, R_ModernClusteredLighting_CeilDiv( grid.width, 160 ) ) );
		targetTilesY = idMath::ClampInt( targetTilesY, MODERN_CLUSTER_MAX_TILES_Y, Max( targetTilesY, R_ModernClusteredLighting_CeilDiv( grid.height, 160 ) ) );
		if ( estimatedLights > budget.lightBatchTarget ) {
			targetSlicesZ = idMath::ClampInt( targetSlicesZ, MODERN_CLUSTER_MAX_SLICES_Z, targetSlicesZ + 4 );
		}
	}
	const int requestedLightsPerCluster = R_ModernClusteredLighting_TargetLightsPerCluster( estimatedLights, budget );
	R_ModernClusteredLighting_ClampGridToIndexBudget( targetTilesX, targetTilesY, targetSlicesZ, 1, availableIndexRecords );
	const int targetTileWidth = Max( 1, grid.width / targetTilesX );
	const int targetTileHeight = Max( 1, grid.height / targetTilesY );
	grid.tileCountX = idMath::ClampInt( 1, targetTilesX, R_ModernClusteredLighting_CeilDiv( grid.width, targetTileWidth ) );
	grid.tileCountY = idMath::ClampInt( 1, targetTilesY, R_ModernClusteredLighting_CeilDiv( grid.height, targetTileHeight ) );
	grid.sliceCountZ = targetSlicesZ;
	grid.clusterOffset = clusterOffset;
	grid.clusterRecordOffset = clusterOffset;
	grid.clusterCount = grid.tileCountX * grid.tileCountY * grid.sliceCountZ;
	grid.indexRecordOffset = 0;
	grid.firstLightReference = 0;
	grid.uploadedLightReferences = 0;
	grid.maxLightsPerCluster = requestedLightsPerCluster;
	grid.indexGroupsPerCluster = R_ModernClusteredLighting_CeilDiv( requestedLightsPerCluster, 4 );
	grid.firstLight = rg_clusteredLightingFrame.lightCount;
	grid.lightCount = 0;
	grid.nearZ = viewDef != NULL && viewDef->renderView.cramZNear ? 0.25f : Max( 0.01f, r_znear.GetFloat() );
	grid.farZ = 4096.0f;
	R_ModernClusteredLighting_FormatDebugString( grid.debugName, sizeof( grid.debugName ), "scene%d_%dx%dx%d", sceneIndex, grid.tileCountX, grid.tileCountY, grid.sliceCountZ );
}

static int R_ModernClusteredLighting_ClusterIndex( const modernClusterGridRecord_t &grid, int tileX, int tileY, int sliceZ ) {
	return grid.clusterOffset + ( sliceZ * grid.tileCountY + tileY ) * grid.tileCountX + tileX;
}

static void R_ModernClusteredLighting_CountClusterReference( modernClusterRecord_t &cluster, rendererClusteredLightingStats_t &stats ) {
	cluster.lightCount++;
	stats.lightReferences++;
	stats.maxLightsInCluster = Max( stats.maxLightsInCluster, cluster.lightCount );
}

static void R_ModernClusteredLighting_FillClusterReference( modernClusterRecord_t &cluster, int lightIndex ) {
	if ( cluster.firstLightIndex < 0 || cluster.writeLightCount >= cluster.uploadedLightCount ) {
		return;
	}
	const int dstIndex = cluster.firstLightIndex + cluster.writeLightCount;
	if ( dstIndex < 0 || dstIndex >= rg_clusteredLightingFrame.clusterLightIndices.Num() ) {
		return;
	}
	rg_clusteredLightingFrame.clusterLightIndices[dstIndex] = static_cast<GLuint>( lightIndex );
	cluster.writeLightCount++;
}

static void R_ModernClusteredLighting_BinLight( const modernClusterGridRecord_t &grid, int lightIndex, const modernClusterLightRecord_t &light, rendererClusteredLightingStats_t &stats, bool fillReferences ) {
	const int tileMinX = idMath::ClampInt( 0, grid.tileCountX - 1, ( light.scissor.x1 * grid.tileCountX ) / Max( 1, grid.width ) );
	const int tileMaxX = idMath::ClampInt( 0, grid.tileCountX - 1, ( light.scissor.x2 * grid.tileCountX ) / Max( 1, grid.width ) );
	const int tileMinY = idMath::ClampInt( 0, grid.tileCountY - 1, ( light.scissor.y1 * grid.tileCountY ) / Max( 1, grid.height ) );
	const int tileMaxY = idMath::ClampInt( 0, grid.tileCountY - 1, ( light.scissor.y2 * grid.tileCountY ) / Max( 1, grid.height ) );
	const int sliceMinZ = light.fullDepthRange ? 0 : R_ModernClusteredLighting_DepthSliceForZ( grid, light.depthMin );
	const int sliceMaxZ = light.fullDepthRange ? grid.sliceCountZ - 1 : R_ModernClusteredLighting_DepthSliceForZ( grid, light.depthMax );

	for ( int z = sliceMinZ; z <= sliceMaxZ; ++z ) {
		for ( int y = tileMinY; y <= tileMaxY; ++y ) {
			for ( int x = tileMinX; x <= tileMaxX; ++x ) {
				const int clusterIndex = R_ModernClusteredLighting_ClusterIndex( grid, x, y, z );
				if ( clusterIndex >= 0 && clusterIndex < rg_clusteredLightingFrame.clusterCount ) {
					if ( fillReferences ) {
						R_ModernClusteredLighting_FillClusterReference( rg_clusteredLightingFrame.clusters[clusterIndex], lightIndex );
					} else {
						R_ModernClusteredLighting_CountClusterReference( rg_clusteredLightingFrame.clusters[clusterIndex], stats );
					}
				}
			}
		}
	}
}

static unsigned int R_ModernClusteredLighting_ImageHandle( const idImage *image ) {
	if ( image == NULL || !image->IsLoaded() ) {
		return 0;
	}
	return const_cast<idImage *>( image )->GetDeviceHandle();
}

static const idImage *R_ModernClusteredLighting_FirstStageImage( const idMaterial *material ) {
	if ( material == NULL ) {
		return NULL;
	}
	for ( int stageIndex = 0; stageIndex < material->GetNumStages(); ++stageIndex ) {
		const shaderStage_t *stage = material->GetStage( stageIndex );
		if ( stage != NULL && stage->texture.image != NULL ) {
			return stage->texture.image;
		}
	}
	return NULL;
}

static void R_ModernClusteredLighting_ViewPlaneForLightProject( const viewDef_t *viewDef, const idPlane &worldPlane, float out[4] ) {
	if ( viewDef == NULL ) {
		out[0] = worldPlane[0];
		out[1] = worldPlane[1];
		out[2] = worldPlane[2];
		out[3] = worldPlane[3];
		return;
	}
	const idVec3 &normal = worldPlane.Normal();
	out[0] = normal * viewDef->renderView.viewaxis[1];
	out[1] = normal * viewDef->renderView.viewaxis[2];
	out[2] = normal * viewDef->renderView.viewaxis[0];
	out[3] = worldPlane.Distance( viewDef->renderView.vieworg );
}

static void R_ModernClusteredLighting_CopyPlane( const float src[4], idPlane &dst ) {
	dst[0] = src[0];
	dst[1] = src[1];
	dst[2] = src[2];
	dst[3] = src[3];
}

static void R_ModernClusteredLighting_FillDescriptor( modernClusterLightRecord_t &record, const modernClusterGridRecord_t &grid, const viewLight_t *vLight ) {
	rendererModernLightDescriptor_t &descriptor = record.descriptor;
	memset( &descriptor, 0, sizeof( descriptor ) );
	descriptor.type = record.type;
	descriptor.descriptorIndex = rg_clusteredLightingFrame.lightCount;
	descriptor.sceneIndex = record.sceneIndex;
	descriptor.lightDefIndex = record.lightDefIndex;
	descriptor.areaNum = record.areaNum;
	descriptor.flags = record.flags;
	descriptor.shadowDescriptorIndex = record.shadowDescriptorIndex;
	descriptor.shadowPolicy = record.shadowPolicy;
	descriptor.shadowFallbackReason = record.shadowFallbackReason;
	descriptor.portalVisible = true;
	descriptor.fullDepthRange = record.fullDepthRange;
	descriptor.worldOrigin[0] = record.worldOrigin.x;
	descriptor.worldOrigin[1] = record.worldOrigin.y;
	descriptor.worldOrigin[2] = record.worldOrigin.z;
	descriptor.worldOrigin[3] = 1.0f;
	descriptor.viewOriginRadius[0] = record.cameraOrigin.x;
	descriptor.viewOriginRadius[1] = record.cameraOrigin.y;
	descriptor.viewOriginRadius[2] = record.cameraOrigin.z;
	descriptor.viewOriginRadius[3] = record.radius;
	descriptor.color[0] = record.color.x;
	descriptor.color[1] = record.color.y;
	descriptor.color[2] = record.color.z;
	descriptor.color[3] = 1.0f;
	descriptor.scissorDepth[0] = static_cast<float>( record.scissor.x1 );
	descriptor.scissorDepth[1] = static_cast<float>( record.scissor.y1 );
	descriptor.scissorDepth[2] = static_cast<float>( record.scissor.x2 );
	descriptor.scissorDepth[3] = static_cast<float>( record.scissor.y2 );
	descriptor.depthRange[0] = record.depthMin;
	descriptor.depthRange[1] = record.depthMax;
	descriptor.depthRange[2] = grid.nearZ;
	descriptor.depthRange[3] = grid.farZ;
	descriptor.falloff[0] = record.falloffScale;
	descriptor.falloff[1] = record.falloffBias;
	descriptor.falloff[2] = record.fullDepthRange ? 1.0f : 0.0f;
	descriptor.falloff[3] = 0.0f;
	R_ModernClusteredLighting_ViewPlaneForLightProject( grid.viewDef, vLight->lightProject[0], descriptor.projectS );
	R_ModernClusteredLighting_ViewPlaneForLightProject( grid.viewDef, vLight->lightProject[1], descriptor.projectT );
	R_ModernClusteredLighting_ViewPlaneForLightProject( grid.viewDef, vLight->lightProject[3], descriptor.projectQ );
	R_ModernClusteredLighting_CopyPlane( descriptor.projectS, record.viewLightProject[0] );
	R_ModernClusteredLighting_CopyPlane( descriptor.projectT, record.viewLightProject[1] );
	R_ModernClusteredLighting_CopyPlane( descriptor.projectQ, record.viewLightProject[3] );

	const idMaterial *lightShader = vLight->lightShader != NULL ? vLight->lightShader : ( vLight->lightDef != NULL ? vLight->lightDef->lightShader : NULL );
	const idImage *projectionImage = R_ModernClusteredLighting_FirstStageImage( lightShader );
	const idImage *falloffImage = vLight->falloffImage != NULL ? vLight->falloffImage : ( lightShader != NULL ? lightShader->LightFalloffImage() : NULL );
	descriptor.projectionImageHandle = R_ModernClusteredLighting_ImageHandle( projectionImage );
	descriptor.falloffImageHandle = R_ModernClusteredLighting_ImageHandle( falloffImage );
	descriptor.cubeImageHandle = vLight->pointLight ? descriptor.projectionImageHandle : 0;
	descriptor.projectionFilter = projectionImage != NULL ? projectionImage->GetFilter() : TF_DEFAULT;
	descriptor.projectionRepeat = projectionImage != NULL ? projectionImage->GetRepeat() : TR_CLAMP;
	descriptor.falloffFilter = falloffImage != NULL ? falloffImage->GetFilter() : TF_DEFAULT;
	descriptor.falloffRepeat = falloffImage != NULL ? falloffImage->GetRepeat() : TR_CLAMP;
	idStr::Copynz( descriptor.debugName, record.debugName, sizeof( descriptor.debugName ) );
}

static bool R_ModernClusteredLighting_AddLight( modernClusterGridRecord_t &grid, const viewLight_t *vLight, rendererClusteredLightingStats_t &stats ) {
	if ( vLight == NULL ) {
		return false;
	}
	if ( !R_ModernClusteredLighting_LightAreaVisible( grid.viewDef, vLight ) ) {
		stats.culledLights++;
		return false;
	}
	idScreenRect scissor = vLight->scissorRect;
	if ( scissor.IsEmpty() ) {
		stats.clippedLights++;
		return false;
	}
	R_ModernClusteredLighting_ClampRectToView( scissor, grid.viewDef );
	if ( scissor.IsEmpty() ) {
		stats.clippedLights++;
		return false;
	}
	if ( rg_clusteredLightingFrame.lightCount >= rg_clusteredLightingFrame.lightCapacity ) {
		stats.overflow = true;
		stats.overflowLights++;
		return false;
	}

	rg_clusteredLightingFrame.lights.SetNum( rg_clusteredLightingFrame.lightCount + 1, false );
	modernClusterLightRecord_t &record = rg_clusteredLightingFrame.lights[rg_clusteredLightingFrame.lightCount];
	memset( &record, 0, sizeof( record ) );
	record.type = R_ModernClusteredLighting_ClassifyLight( vLight );
	record.sceneIndex = grid.sceneIndex;
	record.lightDefIndex = vLight->lightDef != NULL ? vLight->lightDef->index : -1;
	record.areaNum = vLight->lightDef != NULL ? vLight->lightDef->areaNum : -1;
	record.worldOrigin = vLight->globalLightOrigin;
	record.color = R_ModernClusteredLighting_LightColor( vLight );
	record.radius = R_ModernClusteredLighting_LightRadius( vLight );
	record.scissor = scissor;
	R_ModernClusteredLighting_CameraPoint( grid.viewDef, record.worldOrigin, record.cameraOrigin );
	record.fullDepthRange = record.type == RENDERER_MODERN_LIGHT_FOG || record.type == RENDERER_MODERN_LIGHT_AMBIENT || record.type == RENDERER_MODERN_LIGHT_BLEND || record.type == RENDERER_MODERN_LIGHT_SPECIAL;
	record.depthMin = record.fullDepthRange ? grid.nearZ : Max( grid.nearZ, record.cameraOrigin.z - record.radius );
	record.depthMax = record.fullDepthRange ? grid.farZ : Min( grid.farZ, record.cameraOrigin.z + record.radius );
	record.falloffScale = record.radius > 1.0f ? 1.0f / record.radius : 1.0f;
	record.falloffBias = 0.0f;
	if ( record.depthMax < grid.nearZ || record.depthMin > grid.farZ ) {
		stats.culledLights++;
		return false;
	}
	record.flags =
		( vLight->viewInsideLight ? MODERN_CLUSTER_LIGHT_FLAG_VIEW_INSIDE : 0 ) |
		( vLight->viewSeesGlobalLightOrigin ? MODERN_CLUSTER_LIGHT_FLAG_GLOBAL_ORIGIN_VISIBLE : 0 ) |
		( vLight->parallel ? MODERN_CLUSTER_LIGHT_FLAG_PARALLEL : 0 ) |
		( record.fullDepthRange ? MODERN_CLUSTER_LIGHT_FLAG_FULL_DEPTH : 0 ) |
		( record.type == RENDERER_MODERN_LIGHT_BLEND ? MODERN_CLUSTER_LIGHT_FLAG_BLEND : 0 );
	R_ModernClusteredLighting_ApplyShadowDescriptor( record, vLight, stats );
	if ( r_rendererMetrics.GetInteger() >= 2 || r_rendererClusterDebug.GetInteger() > 0 ) {
		const char *shaderName = vLight->lightShader != NULL ? vLight->lightShader->GetName() : ( vLight->lightDef != NULL && vLight->lightDef->lightShader != NULL ? vLight->lightDef->lightShader->GetName() : "<light>" );
		if ( !R_ModernClusteredLighting_FormatDebugString( record.debugName, sizeof( record.debugName ), "%s:%d:%s", R_ModernClusteredLighting_TypeName( record.type ), record.lightDefIndex, shaderName != NULL ? shaderName : "<null>" ) ) {
			R_ModernClusteredLighting_RecordDebugStringTruncation( stats, "cluster light debugName" );
		}
	} else if ( !R_ModernClusteredLighting_FormatDebugString( record.debugName, sizeof( record.debugName ), "%s:%d", R_ModernClusteredLighting_TypeName( record.type ), record.lightDefIndex ) ) {
		R_ModernClusteredLighting_RecordDebugStringTruncation( stats, "cluster light shortName" );
	}
	R_ModernClusteredLighting_FillDescriptor( record, grid, vLight );

	const int lightIndex = rg_clusteredLightingFrame.lightCount++;
	grid.lightCount++;
	stats.lightCount++;
	R_ModernClusteredLighting_CountLightType( record.type, stats );
	if ( record.depthMax > grid.farZ ) {
		grid.farZ = idMath::ClampFloat( grid.nearZ + 1.0f, 32768.0f, record.depthMax );
	}
	return true;
}

static void R_ModernClusteredLighting_BinFrameReferences( rendererClusteredLightingStats_t &stats, bool fillReferences ) {
	for ( int gridIndex = 0; gridIndex < rg_clusteredLightingFrame.gridCount; ++gridIndex ) {
		const modernClusterGridRecord_t &grid = rg_clusteredLightingFrame.grids[gridIndex];
		for ( int i = 0; i < grid.lightCount; ++i ) {
			const int lightIndex = grid.firstLight + i;
			if ( lightIndex < 0 || lightIndex >= rg_clusteredLightingFrame.lightCount ) {
				continue;
			}
			R_ModernClusteredLighting_BinLight( grid, lightIndex, rg_clusteredLightingFrame.lights[lightIndex], stats, fillReferences );
		}
	}
}

static void R_ModernClusteredLighting_BuildClusterLayout( rendererClusteredLightingStats_t &stats ) {
	stats.clusterCount = rg_clusteredLightingFrame.clusterCount;
	stats.activeClusters = 0;
	stats.overflowClusters = 0;
	stats.spillClusters = 0;
	stats.spillReferences = 0;
	stats.unsampledSpillReferences = 0;
	stats.lossyClusters = 0;
	stats.lossyReferences = 0;
	stats.overflowReferences = 0;
	stats.maxLightsPerCluster = 0;
	stats.indexGroupsPerCluster = 0;

	rg_clusteredLightingFrame.clusterRecordCount = rg_clusteredLightingFrame.clusterCount;
	rg_clusteredLightingFrame.flatIndexReferenceCapacity = Max( 0, ( rg_clusteredLightingFrame.indexRecordCapacity - rg_clusteredLightingFrame.clusterRecordCount ) * 4 );

	int uploadedReferences = 0;
	for ( int gridIndex = 0; gridIndex < rg_clusteredLightingFrame.gridCount; ++gridIndex ) {
		modernClusterGridRecord_t &grid = rg_clusteredLightingFrame.grids[gridIndex];
		grid.clusterRecordOffset = grid.clusterOffset;
		grid.indexRecordOffset = rg_clusteredLightingFrame.clusterRecordCount;
		grid.firstLightReference = uploadedReferences;
		grid.uploadedLightReferences = 0;

		int gridMaxLights = 0;
		for ( int localClusterIndex = 0; localClusterIndex < grid.clusterCount; ++localClusterIndex ) {
			const int clusterIndex = grid.clusterOffset + localClusterIndex;
			if ( clusterIndex < 0 || clusterIndex >= rg_clusteredLightingFrame.clusterCount ) {
				continue;
			}
			modernClusterRecord_t &cluster = rg_clusteredLightingFrame.clusters[clusterIndex];
			cluster.firstLightIndex = -1;
			cluster.uploadedLightCount = 0;
			cluster.writeLightCount = 0;
			cluster.overflow = false;
			gridMaxLights = Max( gridMaxLights, cluster.lightCount );
			if ( cluster.lightCount > 0 ) {
				stats.activeClusters++;
			}

			const int remainingReferences = Max( 0, rg_clusteredLightingFrame.flatIndexReferenceCapacity - uploadedReferences );
			const int clusterUploadedReferences = Min( cluster.lightCount, remainingReferences );
			if ( clusterUploadedReferences > 0 ) {
				cluster.firstLightIndex = uploadedReferences;
				cluster.uploadedLightCount = clusterUploadedReferences;
				uploadedReferences += clusterUploadedReferences;
				grid.uploadedLightReferences += clusterUploadedReferences;
			}
			if ( clusterUploadedReferences < cluster.lightCount ) {
				cluster.overflow = true;
				stats.overflow = true;
				stats.overflowClusters++;
				stats.overflowReferences += cluster.lightCount - clusterUploadedReferences;
				stats.lossyClusters++;
				stats.lossyReferences += cluster.lightCount - clusterUploadedReferences;
			}
		}
		grid.maxLightsPerCluster = Max( grid.maxLightsPerCluster, gridMaxLights );
		grid.indexGroupsPerCluster = Max( 1, R_ModernClusteredLighting_CeilDiv( gridMaxLights, 4 ) );
		stats.maxLightsPerCluster = Max( stats.maxLightsPerCluster, grid.maxLightsPerCluster );
		stats.indexGroupsPerCluster = Max( stats.indexGroupsPerCluster, grid.indexGroupsPerCluster );
	}

	rg_clusteredLightingFrame.flatIndexRecordCount = R_ModernClusteredLighting_CeilDiv( uploadedReferences, 4 );
	rg_clusteredLightingFrame.indexRecordCount = rg_clusteredLightingFrame.clusterRecordCount + rg_clusteredLightingFrame.flatIndexRecordCount;
	if ( rg_clusteredLightingFrame.indexRecordCount > rg_clusteredLightingFrame.indexRecordCapacity ) {
		stats.overflow = true;
		stats.overflowReferences += ( rg_clusteredLightingFrame.indexRecordCount - rg_clusteredLightingFrame.indexRecordCapacity ) * 4;
		stats.lossyReferences += ( rg_clusteredLightingFrame.indexRecordCount - rg_clusteredLightingFrame.indexRecordCapacity ) * 4;
	}

	rg_clusteredLightingFrame.clusterLightIndices.SetNum( uploadedReferences, false );
	if ( uploadedReferences > 0 ) {
		memset( rg_clusteredLightingFrame.clusterLightIndices.Ptr(), 0xff, sizeof( GLuint ) * uploadedReferences );
	}

	stats.clusterRecordCount = rg_clusteredLightingFrame.clusterRecordCount;
	stats.flatIndexRecordCount = rg_clusteredLightingFrame.flatIndexRecordCount;
	stats.flatIndexReferenceCapacity = rg_clusteredLightingFrame.flatIndexReferenceCapacity;
	stats.uploadedReferences = uploadedReferences;
	stats.uploadedGridIndexRecords = rg_clusteredLightingFrame.indexRecordCount;
	stats.csrReady = true;
}

static void R_ModernClusteredLighting_FillClusterIndexList( rendererClusteredLightingStats_t &stats ) {
	for ( int i = 0; i < rg_clusteredLightingFrame.clusterCount; ++i ) {
		rg_clusteredLightingFrame.clusters[i].writeLightCount = 0;
	}
	R_ModernClusteredLighting_BinFrameReferences( stats, true );
	int filledReferences = 0;
	for ( int i = 0; i < rg_clusteredLightingFrame.clusterCount; ++i ) {
		const modernClusterRecord_t &cluster = rg_clusteredLightingFrame.clusters[i];
		filledReferences += cluster.writeLightCount;
		if ( cluster.lightCount > 0 ) {
			stats.maxLightsInCluster = Max( stats.maxLightsInCluster, cluster.lightCount );
		}
	}
	stats.uploadedReferences = filledReferences;
	if ( filledReferences != rg_clusteredLightingFrame.clusterLightIndices.Num() ) {
		stats.overflow = true;
		stats.overflowReferences += Max( 0, rg_clusteredLightingFrame.clusterLightIndices.Num() - filledReferences );
		stats.lossyReferences += Max( 0, rg_clusteredLightingFrame.clusterLightIndices.Num() - filledReferences );
		stats.lossyClusters = Max( stats.lossyClusters, 1 );
	}
}

static void R_ModernClusteredLighting_FinalizeClusterStats( rendererClusteredLightingStats_t &stats ) {
	R_ModernClusteredLighting_BuildClusterLayout( stats );
	R_ModernClusteredLighting_FillClusterIndexList( stats );
	stats.lossless = stats.overflowReferences == 0
		&& stats.overflowLights == 0
		&& stats.unsampledSpillReferences == 0
		&& stats.uploadedReferences == stats.lightReferences
		&& rg_clusteredLightingFrame.indexRecordCount <= rg_clusteredLightingFrame.indexRecordCapacity;
	stats.frameValid = true;
}

static void R_ModernClusteredLighting_BuildFrame( const idScenePacketFrame &packetFrame, bool requested, rendererClusteredLightingStats_t &stats ) {
	R_ModernClusteredLighting_ResetFrameData();
	memset( &stats, 0, sizeof( stats ) );
	stats.available = rg_clusteredLightingAvailable;
	stats.initialized = rg_clusteredLightingInitialized;
	stats.requested = requested;
	stats.debugMode = r_rendererClusterDebug.GetInteger();
	stats.debugOverlayReady = rg_clusteredLightingDebugProgram != 0 && rg_clusteredLightingDebugVAO != 0;
	stats.debugTextureReady = rg_clusteredLightingDebugTexture != 0;
	stats.shaderStorageReady = R_ModernClusteredLighting_UseShaderStoragePath();
	stats.computeBinningReady = R_ModernClusteredLighting_UseComputeBinningPath();
	stats.lightCapacity = rg_clusteredLightingFrame.lightCapacity;
	stats.indexRecordCapacity = rg_clusteredLightingFrame.indexRecordCapacity;
	stats.sceneCount = packetFrame.NumScenes();
	R_ModernClusteredLighting_SetStatus( stats, requested ? "unavailable" : "off" );

	if ( !requested ) {
		return;
	}
	if ( !rg_clusteredLightingAvailable ) {
		return;
	}
	if ( !rg_clusteredLightingInitialized ) {
		R_ModernClusteredLighting_SetStatus( stats, "not-initialized" );
		return;
	}

	const int startMsec = Sys_Milliseconds();
	const rendererBenchmarkBudget_t &budget = RendererBenchmarks_CurrentBudget();
	for ( int sceneIndex = 0; sceneIndex < packetFrame.NumScenes(); ++sceneIndex ) {
		const scenePacket_t &scene = packetFrame.Scene( sceneIndex );
		if ( scene.viewDef == NULL ) {
			continue;
		}
		if ( rg_clusteredLightingFrame.gridCount >= MODERN_CLUSTER_MAX_GRIDS ) {
			stats.overflow = true;
			break;
		}
		if ( rg_clusteredLightingFrame.clusterCount >= MODERN_CLUSTER_MAX_CLUSTERS ) {
			stats.overflow = true;
			break;
		}
		const int estimatedLights = R_ModernClusteredLighting_CountViewLights( scene.viewDef );
		rg_clusteredLightingFrame.grids.SetNum( rg_clusteredLightingFrame.gridCount + 1, false );
		modernClusterGridRecord_t &grid = rg_clusteredLightingFrame.grids[rg_clusteredLightingFrame.gridCount];
		const int remainingIndexRecords = Max( 1, rg_clusteredLightingFrame.indexRecordCapacity - rg_clusteredLightingFrame.clusterCount );
		R_ModernClusteredLighting_InitGrid( grid, scene.viewDef, sceneIndex, rg_clusteredLightingFrame.clusterCount, remainingIndexRecords, estimatedLights, budget );
		if ( rg_clusteredLightingFrame.clusterCount + grid.clusterCount > MODERN_CLUSTER_MAX_CLUSTERS ) {
			stats.overflow = true;
			break;
		}
		if ( rg_clusteredLightingFrame.clusterCount + grid.clusterCount > rg_clusteredLightingFrame.indexRecordCapacity ) {
			stats.overflow = true;
			break;
		}
		rg_clusteredLightingFrame.clusters.SetNum( rg_clusteredLightingFrame.clusterCount + grid.clusterCount, false );
		for ( int i = 0; i < grid.clusterCount; ++i ) {
			modernClusterRecord_t &cluster = rg_clusteredLightingFrame.clusters[rg_clusteredLightingFrame.clusterCount + i];
			memset( &cluster, 0, sizeof( cluster ) );
			cluster.firstLightIndex = -1;
		}
		rg_clusteredLightingFrame.gridCount++;
		rg_clusteredLightingFrame.clusterCount += grid.clusterCount;
		stats.gridCount++;
		stats.maxLightsPerCluster = Max( stats.maxLightsPerCluster, grid.maxLightsPerCluster );
		stats.indexGroupsPerCluster = Max( stats.indexGroupsPerCluster, grid.indexGroupsPerCluster );
		if ( stats.gridCount == 1 ) {
			stats.tileCountX = grid.tileCountX;
			stats.tileCountY = grid.tileCountY;
			stats.sliceCountZ = grid.sliceCountZ;
			stats.nearZ = idMath::FtoiFast( grid.nearZ );
			stats.farZ = idMath::FtoiFast( grid.farZ );
		}
	}

	for ( int gridIndex = 0; gridIndex < rg_clusteredLightingFrame.gridCount; ++gridIndex ) {
		modernClusterGridRecord_t &grid = rg_clusteredLightingFrame.grids[gridIndex];
		if ( grid.sceneIndex < 0 || grid.sceneIndex >= packetFrame.NumScenes() ) {
			continue;
		}
		const scenePacket_t &scene = packetFrame.Scene( grid.sceneIndex );
		if ( scene.viewDef == NULL ) {
			continue;
		}
		for ( const viewLight_t *vLight = scene.viewDef->viewLights; vLight != NULL; vLight = vLight->next ) {
			R_ModernClusteredLighting_AddLight( grid, vLight, stats );
		}
		if ( grid.lightCount > 0 ) {
			stats.scenesWithLights++;
		}
		if ( stats.gridCount == 1 ) {
			stats.farZ = idMath::FtoiFast( grid.farZ );
		}
	}
	R_ModernClusteredLighting_BinFrameReferences( stats, false );
	stats.buildMsec = Sys_Milliseconds() - startMsec;
	R_ModernClusteredLighting_FinalizeClusterStats( stats );
	R_ModernClusteredLighting_SetStatus( stats, stats.overflow ? "prepared-csr-overflow" : "prepared-csr" );
}

static GLuint R_ModernClusteredLighting_CompileShaderStage( GLenum stage, const char *source, const char *label ) {
	GLuint shader = glCreateShader( stage );
	if ( shader == 0 ) {
		return 0;
	}
	glShaderSource( shader, 1, &source, NULL );
	glCompileShader( shader );
	GLint compiled = GL_FALSE;
	glGetShaderiv( shader, GL_COMPILE_STATUS, &compiled );
	if ( compiled != GL_TRUE ) {
		char log[2048];
		memset( log, 0, sizeof( log ) );
		if ( glGetShaderInfoLog != NULL ) {
			glGetShaderInfoLog( shader, sizeof( log ) - 1, NULL, log );
		}
		common->Printf( "Modern clustered lighting: %s shader compile failed: %s\n", label ? label : "debug", log );
		glDeleteShader( shader );
		return 0;
	}
	return shader;
}

static GLuint R_ModernClusteredLighting_CompileDebugProgram( void ) {
	static const char *vertexSource =
		"#version 330\n"
		"out vec2 vTexCoord;\n"
		"void main() {\n"
		"	vec2 positions[4] = vec2[]( vec2(-0.96, -0.46), vec2(-0.46, -0.46), vec2(-0.96, -0.96), vec2(-0.46, -0.96) );\n"
		"	vec2 texcoords[4] = vec2[]( vec2(0.0, 1.0), vec2(1.0, 1.0), vec2(0.0, 0.0), vec2(1.0, 0.0) );\n"
		"	vTexCoord = texcoords[gl_VertexID];\n"
		"	gl_Position = vec4( positions[gl_VertexID], 0.0, 1.0 );\n"
		"}\n";
	static const char *fragmentSource =
		"#version 330\n"
		"in vec2 vTexCoord;\n"
		"layout(location = 0) out vec4 out_Color;\n"
		"uniform sampler2D uClusterTexture;\n"
		"uniform vec4 uParams;\n"
		"void main() {\n"
		"	vec4 value = texture( uClusterTexture, vTexCoord );\n"
		"	out_Color = vec4( value.rgb, 1.0 );\n"
		"}\n";

	if ( glCreateShader == NULL || glShaderSource == NULL || glCompileShader == NULL || glCreateProgram == NULL || glAttachShader == NULL || glLinkProgram == NULL || glGetProgramiv == NULL ) {
		return 0;
	}
	GLuint vertexShader = R_ModernClusteredLighting_CompileShaderStage( GL_VERTEX_SHADER, vertexSource, "cluster debug vertex" );
	if ( vertexShader == 0 ) {
		return 0;
	}
	GLuint fragmentShader = R_ModernClusteredLighting_CompileShaderStage( GL_FRAGMENT_SHADER, fragmentSource, "cluster debug fragment" );
	if ( fragmentShader == 0 ) {
		glDeleteShader( vertexShader );
		return 0;
	}
	GLuint program = glCreateProgram();
	if ( program == 0 ) {
		glDeleteShader( vertexShader );
		glDeleteShader( fragmentShader );
		return 0;
	}
	glAttachShader( program, vertexShader );
	glAttachShader( program, fragmentShader );
	glLinkProgram( program );
	glDetachShader( program, vertexShader );
	glDetachShader( program, fragmentShader );
	glDeleteShader( vertexShader );
	glDeleteShader( fragmentShader );
	GLint linked = GL_FALSE;
	glGetProgramiv( program, GL_LINK_STATUS, &linked );
	if ( linked != GL_TRUE ) {
		char log[2048];
		memset( log, 0, sizeof( log ) );
		if ( glGetProgramInfoLog != NULL ) {
			glGetProgramInfoLog( program, sizeof( log ) - 1, NULL, log );
		}
		common->Printf( "Modern clustered lighting: debug overlay program link failed: %s\n", log );
		glDeleteProgram( program );
		return 0;
	}
	rg_clusteredLightingDebugTextureLocation = glGetUniformLocation != NULL ? glGetUniformLocation( program, "uClusterTexture" ) : -1;
	rg_clusteredLightingDebugParamsLocation = glGetUniformLocation != NULL ? glGetUniformLocation( program, "uParams" ) : -1;
	return program;
}

static GLuint R_ModernClusteredLighting_CompileComputeBinningProgram( void ) {
	if ( !rg_clusteredLightingCaps.hasCompute || glCreateShader == NULL || glShaderSource == NULL || glCompileShader == NULL || glCreateProgram == NULL || glAttachShader == NULL || glLinkProgram == NULL || glDispatchCompute == NULL || glMemoryBarrier == NULL ) {
		return 0;
	}

	static const char *computeSource =
		"#version 430\n"
		"layout(local_size_x = 64) in;\n"
		"struct ModernClusterLightRecord {\n"
		"	vec4 positionRadius;\n"
		"	vec4 colorType;\n"
		"	vec4 scissorDepth;\n"
		"	vec4 flags;\n"
		"	vec4 depthRange;\n"
		"	vec4 falloff;\n"
		"	vec4 projectS;\n"
		"	vec4 projectT;\n"
		"	vec4 projectQ;\n"
		"};\n"
		"layout(std140, binding = 3) uniform ModernClusterGridParams {\n"
		"	vec4 grid;\n"
		"	vec4 depth;\n"
		"	vec4 viewport;\n"
		"	vec4 counts;\n"
		"} uClusterGrid;\n"
		"layout(std430, binding = 6) readonly buffer ModernLightRecords {\n"
		"	ModernClusterLightRecord lights[];\n"
		"} uClusterLights;\n"
		"layout(std430, binding = 7) buffer ModernClusterIndexRecordsSSBO {\n"
		"	uvec4 indices[];\n"
		"} uClusterIndices;\n"
		"uniform int u_mode;\n"
		"uniform vec4 u_lightRange;\n"
		"uniform vec4 u_flatRange;\n"
		"uint HeaderBase() { return uint(max(uClusterGrid.viewport.w, 0.0)); }\n"
		"uint FlatBase() { return uint(max(uClusterGrid.viewport.z, 0.0)); }\n"
		"uint ClusterCount() { return uint(max(uClusterGrid.counts.y, 0.0)); }\n"
		"uint HeaderRecord(uint localCluster) { return HeaderBase() + localCluster; }\n"
		"int DepthSliceForZ(float z) {\n"
		"	ivec3 grid = ivec3(max(uClusterGrid.grid.xyz, vec3(1.0)));\n"
		"	float nearZ = max(uClusterGrid.depth.x, 0.01);\n"
		"	float farZ = max(uClusterGrid.depth.y, nearZ + 1.0);\n"
		"	float denom = max(uClusterGrid.depth.w, 0.0001);\n"
		"	float normalized = clamp(log(max(clamp(z, nearZ, farZ), nearZ) / nearZ) / denom, 0.0, 0.999999);\n"
		"	return clamp(int(floor(normalized * float(grid.z))), 0, grid.z - 1);\n"
		"}\n"
		"uint LocalClusterIndex(int x, int y, int z) {\n"
		"	ivec3 grid = ivec3(max(uClusterGrid.grid.xyz, vec3(1.0)));\n"
		"	return uint((z * grid.y + y) * grid.x + x);\n"
		"}\n"
		"void StoreFlatIndex(uint scalarOffset, uint lightIndex) {\n"
		"	uint recordIndex = FlatBase() + (scalarOffset >> 2u);\n"
		"	if (recordIndex >= uint(uClusterIndices.indices.length())) { return; }\n"
		"	uint lane = scalarOffset & 3u;\n"
		"	if (lane == 0u) { atomicExchange(uClusterIndices.indices[recordIndex].x, lightIndex); }\n"
		"	else if (lane == 1u) { atomicExchange(uClusterIndices.indices[recordIndex].y, lightIndex); }\n"
		"	else if (lane == 2u) { atomicExchange(uClusterIndices.indices[recordIndex].z, lightIndex); }\n"
		"	else { atomicExchange(uClusterIndices.indices[recordIndex].w, lightIndex); }\n"
		"}\n"
		"void BinLight(uint lightIndex, bool fill) {\n"
		"	if (lightIndex >= uint(uClusterLights.lights.length())) { return; }\n"
		"	ModernClusterLightRecord light = uClusterLights.lights[lightIndex];\n"
		"	ivec3 grid = ivec3(max(uClusterGrid.grid.xyz, vec3(1.0)));\n"
		"	vec2 viewport = max(uClusterGrid.viewport.xy, vec2(1.0));\n"
		"	int tileMinX = clamp(int(floor(light.scissorDepth.x * float(grid.x) / viewport.x)), 0, grid.x - 1);\n"
		"	int tileMaxX = clamp(int(floor(light.scissorDepth.z * float(grid.x) / viewport.x)), 0, grid.x - 1);\n"
		"	int tileMinY = clamp(int(floor(light.scissorDepth.y * float(grid.y) / viewport.y)), 0, grid.y - 1);\n"
		"	int tileMaxY = clamp(int(floor(light.scissorDepth.w * float(grid.y) / viewport.y)), 0, grid.y - 1);\n"
		"	bool fullDepth = light.falloff.z > 0.5;\n"
		"	int sliceMinZ = fullDepth ? 0 : DepthSliceForZ(light.depthRange.x);\n"
		"	int sliceMaxZ = fullDepth ? grid.z - 1 : DepthSliceForZ(light.depthRange.y);\n"
		"	for (int z = sliceMinZ; z <= sliceMaxZ; ++z) {\n"
		"		for (int y = tileMinY; y <= tileMaxY; ++y) {\n"
		"			for (int x = tileMinX; x <= tileMaxX; ++x) {\n"
		"				uint header = HeaderRecord(LocalClusterIndex(x, y, z));\n"
		"				if (header >= uint(uClusterIndices.indices.length())) { continue; }\n"
		"				if (fill) {\n"
		"					uint slot = atomicAdd(uClusterIndices.indices[header].w, 1u);\n"
		"					uint count = uClusterIndices.indices[header].y;\n"
		"					if (slot < count) { StoreFlatIndex(uClusterIndices.indices[header].x + slot, lightIndex); }\n"
		"				} else {\n"
		"					atomicAdd(uClusterIndices.indices[header].y, 1u);\n"
		"				}\n"
		"			}\n"
		"		}\n"
		"	}\n"
		"}\n"
		"void PrefixClusters() {\n"
		"	uint headerBase = HeaderBase();\n"
		"	uint clusterCount = ClusterCount();\n"
		"	uint running = uint(max(u_flatRange.x, 0.0));\n"
		"	uint limit = running + uint(max(u_flatRange.y, 0.0));\n"
		"	for (uint i = 0u; i < clusterCount; ++i) {\n"
		"		uint header = headerBase + i;\n"
		"		if (header >= uint(uClusterIndices.indices.length())) { return; }\n"
		"		uint count = uClusterIndices.indices[header].y;\n"
		"		uint available = running < limit ? limit - running : 0u;\n"
		"		uint uploaded = min(count, available);\n"
		"		uClusterIndices.indices[header] = uvec4(running, uploaded, count > uploaded ? 1u : 0u, 0u);\n"
		"		running += uploaded;\n"
		"	}\n"
		"}\n"
		"void main() {\n"
		"	if (u_mode == 1) {\n"
		"		if (gl_GlobalInvocationID.x == 0u) { PrefixClusters(); }\n"
		"		return;\n"
		"	}\n"
		"	uint localLight = gl_GlobalInvocationID.x;\n"
		"	uint lightCount = uint(max(u_lightRange.y, 0.0));\n"
		"	if (localLight >= lightCount) { return; }\n"
		"	uint lightIndex = uint(max(u_lightRange.x, 0.0)) + localLight;\n"
		"	BinLight(lightIndex, u_mode == 2);\n"
		"}\n";

	GLuint shader = R_ModernClusteredLighting_CompileShaderStage( GL_COMPUTE_SHADER, computeSource, "cluster compute binning" );
	if ( shader == 0 ) {
		return 0;
	}
	GLuint program = glCreateProgram();
	if ( program == 0 ) {
		glDeleteShader( shader );
		return 0;
	}
	glAttachShader( program, shader );
	glLinkProgram( program );
	glDetachShader( program, shader );
	glDeleteShader( shader );

	GLint linked = GL_FALSE;
	glGetProgramiv( program, GL_LINK_STATUS, &linked );
	if ( linked != GL_TRUE ) {
		char log[2048];
		memset( log, 0, sizeof( log ) );
		if ( glGetProgramInfoLog != NULL ) {
			glGetProgramInfoLog( program, sizeof( log ) - 1, NULL, log );
		}
		common->Printf( "Modern clustered lighting: compute binning program link failed: %s\n", log );
		glDeleteProgram( program );
		return 0;
	}

	rg_clusteredLightingComputeModeLocation = glGetUniformLocation != NULL ? glGetUniformLocation( program, "u_mode" ) : -1;
	rg_clusteredLightingComputeLightRangeLocation = glGetUniformLocation != NULL ? glGetUniformLocation( program, "u_lightRange" ) : -1;
	rg_clusteredLightingComputeFlatRangeLocation = glGetUniformLocation != NULL ? glGetUniformLocation( program, "u_flatRange" ) : -1;
	return program;
}

static bool R_ModernClusteredLighting_CreateBuffer( GLenum target, int bytes, GLuint &buffer, const char *label ) {
	buffer = 0;
	if ( bytes <= 0 || glGenBuffers == NULL || glBindBuffer == NULL || glBufferData == NULL ) {
		return false;
	}
	glGenBuffers( 1, &buffer );
	if ( buffer == 0 ) {
		return false;
	}
	R_GLStateCache().BindBuffer( target, buffer );
	glBufferData( target, bytes, NULL, GL_DYNAMIC_DRAW );
	R_GLStateCache().BindBuffer( target, 0 );
	R_GLDebug_LabelBuffer( buffer, label );
	return true;
}

static bool R_ModernClusteredLighting_EnsureDebugTexture( int width, int height ) {
	if ( width <= 0 || height <= 0 || glGenTextures == NULL || glBindTexture == NULL || glTexImage2D == NULL ) {
		return false;
	}
	if ( rg_clusteredLightingDebugTexture == 0 ) {
		glGenTextures( 1, &rg_clusteredLightingDebugTexture );
		R_GLDebug_LabelTexture( rg_clusteredLightingDebugTexture, "Modern clustered lighting debug texture" );
	}
	R_GLStateCache().BindTexture( 0, GL_TEXTURE_2D, rg_clusteredLightingDebugTexture );
	if ( rg_clusteredLightingDebugTextureWidth != width || rg_clusteredLightingDebugTextureHeight != height ) {
		glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
		rg_clusteredLightingDebugTextureWidth = width;
		rg_clusteredLightingDebugTextureHeight = height;
	}
	R_GLStateCache().BindTexture( 0, GL_TEXTURE_2D, 0 );
	return rg_clusteredLightingDebugTexture != 0;
}

static void R_ModernClusteredLighting_BuildGridParams( const modernClusterGridRecord_t &grid, const rendererClusteredLightingStats_t &stats, modernClusterGridGpuParams_t &params );

static void R_ModernClusteredLighting_DispatchComputeBinning( rendererClusteredLightingStats_t &stats ) {
	if ( !R_ModernClusteredLighting_UseComputeBinningPath() || rg_clusteredLightingFrame.gridCount <= 0 ) {
		return;
	}
	idGLDebugScope computeScope( "Modern clustered lighting compute binning" );
	R_ModernClusteredLighting_BindGpuBuffers( true );
	R_GLStateCache().UseProgram( rg_clusteredLightingComputeProgram );

	for ( int gridIndex = 0; gridIndex < rg_clusteredLightingFrame.gridCount; ++gridIndex ) {
		const modernClusterGridRecord_t &grid = rg_clusteredLightingFrame.grids[gridIndex];
		if ( grid.clusterCount <= 0 ) {
			continue;
		}
		modernClusterGridGpuParams_t params;
		R_ModernClusteredLighting_BuildGridParams( grid, stats, params );
		R_ModernClusteredLighting_UpdateBuffer( GL_UNIFORM_BUFFER, rg_clusteredLightingParamsUBO, 0, sizeof( params ), &params );

		if ( rg_clusteredLightingComputeLightRangeLocation >= 0 && glUniform4f != NULL ) {
			glUniform4f(
				rg_clusteredLightingComputeLightRangeLocation,
				static_cast<float>( grid.firstLight ),
				static_cast<float>( grid.lightCount ),
				0.0f,
				0.0f );
		}
		if ( rg_clusteredLightingComputeFlatRangeLocation >= 0 && glUniform4f != NULL ) {
			glUniform4f(
				rg_clusteredLightingComputeFlatRangeLocation,
				static_cast<float>( grid.firstLightReference ),
				static_cast<float>( grid.uploadedLightReferences ),
				0.0f,
				0.0f );
		}

		if ( grid.lightCount > 0 ) {
			if ( rg_clusteredLightingComputeModeLocation >= 0 && glUniform1i != NULL ) {
				glUniform1i( rg_clusteredLightingComputeModeLocation, 0 );
			}
			glDispatchCompute( static_cast<GLuint>( ( grid.lightCount + MODERN_CLUSTER_COMPUTE_WORKGROUP_SIZE - 1 ) / MODERN_CLUSTER_COMPUTE_WORKGROUP_SIZE ), 1, 1 );
			glMemoryBarrier( GL_SHADER_STORAGE_BARRIER_BIT );
			stats.computeBinningDispatches++;
		}

		if ( rg_clusteredLightingComputeModeLocation >= 0 && glUniform1i != NULL ) {
			glUniform1i( rg_clusteredLightingComputeModeLocation, 1 );
		}
		glDispatchCompute( 1, 1, 1 );
		glMemoryBarrier( GL_SHADER_STORAGE_BARRIER_BIT );
		stats.computeBinningDispatches++;

		if ( grid.lightCount > 0 ) {
			if ( rg_clusteredLightingComputeModeLocation >= 0 && glUniform1i != NULL ) {
				glUniform1i( rg_clusteredLightingComputeModeLocation, 2 );
			}
			glDispatchCompute( static_cast<GLuint>( ( grid.lightCount + MODERN_CLUSTER_COMPUTE_WORKGROUP_SIZE - 1 ) / MODERN_CLUSTER_COMPUTE_WORKGROUP_SIZE ), 1, 1 );
			glMemoryBarrier( GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT );
			stats.computeBinningDispatches++;
		}
	}

	R_GLStateCache().UseProgram( 0 );
	if ( rg_clusteredLightingFrame.gridCount > 0 ) {
		modernClusterGridGpuParams_t params;
		R_ModernClusteredLighting_BuildGridParams( rg_clusteredLightingFrame.grids[0], stats, params );
		R_ModernClusteredLighting_UpdateBuffer( GL_UNIFORM_BUFFER, rg_clusteredLightingParamsUBO, 0, sizeof( params ), &params );
		rg_clusteredLightingBoundGridIndex = 0;
	}
	stats.computeBinningExecuted = stats.computeBinningDispatches > 0;
	if ( stats.computeBinningExecuted ) {
		R_ModernClusteredLighting_SetStatus( stats, stats.lossless ? "uploaded-compute-csr" : "uploaded-compute-csr-lossy" );
	}
}

static bool R_ModernClusteredLighting_UploadBuffers( rendererClusteredLightingStats_t &stats ) {
	rg_clusteredLightingBoundGridIndex = -1;
	stats.paramsUBOBytes = sizeof( modernClusterGridGpuParams_t );
	const bool useShaderStorage = R_ModernClusteredLighting_UseShaderStoragePath();
	const int lightCapacity = useShaderStorage ? MODERN_CLUSTER_MAX_LIGHTS_SSBO : MODERN_CLUSTER_MAX_LIGHTS_UBO;
	const int indexRecordCapacity = useShaderStorage ? MODERN_CLUSTER_MAX_INDEX_RECORDS_SSBO : MODERN_CLUSTER_MAX_INDEX_RECORDS_UBO;
	stats.lightsUBOBytes = sizeof( modernClusterLightGpuRecord_t ) * lightCapacity;
	stats.indicesUBOBytes = sizeof( modernClusterIndexGpuRecord_t ) * indexRecordCapacity;
	stats.shaderStorageReady = useShaderStorage;
	stats.computeBinningReady = R_ModernClusteredLighting_UseComputeBinningPath();
	stats.clusterRecordCount = rg_clusteredLightingFrame.clusterRecordCount;
	stats.flatIndexRecordCount = rg_clusteredLightingFrame.flatIndexRecordCount;
	stats.flatIndexReferenceCapacity = rg_clusteredLightingFrame.flatIndexReferenceCapacity;
	if ( !stats.requested || !stats.frameValid || rg_clusteredLightingParamsUBO == 0 || glBufferSubData == NULL || glBindBufferBase == NULL ) {
		return false;
	}
	if ( useShaderStorage ) {
		if ( rg_clusteredLightingLightsSSBO == 0 || rg_clusteredLightingIndicesSSBO == 0 ) {
			return false;
		}
	} else if ( rg_clusteredLightingLightsUBO == 0 || rg_clusteredLightingIndicesUBO == 0 ) {
		return false;
	}

	const int uploadedLights = Min( rg_clusteredLightingFrame.lightCount, lightCapacity );
	stats.uploadedLights = uploadedLights;
	stats.uploadedClusters = Min( rg_clusteredLightingFrame.clusterCount, MODERN_CLUSTER_MAX_CLUSTERS );
	stats.uploadedGridIndexRecords = Min( rg_clusteredLightingFrame.indexRecordCount, indexRecordCapacity );
	modernClusterGridGpuParams_t params;
	if ( rg_clusteredLightingFrame.gridCount > 0 ) {
		R_ModernClusteredLighting_BuildGridParams( rg_clusteredLightingFrame.grids[0], stats, params );
	} else {
		memset( &params, 0, sizeof( params ) );
	}

	idList<modernClusterLightGpuRecord_t> &lightRecords = rg_clusteredLightingFrame.lightGpuRecords;
	const int lightUploadRecords = useShaderStorage ? Max( uploadedLights, 1 ) : lightCapacity;
	lightRecords.SetNum( lightUploadRecords, false );
	memset( lightRecords.Ptr(), 0, sizeof( modernClusterLightGpuRecord_t ) * lightUploadRecords );
	for ( int i = 0; i < uploadedLights; ++i ) {
		const modernClusterLightRecord_t &src = rg_clusteredLightingFrame.lights[i];
		modernClusterLightGpuRecord_t &dst = lightRecords[i];
		dst.positionRadius[0] = src.cameraOrigin.x;
		dst.positionRadius[1] = src.cameraOrigin.y;
		dst.positionRadius[2] = src.cameraOrigin.z;
		dst.positionRadius[3] = src.radius;
		dst.colorType[0] = src.color.x;
		dst.colorType[1] = src.color.y;
		dst.colorType[2] = src.color.z;
		dst.colorType[3] = static_cast<float>( src.type );
		dst.scissorDepth[0] = static_cast<float>( src.scissor.x1 );
		dst.scissorDepth[1] = static_cast<float>( src.scissor.y1 );
		dst.scissorDepth[2] = static_cast<float>( src.scissor.x2 );
		dst.scissorDepth[3] = static_cast<float>( src.scissor.y2 );
		dst.flags[0] = static_cast<float>( src.flags );
		dst.flags[1] = static_cast<float>( src.lightDefIndex );
		dst.flags[2] = static_cast<float>( src.shadowDescriptorIndex );
		dst.flags[3] = static_cast<float>( src.shadowPolicy );
		dst.depthRange[0] = src.descriptor.depthRange[0];
		dst.depthRange[1] = src.descriptor.depthRange[1];
		dst.depthRange[2] = src.descriptor.depthRange[2];
		dst.depthRange[3] = src.descriptor.depthRange[3];
		dst.falloff[0] = src.descriptor.falloff[0];
		dst.falloff[1] = src.descriptor.falloff[1];
		dst.falloff[2] = src.descriptor.falloff[2];
		dst.falloff[3] = src.descriptor.falloff[3];
		memcpy( dst.projectS, src.descriptor.projectS, sizeof( dst.projectS ) );
		memcpy( dst.projectT, src.descriptor.projectT, sizeof( dst.projectT ) );
		memcpy( dst.projectQ, src.descriptor.projectQ, sizeof( dst.projectQ ) );
	}

	idList<modernClusterIndexGpuRecord_t> &indexRecords = rg_clusteredLightingFrame.indexGpuRecords;
	const int indexUploadRecords = useShaderStorage ? Max( stats.uploadedGridIndexRecords, 1 ) : indexRecordCapacity;
	indexRecords.SetNum( indexUploadRecords, false );
	for ( int i = 0; i < indexUploadRecords; ++i ) {
		indexRecords[i].indices[0] = 0xffffffffu;
		indexRecords[i].indices[1] = 0xffffffffu;
		indexRecords[i].indices[2] = 0xffffffffu;
		indexRecords[i].indices[3] = 0xffffffffu;
	}
	stats.uploadedReferences = 0;
	const bool seedComputeBinning = R_ModernClusteredLighting_UseComputeBinningPath();
	for ( int gridIndex = 0; gridIndex < rg_clusteredLightingFrame.gridCount; ++gridIndex ) {
		const modernClusterGridRecord_t &grid = rg_clusteredLightingFrame.grids[gridIndex];
		for ( int localClusterIndex = 0; localClusterIndex < grid.clusterCount; ++localClusterIndex ) {
			const int globalClusterIndex = grid.clusterOffset + localClusterIndex;
			if ( globalClusterIndex < 0 || globalClusterIndex >= rg_clusteredLightingFrame.clusterCount ) {
				continue;
			}
			const modernClusterRecord_t &cluster = rg_clusteredLightingFrame.clusters[globalClusterIndex];
			const int headerRecord = grid.clusterRecordOffset + localClusterIndex;
			if ( headerRecord < 0 || headerRecord >= indexUploadRecords ) {
				continue;
			}
			if ( seedComputeBinning ) {
				indexRecords[headerRecord].indices[0] = 0u;
				indexRecords[headerRecord].indices[1] = 0u;
				indexRecords[headerRecord].indices[2] = 0u;
				indexRecords[headerRecord].indices[3] = 0u;
			} else {
				indexRecords[headerRecord].indices[0] = static_cast<GLuint>( Max( 0, cluster.firstLightIndex ) );
				indexRecords[headerRecord].indices[1] = static_cast<GLuint>( Max( 0, cluster.uploadedLightCount ) );
				indexRecords[headerRecord].indices[2] = cluster.overflow ? 1u : 0u;
				indexRecords[headerRecord].indices[3] = 0u;
			}
		}
	}
	if ( seedComputeBinning ) {
		stats.uploadedReferences = rg_clusteredLightingFrame.clusterLightIndices.Num();
	} else {
		for ( int i = 0; i < rg_clusteredLightingFrame.clusterLightIndices.Num(); ++i ) {
			const int recordIndex = rg_clusteredLightingFrame.clusterRecordCount + i / 4;
			if ( recordIndex < 0 || recordIndex >= indexUploadRecords ) {
				continue;
			}
			const GLuint lightIndex = rg_clusteredLightingFrame.clusterLightIndices[i];
			if ( lightIndex < static_cast<GLuint>( uploadedLights ) ) {
				indexRecords[recordIndex].indices[i & 3] = lightIndex;
				stats.uploadedReferences++;
			}
		}
	}
	stats.lossless = stats.lossless
		&& stats.uploadedLights == stats.lightCount
		&& stats.uploadedReferences == stats.lightReferences
		&& stats.uploadedGridIndexRecords == rg_clusteredLightingFrame.indexRecordCount;

	R_ModernClusteredLighting_UpdateBuffer( GL_UNIFORM_BUFFER, rg_clusteredLightingParamsUBO, 0, sizeof( params ), &params );
	rg_clusteredLightingBoundGridIndex = rg_clusteredLightingFrame.gridCount > 0 ? 0 : -1;
	if ( useShaderStorage ) {
		R_ModernClusteredLighting_UpdateBuffer( GL_SHADER_STORAGE_BUFFER, rg_clusteredLightingLightsSSBO, 0, sizeof( modernClusterLightGpuRecord_t ) * lightUploadRecords, lightRecords.Ptr() );
		R_ModernClusteredLighting_UpdateBuffer( GL_SHADER_STORAGE_BUFFER, rg_clusteredLightingIndicesSSBO, 0, sizeof( modernClusterIndexGpuRecord_t ) * indexUploadRecords, indexRecords.Ptr() );
	} else {
		R_ModernClusteredLighting_UpdateBuffer( GL_UNIFORM_BUFFER, rg_clusteredLightingLightsUBO, 0, sizeof( modernClusterLightGpuRecord_t ) * lightUploadRecords, lightRecords.Ptr() );
		R_ModernClusteredLighting_UpdateBuffer( GL_UNIFORM_BUFFER, rg_clusteredLightingIndicesUBO, 0, sizeof( modernClusterIndexGpuRecord_t ) * indexUploadRecords, indexRecords.Ptr() );
	}
	R_ModernClusteredLighting_BindGpuBuffers( useShaderStorage );
	if ( seedComputeBinning ) {
		R_ModernClusteredLighting_DispatchComputeBinning( stats );
	} else {
		R_ModernClusteredLighting_SetStatus( stats, stats.lossless ? "uploaded-csr" : "uploaded-csr-lossy" );
	}

	stats.bufferUploads += 3;
	stats.buffersReady = true;
	stats.uboFallbackReady = !useShaderStorage;
	return true;
}

static void R_ModernClusteredLighting_ColorForCluster( const modernClusterRecord_t &cluster, int debugMode, modernClusterDebugPixel_t &pixel ) {
	const int count = cluster.lightCount;
	pixel.r = pixel.g = pixel.b = 0;
	pixel.a = 255;
	if ( debugMode == RENDERER_CLUSTER_DEBUG_OVERFLOW ) {
		pixel.r = cluster.overflow ? 255 : 18;
		pixel.g = cluster.overflow ? 32 : 18;
		pixel.b = cluster.overflow ? 32 : 18;
		return;
	}
	if ( debugMode == RENDERER_CLUSTER_DEBUG_OCCUPANCY ) {
		const unsigned char v = static_cast<unsigned char>( idMath::ClampInt( 0, 255, count * 64 ) );
		pixel.r = count > 0 ? 16 : 0;
		pixel.g = v;
		pixel.b = count > 0 ? 80 : 0;
		return;
	}
	const int scaled = idMath::ClampInt( 0, 255, count * 64 );
	pixel.r = static_cast<unsigned char>( scaled );
	pixel.g = static_cast<unsigned char>( count > 1 ? Min( 255, 64 + scaled / 2 ) : scaled / 2 );
	pixel.b = static_cast<unsigned char>( count == 0 ? 32 : Max( 0, 192 - scaled ) );
}

static bool R_ModernClusteredLighting_UpdateDebugTexture( rendererClusteredLightingStats_t &stats ) {
	if ( r_rendererClusterDebug.GetInteger() <= 0 || rg_clusteredLightingFrame.gridCount <= 0 ) {
		return false;
	}
	const modernClusterGridRecord_t &grid = rg_clusteredLightingFrame.grids[0];
	const int width = grid.tileCountX * grid.sliceCountZ;
	const int height = grid.tileCountY;
	if ( width <= 0 || height <= 0 || !R_ModernClusteredLighting_EnsureDebugTexture( width, height ) ) {
		return false;
	}
	idList<modernClusterDebugPixel_t> pixels;
	pixels.SetNum( width * height, false );
	memset( pixels.Ptr(), 0, sizeof( modernClusterDebugPixel_t ) * width * height );
	const int debugMode = idMath::ClampInt( RENDERER_CLUSTER_DEBUG_OCCUPANCY, RENDERER_CLUSTER_DEBUG_OVERFLOW, r_rendererClusterDebug.GetInteger() );
	for ( int z = 0; z < grid.sliceCountZ; ++z ) {
		for ( int y = 0; y < grid.tileCountY; ++y ) {
			for ( int x = 0; x < grid.tileCountX; ++x ) {
				const int clusterIndex = R_ModernClusteredLighting_ClusterIndex( grid, x, y, z );
				const int pixelIndex = y * width + z * grid.tileCountX + x;
				R_ModernClusteredLighting_ColorForCluster( rg_clusteredLightingFrame.clusters[clusterIndex], debugMode, pixels[pixelIndex] );
			}
		}
	}
	R_GLStateCache().BindTexture( 0, GL_TEXTURE_2D, rg_clusteredLightingDebugTexture );
	glTexSubImage2D( GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.Ptr() );
	R_GLStateCache().BindTexture( 0, GL_TEXTURE_2D, 0 );
	stats.debugTextureReady = true;
	return true;
}

static void R_ModernClusteredLighting_BuildGridParams( const modernClusterGridRecord_t &grid, const rendererClusteredLightingStats_t &stats, modernClusterGridGpuParams_t &params ) {
	memset( &params, 0, sizeof( params ) );
	params.grid[0] = static_cast<float>( grid.tileCountX );
	params.grid[1] = static_cast<float>( grid.tileCountY );
	params.grid[2] = static_cast<float>( grid.sliceCountZ );
	params.grid[3] = static_cast<float>( Max( 1, grid.maxLightsPerCluster ) );
	params.depth[0] = grid.nearZ;
	params.depth[1] = grid.farZ;
	params.depth[2] = grid.farZ > grid.nearZ ? 1.0f / ( grid.farZ - grid.nearZ ) : 1.0f;
	params.depth[3] = Max( 0.0001f, idMath::Log( grid.farZ / Max( 0.01f, grid.nearZ ) ) );
	params.viewport[0] = static_cast<float>( grid.width );
	params.viewport[1] = static_cast<float>( grid.height );
	params.viewport[2] = static_cast<float>( grid.indexRecordOffset );
	params.viewport[3] = static_cast<float>( grid.clusterRecordOffset );
	params.counts[0] = static_cast<float>( stats.uploadedLights );
	params.counts[1] = static_cast<float>( grid.clusterCount );
	params.counts[2] = static_cast<float>( stats.activeClusters );
	params.counts[3] = static_cast<float>( stats.lossyReferences );
}

void R_ModernClusteredLighting_Init( const renderBackendCaps_t &caps, const renderFeatureSet_t &features ) {
	rg_clusteredLightingCaps = caps;
	rg_clusteredLightingFeatures = features;
	rg_clusteredLightingAvailable = features.modernBaseline && caps.hasUBO && caps.hasVAO && glBindBufferBase != NULL;
	rg_clusteredLightingShaderStorageAvailable = rg_clusteredLightingAvailable && features.gpuDriven && caps.hasSSBO;
	if ( !rg_clusteredLightingAvailable ) {
		memset( &rg_clusteredLightingStats, 0, sizeof( rg_clusteredLightingStats ) );
		R_ModernClusteredLighting_SetStatus( rg_clusteredLightingStats, "unavailable" );
		return;
	}

	const int paramsBytes = sizeof( modernClusterGridGpuParams_t );
	const int lightBytes = sizeof( modernClusterLightGpuRecord_t ) * MODERN_CLUSTER_MAX_LIGHTS_UBO;
	const int indexBytes = sizeof( modernClusterIndexGpuRecord_t ) * MODERN_CLUSTER_MAX_INDEX_RECORDS_UBO;
	if ( !R_ModernClusteredLighting_CreateBuffer( GL_UNIFORM_BUFFER, paramsBytes, rg_clusteredLightingParamsUBO, "Modern clustered lighting grid params UBO" ) ||
		!R_ModernClusteredLighting_CreateBuffer( GL_UNIFORM_BUFFER, lightBytes, rg_clusteredLightingLightsUBO, "Modern clustered lighting light records UBO" ) ||
		!R_ModernClusteredLighting_CreateBuffer( GL_UNIFORM_BUFFER, indexBytes, rg_clusteredLightingIndicesUBO, "Modern clustered lighting indices UBO" ) ) {
		R_ModernClusteredLighting_Shutdown();
		R_ModernClusteredLighting_SetStatus( rg_clusteredLightingStats, "buffer-init-failed" );
		return;
	}
	if ( rg_clusteredLightingShaderStorageAvailable ) {
		const int ssboLightBytes = sizeof( modernClusterLightGpuRecord_t ) * MODERN_CLUSTER_MAX_LIGHTS_SSBO;
		const int ssboIndexBytes = sizeof( modernClusterIndexGpuRecord_t ) * MODERN_CLUSTER_MAX_INDEX_RECORDS_SSBO;
		if ( !R_ModernClusteredLighting_CreateBuffer( GL_SHADER_STORAGE_BUFFER, ssboLightBytes, rg_clusteredLightingLightsSSBO, "Modern clustered lighting light records SSBO" ) ||
			!R_ModernClusteredLighting_CreateBuffer( GL_SHADER_STORAGE_BUFFER, ssboIndexBytes, rg_clusteredLightingIndicesSSBO, "Modern clustered lighting indices SSBO" ) ) {
			R_ModernClusteredLighting_Shutdown();
			R_ModernClusteredLighting_SetStatus( rg_clusteredLightingStats, "ssbo-init-failed" );
			return;
		}
		rg_clusteredLightingComputeProgram = R_ModernClusteredLighting_CompileComputeBinningProgram();
		if ( rg_clusteredLightingComputeProgram != 0 ) {
			R_GLDebug_LabelProgram( rg_clusteredLightingComputeProgram, "Modern clustered lighting compute binning" );
		}
	}
	if ( glGenVertexArrays != NULL ) {
		glGenVertexArrays( 1, &rg_clusteredLightingDebugVAO );
		if ( rg_clusteredLightingDebugVAO != 0 ) {
			R_GLDebug_LabelVertexArray( rg_clusteredLightingDebugVAO, "Modern clustered lighting debug VAO" );
		}
	}
	rg_clusteredLightingDebugProgram = R_ModernClusteredLighting_CompileDebugProgram();
	if ( rg_clusteredLightingDebugProgram != 0 ) {
		R_GLDebug_LabelProgram( rg_clusteredLightingDebugProgram, "Modern clustered lighting debug overlay" );
	}
	rg_clusteredLightingInitialized = true;
	memset( &rg_clusteredLightingStats, 0, sizeof( rg_clusteredLightingStats ) );
	rg_clusteredLightingStats.available = true;
	rg_clusteredLightingStats.initialized = true;
	rg_clusteredLightingStats.buffersReady = true;
	rg_clusteredLightingStats.shaderStorageReady = R_ModernClusteredLighting_UseShaderStoragePath();
	rg_clusteredLightingStats.computeBinningReady = R_ModernClusteredLighting_UseComputeBinningPath();
	rg_clusteredLightingStats.uboFallbackReady = !rg_clusteredLightingStats.shaderStorageReady;
	rg_clusteredLightingStats.lightCapacity = R_ModernClusteredLighting_LightCapacity();
	rg_clusteredLightingStats.indexRecordCapacity = R_ModernClusteredLighting_IndexRecordCapacity();
	rg_clusteredLightingStats.debugOverlayReady = rg_clusteredLightingDebugProgram != 0 && rg_clusteredLightingDebugVAO != 0;
	rg_clusteredLightingStats.paramsUBOBytes = paramsBytes;
	rg_clusteredLightingStats.lightsUBOBytes = sizeof( modernClusterLightGpuRecord_t ) * rg_clusteredLightingStats.lightCapacity;
	rg_clusteredLightingStats.indicesUBOBytes = sizeof( modernClusterIndexGpuRecord_t ) * rg_clusteredLightingStats.indexRecordCapacity;
	R_ModernClusteredLighting_SetStatus( rg_clusteredLightingStats, "initialized" );
}

void R_ModernClusteredLighting_Shutdown( void ) {
	GLuint buffers[5];
	int numBuffers = 0;
	if ( rg_clusteredLightingParamsUBO != 0 ) {
		buffers[numBuffers++] = rg_clusteredLightingParamsUBO;
	}
	if ( rg_clusteredLightingLightsUBO != 0 ) {
		buffers[numBuffers++] = rg_clusteredLightingLightsUBO;
	}
	if ( rg_clusteredLightingIndicesUBO != 0 ) {
		buffers[numBuffers++] = rg_clusteredLightingIndicesUBO;
	}
	if ( rg_clusteredLightingLightsSSBO != 0 ) {
		buffers[numBuffers++] = rg_clusteredLightingLightsSSBO;
	}
	if ( rg_clusteredLightingIndicesSSBO != 0 ) {
		buffers[numBuffers++] = rg_clusteredLightingIndicesSSBO;
	}
	if ( numBuffers > 0 && glDeleteBuffers != NULL ) {
		glDeleteBuffers( numBuffers, buffers );
	}
	if ( rg_clusteredLightingDebugTexture != 0 && glDeleteTextures != NULL ) {
		glDeleteTextures( 1, &rg_clusteredLightingDebugTexture );
	}
	if ( rg_clusteredLightingDebugProgram != 0 && glDeleteProgram != NULL ) {
		glDeleteProgram( rg_clusteredLightingDebugProgram );
	}
	if ( rg_clusteredLightingComputeProgram != 0 && glDeleteProgram != NULL ) {
		glDeleteProgram( rg_clusteredLightingComputeProgram );
	}
	if ( rg_clusteredLightingDebugVAO != 0 && glDeleteVertexArrays != NULL ) {
		glDeleteVertexArrays( 1, &rg_clusteredLightingDebugVAO );
	}
	rg_clusteredLightingParamsUBO = 0;
	rg_clusteredLightingLightsUBO = 0;
	rg_clusteredLightingIndicesUBO = 0;
	rg_clusteredLightingLightsSSBO = 0;
	rg_clusteredLightingIndicesSSBO = 0;
	rg_clusteredLightingComputeProgram = 0;
	rg_clusteredLightingDebugTexture = 0;
	rg_clusteredLightingDebugProgram = 0;
	rg_clusteredLightingDebugVAO = 0;
	rg_clusteredLightingComputeModeLocation = -1;
	rg_clusteredLightingComputeLightRangeLocation = -1;
	rg_clusteredLightingComputeFlatRangeLocation = -1;
	rg_clusteredLightingDebugTextureWidth = 0;
	rg_clusteredLightingDebugTextureHeight = 0;
	rg_clusteredLightingInitialized = false;
	rg_clusteredLightingAvailable = false;
	rg_clusteredLightingShaderStorageAvailable = false;
	R_ModernClusteredLighting_ResetFrameData();
	memset( &rg_clusteredLightingStats, 0, sizeof( rg_clusteredLightingStats ) );
	R_ModernClusteredLighting_SetStatus( rg_clusteredLightingStats, "off" );
}

void R_ModernClusteredLighting_PrepareFrame( const idScenePacketFrame &packetFrame, bool requested ) {
	R_ModernClusteredLighting_BuildFrame( packetFrame, requested || r_rendererClusterDebug.GetInteger() > 0, rg_clusteredLightingStats );
	R_ModernClusteredLighting_UploadBuffers( rg_clusteredLightingStats );
	R_ModernClusteredLighting_UpdateDebugTexture( rg_clusteredLightingStats );
	R_RendererMetrics_RecordClusteredLighting( rg_clusteredLightingStats );
	if ( r_rendererMetrics.GetInteger() >= 2 && rg_clusteredLightingStats.requested ) {
		common->Printf(
			"clusteredLighting status=%s requested=%d valid=%d grids=%d scenes=%d lights=%d point=%d projected=%d fog=%d ambient=%d blend=%d special=%d shadow(mapped=%d fallback=%d skipped=%d descriptors=%d receiverBlocked=%d) clusters=%d active=%d refs=%d uploaded(l=%d c=%d r=%d idx=%d) csr=%d records(h=%d flat=%d cap=%d) compute=%d/%d dispatch=%d spill=%d/%d unsampled=%d lossy=%d/%d lossless=%d overflow=%d/%d overflowRefs=%d maxCluster=%d/%d groups=%d grid=%dx%dx%d z=%d..%d ubo=%d ssbo=%d buffers=%d caps(l=%d idx=%d) bytes(params=%d lights=%d indices=%d) switches=%d bindFail=%d debug=%d/%d debugTrunc=%d source='%s' build=%dms uploads=%d\n",
			rg_clusteredLightingStats.status,
			rg_clusteredLightingStats.requested ? 1 : 0,
			rg_clusteredLightingStats.frameValid ? 1 : 0,
			rg_clusteredLightingStats.gridCount,
			rg_clusteredLightingStats.sceneCount,
			rg_clusteredLightingStats.lightCount,
			rg_clusteredLightingStats.pointLights,
			rg_clusteredLightingStats.projectedLights,
			rg_clusteredLightingStats.fogLights,
			rg_clusteredLightingStats.ambientLights,
			rg_clusteredLightingStats.blendLights,
			rg_clusteredLightingStats.specialLights,
			rg_clusteredLightingStats.shadowMappedLights,
			rg_clusteredLightingStats.shadowFallbackLights,
			rg_clusteredLightingStats.shadowSkippedLights,
			rg_clusteredLightingStats.shadowDescriptorCount,
			rg_clusteredLightingStats.shadowReceiverBlockedLights,
			rg_clusteredLightingStats.clusterCount,
			rg_clusteredLightingStats.activeClusters,
			rg_clusteredLightingStats.lightReferences,
			rg_clusteredLightingStats.uploadedLights,
			rg_clusteredLightingStats.uploadedClusters,
			rg_clusteredLightingStats.uploadedReferences,
			rg_clusteredLightingStats.uploadedGridIndexRecords,
			rg_clusteredLightingStats.csrReady ? 1 : 0,
			rg_clusteredLightingStats.clusterRecordCount,
			rg_clusteredLightingStats.flatIndexRecordCount,
			rg_clusteredLightingStats.flatIndexReferenceCapacity,
			rg_clusteredLightingStats.computeBinningReady ? 1 : 0,
			rg_clusteredLightingStats.computeBinningExecuted ? 1 : 0,
			rg_clusteredLightingStats.computeBinningDispatches,
			rg_clusteredLightingStats.spillClusters,
			rg_clusteredLightingStats.spillReferences,
			rg_clusteredLightingStats.unsampledSpillReferences,
			rg_clusteredLightingStats.lossyClusters,
			rg_clusteredLightingStats.lossyReferences,
			rg_clusteredLightingStats.lossless ? 1 : 0,
			rg_clusteredLightingStats.overflow ? 1 : 0,
			rg_clusteredLightingStats.overflowClusters,
			rg_clusteredLightingStats.overflowReferences,
			rg_clusteredLightingStats.maxLightsInCluster,
			rg_clusteredLightingStats.maxLightsPerCluster,
			rg_clusteredLightingStats.indexGroupsPerCluster,
			rg_clusteredLightingStats.tileCountX,
			rg_clusteredLightingStats.tileCountY,
			rg_clusteredLightingStats.sliceCountZ,
			rg_clusteredLightingStats.nearZ,
			rg_clusteredLightingStats.farZ,
			rg_clusteredLightingStats.uboFallbackReady ? 1 : 0,
			rg_clusteredLightingStats.shaderStorageReady ? 1 : 0,
			rg_clusteredLightingStats.buffersReady ? 1 : 0,
			rg_clusteredLightingStats.lightCapacity,
			rg_clusteredLightingStats.indexRecordCapacity,
			rg_clusteredLightingStats.paramsUBOBytes,
			rg_clusteredLightingStats.lightsUBOBytes,
			rg_clusteredLightingStats.indicesUBOBytes,
			rg_clusteredLightingStats.gridSwitches,
			rg_clusteredLightingStats.gridBindFailures,
			rg_clusteredLightingStats.debugOverlayReady ? 1 : 0,
			rg_clusteredLightingStats.debugMode,
			rg_clusteredLightingStats.debugStringTruncations,
			rg_clusteredLightingStats.debugStringTruncationSource,
			rg_clusteredLightingStats.buildMsec,
			rg_clusteredLightingStats.bufferUploads );
	}
}

void R_ModernClusteredLighting_DrawDebugOverlay( void ) {
	const int debugMode = r_rendererClusterDebug.GetInteger();
	if ( debugMode <= 0 || rg_clusteredLightingDebugProgram == 0 || rg_clusteredLightingDebugTexture == 0 || rg_clusteredLightingDebugVAO == 0 || !rg_clusteredLightingStats.debugTextureReady ) {
		return;
	}
	idGLDebugScope overlayScope( "Modern clustered lighting debug overlay" );
	R_GLStateCache_InvalidateAll( "modern clustered lighting debug overlay" );
	R_GLStateCache().BindFramebuffer( GL_FRAMEBUFFER, 0 );
	R_GLStateCache().BindVertexArray( rg_clusteredLightingDebugVAO );
	R_GLStateCache().SetViewport( 0, 0, Max( 1, glConfig.vidWidth ), Max( 1, glConfig.vidHeight ) );
	R_GLStateCache().SetScissor( 0, 0, Max( 1, glConfig.vidWidth ), Max( 1, glConfig.vidHeight ) );
	R_GLStateCache().SetScissorTestEnabled( false );
	R_GLStateCache().SetDepthTestEnabled( false );
	R_GLStateCache().SetDepthMask( GL_FALSE );
	R_GLStateCache().SetStencilTestEnabled( false );
	R_GLStateCache().SetBlendEnabled( false );
	R_GLStateCache().SetCullFaceEnabled( false );
	R_GLStateCache().SetColorMask( GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE );
	R_GLStateCache().UseProgram( rg_clusteredLightingDebugProgram );
	R_GLStateCache().ActiveTextureUnit( 0 );
	R_GLStateCache().BindTexture( 0, GL_TEXTURE_2D, rg_clusteredLightingDebugTexture );
	if ( rg_clusteredLightingDebugTextureLocation >= 0 ) {
		glUniform1i( rg_clusteredLightingDebugTextureLocation, 0 );
	}
	if ( rg_clusteredLightingDebugParamsLocation >= 0 ) {
		glUniform4f( rg_clusteredLightingDebugParamsLocation, static_cast<float>( debugMode ), static_cast<float>( rg_clusteredLightingStats.tileCountX ), static_cast<float>( rg_clusteredLightingStats.tileCountY ), static_cast<float>( rg_clusteredLightingStats.sliceCountZ ) );
	}
	glDrawArrays( GL_TRIANGLE_STRIP, 0, 4 );
	rg_clusteredLightingStats.debugOverlayDraws++;
	R_RendererMetrics_RecordClusteredLighting( rg_clusteredLightingStats );
	R_GLStateCache().BindTexture( 0, GL_TEXTURE_2D, 0 );
	R_GLStateCache().UseProgram( 0 );
	R_GLStateCache().BindVertexArray( 0 );
	R_GLStateCache().SetDepthMask( GL_TRUE );
	GL_ClearStateDelta();
}

void R_ModernClusteredLighting_PrintGfxInfo( void ) {
	common->Printf(
		"Modern clustered lighting: %s, requested=%d, cvarDebug=%d, grids=%d scenes=%d lights=%d(point=%d projected=%d fog=%d ambient=%d blend=%d special=%d shadowMapped=%d shadowFallback=%d shadowSkipped=%d shadowDescriptors=%d receiverBlocked=%d) clusters=%d active=%d refs=%d uploaded(l=%d c=%d r=%d idx=%d) csr=%d records(h=%d flat=%d cap=%d) compute=%d/%d dispatch=%d spill=%d/%d unsampled=%d lossy=%d/%d lossless=%d overflow=%d/%d overflowRefs=%d maxCluster=%d/%d groups=%d grid=%dx%dx%d z=%d..%d ubo=%d ssbo=%d buffers=%d caps(l=%d idx=%d) bytes(params=%d lights=%d indices=%d) switches=%d bindFail=%d overlay=%d/%d texture=%d debugTrunc=%d source='%s' build=%dms uploads=%d\n",
		rg_clusteredLightingStats.available ? "available" : "unavailable",
		rg_clusteredLightingStats.requested ? 1 : 0,
		r_rendererClusterDebug.GetInteger(),
		rg_clusteredLightingStats.gridCount,
		rg_clusteredLightingStats.sceneCount,
		rg_clusteredLightingStats.lightCount,
		rg_clusteredLightingStats.pointLights,
		rg_clusteredLightingStats.projectedLights,
		rg_clusteredLightingStats.fogLights,
		rg_clusteredLightingStats.ambientLights,
		rg_clusteredLightingStats.blendLights,
		rg_clusteredLightingStats.specialLights,
		rg_clusteredLightingStats.shadowMappedLights,
		rg_clusteredLightingStats.shadowFallbackLights,
		rg_clusteredLightingStats.shadowSkippedLights,
		rg_clusteredLightingStats.shadowDescriptorCount,
		rg_clusteredLightingStats.shadowReceiverBlockedLights,
		rg_clusteredLightingStats.clusterCount,
		rg_clusteredLightingStats.activeClusters,
		rg_clusteredLightingStats.lightReferences,
		rg_clusteredLightingStats.uploadedLights,
		rg_clusteredLightingStats.uploadedClusters,
		rg_clusteredLightingStats.uploadedReferences,
		rg_clusteredLightingStats.uploadedGridIndexRecords,
		rg_clusteredLightingStats.csrReady ? 1 : 0,
		rg_clusteredLightingStats.clusterRecordCount,
		rg_clusteredLightingStats.flatIndexRecordCount,
		rg_clusteredLightingStats.flatIndexReferenceCapacity,
		rg_clusteredLightingStats.computeBinningReady ? 1 : 0,
		rg_clusteredLightingStats.computeBinningExecuted ? 1 : 0,
		rg_clusteredLightingStats.computeBinningDispatches,
		rg_clusteredLightingStats.spillClusters,
		rg_clusteredLightingStats.spillReferences,
		rg_clusteredLightingStats.unsampledSpillReferences,
		rg_clusteredLightingStats.lossyClusters,
		rg_clusteredLightingStats.lossyReferences,
		rg_clusteredLightingStats.lossless ? 1 : 0,
		rg_clusteredLightingStats.overflow ? 1 : 0,
		rg_clusteredLightingStats.overflowClusters,
		rg_clusteredLightingStats.overflowReferences,
		rg_clusteredLightingStats.maxLightsInCluster,
		rg_clusteredLightingStats.maxLightsPerCluster,
		rg_clusteredLightingStats.indexGroupsPerCluster,
		rg_clusteredLightingStats.tileCountX,
		rg_clusteredLightingStats.tileCountY,
		rg_clusteredLightingStats.sliceCountZ,
		rg_clusteredLightingStats.nearZ,
		rg_clusteredLightingStats.farZ,
		rg_clusteredLightingStats.uboFallbackReady ? 1 : 0,
		rg_clusteredLightingStats.shaderStorageReady ? 1 : 0,
		rg_clusteredLightingStats.buffersReady ? 1 : 0,
		rg_clusteredLightingStats.lightCapacity,
		rg_clusteredLightingStats.indexRecordCapacity,
		rg_clusteredLightingStats.paramsUBOBytes,
		rg_clusteredLightingStats.lightsUBOBytes,
		rg_clusteredLightingStats.indicesUBOBytes,
		rg_clusteredLightingStats.gridSwitches,
		rg_clusteredLightingStats.gridBindFailures,
		rg_clusteredLightingStats.debugOverlayReady ? 1 : 0,
		rg_clusteredLightingStats.debugOverlayDraws,
		rg_clusteredLightingStats.debugTextureReady ? 1 : 0,
		rg_clusteredLightingStats.debugStringTruncations,
		rg_clusteredLightingStats.debugStringTruncationSource,
		rg_clusteredLightingStats.buildMsec,
		rg_clusteredLightingStats.bufferUploads );
}

const rendererClusteredLightingStats_t &R_ModernClusteredLighting_Stats( void ) {
	return rg_clusteredLightingStats;
}

static int R_ModernClusteredLighting_GridIndexForView( const viewDef_t *viewDef ) {
	if ( rg_clusteredLightingFrame.gridCount <= 0 ) {
		return -1;
	}
	if ( viewDef == NULL ) {
		return 0;
	}
	for ( int i = 0; i < rg_clusteredLightingFrame.gridCount; ++i ) {
		if ( rg_clusteredLightingFrame.grids[i].viewDef == viewDef ) {
			return i;
		}
	}
	return rg_clusteredLightingFrame.gridCount == 1 ? 0 : -1;
}

bool R_ModernClusteredLighting_FrameLossless( void ) {
	return rg_clusteredLightingStats.frameValid
		&& rg_clusteredLightingStats.buffersReady
		&& rg_clusteredLightingStats.csrReady
		&& rg_clusteredLightingStats.lossless
		&& !rg_clusteredLightingStats.overflow
		&& rg_clusteredLightingStats.overflowLights == 0
		&& rg_clusteredLightingStats.overflowReferences == 0
		&& rg_clusteredLightingStats.unsampledSpillReferences == 0
		&& rg_clusteredLightingStats.uploadedLights == rg_clusteredLightingStats.lightCount
		&& rg_clusteredLightingStats.uploadedReferences == rg_clusteredLightingStats.lightReferences;
}

bool R_ModernClusteredLighting_BindGridForView( const viewDef_t *viewDef ) {
	if ( !rg_clusteredLightingStats.frameValid || !rg_clusteredLightingStats.buffersReady || rg_clusteredLightingParamsUBO == 0 || glBufferSubData == NULL || glBindBufferBase == NULL ) {
		rg_clusteredLightingStats.gridBindFailures++;
		return false;
	}
	const int gridIndex = R_ModernClusteredLighting_GridIndexForView( viewDef );
	if ( gridIndex < 0 ) {
		rg_clusteredLightingStats.gridBindFailures++;
		return false;
	}

	if ( rg_clusteredLightingBoundGridIndex != gridIndex ) {
		modernClusterGridGpuParams_t params;
		R_ModernClusteredLighting_BuildGridParams( rg_clusteredLightingFrame.grids[gridIndex], rg_clusteredLightingStats, params );
		R_ModernClusteredLighting_UpdateBuffer( GL_UNIFORM_BUFFER, rg_clusteredLightingParamsUBO, 0, sizeof( params ), &params );
		rg_clusteredLightingBoundGridIndex = gridIndex;
		rg_clusteredLightingStats.gridSwitches++;
	}
	R_ModernClusteredLighting_BindGpuBuffers( R_ModernClusteredLighting_UseShaderStoragePath() );
	R_RendererMetrics_RecordClusteredLighting( rg_clusteredLightingStats );
	return true;
}

int R_ModernClusteredLighting_NumLightDescriptors( void ) {
	return rg_clusteredLightingFrame.lightCount;
}

const rendererModernLightDescriptor_t *R_ModernClusteredLighting_LightDescriptor( int index ) {
	if ( index < 0 || index >= rg_clusteredLightingFrame.lightCount ) {
		return NULL;
	}
	return &rg_clusteredLightingFrame.lights[index].descriptor;
}

static void R_ModernClusteredLighting_SetupSelfTestView( viewDef_t &view, viewLight_t *lights, idRenderLightLocal *lightDefs, int lightCount ) {
	memset( &view, 0, sizeof( view ) );
	view.renderView.width = 1280;
	view.renderView.height = 720;
	view.renderView.fov_x = 90.0f;
	view.renderView.fov_y = 70.0f;
	view.renderView.vieworg.Zero();
	view.renderView.viewaxis = mat3_identity;
	view.viewport.x1 = 0;
	view.viewport.y1 = 0;
	view.viewport.x2 = 1279;
	view.viewport.y2 = 719;
	view.scissor.x1 = 0;
	view.scissor.y1 = 0;
	view.scissor.x2 = 1279;
	view.scissor.y2 = 719;
	for ( int i = 0; i < lightCount; ++i ) {
		memset( &lights[i], 0, sizeof( lights[i] ) );
		lightDefs[i].index = i;
		lightDefs[i].areaNum = -1;
		lightDefs[i].parms.origin.Set( 128.0f + 16.0f * i, 0.0f, 0.0f );
		lightDefs[i].parms.lightRadius.Set( 160.0f, 160.0f, 160.0f );
		lightDefs[i].parms.shaderParms[SHADERPARM_RED] = 1.0f;
		lightDefs[i].parms.shaderParms[SHADERPARM_GREEN] = 0.75f;
		lightDefs[i].parms.shaderParms[SHADERPARM_BLUE] = 0.5f;
		lights[i].lightDef = &lightDefs[i];
		lights[i].next = i + 1 < lightCount ? &lights[i + 1] : NULL;
		lights[i].scissorRect.x1 = 120;
		lights[i].scissorRect.y1 = 120;
		lights[i].scissorRect.x2 = 360;
		lights[i].scissorRect.y2 = 300;
		lights[i].globalLightOrigin = lightDefs[i].parms.origin;
		lights[i].lightRadius = lightDefs[i].parms.lightRadius;
		lights[i].pointLight = ( i & 1 ) == 0;
		lights[i].parallel = false;
		lights[i].viewInsideLight = i == 0;
		lights[i].viewSeesGlobalLightOrigin = true;
	}
	view.viewLights = lightCount > 0 ? &lights[0] : NULL;
}

bool RendererClusterGrid_RunSelfTest( void ) {
	if ( !r_rendererModernExecutor.GetBool() && r_rendererClusterDebug.GetInteger() <= 0 ) {
		common->Printf( "RendererClusterGrid self-test passed (disabled)\n" );
		return true;
	}

	viewDef_t view;
	viewLight_t lights[6];
	idRenderLightLocal lightDefs[6];
	R_ModernClusteredLighting_SetupSelfTestView( view, lights, lightDefs, 6 );

	idScenePacketFrame packetFrame;
	packetFrame.Clear();
	if ( !packetFrame.AddScene( &view, true ) ) {
		common->Printf( "RendererClusterGrid self-test failed: could not add scene\n" );
		return false;
	}
	packetFrame.FinishScene();

	rendererClusteredLightingStats_t stats;
	R_ModernClusteredLighting_BuildFrame( packetFrame, true, stats );
	if ( !stats.frameValid || !stats.lossless || !stats.csrReady || stats.gridCount != 1 || stats.lightCount != 6 || stats.pointLights != 3 || stats.projectedLights != 3 || stats.clusterCount <= 0 || stats.activeClusters <= 0 || stats.maxLightsInCluster < 6 || stats.overflowClusters != 0 || stats.overflowReferences != 0 || stats.clusterRecordCount != stats.clusterCount || stats.flatIndexRecordCount <= 0 || stats.flatIndexReferenceCapacity < stats.uploadedReferences || stats.uploadedGridIndexRecords != stats.clusterRecordCount + stats.flatIndexRecordCount ) {
		common->Printf(
			"RendererClusterGrid self-test failed: grid=%d valid=%d lossless=%d csr=%d lights=%d point=%d projected=%d clusters=%d active=%d maxCluster=%d records=%d+%d cap=%d overflow=%d refs=%d indexRecords=%d\n",
			stats.gridCount,
			stats.frameValid ? 1 : 0,
			stats.lossless ? 1 : 0,
			stats.csrReady ? 1 : 0,
			stats.lightCount,
			stats.pointLights,
			stats.projectedLights,
			stats.clusterCount,
			stats.activeClusters,
			stats.maxLightsInCluster,
			stats.clusterRecordCount,
			stats.flatIndexRecordCount,
			stats.flatIndexReferenceCapacity,
			stats.overflowClusters,
			stats.overflowReferences,
			stats.uploadedGridIndexRecords );
		return false;
	}
	const rendererModernLightDescriptor_t *firstDescriptor = rg_clusteredLightingFrame.lightCount > 0 ? &rg_clusteredLightingFrame.lights[0].descriptor : NULL;
	if ( firstDescriptor == NULL
		|| firstDescriptor->type != RENDERER_MODERN_LIGHT_POINT
		|| firstDescriptor->viewOriginRadius[3] <= 1.0f
		|| firstDescriptor->color[0] <= 0.0f
		|| firstDescriptor->scissorDepth[2] <= firstDescriptor->scissorDepth[0]
		|| firstDescriptor->depthRange[1] <= firstDescriptor->depthRange[0] ) {
		common->Printf( "RendererClusterGrid self-test failed: shared descriptor contract invalid\n" );
		return false;
	}
	const int sliceA = R_ModernClusteredLighting_DepthSliceForZ( rg_clusteredLightingFrame.grids[0], 32.0f );
	const int sliceB = R_ModernClusteredLighting_DepthSliceForZ( rg_clusteredLightingFrame.grids[0], 512.0f );
	const int sliceC = R_ModernClusteredLighting_DepthSliceForZ( rg_clusteredLightingFrame.grids[0], 2048.0f );
	if ( sliceA > sliceB || sliceB > sliceC ) {
		common->Printf( "RendererClusterGrid self-test failed: depth slices not monotonic (%d %d %d)\n", sliceA, sliceB, sliceC );
		return false;
	}
	if ( rg_clusteredLightingAvailable && rg_clusteredLightingInitialized ) {
		if ( !R_ModernClusteredLighting_UploadBuffers( stats ) ) {
			common->Printf( "RendererClusterGrid self-test failed: clustered CSR upload unavailable\n" );
			return false;
		}
		if ( !stats.buffersReady || !stats.lossless || !stats.csrReady || stats.uploadedLights != 6 || stats.uploadedClusters <= 0 || stats.uploadedReferences != stats.lightReferences || stats.uploadedGridIndexRecords <= 0 || ( stats.computeBinningReady && ( !stats.computeBinningExecuted || stats.computeBinningDispatches <= 0 ) ) ) {
			common->Printf( "RendererClusterGrid self-test failed: upload stats invalid (buffers=%d lossless=%d csr=%d lights=%d clusters=%d refs=%d/%d indexRecords=%d compute=%d/%d dispatch=%d)\n", stats.buffersReady ? 1 : 0, stats.lossless ? 1 : 0, stats.csrReady ? 1 : 0, stats.uploadedLights, stats.uploadedClusters, stats.uploadedReferences, stats.lightReferences, stats.uploadedGridIndexRecords, stats.computeBinningReady ? 1 : 0, stats.computeBinningExecuted ? 1 : 0, stats.computeBinningDispatches );
			return false;
		}
		rg_clusteredLightingStats = stats;
		if ( !R_ModernClusteredLighting_BindGridForView( &view ) ) {
			common->Printf( "RendererClusterGrid self-test failed: grid bind unavailable\n" );
			return false;
		}
		stats = rg_clusteredLightingStats;
		if ( r_rendererClusterDebug.GetInteger() > 0 && !R_ModernClusteredLighting_UpdateDebugTexture( stats ) ) {
			common->Printf( "RendererClusterGrid self-test failed: debug texture update unavailable\n" );
			return false;
		}
	}

	viewLight_t stressLights[40];
	idRenderLightLocal stressDefs[40];
	R_ModernClusteredLighting_SetupSelfTestView( view, stressLights, stressDefs, 40 );
	packetFrame.Clear();
	if ( !packetFrame.AddScene( &view, true ) ) {
		common->Printf( "RendererClusterGrid self-test failed: could not add stress scene\n" );
		return false;
	}
	packetFrame.FinishScene();
	rendererClusteredLightingStats_t stressStats;
	R_ModernClusteredLighting_BuildFrame( packetFrame, true, stressStats );
	if ( !stressStats.frameValid || !stressStats.csrReady || !stressStats.lossless || stressStats.lightCount != 40 || stressStats.spillReferences != 0 || stressStats.unsampledSpillReferences != 0 || stressStats.lossyReferences != 0 || stressStats.overflowReferences != 0 || stressStats.maxLightsInCluster < 40 || stressStats.uploadedReferences != stressStats.lightReferences ) {
		common->Printf(
			"RendererClusterGrid self-test failed: CSR dense path invalid (valid=%d csr=%d lossless=%d lights=%d refs=%d/%d spill=%d/%d unsampled=%d lossy=%d overflow=%d/%d maxCluster=%d records=%d+%d cap=%d)\n",
			stressStats.frameValid ? 1 : 0,
			stressStats.csrReady ? 1 : 0,
			stressStats.lossless ? 1 : 0,
			stressStats.lightCount,
			stressStats.uploadedReferences,
			stressStats.lightReferences,
			stressStats.spillClusters,
			stressStats.spillReferences,
			stressStats.unsampledSpillReferences,
			stressStats.lossyReferences,
			stressStats.overflowClusters,
			stressStats.overflowReferences,
			stressStats.maxLightsInCluster,
			stressStats.clusterRecordCount,
			stressStats.flatIndexRecordCount,
			stressStats.flatIndexReferenceCapacity );
		return false;
	}

	common->Printf(
		"RendererClusterGrid self-test passed (grid=%dx%dx%d lights=%d point=%d projected=%d clusters=%d active=%d refs=%d uploaded=%d/%d indexRecords=%d csr=%d records=%d+%d cap=%d compute=%d/%d dispatch=%d lossless=%d stressRefs=%d/%d stressMax=%d spill=%d/%d overflow=%d/%d maxCluster=%d/%d groups=%d ubo=%d ssbo=%d overlay=%d)\n",
		stats.tileCountX,
		stats.tileCountY,
		stats.sliceCountZ,
		stats.lightCount,
		stats.pointLights,
		stats.projectedLights,
		stats.clusterCount,
		stats.activeClusters,
		stats.lightReferences,
		stats.uploadedLights,
		stats.uploadedReferences,
		stats.uploadedGridIndexRecords,
		stats.csrReady ? 1 : 0,
		stats.clusterRecordCount,
		stats.flatIndexRecordCount,
		stats.flatIndexReferenceCapacity,
		stats.computeBinningReady ? 1 : 0,
		stats.computeBinningExecuted ? 1 : 0,
		stats.computeBinningDispatches,
		stats.lossless ? 1 : 0,
		stressStats.uploadedReferences,
		stressStats.lightReferences,
		stressStats.maxLightsInCluster,
		stressStats.spillClusters,
		stressStats.spillReferences,
		stats.overflowClusters,
		stats.overflowReferences,
		stats.maxLightsInCluster,
		stats.maxLightsPerCluster,
		stats.indexGroupsPerCluster,
		stats.uboFallbackReady ? 1 : 0,
		stats.shaderStorageReady ? 1 : 0,
		rg_clusteredLightingDebugProgram != 0 ? 1 : 0 );
	return true;
}
