/*
===========================================================================

Doom 3 BFG Edition GPL Source Code
Copyright (C) 1993-2012 id Software LLC, a ZeniMax Media company. 

This file is part of the Doom 3 BFG Edition GPL Source Code ("Doom 3 BFG Edition Source Code").  

Doom 3 BFG Edition Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Doom 3 BFG Edition Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Doom 3 BFG Edition Source Code.  If not, see <http://www.gnu.org/licenses/>.

In addition, the Doom 3 BFG Edition Source Code is also subject to certain additional terms. You should have received a copy of these additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the Doom 3 BFG Edition Source Code.  If not, please request a copy in writing from id Software at the address below.

If you have questions concerning this license or the applicable additional terms, you may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.

===========================================================================
*/





#include "tr_local.h"
#include "RendererResourceSettings.h"
#include "../imagetools/LegacyScopeMask.h"

#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

bool R_IsMutableRenderImageName( const char *name ) {
	if ( name == NULL || name[0] == '\0' ) {
		return false;
	}
	static const char *prefixes[] = {
		"_cinematic", "_scratch", "_accum",
		"_reflectionRender", "_refractionRender", "_mirrorRender",
		"_currentRender", "_originalCurrentRender", "_currentDepth",
		"_forwardRenderResolved", "_postProcessAlbedo",
		"_scenePreserveDepth", "_hdrScene", "_ssao", "_cel",
		"_motionVector", "_bloom", "_hdrLum", "_underwaterDepth",
		"_shadowMap", "_pointShadowMap",
		"_translucentShadowMap", "_pointTranslucentShadowMap"
	};
	for ( int i = 0; i < static_cast<int>( sizeof( prefixes ) / sizeof( prefixes[0] ) ); ++i ) {
		if ( idStr::Icmpn( name, prefixes[i], static_cast<int>( strlen( prefixes[i] ) ) ) == 0 ) {
			return true;
		}
	}
	return !idStr::Icmp( name, "BlurTexture1" ) || !idStr::Icmp( name, "DepthTexture" );
}

bool R_IsMutableRenderImage( const idImage *image ) {
	return image != NULL && ( image->IsScratchImage() || R_IsMutableRenderImageName( image->GetName() ) );
}

// do this with a pointer, in case we want to make the actual manager
// a private virtual subclass
idImageManager	imageManager;
idImageManager * globalImages = &imageManager;

idCVar preLoad_Images( "preLoad_Images", "1", CVAR_SYSTEM | CVAR_BOOL, "preload images during beginlevelload" );
static const char *image_filterArgs[] = {
	"GL_LINEAR_MIPMAP_LINEAR",
	"GL_LINEAR_MIPMAP_NEAREST",
	"GL_NEAREST",
	"GL_LINEAR",
	"GL_NEAREST_MIPMAP_NEAREST",
	"GL_NEAREST_MIPMAP_LINEAR",
	NULL
};
idCVar image_filter(
	"image_filter",
	"GL_LINEAR_MIPMAP_LINEAR",
	CVAR_RENDERER | CVAR_ARCHIVE,
	"sampling mode for material textures that use the default filter",
	image_filterArgs,
	idCmdSystem::ArgCompletion_String<image_filterArgs> );
idCVar image_anisotropy(
	"image_anisotropy",
	"16",
	CVAR_RENDERER | CVAR_ARCHIVE | CVAR_INTEGER,
	"anisotropic filtering level for mipmapped material textures",
	1,
	16 );

imageFilterState_t R_GetDefaultImageFilterState() {
	static const imageFilterState_t states[ IMAGE_FILTER_MODE_COUNT ] = {
		{ IMAGE_FILTER_LINEAR_MIPMAP_LINEAR, true, true, true, true },
		{ IMAGE_FILTER_LINEAR_MIPMAP_NEAREST, true, true, true, false },
		{ IMAGE_FILTER_NEAREST, false, false, false, false },
		{ IMAGE_FILTER_LINEAR, true, true, false, false },
		{ IMAGE_FILTER_NEAREST_MIPMAP_NEAREST, false, false, true, false },
		{ IMAGE_FILTER_NEAREST_MIPMAP_LINEAR, false, false, true, true }
	};

	const int mode = image_filter.GetInteger();
	if ( mode < 0 || mode >= IMAGE_FILTER_MODE_COUNT ) {
		return states[ IMAGE_FILTER_LINEAR_MIPMAP_LINEAR ];
	}
	return states[ mode ];
}
idCVar image_downSize(
	"image_downSize",
	"0",
	CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL,
	"downsize material textures to image_downSizeLimit" );
idCVar image_downSizeLimit(
	"image_downSizeLimit",
	"0",
	CVAR_RENDERER | CVAR_ARCHIVE | CVAR_INTEGER,
	"maximum material texture dimension when image_downSize is enabled; 0 disables the limit",
	0,
	32768 );
idCVar image_downSizeSpecular(
	"image_downSizeSpecular",
	"0",
	CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL,
	"downsize specular textures to image_downSizeSpecularLimit" );
idCVar image_downSizeSpecularLimit(
	"image_downSizeSpecularLimit",
	"64",
	CVAR_RENDERER | CVAR_ARCHIVE | CVAR_INTEGER,
	"maximum specular texture dimension when image_downSizeSpecular is enabled; 0 disables the limit",
	0,
	32768 );
idCVar image_downSizeBump(
	"image_downSizeBump",
	"0",
	CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL,
	"downsize bump textures to image_downSizeBumpLimit" );
idCVar image_downSizeBumpLimit(
	"image_downSizeBumpLimit",
	"256",
	CVAR_RENDERER | CVAR_ARCHIVE | CVAR_INTEGER,
	"maximum bump texture dimension when image_downSizeBump is enabled; 0 disables the limit",
	0,
	32768 );
idCVar image_ignoreHighQuality(
	"image_ignoreHighQuality",
	"0",
	CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL,
	"ignore material highquality / uncompressed image usage hints" );
// Only does anything where the renderer reports no S3TC, which in practice
// means an OpenGL ES driver that never exposed it -- everywhere else the DXT
// data Quake 4 ships is already being uploaded as-is and is both smaller and
// better than anything re-encoded from it would be.
//
// Rising by usage rather than all at once because each step has a different
// risk: specular is the least visually sensitive, diffuse needs the alpha split
// decided against real materials, and bump wants EAC_RG11 plus a shader change
// that does not exist yet.
idCVar image_useETC2(
	"image_useETC2",
	"0",
	CVAR_RENDERER | CVAR_ARCHIVE | CVAR_INTEGER,
	"compress textures to ETC2 when the driver exposes no S3TC:\n 0: off, keep uncompressed RGBA8\n 1: specular only\n 2: specular and diffuse\n 3: specular, diffuse and bump (bump as EAC_RG11)",
	0,
	3 );
idCVar image_picmip(
	"image_picmip",
	"0",
	CVAR_RENDERER | CVAR_ARCHIVE | CVAR_INTEGER,
	"reduce the diffuse layer of materials by this many mip levels; 0 = full resolution, each step halves the texture. Normal, specular, light, sky, font, and 2D images keep their authored size",
	0,
	MAX_IMAGE_PICMIP );
idCVar image_picmipFilter(
	"image_picmipFilter",
	"1",
	CVAR_RENDERER | CVAR_ARCHIVE | CVAR_INTEGER,
	"image paths image_picmip may reduce:\n 0: every diffuse texture\n 1: textures/* (world surfaces)\n 2: models/* (characters, weapons, props)\n 4: any other namespace\nAdd values to combine categories.",
	0,
	PICMIP_FILTER_MASK );
idCVar image_picmipMinSize(
	"image_picmipMinSize",
	"32",
	CVAR_RENDERER | CVAR_ARCHIVE | CVAR_INTEGER,
	"image_picmip stops reducing a texture once its longest axis reaches this size",
	1,
	4096 );

/*
===============
R_ImagePicmipFilterAllows

image_picmip is scoped by namespace the same way Quake 3 style picmip filters
scope shader paths. openQ4 classifies the image rather than the material because
one idImage is shared by every material that names it, and because the diffuse
usage check already keeps GUI, font, light, and sky images out of range.
===============
*/
static bool R_ImagePathStartsWith( const char *name, const char *dir, int dirLength ) {
	return idStr::Icmpn( name, dir, dirLength ) == 0 && ( name[dirLength] == '/' || name[dirLength] == '\\' );
}

