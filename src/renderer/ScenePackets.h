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

typedef struct drawPacketSortKey_s {
	unsigned long long	value;
} drawPacketSortKey_t;

typedef struct drawPacket_s {
	const drawSurf_t			*legacyDrawSurf;
	const viewDef_t			*viewDef;
	const viewEntity_t		*space;
	const materialResourceRecord_t *materialRecord;
	drawPacketSortKey_t		sortKey;
	renderPassCategory_t	passCategory;
	float					legacySort;
	int						materialRecordIndex;
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
	int						firstDrawPacket;
	int						drawPacketCount;
	bool					enabled;
	bool					commandOnly;
} passPacket_t;

typedef struct scenePacket_s {
	const viewDef_t			*viewDef;
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
	int						drawPacketsWithMaterial;
	int						drawPacketsWithResourceRecord;
	int						drawPacketsWithGeometry;
	int						drawPacketsWithShaderRegisters;
	int						drawPacketsWithIndexCache;
	int						drawPacketsWithAmbientCache;
	bool					frontEndDerived;
	bool					backendDerived;
	bool					overflow;
} scenePacketFrameStats_t;

class idScenePacketFrame {
public:
	idScenePacketFrame();
	void Clear( void );

	bool AddScene( const viewDef_t *viewDef, bool legacyBridge );
	bool AddPass( renderPassCategory_t category, bool enabled, bool commandOnly = false );
	bool AddDrawPacket( const drawSurf_t *drawSurf, renderPassCategory_t category, int drawIndex );
	void FinishScene( void );
	void AddCommandPacket( void );
	void AddLegacyDrawView( void );
	void AddClippedDrawPackets( int count );
	void MarkFrontEndDerived( void );
	void MarkBackendDerived( void );

	int NumScenes( void ) const;
	int NumPasses( void ) const;
	int NumDrawPackets( void ) const;
	int NumMaterialRecords( void ) const;
	const scenePacket_t &Scene( int index ) const;
	const passPacket_t &Pass( int index ) const;
	const drawPacket_t &DrawPacket( int index ) const;
	const materialResourceRecord_t &MaterialRecord( int index ) const;
	const scenePacketFrameStats_t &Stats( void ) const;

private:
	int FindOrAddMaterialRecord( const drawSurf_t *drawSurf );

	scenePacket_t			scenes[SCENE_PACKET_MAX_SCENES];
	passPacket_t			passes[SCENE_PACKET_MAX_PASSES];
	drawPacket_t			drawPackets[SCENE_PACKET_MAX_DRAWS];
	materialResourceRecord_t materialRecords[SCENE_PACKET_MAX_MATERIAL_RECORDS];
	scenePacketFrameStats_t	stats;
	int						activeScene;
	int						activePass;
};

const char *RenderPassCategory_Name( renderPassCategory_t category );
const char *RendererMaterialClass_Name( rendererMaterialClass_t materialClass );
void R_ScenePackets_BeginFrame( void );
void R_ScenePackets_EndFrame( void );
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
