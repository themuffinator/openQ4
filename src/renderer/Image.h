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

/*
====================================================================

IMAGE

idImage have a one to one correspondance with GL/DX/GCM textures.

No texture is ever used that does not have a corresponding idImage.

====================================================================
*/

#include "RendererConsumedPolicy.h"

static const int	MAX_TEXTURE_LEVELS = 14;

// How is this texture used?  Determines the storage and color format
typedef enum {
	TD_SPECULAR,			// may be compressed, and always zeros the alpha channel
	TD_DIFFUSE,				// may be compressed
	TD_DEFAULT,				// generic RGBA texture (particles, etc...)
	TD_BUMP,				// may be compressed with 8 bit lookup
	TD_FONT,				// Font image
	TD_LIGHT,				// Light image
	TD_LIGHTGRID,			// Light-grid irradiance atlas
	TD_LIGHTGRID_VISIBILITY,// Light-grid visibility/distance moments
	TD_LIGHTGRID_PROBE,		// Light-grid probe relocation/origin metadata
	TD_LOOKUP_TABLE_MONO,	// Mono lookup table (including alpha)
	TD_LOOKUP_TABLE_ALPHA,	// Alpha lookup table with a white color channel
	TD_LOOKUP_TABLE_RGB1,	// RGB lookup table with a solid white alpha
	TD_LOOKUP_TABLE_RGBA,	// RGBA lookup table
	TD_COVERAGE,			// coverage map for fill depth pass when YCoCG is used
	TD_DEPTH,				// depth buffer copy for motion blur
	// Preserve retail Quake 4's explicit high-quality / uncompressed material
	// bucket without renumbering the existing generated-image cache keys.
	TD_HIGH_QUALITY,
	// Appended openQ4 PBR usages keep all historical binary-image cache keys
	// stable. Colour inputs receive gamma-correct mip generation; material
	// data (ORM/metallic/roughness/AO) always remains linear.
	TD_PBR_COLOR,
	TD_MATERIAL_DATA,
} textureUsage_t;

typedef enum {
	CF_2D,			// not a cube map
	CF_NATIVE,		// _px, _nx, _py, etc, directly sent to GL
	CF_CAMERA		// _forward, _back, etc, rotated and flipped as needed before sending to GL
} cubeFiles_t;

static const unsigned int IMAGEFLAG_NOMIPS = BIT( 0 );
static const unsigned int IMAGEFLAG_FILTER_NEUTRAL_ALPHA = BIT( 1 );

// Highest mip shift image_picmip accepts. Anything beyond this is meaningless
// once image_picmipMinSize floors the reduction, but the range is kept wide so
// the cvar behaves like the Quake 3 style r_picmip it mirrors.
static const int	MAX_IMAGE_PICMIP = 16;

// image_picmipFilter buckets. The filter classifies the logical image name, so
// a DDS replacement under dds/ is still judged by the texture it stands in for.
static const int	PICMIP_FILTER_ALL = 0;
static const int	PICMIP_FILTER_TEXTURES = BIT( 0 );
static const int	PICMIP_FILTER_MODELS = BIT( 1 );
static const int	PICMIP_FILTER_OTHER = BIT( 2 );
static const int	PICMIP_FILTER_MASK = PICMIP_FILTER_TEXTURES | PICMIP_FILTER_MODELS | PICMIP_FILTER_OTHER;

