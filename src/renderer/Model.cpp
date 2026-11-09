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
#include "Model_ase.h"
#include "Model_lwo.h"
#include "Model_ma.h"

#include <cmath>

idCVar idRenderModelStatic::r_mergeModelSurfaces( "r_mergeModelSurfaces", "1", CVAR_BOOL|CVAR_RENDERER, "combine model surfaces with the same material" );
idCVar idRenderModelStatic::r_slopVertex( "r_slopVertex", "0.01", CVAR_RENDERER, "merge xyz coordinates this far apart" );
idCVar idRenderModelStatic::r_slopTexCoord( "r_slopTexCoord", "0.001", CVAR_RENDERER, "merge texture coordinates this far apart" );
idCVar idRenderModelStatic::r_slopNormal( "r_slopNormal", "0.02", CVAR_RENDERER, "merge normals that dot less than this" );

namespace {
	static const uint64_t RENDER_MODEL_CACHE_MAX_TRANSFER_BYTES = 512ULL * 1024ULL * 1024ULL;
	static const uint64_t RENDER_MODEL_CACHE_MAX_ALLOCATION_BYTES = 512ULL * 1024ULL * 1024ULL;
}

bool R_RenderModelCacheFloatIsFinite( float value ) {
	return std::isfinite( value );
}

idRenderModelCacheReader::idRenderModelCacheReader( idFile &source ) :
	file( source ),
	transferBytes( 0 ),
	allocationBytes( 0 ),
	valid( true ) {
}

bool idRenderModelCacheReader::ReadBytes( void *data, int length ) {
	if ( !valid || length < 0 || ( length > 0 && data == NULL )
		|| static_cast<uint64_t>( length ) > RENDER_MODEL_CACHE_MAX_TRANSFER_BYTES - transferBytes ) {
		valid = false;
		return false;
	}
	if ( length > 0 && file.Read( data, length ) != length ) {
		valid = false;
		return false;
	}
	transferBytes += static_cast<uint64_t>( length );
	return true;
}

bool idRenderModelCacheReader::ReadInt( int &value ) {
	if ( !valid || sizeof( value ) > RENDER_MODEL_CACHE_MAX_TRANSFER_BYTES - transferBytes
		|| file.ReadInt( value ) != sizeof( value ) ) {
		valid = false;
		return false;
	}
	transferBytes += sizeof( value );
	return true;
}

bool idRenderModelCacheReader::ReadUnsignedInt( unsigned int &value ) {
	if ( !valid || sizeof( value ) > RENDER_MODEL_CACHE_MAX_TRANSFER_BYTES - transferBytes
		|| file.ReadUnsignedInt( value ) != sizeof( value ) ) {
		valid = false;
		return false;
	}
	transferBytes += sizeof( value );
	return true;
}

bool idRenderModelCacheReader::ReadUnsigned64( uint64_t &value ) {
	unsigned int low = 0;
	unsigned int high = 0;
	if ( !ReadUnsignedInt( low ) || !ReadUnsignedInt( high ) ) {
		return false;
	}
	value = static_cast<uint64_t>( low ) | ( static_cast<uint64_t>( high ) << 32 );
	return true;
}

bool idRenderModelCacheReader::ReadByte( byte &value ) {
	if ( !valid || transferBytes >= RENDER_MODEL_CACHE_MAX_TRANSFER_BYTES
		|| file.ReadUnsignedChar( value ) != sizeof( value ) ) {
		valid = false;
		return false;
	}
	++transferBytes;
	return true;
}

bool idRenderModelCacheReader::ReadBool( bool &value ) {
	byte encoded = 0;
	if ( !ReadByte( encoded ) || encoded > 1 ) {
		valid = false;
		return false;
	}
	value = ( encoded != 0 );
	return true;
}

bool idRenderModelCacheReader::ReadFloat( float &value ) {
	if ( !valid || sizeof( value ) > RENDER_MODEL_CACHE_MAX_TRANSFER_BYTES - transferBytes
		|| file.ReadFloat( value ) != sizeof( value ) || !R_RenderModelCacheFloatIsFinite( value ) ) {
		valid = false;
		return false;
	}
	transferBytes += sizeof( value );
	return true;
}

bool idRenderModelCacheReader::ReadVec2( idVec2 &value ) {
	return ReadFloat( value.x ) && ReadFloat( value.y );
}

bool idRenderModelCacheReader::ReadVec3( idVec3 &value ) {
	return ReadFloat( value.x ) && ReadFloat( value.y ) && ReadFloat( value.z );
}

bool idRenderModelCacheReader::ReadVec4( idVec4 &value ) {
	return ReadFloat( value.x ) && ReadFloat( value.y ) && ReadFloat( value.z ) && ReadFloat( value.w );
}

bool idRenderModelCacheReader::ReadFloatArray( float *values, int count ) {
	if ( count < 0 || ( count > 0 && values == NULL )
		|| static_cast<uint64_t>( count ) > RENDER_MODEL_CACHE_MAX_TRANSFER_BYTES / sizeof( float ) ) {
		valid = false;
		return false;
	}
	if ( !ReadBytes( values, count * static_cast<int>( sizeof( float ) ) ) ) {
		return false;
	}
	for ( int i = 0; i < count; ++i ) {
		values[i] = LittleFloat( values[i] );
		if ( !R_RenderModelCacheFloatIsFinite( values[i] ) ) {
			valid = false;
			return false;
		}
	}
	return true;
}

bool idRenderModelCacheReader::ReadIntArray( int *values, int count ) {
	if ( count < 0 || ( count > 0 && values == NULL )
		|| static_cast<uint64_t>( count ) > RENDER_MODEL_CACHE_MAX_TRANSFER_BYTES / sizeof( int ) ) {
		valid = false;
		return false;
	}
	if ( !ReadBytes( values, count * static_cast<int>( sizeof( int ) ) ) ) {
		return false;
	}
	for ( int i = 0; i < count; ++i ) {
		values[i] = LittleLong( values[i] );
	}
	return true;
}

bool idRenderModelCacheReader::ReadBounds( idBounds &value, bool allowCleared ) {
	if ( !ReadVec3( value[0] ) || !ReadVec3( value[1] ) ) {
		return false;
	}
	if ( !allowCleared || !value.IsCleared() ) {
		for ( int axis = 0; axis < 3; ++axis ) {
			if ( value[0][axis] > value[1][axis] ) {
				valid = false;
				return false;
			}
		}
	}
	return true;
}

bool idRenderModelCacheReader::ReadString( idStr &value, int maxLength ) {
	int length = 0;
	if ( maxLength < 0 || !ReadInt( length ) || length < 0 || length > maxLength
		|| !Reserve( length + 1, sizeof( char ) ) ) {
		valid = false;
		value.Clear();
		return false;
	}
	if ( length == 0 ) {
		value.Clear();
		return true;
	}
	value.Fill( ' ', length );
	if ( !ReadBytes( &value[0], length ) ) {
		value.Clear();
		return false;
	}
	for ( int i = 0; i < length; ++i ) {
		if ( value[i] == '\0' ) {
			valid = false;
			value.Clear();
			return false;
		}
	}
	return true;
}

bool idRenderModelCacheReader::ReadCount( int &value, int maxCount, size_t elementSize ) {
	if ( maxCount < 0 || !ReadInt( value ) || value < 0 || value > maxCount
		|| ( elementSize > 0 && !Reserve( value, elementSize ) ) ) {
		valid = false;
		return false;
	}
	return true;
}

bool idRenderModelCacheReader::Reserve( int count, size_t elementSize ) {
	if ( !valid || count < 0 || ( count > 0 && elementSize > RENDER_MODEL_CACHE_MAX_ALLOCATION_BYTES / static_cast<uint64_t>( count ) ) ) {
		valid = false;
		return false;
	}
	const uint64_t bytes = static_cast<uint64_t>( count ) * static_cast<uint64_t>( elementSize );
	if ( bytes > RENDER_MODEL_CACHE_MAX_ALLOCATION_BYTES - allocationBytes ) {
		valid = false;
		return false;
	}
	allocationBytes += bytes;
	return true;
}

idRenderModelCacheWriter::idRenderModelCacheWriter( idFile &destination ) :
	file( destination ),
	transferBytes( 0 ),
	valid( true ) {
}

bool idRenderModelCacheWriter::WriteBytes( const void *data, int length ) {
	if ( !valid || length < 0 || ( length > 0 && data == NULL )
		|| static_cast<uint64_t>( length ) > RENDER_MODEL_CACHE_MAX_TRANSFER_BYTES - transferBytes ) {
		valid = false;
		return false;
	}
	if ( length > 0 && file.Write( data, length ) != length ) {
		valid = false;
		return false;
	}
	transferBytes += static_cast<uint64_t>( length );
	return true;
}

bool idRenderModelCacheWriter::WriteInt( int value ) {
	if ( !valid || sizeof( value ) > RENDER_MODEL_CACHE_MAX_TRANSFER_BYTES - transferBytes
		|| file.WriteInt( value ) != sizeof( value ) ) {
		valid = false;
		return false;
	}
	transferBytes += sizeof( value );
	return true;
}

bool idRenderModelCacheWriter::WriteUnsignedInt( unsigned int value ) {
	if ( !valid || sizeof( value ) > RENDER_MODEL_CACHE_MAX_TRANSFER_BYTES - transferBytes
		|| file.WriteUnsignedInt( value ) != sizeof( value ) ) {
		valid = false;
		return false;
	}
	transferBytes += sizeof( value );
	return true;
}

bool idRenderModelCacheWriter::WriteUnsigned64( uint64_t value ) {
	return WriteUnsignedInt( static_cast<unsigned int>( value & 0xffffffffULL ) )
		&& WriteUnsignedInt( static_cast<unsigned int>( value >> 32 ) );
}

bool idRenderModelCacheWriter::WriteByte( byte value ) {
	if ( !valid || transferBytes >= RENDER_MODEL_CACHE_MAX_TRANSFER_BYTES
		|| file.WriteUnsignedChar( value ) != sizeof( value ) ) {
		valid = false;
		return false;
	}
	++transferBytes;
	return true;
}

bool idRenderModelCacheWriter::WriteBool( bool value ) {
	return WriteByte( value ? 1 : 0 );
}

bool idRenderModelCacheWriter::WriteFloat( float value ) {
	if ( !R_RenderModelCacheFloatIsFinite( value ) || !valid
		|| sizeof( value ) > RENDER_MODEL_CACHE_MAX_TRANSFER_BYTES - transferBytes
		|| file.WriteFloat( value ) != sizeof( value ) ) {
		valid = false;
		return false;
	}
	transferBytes += sizeof( value );
	return true;
}

bool idRenderModelCacheWriter::WriteVec2( const idVec2 &value ) {
	return WriteFloat( value.x ) && WriteFloat( value.y );
}

bool idRenderModelCacheWriter::WriteVec3( const idVec3 &value ) {
	return WriteFloat( value.x ) && WriteFloat( value.y ) && WriteFloat( value.z );
}

bool idRenderModelCacheWriter::WriteVec4( const idVec4 &value ) {
	return WriteFloat( value.x ) && WriteFloat( value.y ) && WriteFloat( value.z ) && WriteFloat( value.w );
}

bool idRenderModelCacheWriter::WriteFloatArray( const float *values, int count ) {
	if ( count < 0 || ( count > 0 && values == NULL )
		|| static_cast<uint64_t>( count ) > RENDER_MODEL_CACHE_MAX_TRANSFER_BYTES / sizeof( float ) ) {
		valid = false;
		return false;
	}
	for ( int i = 0; i < count; ++i ) {
		if ( !R_RenderModelCacheFloatIsFinite( values[i] ) ) {
			valid = false;
			return false;
		}
	}
	return WriteBytes( values, count * static_cast<int>( sizeof( float ) ) );
}

bool idRenderModelCacheWriter::WriteIntArray( const int *values, int count ) {
	if ( count < 0 || ( count > 0 && values == NULL )
		|| static_cast<uint64_t>( count ) > RENDER_MODEL_CACHE_MAX_TRANSFER_BYTES / sizeof( int ) ) {
		valid = false;
		return false;
	}
	return WriteBytes( values, count * static_cast<int>( sizeof( int ) ) );
}

bool idRenderModelCacheWriter::WriteBounds( const idBounds &value ) {
	return WriteVec3( value[0] ) && WriteVec3( value[1] );
}

bool idRenderModelCacheWriter::WriteString( const char *value, int maxLength ) {
	if ( value == NULL ) {
		value = "";
	}
	const size_t length = strlen( value );
	if ( maxLength < 0 || length > static_cast<size_t>( maxLength ) || length > 0x7fffffffU ) {
		valid = false;
		return false;
	}
	return WriteInt( static_cast<int>( length ) )
		&& WriteBytes( value, static_cast<int>( length ) );
}

bool R_TryReadGeneratedRenderModelCache( idRenderModel &model, const char *sourcePath,
		unsigned int parserVersion, const char *settingsKey ) {
	idRenderModelStatic *cacheModel = dynamic_cast<idRenderModelStatic *>( &model );
	if ( sourcePath == NULL || sourcePath[0] == '\0' || parserVersion == 0
		|| cacheModel == NULL
		|| cacheModel->LevelLoadCachePayloadType() == RENDER_MODEL_CACHE_UNSUPPORTED ) {
		return false;
	}
	fileSystem->RecordLevelLoadResource( LEVEL_LOAD_RESOURCE_RENDER_MODEL,
		sourcePath, settingsKey != NULL ? settingsKey : "", 0u, 2u );
	// the learned manifest records the model either way; only the cache read
	// depends on the generated caches being on
	if ( !fileSystem->GeneratedCacheReadsEnabled( GENERATED_CACHE_RENDER_MODEL ) ) {
		return false;
	}
	idFile *cache = fileSystem->OpenGeneratedCacheRead( GENERATED_CACHE_RENDER_MODEL,
		sourcePath, parserVersion, settingsKey != NULL ? settingsKey : "" );
	if ( cache == NULL ) {
		return false;
	}
	const bool decoded = cacheModel->ReadLevelLoadCachePayload( *cache );
	const bool consumedPayload = decoded && cache->Tell() == cache->Length();
	fileSystem->CloseFile( cache );
	if ( decoded ) {
		// the payload now lives in the model; a caller that re-derives it
		// purges first, which clears this again
		cacheModel->SetGeneratedCacheContent( true );
	}
	if ( !consumedPayload ) {
		fileSystem->DiscardGeneratedCache( GENERATED_CACHE_RENDER_MODEL,
			sourcePath, parserVersion, settingsKey != NULL ? settingsKey : "" );
	}
	return consumedPayload;
}

void R_WriteGeneratedRenderModelCache( const idRenderModel &model, const char *sourcePath,
		unsigned int parserVersion, const char *settingsKey ) {
	const idRenderModelStatic *cacheModel = dynamic_cast<const idRenderModelStatic *>( &model );
	if ( sourcePath == NULL || sourcePath[0] == '\0' || parserVersion == 0
		|| cacheModel == NULL
		|| cacheModel->LevelLoadCachePayloadType() == RENDER_MODEL_CACHE_UNSUPPORTED ) {
		return;
	}
	// every static, MD5 and MD5R load ends here; with the generated caches off
	// (the default) WriteGeneratedCache would discard the encoded payload
	if ( !fileSystem->GeneratedCacheWritesEnabled( GENERATED_CACHE_RENDER_MODEL ) ) {
		return;
	}
	idFile *payload = fileSystem->GetNewFileMemory();
	if ( payload == NULL ) {
		return;
	}
	if ( cacheModel->WriteLevelLoadCachePayload( *payload ) ) {
		const int payloadLength = payload->Length();
		const char *payloadData = payload->GetDataPtr();
		if ( payloadLength > 0 && payloadData != NULL ) {
			fileSystem->WriteGeneratedCache( GENERATED_CACHE_RENDER_MODEL,
				sourcePath, parserVersion, settingsKey != NULL ? settingsKey : "",
				payloadData, static_cast<unsigned int>( payloadLength ) );
		}
	}
	fileSystem->CloseFile( payload );
}

namespace {
	static const unsigned int STATIC_MODEL_CACHE_MAGIC = 0x5334514fU; // "OQ4S"
	static const int STATIC_MODEL_CACHE_VERSION = 2;
	static const unsigned int STATIC_MODEL_GENERATED_CACHE_PARSER_VERSION = 0x00010004U;	// bump when R_BuildDeformInfo's derivation changes: cache hits no longer cross-check against a fresh rebuild; 3: back-side surface ids were uninitialised before; 4: silhouette edges stopped at 65536
	static const int MODEL_CACHE_MAX_NAME = 4096;
	static const int MODEL_CACHE_MAX_MATERIAL = 4096;
	static const int MODEL_CACHE_MAX_SURFACES = 65536;
	static const int MODEL_CACHE_MAX_SURFACE_VERTS = 1 << 20;
	static const int MODEL_CACHE_MAX_SURFACE_INDEXES = 1 << 24;
	static const int MODEL_CACHE_MAX_SIL_EDGES = 1 << 23;
	static const int MODEL_CACHE_MAX_SKIN_TRANSFORMS = 65536;
	static const int MODEL_CACHE_MAX_MATERIALIZED_VERTS = 1 << 18;
	static const int MODEL_CACHE_MAX_MATERIALIZED_INDEXES = 1 << 20;

	// the bulk array codecs stream these structures as raw little-endian bytes;
	// pin their layouts so a change breaks the build instead of the cache format
	static_assert( sizeof( glIndex_t ) == sizeof( int ), "cache bulk IO streams glIndex_t as int" );
	static_assert( sizeof( silEdge_t ) == 4 * sizeof( int ), "cache bulk IO streams silEdge_t as 4 ints" );
	static_assert( sizeof( dominantTri_t ) == 2 * sizeof( int ) + 3 * sizeof( float ), "cache bulk IO streams dominantTri_t as 2 ints + 3 floats" );
	static_assert( sizeof( idPlane ) == 4 * sizeof( float ), "cache bulk IO streams idPlane as 4 floats" );
	static_assert( sizeof( idVec4 ) == 4 * sizeof( float ), "cache bulk IO streams idVec4 as 4 floats" );
	static_assert( sizeof( shadowCache_t ) == sizeof( idVec4 ), "cache bulk IO streams shadowCache_t as idVec4" );
#if defined( _MD5R_SUPPORT )
	static_assert( sizeof( rvSilTraceVertT ) == sizeof( idVec4 ), "cache bulk IO streams rvSilTraceVertT as idVec4" );
#endif

