// Copyright (C) 2004 Id Software, Inc.
//

#ifndef __SCENE_PACKETS_H__
#define __SCENE_PACKETS_H__

#include "GpuSkinning.h"
#include "ClassicDeformDomain.h"
#include "TemporalHistoryCore.h"

/*
===============================================================================

	Backend-neutral frame data scaffolding.

	The legacy renderer still consumes drawSurfs directly.  These packet types are
	the stable contract the modern GL executors and render graph will grow into.

===============================================================================
*/

class idImage;
class idMaterial;

enum renderPassCategory_t {
	RENDER_PASS_DEPTH = 0,
	RENDER_PASS_STENCIL_SHADOW,
	RENDER_PASS_SHADOW_MAP,
	RENDER_PASS_ARB2_INTERACTION,
	RENDER_PASS_LIGHT_GRID,
	RENDER_PASS_AMBIENT,
	RENDER_PASS_DEFERRED_RESOLVE,
	RENDER_PASS_FORWARD_PLUS,
	RENDER_PASS_FOG_BLEND,
	RENDER_PASS_SSAO,
	RENDER_PASS_MOTION_BLUR,
	RENDER_PASS_BLOOM,
	RENDER_PASS_AUTHORED_POST,
	RENDER_PASS_SPECIAL_EFFECTS,
	RENDER_PASS_GUI,
	RENDER_PASS_PRESENT
};

const int SCENE_PACKET_MAX_SCENES = 64;
const int SCENE_PACKET_MAX_PASSES = 256;
const int SCENE_PACKET_MAX_DRAWS = 4096;
const int SCENE_PACKET_MAX_MATERIAL_RECORDS = 1024;
const int SCENE_PACKET_MAX_GEOMETRY_RECORDS = 1024;
const int SCENE_PACKET_MAX_INSTANCE_RECORDS = 1024;
// Captures are command-stream edges rather than drawable passes. A subview
// publishes at most one color capture, so bound this table with scenes.
const int SCENE_PACKET_MAX_SUBVIEW_CAPTURES = SCENE_PACKET_MAX_SCENES;

enum scenePacketCategory_t {
	SCENE_PACKET_CATEGORY_UNKNOWN = 0,
	SCENE_PACKET_CATEGORY_WORLD,
	SCENE_PACKET_CATEGORY_SUBVIEW,
	SCENE_PACKET_CATEGORY_REMOTE_CAMERA,
	SCENE_PACKET_CATEGORY_SPECIAL_EFFECTS,
	SCENE_PACKET_CATEGORY_VIEWMODEL,
	SCENE_PACKET_CATEGORY_RENDER_DEMO,
	SCENE_PACKET_CATEGORY_GUI,
	SCENE_PACKET_CATEGORY_POST_PROCESS,
	SCENE_PACKET_CATEGORY_PRESENT,
	SCENE_PACKET_CATEGORY_COMMAND
};

enum scenePacketOverflowCause_t {
	SCENE_PACKET_OVERFLOW_NONE = 0,
	SCENE_PACKET_OVERFLOW_SCENES,
	SCENE_PACKET_OVERFLOW_PASSES,
	SCENE_PACKET_OVERFLOW_DRAWS,
	SCENE_PACKET_OVERFLOW_MATERIALS,
	SCENE_PACKET_OVERFLOW_GEOMETRY_RECORDS,
	SCENE_PACKET_OVERFLOW_INSTANCE_RECORDS,
	SCENE_PACKET_OVERFLOW_SUBVIEW_CAPTURES
};

enum rendererMaterialClass_t {
	RENDER_MATERIAL_NONE = 0,
	RENDER_MATERIAL_SHADOW_ONLY,
	RENDER_MATERIAL_OPAQUE,
	RENDER_MATERIAL_PERFORATED,
	RENDER_MATERIAL_TRANSLUCENT,
	RENDER_MATERIAL_GUI,
	RENDER_MATERIAL_SUBVIEW,
	RENDER_MATERIAL_POST_PROCESS
};

typedef struct rendererPermutationKey_s {
	unsigned int	materialClass;
	unsigned int	lightingMode;
	unsigned int	shadowMode;
	unsigned int	alphaMode;
	unsigned int	skinningMode;
	unsigned int	deformMode;
	unsigned int	lightGridMode;
	unsigned int	fogMode;
	unsigned int	debugMode;
	unsigned int	tier;
} rendererPermutationKey_t;