/*
================================================
imageDownsizePolicy_t

The single reduction contract shared by every path that can shrink a source
image: the raw 2D loader, the cube loader, and the precompressed DDS loader.
Keeping one struct means a texture reaches the same dimensions no matter which
of those supplied its pixels, which is what lets the generated .bimage cache key
be derived from the policy alone.

maxDimension is retail Quake 4's absolute ceiling (image_downSize* family) and
mipShift is the Quake 3 style relative reduction (image_picmip). The ceiling is
applied first so every mipShift step still halves a texture that was already
clamped; applying them the other way round would let a low ceiling silently
swallow the first few picmip steps.
================================================
*/
// Resolves the image reduction cvars for one image. Implemented next to those
// cvars in ImageManager.cpp; every loader path and the cache key go through it.
void R_GetImageDownsizePolicy( const char *name, textureUsage_t usage, bool allowDownSize, imageDownsizePolicy_t &policy );
void R_ResolveImageDownsizePolicy( const imageDownsizeInputs_t& inputs, const char* name, textureUsage_t usage, bool allowDownSize, imageDownsizePolicy_t& policy );
textureUsage_t R_ResolveMaterialHighQualityUsage( const materialQualityInputs_t& inputs, textureUsage_t usage, bool forceHighQuality );
unsigned int R_ResolveMaterialNoMipFlags( const materialQualityInputs_t& inputs, unsigned int flags );

// Reduces width/height in place. Safe for non power of two and degenerate sizes.
void R_ApplyImageDownsizePolicy( const imageDownsizePolicy_t &policy, int &width, int &height );

// Number of whole mip levels between the source size and the policy result, for
// loaders that select a level out of an existing mip chain instead of resampling.
int R_ImageDownsizePolicyMipSkip( const imageDownsizePolicy_t &policy, int width, int height, int availableLevels );
// authoredLevels == 0 selects decoded resampling; positive levels select only
// stored DDS mips. False leaves output unchanged.
bool R_ResolveImageReduction(const imageDownsizePolicy_t& policy, int width, int height, int authoredLevels, imageReductionResult_t& output);
bool R_ImageReductionIsExact(const imageDownsizePolicy_t& policy, const imageReductionResult_t& result);

// User-selectable sampling for TF_DEFAULT images. The names intentionally
// mirror Quake 4's image_filter values, while this backend-neutral state keeps
// the Vulkan renderer independent of OpenGL constants.
typedef enum {
	IMAGE_FILTER_LINEAR_MIPMAP_LINEAR = 0,
	IMAGE_FILTER_LINEAR_MIPMAP_NEAREST,
	IMAGE_FILTER_NEAREST,
	IMAGE_FILTER_LINEAR,
	IMAGE_FILTER_NEAREST_MIPMAP_NEAREST,
	IMAGE_FILTER_NEAREST_MIPMAP_LINEAR,
	IMAGE_FILTER_MODE_COUNT
} imageFilterMode_t;

struct imageFilterState_t {
	imageFilterMode_t	mode;
	bool				minLinear;
	bool				magLinear;
	bool				usesMipmaps;
	bool				mipLinear;
};

imageFilterState_t R_GetDefaultImageFilterState();

#include "ImageOpts.h"
#include "../imagetools/BinaryImage.h"

#define	MAX_IMAGE_NAME	256

uint64_t R_ImagePolicyNewResourceIdentity() noexcept;
void R_ImagePolicyResourceDestroyed() noexcept;

class idImage {
public:
	idImage(const char* name);
	~idImage() { R_ImagePolicyResourceDestroyed(); }
	idImage(const idImage&) = delete;
	idImage& operator=(const idImage&) = delete;
	uint64_t GetImagePolicyIdentity() const { return imagePolicyIdentity; }
	// Callback-free; complete observations only. False preserves output.
	bool GetConsumedPolicy(imageConsumedPolicy_t& output) const;
    bool GetPortableContent(imagePortableContent_t& output) const;
    imageDeclaredPolicy_t GetDeclaredPolicy() const { return {filter,repeat,usage,cubeFiles,flags,allowDownSize}; }
	void InvalidateConsumedPolicy();

	const char* GetName() const { return imgName; }

	// Makes this image active on the current GL texture unit.
	// automatically enables or disables cube mapping
	// May perform file loading if the image was not preloaded.
	void		Bind();

	// Should be called at least once
	void		SetSamplerState(textureFilter_t tf, textureRepeat_t tr);
	void		RefreshSamplerState();

