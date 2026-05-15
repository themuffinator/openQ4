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
	if ( model.NumSurfaces() <= 0 ) {
		return false;
	}

	for ( int surfaceIndex = 0; surfaceIndex < model.NumSurfaces(); ++surfaceIndex ) {
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
	if ( model.NumSurfaces() <= 0 ) {
		return false;
	}

	for ( int surfaceIndex = 0; surfaceIndex < model.NumSurfaces(); ++surfaceIndex ) {
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
	for ( int surfaceIndex = 0; surfaceIndex < model.NumSurfaces(); ++surfaceIndex ) {
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

	for ( int surfaceIndex = 0; surfaceIndex < model.NumSurfaces(); ++surfaceIndex ) {
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

		outFile.WriteFloatString(
			"%d %d %d ",
			winding->GetNumPoints(),
			negativePortal->intoArea,
			positivePortal->intoArea );

		for ( int pointIndex = 0; pointIndex < winding->GetNumPoints(); ++pointIndex ) {
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

/*
================
R_RenderWorld_ParseSupportedMD5RProc

Retail Quake 4 MD5RProc companions serialize shared packed buffers first and
then attach per-area Model blocks that reference those shared arrays. OpenQ4
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
	int i;

	// this will free all the lightDefs and entityDefs
	FreeDefs();

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
	modelSurface_t	surf;

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
	if (!src->PeekTokenString("{") && !src->PeekTokenString("}")) {
		int sky = src->ParseInt();
	}
// jmarshall end

	for ( i = 0 ; i < numSurfaces ; i++ ) {
		src->ExpectTokenString( "{" );

		src->ExpectAnyToken( &token );

		surf.shader = declManager->FindMaterial( token );

		((idMaterial*)surf.shader)->AddReference();

		tri = R_AllocStaticTriSurf();
		surf.geometry = tri;

		tri->numVerts = src->ParseInt();
		tri->numIndexes = src->ParseInt();

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
			tri->indexes[j] = src->ParseInt();
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
	modelSurface_t	surf;

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

Retail Quake 4 writes fully packed MD5RProc worlds from here. OpenQ4 now does
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
		idLexer::WriteBinaryFile( exportFilename.c_str() );
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
	int		i;

	generateAllInteractionsCalled = false;

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
static bool R_RenderWorld_HasMD5RProcCompanion( const char *mapName, idStr &md5rProcFilename, ID_TIME_T *timeStamp = NULL ) {
	if ( r_forceConvertMD5R.GetBool() ) {
		md5rProcFilename.Clear();
		if ( timeStamp != NULL ) {
			*timeStamp = FILE_NOT_FOUND_TIMESTAMP;
		}
		return false;
	}

	md5rProcFilename = mapName;
	md5rProcFilename.SetFileExtension( MD5R_PROC_FILE_EXT );

	const ID_TIME_T md5rProcTimeStamp = R_RenderWorld_ReadBinaryAwareTimestamp( md5rProcFilename );
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
	Lexer *			src;
	idToken			token;
	idStr			filename;
	idStr			md5rProcFilename;
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
	procTimeStamp = R_RenderWorld_ReadBinaryAwareTimestamp( filename );

	R_DisableUnavailableMD5RCVar( r_convertProcToMD5R, "the MD5R proc-world runtime" );

	const bool hasMD5RProcCompanion = R_RenderWorld_HasMD5RProcCompanion( name, md5rProcFilename, &md5rProcTimeStamp );
	if ( hasMD5RProcCompanion ) {
		common->DPrintf(
			"Found MD5RProc companion '%s' for map '%s'; OpenQ4 will prefer it before the classic proc world '%s'.\n",
			md5rProcFilename.c_str(),
			name,
			filename.c_str() );
	}

	// if we are reloading the same map, check the timestamp
	// and try to skip all the work
	const ID_TIME_T currentTimeStamp = hasMD5RProcCompanion ? md5rProcTimeStamp : procTimeStamp;

	if ( name == mapName ) {
		if ( currentTimeStamp != FILE_NOT_FOUND_TIMESTAMP && currentTimeStamp == mapTimeStamp ) {
			common->Printf( "idRenderWorldLocal::InitFromMap: retaining existing map\n" );
			FreeDefs();
			TouchWorldModels();
			AddWorldModelEntities();
			SetupLightGrid();
			ClearPortalStates();
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
			return true;
		}

		delete src;
		FreeWorld();
		common->Warning(
			"idRenderWorldLocal::InitFromMap: falling back to classic proc '%s' after MD5RProc companion '%s'",
			filename.c_str(),
			md5rProcFilename.c_str() );
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

		for ( int j = 0; j < hModel->NumSurfaces(); j++ ) {
			const modelSurface_t *surf = hModel->Surface( j );

			if ( surf->shader != NULL && surf->shader->IsPortalSky() ) {
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