// Spelled out rather than deferred to <ctype.h>, whose classifiers are undefined
// for bytes above 0x7f under a signed char. Image names are matched as plain
// ASCII everywhere else in the pipeline.
static bool R_IsImageProgramNameChar( char c ) {
	return ( c >= 'a' && c <= 'z' ) || ( c >= 'A' && c <= 'Z' ) || ( c >= '0' && c <= '9' ) || c == '_';
}

static bool R_ImagePicmipFilterAllows( const char *name, int value ) {
	const int filter = value & PICMIP_FILTER_MASK;
	if ( filter == PICMIP_FILTER_ALL ) {
		return true;
	}
	if ( name == NULL || name[0] == '\0' ) {
		return false;
	}

	// An image program names its subject as the first operand, so unwrap the
	// leading function calls: addnormals( textures/a, heightmap( textures/b, 4 ) )
	// is classified as textures/a. Scanning to the last '(' or ',' instead would
	// land on a numeric argument and misfile the whole image.
	const char *path = name;
	for ( ;; ) {
		const char *probe = path;
		while ( R_IsImageProgramNameChar( *probe ) ) {
			probe++;
		}
		if ( probe == path || *probe != '(' ) {
			break;
		}
		path = probe + 1;
		while ( *path == ' ' || *path == '\t' ) {
			path++;
		}
	}

	if ( R_ImagePathStartsWith( path, "textures", 8 ) ) {
		return ( filter & PICMIP_FILTER_TEXTURES ) != 0;
	}
	if ( R_ImagePathStartsWith( path, "models", 6 ) ) {
		return ( filter & PICMIP_FILTER_MODELS ) != 0;
	}
	return ( filter & PICMIP_FILTER_OTHER ) != 0;
}

/*
===============
R_GetImageDownsizePolicy

The one place the image reduction cvars are turned into a policy. Every loader
path resolves its target size through this, and the generated .bimage cache key
is derived from it, so a cvar change always produces a cache miss rather than a
stale texture at the previous size.
===============
*/
imageDownsizeInputs_t R_ReadImageDownsizeInputs() {
    return {image_downSize.GetInteger(), image_downSizeLimit.GetInteger(),
        image_downSizeSpecular.GetInteger(), image_downSizeSpecularLimit.GetInteger(),
        image_downSizeBump.GetInteger(), image_downSizeBumpLimit.GetInteger(),
        image_picmip.GetInteger(), image_picmipFilter.GetInteger(), image_picmipMinSize.GetInteger()};
}
void R_GetImageDownsizePolicy( const char* name, textureUsage_t usage, bool allowDownSize, imageDownsizePolicy_t& policy ) {
    const imageDownsizeInputs_t inputs = R_ReadImageDownsizeInputs();
    R_ResolveImageDownsizePolicy(inputs, name, usage, allowDownSize, policy);
}
void R_ResolveImageDownsizePolicy( const imageDownsizeInputs_t& inputs, const char *name, textureUsage_t usage, bool allowDownSize, imageDownsizePolicy_t &policy ) {
	policy = imageDownsizePolicy_t();

	// 'nopicmip' materials and the presentation namespaces opt out entirely
	if ( !allowDownSize ) {
		return;
	}

	if ( usage == TD_SPECULAR && inputs.downSizeSpecular != 0 ) {
		policy.maxDimension = inputs.downSizeSpecularLimit;
	} else if ( usage == TD_BUMP && inputs.downSizeBump != 0 ) {
		policy.maxDimension = inputs.downSizeBumpLimit;
	} else if ( inputs.downSize != 0 ) {
		policy.maxDimension = inputs.downSizeLimit;
	}
	if ( policy.maxDimension < 0 ) {
		policy.maxDimension = 0;
	}

	// picmip is deliberately narrower than the downsize limits: it only touches
	// the diffuse layer, so bump and specular detail, lighting, sky, decals, and
	// every 2D surface keep their authored resolution.
	if ( usage == TD_DIFFUSE && R_ImagePicmipFilterAllows( name, inputs.picmipFilter ) ) {
		policy.mipShift = Max( 0, inputs.picmip );
	}
	policy.minDimension = Max( 1, inputs.picmipMinSize );
}

/*
===============
R_ImageReductionCvarsChanged

One list, consumed by both the startup priming and the per-frame check so the
two can never drift apart.
===============
*/
static bool R_ImageReductionCvarsChanged( bool clear ) {
	idCVar *const reductionCvars[] = {
		&image_picmip, &image_picmipFilter, &image_picmipMinSize,
		&image_downSize, &image_downSizeLimit,
		&image_downSizeSpecular, &image_downSizeSpecularLimit,
		&image_downSizeBump, &image_downSizeBumpLimit
	};

	bool changed = false;
	for ( int i = 0; i < static_cast<int>( sizeof( reductionCvars ) / sizeof( reductionCvars[0] ) ); i++ ) {
		if ( reductionCvars[i]->IsModified() ) {
			changed = true;
			if ( clear ) {
				reductionCvars[i]->ClearModified();
			}
		}
	}

	return changed;
}

static bool R_ImageSamplerCvarsChanged( bool clear ) {
	idCVar *const samplerCvars[] = { &image_filter, &image_anisotropy };
	bool changed = false;
	for ( int i = 0; i < static_cast<int>( sizeof( samplerCvars ) / sizeof( samplerCvars[0] ) ); i++ ) {
		if ( samplerCvars[i]->IsModified() ) {
			changed = true;
			if ( clear ) {
				samplerCvars[i]->ClearModified();
			}
		}
	}
	return changed;
}

/*
===============
idImageManager::PrimeCvars

Every cvar is born CVAR_MODIFIED, so without this the first rendered frame of
every launch would see nine "changed" reduction cvars and reload every image for
nothing. Sampling cvars need the same startup priming so they do not pointlessly
reapply every default sampler. Called once the intrinsic images exist and the
real values are in.
===============
*/
void idImageManager::PrimeCvars() {
	R_ImageReductionCvarsChanged( true );
	R_ImageSamplerCvarsChanged( true );
}

/*
===============
idImageManager::CheckCvars

Image reduction cvars change the pixels a texture is built from, so they take
effect through a reload. Sampling cvars only reapply loaded TF_DEFAULT samplers.
Doing both here makes console changes visible without a vid_restart.
===============
*/
void idImageManager::CheckCvars() {
	const bool reductionChanged = R_ImageReductionCvarsChanged( true );
	const bool samplerChanged = R_ImageSamplerCvarsChanged( true );
	if ( !reductionChanged && !samplerChanged ) {
		return;
	}

	// vid_restart already purges and reloads everything, and a level load is
	// about to touch every image anyway, so do not duplicate that work here.
	if ( insideLevelLoad ) {
		return;
	}

	if ( reductionChanged ) {
		common->Printf( "Texture reduction changed, reloading images...\n" );
		// Only file-backed images are built from the reduction cvars. Reloading
		// the rest reallocated every generated TTF glyph atlas with no texels,
		// and nothing re-rasterises them without a vid_restart, so all menu and
		// HUD text disappeared the moment a video quality preset moved
		// image_downSize/image_downSizeBump/image_downSizeSpecular.
		ReloadImages( true, true );
		return;
	}

	common->Printf( "Texture sampling changed, updating samplers...\n" );
	for ( int i = 0; i < images.Num(); i++ ) {
		idImage *image = images[i];
		if ( image != NULL && image->IsLoaded() && image->GetFilter() == TF_DEFAULT ) {
			image->RefreshSamplerState();
		}
	}
}

static void R_NormalizeInternalImageName( idStr& name ) {
	// Runtime render targets are referenced from materials with option hashes
	// (for example _postProcessAlbedo0#__0200). Treat those as the same image.
	if ( name.IsEmpty() || name[0] != '_' ) {
		return;
	}

	const int optionsPos = name.Find( "#__" );
	if ( optionsPos >= 0 ) {
		name.CapLength( optionsPos );
	}
}

static bool R_IsQ4LightImageNamespace( const char *name ) {
	return name != NULL
		&& ( idStr::Icmpn( name, "lights/", 7 ) == 0
			|| idStr::Icmpn( name, "gfx/lights/", 11 ) == 0 );
}

