/*
===========================================================================

Doom 3 GPL Source Code
Copyright (C) 1999-2011 id Software LLC, a ZeniMax Media company. 

This file is part of the Doom 3 GPL Source Code (?Doom 3 Source Code?).  

Doom 3 Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Doom 3 Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Doom 3 Source Code.  If not, see <http://www.gnu.org/licenses/>.

In addition, the Doom 3 Source Code is also subject to certain additional terms. You should have received a copy of these additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the Doom 3 Source Code.  If not, please request a copy in writing from id Software at the address below.

If you have questions concerning this license or the applicable additional terms, you may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.

===========================================================================
*/

#ifndef __TR_LOCAL_H__
#define __TR_LOCAL_H__

#include "../imagetools/BinaryImage.h"
#include "../imagetools/ImageTools.h"
#include "Image.h"
#include "RendererStartupDiagnostics.h"
#include "RenderTexture.h"
#include "GpuSkinning.h"
#include "ClassicDeformDomain.h"
#if defined( _MD5R_SUPPORT ) || defined( Q4SDK_MD5R )
#include "../idlib/geometry/rvVertex.h"
#endif

class idRenderWorldLocal;

// lightGridBakeOptions_t is public (RenderSystem.h); the free-function bake
// implementation stays renderer-internal behind the idRenderSystem virtuals
void					R_SetDefaultLightGridBakeOptions( lightGridBakeOptions_t &options );
bool					R_BakeCurrentLightGrids( const lightGridBakeOptions_t &options, const char *jobName = NULL );
bool					R_LightGridFileMatchesBakeOptions( const char *name, const lightGridBakeOptions_t &options, const idRenderWorldLocal *world );
bool					R_LightGridPackFileMatchesBakeOptions( const char *name, const lightGridBakeOptions_t &options, const idRenderWorldLocal *world );

// everything that is needed by the backend needs
// to be double buffered to allow it to run in
// parallel on a dual cpu machine
const int SMP_FRAMES = 1;

const int FALLOFF_TEXTURE_SIZE =	64;

const float	DEFAULT_FOG_DISTANCE = 500.0f;

const int FOG_ENTER_SIZE = 64;
const float FOG_ENTER = (FOG_ENTER_SIZE+1.0f)/(FOG_ENTER_SIZE*2);
// picky to get the bilerp correct at terminator

enum fogPlaneIndex_t {
	FOG_DISTANCE_PLANE_S = 0,
	FOG_DISTANCE_PLANE_T,
	FOG_ENTER_PLANE_T,
	FOG_ENTER_PLANE_S
};

extern idPlane fogTexGenPlanes[4];


// idScreenRect gets carried around with each drawSurf, so it makes sense
// to keep it compact, instead of just using the idBounds class
class idScreenRect {
public:
	short		x1, y1, x2, y2;							// inclusive pixel bounds inside viewport
    float       zmin, zmax;								// for depth bounds test

	void		Clear();								// clear to backwards values
	void		AddPoint( float x, float y );			// adds a point
	void		Expand();								// expand by one pixel each way to fix roundoffs
	void		Intersect( const idScreenRect &rect );
	void		Union( const idScreenRect &rect );
	bool		Equals( const idScreenRect &rect ) const;
	bool		IsEmpty() const;
};

idScreenRect R_ScreenRectFromViewFrustumBounds( const idBounds &bounds );
void R_ShowColoredScreenRect( const idScreenRect &rect, int colorIndex );

typedef enum {
	DC_BAD,
	DC_RENDERVIEW,
	DC_UPDATE_ENTITYDEF,
	DC_DELETE_ENTITYDEF,
	DC_UPDATE_LIGHTDEF,
	DC_DELETE_LIGHTDEF,
	DC_LOADMAP,
	DC_CROP_RENDER,
	DC_UNCROP_RENDER,
	DC_CAPTURE_RENDER,
	DC_END_FRAME,
	DC_DEFINE_MODEL,
	DC_SET_PORTAL_STATE,
	DC_UPDATE_SOUNDOCCLUSION,
	DC_GUI_MODEL,
	DC_UPDATE_EFFECTDEF,
	DC_STOP_EFFECTDEF,
	DC_DELETE_EFFECTDEF
} demoCommand_t;

/*
==============================================================================

SURFACES

==============================================================================
*/

#include "ModelDecal.h"
#include "ModelOverlay.h"
#include "Interaction.h"
#include "TemporalPresentation.h"


// drawSurf_t structures command the back end to render surfaces
// a given srfTriangles_t may be used with multiple viewEntity_t,
// as when viewed in a subview or multiple viewport render, or
// with multiple shaders when skinned, or, possibly with multiple
// lights, although currently each lighting interaction creates
// unique srfTriangles_t

// drawSurf_t are always allocated and freed every frame, they are never cached
static const int	DSF_VIEW_INSIDE_SHADOW	= 1;
static const int	DSF_BSE_EFFECT			= 2;
// Surface of an entity forced into the view by REF_OUTLINE_THROUGH_WORLD that the
// portal walk never reached. It lives on viewDef->outlineDrawSurfs rather than
// viewDef->drawSurfs, so the flag is a statement of provenance for anything
// holding one, not a test the main passes have to remember to make.
static const int	DSF_OUTLINE_ONLY		= 4;
// Geometry emitted while R_RenderGuiSurf expands a GUI onto a 3D surface.
// This provenance is independent of the generated material's GUI sort.
static const int	DSF_IN_WORLD_GUI		= 8;

typedef struct drawSurf_s {
	const srfTriangles_t	*geo;
	const struct viewEntity_s *space;
	const idMaterial		*material;	// may be NULL for shadow volumes
	float					sort;		// material->sort, modified by gui / entity sort offsets
	const float				*shaderRegisters;	// evaluated and adjusted for referenceShaders
	const struct drawSurf_s	*nextOnLight;	// viewLight chains
	idScreenRect			scissorRect;	// for scissor clipping, local inside renderView viewport
	const struct portalArea_s *area;	// portal area used for light-grid lookup, if available
	int						dsFlags;			// DSF_VIEW_INSIDE_SHADOW, etc
	struct vertCache_s		*dynamicTexCoords;	// float * in vertex cache memory
	float					*texGenTransformAndViewOrg;	// packed MD5R texgen rows + local view origin
	struct vertCache_s		*decalColorCache;	// optional per-stage color blocks for decals
	int						decalColorOffset;	// bytes from decalColorCache to the first stage color block
	int						decalColorStride;	// bytes between stage color blocks (numVerts * 4)
	int						decalColorStageCount;
	// Sealed by R_FinalizeDrawSurf. Receiver-only drawSurfs intentionally leave
	// this slot untouched and publish NOT_APPLICABLE snapshots per packet role.
	classicDeformRecord_t	classicDeform;
	// specular directions for non vertex program cards, skybox texcoords, etc
} drawSurf_t;


// shadowFrustum_t, areaReference_t, the idRenderLight/idRenderEntity
// interfaces, and the idRenderLightLocal/idRenderEntityLocal definitions live
// in the shared render-geometry library so dmap can construct them without
// renderer internals
#include "../render_geo/RenderGeometry.h"

typedef enum {
	SHADOWMAP_CASTER_REJECT_UNKNOWN = 0,
	SHADOWMAP_CASTER_REJECT_NO_SHADOW,
	SHADOWMAP_CASTER_REJECT_VIEW_ONLY,
	SHADOWMAP_CASTER_REJECT_DEPTH_HACK,
	SHADOWMAP_CASTER_REJECT_NO_GEOMETRY,
	SHADOWMAP_CASTER_REJECT_POINT_LIGHT_EMITTER,
	SHADOWMAP_CASTER_REJECT_TRANSLUCENT_DISABLED,
	SHADOWMAP_CASTER_REJECT_TRANSLUCENT_UNSUPPORTED,
	SHADOWMAP_CASTER_REJECT_SURFACE_NO_SHADOW,
	SHADOWMAP_CASTER_REJECT_DEDICATED_COLLISION,
	SHADOWMAP_CASTER_REJECT_GUI,
	SHADOWMAP_CASTER_REJECT_SUBVIEW,
	SHADOWMAP_CASTER_REJECT_AREA_DISCONNECTED,
	SHADOWMAP_CASTER_REJECT_SPECTRUM_MISMATCH,
	SHADOWMAP_CASTER_REJECT_LOD,
	SHADOWMAP_CASTER_REJECT_COUNT
} shadowMapCasterRejectReason_t;

// Per-receiver ownership completeness bits shared by the front end and
// renderer backends. LOCAL maps contain global caster chains; GLOBAL maps
// additionally contain noSelfShadow/local caster chains.
typedef enum {
	SHADOWMAP_RECEIVER_MASK_LOCAL = 1 << 0,
	SHADOWMAP_RECEIVER_MASK_GLOBAL = 1 << 1
} shadowMapReceiverMask_t;

bool R_ShadowMapCasterAdmissionSelfTest( void );
bool R_ShadowMapLODAdmissionSelfTest( void );
bool R_ShadowMapsNeedPerSurfaceStencilVolumes(
		const idRenderLightLocal *lightDef );

// viewLights are allocated on the frame temporary stack memory
// a viewLight contains everything that the back end needs out of an idRenderLightLocal,
// which the front end may be modifying simultaniously if running in SMP mode.
// a viewLight may exist even without any surfaces, and may be relevent for fogging,
// but should never exist if its volume does not intersect the view frustum
typedef struct viewLight_s {
	struct viewLight_s *	next;

	// back end should NOT reference the lightDef, because it can change when running SMP
	idRenderLightLocal *	lightDef;

	// for scissor clipping, local inside renderView viewport
	// scissorRect.Empty() is true if the viewEntity_t was never actually
	// seen through any portals
	idScreenRect			scissorRect;

	// if the view isn't inside the light, we can use the non-reversed
	// shadow drawing, avoiding the draws of the front and rear caps
	bool					viewInsideLight;

	// true if globalLightOrigin is inside the view frustum, even if it may
	// be obscured by geometry.  This allows us to skip shadows from non-visible objects
	bool					viewSeesGlobalLightOrigin;	

	// if !viewInsideLight, the corresponding bit for each of the shadowFrustum
	// projection planes that the view is on the negative side of will be set,
	// allowing us to skip drawing the projected caps of shadows if we can't see the face
	int						viewSeesShadowPlaneBits;

	idVec3					globalLightOrigin;			// global light origin used by backend
	bool					pointLight;					// true for point light projections
	bool					parallel;					// true for directional/parallel lights
	idVec3					lightRadius;				// copied from renderLight parms for point-light shadow mapping
	idMat3					lightAxis;					// immutable authored orientation for backend probe records
	idPlane					lightProject[4];			// light project used by backend
	idPlane					fogPlane;					// fog plane for backend fog volume rendering
	const srfTriangles_t *	frustumTris;				// light frustum for backend fog volume rendering
	const idMaterial *		lightShader;				// light shader used by backend
	const float	*			shaderRegisters;			// shader registers used by backend
	idImage *				falloffImage;				// falloff image used by backend

	const struct drawSurf_s	*globalShadows;				// shadow everything
	const struct drawSurf_s	*localInteractions;			// don't get local shadows
	const struct drawSurf_s	*localShadows;				// don't shadow local Surfaces
	// Receiver-specific stencil lists containing only casters absent from a
	// partial shadow map. Vulkan combines these with map sampling without
	// re-stamping casters already represented in the map.
	const struct drawSurf_s	*globalShadowMapStencilSupplements;
	const struct drawSurf_s	*localShadowMapStencilSupplements;
	const struct drawSurf_s	*globalInteractions;		// get shadows from everything
	const struct drawSurf_s	*localShadowMapCasters;		// ambient caster geometry that should not shadow local receivers
	const struct drawSurf_s	*globalShadowMapCasters;	// ambient caster geometry that can shadow all receivers
	const struct drawSurf_s	*localShadowMapDynamicCasters;	// dynamic (recently changing) casters, kept out of the
	const struct drawSurf_s	*globalShadowMapDynamicCasters;	// static cache signature and composed over cached tiles
	const struct drawSurf_s	*localTranslucentShadowMapCasters;	// blended caster geometry excluded from local-only receivers
	const struct drawSurf_s	*globalTranslucentShadowMapCasters;	// blended caster geometry that can shadow all receivers
	int						shadowMapCasterCount;
	int						shadowMapAlphaCasterCount;
	int						shadowMapTranslucentCasterCount;
	int						shadowMapStaticCasterCount;
	int						shadowMapDynamicCasterCount;
	int						shadowMapRejectedCasterCount;
	int						shadowMapExpandedCasterCount;
	int						shadowMapLODTestCount;
	int						shadowMapLODRejectedCount;
	int						shadowMapLODAlphaRejectedCount;
	int						shadowMapLODTranslucentRejectedCount;
	int						shadowMapCasterSignature;
	int						shadowMapRejectedCasterReasons[SHADOWMAP_CASTER_REJECT_COUNT];
	// A complete mapped receiver pass contains every parity-relevant stock
	// stencil caster. A partial map remains usable only with the exact
	// ownership-specific stencil supplements below. Conversely, a mapped-pass
	// miss may use full stencil only when every admitted map caster has a
	// usable volume.
	int						shadowMapIncompleteMapMask;
	int						shadowMapIncompleteStencilMask;
	// A hybrid receiver samples the partial map while stencil supplies the
	// casters absent from it. This mask is narrower than either completeness
	// mask: it records only ownership for which at least one required caster
	// is absent from BOTH representations.
	int						shadowMapHybridIncompleteMask;
	int						shadowMapPrelightMapMissingMask;
	int						shadowMapPrelightStencilRequiredMask;
	int						shadowMapPrelightStencilReadyMask;
	const struct drawSurf_s	*translucentInteractions;	// get shadows from everything
} viewLight_t;


// a viewEntity is created whenever a idRenderEntityLocal is considered for inclusion
// in the current view, but it may still turn out to be culled.
// viewEntity are allocated on the frame temporary stack memory
// a viewEntity contains everything that the back end needs out of a idRenderEntityLocal,
// which the front end may be modifying simultaniously if running in SMP mode.
// A single entityDef can generate multiple viewEntity_t in a single frame, as when seen in a mirror
typedef struct viewEntity_s {
	struct viewEntity_s	*next;

	// back end should NOT reference the entityDef, because it can change when running SMP
	idRenderEntityLocal	*entityDef;

	// for scissor clipping, local inside renderView viewport
	// scissorRect.Empty() is true if the viewEntity_t was never actually
	// seen through any portals, but was created for shadow casting.
	// a viewEntity can have a non-empty scissorRect, meaning that an area
	// that it is in is visible, and still not be visible.
	idScreenRect		scissorRect;

	bool				weaponDepthHack;
	float				modelDepthHack;
	float				distanceToCamera;
	float				screenCoverage;

	// Immutable back-end copy of renderEntity_t's flat diffuse presentation.
	// Height parameters normalize model-local Z as
	// ( localZ - flatDiffuseMinZ ) * flatDiffuseInvHeight.
	idVec4				flatDiffuseColor;
	int					flatDiffuseFlags;
	float				flatDiffuseMinZ;
	float				flatDiffuseInvHeight;

	float				modelMatrix[16];		// local coords to global coords
	float				modelViewMatrix[16];	// local coords to eye coords

	// per-view memo for non-constant material register evaluation: the
	// ambient pass and every per-light R_LinkLightSurf evaluate identical
	// inputs for the same (entity, material) pair within one view. Lives in
	// cleared frame memory, so it dies with the view by construction.
	const idMaterial *	shaderRegisterMemoMaterials[4];
	const float *		shaderRegisterMemoRegs[4];
	int					numShaderRegisterMemos;
} viewEntity_t;


const int	MAX_CLIP_PLANES	= 1;				// we may expand this to six for some subview issues

