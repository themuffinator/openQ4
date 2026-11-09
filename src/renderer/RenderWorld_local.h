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

#ifndef __RENDERWORLDLOCAL_H__
#define __RENDERWORLDLOCAL_H__

#include "RendererSettingsReports.h"

// assume any lightDef or entityDef index above this is an internal error
const int LUDICROUS_INDEX	= 10000;


typedef struct portal_s {
	int						intoArea;		// area this portal leads to
	idWinding *				w;				// winding points have counter clockwise ordering seen this area
	idPlane					plane;			// view must be on the positive side of the plane to cross
	idImage *				image;			// optional fade image for distance-cull portal transitions
	float					cullNear;		// near threshold for portal fade/cull
	float					cullFar;		// far threshold for portal fade/cull
	struct portal_s *		next;			// next portal of the area
	struct doublePortal_s *	doublePortal;
} portal_t;


typedef struct doublePortal_s {
	struct portal_s	*		portals[2];
	int						blockingBits;	// PS_BLOCK_VIEW, PS_BLOCK_AIR, etc, set by doors that shut them off

	// A portal will be considered closed if it is past the
	// fog-out point in a fog volume.  We only support a single
	// fog volume over each portal.
	idRenderLightLocal *		fogLight;
	struct doublePortal_s *	nextFoggedPortal;
} doublePortal_t;

typedef struct lightGridPoint_s {
	idVec3					origin;
	float					visibilityMeanDistance;
	float					visibilityMeanDistanceSq;
	byte					valid;
} lightGridPoint_t;

enum lightGridPointState_t {
	LIGHTGRID_POINT_INVALID = 0,
	LIGHTGRID_POINT_VALID = 1,
	LIGHTGRID_POINT_RELOCATED = 2,
	LIGHTGRID_POINT_NEAR_SOLID = 3,
	LIGHTGRID_POINT_RELOCATED_NEAR_SOLID = 4
};

typedef struct lightGridPackedImageRef_s {
	idStr					packFileName;
	int						dataOffset;
	int						dataBytes;
	int						width;
	int						height;
} lightGridPackedImageRef_t;

class LightGrid {
public:
	idVec3					lightGridOrigin;
	idVec3					lightGridSize;
	int						lightGridBounds[3];
	idList<lightGridPoint_t> lightGridPoints;
	int						totalGridPointCount;
	int						validGridPointCount;
	int						relocatedGridPointCount;
	int						nearSolidGridPointCount;
	int						area;
	idImage *				irradianceImage;
	idImage *				visibilityImage;
	idImage *				probeImage;
	lightGridPackedImageRef_t packedIrradianceImage;
	lightGridPackedImageRef_t packedVisibilityImage;
	lightGridPackedImageRef_t packedProbeImage;
	int						imageSingleProbeSize;
	int						imageBorderSize;
	float					visibilityMaxDistance;
	float					relocationMaxDistance;

	void					Clear();
	bool					HasImage() const;
	bool					IsUsable() const;
	int						GridPointCount() const;
	int						CountValidGridPoints() const;
	int						CountRelocatedGridPoints() const;
	int						CountNearSolidGridPoints() const;
	bool					HasPointData() const;
	void					DiscardPointData();
	void					SetupGrid( const idBounds &bounds, const idRenderWorld *world, const idVec3 &preferredSize, int areaIndex, int totalAreas, int maxProbes, bool printToConsole );
	void					GetBaseGridCoord( const idVec3 &origin, int gridCoord[3] ) const;
	int						GridCoordToProbeIndex( const int gridCoord[3] ) const;
	idVec3					GetGridCoordDebugColor( const int gridCoord[3] ) const;

private:
	void					CalculateGridPointPositions( const idRenderWorld *world, int totalAreas, bool printToConsole );
};


