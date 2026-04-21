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

/*
===============================================================================

	Trace model vs. polygonal model collision detection.

===============================================================================
*/




#include "CollisionModel_local.h"

#define CM_FILE_EXT			"cm"
#define CM_FILEID			"CM"
#define CM_FILEVERSION		"3"


/*
===============================================================================

Writing of collision model file

===============================================================================
*/

void CM_GetNodeBounds( idBounds *bounds, cm_node_t *node );
int CM_GetNodeContents( cm_node_t *node );

static idStr CM_GetFileModelName( const idCollisionModelLocal *model ) {
	idStr name = model->name;

	if ( idStr::IcmpnPath( name.c_str(), "maps/", 5 ) == 0 ) {
		const int slash = name.Last( '/' );
		if ( slash != -1 ) {
			name = name.c_str() + slash + 1;
		}
	}

	return name;
}


/*
================
idCollisionModelManagerLocal::WriteNodes
================
*/
void idCollisionModelManagerLocal::WriteNodes( idFile *fp, cm_node_t *node ) {
	fp->WriteFloatString( "\t( %d %f )\n", node->planeType, node->planeDist );
	if ( node->planeType != -1 ) {
		WriteNodes( fp, node->children[0] );
		WriteNodes( fp, node->children[1] );
	}
}

/*
================
idCollisionModelManagerLocal::CountPolygonMemory
================
*/
int idCollisionModelManagerLocal::CountPolygonMemory( cm_node_t *node ) const {
	cm_polygonRef_t *pref;
	cm_polygon_t *p;
	int memory;

	memory = 0;
	for ( pref = node->polygons; pref; pref = pref->next ) {
		p = pref->p;
		if ( p->checkcount == checkCount ) {
			continue;
		}
		p->checkcount = checkCount;

		memory += sizeof( cm_polygon_t ) + ( p->numEdges - 1 ) * sizeof( p->edges[0] );
	}
	if ( node->planeType != -1 ) {
		memory += CountPolygonMemory( node->children[0] );
		memory += CountPolygonMemory( node->children[1] );
	}
	return memory;
}

/*
================
idCollisionModelManagerLocal::WritePolygons
================
*/
void idCollisionModelManagerLocal::WritePolygons( idFile *fp, cm_node_t *node ) {
	cm_polygonRef_t *pref;
	cm_polygon_t *p;
	int i;

	for ( pref = node->polygons; pref; pref = pref->next ) {
		p = pref->p;
		if ( p->checkcount == checkCount ) {
			continue;
		}
		p->checkcount = checkCount;
		fp->WriteFloatString( "\t%d (", p->numEdges );
		for ( i = 0; i < p->numEdges; i++ ) {
			fp->WriteFloatString( " %d", p->edges[i] );
		}
		fp->WriteFloatString( " ) ( %f %f %f ) %f", p->plane.Normal()[0], p->plane.Normal()[1], p->plane.Normal()[2], p->plane.Dist() );
		fp->WriteFloatString( " ( %f %f %f )", p->bounds[0][0], p->bounds[0][1], p->bounds[0][2] );
		fp->WriteFloatString( " ( %f %f %f )", p->bounds[1][0], p->bounds[1][1], p->bounds[1][2] );
		fp->WriteFloatString( " \"%s\"", p->material->GetName() );
		fp->WriteFloatString( " ( %f %f )", p->texBounds[0][0], p->texBounds[0][1] );
		fp->WriteFloatString( " ( %f %f )", p->texBounds[1][0], p->texBounds[1][1] );
		fp->WriteFloatString( " ( %f %f ) %d", p->texBounds[2][0], p->texBounds[2][1], p->primitiveNum );
		fp->WriteFloatString( "\n" );
	}
	if ( node->planeType != -1 ) {
		WritePolygons( fp, node->children[0] );
		WritePolygons( fp, node->children[1] );
	}
}

/*
================
idCollisionModelManagerLocal::CountBrushMemory
================
*/
int idCollisionModelManagerLocal::CountBrushMemory( cm_node_t *node ) const {
	cm_brushRef_t *bref;
	cm_brush_t *b;
	int memory;

	memory = 0;
	for ( bref = node->brushes; bref; bref = bref->next ) {
		b = bref->b;
		if ( b->checkcount == checkCount ) {
			continue;
		}
		b->checkcount = checkCount;

		memory += sizeof( cm_brush_t ) + ( b->numPlanes - 1 ) * sizeof( b->planes[0] );
	}
	if ( node->planeType != -1 ) {
		memory += CountBrushMemory( node->children[0] );
		memory += CountBrushMemory( node->children[1] );
	}
	return memory;
}

