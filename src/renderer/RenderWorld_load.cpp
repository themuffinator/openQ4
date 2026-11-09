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




#include "tr_local.h"
#include "Model_local.h"

class idRenderWorldMD5RProcData {
public:
	idList<rvMD5RVertexBufferDesc>	vertexBuffers;
	idList<rvMD5RIndexBufferDesc>	indexBuffers;
	idList<silEdge_t>				silEdges;
	idList<rvRenderModelMD5R *>		models;

	void Clear() {
		vertexBuffers.Clear();
		indexBuffers.Clear();
		silEdges.Clear();
		models.Clear();
	}

	bool HasPackedWorldData() const {
		return vertexBuffers.Num() > 0 || indexBuffers.Num() > 0 || silEdges.Num() > 0 || models.Num() > 0;
	}
};

/*
================
R_RenderWorld_EnsureMD5RProcData
================
*/
static idRenderWorldMD5RProcData &R_RenderWorld_EnsureMD5RProcData( idRenderWorldLocal &world ) {
	if ( world.md5rProcData == NULL ) {
		world.md5rProcData = new idRenderWorldMD5RProcData;
	}

	return *world.md5rProcData;
}

/*
================
R_RenderWorld_ClearMD5RProcData
================
*/
static void R_RenderWorld_ClearMD5RProcData( idRenderWorldLocal &world ) {
	if ( world.md5rProcData != NULL ) {
		delete world.md5rProcData;
		world.md5rProcData = NULL;
	}
}

/*
================
R_RenderWorld_IsShadowModel
================
*/
static bool R_RenderWorld_IsShadowModel( const idRenderModel &model ) {
	const int surfaceCount = model.NumSurfaces();
	if ( surfaceCount <= 0 ) {
		return false;
	}

	for ( int surfaceIndex = 0; surfaceIndex < surfaceCount; ++surfaceIndex ) {
		const modelSurface_t *surface = model.Surface( surfaceIndex );
		if ( surface == NULL || surface->geometry == NULL ) {
			continue;
		}

		const srfTriangles_t *tri = surface->geometry;
		if ( tri->shadowVertexes != NULL && tri->verts == NULL ) {
			return true;
		}

		if ( tri->verts != NULL ) {
			return false;
		}
	}

	return false;
}

/*
================
R_RenderWorld_HasRenderableSurfaces
================
*/
static bool R_RenderWorld_HasRenderableSurfaces( const idRenderModel &model ) {
	const int surfaceCount = model.NumSurfaces();
	if ( surfaceCount <= 0 ) {
		return false;
	}

	for ( int surfaceIndex = 0; surfaceIndex < surfaceCount; ++surfaceIndex ) {
		const modelSurface_t *surface = model.Surface( surfaceIndex );
		if ( surface == NULL || surface->geometry == NULL || surface->shader == NULL ) {
			continue;
		}

		const srfTriangles_t *tri = surface->geometry;
		if ( tri->verts != NULL && tri->indexes != NULL && tri->numVerts > 0 && tri->numIndexes > 0 ) {
			return true;
		}
	}

	return false;
}

/*
================
R_RenderWorld_WriteClassicMD5RProcModel
================
*/
static void R_RenderWorld_WriteClassicMD5RProcModel( idFile &outFile, const idRenderModel &model ) {
	int numExportableSurfaces = 0;
	const int surfaceCount = model.NumSurfaces();
	for ( int surfaceIndex = 0; surfaceIndex < surfaceCount; ++surfaceIndex ) {
		const modelSurface_t *surface = model.Surface( surfaceIndex );
		if ( surface == NULL || surface->geometry == NULL || surface->shader == NULL ) {
			continue;
		}

		const srfTriangles_t *tri = surface->geometry;
		if ( tri->verts == NULL || tri->indexes == NULL ) {
			continue;
		}

		++numExportableSurfaces;
	}

	outFile.WriteFloatString( "model {\n" );
	outFile.WriteFloatString( "\"%s\"\n", model.Name() );
	outFile.WriteFloatString( "%d\n", numExportableSurfaces );
	outFile.WriteFloatString( "0\n" );

	for ( int surfaceIndex = 0; surfaceIndex < surfaceCount; ++surfaceIndex ) {
		const modelSurface_t *surface = model.Surface( surfaceIndex );
		if ( surface == NULL || surface->geometry == NULL || surface->shader == NULL ) {
			continue;
		}

		const srfTriangles_t *tri = surface->geometry;
		if ( tri->verts == NULL || tri->indexes == NULL ) {
			continue;
		}

		outFile.WriteFloatString( "{\n" );
		outFile.WriteFloatString( "\"%s\"\n", surface->shader->GetName() );
		outFile.WriteFloatString( "%d %d\n", tri->numVerts, tri->numIndexes );

		for ( int vertexIndex = 0; vertexIndex < tri->numVerts; ++vertexIndex ) {
			const idDrawVert &vert = tri->verts[ vertexIndex ];
			outFile.WriteFloatString(
				"( %f %f %f %f %f %f %f %f %d %d %d %d )\n",
				vert.xyz.x, vert.xyz.y, vert.xyz.z,
				vert.st.x, vert.st.y,
				vert.normal.x, vert.normal.y, vert.normal.z,
				static_cast<int>( vert.color[ 0 ] ),
				static_cast<int>( vert.color[ 1 ] ),
				static_cast<int>( vert.color[ 2 ] ),
				static_cast<int>( vert.color[ 3 ] ) );
		}

		for ( int index = 0; index < tri->numIndexes; ++index ) {
			outFile.WriteFloatString( "%d\n", tri->indexes[ index ] );
		}

		outFile.WriteFloatString( "}\n" );
	}

	outFile.WriteFloatString( "}\n\n" );
}

/*
================
R_RenderWorld_WriteClassicMD5RProcShadowModel
================
*/
static void R_RenderWorld_WriteClassicMD5RProcShadowModel( idFile &outFile, const idRenderModel &model ) {
	if ( model.NumSurfaces() <= 0 ) {
		return;
	}

	const modelSurface_t *surface = model.Surface( 0 );
	if ( surface == NULL || surface->geometry == NULL ) {
		return;
	}

	const srfTriangles_t *tri = surface->geometry;
	outFile.WriteFloatString( "shadowModel {\n" );
	outFile.WriteFloatString( "\"%s\"\n", model.Name() );
	outFile.WriteFloatString(
		"%d %d %d %d %d\n",
		tri->numVerts,
		tri->numShadowIndexesNoCaps,
		tri->numShadowIndexesNoFrontCaps,
		tri->numIndexes,
		tri->shadowCapPlaneBits );

	for ( int vertexIndex = 0; vertexIndex < tri->numVerts; ++vertexIndex ) {
		outFile.WriteFloatString(
			"( %f %f %f )\n",
			tri->shadowVertexes[ vertexIndex ].xyz[ 0 ],
			tri->shadowVertexes[ vertexIndex ].xyz[ 1 ],
			tri->shadowVertexes[ vertexIndex ].xyz[ 2 ] );
	}

	for ( int index = 0; index < tri->numIndexes; ++index ) {
		outFile.WriteFloatString( "%d\n", tri->indexes[ index ] );
	}

	outFile.WriteFloatString( "}\n\n" );
}

/*
================
R_RenderWorld_WriteClassicMD5RProcInterAreaPortals
================
*/
static void R_RenderWorld_WriteClassicMD5RProcInterAreaPortals( const idRenderWorldLocal &world, idFile &outFile ) {
	if ( world.numPortalAreas <= 0 ) {
		return;
	}

	outFile.WriteFloatString( "interAreaPortals {\n" );
	outFile.WriteFloatString( "%d\n", world.numPortalAreas );
	outFile.WriteFloatString( "%d\n", world.numInterAreaPortals );

	for ( int portalIndex = 0; portalIndex < world.numInterAreaPortals; ++portalIndex ) {
		const doublePortal_t &doublePortal = world.doublePortals[ portalIndex ];
		const portal_t *positivePortal = doublePortal.portals[ 0 ];
		const portal_t *negativePortal = doublePortal.portals[ 1 ];
		const idWinding *winding = ( positivePortal != NULL ) ? positivePortal->w : NULL;
		if ( positivePortal == NULL || negativePortal == NULL || winding == NULL ) {
			continue;
		}

		const int windingPointCount = winding->GetNumPoints();
		outFile.WriteFloatString(
			"%d %d %d ",
			windingPointCount,
			negativePortal->intoArea,
			positivePortal->intoArea );

		for ( int pointIndex = 0; pointIndex < windingPointCount; ++pointIndex ) {
			outFile.WriteFloatString(
				"( %f %f %f ) ",
				( *winding )[ pointIndex ].x,
				( *winding )[ pointIndex ].y,
				( *winding )[ pointIndex ].z );
		}

		if ( positivePortal->image != NULL ) {
			outFile.WriteFloatString(
				"( \"%s\" %.2f %.2f )",
				positivePortal->image->GetName(),
				positivePortal->cullNear,
				positivePortal->cullFar );
		}

		outFile.WriteFloatString( "\n" );
	}

	outFile.WriteFloatString( "}\n\n" );
}

/*
================
R_RenderWorld_WriteClassicMD5RProcNodes
================
*/
static void R_RenderWorld_WriteClassicMD5RProcNodes( const idRenderWorldLocal &world, idFile &outFile ) {
	if ( world.areaNodes == NULL || world.numAreaNodes <= 0 ) {
		return;
	}

	outFile.WriteFloatString( "nodes {\n" );
	outFile.WriteFloatString( "%d\n", world.numAreaNodes );

	for ( int nodeIndex = 0; nodeIndex < world.numAreaNodes; ++nodeIndex ) {
		const areaNode_t &node = world.areaNodes[ nodeIndex ];
		outFile.WriteFloatString(
			"( %f %f %f %f ) %d %d\n",
			node.plane[ 0 ],
			node.plane[ 1 ],
			node.plane[ 2 ],
			node.plane[ 3 ],
			node.children[ 0 ],
			node.children[ 1 ] );
	}

	outFile.WriteFloatString( "}\n" );
}

/*
================
R_RenderWorld_WritePackedMD5RProcModels
================
*/
static void R_RenderWorld_WritePackedMD5RProcModels( idFile &outFile, const idRenderWorldMD5RProcData &md5rProcData ) {
	if ( md5rProcData.models.Num() == 0 ) {
		return;
	}

	outFile.WriteFloatString( "Model[ %d ]\n", md5rProcData.models.Num() );
	outFile.WriteFloatString( "{\n" );

	for ( int modelIndex = 0; modelIndex < md5rProcData.models.Num(); ++modelIndex ) {
		const rvRenderModelMD5R *model = md5rProcData.models[ modelIndex ];
		if ( model == NULL ) {
			continue;
		}

		outFile.WriteFloatString( "\tModel \"%s\"\n", model->Name() );
		outFile.WriteFloatString( "\t{\n" );
		model->WriteSansBuffers( outFile, "\t\t" );
		outFile.WriteFloatString( "\t}\n" );
	}

	outFile.WriteFloatString( "}\n\n" );
}

/*
================
idRenderWorldLocal::ConvertProcToMD5R
================
*/
void idRenderWorldLocal::ConvertProcToMD5R() {
	idRenderWorldMD5RProcData &md5rProcData = R_RenderWorld_EnsureMD5RProcData( *this );
	md5rProcData.Clear();

	idList<int> convertedIndexes;
	idList<rvRenderModelMD5R *> convertedModels;

	for ( int modelIndex = 0; modelIndex < localModels.Num(); ++modelIndex ) {
		idRenderModel *model = localModels[ modelIndex ];
		if ( model == NULL || R_RenderWorld_IsShadowModel( *model ) ) {
			continue;
		}
		if ( !R_RenderWorld_HasRenderableSurfaces( *model ) ) {
			continue;
		}

		idRenderModelStatic *staticModel = dynamic_cast<idRenderModelStatic *>( model );
		if ( staticModel == NULL || staticModel->IsDefaultModel() ) {
			common->Warning(
				"idRenderWorldLocal::ConvertProcToMD5R: can't convert proc model '%s' to MD5R",
				( model != NULL ) ? model->Name() : "<null>" );
			goto conversionFailed;
		}

		rvRenderModelMD5R *convertedModel = new rvRenderModelMD5R;
		if ( convertedModel == NULL ) {
			common->Error( "idRenderWorldLocal::ConvertProcToMD5R: out of memory" );
			goto conversionFailed;
		}

		if ( !convertedModel->InitFromProcWorldStaticModel(
			*staticModel,
			md5rProcData.vertexBuffers,
			md5rProcData.indexBuffers,
			md5rProcData.silEdges ) ) {
			common->Warning(
				"idRenderWorldLocal::ConvertProcToMD5R: failed to convert proc model '%s' to shared MD5R data",
				model->Name() );
			delete convertedModel;
			goto conversionFailed;
		}

		convertedIndexes.Append( modelIndex );
		convertedModels.Append( convertedModel );
	}

	if ( convertedModels.Num() <= 0 ) {
		R_RenderWorld_ClearMD5RProcData( *this );
		return;
	}

	for ( int convertedIndex = 0; convertedIndex < convertedIndexes.Num(); ++convertedIndex ) {
		const int modelIndex = convertedIndexes[ convertedIndex ];
		idRenderModel *oldModel = localModels[ modelIndex ];
		rvRenderModelMD5R *convertedModel = convertedModels[ convertedIndex ];

		renderModelManager->RemoveModel( oldModel );
		delete oldModel;

		renderModelManager->AddModel( convertedModel );
		localModels[ modelIndex ] = convertedModel;
		md5rProcData.models.Append( convertedModel );
	}

	common->Printf(
		"idRenderWorldLocal::ConvertProcToMD5R: converted %d proc model(s) to shared MD5R data for '%s'\n",
		convertedModels.Num(),
		mapName.c_str() );
	return;

conversionFailed:
	for ( int convertedModelIndex = 0; convertedModelIndex < convertedModels.Num(); ++convertedModelIndex ) {
		delete convertedModels[ convertedModelIndex ];
	}
	md5rProcData.Clear();
}