typedef struct materialResourceRecord_s {
	const idMaterial		*material;
	const idImage			*diffuseImage;
	const idImage			*normalImage;
	const idImage			*specularImage;
	const idImage			*pbrAlbedoImage;
	const idImage			*pbrNormalImage;
	const idImage			*pbrORMImage;
	const idImage			*pbrMetallicImage;
	const idImage			*pbrRoughnessImage;
	const idImage			*pbrAOImage;
	const idImage			*pbrEmissiveImage;
	bool					hasPBR;
	int					pbrWorkflow;
	int					pbrNormalFormat;
	bool					pbrHasAuthoredClassicFallback;
	bool					pbrHasExplicitLegacyFallback;
	bool					pbrUsesGeneratedLegacyFallback;
	bool					pbrUsesApproximateLegacyFallback;
	bool					pbrLegacyFallbackMissing;
	int					pbrMetallicRegister;
	int					pbrRoughnessRegister;
	int					pbrAORegister;
	int					pbrNormalScaleRegister;
	int					pbrEmissiveColorRegisters[3];
	int					resourceTableIndex;
	// per-material DI_REMOTE_RENDER stage presence, cached once at record
	// creation so AddDrawPacket does not re-walk stages per draw packet
	bool					usesRemoteRender;
	rendererPermutationKey_t permutation;
} materialResourceRecord_t;

enum geometryUploadLifetime_t {
	GEOMETRY_UPLOAD_LIFETIME_UNKNOWN = 0,
	GEOMETRY_UPLOAD_LIFETIME_STATIC,
	GEOMETRY_UPLOAD_LIFETIME_FRAME_TEMP,
	GEOMETRY_UPLOAD_LIFETIME_CLIENT_MEMORY,
	GEOMETRY_UPLOAD_LIFETIME_DYNAMIC_BRIDGE
};

enum geometrySkinningMode_t {
	GEOMETRY_SKINNING_NONE = 0,
	GEOMETRY_SKINNING_CPU,
	GEOMETRY_SKINNING_GPU_PALETTE
};

enum geometryDeformMode_t {
	GEOMETRY_DEFORM_NONE = 0,
	GEOMETRY_DEFORM_SURFACE,
	GEOMETRY_DEFORM_MATERIAL
};

enum geometryResourceFallbackReason_t {
	GEOMETRY_RESOURCE_FALLBACK_NONE = 0,
	GEOMETRY_RESOURCE_FALLBACK_MISSING_GEOMETRY,
	GEOMETRY_RESOURCE_FALLBACK_UNSUPPORTED_DEFORM,
	GEOMETRY_RESOURCE_FALLBACK_UNSUPPORTED_GPU_SKINNING,
	GEOMETRY_RESOURCE_FALLBACK_MISSING_VERTEX_BUFFER,
	GEOMETRY_RESOURCE_FALLBACK_MISSING_INDEX_DATA
};

enum geometryResourceFallbackFlags_t {
	GEOMETRY_RESOURCE_FALLBACK_FLAG_MISSING_GEOMETRY = 1 << 0,
	GEOMETRY_RESOURCE_FALLBACK_FLAG_UNSUPPORTED_DEFORM = 1 << 1,
	GEOMETRY_RESOURCE_FALLBACK_FLAG_UNSUPPORTED_GPU_SKINNING = 1 << 2,
	GEOMETRY_RESOURCE_FALLBACK_FLAG_MISSING_VERTEX_BUFFER = 1 << 3,
	GEOMETRY_RESOURCE_FALLBACK_FLAG_MISSING_INDEX_DATA = 1 << 4
};

typedef struct geometryResourceRecord_s {
	const srfTriangles_t		*legacyGeometry;
	idBounds				bounds;
	int						recordIndex;
	int						ambientVertexBuffer;
	int						indexBuffer;
	int						ambientCacheOffset;
	int						indexCacheOffset;
	int						ambientCacheBytes;
	int						indexCacheBytes;
	int						vertexStride;
	int						indexType;
	int						vertexCount;
	int						indexCount;
	int						firstVertex;
	int						firstIndex;
	int						skinningMode;
	int						deformMode;
	int						uploadLifetime;
	classicDeformRecord_t	classicDeform;
	geometryResourceFallbackReason_t fallbackReason;
	unsigned int			fallbackFlags;
	int						skinningPaletteOffset;
	int						skinningPaletteCount;
	gpuSkinningSurface_t	gpuSkinningSurface;
	const glIndex_t			*legacyIndexData;
	bool					hasAmbientVertexBuffer;
	bool					hasIndexBuffer;
	bool					hasClientIndexData;
	bool					hasPrimBatchMesh;
	bool					hasGpuSkinningContract;
	bool					hasBounds;
	bool					hasClassicDeformRecord;
} geometryResourceRecord_t;

