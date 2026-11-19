// Copyright (C) 2004 Id Software, Inc.
//

#ifndef __MODERN_CLUSTERED_LIGHTING_H__
#define __MODERN_CLUSTERED_LIGHTING_H__

#include "RendererCaps.h"
#include "ModernSpecularProbeAtlas.h"
#include <vector>

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

static const int RENDERER_MODERN_SHADOW_DESCRIPTOR_MAX_TILES = 6;
static const int RENDERER_MODERN_SHADOW_DESCRIPTOR_MAX_CASCADES = 4;
static const int RENDERER_CLUSTER_SPECULAR_PROBE_MAX_RECORDS = 32;
static const int RENDERER_CLUSTER_SPECULAR_PROBES_PER_CLUSTER = 2;

// The same packed records and CPU top-two selection serve both graphics APIs.
// Resource allocation, upload, publication and lifetime belong to the caller.
struct rendererSpecularProbeRecord_t {
	float positionRadius[4];
	float tintIntensity[4];
	float axisXPriority[4];
	float axisYBlend[4];
	float axisZSlot[4];
	float identity[4];
	// xyz: half extents along the axes; w: 1 for a box-parallax probe
	// (positionRadius.w is then its bounding-sphere radius), 0 for a sphere.
	float boxExtents[4];
};

struct rendererSpecularProbeView_t {
	const viewDef_t *viewDef;
	float grid[4]; // xyz dimensions, w record count
	float depth[4]; // near, far, log(far/near), publication generation
	float viewOrigin[4];
	float worldToView[3][4]; // camera left, up, forward; w unused
	float projection[4]; // x/y scale and offset, before the Vulkan Y flip
	rendererSpecularProbeRecord_t records[RENDERER_CLUSTER_SPECULAR_PROBE_MAX_RECORDS];
	std::vector<std::uint32_t> indices; // two record indices per cluster
};

typedef modernSpecularProbeAtlasReject_t ( *rendererProbeAtlasAcquire_t )(
	const idImage *, modernSpecularProbeAtlasPlacement_t * );
static const int RENDERER_CLUSTER_DECAL_MAX_RECORDS = 1024;
static const int RENDERER_CLUSTER_DECAL_MAX_REFERENCES = 65536;
static const unsigned int RENDERER_CLUSTER_DECAL_INVALID_STABLE_ID = 0xffffffffu;

// CPU-only source contract for the isolated clustered-decal transaction.  The
// screen rectangle is inclusive and view-local.  Stable IDs must describe the
// immutable material/geometry/instance resources behind sourceSurface; the
// pointer itself is used only as exact within-frame membership identity.
typedef struct rendererClusteredDecalSource_s {
	const viewDef_t *	viewDef;
	const void *		sourceSurface;
	int				commandIndex;
	int				commandOrder;
	unsigned int		materialStableId;
	unsigned int		geometryStableId;
	unsigned int		instanceStableId;
	int				screenX1;
	int				screenY1;
	int				screenX2;
	int				screenY2;
	float			depthMin;
	float			depthMax;
	unsigned int		generation;
} rendererClusteredDecalSource_t;

enum rendererClusteredDecalReject_t {
	RENDERER_CLUSTER_DECAL_REJECT_NONE = 0,
	RENDERER_CLUSTER_DECAL_REJECT_NOT_PREPARED,
	RENDERER_CLUSTER_DECAL_REJECT_INVALID_ARGUMENT,
	RENDERER_CLUSTER_DECAL_REJECT_RESOURCE_UNAVAILABLE,
	RENDERER_CLUSTER_DECAL_REJECT_RECORD_CAPACITY,
	RENDERER_CLUSTER_DECAL_REJECT_REFERENCE_CAPACITY,
	RENDERER_CLUSTER_DECAL_REJECT_STALE_GENERATION,
	RENDERER_CLUSTER_DECAL_REJECT_STALE_IDENTITY,
	RENDERER_CLUSTER_DECAL_REJECT_MALFORMED_RECT,
	RENDERER_CLUSTER_DECAL_REJECT_MALFORMED_DEPTH,
	RENDERER_CLUSTER_DECAL_REJECT_MALFORMED_ORDER,
	RENDERER_CLUSTER_DECAL_REJECT_VIEW_INCOMPLETE,
	RENDERER_CLUSTER_DECAL_REJECT_ORDER_MISMATCH,
	RENDERER_CLUSTER_DECAL_REJECT_HASH_MISMATCH,
	RENDERER_CLUSTER_DECAL_REJECT_INTERNAL
};

