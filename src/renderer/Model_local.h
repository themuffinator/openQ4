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

#ifndef __MODEL_LOCAL_H__
#define __MODEL_LOCAL_H__

/*
===============================================================================

	Static model

===============================================================================
*/

class idRenderModelStatic : public idRenderModel {
public:
	// the inherited public interface
	static idRenderModel *		Alloc();

								idRenderModelStatic();
	virtual						~idRenderModelStatic();

	virtual void				InitFromFile( const char *fileName );
	virtual void				PartialInitFromFile( const char *fileName );
	virtual void				PurgeModel();
	virtual void				Reset() {};
	virtual void				LoadModel();
	virtual bool				IsLoaded();
	virtual void				SetLevelLoadReferenced( bool referenced );
	virtual bool				IsLevelLoadReferenced();
	virtual void				TouchData();
	virtual void				InitEmpty( const char *name );
	virtual void				AddSurface( modelSurface_t surface );
	virtual void				FinishSurfaces();
	virtual void				FreeVertexCache();
	virtual const char *		Name() const;
	virtual void				Print() const;
	virtual void				List() const;
	virtual int					Memory() const;
	virtual ID_TIME_T				Timestamp() const;
	virtual int					NumSurfaces() const;
	virtual int					NumBaseSurfaces() const;
	virtual const modelSurface_t *Surface( int surfaceNum ) const;
	virtual srfTriangles_t *	AllocSurfaceTriangles( int numVerts, int numIndexes ) const;
	virtual void				FreeSurfaceTriangles( srfTriangles_t *tris ) const;
	virtual srfTriangles_t *	ShadowHull() const;
	virtual bool				IsStaticWorldModel() const;
	virtual dynamicModel_t		IsDynamicModel() const;
	virtual bool				IsDefaultModel() const;
	virtual bool				IsReloadable() const;
	virtual idRenderModel *		InstantiateDynamicModel( const struct renderEntity_s *ent, const struct viewDef_s *view, idRenderModel *cachedModel );
	virtual int					NumJoints( void ) const;
	virtual const idMD5Joint *	GetJoints( void ) const;
	virtual jointHandle_t		GetJointHandle( const char *name ) const;
	virtual const char *		GetJointName( jointHandle_t handle ) const;
	virtual const idJointQuat *	GetDefaultPose( void ) const;
	virtual int					NearestJoint( int surfaceNum, int a, int b, int c ) const;
	virtual idBounds			Bounds( const struct renderEntity_s *ent ) const;
	virtual void				ReadFromDemoFile( class idDemoFile *f );
	virtual void				WriteToDemoFile( class idDemoFile *f );
	virtual float				DepthHack() const;
	virtual int					GetSurfaceMask( const char *surface ) const;

	void						MakeDefaultModel();
	
	bool						LoadASE( const char *fileName );
	bool						LoadLWO( const char *fileName );
	bool						LoadFLT( const char *fileName );
	bool						LoadMA( const char *filename );

	bool						ConvertASEToModelSurfaces( const struct aseModel_s *ase );
	bool						ConvertLWOToModelSurfaces( const struct st_lwObject *lwo );
	bool						ConvertMAToModelSurfaces (const struct maModel_s *ma );

	struct aseModel_s *			ConvertLWOToASE( const struct st_lwObject *obj, const char *fileName );

	bool						DeleteSurfaceWithId( int id );
	void						DeleteSurfacesWithNegativeId( void );
	bool						FindSurfaceWithId( int id, int &surfaceNum );

public:
	idList<modelSurface_t>		surfaces;
	idBounds					bounds;
	int							overlaysAdded;

protected:
	int							lastModifiedFrame;
	int							lastArchivedFrame;

	idStr						name;
	srfTriangles_t *			shadowHull;
	bool						isStaticWorldModel;
	bool						defaulted;
	bool						purged;					// eventually we will have dynamic reloading
	bool						fastLoad;				// don't generate tangents and shadow data
	bool						reloadable;				// if not, reloadModels won't check timestamp
	bool						levelLoadReferenced;	// for determining if it needs to be freed
	ID_TIME_T						timeStamp;