enum instanceVisibilityFlags_t {
	INSTANCE_VISIBILITY_NONE = 0,
	INSTANCE_VISIBILITY_WORLD = 1 << 0,
	INSTANCE_VISIBILITY_GUI = 1 << 1,
	INSTANCE_VISIBILITY_VIEWMODEL = 1 << 2,
	INSTANCE_VISIBILITY_SUBVIEW = 1 << 3,
	INSTANCE_VISIBILITY_REMOTE_CAMERA = 1 << 4,
	INSTANCE_VISIBILITY_RENDER_DEMO = 1 << 5,
	INSTANCE_VISIBILITY_LEGACY_BRIDGE = 1 << 6
};

typedef struct instanceRecord_s {
	const viewEntity_t		*legacySpace;
	const float				*legacyShaderRegisters;
	int						recordIndex;
	int						entityIndex;
	int						shaderRegisterBase;
	int						shaderRegisterCount;
	int						skinningPaletteOffset;
	int						visibilityFlags;
	unsigned long long		temporalViewIdentity;
	unsigned int			temporalHistoryGeneration;
	int						temporalHistoryAgeFrames;
	float					modelMatrix[16];
	float					previousModelMatrix[16];
	float					modelViewMatrix[16];
	float					entityColor[4];
	float					modelDepthHack;
	bool					hasModelMatrix;
	bool					hasPreviousModelMatrix;
	bool					hasShaderRegisters;
	bool					weaponDepthHack;
	bool					negativeScale;
	bool					legacyBridge;
	bool					temporalCaptureFrame;
} instanceRecord_t;

typedef struct drawPacketSortKey_s {
	unsigned long long	value;
} drawPacketSortKey_t;

enum sceneInteractionReceiverClass_t {
	SCENE_INTERACTION_RECEIVER_NONE = 0,
	SCENE_INTERACTION_RECEIVER_LOCAL,
	SCENE_INTERACTION_RECEIVER_GLOBAL,
	SCENE_INTERACTION_RECEIVER_TRANSLUCENT
};

// Fog/blend receiver identity is independent of the generic pass category.
// The classic phase walks each relevant viewLight in source order and then its
// authoritative GLOBAL -> LOCAL chains.  Capturing that identity lets shared
// owners prove exact coverage without re-evaluating the receiver material.
enum sceneFogBlendReceiverClass_t {
	SCENE_FOG_BLEND_RECEIVER_NONE = 0,
	SCENE_FOG_BLEND_RECEIVER_GLOBAL,
	SCENE_FOG_BLEND_RECEIVER_LOCAL,
	SCENE_FOG_BLEND_RECEIVER_COUNT
};

// Shadow packet identity is deliberately separate from the render-pass
// category.  One light can contribute the same surface to several ownership
// chains, and a flat RENDER_PASS_* list cannot prove which receiver-visible
// shadow contribution a packet represents.
enum sceneShadowCasterClass_t {
	SCENE_SHADOW_CASTER_NONE = 0,
	SCENE_SHADOW_CASTER_STENCIL_GLOBAL,
	SCENE_SHADOW_CASTER_STENCIL_LOCAL,
	SCENE_SHADOW_CASTER_MAP_GLOBAL_STATIC,
	SCENE_SHADOW_CASTER_MAP_LOCAL_STATIC,
	SCENE_SHADOW_CASTER_MAP_GLOBAL_DYNAMIC,
	SCENE_SHADOW_CASTER_MAP_LOCAL_DYNAMIC,
	SCENE_SHADOW_CASTER_MAP_GLOBAL_TRANSLUCENT,
	SCENE_SHADOW_CASTER_MAP_LOCAL_TRANSLUCENT,
	SCENE_SHADOW_CASTER_SUPPLEMENT_GLOBAL,
	SCENE_SHADOW_CASTER_SUPPLEMENT_LOCAL,
	SCENE_SHADOW_CASTER_COUNT
};

