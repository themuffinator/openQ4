// Copyright (C) 2004 Id Software, Inc.
//

#ifndef __RENDERER_H__
#define __RENDERER_H__

// jmarshall
class idImage;
class idRenderTexture;

#include "ImageOpts.h"
#include "RendererCaps.h"
// jmarshall end

/*
===============================================================================

	idRenderSystem is responsible for managing the screen, which can have
	multiple idRenderWorld and 2D drawing done on it.

===============================================================================
*/


// Contains variables specific to the OpenGL configuration being run right now.
// These are constant once the OpenGL subsystem is initialized.
typedef struct glconfig_s {
	const char			*renderer_string;
	const char			*vendor_string;
	const char			*version_string;
	const char			*extensions_string;
	const char			*wgl_extensions_string;

	float				glVersion;				// atof( version_string )
	rendererContextRequest_t contextRequest;		// requested/created context from the platform layer
	renderBackendCaps_t	backendCaps;			// exact feature-driven capability probe
	rendererTier_t		rendererTier;			// highest selected renderer tier
	renderFeatureSet_t	renderFeatures;			// feature flags derived from the selected tier


	int					maxTextureSize;			// queried from GL
	int					maxTextureUnits;
	int					maxTextureCoords;
	int					maxTextureImageUnits;
	int					maxDrawBuffers;
	int					maxColorAttachments;
	float				maxTextureAnisotropy;

	int					colorBits, depthBits, stencilBits;
// RAVEN BEGIN
	int					alphaBits;
// RAVEN END

	bool				multitextureAvailable;
	bool				anisotropicAvailable;
	bool				textureLODBiasAvailable;
	bool				textureEnvAddAvailable;
	bool				textureEnvCombineAvailable;
// RAVEN BEGIN
// jscott: added
	bool				blendSquareAvailable;
// RAVEN END
	bool				registerCombinersAvailable;
	bool				cubeMapAvailable;
	bool				envDot3Available;
	bool				texture3DAvailable;
	bool				sharedTexturePaletteAvailable;
	bool				textureCompressionAvailable;
	bool				bptcTextureCompressionAvailable;
	bool				etc2TextureCompressionAvailable;
// RAVEN BEGIN
// dluetscher: added
	bool				drawRangeElementsAvailable;
	bool				blendMinMaxAvailable;
	bool				floatBufferAvailable;
	bool				textureSRGBAvailable;
	bool				framebufferSRGBAvailable;
// RAVEN END
	bool				ARBVertexBufferObjectAvailable;
	bool				pixelBufferObjectAvailable;
	bool				ARBVertexProgramAvailable;
	bool				ARBFragmentProgramAvailable;
	bool				twoSidedStencilAvailable;
	bool				textureNonPowerOfTwoAvailable;
	bool				depthBoundsTestAvailable;

// RAVEN BEGIN
// rjohnson: new shader stage system
	bool				GLSLProgramAvailable;
// RAVEN END
	bool				GLSL130Available;			// GLSL >= 1.30; false on Apple GL 2.1 (GLSL 1.20). Feeds AA diagnostics and menu availability; the #version 130 post shaders themselves fail safe via material validation

// RAVEN BEGIN
// dluetscher: added check for NV_vertex_program and NV_fragment_program support
	bool				nvProgramsAvailable;
// RAVEN END

	// ati r200 extensions
	bool				atiFragmentShaderAvailable;

	// ati r300
	bool				atiTwoSidedStencilAvailable;

	int					vidWidth, vidHeight;	// passed to R_BeginFrame
	// 2D draw region in framebuffer pixels with a top-left origin.
	// Defaults to the full framebuffer and can be narrowed (for example to the primary monitor).
	int					uiViewportX, uiViewportY;
	int					uiViewportWidth, uiViewportHeight;

	int					displayFrequency;

	bool				isFullscreen;

#ifndef _CONSOLE
	bool				preferNV20Path;			// for FX5200 cards
// RAVEN BEGIN
// dluetscher: added preferSimpleLighting flag
	bool				preferSimpleLighting;	// for the ATI 9700 cards
	bool				preferSimpleInteraction;	// driver quirk fallback for fragile ARB interaction uploads
	bool				disableARB2Interactions;	// driver quirk fallback for fragile ARB2 light-interaction draws
// RAVEN END
#endif

	bool				allowNV20Path;
	bool				allowNV10Path;
	bool				allowR200Path;
	bool				allowARB2Path;

	bool				isInitialized;
// INTEL BEGIN 
// Anu adding support to toggle SMP
#ifdef ENABLE_INTEL_SMP
	bool				isSmpAvailable;
	int					isSmpActive;
	int					rearmSmp;
#endif
// INTEL END

} glconfig_t;

