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
#include "OpenGL/FramebufferSamples.h"

/*
========================
R_ShouldSuppressMissingImageWarning

Suppress warning noise for known optional/missing stock references that
already fall back to a safe transparent placeholder.
========================
*/
static bool R_ShouldSuppressMissingImageWarning( const char * imageName ) {
	if ( imageName == NULL || imageName[0] == '\0' ) {
		return true;
	}
	if ( idStr::Icmp( imageName, "_emptyname" ) == 0 ) {
		return true;
	}
	if ( idStr::Icmp( imageName, "textures/common_misc/flickerflare" ) == 0 ) {
		return true;
	}
	if ( idStr::FindText( imageName, "_lightgrid_" ) >= 0 ) {
		return true;
	}
	return false;
}

/*
========================
R_FindMissingQ4StockImageFallback

Some retail effect materials name images that were not shipped in any Quake 4
PK4. Keep the authored name as the image identity so a loose/mod replacement
wins, but use the closest neutral stock shape when that primary lookup fails.
========================
*/
static const char *R_FindMissingQ4StockImageFallback( const char *imageName ) {
	if ( imageName == NULL || imageName[0] == '\0' ) {
		return NULL;
	}

	idStr canonicalName = imageName;
	canonicalName.BackSlashesToSlashes();
	canonicalName.StripFileExtension();

	struct q4StockImageFallback_t {
		const char *missingName;
		const char *replacementName;
	};
	static const q4StockImageFallback_t fallbacks[] = {
		{ "gfx/effects/fluids_drips/brown_bubble",
			"gfx/effects/fluids_drips/bubble_alpha.tga" },
		{ "gfx/effects/fluids_drips/brown_bubble_half",
			"gfx/effects/fluids_drips/bubble_half.tga" },
		{ "gfx/effects/fluids_drips/brown_splash_line",
			"gfx/effects/fluids_drips/splash_line.tga" }
	};

	for ( int i = 0; i < (int)( sizeof( fallbacks ) / sizeof( fallbacks[0] ) ); i++ ) {
		if ( canonicalName.Icmp( fallbacks[i].missingName ) == 0 ) {
			return fallbacks[i].replacementName;
		}
	}

	return NULL;
}

/*
========================
R_SelectMissingQ4StockImageSource

Resolve only the known incomplete retail declarations. Use the image-program
parser's load result rather than its timestamp: valid files inside retail PK4s
can report timestamp zero. The authored name always wins when present so adding
a loose/mod replacement immediately supersedes stock.
========================
*/
static bool R_SelectMissingQ4StockImageSource( const char *imageName, idStr &selectedSourceName,
	ID_TIME_T &selectedSourceTime, bool &stockFallbackSelected ) {
	const char *stockFallbackName = R_FindMissingQ4StockImageFallback( imageName );
	if ( stockFallbackName == NULL ) {
		return false;
	}

	selectedSourceName = imageName;
	selectedSourceTime = 0;
	stockFallbackSelected = false;
	if ( R_LoadImageProgram( imageName, NULL, NULL, NULL, &selectedSourceTime ) ) {
		return true;
	}

	selectedSourceTime = 0;
	if ( R_LoadImageProgram( stockFallbackName, NULL, NULL, NULL, &selectedSourceTime ) ) {
		selectedSourceName = stockFallbackName;
		stockFallbackSelected = true;
	}
	return true;
}

/*
========================
R_AddMissingQ4StockImageCacheIdentity

The three logical retail names need separate authored and fallback cache paths.
Besides preventing equal timestamps from aliasing the two sources, this keeps
legacy unsalted .bimage files from being accepted for either selection.
========================
*/
static void R_AddMissingQ4StockImageCacheIdentity( idStr &generatedName, bool stockFallbackSelected ) {
	idStr extension;
	generatedName.ExtractFileExtension( extension );
	generatedName.StripFileExtension();
	generatedName += stockFallbackSelected ? "#q4stockfallback" : "#q4authored";
	if ( extension.Length() > 0 ) {
		generatedName.SetFileExtension( extension );
	}
}

static unsigned int R_GetImageDownsizeSignature( const char *name, textureUsage_t usage, bool allowDownSize );
static void R_DownsizeLoadedImageData( const char *name, textureUsage_t usage, bool allowDownSize, byte *&pic, int &width, int &height );
static void R_DownsizeLoadedCubeImageData( const char *name, textureUsage_t usage, bool allowDownSize, byte *pics[6], int &size );

imageLoadPhaseTimings_t imageLoadPhaseTimings;

// Image loading is single-threaded (see parseBuffer in Image_program.cpp), so a
// plain accumulator is safe here.
class idScopedImageLoadPhase {
public:
	idScopedImageLoadPhase( double &accumulator, int &counter ) : target( accumulator ) {
		counter++;
		timer.Start();
	}
	~idScopedImageLoadPhase() {
		timer.Stop();
		target += timer.Milliseconds();
	}

private:
	double &	target;
	idTimer		timer;
};

static void R_LoadImageProgramForDeclaredUsage( const char *name, byte **pic, int *width, int *height,
	ID_TIME_T *timestamp, textureUsage_t &usage ) {
	// Classic image programs infer TD_BUMP for normal-producing operations.
	// PBR semantics are explicit authoring contracts and form part of the image
	// cache key, so decoding must not mutate them after lookup/name generation.
	const textureUsage_t declaredUsage = usage;
	textureUsage_t inferredUsage = usage;
	R_LoadImageProgram( name, pic, width, height, timestamp, &inferredUsage );
	if ( declaredUsage != TD_PBR_COLOR && declaredUsage != TD_MATERIAL_DATA ) {
		usage = inferredUsage;
	}
}

/*
========================
R_ETC2FormatForUsage

The uncompressed default, unless this renderer has no S3TC at all and
image_useETC2 has opted this usage in.

The S3TC condition is the whole point of the gate. Where DXT is available the
engine uploads Quake 4's shipped DXT blocks untouched, which is both smaller
and better than anything re-encoded from them could be; ETC2 there would be a
pure loss. It is the drivers with no DXT -- the Adreno 650 class -- that were
carrying every texture at 32 bpp with nowhere to go.

Bump maps are last, at level 3, because they are the only usage whose format
change reaches the shaders: EAC_RG11 stores no Z, so the interaction shaders
rebuild it. ETC2's RGB modes were never an option for them -- those fit a single
colour line through all three channels, which models a photograph well and a
normal badly.
========================
*/
static ID_INLINE textureFormat_t R_ETC2FormatForUsage( textureUsage_t usage, bool isCubeMap ) {
	if ( !glConfig.etc2TextureCompressionAvailable || glConfig.textureCompressionAvailable ) {
		return FMT_RGBA8;
	}

	// Cube maps are built by idBinaryImage::LoadCubeFromMemory, which has no
	// ETC2 branch: the format would fall through to its uncompressed default and
	// be stored as RGBA8. That is worse than merely not compressing, because
	// DeriveOpts would ask for ETC2 again on the next load, mismatch the RGBA8
	// header, and re-derive and rewrite the image on every single load forever.
	// Measured as 12 such images looping on game/airdefense1 before this check.
	if ( isCubeMap ) {
		return FMT_RGBA8;
	}

	const int level = image_useETC2.GetInteger();
	if ( level <= 0 ) {
		return FMT_RGBA8;
	}

	switch ( usage ) {
		case TD_SPECULAR:
			// Greyscale-ish and low frequency: the least that block artefacts
			// can cost, which is why this is the first usage switched over.
			return FMT_ETC2_RGB8;
		case TD_DIFFUSE:
		case TD_DEFAULT:
			if ( level >= 2 ) {
				// RGBA8 rather than RGB8 because a diffuse map may carry alpha
				// and nothing here knows yet whether this one does. Costs 8 bpp
				// instead of 4 -- still a quarter of uncompressed -- and the
				// per-image alpha split is a later refinement.
				return FMT_ETC2_RGBA8;
			}
			return FMT_RGBA8;
		case TD_BUMP:
			if ( level >= 3 ) {
				// 8 bpp, same as ETC2_RGBA8, but spent on two channels with
				// independent endpoints each rather than on three sharing one
				// colour line plus an alpha. That is why normals survive it:
				// measured 0.63 degrees RMS angular error on synthetic bump
				// art, against 4.09 degrees at the worst block.
				return FMT_EAC_RG11;
			}
			return FMT_RGBA8;
		default:
			return FMT_RGBA8;
	}
}

