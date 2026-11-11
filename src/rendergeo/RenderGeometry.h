// Copyright (C) 2026 DarkMatter Productions
//

#ifndef __RENDERGEOMETRY_H__
#define __RENDERGEOMETRY_H__

/*
===============================================================================

	CPU render-geometry library shared between the engine and the renderer.

	Everything in src/rendergeo is CPU-side geometry work with no GPU or
	renderer-global dependencies: triangle-surface allocation and cleanup,
	silhouette/tangent derivation, light-volume derivation, polytope
	generation, interaction facing/culling, and stencil shadow-volume
	construction. It builds as the openq4_render_geo static library so the
	dmap compiler (engine-side) and the renderer (module-side after Phase B8)
	share one implementation
	(docs/dev/archive/2026-10-18/plans/2026-07-16-vulkan-renderer-phase-b.md, B6).

	Renderer-owned behavior crosses the boundary through renderGeoHooks_t:
	the zeroed/default hooks give the dmap-correct behavior (sil remapping
	on, shadows on, immediate frees, no GPU caches, no turbo-shadow path);
	the renderer installs live hooks at init. dmap additionally binds the
	offline shadow-optimizer callbacks, which only the SG_OFFLINE path uses.

	This header expects the idlib precompiled world (Model.h, RenderWorld.h)
	like every renderer header.

===============================================================================
*/

class idRenderWorldLocal;
class idInteraction;
class idRenderModelDecal;
class idRenderModelOverlay;
class idImage;
struct viewLight_s;
struct viewEntity_s;
struct doublePortal_s;
struct portalArea_s;

/*
===============================================================================
	Interaction culling info (relocated from Interaction.h)
===============================================================================
*/

#define LIGHT_TRIS_DEFERRED			reinterpret_cast< srfTriangles_t * >( static_cast<uintptr_t>( -1 ) )
#define LIGHT_CULL_ALL_FRONT		reinterpret_cast< byte * >( static_cast<uintptr_t>( -1 ) )
#define	LIGHT_CLIP_EPSILON			0.1f

typedef struct {
	// For each triangle a byte set to 1 if facing the light origin.
	byte *					facing;

	// For each vertex a byte with the bits [0-5] set if the
	// vertex is at the back side of the corresponding clip plane.
	// If the 'cullBits' pointer equals LIGHT_CULL_ALL_FRONT all
	// vertices are at the front of all the clip planes.
	byte *					cullBits;

	// Clip planes in surface space used to calculate the cull bits.
	idPlane					localClipPlanes[6];
} srfCullInfo_t;

/*
===============================================================================
	Light / entity definitions (relocated from tr_local.h)
===============================================================================
*/

typedef struct {
	int		numPlanes;		// this is always 6 for now
	idPlane	planes[6];
	// positive sides facing inward
	// plane 5 is always the plane the projection is going to, the
	// other planes are just clip planes
	// all planes are in global coordinates

	bool	makeClippedPlanes;
	// a projected light with a single frustum needs to make sil planes
	// from triangles that clip against side planes, but a point light
	// that has adjacent frustums doesn't need to
} shadowFrustum_t;


// areas have references to hold all the lights and entities in them
typedef struct areaReference_s {
	struct areaReference_s *areaNext;				// chain in the area
	struct areaReference_s *areaPrev;
	struct areaReference_s *ownerNext;				// chain on either the entityDef or lightDef
	class idRenderEntityLocal *	entity;				// only one of entity / light will be non-NULL
	class idRenderLightLocal *	light;				// only one of entity / light will be non-NULL
	struct portalArea_s	*	area;					// so owners can find all the areas they are in
} areaReference_t;


// idRenderLight should become the new public interface replacing the qhandle_t to light defs in the idRenderWorld interface
class idRenderLight {
public:
	virtual					~idRenderLight() {}

