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
#if defined( USE_SDL3 )
#include <SDL3/SDL.h>
#endif

static const char *LGRID_FILE_ID = "LGRID";
static const int LIGHTGRID_SUPPORTED_VERSION_A = 3;
static const int LIGHTGRID_SUPPORTED_VERSION_B = 4;
static const int LIGHTGRID_DEFAULT_SINGLE_PROBE_SIZE = 16;
static const int LIGHTGRID_DEFAULT_BORDER_SIZE = 2;
static const idVec3 LIGHTGRID_DEFAULT_SIZE( 64.0f, 64.0f, 128.0f );
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

typedef struct lightGridBakeProbeTask_s {
	int					atlasX;
	int					atlasY;
	idVec3				origin;
} lightGridBakeProbeTask_t;

typedef struct lightGridBakeAreaPlan_s {
	int								areaIndex;
	int								atlasWidth;
	int								atlasHeight;
	int								validProbeCount;
	idList<lightGridBakeProbeTask_t>	probeTasks;
} lightGridBakeAreaPlan_t;

void R_ReadTiledPixels( int width, int height, byte *buffer, renderView_t *ref );
static void LightGrid_CaptureViewRGB( int width, int height, int blends, renderView_t *ref, byte *rgbOut );
static void LightGrid_BakeProbeTile( byte *outTile, int probeSize, int borderSize, byte *cubeFaces[6], int captureSize, int sampleCount );
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
	byte *				capturedFaces;
	byte *				probeTile;

	lightGridBakeJob_s( int _atlasX, int _atlasY, int _probeSize, int _borderSize, int _captureSize, int _sampleCount, int _faceBytes )
		: atlasX( _atlasX )
		, atlasY( _atlasY )
		, probeSize( _probeSize )
		, borderSize( _borderSize )
		, captureSize( _captureSize )
		, sampleCount( _sampleCount )
		, faceBytes( _faceBytes )
		, pendingReadbacks( 0 )
		, capturedFaces( NULL )
		, probeTile( NULL ) {
		capturedFaces = new byte[ faceBytes * 6 ];
		probeTile = new byte[ probeSize * probeSize * LIGHTGRID_BAKE_RGB_BYTES_PER_PIXEL ];
	}

	~lightGridBakeJob_s() {
		delete[] capturedFaces;
		delete[] probeTile;
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
		, outstandingReads( 0 ) {
		const int slotCount = idMath::ClampInt( 0, LIGHTGRID_BAKE_MAX_ASYNC_READBACK_SLOTS, requestedSlotCount );
		if ( slotCount <= 0 || readbackBytes <= 0 || readbackSize <= 0 ) {
			return;
		}

		slots.SetNum( slotCount );
		idTempArray<GLuint> pboIds( slotCount );
		memset( pboIds.Ptr(), 0, sizeof( GLuint ) * slotCount );
		glGenBuffersARB( slotCount, pboIds.Ptr() );

		for ( int i = 0; i < slotCount; i++ ) {
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
		glDeleteBuffersARB( slots.Num(), pboIds.Ptr() );
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

		slot.job = NULL;
		slot.faceIndex = 0;
		slot.inUse = false;
		outstandingReads--;
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

static void LightGrid_CopyProbeTileToPixels( byte *pixels, int pixelWidth, int pixelBaseAtlasY, const lightGridBakeJob_t &job ) {
	const int rowOffset = job.atlasY - pixelBaseAtlasY;
	for ( int row = 0; row < job.probeSize; row++ ) {
		memcpy(
			pixels + ( ( rowOffset + row ) * pixelWidth + job.atlasX ) * LIGHTGRID_BAKE_RGB_BYTES_PER_PIXEL,
			job.probeTile + row * job.probeSize * LIGHTGRID_BAKE_RGB_BYTES_PER_PIXEL,
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
				task.atlasX = ( x + z * lightGrid.lightGridBounds[0] ) * lightGrid.imageSingleProbeSize;
				task.atlasY = y * lightGrid.imageSingleProbeSize;
				areaPlan.probeTasks.Append( task );
				areaPlan.validProbeCount++;
			}
		}
	}
}

static int LightGrid_GetBakeTransientMemoryBudgetBytes() {
	return idMath::ClampInt( 4, 256, r_lightGridBakeMemoryMB.GetInteger() ) * 1024 * 1024;
}

static int LightGrid_GetCaptureBatchProbeCount( const LightGrid &lightGrid, int faceBytes, const LightGridBakeWorkerPool &bakeWorkerPool ) {
	const int budgetBytes = LightGrid_GetBakeTransientMemoryBudgetBytes();
	const int perProbeBytes =
		faceBytes * 6 +
		lightGrid.imageSingleProbeSize * lightGrid.imageSingleProbeSize * LIGHTGRID_BAKE_RGB_BYTES_PER_PIXEL;
	const int maxByBudget = Max( budgetBytes / Max( perProbeBytes, 1 ), 1 );
	const int preferred = bakeWorkerPool.IsEnabled() ? Max( bakeWorkerPool.WorkerCount() * 2, 2 ) : 4;
	return idMath::ClampInt( 1, 64, Min( preferred, maxByBudget ) );
}

static lightGridBakeJob_t *LightGrid_AllocBakeJob( const lightGridBakeProbeTask_t &task, const LightGrid &lightGrid, const lightGridBakeOptions_t &options, int faceBytes ) {
	return new lightGridBakeJob_t(
		task.atlasX,
		task.atlasY,
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
	const LightGrid &lightGrid,
	const lightGridBakeOptions_t &options,
	renderView_t &captureView,
	const idMat3 *cubeAxes,
	LightGridBakeReadbackPool &readbackPool,
	int faceBytes,
	idList<lightGridBakeJob_t *> &capturedJobs ) {
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
}

static void LightGrid_ProcessCapturedJobs(
	idList<lightGridBakeJob_t *> &capturedJobs,
	LightGridBakeWorkerPool &bakeWorkerPool,
	byte *targetPixels,
	int targetWidth,
	int pixelBaseAtlasY,
	lightGridBakeProgress_t &progress,
	int areaOrdinal,
	int areaProbeCount,
	int &processedProbes ) {
	if ( capturedJobs.Num() <= 0 ) {
		return;
	}

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
			processedProbes++;
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
			processedProbes++;
			progress.globalProcessedProbes++;
			LightGrid_PrintBakeProbeProgress( progress, areaOrdinal, processedProbes, areaProbeCount );
			delete job;
		}
	}

	capturedJobs.Clear();
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

static int LightGrid_GetBakeWorkerCount() {
	const int requestedWorkers = r_lightGridBakeWorkers.GetInteger();
	if ( requestedWorkers < 0 ) {
		return 0;
	}

	if ( requestedWorkers > 0 ) {
		return idMath::ClampInt( 1, LIGHTGRID_BAKE_MAX_WORKERS, requestedWorkers );
	}

#if defined( USE_SDL3 )
	const int logicalCpuCount = SDL_GetNumLogicalCPUCores();
	if ( logicalCpuCount > 1 ) {
		return idMath::ClampInt( 1, LIGHTGRID_BAKE_MAX_WORKERS, logicalCpuCount - 1 );
	}
#endif

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

void LightGrid::Clear() {
	lightGridOrigin.Zero();
	lightGridSize = LIGHTGRID_DEFAULT_SIZE;
	lightGridBounds[0] = 0;
	lightGridBounds[1] = 0;
	lightGridBounds[2] = 0;
	lightGridPoints.Clear();
	totalGridPointCount = 0;
	validGridPointCount = 0;
	area = -1;
	irradianceImage = NULL;
	imageSingleProbeSize = LIGHTGRID_DEFAULT_SINGLE_PROBE_SIZE;
	imageBorderSize = LIGHTGRID_DEFAULT_BORDER_SIZE;
}

bool LightGrid::HasImage() const {
	return irradianceImage != NULL && !irradianceImage->IsDefaulted();
}

int LightGrid::GridPointCount() const {
	return totalGridPointCount;
}

int LightGrid::CountValidGridPoints() const {
	return validGridPointCount;
}

bool LightGrid::HasPointData() const {
	return lightGridPoints.Num() > 0;
}

void LightGrid::DiscardPointData() {
	lightGridPoints.Clear();
}

void LightGrid::SetupGrid( const idBounds &bounds, const idRenderWorld *world, const idVec3 &preferredSize, int areaIndex, int totalAreas, int maxProbes, bool printToConsole ) {
	idImage *existingImage = irradianceImage;
	Clear();
	irradianceImage = existingImage;
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

	for ( int x = 0; x < lightGridBounds[0]; x++ ) {
		for ( int y = 0; y < lightGridBounds[1]; y++ ) {
			for ( int z = 0; z < lightGridBounds[2]; z++ ) {
				lightGridPoint_t &gridPoint = lightGridPoints[ x * gridStep0 + y * gridStep1 + z * gridStep2 ];
				gridPoint.origin = idVec3(
					lightGridOrigin.x + x * lightGridSize.x,
					lightGridOrigin.y + y * lightGridSize.y,
					lightGridOrigin.z + z * lightGridSize.z );
				gridPoint.valid = 0;

				if ( world != NULL && world->PointInArea( gridPoint.origin ) == area ) {
					gridPoint.valid = 1;
				} else if ( world != NULL ) {
					const idVec3 baseOrigin = gridPoint.origin;
					for ( int step = 9; step <= 18 && !gridPoint.valid; step += 9 ) {
						for ( int corner = 0; corner < 8; corner++ ) {
							idVec3 candidate = baseOrigin;
							candidate.x += ( corner & 1 ) ? step : -step;
							candidate.y += ( corner & 2 ) ? step : -step;
							candidate.z += ( corner & 4 ) ? step : -step;
							if ( world->PointInArea( candidate ) == area ) {
								gridPoint.origin = candidate;
								gridPoint.valid = 1;
								break;
							}
						}
					}
				}

				if ( !gridPoint.valid ) {
					invalidCount++;
				}
			}
		}
	}

	validGridPointCount = lightGridPoints.Num() - invalidCount;

	if ( printToConsole ) {
		common->Printf(
			"lightgrid area %i of %i: %i x %i x %i points, %i valid, grid size (%i %i %i)\n",
			area,
			totalAreas,
			lightGridBounds[0],
			lightGridBounds[1],
			lightGridBounds[2],
			validGridPointCount,
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
	for ( int i = 0; i < numPortalAreas; i++ ) {
		portalAreas[i].lightGrid.Clear();
		portalAreas[i].lightGrid.area = i;
	}

	idStr filename = mapName;
	filename.SetFileExtension( "lightgrid" );

	if ( LoadLightGridFile( filename ) ) {
		LoadLightGridImages();
		return;
	}

	int totalValidGridPoints = 0;
	for ( int i = 0; i < numPortalAreas; i++ ) {
		portalAreas[i].lightGrid.SetupGrid( portalAreas[i].globalBounds, this, LIGHTGRID_DEFAULT_SIZE, i, numPortalAreas, -1, false );
		totalValidGridPoints += portalAreas[i].lightGrid.CountValidGridPoints();
	}

	if ( totalValidGridPoints > 0 ) {
		common->DPrintf( "No lightgrid assets found for %s. Generated a runtime probe layout with %i valid points for baking/debugging.\n", mapName.c_str(), totalValidGridPoints );
	}
}

void idRenderWorldLocal::LoadLightGridImages( bool forceReloadLoaded ) {
	idStr baseName = mapName;
	baseName.StripFileExtension();

	for ( int i = 0; i < numPortalAreas; i++ ) {
		LightGrid &lightGrid = portalAreas[i].lightGrid;
		if ( lightGrid.GridPointCount() <= 0 ) {
			continue;
		}

		idStr imageName = va( "env/%s/area%i_lightgrid_amb", baseName.c_str(), i );
		lightGrid.irradianceImage = globalImages->ImageHandleDeferred( imageName, TF_LINEAR, TR_CLAMP, TD_LIGHTGRID, CF_2D );
		if ( lightGrid.irradianceImage != NULL ) {
			if ( forceReloadLoaded && lightGrid.irradianceImage->IsLoaded() ) {
				lightGrid.irradianceImage->Reload( true );
			}
			if ( lightGrid.irradianceImage->IsLoaded() && lightGrid.irradianceImage->IsDefaulted() ) {
				common->DPrintf( "LightGrid image missing for area %i: %s\n", i, imageName.c_str() );
			}
		}
	}
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
	if ( version != LIGHTGRID_SUPPORTED_VERSION_A && version != LIGHTGRID_SUPPORTED_VERSION_B ) {
		common->Warning( "%s has unsupported light-grid version %i", name, version );
		delete src;
		return false;
	}

	while ( src->ReadToken( &token ) ) {
		if ( token == "lightGridPoints" ) {
			ParseLightGridPoints( src );
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
		if ( gridPoint.valid != 0 ) {
			lightGrid.validGridPointCount++;
		}
		src->Parse1DMatrix( 3, gridPoint.origin.ToFloatPtr() );

		if ( src->PeekTokenString( "(" ) ) {
			float ignoredSH[64];
			src->Parse1DMatrixOpenEnded( 64, ignoredSH );
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

	file->WriteFloatString( "%s %i\n\n", LGRID_FILE_ID, LIGHTGRID_SUPPORTED_VERSION_B );
	for ( int i = 0; i < numPortalAreas; i++ ) {
		LightGrid_WriteLightGridBlock( file, portalAreas[i].lightGrid );
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

	const bool separateAreas = options.separateAreas;
	lightGridStagedWrite_t stagedLightGridWrite;
	idFile *stagedLightGridFile = NULL;
	if ( separateAreas ) {
		stagedLightGridWrite.finalName = world->mapName;
		stagedLightGridWrite.finalName.SetFileExtension( "lightgrid" );
		stagedLightGridWrite.tempName = stagedLightGridWrite.finalName;
		stagedLightGridWrite.tempName += ".baking";
		stagedLightGridFile = fileSystem->OpenFileWrite( stagedLightGridWrite.tempName.c_str(), "fs_savepath" );
		if ( stagedLightGridFile == NULL ) {
			common->Warning( "bakeLightGrids: failed to open %s for staged metadata", stagedLightGridWrite.tempName.c_str() );
			return false;
		}

		stagedLightGridFile->WriteFloatString( "%s %i\n\n", LGRID_FILE_ID, LIGHTGRID_SUPPORTED_VERSION_B );
		common->Printf( "bakeLightGrids: separateAreas enabled; probe layouts will be regenerated per area to reduce peak memory use\n" );
	}

	int totalAreas = 0;
	int totalValidProbes = 0;
	for ( int areaIndex = 0; areaIndex < world->numPortalAreas; areaIndex++ ) {
		LightGrid &lightGrid = world->portalAreas[ areaIndex ].lightGrid;
		lightGrid.SetupGrid( world->portalAreas[ areaIndex ].globalBounds, world, options.gridSize, areaIndex, world->numPortalAreas, options.maxProbes, true );
		const int validProbes = lightGrid.CountValidGridPoints();

		if ( stagedLightGridFile != NULL ) {
			LightGrid_WriteLightGridBlock( stagedLightGridFile, lightGrid );
			lightGrid.DiscardPointData();
		}

		if ( validProbes > 0 ) {
			totalAreas++;
			totalValidProbes += validProbes;
		}
	}

	if ( stagedLightGridFile != NULL ) {
		fileSystem->CloseFile( stagedLightGridFile );
		stagedLightGridFile = NULL;
	}

	if ( totalValidProbes <= 0 ) {
		if ( separateAreas ) {
			LightGrid_RemoveStagedOutputFile( stagedLightGridWrite.tempName );
		}
		common->Printf( "bakeLightGrids: no valid probes were generated for %s\n", world->mapName.c_str() );
		return false;
	}

	common->Printf(
		"bakeLightGrids: %s has %i valid probes in %i areas, capture size %i, blends %i, samples %i, bounces %i\n",
		bakeLabel.c_str(),
		totalValidProbes,
		totalAreas,
		options.captureSize,
		options.blends,
		options.samples,
		options.bounces );
	if ( options.bounces > 1 ) {
		common->Printf( "bakeLightGrids: bounce 2+ reuse the previous OpenQ4 bake through the runtime light-grid pass.\n" );
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
	progress.totalAreas = totalAreas;
	progress.totalBounces = options.bounces;
	progress.totalValidProbes = totalValidProbes;

	if ( bakeWorkerPool.IsEnabled() ) {
		common->Printf( "bakeLightGrids: using %i worker threads for probe integration\n", bakeWorkerPool.WorkerCount() );
	}
	common->Printf( "bakeLightGrids: transient memory budget %i MB\n", idMath::ClampInt( 4, 256, r_lightGridBakeMemoryMB.GetInteger() ) );
	if ( readbackPool.IsEnabled() ) {
		common->Printf( "bakeLightGrids: using %i async readback buffers for probe capture\n", readbackPool.SlotCount() );
	} else if ( r_lightGridBakeAsyncReadback.GetBool() && !useAsyncReadback ) {
		common->Printf( "bakeLightGrids: async readback unavailable for this bake; using synchronous readback\n" );
	}

	int totalStart = Sys_Milliseconds();

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
			const int batchProbeCount = LightGrid_GetCaptureBatchProbeCount( lightGrid, faceBytes, bakeWorkerPool );
			const int areaProbeCount = areaPlan.validProbeCount;
			const int perProbeTransientBytes =
				faceBytes * 6 +
				lightGrid.imageSingleProbeSize * lightGrid.imageSingleProbeSize * LIGHTGRID_BAKE_RGB_BYTES_PER_PIXEL;
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
				"bakeLightGrids: %s bounce %i/%i area %i/%i starting (%i probes, atlas %dx%d, batch %i probes ~= %.2f MB, readback ~= %.2f MB, strip ~= %.2f MB)\n",
				bakeLabel.c_str(),
				bounce + 1,
				options.bounces,
				progress.areasProcessed,
				totalAreas,
				areaProbeCount,
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
			if ( !LightGrid_WriteTGA24Header( atlasFile, areaPlan.atlasWidth, areaPlan.atlasHeight ) ) {
				common->Warning( "bakeLightGrids: failed to write TGA header for %s", stagedAtlasWrite.tempName.c_str() );
				fileSystem->CloseFile( atlasFile );
				LightGrid_RemoveStagedOutputFile( stagedAtlasWrite.tempName );
				if ( separateAreas ) {
					lightGrid.DiscardPointData();
				}
				continue;
			}

			idTempArray<byte> stripPixels( areaPlan.atlasWidth * lightGrid.imageSingleProbeSize * LIGHTGRID_BAKE_RGB_BYTES_PER_PIXEL );
			int nextTaskIndex = 0;
			for ( int stripIndex = 0; stripIndex < lightGrid.lightGridBounds[1]; stripIndex++ ) {
				const int stripAtlasY = stripIndex * lightGrid.imageSingleProbeSize;
				stripPixels.Zero();

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
						options,
						captureView,
						cubeAxes,
						readbackPool,
						faceBytes,
						capturedJobs );
					LightGrid_ProcessCapturedJobs(
						capturedJobs,
						bakeWorkerPool,
						stripPixels.Ptr(),
						areaPlan.atlasWidth,
						stripAtlasY,
						progress,
						progress.areasProcessed - 1,
						areaProbeCount,
						processedProbes );
				}

				if ( !LightGrid_WriteRGBRowsAsTGA( atlasFile, stripPixels.Ptr(), areaPlan.atlasWidth, lightGrid.imageSingleProbeSize ) ) {
					common->Warning( "bakeLightGrids: failed to write atlas rows for %s", stagedAtlasWrite.tempName.c_str() );
					areaWriteFailed = true;
					break;
				}
			}

			fileSystem->CloseFile( atlasFile );

			if ( areaWriteFailed ) {
				LightGrid_RemoveStagedOutputFile( stagedAtlasWrite.tempName );
				common->Warning( "bakeLightGrids: area output is incomplete for %s", stagedAtlasWrite.finalName.c_str() );
			} else {
				stagedAtlasWrites.Append( stagedAtlasWrite );
				common->Printf(
					"bakeLightGrids: %s bounce %i/%i area %i/%i staged %s (%dx%d, %i valid probes)\n",
					bakeLabel.c_str(),
					bounce + 1,
					options.bounces,
					progress.areasProcessed,
					totalAreas,
					stagedAtlasWrite.finalName.c_str(),
					areaPlan.atlasWidth,
					areaPlan.atlasHeight,
					processedProbes );
			}

			if ( separateAreas ) {
				lightGrid.DiscardPointData();
			}
		}

		for ( int i = 0; i < stagedAtlasWrites.Num(); i++ ) {
			LightGrid_CommitStagedOutputFile( stagedAtlasWrites[i] );
		}
		world->LoadLightGridImages( true );
	}

	if ( separateAreas ) {
		if ( LightGrid_CommitStagedOutputFile( stagedLightGridWrite ) ) {
			common->Printf( "Wrote %s\n", stagedLightGridWrite.finalName.c_str() );
		}
	} else {
		idStr lightGridName = world->mapName;
		lightGridName.SetFileExtension( "lightgrid" );
		world->WriteLightGridsToFile( lightGridName.c_str() );
	}
	world->LoadLightGridImages( true );

	tr.suppressLevelshotViewModels = oldSuppressLevelshotViewModels;
	r_showLightGrid.SetInteger( oldShowLightGrid );
	r_skipSubviews.SetBool( oldSkipSubviews );
	r_skipGlowOverlay.SetBool( oldSkipGlowOverlay );
	r_skipPostProcess.SetBool( oldSkipPostProcess );
	r_useLightGrid.SetBool( oldUseLightGrid );

	const int totalEnd = Sys_Milliseconds();
	common->Printf( "bakeLightGrids: %s completed in %.2f minutes\n", bakeLabel.c_str(), ( totalEnd - totalStart ) / ( 1000.0f * 60.0f ) );
	return true;
}