/*
========================
idImage::DeriveOpts
========================
*/
ID_INLINE void idImage::DeriveOpts() {

	if ( usage == TD_FONT ) {
		// Unguarded, this asked for DXT1 on renderers with no S3TC, which
		// R_BinaryImageHeaderSupportedByRenderer then rejected -- so the font
		// was re-derived and rewritten on every single map load and its cache
		// entry could never be used. TD_LIGHTGRID below has always guarded the
		// same way; this is only catching up with it.
		//
		// CFM_GREEN_ALPHA survives the change: Load2DFromMemory applies that
		// swizzle before it looks at the format, so the font shader still finds
		// the coverage value in green either way.
		opts.format = glConfig.textureCompressionAvailable ? FMT_DXT1 : FMT_RGBA8;
		opts.colorFormat = CFM_GREEN_ALPHA;
		opts.numLevels = 4; // Retail Quake 4's generated font-atlas path keeps four mip levels.
		opts.gammaMips = true;
		return;
	}

	if ( opts.format == FMT_NONE ) {
		opts.colorFormat = CFM_DEFAULT;
// jmarshall - no need to compress
		switch (usage) {
		case TD_DEPTH:
			opts.format = FMT_DEPTH;
			break;
		case TD_LIGHT:
			opts.format = FMT_RGB565;
			opts.gammaMips = true;
			break;
		case TD_LIGHTGRID:
			opts.gammaMips = false;
			opts.colorFormat = CFM_DEFAULT;
			opts.format = glConfig.textureCompressionAvailable ? FMT_DXT1 : FMT_RGB565;
			break;
		case TD_LIGHTGRID_VISIBILITY:
			opts.gammaMips = false;
			opts.colorFormat = CFM_DEFAULT;
			opts.format = FMT_RGBA8;
			break;
		case TD_LIGHTGRID_PROBE:
			opts.gammaMips = false;
			opts.colorFormat = CFM_DEFAULT;
			opts.format = FMT_RGBA8;
			break;
		case TD_LOOKUP_TABLE_MONO:
			opts.format = FMT_INT8;
			break;
		case TD_LOOKUP_TABLE_ALPHA:
			opts.format = FMT_ALPHA;
			break;
		case TD_LOOKUP_TABLE_RGB1:
		case TD_LOOKUP_TABLE_RGBA:
			opts.format = FMT_RGBA8;
			break;
		case TD_HIGH_QUALITY:
			// Preserve Quake 4's distinct "uncompressed/highquality" image bucket,
			// but keep it on openQ4's modern uncompressed RGBA8 path rather than
			// reviving older compressed-driver behavior.
			opts.gammaMips = false;
			opts.colorFormat = CFM_DEFAULT;
			opts.format = FMT_RGBA8;
			break;
		case TD_PBR_COLOR:
			opts.gammaMips = true;
			opts.colorFormat = CFM_DEFAULT;
			opts.format = FMT_RGBA8;
			break;
		case TD_MATERIAL_DATA:
			opts.gammaMips = false;
			opts.colorFormat = CFM_DEFAULT;
			opts.format = FMT_RGBA8;
			break;
		default:
				// TD_SPECULAR, TD_BUMP, TD_DIFFUSE and TD_DEFAULT all land here.
				// R_ETC2FormatForUsage returns FMT_RGBA8 unless the renderer has
				// no S3TC and image_useETC2 opts this usage in, so gammaMips and
				// colorFormat stay exactly as they were.
				opts.gammaMips = false;
				opts.format = R_ETC2FormatForUsage( usage, cubeFiles != CF_2D );
				opts.colorFormat = CFM_DEFAULT;
				break;
		}

/*
		switch ( usage ) {
			case TD_COVERAGE:
				opts.format = FMT_DXT1;
				opts.colorFormat = CFM_GREEN_ALPHA;
				break;
			case TD_DEPTH:
				opts.format = FMT_DEPTH;
				break;
			case TD_DIFFUSE: 
				// TD_DIFFUSE gets only set to when its a diffuse texture for an interaction
				opts.gammaMips = true;
				opts.format = FMT_DXT5;
				opts.colorFormat = CFM_DEFAULT;
				break;
			case TD_SPECULAR:
				opts.gammaMips = true;
				opts.format = FMT_DXT1;
				opts.colorFormat = CFM_DEFAULT;
				break;
			case TD_DEFAULT:
				opts.gammaMips = true;
				opts.format = FMT_DXT5;
				opts.colorFormat = CFM_DEFAULT;
				break;
			case TD_BUMP:
				opts.format = FMT_DXT5;
				opts.colorFormat = CFM_NORMAL_DXT5;
				break;
			case TD_FONT:
				opts.format = FMT_DXT1;
				opts.colorFormat = CFM_GREEN_ALPHA;
				opts.numLevels = 4; // We only support 4 levels because we align to 16 in the exporter
				opts.gammaMips = true;
				break;
			case TD_LIGHT:
				opts.format = FMT_RGB565;
				opts.gammaMips = true;
				break;
			case TD_LOOKUP_TABLE_MONO:
				opts.format = FMT_INT8;
				break;
			case TD_LOOKUP_TABLE_ALPHA:
				opts.format = FMT_ALPHA;
				break;
			case TD_LOOKUP_TABLE_RGB1:
			case TD_LOOKUP_TABLE_RGBA:
				opts.format = FMT_RGBA8;
				break;
			default:
				assert( false );
				opts.format = FMT_RGBA8;
		}
*/
	}

	if ( opts.numLevels == 0 ) {
		opts.numLevels = 1;

		if ( ( flags & IMAGEFLAG_NOMIPS ) != 0 ) {
			// Keep Quake 4's "nomips" semantic as a single-level texture, but
			// let the sampler policy decide the best modern filtering for that level.
		} else if ( filter == TF_LINEAR || filter == TF_NEAREST ) {
			// don't create mip maps if we aren't going to be using them
		} else {
			int	temp_width = opts.width;
			int	temp_height = opts.height;
			while ( temp_width > 1 || temp_height > 1 ) {
				temp_width >>= 1;
				temp_height >>= 1;
				if ( ( opts.format == FMT_DXT1 || opts.format == FMT_DXT5 || opts.format == FMT_BC7 ) &&
					( ( temp_width & 0x3 ) != 0 || ( temp_height & 0x3 ) != 0 ) ) {
						break;
				}
				opts.numLevels++;
			}
		}
	}
}

static ID_INLINE bool R_GeneratedImageHeaderMatchesDerivedOpts( const bimageFile_t &header, const idImageOpts &derivedOpts, textureUsage_t usage ) {
	if ( header.colorFormat != derivedOpts.colorFormat ||
		header.format != derivedOpts.format ||
		header.textureType != derivedOpts.textureType ) {
		return false;
	}

	if ( usage == TD_FONT && header.numLevels != derivedOpts.numLevels ) {
		return false;
	}

	return true;
}

static ID_INLINE bool R_BinaryImageHeaderSupportedByRenderer( const bimageFile_t &header ) {
	const textureFormat_t format = (textureFormat_t)header.format;
	if ( ( format == FMT_DXT1 || format == FMT_DXT5 ) && !glConfig.textureCompressionAvailable ) {
		return false;
	}
	if ( format == FMT_BC7 && !glConfig.bptcTextureCompressionAvailable ) {
		return false;
	}
	if ( ( format == FMT_ETC2_RGB8 || format == FMT_ETC2_RGBA8 || format == FMT_EAC_RG11 ) &&
			!glConfig.etc2TextureCompressionAvailable ) {
		return false;
	}
	return true;
}

/*
========================
idImage::AllocImage
========================
*/
void idImage::AllocImage( const idImageOpts &imgOpts, textureFilter_t tf, textureRepeat_t tr ) {
	filter = tf;
	repeat = tr;
	opts = imgOpts;
	defaulted = false;
	DeriveOpts();
	AllocImage();
}

/*
================
GenerateImage
================
*/
void idImage::GenerateImage( const byte *pic, int width, int height, textureFilter_t filterParm, textureRepeat_t repeatParm, textureUsage_t usageParm ) {
	PurgeImage();

	filter = filterParm;
	repeat = repeatParm;
	usage = usageParm;
	cubeFiles = CF_2D;
	defaulted = false;

	opts.textureType = TT_2D;
	opts.width = width;
	opts.height = height;
	opts.numLevels = 0;
	DeriveOpts();

	// if we don't have a rendering context, just return after we
	// have filled in the parms.  We must have the values set, or
	// an image match from a shader before the render starts would miss
	// the generated texture
	if ( !tr.IsOpenGLRunning() ) {
		return;
	}

	idBinaryImage im( GetName() );
	im.Load2DFromMemory( width, height, pic, opts.numLevels, opts.format, opts.colorFormat, opts.gammaMips, ( flags & IMAGEFLAG_FILTER_NEUTRAL_ALPHA ) != 0 );

	AllocImage();

	const int imageCount = im.NumImages();
	for ( int i = 0; i < imageCount; i++ ) {
		const bimageImage_t & img = im.GetImageHeader( i );
		const byte * data = im.GetImageData( i );
		SubImageUpload( img.level, 0, 0, img.destZ, img.width, img.height, data );
	}
}

/*
====================
GenerateCubeImage

Non-square cube sides are not allowed
====================
*/
void idImage::GenerateCubeImage( const byte *pic[6], int size, textureFilter_t filterParm, textureUsage_t usageParm ) {
	PurgeImage();

	filter = filterParm;
	repeat = TR_CLAMP;
	usage = usageParm;
	cubeFiles = CF_NATIVE;
	defaulted = false;

	opts.textureType = TT_CUBIC;
	opts.width = size;
	opts.height = size;
	opts.numLevels = 0;
	DeriveOpts();

	// if we don't have a rendering context, just return after we
	// have filled in the parms.  We must have the values set, or
	// an image match from a shader before the render starts would miss
	// the generated texture
	if ( !tr.IsOpenGLRunning() ) {
		return;
	}

	idBinaryImage im( GetName() );
	im.LoadCubeFromMemory( size, pic, opts.numLevels, opts.format, opts.gammaMips );

	AllocImage();

	const int imageCount = im.NumImages();
	for ( int i = 0; i < imageCount; i++ ) {
		const bimageImage_t & img = im.GetImageHeader( i );
		const byte * data = im.GetImageData( i );
		SubImageUpload( img.level, 0, 0, img.destZ, img.width, img.height, data );
	}
}

/*
===============
GetGeneratedName

name contains GetName() upon entry
===============
*/
 void idImage::GetGeneratedName( idStr &_name, const char *_policyName, const textureUsage_t &_usage, const cubeFiles_t &_cube, bool allowDownSize, unsigned int flags ) {
	idStr extension;

	_name.ExtractFileExtension( extension );
	_name.StripFileExtension();

	_name += va( "#__%02d%02d", (int)_usage, (int)_cube );
	if ( _cube != CF_2D ) {
		// Cube faces are assembled on the CPU before they reach the generated
		// file, so a change to that assembly invalidates every cached cube even
		// though the pk4 sources it names are untouched. Bump this revision with
		// any such change: builds up to 0.9.x cached faces that had the retail
		// progimg/ camera->native conversion applied twice, which left skyboxes
		// mis-oriented until the cache was deleted by hand.
		_name += "r1";
	}
	const unsigned int downsizeSignature = R_GetImageDownsizeSignature( _policyName, _usage, allowDownSize );
	if ( downsizeSignature != 0 ) {
		_name += va( "d%08x", downsizeSignature );
	}
	if ( flags != 0 ) {
		_name += va( "f%08x", flags );
	}
	if ( extension.Length() > 0 ) {
		_name.SetFileExtension( extension );
	}
}

static bool R_IsPreferredDDSStale( const idStr &preferredDDSName, ID_TIME_T preferredDDSFileTime, ID_TIME_T originalSourceTime ) {
	if ( originalSourceTime == FILE_NOT_FOUND_TIMESTAMP || preferredDDSFileTime >= originalSourceTime ) {
		return false;
	}

	// Retail PK4 timestamps can differ by one or two ZIP timestamp quanta even
	// though progimg/ and its source were produced together. Loose overrides
	// remain strict once they exceed that narrow stock-archive allowance.
	const bool retailProgramDDS = preferredDDSName.IcmpPrefix( "progimg/" ) == 0;
	return !retailProgramDDS || originalSourceTime - preferredDDSFileTime > 4;
}

