// Copyright (C) 2026 DarkMatter Productions
//

#include "tr_local.h"
#include "ModernSpecularProbeAtlas.h"
#include "GLStateCache.h"
#include "PBREnvironment.h"
#include "GLPixelTransferScope.h"
#include "ScenePackets.h"
#include "RendererMetrics.h"

/*
================================================================================

	Authored reflection cubemaps are convolved into a fixed HDR 2D face atlas,
	with diffuse irradiance and a split-sum BRDF table in reserved cells. Acquire
	is CPU-only. BeginFrame lazily creates the environment for PBR consumers;
	FlushUploads owns the bounded authored-image readback/convolution batch.
	Neither path binds a framebuffer, program, VAO, or viewport.

	Residency is keyed by image identity, device handle, and image storage
	generation. LRU selection uses frame generations with a stable slot-index tie
	break, and no slot used in the current frame may be evicted.

================================================================================
*/

typedef struct modernSpecularProbeAtlasEntry_s {
	const idImage *	image;
	unsigned int	sourceHandle;
	std::uint64_t	sourceStorageGeneration;
	std::uint64_t	residencyGeneration;
	std::uint64_t	lastUsedFrame;
	int				slot;
	int				faceSize;
	bool			resident;
	bool			pendingUpload;
	bool			uploaded;
	bool			prefilterRunning;	// prefilterJobs[slot] holds this source
	unsigned long long prefilterStartClock;
	modernSpecularProbeAtlasPlacement_t placement;
} modernSpecularProbeAtlasEntry_t;

static bool rg_modernSpecularProbeAtlasAvailable = false;
static bool rg_modernSpecularProbeAtlasInitialized = false;
static bool rg_modernSpecularProbeAtlasAllocationAttempted = false;
static GLuint rg_modernSpecularProbeAtlasTexture = 0;
static modernSpecularProbeAtlasEntry_t
	rg_modernSpecularProbeAtlasEntries[MODERN_SPECULAR_PROBE_ATLAS_MAX_ENTRIES];
static std::uint64_t rg_modernSpecularProbeAtlasGenerationCounter = 0;
static std::uint64_t rg_modernSpecularProbeAtlasGeneration = 0;
static std::uint64_t rg_modernSpecularProbeAtlasNextResidencyGeneration = 0;
static std::uint64_t rg_modernSpecularProbeAtlasFrame = 0;
static modernSpecularProbeAtlasStats_t rg_modernSpecularProbeAtlasStats;
static openq4PBR::AsyncPrefilter rg_modernSpecularProbePrefilterJobs[MODERN_SPECULAR_PROBE_ATLAS_MAX_ENTRIES];
// Finished background tiles awaiting their budgeted upload slices.
struct modernSpecularProbePublish_t {
	bool taken = false;
	int nextTile = 0;
	openq4PBR::PrefilteredTiles tiles;
};
static modernSpecularProbePublish_t rg_modernSpecularProbePublish[MODERN_SPECULAR_PROBE_ATLAS_MAX_ENTRIES];
static const unsigned int MODERN_SPECULAR_PROBE_PREFILTER_WORKERS = 4;

static void R_ModernSpecularProbeAtlas_CancelPrefilter( int slot ) {
	if ( rg_modernSpecularProbePrefilterJobs[slot].Running() || rg_modernSpecularProbePublish[slot].taken ) {
		rg_modernSpecularProbePrefilterJobs[slot].Cancel();
		rg_modernSpecularProbeAtlasStats.prefilterCancelled++;
	}
	rg_modernSpecularProbePublish[slot] = modernSpecularProbePublish_t();
}

bool R_ModernSpecularProbeAtlas_CaptureFrame( const idScenePacketFrame &frame ) {
	if ( R_PBRProbes_SettleRequested() ) {
		return true;
	}
	for ( int i = 0; i < frame.NumScenes(); ++i ) {
		if ( frame.Scene( i ).temporalCaptureFrame ) {
			return true;
		}
	}
	return false;
}

const char *ModernSpecularProbeAtlasReject_Name(
		modernSpecularProbeAtlasReject_t reject ) {
	switch ( reject ) {
	case MODERN_SPECULAR_PROBE_ATLAS_REJECT_NONE:				return "none";
	case MODERN_SPECULAR_PROBE_ATLAS_REJECT_NULL_OUTPUT:		return "null-output";
	case MODERN_SPECULAR_PROBE_ATLAS_REJECT_NULL_IMAGE:		return "null-image";
	case MODERN_SPECULAR_PROBE_ATLAS_REJECT_UNAVAILABLE:		return "unavailable";
	case MODERN_SPECULAR_PROBE_ATLAS_REJECT_NOT_LOADED:		return "image-not-loaded";
	case MODERN_SPECULAR_PROBE_ATLAS_REJECT_DEFAULTED:		return "image-defaulted";
	case MODERN_SPECULAR_PROBE_ATLAS_REJECT_MUTABLE:			return "mutable-image";
	case MODERN_SPECULAR_PROBE_ATLAS_REJECT_NOT_CUBE:			return "not-cubemap";
	case MODERN_SPECULAR_PROBE_ATLAS_REJECT_NON_SQUARE:		return "non-square-cubemap";
	case MODERN_SPECULAR_PROBE_ATLAS_REJECT_OVERSIZED:		return "cubemap-oversized";
	case MODERN_SPECULAR_PROBE_ATLAS_REJECT_INVALID_STORAGE:	return "invalid-storage";
	case MODERN_SPECULAR_PROBE_ATLAS_REJECT_ATLAS_FULL:		return "atlas-full";
	case MODERN_SPECULAR_PROBE_ATLAS_REJECT_SOURCE_CHANGED:	return "source-changed";
	case MODERN_SPECULAR_PROBE_ATLAS_REJECT_UPLOAD_UNAVAILABLE:return "upload-unavailable";
	case MODERN_SPECULAR_PROBE_ATLAS_REJECT_PREFILTER_PENDING:return "prefilter-pending";
	default:											return "unknown";
	}
}

static std::uint64_t R_ModernSpecularProbeAtlas_AdvanceGeneration(
		std::uint64_t &generation ) {
	generation++;
	if ( generation == 0 ) {
		generation = 1;
	}
	return generation;
}

static void R_ModernSpecularProbeAtlas_SetStatus( const char *status ) {
	idStr::Copynz( rg_modernSpecularProbeAtlasStats.status,
		status != NULL ? status : "unknown",
		sizeof( rg_modernSpecularProbeAtlasStats.status ) );
}

static void R_ModernSpecularProbeAtlas_ResetEntries( void ) {
	// Join every background filter before its slot or the module goes away.
	for ( int slot = 0; slot < MODERN_SPECULAR_PROBE_ATLAS_MAX_ENTRIES; ++slot ) {
		R_ModernSpecularProbeAtlas_CancelPrefilter( slot );
	}
	memset( rg_modernSpecularProbeAtlasEntries, 0,
		sizeof( rg_modernSpecularProbeAtlasEntries ) );
	for ( int slot = 0; slot < MODERN_SPECULAR_PROBE_ATLAS_MAX_ENTRIES; ++slot ) {
		rg_modernSpecularProbeAtlasEntries[slot].slot = slot;
		ModernSpecularProbeAtlas_ClearPlacement(
			rg_modernSpecularProbeAtlasEntries[slot].placement );
	}
}