// outside of TR since it shouldn't be cleared during ref re-init; the window
// layer and GUI code read (and, until the renderer-module window seam lands,
// write) the vid/viewport fields through this public declaration
extern glconfig_t glConfig;

typedef bool (*renderPortalSkyCaptureViewCallback_t)( const struct renderView_s *sourceView, struct renderView_s *portalSkyView );

// backend-format facts about a material stage's image, for engine-side
// self-tests that must not reach into renderer-internal image types
typedef struct materialImageInfo_s {
	int						numLevels;
	bool					isDXT1Compressed;
	bool					usesGreenAlphaColorFormat;
} materialImageInfo_t;

// light-grid bake parameters for the bakeLightGrids command; the session
// owns argument parsing and output-path policy, the renderer owns the bake
typedef struct lightGridBakeOptions_s {
	int					maxProbes;
	int					bounces;
	int					captureSize;
	int					blends;
	int					samples;
	bool				separateAreas;
	idVec3				gridSize;
} lightGridBakeOptions_t;


// font support
const int GLYPH_START = 0;
const int GLYPH_END = 255;
const int GLYPH_CHARSTART = 32;
const int GLYPH_CHAREND = 127;
const int GLYPHS_PER_FONT = GLYPH_END - GLYPH_START + 1;

typedef struct {
	float				width;
	float				height;
	float				horiAdvance;
	float				horiBearingX;
	float				horiBearingY;
	float				s;				// x offset in image where glyph starts
	float				t;				// y offset in image where glyph starts
	float				s2;
	float				t2;
} glyphInfo_t;

/*
===============================================================================

	What the 256 base glyph slots of a fontInfo_t are indexed by.

	The retail .fontdat atlases are art for one 8-bit codepage, so slot 0xE9 is
	whichever character that byte meant in the language the atlas shipped for.
	The scalable path rasterises from Unicode-mapped .ttf files and fills the
	same 256 slots with Latin-1, so slot 0xE9 is U+00E9 no matter what language
	is selected, and everything above U+00FF lives in the extended pages below.

	Engine text is UTF-8, so the text layer works in code points and asks the
	font which of the two it is holding.

===============================================================================
*/
typedef enum {
	GLYPH_INDEX_CODEPAGE = 0,	// slot i is byte i of the font's 8-bit codepage
	GLYPH_INDEX_UNICODE			// slot i is code point U+00<i>
} glyphIndexing_t;

// Code points above U+00FF live in sparse 256-entry pages hung off the font, so
// a face can carry any script it has art for without every fontInfo_t paying
// for 64K glyph slots. Pages stop at the Basic Multilingual Plane: no script
// the GUI draws lives above it, and stopping there keeps the page directory to
// 256 pointers.
const int GLYPH_PAGE_SHIFT = 8;
const int GLYPH_PAGE_SIZE = 1 << GLYPH_PAGE_SHIFT;
const int GLYPH_MAX_PAGES = 0x10000 >> GLYPH_PAGE_SHIFT;
const unsigned int GLYPH_MAX_CODEPOINT = 0xFFFF;

typedef struct fontGlyphPage_s {
	// Extended glyphs are packed into their own atlas, so one found here draws
	// with this material rather than the font's base one.
	const idMaterial *	material;
	glyphInfo_t			glyphs[GLYPH_PAGE_SIZE];
	// A page is built for a whole Unicode block but a face rarely covers all of
	// it. Without this a hole would be indistinguishable from a real glyph that
	// happens to be zero width.
	bool				covered[GLYPH_PAGE_SIZE];
} fontGlyphPage_t;