typedef struct rendererClusteredDecalStats_s {
	bool		prepared;
	bool		sealed;
	bool		ownershipReady;
	bool		csrReady;
	unsigned int	generation;
	int		submittedRecords;
	int		stagedRecords;
	int		publishedRecords;
	int		viewCount;
	int		ownedViews;
	int		clusterCount;
	int		clusterReferences;
	int		recordCapacity;
	int		referenceCapacity;
	int		prepareRejects;
	int		sealRejects;
	rendererClusteredDecalReject_t lastReject;
	int		lastRejectSource;
	int		lastRejectView;
	unsigned long long sourceOrderHash;
	char		status[96];
} rendererClusteredDecalStats_t;

typedef struct rendererClusteredDecalViewStats_s {
	const viewDef_t *	viewDef;
	bool				prepared;
	bool				owned;
	int				recordCount;
	int				firstCommandOrder;
	int				lastCommandOrder;
	int				clusterReferences;
	unsigned long long sourceOrderHash;
} rendererClusteredDecalViewStats_t;

enum rendererModernShadowDescriptorFlag_t {
	RENDERER_MODERN_SHADOW_DESCRIPTOR_FLAG_MAPPED = 1 << 0,
	RENDERER_MODERN_SHADOW_DESCRIPTOR_FLAG_CACHE_REUSE = 1 << 1,
	RENDERER_MODERN_SHADOW_DESCRIPTOR_FLAG_STENCIL_FALLBACK = 1 << 2,
	RENDERER_MODERN_SHADOW_DESCRIPTOR_FLAG_SKIPPED = 1 << 3,
	RENDERER_MODERN_SHADOW_DESCRIPTOR_FLAG_RECEIVER_BLOCKED = 1 << 4,
	RENDERER_MODERN_SHADOW_DESCRIPTOR_FLAG_PROJECTED = 1 << 5,
	RENDERER_MODERN_SHADOW_DESCRIPTOR_FLAG_POINT = 1 << 6,
	RENDERER_MODERN_SHADOW_DESCRIPTOR_FLAG_CASCADE = 1 << 7,
	RENDERER_MODERN_SHADOW_DESCRIPTOR_FLAG_TRANSLUCENT = 1 << 8,
	RENDERER_MODERN_SHADOW_DESCRIPTOR_FLAG_ATLAS_READY = 1 << 9,
	RENDERER_MODERN_SHADOW_DESCRIPTOR_FLAG_CASTER_READY = 1 << 10,
	RENDERER_MODERN_SHADOW_DESCRIPTOR_FLAG_RECEIVER_GUARD_READY = 1 << 11,
	RENDERER_MODERN_SHADOW_DESCRIPTOR_FLAG_SAMPLING_READY = 1 << 12,
	RENDERER_MODERN_SHADOW_DESCRIPTOR_FLAG_STABLE_CASCADE = 1 << 13,
	RENDERER_MODERN_SHADOW_DESCRIPTOR_FLAG_PROJECTED_STATE_READY = 1 << 14,
	RENDERER_MODERN_SHADOW_DESCRIPTOR_FLAG_PROJECTED_FALLBACK = 1 << 15,
	RENDERER_MODERN_SHADOW_DESCRIPTOR_FLAG_RECEIVER_PLANE_BIAS = 1 << 16,
	// The descriptor references a signature-current, static-complete physical
	// ARB2 resource: a persistent-atlas cell for projected lights or the exact
	// singular cache cube currently bound for a point light.
	RENDERER_MODERN_SHADOW_DESCRIPTOR_FLAG_ATLAS_SLOT = 1 << 17,
	RENDERER_MODERN_SHADOW_DESCRIPTOR_FLAG_CURRENT_FRAME_MAP = 1 << 18
};