/*
================
idCollisionModelManagerLocal::WriteBrushes
================
*/
void idCollisionModelManagerLocal::WriteBrushes( idFile *fp, cm_node_t *node ) {
	cm_brushRef_t *bref;
	cm_brush_t *b;
	int i;

	for ( bref = node->brushes; bref; bref = bref->next ) {
		b = bref->b;
		if ( b->checkcount == checkCount ) {
			continue;
		}
		b->checkcount = checkCount;
		fp->WriteFloatString( "\t%d {\n", b->numPlanes );
		for ( i = 0; i < b->numPlanes; i++ ) {
			fp->WriteFloatString( "\t\t( %f %f %f ) %f\n", b->planes[i].Normal()[0], b->planes[i].Normal()[1], b->planes[i].Normal()[2], b->planes[i].Dist() );
		}
		fp->WriteFloatString( "\t} ( %f %f %f )", b->bounds[0][0], b->bounds[0][1], b->bounds[0][2] );
		fp->WriteFloatString( " ( %f %f %f ) \"%s\" %d\n", b->bounds[1][0], b->bounds[1][1], b->bounds[1][2], StringFromContents( b->contents ), b->primitiveNum );
	}
	if ( node->planeType != -1 ) {
		WriteBrushes( fp, node->children[0] );
		WriteBrushes( fp, node->children[1] );
	}
}

/*
================
idCollisionModelManagerLocal::WriteCollisionModel
================
*/
void idCollisionModelManagerLocal::WriteCollisionModel( idFile *fp, idCollisionModelLocal *model ) {
	int i, polygonMemory, brushMemory;
	const idStr name = CM_GetFileModelName( model );

	fp->WriteFloatString( "collisionModel \"%s\" %d {\n", name.c_str(), model->numPrimitives );
	// vertices
	fp->WriteFloatString( "\tvertices { /* numVertices = */ %d\n", model->numVertices );
	for ( i = 0; i < model->numVertices; i++ ) {
		fp->WriteFloatString( "\t/* %d */ ( %f %f %f )\n", i, model->vertices[i].p[0], model->vertices[i].p[1], model->vertices[i].p[2] );
	}
	fp->WriteFloatString( "\t}\n" );
	// edges
	fp->WriteFloatString( "\tedges { /* numEdges = */ %d\n", model->numEdges );
	for ( i = 0; i < model->numEdges; i++ ) {
		fp->WriteFloatString( "\t/* %d */ ( %d %d ) %d %d\n", i, model->edges[i].vertexNum[0], model->edges[i].vertexNum[1], model->edges[i].internal, model->edges[i].numUsers );
	}
	fp->WriteFloatString( "\t}\n" );
	// nodes
	fp->WriteFloatString( "\tnodes {\n" );
	WriteNodes( fp, model->node );
	fp->WriteFloatString( "\t}\n" );
	// polygons
	checkCount++;
	polygonMemory = CountPolygonMemory( model->node );
	fp->WriteFloatString( "\tpolygons /* polygonMemory = */ %d {\n", polygonMemory );
	checkCount++;
	WritePolygons( fp, model->node );
	fp->WriteFloatString( "\t}\n" );
	// brushes
	checkCount++;
	brushMemory = CountBrushMemory( model->node );
	fp->WriteFloatString( "\tbrushes /* brushMemory = */ %d {\n", brushMemory );
	checkCount++;
	WriteBrushes( fp, model->node );
	fp->WriteFloatString( "\t}\n" );
	// closing brace
	fp->WriteFloatString( "}\n" );
}

