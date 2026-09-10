/*
===========================================================================

Doom 3 GPL Source Code
Copyright (C) 1999-2011 id Software LLC, a ZeniMax Media company.

This file is part of the Doom 3 GPL Source Code ("Doom 3 Source Code").

Doom 3 Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Doom 3 Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

===========================================================================
*/

#include "tr_local.h"
#if !defined( _WIN32 )
#include <unistd.h>
#endif

static const char *LGRID_FILE_ID = "LGRID";
static const int LIGHTGRID_PACK_MAGIC = ( 'L' << 0 ) | ( 'G' << 8 ) | ( 'P' << 16 ) | ( 'K' << 24 );
static const int LIGHTGRID_PACK_VERSION = 1;
static const int LIGHTGRID_SUPPORTED_VERSION_A = 3;
static const int LIGHTGRID_SUPPORTED_VERSION_B = 4;
static const int LIGHTGRID_SUPPORTED_VERSION_C = 5;
static const int LIGHTGRID_SUPPORTED_VERSION_D = 6;
static const int LIGHTGRID_CURRENT_VERSION = 7;
static const int LIGHTGRID_BAKE_HEADER_VERSION = 1;
static const int LIGHTGRID_DEFAULT_SINGLE_PROBE_SIZE = 16;
static const int LIGHTGRID_DEFAULT_BORDER_SIZE = 2;
static const idVec3 LIGHTGRID_DEFAULT_SIZE( 64.0f, 64.0f, 128.0f );
static const float LIGHTGRID_VISIBILITY_MAX_DISTANCE = 4096.0f;
static const float LIGHTGRID_VISIBILITY_TRACE_BIAS = 2.0f;
static const int LIGHTGRID_VISIBILITY_TRACE_SAMPLES = 4;
static const float LIGHTGRID_VISIBILITY_CONE_OFFSET = 0.12f;
static const float LIGHTGRID_RELOCATION_CLEARANCE = 18.0f;
static const float LIGHTGRID_RELOCATION_MAX_DISTANCE = 48.0f;
static const float LIGHTGRID_RELOCATION_SEARCH_STEP = 8.0f;
static const int LIGHTGRID_MAX_ATLAS_SIZE = 2048;
static const int LIGHTGRID_DEFAULT_CAPTURE_SIZE = 128;
static const int LIGHTGRID_DEFAULT_BLENDS = 1;
static const int LIGHTGRID_DEFAULT_SAMPLES = 128;
static const int LIGHTGRID_DEFAULT_MAX_AREA_POINTS =
	( LIGHTGRID_MAX_ATLAS_SIZE / LIGHTGRID_DEFAULT_SINGLE_PROBE_SIZE ) *
	( LIGHTGRID_MAX_ATLAS_SIZE / LIGHTGRID_DEFAULT_SINGLE_PROBE_SIZE );
static const int LIGHTGRID_BAKE_RGB_BYTES_PER_PIXEL = 3;
static const int LIGHTGRID_BAKE_MAX_WORKERS = 8;
static const int LIGHTGRID_BAKE_MAX_ASYNC_READBACK_SLOTS = 16;
static const int LIGHTGRID_BAKE_IDLE_SLEEP_MSEC = 1;
static const int LIGHTGRID_BAKE_QUEUE_CRITICAL_SECTION = CRITICAL_SECTION_TWO;

typedef struct lightGridBakeProgress_s {
	const char *		jobName;
	int					totalAreas;
	int					totalBounces;
	int					totalValidProbes;
	int					areasProcessed;
	int					bounceIndex;
	int					lastPrintTime;
	int					globalProcessedProbes;
} lightGridBakeProgress_t;

typedef struct lightGridStagedWrite_s {
	idStr				finalName;
	idStr				tempName;
} lightGridStagedWrite_t;

enum lightGridPackChunkKind_t {
	LIGHTGRID_PACK_CHUNK_IRRADIANCE = 1,
	LIGHTGRID_PACK_CHUNK_VISIBILITY = 2,
	LIGHTGRID_PACK_CHUNK_PROBE = 3
};

typedef struct lightGridPackChunkDirectory_s {
	int					areaIndex;
	int					kind;
	int					format;
	int					width;
	int					height;
	int					dataOffset;
	int					dataBytes;
} lightGridPackChunkDirectory_t;

static bool LightGrid_PackedImageRefIsValid( const lightGridPackedImageRef_t &ref );

enum lightGridPackedImageLoadResult_t {
	LIGHTGRID_PACKED_IMAGE_LOAD_FAILED,
	LIGHTGRID_PACKED_IMAGE_LOAD_ALREADY_RESIDENT,
	LIGHTGRID_PACKED_IMAGE_LOAD_MATERIALIZED
};

typedef struct lightGridBakeProbeTask_s {
	int					atlasX;
	int					atlasY;
	int					probeIndex;
	idVec3				origin;
} lightGridBakeProbeTask_t;

typedef struct lightGridBakeAreaPlan_s {
	int								areaIndex;
	int								atlasWidth;
	int								atlasHeight;
	int								validProbeCount;
	idList<lightGridBakeProbeTask_t>	probeTasks;
} lightGridBakeAreaPlan_t;

typedef struct lightGridBakeAreaFileStats_s {
	int					areaIndex;
	int					totalProbes;
	int					validProbes;
	int					invalidProbes;
	int					relocatedProbes;
	int					nearSolidProbes;
	int					atlasWidth;
	int					atlasHeight;
} lightGridBakeAreaFileStats_t;

typedef struct lightGridBakeFileStats_s {
	uint32_t			settingsHash;
	int					numPortalAreas;
	int					bakeAreaCount;
	int					totalProbes;
	int					validProbes;
	int					invalidProbes;
	int					relocatedProbes;
	int					nearSolidProbes;
	int					maxAtlasWidth;
	int					maxAtlasHeight;
	idList<lightGridBakeAreaFileStats_t> areas;
} lightGridBakeFileStats_t;

typedef struct lightGridBakeRunStats_s {
	int					layoutMsec;
	int					captureMsec;
	int					integrationMsec;
	int					atlasWriteMsec;
	int					metadataMsec;
	int					commitMsec;
	int					reloadMsec;
	int					visibilityMsec;
	int					capturedProbeCount;
	int					capturedFaceCount;
	int					captureBatchCount;
	int					processedProbeCount;
	int					visibilityProbeCount;
	int					visibilityTraceCount;
	int					atlasCount;
	int					atlasRowCount;
} lightGridBakeRunStats_t;

void R_ReadTiledPixels( int width, int height, byte *buffer, renderView_t *ref );
static void LightGrid_CaptureViewRGB( int width, int height, int blends, renderView_t *ref, byte *rgbOut );
static void LightGrid_BakeProbeTile( byte *outTile, int probeSize, int borderSize, byte *cubeFaces[6], int captureSize, int sampleCount );
static void LightGrid_BakeProbeVisibilityTile( byte *outTile, int probeSize, int borderSize, const idVec3 &probeOrigin, const idRenderWorld *world, float &meanDistance, float &meanDistanceSq, int &traceCount );
static void LightGrid_PrintBakeProbeProgress( lightGridBakeProgress_t &progress, int areaIndex, int areaProbeIndex, int areaProbeCount );
static void LightGrid_RemoveStagedOutputFile( const idStr &relativePath );

typedef struct lightGridBakeJob_s {
	int					atlasX;
	int					atlasY;
	int					probeSize;
	int					borderSize;
	int					captureSize;
	int					sampleCount;
	int					faceBytes;
	int					pendingReadbacks;
	idVec3				probeOrigin;
	int					probeIndex;
	float				visibilityMeanDistance;
	float				visibilityMeanDistanceSq;
	byte *				capturedFaces;
	byte *				probeTile;
	byte *				visibilityTile;

	lightGridBakeJob_s( int _atlasX, int _atlasY, int _probeIndex, const idVec3 &_probeOrigin, int _probeSize, int _borderSize, int _captureSize, int _sampleCount, int _faceBytes )
		: atlasX( _atlasX )
		, atlasY( _atlasY )
		, probeSize( _probeSize )
		, borderSize( _borderSize )
		, captureSize( _captureSize )
		, sampleCount( _sampleCount )
		, faceBytes( _faceBytes )
		, pendingReadbacks( 0 )
		, probeOrigin( _probeOrigin )
		, probeIndex( _probeIndex )
		, visibilityMeanDistance( LIGHTGRID_VISIBILITY_MAX_DISTANCE )
		, visibilityMeanDistanceSq( LIGHTGRID_VISIBILITY_MAX_DISTANCE * LIGHTGRID_VISIBILITY_MAX_DISTANCE )
		, capturedFaces( NULL )
		, probeTile( NULL )
		, visibilityTile( NULL ) {
		capturedFaces = new byte[ faceBytes * 6 ];
		probeTile = new byte[ probeSize * probeSize * LIGHTGRID_BAKE_RGB_BYTES_PER_PIXEL ];
		visibilityTile = new byte[ probeSize * probeSize * LIGHTGRID_BAKE_RGB_BYTES_PER_PIXEL ];
	}

	~lightGridBakeJob_s() {
		delete[] capturedFaces;
		delete[] probeTile;
		delete[] visibilityTile;
	}
} lightGridBakeJob_t;

class LightGridBakeWorkerPool {
public:
	explicit LightGridBakeWorkerPool( int requestedWorkerCount )
		: stopRequested( false )
		, submittedJobs( 0 )
		, drainedJobs( 0 ) {
		const int workerCount = idMath::ClampInt( 0, LIGHTGRID_BAKE_MAX_WORKERS, requestedWorkerCount );
		if ( workerCount <= 0 ) {
			return;
		}

		workers.SetNum( workerCount );
		for ( int i = 0; i < workerCount; i++ ) {
			memset( &workers[ i ], 0, sizeof( workers[ i ] ) );
			Sys_CreateThread( LightGridBakeWorkerPool::WorkerThreadMain, this, THREAD_NORMAL, workers[ i ], "LightGridBake", g_threads, &g_thread_count );
		}
	}

	~LightGridBakeWorkerPool() {
		Shutdown();
	}

	bool IsEnabled() const {
		return workers.Num() > 0;
	}

	int WorkerCount() const {
		return workers.Num();
	}

	int OutstandingJobs() {
		int outstandingJobs = 0;
		Sys_EnterCriticalSection( LIGHTGRID_BAKE_QUEUE_CRITICAL_SECTION );
		outstandingJobs = submittedJobs - drainedJobs;
		Sys_LeaveCriticalSection( LIGHTGRID_BAKE_QUEUE_CRITICAL_SECTION );
		return Max( outstandingJobs, 0 );
	}

	void Submit( lightGridBakeJob_t *job ) {
		if ( job == NULL || !IsEnabled() ) {
			return;
		}

		Sys_EnterCriticalSection( LIGHTGRID_BAKE_QUEUE_CRITICAL_SECTION );
		pendingJobs.Append( job );
		submittedJobs++;
		Sys_LeaveCriticalSection( LIGHTGRID_BAKE_QUEUE_CRITICAL_SECTION );
	}

	bool TryPopCompleted( lightGridBakeJob_t *&job ) {
		job = NULL;

		Sys_EnterCriticalSection( LIGHTGRID_BAKE_QUEUE_CRITICAL_SECTION );
		if ( completedJobs.Num() <= 0 ) {
			Sys_LeaveCriticalSection( LIGHTGRID_BAKE_QUEUE_CRITICAL_SECTION );
			return false;
		}

		job = completedJobs[ 0 ];
		completedJobs.RemoveIndex( 0 );
		drainedJobs++;
		Sys_LeaveCriticalSection( LIGHTGRID_BAKE_QUEUE_CRITICAL_SECTION );
		return true;
	}

	bool WaitPopCompleted( lightGridBakeJob_t *&job ) {
		for ( ;; ) {
			if ( TryPopCompleted( job ) ) {
				return true;
			}

			if ( !IsEnabled() ) {
				job = NULL;
				return false;
			}

			Sys_Sleep( LIGHTGRID_BAKE_IDLE_SLEEP_MSEC );
		}
	}

private:
	static unsigned int WorkerThreadMain( void *parms ) {
		LightGridBakeWorkerPool *pool = static_cast<LightGridBakeWorkerPool *>( parms );
		if ( pool != NULL ) {
			pool->WorkerMain();
		}
		return 0;
	}

	bool TryPopPending( lightGridBakeJob_t *&job, bool &shouldStop ) {
		job = NULL;
		shouldStop = false;

		Sys_EnterCriticalSection( LIGHTGRID_BAKE_QUEUE_CRITICAL_SECTION );
		if ( pendingJobs.Num() > 0 ) {
			job = pendingJobs[ 0 ];
			pendingJobs.RemoveIndex( 0 );
		} else {
			shouldStop = stopRequested;
		}
		Sys_LeaveCriticalSection( LIGHTGRID_BAKE_QUEUE_CRITICAL_SECTION );

		return job != NULL;
	}

	void Shutdown() {
		if ( !IsEnabled() ) {
			return;
		}

		Sys_EnterCriticalSection( LIGHTGRID_BAKE_QUEUE_CRITICAL_SECTION );
		stopRequested = true;
		Sys_LeaveCriticalSection( LIGHTGRID_BAKE_QUEUE_CRITICAL_SECTION );

		for ( int i = 0; i < workers.Num(); i++ ) {
			if ( workers[ i ].threadHandle != 0 ) {
				Sys_DestroyThread( workers[ i ] );
			}
		}

		workers.Clear();

		Sys_EnterCriticalSection( LIGHTGRID_BAKE_QUEUE_CRITICAL_SECTION );
		pendingJobs.DeleteContents( true );
		completedJobs.DeleteContents( true );
		submittedJobs = 0;
		drainedJobs = 0;
		Sys_LeaveCriticalSection( LIGHTGRID_BAKE_QUEUE_CRITICAL_SECTION );
	}

	void WorkerMain() {
		for ( ;; ) {
			if ( Sys_IsCurrentThreadStopRequested() ) {
				return;
			}

			lightGridBakeJob_t *job = NULL;
			bool shouldStop = false;
			if ( !TryPopPending( job, shouldStop ) ) {
				if ( shouldStop ) {
					return;
				}

				Sys_Sleep( LIGHTGRID_BAKE_IDLE_SLEEP_MSEC );
				continue;
			}

			if ( job != NULL ) {
				byte *cubeFaces[6];
				for ( int side = 0; side < 6; side++ ) {
					cubeFaces[ side ] = job->capturedFaces + side * job->faceBytes;
				}

				LightGrid_BakeProbeTile(
					job->probeTile,
					job->probeSize,
					job->borderSize,
					cubeFaces,
					job->captureSize,
					job->sampleCount );
			}

			Sys_EnterCriticalSection( LIGHTGRID_BAKE_QUEUE_CRITICAL_SECTION );
			completedJobs.Append( job );
			Sys_LeaveCriticalSection( LIGHTGRID_BAKE_QUEUE_CRITICAL_SECTION );
		}
	}

	bool							stopRequested;
	int								submittedJobs;
	int								drainedJobs;
	idList<lightGridBakeJob_t *>	pendingJobs;
	idList<lightGridBakeJob_t *>	completedJobs;
	idList<xthreadInfo>				workers;
};

typedef struct lightGridBakeReadbackSlot_s {
	GLuint				pbo;
	lightGridBakeJob_t *	job;
	int					faceIndex;
	bool				inUse;
} lightGridBakeReadbackSlot_t;

static void LightGrid_CopyBottomUpRGB( int width, int height, const byte *srcBottomUp, byte *dstTopDown ) {
	for ( int y = 0; y < height; y++ ) {
		memcpy(
			dstTopDown + y * width * LIGHTGRID_BAKE_RGB_BYTES_PER_PIXEL,
			srcBottomUp + ( ( height - 1 - y ) * width ) * LIGHTGRID_BAKE_RGB_BYTES_PER_PIXEL,
			width * LIGHTGRID_BAKE_RGB_BYTES_PER_PIXEL );
	}
}

static void LightGrid_RenderCaptureScene( int width, int height, renderView_t *ref ) {
	const bool oldUseScissor = r_useScissor.GetBool();

	tr.tiledViewport[0] = width;
	tr.tiledViewport[1] = height;
	tr.viewportOffset[0] = 0;
	tr.viewportOffset[1] = 0;
	r_useScissor.SetBool( false );

	tr.BeginFrame( glConfig.vidWidth, glConfig.vidHeight );
	tr.primaryWorld->RenderScene( ref );

	tr.guiModel->EmitFullScreen();
	tr.guiModel->Clear();

	if ( frameData->cmdHead->commandId != RC_NOP || frameData->cmdHead->next != NULL ) {
		if ( !r_skipBackEnd.GetBool() ) {
			RB_ExecuteBackEndCommands( frameData->cmdHead );
		}
		R_ClearCommandChain();
	}

	glReadBuffer( GL_BACK );

	r_useScissor.SetBool( oldUseScissor );
	tr.viewportOffset[0] = 0;
	tr.viewportOffset[1] = 0;
	tr.tiledViewport[0] = 0;
	tr.tiledViewport[1] = 0;
}

class LightGridBakeReadbackPool {
public:
	LightGridBakeReadbackPool( int requestedSlotCount, int captureSize, int captureBytes )
		: nextDrainSlot( 0 )
		, readbackBytes( captureBytes )
		, readbackSize( captureSize )
		, outstandingReads( 0 )
		, drainedReadbacks( 0 )
		, readbackStallMsec( 0 ) {
		const int slotCount = idMath::ClampInt( 0, LIGHTGRID_BAKE_MAX_ASYNC_READBACK_SLOTS, requestedSlotCount );
		if ( slotCount <= 0 || readbackBytes <= 0 || readbackSize <= 0 ) {
			return;
		}

		slots.SetNum( slotCount );
		idTempArray<GLuint> pboIds( slotCount );
		memset( pboIds.Ptr(), 0, sizeof( GLuint ) * slotCount );
		glGenBuffersARB( slotCount, pboIds.Ptr() );

		for ( int i = 0; i < slotCount; i++ ) {
			if ( pboIds[ i ] == 0 ) {
				if ( glDeleteBuffersARB != NULL ) {
					glDeleteBuffersARB( slotCount, pboIds.Ptr() );
				}
				slots.Clear();
				glBindBufferARB( GL_PIXEL_PACK_BUFFER_ARB, 0 );
				return;
			}
			memset( &slots[ i ], 0, sizeof( slots[ i ] ) );
			slots[ i ].pbo = pboIds[ i ];
			glBindBufferARB( GL_PIXEL_PACK_BUFFER_ARB, slots[ i ].pbo );
			glBufferDataARB( GL_PIXEL_PACK_BUFFER_ARB, readbackBytes, NULL, GL_STREAM_READ_ARB );
		}

		glBindBufferARB( GL_PIXEL_PACK_BUFFER_ARB, 0 );
	}

	~LightGridBakeReadbackPool() {
		if ( slots.Num() <= 0 ) {
			return;
		}

		idTempArray<GLuint> pboIds( slots.Num() );
		for ( int i = 0; i < slots.Num(); i++ ) {
			pboIds[ i ] = slots[ i ].pbo;
		}
		if ( glDeleteBuffersARB != NULL ) {
			glDeleteBuffersARB( slots.Num(), pboIds.Ptr() );
		}
	}

	bool IsEnabled() const {
		return slots.Num() > 0;
	}

	int SlotCount() const {
		return slots.Num();
	}

	int OutstandingReads() const {
		return outstandingReads;
	}

	int DrainedReadbacks() const {
		return drainedReadbacks;
	}

	int ReadbackStallMilliseconds() const {
		return readbackStallMsec;
	}

	bool HasFreeSlot() const {
		return outstandingReads < slots.Num();
	}

