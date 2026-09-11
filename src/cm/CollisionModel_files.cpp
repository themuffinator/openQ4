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
#include "../idlib/LexerFactory.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

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

	// Retail Q4 writes generated collision caches on fs_devpath.
	fp = fileSystem->OpenFileWrite( name, "fs_devpath" );
	if ( !fp ) {
		common->Warning( "idCollisionModelManagerLocal::WriteCollisionModelsToFile: Error opening file %s\n", name.c_str() );
		return;
	}

	// report where the file actually landed; fs_devpath is unset by default, so
	// this is not necessarily the directory the .map was read from
	common->Printf( "writing %s\n", fp->GetFullPath() );

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
			 idStr::IcmpnPath( models[ i ]->name.c_str(), mask.c_str(), mask.Length() ) != 0 ) {
			continue;
		}
		WriteCollisionModel( fp, models[ i ] );
	}

	fileSystem->CloseFile( fp );

	if ( cvarSystem->GetCVarInteger( "com_BinaryWrite" ) ) {
		idLexer::WriteBinaryFile( name.c_str() );
	}
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

	fp = fileSystem->OpenFileWrite( name, "fs_devpath" );
	if ( !fp ) {
		common->Printf( "idCollisionModelManagerLocal::WriteCollisionModelForMapEntity: Error opening file %s\n", name.c_str() );
		FreeModel( model );
		return false;
	}

	common->Printf( "writing %s\n", fp->GetFullPath() );

	// write file id and version
	fp->WriteFloatString( "%s \"%s\"\n\n", CM_FILEID, CM_FILEVERSION );
	// write the map file crc
	fp->WriteFloatString( "%u\n\n", 0 );

	// write the collision model
	WriteCollisionModel( fp, model );

	fileSystem->CloseFile( fp );

	if ( cvarSystem->GetCVarInteger( "com_BinaryWrite" ) ) {
		idLexer::WriteBinaryFile( name.c_str() );
	}

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
void idCollisionModelManagerLocal::ParseVertices( Lexer *src, idCollisionModelLocal *model ) {
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
void idCollisionModelManagerLocal::ParseEdges( Lexer *src, idCollisionModelLocal *model ) {
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
cm_node_t *idCollisionModelManagerLocal::ParseNodes( Lexer *src, idCollisionModelLocal *model, cm_node_t *parent ) {
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
void idCollisionModelManagerLocal::ParsePolygons( Lexer *src, idCollisionModelLocal *model ) {
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
void idCollisionModelManagerLocal::ParseBrushes( Lexer *src, idCollisionModelLocal *model ) {
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
		b->material = NULL;
		// filter brush into tree
		R_FilterBrushIntoTree( model, model->node, NULL, b );
	}
}

/*
================
idCollisionModelManagerLocal::ParseCollisionModel
================
*/
bool idCollisionModelManagerLocal::ParseCollisionModel( Lexer *src, const char *fileName, unsigned int mapFileCRC ) {
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
	
	if ( newModel && model->name.Cmpn( PROC_CLIPMODEL_STRING_PRFX,
		 static_cast<int>( sizeof( PROC_CLIPMODEL_STRING_PRFX ) - 1 ) ) == 0 ) {
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
	AssignPolygonFeatureIndices( model );
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
	Lexer *src;
	unsigned int crc;

	// load it
	fileName = name;
	fileName.SetFileExtension( CM_FILE_EXT );
	src = LexerFactory::MakeLexer( fileName.c_str(), LEXFL_NOSTRINGCONCAT | LEXFL_NODOLLARPRECOMPILE, false );
	if ( !src->IsLoaded() ) {
		delete src;
		return false;
	}

	if ( !src->ExpectTokenString( CM_FILEID ) ) {
		common->Warning( "%s is not an CM file.", fileName.c_str() );
		delete src;
		return false;
	}

	if ( !src->ReadToken( &token ) || ( token != CM_FILEVERSION && token.Icmp( "3" ) != 0 && token.Icmp( "3.00" ) != 0 ) ) {
		common->Warning( "%s has version %s instead of %s", fileName.c_str(), token.c_str(), CM_FILEVERSION );
		delete src;
		return false;
	}

	if ( !src->ExpectTokenType( TT_NUMBER, TT_INTEGER, &token ) ) {
		common->Warning( "%s has no map file CRC", fileName.c_str() );
		delete src;
		return false;
	}

	crc = token.GetUnsignedLongValue();
	if ( mapFileCRC && crc != mapFileCRC ) {
		common->Printf( "%s is out of date\n", fileName.c_str() );
		delete src;
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
				return false;
			}
			continue;
		}

		src->Error( "idCollisionModelManagerLocal::LoadCollisionModelFile: bad token \"%s\"", token.c_str() );
	}

	delete src;
	return true;
}


/*
===============================================================================

	Private generated collision cache

	This codec is intentionally unrelated to the retail-compatible text .cm and
	binary .cmc paths above.  The framework owns the authenticated envelope and
	the writable generated/ namespace; this file owns only the pointer-free
	collision payload and its transactional adoption.

===============================================================================
*/

namespace {
	static const unsigned int CM_GENERATED_CACHE_MAGIC = 0x434d514fU; // "OQMC"
	static const unsigned int CM_GENERATED_CACHE_PAYLOAD_VERSION = 1;
	static const unsigned int CM_GENERATED_CACHE_FLAGS = 0;
	static const int CM_CACHE_MAX_STRING = 4096;
	static const int CM_CACHE_MAX_VERTICES = 1 << 22;
	static const int CM_CACHE_MAX_EDGES = 1 << 23;
	static const int CM_CACHE_MAX_POLYGONS = 1 << 20;
	static const int CM_CACHE_MAX_BRUSHES = 1 << 20;
	static const int CM_CACHE_MAX_NODES = 1 << 20;
	static const int CM_CACHE_MAX_BRUSH_PLANES = 4096;
	static const int CM_CACHE_MAX_REFS = 1 << 24;
	static const unsigned int CM_CACHE_ALLOWED_CONTENTS = 0x0fcfffffU;
	static const float CM_CACHE_MAX_WORLD_COORDINATE = 1000000000.0f;
	static const float CM_CACHE_MAX_TEX_COORDINATE = 1000000000000.0f;
	static const uint64_t CM_CACHE_MAX_TRANSFER_BYTES = 512ULL * 1024ULL * 1024ULL;
	static const uint64_t CM_CACHE_MAX_ALLOCATION_BYTES = 384ULL * 1024ULL * 1024ULL;

	static bool CM_CacheFloatIsFinite( const float value ) {
		return std::isfinite( value );
	}

	static bool CM_CacheVec2IsValid( const idVec2 &value ) {
		return CM_CacheFloatIsFinite( value.x ) && CM_CacheFloatIsFinite( value.y )
			&& std::fabs( value.x ) <= CM_CACHE_MAX_TEX_COORDINATE
			&& std::fabs( value.y ) <= CM_CACHE_MAX_TEX_COORDINATE;
	}

	static bool CM_CacheVec3IsValid( const idVec3 &value ) {
		return CM_CacheFloatIsFinite( value.x ) && CM_CacheFloatIsFinite( value.y )
			&& CM_CacheFloatIsFinite( value.z )
			&& std::fabs( value.x ) <= CM_CACHE_MAX_WORLD_COORDINATE
			&& std::fabs( value.y ) <= CM_CACHE_MAX_WORLD_COORDINATE
			&& std::fabs( value.z ) <= CM_CACHE_MAX_WORLD_COORDINATE;
	}

	static bool CM_CachePlaneIsValid( const idPlane &value ) {
		return CM_CacheVec3IsValid( value.Normal() ) && CM_CacheFloatIsFinite( value.Dist() )
			&& std::fabs( value.Dist() ) <= CM_CACHE_MAX_WORLD_COORDINATE;
	}

	static bool CM_CacheBoundsIsValid( const idBounds &value ) {
		return CM_CacheVec3IsValid( value[0] ) && CM_CacheVec3IsValid( value[1] )
			&& value[0].x <= value[1].x && value[0].y <= value[1].y
			&& value[0].z <= value[1].z;
	}

	static bool CM_CacheStringIsSafe( const char *value, const bool allowEmpty ) {
		if ( value == NULL ) {
			return allowEmpty;
		}
		size_t length = 0;
		while ( length <= static_cast<size_t>( CM_CACHE_MAX_STRING ) && value[length] != '\0' ) {
			++length;
		}
		if ( length > static_cast<size_t>( CM_CACHE_MAX_STRING ) || ( !allowEmpty && length == 0 ) ) {
			return false;
		}
		for ( size_t i = 0; i < length; ++i ) {
			const unsigned char c = static_cast<unsigned char>( value[i] );
			if ( c < 0x20 || c == 0x7f ) {
				return false;
			}
		}
		return true;
	}

	class cmCacheWriter_t {
	public:
		explicit cmCacheWriter_t( idFile &destination ) : file( destination ), transferred( 0 ), valid( true ) {}

		bool WriteBytes( const void *data, const int length ) {
			if ( !valid || length < 0 || ( length > 0 && data == NULL )
				|| static_cast<uint64_t>( length ) > CM_CACHE_MAX_TRANSFER_BYTES - transferred
				|| ( length > 0 && file.Write( data, length ) != length ) ) {
				valid = false;
				return false;
			}
			transferred += static_cast<uint64_t>( length );
			return true;
		}

		bool WriteInt( const int value ) {
			if ( !valid || sizeof( value ) > CM_CACHE_MAX_TRANSFER_BYTES - transferred
				|| file.WriteInt( value ) != sizeof( value ) ) {
				valid = false;
				return false;
			}
			transferred += sizeof( value );
			return true;
		}

		bool WriteUnsignedInt( const unsigned int value ) {
			if ( !valid || sizeof( value ) > CM_CACHE_MAX_TRANSFER_BYTES - transferred
				|| file.WriteUnsignedInt( value ) != sizeof( value ) ) {
				valid = false;
				return false;
			}
			transferred += sizeof( value );
			return true;
		}

		bool WriteBool( const bool value ) {
			const unsigned char encoded = value ? 1 : 0;
			if ( !valid || transferred >= CM_CACHE_MAX_TRANSFER_BYTES
				|| file.WriteUnsignedChar( encoded ) != sizeof( encoded ) ) {
				valid = false;
				return false;
			}
			++transferred;
			return true;
		}

		bool WriteFloat( const float value ) {
			if ( !CM_CacheFloatIsFinite( value ) || !valid
				|| sizeof( value ) > CM_CACHE_MAX_TRANSFER_BYTES - transferred
				|| file.WriteFloat( value ) != sizeof( value ) ) {
				valid = false;
				return false;
			}
			transferred += sizeof( value );
			return true;
		}

		bool WriteVec2( const idVec2 &value ) {
			return WriteFloat( value.x ) && WriteFloat( value.y );
		}

		bool WriteVec3( const idVec3 &value ) {
			return WriteFloat( value.x ) && WriteFloat( value.y ) && WriteFloat( value.z );
		}

		bool WritePlane( const idPlane &value ) {
			return WriteVec3( value.Normal() ) && WriteFloat( value.Dist() );
		}

		bool WriteBounds( const idBounds &value ) {
			return WriteVec3( value[0] ) && WriteVec3( value[1] );
		}

		bool WriteString( const char *value, const bool allowEmpty = false ) {
			if ( !CM_CacheStringIsSafe( value, allowEmpty ) ) {
				valid = false;
				return false;
			}
			const size_t length = strlen( value );
			return WriteInt( static_cast<int>( length ) )
				&& WriteBytes( value, static_cast<int>( length ) );
		}

		bool IsValid() const { return valid; }

	private:
		idFile &file;
		uint64_t transferred;
		bool valid;
	};

	class cmCacheReader_t {
	public:
		explicit cmCacheReader_t( idFile &source ) :
			file( source ), end( source.Length() ), transferred( 0 ), allocated( 0 ), valid( true ) {
			if ( end < 0 || source.Tell() < 0 || source.Tell() > end
				|| static_cast<uint64_t>( end - source.Tell() ) > CM_CACHE_MAX_TRANSFER_BYTES ) {
				valid = false;
			}
		}

		bool ReadBytes( void *data, const int length ) {
			const int position = file.Tell();
			if ( !valid || length < 0 || ( length > 0 && data == NULL ) || position < 0
				|| position > end || length > end - position
				|| static_cast<uint64_t>( length ) > CM_CACHE_MAX_TRANSFER_BYTES - transferred
				|| ( length > 0 && file.Read( data, length ) != length ) ) {
				valid = false;
				return false;
			}
			transferred += static_cast<uint64_t>( length );
			return true;
		}

		bool ReadInt( int &value ) {
			const int position = file.Tell();
			if ( !valid || position < 0 || position > end || sizeof( value ) > static_cast<size_t>( end - position )
				|| sizeof( value ) > CM_CACHE_MAX_TRANSFER_BYTES - transferred
				|| file.ReadInt( value ) != sizeof( value ) ) {
				valid = false;
				return false;
			}
			transferred += sizeof( value );
			return true;
		}

		bool ReadUnsignedInt( unsigned int &value ) {
			const int position = file.Tell();
			if ( !valid || position < 0 || position > end || sizeof( value ) > static_cast<size_t>( end - position )
				|| sizeof( value ) > CM_CACHE_MAX_TRANSFER_BYTES - transferred
				|| file.ReadUnsignedInt( value ) != sizeof( value ) ) {
				valid = false;
				return false;
			}
			transferred += sizeof( value );
			return true;
		}

		bool ReadBool( bool &value ) {
			unsigned char encoded = 0;
			const int position = file.Tell();
			if ( !valid || position < 0 || position >= end || transferred >= CM_CACHE_MAX_TRANSFER_BYTES
				|| file.ReadUnsignedChar( encoded ) != sizeof( encoded ) || encoded > 1 ) {
				valid = false;
				return false;
			}
			++transferred;
			value = encoded != 0;
			return true;
		}

		bool ReadFloat( float &value ) {
			const int position = file.Tell();
			if ( !valid || position < 0 || position > end || sizeof( value ) > static_cast<size_t>( end - position )
				|| sizeof( value ) > CM_CACHE_MAX_TRANSFER_BYTES - transferred
				|| file.ReadFloat( value ) != sizeof( value ) || !CM_CacheFloatIsFinite( value ) ) {
				valid = false;
				return false;
			}
			transferred += sizeof( value );
			return true;
		}

		bool ReadVec2( idVec2 &value ) {
			return ReadFloat( value.x ) && ReadFloat( value.y );
		}

		bool ReadVec3( idVec3 &value ) {
			return ReadFloat( value.x ) && ReadFloat( value.y ) && ReadFloat( value.z );
		}

		bool ReadPlane( idPlane &value ) {
			idVec3 normal;
			float distance = 0.0f;
			if ( !ReadVec3( normal ) || !ReadFloat( distance ) ) {
				return false;
			}
			value.SetNormal( normal );
			value.SetDist( distance );
			return true;
		}

		bool ReadBounds( idBounds &value ) {
			return ReadVec3( value[0] ) && ReadVec3( value[1] ) && CM_CacheBoundsIsValid( value );
		}

		bool Reserve( const int count, const size_t elementSize ) {
			if ( !valid || count < 0 || ( count > 0 && elementSize > CM_CACHE_MAX_ALLOCATION_BYTES / static_cast<uint64_t>( count ) ) ) {
				valid = false;
				return false;
			}
			const uint64_t bytes = static_cast<uint64_t>( count ) * static_cast<uint64_t>( elementSize );
			if ( bytes > CM_CACHE_MAX_ALLOCATION_BYTES - allocated ) {
				valid = false;
				return false;
			}
			allocated += bytes;
			return true;
		}

		bool ReadCount( int &value, const int maximum, const size_t elementSize ) {
			return maximum >= 0 && ReadInt( value ) && value >= 0 && value <= maximum
				&& Reserve( value, elementSize );
		}

		bool ReadString( idStr &value, const bool allowEmpty = false ) {
			int length = 0;
			if ( !ReadCount( length, CM_CACHE_MAX_STRING, sizeof( char ) ) || ( !allowEmpty && length == 0 ) ) {
				valid = false;
				value.Clear();
				return false;
			}
			value.Fill( ' ', length );
			if ( length > 0 && !ReadBytes( &value[0], length ) ) {
				value.Clear();
				return false;
			}
			if ( !CM_CacheStringIsSafe( value.c_str(), allowEmpty ) ) {
				valid = false;
				value.Clear();
				return false;
			}
			return true;
		}

		bool AtEnd() const { return valid && file.Tell() == end; }
		bool IsValid() const { return valid; }

	private:
		idFile &file;
		int end;
		uint64_t transferred;
		uint64_t allocated;
		bool valid;
	};

	struct cmCacheEdgeStage_t {
		int vertexNum[2];
		int internal;
		int numUsers;
		idVec3 normal;
	};

	struct cmCachePolygonStage_t {
		std::vector<int> edges;
		idPlane plane;
		idBounds bounds;
		idStr materialName;
		const idMaterial *material;
		int contents;
		int primitiveNum;
		int featureIndex;
		idVec2 texBounds[3];

		cmCachePolygonStage_t() : material( NULL ), contents( 0 ), primitiveNum( 0 ), featureIndex( 0 ) {}
	};

	struct cmCacheBrushStage_t {
		std::vector<idPlane> planes;
		idBounds bounds;
		idStr materialName;
		const idMaterial *material;
		int contents;
		int primitiveNum;

		cmCacheBrushStage_t() : material( NULL ), contents( 0 ), primitiveNum( 0 ) {}
	};

	struct cmCacheNodeStage_t {
		int planeType;
		float planeDist;
		int children[2];
		std::vector<int> polygons;
		std::vector<int> brushes;

		cmCacheNodeStage_t() : planeType( -1 ), planeDist( 0.0f ) {
			children[0] = children[1] = -1;
		}
	};

	struct cmCacheModelStage_t {
		int sourceSlot;
		idStr name;
		int numPrimitives;
		bool isConvex;
		std::vector<idVec3> vertices;
		std::vector<cmCacheEdgeStage_t> edges;
		std::vector<cmCachePolygonStage_t> polygons;
		std::vector<cmCacheBrushStage_t> brushes;
		std::vector<cmCacheNodeStage_t> nodes;

		cmCacheModelStage_t() : sourceSlot( -1 ), numPrimitives( 0 ), isConvex( false ) {}
	};

	static idStr CM_CacheSourceBase( const char *sourcePath ) {
		idStr base = sourcePath != NULL ? sourcePath : "";
		base.BackSlashesToSlashes();
		base.StripFileExtension();
		return base;
	}

	static bool CM_CacheModelBelongsToSource( const char *modelName, const char *sourcePath ) {
		if ( !CM_CacheStringIsSafe( modelName, false ) ) {
			return false;
		}
		if ( idStr::Cmpn( modelName, PROC_CLIPMODEL_STRING_PRFX,
				static_cast<int>( sizeof( PROC_CLIPMODEL_STRING_PRFX ) - 1 ) ) == 0 ) {
			return true;
		}
		idStr prefix = CM_CacheSourceBase( sourcePath );
		prefix += "/";
		return idStr::IcmpnPath( modelName, prefix.c_str(), prefix.Length() ) == 0;
	}

	static idStr CM_CacheSettings( const unsigned int mapFileCRC, const char *ownerSettings ) {
		return va( "cm-payload=%u;cm-text=%s;geometry=%08x;owner=%s",
			CM_GENERATED_CACHE_PAYLOAD_VERSION, CM_FILEVERSION, mapFileCRC,
			ownerSettings != NULL ? ownerSettings : "" );
	}

	static bool CM_CacheValidateNodeGraph( const std::vector<cmCacheNodeStage_t> &nodes ) {
		if ( nodes.empty() || nodes.size() > static_cast<size_t>( CM_CACHE_MAX_NODES ) ) {
			return false;
		}
		std::vector<int> parents( nodes.size(), 0 );
		for ( size_t i = 0; i < nodes.size(); ++i ) {
			const cmCacheNodeStage_t &node = nodes[i];
			if ( !CM_CacheFloatIsFinite( node.planeDist )
				|| std::fabs( node.planeDist ) > CM_CACHE_MAX_WORLD_COORDINATE ) {
				return false;
			}
			if ( node.planeType == -1 ) {
				if ( node.children[0] != -1 || node.children[1] != -1 ) {
					return false;
				}
				continue;
			}
			if ( node.planeType < 0 || node.planeType > 2 ) {
				return false;
			}
			for ( int side = 0; side < 2; ++side ) {
				const int child = node.children[side];
				if ( child <= 0 || child >= static_cast<int>( nodes.size() ) || child == static_cast<int>( i )
					|| ++parents[child] != 1 ) {
					return false;
				}
			}
		}
		if ( parents[0] != 0 ) {
			return false;
		}
		std::vector<unsigned char> visited( nodes.size(), 0 );
		std::vector<int> pending( 1, 0 );
		for ( size_t cursor = 0; cursor < pending.size(); ++cursor ) {
			const int index = pending[cursor];
			if ( visited[index] != 0 ) {
				return false;
			}
			visited[index] = 1;
			if ( nodes[index].planeType != -1 ) {
				pending.push_back( nodes[index].children[0] );
				pending.push_back( nodes[index].children[1] );
			}
		}
		if ( pending.size() != nodes.size() ) {
			return false;
		}
		for ( size_t i = 1; i < parents.size(); ++i ) {
			if ( parents[i] != 1 ) {
				return false;
			}
		}
		return true;
	}

	struct cmCacheLiveModel_t {
		std::vector<const cm_node_t *> nodes;
		std::vector<const cm_polygon_t *> polygons;
		std::vector<const cm_brush_t *> brushes;
		std::unordered_map<const cm_node_t *, int> nodeIndices;
		std::unordered_map<const cm_polygon_t *, int> polygonIndices;
		std::unordered_map<const cm_brush_t *, int> brushIndices;
		int polygonRefs;
		int brushRefs;

		cmCacheLiveModel_t() : polygonRefs( 0 ), brushRefs( 0 ) {}
	};

	static bool CM_CacheCollectLiveModel( const idCollisionModelLocal *model, cmCacheLiveModel_t &live ) {
		if ( model == NULL || model->node == NULL || model->node->parent != NULL
			|| model->numVertices < 0 || model->numVertices > CM_CACHE_MAX_VERTICES
			|| model->maxVertices < model->numVertices
			|| ( model->numVertices > 0 && model->vertices == NULL )
			|| model->numEdges < 0 || model->numEdges > CM_CACHE_MAX_EDGES
			|| model->maxEdges < model->numEdges
			|| ( model->numEdges > 0 && model->edges == NULL )
			|| model->numPrimitives < 0 || !CM_CacheStringIsSafe( model->name.c_str(), false ) ) {
			return false;
		}

		live.nodeIndices.emplace( model->node, 0 );
		live.nodes.push_back( model->node );
		for ( size_t cursor = 0; cursor < live.nodes.size(); ++cursor ) {
			if ( live.nodes.size() > static_cast<size_t>( CM_CACHE_MAX_NODES ) ) {
				return false;
			}
			const cm_node_t *node = live.nodes[cursor];
			if ( node == NULL || !CM_CacheFloatIsFinite( node->planeDist )
				|| std::fabs( node->planeDist ) > CM_CACHE_MAX_WORLD_COORDINATE ) {
				return false;
			}
			if ( node->planeType == -1 ) {
				if ( node->children[0] != NULL || node->children[1] != NULL ) {
					return false;
				}
			} else {
				if ( node->planeType < 0 || node->planeType > 2 || node->children[0] == NULL
					|| node->children[1] == NULL || node->children[0] == node->children[1] ) {
					return false;
				}
				for ( int side = 0; side < 2; ++side ) {
					const cm_node_t *child = node->children[side];
					if ( child->parent != node || live.nodeIndices.find( child ) != live.nodeIndices.end() ) {
						return false;
					}
					const int index = static_cast<int>( live.nodes.size() );
					live.nodeIndices.emplace( child, index );
					live.nodes.push_back( child );
				}
			}

			std::unordered_set<const cm_polygonRef_t *> polygonRefSet;
			for ( const cm_polygonRef_t *ref = node->polygons; ref != NULL; ref = ref->next ) {
				if ( ref->p == NULL || !polygonRefSet.insert( ref ).second
					|| live.polygonRefs >= CM_CACHE_MAX_REFS ) {
					return false;
				}
				++live.polygonRefs;
				if ( live.polygonIndices.find( ref->p ) == live.polygonIndices.end() ) {
					if ( live.polygons.size() >= static_cast<size_t>( CM_CACHE_MAX_POLYGONS ) ) {
						return false;
					}
					const int index = static_cast<int>( live.polygons.size() );
					live.polygonIndices.emplace( ref->p, index );
					live.polygons.push_back( ref->p );
				}
			}

			std::unordered_set<const cm_brushRef_t *> brushRefSet;
			for ( const cm_brushRef_t *ref = node->brushes; ref != NULL; ref = ref->next ) {
				if ( ref->b == NULL || !brushRefSet.insert( ref ).second
					|| live.brushRefs >= CM_CACHE_MAX_REFS ) {
					return false;
				}
				++live.brushRefs;
				if ( live.brushIndices.find( ref->b ) == live.brushIndices.end() ) {
					if ( live.brushes.size() >= static_cast<size_t>( CM_CACHE_MAX_BRUSHES ) ) {
						return false;
					}
					const int index = static_cast<int>( live.brushes.size() );
					live.brushIndices.emplace( ref->b, index );
					live.brushes.push_back( ref->b );
				}
			}
		}

		if ( model->numNodes != static_cast<int>( live.nodes.size() )
			|| model->numPolygons != static_cast<int>( live.polygons.size() )
			|| model->numBrushes != static_cast<int>( live.brushes.size() )
			|| model->numPolygonRefs != live.polygonRefs || model->numBrushRefs != live.brushRefs ) {
			return false;
		}
		return true;
	}
}

/*
================
idCollisionModelManagerLocal::WriteGeneratedCollisionCache
================
*/
void idCollisionModelManagerLocal::WriteGeneratedCollisionCache( const char *sourcePath,
		unsigned int mapFileCRC, const char *ownerSettings ) {
	if ( sourcePath == NULL || sourcePath[0] == '\0' || models == NULL ) {
		return;
	}

	struct record_t {
		int slot;
		const idCollisionModelLocal *model;
	};
	std::vector<record_t> records;
	for ( int i = 0; i < numModels; ++i ) {
		const idCollisionModelLocal *model = models[i];
		if ( model == NULL || model->fileTime == static_cast<ID_TIME_T>( -1 )
			|| !CM_CacheModelBelongsToSource( model->name.c_str(), sourcePath ) ) {
			continue;
		}
		record_t record;
		record.slot = i;
		record.model = model;
		records.push_back( record );
	}
	if ( records.empty() || records.size() > static_cast<size_t>( MAX_SUBMODELS ) ) {
		return;
	}

	idFile_Memory payload( "generated-collision-cache-payload" );
	cmCacheWriter_t writer( payload );
	if ( !writer.WriteUnsignedInt( CM_GENERATED_CACHE_MAGIC )
		|| !writer.WriteUnsignedInt( CM_GENERATED_CACHE_PAYLOAD_VERSION )
		|| !writer.WriteUnsignedInt( CM_GENERATED_CACHE_FLAGS )
		|| !writer.WriteUnsignedInt( mapFileCRC )
		|| !writer.WriteInt( static_cast<int>( records.size() ) ) ) {
		return;
	}

	for ( size_t recordIndex = 0; recordIndex < records.size(); ++recordIndex ) {
		const idCollisionModelLocal *model = records[recordIndex].model;
		cmCacheLiveModel_t live;
		if ( !CM_CacheCollectLiveModel( model, live )
			|| !writer.WriteInt( records[recordIndex].slot )
			|| !writer.WriteString( model->name.c_str() )
			|| !writer.WriteInt( model->numPrimitives )
			|| !writer.WriteBool( model->isConvex )
			|| !writer.WriteInt( model->numVertices ) ) {
			return;
		}

		for ( int i = 0; i < model->numVertices; ++i ) {
			if ( !CM_CacheVec3IsValid( model->vertices[i].p ) || !writer.WriteVec3( model->vertices[i].p ) ) {
				return;
			}
		}

		if ( !writer.WriteInt( model->numEdges ) ) {
			return;
		}
		for ( int i = 0; i < model->numEdges; ++i ) {
			const cm_edge_t &edge = model->edges[i];
			if ( edge.vertexNum[0] < 0 || edge.vertexNum[0] >= model->numVertices
				|| edge.vertexNum[1] < 0 || edge.vertexNum[1] >= model->numVertices
				|| !CM_CacheVec3IsValid( edge.normal )
				|| !writer.WriteInt( edge.vertexNum[0] ) || !writer.WriteInt( edge.vertexNum[1] )
				|| !writer.WriteInt( edge.internal ) || !writer.WriteInt( edge.numUsers )
				|| !writer.WriteVec3( edge.normal ) ) {
				return;
			}
		}

		if ( !writer.WriteInt( static_cast<int>( live.polygons.size() ) ) ) {
			return;
		}
		std::unordered_set<int> featureIndices;
		for ( size_t i = 0; i < live.polygons.size(); ++i ) {
			const cm_polygon_t *polygon = live.polygons[i];
			if ( polygon == NULL || polygon->material == NULL || polygon->numEdges < 3
				|| polygon->numEdges > CM_MAX_POLYGON_EDGES
				|| !CM_CachePlaneIsValid( polygon->plane ) || !CM_CacheBoundsIsValid( polygon->bounds )
				|| !CM_CacheStringIsSafe( polygon->material->GetName(), false )
				|| polygon->featureIndex <= 0 || !featureIndices.insert( polygon->featureIndex ).second
				|| !writer.WriteInt( polygon->numEdges ) ) {
				return;
			}
			for ( int edgeIndex = 0; edgeIndex < polygon->numEdges; ++edgeIndex ) {
				const int encodedEdge = polygon->edges[edgeIndex];
				if ( encodedEdge == ( std::numeric_limits<int>::min )()
					|| std::abs( encodedEdge ) >= model->numEdges || !writer.WriteInt( encodedEdge ) ) {
					return;
				}
			}
			if ( !writer.WritePlane( polygon->plane ) || !writer.WriteBounds( polygon->bounds )
				|| !writer.WriteString( polygon->material->GetName() )
				|| !writer.WriteInt( polygon->contents ) || !writer.WriteInt( polygon->primitiveNum )
				|| !writer.WriteInt( polygon->featureIndex ) ) {
				return;
			}
			for ( int texBound = 0; texBound < 3; ++texBound ) {
				if ( !CM_CacheVec2IsValid( polygon->texBounds[texBound] )
					|| !writer.WriteVec2( polygon->texBounds[texBound] ) ) {
					return;
				}
			}
		}

		if ( !writer.WriteInt( static_cast<int>( live.brushes.size() ) ) ) {
			return;
		}
		for ( size_t i = 0; i < live.brushes.size(); ++i ) {
			const cm_brush_t *brush = live.brushes[i];
			const char *materialName = brush != NULL && brush->material != NULL ? brush->material->GetName() : "";
			if ( brush == NULL || brush->numPlanes <= 0 || brush->numPlanes > CM_CACHE_MAX_BRUSH_PLANES
				|| brush->contents == 0
				|| ( static_cast<unsigned int>( brush->contents ) & ~CM_CACHE_ALLOWED_CONTENTS ) != 0
				|| !CM_CacheBoundsIsValid( brush->bounds )
				|| !CM_CacheStringIsSafe( materialName, true )
				|| !writer.WriteInt( brush->numPlanes ) ) {
				return;
			}
			for ( int planeIndex = 0; planeIndex < brush->numPlanes; ++planeIndex ) {
				if ( !CM_CachePlaneIsValid( brush->planes[planeIndex] )
					|| !writer.WritePlane( brush->planes[planeIndex] ) ) {
					return;
				}
			}
			if ( !writer.WriteBounds( brush->bounds ) || !writer.WriteInt( brush->contents )
				|| !writer.WriteString( materialName, true ) || !writer.WriteInt( brush->primitiveNum ) ) {
				return;
			}
		}

		if ( !writer.WriteInt( static_cast<int>( live.nodes.size() ) ) ) {
			return;
		}
		for ( size_t i = 0; i < live.nodes.size(); ++i ) {
			const cm_node_t *node = live.nodes[i];
			const int child0 = node->planeType == -1 ? -1 : live.nodeIndices[node->children[0]];
			const int child1 = node->planeType == -1 ? -1 : live.nodeIndices[node->children[1]];
			if ( !writer.WriteInt( node->planeType ) || !writer.WriteFloat( node->planeDist )
				|| !writer.WriteInt( child0 ) || !writer.WriteInt( child1 ) ) {
				return;
			}

			int polygonRefCount = 0;
			for ( const cm_polygonRef_t *ref = node->polygons; ref != NULL; ref = ref->next ) {
				++polygonRefCount;
			}
			if ( !writer.WriteInt( polygonRefCount ) ) {
				return;
			}
			for ( const cm_polygonRef_t *ref = node->polygons; ref != NULL; ref = ref->next ) {
				const std::unordered_map<const cm_polygon_t *, int>::const_iterator found = live.polygonIndices.find( ref->p );
				if ( found == live.polygonIndices.end() || !writer.WriteInt( found->second ) ) {
					return;
				}
			}

			int brushRefCount = 0;
			for ( const cm_brushRef_t *ref = node->brushes; ref != NULL; ref = ref->next ) {
				++brushRefCount;
			}
			if ( !writer.WriteInt( brushRefCount ) ) {
				return;
			}
			for ( const cm_brushRef_t *ref = node->brushes; ref != NULL; ref = ref->next ) {
				const std::unordered_map<const cm_brush_t *, int>::const_iterator found = live.brushIndices.find( ref->b );
				if ( found == live.brushIndices.end() || !writer.WriteInt( found->second ) ) {
					return;
				}
			}
		}
	}

	if ( !writer.IsValid() || payload.Length() <= 0
		|| static_cast<uint64_t>( payload.Length() ) > CM_CACHE_MAX_TRANSFER_BYTES ) {
		return;
	}
	const idStr settings = CM_CacheSettings( mapFileCRC, ownerSettings );
	fileSystem->WriteGeneratedCache( GENERATED_CACHE_COLLISION_MODEL, sourcePath,
		CM_GENERATED_CACHE_PAYLOAD_VERSION, settings.c_str(), payload.GetDataPtr(),
		static_cast<unsigned int>( payload.Length() ) );
}

namespace {
	static bool CM_CacheProcClipOrdinal( const char *name, int &ordinal ) {
		ordinal = -1;
		if ( name == NULL ) {
			return false;
		}
		const int prefixLength = static_cast<int>( sizeof( PROC_CLIPMODEL_STRING_PRFX ) - 1 );
		if ( idStr::Cmpn( name, PROC_CLIPMODEL_STRING_PRFX, prefixLength ) != 0 ) {
			return false;
		}
		const char *digits = name + prefixLength;
		if ( digits[0] == '\0' ) {
			return false;
		}
		uint64_t value = 0;
		for ( const char *cursor = digits; cursor[0] != '\0'; ++cursor ) {
			if ( cursor[0] < '0' || cursor[0] > '9' ) {
				return false;
			}
			value = value * 10 + static_cast<unsigned int>( cursor[0] - '0' );
			if ( value >= static_cast<uint64_t>( MAX_SUBMODELS ) ) {
				return false;
			}
		}
		ordinal = static_cast<int>( value );
		return idStr::Cmp( name, va( "%s%d", PROC_CLIPMODEL_STRING_PRFX, ordinal ) ) == 0;
	}

	static bool CM_CacheModelNameMatches( const char *lhs, const char *rhs ) {
		return lhs != NULL && rhs != NULL && idStr::IcmpPath( lhs, rhs ) == 0;
	}
}

/*
================
idCollisionModelManagerLocal::LoadGeneratedCollisionCache
================
*/
bool idCollisionModelManagerLocal::LoadGeneratedCollisionCache( const char *sourcePath,
		unsigned int mapFileCRC, const char *ownerSettings ) {
	if ( sourcePath == NULL || sourcePath[0] == '\0' ) {
		return false;
	}
	const idStr settings = CM_CacheSettings( mapFileCRC, ownerSettings );
	idFile *cacheFile = fileSystem->OpenGeneratedCacheRead( GENERATED_CACHE_COLLISION_MODEL,
		sourcePath, CM_GENERATED_CACHE_PAYLOAD_VERSION, settings.c_str() );
	if ( cacheFile == NULL ) {
		return false;
	}

	cmCacheReader_t reader( *cacheFile );
	unsigned int magic = 0;
	unsigned int version = 0;
	unsigned int flags = 0;
	unsigned int cachedCRC = 0;
	int modelCount = 0;
	bool valid = reader.ReadUnsignedInt( magic ) && magic == CM_GENERATED_CACHE_MAGIC
		&& reader.ReadUnsignedInt( version ) && version == CM_GENERATED_CACHE_PAYLOAD_VERSION
		&& reader.ReadUnsignedInt( flags ) && flags == CM_GENERATED_CACHE_FLAGS
		&& reader.ReadUnsignedInt( cachedCRC ) && cachedCRC == mapFileCRC
		&& reader.ReadCount( modelCount, MAX_SUBMODELS, sizeof( cmCacheModelStage_t ) )
		&& modelCount > 0;

	std::vector<cmCacheModelStage_t> stagedModels;
	if ( valid ) {
		stagedModels.resize( modelCount );
	}
	std::unordered_set<int> sourceSlots;
	bool foundWorldModel = false;
	const idStr worldModelName = CM_CacheSourceBase( sourcePath ) + "/" WORLD_MODEL_NAME;

	for ( int modelIndex = 0; valid && modelIndex < modelCount; ++modelIndex ) {
		cmCacheModelStage_t &model = stagedModels[modelIndex];
		int vertexCount = 0;
		int edgeCount = 0;
		int polygonCount = 0;
		int brushCount = 0;
		int nodeCount = 0;
		valid = reader.ReadInt( model.sourceSlot ) && model.sourceSlot >= 0
			&& model.sourceSlot < MAX_SUBMODELS && sourceSlots.insert( model.sourceSlot ).second
			&& reader.ReadString( model.name )
			&& CM_CacheModelBelongsToSource( model.name.c_str(), sourcePath )
			&& reader.ReadInt( model.numPrimitives ) && model.numPrimitives >= 0
			&& model.numPrimitives <= CM_CACHE_MAX_REFS
			&& reader.ReadBool( model.isConvex )
			&& reader.ReadCount( vertexCount, CM_CACHE_MAX_VERTICES, sizeof( idVec3 ) );
		if ( !valid ) {
			break;
		}
		if ( CM_CacheModelNameMatches( model.name.c_str(), worldModelName.c_str() ) ) {
			if ( foundWorldModel ) {
				valid = false;
				break;
			}
			foundWorldModel = true;
		}

		model.vertices.resize( vertexCount );
		for ( int i = 0; valid && i < vertexCount; ++i ) {
			valid = reader.ReadVec3( model.vertices[i] ) && CM_CacheVec3IsValid( model.vertices[i] );
		}

		valid = valid && reader.ReadCount( edgeCount, CM_CACHE_MAX_EDGES, sizeof( cmCacheEdgeStage_t ) );
		if ( !valid ) {
			break;
		}
		model.edges.resize( edgeCount );
		for ( int i = 0; valid && i < edgeCount; ++i ) {
			cmCacheEdgeStage_t &edge = model.edges[i];
			valid = reader.ReadInt( edge.vertexNum[0] ) && reader.ReadInt( edge.vertexNum[1] )
				&& edge.vertexNum[0] >= 0 && edge.vertexNum[0] < vertexCount
				&& edge.vertexNum[1] >= 0 && edge.vertexNum[1] < vertexCount
				&& reader.ReadInt( edge.internal ) && edge.internal >= 0
				&& edge.internal <= ( std::numeric_limits<unsigned short>::max )()
				&& reader.ReadInt( edge.numUsers ) && edge.numUsers >= 0
				&& edge.numUsers <= ( std::numeric_limits<unsigned short>::max )()
				&& reader.ReadVec3( edge.normal ) && CM_CacheVec3IsValid( edge.normal );
		}

		valid = valid && reader.ReadCount( polygonCount, CM_CACHE_MAX_POLYGONS, sizeof( cmCachePolygonStage_t ) );
		if ( !valid ) {
			break;
		}
		model.polygons.resize( polygonCount );
		std::unordered_set<int> featureIndices;
		for ( int i = 0; valid && i < polygonCount; ++i ) {
			cmCachePolygonStage_t &polygon = model.polygons[i];
			int polygonEdgeCount = 0;
			valid = reader.ReadCount( polygonEdgeCount, CM_MAX_POLYGON_EDGES, sizeof( int ) )
				&& polygonEdgeCount >= 3;
			if ( !valid ) {
				break;
			}
			polygon.edges.resize( polygonEdgeCount );
			for ( int edgeIndex = 0; valid && edgeIndex < polygonEdgeCount; ++edgeIndex ) {
				valid = reader.ReadInt( polygon.edges[edgeIndex] )
					&& polygon.edges[edgeIndex] != ( std::numeric_limits<int>::min )()
					&& std::abs( polygon.edges[edgeIndex] ) < edgeCount;
			}
			valid = valid && reader.ReadPlane( polygon.plane ) && CM_CachePlaneIsValid( polygon.plane )
				&& reader.ReadBounds( polygon.bounds )
				&& reader.ReadString( polygon.materialName )
				&& reader.ReadInt( polygon.contents )
				&& reader.ReadInt( polygon.primitiveNum ) && polygon.primitiveNum >= 0
				&& ( model.numPrimitives == 0 || polygon.primitiveNum < model.numPrimitives )
				&& reader.ReadInt( polygon.featureIndex ) && polygon.featureIndex > 0
				&& featureIndices.insert( polygon.featureIndex ).second;
			for ( int texBound = 0; valid && texBound < 3; ++texBound ) {
				valid = reader.ReadVec2( polygon.texBounds[texBound] )
					&& CM_CacheVec2IsValid( polygon.texBounds[texBound] );
			}
		}

		valid = valid && reader.ReadCount( brushCount, CM_CACHE_MAX_BRUSHES, sizeof( cmCacheBrushStage_t ) );
		if ( !valid ) {
			break;
		}
		model.brushes.resize( brushCount );
		for ( int i = 0; valid && i < brushCount; ++i ) {
			cmCacheBrushStage_t &brush = model.brushes[i];
			int planeCount = 0;
			valid = reader.ReadCount( planeCount, CM_CACHE_MAX_BRUSH_PLANES, sizeof( idPlane ) )
				&& planeCount > 0;
			if ( !valid ) {
				break;
			}
			brush.planes.resize( planeCount );
			for ( int planeIndex = 0; valid && planeIndex < planeCount; ++planeIndex ) {
				valid = reader.ReadPlane( brush.planes[planeIndex] )
					&& CM_CachePlaneIsValid( brush.planes[planeIndex] );
			}
			valid = valid && reader.ReadBounds( brush.bounds )
				&& reader.ReadInt( brush.contents ) && brush.contents != 0
				&& ( static_cast<unsigned int>( brush.contents ) & ~CM_CACHE_ALLOWED_CONTENTS ) == 0
				&& reader.ReadString( brush.materialName, true )
				&& reader.ReadInt( brush.primitiveNum ) && brush.primitiveNum >= 0
				&& ( model.numPrimitives == 0 || brush.primitiveNum < model.numPrimitives );
		}
		if ( valid && model.numPrimitives == 0 && ( polygonCount > 0 || brushCount > 0 ) ) {
			valid = false;
		}

		valid = valid && reader.ReadCount( nodeCount, CM_CACHE_MAX_NODES, sizeof( cmCacheNodeStage_t ) )
			&& nodeCount > 0;
		if ( !valid ) {
			break;
		}
		model.nodes.resize( nodeCount );
		std::vector<int> polygonUses( polygonCount, 0 );
		std::vector<int> brushUses( brushCount, 0 );
		int totalPolygonRefs = 0;
		int totalBrushRefs = 0;
		for ( int i = 0; valid && i < nodeCount; ++i ) {
			cmCacheNodeStage_t &node = model.nodes[i];
			int polygonRefCount = 0;
			int brushRefCount = 0;
			valid = reader.ReadInt( node.planeType ) && reader.ReadFloat( node.planeDist )
				&& reader.ReadInt( node.children[0] ) && reader.ReadInt( node.children[1] )
				&& reader.ReadCount( polygonRefCount, CM_CACHE_MAX_REFS - totalPolygonRefs, sizeof( int ) );
			if ( !valid ) {
				break;
			}
			totalPolygonRefs += polygonRefCount;
			node.polygons.resize( polygonRefCount );
			std::unordered_set<int> localPolygons;
			for ( int refIndex = 0; valid && refIndex < polygonRefCount; ++refIndex ) {
				valid = reader.ReadInt( node.polygons[refIndex] )
					&& node.polygons[refIndex] >= 0 && node.polygons[refIndex] < polygonCount
					&& localPolygons.insert( node.polygons[refIndex] ).second;
				if ( valid ) {
					++polygonUses[node.polygons[refIndex]];
				}
			}
			valid = valid && reader.ReadCount( brushRefCount, CM_CACHE_MAX_REFS - totalBrushRefs, sizeof( int ) );
			if ( !valid ) {
				break;
			}
			totalBrushRefs += brushRefCount;
			node.brushes.resize( brushRefCount );
			std::unordered_set<int> localBrushes;
			for ( int refIndex = 0; valid && refIndex < brushRefCount; ++refIndex ) {
				valid = reader.ReadInt( node.brushes[refIndex] )
					&& node.brushes[refIndex] >= 0 && node.brushes[refIndex] < brushCount
					&& localBrushes.insert( node.brushes[refIndex] ).second;
				if ( valid ) {
					++brushUses[node.brushes[refIndex]];
				}
			}
		}
		valid = valid && CM_CacheValidateNodeGraph( model.nodes );
		for ( int i = 0; valid && i < polygonCount; ++i ) {
			valid = polygonUses[i] > 0;
		}
		for ( int i = 0; valid && i < brushCount; ++i ) {
			valid = brushUses[i] > 0;
		}
	}

	valid = valid && foundWorldModel && reader.AtEnd();
	fileSystem->CloseFile( cacheFile );
	if ( !valid ) {
		fileSystem->DiscardGeneratedCache( GENERATED_CACHE_COLLISION_MODEL, sourcePath,
			CM_GENERATED_CACHE_PAYLOAD_VERSION, settings.c_str() );
		common->Warning( "Discarded malformed generated collision cache for %s", sourcePath );
		return false;
	}

	// Resolve declarations only after the complete pointer-free structure has
	// passed its bounds and graph checks.  Missing or changed declarations make
	// the payload non-adoptable and fall back to the authoritative source.
	int procClipCount = 0;
	std::vector<int> procClipModelForOrdinal( MAX_SUBMODELS, -1 );
	for ( int modelIndex = 0; valid && modelIndex < modelCount; ++modelIndex ) {
		cmCacheModelStage_t &model = stagedModels[modelIndex];
		int ordinal = -1;
		if ( CM_CacheProcClipOrdinal( model.name.c_str(), ordinal ) ) {
			if ( ordinal < 0 || ordinal >= MAX_SUBMODELS
				|| model.sourceSlot != PROC_CLIPMODEL_INDEX_START + ordinal
				|| procClipModelForOrdinal[ordinal] != -1 ) {
				valid = false;
				break;
			}
			procClipModelForOrdinal[ordinal] = modelIndex;
			++procClipCount;
		}
		for ( size_t i = 0; valid && i < model.polygons.size(); ++i ) {
			cmCachePolygonStage_t &polygon = model.polygons[i];
			polygon.material = declManager->FindMaterial( polygon.materialName.c_str() );
			valid = polygon.material != NULL
				&& idStr::IcmpPath( polygon.material->GetName(), polygon.materialName.c_str() ) == 0
				&& polygon.material->GetContentFlags() == polygon.contents;
		}
		for ( size_t i = 0; valid && i < model.brushes.size(); ++i ) {
			cmCacheBrushStage_t &brush = model.brushes[i];
			if ( brush.materialName.IsEmpty() ) {
				brush.material = NULL;
			} else {
				brush.material = declManager->FindMaterial( brush.materialName.c_str() );
				valid = brush.material != NULL
					&& idStr::IcmpPath( brush.material->GetName(), brush.materialName.c_str() ) == 0;
			}
		}
	}
	for ( int ordinal = 0; valid && ordinal < procClipCount; ++ordinal ) {
		valid = procClipModelForOrdinal[ordinal] >= 0;
	}
	if ( !valid ) {
		fileSystem->DiscardGeneratedCache( GENERATED_CACHE_COLLISION_MODEL, sourcePath,
			CM_GENERATED_CACHE_PAYLOAD_VERSION, settings.c_str() );
		common->Warning( "Discarded generated collision cache with invalid declarations for %s", sourcePath );
		return false;
	}

	// Construct a completely detached model set.  All allocations and pointer
	// wiring finish before the live table is touched.
	std::vector<idCollisionModelLocal *> builtModels( modelCount, NULL );
	for ( int modelIndex = 0; modelIndex < modelCount; ++modelIndex ) {
		const cmCacheModelStage_t &stage = stagedModels[modelIndex];
		idCollisionModelLocal *model = AllocModel();
		builtModels[modelIndex] = model;
		model->name = stage.name;
		model->fileTime = mapFileCRC;
		model->numPrimitives = stage.numPrimitives;
		model->isConvex = stage.isConvex;
		model->maxVertices = model->numVertices = static_cast<int>( stage.vertices.size() );
		if ( model->numVertices > 0 ) {
			model->vertices = static_cast<cm_vertex_t *>( Mem_ClearedAlloc(
				static_cast<size_t>( model->numVertices ) * sizeof( cm_vertex_t ) ) );
			for ( int i = 0; i < model->numVertices; ++i ) {
				model->vertices[i].p = stage.vertices[i];
			}
		}

		model->maxEdges = model->numEdges = static_cast<int>( stage.edges.size() );
		if ( model->numEdges > 0 ) {
			model->edges = static_cast<cm_edge_t *>( Mem_ClearedAlloc(
				static_cast<size_t>( model->numEdges ) * sizeof( cm_edge_t ) ) );
			for ( int i = 0; i < model->numEdges; ++i ) {
				model->edges[i].vertexNum[0] = stage.edges[i].vertexNum[0];
				model->edges[i].vertexNum[1] = stage.edges[i].vertexNum[1];
				model->edges[i].internal = static_cast<unsigned short>( stage.edges[i].internal );
				model->edges[i].numUsers = static_cast<unsigned short>( stage.edges[i].numUsers );
				model->edges[i].normal = stage.edges[i].normal;
				model->numInternalEdges += stage.edges[i].internal;
			}
		}

		std::vector<cm_polygon_t *> polygons( stage.polygons.size(), NULL );
		for ( size_t i = 0; i < stage.polygons.size(); ++i ) {
			const cmCachePolygonStage_t &source = stage.polygons[i];
			cm_polygon_t *polygon = AllocPolygon( model, static_cast<int>( source.edges.size() ) );
			polygons[i] = polygon;
			polygon->numEdges = static_cast<int>( source.edges.size() );
			for ( int edgeIndex = 0; edgeIndex < polygon->numEdges; ++edgeIndex ) {
				polygon->edges[edgeIndex] = source.edges[edgeIndex];
			}
			polygon->plane = source.plane;
			polygon->bounds = source.bounds;
			polygon->material = source.material;
			polygon->contents = source.contents;
			polygon->primitiveNum = source.primitiveNum;
			polygon->featureIndex = source.featureIndex;
			for ( int texBound = 0; texBound < 3; ++texBound ) {
				polygon->texBounds[texBound] = source.texBounds[texBound];
			}
		}

		std::vector<cm_brush_t *> brushes( stage.brushes.size(), NULL );
		for ( size_t i = 0; i < stage.brushes.size(); ++i ) {
			const cmCacheBrushStage_t &source = stage.brushes[i];
			cm_brush_t *brush = AllocBrush( model, static_cast<int>( source.planes.size() ) );
			brushes[i] = brush;
			brush->numPlanes = static_cast<int>( source.planes.size() );
			for ( int planeIndex = 0; planeIndex < brush->numPlanes; ++planeIndex ) {
				brush->planes[planeIndex] = source.planes[planeIndex];
			}
			brush->bounds = source.bounds;
			brush->contents = source.contents;
			brush->material = source.material;
			brush->primitiveNum = source.primitiveNum;
			brush->checkcount = 0;
		}

		std::vector<cm_node_t *> nodes( stage.nodes.size(), NULL );
		for ( size_t i = 0; i < stage.nodes.size(); ++i ) {
			nodes[i] = AllocNode( model, model->numNodes < NODE_BLOCK_SIZE_SMALL
				? NODE_BLOCK_SIZE_SMALL : NODE_BLOCK_SIZE_LARGE );
			++model->numNodes;
			nodes[i]->planeType = stage.nodes[i].planeType;
			nodes[i]->planeDist = stage.nodes[i].planeDist;
		}
		for ( size_t i = 0; i < stage.nodes.size(); ++i ) {
			if ( stage.nodes[i].planeType == -1 ) {
				continue;
			}
			for ( int side = 0; side < 2; ++side ) {
				nodes[i]->children[side] = nodes[stage.nodes[i].children[side]];
				nodes[stage.nodes[i].children[side]]->parent = nodes[i];
			}
		}
		model->node = nodes[0];
		for ( size_t i = 0; i < stage.nodes.size(); ++i ) {
			for ( int refIndex = static_cast<int>( stage.nodes[i].polygons.size() ) - 1; refIndex >= 0; --refIndex ) {
				AddPolygonToNode( model, nodes[i], polygons[stage.nodes[i].polygons[refIndex]] );
			}
			for ( int refIndex = static_cast<int>( stage.nodes[i].brushes.size() ) - 1; refIndex >= 0; --refIndex ) {
				AddBrushToNode( model, nodes[i], brushes[stage.nodes[i].brushes[refIndex]] );
			}
		}

		CM_GetNodeBounds( &model->bounds, model->node );
		model->contents = CM_GetNodeContents( model->node );
		model->usedMemory = model->numVertices * sizeof( cm_vertex_t )
			+ model->numEdges * sizeof( cm_edge_t ) + model->polygonMemory + model->brushMemory
			+ model->numNodes * sizeof( cm_node_t )
			+ model->numPolygonRefs * sizeof( cm_polygonRef_t )
			+ model->numBrushRefs * sizeof( cm_brushRef_t );
	}

	// Cache slots are treated as hints, but adoption cannot evict an in-use
	// collision model.  Reject before any mutation if the learned layout is no
	// longer compatible with the live table.
	EnsureModelTable();
	for ( int i = 0; valid && i < numModels; ++i ) {
		const idCollisionModelLocal *existing = models[i];
		if ( existing == NULL ) {
			continue;
		}
		bool replace = existing->name.Cmpn( PROC_CLIPMODEL_STRING_PRFX,
			static_cast<int>( sizeof( PROC_CLIPMODEL_STRING_PRFX ) - 1 ) ) == 0;
		for ( int modelIndex = 0; !replace && modelIndex < modelCount; ++modelIndex ) {
			replace = CM_CacheModelNameMatches( existing->name.c_str(), stagedModels[modelIndex].name.c_str() )
				|| stagedModels[modelIndex].sourceSlot == i;
		}
		if ( replace ) {
			valid = existing->refCount <= 0 && !existing->isTrmModel;
		}
	}
	if ( !valid ) {
		for ( size_t i = 0; i < builtModels.size(); ++i ) {
			DestroyModel( builtModels[i] );
		}
		return false;
	}

	// Remove stale proc-clip entries and duplicate names before installing the
	// detached set.  No validation or allocation remains after this point.
	for ( int i = 0; i < numModels; ++i ) {
		idCollisionModelLocal *existing = models[i];
		if ( existing == NULL ) {
			continue;
		}
		bool replace = existing->name.Cmpn( PROC_CLIPMODEL_STRING_PRFX,
			static_cast<int>( sizeof( PROC_CLIPMODEL_STRING_PRFX ) - 1 ) ) == 0;
		for ( int modelIndex = 0; !replace && modelIndex < modelCount; ++modelIndex ) {
			replace = CM_CacheModelNameMatches( existing->name.c_str(), stagedModels[modelIndex].name.c_str() )
				|| stagedModels[modelIndex].sourceSlot == i;
		}
		if ( replace ) {
			DestroyModel( existing );
			models[i] = NULL;
		}
	}
	int highestSlot = numModels - 1;
	for ( int modelIndex = 0; modelIndex < modelCount; ++modelIndex ) {
		const int slot = stagedModels[modelIndex].sourceSlot;
		models[slot] = builtModels[modelIndex];
		builtModels[modelIndex] = NULL;
		highestSlot = Max( highestSlot, slot );
	}
	numModels = highestSlot + 1;
	numInlinedProcClipModels = procClipCount;
	return true;
}