static textureUsage_t R_ImageUsageForName( const char *name, textureUsage_t requestedUsage ) {
	// Namespace remaps are a legacy convenience for generic callers only.
	// Explicit PBR and high-quality usage classes are cache/storage contracts
	// and must survive regardless of the source path chosen by an author.
	if ( requestedUsage != TD_DEFAULT ) {
		return requestedUsage;
	}
	if ( R_IsLegacyScopeDistortionMask( name ) ) {
		return TD_HIGH_QUALITY;
	}
	if ( idStr::Icmpn( name, "fonts", 5 ) == 0 || idStr::Icmpn( name, "newfonts", 8 ) == 0 ) {
		return TD_FONT;
	}
	if ( R_IsQ4LightImageNamespace( name ) ) {
		return TD_LIGHT;
	}
	return requestedUsage;
}

static bool R_IsQ4PresentationImageNamespace( const char *name ) {
	return name != NULL
		&& ( idStr::Icmpn( name, "gfx/guis/", 9 ) == 0
			|| idStr::Icmpn( name, "fonts", 5 ) == 0
			|| idStr::Icmpn( name, "newfonts", 8 ) == 0 );
}

static bool R_AllowImageDownSizeForName( const char *name, bool allowDownSize ) {
	if ( !allowDownSize ) {
		return false;
	}

	return !R_IsQ4PresentationImageNamespace( name );
}

/*
===============
R_ReloadImages_f

Regenerate all images that came directly from files that have changed, so
any saved changes will show up in place.

New r_texturesize/r_texturedepth variables will take effect on reload

reloadImages <all>
===============
*/
void R_ReloadImages_f( const idCmdArgs &args ) {
	bool all = false;

	if ( args.Argc() == 2 ) {
		if ( !idStr::Icmp( args.Argv(1), "all" ) ) {
			all = true;
		} else {
			common->Printf( "USAGE: reloadImages <all>\n" );
			return;
		}
	}

	globalImages->ReloadImages( all );
}

static void R_ImageDDSSelfTest_f( const idCmdArgs &args ) {
	(void)args;
	if ( !R_ImageDDS_RunSelfTest() ) {
		common->Warning( "Image DDS self-test failed" );
	}
}

typedef struct {
	idImage	*image;
	int		size;
	int		index;
} sortedImage_t;

/*
=======================
R_QsortImageSizes

=======================
*/
static int R_QsortImageSizes( const void *a, const void *b ) {
	const sortedImage_t	*ea, *eb;

	ea = (sortedImage_t *)a;
	eb = (sortedImage_t *)b;

	if ( ea->size > eb->size ) {
		return -1;
	}
	if ( ea->size < eb->size ) {
		return 1;
	}
	return idStr::Icmp( ea->image->GetName(), eb->image->GetName() );
}

/*
====================================================================

Level image prefetch

A level load reads ~1,700 images one after another, and for most of them the
whole cost of the read is inflating a precompressed DDS out of its pk4 and
fingerprinting it. While the main thread uploads image N, worker threads read
and fingerprint the DDS files of the next images. The main thread still opens
every file (the filesystem's search state is not thread-safe) and allocates
every buffer from this module's heap; a worker only reads its own open file
into its own buffer. R_LoadPrecompressedDDS takes a finished buffer through
R_SetDDSPrefetchSource, and an image that turns out not to read its predicted
file just releases it.

====================================================================
*/

idCVar image_prefetchLevelImages( "image_prefetchLevelImages", "1", CVAR_RENDERER | CVAR_BOOL,
	"read and fingerprint a level's precompressed textures on worker threads while earlier ones upload" );

namespace {

static const int LEVEL_IMAGE_PREFETCH_MAX_OPEN = 32;
static const int64 LEVEL_IMAGE_PREFETCH_MAX_BYTES = 64ll << 20;
static const int LEVEL_IMAGE_PREFETCH_MAX_FILE_BYTES = 64 << 20;
static const int LEVEL_IMAGE_PREFETCH_MAX_WORKERS = 4;
static const int LEVEL_IMAGE_PREFETCH_MIN_FILE_BYTES = 128;	// a DDS header

struct levelImagePrefetchEntry_t {
	idStr				qpath;		// empty when the image reads no direct DDS
	idFile *			file = NULL;
	byte *				buffer = NULL;
	int					bytes = 0;
	ID_TIME_T			timestamp = FILE_NOT_FOUND_TIMESTAMP;
	imageFileContent_t	content{};
	bool				contentObserved = false;
	bool				readOk = false;
	bool				opened = false;
	bool				queued = false;		// waiting in the queue, not yet started
	bool				done = false;
	bool				settled = false;	// taken or released by the main thread
};

class idLevelImagePrefetch {
public:
	~idLevelImagePrefetch() { End(); }

	void Begin( const idList<sortedImage_t> &images ) {
		entries.resize( static_cast<size_t>( images.Num() ) );
		for ( int i = 0; i < images.Num(); ++i ) {
			images[ i ].image->PredictDirectDDSPayload( entries[ static_cast<size_t>( i ) ].qpath );
		}
		const unsigned int hardware = std::thread::hardware_concurrency();
		const int workerCount = idMath::ClampInt( 1, LEVEL_IMAGE_PREFETCH_MAX_WORKERS,
			hardware > 1 ? static_cast<int>( hardware ) - 1 : 1 );
		stopping = false;
		for ( int i = 0; i < workerCount; ++i ) {
			workers.emplace_back( &idLevelImagePrefetch::Work, this );
		}
		active = this;
		R_SetDDSPrefetchSource( &idLevelImagePrefetch::TakeHook );
	}

	// Main thread: open and queue the files of the next images, within the
	// open-file and byte budgets.
	void Pump( int consumeIndex ) {
		current = consumeIndex;
		if ( nextToOpen < consumeIndex ) {
			nextToOpen = consumeIndex;
		}
		while ( nextToOpen < static_cast<int>( entries.size() )
				&& nextToOpen - consumeIndex < LEVEL_IMAGE_PREFETCH_MAX_OPEN
				&& outstandingBytes < LEVEL_IMAGE_PREFETCH_MAX_BYTES ) {
			levelImagePrefetchEntry_t &entry = entries[ static_cast<size_t>( nextToOpen++ ) ];
			if ( entry.qpath.Length() == 0 ) {
				continue;
			}
			idFile *file = fileSystem->OpenFileRead( entry.qpath.c_str() );
			if ( file == NULL ) {
				continue;
			}
			const int bytes = file->Length();
			if ( bytes < LEVEL_IMAGE_PREFETCH_MIN_FILE_BYTES || bytes > LEVEL_IMAGE_PREFETCH_MAX_FILE_BYTES ) {
				fileSystem->CloseFile( file );
				continue;
			}
			entry.file = file;
			entry.bytes = bytes;
			entry.timestamp = file->ContentStamp();
			entry.buffer = static_cast<byte *>( Mem_Alloc( bytes ) );
			entry.opened = true;
			outstandingBytes += bytes;
			std::lock_guard<std::mutex> lock( mutex );
			entry.queued = true;
			queue.push_back( &entry );
			workReady.notify_one();
		}
	}

	// Main thread: an image finished loading; drop its file if it was not taken.
	void Retire( int index ) {
		levelImagePrefetchEntry_t &entry = entries[ static_cast<size_t>( index ) ];
		if ( entry.opened && !entry.settled ) {
			Settle( entry, false );
			Mem_Free( entry.buffer );
			entry.buffer = NULL;
		}
		current = -1;
	}

	void End() {
		if ( active == this ) {
			R_SetDDSPrefetchSource( NULL );
			active = NULL;
		}
		{
			std::lock_guard<std::mutex> lock( mutex );
			stopping = true;
			for ( levelImagePrefetchEntry_t *entry : queue ) {
				entry->queued = false;
				entry->done = true;	// never started: nothing read
			}
			queue.clear();
		}
		workReady.notify_all();
		for ( std::thread &worker : workers ) {
			worker.join();
		}
		workers.clear();
		for ( levelImagePrefetchEntry_t &entry : entries ) {
			if ( entry.opened && !entry.settled ) {
				Settle( entry, false );
				Mem_Free( entry.buffer );
				entry.buffer = NULL;
			}
		}
		entries.clear();
	}

