// Copyright (C) 2026 DarkMatter Productions
//

#ifndef __MODERN_SPECULAR_PROBE_ATLAS_H__
#define __MODERN_SPECULAR_PROBE_ATLAS_H__

#include "RendererCaps.h"

#include <cstdint>

class idImage;

/*
===============================================================================

	Bounded six-face residency for authored specular probes.

	Each cubemap owns six consecutive 256x256 cells in a fixed 2048x2048
	linear RGBA16F 2D atlas with independently convolved GGX mip levels.
	UV rectangles address texel centres at each level, so bilinear filtering
	cannot cross into an adjacent face even though no gutter pixels are needed.
	The fixed face order is +X, -X, +Y, -Y, +Z, -Z.

	The placement helper below is graphics-API independent. Cluster record and
	shader work can therefore pin the exact packing contract without creating a
	GL context or depending on pointer values.

===============================================================================
*/

static const int MODERN_SPECULAR_PROBE_ATLAS_SIZE = 2048;
static const int MODERN_SPECULAR_PROBE_ATLAS_FACE_SIZE = 256;
static const int MODERN_SPECULAR_PROBE_ATLAS_CELLS_PER_ROW = 8;
static const int MODERN_SPECULAR_PROBE_ATLAS_FACE_COUNT = 6;
static const int MODERN_SPECULAR_PROBE_ATLAS_MAX_ENTRIES = 8;
static const int MODERN_SPECULAR_PROBE_ATLAS_TEXTURE_UNIT = 21;
// Retain at least 4x4 samples per face. Adjacent edge samples then share
// directions even at maximum roughness instead of becoming six flat colors.
static const int MODERN_SPECULAR_PROBE_ATLAS_MAX_MIP = 6;
static const int MODERN_SPECULAR_PROBE_ANALYTIC_SLOT = 8;
static const int MODERN_SPECULAR_PROBE_DIFFUSE_FIRST_CELL = 54;
static const int MODERN_SPECULAR_PROBE_DIFFUSE_SIZE = 32;
static const int MODERN_SPECULAR_PROBE_BRDF_CELL = 63;
static const int MODERN_SPECULAR_PROBE_BRDF_SIZE = 128;

static_assert( MODERN_SPECULAR_PROBE_ANALYTIC_SLOT == MODERN_SPECULAR_PROBE_ATLAS_MAX_ENTRIES,
	"analytic environment must follow authored probe slots" );
static_assert( ( MODERN_SPECULAR_PROBE_ANALYTIC_SLOT + 1 ) * MODERN_SPECULAR_PROBE_ATLAS_FACE_COUNT == MODERN_SPECULAR_PROBE_DIFFUSE_FIRST_CELL,
	"diffuse tiles must follow all specular cells" );
static_assert( MODERN_SPECULAR_PROBE_DIFFUSE_FIRST_CELL + MODERN_SPECULAR_PROBE_ANALYTIC_SLOT + 1 == MODERN_SPECULAR_PROBE_BRDF_CELL,
	"BRDF tile must follow all diffuse tiles" );
static_assert( MODERN_SPECULAR_PROBE_BRDF_CELL + 1 == MODERN_SPECULAR_PROBE_ATLAS_CELLS_PER_ROW * MODERN_SPECULAR_PROBE_ATLAS_CELLS_PER_ROW,
	"all atlas regions must fit without overlap" );

// A background-filtered probe (r_pbrProbeAsyncPrefilter) publishes its tiles
// in this order, mip-major specular faces then the diffuse tile, spending at
// most the budget below of each frame after its first tile; the probe stays
// invisible (the set keeps its fallback) until the last tile lands.
static const int MODERN_SPECULAR_PROBE_TILE_COUNT =
	( MODERN_SPECULAR_PROBE_ATLAS_MAX_MIP + 1 ) * MODERN_SPECULAR_PROBE_ATLAS_FACE_COUNT + 1;
static const unsigned long long MODERN_SPECULAR_PROBE_PUBLISH_BUDGET_US = 2000;

// Atlas cell, mip level and edge size of a slot's tile in publishing order.
inline void ModernSpecularProbeAtlas_TileTarget( int slot, int tile, int &cell, int &level, int &size ) {
	if ( tile >= MODERN_SPECULAR_PROBE_TILE_COUNT - 1 ) {
		cell = MODERN_SPECULAR_PROBE_DIFFUSE_FIRST_CELL + slot;
		level = 0;
		size = MODERN_SPECULAR_PROBE_DIFFUSE_SIZE;
		return;
	}
	level = tile / MODERN_SPECULAR_PROBE_ATLAS_FACE_COUNT;
	cell = slot * MODERN_SPECULAR_PROBE_ATLAS_FACE_COUNT + tile % MODERN_SPECULAR_PROBE_ATLAS_FACE_COUNT;
	size = MODERN_SPECULAR_PROBE_ATLAS_FACE_SIZE >> level;
}