static void R_ModernSpecularProbeAtlas_RefreshCounts( void ) {
	int resident = 0;
	int live = 0;
	int pending = 0;
	unsigned int running = 0;
	for ( int slot = 0; slot < MODERN_SPECULAR_PROBE_ATLAS_MAX_ENTRIES; ++slot ) {
		const modernSpecularProbeAtlasEntry_t &entry =
			rg_modernSpecularProbeAtlasEntries[slot];
		if ( !entry.resident ) {
			continue;
		}
		resident++;
		if ( entry.lastUsedFrame == rg_modernSpecularProbeAtlasFrame ) {
			live++;
		}
		if ( entry.pendingUpload ) {
			pending++;
		}
		if ( entry.prefilterRunning ) {
			running++;
		}
	}
	rg_modernSpecularProbeAtlasStats.prefilterRunning = running;
	rg_modernSpecularProbeAtlasStats.residentEntries = resident;
	rg_modernSpecularProbeAtlasStats.liveEntries = live;
	rg_modernSpecularProbeAtlasStats.pendingEntries = pending;
}

static modernSpecularProbeAtlasReject_t R_ModernSpecularProbeAtlas_RecordReject(
		modernSpecularProbeAtlasReject_t reject ) {
	switch ( reject ) {
	case MODERN_SPECULAR_PROBE_ATLAS_REJECT_NULL_OUTPUT:
		rg_modernSpecularProbeAtlasStats.rejectedNullOutput++;
		break;
	case MODERN_SPECULAR_PROBE_ATLAS_REJECT_NULL_IMAGE:
		rg_modernSpecularProbeAtlasStats.rejectedNullImage++;
		break;
	case MODERN_SPECULAR_PROBE_ATLAS_REJECT_UNAVAILABLE:
		rg_modernSpecularProbeAtlasStats.rejectedUnavailable++;
		break;
	case MODERN_SPECULAR_PROBE_ATLAS_REJECT_NOT_LOADED:
		rg_modernSpecularProbeAtlasStats.rejectedNotLoaded++;
		break;
	case MODERN_SPECULAR_PROBE_ATLAS_REJECT_DEFAULTED:
		rg_modernSpecularProbeAtlasStats.rejectedDefaulted++;
		break;
	case MODERN_SPECULAR_PROBE_ATLAS_REJECT_MUTABLE:
		rg_modernSpecularProbeAtlasStats.rejectedMutable++;
		break;
	case MODERN_SPECULAR_PROBE_ATLAS_REJECT_NOT_CUBE:
		rg_modernSpecularProbeAtlasStats.rejectedNotCube++;
		break;
	case MODERN_SPECULAR_PROBE_ATLAS_REJECT_NON_SQUARE:
		rg_modernSpecularProbeAtlasStats.rejectedNonSquare++;
		break;
	case MODERN_SPECULAR_PROBE_ATLAS_REJECT_OVERSIZED:
		rg_modernSpecularProbeAtlasStats.rejectedOversized++;
		break;
	case MODERN_SPECULAR_PROBE_ATLAS_REJECT_INVALID_STORAGE:
		rg_modernSpecularProbeAtlasStats.rejectedInvalidStorage++;
		break;
	case MODERN_SPECULAR_PROBE_ATLAS_REJECT_ATLAS_FULL:
		rg_modernSpecularProbeAtlasStats.rejectedAtlasFull++;
		break;
	case MODERN_SPECULAR_PROBE_ATLAS_REJECT_SOURCE_CHANGED:
		rg_modernSpecularProbeAtlasStats.rejectedSourceChanged++;
		break;
	case MODERN_SPECULAR_PROBE_ATLAS_REJECT_UPLOAD_UNAVAILABLE:
		rg_modernSpecularProbeAtlasStats.rejectedUploadUnavailable++;
		break;
	default:
		break;
	}
	idStr::Copynz( rg_modernSpecularProbeAtlasStats.lastReject,
		ModernSpecularProbeAtlasReject_Name( reject ),
		sizeof( rg_modernSpecularProbeAtlasStats.lastReject ) );
	rg_modernSpecularProbeAtlasStats.frameLossless = false;
	rg_modernSpecularProbeAtlasStats.frameReady = false;
	return reject;
}

static unsigned int R_ModernSpecularProbeAtlas_SourceHandle(
		const idImage *image ) {
	return image != NULL
		? const_cast<idImage *>( image )->GetDeviceHandle() : 0;
}

static modernSpecularProbeAtlasReject_t
R_ModernSpecularProbeAtlas_ValidateSource( const idImage *image ) {
	if ( image == NULL ) {
		return MODERN_SPECULAR_PROBE_ATLAS_REJECT_NULL_IMAGE;
	}
	if ( !rg_modernSpecularProbeAtlasAvailable
			|| !rg_modernSpecularProbeAtlasInitialized ) {
		return MODERN_SPECULAR_PROBE_ATLAS_REJECT_UNAVAILABLE;
	}
	if ( !image->IsLoaded() ) {
		return MODERN_SPECULAR_PROBE_ATLAS_REJECT_NOT_LOADED;
	}
	if ( image->IsDefaulted() ) {
		return MODERN_SPECULAR_PROBE_ATLAS_REJECT_DEFAULTED;
	}
	if ( R_IsMutableRenderImage( image ) ) {
		return MODERN_SPECULAR_PROBE_ATLAS_REJECT_MUTABLE;
	}
	if ( image->GetOpts().textureType != TT_CUBIC ) {
		return MODERN_SPECULAR_PROBE_ATLAS_REJECT_NOT_CUBE;
	}
	const int width = image->GetUploadWidth();
	const int height = image->GetUploadHeight();
	if ( width <= 0 || height <= 0 || width != height ) {
		return MODERN_SPECULAR_PROBE_ATLAS_REJECT_NON_SQUARE;
	}
	if ( width > MODERN_SPECULAR_PROBE_ATLAS_FACE_SIZE ) {
		return MODERN_SPECULAR_PROBE_ATLAS_REJECT_OVERSIZED;
	}
	if ( R_ModernSpecularProbeAtlas_SourceHandle( image ) == 0
			|| image->GetStorageGeneration() == 0 ) {
		return MODERN_SPECULAR_PROBE_ATLAS_REJECT_INVALID_STORAGE;
	}
	return MODERN_SPECULAR_PROBE_ATLAS_REJECT_NONE;
}

static int R_ModernSpecularProbeAtlas_FindEntry( const idImage *image ) {
	for ( int slot = 0; slot < MODERN_SPECULAR_PROBE_ATLAS_MAX_ENTRIES; ++slot ) {
		if ( rg_modernSpecularProbeAtlasEntries[slot].resident
				&& rg_modernSpecularProbeAtlasEntries[slot].image == image ) {
			return slot;
		}
	}
	return -1;
}

static int R_ModernSpecularProbeAtlas_SelectSlot(
		const modernSpecularProbeAtlasEntry_t *entries,
		std::uint64_t currentFrame ) {
	for ( int slot = 0; slot < MODERN_SPECULAR_PROBE_ATLAS_MAX_ENTRIES; ++slot ) {
		if ( !entries[slot].resident ) {
			return slot;
		}
	}

	int oldest = -1;
	for ( int slot = 0; slot < MODERN_SPECULAR_PROBE_ATLAS_MAX_ENTRIES; ++slot ) {
		if ( entries[slot].lastUsedFrame == currentFrame ) {
			continue;
		}
		if ( oldest < 0
				|| entries[slot].lastUsedFrame < entries[oldest].lastUsedFrame ) {
			oldest = slot;
		}
	}
	return oldest;
}