/*
===============
ActuallyLoadImage

Absolutely every image goes through this path
On exit, the idImage will have a valid OpenGL texture number that can be bound
===============
*/
void idImage::ActuallyLoadImage( bool fromBackEnd ) {

	// if we don't have a rendering context yet, just return
	if ( !tr.IsOpenGLRunning() ) {
		return;
	}

	// this is the ONLY place generatorFunction will ever be called
	if ( generatorFunction ) {
		generatorFunction( this );
		return;
	}

	defaulted = false;
	// File-backed options may have been replaced by a directly uploaded DDS on
	// the previous load. Re-derive them from the image's declared usage so a
	// cvar/path change can safely move between BC/DXT and ordinary RGBA data.
	opts.format = FMT_NONE;
	opts.colorFormat = CFM_DEFAULT;
	opts.width = 0;
	opts.height = 0;
	opts.numLevels = 0;
	opts.gammaMips = false;

	bool sourceFileTimeKnown = false;
	// read through the cvar system rather than the framework-owned idCVar
	// object so this file stays linkable inside a renderer module
	if ( cvarSystem->GetCVarInteger( "com_productionMode" ) != 0 ) {
		sourceFileTime = FILE_NOT_FOUND_TIMESTAMP;
		sourceFileTimeKnown = true;
		if ( cubeFiles != CF_2D ) {
			opts.textureType = TT_CUBIC;
			repeat = TR_CLAMP;
		}
	} else {
		if ( cubeFiles != CF_2D ) {
			opts.textureType = TT_CUBIC;
			repeat = TR_CLAMP;
		} else {
			opts.textureType = TT_2D;
		}
	}

	// Figure out opts.colorFormat and opts.format so we can make sure the binary image is up to date
	DeriveOpts();

	idStr generatedName = GetName();
	GetGeneratedName( generatedName, GetName(), usage, cubeFiles, allowDownSize, flags );
	if ( filter == TF_LINEAR || filter == TF_NEAREST ) {
		// the unmipped sampler policy changes the generated mip count ( DeriveOpts ), so
		// keep its cache file distinct from the mipped variant of the same source
		idStr mipExt;
		generatedName.ExtractFileExtension( mipExt );
		generatedName.StripFileExtension();
		generatedName += "m0";
		if ( mipExt.Length() > 0 ) {
			generatedName.SetFileExtension( mipExt );
		}
	}
	idStr sourceExtension;
	idStr sourceName = GetName();
	sourceName.ExtractFileExtension( sourceExtension );
	const bool explicitDDSImage = idStr::Icmp( sourceExtension.c_str(), "dds" ) == 0;
	idStr preferredDDSName;
	ID_TIME_T preferredDDSFileTime = FILE_NOT_FOUND_TIMESTAMP;
	bool preferredDDSPrecompressed = false;
	bool preferredDDSImage = !explicitDDSImage &&
		cubeFiles == CF_2D &&
		R_ResolvePreferredDDSImageSource( GetName(), preferredDDSName, &preferredDDSFileTime, true, &preferredDDSPrecompressed );
	if ( preferredDDSImage && !fileSystem->InProductionMode() ) {
		ID_TIME_T originalSourceTime = FILE_NOT_FOUND_TIMESTAMP;
		{
			idScopedImageLoadPhase probePhase( imageLoadPhaseTimings.probeMsec, imageLoadPhaseTimings.probeCount );
			R_LoadImageProgramForDeclaredUsage( GetName(), NULL, NULL, NULL, &originalSourceTime, usage );
		}
		if ( R_IsPreferredDDSStale( preferredDDSName, preferredDDSFileTime, originalSourceTime ) ) {
			if ( cvarSystem->GetCVarBool( "image_showPrecompressedTextures" ) ) {
				common->Printf( "Ignoring stale DDS replacement %s for %s\n", preferredDDSName.c_str(), GetName() );
			}
			preferredDDSImage = false;
			preferredDDSPrecompressed = false;
			sourceFileTime = originalSourceTime;
			sourceFileTimeKnown = true;
		} else {
			sourceFileTime = preferredDDSFileTime;
			sourceFileTimeKnown = true;
		}
	}
	if ( preferredDDSImage && cvarSystem->GetCVarBool( "image_showPrecompressedTextures" ) ) {
		common->Printf( "Using DDS replacement %s for %s\n", preferredDDSName.c_str(), GetName() );
	}
	const bool selectedDDSImage = explicitDDSImage || preferredDDSImage;
	const bool bypassGeneratedFile = explicitDDSImage || preferredDDSPrecompressed;
	idStr selectedSourceName = GetName();
	if ( preferredDDSImage ) {
		selectedSourceName = preferredDDSName;
	}
	if ( preferredDDSImage && !preferredDDSPrecompressed ) {
		generatedName = preferredDDSName;
		GetGeneratedName( generatedName, GetName(), usage, cubeFiles, allowDownSize, flags );
		if ( filter == TF_LINEAR || filter == TF_NEAREST ) {
			idStr mipExt;
			generatedName.ExtractFileExtension( mipExt );
			generatedName.StripFileExtension();
			generatedName += "m0";
			if ( mipExt.Length() > 0 ) {
				generatedName.SetFileExtension( mipExt );
			}
		}
	}
	bool q4StockImageCandidateResolved = false;
	if ( !explicitDDSImage && !preferredDDSImage && cubeFiles == CF_2D ) {
		bool stockFallbackSelected = false;
		q4StockImageCandidateResolved = R_SelectMissingQ4StockImageSource( GetName(), selectedSourceName,
			sourceFileTime, stockFallbackSelected );
		if ( q4StockImageCandidateResolved ) {
			sourceFileTimeKnown = true;
			R_AddMissingQ4StockImageCacheIdentity( generatedName, stockFallbackSelected );
		}
	}
	idStr selectedLoadSourceName = preferredDDSImage ? preferredDDSName : selectedSourceName;
	const char *loadSourceName = selectedLoadSourceName.c_str();

	idBinaryImage im( generatedName );
	if ( bypassGeneratedFile ) {
		binaryFileTime = FILE_NOT_FOUND_TIMESTAMP;
	} else {
		idScopedImageLoadPhase generatedPhase( imageLoadPhaseTimings.generatedMsec, imageLoadPhaseTimings.generatedCount );
		binaryFileTime = im.LoadFromGeneratedFileUnchecked();
	}

	// BFHACK, do not want to tweak on buildgame so catch these images here
	if ( !bypassGeneratedFile && binaryFileTime == FILE_NOT_FOUND_TIMESTAMP ) {
		int c = 1;
		while ( c-- > 0 ) {
			if ( generatedName.Find( "guis/assets/white#__0000", false ) >= 0 ) {
				generatedName.Replace( "white#__0000", "white#__0200" );
				im.SetName( generatedName );
				binaryFileTime = im.LoadFromGeneratedFileUnchecked();
				break;
			}
			if ( generatedName.Find( "guis/assets/white#__0100", false ) >= 0 ) {
				generatedName.Replace( "white#__0100", "white#__0200" );
				im.SetName( generatedName );
				binaryFileTime = im.LoadFromGeneratedFileUnchecked();
				break;
			}
			if ( generatedName.Find( "textures/black#__0100", false ) >= 0 ) {
				generatedName.Replace( "black#__0100", "black#__0200" );
				im.SetName( generatedName );
				binaryFileTime = im.LoadFromGeneratedFileUnchecked();
				break;
			}
			if ( generatedName.Find( "textures/decals/bulletglass1_d#__0100", false ) >= 0 ) {
				generatedName.Replace( "bulletglass1_d#__0100", "bulletglass1_d#__0200" );
				im.SetName( generatedName );
				binaryFileTime = im.LoadFromGeneratedFileUnchecked();
				break;
			}
			if ( generatedName.Find( "models/monsters/skeleton/skeleton01_d#__1000", false ) >= 0 ) {
				generatedName.Replace( "skeleton01_d#__1000", "skeleton01_d#__0100" );
				im.SetName( generatedName );
				binaryFileTime = im.LoadFromGeneratedFileUnchecked();
				break;
			}
		}
	}
	const bool productionMode = fileSystem->InProductionMode();
	auto acceptGeneratedImage = [&]( ID_TIME_T candidateFileTime ) -> bool {
		if ( candidateFileTime == FILE_NOT_FOUND_TIMESTAMP ) {
			return false;
		}
		// A rejection here throws the cache entry away and re-derives the image
		// from source, which on a device with no DDS fast path means decoding
		// and recompressing it -- the single most expensive thing a map load
		// does. Each gate reports both sides, because a systematically wrong
		// timestamp or a stale opts field makes the cache silently useless
		// while still looking populated on disk.
		const bool reportGeneratedCache = cvarSystem->GetCVarBool( "image_showGeneratedImageWrites" );
		if ( !productionMode ) {
			if ( !sourceFileTimeKnown ) {
				idScopedImageLoadPhase probePhase( imageLoadPhaseTimings.probeMsec, imageLoadPhaseTimings.probeCount );
				if ( cubeFiles != CF_2D ) {
					R_LoadCubeImages( GetName(), cubeFiles, NULL, NULL, &sourceFileTime );
				} else if ( preferredDDSImage ) {
					sourceFileTime = preferredDDSFileTime;
				} else {
					R_LoadImageProgramForDeclaredUsage( GetName(), NULL, NULL, NULL, &sourceFileTime, usage );
				}
				sourceFileTimeKnown = true;
			}
			if ( im.GetFileHeader().sourceFileTime != sourceFileTime ) {
				if ( reportGeneratedCache ) {
					common->Printf( "generated cache MISS %s: header=%lld computed=%lld (source '%s'%s)\n",
						generatedName.c_str(),
						( long long )im.GetFileHeader().sourceFileTime,
						( long long )sourceFileTime,
						selectedSourceName.c_str(),
						preferredDDSImage ? ", dds replacement" : "" );
				}
				im.Clear();
				return false;
			}
		}
		if ( !R_BinaryImageHeaderSupportedByRenderer( im.GetFileHeader() ) ) {
			if ( reportGeneratedCache ) {
				common->Printf( "generated cache UNSUPPORTED %s: format=%d\n",
					generatedName.c_str(), im.GetFileHeader().format );
			}
			im.Clear();
			return false;
		}
		// The opts check stays on in production mode. It costs nothing -- the header
		// is already in memory -- and the generated file name does not encode the
		// texture format, so it is the only thing that notices when a cvar such as
		// image_useETC2 changes what DeriveOpts just asked for. Skipping it made a
		// format change silently reuse cache entries written in the old format.
		// Production mode is meant to skip source timestamp validation, nothing else.
		if ( !R_GeneratedImageHeaderMatchesDerivedOpts( im.GetFileHeader(), opts, usage ) ) {
			if ( reportGeneratedCache ) {
				const bimageFile_t &h = im.GetFileHeader();
				common->Printf( "generated cache OPTSMISS %s: fmt hdr=%d drv=%d, color hdr=%d drv=%d, type hdr=%d drv=%d, usage=%d\n",
					generatedName.c_str(),
					h.format, opts.format,
					h.colorFormat, opts.colorFormat,
					h.textureType, opts.textureType,
					(int)usage );
			}
			im.Clear();
			return false;
		}
		if ( reportGeneratedCache ) {
			common->Printf( "generated cache hit %s\n", generatedName.c_str() );
		}
		return true;
	};

	bool generatedImageAccepted = acceptGeneratedImage( binaryFileTime );
	if ( !generatedImageAccepted && !bypassGeneratedFile ) {
		{
			idScopedImageLoadPhase generatedPhase( imageLoadPhaseTimings.generatedMsec, imageLoadPhaseTimings.generatedCount );
			binaryFileTime = im.LoadFromCompactGeneratedFileUnchecked();
		}
		generatedImageAccepted = acceptGeneratedImage( binaryFileTime );
	}
	if ( generatedImageAccepted ) {
		const bimageFile_t & header = im.GetFileHeader();
		opts.width = header.width;
		opts.height = header.height;
		opts.numLevels = header.numLevels;
		opts.colorFormat = (textureColor_t)header.colorFormat;
		opts.format = (textureFormat_t)header.format;
		opts.textureType = (textureType_t)header.textureType;
		//if ( cvarSystem->GetCVarBool( "fs_buildresources" ) ) {
		//	// for resource gathering write this image to the preload file for this map
		//	fileSystem->AddImagePreload( GetName(), filter, repeat, usage, cubeFiles );
		//}
	} else {
		idScopedImageLoadPhase decodePhase( imageLoadPhaseTimings.decodeMsec, imageLoadPhaseTimings.decodeCount );
		im.Clear();
		bool loadedPrecompressedDDS = false;
		if ( cubeFiles != CF_2D ) {
			int size;
			byte * pics[6];

			if ( !R_LoadCubeImages( GetName(), cubeFiles, pics, &size, &sourceFileTime ) || size == 0 ) {
				idLib::Warning( "Couldn't load cube image: %s", GetName() );
				// create a default so it doesn't get continuously reloaded
				opts.width = 8;
				opts.height = 8;
				opts.numLevels = 1;
				DeriveOpts();
				AllocImage();

				// clear the data so it's not left uninitialized
				idTempArray<byte> clear( opts.width * opts.height * 4 );
				memset( clear.Ptr(), 0, clear.Size() );
				for ( int level = 0; level < opts.numLevels; level++ ) {
					for ( int side = 0; side < 6; side++ ) {
						SubImageUpload( level, 0, 0, side, opts.width >> level, opts.height >> level, clear.Ptr() );
					}
				}

				defaulted = true;
				return;
			}

			R_DownsizeLoadedCubeImageData( GetName(), usage, allowDownSize, pics, size );
			opts.textureType = TT_CUBIC;
			repeat = TR_CLAMP;
			opts.width = size;
			opts.height = size;
			opts.numLevels = 0;
			DeriveOpts();
			im.LoadCubeFromMemory( size, (const byte **)pics, opts.numLevels, opts.format, opts.gammaMips );
			repeat = TR_CLAMP;

			for ( int i = 0; i < 6; i++ ) {
				if ( pics[i] ) {
					Mem_Free( pics[i] );
				}
			}
		} else {
			int width, height;
			byte *pic = NULL;
			imageDownsizePolicy_t precompressedDownsizePolicy;
			R_GetImageDownsizePolicy( GetName(), usage, allowDownSize, precompressedDownsizePolicy );
			const bool usePrecompressedMipmaps = ( flags & IMAGEFLAG_NOMIPS ) == 0 && filter != TF_LINEAR && filter != TF_NEAREST;
			const bool tryDirectDDSLoad = selectedDDSImage && ( explicitDDSImage || preferredDDSPrecompressed );

			if ( tryDirectDDSLoad && R_LoadPrecompressedDDS( loadSourceName, im, &sourceFileTime, usage, precompressedDownsizePolicy, usePrecompressedMipmaps ) ) {
				const bimageFile_t &header = im.GetFileHeader();
				opts.width = header.width;
				opts.height = header.height;
				opts.numLevels = header.numLevels;
				opts.colorFormat = (textureColor_t)header.colorFormat;
				opts.format = (textureFormat_t)header.format;
				opts.textureType = (textureType_t)header.textureType;
				sourceFileTimeKnown = true;
				loadedPrecompressedDDS = true;

				// Compressed data can only be reduced by dropping authored mip
				// levels, so a replacement exported without a full chain cannot
				// always reach the requested size. If the policy would still
				// shrink what we ended up with, the chain ran out.
				if ( precompressedDownsizePolicy.IsActive() ) {
					int reachedWidth = header.width;
					int reachedHeight = header.height;
					R_ApplyImageDownsizePolicy( precompressedDownsizePolicy, reachedWidth, reachedHeight );
					if ( ( reachedWidth != header.width || reachedHeight != header.height ) &&
						cvarSystem->GetCVarBool( "image_showPrecompressedTextures" ) ) {
						common->Printf( "%s: %s has no mip level small enough for the active texture reduction (kept %dx%d, wanted %dx%d)\n",
							GetName(), loadSourceName, header.width, header.height, reachedWidth, reachedHeight );
					}
				}
			} else {
				const char *fallbackLoadSourceName = loadSourceName;
				if ( preferredDDSPrecompressed ) {
					common->Warning( "Couldn't load preferred precompressed DDS replacement %s for %s; falling back to original source", loadSourceName, GetName() );
					fallbackLoadSourceName = GetName();
					selectedSourceName = GetName();
					sourceFileTime = FILE_NOT_FOUND_TIMESTAMP;
				}

				// load the full specification, and perform any image program calculations
				R_LoadImageProgramForDeclaredUsage( fallbackLoadSourceName, &pic, &width, &height, &sourceFileTime, usage );
				if ( pic == NULL && preferredDDSImage && !preferredDDSPrecompressed ) {
					common->Warning( "Couldn't decode preferred DDS replacement %s for %s; falling back to original source", loadSourceName, GetName() );
					selectedSourceName = GetName();
					sourceFileTime = FILE_NOT_FOUND_TIMESTAMP;
					R_LoadImageProgramForDeclaredUsage( GetName(), &pic, &width, &height, &sourceFileTime, usage );
				}
				if ( pic == NULL && !q4StockImageCandidateResolved ) {
					const char *stockFallbackName = R_FindMissingQ4StockImageFallback( GetName() );
					if ( stockFallbackName != NULL ) {
						sourceFileTime = FILE_NOT_FOUND_TIMESTAMP;
						R_LoadImageProgramForDeclaredUsage( stockFallbackName, &pic, &width, &height, &sourceFileTime, usage );
						if ( pic != NULL ) {
							selectedSourceName = stockFallbackName;
						}
					}
				}
				sourceFileTimeKnown = true;

				if ( pic == NULL ) {
					if ( !R_ShouldSuppressMissingImageWarning( GetName() ) ) {
						idLib::Warning( "Couldn't load image: %s : %s", GetName(), generatedName.c_str() );
					}
					// create a default so it doesn't get continuously reloaded
					opts.width = 8;
					opts.height = 8;
					opts.numLevels = 1;
					DeriveOpts();
					AllocImage();

					// clear the data so it's not left uninitialized
					idTempArray<byte> clear( opts.width * opts.height * 4 );
					memset( clear.Ptr(), 0, clear.Size() );
					for ( int level = 0; level < opts.numLevels; level++ ) {
						SubImageUpload( level, 0, 0, 0, opts.width >> level, opts.height >> level, clear.Ptr() );
					}

					defaulted = true;
					return;
				}

				R_DownsizeLoadedImageData( GetName(), usage, allowDownSize, pic, width, height );
				opts.width = width;
				opts.height = height;
				opts.numLevels = 0;
				DeriveOpts();
				im.Load2DFromMemory( opts.width, opts.height, pic, opts.numLevels, opts.format, opts.colorFormat, opts.gammaMips, ( flags & IMAGEFLAG_FILTER_NEUTRAL_ALPHA ) != 0 );

				Mem_Free( pic );
			}
		}
		if ( !loadedPrecompressedDDS ) {
			binaryFileTime = im.WriteGeneratedFile( sourceFileTime );
		}
	}

	{
		idScopedImageLoadPhase uploadPhase( imageLoadPhaseTimings.uploadMsec, imageLoadPhaseTimings.uploadCount );
		AllocImage();

		const int imageCount = im.NumImages();
		for ( int i = 0; i < imageCount; i++ ) {
			const bimageImage_t & img = im.GetImageHeader( i );
			const byte * data = im.GetImageData( i );
			SubImageUpload( img.level, 0, 0, img.destZ, img.width, img.height, data );
		}
	}
	loadedSourceName = selectedSourceName;
}