typedef enum modernSpecularProbeAtlasFace_e {
	MODERN_SPECULAR_PROBE_FACE_POSITIVE_X = 0,
	MODERN_SPECULAR_PROBE_FACE_NEGATIVE_X,
	MODERN_SPECULAR_PROBE_FACE_POSITIVE_Y,
	MODERN_SPECULAR_PROBE_FACE_NEGATIVE_Y,
	MODERN_SPECULAR_PROBE_FACE_POSITIVE_Z,
	MODERN_SPECULAR_PROBE_FACE_NEGATIVE_Z
} modernSpecularProbeAtlasFace_t;

typedef struct modernSpecularProbeAtlasPlacement_s {
	bool			valid;
	int			slot;
	int			faceSize;
	int			faceCells[MODERN_SPECULAR_PROBE_ATLAS_FACE_COUNT];
	std::uint64_t atlasGeneration;
	std::uint64_t residencyGeneration;
	std::uint64_t sourceStorageGeneration;
	float			faceRects[MODERN_SPECULAR_PROBE_ATLAS_FACE_COUNT][4];
} modernSpecularProbeAtlasPlacement_t;

inline void ModernSpecularProbeAtlas_ClearPlacement(
		modernSpecularProbeAtlasPlacement_t &placement ) {
	placement.valid = false;
	placement.slot = -1;
	placement.faceSize = 0;
	placement.atlasGeneration = 0;
	placement.residencyGeneration = 0;
	placement.sourceStorageGeneration = 0;
	for ( int face = 0; face < MODERN_SPECULAR_PROBE_ATLAS_FACE_COUNT; ++face ) {
		placement.faceCells[face] = -1;
		for ( int component = 0; component < 4; ++component ) {
			placement.faceRects[face][component] = 0.0f;
		}
	}
}

inline bool ModernSpecularProbeAtlas_BuildPlacement( int slot, int faceSize,
		modernSpecularProbeAtlasPlacement_t &placement ) {
	ModernSpecularProbeAtlas_ClearPlacement( placement );
	if ( slot < 0 || slot >= MODERN_SPECULAR_PROBE_ATLAS_MAX_ENTRIES
			|| faceSize <= 0 || faceSize > MODERN_SPECULAR_PROBE_ATLAS_FACE_SIZE ) {
		return false;
	}

	const float inverseAtlas =
		1.0f / static_cast<float>( MODERN_SPECULAR_PROBE_ATLAS_SIZE );
	for ( int face = 0; face < MODERN_SPECULAR_PROBE_ATLAS_FACE_COUNT; ++face ) {
		const int cell = slot * MODERN_SPECULAR_PROBE_ATLAS_FACE_COUNT + face;
		const int cellX = ( cell % MODERN_SPECULAR_PROBE_ATLAS_CELLS_PER_ROW )
			* MODERN_SPECULAR_PROBE_ATLAS_FACE_SIZE;
		const int cellY = ( cell / MODERN_SPECULAR_PROBE_ATLAS_CELLS_PER_ROW )
			* MODERN_SPECULAR_PROBE_ATLAS_FACE_SIZE;
		placement.faceCells[face] = cell;
		placement.faceRects[face][0] =
			( static_cast<float>( cellX ) + 0.5f ) * inverseAtlas;
		placement.faceRects[face][1] =
			( static_cast<float>( cellY ) + 0.5f ) * inverseAtlas;
		placement.faceRects[face][2] =
			static_cast<float>( MODERN_SPECULAR_PROBE_ATLAS_FACE_SIZE - 1 ) * inverseAtlas;
		placement.faceRects[face][3] =
			static_cast<float>( MODERN_SPECULAR_PROBE_ATLAS_FACE_SIZE - 1 ) * inverseAtlas;
	}
	placement.valid = true;
	placement.slot = slot;
	placement.faceSize = faceSize;
	return true;
}