static bool R_ModernSpecularProbeAtlas_FingerprintMatches(
		const modernSpecularProbeAtlasEntry_t &entry ) {
	return entry.image != NULL
		&& entry.image->IsLoaded()
		&& !entry.image->IsDefaulted()
		&& !R_IsMutableRenderImage( entry.image )
		&& entry.image->GetOpts().textureType == TT_CUBIC
		&& entry.image->GetUploadWidth() == entry.faceSize
		&& entry.image->GetUploadHeight() == entry.faceSize
		&& R_ModernSpecularProbeAtlas_SourceHandle( entry.image )
			== entry.sourceHandle
		&& entry.image->GetStorageGeneration()
			== entry.sourceStorageGeneration;
}

static void R_ModernSpecularProbeAtlas_UploadTile( int cell, int level,
		int size, const std::vector<float> &rgba ) {
	const int stride = MODERN_SPECULAR_PROBE_ATLAS_FACE_SIZE >> level;
	glTexSubImage2D( GL_TEXTURE_2D, level,
		( cell % MODERN_SPECULAR_PROBE_ATLAS_CELLS_PER_ROW ) * stride,
		( cell / MODERN_SPECULAR_PROBE_ATLAS_CELLS_PER_ROW ) * stride,
		size, size, GL_RGBA, GL_FLOAT, rgba.data() );
}

static void R_ModernSpecularProbeAtlas_UploadTiles( int slot, const openq4PBR::PrefilteredTiles &tiles ) {
	for ( int level = 0; level <= MODERN_SPECULAR_PROBE_ATLAS_MAX_MIP; ++level ) {
		const int size = MODERN_SPECULAR_PROBE_ATLAS_FACE_SIZE >> level;
		for ( int face = 0; face < MODERN_SPECULAR_PROBE_ATLAS_FACE_COUNT; ++face ) {
			R_ModernSpecularProbeAtlas_UploadTile( slot * MODERN_SPECULAR_PROBE_ATLAS_FACE_COUNT + face,
				level, size, tiles.specular[ level * MODERN_SPECULAR_PROBE_ATLAS_FACE_COUNT + face ] );
		}
	}
	R_ModernSpecularProbeAtlas_UploadTile( MODERN_SPECULAR_PROBE_DIFFUSE_FIRST_CELL + slot,
		0, MODERN_SPECULAR_PROBE_DIFFUSE_SIZE, tiles.diffuse );
}

static void R_ModernSpecularProbeAtlas_UploadHalfTile( int cell, int level,
		int size, const std::vector<std::uint16_t> &rgba ) {
	const int stride = MODERN_SPECULAR_PROBE_ATLAS_FACE_SIZE >> level;
	glTexSubImage2D( GL_TEXTURE_2D, level,
		( cell % MODERN_SPECULAR_PROBE_ATLAS_CELLS_PER_ROW ) * stride,
		( cell / MODERN_SPECULAR_PROBE_ATLAS_CELLS_PER_ROW ) * stride,
		size, size, GL_RGBA, GL_HALF_FLOAT, rgba.data() );
}

template<class Sampler>
static void R_ModernSpecularProbeAtlas_Filter( int slot, const Sampler &sample ) {
	// The tiles are computed in parallel and uploaded in the original order.
	R_ModernSpecularProbeAtlas_UploadTiles( slot, openq4PBR::PrefilterTiles( sample,
		MODERN_SPECULAR_PROBE_ATLAS_FACE_SIZE, MODERN_SPECULAR_PROBE_ATLAS_MAX_MIP,
		MODERN_SPECULAR_PROBE_DIFFUSE_SIZE ) );
}

void R_ModernSpecularProbeAtlas_Init( const renderBackendCaps_t &caps,
		const renderFeatureSet_t &features ) {
	R_ModernSpecularProbeAtlas_Shutdown();
	memset( &rg_modernSpecularProbeAtlasStats, 0,
		sizeof( rg_modernSpecularProbeAtlasStats ) );
	R_ModernSpecularProbeAtlas_ResetEntries();

	rg_modernSpecularProbeAtlasStats.atlasSize =
		MODERN_SPECULAR_PROBE_ATLAS_SIZE;
	rg_modernSpecularProbeAtlasStats.faceSize =
		MODERN_SPECULAR_PROBE_ATLAS_FACE_SIZE;
	rg_modernSpecularProbeAtlasStats.capacity =
		MODERN_SPECULAR_PROBE_ATLAS_MAX_ENTRIES;
	idStr::Copynz( rg_modernSpecularProbeAtlasStats.lastReject, "none",
		sizeof( rg_modernSpecularProbeAtlasStats.lastReject ) );

	const bool textureLimitReady =
		caps.maxTextureSize >= MODERN_SPECULAR_PROBE_ATLAS_SIZE;
	const bool samplerContractReady =
		caps.maxTextureImageUnits > MODERN_SPECULAR_PROBE_ATLAS_TEXTURE_UNIT;
	rg_modernSpecularProbeAtlasStats.available = features.modernBaseline
		&& textureLimitReady && samplerContractReady;
	if ( !rg_modernSpecularProbeAtlasStats.available ) {
		R_ModernSpecularProbeAtlas_SetStatus( "capability-unavailable" );
		return;
	}
	if ( glGenTextures == NULL || glDeleteTextures == NULL
			|| glGetTexImage == NULL || glTexSubImage2D == NULL ) {
		R_ModernSpecularProbeAtlas_SetStatus( "entry-points-missing" );
		return;
	}
	rg_modernSpecularProbeAtlasAvailable = true;
	R_ModernSpecularProbeAtlas_SetStatus( "standby" );
}