typedef struct {
	glyphInfo_t			glyphs[GLYPHS_PER_FONT];
	float				pointSize;
	float				fontHeight;
	float				ascender;
	float				descender;
	const idMaterial *	material;
	char				name[64];
	int					glyphIndexing;		// glyphIndexing_t
	// Code points above U+00FF, indexed by ( code point >> GLYPH_PAGE_SHIFT ),
	// or NULL for a font that carries none. Owned by whoever registered the
	// font and valid until the renderer shuts down; a fontInfo_t is copied by
	// value throughout the GUI and every copy shares these pages.
	fontGlyphPage_t * const *extendedPages;
} fontInfo_t;

// Declared by ../idlib/LangDict.h, which precompiled.h includes ahead of this
// header. Repeated here so the glyph lookup below stays self-contained rather
// than depending on that ordering; LangDict.h itself cannot be included from
// here, because it is built on idStr and idList.
bool LangDict_ByteForCodePoint( unsigned int codePoint, unsigned char &out );

/*
================
R_GlyphForCodePoint

The single place that knows how a fontInfo_t is indexed. Returns NULL when the
font has no art for the code point - which the text layer has to handle, not
paper over, because it is the difference between drawing a substitute and
drawing a zero-advance blank that swallows the character.

*material receives the atlas the returned glyph lives in. It is not always the
font's own: extended pages are packed separately, so a page glyph and a base
glyph in the same string come from two different images.
================
*/
ID_INLINE const glyphInfo_t *R_GlyphForCodePoint( const fontInfo_t *font, unsigned int codePoint, const idMaterial **material ) {
	if ( font == NULL ) {
		return NULL;
	}
	if ( material != NULL ) {
		*material = font->material;
	}

	if ( font->glyphIndexing == GLYPH_INDEX_CODEPAGE ) {
		// 256 slots of retail art for one 8-bit codepage: map the code point back
		// onto a byte of it. Anything that codepage cannot express has no glyph,
		// which is exactly why a Cyrillic language cannot use these atlases.
		unsigned char byte = 0;
		if ( !LangDict_ByteForCodePoint( codePoint, byte ) ) {
			return NULL;
		}
		return &font->glyphs[byte];
	}

	if ( codePoint < (unsigned int)GLYPHS_PER_FONT ) {
		return &font->glyphs[codePoint];
	}
	if ( codePoint > GLYPH_MAX_CODEPOINT || font->extendedPages == NULL ) {
		return NULL;
	}

	const fontGlyphPage_t *page = font->extendedPages[codePoint >> GLYPH_PAGE_SHIFT];
	if ( page == NULL ) {
		return NULL;
	}
	const int slot = (int)( codePoint & ( GLYPH_PAGE_SIZE - 1 ) );
	if ( !page->covered[slot] ) {
		return NULL;
	}
	if ( material != NULL ) {
		*material = page->material;
	}
	return &page->glyphs[slot];
}

typedef struct {
	fontInfo_t			fontInfoSmall;
	fontInfo_t			fontInfoMedium;
	fontInfo_t			fontInfoLarge;
	float				maxHeight;
	float				maxWidth;
	float				maxHeightSmall;
	float				maxWidthSmall;
	float				maxHeightMedium;
	float				maxWidthMedium;
	float				maxHeightLarge;
	float				maxWidthLarge;
	char				name[64];
} fontInfoEx_t;


const int TINYCHAR_WIDTH		= 4;
const int TINYCHAR_HEIGHT		= 8;
const int SMALLCHAR_WIDTH		= 8;
const int SMALLCHAR_HEIGHT		= 16;
const int BIGCHAR_WIDTH			= 16;
const int BIGCHAR_HEIGHT		= 16;

// all drawing is done to a 640 x 480 virtual screen size
// and will be automatically scaled to the real resolution
const int SCREEN_WIDTH			= 640;
const int SCREEN_HEIGHT			= 480;