/*
================
R_RenderWorld_FinalizeLoadedWorld
================
*/
static void R_RenderWorld_FinalizeLoadedWorld( idRenderWorldLocal &world ) {
	if ( !world.numPortalAreas || world.areaNodes == NULL ) {
		world.ClearWorld();
	}

	world.CommonChildrenArea_r( &world.areaNodes[ 0 ] );
	world.AddWorldModelEntities();
	world.SetupLightGrid();
	world.ClearPortalStates();
}

namespace {
	// This is intentionally a different format from BFG's trusted bproc stream.
	// The BFG serializer was used only as a field inventory (models, portals and
	// nodes); all bounds, validation and transactional publication below are
	// openQ4-specific.
	static const unsigned int RENDER_WORLD_CACHE_MAGIC = 0x5734514fU; // "OQ4W"
	static const int RENDER_WORLD_CACHE_VERSION = 2;
	static const unsigned int RENDER_WORLD_CACHE_PARSER_VERSION = 4u;	// bump when the parsed models change; 3: surface ids were uninitialised before; 4: silhouette edges stopped at 65536
	static const unsigned int RENDER_WORLD_CACHE_FLAGS = 0u;
	static const unsigned int RENDER_WORLD_SHADOW_CACHE_MAGIC = 0x4834514fU; // "OQ4H"
	static const int RENDER_WORLD_SHADOW_CACHE_VERSION = 1;
	static const int RENDER_WORLD_CACHE_MODEL_STATIC = 1;
	static const int RENDER_WORLD_CACHE_MODEL_SHADOW = 2;
	static const int RENDER_WORLD_CACHE_MAX_MAP_NAME = 4096;
	static const int RENDER_WORLD_CACHE_MAX_IMAGE_NAME = 4096;
	static const int RENDER_WORLD_CACHE_MAX_MODELS = 65536;
	static const int RENDER_WORLD_CACHE_MAX_MODEL_PAYLOAD = 128 * 1024 * 1024;
	static const uint64_t RENDER_WORLD_CACHE_MAX_MODEL_MEMORY = 384ULL * 1024ULL * 1024ULL;
	static const int RENDER_WORLD_CACHE_MAX_SURFACE_VERTS = 1 << 20;
	static const int RENDER_WORLD_CACHE_MAX_SURFACE_INDEXES = 1 << 24;
	static const int RENDER_WORLD_CACHE_MAX_SHADOW_VERTS = 1 << 20;
	static const int RENDER_WORLD_CACHE_MAX_SHADOW_INDEXES = 1 << 24;
	static const int RENDER_WORLD_CACHE_MAX_AREAS = 4096;
	static const int RENDER_WORLD_CACHE_MAX_PORTALS = 16384;
	static const int RENDER_WORLD_CACHE_MAX_WINDING_POINTS = 4096;
	static const int RENDER_WORLD_CACHE_MAX_TOTAL_WINDING_POINTS = 1 << 20;
	static const int RENDER_WORLD_CACHE_MAX_NODES = 1 << 20;
	static const int RENDER_WORLD_CACHE_MAX_NODE_DEPTH = 1024;
	static const int RENDER_WORLD_CACHE_MAX_PAYLOAD_BYTES = 512 * 1024 * 1024;
	static const float RENDER_WORLD_CACHE_MAX_COORDINATE = 1000000000.0f;
	static const char *const RENDER_WORLD_CACHE_CLASSIC_SETTINGS = "proc4-static-cpu-v2";
	static const char *const RENDER_WORLD_CACHE_CLASSIC_SEMANTIC = "classic-proc";
	static const char *const RENDER_WORLD_CACHE_MD5R_SEMANTIC = "md5rproc";

	static unsigned int R_RenderWorldCacheFloatBits( float value ) {
		unsigned int bits = 0;
		static_assert( sizeof( bits ) == sizeof( value ), "cache setting float width" );
		memcpy( &bits, &value, sizeof( bits ) );
		return bits;
	}

	static idStr R_RenderWorldCacheBuildSettingsKey() {
		// FinishSurfaces consumes these merge/slop settings, so they are part of
		// the cached CPU representation rather than merely runtime preferences.
		return va(
			"%s-m%d-s%d-v%08x-t%08x-n%08x",
			RENDER_WORLD_CACHE_CLASSIC_SETTINGS,
			cvarSystem->GetCVarBool( "r_mergeModelSurfaces" ) ? 1 : 0,
			cvarSystem->GetCVarBool( "r_useSilRemap" ) ? 1 : 0,
			R_RenderWorldCacheFloatBits( cvarSystem->GetCVarFloat( "r_slopVertex" ) ),
			R_RenderWorldCacheFloatBits( cvarSystem->GetCVarFloat( "r_slopTexCoord" ) ),
			R_RenderWorldCacheFloatBits( cvarSystem->GetCVarFloat( "r_slopNormal" ) ) );
	}

	struct renderWorldCachePortalStage_t {
		int areas[2];
		idList<idVec3> points;
		idStr imageName;
		float cullNear;
		float cullFar;

		renderWorldCachePortalStage_t() : cullNear( 0.0f ), cullFar( 0.0f ) {
			areas[0] = areas[1] = -1;
		}
	};

	class idRenderWorldCacheModelOwner {
	public:
		~idRenderWorldCacheModelOwner() {
			for ( int i = 0; i < models.Num(); ++i ) {
				delete models[i];
			}
		}

		void Release() {
			models.Clear();
		}

		idList<idRenderModel *> models;
	};

	class idRenderWorldCacheMemoryFileOwner {
	public:
		idRenderWorldCacheMemoryFileOwner() {
			file = fileSystem != NULL ? fileSystem->GetNewFileMemory() : NULL;
		}

		~idRenderWorldCacheMemoryFileOwner() {
			if ( file != NULL ) {
				fileSystem->CloseFile( file );
			}
		}

		idFile *Get() const {
			return file;
		}

	private:
		idRenderWorldCacheMemoryFileOwner( const idRenderWorldCacheMemoryFileOwner & );
		idRenderWorldCacheMemoryFileOwner &operator=( const idRenderWorldCacheMemoryFileOwner & );

		idFile *file;
	};

	static int R_RenderWorldCacheBoundedStringLength( const char *value, int maximumLength ) {
		if ( value == NULL || maximumLength < 0 ) {
			return -1;
		}
		for ( int length = 0; length <= maximumLength; ++length ) {
			if ( value[length] == '\0' ) {
				return length;
			}
		}
		return -1;
	}

	static bool R_RenderWorldCacheStringIsSafe( const char *value, int maximumLength, bool allowEmpty ) {
		const int length = R_RenderWorldCacheBoundedStringLength( value, maximumLength );
		if ( length < 0 || ( !allowEmpty && length == 0 ) ) {
			return false;
		}
		for ( int i = 0; i < length; ++i ) {
			const unsigned char c = static_cast<unsigned char>( value[i] );
			if ( c < 32 || c == 127 ) {
				return false;
			}
		}
		return true;
	}

	static bool R_RenderWorldCacheCoordinateIsValid( float value ) {
		return R_RenderModelCacheFloatIsFinite( value )
			&& idMath::Fabs( value ) <= RENDER_WORLD_CACHE_MAX_COORDINATE;
	}

	static bool R_RenderWorldCacheVec3IsValid( const idVec3 &value ) {
		return R_RenderWorldCacheCoordinateIsValid( value.x )
			&& R_RenderWorldCacheCoordinateIsValid( value.y )
			&& R_RenderWorldCacheCoordinateIsValid( value.z );
	}

	static bool R_RenderWorldCacheBoundsIsValid( const idBounds &bounds ) {
		if ( bounds.IsCleared() ) {
			return false;
		}
		return R_RenderWorldCacheVec3IsValid( bounds[0] )
			&& R_RenderWorldCacheVec3IsValid( bounds[1] )
			&& bounds[0].x <= bounds[1].x
			&& bounds[0].y <= bounds[1].y
			&& bounds[0].z <= bounds[1].z;
	}

	/*
	================
	R_RenderWorldCacheWriteShadowModel

	Classic proc shadowModel blocks are deliberately not ordinary static-model
	payloads: their sole surface owns shadow vertices and indexes, but no draw
	vertices.  Keep this narrow source representation local to the render-world
	cache instead of broadening the standalone model codec.
	================
	*/
	static bool R_RenderWorldCacheWriteShadowModel( idFile &file, const idRenderModel &model ) {
		if ( model.IsDefaultModel() || model.IsDynamicModel() != DM_STATIC
			|| model.IsReloadable() || model.IsStaticWorldModel()
			|| model.NumSurfaces() != 1 || model.NumBaseSurfaces() != 1
			|| !R_RenderWorldCacheStringIsSafe( model.Name(), RENDER_WORLD_CACHE_MAX_MAP_NAME, false ) ) {
			return false;
		}

		const modelSurface_t *surface = model.Surface( 0 );
		if ( surface == NULL || surface->shader != tr.defaultMaterial || surface->geometry == NULL ) {
			return false;
		}
		const srfTriangles_t *tri = surface->geometry;
		if ( tri->verts != NULL || tri->shadowVertexes == NULL || tri->indexes == NULL
			|| tri->numVerts <= 0 || tri->numVerts > RENDER_WORLD_CACHE_MAX_SHADOW_VERTS
			|| tri->numIndexes <= 0 || tri->numIndexes > RENDER_WORLD_CACHE_MAX_SHADOW_INDEXES
			|| ( tri->numIndexes % 3 ) != 0
			|| tri->numShadowIndexesNoCaps < 0 || ( tri->numShadowIndexesNoCaps % 3 ) != 0
			|| tri->numShadowIndexesNoFrontCaps < tri->numShadowIndexesNoCaps
			|| ( tri->numShadowIndexesNoFrontCaps % 3 ) != 0
			|| tri->numShadowIndexesNoFrontCaps > tri->numIndexes
			|| tri->shadowCapPlaneBits < 0 || tri->shadowCapPlaneBits > 127
			|| tri->deformedSurface || !R_RenderWorldCacheBoundsIsValid( tri->bounds )
			|| tri->silIndexes != NULL || tri->numMirroredVerts != 0 || tri->mirroredVerts != NULL
			|| tri->numDupVerts != 0 || tri->dupVerts != NULL
			|| tri->numSilEdges != 0 || tri->silEdges != NULL
			|| tri->facePlanes != NULL || tri->dominantTris != NULL
			|| R_TriHasPrimBatchMesh( tri ) ) {
			return false;
		}
#if defined( _MD5R_SUPPORT ) || defined( Q4SDK_MD5R )
		if ( tri->silTraceVerts != NULL || tri->numSkinToModelTransforms != 0
			|| tri->skinToModelTransforms != NULL || tri->skinToModelTransformsAlloc != NULL ) {
			return false;
		}
#endif

		idBounds computedBounds;
		computedBounds.Clear();
		for ( int i = 0; i < tri->numVerts; ++i ) {
			const idVec4 &vertex = tri->shadowVertexes[i].xyz;
			if ( vertex.w != 1.0f || !R_RenderWorldCacheVec3IsValid( vertex.ToVec3() ) ) {
				return false;
			}
			computedBounds.AddPoint( vertex.ToVec3() );
		}
		if ( !computedBounds.Compare( tri->bounds ) ) {
			return false;
		}
		for ( int i = 0; i < tri->numIndexes; ++i ) {
			if ( tri->indexes[i] < 0 || tri->indexes[i] >= tri->numVerts ) {
				return false;
			}
		}

		idRenderModelCacheWriter writer( file );
		if ( !writer.WriteUnsignedInt( RENDER_WORLD_SHADOW_CACHE_MAGIC )
			|| !writer.WriteInt( RENDER_WORLD_SHADOW_CACHE_VERSION )
			|| !writer.WriteString( model.Name(), RENDER_WORLD_CACHE_MAX_MAP_NAME )
			|| !writer.WriteInt( tri->numVerts )
			|| !writer.WriteInt( tri->numShadowIndexesNoCaps )
			|| !writer.WriteInt( tri->numShadowIndexesNoFrontCaps )
			|| !writer.WriteInt( tri->numIndexes )
			|| !writer.WriteInt( tri->shadowCapPlaneBits ) ) {
			return false;
		}
		for ( int i = 0; i < tri->numVerts; ++i ) {
			if ( !writer.WriteVec3( tri->shadowVertexes[i].xyz.ToVec3() ) ) {
				return false;
			}
		}
		for ( int i = 0; i < tri->numIndexes; ++i ) {
			if ( !writer.WriteInt( tri->indexes[i] ) ) {
				return false;
			}
		}
		return writer.IsValid();
	}