	int Taken() const { return taken; }
	int Opened() const { return settledCount; }

private:
	static bool TakeHook( const char *qpath, byte **buffer, int *bytes, ID_TIME_T *timestamp,
			imageFileContent_t *content, bool *contentObserved ) {
		return active != NULL && active->Take( qpath, buffer, bytes, timestamp, content, contentObserved );
	}

	bool Take( const char *qpath, byte **buffer, int *bytes, ID_TIME_T *timestamp,
			imageFileContent_t *content, bool *contentObserved ) {
		if ( current < 0 || current >= static_cast<int>( entries.size() ) ) {
			return false;
		}
		levelImagePrefetchEntry_t &entry = entries[ static_cast<size_t>( current ) ];
		if ( !entry.opened || entry.settled || entry.qpath.Cmp( qpath ) != 0 ) {
			return false;
		}
		Settle( entry, true );
		if ( !entry.readOk ) {
			Mem_Free( entry.buffer );
			entry.buffer = NULL;
			return false;
		}
		*buffer = entry.buffer;
		*bytes = entry.bytes;
		if ( timestamp != NULL ) {
			*timestamp = entry.timestamp;
		}
		*content = entry.content;
		*contentObserved = entry.contentObserved;
		entry.buffer = NULL;
		taken++;
		return true;
	}

	// Main thread: finish the entry, then close its file. A file no worker has
	// started is read here when its data is wanted, and otherwise dropped.
	void Settle( levelImagePrefetchEntry_t &entry, bool wantData ) {
		bool readHere = false;
		{
			std::unique_lock<std::mutex> lock( mutex );
			if ( entry.queued ) {
				for ( auto it = queue.begin(); it != queue.end(); ++it ) {
					if ( *it == &entry ) {
						queue.erase( it );
						break;
					}
				}
				entry.queued = false;
				entry.done = true;
				readHere = wantData;
			} else {
				workDone.wait( lock, [&entry] { return entry.done; } );
			}
		}
		if ( readHere ) {
			Read( entry );
		}
		fileSystem->CloseFile( entry.file );
		entry.file = NULL;
		entry.settled = true;
		outstandingBytes -= entry.bytes;
		settledCount++;
	}

	// Touches only the entry's own file and buffer, and reads its path without
	// copying it, so it is safe on a worker.
	static void Read( levelImagePrefetchEntry_t &entry ) {
		entry.readOk = entry.file->Read( entry.buffer, entry.bytes ) == entry.bytes;
		if ( entry.readOk ) {
			entry.contentObserved = R_MakeImageFileContent( IFC_DIRECT_DDS, entry.qpath.c_str(),
				entry.buffer, static_cast<std::size_t>( entry.bytes ), entry.content );
		}
	}

	void Work() {
		for ( ;; ) {
			levelImagePrefetchEntry_t *entry = NULL;
			{
				std::unique_lock<std::mutex> lock( mutex );
				workReady.wait( lock, [this] { return stopping || !queue.empty(); } );
				if ( queue.empty() ) {
					return;
				}
				entry = queue.front();
				queue.pop_front();
				entry->queued = false;
			}
			// Until done is published under the lock the main thread does not
			// touch this entry (Settle waits on done), so no copy is needed.
			Read( *entry );
			{
				std::lock_guard<std::mutex> lock( mutex );
				entry->done = true;
			}
			workDone.notify_all();
		}
	}

	static idLevelImagePrefetch *active;
	std::vector<levelImagePrefetchEntry_t> entries;
	std::vector<std::thread> workers;
	std::deque<levelImagePrefetchEntry_t *> queue;
	std::mutex mutex;
	std::condition_variable workReady;
	std::condition_variable workDone;
	bool stopping = false;
	int current = -1;
	int nextToOpen = 0;
	int64 outstandingBytes = 0;
	int taken = 0;
	int settledCount = 0;
};

idLevelImagePrefetch *idLevelImagePrefetch::active = NULL;

} // namespace

/*
=======================
R_QsortImageName

=======================
*/
static int R_QsortImageName( const void* a, const void* b ) {
	const sortedImage_t	*ea, *eb;

	ea = (sortedImage_t *)a;
	eb = (sortedImage_t *)b;

	const int nameCompare = idStr::Icmp( ea->image->GetName(), eb->image->GetName() );
	if ( nameCompare != 0 ) {
		return nameCompare;
	}
	return ea->index - eb->index;
}

/*
===============
R_ListImages_f
===============
*/
void R_ListImages_f( const idCmdArgs &args ) {
	int		i, partialSize;
	idImage	*image;
	int		totalSize;
	int		count = 0;
	bool	uncompressedOnly = false;
	bool	unloaded = false;
	bool	failed = false;
	bool	sorted = false;
	bool	duplicated = false;
	bool	overSized = false;
	bool	sortByName = false;

	if ( args.Argc() == 1 ) {

	} else if ( args.Argc() == 2 ) {
		if ( idStr::Icmp( args.Argv( 1 ), "uncompressed" ) == 0 ) {
			uncompressedOnly = true;
		} else if ( idStr::Icmp( args.Argv( 1 ), "sorted" ) == 0 ) {
			sorted = true;
		} else if ( idStr::Icmp( args.Argv( 1 ), "namesort" ) == 0 ) {
			sortByName = true;
		} else if ( idStr::Icmp( args.Argv( 1 ), "unloaded" ) == 0 ) {
			unloaded = true;
		} else if ( idStr::Icmp( args.Argv( 1 ), "duplicated" ) == 0 ) {
			duplicated = true;
		} else if ( idStr::Icmp( args.Argv( 1 ), "oversized" ) == 0 ) {
			sorted = true;
			overSized = true;
		} else {
			failed = true;
		}
	} else {
		failed = true;
	}

	if ( failed ) {
		common->Printf( "usage: listImages [ sorted | namesort | unloaded | duplicated | showOverSized ]\n" );
		return;
	}

	const char *header = "       -w-- -h-- filt -fmt-- wrap  use  size --name-------\n";
	common->Printf( "\n%s", header );

	totalSize = 0;

	idList<sortedImage_t> sortedImages;
	sortedImage_t *sortedArray = NULL;
	if ( sorted || sortByName ) {
		sortedImages.SetNum( globalImages->images.Num() );
		sortedArray = sortedImages.Ptr();
	}

	for ( i = 0 ; i < globalImages->images.Num() ; i++ ) {
		image = globalImages->images[ i ];

		if ( uncompressedOnly ) {
			if ( image->IsCompressed() ) {
				continue;
			}
		}
		if ( unloaded == image->IsLoaded() ) {
			continue;
		}

		// only print duplicates (from mismatched wrap / clamp, etc)
		if ( duplicated ) {
			int j;
			for ( j = i+1 ; j < globalImages->images.Num() ; j++ ) {
				if ( idStr::Icmp( image->GetName(), globalImages->images[ j ]->GetName() ) == 0 ) {
					break;
				}
			}
			if ( j == globalImages->images.Num() ) {
				continue;
			}
		}

		if ( sorted || sortByName ) {
			sortedArray[count].image = image;
			sortedArray[count].size = image->StorageSize();
			sortedArray[count].index = i;
		} else {
			common->Printf( "%4i:",	i );
			image->Print();
		}
		totalSize += image->StorageSize();
		count++;
	}

	if ( sorted || sortByName ) {
		if ( sortByName) {
			qsort( sortedArray, count, sizeof( sortedImage_t ), R_QsortImageName );
		} else {
			qsort( sortedArray, count, sizeof( sortedImage_t ), R_QsortImageSizes );
		}
		partialSize = 0;
		for ( i = 0 ; i < count ; i++ ) {
			common->Printf( "%4i:",	sortedArray[i].index );
			sortedArray[i].image->Print();
			partialSize += sortedArray[i].image->StorageSize();
			if ( ( (i+1) % 10 ) == 0 ) {
				common->Printf( "-------- %5.1f of %5.1f megs --------\n", 
					partialSize / (1024*1024.0), totalSize / (1024*1024.0) );
			}
		}
	}

	common->Printf( "%s", header );
	common->Printf( " %i images (%i total)\n", count, globalImages->images.Num() );
	common->Printf( " %5.1f total megabytes of images\n\n\n", totalSize / (1024*1024.0) );
}