// RAVEN BEGIN
// rjohnson: new blur special effect
typedef enum
{
	SPECIAL_EFFECT_NONE = 0,
	SPECIAL_EFFECT_BLUR		= 0x00000001,
	SPECIAL_EFFECT_AL		= 0x00000002,
	SPECIAL_EFFECT_MAX,
} ESpecialEffectType;
// RAVEN END

// Backend-neutral, delayed GPU duration for one complete submitted renderer
// frame. The sample is intentionally POD because idRenderSystem may cross the
// renderer-module boundary. A valid result always belongs to the reported
// timing generation; resets invalidate the previous generation immediately.
typedef enum {
	RENDER_GPU_TIMING_BACKEND_NONE = 0,
	RENDER_GPU_TIMING_BACKEND_OPENGL,
	RENDER_GPU_TIMING_BACKEND_VULKAN
} renderGpuTimingBackend_t;

typedef struct renderGpuFrameTiming_s {
	bool				supported;
	bool				valid;
	renderGpuTimingBackend_t backend;
	int				frameNumber;
	unsigned int		generation;
	unsigned int		latencyFrames;
	unsigned long long	elapsedMicroseconds;
	unsigned long long	resolvedSamples;
	unsigned long long	unavailableSamples;
	unsigned long long	droppedSamples;
	unsigned long long	resetCount;
} renderGpuFrameTiming_t;

// Frame-latched presentation dimensions shared with the game modules. The
// 3D scene and its post-process chain render at sceneWidth/sceneHeight; final
// composition and all game/UI drawing remain at outputWidth/outputHeight.
// Keep this structure POD because idRenderSystem crosses both renderer- and
// game-module boundaries.
typedef struct renderPresentationState_s {
	int				frameNumber;
	int				outputWidth;
	int				outputHeight;
	int				sceneWidth;
	int				sceneHeight;
	int				effectiveScalePercent;
	unsigned int		historyGeneration;
	bool				dynamicResolutionRequested;
	bool				dynamicResolutionActive;
	bool				temporalAARequested;
	bool				captureFrozen;
	bool				captureForcedNative;
} renderPresentationState_t;

class idRenderWorld;


class idRenderSystem {
public:

	virtual					~idRenderSystem() {}

	// set up cvars and basic data structures, but don't
	// init OpenGL, so it can also be used for dedicated servers
// RAVEN BEGIN
	// nrausch: Init things that touch the render state seperately
	virtual void			DeferredInit( void ) { }
// RAVEN END
	virtual void			Init( void ) = 0;

	// only called before quitting
	virtual void			Shutdown( void ) = 0;

// RAVEN BEGIN
// mwhitlock: Dynamic memory consolidation
#if defined(_RV_MEM_SYS_SUPPORT)
	virtual void			FlushLevelImages( void ) = 0;
#endif
// RAVEN END

	virtual void			InitOpenGL( void ) = 0;

	virtual void			ShutdownOpenGL( void ) = 0;

	virtual bool			IsOpenGLRunning( void ) const = 0;
	//virtual void			GetValidModes( idStr &Mode4x3Text, idStr &Mode4x3Values, idStr &Mode16x9Text, idStr &Mode16x9Values, 
	//									   idStr &Mode16x10Text, idStr &Mode16x10Values ) = 0;

	virtual bool			IsFullScreen( void ) const = 0;
	virtual int				GetScreenWidth( void ) const = 0;
	virtual int				GetScreenHeight( void ) const = 0;
	virtual int				GetVideoRestartCount( void ) const = 0;

	// allocate a renderWorld to be used for drawing
	virtual idRenderWorld *	AllocRenderWorld( void ) = 0;
	virtual	void			FreeRenderWorld( idRenderWorld * rw ) = 0;

// RAVEN BEGIN
	//virtual void			RemoveAllModelReferences( idRenderModel *model ) = 0;
// RAVEN BEGIN