// viewDefs are allocated on the frame temporary stack memory
typedef struct viewDef_s {
	// specified in the call to DrawScene()
	renderView_t		renderView;

	float				projectionMatrix[16];
	viewEntity_t		worldSpace;

	// Immutable temporal-presentation metadata prepared before projection
	// setup. The fields travel with RC_DRAW_VIEW so GL and Vulkan consume the
	// same jitter, cut, generation, and per-view history identity.
	unsigned long long	temporalViewIdentity;
	unsigned int		temporalHistoryGeneration;
	int				temporalJitterIndex;
	idVec2				temporalJitterPixels;
	idVec3				temporalPreviousViewOrigin;
	idMat3				temporalPreviousViewAxis;
	idVec4				temporalPreviousProjectInfo;
	idVec2				temporalPreviousJitterPixels;
	temporalHistoryResetReason_t temporalHistoryResetReason;
	bool				temporalPrepared;
	bool				temporalJitterEnabled;
	bool				temporalHistoryValid;
	bool				temporalCaptureFrame;
	bool				temporalPreviousProjectionValid;

	idRenderWorldLocal *renderWorld;
	int					renderFlags;

	float				floatTime;

	idVec3				initialViewAreaOrigin;
	// Used to find the portalArea that view flooding will take place from.
	// for a normal view, the initialViewOrigin will be renderView.viewOrg,
	// but a mirror may put the projection origin outside
	// of any valid area, or in an unconnected area of the map, so the view
	// area must be based on a point just off the surface of the mirror / subview.
	// It may be possible to get a failed portal pass if the plane of the
	// mirror intersects a portal, and the initialViewAreaOrigin is on
	// a different side than the renderView.viewOrg is.

	bool				isSubview;				// true if this view is not the main view
	bool				isMirror;				// the portal is a mirror, invert the face culling
	bool				isXraySubview;

	bool				isEditor;

	bool				skipDrawSurfAreaResolve;
	// drawSurf->area is only consumed by the light-grid indirect pass and
	// scene-packet capture; when neither can use it this view, R_AddDrawSurf
	// skips the per-surface PointInArea BSP descent. false (= resolve) for
	// manually constructed views (e.g. 2D gui views) by cleared allocation.

	int					numClipPlanes;			// mirrors will often use a single clip plane
	idPlane				clipPlanes[MAX_CLIP_PLANES];		// in world space, the positive side
												// of the plane is the visible side
	idScreenRect		viewport;				// in real pixels and proper Y flip

	idScreenRect		scissor;
	// for scissor clipping, local inside renderView viewport
	// subviews may only be rendering part of the main view
	// these are real physical pixel values, possibly scaled and offset from the
	// renderView x/y/width/height

	struct viewDef_s *	superView;				// never go into an infinite subview loop 
	struct drawSurf_s *	subviewSurface;

	// drawSurfs are the visible surfaces of the viewEntities, sorted
	// by the material sort parameter
	drawSurf_t **		drawSurfs;				// we don't use an idList for this, because
	int					numDrawSurfs;			// it is allocated in frame temporary memory
	int					maxDrawSurfs;			// may be resized

	// Surfaces of entities forced into the view for their outline alone, which the
	// portal walk never reached - see R_AddThroughWorldOutlines. They are kept off
	// drawSurfs deliberately. Such an entity is in no depth buffer, receives no
	// light and casts no shadow, so every pass but the outline would draw it
	// wrong; a separate list means no pass can get it wrong by forgetting to look.
	drawSurf_t **		outlineDrawSurfs;
	int					numOutlineDrawSurfs;
	int					maxOutlineDrawSurfs;

	struct viewLight_s	*viewLights;			// chain of all viewLights effecting view
	struct viewEntity_s	*viewEntitys;			// chain of all viewEntities effecting view, including off screen ones casting shadows
	// we use viewEntities as a check to see if a given view consists solely
	// of 2D rendering, which we can optimize in certain ways.  A 2D view will
	// not have any viewEntities

	idPlane				frustum[5];				// positive sides face outward, [4] is the front clip plane
	idFrustum			viewFrustum;

	int					areaNum;				// -1 = not in a valid area

	bool *				connectedAreas;
	// An array in frame temporary memory that lists if an area can be reached without
	// crossing a closed door.  This is used to avoid drawing interactions
	// when the light is behind a closed door.

} viewDef_t;


// complex light / surface interactions are broken up into multiple passes of a
// simple interaction shader
typedef struct {
	const drawSurf_t *	surf;

	idImage *			lightImage;
	idImage *			lightFalloffImage;
	idImage *			bumpImage;
	idImage *			diffuseImage;
	idImage *			specularImage;

	idVec4				diffuseColor;	// may have a light color baked into it, will be < tr.backEndRendererMaxLight
	idVec4				specularColor;	// may have a light color baked into it, will be < tr.backEndRendererMaxLight
	idVec4				flatDiffuseParams;	// sweep strength, local min Z, inverse height, upward phase
	stageVertexColor_t	vertexColor;	// applies to both diffuse and specular

	int					ambientLight;	// use tr.ambientNormalMap instead of normalization cube map 
	// (not a bool just to avoid an uninitialized memory check of the pad region by valgrind)

	// these are loaded into the vertex program
	idVec4				localLightOrigin;
	idVec4				localViewOrigin;
	idVec4				lightProjection[4];	// in local coordinates, possibly with a texture matrix baked in
	idVec4				bumpMatrix[2];
	idVec4				diffuseMatrix[2];
	idVec4				specularMatrix[2];
} drawInteraction_t;

typedef bool (*drawInteractionStageFilter_t)( const shaderStage_t *surfaceStage, const float *surfaceRegs );


/*
=============================================================

RENDERER BACK END COMMAND QUEUE

TR_CMDS

=============================================================
*/

typedef enum {
	RC_NOP,
	RC_DRAW_VIEW,
	RC_DRAW_SPECIAL_EFFECTS,
	RC_SET_BUFFER,
	RC_COPY_RENDER,
	RC_SET_RENDERTEXTURE,
	RC_RESOLVE_MSAA,
	RC_RESOLVE_TEMPORAL_PRESENTATION,
	RC_CLEAR_RENDERTARGET,
	RC_SET_POSTPROCESS_SOURCE_SIZE,
	RC_SET_POSTPROCESS_SOURCE_COLOR_SPACE,
	RC_SET_POSTPROCESS_SMAA_QUALITY,
	RC_SWAP_BUFFERS		// can't just assume swap at end of list because
						// of forced list submission before syncs
} renderCommand_t;

typedef struct {
	renderCommand_t		commandId, *next;
} emptyCommand_t;

typedef struct {
	renderCommand_t		commandId, *next;
	unsigned int	buffer;
	int		frameCount;
} setBufferCommand_t;

typedef struct {
	renderCommand_t		commandId, *next;
	viewDef_t	*viewDef;
} drawSurfsCommand_t;

typedef struct {
	renderCommand_t		commandId, *next;
	int		x, y, imageWidth, imageHeight;
	idImage	*image;
	int		cubeFace;					// when copying to a cubeMap
	bool	copyDepth;					// copy the depth buffer instead of color
} copyRenderCommand_t;

// jmarshall
typedef struct {
	renderCommand_t		commandId, * next;
	idRenderTexture* renderTexture;
	idRenderTexture* feedbackRenderTexture;	// optional scene target allowed to feed _currentRender
} setRenderTargetCommand_t;

typedef struct {
	renderCommand_t		commandId, * next;
	bool clearColor;
	bool clearDepth;

	float clearDepthValue;
	idVec4 clearColorValue;
} renderClearBufferCommand_t;

typedef struct {
	renderCommand_t		commandId, * next;
	idRenderTexture* msaaRenderTexture;
	idRenderTexture* destRenderTexture;
	bool resolveDepth;
	int frameNumber;
	unsigned int historyGeneration;
} resolveRenderTargetCommand_t;

typedef struct {
	renderCommand_t		commandId, * next;
	idRenderTexture*	sceneColorTarget;
	idRenderTexture*	sceneDepthTarget;
	idRenderTexture*	historyReadTarget;
	idRenderTexture*	historyWriteTarget;
	const viewDef_t*	viewDef;
	renderPresentationState_t presentation;
	unsigned int		historyGeneration;
	bool				historyValid;
	bool				captureFrame;
	float				feedback;
	float				reactiveScale;
	int				debugMode;
	advancedScreenSpaceConfig_t advancedScreenSpace;
} resolveTemporalPresentationCommand_t;

typedef struct {
	renderCommand_t		commandId, * next;
	idVec4 texelSize;
} setPostProcessSourceSizeCommand_t;

typedef struct {
	renderCommand_t		commandId, * next;
	idVec4 colorSpace;
} setPostProcessSourceColorSpaceCommand_t;

typedef struct {
	renderCommand_t		commandId, * next;
	idVec4 quality;
} setPostProcessSMAAQualityCommand_t;
// jmarshall end

//=======================================================================

// this is the inital allocation for max number of drawsurfs
// in a given view, but it will automatically grow if needed
const int	INITIAL_DRAWSURFS =			0x4000;

// a request for frame memory will never fail
// (until malloc fails), but it may force the
// allocation of a new memory block that will
// be discontinuous with the existing memory
typedef struct frameMemoryBlock_s {
	struct frameMemoryBlock_s *next;
	int		size;
	int		used;
	alignas(16) byte base[4];	// dynamically allocated as [size]
} frameMemoryBlock_t;
static_assert( offsetof( frameMemoryBlock_t, base ) % 16 == 0, "frame allocator payload must remain 16-byte aligned" );

// all of the information needed by the back end must be
// contained in a frameData_t.  This entire structure is
// duplicated so the front and back end can run in parallel
// on an SMP machine (OBSOLETE: this capability has been removed)
typedef struct {
	// one or more blocks of memory for all frame
	// temporary allocations
	frameMemoryBlock_t	*memory;

	// alloc will point somewhere into the memory chain
	frameMemoryBlock_t	*alloc;

	srfTriangles_t *	firstDeferredFreeTriSurf;
	srfTriangles_t *	lastDeferredFreeTriSurf;

	int					memoryHighwater;	// max used on any frame

	// the currently building command list 
	// commands can be inserted at the front if needed, as for required
	// dynamically generated textures
	emptyCommand_t	*cmdHead, *cmdTail;		// may be of other command type based on commandId
} frameData_t;

extern	frameData_t	*frameData;

//=======================================================================

void R_LockSurfaceScene( viewDef_t *parms );
void R_ClearCommandChain( void );
void R_AddDrawViewCmd( viewDef_t *parms );
void R_AddSpecialEffects( viewDef_t *parms );

void R_ReloadGuis_f( const idCmdArgs &args );
void R_ListGuis_f( const idCmdArgs &args );

void *R_GetCommandBuffer( int bytes );

// this allows a global override of all materials
bool R_GlobalShaderOverride( const idMaterial **shader );
bool R_ValidateGLSLProgram( newShaderStage_t *stage );
bool RB_BindGLSLShaderParm( glslShaderParmBinding_t binding, int location, const shaderStage_t *stage, const drawInteraction_t *din );
idImage *RB_ResolveGLSLShaderTextureImage( const newShaderStage_t *stage, int slot, const drawInteraction_t *din );

// this does various checks before calling the idDeclSkin
const idMaterial *R_RemapShaderBySkin( const idMaterial *shader, const idDeclSkin *customSkin, const idMaterial *customShader );


//====================================================


/*
** performanceCounters_t
*/
typedef struct {
	int		c_sphere_cull_in, c_sphere_cull_clip, c_sphere_cull_out;
	int		c_box_cull_in, c_box_cull_out;
	int		c_createInteractions;	// number of calls to idInteraction::CreateInteraction
	int		c_createLightTris;
	int		c_createShadowVolumes;
	int		c_generateMd5;
	int		c_entityDefCallbacks;
	int		c_alloc, c_free;	// counts for R_StaticAllc/R_StaticFree
	int		c_visibleViewEntities;
	int		c_shadowViewEntities;
	int		c_viewLights;
	int		c_numViews;			// number of total views rendered
	int		c_deformedSurfaces;	// idMD5Mesh::GenerateSurface
	int		c_deformedVerts;	// idMD5Mesh::GenerateSurface
	int		c_deformedIndexes;	// idMD5Mesh::GenerateSurface
	int		c_tangentIndexes;	// R_DeriveTangents()
	int		c_numDecalIndexes;	// idRenderModelDecal::AddDecalDrawSurf
	int		c_entityUpdates, c_lightUpdates, c_entityReferences, c_lightReferences;
	int		c_entitySnapshotsReused;	// transform-only entity updates that kept the dynamic snapshot
	int		c_entityUpdatesElided;		// bytewise-identical entity updates that also kept refs and interactions
	int		c_guiSurfs;
	int		frontEndMsec;		// sum of time in all RE_RenderScene's in a frame
} performanceCounters_t;


typedef struct {
	int		current2DMap;
	int		current3DMap;
	int		currentCubeMap;
	int		texEnv;
	textureType_t	textureType;
} tmu_t;

const int MAX_MULTITEXTURE_UNITS =	8;
typedef struct {
	tmu_t		tmu[MAX_MULTITEXTURE_UNITS];
	int			currenttmu;

	int			faceCulling;
	int			glStateBits;
	bool		forceGlState;		// the next GL_State will ignore glStateBits and set everything
} glstate_t;


typedef struct {
	int		c_surfaces;
	int		c_shaders;
	int		c_vertexes;
	int		c_indexes;		// one set per pass
	int		c_totalIndexes;	// counting all passes

	int		c_drawElements;
	int		c_drawIndexes;
	int		c_drawVertexes;
	int		c_drawRefIndexes;
	int		c_drawRefVertexes;

	int		c_shadowElements;
	int		c_shadowIndexes;
	int		c_shadowVertexes;

	int		c_vboIndexes;
	float	c_overDraw;	

	float	maxLightValue;	// for light scale
	int		msec;			// total msec for backend run
} backEndCounters_t;

// all state modified by the back end is separated
// from the front end state
typedef struct {
	int					frameCount;		// used to track all images used in a frame
	const viewDef_t	*	viewDef;
	backEndCounters_t	pc;

	idRenderTexture		*renderTexture;
	idRenderTexture		*feedbackRenderTexture;	// active scene target allowed to feed _currentRender
	idVec4				postProcessTexelSize;	// x/y = inverse source size, z/w = source size
	int					resolutionScaleWidth;	// crop this frame rendered into, 0 when unscaled
	int					resolutionScaleHeight;
	idVec4				postProcessSourceColorSpace;	// x = contract enum, y = display gamma, z/w reserved
	idVec4				postProcessSMAAQuality;	// x = edge mode, y = threshold, z = search steps, w = local contrast

	const viewEntity_t *currentSpace;		// for detecting when a matrix must change
	idScreenRect		currentScissor;
	// for scissor clipping, local inside renderView viewport

	viewLight_t *		vLight;
	int					depthFunc;			// GLS_DEPTHFUNC_EQUAL, or GLS_DEPTHFUNC_LESS for translucent
	float				lightTextureMatrix[16];	// only if lightStage->texture.hasMatrix
	float				lightColor[4];		// evaluation of current light's color stage

	float				lightScale;			// Every light color calaculation will be multiplied by this,
											// which will guarantee that the result is < tr.backEndRendererMaxLight
											// A card with high dynamic range will have this set to 1.0
	float				overBright;			// The amount that all light interactions must be multiplied by
											// with post processing to get the desired total light level.
											// A high dynamic range card will have this set to 1.0.

	bool				currentRenderCopied;	// true if any material has already referenced _currentRender
	bool				currentDepthCopied;	// true if any material has already referenced _currentDepth

	// our OpenGL state deltas
	glstate_t			glState;

	int					c_copyFrameBuffer;
} backEndState_t;


const int MAX_GUI_SURFACES	= 1024;		// default size of the drawSurfs list for guis, will
										// be automatically expanded as needed