/*
==============
AllocImage

Allocates an idImage, adds it to the list,
copies the name, and adds it to the hash chain.
==============
*/
// Name-only lookups may overlap backend scratch-image creation. Mutating
// callers join before taking this lock, so no waiter can hold it against the
// worker it is collecting.
static std::mutex imageRegistryMutex;

idImage *idImageManager::AllocImage( const char *name ) {
	// the image list and hash are read by a frame on the render thread
	R_RenderThread_Sync( "image" );
	std::lock_guard<std::mutex> registryLock( imageRegistryMutex );
	if (strlen(name) >= MAX_IMAGE_NAME ) {
		common->Error ("idImageManager::AllocImage: \"%s\" is too long\n", name);
	}

	int hash = idStr( name ).FileNameHash();

	idImage * image = new idImage( name );

	imageHash.Add( hash, images.Append( image ) );

	return image;
}

/*
==============
AllocStandaloneImage

Allocates an idImage,does not add it to the list or hash chain

==============
*/
idImage *idImageManager::AllocStandaloneImage( const char *name ) {
	if (strlen(name) >= MAX_IMAGE_NAME ) {
		common->Error ("idImageManager::AllocImage: \"%s\" is too long\n", name);
	}

	idImage * image = new idImage( name );

	return image;
}

/*
==================
ImageFromFunction

Images that are procedurally generated are allways specified
with a callback which must work at any time, allowing the OpenGL
system to be completely regenerated if needed.
==================
*/
idImage *idImageManager::ImageFromFunction( const char *_name, void (*generatorFunction)( idImage *image ) ) {
	R_RenderThread_Sync( "image registry" );

	// strip any .tga file extensions from anywhere in the _name
	idStr name = _name;
	name.Replace( ".tga", "" );
	name.BackSlashesToSlashes();
	R_NormalizeInternalImageName( name );

	// see if the image already exists
	int hash = name.FileNameHash();
	for ( int i = imageHash.First( hash ); i != -1; i = imageHash.Next( i ) ) {
		idImage * image = images[i];
		if ( name.Icmp( image->GetName() ) == 0 ) {
			if ( image->generatorFunction != generatorFunction ) {
				common->DPrintf( "WARNING: reused image %s with mixed generators\n", name.c_str() );
			}
			return image;
		}
	}

	// create the image and issue the callback
	idImage	* image = AllocImage( name );

	image->generatorFunction = generatorFunction;

	// check for precompressed, load is from the front end
	image->referencedOutsideLevelLoad = true;
	image->ActuallyLoadImage( false );

	return image;
}


/*
===============
GetImageWithParameters
==============
*/
idImage	*idImageManager::GetImageWithParameters( const char *_name, textureFilter_t filter, textureRepeat_t repeat, textureUsage_t usage, cubeFiles_t cubeMap, bool allowDownSize, unsigned int flags ) const {
	R_RenderThread_Sync( "image registry" );
	if ( !_name || !_name[0] || idStr::Icmp( _name, "default" ) == 0 || idStr::Icmp( _name, "_default" ) == 0 ) {
		declManager->MediaPrint( "DEFAULTED\n" );
		return globalImages->defaultImage;
	}
	usage = R_ImageUsageForName( _name, usage );
	// strip any .tga file extensions from anywhere in the _name, including image program parameters
	idStr name = _name;
	name.Replace( ".tga", "" );
	name.BackSlashesToSlashes();
	R_NormalizeInternalImageName( name );
	allowDownSize = R_AllowImageDownSizeForName( name.c_str(), allowDownSize );
	int hash = name.FileNameHash();
	for ( int i = imageHash.First( hash ); i != -1; i = imageHash.Next( i ) ) {
		idImage	* image = images[i];
		if ( name.Icmp( image->GetName() ) == 0 ) {
			// the built in's, like _white and _flat always match the other options
			if ( name[0] == '_' ) {
				return image;
			}
			if ( image->cubeFiles != cubeMap ) {
				common->Error( "Image '%s' has been referenced with conflicting cube map states", _name );
			}
			if ( image->filter != filter || image->repeat != repeat ) {
				// we might want to have the system reset these parameters on every bind and
				// share the image data
				continue;
			}
			if ( image->usage != usage || image->allowDownSize != allowDownSize || image->flags != flags ) {
				// If an image is used differently then we need 2 copies of it because usage affects the way it's compressed and swizzled
				continue;
			}
			return image;
		}
	}
	return NULL;
}
/*
===============
ImageFromFile

Finds or loads the given image, always returning a valid image pointer.
Loading of the image may be deferred for dynamic loading.
==============
*/
idImage	*idImageManager::ImageFromFile( const char *_name, textureFilter_t filter, 
						 textureRepeat_t repeat, textureUsage_t usage, cubeFiles_t cubeMap, bool allowDownSize, unsigned int flags ) {
	R_RenderThread_Sync( "image registry" );

	if ( !_name || !_name[0] || idStr::Icmp( _name, "default" ) == 0 || idStr::Icmp( _name, "_default" ) == 0 ) {
		declManager->MediaPrint( "DEFAULTED\n" );
		return globalImages->defaultImage;
	}
	usage = R_ImageUsageForName( _name, usage );
	fileSystem->RecordLevelLoadResource( LEVEL_LOAD_RESOURCE_IMAGE, _name,
		va( "filter=%d;repeat=%d;usage=%d;cube=%d;downsize=%d;flags=%u",
			static_cast<int>( filter ), static_cast<int>( repeat ),
			static_cast<int>( usage ), static_cast<int>( cubeMap ),
			allowDownSize ? 1 : 0, flags ), 0, 1 );

	// strip any .tga file extensions from anywhere in the _name, including image program parameters
	idStr name = _name;
	name.Replace( ".tga", "" );
	name.BackSlashesToSlashes();
	R_NormalizeInternalImageName( name );
	allowDownSize = R_AllowImageDownSizeForName( name.c_str(), allowDownSize );

	//
	// see if the image is already loaded, unless we
	// are in a reloadImages call
	//
	int hash = name.FileNameHash();
	for ( int i = imageHash.First( hash ); i != -1; i = imageHash.Next( i ) ) {
		idImage	* image = images[i];
		if ( name.Icmp( image->GetName() ) == 0 ) {
			// the built in's, like _white and _flat always match the other options
			if ( name[0] == '_' ) {
				return image;
			}
			if ( image->cubeFiles != cubeMap ) {
				common->Error( "Image '%s' has been referenced with conflicting cube map states", _name );
			}

			if ( image->filter != filter || image->repeat != repeat ) {
				// we might want to have the system reset these parameters on every bind and
				// share the image data
				continue;
			}
			if ( image->usage != usage || image->flags != flags ) {
				// If an image is used differently then we need 2 copies of it because usage affects the way it's compressed and swizzled
				continue;
			}

			const bool mergedAllowDownSize = image->allowDownSize && allowDownSize;
			const bool allowDownSizeChanged = image->allowDownSize != mergedAllowDownSize;
			if ( allowDownSizeChanged && !R_ImagePolicyContentMutation() ) return image;
			image->allowDownSize = mergedAllowDownSize;
			image->usage = usage;
			image->levelLoadReferenced = true;

			if ( ( !insideLevelLoad  || preloadingMapImages ) && ( !image->IsLoaded() || allowDownSizeChanged ) ) {
				image->referencedOutsideLevelLoad = ( !insideLevelLoad && !preloadingMapImages );
				image->ActuallyLoadImage( false );	// load is from front end
				declManager->MediaPrint( "%ix%i %s (reload for mixed referneces)\n", image->GetUploadWidth(), image->GetUploadHeight(), image->GetName() );
			}
			return image;
		}
	}

	//
	// create a new image
	//
	idImage	* image = AllocImage( name );
	image->cubeFiles = cubeMap;
	image->usage = usage;
	image->filter = filter;
	image->repeat = repeat;
	image->allowDownSize = allowDownSize;
	image->flags = flags;

	image->levelLoadReferenced = true;

	// load it if we aren't in a level preload
	if ( !insideLevelLoad || preloadingMapImages ) {
		image->referencedOutsideLevelLoad = ( !insideLevelLoad && !preloadingMapImages );
		image->ActuallyLoadImage( false );	// load is from front end
		declManager->MediaPrint( "%ix%i %s\n", image->GetUploadWidth(), image->GetUploadHeight(), image->GetName() );
	} else {
		declManager->MediaPrint( "%s\n", image->GetName() );
	}

	return image;
}