static void R_ModernSpecularProbeAtlas_Allocate( void ) {
	idGLPixelTransferScope transfer;
	rg_modernSpecularProbeAtlasAllocationAttempted = true;
	glGenTextures( 1, &rg_modernSpecularProbeAtlasTexture );
	if ( rg_modernSpecularProbeAtlasTexture != 0 ) {
		R_GLStateCache().ActiveTextureUnit( 0 );
		R_GLStateCache().BindTexture( 0, GL_TEXTURE_2D,
			rg_modernSpecularProbeAtlasTexture );
		byte *zeroed = static_cast<byte *>( Mem_ClearedAlloc(
			MODERN_SPECULAR_PROBE_ATLAS_SIZE
				* MODERN_SPECULAR_PROBE_ATLAS_SIZE * 4 ) );
		bool storageReady = true;
		for ( int level = 0; level <= MODERN_SPECULAR_PROBE_ATLAS_MAX_MIP; ++level ) {
			const int size = MODERN_SPECULAR_PROBE_ATLAS_SIZE >> level;
			glTexImage2D( GL_TEXTURE_2D, level, GL_RGBA16F, size, size, 0,
				GL_RGBA, GL_UNSIGNED_BYTE, zeroed );
			GLint width = 0, height = 0, format = 0;
			glGetTexLevelParameteriv( GL_TEXTURE_2D, level, GL_TEXTURE_WIDTH, &width );
			glGetTexLevelParameteriv( GL_TEXTURE_2D, level, GL_TEXTURE_HEIGHT, &height );
			glGetTexLevelParameteriv( GL_TEXTURE_2D, level, GL_TEXTURE_INTERNAL_FORMAT, &format );
			storageReady = storageReady && width == size && height == size && format == GL_RGBA16F;
		}
		Mem_Free( zeroed );
		if ( !storageReady ) {
			glDeleteTextures( 1, &rg_modernSpecularProbeAtlasTexture );
			rg_modernSpecularProbeAtlasTexture = 0;
			R_ModernSpecularProbeAtlas_SetStatus( "storage-allocation-failed" );
			return;
		}
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, MODERN_SPECULAR_PROBE_ATLAS_MAX_MIP );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
		// Bake the analytic source once before filtering; evaluating its lobe
		// separately for every integration sample needlessly stalls debug builds.
		openq4PBR::Cube analytic;
		analytic.size = 64;
		const openq4PBR::AnalyticEnvironment analyticSample;
		for ( int face = 0; face < 6; ++face ) {
			analytic.faces[face].resize( analytic.size * analytic.size );
			for ( int y = 0; y < analytic.size; ++y ) {
				for ( int x = 0; x < analytic.size; ++x ) {
					analytic.faces[face][y * analytic.size + x] = analyticSample(
						openq4PBR::FaceDirection( face, 2.0f * ( x + 0.5f ) / analytic.size - 1.0f,
							2.0f * ( y + 0.5f ) / analytic.size - 1.0f ) );
				}
			}
		}
		R_ModernSpecularProbeAtlas_Filter( MODERN_SPECULAR_PROBE_ANALYTIC_SLOT, analytic );
		R_ModernSpecularProbeAtlas_UploadTile( MODERN_SPECULAR_PROBE_BRDF_CELL,
			0, MODERN_SPECULAR_PROBE_BRDF_SIZE,
			openq4PBR::BRDFTable( MODERN_SPECULAR_PROBE_BRDF_SIZE ) );
		rg_modernSpecularProbeAtlasStats.textureReady = true;
	}

	rg_modernSpecularProbeAtlasInitialized =
		rg_modernSpecularProbeAtlasStats.textureReady;
	rg_modernSpecularProbeAtlasStats.initialized =
		rg_modernSpecularProbeAtlasInitialized;
	if ( rg_modernSpecularProbeAtlasInitialized ) {
		rg_modernSpecularProbeAtlasGeneration =
			R_ModernSpecularProbeAtlas_AdvanceGeneration(
				rg_modernSpecularProbeAtlasGenerationCounter );
		rg_modernSpecularProbeAtlasNextResidencyGeneration = 0;
		rg_modernSpecularProbeAtlasStats.atlasGeneration =
			rg_modernSpecularProbeAtlasGeneration;
	}
	R_ModernSpecularProbeAtlas_SetStatus(
		rg_modernSpecularProbeAtlasInitialized ? "ready" : "incomplete" );
}

void R_ModernSpecularProbeAtlas_Shutdown( void ) {
	if ( rg_modernSpecularProbeAtlasTexture != 0 && glDeleteTextures != NULL ) {
		glDeleteTextures( 1, &rg_modernSpecularProbeAtlasTexture );
	}
	rg_modernSpecularProbeAtlasTexture = 0;
	rg_modernSpecularProbeAtlasAvailable = false;
	rg_modernSpecularProbeAtlasInitialized = false;
	rg_modernSpecularProbeAtlasAllocationAttempted = false;
	rg_modernSpecularProbeAtlasGeneration = 0;
	rg_modernSpecularProbeAtlasNextResidencyGeneration = 0;
	rg_modernSpecularProbeAtlasFrame = 0;
	R_ModernSpecularProbeAtlas_ResetEntries();
	memset( &rg_modernSpecularProbeAtlasStats, 0,
		sizeof( rg_modernSpecularProbeAtlasStats ) );
	idStr::Copynz( rg_modernSpecularProbeAtlasStats.lastReject, "none",
		sizeof( rg_modernSpecularProbeAtlasStats.lastReject ) );
	R_ModernSpecularProbeAtlas_SetStatus( "off" );
}

void R_ModernSpecularProbeAtlas_BeginFrame( bool environmentRequested ) {
	if ( environmentRequested && rg_modernSpecularProbeAtlasAvailable
			&& !rg_modernSpecularProbeAtlasAllocationAttempted ) {
		R_ModernSpecularProbeAtlas_Allocate();
	}
	R_ModernSpecularProbeAtlas_AdvanceGeneration(
		rg_modernSpecularProbeAtlasFrame );
	rg_modernSpecularProbeAtlasStats.frameGeneration =
		rg_modernSpecularProbeAtlasFrame;
	rg_modernSpecularProbeAtlasStats.acquires = 0;
	rg_modernSpecularProbeAtlasStats.cacheHits = 0;
	rg_modernSpecularProbeAtlasStats.uploadedEntries = 0;
	rg_modernSpecularProbeAtlasStats.uploadedFaces = 0;
	rg_modernSpecularProbeAtlasStats.reloads = 0;
	rg_modernSpecularProbeAtlasStats.evictions = 0;
	rg_modernSpecularProbeAtlasStats.rejectedNullOutput = 0;
	rg_modernSpecularProbeAtlasStats.rejectedNullImage = 0;
	rg_modernSpecularProbeAtlasStats.rejectedUnavailable = 0;
	rg_modernSpecularProbeAtlasStats.rejectedNotLoaded = 0;
	rg_modernSpecularProbeAtlasStats.rejectedDefaulted = 0;
	rg_modernSpecularProbeAtlasStats.rejectedMutable = 0;
	rg_modernSpecularProbeAtlasStats.rejectedNotCube = 0;
	rg_modernSpecularProbeAtlasStats.rejectedNonSquare = 0;
	rg_modernSpecularProbeAtlasStats.rejectedOversized = 0;
	rg_modernSpecularProbeAtlasStats.rejectedInvalidStorage = 0;
	rg_modernSpecularProbeAtlasStats.rejectedAtlasFull = 0;
	rg_modernSpecularProbeAtlasStats.rejectedSourceChanged = 0;
	rg_modernSpecularProbeAtlasStats.rejectedUploadUnavailable = 0;
	idStr::Copynz( rg_modernSpecularProbeAtlasStats.lastReject, "none",
		sizeof( rg_modernSpecularProbeAtlasStats.lastReject ) );
	rg_modernSpecularProbeAtlasStats.frameLossless =
		rg_modernSpecularProbeAtlasInitialized;
	rg_modernSpecularProbeAtlasStats.frameReady =
		rg_modernSpecularProbeAtlasInitialized;
	for ( int slot = 0; slot < MODERN_SPECULAR_PROBE_ATLAS_MAX_ENTRIES; ++slot ) {
		if ( rg_modernSpecularProbeAtlasEntries[slot].resident
				&& rg_modernSpecularProbeAtlasEntries[slot].pendingUpload ) {
			rg_modernSpecularProbeAtlasStats.frameReady = false;
			break;
		}
	}
	R_ModernSpecularProbeAtlas_RefreshCounts();
}