typedef enum {
	BE_ARB2,
	// The programmable path standing on its own, with no ARB2 bridge beneath
	// it. Selected only on a profile that cannot have ARB2 (desktop core,
	// OpenGL ES); every compatibility context keeps BE_ARB2 with the modern
	// executor layered over it as before. Passes the modern executor does not
	// own simply do not render under this backend -- there is nothing to hand
	// them back to.
	BE_MODERN,
	// Doom 3-shaped GLES 3.0 backend: its own depth / interaction / ambient /
	// fog passes written directly against ES 3.0, rather than the modern
	// executor's cluster-forward architecture. Opt-in with `r_renderer glesd3`
	// on the renderer-gles module only; never selected automatically, so
	// BE_MODERN remains the default ES path.
	BE_GLES_D3,
	BE_BAD
} backEndName_t;

typedef struct {
	int		x, y, width, height;	// these are in physical, OpenGL Y-at-bottom pixels
} renderCrop_t;
static const int	MAX_RENDER_CROPS = 8;

/*
** Most renderer globals are defined here.
** backend functions should never modify any of these fields,
** but may read fields that aren't dynamically modified
** by the frontend.
*/
class idRenderSystemLocal : public idRenderSystem {
public:
	// external functions
	virtual void			Init( void );
	virtual void			Shutdown( void );
	virtual void			InitOpenGL( void );
	virtual void			ShutdownOpenGL( void );
	virtual bool			IsOpenGLRunning( void ) const;
	virtual bool			IsFullScreen( void ) const;
	virtual int				GetScreenWidth( void ) const;
	virtual int				GetScreenHeight( void ) const;
	virtual int				GetVideoRestartCount( void ) const;
	virtual idRenderWorld *	AllocRenderWorld( void );
	virtual void			FreeRenderWorld( idRenderWorld *rw );
	virtual void			BeginLevelLoad( void );
	virtual void			EndLevelLoad( void );
	virtual void			ExportMD5R( bool compressed );
#ifdef Q4SDK_MD5R
	virtual void			CopyPrimBatchTriangles( idDrawVert *destDrawVerts, glIndex_t *destIndices, void *primBatchMesh, void *silTraceVerts );
#else
#if defined( _MD5R_SUPPORT )
	virtual void			CopyPrimBatchTriangles( idDrawVert *destDrawVerts, glIndex_t *destIndices, rvMesh *primBatchMesh, const rvSilTraceVertT *silTraceVerts );
#endif
#endif
	virtual bool			RegisterFont( const char *fontName, fontInfoEx_t &font );
	virtual void			SetColor( const idVec4 &rgba );
	virtual void			SetColor4( float r, float g, float b, float a );
	virtual void			DrawStretchPic ( const idDrawVert *verts, const glIndex_t *indexes, int vertCount, int indexCount, const idMaterial *material,
											bool clip = true, float x = 0.0f, float y = 0.0f, float w = 640.0f, float h = 480.0f );
	virtual void			DrawStretchPic ( float x, float y, float w, float h, float s1, float t1, float s2, float t2, const idMaterial *material );

	virtual void			DrawStretchTri ( idVec2 p1, idVec2 p2, idVec2 p3, idVec2 t1, idVec2 t2, idVec2 t3, const idMaterial *material );
	virtual bool			GetMaterialStageImageInfo( const idMaterial *material, int stageIndex, materialImageInfo_t &info );
	virtual bool			UploadMaterialStageScratchImage( const idMaterial *material, int stageIndex, const byte *data, int width, int height );
	virtual void			SetLoadingScreenSwapIntervalBypass( bool active );
	virtual idDecl *		AllocMaterialDecl( void );
	virtual void			PreloadImage( const char *name );
	virtual void			GetDefaultLightGridBakeOptions( lightGridBakeOptions_t &options );
	virtual bool			HasPrimaryRenderView( void );
	virtual bool			GetCurrentLightGridBakeInfo( const lightGridBakeOptions_t &options, char *mapName, int mapNameLength, int *validAreaIndices, int maxAreaIndices, int &numValidAreaIndices );
	virtual bool			LightGridFileMatchesBakeOptions( const char *name, const lightGridBakeOptions_t &options );
	virtual bool			LightGridPackFileMatchesBakeOptions( const char *name, const lightGridBakeOptions_t &options );
	virtual bool			BakeCurrentLightGrids( const lightGridBakeOptions_t &options, const char *jobName = NULL );
	virtual void			FlushGui();
	virtual void			GlobalToNormalizedDeviceCoordinates( const idVec3 &global, idVec3 &ndc );
	virtual void			GetGLSettings( int& width, int& height );
	virtual void			PrintMemInfo( MemInfo_t *mi );

	virtual void			DrawSmallChar( int x, int y, int ch, const idMaterial *material );
	virtual void			DrawSmallStringExt( int x, int y, const char *string, const idVec4 &setColor, bool forceColor, const idMaterial *material );
	virtual void			DrawBigChar( int x, int y, int ch, const idMaterial *material );
	virtual void			DrawBigStringExt( int x, int y, const char *string, const idVec4 &setColor, bool forceColor, const idMaterial *material );
	virtual void			WriteDemoPics();
	virtual void			DrawDemoPics();
	virtual void			SetFrameShaderTime( int timeMsec );
	virtual void			BeginFrame( int windowWidth, int windowHeight );
	virtual void			SetSpecialEffect( ESpecialEffectType Which, bool Enabled );
	virtual void			SetSpecialEffectParm( ESpecialEffectType Which, int Parm, float Value );
	virtual void			ShutdownSpecialEffects( void );
	virtual void			EndFrame( int *frontEndMsec, int *backEndMsec );
	virtual void			TakeScreenshot( int width, int height, const char *fileName, int downSample, renderView_t *ref );
	virtual void			CropRenderSize( int width, int height, bool makePowerOfTwo = false, bool forceDimensions = false );
	virtual void			CaptureRenderToImage( const char *imageName );
	virtual void			CaptureRenderToFile( const char *fileName, bool fixAlpha );
	virtual void			CaptureRenderToFile( const char *fileName, bool fixAlpha, int outputWidth, int outputHeight );
	virtual void			SetPortalSkyCaptureViewCallback( renderPortalSkyCaptureViewCallback_t callback );
	virtual void			UnCrop();
	virtual void			GetCardCaps( bool &oldCard, bool &nv10or20 );
	virtual bool			UploadImage( const char *imageName, const byte *data, int width, int height );


	virtual idImage*		CreateImage(const char* name, idImageOpts* opts, textureFilter_t textureFilter);
	virtual idImage*		FindImage(const char* name);
	virtual bool			ValidateMaterialArbPrograms( const idMaterial* material );
	virtual bool			ValidateSMAALookupTextures( void );
	virtual idRenderTexture* CreateRenderTexture(idImage* albedoImage, idImage* depthImage, idImage* albedoImage2 = nullptr, idImage* albedoImage3 = nullptr);
	virtual void			DestroyRenderTexture(idRenderTexture* renderTexture);
	virtual void			ResizeImage(idImage* image, int width, int height);
	virtual bool			ResizeRenderTexture(idRenderTexture*& renderTexture, int width, int height);
	virtual void			GetRenderTextureSize(idRenderTexture* renderTexture, int& renderTextureWidth, int& renderTextureHeight);
	virtual void			SetRenderTextureDebugName(idRenderTexture* renderTexture, const char* label);
	virtual void			BindRenderTexture(idRenderTexture* renderTexture, idRenderTexture* feedbackRenderTexture);
	virtual void			ResolveMSAA(idRenderTexture* msaaRenderTexture, idRenderTexture* destRenderTexture, bool resolveDepth = false);
	virtual void			ClearRenderTarget(bool clearColor, bool clearDepth, float depthValue, float red, float green, float blue, float alpha = 1.0f);
	virtual void			SetPostProcessSourceSize(int width, int height);
	virtual void			SetPostProcessSourceColorSpace(const idVec4& colorSpace);
	virtual void			SetPostProcessSMAAQuality(const idVec4& quality);
	virtual void			SetUseUIViewportFor2D( bool enable );
	virtual bool			GetUseUIViewportFor2D( void ) const;
	virtual void			GetImageSize(idImage* image, int& imageWidth, int& imageHeight);
	virtual int				GetImageMSAASamples(idImage* image);
	virtual const glconfig_t &	GetGLConfig( void ) const;
	virtual bool			SetUnderwaterView( float amount, const idVec3 &tint, float fogDistance );
	virtual void			GetGpuFrameTiming( renderGpuFrameTiming_t &timing ) const;
	virtual void			ResetGpuFrameTiming( const char *reason );
	virtual void			GetPresentationState( renderPresentationState_t &state ) const;
	virtual bool			ResolveTemporalPresentation(
		idRenderTexture *sceneColorTarget,
		idRenderTexture *sceneDepthTarget,
		idRenderTexture *historyReadTarget,
		idRenderTexture *historyWriteTarget );
public:
	// internal functions
							idRenderSystemLocal( void );
							~idRenderSystemLocal( void );

	void					Clear( void );
	void					ResetSpecialEffects( void );
	void					SetBackEndRenderer();			// sets tr.backEndRenderer based on cvars
	void					ProcessPendingRenderTextureDeletes( void );
	void					RenderViewToViewport( const renderView_t *renderView, idScreenRect *viewport );
	void					CaptureDepthRenderToImage( const char *imageName );
	void					EmitFullscreenSpecialEffects( const renderView_t *renderView );

public:
	// renderer globals
	bool					registered;		// cleared at shutdown, set at InitOpenGL

	bool					takingScreenshot;

	int						frameCount;		// incremented every frame
	int						viewCount;		// incremented every view (twice a scene if subviewed)
											// and every R_MarkFragments call
	int						videoRestartCount; // incremented after successful vid_restart operations
	int						glContextGeneration; // incremented only when the GL context is destroyed and recreated
												 // (full restart); partial restarts keep the context and its
												 // objects alive, so generation-stamped GL handles stay valid

	size_t					staticAllocCount;	// running total of bytes allocated

	float					frameShaderTime;	// shader time for all non-world 2D rendering
	int						frameShaderTimeMsec;	// integer companion for GUI/cinematic/material timing
	idVec4					postProcessTexelSize;	// x/y = inverse source size, z/w = source size
	idVec4					postProcessSourceColorSpace;	// x = contract enum, y = display gamma, z/w reserved
	idVec4					postProcessSMAAQuality;	// x = edge mode, y = threshold, z = search steps, w = local contrast
	float					deltaTime;		// seconds since the previous top-level RenderScene call
	int						lastRenderTimeMsec;	// host milliseconds of the previous top-level RenderScene call

	int						viewportOffset[2];	// for doing larger-than-window tiled renderings
	int						tiledViewport[2];

	// determines which back end to use, and if vertex programs are in use
	backEndName_t			backEndRenderer;
	bool					backEndRendererHasVertexPrograms;
	float					backEndRendererMaxLight;	// 1.0 for standard, unlimited for floats
														// determines how much overbrighting needs
														// to be done post-process

	idVec4					ambientLightVector;	// used for "ambient bump mapping"

	float					sortOffset;				// for determinist sorting of equal sort materials

	idList<idRenderWorldLocal*>worlds;

	idRenderWorldLocal *	primaryWorld;
	renderView_t			primaryRenderView;
	viewDef_t *				primaryView;
	// many console commands need to know which world they should operate on
	int						specialEffectsEnabled;
	float					specialEffectParms[ SPECIAL_EFFECT_MAX ][ MAX_ENTITY_SHADER_PARMS ];
	idImage *				specialBlurDepthImage;
	idImage *				specialBlurDepthStencilImage;
	idImage *				specialBlurImage;
	idRenderTexture *		specialBlurDepthRenderTexture;
	idRenderTexture *		specialBlurRenderTexture;
	idImage *				specialALDepthImage;
	idImage *				specialALDepthStencilImage;
	idRenderTexture *		specialALDepthRenderTexture;
	idImage *				specialALLightImage;

	const idMaterial *		defaultMaterial;
	idImage *				testImage;
	idCinematic *			testVideo;
	float					testVideoStartTime;

	idImage *				ambientCubeImage;	// hack for testing dependent ambient lighting

	viewDef_t *				viewDef;

	performanceCounters_t	pc;					// performance counters

	drawSurfsCommand_t		lockSurfacesCmd;	// use this when r_lockSurfaces = 1

	viewEntity_t			identitySpace;		// can use if we don't know viewDef->worldSpace is valid
	FILE *					logFile;			// for logging GL calls and frame breaks

	int						stencilIncr, stencilDecr;	// GL_INCR / INCR_WRAP_EXT, GL_DECR / GL_DECR_EXT

	renderCrop_t			renderCrops[MAX_RENDER_CROPS];
	int						currentRenderCrop;

	// r_screenFraction below native, on a back end that can upscale the finished
	// frame. BeginFrame pushes a crop the whole frame renders into, and the back
	// end blits that corner out to the full back buffer before the swap.
	// Zero means no scaling crop is live this frame.
	bool					resolutionScaleCropActive;
	int						resolutionScaleWidth;
	int						resolutionScaleHeight;
	// latched once the game routes a frame through an offscreen scene target,
	// where a whole-frame crop cannot be resolved back to full screen
	bool					resolutionScaleSuppressed;

	bool					PushSceneResolutionScale( void );
	void					PopSceneResolutionScale( void );

	// GUI drawing variables for surface creation
	int						guiRecursionLevel;		// to prevent infinite overruns
	int						inWorldGuiEmissionDepth;	// provenance scope for R_RenderGuiSurf output
	class idGuiModel *		guiModel;
	class idGuiModel *		demoGuiModel;
	idList<idRenderTexture*> pendingRenderTextureDeletes;
	bool					useUIViewportFor2D;

	// openQ4: underwater view state, published by the game each frame and consumed by the GL
	// post-process pass. Not part of any render command, because the pass runs on the back buffer
	// after the view has already been resolved.
	float					underwaterAmount;
	idVec3					underwaterTint;
	float					underwaterFogDistance;
	idRenderTexture *		activeRenderTexture;
	renderPortalSkyCaptureViewCallback_t portalSkyCaptureViewCallback;
	bool					suppressLevelshotViewModels;
	bool					disableLevelshotEntityCulling;

	unsigned short			gammaTable[256];	// brightness / gamma modify this
};

extern backEndState_t		backEnd;
extern idRenderSystemLocal	tr;
extern glconfig_t			glConfig;		// outside of TR since it shouldn't be cleared during ref re-init
extern bool					tr_levelshotProjectionShiftActive;
extern float				tr_levelshotProjectionShiftX;
extern float				tr_levelshotProjectionShiftY;

static ID_INLINE bool R_IsPortalSkyView( void ) {
	return tr.viewDef != NULL && ( tr.viewDef->renderFlags & RF_PORTAL_SKY ) != 0;
}

static ID_INLINE bool R_ShouldSuppressViewModelForLevelshot( int viewID, int allowSurfaceInViewID, int weaponDepthHackInViewID ) {
	return tr.suppressLevelshotViewModels
		&& viewID != 0
		&& ( ( allowSurfaceInViewID != 0 && allowSurfaceInViewID == viewID )
			|| ( weaponDepthHackInViewID != 0 && weaponDepthHackInViewID == viewID ) );
}

static ID_INLINE bool R_ShouldSuppressViewLightForLevelshot( int viewID, int allowLightInViewID ) {
	return tr.suppressLevelshotViewModels
		&& viewID != 0
		&& allowLightInViewID != 0
		&& allowLightInViewID == viewID;
}

static ID_INLINE bool R_ShouldDisableEntityCullingForLevelshot( void ) {
	return tr.disableLevelshotEntityCulling;
}


//
// cvars
//
extern idCVar r_ext_vertex_array_range;