typedef struct portalArea_s {
	int				areaNum;
	int				connectedAreaNum[NUM_PORTAL_ATTRIBUTES];	// if two areas have matching connectedAreaNum, they are
									// not separated by a portal with the apropriate PS_BLOCK_* blockingBits
	idBounds		globalBounds;
	LightGrid		lightGrid;
	int				viewCount;		// set by R_FindViewLightsAndEntities
	portal_t *		portals;		// never changes after load
	areaReference_t	entityRefs;		// head/tail of doubly linked list, may change
	areaReference_t	lightRefs;		// head/tail of doubly linked list, may change
} portalArea_t;


static const int	CHILDREN_HAVE_MULTIPLE_AREAS = -2;
static const int	AREANUM_SOLID = -1;
typedef struct {
	idPlane			plane;
	int				children[2];		// negative numbers are (-1 - areaNumber), 0 = solid
	int				commonChildrenArea;	// if all children are either solid or a single area,
										// this is the area number, else CHILDREN_HAVE_MULTIPLE_AREAS
} areaNode_t;

class idRenderWorldMD5RProcData;


class idRenderWorldLocal : public idRenderWorld {
public:
							idRenderWorldLocal();
	virtual					~idRenderWorldLocal();

	virtual	qhandle_t		AddEntityDef( const renderEntity_t *re );
	virtual	void			UpdateEntityDef( qhandle_t entityHandle, const renderEntity_t *re );
	virtual	void			FreeEntityDef( qhandle_t entityHandle );
	virtual const renderEntity_t *GetRenderEntity( qhandle_t entityHandle ) const;

	virtual	qhandle_t		AddLightDef( const renderLight_t *rlight );
	virtual	void			UpdateLightDef( qhandle_t lightHandle, const renderLight_t *rlight );
	virtual	void			FreeLightDef( qhandle_t lightHandle );
	virtual const renderLight_t *GetRenderLight( qhandle_t lightHandle ) const;

	virtual bool			CheckAreaForPortalSky( int areaNum );

	virtual	void			GenerateAllInteractions();
	virtual void			RegenerateWorld();

	virtual void			ProjectDecalOntoWorld( const idFixedWinding &winding, const idVec3 &projectionOrigin, const bool parallel, const float fadeDepth, const idMaterial *material, const int startTime );
	virtual void			ProjectDecal( qhandle_t entityHandle, const idFixedWinding &winding, const idVec3 &projectionOrigin, const bool parallel, const float fadeDepth, const idMaterial *material, const int startTime );
	virtual void			ProjectOverlay( qhandle_t entityHandle, const idPlane localTextureAxis[2], const idMaterial *material );
	virtual void			RemoveDecals( qhandle_t entityHandle );

	virtual void			SetRenderView( const renderView_t *renderView );
	virtual	void			RenderScene( const renderView_t *renderView, int renderFlags = RF_NORMAL );

	virtual	int				NumAreas( void ) const;
	virtual int				PointInArea( const idVec3 &point ) const;
	virtual int				BoundsInAreas( const idBounds &bounds, int *areas, int maxAreas ) const;
	virtual	int				NumPortalsInArea( int areaNum );
	virtual exitPortal_t	GetPortal( int areaNum, int portalNum );

	virtual	guiPoint_t		GuiTrace( qhandle_t entityHandle, const idVec3 start, const idVec3 end ) const;
	virtual bool			ModelTrace( modelTrace_t &trace, qhandle_t entityHandle, const idVec3 &start, const idVec3 &end, const float radius ) const;
	virtual bool			Trace( modelTrace_t &trace, const idVec3 &start, const idVec3 &end, const float radius, bool skipDynamic = true, bool skipPlayer = false ) const;
	virtual bool			FastWorldTrace( modelTrace_t &trace, const idVec3 &start, const idVec3 &end ) const;