modernSpecularProbeAtlasReject_t R_ModernSpecularProbeAtlas_Acquire(
		const idImage *image,
		modernSpecularProbeAtlasPlacement_t *placement ) {
	if ( placement != NULL ) {
		ModernSpecularProbeAtlas_ClearPlacement( *placement );
	}
	rg_modernSpecularProbeAtlasStats.acquires++;
	if ( placement == NULL ) {
		return R_ModernSpecularProbeAtlas_RecordReject(
			MODERN_SPECULAR_PROBE_ATLAS_REJECT_NULL_OUTPUT );
	}

	const modernSpecularProbeAtlasReject_t validation =
		R_ModernSpecularProbeAtlas_ValidateSource( image );
	if ( validation != MODERN_SPECULAR_PROBE_ATLAS_REJECT_NONE ) {
		return R_ModernSpecularProbeAtlas_RecordReject( validation );
	}

	const unsigned int sourceHandle =
		R_ModernSpecularProbeAtlas_SourceHandle( image );
	const std::uint64_t sourceStorageGeneration =
		image->GetStorageGeneration();
	const int faceSize = image->GetUploadWidth();
	int slot = R_ModernSpecularProbeAtlas_FindEntry( image );
	if ( slot >= 0 ) {
		modernSpecularProbeAtlasEntry_t &entry =
			rg_modernSpecularProbeAtlasEntries[slot];
		const bool stale = entry.sourceHandle != sourceHandle
			|| entry.sourceStorageGeneration != sourceStorageGeneration
			|| entry.faceSize != faceSize;
		if ( !stale ) {
			entry.lastUsedFrame = rg_modernSpecularProbeAtlasFrame;
			*placement = entry.placement;
			rg_modernSpecularProbeAtlasStats.cacheHits++;
			if ( entry.pendingUpload || !entry.uploaded ) {
				rg_modernSpecularProbeAtlasStats.frameReady = false;
			}
			R_ModernSpecularProbeAtlas_RefreshCounts();
			return MODERN_SPECULAR_PROBE_ATLAS_REJECT_NONE;
		}
		if ( entry.lastUsedFrame == rg_modernSpecularProbeAtlasFrame ) {
			return R_ModernSpecularProbeAtlas_RecordReject(
				MODERN_SPECULAR_PROBE_ATLAS_REJECT_SOURCE_CHANGED );
		}
		rg_modernSpecularProbeAtlasStats.reloads++;
	} else {
		slot = R_ModernSpecularProbeAtlas_SelectSlot(
			rg_modernSpecularProbeAtlasEntries,
			rg_modernSpecularProbeAtlasFrame );
		if ( slot < 0 ) {
			return R_ModernSpecularProbeAtlas_RecordReject(
				MODERN_SPECULAR_PROBE_ATLAS_REJECT_ATLAS_FULL );
		}
		if ( rg_modernSpecularProbeAtlasEntries[slot].resident ) {
			rg_modernSpecularProbeAtlasStats.evictions++;
		}
	}

	modernSpecularProbeAtlasEntry_t &entry =
		rg_modernSpecularProbeAtlasEntries[slot];
	// A reused slot (eviction or reload) discards any filter of its old source.
	R_ModernSpecularProbeAtlas_CancelPrefilter( slot );
	memset( &entry, 0, sizeof( entry ) );
	entry.image = image;
	entry.sourceHandle = sourceHandle;
	entry.sourceStorageGeneration = sourceStorageGeneration;
	entry.residencyGeneration =
		R_ModernSpecularProbeAtlas_AdvanceGeneration(
			rg_modernSpecularProbeAtlasNextResidencyGeneration );
	entry.lastUsedFrame = rg_modernSpecularProbeAtlasFrame;
	entry.slot = slot;
	entry.faceSize = faceSize;
	entry.resident = true;
	entry.pendingUpload = true;
	entry.uploaded = false;
	if ( !ModernSpecularProbeAtlas_BuildPlacement(
			slot, faceSize, entry.placement ) ) {
		entry.resident = false;
		return R_ModernSpecularProbeAtlas_RecordReject(
			MODERN_SPECULAR_PROBE_ATLAS_REJECT_INVALID_STORAGE );
	}
	entry.placement.atlasGeneration =
		rg_modernSpecularProbeAtlasGeneration;
	entry.placement.residencyGeneration = entry.residencyGeneration;
	entry.placement.sourceStorageGeneration = sourceStorageGeneration;
	*placement = entry.placement;
	rg_modernSpecularProbeAtlasStats.frameReady = false;
	R_ModernSpecularProbeAtlas_RefreshCounts();
	return MODERN_SPECULAR_PROBE_ATLAS_REJECT_NONE;
}