	void IssueReadback( renderView_t *ref, lightGridBakeJob_t *job, int faceIndex ) {
		assert( job != NULL );
		assert( faceIndex >= 0 && faceIndex < 6 );
		const int slotIndex = FindFreeSlot();
		assert( slotIndex >= 0 );

		LightGrid_RenderCaptureScene( readbackSize, readbackSize, ref );

		glBindBufferARB( GL_PIXEL_PACK_BUFFER_ARB, slots[ slotIndex ].pbo );
		glBufferDataARB( GL_PIXEL_PACK_BUFFER_ARB, readbackBytes, NULL, GL_STREAM_READ_ARB );
		glPixelStorei( GL_PACK_ALIGNMENT, 1 );
		glReadPixels( 0, 0, readbackSize, readbackSize, GL_RGB, GL_UNSIGNED_BYTE, 0 );
		glPixelStorei( GL_PACK_ALIGNMENT, 4 );
		glBindBufferARB( GL_PIXEL_PACK_BUFFER_ARB, 0 );

		slots[ slotIndex ].job = job;
		slots[ slotIndex ].faceIndex = faceIndex;
		slots[ slotIndex ].inUse = true;
		job->pendingReadbacks++;
		outstandingReads++;
	}

	bool DrainOne( lightGridBakeJob_t *&readyJob ) {
		readyJob = NULL;
		if ( outstandingReads <= 0 ) {
			return false;
		}

		const int slotIndex = FindNextInUseSlot();
		if ( slotIndex < 0 ) {
			return false;
		}

		lightGridBakeReadbackSlot_t &slot = slots[ slotIndex ];
		lightGridBakeJob_t *job = slot.job;
		byte *dst = job->capturedFaces + slot.faceIndex * readbackBytes;

		const int stallStart = Sys_Milliseconds();
		glBindBufferARB( GL_PIXEL_PACK_BUFFER_ARB, slot.pbo );
		const byte *mappedPixels = static_cast<const byte *>( glMapBufferARB( GL_PIXEL_PACK_BUFFER_ARB, GL_READ_ONLY_ARB ) );
		if ( mappedPixels != NULL ) {
			LightGrid_CopyBottomUpRGB( readbackSize, readbackSize, mappedPixels, dst );
			if ( glUnmapBufferARB( GL_PIXEL_PACK_BUFFER_ARB ) == GL_FALSE ) {
				common->Warning( "LightGridBakeReadbackPool: async readback map became invalid; zeroing capture face" );
				memset( dst, 0, readbackBytes );
			}
		} else {
			common->Warning( "LightGridBakeReadbackPool: failed to map async readback buffer; zeroing capture face" );
			memset( dst, 0, readbackBytes );
		}
		glBindBufferARB( GL_PIXEL_PACK_BUFFER_ARB, 0 );
		readbackStallMsec += Sys_Milliseconds() - stallStart;

		slot.job = NULL;
		slot.faceIndex = 0;
		slot.inUse = false;
		outstandingReads--;
		drainedReadbacks++;
		nextDrainSlot = ( slotIndex + 1 ) % slots.Num();

		job->pendingReadbacks--;
		if ( job->pendingReadbacks <= 0 ) {
			job->pendingReadbacks = 0;
			readyJob = job;
		}

		return true;
	}

private:
	int FindFreeSlot() const {
		for ( int i = 0; i < slots.Num(); i++ ) {
			if ( !slots[ i ].inUse ) {
				return i;
			}
		}

		return -1;
	}

	int FindNextInUseSlot() const {
		for ( int i = 0; i < slots.Num(); i++ ) {
			const int slotIndex = ( nextDrainSlot + i ) % slots.Num();
			if ( slots[ slotIndex ].inUse ) {
				return slotIndex;
			}
		}

		return -1;
	}

	idList<lightGridBakeReadbackSlot_t>	slots;
	int									nextDrainSlot;
	int									readbackBytes;
	int									readbackSize;
	int									outstandingReads;
	int									drainedReadbacks;
	int									readbackStallMsec;
};

static const idMat3 *LightGrid_GetCubeAxes() {
	static bool initialized = false;
	static idMat3 axes[6];

	if ( initialized ) {
		return axes;
	}

	memset( axes, 0, sizeof( axes ) );
	axes[0][0][0] = 1;
	axes[0][1][2] = 1;
	axes[0][2][1] = 1;

	axes[1][0][0] = -1;
	axes[1][1][2] = -1;
	axes[1][2][1] = 1;

	axes[2][0][1] = 1;
	axes[2][1][0] = -1;
	axes[2][2][2] = -1;

	axes[3][0][1] = -1;
	axes[3][1][0] = -1;
	axes[3][2][2] = 1;

	axes[4][0][2] = 1;
	axes[4][1][0] = -1;
	axes[4][2][1] = 1;

	axes[5][0][2] = -1;
	axes[5][1][0] = 1;
	axes[5][2][1] = 1;

	initialized = true;
	return axes;
}

static float LightGrid_RadicalInverseVdC( unsigned int bits ) {
	bits = ( bits << 16 ) | ( bits >> 16 );
	bits = ( ( bits & 0x55555555u ) << 1 ) | ( ( bits & 0xAAAAAAAAu ) >> 1 );
	bits = ( ( bits & 0x33333333u ) << 2 ) | ( ( bits & 0xCCCCCCCCu ) >> 2 );
	bits = ( ( bits & 0x0F0F0F0Fu ) << 4 ) | ( ( bits & 0xF0F0F0F0u ) >> 4 );
	bits = ( ( bits & 0x00FF00FFu ) << 8 ) | ( ( bits & 0xFF00FF00u ) >> 8 );
	return static_cast<float>( bits ) * 2.3283064365386963e-10f;
}

static idVec3 LightGrid_CosineSampleHemisphere( float u1, float u2 ) {
	const float r = idMath::Sqrt( idMath::ClampFloat( 0.0f, 1.0f, u1 ) );
	const float phi = 2.0f * idMath::PI * u2;
	const float z = idMath::Sqrt( Max( 0.0f, 1.0f - u1 ) );
	return idVec3( r * idMath::Cos( phi ), r * idMath::Sin( phi ), z );
}

static void LightGrid_BuildBasis( const idVec3 &normal, idVec3 &tangent, idVec3 &bitangent ) {
	const idVec3 up = ( idMath::Fabs( normal.z ) < 0.999f ) ? idVec3( 0.0f, 0.0f, 1.0f ) : idVec3( 0.0f, 1.0f, 0.0f );
	tangent = up.Cross( normal );
	if ( tangent.LengthSqr() < 1.0e-6f ) {
		tangent = idVec3( 1.0f, 0.0f, 0.0f );
	}
	tangent.Normalize();

	bitangent = normal.Cross( tangent );
	if ( bitangent.LengthSqr() < 1.0e-6f ) {
		bitangent = idVec3( 0.0f, 1.0f, 0.0f );
	}
	bitangent.Normalize();
}

static idVec3 LightGrid_DecodeOctahedral( const idVec2 &octCoord ) {
	idVec3 normal( octCoord.x, octCoord.y, 1.0f - idMath::Fabs( octCoord.x ) - idMath::Fabs( octCoord.y ) );
	if ( normal.z < 0.0f ) {
		const float oldX = normal.x;
		normal.x = ( 1.0f - idMath::Fabs( normal.y ) ) * ( oldX >= 0.0f ? 1.0f : -1.0f );
		normal.y = ( 1.0f - idMath::Fabs( oldX ) ) * ( normal.y >= 0.0f ? 1.0f : -1.0f );
	}
	normal.Normalize();
	return normal;
}

static idVec3 LightGrid_DecodeOctahedralTexel( int x, int y, int probeSize, int borderSize ) {
	const float activeSize = static_cast<float>( Max( probeSize - borderSize, 1 ) );
	const float borderHalf = static_cast<float>( borderSize ) * 0.5f;

	const float u = idMath::ClampFloat( 0.0f, 1.0f, ( static_cast<float>( x ) + 0.5f - borderHalf ) / activeSize );
	const float v = idMath::ClampFloat( 0.0f, 1.0f, ( static_cast<float>( y ) + 0.5f - borderHalf ) / activeSize );
	return LightGrid_DecodeOctahedral( idVec2( u * 2.0f - 1.0f, v * 2.0f - 1.0f ) );
}

static idVec3 LightGrid_GetVisibilityTraceDirection( const idVec3 &direction, int sampleIndex ) {
	if ( sampleIndex <= 0 ) {
		return direction;
	}

	idVec3 tangent;
	idVec3 bitangent;
	LightGrid_BuildBasis( direction, tangent, bitangent );

	const float angle = idMath::TWO_PI * static_cast<float>( sampleIndex - 1 ) / static_cast<float>( Max( LIGHTGRID_VISIBILITY_TRACE_SAMPLES - 1, 1 ) );
	idVec3 sampleDirection = direction +
		tangent * ( idMath::Cos( angle ) * LIGHTGRID_VISIBILITY_CONE_OFFSET ) +
		bitangent * ( idMath::Sin( angle ) * LIGHTGRID_VISIBILITY_CONE_OFFSET );
	sampleDirection.Normalize();
	return sampleDirection;
}

static float LightGrid_TraceVisibilityDistance( const idRenderWorld *world, const idVec3 &probeOrigin, const idVec3 &direction ) {
	if ( world == NULL ) {
		return LIGHTGRID_VISIBILITY_MAX_DISTANCE;
	}

	const idVec3 start = probeOrigin + direction * LIGHTGRID_VISIBILITY_TRACE_BIAS;
	const idVec3 end = probeOrigin + direction * LIGHTGRID_VISIBILITY_MAX_DISTANCE;
	modelTrace_t trace;
	if ( world->FastWorldTrace( trace, start, end ) ) {
		return idMath::ClampFloat( 0.0f, LIGHTGRID_VISIBILITY_MAX_DISTANCE, ( trace.point - probeOrigin ).Length() );
	}

	return LIGHTGRID_VISIBILITY_MAX_DISTANCE;
}

static void LightGrid_EncodeVisibilityMoments( byte *pixel, float meanDistance, float meanDistanceSq ) {
	const float invMaxDistance = 1.0f / LIGHTGRID_VISIBILITY_MAX_DISTANCE;
	const float invMaxDistanceSq = invMaxDistance * invMaxDistance;
	pixel[0] = idMath::ClampInt( 0, 255, idMath::FtoiFast( meanDistance * invMaxDistance * 255.0f + 0.5f ) );
	pixel[1] = idMath::ClampInt( 0, 255, idMath::FtoiFast( meanDistanceSq * invMaxDistanceSq * 255.0f + 0.5f ) );
	pixel[2] = 255;
}

static idVec3 LightGrid_GetIdealGridPointOrigin( const LightGrid &lightGrid, const int gridCoord[3] ) {
	return idVec3(
		lightGrid.lightGridOrigin.x + gridCoord[0] * lightGrid.lightGridSize.x,
		lightGrid.lightGridOrigin.y + gridCoord[1] * lightGrid.lightGridSize.y,
		lightGrid.lightGridOrigin.z + gridCoord[2] * lightGrid.lightGridSize.z );
}

static float LightGrid_TraceClearanceDistance( const idRenderWorld *world, const idVec3 &origin, const idVec3 &direction, float maxDistance ) {
	if ( world == NULL ) {
		return maxDistance;
	}

	modelTrace_t trace;
	if ( world->FastWorldTrace( trace, origin, origin + direction * maxDistance ) ) {
		return idMath::ClampFloat( 0.0f, maxDistance, ( trace.point - origin ).Length() );
	}

	return maxDistance;
}

static bool LightGrid_EvaluateProbeCandidate( const idRenderWorld *world, int area, const idVec3 &origin, float clearance, float &minClearance, idVec3 &pushAway ) {
	if ( world == NULL || world->PointInArea( origin ) != area ) {
		return false;
	}

	static const idVec3 directions[] = {
		idVec3( 1.0f, 0.0f, 0.0f ),
		idVec3( -1.0f, 0.0f, 0.0f ),
		idVec3( 0.0f, 1.0f, 0.0f ),
		idVec3( 0.0f, -1.0f, 0.0f ),
		idVec3( 0.0f, 0.0f, 1.0f ),
		idVec3( 0.0f, 0.0f, -1.0f )
	};

	minClearance = clearance;
	pushAway.Zero();

	for ( int i = 0; i < sizeof( directions ) / sizeof( directions[0] ); i++ ) {
		const float distance = LightGrid_TraceClearanceDistance( world, origin, directions[i], clearance );
		minClearance = Min( minClearance, distance );
		if ( distance < clearance ) {
			pushAway -= directions[i] * ( clearance - distance );
		}
	}

	return true;
}

static bool LightGrid_ProbeCandidateIsClear( float minClearance ) {
	return minClearance >= LIGHTGRID_RELOCATION_CLEARANCE * 0.99f;
}

static byte LightGrid_ClassifyProbeState( const idVec3 &baseOrigin, const idVec3 &probeOrigin, float minClearance ) {
	const bool relocated = ( probeOrigin - baseOrigin ).LengthSqr() > 1.0f;
	const bool nearSolid = !LightGrid_ProbeCandidateIsClear( minClearance );

	if ( relocated && nearSolid ) {
		return LIGHTGRID_POINT_RELOCATED_NEAR_SOLID;
	}
	if ( relocated ) {
		return LIGHTGRID_POINT_RELOCATED;
	}
	if ( nearSolid ) {
		return LIGHTGRID_POINT_NEAR_SOLID;
	}
	return LIGHTGRID_POINT_VALID;
}

static byte LightGrid_RelocateProbeOrigin( const idRenderWorld *world, int area, const idVec3 &baseOrigin, idVec3 &probeOrigin ) {
	probeOrigin = baseOrigin;

	float bestClearance = -1.0f;
	float bestDistanceSq = idMath::INFINITY;
	idVec3 bestOrigin = baseOrigin;
	bool foundCandidate = false;

	auto considerCandidate = [&]( const idVec3 &candidate ) {
		const idVec3 offset = candidate - baseOrigin;
		const float distanceSq = offset.LengthSqr();
		if ( distanceSq > LIGHTGRID_RELOCATION_MAX_DISTANCE * LIGHTGRID_RELOCATION_MAX_DISTANCE + 0.01f ) {
			return;
		}

		float candidateClearance = 0.0f;
		idVec3 pushAway;
		if ( !LightGrid_EvaluateProbeCandidate( world, area, candidate, LIGHTGRID_RELOCATION_CLEARANCE, candidateClearance, pushAway ) ) {
			return;
		}

		const bool betterClearance = candidateClearance > bestClearance + 0.01f;
		const bool equalButCloser = idMath::Fabs( candidateClearance - bestClearance ) <= 0.01f && distanceSq < bestDistanceSq;
		if ( !foundCandidate || betterClearance || equalButCloser ) {
			foundCandidate = true;
			bestClearance = candidateClearance;
			bestDistanceSq = distanceSq;
			bestOrigin = candidate;
		}
	};

	float baseClearance = 0.0f;
	idVec3 basePushAway;
	if ( LightGrid_EvaluateProbeCandidate( world, area, baseOrigin, LIGHTGRID_RELOCATION_CLEARANCE, baseClearance, basePushAway ) ) {
		foundCandidate = true;
		bestClearance = baseClearance;
		bestDistanceSq = 0.0f;
		bestOrigin = baseOrigin;

		if ( LightGrid_ProbeCandidateIsClear( baseClearance ) ) {
			probeOrigin = baseOrigin;
			return LIGHTGRID_POINT_VALID;
		}

		if ( basePushAway.LengthSqr() > 0.01f ) {
			idVec3 pushDirection = basePushAway;
			pushDirection.Normalize();
			considerCandidate( baseOrigin + pushDirection * Min( basePushAway.Length() + 2.0f, LIGHTGRID_RELOCATION_MAX_DISTANCE ) );
		}
	}

	for ( float distance = LIGHTGRID_RELOCATION_SEARCH_STEP; distance <= LIGHTGRID_RELOCATION_MAX_DISTANCE; distance += LIGHTGRID_RELOCATION_SEARCH_STEP ) {
		for ( int dx = -1; dx <= 1; dx++ ) {
			for ( int dy = -1; dy <= 1; dy++ ) {
				for ( int dz = -1; dz <= 1; dz++ ) {
					if ( dx == 0 && dy == 0 && dz == 0 ) {
						continue;
					}

					idVec3 offset( static_cast<float>( dx ), static_cast<float>( dy ), static_cast<float>( dz ) );
					offset.Normalize();
					considerCandidate( baseOrigin + offset * distance );
				}
			}
		}

		if ( foundCandidate && LightGrid_ProbeCandidateIsClear( bestClearance ) ) {
			break;
		}
	}

	if ( !foundCandidate ) {
		return LIGHTGRID_POINT_INVALID;
	}

	probeOrigin = bestOrigin;
	return LightGrid_ClassifyProbeState( baseOrigin, probeOrigin, bestClearance );
}

static void LightGrid_EncodeProbeRelocation( byte *pixel, const idVec3 &relocation, float maxDistance ) {
	for ( int i = 0; i < 3; i++ ) {
		const float normalized = idMath::ClampFloat( -1.0f, 1.0f, relocation[i] / maxDistance );
		pixel[i] = idMath::ClampInt( 0, 255, idMath::FtoiFast( normalized * 127.0f + 128.0f + 0.5f ) );
	}
}

static void LightGrid_CaptureViewRGB( int width, int height, int blends, renderView_t *ref, byte *rgbOut ) {
	const int pixelCount = width * height;
	idTempArray<byte> rgbBuffer( pixelCount * 3 );

	if ( blends <= 1 ) {
		R_ReadTiledPixels( width, height, rgbBuffer.Ptr(), ref );
	} else {
		idTempArray<unsigned short> accumBuffer( pixelCount * 3 );
		memset( accumBuffer.Ptr(), 0, pixelCount * 3 * sizeof( unsigned short ) );

		r_jitter.SetBool( true );
		for ( int i = 0; i < blends; i++ ) {
			R_ReadTiledPixels( width, height, rgbBuffer.Ptr(), ref );
			for ( int j = 0; j < pixelCount * 3; j++ ) {
				accumBuffer[ j ] += rgbBuffer[ j ];
			}
		}
		r_jitter.SetBool( false );

		for ( int i = 0; i < pixelCount * 3; i++ ) {
			rgbBuffer[ i ] = accumBuffer[ i ] / blends;
		}
	}

	LightGrid_CopyBottomUpRGB( width, height, rgbBuffer.Ptr(), rgbOut );
}

static void LightGrid_SampleCubeMap( const idVec3 &dir, int size, byte *buffers[6], byte result[3] ) {
	const idMat3 *cubeAxes = LightGrid_GetCubeAxes();

	float axisDistances[3];
	axisDistances[0] = idMath::Fabs( dir.x );
	axisDistances[1] = idMath::Fabs( dir.y );
	axisDistances[2] = idMath::Fabs( dir.z );

	int axis = 0;
	if ( dir.x >= axisDistances[1] && dir.x >= axisDistances[2] ) {
		axis = 0;
	} else if ( -dir.x >= axisDistances[1] && -dir.x >= axisDistances[2] ) {
		axis = 1;
	} else if ( dir.y >= axisDistances[0] && dir.y >= axisDistances[2] ) {
		axis = 2;
	} else if ( -dir.y >= axisDistances[0] && -dir.y >= axisDistances[2] ) {
		axis = 3;
	} else if ( dir.z >= axisDistances[1] && dir.z >= axisDistances[2] ) {
		axis = 4;
	} else {
		axis = 5;
	}

	float fx = ( dir * cubeAxes[ axis ][1] ) / ( dir * cubeAxes[ axis ][0] );
	float fy = ( dir * cubeAxes[ axis ][2] ) / ( dir * cubeAxes[ axis ][0] );

	fx = -fx;
	fy = -fy;

	int x = idMath::FtoiFast( size * 0.5f * ( fx + 1.0f ) );
	int y = idMath::FtoiFast( size * 0.5f * ( fy + 1.0f ) );

	x = idMath::ClampInt( 0, size - 1, x );
	y = idMath::ClampInt( 0, size - 1, y );

	const byte *sample = buffers[ axis ] + ( y * size + x ) * LIGHTGRID_BAKE_RGB_BYTES_PER_PIXEL;
	result[0] = sample[0];
	result[1] = sample[1];
	result[2] = sample[2];
}