/*
================
idCollisionModelManagerLocal::WriteCollisionModelsToFile
================
*/
void idCollisionModelManagerLocal::WriteCollisionModelsToFile( const char *filename, int firstModel, int lastModel, unsigned int mapFileCRC ) {
	int i;
	idFile *fp;
	idStr name;
	idStr mask;

	name = filename;
	name.SetFileExtension( CM_FILE_EXT );
	mask = filename;
	if ( !IsRenderModelName( mask.c_str() ) ) {
		mask.StripFileExtension();
		mask += "/";
	}

	common->Printf( "writing %s\n", name.c_str() );
	// Keep runtime-generated collision caches out of fs_cdpath so packaged
	// collision assets remain authoritative on subsequent runs.
	fp = fileSystem->OpenFileWrite( name, "fs_savepath" );
	if ( !fp ) {
		common->Warning( "idCollisionModelManagerLocal::WriteCollisionModelsToFile: Error opening file %s\n", name.c_str() );
		return;
	}

	// write file id and version
	fp->WriteFloatString( "%s \"%s\"\n\n", CM_FILEID, CM_FILEVERSION );
	// write the map file crc
	fp->WriteFloatString( "%u\n\n", mapFileCRC );

	// write the collision models
	for ( i = firstModel; i < lastModel; i++ ) {
		if ( models[ i ] == NULL ) {
			continue;
		}
		if ( !IsRenderModelName( filename ) &&
			 idStr::IcmpnPath( models[ i ]->name.c_str(), mask.c_str(), mask.Length() ) != 0 &&
			 models[ i ]->name.Cmpn( PROC_CLIPMODEL_STRING_PRFX, strlen( PROC_CLIPMODEL_STRING_PRFX ) ) != 0 ) {
			continue;
		}
		WriteCollisionModel( fp, models[ i ] );
	}

	fileSystem->CloseFile( fp );
}

/*
================
idCollisionModelManagerLocal::WriteCollisionModelForMapEntity
================
*/
bool idCollisionModelManagerLocal::WriteCollisionModelForMapEntity( const idMapEntity *mapEnt, const char *filename, const bool testTraceModel ) {
	idFile *fp;
	idStr name;
	idCollisionModelLocal *model;

	SetupHash();
	model = CollisionModelForMapEntity( NULL, mapEnt );
	model->name = filename;

	name = filename;
	name.SetFileExtension( CM_FILE_EXT );

	common->Printf( "writing %s\n", name.c_str() );
	fp = fileSystem->OpenFileWrite( name, "fs_cdpath" );
	if ( !fp ) {
		common->Printf( "idCollisionModelManagerLocal::WriteCollisionModelForMapEntity: Error opening file %s\n", name.c_str() );
		FreeModel( model );
		return false;
	}

	// write file id and version
	fp->WriteFloatString( "%s \"%s\"\n\n", CM_FILEID, CM_FILEVERSION );
	// write the map file crc
	fp->WriteFloatString( "%u\n\n", 0 );

	// write the collision model
	WriteCollisionModel( fp, model );

	fileSystem->CloseFile( fp );

	if ( testTraceModel ) {
		idTraceModel trm;
		TrmFromModel( model, trm );
	}

	FreeModel( model );

	return true;
}


/*
===============================================================================

Loading of collision model file

===============================================================================
*/

/*
================
idCollisionModelManagerLocal::ParseVertices
================
*/
void idCollisionModelManagerLocal::ParseVertices( idLexer *src, idCollisionModelLocal *model ) {
	int i;

	src->ExpectTokenString( "{" );
	model->numVertices = src->ParseInt();
	model->maxVertices = model->numVertices;
	model->vertices = (cm_vertex_t *) Mem_Alloc( model->maxVertices * sizeof( cm_vertex_t ) );
	for ( i = 0; i < model->numVertices; i++ ) {
		src->Parse1DMatrix( 3, model->vertices[i].p.ToFloatPtr() );
		model->vertices[i].side = 0;
		model->vertices[i].sideSet = 0;
		model->vertices[i].checkcount = 0;
	}
	src->ExpectTokenString( "}" );
}

/*
================
idCollisionModelManagerLocal::ParseEdges
================
*/
void idCollisionModelManagerLocal::ParseEdges( idLexer *src, idCollisionModelLocal *model ) {
	int i;

	src->ExpectTokenString( "{" );
	model->numEdges = src->ParseInt();
	model->maxEdges = model->numEdges;
	model->edges = (cm_edge_t *) Mem_Alloc( model->maxEdges * sizeof( cm_edge_t ) );
	for ( i = 0; i < model->numEdges; i++ ) {
		src->ExpectTokenString( "(" );
		model->edges[i].vertexNum[0] = src->ParseInt();
		model->edges[i].vertexNum[1] = src->ParseInt();
		src->ExpectTokenString( ")" );
		model->edges[i].side = 0;
		model->edges[i].sideSet = 0;
		model->edges[i].internal = src->ParseInt();
		model->edges[i].numUsers = src->ParseInt();
		model->edges[i].normal = vec3_origin;
		model->edges[i].checkcount = 0;
		model->numInternalEdges += model->edges[i].internal;
	}
	src->ExpectTokenString( "}" );
}