	// All data that will be used in a level should be
	// registered before rendering any frames to prevent disk hits,
	// but they can still be registered at a later time
	// if necessary.
	virtual void			BeginLevelLoad( void ) = 0;
	virtual void			EndLevelLoad( void ) = 0;

// RAVEN BEGIN
// mwhitlock: changes for Xenon to enable us to use texture resources from .xpr
// bundles.
#if defined(_XENON)
	// Like BeginLevelLoad and EndLevelLoad, but only deals with textures.
	virtual void			XenonBeginLevelLoadTextures( void ) = 0;
	virtual	void			XenonEndLevelLoadTextures( void ) = 0;
#endif
// RAVEN END

	virtual	void			ExportMD5R( bool compressed ) = 0;

#ifdef Q4SDK_MD5R

	virtual void			CopyPrimBatchTriangles( idDrawVert *destDrawVerts, glIndex_t *destIndices, void *primBatchMesh, void *silTraceVerts ) = 0;

#else	// Q4SDK_MD5R

// RAVEN BEGIN
// dluetscher: added call to write out the MD5R models that have been converted at load,
//			   also added call to retrieve idDrawVert geometry from a MD5R primitive batch 
//			   from the game DLL
#if defined( _MD5R_SUPPORT )
	virtual void			CopyPrimBatchTriangles( idDrawVert *destDrawVerts, glIndex_t *destIndices, rvMesh *primBatchMesh, const rvSilTraceVertT *silTraceVerts ) = 0;
#endif

#endif // !Q4SDK_MD5R

// jnewquist: Track texture usage during cinematics for streaming purposes
#ifndef _CONSOLE
//	enum TextureTrackCommand {
//		TEXTURE_TRACK_BEGIN,
//		TEXTURE_TRACK_UPDATE,
//		TEXTURE_TRACK_END
//	};
//	virtual void			TrackTextureUsage( TextureTrackCommand command, int frametime = 0, const char *name=NULL ) = 0;
#endif
// RAVEN END

	// font support
	virtual bool			RegisterFont( const char *fontName, fontInfoEx_t &font ) = 0;
	// GUI drawing just involves shader parameter setting and axial image subsections
	virtual void			SetColor( const idVec4 &rgba ) = 0;
	virtual void			SetColor4( float r, float g, float b, float a ) = 0;

	virtual void			DrawStretchPic( const idDrawVert *verts, const glIndex_t *indexes, int vertCount, int indexCount, const idMaterial *material,
											bool clip = true, float x = 0.0f, float y = 0.0f, float w = 640.0f, float h = 480.0f ) = 0;
	virtual void			DrawStretchPic( float x, float y, float w, float h, float s1, float t1, float s2, float t2, const idMaterial *material ) = 0;
// RAVEN BEGIN
// jnewquist: Deal with flipped back-buffer copies on Xenon
//	virtual void			DrawStretchCopy( float x, float y, float w, float h, float s1, float t1, float s2, float t2, const idMaterial *material ) = 0;
// RAVEN END

	virtual void			DrawStretchTri ( idVec2 p1, idVec2 p2, idVec2 p3, idVec2 t1, idVec2 t2, idVec2 t3, const idMaterial *material ) = 0;
	virtual void			FlushGui() = 0;

	// binds the stage's image and reports its uploaded format facts; returns
	// false when the material/stage has no image
	virtual bool			GetMaterialStageImageInfo( const idMaterial *material, int stageIndex, materialImageInfo_t &info ) = 0;

	// uploads raw RGBA8 pixels into the stage's image (idImage::UploadScratch)
	// for dynamically generated GUI content; returns false when the
	// material/stage has no image
	virtual bool			UploadMaterialStageScratchImage( const idMaterial *material, int stageIndex, const byte *data, int width, int height ) = 0;

	// keeps the loading screen presenting without vsync throttling during
	// map changes; replaces the renderer-internal free-function call
	virtual void			SetLoadingScreenSwapIntervalBypass( bool active ) = 0;

	// allocates a material decl instance for the decl manager's DECL_MATERIAL
	// registration; material construction is renderer-owned
	virtual class idDecl *	AllocMaterialDecl( void ) = 0;

	// resolves and uploads an image file ahead of first use (menu background
	// preloading); replaces direct image-manager access from framework code
	virtual void			PreloadImage( const char *name ) = 0;