extern idCVar r_glDriver;				// "opengl32", etc
extern idCVar r_mode;					// video mode number
extern idCVar r_displayRefresh;			// optional display refresh rate option for vid mode
extern idCVar r_fullscreen;				// 0 = windowed, 1 = full screen
extern idCVar r_fullscreenDesktop;		// 1 = desktop-native fullscreen, 0 = exclusive fullscreen mode
extern idCVar r_borderless;				// 1 = borderless window when r_fullscreen is 0
extern idCVar r_hiddenWindow;			// 1 = create a hidden OpenGL window for batch jobs
extern idCVar r_windowWidth;				// windowed mode width
extern idCVar r_windowHeight;				// windowed mode height
extern idCVar r_multiSamples;			// number of antialiasing samples
extern idCVar r_postAA;					// post AA mode: 0 = off, 1/2/3 = SMAA medium/high/ultra, 4 = colour-edge prototype
extern idCVar r_postAAStatePoisonTest;	// intentionally dirty GL texture/client state before SMAA draws
extern idCVar r_pbrMaterials;			// allow explicitly PBR-authored materials on modern paths
extern idCVar r_pbrGeneratedLegacyFallback;	// allow development-only generated classic fallbacks
extern idCVar r_pbrDebug;				// PBR attachment/fallback debug view
extern idCVar r_pbrIBL;				// opt-in PBR-only analytic environment contribution
extern idCVar r_pbrIBLIntensity;		// PBR analytic environment intensity
extern idCVar r_pbrInferFromLegacyMaterials;	// research-only classic-material inference
extern idCVar r_bloom;					// enable bloom post-process
extern idCVar r_bloomThreshold;			// bloom bright-pass threshold
extern idCVar r_bloomSoftKnee;			// relative bloom soft threshold knee
extern idCVar r_bloomIntensity;			// bloom contribution scale
extern idCVar r_bloomRadius;			// bloom sample radius scale
extern idCVar r_bloomMipCount;			// number of bloom pyramid levels
extern idCVar r_ssao;					// enable SSAO post-process
extern idCVar r_ssaoRadius;			// SSAO sampling radius in view-space units
extern idCVar r_ssaoBias;				// SSAO horizon bias in view-space units
extern idCVar r_ssaoIntensity;			// SSAO darkening strength
extern idCVar r_ssaoPower;				// SSAO response curve
extern idCVar r_ssaoMaxDistance;		// SSAO far-distance fade
extern idCVar r_ssaoSamples;			// SSAO spiral sample count
extern idCVar r_ssaoDebug;				// visualize SSAO only
extern idCVar r_motionBlur;				// enable camera motion blur post-process
extern idCVar r_motionBlurStrength;		// motion blur strength multiplier
extern idCVar r_motionBlurMaxPixels;	// maximum motion blur radius in pixels
extern idCVar r_motionBlurSamples;		// motion blur gather sample count
extern idCVar r_motionBlurDebug;		// visualize motion blur vectors
extern idCVar r_motionBlurObjectVectors;	// include rigid-object motion vectors
extern idCVar r_forceSpecialEffects;	// force legacy special-effect bitmask for debugging
extern idCVar r_hdrSceneTarget;			// render the main scene into an HDR scene target before post-processing
extern idCVar r_hdrToneMap;				// enable filmic tone mapping and color correction
extern idCVar r_hdrExposure;			// tone-mapping exposure
extern idCVar r_hdrWhitePoint;			// filmic white point for tone mapping
extern idCVar r_hdrLift;				// post-process shadow lift when tone mapping is enabled
extern idCVar r_hdrPostGamma;			// post-process gamma curve when tone mapping is enabled
extern idCVar r_hdrGain;				// post-process gain when tone mapping is enabled
extern idCVar r_hdrVibrance;			// post-process vibrance when tone mapping is enabled
extern idCVar r_hdrSaturation;			// post-process saturation when tone mapping is enabled
extern idCVar r_hdrContrast;			// post-process contrast when tone mapping is enabled
extern idCVar r_hdrAutoExposure;		// automatically derive exposure from scene luminance
extern idCVar r_hdrAutoExposureAsync;	// async PBO readback for the auto-exposure luminance sample
extern idCVar r_hdrKeyValue;			// exposure key value used by auto exposure
extern idCVar r_hdrMinExposure;			// minimum auto-exposure multiplier
extern idCVar r_hdrMaxExposure;			// maximum auto-exposure multiplier
extern idCVar r_hdrAdaptUpSpeed;		// adaptation speed when moving toward a brighter exposure
extern idCVar r_hdrAdaptDownSpeed;		// adaptation speed when moving toward a darker exposure
extern idCVar r_hdrHighlightDesaturation;	// desaturate highlights before the final clamp
extern idCVar r_hdrGamutCompression;	// compress saturated highlights before the final clamp
extern idCVar r_hdrSRGBTextures;		// store diffuse material textures in sRGB formats when available
extern idCVar r_hdrSRGB;				// enable final framebuffer sRGB conversion when available
extern idCVar r_hdrDebugView;			// 0 = off, 1 = pre-tonemap heatmap, 2 = log-luminance view
extern idCVar r_crt;					// enable CRT monitor post-process
extern idCVar r_crtAmount;				// overall CRT blend amount
extern idCVar r_crtScanlineStrength;	// scanline intensity
extern idCVar r_crtMaskStrength;		// phosphor mask intensity
extern idCVar r_crtCurvature;			// screen curvature
extern idCVar r_crtChromatic;			// chromatic offset in pixel units
extern idCVar r_underwater;				// underwater view post-process
extern idCVar r_underwaterWarp;
extern idCVar r_underwaterBlur;
extern idCVar r_underwaterEdgeSoften;
extern idCVar r_underwaterCaustics;
extern idCVar r_underwaterBloom;
extern idCVar r_underwaterAberration;
extern idCVar r_underwaterParticles;
extern idCVar r_underwaterVisibility;
extern idCVar r_msaaResolveDepth;		// include depth when resolving MSAA render targets
extern idCVar r_msaaAlphaToCoverage;	// alpha-to-coverage for perforated materials on MSAA targets
extern idCVar r_celShading;				// cel shading on model entities
extern idCVar r_celShadingWorld;		// cel shading and edge outlines on world geometry
extern idCVar r_celShadingBands;		// quantize interaction lighting into cel bands
extern idCVar r_celShadingSteps;		// number of cel lighting bands
extern idCVar r_celShadingSoftness;		// how far a cel band boundary is allowed to blend
extern idCVar r_celShadingSpecular;		// hard-edge specular highlights when cel shading
extern idCVar r_celViewWeapon;			// allow cel shading on the first-person weapon
extern idCVar r_celOutline;				// silhouette outline shells on cel-shaded models
extern idCVar r_celOutlineWidth;		// model outline width in pixels
extern idCVar r_celOutlineAlpha;		// model outline opacity
extern idCVar r_celOutlineColor;		// model outline colour as "r g b a" (0-255)
extern idCVar r_celViewWeaponOutlineWidth;	// first-person weapon outline width in pixels
extern idCVar r_celViewWeaponOutlineAlpha;	// first-person weapon outline opacity
extern idCVar r_celShadingWorldWidth;	// screen-space world outline radius in pixels
extern idCVar r_celShadingWorldAlpha;	// screen-space world outline opacity
extern idCVar r_celShadingWorldDepthThreshold;	// relative depth discontinuity threshold
extern idCVar r_celShadingWorldNormalThreshold;	// surface crease threshold
extern idCVar r_celShadingWorldDebug;	// draw world cel edges over a flat background

extern idCVar r_ignore;					// used for random debugging without defining new vars
extern idCVar r_ignore2;				// used for random debugging without defining new vars
extern idCVar r_znear;					// near Z clip plane
extern idCVar cl_gunfov;				// first-person weapon FOV override
extern idCVar cl_gunfov_adjust;		// weapon FOV aspect policy

extern idCVar r_finish;					// force a call to glFinish() every frame
extern idCVar r_frontBuffer;			// draw to front buffer for debugging
extern idCVar r_swapInterval;			// controls platform swap interval / VSync
extern idCVar r_disableVSyncDuringLevelLoad;	// temporarily bypass VSync while drawing blocking loading screens
extern idCVar r_offsetFactor;			// polygon offset parameter
extern idCVar r_offsetUnits;			// polygon offset parameter
extern idCVar r_singleTriangle;			// only draw a single triangle per primitive
extern idCVar r_logFile;				// number of frames to emit GL logs
extern idCVar r_clear;					// force screen clear every frame
extern idCVar r_shadows;				// enable shadows
extern idCVar r_subviewOnly;			// 1 = don't render main view, allowing subviews to be debugged
extern idCVar r_lightScale;				// all light intensities are multiplied by this, which is normally 2
extern idCVar r_lightDetailLevel;		// minimum light detailLevel to include in view lists
extern idCVar r_flareSize;				// scale the flare deforms from the material def

extern idCVar r_gamma;					// changes gamma tables
extern idCVar r_brightness;				// changes gamma tables

extern idCVar r_renderer;				// arb, nv10, nv20, r200, gl2, etc
extern idCVar r_actualRenderer;			// actual active renderer backend after fallback
extern idCVar r_glTier;					// auto, legacy, gl33, gl41, gl43, gl45, gl46
extern idCVar r_glesContext;			// request an OpenGL ES 3.0 context (Android GLES backend bring-up)
extern idCVar r_glCoreProfileFirst;		// try core-profile contexts before the compatibility fallback
extern idCVar r_glDebugContext;			// request a debug GL context when the platform backend supports it
extern idCVar r_glDebugOutput;			// report driver debug messages when a debug context is active
extern idCVar r_glDebugSynchronous;		// synchronously deliver GL debug callbacks for diagnostics
extern idCVar r_rendererMetrics;			// 0 off, 1 summary, 2 verbose per-frame/pass metrics
extern idCVar r_rendererGpuTimers;		// delayed whole-frame GPU timing; GL pass detail also requires renderer metrics
extern idCVar r_rendererBenchmarkPreset;	// benchmark budget preset
extern idCVar r_rendererPerfThresholdP95;	// custom P95 benchmark threshold in milliseconds
extern idCVar r_rendererPerfThresholdP99;	// custom P99 benchmark threshold in milliseconds
extern idCVar r_rendererAdaptiveClusterGrid;	// use preset-driven cluster-grid dimensions
void R_SetLoadingScreenSwapIntervalBypass( bool active );
int R_GetEffectiveSwapInterval( void );
extern idCVar r_rendererDynamicResolution;	// automatic 3D scale from delayed GPU time
extern idCVar r_rendererUploadMegs;		// dynamic upload stream size in megabytes per frame buffer
extern idCVar r_rendererUploadFrameBuffers;	// dynamic upload stream frame-buffer rotation depth
extern idCVar r_rendererUploadPersistent;	// allow persistent-mapped dynamic upload stream
extern idCVar r_rendererUploadBufferPool;	// recycle static GL buffer names instead of gen/data/delete churn
extern idCVar r_rendererModernQuality;	// master permit/rollback for Milestone-F material and lighting domains
extern idCVar r_rendererReflectionProbes;	// opt-in authored clustered reflection probes
extern idCVar r_rendererClusteredDecals;	// opt-in atomic clustered decal ownership
extern idCVar r_rendererFroxelVolumetrics;	// opt-in bounded view-aligned volumetric integration
extern idCVar r_rendererSSR;			// opt-in bounded screen-space reflections
extern idCVar r_rendererSSGI;			// opt-in bounded screen-space diffuse GI
extern idCVar r_rendererModernExecutor;	// opt-in modern GL executor prepare path
extern idCVar r_rendererModernSubmit;	// opt-in modern GL draw submission before ARB2 fallback
extern idCVar r_rendererGpuValidation;	// compare GL43 GPU-driven compute results against CPU reference data
extern idCVar r_rendererGpuValidationReadbackDelay;	// defer opt-in GL43 validation readback polling
extern idCVar r_rendererBindless;	// opt-in experimental bindless texture diagnostics, disabled by default
extern idCVar r_rendererModernVisible;	// opt-in modern hybrid visible-frame composition
extern idCVar r_rendererSharedGui;	// opt-in backend-neutral fixed-function 2D GUI ownership
extern idCVar r_rendererSharedInWorldGui;	// opt-in backend-neutral in-world GUI ownership
extern idCVar r_rendererSharedCinematicPost;	// opt-in backend-neutral cinematic/post transaction ownership
extern idCVar r_rendererSharedSpecialFrame;	// opt-in backend-neutral render-demo/Raven special-frame transaction ownership
extern idCVar r_rendererSharedWorldAmbient;	// opt-in backend-neutral whole-view world ambient/material ownership
extern idCVar r_rendererSharedWorldInteraction;	// opt-in backend-neutral whole-view unshadowed interaction ownership
extern idCVar r_rendererSharedWorldFogBlend;	// opt-in backend-neutral whole-view fog/blend ownership
extern idCVar r_rendererSharedSubview;	// opt-in backend-neutral direct/capture special-subview ownership
extern idCVar r_rendererSharedDeform;	// opt-in backend-neutral material-deform ownership
extern idCVar r_vkShadowFallbackTest;	// diagnostic-only forced Vulkan unshadowed receiver fallback
extern idCVar r_rendererModernLightingParity;	// diagnostic override forcing lighting-ownership parity contracts proven
extern idCVar r_rendererModernAutoPromote;	// allow gated default modern-visible promotion
extern idCVar r_rendererPromotionEvidence;	// Phase 8 evidence token required before auto-promotion
extern idCVar r_rendererShaderReload;	// allow runtime reload of the internal modern GL shader library
extern idCVar r_rendererModernVisibleDepth;	// opt-in graph-backed modern depth/shadow-depth execution
extern idCVar r_rendererModernDepthDebug;	// show graph-backed modern depth resources as a debug overlay
extern idCVar r_rendererModernOpaque;	// opt-in graph-backed modern opaque G-buffer execution
extern idCVar r_rendererModernGBufferDebug;	// show graph-backed modern G-buffer attachments as a debug overlay
extern idCVar r_rendererModernDeferred;	// opt-in graph-backed modern deferred light resolve execution
extern idCVar r_rendererModernDeferredDebug;	// show graph-backed modern deferred resolve debug output
extern idCVar r_rendererForwardPlus;	// opt-in graph-backed modern clustered forward+ execution
extern idCVar r_rendererClusterDebug;	// show modern clustered light bins as a debug overlay
extern idCVar r_rendererOcclusion;	// enable conservative modern visibility and occlusion culling
extern idCVar r_rendererHiZ;	// allocate and build the modern scene Hi-Z depth pyramid
extern idCVar r_useSimpleInteraction;	// use the simpler Quake 4 interaction program pair as a compatibility fallback
extern idCVar r_interactionColorMode;	// interaction color mode: 0 auto, 1 packed env16.xy, 2 vector env16/env17
extern idCVar r_appleARB2Interactions;	// Apple GL 2.1: 0 automatic GLSL/simple fallback, 1 simple diagnostic, 2 full diagnostic, 3 emergency bypass
extern idCVar r_forceAppleGL21InteractionCorridor;	// treat a GL 2.1 compatibility context as the Apple corridor on non-Apple hosts (reproduction only)
extern idCVar r_shaderReport;			// shader diagnostics: 0 off, 1 summaries, 2 invalid-use warnings

extern idCVar r_cgVertexProfile;		// arbvp1, vp20, vp30
extern idCVar r_cgFragmentProfile;		// arbfp1, fp30

extern idCVar r_checkBounds;			// compare all surface bounds with precalculated ones

