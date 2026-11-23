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
#include "DisplayPresentation.h"
#include "../imagetools/LegacyScopeMask.h"
#ifdef OPENQ4_RENDERER_VK_MODULE
#include "Vulkan/vk_Image.h"
#endif

#include <chrono>
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

idCVar image_lodbias(
	"image_lodbias",
	"0",
	CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT,
	"mip level bias for mipmapped material textures, in mips (applied in quarter-mip steps): negative samples sharper mips, positive blurrier ones; OpenGL ES ignores it",
	-2.0f,
	2.0f );

idCVar r_vkAnisotropicLodBias(
	"r_vkAnisotropicLodBias",
	"1",
	CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT,
	"Vulkan: scale of the mip bias that corrects a driver whose anisotropic filter samples blurrier mips than OpenGL's on the same GPU (NVIDIA's Vulkan driver; no other driver is corrected): 1 = the measured correction, 0 = off",
	0.0f,
	2.0f );

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
// Quake 4's own names and meanings. Retail compressed every colour texture
// and RXGB-compressed bump maps below its Ultra spec; openQ4 keeps the
// uncompressed Ultra behaviour by default. Only textures the engine decodes
// are affected: precompressed DDS (almost all of Quake 4's stock textures) and
// 2D/HUD art (images that may not be reduced) are never re-encoded. Meant for
// texture packs whose loose TGA or PNG files would otherwise sit in video memory
// at 32 bpp. Encoded once, then read from the generated image cache.
idCVar image_useCompression(
	"image_useCompression",
	"0",
	CVAR_RENDERER | CVAR_ARCHIVE | CVAR_INTEGER,
	"block-compress textures decoded from TGA/PNG/JPG/BC4/BC5 sources: 0 = keep uncompressed RGBA8, 1 = diffuse and specular to DXT1 (DXT5 when the image has alpha)",
	0,
	1 );
idCVar image_useNormalCompression(
	"image_useNormalCompression",
	"0",
	CVAR_RENDERER | CVAR_ARCHIVE | CVAR_INTEGER,
	"block-compress bump maps decoded from TGA/PNG/JPG/BC5 sources or built by image programs: 0 = keep uncompressed RGBA8, 2 = DXT5 RXGB as Quake 4 stores its own (1 reads as 2)",
	0,
	2 );
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
two can never drift apart. image_usePrecompressedTextures belongs to it: it
chooses the file a texture is read from (a precompressed DDS or its source
image), so a change reaches the GPU the same way, through a reload. Until it
joined the list only a full renderer restart applied it.
===============
*/
extern idCVar image_usePrecompressedTextures;

idCVar image_reloadWorkers( "image_reloadWorkers", "2", CVAR_RENDERER | CVAR_INTEGER,
	"worker threads that decode, resample and compress textures for a texture setting change; the main thread "
	"keeps the file reads and the upload. 0 decodes on the main thread", 0, 8 );
idCVar image_reloadFrameMsec( "image_reloadFrameMsec", "4", CVAR_RENDERER | CVAR_INTEGER,
	"milliseconds of texture reloading one frame may spend after a texture setting changes (at least one texture "
	"a frame); 0 reloads every affected texture in the frame of the change", 0, 1000 );

