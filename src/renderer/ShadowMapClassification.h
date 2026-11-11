// Copyright (C) 2004 Id Software, Inc.
//

#ifndef __SHADOWMAP_CLASSIFICATION_H__
#define __SHADOWMAP_CLASSIFICATION_H__

typedef struct viewLight_s viewLight_t;
typedef struct renderLight_s renderLight_t;

static const int SHADOWMAP_CLASSIFICATION_MAX_CASCADES = 4;

typedef enum {
	SHADOWMAP_LIGHT_PROJECTED = 0,
	SHADOWMAP_LIGHT_POINT,
	SHADOWMAP_LIGHT_PARALLEL,
	SHADOWMAP_LIGHT_GLOBAL
} shadowMapLightClass_t;

typedef struct shadowMapLightClassification_s {
	shadowMapLightClass_t	lightClass;
	bool					projectedLight;
	bool					pointLight;
	bool					distantPointLight;
	bool					ordinaryProjectedLight;
	bool					parallelLight;
	bool					globalLight;
	bool					projectedCSMGateApplies;
	bool					projectedCSMEnabled;
	bool					csmEnabled;
	int						cascadeCount;
	int						atlasDiv;
	int						tileCount;
} shadowMapLightClassification_t;

// An off-centre point source whose complete radius box lies in front of it
// can use a fitted perspective map instead of spending six faces on empty sky.
bool R_ShadowMapUsesDistantPointProjection( const renderLight_t &parms );

// Receiver filtering is expressed in shadow texels, but a texel from a
// distant-source projection covers substantially more world space than one
// from a local projector. Keep the source-aware policy in one place so
// the cascade fitter, legacy receiver, and modern descriptor agree.
typedef struct shadowMapProjectedFilterSettings_s {
	bool					distantSource;
	float				filterScale;
	float				filterRadius;
	int					filterTaps;
	int					filterMode;
	float				pcssLightRadius;
	float				pcssMaxRadius;
	float				effectiveFilterRadius;
} shadowMapProjectedFilterSettings_t;

// Point receivers express depth bias in normalized radial depth and normal
// offset in cube texels.  On very large authored lights both therefore grow
// into a large world-space displacement.  Keep their common CPU policy in one
// place so classic/shared GL and Vulkan upload identical effective values.
static const float SHADOWMAP_POINT_RECEIVER_MAX_SLOPE = 4.0f;

typedef struct shadowMapPointReceiverSettings_s {
	float				constantBias;
	float				normalBias;
	float				texelBiasScale;
	float				normalOffsetScale;
	float				worldBiasScale;
} shadowMapPointReceiverSettings_t;

shadowMapLightClassification_t R_ClassifyShadowMapLight( const viewLight_t *vLight );
// Preserve projection coefficients exactly: quantizing to world-coordinate
// precision can erase entire rows of a large light's projection.
int R_ShadowMapHashFloat( int hash, float value );
shadowMapProjectedFilterSettings_t R_ShadowMapProjectedFilterSettings( const viewLight_t *vLight );
float R_ShadowMapPointFarDistance( const viewLight_t *vLight );
shadowMapPointReceiverSettings_t R_ClampShadowMapPointReceiverSettings(
	float farDistance, int faceSize, float constantBias, float normalBias,
	float texelBiasScale, float normalOffsetScale, float maxWorldBias );
shadowMapPointReceiverSettings_t R_ShadowMapPointReceiverSettings(
	float farDistance, int faceSize );
shadowMapPointReceiverSettings_t R_ShadowMapPointStorageAdjustedReceiverSettings(
	const shadowMapPointReceiverSettings_t &baseSettings,
	float farDistance, int faceSize, bool depthCompare, bool highPrecision );
// Mirrored or invalid model transforms make authored front-face winding
// unreliable. Invalid/unknown inputs return true so AUTO culling can fall
// back conservatively to two-sided depth.
bool R_ShadowMapCasterTransformNeedsTwoSided( const float modelMatrix[ 16 ] );
// A sealed hull is only safe for one-sided near-shell rendering when its
// light is outside the caster. Invalid/unknown inputs return true so AUTO
// culling fails conservatively to two-sided depth.
bool R_ShadowMapLightOriginInsideCasterBounds( const viewLight_t *vLight,
	const float modelMatrix[ 16 ], const float boundsMin[ 3 ],
	const float boundsMax[ 3 ] );