	static idRenderModelStatic *R_RenderWorldCacheReadShadowModel( idFile &file ) {
		idRenderModelCacheReader reader( file );
		unsigned int magic = 0;
		int version = 0;
		idStr name;
		int numVerts = 0;
		int numIndexes = 0;
		int numIndexesNoCaps = 0;
		int numIndexesNoFrontCaps = 0;
		int shadowCapPlaneBits = 0;
		if ( !reader.ReadUnsignedInt( magic ) || magic != RENDER_WORLD_SHADOW_CACHE_MAGIC
			|| !reader.ReadInt( version ) || version != RENDER_WORLD_SHADOW_CACHE_VERSION
			|| !reader.ReadString( name, RENDER_WORLD_CACHE_MAX_MAP_NAME )
			|| !R_RenderWorldCacheStringIsSafe( name.c_str(), RENDER_WORLD_CACHE_MAX_MAP_NAME, false )
			|| !reader.ReadCount( numVerts, RENDER_WORLD_CACHE_MAX_SHADOW_VERTS, sizeof( idVec4 ) )
			|| numVerts <= 0
			|| !reader.ReadCount( numIndexesNoCaps, RENDER_WORLD_CACHE_MAX_SHADOW_INDEXES )
			|| !reader.ReadCount( numIndexesNoFrontCaps, RENDER_WORLD_CACHE_MAX_SHADOW_INDEXES )
			|| !reader.ReadCount( numIndexes, RENDER_WORLD_CACHE_MAX_SHADOW_INDEXES, sizeof( glIndex_t ) )
			|| numIndexes <= 0 || ( numIndexes % 3 ) != 0
			|| numIndexesNoCaps > numIndexesNoFrontCaps || numIndexesNoFrontCaps > numIndexes
			|| ( numIndexesNoCaps % 3 ) != 0 || ( numIndexesNoFrontCaps % 3 ) != 0
			|| !reader.ReadInt( shadowCapPlaneBits ) || shadowCapPlaneBits < 0 || shadowCapPlaneBits > 127 ) {
			return NULL;
		}

		idList<idVec3> vertices;
		idList<glIndex_t> indexes;
		vertices.SetNum( numVerts );
		indexes.SetNum( numIndexes );
		idBounds bounds;
		bounds.Clear();
		for ( int i = 0; i < numVerts; ++i ) {
			if ( !reader.ReadVec3( vertices[i] ) || !R_RenderWorldCacheVec3IsValid( vertices[i] ) ) {
				return NULL;
			}
			bounds.AddPoint( vertices[i] );
		}
		if ( !R_RenderWorldCacheBoundsIsValid( bounds ) ) {
			return NULL;
		}
		for ( int i = 0; i < numIndexes; ++i ) {
			if ( !reader.ReadInt( indexes[i] ) || indexes[i] < 0 || indexes[i] >= numVerts ) {
				return NULL;
			}
		}
		if ( !reader.IsValid() || tr.defaultMaterial == NULL ) {
			return NULL;
		}

		idRenderModelStatic *model = new idRenderModelStatic;
		if ( model == NULL ) {
			return NULL;
		}
		model->InitEmpty( name.c_str() );
		if ( model->IsStaticWorldModel() ) {
			delete model;
			return NULL;
		}
		srfTriangles_t *tri = R_AllocStaticTriSurf();
		tri->numVerts = numVerts;
		tri->numIndexes = numIndexes;
		tri->numShadowIndexesNoCaps = numIndexesNoCaps;
		tri->numShadowIndexesNoFrontCaps = numIndexesNoFrontCaps;
		tri->shadowCapPlaneBits = shadowCapPlaneBits;
		tri->bounds = bounds;
		R_AllocStaticTriSurfShadowVerts( tri, numVerts );
		for ( int i = 0; i < numVerts; ++i ) {
			tri->shadowVertexes[i].xyz.Set( vertices[i].x, vertices[i].y, vertices[i].z, 1.0f );
		}
		R_AllocStaticTriSurfIndexes( tri, numIndexes );
		memcpy( tri->indexes, indexes.Ptr(), numIndexes * sizeof( indexes[0] ) );

		modelSurface_t surface;
		surface.id = 0;
		surface.shader = tr.defaultMaterial;
		surface.geometry = tri;
		surface.mOriginalSurfaceName = NULL;
		model->AddSurface( surface );
		return model;
	}

	static bool R_RenderWorldCachePlaneIsValid( const idPlane &plane ) {
		for ( int i = 0; i < 4; ++i ) {
			if ( !R_RenderWorldCacheCoordinateIsValid( plane[i] ) ) {
				return false;
			}
		}
		const double normalLengthSquared =
			static_cast<double>( plane[0] ) * plane[0]
			+ static_cast<double>( plane[1] ) * plane[1]
			+ static_cast<double>( plane[2] ) * plane[2];
		return normalLengthSquared >= 0.25 && normalLengthSquared <= 4.0;
	}

	static bool R_RenderWorldCacheWindingIsValid( const idVec3 *points, int numPoints ) {
		if ( points == NULL || numPoints < 3 || numPoints > RENDER_WORLD_CACHE_MAX_WINDING_POINTS ) {
			return false;
		}
		for ( int i = 0; i < numPoints; ++i ) {
			if ( !R_RenderWorldCacheVec3IsValid( points[i] ) ) {
				return false;
			}
		}

		const double edgeX = static_cast<double>( points[1].x ) - points[0].x;
		const double edgeY = static_cast<double>( points[1].y ) - points[0].y;
		const double edgeZ = static_cast<double>( points[1].z ) - points[0].z;
		for ( int i = 2; i < numPoints; ++i ) {
			const double otherX = static_cast<double>( points[i].x ) - points[0].x;
			const double otherY = static_cast<double>( points[i].y ) - points[0].y;
			const double otherZ = static_cast<double>( points[i].z ) - points[0].z;
			const double crossX = edgeY * otherZ - edgeZ * otherY;
			const double crossY = edgeZ * otherX - edgeX * otherZ;
			const double crossZ = edgeX * otherY - edgeY * otherX;
			const double areaSquared = crossX * crossX + crossY * crossY + crossZ * crossZ;
			if ( areaSquared > 1.0e-12 ) {
				return true;
			}
		}
		return false;
	}

	static bool R_RenderWorldCachePortalRangeIsValid( float cullNear, float cullFar ) {
		return R_RenderModelCacheFloatIsFinite( cullNear )
			&& R_RenderModelCacheFloatIsFinite( cullFar )
			&& cullNear >= 0.0f && cullNear <= RENDER_WORLD_CACHE_MAX_COORDINATE
			&& cullFar >= cullNear && cullFar <= RENDER_WORLD_CACHE_MAX_COORDINATE;
	}

	static bool R_RenderWorldCacheValidateNodeGraph( const areaNode_t *nodes, int numNodes, int numAreas ) {
		if ( nodes == NULL || numNodes <= 0 || numNodes > RENDER_WORLD_CACHE_MAX_NODES
			|| numAreas <= 0 || numAreas > RENDER_WORLD_CACHE_MAX_AREAS ) {
			return false;
		}

		for ( int i = 0; i < numNodes; ++i ) {
			if ( !R_RenderWorldCachePlaneIsValid( nodes[i].plane ) ) {
				return false;
			}
			for ( int side = 0; side < 2; ++side ) {
				const int child = nodes[i].children[side];
				if ( child > 0 ) {
					if ( child >= numNodes ) {
						return false;
					}
				} else if ( child < -numAreas ) {
					return false;
				}
			}
		}

		idList<byte> colors;
		idList<int> nodeStack;
		idList<byte> edgeStack;
		colors.SetNum( numNodes );
		memset( colors.Ptr(), 0, numNodes * sizeof( colors[0] ) );
		nodeStack.Append( 0 );
		edgeStack.Append( 0 );

		while ( nodeStack.Num() > 0 ) {
			const int stackIndex = nodeStack.Num() - 1;
			const int nodeIndex = nodeStack[stackIndex];
			if ( colors[nodeIndex] == 0 ) {
				colors[nodeIndex] = 1;
			}

			if ( edgeStack[stackIndex] >= 2 ) {
				colors[nodeIndex] = 2;
				nodeStack.SetNum( stackIndex );
				edgeStack.SetNum( stackIndex );
				continue;
			}

			const int side = edgeStack[stackIndex]++;
			const int child = nodes[nodeIndex].children[side];
			if ( child <= 0 ) {
				continue;
			}
			if ( colors[child] == 1 ) {
				return false;
			}
			if ( colors[child] == 0 ) {
				if ( nodeStack.Num() >= RENDER_WORLD_CACHE_MAX_NODE_DEPTH ) {
					return false;
				}
				nodeStack.Append( child );
				edgeStack.Append( 0 );
			}
		}

		// Hidden, unreachable nodes are never part of the finalized BSP and are
		// rejected instead of being retained as attacker-controlled baggage.
		for ( int i = 0; i < numNodes; ++i ) {
			if ( colors[i] != 2 ) {
				return false;
			}
		}
		return true;
	}

	static bool R_RenderWorldCacheValidateModels( const idList<idRenderModel *> &models, int numAreas ) {
		if ( models.Num() < numAreas || models.Num() > RENDER_WORLD_CACHE_MAX_MODELS ) {
			return false;
		}

		idHashIndex nameHash( 1024, models.Num() > 1 ? models.Num() : 1 );
		for ( int i = 0; i < models.Num(); ++i ) {
			const idRenderModel *model = models[i];
			const idRenderModelStatic *staticModel = dynamic_cast<const idRenderModelStatic *>( model );
			if ( staticModel == NULL || staticModel->LevelLoadCachePayloadType() != RENDER_MODEL_CACHE_STATIC
				|| dynamic_cast<const rvRenderModelMD5R *>( model ) != NULL
				|| dynamic_cast<const idRenderModelMD5 *>( model ) != NULL
				|| model->IsDefaultModel()
				|| !R_RenderWorldCacheStringIsSafe( model->Name(), RENDER_WORLD_CACHE_MAX_MAP_NAME, false ) ) {
				return false;
			}

			const int key = nameHash.GenerateKey( model->Name(), false );
			for ( int previous = nameHash.First( key ); previous != -1; previous = nameHash.Next( previous ) ) {
				if ( idStr::Icmp( models[previous]->Name(), model->Name() ) == 0 ) {
					return false;
				}
			}
			nameHash.Add( key, i );
		}

		for ( int area = 0; area < numAreas; ++area ) {
			const idStr areaModelName = va( "_area%i", area );
			const int key = nameHash.GenerateKey( areaModelName.c_str(), false );
			int foundIndex = -1;
			for ( int index = nameHash.First( key ); index != -1; index = nameHash.Next( index ) ) {
				if ( idStr::Icmp( models[index]->Name(), areaModelName.c_str() ) == 0 ) {
					foundIndex = index;
					break;
				}
			}
			if ( foundIndex < 0 || !models[foundIndex]->IsStaticWorldModel() ) {
				return false;
			}
		}
		return true;
	}

	static bool R_RenderWorldCacheValidateLivePortals( const idRenderWorldLocal &world ) {
		if ( world.numPortalAreas <= 0 || world.numPortalAreas > RENDER_WORLD_CACHE_MAX_AREAS
			|| world.portalAreas == NULL || world.areaScreenRect == NULL
			|| world.numInterAreaPortals < 0 || world.numInterAreaPortals > RENDER_WORLD_CACHE_MAX_PORTALS
			|| ( world.numInterAreaPortals > 0 && world.doublePortals == NULL ) ) {
			return false;
		}

		int totalWindingPoints = 0;
		for ( int i = 0; i < world.numInterAreaPortals; ++i ) {
			const doublePortal_t &doublePortal = world.doublePortals[i];
			const portal_t *p0 = doublePortal.portals[0];
			const portal_t *p1 = doublePortal.portals[1];
			if ( p0 == NULL || p1 == NULL || p0 == p1 || p0->doublePortal != &doublePortal
				|| p1->doublePortal != &doublePortal || p0->w == NULL || p1->w == NULL
				|| p0->intoArea < 0 || p0->intoArea >= world.numPortalAreas
				|| p1->intoArea < 0 || p1->intoArea >= world.numPortalAreas
				|| p0->intoArea == p1->intoArea || p0->image != p1->image
				|| p0->cullNear != p1->cullNear || p0->cullFar != p1->cullFar
				|| !R_RenderWorldCachePortalRangeIsValid( p0->cullNear, p0->cullFar ) ) {
				return false;
			}

			const int numPoints = p0->w->GetNumPoints();
			if ( numPoints < 3 || numPoints > RENDER_WORLD_CACHE_MAX_WINDING_POINTS
				|| p1->w->GetNumPoints() != numPoints
				|| numPoints > RENDER_WORLD_CACHE_MAX_TOTAL_WINDING_POINTS - totalWindingPoints ) {
				return false;
			}
			idList<idVec3> points;
			points.SetNum( numPoints );
			for ( int point = 0; point < numPoints; ++point ) {
				points[point] = ( *p0->w )[point].ToVec3();
				const idVec3 reversePoint = ( *p1->w )[numPoints - point - 1].ToVec3();
				if ( points[point].x != reversePoint.x || points[point].y != reversePoint.y || points[point].z != reversePoint.z ) {
					return false;
				}
			}
			if ( !R_RenderWorldCacheWindingIsValid( points.Ptr(), numPoints ) ) {
				return false;
			}
			if ( p0->image != NULL
				&& !R_RenderWorldCacheStringIsSafe( p0->image->GetName(), RENDER_WORLD_CACHE_MAX_IMAGE_NAME, false ) ) {
				return false;
			}
			totalWindingPoints += numPoints;
		}

		idList<byte> seenPortalSides;
		seenPortalSides.SetNum( world.numInterAreaPortals * 2 );
		if ( seenPortalSides.Num() > 0 ) {
			memset( seenPortalSides.Ptr(), 0, seenPortalSides.Num() * sizeof( seenPortalSides[0] ) );
		}
		int traversedPortalSides = 0;
		const uintptr_t doublePortalBase = reinterpret_cast<uintptr_t>( world.doublePortals );
		const size_t doublePortalBytes = static_cast<size_t>( world.numInterAreaPortals ) * sizeof( world.doublePortals[0] );
		for ( int area = 0; area < world.numPortalAreas; ++area ) {
			for ( const portal_t *portal = world.portalAreas[area].portals; portal != NULL; portal = portal->next ) {
				if ( traversedPortalSides >= world.numInterAreaPortals * 2 || portal->doublePortal == NULL ) {
					return false;
				}
				const uintptr_t portalAddress = reinterpret_cast<uintptr_t>( portal->doublePortal );
				if ( portalAddress < doublePortalBase ) {
					return false;
				}
				const uintptr_t byteOffset = portalAddress - doublePortalBase;
				if ( byteOffset >= doublePortalBytes || ( byteOffset % sizeof( doublePortal_t ) ) != 0 ) {
					return false;
				}
				const int portalIndex = static_cast<int>( byteOffset / sizeof( doublePortal_t ) );
				const doublePortal_t &doublePortal = world.doublePortals[portalIndex];
				int side = -1;
				if ( doublePortal.portals[0] == portal ) {
					side = 0;
				} else if ( doublePortal.portals[1] == portal ) {
					side = 1;
				}
				const int oppositeSide = 1 - side;
				if ( side < 0 || doublePortal.portals[oppositeSide] == NULL
					|| doublePortal.portals[oppositeSide]->intoArea != area
					|| seenPortalSides[portalIndex * 2 + side] != 0 ) {
					return false;
				}
				seenPortalSides[portalIndex * 2 + side] = 1;
				++traversedPortalSides;
			}
		}
		return traversedPortalSides == world.numInterAreaPortals * 2;
	}
}