	static idCVar				r_mergeModelSurfaces;	// combine model surfaces with the same material
	static idCVar				r_slopVertex;			// merge xyz coordinates this far apart
	static idCVar				r_slopTexCoord;			// merge texture coordinates this far apart
	static idCVar				r_slopNormal;			// merge normals that dot less than this
};

/*
===============================================================================

	MD5 animated model

===============================================================================
*/

class idMD5Mesh {
	friend class				idRenderModelMD5;
	friend class				rvRenderModelMD5R;

public:
								idMD5Mesh();
								~idMD5Mesh();

 	void						ParseMesh( idLexer &parser, int numJoints, const idJointMat *joints );
	void						UpdateSurface( const struct renderEntity_s *ent, const idJointMat *joints, modelSurface_t *surf, bool calculateTangents = true );
	idBounds					CalcBounds( const idJointMat *joints );
	int							NearestJoint( int a, int b, int c ) const;
	int							NumVerts( void ) const;
	int							NumTris( void ) const;
	int							NumWeights( void ) const;

private:
	idList<idVec2>				texCoords;			// texture coordinates
	int							numWeights;			// number of weights
	jointWeight_t *				weights;			// retail-style joint weights for SIMD skinning
	idVec4 *					scaledBaseVectors;	// weighted source-space positions
	idVec4 *					baseVectors;		// source-space basis vectors for tangent-preserving skinning
	idVec4 *					scaledWeights;		// joint weights
	int *						weightIndex;		// pairs of: joint offset + bool true if next weight is for next vertex
	const idMaterial *			shader;				// material applied to mesh
	int							numTris;			// number of triangles
	struct deformInfo_s *		deformInfo;			// used to create srfTriangles_t from base frames and new vertexes
	int							surfaceNum;			// number of the static surface created for this mesh
	float						currentTime;		// animation LOD timer

	bool						UpdateLod( const struct renderEntity_s *ent, const struct viewEntity_s *viewEnt, const modelSurface_t *surf );
	void						TransformVerts( idDrawVert *verts, const idJointMat *joints );
	void						TransformScaledVerts( idDrawVert *verts, const idJointMat *joints, float scale );
};

class idRenderModelMD5 : public idRenderModelStatic {
	friend class				rvRenderModelMD5R;

public:
								idRenderModelMD5();
	virtual void				InitFromFile( const char *fileName );
	virtual dynamicModel_t		IsDynamicModel() const;
	virtual idBounds			Bounds( const struct renderEntity_s *ent ) const;
	virtual bool				BoundsFromJoints( const idJointMat *joints, idBounds &bounds ) const;
	virtual void				Print() const;
	virtual void				List() const;
	virtual void				TouchData();
	virtual void				PurgeModel();
	virtual void				LoadModel();
	virtual int					Memory() const;
	virtual idRenderModel *		InstantiateDynamicModel( const struct renderEntity_s *ent, const struct viewDef_s *view, idRenderModel *cachedModel );
	virtual idRenderModel *		InstantiateDynamicModel( const struct renderEntity_s *ent, const struct viewDef_s *view, idRenderModel *cachedModel, dword surfMask );
	virtual bool				HasCollisionSurface( const struct renderEntity_s *ent ) const;
	virtual void				SetViewEntity( const struct viewEntity_s *ve );
	virtual int					NumJoints( void ) const;
	virtual const idMD5Joint *	GetJoints( void ) const;
	virtual jointHandle_t		GetJointHandle( const char *name ) const;
	virtual const char *		GetJointName( jointHandle_t handle ) const;
	virtual const idJointQuat *	GetDefaultPose( void ) const;
	virtual const idJointMat *	GetSkinSpaceToLocalMats( void ) const;
	virtual int					NearestJoint( int surfaceNum, int a, int b, int c ) const;
	virtual int					GetSurfaceMask( const char *surface ) const;

private:
	idList<idMD5Joint>			joints;
	idList<idJointQuat>			defaultPose;
	idList<idJointMat>			skinSpaceToLocalMats;
	idList<idMD5Mesh>			meshes;
	const struct viewEntity_s *	viewEnt;