	// light-grid baking (bakeLightGrids command); the world internals stay
	// renderer-side while the session owns command flow and output paths
	virtual void			GetDefaultLightGridBakeOptions( lightGridBakeOptions_t &options ) = 0;
	// true when a primary world and view exist (a map is loaded and rendering)
	virtual bool			HasPrimaryRenderView( void ) = 0;
	// reports the primary world's map name and, after grid setup under the
	// given options, the portal areas holding valid grid points; false when
	// no primary world exists. Caller-buffer out-params (idStr/idList must
	// not resize across the renderer-module ABI): numValidAreaIndices always
	// receives the true count; indices beyond maxAreaIndices are dropped and
	// validAreaIndices may be NULL when only the count is wanted
	virtual bool			GetCurrentLightGridBakeInfo( const lightGridBakeOptions_t &options, char *mapName, int mapNameLength, int *validAreaIndices, int maxAreaIndices, int &numValidAreaIndices ) = 0;
	// verify existing bake outputs against the primary world and options
	virtual bool			LightGridFileMatchesBakeOptions( const char *name, const lightGridBakeOptions_t &options ) = 0;
	virtual bool			LightGridPackFileMatchesBakeOptions( const char *name, const lightGridBakeOptions_t &options ) = 0;
	// bakes the currently loaded map's light grids; blocks until complete
	virtual bool			BakeCurrentLightGrids( const lightGridBakeOptions_t &options, const char *jobName = NULL ) = 0;
	virtual void			GlobalToNormalizedDeviceCoordinates( const idVec3 &global, idVec3 &ndc ) = 0;
	virtual void			GetGLSettings( int& width, int& height ) = 0;
//	virtual void			PrintMemInfo( MemInfo *mi ) = 0;

	//virtual void			DrawTinyChar( int x, int y, int ch, const idMaterial *material ) = 0;
	//virtual void			DrawTinyStringExt( int x, int y, const char *string, const idVec4 &setColor, bool forceColor, const idMaterial *material ) = 0;
	virtual void			DrawSmallChar( int x, int y, int ch, const idMaterial *material ) = 0;
	virtual void			DrawSmallStringExt( int x, int y, const char *string, const idVec4 &setColor, bool forceColor, const idMaterial *material ) = 0;
	virtual void			DrawBigChar( int x, int y, int ch, const idMaterial *material ) = 0;
	virtual void			DrawBigStringExt( int x, int y, const char *string, const idVec4 &setColor, bool forceColor, const idMaterial *material ) = 0;

	// dump all 2D drawing so far this frame to the demo file
	virtual void			WriteDemoPics() = 0;

	// draw the 2D pics that were saved out with the current demo frame
	virtual void			DrawDemoPics() = 0;

	// Sets the shader time used by non-world 2D rendering for the current frame.
	virtual void			SetFrameShaderTime( int timeMsec ) = 0;

	// FIXME: add an interface for arbitrary point/texcoord drawing


	// a frame cam consist of 2D drawing and potentially multiple 3D scenes
	// window sizes are needed to convert SCREEN_WIDTH / SCREEN_HEIGHT values
	virtual void			BeginFrame( int windowWidth, int windowHeight ) = 0;
// RAVEN BEGIN
//	virtual void			BeginFrame( struct viewDef_s *viewDef, int windowWidth, int windowHeight ) = 0;
//	virtual	void			RenderLightFrustum( const struct renderLight_s &renderLight, idPlane lightFrustum[6] ) = 0;
//	virtual	void			LightProjectionMatrix( const idVec3 &origin, const idPlane &rearPlane, idVec4 mat[4] ) = 0;
//	virtual void			ToggleSmpFrame( void ) = 0;
// RAVEN END

// RAVEN BEGIN
// rjohnson: new blur special effect
	virtual void			SetSpecialEffect( ESpecialEffectType Which, bool Enabled ) = 0;
	virtual void			SetSpecialEffectParm( ESpecialEffectType Which, int Parm, float Value ) = 0;
	virtual void			ShutdownSpecialEffects( void ) = 0;
// RAVEN END