// Point lights are the dominant shadowing class in Quake 4: ten thousand
// authored against five hundred projectors, forty in one dense stock view.
// The static cube cache is therefore sized for a whole view and bounded by
// memory, not by a fixed handful of slots: a light without a slot re-renders
// six faces every view. Both backends keep this many slot records.
static const int SHADOWMAP_POINT_CACHE_MAX_SLOTS = 96;
// Slots that are always available, so a large cube size cannot starve the
// cache below what earlier builds offered.
static const int SHADOWMAP_POINT_CACHE_MIN_BUDGET_SLOTS = 12;
// Usable point cache slots for a cube of faceSize texels whose six faces
// cost bytesPerTexel each: r_shadowMapPointCacheSize, bounded by
// r_shadowMapPointCacheMegs and by maxSlots.
int R_ShadowMapPointCacheSlotLimit( int faceSize, int bytesPerTexel,
	int maxSlots );
// The pure form, for the self-test.
int R_ShadowMapPointCacheSlotLimitFor( int requestedSlots, int budgetMegs,
	int faceSize, int bytesPerTexel, int maxSlots );
// A caster is live (drawn every view over the cached static depth) while its
// entity changed within about a quarter of a second, counted in rendered
// frames. One frame is not enough: the game updates an entity once a tic, so
// at twice the tic rate and above a walking character read as static on the
// frames between tics, joined the static signature and forced the cached map
// to be re-rendered every tic.
static const int SHADOWMAP_DYNAMIC_CASTER_WINDOW_MSEC = 250;
static const int SHADOWMAP_DYNAMIC_CASTER_MIN_FRAMES = 2;
static const int SHADOWMAP_DYNAMIC_CASTER_MAX_FRAMES = 500;
// Called once per rendered frame with a millisecond clock.
void R_ShadowMapBeginFrame( int nowMsec );
int R_ShadowMapDynamicCasterFrames( void );
// The light's shape changed within the last two game tics' worth of frames:
// it is moving, and every view of it needs a new map.
int R_ShadowMapMovingLightFramesFor( float frameMsec );
bool R_ShadowMapLightIsMoving( const idRenderLightLocal *lightDef, int frame );
// A moving light draws its maps directly. Through the static cache each view
// published a map that the next view could not use, copied it and drew the
// live casters over the copy.
bool R_ShadowMapMovingLightSkipsCache( const viewLight_t *vLight, int frame );
// The pure form, for the self-test: the window in frames for a smoothed frame
// interval in milliseconds.
int R_ShadowMapDynamicCasterFramesFor( float frameMsec );
// The projected atlas is shared by every projected, parallel and distant point
// light of a view, and each of them asks for one block per receiver ownership.
// Blocks are placed as power-of-two squares inside the largest power-of-two
// square the atlas holds, so a set of them fits exactly when their areas do,
// in whatever order they are asked for.
//
// The edge a block of atlasDiv tiles a side occupies, and the edge of the
// square the blocks of an atlas are cut from.
int R_ShadowMapAtlasBlockEdge( int tileSize, int atlasDiv );
int R_ShadowMapAtlasRootEdge( int atlasSize );
// How many such blocks the atlas holds.
int R_ShadowMapAtlasBlockCapacity( int atlasSize, int tileSize, int atlasDiv );

// One atlas light of a view. A light keeps the tile it would have alone
// (r_shadowMapSize) while the view fits. When it does not, lights give up
// resolution one halving at a time, those covering the least world first,
// because a small light spends the same texels on less surface. No light goes
// below minTileSize.
typedef struct shadowMapAtlasRequest_s {
	int		tileSize;		// in: the tile edge the light has alone; out: its edge in this view
	int		minTileSize;	// the smallest edge it may be given
	int		atlasDiv;		// 1, or 2 for the four-tile block of a cascaded light
	int		passes;			// receiver ownerships that need a block of their own, 1 or 2
	float	extent;			// world size of what the light covers
} shadowMapAtlasRequest_t;