/*
===============
ImageHandleDeferred

Returns an image handle without forcing file IO or level-load residency.
The texture can then be streamed in lazily on first bind.
===============
*/
idImage *idImageManager::ImageHandleDeferred( const char *_name, textureFilter_t filter,
						 textureRepeat_t repeat, textureUsage_t usage, cubeFiles_t cubeMap, bool allowDownSize, unsigned int flags ) {
	R_RenderThread_Sync( "image registry" );

	if ( !_name || !_name[0] || idStr::Icmp( _name, "default" ) == 0 || idStr::Icmp( _name, "_default" ) == 0 ) {
		declManager->MediaPrint( "DEFAULTED\n" );
		return globalImages->defaultImage;
	}
	usage = R_ImageUsageForName( _name, usage );

	idStr name = _name;
	name.Replace( ".tga", "" );
	name.BackSlashesToSlashes();
	R_NormalizeInternalImageName( name );
	allowDownSize = R_AllowImageDownSizeForName( name.c_str(), allowDownSize );

	int hash = name.FileNameHash();
	for ( int i = imageHash.First( hash ); i != -1; i = imageHash.Next( i ) ) {
		idImage *image = images[i];
		if ( name.Icmp( image->GetName() ) == 0 ) {
			if ( name[0] == '_' ) {
				return image;
			}
			if ( image->cubeFiles != cubeMap ) {
				common->Error( "Image '%s' has been referenced with conflicting cube map states", _name );
			}
			if ( image->filter != filter || image->repeat != repeat ) {
				continue;
			}
			if ( image->usage != usage || image->flags != flags ) {
				continue;
			}
			const bool mergedAllowDownSize = image->allowDownSize && allowDownSize;
			if ( image->allowDownSize != mergedAllowDownSize && !R_ImagePolicyContentMutation() ) return image;
			image->allowDownSize = mergedAllowDownSize;
			return image;
		}
	}

	idImage *image = AllocImage( name );
	image->cubeFiles = cubeMap;
	image->usage = usage;
	image->filter = filter;
	image->repeat = repeat;
	image->allowDownSize = allowDownSize;
	image->flags = flags;
	return image;
}

/*
========================
idImageManager::ScratchImage
========================
*/
idImage * idImageManager::ScratchImage( const char *_name, idImageOpts *imgOpts, textureFilter_t filter, textureRepeat_t repeat, textureUsage_t usage ) {
	R_RenderThread_Sync( "image registry" );
	if ( !_name || !_name[0] ) {
		idLib::FatalError( "idImageManager::ScratchImage called with empty name" );
	}

	if ( imgOpts == NULL ) {
		idLib::FatalError( "idImageManager::ScratchImage called with NULL imgOpts" );
	}

	idStr name = _name;
	name.BackSlashesToSlashes();
	R_NormalizeInternalImageName( name );

	//
	// see if the image is already loaded, unless we
	// are in a reloadImages call
	//
	int hash = name.FileNameHash();
	for ( int i = imageHash.First( hash ); i != -1; i = imageHash.Next( i ) ) {
		idImage	* image = images[i];
		if ( name.Icmp( image->GetName() ) == 0 ) {
			if ( ( !image->scratchImage || image->usage != usage ) && !R_ImagePolicyContentMutation() ) return image;
			image->scratchImage = true;
			image->usage = usage;
			image->levelLoadReferenced = true;
			image->referencedOutsideLevelLoad = true;

			if ( image->GetFilter() != filter || image->GetRepeat() != repeat || !( image->GetOpts() == *imgOpts ) || !image->IsLoaded() ) {
				image->AllocImage( *imgOpts, filter, repeat );
			}
			return image;
		}
	}

	// clamp is the only repeat mode that makes sense for cube maps, but
	// some platforms let them stay in repeat mode and get border seam issues
	if ( imgOpts->textureType == TT_CUBIC && repeat != TR_CLAMP ) {
		repeat = TR_CLAMP;
	}

	//
	// create a new image
	//
	idImage* newImage = AllocImage( name );
	if ( newImage != NULL ) {
		newImage->scratchImage = true;
		newImage->usage = usage;
		newImage->levelLoadReferenced = true;
		newImage->referencedOutsideLevelLoad = true;
		newImage->AllocImage( *imgOpts, filter, repeat );
	}
	return newImage;
}

/*
===============
idImageManager::GetImage
===============
*/
idImage *idImageManager::GetImage( const char *_name ) const {
	std::lock_guard<std::mutex> registryLock( imageRegistryMutex );

	if ( !_name || !_name[0] || idStr::Icmp( _name, "default" ) == 0 || idStr::Icmp( _name, "_default" ) == 0 ) {
		declManager->MediaPrint( "DEFAULTED\n" );
		return globalImages->defaultImage;
	}

	// strip any .tga file extensions from anywhere in the _name, including image program parameters
	idStr name = _name;
	name.Replace( ".tga", "" );
	name.BackSlashesToSlashes();
	R_NormalizeInternalImageName( name );

	//
	// look in loaded images
	//
	int hash = name.FileNameHash();
	for ( int i = imageHash.First( hash ); i != -1; i = imageHash.Next( i ) ) {
		idImage * image = images[i];
		if ( name.Icmp( image->GetName() ) == 0 ) {
			return image;
		}
	}

	return NULL;
}

/*
===============
PurgeAllImages
===============
*/
void idImageManager::PurgeAllImages() {
	R_RenderThread_Sync( "image registry" );
	int		i;
	idImage	*image;

	for ( i = 0; i < images.Num() ; i++ ) {
		image = images[i];

		image->PurgeImage();
	}
}

/*
===============
ReloadImages
===============
*/
void idImageManager::ReloadImages( bool all, bool fileBackedOnly ) {
	if ( !R_ImagePolicyOperationAllowed() ) return;
	R_RenderThread_Sync( "image registry" );
	// a reload exists to observe files dropped since the last level load;
	// never let it consult (or leave behind) memoized probe results
	R_SetDDSProbeCacheActive( false );

	for ( int i = 0 ; i < globalImages->images.Num() ; i++ ) {
		idImage *image = globalImages->images[ i ];
		if ( image == NULL ) {
			continue;
		}
		// Scratch pages and render targets are authored in memory, so there is
		// nothing on disk to rebuild them from. idImage::Reload answers a
		// persistent image by reallocating it with no data, which is correct
		// after a device loss and destructive at any other time.
		if ( fileBackedOnly && R_IsMutableRenderImage( image ) ) {
			continue;
		}
		if ( R_ImagePolicyShouldReload( image ) ) image->Reload( all );
	}
}

/*
===============
ReloadGeneratedCacheImages

A generated image cache entry is checked against its source only by a stamp
anyone can copy, so nothing it put on the GPU before a pure lock may stay there
afterwards. Called with the lock already in place: the directory gate then
refuses every generated/images read, and the forced reload derives each image
from the pure paks (and rewrites its cache entry).

Expanded loading backgrounds that builds until 9 November 2026 published under
fs_savepath (guis/assets/generated/loadscreens/, FS_IsGeneratedLoadscreenPath)
are derived data with no source in any pak, and the lock refuses those files
too. One a stray reference loaded is purged rather than reloaded; should
anything bind it again, its load is refused and the image defaults. The
session's own wide loading screen is an in-memory scratch image
(_expandedLoadingBackground_<w>x<h>), composed from reads the lock admits, and
is left alone.
===============
*/
static bool R_IsGeneratedLoadscreenImage( const char *name ) {
	static const char prefix[] = "guis/assets/generated/loadscreens/";
	return name != NULL && idStr::Icmpn( name, prefix, static_cast<int>( sizeof( prefix ) - 1 ) ) == 0;
}