/*
==============
Bind

Automatically enables 2D mapping or cube mapping if needed
==============
*/
static bool R_IsValidTrackedTextureUnit( int texUnit ) {
	if ( texUnit >= 0 && texUnit < MAX_MULTITEXTURE_UNITS &&
		texUnit < glConfig.maxTextureUnits && texUnit < glConfig.maxTextureImageUnits ) {
		return true;
	}

	common->Warning(
		"idImage::Bind: tracked texture unit %d is outside limits (tracked=%d, textureUnits=%d, imageUnits=%d)",
		texUnit,
		MAX_MULTITEXTURE_UNITS,
		glConfig.maxTextureUnits,
		glConfig.maxTextureImageUnits );
	return false;
}

static bool R_BindTextureToUnit( int texUnit, GLenum target, GLuint texture ) {
	// Core/ARB DSA derives the target from an existing texture object. Binding
	// zero has broader unbind semantics, so retain the target-specific paths for
	// that case.
	if ( texture != 0 && glConfig.backendCaps.hasDSA && glBindTextureUnit != NULL ) {
		glBindTextureUnit( static_cast<GLuint>( texUnit ), texture );
		return true;
	}

	// Experimental loaders can expose entry points absent from this context.
	if ( GLCapabilityProbe_HasExtension( "GL_EXT_direct_state_access" ) && glBindMultiTextureEXT != NULL ) {
		glBindMultiTextureEXT( GL_TEXTURE0_ARB + texUnit, target, texture );
		return true;
	}

	// The compatibility renderer requires ARB multitexture, but keep this
	// guard so an incomplete loader cannot turn an image bind into a null call.
	if ( glActiveTextureARB == NULL ) {
		common->Warning( "idImage::Bind: no texture-unit binding entry point is available" );
		return false;
	}

	glActiveTextureARB( GL_TEXTURE0_ARB + texUnit );
	glBindTexture( target, texture );
	return true;
}