static const int SHADOWMAP_ATLAS_MAX_REQUESTS = 64;
// The smallest tile a light is cut down to: two halvings, never under 128.
int R_ShadowMapAtlasMinTileSize( int tileSize );
// Returns whether the requests fit the atlas once their tile sizes are set.
bool R_ShadowMapFitAtlasTiles( int atlasSize, shadowMapAtlasRequest_t *requests, int count );
// The tile edge a back end gives a light: the one assigned for this view
// (viewLight_t::shadowMapTileSize), else r_shadowMapSize, and never more than
// maxBlockSize allows. 0 when no tile of 128 fits.
int R_ShadowMapProjectedTileSize( const viewLight_t *vLight, int atlasDiv, int maxBlockSize );

// The static projected cache keeps the depth of a light's static casters; a
// pass without an entry draws them again every view. It is not bounded by a
// count of entries, because the tiles of a view are no longer one size:
// r_shadowMapProjectedCacheSize says how many full-size (r_shadowMapSize)
// maps' worth of tile texels may stay resident, and a map cut down for a
// crowded view takes its share of one. Eight hold eight full-size maps, as
// eight slots always did, or the thirty-two half-size ones of a crowded view.
//
// A map is counted by its tile, so a cascaded light's block of four tiles
// costs what its one slot cost: the memory ceiling is what it was, eight
// blocks of the largest kind at full size.
//
// Both back ends keep this many entry records. Only Vulkan needs the bound:
// its resident tiles are images of their own. OpenGL keeps them in the atlas,
// which is allocated whole and so is the bound there.
static const int SHADOWMAP_PROJECTED_CACHE_MAX_SLOTS = 64;
// Tile texels the cache may hold; 0 turns it off.
long long R_ShadowMapProjectedCacheTexelBudget( void );
// The pure form, for the self-test.
long long R_ShadowMapProjectedCacheTexelBudgetFor( int cacheSize, int tileSize );
// Whether a map with tiles of tileSize texels a side may be added to a cache
// that already holds residentTexels.
bool R_ShadowMapProjectedCacheAdmits( long long budgetTexels, long long residentTexels, int tileSize );

// A cutout (perforated) caster's coverage can change while its entity does
// not: through a stage condition, alpha reference, alpha scale or texture
// matrix that follows time or a parm, through a video or render-target image,
// or through a deform. Such a caster has to be drawn every view, over the
// cached static depth. Most cutouts of the stock world (grates, railings,
// fences) have none of those: the registers their alpha stages read are
// literals and those stages sample ordinary images. They are as static as their entity and
// belong in the cached depth, like any opaque caster. Treating every cutout
// as live made each light that reaches one copy and redraw its map every
// view for the same result: eleven of the fourteen point lights composed in
// convoy2's start view had no other live caster.
//
// False for a material that is not perforated.
bool R_ShadowMapCutoutCoverageAnimates( const idMaterial *shader );
// What the baked coverage of a static cutout depends on beyond its material:
// the storage of its alpha images and the hashed-alpha settings. Folded into
// the static caster signature so a change renders the map again.
int R_ShadowMapCutoutCoverageSignature( const idMaterial *shader, int hash );

// pins the cache-size, live-caster window and atlas policies
bool R_ShadowMapCachePolicySelfTest( void );
// Conservative cube-face rejection, shared by GL and Vulkan. Invalid bounds
// or transforms remain visible. Face order is +X,-X,+Y,-Y,+Z,-Z.
bool R_ShadowMapCasterOutsidePointFace( const viewLight_t *vLight,
	const float modelMatrix[ 16 ], const float boundsMin[ 3 ],
	const float boundsMax[ 3 ], int cubeFace );
const char *R_ShadowMapLightClassName( shadowMapLightClass_t lightClass );

#endif /* !__SHADOWMAP_CLASSIFICATION_H__ */