	virtual void			DebugClear(int time) { DebugClearLines(0); }
	virtual void			DebugClearLines( int time );
	virtual void			DebugLine( const idVec4 &color, const idVec3 &start, const idVec3 &end, const int lifetime = 0, const bool depthTest = false );
	virtual void			DebugArrow( const idVec4 &color, const idVec3 &start, const idVec3 &end, int size, const int lifetime = 0 );
	virtual void			DebugWinding( const idVec4 &color, const idWinding &w, const idVec3 &origin, const idMat3 &axis, const int lifetime = 0, const bool depthTest = false );
	virtual void			DebugCircle( const idVec4 &color, const idVec3 &origin, const idVec3 &dir, const float radius, const int numSteps, const int lifetime = 0, const bool depthTest = false );
	virtual void			DebugSphere( const idVec4 &color, const idSphere &sphere, const int lifetime = 0, bool depthTest = false );
	virtual void			DebugBounds( const idVec4 &color, const idBounds &bounds, const idVec3 &org = vec3_origin, const int lifetime = 0, bool depthTest = false );
	virtual void			DebugBox( const idVec4 &color, const idBox &box, const int lifetime = 0 );
	virtual void			DebugFrustum( const idVec4 &color, const idFrustum &frustum, const bool showFromOrigin = false, const int lifetime = 0 );
	virtual void			DebugCone( const idVec4 &color, const idVec3 &apex, const idVec3 &dir, float radius1, float radius2, const int lifetime = 0 );
	virtual void			DebugScreenRect( const idVec4 &color, const idScreenRect &rect, const viewDef_t *viewDef, const int lifetime = 0 );
	virtual void			DebugAxis( const idVec3 &origin, const idMat3 &axis );

	virtual void			DebugClearPolygons( int time );
	virtual void			DebugPolygon( const idVec4 &color, const idWinding &winding, const int lifeTime = 0, const bool depthTest = false );

	virtual void			DrawText( const char *text, const idVec3 &origin, float scale, const idVec4 &color, const idMat3 &viewAxis, const int align = 1, const int lifetime = 0, bool depthTest = false );

	virtual void			WriteRenderLight(idDemoFile* writeDemo, const renderLight_t* light) { };
	virtual void			ReadRenderLight(idDemoFile* readDemo, renderLight_t& light) { };

	// jscott: handling of effects
	virtual qhandle_t		AddEffectDef(const renderEffect_t* reffect, int time);
	virtual bool			UpdateEffectDef(qhandle_t effectHandle, const renderEffect_t* reffect, int time);
	virtual void			StopEffectDef(qhandle_t effectHandle);
	virtual const class rvRenderEffectLocal* GetEffectDef(qhandle_t effectHandle) const;
	virtual void			FreeEffectDef(qhandle_t effectHandle);
	virtual bool			EffectDefHasSound(const renderEffect_s* reffect);

	// jscott: for optimised pushes
	virtual void			PushMarkedDefs(void) { }
	virtual void			ClearMarkedDefs(void) { }

	// jscott: fix for FreeWorld crash
	virtual void			RemoveAllModelReferences(idRenderModel* model) { }

	virtual bool			HasSkybox( int areaNum );
	void					FreeDeferredLightDefs();
	void					ApplyPendingDefOps( void );

	//-----------------------

	idStr					mapName;				// ie: maps/tim_dm2.proc, written to demoFile
	ID_TIME_T					mapTimeStamp;			// for fast reloads of the same level
	bool					worldFromGeneratedCache;	// decoded from the generated cache: not retained once those reads are refused
	unsigned int			mapFileCRC;				// retail PROC / MD5RProc header CRC token

	areaNode_t *			areaNodes;
	int						numAreaNodes;

	portalArea_t *			portalAreas;
	int						numPortalAreas;
	int						connectedAreaNum;		// incremented every time a door portal state changes

	int						lightGridAvailabilityFrame;	// tr.frameCount the latch below was evaluated for
	bool					anyLightGridAvailable;		// any portal area has a usable light grid
	idFile *				lightGridPackFile;			// whole .lightgridpack in memory while atlases can still stream from it
	idStr					lightGridPackFileName;		// pack held (or that failed to read) in lightGridPackFile

	idScreenRect *			areaScreenRect;

	doublePortal_t *		doublePortals;
	int						numInterAreaPortals;

	idList<idRenderModel *>	localModels;
	idRenderWorldMD5RProcData *	md5rProcData;

	idList<idRenderEntityLocal*>	entityDefs;