void idImage::Bind() {

	//	RENDERLOG_PRINTF( "idImage::Bind( %s )\n", GetName() );

		// load the image if necessary (FIXME: not SMP safe!)
	if (!IsLoaded()) {
		// load the image on demand here, which isn't our normal game operating mode
		ActuallyLoadImage(true);
	}

	const int texUnit = backEnd.glState.currenttmu;
	if ( !R_IsValidTrackedTextureUnit( texUnit ) ) {
		return;
	}

	tmu_t* tmu = &backEnd.glState.tmu[texUnit];

	// enable or disable apropriate texture modes
	//
	// GL_TEXTURE_2D and GL_TEXTURE_CUBE_MAP are fixed-function texture-target
	// enables: they select which target the fixed-function fragment stage
	// samples, and they do not exist as enables in an ES or core profile,
	// where the shader names its own sampler. glEnable/glDisable with them
	// raises GL_INVALID_ENUM on every texture-type transition -- measured on
	// ES as a persistent error that outlived the frame raising it and
	// corrupted per-draw glGetError checks in the back end.
	//
	// The bookkeeping still runs on every profile; only the two calls that
	// reach the driver are gated.
	if (tmu->textureType != opts.textureType && (backEnd.glState.currenttmu < glConfig.maxTextureUnits)) {
		const bool hasFixedFunctionTextureEnables =
			glConfig.backendCaps.profile != RENDERER_CONTEXT_PROFILE_ES
			&& glConfig.backendCaps.profile != RENDERER_CONTEXT_PROFILE_CORE;
		if (hasFixedFunctionTextureEnables) {
			if (tmu->textureType == TT_CUBIC) {
				glDisable(GL_TEXTURE_CUBE_MAP_EXT);
			}
			else if (tmu->textureType == TT_2D) {
				glDisable(GL_TEXTURE_2D);
			}

			if (opts.textureType == TT_CUBIC) {
				glEnable(GL_TEXTURE_CUBE_MAP_EXT);
			}
			else if (opts.textureType == TT_2D) {
				glEnable(GL_TEXTURE_2D);
			}
		}
		tmu->textureType = opts.textureType;
	}

	// bind the texture
	if (opts.textureType == TT_2D) {
		if (tmu->current2DMap != texnum) {
			if ( R_BindTextureToUnit( texUnit, GL_TEXTURE_2D, texnum ) ) {
				tmu->current2DMap = texnum;
			}
		}
	}
	else if (opts.textureType == TT_CUBIC) {
		if (tmu->currentCubeMap != texnum) {
			if ( R_BindTextureToUnit( texUnit, GL_TEXTURE_CUBE_MAP_EXT, texnum ) ) {
				tmu->currentCubeMap = texnum;
			}
		}
	}

}

/*
================
MakePowerOfTwo
================
*/
int MakePowerOfTwo( int num ) {
	int	pot;
	for ( pot = 1; pot < num; pot <<= 1 ) {
	}
	return pot;
}

static unsigned int R_GetImageDownsizeSignature( const char *name, textureUsage_t usage, bool allowDownSize ) {
	imageDownsizePolicy_t policy;
	R_GetImageDownsizePolicy( name, usage, allowDownSize, policy );
	if ( !policy.IsActive() ) {
		return 0;
	}

	// The trailing byte is a revision of the reduction itself, not of any cvar.
	// Bump it whenever the pixels a given policy produces change: 0x00 -> 0x01
	// moved reduction from a single bilinear resample onto the same box-filter
	// mip chain the rest of the pipeline uses, so every cached downsized image
	// written before that is stale even though its policy is unchanged.
	unsigned int signature = ( static_cast<unsigned int>( policy.maxDimension ) << 8 ) ^ static_cast<unsigned int>( usage ) ^ 0x6F713401u;
	if ( policy.mipShift > 0 ) {
		signature ^= ( static_cast<unsigned int>( policy.mipShift ) * 0x9E3779B9u );
		signature ^= ( static_cast<unsigned int>( policy.minDimension ) * 0x85EBCA6Bu );
	}

	return signature;
}

/*
================
R_ShrinkLoadedImageData

Reduces decoded pixels to the size the policy asks for. An exact halving chain
goes through the same box filter that builds the mip chain, so image_picmip N
produces byte for byte what mip level N of the unreduced texture would have
been; anything else falls back to a general resample.
================
*/
static bool R_ImageUsageUsesGammaMips( textureUsage_t usage ) {
	// mirrors the gammaMips choices DeriveOpts makes for each usage
	return usage == TD_FONT || usage == TD_LIGHT || usage == TD_PBR_COLOR;
}

static int R_CountExactHalvings( int width, int height, int scaledWidth, int scaledHeight ) {
	int halvings = 0;
	while ( width > scaledWidth || height > scaledHeight ) {
		if ( ( width > 1 && ( width & 1 ) ) || ( height > 1 && ( height & 1 ) ) ) {
			return -1;	// odd extent, not an exact mip step
		}
		width = Max( 1, width >> 1 );
		height = Max( 1, height >> 1 );
		halvings++;
		if ( width < scaledWidth || height < scaledHeight ) {
			return -1;	// overshoots the requested size
		}
	}
	return ( width == scaledWidth && height == scaledHeight ) ? halvings : -1;
}

static byte *R_ShrinkLoadedImageData( const byte *pic, int width, int height, int scaledWidth, int scaledHeight, bool gammaMips ) {
	const int halvings = R_CountExactHalvings( width, height, scaledWidth, scaledHeight );
	if ( halvings <= 0 ) {
		return R_ResampleTexture( pic, width, height, scaledWidth, scaledHeight );
	}

	byte *shrunk = NULL;
	int level = width;
	int levelHeight = height;
	for ( int i = 0; i < halvings; i++ ) {
		const byte *source = ( shrunk != NULL ) ? shrunk : pic;
		byte *next = gammaMips ? R_MipMapWithGamma( source, level, levelHeight ) : R_MipMap( source, level, levelHeight );
		if ( next == NULL ) {
			break;
		}
		if ( shrunk != NULL ) {
			Mem_Free( shrunk );
		}
		shrunk = next;
		level = Max( 1, level >> 1 );
		levelHeight = Max( 1, levelHeight >> 1 );
	}

	if ( shrunk == NULL ) {
		return R_ResampleTexture( pic, width, height, scaledWidth, scaledHeight );
	}
	if ( level != scaledWidth || levelHeight != scaledHeight ) {
		Mem_Free( shrunk );
		return R_ResampleTexture( pic, width, height, scaledWidth, scaledHeight );
	}

	return shrunk;
}

static void R_DownsizeLoadedImageData( const char *name, textureUsage_t usage, bool allowDownSize, byte *&pic, int &width, int &height ) {
	if ( pic == NULL || width <= 0 || height <= 0 ) {
		return;
	}

	imageDownsizePolicy_t policy;
	R_GetImageDownsizePolicy( name, usage, allowDownSize, policy );

	int scaledWidth = width;
	int scaledHeight = height;
	R_ApplyImageDownsizePolicy( policy, scaledWidth, scaledHeight );
	if ( scaledWidth == width && scaledHeight == height ) {
		return;
	}

	byte *resampled = R_ShrinkLoadedImageData( pic, width, height, scaledWidth, scaledHeight, R_ImageUsageUsesGammaMips( usage ) );
	if ( resampled == NULL ) {
		return;
	}

	Mem_Free( pic );
	pic = resampled;
	width = scaledWidth;
	height = scaledHeight;
}

static void R_DownsizeLoadedCubeImageData( const char *name, textureUsage_t usage, bool allowDownSize, byte *pics[6], int &size ) {
	if ( pics == NULL || size <= 0 ) {
		return;
	}

	imageDownsizePolicy_t policy;
	R_GetImageDownsizePolicy( name, usage, allowDownSize, policy );

	int scaledSize = size;
	int scaledHeight = size;
	R_ApplyImageDownsizePolicy( policy, scaledSize, scaledHeight );
	if ( scaledSize == size && scaledHeight == size ) {
		return;
	}

	for ( int i = 0; i < 6; i++ ) {
		if ( pics[i] == NULL ) {
			continue;
		}

		byte *resampled = R_ShrinkLoadedImageData( pics[i], size, size, scaledSize, scaledSize, R_ImageUsageUsesGammaMips( usage ) );
		if ( resampled == NULL ) {
			continue;
		}

		Mem_Free( pics[i] );
		pics[i] = resampled;
	}

	size = scaledSize;
}

/*
====================
R_BindTextureForDirectAccess

Raw glBindTexture for framebuffer copies, uploads, and sampler-state updates.
The bind replaces the active unit's binding BEHIND idImage::Bind()'s
redundant-bind filter, so the per-TMU tracker must be updated alongside or a
later Bind() of the previously tracked texture silently early-outs while
something else is actually bound. A stale tracker entry here is how the SSAO
pass ended up sampling its depth scratch image as the scene texture
(near-dark / far-white frames): a _currentRender material left
tracking[0] == scene, the depth copy raw-bound the depth texture over it,
and the scene Bind() was filtered out.
====================
*/
void R_BindTextureForDirectAccess( unsigned int target, int texnum ) {
	glBindTexture( target, texnum );
	if ( target == GL_TEXTURE_2D ) {
		backEnd.glState.tmu[backEnd.glState.currenttmu].current2DMap = texnum;
	} else if ( target == GL_TEXTURE_CUBE_MAP_EXT ) {
		backEnd.glState.tmu[backEnd.glState.currenttmu].currentCubeMap = texnum;
	}
	// other targets (e.g. multisample) are not tracked by the legacy filter
	// and do not disturb the tracked 2D / cube bindings
}