static idVec3 LightGrid_ComputeTexelIrradiance( byte *cubeFaces[6], int captureSize, const idVec3 &normal, int sampleCount ) {
	idVec3 tangent;
	idVec3 bitangent;
	LightGrid_BuildBasis( normal, tangent, bitangent );

	idVec3 total( 0.0f, 0.0f, 0.0f );
	byte sample[3];

	for ( int i = 0; i < sampleCount; i++ ) {
		const float u1 = ( static_cast<float>( i ) + 0.5f ) / static_cast<float>( sampleCount );
		const float u2 = LightGrid_RadicalInverseVdC( i );

		const idVec3 localDir = LightGrid_CosineSampleHemisphere( u1, u2 );
		idVec3 worldDir = tangent * localDir.x + bitangent * localDir.y + normal * localDir.z;
		worldDir.Normalize();

		LightGrid_SampleCubeMap( worldDir, captureSize, cubeFaces, sample );
		total.x += sample[0];
		total.y += sample[1];
		total.z += sample[2];
	}

	const float scale = 1.0f / ( 255.0f * Max( sampleCount, 1 ) );
	return total * scale;
}

static void LightGrid_BakeProbeTile( byte *outTile, int probeSize, int borderSize, byte *cubeFaces[6], int captureSize, int sampleCount ) {
	for ( int y = 0; y < probeSize; y++ ) {
		for ( int x = 0; x < probeSize; x++ ) {
			const idVec3 normal = LightGrid_DecodeOctahedralTexel( x, y, probeSize, borderSize );
			const idVec3 irradiance = LightGrid_ComputeTexelIrradiance( cubeFaces, captureSize, normal, sampleCount );
			byte *pixel = outTile + ( y * probeSize + x ) * LIGHTGRID_BAKE_RGB_BYTES_PER_PIXEL;
			pixel[0] = idMath::ClampInt( 0, 255, idMath::FtoiFast( irradiance.x * 255.0f + 0.5f ) );
			pixel[1] = idMath::ClampInt( 0, 255, idMath::FtoiFast( irradiance.y * 255.0f + 0.5f ) );
			pixel[2] = idMath::ClampInt( 0, 255, idMath::FtoiFast( irradiance.z * 255.0f + 0.5f ) );
		}
	}
}

static void LightGrid_BakeProbeVisibilityTile( byte *outTile, int probeSize, int borderSize, const idVec3 &probeOrigin, const idRenderWorld *world, float &meanDistance, float &meanDistanceSq, int &traceCount ) {
	float probeDistanceSum = 0.0f;
	float probeDistanceSqSum = 0.0f;
	int probeSampleCount = 0;
	traceCount = 0;

	for ( int y = 0; y < probeSize; y++ ) {
		for ( int x = 0; x < probeSize; x++ ) {
			const idVec3 direction = LightGrid_DecodeOctahedralTexel( x, y, probeSize, borderSize );
			float distanceSum = 0.0f;
			float distanceSqSum = 0.0f;

			for ( int sampleIndex = 0; sampleIndex < LIGHTGRID_VISIBILITY_TRACE_SAMPLES; sampleIndex++ ) {
				const idVec3 sampleDirection = LightGrid_GetVisibilityTraceDirection( direction, sampleIndex );
				const float distance = LightGrid_TraceVisibilityDistance( world, probeOrigin, sampleDirection );
				distanceSum += distance;
				distanceSqSum += distance * distance;
				traceCount++;
			}

			const float invSampleCount = 1.0f / static_cast<float>( Max( LIGHTGRID_VISIBILITY_TRACE_SAMPLES, 1 ) );
			const float texelMeanDistance = distanceSum * invSampleCount;
			const float texelMeanDistanceSq = distanceSqSum * invSampleCount;
			LightGrid_EncodeVisibilityMoments(
				outTile + ( y * probeSize + x ) * LIGHTGRID_BAKE_RGB_BYTES_PER_PIXEL,
				texelMeanDistance,
				texelMeanDistanceSq );

			probeDistanceSum += texelMeanDistance;
			probeDistanceSqSum += texelMeanDistanceSq;
			probeSampleCount++;
		}
	}

	const float invProbeSampleCount = 1.0f / static_cast<float>( Max( probeSampleCount, 1 ) );
	meanDistance = probeDistanceSum * invProbeSampleCount;
	meanDistanceSq = probeDistanceSqSum * invProbeSampleCount;
}

static void LightGrid_CopyProbeTileToPixels( byte *pixels, int pixelWidth, int pixelBaseAtlasY, const lightGridBakeJob_t &job ) {
	const int rowOffset = job.atlasY - pixelBaseAtlasY;
	for ( int row = 0; row < job.probeSize; row++ ) {
		memcpy(
			pixels + ( ( rowOffset + row ) * pixelWidth + job.atlasX ) * LIGHTGRID_BAKE_RGB_BYTES_PER_PIXEL,
			job.probeTile + row * job.probeSize * LIGHTGRID_BAKE_RGB_BYTES_PER_PIXEL,
			job.probeSize * LIGHTGRID_BAKE_RGB_BYTES_PER_PIXEL );
	}
}

static void LightGrid_CopyVisibilityTileToPixels( byte *pixels, int pixelWidth, int pixelBaseAtlasY, const lightGridBakeJob_t &job ) {
	const int rowOffset = job.atlasY - pixelBaseAtlasY;
	for ( int row = 0; row < job.probeSize; row++ ) {
		memcpy(
			pixels + ( ( rowOffset + row ) * pixelWidth + job.atlasX ) * LIGHTGRID_BAKE_RGB_BYTES_PER_PIXEL,
			job.visibilityTile + row * job.probeSize * LIGHTGRID_BAKE_RGB_BYTES_PER_PIXEL,
			job.probeSize * LIGHTGRID_BAKE_RGB_BYTES_PER_PIXEL );
	}
}

static bool LightGrid_WriteTGA24Header( idFile *file, int width, int height ) {
	byte header[18];
	memset( header, 0, sizeof( header ) );
	header[2] = 2;
	header[12] = width & 255;
	header[13] = width >> 8;
	header[14] = height & 255;
	header[15] = height >> 8;
	header[16] = 24;
	header[17] = ( 1 << 5 );
	return file->Write( header, sizeof( header ) ) == sizeof( header );
}

static bool LightGrid_WriteRGBRowsAsTGA( idFile *file, const byte *rgbPixels, int width, int rowCount ) {
	idTempArray<byte> bgrRow( width * LIGHTGRID_BAKE_RGB_BYTES_PER_PIXEL );

	for ( int row = 0; row < rowCount; row++ ) {
		const byte *src = rgbPixels + row * width * LIGHTGRID_BAKE_RGB_BYTES_PER_PIXEL;
		byte *dst = bgrRow.Ptr();
		for ( int x = 0; x < width; x++ ) {
			dst[ x * 3 + 0 ] = src[ x * 3 + 2 ];
			dst[ x * 3 + 1 ] = src[ x * 3 + 1 ];
			dst[ x * 3 + 2 ] = src[ x * 3 + 0 ];
		}

		if ( file->Write( dst, width * LIGHTGRID_BAKE_RGB_BYTES_PER_PIXEL ) != width * LIGHTGRID_BAKE_RGB_BYTES_PER_PIXEL ) {
			return false;
		}
	}

	return true;
}

static int LightGrid_GetProbePositionAtlasWidth( const LightGrid &lightGrid ) {
	return Max( lightGrid.lightGridBounds[0] * lightGrid.lightGridBounds[2], 1 );
}

static int LightGrid_GetProbePositionAtlasHeight( const LightGrid &lightGrid ) {
	return Max( lightGrid.lightGridBounds[1], 1 );
}

static bool LightGrid_WriteProbePositionAtlas( idFile *file, const LightGrid &lightGrid ) {
	const int width = LightGrid_GetProbePositionAtlasWidth( lightGrid );
	const int height = LightGrid_GetProbePositionAtlasHeight( lightGrid );
	if ( !LightGrid_WriteTGA24Header( file, width, height ) ) {
		return false;
	}

	idTempArray<byte> pixels( width * height * LIGHTGRID_BAKE_RGB_BYTES_PER_PIXEL );
	pixels.Zero();

	for ( int y = 0; y < lightGrid.lightGridBounds[1]; y++ ) {
		for ( int x = 0; x < lightGrid.lightGridBounds[0]; x++ ) {
			for ( int z = 0; z < lightGrid.lightGridBounds[2]; z++ ) {
				const int gridCoord[3] = { x, y, z };
				const int probeIndex = lightGrid.GridCoordToProbeIndex( gridCoord );
				if ( probeIndex < 0 || probeIndex >= lightGrid.lightGridPoints.Num() ) {
					continue;
				}

				const lightGridPoint_t &gridPoint = lightGrid.lightGridPoints[ probeIndex ];
				const idVec3 idealOrigin = LightGrid_GetIdealGridPointOrigin( lightGrid, gridCoord );
				const int pixelX = x + z * lightGrid.lightGridBounds[0];
				byte *pixel = pixels.Ptr() + ( y * width + pixelX ) * LIGHTGRID_BAKE_RGB_BYTES_PER_PIXEL;
				LightGrid_EncodeProbeRelocation( pixel, gridPoint.origin - idealOrigin, lightGrid.relocationMaxDistance );
			}
		}
	}

	return LightGrid_WriteRGBRowsAsTGA( file, pixels.Ptr(), width, height );
}

static bool LightGrid_WritePackInt( idFile *file, int value ) {
	return file != NULL && file->WriteInt( value ) == sizeof( value );
}

static bool LightGrid_WritePackUnsignedInt( idFile *file, unsigned int value ) {
	return file != NULL && file->WriteUnsignedInt( value ) == sizeof( value );
}

static bool LightGrid_WritePackFloat( idFile *file, float value ) {
	return file != NULL && file->WriteFloat( value ) == sizeof( value );
}

static bool LightGrid_WritePackByte( idFile *file, byte value ) {
	return file != NULL && file->WriteUnsignedChar( value ) == sizeof( value );
}

static bool LightGrid_ReadPackInt( idFile *file, int &value ) {
	return file != NULL && file->ReadInt( value ) == sizeof( value );
}

static bool LightGrid_ReadPackUnsignedInt( idFile *file, unsigned int &value ) {
	return file != NULL && file->ReadUnsignedInt( value ) == sizeof( value );
}

static bool LightGrid_ReadPackFloat( idFile *file, float &value ) {
	return file != NULL && file->ReadFloat( value ) == sizeof( value );
}

static bool LightGrid_ReadPackByte( idFile *file, byte &value ) {
	return file != NULL && file->ReadUnsignedChar( value ) == sizeof( value );
}

static bool LightGrid_WritePackVec3( idFile *file, const idVec3 &value ) {
	return LightGrid_WritePackFloat( file, value.x ) &&
		LightGrid_WritePackFloat( file, value.y ) &&
		LightGrid_WritePackFloat( file, value.z );
}

static bool LightGrid_ReadPackVec3( idFile *file, idVec3 &value ) {
	return LightGrid_ReadPackFloat( file, value.x ) &&
		LightGrid_ReadPackFloat( file, value.y ) &&
		LightGrid_ReadPackFloat( file, value.z );
}

static bool LightGrid_WritePackChunkDirectoryEntry( idFile *file, const lightGridPackChunkDirectory_t &chunk ) {
	return LightGrid_WritePackInt( file, chunk.areaIndex ) &&
		LightGrid_WritePackInt( file, chunk.kind ) &&
		LightGrid_WritePackInt( file, chunk.format ) &&
		LightGrid_WritePackInt( file, chunk.width ) &&
		LightGrid_WritePackInt( file, chunk.height ) &&
		LightGrid_WritePackInt( file, chunk.dataOffset ) &&
		LightGrid_WritePackInt( file, chunk.dataBytes );
}

static bool LightGrid_ReadPackChunkDirectoryEntry( idFile *file, lightGridPackChunkDirectory_t &chunk ) {
	return LightGrid_ReadPackInt( file, chunk.areaIndex ) &&
		LightGrid_ReadPackInt( file, chunk.kind ) &&
		LightGrid_ReadPackInt( file, chunk.format ) &&
		LightGrid_ReadPackInt( file, chunk.width ) &&
		LightGrid_ReadPackInt( file, chunk.height ) &&
		LightGrid_ReadPackInt( file, chunk.dataOffset ) &&
		LightGrid_ReadPackInt( file, chunk.dataBytes );
}

static void LightGrid_BuildAreaBakePlan( const LightGrid &lightGrid, lightGridBakeAreaPlan_t &areaPlan ) {
	areaPlan.areaIndex = lightGrid.area;
	areaPlan.atlasWidth = Max( lightGrid.lightGridBounds[0] * lightGrid.lightGridBounds[2], 1 ) * lightGrid.imageSingleProbeSize;
	areaPlan.atlasHeight = Max( lightGrid.lightGridBounds[1], 1 ) * lightGrid.imageSingleProbeSize;
	areaPlan.validProbeCount = 0;
	areaPlan.probeTasks.Clear();

	for ( int y = 0; y < lightGrid.lightGridBounds[1]; y++ ) {
		for ( int x = 0; x < lightGrid.lightGridBounds[0]; x++ ) {
			for ( int z = 0; z < lightGrid.lightGridBounds[2]; z++ ) {
				const int gridCoord[3] = { x, y, z };
				const lightGridPoint_t &gridPoint = lightGrid.lightGridPoints[ lightGrid.GridCoordToProbeIndex( gridCoord ) ];
				if ( !gridPoint.valid ) {
					continue;
				}

				lightGridBakeProbeTask_t task;
				task.origin = gridPoint.origin;
				task.probeIndex = lightGrid.GridCoordToProbeIndex( gridCoord );
				task.atlasX = ( x + z * lightGrid.lightGridBounds[0] ) * lightGrid.imageSingleProbeSize;
				task.atlasY = y * lightGrid.imageSingleProbeSize;
				areaPlan.probeTasks.Append( task );
				areaPlan.validProbeCount++;
			}
		}
	}
}

static int LightGrid_GetAtlasWidth( const LightGrid &lightGrid ) {
	return Max( lightGrid.lightGridBounds[0] * lightGrid.lightGridBounds[2], 1 ) * lightGrid.imageSingleProbeSize;
}

static int LightGrid_GetAtlasHeight( const LightGrid &lightGrid ) {
	return Max( lightGrid.lightGridBounds[1], 1 ) * lightGrid.imageSingleProbeSize;
}

static void LightGrid_InitBakeFileStats( lightGridBakeFileStats_t &stats ) {
	stats.settingsHash = 0;
	stats.numPortalAreas = 0;
	stats.bakeAreaCount = 0;
	stats.totalProbes = 0;
	stats.validProbes = 0;
	stats.invalidProbes = 0;
	stats.relocatedProbes = 0;
	stats.nearSolidProbes = 0;
	stats.maxAtlasWidth = 0;
	stats.maxAtlasHeight = 0;
	stats.areas.Clear();
}

static void LightGrid_InitBakeRunStats( lightGridBakeRunStats_t &stats ) {
	stats.layoutMsec = 0;
	stats.captureMsec = 0;
	stats.integrationMsec = 0;
	stats.atlasWriteMsec = 0;
	stats.metadataMsec = 0;
	stats.commitMsec = 0;
	stats.reloadMsec = 0;
	stats.visibilityMsec = 0;
	stats.capturedProbeCount = 0;
	stats.capturedFaceCount = 0;
	stats.captureBatchCount = 0;
	stats.processedProbeCount = 0;
	stats.visibilityProbeCount = 0;
	stats.visibilityTraceCount = 0;
	stats.atlasCount = 0;
	stats.atlasRowCount = 0;
}

static void LightGrid_AddAreaFileStats( lightGridBakeFileStats_t &stats, const LightGrid &lightGrid ) {
	lightGridBakeAreaFileStats_t areaStats;
	areaStats.areaIndex = lightGrid.area;
	areaStats.totalProbes = lightGrid.GridPointCount();
	areaStats.validProbes = lightGrid.CountValidGridPoints();
	areaStats.invalidProbes = Max( areaStats.totalProbes - areaStats.validProbes, 0 );
	areaStats.relocatedProbes = lightGrid.CountRelocatedGridPoints();
	areaStats.nearSolidProbes = lightGrid.CountNearSolidGridPoints();
	areaStats.atlasWidth = LightGrid_GetAtlasWidth( lightGrid );
	areaStats.atlasHeight = LightGrid_GetAtlasHeight( lightGrid );

	stats.areas.Append( areaStats );
	stats.numPortalAreas++;
	stats.totalProbes += areaStats.totalProbes;
	stats.validProbes += areaStats.validProbes;
	stats.invalidProbes += areaStats.invalidProbes;
	stats.relocatedProbes += areaStats.relocatedProbes;
	stats.nearSolidProbes += areaStats.nearSolidProbes;
	stats.maxAtlasWidth = Max( stats.maxAtlasWidth, areaStats.atlasWidth );
	stats.maxAtlasHeight = Max( stats.maxAtlasHeight, areaStats.atlasHeight );
	if ( areaStats.validProbes > 0 ) {
		stats.bakeAreaCount++;
	}
}

static void LightGrid_HashBytes( uint32_t &hash, const void *data, int bytes ) {
	CRC32_UpdateChecksum( hash, data, bytes );
}

static void LightGrid_HashInt( uint32_t &hash, int value ) {
	LightGrid_HashBytes( hash, &value, sizeof( value ) );
}

static void LightGrid_HashFloat( uint32_t &hash, float value ) {
	LightGrid_HashBytes( hash, &value, sizeof( value ) );
}

static void LightGrid_HashVec3( uint32_t &hash, const idVec3 &value ) {
	LightGrid_HashFloat( hash, value.x );
	LightGrid_HashFloat( hash, value.y );
	LightGrid_HashFloat( hash, value.z );
}

static void LightGrid_HashString( uint32_t &hash, const char *value ) {
	const char *safeValue = ( value != NULL ) ? value : "";
	CRC32_UpdateChecksum( hash, safeValue, idStr::Length( safeValue ) + 1 );
}

static void LightGrid_HashFileContents( uint32_t &hash, const char *relativePath ) {
	LightGrid_HashString( hash, relativePath );

	void *buffer = NULL;
	const int length = fileSystem->ReadFile( relativePath, &buffer, NULL );
	LightGrid_HashInt( hash, length );
	if ( length > 0 && buffer != NULL ) {
		CRC32_UpdateChecksum( hash, buffer, length );
	}
	if ( buffer != NULL ) {
		fileSystem->FreeFile( buffer );
	}
}