	// used by callback functions to specify the actual data
	// data goes from the bottom to the top line of the image, as OpenGL expects it
	// These perform an implicit Bind() on the current texture unit
	// FIXME: should we implement cinematics this way, instead of with explicit calls?
	void		GenerateImage(const byte* pic, int width, int height,
		textureFilter_t filter, textureRepeat_t repeat, textureUsage_t usage);
	void		GenerateCubeImage(const byte* pic[6], int size,
		textureFilter_t filter, textureUsage_t usage);

	bool		CopyFramebuffer(int x, int y, int width, int height,
				int cubeFace = 0);
	bool		CopyDepthbuffer(int x, int y, int width, int height,
				int cubeFace = 0);

	void		UploadScratch(const byte* pic, int width, int height);

	// estimates size of the GL image based on dimensions and storage type
	int			StorageSize() const;

	// print a one line summary of the image
	void		Print() const;

	// check for changed timestamp on disk and reload if necessary
	void		Reload(bool force);

	void		AddReference() { refCount++; };

	void		MakeDefault();	// fill with a grid pattern

	const idImageOpts& GetOpts() const { return opts; }
	int			GetUploadWidth() const { return opts.width; }
	int			GetUploadHeight() const { return opts.height; }
	textureFilter_t GetFilter() const { return filter; }
	textureRepeat_t GetRepeat() const { return repeat; }
	textureUsage_t GetUsage() const { return usage; }
	bool		IsDefaulted() const { return defaulted; }
	bool		IsScratchImage() const { return scratchImage; }
	bool IsFileBacked() const { return generatorFunction == NULL && !opts.isPersistant && !scratchImage; }

	void		SetReferencedOutsideLevelLoad() { referencedOutsideLevelLoad = true; }
	void		SetReferencedInsideLevelLoad() { levelLoadReferenced = true; }
	void		ClearUseCount() { useCount = 0; }
	void		AddUseCount( int count ) { useCount += count; }
	int			GetUseCount() const { return useCount; }
	void		ActuallyLoadImage(bool fromBackEnd);
	//---------------------------------------------
	// Platform specific implementations
	//---------------------------------------------

	void		AllocImage(const idImageOpts& imgOpts, textureFilter_t filter, textureRepeat_t repeat);

	// Deletes the texture object, but leaves the structure so it can be reloaded
	// or resized.
	void		PurgeImage();

	// z is 0 for 2D textures, 0 - 5 for cube maps, and 0 - uploadDepth for 3D textures. Only 
	// one plane at a time of 3D textures can be uploaded. The data is assumed to be correct for 
	// the format, either bytes, halfFloats, floats, or DXT compressed. The data is assumed to 
	// be in OpenGL RGBA format, the consoles may have to reorganize. pixelPitch is only needed 
	// when updating from a source subrect. Width, height, and dest* are always in pixels, so 
	// they must be a multiple of four for dxt data.
	void		SubImageUpload(int mipLevel, int destX, int destY, int destZ,
		int width, int height, const void* data,
		int pixelPitch = 0) const;

	// SetPixel is assumed to be a fast memory write on consoles, degenerating to a 
	// SubImageUpload on PCs.  Used to update the page mapping images.
	// We could remove this now, because the consoles don't use the intermediate page mapping
	// textures now that they can pack everything into the virtual page table images.
	void		SetPixel(int mipLevel, int x, int y, const void* data, int dataSize);

	// some scratch images are dynamically resized based on the display window size.  This 
	// simply purges the image and recreates it if the sizes are different, so it should not be 
	// done under any normal circumstances, and probably not at all on consoles.
	void		Resize(int width, int height);

	// every block-compressed format: gates the compressed upload path, the
	// 4-pixel alignment asserts and the block-size arithmetic
	bool		IsCompressed() const {
		return ( opts.format == FMT_DXT1 || opts.format == FMT_DXT5 || opts.format == FMT_BC7 ||
				 opts.format == FMT_ETC2_RGB8 || opts.format == FMT_ETC2_RGBA8 || opts.format == FMT_EAC_RG11 );
	}

	void		SetTexParameters();	// update aniso and trilinear

	bool		IsLoaded() const { return texnum != TEXTURE_NOT_LOADED; }
	uint64_t	GetStorageGeneration() const { return storageGeneration; }