	struct renderModelCacheTriStage_t {
		idBounds bounds;
		int surfaceFlags;
		bool generateNormals;
		bool tangentsCalculated;
		bool facePlanesCalculated;
		bool perfectHull;
		bool deformedSurface;
		int numVerts;
		idList<idDrawVert> verts;
		int numIndexes;
		idList<glIndex_t> indexes;
		idList<glIndex_t> silIndexes;
		idList<int> mirroredVerts;
		idList<int> dupVerts;
		idList<silEdge_t> silEdges;
		idList<idPlane> facePlanes;
		idList<dominantTri_t> dominantTris;
		int numShadowIndexesNoFrontCaps;
		int numShadowIndexesNoCaps;
		int shadowCapPlaneBits;
		idList<idVec4> shadowVertexes;
		idList<idVec4> silTraceVertexes;
		int numSkinToModelTransforms;
		idList<float> skinToModelTransforms;

		renderModelCacheTriStage_t() :
			surfaceFlags( 0 ),
			generateNormals( false ),
			tangentsCalculated( false ),
			facePlanesCalculated( false ),
			perfectHull( false ),
			deformedSurface( false ),
			numVerts( 0 ),
			numIndexes( 0 ),
			numShadowIndexesNoFrontCaps( 0 ),
			numShadowIndexesNoCaps( 0 ),
			shadowCapPlaneBits( 0 ),
			numSkinToModelTransforms( 0 ) {
			bounds.Clear();
		}
	};

	struct renderModelCacheSurfaceStage_t {
		int id;
		idStr materialName;
		bool materialUnsmoothedTangents;
		bool materialCreatesBackSides;
		bool materialCastsShadow;
		bool materialDiscrete;
		bool hasGeometry;
		renderModelCacheTriStage_t geometry;

		renderModelCacheSurfaceStage_t() :
			id( 0 ),
			materialUnsmoothedTangents( false ),
			materialCreatesBackSides( false ),
			materialCastsShadow( false ),
			materialDiscrete( false ),
			hasGeometry( false ) {}
	};

	static bool R_ModelCacheBoundsCanWrite( const idBounds &bounds ) {
		for ( int axis = 0; axis < 3; ++axis ) {
			if ( !R_RenderModelCacheFloatIsFinite( bounds[0][axis] )
				|| !R_RenderModelCacheFloatIsFinite( bounds[1][axis] ) ) {
				return false;
			}
		}
		if ( bounds.IsCleared() ) {
			return true;
		}
		for ( int axis = 0; axis < 3; ++axis ) {
			if ( bounds[0][axis] > bounds[1][axis] ) {
				return false;
			}
		}
		return true;
	}

	// an idDrawVert in the cache stream: 14 floats (xyz, st, normal, tangents)
	// followed by the 8 color/color2 bytes; chunked so a whole batch of vertexes
	// moves through one idFile call instead of 22 per vertex
	static const int MODEL_CACHE_DRAW_VERT_STREAM_FLOATS = 14;
	static const int MODEL_CACHE_DRAW_VERT_STREAM_BYTES = MODEL_CACHE_DRAW_VERT_STREAM_FLOATS * static_cast<int>( sizeof( float ) ) + 8;
	static const int MODEL_CACHE_DRAW_VERT_CHUNK_RECORDS = 64;

	static bool R_ModelCacheWriteDrawVertArray( idRenderModelCacheWriter &writer, const idDrawVert *verts, int count ) {
		byte chunk[MODEL_CACHE_DRAW_VERT_CHUNK_RECORDS * MODEL_CACHE_DRAW_VERT_STREAM_BYTES];
		if ( count < 0 || ( count > 0 && verts == NULL ) ) {
			return false;
		}
		for ( int base = 0; base < count; base += MODEL_CACHE_DRAW_VERT_CHUNK_RECORDS ) {
			const int chunkRecords = Min( count - base, MODEL_CACHE_DRAW_VERT_CHUNK_RECORDS );
			byte *record = chunk;
			for ( int i = 0; i < chunkRecords; ++i, record += MODEL_CACHE_DRAW_VERT_STREAM_BYTES ) {
				const idDrawVert &vert = verts[base + i];
				float streamFloats[MODEL_CACHE_DRAW_VERT_STREAM_FLOATS];
				streamFloats[0] = vert.xyz.x;
				streamFloats[1] = vert.xyz.y;
				streamFloats[2] = vert.xyz.z;
				streamFloats[3] = vert.st.x;
				streamFloats[4] = vert.st.y;
				streamFloats[5] = vert.normal.x;
				streamFloats[6] = vert.normal.y;
				streamFloats[7] = vert.normal.z;
				streamFloats[8] = vert.tangents[0].x;
				streamFloats[9] = vert.tangents[0].y;
				streamFloats[10] = vert.tangents[0].z;
				streamFloats[11] = vert.tangents[1].x;
				streamFloats[12] = vert.tangents[1].y;
				streamFloats[13] = vert.tangents[1].z;
				for ( int component = 0; component < MODEL_CACHE_DRAW_VERT_STREAM_FLOATS; ++component ) {
					if ( !R_RenderModelCacheFloatIsFinite( streamFloats[component] ) ) {
						// same acceptance as WriteFloat: the payload is never published
						return false;
					}
					streamFloats[component] = LittleFloat( streamFloats[component] );
				}
				memcpy( record, streamFloats, sizeof( streamFloats ) );
				memcpy( record + sizeof( streamFloats ), vert.color, sizeof( vert.color ) );
				memcpy( record + sizeof( streamFloats ) + sizeof( vert.color ), vert.color2, sizeof( vert.color2 ) );
			}
			if ( !writer.WriteBytes( chunk, chunkRecords * MODEL_CACHE_DRAW_VERT_STREAM_BYTES ) ) {
				return false;
			}
		}
		return true;
	}

	static bool R_ModelCacheReadDrawVertArray( idRenderModelCacheReader &reader, idDrawVert *verts, int count ) {
		byte chunk[MODEL_CACHE_DRAW_VERT_CHUNK_RECORDS * MODEL_CACHE_DRAW_VERT_STREAM_BYTES];
		if ( count < 0 || ( count > 0 && verts == NULL ) ) {
			return false;
		}
		for ( int base = 0; base < count; base += MODEL_CACHE_DRAW_VERT_CHUNK_RECORDS ) {
			const int chunkRecords = Min( count - base, MODEL_CACHE_DRAW_VERT_CHUNK_RECORDS );
			if ( !reader.ReadBytes( chunk, chunkRecords * MODEL_CACHE_DRAW_VERT_STREAM_BYTES ) ) {
				return false;
			}
			const byte *record = chunk;
			for ( int i = 0; i < chunkRecords; ++i, record += MODEL_CACHE_DRAW_VERT_STREAM_BYTES ) {
				float streamFloats[MODEL_CACHE_DRAW_VERT_STREAM_FLOATS];
				memcpy( streamFloats, record, sizeof( streamFloats ) );
				for ( int component = 0; component < MODEL_CACHE_DRAW_VERT_STREAM_FLOATS; ++component ) {
					streamFloats[component] = LittleFloat( streamFloats[component] );
					if ( !R_RenderModelCacheFloatIsFinite( streamFloats[component] ) ) {
						// same acceptance as ReadFloat: the caller rejects the whole payload
						return false;
					}
				}
				idDrawVert &vert = verts[base + i];
				vert.xyz.Set( streamFloats[0], streamFloats[1], streamFloats[2] );
				vert.st.Set( streamFloats[3], streamFloats[4] );
				vert.normal.Set( streamFloats[5], streamFloats[6], streamFloats[7] );
				vert.tangents[0].Set( streamFloats[8], streamFloats[9], streamFloats[10] );
				vert.tangents[1].Set( streamFloats[11], streamFloats[12], streamFloats[13] );
				memcpy( vert.color, record + sizeof( streamFloats ), sizeof( vert.color ) );
				memcpy( vert.color2, record + sizeof( streamFloats ) + sizeof( vert.color ), sizeof( vert.color2 ) );
			}
		}
		return true;
	}

	static bool R_ModelCacheWriteTri( idRenderModelCacheWriter &writer, const srfTriangles_t &tri ) {
		const bool hasDerivedTopology = tri.silIndexes != NULL || tri.numMirroredVerts > 0
			|| tri.numDupVerts > 0 || tri.numSilEdges > 0 || tri.dominantTris != NULL;
		if ( !R_ModelCacheBoundsCanWrite( tri.bounds )
			|| tri.gpuSkinningBindPoseVerts != NULL || tri.gpuSkinningVerts != NULL
			|| tri.numGpuSkinningVerts != 0 || tri.gpuSkinningJointPalette != NULL
			|| tri.gpuSkinningJointPaletteAlloc != NULL || tri.numGpuSkinningJoints != 0
			|| tri.numGpuSkinningJointPaletteAllocJoints != 0
			|| tri.gpuSkinningPaletteGeneration != 0 || tri.gpuSkinningFallbackReason != 0
			|| tri.gpuSkinningSignedWeights
			|| tri.surfaceFlags < 0 || ( tri.surfaceFlags & ~STF_SOFT_PARTICLE_CANDIDATE ) != 0
			|| tri.numVerts < 0 || tri.numVerts > MODEL_CACHE_MAX_SURFACE_VERTS
			|| ( tri.numVerts > 0 && tri.verts == NULL )
			|| tri.numIndexes < 0 || tri.numIndexes > MODEL_CACHE_MAX_SURFACE_INDEXES
			|| ( tri.numIndexes % 3 ) != 0 || tri.deformedSurface
			|| tri.numMirroredVerts < 0 || tri.numMirroredVerts > tri.numVerts
			|| tri.numDupVerts < 0 || tri.numDupVerts > tri.numVerts
			|| tri.numSilEdges < 0 || tri.numSilEdges > MODEL_CACHE_MAX_SIL_EDGES
			|| tri.numShadowIndexesNoFrontCaps < 0 || tri.numShadowIndexesNoFrontCaps > tri.numIndexes
			|| tri.numShadowIndexesNoCaps < 0 || tri.numShadowIndexesNoCaps > tri.numShadowIndexesNoFrontCaps
			|| tri.shadowCapPlaneBits < 0 || tri.shadowCapPlaneBits > 127
			|| ( hasDerivedTopology && ( tri.numVerts > MODEL_CACHE_MAX_MATERIALIZED_VERTS
				|| tri.numIndexes > MODEL_CACHE_MAX_MATERIALIZED_INDEXES ) ) ) {
			return false;
		}

		if ( !writer.WriteBounds( tri.bounds ) || !writer.WriteInt( tri.surfaceFlags )
			|| !writer.WriteBool( tri.generateNormals ) || !writer.WriteBool( tri.tangentsCalculated )
			|| !writer.WriteBool( tri.facePlanesCalculated ) || !writer.WriteBool( tri.perfectHull )
			|| !writer.WriteBool( tri.deformedSurface ) || !writer.WriteInt( tri.numVerts )
			|| !writer.WriteBool( tri.verts != NULL ) ) {
			return false;
		}
		if ( tri.verts != NULL ) {
			if ( !R_ModelCacheWriteDrawVertArray( writer, tri.verts, tri.numVerts ) ) {
				return false;
			}
		}

		if ( !writer.WriteInt( tri.numIndexes ) || !writer.WriteBool( tri.indexes != NULL ) ) {
			return false;
		}
		if ( tri.indexes != NULL ) {
			for ( int i = 0; i < tri.numIndexes; ++i ) {
				if ( tri.indexes[i] < 0 || tri.indexes[i] >= tri.numVerts ) {
					return false;
				}
			}
			if ( !writer.WriteIntArray( tri.indexes, tri.numIndexes ) ) {
				return false;
			}
		} else if ( tri.numIndexes != 0 ) {
			return false;
		}

		if ( !writer.WriteBool( tri.silIndexes != NULL ) ) {
			return false;
		}
		if ( tri.silIndexes != NULL ) {
			for ( int i = 0; i < tri.numIndexes; ++i ) {
				if ( tri.silIndexes[i] < 0 || tri.silIndexes[i] >= tri.numVerts ) {
					return false;
				}
			}
			if ( !writer.WriteIntArray( tri.silIndexes, tri.numIndexes ) ) {
				return false;
			}
		}

		if ( !writer.WriteInt( tri.numMirroredVerts ) ) {
			return false;
		}
		const int sourceVertCount = tri.numVerts - tri.numMirroredVerts;
		for ( int i = 0; i < tri.numMirroredVerts; ++i ) {
			if ( tri.mirroredVerts == NULL || tri.mirroredVerts[i] < 0 || tri.mirroredVerts[i] >= sourceVertCount ) {
				return false;
			}
		}
		if ( !writer.WriteIntArray( tri.mirroredVerts, tri.numMirroredVerts ) ) {
			return false;
		}

		if ( !writer.WriteInt( tri.numDupVerts ) ) {
			return false;
		}
		for ( int i = 0; i < tri.numDupVerts * 2; ++i ) {
			if ( tri.dupVerts == NULL || tri.dupVerts[i] < 0 || tri.dupVerts[i] >= tri.numVerts ) {
				return false;
			}
		}
		if ( !writer.WriteIntArray( tri.dupVerts, tri.numDupVerts * 2 ) ) {
			return false;
		}

		if ( !writer.WriteInt( tri.numSilEdges ) ) {
			return false;
		}
		if ( tri.numSilEdges > 0 && tri.silEdges == NULL ) {
			return false;
		}
		const int numPlanes = tri.numIndexes / 3;
		for ( int i = 0; i < tri.numSilEdges; ++i ) {
			const silEdge_t &edge = tri.silEdges[i];
			if ( edge.p1 < 0 || edge.p1 >= numPlanes
				|| edge.p2 < 0 || edge.p2 > numPlanes || edge.v1 < 0 || edge.v1 >= tri.numVerts
				|| edge.v2 < 0 || edge.v2 >= tri.numVerts ) {
				return false;
			}
		}
		if ( !writer.WriteIntArray( reinterpret_cast<const int *>( tri.silEdges ), tri.numSilEdges * 4 ) ) {
			return false;
		}

		if ( !writer.WriteBool( tri.facePlanes != NULL ) ) {
			return false;
		}
		if ( tri.facePlanes != NULL ) {
			if ( !writer.WriteFloatArray( reinterpret_cast<const float *>( tri.facePlanes ), numPlanes * 4 ) ) {
				return false;
			}
		}

		if ( !writer.WriteBool( tri.dominantTris != NULL ) ) {
			return false;
		}
		if ( tri.dominantTris != NULL ) {
			for ( int i = 0; i < tri.numVerts; ++i ) {
				const dominantTri_t &dominant = tri.dominantTris[i];
				if ( dominant.v2 < 0 || dominant.v2 >= tri.numVerts || dominant.v3 < 0 || dominant.v3 >= tri.numVerts
					|| !R_RenderModelCacheFloatIsFinite( dominant.normalizationScale[0] )
					|| !R_RenderModelCacheFloatIsFinite( dominant.normalizationScale[1] )
					|| !R_RenderModelCacheFloatIsFinite( dominant.normalizationScale[2] ) ) {
					return false;
				}
			}
			if ( !writer.WriteBytes( tri.dominantTris, tri.numVerts * static_cast<int>( sizeof( dominantTri_t ) ) ) ) {
				return false;
			}
		}

		if ( !writer.WriteInt( tri.numShadowIndexesNoFrontCaps ) || !writer.WriteInt( tri.numShadowIndexesNoCaps )
			|| !writer.WriteInt( tri.shadowCapPlaneBits ) || !writer.WriteBool( tri.shadowVertexes != NULL ) ) {
			return false;
		}
		if ( tri.shadowVertexes != NULL ) {
			if ( !writer.WriteFloatArray( reinterpret_cast<const float *>( tri.shadowVertexes ), tri.numVerts * 4 ) ) {
				return false;
			}
		}

		bool hasSilTraceVerts = false;
#if defined( _MD5R_SUPPORT ) || defined( Q4SDK_MD5R )
		hasSilTraceVerts = tri.silTraceVerts != NULL;
#endif
		if ( !writer.WriteBool( hasSilTraceVerts ) ) {
			return false;
		}
#if defined( _MD5R_SUPPORT )
		if ( hasSilTraceVerts ) {
			if ( !writer.WriteFloatArray( reinterpret_cast<const float *>( tri.silTraceVerts ), tri.numVerts * 4 ) ) {
				return false;
			}
		}
#elif defined( Q4SDK_MD5R )
		if ( hasSilTraceVerts ) {
			return false;
		}
#endif

		int numSkinToModelTransforms = 0;
#if defined( _MD5R_SUPPORT ) || defined( Q4SDK_MD5R )
		numSkinToModelTransforms = tri.numSkinToModelTransforms;
#endif
		if ( numSkinToModelTransforms < 0 || numSkinToModelTransforms > MODEL_CACHE_MAX_SKIN_TRANSFORMS
			|| !writer.WriteInt( numSkinToModelTransforms ) ) {
			return false;
		}
		if ( numSkinToModelTransforms > 0 ) {
#if defined( _MD5R_SUPPORT ) || defined( Q4SDK_MD5R )
			if ( tri.skinToModelTransforms == NULL ) {
				return false;
			}
			const int floatCount = numSkinToModelTransforms * 16;
			if ( !writer.WriteFloatArray( tri.skinToModelTransforms, floatCount ) ) {
				return false;
			}
#else
			return false;
#endif
		}
		return writer.IsValid();
	}

