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
#include "DXT/DXTCodec.h"
#include "RendererBootstrap.h"
#include "RendererMetrics.h"
#include "RendererUpload.h"
#include "RenderGraph.h"
#include "ScenePackets.h"
#include "ModernGLExecutor.h"
#include "ModernGLDrawPlan.h"
#include "ModernGLShaderLibrary.h"
#include "ModernGLSubmitPlan.h"
#include "../framework/RenderDoc.h"

// Detect the Microsoft software OpenGL wrapper and guide the user toward
// installing proper vendor drivers.
#ifdef _WIN32
#include "../sys/win32/win_local.h"
#endif

// functions that are not called every frame

static void R_ErrorForUnsupportedCompatibilityOpenGL( void ) {
	common->Error( common->GetLanguageDict()->GetString( "#str_41106" ) );
}

bool R_CheckExtension( char *name );
extern idCVar r_inhibitFragmentProgram;

static idStr g_missingRequiredOpenGLFeatures;

static void R_ClearMissingRequiredOpenGLFeatures( void ) {
	g_missingRequiredOpenGLFeatures.Clear();
}

static void R_RecordMissingRequiredOpenGLFeature( const char *name ) {
	if ( name == NULL || name[0] == '\0' ) {
		return;
	}

	if ( g_missingRequiredOpenGLFeatures.Length() > 0 ) {
		g_missingRequiredOpenGLFeatures += ", ";
	}
	g_missingRequiredOpenGLFeatures += name;
}

static bool R_CheckRequiredExtension( const char *name ) {
	const bool available = R_CheckExtension( const_cast<char *>( name ) );
	if ( !available ) {
		R_RecordMissingRequiredOpenGLFeature( name );
	}
	return available;
}

static void R_ErrorForMissingRequiredOpenGLFeatures( void ) {
	if ( g_missingRequiredOpenGLFeatures.Length() > 0 ) {
		common->Printf(
			"Missing required OpenGL features: %s\n",
			g_missingRequiredOpenGLFeatures.c_str() );
	}

	if ( RenderDoc_IsInjected() ) {
		common->Printf(
			"RenderDoc detected during OpenGL initialization.\n"
			"OpenQ4 currently requires OpenGL compatibility / ARB2-era features "
			"that are unavailable in the injected context.\n" );
		R_ErrorForUnsupportedCompatibilityOpenGL();
	}

	common->Error( common->GetLanguageDict()->GetString( "#str_06780" ) );
}

static bool R_CheckGLSLProgramExtensions() {
	return R_CheckExtension( "GL_ARB_shader_objects" )
		&& R_CheckExtension( "GL_ARB_vertex_shader" )
		&& R_CheckExtension( "GL_ARB_fragment_shader" )
		&& R_CheckExtension( "GL_ARB_shading_language_100" );
}

static bool R_CanUseGLSLPrograms() {
	if ( r_inhibitFragmentProgram.GetBool() ) {
		return false;
	}

	if ( idAsyncNetwork::serverDedicated.GetBool() ) {
		return false;
	}

	if ( !glConfig.ARBVertexProgramAvailable || !glConfig.ARBFragmentProgramAvailable ) {
		return false;
	}

	return R_CheckGLSLProgramExtensions();
}

glconfig_t	glConfig;

static void GfxInfo_f( void );

const char *r_rendererArgs[] = { "best", "arb", "arb2", "Cg", "exp", "nv10", "nv20", "r200", NULL };
const char *r_glTierArgs[] = { "auto", "legacy", "gl33", "gl41", "gl43", "gl45", "gl46", NULL };