extern idCVar r_useNV20MonoLights;		// 1 = allow an interaction pass optimization
extern idCVar r_useLightPortalFlow;		// 1 = do a more precise area reference determination
extern idCVar r_useTripleTextureARB;	// 1 = cards with 3+ texture units do a two pass instead of three pass
extern idCVar r_useShadowSurfaceScissor;// 1 = scissor shadows by the scissor rect of the interaction surfaces
extern idCVar r_useConstantMaterials;	// 1 = use pre-calculated material registers if possible
extern idCVar r_useInteractionTable;	// create a full entityDefs * lightDefs table to make finding interactions faster
extern idCVar r_useNodeCommonChildren;	// stop pushing reference bounds early when possible
extern idCVar r_useSilRemap;			// 1 = consider verts with the same XYZ, but different ST the same for shadows
extern idCVar r_useCulling;				// 0 = none, 1 = sphere, 2 = sphere + box
extern idCVar r_useLightCulling;		// 0 = none, 1 = box, 2 = exact clip of polyhedron faces
extern idCVar r_useLightScissors;		// 1 = use custom scissor rectangle for each light
extern idCVar r_useClippedLightScissors;// 0 = full screen when near clipped, 1 = exact when near clipped, 2 = exact always
extern idCVar r_useEntityCulling;		// 0 = none, 1 = box
extern idCVar r_useEntityScissors;		// 1 = use custom scissor rectangle for each entity
extern idCVar r_useInteractionCulling;	// 1 = cull interactions
extern idCVar r_useInteractionScissors;	// 1 = use a custom scissor rectangle for each interaction
extern idCVar r_limitBatchSize;		// interaction/shadow batches at or below this many indexes are skipped
extern idCVar r_useFrustumFarDistance;	// if != 0 force the view frustum far distance to this distance
extern idCVar r_useShadowCulling;		// try to cull shadows from partially visible lights
extern idCVar r_usePreciseTriangleInteractions;	// 1 = do winding clipping to determine if each ambiguous tri should be lit
extern idCVar r_useTurboShadow;			// 1 = use the infinite projection with W technique for dynamic shadows
extern idCVar r_useExternalShadows;		// 1 = skip drawing caps when outside the light volume
extern idCVar r_useOptimizedShadows;	// 1 = use the dmap generated static shadow volumes
extern idCVar r_useShadowVertexProgram;	// 1 = do the shadow projection in the vertex program on capable cards
extern idCVar r_useShadowProjectedCull;	// 1 = discard triangles outside light volume before shadowing
extern idCVar r_useTrueTypeFonts;		// 1 = render GUI text from the shipped .ttf faces instead of the bitmap atlases
extern idCVar r_ttfFontResolution;		// multiplier on the rasterisation resolution of the TrueType glyph atlases
extern idCVar r_ttfFontDebug;			// 1 = dump each TrueType glyph atlas to fs_savepath and log its layout
extern idCVar r_useShadowMap;			// 1 = use a simple shadow-map path for projected and point lights when supported
extern idCVar r_shadowMapCSM;			// 1 = use projected-light cascaded shadow maps when shadow maps are enabled
extern idCVar r_shadowMapHashedAlpha;		// 1 = use hashed alpha testing for perforated shadow-map casters when available
extern idCVar r_shadowMapTranslucentMoments;	// 1 = accumulate experimental translucent shadow moments for blended casters
extern idCVar r_shadowMapTranslucentDensity;	// density scale applied when resolving translucent shadow moments
extern idCVar r_shadowMapTranslucentMinAlpha;	// minimum per-stage alpha considered by translucent shadow moments
extern idCVar r_shadowMapCustomGLSLReceiverWrapper;	// 1 = map compatible custom GLSL receivers through stock interactions
extern idCVar r_shadowMapReport;		// 0 = off, 1 = per-view summary, 2 = per-light joined planner/ARB2/modern decisions, 3 = verbose receiver-submit decisions
extern idCVar r_shadowMapModernStrict;	// fail-visible modern shadow contract breaks (black light instead of silently unshadowed)
extern idCVar r_shadowMapTranslucentReceivers;	// translucent surfaces sample the shadow map like opaque receivers
extern idCVar r_shadowMapSubviewPolicy;	// 0 = full renders in subviews, 1 = reuse-or-stencil, 2 = always stencil
extern idCVar r_shadowMapReportInterval;	// frames between shadow-map diagnostic reports
extern idCVar r_shadowMapConservativeCasters;	// 1 = keep shadow-map caster submission independent from visible receiver scissors
extern idCVar r_shadowMapProjectedCSM;	// 1 = allow ordinary projected lights to use CSM when r_shadowMapCSM is enabled
extern idCVar r_shadowMapDepthCompare;	// 1 = use sampler2DShadow compare mode for projected depth maps
extern idCVar r_shadowMapTexelBiasScale;	// texel-aware receiver bias scale
extern idCVar r_shadowMapReceiverPlaneBias;	// 1 = add derivative receiver-plane depth bias for wider filters
extern idCVar r_shadowMapFilterTaps;		// projected-light PCF tap budget
extern idCVar r_shadowMapPointFilterTaps;	// point-light PCF tap budget
extern idCVar r_shadowMapFilterMode;	// projected-light filter mode
extern idCVar r_shadowMapPointFilterMode;	// point-light filter mode
extern idCVar r_shadowMapDistantFilterScale;	// filter-radius scale for parallel/global projected sources
extern idCVar r_shadowMapPCSSLightRadius;	// projected PCSS-lite blocker search radius
extern idCVar r_shadowMapPCSSMaxRadius;	// projected PCSS-lite maximum filter radius
extern idCVar r_shadowMapNormalOffsetScale;	// normal-offset receiver bias in shadow texels
extern idCVar r_shadowMapCasterCulling;	// caster face culling: 0 = two-sided, 1 = near shell, 2 = topology-aware automatic
extern idCVar r_shadowMapPointHighPrecision;	// 1 = store point shadow depth as high-precision float color
extern idCVar r_shadowMapPointLights;	// 1 = shadow-map point lights (the dominant Q4 light class); 0 = stencil fallback for point lights
extern idCVar r_shadowMapPointSize;	// point-light cube face resolution, separate from r_shadowMapSize
extern idCVar r_shadowMapAtlasSize;	// shared projected-light shadow atlas edge size
extern idCVar r_shadowMapPointDepthCompare;	// 1 = use samplerCubeShadow depth compare for point maps when supported
extern idCVar r_shadowMapStableAlphaHash;	// 1 = seed hashed alpha from stable world coordinates
extern idCVar r_shadowMapMaxUpdatesPerView;	// shadow-map pass budget per backend view, 0 = unlimited
extern idCVar r_shadowMapSkipStencilShadows;	// 1 = skip stencil volume generation/linking for lights that will render shadow maps

// mirrors the backend shadow-map admission policy for front-end stencil-volume elision (tr_light.cpp)
bool R_ShadowMapLightWillUseShadowMaps( const idRenderLightLocal *lightDef );
// backend-published: shadow-map resources were successfully prepared this video generation (draw_arb2.cpp)
bool RB_ShadowMapResourcesKnownGood( bool pointLight );
extern idCVar r_shadowMapStaticCache;		// 1 = reuse resident static-only shadow-map passes
extern idCVar r_shadowMapStaticHysteresisFrames;	// frames after dynamic casters before static reuse
extern idCVar r_shadowMapResidentFrames;	// frames a resident static shadow map may remain unused
extern idCVar r_shadowMapProjectedCacheSize;	// projected shadow-map static cache slots
extern idCVar r_shadowMapPointCacheSize;	// point shadow-map static cache slots
extern idCVar r_shadowMapCacheCSM;		// 1 = allow CSM/static cache reuse
extern idCVar r_shadowMapTranslucentFilterRadius;	// translucent moment filter radius, -1 = inherit opaque radius
extern idCVar r_shadowMapTranslucentMinVariance;	// minimum variance for translucent moment resolve
extern idCVar r_shadowMapTranslucentBleedReduction;	// moment light-bleed reduction
extern idCVar r_shadowMapGpuSyncTimings;	// 1 = glFinish-bracket shadow-map passes for GPU-synchronized diagnostics
extern idCVar r_shadowMapGpuTimerQueries;	// 1 = use GL timer queries for shadow-map GPU diagnostics when available
extern idCVar r_softParticles;			// 1 = depth-fade eligible BSE particle surfaces against the opaque scene depth
extern idCVar r_softParticleFadeDistance;	// world-unit fade distance for r_softParticles
extern idCVar r_enhancedMaterials;		// 1 = use enhanced GLSL interaction shading for stock materials when supported
extern idCVar r_enhancedMaterialNormalScale;	// tangent-space normal XY scale when enhanced material shading is enabled
extern idCVar r_enhancedMaterialSpecularBoost;	// specular intensity scale when enhanced material shading is enabled
extern idCVar r_enhancedMaterialFresnel;	// grazing-angle fresnel contribution when enhanced material shading is enabled
extern idCVar r_useDeferredTangents;	// 1 = don't always calc tangents after deform
extern idCVar r_useCachedDynamicModels;	// 1 = cache snapshots of dynamic models
extern idCVar r_useRepeatedStateReuse;	// 1 = keep model-space dynamic snapshots across transform-only entity updates
extern idCVar r_useNewSkinning;		// 1 = use retail Quake 4's SIMD-ready MD5 skinning path
extern idCVar r_useFastSkinning;	// 1 = approximate MD5 skinning with single-joint tangent transforms
extern idCVar r_deriveBiTangents;	// 1 = derive bitangents from normal/tangent after skinning
extern idCVar r_forceConvertMD5R;	// 1 = force source-model / source-proc loading instead of prebuilt MD5R companions
extern idCVar r_convertMD5toMD5R;	// 1 = convert authored MD5 meshes to packed MD5R form when supported
extern idCVar r_convertStaticToMD5R;	// 1 = convert static renderer models to packed MD5R form when supported
extern idCVar r_convertProcToMD5R;	// 1 = convert classic proc worlds to packed MD5R proc data when supported
bool					R_IsMD5RRuntimeAvailable( void );
bool					R_IsMD5RWriteAvailable( void );
void					R_DisableUnavailableMD5RCVar( idCVar &cvar, const char *capabilityName );
extern idCVar r_lod_animations_distance;	// distance gate for animation-update LOD
extern idCVar r_lod_animations_wait;	// delay before reusing the last animated pose when LODing
extern idCVar r_lod_animations_coverage;	// screen-coverage threshold for animation-update LOD
extern idCVar r_lod_entities;			// 1 = enable retail-style entity scissor LOD gating
extern idCVar r_lod_entities_percent;	// screen-coverage threshold for retaining ambient entity submissions
extern idCVar r_lod_shadows;			// 1 = enable retail-style shadow LOD gating
extern idCVar r_lod_shadows_percent;	// screen-coverage threshold for retaining interaction shadows
extern idCVar r_useTwoSidedStencil;		// 1 = do stencil shadows in one pass with different ops on each side
extern idCVar r_stencilTranslucentShadows;	// 1 = let translucent materials cast and receive stencil shadows in the stencil-volume path
extern idCVar r_useInfiniteFarZ;		// 1 = use the no-far-clip-plane trick
extern idCVar r_useScissor;				// 1 = scissor clip as portals and lights are processed
extern idCVar r_usePortals;				// 1 = use portals to perform area culling, otherwise draw everything
extern idCVar r_portalsDistanceCull;	// 1 = enable distance-cull checks from portal fade data
extern idCVar r_useStateCaching;		// avoid redundant state changes in GL_*() calls
extern idCVar r_useRedundantStateFiltering;	// skip redundant legacy-backend env params, attrib toggles, and buffer rebinds
extern idCVar r_useCombinerDisplayLists;// if 1, put all nvidia register combiner programming in display lists
extern idCVar r_useVertexBuffers;		// if 0, don't use ARB_vertex_buffer_object for vertexes
extern idCVar r_useIndexBuffers;		// if 0, don't use ARB_vertex_buffer_object for indexes
extern idCVar r_useEntityCallbacks;		// if 0, issue the callback immediately at update time, rather than defering
extern idCVar r_lightAllBackFaces;		// light all the back faces, even when they would be shadowed
extern idCVar r_useDepthBoundsTest;     // use depth bounds test to reduce shadow fill

extern idCVar r_skipPostProcess;		// skip all post-process renderings
extern idCVar r_skipGlowOverlay;		// skip glow overlay material stages
extern idCVar r_skipSuppress;			// ignore the per-view suppressions
extern idCVar r_skipInteractions;		// skip all light/surface interaction drawing
extern idCVar r_skipFrontEnd;			// bypasses all front end work, but 2D gui rendering still draws
extern idCVar r_skipBackEnd;			// don't draw anything
extern idCVar r_skipCopyTexture;		// do all rendering, but don't actually copyTexSubImage2D
extern idCVar r_skipRender;				// skip 3D rendering, but pass 2D
extern idCVar r_skipRenderContext;		// NULL the rendering context during backend 3D rendering
extern idCVar r_skipTranslucent;		// skip the translucent interaction rendering
extern idCVar r_skipAmbient;			// bypasses all non-interaction drawing
extern idCVar r_skipNewAmbient;			// bypasses all vertex/fragment program ambients
extern idCVar r_forceAmbient;			// lifts the final scene toward a minimum brightness
extern idCVar r_useLightGrid;			// enable indirect diffuse from precomputed irradiance volumes
extern idCVar r_lightGridIntensity;		// scales baked light-grid indirect diffuse contribution
extern idCVar r_lightGridVisibilityFloor;	// minimum light-grid probe visibility after falloff
extern idCVar r_lightGridIrradianceGamma;	// gamma decode for baked LDR light-grid irradiance
extern idCVar r_lightGridMaxContribution;	// maximum light-grid contribution before bloom/HDR
extern idCVar r_lightGridReport;		// print light-grid receiver statistics every N frames while enabled
extern idCVar r_lightGridDebug;			// debug baked light-grid indirect pass output
extern idCVar r_lightGridDepthBiasFactor;	// polygon offset factor for light-grid overlay
extern idCVar r_lightGridDepthBiasUnits;	// polygon offset units for light-grid overlay
extern idCVar r_lightGridDepthTolerance;	// depth texture tolerance for light-grid receiver clipping
extern idCVar r_lightGridPortalBlend;	// world-unit blend radius for light-grid sampling across visible portal boundaries
extern idCVar r_lightGridPreload;		// preload all light-grid atlases during map load instead of streaming visible areas
extern idCVar r_lightGridResidencyFrames;	// keep light-grid atlases resident after visible/neighbor use; 0 disables runtime purging
extern idCVar r_lightGridBakeWorkers;	// worker thread count for CPU probe integration (-1 = disabled, 0 = auto)
extern idCVar r_lightGridBakeAsyncReadback;	// use async pixel-pack-buffer readback during light-grid baking when supported
extern idCVar r_lightGridBakeMemoryMB;	// transient memory budget for in-flight light-grid bake jobs
extern idCVar r_lightGridBakeReadbackSlots;	// async readback slot count for light-grid baking (0 = auto)
extern idCVar r_skipBlendLights;		// skip all blend lights
extern idCVar r_skipFogLights;			// skip all fog lights
extern idCVar r_skipPlayerVisibilityEffects;	// skip the player brightskin / rimlight / outline overlays

// Player rimlight shaping ladder, shared by the cvar registration and the pass
// that feeds glprogs/player_rimlight.fs. The defaults reproduce the squared
// falloff the pass used to hard-code, so an untouched config looks unchanged.
const float RB_PLAYER_RIMLIGHT_MIN_POWER = 0.25f;
const float RB_PLAYER_RIMLIGHT_MAX_POWER = 8.0f;
const float RB_PLAYER_RIMLIGHT_MIN_FLOOR = 0.0f;
const float RB_PLAYER_RIMLIGHT_MAX_FLOOR = 1.0f;