	void						CalculateBounds( const idJointMat *joints );
	void						GetFrameBounds( const renderEntity_t *ent, idBounds &bounds ) const;
	void						DrawJoints( const renderEntity_t *ent, const struct viewDef_s *view ) const;
	void						ParseJoint( idLexer &parser, idMD5Joint *joint, idJointQuat *defaultPose );
};

typedef enum {
	MD5R_SOURCE_FILE,
	MD5R_SOURCE_MD5,
	MD5R_SOURCE_LWO_ASE_FLT,
	MD5R_SOURCE_PROC
} rvMD5RSource_t;

struct rvMD5RGeometrySpec {
								rvMD5RGeometrySpec() :
									vertexStart( 0 ),
									vertexCount( 0 ),
									indexStart( 0 ),
									primitiveCount( 0 ) {
								}

	int							vertexStart;
	int							vertexCount;
	int							indexStart;
	int							primitiveCount;
};

struct rvMD5RPrimBatch {
								rvMD5RPrimBatch() :
									numTransforms( 1 ),
									numShadowPrimitivesNoCaps( 0 ),
									shadowCapPlaneBits( 0 ),
									silEdgeStart( 0 ),
									silEdgeCount( 0 ),
									hasSilTraceGeoSpec( false ),
									hasDrawGeoSpec( false ),
									hasShadowGeoSpec( false ) {
								}

	int							numTransforms;
	idList<int>					transformPalette;
	rvMD5RGeometrySpec			silTraceGeoSpec;
	rvMD5RGeometrySpec			drawGeoSpec;
	rvMD5RGeometrySpec			shadowVolGeoSpec;
	int							numShadowPrimitivesNoCaps;
	int							shadowCapPlaneBits;
	int							silEdgeStart;
	int							silEdgeCount;
	bool						hasSilTraceGeoSpec;
	bool						hasDrawGeoSpec;
	bool						hasShadowGeoSpec;
};

struct rvMD5RLevelOfDetail {
								rvMD5RLevelOfDetail() :
									rangeEnd( 0.0f ),
									rangeEndSquared( 0.0f ) {
								}

	float						rangeEnd;
	float						rangeEndSquared;
	idList<int>					meshIndexes;
};

class rvRenderModelMD5R;
class rvSilTraceVertT;
class idRenderModelDecal;
struct decalProjectionInfo_s;
struct rvMD5RMesh;
struct rvMD5RVertexBufferDesc;
struct rvMD5RIndexBufferDesc;

#if defined( _MD5R_SUPPORT ) || defined( Q4SDK_MD5R )
bool							R_MD5R_CreateDecalTriangles( idRenderModelDecal *decalModel, const srfTriangles_t &sourceTri, const decalProjectionInfo_s &localInfo );
bool							R_MD5R_CreateLightTris( const srfTriangles_t &sourceTri, srfTriangles_t *destTri, int &c_backfaced, int &c_distance, const byte *facing, const byte *cullBits, bool includeBackFaces );
const rvMD5RMesh *				R_MD5R_GetMeshForTri( const srfTriangles_t *tri );
const rvMD5RVertexBufferDesc *	R_MD5R_GetDrawVertexBufferForTri( const srfTriangles_t *tri );
const rvMD5RIndexBufferDesc *	R_MD5R_GetSilTraceIndexBufferForTri( const srfTriangles_t *tri );
const rvMD5RIndexBufferDesc *	R_MD5R_GetDrawIndexBufferForTri( const srfTriangles_t *tri );
#endif