	// Handles of the entityDefs carrying REF_OUTLINE_THROUGH_WORLD. The view build
	// has to find them every frame and they are a handful of players at most, so
	// they are tracked as they are updated rather than searched for by walking
	// every entity in the map. See R_AddThroughWorldOutlines.
	idList<int>						throughWorldOutlineEntities;

	idList<idRenderLightLocal*>		lightDefs;
	idList<idRenderLightLocal*>		deferredFreeLightDefs;
	// entityDefs freed while the render thread still draws a frame that
	// references them (r_vkRenderThread); deleted with the deferred lights
	idList<idRenderEntityLocal*>	deferredFreeEntityDefs;

	// Render thread stage 1 (r_vkRenderThread, REN-07). While a frame records
	// on the render thread, the game's changes to existing defs it may draw,
	// and to their decals and overlays, are queued here and applied in call
	// order by the next R_RenderThread_Sync, before the next front end runs.
	typedef enum {
		PENDING_UPDATE_ENTITY,
		PENDING_FREE_ENTITY,
		PENDING_UPDATE_LIGHT,
		PENDING_FREE_LIGHT,
		PENDING_DECAL_ONTO_WORLD,
		PENDING_DECAL,
		PENDING_OVERLAY,
		PENDING_REMOVE_DECALS
	} pendingDefOpType_t;
	typedef struct {
		pendingDefOpType_t	type;
		qhandle_t			handle;
		renderEntity_t		entity;
		renderLight_t		light;
		idFixedWinding *	winding;
		idVec3				projectionOrigin;
		bool				parallel;
		float				fadeDepth;
		const idMaterial *	material;
		int					startTime;
		idPlane				localTextureAxis[2];
	} pendingDefOp_t;
	idList<pendingDefOp_t>			pendingDefOps;
	// effect snapshots deleted while the in-flight frame may still draw them
	idList<idRenderModel *>			pendingModelDeletes;

	pendingDefOp_t *				QueueDefOp( pendingDefOpType_t type, qhandle_t handle );
	const pendingDefOp_t *			FindPendingDefOp( qhandle_t handle, bool light ) const;
	void							DeleteEffectModel( idRenderModel *model );
// jmarshall: BSE
	idList<rvRenderEffectLocal*>	effectsDef;
// jmarshll end

	idBlockAlloc<areaReference_t, 1024, 0> areaReferenceAllocator;
	idBlockAlloc<idInteraction, 256, 0>	interactionAllocator;
	idBlockAlloc<areaNumRef_t, 1024, 0>	areaNumRefAllocator;

	// all light / entity interactions are referenced here for fast lookup without
	// having to crawl the doubly linked lists.  EnntityDefs are sequential for better
	// cache access, because the table is accessed by light in idRenderWorldLocal::CreateLightDefInteractions()
	// Growing this table is time consuming, so we add a pad value to the number
	// of entityDefs and lightDefs
	idInteraction **		interactionTable;
	int						interactionTableWidth;		// entityDefs
	int						interactionTableHeight;		// lightDefs


	bool					generateAllInteractionsCalled;
	bool					pendingDemoTimeOffset;	// a demo map load occurred and the next render view should refresh timing

	//-----------------------
	// RenderWorld_load.cpp