bool idImageManager::ReloadGeneratedCacheImages() {
	if ( !R_ImagePolicyOperationAllowed() ) {
		return false;
	}
	R_RenderThread_Sync( "image registry" );
	// the memoized source probes predate the lock
	R_SetDDSProbeCacheActive( false );

	int reloaded = 0;
	int purged = 0;
	int failed = 0;
	for ( int i = 0; i < images.Num(); i++ ) {
		idImage *image = images[ i ];
		if ( image != NULL && image->IsLoaded() && R_IsGeneratedLoadscreenImage( image->GetName() ) ) {
			image->PurgeImage();
			if ( image->IsLoaded() ) {
				common->Warning( "Could not evict loading background %s", image->GetName() );
				failed++;
			} else {
				purged++;
			}
			continue;
		}
		if ( image == NULL || !image->HoldsGeneratedCacheContent() ) {
			continue;
		}
		image->Reload( true );
		if ( image->HoldsGeneratedCacheContent() ) {
			common->Warning( "Could not reload cached image %s from its source", image->GetName() );
			failed++;
		} else {
			reloaded++;
		}
	}
	common->Printf( "Reloaded %d cached image%s from pure content%s%s\n", reloaded, reloaded == 1 ? "" : "s",
		purged ? va( "; evicted %d loading background%s", purged, purged == 1 ? "" : "s" ) : "",
		failed ? va( "; %d could not be reloaded", failed ) : "" );
	return failed == 0;
}

/*
===============
R_CombineCubeImages_f

Used to combine animations of six separate tga files into
a serials of 6x taller tga files, for preparation to roq compress
===============
*/
void R_CombineCubeImages_f( const idCmdArgs &args ) {
	if ( args.Argc() != 2 ) {
		common->Printf( "usage: combineCubeImages <baseName>\n" );
		common->Printf( " combines basename[1-6][0001-9999].tga to basenameCM[0001-9999].tga\n" );
		common->Printf( " 1: forward 2:right 3:back 4:left 5:up 6:down\n" );
		return;
	}

	idStr	baseName = args.Argv( 1 );
	common->SetRefreshOnPrint( true );

	for ( int frameNum = 1 ; frameNum < 10000 ; frameNum++ ) {
		idStr	filename;
		byte	*pics[6];
		int		width = 0, height = 0;
		int		side;
		int		orderRemap[6] = { 1,3,4,2,5,6 };
		for ( side = 0 ; side < 6 ; side++ ) {
			char suffix[32];
			idStr::snPrintf( suffix, sizeof( suffix ), "%i%04i.tga", orderRemap[side], frameNum );
			filename = baseName;
			filename += suffix;

			common->Printf( "reading %s\n", filename.c_str() );
			R_LoadImage( filename.c_str(), &pics[side], &width, &height, NULL, true );

			if ( !pics[side] ) {
				common->Printf( "not found.\n" );
				break;
			}

			// convert from "camera" images to native cube map images
			switch( side ) {
			case 0:	// forward
				R_RotatePic( pics[side], width);
				break;
			case 1:	// back
				R_RotatePic( pics[side], width);
				R_HorizontalFlip( pics[side], width, height );
				R_VerticalFlip( pics[side], width, height );
				break;
			case 2:	// left
				R_VerticalFlip( pics[side], width, height );
				break;
			case 3:	// right
				R_HorizontalFlip( pics[side], width, height );
				break;
			case 4:	// up
				R_RotatePic( pics[side], width);
				break;
			case 5: // down
				R_RotatePic( pics[side], width);
				break;
			}
		}

		if ( side != 6 ) {
			for ( int i = 0 ; i < side ; i++ ) {
				Mem_Free( pics[i] );
			}
			break;
		}

		if ( width <= 0 || height <= 0 || height > idMath::INT_MAX / 6 ) {
			common->Warning( "combineCubeImages: invalid image dimensions %dx%d", width, height );
			for ( int i = 0 ; i < 6 ; i++ ) {
				Mem_Free( pics[i] );
			}
			break;
		}
		const int combinedHeight = height * 6;
		if ( width > idMath::INT_MAX / combinedHeight || width * combinedHeight > idMath::INT_MAX / 4 ) {
			common->Warning( "combineCubeImages: image dimensions are too large %dx%d", width, height );
			for ( int i = 0 ; i < 6 ; i++ ) {
				Mem_Free( pics[i] );
			}
			break;
		}

		const int combinedBytes = width * combinedHeight * 4;
		idTempArray<byte> buf( combinedBytes );
		byte	*combined = (byte *)buf.Ptr();
		for (  side = 0 ; side < 6 ; side++ ) {
			memcpy( combined+width*height*4*side, pics[side], width*height*4 );
			Mem_Free( pics[side] );
		}
		char suffix[32];
		idStr::snPrintf( suffix, sizeof( suffix ), "CM%04i.tga", frameNum );
		filename = baseName;
		filename += suffix;

		common->Printf( "writing %s\n", filename.c_str() );
		R_WriteTGA( filename.c_str(), combined, width, combinedHeight );
	}
	common->SetRefreshOnPrint( false );
}

/*
===============
UnbindAll
===============
*/
void idImageManager::UnbindAll() {
	int oldTMU = backEnd.glState.currenttmu;
	for ( int i = 1; i < MAX_MULTITEXTURE_UNITS; ++i ) {
		GL_SelectTextureNoClient(i);
		BindNull();
	}
	backEnd.glState.currenttmu = oldTMU;
}

/*
===============
BindNull
===============
*/
void idImageManager::BindNull() {
	tmu_t* tmu;
	if ( backEnd.glState.currenttmu < 0 || backEnd.glState.currenttmu >= MAX_MULTITEXTURE_UNITS ) {
		common->Warning( "idImageManager::BindNull: invalid tracked texture unit %d", backEnd.glState.currenttmu );
		return;
	}

	tmu = &backEnd.glState.tmu[backEnd.glState.currenttmu];

	RB_LogComment("BindNull()\n");
	if (tmu->textureType == TT_CUBIC) {
		glDisable(GL_TEXTURE_CUBE_MAP_EXT);
	}
	else if (tmu->textureType == TT_2D) {
		glDisable(GL_TEXTURE_2D);
	}
	tmu->textureType = TT_DISABLED;

}

/*
===============
Init
===============
*/
void idImageManager::Init() {
	R_ImagePolicyLifecycleChanged();

	images.Resize( 1024, 1024 );
	imageHash.ResizeIndex( 1024 );

	CreateIntrinsicImages();

	cmdSystem->AddCommand( "reloadImages", R_ReloadImages_f, CMD_FL_RENDERER, "reloads images" );
	cmdSystem->AddCommand( "listImages", R_ListImages_f, CMD_FL_RENDERER, "lists images" );
	cmdSystem->AddCommand( "imageDDSSelfTest", R_ImageDDSSelfTest_f, CMD_FL_RENDERER, "validates DDS naming, BC7 metadata handling and DXT3 decoding" );
	cmdSystem->AddCommand( "combineCubeImages", R_CombineCubeImages_f, CMD_FL_RENDERER, "combines six images for roq compression" );

	// swallow the born-modified state of the reduction cvars so the first
	// rendered frame does not reload every image for a change nobody made
	PrimeCvars();

	// should forceLoadImages be here?
}

/*
===============
Shutdown
===============
*/
void idImageManager::Shutdown() {
	R_ImagePolicyLifecycleChanged();
	images.DeleteContents( true );
	imageHash.Clear();

}

/*
====================
idImageManager::BeginLevelLoad
Frees all images used by the previous level
====================
*/
void idImageManager::BeginLevelLoad() {
	R_ImagePolicyLifecycleChanged();
	R_RenderThread_Sync( "image registry" );
	insideLevelLoad = true;

	// search paths are stable for the whole load, so DDS replacement probes
	// can be memoized until EndLevelLoad finishes LoadLevelImages
	R_SetDDSProbeCacheActive( true );
	imageLoadPhaseTimings.Clear();

	for ( int i = 0 ; i < images.Num() ; i++ ) {
		idImage	*image = images[ i ];
		image->ClearUseCount();

		// generator function images are always kept around
		if ( image->generatorFunction || image->GetOpts().isPersistant ) {
			continue;
		}

		if ( !image->referencedOutsideLevelLoad && image->IsLoaded() ) {
			image->PurgeImage();
			//idLib::Printf( "purging %s\n", image->GetName() );
		} else {
			//idLib::Printf( "not purging %s\n", image->GetName() );
		}

		image->levelLoadReferenced = false;
	}
}


/*
====================
idImageManager::ExcludePreloadImage
====================
*/
bool idImageManager::ExcludePreloadImage( const char *name ) {
	idStr imgName = name;
	imgName.ToLower();
	if ( imgName.Find( "newfonts/", false ) >= 0 ) {
		return true;
	}
	if ( imgName.Find( "generated/swf/", false ) >= 0 ) {
		return true;
	}
	if ( imgName.Find( "/loadscreens/", false ) >= 0 ) {
		return true;
	}
	return false;
}