struct rvMD5RMesh {
								rvMD5RMesh() :
									renderModel( NULL ),
									material( NULL ),
									levelOfDetail( -1 ),
									surfaceNum( -1 ),
									meshIdentifier( 0 ),
									silTraceVertexBuffer( -1 ),
									silTraceIndexBuffer( -1 ),
									drawVertexBuffer( -1 ),
									drawIndexBuffer( -1 ),
									shadowVolVertexBuffer( -1 ),
									shadowVolIndexBuffer( -1 ),
									numSilTraceVertices( 0 ),
									numSilTraceIndices( 0 ),
									numSilTracePrimitives( 0 ),
									numSilEdges( 0 ),
									numDrawVertices( 0 ),
									numDrawIndices( 0 ),
									numDrawPrimitives( 0 ),
									numTransforms( 0 ),
									deformInfo( NULL ) {
									bounds.Clear();
								}

	rvRenderModelMD5R *			renderModel;
	const idMaterial *			material;
	idStr						materialName;
	idBounds					bounds;
	int							levelOfDetail;
	int							surfaceNum;
	int							meshIdentifier;
	int							silTraceVertexBuffer;
	int							silTraceIndexBuffer;
	int							drawVertexBuffer;
	int							drawIndexBuffer;
	int							shadowVolVertexBuffer;
	int							shadowVolIndexBuffer;
	int							numSilTraceVertices;
	int							numSilTraceIndices;
	int							numSilTracePrimitives;
	int							numSilEdges;
	int							numDrawVertices;
	int							numDrawIndices;
	int							numDrawPrimitives;
	int							numTransforms;
	idList<rvMD5RPrimBatch>		primBatches;
	struct deformInfo_s *		deformInfo;
	idList<idDrawVert>			baseDrawVerts;
};

struct rvMD5RVertexFormatDesc {
								rvMD5RVertexFormatDesc() :
									hasPosition( false ),
									positionSwizzled( false ),
									positionDim( 0 ),
									positionTokenType( 0 ),
									hasBlendIndex( false ),
									blendIndexTokenType( 0 ),
									hasBlendWeight( false ),
									blendWeightDim( 0 ),
									blendWeightTransformCount( 0 ),
									blendWeightTokenType( 0 ),
									hasNormal( false ),
									normalTokenType( 0 ),
									hasTangent( false ),
									tangentTokenType( 0 ),
									hasBinormal( false ),
									binormalTokenType( 0 ),
									hasDiffuseColor( false ),
									diffuseColorTokenType( 0 ),
									hasSpecularColor( false ),
									specularColorTokenType( 0 ),
									hasPointSize( false ),
									pointSizeTokenType( 0 ) {
									memset( hasTexCoord, 0, sizeof( hasTexCoord ) );
									memset( texCoordDim, 0, sizeof( texCoordDim ) );
									memset( texCoordTokenType, 0, sizeof( texCoordTokenType ) );
								}

	bool						hasPosition;
	bool						positionSwizzled;
	int							positionDim;
	int							positionTokenType;
	bool						hasBlendIndex;
	int							blendIndexTokenType;
	bool						hasBlendWeight;
	int							blendWeightDim;
	int							blendWeightTransformCount;
	int							blendWeightTokenType;
	bool						hasNormal;
	int							normalTokenType;
	bool						hasTangent;
	int							tangentTokenType;
	bool						hasBinormal;
	int							binormalTokenType;
	bool						hasDiffuseColor;
	int							diffuseColorTokenType;
	bool						hasSpecularColor;
	int							specularColorTokenType;
	bool						hasPointSize;
	int							pointSizeTokenType;
	bool						hasTexCoord[7];
	int							texCoordDim[7];
	int							texCoordTokenType[7];
};

struct rvMD5RVertexBufferDesc {
								rvMD5RVertexBufferDesc() :
									numVertices( 0 ),
									systemMemory( false ),
									videoMemory( false ),
									soA( false ),
									hasVertexFormat( false ),
									hasLoadVertexFormat( false ) {
								}