/*
================
idRenderWorldLocal::WriteLevelLoadCachePayload
================
*/
bool idRenderWorldLocal::WriteLevelLoadCachePayload( idFile &file ) const {
	// Packed MD5R proc worlds contain cross-model shared buffers.  The ordinary
	// model codec deliberately owns its decoded buffers, so caching such a world
	// here would silently change that representation.  Fail before writing and
	// let InitFromMap retain the MD5RProc / classic-proc source path.
	if ( md5rProcData != NULL && md5rProcData->HasPackedWorldData() ) {
		common->DPrintf( "Render-world cache skipped for '%s': packed MD5R world\n", mapName.c_str() );
		return false;
	}
	if ( mapName.Length() <= 0 || mapName == "<FREED>"
		|| mapTimeStamp == FILE_NOT_FOUND_TIMESTAMP
		|| !R_RenderWorldCacheStringIsSafe( mapName.c_str(), RENDER_WORLD_CACHE_MAX_MAP_NAME, false ) ) {
		common->DPrintf( "Render-world cache skipped: invalid map identity\n" );
		return false;
	}
	if ( !R_RenderWorldCacheValidateModels( localModels, numPortalAreas ) ) {
		common->DPrintf( "Render-world cache skipped for '%s': model validation failed\n", mapName.c_str() );
		return false;
	}
	if ( !R_RenderWorldCacheValidateLivePortals( *this ) ) {
		common->DPrintf( "Render-world cache skipped for '%s': portal validation failed\n", mapName.c_str() );
		return false;
	}
	if ( !R_RenderWorldCacheValidateNodeGraph( areaNodes, numAreaNodes, numPortalAreas ) ) {
		common->DPrintf( "Render-world cache skipped for '%s': BSP validation failed\n", mapName.c_str() );
		return false;
	}

	idRenderWorldCacheMemoryFileOwner payloadOwner;
	idFile *payload = payloadOwner.Get();
	if ( payload == NULL ) {
		return false;
	}
	idRenderModelCacheWriter writer( *payload );
	if ( !writer.WriteUnsignedInt( RENDER_WORLD_CACHE_MAGIC )
		|| !writer.WriteInt( RENDER_WORLD_CACHE_VERSION )
		|| !writer.WriteUnsignedInt( RENDER_WORLD_CACHE_FLAGS )
		|| !writer.WriteString( mapName.c_str(), RENDER_WORLD_CACHE_MAX_MAP_NAME )
		|| !writer.WriteUnsigned64( static_cast<uint64_t>( static_cast<int64_t>( mapTimeStamp ) ) )
		|| !writer.WriteUnsignedInt( mapFileCRC )
		|| !writer.WriteInt( localModels.Num() ) ) {
		return false;
	}

	for ( int i = 0; i < localModels.Num(); ++i ) {
		const idRenderModel *model = localModels[i];
		const idRenderModelStatic *staticModel = dynamic_cast<const idRenderModelStatic *>( model );
		if ( staticModel == NULL || staticModel->LevelLoadCachePayloadType() != RENDER_MODEL_CACHE_STATIC ) {
			return false;
		}
		const bool shadowModel = R_RenderWorld_IsShadowModel( *model );
		idRenderWorldCacheMemoryFileOwner modelPayloadOwner;
		idFile *modelPayload = modelPayloadOwner.Get();
		if ( modelPayload == NULL
			|| ( shadowModel
				? !R_RenderWorldCacheWriteShadowModel( *modelPayload, *model )
				: !staticModel->WriteLevelLoadCachePayload( *modelPayload ) ) ) {
			common->DPrintf( "Render-world cache skipped for '%s': model payload %d ('%s') failed\n",
				mapName.c_str(), i, model->Name() );
			return false;
		}
		const int modelPayloadLength = modelPayload->Length();
		const char *modelPayloadData = modelPayload->GetDataPtr();
		if ( modelPayloadLength <= 0 || modelPayloadLength > RENDER_WORLD_CACHE_MAX_MODEL_PAYLOAD
			|| modelPayloadData == NULL
			|| !writer.WriteInt( shadowModel ? RENDER_WORLD_CACHE_MODEL_SHADOW : RENDER_WORLD_CACHE_MODEL_STATIC )
			|| !writer.WriteInt( modelPayloadLength )
			|| !writer.WriteBytes( modelPayloadData, modelPayloadLength ) ) {
			return false;
		}
	}

	if ( !writer.WriteInt( numPortalAreas ) || !writer.WriteInt( numInterAreaPortals ) ) {
		return false;
	}
	for ( int i = 0; i < numInterAreaPortals; ++i ) {
		const portal_t *p0 = doublePortals[i].portals[0];
		const portal_t *p1 = doublePortals[i].portals[1];
		const int numPoints = p0->w->GetNumPoints();
		if ( !writer.WriteInt( p1->intoArea ) || !writer.WriteInt( p0->intoArea )
			|| !writer.WriteInt( numPoints ) ) {
			return false;
		}
		for ( int point = 0; point < numPoints; ++point ) {
			if ( !writer.WriteVec3( ( *p0->w )[point].ToVec3() ) ) {
				return false;
			}
		}
		const bool hasImage = p0->image != NULL;
		if ( !writer.WriteBool( hasImage )
			|| ( hasImage && !writer.WriteString( p0->image->GetName(), RENDER_WORLD_CACHE_MAX_IMAGE_NAME ) )
			|| !writer.WriteFloat( p0->cullNear ) || !writer.WriteFloat( p0->cullFar ) ) {
			return false;
		}
	}

	if ( !writer.WriteInt( numAreaNodes ) ) {
		return false;
	}
	for ( int i = 0; i < numAreaNodes; ++i ) {
		if ( !writer.WriteVec4( areaNodes[i].plane.ToVec4() )
			|| !writer.WriteInt( areaNodes[i].children[0] )
			|| !writer.WriteInt( areaNodes[i].children[1] ) ) {
			return false;
		}
	}

	const int payloadLength = payload->Length();
	const char *payloadData = payload->GetDataPtr();
	if ( !writer.IsValid() || payloadLength <= 0 || payloadLength > RENDER_WORLD_CACHE_MAX_PAYLOAD_BYTES ) {
		return false;
	}
	return payloadData != NULL && file.Write( payloadData, payloadLength ) == payloadLength;
}