static uint32_t LightGrid_CalculateBakeSettingsHash( const lightGridBakeOptions_t &options, const idRenderWorldLocal *world ) {
	uint32_t hash;
	CRC32_InitChecksum( hash );

	LightGrid_HashString( hash, "openQ4 lightgrid bake settings" );
	LightGrid_HashInt( hash, LIGHTGRID_CURRENT_VERSION );
	LightGrid_HashInt( hash, LIGHTGRID_BAKE_HEADER_VERSION );
	LightGrid_HashInt( hash, options.maxProbes );
	LightGrid_HashInt( hash, options.bounces );
	LightGrid_HashInt( hash, options.captureSize );
	LightGrid_HashInt( hash, options.blends );
	LightGrid_HashInt( hash, options.samples );
	LightGrid_HashVec3( hash, options.gridSize );
	LightGrid_HashInt( hash, LIGHTGRID_DEFAULT_SINGLE_PROBE_SIZE );
	LightGrid_HashInt( hash, LIGHTGRID_DEFAULT_BORDER_SIZE );
	LightGrid_HashInt( hash, LIGHTGRID_MAX_ATLAS_SIZE );
	LightGrid_HashFloat( hash, LIGHTGRID_VISIBILITY_MAX_DISTANCE );
	LightGrid_HashFloat( hash, LIGHTGRID_VISIBILITY_TRACE_BIAS );
	LightGrid_HashInt( hash, LIGHTGRID_VISIBILITY_TRACE_SAMPLES );
	LightGrid_HashFloat( hash, LIGHTGRID_VISIBILITY_CONE_OFFSET );
	LightGrid_HashFloat( hash, LIGHTGRID_RELOCATION_CLEARANCE );
	LightGrid_HashFloat( hash, LIGHTGRID_RELOCATION_MAX_DISTANCE );
	LightGrid_HashFloat( hash, LIGHTGRID_RELOCATION_SEARCH_STEP );

	if ( world != NULL ) {
		LightGrid_HashString( hash, world->mapName.c_str() );
		LightGrid_HashFileContents( hash, world->mapName.c_str() );
		idStr procName = world->mapName;
		procName.SetFileExtension( "proc" );
		LightGrid_HashFileContents( hash, procName.c_str() );
		LightGrid_HashInt( hash, world->numPortalAreas );
		for ( int areaIndex = 0; areaIndex < world->numPortalAreas; areaIndex++ ) {
			const portalArea_t &area = world->portalAreas[ areaIndex ];
			const LightGrid &lightGrid = area.lightGrid;

			LightGrid_HashInt( hash, areaIndex );
			LightGrid_HashVec3( hash, area.globalBounds[0] );
			LightGrid_HashVec3( hash, area.globalBounds[1] );
			LightGrid_HashInt( hash, lightGrid.area );
			LightGrid_HashVec3( hash, lightGrid.lightGridOrigin );
			LightGrid_HashVec3( hash, lightGrid.lightGridSize );
			LightGrid_HashInt( hash, lightGrid.lightGridBounds[0] );
			LightGrid_HashInt( hash, lightGrid.lightGridBounds[1] );
			LightGrid_HashInt( hash, lightGrid.lightGridBounds[2] );
			LightGrid_HashInt( hash, lightGrid.imageSingleProbeSize );
			LightGrid_HashInt( hash, lightGrid.imageBorderSize );
			LightGrid_HashFloat( hash, lightGrid.visibilityMaxDistance );
			LightGrid_HashFloat( hash, lightGrid.relocationMaxDistance );
			LightGrid_HashInt( hash, lightGrid.GridPointCount() );
			LightGrid_HashInt( hash, lightGrid.CountValidGridPoints() );

			if ( lightGrid.HasPointData() ) {
				for ( int probeIndex = 0; probeIndex < lightGrid.lightGridPoints.Num(); probeIndex++ ) {
					const lightGridPoint_t &gridPoint = lightGrid.lightGridPoints[ probeIndex ];
					LightGrid_HashInt( hash, static_cast<int>( gridPoint.valid ) );
					LightGrid_HashVec3( hash, gridPoint.origin );
				}
			}
		}
	}

	CRC32_FinishChecksum( hash );
	return hash;
}

static int LightGrid_GetBakeTransientMemoryBudgetBytes() {
	return idMath::ClampInt( 4, 256, r_lightGridBakeMemoryMB.GetInteger() ) * 1024 * 1024;
}

static int LightGrid_GetCaptureBatchProbeCount( const LightGrid &lightGrid, int faceBytes, const LightGridBakeWorkerPool &bakeWorkerPool ) {
	const int budgetBytes = LightGrid_GetBakeTransientMemoryBudgetBytes();
	const int perProbeBytes =
		faceBytes * 6 +
		lightGrid.imageSingleProbeSize * lightGrid.imageSingleProbeSize * LIGHTGRID_BAKE_RGB_BYTES_PER_PIXEL * 2;
	const int maxByBudget = Max( budgetBytes / Max( perProbeBytes, 1 ), 1 );
	const int preferred = bakeWorkerPool.IsEnabled() ? Max( bakeWorkerPool.WorkerCount() * 2, 2 ) : 4;
	return idMath::ClampInt( 1, 64, Min( preferred, maxByBudget ) );
}

static lightGridBakeJob_t *LightGrid_AllocBakeJob( const lightGridBakeProbeTask_t &task, const LightGrid &lightGrid, const lightGridBakeOptions_t &options, int faceBytes ) {
	return new lightGridBakeJob_t(
		task.atlasX,
		task.atlasY,
		task.probeIndex,
		task.origin,
		lightGrid.imageSingleProbeSize,
		lightGrid.imageBorderSize,
		options.captureSize,
		options.samples,
		faceBytes );
}

static void LightGrid_CaptureProbeFacesSync( const lightGridBakeProbeTask_t &task, const lightGridBakeOptions_t &options, renderView_t &captureView, const idMat3 *cubeAxes, lightGridBakeJob_t &job ) {
	captureView.vieworg = task.origin;
	for ( int side = 0; side < 6; side++ ) {
		captureView.viewaxis = cubeAxes[ side ];
		LightGrid_CaptureViewRGB(
			options.captureSize,
			options.captureSize,
			options.blends,
			&captureView,
			job.capturedFaces + side * job.faceBytes );
	}
}

static void LightGrid_CaptureProbeBatch(
	const idList<lightGridBakeProbeTask_t> &probeTasks,
	int firstTaskIndex,
	int taskCount,
	LightGrid &lightGrid,
	const idRenderWorld *world,
	const lightGridBakeOptions_t &options,
	renderView_t &captureView,
	const idMat3 *cubeAxes,
	LightGridBakeReadbackPool &readbackPool,
	int faceBytes,
	idList<lightGridBakeJob_t *> &capturedJobs,
	lightGridBakeRunStats_t &runStats ) {
	const int captureStart = Sys_Milliseconds();
	capturedJobs.Clear();

	auto drainReadbackJob = [&]() {
		lightGridBakeJob_t *readyJob = NULL;
		if ( readbackPool.DrainOne( readyJob ) && readyJob != NULL ) {
			capturedJobs.Append( readyJob );
		}
	};

	for ( int taskIndex = 0; taskIndex < taskCount; taskIndex++ ) {
		const lightGridBakeProbeTask_t &task = probeTasks[ firstTaskIndex + taskIndex ];
		lightGridBakeJob_t *job = LightGrid_AllocBakeJob( task, lightGrid, options, faceBytes );
		const int visibilityStart = Sys_Milliseconds();
		int visibilityTraceCount = 0;
		LightGrid_BakeProbeVisibilityTile(
			job->visibilityTile,
			job->probeSize,
			job->borderSize,
			job->probeOrigin,
			world,
			job->visibilityMeanDistance,
			job->visibilityMeanDistanceSq,
			visibilityTraceCount );
		runStats.visibilityMsec += Sys_Milliseconds() - visibilityStart;
		runStats.visibilityProbeCount++;
		runStats.visibilityTraceCount += visibilityTraceCount;
		if ( task.probeIndex >= 0 && task.probeIndex < lightGrid.lightGridPoints.Num() ) {
			lightGridPoint_t &gridPoint = lightGrid.lightGridPoints[ task.probeIndex ];
			gridPoint.visibilityMeanDistance = job->visibilityMeanDistance;
			gridPoint.visibilityMeanDistanceSq = job->visibilityMeanDistanceSq;
		}

		if ( readbackPool.IsEnabled() ) {
			captureView.vieworg = task.origin;
			for ( int side = 0; side < 6; side++ ) {
				captureView.viewaxis = cubeAxes[ side ];
				while ( !readbackPool.HasFreeSlot() ) {
					drainReadbackJob();
				}
				readbackPool.IssueReadback( &captureView, job, side );
			}
		} else {
			LightGrid_CaptureProbeFacesSync( task, options, captureView, cubeAxes, *job );
			capturedJobs.Append( job );
		}
	}

	if ( readbackPool.IsEnabled() ) {
		while ( readbackPool.OutstandingReads() > 0 ) {
			drainReadbackJob();
		}
	}

	runStats.captureMsec += Sys_Milliseconds() - captureStart;
	runStats.captureBatchCount++;
	runStats.capturedProbeCount += taskCount;
	runStats.capturedFaceCount += taskCount * 6;
}

static void LightGrid_ProcessCapturedJobs(
	idList<lightGridBakeJob_t *> &capturedJobs,
	LightGridBakeWorkerPool &bakeWorkerPool,
	byte *targetPixels,
	byte *targetVisibilityPixels,
	int targetWidth,
	int pixelBaseAtlasY,
	lightGridBakeProgress_t &progress,
	int areaOrdinal,
	int areaProbeCount,
	int &processedProbes,
	lightGridBakeRunStats_t &runStats ) {
	if ( capturedJobs.Num() <= 0 ) {
		return;
	}

	const int integrationStart = Sys_Milliseconds();
	if ( bakeWorkerPool.IsEnabled() ) {
		for ( int i = 0; i < capturedJobs.Num(); i++ ) {
			bakeWorkerPool.Submit( capturedJobs[ i ] );
		}

		int completedJobs = 0;
		while ( completedJobs < capturedJobs.Num() ) {
			lightGridBakeJob_t *completedJob = NULL;
			if ( !bakeWorkerPool.WaitPopCompleted( completedJob ) || completedJob == NULL ) {
				continue;
			}

			LightGrid_CopyProbeTileToPixels( targetPixels, targetWidth, pixelBaseAtlasY, *completedJob );
			LightGrid_CopyVisibilityTileToPixels( targetVisibilityPixels, targetWidth, pixelBaseAtlasY, *completedJob );
			processedProbes++;
			runStats.processedProbeCount++;
			progress.globalProcessedProbes++;
			LightGrid_PrintBakeProbeProgress( progress, areaOrdinal, processedProbes, areaProbeCount );
			delete completedJob;
			completedJobs++;
		}
	} else {
		for ( int i = 0; i < capturedJobs.Num(); i++ ) {
			lightGridBakeJob_t *job = capturedJobs[ i ];
			byte *cubeFaces[6];
			for ( int side = 0; side < 6; side++ ) {
				cubeFaces[ side ] = job->capturedFaces + side * job->faceBytes;
			}

			LightGrid_BakeProbeTile(
				job->probeTile,
				job->probeSize,
				job->borderSize,
				cubeFaces,
				job->captureSize,
				job->sampleCount );
			LightGrid_CopyProbeTileToPixels( targetPixels, targetWidth, pixelBaseAtlasY, *job );
			LightGrid_CopyVisibilityTileToPixels( targetVisibilityPixels, targetWidth, pixelBaseAtlasY, *job );
			processedProbes++;
			runStats.processedProbeCount++;
			progress.globalProcessedProbes++;
			LightGrid_PrintBakeProbeProgress( progress, areaOrdinal, processedProbes, areaProbeCount );
			delete job;
		}
	}

	capturedJobs.Clear();
	runStats.integrationMsec += Sys_Milliseconds() - integrationStart;
}

static void LightGrid_RemoveStagedOutputFile( const idStr &relativePath ) {
	idStr osPath = fileSystem->RelativePathToOSPath( relativePath.c_str(), "fs_savepath" );
	remove( osPath.c_str() );
}

static bool LightGrid_CommitStagedOutputFile( const lightGridStagedWrite_t &stagedWrite ) {
	idStr tempOSPath = fileSystem->RelativePathToOSPath( stagedWrite.tempName.c_str(), "fs_savepath" );
	idStr finalOSPath = fileSystem->RelativePathToOSPath( stagedWrite.finalName.c_str(), "fs_savepath" );
	idStr backupOSPath = finalOSPath;
	backupOSPath += ".prev";

	remove( backupOSPath.c_str() );
	const bool movedExistingFinal = ( rename( finalOSPath.c_str(), backupOSPath.c_str() ) == 0 );
	if ( rename( tempOSPath.c_str(), finalOSPath.c_str() ) == 0 ) {
		if ( movedExistingFinal ) {
			remove( backupOSPath.c_str() );
		}
		return true;
	}

	if ( movedExistingFinal ) {
		rename( backupOSPath.c_str(), finalOSPath.c_str() );
	}

	common->Warning( "bakeLightGrids: failed to commit staged output %s -> %s", stagedWrite.tempName.c_str(), stagedWrite.finalName.c_str() );
	return false;
}

static bool LightGrid_WritePackPointData( idFile *file, const LightGrid &lightGrid ) {
	if ( !LightGrid_WritePackInt( file, lightGrid.area ) ||
		 !LightGrid_WritePackInt( file, lightGrid.lightGridPoints.Num() ) ||
		 !LightGrid_WritePackInt( file, lightGrid.imageSingleProbeSize ) ||
		 !LightGrid_WritePackInt( file, lightGrid.imageBorderSize ) ||
		 !LightGrid_WritePackVec3( file, lightGrid.lightGridOrigin ) ||
		 !LightGrid_WritePackVec3( file, lightGrid.lightGridSize ) ||
		 !LightGrid_WritePackInt( file, lightGrid.lightGridBounds[0] ) ||
		 !LightGrid_WritePackInt( file, lightGrid.lightGridBounds[1] ) ||
		 !LightGrid_WritePackInt( file, lightGrid.lightGridBounds[2] ) ||
		 !LightGrid_WritePackFloat( file, lightGrid.visibilityMaxDistance ) ||
		 !LightGrid_WritePackFloat( file, lightGrid.relocationMaxDistance ) ) {
		return false;
	}

	for ( int i = 0; i < lightGrid.lightGridPoints.Num(); i++ ) {
		const lightGridPoint_t &point = lightGrid.lightGridPoints[i];
		if ( !LightGrid_WritePackByte( file, point.valid ) ||
			 !LightGrid_WritePackVec3( file, point.origin ) ||
			 !LightGrid_WritePackFloat( file, point.visibilityMeanDistance ) ||
			 !LightGrid_WritePackFloat( file, point.visibilityMeanDistanceSq ) ) {
			return false;
		}
	}

	return true;
}

static textureFormat_t LightGrid_PackChunkFormat( int kind ) {
	switch ( kind ) {
		case LIGHTGRID_PACK_CHUNK_IRRADIANCE:
			return glConfig.textureCompressionAvailable ? FMT_DXT1 : FMT_RGB565;
		case LIGHTGRID_PACK_CHUNK_VISIBILITY:
		case LIGHTGRID_PACK_CHUNK_PROBE:
			return FMT_RGBA8;
		default:
			return FMT_RGBA8;
	}
}

static void LightGrid_AppendPackChunk( idList<lightGridPackChunkDirectory_t> &chunks, int areaIndex, int kind, int width, int height ) {
	lightGridPackChunkDirectory_t chunk;
	chunk.areaIndex = areaIndex;
	chunk.kind = kind;
	chunk.format = LightGrid_PackChunkFormat( kind );
	chunk.width = width;
	chunk.height = height;
	chunk.dataOffset = 0;
	chunk.dataBytes = 0;
	chunks.Append( chunk );
}

static const char *LightGrid_PackChunkSourceSuffix( int kind ) {
	switch ( kind ) {
		case LIGHTGRID_PACK_CHUNK_IRRADIANCE:
			return "amb";
		case LIGHTGRID_PACK_CHUNK_VISIBILITY:
			return "vis";
		case LIGHTGRID_PACK_CHUNK_PROBE:
			return "pos";
		default:
			return "";
	}
}

static bool LightGrid_WritePackImagePayload( idFile *file, const char *baseName, lightGridPackChunkDirectory_t &chunk ) {
	const char *suffix = LightGrid_PackChunkSourceSuffix( chunk.kind );
	if ( suffix[0] == '\0' ) {
		return false;
	}

	idStr sourceName = va( "env/%s/area%i_lightgrid_%s.tga", baseName, chunk.areaIndex, suffix );
	byte *pic = NULL;
    struct ReleasePixels { byte*& pic; ~ReleasePixels() { if (pic) R_StaticFree(pic); } } releasePixels{pic};
	int width = 0;
	int height = 0;
	ID_TIME_T timestamp = FILE_NOT_FOUND_TIMESTAMP;
	R_LoadImage( sourceName.c_str(), &pic, &width, &height, &timestamp, false );
	if ( pic == NULL ) {
		common->Warning( "LightGrid pack: failed to read %s", sourceName.c_str() );
		return false;
	}

	if ( width != chunk.width || height != chunk.height ) {
		common->Warning(
			"LightGrid pack: %s dimensions changed while packing (%ix%i, expected %ix%i)",
			sourceName.c_str(),
			width,
			height,
			chunk.width,
			chunk.height );
		return false;
	}

	textureFormat_t textureFormat = static_cast<textureFormat_t>( chunk.format );
	textureColor_t colorFormat = CFM_DEFAULT;
	bool gammaMips = false;
	if ( chunk.kind == LIGHTGRID_PACK_CHUNK_IRRADIANCE ) {
		gammaMips = false;
	} else {
		textureFormat = FMT_RGBA8;
	}

	idBinaryImage packedImage( sourceName.c_str() );
	if (!packedImage.Load2DFromMemory( width, height, pic, 1, textureFormat, colorFormat, gammaMips )) return false;
	chunk.dataOffset = file->Tell();
	const bool wrote = packedImage.WriteToFile( file, timestamp );
	chunk.dataBytes = file->Tell() - chunk.dataOffset;
	chunk.format = packedImage.GetFileHeader().format;
	if ( !wrote ) {
		common->Warning( "LightGrid pack: failed to write payload for %s", sourceName.c_str() );
		return false;
	}

	return true;
}

static bool LightGrid_WritePackFileInternal( idFile *file, const idRenderWorldLocal &world, const lightGridBakeFileStats_t &stats, idList<lightGridPackChunkDirectory_t> &chunks ) {
	if ( !LightGrid_WritePackInt( file, LIGHTGRID_PACK_MAGIC ) ||
		 !LightGrid_WritePackInt( file, LIGHTGRID_PACK_VERSION ) ||
		 !LightGrid_WritePackInt( file, LIGHTGRID_CURRENT_VERSION ) ||
		 !LightGrid_WritePackInt( file, LIGHTGRID_BAKE_HEADER_VERSION ) ||
		 !LightGrid_WritePackUnsignedInt( file, static_cast<unsigned int>( stats.settingsHash ) ) ||
		 !LightGrid_WritePackInt( file, world.numPortalAreas ) ||
		 !LightGrid_WritePackInt( file, chunks.Num() ) ) {
		return false;
	}

	for ( int i = 0; i < world.numPortalAreas; i++ ) {
		if ( !LightGrid_WritePackPointData( file, world.portalAreas[i].lightGrid ) ) {
			return false;
		}
	}

	const int chunkDirectoryOffset = file->Tell();
	for ( int i = 0; i < chunks.Num(); i++ ) {
		if ( !LightGrid_WritePackChunkDirectoryEntry( file, chunks[i] ) ) {
			return false;
		}
	}

	idStr baseName = world.mapName;
	baseName.StripFileExtension();
	for ( int i = 0; i < chunks.Num(); i++ ) {
		if ( !LightGrid_WritePackImagePayload( file, baseName.c_str(), chunks[i] ) ) {
			return false;
		}
	}

	const int endOffset = file->Tell();
	file->Seek( chunkDirectoryOffset, FS_SEEK_SET );
	for ( int i = 0; i < chunks.Num(); i++ ) {
		if ( !LightGrid_WritePackChunkDirectoryEntry( file, chunks[i] ) ) {
			return false;
		}
	}
	file->Seek( endOffset, FS_SEEK_SET );

	return true;
}