	int							numVertices;
	bool						systemMemory;
	bool						videoMemory;
	bool						soA;
	bool						hasVertexFormat;
	bool						hasLoadVertexFormat;
	rvMD5RVertexFormatDesc		vertexFormat;
	rvMD5RVertexFormatDesc		loadVertexFormat;
	idList<idVec4>				positions;
	idList<dword>				blendIndices;
	idList<idVec4>				blendWeights;
	idList<idVec3>				normals;
	idList<idVec3>				tangents;
	idList<idVec3>				binormals;
	idList<idVec4>				texCoords[7];
	idList<dword>				diffuseColors;
	idList<dword>				specularColors;
	idList<float>				pointSizes;
};

struct rvMD5RIndexBufferDesc {
								rvMD5RIndexBufferDesc() :
									numIndices( 0 ),
									bitDepth( 32 ),
									systemMemory( false ),
									videoMemory( false ) {
								}

	int							numIndices;
	int							bitDepth;
	bool						systemMemory;
	bool						videoMemory;
	idList<glIndex_t>			indices;
};

#if defined( _MD5R_SUPPORT ) || defined( Q4SDK_MD5R )
bool							R_MD5R_CopyPrimBatchTriangles( idDrawVert *destDrawVerts, glIndex_t *destIndices, const rvMesh *primBatchMesh, const rvSilTraceVertT *silTraceVerts );
#endif

class rvRenderModelMD5R : public idRenderModelStatic {
public:
								rvRenderModelMD5R();
	virtual						~rvRenderModelMD5R();

	virtual void				InitFromFile( const char *fileName );
	virtual void				LoadModel();
	virtual void				PurgeModel();
	virtual void				Print() const;
	virtual void				List() const;
	virtual void				TouchData();
	virtual dynamicModel_t		IsDynamicModel() const;
	virtual idBounds			Bounds( const struct renderEntity_s *ent ) const;
	virtual bool				HasCollisionSurface( const struct renderEntity_s *ent ) const;
	virtual idRenderModel *		InstantiateDynamicModel( const struct renderEntity_s *ent, const struct viewDef_s *view, idRenderModel *cachedModel );
	virtual idRenderModel *		InstantiateDynamicModel( const struct renderEntity_s *ent, const struct viewDef_s *view, idRenderModel *cachedModel, dword surfMask );
	virtual int					NumJoints( void ) const;
	virtual const idMD5Joint *	GetJoints( void ) const;
	virtual jointHandle_t		GetJointHandle( const char *name ) const;
	virtual const char *		GetJointName( jointHandle_t handle ) const;
	virtual const idJointQuat *	GetDefaultPose( void ) const;
	virtual const idJointMat *	GetSkinSpaceToLocalMats( void ) const;
	virtual int					NearestJoint( int surfaceNum, int a, int b, int c ) const;
	virtual int					GetSurfaceMask( const char *surface ) const;
	virtual int					Memory() const;

	bool						InitFromMD5Model( const idRenderModelMD5 &sourceModel );
	bool						InitFromStaticModel( const idRenderModelStatic &sourceModel, rvMD5RSource_t sourceType );
	bool						InitFromProcWorldStaticModel( const idRenderModelStatic &sourceModel, idList<rvMD5RVertexBufferDesc> &sharedVertexBuffers, idList<rvMD5RIndexBufferDesc> &sharedIndexBuffers, idList<silEdge_t> &sharedSilEdges );