	static bool R_ModelCacheReadTriStage( idRenderModelCacheReader &reader, renderModelCacheTriStage_t &tri ) {
		bool hasVerts = false;
		bool hasIndexes = false;
		bool hasSilIndexes = false;
		bool hasFacePlanes = false;
		bool hasDominantTris = false;
		bool hasShadowVertexes = false;
		bool hasSilTraceVertexes = false;

		if ( !reader.ReadBounds( tri.bounds, true ) || !reader.ReadInt( tri.surfaceFlags )
			|| tri.surfaceFlags < 0 || ( tri.surfaceFlags & ~STF_SOFT_PARTICLE_CANDIDATE ) != 0
			|| !reader.ReadBool( tri.generateNormals ) || !reader.ReadBool( tri.tangentsCalculated )
			|| !reader.ReadBool( tri.facePlanesCalculated ) || !reader.ReadBool( tri.perfectHull )
			|| !reader.ReadBool( tri.deformedSurface ) || tri.deformedSurface
			|| !reader.ReadCount( tri.numVerts, MODEL_CACHE_MAX_SURFACE_VERTS ) || !reader.ReadBool( hasVerts )
			|| ( tri.numVerts > 0 && !hasVerts ) ) {
			return false;
		}
		if ( hasVerts ) {
			if ( !reader.Reserve( tri.numVerts, sizeof( idDrawVert ) ) ) {
				return false;
			}
			tri.verts.SetNum( tri.numVerts );
			if ( !R_ModelCacheReadDrawVertArray( reader, tri.verts.Ptr(), tri.numVerts ) ) {
				return false;
			}
		}

		if ( !reader.ReadCount( tri.numIndexes, MODEL_CACHE_MAX_SURFACE_INDEXES )
			|| ( tri.numIndexes % 3 ) != 0 || !reader.ReadBool( hasIndexes )
			|| ( tri.numIndexes > 0 && !hasIndexes ) ) {
			return false;
		}
		if ( hasIndexes ) {
			if ( !reader.Reserve( tri.numIndexes, sizeof( glIndex_t ) ) ) {
				return false;
			}
			tri.indexes.SetNum( tri.numIndexes );
			if ( !reader.ReadIntArray( tri.indexes.Ptr(), tri.numIndexes ) ) {
				return false;
			}
			for ( int i = 0; i < tri.numIndexes; ++i ) {
				if ( tri.indexes[i] < 0 || tri.indexes[i] >= tri.numVerts ) {
					return false;
				}
			}
		}

		if ( !reader.ReadBool( hasSilIndexes ) ) {
			return false;
		}
		if ( hasSilIndexes ) {
			if ( !hasVerts || !reader.Reserve( tri.numIndexes, sizeof( glIndex_t ) ) ) {
				return false;
			}
			tri.silIndexes.SetNum( tri.numIndexes );
			if ( !reader.ReadIntArray( tri.silIndexes.Ptr(), tri.numIndexes ) ) {
				return false;
			}
			for ( int i = 0; i < tri.numIndexes; ++i ) {
				if ( tri.silIndexes[i] < 0 || tri.silIndexes[i] >= tri.numVerts ) {
					return false;
				}
			}
		}

		int mirroredCount = 0;
		if ( !reader.ReadCount( mirroredCount, tri.numVerts, sizeof( int ) ) ) {
			return false;
		}
		tri.mirroredVerts.SetNum( mirroredCount );
		if ( !reader.ReadIntArray( tri.mirroredVerts.Ptr(), mirroredCount ) ) {
			return false;
		}
		const int sourceVertCount = tri.numVerts - mirroredCount;
		for ( int i = 0; i < mirroredCount; ++i ) {
			if ( tri.mirroredVerts[i] < 0 || tri.mirroredVerts[i] >= sourceVertCount ) {
				return false;
			}
		}

		int dupCount = 0;
		if ( !reader.ReadCount( dupCount, tri.numVerts ) || dupCount > 0x3fffffff
			|| !reader.Reserve( dupCount * 2, sizeof( int ) ) ) {
			return false;
		}
		tri.dupVerts.SetNum( dupCount * 2 );
		if ( !reader.ReadIntArray( tri.dupVerts.Ptr(), tri.dupVerts.Num() ) ) {
			return false;
		}
		for ( int i = 0; i < tri.dupVerts.Num(); ++i ) {
			if ( tri.dupVerts[i] < 0 || tri.dupVerts[i] >= tri.numVerts ) {
				return false;
			}
		}

		int silEdgeCount = 0;
		if ( !reader.ReadCount( silEdgeCount, MODEL_CACHE_MAX_SIL_EDGES, sizeof( silEdge_t ) ) ) {
			return false;
		}
		tri.silEdges.SetNum( silEdgeCount );
		if ( !reader.ReadIntArray( reinterpret_cast<int *>( tri.silEdges.Ptr() ), silEdgeCount * 4 ) ) {
			return false;
		}
		const int numPlanes = tri.numIndexes / 3;
		for ( int i = 0; i < silEdgeCount; ++i ) {
			const silEdge_t &edge = tri.silEdges[i];
			if ( edge.p1 < 0 || edge.p1 >= numPlanes || edge.p2 < 0 || edge.p2 > numPlanes
				|| edge.v1 < 0 || edge.v1 >= tri.numVerts || edge.v2 < 0 || edge.v2 >= tri.numVerts ) {
				return false;
			}
		}

		if ( !reader.ReadBool( hasFacePlanes ) ) {
			return false;
		}
		if ( hasFacePlanes ) {
			if ( !reader.Reserve( numPlanes, sizeof( idPlane ) ) ) {
				return false;
			}
			tri.facePlanes.SetNum( numPlanes );
			if ( !reader.ReadFloatArray( reinterpret_cast<float *>( tri.facePlanes.Ptr() ), numPlanes * 4 ) ) {
				return false;
			}
		}

		if ( !reader.ReadBool( hasDominantTris ) ) {
			return false;
		}
		if ( hasDominantTris ) {
			if ( !hasVerts || !reader.Reserve( tri.numVerts, sizeof( dominantTri_t ) ) ) {
				return false;
			}
			tri.dominantTris.SetNum( tri.numVerts );
			if ( !reader.ReadBytes( tri.dominantTris.Ptr(), tri.numVerts * static_cast<int>( sizeof( dominantTri_t ) ) ) ) {
				return false;
			}
			for ( int i = 0; i < tri.numVerts; ++i ) {
				dominantTri_t &dominant = tri.dominantTris[i];
				dominant.v2 = LittleLong( dominant.v2 );
				dominant.v3 = LittleLong( dominant.v3 );
				if ( dominant.v2 < 0 || dominant.v2 >= tri.numVerts || dominant.v3 < 0 || dominant.v3 >= tri.numVerts ) {
					return false;
				}
				for ( int scale = 0; scale < 3; ++scale ) {
					dominant.normalizationScale[scale] = LittleFloat( dominant.normalizationScale[scale] );
					if ( !R_RenderModelCacheFloatIsFinite( dominant.normalizationScale[scale] ) ) {
						return false;
					}
				}
			}
		}

		if ( !reader.ReadInt( tri.numShadowIndexesNoFrontCaps ) || tri.numShadowIndexesNoFrontCaps < 0
			|| tri.numShadowIndexesNoFrontCaps > tri.numIndexes || !reader.ReadInt( tri.numShadowIndexesNoCaps )
			|| tri.numShadowIndexesNoCaps < 0 || tri.numShadowIndexesNoCaps > tri.numShadowIndexesNoFrontCaps
			|| !reader.ReadInt( tri.shadowCapPlaneBits ) || tri.shadowCapPlaneBits < 0 || tri.shadowCapPlaneBits > 127
			|| !reader.ReadBool( hasShadowVertexes ) ) {
			return false;
		}
		if ( hasShadowVertexes ) {
			if ( !reader.Reserve( tri.numVerts, sizeof( idVec4 ) ) ) {
				return false;
			}
			tri.shadowVertexes.SetNum( tri.numVerts );
			if ( !reader.ReadFloatArray( reinterpret_cast<float *>( tri.shadowVertexes.Ptr() ), tri.numVerts * 4 ) ) {
				return false;
			}
		}

		if ( !reader.ReadBool( hasSilTraceVertexes ) ) {
			return false;
		}
		if ( hasSilTraceVertexes ) {
			if ( !reader.Reserve( tri.numVerts, sizeof( idVec4 ) ) ) {
				return false;
			}
			tri.silTraceVertexes.SetNum( tri.numVerts );
			if ( !reader.ReadFloatArray( reinterpret_cast<float *>( tri.silTraceVertexes.Ptr() ), tri.numVerts * 4 ) ) {
				return false;
			}
		}

		if ( !reader.ReadCount( tri.numSkinToModelTransforms, MODEL_CACHE_MAX_SKIN_TRANSFORMS )
			|| tri.numSkinToModelTransforms > 0x07ffffff
			|| !reader.Reserve( tri.numSkinToModelTransforms * 16, sizeof( float ) ) ) {
			return false;
		}
		tri.skinToModelTransforms.SetNum( tri.numSkinToModelTransforms * 16 );
		if ( !reader.ReadFloatArray( tri.skinToModelTransforms.Ptr(), tri.skinToModelTransforms.Num() ) ) {
			return false;
		}

		const bool hasDerivedTopology = hasSilIndexes || mirroredCount > 0 || dupCount > 0 || silEdgeCount > 0 || hasDominantTris;
		if ( hasDerivedTopology && ( !hasVerts || !hasIndexes || tri.numVerts <= 0 || tri.numIndexes <= 0 ) ) {
			return false;
		}
		return reader.IsValid();
	}

	static bool R_ModelCacheMaterializeTri( const renderModelCacheTriStage_t &source, srfTriangles_t *&outTri ) {
		outTri = NULL;
		srfTriangles_t *tri = R_AllocStaticTriSurf();
		if ( tri == NULL ) {
			return false;
		}

		tri->bounds = source.bounds;
		tri->surfaceFlags = source.surfaceFlags;
		tri->generateNormals = source.generateNormals;
		tri->tangentsCalculated = source.tangentsCalculated;
		tri->facePlanesCalculated = source.facePlanesCalculated;
		tri->perfectHull = source.perfectHull;
		tri->deformedSurface = false;
		tri->numVerts = source.numVerts;
		tri->numIndexes = source.numIndexes;

		if ( source.verts.Num() > 0 ) {
			R_AllocStaticTriSurfVerts( tri, source.numVerts );
			memcpy( tri->verts, source.verts.Ptr(), source.numVerts * sizeof( tri->verts[0] ) );
		}
		if ( source.indexes.Num() > 0 ) {
			R_AllocStaticTriSurfIndexes( tri, source.numIndexes );
			memcpy( tri->indexes, source.indexes.Ptr(), source.numIndexes * sizeof( tri->indexes[0] ) );
		}

		const bool hasDerivedTopology = source.silIndexes.Num() > 0 || source.mirroredVerts.Num() > 0
			|| source.dupVerts.Num() > 0 || source.silEdges.Num() > 0 || source.dominantTris.Num() > 0;
		if ( hasDerivedTopology ) {
			if ( source.silIndexes.Num() != source.numIndexes
				|| source.numVerts > MODEL_CACHE_MAX_MATERIALIZED_VERTS
				|| source.numIndexes > MODEL_CACHE_MAX_MATERIALIZED_INDEXES ) {
				R_FreeStaticTriSurf( tri );
				return false;
			}

			// R_ModelCacheReadTriStage fully range-validated the derived topology
			// (and numVerts == sourceVertCount + mirroredVerts.Num() by construction);
			// allocate the arrays directly instead of re-deriving them just to
			// overwrite the results with the cached contents.
			const int sourceVertCount = source.numVerts - source.mirroredVerts.Num();
			deformInfo_t *deform = R_AllocDeformInfo( sourceVertCount, source.numVerts,
				source.numIndexes, source.mirroredVerts.Num(), source.dupVerts.Num() / 2,
				source.silEdges.Num(), source.dominantTris.Num() > 0 );
			if ( deform == NULL ) {
				R_FreeStaticTriSurf( tri );
				return false;
			}

			tri->silIndexes = deform->silIndexes;
			deform->silIndexes = NULL;
			memcpy( tri->silIndexes, source.silIndexes.Ptr(), source.numIndexes * sizeof( tri->silIndexes[0] ) );

			tri->numMirroredVerts = deform->numMirroredVerts;
			tri->mirroredVerts = deform->mirroredVerts;
			deform->mirroredVerts = NULL;
			if ( tri->numMirroredVerts > 0 ) {
				memcpy( tri->mirroredVerts, source.mirroredVerts.Ptr(), tri->numMirroredVerts * sizeof( tri->mirroredVerts[0] ) );
			}

			tri->numDupVerts = deform->numDupVerts;
			tri->dupVerts = deform->dupVerts;
			deform->dupVerts = NULL;
			if ( tri->numDupVerts > 0 ) {
				memcpy( tri->dupVerts, source.dupVerts.Ptr(), tri->numDupVerts * 2 * sizeof( tri->dupVerts[0] ) );
			}

			tri->dominantTris = deform->dominantTris;
			deform->dominantTris = NULL;
			if ( source.dominantTris.Num() > 0 ) {
				memcpy( tri->dominantTris, source.dominantTris.Ptr(), source.numVerts * sizeof( tri->dominantTris[0] ) );
			}

			tri->numSilEdges = deform->numSilEdges;
			tri->silEdges = deform->silEdges;
			deform->silEdges = NULL;
			R_FreeDeformInfo( deform );
			if ( tri->numSilEdges > 0 ) {
				memcpy( tri->silEdges, source.silEdges.Ptr(), tri->numSilEdges * sizeof( tri->silEdges[0] ) );
			}
		}

		if ( source.facePlanes.Num() > 0 ) {
			R_AllocStaticTriSurfPlanes( tri, source.numIndexes );
			memcpy( tri->facePlanes, source.facePlanes.Ptr(), source.facePlanes.Num() * sizeof( tri->facePlanes[0] ) );
		}
		if ( source.shadowVertexes.Num() > 0 ) {
			R_AllocStaticTriSurfShadowVerts( tri, source.numVerts );
			for ( int i = 0; i < source.numVerts; ++i ) {
				tri->shadowVertexes[i].xyz = source.shadowVertexes[i];
			}
		}

		if ( source.silTraceVertexes.Num() > 0 ) {
#if defined( _MD5R_SUPPORT )
			R_AllocStaticTriSurfSilTraceVerts( tri, source.numVerts );
			for ( int i = 0; i < source.numVerts; ++i ) {
				tri->silTraceVerts[i].xyzw = source.silTraceVertexes[i];
			}
#else
			R_FreeStaticTriSurf( tri );
			return false;
#endif
		}
		if ( source.numSkinToModelTransforms > 0 ) {
#if defined( _MD5R_SUPPORT ) || defined( Q4SDK_MD5R )
			R_AllocStaticSkinToModelTransforms( tri, source.numSkinToModelTransforms );
			memcpy( tri->skinToModelTransforms, source.skinToModelTransforms.Ptr(),
				source.skinToModelTransforms.Num() * sizeof( source.skinToModelTransforms[0] ) );
#else
			R_FreeStaticTriSurf( tri );
			return false;
#endif
		}

		tri->numShadowIndexesNoFrontCaps = source.numShadowIndexesNoFrontCaps;
		tri->numShadowIndexesNoCaps = source.numShadowIndexesNoCaps;
		tri->shadowCapPlaneBits = source.shadowCapPlaneBits;
		tri->ambientViewCount = 0;
		tri->ambientSurface = NULL;
		tri->nextDeferredFree = NULL;
		tri->indexCache = NULL;
		tri->ambientCache = NULL;
		tri->lightingCache = NULL;
		tri->shadowCache = NULL;
		outTri = tri;
		return true;
	}
}

/*
================
idRenderModel::BoundsFromJoints
================
*/
bool idRenderModel::BoundsFromJoints( const idJointMat *joints, idBounds &bounds ) const {
	return false;
}

/*
================
idRenderModel::HasCollisionSurface
================
*/
bool idRenderModel::HasCollisionSurface( const struct renderEntity_s *ent ) const {
	return false;
}

/*
================
idRenderModelStatic::HasCollisionSurface
================
*/
bool idRenderModelStatic::HasCollisionSurface( const struct renderEntity_s *ent ) const {
	for ( int i = 0; i < surfaces.Num(); ++i ) {
		const modelSurface_t *surf = &surfaces[i];
		if ( surf->id == -1 || surf->shader == NULL ) {
			continue;
		}

		const idMaterial *shader = ent != NULL
			? R_RemapShaderBySkin( surf->shader, ent->customSkin, ent->customShader )
			: surf->shader;
		if ( shader != NULL && shader->IsDedicatedCollisionSurface() ) {
			return true;
		}
	}

	return false;
}

/*
================
idRenderModel::InstantiateDynamicModel
================
*/
idRenderModel *idRenderModel::InstantiateDynamicModel( const struct renderEntity_s *ent, const struct viewDef_s *view,
		idRenderModel *cachedModel, dword surfMask ) {
	return InstantiateDynamicModel( ent, view, cachedModel );
}

/*
================
idRenderModel::GetSkinSpaceToLocalMats
================
*/
const idJointMat *idRenderModel::GetSkinSpaceToLocalMats( void ) const {
	return NULL;
}

/*
================
idRenderModel::SetViewEntity
================
*/
void idRenderModel::SetViewEntity( const struct viewEntity_s *ve ) {
}

#if defined( _MD5R_SUPPORT )
/*
================
idRenderModel::HasSeparateSilTraceMeshes
================
*/
bool idRenderModel::HasSeparateSilTraceMeshes( void ) const {
	return false;
}
#endif

/*
================
idRenderModel::~idRenderModel
================
*/
idRenderModel::~idRenderModel() {
}

/*
================
idRenderModelStatic::idRenderModelStatic
================
*/
idRenderModelStatic::idRenderModelStatic() {
	name = "<undefined>";
	bounds.Clear();
	lastModifiedFrame = 0;
	lastArchivedFrame = 0;
	overlaysAdded = 0;
	shadowHull = NULL;
	isStaticWorldModel = false;
	defaulted = false;
	purged = false;
	fastLoad = false;
	reloadable = true;
	procSky = false;
	generatedCacheContent = false;
	levelLoadReferenced = false;
	timeStamp = 0;
}

/*
================
idRenderModelStatic::~idRenderModelStatic
================
*/
idRenderModelStatic::~idRenderModelStatic() {
	PurgeModel();
}