	idRenderModel *			ParseModel( Lexer *src );
	idRenderModel *			ParseShadowModel( Lexer *src );
	void					SetupAreaRefs();
	void					ParseInterAreaPortals( Lexer *src );
	void					ParseNodes( Lexer *src );
	int						CommonChildrenArea_r( areaNode_t *node );
	void					FreeWorld();
	void					ClearWorld();
	void					FreeDefs();
	void					TouchWorldModels( void );
	void					AddWorldModelEntities();
	void					ConvertProcToMD5R();
	void					ClearPortalStates();
	virtual	bool			InitFromMap( const char *mapName );
	bool					WriteLevelLoadCachePayload( idFile &file ) const;
	bool					ReadLevelLoadCachePayload( idFile &file, const char *expectedMapName, ID_TIME_T sourceTimestamp );
	bool					WriteMD5R( bool compressed );
	void					SetupLightGrid();
	void					LoadLightGridImages( bool forceReloadLoaded = false );
	// Makes the map's light grids resident under the load's preload policy and
	// fills in what it did; SetupLightGrid publishes the receipt (render API 20).
	void					PreloadLightGridImages( renderLightGridLoadReceipt_t &receipt );
	void					PublishLightGridLoadReceipt( const renderLightGridLoadReceipt_t &receipt ) const;
	bool					EnsureLightGridAreaImages( int areaIndex );
	idFile *				AcquireLightGridPack( const char *name );
	void					ReleaseLightGridPack();
	void					TrimLightGridPack();
	bool					AnyLightGridAvailable();
	bool					LoadLightGridFile( const char *name, bool osPath = false );	// osPath: name is an OS path, not a game path
	// chunkCount/assignedChunks, when given, report the pack's chunk directory and how many found an atlas
	bool					LoadLightGridPackFile( const char *name, int *chunkCount = NULL, int *assignedChunks = NULL );
	bool					ParseLightGridPoints( idLexer *src );
	bool					ParseLightGridVisibility( idLexer *src );
	void					WriteLightGridsToFile( const char *name ) const;

	//--------------------------
	// RenderWorld_portals.cpp

	idScreenRect			ScreenRectFromWinding( const idWinding *w, viewEntity_t *space );
	bool					PortalIsFoggedOut( const portal_t *p );
	virtual void			RenderPortalFades( void );
	void					FloodViewThroughArea_r( const idVec3 origin, int areaNum, const struct portalStack_s *ps );
	void					FlowViewThroughPortals( const idVec3 origin, int numPlanes, const idPlane *planes );
	void					FindVisibleAreas_r( const idVec3 origin, int areaNum, bool *visibleAreas );
	virtual void			FindVisibleAreas( idVec3 origin, int areaNum, bool *visibleAreas );
	void					FloodLightThroughArea_r( idRenderLightLocal *light, int areaNum, const struct portalStack_s *ps );
	void					FlowLightThroughPortals( idRenderLightLocal *light );
	areaNumRef_t *			FloodFrustumAreas_r( const idFrustum &frustum, const int areaNum, const idBounds &bounds, areaNumRef_t *areas );
	areaNumRef_t *			FloodFrustumAreas( const idFrustum &frustum, areaNumRef_t *areas );
	bool					CullEntityByPortals( const idRenderEntityLocal *entity, const struct portalStack_s *ps );
	void					AddAreaEntityRefs( int areaNum, const struct portalStack_s *ps );
	bool					CullLightByPortals( const idRenderLightLocal *light, const struct portalStack_s *ps );
	void					AddAreaLightRefs( int areaNum, const struct portalStack_s *ps );
	void					AddAreaRefs( int areaNum, const struct portalStack_s *ps );
	void					BuildConnectedAreas_r( int areaNum );
	void					BuildConnectedAreas( void );
	void					FindViewLightsAndEntities( void );

	int						NumPortals( void ) const;
	qhandle_t				FindPortal( const idBounds &b ) const;
	void					SetPortalState( qhandle_t portal, int blockingBits );
	int						GetPortalState( qhandle_t portal );
	bool					AreasAreConnected( int areaNum1, int areaNum2, portalConnection_t connection );
	void					FloodConnectedAreas( portalArea_t *area, int portalAttributeIndex );
	idScreenRect &			GetAreaScreenRect( int areaNum ) const { return areaScreenRect[areaNum]; }
	void					ShowPortals();

	//--------------------------
	// RenderWorld_demo.cpp

	void					StartWritingDemo( idDemoFile *demo );
	void					StopWritingDemo();
	bool					ProcessDemoCommand( idDemoFile *readDemo, renderView_t *demoRenderView, int *demoTimeOffset );