static bool LightGrid_WriteLightGridPackFile( const idRenderWorldLocal &world, const char *name, const lightGridBakeOptions_t &options, const lightGridBakeFileStats_t &stats ) {
	(void)options;

	idStr fileName = name;
	fileName.SetFileExtension( "lightgridpack" );

	lightGridStagedWrite_t stagedWrite;
	stagedWrite.finalName = fileName;
	stagedWrite.tempName = fileName;
	stagedWrite.tempName += ".baking";

	idList<lightGridPackChunkDirectory_t> chunks;
	for ( int areaIndex = 0; areaIndex < world.numPortalAreas; areaIndex++ ) {
		const LightGrid &lightGrid = world.portalAreas[areaIndex].lightGrid;
		if ( lightGrid.GridPointCount() <= 0 || lightGrid.CountValidGridPoints() <= 0 ) {
			continue;
		}

		LightGrid_AppendPackChunk( chunks, areaIndex, LIGHTGRID_PACK_CHUNK_IRRADIANCE, LightGrid_GetAtlasWidth( lightGrid ), LightGrid_GetAtlasHeight( lightGrid ) );
		LightGrid_AppendPackChunk( chunks, areaIndex, LIGHTGRID_PACK_CHUNK_VISIBILITY, LightGrid_GetAtlasWidth( lightGrid ), LightGrid_GetAtlasHeight( lightGrid ) );
		LightGrid_AppendPackChunk( chunks, areaIndex, LIGHTGRID_PACK_CHUNK_PROBE, LightGrid_GetProbePositionAtlasWidth( lightGrid ), LightGrid_GetProbePositionAtlasHeight( lightGrid ) );
	}

	const int packStart = Sys_Milliseconds();
	idFile *file = fileSystem->OpenFileWrite( stagedWrite.tempName.c_str(), "fs_savepath" );
	if ( file == NULL ) {
		common->Warning( "LightGrid pack: failed to open %s", stagedWrite.tempName.c_str() );
		return false;
	}

	const bool wrotePack = LightGrid_WritePackFileInternal( file, world, stats, chunks );
	fileSystem->CloseFile( file );

	if ( !wrotePack ) {
		LightGrid_RemoveStagedOutputFile( stagedWrite.tempName );
		return false;
	}

	if ( !LightGrid_CommitStagedOutputFile( stagedWrite ) ) {
		LightGrid_RemoveStagedOutputFile( stagedWrite.tempName );
		return false;
	}

	int payloadBytes = 0;
	for ( int i = 0; i < chunks.Num(); i++ ) {
		payloadBytes += chunks[i].dataBytes;
	}
	common->Printf(
		"Wrote %s (%i chunks, %.2f MB payload, %.2fs)\n",
		stagedWrite.finalName.c_str(),
		chunks.Num(),
		payloadBytes / ( 1024.0f * 1024.0f ),
		( Sys_Milliseconds() - packStart ) * 0.001f );
	return true;
}

static void LightGrid_WriteBakeStatsBlock( idFile *file, const lightGridBakeOptions_t &options, const lightGridBakeFileStats_t &stats, const idRenderWorldLocal *world ) {
	char settingsHashString[16];
	idStr::snPrintf( settingsHashString, sizeof( settingsHashString ), "%08x", static_cast<unsigned int>( stats.settingsHash ) );

	file->WriteFloatString( "lightGridBakeStats {\n" );
	file->WriteFloatString( "\theaderVersion %i\n", LIGHTGRID_BAKE_HEADER_VERSION );
	file->WriteFloatString( "\tsettingsHash 0x%s\n", settingsHashString );
	file->WriteFloatString( "\tmap \"%s\"\n", ( world != NULL ) ? world->mapName.c_str() : "" );
	file->WriteFloatString(
		"\toptions maxProbes %i bounces %i captureSize %i blends %i samples %i gridSize ( %f %f %f )\n",
		options.maxProbes,
		options.bounces,
		options.captureSize,
		options.blends,
		options.samples,
		options.gridSize.x,
		options.gridSize.y,
		options.gridSize.z );
	file->WriteFloatString(
		"\tprobes total %i valid %i invalid %i relocated %i nearSolid %i\n",
		stats.totalProbes,
		stats.validProbes,
		stats.invalidProbes,
		stats.relocatedProbes,
		stats.nearSolidProbes );
	file->WriteFloatString(
		"\tareas total %i baked %i maxAtlas %i %i\n",
		stats.numPortalAreas,
		stats.bakeAreaCount,
		stats.maxAtlasWidth,
		stats.maxAtlasHeight );
	file->WriteFloatString(
		"\tvisibility maxDistance %f traceBias %f traceSamples %i coneOffset %f\n",
		LIGHTGRID_VISIBILITY_MAX_DISTANCE,
		LIGHTGRID_VISIBILITY_TRACE_BIAS,
		LIGHTGRID_VISIBILITY_TRACE_SAMPLES,
		LIGHTGRID_VISIBILITY_CONE_OFFSET );
	file->WriteFloatString(
		"\trelocation clearance %f maxDistance %f searchStep %f\n",
		LIGHTGRID_RELOCATION_CLEARANCE,
		LIGHTGRID_RELOCATION_MAX_DISTANCE,
		LIGHTGRID_RELOCATION_SEARCH_STEP );

	for ( int i = 0; i < stats.areas.Num(); i++ ) {
		const lightGridBakeAreaFileStats_t &areaStats = stats.areas[i];
		file->WriteFloatString(
			"\tarea %i probes %i %i %i relocated %i nearSolid %i atlas %i %i\n",
			areaStats.areaIndex,
			areaStats.totalProbes,
			areaStats.validProbes,
			areaStats.invalidProbes,
			areaStats.relocatedProbes,
			areaStats.nearSolidProbes,
			areaStats.atlasWidth,
			areaStats.atlasHeight );
	}

	file->WriteFloatString( "}\n\n" );
}

static void LightGrid_WriteLightGridBlock( idFile *file, const LightGrid &lightGrid ) {
	file->WriteFloatString( "lightGridPoints {\n" );
	file->WriteFloatString( "\t%i %i %i %i\n", lightGrid.area, lightGrid.lightGridPoints.Num(), lightGrid.imageSingleProbeSize, lightGrid.imageBorderSize );
	file->WriteFloatString( "\t( %f %f %f )\n", lightGrid.lightGridOrigin.x, lightGrid.lightGridOrigin.y, lightGrid.lightGridOrigin.z );
	file->WriteFloatString( "\t( %f %f %f )\n", lightGrid.lightGridSize.x, lightGrid.lightGridSize.y, lightGrid.lightGridSize.z );
	file->WriteFloatString( "\t%i %i %i\n", lightGrid.lightGridBounds[0], lightGrid.lightGridBounds[1], lightGrid.lightGridBounds[2] );

	for ( int i = 0; i < lightGrid.lightGridPoints.Num(); i++ ) {
		const lightGridPoint_t &gridPoint = lightGrid.lightGridPoints[i];
		file->WriteFloatString( "\t%i ( %f %f %f )\n", static_cast<int>( gridPoint.valid ), gridPoint.origin.x, gridPoint.origin.y, gridPoint.origin.z );
	}

	file->WriteFloatString( "}\n\n" );
}

static void LightGrid_WriteLightGridVisibilityBlock( idFile *file, const LightGrid &lightGrid ) {
	file->WriteFloatString( "lightGridVisibility {\n" );
	file->WriteFloatString( "\t%i %i %f\n", lightGrid.area, lightGrid.lightGridPoints.Num(), lightGrid.visibilityMaxDistance );

	for ( int i = 0; i < lightGrid.lightGridPoints.Num(); i++ ) {
		const lightGridPoint_t &gridPoint = lightGrid.lightGridPoints[i];
		file->WriteFloatString( "\t%f %f\n", gridPoint.visibilityMeanDistance, gridPoint.visibilityMeanDistanceSq );
	}

	file->WriteFloatString( "}\n\n" );
}

static bool LightGrid_WriteLightGridFile( const idRenderWorldLocal &world, const char *name, const lightGridBakeOptions_t &options, const lightGridBakeFileStats_t &stats ) {
	idStr fileName = name;
	fileName.SetFileExtension( "lightgrid" );

	idFile *file = fileSystem->OpenFileWrite( fileName.c_str(), "fs_savepath" );
	if ( file == NULL ) {
		common->Warning( "LightGrid_WriteLightGridFile: failed to open %s", fileName.c_str() );
		return false;
	}

	file->WriteFloatString( "%s %i\n\n", LGRID_FILE_ID, LIGHTGRID_CURRENT_VERSION );
	LightGrid_WriteBakeStatsBlock( file, options, stats, &world );
	for ( int i = 0; i < world.numPortalAreas; i++ ) {
		LightGrid_WriteLightGridBlock( file, world.portalAreas[i].lightGrid );
		LightGrid_WriteLightGridVisibilityBlock( file, world.portalAreas[i].lightGrid );
	}

	fileSystem->CloseFile( file );
	common->Printf( "Wrote %s\n", fileName.c_str() );
	return true;
}

static void LightGrid_PrintBakeProbeProgress( lightGridBakeProgress_t &progress, int areaIndex, int areaProbeIndex, int areaProbeCount ) {
	const int now = Sys_Milliseconds();
	if ( areaProbeIndex < areaProbeCount && progress.lastPrintTime != 0 && now - progress.lastPrintTime < 500 ) {
		return;
	}

	progress.lastPrintTime = now;

	const float totalFraction = ( progress.totalValidProbes > 0 )
		? ( static_cast<float>( progress.globalProcessedProbes ) / static_cast<float>( progress.totalValidProbes ) )
		: 1.0f;
	const float areaFraction = ( areaProbeCount > 0 )
		? ( static_cast<float>( areaProbeIndex ) / static_cast<float>( areaProbeCount ) )
		: 1.0f;

	common->Printf(
		"bakeLightGrids: %s bounce %i/%i area %i/%i probe %i/%i (area %.1f%%, total %.1f%%)\n",
		progress.jobName,
		progress.bounceIndex + 1,
		progress.totalBounces,
		areaIndex + 1,
		progress.totalAreas,
		areaProbeIndex,
		areaProbeCount,
		areaFraction * 100.0f,
		totalFraction * 100.0f );
}

static bool LightGrid_ReadBakeStatsHash( idLexer *src, int &headerVersion, uint32_t &settingsHash ) {
	headerVersion = 0;
	settingsHash = 0;
	bool foundHeaderVersion = false;
	bool foundSettingsHash = false;

	if ( !src->ExpectTokenString( "{" ) ) {
		return false;
	}

	idToken token;
	while ( src->ReadToken( &token ) ) {
		if ( token == "}" ) {
			return foundHeaderVersion && foundSettingsHash && headerVersion == LIGHTGRID_BAKE_HEADER_VERSION;
		}

		if ( token == "headerVersion" ) {
			headerVersion = src->ParseInt();
			foundHeaderVersion = true;
			continue;
		}

		if ( token == "settingsHash" ) {
			if ( !src->ReadToken( &token ) ) {
				return false;
			}
			char *end = NULL;
			const unsigned long long parsedHash = strtoull( token.c_str(), &end, 16 );
			if ( end == token.c_str() || *end != '\0' || parsedHash > 0xFFFFFFFFULL ) {
				return false;
			}
			settingsHash = static_cast<uint32_t>( parsedHash );
			foundSettingsHash = true;
			continue;
		}
	}

	return false;
}

static bool LightGrid_ReadFileBakeStatsHash( const char *name, int &fileVersion, int &headerVersion, uint32_t &settingsHash ) {
	fileVersion = 0;
	headerVersion = 0;
	settingsHash = 0;

	idLexer *src = new idLexer( name, LEXFL_NOSTRINGCONCAT | LEXFL_NODOLLARPRECOMPILE );
	if ( !src->IsLoaded() ) {
		delete src;
		return false;
	}

	idToken token;
	if ( !src->ReadToken( &token ) || token.Icmp( LGRID_FILE_ID ) ) {
		delete src;
		return false;
	}

	if ( !src->ReadToken( &token ) ) {
		delete src;
		return false;
	}

	fileVersion = atoi( token.c_str() );
	if ( fileVersion != LIGHTGRID_CURRENT_VERSION ) {
		delete src;
		return false;
	}

	while ( src->ReadToken( &token ) ) {
		if ( token == "lightGridBakeStats" ) {
			const bool readStats = LightGrid_ReadBakeStatsHash( src, headerVersion, settingsHash );
			delete src;
			return readStats;
		}

		if ( token == "lightGridPoints" ) {
			src->SkipBracedSection();
			continue;
		}

		if ( src->PeekTokenString( "{" ) ) {
			src->SkipBracedSection();
		}
	}

	delete src;
	return false;
}

bool R_LightGridFileMatchesBakeOptions( const char *name, const lightGridBakeOptions_t &options, const idRenderWorldLocal *world ) {
	int fileVersion = 0;
	int headerVersion = 0;
	uint32_t fileSettingsHash = 0;
	if ( !LightGrid_ReadFileBakeStatsHash( name, fileVersion, headerVersion, fileSettingsHash ) ) {
		if ( fileVersion > 0 && fileVersion != LIGHTGRID_CURRENT_VERSION ) {
			common->Printf( "bakeLightGrids: %s uses light-grid cache version %i; rebuilding for version %i\n", name, fileVersion, LIGHTGRID_CURRENT_VERSION );
		} else {
			common->Printf( "bakeLightGrids: %s has no v%i bake settings header; rebuilding\n", name, LIGHTGRID_CURRENT_VERSION );
		}
		return false;
	}

	const uint32_t expectedSettingsHash = LightGrid_CalculateBakeSettingsHash( options, world );
	if ( fileSettingsHash != expectedSettingsHash ) {
		common->Printf(
			"bakeLightGrids: %s settings hash changed (file 0x%08x, current 0x%08x); rebuilding\n",
			name,
			static_cast<unsigned int>( fileSettingsHash ),
			static_cast<unsigned int>( expectedSettingsHash ) );
		return false;
	}

	return true;
}

static bool LightGrid_ReadPackHeader( idFile *file, int &packVersion, int &lightGridVersion, int &bakeHeaderVersion, unsigned int &settingsHash, int &numPortalAreas, int &chunkCount ) {
	int magic = 0;
	if ( !LightGrid_ReadPackInt( file, magic ) ||
		 !LightGrid_ReadPackInt( file, packVersion ) ||
		 !LightGrid_ReadPackInt( file, lightGridVersion ) ||
		 !LightGrid_ReadPackInt( file, bakeHeaderVersion ) ||
		 !LightGrid_ReadPackUnsignedInt( file, settingsHash ) ||
		 !LightGrid_ReadPackInt( file, numPortalAreas ) ||
		 !LightGrid_ReadPackInt( file, chunkCount ) ) {
		return false;
	}

	if ( magic != LIGHTGRID_PACK_MAGIC || packVersion != LIGHTGRID_PACK_VERSION ||
		 lightGridVersion != LIGHTGRID_CURRENT_VERSION || bakeHeaderVersion != LIGHTGRID_BAKE_HEADER_VERSION ||
		 numPortalAreas < 0 || chunkCount < 0 ) {
		return false;
	}
	if ( chunkCount > numPortalAreas * 3 ) {
		return false;
	}

	return true;
}

bool R_LightGridPackFileMatchesBakeOptions( const char *name, const lightGridBakeOptions_t &options, const idRenderWorldLocal *world ) {
	idFile *file = fileSystem->OpenFileRead( name );
	if ( file == NULL ) {
		return false;
	}

	int packVersion = 0;
	int lightGridVersion = 0;
	int bakeHeaderVersion = 0;
	unsigned int fileSettingsHash = 0;
	int numPortalAreas = 0;
	int chunkCount = 0;
	const bool readHeader = LightGrid_ReadPackHeader( file, packVersion, lightGridVersion, bakeHeaderVersion, fileSettingsHash, numPortalAreas, chunkCount );
	fileSystem->CloseFile( file );

	if ( !readHeader ) {
		common->Printf( "bakeLightGrids: %s has no valid light-grid pack header; rebuilding\n", name );
		return false;
	}

	if ( world != NULL && numPortalAreas != world->numPortalAreas ) {
		common->Printf(
			"bakeLightGrids: %s area count changed (file %i, current %i); rebuilding\n",
			name,
			numPortalAreas,
			world->numPortalAreas );
		return false;
	}

	const uint32_t expectedSettingsHash = LightGrid_CalculateBakeSettingsHash( options, world );
	if ( fileSettingsHash != static_cast<unsigned int>( expectedSettingsHash ) ) {
		common->Printf(
			"bakeLightGrids: %s settings hash changed (file 0x%08x, current 0x%08x); rebuilding\n",
			name,
			fileSettingsHash,
			static_cast<unsigned int>( expectedSettingsHash ) );
		return false;
	}

	return true;
}

static bool LightGrid_ReadPackPointData( idFile *file, idRenderWorldLocal *world ) {
	int areaIndex = -1;
	int numLightGridPoints = 0;
	int imageProbeSize = 0;
	int imageBorderSize = 0;

	if ( !LightGrid_ReadPackInt( file, areaIndex ) ||
		 !LightGrid_ReadPackInt( file, numLightGridPoints ) ||
		 !LightGrid_ReadPackInt( file, imageProbeSize ) ||
		 !LightGrid_ReadPackInt( file, imageBorderSize ) ) {
		return false;
	}
	if ( areaIndex < 0 || world == NULL || areaIndex >= world->numPortalAreas || numLightGridPoints < 0 || numLightGridPoints > LIGHTGRID_DEFAULT_MAX_AREA_POINTS ||
		 imageProbeSize <= 0 || imageBorderSize < 0 ) {
		return false;
	}

	LightGrid &lightGrid = world->portalAreas[areaIndex].lightGrid;
	lightGrid.Clear();
	lightGrid.area = areaIndex;
	lightGrid.imageSingleProbeSize = imageProbeSize;
	lightGrid.imageBorderSize = imageBorderSize;
	lightGrid.totalGridPointCount = numLightGridPoints;

	if ( !LightGrid_ReadPackVec3( file, lightGrid.lightGridOrigin ) ||
		 !LightGrid_ReadPackVec3( file, lightGrid.lightGridSize ) ||
		 !LightGrid_ReadPackInt( file, lightGrid.lightGridBounds[0] ) ||
		 !LightGrid_ReadPackInt( file, lightGrid.lightGridBounds[1] ) ||
		 !LightGrid_ReadPackInt( file, lightGrid.lightGridBounds[2] ) ||
		 !LightGrid_ReadPackFloat( file, lightGrid.visibilityMaxDistance ) ||
		 !LightGrid_ReadPackFloat( file, lightGrid.relocationMaxDistance ) ) {
		return false;
	}

	if ( lightGrid.lightGridBounds[0] <= 0 || lightGrid.lightGridBounds[1] <= 0 || lightGrid.lightGridBounds[2] <= 0 ) {
		return false;
	}

	lightGrid.lightGridPoints.SetNum( numLightGridPoints );
	for ( int i = 0; i < numLightGridPoints; i++ ) {
		lightGridPoint_t &point = lightGrid.lightGridPoints[i];
		if ( !LightGrid_ReadPackByte( file, point.valid ) ||
			 !LightGrid_ReadPackVec3( file, point.origin ) ||
			 !LightGrid_ReadPackFloat( file, point.visibilityMeanDistance ) ||
			 !LightGrid_ReadPackFloat( file, point.visibilityMeanDistanceSq ) ) {
			return false;
		}
		if ( point.valid != 0 ) {
			lightGrid.validGridPointCount++;
			if ( point.valid == LIGHTGRID_POINT_RELOCATED || point.valid == LIGHTGRID_POINT_RELOCATED_NEAR_SOLID ) {
				lightGrid.relocatedGridPointCount++;
			}
			if ( point.valid == LIGHTGRID_POINT_NEAR_SOLID || point.valid == LIGHTGRID_POINT_RELOCATED_NEAR_SOLID ) {
				lightGrid.nearSolidGridPointCount++;
			}
		}
	}

	return true;
}

static textureUsage_t LightGrid_PackChunkUsage( int kind ) {
	switch ( kind ) {
		case LIGHTGRID_PACK_CHUNK_IRRADIANCE:
			return TD_LIGHTGRID;
		case LIGHTGRID_PACK_CHUNK_VISIBILITY:
			return TD_LIGHTGRID_VISIBILITY;
		case LIGHTGRID_PACK_CHUNK_PROBE:
			return TD_LIGHTGRID_PROBE;
		default:
			return TD_DEFAULT;
	}
}