/*
================
idCollisionModelManagerLocal::ParseNodes
================
*/
cm_node_t *idCollisionModelManagerLocal::ParseNodes( idLexer *src, idCollisionModelLocal *model, cm_node_t *parent ) {
	cm_node_t *node;

	model->numNodes++;
	node = AllocNode( model, model->numNodes < NODE_BLOCK_SIZE_SMALL ? NODE_BLOCK_SIZE_SMALL : NODE_BLOCK_SIZE_LARGE );
	node->brushes = NULL;
	node->polygons = NULL;
	node->parent = parent;
	src->ExpectTokenString( "(" );
	node->planeType = src->ParseInt();
	node->planeDist = src->ParseFloat();
	src->ExpectTokenString( ")" );
	if ( node->planeType != -1 ) {
		node->children[0] = ParseNodes( src, model, node );
		node->children[1] = ParseNodes( src, model, node );
	}
	return node;
}

/*
================
idCollisionModelManagerLocal::ParsePolygons
================
*/
void idCollisionModelManagerLocal::ParsePolygons( idLexer *src, idCollisionModelLocal *model ) {
	cm_polygon_t *p;
	int i, numEdges;
	idVec3 normal;
	idToken token;

	if ( !src->ReadToken( &token ) ) {
		src->Error( "ParsePolygons: unexpected end of file" );
	}
	if ( token == "{" ) {
		// no preamble
	} else if ( token.type == TT_NUMBER ) {
		const int first = token.GetIntValue();
		if ( !src->ReadToken( &token ) ) {
			src->Error( "ParsePolygons: unexpected end of file after preamble" );
		}
		if ( token == "{" ) {
			// legacy block size preamble
			model->polygonBlock = (cm_polygonBlock_t *) Mem_Alloc( sizeof( cm_polygonBlock_t ) + first );
			model->polygonBlock->bytesRemaining = first;
			model->polygonBlock->next = ( (byte *) model->polygonBlock ) + sizeof( cm_polygonBlock_t );
		} else if ( token.type == TT_NUMBER ) {
			// Quake 4 .cm format: numPolygons numPolygonEdges
			src->ExpectTokenString( "{" );
		} else {
			src->Error( "ParsePolygons: expected '{' but found '%s'", token.c_str() );
		}
	} else {
		src->Error( "ParsePolygons: expected '{' but found '%s'", token.c_str() );
	}

	while ( !src->CheckTokenString( "}" ) ) {
		// parse polygon
		numEdges = src->ParseInt();
		p = AllocPolygon( model, numEdges );
		p->numEdges = numEdges;
		src->ExpectTokenString( "(" );
		for ( i = 0; i < p->numEdges; i++ ) {
			p->edges[i] = src->ParseInt();
		}
		src->ExpectTokenString( ")" );
		src->Parse1DMatrix( 3, normal.ToFloatPtr() );
		p->plane.SetNormal( normal );
		p->plane.SetDist( src->ParseFloat() );
		src->Parse1DMatrix( 3, p->bounds[0].ToFloatPtr() );
		src->Parse1DMatrix( 3, p->bounds[1].ToFloatPtr() );
		src->ExpectTokenType( TT_STRING, 0, &token );
		// get material
		p->material = declManager->FindMaterial( token );
		p->contents = p->material->GetContentFlags();
		p->checkcount = 0;
		p->primitiveNum = 0;
		if ( src->ReadToken( &token ) ) {
			if ( token == "(" ) {
				src->UnreadToken( &token );
				src->Parse1DMatrix( 2, p->texBounds[0].ToFloatPtr() );
				src->Parse1DMatrix( 2, p->texBounds[1].ToFloatPtr() );
				src->Parse1DMatrix( 2, p->texBounds[2].ToFloatPtr() );
				p->primitiveNum = src->ParseInt();
			} else {
				src->UnreadToken( &token );
			}
		}
		// filter polygon into tree
		R_FilterPolygonIntoTree( model, model->node, NULL, p );
	}
}