typedef struct drawPacket_s {
	const drawSurf_t			*legacyDrawSurf;
	const viewDef_t			*viewDef;
	const viewEntity_t		*space;
	const materialResourceRecord_t *materialRecord;
	const geometryResourceRecord_t *geometryRecord;
	const instanceRecord_t	*instanceRecord;
	drawPacketSortKey_t		sortKey;
	renderPassCategory_t	passCategory;
	scenePacketCategory_t	packetCategory;
	float					legacySort;
	int						materialRecordIndex;
	int						geometryRecordIndex;
	int						instanceRecordIndex;
	int						vertexCount;
	int						firstIndex;
	int						indexCount;
	int						vertexOffset;
	int						instanceOffset;
	int						instanceCount;
	temporalMotionOwnership_t temporalMotion;
	const viewLight_t		*interactionLight;
	int						interactionLightOrdinal;
	int						interactionReceiverOrdinal;
	int						interactionSourceOrdinal;
	sceneInteractionReceiverClass_t interactionReceiverClass;
	const viewLight_t		*fogBlendLight;
	int						fogBlendLightOrdinal;
	int						fogBlendReceiverOrdinal;
	int						fogBlendSourceOrdinal;
	sceneFogBlendReceiverClass_t fogBlendReceiverClass;
	const viewLight_t		*shadowLight;
	int						shadowLightOrdinal;
	int						shadowChainOrdinal;
	int						shadowSourceOrdinal;
	sceneShadowCasterClass_t shadowCasterClass;
	int						scissorX1;
	int						scissorY1;
	int						scissorX2;
	int						scissorY2;
	// The immutable deform contract is owned by geometryRecord. Keeping only an
	// alias here avoids duplicating the comparatively large sealed record across
	// every draw slot in the fixed packet arena.
	const classicDeformRecord_t *classicDeformRecord;
	bool					hasGeometry;
	bool					hasShaderRegisters;
	bool					hasIndexCache;
	bool					hasAmbientCache;
	bool					hasClassicDeformRecord;
	bool					temporalExactRigidEligible;
} drawPacket_t;

typedef struct passPacket_s {
	renderPassCategory_t	passCategory;
	scenePacketCategory_t	packetCategory;
	int						firstDrawPacket;
	int						drawPacketCount;
	bool					enabled;
	bool					commandOnly;
} passPacket_t;

typedef struct scenePacket_s {
	const viewDef_t			*viewDef;
	scenePacketCategory_t	packetCategory;
	int						firstPassPacket;
	int						passPacketCount;
	int						firstDrawPacket;
	int						drawPacketCount;
	bool					legacyBridge;
	// A real render-demo frame is identified by the live session stream, not
	// by a negative view id (which is also used by portal-sky cameras).
	bool					renderDemoPlayback;
	// RC_DRAW_SPECIAL_EFFECTS is command-only. Preserve the admitted controller
	// mask with its exact view association for special-frame ownership.
	int					specialEffectsMask;
	unsigned long long		temporalViewIdentity;
	unsigned int			temporalHistoryGeneration;
	int					temporalJitterIndex;
	temporalHistoryResetReason_t temporalHistoryResetReason;
	bool					temporalHistoryValid;
	bool					temporalJitterEnabled;
	bool					temporalCaptureFrame;
} scenePacket_t;

// CaptureRenderToImage immediately follows the RC_DRAW_VIEW which produced a
// capture-backed subview. Retaining that relationship prevents a shared owner
// from treating an unrelated feedback copy as its parent-facing image.
typedef struct sceneSubviewCapture_s {
	const viewDef_t			*viewDef;
	idImage					*image;
	int						viewScenePacketIndex;
	int						x;
	int						y;
	int						width;
	int						height;
	int						cubeFace;
	bool					copyDepth;
} sceneSubviewCapture_t;