	// if the pointers are not NULL, timing info will be returned
	virtual void			EndFrame( int *frontEndMsec = NULL, int *backEndMsec = NULL ) = 0;

	// aviDemo uses this.
	// Will automatically tile render large screen shots if necessary
	// Samples is the number of jittered frames for anti-aliasing
	// If ref == NULL, session->updateScreen will be used
	// This will perform swapbuffers, so it is NOT an approppriate way to
	// generate image files that happen during gameplay, as for savegame
	// markers.  Use WriteRender() instead.
// RAVEN BEGIN
// rjohnson: added basePath
	//virtual	void			TakeJPGScreenshot( int width, int height, const char *fileName, int blends, struct renderView_s *ref, const char *basePath = "fs_savepath" ) = 0;
	//virtual void			TakeScreenshot( int width, int height, const char *fileName, int blends, struct renderView_s *ref, const char *basePath = "fs_savepath" ) = 0;
// RAVEN END

	// the render output can be cropped down to a subset of the real screen, as
	// for save-game reviews and split-screen multiplayer.  Users of the renderer
	// will not know the actual pixel size of the area they are rendering to

	// the x,y,width,height values are in virtual SCREEN_WIDTH / SCREEN_HEIGHT coordinates

	// to render to a texture, first set the crop size with makePowerOfTwo = true,
	// then perform all desired rendering, then capture to an image
	// if the specified physical dimensions are larger than the current cropped region, they will be cut down to fit
	virtual void			CropRenderSize( int width, int height, bool makePowerOfTwo = false, bool forceDimensions = false ) = 0;
	virtual void			CaptureRenderToImage( const char *imageName ) = 0;
	//virtual void			CaptureRenderToMemory( void *buffer ) = 0;
	// fixAlpha will set all the alpha channel values to 0xff, which allows screen captures
	// to use the default tga loading code without having dimmed down areas in many places
	virtual void			CaptureRenderToFile( const char *fileName, bool fixAlpha = false ) = 0;
	virtual void			SetPortalSkyCaptureViewCallback( renderPortalSkyCaptureViewCallback_t callback ) = 0;
	virtual void			UnCrop() = 0;
	virtual void			GetCardCaps( bool &oldCard, bool &nv10or20 ) = 0;
	virtual bool			UploadImage( const char *imageName, const byte *data, int width, int height ) = 0;

	//virtual void			DebugGraph( float cur, float min, float max, const idVec4 &color ) = 0;
	//virtual void			ShowDebugGraph( void ) = 0;

// jmarshall
	// Returns a image based on the options in idImageOpts.
	virtual idImage* CreateImage(const char* name, idImageOpts* opts, textureFilter_t textureFilter) = 0;

	// Returns the specified image.
	virtual idImage* FindImage(const char* name) = 0;
	virtual bool			ValidateMaterialArbPrograms( const idMaterial* material ) = 0;
	virtual bool			ValidateSMAALookupTextures( void ) = 0;

	// Creates a render texture given a albedo and depth image.
	virtual idRenderTexture* CreateRenderTexture(idImage* albedoImage, idImage* depthImage, idImage* albedoImage2 = nullptr, idImage* albedoImage3 = nullptr) = 0;
	virtual void			DestroyRenderTexture(idRenderTexture* renderTexture) = 0;

	// Resizes a image to the specified width and height.
	virtual void			ResizeImage(idImage* image, int width, int height) = 0;

	// Resizes a render texture to the specified width and height. A failed target
	// is queued for destruction and the caller's pointer is cleared.
	virtual bool			ResizeRenderTexture(idRenderTexture*& renderTexture, int width, int height) = 0;

	// Fills in the dimensions for the given render texture.
	virtual void			GetRenderTextureSize(idRenderTexture* renderTexture, int& renderTextureWidth, int& renderTextureHeight) = 0;

	// Sets a debug label on the render texture and its attachments when GL object labels are available.
	virtual void			SetRenderTextureDebugName(idRenderTexture* renderTexture, const char* label) = 0;

	// Binds the specified render texture, set as nullptr to draw directly to the back buffer.
	virtual void			BindRenderTexture(idRenderTexture* renderTexture, idRenderTexture* feedbackRenderTexture) = 0;