static bool LightGrid_AssignPackChunk( idRenderWorldLocal *world, const char *packFileName, const lightGridPackChunkDirectory_t &chunk ) {
	if ( world == NULL || packFileName == NULL || chunk.areaIndex < 0 || chunk.areaIndex >= world->numPortalAreas ||
		 chunk.width <= 0 || chunk.height <= 0 || chunk.dataOffset <= 0 || chunk.dataBytes <= 0 ) {
		return false;
	}

	LightGrid &lightGrid = world->portalAreas[chunk.areaIndex].lightGrid;
	lightGridPackedImageRef_t *ref = NULL;
	idImage **image = NULL;
	switch ( chunk.kind ) {
		case LIGHTGRID_PACK_CHUNK_IRRADIANCE:
			ref = &lightGrid.packedIrradianceImage;
			image = &lightGrid.irradianceImage;
			break;
		case LIGHTGRID_PACK_CHUNK_VISIBILITY:
			ref = &lightGrid.packedVisibilityImage;
			image = &lightGrid.visibilityImage;
			break;
		case LIGHTGRID_PACK_CHUNK_PROBE:
			ref = &lightGrid.packedProbeImage;
			image = &lightGrid.probeImage;
			break;
		default:
			return false;
	}

	ref->packFileName = packFileName;
	ref->dataOffset = chunk.dataOffset;
	ref->dataBytes = chunk.dataBytes;
	ref->width = chunk.width;
	ref->height = chunk.height;

	idStr baseName = world->mapName;
	baseName.StripFileExtension();
	const char *suffix = LightGrid_PackChunkSourceSuffix( chunk.kind );
	idStr imageName = va( "_lightgridpack/%s/area%i_lightgrid_%s", baseName.c_str(), chunk.areaIndex, suffix );
	*image = globalImages->ImageHandleDeferred( imageName.c_str(), TF_LINEAR, TR_CLAMP, LightGrid_PackChunkUsage( chunk.kind ), CF_2D );
	return *image != NULL;
}

bool idRenderWorldLocal::LoadLightGridPackFile( const char *name ) {
	const int start = Sys_Milliseconds();
	idFile *file = fileSystem->OpenFileRead( name );
	if ( file == NULL ) {
		return false;
	}

	int packVersion = 0;
	int lightGridVersion = 0;
	int bakeHeaderVersion = 0;
	unsigned int settingsHash = 0;
	int packPortalAreas = 0;
	int chunkCount = 0;
	if ( !LightGrid_ReadPackHeader( file, packVersion, lightGridVersion, bakeHeaderVersion, settingsHash, packPortalAreas, chunkCount ) ) {
		fileSystem->CloseFile( file );
		common->Warning( "%s is not a valid light-grid pack", name );
		return false;
	}
	if ( packPortalAreas != numPortalAreas ) {
		fileSystem->CloseFile( file );
		common->Warning( "%s has %i light-grid areas, but map has %i", name, packPortalAreas, numPortalAreas );
		return false;
	}

	for ( int i = 0; i < packPortalAreas; i++ ) {
		if ( !LightGrid_ReadPackPointData( file, this ) ) {
			fileSystem->CloseFile( file );
			common->Warning( "%s has invalid light-grid metadata", name );
			return false;
		}
	}

	idList<lightGridPackChunkDirectory_t> chunks;
	chunks.SetNum( chunkCount );
	for ( int i = 0; i < chunkCount; i++ ) {
		if ( !LightGrid_ReadPackChunkDirectoryEntry( file, chunks[i] ) ) {
			fileSystem->CloseFile( file );
			common->Warning( "%s has an invalid light-grid chunk directory", name );
			return false;
		}
	}
	fileSystem->CloseFile( file );

	int assignedChunks = 0;
	for ( int i = 0; i < chunks.Num(); i++ ) {
		if ( ( chunks[i].format == FMT_DXT1 || chunks[i].format == FMT_DXT5 ) && !glConfig.textureCompressionAvailable ) {
			common->Warning( "%s contains compressed light-grid chunks but this renderer does not support texture compression", name );
			return false;
		}
		if ( chunks[i].format == FMT_BC7 && !glConfig.bptcTextureCompressionAvailable ) {
			common->Warning( "%s contains BC7 light-grid chunks but this renderer does not support BPTC texture compression", name );
			return false;
		}
		if ( LightGrid_AssignPackChunk( this, name, chunks[i] ) ) {
			assignedChunks++;
		}
	}

	lightGridAvailabilityFrame = -1;
	common->DPrintf(
		"Loaded light-grid pack %s: %i areas, %i/%i chunks, settings 0x%08x in %.3fs\n",
		name,
		packPortalAreas,
		assignedChunks,
		chunkCount,
		settingsHash,
		( Sys_Milliseconds() - start ) * 0.001f );
	return assignedChunks > 0;
}

static lightGridPackedImageLoadResult_t LightGrid_LoadPackedImageRef( const lightGridPackedImageRef_t &ref, idImage *image, textureUsage_t usage ) {
	(void)usage;
	if ( !LightGrid_PackedImageRefIsValid( ref ) || image == NULL ) {
		return LIGHTGRID_PACKED_IMAGE_LOAD_FAILED;
	}
	if ( image->IsLoaded() && !image->IsDefaulted() ) {
		return LIGHTGRID_PACKED_IMAGE_LOAD_ALREADY_RESIDENT;
	}

	idFile *file = fileSystem->OpenFileRead( ref.packFileName.c_str() );
	if ( file == NULL ) {
		return LIGHTGRID_PACKED_IMAGE_LOAD_FAILED;
	}
	if ( file->Seek( ref.dataOffset, FS_SEEK_SET ) != 0 ) {
		fileSystem->CloseFile( file );
		return LIGHTGRID_PACKED_IMAGE_LOAD_FAILED;
	}

	idBinaryImage packedImage( image->GetName() );
	const int chunkStart = file->Tell();
	const bool readPayload = packedImage.LoadFromFile( file, ref.dataBytes );
	const int chunkEnd = file->Tell();
	fileSystem->CloseFile( file );
	if ( !readPayload || chunkEnd - chunkStart != ref.dataBytes ) {
		common->Warning( "LightGrid pack: failed to read %s from %s", image->GetName(), ref.packFileName.c_str() );
		return LIGHTGRID_PACKED_IMAGE_LOAD_FAILED;
	}

	const bimageFile_t &header = packedImage.GetFileHeader();
	if ( header.width != ref.width || header.height != ref.height || header.textureType != TT_2D || header.numLevels <= 0 ) {
		common->Warning( "LightGrid pack: invalid image header for %s", image->GetName() );
		return LIGHTGRID_PACKED_IMAGE_LOAD_FAILED;
	}
	if ( ( header.format == FMT_DXT1 || header.format == FMT_DXT5 ) && !glConfig.textureCompressionAvailable ) {
		common->Warning( "LightGrid pack: %s requires compressed texture support", image->GetName() );
		return LIGHTGRID_PACKED_IMAGE_LOAD_FAILED;
	}
	if ( header.format == FMT_BC7 && !glConfig.bptcTextureCompressionAvailable ) {
		common->Warning( "LightGrid pack: %s requires BC7/BPTC texture support", image->GetName() );
		return LIGHTGRID_PACKED_IMAGE_LOAD_FAILED;
	}

	idImageOpts opts;
	opts.textureType = static_cast<textureType_t>( header.textureType );
	opts.format = static_cast<textureFormat_t>( header.format );
	opts.colorFormat = static_cast<textureColor_t>( header.colorFormat );
	opts.width = header.width;
	opts.height = header.height;
	opts.numLevels = header.numLevels;
	opts.gammaMips = false;
	image->AllocImage( opts, TF_LINEAR, TR_CLAMP );
	const int imageCount = packedImage.NumImages();
	for ( int i = 0; i < imageCount; i++ ) {
		const bimageImage_t &img = packedImage.GetImageHeader( i );
		image->SubImageUpload( img.level, 0, 0, img.destZ, img.width, img.height, packedImage.GetImageData( i ) );
	}
	return ( image->IsLoaded() && !image->IsDefaulted() ) ? LIGHTGRID_PACKED_IMAGE_LOAD_MATERIALIZED : LIGHTGRID_PACKED_IMAGE_LOAD_FAILED;
}

bool idRenderWorldLocal::EnsureLightGridAreaImages( int areaIndex ) {
	if ( areaIndex < 0 || areaIndex >= numPortalAreas ) {
		return false;
	}

	if ( !tr.IsOpenGLRunning() ) {
		return false;
	}

	LightGrid &lightGrid = portalAreas[areaIndex].lightGrid;
	const int start = Sys_Milliseconds();
	bool materializedPackedImage = false;

	if ( LightGrid_PackedImageRefIsValid( lightGrid.packedIrradianceImage ) ) {
		const lightGridPackedImageLoadResult_t irradianceLoad =
			LightGrid_LoadPackedImageRef( lightGrid.packedIrradianceImage, lightGrid.irradianceImage, TD_LIGHTGRID );
		const lightGridPackedImageLoadResult_t visibilityLoad =
			LightGrid_LoadPackedImageRef( lightGrid.packedVisibilityImage, lightGrid.visibilityImage, TD_LIGHTGRID_VISIBILITY );
		const lightGridPackedImageLoadResult_t probeLoad =
			LightGrid_LoadPackedImageRef( lightGrid.packedProbeImage, lightGrid.probeImage, TD_LIGHTGRID_PROBE );
		materializedPackedImage = irradianceLoad == LIGHTGRID_PACKED_IMAGE_LOAD_MATERIALIZED ||
			visibilityLoad == LIGHTGRID_PACKED_IMAGE_LOAD_MATERIALIZED ||
			probeLoad == LIGHTGRID_PACKED_IMAGE_LOAD_MATERIALIZED;
	} else {
		if ( lightGrid.irradianceImage != NULL && !lightGrid.irradianceImage->IsLoaded() ) {
			lightGrid.irradianceImage->ActuallyLoadImage( true );
		}
		if ( lightGrid.visibilityImage != NULL && !lightGrid.visibilityImage->IsLoaded() ) {
			lightGrid.visibilityImage->ActuallyLoadImage( true );
		}
		if ( lightGrid.probeImage != NULL && !lightGrid.probeImage->IsLoaded() ) {
			lightGrid.probeImage->ActuallyLoadImage( true );
		}
	}

	if ( materializedPackedImage && r_lightGridReport.GetInteger() > 0 ) {
		common->Printf(
			"LightGrid pack: materialized area %i images in %.3fs\n",
			areaIndex,
			( Sys_Milliseconds() - start ) * 0.001f );
	}

	return lightGrid.irradianceImage != NULL && lightGrid.irradianceImage->IsLoaded() && !lightGrid.irradianceImage->IsDefaulted();
}

static int LightGrid_GetBakeWorkerCount() {
	const int requestedWorkers = r_lightGridBakeWorkers.GetInteger();
	if ( requestedWorkers < 0 ) {
		return 0;
	}

	if ( requestedWorkers > 0 ) {
		return idMath::ClampInt( 1, LIGHTGRID_BAKE_MAX_WORKERS, requestedWorkers );
	}

	// platform CPU count, not SDL: renderer sources must stay free of
	// windowing-library references so the module build never links SDL
#if defined( _WIN32 )
	SYSTEM_INFO systemInfo;
	GetNativeSystemInfo( &systemInfo );
	const int logicalCpuCount = ( int )systemInfo.dwNumberOfProcessors;
#else
	const long onlineCpus = sysconf( _SC_NPROCESSORS_ONLN );
	const int logicalCpuCount = onlineCpus > 0 ? ( int )onlineCpus : 1;
#endif
	if ( logicalCpuCount > 1 ) {
		return idMath::ClampInt( 1, LIGHTGRID_BAKE_MAX_WORKERS, logicalCpuCount - 1 );
	}

	return 0;
}

static bool LightGrid_UseAsyncReadback( int captureSize, int blends ) {
	if ( !r_lightGridBakeAsyncReadback.GetBool() ) {
		return false;
	}

	if ( !glConfig.pixelBufferObjectAvailable ) {
		return false;
	}

	if ( blends > 1 ) {
		return false;
	}

	if ( captureSize <= 0 || captureSize > glConfig.vidWidth || captureSize > glConfig.vidHeight ) {
		return false;
	}

	return true;
}

static int LightGrid_GetAsyncReadbackSlotCount( int workerCount, int faceBytes ) {
	const int requestedSlots = r_lightGridBakeReadbackSlots.GetInteger();
	if ( requestedSlots > 0 ) {
		return idMath::ClampInt( 1, LIGHTGRID_BAKE_MAX_ASYNC_READBACK_SLOTS, requestedSlots );
	}

	const int budgetBytes = LightGrid_GetBakeTransientMemoryBudgetBytes();
	const int desiredSlots = Max( workerCount, 3 );
	const int maxByBudget = Max( budgetBytes / Max( faceBytes * 2, 1 ), 1 );
	return idMath::ClampInt( 1, 8, Min( desiredSlots, maxByBudget ) );
}

void R_SetDefaultLightGridBakeOptions( lightGridBakeOptions_t &options ) {
	options.maxProbes = LIGHTGRID_DEFAULT_MAX_AREA_POINTS;
	options.bounces = 1;
	options.captureSize = LIGHTGRID_DEFAULT_CAPTURE_SIZE;
	options.blends = LIGHTGRID_DEFAULT_BLENDS;
	options.samples = LIGHTGRID_DEFAULT_SAMPLES;
	options.separateAreas = false;
	options.gridSize = LIGHTGRID_DEFAULT_SIZE;
}

static void LightGrid_ClearPackedImageRef( lightGridPackedImageRef_t &ref ) {
	ref.packFileName.Clear();
	ref.dataOffset = 0;
	ref.dataBytes = 0;
	ref.width = 0;
	ref.height = 0;
}

static bool LightGrid_PackedImageRefIsValid( const lightGridPackedImageRef_t &ref ) {
	return ref.packFileName.Length() > 0 && ref.dataOffset > 0 && ref.dataBytes > 0 && ref.width > 0 && ref.height > 0;
}

void LightGrid::Clear() {
	lightGridOrigin.Zero();
	lightGridSize = LIGHTGRID_DEFAULT_SIZE;
	lightGridBounds[0] = 0;
	lightGridBounds[1] = 0;
	lightGridBounds[2] = 0;
	lightGridPoints.Clear();
	totalGridPointCount = 0;
	validGridPointCount = 0;
	relocatedGridPointCount = 0;
	nearSolidGridPointCount = 0;
	area = -1;
	irradianceImage = NULL;
	visibilityImage = NULL;
	probeImage = NULL;
	LightGrid_ClearPackedImageRef( packedIrradianceImage );
	LightGrid_ClearPackedImageRef( packedVisibilityImage );
	LightGrid_ClearPackedImageRef( packedProbeImage );
	imageSingleProbeSize = LIGHTGRID_DEFAULT_SINGLE_PROBE_SIZE;
	imageBorderSize = LIGHTGRID_DEFAULT_BORDER_SIZE;
	visibilityMaxDistance = LIGHTGRID_VISIBILITY_MAX_DISTANCE;
	relocationMaxDistance = LIGHTGRID_RELOCATION_MAX_DISTANCE;
}

bool LightGrid::HasImage() const {
	return ( irradianceImage != NULL && !irradianceImage->IsDefaulted() ) ||
		LightGrid_PackedImageRefIsValid( packedIrradianceImage );
}

bool LightGrid::IsUsable() const {
	if ( totalGridPointCount <= 0 || !HasImage() ) {
		return false;
	}
	if ( lightGridBounds[0] <= 0 || lightGridBounds[1] <= 0 || lightGridBounds[2] <= 0 ) {
		return false;
	}
	return true;
}

int LightGrid::GridPointCount() const {
	return totalGridPointCount;
}

int LightGrid::CountValidGridPoints() const {
	return validGridPointCount;
}

int LightGrid::CountRelocatedGridPoints() const {
	return relocatedGridPointCount;
}

int LightGrid::CountNearSolidGridPoints() const {
	return nearSolidGridPointCount;
}

bool LightGrid::HasPointData() const {
	return lightGridPoints.Num() > 0;
}

void LightGrid::DiscardPointData() {
	lightGridPoints.Clear();
}

void LightGrid::SetupGrid( const idBounds &bounds, const idRenderWorld *world, const idVec3 &preferredSize, int areaIndex, int totalAreas, int maxProbes, bool printToConsole ) {
	idImage *existingImage = irradianceImage;
	idImage *existingVisibilityImage = visibilityImage;
	idImage *existingProbeImage = probeImage;
	const lightGridPackedImageRef_t existingPackedImage = packedIrradianceImage;
	const lightGridPackedImageRef_t existingPackedVisibilityImage = packedVisibilityImage;
	const lightGridPackedImageRef_t existingPackedProbeImage = packedProbeImage;
	Clear();
	irradianceImage = existingImage;
	visibilityImage = existingVisibilityImage;
	probeImage = existingProbeImage;
	packedIrradianceImage = existingPackedImage;
	packedVisibilityImage = existingPackedVisibilityImage;
	packedProbeImage = existingPackedProbeImage;
	area = areaIndex;
	lightGridSize = preferredSize;

	if ( bounds.IsCleared() ) {
		return;
	}

	const idVec3 boundsCenter = bounds.GetCenter();
	const int maxGridPoints = ( maxProbes > 0 ) ? idMath::ClampInt( 1, LIGHTGRID_DEFAULT_MAX_AREA_POINTS, maxProbes ) : LIGHTGRID_DEFAULT_MAX_AREA_POINTS;

	int growAxis = 0;
	for ( ;; ) {
		int numGridPoints = 1;

		for ( int i = 0; i < 3; i++ ) {
			if ( lightGridSize[i] <= 0.0f ) {
				lightGridSize[i] = LIGHTGRID_DEFAULT_SIZE[i];
			}

			float alignedMin = lightGridSize[i] * idMath::Ceil( bounds[0][i] / lightGridSize[i] );
			float alignedMax = lightGridSize[i] * idMath::Floor( bounds[1][i] / lightGridSize[i] );
			if ( alignedMax < alignedMin ) {
				alignedMin = lightGridSize[i] * idMath::Floor( boundsCenter[i] / lightGridSize[i] );
				alignedMax = alignedMin;
			}

			lightGridOrigin[i] = alignedMin;
			lightGridBounds[i] = idMath::FtoiFast( ( alignedMax - alignedMin ) / lightGridSize[i] ) + 1;
			lightGridBounds[i] = Max( lightGridBounds[i], 1 );
			numGridPoints *= lightGridBounds[i];
		}

		const int atlasWidth = Max( lightGridBounds[0] * lightGridBounds[2], 1 ) * imageSingleProbeSize;
		const int atlasHeight = Max( lightGridBounds[1], 1 ) * imageSingleProbeSize;

		if ( numGridPoints <= maxGridPoints && atlasWidth <= LIGHTGRID_MAX_ATLAS_SIZE && atlasHeight <= LIGHTGRID_MAX_ATLAS_SIZE ) {
			totalGridPointCount = numGridPoints;
			lightGridPoints.SetNum( numGridPoints );
			CalculateGridPointPositions( world, totalAreas, printToConsole );
			break;
		}

		lightGridSize[ growAxis++ % 3 ] += 16.0f;
	}
}