/*
================
idRenderWorldLocal::ReadLevelLoadCachePayload
================
*/
bool idRenderWorldLocal::ReadLevelLoadCachePayload( idFile &file, const char *expectedMapName, ID_TIME_T sourceTimestamp ) {
	const int expectedMapNameLength = R_RenderWorldCacheBoundedStringLength( expectedMapName, RENDER_WORLD_CACHE_MAX_MAP_NAME );
	const int payloadStart = file.Tell();
	const int payloadEnd = file.Length();
	if ( expectedMapNameLength <= 0
		|| !R_RenderWorldCacheStringIsSafe( expectedMapName, RENDER_WORLD_CACHE_MAX_MAP_NAME, false )
		|| sourceTimestamp == FILE_NOT_FOUND_TIMESTAMP
		|| payloadStart < 0 || payloadEnd < payloadStart
		|| payloadEnd - payloadStart <= 0 || payloadEnd - payloadStart > RENDER_WORLD_CACHE_MAX_PAYLOAD_BYTES ) {
		return false;
	}

	idRenderModelCacheReader reader( file );
	unsigned int magic = 0;
	int version = 0;
	unsigned int flags = 0;
	idStr stagedMapName;
	uint64_t encodedTimestamp = 0;
	unsigned int stagedMapCRC = 0;
	int numModels = 0;
	if ( !reader.ReadUnsignedInt( magic ) || magic != RENDER_WORLD_CACHE_MAGIC
		|| !reader.ReadInt( version ) || version != RENDER_WORLD_CACHE_VERSION
		|| !reader.ReadUnsignedInt( flags ) || flags != RENDER_WORLD_CACHE_FLAGS
		|| !reader.ReadString( stagedMapName, RENDER_WORLD_CACHE_MAX_MAP_NAME )
		|| !R_RenderWorldCacheStringIsSafe( stagedMapName.c_str(), RENDER_WORLD_CACHE_MAX_MAP_NAME, false )
		|| stagedMapName == "<FREED>" || idStr::Icmp( stagedMapName.c_str(), expectedMapName ) != 0
		|| !reader.ReadUnsigned64( encodedTimestamp )
		|| encodedTimestamp != static_cast<uint64_t>( static_cast<int64_t>( sourceTimestamp ) )
		|| !reader.ReadUnsignedInt( stagedMapCRC )
		|| !reader.ReadCount( numModels, RENDER_WORLD_CACHE_MAX_MODELS, sizeof( idRenderModel * ) ) ) {
		return false;
	}

	idRenderWorldCacheModelOwner modelOwner;
	uint64_t aggregateModelMemory = 0;
	for ( int i = 0; i < numModels; ++i ) {
		int encodedModelType = 0;
		int modelPayloadLength = 0;
		if ( !reader.ReadInt( encodedModelType )
			|| ( encodedModelType != RENDER_WORLD_CACHE_MODEL_STATIC
				&& encodedModelType != RENDER_WORLD_CACHE_MODEL_SHADOW )
			|| !reader.ReadCount( modelPayloadLength, RENDER_WORLD_CACHE_MAX_MODEL_PAYLOAD, sizeof( byte ) )
			|| modelPayloadLength <= 0 ) {
			return false;
		}

		idRenderWorldCacheMemoryFileOwner modelPayloadOwner;
		idFile *modelPayload = modelPayloadOwner.Get();
		if ( modelPayload == NULL ) {
			return false;
		}
		byte copyBuffer[16 * 1024];
		int remainingPayloadBytes = modelPayloadLength;
		while ( remainingPayloadBytes > 0 ) {
			const int copyBytes = remainingPayloadBytes < static_cast<int>( sizeof( copyBuffer ) )
				? remainingPayloadBytes
				: static_cast<int>( sizeof( copyBuffer ) );
			if ( !reader.ReadBytes( copyBuffer, copyBytes )
				|| modelPayload->Write( copyBuffer, copyBytes ) != copyBytes ) {
				return false;
			}
			remainingPayloadBytes -= copyBytes;
		}
		modelPayload->MakeReadOnly();
		idRenderModelStatic *model = encodedModelType == RENDER_WORLD_CACHE_MODEL_SHADOW
			? R_RenderWorldCacheReadShadowModel( *modelPayload )
			: new idRenderModelStatic;
		if ( model == NULL ) {
			return false;
		}
		if ( ( encodedModelType == RENDER_WORLD_CACHE_MODEL_STATIC
				&& !model->ReadLevelLoadCachePayload( *modelPayload ) )
			|| modelPayload->Tell() != modelPayload->Length()
			|| model->LevelLoadCachePayloadType() != RENDER_MODEL_CACHE_STATIC ) {
			delete model;
			return false;
		}

		const int modelMemory = model->Memory();
		if ( modelMemory <= 0 || static_cast<uint64_t>( modelMemory ) > RENDER_WORLD_CACHE_MAX_MODEL_MEMORY - aggregateModelMemory ) {
			delete model;
			return false;
		}
		aggregateModelMemory += static_cast<uint64_t>( modelMemory );
		modelOwner.models.Append( model );
	}

	int stagedNumAreas = 0;
	int stagedNumPortals = 0;
	if ( !reader.ReadCount( stagedNumAreas, RENDER_WORLD_CACHE_MAX_AREAS, sizeof( portalArea_t ) )
		|| stagedNumAreas <= 0
		|| !reader.ReadCount( stagedNumPortals, RENDER_WORLD_CACHE_MAX_PORTALS, sizeof( renderWorldCachePortalStage_t ) ) ) {
		return false;
	}

	idList<renderWorldCachePortalStage_t> stagedPortals;
	stagedPortals.SetNum( stagedNumPortals );
	int totalWindingPoints = 0;
	for ( int i = 0; i < stagedNumPortals; ++i ) {
		renderWorldCachePortalStage_t &portal = stagedPortals[i];
		int numPoints = 0;
		bool hasImage = false;
		if ( !reader.ReadInt( portal.areas[0] ) || !reader.ReadInt( portal.areas[1] )
			|| portal.areas[0] < 0 || portal.areas[0] >= stagedNumAreas
			|| portal.areas[1] < 0 || portal.areas[1] >= stagedNumAreas
			|| portal.areas[0] == portal.areas[1]
			|| !reader.ReadCount( numPoints, RENDER_WORLD_CACHE_MAX_WINDING_POINTS, sizeof( idVec3 ) )
			|| numPoints < 3 || numPoints > RENDER_WORLD_CACHE_MAX_TOTAL_WINDING_POINTS - totalWindingPoints ) {
			return false;
		}
		portal.points.SetNum( numPoints );
		for ( int point = 0; point < numPoints; ++point ) {
			if ( !reader.ReadVec3( portal.points[point] ) || !R_RenderWorldCacheVec3IsValid( portal.points[point] ) ) {
				return false;
			}
		}
		if ( !R_RenderWorldCacheWindingIsValid( portal.points.Ptr(), numPoints )
			|| !reader.ReadBool( hasImage )
			|| ( hasImage && ( !reader.ReadString( portal.imageName, RENDER_WORLD_CACHE_MAX_IMAGE_NAME )
				|| !R_RenderWorldCacheStringIsSafe( portal.imageName.c_str(), RENDER_WORLD_CACHE_MAX_IMAGE_NAME, false ) ) )
			|| !reader.ReadFloat( portal.cullNear ) || !reader.ReadFloat( portal.cullFar )
			|| !R_RenderWorldCachePortalRangeIsValid( portal.cullNear, portal.cullFar ) ) {
			return false;
		}
		if ( !hasImage ) {
			portal.imageName.Clear();
		}
		totalWindingPoints += numPoints;
	}

	int stagedNumNodes = 0;
	if ( !reader.ReadCount( stagedNumNodes, RENDER_WORLD_CACHE_MAX_NODES, sizeof( areaNode_t ) )
		|| stagedNumNodes <= 0 ) {
		return false;
	}
	idList<areaNode_t> stagedNodes;
	stagedNodes.SetNum( stagedNumNodes );
	for ( int i = 0; i < stagedNumNodes; ++i ) {
		idVec4 plane;
		if ( !reader.ReadVec4( plane ) || !reader.ReadInt( stagedNodes[i].children[0] )
			|| !reader.ReadInt( stagedNodes[i].children[1] ) ) {
			return false;
		}
		stagedNodes[i].plane.ToVec4() = plane;
		stagedNodes[i].commonChildrenArea = CHILDREN_HAVE_MULTIPLE_AREAS;
	}

	if ( !reader.IsValid() || file.Tell() != payloadEnd
		|| !R_RenderWorldCacheValidateModels( modelOwner.models, stagedNumAreas )
		|| !R_RenderWorldCacheValidateNodeGraph( stagedNodes.Ptr(), stagedNumNodes, stagedNumAreas ) ) {
		return false;
	}

	// Publication begins only after every byte and every cross-reference has
	// passed validation.  No failure return exists below this boundary.
	FreeWorld();
	mapName = stagedMapName;
	mapTimeStamp = sourceTimestamp;
	mapFileCRC = stagedMapCRC;

	numPortalAreas = stagedNumAreas;
	portalAreas = static_cast<portalArea_t *>( R_ClearedStaticAlloc( numPortalAreas * sizeof( portalAreas[0] ) ) );
	areaScreenRect = static_cast<idScreenRect *>( R_ClearedStaticAlloc( numPortalAreas * sizeof( areaScreenRect[0] ) ) );
	SetupAreaRefs();

	numInterAreaPortals = stagedNumPortals;
	if ( numInterAreaPortals > 0 ) {
		doublePortals = static_cast<doublePortal_t *>( R_ClearedStaticAlloc( numInterAreaPortals * sizeof( doublePortals[0] ) ) );
	}
	for ( int i = 0; i < numInterAreaPortals; ++i ) {
		const renderWorldCachePortalStage_t &sourcePortal = stagedPortals[i];
		idWinding *winding = new idWinding( sourcePortal.points.Num() );
		winding->SetNumPoints( sourcePortal.points.Num() );
		for ( int point = 0; point < sourcePortal.points.Num(); ++point ) {
			( *winding )[point].ToVec3() = sourcePortal.points[point];
			( *winding )[point][3] = 0.0f;
			( *winding )[point][4] = 0.0f;
		}
		idImage *portalImage = NULL;
		if ( sourcePortal.imageName.Length() > 0 ) {
			portalImage = globalImages->ImageFromFile(
				sourcePortal.imageName.c_str(), TF_DEFAULT, TR_REPEAT, TD_DEFAULT );
		}

		portal_t *p0 = static_cast<portal_t *>( R_ClearedStaticAlloc( sizeof( *p0 ) ) );
		p0->intoArea = sourcePortal.areas[1];
		p0->doublePortal = &doublePortals[i];
		p0->w = winding;
		p0->w->GetPlane( p0->plane );
		p0->image = portalImage;
		p0->cullNear = sourcePortal.cullNear;
		p0->cullFar = sourcePortal.cullFar;
		p0->next = portalAreas[sourcePortal.areas[0]].portals;
		portalAreas[sourcePortal.areas[0]].portals = p0;
		doublePortals[i].portals[0] = p0;

		portal_t *p1 = static_cast<portal_t *>( R_ClearedStaticAlloc( sizeof( *p1 ) ) );
		p1->intoArea = sourcePortal.areas[0];
		p1->doublePortal = &doublePortals[i];
		p1->w = winding->Reverse();
		p1->w->GetPlane( p1->plane );
		p1->image = portalImage;
		p1->cullNear = sourcePortal.cullNear;
		p1->cullFar = sourcePortal.cullFar;
		p1->next = portalAreas[sourcePortal.areas[1]].portals;
		portalAreas[sourcePortal.areas[1]].portals = p1;
		doublePortals[i].portals[1] = p1;
	}

	numAreaNodes = stagedNumNodes;
	areaNodes = static_cast<areaNode_t *>( R_ClearedStaticAlloc( numAreaNodes * sizeof( areaNodes[0] ) ) );
	for ( int i = 0; i < numAreaNodes; ++i ) {
		areaNodes[i] = stagedNodes[i];
	}

	for ( int i = 0; i < modelOwner.models.Num(); ++i ) {
		renderModelManager->AddModel( modelOwner.models[i] );
		localModels.Append( modelOwner.models[i] );
	}
	modelOwner.Release();

	R_RenderWorld_FinalizeLoadedWorld( *this );
	return true;
}

/*
================
R_RenderWorld_ParseSupportedMD5RProc

Retail Quake 4 MD5RProc companions serialize shared packed buffers first and
then attach per-area Model blocks that reference those shared arrays. openQ4
now mirrors that layout directly while still accepting the older classic
model / shadowModel fallback sections for compatibility.
================
*/
static bool R_RenderWorld_ParseSupportedMD5RProc( idRenderWorldLocal &world, Lexer &src, const char *fileName ) {
	idToken token;

	if ( !src.ReadToken( &token ) || token.Icmp( MD5R_PROC_FILE_ID ) != 0 ) {
		common->Warning(
			"idRenderWorldLocal::InitFromMap: bad MD5RProc id '%s' in '%s' instead of '%s'",
			token.c_str(),
			fileName,
			MD5R_PROC_FILE_ID );
		return false;
	}

	if ( src.ParseInt() != MD5R_PROC_FILEVERSION ) {
		common->Warning(
			"idRenderWorldLocal::InitFromMap: unsupported MD5RProc version in '%s' (expected %d)",
			fileName,
			MD5R_PROC_FILEVERSION );
		return false;
	}

	if ( !src.ReadToken( &token ) ) {
		common->Warning( "idRenderWorldLocal::InitFromMap: '%s' has no MD5RProc CRC token", fileName );
		return false;
	}
	world.mapFileCRC = token.GetUnsignedLongValue();

	idRenderWorldMD5RProcData &md5rProcData = R_RenderWorld_EnsureMD5RProcData( world );
	md5rProcData.Clear();

	while ( src.ReadToken( &token ) ) {
		if ( token == "VertexBuffer" ) {
			rvRenderModelMD5R::ParseSharedVertexBuffers( src, md5rProcData.vertexBuffers );
			continue;
		}

		if ( token == "IndexBuffer" ) {
			rvRenderModelMD5R::ParseSharedIndexBuffers( src, md5rProcData.indexBuffers );
			continue;
		}

		if ( token == "SilhouetteEdge" ) {
			rvRenderModelMD5R::ParseSharedSilhouetteEdges( src, md5rProcData.silEdges );
			continue;
		}

		if ( token == "Model" ) {
			src.ExpectTokenString( "[" );
			const int numModels = src.ParseInt();
			src.ExpectTokenString( "]" );
			src.ExpectTokenString( "{" );

			if ( numModels < 0 ) {
				common->Warning(
					"idRenderWorldLocal::InitFromMap: invalid packed MD5RProc model count %d in '%s'",
					numModels,
					fileName );
				return false;
			}

			for ( int modelIndex = 0; modelIndex < numModels; ++modelIndex ) {
				idToken modelName;
				src.ExpectTokenString( "Model" );
				src.ExpectAnyToken( &modelName );

				rvRenderModelMD5R *model = new rvRenderModelMD5R;
				model->InitEmpty( modelName );

				src.ExpectTokenString( "{" );
				model->InitFromProcWorldModel( src, md5rProcData.vertexBuffers, md5rProcData.indexBuffers, md5rProcData.silEdges );
				src.ExpectTokenString( "}" );

				renderModelManager->AddModel( model );
				world.localModels.Append( model );
				md5rProcData.models.Append( model );
			}

			src.ExpectTokenString( "}" );
			continue;
		}

		if ( token == "model" ) {
			idRenderModel *model = world.ParseModel( &src );
			renderModelManager->AddModel( model );
			world.localModels.Append( model );
			continue;
		}

		if ( token == "shadowModel" ) {
			idRenderModel *model = world.ParseShadowModel( &src );
			renderModelManager->AddModel( model );
			world.localModels.Append( model );
			continue;
		}

		if ( token == "interAreaPortals" ) {
			world.ParseInterAreaPortals( &src );
			continue;
		}

		if ( token == "nodes" ) {
			world.ParseNodes( &src );
			continue;
		}

		common->Warning(
			"idRenderWorldLocal::InitFromMap: unsupported token '%s' in MD5RProc companion '%s'; falling back to the classic .proc world if available",
			token.c_str(),
			fileName );
		return false;
	}

	return true;
}


/*
================
idRenderWorldLocal::FreeWorld
================
*/
void idRenderWorldLocal::FreeWorld() {
	R_RenderThread_Sync( "world" );
	int i;

	// drop every memoized draw-surf area before the defs and portal areas the
	// entries point at are freed (also keeps FreeEntityDef's memo scan trivial
	// during mass teardown)
	R_ClearDrawSurfAreaMemo();

	// this will free all the lightDefs and entityDefs
	FreeDefs();

	ReleaseLightGridPack();

	// free all the portals and check light/model references
	for ( i = 0 ; i < numPortalAreas ; i++ ) {
		portalArea_t	*area;
		portal_t		*portal, *nextPortal;

		area = &portalAreas[i];
		area->lightGrid.Clear();
		for ( portal = area->portals ; portal ; portal = nextPortal ) {
			nextPortal = portal->next;
			delete portal->w;
			R_StaticFree( portal );
		}

		// there shouldn't be any remaining lightRefs or entityRefs
		if ( area->lightRefs.areaNext != &area->lightRefs ) {
			common->Error( "FreeWorld: unexpected remaining lightRefs" );
		}
		if ( area->entityRefs.areaNext != &area->entityRefs ) {
			common->Error( "FreeWorld: unexpected remaining entityRefs" );
		}
	}

	if ( portalAreas ) {
		R_StaticFree( portalAreas );
		portalAreas = NULL;
		numPortalAreas = 0;
		R_StaticFree( areaScreenRect );
		areaScreenRect = NULL;
	}

	if ( doublePortals ) {
		R_StaticFree( doublePortals );
		doublePortals = NULL;
		numInterAreaPortals = 0;
	}

	if ( areaNodes ) {
		R_StaticFree( areaNodes );
		areaNodes = NULL;
	}

	// free all the inline idRenderModels 
	for ( i = 0 ; i < localModels.Num() ; i++ ) {
		renderModelManager->RemoveModel( localModels[i] );
		delete localModels[i];
	}
	localModels.Clear();
	R_RenderWorld_ClearMD5RProcData( *this );

	areaReferenceAllocator.Shutdown();
	interactionAllocator.Shutdown();
	areaNumRefAllocator.Shutdown();

	mapName = "<FREED>";
	mapFileCRC = 0u;
	worldFromGeneratedCache = false;
}