renderModelCacheType_t idRenderModelStatic::LevelLoadCachePayloadType() const {
	// Dynamic subclasses share the static model's surface container but have
	// additional runtime state that this payload intentionally does not encode.
	return IsDynamicModel() == DM_STATIC ? RENDER_MODEL_CACHE_STATIC : RENDER_MODEL_CACHE_UNSUPPORTED;
}

bool idRenderModelStatic::WriteLevelLoadCachePayload( idFile &file ) const {
	if ( defaulted || purged || !R_ModelCacheBoundsCanWrite( bounds )
		|| surfaces.Num() < 0 || surfaces.Num() > MODEL_CACHE_MAX_SURFACES ) {
		return false;
	}

	idRenderModelCacheWriter writer( file );
	if ( !writer.WriteUnsignedInt( STATIC_MODEL_CACHE_MAGIC ) || !writer.WriteInt( STATIC_MODEL_CACHE_VERSION )
		|| !writer.WriteString( name.c_str(), MODEL_CACHE_MAX_NAME )
		|| !writer.WriteUnsigned64( static_cast<uint64_t>( static_cast<int64_t>( timeStamp ) ) )
		|| !writer.WriteBounds( bounds ) || !writer.WriteBool( isStaticWorldModel )
		|| !writer.WriteBool( defaulted ) || !writer.WriteBool( purged ) || !writer.WriteBool( fastLoad )
		|| !writer.WriteBool( reloadable ) || !writer.WriteBool( procSky )
		|| !writer.WriteInt( surfaces.Num() ) ) {
		return false;
	}

	for ( int i = 0; i < surfaces.Num(); ++i ) {
		const modelSurface_t &surface = surfaces[i];
		const char *materialName = ( surface.shader != NULL && surface.shader->GetName() != NULL )
			? surface.shader->GetName() : "";
		if ( !writer.WriteInt( surface.id ) || !writer.WriteString( materialName, MODEL_CACHE_MAX_MATERIAL )
			|| !writer.WriteBool( surface.shader != NULL && surface.shader->UseUnsmoothedTangents() )
			|| !writer.WriteBool( surface.shader != NULL && surface.shader->ShouldCreateBackSides() )
			|| !writer.WriteBool( surface.shader != NULL && surface.shader->SurfaceCastsShadow() )
			|| !writer.WriteBool( surface.shader != NULL && surface.shader->IsDiscrete() )
			|| !writer.WriteBool( surface.geometry != NULL ) ) {
			return false;
		}
		if ( surface.geometry != NULL && !R_ModelCacheWriteTri( writer, *surface.geometry ) ) {
			return false;
		}
	}
	return writer.IsValid();
}

bool idRenderModelStatic::ReadLevelLoadCachePayload( idFile &file ) {
	idRenderModelCacheReader reader( file );
	unsigned int magic = 0;
	int version = 0;
	idStr decodedName;
	uint64_t encodedTimestamp = 0;
	idBounds decodedBounds;
	bool decodedStaticWorld = false;
	bool decodedDefaulted = false;
	bool decodedPurged = false;
	bool decodedFastLoad = false;
	bool decodedReloadable = false;
	bool decodedProcSky = false;
	int surfaceCount = 0;

	if ( !reader.ReadUnsignedInt( magic ) || magic != STATIC_MODEL_CACHE_MAGIC
		|| !reader.ReadInt( version ) || version != STATIC_MODEL_CACHE_VERSION
		|| !reader.ReadString( decodedName, MODEL_CACHE_MAX_NAME ) || decodedName.IsEmpty()
		|| !reader.ReadUnsigned64( encodedTimestamp ) || !reader.ReadBounds( decodedBounds, true )
		|| !reader.ReadBool( decodedStaticWorld ) || !reader.ReadBool( decodedDefaulted )
		|| !reader.ReadBool( decodedPurged ) || !reader.ReadBool( decodedFastLoad )
		|| !reader.ReadBool( decodedReloadable ) || !reader.ReadBool( decodedProcSky )
		|| decodedDefaulted || decodedPurged
		|| !reader.ReadCount( surfaceCount, MODEL_CACHE_MAX_SURFACES, sizeof( renderModelCacheSurfaceStage_t ) ) ) {
		return false;
	}

	idList<renderModelCacheSurfaceStage_t> decodedSurfaces;
	decodedSurfaces.SetNum( surfaceCount );
	for ( int i = 0; i < surfaceCount; ++i ) {
		renderModelCacheSurfaceStage_t &surface = decodedSurfaces[i];
		if ( !reader.ReadInt( surface.id ) || !reader.ReadString( surface.materialName, MODEL_CACHE_MAX_MATERIAL )
			|| !reader.ReadBool( surface.materialUnsmoothedTangents )
			|| !reader.ReadBool( surface.materialCreatesBackSides )
			|| !reader.ReadBool( surface.materialCastsShadow )
			|| !reader.ReadBool( surface.materialDiscrete )
			|| !reader.ReadBool( surface.hasGeometry )
			|| ( surface.materialName.IsEmpty() && ( surface.materialUnsmoothedTangents
				|| surface.materialCreatesBackSides || surface.materialCastsShadow || surface.materialDiscrete ) ) ) {
			return false;
		}
		if ( surface.hasGeometry && ( surface.materialName.IsEmpty()
			|| !R_ModelCacheReadTriStage( reader, surface.geometry ) ) ) {
			return false;
		}
	}
	if ( !reader.IsValid() ) {
		return false;
	}

	const ID_TIME_T decodedTimestamp = static_cast<ID_TIME_T>( static_cast<int64_t>( encodedTimestamp ) );
	if ( static_cast<uint64_t>( static_cast<int64_t>( decodedTimestamp ) ) != encodedTimestamp ) {
		return false;
	}

	idRenderModelStatic staged;
	staged.name = decodedName;
	staged.timeStamp = decodedTimestamp;
	staged.bounds = decodedBounds;
	staged.isStaticWorldModel = decodedStaticWorld;
	staged.defaulted = false;
	staged.purged = false;
	staged.fastLoad = decodedFastLoad;
	staged.reloadable = decodedReloadable;
	staged.procSky = decodedProcSky;
	staged.levelLoadReferenced = false;
	staged.overlaysAdded = 0;
	staged.lastModifiedFrame = 0;
	staged.lastArchivedFrame = 0;
	staged.shadowHull = NULL;
	staged.surfaces.SetNum( surfaceCount );
	for ( int i = 0; i < surfaceCount; ++i ) {
		staged.surfaces[i].id = 0;
		staged.surfaces[i].shader = NULL;
		staged.surfaces[i].geometry = NULL;
		staged.surfaces[i].mOriginalSurfaceName = NULL;
	}

	for ( int i = 0; i < surfaceCount; ++i ) {
		const renderModelCacheSurfaceStage_t &sourceSurface = decodedSurfaces[i];
		modelSurface_t &surface = staged.surfaces[i];
		surface.id = sourceSurface.id;
		surface.shader = sourceSurface.materialName.IsEmpty()
			? NULL : declManager->FindMaterial( sourceSurface.materialName.c_str() );
		surface.geometry = NULL;
		surface.mOriginalSurfaceName = NULL;
		if ( !sourceSurface.materialName.IsEmpty() && ( surface.shader == NULL
			|| idStr::Icmp( surface.shader->GetName(), sourceSurface.materialName.c_str() ) != 0
			|| surface.shader->UseUnsmoothedTangents() != sourceSurface.materialUnsmoothedTangents
			|| surface.shader->ShouldCreateBackSides() != sourceSurface.materialCreatesBackSides
			|| surface.shader->SurfaceCastsShadow() != sourceSurface.materialCastsShadow
			|| surface.shader->IsDiscrete() != sourceSurface.materialDiscrete ) ) {
			return false;
		}
		if ( sourceSurface.hasGeometry && ( surface.shader == NULL
			|| ( !decodedFastLoad && sourceSurface.geometry.verts.Num() > 0
				&& sourceSurface.materialUnsmoothedTangents
					!= ( sourceSurface.geometry.dominantTris.Num() > 0 ) )
			|| !R_ModelCacheMaterializeTri( sourceSurface.geometry, surface.geometry ) ) ) {
			return false;
		}
	}

	SwapLevelLoadCacheState( staged );
	return true;
}

void idRenderModelStatic::SwapLevelLoadCacheState( idRenderModelStatic &other ) {
	surfaces.Swap( other.surfaces );
	idSwap( bounds, other.bounds );
	idSwap( overlaysAdded, other.overlaysAdded );
	idSwap( lastModifiedFrame, other.lastModifiedFrame );
	idSwap( lastArchivedFrame, other.lastArchivedFrame );
	idSwap( name, other.name );
	idSwap( shadowHull, other.shadowHull );
	idSwap( isStaticWorldModel, other.isStaticWorldModel );
	idSwap( defaulted, other.defaulted );
	idSwap( purged, other.purged );
	idSwap( fastLoad, other.fastLoad );
	idSwap( reloadable, other.reloadable );
	idSwap( procSky, other.procSky );
	idSwap( levelLoadReferenced, other.levelLoadReferenced );
	idSwap( timeStamp, other.timeStamp );
}

/*
==============
idRenderModelStatic::Print
==============
*/
void idRenderModelStatic::Print() const {
	common->Printf( "%s\n", name.c_str() );
	common->Printf( "Static model.\n" );
	common->Printf( "bounds: (%f %f %f) to (%f %f %f)\n", 
		bounds[0][0], bounds[0][1], bounds[0][2], 
		bounds[1][0], bounds[1][1], bounds[1][2] );

	common->Printf( "    verts  tris material\n" );
	for ( int i = 0 ; i < NumSurfaces() ; i++ ) {
		const modelSurface_t	*surf = Surface( i );

		srfTriangles_t *tri = surf->geometry;
		const idMaterial *material = surf->shader;
		
		if ( !tri ) {
			common->Printf( "%2i: %s, NULL surface geometry\n", i, material->GetName() );
			continue;
		}

		common->Printf( "%2i: %5i %5i %s", i, tri->numVerts, tri->numIndexes / 3, material->GetName() );
		if ( tri->generateNormals ) {
			common->Printf( " (smoothed)\n" );
		} else {
			common->Printf( "\n" );
		}
	}	
}

/*
==============
idRenderModelStatic::Memory
==============
*/
int idRenderModelStatic::Memory() const {
	size_t	totalBytes = 0;

	totalBytes += sizeof( *this );
	totalBytes += name.DynamicMemoryUsed();
	totalBytes += surfaces.MemoryUsed();

	if ( shadowHull ) {
		totalBytes += R_TriSurfMemory( shadowHull );
	}

	for ( int j = 0 ; j < NumSurfaces() ; j++ ) {
		const modelSurface_t	*surf = Surface( j );
		if ( !surf->geometry ) {
			continue;
		}
		totalBytes += R_TriSurfMemory( surf->geometry );
	}

	return idLib::SizeToInt( totalBytes, "idRenderModelStatic::Memory" );
}

/*
==============
idRenderModelStatic::List
==============
*/
void idRenderModelStatic::List() const {
	int	totalTris = 0;
	int	totalVerts = 0;
	int	totalBytes = 0;

	totalBytes = Memory();

	char	closed = 'C';
	for ( int j = 0 ; j < NumSurfaces() ; j++ ) {
		const modelSurface_t	*surf = Surface( j );
		if ( !surf->geometry ) {
			continue;
		}
		if ( !surf->geometry->perfectHull ) {
			closed = ' ';
		}
		totalTris += surf->geometry->numIndexes / 3;
		totalVerts += surf->geometry->numVerts;
	}
	common->Printf( "%c%4ik %3i %4i %4i %s", closed, totalBytes/1024, NumSurfaces(), totalVerts, totalTris, Name() );

	if ( IsDynamicModel() == DM_CACHED ) {
		common->Printf( " (DM_CACHED)" );
	}
	if ( IsDynamicModel() == DM_CONTINUOUS ) {
		common->Printf( " (DM_CONTINUOUS)" );
	}
	if ( defaulted ) {
		common->Printf( " (DEFAULTED)" );
	}
	if ( bounds[0][0] >= bounds[1][0] ) {
		common->Printf( " (EMPTY BOUNDS)" );
	}
	if ( bounds[1][0] - bounds[0][0] > 100000 ) {
		common->Printf( " (HUGE BOUNDS)" );
	}

	common->Printf( "\n" );
}

/*
================
idRenderModelStatic::IsDefaultModel
================
*/
bool idRenderModelStatic::IsDefaultModel() const {
	return defaulted;
}

/*
================
AddCubeFace
================
*/
static void AddCubeFace( srfTriangles_t *tri, idVec3 v1, idVec3 v2, idVec3 v3, idVec3 v4 ) {
	tri->verts[tri->numVerts+0].Clear();
	tri->verts[tri->numVerts+0].xyz = v1 * 8;
	tri->verts[tri->numVerts+0].st[0] = 0;
	tri->verts[tri->numVerts+0].st[1] = 0;

	tri->verts[tri->numVerts+1].Clear();
	tri->verts[tri->numVerts+1].xyz = v2 * 8;
	tri->verts[tri->numVerts+1].st[0] = 1;
	tri->verts[tri->numVerts+1].st[1] = 0;

	tri->verts[tri->numVerts+2].Clear();
	tri->verts[tri->numVerts+2].xyz = v3 * 8;
	tri->verts[tri->numVerts+2].st[0] = 1;
	tri->verts[tri->numVerts+2].st[1] = 1;

	tri->verts[tri->numVerts+3].Clear();
	tri->verts[tri->numVerts+3].xyz = v4 * 8;
	tri->verts[tri->numVerts+3].st[0] = 0;
	tri->verts[tri->numVerts+3].st[1] = 1;

	tri->indexes[tri->numIndexes+0] = tri->numVerts + 0;
	tri->indexes[tri->numIndexes+1] = tri->numVerts + 1;
	tri->indexes[tri->numIndexes+2] = tri->numVerts + 2;
	tri->indexes[tri->numIndexes+3] = tri->numVerts + 0;
	tri->indexes[tri->numIndexes+4] = tri->numVerts + 2;
	tri->indexes[tri->numIndexes+5] = tri->numVerts + 3;

	tri->numVerts += 4;
	tri->numIndexes += 6;
}

/*
================
idRenderModelStatic::MakeDefaultModel
================
*/
void idRenderModelStatic::MakeDefaultModel() {

	defaulted = true;

	// throw out any surfaces we already have
	PurgeModel();

	// create one new surface
	modelSurface_t	surf = {};

	srfTriangles_t *tri = R_AllocStaticTriSurf();

	surf.shader = tr.defaultMaterial;
	surf.geometry = tri;

	R_AllocStaticTriSurfVerts( tri, 24 );
	R_AllocStaticTriSurfIndexes( tri, 36 );

	AddCubeFace( tri, idVec3(-1, 1, 1), idVec3(1, 1, 1), idVec3(1, -1, 1), idVec3(-1, -1, 1) );
	AddCubeFace( tri, idVec3(-1, 1, -1), idVec3(-1, -1, -1), idVec3(1, -1, -1), idVec3(1, 1, -1) );

	AddCubeFace( tri, idVec3(1, -1, 1), idVec3(1, 1, 1), idVec3(1, 1, -1), idVec3(1, -1, -1) );
	AddCubeFace( tri, idVec3(-1, -1, 1), idVec3(-1, -1, -1), idVec3(-1, 1, -1), idVec3(-1, 1, 1) );

	AddCubeFace( tri, idVec3(-1, -1, 1), idVec3(1, -1, 1), idVec3(1, -1, -1), idVec3(-1, -1, -1) );
	AddCubeFace( tri, idVec3(-1, 1, 1), idVec3(-1, 1, -1), idVec3(1, 1, -1), idVec3(1, 1, 1) );

	tri->generateNormals = true;

	AddSurface( surf );
	FinishSurfaces();
}

/*
================
idRenderModelStatic::PartialInitFromFile
================
*/
void idRenderModelStatic::PartialInitFromFile( const char *fileName ) {
	fastLoad = true;
	InitFromFile( fileName );
}

/*
================
idRenderModelStatic::InitFromFile
================
*/
void idRenderModelStatic::InitFromFile( const char *fileName ) {
	bool loaded;
	idStr extension;
	const bool requestedFastLoad = fastLoad;

	InitEmpty( fileName );
	char cacheSettingsBuffer[1024];
	idStr::snPrintf( cacheSettingsBuffer, sizeof( cacheSettingsBuffer ),
		"static-payload=2;merge=%s;slopv=%s;slopt=%s;slopn=%s;silremap=%s;binary=%s;force-md5r=%s;convert-static=%s;fast=%d",
		r_mergeModelSurfaces.GetString(), r_slopVertex.GetString(), r_slopTexCoord.GetString(),
		r_slopNormal.GetString(), cvarSystem->GetCVarString( "r_useSilRemap" ),
		cvarSystem->GetCVarString( "com_binaryRead" ), cvarSystem->GetCVarString( "r_forceConvertMD5R" ),
		cvarSystem->GetCVarString( "r_convertStaticToMD5R" ), requestedFastLoad ? 1 : 0 );
	const idStr cacheSettings = cacheSettingsBuffer;
	if ( R_TryReadGeneratedRenderModelCache( *this, fileName,
			STATIC_MODEL_GENERATED_CACHE_PARSER_VERSION, cacheSettings.c_str() ) ) {
		if ( name.Icmp( fileName ) == 0 && fastLoad == requestedFastLoad ) {
			return;
		}
		fileSystem->DiscardGeneratedCache( GENERATED_CACHE_RENDER_MODEL, fileName,
			STATIC_MODEL_GENERATED_CACHE_PARSER_VERSION, cacheSettings.c_str() );
		InitEmpty( fileName );
		fastLoad = requestedFastLoad;
	}

	// FIXME: load new .proc map format

	name.ExtractFileExtension( extension );

	if ( extension.Icmp( "ase" ) == 0 ) {
		loaded		= LoadASE( name );
		reloadable	= true;
	} else if ( extension.Icmp( "lwo" ) == 0 ) {
		loaded		= LoadLWO( name );
		reloadable	= true;
	} else if ( extension.Icmp( "flt" ) == 0 ) {
		loaded		= LoadFLT( name );
		reloadable	= true;
	} else if ( extension.Icmp( "ma" ) == 0 ) {
		loaded		= LoadMA( name );
		reloadable	= true;
	} else {
		common->Warning( "idRenderModelStatic::InitFromFile: unknown type for model: \'%s\'", name.c_str() );
		loaded		= false;
	}

	if ( !loaded ) {
		common->Warning( "Couldn't load model: '%s'", name.c_str() );
		MakeDefaultModel();
		return;
	}

	// it is now available for use
	purged = false;

	// create the bounds for culling and dynamic surface creation
	FinishSurfaces();
	R_WriteGeneratedRenderModelCache( *this, fileName,
		STATIC_MODEL_GENERATED_CACHE_PARSER_VERSION, cacheSettings.c_str() );
}