	virtual void			FreeRenderLight() = 0;
	virtual void			UpdateRenderLight( const renderLight_t *re, bool forceUpdate = false ) = 0;
	virtual void			GetRenderLight( renderLight_t *re ) = 0;
	virtual void			ForceUpdate() = 0;
	virtual int				GetIndex() = 0;
};

ID_INLINE bool R_IsInvalidPrelightModelPointer( const idRenderModel *model ) {
	return reinterpret_cast<uintptr_t>( model ) == static_cast<uintptr_t>( -1 );
}

ID_INLINE idRenderModel *R_SanitizePrelightModelPointer( idRenderModel *model ) {
	return R_IsInvalidPrelightModelPointer( model ) ? NULL : model;
}

ID_INLINE bool R_LightHasRealPrelightModel( const renderLight_t &parms ) {
	return R_SanitizePrelightModelPointer( parms.prelightModel ) != NULL;
}

// idRenderEntity should become the new public interface replacing the qhandle_t to entity defs in the idRenderWorld interface
class idRenderEntity {
public:
	virtual					~idRenderEntity() {}

	virtual void			FreeRenderEntity() = 0;
	virtual void			UpdateRenderEntity( const renderEntity_t *re, bool forceUpdate = false ) = 0;
	virtual void			GetRenderEntity( renderEntity_t *re ) = 0;
	virtual void			ForceUpdate() = 0;
	virtual int				GetIndex() = 0;

	// overlays are extra polygons that deform with animating models for blood and damage marks
	virtual void			ProjectOverlay( const idPlane localTextureAxis[2], const idMaterial *material ) = 0;
	virtual void			RemoveDecals() = 0;
};


class idRenderLightLocal : public idRenderLight {
public:
							idRenderLightLocal();

	virtual void			FreeRenderLight();
	virtual void			UpdateRenderLight( const renderLight_t *re, bool forceUpdate = false );
	virtual void			GetRenderLight( renderLight_t *re );
	virtual void			ForceUpdate();
	virtual int				GetIndex();

	renderLight_t			parms;					// specification

	int						lastMovedFrameNum;		// the frame its shape last changed (origin, axes, radius,
													// projection): lastModifiedFrameNum also follows colour
	bool					lightHasMoved;			// the light has changed its position since it was
													// first added, so the prelight model is not valid

	bool					shadowMapStencilFallbackSticky;	// the backend hit a shadow-map render failure on this
													// light; keep generating stencil volumes for its fallback
	int						shadowMapStencilFallbackFrame;	// the frame something last asked for that fallback
	int						shadowMapStencilFallbackCleanViews;	// views the light has been through since

	float					modelMatrix[16];		// this is just a rearrangement of parms.axis and parms.origin

	idRenderWorldLocal *	world;
	int						index;					// in world lightdefs

	int						areaNum;				// if not -1, we may be able to cull all the light's
													// interactions if !viewDef->connectedAreas[areaNum]

	int					lastModifiedFrameNum;	// to determine if it is constantly changing,
													// and should go in the dynamic frame memory, or kept
													// in the cached memory
	int					referencedFrameNum;	// last frame where a viewLight captured this light
	bool					archived;				// for demo writing


	// derived information
	idPlane					lightProject[4];

	const idMaterial *		lightShader;			// guaranteed to be valid, even if parms.shader isn't
	idImage *				falloffImage;

	idVec3					globalLightOrigin;		// accounting for lightCenter and parallel


	idPlane					frustum[6];				// in global space, positive side facing out, last two are front/back
	idWinding *				frustumWindings[6];		// used for culling
	srfTriangles_t *		frustumTris;			// triangulated frustumWindings[]

	int						numShadowFrustums;		// one for projected lights, usually six for point lights
	shadowFrustum_t			shadowFrustums[6];

	int						viewCount;				// if == tr.viewCount, the light is on the viewDef->viewLights list
	struct viewLight_s *	viewLight;