typedef enum modernSpecularProbeAtlasReject_e {
	MODERN_SPECULAR_PROBE_ATLAS_REJECT_NONE = 0,
	MODERN_SPECULAR_PROBE_ATLAS_REJECT_NULL_OUTPUT,
	MODERN_SPECULAR_PROBE_ATLAS_REJECT_NULL_IMAGE,
	MODERN_SPECULAR_PROBE_ATLAS_REJECT_UNAVAILABLE,
	MODERN_SPECULAR_PROBE_ATLAS_REJECT_NOT_LOADED,
	MODERN_SPECULAR_PROBE_ATLAS_REJECT_DEFAULTED,
	MODERN_SPECULAR_PROBE_ATLAS_REJECT_MUTABLE,
	MODERN_SPECULAR_PROBE_ATLAS_REJECT_NOT_CUBE,
	MODERN_SPECULAR_PROBE_ATLAS_REJECT_NON_SQUARE,
	MODERN_SPECULAR_PROBE_ATLAS_REJECT_OVERSIZED,
	MODERN_SPECULAR_PROBE_ATLAS_REJECT_INVALID_STORAGE,
	MODERN_SPECULAR_PROBE_ATLAS_REJECT_ATLAS_FULL,
	MODERN_SPECULAR_PROBE_ATLAS_REJECT_SOURCE_CHANGED,
	MODERN_SPECULAR_PROBE_ATLAS_REJECT_UPLOAD_UNAVAILABLE,
	// The source is resident-bound but its background prefilter has not yet
	// published; the set keeps its analytic fallback for this frame.
	MODERN_SPECULAR_PROBE_ATLAS_REJECT_PREFILTER_PENDING
} modernSpecularProbeAtlasReject_t;

typedef struct modernSpecularProbeAtlasStats_s {
	bool		available;
	bool		initialized;
	bool		textureReady;
	bool		frameLossless;
	bool		frameReady;
	int			atlasSize;
	int			faceSize;
	int			capacity;
	int			residentEntries;
	int			liveEntries;
	int			pendingEntries;
	int			acquires;
	int			cacheHits;
	int			uploadedEntries;
	int			uploadedFaces;
	int			reloads;
	int			evictions;
	int			rejectedNullOutput;
	int			rejectedNullImage;
	int			rejectedUnavailable;
	int			rejectedNotLoaded;
	int			rejectedDefaulted;
	int			rejectedMutable;
	int			rejectedNotCube;
	int			rejectedNonSquare;
	int			rejectedOversized;
	int			rejectedInvalidStorage;
	int			rejectedAtlasFull;
	int			rejectedSourceChanged;
	int			rejectedUploadUnavailable;
	// Background prefilter (r_pbrProbeAsyncPrefilter), cumulative since Init.
	unsigned int prefilterStarted;
	unsigned int prefilterPublished;
	unsigned int prefilterCancelled;
	unsigned int prefilterWaits;
	unsigned int prefilterRunning;
	unsigned long long prefilterLatencyTotalUs;
	unsigned long long prefilterLatencyMaxUs;
	unsigned long long frameFilterTotalUs;	// frame-thread filter/upload time per published entry
	unsigned long long frameFilterMaxUs;	// most one non-capture flush spent filtering/uploading
	unsigned int frameFilterCount;
	unsigned long long sourceReadTotalUs;	// synchronous source readback per miss
	unsigned long long sourceReadMaxUs;
	unsigned int sourceReads;
	std::uint64_t atlasGeneration;
	std::uint64_t frameGeneration;
	char		status[96];
	char		lastReject[64];
} modernSpecularProbeAtlasStats_t;

void R_ModernSpecularProbeAtlas_Init( const renderBackendCaps_t &caps,
	const renderFeatureSet_t &features );
void R_ModernSpecularProbeAtlas_Shutdown( void );
// Allocate/filter only when an authored PBR environment consumer is present.
// Disabled PBR and ordinary stock maps incur no allocation or convolution.
void R_ModernSpecularProbeAtlas_BeginFrame( bool environmentRequested );

// Acquire performs no GL transfer. It reserves or reuses six cells and fills a
// generation-stamped placement. Every rejection clears the placement and
// poisons FrameReady for this frame, preventing partial visual consumption.
modernSpecularProbeAtlasReject_t R_ModernSpecularProbeAtlas_Acquire(
	const idImage *image, modernSpecularProbeAtlasPlacement_t *placement );

// Transfers all pending cube faces at a caller-owned synchronization point.
// FrameReady becomes true only when every live placement is resident, uploaded,
// and still matches its source storage generation. A newly read source is
// prefiltered on a background thread and published by a later flush, unless
// captureFrame (a screenshot) requires the settled result now.
void R_ModernSpecularProbeAtlas_FlushUploads( bool captureFrame );

class idScenePacketFrame;
// True when any scene of the frame is a screenshot capture.
bool R_ModernSpecularProbeAtlas_CaptureFrame( const idScenePacketFrame &frame );

const char *ModernSpecularProbeAtlasReject_Name(
	modernSpecularProbeAtlasReject_t reject );
unsigned int R_ModernSpecularProbeAtlas_Texture( void );
bool R_ModernSpecularProbeAtlas_Ready( void );
bool R_ModernSpecularProbeAtlas_FrameReady( void );
const modernSpecularProbeAtlasStats_t &R_ModernSpecularProbeAtlas_Stats( void );
void R_ModernSpecularProbeAtlas_PrintGfxInfo( void );
bool RendererSpecularProbeAtlas_RunSelfTest( void );

#endif /* !__MODERN_SPECULAR_PROBE_ATLAS_H__ */