/*
===============
idImageManager::LoadLevelImages
===============
*/
int idImageManager::LoadLevelImages( bool pacifier ) {
	R_RenderThread_Sync( "image registry" );
	idList<sortedImage_t> pendingImages;
	pendingImages.SetNum( CountPendingLevelLoads() );
	const bool profileLevelLoad = cvarSystem->GetCVarBool( "com_showLevelLoadTimes" );
	uint64_t loadedStorageBytes = 0;
	double measuredLoadMsec = 0.0;

	int pendingIndex = 0;
	for ( int i = 0 ; i < images.Num() ; i++ ) {
		idImage	*image = images[ i ];
		if ( image->generatorFunction ) {
			continue;
		}
		if ( image->levelLoadReferenced && !image->IsLoaded() ) {
			if ( pendingIndex >= pendingImages.Num() ) {
				// CountPendingLevelLoads and this loop share a predicate today, so
				// this cannot trip. Keep the bound anyway: the array is pre-sized
				// from that count, and any future divergence would otherwise be a
				// silent heap overflow rather than a dropped image.
				break;
			}
			pendingImages[ pendingIndex ].image = image;
			pendingImages[ pendingIndex ].size = 0;
			pendingImages[ pendingIndex ].index = i;
			pendingIndex++;
		}
	}

	if ( pendingImages.Num() > 1 ) {
		qsort( pendingImages.Ptr(), pendingImages.Num(), sizeof( sortedImage_t ), R_QsortImageName );
	}

	idLevelImagePrefetch prefetch;
	// Without a running renderer ActuallyLoadImage reads nothing, so a dedicated
	// server would read and discard every prefetched file (800-1,000 per stock
	// MP map).
	const bool prefetching = image_prefetchLevelImages.GetBool() && pendingImages.Num() > 1 && tr.IsOpenGLRunning();
	if ( prefetching ) {
		prefetch.Begin( pendingImages );
	}

	for ( int i = 0; i < pendingImages.Num(); i++ ) {
		if ( pacifier ) {
			//common->UpdateLevelLoadPacifier();
		}

		idTimer loadTimer;
		if ( profileLevelLoad ) {
			loadTimer.Start();
		}
		if ( prefetching ) {
			prefetch.Pump( i );
		}
		pendingImages[ i ].image->ActuallyLoadImage( false );
		if ( prefetching ) {
			prefetch.Retire( i );
		}
		if ( profileLevelLoad ) {
			loadTimer.Stop();
			const double imageLoadMsec = loadTimer.Milliseconds();
			measuredLoadMsec += imageLoadMsec;
			pendingImages[ i ].size = static_cast<int>( imageLoadMsec + 0.5 );
			loadedStorageBytes += pendingImages[ i ].image->StorageSize();
		}
		session->AdvanceLoadingAssetQueue( 1 );
	}

	if ( prefetching ) {
		prefetch.End();
		if ( profileLevelLoad ) {
			common->Printf( "Image prefetch: %d of %d prefetched files taken\n", prefetch.Taken(), prefetch.Opened() );
		}
	}

	if ( profileLevelLoad && pendingImages.Num() > 0 ) {
		qsort( pendingImages.Ptr(), pendingImages.Num(), sizeof( sortedImage_t ), R_QsortImageSizes );
		common->Printf(
			"Image load profile: files=%d measured=%.1f msec storage=%.1f MiB\n",
			pendingImages.Num(),
			measuredLoadMsec,
			static_cast<double>( loadedStorageBytes ) / ( 1024.0 * 1024.0 ) );
		// Which of the four costs a load is actually paying decides what is worth
		// optimizing: a timestamp probe reads no payload but still opens a PK4
		// member, while a generated-cache hit skips decoding entirely.
		common->Printf(
			"  phases: probe=%.1f/%d generated=%.1f/%d decode=%.1f/%d upload=%.1f/%d msec/calls\n",
			imageLoadPhaseTimings.probeMsec, imageLoadPhaseTimings.probeCount,
			imageLoadPhaseTimings.generatedMsec, imageLoadPhaseTimings.generatedCount,
			imageLoadPhaseTimings.decodeMsec, imageLoadPhaseTimings.decodeCount,
			imageLoadPhaseTimings.uploadMsec, imageLoadPhaseTimings.uploadCount );
		const int slowImageCount = Min( 12, pendingImages.Num() );
		for ( int i = 0; i < slowImageCount; i++ ) {
			if ( pendingImages[ i ].size <= 0 ) {
				break;
			}
			common->Printf(
				"  image %4d msec %6.1f MiB %s\n",
				pendingImages[ i ].size,
				static_cast<double>( pendingImages[ i ].image->StorageSize() ) / ( 1024.0 * 1024.0 ),
				pendingImages[ i ].image->GetName() );
		}
	}

	return pendingImages.Num();
}

/*
===============
idImageManager::CountPendingLevelLoads
===============
*/
int idImageManager::CountPendingLevelLoads() const {
	R_RenderThread_Sync( "image registry" );
	int pendingCount = 0;

	for ( int i = 0; i < images.Num(); i++ ) {
		const idImage *image = images[ i ];
		if ( image->generatorFunction ) {
			continue;
		}
		if ( image->levelLoadReferenced && !image->IsLoaded() ) {
			pendingCount++;
		}
	}

	return pendingCount;
}

/*
===============
idImageManager::EndLevelLoad
===============
*/
void idImageManager::EndLevelLoad() {
	insideLevelLoad = false;

	common->Printf( "----- idImageManager::EndLevelLoad -----\n" );
	int start = Sys_Milliseconds();
	int	loadCount = LoadLevelImages( true );

	int	end = Sys_Milliseconds();
	common->Printf( "%5i images loaded in %5.1f seconds\n", loadCount, (end-start) * 0.001 );
	common->Printf( "----------------------------------------\n" );
	//R_ListImages_f( idCmdArgs( "sorted sorted", false ) );

	R_SetDDSProbeCacheActive( false );
}

/*
===============
idImageManager::StartBuild
===============
*/
void idImageManager::StartBuild() {
}

/*
===============
idImageManager::FinishBuild
===============
*/
void idImageManager::FinishBuild( bool removeDups ) {
}

/*
===============
idImageManager::PrintMemInfo
===============
*/
void idImageManager::PrintMemInfo( MemInfo_t *mi ) {
	R_RenderThread_Sync( "image registry" );
	int i, j, total = 0;
	int *sortIndex;
	idFile *f;

	f = fileSystem->OpenFileWrite( mi->filebase + "_images.txt" );
	if ( !f ) {
		return;
	}

	// sort first
	sortIndex = new int[images.Num()];

	for ( i = 0; i < images.Num(); i++ ) {
		sortIndex[i] = i;
	}

	for ( i = 0; i < images.Num() - 1; i++ ) {
		for ( j = i + 1; j < images.Num(); j++ ) {
			if ( images[sortIndex[i]]->StorageSize() < images[sortIndex[j]]->StorageSize() ) {
				int temp = sortIndex[i];
				sortIndex[i] = sortIndex[j];
				sortIndex[j] = temp;
			}
		}
	}

	// print next
	for ( i = 0; i < images.Num(); i++ ) {
		idImage *im = images[sortIndex[i]];
		int size;

		size = im->StorageSize();
		total += size;

		f->Printf( "%s %3i %s\n", idStr::FormatNumber( size ).c_str(), im->refCount, im->GetName() );
	}

	delete [] sortIndex;
	mi->imageAssetsTotal = total;

	f->Printf( "\nTotal image bytes allocated: %s\n", idStr::FormatNumber( total ).c_str() );
	fileSystem->CloseFile( f );
}

// The checked service has already compared the exact eight-field policy. Other
// reduction/sampler edits remain pending for their ordinary lifecycle.
void idImageManager::ClearCheckedImagePolicyChanges() {
    image_downSize.ClearModified(); image_downSizeLimit.ClearModified();
    image_downSizeSpecular.ClearModified(); image_downSizeSpecularLimit.ClearModified();
    image_downSizeBump.ClearModified(); image_downSizeBumpLimit.ClearModified();
    image_ignoreHighQuality.ClearModified();
}