	void						InitFromProcWorldModel( Lexer &parser, const idList<rvMD5RVertexBufferDesc> &sharedVertexBuffers, const idList<rvMD5RIndexBufferDesc> &sharedIndexBuffers, const idList<silEdge_t> &sharedSilEdges );
	void						WriteSansBuffers( idFile &outFile, const char *prepend ) const;
	static void					ParseSharedVertexBuffers( Lexer &parser, idList<rvMD5RVertexBufferDesc> &vertexBuffers );
	static void					ParseSharedIndexBuffers( Lexer &parser, idList<rvMD5RIndexBufferDesc> &indexBuffers );
	static void					ParseSharedSilhouetteEdges( Lexer &parser, idList<silEdge_t> &silEdges );
	static void					WriteSharedVertexBuffers( idFile &outFile, const idList<rvMD5RVertexBufferDesc> &vertexBuffers, const char *prepend );
	static void					WriteSharedIndexBuffers( idFile &outFile, const idList<rvMD5RIndexBufferDesc> &indexBuffers, const char *prepend );
	static void					WriteSharedSilhouetteEdges( idFile &outFile, const idList<silEdge_t> &silEdges, const char *prepend );
	static void					WriteAll( bool compressed );

private:
	bool						InitFromStaticModelInternal( const idRenderModelStatic &sourceModel, rvMD5RSource_t sourceType, idList<rvMD5RVertexBufferDesc> *sharedVertexBuffers, idList<rvMD5RIndexBufferDesc> *sharedIndexBuffers, idList<silEdge_t> *sharedSilEdges );
	const idList<rvMD5RVertexBufferDesc> &GetVertexBuffers() const;
	const idList<rvMD5RIndexBufferDesc> &GetIndexBuffers() const;
	const idList<silEdge_t> &GetSilhouetteEdges() const;
	idStr						BuildExportFileName() const;
	bool						CanWriteModelData( idStr &reason ) const;
	bool						WriteFile( const char *fileName, bool compressed ) const;
	void						WriteModel( idFile &outFile ) const;
	static void					WriteVertexFormat( idFile &outFile, const rvMD5RVertexFormatDesc &vertexFormat, const char *prepend );
	static void					WriteVertexBuffer( idFile &outFile, const rvMD5RVertexBufferDesc &vertexBuffer, const char *prepend );
	void						WriteVertexBuffers( idFile &outFile, const char *prepend ) const;
	static void					WriteIndexBuffer( idFile &outFile, const rvMD5RIndexBufferDesc &indexBuffer, const char *prepend );
	void						WriteIndexBuffers( idFile &outFile, const char *prepend ) const;
	void						WriteSilhouetteEdges( idFile &outFile, const char *prepend ) const;
	void						WriteLevelsOfDetail( idFile &outFile, const char *prepend ) const;
	void						WritePrimBatch( idFile &outFile, const rvMD5RPrimBatch &primBatch, const char *prepend ) const;
	void						WriteMesh( idFile &outFile, const rvMD5RMesh &mesh, const char *prepend ) const;
	void						WriteMeshes( idFile &outFile, const char *prepend ) const;
	void						WriteJoints( idFile &outFile, const char *prepend ) const;
	static void					ParseVertexFormat( Lexer &parser, rvMD5RVertexFormatDesc &vertexFormat );
	void						ParseVertexBuffers( Lexer &parser );
	static void					ParseVertexBuffer( Lexer &parser, rvMD5RVertexBufferDesc &vertexBuffer );
	void						ParseIndexBuffers( Lexer &parser );
	static void					ParseIndexBuffer( Lexer &parser, rvMD5RIndexBufferDesc &indexBuffer );
	void						ParseSilhouetteEdges( Lexer &parser );
	void						ParseLevelOfDetail( Lexer &parser );
	void						ParseMeshes( Lexer &parser );
	void						ParseMesh( Lexer &parser, int meshIndex );
	void						ParsePrimBatch( Lexer &parser, rvMD5RPrimBatch &primBatch );
	void						ParseJoints( Lexer &parser );
	void						ParseJoint( Lexer &parser, int jointIndex, idJointQuat &worldPose );
	void						BuildLevelsOfDetail();
	bool						BuildDynamicMeshTemplate( rvMD5RMesh &mesh );
	bool						UpdateDynamicSurface( const rvMD5RMesh &mesh, const idJointMat *entJoints, modelSurface_t &surface, bool calculateTangents ) const;
	bool						GenerateDynamicSurface( idRenderModelStatic &staticModel, rvMD5RMesh &mesh, const renderEntity_s &ent, const idJointMat *entJoints, dword surfMask );
	bool						CopyPrimBatchTriangles( const rvMD5RMesh &mesh, idDrawVert *destDrawVerts, glIndex_t *destIndices, const rvSilTraceVertT *silTraceVerts ) const;
	bool						GenerateStaticSurfaces();
	srfTriangles_t *			GenerateStaticTriSurface( const rvMD5RMesh &mesh ) const;
#if defined( _MD5R_SUPPORT ) || defined( Q4SDK_MD5R )
	friend bool					R_MD5R_CopyPrimBatchTriangles( idDrawVert *destDrawVerts, glIndex_t *destIndices, const rvMesh *primBatchMesh, const rvSilTraceVertT *silTraceVerts );
	friend bool					R_MD5R_CreateDecalTriangles( idRenderModelDecal *decalModel, const srfTriangles_t &sourceTri, const decalProjectionInfo_s &localInfo );
	friend const rvMD5RMesh *	R_MD5R_GetMeshForTri( const srfTriangles_t *tri );
	friend const rvMD5RVertexBufferDesc *	R_MD5R_GetDrawVertexBufferForTri( const srfTriangles_t *tri );
	friend const rvMD5RIndexBufferDesc *	R_MD5R_GetSilTraceIndexBufferForTri( const srfTriangles_t *tri );
	friend const rvMD5RIndexBufferDesc *	R_MD5R_GetDrawIndexBufferForTri( const srfTriangles_t *tri );
#endif
	static void					RemoveFromList( rvRenderModelMD5R &model );