/*
================
idRenderWorldLocal::TouchWorldModels
================
*/
void idRenderWorldLocal::TouchWorldModels( void ) {
	int i;

	for ( i = 0 ; i < localModels.Num() ; i++ ) {
		renderModelManager->CheckModel( localModels[i]->Name() );
	}
}

/*
================
idRenderWorldLocal::ParseModel
================
*/
idRenderModel *idRenderWorldLocal::ParseModel( Lexer *src ) {
	idRenderModel	*model;
	idToken			token;
	int				i, j;
	srfTriangles_t	*tri;
	modelSurface_t	surf = {};

	src->ExpectTokenString( "{" );

	// parse the name
	src->ExpectAnyToken( &token );

	model = renderModelManager->AllocModel();
	model->InitEmpty( token );

	int numSurfaces = src->ParseInt();
	if ( numSurfaces < 0 ) {
		src->Error( "R_ParseModel: bad numSurfaces" );
	}

// jmarshall - quake 4 proc format
	if ( !src->PeekTokenString( "{" ) && !src->PeekTokenString( "}" ) ) {
		static_cast<idRenderModelStatic *>( model )->SetProcSky( src->ParseInt() != 0 );
	}
// jmarshall end

	for ( i = 0 ; i < numSurfaces ; i++ ) {
		src->ExpectTokenString( "{" );

		src->ExpectAnyToken( &token );

		// a surface's id is its index, as in the ASE/LWO/MA conversions:
		// GetSurfaceMask sets suppressSurfaceMask bits by surface index and
		// the ambient pass tests them against the id; the id is also cached
		surf.id = model->NumSurfaces();
		surf.shader = declManager->FindMaterial( token );

		((idMaterial*)surf.shader)->AddReference();

		tri = R_AllocStaticTriSurf();
		surf.geometry = tri;

		tri->numVerts = src->ParseInt();
		tri->numIndexes = src->ParseInt();

		// FinishSurfaces below dereferences every index while deriving tangents
		// and silhouette edges, so the file-provided counts have to be sane
		// before anything is allocated.  ParseShadowModel and the binary
		// render-world cache already apply the same predicates.
		if ( tri->numVerts < 0 || tri->numVerts > RENDER_WORLD_CACHE_MAX_SURFACE_VERTS
				|| tri->numIndexes < 0 || tri->numIndexes > RENDER_WORLD_CACHE_MAX_SURFACE_INDEXES
				|| ( tri->numIndexes % 3 ) != 0 ) {
			src->Error( "R_ParseModel: bad surface counts" );
			return NULL;
		}

		R_AllocStaticTriSurfVerts( tri, tri->numVerts );
		for ( j = 0 ; j < tri->numVerts ; j++ ) {
// jmarshall - quake 4 proc format
			float vec[12];
			const int numFloats = src->Parse1DMatrixOpenEnded( 12, vec );
			if ( numFloats != 8 && numFloats != 12 ) {
				src->Error( "R_ParseModel: bad vertex read" );
			}

			tri->verts[j].xyz[0] = vec[0];
			tri->verts[j].xyz[1] = vec[1];
			tri->verts[j].xyz[2] = vec[2];
			tri->verts[j].st[0] = vec[3];
			tri->verts[j].st[1] = vec[4];
			tri->verts[j].normal[0] = vec[5];
			tri->verts[j].normal[1] = vec[6];
			tri->verts[j].normal[2] = vec[7];

			if ( numFloats == 12 ) {
				tri->verts[j].color[0] = idMath::Ftob( vec[8] );
				tri->verts[j].color[1] = idMath::Ftob( vec[9] );
				tri->verts[j].color[2] = idMath::Ftob( vec[10] );
				tri->verts[j].color[3] = idMath::Ftob( vec[11] );
			} else {
				tri->verts[j].color[0] = 0;
				tri->verts[j].color[1] = 0;
				tri->verts[j].color[2] = 0;
				tri->verts[j].color[3] = 255;
			}

			tri->verts[j].color2[0] = tri->verts[j].color[0];
			tri->verts[j].color2[1] = tri->verts[j].color[1];
			tri->verts[j].color2[2] = tri->verts[j].color[2];
			tri->verts[j].color2[3] = tri->verts[j].color[3];
// jmarshall end
		}

		R_AllocStaticTriSurfIndexes( tri, tri->numIndexes );
		for ( j = 0 ; j < tri->numIndexes ; j++ ) {
			const int index = src->ParseInt();
			if ( index < 0 || index >= tri->numVerts ) {
				src->Error( "R_ParseModel: index %i out of range (%i verts)", index, tri->numVerts );
				return NULL;
			}
			tri->indexes[j] = index;
		}
		src->ExpectTokenString( "}" );

		// add the completed surface to the model
		model->AddSurface( surf );
	}

	src->ExpectTokenString( "}" );

	model->FinishSurfaces();

	return model;
}

/*
================
idRenderWorldLocal::ParseShadowModel
================
*/
idRenderModel *idRenderWorldLocal::ParseShadowModel( Lexer *src ) {
	idRenderModel	*model;
	idToken			token;
	int				j;
	srfTriangles_t	*tri;
	modelSurface_t	surf = {};	// id 0: the only surface, as the shadow cache reader builds it

	src->ExpectTokenString( "{" );

	// parse the name
	src->ExpectAnyToken( &token );

	model = renderModelManager->AllocModel();
	model->InitEmpty( token );

	surf.shader = tr.defaultMaterial;

	tri = R_AllocStaticTriSurf();
	surf.geometry = tri;

	tri->numVerts = src->ParseInt();
	tri->numShadowIndexesNoCaps = src->ParseInt();
	tri->numShadowIndexesNoFrontCaps = src->ParseInt();
	tri->numIndexes = src->ParseInt();
	tri->shadowCapPlaneBits = src->ParseInt();

	// FinishSurfaces is intentionally skipped for shadow models, so nothing
	// downstream range-checks these file-provided counts before the stencil draw
	// fetches indexes. Validate them here with the same predicates the binary
	// shadow-cache reader applies. Zero counts are allowed (an empty shadow model
	// parses harmlessly); only malformed data is rejected.
	if ( tri->numVerts < 0 || tri->numVerts > RENDER_WORLD_CACHE_MAX_SHADOW_VERTS
			|| tri->numIndexes < 0 || tri->numIndexes > RENDER_WORLD_CACHE_MAX_SHADOW_INDEXES
			|| ( tri->numIndexes % 3 ) != 0
			|| tri->numShadowIndexesNoCaps < 0 || tri->numShadowIndexesNoFrontCaps < 0
			|| ( tri->numShadowIndexesNoCaps % 3 ) != 0 || ( tri->numShadowIndexesNoFrontCaps % 3 ) != 0
			|| tri->numShadowIndexesNoCaps > tri->numShadowIndexesNoFrontCaps
			|| tri->numShadowIndexesNoFrontCaps > tri->numIndexes ) {
		src->Error( "R_ParseShadowModel: bad shadow model counts" );
		return NULL;
	}

	R_AllocStaticTriSurfShadowVerts( tri, tri->numVerts );
	tri->bounds.Clear();
	for ( j = 0 ; j < tri->numVerts ; j++ ) {
		float	vec[8];

		src->Parse1DMatrix( 3, vec );
		tri->shadowVertexes[j].xyz[0] = vec[0];
		tri->shadowVertexes[j].xyz[1] = vec[1];
		tri->shadowVertexes[j].xyz[2] = vec[2];
		tri->shadowVertexes[j].xyz[3] = 1;		// no homogenous value

		tri->bounds.AddPoint( tri->shadowVertexes[j].xyz.ToVec3() );
	}

	R_AllocStaticTriSurfIndexes( tri, tri->numIndexes );
	for ( j = 0 ; j < tri->numIndexes ; j++ ) {
		tri->indexes[j] = src->ParseInt();
	}

	// add the completed surface to the model
	model->AddSurface( surf );

	src->ExpectTokenString( "}" );

	// we do NOT do a model->FinishSurfaceces, because we don't need sil edges, planes, tangents, etc.
//	model->FinishSurfaces();

	return model;
}

/*
================
idRenderWorldLocal::SetupAreaRefs
================
*/
void idRenderWorldLocal::SetupAreaRefs() {
	int		i;

	connectedAreaNum = 0;
	for ( i = 0 ; i < numPortalAreas ; i++ ) {
		portalAreas[i].areaNum = i;
		portalAreas[i].globalBounds.Clear();
		portalAreas[i].lightGrid.Clear();
		portalAreas[i].lightGrid.area = i;
		portalAreas[i].lightRefs.areaNext =
		portalAreas[i].lightRefs.areaPrev =
			&portalAreas[i].lightRefs;
		portalAreas[i].entityRefs.areaNext =
		portalAreas[i].entityRefs.areaPrev =
			&portalAreas[i].entityRefs;
	}
}

/*
================
idRenderWorldLocal::ParseInterAreaPortals
================
*/
void idRenderWorldLocal::ParseInterAreaPortals( Lexer *src ) {
	int i, j;

	src->ExpectTokenString( "{" );

	numPortalAreas = src->ParseInt();
	if ( numPortalAreas < 0 ) {
		src->Error( "R_ParseInterAreaPortals: bad numPortalAreas" );
		return;
	}
	portalAreas = (portalArea_t *)R_ClearedStaticAlloc( numPortalAreas * sizeof( portalAreas[0] ) );
	areaScreenRect = (idScreenRect *) R_ClearedStaticAlloc( numPortalAreas * sizeof( idScreenRect ) );

	// set the doubly linked lists
	SetupAreaRefs();

	numInterAreaPortals = src->ParseInt();
	if ( numInterAreaPortals < 0 ) {
		src->Error(  "R_ParseInterAreaPortals: bad numInterAreaPortals" );
		return;
	}

	doublePortals = (doublePortal_t *)R_ClearedStaticAlloc( numInterAreaPortals * 
		sizeof( doublePortals [0] ) );

	for ( i = 0 ; i < numInterAreaPortals ; i++ ) {
		int		numPoints, a1, a2;
		idWinding	*w;
		portal_t	*p;
		float		cullNear = 262144.0f;
		float		cullFar = 262144.0f;
		idImage* portalImage = NULL;

		numPoints = src->ParseInt();
		a1 = src->ParseInt();
		a2 = src->ParseInt();

		// a1/a2 index portalAreas directly below; reject file-provided area
		// numbers out of range rather than corrupting adjacent memory. numPoints
		// must be at least a triangle (GetPlane reads three points) and is capped
		// to the same bound the binary cache reader enforces.
		if ( a1 < 0 || a1 >= numPortalAreas || a2 < 0 || a2 >= numPortalAreas ) {
			src->Error( "R_ParseInterAreaPortals: portal %i references area out of range (%i, %i of %i)", i, a1, a2, numPortalAreas );
			return;
		}
		if ( numPoints < 3 || numPoints > RENDER_WORLD_CACHE_MAX_WINDING_POINTS ) {
			src->Error( "R_ParseInterAreaPortals: portal %i has bad point count %i", i, numPoints );
			return;
		}

		w = new idWinding( numPoints );
		w->SetNumPoints( numPoints );
		for ( j = 0 ; j < numPoints ; j++ ) {
			src->Parse1DMatrix( 3, (*w)[j].ToFloatPtr() );
			// no texture coordinates
			(*w)[j][3] = 0;
			(*w)[j][4] = 0;
		}

		// Quake 4 optional portal fade tuple:
		// ( fadeImage distanceNear distanceFar )
		if ( src->PeekTokenString( "(" ) ) {
			idToken imageToken;
			src->ExpectTokenString( "(" );
			src->ExpectAnyToken( &imageToken );
			portalImage = globalImages->ImageFromFile( imageToken.c_str(), TF_DEFAULT, TR_REPEAT, TD_DEFAULT );
			cullNear = src->ParseFloat();
			cullFar = src->ParseFloat();
			src->ExpectTokenString( ")" );
		}

		// add the portal to a1
		p = (portal_t *)R_ClearedStaticAlloc( sizeof( *p ) );
		p->intoArea = a2;
		p->doublePortal = &doublePortals[i];
		p->w = w;
		p->w->GetPlane( p->plane );
		p->image = portalImage;
		p->cullNear = cullNear;
		p->cullFar = cullFar;

		p->next = portalAreas[a1].portals;
		portalAreas[a1].portals = p;

		doublePortals[i].portals[0] = p;

		// reverse it for a2
		p = (portal_t *)R_ClearedStaticAlloc( sizeof( *p ) );
		p->intoArea = a1;
		p->doublePortal = &doublePortals[i];
		p->w = w->Reverse();
		p->w->GetPlane( p->plane );
		p->image = portalImage;
		p->cullNear = cullNear;
		p->cullFar = cullFar;

		p->next = portalAreas[a2].portals;
		portalAreas[a2].portals = p;

		doublePortals[i].portals[1] = p;
	}

	src->ExpectTokenString( "}" );
}