extern idCVar r_playerRimlightPower;	// player rimlight falloff exponent
extern idCVar r_playerRimlightFloor;	// player rimlight floor, keeps a sliver of colour on camera-facing surfaces
extern idCVar r_skipSubviews;			// 1 = don't render any mirrors / cameras / etc
extern idCVar r_skipGuiShaders;			// 1 = don't render any gui elements on surfaces
extern idCVar r_skipParticles;			// 1 = don't render any particles
extern idCVar r_skipUpdates;			// 1 = don't accept any entity or light updates, making everything static
extern idCVar r_skipEntities;			// 1 = skip non-world render entities
extern idCVar r_skipDeforms;			// leave all deform materials in their original state
extern idCVar r_skipDynamicTextures;	// don't dynamically create textures
extern idCVar r_skipLightScale;			// don't do any post-interaction light scaling, makes things dim on low-dynamic range cards
extern idCVar r_skipBump;				// uses a flat surface instead of the bump map
extern idCVar r_skipSpecular;			// use black for specular
extern idCVar r_skipDiffuse;			// use black for diffuse
extern idCVar r_skipDecals;			// skip decal surfaces
extern idCVar r_skipOverlays;			// skip overlay surfaces
extern idCVar r_skipROQ;

extern idCVar r_ignoreGLErrors;
extern idCVar image_ignoreHighQuality;
extern idCVar image_useETC2;			// compress to ETC2 where the driver exposes no S3TC

extern idCVar r_forceLoadImages;		// draw all images to screen after registration
extern idCVar r_demonstrateBug;			// used during development to show IHV's their problems
extern idCVar r_screenFraction;			// for testing fill rate, the resolution of the entire screen can be changed
extern idCVar r_resolutionScaleMode;		// upscale path when r_screenFraction < 100
extern idCVar r_resolutionScaleSharpness;	// sharpen amount for HQ resolution scaling

extern idCVar r_showUnsmoothedTangents;	// highlight geometry rendered with unsmoothed tangents
extern idCVar r_showSilhouette;			// highlight edges that are casting shadow planes
extern idCVar r_reportSilhouetteEdgeWarnings;	// report duplicate/tripled silhouette-edge topology diagnostics
extern idCVar r_showVertexColor;		// draws all triangles with the solid vertex color
extern idCVar r_showUpdates;			// report entity and light updates and ref counts
extern idCVar r_showDemo;				// report reads and writes to the demo file
extern idCVar r_showDynamic;			// report stats on dynamic surface generation
extern idCVar r_showLightScale;			// report the scale factor applied to drawing for overbrights
extern idCVar r_showIntensity;			// draw the screen colors based on intensity, red = 0, green = 128, blue = 255
extern idCVar r_showDefs;				// report the number of modeDefs and lightDefs in view
extern idCVar r_showTrace;				// show the intersection of an eye trace with the world
extern idCVar r_showSmp;				// show which end (front or back) is blocking
extern idCVar r_showDepth;				// display the contents of the depth buffer and the depth range
extern idCVar r_showImages;				// draw all images to screen instead of rendering
extern idCVar r_showTris;				// enables wireframe rendering of the world
extern idCVar r_showSurfaceInfo;		// show surface material name under crosshair
extern idCVar r_showNormals;			// draws wireframe normals
extern idCVar r_showEdges;				// draw the sil edges
extern idCVar r_showViewEntitys;		// displays the bounding boxes of all view models and optionally the index
extern idCVar r_showTexturePolarity;	// shade triangles by texture area polarity
extern idCVar r_showTangentSpace;		// shade triangles by tangent space
extern idCVar r_showDominantTri;		// draw lines from vertexes to center of dominant triangles
extern idCVar r_showTextureVectors;		// draw each triangles texture (tangent) vectors
extern idCVar r_showLights;				// 1 = print light info, 2 = also draw volumes
extern idCVar r_showViewLights;			// print detailed light info for lights affecting the current view origin
extern idCVar r_showViewLightsInterval;	// frames between repeated view-origin light reports
extern idCVar r_showViewLightsVisuals;	// draw persistent debug overlays for the last reported view-origin lights
extern idCVar r_showLightGrid;			// show loaded irradiance-volume probe positions
extern idCVar r_showLightCount;			// colors surfaces based on light count
extern idCVar r_showShadows;			// visualize the stencil shadow volumes
extern idCVar r_showShadowCount;		// colors screen based on shadow volume depth complexity
extern idCVar r_showLightScissors;		// show light scissor rectangles
extern idCVar r_showEntityScissors;		// show entity scissor rectangles
extern idCVar r_showInteractionFrustums;// show a frustum for each interaction
extern idCVar r_showInteractionScissors;// show screen rectangle which contains the interaction frustum
extern idCVar r_showMemory;				// print frame memory utilization
extern idCVar r_showCull;				// report sphere and box culling stats
extern idCVar r_showInteractions;		// report interaction generation activity
extern idCVar r_showSurfaces;			// report surface/light/shadow counts
extern idCVar r_showViewBuildTimes;		// print CPU timings for render-view construction
extern idCVar r_showViewBuildTimesInterval; // frames between render-view timing reports
extern idCVar r_showPrimitives;			// report vertex/index/draw counts
extern idCVar r_showPortals;			// draw portal outlines in color based on passed / not passed
extern idCVar r_showAlloc;				// report alloc/free counts
extern idCVar r_showSkel;				// draw the skeleton when model animates
extern idCVar r_showOverDraw;			// show overdraw
extern idCVar r_jointNameScale;			// size of joint names when r_showskel is set to 1
extern idCVar r_jointNameOffset;		// offset of joint names when r_showskel is set to 1

extern idCVar r_testGamma;				// draw a grid pattern to test gamma levels
extern idCVar r_testStepGamma;			// draw a grid pattern to test gamma levels
extern idCVar r_testGammaBias;			// draw a grid pattern to test gamma levels

extern idCVar r_testARBProgram;			// experiment with vertex/fragment programs

extern idCVar r_singleLight;			// suppress all but one light
extern idCVar r_singleEntity;			// suppress all but one entity
extern idCVar r_singleArea;				// only draw the portal area the view is actually in
extern idCVar r_singleSurface;			// suppress all but one surface on each entity
extern idCVar r_shadowPolygonOffset;	// bias value added to depth test for stencil shadow drawing
extern idCVar r_shadowPolygonFactor;	// scale value for stencil shadow drawing
extern idCVar r_shadowMapSize;			// square resolution used for simple shadow maps
extern idCVar r_shadowMapBias;			// constant receiver depth bias used by projected shadow maps
extern idCVar r_shadowMapNormalBias;		// slope-aware receiver bias used by projected shadow maps
extern idCVar r_shadowMapPointBias;		// constant receiver depth bias used by point-light shadow maps
extern idCVar r_shadowMapPointNormalBias;	// slope-aware receiver bias used by point-light shadow maps
extern idCVar r_shadowMapPointMaxWorldBias;	// combined point receiver depth/normal-offset cap in world units
extern idCVar r_shadowMapFilterRadius;	// projected-light PCF radius in texels used by simple shadow maps
extern idCVar r_shadowMapPointFilterRadius;	// point-light PCF radius in texels used by simple shadow maps
extern idCVar r_shadowMapProjectionPad;	// normalized padding applied around projected-light shadow-map coverage
extern idCVar r_shadowMapCascadeCount;		// number of projected-light cascades used when CSM is enabled
extern idCVar r_shadowMapCascadeDistance;	// camera distance covered by cropped projected-light cascades
extern idCVar r_shadowMapCascadeLambda;		// uniform/log split blend for projected-light cascades
extern idCVar r_shadowMapCascadeBlend;		// transition width between projected-light cascades
extern idCVar r_shadowMapDebugOverlay;		// 1 = show the selected shadow map as a top-left overlay with frame stats
typedef enum {
	SHADOWMAP_DEBUGMODE_OFF = 0,
	SHADOWMAP_DEBUGMODE_ATLAS,
	SHADOWMAP_DEBUGMODE_CASCADE_INDEX,
	SHADOWMAP_DEBUGMODE_PROJECTED_UV,
	SHADOWMAP_DEBUGMODE_PROJECTED_DEPTH,
	SHADOWMAP_DEBUGMODE_PROJECTED_W,
	SHADOWMAP_DEBUGMODE_INVALID_MASK,
	SHADOWMAP_DEBUGMODE_BIAS_HEATMAP,
	SHADOWMAP_DEBUGMODE_BIAS_OFF,
	SHADOWMAP_DEBUGMODE_PCF_OFF,
	SHADOWMAP_DEBUGMODE_CASTER_OFFSET_OFF,
	SHADOWMAP_DEBUGMODE_RECEIVER_PLANE_BIAS_OFF,
	SHADOWMAP_DEBUGMODE_COMPARE_DELTA,
	SHADOWMAP_DEBUGMODE_RECEIVER_ELIGIBILITY,
	SHADOWMAP_DEBUGMODE_RECEIVER_FALLBACK_REASON,
	SHADOWMAP_DEBUGMODE_COUNT
} shadowMapDebugMode_t;
extern idCVar r_shadowMapDebugMode;		// projected shadow-map visualization mode
extern idCVar r_shadowMapCascadeStabilize;	// snap projected-light cascade bounds to texels
extern idCVar r_shadowMapPointFarScale;	// range padding multiplier used by point-light shadow maps
extern idCVar r_shadowMapPolygonFactor;	// slope-scale bias used when rendering simple shadow maps
extern idCVar r_shadowMapPolygonOffset;	// constant bias used when rendering simple shadow maps

extern idCVar r_jitter;					// randomly subpixel jitter the projection matrix
extern idCVar r_lightSourceRadius;		// for soft-shadow sampling
extern idCVar r_lockSurfaces;
extern idCVar r_orderIndexes;			// perform index reorganization to optimize vertex use

extern idCVar r_debugLineDepthTest;		// perform depth test on debug lines
extern idCVar r_debugLineWidth;			// width of debug lines
extern idCVar r_debugArrowStep;			// step size of arrow cone line rotation in degrees
extern idCVar r_debugPolygonFilled;

extern idCVar r_materialOverride;		// override all materials

extern idCVar r_debugRenderToTexture;

/*
====================================================================

GL wrapper/helper functions

====================================================================
*/

void	GL_SelectTexture( int unit );
void	GL_CheckErrors( void );
void	GL_ClearStateDelta( void );
void	GL_State( int stateVector );
void	GL_TexEnv( int env );
void	GL_Cull( int cullType );

const int GLS_SRCBLEND_ZERO						= 0x00000001;
const int GLS_SRCBLEND_ONE						= 0x0;
const int GLS_SRCBLEND_DST_COLOR				= 0x00000003;
const int GLS_SRCBLEND_ONE_MINUS_DST_COLOR		= 0x00000004;
const int GLS_SRCBLEND_SRC_ALPHA				= 0x00000005;
const int GLS_SRCBLEND_ONE_MINUS_SRC_ALPHA		= 0x00000006;
const int GLS_SRCBLEND_DST_ALPHA				= 0x00000007;
const int GLS_SRCBLEND_ONE_MINUS_DST_ALPHA		= 0x00000008;
const int GLS_SRCBLEND_ALPHA_SATURATE			= 0x00000009;
const int GLS_SRCBLEND_SRC_COLOR				= 0x0000000a;
const int GLS_SRCBLEND_ONE_MINUS_SRC_COLOR		= 0x0000000b;
const int GLS_SRCBLEND_BITS						= 0x0000000f;

const int GLS_DSTBLEND_ZERO						= 0x0;
const int GLS_DSTBLEND_ONE						= 0x00000020;
const int GLS_DSTBLEND_SRC_COLOR				= 0x00000030;
const int GLS_DSTBLEND_ONE_MINUS_SRC_COLOR		= 0x00000040;
const int GLS_DSTBLEND_SRC_ALPHA				= 0x00000050;
const int GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA		= 0x00000060;
const int GLS_DSTBLEND_DST_ALPHA				= 0x00000070;
const int GLS_DSTBLEND_ONE_MINUS_DST_ALPHA		= 0x00000080;
const int GLS_DSTBLEND_BITS						= 0x000000f0;

// Straight-alpha retained images write source-over coverage to an isolated
// target's alpha channel, while RGB keeps the ordinary SRC_ALPHA factor.
const int GLS_ALPHA_COVERAGE = 0x00040000;


// these masks are the inverse, meaning when set the glColorMask value will be 0,
// preventing that channel from being written
const int GLS_DEPTHMASK							= 0x00000100;
const int GLS_REDMASK							= 0x00000200;
const int GLS_GREENMASK							= 0x00000400;
const int GLS_BLUEMASK							= 0x00000800;
const int GLS_ALPHAMASK							= 0x00001000;
const int GLS_COLORMASK							= (GLS_REDMASK|GLS_GREENMASK|GLS_BLUEMASK);

const int GLS_POLYMODE_LINE						= 0x00002000;

const int GLS_DEPTHFUNC_ALWAYS					= 0x00010000;
const int GLS_DEPTHFUNC_EQUAL					= 0x00020000;
const int GLS_DEPTHFUNC_LESS					= 0x0;

const int GLS_ATEST_EQ_255						= 0x10000000;
const int GLS_ATEST_LT_128						= 0x20000000;
const int GLS_ATEST_GE_128						= 0x40000000;
const int GLS_ATEST_BITS						= 0x70000000;

const int GLS_DEFAULT							= GLS_DEPTHFUNC_ALWAYS;

void R_Init( void );
void R_InitOpenGL( void );
// Renderer-private recoverable device route. The caller must invoke between
// submitted frames. A refused attempt remains uninitialized and can be retried
// with an explicit restore request; process-wide allocation failure is excluded.
struct renderWindowRequest_s;
bool R_TryFullVidRestart( const renderWindowRequest_s *request, char *error, int errorSize );
bool R_TryInitializeDisplay( const renderWindowRequest_s *request, char *error, int errorSize );
bool R_IsRecoverableRendererRestart( void );
bool R_ForceWindowForRendererRestart( void );
const renderWindowRequest_s *R_GetRecoverableWindowRequest( void );
// Does nothing during legacy startup; rejects a recoverable attempt without
// entering Common::Error/FatalError and their Session/process teardown paths.
void R_RejectRecoverableRendererRestart( const char *reason );

// publish glConfig's compression capabilities into this binary's imagetools
// copy; every backend that fills glConfig must call it
void R_PublishCompressionCapsToImageTools( void );

void R_InitFreeType( void );
void R_DoneFreeType( void );

// Scalable font path: rasterises the shipped .ttf faces at the display's own
// resolution instead of scaling up the fixed 12/24/48 point retail atlases.
// Returns false when no usable face exists, leaving the bitmap path in charge.
bool R_RegisterTrueTypeFont( const char *fontName, fontInfoEx_t &font );
// Rebuilds the fixed-cell 'bigchars' console sheet at display resolution and
// retargets its material, so the console and loading screen sharpen too.
bool R_BuildConsoleFontAtlas( void );
void R_RefreshConsoleFontAtlas( void );
void R_SyncFontCodePage( void );
// The generated atlas materials have no file behind them, so a level load purge
// would otherwise leave every glyph sampling the transparent default image.
void R_TTFRestoreAtlasMaterials( void );
void R_ShutdownTrueTypeFonts( void );

void R_SetColorMappings( void );

void R_ScreenShot_f( const idCmdArgs &args );
void R_StencilShot( void );

bool R_CheckExtension( char *name );

// raw glBindTexture that keeps the per-TMU redundant-bind tracker coherent;
// REQUIRED for any direct bind outside idImage::Bind() (framebuffer copies,
// uploads, sampler-state changes) or a later filtered Bind() can silently
// leave the wrong texture bound
void R_BindTextureForDirectAccess( unsigned int target, int texnum );

const int RENDERER_SHADOW_TEXTURE_MOMENT_COUNT = 3;

typedef struct rendererShadowTextureBinding_s {
	unsigned int	texture;
	unsigned int	target;
	int				width;
	int				height;
	bool			ready;
} rendererShadowTextureBinding_t;