	idList<rvMD5RVertexBufferDesc>	vertexBuffers;
	idList<rvMD5RIndexBufferDesc>	indexBuffers;
	idList<silEdge_t>			silEdges;
	idList<rvMD5RLevelOfDetail>	lods;
	idList<int>					allLODMeshes;
	idList<rvMD5RMesh>			meshes;
	idList<idMD5Joint>			joints;
	idList<idJointQuat>			defaultPose;
	idList<idJointMat>			skinSpaceToLocalMats;
	idStr						commandLine;
	int							md5rVersion;
	bool						metadataOnly;
	bool						geometrySectionsSkipped;
	bool						hasSky;
	rvMD5RSource_t				source;
	rvRenderModelMD5R *			next;
	const idList<rvMD5RVertexBufferDesc> *sharedVertexBuffers;
	const idList<rvMD5RIndexBufferDesc> *sharedIndexBuffers;
	const idList<silEdge_t> *	sharedSilEdges;

	static rvRenderModelMD5R *	modelList;
};

/*
===============================================================================

	MD3 animated model

===============================================================================
*/

struct md3Header_s;
struct md3Surface_s;

class idRenderModelMD3 : public idRenderModelStatic {
public:
	virtual void				InitFromFile( const char *fileName );
	virtual dynamicModel_t		IsDynamicModel() const;
	virtual idRenderModel *		InstantiateDynamicModel( const struct renderEntity_s *ent, const struct viewDef_s *view, idRenderModel *cachedModel );
	virtual idBounds			Bounds( const struct renderEntity_s *ent ) const;

private:
	int							index;			// model = tr.models[model->index]
	int							dataSize;		// just for listing purposes
	struct md3Header_s *		md3;			// only if type == MOD_MESH
	int							numLods;

	void						LerpMeshVertexes( srfTriangles_t *tri, const struct md3Surface_s *surf, const float backlerp, const int frame, const int oldframe ) const;
};

/*
===============================================================================

	Liquid model

===============================================================================
*/

class idRenderModelLiquid : public idRenderModelStatic {
public:
								idRenderModelLiquid();

	virtual void				InitFromFile( const char *fileName );
	virtual dynamicModel_t		IsDynamicModel() const;
	virtual idRenderModel *		InstantiateDynamicModel( const struct renderEntity_s *ent, const struct viewDef_s *view, idRenderModel *cachedModel );
	virtual idBounds			Bounds( const struct renderEntity_s *ent ) const;