typedef struct scenePacketFrameStats_s {
	int						scenePackets;
	int						passPackets;
	int						drawPackets;
	int						clippedDrawPackets;
	int						commandPackets;
	int						legacyDrawViews;
	int						materialRecords;
	int						geometryRecords;
	int						instanceRecords;
	int						drawPacketsWithMaterial;
	int						drawPacketsWithResourceRecord;
	int						drawPacketsWithGeometryRecord;
	int						drawPacketsWithInstanceRecord;
	int						drawPacketsWithGeometry;
	int						drawPacketsWithShaderRegisters;
	int						drawPacketsWithIndexCache;
	int						drawPacketsWithAmbientCache;
	int						drawPacketsWithClassicDeformRecord;
	int						materialDeformDrawPackets;
	int						deformFinalizedDrawPackets;
	int						deformInteractionReceiverPackets;
	int						deformFogReceiverPackets;
	int						deformShadowVolumePackets;
	int						deformOtherRolePackets;
	int						deformCompletedPackets;
	int						deformEmptyPackets;
	int						deformNotApplicablePackets;
	int						deformSkippedPackets;
	int						deformFailedPackets;
	int						deformUnsupportedPackets;
	int						deformFallbackPackets;
	int						worldPackets;
	int						subviewPackets;
	int						remoteCameraPackets;
	int						specialEffectPackets;
	int						viewmodelPackets;
	int						renderDemoPackets;
	int						guiPackets;
	int						postProcessPackets;
	int						presentPackets;
	int						commandOnlyPackets;
	int						temporalMotionDomainPackets[TEMPORAL_MOTION_DOMAIN_COUNT];
	int						temporalReactivePackets;
	int						temporalPreviousTransformPackets;
	int						temporalSeparateHistoryPackets;
	int						subviewCaptures;
	int						sortKeyValidationFailures;
	bool					frontEndDerived;
	bool					backendDerived;
	bool					overflow;
	scenePacketOverflowCause_t overflowCause;
} scenePacketFrameStats_t;

class idScenePacketFrame {
public:
	idScenePacketFrame();
	~idScenePacketFrame();
	void Clear( void );

	bool AddScene( const viewDef_t *viewDef, bool legacyBridge );
	bool AddPass( renderPassCategory_t category, bool enabled, bool commandOnly = false );
	void SetLastSceneSpecialEffectsMask( int specialEffectsMask );
	bool AddDrawPacket( const drawSurf_t *drawSurf, renderPassCategory_t category,
		int drawIndex,
		classicDeformRole_t deformRole = CLASSIC_DEFORM_ROLE_UNKNOWN );
	bool AddInteractionDrawPacket( const drawSurf_t *drawSurf, int drawIndex,
		const viewLight_t *viewLight, int lightOrdinal,
		sceneInteractionReceiverClass_t receiverClass, int receiverOrdinal );
	bool AddFogBlendDrawPacket( const drawSurf_t *drawSurf, int drawIndex,
		const viewLight_t *viewLight, int lightOrdinal,
		sceneFogBlendReceiverClass_t receiverClass, int receiverOrdinal );
	bool AddShadowDrawPacket( const drawSurf_t *drawSurf,
		renderPassCategory_t category, int drawIndex,
		const viewLight_t *viewLight, int lightOrdinal,
		sceneShadowCasterClass_t casterClass, int chainOrdinal );
	void FinishScene( void );
	void AddCommandPacket( scenePacketCategory_t category = SCENE_PACKET_CATEGORY_COMMAND );
	bool AddSubviewCapture( const viewDef_t *viewDef, idImage *image,
		int x, int y, int width, int height, int cubeFace, bool copyDepth );
	void AddLegacyDrawView( void );
	void AddClippedDrawPackets( int count );
	void MarkFrontEndDerived( void );
	void MarkBackendDerived( void );

	int NumScenes( void ) const;
	int NumPasses( void ) const;
	int NumDrawPackets( void ) const;
	int NumMaterialRecords( void ) const;
	int NumGeometryRecords( void ) const;
	int NumInstanceRecords( void ) const;
	const scenePacket_t &Scene( int index ) const;
	const passPacket_t &Pass( int index ) const;
	const drawPacket_t &DrawPacket( int index ) const;
	const materialResourceRecord_t &MaterialRecord( int index ) const;
	const geometryResourceRecord_t &GeometryRecord( int index ) const;
	const instanceRecord_t &InstanceRecord( int index ) const;
	int NumSubviewCaptures( void ) const;
	const sceneSubviewCapture_t &SubviewCapture( int index ) const;
	const scenePacketFrameStats_t &Stats( void ) const;
	bool ValidateSortKeys( void ) const;
	bool BuildTemporalViewMotionPolicy( const viewDef_t *viewDef,
		unsigned int backendExactMotionDomainMask,
		temporalViewMotionPolicy_t &policy, bool perPixelOwnership = false ) const;

private:
	int FindOrAddMaterialRecord( const drawSurf_t *drawSurf );
	int FindOrAddGeometryRecord( const drawSurf_t *drawSurf,
		const classicDeformRecord_t &classicDeform );
	int FindOrAddInstanceRecord( const drawSurf_t *drawSurf, scenePacketCategory_t packetCategory );
	void SetOverflow( scenePacketOverflowCause_t cause );
	void CountCategory( scenePacketCategory_t category );