/*
================
idCollisionModelManagerLocal::ParseBrushes
================
*/
void idCollisionModelManagerLocal::ParseBrushes( idLexer *src, idCollisionModelLocal *model ) {
	cm_brush_t *b;
	int i, numPlanes;
	idVec3 normal;
	idToken token;

	if ( !src->ReadToken( &token ) ) {
		src->Error( "ParseBrushes: unexpected end of file" );
	}
	if ( token == "{" ) {
		// no preamble
	} else if ( token.type == TT_NUMBER ) {
		const int first = token.GetIntValue();
		if ( !src->ReadToken( &token ) ) {
			src->Error( "ParseBrushes: unexpected end of file after preamble" );
		}
		if ( token == "{" ) {
			// legacy block size preamble
			model->brushBlock = (cm_brushBlock_t *) Mem_Alloc( sizeof( cm_brushBlock_t ) + first );
			model->brushBlock->bytesRemaining = first;
			model->brushBlock->next = ( (byte *) model->brushBlock ) + sizeof( cm_brushBlock_t );
		} else if ( token.type == TT_NUMBER ) {
			// Quake 4 .cm format: numBrushes numBrushPlanes
			src->ExpectTokenString( "{" );
		} else {
			src->Error( "ParseBrushes: expected '{' but found '%s'", token.c_str() );
		}
	} else {
		src->Error( "ParseBrushes: expected '{' but found '%s'", token.c_str() );
	}

	while ( !src->CheckTokenString( "}" ) ) {
		// parse brush
		numPlanes = src->ParseInt();
		b = AllocBrush( model, numPlanes );
		b->numPlanes = numPlanes;
		src->ExpectTokenString( "{" );
		for ( i = 0; i < b->numPlanes; i++ ) {
			src->Parse1DMatrix( 3, normal.ToFloatPtr() );
			b->planes[i].SetNormal( normal );
			b->planes[i].SetDist( src->ParseFloat() );
		}
		src->ExpectTokenString( "}" );
		src->Parse1DMatrix( 3, b->bounds[0].ToFloatPtr() );
		src->Parse1DMatrix( 3, b->bounds[1].ToFloatPtr() );
		src->ReadToken( &token );
		if ( token.type == TT_NUMBER ) {
			b->contents = token.GetIntValue();		// old .cm files use a single integer
			b->primitiveNum = 0;
		} else {
			b->contents = ContentsFromString( token );
			// Quake 4 .cm brush entries may optionally include a primitive number on the same line.
			// Only consume a token if it appears before a line break to avoid eating the next brush count.
			if ( src->ReadTokenOnLine( &token ) && token.type == TT_NUMBER ) {
				b->primitiveNum = token.GetIntValue();
			} else {
				b->primitiveNum = 0;
			}
		}
		b->checkcount = 0;
		// filter brush into tree
		R_FilterBrushIntoTree( model, model->node, NULL, b );
	}
}

/*
================
idCollisionModelManagerLocal::ParseCollisionModel
================
*/
bool idCollisionModelManagerLocal::ParseCollisionModel( idLexer *src, const char *fileName, unsigned int mapFileCRC ) {
	idCollisionModelLocal *model;
	idToken token;
	idStr fullModelName;
	bool newModel = false;
	int modelIndex;

	// LoadModel() can parse standalone .cm files before LoadMap() allocates
	// the model pointer table.
	EnsureModelTable();

	src->ExpectTokenType( TT_STRING, 0, &token );
	GetFullModelName( fileName, token.c_str(), fullModelName );
	modelIndex = FindModelIndex( fullModelName );
	if ( modelIndex >= 0 ) {
		model = models[ modelIndex ];
		FreeModelMemory( model );
	} else {
		model = AllocModel();
		model->name = fullModelName;
		StoreModel( model );
		newModel = true;
	}
	model->name = fullModelName;
	model->fileTime = mapFileCRC;
	model->refCount = 0;
	
	if ( newModel && model->name.Cmpn( PROC_CLIPMODEL_STRING_PRFX, strlen( PROC_CLIPMODEL_STRING_PRFX ) ) == 0 ) {
		numInlinedProcClipModels++;
	}
	model->numPrimitives = 0;
	if ( !src->ReadToken( &token ) ) {
		src->Error( "ParseCollisionModel: unexpected end of file after model name" );
	}
	if ( token != "{" ) {
		if ( token.type == TT_NUMBER ) {
			model->numPrimitives = token.GetIntValue();
			src->ExpectTokenString( "{" );
		} else {
			src->Error( "ParseCollisionModel: expected '{' but found '%s'", token.c_str() );
		}
	}
	while ( !src->CheckTokenString( "}" ) ) {

		src->ReadToken( &token );

		if ( token == "vertices" ) {
			ParseVertices( src, model );
			continue;
		}

		if ( token == "edges" ) {
			ParseEdges( src, model );
			continue;
		}

		if ( token == "nodes" ) {
			src->ExpectTokenString( "{" );
			model->node = ParseNodes( src, model, NULL );
			src->ExpectTokenString( "}" );
			continue;
		}

		if ( token == "polygons" ) {
			ParsePolygons( src, model );
			continue;
		}

		if ( token == "brushes" ) {
			ParseBrushes( src, model );
			continue;
		}

		src->Error( "ParseCollisionModel: bad token \"%s\"", token.c_str() );
	}
	// calculate edge normals
	checkCount++;
	CalculateEdgeNormals( model, model->node );
	// get model bounds from brush and polygon bounds
	CM_GetNodeBounds( &model->bounds, model->node );
	// get model contents
	model->contents = CM_GetNodeContents( model->node );
	if ( model->numPrimitives <= 0 ) {
		UpdateModelPrimitiveCount( model );
	}
	// total memory used by this model
	model->usedMemory = model->numVertices * sizeof(cm_vertex_t) +
						model->numEdges * sizeof(cm_edge_t) +
						model->polygonMemory +
						model->brushMemory +
						model->numNodes * sizeof(cm_node_t) +
						model->numPolygonRefs * sizeof(cm_polygonRef_t) +
						model->numBrushRefs * sizeof(cm_brushRef_t);

	return true;
}