/*
================
idRenderModelStatic::LoadModel
================
*/
void idRenderModelStatic::LoadModel() {
	PurgeModel();
	InitFromFile( name );
}

/*
================
idRenderModelStatic::InitEmpty
================
*/
void idRenderModelStatic::InitEmpty( const char *fileName ) {
	// model names of the form _area* are static parts of the
	// world, and have already been considered for optimized shadows
	// other model names are inline entity models, and need to be
	// shadowed normally
	if ( !idStr::Cmpn( fileName, "_area", 5 ) ) {
		isStaticWorldModel = true;
	} else {
		isStaticWorldModel = false;
	}

	name = fileName;
	reloadable = false;	// if it didn't come from a file, we can't reload it
	PurgeModel();
	purged = false;
	procSky = false;
	bounds.Zero();
}

/*
================
idRenderModelStatic::AddSurface
================
*/
void idRenderModelStatic::AddSurface( modelSurface_t surface ) {
	surfaces.Append( surface );
	if ( surface.geometry ) {
		bounds += surface.geometry->bounds;
	}
}

/*
================
idRenderModelStatic::Name
================
*/
const char *idRenderModelStatic::Name() const {
	return name;
}

/*
================
idRenderModelStatic::SetProcSky
================
*/
void idRenderModelStatic::SetProcSky( bool value ) {
	procSky = value;
}

/*
================
idRenderModelStatic::HasProcSky
================
*/
bool idRenderModelStatic::HasProcSky() const {
	return procSky;
}

/*
================
idRenderModelStatic::Timestamp
================
*/
ID_TIME_T idRenderModelStatic::Timestamp() const {
	return timeStamp;
}

/*
================
idRenderModelStatic::NumSurfaces
================
*/
int idRenderModelStatic::NumSurfaces() const {
	return surfaces.Num();
}

/*
================
idRenderModelStatic::NumBaseSurfaces
================
*/
int idRenderModelStatic::NumBaseSurfaces() const {
	return surfaces.Num() - overlaysAdded;
}

/*
================
idRenderModelStatic::Surface
================
*/
const modelSurface_t *idRenderModelStatic::Surface( int surfaceNum ) const {
	return &surfaces[surfaceNum];
}

/*
================
idRenderModelStatic::AllocSurfaceTriangles
================
*/
srfTriangles_t *idRenderModelStatic::AllocSurfaceTriangles( int numVerts, int numIndexes ) const {
	srfTriangles_t *tri = R_AllocStaticTriSurf();
	R_AllocStaticTriSurfVerts( tri, numVerts );
	R_AllocStaticTriSurfIndexes( tri, numIndexes );
	return tri;
}

/*
================
idRenderModelStatic::FreeSurfaceTriangles
================
*/
void idRenderModelStatic::FreeSurfaceTriangles( srfTriangles_t *tris ) const {
	R_FreeStaticTriSurf( tris );
}

/*
================
idRenderModelStatic::ShadowHull
================
*/
srfTriangles_t *idRenderModelStatic::ShadowHull() const {
	return shadowHull;
}

/*
================
idRenderModelStatic::IsStaticWorldModel
================
*/
bool idRenderModelStatic::IsStaticWorldModel() const {
	return isStaticWorldModel;
}

/*
================
idRenderModelStatic::IsDynamicModel
================
*/
dynamicModel_t idRenderModelStatic::IsDynamicModel() const {
	// dynamic subclasses will override this
	return DM_STATIC;
}

/*
================
idRenderModelStatic::IsReloadable
================
*/
bool idRenderModelStatic::IsReloadable() const {
	return reloadable;
}

/*
================
idRenderModelStatic::Bounds
================
*/
idBounds idRenderModelStatic::Bounds( const struct renderEntity_s *mdef ) const {
	return bounds;
}

/*
================
idRenderModelStatic::DepthHack
================
*/
float idRenderModelStatic::DepthHack() const {
	return 0.0f;
}

/*
================
idRenderModelStatic::InstantiateDynamicModel
================
*/
idRenderModel *idRenderModelStatic::InstantiateDynamicModel( const struct renderEntity_s *ent, const struct viewDef_s *view, idRenderModel *cachedModel ) {
	if ( cachedModel ) {
		delete cachedModel;
		cachedModel = NULL;
	}
	common->Error( "InstantiateDynamicModel called on static model '%s'", name.c_str() );
	return NULL;
}

/*
================
idRenderModelStatic::NumJoints
================
*/
int idRenderModelStatic::NumJoints( void ) const {
	return 0;
}

/*
================
idRenderModelStatic::GetJoints
================
*/
const idMD5Joint *idRenderModelStatic::GetJoints( void ) const {
	return NULL;
}

/*
================
idRenderModelStatic::GetJointHandle
================
*/
jointHandle_t idRenderModelStatic::GetJointHandle( const char *name ) const {
	return INVALID_JOINT;
}

/*
================
idRenderModelStatic::GetJointName
================
*/
const char * idRenderModelStatic::GetJointName( jointHandle_t handle ) const {
	return "";
}

/*
================
idRenderModelStatic::GetDefaultPose
================
*/
const idJointQuat *idRenderModelStatic::GetDefaultPose( void ) const {
	return NULL;
}

/*
================
idRenderModelStatic::NearestJoint
================
*/
int idRenderModelStatic::NearestJoint( int surfaceId, int a, int b, int c ) const {
	return INVALID_JOINT;
}


//=====================================================================


/*
================
idRenderModelStatic::FinishSurfaces

The mergeShadows option allows surfaces with different textures to share
silhouette edges for shadow calculation, instead of leaving shared edges
hanging.

If any of the original shaders have the noSelfShadow flag set, the surfaces
can't be merged, because they will need to be drawn in different order.

If there is only one surface, a separate merged surface won't be generated.

A model with multiple surfaces can't later have a skinned shader change the
state of the noSelfShadow flag.

-----------------

Creates mirrored copies of two sided surfaces with normal maps, which would
otherwise light funny.

Extends the bounds of deformed surfaces so they don't cull incorrectly at screen edges.

================
*/
void idRenderModelStatic::FinishSurfaces() {
	FinishSurfaces( true );
}

void idRenderModelStatic::FinishSurfaces( bool createBackSides ) {
	int			i;
	int			totalVerts, totalIndexes;

	purged = false;

	// make sure we don't have a huge bounds even if we don't finish everything
	bounds.Zero();

	if ( surfaces.Num() == 0 ) {
		return;
	}

	// renderBump doesn't care about most of this
	if ( fastLoad ) {
		bounds.Zero();
		for ( i = 0 ; i < surfaces.Num() ; i++ ) {
			const modelSurface_t	*surf = &surfaces[i];

			R_BoundTriSurf( surf->geometry );
			bounds.AddBounds( surf->geometry->bounds );
		}

		return;
	}

	// cleanup all the final surfaces, but don't create sil edges
	totalVerts = 0;
	totalIndexes = 0;

	// decide if we are going to merge all the surfaces into one shadower
	int	numOriginalSurfaces = surfaces.Num();

	// make sure there aren't any NULL shaders or geometry
	for ( i = 0 ; i < numOriginalSurfaces ; i++ ) {
		const modelSurface_t	*surf = &surfaces[i];

		if ( surf->geometry == NULL || surf->shader == NULL ) {
			MakeDefaultModel();
			common->Error( "Model %s, surface %i had NULL geometry", name.c_str(), i );
		}
		if ( surf->shader == NULL ) {
			MakeDefaultModel();
			common->Error( "Model %s, surface %i had NULL shader", name.c_str(), i );
		}
	}

	// duplicate and reverse triangles for two sided bump mapped surfaces
	// note that this won't catch surfaces that have their shaders dynamically
	// changed, and won't work with animated models.
	// It is better to create completely separate surfaces, rather than
	// add vertexes and indexes to the existing surface, because the
	// tangent generation wouldn't like the acute shared edges
	for ( i = 0 ; i < numOriginalSurfaces && createBackSides ; i++ ) {
		const modelSurface_t	*surf = &surfaces[i];

		if ( surf->shader->ShouldCreateBackSides() ) {
			srfTriangles_t *newTri;

			newTri = R_CopyStaticTriSurf( surf->geometry );
			R_ReverseTriangles( newTri );

			modelSurface_t	newSurf = {};

			newSurf.id = NumSurfaces();	// its index, as the converters and ParseModel number surfaces
			newSurf.shader = surf->shader;
			newSurf.geometry = newTri;

			AddSurface( newSurf );
		}
	}

	// clean the surfaces
	for ( i = 0 ; i < surfaces.Num() ; i++ ) {
		const modelSurface_t	*surf = &surfaces[i];

		R_CleanupTriangles( surf->geometry, surf->geometry->generateNormals, true, surf->shader->UseUnsmoothedTangents() );
		if ( surf->shader->SurfaceCastsShadow() ) {
			totalVerts += surf->geometry->numVerts;
			totalIndexes += surf->geometry->numIndexes;
		}
	}

	// add up the total surface area for development information
	for ( i = 0 ; i < surfaces.Num() ; i++ ) {
		const modelSurface_t	*surf = &surfaces[i];
		srfTriangles_t	*tri = surf->geometry;

		for ( int j = 0 ; j < tri->numIndexes ; j += 3 ) {
			float	area = idWinding::TriangleArea( tri->verts[tri->indexes[j]].xyz,
				 tri->verts[tri->indexes[j+1]].xyz,  tri->verts[tri->indexes[j+2]].xyz );
			const_cast<idMaterial *>(surf->shader)->AddToSurfaceArea( area );
		}
	}

	// calculate the bounds
	if ( surfaces.Num() == 0 ) {
		bounds.Zero();
	} else {
		bounds.Clear();
		for ( i = 0 ; i < surfaces.Num() ; i++ ) {
			modelSurface_t	*surf = &surfaces[i];

			// if the surface has a deformation, increase the bounds
			// the amount here is somewhat arbitrary, designed to handle
			// autosprites and flares, but could be done better with exact
			// deformation information.
			// Note that this doesn't handle deformations that are skinned in
			// at run time...
			if ( surf->shader->Deform() != DFRM_NONE ) {
				srfTriangles_t	*tri = surf->geometry;
				idVec3	mid = ( tri->bounds[1] + tri->bounds[0] ) * 0.5f;
				float	radius = ( tri->bounds[0] - mid ).Length();
				radius += 20.0f;

				tri->bounds[0][0] = mid[0] - radius;
				tri->bounds[0][1] = mid[1] - radius;
				tri->bounds[0][2] = mid[2] - radius;

				tri->bounds[1][0] = mid[0] + radius;
				tri->bounds[1][1] = mid[1] + radius;
				tri->bounds[1][2] = mid[2] + radius;
			}

			// add to the model bounds
			bounds.AddBounds( surf->geometry->bounds );

		}
	}
}

/*
=================
idRenderModelStatic::ConvertASEToModelSurfaces
=================
*/
typedef struct matchVert_s {
	struct matchVert_s	*next;
	int		v, tv;
	byte	color[4];
	idVec3	normal;
} matchVert_t;

bool idRenderModelStatic::ConvertASEToModelSurfaces( const struct aseModel_s *ase ) {
	aseObject_t *	object;
	aseMesh_t *		mesh;
	aseMaterial_t *	material;
	const idMaterial *im1, *im2;
	srfTriangles_t *tri;
	int				objectNum;
	int				i, j, k;
	int				v, tv;
	int *			vRemap;
	int *			tvRemap;
	matchVert_t *	mvTable;	// all of the match verts
	matchVert_t **	mvHash;		// points inside mvTable for each xyz index
	matchVert_t *	lastmv;
	matchVert_t *	mv;
	idVec3			normal;
	float			uOffset, vOffset, textureSin, textureCos;
	float			uTiling, vTiling;
	int *			mergeTo;
	byte *			color;
	static byte	identityColor[4] = { 255, 255, 255, 255 };
	modelSurface_t	surf = {}, *modelSurf;

	if ( !ase ) {
		return false;
	}
	if ( ase->objects.Num() < 1 ) {
		return false;
	}

	timeStamp = ase->timeStamp;

	// the modeling programs can save out multiple surfaces with a common
	// material, but we would like to mege them together where possible
	// meaning that this->NumSurfaces() <= ase->objects.currentElements
	mergeTo = (int *)_alloca( ase->objects.Num() * sizeof( *mergeTo ) ); 
	surf.geometry = NULL;
	if ( ase->materials.Num() == 0 ) {
		// if we don't have any materials, dump everything into a single surface
		surf.shader = tr.defaultMaterial;
		surf.id = 0;
		this->AddSurface( surf );
		for ( i = 0 ; i < ase->objects.Num() ; i++ ) { 
			mergeTo[i] = 0;
		}
	} else if ( !r_mergeModelSurfaces.GetBool() ) {
		// don't merge any
		for ( i = 0 ; i < ase->objects.Num() ; i++ ) { 
			mergeTo[i] = i;
			object = ase->objects[i];
			material = ase->materials[object->materialRef];
			surf.shader = declManager->FindMaterial( material->name );
			surf.id = this->NumSurfaces();
			this->AddSurface( surf );
		}
	} else {
		// search for material matches
		for ( i = 0 ; i < ase->objects.Num() ; i++ ) { 
			object = ase->objects[i];
			material = ase->materials[object->materialRef];
			im1 = declManager->FindMaterial( material->name );
			if ( im1->IsDiscrete() ) {
				// flares, autosprites, etc
				j = this->NumSurfaces();
			} else {
				for ( j = 0 ; j < this->NumSurfaces() ; j++ ) {
					modelSurf = &this->surfaces[j];
					im2 = modelSurf->shader;
					if ( im1 == im2 ) {
						// merge this
						mergeTo[i] = j;
						break;
					}
				}
			}
			if ( j == this->NumSurfaces() ) {
				// didn't merge
				mergeTo[i] = j;
				surf.shader = im1;
				surf.id = this->NumSurfaces();
				this->AddSurface( surf );
			}
		}
	}

	idVectorSubset<idVec3, 3> vertexSubset;
	idVectorSubset<idVec2, 2> texCoordSubset;

	// build the surfaces
	for ( objectNum = 0 ; objectNum < ase->objects.Num() ; objectNum++ ) {
		object = ase->objects[objectNum];
		mesh = &object->mesh;
		material = ase->materials[object->materialRef];
		im1 = declManager->FindMaterial( material->name );

		bool normalsParsed = mesh->normalsParsed;

		// completely ignore any explict normals on surfaces with a renderbump command
		// which will guarantee the best contours and least vertexes.
		const char *rb = im1->GetRenderBump();
		if ( rb && rb[0] ) {
			normalsParsed = false;
		}

		// It seems like the tools our artists are using often generate
		// verts and texcoords slightly separated that should be merged
		// note that we really should combine the surfaces with common materials
		// before doing this operation, because we can miss a slop combination
		// if they are in different surfaces

		vRemap = (int *)R_StaticAlloc( mesh->numVertexes * sizeof( vRemap[0] ) );

		if ( fastLoad ) {
			// renderbump doesn't care about vertex count
			for ( j = 0; j < mesh->numVertexes; j++ ) {
				vRemap[j] = j;
			}
		} else {
			float vertexEpsilon = r_slopVertex.GetFloat();
			float expand = 2 * 32 * vertexEpsilon;
			idVec3 mins, maxs;

			SIMDProcessor->MinMax( mins, maxs, mesh->vertexes, mesh->numVertexes );
			mins -= idVec3( expand, expand, expand );
			maxs += idVec3( expand, expand, expand );
			vertexSubset.Init( mins, maxs, 32, 1024 );
			for ( j = 0; j < mesh->numVertexes; j++ ) {
				vRemap[j] = vertexSubset.FindVector( mesh->vertexes, j, vertexEpsilon );
			}
		}

		tvRemap = (int *)R_StaticAlloc( mesh->numTVertexes * sizeof( tvRemap[0] ) );

		if ( fastLoad ) {
			// renderbump doesn't care about vertex count
			for ( j = 0; j < mesh->numTVertexes; j++ ) {
				tvRemap[j] = j;
			}
		} else {
			float texCoordEpsilon = r_slopTexCoord.GetFloat();
			float expand = 2 * 32 * texCoordEpsilon;
			idVec2 mins, maxs;

			SIMDProcessor->MinMax( mins, maxs, mesh->tvertexes, mesh->numTVertexes );
			mins -= idVec2( expand, expand );
			maxs += idVec2( expand, expand );
			texCoordSubset.Init( mins, maxs, 32, 1024 );
			for ( j = 0; j < mesh->numTVertexes; j++ ) {
				tvRemap[j] = texCoordSubset.FindVector( mesh->tvertexes, j, texCoordEpsilon );
			}
		}

		// we need to find out how many unique vertex / texcoord combinations
		// there are, because ASE tracks them separately but we need them unified

		// the maximum possible number of combined vertexes is the number of indexes
		mvTable = (matchVert_t *)R_ClearedStaticAlloc( mesh->numFaces * 3 * sizeof( mvTable[0] ) );

		// we will have a hash chain based on the xyz values
		mvHash = (matchVert_t **)R_ClearedStaticAlloc( mesh->numVertexes * sizeof( mvHash[0] ) );

		// allocate triangle surface
		tri = R_AllocStaticTriSurf();
		tri->numVerts = 0;
		tri->numIndexes = 0;
		R_AllocStaticTriSurfIndexes( tri, mesh->numFaces * 3 );
		tri->generateNormals = !normalsParsed;

		// init default normal, color and tex coord index
		normal.Zero();
		color = identityColor;
		tv = 0;

		// find all the unique combinations
		float normalEpsilon = 1.0f - r_slopNormal.GetFloat();
		for ( j = 0; j < mesh->numFaces; j++ ) {
			for ( k = 0; k < 3; k++ ) {
				v = mesh->faces[j].vertexNum[k];

				if ( v < 0 || v >= mesh->numVertexes ) {
					common->Error( "ConvertASEToModelSurfaces: bad vertex index in ASE file %s", name.c_str() );
				}

				// collapse the position if it was slightly offset 
				v = vRemap[v];

				// we may or may not have texcoords to compare
				if ( mesh->numTVFaces == mesh->numFaces && mesh->numTVertexes != 0 ) {
					tv = mesh->faces[j].tVertexNum[k];
					if ( tv < 0 || tv >= mesh->numTVertexes ) {
						common->Error( "ConvertASEToModelSurfaces: bad tex coord index in ASE file %s", name.c_str() );
					}
					// collapse the tex coord if it was slightly offset
					tv = tvRemap[tv];
				}

				// we may or may not have normals to compare
				if ( normalsParsed ) {
					normal = mesh->faces[j].vertexNormals[k];
				}

				// we may or may not have colors to compare
				if ( mesh->colorsParsed ) {
					color = mesh->faces[j].vertexColors[k];
				}

				// find a matching vert
				for ( lastmv = NULL, mv = mvHash[v]; mv != NULL; lastmv = mv, mv = mv->next ) {
					if ( mv->tv != tv ) {
						continue;
					}
					if ( *(unsigned *)mv->color != *(unsigned *)color ) {
						continue;
					}
					if ( !normalsParsed ) {
						// if we are going to create the normals, just
						// matching texcoords is enough
						break;
					}
					if ( mv->normal * normal > normalEpsilon ) {
						break;		// we already have this one
					}
				}
				if ( !mv ) {
					// allocate a new match vert and link to hash chain
					mv = &mvTable[ tri->numVerts ];
					mv->v = v;
					mv->tv = tv;
					mv->normal = normal;
					*(unsigned *)mv->color = *(unsigned *)color;
					mv->next = NULL;
					if ( lastmv ) {
						lastmv->next = mv;
					} else {
						mvHash[v] = mv;
					}
					tri->numVerts++;
				}

				tri->indexes[tri->numIndexes] = mv - mvTable;
				tri->numIndexes++;
			}
		}

		// allocate space for the indexes and copy them
		if ( tri->numIndexes > mesh->numFaces * 3 ) {
			common->FatalError( "ConvertASEToModelSurfaces: index miscount in ASE file %s", name.c_str() );
		}
		if ( tri->numVerts > mesh->numFaces * 3 ) {
			common->FatalError( "ConvertASEToModelSurfaces: vertex miscount in ASE file %s", name.c_str() );
		}

		// an ASE allows the texture coordinates to be scaled, translated, and rotated
		if ( ase->materials.Num() == 0 ) {
			uOffset = vOffset = 0.0f;
			uTiling = vTiling = 1.0f;
			textureSin = 0.0f;
			textureCos = 1.0f;
		} else {
			material = ase->materials[object->materialRef];
			uOffset = -material->uOffset;
			vOffset = material->vOffset;
			uTiling = material->uTiling;
			vTiling = material->vTiling;
			textureSin = idMath::Sin( material->angle );
			textureCos = idMath::Cos( material->angle );
		}

		// now allocate and generate the combined vertexes
		R_AllocStaticTriSurfVerts( tri, tri->numVerts );
		for ( j = 0; j < tri->numVerts; j++ ) {
			mv = &mvTable[j];
			tri->verts[ j ].Clear();
			tri->verts[ j ].xyz = mesh->vertexes[ mv->v ];
			tri->verts[ j ].normal = mv->normal;
			*(unsigned *)tri->verts[j].color = *(unsigned *)mv->color;
			if ( mesh->numTVFaces == mesh->numFaces && mesh->numTVertexes != 0 ) {
				const idVec2 &tv = mesh->tvertexes[ mv->tv ];
				float u = tv.x * uTiling + uOffset;
				float v = tv.y * vTiling + vOffset;
				tri->verts[ j ].st[0] = u * textureCos + v * textureSin;
				tri->verts[ j ].st[1] = u * -textureSin + v * textureCos;
			}
		}

		R_StaticFree( mvTable );
		R_StaticFree( mvHash );
		R_StaticFree( tvRemap );
		R_StaticFree( vRemap );

		// see if we need to merge with a previous surface of the same material
		modelSurf = &this->surfaces[mergeTo[ objectNum ]];
		srfTriangles_t	*mergeTri = modelSurf->geometry;
		if ( !mergeTri ) {
			modelSurf->geometry = tri;
		} else {
			modelSurf->geometry = R_MergeTriangles( mergeTri, tri );
			R_FreeStaticTriSurf( tri );
			R_FreeStaticTriSurf( mergeTri );
		}
	}

	return true;
}