typedef struct rendererShadowTextureBindings_s {
	// last-selected per-light target (scratch or atlas); ARB2-shaped alias
	rendererShadowTextureBinding_t	projectedAtlas;
	// the persistent projected atlas itself - stable identity across frames,
	// the texture shadowMapArb2AtlasSlot_t cell rects index into
	rendererShadowTextureBinding_t	projectedPersistentAtlas;
	rendererShadowTextureBinding_t	pointAtlas;
	rendererShadowTextureBinding_t	projectedMoments[RENDERER_SHADOW_TEXTURE_MOMENT_COUNT];
	rendererShadowTextureBinding_t	pointMoments[RENDERER_SHADOW_TEXTURE_MOMENT_COUNT];
	bool							projectedAtlasReady;
	bool							projectedPersistentAtlasReady;
	bool							pointAtlasReady;
	// Exact identity of pointAtlas when ready. Modern planning may precede
	// classic light submission, so binding rechecks these fields to prevent a
	// later-selected light's cube from satisfying an older plan.
	int							pointAtlasLightIndex;
	int							pointAtlasSignature;
	int							pointAtlasContentFrame;
	bool							projectedMomentsReady;
	bool							pointMomentsReady;
	bool							projectedDepthCompare;
	bool							pointDepthCompare;
	bool							pointHighPrecision;
	float							translucentDensity;
	float							translucentFilterRadius;
	float							translucentMinVariance;
	float							translucentBleedReduction;
} rendererShadowTextureBindings_t;

bool RB_ShadowMapTextureBindings( rendererShadowTextureBindings_t &bindings );

// Establish the render-world ownership scope for the persistent ARB2 shadow
// caches. Returns true when a world transition invalidated resident entries.
// Modern GL calls this before planning or sampling so it cannot observe the
// previous world's cache during the frame in which a map changes.
bool RB_ShadowMapPrepareCacheView( const viewDef_t *viewDef );

// deletes the lazily created CopyFramebuffer/CopyDepthbuffer scratch FBOs;
// must be called before GLimp_Shutdown while the old context is still current
// so the names cannot alias FBOs of a recreated context after vid_restart
void R_PurgeFramebufferCopyFBOs( void );

// true when geometry owned by this entity def may allocate a STATIC index
// VBO (r_useIndexBuffers 1 limits that to static models; per-frame
// regenerated dynamic-model tris re-upload every frame, which costs more
// than the client-memory index draw it replaces and causes pacing spikes)
bool R_StaticIndexCacheAllowed( const idRenderEntityLocal *def );


/*
====================================================================

IMPLEMENTATION SPECIFIC FUNCTIONS

====================================================================
*/

typedef struct {
	int			width;
	int			height;
	bool		fullScreen;
	bool		borderless;
	bool		hiddenWindow;
	bool		stereo;
	int			displayHz;
	int			multiSamples;
} glimpParms_t;

bool		GLimp_Init( glimpParms_t parms );
// If the desired mode can't be set satisfactorily, false will be returned.
// The renderer will then reset the glimpParms to "safe mode" of 640x480
// fullscreen and try again.  If that also fails, the error will be fatal.

bool		GLimp_SetScreenParms( glimpParms_t parms );
// will set up gl up with the new parms

void		GLimp_Shutdown( void );
// Destroys the rendering context, closes the window, resets the resolution,
// and resets the gamma ramps.

void		GLimp_PreserveWindowOnShutdown( bool preserve );
// Requests that the next GLimp_Shutdown keep the native window alive for a
// same-process renderer restart.

void		GLimp_SwapBuffers( void );
// Calls the system specific swapbuffers routine, and may also perform
// other system specific cvar checks that happen every frame.
// This will not be called if 'r_drawBuffer GL_FRONT'

void		GLimp_SetGamma( unsigned short red[256], 
						    unsigned short green[256],
							unsigned short blue[256] );
// Sets the hardware gamma ramps for gamma and brightness adjustment.
// These are now taken as 16 bit values, so we can take full advantage
// of dacs with >8 bits of precision
bool		GLimp_UseNativeGammaRamps( void );
// Returns true when the platform backend applies r_gamma/r_brightness through
// OS display gamma ramps instead of the renderer's final framebuffer pass.


bool		GLimp_SpawnRenderThread( void (*function)( void ) );
// Returns false if the system only has a single processor

void *		GLimp_BackEndSleep( void );
void		GLimp_FrontEndSleep( void );
void		GLimp_WakeBackEnd( void *data );
// these functions implement the dual processor syncronization

void		GLimp_ActivateContext( void );
bool		GLimp_EnsureActiveContext( const char *operation );
void		GLimp_DeactivateContext( void );
// These are used for managing SMP handoffs of the OpenGL context
// between threads, and as a performance tunining aid.  Setting
// 'r_skipRenderContext 1' will call GLimp_DeactivateContext() before
// the 3D rendering code, and GLimp_ActivateContext() afterwards.  On
// most OpenGL implementations, this will result in all OpenGL calls
// being immediate returns, which lets us guage how much time is
// being spent inside OpenGL.

void		GLimp_EnableLogging( bool enable );


/*
====================================================================

MAIN

====================================================================
*/

void R_RenderView( viewDef_t *parms );

// performs radius cull first, then corner cull
bool R_CullLocalBox( const idBounds &bounds, const float modelMatrix[16], int numPlanes, const idPlane *planes );
bool R_RadiusCullLocalBox( const idBounds &bounds, const float modelMatrix[16], int numPlanes, const idPlane *planes );
bool R_CornerCullLocalBox( const idBounds &bounds, const float modelMatrix[16], int numPlanes, const idPlane *planes );

void R_AxisToModelMatrix( const idMat3 &axis, const idVec3 &origin, float modelMatrix[16] );

// note that many of these assume a normalized matrix, and will not work with scaled axis
void R_GlobalPointToLocal( const float modelMatrix[16], const idVec3 &in, idVec3 &out );
void R_GlobalVectorToLocal( const float modelMatrix[16], const idVec3 &in, idVec3 &out );
void R_GlobalPlaneToLocal( const float modelMatrix[16], const idPlane &in, idPlane &out );
void R_PointTimesMatrix( const float modelMatrix[16], const idVec4 &in, idVec4 &out );
void R_LocalPointToGlobal( const float modelMatrix[16], const idVec3 &in, idVec3 &out );
void R_LocalVectorToGlobal( const float modelMatrix[16], const idVec3 &in, idVec3 &out );
void R_LocalPlaneToGlobal( const float modelMatrix[16], const idPlane &in, idPlane &out );
void R_TransformEyeZToWin( float src_z, const float *projectionMatrix, float &dst_z );

void R_GlobalToNormalizedDeviceCoordinates( const idVec3 &global, idVec3 &ndc );

void R_TransformModelToClip( const idVec3 &src, const float *modelMatrix, const float *projectionMatrix, idPlane &eye, idPlane &dst );

void R_TransformClipToDevice( const idPlane &clip, const viewDef_t *view, idVec3 &normalized );

void R_TransposeGLMatrix( const float in[16], float out[16] );

void R_SetViewMatrix( viewDef_t *viewDef );

void myGlMultMatrix( const float *a, const float *b, float *out );

/*
============================================================

LIGHT

============================================================
*/

void R_ListRenderLightDefs_f( const idCmdArgs &args );
void R_ListRenderEntityDefs_f( const idCmdArgs &args );

bool R_IssueEntityDefCallback( idRenderEntityLocal *def );
idRenderModel *R_EntityDefDynamicModel( idRenderEntityLocal *def, bool collisionOnly = false );

viewEntity_t *R_SetEntityDefViewEntity( idRenderEntityLocal *def );
viewLight_t *R_SetLightDefViewLight( idRenderLightLocal *def );

void R_ClearDrawSurfAreaMemo( void );
void R_InvalidateDrawSurfAreaMemoForEntity( const idRenderEntityLocal *entityDef );

const float *R_SetupDrawSurfShaderRegisters( const viewEntity_t *space, const renderEntity_t *renderEntity,
					 const idMaterial *shader );
void R_FinalizeDrawSurf( drawSurf_t *drawSurf );
void R_AddDrawSurf( const srfTriangles_t *tri, const viewEntity_t *space, const renderEntity_t *renderEntity,
					const idMaterial *shader, const idScreenRect &scissor, int extraDrawSurfFlags = 0 );

bool R_LinkLightSurf( const drawSurf_t **link, const srfTriangles_t *tri, const viewEntity_t *space,
				   const idRenderLightLocal *light, const idMaterial *shader, const idScreenRect &scissor, bool viewInsideShadow );

bool R_CreateAmbientCache( srfTriangles_t *tri, bool needsLighting );
bool R_CreatePackedSurfaceFrameCaches( srfTriangles_t *tri, bool needsLighting, bool createIndexCache );
bool R_CreateLightingCache( const idRenderEntityLocal *ent, const idRenderLightLocal *light, srfTriangles_t *tri );
void R_TouchVertexCache( struct vertCache_s *cache );
void R_CreatePrivateShadowCache( srfTriangles_t *tri );
void R_CreateVertexProgramShadowCache( srfTriangles_t *tri );

/*
============================================================

LIGHTRUN

============================================================
*/

void R_RegenerateWorld_f( const idCmdArgs &args );

void R_ModulateLights_f( const idCmdArgs &args );

void R_SetLightProject( idPlane lightProject[4], const idVec3 origin, const idVec3 targetPoint,
	   const idVec3 rightVector, const idVec3 upVector, const idVec3 start, const idVec3 stop );

void R_AddEffectSurfaces(void);

void R_AddLightSurfaces( void );
void R_AddModelSurfaces( void );
void R_AddThroughWorldOutlines( void );
void R_RemoveUnecessaryViewLights( void );

void R_FreeDerivedData( void );
void R_ReCreateWorldReferences( void );

void R_CreateEntityRefs( idRenderEntityLocal *def );
void R_CreateLightRefs( idRenderLightLocal *light );

void R_DeriveLightData( idRenderLightLocal *light );
void R_FreeLightDefDerivedData( idRenderLightLocal *light );
void R_CheckForEntityDefsUsingModel( idRenderModel *model );

void R_ClearEntityDefDynamicModel( idRenderEntityLocal *def );
void R_FreeEntityDefDerivedData( idRenderEntityLocal *def, bool keepDecals, bool keepCachedDynamicModel, bool keepDynamicModel = false );

// content hash over game-owned joint matrices, used to prove a dynamic
// snapshot is still valid across transform-only entity updates
unsigned long long R_HashJointMatrices( const idJointMat *joints, int numJoints );
void R_FreeEntityDefCachedDynamicModel( idRenderEntityLocal *def );
void R_FreeEntityDefDecals( idRenderEntityLocal *def );
void R_FreeEntityDefOverlay( idRenderEntityLocal *def );
void R_FreeEntityDefFadedDecals( idRenderEntityLocal *def, int time );

void R_CreateLightDefFogPortals( idRenderLightLocal *ldef );

/*
============================================================

POLYTOPE

============================================================
*/

srfTriangles_t *R_PolytopeSurface( int numPlanes, const idPlane *planes, idWinding **windings );

/*
============================================================

RENDER

============================================================
*/

void RB_EnterWeaponDepthHack();
void RB_EnterModelDepthHack( float depth );
void RB_LeaveDepthHack();

// view projection with the model/weapon depth-hack transforms applied
// (modelDepthHack wins; the weapon hack honors the cl_gunfov override);
// shared so the fixed-function and packed-MD5R env-param paths cannot diverge
void R_GetDepthHackProjectionMatrix( const viewDef_t *viewDef, bool weaponDepthHack, float modelDepthHack, float matrix[16] );
void RB_DrawElementsImmediate( const srfTriangles_t *tri );
void RB_RenderTriangleSurface( const srfTriangles_t *tri );
void RB_T_RenderTriangleSurface( const drawSurf_t *surf );
void RB_RenderDrawSurfListWithFunction( drawSurf_t **drawSurfs, int numDrawSurfs, 
					  void (*triFunc_)( const drawSurf_t *) );
void RB_RenderDrawSurfListWithFunctionIgnoreScissor( drawSurf_t **drawSurfs, int numDrawSurfs,
					  void (*triFunc_)( const drawSurf_t *) );
void RB_RenderDrawSurfChainWithFunction( const drawSurf_t *drawSurfs, 
										void (*triFunc_)( const drawSurf_t *) );
void RB_DrawShaderPasses( drawSurf_t **drawSurfs, int numDrawSurfs );
void RB_LoadShaderTextureMatrix( const float *shaderRegisters, const textureStage_t *texture );
void RB_GetShaderTextureMatrix( const float *shaderRegisters, const textureStage_t *texture, float matrix[16] );
void RB_CreateSingleDrawInteractions( const drawSurf_t *surf, void (*DrawInteraction)(const drawInteraction_t *) );
void RB_CreateSingleDrawInteractionsFiltered( const drawSurf_t *surf, void (*DrawInteraction)(const drawInteraction_t *), drawInteractionStageFilter_t StageFilter );
void R_SetDrawInteraction( const shaderStage_t *surfaceStage, const float *surfaceRegs,
						  idImage **image, idVec4 matrix[2], float color[4] );
bool RB_FlatDiffuseSurfaceActive( const drawSurf_t *surf );
// representability of a surface's baked indirect contribution for the modern
// light-grid pipeline; sets *reason to a stable blocker token when false
bool RB_LightGridSurfaceModernRepresentable( const drawSurf_t *surf, const viewDef_t *viewDef, const char **reason );
bool RB_FlatDiffuseSweepActive( const drawSurf_t *surf );
void RB_GetFlatDiffuseParams( const drawSurf_t *surf, idVec4 &params );
void RB_ApplyFlatDiffuseStage( const drawSurf_t *surf, idImage **diffuseImage, float diffuseColor[4], idVec4 &params );

const shaderStage_t *RB_SetLightTexture( const idRenderLightLocal *light );

void RB_DrawView( const void *data );
bool RB_DrawSharedGuiView( const viewDef_t *viewDef );
bool RB_DrawSharedInWorldGuiView( const viewDef_t *viewDef );
bool RB_DrawSharedCinematicRootView( const viewDef_t *viewDef );
bool RB_DrawSharedAuthoredPostView( const viewDef_t *viewDef,
	drawSurf_t **drawSurfs, int firstSourceSurface, int sourceSurfaceCount );
void RB_DrawSpecialEffects( const void *data );
void RB_ApplyResolutionScaleToBackBuffer( void );
void RB_ApplyCRTToBackBuffer( void );
bool RB_UnderwaterViewAvailable( void );

void RB_DetermineLightScale( void );
void RB_STD_LightScale( void );
void RB_BeginDrawingView (void);

/*
============================================================

DRAW_STANDARD

============================================================
*/

void RB_DrawElementsWithCounters( const srfTriangles_t *tri );
void RB_DrawShadowElementsWithCounters( const drawSurf_t *surf, int numIndexes );
void RB_STD_FillDepthBuffer( drawSurf_t **drawSurfs, int numDrawSurfs );
bool RB_PrepareStageTexturing( const shaderStage_t *pStage, const drawSurf_t *surf, idDrawVert *ac );
void RB_FinishStageTexturing( const shaderStage_t *pStage, const drawSurf_t *surf, idDrawVert *ac );
void RB_BindVariableStageImage( const textureStage_t *texture, const float *shaderRegisters );
void RB_BindStageTexture( const float *shaderRegisters, const textureStage_t *texture, const drawSurf_t *surf );
void RB_FinishStageTexture( const textureStage_t *texture, const drawSurf_t *surf );
bool RB_DrawSurfHasSoftParticleStage( const drawSurf_t *surf );
void RB_StencilShadowPass( const drawSurf_t *drawSurfs );
void RB_STD_DrawView( void );
void RB_STD_FogAllLights( void );
void RB_BakeTextureMatrixIntoTexgen( idPlane lightProject[3], const float textureMatrix[16] );

/*
============================================================

DRAW_*

============================================================
*/