	// _policyName is the logical image name the downsize policy is classified
	// against. It differs from _name whenever a dds/ replacement supplies the
	// pixels, and passing it keeps the cache key in step with the reduction that
	// is actually applied to those pixels.
	static void			GetGeneratedName(idStr& _name, const char* _policyName, const textureUsage_t& _usage, const cubeFiles_t& _cube, bool allowDownSize = true, unsigned int flags = 0, const imageDownsizePolicy_t* consumed = NULL);

	unsigned int		GetDeviceHandle(void) { return texnum; }
private:
	friend class idImageManager;

	void				AllocImage();
	void				DeriveOpts();

	// parameters that define this image
	idStr				imgName;				// game path, including extension (except for cube maps), may be an image program
	cubeFiles_t			cubeFiles;				// If this is a cube map, and if so, what kind
	void				(*generatorFunction)(idImage* image);	// NULL for files
	textureUsage_t		usage;					// Used to determine the type of compression to use
	idImageOpts			opts;					// Parameters that determine the storage method

	// Sampler settings
	textureFilter_t		filter;
	textureRepeat_t		repeat;
	bool				allowDownSize;
	unsigned int		flags;

	bool				referencedOutsideLevelLoad;
	bool				levelLoadReferenced;	// for determining if it needs to be purged
	bool				defaulted;				// true if the default image was generated because a file couldn't be loaded
	bool				scratchImage;			// storage is owned/mutated by a runtime render target or upload path
	ID_TIME_T			sourceFileTime;			// the most recent of all images used in creation, for reloadImages command
	ID_TIME_T			binaryFileTime;			// the time stamp of the binary file
	idStr				loadedSourceName;		// source or automatic DDS replacement used by the last successful load

	int					refCount;				// overall ref count
	int					useCount;				// per-level material reference count

	static const unsigned int TEXTURE_NOT_LOADED = 0xFFFFFFFF;

	unsigned int				texnum;				// gl texture binding

	// we could derive these in subImageUpload each time if necessary
	unsigned int				internalFormat;
	unsigned int				dataFormat;
	unsigned int				dataType;
	uint64_t			storageGeneration;
	friend class imageConsumedLoad_t;
	imageConsumedPolicy_t consumedPolicy{};
	uint64_t consumedLoadRevision = 0;
	const uint64_t		imagePolicyIdentity;


};

ID_INLINE idImage::idImage(const char* name) : imgName(name), imagePolicyIdentity(R_ImagePolicyNewResourceIdentity()) {
	texnum = TEXTURE_NOT_LOADED;
	internalFormat = 0;
	dataFormat = 0;
	dataType = 0;
	storageGeneration = 0;
	generatorFunction = NULL;
	filter = TF_DEFAULT;
	repeat = TR_REPEAT;
	allowDownSize = true;
	flags = 0;
	usage = TD_DEFAULT;
	cubeFiles = CF_2D;

	referencedOutsideLevelLoad = false;
	levelLoadReferenced = false;
	defaulted = false;
	scratchImage = false;
	sourceFileTime = FILE_NOT_FOUND_TIMESTAMP;
	binaryFileTime = FILE_NOT_FOUND_TIMESTAMP;
	loadedSourceName.Clear();
	refCount = 0;
	useCount = 0;
}

// Mutable renderer-owned images must never be treated as static PBR material
// resources. The name form catches targets before lazy allocation; the image
// form also catches arbitrary names registered through ScratchImage().
bool R_IsMutableRenderImageName( const char *name );
bool R_IsMutableRenderImage( const idImage *image );


// data is RGBA
bool	R_WriteTGA(const char* filename, const byte* data, int width, int height, bool flipVertical = false, const char* basePath = "fs_savepath");
// data is in top-to-bottom raster order unless flipVertical is set



class idImageManager {
public:

	idImageManager()
	{
		insideLevelLoad = false;
		preloadingMapImages = false;
	}

	void				Init();
	void				Shutdown();