idCVar r_inhibitFragmentProgram( "r_inhibitFragmentProgram", "0", CVAR_RENDERER | CVAR_BOOL, "ignore the fragment program extension" );
idCVar r_glDriver( "r_glDriver", "", CVAR_RENDERER, "\"opengl32\", etc." );
idCVar r_useLightPortalFlow( "r_useLightPortalFlow", "1", CVAR_RENDERER | CVAR_BOOL, "use a more precise area reference determination" );
idCVar r_multiSamples( "r_multiSamples", "0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_INTEGER, "number of antialiasing samples" );
idCVar r_postAA( "r_postAA", "0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_INTEGER, "post AA mode: 0 = off, 1 = SMAA 1x", 0, 1, idCmdSystem::ArgCompletion_Integer<0,1> );
idCVar r_bloom( "r_bloom", "0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL, "enable bloom post-process" );
idCVar r_lensFlare( "r_lensFlare", "0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_INTEGER, "light corona / lens flare quality: 0 = off, 1 = coronas, 2 = high quality", 0, 2, idCmdSystem::ArgCompletion_Integer<0,2> );
idCVar r_bloomThreshold( "r_bloomThreshold", "0.7", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT, "bloom bright-pass threshold in scene-referred units", 0.0f, 16.0f );
idCVar r_bloomSoftKnee( "r_bloomSoftKnee", "0.15", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT, "relative bloom soft-threshold knee", 0.0f, 1.0f );
idCVar r_bloomIntensity( "r_bloomIntensity", "0.8", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT, "bloom contribution scale", 0.0f, 4.0f );
idCVar r_bloomRadius( "r_bloomRadius", "1.35", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT, "bloom sample radius scale", 0.1f, 8.0f );
idCVar r_bloomMipCount( "r_bloomMipCount", "5", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_INTEGER, "number of bloom pyramid levels", 1, 5, idCmdSystem::ArgCompletion_Integer<1,5> );
idCVar r_ssao( "r_ssao", "0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL, "enable screen-space ambient occlusion" );
idCVar r_ssaoRadius( "r_ssaoRadius", "36.0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT, "SSAO sampling radius in view-space units", 4.0f, 256.0f );
idCVar r_ssaoBias( "r_ssaoBias", "2.0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT, "SSAO horizon bias in view-space units", 0.0f, 32.0f );
idCVar r_ssaoIntensity( "r_ssaoIntensity", "1.35", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT, "SSAO darkening strength", 0.0f, 4.0f );
idCVar r_ssaoPower( "r_ssaoPower", "1.6", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT, "SSAO response curve", 0.1f, 4.0f );
idCVar r_ssaoMaxDistance( "r_ssaoMaxDistance", "220.0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT, "fade SSAO out past this view-space distance", 16.0f, 4096.0f );
idCVar r_ssaoSamples( "r_ssaoSamples", "20", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_INTEGER, "number of SSAO spiral samples", 4, 32, idCmdSystem::ArgCompletion_Integer<4,32> );
idCVar r_ssaoDebug( "r_ssaoDebug", "0", CVAR_RENDERER | CVAR_BOOL, "visualize SSAO only" );
idCVar r_motionBlur( "r_motionBlur", "0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL, "enable subtle camera motion blur post-process" );
idCVar r_motionBlurStrength( "r_motionBlurStrength", "0.45", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT, "motion blur strength multiplier", 0.0f, 2.0f );
idCVar r_motionBlurMaxPixels( "r_motionBlurMaxPixels", "10", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT, "maximum motion blur radius in pixels", 0.0f, 64.0f );
idCVar r_motionBlurSamples( "r_motionBlurSamples", "8", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_INTEGER, "number of motion blur gather samples", 1, 16, idCmdSystem::ArgCompletion_Integer<1,16> );
idCVar r_motionBlurDebug( "r_motionBlurDebug", "0", CVAR_RENDERER | CVAR_BOOL, "visualize motion blur vectors instead of applying blur" );
idCVar r_motionBlurObjectVectors( "r_motionBlurObjectVectors", "1", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL, "include rigid-object motion vectors when r_motionBlur is enabled" );
idCVar r_forceSpecialEffects( "r_forceSpecialEffects", "0", CVAR_RENDERER | CVAR_INTEGER,
	"force legacy special-effect bitmask for debugging (1=blur, 2=AL, 3=both)", 0, 3, idCmdSystem::ArgCompletion_Integer<0,3> );
idCVar r_hdrSceneTarget( "r_hdrSceneTarget", "1", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL, "render the main scene into an HDR scene target before post-processing" );
idCVar r_hdrToneMap( "r_hdrToneMap", "0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL, "enable filmic tone mapping and color correction pass" );
idCVar r_hdrExposure( "r_hdrExposure", "1.0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT, "manual exposure multiplier applied after auto exposure", 0.1f, 16.0f );
idCVar r_hdrWhitePoint( "r_hdrWhitePoint", "6.0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT, "filmic white point used by tone mapping", 1.0f, 16.0f );
idCVar r_hdrLift( "r_hdrLift", "0.0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT, "post-process shadow lift applied when tone mapping is enabled", -0.25f, 0.25f );
idCVar r_hdrPostGamma( "r_hdrPostGamma", "1.0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT, "post-process gamma curve applied when tone mapping is enabled", 0.5f, 2.5f );
idCVar r_hdrGain( "r_hdrGain", "1.0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT, "post-process gain applied when tone mapping is enabled", 0.5f, 2.0f );
idCVar r_hdrVibrance( "r_hdrVibrance", "0.0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT, "post-process vibrance applied when tone mapping is enabled", -1.0f, 1.0f );
idCVar r_hdrSaturation( "r_hdrSaturation", "1.0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT, "post-process saturation applied when tone mapping is enabled", 0.0f, 2.0f );
idCVar r_hdrContrast( "r_hdrContrast", "1.0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT, "post-process contrast applied when tone mapping is enabled", 0.1f, 3.0f );
idCVar r_hdrAutoExposure( "r_hdrAutoExposure", "0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL, "derive exposure from a log-average scene luminance pyramid" );
idCVar r_hdrKeyValue( "r_hdrKeyValue", "0.18", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT, "middle-gray key value used by HDR auto exposure", 0.01f, 1.0f );
idCVar r_hdrMinExposure( "r_hdrMinExposure", "0.25", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT, "minimum auto-exposure multiplier", 0.01f, 16.0f );
idCVar r_hdrMaxExposure( "r_hdrMaxExposure", "8.0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT, "maximum auto-exposure multiplier", 0.01f, 32.0f );
idCVar r_hdrAdaptUpSpeed( "r_hdrAdaptUpSpeed", "3.0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT, "adaptation speed when exposure needs to brighten", 0.01f, 16.0f );
idCVar r_hdrAdaptDownSpeed( "r_hdrAdaptDownSpeed", "1.5", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT, "adaptation speed when exposure needs to darken", 0.01f, 16.0f );
idCVar r_hdrHighlightDesaturation( "r_hdrHighlightDesaturation", "0.35", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT, "desaturate tone-mapped highlights before the final clamp", 0.0f, 1.0f );
idCVar r_hdrGamutCompression( "r_hdrGamutCompression", "1.0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT, "compress saturated highlights before the final clamp", 0.0f, 4.0f );
idCVar r_hdrSRGBTextures( "r_hdrSRGBTextures", "0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL, "experimental strict sRGB texture decode path; disabled by default until the full renderer is linearized" );
idCVar r_hdrSRGB( "r_hdrSRGB", "0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL, "experimental final framebuffer sRGB conversion path; disabled by default until the full renderer is linearized" );
idCVar r_hdrDebugView( "r_hdrDebugView", "0", CVAR_RENDERER | CVAR_INTEGER, "HDR debug view: 0 = off, 1 = pre-tonemap heatmap, 2 = log-luminance grayscale", 0, 2, idCmdSystem::ArgCompletion_Integer<0,2> );
idCVar r_crt( "r_crt", "0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL, "enable CRT monitor post-process" );
idCVar r_crtAmount( "r_crtAmount", "1.0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT, "overall blend amount for the CRT monitor post-process", 0.0f, 1.0f );
idCVar r_crtScanlineStrength( "r_crtScanlineStrength", "0.55", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT, "scanline intensity for the CRT monitor post-process", 0.0f, 1.0f );
idCVar r_crtMaskStrength( "r_crtMaskStrength", "0.35", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT, "phosphor mask intensity for the CRT monitor post-process", 0.0f, 1.0f );
idCVar r_crtCurvature( "r_crtCurvature", "0.01", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT, "screen curvature amount for the CRT monitor post-process", 0.0f, 0.25f );
idCVar r_crtChromatic( "r_crtChromatic", "1.35", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT, "channel convergence offset in pixel units for the CRT monitor post-process", 0.0f, 8.0f );
idCVar r_msaaResolveDepth( "r_msaaResolveDepth", "0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL, "resolve depth when blitting MSAA render targets" );
idCVar r_msaaAlphaToCoverage( "r_msaaAlphaToCoverage", "1", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL, "enable alpha-to-coverage for perforated materials on MSAA render targets" );
idCVar r_mode( "r_mode", "-2", CVAR_ARCHIVE | CVAR_RENDERER | CVAR_INTEGER, "video mode number (-2 = desktop native, -1 = custom, 0+ = predefined)" );
idCVar r_displayRefresh( "r_displayRefresh", "0", CVAR_RENDERER | CVAR_INTEGER | CVAR_NOCHEAT, "optional display refresh rate option for vid mode", 0.0f, 200.0f );
idCVar r_fullscreen( "r_fullscreen", "1", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL, "0 = windowed, 1 = full screen" );
idCVar r_fullscreenDesktop( "r_fullscreenDesktop", "1", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL, "1 = native desktop fullscreen, 0 = exclusive mode using r_mode/r_customWidth/r_customHeight" );
idCVar r_borderless( "r_borderless", "0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL, "1 = borderless window mode when r_fullscreen is 0" );
idCVar r_windowWidth( "r_windowWidth", "1280", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_INTEGER, "windowed mode width" );
idCVar r_windowHeight( "r_windowHeight", "720", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_INTEGER, "windowed mode height" );
idCVar r_customWidth( "r_customWidth", "1920", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_INTEGER, "custom screen width. set r_mode to -1 to activate" );
idCVar r_customHeight( "r_customHeight", "1080", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_INTEGER, "custom screen height. set r_mode to -1 to activate" );
idCVar r_singleTriangle( "r_singleTriangle", "0", CVAR_RENDERER | CVAR_BOOL, "only draw a single triangle per primitive" );
idCVar r_checkBounds( "r_checkBounds", "0", CVAR_RENDERER | CVAR_BOOL, "compare all surface bounds with precalculated ones" );

idCVar r_useNV20MonoLights( "r_useNV20MonoLights", "1", CVAR_RENDERER | CVAR_INTEGER, "use pass optimization for mono lights" );
idCVar r_useConstantMaterials( "r_useConstantMaterials", "1", CVAR_RENDERER | CVAR_BOOL, "use pre-calculated material registers if possible" );
idCVar r_useTripleTextureARB( "r_useTripleTextureARB", "1", CVAR_RENDERER | CVAR_BOOL, "cards with 3+ texture units do a two pass instead of three pass" );
idCVar r_useSilRemap( "r_useSilRemap", "1", CVAR_RENDERER | CVAR_BOOL, "consider verts with the same XYZ, but different ST the same for shadows" );
idCVar r_useNodeCommonChildren( "r_useNodeCommonChildren", "1", CVAR_RENDERER | CVAR_BOOL, "stop pushing reference bounds early when possible" );
idCVar r_useShadowProjectedCull( "r_useShadowProjectedCull", "1", CVAR_RENDERER | CVAR_BOOL, "discard triangles outside light volume before shadowing" );
idCVar r_useShadowVertexProgram( "r_useShadowVertexProgram", "1", CVAR_RENDERER | CVAR_BOOL, "do the shadow projection in the vertex program on capable cards" );
idCVar r_useShadowMap( "r_useShadowMap", "0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL, "use a simple shadow-map path for projected and point lights when supported" );
idCVar r_shadowMapCSM( "r_shadowMapCSM", "0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL, "use projected-light cascaded shadow maps when shadow maps are enabled" );
idCVar r_shadowMapHashedAlpha( "r_shadowMapHashedAlpha", "1", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL, "use hashed alpha testing for perforated shadow-map casters when supported" );
idCVar r_shadowMapTranslucentMoments( "r_shadowMapTranslucentMoments", "0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL, "accumulate experimental translucent shadow moments for blended casters" );
idCVar r_shadowMapTranslucentDensity( "r_shadowMapTranslucentDensity", "1.0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT, "density scale applied when resolving translucent shadow moments", 0.0f, 8.0f );
idCVar r_shadowMapTranslucentMinAlpha( "r_shadowMapTranslucentMinAlpha", "0.02", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT, "minimum per-stage alpha considered by translucent shadow moments", 0.0f, 1.0f );
idCVar r_shadowMapReport( "r_shadowMapReport", "0", CVAR_RENDERER | CVAR_INTEGER, "shadow-map diagnostics: 0 = off, 1 = per-view summary, 2 = per-light decisions", 0, 2, idCmdSystem::ArgCompletion_Integer<0,2> );
idCVar r_shadowMapReportInterval( "r_shadowMapReportInterval", "30", CVAR_RENDERER | CVAR_INTEGER, "frames between shadow-map diagnostic reports when r_shadowMapReport is enabled", 1, 3600 );
idCVar r_enhancedMaterials( "r_enhancedMaterials", "0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL, "use enhanced GLSL interaction shading for existing materials when supported" );
idCVar r_enhancedMaterialNormalScale( "r_enhancedMaterialNormalScale", "1.25", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT, "tangent-space normal XY scale when enhanced material shading is enabled", 0.5f, 2.0f );
idCVar r_enhancedMaterialSpecularBoost( "r_enhancedMaterialSpecularBoost", "1.15", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT, "specular intensity scale when enhanced material shading is enabled", 0.0f, 4.0f );
idCVar r_enhancedMaterialFresnel( "r_enhancedMaterialFresnel", "0.65", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT, "grazing-angle fresnel contribution when enhanced material shading is enabled", 0.0f, 1.0f );
idCVar r_useShadowSurfaceScissor( "r_useShadowSurfaceScissor", "1", CVAR_RENDERER | CVAR_BOOL, "scissor shadows by the scissor rect of the interaction surfaces" );
idCVar r_useInteractionTable( "r_useInteractionTable", "1", CVAR_RENDERER | CVAR_BOOL, "create a full entityDefs * lightDefs table to make finding interactions faster" );
idCVar r_useTurboShadow( "r_useTurboShadow", "1", CVAR_RENDERER | CVAR_BOOL, "use the infinite projection with W technique for dynamic shadows" );
idCVar r_useTwoSidedStencil( "r_useTwoSidedStencil", "1", CVAR_RENDERER | CVAR_BOOL, "do stencil shadows in one pass with different ops on each side" );
idCVar r_stencilTranslucentShadows( "r_stencilTranslucentShadows", "1", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL, "let translucent materials cast and receive stencil shadows (requires interaction rebuild when toggled)" );
idCVar r_useDeferredTangents( "r_useDeferredTangents", "1", CVAR_RENDERER | CVAR_BOOL, "defer tangents calculations after deform" );
idCVar r_useCachedDynamicModels( "r_useCachedDynamicModels", "1", CVAR_RENDERER | CVAR_BOOL, "cache snapshots of dynamic models" );
idCVar r_useNewSkinning( "r_useNewSkinning", "1", CVAR_RENDERER | CVAR_BOOL, "use retail-style SIMD MD5 skinning data" );
idCVar r_useFastSkinning( "r_useFastSkinning", "0", CVAR_RENDERER | CVAR_BOOL, "approximate MD5 tangent skinning with the dominant joint only" );
idCVar r_deriveBiTangents( "r_deriveBiTangents", "0", CVAR_RENDERER | CVAR_BOOL, "derive bitangents from skinned normals and tangents" );
idCVar r_forceConvertMD5R( "r_forceConvertMD5R", "0", CVAR_RENDERER | CVAR_BOOL, "prefer source md5/proc assets over any future prebuilt MD5R companions" );
idCVar r_convertMD5toMD5R( "r_convertMD5toMD5R", "0", CVAR_RENDERER | CVAR_BOOL, "convert loaded MD5 meshes to packed MD5R form when the build includes rvRenderModelMD5R support" );
idCVar r_convertStaticToMD5R( "r_convertStaticToMD5R", "0", CVAR_RENDERER | CVAR_BOOL, "convert loaded static render models to packed MD5R form when the build includes rvRenderModelMD5R support" );
idCVar r_convertProcToMD5R( "r_convertProcToMD5R", "0", CVAR_RENDERER | CVAR_BOOL, "convert loaded classic proc worlds to packed MD5R form when the build includes MD5R proc support" );
idCVar r_lod_animations_distance( "r_lod_animations_distance", "0", CVAR_RENDERER | CVAR_FLOAT, "distance threshold for MD5 animation-update LOD", 0.0f, 1000000.0f );
idCVar r_lod_animations_wait( "r_lod_animations_wait", "0.25", CVAR_RENDERER | CVAR_FLOAT, "time before a low-coverage MD5 surface is forced to refresh", 0.0f, 10.0f );
idCVar r_lod_animations_coverage( "r_lod_animations_coverage", "0.01", CVAR_RENDERER | CVAR_FLOAT, "screen-coverage threshold for MD5 animation-update LOD", 0.0f, 1.0f );
idCVar r_lod_entities( "r_lod_entities", "1", CVAR_RENDERER | CVAR_BOOL, "enable retail-style entity scissor LOD gating" );
idCVar r_lod_entities_percent( "r_lod_entities_percent", "0.01", CVAR_RENDERER | CVAR_FLOAT, "screen-coverage threshold for keeping entity ambient submissions active", 0.0f, 1.0f );
idCVar r_lod_shadows( "r_lod_shadows", "1", CVAR_RENDERER | CVAR_BOOL, "enable retail-style interaction shadow LOD gating" );
idCVar r_lod_shadows_percent( "r_lod_shadows_percent", "0.01", CVAR_RENDERER | CVAR_FLOAT, "screen-coverage threshold for keeping interaction shadows active", 0.0f, 1.0f );

idCVar r_useVertexBuffers( "r_useVertexBuffers", "1", CVAR_RENDERER | CVAR_INTEGER, "use ARB_vertex_buffer_object for vertexes", 0, 1, idCmdSystem::ArgCompletion_Integer<0,1>  );
idCVar r_useIndexBuffers( "r_useIndexBuffers", "0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_INTEGER, "use ARB_vertex_buffer_object for indexes", 0, 1, idCmdSystem::ArgCompletion_Integer<0,1>  );
idCVar r_useSmp( "r_useSmp", "0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL, "use SMP rendering" );

idCVar r_useStateCaching( "r_useStateCaching", "1", CVAR_RENDERER | CVAR_BOOL, "avoid redundant state changes in GL_*() calls" );
idCVar r_useInfiniteFarZ( "r_useInfiniteFarZ", "1", CVAR_RENDERER | CVAR_BOOL, "use the no-far-clip-plane trick" );

// Quake 4 gameplay code assumes 3.0f as the normal baseline outside cinematics.
// Keeping the default too low significantly reduces distant depth precision and
// causes polygon-offset surfaces (decals/overlays) to z-fight at range.
idCVar r_znear( "r_znear", "3.0", CVAR_RENDERER | CVAR_FLOAT, "near Z clip plane distance", 0.001f, 200.0f );
idCVar cl_gunfov( "cl_gunfov", "0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT | CVAR_NOCHEAT, "first-person weapon FOV override (0 = follow current view FOV)", 0.0f, 179.0f );
idCVar cl_gunfov_adjust( "cl_gunfov_adjust", "1", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL | CVAR_NOCHEAT, "when cl_gunfov is set, keep weapon FOV aspect-correct across screen ratios" );

idCVar r_ignoreGLErrors( "r_ignoreGLErrors", "1", CVAR_RENDERER | CVAR_BOOL, "ignore GL errors" );
idCVar r_finish( "r_finish", "0", CVAR_RENDERER | CVAR_BOOL, "force a call to glFinish() every frame" );
idCVar r_swapInterval( "r_swapInterval", "0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_INTEGER, "changes wglSwapIntarval" );

idCVar r_gamma( "r_gamma", "1", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT, "changes gamma tables", 0.5f, 3.0f );
idCVar r_brightness( "r_brightness", "1", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT, "changes gamma tables", 0.5f, 2.0f );

idCVar r_renderer( "r_renderer", "best", CVAR_RENDERER | CVAR_ARCHIVE, "hardware specific renderer path to use", r_rendererArgs, idCmdSystem::ArgCompletion_String<r_rendererArgs> );
idCVar r_actualRenderer( "r_actualRenderer", "UNINITIALIZED", CVAR_RENDERER | CVAR_ROM, "actual active renderer backend after request/fallback selection" );
idCVar r_glTier( "r_glTier", "auto", CVAR_RENDERER | CVAR_ARCHIVE, "OpenGL renderer tier: auto, legacy, gl33, gl41, gl43, gl45, gl46", r_glTierArgs, idCmdSystem::ArgCompletion_String<r_glTierArgs> );
idCVar r_glDebugContext( "r_glDebugContext", "0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL, "request a debug OpenGL context when the platform backend supports it" );
idCVar r_rendererMetrics( "r_rendererMetrics", "0", CVAR_RENDERER | CVAR_INTEGER, "renderer metrics: 0 = off, 1 = periodic summary, 2 = per-frame/pass detail", 0, 2, idCmdSystem::ArgCompletion_Integer<0,2> );
idCVar r_rendererGpuTimers( "r_rendererGpuTimers", "1", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL, "sample GL timer queries when r_rendererMetrics is enabled and supported" );
idCVar r_rendererUploadMegs( "r_rendererUploadMegs", "16", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_INTEGER, "dynamic renderer upload stream size in megabytes per frame buffer", 1, 128, idCmdSystem::ArgCompletion_Integer<1,128> );
idCVar r_rendererUploadPersistent( "r_rendererUploadPersistent", "1", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL, "allow persistent-mapped dynamic renderer uploads when supported" );
idCVar r_rendererModernExecutor( "r_rendererModernExecutor", "0", CVAR_RENDERER | CVAR_BOOL, "prepare the opt-in modern GL executor frame contract while legacy ARB2 still executes" );
idCVar r_rendererModernSubmit( "r_rendererModernSubmit", "0", CVAR_RENDERER | CVAR_BOOL, "execute opt-in modern GL draw submission before legacy ARB2 fallback; diagnostic until visible pass replacement lands" );
idCVar r_useSimpleInteraction( "r_useSimpleInteraction", "0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL, "use Quake 4's simpler ARB interaction shader pair as an explicit compatibility fallback; may reduce material lighting quality" );
idCVar r_interactionColorMode( "r_interactionColorMode", "0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_INTEGER, "interaction vertex-color mode: 0 = auto, 1 = packed env16.xy, 2 = vector env16/env17", 0, 2, idCmdSystem::ArgCompletion_Integer<0,2> );
idCVar r_shaderReport( "r_shaderReport", "0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_INTEGER, "shader diagnostics: 0 = off, 1 = startup/vid_restart summary, 2 = also warn on invalid program use", 0, 2, idCmdSystem::ArgCompletion_Integer<0,2> );

idCVar r_jitter( "r_jitter", "0", CVAR_RENDERER | CVAR_BOOL, "randomly subpixel jitter the projection matrix" );

idCVar r_skipSuppress( "r_skipSuppress", "0", CVAR_RENDERER | CVAR_BOOL, "ignore the per-view suppressions" );
idCVar r_skipPostProcess( "r_skipPostProcess", "0", CVAR_RENDERER | CVAR_BOOL, "skip all post-process renderings" );
idCVar r_skipLightScale( "r_skipLightScale", "0", CVAR_RENDERER | CVAR_BOOL, "don't do any post-interaction light scaling, makes things dim on low-dynamic range cards" );
idCVar r_skipSky( "r_skipSky", "0", CVAR_RENDERER | CVAR_BOOL, "skip sky rendering" );
idCVar r_skipInteractions( "r_skipInteractions", "0", CVAR_RENDERER | CVAR_BOOL, "skip all light/surface interaction drawing" );
idCVar r_skipDynamicTextures( "r_skipDynamicTextures", "0", CVAR_RENDERER | CVAR_BOOL, "don't dynamically create textures" );
idCVar r_skipCopyTexture( "r_skipCopyTexture", "0", CVAR_RENDERER | CVAR_BOOL, "do all rendering, but don't actually copyTexSubImage2D" );
idCVar r_skipBackEnd( "r_skipBackEnd", "0", CVAR_RENDERER | CVAR_BOOL, "don't draw anything" );
idCVar r_skipRender( "r_skipRender", "0", CVAR_RENDERER | CVAR_BOOL, "skip 3D rendering, but pass 2D" );
idCVar r_skipRenderContext( "r_skipRenderContext", "0", CVAR_RENDERER | CVAR_BOOL, "NULL the rendering context during backend 3D rendering" );
idCVar r_skipTranslucent( "r_skipTranslucent", "0", CVAR_RENDERER | CVAR_BOOL, "skip the translucent interaction rendering" );
idCVar r_skipAmbient( "r_skipAmbient", "0", CVAR_RENDERER | CVAR_BOOL, "bypasses all non-interaction drawing" );
idCVar r_skipNewAmbient( "r_skipNewAmbient", "0", CVAR_RENDERER | CVAR_BOOL | CVAR_ARCHIVE, "bypasses all vertex/fragment program ambient drawing" );
idCVar r_forceAmbient( "r_forceAmbient", "0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT, "force ambient lighting level", 0.0f, 1.0f );
idCVar r_useLightGrid( "r_useLightGrid", "1", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL, "use precomputed irradiance-volume atlases when present" );
idCVar r_lightGridPortalBlend( "r_lightGridPortalBlend", "64", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT, "world-unit radius for blending indirect light grids across visible portal boundaries; 0 disables", 0.0f, 256.0f );
idCVar r_lightGridResidencyFrames( "r_lightGridResidencyFrames", "180", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_INTEGER, "frames to keep light-grid atlases resident after visible/neighbor use", 0, 3600, idCmdSystem::ArgCompletion_Integer<0,3600> );
idCVar r_lightGridBakeWorkers( "r_lightGridBakeWorkers", "0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_INTEGER, "worker threads for light-grid probe integration (-1 = disabled, 0 = auto, 1..8 = explicit)", -1, 8, idCmdSystem::ArgCompletion_Integer<-1,8> );
idCVar r_lightGridBakeAsyncReadback( "r_lightGridBakeAsyncReadback", "1", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL, "use async pixel-pack-buffer readback for light-grid baking when supported" );
idCVar r_lightGridBakeMemoryMB( "r_lightGridBakeMemoryMB", "12", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_INTEGER, "transient memory budget in MB for in-flight light-grid bake jobs (lower reduces RAM/pagefile use)", 4, 256, idCmdSystem::ArgCompletion_Integer<4,256> );
idCVar r_lightGridBakeReadbackSlots( "r_lightGridBakeReadbackSlots", "0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_INTEGER, "async readback slots for light-grid baking (0 = auto, 1..16 = explicit; lower reduces driver/GPU memory use)", 0, 16, idCmdSystem::ArgCompletion_Integer<0,16> );
idCVar r_skipBlendLights( "r_skipBlendLights", "0", CVAR_RENDERER | CVAR_BOOL, "skip all blend lights" );
idCVar r_skipFogLights( "r_skipFogLights", "0", CVAR_RENDERER | CVAR_BOOL, "skip all fog lights" );
idCVar r_skipDeforms( "r_skipDeforms", "0", CVAR_RENDERER | CVAR_BOOL, "leave all deform materials in their original state" );
idCVar r_skipFrontEnd( "r_skipFrontEnd", "0", CVAR_RENDERER | CVAR_BOOL, "bypasses all front end work, but 2D gui rendering still draws" );
idCVar r_skipUpdates( "r_skipUpdates", "0", CVAR_RENDERER | CVAR_BOOL, "1 = don't accept any entity or light updates, making everything static" );
idCVar r_skipDecals( "r_skipDecals", "0", CVAR_RENDERER | CVAR_BOOL, "skip decal surfaces" );
idCVar r_skipOverlays( "r_skipOverlays", "0", CVAR_RENDERER | CVAR_BOOL, "skip overlay surfaces" );
idCVar r_skipSpecular( "r_skipSpecular", "0", CVAR_RENDERER | CVAR_BOOL | CVAR_CHEAT | CVAR_ARCHIVE, "use black for specular1" );
idCVar r_skipBump( "r_skipBump", "0", CVAR_RENDERER | CVAR_BOOL | CVAR_ARCHIVE, "uses a flat surface instead of the bump map" );
idCVar r_skipDiffuse( "r_skipDiffuse", "0", CVAR_RENDERER | CVAR_BOOL, "use black for diffuse" );
idCVar r_skipROQ( "r_skipROQ", "0", CVAR_RENDERER | CVAR_BOOL, "skip ROQ decoding" );

idCVar r_ignore( "r_ignore", "0", CVAR_RENDERER, "used for random debugging without defining new vars" );
idCVar r_ignore2( "r_ignore2", "0", CVAR_RENDERER, "used for random debugging without defining new vars" );
idCVar r_usePreciseTriangleInteractions( "r_usePreciseTriangleInteractions", "0", CVAR_RENDERER | CVAR_BOOL, "1 = do winding clipping to determine if each ambiguous tri should be lit" );
idCVar r_useCulling( "r_useCulling", "2", CVAR_RENDERER | CVAR_INTEGER, "0 = none, 1 = sphere, 2 = sphere + box", 0, 2, idCmdSystem::ArgCompletion_Integer<0,2> );
idCVar r_useLightCulling( "r_useLightCulling", "3", CVAR_RENDERER | CVAR_INTEGER, "0 = none, 1 = box, 2 = exact clip of polyhedron faces, 3 = also areas", 0, 3, idCmdSystem::ArgCompletion_Integer<0,3> );
idCVar r_useLightScissors( "r_useLightScissors", "1", CVAR_RENDERER | CVAR_BOOL, "1 = use custom scissor rectangle for each light" );
idCVar r_useClippedLightScissors( "r_useClippedLightScissors", "1", CVAR_RENDERER | CVAR_INTEGER, "0 = full screen when near clipped, 1 = exact when near clipped, 2 = exact always", 0, 2, idCmdSystem::ArgCompletion_Integer<0,2> );
idCVar r_useEntityCulling( "r_useEntityCulling", "1", CVAR_RENDERER | CVAR_BOOL, "0 = none, 1 = box" );
idCVar r_useEntityScissors( "r_useEntityScissors", "1", CVAR_RENDERER | CVAR_BOOL, "1 = use custom scissor rectangle for each entity" );
idCVar r_useInteractionCulling( "r_useInteractionCulling", "1", CVAR_RENDERER | CVAR_BOOL, "1 = cull interactions" );
idCVar r_useInteractionScissors( "r_useInteractionScissors", "2", CVAR_RENDERER | CVAR_INTEGER, "1 = use a custom scissor rectangle for each shadow interaction, 2 = also crop using portal scissors", -2, 2, idCmdSystem::ArgCompletion_Integer<-2,2> );
idCVar r_limitBatchSize( "r_limitBatchSize", "0", CVAR_RENDERER | CVAR_INTEGER, "retail light-path batch-size cutoff in indexes (0 = disabled)" );
idCVar r_useShadowCulling( "r_useShadowCulling", "1", CVAR_RENDERER | CVAR_BOOL, "try to cull shadows from partially visible lights" );
idCVar r_useFrustumFarDistance( "r_useFrustumFarDistance", "0", CVAR_RENDERER | CVAR_FLOAT, "if != 0 force the view frustum far distance to this distance" );
idCVar r_logFile( "r_logFile", "0", CVAR_RENDERER | CVAR_INTEGER, "number of frames to emit GL logs" );
idCVar r_clear( "r_clear", "2", CVAR_RENDERER, "force screen clear every frame, 1 = purple, 2 = black, 'r g b' = custom" );
idCVar r_offsetFactor( "r_offsetfactor", "0", CVAR_RENDERER | CVAR_FLOAT, "polygon offset parameter" );
idCVar r_offsetUnits( "r_offsetunits", "-600", CVAR_RENDERER | CVAR_FLOAT, "polygon offset parameter" );
idCVar r_shadowPolygonOffset( "r_shadowPolygonOffset", "-1", CVAR_RENDERER | CVAR_FLOAT, "bias value added to depth test for stencil shadow drawing" );
idCVar r_shadowPolygonFactor( "r_shadowPolygonFactor", "0", CVAR_RENDERER | CVAR_FLOAT, "scale value for stencil shadow drawing" );
idCVar r_shadowMapSize( "r_shadowMapSize", "1024", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_INTEGER, "square resolution for the simple projected-light shadow map", 128, 4096 );
idCVar r_shadowMapBias( "r_shadowMapBias", "0.010", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT, "constant receiver depth bias for projected shadow maps", 0.0f, 0.05f );
idCVar r_shadowMapNormalBias( "r_shadowMapNormalBias", "0.020", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT, "geometric normal bias added on sloped projected-light receivers", 0.0f, 0.05f );
idCVar r_shadowMapPointBias( "r_shadowMapPointBias", "0.00020", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT, "constant receiver depth bias for point-light shadow maps", 0.0f, 0.05f );
idCVar r_shadowMapPointNormalBias( "r_shadowMapPointNormalBias", "0.0020", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT, "geometric normal bias added on sloped point-light receivers", 0.0f, 0.05f );
idCVar r_shadowMapFilterRadius( "r_shadowMapFilterRadius", "2.0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT, "projected-light PCF radius in texels for the simple shadow-map path", 0.0f, 8.0f );
idCVar r_shadowMapPointFilterRadius( "r_shadowMapPointFilterRadius", "2.5", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT, "point-light PCF radius in texels for the simple shadow-map path", 0.0f, 8.0f );
idCVar r_shadowMapProjectionPad( "r_shadowMapProjectionPad", "0.15", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT, "normalized padding applied around projected-light shadow-map coverage", 0.0f, 1.0f );
idCVar r_shadowMapCascadeCount( "r_shadowMapCascadeCount", "4", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_INTEGER, "number of projected-light cascades when r_shadowMapCSM is enabled", 1, 4, idCmdSystem::ArgCompletion_Integer<1,4> );
idCVar r_shadowMapCascadeDistance( "r_shadowMapCascadeDistance", "1536", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT, "camera-space distance covered by the cropped projected-light cascades", 64.0f, 8192.0f );
idCVar r_shadowMapCascadeLambda( "r_shadowMapCascadeLambda", "0.75", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT, "blend factor between uniform and logarithmic projected-light cascade splits", 0.0f, 1.0f );
idCVar r_shadowMapCascadeBlend( "r_shadowMapCascadeBlend", "0.15", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT, "fraction of each projected-light cascade range used to blend into the next cascade", 0.0f, 0.5f );
idCVar r_shadowMapDebugOverlay( "r_shadowMapDebugOverlay", "0", CVAR_RENDERER | CVAR_INTEGER,
	"shadow-map overlay: 0 = off, 1 = show the selected shadow map as a top-left mini-map with stats",
	0, 1, idCmdSystem::ArgCompletion_Integer<0, 1> );
idCVar r_shadowMapDebugMode( "r_shadowMapDebugMode", "0", CVAR_RENDERER | CVAR_INTEGER,
	"projected shadow-map debug mode: 0 = off, 1 = atlas/depth, 2 = cascade index, 3 = projected UV, 4 = projected depth, 5 = projected w, 6 = invalid mask",
	0, SHADOWMAP_DEBUGMODE_COUNT - 1, idCmdSystem::ArgCompletion_Integer<0, SHADOWMAP_DEBUGMODE_COUNT - 1> );
idCVar r_shadowMapCascadeStabilize( "r_shadowMapCascadeStabilize", "1", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL, "snap projected-light cascade bounds to texels to reduce shimmering" );
idCVar r_shadowMapPointFarScale( "r_shadowMapPointFarScale", "1.25", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT, "padding multiplier applied to point-light shadow-map range", 1.0f, 4.0f );
idCVar r_shadowMapPolygonFactor( "r_shadowMapPolygonFactor", "2.0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT, "slope-scale depth bias used when rendering the simple projected-light shadow map", 0.0f, 16.0f );
idCVar r_shadowMapPolygonOffset( "r_shadowMapPolygonOffset", "4.0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT, "constant depth bias used when rendering the simple projected-light shadow map", 0.0f, 64.0f );
idCVar r_frontBuffer( "r_frontBuffer", "0", CVAR_RENDERER | CVAR_BOOL, "draw to front buffer for debugging" );
idCVar r_skipSubviews( "r_skipSubviews", "0", CVAR_RENDERER | CVAR_INTEGER, "1 = don't render any gui elements on surfaces" );
idCVar r_skipGuiShaders( "r_skipGuiShaders", "0", CVAR_RENDERER | CVAR_INTEGER, "1 = skip all gui elements on surfaces, 2 = skip drawing but still handle events, 3 = draw but skip events", 0, 3, idCmdSystem::ArgCompletion_Integer<0,3> );
idCVar r_skipParticles( "r_skipParticles", "0", CVAR_RENDERER | CVAR_INTEGER, "1 = skip all particle systems", 0, 1, idCmdSystem::ArgCompletion_Integer<0,1> );
idCVar r_subviewOnly( "r_subviewOnly", "0", CVAR_RENDERER | CVAR_BOOL, "1 = don't render main view, allowing subviews to be debugged" );
idCVar r_shadows( "r_shadows", "1", CVAR_RENDERER | CVAR_BOOL  | CVAR_ARCHIVE, "enable shadows" );
idCVar r_testARBProgram( "r_testARBProgram", "0", CVAR_RENDERER | CVAR_BOOL, "experiment with vertex/fragment programs" );
idCVar r_testGamma( "r_testGamma", "0", CVAR_RENDERER | CVAR_FLOAT, "if > 0 draw a grid pattern to test gamma levels", 0, 195 );
idCVar r_testGammaBias( "r_testGammaBias", "0", CVAR_RENDERER | CVAR_FLOAT, "if > 0 draw a grid pattern to test gamma levels" );
idCVar r_testStepGamma( "r_testStepGamma", "0", CVAR_RENDERER | CVAR_FLOAT, "if > 0 draw a grid pattern to test gamma levels" );
idCVar r_lightScale( "r_lightScale", "2", CVAR_RENDERER | CVAR_FLOAT, "all light intensities are multiplied by this" );
idCVar r_lightDetailLevel( "r_lightDetailLevel", "9", CVAR_RENDERER | CVAR_FLOAT, "minimum light detail level to render" );
idCVar r_lightSourceRadius( "r_lightSourceRadius", "0", CVAR_RENDERER | CVAR_FLOAT, "for soft-shadow sampling" );
idCVar r_flareSize( "r_flareSize", "1", CVAR_RENDERER | CVAR_FLOAT, "scale the flare deforms from the material def" ); 

idCVar r_useExternalShadows( "r_useExternalShadows", "1", CVAR_RENDERER | CVAR_INTEGER, "1 = skip drawing caps when outside the light volume, 2 = force to no caps for testing", 0, 2, idCmdSystem::ArgCompletion_Integer<0,2> );
idCVar r_useOptimizedShadows( "r_useOptimizedShadows", "1", CVAR_RENDERER | CVAR_BOOL, "use the dmap generated static shadow volumes" );
idCVar r_useScissor( "r_useScissor", "1", CVAR_RENDERER | CVAR_BOOL, "scissor clip as portals and lights are processed" );
idCVar r_useCombinerDisplayLists( "r_useCombinerDisplayLists", "1", CVAR_RENDERER | CVAR_BOOL | CVAR_NOCHEAT, "put all nvidia register combiner programming in display lists" );
idCVar r_useDepthBoundsTest( "r_useDepthBoundsTest", "1", CVAR_RENDERER | CVAR_BOOL, "use depth bounds test to reduce shadow fill" );

idCVar r_screenFraction( "r_screenFraction", "100", CVAR_ARCHIVE | CVAR_RENDERER | CVAR_INTEGER, "main-scene resolution scale percentage", 10, 100 );
idCVar r_resolutionScaleMode( "r_resolutionScaleMode", "1", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_INTEGER, "screen-fraction mode when r_screenFraction < 100: 0 = legacy cropped viewport, 1 = bilinear upscale, 2 = high-quality upscale", 0, 2, idCmdSystem::ArgCompletion_Integer<0,2> );
idCVar r_resolutionScaleSharpness( "r_resolutionScaleSharpness", "0.4", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT, "sharpening amount for high-quality resolution scaling", 0.0f, 1.5f );
idCVar r_demonstrateBug( "r_demonstrateBug", "0", CVAR_RENDERER | CVAR_BOOL, "used during development to show IHV's their problems" );
idCVar r_usePortals( "r_usePortals", "1", CVAR_RENDERER | CVAR_BOOL, " 1 = use portals to perform area culling, otherwise draw everything" );
idCVar r_portalsDistanceCull( "r_portalsDistanceCull", "1", CVAR_RENDERER | CVAR_BOOL, "enable distance-cull checks using portal fade cull ranges" );
idCVar r_singleLight( "r_singleLight", "-1", CVAR_RENDERER | CVAR_INTEGER, "suppress all but one light" );
idCVar r_singleEntity( "r_singleEntity", "-1", CVAR_RENDERER | CVAR_INTEGER, "suppress all but one entity" );
idCVar r_singleSurface( "r_singleSurface", "-1", CVAR_RENDERER | CVAR_INTEGER, "suppress all but one surface on each entity" );
idCVar r_singleArea( "r_singleArea", "0", CVAR_RENDERER | CVAR_BOOL, "only draw the portal area the view is actually in" );
idCVar r_forceLoadImages( "r_forceLoadImages", "0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL, "draw all images to screen after registration" );
idCVar r_orderIndexes( "r_orderIndexes", "1", CVAR_RENDERER | CVAR_BOOL, "perform index reorganization to optimize vertex use" );
idCVar r_lightAllBackFaces( "r_lightAllBackFaces", "0", CVAR_RENDERER | CVAR_BOOL, "light all the back faces, even when they would be shadowed" );

// visual debugging info
idCVar r_showPortals( "r_showPortals", "0", CVAR_RENDERER | CVAR_BOOL, "draw portal outlines in color based on passed / not passed" );
idCVar r_showUnsmoothedTangents( "r_showUnsmoothedTangents", "0", CVAR_RENDERER | CVAR_BOOL, "if 1, put all nvidia register combiner programming in display lists" );
idCVar r_showSilhouette( "r_showSilhouette", "0", CVAR_RENDERER | CVAR_BOOL, "highlight edges that are casting shadow planes" );
idCVar r_showVertexColor( "r_showVertexColor", "0", CVAR_RENDERER | CVAR_BOOL, "draws all triangles with the solid vertex color" );
idCVar r_showUpdates( "r_showUpdates", "0", CVAR_RENDERER | CVAR_BOOL, "report entity and light updates and ref counts" );
idCVar r_showDemo( "r_showDemo", "0", CVAR_RENDERER | CVAR_BOOL, "report reads and writes to the demo file" );
idCVar r_showDynamic( "r_showDynamic", "0", CVAR_RENDERER | CVAR_BOOL, "report stats on dynamic surface generation" );
idCVar r_showLightScale( "r_showLightScale", "0", CVAR_RENDERER | CVAR_BOOL, "report the scale factor applied to drawing for overbrights" );
idCVar r_showDefs( "r_showDefs", "0", CVAR_RENDERER | CVAR_BOOL, "report the number of modeDefs and lightDefs in view" );
idCVar r_showTrace( "r_showTrace", "0", CVAR_RENDERER | CVAR_INTEGER, "show the intersection of an eye trace with the world", idCmdSystem::ArgCompletion_Integer<0,2> );
idCVar r_showIntensity( "r_showIntensity", "0", CVAR_RENDERER | CVAR_BOOL, "draw the screen colors based on intensity, red = 0, green = 128, blue = 255" );
idCVar r_showImages( "r_showImages", "0", CVAR_RENDERER | CVAR_INTEGER, "1 = show all images instead of rendering, 2 = show in proportional size", 0, 2, idCmdSystem::ArgCompletion_Integer<0,2> );
idCVar r_showSmp( "r_showSmp", "0", CVAR_RENDERER | CVAR_BOOL, "show which end (front or back) is blocking" );
idCVar r_showLights( "r_showLights", "0", CVAR_RENDERER | CVAR_INTEGER, "1 = just print volumes numbers, highlighting ones covering the view, 2 = also draw planes of each volume, 3 = also draw edges of each volume", 0, 3, idCmdSystem::ArgCompletion_Integer<0,3> );
idCVar r_showViewLights( "r_showViewLights", "0", CVAR_RENDERER | CVAR_INTEGER, "view-origin light diagnostics: 0 = off, 1 = print affecting lights when the set changes, 2 = print affecting lights every interval, 3 = also include visible non-affecting lights", 0, 3, idCmdSystem::ArgCompletion_Integer<0,3> );
idCVar r_showViewLightsInterval( "r_showViewLightsInterval", "10", CVAR_RENDERER | CVAR_INTEGER, "frames between repeated view-origin light reports when r_showViewLights is 2 or 3", 1, 3600 );
idCVar r_showViewLightsVisuals( "r_showViewLightsVisuals", "0", CVAR_RENDERER | CVAR_BOOL, "draw persistent origin/color/radius overlays for the lights most recently reported by r_showViewLights" );
idCVar r_showLightGrid( "r_showLightGrid", "0", CVAR_RENDERER | CVAR_INTEGER, "irradiance-volume debug: 0 = off, 1 = current area valid probes, 2 = all valid probes, 3 = include invalid probes", 0, 3, idCmdSystem::ArgCompletion_Integer<0,3> );
idCVar r_showShadows( "r_showShadows", "0", CVAR_RENDERER | CVAR_INTEGER, "1 = visualize the stencil shadow volumes, 2 = draw filled in", 0, 3, idCmdSystem::ArgCompletion_Integer<0,3> );
idCVar r_showShadowCount( "r_showShadowCount", "0", CVAR_RENDERER | CVAR_INTEGER, "colors screen based on shadow volume depth complexity, >= 2 = print overdraw count based on stencil index values, 3 = only show turboshadows, 4 = only show static shadows", 0, 4, idCmdSystem::ArgCompletion_Integer<0,4> );
idCVar r_showLightScissors( "r_showLightScissors", "0", CVAR_RENDERER | CVAR_BOOL, "show light scissor rectangles" );
idCVar r_showEntityScissors( "r_showEntityScissors", "0", CVAR_RENDERER | CVAR_BOOL, "show entity scissor rectangles" );
idCVar r_showInteractionFrustums( "r_showInteractionFrustums", "0", CVAR_RENDERER | CVAR_INTEGER, "1 = show a frustum for each interaction, 2 = also draw lines to light origin, 3 = also draw entity bbox", 0, 3, idCmdSystem::ArgCompletion_Integer<0,3> );
idCVar r_showInteractionScissors( "r_showInteractionScissors", "0", CVAR_RENDERER | CVAR_INTEGER, "1 = show screen rectangle which contains the interaction frustum, 2 = also draw construction lines", 0, 2, idCmdSystem::ArgCompletion_Integer<0,2> );
idCVar r_showLightCount( "r_showLightCount", "0", CVAR_RENDERER | CVAR_INTEGER, "1 = colors surfaces based on light count, 2 = also count everything through walls, 3 = also print overdraw", 0, 3, idCmdSystem::ArgCompletion_Integer<0,3> );
idCVar r_showViewEntitys( "r_showViewEntitys", "0", CVAR_RENDERER | CVAR_INTEGER, "1 = displays the bounding boxes of all view models, 2 = print index numbers" );
idCVar r_showTris( "r_showTris", "0", CVAR_RENDERER | CVAR_INTEGER, "enables wireframe rendering of the world, 1 = only draw visible ones, 2 = draw all front facing, 3 = draw all", 0, 3, idCmdSystem::ArgCompletion_Integer<0,3> );
idCVar r_showSurfaceInfo( "r_showSurfaceInfo", "0", CVAR_RENDERER | CVAR_BOOL, "show surface material name under crosshair" );
idCVar r_showNormals( "r_showNormals", "0", CVAR_RENDERER | CVAR_FLOAT, "draws wireframe normals" );
idCVar r_showMemory( "r_showMemory", "0", CVAR_RENDERER | CVAR_BOOL, "print frame memory utilization" );
idCVar r_showCull( "r_showCull", "0", CVAR_RENDERER | CVAR_BOOL, "report sphere and box culling stats" );
idCVar r_showInteractions( "r_showInteractions", "0", CVAR_RENDERER | CVAR_BOOL, "report interaction generation activity" );
idCVar r_showDepth( "r_showDepth", "0", CVAR_RENDERER | CVAR_BOOL, "display the contents of the depth buffer and the depth range" );
idCVar r_showSurfaces( "r_showSurfaces", "0", CVAR_RENDERER | CVAR_BOOL, "report surface/light/shadow counts" );
idCVar r_showPrimitives( "r_showPrimitives", "0", CVAR_RENDERER | CVAR_INTEGER, "report drawsurf/index/vertex counts" );
idCVar r_showEdges( "r_showEdges", "0", CVAR_RENDERER | CVAR_BOOL, "draw the sil edges" );
idCVar r_showTexturePolarity( "r_showTexturePolarity", "0", CVAR_RENDERER | CVAR_BOOL, "shade triangles by texture area polarity" );
idCVar r_showTangentSpace( "r_showTangentSpace", "0", CVAR_RENDERER | CVAR_INTEGER, "shade triangles by tangent space, 1 = use 1st tangent vector, 2 = use 2nd tangent vector, 3 = use normal vector", 0, 3, idCmdSystem::ArgCompletion_Integer<0,3> );
idCVar r_showDominantTri( "r_showDominantTri", "0", CVAR_RENDERER | CVAR_BOOL, "draw lines from vertexes to center of dominant triangles" );
idCVar r_showAlloc( "r_showAlloc", "0", CVAR_RENDERER | CVAR_BOOL, "report alloc/free counts" );
idCVar r_showTextureVectors( "r_showTextureVectors", "0", CVAR_RENDERER | CVAR_FLOAT, " if > 0 draw each triangles texture (tangent) vectors" );
idCVar r_showOverDraw( "r_showOverDraw", "0", CVAR_RENDERER | CVAR_INTEGER, "1 = geometry overdraw, 2 = light interaction overdraw, 3 = geometry and light interaction overdraw", 0, 3, idCmdSystem::ArgCompletion_Integer<0,3> );

idCVar r_lockSurfaces( "r_lockSurfaces", "0", CVAR_RENDERER | CVAR_BOOL, "allow moving the view point without changing the composition of the scene, including culling" );
idCVar r_useEntityCallbacks( "r_useEntityCallbacks", "1", CVAR_RENDERER | CVAR_BOOL, "if 0, issue the callback immediately at update time, rather than defering" );

idCVar r_showSkel( "r_showSkel", "0", CVAR_RENDERER | CVAR_INTEGER, "draw the skeleton when model animates, 1 = draw model with skeleton, 2 = draw skeleton only, 3 = draw joints only", 0, 3, idCmdSystem::ArgCompletion_Integer<0,3> );
idCVar r_jointNameScale( "r_jointNameScale", "0.02", CVAR_RENDERER | CVAR_FLOAT, "size of joint names when r_showskel is set to 1" );
idCVar r_jointNameOffset( "r_jointNameOffset", "0.5", CVAR_RENDERER | CVAR_FLOAT, "offset of joint names when r_showskel is set to 1" );

idCVar r_cgVertexProfile( "r_cgVertexProfile", "best", CVAR_RENDERER | CVAR_ARCHIVE, "arbvp1, vp20, vp30" );     
idCVar r_cgFragmentProfile( "r_cgFragmentProfile", "best", CVAR_RENDERER | CVAR_ARCHIVE, "arbfp1, fp30" );

idCVar r_debugLineDepthTest( "r_debugLineDepthTest", "0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL, "perform depth test on debug lines" );
idCVar r_debugLineWidth( "r_debugLineWidth", "1", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL, "width of debug lines" );
idCVar r_debugArrowStep( "r_debugArrowStep", "120", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_INTEGER, "step size of arrow cone line rotation in degrees", 0, 120 );
idCVar r_debugPolygonFilled( "r_debugPolygonFilled", "1", CVAR_RENDERER | CVAR_BOOL, "draw a filled polygon" );

idCVar r_materialOverride( "r_materialOverride", "", CVAR_RENDERER, "overrides all materials", idCmdSystem::ArgCompletion_Decl<DECL_MATERIAL> );

idCVar r_debugRenderToTexture( "r_debugRenderToTexture", "0", CVAR_RENDERER | CVAR_INTEGER, "" );

/*
=================
R_CheckExtension
=================
*/
bool R_CheckExtension( char *name ) {
	if ( name == NULL || name[0] == '\0' ) {
		return false;
	}

	if ( !GLCapabilityProbe_HasExtension( name ) ) {
		common->Printf( "X..%s not found\n", name );
		return false;
	}

	common->Printf( "...using %s\n", name );
	return true;
}

static void R_RendererTierSelfTest_f( const idCmdArgs &args ) {
	(void)args;
	if ( !RendererTierSelect_RunSelfTest() ) {
		common->Warning( "Renderer tier selector self-test failed" );
	}
}

static void R_RendererContextLadderSelfTest_f( const idCmdArgs &args ) {
	(void)args;
	if ( !RendererContextLadder_RunSelfTest() ) {
		common->Warning( "Renderer context ladder self-test failed" );
	}
}

static void R_RendererUploadSelfTest_f( const idCmdArgs &args ) {
	(void)args;
	if ( !RendererUpload_RunSelfTest() ) {
		common->Warning( "Renderer upload self-test failed" );
	}
}

static void R_RendererGpuTimerSelfTest_f( const idCmdArgs &args ) {
	(void)args;
	if ( !RendererGpuTimer_RunSelfTest() ) {
		common->Warning( "Renderer GPU timer self-test failed" );
	}
}

static void R_RendererScenePacketSelfTest_f( const idCmdArgs &args ) {
	(void)args;
	if ( !RendererScenePacket_RunSelfTest() ) {
		common->Warning( "Renderer scene-packet self-test failed" );
	}
}

static void R_RendererRenderGraphSelfTest_f( const idCmdArgs &args ) {
	(void)args;
	if ( !RendererRenderGraph_RunSelfTest() ) {
		common->Warning( "Renderer render-graph self-test failed" );
	}
}

static void R_RendererModernGLExecutorSelfTest_f( const idCmdArgs &args ) {
	(void)args;
	if ( !RendererModernGLExecutor_RunSelfTest() ) {
		common->Warning( "Renderer modern GL executor self-test failed" );
	}
}

static void R_RendererModernGLShaderLibrarySelfTest_f( const idCmdArgs &args ) {
	(void)args;
	if ( !RendererModernGLShaderLibrary_RunSelfTest() ) {
		common->Warning( "Renderer modern GL shader-library self-test failed" );
	}
}

static void R_RendererModernGLDrawPlanSelfTest_f( const idCmdArgs &args ) {
	(void)args;
	if ( !RendererModernGLDrawPlan_RunSelfTest() ) {
		common->Warning( "Renderer modern GL draw-plan self-test failed" );
	}
}

static void R_RendererModernGLSubmitPlanSelfTest_f( const idCmdArgs &args ) {
	(void)args;
	if ( !RendererModernGLSubmitPlan_RunSelfTest() ) {
		common->Warning( "Renderer modern GL submit-plan self-test failed" );
	}
}

/*
==================
R_CheckPortableExtensions

==================
*/
static void R_CheckPortableExtensions( void ) {
	glConfig.glVersion = atof( glConfig.version_string );
	R_ClearMissingRequiredOpenGLFeatures();

	common->Printf("Init Glew...\n");

	glewExperimental = GL_TRUE;
	if (glewInit() != GLEW_OK)
		common->FatalError("Failed to init glew!\n");
	while ( glGetError() != GL_NO_ERROR ) {
	}

	GLCapabilityProbe_Build( glConfig.backendCaps, glConfig.version_string, glConfig.extensions_string );
	glConfig.extensions_string = GLCapabilityProbe_ExtensionString();
	RendererBootstrap_BeginOpenGL( glConfig.backendCaps, r_glTier.GetString() );
	glConfig.rendererTier = RendererBootstrap_GetState().selectedTier;
	glConfig.renderFeatures = RendererBootstrap_GetState().features;

	const rendererTierPreference_t requestedTier = RendererTierPreference_FromString( r_glTier.GetString() );
	const rendererTier_t forcedTier = RendererTierPreference_ToForcedTier( requestedTier );
	if ( forcedTier != RENDERER_TIER_NULL && forcedTier != glConfig.rendererTier ) {
		common->Warning(
			"r_glTier \"%s\" is not fully supported by this context; selected %s instead",
			r_glTier.GetString(),
			RendererTier_Name( glConfig.rendererTier ) );
	}

	// GL_ARB_multitexture
	glConfig.multitextureAvailable = R_CheckRequiredExtension( "GL_ARB_multitexture" );
	if ( glConfig.multitextureAvailable ) {
		//glGetIntegerv( GL_MAX_TEXTURE_UNITS_ARB, (GLint *)&glConfig.maxTextureUnits );
		//if ( glConfig.maxTextureUnits > MAX_MULTITEXTURE_UNITS ) {
		//	glConfig.maxTextureUnits = MAX_MULTITEXTURE_UNITS;
		//}
		glConfig.maxTextureUnits = 16; // jmarshall: OpenGL LIES
		if ( glConfig.maxTextureUnits < 2 ) {
			glConfig.multitextureAvailable = false;	// shouldn't ever happen
		}
		glGetIntegerv(GL_MAX_TEXTURE_COORDS_ARB, (GLint*)&glConfig.maxTextureCoords);
		glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS_ARB, (GLint*)&glConfig.maxTextureImageUnits);
	}

	glConfig.maxDrawBuffers = 1;
	glConfig.maxColorAttachments = 1;
	if ( GLEW_ARB_draw_buffers || glConfig.glVersion >= 2.0f ) {
		glGetIntegerv( GL_MAX_DRAW_BUFFERS_ARB, (GLint *)&glConfig.maxDrawBuffers );
	}
	if ( GLEW_EXT_framebuffer_object || GLEW_ARB_framebuffer_object || glConfig.glVersion >= 3.0f ) {
		glGetIntegerv( GL_MAX_COLOR_ATTACHMENTS_EXT, (GLint *)&glConfig.maxColorAttachments );
	}
	glConfig.textureSRGBAvailable =
		( GLEW_EXT_texture_sRGB || glConfig.glVersion >= 2.1f );
	glConfig.framebufferSRGBAvailable =
		( GLEW_EXT_framebuffer_sRGB || GLEW_ARB_framebuffer_sRGB || glConfig.glVersion >= 3.0f );

	// GL_ARB_texture_env_combine
	glConfig.textureEnvCombineAvailable = R_CheckRequiredExtension( "GL_ARB_texture_env_combine" );

	// GL_ARB_texture_cube_map
	glConfig.cubeMapAvailable = R_CheckRequiredExtension( "GL_ARB_texture_cube_map" );
	if ( glConfig.cubeMapAvailable && ( GLEW_ARB_seamless_cube_map || glConfig.glVersion >= 3.2f ) ) {
		glEnable( GL_TEXTURE_CUBE_MAP_SEAMLESS );
		common->Printf( "...enabled GL_TEXTURE_CUBE_MAP_SEAMLESS\n" );
	}

	// GL_ARB_texture_env_dot3
	glConfig.envDot3Available = R_CheckRequiredExtension( "GL_ARB_texture_env_dot3" );

	// GL_ARB_texture_env_add
	glConfig.textureEnvAddAvailable = R_CheckExtension( "GL_ARB_texture_env_add" );

	// GL_ARB_texture_non_power_of_two
	glConfig.textureNonPowerOfTwoAvailable = R_CheckExtension( "GL_ARB_texture_non_power_of_two" );

	// GL_ARB_texture_compression + GL_S3_s3tc
	// DRI drivers may have GL_ARB_texture_compression but no GL_EXT_texture_compression_s3tc
	if ( R_CheckExtension( "GL_ARB_texture_compression" ) && R_CheckExtension( "GL_EXT_texture_compression_s3tc" ) ) {
		glConfig.textureCompressionAvailable = true;
	} else {
		glConfig.textureCompressionAvailable = false;
	}

	// GL_EXT_texture_filter_anisotropic
	glConfig.anisotropicAvailable = R_CheckExtension( "GL_EXT_texture_filter_anisotropic" );
	if ( glConfig.anisotropicAvailable ) {
		glGetFloatv( GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &glConfig.maxTextureAnisotropy );
		common->Printf( "   maxTextureAnisotropy: %f\n", glConfig.maxTextureAnisotropy );
	} else {
		glConfig.maxTextureAnisotropy = 1;
	}

	// GL_EXT_texture_lod_bias
	// The actual extension is broken as specificed, storing the state in the texture unit instead
	// of the texture object.  The behavior in GL 1.4 is the behavior we use.
	if ( glConfig.glVersion >= 1.4 || R_CheckExtension( "GL_EXT_texture_lod" ) ) {
		common->Printf( "...using %s\n", "GL_1.4_texture_lod_bias" );
		glConfig.textureLODBiasAvailable = true;
	} else {
		common->Printf( "X..%s not found\n", "GL_1.4_texture_lod_bias" );
		glConfig.textureLODBiasAvailable = false;
	}

	// GL_EXT_shared_texture_palette
	glConfig.sharedTexturePaletteAvailable = R_CheckExtension( "GL_EXT_shared_texture_palette" );
	if ( glConfig.sharedTexturePaletteAvailable ) {
	//	glColorTableEXT = ( void ( APIENTRY * ) ( int, int, int, int, int, const void * ) ) GLimp_ExtensionPointer( "glColorTableEXT" );
	}

	// GL_EXT_texture3D (not currently used for anything)
	glConfig.texture3DAvailable = R_CheckExtension( "GL_EXT_texture3D" );
	if ( glConfig.texture3DAvailable ) {
	//	glTexImage3D = 
	//		(void (APIENTRY *)(GLenum, GLint, GLint, GLsizei, GLsizei, GLsizei, GLint, GLenum, GLenum, const GLvoid *) )
	//		GLimp_ExtensionPointer( "glTexImage3D" );
	}

	// EXT_stencil_wrap
	// This isn't very important, but some pathological case might cause a clamp error and give a shadow bug.
	// Nvidia also believes that future hardware may be able to run faster with this enabled to avoid the
	// serialization of clamping.
	if ( R_CheckExtension( "GL_EXT_stencil_wrap" ) ) {
		tr.stencilIncr = GL_INCR_WRAP_EXT;
		tr.stencilDecr = GL_DECR_WRAP_EXT;
	} else {
		tr.stencilIncr = GL_INCR;
		tr.stencilDecr = GL_DECR;
	}

	// GL_NV_register_combiners
	glConfig.registerCombinersAvailable = R_CheckExtension( "GL_NV_register_combiners" );
	

	// GL_EXT_stencil_two_side
	glConfig.twoSidedStencilAvailable = R_CheckExtension( "GL_EXT_stencil_two_side" );
	if ( glConfig.twoSidedStencilAvailable ) {
		//glActiveStencilFaceEXT = (PFNGLACTIVESTENCILFACEEXTPROC)GLimp_ExtensionPointer( "glActiveStencilFaceEXT" );
	} else {
		glConfig.atiTwoSidedStencilAvailable = R_CheckExtension( "GL_ATI_separate_stencil" );
		//if ( glConfig.atiTwoSidedStencilAvailable ) {
		//	glStencilFuncSeparateATI  = (PFNGLSTENCILFUNCSEPARATEATIPROC)GLimp_ExtensionPointer( "glStencilFuncSeparateATI" );
		//	glStencilOpSeparateATI = (PFNGLSTENCILOPSEPARATEATIPROC)GLimp_ExtensionPointer( "glStencilOpSeparateATI" );
		//}
	}

	// GL_ATI_fragment_shader
	glConfig.atiFragmentShaderAvailable = R_CheckExtension( "GL_ATI_fragment_shader" );
	if (! glConfig.atiFragmentShaderAvailable ) {
		// only on OSX: ATI_fragment_shader is faked through ATI_text_fragment_shader (macosx_glimp.cpp)
		glConfig.atiFragmentShaderAvailable = R_CheckExtension( "GL_ATI_text_fragment_shader" );
	}
	if ( glConfig.atiFragmentShaderAvailable ) {
		//glGenFragmentShadersATI = (PFNGLGENFRAGMENTSHADERSATIPROC)GLimp_ExtensionPointer( "glGenFragmentShadersATI" );
		//glBindFragmentShaderATI = (PFNGLBINDFRAGMENTSHADERATIPROC)GLimp_ExtensionPointer( "glBindFragmentShaderATI" );
		//glDeleteFragmentShaderATI = (PFNGLDELETEFRAGMENTSHADERATIPROC)GLimp_ExtensionPointer( "glDeleteFragmentShaderATI" );
		//glBeginFragmentShaderATI = (PFNGLBEGINFRAGMENTSHADERATIPROC)GLimp_ExtensionPointer( "glBeginFragmentShaderATI" );
		//glEndFragmentShaderATI = (PFNGLENDFRAGMENTSHADERATIPROC)GLimp_ExtensionPointer( "glEndFragmentShaderATI" );
		//glPassTexCoordATI = (PFNGLPASSTEXCOORDATIPROC)GLimp_ExtensionPointer( "glPassTexCoordATI" );
		//glSampleMapATI = (PFNGLSAMPLEMAPATIPROC)GLimp_ExtensionPointer( "glSampleMapATI" );
		//glColorFragmentOp1ATI = (PFNGLCOLORFRAGMENTOP1ATIPROC)GLimp_ExtensionPointer( "glColorFragmentOp1ATI" );
		//glColorFragmentOp2ATI = (PFNGLCOLORFRAGMENTOP2ATIPROC)GLimp_ExtensionPointer( "glColorFragmentOp2ATI" );
		//glColorFragmentOp3ATI = (PFNGLCOLORFRAGMENTOP3ATIPROC)GLimp_ExtensionPointer( "glColorFragmentOp3ATI" );
		//glAlphaFragmentOp1ATI = (PFNGLALPHAFRAGMENTOP1ATIPROC)GLimp_ExtensionPointer( "glAlphaFragmentOp1ATI" );
		//glAlphaFragmentOp2ATI = (PFNGLALPHAFRAGMENTOP2ATIPROC)GLimp_ExtensionPointer( "glAlphaFragmentOp2ATI" );
		//glAlphaFragmentOp3ATI = (PFNGLALPHAFRAGMENTOP3ATIPROC)GLimp_ExtensionPointer( "glAlphaFragmentOp3ATI" );
		//glSetFragmentShaderConstantATI = (PFNGLSETFRAGMENTSHADERCONSTANTATIPROC)GLimp_ExtensionPointer( "glSetFragmentShaderConstantATI" );
	}

	// ARB_vertex_buffer_object
	glConfig.ARBVertexBufferObjectAvailable = R_CheckExtension( "GL_ARB_vertex_buffer_object" );
	glConfig.pixelBufferObjectAvailable =
		( GLEW_ARB_pixel_buffer_object || GLEW_EXT_pixel_buffer_object || GLEW_VERSION_2_1 );

	// ARB_vertex_program
	glConfig.ARBVertexProgramAvailable = R_CheckRequiredExtension( "GL_ARB_vertex_program" );

	// ARB_fragment_program
	if ( r_inhibitFragmentProgram.GetBool() ) {
		glConfig.ARBFragmentProgramAvailable = false;
	} else {
		glConfig.ARBFragmentProgramAvailable = R_CheckRequiredExtension( "GL_ARB_fragment_program" );

	}

	// GL_ARB shader objects / GLSL
	glConfig.GLSLProgramAvailable = R_CanUseGLSLPrograms();
	if ( glConfig.GLSLProgramAvailable ) {
		common->Printf( "...using GL_ARB shader objects + GLSL\n" );
	} else if ( r_inhibitFragmentProgram.GetBool() ) {
		common->Printf( "X..GLSL shader objects disabled by r_inhibitFragmentProgram\n" );
	} else if ( idAsyncNetwork::serverDedicated.GetBool() ) {
		common->Printf( "X..GLSL shader objects disabled on dedicated server\n" );
	} else {
		common->Printf( "X..GLSL shader objects not found\n" );
	}

	// check for minimum set
	if ( !glConfig.multitextureAvailable || !glConfig.textureEnvCombineAvailable || !glConfig.cubeMapAvailable
		|| !glConfig.envDot3Available ) {
			R_ErrorForMissingRequiredOpenGLFeatures();
	}

 	// GL_EXT_depth_bounds_test
 	glConfig.depthBoundsTestAvailable = R_CheckExtension( "EXT_depth_bounds_test" );

	glConfig.backendCaps.maxTextureSize = glConfig.maxTextureSize;
	glConfig.backendCaps.maxTextureUnits = glConfig.maxTextureUnits;
	glConfig.backendCaps.maxTextureCoords = glConfig.maxTextureCoords;
	glConfig.backendCaps.maxTextureImageUnits = glConfig.maxTextureImageUnits;
	glConfig.backendCaps.maxDrawBuffers = glConfig.maxDrawBuffers;
	glConfig.backendCaps.maxColorAttachments = glConfig.maxColorAttachments;
	glConfig.backendCaps.hasARBVertexProgram = glConfig.ARBVertexProgramAvailable;
	glConfig.backendCaps.hasARBFragmentProgram = glConfig.ARBFragmentProgramAvailable;
	glConfig.backendCaps.hasVBO = glConfig.ARBVertexBufferObjectAvailable;
	glConfig.backendCaps.hasPBO = glConfig.pixelBufferObjectAvailable;
	glConfig.backendCaps.hasGLSL = glConfig.GLSLProgramAvailable;
	glConfig.backendCaps.hasSRGBTextures = glConfig.textureSRGBAvailable;
	glConfig.backendCaps.hasFramebufferSRGB = glConfig.framebufferSRGBAvailable;
	glConfig.backendCaps.hasMRT = glConfig.maxDrawBuffers >= 4 && glConfig.maxColorAttachments >= 4;

}


/*
====================
R_GetModeInfo

r_mode is normally a small non-negative integer that
looks resolutions up in a table. If it is set to -2,
the native desktop resolution is used. If it is set to -1,
the values from r_customWidth, and r_customHeight
will be used instead.
====================
*/
typedef struct vidmode_s {
    const char *description;
    int         width, height;
} vidmode_t;

vidmode_t r_vidModes[] = {
	{ "Mode  0: 1280x720 (16:9)",		1280,	720 },
	{ "Mode  1: 1366x768 (16:9)",		1366,	768 },
	{ "Mode  2: 1600x900 (16:9)",		1600,	900 },
	{ "Mode  3: 1920x1080 (16:9)",		1920,	1080 },
	{ "Mode  4: 1920x1200 (16:10)",		1920,	1200 },
	{ "Mode  5: 2560x1080 (21:9)",		2560,	1080 },
	{ "Mode  6: 2560x1440 (16:9)",		2560,	1440 },
	{ "Mode  7: 3440x1440 (21:9)",		3440,	1440 },
	{ "Mode  8: 3840x2160 (16:9)",		3840,	2160 },
	{ "Mode  9: 5120x1440 (32:9)",		5120,	1440 },
	{ "Mode 10: 5120x2880 (16:9)",		5120,	2880 }
};
static int	s_numVidModes = ( sizeof( r_vidModes ) / sizeof( r_vidModes[0] ) );

#if MACOS_X
bool R_GetModeInfo( int *width, int *height, int mode ) {
#else
static bool R_GetModeInfo( int *width, int *height, int mode ) {
#endif
	vidmode_t	*vm;
	const int originalMode = mode;

	if ( mode < -2 ) {
		common->Printf( "^3R_GetModeInfo: r_mode %d is invalid, using custom mode (-1)\n", mode );
		mode = -1;
	}
	if ( mode >= s_numVidModes ) {
		common->Printf( "^3R_GetModeInfo: r_mode %d out of range, using mode 0 (%dx%d)\n",
			mode, r_vidModes[0].width, r_vidModes[0].height );
		mode = 0;
	}

	if ( mode != originalMode && r_mode.GetInteger() == originalMode ) {
		r_mode.SetInteger( mode );
		r_mode.ClearModified();
	}

	if ( mode == -2 ) {
		int desktopWidth = 0;
		int desktopHeight = 0;

		if ( !Sys_GetDesktopResolution( &desktopWidth, &desktopHeight ) ) {
			desktopWidth = idMath::ClampInt( 320, 16384, r_customWidth.GetInteger() );
			desktopHeight = idMath::ClampInt( 240, 16384, r_customHeight.GetInteger() );
			common->Printf( "^3R_GetModeInfo: unable to query desktop resolution, using %dx%d\n", desktopWidth, desktopHeight );
		}

		if ( width ) {
			*width = desktopWidth;
		}
		if ( height ) {
			*height = desktopHeight;
		}
		return true;
	}

	if ( mode == -1 ) {
		const int clampedWidth = idMath::ClampInt( 320, 16384, r_customWidth.GetInteger() );
		const int clampedHeight = idMath::ClampInt( 240, 16384, r_customHeight.GetInteger() );

		if ( r_customWidth.GetInteger() != clampedWidth ) {
			r_customWidth.SetInteger( clampedWidth );
			r_customWidth.ClearModified();
		}
		if ( r_customHeight.GetInteger() != clampedHeight ) {
			r_customHeight.SetInteger( clampedHeight );
			r_customHeight.ClearModified();
		}

		*width = clampedWidth;
		*height = clampedHeight;
		return true;
	}

	vm = &r_vidModes[mode];

	if ( width ) {
		*width  = vm->width;
	}
	if ( height ) {
		*height = vm->height;
	}

    return true;
}

static void R_GetWindowedModeInfo( int *width, int *height ) {
	const int clampedWidth = idMath::ClampInt( 320, 16384, r_windowWidth.GetInteger() );
	const int clampedHeight = idMath::ClampInt( 240, 16384, r_windowHeight.GetInteger() );

	if ( r_windowWidth.GetInteger() != clampedWidth ) {
		r_windowWidth.SetInteger( clampedWidth );
		r_windowWidth.ClearModified();
	}
	if ( r_windowHeight.GetInteger() != clampedHeight ) {
		r_windowHeight.SetInteger( clampedHeight );
		r_windowHeight.ClearModified();
	}

	if ( width ) {
		*width = clampedWidth;
	}
	if ( height ) {
		*height = clampedHeight;
	}
}

static void R_NormalizeDisplayCvars( void ) {
	const int originalMode = r_mode.GetInteger();
	int normalizedMode = originalMode;

	if ( normalizedMode < -2 ) {
		common->Printf( "^3R_GetModeInfo: r_mode %d is invalid, using custom mode (-1)\n", normalizedMode );
		normalizedMode = -1;
	} else if ( normalizedMode >= s_numVidModes ) {
		common->Printf( "^3R_GetModeInfo: r_mode %d out of range, using mode 0 (%dx%d)\n",
			normalizedMode, r_vidModes[0].width, r_vidModes[0].height );
		normalizedMode = 0;
	}

	if ( normalizedMode != originalMode ) {
		r_mode.SetInteger( normalizedMode );
		r_mode.ClearModified();
	}

	const int clampedWindowWidth = idMath::ClampInt( 320, 16384, r_windowWidth.GetInteger() );
	const int clampedWindowHeight = idMath::ClampInt( 240, 16384, r_windowHeight.GetInteger() );
	if ( r_windowWidth.GetInteger() != clampedWindowWidth ) {
		r_windowWidth.SetInteger( clampedWindowWidth );
		r_windowWidth.ClearModified();
	}
	if ( r_windowHeight.GetInteger() != clampedWindowHeight ) {
		r_windowHeight.SetInteger( clampedWindowHeight );
		r_windowHeight.ClearModified();
	}

	const int clampedCustomWidth = idMath::ClampInt( 320, 16384, r_customWidth.GetInteger() );
	const int clampedCustomHeight = idMath::ClampInt( 240, 16384, r_customHeight.GetInteger() );
	if ( r_customWidth.GetInteger() != clampedCustomWidth ) {
		r_customWidth.SetInteger( clampedCustomWidth );
		r_customWidth.ClearModified();
	}
	if ( r_customHeight.GetInteger() != clampedCustomHeight ) {
		r_customHeight.SetInteger( clampedCustomHeight );
		r_customHeight.ClearModified();
	}
}


/*
==================
R_InitOpenGL

This function is responsible for initializing a valid OpenGL subsystem
for rendering.  This is done by calling the system specific GLimp_Init,
which gives us a working OGL subsystem, then setting all necessary openGL
state, including images, vertex programs, and display lists.

Changes to the vertex cache size or smp state require a vid_restart.

If glConfig.isInitialized is false, no rendering can take place, but
all renderSystem functions will still operate properly, notably the material
and model information functions.
==================
*/
void R_InitOpenGL( void ) {
	GLint			temp;
	glimpParms_t	parms;
	int				i;

	common->Printf( "----- R_InitOpenGL -----\n" );

	if ( glConfig.isInitialized ) {
		common->FatalError( "R_InitOpenGL called while active" );
	}

	// in case we had an error while doing a tiled rendering
	tr.viewportOffset[0] = 0;
	tr.viewportOffset[1] = 0;

	R_NormalizeDisplayCvars();

	//
	// initialize OS specific portions of the renderSystem
	//
	for ( i = 0 ; i < 2 ; i++ ) {
		// set the parameters we are trying
		parms.fullScreen = r_fullscreen.GetBool();
		if ( parms.fullScreen ) {
			R_GetModeInfo( &parms.width, &parms.height, r_mode.GetInteger() );
		} else {
			R_GetWindowedModeInfo( &parms.width, &parms.height );
		}
		glConfig.vidWidth = parms.width;
		glConfig.vidHeight = parms.height;
		parms.borderless = !parms.fullScreen && r_borderless.GetBool();
		parms.displayHz = r_displayRefresh.GetInteger();
		parms.multiSamples = r_multiSamples.GetInteger();
		parms.stereo = false;

		if ( GLimp_Init( parms ) ) {
			// it worked
			break;
		}

		if ( i == 1 ) {
			common->FatalError( "Unable to initialize OpenGL" );
		}

		// if we failed, set everything back to "safe mode"
		// and try again
		r_mode.SetInteger( 0 );
		r_fullscreen.SetInteger( 1 );
		r_borderless.SetInteger( 0 );
		r_displayRefresh.SetInteger( 0 );
		r_multiSamples.SetInteger( 0 );
	}

	// input and sound systems need to be tied to the new window
	Sys_InitInput();
	//soundSystem->Init();

	// get our config strings
	glConfig.vendor_string = (const char *)glGetString(GL_VENDOR);
	glConfig.renderer_string = (const char *)glGetString(GL_RENDERER);
	glConfig.version_string = (const char *)glGetString(GL_VERSION);
	glConfig.extensions_string = (const char *)glGetString(GL_EXTENSIONS);
	if ( glConfig.vendor_string == NULL ) {
		glConfig.vendor_string = "unknown";
	}
	if ( glConfig.renderer_string == NULL ) {
		glConfig.renderer_string = "unknown";
	}
	if ( glConfig.version_string == NULL ) {
		glConfig.version_string = "0.0";
	}
	if ( glConfig.extensions_string == NULL ) {
		glConfig.extensions_string = "";
	}

	// Query the actual framebuffer bit depths from the active context.
	// Some platform backends don't populate these fields directly.
	GLint redBits = 0;
	GLint greenBits = 0;
	GLint blueBits = 0;
	GLint alphaBits = 0;
	GLint depthBits = 0;
	GLint stencilBits = 0;
	glGetIntegerv( GL_RED_BITS, &redBits );
	glGetIntegerv( GL_GREEN_BITS, &greenBits );
	glGetIntegerv( GL_BLUE_BITS, &blueBits );
	glGetIntegerv( GL_ALPHA_BITS, &alphaBits );
	glGetIntegerv( GL_DEPTH_BITS, &depthBits );
	glGetIntegerv( GL_STENCIL_BITS, &stencilBits );

	if ( redBits < 0 ) {
		redBits = 0;
	}
	if ( greenBits < 0 ) {
		greenBits = 0;
	}
	if ( blueBits < 0 ) {
		blueBits = 0;
	}
	if ( alphaBits < 0 ) {
		alphaBits = 0;
	}
	if ( depthBits < 0 ) {
		depthBits = 0;
	}
	if ( stencilBits < 0 ) {
		stencilBits = 0;
	}

	glConfig.alphaBits = ( alphaBits > 0 ) ? alphaBits : 8;

	const int queriedColorBits = redBits + greenBits + blueBits + alphaBits;
	glConfig.colorBits = ( queriedColorBits > 0 ) ? queriedColorBits : 32;
	glConfig.depthBits = ( depthBits > 0 ) ? depthBits : 24;
	glConfig.stencilBits = ( stencilBits > 0 ) ? stencilBits : 8;

	// OpenGL driver constants
	glGetIntegerv( GL_MAX_TEXTURE_SIZE, &temp );
	glConfig.maxTextureSize = temp;

	// stubbed or broken drivers may have reported 0...
	if ( glConfig.maxTextureSize <= 0 ) {
		glConfig.maxTextureSize = 256;
	}

	glConfig.isInitialized = true;

	// recheck all the extensions (FIXME: this might be dangerous)
	R_CheckPortableExtensions();

	// parse our vertex and fragment programs, possibly disably support for
	// one of the paths if there was an error
	R_ARB2_Init();
	R_ModernGLExecutor_Init( glConfig.backendCaps, glConfig.renderFeatures );
	RendererBootstrap_SetModernExecutorAvailable( R_ModernGLExecutor_Stats().available );
	RendererBootstrap_FinalizeLegacyBridge( glConfig.allowARB2Path );
	glConfig.rendererTier = RendererBootstrap_GetState().selectedTier;
	glConfig.renderFeatures = RendererBootstrap_GetState().features;
	if ( !glConfig.allowARB2Path ) {
		R_ErrorForMissingRequiredOpenGLFeatures();
	}

	cmdSystem->AddCommand( "reloadARBprograms", R_ReloadARBPrograms_f, CMD_FL_RENDERER, "reloads ARB programs" );
	R_ReloadARBPrograms_f( idCmdArgs() );

	R_RendererUpload_Init( glConfig.backendCaps );

	// allocate the vertex array range or vertex objects
	vertexCache.Init();

	// select which renderSystem we are going to use
	r_renderer.SetModified();
	tr.SetBackEndRenderer();

	// allocate the frame data, which may be more if smp is enabled
	R_InitFrameData();

	// Reset our gamma
	R_SetColorMappings();

#ifdef _WIN32
	static bool glCheck = false;
	if ( !glCheck ) {
		glCheck = true;
		if ( !idStr::Icmp( glConfig.vendor_string, "Microsoft" ) && idStr::FindText( glConfig.renderer_string, "OpenGL-D3D" ) != -1 ) {
			if ( cvarSystem->GetCVarBool( "r_fullscreen" ) ) {
				cmdSystem->BufferCommandText( CMD_EXEC_NOW, "vid_restart partial windowed\n" );
				Sys_GrabMouseCursor( false );
			}
			int ret = MessageBox( NULL, "Please install OpenGL drivers from your graphics hardware vendor to run " GAME_NAME ".\nYour OpenGL functionality is limited.",
				"Insufficient OpenGL capabilities", MB_OKCANCEL | MB_ICONWARNING | MB_TASKMODAL );
			if ( ret == IDCANCEL ) {
				cmdSystem->BufferCommandText( CMD_EXEC_APPEND, "quit\n" );
				cmdSystem->ExecuteCommandBuffer();
			}
			if ( cvarSystem->GetCVarBool( "r_fullscreen" ) ) {
				cmdSystem->BufferCommandText( CMD_EXEC_APPEND, "vid_restart\n" );
			}
		}
	}
#endif
}

/*
==================
GL_CheckErrors
==================
*/
void GL_CheckErrors( void ) {
    int		err;
    char	s[64];
	int		i;

	// check for up to 10 errors pending
	for ( i = 0 ; i < 10 ; i++ ) {
		err = glGetError();
		if ( err == GL_NO_ERROR ) {
			return;
		}
		switch( err ) {
			case GL_INVALID_ENUM:
				strcpy( s, "GL_INVALID_ENUM" );
				break;
			case GL_INVALID_VALUE:
				strcpy( s, "GL_INVALID_VALUE" );
				break;
			case GL_INVALID_OPERATION:
				strcpy( s, "GL_INVALID_OPERATION" );
				break;
			case GL_STACK_OVERFLOW:
				strcpy( s, "GL_STACK_OVERFLOW" );
				break;
			case GL_STACK_UNDERFLOW:
				strcpy( s, "GL_STACK_UNDERFLOW" );
				break;
			case GL_OUT_OF_MEMORY:
				strcpy( s, "GL_OUT_OF_MEMORY" );
				break;
			default:
				idStr::snPrintf( s, sizeof(s), "%i", err);
				break;
		}

		if ( !r_ignoreGLErrors.GetBool() ) {
			common->Printf( "GL_CheckErrors: %s\n", s );
		}
	}
}

/*
=====================
R_ReloadSurface_f

Reload the material displayed by r_showSurfaceInfo
=====================
*/
static void R_ReloadSurface_f( const idCmdArgs &args ) {
	modelTrace_t mt;
	idVec3 start, end;
	
	// start far enough away that we don't hit the player model
	start = tr.primaryView->renderView.vieworg + tr.primaryView->renderView.viewaxis[0] * 16;
	end = start + tr.primaryView->renderView.viewaxis[0] * 1000.0f;
	if ( !tr.primaryWorld->Trace( mt, start, end, 0.0f, false ) ) {
		return;
	}

	common->Printf( "Reloading %s\n", mt.material->GetName() );

	// reload the decl
	mt.material->base->Reload();

	// reload any images used by the decl
	mt.material->ReloadImages( false );
}



/*
==============
R_ListModes_f
==============
*/
static void R_ListModes_f( const idCmdArgs &args ) {
	int i;

	(void)args;

	common->Printf( "\n" );
	for ( i = 0; i < s_numVidModes; i++ ) {
		common->Printf( "%s\n", r_vidModes[i].description );
	}
	common->Printf( "Mode -2: native desktop resolution\n" );
	common->Printf( "Mode -1: custom fullscreen using r_customWidth / r_customHeight\n" );
	common->Printf( "Windowed sizing uses r_windowWidth / r_windowHeight when r_fullscreen is 0\n" );
	common->Printf( "r_mode/r_custom* only affect fullscreen when r_fullscreenDesktop is 0 (exclusive mode)\n" );
	common->Printf( "On SDL3 backends, use listDisplayModes to inspect native monitor modes.\n" );
	common->Printf( "\n" );
}



/*
=============
R_TestImage_f

Display the given image centered on the screen.
testimage <number>
testimage <filename>
=============
*/
void R_TestImage_f( const idCmdArgs &args ) {
	int imageNum;

	if ( tr.testVideo ) {
		delete tr.testVideo;
		tr.testVideo = NULL;
	}
	tr.testImage = NULL;

	if ( args.Argc() != 2 ) {
		return;
	}

	if ( idStr::IsNumeric( args.Argv(1) ) ) {
		imageNum = atoi( args.Argv(1) );
		if ( imageNum >= 0 && imageNum < globalImages->images.Num() ) {
			tr.testImage = globalImages->images[imageNum];
		}
	} else {
		tr.testImage = globalImages->ImageFromFile( args.Argv( 1 ), TF_DEFAULT, TR_REPEAT, TD_DEFAULT );
	}
}

/*
=============
R_TestVideo_f

Plays the cinematic file in a testImage
=============
*/
void R_TestVideo_f( const idCmdArgs &args ) {
	if ( tr.testVideo ) {
		delete tr.testVideo;
		tr.testVideo = NULL;
	}
	tr.testImage = NULL;

	if ( args.Argc() < 2 ) {
		return;
	}

	tr.testImage = globalImages->ImageFromFile( "_scratch", TF_DEFAULT, TR_REPEAT, TD_DEFAULT );
	tr.testVideo = idCinematic::Alloc();
	tr.testVideo->InitFromFile( args.Argv( 1 ), true );

	cinData_t	cin;
	cin = tr.testVideo->ImageForTime( 0 );
	if ( !cin.image ) {
		delete tr.testVideo;
		tr.testVideo = NULL;
		tr.testImage = NULL;
		return;
	}

	common->Printf( "%i x %i images\n", cin.imageWidth, cin.imageHeight );

	int	len = tr.testVideo->AnimationLength();
	common->Printf( "%5.1f seconds of video\n", len * 0.001 );

	tr.testVideoStartTime = tr.primaryRenderView.time * 0.001;

	// try to play the matching wav file
	idStr	wavString = args.Argv( ( args.Argc() == 2 ) ? 1 : 2 );
	wavString.StripFileExtension();
	wavString = wavString + ".wav";
	session->sw->PlayShaderDirectly( wavString.c_str() );
}

static int R_QsortSurfaceAreas( const void *a, const void *b ) {
	const idMaterial	*ea, *eb;
	int	ac, bc;

	ea = *(idMaterial **)a;
	if ( !ea->EverReferenced() ) {
		ac = 0;
	} else {
		ac = ea->GetSurfaceArea();
	}
	eb = *(idMaterial **)b;
	if ( !eb->EverReferenced() ) {
		bc = 0;
	} else {
		bc = eb->GetSurfaceArea();
	}

	if ( ac < bc ) {
		return -1;
	}
	if ( ac > bc ) {
		return 1;
	}

	return idStr::Icmp( ea->GetName(), eb->GetName() );
}


/*
===================
R_ReportSurfaceAreas_f

Prints a list of the materials sorted by surface area
===================
*/
void R_ReportSurfaceAreas_f( const idCmdArgs &args ) {
	int		i, count;
	idMaterial	**list;

	count = declManager->GetNumDecls( DECL_MATERIAL );
	list = (idMaterial **)_alloca( count * sizeof( *list ) );

	for ( i = 0 ; i < count ; i++ ) {
		list[i] = (idMaterial *)declManager->DeclByIndex( DECL_MATERIAL, i, false );
	}

	qsort( list, count, sizeof( list[0] ), R_QsortSurfaceAreas );

	// skip over ones with 0 area
	for ( i = 0 ; i < count ; i++ ) {
		if ( list[i]->GetSurfaceArea() > 0 ) {
			break;
		}
	}

	for ( ; i < count ; i++ ) {
		// report size in "editor blocks"
		int	blocks = list[i]->GetSurfaceArea() / 4096.0;
		common->Printf( "%7i %s\n", blocks, list[i]->GetName() );
	}
}

/*
===================
R_ReportImageDuplication_f

Checks for images with the same hash value and does a better comparison
===================
*/
void R_ReportImageDuplication_f( const idCmdArgs &args ) {
	
}

/* 
============================================================================== 
 
						THROUGHPUT BENCHMARKING
 
============================================================================== 
*/ 

/*
================
R_RenderingFPS
================
*/
static float R_RenderingFPS( const renderView_t *renderView ) {
	glFinish();

	int		start = Sys_Milliseconds();
	static const int SAMPLE_MSEC = 1000;
	int		end;
	int		count = 0;

	while( 1 ) {
		// render
		renderSystem->BeginFrame( glConfig.vidWidth, glConfig.vidHeight );
		tr.primaryWorld->RenderScene( renderView );
		renderSystem->EndFrame( NULL, NULL );
		glFinish();
		count++;
		end = Sys_Milliseconds();
		if ( end - start > SAMPLE_MSEC ) {
			break;
		}
	}

	float fps = count * 1000.0 / ( end - start );

	return fps;
}

/*
================
R_Benchmark_f
================
*/
void R_Benchmark_f( const idCmdArgs &args ) {
	float	fps, msec;
	renderView_t	view;

	if ( !tr.primaryView ) {
		common->Printf( "No primaryView for benchmarking\n" );
		return;
	}
	view = tr.primaryRenderView;

	for ( int size = 100 ; size >= 10 ; size -= 10 ) {
		r_screenFraction.SetInteger( size );
		fps = R_RenderingFPS( &view );
		int	kpix = glConfig.vidWidth * glConfig.vidHeight * ( size * 0.01 ) * ( size * 0.01 ) * 0.001;
		msec = 1000.0 / fps;
		common->Printf( "kpix: %4i  msec:%5.1f fps:%5.1f\n", kpix, msec, fps );
	}

	// enable r_singleTriangle 1 while r_screenFraction is still at 10
	r_singleTriangle.SetBool( 1 );
	fps = R_RenderingFPS( &view );
	msec = 1000.0 / fps;
	common->Printf( "single tri  msec:%5.1f fps:%5.1f\n", msec, fps );
	r_singleTriangle.SetBool( 0 );
	r_screenFraction.SetInteger( 100 );

	// enable r_skipRenderContext 1
	r_skipRenderContext.SetBool( true );
	fps = R_RenderingFPS( &view );
	msec = 1000.0 / fps;
	common->Printf( "no context  msec:%5.1f fps:%5.1f\n", msec, fps );
	r_skipRenderContext.SetBool( false );
}


/* 
============================================================================== 
 
						SCREEN SHOTS 
 
============================================================================== 
*/ 

/*
====================
R_ReadTiledPixels

Allows the rendering of an image larger than the actual window by
tiling it into window-sized chunks and rendering each chunk separately

If ref isn't specified, the full session UpdateScreen will be done.
====================
*/
void R_ReadTiledPixels( int width, int height, byte *buffer, renderView_t *ref = NULL ) {
	// include extra space for OpenGL padding to word boundaries
	byte	*temp = (byte *)R_StaticAlloc( (glConfig.vidWidth+3) * glConfig.vidHeight * 3 );

	int	oldWidth = glConfig.vidWidth;
	int oldHeight = glConfig.vidHeight;

	tr.tiledViewport[0] = width;
	tr.tiledViewport[1] = height;

	// disable scissor, so we don't need to adjust all those rects
	r_useScissor.SetBool( false );

	for ( int xo = 0 ; xo < width ; xo += oldWidth ) {
		for ( int yo = 0 ; yo < height ; yo += oldHeight ) {
			tr.viewportOffset[0] = -xo;
			tr.viewportOffset[1] = -yo;

			if ( ref ) {
				tr.BeginFrame( oldWidth, oldHeight );
				tr.primaryWorld->RenderScene( ref );

				// Match CaptureRenderToFile's direct back-buffer readback path instead of
				// swapping and reading GL_FRONT. Front-buffer tiled captures are fragile in
				// windowed/composited environments and can produce partially black strips.
				tr.guiModel->EmitFullScreen();
				tr.guiModel->Clear();

				if ( frameData->cmdHead->commandId != RC_NOP || frameData->cmdHead->next != NULL ) {
					if ( !r_skipBackEnd.GetBool() ) {
						RB_ExecuteBackEndCommands( frameData->cmdHead );
					}
					R_ClearCommandChain();
				}

				glReadBuffer( GL_BACK );
			} else {
				session->UpdateScreen();
				glReadBuffer( GL_FRONT );
			}

			int w = oldWidth;
			if ( xo + w > width ) {
				w = width - xo;
			}
			int h = oldHeight;
			if ( yo + h > height ) {
				h = height - yo;
			}

			glReadPixels( 0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, temp ); 

			int	row = ( w * 3 + 3 ) & ~3;		// OpenGL pads to dword boundaries

			for ( int y = 0 ; y < h ; y++ ) {
				memcpy( buffer + ( ( yo + y )* width + xo ) * 3,
					temp + y * row, w * 3 );
			}
		}
	}

	r_useScissor.SetBool( true );

	tr.viewportOffset[0] = 0;
	tr.viewportOffset[1] = 0;
	tr.tiledViewport[0] = 0;
	tr.tiledViewport[1] = 0;

	R_StaticFree( temp );

	glConfig.vidWidth = oldWidth;
	glConfig.vidHeight = oldHeight;
}


/*
================== 
TakeScreenshot

Move to tr_imagefiles.c...

Will automatically tile render large screen shots if necessary
Downsample is the number of steps to mipmap the image before saving it
If ref == NULL, session->updateScreen will be used
================== 
*/  
void idRenderSystemLocal::TakeScreenshot( int width, int height, const char *fileName, int blends, renderView_t *ref ) {
	byte		*buffer;
	int			i, j, c, temp;

	takingScreenshot = true;

	int	pix = width * height;

	buffer = (byte *)R_StaticAlloc(pix*3 + 18);
	memset (buffer, 0, 18);

	if ( blends <= 1 ) {
		R_ReadTiledPixels( width, height, buffer + 18, ref );
	} else {
		unsigned short *shortBuffer = (unsigned short *)R_StaticAlloc(pix*2*3);
		memset (shortBuffer, 0, pix*2*3);

		// enable anti-aliasing jitter
		r_jitter.SetBool( true );

		for ( i = 0 ; i < blends ; i++ ) {
			R_ReadTiledPixels( width, height, buffer + 18, ref );

			for ( j = 0 ; j < pix*3 ; j++ ) {
				shortBuffer[j] += buffer[18+j];
			}
		}

		// divide back to bytes
		for ( i = 0 ; i < pix*3 ; i++ ) {
			buffer[18+i] = shortBuffer[i] / blends;
		}

		R_StaticFree( shortBuffer );
		r_jitter.SetBool( false );
	}

	// fill in the header (this is vertically flipped, which glReadPixels emits)
	buffer[2] = 2;		// uncompressed type
	buffer[12] = width & 255;
	buffer[13] = width >> 8;
	buffer[14] = height & 255;
	buffer[15] = height >> 8;
	buffer[16] = 24;	// pixel size

	// swap rgb to bgr
	c = 18 + width * height * 3;
	for (i=18 ; i<c ; i+=3) {
		temp = buffer[i];
		buffer[i] = buffer[i+2];
		buffer[i+2] = temp;
	}

	// _D3XP adds viewnote screenie save to cdpath
	if ( strstr( fileName, "viewnote" ) ) {
		fileSystem->WriteFile( fileName, buffer, c, "fs_cdpath" );
	} else {
		fileSystem->WriteFile( fileName, buffer, c );
	}

	R_StaticFree( buffer );

	takingScreenshot = false;

}


/* 
================== 
R_ScreenshotFilename

Returns a filename with digits appended
if we have saved a previous screenshot, don't scan
from the beginning, because recording demo avis can involve
thousands of shots
================== 
*/  
void R_ScreenshotFilename( int &lastNumber, const char *base, idStr &fileName ) {
	int	a,b,c,d, e;

	bool restrict = cvarSystem->GetCVarBool( "fs_restrict" );
	cvarSystem->SetCVarBool( "fs_restrict", false );

	lastNumber++;
	if ( lastNumber > 99999 ) {
		lastNumber = 99999;
	}
	for ( ; lastNumber < 99999 ; lastNumber++ ) {
		int	frac = lastNumber;

		a = frac / 10000;
		frac -= a*10000;
		b = frac / 1000;
		frac -= b*1000;
		c = frac / 100;
		frac -= c*100;
		d = frac / 10;
		frac -= d*10;
		e = frac;

		sprintf( fileName, "%s%i%i%i%i%i.tga", base, a, b, c, d, e );
		if ( lastNumber == 99999 ) {
			break;
		}
		int len = fileSystem->ReadFile( fileName, NULL, NULL );
		if ( len <= 0 ) {
			break;
		}
		// check again...
	}
	cvarSystem->SetCVarBool( "fs_restrict", restrict );
}

static ID_INLINE void R_WriteLittleUInt32( byte *buffer, int offset, uint32 value ) {
	buffer[ offset + 0 ] = value & 0xFF;
	buffer[ offset + 1 ] = ( value >> 8 ) & 0xFF;
	buffer[ offset + 2 ] = ( value >> 16 ) & 0xFF;
	buffer[ offset + 3 ] = ( value >> 24 ) & 0xFF;
}

static void R_WriteDDS( const char *fileName, const byte *rgba, int width, int height ) {
	if ( fileName == NULL || rgba == NULL || width <= 0 || height <= 0 ) {
		return;
	}

	bool hasAlpha = false;
	for ( int i = 0; i < width * height; i++ ) {
		if ( rgba[ i * 4 + 3 ] != 255 ) {
			hasAlpha = true;
			break;
		}
	}

	const bool useDXT5 = hasAlpha;
	const int paddedWidth = ( width + 3 ) & ~3;
	const int paddedHeight = ( height + 3 ) & ~3;
	const int blockSize = useDXT5 ? 16 : 8;
	const int blockCount = Max( 1, ( width + 3 ) >> 2 ) * Max( 1, ( height + 3 ) >> 2 );
	const int compressedSize = blockCount * blockSize;

	idTempArray<byte> paddedPixels( paddedWidth * paddedHeight * 4 );
	for ( int y = 0; y < paddedHeight; y++ ) {
		const int srcY = idMath::ClampInt( 0, height - 1, y );
		for ( int x = 0; x < paddedWidth; x++ ) {
			const int srcX = idMath::ClampInt( 0, width - 1, x );
			const byte *srcPixel = rgba + ( srcY * width + srcX ) * 4;
			byte *dstPixel = paddedPixels.Ptr() + ( y * paddedWidth + x ) * 4;
			dstPixel[ 0 ] = srcPixel[ 0 ];
			dstPixel[ 1 ] = srcPixel[ 1 ];
			dstPixel[ 2 ] = srcPixel[ 2 ];
			dstPixel[ 3 ] = srcPixel[ 3 ];
		}
	}

	idTempArray<byte> compressedPixels( compressedSize );
	idDxtEncoder encoder;
	if ( useDXT5 ) {
		encoder.CompressImageDXT5Fast( paddedPixels.Ptr(), compressedPixels.Ptr(), paddedWidth, paddedHeight );
	} else {
		encoder.CompressImageDXT1Fast( paddedPixels.Ptr(), compressedPixels.Ptr(), paddedWidth, paddedHeight );
	}

	idTempArray<byte> fileBuffer( 128 + compressedSize );
	memset( fileBuffer.Ptr(), 0, 128 + compressedSize );

	fileBuffer[ 0 ] = 'D';
	fileBuffer[ 1 ] = 'D';
	fileBuffer[ 2 ] = 'S';
	fileBuffer[ 3 ] = ' ';
	R_WriteLittleUInt32( fileBuffer.Ptr(), 4, 124 );
	R_WriteLittleUInt32( fileBuffer.Ptr(), 8, 0x00000001 | 0x00000002 | 0x00000004 | 0x00001000 | 0x00080000 );
	R_WriteLittleUInt32( fileBuffer.Ptr(), 12, height );
	R_WriteLittleUInt32( fileBuffer.Ptr(), 16, width );
	R_WriteLittleUInt32( fileBuffer.Ptr(), 20, compressedSize );
	R_WriteLittleUInt32( fileBuffer.Ptr(), 76, 32 );
	R_WriteLittleUInt32( fileBuffer.Ptr(), 80, 0x00000004 );
	R_WriteLittleUInt32( fileBuffer.Ptr(), 84,
		( (uint32)'D' ) | ( (uint32)'X' << 8 ) | ( (uint32)'T' << 16 ) | ( (uint32)( useDXT5 ? '5' : '1' ) << 24 ) );
	R_WriteLittleUInt32( fileBuffer.Ptr(), 108, 0x00001000 );

	memcpy( fileBuffer.Ptr() + 128, compressedPixels.Ptr(), compressedSize );
	fileSystem->WriteFile( fileName, fileBuffer.Ptr(), 128 + compressedSize );
}

static void R_CaptureTiledPixelsRGBA( int width, int height, int blends, renderView_t *ref, byte *rgbaOut ) {
	const int pixelCount = width * height;
	byte *rgbBuffer = (byte *)R_StaticAlloc( pixelCount * 3 );

	if ( blends <= 1 ) {
		R_ReadTiledPixels( width, height, rgbBuffer, ref );
	} else {
		unsigned short *accumBuffer = (unsigned short *)R_StaticAlloc( pixelCount * 3 * sizeof( unsigned short ) );
		memset( accumBuffer, 0, pixelCount * 3 * sizeof( unsigned short ) );

		r_jitter.SetBool( true );
		for ( int i = 0; i < blends; i++ ) {
			R_ReadTiledPixels( width, height, rgbBuffer, ref );
			for ( int j = 0; j < pixelCount * 3; j++ ) {
				accumBuffer[ j ] += rgbBuffer[ j ];
			}
		}
		r_jitter.SetBool( false );

		for ( int i = 0; i < pixelCount * 3; i++ ) {
			rgbBuffer[ i ] = accumBuffer[ i ] / blends;
		}

		R_StaticFree( accumBuffer );
	}

	for ( int y = 0; y < height; y++ ) {
		const byte *srcRow = rgbBuffer + ( ( height - 1 - y ) * width ) * 3;
		byte *dstRow = rgbaOut + y * width * 4;
		for ( int x = 0; x < width; x++ ) {
			dstRow[ x * 4 + 0 ] = srcRow[ x * 3 + 0 ];
			dstRow[ x * 4 + 1 ] = srcRow[ x * 3 + 1 ];
			dstRow[ x * 4 + 2 ] = srcRow[ x * 3 + 2 ];
			dstRow[ x * 4 + 3 ] = 255;
		}
	}

	R_StaticFree( rgbBuffer );
}

static bool R_ResampleLevelShotTileRGBA( const byte *src, int srcWidth, int srcHeight, int tileSize, byte *dst ) {
	byte *resampled = R_ResampleTexture( src, srcWidth, srcHeight, tileSize, tileSize );
	if ( resampled == NULL ) {
		return false;
	}

	memcpy( dst, resampled, tileSize * tileSize * 4 );
	R_StaticFree( resampled );
	return true;
}

static void R_CaptureLevelShotTileRGBA( int width, int height, int blends, const renderView_t &sourceView,
	float projectionShiftX, float projectionShiftY, byte *rgbaOut ) {
	const bool previousDisableLevelshotEntityCulling = tr.disableLevelshotEntityCulling;
	tr_levelshotProjectionShiftActive = true;
	tr_levelshotProjectionShiftX = projectionShiftX;
	tr_levelshotProjectionShiftY = projectionShiftY;
	tr.disableLevelshotEntityCulling = true;
	renderView_t captureView = sourceView;
	R_CaptureTiledPixelsRGBA( width, height, blends, &captureView, rgbaOut );
	tr.disableLevelshotEntityCulling = previousDisableLevelshotEntityCulling;
	tr_levelshotProjectionShiftActive = false;
	tr_levelshotProjectionShiftX = 0.0f;
	tr_levelshotProjectionShiftY = 0.0f;
}

static void R_LevelShotNormalizeFovToAspect( const renderView_t &sourceView, float currentAspect, float targetAspect, float &fovX, float &fovY ) {
	const float aspectEpsilon = 0.0001f;

	fovX = sourceView.fov_x;
	fovY = sourceView.fov_y;

	if ( currentAspect > targetAspect + aspectEpsilon ) {
		fovX = RAD2DEG( 2.0f * idMath::ATan( idMath::Tan( DEG2RAD( sourceView.fov_y * 0.5f ) ) * targetAspect ) );
	} else if ( currentAspect + aspectEpsilon < targetAspect ) {
		fovY = RAD2DEG( 2.0f * idMath::ATan( idMath::Tan( DEG2RAD( sourceView.fov_x * 0.5f ) ) / targetAspect ) );
	}
}

static void R_NormalizeLevelShotBaseName( idStr &baseName ) {
	if ( baseName.Length() <= 0 ) {
		idStr mapName = cvarSystem->GetCVarString( "si_map" );
		mapName.StripPath();
		mapName.StripFileExtension();
		if ( mapName.Length() > 0 ) {
			baseName = va( "gfx/guis/loadscreens/%s", mapName.c_str() );
			return;
		}

		static int lastLevelshotNumber = 0;
		R_ScreenshotFilename( lastLevelshotNumber, "screenshots/levelshot", baseName );
		baseName.StripFileExtension();
		return;
	}

	if ( baseName.Find( "/", false ) < 0 && baseName.Find( ":", false ) < 0 ) {
		baseName = va( "gfx/guis/loadscreens/%s", baseName.c_str() );
	}
}

static void R_WriteLevelShotTile( const idStr &baseName, const char *suffix, const byte *rgba, int tileSize ) {
	idStr tgaName = baseName;
	tgaName += suffix;
	tgaName += ".tga";
	R_WriteTGA( tgaName.c_str(), rgba, tileSize, tileSize );

	idStr ddsName = baseName;
	ddsName += suffix;
	ddsName += ".dds";
	R_WriteDDS( ddsName.c_str(), rgba, tileSize, tileSize );
}

static void R_WriteLevelShotPose( const idStr &baseName, const renderView_t &view ) {
	idStr txtName = baseName;
	float pose[6];
	idAngles angles;

	txtName.SetFileExtension( ".txt" );
	idFile *poseFile = fileSystem->OpenFileWrite( txtName );
	if ( poseFile == NULL ) {
		return;
	}

	angles = view.viewaxis.ToAngles();
	pose[0] = view.vieworg.x;
	pose[1] = view.vieworg.y;
	pose[2] = view.vieworg.z;
	pose[3] = angles.pitch;
	pose[4] = angles.yaw;
	pose[5] = angles.roll;
	poseFile->WriteFloatString( "%.6f %.6f %.6f %.6f %.6f %.6f\n", pose[0], pose[1], pose[2], pose[3], pose[4], pose[5] );
	fileSystem->CloseFile( poseFile );
}

void R_LevelShot_f( const idCmdArgs &args ) {
	idStr baseName;
	int size = 512;

	if ( args.Argc() > 2 ) {
		common->Printf( "usage: levelshot\n       levelshot <size>\n" );
		return;
	}
	if ( args.Argc() == 2 ) {
		size = atoi( args.Argv( 1 ) );
	}

	R_NormalizeLevelShotBaseName( baseName );
	if ( size < 1 ) {
		size = 1;
	}
	const int blends = 1;

	if ( !tr.primaryView || !tr.primaryWorld ) {
		common->Printf( "No primary view.\n" );
		return;
	}

	const int rawTileHeight = size;
	const float tileAspect = static_cast<float>( SCREEN_WIDTH ) / static_cast<float>( SCREEN_HEIGHT );
	const int rawTileWidth = Max( 1, idMath::Ftoi( rawTileHeight * tileAspect + 0.5f ) );

	renderView_t baseRef = tr.primaryView->renderView;
	float currentAspect = tileAspect;
	if ( glConfig.vidWidth > 0 && glConfig.vidHeight > 0 ) {
		currentAspect = static_cast<float>( glConfig.vidWidth ) / static_cast<float>( glConfig.vidHeight );
	}
	R_LevelShotNormalizeFovToAspect( tr.primaryView->renderView, currentAspect, tileAspect, baseRef.fov_x, baseRef.fov_y );
	baseRef.x = 0;
	baseRef.y = 0;
	baseRef.width = SCREEN_WIDTH;
	baseRef.height = SCREEN_HEIGHT;

	idTempArray<byte> centerRawTile( rawTileWidth * rawTileHeight * 4 );
	idTempArray<byte> leftRawTile( rawTileWidth * rawTileHeight * 4 );
	idTempArray<byte> rightRawTile( rawTileWidth * rawTileHeight * 4 );
	idTempArray<byte> topRawTile( rawTileWidth * rawTileHeight * 4 );
	idTempArray<byte> bottomRawTile( rawTileWidth * rawTileHeight * 4 );
	idTempArray<byte> centerTile( size * size * 4 );
	idTempArray<byte> leftTile( size * size * 4 );
	idTempArray<byte> rightTile( size * size * 4 );
	idTempArray<byte> topTile( size * size * 4 );
	idTempArray<byte> bottomTile( size * size * 4 );

	console->Close();

	const bool previousSuppressLevelshotViewModels = tr.suppressLevelshotViewModels;
	tr.suppressLevelshotViewModels = true;

	// Capture each tile directly as an off-axis 4:3 view. This avoids depending on
	// oversized stitched strips, which are fragile on some drivers/window systems.
	R_CaptureLevelShotTileRGBA( rawTileWidth, rawTileHeight, blends, baseRef, 0.0f, 0.0f, centerRawTile.Ptr() );
	R_CaptureLevelShotTileRGBA( rawTileWidth, rawTileHeight, blends, baseRef, -2.0f, 0.0f, leftRawTile.Ptr() );
	R_CaptureLevelShotTileRGBA( rawTileWidth, rawTileHeight, blends, baseRef, 2.0f, 0.0f, rightRawTile.Ptr() );
	R_CaptureLevelShotTileRGBA( rawTileWidth, rawTileHeight, blends, baseRef, 0.0f, 2.0f, topRawTile.Ptr() );
	R_CaptureLevelShotTileRGBA( rawTileWidth, rawTileHeight, blends, baseRef, 0.0f, -2.0f, bottomRawTile.Ptr() );

	tr.suppressLevelshotViewModels = previousSuppressLevelshotViewModels;

	if ( !R_ResampleLevelShotTileRGBA( leftRawTile.Ptr(), rawTileWidth, rawTileHeight, size, leftTile.Ptr() ) ||
		!R_ResampleLevelShotTileRGBA( centerRawTile.Ptr(), rawTileWidth, rawTileHeight, size, centerTile.Ptr() ) ||
		!R_ResampleLevelShotTileRGBA( rightRawTile.Ptr(), rawTileWidth, rawTileHeight, size, rightTile.Ptr() ) ||
		!R_ResampleLevelShotTileRGBA( topRawTile.Ptr(), rawTileWidth, rawTileHeight, size, topTile.Ptr() ) ||
		!R_ResampleLevelShotTileRGBA( bottomRawTile.Ptr(), rawTileWidth, rawTileHeight, size, bottomTile.Ptr() ) ) {
		tr.suppressLevelshotViewModels = previousSuppressLevelshotViewModels;
		common->Warning( "levelshot: failed to resample one or more tiles" );
		return;
	}

	R_WriteLevelShotTile( baseName, "", centerTile.Ptr(), size );
	R_WriteLevelShotTile( baseName, "_left", leftTile.Ptr(), size );
	R_WriteLevelShotTile( baseName, "_right", rightTile.Ptr(), size );
	R_WriteLevelShotTile( baseName, "_top", topTile.Ptr(), size );
	R_WriteLevelShotTile( baseName, "_bottom", bottomTile.Ptr(), size );
	R_WriteLevelShotPose( baseName, baseRef );

	common->Printf( "Wrote %s(.tga/.dds) and _left/_right/_top/_bottom tiles from %dx%d 4:3 source captures\n",
		baseName.c_str(), rawTileWidth, rawTileHeight );
}

/*
================== 
R_BlendedScreenShot

screenshot
screenshot [filename]
screenshot [width] [height]
screenshot [width] [height] [samples]
================== 
*/ 
#define	MAX_BLENDS	256	// to keep the accumulation in shorts
void R_ScreenShot_f( const idCmdArgs &args ) {
	static int lastNumber = 0;
	idStr checkname;

	int width = glConfig.vidWidth;
	int height = glConfig.vidHeight;
	int	x = 0;
	int y = 0;
	int	blends = 0;

	switch ( args.Argc() ) {
	case 1:
		width = glConfig.vidWidth;
		height = glConfig.vidHeight;
		blends = 1;
		R_ScreenshotFilename( lastNumber, "screenshots/shot", checkname );
		break;
	case 2:
		width = glConfig.vidWidth;
		height = glConfig.vidHeight;
		blends = 1;
		checkname = args.Argv( 1 );
		break;
	case 3:
		width = atoi( args.Argv( 1 ) );
		height = atoi( args.Argv( 2 ) );
		blends = 1;
		R_ScreenshotFilename( lastNumber, "screenshots/shot", checkname );
		break;
	case 4:
		width = atoi( args.Argv( 1 ) );
		height = atoi( args.Argv( 2 ) );
		blends = atoi( args.Argv( 3 ) );
		if ( blends < 1 ) {
			blends = 1;
		}
		if ( blends > MAX_BLENDS ) {
			blends = MAX_BLENDS;
		}
		R_ScreenshotFilename( lastNumber, "screenshots/shot", checkname );
		break;
	default:
		common->Printf( "usage: screenshot\n       screenshot <filename>\n       screenshot <width> <height>\n       screenshot <width> <height> <blends>\n" );
		return;
	}

	// put the console away
	console->Close();

	tr.TakeScreenshot( width, height, checkname, blends, NULL );

	common->Printf( "Wrote %s\n", checkname.c_str() );
}

/*
===============
R_StencilShot
Save out a screenshot showing the stencil buffer expanded by 16x range
===============
*/
void R_StencilShot( void ) {
	byte		*buffer;
	int			i, c;

	int	width = tr.GetScreenWidth();
	int	height = tr.GetScreenHeight();

	int	pix = width * height;

	c = pix * 3 + 18;
	buffer = (byte *)Mem_Alloc(c);
	memset (buffer, 0, 18);

	byte *byteBuffer = (byte *)Mem_Alloc(pix);

	glReadPixels( 0, 0, width, height, GL_STENCIL_INDEX , GL_UNSIGNED_BYTE, byteBuffer ); 

	for ( i = 0 ; i < pix ; i++ ) {
		buffer[18+i*3] =
		buffer[18+i*3+1] =
			//		buffer[18+i*3+2] = ( byteBuffer[i] & 15 ) * 16;
		buffer[18+i*3+2] = byteBuffer[i];
	}

	// fill in the header (this is vertically flipped, which glReadPixels emits)
	buffer[2] = 2;		// uncompressed type
	buffer[12] = width & 255;
	buffer[13] = width >> 8;
	buffer[14] = height & 255;
	buffer[15] = height >> 8;
	buffer[16] = 24;	// pixel size

	fileSystem->WriteFile( "screenshots/stencilShot.tga", buffer, c, "fs_savepath" );

	Mem_Free( buffer );
	Mem_Free( byteBuffer );	
}

/* 
================== 
R_EnvShot_f

envshot <basename>

Saves out env/<basename>_ft.tga, etc
================== 
*/  
void R_EnvShot_f( const idCmdArgs &args ) {
	idStr		fullname;
	const char	*baseName;
	int			i;
	idMat3		axis[6];
	renderView_t	ref;
	viewDef_t	primary;
	int			blends;
	char	*extensions[6] =  { "_px.tga", "_nx.tga", "_py.tga", "_ny.tga", 
		"_pz.tga", "_nz.tga" };
	int			size;

	if ( args.Argc() != 2 && args.Argc() != 3 && args.Argc() != 4 ) {
		common->Printf( "USAGE: envshot <basename> [size] [blends]\n" );
		return;
	}
	baseName = args.Argv( 1 );

	blends = 1;
	if ( args.Argc() == 4 ) {
		size = atoi( args.Argv( 2 ) );
		blends = atoi( args.Argv( 3 ) );
	} else if ( args.Argc() == 3 ) {
		size = atoi( args.Argv( 2 ) );
		blends = 1;
	} else {
		size = 256;
		blends = 1;
	}

	if ( !tr.primaryView ) {
		common->Printf( "No primary view.\n" );
		return;
	}

	primary = *tr.primaryView;

	memset( &axis, 0, sizeof( axis ) );
	axis[0][0][0] = 1;
	axis[0][1][2] = 1;
	axis[0][2][1] = 1;

	axis[1][0][0] = -1;
	axis[1][1][2] = -1;
	axis[1][2][1] = 1;

	axis[2][0][1] = 1;
	axis[2][1][0] = -1;
	axis[2][2][2] = -1;

	axis[3][0][1] = -1;
	axis[3][1][0] = -1;
	axis[3][2][2] = 1;

	axis[4][0][2] = 1;
	axis[4][1][0] = -1;
	axis[4][2][1] = 1;

	axis[5][0][2] = -1;
	axis[5][1][0] = 1;
	axis[5][2][1] = 1;

	for ( i = 0 ; i < 6 ; i++ ) {
		ref = primary.renderView;
		ref.x = ref.y = 0;
		ref.fov_x = ref.fov_y = 90;
		ref.width = glConfig.vidWidth;
		ref.height = glConfig.vidHeight;
		ref.viewaxis = axis[i];
		sprintf( fullname, "env/%s%s", baseName, extensions[i] );
		tr.TakeScreenshot( size, size, fullname, blends, &ref );
	}

	common->Printf( "Wrote %s, etc\n", fullname.c_str() );
} 

//============================================================================

static idMat3		cubeAxis[6];


/*
==================
R_SampleCubeMap
==================
*/
void R_SampleCubeMap( const idVec3 &dir, int size, byte *buffers[6], byte result[4] ) {
	float	adir[3];
	int		axis, x, y;

	adir[0] = fabs(dir[0]);
	adir[1] = fabs(dir[1]);
	adir[2] = fabs(dir[2]);

	if ( dir[0] >= adir[1] && dir[0] >= adir[2] ) {
		axis = 0;
	} else if ( -dir[0] >= adir[1] && -dir[0] >= adir[2] ) {
		axis = 1;
	} else if ( dir[1] >= adir[0] && dir[1] >= adir[2] ) {
		axis = 2;
	} else if ( -dir[1] >= adir[0] && -dir[1] >= adir[2] ) {
		axis = 3;
	} else if ( dir[2] >= adir[1] && dir[2] >= adir[2] ) {
		axis = 4;
	} else {
		axis = 5;
	}

	float	fx = (dir * cubeAxis[axis][1]) / (dir * cubeAxis[axis][0]);
	float	fy = (dir * cubeAxis[axis][2]) / (dir * cubeAxis[axis][0]);

	fx = -fx;
	fy = -fy;
	x = size * 0.5 * (fx + 1);
	y = size * 0.5 * (fy + 1);
	if ( x < 0 ) {
		x = 0;
	} else if ( x >= size ) {
		x = size-1;
	}
	if ( y < 0 ) {
		y = 0;
	} else if ( y >= size ) {
		y = size-1;
	}

	result[0] = buffers[axis][(y*size+x)*4+0];
	result[1] = buffers[axis][(y*size+x)*4+1];
	result[2] = buffers[axis][(y*size+x)*4+2];
	result[3] = buffers[axis][(y*size+x)*4+3];
}

/* 
================== 
R_MakeAmbientMap_f

R_MakeAmbientMap_f <basename> [size]

Saves out env/<basename>_amb_ft.tga, etc
================== 
*/  
void R_MakeAmbientMap_f( const idCmdArgs &args ) {
	idStr fullname;
	const char	*baseName;
	int			i;
	renderView_t	ref;
	viewDef_t	primary;
	int			downSample;
	char	*extensions[6] =  { "_px.tga", "_nx.tga", "_py.tga", "_ny.tga", 
		"_pz.tga", "_nz.tga" };
	int			outSize;
	byte		*buffers[6];
	int			width, height;

	if ( args.Argc() != 2 && args.Argc() != 3 ) {
		common->Printf( "USAGE: ambientshot <basename> [size]\n" );
		return;
	}
	baseName = args.Argv( 1 );

	downSample = 0;
	if ( args.Argc() == 3 ) {
		outSize = atoi( args.Argv( 2 ) );
	} else {
		outSize = 32;
	}

	memset( &cubeAxis, 0, sizeof( cubeAxis ) );
	cubeAxis[0][0][0] = 1;
	cubeAxis[0][1][2] = 1;
	cubeAxis[0][2][1] = 1;

	cubeAxis[1][0][0] = -1;
	cubeAxis[1][1][2] = -1;
	cubeAxis[1][2][1] = 1;

	cubeAxis[2][0][1] = 1;
	cubeAxis[2][1][0] = -1;
	cubeAxis[2][2][2] = -1;

	cubeAxis[3][0][1] = -1;
	cubeAxis[3][1][0] = -1;
	cubeAxis[3][2][2] = 1;

	cubeAxis[4][0][2] = 1;
	cubeAxis[4][1][0] = -1;
	cubeAxis[4][2][1] = 1;

	cubeAxis[5][0][2] = -1;
	cubeAxis[5][1][0] = 1;
	cubeAxis[5][2][1] = 1;

	// read all of the images
	for ( i = 0 ; i < 6 ; i++ ) {
		sprintf( fullname, "env/%s%s", baseName, extensions[i] );
		common->Printf( "loading %s\n", fullname.c_str() );
		session->UpdateScreen();
		R_LoadImage( fullname, &buffers[i], &width, &height, NULL, true );
		if ( !buffers[i] ) {
			common->Printf( "failed.\n" );
			for ( i-- ; i >= 0 ; i-- ) {
				Mem_Free( buffers[i] );
			}
			return;
		}
	}

	// resample with hemispherical blending
	int	samples = 1000;

	byte	*outBuffer = (byte *)_alloca( outSize * outSize * 4 );

	for ( int map = 0 ; map < 2 ; map++ ) {
		for ( i = 0 ; i < 6 ; i++ ) {
			for ( int x = 0 ; x < outSize ; x++ ) {
				for ( int y = 0 ; y < outSize ; y++ ) {
					idVec3	dir;
					float	total[3];

					dir = cubeAxis[i][0] + -( -1 + 2.0*x/(outSize-1) ) * cubeAxis[i][1] + -( -1 + 2.0*y/(outSize-1) ) * cubeAxis[i][2];
					dir.Normalize();
					total[0] = total[1] = total[2] = 0;
	//samples = 1;
					float	limit = map ? 0.95 : 0.25;		// small for specular, almost hemisphere for ambient

					for ( int s = 0 ; s < samples ; s++ ) {
						// pick a random direction vector that is inside the unit sphere but not behind dir,
						// which is a robust way to evenly sample a hemisphere
						idVec3	test;
						while( 1 ) {
							for ( int j = 0 ; j < 3 ; j++ ) {
								test[j] = -1 + 2 * (rand()&0x7fff)/(float)0x7fff;
							}
							if ( test.Length() > 1.0 ) {
								continue;
							}
							test.Normalize();
							if ( test * dir > limit ) {	// don't do a complete hemisphere
								break;
							}
						}
						byte	result[4];
	//test = dir;
						R_SampleCubeMap( test, width, buffers, result );
						total[0] += result[0];
						total[1] += result[1];
						total[2] += result[2];
					}
					outBuffer[(y*outSize+x)*4+0] = total[0] / samples;
					outBuffer[(y*outSize+x)*4+1] = total[1] / samples;
					outBuffer[(y*outSize+x)*4+2] = total[2] / samples;
					outBuffer[(y*outSize+x)*4+3] = 255;
				}
			}

			if ( map == 0 ) {
				sprintf( fullname, "env/%s_amb%s", baseName, extensions[i] );
			} else {
				sprintf( fullname, "env/%s_spec%s", baseName, extensions[i] );
			}
			common->Printf( "writing %s\n", fullname.c_str() );
			session->UpdateScreen();
			R_WriteTGA( fullname, outBuffer, outSize, outSize );
		}
	}

	for ( i = 0 ; i < 6 ; i++ ) {
		if ( buffers[i] ) {
			Mem_Free( buffers[i] );
		}
	}
} 

//============================================================================


/*
===============
R_SetColorMappings
===============
*/
void R_SetColorMappings( void ) {
	int		i, j;
	float	g, b;
	int		inf;

	b = r_brightness.GetFloat();
	g = r_gamma.GetFloat();

	for ( i = 0; i < 256; i++ ) {
		j = i * b;
		if (j > 255) {
			j = 255;
		}

		if ( g == 1 ) {
			inf = (j<<8) | j;
		} else {
			inf = 0xffff * pow ( j/255.0f, 1.0f / g ) + 0.5f;
		}
		if (inf < 0) {
			inf = 0;
		}
		if (inf > 0xffff) {
			inf = 0xffff;
		}

		tr.gammaTable[i] = inf;
	}

	GLimp_SetGamma( tr.gammaTable, tr.gammaTable, tr.gammaTable );
}


/*
================
GfxInfo_f
================
*/
void GfxInfo_f( const idCmdArgs &args ) {
	const char *fsstrings[] =
	{
		"windowed",
		"fullscreen"
	};
	const char *modeString = fsstrings[r_fullscreen.GetBool()];
	if ( !r_fullscreen.GetBool() && r_borderless.GetBool() ) {
		modeString = "borderless";
	}
	const char *fullscreenPolicy = r_fullscreenDesktop.GetBool() ? "desktop" : "exclusive";

	common->Printf( "\nGL_VENDOR: %s\n", glConfig.vendor_string );
	common->Printf( "GL_RENDERER: %s\n", glConfig.renderer_string );
	common->Printf( "GL_VERSION: %s\n", glConfig.version_string );
	common->Printf( "GL_EXTENSIONS: %s\n", glConfig.extensions_string );
	if ( glConfig.wgl_extensions_string ) {
		common->Printf( "WGL_EXTENSIONS: %s\n", glConfig.wgl_extensions_string );
	}
	common->Printf( "GL_MAX_TEXTURE_SIZE: %d\n", glConfig.maxTextureSize );
	common->Printf( "GL_MAX_TEXTURE_UNITS_ARB: %d\n", glConfig.maxTextureUnits );
	common->Printf( "GL_MAX_TEXTURE_COORDS_ARB: %d\n", glConfig.maxTextureCoords );
	common->Printf( "GL_MAX_TEXTURE_IMAGE_UNITS_ARB: %d\n", glConfig.maxTextureImageUnits );
	common->Printf( "GL_MAX_DRAW_BUFFERS_ARB: %d\n", glConfig.maxDrawBuffers );
	common->Printf( "GL_MAX_COLOR_ATTACHMENTS_EXT: %d\n", glConfig.maxColorAttachments );
	common->Printf( "\nPIXELFORMAT: color(%d-bits) Z(%d-bit) stencil(%d-bits)\n", glConfig.colorBits, glConfig.depthBits, glConfig.stencilBits );
	common->Printf( "MODE: %d, %d x %d %s", r_mode.GetInteger(), glConfig.vidWidth, glConfig.vidHeight, modeString );
	if ( r_fullscreen.GetBool() ) {
		common->Printf( " policy=%s", fullscreenPolicy );
	}
	common->Printf( " hz:" );

	if ( glConfig.displayFrequency ) {
		common->Printf( "%d\n", glConfig.displayFrequency );
	} else {
		common->Printf( "N/A\n" );
	}
	common->Printf( "CPU: %s\n", Sys_GetProcessorString() );

	const char *active[2] = { "", " (ACTIVE)" };

	common->Printf( "Requested renderer path: %s\n", r_renderer.GetString() );
	common->Printf( "Active renderer path: %s\n", r_actualRenderer.GetString() );
	common->Printf( "Requested GL tier: %s\n", r_glTier.GetString() );
	common->Printf( "Selected renderer tier: %s\n", RendererTier_Name( glConfig.rendererTier ) );
	common->Printf( "GL context profile: %s", RendererContextProfile_Name( glConfig.backendCaps.profile ) );
	if ( glConfig.contextRequest.label[0] != '\0' ) {
		common->Printf( " (%s)", glConfig.contextRequest.label );
	}
	common->Printf( "\n" );
	common->Printf(
		"GL context request: %s %d.%d explicit=%d requestedDebug=%d actualDebug=%d forwardCompatible=%d\n",
		RendererContextProfile_Name( glConfig.contextRequest.profile ),
		glConfig.contextRequest.major,
		glConfig.contextRequest.minor,
		glConfig.contextRequest.explicitVersion ? 1 : 0,
		glConfig.contextRequest.debugContext ? 1 : 0,
		glConfig.backendCaps.debugContext ? 1 : 0,
		glConfig.backendCaps.forwardCompatibleContext ? 1 : 0 );
	common->Printf(
		"Renderer features: modern=%d gl41=%d gpuDriven=%d lowOverhead=%d persistentUploads=%d DSA=%d multiBind=%d renderGraph=%d scenePackets=%d legacyBridge=%d\n",
		glConfig.renderFeatures.modernBaseline ? 1 : 0,
		glConfig.renderFeatures.modernGL41 ? 1 : 0,
		glConfig.renderFeatures.gpuDriven ? 1 : 0,
		glConfig.renderFeatures.lowOverhead ? 1 : 0,
		glConfig.renderFeatures.persistentMappedUploads ? 1 : 0,
		glConfig.renderFeatures.directStateAccess ? 1 : 0,
		glConfig.renderFeatures.multiBind ? 1 : 0,
		glConfig.renderFeatures.renderGraph ? 1 : 0,
		glConfig.renderFeatures.scenePackets ? 1 : 0,
		glConfig.renderFeatures.legacyARB2Bridge ? 1 : 0 );
	{
		char capsSummary[512];
		RendererCaps_FormatSummary( glConfig.backendCaps, capsSummary, sizeof( capsSummary ) );
		common->Printf( "Renderer caps: %s\n", capsSummary );
	}
	common->Printf(
		"Renderer GPU timers: %s, cvar=%d, timerQuery=%d\n",
		R_RendererMetrics_GpuTimersAvailable() ? "available" : "unavailable",
		r_rendererGpuTimers.GetBool() ? 1 : 0,
		glConfig.backendCaps.hasTimerQuery ? 1 : 0 );
	common->Printf(
		"Renderer scene packets: legacy bridge, maxScenes=%d, maxPasses=%d, maxDrawPackets=%d, maxMaterialRecords=%d\n",
		SCENE_PACKET_MAX_SCENES,
		SCENE_PACKET_MAX_PASSES,
		SCENE_PACKET_MAX_DRAWS,
		SCENE_PACKET_MAX_MATERIAL_RECORDS );
	common->Printf(
		"Renderer graph: resource-backed packet graph, maxPasses=%d, maxResources=%d, maxResourceAccesses=%d\n",
		RENDER_GRAPH_MAX_PASSES,
		RENDER_GRAPH_MAX_RESOURCES,
		RENDER_GRAPH_MAX_RESOURCE_ACCESSES );
	R_ModernGLExecutor_PrintGfxInfo();
	{
		const rendererUploadStats_t &uploadStats = R_RendererUpload_Stats();
		common->Printf(
			"Renderer upload manager: frameStream=%s, staticAllocator=%d, buffers=%d, ring=%dKB, persistent=%d, mapRangeFallback=%d, staticLive=%d/%dKB, legacyBridge=%d\n",
			uploadStats.dynamicFrameBridge ? "enabled" : "disabled",
			uploadStats.staticBufferAllocator ? 1 : 0,
			uploadStats.ringBufferCount,
			uploadStats.ringSizeBytes / 1024,
			uploadStats.persistentMapped ? 1 : 0,
			uploadStats.mapRangeFallback ? 1 : 0,
			uploadStats.staticBuffersLive,
			uploadStats.staticBytesLive / 1024,
			uploadStats.legacyBridge ? 1 : 0 );
	}

	if ( glConfig.allowARB2Path ) {
		common->Printf( "ARB2 path ENABLED%s\n", active[tr.backEndRenderer == BE_ARB2] );
	} else {
		common->Printf( "ARB2 path disabled\n" );
	}

	if ( glConfig.preferNV20Path && !glConfig.allowNV20Path ) {
		common->Printf( "Legacy NV20 compatibility preference detected, but no NV20 backend is available in this build\n" );
	}
	if ( glConfig.preferSimpleLighting ) {
		common->Printf( "Simple lighting compatibility mode preferred for this renderer\n" );
	}

	//=============================

	common->Printf( "-------\n" );

	if ( r_finish.GetBool() ) {
		common->Printf( "Forcing glFinish\n" );
	} else {
		common->Printf( "glFinish not forced\n" );
	}

#ifdef _WIN32	
// WGL_EXT_swap_interval
typedef BOOL (WINAPI * PFNWGLSWAPINTERVALEXTPROC) (int interval);
extern	PFNWGLSWAPINTERVALEXTPROC wglSwapIntervalEXT;

	if ( r_swapInterval.GetInteger() && wglSwapIntervalEXT ) {
		common->Printf( "Forcing swapInterval %i\n", r_swapInterval.GetInteger() );
	} else {
		common->Printf( "swapInterval not forced\n" );
	}
#endif
	
	bool tss = glConfig.twoSidedStencilAvailable || glConfig.atiTwoSidedStencilAvailable;

	if ( !r_useTwoSidedStencil.GetBool() && tss ) {
		common->Printf( "Two sided stencil available but disabled\n" );
	} else if ( !tss ) {
		common->Printf( "Two sided stencil not available\n" );
	} else if ( tss ) {
		common->Printf( "Using two sided stencil\n" );
	}

	if ( vertexCache.IsFast() ) {
		common->Printf( "Vertex cache is fast\n" );
	} else {
		common->Printf( "Vertex cache is SLOW\n" );
	}
}

/*
=================
R_VidRestart_f
=================
*/
static void R_PerformFullVidRestart( bool forceWindow ) {
	backEnd.renderTexture = NULL;
	tr.activeRenderTexture = NULL;
	idRenderTexture::BindNull();
	tr.ProcessPendingRenderTextureDeletes();

	// Input is tied to the native window/context lifecycle.
	Sys_ShutdownInput();

	// Force image/object handles to rebuild against the new context.
	globalImages->PurgeAllImages();
	R_ModernGLExecutor_Shutdown();
	GLimp_Shutdown();
	glConfig.isInitialized = false;

	const bool latchedFullscreen = cvarSystem->GetCVarBool( "r_fullscreen" );
	if ( forceWindow ) {
		cvarSystem->SetCVarBool( "r_fullscreen", false );
	}

	R_InitOpenGL();
	cvarSystem->SetCVarBool( "r_fullscreen", latchedFullscreen );
	backEnd.renderTexture = NULL;
	tr.activeRenderTexture = NULL;

	globalImages->ReloadImages( true );
}

static GLenum R_ClearPendingGLErrors( void ) {
	GLenum err = GL_NO_ERROR;
	GLenum lastErr = GL_NO_ERROR;
	while ( ( err = glGetError() ) != GL_NO_ERROR ) {
		lastErr = err;
	}
	return lastErr;
}

void R_VidRestart_f( const idCmdArgs &args ) {
	int	err;

	// if OpenGL isn't started, do nothing
	if ( !glConfig.isInitialized ) {
		return;
	}

	bool full = true;
	bool forceWindow = false;
	for ( int i = 1 ; i < args.Argc() ; i++ ) {
		if ( idStr::Icmp( args.Argv( i ), "partial" ) == 0 ) {
			full = false;
			continue;
		}
		if ( idStr::Icmp( args.Argv( i ), "windowed" ) == 0 ) {
			forceWindow = true;
			continue;
		}
	}

	R_NormalizeDisplayCvars();

	// this could take a while, so give them the cursor back ASAP
	Sys_GrabMouseCursor( false );

	// dump ambient caches
	renderModelManager->FreeModelVertexCaches();

	// free any current world interaction surfaces and vertex caches
	R_FreeDerivedData();

	// make sure the defered frees are actually freed
	R_ToggleSmpFrame();
	R_ToggleSmpFrame();

	// free the vertex caches so they will be regenerated again
	vertexCache.PurgeAll();

	if ( full ) {
		R_PerformFullVidRestart( forceWindow );
	} else {
		glimpParms_t	parms;
		parms.fullScreen = ( forceWindow ) ? false : r_fullscreen.GetBool();
		if ( parms.fullScreen ) {
			R_GetModeInfo( &parms.width, &parms.height, r_mode.GetInteger() );
		} else {
			R_GetWindowedModeInfo( &parms.width, &parms.height );
		}
		parms.borderless = !parms.fullScreen && r_borderless.GetBool();
		parms.displayHz = r_displayRefresh.GetInteger();
		parms.multiSamples = r_multiSamples.GetInteger();
		parms.stereo = false;
		if ( !GLimp_SetScreenParms( parms ) ) {
			common->Printf( "^3vid_restart partial failed, retrying full restart\n" );
			R_PerformFullVidRestart( forceWindow );
		} else {
			(void)R_ClearPendingGLErrors();
		}
	}

	// Mark a renderer restart generation so higher-level systems can rebuild
	// restart-sensitive resources (for example custom game render targets).
	if ( tr.videoRestartCount < 0x7fffffff ) {
		tr.videoRestartCount++;
	} else {
		tr.videoRestartCount = 1;
	}


	// make sure the regeneration doesn't use anything no longer valid
	tr.viewCount++;
	tr.viewDef = NULL;

	// regenerate all necessary interactions
	R_RegenerateWorld_f( idCmdArgs() );

	// check for problems
	GLimp_ActivateContext();
	err = glGetError();
	if ( err != GL_NO_ERROR ) {
		common->Printf( "glGetError() = 0x%x\n", err );
	}

	if ( session != NULL ) {
		session->SetPlayingSoundWorld();
	}

	// start sound playing again
	soundSystem->SetMute( false );
}


/*
=================
R_InitMaterials
=================
*/
void R_InitMaterials( void ) {
	tr.defaultMaterial = declManager->FindMaterial( "_default", false );
	if ( !tr.defaultMaterial ) {
		common->FatalError( "_default material not found" );
	}
	declManager->FindMaterial( "_default", false );

	// needed by R_DeriveLightData
	declManager->FindMaterial( "lights/defaultPointLight" );
	declManager->FindMaterial( "lights/defaultProjectedLight" );
}


/*
=================
R_SizeUp_f

Keybinding command
=================
*/
static void R_SizeUp_f( const idCmdArgs &args ) {
	if ( r_screenFraction.GetInteger() + 10 > 100 ) {
		r_screenFraction.SetInteger( 100 );
	} else {
		r_screenFraction.SetInteger( r_screenFraction.GetInteger() + 10 );
	}
}


/*
=================
R_SizeDown_f

Keybinding command
=================
*/
static void R_SizeDown_f( const idCmdArgs &args ) {
	if ( r_screenFraction.GetInteger() - 10 < 10 ) {
		r_screenFraction.SetInteger( 10 );
	} else {
		r_screenFraction.SetInteger( r_screenFraction.GetInteger() - 10 );
	}
}


/*
===============
TouchGui_f

  this is called from the main thread
===============
*/
void R_TouchGui_f( const idCmdArgs &args ) {
	const char	*gui = args.Argv( 1 );

	if ( !gui[0] ) {
		common->Printf( "USAGE: touchGui <guiName>\n" );
		return;
	}

	common->Printf( "touchGui %s\n", gui );
	session->UpdateScreen();
	uiManager->Touch( gui );
}

/*
=================
R_InitCvars
=================
*/
void R_InitCvars( void ) {
	// update latched cvars here
}

/*
=================
R_InitCommands
=================
*/
void R_InitCommands( void ) {
	cmdSystem->AddCommand( "sizeUp", R_SizeUp_f, CMD_FL_RENDERER, "makes the rendered view larger" );
	cmdSystem->AddCommand( "sizeDown", R_SizeDown_f, CMD_FL_RENDERER, "makes the rendered view smaller" );
	cmdSystem->AddCommand( "reloadGuis", R_ReloadGuis_f, CMD_FL_RENDERER, "reloads guis" );
	cmdSystem->AddCommand( "listGuis", R_ListGuis_f, CMD_FL_RENDERER, "lists guis" );
	cmdSystem->AddCommand( "touchGui", R_TouchGui_f, CMD_FL_RENDERER, "touches a gui" );
	cmdSystem->AddCommand( "screenshot", R_ScreenShot_f, CMD_FL_RENDERER, "takes a screenshot" );
	cmdSystem->AddCommand( "levelshot", R_LevelShot_f, CMD_FL_RENDERER | CMD_FL_CHEAT, "captures a 5-tile levelshot set" );
	cmdSystem->AddCommand( "envshot", R_EnvShot_f, CMD_FL_RENDERER, "takes an environment shot" );
	cmdSystem->AddCommand( "makeAmbientMap", R_MakeAmbientMap_f, CMD_FL_RENDERER|CMD_FL_CHEAT, "makes an ambient map" );
	cmdSystem->AddCommand( "benchmark", R_Benchmark_f, CMD_FL_RENDERER, "benchmark" );
	cmdSystem->AddCommand( "gfxInfo", GfxInfo_f, CMD_FL_RENDERER, "show graphics info" );
	cmdSystem->AddCommand( "rendererTierSelfTest", R_RendererTierSelfTest_f, CMD_FL_RENDERER, "run renderer tier-selection self tests" );
	cmdSystem->AddCommand( "rendererContextLadderSelfTest", R_RendererContextLadderSelfTest_f, CMD_FL_RENDERER, "run renderer context ladder self tests" );
	cmdSystem->AddCommand( "rendererUploadSelfTest", R_RendererUploadSelfTest_f, CMD_FL_RENDERER, "run renderer upload stream self tests" );
	cmdSystem->AddCommand( "rendererGpuTimerSelfTest", R_RendererGpuTimerSelfTest_f, CMD_FL_RENDERER, "run renderer GPU timer query self tests" );
	cmdSystem->AddCommand( "rendererScenePacketSelfTest", R_RendererScenePacketSelfTest_f, CMD_FL_RENDERER, "run renderer front-end scene-packet self tests" );
	cmdSystem->AddCommand( "rendererRenderGraphSelfTest", R_RendererRenderGraphSelfTest_f, CMD_FL_RENDERER, "run renderer resource-graph self tests" );
	cmdSystem->AddCommand( "rendererModernGLExecutorSelfTest", R_RendererModernGLExecutorSelfTest_f, CMD_FL_RENDERER, "run renderer modern GL executor self tests" );
	cmdSystem->AddCommand( "rendererModernGLShaderLibrarySelfTest", R_RendererModernGLShaderLibrarySelfTest_f, CMD_FL_RENDERER, "run renderer modern GL shader-library self tests" );
	cmdSystem->AddCommand( "rendererModernGLDrawPlanSelfTest", R_RendererModernGLDrawPlanSelfTest_f, CMD_FL_RENDERER, "run renderer modern GL draw-plan self tests" );
	cmdSystem->AddCommand( "rendererModernGLSubmitPlanSelfTest", R_RendererModernGLSubmitPlanSelfTest_f, CMD_FL_RENDERER, "run renderer modern GL submit-plan self tests" );
	cmdSystem->AddCommand( "modulateLights", R_ModulateLights_f, CMD_FL_RENDERER | CMD_FL_CHEAT, "modifies shader parms on all lights" );
	cmdSystem->AddCommand( "testImage", R_TestImage_f, CMD_FL_RENDERER | CMD_FL_CHEAT, "displays the given image centered on screen", idCmdSystem::ArgCompletion_ImageName );
	cmdSystem->AddCommand( "testVideo", R_TestVideo_f, CMD_FL_RENDERER | CMD_FL_CHEAT, "displays the given cinematic", idCmdSystem::ArgCompletion_VideoName );
	cmdSystem->AddCommand( "reportSurfaceAreas", R_ReportSurfaceAreas_f, CMD_FL_RENDERER, "lists all used materials sorted by surface area" );
	cmdSystem->AddCommand( "reportImageDuplication", R_ReportImageDuplication_f, CMD_FL_RENDERER, "checks all referenced images for duplications" );
	cmdSystem->AddCommand( "reportShaderPrograms", R_ReportShaderPrograms_f, CMD_FL_RENDERER, "shows ARB plus material/shadow GLSL shader program status" );
	cmdSystem->AddCommand( "regenerateWorld", R_RegenerateWorld_f, CMD_FL_RENDERER, "regenerates all interactions" );
	cmdSystem->AddCommand( "showInteractionMemory", R_ShowInteractionMemory_f, CMD_FL_RENDERER, "shows memory used by interactions" );
	cmdSystem->AddCommand( "showTriSurfMemory", R_ShowTriSurfMemory_f, CMD_FL_RENDERER, "shows memory used by triangle surfaces" );
	cmdSystem->AddCommand( "vid_restart", R_VidRestart_f, CMD_FL_RENDERER, "restarts renderSystem" );
	cmdSystem->AddCommand( "listRenderEntityDefs", R_ListRenderEntityDefs_f, CMD_FL_RENDERER, "lists the entity defs" );
	cmdSystem->AddCommand( "listRenderLightDefs", R_ListRenderLightDefs_f, CMD_FL_RENDERER, "lists the light defs" );
	cmdSystem->AddCommand( "listModes", R_ListModes_f, CMD_FL_RENDERER, "lists all video modes" );
	cmdSystem->AddCommand( "reloadSurface", R_ReloadSurface_f, CMD_FL_RENDERER, "reloads the decl and images for selected surface" );
}

/*
===============
idRenderSystemLocal::Clear
===============
*/
void idRenderSystemLocal::Clear( void ) {
	registered = false;
	frameCount = 0;
	viewCount = 0;
	videoRestartCount = 0;
	staticAllocCount = 0;
	frameShaderTime = 0.0f;
	frameShaderTimeMsec = 0;
	deltaTime = 0.0f;
	lastRenderTimeMsec = 0;
	viewportOffset[0] = 0;
	viewportOffset[1] = 0;
	tiledViewport[0] = 0;
	tiledViewport[1] = 0;
	backEndRenderer = BE_BAD;
	backEndRendererHasVertexPrograms = false;
	backEndRendererMaxLight = 1.0f;
	ambientLightVector.Zero();
	sortOffset = 0;
	worlds.Clear();
	primaryWorld = NULL;
	memset( &primaryRenderView, 0, sizeof( primaryRenderView ) );
	primaryView = NULL;
	ResetSpecialEffects();
	specialBlurDepthImage = NULL;
	specialBlurDepthStencilImage = NULL;
	specialBlurImage = NULL;
	specialBlurDepthRenderTexture = NULL;
	specialBlurRenderTexture = NULL;
	specialALDepthImage = NULL;
	specialALDepthStencilImage = NULL;
	specialALDepthRenderTexture = NULL;
	specialALLightImage = NULL;
	defaultMaterial = NULL;
	testImage = NULL;
	ambientCubeImage = NULL;
	viewDef = NULL;
	memset( &pc, 0, sizeof( pc ) );
	memset( &lockSurfacesCmd, 0, sizeof( lockSurfacesCmd ) );
	memset( &identitySpace, 0, sizeof( identitySpace ) );
	logFile = NULL;
	stencilIncr = 0;
	stencilDecr = 0;
	memset( renderCrops, 0, sizeof( renderCrops ) );
	currentRenderCrop = 0;
	guiRecursionLevel = 0;
	guiModel = NULL;
	demoGuiModel = NULL;
	pendingRenderTextureDeletes.Clear();
	useUIViewportFor2D = true;
	activeRenderTexture = NULL;
	suppressLevelshotViewModels = false;
	disableLevelshotEntityCulling = false;
	memset( gammaTable, 0, sizeof( gammaTable ) );
	takingScreenshot = false;
}

/*
===============
idRenderSystemLocal::Init
===============
*/
void idRenderSystemLocal::Init( void ) {	

	common->Printf( "------- Initializing renderSystem --------\n" );

	// clear all our internal state
	viewCount = 1;		// so cleared structures never match viewCount
	// we used to memset tr, but now that it is a class, we can't, so
	// there may be other state we need to reset

	ambientLightVector[0] = 0.5f;
	ambientLightVector[1] = 0.5f - 0.385f;
	ambientLightVector[2] = 0.8925f;
	ambientLightVector[3] = 1.0f;
	deltaTime = 0.0f;
	lastRenderTimeMsec = 0;

	memset( &backEnd, 0, sizeof( backEnd ) );

	R_InitCvars();

	R_InitCommands();

	guiModel = new idGuiModel;
	guiModel->Clear();

	demoGuiModel = new idGuiModel;
	demoGuiModel->Clear();

	R_InitTriSurfData();

	globalImages->Init();

	idCinematic::InitCinematic( );

	// build brightness translation tables
	R_SetColorMappings();

	R_InitMaterials();

	renderModelManager->Init();

	// set the identity space
	identitySpace.modelMatrix[0*4+0] = 1.0f;
	identitySpace.modelMatrix[1*4+1] = 1.0f;
	identitySpace.modelMatrix[2*4+2] = 1.0f;

	// determine which back end we will use
	// ??? this is invalid here as there is not enough information to set it up correctly
	SetBackEndRenderer();

	common->Printf( "renderSystem initialized.\n" );
	common->Printf( "--------------------------------------\n" );
}

/*
===============
idRenderSystemLocal::Shutdown
===============
*/
void idRenderSystemLocal::Shutdown( void ) {	
	common->Printf( "idRenderSystem::Shutdown()\n" );

	R_DoneFreeType( );

	if ( glConfig.isInitialized ) {
		globalImages->PurgeAllImages();
	}

	renderModelManager->Shutdown();

	idCinematic::ShutdownCinematic( );

	globalImages->Shutdown();

	// close the r_logFile
	if ( logFile ) {
		fprintf( logFile, "*** CLOSING LOG ***\n" );
		fclose( logFile );
		logFile = 0;
	}

	// free frame memory
	R_ShutdownFrameData();

	// free the vertex cache, which should have nothing allocated now
	vertexCache.Shutdown();

	R_ShutdownTriSurfData();

	RB_ShutdownDebugTools();

	ProcessPendingRenderTextureDeletes();

	delete guiModel;
	delete demoGuiModel;

	ShutdownSpecialEffects();
	Clear();

	ShutdownOpenGL();
}

/*
========================
idRenderSystemLocal::BeginLevelLoad
========================
*/
void idRenderSystemLocal::BeginLevelLoad( void ) {
	renderModelManager->BeginLevelLoad();
	globalImages->BeginLevelLoad();
}

/*
========================
idRenderSystemLocal::EndLevelLoad
========================
*/
void idRenderSystemLocal::EndLevelLoad( void ) {
	// Count each phase immediately before it runs so assets discovered by earlier
	// phases (for example images referenced during model loads) are included.
	const int pendingModelLoads = renderModelManager->CountPendingLevelLoads();
	session->BeginLoadingAssetQueue( pendingModelLoads );
	renderModelManager->EndLevelLoad();
	session->EndLoadingAssetQueue();

	const int pendingImageLoads = globalImages->CountPendingLevelLoads();
	session->BeginLoadingAssetQueue( pendingImageLoads );
	globalImages->EndLevelLoad();
	session->EndLoadingAssetQueue();

	if ( r_forceLoadImages.GetBool() ) {
		RB_ShowImages();
	}
}

/*
========================
idRenderSystemLocal::InitOpenGL
========================
*/
void idRenderSystemLocal::InitOpenGL( void ) {
	// if OpenGL isn't started, start it now
	if ( !glConfig.isInitialized ) {
		int	err;

		R_InitOpenGL();

		globalImages->ReloadImages(true);

		err = glGetError();
		if ( err != GL_NO_ERROR ) {
			common->Printf( "glGetError() = 0x%x\n", err );
		}
	}
}

/*
========================
idRenderSystemLocal::ShutdownOpenGL
========================
*/
void idRenderSystemLocal::ShutdownOpenGL( void ) {
	ShutdownSpecialEffects();
	ProcessPendingRenderTextureDeletes();

	// free the context and close the window
	R_ShutdownFrameData();
	R_RendererMetrics_ShutdownGpuTimers();
	R_ModernGLExecutor_Shutdown();
	R_RendererUpload_Shutdown();
	RendererBootstrap_Shutdown();
	GLimp_Shutdown();
	glConfig.isInitialized = false;
	backEnd.renderTexture = NULL;
	activeRenderTexture = NULL;
	useUIViewportFor2D = true;
}

/*
========================
idRenderSystemLocal::IsOpenGLRunning
========================
*/
bool idRenderSystemLocal::IsOpenGLRunning( void ) const {
	if ( !glConfig.isInitialized ) {
		return false;
	}
	return true;
}

/*
========================
idRenderSystemLocal::IsFullScreen
========================
*/
bool idRenderSystemLocal::IsFullScreen( void ) const {
	return glConfig.isFullscreen;
}

/*
========================
idRenderSystemLocal::GetScreenWidth
========================
*/
int idRenderSystemLocal::GetScreenWidth( void ) const {
	return glConfig.vidWidth;
}

/*
========================
idRenderSystemLocal::GetScreenHeight
========================
*/
int idRenderSystemLocal::GetScreenHeight( void ) const {
	return glConfig.vidHeight;
}

/*
========================
idRenderSystemLocal::GetVideoRestartCount
========================
*/
int idRenderSystemLocal::GetVideoRestartCount( void ) const {
	return videoRestartCount;
}

/*
========================
idRenderSystemLocal::GetCardCaps
========================
*/
void idRenderSystemLocal::GetCardCaps( bool &oldCard, bool &nv10or20 ) {
	oldCard = false;
	nv10or20 = false;
}