/*
=================
idRenderModelStatic::ConvertLWOToModelSurfaces
=================
*/
bool idRenderModelStatic::ConvertLWOToModelSurfaces( const struct st_lwObject *lwo ) {
	const idMaterial *im1, *im2;
	srfTriangles_t	*tri;
	lwSurface *		lwoSurf;
	int				numTVertexes;
	int				i, j, k;
	int				v, tv;
	idVec3 *		vList;
	int *			vRemap;
	idVec2 *		tvList;
	int *			tvRemap;
	matchVert_t *	mvTable;	// all of the match verts
	matchVert_t **	mvHash;		// points inside mvTable for each xyz index
	matchVert_t *	lastmv;
	matchVert_t *	mv;
	idVec3			normal;
	int *			mergeTo;
	byte			color[4];
	modelSurface_t	surf = {}, *modelSurf;

	if ( !lwo ) {
		return false;
	}
	if ( lwo->surf == NULL ) {
		return false;
	}

	timeStamp = lwo->timeStamp;

	// count the number of surfaces
	i = 0;
	for ( lwoSurf = lwo->surf; lwoSurf; lwoSurf = lwoSurf->next ) {
		i++;
	}

	// the modeling programs can save out multiple surfaces with a common
	// material, but we would like to merge them together where possible
	mergeTo = (int *)_alloca( i * sizeof( mergeTo[0] ) ); 
	memset( &surf, 0, sizeof( surf ) );

	if ( !r_mergeModelSurfaces.GetBool() ) {
		// don't merge any
		for ( lwoSurf = lwo->surf, i = 0; lwoSurf; lwoSurf = lwoSurf->next, i++ ) {
			mergeTo[i] = i;
			surf.shader = declManager->FindMaterial( lwoSurf->name );
			surf.id = this->NumSurfaces();
			this->AddSurface( surf );
		}
	} else {
		// search for material matches
		for ( lwoSurf = lwo->surf, i = 0; lwoSurf; lwoSurf = lwoSurf->next, i++ ) {
			im1 = declManager->FindMaterial( lwoSurf->name );
			if ( im1->IsDiscrete() ) {
				// flares, autosprites, etc
				j = this->NumSurfaces();
			} else {
				for ( j = 0 ; j < this->NumSurfaces() ; j++ ) {
					modelSurf = &this->surfaces[j];
					im2 = modelSurf->shader;
					if ( im1 == im2 ) {
						// merge this
						mergeTo[i] = j;
						break;
					}
				}
			}
			if ( j == this->NumSurfaces() ) {
				// didn't merge
				mergeTo[i] = j;
				surf.shader = im1;
				surf.id = this->NumSurfaces();
				this->AddSurface( surf );
			}
		}
	}

	idVectorSubset<idVec3, 3> vertexSubset;
	idVectorSubset<idVec2, 2> texCoordSubset;

	// we only ever use the first layer
	lwLayer *layer = lwo->layer;

	// vertex positions
	if ( layer->point.count <= 0 ) {
		common->Warning( "ConvertLWOToModelSurfaces: model \'%s\' has bad or missing vertex data", name.c_str() );
		return false;
	}

	vList = (idVec3 *)R_StaticAlloc( layer->point.count * sizeof( vList[0] ) );
	for ( j = 0; j < layer->point.count; j++ ) {
		vList[j].x = layer->point.pt[j].pos[0];
		vList[j].y = layer->point.pt[j].pos[2];
		vList[j].z = layer->point.pt[j].pos[1];
	}

	// vertex texture coords
	numTVertexes = 0;

	if ( layer->nvmaps ) {
		for( lwVMap *vm = layer->vmap; vm; vm = vm->next ) {
			if ( vm->type == LWID_('T','X','U','V') ) {
				numTVertexes += vm->nverts;
			}
		}
	}

	if ( numTVertexes ) {
		tvList = (idVec2 *)Mem_Alloc( numTVertexes * sizeof( tvList[0] ) );
		int offset = 0;
		for( lwVMap *vm = layer->vmap; vm; vm = vm->next ) {
			if ( vm->type == LWID_('T','X','U','V') ) {
	  			vm->offset = offset;
		  		for ( k = 0; k < vm->nverts; k++ ) {
		  		   	tvList[k + offset].x = vm->val[k][0];
					tvList[k + offset].y = 1.0f - vm->val[k][1];	// invert the t
		  		}
			  	offset += vm->nverts;
			}
		}
	} else {
		common->Warning( "ConvertLWOToModelSurfaces: model \'%s\' has bad or missing uv data", name.c_str() );
	  	numTVertexes = 1;
		tvList = (idVec2 *)Mem_ClearedAlloc( numTVertexes * sizeof( tvList[0] ) );
	}

	// It seems like the tools our artists are using often generate
	// verts and texcoords slightly separated that should be merged
	// note that we really should combine the surfaces with common materials
	// before doing this operation, because we can miss a slop combination
	// if they are in different surfaces

	vRemap = (int *)R_StaticAlloc( layer->point.count * sizeof( vRemap[0] ) );

	if ( fastLoad ) {
		// renderbump doesn't care about vertex count
		for ( j = 0; j < layer->point.count; j++ ) {
			vRemap[j] = j;
		}
	} else {
		float vertexEpsilon = r_slopVertex.GetFloat();
		float expand = 2 * 32 * vertexEpsilon;
		idVec3 mins, maxs;

		SIMDProcessor->MinMax( mins, maxs, vList, layer->point.count );
		mins -= idVec3( expand, expand, expand );
		maxs += idVec3( expand, expand, expand );
		vertexSubset.Init( mins, maxs, 32, 1024 );
		for ( j = 0; j < layer->point.count; j++ ) {
			vRemap[j] = vertexSubset.FindVector( vList, j, vertexEpsilon );
		}
	}

	tvRemap = (int *)R_StaticAlloc( numTVertexes * sizeof( tvRemap[0] ) );

	if ( fastLoad ) {
		// renderbump doesn't care about vertex count
		for ( j = 0; j < numTVertexes; j++ ) {
			tvRemap[j] = j;
		}
	} else {
		float texCoordEpsilon = r_slopTexCoord.GetFloat();
		float expand = 2 * 32 * texCoordEpsilon;
		idVec2 mins, maxs;

		SIMDProcessor->MinMax( mins, maxs, tvList, numTVertexes );
		mins -= idVec2( expand, expand );
		maxs += idVec2( expand, expand );
		texCoordSubset.Init( mins, maxs, 32, 1024 );
		for ( j = 0; j < numTVertexes; j++ ) {
			tvRemap[j] = texCoordSubset.FindVector( tvList, j, texCoordEpsilon );
		}
	}

	// bucket the polygons by surface up front so each surface pass only visits
	// its own polygons instead of rescanning the whole layer
	idList<const lwSurface *> surfOrder;
	for ( lwoSurf = lwo->surf; lwoSurf; lwoSurf = lwoSurf->next ) {
		surfOrder.Append( lwoSurf );
	}
	idList<int> surfPolyCount;
	surfPolyCount.SetNum( surfOrder.Num() );
	memset( surfPolyCount.Ptr(), 0, surfOrder.Num() * sizeof( surfPolyCount[0] ) );
	idList<int> polySurfOrdinal;
	polySurfOrdinal.SetNum( layer->polygon.count );
	int lastOrdinal = -1;
	for ( j = 0; j < layer->polygon.count; j++ ) {
		const lwSurface *polySurf = layer->polygon.pol[j].surf;
		int ordinal = -1;
		if ( lastOrdinal >= 0 && surfOrder[lastOrdinal] == polySurf ) {
			// polygons are typically grouped by surface
			ordinal = lastOrdinal;
		} else {
			for ( k = 0; k < surfOrder.Num(); k++ ) {
				if ( surfOrder[k] == polySurf ) {
					ordinal = k;
					break;
				}
			}
		}
		// unmatched polygons keep ordinal -1 and are dropped, exactly as the
		// old all-surfaces scan skipped them
		polySurfOrdinal[j] = ordinal;
		if ( ordinal >= 0 ) {
			surfPolyCount[ordinal]++;
			lastOrdinal = ordinal;
		}
	}
	idList<int> surfPolyStart;
	surfPolyStart.SetNum( surfOrder.Num() );
	int polyStart = 0;
	for ( k = 0; k < surfOrder.Num(); k++ ) {
		surfPolyStart[k] = polyStart;
		polyStart += surfPolyCount[k];
	}
	idList<int> surfPolyIndexes;
	surfPolyIndexes.SetNum( polyStart );
	idList<int> surfPolyFill( surfPolyStart );
	for ( j = 0; j < layer->polygon.count; j++ ) {
		// ascending j order preserves the original within-surface polygon order
		if ( polySurfOrdinal[j] >= 0 ) {
			surfPolyIndexes[ surfPolyFill[ polySurfOrdinal[j] ]++ ] = j;
		}
	}

	// build the surfaces
	for ( lwoSurf = lwo->surf, i = 0; lwoSurf; lwoSurf = lwoSurf->next, i++ ) {
		im1 = declManager->FindMaterial( lwoSurf->name );

		bool normalsParsed = true;

		// completely ignore any explict normals on surfaces with a renderbump command
		// which will guarantee the best contours and least vertexes.
		const char *rb = im1->GetRenderBump();
		if ( rb && rb[0] ) {
			normalsParsed = false;
		}

		// we need to find out how many unique vertex / texcoord combinations there are

		// the maximum possible number of combined vertexes is the number of indexes
		const int surfTriIndexCount = surfPolyCount[i] * 3;
		mvTable = (matchVert_t *)R_ClearedStaticAlloc( surfTriIndexCount * sizeof( mvTable[0] ) );

		// we will have a hash chain based on the xyz values
		mvHash = (matchVert_t **)R_ClearedStaticAlloc( layer->point.count * sizeof( mvHash[0] ) );

		// allocate triangle surface
		tri = R_AllocStaticTriSurf();
		tri->numVerts = 0;
		tri->numIndexes = 0;
		R_AllocStaticTriSurfIndexes( tri, surfTriIndexCount > 0 ? surfTriIndexCount : 1 );
		tri->generateNormals = !normalsParsed;

		// find all the unique combinations
		float	normalEpsilon;
		if ( fastLoad ) {
			normalEpsilon = 1.0f;	// don't merge unless completely exact
		} else {
			normalEpsilon = 1.0f - r_slopNormal.GetFloat();
		}
		const int surfPolyEnd = surfPolyStart[i] + surfPolyCount[i];
		for ( int p = surfPolyStart[i]; p < surfPolyEnd; p++ ) {
			lwPolygon *poly = &layer->polygon.pol[ surfPolyIndexes[p] ];

			if ( poly->surf != lwoSurf ) {
				continue;	// defensive: buckets are per surface
			}

			if ( poly->nverts != 3 ) {
				common->Warning( "ConvertLWOToModelSurfaces: model %s has too many verts for a poly! Make sure you triplet it down", name.c_str() );
				continue;
			}

			for ( k = 0; k < 3; k++ ) {

				v = vRemap[poly->v[k].index];

				normal.x = poly->v[k].norm[0];
				normal.y = poly->v[k].norm[2];
				normal.z = poly->v[k].norm[1];

				// LWO models aren't all that pretty when it comes down to the floating point values they store
				normal.FixDegenerateNormal();

				tv = 0;

				color[0] = lwoSurf->color.rgb[0] * 255;
				color[1] = lwoSurf->color.rgb[1] * 255;
				color[2] = lwoSurf->color.rgb[2] * 255;
				color[3] = 255;

				// first set attributes from the vertex
				lwPoint	*pt = &layer->point.pt[poly->v[k].index];
				int nvm;
				for ( nvm = 0; nvm < pt->nvmaps; nvm++ ) {
					lwVMapPt *vm = &pt->vm[nvm];

					if ( vm->vmap->type == LWID_('T','X','U','V') ) {
						tv = tvRemap[vm->index + vm->vmap->offset];
					}
					if ( vm->vmap->type == LWID_('R','G','B','A') ) {
						for ( int chan = 0; chan < 4; chan++ ) {
							color[chan] = 255 * vm->vmap->val[vm->index][chan];
						}
					}
				}

				// then override with polygon attributes
				for ( nvm = 0; nvm < poly->v[k].nvmaps; nvm++ ) {
					lwVMapPt *vm = &poly->v[k].vm[nvm];

					if ( vm->vmap->type == LWID_('T','X','U','V') ) {
						tv = tvRemap[vm->index + vm->vmap->offset];
					}
					if ( vm->vmap->type == LWID_('R','G','B','A') ) {
						for ( int chan = 0; chan < 4; chan++ ) {
							color[chan] = 255 * vm->vmap->val[vm->index][chan];
						}
					}
				}

				// find a matching vert
				for ( lastmv = NULL, mv = mvHash[v]; mv != NULL; lastmv = mv, mv = mv->next ) {
					if ( mv->tv != tv ) {
						continue;
					}
					if ( *(unsigned *)mv->color != *(unsigned *)color ) {
						continue;
					}
					if ( !normalsParsed ) {
						// if we are going to create the normals, just
						// matching texcoords is enough
						break;
					}
					if ( mv->normal * normal > normalEpsilon ) {
						break;		// we already have this one
					}
				}
				if ( !mv ) {
					// allocate a new match vert and link to hash chain
					mv = &mvTable[ tri->numVerts ];
					mv->v = v;
					mv->tv = tv;
					mv->normal = normal;
					*(unsigned *)mv->color = *(unsigned *)color;
					mv->next = NULL;
					if ( lastmv ) {
						lastmv->next = mv;
					} else {
						mvHash[v] = mv;
					}
					tri->numVerts++;
				}

				tri->indexes[tri->numIndexes] = mv - mvTable;
				tri->numIndexes++;
			}
		}

		// allocate space for the indexes and copy them
		if ( tri->numIndexes > layer->polygon.count * 3 ) {
			common->FatalError( "ConvertLWOToModelSurfaces: index miscount in LWO file %s", name.c_str() );
		}
		if ( tri->numVerts > layer->polygon.count * 3 ) {
			common->FatalError( "ConvertLWOToModelSurfaces: vertex miscount in LWO file %s", name.c_str() );
		}

		// now allocate and generate the combined vertexes
		R_AllocStaticTriSurfVerts( tri, tri->numVerts );
		for ( j = 0; j < tri->numVerts; j++ ) {
			mv = &mvTable[j];
			tri->verts[ j ].Clear();
			tri->verts[ j ].xyz = vList[ mv->v ];
			tri->verts[ j ].st = tvList[ mv->tv ];
			tri->verts[ j ].normal = mv->normal;
			*(unsigned *)tri->verts[j].color = *(unsigned *)mv->color;
		}

		R_StaticFree( mvTable );
		R_StaticFree( mvHash );

		// see if we need to merge with a previous surface of the same material
		modelSurf = &this->surfaces[mergeTo[ i ]];
		srfTriangles_t	*mergeTri = modelSurf->geometry;
		if ( !mergeTri ) {
			modelSurf->geometry = tri;
		} else {
			modelSurf->geometry = R_MergeTriangles( mergeTri, tri );
			R_FreeStaticTriSurf( tri );
			R_FreeStaticTriSurf( mergeTri );
		}
	}

	R_StaticFree( tvRemap );
	R_StaticFree( vRemap );
	R_StaticFree( tvList );
	R_StaticFree( vList );

	return true;
}