	areaReference_t *		references;				// each area the light is present in will have a lightRef
	idInteraction *			firstInteraction;		// doubly linked list
	idInteraction *			lastInteraction;

	struct doublePortal_s *	foggedPortals;
};


class idRenderEntityLocal : public idRenderEntity {
public:
							idRenderEntityLocal();

	virtual void			FreeRenderEntity();
	virtual void			UpdateRenderEntity( const renderEntity_t *re, bool forceUpdate = false );
	virtual void			GetRenderEntity( renderEntity_t *re );
	virtual void			ForceUpdate();
	virtual int				GetIndex();

	// overlays are extra polygons that deform with animating models for blood and damage marks
	virtual void			ProjectOverlay( const idPlane localTextureAxis[2], const idMaterial *material );
	virtual void			RemoveDecals();

	renderEntity_t			parms;
	struct renderView_s *	demoRemoteRenderView;	// owned storage for render-demo remote views

	float					modelMatrix[16];		// this is just a rearrangement of parms.axis and parms.origin

	idRenderWorldLocal *	world;
	int						index;					// in world entityDefs

	int						lastModifiedFrameNum;	// to determine if it is constantly changing,
													// and should go in the dynamic frame memory, or kept
													// in the cached memory
	bool					archived;				// for demo writing
	bool					hasDemoRemoteRenderView;

	idRenderModel *			dynamicModel;			// if parms.model->IsDynamicModel(), this is the generated data
	int						dynamicModelFrameCount;	// continuously animating dynamic models will recreate
													// dynamicModel if this doesn't == tr.viewCount
	int						dynamicModelRegenFrame;	// the frame a snapshot last replaced an earlier one.
													// That is a model changing; its first snapshot is not
	idRenderModel *			cachedDynamicModel;
	idRenderModel *			dynamicCollisionModel;	// collision-only dynamic snapshot for traces / sil-trace parity
	idRenderModel *			cachedDynamicCollisionModel;

	// repeated-state reuse: content hash of parms.joints when the dynamic
	// snapshot was generated, so transform-only entity updates (interpolated
	// presentation frames, movers) can keep the model-space skinned snapshot
	unsigned long long		dynamicModelJointsHash;
	bool					dynamicModelJointsHashValid;

	idBounds				referenceBounds;		// the local bounds used to place entityRefs, either from parms or a model

	// a viewEntity_t is created whenever a idRenderEntityLocal is considered for inclusion
	// in a given view, even if it turns out to not be visible
	int						viewCount;				// if tr.viewCount == viewCount, viewEntity is valid,
													// but the entity may still be off screen
	struct viewEntity_s *	viewEntity;				// in frame temporary memory

	int						visibleCount;
	// if tr.viewCount == visibleCount, at least one ambient
	// surface has actually been added by R_AddAmbientDrawsurfs
	// note that an entity could still be in the view frustum and not be visible due
	// to portal passing
	int						LODModificationFrame;	// retail shadow LOD frame-hold gate for interaction shadows

	idRenderModelDecal *	decals;					// chain of decals that have been projected on this model
	idRenderModelOverlay *	overlay;				// blood overlays on animated models

	areaReference_t *		entityRefs;				// chain of all references
	idInteraction *			firstInteraction;		// doubly linked list
	idInteraction *			lastInteraction;

	bool					needsPortalSky;

	// Vulkan shadow-map admission (R_ShadowMapReceiversAllowMaps): whether a
	// surface of the model this entity last drew uses authored GLSL lighting,
	// which only a light's stencil chain can shadow. An animating entity's
	// interactions are freed on every update, so the verdict is kept here,
	// with the model instance and the model, skin and custom shader it was
	// taken from. A generation of -1 means none was taken yet.
	int						stencilReceiverGeneration;
	bool					stencilReceiverNeeded;
	const idRenderModel *	stencilReceiverInstance;
	int						stencilReceiverInstanceFrame;
	const idRenderModel *	stencilReceiverModel;
	const idDeclSkin *		stencilReceiverSkin;
	const idMaterial *		stencilReceiverShader;
};