void LightGrid::CalculateGridPointPositions( const idRenderWorld *world, int totalAreas, bool printToConsole ) {
	const int gridStep0 = 1;
	const int gridStep1 = lightGridBounds[0];
	const int gridStep2 = lightGridBounds[0] * lightGridBounds[1];

	int invalidCount = 0;
	int relocatedCount = 0;
	int nearSolidCount = 0;

	for ( int x = 0; x < lightGridBounds[0]; x++ ) {
		for ( int y = 0; y < lightGridBounds[1]; y++ ) {
			for ( int z = 0; z < lightGridBounds[2]; z++ ) {
				lightGridPoint_t &gridPoint = lightGridPoints[ x * gridStep0 + y * gridStep1 + z * gridStep2 ];
				const int gridCoord[3] = { x, y, z };
				const idVec3 baseOrigin = LightGrid_GetIdealGridPointOrigin( *this, gridCoord );
				gridPoint.origin = baseOrigin;
				gridPoint.visibilityMeanDistance = LIGHTGRID_VISIBILITY_MAX_DISTANCE;
				gridPoint.visibilityMeanDistanceSq = LIGHTGRID_VISIBILITY_MAX_DISTANCE * LIGHTGRID_VISIBILITY_MAX_DISTANCE;
				gridPoint.valid = LightGrid_RelocateProbeOrigin( world, area, baseOrigin, gridPoint.origin );

				if ( !gridPoint.valid ) {
					invalidCount++;
				} else {
					if ( gridPoint.valid == LIGHTGRID_POINT_RELOCATED || gridPoint.valid == LIGHTGRID_POINT_RELOCATED_NEAR_SOLID ) {
						relocatedCount++;
					}
					if ( gridPoint.valid == LIGHTGRID_POINT_NEAR_SOLID || gridPoint.valid == LIGHTGRID_POINT_RELOCATED_NEAR_SOLID ) {
						nearSolidCount++;
					}
				}
			}
		}
	}

	validGridPointCount = lightGridPoints.Num() - invalidCount;
	relocatedGridPointCount = relocatedCount;
	nearSolidGridPointCount = nearSolidCount;

	if ( printToConsole ) {
		common->Printf(
			"lightgrid area %i of %i: %i x %i x %i points, %i valid (%i relocated, %i near solid), grid size (%i %i %i)\n",
			area,
			totalAreas,
			lightGridBounds[0],
			lightGridBounds[1],
			lightGridBounds[2],
			validGridPointCount,
			relocatedGridPointCount,
			nearSolidGridPointCount,
			static_cast<int>( lightGridSize.x ),
			static_cast<int>( lightGridSize.y ),
			static_cast<int>( lightGridSize.z ) );
	}
}

void LightGrid::GetBaseGridCoord( const idVec3 &origin, int gridCoord[3] ) const {
	idVec3 lightOrigin = origin - lightGridOrigin;
	for ( int i = 0; i < 3; i++ ) {
		if ( lightGridBounds[i] <= 0 || lightGridSize[i] <= 0.0f ) {
			gridCoord[i] = 0;
			continue;
		}

		float v = lightOrigin[i] * ( 1.0f / lightGridSize[i] );
		int pos = idMath::Ftoi( idMath::Floor( v ) );
		pos = idMath::ClampInt( 0, lightGridBounds[i] - 1, pos );
		gridCoord[i] = pos;
	}
}

int LightGrid::GridCoordToProbeIndex( const int gridCoord[3] ) const {
	const int gridStep0 = 1;
	const int gridStep1 = lightGridBounds[0];
	const int gridStep2 = lightGridBounds[0] * lightGridBounds[1];
	return gridCoord[0] * gridStep0 + gridCoord[1] * gridStep1 + gridCoord[2] * gridStep2;
}

idVec3 LightGrid::GetGridCoordDebugColor( const int gridCoord[3] ) const {
	const int gridPointIndex = GridCoordToProbeIndex( gridCoord );
	static const idVec4 colors[] = {
		colorBlue, colorCyan, colorGreen, colorYellow, colorRed, colorWhite, colorPurple
	};

	const idVec4 &color = colors[ gridPointIndex % ( sizeof( colors ) / sizeof( colors[0] ) ) ];
	return idVec3( color.x, color.y, color.z );
}

void idRenderWorldLocal::SetupLightGrid() {
	const int setupStart = Sys_Milliseconds();
	for ( int i = 0; i < numPortalAreas; i++ ) {
		portalAreas[i].lightGrid.Clear();
		portalAreas[i].lightGrid.area = i;
	}

	idStr filename = mapName;
	filename.SetFileExtension( "lightgridpack" );
	if ( LoadLightGridPackFile( filename ) ) {
		PreloadLightGridImages();
		common->DPrintf( "LightGrid setup for %s used packed data in %.3fs\n", mapName.c_str(), ( Sys_Milliseconds() - setupStart ) * 0.001f );
		return;
	}

	for ( int i = 0; i < numPortalAreas; i++ ) {
		portalAreas[i].lightGrid.Clear();
		portalAreas[i].lightGrid.area = i;
	}

	filename = mapName;
	filename.SetFileExtension( "lightgrid" );

	if ( LoadLightGridFile( filename ) ) {
		LoadLightGridImages();
		PreloadLightGridImages();
		common->DPrintf( "LightGrid setup for %s used loose metadata/images in %.3fs\n", mapName.c_str(), ( Sys_Milliseconds() - setupStart ) * 0.001f );
		return;
	}

	// Stock Quake 4 maps do not contain openQ4 light-grid assets.  Building a
	// probe layout here performs thousands of collision/PVS tests even though
	// the runtime cannot consume the layout without baked atlas images.  Bake
	// entry points build the requested layout explicitly, so leave stock maps
	// empty and pay this cost only when the developer actually starts a bake.
	lightGridAvailabilityFrame = -1;
	common->DPrintf( "LightGrid setup for %s found no baked assets in %.3fs\n", mapName.c_str(), ( Sys_Milliseconds() - setupStart ) * 0.001f );
}

/*
===================
idRenderWorldLocal::AnyLightGridAvailable

Re-evaluated lazily at most once per frame so bakes/reloads pick up within a
frame; on stock content (no baked grids) this stays false for the whole level,
letting the front-end skip the per-drawSurf area resolve and the backend skip
the light-grid indirect pass setup.
===================
*/
bool idRenderWorldLocal::AnyLightGridAvailable() {
	if ( lightGridAvailabilityFrame == tr.frameCount ) {
		return anyLightGridAvailable;
	}
	lightGridAvailabilityFrame = tr.frameCount;
	anyLightGridAvailable = false;
	for ( int i = 0; i < numPortalAreas; i++ ) {
		if ( portalAreas[i].lightGrid.IsUsable() ) {
			anyLightGridAvailable = true;
			break;
		}
	}
	return anyLightGridAvailable;
}

void idRenderWorldLocal::LoadLightGridImages( bool forceReloadLoaded ) {
	const int loadStart = Sys_Milliseconds();
	idStr baseName = mapName;
	baseName.StripFileExtension();
	int handleCount = 0;

	for ( int i = 0; i < numPortalAreas; i++ ) {
		LightGrid &lightGrid = portalAreas[i].lightGrid;
		if ( lightGrid.GridPointCount() <= 0 ) {
			continue;
		}

		idStr imageName = va( "env/%s/area%i_lightgrid_amb", baseName.c_str(), i );
		lightGrid.irradianceImage = globalImages->ImageHandleDeferred( imageName, TF_LINEAR, TR_CLAMP, TD_LIGHTGRID, CF_2D );
		if ( lightGrid.irradianceImage != NULL ) {
			handleCount++;
			if ( forceReloadLoaded && lightGrid.irradianceImage->IsLoaded() ) {
				lightGrid.irradianceImage->Reload( true );
			}
			if ( lightGrid.irradianceImage->IsLoaded() && lightGrid.irradianceImage->IsDefaulted() ) {
				common->DPrintf( "LightGrid image missing for area %i: %s\n", i, imageName.c_str() );
			}
		}

		idStr visibilityImageName = va( "env/%s/area%i_lightgrid_vis", baseName.c_str(), i );
		lightGrid.visibilityImage = globalImages->ImageHandleDeferred( visibilityImageName, TF_LINEAR, TR_CLAMP, TD_LIGHTGRID_VISIBILITY, CF_2D );
		if ( lightGrid.visibilityImage != NULL ) {
			handleCount++;
			if ( forceReloadLoaded && lightGrid.visibilityImage->IsLoaded() ) {
				lightGrid.visibilityImage->Reload( true );
			}
			if ( lightGrid.visibilityImage->IsLoaded() && lightGrid.visibilityImage->IsDefaulted() ) {
				common->DPrintf( "LightGrid visibility image missing for area %i: %s\n", i, visibilityImageName.c_str() );
			}
		}

		idStr probeImageName = va( "env/%s/area%i_lightgrid_pos", baseName.c_str(), i );
		lightGrid.probeImage = globalImages->ImageHandleDeferred( probeImageName, TF_LINEAR, TR_CLAMP, TD_LIGHTGRID_PROBE, CF_2D );
		if ( lightGrid.probeImage != NULL ) {
			handleCount++;
			if ( forceReloadLoaded && lightGrid.probeImage->IsLoaded() ) {
				lightGrid.probeImage->Reload( true );
			}
			if ( lightGrid.probeImage->IsLoaded() && lightGrid.probeImage->IsDefaulted() ) {
				common->DPrintf( "LightGrid probe-position image missing for area %i: %s\n", i, probeImageName.c_str() );
			}
		}
	}

	// grid images (re)assigned: force AnyLightGridAvailable to re-evaluate
	// even if tr.frameCount has not advanced (bake paths render inline)
	lightGridAvailabilityFrame = -1;
	common->DPrintf( "LightGrid image handles for %s: %i deferred handles in %.3fs\n", mapName.c_str(), handleCount, ( Sys_Milliseconds() - loadStart ) * 0.001f );
}

void idRenderWorldLocal::PreloadLightGridImages() {
	if ( !tr.IsOpenGLRunning() ) {
		return;
	}
	if ( !r_lightGridPreload.GetBool() ) {
		common->DPrintf(
			"LightGrid deferred atlas residency for %s; visible areas will stream on first use.\n",
			mapName.c_str() );
		return;
	}

	const int loadStart = Sys_Milliseconds();
	int areaCount = 0;
	int failedCount = 0;
	for ( int i = 0; i < numPortalAreas; i++ ) {
		if ( !portalAreas[i].lightGrid.IsUsable() ) {
			continue;
		}
		areaCount++;
		if ( !EnsureLightGridAreaImages( i ) ) {
			failedCount++;
		}
	}

	lightGridAvailabilityFrame = -1;
	common->DPrintf(
		"LightGrid preloaded %i/%i areas for %s in %.3fs\n",
		areaCount - failedCount,
		areaCount,
		mapName.c_str(),
		( Sys_Milliseconds() - loadStart ) * 0.001f );
}

bool idRenderWorldLocal::LoadLightGridFile( const char *name ) {
	idLexer *src = new idLexer( name, LEXFL_NOSTRINGCONCAT | LEXFL_NODOLLARPRECOMPILE );
	if ( !src->IsLoaded() ) {
		delete src;
		return false;
	}

	idToken token;
	if ( !src->ReadToken( &token ) || token.Icmp( LGRID_FILE_ID ) ) {
		common->Warning( "%s is not a light-grid file", name );
		delete src;
		return false;
	}

	if ( !src->ReadToken( &token ) ) {
		common->Warning( "%s is missing a version token", name );
		delete src;
		return false;
	}

	const int version = atoi( token.c_str() );
	if ( version != LIGHTGRID_SUPPORTED_VERSION_A && version != LIGHTGRID_SUPPORTED_VERSION_B && version != LIGHTGRID_SUPPORTED_VERSION_C && version != LIGHTGRID_SUPPORTED_VERSION_D && version != LIGHTGRID_CURRENT_VERSION ) {
		common->Warning( "%s has unsupported light-grid version %i", name, version );
		delete src;
		return false;
	}

	while ( src->ReadToken( &token ) ) {
		if ( token == "lightGridBakeStats" ) {
			src->SkipBracedSection();
			continue;
		}

		if ( token == "lightGridPoints" ) {
			ParseLightGridPoints( src );
			continue;
		}

		if ( token == "lightGridVisibility" ) {
			ParseLightGridVisibility( src );
			continue;
		}

		src->Error( "idRenderWorldLocal::LoadLightGridFile: bad token \"%s\"", token.c_str() );
	}

	delete src;
	return true;
}

void idRenderWorldLocal::ParseLightGridPoints( idLexer *src ) {
	src->ExpectTokenString( "{" );

	const int areaIndex = src->ParseInt();
	if ( areaIndex < 0 || areaIndex >= numPortalAreas ) {
		src->Error( "ParseLightGridPoints: bad area index %i", areaIndex );
		return;
	}

	const int numLightGridPoints = src->ParseInt();
	if ( numLightGridPoints < 0 ) {
		src->Error( "ParseLightGridPoints: bad numLightGridPoints %i", numLightGridPoints );
		return;
	}

	const int imageProbeSize = src->ParseInt();
	const int imageBorderSize = src->ParseInt();
	if ( imageProbeSize <= 0 || imageBorderSize < 0 ) {
		src->Error( "ParseLightGridPoints: bad probe layout %i %i", imageProbeSize, imageBorderSize );
		return;
	}

	LightGrid &lightGrid = portalAreas[areaIndex].lightGrid;
	lightGrid.Clear();
	lightGrid.area = areaIndex;
	lightGrid.imageSingleProbeSize = imageProbeSize;
	lightGrid.imageBorderSize = imageBorderSize;
	lightGrid.visibilityMaxDistance = LIGHTGRID_VISIBILITY_MAX_DISTANCE;
	lightGrid.relocationMaxDistance = LIGHTGRID_RELOCATION_MAX_DISTANCE;
	lightGrid.totalGridPointCount = numLightGridPoints;

	src->Parse1DMatrix( 3, lightGrid.lightGridOrigin.ToFloatPtr() );
	src->Parse1DMatrix( 3, lightGrid.lightGridSize.ToFloatPtr() );
	for ( int i = 0; i < 3; i++ ) {
		lightGrid.lightGridBounds[i] = src->ParseInt();
	}

	lightGrid.lightGridPoints.SetNum( numLightGridPoints );
	for ( int i = 0; i < numLightGridPoints; i++ ) {
		lightGridPoint_t &gridPoint = lightGrid.lightGridPoints[i];
		gridPoint.valid = static_cast<byte>( src->ParseInt() );
		gridPoint.visibilityMeanDistance = LIGHTGRID_VISIBILITY_MAX_DISTANCE;
		gridPoint.visibilityMeanDistanceSq = LIGHTGRID_VISIBILITY_MAX_DISTANCE * LIGHTGRID_VISIBILITY_MAX_DISTANCE;
		if ( gridPoint.valid != 0 ) {
			lightGrid.validGridPointCount++;
			if ( gridPoint.valid == LIGHTGRID_POINT_RELOCATED || gridPoint.valid == LIGHTGRID_POINT_RELOCATED_NEAR_SOLID ) {
				lightGrid.relocatedGridPointCount++;
			}
			if ( gridPoint.valid == LIGHTGRID_POINT_NEAR_SOLID || gridPoint.valid == LIGHTGRID_POINT_RELOCATED_NEAR_SOLID ) {
				lightGrid.nearSolidGridPointCount++;
			}
		}
		src->Parse1DMatrix( 3, gridPoint.origin.ToFloatPtr() );

		if ( src->PeekTokenString( "(" ) ) {
			float ignoredSH[64];
			src->Parse1DMatrixOpenEnded( 64, ignoredSH );
		}
	}

	src->ExpectTokenString( "}" );
}

void idRenderWorldLocal::ParseLightGridVisibility( idLexer *src ) {
	src->ExpectTokenString( "{" );

	const int areaIndex = src->ParseInt();
	if ( areaIndex < 0 || areaIndex >= numPortalAreas ) {
		src->Error( "ParseLightGridVisibility: bad area index %i", areaIndex );
		return;
	}

	const int numLightGridPoints = src->ParseInt();
	if ( numLightGridPoints < 0 ) {
		src->Error( "ParseLightGridVisibility: bad numLightGridPoints %i", numLightGridPoints );
		return;
	}

	LightGrid &lightGrid = portalAreas[areaIndex].lightGrid;
	lightGrid.visibilityMaxDistance = src->ParseFloat();
	if ( lightGrid.visibilityMaxDistance <= 0.0f ) {
		lightGrid.visibilityMaxDistance = LIGHTGRID_VISIBILITY_MAX_DISTANCE;
	}

	for ( int i = 0; i < numLightGridPoints; i++ ) {
		const float meanDistance = src->ParseFloat();
		const float meanDistanceSq = src->ParseFloat();
		if ( i < lightGrid.lightGridPoints.Num() ) {
			lightGridPoint_t &gridPoint = lightGrid.lightGridPoints[i];
			gridPoint.visibilityMeanDistance = meanDistance;
			gridPoint.visibilityMeanDistanceSq = meanDistanceSq;
		}
	}

	src->ExpectTokenString( "}" );
}

void idRenderWorldLocal::WriteLightGridsToFile( const char *name ) const {
	idStr fileName = name;
	fileName.SetFileExtension( "lightgrid" );

	idFile *file = fileSystem->OpenFileWrite( fileName.c_str(), "fs_savepath" );
	if ( file == NULL ) {
		common->Warning( "WriteLightGridsToFile: failed to open %s", fileName.c_str() );
		return;
	}

	file->WriteFloatString( "%s %i\n\n", LGRID_FILE_ID, LIGHTGRID_CURRENT_VERSION );
	for ( int i = 0; i < numPortalAreas; i++ ) {
		LightGrid_WriteLightGridBlock( file, portalAreas[i].lightGrid );
		LightGrid_WriteLightGridVisibilityBlock( file, portalAreas[i].lightGrid );
	}

	fileSystem->CloseFile( file );
	common->Printf( "Wrote %s\n", fileName.c_str() );
}