/*
=================
idRenderModelStatic::ConvertLWOToASE
=================
*/
struct aseModel_s *idRenderModelStatic::ConvertLWOToASE( const struct st_lwObject *obj, const char *fileName ) {
	int j, k;
	aseModel_t *ase;

	if ( !obj ) {
		return NULL;
	}

	// NOTE: using new operator because aseModel_t contains idList class objects
	ase = new aseModel_t;
	ase->timeStamp = obj->timeStamp;
	ase->objects.Resize( obj->nlayers, obj->nlayers );

	int materialRef = 0;

	for ( lwSurface *surf = obj->surf; surf; surf = surf->next ) {

		aseMaterial_t *mat = (aseMaterial_t *)Mem_ClearedAlloc( sizeof( *mat ) );
		idStr::Copynz( mat->name, surf->name, sizeof( mat->name ) );
		mat->uTiling = mat->vTiling = 1;
		mat->angle = mat->uOffset = mat->vOffset = 0;
		ase->materials.Append( mat );

		lwLayer *layer = obj->layer;

		aseObject_t *object = (aseObject_t *)Mem_ClearedAlloc( sizeof( *object ) );
		object->materialRef = materialRef++;

		aseMesh_t *mesh = &object->mesh;
		ase->objects.Append( object );

		mesh->numFaces = layer->polygon.count;
		mesh->numTVFaces = mesh->numFaces;
		mesh->faces = (aseFace_t *)Mem_Alloc( mesh->numFaces  * sizeof( mesh->faces[0] ) );

		mesh->numVertexes = layer->point.count;
		mesh->vertexes = (idVec3 *)Mem_Alloc( mesh->numVertexes * sizeof( mesh->vertexes[0] ) );

		// vertex positions
		if ( layer->point.count <= 0 ) {
			common->Warning( "ConvertLWOToASE: model \'%s\' has bad or missing vertex data", name.c_str() );
		}

		for ( j = 0; j < layer->point.count; j++ ) {
			mesh->vertexes[j].x = layer->point.pt[j].pos[0];
			mesh->vertexes[j].y = layer->point.pt[j].pos[2];
			mesh->vertexes[j].z = layer->point.pt[j].pos[1];
		}

		// vertex texture coords
		mesh->numTVertexes = 0;

		if ( layer->nvmaps ) {
		  	for( lwVMap *vm = layer->vmap; vm; vm = vm->next ) {
				if ( vm->type == LWID_('T','X','U','V') ) {
					mesh->numTVertexes += vm->nverts;
				}
			}
		}

		if ( mesh->numTVertexes ) {
		  	mesh->tvertexes = (idVec2 *)Mem_Alloc( mesh->numTVertexes * sizeof( mesh->tvertexes[0] ) );
		  	int offset = 0;
		  	for( lwVMap *vm = layer->vmap; vm; vm = vm->next ) {
				if ( vm->type == LWID_('T','X','U','V') ) {
	  				vm->offset = offset;
		  			for ( k = 0; k < vm->nverts; k++ ) {
		  		   		mesh->tvertexes[k + offset].x = vm->val[k][0];
						mesh->tvertexes[k + offset].y = 1.0f - vm->val[k][1];	// invert the t
		  		   	}
			  		offset += vm->nverts;
				}
		  	}
	  	} else {
			common->Warning( "ConvertLWOToASE: model \'%s\' has bad or missing uv data", fileName );
	  		mesh->numTVertexes = 1;
	  		mesh->tvertexes = (idVec2 *)Mem_ClearedAlloc( mesh->numTVertexes * sizeof( mesh->tvertexes[0] ) );
	  	}

		mesh->normalsParsed = true;
		mesh->colorsParsed = true;	// because we are falling back to the surface color

		// triangles
		int faceIndex = 0;
		for ( j = 0; j < layer->polygon.count; j++ ) {
			lwPolygon *poly = &layer->polygon.pol[j];

			if ( poly->surf != surf ) {
				continue;
			}

			if ( poly->nverts != 3 ) {
				common->Warning( "ConvertLWOToASE: model %s has too many verts for a poly! Make sure you triplet it down", fileName );
				continue;
			}
	
			mesh->faces[faceIndex].faceNormal.x = poly->norm[0];
			mesh->faces[faceIndex].faceNormal.y = poly->norm[2];
			mesh->faces[faceIndex].faceNormal.z = poly->norm[1];

			for ( k = 0; k < 3; k++ ) {

				mesh->faces[faceIndex].vertexNum[k] = poly->v[k].index;

				mesh->faces[faceIndex].vertexNormals[k].x = poly->v[k].norm[0];
				mesh->faces[faceIndex].vertexNormals[k].y = poly->v[k].norm[2];
				mesh->faces[faceIndex].vertexNormals[k].z = poly->v[k].norm[1];

				// complete fallbacks
				mesh->faces[faceIndex].tVertexNum[k] = 0;

				mesh->faces[faceIndex].vertexColors[k][0] = surf->color.rgb[0] * 255;
				mesh->faces[faceIndex].vertexColors[k][1] = surf->color.rgb[1] * 255;
				mesh->faces[faceIndex].vertexColors[k][2] = surf->color.rgb[2] * 255;
				mesh->faces[faceIndex].vertexColors[k][3] = 255;

				// first set attributes from the vertex
				lwPoint	*pt = &layer->point.pt[poly->v[k].index];
				int nvm;
				for ( nvm = 0; nvm < pt->nvmaps; nvm++ ) {
					lwVMapPt *vm = &pt->vm[nvm];

					if ( vm->vmap->type == LWID_('T','X','U','V') ) {
						mesh->faces[faceIndex].tVertexNum[k] = vm->index + vm->vmap->offset;
					}
					if ( vm->vmap->type == LWID_('R','G','B','A') ) {
						for ( int chan = 0; chan < 4; chan++ ) {
							mesh->faces[faceIndex].vertexColors[k][chan] = 255 * vm->vmap->val[vm->index][chan];
						}
					}
				}

				// then override with polygon attributes
				for ( nvm = 0; nvm < poly->v[k].nvmaps; nvm++ ) {
					lwVMapPt *vm = &poly->v[k].vm[nvm];

					if ( vm->vmap->type == LWID_('T','X','U','V') ) {
						mesh->faces[faceIndex].tVertexNum[k] = vm->index + vm->vmap->offset;
					}
					if ( vm->vmap->type == LWID_('R','G','B','A') ) {
						for ( int chan = 0; chan < 4; chan++ ) {
							mesh->faces[faceIndex].vertexColors[k][chan] = 255 * vm->vmap->val[vm->index][chan];
						}
					}
				}
			}

			faceIndex++;
		}

		mesh->numFaces = faceIndex;
		mesh->numTVFaces = faceIndex;

		aseFace_t *newFaces = ( aseFace_t* )Mem_Alloc( mesh->numFaces * sizeof ( mesh->faces[0] ) );
		memcpy( newFaces, mesh->faces, sizeof( mesh->faces[0] ) * mesh->numFaces );
		Mem_Free( mesh->faces );
		mesh->faces = newFaces;
	}

	return ase;
}

/*
=================
idRenderModelStatic::ConvertMAToModelSurfaces
=================
*/
bool idRenderModelStatic::ConvertMAToModelSurfaces (const struct maModel_s *ma ) {

	maObject_t *	object;
	maMesh_t *		mesh;
	maMaterial_t *	material;
	
	const idMaterial *im1, *im2;
	srfTriangles_t *tri;
	int				objectNum;
	int				i, j, k;
	int				v, tv;
	int *			vRemap;
	int *			tvRemap;
	matchVert_t *	mvTable;	// all of the match verts
	matchVert_t **	mvHash;		// points inside mvTable for each xyz index
	matchVert_t *	lastmv;
	matchVert_t *	mv;
	idVec3			normal;
	float			uOffset, vOffset, textureSin, textureCos;
	float			uTiling, vTiling;
	int *			mergeTo;
	byte *			color;
	static byte	identityColor[4] = { 255, 255, 255, 255 };
	modelSurface_t	surf = {}, *modelSurf;

	if ( !ma ) {
		return false;
	}
	if ( ma->objects.Num() < 1 ) {
		return false;
	}

	timeStamp = ma->timeStamp;

	// the modeling programs can save out multiple surfaces with a common
	// material, but we would like to mege them together where possible
	// meaning that this->NumSurfaces() <= ma->objects.currentElements
	mergeTo = (int *)_alloca( ma->objects.Num() * sizeof( *mergeTo ) ); 

	surf.geometry = NULL;
	if ( ma->materials.Num() == 0 ) {
		// if we don't have any materials, dump everything into a single surface
		surf.shader = tr.defaultMaterial;
		surf.id = 0;
		this->AddSurface( surf );
		for ( i = 0 ; i < ma->objects.Num() ; i++ ) { 
			mergeTo[i] = 0;
		}
	} else if ( !r_mergeModelSurfaces.GetBool() ) {
		// don't merge any
		for ( i = 0 ; i < ma->objects.Num() ; i++ ) { 
			mergeTo[i] = i;
			object = ma->objects[i];
			if(object->materialRef >= 0) {
				material = ma->materials[object->materialRef];
				surf.shader = declManager->FindMaterial( material->name );
			} else {
				surf.shader = tr.defaultMaterial;
			}
			surf.id = this->NumSurfaces();
			this->AddSurface( surf );
		}
	} else {
		// search for material matches
		for ( i = 0 ; i < ma->objects.Num() ; i++ ) { 
			object = ma->objects[i];
			if(object->materialRef >= 0) {
				material = ma->materials[object->materialRef];
				im1 = declManager->FindMaterial( material->name );
			} else {
				im1 = tr.defaultMaterial;
			}
			if ( im1->IsDiscrete() ) {
				// flares, autosprites, etc
				j = this->NumSurfaces();
			} else {
				for ( j = 0 ; j < this->NumSurfaces() ; j++ ) {
					modelSurf = &this->surfaces[j];
					im2 = modelSurf->shader;
					if ( im1 == im2 ) {
						// merge this
						mergeTo[i] = j;
						break;
					}
				}
			}
			if ( j == this->NumSurfaces() ) {
				// didn't merge
				mergeTo[i] = j;
				surf.shader = im1;
				surf.id = this->NumSurfaces();
				this->AddSurface( surf );
			}
		}
	}

	idVectorSubset<idVec3, 3> vertexSubset;
	idVectorSubset<idVec2, 2> texCoordSubset;

	// build the surfaces
	for ( objectNum = 0 ; objectNum < ma->objects.Num() ; objectNum++ ) {
		object = ma->objects[objectNum];
		mesh = &object->mesh;
		if(object->materialRef >= 0) {
			material = ma->materials[object->materialRef];
			im1 = declManager->FindMaterial( material->name );
		} else {
			im1 = tr.defaultMaterial;
		}

		bool normalsParsed = mesh->normalsParsed;
		
		// completely ignore any explict normals on surfaces with a renderbump command
		// which will guarantee the best contours and least vertexes.
		const char *rb = im1->GetRenderBump();
		if ( rb && rb[0] ) {
			normalsParsed = false;
		}

		// It seems like the tools our artists are using often generate
		// verts and texcoords slightly separated that should be merged
		// note that we really should combine the surfaces with common materials
		// before doing this operation, because we can miss a slop combination
		// if they are in different surfaces

		vRemap = (int *)R_StaticAlloc( mesh->numVertexes * sizeof( vRemap[0] ) );

		if ( fastLoad ) {
			// renderbump doesn't care about vertex count
			for ( j = 0; j < mesh->numVertexes; j++ ) {
				vRemap[j] = j;
			}
		} else {
			float vertexEpsilon = r_slopVertex.GetFloat();
			float expand = 2 * 32 * vertexEpsilon;
			idVec3 mins, maxs;

			SIMDProcessor->MinMax( mins, maxs, mesh->vertexes, mesh->numVertexes );
			mins -= idVec3( expand, expand, expand );
			maxs += idVec3( expand, expand, expand );
			vertexSubset.Init( mins, maxs, 32, 1024 );
			for ( j = 0; j < mesh->numVertexes; j++ ) {
				vRemap[j] = vertexSubset.FindVector( mesh->vertexes, j, vertexEpsilon );
			}
		}

		tvRemap = (int *)R_StaticAlloc( mesh->numTVertexes * sizeof( tvRemap[0] ) );

		if ( fastLoad ) {
			// renderbump doesn't care about vertex count
			for ( j = 0; j < mesh->numTVertexes; j++ ) {
				tvRemap[j] = j;
			}
		} else {
			float texCoordEpsilon = r_slopTexCoord.GetFloat();
			float expand = 2 * 32 * texCoordEpsilon;
			idVec2 mins, maxs;

			SIMDProcessor->MinMax( mins, maxs, mesh->tvertexes, mesh->numTVertexes );
			mins -= idVec2( expand, expand );
			maxs += idVec2( expand, expand );
			texCoordSubset.Init( mins, maxs, 32, 1024 );
			for ( j = 0; j < mesh->numTVertexes; j++ ) {
				tvRemap[j] = texCoordSubset.FindVector( mesh->tvertexes, j, texCoordEpsilon );
			}
		}

		// we need to find out how many unique vertex / texcoord / color combinations
		// there are, because MA tracks them separately but we need them unified

		// the maximum possible number of combined vertexes is the number of indexes
		mvTable = (matchVert_t *)R_ClearedStaticAlloc( mesh->numFaces * 3 * sizeof( mvTable[0] ) );

		// we will have a hash chain based on the xyz values
		mvHash = (matchVert_t **)R_ClearedStaticAlloc( mesh->numVertexes * sizeof( mvHash[0] ) );

		// allocate triangle surface
		tri = R_AllocStaticTriSurf();
		tri->numVerts = 0;
		tri->numIndexes = 0;
		R_AllocStaticTriSurfIndexes( tri, mesh->numFaces * 3 );
		tri->generateNormals = !normalsParsed;

		// init default normal, color and tex coord index
		normal.Zero();
		color = identityColor;
		tv = 0;

		// find all the unique combinations
		float normalEpsilon = 1.0f - r_slopNormal.GetFloat();
		for ( j = 0; j < mesh->numFaces; j++ ) {
			for ( k = 0; k < 3; k++ ) {
				v = mesh->faces[j].vertexNum[k];

				if ( v < 0 || v >= mesh->numVertexes ) {
					common->Error( "ConvertMAToModelSurfaces: bad vertex index in MA file %s", name.c_str() );
				}

				// collapse the position if it was slightly offset 
				v = vRemap[v];

				// we may or may not have texcoords to compare
				if ( mesh->numTVertexes != 0 ) {
					tv = mesh->faces[j].tVertexNum[k];
					if ( tv < 0 || tv >= mesh->numTVertexes ) {
						common->Error( "ConvertMAToModelSurfaces: bad tex coord index in MA file %s", name.c_str() );
					}
					// collapse the tex coord if it was slightly offset
					tv = tvRemap[tv];
				}

				// we may or may not have normals to compare
				if ( normalsParsed ) {
					normal = mesh->faces[j].vertexNormals[k];
				}

				//BSM: Todo: Fix the vertex colors
				// we may or may not have colors to compare
				if ( mesh->faces[j].vertexColors[k] != -1 && mesh->faces[j].vertexColors[k] != -999 ) {

					color = &mesh->colors[mesh->faces[j].vertexColors[k]*4];
				}

				// find a matching vert
				for ( lastmv = NULL, mv = mvHash[v]; mv != NULL; lastmv = mv, mv = mv->next ) {
					if ( mv->tv != tv ) {
						continue;
					}
					if ( *(unsigned *)mv->color != *(unsigned *)color ) {
						continue;
					}
					if ( !normalsParsed ) {
						// if we are going to create the normals, just
						// matching texcoords is enough
						break;
					}
					if ( mv->normal * normal > normalEpsilon ) {
						break;		// we already have this one
					}
				}
				if ( !mv ) {
					// allocate a new match vert and link to hash chain
					mv = &mvTable[ tri->numVerts ];
					mv->v = v;
					mv->tv = tv;
					mv->normal = normal;
					*(unsigned *)mv->color = *(unsigned *)color;
					mv->next = NULL;
					if ( lastmv ) {
						lastmv->next = mv;
					} else {
						mvHash[v] = mv;
					}
					tri->numVerts++;
				}

				tri->indexes[tri->numIndexes] = mv - mvTable;
				tri->numIndexes++;
			}
		}

		// allocate space for the indexes and copy them
		if ( tri->numIndexes > mesh->numFaces * 3 ) {
			common->FatalError( "ConvertMAToModelSurfaces: index miscount in MA file %s", name.c_str() );
		}
		if ( tri->numVerts > mesh->numFaces * 3 ) {
			common->FatalError( "ConvertMAToModelSurfaces: vertex miscount in MA file %s", name.c_str() );
		}

		// an MA allows the texture coordinates to be scaled, translated, and rotated
		//BSM: Todo: Does Maya support this and if so how
		//if ( ase->materials.Num() == 0 ) {
			uOffset = vOffset = 0.0f;
			uTiling = vTiling = 1.0f;
			textureSin = 0.0f;
			textureCos = 1.0f;
		//} else {
		//	material = ase->materials[object->materialRef];
		//	uOffset = -material->uOffset;
		//	vOffset = material->vOffset;
		//	uTiling = material->uTiling;
		//	vTiling = material->vTiling;
		//	textureSin = idMath::Sin( material->angle );
		//	textureCos = idMath::Cos( material->angle );
		//}

		// now allocate and generate the combined vertexes
		R_AllocStaticTriSurfVerts( tri, tri->numVerts );
		for ( j = 0; j < tri->numVerts; j++ ) {
			mv = &mvTable[j];
			tri->verts[ j ].Clear();
			tri->verts[ j ].xyz = mesh->vertexes[ mv->v ];
			tri->verts[ j ].normal = mv->normal;
			*(unsigned *)tri->verts[j].color = *(unsigned *)mv->color;
			if ( mesh->numTVertexes != 0 ) {
				const idVec2 &tv = mesh->tvertexes[ mv->tv ];
				float u = tv.x * uTiling + uOffset;
				float v = tv.y * vTiling + vOffset;
				tri->verts[ j ].st[0] = u * textureCos + v * textureSin;
				tri->verts[ j ].st[1] = u * -textureSin + v * textureCos;
			}
		}

		R_StaticFree( mvTable );
		R_StaticFree( mvHash );
		R_StaticFree( tvRemap );
		R_StaticFree( vRemap );

		// see if we need to merge with a previous surface of the same material
		modelSurf = &this->surfaces[mergeTo[ objectNum ]];
		srfTriangles_t	*mergeTri = modelSurf->geometry;
		if ( !mergeTri ) {
			modelSurf->geometry = tri;
		} else {
			modelSurf->geometry = R_MergeTriangles( mergeTri, tri );
			R_FreeStaticTriSurf( tri );
			R_FreeStaticTriSurf( mergeTri );
		}
	}

	return true;
}