/*
===============================================================================
	Shadow volume generation (relocated from tr_local.h)
===============================================================================
*/

typedef enum {
	SG_DYNAMIC,		// use infinite projections
	SG_STATIC,		// clip to bounds
	SG_OFFLINE		// perform very time consuming optimizations
} shadowGen_t;

typedef struct {
	idVec3	*verts;			// includes both front and back projections, caller should free
	int		numVerts;
	glIndex_t	*indexes;	// caller should free

	// indexes must be sorted frontCap, rearCap, silPlanes so the caps can be removed
	// when the viewer is in a position that they don't need to see them
	int		numFrontCapIndexes;
	int		numRearCapIndexes;
	int		numSilPlaneIndexes;
	int		totalIndexes;
} optimizedShadow_t;

// dmap-time shadow optimization, implemented by the dmap compiler
// (tools/compilers/dmap/shadowopt3.cpp) and reached through the hooks below
optimizedShadow_t SuperOptimizeOccluders( idVec4 *verts, glIndex_t *indexes, int numIndexes,
										 idPlane projectionPlane, idVec3 projectionOrigin );
void CleanupOptimizedShadowTris( srfTriangles_t *tri );

/*
===============================================================================
	Deformable-mesh precalculation (relocated from tr_local.h)
===============================================================================
*/

typedef struct deformInfo_s {
	int				numSourceVerts;

	// numOutputVerts may be smaller if the input had duplicated or degenerate triangles
	// it will often be larger if the input had mirrored texture seams that needed
	// to be busted for proper tangent spaces
	int				numOutputVerts;

	int				numMirroredVerts;
	int *			mirroredVerts;

	int				numIndexes;
	glIndex_t *		indexes;

	glIndex_t *		silIndexes;

	int				numDupVerts;
	int *			dupVerts;

	int				numSilEdges;
	silEdge_t *		silEdges;

	dominantTri_t *	dominantTris;
} deformInfo_t;

/*
===============================================================================
	Renderer-owned behavior hooks

	The zeroed defaults are the dmap-correct behavior: silhouette remapping
	on, shadows on, no warning spam, no counters, no GPU vertex caches,
	immediate triangle frees, no turbo-shadow path, offline optimizer unset.
	The renderer installs live hooks at init; dmap binds only the offline
	shadow-optimizer pair.
===============================================================================
*/

typedef struct renderGeoHooks_s {
	// cvar-gated behavior; NULL means the default in parentheses
	bool	( *useSilRemap )( void );				// (true)
	bool	( *reportSilEdgeWarnings )( void );		// (false)
	bool	( *shadowsEnabled )( void );			// (true)

	// performance counters; NULL means no counting
	void	( *countTangentIndexes )( int amount );
	void	( *countShadowVolume )( void );

	// GPU vertex-cache release for a triangle surface; NULL means no caches
	void	( *freeVertexCaches )( srfTriangles_t *tri );

	// deferred free: return true when the surface was queued for end-of-frame
	// deletion; NULL or false means free immediately
	bool	( *deferFree )( srfTriangles_t *tri );

	// SG_DYNAMIC turbo-shadow path (renderer-only); returns true when the
	// turbo path handled the surface (writing *out, possibly NULL on
	// overflow) and false to fall through to static generation. NULL in
	// dmap, which never requests SG_DYNAMIC.
	bool	( *tryTurboShadow )( const idRenderEntityLocal *ent,
								 const srfTriangles_t *tri, const idRenderLightLocal *light,
								 srfCullInfo_t &cullInfo, srfTriangles_t **out );

	// offline shadow optimization (dmap-only, SG_OFFLINE)
	optimizedShadow_t ( *superOptimize )( idVec4 *verts, glIndex_t *indexes, int numIndexes,
										  idPlane projectionPlane, idVec3 projectionOrigin );
	void	( *cleanupOptimizedShadowTris )( srfTriangles_t *tri );
} renderGeoHooks_t;

