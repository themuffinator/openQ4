// Copyright (C) 2004 Id Software, Inc.
//

#ifndef __SCENE_PACKETS_H__
#define __SCENE_PACKETS_H__

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
	RENDER_PASS_LENS_FLARE,
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
	SCENE_PACKET_OVERFLOW_INSTANCE_RECORDS
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
	int					resourceTableIndex;
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
	geometryResourceFallbackReason_t fallbackReason;
	unsigned int			fallbackFlags;
	int						skinningPaletteOffset;
	int						skinningPaletteCount;
	const glIndex_t			*legacyIndexData;
	bool					hasAmbientVertexBuffer;
	bool					hasIndexBuffer;
	bool					hasClientIndexData;
	bool					hasPrimBatchMesh;
	bool					hasBounds;
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
} instanceRecord_t;

typedef struct drawPacketSortKey_s {
	unsigned long long	value;
} drawPacketSortKey_t;

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
	int						scissorX1;
	int						scissorY1;
	int						scissorX2;
	int						scissorY2;
	bool					hasGeometry;
	bool					hasShaderRegisters;
	bool					hasIndexCache;
	bool					hasAmbientCache;
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
} scenePacket_t;

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
	int						sortKeyValidationFailures;
	bool					frontEndDerived;
	bool					backendDerived;
	bool					overflow;
	scenePacketOverflowCause_t overflowCause;
} scenePacketFrameStats_t;

class idScenePacketFrame {
public:
	idScenePacketFrame();
	void Clear( void );

	bool AddScene( const viewDef_t *viewDef, bool legacyBridge );
	bool AddPass( renderPassCategory_t category, bool enabled, bool commandOnly = false );
	bool AddDrawPacket( const drawSurf_t *drawSurf, renderPassCategory_t category, int drawIndex );
	void FinishScene( void );
	void AddCommandPacket( scenePacketCategory_t category = SCENE_PACKET_CATEGORY_COMMAND );
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
	const scenePacketFrameStats_t &Stats( void ) const;
	bool ValidateSortKeys( void ) const;

private:
	int FindOrAddMaterialRecord( const drawSurf_t *drawSurf );
	int FindOrAddGeometryRecord( const drawSurf_t *drawSurf );
	int FindOrAddInstanceRecord( const drawSurf_t *drawSurf, scenePacketCategory_t packetCategory );
	void SetOverflow( scenePacketOverflowCause_t cause );
	void CountCategory( scenePacketCategory_t category );

	scenePacket_t			scenes[SCENE_PACKET_MAX_SCENES];
	passPacket_t			passes[SCENE_PACKET_MAX_PASSES];
	drawPacket_t			drawPackets[SCENE_PACKET_MAX_DRAWS];
	materialResourceRecord_t materialRecords[SCENE_PACKET_MAX_MATERIAL_RECORDS];
	geometryResourceRecord_t geometryRecords[SCENE_PACKET_MAX_GEOMETRY_RECORDS];
	instanceRecord_t		instanceRecords[SCENE_PACKET_MAX_INSTANCE_RECORDS];
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
bool R_ScenePackets_FrontEndCaptureRequired( void );
bool R_ScenePackets_SidePipelineRequired( void );
void R_ScenePackets_AddRenderView( const viewDef_t *viewDef );
void R_ScenePackets_AddSpecialEffects( const viewDef_t *viewDef );
void R_ScenePackets_AddRenderTargetOp( void );
void R_ScenePackets_AddCopyRender( void );
void R_ScenePackets_AddPresent( void );
void R_ScenePackets_AddCommandOnly( void );
const idScenePacketFrame &R_ScenePackets_FrontEndFrame( void );
bool R_ScenePackets_FrontEndFrameAvailable( void );
void R_ScenePackets_BuildLegacyCommandStream( const emptyCommand_t *cmds, idScenePacketFrame &packetFrame );
void R_ScenePackets_LogIfVerbose( const idScenePacketFrame &packetFrame );
bool RendererScenePacket_RunSelfTest( void );

#endif /* !__SCENE_PACKETS_H__ */