	virtual void				Reset();
	void						IntersectBounds( const idBounds &bounds, float displacement );

private:
	modelSurface_t				GenerateSurface( float lerp );
	void						WaterDrop( int x, int y, float *page );
	void						Update( void );
						
	int							verts_x;
	int							verts_y;
	float						scale_x;
	float						scale_y;
	int							time;
	int							liquid_type;
	int							update_tics;
	int							seed;

	idRandom					random;
						
	const idMaterial *			shader;
	struct deformInfo_s	*		deformInfo;		// used to create srfTriangles_t from base frames
											// and new vertexes
						
	float						density;
	float						drop_height;
	int							drop_radius;
	float						drop_delay;

	idList<float>				pages;
	float *						page1;
	float *						page2;

	idList<idDrawVert>			verts;

	int							nextDropTime;

};

/*
===============================================================================

	PRT model

===============================================================================
*/

class idRenderModelPrt : public idRenderModelStatic {
public:
								idRenderModelPrt();

	virtual void				InitFromFile( const char *fileName );
	virtual void				TouchData();
	virtual dynamicModel_t		IsDynamicModel() const;
	virtual idRenderModel *		InstantiateDynamicModel( const struct renderEntity_s *ent, const struct viewDef_s *view, idRenderModel *cachedModel );
	virtual idBounds			Bounds( const struct renderEntity_s *ent ) const;
	virtual float				DepthHack() const;
	virtual int					Memory() const;

private:
	//const idDeclParticle *		particleSystem;
};

/*
===============================================================================

	Beam model

===============================================================================
*/

class idRenderModelBeam : public idRenderModelStatic {
public:
	virtual dynamicModel_t		IsDynamicModel() const;
	virtual bool				IsLoaded() const;
	virtual idRenderModel *		InstantiateDynamicModel( const struct renderEntity_s *ent, const struct viewDef_s *view, idRenderModel *cachedModel );
	virtual idBounds			Bounds( const struct renderEntity_s *ent ) const;
};

/*
===============================================================================

	Beam model

===============================================================================
*/
#define MAX_TRAIL_PTS	20

struct Trail_t {
	int							lastUpdateTime;
	int							duration;

	idVec3						pts[MAX_TRAIL_PTS];
	int							numPoints;
};

class idRenderModelTrail : public idRenderModelStatic {
	idList<Trail_t>				trails;
	int							numActive;
	idBounds					trailBounds;

public:
								idRenderModelTrail();

	virtual dynamicModel_t		IsDynamicModel() const;
	virtual bool				IsLoaded() const;
	virtual idRenderModel *		InstantiateDynamicModel( const struct renderEntity_s *ent, const struct viewDef_s *view, idRenderModel *cachedModel );
	virtual idBounds			Bounds( const struct renderEntity_s *ent ) const;

	int							NewTrail( idVec3 pt, int duration );
	void						UpdateTrail( int index, idVec3 pt );
	void						DrawTrail( int index, const struct renderEntity_s *ent, srfTriangles_t *tri, float globalAlpha );
};

/*
===============================================================================

	Lightning model

===============================================================================
*/

class idRenderModelLightning : public idRenderModelStatic {
public:
	virtual dynamicModel_t		IsDynamicModel() const;
	virtual bool				IsLoaded() const;
	virtual idRenderModel *		InstantiateDynamicModel( const struct renderEntity_s *ent, const struct viewDef_s *view, idRenderModel *cachedModel );
	virtual idBounds			Bounds( const struct renderEntity_s *ent ) const;
};

/*
================================================================================

	idRenderModelSprite 

================================================================================
*/
class idRenderModelSprite : public idRenderModelStatic {
public:
	virtual	dynamicModel_t	IsDynamicModel() const;
	virtual	bool			IsLoaded() const;
	virtual	idRenderModel *	InstantiateDynamicModel( const struct renderEntity_s *ent, const struct viewDef_s *view, idRenderModel *cachedModel );
	virtual	idBounds		Bounds( const struct renderEntity_s *ent ) const;
};

#endif /* !__MODEL_LOCAL_H__ */