	// If the exact combination of parameters has been asked for already, an existing
	// image will be returned, otherwise a new image will be created.
	// Be careful not to use the same image file with different filter / repeat / etc parameters
	// if possible, because it will cause a second copy to be loaded.
	// If the load fails for any reason, the image will be filled in with the default
	// grid pattern.
	// Will automatically execute image programs if needed.
	idImage* ImageFromFile(const char* name,
		textureFilter_t filter, textureRepeat_t repeat, textureUsage_t usage, cubeFiles_t cubeMap = CF_2D, bool allowDownSize = true, unsigned int flags = 0);

	// Returns an image handle without forcing an immediate file load or level-load preload.
	// The texture will be loaded on first use or an explicit Reload().
	idImage* ImageHandleDeferred(const char* name,
		textureFilter_t filter, textureRepeat_t repeat, textureUsage_t usage, cubeFiles_t cubeMap = CF_2D, bool allowDownSize = true, unsigned int flags = 0);

	// look for a loaded image, whatever the parameters
	idImage* GetImage(const char* name) const;

	// look for a loaded image, whatever the parameters
	idImage* GetImageWithParameters(const char* name, textureFilter_t filter, textureRepeat_t repeat, textureUsage_t usage, cubeFiles_t cubeMap, bool allowDownSize = true, unsigned int flags = 0) const;

	// The callback will be issued immediately, and later if images are reloaded or vid_restart
	// The callback function should call one of the idImage::Generate* functions to fill in the data
	idImage* ImageFromFunction(const char* name, void (*generatorFunction)(idImage* image));

	// scratch images are for internal renderer use.  ScratchImage names should always begin with an underscore
	idImage* ScratchImage(const char* name, idImageOpts* imgOpts, textureFilter_t filter, textureRepeat_t repeat, textureUsage_t usage);

	// purges all the images before a vid_restart
	void				PurgeAllImages();

	// reloads all apropriate images after a vid_restart
	void				ReloadImages(bool all);
	void ClearCheckedImagePolicyChanges(); // Only after an exact checked restart.

	// reloads every image when a texture reduction cvar changed this frame
	void				CheckCvars();

	// swallows the born-modified state of those cvars during startup
	void				PrimeCvars();

	// unbind all textures from all texture units
	void				UnbindAll();

	// disable the active texture unit
	void				BindNull();

	// Called only by renderSystem::BeginLevelLoad
	void				BeginLevelLoad();

	// Called only by renderSystem::EndLevelLoad
	void				EndLevelLoad();

	//void				Preload(const idPreloadManifest& manifest, const bool& mapPreload);

	// Loads unloaded level images
	int					LoadLevelImages(bool pacifier);
	int					CountPendingLevelLoads() const;

	// used to clear and then write the dds conversion batch file
	void				StartBuild();
	void				FinishBuild(bool removeDups = false);

	void				PrintMemInfo(MemInfo_t* mi);

	// built-in images
	void CreateIntrinsicImages();
	idImage* defaultImage;
	idImage* flatNormalMap;				// 128 128 255 in all pixels
	idImage* alphaNotchImage;			// 2x1 texture with just 1110 and 1111 with point sampling
	idImage* whiteImage;					// full of 0xff
	idImage* blackImage;					// full of 0x00
	idImage* noFalloffImage;				// all 255, but zero clamped
	idImage* fogImage;					// increasing alpha is denser fog
	idImage* fogEnterImage;				// adjust fogImage alpha based on terminator plane
	idImage* scratchImage;
	idImage* scratchImage2;
	idImage* accumImage;
	idImage* currentRenderImage;				// FP16 scene copy for SS_POST_PROCESS shaders
	idImage* currentDepthImage;				// for motion blur
	idImage* originalCurrentRenderImage;		// currentRenderImage before any changes for stereo rendering
	idImage* loadingIconImage;				// loading icon must exist always
	idImage* hellLoadingIconImage;				// loading icon must exist always
	idImage* cinematicImage;
	idImage* ambientNormalMap;
	idImage* specularTableImage;
	idImage* normalCubeMapImage;
	idImage* borderClampImage;
	idImage* rampImage;
	idImage* alphaRampImage;