typedef struct rendererModernShadowDescriptor_s {
	int		descriptorIndex;
	int		sceneIndex;
	int		lightDefIndex;
	int		mapType;
	int		policy;
	int		fallbackReason;
	int		compareMode;
	int		biasModel;
	int		depthFormat;
	int		pcfKernel;
	int		tileCount;
	int		cascadeCount;
	int		requestedCascadeCount;
	int		atlasDiv;
	int		faceIndex;
	int		cascadeIndex;
	int		projectedFallbackCascade;
	int		projectedFallbackReason;
	// index + 1 of the cascade holding the light's whole map, 0 = none
	int		wholeLightCascadeSlot;
	int		projectedSampleCount;
	int		projectedValidSampleCount;
	int		projectedSkippedSampleCount;
	int		flags;
	// freshness (I7): frame the planner built this descriptor, and the frame
	// the referenced atlas cell or point cube was last rendered
	int		updateFrame;
	int		atlasContentFrame;
	int		atlasCellX;
	int		atlasCellY;
	int		atlasCellSpan;
	bool	atlasSlotValid;
	float	shadowMatrix[16];
	float	viewShadowMatrix[RENDERER_MODERN_SHADOW_DESCRIPTOR_MAX_CASCADES][16];
	float	projection[4];
	float	cascadeSplitDepths[RENDERER_MODERN_SHADOW_DESCRIPTOR_MAX_CASCADES];
	float	cascadeBiasScale[RENDERER_MODERN_SHADOW_DESCRIPTOR_MAX_CASCADES];
	float	texelDepthBias[RENDERER_MODERN_SHADOW_DESCRIPTOR_MAX_CASCADES];
	float	worldTexelSize[RENDERER_MODERN_SHADOW_DESCRIPTOR_MAX_CASCADES];
	float	sliceNear[RENDERER_MODERN_SHADOW_DESCRIPTOR_MAX_CASCADES];
	float	sliceFar[RENDERER_MODERN_SHADOW_DESCRIPTOR_MAX_CASCADES];
	float	depthRange[RENDERER_MODERN_SHADOW_DESCRIPTOR_MAX_CASCADES];
	float	clipZExtent[RENDERER_MODERN_SHADOW_DESCRIPTOR_MAX_CASCADES];
	float	bias[4];
	float	normalOffsetScale;
	float	projectedBaseClipPlanes[4][4];
	float	projectedClipPlanes[RENDERER_MODERN_SHADOW_DESCRIPTOR_MAX_CASCADES][4][4];
	float	projectedAtlasRect[RENDERER_MODERN_SHADOW_DESCRIPTOR_MAX_CASCADES][4];
	// Projected filter, as the classic and Vulkan receivers take it
	// (R_ShadowMapProjectedFilterSettings): x = contact radius in texels,
	// y = taps + 32 * filter mode, z = PCSS search (light) radius and
	// w = PCSS maximum radius, both zero unless the mode is PCSS-lite (2).
	// Zero for point lights.
	float	projectedFilter[4];
} rendererModernShadowDescriptor_t;

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
	float	falloff[4]; // Cluster-space plane for the authored 1D falloff texture.
	float	projectS[4];
	float	projectT[4];
	float	projectQ[4];
	unsigned int projectionImageHandle;
	unsigned int falloffImageHandle;
	unsigned int cubeImageHandle;
	// cells in the shared light-image atlas, { uOffset, vOffset, uScale, vScale }.
	// A zero scale means the image is not resident and must not be sampled;
	// atlasReady is the single "this light can be shaded from the atlas" answer.
	float		falloffAtlasRect[4];
	float		projectionAtlasRect[4];
	bool		atlasReady;
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
	bool	shadowDescriptorBufferReady;
	bool	uboFallbackReady;
	bool	debugOverlayReady;
	bool	debugTextureReady;
	bool	shaderStorageReady;
	bool	csrReady;
	bool	computeBinningReady;
	bool	computeBinningExecuted;
	bool	probeBufferReady;
	bool	probeFrameReady;
	bool	probeCpuCSRForced;
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
	int		uploadedShadowDescriptors;
	int		shadowDescriptorCapacity;
	int		shadowReceiverBlockedLights;
	int		shadowAtlasSlotBlockedLights;
	int		probeCount;
	int		uploadedProbes;
	int		probeCapacity;
	int		probeReferences;
	int		probeOverflow;
	int		probeRejectedMaterial;
	int		probeRejectedVolume;
	int		probeRejectedAtlas;
	unsigned int probeFrameGeneration;
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
	int		shadowDescriptorBytes;
	int		probeUBOBytes;
	int		debugMode;
	int		debugOverlayDraws;
	int		debugStringTruncations;
	char	debugStringTruncationSource[64];
	char	status[96];
} rendererClusteredLightingStats_t;