bool R_BakeCurrentLightGrids( const lightGridBakeOptions_t &options, const char *jobName ) {
	if ( !tr.primaryWorld || !tr.primaryView ) {
		common->Printf( "bakeLightGrids: no primary world/view loaded.\n" );
		return false;
	}

	idRenderWorldLocal *world = tr.primaryWorld;
	idStr baseName = world->mapName;
	baseName.StripFileExtension();
	idStr bakeLabel;
	if ( jobName != NULL && jobName[0] != '\0' ) {
		bakeLabel = jobName;
	} else {
		bakeLabel = baseName;
	}

	const int totalStart = Sys_Milliseconds();
	lightGridBakeRunStats_t runStats;
	LightGrid_InitBakeRunStats( runStats );
	lightGridBakeFileStats_t fileStats;
	LightGrid_InitBakeFileStats( fileStats );
	const bool separateAreas = options.separateAreas;
	lightGridStagedWrite_t stagedLightGridWrite;

	const int layoutStart = Sys_Milliseconds();
	for ( int areaIndex = 0; areaIndex < world->numPortalAreas; areaIndex++ ) {
		LightGrid &lightGrid = world->portalAreas[ areaIndex ].lightGrid;
		lightGrid.SetupGrid( world->portalAreas[ areaIndex ].globalBounds, world, options.gridSize, areaIndex, world->numPortalAreas, options.maxProbes, true );
		LightGrid_AddAreaFileStats( fileStats, lightGrid );
	}
	runStats.layoutMsec += Sys_Milliseconds() - layoutStart;
	fileStats.settingsHash = LightGrid_CalculateBakeSettingsHash( options, world );

	if ( fileStats.validProbes <= 0 ) {
		common->Printf( "bakeLightGrids: no valid probes were generated for %s\n", world->mapName.c_str() );
		return false;
	}

	if ( separateAreas ) {
		stagedLightGridWrite.finalName = world->mapName;
		stagedLightGridWrite.finalName.SetFileExtension( "lightgrid" );
		stagedLightGridWrite.tempName = stagedLightGridWrite.finalName;
		stagedLightGridWrite.tempName += ".baking";

		const int metadataStart = Sys_Milliseconds();
		idFile *stagedLightGridFile = fileSystem->OpenFileWrite( stagedLightGridWrite.tempName.c_str(), "fs_savepath" );
		if ( stagedLightGridFile == NULL ) {
			common->Warning( "bakeLightGrids: failed to open %s for staged metadata", stagedLightGridWrite.tempName.c_str() );
			return false;
		}

		stagedLightGridFile->WriteFloatString( "%s %i\n\n", LGRID_FILE_ID, LIGHTGRID_CURRENT_VERSION );
		LightGrid_WriteBakeStatsBlock( stagedLightGridFile, options, fileStats, world );
		for ( int areaIndex = 0; areaIndex < world->numPortalAreas; areaIndex++ ) {
			LightGrid &lightGrid = world->portalAreas[ areaIndex ].lightGrid;
			LightGrid_WriteLightGridBlock( stagedLightGridFile, lightGrid );
			LightGrid_WriteLightGridVisibilityBlock( stagedLightGridFile, lightGrid );
			lightGrid.DiscardPointData();
		}
		fileSystem->CloseFile( stagedLightGridFile );
		runStats.metadataMsec += Sys_Milliseconds() - metadataStart;
		common->Printf( "bakeLightGrids: separateAreas enabled; probe layouts will be regenerated per area to reduce peak memory use\n" );
	}

	common->Printf(
		"bakeLightGrids: %s has %i valid probes (%i relocated, %i near solid) and %i invalid probes across %i bake areas, capture size %i, blends %i, samples %i, bounces %i, settings 0x%08x\n",
		bakeLabel.c_str(),
		fileStats.validProbes,
		fileStats.relocatedProbes,
		fileStats.nearSolidProbes,
		fileStats.invalidProbes,
		fileStats.bakeAreaCount,
		options.captureSize,
		options.blends,
		options.samples,
		options.bounces,
		static_cast<unsigned int>( fileStats.settingsHash ) );
	if ( options.bounces > 1 ) {
		common->Printf( "bakeLightGrids: bounce 2+ reuse the previous openQ4 bake through the runtime light-grid pass.\n" );
	}

	const bool oldUseLightGrid = r_useLightGrid.GetBool();
	const bool oldSkipPostProcess = r_skipPostProcess.GetBool();
	const bool oldSkipGlowOverlay = r_skipGlowOverlay.GetBool();
	const bool oldSkipSubviews = r_skipSubviews.GetBool();
	const int oldShowLightGrid = r_showLightGrid.GetInteger();
	const bool oldSuppressLevelshotViewModels = tr.suppressLevelshotViewModels;

	r_skipPostProcess.SetBool( true );
	r_skipGlowOverlay.SetBool( true );
	r_skipSubviews.SetBool( true );
	r_showLightGrid.SetInteger( 0 );
	tr.suppressLevelshotViewModels = true;

	renderView_t captureView = tr.primaryView->renderView;
	captureView.x = 0;
	captureView.y = 0;
	captureView.width = glConfig.vidWidth;
	captureView.height = glConfig.vidHeight;
	captureView.fov_x = 90.0f;
	captureView.fov_y = 90.0f;

	const idMat3 *cubeAxes = LightGrid_GetCubeAxes();
	const int faceBytes = options.captureSize * options.captureSize * LIGHTGRID_BAKE_RGB_BYTES_PER_PIXEL;
	const int workerCount = LightGrid_GetBakeWorkerCount();
	const bool useAsyncReadback = LightGrid_UseAsyncReadback( options.captureSize, options.blends );
	LightGridBakeWorkerPool bakeWorkerPool( workerCount );
	LightGridBakeReadbackPool readbackPool( useAsyncReadback ? LightGrid_GetAsyncReadbackSlotCount( bakeWorkerPool.WorkerCount(), faceBytes ) : 0, options.captureSize, faceBytes );

	lightGridBakeProgress_t progress;
	memset( &progress, 0, sizeof( progress ) );
	progress.jobName = bakeLabel.c_str();
	progress.totalAreas = fileStats.bakeAreaCount;
	progress.totalBounces = options.bounces;
	progress.totalValidProbes = fileStats.validProbes;

	if ( bakeWorkerPool.IsEnabled() ) {
		common->Printf( "bakeLightGrids: using %i worker threads for probe integration\n", bakeWorkerPool.WorkerCount() );
	}
	common->Printf( "bakeLightGrids: transient memory budget %i MB\n", idMath::ClampInt( 4, 256, r_lightGridBakeMemoryMB.GetInteger() ) );
	if ( readbackPool.IsEnabled() ) {
		common->Printf( "bakeLightGrids: using %i async readback buffers for probe capture\n", readbackPool.SlotCount() );
	} else if ( r_lightGridBakeAsyncReadback.GetBool() && !useAsyncReadback ) {
		common->Printf( "bakeLightGrids: async readback unavailable for this bake; using synchronous readback\n" );
	}

	for ( int bounce = 0; bounce < options.bounces; bounce++ ) {
		r_useLightGrid.SetBool( bounce > 0 );
		progress.bounceIndex = bounce;
		progress.areasProcessed = 0;
		progress.globalProcessedProbes = 0;
		progress.lastPrintTime = 0;
		idList<lightGridStagedWrite_t> stagedAtlasWrites;
		common->Printf( "bakeLightGrids: %s bounce %i of %i\n", bakeLabel.c_str(), bounce + 1, options.bounces );

		for ( int areaIndex = 0; areaIndex < world->numPortalAreas; areaIndex++ ) {
			LightGrid &lightGrid = world->portalAreas[ areaIndex ].lightGrid;
			if ( lightGrid.GridPointCount() <= 0 || lightGrid.CountValidGridPoints() <= 0 ) {
				continue;
			}

			if ( separateAreas ) {
				lightGrid.SetupGrid( world->portalAreas[ areaIndex ].globalBounds, world, options.gridSize, areaIndex, world->numPortalAreas, options.maxProbes, false );
			}
			if ( !lightGrid.HasPointData() ) {
				continue;
			}

			lightGridBakeAreaPlan_t areaPlan;
			LightGrid_BuildAreaBakePlan( lightGrid, areaPlan );
			if ( areaPlan.validProbeCount <= 0 ) {
				if ( separateAreas ) {
					lightGrid.DiscardPointData();
				}
				continue;
			}

			lightGridStagedWrite_t stagedAtlasWrite;
			stagedAtlasWrite.finalName = va( "env/%s/area%i_lightgrid_amb.tga", baseName.c_str(), areaIndex );
			stagedAtlasWrite.tempName = va( "env/%s/area%i_lightgrid_amb.baking.tga", baseName.c_str(), areaIndex );
			lightGridStagedWrite_t stagedVisibilityWrite;
			stagedVisibilityWrite.finalName = va( "env/%s/area%i_lightgrid_vis.tga", baseName.c_str(), areaIndex );
			stagedVisibilityWrite.tempName = va( "env/%s/area%i_lightgrid_vis.baking.tga", baseName.c_str(), areaIndex );
			lightGridStagedWrite_t stagedProbeWrite;
			stagedProbeWrite.finalName = va( "env/%s/area%i_lightgrid_pos.tga", baseName.c_str(), areaIndex );
			stagedProbeWrite.tempName = va( "env/%s/area%i_lightgrid_pos.baking.tga", baseName.c_str(), areaIndex );
			const int batchProbeCount = LightGrid_GetCaptureBatchProbeCount( lightGrid, faceBytes, bakeWorkerPool );
			const int areaProbeCount = areaPlan.validProbeCount;
			const int areaInvalidProbeCount = Max( lightGrid.GridPointCount() - areaProbeCount, 0 );
			const int perProbeTransientBytes =
				faceBytes * 6 +
				lightGrid.imageSingleProbeSize * lightGrid.imageSingleProbeSize * LIGHTGRID_BAKE_RGB_BYTES_PER_PIXEL * 2;
			const float batchTransientMB =
				( static_cast<float>( batchProbeCount * perProbeTransientBytes ) / ( 1024.0f * 1024.0f ) );
			const float readbackMB =
				( static_cast<float>( readbackPool.SlotCount() * faceBytes ) / ( 1024.0f * 1024.0f ) );
			const float stripMB =
				( static_cast<float>( areaPlan.atlasWidth * lightGrid.imageSingleProbeSize * LIGHTGRID_BAKE_RGB_BYTES_PER_PIXEL ) / ( 1024.0f * 1024.0f ) );
			int processedProbes = 0;
			bool areaWriteFailed = false;
			progress.areasProcessed++;

			common->Printf(
				"bakeLightGrids: %s bounce %i/%i area %i/%i starting (%i valid probes, %i invalid probes, atlas %dx%d, batch %i probes ~= %.2f MB, readback ~= %.2f MB, strip ~= %.2f MB)\n",
				bakeLabel.c_str(),
				bounce + 1,
				options.bounces,
				progress.areasProcessed,
				fileStats.bakeAreaCount,
				areaProbeCount,
				areaInvalidProbeCount,
				areaPlan.atlasWidth,
				areaPlan.atlasHeight,
				batchProbeCount,
				batchTransientMB,
				readbackMB,
				stripMB );

			idFile *atlasFile = fileSystem->OpenFileWrite( stagedAtlasWrite.tempName.c_str(), "fs_savepath" );
			if ( atlasFile == NULL ) {
				common->Warning( "bakeLightGrids: failed to open %s for writing", stagedAtlasWrite.tempName.c_str() );
				if ( separateAreas ) {
					lightGrid.DiscardPointData();
				}
				continue;
			}
			idFile *visibilityFile = fileSystem->OpenFileWrite( stagedVisibilityWrite.tempName.c_str(), "fs_savepath" );
			if ( visibilityFile == NULL ) {
				common->Warning( "bakeLightGrids: failed to open %s for writing", stagedVisibilityWrite.tempName.c_str() );
				fileSystem->CloseFile( atlasFile );
				LightGrid_RemoveStagedOutputFile( stagedAtlasWrite.tempName );
				if ( separateAreas ) {
					lightGrid.DiscardPointData();
				}
				continue;
			}
			idFile *probeFile = fileSystem->OpenFileWrite( stagedProbeWrite.tempName.c_str(), "fs_savepath" );
			if ( probeFile == NULL ) {
				common->Warning( "bakeLightGrids: failed to open %s for writing", stagedProbeWrite.tempName.c_str() );
				fileSystem->CloseFile( atlasFile );
				fileSystem->CloseFile( visibilityFile );
				LightGrid_RemoveStagedOutputFile( stagedAtlasWrite.tempName );
				LightGrid_RemoveStagedOutputFile( stagedVisibilityWrite.tempName );
				if ( separateAreas ) {
					lightGrid.DiscardPointData();
				}
				continue;
			}
			const int atlasHeaderStart = Sys_Milliseconds();
			if ( !LightGrid_WriteTGA24Header( atlasFile, areaPlan.atlasWidth, areaPlan.atlasHeight ) ||
				 !LightGrid_WriteTGA24Header( visibilityFile, areaPlan.atlasWidth, areaPlan.atlasHeight ) ||
				 !LightGrid_WriteProbePositionAtlas( probeFile, lightGrid ) ) {
				runStats.atlasWriteMsec += Sys_Milliseconds() - atlasHeaderStart;
				common->Warning( "bakeLightGrids: failed to write TGA header/data for %s, %s, or %s", stagedAtlasWrite.tempName.c_str(), stagedVisibilityWrite.tempName.c_str(), stagedProbeWrite.tempName.c_str() );
				fileSystem->CloseFile( atlasFile );
				fileSystem->CloseFile( visibilityFile );
				fileSystem->CloseFile( probeFile );
				LightGrid_RemoveStagedOutputFile( stagedAtlasWrite.tempName );
				LightGrid_RemoveStagedOutputFile( stagedVisibilityWrite.tempName );
				LightGrid_RemoveStagedOutputFile( stagedProbeWrite.tempName );
				if ( separateAreas ) {
					lightGrid.DiscardPointData();
				}
				continue;
			}
			runStats.atlasWriteMsec += Sys_Milliseconds() - atlasHeaderStart;
			runStats.atlasRowCount += LightGrid_GetProbePositionAtlasHeight( lightGrid );

			idTempArray<byte> stripPixels( areaPlan.atlasWidth * lightGrid.imageSingleProbeSize * LIGHTGRID_BAKE_RGB_BYTES_PER_PIXEL );
			idTempArray<byte> stripVisibilityPixels( areaPlan.atlasWidth * lightGrid.imageSingleProbeSize * LIGHTGRID_BAKE_RGB_BYTES_PER_PIXEL );
			int nextTaskIndex = 0;
			for ( int stripIndex = 0; stripIndex < lightGrid.lightGridBounds[1]; stripIndex++ ) {
				const int stripAtlasY = stripIndex * lightGrid.imageSingleProbeSize;
				stripPixels.Zero();
				stripVisibilityPixels.Zero();

				const int stripTaskStart = nextTaskIndex;
				while ( nextTaskIndex < areaPlan.probeTasks.Num() && areaPlan.probeTasks[ nextTaskIndex ].atlasY == stripAtlasY ) {
					nextTaskIndex++;
				}
				const int stripTaskEnd = nextTaskIndex;

				for ( int firstTaskIndex = stripTaskStart; firstTaskIndex < stripTaskEnd; firstTaskIndex += batchProbeCount ) {
					const int taskCount = Min( batchProbeCount, stripTaskEnd - firstTaskIndex );
					idList<lightGridBakeJob_t *> capturedJobs;
					LightGrid_CaptureProbeBatch(
						areaPlan.probeTasks,
						firstTaskIndex,
						taskCount,
						lightGrid,
						world,
						options,
						captureView,
						cubeAxes,
						readbackPool,
						faceBytes,
						capturedJobs,
						runStats );
					LightGrid_ProcessCapturedJobs(
						capturedJobs,
						bakeWorkerPool,
						stripPixels.Ptr(),
						stripVisibilityPixels.Ptr(),
						areaPlan.atlasWidth,
						stripAtlasY,
						progress,
						progress.areasProcessed - 1,
						areaProbeCount,
						processedProbes,
						runStats );
				}

				const int atlasRowsStart = Sys_Milliseconds();
				const bool wroteRows = LightGrid_WriteRGBRowsAsTGA( atlasFile, stripPixels.Ptr(), areaPlan.atlasWidth, lightGrid.imageSingleProbeSize );
				const bool wroteVisibilityRows = LightGrid_WriteRGBRowsAsTGA( visibilityFile, stripVisibilityPixels.Ptr(), areaPlan.atlasWidth, lightGrid.imageSingleProbeSize );
				runStats.atlasWriteMsec += Sys_Milliseconds() - atlasRowsStart;
				runStats.atlasRowCount += lightGrid.imageSingleProbeSize * 2;
				if ( !wroteRows || !wroteVisibilityRows ) {
					common->Warning( "bakeLightGrids: failed to write atlas rows for %s or %s", stagedAtlasWrite.tempName.c_str(), stagedVisibilityWrite.tempName.c_str() );
					areaWriteFailed = true;
					break;
				}
			}

			fileSystem->CloseFile( atlasFile );
			fileSystem->CloseFile( visibilityFile );
			fileSystem->CloseFile( probeFile );

			if ( areaWriteFailed ) {
				LightGrid_RemoveStagedOutputFile( stagedAtlasWrite.tempName );
				LightGrid_RemoveStagedOutputFile( stagedVisibilityWrite.tempName );
				LightGrid_RemoveStagedOutputFile( stagedProbeWrite.tempName );
				common->Warning( "bakeLightGrids: area output is incomplete for %s / %s / %s", stagedAtlasWrite.finalName.c_str(), stagedVisibilityWrite.finalName.c_str(), stagedProbeWrite.finalName.c_str() );
			} else {
				stagedAtlasWrites.Append( stagedAtlasWrite );
				stagedAtlasWrites.Append( stagedVisibilityWrite );
				stagedAtlasWrites.Append( stagedProbeWrite );
				common->Printf(
					"bakeLightGrids: %s bounce %i/%i area %i/%i staged %s + visibility + probe positions (%dx%d, %i valid probes)\n",
					bakeLabel.c_str(),
					bounce + 1,
					options.bounces,
					progress.areasProcessed,
					fileStats.bakeAreaCount,
					stagedAtlasWrite.finalName.c_str(),
					areaPlan.atlasWidth,
					areaPlan.atlasHeight,
					processedProbes );
				runStats.atlasCount += 3;
			}

			if ( separateAreas ) {
				lightGrid.DiscardPointData();
			}
		}

		for ( int i = 0; i < stagedAtlasWrites.Num(); i++ ) {
			const int commitStart = Sys_Milliseconds();
			LightGrid_CommitStagedOutputFile( stagedAtlasWrites[i] );
			runStats.commitMsec += Sys_Milliseconds() - commitStart;
		}
		const int reloadStart = Sys_Milliseconds();
		world->LoadLightGridImages( true );
		runStats.reloadMsec += Sys_Milliseconds() - reloadStart;
	}

	idStr lightGridName = world->mapName;
	lightGridName.SetFileExtension( "lightgrid" );
	bool metadataAvailableForPack = false;
	if ( separateAreas ) {
		const int commitStart = Sys_Milliseconds();
		if ( LightGrid_CommitStagedOutputFile( stagedLightGridWrite ) ) {
			common->Printf( "Wrote %s\n", stagedLightGridWrite.finalName.c_str() );
			metadataAvailableForPack = world->LoadLightGridFile( stagedLightGridWrite.finalName.c_str() );
		}
		runStats.commitMsec += Sys_Milliseconds() - commitStart;
	} else {
		const int metadataStart = Sys_Milliseconds();
		metadataAvailableForPack = LightGrid_WriteLightGridFile( *world, lightGridName.c_str(), options, fileStats );
		runStats.metadataMsec += Sys_Milliseconds() - metadataStart;
	}
	if ( metadataAvailableForPack ) {
		const int packStart = Sys_Milliseconds();
		LightGrid_WriteLightGridPackFile( *world, lightGridName.c_str(), options, fileStats );
		runStats.metadataMsec += Sys_Milliseconds() - packStart;
	}
	const int finalReloadStart = Sys_Milliseconds();
	world->LoadLightGridImages( true );
	runStats.reloadMsec += Sys_Milliseconds() - finalReloadStart;

	tr.suppressLevelshotViewModels = oldSuppressLevelshotViewModels;
	r_showLightGrid.SetInteger( oldShowLightGrid );
	r_skipSubviews.SetBool( oldSkipSubviews );
	r_skipGlowOverlay.SetBool( oldSkipGlowOverlay );
	r_skipPostProcess.SetBool( oldSkipPostProcess );
	r_useLightGrid.SetBool( oldUseLightGrid );

	const int totalEnd = Sys_Milliseconds();
	const int totalMsec = totalEnd - totalStart;
	common->Printf( "bakeLightGrids: %s completed in %.2f minutes\n", bakeLabel.c_str(), totalMsec / ( 1000.0f * 60.0f ) );
	common->Printf(
		"bakeLightGrids: stats settings 0x%08x, areas %i/%i, probes %i valid / %i relocated / %i near-solid / %i invalid / %i total, processed %i, atlases %i, max atlas %dx%d, captures %i probes / %i faces in %i batches, visibility %i probes / %i traces\n",
		static_cast<unsigned int>( fileStats.settingsHash ),
		fileStats.bakeAreaCount,
		fileStats.numPortalAreas,
		fileStats.validProbes,
		fileStats.relocatedProbes,
		fileStats.nearSolidProbes,
		fileStats.invalidProbes,
		fileStats.totalProbes,
		runStats.processedProbeCount,
		runStats.atlasCount,
		fileStats.maxAtlasWidth,
		fileStats.maxAtlasHeight,
		runStats.capturedProbeCount,
		runStats.capturedFaceCount,
		runStats.captureBatchCount,
		runStats.visibilityProbeCount,
		runStats.visibilityTraceCount );
	common->Printf(
		"bakeLightGrids: phase timings layout %.2fs, capture %.2fs, visibility %.2fs, integration %.2fs, atlas-write %.2fs, metadata %.2fs, commit %.2fs, reload %.2fs, async-readback-stall %.2fs over %i drains, total %.2fs\n",
		runStats.layoutMsec * 0.001f,
		runStats.captureMsec * 0.001f,
		runStats.visibilityMsec * 0.001f,
		runStats.integrationMsec * 0.001f,
		runStats.atlasWriteMsec * 0.001f,
		runStats.metadataMsec * 0.001f,
		runStats.commitMsec * 0.001f,
		runStats.reloadMsec * 0.001f,
		readbackPool.ReadbackStallMilliseconds() * 0.001f,
		readbackPool.DrainedReadbacks(),
		totalMsec * 0.001f );
	return true;
}