// replaces the whole hook set (renderer init)
void	RenderGeo_SetHooks( const renderGeoHooks_t &hooks );
// binds only the offline shadow-optimizer pair (dmap init)
void	RenderGeo_SetShadowOptimizer( optimizedShadow_t ( *superOptimize )( idVec4 *, glIndex_t *, int, idPlane, idVec3 ),
									  void ( *cleanupOptimizedShadowTris )( srfTriangles_t * ) );
const renderGeoHooks_t &RenderGeo_GetHooks( void );

/*
===============================================================================
	Library entry points
===============================================================================
*/

#define USE_TRI_DATA_ALLOCATOR

// triangle-surface allocation and processing (RenderGeometryTriSurf.cpp)
void				R_InitTriSurfData( void );
void				R_ShutdownTriSurfData( void );
void				R_ShowTriSurfMemory_f( const idCmdArgs &args );

srfTriangles_t *	R_AllocStaticTriSurf( void );
srfTriangles_t *	R_CopyStaticTriSurf( const srfTriangles_t *tri );
void				R_AllocStaticTriSurfVerts( srfTriangles_t *tri, int numVerts );
void				R_AllocStaticTriSurfIndexes( srfTriangles_t *tri, int numIndexes );
void				R_AllocStaticTriSurfShadowVerts( srfTriangles_t *tri, int numVerts );
#if defined( _MD5R_SUPPORT ) || defined( Q4SDK_MD5R )
void				R_AllocStaticTriSurfSilTraceVerts( srfTriangles_t *tri, int numVerts );
void				R_AllocStaticSkinToModelTransforms( srfTriangles_t *tri, int numTransforms );
#endif
bool				R_AllocStaticGpuSkinningJointPalette( srfTriangles_t *tri, int numJoints );
void				R_ClearStaticGpuSkinningJointPalette( srfTriangles_t *tri );
void				R_ReferenceStaticGpuSkinning( srfTriangles_t *tri, const srfTriangles_t *reference );
bool				R_CopyStaticGpuSkinning( srfTriangles_t *tri, const srfTriangles_t *source );
void				R_AllocStaticTriSurfPlanes( srfTriangles_t *tri, int numIndexes );
void				R_ResizeStaticTriSurfVerts( srfTriangles_t *tri, int numVerts );
void				R_ResizeStaticTriSurfIndexes( srfTriangles_t *tri, int numIndexes );
void				R_ResizeStaticTriSurfShadowVerts( srfTriangles_t *tri, int numVerts );
void				R_ReferenceStaticTriSurfVerts( srfTriangles_t *tri, const srfTriangles_t *reference );
void				R_ReferenceStaticTriSurfIndexes( srfTriangles_t *tri, const srfTriangles_t *reference );
void				R_FreeStaticTriSurfSilIndexes( srfTriangles_t *tri );
void				R_FreeStaticTriSurf( srfTriangles_t *tri );
void				R_FreeStaticTriSurfVertexCaches( srfTriangles_t *tri );
void				R_ReallyFreeStaticTriSurf( srfTriangles_t *tri );
void				RenderGeo_FreeEmptyBaseBlocks( void );
int					R_TriSurfMemory( const srfTriangles_t *tri );

void				R_BoundTriSurf( srfTriangles_t *tri );
void				R_RemoveDuplicatedTriangles( srfTriangles_t *tri );
void				R_CreateSilIndexes( srfTriangles_t *tri );
void				R_RemoveDegenerateTriangles( srfTriangles_t *tri );
void				R_RemoveUnusedVerts( srfTriangles_t *tri );
void				R_RangeCheckIndexes( const srfTriangles_t *tri );
void				R_CreateVertexNormals( srfTriangles_t *tri );
void				R_DeriveFacePlanes( srfTriangles_t *tri );
void				R_CleanupTriangles( srfTriangles_t *tri, bool createNormals, bool identifySilEdges, bool useUnsmoothedTangents );
void				R_ReverseTriangles( srfTriangles_t *tri );
srfTriangles_t *	R_MergeSurfaceList( const srfTriangles_t **surfaces, int numSurfaces );
srfTriangles_t *	R_MergeTriangles( const srfTriangles_t *tri1, const srfTriangles_t *tri2 );
void				R_DeriveTangents( srfTriangles_t *tri, bool allocFacePlanes = true );