void	R_ARB2_Init( void );
void	RB_ARB2_DrawInteractions( void );
void	RB_ResetARB2InteractionHandoffBreadcrumb( void );
void	RB_ResetAppleGL21RouteCounters( void );
void	RB_ReportAppleGL21RouteCounters( void );
void	RB_ARB2_MD5R_DrawDepthElements( const drawSurf_t *surf );
void	RB_ARB2_MD5R_DrawShadowElements( const drawSurf_t *surf, int numIndexes );
void	RB_ARB2_MD5R_DrawBasicFog( const drawSurf_t *surf );
void	RB_ARB2_LoadMD5RLocalViewOrigin( const drawSurf_t *surf );
void	RB_ARB2_LoadMD5RMVPMatrix( const drawSurf_t *surf );
void	RB_ARB2_LoadMD5RProjectionMatrix( void );
void	RB_ARB2_LoadMD5RModelViewMatrix( const drawSurf_t *surf );
void	RB_ARB2_PrepareStageTexturing( const shaderStage_t *pStage, const drawSurf_t *surf, bool fillingDepth );
void	RB_ARB2_DisableStageTexturing( const shaderStage_t *pStage, const drawSurf_t *surf );
bool	RB_ARB2_PreparePackedMD5RProgramStageDraw( const drawSurf_t *surf );
bool	RB_ARB2_PreparePackedMD5RDirectDraw( const drawSurf_t *surf );
void	RB_ARB2_ClearPreparedPackedMD5RDirectDraw( void );
void	RB_ARB2_ClearPreparedPackedMD5RDraw( void );
bool	RB_ARB2_DrawPreparedPackedMD5RStageBatches( const srfTriangles_t *tri );
bool	RB_ARB2_DrawPreparedPackedMD5RDirectBatches( const srfTriangles_t *tri );
void	R_ReloadARBPrograms_f( const idCmdArgs &args );
void	R_ReportShaderPrograms_f( const idCmdArgs &args );

// Stable identities for the stock ARB newStage program families.  Vulkan
// keeps parser handles opaque; its stage executor uses this
// identity instead of depending on registry insertion order or program-name
// spelling.  Unknown is also returned for invalid handles.
typedef enum {
	VK_MATERIAL_PROGRAM_FAMILY_UNKNOWN = 0,
	VK_MATERIAL_PROGRAM_FAMILY_BUMPY_ENVIRONMENT = 1,
	VK_MATERIAL_PROGRAM_FAMILY_HEAT_HAZE = 2,
	VK_MATERIAL_PROGRAM_FAMILY_HEAT_HAZE_WITH_MASK = 3,
	VK_MATERIAL_PROGRAM_FAMILY_HEAT_HAZE_GRAY_WITH_MASK = 4,
	VK_MATERIAL_PROGRAM_FAMILY_HEAT_HAZE_WITH_MASK_AND_VERTEX = 5,
	VK_MATERIAL_PROGRAM_FAMILY_MONOCHROME = 6,
	VK_MATERIAL_PROGRAM_FAMILY_REFRACTIVE_GLASS = 7
} vkMaterialProgramFamily_t;

int		R_FindARBProgram( unsigned int target, const char *program );
bool	R_IsARBProgramValid( unsigned int target, unsigned int ident );
vkMaterialProgramFamily_t R_GetARBProgramFamily( unsigned int target, unsigned int ident );
bool	R_BindARBProgram( unsigned int target, unsigned int ident, const char *usage, bool required );

// Stable identities for every GLSL family named by the shipped Quake 4
// materials. Vulkan embeds native SPIR-V replacements and resolves authored
// shaderParm/shaderTexture declarations by semantic name, so it does not
// depend on source files or OpenGL shader-object handles at run time.
typedef enum {
	VK_GLSL_PROGRAM_FAMILY_UNKNOWN = 0,
	VK_GLSL_PROGRAM_FAMILY_DISPLACEMENT,
	VK_GLSL_PROGRAM_FAMILY_DISPLACEMENT_TWO_STAGE,
	VK_GLSL_PROGRAM_FAMILY_GHOST_PULLING,
	VK_GLSL_PROGRAM_FAMILY_DISPLACEMENT2,
	VK_GLSL_PROGRAM_FAMILY_MULTIPLY_BLEND,
	VK_GLSL_PROGRAM_FAMILY_DISPLACEMENT_CUBE,
	VK_GLSL_PROGRAM_FAMILY_SNIPER_STRETCH2,
	VK_GLSL_PROGRAM_FAMILY_DEPTH_TEXTURE,
	VK_GLSL_PROGRAM_FAMILY_BLUR,
	VK_GLSL_PROGRAM_FAMILY_MEDLABS,
	VK_GLSL_PROGRAM_FAMILY_DEPTH_TEXTURE2,
	VK_GLSL_PROGRAM_FAMILY_AL,
	VK_GLSL_PROGRAM_FAMILY_PARALLAX_BUMP,
	VK_GLSL_PROGRAM_FAMILY_CUSTOM_LIT,
	VK_GLSL_PROGRAM_FAMILY_WATER,
	VK_GLSL_PROGRAM_FAMILY_DEPTH_AWARE_BLUR,
	VK_GLSL_PROGRAM_FAMILY_SMAA_EDGE,
	VK_GLSL_PROGRAM_FAMILY_SMAA_WEIGHTS,
	VK_GLSL_PROGRAM_FAMILY_SMAA_BLEND,
	VK_GLSL_PROGRAM_FAMILY_COUNT
} vkGLSLProgramFamily_t;

vkGLSLProgramFamily_t R_GetGLSLProgramFamily( const char *program );
void	RB_ShutdownShadowMapResources( void );

typedef enum {
	PROG_INVALID,
	VPROG_INTERACTION,
	VPROG_ENVIRONMENT,
	VPROG_BUMPY_ENVIRONMENT,
	VPROG_R200_INTERACTION,
	VPROG_STENCIL_SHADOW,
	VPROG_NV20_BUMP_AND_LIGHT,
	VPROG_NV20_DIFFUSE_COLOR,
	VPROG_NV20_SPECULAR_COLOR,
	VPROG_NV20_DIFFUSE_AND_SPECULAR_COLOR,
	VPROG_TEST,
	FPROG_INTERACTION,
	FPROG_ENVIRONMENT,
	FPROG_BUMPY_ENVIRONMENT,
	FPROG_TEST,
	VPROG_AMBIENT,
	FPROG_AMBIENT,
	VPROG_GLASSWARP,
	FPROG_GLASSWARP,
	VPROG_SIMPLE_INTERACTION,
	FPROG_SIMPLE_INTERACTION,
	ARB2_MD5R_INTERACTION_VPROG_BASE = 21,
	ARB2_MD5R_DEPTH_VPROG_BASE = 24,
	ARB2_MD5R_STAGE_VPROG_BASE = 27,
	ARB2_MD5R_SKYBOX_VPROG_BASE = 30,
	ARB2_MD5R_DIFFUSE_CUBE_VPROG_BASE = 33,
	ARB2_MD5R_REFLECT_CUBE_VPROG_BASE = 36,
	ARB2_MD5R_BUMPY_REFLECT_CUBE_VPROG_BASE = 39,
	ARB2_MD5R_SHADOW_VOLUME_VPROG_BASE = 42,
	ARB2_MD5R_BASIC_FOG_VPROG_BASE = 45,
	PROG_USER = 51
} program_t;

/*

  All vertex programs use the same constant register layout:

c[4]	localLightOrigin
c[5]	localViewOrigin
c[6]	lightProjection S
c[7]	lightProjection T
c[8]	lightProjection Q
c[9]	lightFalloff	S
c[10]	bumpMatrix S
c[11]	bumpMatrix T
c[12]	diffuseMatrix S
c[13]	diffuseMatrix T
c[14]	specularMatrix S
c[15]	specularMatrix T


c[20]	light falloff tq constant

// texture 0 was cube map
// texture 1 will be the per-surface bump map
// texture 2 will be the light falloff texture
// texture 3 will be the light projection texture
// texture 4 is the per-surface diffuse map
// texture 5 is the per-surface specular map
// texture 6 is the specular half angle cube map

*/

typedef enum {
	PP_LIGHT_ORIGIN = 4,
	PP_VIEW_ORIGIN,
	PP_LIGHT_PROJECT_S,
	PP_LIGHT_PROJECT_T,
	PP_LIGHT_PROJECT_Q,
	PP_LIGHT_FALLOFF_S,
	PP_BUMP_MATRIX_S,
	PP_BUMP_MATRIX_T,
	PP_DIFFUSE_MATRIX_S,
	PP_DIFFUSE_MATRIX_T,
	PP_SPECULAR_MATRIX_S,
	PP_SPECULAR_MATRIX_T,
	PP_COLOR_MODULATE,
	PP_COLOR_ADD,

	PP_LIGHT_FALLOFF_TQ = 20	// only for NV programs
} programParameter_t;


/*
============================================================

TR_STENCILSHADOWS

"facing" should have one more element than tri->numIndexes / 3, which should be set to 1

============================================================
*/

void R_MakeShadowFrustums( idRenderLightLocal *def );

// shadowGen_t is defined by ../render_geo/RenderGeometry.h

srfTriangles_t *R_CreateShadowVolume( const idRenderEntityLocal *ent,
									 const srfTriangles_t *tri, const idRenderLightLocal *light,
									 shadowGen_t optimize, srfCullInfo_t &cullInfo );

/*
============================================================

TR_TURBOSHADOW

Fast, non-clipped overshoot shadow volumes

"facing" should have one more element than tri->numIndexes / 3, which should be set to 1
calling this function may modify "facing" based on culling

============================================================
*/

srfTriangles_t *R_CreateTurboShadowVolumeForSurface( const idRenderEntityLocal *ent,
									 const srfTriangles_t *tri, const idRenderLightLocal *light,
									 srfCullInfo_t &cullInfo );

srfTriangles_t *R_CreateVertexProgramTurboShadowVolume( const idRenderEntityLocal *ent,
									 const srfTriangles_t *tri, const idRenderLightLocal *light,
									 srfCullInfo_t &cullInfo );

srfTriangles_t *R_CreatePackedTurboShadowVolume( const idRenderEntityLocal *ent,
									 const srfTriangles_t *tri, const idRenderLightLocal *light,
									 srfCullInfo_t &cullInfo );

srfTriangles_t *R_CreateTurboShadowVolume( const idRenderEntityLocal *ent,
									 const srfTriangles_t *tri, const idRenderLightLocal *light,
									 srfCullInfo_t &cullInfo );

/*
============================================================

util/shadowopt3

dmap time optimization of shadow volumes, called from R_CreateShadowVolume

============================================================
*/


// optimizedShadow_t and the dmap shadow-optimizer entry points are declared
// by ../render_geo/RenderGeometry.h

/*
============================================================

TRISURF

============================================================
*/

// triangle-surface entry points are declared by ../render_geo/RenderGeometry.h;
// the frame-memory-coupled maintenance below stays renderer-side
void				R_PurgeTriSurfData( frameData_t *frame );
void				R_FreeDeferredTriSurfs( frameData_t *frame );

/*
============================================================

SUBVIEW

============================================================
*/

bool	R_PreciseCullSurface( const drawSurf_t *drawSurf, idBounds &ndcBounds );
bool	R_GenerateSubViews( void );

/*
============================================================

SCENE GENERATION

============================================================
*/

void R_InitFrameData( void );
void R_ShutdownFrameData( void );
int R_CountFrameData( void );
void R_ToggleSmpFrame( void );
void *R_FrameAlloc( int bytes );
void *R_ClearedFrameAlloc( int bytes );
void R_FrameFree( void *data );

// R_StaticAlloc/R_ClearedStaticAlloc/R_StaticFree live in the shared
// imagetools library (declared by ../imagetools/ImageTools.h, included above);
// the renderer installs its performance-counter hooks at Init
void R_InstallImageToolsHooks( void );

// binds the shared render-geometry library's hooks (cvars, counters, vertex
// caches, deferred frees, turbo shadows) to live renderer behavior
void R_InstallRenderGeoHooks( void );


/*
=============================================================

RENDERER DEBUG TOOLS

=============================================================
*/

float RB_DrawTextLength( const char *text, float scale, int len );
void RB_AddDebugText( const char *text, const idVec3 &origin, float scale, const idVec4 &color, const idMat3 &viewAxis, const int align, const int lifetime, const bool depthTest );
void RB_ClearDebugText( int time );
void RB_AddDebugLine( const idVec4 &color, const idVec3 &start, const idVec3 &end, const int lifeTime, const bool depthTest );
void RB_ClearDebugLines( int time );
void RB_AddDebugPolygon( const idVec4 &color, const idWinding &winding, const int lifeTime, const bool depthTest );
void RB_ClearDebugPolygons( int time );
void RB_DrawBounds( const idBounds &bounds );
void RB_SimpleSurfaceSetup( const drawSurf_t *drawSurf );
void RB_ShowLights( drawSurf_t **drawSurfs, int numDrawSurfs );
void RB_ShowLightCount( drawSurf_t **drawSurfs, int numDrawSurfs );
void RB_PolygonClear( void );
void RB_ScanStencilBuffer( void );
void RB_ShowDestinationAlpha( void );
void RB_ShowOverdraw( void );
void RB_RenderDebugTools( drawSurf_t **drawSurfs, int numDrawSurfs );
void RB_ShutdownDebugTools( void );
void RB_ShutdownScenePostProcess( void );
void RB_ApplyColorMappingsToBackBuffer( void );

/*
=============================================================

TR_BACKEND

=============================================================
*/

void RB_SetDefaultGLState( void );
void RB_SetGL2D( void );

// write a comment to the r_logFile if it is enabled
void RB_LogComment( const char *comment, ... ) id_attribute((format(printf,1,2)));

void RB_ShowImages( void );

// Backend implementation of RC_RESOLVE_TEMPORAL_PRESENTATION. The command is
// copied at dispatch so a capture scope that begins after frontend enqueue can
// suppress history reads/writes without mutating frame-memory ownership.
bool RB_ResolveTemporalPresentation(
	const resolveTemporalPresentationCommand_t &command );

// GL temporal history may only consume depth copied successfully for the
// command's immutable frame/generation snapshot. Resolve commands invalidate
// before attempting a depth blit and stamp only after an error-free copy.
void RB_InvalidateTemporalDepthStamp( idRenderTexture *target );
void RB_StampTemporalDepthResolved( idRenderTexture *target,
	int frameNumber, unsigned int historyGeneration );

void RB_ExecuteBackEndCommands( const emptyCommand_t *cmds );


/*
=============================================================

TR_GUISURF

=============================================================
*/

void R_SurfaceToTextureAxis( const srfTriangles_t *tri, idVec3 &origin, idVec3 axis[3] );
void R_RenderGuiSurf( idUserInterface *gui, drawSurf_t *drawSurf );
void R_GuiTraceProbe_f( const idCmdArgs &args );

/*
=============================================================

TR_ORDERINDEXES

=============================================================
*/

void R_OrderIndexes( int numIndexes, glIndex_t *indexes );

/*
=============================================================

TR_DEFORM

=============================================================
*/

void R_DeformDrawSurf( drawSurf_t *drawSurf );

/*
=============================================================

TR_TRACE

=============================================================
*/

typedef struct {
	float		fraction;
	// only valid if fraction < 1.0
	idVec3		point;
	idVec3		normal;
	int			indexes[3];
} localTrace_t;

localTrace_t R_LocalTrace( const idVec3 &start, const idVec3 &end, const float radius, const srfTriangles_t *tri );
void RB_ShowTrace( drawSurf_t **drawSurfs, int numDrawSurfs );

/*
=============================================================

TR_SHADOWBOUNDS

=============================================================
*/
idScreenRect R_CalcIntersectionScissor( const idRenderLightLocal * lightDef,
									    const idRenderEntityLocal * entityDef,
									    const viewDef_t * viewDef );

//=============================================

#include "RenderWorld_local.h"
#include "GuiModel.h"
#include "VertexCache.h"

void GL_SelectTextureNoClient(int unit);

#endif /* !__TR_LOCAL_H__ */