/*
================
idRenderWorldLocal::ParseNodes
================
*/
void idRenderWorldLocal::ParseNodes( Lexer *src ) {
	int			i;

	src->ExpectTokenString( "{" );

	numAreaNodes = src->ParseInt();
	if ( numAreaNodes < 0 ) {
		src->Error( "R_ParseNodes: bad numAreaNodes" );
	}
	areaNodes = (areaNode_t *)R_ClearedStaticAlloc( numAreaNodes * sizeof( areaNodes[0] ) );

	for ( i = 0 ; i < numAreaNodes ; i++ ) {
		areaNode_t	*node;

		node = &areaNodes[i];

		src->Parse1DMatrix( 4, node->plane.ToFloatPtr() );
		node->children[0] = src->ParseInt();
		node->children[1] = src->ParseInt();
	}

	src->ExpectTokenString( "}" );

	// CommonChildrenArea_r (run at finalize) recurses this graph over file-
	// provided child indices with no bounds or cycle guard, so validate it here
	// with the same bounds/area-range/cycle/depth checks the binary cache path
	// enforces. Nodes are written after portals, so numPortalAreas is already
	// set for any multi-area map that carries nodes.
	if ( numAreaNodes > 0
			&& !R_RenderWorldCacheValidateNodeGraph( areaNodes, numAreaNodes, numPortalAreas ) ) {
		src->Error( "R_ParseNodes: invalid area node graph" );
	}
}

/*
================
idRenderWorldLocal::CommonChildrenArea_r
================
*/
int idRenderWorldLocal::CommonChildrenArea_r( areaNode_t *node ) {
	int	nums[2];

	for ( int i = 0 ; i < 2 ; i++ ) {
		if ( node->children[i] <= 0 ) {
			nums[i] = -1 - node->children[i];
		} else {
			nums[i] = CommonChildrenArea_r( &areaNodes[ node->children[i] ] );
		}
	}

	// solid nodes will match any area
	if ( nums[0] == AREANUM_SOLID ) {
		nums[0] = nums[1];
	}
	if ( nums[1] == AREANUM_SOLID ) {
		nums[1] = nums[0];
	}

	int	common;
	if ( nums[0] == nums[1] ) {
		common = nums[0];
	} else {
		common = CHILDREN_HAVE_MULTIPLE_AREAS;
	}

	node->commonChildrenArea = common;

	return common;
}

/*
=================
idRenderWorldLocal::ClearWorld

Sets up for a single area world
=================
*/
void idRenderWorldLocal::ClearWorld() {
	numPortalAreas = 1;
	portalAreas = (portalArea_t *)R_ClearedStaticAlloc( sizeof( portalAreas[0] ) );
	areaScreenRect = (idScreenRect *) R_ClearedStaticAlloc( sizeof( idScreenRect ) );

	SetupAreaRefs();

	// even though we only have a single area, create a node
	// that has both children pointing at it so we don't need to
	//
	areaNodes = (areaNode_t *)R_ClearedStaticAlloc( sizeof( areaNodes[0] ) );
	areaNodes[0].plane[3] = 1;
	areaNodes[0].children[0] = -1;
	areaNodes[0].children[1] = -1;
}

/*
===========================
idRenderWorldLocal::WriteMD5R

Retail Quake 4 writes fully packed MD5RProc worlds from here. openQ4 now does
the same when the world was loaded from an MD5RProc companion and still has the
shared packed buffer state resident, or when a classic proc world has already
been converted into resident MD5R proc data during this session.
===========================
*/
bool idRenderWorldLocal::WriteMD5R( bool compressed ) {
	const char *worldName = mapName.Length() > 0 ? mapName.c_str() : "<unnamed>";

	if ( !R_IsMD5RWriteAvailable() ) {
		common->Warning(
			"idRenderWorldLocal::WriteMD5R: MD5R export is not available in this build for world '%s'",
			worldName );
		return false;
	}

	if ( mapName.Length() == 0 || mapName == "<FREED>" ) {
		common->Warning( "idRenderWorldLocal::WriteMD5R: no active world is loaded" );
		return false;
	}

	idStr exportFilename = mapName;
	exportFilename.SetFileExtension( MD5R_PROC_FILE_EXT );

	idFile *outFile = fileSystem->OpenFileWrite( exportFilename.c_str(), "fs_savepath" );
	if ( outFile == NULL ) {
		common->Warning(
			"idRenderWorldLocal::WriteMD5R: couldn't open '%s' for MD5RProc export from world '%s'",
			exportFilename.c_str(),
			worldName );
		return false;
	}

	common->Printf( "writing %s\n", exportFilename.c_str() );
	outFile->WriteFloatString( "%s %d\n", MD5R_PROC_FILE_ID, MD5R_PROC_FILEVERSION );
	outFile->WriteFloatString( "%u\n\n", mapFileCRC );

	const idRenderWorldMD5RProcData *md5rProcData = this->md5rProcData;
	if ( md5rProcData != NULL && md5rProcData->HasPackedWorldData() && md5rProcData->models.Num() > 0 ) {
		rvRenderModelMD5R::WriteSharedVertexBuffers( *outFile, md5rProcData->vertexBuffers, "" );
		if ( md5rProcData->indexBuffers.Num() > 0 ) {
			rvRenderModelMD5R::WriteSharedIndexBuffers( *outFile, md5rProcData->indexBuffers, "" );
		}
		if ( md5rProcData->silEdges.Num() > 0 ) {
			rvRenderModelMD5R::WriteSharedSilhouetteEdges( *outFile, md5rProcData->silEdges, "" );
		}
		R_RenderWorld_WritePackedMD5RProcModels( *outFile, *md5rProcData );
		for ( int modelIndex = 0; modelIndex < localModels.Num(); ++modelIndex ) {
			idRenderModel *model = localModels[ modelIndex ];
			if ( model != NULL && R_RenderWorld_IsShadowModel( *model ) ) {
				R_RenderWorld_WriteClassicMD5RProcShadowModel( *outFile, *model );
			}
		}
	} else {
		for ( int modelIndex = 0; modelIndex < localModels.Num(); ++modelIndex ) {
			idRenderModel *model = localModels[ modelIndex ];
			if ( model == NULL ) {
				continue;
			}

			if ( R_RenderWorld_IsShadowModel( *model ) ) {
				R_RenderWorld_WriteClassicMD5RProcShadowModel( *outFile, *model );
			} else {
				R_RenderWorld_WriteClassicMD5RProcModel( *outFile, *model );
			}
		}
	}

	R_RenderWorld_WriteClassicMD5RProcInterAreaPortals( *this, *outFile );
	R_RenderWorld_WriteClassicMD5RProcNodes( *this, *outFile );
	fileSystem->CloseFile( outFile );

	if ( compressed ) {
		const idStr savePathExportFilename = fileSystem->RelativePathToOSPath( exportFilename.c_str(), "fs_savepath" );
		idLexer::WriteBinaryFile( savePathExportFilename.c_str(), true );
	} else {
		idStr compiledExportFilename = exportFilename;
		compiledExportFilename += Lexer::sCompiledFileSuffix;
		const idStr compiledSavePath = fileSystem->RelativePathToOSPath( compiledExportFilename.c_str(), "fs_savepath" );
		fileSystem->RemoveExplicitFile( compiledSavePath.c_str() );
	}

	common->Printf(
		"idRenderWorldLocal::WriteMD5R: wrote %sMD5RProc companion '%s' for world '%s'\n",
		( md5rProcData != NULL && md5rProcData->HasPackedWorldData() && md5rProcData->models.Num() > 0 ) ? "packed " : "interim ",
		exportFilename.c_str(),
		worldName );
	return true;
}

/*
=================
idRenderWorldLocal::FreeDefs

dump all the interactions
=================
*/
void idRenderWorldLocal::FreeDefs() {
	// FreeDefs also runs directly during map shutdown. Drain queued updates
	// before clearing the handle tables; queued frees cannot own erased slots.
	R_RenderThread_Sync( "world defs" );
	int		i;

	// render-demo in-world GUIs and joint history belong to the entities going away
	FreeDemoGuiSurfaces( -1 );
	ResetDemoJointHistory( -1 );

	generateAllInteractionsCalled = false;

	// Every def is about to be freed one by one; clearing the draw-surf area memo
	// up front keeps each FreeEntityDef's per-entity invalidation trivial instead
	// of scanning a still-populated memo thousands of times (e.g. same-map restart
	// via InitFromMap's retain path, which frees defs without FreeWorld's clear).
	R_ClearDrawSurfAreaMemo();

	if ( interactionTable ) {
		R_StaticFree( interactionTable );
		interactionTable = NULL;
	}

	// free all lightDefs
	for ( i = 0 ; i < lightDefs.Num() ; i++ ) {
		idRenderLightLocal	*light;

		light = lightDefs[i];
		if ( light && light->world == this ) {
			FreeLightDef( i );
			lightDefs[i] = NULL;
		}
	}

	// free all entityDefs
	for ( i = 0 ; i < entityDefs.Num() ; i++ ) {
		idRenderEntityLocal	*mod;

		mod = entityDefs[i];
		if ( mod && mod->world == this ) {
			FreeEntityDef( i );
			entityDefs[i] = NULL;
		}
	}

	// FreeEntityDef drops each handle as it goes, so this is only a backstop: a
	// handle surviving a map change would ring an entity in the next map.
	throughWorldOutlineEntities.Clear();

	FreeDeferredLightDefs();

	// free all effectDefs
	for ( i = 0 ; i < effectsDef.Num() ; i++ ) {
		rvRenderEffectLocal* effect;

		effect = effectsDef[i];
		if ( effect && effect->world == this ) {
			FreeEffectDef( i );
			effectsDef[i] = NULL;
		}
	}
}

/*
=================
R_RenderWorld_ReadBinaryAwareTimestamp

Retail Quake 4 timestamps and opens .proc / MD5RProc files through the binary-
aware lexer path. Mirror that here so companion .c files participate in reload
checks and discovery the same way they do in the shipping game.
=================
*/
static ID_TIME_T R_RenderWorld_ReadBinaryAwareTimestamp( const idStr &filename, idStr *resolvedFilename = NULL ) {
	if ( resolvedFilename != NULL ) {
		resolvedFilename->Clear();
	}

	if ( cvarSystem->GetCVarBool( "com_binaryRead" ) ) {
		idStr compiledFilename = filename;
		compiledFilename += Lexer::sCompiledFileSuffix;

		ID_TIME_T compiledTimeStamp;
		fileSystem->ReadFile( compiledFilename, NULL, &compiledTimeStamp );
		if ( compiledTimeStamp != FILE_NOT_FOUND_TIMESTAMP ) {
			if ( resolvedFilename != NULL ) {
				*resolvedFilename = compiledFilename;
			}
			return compiledTimeStamp;
		}
	}

	ID_TIME_T sourceTimeStamp;
	fileSystem->ReadFile( filename, NULL, &sourceTimeStamp );
	if ( resolvedFilename != NULL && sourceTimeStamp != FILE_NOT_FOUND_TIMESTAMP ) {
		*resolvedFilename = filename;
	}

	return sourceTimeStamp;
}

/*
================
R_RenderWorld_HasMD5RProcCompanion

Retail prefers a compiled MD5RProc companion when binary reads are enabled, then
falls back to the text MD5RProc file before using the classic .proc world.
=================
*/
static bool R_RenderWorld_HasMD5RProcCompanion( const char *mapName, idStr &md5rProcFilename,
		ID_TIME_T *timeStamp = NULL, idStr *resolvedFilename = NULL ) {
	if ( r_forceConvertMD5R.GetBool() ) {
		md5rProcFilename.Clear();
		if ( resolvedFilename != NULL ) {
			resolvedFilename->Clear();
		}
		if ( timeStamp != NULL ) {
			*timeStamp = FILE_NOT_FOUND_TIMESTAMP;
		}
		return false;
	}

	md5rProcFilename = mapName;
	md5rProcFilename.SetFileExtension( MD5R_PROC_FILE_EXT );

	const ID_TIME_T md5rProcTimeStamp = R_RenderWorld_ReadBinaryAwareTimestamp( md5rProcFilename, resolvedFilename );
	if ( timeStamp != NULL ) {
		*timeStamp = md5rProcTimeStamp;
	}
	if ( md5rProcTimeStamp == FILE_NOT_FOUND_TIMESTAMP ) {
		md5rProcFilename.Clear();
		return false;
	}

	return true;
}