void R_ModernSpecularProbeAtlas_FlushUploads( bool captureFrame ) {
	if ( !rg_modernSpecularProbeAtlasInitialized ) {
		return;
	}
	R_ModernSpecularProbeAtlas_RefreshCounts();
	if ( rg_modernSpecularProbeAtlasStats.pendingEntries == 0 ) {
		if ( R_PBRProbes_SettleRequested() ) {
			R_PBRProbes_SettleDone();
		}
		rg_modernSpecularProbeAtlasStats.frameReady =
			rg_modernSpecularProbeAtlasStats.frameLossless;
		return;
	}
	if ( glGetTexImage == NULL || glTexSubImage2D == NULL ) {
		R_ModernSpecularProbeAtlas_RecordReject(
			MODERN_SPECULAR_PROBE_ATLAS_REJECT_UPLOAD_UNAVAILABLE );
		return;
	}

	static float scratch[
		MODERN_SPECULAR_PROBE_ATLAS_FACE_SIZE
			* MODERN_SPECULAR_PROBE_ATLAS_FACE_SIZE * 4];
	idGLPixelTransferScope transfer;
	unsigned long long flushSpent = 0;	// frame time spent reading/filtering/publishing
	int flushTiles = 0;
	int flushReads = 0;
	for ( int slot = 0; slot < MODERN_SPECULAR_PROBE_ATLAS_MAX_ENTRIES; ++slot ) {
		modernSpecularProbeAtlasEntry_t &entry =
			rg_modernSpecularProbeAtlasEntries[slot];
		if ( !entry.resident || !entry.pendingUpload ) {
			continue;
		}
		const modernSpecularProbeAtlasReject_t validation =
			R_ModernSpecularProbeAtlas_ValidateSource( entry.image );
		if ( validation != MODERN_SPECULAR_PROBE_ATLAS_REJECT_NONE
				|| !R_ModernSpecularProbeAtlas_FingerprintMatches( entry ) ) {
			R_ModernSpecularProbeAtlas_CancelPrefilter( slot );
			entry.prefilterRunning = false;
			entry.resident = false;
			entry.pendingUpload = false;
			entry.uploaded = false;
			R_ModernSpecularProbeAtlas_RecordReject(
				validation != MODERN_SPECULAR_PROBE_ATLAS_REJECT_NONE
					? validation
					: MODERN_SPECULAR_PROBE_ATLAS_REJECT_SOURCE_CHANGED );
			continue;
		}

		openq4PBR::AsyncPrefilter &job = rg_modernSpecularProbePrefilterJobs[slot];
		modernSpecularProbePublish_t &publish = rg_modernSpecularProbePublish[slot];
		if ( entry.prefilterRunning ) {
			if ( !publish.taken ) {
				if ( !job.Ready() && !captureFrame ) {
					continue;	// still filtering; the set keeps its fallback
				}
				if ( !job.Ready() ) {
					rg_modernSpecularProbeAtlasStats.prefilterWaits++;
				}
				const unsigned long long takeClock = R_RendererMetrics_CpuClock();
				publish.tiles = job.Take();
				publish.taken = true;
				const unsigned long long latency = takeClock - entry.prefilterStartClock;
				rg_modernSpecularProbeAtlasStats.prefilterLatencyTotalUs += latency;
				rg_modernSpecularProbeAtlasStats.prefilterLatencyMaxUs =
					Max( rg_modernSpecularProbeAtlasStats.prefilterLatencyMaxUs, latency );
			}
			// Upload a budgeted slice; the entry stays pending until the last tile.
			const unsigned long long sliceBegin = R_RendererMetrics_CpuClock();
			R_GLStateCache().ActiveTextureUnit( 0 );
			R_GLStateCache().BindTexture( 0, GL_TEXTURE_2D, rg_modernSpecularProbeAtlasTexture );
			while ( publish.nextTile < MODERN_SPECULAR_PROBE_TILE_COUNT ) {
				const unsigned long long spent = flushSpent + ( R_RendererMetrics_CpuClock() - sliceBegin );
				if ( !captureFrame && flushTiles > 0 && spent >= MODERN_SPECULAR_PROBE_PUBLISH_BUDGET_US ) {
					break;
				}
				int cell, level, size;
				ModernSpecularProbeAtlas_TileTarget( slot, publish.nextTile, cell, level, size );
				R_ModernSpecularProbeAtlas_UploadHalfTile( cell, level, size,
					publish.nextTile < MODERN_SPECULAR_PROBE_TILE_COUNT - 1
						? publish.tiles.specularHalf[publish.nextTile] : publish.tiles.diffuseHalf );
				publish.nextTile++;
				flushTiles++;
			}
			const unsigned long long sliceTime = R_RendererMetrics_CpuClock() - sliceBegin;
			flushSpent += sliceTime;
			rg_modernSpecularProbeAtlasStats.frameFilterTotalUs += sliceTime;
			if ( publish.nextTile < MODERN_SPECULAR_PROBE_TILE_COUNT ) {
				continue;
			}
			publish = modernSpecularProbePublish_t();
			entry.prefilterRunning = false;
			rg_modernSpecularProbeAtlasStats.prefilterPublished++;
			rg_modernSpecularProbeAtlasStats.frameFilterCount++;
		} else {
		// With background filtering, a frame that has already spent its budget
		// leaves further source reads to the next flush (one always proceeds).
		if ( r_pbrProbeAsyncPrefilter.GetBool() && !captureFrame
				&& ( flushTiles > 0 || flushReads > 0 )
				&& flushSpent >= MODERN_SPECULAR_PROBE_PUBLISH_BUDGET_US ) {
			continue;
		}
		flushReads++;
		const unsigned long long readBegin = R_RendererMetrics_CpuClock();
		openq4PBR::Cube source;
		source.size = entry.faceSize;
		const bool linearHDR = entry.image->GetOpts().format == FMT_RGBA16F;
		bool sourceValid = true;
		for ( int face = 0;
				face < MODERN_SPECULAR_PROBE_ATLAS_FACE_COUNT; ++face ) {
			R_GLStateCache().ActiveTextureUnit( 0 );
			R_GLStateCache().BindTexture( 0, GL_TEXTURE_CUBE_MAP,
				entry.sourceHandle );
			GLint width = 0, height = 0;
			glGetTexLevelParameteriv( GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, 0, GL_TEXTURE_WIDTH, &width );
			glGetTexLevelParameteriv( GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, 0, GL_TEXTURE_HEIGHT, &height );
			if ( width != entry.faceSize || height != entry.faceSize ) { sourceValid = false; break; }
			glGetTexImage( GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, 0,
				GL_RGBA, GL_FLOAT, scratch );

			source.faces[face].resize( entry.faceSize * entry.faceSize );
			for ( int pixel = 0; pixel < entry.faceSize * entry.faceSize; ++pixel ) {
				float color[3];
				for ( int c = 0; c < 3; ++c ) {
					const float value = scratch[pixel * 4 + c];
					sourceValid = sourceValid && std::isfinite( value );
					color[c] = linearHDR ? Max( 0.0f, value ) : openq4PBRMath::PBRSRGBToLinear( value );
				}
				source.faces[face][pixel] = {
					color[0], color[1], color[2] };
			}
		}
		{
			const unsigned long long readTime = R_RendererMetrics_CpuClock() - readBegin;
			flushSpent += readTime;
			rg_modernSpecularProbeAtlasStats.sourceReads++;
			rg_modernSpecularProbeAtlasStats.sourceReadTotalUs += readTime;
			rg_modernSpecularProbeAtlasStats.sourceReadMaxUs =
				Max( rg_modernSpecularProbeAtlasStats.sourceReadMaxUs, readTime );
		}
		if ( !sourceValid ) {
			entry.resident = entry.pendingUpload = entry.uploaded = false;
			R_ModernSpecularProbeAtlas_RecordReject( MODERN_SPECULAR_PROBE_ATLAS_REJECT_INVALID_STORAGE );
			continue;
		}
		if ( r_pbrProbeAsyncPrefilter.GetBool() && !captureFrame
				&& job.Start( std::move( source ), MODERN_SPECULAR_PROBE_ATLAS_FACE_SIZE,
					MODERN_SPECULAR_PROBE_ATLAS_MAX_MIP, MODERN_SPECULAR_PROBE_DIFFUSE_SIZE,
					MODERN_SPECULAR_PROBE_PREFILTER_WORKERS, true ) ) {
			entry.prefilterRunning = true;
			entry.prefilterStartClock = R_RendererMetrics_CpuClock();
			rg_modernSpecularProbeAtlasStats.prefilterStarted++;
			continue;	// a later flush uploads the tiles
		}
		const unsigned long long filterBegin = R_RendererMetrics_CpuClock();
		R_GLStateCache().BindTexture( 0, GL_TEXTURE_2D, rg_modernSpecularProbeAtlasTexture );
		R_ModernSpecularProbeAtlas_Filter( slot, source );
		const unsigned long long filterTime = R_RendererMetrics_CpuClock() - filterBegin;
		flushSpent += filterTime;
		rg_modernSpecularProbeAtlasStats.frameFilterTotalUs += filterTime;
		rg_modernSpecularProbeAtlasStats.frameFilterCount++;
		}

		if ( !R_ModernSpecularProbeAtlas_FingerprintMatches( entry ) ) {
			entry.resident = false;
			entry.pendingUpload = false;
			entry.uploaded = false;
			R_ModernSpecularProbeAtlas_RecordReject(
				MODERN_SPECULAR_PROBE_ATLAS_REJECT_SOURCE_CHANGED );
			continue;
		}
		entry.pendingUpload = false;
		entry.uploaded = true;
		rg_modernSpecularProbeAtlasStats.uploadedEntries++;
		rg_modernSpecularProbeAtlasStats.uploadedFaces +=
			MODERN_SPECULAR_PROBE_ATLAS_FACE_COUNT;
	}

	R_GLStateCache().ActiveTextureUnit( 0 );
	if ( captureFrame && R_PBRProbes_SettleRequested() ) {
		R_ModernSpecularProbeAtlas_RefreshCounts();
		if ( rg_modernSpecularProbeAtlasStats.prefilterRunning == 0 ) {
			R_PBRProbes_SettleDone();
		}
	}
	if ( !captureFrame ) {
		// A screenshot publishes everything at once by design; it is not a
		// gameplay frame and stays out of the frame-cost maximum.
		rg_modernSpecularProbeAtlasStats.frameFilterMaxUs =
			Max( rg_modernSpecularProbeAtlasStats.frameFilterMaxUs, flushSpent );
	}
	R_ModernSpecularProbeAtlas_RefreshCounts();
	bool allLiveUploaded = true;
	for ( int slot = 0; slot < MODERN_SPECULAR_PROBE_ATLAS_MAX_ENTRIES; ++slot ) {
		const modernSpecularProbeAtlasEntry_t &entry =
			rg_modernSpecularProbeAtlasEntries[slot];
		if ( entry.resident
				&& entry.lastUsedFrame == rg_modernSpecularProbeAtlasFrame
				&& ( entry.pendingUpload || !entry.uploaded
					|| !R_ModernSpecularProbeAtlas_FingerprintMatches( entry ) ) ) {
			allLiveUploaded = false;
			break;
		}
	}
	rg_modernSpecularProbeAtlasStats.frameReady =
		rg_modernSpecularProbeAtlasStats.frameLossless && allLiveUploaded;
}