// Lazily created scratch FBOs for the CopyFramebuffer/CopyDepthbuffer blit
// paths. They are GL context objects: the names must be forgotten (and deleted
// while the old context is still current) before the context is destroyed, or
// the surviving nonzero names alias the new context's render-target FBOs and
// the next copy blit detaches a live render target's attachment.
static GLuint r_copyFramebufferFbo = 0;
static GLuint r_copyDepthbufferFbo = 0;

/*
====================
R_PurgeFramebufferCopyFBOs

Called before GLimp_Shutdown (full vid_restart and final shutdown) while the
old context is still current.
====================
*/
void R_PurgeFramebufferCopyFBOs( void ) {
	if ( r_copyFramebufferFbo != 0 ) {
		glDeleteFramebuffers( 1, &r_copyFramebufferFbo );
		r_copyFramebufferFbo = 0;
	}
	if ( r_copyDepthbufferFbo != 0 ) {
		glDeleteFramebuffers( 1, &r_copyDepthbufferFbo );
		r_copyDepthbufferFbo = 0;
	}
}

// A cubemap is complete only when all six faces have matching storage. A
// framebuffer/depth copy writes a single sealed face, so resize every face
// first and then use the selected face as the transfer destination.
static void R_AllocateCopyTextureStorage( bool isCube, GLenum copyTarget,
		GLint copyInternalFormat, int width, int height, GLenum copyDataFormat,
		GLenum copyDataType ) {
	if ( !isCube ) {
		glTexImage2D( copyTarget, 0, copyInternalFormat, width, height, 0,
			copyDataFormat, copyDataType, NULL );
		return;
	}
	for ( int face = 0; face < 6; ++face ) {
		glTexImage2D( GL_TEXTURE_CUBE_MAP_POSITIVE_X_EXT + face, 0,
			copyInternalFormat, width, height, 0, copyDataFormat, copyDataType,
			NULL );
	}
}