static bool R_ImageReductionCvarsChanged( bool clear ) {
	idCVar *const reductionCvars[] = {
		&image_picmip, &image_picmipFilter, &image_picmipMinSize,
		&image_downSize, &image_downSizeLimit,
		&image_downSizeSpecular, &image_downSizeSpecularLimit,
		&image_downSizeBump, &image_downSizeBumpLimit,
		&image_usePrecompressedTextures,
		&image_useCompression, &image_useNormalCompression
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
	idCVar *const samplerCvars[] = { &image_filter, &image_anisotropy, &r_vkAnisotropicLodBias };
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
every launch would see ten "changed" reduction cvars and reload every image for
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

Only the images a change reaches are reloaded: each file-backed image keeps the
reduction key its last load was built under (idImage::ReductionInputsChanged).
A preset that moves the bump limit reloads the bump maps, not the whole
catalogue, and the interface, fonts and nopicmip images, whose policy is empty
whatever the settings, are never touched by one.

A change made during a level load, or while a checked image-policy attempt owns
resource mutation, stays pending: the flags are left set and the first frame
after either reloads what is stale. Clearing them early lost the change for
every image the load kept resident (the interface, models referenced outside
the level), which kept the size or block format of the old settings until the
next vid_restart.
===============
*/
void idImageManager::CheckCvars() {
	const bool changed = R_ImageReductionCvarsChanged( false ) || R_ImageSamplerCvarsChanged( false )
		|| WantedLodBiasStep() != appliedLodBiasStep;
	if ( !changed && PendingReductionReloads() <= 0 ) {
		return;
	}
	if ( insideLevelLoad || R_ImagePolicyActive() ) {
		return;
	}
	ApplyPendingImageCvars( false );
}

/*
===============
idImageManager::ApplyPendingImageCvars

Takes a pending cvar change (queueing the images a reduction change reaches,
reapplying samplers for a sampling change) and then reloads queued images:
within image_reloadFrameMsec from CheckCvars, all of them at the end of a level
load, where the loading screen already holds the frame.
===============
*/
void idImageManager::ApplyPendingImageCvars( bool finishReloads ) {
	const bool reductionChanged = R_ImageReductionCvarsChanged( true );
	const bool samplerChanged = R_ImageSamplerCvarsChanged( true );

	if ( reductionChanged ) {
		ReloadReductionChangedImages();
	}

	// The upscaling LOD bias follows the scene scale, which dynamic resolution
	// moves during play, so it changes samplers without a console line.
	const int wantedLodBiasStep = WantedLodBiasStep();
	const bool lodBiasChanged = wantedLodBiasStep != appliedLodBiasStep;
	if ( lodBiasChanged ) {
		appliedLodBiasStep = wantedLodBiasStep;
		lodBiasChanges++;
	}

	if ( samplerChanged ) {
		common->Printf( "Texture sampling changed, updating samplers...\n" );
	}
	if ( samplerChanged || lodBiasChanged ) {
		for ( int i = 0; i < images.Num(); i++ ) {
			idImage *image = images[i];
			if ( image != NULL && image->IsLoaded() && image->GetFilter() == TF_DEFAULT ) {
				image->RefreshSamplerState();
			}
		}
	}

	if ( PendingReductionReloads() > 0 ) {
		ContinueReductionReloads( finishReloads ? 0.0 : static_cast<double>( Max( 0, image_reloadFrameMsec.GetInteger() ) ) );
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

/*
===============
idImageManager::WantedLodBiasStep

image_lodbias plus the temporal-upscaling bias, in quarter mips.
===============
*/
int idImageManager::WantedLodBiasStep() const {
	const float userSteps = image_lodbias.GetFloat() * TEMPORAL_LOD_BIAS_STEPS_PER_MIP;
	const int userStep = static_cast<int>( userSteps < 0.0f ? userSteps - 0.5f : userSteps + 0.5f );
	return idMath::ClampInt( -3 * TEMPORAL_LOD_BIAS_STEPS_PER_MIP, 2 * TEMPORAL_LOD_BIAS_STEPS_PER_MIP,
		userStep + requestedLodBiasStep );
}

/*
===============
idImage::SamplerLodBiasStep

The LOD bias reaches mipmapped material textures drawn in the 3D scene. The
interface and fonts are drawn at the output resolution, and render targets
(names starting with an underscore) are never minified by the scene scale, so
they keep an unbiased sampler.
===============
*/
int idImage::SamplerLodBiasStep() const {
	if ( filter != TF_DEFAULT || usage == TD_FONT || globalImages == NULL ) {
		return 0;
	}
	const int step = globalImages->AppliedLodBiasStep();
	if ( step == 0 ) {
		return 0;
	}
	const char *name = imgName.c_str();
	if ( name[0] == '_' || R_IsQ4PresentationImageNamespace( name ) ) {
		return 0;
	}
	return step;
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
	int64	size;		// bytes, or msec in the level-load profile
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
			// Each prediction opens the image's file and reads its header: a
			// level's worth takes seconds on a cold disk.
			common->LoadingCheckpoint();
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
				// A worker is reading this file. The loading screen keeps
				// presenting meanwhile, outside the lock the workers take.
				while ( !workDone.wait_for( lock, std::chrono::milliseconds( 2 ), [&entry] { return entry.done; } ) ) {
					lock.unlock();
					common->LoadingCheckpoint();
					lock.lock();
				}
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
	int		i;
	int64	partialSize;
	idImage	*image;
	int64	totalSize;	// 64-bit: a texture pack passes 2 GiB long before the list ends
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
		common->Printf( "usage: listImages [ sorted | namesort | uncompressed | unloaded | duplicated | oversized ]\n" );
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

static void R_StopImageDecodeWorkers();

/*
===============
PurgeAllImages
===============
*/
void idImageManager::PurgeAllImages() {
	R_RenderThread_Sync( "image registry" );
	// the reload after a purge rebuilds every image under the settings in effect
	ClearReductionReloads();
	R_StopImageDecodeWorkers();
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
R_TestLayeredImages_f

testLayeredImages: builds volumes (TT_3D) and 2D texture arrays (TT_2D_ARRAY)
in several formats and sizes, uploads a known pattern to every slice or layer
of every level and reads each level back from the GPU unconverted
(idImage::ReadLevelBytes), on whichever renderer is running. A volume's depth
halves with its levels, an array's layers do not; odd and non-power-of-two
sizes check the level arithmetic. A block-format volume is tried only where
the renderer can hold one (R_ImageFormatAllowedInVolume).
===============
*/
static void R_TestLayeredImages_f( const idCmdArgs &args ) {
	struct layeredCase_t {
		const char *		name;
		textureType_t		type;
		textureFormat_t		format;
		int					width, height, depth, levels;
	};
	static const layeredCase_t cases[] = {
		{ "volume RGBA8 8x8x8, 4 levels",		TT_3D,			FMT_RGBA8,		8, 8, 8, 4 },
		{ "volume LUM8 6x5x3",					TT_3D,			FMT_LUM8,		6, 5, 3, 1 },
		{ "volume RGBA16F 4x4x4, 3 levels",		TT_3D,			FMT_RGBA16F,	4, 4, 4, 3 },
		{ "volume DXT1 8x8x4, 2 levels",		TT_3D,			FMT_DXT1,		8, 8, 4, 2 },
		{ "array RGBA8 8x8, 5 layers, 4 levels",	TT_2D_ARRAY,	FMT_RGBA8,		8, 8, 5, 4 },
		{ "array RGBA16F 7x3, 2 layers",		TT_2D_ARRAY,	FMT_RGBA16F,	7, 3, 2, 1 },
		{ "array DXT1 8x8, 3 layers, 2 levels",	TT_2D_ARRAY,	FMT_DXT1,		8, 8, 3, 2 },
	};
	int passed = 0, run = 0;
	for ( int c = 0; c < (int)( sizeof( cases ) / sizeof( cases[0] ) ); c++ ) {
		const layeredCase_t &test = cases[c];
		const int blockBytes = BytesPerBlockForFormat( test.format );
		if ( blockBytes > 0 && !ImageTools_GetCompressionCaps().textureCompressionAvailable ) {
			common->Printf( "testLayeredImages: %s: skipped, this renderer samples no S3TC\n", test.name );
			continue;
		}
		if ( test.type == TT_3D && !R_ImageFormatAllowedInVolume( test.format ) ) {
			common->Printf( "testLayeredImages: %s: skipped, this renderer holds no such volume\n", test.name );
			continue;
		}
		run++;
		idImageOpts opts;
		opts.textureType = test.type;
		opts.format = test.format;
		opts.width = test.width;
		opts.height = test.height;
		opts.depth = test.depth;
		opts.numLevels = test.levels;
		idImage *image = globalImages->ScratchImage( va( "_testLayeredImage%d", c ), &opts, TF_NEAREST, TR_CLAMP, TD_DEFAULT );
		if ( image == NULL || !image->IsLoaded() ) {
			common->Printf( "testLayeredImages: %s: FAILED, not allocated\n", test.name );
			continue;
		}
		int levelsSame = 0;
		for ( int level = 0; level < test.levels; level++ ) {
			const int w = Max( 1, test.width >> level ), h = Max( 1, test.height >> level );
			const int d = R_ImageLevelDepth( test.type, test.depth, level );
			const int sliceBytes = blockBytes > 0 ? ( ( w + 3 ) >> 2 ) * ( ( h + 3 ) >> 2 ) * blockBytes : w * h * BitsForFormat( test.format ) / 8;
			idList<byte> expected;
			expected.SetNum( sliceBytes * d );
			for ( int i = 0; i < expected.Num(); i++ ) {
				// distinct per level, slice and byte; RGBA16F bytes stay finite
				// halves (exponent below 31) so no driver canonicalises a NaN
				byte value = (byte)( i * 37 + level * 101 + ( i / sliceBytes ) * 53 + c * 11 );
				if ( test.format == FMT_RGBA16F && ( i & 1 ) ) {
					value &= 0x3f;
				}
				expected[i] = value;
			}
			for ( int z = 0; z < d; z++ ) {
				image->SubImageUpload( level, 0, 0, z, w, h, expected.Ptr() + z * sliceBytes );
			}
			idList<byte> gpu;
			const bool read = image->ReadLevelBytes( level, gpu );
			const bool same = read && gpu.Num() == expected.Num() && memcmp( gpu.Ptr(), expected.Ptr(), expected.Num() ) == 0;
			levelsSame += same ? 1 : 0;
			if ( !same ) {
				common->Printf( "  level %d (%dx%dx%d): %s\n", level, w, h, d,
					!read ? "no readback" : va( "%d bytes read, %d expected%s", gpu.Num(), expected.Num(), gpu.Num() == expected.Num() ? ", contents differ" : "" ) );
			}
		}
		image->PurgeImage();
		const bool ok = levelsSame == test.levels;
		passed += ok ? 1 : 0;
		common->Printf( "testLayeredImages: %s: %d of %d levels identical bit for bit, %s\n", test.name, levelsSame, test.levels, ok ? "passed" : "FAILED" );
	}
	common->Printf( "testLayeredImages: %d of %d cases passed%s\n", passed, run, passed == run ? "" : ", FAILED" );
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
	cmdSystem->AddCommand( "imageDDSSelfTest", R_ImageDDSSelfTest_f, CMD_FL_RENDERER, "validates DDS naming, BC7 and BC6H metadata handling and DXT3 decoding" );
	cmdSystem->AddCommand( "combineCubeImages", R_CombineCubeImages_f, CMD_FL_RENDERER, "combines six images for roq compression" );
	cmdSystem->AddCommand( "testLayeredImages", R_TestLayeredImages_f, CMD_FL_RENDERER, "uploads volumes and texture arrays and reads every level back from the GPU" );

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
	ClearReductionReloads();
	R_StopImageDecodeWorkers();
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
// A renderer device that failed and waits for the restart its backend queued
// (a lost Vulkan device) presents nothing until then. A device that is simply
// not up yet (startup, a dedicated server) reports a pending or unavailable
// outcome instead of a failure.
static bool R_RendererDeviceAwaitsRestart() {
	renderDisplayPresentation_t presentation{};
	R_GetDisplayPresentation( &presentation );
	return presentation.available == 0 && presentation.outcome >= RDP_INIT_FAILED;
}

/*
===============
idImageManager::ReloadReductionChangedImages

Queues the resident file-backed images whose reduction key moved, in name
order; ContinueReductionReloads reloads them. Generated and persistent images
are built from no file and keep their content. Purged and never-loaded images
are left alone; they load under the new settings when they are next bound.

The reload is spread across frames because it blocked the frame it ran in:
1.4 s on OpenGL for the 318 normal maps of Air Defense 1, 4 to 7 s for every
texture. Until an image's turn it keeps its old pixels, which are complete and
valid, so a frame shows a mix of old and new sizes and never a missing
texture. The queue holds the images, not the change: each one is checked
again at its turn, so one loaded in the meantime (a level load, a bind, a
device restart) or brought back to its key by a second change is skipped, and
a second change before the queue drains simply builds a new queue.
Returns the number queued.

The level load's file prefetch is not used: right after a load the files sit
in the operating system's cache, and its worker threads made a 318-image reload
slower, 1.0 to 3.1 s against 1.3 to 1.5 s without them (OpenGL, Air Defense 1,
a busy machine).
===============
*/
// Vulkan stores an image that did not fit in video memory without its top
// levels. A reduction elsewhere may free the room for them, so such an image is
// reloaded by any reduction change, as every image was before.
static bool R_ImageHeldBelowRequest( idImage *image ) {
#ifdef OPENQ4_RENDERER_VK_MODULE
	const vkImageEntry_t *entry = VK_Image_GetEntry( image->GetDeviceHandle() );
	return entry != NULL && entry->droppedLevels > 0;
#else
	(void)image;
	return false;
#endif
}

static double R_ReductionReloadClockMsec() {
	return std::chrono::duration<double, std::milli>( std::chrono::steady_clock::now().time_since_epoch() ).count();
}

// Scratch pages and render targets are authored in memory, so there is nothing
// on disk to rebuild them from. idImage::Reload answers a persistent image by
// reallocating it with no data, which is correct after a device loss and
// destructive at any other time.
static bool R_ImageNeedsReductionReload( idImage *image ) {
	if ( image == NULL || !image->IsFileBacked() || R_IsMutableRenderImage( image ) || !image->IsLoaded() ) {
		return false;
	}
	if ( !R_ImagePolicyShouldReload( image ) ) {
		return false;
	}
	return image->ReductionInputsChanged() || R_ImageHeldBelowRequest( image );
}

// The one texture that took longest, for the completion line: a single texture
// larger than the frame budget sets the longest frame the reload can cost.
static idStr reductionSlowestName;
static double reductionSlowestMsec = 0.0;
// Where the reload's time went, from ActuallyLoadImage's phase accounting:
// probes, generated-cache reads, source reads and decoding (direct DDS reads
// included), and the GPU upload. Only the upload must stay on the renderer thread.
static imageLoadPhaseTimings_t reductionPhases;
// main-thread time by kind of step: loads finished inline, deferrals, publications
static double reductionInlineMsec = 0.0, reductionDeferMsec = 0.0, reductionPublishMsec = 0.0;
static imageLoadPhaseTimings_t reductionSlowestPhases;

static void R_AddPhaseDelta( imageLoadPhaseTimings_t &total, const imageLoadPhaseTimings_t &before,
		const imageLoadPhaseTimings_t &after ) {
	total.probeMsec += after.probeMsec - before.probeMsec;
	total.generatedMsec += after.generatedMsec - before.generatedMsec;
	total.decodeMsec += after.decodeMsec - before.decodeMsec;
	total.uploadMsec += after.uploadMsec - before.uploadMsec;
	total.probeCount += after.probeCount - before.probeCount;
	total.generatedCount += after.generatedCount - before.generatedCount;
	total.decodeCount += after.decodeCount - before.decodeCount;
	total.uploadCount += after.uploadCount - before.uploadCount;
}

/*
===============
idImageDecodeWorkers

The threads that run imageDecodeJob_t for the settings reload. Started when a
reload first defers a decode and stopped when its queue drains, at shutdown and
by PurgeAllImages, so no thread idles through play. A job holds no image
pointer; the main thread publishes or drops it. Each worker takes the main
thread's floating-point control word, so its resampling and compression round
as an inline decode would.
===============
*/
#if defined( _M_X64 ) || defined( _M_IX86 ) || defined( __x86_64__ ) || defined( __i386__ )
#include <xmmintrin.h>
#define OPENQ4_IMAGE_WORKER_MXCSR 1
#endif
#if defined( __linux__ )
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>
#elif defined( __APPLE__ )
#include <pthread.h>
#endif

// Below the main thread, which reads the files the decodes wait for. At equal
// priority two busy decoders on a loaded machine made the main thread's file
// lookups three to five times slower, which cost more than the decode saved.
static void R_LowerImageWorkerPriority() {
#if defined( _WIN32 )
	SetThreadPriority( GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL );
#elif defined( __linux__ )
	setpriority( PRIO_PROCESS, static_cast<id_t>( syscall( SYS_gettid ) ), 10 );
#elif defined( __APPLE__ )
	pthread_set_qos_class_self_np( QOS_CLASS_UTILITY, 0 );
#endif
}

class idImageDecodeWorkers {
public:
	~idImageDecodeWorkers() {
		// Shutdown stops the workers. Joining from a static destructor can wait
		// on the loader lock while a module unloads, so a survivor is let go.
		for ( std::thread &worker : workers ) {
			if ( worker.joinable() ) {
				worker.detach();
			}
		}
	}

	void EnsureStarted( int count ) {
		if ( count <= 0 || static_cast<int>( workers.size() ) == count ) {
			return;
		}
		Stop();
#ifdef OPENQ4_IMAGE_WORKER_MXCSR
		controlWord = _mm_getcsr();
#endif
		stopping = false;
		for ( int i = 0; i < count; i++ ) {
			workers.emplace_back( &idImageDecodeWorkers::Work, this );
		}
	}

	void Submit( const std::shared_ptr<imageDecodeJob_t> &job ) {
		{
			std::lock_guard<std::mutex> lock( mutex );
			queue.push_back( job );
		}
		ready.notify_one();
	}

	// Blocks until the job is done. Only while the workers run.
	void WaitFor( const std::shared_ptr<imageDecodeJob_t> &job ) {
		std::unique_lock<std::mutex> lock( mutex );
		done.wait( lock, [&] { return job->state.load() == imageDecodeJob_t::DONE || workers.empty(); } );
	}

	// Drops queued jobs, lets the running ones finish and joins every worker.
	void Stop() {
		{
			std::lock_guard<std::mutex> lock( mutex );
			stopping = true;
			for ( const std::shared_ptr<imageDecodeJob_t> &job : queue ) {
				job->decoded = false;
				job->state.store( imageDecodeJob_t::DONE );
			}
			queue.clear();
		}
		ready.notify_all();
		for ( std::thread &worker : workers ) {
			if ( worker.joinable() ) {
				worker.join();
			}
		}
		workers.clear();
		done.notify_all();
	}

	bool Running() const { return !workers.empty(); }

private:
	void Work() {
#ifdef OPENQ4_IMAGE_WORKER_MXCSR
		_mm_setcsr( controlWord );
#endif
		R_LowerImageWorkerPriority();
		for ( ;; ) {
			std::shared_ptr<imageDecodeJob_t> job;
			{
				std::unique_lock<std::mutex> lock( mutex );
				ready.wait( lock, [this] { return stopping || !queue.empty(); } );
				if ( queue.empty() ) {
					return;
				}
				job = queue.front();
				queue.pop_front();
				job->state.store( imageDecodeJob_t::RUNNING );
			}
			R_RunImageDecodeJob( *job );
			{
				std::lock_guard<std::mutex> lock( mutex );
				job->state.store( imageDecodeJob_t::DONE );
			}
			done.notify_all();
		}
	}

	std::vector<std::thread>						workers;
	std::deque<std::shared_ptr<imageDecodeJob_t>>	queue;
	std::mutex										mutex;
	std::condition_variable							ready;
	std::condition_variable							done;
	bool											stopping = false;
	unsigned int									controlWord = 0;
};

static idImageDecodeWorkers imageDecodeWorkers;

static void R_StopImageDecodeWorkers() {
	imageDecodeWorkers.Stop();
}

struct reductionInFlight_t {
	idImage *							image;
	std::shared_ptr<imageDecodeJob_t>	job;
};
// Deferred images in the order they were deferred, published in the same order.
static std::deque<reductionInFlight_t> reductionInFlight;
// Recorded source bytes a deferral may hold at once, and deferrals per worker.
static const size_t REDUCTION_IN_FLIGHT_BYTES = size_t( 256 ) << 20;
static const int REDUCTION_IN_FLIGHT_PER_WORKER = 3;

static int R_ReductionWorkerCount() {
	const int requested = Max( 0, image_reloadWorkers.GetInteger() );
	if ( requested == 0 ) {
		return 0;
	}
	const unsigned int hardware = std::thread::hardware_concurrency();
	const int available = hardware > 1 ? static_cast<int>( hardware ) - 1 : 1;
	return Min( requested, available );
}

static size_t R_ReductionInFlightBytes() {
	size_t bytes = 0;
	for ( const reductionInFlight_t &entry : reductionInFlight ) {
		if ( entry.job->state.load() != imageDecodeJob_t::DONE ) {
			bytes += entry.job->tape.bytes;
		}
	}
	return bytes;
}

int idImageManager::PendingReductionReloads() const {
	return reductionReloads.Num() - reductionReloadNext + static_cast<int>( reductionInFlight.size() );
}

void idImageManager::ClearReductionReloads() {
	// a worker still running a dropped job finishes it into nothing
	reductionInFlight.clear();
	reductionSlowestName.Clear();
	reductionSlowestMsec = 0.0;
	reductionPhases.Clear();
	reductionSlowestPhases.Clear();
	reductionInlineMsec = reductionDeferMsec = reductionPublishMsec = 0.0;
	reductionReloads.Clear();
	reductionReloadNext = 0;
	reductionReloadStats = reductionReloadStats_t();
}

int idImageManager::ReloadReductionChangedImages() {
	if ( !R_ImagePolicyOperationAllowed() ) {
		return 0;
	}
	R_RenderThread_Sync( "image registry" );
	// a reload exists to observe the files in place now; never let it consult
	// (or leave behind) memoized probe results
	R_SetDDSProbeCacheActive( false );

	if ( PendingReductionReloads() > 0 ) {
		common->Printf( "Texture reduction changed again: %d queued textures are checked again\n", PendingReductionReloads() );
	}
	ClearReductionReloads();
	idList<sortedImage_t> stale;
	int unaffected = 0;
	for ( int i = 0; i < images.Num(); i++ ) {
		idImage *image = images[i];
		if ( R_ImageNeedsReductionReload( image ) ) {
			sortedImage_t &entry = stale.Alloc();
			entry.image = image;
			entry.size = 0;
			entry.index = i;
		} else if ( image != NULL && image->IsFileBacked() && image->IsLoaded() && !R_IsMutableRenderImage( image ) ) {
			unaffected++;
		}
	}
	if ( stale.Num() > 1 ) {
		qsort( stale.Ptr(), stale.Num(), sizeof( sortedImage_t ), R_QsortImageName );
	}
	reductionReloads.SetNum( stale.Num() );
	for ( int i = 0; i < stale.Num(); i++ ) {
		reductionReloads[i] = stale[i].image;
	}
	reductionReloadStats.startMsec = R_ReductionReloadClockMsec();
	reductionReloadStats.unaffected = unaffected;
	common->Printf( "Texture reduction changed: %d textures to reload, %d unaffected\n", stale.Num(), unaffected );
	return stale.Num();
}

bool idImageManager::ContinueReductionReloads( double budgetMsec ) {
	if ( PendingReductionReloads() <= 0 ) {
		return false;
	}
	if ( !R_ImagePolicyOperationAllowed() ) {
		return true;
	}
	// a failed device reloads everything when it restarts
	if ( R_RendererDeviceAwaitsRestart() ) {
		common->Printf( "Texture reduction: %d queued textures left for the renderer restart\n", PendingReductionReloads() );
		ClearReductionReloads();
		imageDecodeWorkers.Stop();
		return false;
	}
	R_RenderThread_Sync( "image registry" );

	const bool finishing = budgetMsec <= 0.0;
	const int workerCount = R_ReductionWorkerCount();
	const size_t inFlightLimit = static_cast<size_t>( Max( 1, workerCount ) * REDUCTION_IN_FLIGHT_PER_WORKER );
	const double frameStart = R_ReductionReloadClockMsec();
	int workThisFrame = 0;
	const auto overBudget = [&]() {
		return workThisFrame > 0 && !finishing && R_ReductionReloadClockMsec() - frameStart >= budgetMsec;
	};
	// One step of main-thread work on one image, timed for the completion line.
	double lastStepMsec = 0.0;
	const auto timed = [&]( idImage *image, const auto &step ) {
		const double imageStart = R_ReductionReloadClockMsec();
		const imageLoadPhaseTimings_t phasesBefore = imageLoadPhaseTimings;
		step();
		const double imageMsec = R_ReductionReloadClockMsec() - imageStart;
		lastStepMsec = imageMsec;
		R_AddPhaseDelta( reductionPhases, phasesBefore, imageLoadPhaseTimings );
		if ( imageMsec > reductionSlowestMsec ) {
			reductionSlowestMsec = imageMsec;
			reductionSlowestName = image->GetName();
			reductionSlowestPhases.Clear();
			R_AddPhaseDelta( reductionSlowestPhases, phasesBefore, imageLoadPhaseTimings );
		}
		workThisFrame++;
	};

	for ( ;; ) {
		// Publish what the workers finished, in the order it was deferred.
		while ( !reductionInFlight.empty() && !overBudget() ) {
			if ( reductionInFlight.front().job->state.load() != imageDecodeJob_t::DONE ) {
				if ( !finishing || !imageDecodeWorkers.Running() ) {
					break;
				}
				imageDecodeWorkers.WaitFor( reductionInFlight.front().job );
				continue;
			}
			const reductionInFlight_t entry = reductionInFlight.front();
			reductionInFlight.pop_front();
			if ( !R_ImageNeedsReductionReload( entry.image ) ) {
				reductionReloadStats.skipped++;
				continue;
			}
			if ( entry.job->decoded ) {
				reductionReloadStats.workerDecoded++;
				reductionReloadStats.workerMsec += entry.job->workerMsec;
				reductionReloadStats.longestWorkerMsec = Max( reductionReloadStats.longestWorkerMsec, entry.job->workerMsec );
			}
			bool published = false;
			timed( entry.image, [&]() { published = entry.image->PublishDecodeJob( entry.job ); } );
			if ( published ) {
				reductionReloadStats.workerPublished++;
			}
			reductionPublishMsec += lastStepMsec;
			reductionReloadStats.reloaded++;
		}

		// Start the next images: a load that needs no decode (a DDS, the
		// generated cache) finishes here; a 2D decode goes to a worker.
		while ( reductionReloadNext < reductionReloads.Num() && !overBudget()
				&& ( workerCount == 0 || reductionInFlight.size() < inFlightLimit )
				&& R_ReductionInFlightBytes() < REDUCTION_IN_FLIGHT_BYTES ) {
			idImage *image = reductionReloads[ reductionReloadNext++ ];
			if ( !R_ImageNeedsReductionReload( image ) ) {
				reductionReloadStats.skipped++;
				continue;
			}
			if ( workerCount == 0 ) {
				timed( image, [&]() { image->Reload( true ); } );
				reductionInlineMsec += lastStepMsec;
				reductionReloadStats.reloaded++;
				continue;
			}
			std::shared_ptr<imageDecodeJob_t> job;
			bool deferred = false;
			timed( image, [&]() { deferred = image->ReloadOrDefer( job ); } );
			( deferred ? reductionDeferMsec : reductionInlineMsec ) += lastStepMsec;
			if ( deferred ) {
				imageDecodeWorkers.EnsureStarted( workerCount );
				imageDecodeWorkers.Submit( job );
				reductionInFlight.push_back( { image, job } );
				reductionReloadStats.deferred++;
			} else {
				reductionReloadStats.reloaded++;
			}
		}

		if ( !finishing || PendingReductionReloads() <= 0 ) {
			break;
		}
		if ( !reductionInFlight.empty() && !imageDecodeWorkers.Running() ) {
			// nothing left to run them: load them inline
			for ( reductionInFlight_t &entry : reductionInFlight ) {
				entry.job->decoded = false;
				entry.job->state.store( imageDecodeJob_t::DONE );
			}
		}
	}
	const double frameMsec = R_ReductionReloadClockMsec() - frameStart;
	reductionReloadStats.workMsec += frameMsec;
	reductionReloadStats.longestFrameMsec = Max( reductionReloadStats.longestFrameMsec, frameMsec );
	reductionReloadStats.frames++;

	if ( PendingReductionReloads() > 0 ) {
		return true;
	}
	imageDecodeWorkers.Stop();
	const double totalMsec = R_ReductionReloadClockMsec() - reductionReloadStats.startMsec;
	common->Printf( "Texture reduction applied: reloaded %d textures over %d frames in %.0f ms "
		"(%.0f ms of loading, at most %.1f ms in one frame), %d already current, %d unaffected\n",
		reductionReloadStats.reloaded, reductionReloadStats.frames, totalMsec, reductionReloadStats.workMsec,
		reductionReloadStats.longestFrameMsec, reductionReloadStats.skipped, reductionReloadStats.unaffected );
	common->Printf( "Texture reduction workers: %d of %d textures deferred, %d decoded by a worker, %d published from it, %.0f ms of worker decoding (longest %.1f ms)\n",
		reductionReloadStats.deferred, reductionReloadStats.reloaded, reductionReloadStats.workerDecoded,
		reductionReloadStats.workerPublished, reductionReloadStats.workerMsec, reductionReloadStats.longestWorkerMsec );
	common->Printf( "Texture reduction main thread: %.0f ms loading inline, %.0f ms deferring, %.0f ms publishing\n",
		reductionInlineMsec, reductionDeferMsec, reductionPublishMsec );
	common->Printf( "Texture reduction phases: probe %.0f ms (%d), cache %.0f ms (%d), read and decode %.0f ms (%d), upload %.0f ms (%d)\n",
		reductionPhases.probeMsec, reductionPhases.probeCount, reductionPhases.generatedMsec, reductionPhases.generatedCount,
		reductionPhases.decodeMsec, reductionPhases.decodeCount, reductionPhases.uploadMsec, reductionPhases.uploadCount );
	if ( reductionSlowestName.Length() > 0 ) {
		common->Printf( "Texture reduction: slowest main-thread step %s, %.1f ms (probe %.1f, cache %.1f, read and decode %.1f, upload %.1f)\n",
			reductionSlowestName.c_str(), reductionSlowestMsec, reductionSlowestPhases.probeMsec,
			reductionSlowestPhases.generatedMsec, reductionSlowestPhases.decodeMsec, reductionSlowestPhases.uploadMsec );
	}
	ClearReductionReloads();
	return false;
}

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

	int deferredToRestart = 0;
	for ( int i = 0; i < pendingImages.Num(); i++ ) {
		if ( pacifier ) {
			//common->UpdateLevelLoadPacifier();
		}
		// The restart that recovers a failed device reloads every level image,
		// so decoding the rest now only spends minutes on textures nothing can
		// hold (#231: a device lost early in a 300-second texture load).
		if ( R_RendererDeviceAwaitsRestart() ) {
			deferredToRestart = pendingImages.Num() - i;
			break;
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

	if ( deferredToRestart > 0 ) {
		common->Printf( "%d level images left for the renderer restart to load\n", deferredToRestart );
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
				static_cast<int>( pendingImages[ i ].size ),
				static_cast<double>( pendingImages[ i ].image->StorageSize() ) / ( 1024.0 * 1024.0 ),
				pendingImages[ i ].image->GetName() );
		}
	}

	return pendingImages.Num() - deferredToRestart;
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

	// A texture setting changed before or during the load, and the images the
	// load kept still carry the old one. Finish them now, behind the loading
	// screen, rather than over the first frames of play.
	if ( ( R_ImageReductionCvarsChanged( false ) || R_ImageSamplerCvarsChanged( false ) || PendingReductionReloads() > 0 )
			&& !R_ImagePolicyActive() ) {
		ApplyPendingImageCvars( true );
	}
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
	idFile *f = fileSystem->OpenFileWrite( mi->filebase + "_images.txt" );
	if ( !f ) {
		return;
	}

	// One size per image, sorted once: the old exchange sort asked every image
	// for its size O(n^2) times, millions of calls for a loaded level.
	idList<sortedImage_t> sorted;
	sorted.SetNum( images.Num() );
	for ( int i = 0; i < images.Num(); i++ ) {
		sorted[i].image = images[i];
		sorted[i].size = images[i]->StorageSize();
		sorted[i].index = i;
	}
	if ( sorted.Num() > 1 ) {
		qsort( sorted.Ptr(), sorted.Num(), sizeof( sortedImage_t ), R_QsortImageSizes );
	}

	int64 total = 0;
	for ( int i = 0; i < sorted.Num(); i++ ) {
		total += sorted[i].size;
		f->Printf( "%14lld %3i %s\n", (long long)sorted[i].size, sorted[i].image->refCount, sorted[i].image->GetName() );
	}

	// MemInfo_t is shared with the engine and stays an int; saturate it rather
	// than wrap negative once a texture pack passes 2 GiB
	mi->imageAssetsTotal = (int)Min( total, (int64)0x7fffffff );

	f->Printf( "\nTotal image bytes allocated: %lld\n", (long long)total );
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