unsigned int R_ModernSpecularProbeAtlas_Texture( void ) {
	return rg_modernSpecularProbeAtlasTexture;
}

bool R_ModernSpecularProbeAtlas_Ready( void ) {
	return rg_modernSpecularProbeAtlasInitialized;
}

bool R_ModernSpecularProbeAtlas_FrameReady( void ) {
	if ( !rg_modernSpecularProbeAtlasStats.frameReady ) {
		return false;
	}
	for ( int slot = 0; slot < MODERN_SPECULAR_PROBE_ATLAS_MAX_ENTRIES; ++slot ) {
		const modernSpecularProbeAtlasEntry_t &entry =
			rg_modernSpecularProbeAtlasEntries[slot];
		if ( entry.resident
				&& entry.lastUsedFrame == rg_modernSpecularProbeAtlasFrame
				&& !R_ModernSpecularProbeAtlas_FingerprintMatches( entry ) ) {
			R_ModernSpecularProbeAtlas_RecordReject(
				MODERN_SPECULAR_PROBE_ATLAS_REJECT_SOURCE_CHANGED );
			return false;
		}
	}
	return true;
}

const modernSpecularProbeAtlasStats_t &R_ModernSpecularProbeAtlas_Stats( void ) {
	return rg_modernSpecularProbeAtlasStats;
}

void R_ModernSpecularProbeAtlas_PrintGfxInfo( void ) {
	const modernSpecularProbeAtlasStats_t &stats =
		rg_modernSpecularProbeAtlasStats;
	common->Printf(
		"Modern specular probe atlas: %s lastReject=%s available=%d init=%d texture=%d frameLossless=%d frameReady=%d size=%d face=%d capacity=%d resident=%d live=%d pending=%d generation=%llu/%llu acquires=%d hits=%d uploads=%d/%d reloads=%d evictions=%d rejected(output=%d null=%d unavailable=%d notLoaded=%d defaulted=%d mutable=%d notCube=%d nonSquare=%d oversized=%d storage=%d full=%d changed=%d upload=%d) prefilterAsync=%d prefilterStarted=%u prefilterPublished=%u prefilterRunning=%u prefilterCancelled=%u prefilterWaits=%u prefilterLatencyMeanUs=%llu prefilterLatencyMaxUs=%llu frameFilterMeanUs=%llu frameFilterMaxUs=%llu sourceReads=%u sourceReadMeanUs=%llu sourceReadMaxUs=%llu\n",
		stats.status,
		stats.lastReject,
		stats.available ? 1 : 0,
		stats.initialized ? 1 : 0,
		stats.textureReady ? 1 : 0,
		stats.frameLossless ? 1 : 0,
		stats.frameReady ? 1 : 0,
		stats.atlasSize,
		stats.faceSize,
		stats.capacity,
		stats.residentEntries,
		stats.liveEntries,
		stats.pendingEntries,
		static_cast<unsigned long long>( stats.atlasGeneration ),
		static_cast<unsigned long long>( stats.frameGeneration ),
		stats.acquires,
		stats.cacheHits,
		stats.uploadedEntries,
		stats.uploadedFaces,
		stats.reloads,
		stats.evictions,
		stats.rejectedNullOutput,
		stats.rejectedNullImage,
		stats.rejectedUnavailable,
		stats.rejectedNotLoaded,
		stats.rejectedDefaulted,
		stats.rejectedMutable,
		stats.rejectedNotCube,
		stats.rejectedNonSquare,
		stats.rejectedOversized,
		stats.rejectedInvalidStorage,
		stats.rejectedAtlasFull,
		stats.rejectedSourceChanged,
		stats.rejectedUploadUnavailable,
		r_pbrProbeAsyncPrefilter.GetBool() ? 1 : 0,
		stats.prefilterStarted,
		stats.prefilterPublished,
		stats.prefilterRunning,
		stats.prefilterCancelled,
		stats.prefilterWaits,
		stats.prefilterPublished > 0 ? stats.prefilterLatencyTotalUs / stats.prefilterPublished : 0ull,
		stats.prefilterLatencyMaxUs,
		stats.frameFilterCount > 0 ? stats.frameFilterTotalUs / stats.frameFilterCount : 0ull,
		stats.frameFilterMaxUs,
		stats.sourceReads,
		stats.sourceReads > 0 ? stats.sourceReadTotalUs / stats.sourceReads : 0ull,
		stats.sourceReadMaxUs );
}