/*
====================
CopyFramebuffer
====================
*/
bool idImage::CopyFramebuffer( int x, int y, int imageWidth, int imageHeight,
		int cubeFace ) {
	const bool isCube = opts.textureType == TT_CUBIC;
	if ( imageWidth <= 0 || imageHeight <= 0
			|| ( isCube && ( cubeFace < 0 || cubeFace >= 6 ) )
			|| ( !isCube && cubeFace != 0 ) ) {
		return false;
	}
	const GLenum textureTarget = isCube ? GL_TEXTURE_CUBE_MAP_EXT : GL_TEXTURE_2D;
	const GLenum copyTarget = isCube
		? GL_TEXTURE_CUBE_MAP_POSITIVE_X_EXT + cubeFace : GL_TEXTURE_2D;
	R_BindTextureForDirectAccess( textureTarget, texnum );

	const bool readingFromRenderTexture = ( backEnd.renderTexture != NULL ) && ( backEnd.renderTexture->GetNumColorImages() > 0 );
	const GLenum readAttachment = GL_COLOR_ATTACHMENT0;
	bool needsStorageResize = ( opts.width != imageWidth ) || ( opts.height != imageHeight );

	// ES 3.0 will not copy the fixed-point default framebuffer into a
	// floating-point texture. _currentRender is FMT_RGBA16F (Image_intrinsic.cpp),
	// so on ES the whole non-blit capture path failed silently: glCopyTexImage2D
	// raised GL_INVALID_OPERATION, the destination kept its 16x16 intrinsic
	// storage, and because opts.width/height were updated anyway every later
	// call took the glCopyTexSubImage2D branch and raised GL_INVALID_VALUE for a
	// region larger than the level. Respecify as RGBA8 instead: the source is an
	// 8-bit back buffer, so the float storage was buying nothing here. Desktop GL
	// accepts the mismatched copy and keeps its 16F target.
	if ( !readingFromRenderTexture && glConfig.backendCaps.profile == RENDERER_CONTEXT_PROFILE_ES ) {
		const bool destIsFixedPoint =
			internalFormat == GL_RGBA8 || internalFormat == GL_RGB8
			|| internalFormat == GL_RGBA || internalFormat == GL_RGB
			|| internalFormat == GL_SRGB8_ALPHA8 || internalFormat == GL_SRGB8
			|| internalFormat == GL_RGB565 || internalFormat == GL_RGB5_A1
			|| internalFormat == GL_RGBA4;
		if ( !destIsFixedPoint ) {
			opts.format = FMT_RGBA8;
			internalFormat = GL_RGBA8;
			dataFormat = GL_RGBA;
			dataType = GL_UNSIGNED_BYTE;
			needsStorageResize = true;
		}
	}

	opts.width = imageWidth;
	opts.height = imageHeight;

	if ( readingFromRenderTexture && ( GLEW_EXT_framebuffer_blit || GLEW_ARB_framebuffer_object || GLEW_VERSION_3_0 ) ) {
		GLint previousReadFbo = 0;
		GLint previousDrawFbo = 0;
		GLint previousReadBuffer = GL_BACK;
		GLint previousDrawBuffer = GL_BACK;

		glGetIntegerv( GL_READ_FRAMEBUFFER_BINDING, &previousReadFbo );
		glGetIntegerv( GL_DRAW_FRAMEBUFFER_BINDING, &previousDrawFbo );
		glGetIntegerv( GL_READ_BUFFER, &previousReadBuffer );
		glGetIntegerv( GL_DRAW_BUFFER, &previousDrawBuffer );

		if ( r_copyFramebufferFbo == 0 ) {
			glGenFramebuffers( 1, &r_copyFramebufferFbo );
		}
		const GLuint copyFbo = r_copyFramebufferFbo;

		if ( needsStorageResize ) {
			R_AllocateCopyTextureStorage( isCube, copyTarget,
				internalFormat != 0 ? internalFormat : GL_RGBA8, imageWidth,
				imageHeight, dataFormat != 0 ? dataFormat : GL_RGBA,
				dataType != 0 ? dataType : GL_UNSIGNED_BYTE );
			glTexParameterf( textureTarget, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
			glTexParameterf( textureTarget, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
			glTexParameterf( textureTarget, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
			glTexParameterf( textureTarget, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
		}

		glBindFramebuffer( GL_READ_FRAMEBUFFER, backEnd.renderTexture->GetDeviceHandle() );
		glReadBuffer( readAttachment );

		glBindFramebuffer( GL_DRAW_FRAMEBUFFER, copyFbo );
		glFramebufferTexture2D( GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, copyTarget, texnum, 0 );
		glDrawBuffer( GL_COLOR_ATTACHMENT0 );

		// glBlitFramebuffer obeys scissor state; ensure the copy is not clipped by a prior light scissor.
		const GLboolean scissorWasEnabled = glIsEnabled( GL_SCISSOR_TEST );
		GLint previousScissorBox[4] = { 0, 0, 0, 0 };
		if ( scissorWasEnabled ) {
			glGetIntegerv( GL_SCISSOR_BOX, previousScissorBox );
			glDisable( GL_SCISSOR_TEST );
		}

		glBlitFramebuffer( x, y, x + imageWidth, y + imageHeight, 0, 0, imageWidth, imageHeight, GL_COLOR_BUFFER_BIT, GL_NEAREST );
		if ( scissorWasEnabled ) {
			glScissor( previousScissorBox[0], previousScissorBox[1], previousScissorBox[2], previousScissorBox[3] );
			glEnable( GL_SCISSOR_TEST );
		}

		glFramebufferTexture2D( GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, copyTarget, 0, 0 );

		glBindFramebuffer( GL_READ_FRAMEBUFFER, previousReadFbo );
		glBindFramebuffer( GL_DRAW_FRAMEBUFFER, previousDrawFbo );
		glReadBuffer( previousReadBuffer );
		glDrawBuffer( previousDrawBuffer );
	} else {
		GLint previousReadFbo = 0;
		GLint previousReadBuffer = GL_BACK;
		glGetIntegerv( GL_READ_FRAMEBUFFER_BINDING, &previousReadFbo );
		glGetIntegerv( GL_READ_BUFFER, &previousReadBuffer );

		if ( readingFromRenderTexture ) {
			glBindFramebuffer( GL_READ_FRAMEBUFFER, backEnd.renderTexture->GetDeviceHandle() );
			glReadBuffer( readAttachment );
		} else {
			glBindFramebuffer( GL_READ_FRAMEBUFFER, 0 );
			glReadBuffer( GL_BACK );
		}

		const GLboolean scissorWasEnabled = glIsEnabled( GL_SCISSOR_TEST );
		GLint previousScissorBox[4] = { 0, 0, 0, 0 };
		if ( scissorWasEnabled ) {
			glGetIntegerv( GL_SCISSOR_BOX, previousScissorBox );
			glDisable( GL_SCISSOR_TEST );
		}

		// ES 3.0 constrains glCopyTexImage2D's internalformat more tightly than
		// desktop does. Allocating the level with glTexImage2D and then copying
		// into it needs only that the level exist and be format-compatible with
		// the read buffer, which is always true here.
		const bool useCopyTexImage = needsStorageResize && !isCube
			&& glConfig.backendCaps.profile != RENDERER_CONTEXT_PROFILE_ES;
		if ( useCopyTexImage ) {
			glCopyTexImage2D( copyTarget, 0, internalFormat != 0 ? internalFormat : GL_RGBA8, x, y, imageWidth, imageHeight, 0 );
		} else {
			if ( needsStorageResize ) {
				R_AllocateCopyTextureStorage( isCube, copyTarget,
					internalFormat != 0 ? internalFormat : GL_RGBA8, imageWidth,
					imageHeight, dataFormat != 0 ? dataFormat : GL_RGBA,
					dataType != 0 ? dataType : GL_UNSIGNED_BYTE );
			}
			glCopyTexSubImage2D( copyTarget, 0, 0, 0, x, y, imageWidth, imageHeight );
		}

		if ( scissorWasEnabled ) {
			glScissor( previousScissorBox[0], previousScissorBox[1], previousScissorBox[2], previousScissorBox[3] );
			glEnable( GL_SCISSOR_TEST );
		}

		glBindFramebuffer( GL_READ_FRAMEBUFFER, previousReadFbo );
		glReadBuffer( previousReadBuffer );

		if ( needsStorageResize ) {
			glTexParameterf( textureTarget, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
			glTexParameterf( textureTarget, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
			glTexParameterf( textureTarget, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
			glTexParameterf( textureTarget, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
		}
	}

//	backEnd.pc.c_copyFrameBuffer++;
	return true;
}

/*
====================
CopyDepthbuffer
====================
*/
bool idImage::CopyDepthbuffer( int x, int y, int imageWidth, int imageHeight,
		int cubeFace ) {
	const bool isCube = opts.textureType == TT_CUBIC;
	if ( imageWidth <= 0 || imageHeight <= 0
			|| ( isCube && ( cubeFace < 0 || cubeFace >= 6 ) )
			|| ( !isCube && cubeFace != 0 ) ) {
		return false;
	}
	const GLenum textureTarget = isCube ? GL_TEXTURE_CUBE_MAP_EXT : GL_TEXTURE_2D;
	const GLenum copyTarget = isCube
		? GL_TEXTURE_CUBE_MAP_POSITIVE_X_EXT + cubeFace : GL_TEXTURE_2D;
	R_BindTextureForDirectAccess( textureTarget, texnum );

	// The destination must hold depth-renderable storage: it gets attached to
	// GL_DEPTH_ATTACHMENT for the blit path and receives GL_DEPTH_COMPONENT
	// copies otherwise. Images that were ever allocated with a color format
	// (e.g. the legacy RGBA placeholder path) would make those operations fail
	// silently every frame, so respecify them as real depth here.
	const bool hasDepthStorage =
		internalFormat == GL_DEPTH_COMPONENT
		|| internalFormat == GL_DEPTH_COMPONENT16
		|| internalFormat == GL_DEPTH_COMPONENT24
		|| internalFormat == GL_DEPTH_COMPONENT32
		|| internalFormat == GL_DEPTH_COMPONENT32F
		|| internalFormat == GL_DEPTH24_STENCIL8
		|| internalFormat == GL_DEPTH32F_STENCIL8;
	if ( !hasDepthStorage ) {
		internalFormat = GL_DEPTH_COMPONENT24;
		dataFormat = GL_DEPTH_COMPONENT;
		dataType = GL_FLOAT;
		opts.format = FMT_DEPTH;
	}

	const bool needsStorageResize = !hasDepthStorage || ( opts.width != imageWidth ) || ( opts.height != imageHeight );
	opts.width = imageWidth;
	opts.height = imageHeight;

	const bool readingFromRenderTexture = ( backEnd.renderTexture != NULL ) && ( backEnd.renderTexture->GetDepthImage() != NULL );
	// A strict context may intentionally differ from archived r_multiSamples.
	// Unknown actual samples cannot safely select a default-depth copy path.
	const int defaultSamples = readingFromRenderTexture ? 0 : R_DefaultFramebufferSamples();
	if ( defaultSamples < 0 ) return false;
	const bool sourceDepthIsMSAA =
		readingFromRenderTexture
		? ( backEnd.renderTexture->GetDepthImage()->GetOpts().numMSAASamples > 1 )
		: ( defaultSamples > 1 );
	const bool canBlitDepth = ( GLEW_EXT_framebuffer_blit || GLEW_ARB_framebuffer_object || GLEW_VERSION_3_0 );

	// Prefer depth blits when sampling depth for SSAO, especially for MSAA sources.
	// glReadPixels from multisampled depth buffers is invalid on many drivers.
	if ( canBlitDepth && ( readingFromRenderTexture || sourceDepthIsMSAA ) ) {
		GLint previousReadFbo = 0;
		GLint previousDrawFbo = 0;
		GLint previousReadBuffer = GL_BACK;
		GLint previousDrawBuffer = GL_BACK;

		glGetIntegerv( GL_READ_FRAMEBUFFER_BINDING, &previousReadFbo );
		glGetIntegerv( GL_DRAW_FRAMEBUFFER_BINDING, &previousDrawFbo );
		glGetIntegerv( GL_READ_BUFFER, &previousReadBuffer );
		glGetIntegerv( GL_DRAW_BUFFER, &previousDrawBuffer );

		if ( r_copyDepthbufferFbo == 0 ) {
			glGenFramebuffers( 1, &r_copyDepthbufferFbo );
		}
		const GLuint copyDepthFbo = r_copyDepthbufferFbo;

		if ( needsStorageResize ) {
			R_AllocateCopyTextureStorage( isCube, copyTarget,
				internalFormat != 0 ? internalFormat : GL_DEPTH_COMPONENT24,
				imageWidth, imageHeight,
				dataFormat != 0 ? dataFormat : GL_DEPTH_COMPONENT,
				dataType != 0 ? dataType : GL_FLOAT );
			glTexParameterf( textureTarget, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
			glTexParameterf( textureTarget, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
			glTexParameterf( textureTarget, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
			glTexParameterf( textureTarget, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
		}

		if ( readingFromRenderTexture ) {
			glBindFramebuffer( GL_READ_FRAMEBUFFER, backEnd.renderTexture->GetDeviceHandle() );
		} else {
			glBindFramebuffer( GL_READ_FRAMEBUFFER, 0 );
			glReadBuffer( GL_BACK );
		}

		glBindFramebuffer( GL_DRAW_FRAMEBUFFER, copyDepthFbo );
		glFramebufferTexture2D( GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, copyTarget, texnum, 0 );
		glReadBuffer( GL_NONE );
		glDrawBuffer( GL_NONE );

		const GLboolean scissorWasEnabled = glIsEnabled( GL_SCISSOR_TEST );
		GLint previousScissorBox[4] = { 0, 0, 0, 0 };
		if ( scissorWasEnabled ) {
			glGetIntegerv( GL_SCISSOR_BOX, previousScissorBox );
			glDisable( GL_SCISSOR_TEST );
		}

		glBlitFramebuffer( x, y, x + imageWidth, y + imageHeight, 0, 0, imageWidth, imageHeight, GL_DEPTH_BUFFER_BIT, GL_NEAREST );

		if ( scissorWasEnabled ) {
			glScissor( previousScissorBox[0], previousScissorBox[1], previousScissorBox[2], previousScissorBox[3] );
			glEnable( GL_SCISSOR_TEST );
		}

		glFramebufferTexture2D( GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, copyTarget, 0, 0 );

		glBindFramebuffer( GL_READ_FRAMEBUFFER, previousReadFbo );
		glBindFramebuffer( GL_DRAW_FRAMEBUFFER, previousDrawFbo );
		glReadBuffer( previousReadBuffer );
		glDrawBuffer( previousDrawBuffer );
		return true;
	}

	if ( sourceDepthIsMSAA ) {
		static bool msaaDepthFallbackWarned = false;
		if ( !msaaDepthFallbackWarned ) {
			common->Warning( "CopyDepthbuffer: missing framebuffer blit support for MSAA depth resolve; using readback fallback" );
			msaaDepthFallbackWarned = true;
		}

		const int pixelCount = imageWidth * imageHeight;
		if ( pixelCount <= 0 ) {
			return false;
		}

		GLint previousReadFbo = 0;
		GLint previousDrawFbo = 0;
		GLint previousReadBuffer = GL_BACK;
		GLint previousDrawBuffer = GL_BACK;
		glGetIntegerv( GL_READ_FRAMEBUFFER_BINDING, &previousReadFbo );
		glGetIntegerv( GL_DRAW_FRAMEBUFFER_BINDING, &previousDrawFbo );
		glGetIntegerv( GL_READ_BUFFER, &previousReadBuffer );
		glGetIntegerv( GL_DRAW_BUFFER, &previousDrawBuffer );

		if ( readingFromRenderTexture ) {
			glBindFramebuffer( GL_READ_FRAMEBUFFER, backEnd.renderTexture->GetDeviceHandle() );
		} else {
			glBindFramebuffer( GL_READ_FRAMEBUFFER, 0 );
			glReadBuffer( GL_BACK );
		}

		const GLboolean scissorWasEnabled = glIsEnabled( GL_SCISSOR_TEST );
		GLint previousScissorBox[4] = { 0, 0, 0, 0 };
		if ( scissorWasEnabled ) {
			glGetIntegerv( GL_SCISSOR_BOX, previousScissorBox );
			glDisable( GL_SCISSOR_TEST );
		}

		static idList<float> resolvedDepthBuffer;
		resolvedDepthBuffer.SetNum( pixelCount, false );
		glReadPixels( x, y, imageWidth, imageHeight, GL_DEPTH_COMPONENT, GL_FLOAT, resolvedDepthBuffer.Ptr() );

		if ( scissorWasEnabled ) {
			glScissor( previousScissorBox[0], previousScissorBox[1], previousScissorBox[2], previousScissorBox[3] );
			glEnable( GL_SCISSOR_TEST );
		}

		// The readback buffer always holds GL_FLOAT depth values regardless of
		// the texture's preferred upload type.
		if ( needsStorageResize && !isCube ) {
			glTexImage2D( copyTarget, 0, internalFormat != 0 ? internalFormat : GL_DEPTH_COMPONENT24, imageWidth, imageHeight, 0,
				GL_DEPTH_COMPONENT, GL_FLOAT, resolvedDepthBuffer.Ptr() );
		} else {
			if ( needsStorageResize ) {
				R_AllocateCopyTextureStorage( true, copyTarget,
					internalFormat != 0 ? internalFormat : GL_DEPTH_COMPONENT24,
					imageWidth, imageHeight, GL_DEPTH_COMPONENT, GL_FLOAT );
			}
			glTexSubImage2D( copyTarget, 0, 0, 0, imageWidth, imageHeight,
				GL_DEPTH_COMPONENT, GL_FLOAT, resolvedDepthBuffer.Ptr() );
		}
		if ( needsStorageResize ) {
			glTexParameterf( textureTarget, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
			glTexParameterf( textureTarget, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
			glTexParameterf( textureTarget, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
			glTexParameterf( textureTarget, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
		}

		glBindFramebuffer( GL_READ_FRAMEBUFFER, previousReadFbo );
		glBindFramebuffer( GL_DRAW_FRAMEBUFFER, previousDrawFbo );
		glReadBuffer( previousReadBuffer );
		glDrawBuffer( previousDrawBuffer );
	} else {
		GLint previousReadFbo = 0;
		GLint previousReadBuffer = GL_BACK;
		glGetIntegerv( GL_READ_FRAMEBUFFER_BINDING, &previousReadFbo );
		glGetIntegerv( GL_READ_BUFFER, &previousReadBuffer );

		if ( readingFromRenderTexture ) {
			glBindFramebuffer( GL_READ_FRAMEBUFFER, backEnd.renderTexture->GetDeviceHandle() );
		} else {
			glBindFramebuffer( GL_READ_FRAMEBUFFER, 0 );
		}

		const GLboolean scissorWasEnabled = glIsEnabled( GL_SCISSOR_TEST );
		GLint previousScissorBox[4] = { 0, 0, 0, 0 };
		if ( scissorWasEnabled ) {
			glGetIntegerv( GL_SCISSOR_BOX, previousScissorBox );
			glDisable( GL_SCISSOR_TEST );
		}

		if ( needsStorageResize && !isCube ) {
			glCopyTexImage2D( copyTarget, 0, internalFormat != 0 ? internalFormat : GL_DEPTH_COMPONENT, x, y, imageWidth, imageHeight, 0 );
		} else {
			if ( needsStorageResize ) {
				R_AllocateCopyTextureStorage( true, copyTarget,
					internalFormat != 0 ? internalFormat : GL_DEPTH_COMPONENT,
					imageWidth, imageHeight,
					dataFormat != 0 ? dataFormat : GL_DEPTH_COMPONENT,
					dataType != 0 ? dataType : GL_FLOAT );
			}
			glCopyTexSubImage2D( copyTarget, 0, 0, 0, x, y, imageWidth, imageHeight );
		}

		if ( scissorWasEnabled ) {
			glScissor( previousScissorBox[0], previousScissorBox[1], previousScissorBox[2], previousScissorBox[3] );
			glEnable( GL_SCISSOR_TEST );
		}

		glBindFramebuffer( GL_READ_FRAMEBUFFER, previousReadFbo );
		glReadBuffer( previousReadBuffer );
	}

	//backEnd.pc.c_copyFrameBuffer++;
	return true;
}

/*
=============
RB_UploadScratchImage

if rows = cols * 6, assume it is a cube map animation
=============
*/
void idImage::UploadScratch( const byte * data, int cols, int rows ) {

	// if rows = cols * 6, assume it is a cube map animation
	if ( rows == cols * 6 ) {
		rows /= 6;
		const byte * pic[6];
		for ( int i = 0; i < 6; i++ ) {
			pic[i] = data + cols * rows * 4 * i;
		}

		if ( opts.textureType != TT_CUBIC || usage != TD_LOOKUP_TABLE_RGBA ) {
			GenerateCubeImage( pic, cols, TF_LINEAR, TD_LOOKUP_TABLE_RGBA );
			return;
		}
		if ( opts.width != cols || opts.height != rows ) {
			opts.width = cols;
			opts.height = rows;
			AllocImage();
		}
		SetSamplerState( TF_LINEAR, TR_CLAMP );
		for ( int i = 0; i < 6; i++ ) {
			SubImageUpload( 0, 0, 0, i, opts.width, opts.height, pic[i] );
		}

	} else {
		if ( opts.textureType != TT_2D || usage != TD_LOOKUP_TABLE_RGBA ) {
			GenerateImage( data, cols, rows, TF_LINEAR, TR_REPEAT, TD_LOOKUP_TABLE_RGBA );
			return;
		}
		if ( opts.width != cols || opts.height != rows ) {
			opts.width = cols;
			opts.height = rows;
			AllocImage();
		}
		SetSamplerState( TF_LINEAR, TR_REPEAT );
		SubImageUpload( 0, 0, 0, 0, opts.width, opts.height, data );
	}
}

/*
==================
StorageSize
==================
*/
int idImage::StorageSize() const {

	if ( !IsLoaded() ) {
		return 0;
	}
	int baseSize = opts.width * opts.height;
	if ( opts.numLevels > 1 ) {
		baseSize *= 4;
		baseSize /= 3;
	}
	baseSize *= BitsForFormat( opts.format );
	baseSize /= 8;
	// A cube map allocates all six faces under one idImage. Counting one face
	// made listImages report a sixth of what reflection probes and the lightgrid
	// actually cost, which is the difference between the total agreeing with the
	// driver's own accounting and being quietly low.
	if ( opts.textureType == TT_CUBIC ) {
		baseSize *= 6;
	}
	// A multisampled target stores every sample and has no mip chain.
	if ( opts.numMSAASamples > 1 ) {
		baseSize *= opts.numMSAASamples;
	}
	return baseSize;
}

/*
==================
Print
==================
*/
void idImage::Print() const {
	if ( generatorFunction ) {
		common->Printf( "F" );
	} else {
		common->Printf( " " );
	}

	switch ( opts.textureType ) {
		case TT_2D:
			common->Printf( " " );
			break;
		case TT_CUBIC:
			common->Printf( "C" );
			break;
		default:
			common->Printf( "<BAD TYPE:%i>", opts.textureType );
			break;
	}

	common->Printf( "%4i %4i ",	opts.width, opts.height );

	switch ( opts.format ) {
#define NAME_FORMAT( x ) case FMT_##x: common->Printf( "%-6s ", #x ); break;
		NAME_FORMAT( NONE );
		NAME_FORMAT( RGBA8 );
		NAME_FORMAT( XRGB8 );
		NAME_FORMAT( RGB565 );
		NAME_FORMAT( L8A8 );
		NAME_FORMAT( ALPHA );
		NAME_FORMAT( LUM8 );
		NAME_FORMAT( INT8 );
		NAME_FORMAT( DXT1 );
		NAME_FORMAT( DXT5 );
		NAME_FORMAT( BC7 );
		NAME_FORMAT( ETC2_RGB8 );
		NAME_FORMAT( ETC2_RGBA8 );
		NAME_FORMAT( EAC_RG11 );
		NAME_FORMAT( DEPTH );
		NAME_FORMAT( X16 );
		NAME_FORMAT( Y16_X16 );
		default:
			common->Printf( "<%3i>", opts.format );
			break;
	}

	switch( filter ) {
		case TF_DEFAULT:
			common->Printf( "mip  " );
			break;
		case TF_LINEAR:
			common->Printf( "linr " );
			break;
		case TF_NEAREST:
			common->Printf( "nrst " );
			break;
		default:
			common->Printf( "<BAD FILTER:%i>", filter );
			break;
	}

	switch ( repeat ) {
		case TR_REPEAT:
			common->Printf( "rept " );
			break;
		case TR_MIRRORED_REPEAT:
			common->Printf( "mrrr " );
			break;
		case TR_CLAMP_TO_ZERO:
			common->Printf( "zero " );
			break;
		case TR_CLAMP_TO_ZERO_ALPHA:
			common->Printf( "azro " );
			break;
		case TR_CLAMP:
			common->Printf( "clmp " );
			break;
		default:
			common->Printf( "<BAD REPEAT:%i>", repeat );
			break;
	}

	common->Printf( "%5i %4ik ", GetUseCount(), StorageSize() / 1024 );

	common->Printf( " %s\n", GetName() );
}

/*
===============
idImage::Reload
===============
*/
void idImage::Reload( bool force ) {
	// always regenerate functional images
	if ( generatorFunction ) {
		common->DPrintf( "regenerating %s.\n", GetName() );
		generatorFunction( this );
		return;
	}

	// persistent images are runtime render targets, not file-backed
	if ( opts.isPersistant ) {
		common->DPrintf( "reallocating persistent %s.\n", GetName() );
		DeriveOpts();
		AllocImage();
		return;
	}

	// check file times
	if ( !force ) {
		ID_TIME_T current = FILE_NOT_FOUND_TIMESTAMP;
		idStr currentSourceName = imgName;
		if ( cubeFiles != CF_2D ) {
			R_LoadCubeImages( imgName, cubeFiles, NULL, NULL, &current );
		} else {
			idStr sourceExtension;
			idStr sourceName = imgName;
			sourceName.ExtractFileExtension( sourceExtension );
			idStr preferredDDSName;
			ID_TIME_T preferredDDSFileTime = FILE_NOT_FOUND_TIMESTAMP;
			if ( idStr::Icmp( sourceExtension.c_str(), "dds" ) != 0 &&
				 R_ResolvePreferredDDSImageSource( imgName, preferredDDSName, &preferredDDSFileTime, true, NULL ) ) {
				ID_TIME_T originalSourceTime = FILE_NOT_FOUND_TIMESTAMP;
				if ( !fileSystem->InProductionMode() ) {
					R_LoadImageProgram( imgName, NULL, NULL, NULL, &originalSourceTime );
				}
				if ( R_IsPreferredDDSStale( preferredDDSName, preferredDDSFileTime, originalSourceTime ) ) {
					current = originalSourceTime;
				} else {
					current = preferredDDSFileTime;
					currentSourceName = preferredDDSName;
				}
			} else {
				// get the current values
				bool stockFallbackSelected = false;
				if ( !R_SelectMissingQ4StockImageSource( imgName, currentSourceName,
						current, stockFallbackSelected ) ) {
					R_LoadImageProgram( imgName, NULL, NULL, NULL, &current );
				}
			}
		}
		const bool sourceSelectionChanged = loadedSourceName.Length() == 0 || loadedSourceName.Icmp( currentSourceName ) != 0;
		if ( !sourceSelectionChanged && current <= sourceFileTime ) {
			return;
		}
	}

	common->DPrintf( "reloading %s.\n", GetName() );

	PurgeImage();

	// Load is from the front end, so the back end must be synced
	ActuallyLoadImage( false );
}

/*
========================
idImage::SetSamplerState
========================
*/
void idImage::SetSamplerState( textureFilter_t tf, textureRepeat_t tr ) {
	if ( tf == filter && tr == repeat ) {
		return;
	}
	filter = tf;
	repeat = tr;
	R_BindTextureForDirectAccess( ( opts.textureType == TT_CUBIC ) ? GL_TEXTURE_CUBE_MAP_EXT : GL_TEXTURE_2D, texnum );
	SetTexParameters();
}