/*
=================
idRenderWorldLocal::InitFromMap

A NULL or empty name will make a world without a map model, which
is still useful for displaying a bare model
=================
*/
bool idRenderWorldLocal::InitFromMap( const char *name ) {
	R_RenderThread_Sync( "world" );
	Lexer *			src;
	idToken			token;
	idStr			filename;
	idStr			procSourceFilename;
	idStr			md5rProcFilename;
	idStr			md5rProcSourceFilename;
	ID_TIME_T		procTimeStamp;
	ID_TIME_T		md5rProcTimeStamp = FILE_NOT_FOUND_TIMESTAMP;
	idRenderModel *	lastModel;

	// if this is an empty world, initialize manually
	if ( !name || !name[0] ) {
		FreeWorld();
		mapName.Clear();
		mapFileCRC = 0u;
		ClearWorld();
		return true;
	}


	// load it
	filename = name;
	filename.SetFileExtension( PROC_FILE_EXT );
	procTimeStamp = R_RenderWorld_ReadBinaryAwareTimestamp( filename, &procSourceFilename );

	R_DisableUnavailableMD5RCVar( r_convertProcToMD5R, "the MD5R proc-world runtime" );

	const bool hasMD5RProcCompanion = R_RenderWorld_HasMD5RProcCompanion(
		name, md5rProcFilename, &md5rProcTimeStamp, &md5rProcSourceFilename );
	if ( hasMD5RProcCompanion ) {
		common->DPrintf(
			"Found MD5RProc companion '%s' for map '%s'; openQ4 will prefer it before the classic proc world '%s'.\n",
			md5rProcFilename.c_str(),
			name,
			filename.c_str() );
	}

	// if we are reloading the same map, check the timestamp
	// and try to skip all the work
	const ID_TIME_T currentTimeStamp = hasMD5RProcCompanion ? md5rProcTimeStamp : procTimeStamp;

	// A world decoded from the generated cache must not outlive a pure lock
	// (or fs_restrict): once those reads are refused, load it again from the
	// pure paks instead of retaining it. See
	// idFileSystemLocal::GeneratedCacheReadsRefused.
	const bool retainedWorldRefused = worldFromGeneratedCache
		&& !fileSystem->GeneratedCacheReadsEnabled( GENERATED_CACHE_RENDER_WORLD );
	if ( name == mapName && retainedWorldRefused ) {
		common->Printf( "idRenderWorldLocal::InitFromMap: reloading a world decoded from the generated cache: generated-cache reads are refused\n" );
	}

	if ( name == mapName && !retainedWorldRefused ) {
		if ( currentTimeStamp != FILE_NOT_FOUND_TIMESTAMP && currentTimeStamp == mapTimeStamp ) {
			common->Printf( "idRenderWorldLocal::InitFromMap: retaining existing map\n" );
			FreeDefs();
			TouchWorldModels();
			AddWorldModelEntities();
			SetupLightGrid();
			ClearPortalStates();
			fileSystem->RecordLevelLoadResource(
				LEVEL_LOAD_RESOURCE_WORLD,
				hasMD5RProcCompanion
					? ( md5rProcSourceFilename.Length() > 0 ? md5rProcSourceFilename.c_str() : md5rProcFilename.c_str() )
					: ( procSourceFilename.Length() > 0 ? procSourceFilename.c_str() : filename.c_str() ),
				hasMD5RProcCompanion ? RENDER_WORLD_CACHE_MD5R_SEMANTIC : RENDER_WORLD_CACHE_CLASSIC_SEMANTIC,
				0u,
				3u );
			return true;
		}
		common->Printf( "idRenderWorldLocal::InitFromMap: timestamp has changed, reloading.\n" );
	}

	FreeWorld();

	if ( hasMD5RProcCompanion ) {
		src = LexerFactory::MakeLexer( md5rProcFilename.c_str(), LEXFL_NOSTRINGCONCAT | LEXFL_NODOLLARPRECOMPILE, false );
		if ( src->IsLoaded() && R_RenderWorld_ParseSupportedMD5RProc( *this, *src, md5rProcFilename.c_str() ) ) {
			delete src;

			mapName = name;
			mapTimeStamp = md5rProcTimeStamp;
			common->Printf(
				"idRenderWorldLocal::InitFromMap: loaded MD5RProc companion '%s' for map '%s'\n",
				md5rProcFilename.c_str(),
				name );

			if ( session->writeDemo ) {
				WriteLoadMap();
			}

			R_RenderWorld_FinalizeLoadedWorld( *this );
			fileSystem->RecordLevelLoadResource(
				LEVEL_LOAD_RESOURCE_WORLD,
				md5rProcSourceFilename.Length() > 0 ? md5rProcSourceFilename.c_str() : md5rProcFilename.c_str(),
				RENDER_WORLD_CACHE_MD5R_SEMANTIC,
				0u,
				3u );
			return true;
		}

		delete src;
		FreeWorld();
		common->Warning(
			"idRenderWorldLocal::InitFromMap: falling back to classic proc '%s' after MD5RProc companion '%s'",
			filename.c_str(),
			md5rProcFilename.c_str() );
	}

	const char *procCacheSource = procSourceFilename.Length() > 0
		? procSourceFilename.c_str() : filename.c_str();
	const idStr cacheSettingsKey = R_RenderWorldCacheBuildSettingsKey();
	// Proc-to-MD5R conversion selects a shared packed representation that this
	// world codec intentionally refuses to flatten.  In that mode source parsing
	// remains authoritative and no unusable cache is probed or generated.
	if ( procTimeStamp != FILE_NOT_FOUND_TIMESTAMP && !r_convertProcToMD5R.GetBool() ) {
		idFile *cacheFile = fileSystem->OpenGeneratedCacheRead(
			GENERATED_CACHE_RENDER_WORLD,
			procCacheSource,
			RENDER_WORLD_CACHE_PARSER_VERSION,
			cacheSettingsKey.c_str() );
		if ( cacheFile != NULL ) {
			const bool loadedFromCache = ReadLevelLoadCachePayload( *cacheFile, name, procTimeStamp );
			fileSystem->CloseFile( cacheFile );
			if ( loadedFromCache ) {
				worldFromGeneratedCache = true;
				fileSystem->RecordLevelLoadResource(
					LEVEL_LOAD_RESOURCE_WORLD,
					procCacheSource,
					RENDER_WORLD_CACHE_CLASSIC_SEMANTIC,
					0u,
					3u );
				if ( session->writeDemo ) {
					WriteLoadMap();
				}
				common->DPrintf( "Loaded generated render-world cache for '%s'\n", procCacheSource );
				return true;
			}
			fileSystem->DiscardGeneratedCache(
				GENERATED_CACHE_RENDER_WORLD,
				procCacheSource,
				RENDER_WORLD_CACHE_PARSER_VERSION,
				cacheSettingsKey.c_str() );
		}
	}

	src = LexerFactory::MakeLexer(
		filename.c_str(),
		LEXFL_NOSTRINGCONCAT | LEXFL_NODOLLARPRECOMPILE,
		false );
	if ( !src->IsLoaded() ) {
		delete src;
		if ( hasMD5RProcCompanion ) {
			common->Printf(
				"idRenderWorldLocal::InitFromMap: classic proc '%s' not found, and MD5RProc companion '%s' could not be loaded\n",
				filename.c_str(),
				md5rProcFilename.c_str() );
		} else {
			common->Printf( "idRenderWorldLocal::InitFromMap: %s not found\n", filename.c_str() );
		}
		ClearWorld();
		return false;
	}


	mapName = name;
	mapTimeStamp = procTimeStamp;

	// if we are writing a demo, archive the load command
	if ( session->writeDemo ) {
		WriteLoadMap();
	}

	if ( !src->ReadToken( &token ) || token.Icmp( PROC_FILE_ID ) ) {
		common->Printf( "idRenderWorldLocal::InitFromMap: bad id '%s' instead of '%s'\n", token.c_str(), PROC_FILE_ID );
		delete src;
		return false;
	}

// jmarshall: quake 4 proc format
	if (!src->ReadToken(&token) || token.Icmp(PROC_FILEVERSION)) {
		common->Printf("idRenderWorldLocal::InitFromMap: bad version '%s' instead of '%s'\n", token.c_str(), PROC_FILEVERSION);
		delete src;
		return false;
	}

	mapFileCRC = 0u;
	if ( src->ReadToken( &token ) ) {
		mapFileCRC = token.GetUnsignedLongValue();
	}
// jmarshall end

	// parse the file
	while ( 1 ) {
		if ( !src->ReadToken( &token ) ) {
			break;
		}

		if ( token == "model" ) {
			lastModel = ParseModel( src );

			// add it to the model manager list
			renderModelManager->AddModel( lastModel );

			// save it in the list to free when clearing this map
			localModels.Append( lastModel );
			continue;
		}

		if ( token == "shadowModel" ) {
			lastModel = ParseShadowModel( src );

			// add it to the model manager list
			renderModelManager->AddModel( lastModel );

			// save it in the list to free when clearing this map
			localModels.Append( lastModel );
			continue;
		}

		if ( token == "interAreaPortals" ) {
			ParseInterAreaPortals( src );
			continue;
		}

		if ( token == "nodes" ) {
			ParseNodes( src );
			continue;
		}

		src->Error( "idRenderWorldLocal::InitFromMap: bad token \"%s\"", token.c_str() );
	}

	delete src;

	if ( r_convertProcToMD5R.GetBool() ) {
		ConvertProcToMD5R();
	}

	R_RenderWorld_FinalizeLoadedWorld( *this );
	fileSystem->RecordLevelLoadResource(
		LEVEL_LOAD_RESOURCE_WORLD,
		procCacheSource,
		RENDER_WORLD_CACHE_CLASSIC_SEMANTIC,
		0u,
		3u );

	if ( !r_convertProcToMD5R.GetBool()
		&& fileSystem->GeneratedCacheWritesEnabled( GENERATED_CACHE_RENDER_WORLD ) ) {
		idRenderWorldCacheMemoryFileOwner cachePayloadOwner;
		idFile *cachePayload = cachePayloadOwner.Get();
		if ( cachePayload != NULL && WriteLevelLoadCachePayload( *cachePayload ) && cachePayload->Length() > 0 ) {
			const char *cachePayloadData = cachePayload->GetDataPtr();
			if ( cachePayloadData != NULL ) {
				fileSystem->WriteGeneratedCache(
					GENERATED_CACHE_RENDER_WORLD,
					procCacheSource,
					RENDER_WORLD_CACHE_PARSER_VERSION,
					cacheSettingsKey.c_str(),
					cachePayloadData,
					static_cast<unsigned int>( cachePayload->Length() ) );
			}
		}
	}

	// done!
	return true;
}

/*
=====================
idRenderWorldLocal::ClearPortalStates
=====================
*/
void idRenderWorldLocal::ClearPortalStates() {
	int		i, j;

	// all portals start off open
	for ( i = 0 ; i < numInterAreaPortals ; i++ ) {
		doublePortals[i].blockingBits = PS_BLOCK_NONE;
	}

	// flood fill all area connections
	for ( i = 0 ; i < numPortalAreas ; i++ ) {
		for ( j = 0 ; j < NUM_PORTAL_ATTRIBUTES ; j++ ) {
			connectedAreaNum++;
			FloodConnectedAreas( &portalAreas[i], j );
		}
	}
}

/*
=====================
idRenderWorldLocal::AddWorldModelEntities
=====================
*/
void idRenderWorldLocal::AddWorldModelEntities() {
	int		i;

	// add the world model for each portal area
	// we can't just call AddEntityDef, because that would place the references
	// based on the bounding box, rather than explicitly into the correct area
	for ( i = 0 ; i < numPortalAreas ; i++ ) {
		idRenderEntityLocal	*def;
		int			index;

		def = new idRenderEntityLocal;

		// try and reuse a free spot
		index = entityDefs.FindNull();
		if ( index == -1 ) {
			index = entityDefs.Append(def);
		} else {
			entityDefs[index] = def;
		}

		def->index = index;
		def->world = this;

		def->parms.hModel = renderModelManager->FindModel( va("_area%i", i ) );
		if ( def->parms.hModel->IsDefaultModel() || !def->parms.hModel->IsStaticWorldModel() ) {
			common->Error( "idRenderWorldLocal::InitFromMap: bad area model lookup" );
		}

		idRenderModel *hModel = def->parms.hModel;
		portalAreas[i].globalBounds = hModel->Bounds();
		const int surfaceCount = hModel->NumSurfaces();
		if ( idRenderModelStatic *staticModel = dynamic_cast<idRenderModelStatic *>( hModel ) ) {
			def->needsPortalSky = staticModel->HasProcSky();
		}

		for ( int j = 0; j < surfaceCount; j++ ) {
			const modelSurface_t *surf = hModel->Surface( j );

			if ( surf->shader != NULL && ( surf->shader->IsPortalSky()
				|| surf->shader->Texgen() == TG_SKYBOX_CUBE
				|| surf->shader->Texgen() == TG_WOBBLESKY_CUBE ) ) {
				def->needsPortalSky = true;
			}
		}

		def->referenceBounds = def->parms.hModel->Bounds();

		def->parms.axis[0][0] = 1;
		def->parms.axis[1][1] = 1;
		def->parms.axis[2][2] = 1;

		R_AxisToModelMatrix( def->parms.axis, def->parms.origin, def->modelMatrix );

		// in case an explicit shader is used on the world, we don't
		// want it to have a 0 alpha or color
		def->parms.shaderParms[0] =
		def->parms.shaderParms[1] =
		def->parms.shaderParms[2] =
		def->parms.shaderParms[3] = 1;

		AddEntityRefToArea( def, &portalAreas[i] );
	}
}

/*
=====================
CheckAreaForPortalSky
=====================
*/
bool idRenderWorldLocal::CheckAreaForPortalSky( int areaNum ) {
	areaReference_t	*ref;

	assert( areaNum >= 0 && areaNum < numPortalAreas );

	for ( ref = portalAreas[areaNum].entityRefs.areaNext; ref->entity; ref = ref->areaNext ) {
		assert( ref->area == &portalAreas[areaNum] );

		if ( ref->entity && ref->entity->needsPortalSky ) {
			return true;
		}
	}

	return false;
}

/*
=====================
idRenderWorldLocal::HasSkybox
=====================
*/
bool idRenderWorldLocal::HasSkybox( int areaNum ) {
	if ( areaNum < 0 || areaNum >= numPortalAreas ) {
		return false;
	}
	return CheckAreaForPortalSky( areaNum );
}