/*
===================
RendererSpecularProbeAtlas_RunSelfTest

Exercises only deterministic CPU contracts: fixed face order, texel-centre
rectangles, disjoint bounded placements, stable LRU tie-breaking, and non-zero
generation rollover. No image, renderer resource, or GL context is required.
===================
*/
static bool R_ModernSpecularProbeAtlas_TransferSelfTest( void ) {
	if ( !glConfig.backendCaps.contextCreated || !glConfig.renderFeatures.modernBaseline ) { return true; }
	// No texture readback (OpenGL ES): nothing to round-trip, and the atlas
	// itself reports entry-points-missing on such a context.
	if ( glGetTexImage == NULL ) { return true; }
	idGLPixelTransferScope restoreCaller;
	GLuint texture = 0, buffers[2] = { 0, 0 };
	glGenTextures( 1, &texture );
	glGenBuffers( 2, buffers );
	if ( texture == 0 || buffers[0] == 0 || buffers[1] == 0 ) {
		glDeleteBuffers( 2, buffers );
		glDeleteTextures( 1, &texture );
		return false;
	}
	R_GLStateCache().BindBuffer( GL_PIXEL_PACK_BUFFER, buffers[0] );
	R_GLStateCache().BindBuffer( GL_PIXEL_UNPACK_BUFFER, buffers[1] );
	GLint poison[idGLPixelTransferScope::NUM_STORES];
	for ( int i = 0; i < idGLPixelTransferScope::NUM_STORES; ++i ) {
		poison[i] = idGLPixelTransferScope::IsAlignment( i ) ? 8 : ( i % 8 >= 6 ? 1 : 7 );
		glPixelStorei( idGLPixelTransferScope::StoreName( i ), poison[i] );
	}
	R_GLStateCache().ActiveTextureUnit( 3 );
	const float expected[16] = { 0.125f, 2, 4, 1, 0.25f, 3, 8, 1, 0.5f, 4, 16, 1, 1, 5, 32, 1 };
	float actual[16] = {};
	{
		idGLPixelTransferScope transfer;
		R_GLStateCache().BindTexture( 0, GL_TEXTURE_2D, texture );
		glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA16F, 2, 2, 0, GL_RGBA, GL_FLOAT, expected );
		glGetTexImage( GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT, actual );
	}
	bool ok = true;
	for ( int i = 0; i < idGLPixelTransferScope::NUM_STORES; ++i ) {
		GLint state = 0;
		glGetIntegerv( idGLPixelTransferScope::StoreName( i ), &state );
		ok = ok && state == poison[i];
	}
	for ( int i = 0; i < 16; ++i ) {
		ok = ok && actual[i] == expected[i];
	}
	GLint pack = 0, unpack = 0, active = 0;
	glGetIntegerv( GL_PIXEL_PACK_BUFFER_BINDING, &pack );
	glGetIntegerv( GL_PIXEL_UNPACK_BUFFER_BINDING, &unpack );
	glGetIntegerv( GL_ACTIVE_TEXTURE, &active );
	ok = ok && pack == buffers[0] && unpack == buffers[1] && active == GL_TEXTURE3;
	R_GLStateCache().BindBuffer( GL_PIXEL_PACK_BUFFER, 0 );
	R_GLStateCache().BindBuffer( GL_PIXEL_UNPACK_BUFFER, 0 );
	glDeleteBuffers( 2, buffers );
	glDeleteTextures( 1, &texture );
	common->Printf( "RendererPixelTransfer self-test %s (HDR upload/readback, 16 poisoned pixel stores, 2 PBOs, active texture restored)\n", ok ? "passed" : "failed" );
	return ok;
}

bool RendererSpecularProbeAtlas_RunSelfTest( void ) {
	if ( !R_ModernSpecularProbeAtlas_TransferSelfTest() ) { return false; }
	modernSpecularProbeAtlasPlacement_t placements[
		MODERN_SPECULAR_PROBE_ATLAS_MAX_ENTRIES];
	for ( int slot = 0; slot < MODERN_SPECULAR_PROBE_ATLAS_MAX_ENTRIES; ++slot ) {
		if ( !ModernSpecularProbeAtlas_BuildPlacement( slot,
				MODERN_SPECULAR_PROBE_ATLAS_FACE_SIZE, placements[slot] ) ) {
			common->Printf( "RendererSpecularProbeAtlas self-test failed: slot %d did not pack\n", slot );
			return false;
		}
		for ( int face = 0; face < MODERN_SPECULAR_PROBE_ATLAS_FACE_COUNT; ++face ) {
			const int expectedCell =
				slot * MODERN_SPECULAR_PROBE_ATLAS_FACE_COUNT + face;
			const float *rect = placements[slot].faceRects[face];
			if ( placements[slot].faceCells[face] != expectedCell
					|| rect[0] < 0.0f || rect[1] < 0.0f
					|| rect[0] + rect[2] > 1.0f
					|| rect[1] + rect[3] > 1.0f ) {
				common->Printf( "RendererSpecularProbeAtlas self-test failed: slot %d face %d escaped packing\n", slot, face );
				return false;
			}
			for ( int priorSlot = 0; priorSlot <= slot; ++priorSlot ) {
				const int priorFaceLimit = priorSlot == slot ? face :
					MODERN_SPECULAR_PROBE_ATLAS_FACE_COUNT;
				for ( int priorFace = 0; priorFace < priorFaceLimit; ++priorFace ) {
					const float *other = placements[priorSlot].faceRects[priorFace];
					const bool disjoint = rect[0] + rect[2] < other[0]
						|| other[0] + other[2] < rect[0]
						|| rect[1] + rect[3] < other[1]
						|| other[1] + other[3] < rect[1];
					if ( !disjoint ) {
						common->Printf( "RendererSpecularProbeAtlas self-test failed: slot %d face %d overlaps slot %d face %d\n", slot, face, priorSlot, priorFace );
						return false;
					}
				}
			}
		}
	}

	modernSpecularProbeAtlasPlacement_t invalid;
	invalid.valid = true;
	if ( ModernSpecularProbeAtlas_BuildPlacement( -1,
			MODERN_SPECULAR_PROBE_ATLAS_FACE_SIZE, invalid ) || invalid.valid
			|| invalid.faceCells[0] != -1 || invalid.faceRects[0][0] != 0.0f ) {
		common->Printf( "RendererSpecularProbeAtlas self-test failed: invalid placement was not cleared\n" );
		return false;
	}

	modernSpecularProbeAtlasEntry_t lru[
		MODERN_SPECULAR_PROBE_ATLAS_MAX_ENTRIES];
	memset( lru, 0, sizeof( lru ) );
	if ( R_ModernSpecularProbeAtlas_SelectSlot( lru, 10 ) != 0 ) {
		common->Printf( "RendererSpecularProbeAtlas self-test failed: first free slot is not deterministic\n" );
		return false;
	}
	for ( int slot = 0; slot < MODERN_SPECULAR_PROBE_ATLAS_MAX_ENTRIES; ++slot ) {
		lru[slot].resident = true;
		lru[slot].lastUsedFrame = slot < 2 ? 9 : 3;
	}
	if ( R_ModernSpecularProbeAtlas_SelectSlot( lru, 10 ) != 2 ) {
		common->Printf( "RendererSpecularProbeAtlas self-test failed: equal-age LRU did not choose the lowest slot\n" );
		return false;
	}
	lru[2].lastUsedFrame = 10;
	if ( R_ModernSpecularProbeAtlas_SelectSlot( lru, 10 ) != 3 ) {
		common->Printf( "RendererSpecularProbeAtlas self-test failed: a live slot was selected for eviction\n" );
		return false;
	}
	for ( int slot = 0; slot < MODERN_SPECULAR_PROBE_ATLAS_MAX_ENTRIES; ++slot ) {
		lru[slot].lastUsedFrame = 10;
	}
	if ( R_ModernSpecularProbeAtlas_SelectSlot( lru, 10 ) != -1 ) {
		common->Printf( "RendererSpecularProbeAtlas self-test failed: all-live atlas did not fail closed\n" );
		return false;
	}

	std::uint64_t generation = 0;
	if ( R_ModernSpecularProbeAtlas_AdvanceGeneration( generation ) != 1 ) {
		common->Printf( "RendererSpecularProbeAtlas self-test failed: zero generation was published\n" );
		return false;
	}
	generation = ~static_cast<std::uint64_t>( 0 );
	if ( R_ModernSpecularProbeAtlas_AdvanceGeneration( generation ) != 1 ) {
		common->Printf( "RendererSpecularProbeAtlas self-test failed: wrapped generation was zero\n" );
		return false;
	}

	common->Printf(
		"RendererSpecularProbeAtlas self-test passed (entries=%d faces=%d atlas=%d face=%d)\n",
		MODERN_SPECULAR_PROBE_ATLAS_MAX_ENTRIES,
		MODERN_SPECULAR_PROBE_ATLAS_FACE_COUNT,
		MODERN_SPECULAR_PROBE_ATLAS_SIZE,
		MODERN_SPECULAR_PROBE_ATLAS_FACE_SIZE );
	return true;
}