/*
=================
idRenderModelStatic::LoadASE
=================
*/
bool idRenderModelStatic::LoadASE( const char *fileName ) {
	aseModel_t *ase;

	ase = ASE_Load( fileName );
	if ( ase == NULL ) {
		return false;
	}

	ConvertASEToModelSurfaces( ase );

	ASE_Free( ase );

	return true;
}

/*
=================
idRenderModelStatic::LoadLWO
=================
*/
bool idRenderModelStatic::LoadLWO( const char *fileName ) {
	unsigned int failID;
	int failPos;
	lwObject *lwo;

	lwo = lwGetObject( fileName, &failID, &failPos );
	if ( lwo == NULL ) {
		return false;
	}

	ConvertLWOToModelSurfaces( lwo );

	lwFreeObject( lwo );

	return true;
}

/*
=================
idRenderModelStatic::LoadMA
=================
*/
bool idRenderModelStatic::LoadMA( const char *fileName ) {
	maModel_t *ma;

	ma = MA_Load( fileName );
	if ( ma == NULL ) {
		return false;
	}

	ConvertMAToModelSurfaces( ma );

	MA_Free( ma );

	return true;
}

/*
=================
idRenderModelStatic::LoadFLT

USGS height map data for megaTexture experiments
=================
*/
bool idRenderModelStatic::LoadFLT( const char *fileName ) {
	float	*data;
	int		len;

	len = fileSystem->ReadFile( fileName, (void **)&data );
	if ( len <= 0 ) {
		return false;
	}
	int	size = sqrt( len / 4.0f );

	// bound the altitudes
	float min = 9999999;
	float max = -9999999;
	for ( int i = 0 ; i < len/4 ; i++ ) {
	data[i] = BigFloat( data[i] );
	if ( data[i] == -9999 ) {
		data[i] = 0;		// unscanned areas
	}

		if ( data[i] < min ) {
			min = data[i];
		}
		if ( data[i] > max ) {
			max = data[i];
		}
	}
#if 1
	// write out a gray scale height map
	byte	*image = (byte *)R_StaticAlloc( len );
	byte	*image_p = image;
	for ( int i = 0 ; i < len/4 ; i++ ) {
		float v = ( data[i] - min ) / ( max - min );
		image_p[0] =
		image_p[1] =
		image_p[2] = v * 255;
		image_p[3] = 255;
		image_p += 4;
	}
	idStr	tgaName = fileName;
	tgaName.StripFileExtension();
	tgaName += ".tga";
	R_WriteTGA( tgaName.c_str(), image, size, size, false );
	R_StaticFree( image );
//return false;
#endif

	// find the island above sea level
	int	minX, maxX, minY, maxY;
	{
		int	i;	
		for ( minX = 0 ; minX < size ; minX++ ) {
			for ( i = 0 ; i < size ; i++ ) {
				if ( data[i*size + minX] > 1.0 ) {
					break;
				}
			}
			if ( i != size ) {
				break;
			}
		}

		for ( maxX = size-1 ; maxX > 0 ; maxX-- ) {
			for ( i = 0 ; i < size ; i++ ) {
				if ( data[i*size + maxX] > 1.0 ) {
					break;
				}
			}
			if ( i != size ) {
				break;
			}
		}

		for ( minY = 0 ; minY < size ; minY++ ) {
			for ( i = 0 ; i < size ; i++ ) {
				if ( data[minY*size + i] > 1.0 ) {
					break;
				}
			}
			if ( i != size ) {
				break;
			}
		}

		for ( maxY = size-1 ; maxY < size ; maxY-- ) {
			for ( i = 0 ; i < size ; i++ ) {
				if ( data[maxY*size + i] > 1.0 ) {
					break;
				}
			}
			if ( i != size ) {
				break;
			}
		}
	}

	int	width = maxX - minX + 1;
	int height = maxY - minY + 1;

//width /= 2;
	// allocate triangle surface
	srfTriangles_t *tri = R_AllocStaticTriSurf();
	tri->numVerts = width * height;
	tri->numIndexes = (width-1) * (height-1) * 6;

	fastLoad = true;		// don't do all the sil processing

	R_AllocStaticTriSurfIndexes( tri, tri->numIndexes );
	R_AllocStaticTriSurfVerts( tri, tri->numVerts );

	for ( int i = 0 ; i < height ; i++ ) {
		for ( int j = 0; j < width ; j++ ) {
			int		v = i * width + j;
			tri->verts[ v ].Clear();
			tri->verts[ v ].xyz[0] = j * 10;	// each sample is 10 meters
			tri->verts[ v ].xyz[1] = -i * 10;
			tri->verts[ v ].xyz[2] = data[(minY+i)*size+minX+j];	// height is in meters
			tri->verts[ v ].st[0] = (float) j / (width-1);
			tri->verts[ v ].st[1] = 1.0 - ( (float) i / (height-1) );
		}
	}

	for ( int i = 0 ; i < height-1 ; i++ ) {
		for ( int j = 0; j < width-1 ; j++ ) {
			int	v = ( i * (width-1) + j ) * 6;
#if 0
			tri->indexes[ v + 0 ] = i * width + j;
			tri->indexes[ v + 1 ] = (i+1) * width + j;
			tri->indexes[ v + 2 ] = (i+1) * width + j + 1;
			tri->indexes[ v + 3 ] = i * width + j;
			tri->indexes[ v + 4 ] = (i+1) * width + j + 1;
			tri->indexes[ v + 5 ] = i * width + j + 1;
#else
			tri->indexes[ v + 0 ] = i * width + j;
			tri->indexes[ v + 1 ] = i * width + j + 1;
			tri->indexes[ v + 2 ] = (i+1) * width + j + 1;
			tri->indexes[ v + 3 ] = i * width + j;
			tri->indexes[ v + 4 ] = (i+1) * width + j + 1;
			tri->indexes[ v + 5 ] = (i+1) * width + j;
#endif
		}
	}

	fileSystem->FreeFile( data );

	modelSurface_t	surface = {};

	surface.geometry = tri;
	surface.id = 0;
	surface.shader = tr.defaultMaterial; // declManager->FindMaterial( "shaderDemos/megaTexture" );

	this->AddSurface( surface );

	return true;
}


//=============================================================================

/*
================
idRenderModelStatic::PurgeModel
================
*/
void idRenderModelStatic::PurgeModel() {
	int		i;
	modelSurface_t	*surf;

	for ( i = 0 ; i < surfaces.Num() ; i++ ) {
		surf = &surfaces[i];

		if ( surf->geometry ) {
			R_FreeStaticTriSurf( surf->geometry );
		}
	}
	surfaces.Clear();

	purged = true;
	procSky = false;
	generatedCacheContent = false;
}

/*
==============
idRenderModelStatic::FreeVertexCache

We are about to restart the vertex cache, so dump everything
==============
*/
void idRenderModelStatic::FreeVertexCache( void ) {
	for ( int j = 0 ; j < surfaces.Num() ; j++ ) {
		srfTriangles_t *tri = surfaces[j].geometry;
		if ( !tri ) {
			continue;
		}
		// TAG_TEMP blocks live in the shared per-frame space and are recycled
		// by the frame loop; abandoning the pointer is the correct disposal
		// (idVertexCache::Free fatals on them)
		if ( tri->ambientCache ) {
			if ( tri->ambientCache->tag != TAG_TEMP ) {
				vertexCache.Free( tri->ambientCache );
			}
			tri->ambientCache = NULL;
		}
		// static shadows may be present
		if ( tri->shadowCache ) {
			if ( tri->shadowCache->tag != TAG_TEMP ) {
				vertexCache.Free( tri->shadowCache );
			}
			tri->shadowCache = NULL;
		}
		if ( tri->indexCache ) {
			if ( tri->indexCache->tag != TAG_TEMP ) {
				vertexCache.Free( tri->indexCache );
			}
			tri->indexCache = NULL;
		}
	}
}

/*
================
idRenderModelStatic::ReadFromDemoFile
================
*/
void idRenderModelStatic::ReadFromDemoFile( class idDemoFile *f ) {
	PurgeModel();

	InitEmpty( f->ReadHashString() );

	int i, j, numSurfaces;
	f->ReadInt( numSurfaces );
	if ( numSurfaces < 0 || numSurfaces > 65536 ) {
		common->Error( "idRenderModelStatic::ReadFromDemoFile: bad surface count %d", numSurfaces );
	}
	
	for ( i = 0 ; i < numSurfaces ; i++ ) {
		modelSurface_t	surf = {};
		
		surf.id = i;
		surf.shader = declManager->FindMaterial( f->ReadHashString() );
		
		srfTriangles_t	*tri = R_AllocStaticTriSurf();
		
		int numIndexes;
		f->ReadInt( numIndexes );
		if ( numIndexes < 0 || numIndexes > 1 << 24 || ( numIndexes % 3 ) != 0 ) {
			common->Error( "idRenderModelStatic::ReadFromDemoFile: bad index count %d", numIndexes );
		}
		idList<int> demoIndexes;
		demoIndexes.SetNum( numIndexes );
		for ( j = 0; j < numIndexes; ++j ) {
			f->ReadInt( demoIndexes[j] );
		}
		
		f->ReadInt( tri->numVerts );
		if ( tri->numVerts < 0 || tri->numVerts > 1 << 20 ) {
			common->Error( "idRenderModelStatic::ReadFromDemoFile: bad vertex count %d", tri->numVerts );
		}
		tri->numIndexes = numIndexes;
		R_AllocStaticTriSurfIndexes( tri, tri->numIndexes );
		for ( j = 0; j < tri->numIndexes; ++j ) {
			if ( demoIndexes[j] < 0 || demoIndexes[j] >= tri->numVerts ) {
				common->Error( "idRenderModelStatic::ReadFromDemoFile: bad vertex index %d", demoIndexes[j] );
			}
			tri->indexes[j] = (glIndex_t)demoIndexes[j];
		}
		R_AllocStaticTriSurfVerts( tri, tri->numVerts );
		for ( j = 0; j < tri->numVerts; ++j ) {
			f->ReadVec3( tri->verts[j].xyz );
			f->ReadVec2( tri->verts[j].st );
			f->ReadVec3( tri->verts[j].normal );
			f->ReadVec3( tri->verts[j].tangents[0] );
			f->ReadVec3( tri->verts[j].tangents[1] );
			f->ReadUnsignedChar( tri->verts[j].color[0] );
			f->ReadUnsignedChar( tri->verts[j].color[1] );
			f->ReadUnsignedChar( tri->verts[j].color[2] );
			f->ReadUnsignedChar( tri->verts[j].color[3] );
		}
		
		surf.geometry = tri;
		
		this->AddSurface( surf );
	}
	this->FinishSurfaces();
}

/*
================
idRenderModelStatic::WriteToDemoFile
================
*/
void idRenderModelStatic::WriteToDemoFile( class idDemoFile *f ) {
	int	data[1];

	// note that it has been updated
	lastArchivedFrame = tr.frameCount;

	data[0] = DC_DEFINE_MODEL;
	f->WriteInt( data[0] );
	f->WriteHashString( this->Name() );

	int i, j, iData = surfaces.Num();
	f->WriteInt( iData );

	for ( i = 0 ; i < surfaces.Num() ; i++ ) {
		const modelSurface_t	*surf = &surfaces[i];
		
		f->WriteHashString( surf->shader->GetName() );
		
		srfTriangles_t *tri = surf->geometry;
		f->WriteInt( tri->numIndexes );
		for ( j = 0; j < tri->numIndexes; ++j )
			f->WriteInt( (int&)tri->indexes[j] );
		f->WriteInt( tri->numVerts );
		for ( j = 0; j < tri->numVerts; ++j ) {
			f->WriteVec3( tri->verts[j].xyz );
			f->WriteVec2( tri->verts[j].st );
			f->WriteVec3( tri->verts[j].normal );
			f->WriteVec3( tri->verts[j].tangents[0] );
			f->WriteVec3( tri->verts[j].tangents[1] );
			f->WriteUnsignedChar( tri->verts[j].color[0] );
			f->WriteUnsignedChar( tri->verts[j].color[1] );
			f->WriteUnsignedChar( tri->verts[j].color[2] );
			f->WriteUnsignedChar( tri->verts[j].color[3] );
		}
	}
}

/*
================
idRenderModelStatic::IsLoaded
================
*/
bool idRenderModelStatic::IsLoaded( void ) {
	return !purged;
}

/*
================
idRenderModelStatic::SetLevelLoadReferenced
================
*/
void idRenderModelStatic::SetLevelLoadReferenced( bool referenced ) {
	levelLoadReferenced = referenced;
}

/*
================
idRenderModelStatic::IsLevelLoadReferenced
================
*/
bool idRenderModelStatic::IsLevelLoadReferenced( void ) {
	return levelLoadReferenced;
}

/*
=================
idRenderModelStatic::TouchData
=================
*/
void idRenderModelStatic::TouchData( void ) {
	for ( int i = 0 ; i < surfaces.Num() ; i++ ) {
		const modelSurface_t	*surf = &surfaces[i];

		// re-find the material to make sure it gets added to the
		// level keep list
		declManager->FindMaterial( surf->shader->GetName() );
	}
}

/*
=================
idRenderModelStatic::DeleteSurfaceWithId
=================
*/
bool idRenderModelStatic::DeleteSurfaceWithId( int id ) {
	int i;

	for ( i = 0; i < surfaces.Num(); i++ ) {
		if ( surfaces[i].id == id ) {
			R_FreeStaticTriSurf( surfaces[i].geometry );
			surfaces.RemoveIndex( i );
			return true;
		}
	}
	return false;
}

/*
=================
idRenderModelStatic::DeleteSurfacesWithNegativeId
=================
*/
void idRenderModelStatic::DeleteSurfacesWithNegativeId( void ) {
	int i;

	for ( i = 0; i < surfaces.Num(); i++ ) {
		if ( surfaces[i].id < 0 ) {
			R_FreeStaticTriSurf( surfaces[i].geometry );
			surfaces.RemoveIndex( i );
			i--;
		}
	}
}

/*
=================
idRenderModelStatic::FindSurfaceWithId
=================
*/
bool idRenderModelStatic::FindSurfaceWithId( int id, int &surfaceNum ) {
	int i;

	for ( i = 0; i < surfaces.Num(); i++ ) {
		if ( surfaces[i].id == id ) {
			surfaceNum = i;
			return true;
		}
	}
	return false;
}

/*
====================
idRenderModelStatic::GetSurfaceMask
====================
*/
int idRenderModelStatic::GetSurfaceMask( const char *surface ) const {
	if ( surface == NULL || surface[0] == '\0' ) {
		return 0;
	}

	const idMaterial *material = declManager->FindMaterial( surface, false );
	if ( material == NULL ) {
		return 0;
	}

	int mask = 0;
	for ( int i = 0; i < surfaces.Num() && i < static_cast<int>( sizeof( unsigned int ) * 8 ); ++i ) {
		if ( surfaces[i].shader == material ) {
			mask |= ( 1u << i );
		}
	}

	return mask;
}
