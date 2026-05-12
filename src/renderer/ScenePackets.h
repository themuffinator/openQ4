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
	int					diffuseImage;
	int					normalImage;
	int					specularImage;
	int					resourceTableIndex;
	rendererPermutationKey_t permutation;
} materialResourceRecord_t;

typedef struct drawPacketSortKey_s {
	unsigned long long	value;
} drawPacketSortKey_t;

typedef struct drawPacket_s {
	const drawSurf_t			*legacyDrawSurf;
	const viewEntity_t		*space;
	const materialResourceRecord_t *materialRecord;
	drawPacketSortKey_t		sortKey;
	renderPassCategory_t	passCategory;
	int						firstIndex;
	int						indexCount;
	int						vertexOffset;
	int						instanceOffset;
	int						instanceCount;
} drawPacket_t;

typedef struct passPacket_s {
	renderPassCategory_t	passCategory;
	int						firstDrawPacket;
	int						drawPacketCount;
	bool					enabled;
} passPacket_t;

typedef struct scenePacket_s {
	const viewDef_t			*viewDef;
	int						firstPassPacket;
	int						passPacketCount;
	int						firstDrawPacket;
	int						drawPacketCount;
	bool					legacyBridge;
} scenePacket_t;

const char *RenderPassCategory_Name( renderPassCategory_t category );

#endif /* !__SCENE_PACKETS_H__ */