	// Resolves a msaa render texture to the blit render texture.
	virtual void			ResolveMSAA(idRenderTexture* msaaRenderTexture, idRenderTexture* destRenderTexture, bool resolveDepth = false) = 0;

	// Clears the current render target
	virtual void			ClearRenderTarget(bool clearColor, bool clearDepth, float depthValue, float red, float green, float blue) = 0;

	// Sets the source dimensions used by post-process shader parameter bindings.
	virtual void			SetPostProcessSourceSize(int width, int height) = 0;

	// Sets the source colour-space contract used by post-process shader parameter bindings.
	virtual void			SetPostProcessSourceColorSpace(const idVec4& colorSpace) = 0;

	// Sets the SMAA quality contract used by post-process shader parameter bindings.
	virtual void			SetPostProcessSMAAQuality(const idVec4& quality) = 0;

	// Controls whether fullscreen 2D submissions target the UI viewport sub-rect.
	virtual void			SetUseUIViewportFor2D( bool enable ) = 0;
	virtual bool			GetUseUIViewportFor2D( void ) const = 0;

	// Fills in the image dinem for the given image.
	virtual void			GetImageSize(idImage* image, int& imageWidth, int& imageHeight) = 0;
	// Returns the sample count actually allocated after driver-cap clamping.
	virtual int				GetImageMSAASamples(idImage* image) = 0;
// jmarshall end

	// read-only view of the active renderer's GL configuration/capability
	// state; the engine must not read the glConfig global directly (the
	// renderer may live in a module whose glConfig is a separate object)
	virtual const glconfig_t &	GetGLConfig( void ) const = 0;

	// openQ4: publishes the underwater view state for this frame. amount is 0 when the eye is out
	// of the liquid and rises to 1 fully submerged; tint is the liquid's colour.
	//
	// Returns true if the renderer will actually draw the effect. A backend that cannot - the
	// Vulkan module only supports a fixed set of material programs - returns false, and the caller
	// is expected to fall back to something simpler rather than showing nothing.
	// fogDistance is how far light travels through this liquid before it is fully absorbed, in
	// world units - the knob that separates clear water from lava you cannot see a foot into.
	virtual bool			SetUnderwaterView( float amount, const idVec3 &tint, float fogDistance ) = 0;

	// Capture the coherent current crop, then center-crop and resample it on the CPU.
	// Kept at the end of the interface so existing render-system vtable slots remain stable.
	virtual void			CaptureRenderToFile( const char *fileName, bool fixAlpha, int outputWidth, int outputHeight ) = 0;

	// Returns the newest resolved whole-frame GPU duration. Resolution is
	// delayed and never waits for the current frame; valid=false is an expected
	// warm-up, reset, disabled, or query-not-ready state.
	virtual void			GetGpuFrameTiming( renderGpuFrameTiming_t &timing ) const = 0;

	// Invalidates delayed samples at a non-renderer session discontinuity. The
	// renderer owns the generation change and backend query retirement.
	virtual void			ResetGpuFrameTiming( const char *reason ) = 0;

	// Returns the immutable scene/output extent selected at BeginFrame. Before
	// the first frame, output dimensions fall back to the active video mode.
	virtual void			GetPresentationState( renderPresentationState_t &state ) const = 0;

	// Queues the backend-neutral temporal presentation resolve. Scene colour
	// and depth remain scene-sized; history targets are native output-sized.
	// historyReadTarget may be NULL when seeding or invalidating history, and
	// both history targets may be NULL to request a jitter-recentered spatial
	// present. Returns false without queuing only when neither safe route can be
	// represented, so the game can retain its existing SMAA/spatial path.
	virtual bool			ResolveTemporalPresentation(
		idRenderTexture *sceneColorTarget,
		idRenderTexture *sceneDepthTarget,
		idRenderTexture *historyReadTarget,
		idRenderTexture *historyWriteTarget ) = 0;
};

extern idRenderSystem *		renderSystem;

#endif /* !__RENDERER_H__ */