/*
================
idCollisionModelManagerLocal::LoadCollisionModelFile
================
*/
bool idCollisionModelManagerLocal::LoadCollisionModelFile( const char *name, unsigned int mapFileCRC ) {
	idStr fileName;
	idToken token;
	idLexer *src;
	idFile *pakFile;
	char *pakBuffer;
	unsigned int crc;

	// load it
	fileName = name;
	fileName.SetFileExtension( CM_FILE_EXT );
	pakFile = fileSystem->OpenFileReadFromPak( fileName, false );
	pakBuffer = NULL;
	if ( pakFile != NULL ) {
		const int length = pakFile->Length();
		pakBuffer = (char *) Mem_Alloc( length + 1 );
		if ( pakFile->Read( pakBuffer, length ) != length ) {
			fileSystem->CloseFile( pakFile );
			Mem_Free( pakBuffer );
			common->Warning( "Failed to read packed collision model %s", fileName.c_str() );
			return false;
		}
		pakBuffer[ length ] = '\0';
		fileSystem->CloseFile( pakFile );
		src = new idLexer( pakBuffer, length, fileName, LEXFL_NOSTRINGCONCAT | LEXFL_NODOLLARPRECOMPILE );
	} else {
		src = new idLexer( fileName, LEXFL_NOSTRINGCONCAT | LEXFL_NODOLLARPRECOMPILE );
		if ( !src->IsLoaded() ) {
			delete src;
			return false;
		}
	}

	if ( !src->ExpectTokenString( CM_FILEID ) ) {
		common->Warning( "%s is not an CM file.", fileName.c_str() );
		delete src;
		if ( pakBuffer != NULL ) {
			Mem_Free( pakBuffer );
		}
		return false;
	}

	if ( !src->ReadToken( &token ) || ( token != CM_FILEVERSION && token.Icmp( "3" ) != 0 && token.Icmp( "3.00" ) != 0 ) ) {
		common->Warning( "%s has version %s instead of %s", fileName.c_str(), token.c_str(), CM_FILEVERSION );
		delete src;
		if ( pakBuffer != NULL ) {
			Mem_Free( pakBuffer );
		}
		return false;
	}

	if ( !src->ExpectTokenType( TT_NUMBER, TT_INTEGER, &token ) ) {
		common->Warning( "%s has no map file CRC", fileName.c_str() );
		delete src;
		if ( pakBuffer != NULL ) {
			Mem_Free( pakBuffer );
		}
		return false;
	}

	crc = token.GetUnsignedLongValue();
	if ( mapFileCRC && crc != mapFileCRC ) {
		common->Printf( "%s is out of date\n", fileName.c_str() );
		delete src;
		if ( pakBuffer != NULL ) {
			Mem_Free( pakBuffer );
		}
		return false;
	}

	// parse the file
	while ( 1 ) {
		if ( !src->ReadToken( &token ) ) {
			break;
		}

		if ( token == "collisionModel" ) {
			if ( !ParseCollisionModel( src, name, mapFileCRC ) ) {
				delete src;
				if ( pakBuffer != NULL ) {
					Mem_Free( pakBuffer );
				}
				return false;
			}
			continue;
		}

		src->Error( "idCollisionModelManagerLocal::LoadCollisionModelFile: bad token \"%s\"", token.c_str() );
	}

	delete src;
	if ( pakBuffer != NULL ) {
		Mem_Free( pakBuffer );
	}

	return true;
}