	scenePacket_t			scenes[SCENE_PACKET_MAX_SCENES];
	passPacket_t			passes[SCENE_PACKET_MAX_PASSES];
	drawPacket_t			drawPackets[SCENE_PACKET_MAX_DRAWS];
	materialResourceRecord_t materialRecords[SCENE_PACKET_MAX_MATERIAL_RECORDS];
	geometryResourceRecord_t geometryRecords[SCENE_PACKET_MAX_GEOMETRY_RECORDS];
	instanceRecord_t		instanceRecords[SCENE_PACKET_MAX_INSTANCE_RECORDS];
	sceneSubviewCapture_t	subviewCaptures[SCENE_PACKET_MAX_SUBVIEW_CAPTURES];
	scenePacketFrameStats_t	stats;
	int						activeScene;
	int						activePass;
	unsigned long long		activePassLastSortKey;
	bool					activePassSortKeyValid;
};

const char *RenderPassCategory_Name( renderPassCategory_t category );
const char *ScenePacketCategory_Name( scenePacketCategory_t category );
const char *ScenePacketOverflowCause_Name( scenePacketOverflowCause_t cause );
const char *RendererMaterialClass_Name( rendererMaterialClass_t materialClass );
const char *GeometryResourceFallbackReason_Name( geometryResourceFallbackReason_t reason );
void R_ScenePackets_BeginFrame( void );
void R_ScenePackets_EndFrame( void );
void R_ScenePackets_SealFrame( void );
bool R_ScenePackets_FrontEndCaptureRequired( void );
bool R_ScenePackets_SidePipelineRequired( void );
void R_ScenePackets_AddRenderView( const viewDef_t *viewDef );
void R_ScenePackets_AddSpecialEffects( const viewDef_t *viewDef,
	int specialEffectsMask );
void R_ScenePackets_AddRenderTargetOp( void );
void R_ScenePackets_AddCopyRender( idImage *image, int x, int y,
	int width, int height, int cubeFace, bool copyDepth );
void R_ScenePackets_AddPresent( void );
void R_ScenePackets_AddCommandOnly( void );
const idScenePacketFrame &R_ScenePackets_FrontEndFrame( void );
bool R_ScenePackets_FrontEndFrameAvailable( void );
// perPixelOwnership: the backend's velocity pass wrote every visible moving
// surface either as exact velocity or into its per-pixel reactive channel, so
// the policy keeps only the whole-view overflow rule and adds no regions.
bool R_ScenePackets_BuildTemporalViewMotionPolicy( const viewDef_t *viewDef,
	unsigned int backendExactMotionDomainMask,
	temporalViewMotionPolicy_t &policy, bool perPixelOwnership = false );
bool R_ScenePackets_TemporalRigidMotionEligible( const drawSurf_t *drawSurf );

// How a backend velocity pass treats one view drawSurf. NONE surfaces are
// owned by camera/depth reprojection (static world, invisible); RIGID needs
// the entity's previous model matrix; PREVIOUS_POSITIONS also binds
// drawSurf->previousPositionCache as the previous model-space position of
// each vertex; REACTIVE surfaces mark their covered pixels in the velocity
// target's reactive channel instead of claiming a vector.
typedef enum {
	TEMPORAL_SURFACE_MOTION_NONE = 0,
	TEMPORAL_SURFACE_MOTION_RIGID,
	TEMPORAL_SURFACE_MOTION_PREVIOUS_POSITIONS,
	TEMPORAL_SURFACE_MOTION_REACTIVE
} temporalSurfaceMotion_t;

temporalSurfaceMotion_t R_ScenePackets_TemporalSurfaceMotion( const drawSurf_t *drawSurf );
// Reactive strength [0,1] written for a REACTIVE surface.
float R_ScenePackets_TemporalSurfaceReactiveStrength( const drawSurf_t *drawSurf );
void R_ScenePackets_BuildLegacyCommandStream( const emptyCommand_t *cmds, idScenePacketFrame &packetFrame );
// True when a view of the command stream draws a PBR-authored material.
bool R_ScenePackets_CommandStreamHasPBR( const emptyCommand_t *cmds );
void R_ScenePackets_LogIfVerbose( const idScenePacketFrame &packetFrame );
bool RendererScenePacket_RunSelfTest( void );

#endif /* !__SCENE_PACKETS_H__ */