	//--------------------------------------------------------

	idImage* AllocImage(const char* name);
	idImage* AllocStandaloneImage(const char* name);

	bool				ExcludePreloadImage(const char* name);

	idList<idImage*>	images;
	idHashIndex			imageHash;

	bool				insideLevelLoad;			// don't actually load images now
	bool				preloadingMapImages;		// unless this is set
};

extern idImageManager* globalImages;		// pointer to global list for the rest of the system

// Level-load image phase accounting. LoadLevelImages reports one wall time for
// the whole set, which cannot say whether a load is spending it probing source
// timestamps, reading the generated cache, decoding, or uploading. Those four
// have different remedies, so ActuallyLoadImage accumulates them separately.
struct imageLoadPhaseTimings_t {
	double			probeMsec;			// source timestamp probes taken without reading a payload
	double			generatedMsec;		// generated .bimage cache reads
	double			decodeMsec;			// source read, decode, compress, and cache write
	double			uploadMsec;			// AllocImage and SubImageUpload
	int				probeCount;
	int				generatedCount;
	int				decodeCount;
	int				uploadCount;

	void			Clear() {
						probeMsec = generatedMsec = decodeMsec = uploadMsec = 0.0;
						probeCount = generatedCount = decodeCount = uploadCount = 0;
					}
};

extern imageLoadPhaseTimings_t imageLoadPhaseTimings;

int MakePowerOfTwo(int num);

/*
====================================================================

IMAGEPROCESS

FIXME: make an "imageBlock" type to hold byte*,width,height?
====================================================================
*/

byte* R_Dropsample(const byte* in, int inwidth, int inheight, int outwidth, int outheight);
byte* R_ResampleTexture(const byte* in, int inwidth, int inheight, int outwidth, int outheight);
byte* R_MipMapWithAlphaSpecularity(const byte* in, int width, int height);
void R_ApplyFilterNeutralAlpha(byte* data, int pixelCount);
byte* R_MipMapWithGamma(const byte* in, int width, int height);
byte* R_MipMap(const byte* in, int width, int height);

// these operate in-place on the provided pixels
void R_BlendOverTexture(byte* data, int pixelCount, const byte blend[4]);
void R_HorizontalFlip(byte* data, int width, int height);
void R_VerticalFlip(byte* data, int width, int height);
void R_RotatePic(byte* data, int width);

/*
====================================================================

IMAGEFILES

====================================================================
*/

void R_LoadImage(const char* name, byte** pic, int* width, int* height, ID_TIME_T* timestamp, bool makePowerOf2);
void R_LoadImageForUsage(const char* name, byte** pic, int* width, int* height, ID_TIME_T* timestamp, bool makePowerOf2, textureUsage_t usage);
bool R_ResolvePreferredDDSImageSource(const char* name, idStr& ddsName, ID_TIME_T* timestamp, bool allowPrecompressedDDS, bool* precompressedDDS);
// enables per-candidate DDS probe memoization for the duration of a level
// load; disabling also clears all memoized probe results
void R_SetDDSProbeCacheActive(bool active);
bool R_LoadPrecompressedDDS(const char* name, idBinaryImage& image, ID_TIME_T* timestamp, textureUsage_t usage, const imageDownsizePolicy_t& downsizePolicy, bool useMipmaps, imageReductionResult_t* reduction = NULL, const imageFileContent_t* expected = NULL);
bool R_ImageDDS_RunSelfTest();
// pic is in top to bottom raster format
bool R_LoadCubeImages(const char* cname, cubeFiles_t extensions, byte* pic[6], int* size, ID_TIME_T* timestamp);

/*
====================================================================

IMAGEPROGRAM

====================================================================
*/

// Returns the parser's load result. Direct-file programs return false when the
// source is missing; a valid PK4-backed source may still have timestamp zero.
bool R_LoadImageProgram(const char* name, byte** pic, int* width, int* height, ID_TIME_T* timestamp, textureUsage_t* usage = NULL);
const char* R_ParsePastImageProgram(idLexer& src);