	void					WriteLoadMap();
	void					WriteRenderView( const renderView_t *renderView );
	void					WriteVisibleDefs( const viewDef_t *viewDef );
	void					WriteFreeLight( qhandle_t handle );
	void					WriteFreeEntity( qhandle_t handle );
	void					WriteFreeEffect( qhandle_t handle );
	void					WriteStopEffect( qhandle_t handle );
	void					WriteRenderLight( qhandle_t handle, const renderLight_t *light );
	void					WriteRenderEntity( qhandle_t handle, const renderEntity_t *ent );
	void					WriteRenderEffect( qhandle_t handle, const rvRenderEffectLocal *effectDef );
	bool					ReadRenderEntity();
	bool					ReadRenderLight();
	bool					ReadRenderEffect();

	// An entity's in-world GUI in a render demo (v11), by entity handle and
	// renderEntity_t::gui slot. Recording writes a GUI's geometry when it
	// changes; playback draws the last geometry read in place of a live GUI.
	void					WriteGuiSurface( qhandle_t handle, int entityGui, const class idGuiModel &model );
	bool					ReadGuiSurface();
	class idGuiModel *		DemoGuiSurface( qhandle_t handle, int entityGui ) const;
	void					FreeDemoGuiSurfaces( qhandle_t handle );	// -1: all
	idList<class idGuiModel *>	demoGuiSurfaces;		// playback, handle * MAX_RENDERENTITY_GUI + slot
	idList<unsigned int>	demoGuiSurfaceChecksums;	// recording, same index; 0 = not written
	class idGuiModel *		demoGuiScratch = NULL;		// recording: the filtered copy written

	// An entity's joints in a render demo (v12). Recording predicts them from the
	// joints it last wrote for the handle and playback from the joints it last
	// read; both sides drop that history at every DC_LOADMAP, so a segment still
	// depends on nothing before it.
	struct demoJointHistory_t {
		int						numJoints = 0;
		bool					hasPrevious = false;	// previous: the record before current, same joint count
		idList<unsigned int>	current;				// the joints as raw IEEE words
		idList<unsigned int>	previous;
	};
	void					WriteDemoJoints( qhandle_t handle, const idJointMat *joints, int numJoints );
	bool					ReadDemoJoints( qhandle_t handle, idJointMat *joints, int numJoints );
	void					ResetDemoJointHistory( qhandle_t handle );	// -1: all
	demoJointHistory_t *	DemoJointHistory( qhandle_t handle, bool create );
	idList<demoJointHistory_t *>	demoJointHistory;	// by entity handle
	idList<byte>			demoJointBuffer;			// one record's encoded joints
	idList<unsigned int>	demoJointWords;				// one record's decoded joints


	//--------------------------
	// RenderWorld.cpp

	void					ResizeInteractionTable();

	void					AddEntityRefToArea( idRenderEntityLocal *def, portalArea_t *area );
	void					TrackThroughWorldOutlineEntity( qhandle_t entityHandle, int outlineFlags );
	void					AddLightRefToArea( idRenderLightLocal *light, portalArea_t *area );

	void					RecurseProcBSP_r( modelTrace_t *results, int parentNodeNum, int nodeNum, float p1f, float p2f, const idVec3 &p1, const idVec3 &p2 ) const;

	void					BoundsInAreas_r( int nodeNum, const idBounds &bounds, int *areas, int *numAreas, int maxAreas ) const;

	float					DrawTextLength( const char *text, float scale, int len = 0 );

	void					FreeInteractions();

	void					PushPolytopeIntoTree_r( idRenderEntityLocal *def, idRenderLightLocal *light, const idBox &box, const idVec3 *points, int numPoints, int nodeNum );

	void					PushPolytopeIntoTree( idRenderEntityLocal *def, idRenderLightLocal *light, const idBox &box, const idVec3 *points, int numPoints );

	void					PushVolumeIntoTree_r( idRenderEntityLocal *def, idRenderLightLocal *light, const idSphere *sphere, int numPoints, const idVec3 (*points), int nodeNum );

	void					PushVolumeIntoTree( idRenderEntityLocal *def, idRenderLightLocal *light, int numPoints, const idVec3 (*points) );

	//-------------------------------
	// tr_light.c
	void					CreateLightDefInteractions( idRenderLightLocal *ldef );
};

#endif /* !__RENDERWORLDLOCAL_H__ */