void R_ModernClusteredLighting_Init( const renderBackendCaps_t &caps, const renderFeatureSet_t &features );
void R_ModernClusteredLighting_Shutdown( void );
void R_ModernClusteredLighting_PrepareFrame( const idScenePacketFrame &packetFrame, bool requested );
// CPU-only export. The acquire callback must publish complete, resident tiles.
// Failure clears every view; partial probe sets are never consumed. No GL call
// is made. Native storage budgets use the same grid policy as the GL SSBO path.
// The probe views of the cluster frame the modern executor built this frame,
// for the classic OpenGL PBR owner that draws beside it (draw_pbr.cpp).
bool R_ModernClusteredLighting_ExportProbeViews( std::uint64_t generation,
	std::vector<rendererSpecularProbeView_t> &views );
bool R_ModernClusteredLighting_PrepareProbes( const idScenePacketFrame &packetFrame,
	rendererProbeAtlasAcquire_t acquire, std::uint64_t generation,
	std::vector<rendererSpecularProbeView_t> &views, rendererClusteredLightingStats_t &stats );
void R_ModernClusteredLighting_ResetDecalsForFrame( void );
void R_ModernClusteredLighting_DrawDebugOverlay( void );
void R_ModernClusteredLighting_PrintGfxInfo( void );
const rendererClusteredLightingStats_t &R_ModernClusteredLighting_Stats( void );
bool R_ModernClusteredLighting_FrameLossless( void );
bool R_ModernClusteredLighting_BindGridForView( const viewDef_t *viewDef );
int R_ModernClusteredLighting_NumLightDescriptors( void );
const rendererModernLightDescriptor_t *R_ModernClusteredLighting_LightDescriptor( int index );
int R_ModernClusteredLighting_NumShadowDescriptors( void );
const rendererModernShadowDescriptor_t *R_ModernClusteredLighting_ShadowDescriptor( int index );
// linked-size expectation for the shadow-descriptor UBO block, for std140
// layout-drift introspection against the driver (M5)
int R_ModernClusteredLighting_ShadowDescriptorUboBlockBytes( void );
// linked-size expectation for the fixed std140 authored-probe block
int R_ModernClusteredLighting_ProbeUboBlockBytes( void );

// Two-phase, whole-frame CPU transaction.  Both calls must occur after the
// ordinary cluster grids have been prepared, and SealDecals must receive the
// same authoritative list.  No membership query succeeds before an exact seal.
bool R_ModernClusteredLighting_PrepareDecals( const rendererClusteredDecalSource_t *sources, int sourceCount, unsigned int generation );
bool R_ModernClusteredLighting_SealDecals( const rendererClusteredDecalSource_t *sources, int sourceCount, unsigned int generation );
// Explicitly aborts the current frame transaction before classic ownership is
// queried.  This is used when the authoritative executor preflight discovers a
// resource or identity that cannot be represented by the sealed source list.
bool R_ModernClusteredLighting_RejectDecalsForFrame( rendererClusteredDecalReject_t reason, int sourceIndex, int submittedRecords, unsigned int generation );
const rendererClusteredDecalStats_t &R_ModernClusteredLighting_DecalStats( void );
bool R_ModernClusteredLighting_DecalViewStats( const viewDef_t *viewDef, rendererClusteredDecalViewStats_t &stats );
bool R_ModernClusteredLighting_DecalOwnsSurface( const viewDef_t *viewDef, const void *sourceSurface );
bool R_ModernClusteredLighting_DecalOwnsCommand( const viewDef_t *viewDef, int commandIndex );
// Sealed-ownership resolution: returns the exact submit-plan command index the
// seal recorded for sourceSurface in this view, -1 when the surface is not
// owned, and sets duplicate when more than one sealed record claims it.
int R_ModernClusteredLighting_DecalSealedCommandForSurface( const viewDef_t *viewDef, const void *sourceSurface, bool &duplicate );
const char *R_ModernClusteredLighting_DecalRejectName( rendererClusteredDecalReject_t reject );
bool RendererClusterGrid_RunSelfTest( void );

#endif /* !__MODERN_CLUSTERED_LIGHTING_H__ */