deformInfo_t *		R_BuildDeformInfo( int numVerts, const idDrawVert *verts, int numIndexes, const int *indexes, bool useUnsmoothedTangents );
deformInfo_t *		R_AllocDeformInfo( int numSourceVerts, int numOutputVerts, int numIndexes, int numMirroredVerts, int numDupVerts, int numSilEdges, bool allocDominantTris );
void				R_FreeDeformInfo( deformInfo_t *deformInfo );
int					R_DeformInfoMemoryUsed( deformInfo_t *deformInfo );

// light-volume derivation (RenderGeometryLight.cpp)
void				R_SetLightProject( idPlane lightProject[4], const idVec3 origin, const idVec3 targetPoint,
									   const idVec3 rightVector, const idVec3 upVector, const idVec3 start, const idVec3 stop );
void				R_SetLightFrustum( const idPlane lightProject[4], idPlane frustum[6] );
void				R_DeriveLightData( idRenderLightLocal *light );
void				R_FreeLightDefFrustum( idRenderLightLocal *ldef );
void				R_RenderLightFrustum( const renderLight_t &renderLight, idPlane lightFrustum[6] );

// polytope generation (RenderGeometryPolytope.cpp)
srfTriangles_t *	R_PolytopeSurface( int numPlanes, const idPlane *planes, idWinding **windings );

// model/world space transforms (RenderGeometryLight.cpp)
void				R_AxisToModelMatrix( const idMat3 &axis, const idVec3 &origin, float modelMatrix[16] );
void				R_LocalPointToGlobal( const float modelMatrix[16], const idVec3 &in, idVec3 &out );
void				R_PointTimesMatrix( const float modelMatrix[16], const idVec4 &in, idVec4 &out );
void				R_GlobalPointToLocal( const float modelMatrix[16], const idVec3 &in, idVec3 &out );
void				R_LocalVectorToGlobal( const float modelMatrix[16], const idVec3 &in, idVec3 &out );
void				R_GlobalVectorToLocal( const float modelMatrix[16], const idVec3 &in, idVec3 &out );
void				R_GlobalPlaneToLocal( const float modelMatrix[16], const idPlane &in, idPlane &out );
void				R_LocalPlaneToGlobal( const float modelMatrix[16], const idPlane &in, idPlane &out );

// interaction facing/culling (RenderGeometryInteraction.cpp)
void				R_CalcInteractionFacing( const idRenderEntityLocal *ent, const srfTriangles_t *tri, const idRenderLightLocal *light, srfCullInfo_t &cullInfo );
void				R_CalcInteractionCullBits( const idRenderEntityLocal *ent, const srfTriangles_t *tri, const idRenderLightLocal *light, srfCullInfo_t &cullInfo );
void				R_FreeInteractionCullInfo( srfCullInfo_t &cullInfo );

// stencil shadow volumes (RenderGeometryShadow.cpp)
void				R_LightProjectionMatrix( const idVec3 &origin, const idPlane &rearPlane, idVec4 mat[4] );
void				R_MakeShadowFrustums( idRenderLightLocal *def );
srfTriangles_t *	R_CreateShadowVolume( const idRenderEntityLocal *ent,
										  const srfTriangles_t *tri, const idRenderLightLocal *light,
										  shadowGen_t optimize, srfCullInfo_t &cullInfo );

#endif /* !__RENDERGEOMETRY_H__ */
