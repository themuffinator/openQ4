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
#include "../imagetools/DXT/DXTCodec.h"
#include "CelShading.h"
#include "RendererBootstrap.h"
#include "RendererModule.h"
#include "GLDebugScope.h"
#include "GLStateCache.h"
#include "RendererBenchmarks.h"
#include "RendererMetrics.h"
#include "RendererUpload.h"
#include "RendererContracts.h"
#include "RenderGraph.h"
#include "RenderGraphResources.h"
#include "MaterialResourceTable.h"
#include "ClassicGuiDomain.h"
#include "ClassicCinematicPostDomain.h"
#include "ClassicSpecialFrameDomain.h"
#include "ClassicWorldAmbientDomain.h"
#include "ClassicInteractionDomain.h"
#include "ClassicFogBlendDomain.h"
#include "ClassicSubviewDomain.h"
#include "ClassicDeformDomain.h"
#include "GeometryResources.h"
#include "ScenePackets.h"
#include "ModernClusteredLighting.h"
#include "ModernGLExecutor.h"
#include "ModernGLDrawPlan.h"
#include "ModernGLShaderLibrary.h"
#include "ModernGLSubmitPlan.h"
#include "ModernLightImageAtlas.h"
#include "ModernShadowPlanner.h"
#include "../framework/RenderDoc.h"
#include "../framework/declEntityDef.h"
#if defined(__APPLE__)
#include <execinfo.h>
#endif

// Detect the Microsoft software OpenGL wrapper and guide the user toward
// installing proper vendor drivers.
#ifdef _WIN32
#include "../sys/win32/win_local.h"
#endif

// functions that are not called every frame

static void R_ErrorForUnsupportedCompatibilityOpenGL( void ) {
	common->Error( "%s", common->GetLanguageDict()->GetString( "#str_41106" ) );
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
		// These "required" features are the fixed-function and ARB-assembly
		// extensions the legacy ARB2 path is built on. A profile that removed
		// fixed-function cannot report them by definition, so their absence is
		// expected rather than fatal: such a context is served by the modern
		// programmable path, and RendererCaps_SupportsTier already refuses the
		// legacy tier for it. Applies to OpenGL ES at any version and to a
		// desktop core profile.
		if ( glConfig.backendCaps.profile == RENDERER_CONTEXT_PROFILE_ES
			|| glConfig.backendCaps.profile == RENDERER_CONTEXT_PROFILE_CORE ) {
			return false;
		}
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
			"openQ4 currently requires OpenGL compatibility / ARB2-era features "
			"that are unavailable in the injected context.\n" );
		R_ErrorForUnsupportedCompatibilityOpenGL();
	}

	common->Error( "%s", common->GetLanguageDict()->GetString( "#str_06780" ) );
}

static bool R_CheckGLSLProgramExtensions() {
	return R_CheckExtension( "GL_ARB_shader_objects" )
		&& R_CheckExtension( "GL_ARB_vertex_shader" )
		&& R_CheckExtension( "GL_ARB_fragment_shader" )
		&& R_CheckExtension( "GL_ARB_shading_language_100" );
}

static bool R_HasARBMultitextureEntryPoints() {
	return glActiveTextureARB != NULL
		&& glClientActiveTextureARB != NULL
		&& glMultiTexCoord2fARB != NULL;
}

static bool R_HasARBBufferObjectEntryPoints() {
	return glBindBufferARB != NULL
		&& glDeleteBuffersARB != NULL
		&& glGenBuffersARB != NULL
		&& glBufferDataARB != NULL;
}

static bool R_HasARBVertexBufferObjectEntryPoints() {
	return R_HasARBBufferObjectEntryPoints()
		&& glBufferSubDataARB != NULL;
}

static bool R_HasARBPixelBufferObjectEntryPoints() {
	return R_HasARBBufferObjectEntryPoints()
		&& glMapBufferARB != NULL
		&& glUnmapBufferARB != NULL;
}

static bool R_HasARBProgramEntryPoints() {
	return glBindProgramARB != NULL
		&& glProgramStringARB != NULL
		&& glProgramEnvParameter4fvARB != NULL
		&& glProgramLocalParameter4fvARB != NULL
		&& glVertexAttribPointerARB != NULL
		&& glEnableVertexAttribArrayARB != NULL
		&& glDisableVertexAttribArrayARB != NULL;
}

static bool R_HasGLSLProgramEntryPoints() {
	return glCreateShaderObjectARB != NULL
		&& glShaderSourceARB != NULL
		&& glCompileShaderARB != NULL
		&& glGetObjectParameterivARB != NULL
		&& glGetInfoLogARB != NULL
		&& glCreateProgramObjectARB != NULL
		&& glAttachObjectARB != NULL
		&& glDetachObjectARB != NULL
		&& glBindAttribLocationARB != NULL
		&& glLinkProgramARB != NULL
		&& glUseProgramObjectARB != NULL
		&& glGetUniformLocationARB != NULL
		&& glUniform1fARB != NULL
		&& glUniform1fvARB != NULL
		&& glUniform1iARB != NULL
		&& glUniform2fvARB != NULL
		&& glUniform3fvARB != NULL
		&& glUniform4fvARB != NULL
		&& glUniformMatrix4fvARB != NULL
		&& glVertexAttribPointerARB != NULL
		&& glEnableVertexAttribArrayARB != NULL
		&& glDisableVertexAttribArrayARB != NULL
		&& glDeleteObjectARB != NULL;
}

static bool R_CanUseGLSLPrograms() {
	if ( r_inhibitFragmentProgram.GetBool() ) {
		return false;
	}

	if ( cvarSystem->GetCVarInteger( "net_serverDedicated" ) != 0 ) {
		return false;
	}

	if ( !glConfig.ARBVertexProgramAvailable || !glConfig.ARBFragmentProgramAvailable ) {
		return false;
	}

	if ( !R_CheckGLSLProgramExtensions() ) {
		return false;
	}

	if ( !R_HasGLSLProgramEntryPoints() ) {
		common->Printf( "X..GLSL shader object entry points incomplete\n" );
		return false;
	}

	return true;
}

glconfig_t	glConfig;

static void GfxInfo_f( void );

const char *r_rendererArgs[] = { "best", "arb2", "modern", "glesd3", "arb", "Cg", "exp", "nv10", "nv20", "r200", NULL };
const char *r_glTierArgs[] = { "auto", "legacy", "gl33", "gl41", "gl43", "gl45", "gl46", NULL };
const char *r_rendererBenchmarkPresetArgs[] = { "low", "baseline", "modern", "high-end", NULL };
const char *r_multiSamplesArgs[] = { "0", "2", "4", "8", "16", NULL };

idCVar r_inhibitFragmentProgram( "r_inhibitFragmentProgram", "0", CVAR_RENDERER | CVAR_BOOL, "ignore the fragment program extension" );
idCVar r_glDriver( "r_glDriver", "", CVAR_RENDERER, "\"opengl32\", etc." );
idCVar r_useLightPortalFlow( "r_useLightPortalFlow", "1", CVAR_RENDERER | CVAR_BOOL, "use a more precise area reference determination" );
idCVar r_multiSamples( "r_multiSamples", "0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_INTEGER, "MSAA sample count: 0 = off, 2/4/8/16 = supported quality steps", 0, 16, idCmdSystem::ArgCompletion_String<r_multiSamplesArgs> );
idCVar r_postAA( "r_postAA", "0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_INTEGER, "post AA mode: 0 = off, 1 = SMAA 1x medium, 2 = SMAA 1x high, 3 = SMAA 1x ultra, 4 = SMAA 1x colour-edge prototype", 0, 4, idCmdSystem::ArgCompletion_Integer<0,4> );
idCVar r_postAAStatePoisonTest( "r_postAAStatePoisonTest", "0", CVAR_RENDERER | CVAR_BOOL, "intentionally dirty GL texture/client state before SMAA post-AA draws for validation" );
idCVar r_pbrMaterials( "r_pbrMaterials", "0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL, "allow explicitly PBR-authored materials on supported modern renderer paths" );
idCVar r_pbrGeneratedLegacyFallback( "r_pbrGeneratedLegacyFallback", "1", CVAR_RENDERER | CVAR_BOOL, "allow development-only classic fallback generation for PBR-only materials" );
idCVar r_pbrDebug( "r_pbrDebug", "0", CVAR_RENDERER | CVAR_INTEGER, "PBR debug view: 0=off, 1=albedo, 2=normal, 3=metallic, 4=roughness, 5=AO, 6=emissive, 7=state marker (green=PBR, magenta=contract mismatch)", 0, 7, idCmdSystem::ArgCompletion_Integer<0,7> );
idCVar r_pbrIBL( "r_pbrIBL", "1", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL, "enable the analytic environment contribution for explicitly PBR-authored materials" );
idCVar r_pbrIBLIntensity( "r_pbrIBLIntensity", "1", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT, "analytic PBR environment intensity", 0.0f, 4.0f );
idCVar r_pbrInferFromLegacyMaterials( "r_pbrInferFromLegacyMaterials", "0", CVAR_RENDERER | CVAR_BOOL, "research-only legacy material reinterpretation; never used for stock rendering by default" );
idCVar r_bloom( "r_bloom", "0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL, "enable bloom post-process" );
idCVar r_bloomThreshold( "r_bloomThreshold", "0.45", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT, "bloom bright-pass threshold in scene-referred units", 0.0f, 16.0f );
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
idCVar r_hdrAutoExposureAsync( "r_hdrAutoExposureAsync", "1", CVAR_RENDERER | CVAR_BOOL, "read the auto-exposure luminance sample back asynchronously with one frame of latency instead of stalling the GPU pipeline" );
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
idCVar r_crtChromatic( "r_crtChromatic", "0.0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT, "optional channel convergence offset in pixel units for the CRT monitor post-process", 0.0f, 0.35f );

// openQ4 underwater view
idCVar r_underwater( "r_underwater", "1", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL, "enable the underwater view post-process" );
idCVar r_underwaterWarp( "r_underwaterWarp", "1.0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT, "refraction strength of the underwater view, 0 disables the warp", 0.0f, 4.0f );
idCVar r_underwaterBlur( "r_underwaterBlur", "1.0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT, "soft focus strength of the underwater view, 0 keeps the image sharp", 0.0f, 4.0f );
idCVar r_underwaterEdgeSoften( "r_underwaterEdgeSoften", "0.8", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT, "how much the underwater view loses focus toward its edges - a soft focus mask, it never darkens the image", 0.0f, 2.0f );
idCVar r_underwaterCaustics( "r_underwaterCaustics", "0.06", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT, "strength of the moving light pattern in the underwater view", 0.0f, 0.5f );
idCVar r_underwaterBloom( "r_underwaterBloom", "1.0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT, "halo suspended particulate throws around lights underwater", 0.0f, 4.0f );
idCVar r_underwaterAberration( "r_underwaterAberration", "0.35", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT, "channel separation toward the edges of the underwater view", 0.0f, 4.0f );
idCVar r_underwaterParticles( "r_underwaterParticles", "0.25", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT, "marine snow drifting past the underwater view", 0.0f, 2.0f );
idCVar r_underwaterVisibility( "r_underwaterVisibility", "1.0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT, "scales how far you can see through a liquid before it fogs out", 0.01f, 8.0f );
idCVar r_celShading( "r_celShading", "0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL, "cel shade model entities: quantized lighting plus silhouette outlines on players, monsters, moveables and the view weapon" );
idCVar r_celShadingWorld( "r_celShadingWorld", "0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL, "extend cel shading to BSP world geometry, adding banded lighting and screen-space edge outlines" );
idCVar r_celShadingBands( "r_celShadingBands", "1", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL, "quantize interaction lighting into cel bands; disable to keep smooth lighting and use outlines alone" );
idCVar r_celShadingSteps( "r_celShadingSteps", "4", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_INTEGER, "number of cel lighting bands; higher values keep more intermediate tones", CEL_MIN_BANDS, CEL_MAX_BANDS, idCmdSystem::ArgCompletion_Integer<CEL_MIN_BANDS,CEL_MAX_BANDS> );
idCVar r_celShadingSoftness( "r_celShadingSoftness", "0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT, "soften cel band boundaries as a fraction of one band; 0 keeps the classic hard step, higher values trade a little of the step for a terminator that stops crawling across curved surfaces", CEL_MIN_BAND_SOFTNESS, CEL_MAX_BAND_SOFTNESS );
idCVar r_celShadingSpecular( "r_celShadingSpecular", "1", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL, "collapse specular highlights into a hard-edged cel highlight instead of a smooth falloff" );
idCVar r_celViewWeapon( "r_celViewWeapon", "1", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL, "allow cel shading and cel outlines on the first-person weapon and arms" );
idCVar r_celOutline( "r_celOutline", "1", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL, "draw a silhouette outline shell around cel-shaded model entities" );
idCVar r_celOutlineWidth( "r_celOutlineWidth", "2.0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT, "model cel outline width in pixels; the shell expansion is rescaled per surface so the line holds its thickness at any distance", CEL_MIN_OUTLINE_WIDTH, CEL_MAX_OUTLINE_WIDTH );
idCVar r_celOutlineAlpha( "r_celOutlineAlpha", "1.0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT, "opacity multiplier for model cel outlines", 0.0f, 1.0f );
idCVar r_celOutlineColor( "r_celOutlineColor", "0 0 0 255", CVAR_RENDERER | CVAR_ARCHIVE, "cel outline colour as \"r g b a\" with components in 0-255" );
idCVar r_celViewWeaponOutlineWidth( "r_celViewWeaponOutlineWidth", "1.0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT, "first-person weapon cel outline width in pixels", CEL_MIN_OUTLINE_WIDTH, CEL_MAX_OUTLINE_WIDTH );
idCVar r_celViewWeaponOutlineAlpha( "r_celViewWeaponOutlineAlpha", "1.0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT, "opacity multiplier for first-person weapon cel outlines", 0.0f, 1.0f );
idCVar r_celShadingWorldWidth( "r_celShadingWorldWidth", "2.0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT, "screen-space radius in pixels used by world cel edge detection; larger values draw thicker outlines", CEL_MIN_WORLD_OUTLINE_WIDTH, CEL_MAX_WORLD_OUTLINE_WIDTH );
idCVar r_celShadingWorldAlpha( "r_celShadingWorldAlpha", "1.0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT, "opacity of world cel outline edges", 0.0f, 1.0f );
idCVar r_celShadingWorldDepthThreshold( "r_celShadingWorldDepthThreshold", "0.0015", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT, "world cel silhouette sensitivity as a fraction of view distance; lower values catch more subtle depth steps", 0.0001f, 0.02f );
idCVar r_celShadingWorldNormalThreshold( "r_celShadingWorldNormalThreshold", "0.4", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT, "world cel crease sensitivity for corners that share a depth; 0 disables interior crease lines", 0.0f, 1.0f );
idCVar r_celShadingWorldDebug( "r_celShadingWorldDebug", "0", CVAR_RENDERER | CVAR_BOOL, "draw the world cel edge mask over a flat background instead of compositing it" );
idCVar r_msaaResolveDepth( "r_msaaResolveDepth", "0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL, "resolve depth when blitting MSAA render targets" );
idCVar r_msaaAlphaToCoverage( "r_msaaAlphaToCoverage", "1", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL, "enable alpha-to-coverage for perforated materials on MSAA render targets" );
idCVar r_mode( "r_mode", "-2", CVAR_ARCHIVE | CVAR_RENDERER | CVAR_INTEGER, "video mode number (-2 = desktop native, -1 = custom, 0+ = predefined)" );
idCVar r_displayRefresh( "r_displayRefresh", "0", CVAR_RENDERER | CVAR_INTEGER | CVAR_NOCHEAT, "optional display refresh rate option for vid mode", 0.0f, 1000.0f );
idCVar r_fullscreen( "r_fullscreen", "1", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL, "0 = windowed, 1 = full screen" );
idCVar r_hiddenWindow( "r_hiddenWindow", "0", CVAR_RENDERER | CVAR_BOOL, "create a hidden OpenGL window for batch renderer jobs" );
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
idCVar r_useTrueTypeFonts( "r_useTrueTypeFonts", "1", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL, "render GUI text from the shipped .ttf faces, rasterised at the display's own resolution, instead of scaling up the fixed 12/24/48 point bitmap atlases; ignored for languages the 256-glyph atlases cannot draw at all, such as Russian" );
idCVar r_ttfFontResolution( "r_ttfFontResolution", "1.0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT, "multiplier on the resolution the TrueType glyph atlases are rasterised at; raise for sharper text at the cost of atlas memory", 0.25f, 4.0f );
idCVar r_ttfFontDebug( "r_ttfFontDebug", "0", CVAR_RENDERER | CVAR_BOOL, "dump each TrueType glyph atlas to fs_savepath/ttfatlas and log its layout" );
idCVar r_useShadowMap( "r_useShadowMap", "0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL, "use a simple shadow-map path for projected and point lights when supported" );
idCVar r_shadowMapCSM( "r_shadowMapCSM", "0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL, "use projected-light cascaded shadow maps when shadow maps are enabled" );
idCVar r_shadowMapHashedAlpha( "r_shadowMapHashedAlpha", "1", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL, "use hashed alpha testing for perforated shadow-map casters when supported" );
idCVar r_shadowMapTranslucentMoments( "r_shadowMapTranslucentMoments", "0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL, "accumulate experimental translucent shadow moments for blended casters" );
idCVar r_shadowMapTranslucentDensity( "r_shadowMapTranslucentDensity", "1.0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT, "density scale applied when resolving translucent shadow moments", 0.0f, 8.0f );
idCVar r_shadowMapTranslucentMinAlpha( "r_shadowMapTranslucentMinAlpha", "0.02", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT, "minimum per-stage alpha considered by translucent shadow moments", 0.0f, 1.0f );
idCVar r_shadowMapCustomGLSLReceiverWrapper( "r_shadowMapCustomGLSLReceiverWrapper", "1", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL, "allow compatible custom GLSL lighting materials to use stock shadow-map receiver interactions" );
idCVar r_shadowMapReport( "r_shadowMapReport", "0", CVAR_RENDERER | CVAR_INTEGER, "shadow-map diagnostics: 0 = off, 1 = per-view summary, 2 = per-light joined planner/ARB2/modern decisions, 3 = verbose receiver-submit decisions", 0, 3, idCmdSystem::ArgCompletion_Integer<0,3> );
idCVar r_shadowMapReportInterval( "r_shadowMapReportInterval", "30", CVAR_RENDERER | CVAR_INTEGER, "frames between shadow-map diagnostic reports when r_shadowMapReport is enabled", 1, 3600 );
idCVar r_shadowMapModernStrict( "r_shadowMapModernStrict", "0", CVAR_RENDERER | CVAR_BOOL, "fail-visible modern shadow contract: lights whose descriptor promises shadows but cannot deliver renders black instead of silently unshadowed" );
idCVar r_shadowMapTranslucentReceivers( "r_shadowMapTranslucentReceivers", "1", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL, "translucent surfaces sample the shadow map like opaque receivers (parity with r_stencilTranslucentShadows)" );
idCVar r_shadowMapSubviewPolicy( "r_shadowMapSubviewPolicy", "1", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_INTEGER, "shadow maps in mirror/remote subviews: 0 = render like main views, 1 = prefer cache then stencil, 2 = prefer stencil; required maps override when stencil cannot preserve the shadow", 0, 2, idCmdSystem::ArgCompletion_Integer<0,2> );
idCVar r_shadowMapConservativeCasters( "r_shadowMapConservativeCasters", "1", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL, "submit shadow-map casters conservatively even when their receiver interaction scissor is not visible" );
idCVar r_shadowMapProjectedCSM( "r_shadowMapProjectedCSM", "1", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL, "allow ordinary projected lights to use cascades when r_shadowMapCSM is enabled" );
idCVar r_shadowMapDepthCompare( "r_shadowMapDepthCompare", "1", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL, "use hardware comparison sampling for projected depth shadow maps when supported; PCSS-lite (r_shadowMapFilterMode 2) implies the manual path" );
idCVar r_shadowMapTexelBiasScale( "r_shadowMapTexelBiasScale", "0.45", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT, "texel-aware receiver bias scale for projected and point shadow maps", 0.0f, 8.0f );
idCVar r_shadowMapNormalOffsetScale( "r_shadowMapNormalOffsetScale", "1.0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT, "normal-offset shadow bias in shadow texels; reduces slope acne but excessive values can move contact edges (point-light use is bounded by r_shadowMapPointMaxWorldBias)", 0.0f, 8.0f );
idCVar r_shadowMapCasterCulling( "r_shadowMapCasterCulling", "2", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_INTEGER, "shadow caster face culling: 0 = always two-sided, 1 = force material-oriented near faces, 2 = automatic (near faces for sealed hulls outside the light, two-sided for open/uncertain/enclosing geometry)", 0, 2, idCmdSystem::ArgCompletion_Integer<0,2> );
idCVar r_shadowMapReceiverPlaneBias( "r_shadowMapReceiverPlaneBias", "0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL, "enable the experimental derivative receiver-plane depth-bias approximation for projected shadow filters" );
idCVar r_shadowMapFilterTaps( "r_shadowMapFilterTaps", "9", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_INTEGER, "projected-light PCF tap budget: 1, 5, 9, or 13", 1, 13, idCmdSystem::ArgCompletion_Integer<1,13> );
idCVar r_shadowMapPointFilterTaps( "r_shadowMapPointFilterTaps", "9", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_INTEGER, "point-light PCF tap budget: 1, 5, 9, or 13", 1, 13, idCmdSystem::ArgCompletion_Integer<1,13> );
idCVar r_shadowMapFilterMode( "r_shadowMapFilterMode", "0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_INTEGER, "projected-light shadow filter mode: 0 = fixed PCF, 1 = stable rotated Poisson, 2 = PCSS-lite when raw depth is available", 0, 2, idCmdSystem::ArgCompletion_Integer<0,2> );
idCVar r_shadowMapPointFilterMode( "r_shadowMapPointFilterMode", "0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_INTEGER, "point-light shadow filter mode: 0 = fixed PCF, 1 = stable rotated Poisson", 0, 1, idCmdSystem::ArgCompletion_Integer<0,1> );
idCVar r_shadowMapDistantFilterScale( "r_shadowMapDistantFilterScale", "0.35", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT, "scale projected PCF and PCSS radii for parallel/global sky lights whose shadow texels cover more world space", 0.0f, 1.0f );
idCVar r_shadowMapPCSSLightRadius( "r_shadowMapPCSSLightRadius", "4.0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT, "projected PCSS-lite blocker search radius in shadow texels", 0.0f, 16.0f );
idCVar r_shadowMapPCSSMaxRadius( "r_shadowMapPCSSMaxRadius", "8.0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT, "projected PCSS-lite maximum filter radius in shadow texels", 0.0f, 16.0f );
idCVar r_shadowMapPointHighPrecision( "r_shadowMapPointHighPrecision", "0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL, "store point-light shadow depth in an fp16 color cubemap instead of packed RGBA8 depth. The packed path quantizes finer (1.5e-5 vs 4.9e-4 near the far envelope) at half the memory, and the default hardware-compare path samples the depth cubemap directly, so this only affects the manual fallback. OpenGL only: the Vulkan backend always stores native depth, which quantizes finer than either colour format" );
idCVar r_shadowMapPointLights( "r_shadowMapPointLights", "1", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL, "shadow-map point lights when r_useShadowMap is enabled; disabled falls back to stencil shadows for point lights. Point lights are the dominant light class in Quake 4 content, so disabling this makes r_useShadowMap nearly a no-op" );
idCVar r_shadowMapPointSize( "r_shadowMapPointSize", "512", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_INTEGER, "point-light cube face resolution; separate from r_shadowMapSize because a cube costs six faces of color+depth storage per cached light", 128, 2048, idCmdSystem::ArgCompletion_Integer<128,2048> );
idCVar r_shadowMapAtlasSize( "r_shadowMapAtlasSize", "4096", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_INTEGER, "edge size of the shared projected-light shadow atlas; cached lights occupy r_shadowMapSize-sized cells inside it (CSM lights use a 2x2 cell block)", 2048, 8192, idCmdSystem::ArgCompletion_Integer<2048,8192> );
idCVar r_shadowMapPointDepthCompare( "r_shadowMapPointDepthCompare", "1", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL, "use hardware comparison sampling for point-light depth cubemaps when GLSL 1.30 support is available" );
idCVar r_shadowMapStableAlphaHash( "r_shadowMapStableAlphaHash", "1", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL, "seed shadow-map hashed alpha from stable world coordinates instead of screen-space fragments" );
idCVar r_shadowMapMaxUpdatesPerView( "r_shadowMapMaxUpdatesPerView", "0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_INTEGER, "maximum shadow-map passes rendered per backend view, 0 = unlimited", 0, 1024, idCmdSystem::ArgCompletion_Integer<0,1024> );
idCVar r_shadowMapSkipStencilShadows( "r_shadowMapSkipStencilShadows", "1", CVAR_RENDERER | CVAR_BOOL, "skip stencil shadow volume generation and linking for lights that will render shadow maps; a light that fails a shadow-map pass restores its volumes on the next frame" );
idCVar r_shadowMapStaticCache( "r_shadowMapStaticCache", "1", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL, "reuse resident shadow maps for static-only lights instead of rerendering every backend view" );
idCVar r_shadowMapStaticHysteresisFrames( "r_shadowMapStaticHysteresisFrames", "2", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_INTEGER, "frames to wait after dynamic casters disappear before static shadow-map reuse", 0, 120, idCmdSystem::ArgCompletion_Integer<0,120> );
idCVar r_shadowMapResidentFrames( "r_shadowMapResidentFrames", "120", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_INTEGER, "frames a resident static shadow map may remain unused before its cache slot can be expired", 1, 3600, idCmdSystem::ArgCompletion_Integer<1,3600> );
idCVar r_shadowMapProjectedCacheSize( "r_shadowMapProjectedCacheSize", "8", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_INTEGER, "static projected-light shadow-map cache slots", 0, 16, idCmdSystem::ArgCompletion_Integer<0,16> );
idCVar r_shadowMapPointCacheSize( "r_shadowMapPointCacheSize", "12", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_INTEGER, "static point-light cubemap cache slots", 0, 16, idCmdSystem::ArgCompletion_Integer<0,16> );
idCVar r_shadowMapCacheCSM( "r_shadowMapCacheCSM", "0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL, "allow static-cache reuse for view-fitted CSM/global shadow-map passes" );
idCVar r_shadowMapTranslucentFilterRadius( "r_shadowMapTranslucentFilterRadius", "-1.0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT, "translucent shadow moment filter radius in texels, -1 = inherit opaque shadow radius", -1.0f, 8.0f );
idCVar r_shadowMapTranslucentMinVariance( "r_shadowMapTranslucentMinVariance", "0.00001", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT, "minimum variance used when resolving translucent shadow moments", 0.000001f, 0.01f );
idCVar r_shadowMapTranslucentBleedReduction( "r_shadowMapTranslucentBleedReduction", "0.0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT, "light-bleed reduction applied to translucent shadow moment resolve", 0.0f, 0.95f );
idCVar r_shadowMapGpuSyncTimings( "r_shadowMapGpuSyncTimings", "0", CVAR_RENDERER | CVAR_BOOL, "diagnostic-only: glFinish around shadow-map passes to report GPU-synchronized milliseconds" );
idCVar r_shadowMapGpuTimerQueries( "r_shadowMapGpuTimerQueries", "1", CVAR_RENDERER | CVAR_BOOL, "use non-blocking GL timer queries for shadow-map GPU diagnostics when available" );
idCVar r_softParticles( "r_softParticles", "1", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL, "depth-fade eligible BSE particles against opaque scene depth when GLSL is available" );
idCVar r_softParticleFadeDistance( "r_softParticleFadeDistance", "64", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT, "world-unit distance over which r_softParticles fades particle intersections", 1.0f, 512.0f );
idCVar r_enhancedMaterials( "r_enhancedMaterials", "0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL, "use enhanced GLSL interaction shading for existing materials when supported" );
idCVar r_enhancedMaterialNormalScale( "r_enhancedMaterialNormalScale", "1.25", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT, "tangent-space normal XY scale when enhanced material shading is enabled", 0.5f, 2.0f );
idCVar r_enhancedMaterialSpecularBoost( "r_enhancedMaterialSpecularBoost", "1.15", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT, "specular intensity scale when enhanced material shading is enabled", 0.0f, 4.0f );
idCVar r_enhancedMaterialFresnel( "r_enhancedMaterialFresnel", "0.65", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT, "grazing-angle fresnel contribution when enhanced material shading is enabled", 0.0f, 1.0f );
idCVar r_useShadowSurfaceScissor( "r_useShadowSurfaceScissor", "0", CVAR_RENDERER | CVAR_BOOL, "scissor shadows by the scissor rect of the interaction surfaces" );
idCVar r_useInteractionTable( "r_useInteractionTable", "1", CVAR_RENDERER | CVAR_BOOL, "create a full entityDefs * lightDefs table to make finding interactions faster" );
idCVar r_useTurboShadow( "r_useTurboShadow", "1", CVAR_RENDERER | CVAR_BOOL, "use the infinite projection with W technique for dynamic shadows" );
idCVar r_useTwoSidedStencil( "r_useTwoSidedStencil", "1", CVAR_RENDERER | CVAR_BOOL, "do stencil shadows in one pass with different ops on each side" );
idCVar r_stencilTranslucentShadows( "r_stencilTranslucentShadows", "1", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL, "let translucent materials cast and receive stencil shadows (requires interaction rebuild when toggled)" );
idCVar r_useDeferredTangents( "r_useDeferredTangents", "1", CVAR_RENDERER | CVAR_BOOL, "defer tangents calculations after deform" );
idCVar r_useCachedDynamicModels( "r_useCachedDynamicModels", "1", CVAR_RENDERER | CVAR_BOOL, "cache snapshots of dynamic models" );
idCVar r_useRepeatedStateReuse( "r_useRepeatedStateReuse", "1", CVAR_RENDERER | CVAR_BOOL, "keep model-space dynamic snapshots across transform-only entity updates so repeated-state presentation frames skip CPU re-skinning" );
idCVar r_useNewSkinning( "r_useNewSkinning", "1", CVAR_RENDERER | CVAR_BOOL, "use retail-style SIMD MD5 skinning data" );
idCVar r_gpuSkinning( "r_gpuSkinning", "0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL, "use exact four-weight GPU deformation for eligible animated surfaces" );
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
// not archived: the VBO-unavailable force-off in idVertexCache::Init must not
// persist into configs. Default 0 (client-memory indexes) matches the measured
// fast path on real content: index VBOs add a per-draw element-buffer bind
// across thousands of distinct per-surface buffers (never elided) plus lazy
// alloc bursts while traversing, and mode 2 additionally re-uploads static
// buffers every frame for regenerated geometry (heavy pacing spikes on
// effect/character scenes). 1 (static geometry only) and 2 (everything) are
// retained for per-machine A/B benchmarking.
idCVar r_useIndexBuffers( "r_useIndexBuffers", "0", CVAR_RENDERER | CVAR_INTEGER, "use ARB_vertex_buffer_object for indexes: 0 = never, 1 = static geometry only, 2 = all geometry", 0, 2, idCmdSystem::ArgCompletion_Integer<0,2>  );
idCVar r_useSmp( "r_useSmp", "0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL, "use SMP rendering" );

idCVar r_useStateCaching( "r_useStateCaching", "1", CVAR_RENDERER | CVAR_BOOL, "avoid redundant state changes in GL_*() calls" );
idCVar r_useRedundantStateFiltering( "r_useRedundantStateFiltering", "1", CVAR_RENDERER | CVAR_BOOL, "skip redundant legacy-backend GL calls: repeated program env parameters, vertex attrib array toggles, and vertex/index buffer rebinds" );
idCVar r_useInfiniteFarZ( "r_useInfiniteFarZ", "1", CVAR_RENDERER | CVAR_BOOL, "use the no-far-clip-plane trick" );

// Quake 4 gameplay code assumes 3.0f as the normal baseline outside cinematics.
// Keeping the default too low significantly reduces distant depth precision and
// causes polygon-offset surfaces (decals/overlays) to z-fight at range.
idCVar r_znear( "r_znear", "3.0", CVAR_RENDERER | CVAR_FLOAT, "near Z clip plane distance", 0.001f, 200.0f );
idCVar cl_gunfov( "cl_gunfov", "0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT | CVAR_NOCHEAT, "first-person weapon FOV override (0 = follow current view FOV)", 0.0f, 179.0f );
idCVar cl_gunfov_adjust( "cl_gunfov_adjust", "1", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL | CVAR_NOCHEAT, "when cl_gunfov is set, keep weapon FOV aspect-correct across screen ratios" );

idCVar r_ignoreGLErrors( "r_ignoreGLErrors", "1", CVAR_RENDERER | CVAR_BOOL, "ignore GL errors" );
idCVar r_finish( "r_finish", "0", CVAR_RENDERER | CVAR_BOOL, "force a call to glFinish() every frame" );
idCVar r_swapInterval( "r_swapInterval", "0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_INTEGER, "controls the platform swap interval / VSync state" );
idCVar r_disableVSyncDuringLevelLoad( "r_disableVSyncDuringLevelLoad", "1", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL, "temporarily disable VSync for loading-screen presents during blocking level loads" );

static bool r_loadingScreenSwapIntervalBypass = false;

static int R_GetEffectiveSwapIntervalForState( bool loadingScreenBypassActive ) {
	const int configuredInterval = r_swapInterval.GetInteger();
	if ( loadingScreenBypassActive && r_disableVSyncDuringLevelLoad.GetBool() && configuredInterval != 0 ) {
		return 0;
	}
	return configuredInterval;
}

void R_SetLoadingScreenSwapIntervalBypass( bool active ) {
	const int oldInterval = R_GetEffectiveSwapIntervalForState( r_loadingScreenSwapIntervalBypass );
	r_loadingScreenSwapIntervalBypass = active;
	const int newInterval = R_GetEffectiveSwapIntervalForState( r_loadingScreenSwapIntervalBypass );
	if ( oldInterval != newInterval ) {
		r_swapInterval.SetModified();
	}
}

int R_GetEffectiveSwapInterval( void ) {
	return R_GetEffectiveSwapIntervalForState( r_loadingScreenSwapIntervalBypass );
}

idCVar r_gamma( "r_gamma", "1", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT, "changes gamma tables", 0.5f, 3.0f );
idCVar r_brightness( "r_brightness", "1", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT, "changes gamma tables", 0.5f, 2.0f );

idCVar r_renderer( "r_renderer", "best", CVAR_RENDERER | CVAR_ARCHIVE, "hardware specific renderer path to use", r_rendererArgs, idCmdSystem::ArgCompletion_String<r_rendererArgs> );
idCVar r_actualRenderer( "r_actualRenderer", "UNINITIALIZED", CVAR_RENDERER | CVAR_ROM, "actual active renderer backend after request/fallback selection" );
idCVar r_glTier( "r_glTier", "auto", CVAR_RENDERER | CVAR_ARCHIVE, "OpenGL renderer tier: auto, legacy, gl33, gl41, gl43, gl45, gl46", r_glTierArgs, idCmdSystem::ArgCompletion_String<r_glTierArgs> );
idCVar r_glesContext( "r_glesContext", "0", CVAR_RENDERER | CVAR_BOOL | CVAR_ARCHIVE, "request an OpenGL ES 3.0 context instead of desktop GL; bring-up aid for the Android GLES backend, on desktop this needs an EGL/GLES translation layer (ANGLE)" );
idCVar r_glCoreProfileFirst( "r_glCoreProfileFirst", "0", CVAR_RENDERER | CVAR_BOOL, "with a forced r_glTier, try core-profile contexts before the compatibility fallback; needed to reach the modern path on drivers that cap compatibility below 3.3 (macOS caps it at 2.1), diagnostic only" );
idCVar r_vkValidation( "r_vkValidation", "0", CVAR_RENDERER | CVAR_BOOL, "enable Vulkan validation layers for the Vulkan renderer module and rendererVkProbe" );
idCVar r_vkDevice( "r_vkDevice", "-1", CVAR_RENDERER | CVAR_INTEGER, "Vulkan physical-device index override, -1 = automatic selection", -1, 15 );
idCVar r_vkShadowFallbackTest( "r_vkShadowFallbackTest", "0", CVAR_RENDERER | CVAR_BOOL, "diagnostic: make Vulkan shadow maps and stencil ownership unavailable to exercise unshadowed receiver fallback" );
idCVar r_glDebugContext( "r_glDebugContext", "0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL, "request a debug OpenGL context when the platform backend supports it" );
idCVar r_glDebugOutput( "r_glDebugOutput", "1", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL, "report OpenGL driver debug messages when a debug context is active" );
idCVar r_glDebugSynchronous( "r_glDebugSynchronous", "0", CVAR_RENDERER | CVAR_BOOL, "deliver OpenGL debug callbacks synchronously (diagnostic; may reduce performance)" );
idCVar r_rendererMetrics( "r_rendererMetrics", "0", CVAR_RENDERER | CVAR_INTEGER, "renderer metrics: 0 = off, 1 = periodic summary, 2 = per-frame/pass detail", 0, 2, idCmdSystem::ArgCompletion_Integer<0,2> );
idCVar r_rendererGpuTimers( "r_rendererGpuTimers", "1", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL, "sample delayed whole-frame GPU timestamps on supported backends; OpenGL pass detail additionally requires r_rendererMetrics" );
idCVar r_rendererBenchmarkPreset( "r_rendererBenchmarkPreset", "baseline", CVAR_RENDERER | CVAR_ARCHIVE, "renderer benchmark budget preset: low, baseline, modern, high-end", r_rendererBenchmarkPresetArgs, idCmdSystem::ArgCompletion_String<r_rendererBenchmarkPresetArgs> );
idCVar r_rendererPerfThresholdP95( "r_rendererPerfThresholdP95", "0", CVAR_RENDERER | CVAR_INTEGER, "custom renderer benchmark P95 frame-time budget in milliseconds (0 = preset default)", 0, 1000, idCmdSystem::ArgCompletion_Integer<0,1000> );
idCVar r_rendererPerfThresholdP99( "r_rendererPerfThresholdP99", "0", CVAR_RENDERER | CVAR_INTEGER, "custom renderer benchmark P99 frame-time budget in milliseconds (0 = preset default)", 0, 1000, idCmdSystem::ArgCompletion_Integer<0,1000> );
idCVar r_rendererAdaptiveClusterGrid( "r_rendererAdaptiveClusterGrid", "0", CVAR_RENDERER | CVAR_BOOL, "use benchmark preset cluster-grid dimensions for the modern clustered-light experiment" );
idCVar r_rendererDynamicResolution( "r_rendererDynamicResolution", "0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL, "automatically scale the 3D scene from delayed GPU timing; disabled by default" );
idCVar r_rendererUploadMegs( "r_rendererUploadMegs", "16", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_INTEGER, "dynamic renderer upload stream size in megabytes per frame buffer", 1, 128, idCmdSystem::ArgCompletion_Integer<1,128> );
idCVar r_rendererUploadFrameBuffers( "r_rendererUploadFrameBuffers", "4", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_INTEGER, "dynamic renderer upload stream frame-buffer rotation depth", 3, 8, idCmdSystem::ArgCompletion_Integer<3,8> );
idCVar r_rendererUploadPersistent( "r_rendererUploadPersistent", "1", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL, "allow persistent-mapped dynamic renderer uploads when supported" );
idCVar r_rendererUploadBufferPool( "r_rendererUploadBufferPool", "1", CVAR_RENDERER | CVAR_BOOL, "recycle static GL buffer names instead of gen/data/delete churn for per-frame regenerated geometry" );
idCVar r_rendererModernQuality( "r_rendererModernQuality", "1", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL, "master permit for experimental Milestone-F PBR, probe, decal, volumetric, SSR, and SSGI domains; 0 rolls every domain back to classic ownership" );
idCVar r_rendererReflectionProbes( "r_rendererReflectionProbes", "0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL, "enable authored reflection probes for eligible modern PBR views; unsupported or incomplete probe sets use analytic/classic fallback" );
idCVar r_rendererClusteredDecals( "r_rendererClusteredDecals", "0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL, "transfer complete eligible decal subsets to bounded clustered forward rendering; incomplete views remain entirely classic" );
idCVar r_rendererModernExecutor( "r_rendererModernExecutor", "0", CVAR_RENDERER | CVAR_BOOL, "prepare the opt-in modern GL executor frame contract while legacy ARB2 still executes" );
idCVar r_rendererModernSubmit( "r_rendererModernSubmit", "0", CVAR_RENDERER | CVAR_BOOL, "execute opt-in modern GL draw submission before legacy ARB2 fallback; diagnostic until visible pass replacement lands" );
idCVar r_rendererGpuValidation( "r_rendererGpuValidation", "0", CVAR_RENDERER | CVAR_BOOL, "compare GL 4.3 GPU-driven compute results against CPU reference data on sampled frames" );
idCVar r_rendererGpuValidationReadbackDelay( "r_rendererGpuValidationReadbackDelay", "2", CVAR_RENDERER | CVAR_INTEGER, "frames to defer opt-in GL 4.3 GPU validation readbacks before polling without a sync wait", 1, 8, idCmdSystem::ArgCompletion_Integer<1,8> );
idCVar r_rendererBindless( "r_rendererBindless", "0", CVAR_RENDERER | CVAR_BOOL, "enable experimental GL 4.5 bindless texture diagnostics without changing visible output" );
idCVar r_rendererModernVisible( "r_rendererModernVisible", "0", CVAR_RENDERER | CVAR_BOOL, "execute the opt-in modern hybrid visible-frame composition when all required pass owners are modern-safe" );
idCVar r_rendererSharedGui( "r_rendererSharedGui", "0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL, "execute eligible fixed-function 2D GUI views from the backend-neutral ordered material-stage stream; any unsupported view uses the complete classic path" );
idCVar r_rendererSharedInWorldGui( "r_rendererSharedInWorldGui", "0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL, "execute eligible GUI geometry emitted onto 3D surfaces from a backend-neutral ordered material-stage stream; any unsupported view keeps every in-world GUI surface on the classic path" );
idCVar r_rendererSharedCinematicPost( "r_rendererSharedCinematicPost", "0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL, "execute eligible root cinematic playback and complete authored post-process ranges through a sealed shared transaction; post/video ranges nested in a shared special-view tree publish or roll back with its root; unsupported ranges use the complete classic path" );
idCVar r_rendererSharedSpecialFrame( "r_rendererSharedSpecialFrame", "0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL, "execute eligible render-demo playback and Raven special-effect controller frames through a sealed shared transaction; unsupported views and commands use the complete classic path" );
idCVar r_rendererSharedWorldAmbient( "r_rendererSharedWorldAmbient", "0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL, "execute every eligible classic world ambient/material surface in a complete 3D view from the backend-neutral ordered material-stage stream; any unsupported view uses the complete classic path" );
idCVar r_rendererSharedWorldInteraction( "r_rendererSharedWorldInteraction", "0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL, "execute every eligible unshadowed fixed-classic interaction light and receiver in a complete 3D view from a backend-neutral sealed stream; any unsupported view uses the complete classic path" );
idCVar r_rendererSharedWorldFogBlend( "r_rendererSharedWorldFogBlend", "0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL, "execute the complete classic fog/blend light phase in an eligible 3D view from a backend-neutral sealed stream; any unsupported view uses the complete classic path" );
idCVar r_rendererSharedSubview( "r_rendererSharedSubview", "0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL, "execute eligible direct mirror and capture-backed remote-camera, mirror, reflection, refraction, and x-ray subview transactions from backend-neutral sealed records; nested child trees commit atomically at their outermost special view, while 2D/cubemap color and depth captures retain exact type, aspect, and face; unsupported subviews use the complete classic path" );
idCVar r_rendererSharedDeform( "r_rendererSharedDeform", "0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL, "enable optional, default-off backend-neutral material-deform ownership; incomplete or unsupported work retains the complete classic path" );
idCVar r_rendererModernLightingParity( "r_rendererModernLightingParity", "0", CVAR_RENDERER | CVAR_INTEGER, "diagnostic override forcing modern lighting-ownership parity contracts proven for bring-up capture: 1 = interaction, 2 = fog/blend, 4 = light grid, 8 = shadow receivers; 0 keeps every unproven domain on the ARB2 bridge", 0, 15, idCmdSystem::ArgCompletion_Integer<0,15> );
idCVar r_rendererModernAutoPromote( "r_rendererModernAutoPromote", "0", CVAR_RENDERER | CVAR_BOOL, "allow r_glTier auto and r_renderer best to request the guarded modern visible path after promotion evidence and sign-off; off keeps ARB2 default" );
idCVar r_rendererPromotionEvidence( "r_rendererPromotionEvidence", "", CVAR_RENDERER, "Phase 8 validation evidence token required with r_rendererModernAutoPromote before automatic modern visible promotion" );
idCVar r_rendererShaderReload( "r_rendererShaderReload", "0", CVAR_RENDERER | CVAR_BOOL, "allow runtime reload of the internal modern GL shader library" );
idCVar r_rendererModernVisibleDepth( "r_rendererModernVisibleDepth", "0", CVAR_RENDERER | CVAR_BOOL, "execute graph-backed modern depth and compatible shadow-depth passes while ARB2 remains the visible color path" );
idCVar r_rendererModernDepthDebug( "r_rendererModernDepthDebug", "0", CVAR_RENDERER | CVAR_INTEGER, "modern depth debug overlay: 0 = off, 1 = scene depth, 2 = shadow-map depth", 0, 2, idCmdSystem::ArgCompletion_Integer<0,2> );
idCVar r_rendererModernOpaque( "r_rendererModernOpaque", "0", CVAR_RENDERER | CVAR_BOOL, "execute graph-backed modern opaque G-buffer passes while ARB2 remains the visible color path" );
idCVar r_rendererModernGBufferDebug( "r_rendererModernGBufferDebug", "0", CVAR_RENDERER | CVAR_INTEGER, "modern G-buffer debug overlay: 0 = off, 1 = albedo, 2 = normal, 3 = material, 4 = emissive/light-grid", 0, 4, idCmdSystem::ArgCompletion_Integer<0,4> );
idCVar r_rendererModernDeferred( "r_rendererModernDeferred", "0", CVAR_RENDERER | CVAR_BOOL, "execute graph-backed modern deferred light resolve while ARB2 remains the visible color path" );
idCVar r_rendererModernDeferredDebug( "r_rendererModernDeferredDebug", "0", CVAR_RENDERER | CVAR_INTEGER, "modern deferred resolve debug overlay: 0 = off, 1 = light contribution, 2 = cluster id, 3 = light count, 4 = fallback pressure", 0, 4, idCmdSystem::ArgCompletion_Integer<0,4> );
idCVar r_rendererForwardPlus( "r_rendererForwardPlus", "0", CVAR_RENDERER | CVAR_BOOL, "execute graph-backed modern clustered forward+ passes while ARB2 remains the visible color path" );
idCVar r_rendererClusterDebug( "r_rendererClusterDebug", "0", CVAR_RENDERER | CVAR_INTEGER, "modern clustered light debug overlay: 0 = off, 1 = occupancy, 2 = light count, 3 = overflow", 0, 3, idCmdSystem::ArgCompletion_Integer<0,3> );
idCVar r_rendererOcclusion( "r_rendererOcclusion", "1", CVAR_RENDERER | CVAR_BOOL, "enable conservative modern visibility and occlusion culling for modern renderer passes; 0 disables the culling path instantly for debugging" );
idCVar r_rendererHiZ( "r_rendererHiZ", "1", CVAR_RENDERER | CVAR_BOOL, "allocate and build the modern scene Hi-Z depth pyramid when visibility culling has a depth producer" );
idCVar r_useSimpleInteraction( "r_useSimpleInteraction", "0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL, "use Quake 4's simpler ARB interaction shader pair as an explicit compatibility fallback; may reduce material lighting quality" );
idCVar r_interactionColorMode( "r_interactionColorMode", "0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_INTEGER, "interaction vertex-color mode: 0 = auto, 1 = packed env16.xy, 2 = vector env16/env17", 0, 2, idCmdSystem::ArgCompletion_Integer<0,2> );
idCVar r_appleARB2Interactions( "r_appleARB2Interactions", "0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_INTEGER, "Apple GL 2.1 light-interaction handling: 0 = automatic stock GLSL interactions with simple ARB per-surface fallback, 1 = force simple ARB diagnostic, 2 = force full ARB diagnostic, 3 = emergency interaction bypass; requires vid_restart", 0, 3, idCmdSystem::ArgCompletion_Integer<0,3> );
idCVar r_shaderReport( "r_shaderReport", "0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_INTEGER, "shader diagnostics: 0 = off, 1 = startup/vid_restart summary, 2 = also warn on invalid program use", 0, 2, idCmdSystem::ArgCompletion_Integer<0,2> );
// Deliberately not archived: this exists so the Apple GL 2.1 corridor can be
// reproduced from a Windows or Linux checkout without an Apple machine. On a
// darwin host the corridor is selected by the context shape and this cvar is
// not consulted.
idCVar r_forceAppleGL21InteractionCorridor( "r_forceAppleGL21InteractionCorridor", "0", CVAR_RENDERER | CVAR_BOOL, "non-Apple hosts only: treat a GL 2.1 compatibility context as the Apple GL 2.1 interaction corridor for reproduction; requires vid_restart" );

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
idCVar r_lightGridIntensity( "r_lightGridIntensity", "1.0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT, "scales baked light-grid indirect diffuse contribution", 0.0f, 16.0f );
idCVar r_lightGridVisibilityFloor( "r_lightGridVisibilityFloor", "0.10", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT, "minimum baked light-grid probe visibility after visibility falloff", 0.0f, 1.0f );
idCVar r_lightGridIrradianceGamma( "r_lightGridIrradianceGamma", "1.0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT, "decodes baked LDR light-grid atlas samples before scene-referred blending", 0.25f, 4.0f );
idCVar r_lightGridMaxContribution( "r_lightGridMaxContribution", "1.0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT, "maximum per-channel baked light-grid contribution before bloom/HDR processing; 0 disables the cap", 0.0f, 16.0f );
idCVar r_lightGridReport( "r_lightGridReport", "0", CVAR_RENDERER | CVAR_INTEGER, "print light-grid receiver statistics every N frames while enabled", 0, 600 );
idCVar r_lightGridDebug( "r_lightGridDebug", "0", CVAR_RENDERER | CVAR_INTEGER, "debug baked light-grid indirect pass output: 0 normal, 1 receiver coverage, 2 irradiance, 3 coverage without depth, 4 albedo diagnostic, 5 final contribution, 6 manual depth accept, 7 sampled scene depth", 0, 7 );
idCVar r_lightGridDepthBiasFactor( "r_lightGridDepthBiasFactor", "0", CVAR_RENDERER | CVAR_FLOAT, "fallback polygon offset factor for depth-tested baked light-grid overlay receivers", -64.0f, 64.0f );
idCVar r_lightGridDepthBiasUnits( "r_lightGridDepthBiasUnits", "0", CVAR_RENDERER | CVAR_FLOAT, "fallback polygon offset units for depth-tested baked light-grid overlay receivers", -4096.0f, 4096.0f );
idCVar r_lightGridDepthTolerance( "r_lightGridDepthTolerance", "0.005", CVAR_RENDERER | CVAR_FLOAT, "normalized depth tolerance for clipping baked light-grid receivers against captured scene depth", 0.0f, 0.1f );
idCVar r_lightGridPortalBlend( "r_lightGridPortalBlend", "64", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT, "world-unit radius for blending indirect light grids across visible portal boundaries; 0 disables", 0.0f, 256.0f );
idCVar r_lightGridPreload( "r_lightGridPreload", "0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL, "preload every light-grid atlas while loading a map; 0 streams visible areas on demand, 1 avoids area-transition uploads at the cost of longer map loads and more VRAM" );
idCVar r_lightGridResidencyFrames( "r_lightGridResidencyFrames", "0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_INTEGER, "frames to keep light-grid atlases resident after visible/neighbor use; 0 disables runtime purging", 0, 3600, idCmdSystem::ArgCompletion_Integer<0,3600> );
idCVar r_lightGridBakeWorkers( "r_lightGridBakeWorkers", "0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_INTEGER, "worker threads for light-grid probe integration (-1 = disabled, 0 = auto, 1..8 = explicit)", -1, 8, idCmdSystem::ArgCompletion_Integer<-1,8> );
idCVar r_lightGridBakeAsyncReadback( "r_lightGridBakeAsyncReadback", "1", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL, "use async pixel-pack-buffer readback for light-grid baking when supported" );
idCVar r_lightGridBakeMemoryMB( "r_lightGridBakeMemoryMB", "12", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_INTEGER, "transient memory budget in MB for in-flight light-grid bake jobs (lower reduces RAM/pagefile use)", 4, 256, idCmdSystem::ArgCompletion_Integer<4,256> );
idCVar r_lightGridBakeReadbackSlots( "r_lightGridBakeReadbackSlots", "0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_INTEGER, "async readback slots for light-grid baking (0 = auto, 1..16 = explicit; lower reduces driver/GPU memory use)", 0, 16, idCmdSystem::ArgCompletion_Integer<0,16> );
idCVar r_skipBlendLights( "r_skipBlendLights", "0", CVAR_RENDERER | CVAR_BOOL, "skip all blend lights" );
idCVar r_skipFogLights( "r_skipFogLights", "0", CVAR_RENDERER | CVAR_BOOL, "skip all fog lights" );
idCVar r_skipPlayerVisibilityEffects( "r_skipPlayerVisibilityEffects", "0", CVAR_RENDERER | CVAR_BOOL, "skip the multiplayer player brightskin / rimlight / outline overlays" );
idCVar r_playerRimlightPower( "r_playerRimlightPower", "2.0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT, "multiplayer player rimlight falloff exponent; higher tightens the band to the silhouette, lower spreads it across the body", RB_PLAYER_RIMLIGHT_MIN_POWER, RB_PLAYER_RIMLIGHT_MAX_POWER );
idCVar r_playerRimlightFloor( "r_playerRimlightFloor", "0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT, "multiplayer player rimlight floor; lifts the whole body by this much of the rim strength so a player facing the camera is still tinted", RB_PLAYER_RIMLIGHT_MIN_FLOOR, RB_PLAYER_RIMLIGHT_MAX_FLOOR );
idCVar r_skipDeforms( "r_skipDeforms", "0", CVAR_RENDERER | CVAR_BOOL, "leave all deform materials in their original state" );
idCVar r_skipFrontEnd( "r_skipFrontEnd", "0", CVAR_RENDERER | CVAR_BOOL, "bypasses all front end work, but 2D gui rendering still draws" );
idCVar r_skipUpdates( "r_skipUpdates", "0", CVAR_RENDERER | CVAR_BOOL, "1 = don't accept any entity or light updates, making everything static" );
idCVar r_skipEntities( "r_skipEntities", "0", CVAR_RENDERER | CVAR_BOOL, "skip non-world render entities for renderer diagnostics" );
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
idCVar r_shadowMapBias( "r_shadowMapBias", "0.00016", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT, "constant receiver depth bias for projected shadow maps", 0.0f, 0.05f );
idCVar r_shadowMapNormalBias( "r_shadowMapNormalBias", "0.00075", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT, "geometric normal bias added on sloped projected-light receivers", 0.0f, 0.05f );
idCVar r_shadowMapPointBias( "r_shadowMapPointBias", "0.00010", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT, "constant receiver depth bias for point-light shadow maps", 0.0f, 0.05f );
idCVar r_shadowMapPointNormalBias( "r_shadowMapPointNormalBias", "0.0010", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT, "geometric normal bias added on sloped point-light receivers", 0.0f, 0.05f );
idCVar r_shadowMapPointMaxWorldBias( "r_shadowMapPointMaxWorldBias", "4.0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT, "maximum combined point-shadow receiver depth bias and normal offset at the padded far envelope, in world units; 0 disables the cap", 0.0f, 64.0f );
idCVar r_shadowMapFilterRadius( "r_shadowMapFilterRadius", "0.75", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT, "projected-light PCF radius in texels for the simple shadow-map path", 0.0f, 8.0f );
idCVar r_shadowMapPointFilterRadius( "r_shadowMapPointFilterRadius", "1.0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT, "point-light PCF radius in texels for the simple shadow-map path", 0.0f, 8.0f );
idCVar r_shadowMapProjectionPad( "r_shadowMapProjectionPad", "0.15", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT, "normalized padding applied around projected-light shadow-map coverage", 0.0f, 1.0f );
idCVar r_shadowMapCascadeCount( "r_shadowMapCascadeCount", "4", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_INTEGER, "number of projected-light cascades when r_shadowMapCSM is enabled", 1, 4, idCmdSystem::ArgCompletion_Integer<1,4> );
idCVar r_shadowMapCascadeDistance( "r_shadowMapCascadeDistance", "1536", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT, "camera-space distance covered by the cropped projected-light cascades", 64.0f, 8192.0f );
idCVar r_shadowMapCascadeLambda( "r_shadowMapCascadeLambda", "0.75", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT, "blend factor between uniform and logarithmic projected-light cascade splits", 0.0f, 1.0f );
idCVar r_shadowMapCascadeBlend( "r_shadowMapCascadeBlend", "0.15", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT, "fraction of each projected-light cascade range used to blend into the next cascade", 0.0f, 0.5f );
idCVar r_shadowMapDebugOverlay( "r_shadowMapDebugOverlay", "0", CVAR_RENDERER | CVAR_INTEGER,
	"shadow-map overlay: 0 = off, 1 = show the selected shadow map as a top-left mini-map with stats",
	0, 1, idCmdSystem::ArgCompletion_Integer<0, 1> );
idCVar r_shadowMapDebugMode( "r_shadowMapDebugMode", "0", CVAR_RENDERER | CVAR_INTEGER,
	"projected shadow-map debug mode: 0 = off, 1 = atlas UV/depth, 2 = cascade index, 3 = projected UV, 4 = projected depth, 5 = projected w, 6 = invalid mask, 7 = bias heatmap, 8 = bias off, 9 = PCF off, 10 = caster offset off, 11 = receiver-plane bias off, 12 = compare-depth delta, 13 = receiver eligibility, 14 = receiver fallback reason",
	0, SHADOWMAP_DEBUGMODE_COUNT - 1, idCmdSystem::ArgCompletion_Integer<0, SHADOWMAP_DEBUGMODE_COUNT - 1> );
idCVar r_shadowMapCascadeStabilize( "r_shadowMapCascadeStabilize", "1", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL, "snap projected-light cascade bounds to texels to reduce shimmering" );
idCVar r_shadowMapPointFarScale( "r_shadowMapPointFarScale", "1.25", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT, "padding multiplier applied to point-light shadow-map range", 1.0f, 4.0f );
idCVar r_shadowMapPolygonFactor( "r_shadowMapPolygonFactor", "0.25", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT, "slope-scale depth bias used when rendering shadow-map casters", 0.0f, 16.0f );
idCVar r_shadowMapPolygonOffset( "r_shadowMapPolygonOffset", "0.5", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT, "constant depth bias used when rendering shadow-map casters", 0.0f, 64.0f );
static idCVar r_shadowMapContactQualityMigrated( "r_shadowMapContactQualityMigrated", "0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL, "one-time migration flag for legacy broad shadow filtering defaults" );
idCVar r_frontBuffer( "r_frontBuffer", "0", CVAR_RENDERER | CVAR_BOOL, "draw to front buffer for debugging" );
idCVar r_skipSubviews( "r_skipSubviews", "0", CVAR_RENDERER | CVAR_INTEGER, "1 = don't render any gui elements on surfaces" );
idCVar r_skipParticles( "r_skipParticles", "0", CVAR_RENDERER | CVAR_INTEGER, "1 = skip all particle systems", 0, 1, idCmdSystem::ArgCompletion_Integer<0,1> );
idCVar r_subviewOnly( "r_subviewOnly", "0", CVAR_RENDERER | CVAR_BOOL, "1 = don't render main view, allowing subviews to be debugged" );
idCVar r_shadows( "r_shadows", "1", CVAR_RENDERER | CVAR_BOOL  | CVAR_ARCHIVE, "enable shadows" );
idCVar r_testARBProgram( "r_testARBProgram", "0", CVAR_RENDERER | CVAR_BOOL, "experiment with vertex/fragment programs" );
idCVar r_testGamma( "r_testGamma", "0", CVAR_RENDERER | CVAR_FLOAT, "if > 0 draw a grid pattern to test gamma levels", 0, 195 );
idCVar r_testGammaBias( "r_testGammaBias", "0", CVAR_RENDERER | CVAR_FLOAT, "if > 0 draw a grid pattern to test gamma levels" );
idCVar r_testStepGamma( "r_testStepGamma", "0", CVAR_RENDERER | CVAR_FLOAT, "if > 0 draw a grid pattern to test gamma levels" );
idCVar r_lightScale( "r_lightScale", "2", CVAR_RENDERER | CVAR_FLOAT, "all light intensities are multiplied by this" );
idCVar r_lightDetailLevel( "r_lightDetailLevel", "0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT, "minimum light detail level to render; 0 keeps all authored lights active", 0.0f, 10.0f );
idCVar r_lightSourceRadius( "r_lightSourceRadius", "0", CVAR_RENDERER | CVAR_FLOAT, "for soft-shadow sampling" );
idCVar r_flareSize( "r_flareSize", "1", CVAR_RENDERER | CVAR_FLOAT, "scale the flare deforms from the material def" ); 

idCVar r_useExternalShadows( "r_useExternalShadows", "1", CVAR_RENDERER | CVAR_INTEGER, "1 = skip drawing caps when outside the light volume, 2 = force to no caps for testing", 0, 2, idCmdSystem::ArgCompletion_Integer<0,2> );
idCVar r_useOptimizedShadows( "r_useOptimizedShadows", "1", CVAR_RENDERER | CVAR_BOOL, "use the dmap generated static shadow volumes" );
idCVar r_useScissor( "r_useScissor", "1", CVAR_RENDERER | CVAR_BOOL, "scissor clip as portals and lights are processed" );
idCVar r_useCombinerDisplayLists( "r_useCombinerDisplayLists", "1", CVAR_RENDERER | CVAR_BOOL | CVAR_NOCHEAT, "put all nvidia register combiner programming in display lists" );
idCVar r_useDepthBoundsTest( "r_useDepthBoundsTest", "1", CVAR_RENDERER | CVAR_BOOL, "use depth bounds test to reduce shadow fill" );

idCVar r_screenFraction( "r_screenFraction", "100", CVAR_ARCHIVE | CVAR_RENDERER | CVAR_INTEGER, "main-scene resolution scale percentage; values above 100 enable offscreen supersampling", 10, 200 );
idCVar r_resolutionScaleMode( "r_resolutionScaleMode", "1", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_INTEGER, "screen-fraction mode below native resolution: 0 = legacy cropped viewport, 1 = bilinear upscale, 2 = high-quality upscale, 3 = nearest-neighbour upscale (sharp, no blur). On OpenGL ES 1 and 2 crop the world render and blit it back out bilinear and 3 does the same with a point filter, all three leaving the HUD and menus at native resolution; supersampling uses the scene-target resolve path", 0, 3, idCmdSystem::ArgCompletion_Integer<0,3> );
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
idCVar r_reportSilhouetteEdgeWarnings( "r_reportSilhouetteEdgeWarnings", "0", CVAR_RENDERER | CVAR_BOOL, "print developer warnings for duplicate/tripled silhouette edges while building triangle surfaces" );
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
idCVar r_showViewBuildTimes( "r_showViewBuildTimes", "0", CVAR_RENDERER | CVAR_INTEGER, "print CPU timings for render-view construction: 1 = primary views, 2 = all views", 0, 2, idCmdSystem::ArgCompletion_Integer<0,2> );
idCVar r_showViewBuildTimesInterval( "r_showViewBuildTimesInterval", "60", CVAR_RENDERER | CVAR_INTEGER, "frames between r_showViewBuildTimes reports", 1, 3600 );
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

static void R_RendererTierContractSelfTest_f( const idCmdArgs &args ) {
	(void)args;
	if ( !RendererTierContract_RunSelfTest() ) {
		common->Warning( "Renderer tier contract self-test failed" );
	}
}

static void R_RendererContextLadderSelfTest_f( const idCmdArgs &args ) {
	(void)args;
	if ( !RendererContextLadder_RunSelfTest() ) {
		common->Warning( "Renderer context ladder self-test failed" );
	}
}

static void R_RendererCompatibilityGatesSelfTest_f( const idCmdArgs &args ) {
	(void)args;
	if ( !RendererCompatibilityGates_RunSelfTest() ) {
		common->Warning( "Renderer compatibility gates self-test failed" );
	}
}

static void R_RendererBenchmarkSelfTest_f( const idCmdArgs &args ) {
	(void)args;
	if ( !RendererBenchmark_RunSelfTest() ) {
		common->Warning( "Renderer benchmark self-test failed" );
	}
}

static void R_RendererDefaultPromotionSelfTest_f( const idCmdArgs &args ) {
	(void)args;
	if ( !RendererDefaultPromotion_RunSelfTest() ) {
		common->Warning( "Renderer default promotion self-test failed" );
	}
}

static void R_RendererDefaultSafetySelfTest_f( const idCmdArgs &args ) {
	(void)args;
	if ( !RendererDefaultSafety_RunSelfTest() ) {
		common->Warning( "Renderer default safety self-test failed" );
	}
}

static void R_RendererBenchmarkCapture_f( const idCmdArgs &args ) {
	(void)args;
	RendererBenchmarks_PrintLatestCapture();
	RendererBenchmarks_PrintTimingMarker();
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

static void R_RendererRenderGraphResourceSelfTest_f( const idCmdArgs &args ) {
	(void)args;
	if ( !RendererRenderGraphResource_RunSelfTest() ) {
		common->Warning( "Renderer render-graph resource self-test failed" );
	}
}

static void R_RendererRenderGraphResourceDump_f( const idCmdArgs &args ) {
	(void)args;
	R_RenderGraphResources_DumpLatest();
}

static void R_RendererMaterialResourceTableSelfTest_f( const idCmdArgs &args ) {
	(void)args;
	if ( !RendererMaterialResourceTable_RunSelfTest() ) {
		common->Warning( "Renderer material resource-table self-test failed" );
	}
}

static void R_RendererClassicGuiDomainSelfTest_f( const idCmdArgs &args ) {
	(void)args;
	if ( RendererClassicGuiDomain_RunSelfTest() ) {
		common->Printf( "RendererClassicGuiDomain self-test passed\n" );
	} else {
		common->Warning( "RendererClassicGuiDomain self-test failed" );
	}
}

static void R_RendererClassicCinematicPostDomainSelfTest_f(
		const idCmdArgs &args ) {
	(void)args;
	if ( RendererClassicCinematicPostDomain_RunSelfTest() ) {
		common->Printf( "RendererClassicCinematicPostDomain self-test passed\n" );
	} else {
		common->Warning( "RendererClassicCinematicPostDomain self-test failed" );
	}
}

static void R_RendererClassicSpecialFrameDomainSelfTest_f(
		const idCmdArgs &args ) {
	(void)args;
	if ( RendererClassicSpecialFrameDomain_RunSelfTest() ) {
		common->Printf( "RendererClassicSpecialFrameDomain self-test passed\n" );
	} else {
		common->Warning( "RendererClassicSpecialFrameDomain self-test failed" );
	}
}

static void R_RendererClassicWorldAmbientDomainSelfTest_f( const idCmdArgs &args ) {
	(void)args;
	if ( RendererClassicWorldAmbientDomain_RunSelfTest() ) {
		common->Printf( "RendererClassicWorldAmbientDomain self-test passed\n" );
	} else {
		common->Warning( "RendererClassicWorldAmbientDomain self-test failed" );
	}
}

static void R_RendererClassicInteractionDomainSelfTest_f( const idCmdArgs &args ) {
	(void)args;
	if ( RendererClassicInteractionDomain_RunSelfTest() ) {
		common->Printf( "RendererClassicInteractionDomain self-test passed\n" );
	} else {
		common->Warning( "RendererClassicInteractionDomain self-test failed" );
	}
}

static void R_RendererClassicFogBlendDomainSelfTest_f(
		const idCmdArgs &args ) {
	(void)args;
	if ( RendererClassicFogBlendDomain_RunSelfTest() ) {
		common->Printf( "RendererClassicFogBlendDomain self-test passed\n" );
	} else {
		common->Warning( "RendererClassicFogBlendDomain self-test failed" );
	}
}

static void R_RendererClassicSubviewDomainSelfTest_f(
		const idCmdArgs &args ) {
	(void)args;
	if ( RendererClassicSubviewDomain_RunSelfTest() ) {
		common->Printf( "RendererClassicSubviewDomain self-test passed\n" );
	} else {
		common->Warning( "RendererClassicSubviewDomain self-test failed" );
	}
}

static void R_RendererClassicDeformDomainSelfTest_f( const idCmdArgs &args ) {
	(void)args;
	if ( RendererClassicDeformDomain_RunSelfTest() ) {
		common->Printf( "RendererClassicDeformDomain self-test passed\n" );
	} else {
		common->Warning( "RendererClassicDeformDomain self-test failed" );
	}
}

static void R_RendererGpuSkinningSelfTest_f( const idCmdArgs &args ) {
	(void)args;
	if ( R_GpuSkinning_RunSelfTest() ) {
		common->Printf( "RendererGpuSkinning self-test passed\n" );
	} else {
		common->Warning( "RendererGpuSkinning self-test failed" );
	}
}

static void R_RendererContractsSelfTest_f( const idCmdArgs &args ) {
	(void)args;
	if ( RendererContracts_RunSelfTest() ) {
		common->Printf( "RendererContracts self-test passed\n" );
	} else {
		common->Warning( "RendererContracts self-test failed" );
	}
}

static void R_RendererPBRMaterialSelfTest_f( const idCmdArgs &args ) {
	(void)args;
	if ( !R_PBRMaterialParserSelfTest() ) {
		common->Warning( "Renderer PBR material parser self-test failed" );
		return;
	}
	common->Printf( "RendererPBRMaterial self-test passed\n" );
}

static void R_RendererMaterialResourceTableDump_f( const idCmdArgs &args ) {
	(void)args;
	R_MaterialResourceTable_DumpLatest();
}

static void R_RendererGeometryResourceSelfTest_f( const idCmdArgs &args ) {
	(void)args;
	if ( !RendererGeometryResource_RunSelfTest() ) {
		common->Warning( "Renderer geometry resource self-test failed" );
	}
}

static void R_RendererGLStateCacheSelfTest_f( const idCmdArgs &args ) {
	(void)args;
	if ( !RendererGLStateCache_RunSelfTest() ) {
		common->Warning( "Renderer GL state-cache self-test failed" );
	}
}

static void R_RendererModernGLExecutorSelfTest_f( const idCmdArgs &args ) {
	(void)args;
	if ( !RendererModernGLExecutor_RunSelfTest() ) {
		common->Warning( "Renderer modern GL executor self-test failed" );
	}
}

static void R_RendererGpuDrivenSelfTest_f( const idCmdArgs &args ) {
	(void)args;
	if ( !RendererGpuDriven_RunSelfTest() ) {
		common->Warning( "Renderer GPU-driven self-test failed" );
	}
}

static void R_RendererVisiblePathSelfTest_f( const idCmdArgs &args ) {
	(void)args;
	if ( !RendererVisiblePath_RunSelfTest() ) {
		common->Warning( "Renderer visible modern depth-path self-test failed" );
	}
}

static void R_RendererGBufferSelfTest_f( const idCmdArgs &args ) {
	(void)args;
	if ( !RendererGBuffer_RunSelfTest() ) {
		common->Warning( "Renderer modern G-buffer self-test failed" );
	}
}

static void R_RendererPBRVisibleSelfTest_f( const idCmdArgs &args ) {
	(void)args;
	if ( !RendererPBRVisible_RunSelfTest() ) {
		common->Warning( "Renderer PBR visible-path self-test failed" );
	}
}

static void R_RendererClusterGridSelfTest_f( const idCmdArgs &args ) {
	(void)args;
	if ( !RendererClusterGrid_RunSelfTest() ) {
		common->Warning( "Renderer clustered light-grid self-test failed" );
	}
}

static void R_RendererLightImageAtlasSelfTest_f( const idCmdArgs &args ) {
	(void)args;
	if ( !RendererLightImageAtlas_RunSelfTest() ) {
		common->Warning( "Renderer light image atlas self-test failed" );
	}
}

static void R_RendererShadowPlannerSelfTest_f( const idCmdArgs &args ) {
	(void)args;
	if ( !RendererShadowPlanner_RunSelfTest() ) {
		common->Warning( "Renderer shadow-planner self-test failed" );
	}
}

static void R_RendererShadowProjectedDiagnosticSelfTest_f( const idCmdArgs &args ) {
	(void)args;
	if ( !RendererShadowProjectedDiagnostic_RunSelfTest() ) {
		common->Warning( "Renderer projected shadow diagnostic self-test failed" );
	}
}

static void R_RendererDeferredResolveSelfTest_f( const idCmdArgs &args ) {
	(void)args;
	if ( !RendererDeferredResolve_RunSelfTest() ) {
		common->Warning( "Renderer deferred light resolve self-test failed" );
	}
}

static void R_RendererForwardPlusSelfTest_f( const idCmdArgs &args ) {
	(void)args;
	if ( !RendererForwardPlus_RunSelfTest() ) {
		common->Warning( "Renderer clustered forward+ self-test failed" );
	}
}

static void R_RendererModernVisibleSelfTest_f( const idCmdArgs &args ) {
	(void)args;
	if ( !RendererModernVisible_RunSelfTest() ) {
		common->Warning( "Renderer modern visible-frame self-test failed" );
	}
}

static void R_RendererModernCompatibilitySelfTest_f( const idCmdArgs &args ) {
	(void)args;
	if ( !RendererModernCompatibility_RunSelfTest() ) {
		common->Warning( "Renderer modern compatibility self-test failed" );
	}
}

static void R_RendererPassOwnershipSelfTest_f( const idCmdArgs &args ) {
	(void)args;
	if ( !RendererPassOwnership_RunSelfTest() ) {
		common->Warning( "Renderer pass ownership self-test failed" );
	}
}

static void R_RendererModernVisibilitySelfTest_f( const idCmdArgs &args ) {
	(void)args;
	if ( !RendererModernVisibility_RunSelfTest() ) {
		common->Warning( "Renderer modern visibility self-test failed" );
	}
}

static void R_RendererLowOverheadSelfTest_f( const idCmdArgs &args ) {
	(void)args;
	if ( !RendererLowOverhead_RunSelfTest() ) {
		common->Warning( "Renderer GL45 low-overhead self-test failed" );
	}
}

static void R_RendererModernGLShaderLibrarySelfTest_f( const idCmdArgs &args ) {
	(void)args;
	if ( !RendererModernGLShaderLibrary_RunSelfTest() ) {
		common->Warning( "Renderer modern GL shader-library self-test failed" );
	}
}

static void R_RendererShaderLibraryReload_f( const idCmdArgs &args ) {
	(void)args;
	if ( !R_ModernGLShaderLibrary_Reload() ) {
		common->Warning( "Renderer shader-library reload did not complete" );
	}
	// plans cache program handles/uniform locations from the previous library
	// generation; drop them so the next frame rebuilds against live programs
	R_ModernGLExecutor_InvalidatePlans();
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
R_PublishCompressionCapsToImageTools

imagetools is a static library, so every binary that links it (the engine, and
each renderer module) owns a private copy of the capability block that gates
precompressed-DDS selection. Any backend that fills glConfig must therefore
publish through here, and the engine must republish into its own copy once the
renderer reports its configuration - otherwise the un-published copies stay
zero-initialized and silently reject every DDS replacement, BC7 included.
==================
*/
void R_PublishCompressionCapsToImageTools( void ) {
	imageToolsCompressionCaps_t compressionCaps;
	compressionCaps.textureCompressionAvailable = glConfig.textureCompressionAvailable;
	compressionCaps.bptcTextureCompressionAvailable = glConfig.bptcTextureCompressionAvailable;
	compressionCaps.etc2TextureCompressionAvailable = glConfig.etc2TextureCompressionAvailable;
	ImageTools_SetCompressionCaps( compressionCaps );
}

/*
==================
R_CheckPortableExtensions

==================
*/
static void R_CheckPortableExtensions( void ) {
	R_RecordRendererStartupPhase( RENDERER_STARTUP_PHASE_R_CHECK_PORTABLE_EXTENSIONS );
	glConfig.glVersion = atof( glConfig.version_string );
	R_ClearMissingRequiredOpenGLFeatures();

	if ( !GLimp_EnsureActiveContext( "GLEW initialization" ) ) {
		common->FatalError( "Unable to make OpenGL context current for GLEW initialization\n" );
	}
	common->Printf("Init Glew...\n");

	glewExperimental = GL_TRUE;
	const GLenum glewResult = glewInit();
	if ( glewResult != GLEW_OK ) {
		const GLubyte *glewError = glewGetErrorString( glewResult );
		common->FatalError( "Failed to init GLEW: %s\n", glewError != NULL ? reinterpret_cast<const char *>( glewError ) : "unknown error" );
	}
	while ( glGetError() != GL_NO_ERROR ) {
	}

	GLCapabilityProbe_Build( glConfig.backendCaps, glConfig.version_string, glConfig.extensions_string );
	glConfig.extensions_string = GLCapabilityProbe_ExtensionString();

	// The atof() above cannot read an "OpenGL ES <n>.<n> ..." version string
	// and leaves 0.0 behind. The probe parses both spellings, so adopt its
	// result now that it has run.
	glConfig.glVersion = glConfig.backendCaps.glVersion;

	// A profile with no fixed-function stage: desktop core, or OpenGL ES at any
	// version. Several checks below are written against compatibility-only
	// extension strings and enums, which such a profile cannot answer.
	const bool programmableOnlyProfile =
		glConfig.backendCaps.profile == RENDERER_CONTEXT_PROFILE_ES
		|| glConfig.backendCaps.profile == RENDERER_CONTEXT_PROFILE_CORE;

	// GL_ARB_multitexture
	glConfig.multitextureAvailable = R_CheckRequiredExtension( "GL_ARB_multitexture" );
	if ( glConfig.multitextureAvailable && !R_HasARBMultitextureEntryPoints() ) {
		common->Printf( "X..GL_ARB_multitexture entry points incomplete\n" );
		R_RecordMissingRequiredOpenGLFeature( "GL_ARB_multitexture entry points" );
		glConfig.multitextureAvailable = false;
	}
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
	} else if ( programmableOnlyProfile ) {
		// GL_ARB_multitexture is compatibility-only, so the block above never
		// runs here and every limit would stay 0 -- which makes GL_SelectTexture
		// and idImage::Bind reject every unit and the frame comes out black.
		// GL_MAX_TEXTURE_IMAGE_UNITS is the core query and is what actually
		// bounds a fragment shader's samplers. GL_MAX_TEXTURE_UNITS and
		// GL_MAX_TEXTURE_COORDS describe fixed-function stages that do not
		// exist, so mirror the image-unit count rather than querying them.
		glGetIntegerv( GL_MAX_TEXTURE_IMAGE_UNITS, (GLint *)&glConfig.maxTextureImageUnits );
		if ( glConfig.maxTextureImageUnits <= 0 ) {
			glConfig.maxTextureImageUnits = 16;		// the ES 3.0 / GL 3.3 floor
		}
		if ( glConfig.maxTextureImageUnits > MAX_MULTITEXTURE_UNITS ) {
			glConfig.maxTextureImageUnits = MAX_MULTITEXTURE_UNITS;
		}
		glConfig.maxTextureUnits = glConfig.maxTextureImageUnits;
		glConfig.maxTextureCoords = glConfig.maxTextureImageUnits;
		glConfig.multitextureAvailable = true;
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

	// GL 1.3/GL_ARB_texture_compression + GL_S3_s3tc
	// DRI drivers may have GL_ARB_texture_compression but no GL_EXT_texture_compression_s3tc
	const bool textureCompressionAdvertised = glConfig.glVersion >= 1.3f || R_CheckExtension( "GL_ARB_texture_compression" );
	const bool textureCompressionEntryPointsAvailable = glCompressedTexImage2DARB != NULL && glCompressedTexSubImage2DARB != NULL;
	if ( textureCompressionAdvertised && !textureCompressionEntryPointsAvailable ) {
		common->Printf( "X..texture compression entry points incomplete\n" );
	}
	const bool textureCompressionAvailable = textureCompressionAdvertised && textureCompressionEntryPointsAvailable;
	// OpenGL ES never advertises GL_EXT_texture_compression_s3tc. ANGLE splits the
	// same formats across GL_EXT_texture_compression_dxt1 (which defines both the
	// RGB and RGBA DXT1 tokens) and GL_ANGLE_texture_compression_dxt5, and only
	// offers the sRGB variant under the s3tc name. The engine uploads exactly
	// GL_COMPRESSED_RGBA_S3TC_DXT1_EXT and GL_COMPRESSED_RGBA_S3TC_DXT5_EXT, so
	// those two extensions cover everything FMT_DXT1/FMT_DXT5 need.
	//
	// Getting this wrong is not a quiet quality downgrade: a rejected DXT header
	// makes R_BinaryImageHeaderSupportedByRenderer fail, so every precompressed
	// image is regenerated from source art as FMT_RGB565 -- slowly, and lazily
	// enough that placeholders are visible until a level load completes.
	const bool s3tcSpelled = R_CheckExtension( "GL_EXT_texture_compression_s3tc" );
	const bool dxtSpelledSeparately =
		R_CheckExtension( "GL_EXT_texture_compression_dxt1" ) &&
		R_CheckExtension( "GL_ANGLE_texture_compression_dxt5" );
	if ( textureCompressionAvailable && ( s3tcSpelled || dxtSpelledSeparately ) ) {
		glConfig.textureCompressionAvailable = true;
	} else {
		glConfig.textureCompressionAvailable = false;
	}
	const bool bptcTextureCompressionAdvertised =
		glConfig.glVersion >= 4.2f || R_CheckExtension( "GL_ARB_texture_compression_bptc" ) || R_CheckExtension( "GL_EXT_texture_compression_bptc" );
	glConfig.bptcTextureCompressionAvailable =
		textureCompressionAvailable &&
		bptcTextureCompressionAdvertised;

	// ETC2 and EAC are core in OpenGL ES 3.0 -- no extension string to check,
	// and every device that can run this renderer has them. That is the whole
	// point: on the Adreno 650 class, which advertises no S3TC at all, this is
	// the only compressed format the driver will take, and without it every
	// texture lands at FMT_RGBA8.
	//
	// Deliberately NOT enabled for desktop GL. Core since 4.3 there too, but
	// desktop drivers commonly satisfy that by decompressing to RGBA in the
	// driver, which is slower than uncompressed and no smaller. Desktop has
	// S3TC and does not need this path.
	glConfig.etc2TextureCompressionAvailable =
		( glConfig.backendCaps.profile == RENDERER_CONTEXT_PROFILE_ES ) &&
		glConfig.glVersion >= 3.0f &&
		textureCompressionEntryPointsAvailable;
	if ( glConfig.etc2TextureCompressionAvailable ) {
		common->Printf( "...using ETC2/EAC texture compression (core in ES 3.0)\n" );
	} else {
		common->Printf( "X..ETC2/EAC texture compression not used on this context\n" );
	}

	// push the compression capabilities into the shared imagetools library,
	// which gates precompressed-DDS selection without reading renderer globals
	R_PublishCompressionCapsToImageTools();

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
	// Promoted to core in OpenGL 2.0 and in OpenGL ES 2.0, so neither a core
	// nor an ES context advertises the extension string -- probing for it
	// there silently selects the saturating GL_INCR/GL_DECR, which clamps
	// nested shadow volumes and breaks the order-equivalence the two-sided
	// single-pass stencil path depends on.
	const bool stencilWrapIsCore =
		glConfig.backendCaps.profile == RENDERER_CONTEXT_PROFILE_ES
		|| glConfig.backendCaps.profile == RENDERER_CONTEXT_PROFILE_CORE;
	if ( stencilWrapIsCore || R_CheckExtension( "GL_EXT_stencil_wrap" ) ) {
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
	if ( !glConfig.ARBVertexBufferObjectAvailable && programmableOnlyProfile ) {
		// Buffer objects are core since GL 1.5 and ES 2.0, so the extension
		// string is absent on these profiles and the probe above finds nothing.
		// Believing it drops the engine onto the virtual-memory vertex cache
		// ("Vertex cache is SLOW"), which leaves every ambientCache->vbo at 0.
		// Every scene surface then fails the modern geometry path with
		// MISSING_VERTEX_BUFFER: measured on game/mcc_1, 654 of ~662 draws.
		glConfig.ARBVertexBufferObjectAvailable = true;
	}
	if ( glConfig.ARBVertexBufferObjectAvailable && !R_HasARBVertexBufferObjectEntryPoints() ) {
		common->Printf( "X..GL_ARB_vertex_buffer_object entry points incomplete; using virtual-memory vertex cache\n" );
		glConfig.ARBVertexBufferObjectAvailable = false;
	}
	const bool pixelBufferObjectAdvertised = ( GLEW_ARB_pixel_buffer_object || GLEW_EXT_pixel_buffer_object || GLEW_VERSION_2_1 ) != 0;
	glConfig.pixelBufferObjectAvailable = pixelBufferObjectAdvertised && R_HasARBPixelBufferObjectEntryPoints();
	if ( pixelBufferObjectAdvertised && !glConfig.pixelBufferObjectAvailable ) {
		common->Printf( "X..pixel buffer object entry points incomplete; async readbacks disabled\n" );
	}

	// ARB_vertex_program
	glConfig.ARBVertexProgramAvailable = R_CheckRequiredExtension( "GL_ARB_vertex_program" );
	if ( glConfig.ARBVertexProgramAvailable && !R_HasARBProgramEntryPoints() ) {
		common->Printf( "X..GL_ARB_vertex_program entry points incomplete\n" );
		R_RecordMissingRequiredOpenGLFeature( "GL_ARB_vertex_program entry points" );
		glConfig.ARBVertexProgramAvailable = false;
	}

	// ARB_fragment_program
	if ( r_inhibitFragmentProgram.GetBool() ) {
		glConfig.ARBFragmentProgramAvailable = false;
	} else {
		glConfig.ARBFragmentProgramAvailable = R_CheckRequiredExtension( "GL_ARB_fragment_program" );
		if ( glConfig.ARBFragmentProgramAvailable && !R_HasARBProgramEntryPoints() ) {
			common->Printf( "X..GL_ARB_fragment_program entry points incomplete\n" );
			R_RecordMissingRequiredOpenGLFeature( "GL_ARB_fragment_program entry points" );
			glConfig.ARBFragmentProgramAvailable = false;
		}
	}

	// GL_ARB shader objects / GLSL
	glConfig.GLSLProgramAvailable = R_CanUseGLSLPrograms();
	glConfig.GLSL130Available = false;
	if ( glConfig.GLSLProgramAvailable ) {
		// Apple GL 2.1 compatibility contexts stop at GLSL 1.20, which cannot
		// compile the #version 130 post shaders (SMAA); record the ceiling so
		// those paths can gate and report instead of failing at compile time.
		const char *glslVersionString = (const char *)glGetString( GL_SHADING_LANGUAGE_VERSION );
		int glslMajor = 0;
		int glslMinor = 0;
		if ( glslVersionString != NULL && sscanf( glslVersionString, "%d.%d", &glslMajor, &glslMinor ) == 2 ) {
			glConfig.GLSL130Available = ( glslMajor > 1 ) || ( glslMajor == 1 && glslMinor >= 30 );
		}
		common->Printf( "...using GL_ARB shader objects + GLSL (version %s)\n", glslVersionString != NULL ? glslVersionString : "unknown" );
	} else if ( r_inhibitFragmentProgram.GetBool() ) {
		common->Printf( "X..GLSL shader objects disabled by r_inhibitFragmentProgram\n" );
	} else if ( cvarSystem->GetCVarInteger( "net_serverDedicated" ) != 0 ) {
		common->Printf( "X..GLSL shader objects disabled on dedicated server\n" );
	} else {
		common->Printf( "X..GLSL shader objects not found\n" );
	}

	// check for minimum set
	//
	// This minimum is the legacy ARB2 path's, expressed as compatibility-only
	// extension strings. A profile without fixed-function cannot report them,
	// so the check is meaningless there and would fail a context the modern
	// path is perfectly able to drive -- see R_CheckRequiredExtension.
	if ( !programmableOnlyProfile
		&& ( !glConfig.multitextureAvailable || !glConfig.textureEnvCombineAvailable || !glConfig.cubeMapAvailable
		|| !glConfig.envDot3Available ) ) {
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
	if ( !programmableOnlyProfile ) {
		// These mirror the compatibility-only extension strings back into the
		// caps. On a core or ES profile those strings are absent for features
		// that are core, so copying them here would undo what the capability
		// probe already worked out from the version -- and a 4.1 core context
		// would report VBO:0 GLSL:0 and drop to NullRenderer.
		glConfig.backendCaps.hasVBO = glConfig.ARBVertexBufferObjectAvailable;
		glConfig.backendCaps.hasPBO = glConfig.pixelBufferObjectAvailable;
		glConfig.backendCaps.hasGLSL = glConfig.GLSLProgramAvailable;
	}
	glConfig.backendCaps.hasSRGBTextures = glConfig.textureSRGBAvailable;
	glConfig.backendCaps.hasFramebufferSRGB = glConfig.framebufferSRGBAvailable;
	glConfig.backendCaps.hasMRT = glConfig.maxDrawBuffers >= 4 && glConfig.maxColorAttachments >= 4;

	const rendererDriverInfo_t driverInfo = {
		glConfig.vendor_string,
		glConfig.renderer_string,
		glConfig.version_string,
		glConfig.backendCaps.glMajor,
		glConfig.backendCaps.glMinor,
		glConfig.backendCaps.profile,
		glConfig.backendCaps.hasFixedFunctionCompatibility
	};
	RendererDriverQuirks_Apply( glConfig.backendCaps, driverInfo );
	if ( glConfig.ARBVertexBufferObjectAvailable && !glConfig.backendCaps.hasVBO ) {
		common->Printf( "X..GL_ARB_vertex_buffer_object disabled by renderer driver quirk; using virtual-memory vertex cache\n" );
		glConfig.ARBVertexBufferObjectAvailable = false;
	}
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
	int         mode;
	int         width, height;
} vidmode_t;

// Keep this list in the same resolution order as mainMenuVidModes in
// Session_menu.cpp. Mode IDs 0..10 preserve the previously shipped openQ4
// meanings; newer presets are appended by ID while the table stays ordered for
// readable diagnostics.
vidmode_t r_vidModes[] = {
	{ 11, 640, 480 },
	{ 12, 720, 480 },
	{ 13, 720, 576 },
	{ 14, 800, 480 },
	{ 15, 800, 600 },
	{ 16, 854, 480 },
	{ 17, 960, 540 },
	{ 18, 960, 600 },
	{ 19, 1024, 576 },
	{ 20, 1024, 600 },
	{ 21, 1024, 768 },
	{ 22, 1152, 648 },
	{ 23, 1152, 720 },
	{ 24, 1152, 768 },
	{ 25, 1152, 864 },
	{ 0, 1280, 720 },
	{ 26, 1280, 768 },
	{ 27, 1280, 800 },
	{ 28, 1280, 854 },
	{ 29, 1280, 960 },
	{ 30, 1280, 1024 },
	{ 31, 1360, 768 },
	{ 1, 1366, 768 },
	{ 32, 1400, 900 },
	{ 33, 1400, 1050 },
	{ 34, 1440, 900 },
	{ 35, 1440, 960 },
	{ 36, 1536, 864 },
	{ 2, 1600, 900 },
	{ 37, 1600, 1200 },
	{ 38, 1680, 1050 },
	{ 3, 1920, 1080 },
	{ 4, 1920, 1200 },
	{ 39, 1920, 1280 },
	{ 40, 2048, 1080 },
	{ 41, 2048, 1152 },
	{ 42, 2048, 1536 },
	{ 43, 2160, 1440 },
	{ 44, 2240, 1260 },
	{ 45, 2240, 1400 },
	{ 46, 2256, 1504 },
	{ 47, 2304, 1440 },
	{ 48, 2400, 1350 },
	{ 49, 2400, 1600 },
	{ 50, 2520, 1680 },
	{ 5, 2560, 1080 },
	{ 6, 2560, 1440 },
	{ 51, 2560, 1600 },
	{ 52, 2560, 1664 },
	{ 53, 2736, 1824 },
	{ 54, 2880, 1620 },
	{ 55, 2880, 1800 },
	{ 56, 2880, 1864 },
	{ 57, 2880, 1920 },
	{ 58, 3000, 2000 },
	{ 59, 3024, 1964 },
	{ 60, 3200, 1800 },
	{ 61, 3200, 2000 },
	{ 62, 3240, 2160 },
	{ 7, 3440, 1440 },
	{ 63, 3456, 2160 },
	{ 64, 3456, 2234 },
	{ 65, 3840, 1080 },
	{ 66, 3840, 1200 },
	{ 67, 3840, 1600 },
	{ 8, 3840, 2160 },
	{ 68, 3840, 2400 },
	{ 69, 3840, 2560 },
	{ 70, 4096, 2160 },
	{ 9, 5120, 1440 },
	{ 71, 5120, 2160 },
	{ 10, 5120, 2880 },
	{ 72, 6016, 3384 },
	{ 73, 7680, 2160 },
	{ 74, 7680, 4320 }
};
static int	s_numVidModes = ( sizeof( r_vidModes ) / sizeof( r_vidModes[0] ) );

static const vidmode_t *R_FindVidModeByMode( int mode ) {
	for ( int i = 0; i < s_numVidModes; ++i ) {
		if ( r_vidModes[i].mode == mode ) {
			return &r_vidModes[i];
		}
	}
	return NULL;
}

static const vidmode_t *R_DefaultVidMode( void ) {
	const vidmode_t *mode = R_FindVidModeByMode( 0 );
	return mode != NULL ? mode : &r_vidModes[0];
}

static int R_ModeGreatestCommonDivisor( int a, int b ) {
	a = idMath::Abs( a );
	b = idMath::Abs( b );
	while ( b != 0 ) {
		const int remainder = a % b;
		a = b;
		b = remainder;
	}
	return a > 0 ? a : 1;
}

static idStr R_ModeAspectRatioLabel( int width, int height ) {
	if ( width <= 0 || height <= 0 ) {
		return "";
	}

	typedef struct commonAspectRatio_s {
		int width;
		int height;
	} commonAspectRatio_t;
	static const commonAspectRatio_t commonAspectRatios[] = {
		{ 5, 4 },
		{ 4, 3 },
		{ 3, 2 },
		{ 5, 3 },
		{ 16, 10 },
		{ 16, 9 },
		{ 21, 9 },
		{ 32, 10 },
		{ 32, 9 }
	};

	const float displayAspect = static_cast<float>( width ) / static_cast<float>( height );
	int bestIndex = -1;
	float bestDelta = idMath::INFINITY;
	for ( int i = 0; i < static_cast<int>( sizeof( commonAspectRatios ) / sizeof( commonAspectRatios[0] ) ); ++i ) {
		const float targetAspect = static_cast<float>( commonAspectRatios[i].width ) / static_cast<float>( commonAspectRatios[i].height );
		const float delta = idMath::Fabs( displayAspect - targetAspect );
		if ( delta < bestDelta ) {
			bestDelta = delta;
			bestIndex = i;
		}
	}

	if ( bestIndex >= 0 ) {
		const float targetAspect = static_cast<float>( commonAspectRatios[bestIndex].width ) / static_cast<float>( commonAspectRatios[bestIndex].height );
		if ( bestDelta <= targetAspect * 0.04f ) {
			return va( "%d:%d", commonAspectRatios[bestIndex].width, commonAspectRatios[bestIndex].height );
		}
	}

	const int gcd = R_ModeGreatestCommonDivisor( width, height );
	const int reducedWidth = width / gcd;
	const int reducedHeight = height / gcd;
	if ( reducedWidth <= 64 && reducedHeight <= 64 ) {
		return va( "%d:%d", reducedWidth, reducedHeight );
	}

	return "";
}

#if defined( MACOS_X )
bool R_GetModeInfo( int *width, int *height, int mode ) {
#else
static bool R_GetModeInfo( int *width, int *height, int mode ) {
#endif
	const vidmode_t	*vm;
	const int originalMode = mode;

	if ( mode < -2 ) {
		common->Printf( "^3R_GetModeInfo: r_mode %d is invalid, using custom mode (-1)\n", mode );
		mode = -1;
	}
	if ( mode >= 0 && R_FindVidModeByMode( mode ) == NULL ) {
		const vidmode_t *defaultMode = R_DefaultVidMode();
		common->Printf( "^3R_GetModeInfo: r_mode %d out of range, using mode 0 (%dx%d)\n",
			mode, defaultMode->width, defaultMode->height );
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

	vm = R_FindVidModeByMode( mode );
	if ( vm == NULL ) {
		vm = R_DefaultVidMode();
	}

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

/*
====================
R_GetInitialWindowSize

Non-static wrapper over the mode/windowed sizing statics so alternative
backends (the Vulkan module) derive the initial window size exactly like
the GL bring-up does.
====================
*/
bool R_GetInitialWindowSize( bool fullScreen, int *width, int *height ) {
	if ( fullScreen ) {
		return R_GetModeInfo( width, height, r_mode.GetInteger() );
	}
	R_GetWindowedModeInfo( width, height );
	return true;
}

static int R_NormalizeMultiSamplesValue( const int samples ) {
	if ( samples <= 1 ) {
		return 0;
	}
	if ( samples <= 2 ) {
		return 2;
	}
	if ( samples <= 4 ) {
		return 4;
	}
	if ( samples <= 8 ) {
		return 8;
	}
	return 16;
}

static void R_NormalizeDisplayCvars( void ) {
	const int originalMode = r_mode.GetInteger();
	int normalizedMode = originalMode;

	if ( normalizedMode < -2 ) {
		common->Printf( "^3R_GetModeInfo: r_mode %d is invalid, using custom mode (-1)\n", normalizedMode );
		normalizedMode = -1;
	} else if ( normalizedMode >= 0 && R_FindVidModeByMode( normalizedMode ) == NULL ) {
		const vidmode_t *defaultMode = R_DefaultVidMode();
		common->Printf( "^3R_GetModeInfo: r_mode %d out of range, using mode 0 (%dx%d)\n",
			normalizedMode, defaultMode->width, defaultMode->height );
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

	const int originalMultiSamples = r_multiSamples.GetInteger();
	const int normalizedMultiSamples = R_NormalizeMultiSamplesValue( originalMultiSamples );
	if ( originalMultiSamples != normalizedMultiSamples ) {
		common->Printf( "^3r_multiSamples %d is not a supported MSAA step; using %d^0\n", originalMultiSamples, normalizedMultiSamples );
		r_multiSamples.SetInteger( normalizedMultiSamples );
		r_multiSamples.ClearModified();
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
	R_RecordRendererStartupPhase( RENDERER_STARTUP_PHASE_R_INIT_OPENGL );
	RB_ResetARB2InteractionHandoffBreadcrumb();
	RB_ResetAppleGL21RouteCounters();

	if ( glConfig.isInitialized ) {
		common->FatalError( "R_InitOpenGL called while active" );
	}

	// every context creation starts a new handle generation; GL object handles
	// stamped with an older generation belong to a destroyed context and must
	// not be deleted (the names may alias live objects in this context)
	tr.glContextGeneration++;

	// in case we had an error while doing a tiled rendering
	tr.viewportOffset[0] = 0;
	tr.viewportOffset[1] = 0;

	R_NormalizeDisplayCvars();

	// select the rendering API path (r_renderApi) before any window or
	// context work; a failed non-GL selection falls back closed onto GL here
	R_RendererModule_Boot();

	//
	// initialize OS specific portions of the renderSystem
	//
	for ( i = 0 ; i < 2 ; i++ ) {
		// set the parameters we are trying
		parms.hiddenWindow = r_hiddenWindow.GetBool();
		parms.fullScreen = !parms.hiddenWindow && r_fullscreen.GetBool();
		if ( parms.fullScreen ) {
			R_GetModeInfo( &parms.width, &parms.height, r_mode.GetInteger() );
		} else {
			R_GetWindowedModeInfo( &parms.width, &parms.height );
		}
		engineWindowState.vidWidth = parms.width;
		engineWindowState.vidHeight = parms.height;
		glConfig.vidWidth = parms.width;
		glConfig.vidHeight = parms.height;
		parms.borderless = !parms.hiddenWindow && !parms.fullScreen && r_borderless.GetBool();
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

	if ( !GLimp_EnsureActiveContext( "OpenGL startup string query" ) ) {
		common->FatalError( "Unable to make OpenGL context current after window creation\n" );
	}

	// get our config strings
	glConfig.vendor_string = (const char *)glGetString(GL_VENDOR);
	glConfig.renderer_string = (const char *)glGetString(GL_RENDERER);
	glConfig.version_string = (const char *)glGetString(GL_VERSION);
	glConfig.extensions_string = (const char *)glGetString(GL_EXTENSIONS);
	if ( glConfig.version_string == NULL ) {
		common->FatalError( "OpenGL context did not report GL_VERSION after window creation; context may not be current\n" );
	}
	if ( glConfig.vendor_string == NULL ) {
		glConfig.vendor_string = "unknown";
	}
	if ( glConfig.renderer_string == NULL ) {
		glConfig.renderer_string = "unknown";
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
	R_GLDebugOutput_Init();

	// parse our vertex and fragment programs, possibly disably support for
	// one of the paths if there was an error
	R_ARB2_Init();
#ifdef OPENQ4_RENDERER_GLES_MODULE
	if ( glConfig.backendCaps.profile == RENDERER_CONTEXT_PROFILE_ES ) {
		// gles_d3 is the back end on ES, and it does not use the modern
		// executor. The executor's shader library is desktop GLSL -- every
		// program in it fails to compile as GLSL ES -- so bringing it up here
		// only prints forty lines of compile errors for a path that will not
		// run. Skip it.
		//
		// The GL state cache still has to come up: tr_backend drives it every
		// frame whichever back end drew the view.
		R_GLStateCache_Init( glConfig.backendCaps );
	} else
#endif
	{
		R_ModernGLExecutor_Init( glConfig.backendCaps, glConfig.renderFeatures );
	}
	RendererBootstrap_SetModernExecutorAvailable( R_ModernGLExecutor_Stats().available );
	RendererBootstrap_FinalizeLegacyBridge( glConfig.allowARB2Path );
	glConfig.rendererTier = RendererBootstrap_GetState().selectedTier;
	glConfig.renderFeatures = RendererBootstrap_GetState().features;
	// The ARB2 bridge is a hard requirement on a compatibility context: the
	// modern executor only *owns* the passes it has claimed and the legacy path
	// renders the rest, so losing ARB2 there means losing most of the frame.
	// Two independent escapes from that requirement:
	//   - a programmable-only profile (core or ES) has no ARB2 by construction;
	//     erroring out would make such a context unusable no matter how capable
	//     it is, and letting it through is what exposes the modern path's
	//     standalone coverage
	//   - once the modern visible path is promoted it can own the frame without
	//     the compatibility bridge (upstream); while no parity contract is
	//     proven the promotion state is false, keeping this exactly as strict
	//     as before
	const bool programmableOnlyProfile =
		glConfig.backendCaps.profile == RENDERER_CONTEXT_PROFILE_ES
		|| glConfig.backendCaps.profile == RENDERER_CONTEXT_PROFILE_CORE;
	if ( !glConfig.allowARB2Path && !programmableOnlyProfile
			&& !RendererBootstrap_ShouldAutoPromoteModernVisible() ) {
		R_ErrorForMissingRequiredOpenGLFeatures();
	}
	if ( !glConfig.allowARB2Path && programmableOnlyProfile ) {
		common->Printf(
			"Renderer: no ARB2 bridge on a %s profile; the modern executor renders standalone "
			"and any pass it does not own will be missing\n",
			RendererContextProfile_Name( glConfig.backendCaps.profile ) );
	}
	R_RenderGraphResources_Init( glConfig.backendCaps, glConfig.renderFeatures );
	R_MaterialResourceTable_Init( glConfig.backendCaps, glConfig.renderFeatures );

	cmdSystem->AddCommand( "reloadARBprograms", R_ReloadARBPrograms_f, CMD_FL_RENDERER, "reloads ARB programs" );
	R_ReloadARBPrograms_f( idCmdArgs() );
	R_GLDebugOutput_FlushMessages();

	R_RendererUpload_Init( glConfig.backendCaps );
	R_GpuSkinning_ContractInit( glConfig.backendCaps );

	// allocate the vertex array range or vertex objects
	R_RecordRendererStartupPhase( RENDERER_STARTUP_PHASE_VERTEX_CACHE_INIT );
	vertexCache.Init();

	// select which renderSystem we are going to use
	r_renderer.SetModified();
	R_RecordRendererStartupPhase( RENDERER_STARTUP_PHASE_SET_BACK_END_RENDERER );
	tr.SetBackEndRenderer();

	// allocate the frame data, which may be more if smp is enabled
	R_InitFrameData();

	// Reset our gamma
	R_SetColorMappings();
	R_RecordRendererStartupPhase( RENDERER_STARTUP_PHASE_READY );

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

	R_GLDebugOutput_FlushMessages();

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
#if defined(__APPLE__)
			void *frames[8];
			const int frameCount = backtrace( frames, 8 );
			char **symbols = backtrace_symbols( frames, frameCount );
			if ( symbols != NULL ) {
				for ( int frame = 0; frame < frameCount; ++frame ) {
					common->Printf( "  %s\n", symbols[frame] );
				}
				free( symbols );
			}
#endif
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

	if ( !tr.primaryView || !tr.primaryWorld ) {
		common->Printf( "No primary view.\n" );
		return;
	}

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
		const idStr aspect = R_ModeAspectRatioLabel( r_vidModes[i].width, r_vidModes[i].height );
		if ( aspect.Length() > 0 ) {
			common->Printf( "Mode %2d: %dx%d (%s)\n", r_vidModes[i].mode, r_vidModes[i].width, r_vidModes[i].height, aspect.c_str() );
		} else {
			common->Printf( "Mode %2d: %dx%d\n", r_vidModes[i].mode, r_vidModes[i].width, r_vidModes[i].height );
		}
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
	idList<idMaterial *> list;

	count = declManager->GetNumDecls( DECL_MATERIAL );
	list.SetNum( count );

	for ( i = 0 ; i < count ; i++ ) {
		list[i] = (idMaterial *)declManager->DeclByIndex( DECL_MATERIAL, i, false );
	}

	qsort( list.Ptr(), count, sizeof( list[0] ), R_QsortSurfaceAreas );

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
typedef struct renderingFPSResult_s {
	float	fps;
	float	averageMsec;
	int		frameCount;
	int		p50Msec;
	int		p95Msec;
	int		p99Msec;
	int		maxMsec;
} renderingFPSResult_t;

static void R_SortBenchmarkFrameTimes( int *values, int count ) {
	for ( int i = 1; i < count; ++i ) {
		const int key = values[i];
		int j = i - 1;
		while ( j >= 0 && values[j] > key ) {
			values[j + 1] = values[j];
			j--;
		}
		values[j + 1] = key;
	}
}

static int R_BenchmarkPercentileIndex( int count, int percentile ) {
	if ( count <= 0 ) {
		return 0;
	}
	const int rank = ( count * percentile + 99 ) / 100;
	return idMath::ClampInt( 0, count - 1, rank - 1 );
}

static renderingFPSResult_t R_RenderingFPS( const renderView_t *renderView ) {
	renderingFPSResult_t result;
	memset( &result, 0, sizeof( result ) );
	glFinish();

	int		start = Sys_Milliseconds();
	static const int SAMPLE_MSEC = 1000;
	static const int MAX_FRAME_SAMPLES = 512;
	int		frameTimes[MAX_FRAME_SAMPLES];
	int		frameSampleCount = 0;
	int		end;
	int		count = 0;

	while( 1 ) {
		// render
		const int frameStart = Sys_Milliseconds();
		renderSystem->BeginFrame( glConfig.vidWidth, glConfig.vidHeight );
		tr.primaryWorld->RenderScene( renderView );
		renderSystem->EndFrame( NULL, NULL );
		glFinish();
		const int frameEnd = Sys_Milliseconds();
		if ( frameSampleCount < MAX_FRAME_SAMPLES ) {
			frameTimes[frameSampleCount++] = Max( 0, frameEnd - frameStart );
		}
		count++;
		end = frameEnd;
		if ( end - start > SAMPLE_MSEC ) {
			break;
		}
	}

	result.frameCount = count;
	result.fps = count * 1000.0f / Max( 1, end - start );
	result.averageMsec = 1000.0f / Max( 0.001f, result.fps );
	if ( frameSampleCount > 0 ) {
		R_SortBenchmarkFrameTimes( frameTimes, frameSampleCount );
		result.p50Msec = frameTimes[R_BenchmarkPercentileIndex( frameSampleCount, 50 )];
		result.p95Msec = frameTimes[R_BenchmarkPercentileIndex( frameSampleCount, 95 )];
		result.p99Msec = frameTimes[R_BenchmarkPercentileIndex( frameSampleCount, 99 )];
		result.maxMsec = frameTimes[frameSampleCount - 1];
	}

	return result;
}

/*
================
R_Benchmark_f
================
*/
void R_Benchmark_f( const idCmdArgs &args ) {
	renderingFPSResult_t result;
	renderView_t	view;

	if ( !tr.primaryView ) {
		common->Printf( "No primaryView for benchmarking\n" );
		return;
	}
	view = tr.primaryRenderView;

	for ( int size = 100 ; size >= 10 ; size -= 10 ) {
		r_screenFraction.SetInteger( size );
		result = R_RenderingFPS( &view );
		int	kpix = glConfig.vidWidth * glConfig.vidHeight * ( size * 0.01 ) * ( size * 0.01 ) * 0.001;
		common->Printf(
			"kpix: %4i  avg:%5.1fms p50:%3ims p95:%3ims p99:%3ims max:%3ims fps:%5.1f frames:%i\n",
			kpix,
			result.averageMsec,
			result.p50Msec,
			result.p95Msec,
			result.p99Msec,
			result.maxMsec,
			result.fps,
			result.frameCount );
	}

	// enable r_singleTriangle 1 while r_screenFraction is still at 10
	r_singleTriangle.SetBool( 1 );
	result = R_RenderingFPS( &view );
	common->Printf(
		"single tri  avg:%5.1fms p50:%3ims p95:%3ims p99:%3ims max:%3ims fps:%5.1f frames:%i\n",
		result.averageMsec,
		result.p50Msec,
		result.p95Msec,
		result.p99Msec,
		result.maxMsec,
		result.fps,
		result.frameCount );
	r_singleTriangle.SetBool( 0 );
	r_screenFraction.SetInteger( 100 );

	// enable r_skipRenderContext 1
	r_skipRenderContext.SetBool( true );
	result = R_RenderingFPS( &view );
	common->Printf(
		"no context  avg:%5.1fms p50:%3ims p95:%3ims p99:%3ims max:%3ims fps:%5.1f frames:%i\n",
		result.averageMsec,
		result.p50Msec,
		result.p95Msec,
		result.p99Msec,
		result.maxMsec,
		result.fps,
		result.frameCount );
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
/*
====================
r_screenshotKeepScissor

A screenshot is a RE-RENDER, not a copy of the frame on screen, and the
capture below turns the scissor off before producing it. Any defect that
depends on scissor state is therefore structurally invisible in a screenshot:
the image comes back correct while the display is wrong.

A capture therefore cannot be trusted to reproduce a scissor-dependent fault.

Note this was NOT the cause of the black surfaces reported during the gles_d3
bring-up, though it was suspected at the time. Those captures came back
correct for a different reason in the same family: the readback below reads
GL_RGBA and packs down to RGB, discarding alpha, and the fault was in the
alpha channel (see RB_ForceOpaquePresentAlpha, tr_backend.cpp). Two
independent ways for a screenshot to disagree with the display, which is why
this one is worth having even though it did not find that bug.

Set this to keep the scissor exactly as the frame loop had it. Default 0, the
historical behaviour, because leaving it on makes tiled (larger-than-screen)
captures responsible for their own scissor rects, which they are not.
====================
*/
idCVar r_screenshotKeepScissor( "r_screenshotKeepScissor", "0", CVAR_RENDERER | CVAR_BOOL,
		"screenshots re-render with the scissor left as the frame loop set it, so scissor-dependent faults are visible in a capture" );

void R_ReadTiledPixels( int width, int height, byte *buffer, renderView_t *ref = NULL ) {
	// include extra space for OpenGL padding to word boundaries
	byte	*temp = (byte *)R_StaticAlloc( (glConfig.vidWidth+3) * glConfig.vidHeight * 3 );

	// OpenGL ES guarantees exactly one glReadPixels format for the default
	// framebuffer -- GL_RGBA/GL_UNSIGNED_BYTE -- plus an implementation-defined
	// pair reported by GL_IMPLEMENTATION_COLOR_READ_FORMAT. ANGLE reports RGBA
	// and rejects the GL_RGB read this function used to issue, which failed with
	// GL_INVALID_OPERATION and left the destination untouched: every ES
	// screenshot came out uniformly black while the frame on screen was fine.
	// RGBA is equally valid on desktop GL, so read RGBA everywhere and pack down
	// rather than branching per backend.
	byte	*rgbaTemp = (byte *)R_StaticAlloc( (glConfig.vidWidth+3) * glConfig.vidHeight * 4 );

	int	oldWidth = glConfig.vidWidth;
	int oldHeight = glConfig.vidHeight;
	const bool oldUseScissor = r_useScissor.GetBool();

	tr.tiledViewport[0] = width;
	tr.tiledViewport[1] = height;

	// disable scissor, so we don't need to adjust all those rects
	const bool overrideScissor = !r_screenshotKeepScissor.GetBool();
	if ( overrideScissor ) {
		r_useScissor.SetBool( false );
	}

	for ( int xo = 0 ; xo < width ; xo += oldWidth ) {
		for ( int yo = 0 ; yo < height ; yo += oldHeight ) {
			tr.viewportOffset[0] = -xo;
			tr.viewportOffset[1] = -yo;

			if ( ref ) {
				tr.BeginFrame( oldWidth, oldHeight );
				if ( tr.portalSkyCaptureViewCallback != NULL && tr.primaryWorld != NULL ) {
					renderView_t portalSkyView = *ref;
					if ( tr.portalSkyCaptureViewCallback( ref, &portalSkyView ) ) {
						tr.primaryWorld->RenderScene( &portalSkyView, RF_DEFER_COMMAND_SUBMIT | RF_PORTAL_SKY );
					}
				}
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
				// RB_SwapBuffers leaves the fully post-processed frame in the back
				// buffer while takingScreenshot is set. Reading it before presentation
				// avoids relying on post-swap front-buffer preservation, which EGL
				// window surfaces (including native Wayland) do not guarantee.
				glReadBuffer( r_frontBuffer.GetBool() ? GL_FRONT : GL_BACK );
			}

			int w = oldWidth;
			if ( xo + w > width ) {
				w = width - xo;
			}
			int h = oldHeight;
			if ( yo + h > height ) {
				h = height - yo;
			}

			// The frame was produced by commands that may still be queued. A
			// desktop driver generally resolves that implicitly, but ANGLE's
			// Metal backend hands back an empty default framebuffer unless the
			// work is forced to complete first -- which is why ES screenshots
			// came out uniformly black while the frame on screen was correct.
			glReadPixels( 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgbaTemp );

			const int rgbaRow = ( w * 4 + 3 ) & ~3;
			const int rgbRow = ( w * 3 + 3 ) & ~3;
			for ( int y = 0; y < h; y++ ) {
				const byte *src = rgbaTemp + y * rgbaRow;
				byte *dst = temp + y * rgbRow;
				for ( int x = 0; x < w; x++ ) {
					dst[x * 3 + 0] = src[x * 4 + 0];
					dst[x * 3 + 1] = src[x * 4 + 1];
					dst[x * 3 + 2] = src[x * 4 + 2];
				}
			}

			int	row = ( w * 3 + 3 ) & ~3;		// OpenGL pads to dword boundaries

			for ( int y = 0 ; y < h ; y++ ) {
				memcpy( buffer + ( ( yo + y )* width + xo ) * 3,
					temp + y * row, w * 3 );
			}
		}
	}

	// restore only what we overrode, and to what the player actually had
	if ( overrideScissor ) {
		r_useScissor.SetBool( oldUseScissor );
	}

	tr.viewportOffset[0] = 0;
	tr.viewportOffset[1] = 0;
	tr.tiledViewport[0] = 0;
	tr.tiledViewport[1] = 0;

	R_StaticFree( temp );
	R_StaticFree( rgbaTemp );

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

	// cap each axis so the worst-case pix*2*3 blend accumulation buffer can't overflow int
	if ( width < 1 || height < 1 || width > 16384 || height > 16384 ) {
		common->Printf( "TakeScreenshot: bad dimensions %i x %i\n", width, height );
		return;
	}

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

		char suffix[16];
		idStr::snPrintf( suffix, sizeof( suffix ), "%i%i%i%i%i.tga", a, b, c, d, e );
		fileName = base;
		fileName += suffix;
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

static void R_NormalizeLevelShotMapDeclPath( const char *mapPath, idStr &normalizedPath ) {
	normalizedPath = ( mapPath != NULL ) ? mapPath : "";
	normalizedPath.BackSlashesToSlashes();
	normalizedPath.Strip( ' ' );
	normalizedPath.Strip( '\t' );
	normalizedPath.StripTrailingWhitespace();
	normalizedPath.StripQuotes();
	normalizedPath.StripFileExtension();

	if ( !idStr::Icmpn( normalizedPath.c_str(), "maps/", 5 ) ) {
		normalizedPath = normalizedPath.c_str() + 5;
	}
}

static void R_NormalizeLevelShotEntityFilterToken( const char *entityFilter, idStr &normalizedFilter ) {
	normalizedFilter = ( entityFilter != NULL ) ? entityFilter : "";
	normalizedFilter.Strip( ' ' );
	normalizedFilter.Strip( '\t' );
	normalizedFilter.StripTrailingWhitespace();
	normalizedFilter.StripQuotes();
}

static bool R_IsLevelShotMapFilterWhitespace( const char ch ) {
	return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
}

static void R_NormalizeLevelShotMapPathAndEntityFilter( const char *mapPath, const char *entityFilter, idStr &normalizedMapPath, idStr &normalizedEntityFilter ) {
	idStr mapToken = ( mapPath != NULL ) ? mapPath : "";
	mapToken.BackSlashesToSlashes();
	mapToken.Strip( ' ' );
	mapToken.Strip( '\t' );
	mapToken.StripTrailingWhitespace();
	mapToken.StripQuotes();

	R_NormalizeLevelShotEntityFilterToken( entityFilter, normalizedEntityFilter );

	int split = -1;
	for ( int i = mapToken.Length() - 1; i >= 0; --i ) {
		if ( R_IsLevelShotMapFilterWhitespace( mapToken[ i ] ) ) {
			split = i;
			break;
		}
	}

	if ( split > 0 ) {
		idStr mapPart = mapToken.Left( split );
		idStr filterPart = mapToken.Right( mapToken.Length() - split - 1 );
		idStr normalizedFilterPart;
		mapPart.Strip( ' ' );
		mapPart.Strip( '\t' );
		mapPart.StripTrailingWhitespace();
		mapPart.StripQuotes();
		R_NormalizeLevelShotEntityFilterToken( filterPart.c_str(), normalizedFilterPart );

		if ( mapPart.Length() > 0 && normalizedFilterPart.Length() > 0 ) {
			mapToken = mapPart;
			if ( normalizedEntityFilter.Length() == 0 ) {
				normalizedEntityFilter = normalizedFilterPart;
			}
		}
	}

	R_NormalizeLevelShotMapDeclPath( mapToken.c_str(), normalizedMapPath );
}

static bool R_GetLevelShotMapDeclForNormalizedPath( const idStr &normalizedPath, idDict &outMapDecl ) {
	if ( normalizedPath.IsEmpty() ) {
		return false;
	}

	const idDecl *mapDecl = declManager->FindType( DECL_MAPDEF, normalizedPath.c_str(), false );
	const idDeclEntityDef *mapDef = static_cast<const idDeclEntityDef *>( mapDecl );
	if ( mapDef != NULL ) {
		outMapDecl = mapDef->dict;
		outMapDecl.Set( "path", mapDef->GetName() );
		return true;
	}

	const int numMaps = fileSystem->GetNumMaps();
	for ( int i = 0; i < numMaps; ++i ) {
		const idDict *candidate = fileSystem->GetMapDecl( i );
		if ( candidate == NULL ) {
			continue;
		}

		idStr candidatePath;
		R_NormalizeLevelShotMapDeclPath( candidate->GetString( "path" ), candidatePath );
		if ( candidatePath.IsEmpty() ) {
			continue;
		}

		if ( !fileSystem->FilenameCompare( normalizedPath.c_str(), candidatePath.c_str() ) ) {
			outMapDecl = *candidate;
			return true;
		}
	}

	return false;
}

static bool R_GetLevelShotMapDecl( const char *mapPath, const char *entityFilter, idDict &outMapDecl ) {
	outMapDecl.Clear();

	idStr normalizedPath;
	idStr normalizedEntityFilter;
	R_NormalizeLevelShotMapPathAndEntityFilter( mapPath, entityFilter, normalizedPath, normalizedEntityFilter );
	if ( normalizedPath.IsEmpty() ) {
		return false;
	}

	if ( normalizedEntityFilter.Length() > 0 ) {
		idStr filteredPath = normalizedPath;
		filteredPath += "_";
		filteredPath += normalizedEntityFilter;
		filteredPath.Strip( ' ' );
		filteredPath.Strip( '\t' );
		filteredPath.StripTrailingWhitespace();
		filteredPath.StripQuotes();
		filteredPath.BackSlashesToSlashes();
		filteredPath.Replace( " ", "_" );
		if ( R_GetLevelShotMapDeclForNormalizedPath( filteredPath, outMapDecl ) ) {
			return true;
		}
	}

	if ( R_GetLevelShotMapDeclForNormalizedPath( normalizedPath, outMapDecl ) ) {
		return true;
	}

	if ( normalizedEntityFilter.Length() == 0 ) {
		idStr firstSegmentPath = normalizedPath;
		firstSegmentPath += "_first";
		if ( R_GetLevelShotMapDeclForNormalizedPath( firstSegmentPath, outMapDecl ) ) {
			return true;
		}
	}

	return false;
}

static bool R_GetDefaultLevelShotBaseName( idStr &baseName ) {
	idStr mapPath;

	if ( tr.primaryWorld != NULL && tr.primaryWorld->mapName.Length() > 0 && tr.primaryWorld->mapName != "<FREED>" ) {
		mapPath = tr.primaryWorld->mapName;
	} else {
		mapPath = cvarSystem->GetCVarString( "si_map" );
	}

	if ( mapPath.Length() <= 0 ) {
		return false;
	}

	idDict mapDeclDict;
	const char *entityFilter = cvarSystem->GetCVarString( "si_entityFilter" );
	if ( R_GetLevelShotMapDecl( mapPath.c_str(), entityFilter, mapDeclDict ) ) {
		const char *loadImage = mapDeclDict.GetString( "loadimage", "" );
		if ( loadImage[0] != '\0' ) {
			// Match the mapDef loadscreen name, including split-map variants such as _first/_second.
			baseName = loadImage;
			baseName.BackSlashesToSlashes();
			baseName.StripFileExtension();
			return true;
		}
	}

	idStr mapName = mapPath;
	mapName.StripPath();
	mapName.StripFileExtension();
	if ( mapName.Length() <= 0 ) {
		return false;
	}

	baseName = va( "gfx/guis/loadscreens/%s", mapName.c_str() );
	return true;
}

static void R_NormalizeLevelShotBaseName( idStr &baseName ) {
	if ( baseName.Length() <= 0 ) {
		if ( R_GetDefaultLevelShotBaseName( baseName ) ) {
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

	if ( width < 1 || height < 1 || width > 16384 || height > 16384 ) {
		common->Printf( "screenshot: bad dimensions %i x %i\n", width, height );
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

	if ( size < 1 || size > 16384 ) {
		common->Printf( "envshot: bad size %i\n", size );
		return;
	}

	// same clamp as R_ScreenShot_f: keeps the short accumulator in
	// idRenderSystemLocal::TakeScreenshot from overflowing
	if ( blends < 1 ) {
		blends = 1;
	} else if ( blends > MAX_BLENDS ) {
		blends = MAX_BLENDS;
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
		fullname = "env/";
		fullname += baseName;
		fullname += extensions[i];
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
	int			faceSize = 0;

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
	// minimum of 2 because the texel direction divides by outSize-1
	if ( outSize < 2 ) {
		outSize = 2;
	} else if ( outSize > 256 ) {
		outSize = 256;
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
		fullname = "env/";
		fullname += baseName;
		fullname += extensions[i];
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
		if ( i == 0 ) {
			faceSize = width;
		}
		// R_SampleCubeMap indexes all six faces with a single square size
		if ( width != height || width != faceSize ) {
			common->Printf( "%s is %ix%i, expected %ix%i.\n", fullname.c_str(), width, height, faceSize, faceSize );
			for ( ; i >= 0 ; i-- ) {
				Mem_Free( buffers[i] );
			}
			return;
		}
	}

	// resample with hemispherical blending
	int	samples = 1000;

	if ( outSize > idMath::INT_MAX / outSize || outSize * outSize > idMath::INT_MAX / 4 ) {
		common->Warning( "ambientshot: output size is too large %d", outSize );
		for ( i = 0 ; i < 6 ; i++ ) {
			Mem_Free( buffers[i] );
		}
		return;
	}
	byte	*outBuffer = (byte *)Mem_Alloc( outSize * outSize * 4 );

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
				fullname = "env/";
				fullname += baseName;
				fullname += "_amb";
				fullname += extensions[i];
			} else {
				fullname = "env/";
				fullname += baseName;
				fullname += "_spec";
				fullname += extensions[i];
			}
			common->Printf( "writing %s\n", fullname.c_str() );
			session->UpdateScreen();
			R_WriteTGA( fullname, outBuffer, outSize, outSize );
		}
	}

	Mem_Free( outBuffer );

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

static const char *R_GfxInfoPostAAName( const int postAA ) {
	switch ( postAA ) {
		case 0:
			return "Off";
		case 1:
			return "SMAA1xMedium";
		case 2:
			return "SMAA1xHigh";
		case 3:
			return "SMAA1xUltra";
		case 4:
			return "SMAA1xColorPrototype";
		default:
			return "Unknown";
	}
}

static int R_GfxInfoGLMaxSamples( bool &available ) {
	available = false;
	int maxSamples = 0;

#ifdef GL_MAX_SAMPLES
	if ( glConfig.isInitialized && ( GLEW_ARB_texture_multisample || GLEW_VERSION_3_2 ) ) {
		glGetIntegerv( GL_MAX_SAMPLES, &maxSamples );
		available = maxSamples > 0;
	}
#endif

	return maxSamples;
}

static void R_GfxInfoPrintAAState( void ) {
	bool maxSamplesAvailable = false;
	const int glMaxSamples = R_GfxInfoGLMaxSamples( maxSamplesAvailable );
	const int requestedMSAA = Max( 0, r_multiSamples.GetInteger() );
	const int screenFraction = idMath::ClampInt( 10, 200, r_screenFraction.GetInteger() );
	const bool supersamplingActive = screenFraction > 100;
	const bool modernVisiblePostSuppressesMSAA = R_ModernGLExecutor_ModernVisibleRequestedForPost();
	const bool textureMSAAAvailable = ( GLEW_ARB_texture_multisample || GLEW_VERSION_3_2 ) != 0;

	int effectiveMSAA = 0;
	const char *msaaReason = "off";
	if ( requestedMSAA > 1 ) {
		if ( supersamplingActive ) {
			msaaReason = "supersampling";
		} else if ( modernVisiblePostSuppressesMSAA ) {
			msaaReason = "modern-visible-post";
		} else if ( !textureMSAAAvailable ) {
			msaaReason = "texture-msaa-unavailable";
		} else {
			effectiveMSAA = requestedMSAA;
			msaaReason = "active";
			if ( maxSamplesAvailable && effectiveMSAA > glMaxSamples ) {
				effectiveMSAA = glMaxSamples;
				msaaReason = "gl-max-clamp";
			}
			if ( effectiveMSAA <= 1 ) {
				effectiveMSAA = 0;
				msaaReason = "gl-max-disabled";
			}
		}
	}

	const int postAA = idMath::ClampInt( 0, 4, r_postAA.GetInteger() );
	const char *postAAName = R_GfxInfoPostAAName( postAA );
	bool postAAEffective = postAA > 0;
	const char *postAAReason = postAAName;
	if ( postAA == 0 ) {
		postAAReason = "off";
	} else if ( r_skipPostProcess.GetBool() ) {
		postAAEffective = false;
		postAAReason = "r_skipPostProcess";
	} else if ( !glConfig.GLSLProgramAvailable ) {
		postAAEffective = false;
		postAAReason = "glsl-unavailable";
	} else if ( !glConfig.GLSL130Available ) {
		// SMAA shaders require #version 130; Apple GL 2.1 stops at GLSL 1.20.
		postAAEffective = false;
		postAAReason = "glsl-130-unavailable";
	}

	char glMaxSamplesText[32];
	if ( maxSamplesAvailable ) {
		idStr::snPrintf( glMaxSamplesText, sizeof( glMaxSamplesText ), "%d", glMaxSamples );
	} else {
		idStr::snPrintf( glMaxSamplesText, sizeof( glMaxSamplesText ), "unavailable" );
	}

	char supersamplingText[32];
	if ( supersamplingActive ) {
		idStr::snPrintf( supersamplingText, sizeof( supersamplingText ), "%d%%", screenFraction );
	} else {
		idStr::snPrintf( supersamplingText, sizeof( supersamplingText ), "off" );
	}

	common->Printf(
		"Renderer AA: MSAA requested=%d effective=%d reason=%s GL_MAX_SAMPLES=%s alphaToCoverage=%d PostAA=%d(%s) postAAEffective=%d postAAReason=%s screenFraction=%d%% supersampling=%s resolutionScaleMode=%d\n",
		requestedMSAA,
		effectiveMSAA,
		msaaReason,
		glMaxSamplesText,
		r_msaaAlphaToCoverage.GetBool() ? 1 : 0,
		postAA,
		postAAName,
		postAAEffective ? 1 : 0,
		postAAReason,
		screenFraction,
		supersamplingText,
		r_resolutionScaleMode.GetInteger() );
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
		"GL debug output: supported=%d requested=%d callback=%d synchronous=%d\n",
		R_GLDebugOutput_Available() ? 1 : 0,
		r_glDebugOutput.GetBool() ? 1 : 0,
		R_GLDebugOutput_Registered() ? 1 : 0,
		R_GLDebugOutput_Registered() && r_glDebugSynchronous.GetBool() ? 1 : 0 );
	R_GfxInfoPrintAAState();
	common->Printf(
		"Renderer features: modern=%d gl41=%d gpuDriven=%d lowOverhead=%d persistentUploads=%d DSA=%d multiBind=%d bindless=%d bindlessExperiment=%d renderGraph=%d scenePackets=%d legacyBridge=%d\n",
		glConfig.renderFeatures.modernBaseline ? 1 : 0,
		glConfig.renderFeatures.modernGL41 ? 1 : 0,
		glConfig.renderFeatures.gpuDriven ? 1 : 0,
		glConfig.renderFeatures.lowOverhead ? 1 : 0,
		glConfig.renderFeatures.persistentMappedUploads ? 1 : 0,
		glConfig.renderFeatures.directStateAccess ? 1 : 0,
		glConfig.renderFeatures.multiBind ? 1 : 0,
		glConfig.renderFeatures.bindlessTextures ? 1 : 0,
		r_rendererBindless.GetBool() ? 1 : 0,
		glConfig.renderFeatures.renderGraph ? 1 : 0,
		glConfig.renderFeatures.scenePackets ? 1 : 0,
		glConfig.renderFeatures.legacyARB2Bridge ? 1 : 0 );
	common->Printf(
		"Texture compression: S3TC/DXT=%d BC7/BPTC=%d\n",
		glConfig.textureCompressionAvailable ? 1 : 0,
		glConfig.bptcTextureCompressionAvailable ? 1 : 0 );
	{
		char capsSummary[512];
		RendererCaps_FormatSummary( glConfig.backendCaps, capsSummary, sizeof( capsSummary ) );
		common->Printf( "Renderer caps: %s\n", capsSummary );
	}
	RendererModule_PrintGfxInfo();
	R_GpuSkinning_PrintGfxInfo();
	RendererCompatibilityGates_PrintGfxInfo();
	RendererTierContract_PrintGfxInfo();
	RendererBootstrap_PrintGfxInfo();
	RendererBenchmarks_PrintGfxInfo();
	renderGpuFrameTiming_t gpuFrameTiming;
	R_RendererMetrics_GetGpuFrameTiming( gpuFrameTiming );
	const char *gpuTimingBackend = "none";
	if ( gpuFrameTiming.backend == RENDER_GPU_TIMING_BACKEND_OPENGL ) {
		gpuTimingBackend = "opengl";
	} else if ( gpuFrameTiming.backend == RENDER_GPU_TIMING_BACKEND_VULKAN ) {
		gpuTimingBackend = "vulkan";
	}
	common->Printf(
		"Renderer GPU timers: %s, cvar=%d, glPassTimerQuery=%d\n",
		R_RendererMetrics_GpuTimersAvailable() ? "available" : "unavailable",
		r_rendererGpuTimers.GetBool() ? 1 : 0,
		glConfig.backendCaps.hasTimerQuery ? 1 : 0 );
	common->Printf(
		"Renderer whole-frame GPU timing: backend=%s supported=%d valid=%d frame=%d generation=%u elapsed=%lluus latency=%u resolved=%llu unavailable=%llu dropped=%llu resets=%llu nonblocking=1\n",
		gpuTimingBackend,
		gpuFrameTiming.supported ? 1 : 0,
		gpuFrameTiming.valid ? 1 : 0,
		gpuFrameTiming.frameNumber,
		gpuFrameTiming.generation,
		gpuFrameTiming.elapsedMicroseconds,
		gpuFrameTiming.latencyFrames,
		gpuFrameTiming.resolvedSamples,
		gpuFrameTiming.unavailableSamples,
		gpuFrameTiming.droppedSamples,
		gpuFrameTiming.resetCount );
	common->Printf(
		"Renderer scene packets: legacy bridge V2, maxScenes=%d, maxPasses=%d, maxDrawPackets=%d, maxMaterialRecords=%d, maxGeometryRecords=%d, maxInstanceRecords=%d\n",
		SCENE_PACKET_MAX_SCENES,
		SCENE_PACKET_MAX_PASSES,
		SCENE_PACKET_MAX_DRAWS,
		SCENE_PACKET_MAX_MATERIAL_RECORDS,
		SCENE_PACKET_MAX_GEOMETRY_RECORDS,
		SCENE_PACKET_MAX_INSTANCE_RECORDS );
	{
		const classicDeformDomainStats_t &deform =
			R_ClassicDeformDomain_Stats();
		common->Printf(
			"Renderer shared material deform: requested=%d frameToken=%llu records=%d none=%d notApplicable=%d completed=%d empty=%d skipped=%d failed=%d unsupported=%d invalid=%d hash=%016llx\n",
			r_rendererSharedDeform.GetBool() ? 1 : 0,
			static_cast<unsigned long long>( deform.frameToken ),
			deform.records,
			deform.none,
			deform.notApplicable,
			deform.completed,
			deform.empty,
			deform.skipped,
			deform.failed,
			deform.unsupported,
			deform.invalid,
			static_cast<unsigned long long>( deform.hash ) );
	}
	R_GeometryResources_PrintGfxInfo();
	common->Printf(
		"Renderer graph: resource-backed packet graph, maxPasses=%d, maxResources=%d, maxResourceAccesses=%d\n",
		RENDER_GRAPH_MAX_PASSES,
		RENDER_GRAPH_MAX_RESOURCES,
		RENDER_GRAPH_MAX_RESOURCE_ACCESSES );
	R_RenderGraphResources_PrintGfxInfo();
	R_MaterialResourceTable_PrintGfxInfo();
	{
		const classicGuiDomainStats_t &guiDomain = R_ClassicGuiDomain_Stats();
		common->Printf(
			"Renderer shared classic GUI: rootRequested=%d inWorldRequested=%d prepared=%d valid=%d views=%d(root=%d world=%d) ready=%d fallback=%d surfaces=%d/%d noop=%d deform=%d/%d/%d passes=%d draw=%d inactive=%d activeNoop=%d hash=%016llx status=%s GL=%d/%d VK=%d/%d\n",
			r_rendererSharedGui.GetBool() ? 1 : 0,
			r_rendererSharedInWorldGui.GetBool() ? 1 : 0,
			guiDomain.prepared ? 1 : 0,
			guiDomain.frameValid ? 1 : 0,
			guiDomain.guiViews,
			guiDomain.rootViews,
			guiDomain.inWorldViews,
			guiDomain.readyViews,
			guiDomain.fallbackViews,
			guiDomain.drawableSurfaces,
			guiDomain.sourceSurfaces,
			guiDomain.noopSurfaces,
			guiDomain.materialDeformSurfaces,
			guiDomain.completedDeformSurfaces,
			guiDomain.emptyDeformSurfaces,
			guiDomain.evaluatedPasses,
			guiDomain.drawablePasses,
			guiDomain.inactivePasses,
			guiDomain.activeNoopPasses,
			static_cast<unsigned long long>( guiDomain.hash ),
			guiDomain.status,
			guiDomain.backend[CLASSIC_GUI_DOMAIN_BACKEND_GL].ownedViews,
			guiDomain.backend[CLASSIC_GUI_DOMAIN_BACKEND_GL].fallbackViews,
			guiDomain.backend[CLASSIC_GUI_DOMAIN_BACKEND_VULKAN].ownedViews,
			guiDomain.backend[CLASSIC_GUI_DOMAIN_BACKEND_VULKAN].fallbackViews );
	}
	{
		const classicCinematicPostDomainStats_t &cinematicPost =
			R_ClassicCinematicPostDomain_Stats();
		common->Printf(
			"Renderer shared cinematic/post: requested=%d prepared=%d valid=%d overflow=%d scenes=%d views=%d(root=%d post=%d nested=%d/%d nestedCinematic=%d) ready=%d fallback=%d cinematicStages=%d currentRender=%d currentDepth=%d hash=%016llx status=%s GL=%d/%d/%d/%d VK=%d/%d/%d/%d\n",
			r_rendererSharedCinematicPost.GetBool() ? 1 : 0,
			cinematicPost.prepared ? 1 : 0,
			cinematicPost.frameValid ? 1 : 0,
			cinematicPost.overflow ? 1 : 0,
			cinematicPost.sourceScenes,
			cinematicPost.views,
			cinematicPost.rootCinematicViews,
			cinematicPost.authoredPostViews,
			cinematicPost.nestedSpecialViews,
			cinematicPost.nestedSpecialTransactions,
			cinematicPost.nestedCinematicStages,
			cinematicPost.readyViews,
			cinematicPost.fallbackViews,
			cinematicPost.cinematicStages,
			cinematicPost.currentRenderStages,
			cinematicPost.currentDepthStages,
			static_cast<unsigned long long>( cinematicPost.hash ),
			cinematicPost.status,
			cinematicPost.backend[CLASSIC_CINEMATIC_POST_BACKEND_GL].ownedViews,
			cinematicPost.backend[CLASSIC_CINEMATIC_POST_BACKEND_GL].fallbackViews,
			cinematicPost.backend[CLASSIC_CINEMATIC_POST_BACKEND_GL].coverageMismatches,
			cinematicPost.backend[CLASSIC_CINEMATIC_POST_BACKEND_GL].duplicateReports,
			cinematicPost.backend[CLASSIC_CINEMATIC_POST_BACKEND_VULKAN].ownedViews,
			cinematicPost.backend[CLASSIC_CINEMATIC_POST_BACKEND_VULKAN].fallbackViews,
			cinematicPost.backend[CLASSIC_CINEMATIC_POST_BACKEND_VULKAN].coverageMismatches,
			cinematicPost.backend[CLASSIC_CINEMATIC_POST_BACKEND_VULKAN].duplicateReports );
	}
	{
		const classicSpecialFrameDomainStats_t &specialFrame =
			R_ClassicSpecialFrameDomain_Stats();
		common->Printf(
			"Renderer shared special frame: requested=%d prepared=%d valid=%d overflow=%d scenes=%d views=%d(demo=%d raven=%d) ready=%d fallback=%d effectsMask=0x%x hash=%016llx status=%s GL=%d/%d VK=%d/%d\n",
			r_rendererSharedSpecialFrame.GetBool() ? 1 : 0,
			specialFrame.prepared ? 1 : 0,
			specialFrame.frameValid ? 1 : 0,
			specialFrame.overflow ? 1 : 0,
			specialFrame.sourceScenes,
			specialFrame.views,
			specialFrame.renderDemoViews,
			specialFrame.ravenEffectsViews,
			specialFrame.readyViews,
			specialFrame.fallbackViews,
			specialFrame.specialEffectsMask,
			static_cast<unsigned long long>( specialFrame.hash ),
			specialFrame.status,
			specialFrame.backend[CLASSIC_SPECIAL_FRAME_BACKEND_GL].ownedViews,
			specialFrame.backend[CLASSIC_SPECIAL_FRAME_BACKEND_GL].fallbackViews,
			specialFrame.backend[CLASSIC_SPECIAL_FRAME_BACKEND_VULKAN].ownedViews,
			specialFrame.backend[CLASSIC_SPECIAL_FRAME_BACKEND_VULKAN].fallbackViews );
	}
	{
		const classicWorldAmbientDomainStats_t &worldAmbient =
			R_ClassicWorldAmbientDomain_Stats();
		common->Printf(
			"Renderer shared world ambient: requested=%d prepared=%d valid=%d views=%d ready=%d fallback=%d surfaces=%d/%d noop=%d deform=%d/%d/%d passes=%d draw=%d pre=%d post=%d inactive=%d activeNoop=%d hash=%016llx status=%s GL=%d/%d VK=%d/%d\n",
			r_rendererSharedWorldAmbient.GetBool() ? 1 : 0,
			worldAmbient.prepared ? 1 : 0,
			worldAmbient.frameValid ? 1 : 0,
			worldAmbient.worldViews,
			worldAmbient.readyViews,
			worldAmbient.fallbackViews,
			worldAmbient.drawableSurfaces,
			worldAmbient.sourceSurfaces,
			worldAmbient.noopSurfaces,
			worldAmbient.materialDeformSurfaces,
			worldAmbient.completedDeformSurfaces,
			worldAmbient.emptyDeformSurfaces,
			worldAmbient.evaluatedPasses,
			worldAmbient.drawablePasses,
			worldAmbient.phaseDrawablePasses[CLASSIC_WORLD_AMBIENT_PHASE_PRE_FOG],
			worldAmbient.phaseDrawablePasses[CLASSIC_WORLD_AMBIENT_PHASE_POST_FOG],
			worldAmbient.inactivePasses,
			worldAmbient.activeNoopPasses,
			static_cast<unsigned long long>( worldAmbient.hash ),
			worldAmbient.status,
			worldAmbient.backend[CLASSIC_WORLD_AMBIENT_BACKEND_GL].ownedViews,
			worldAmbient.backend[CLASSIC_WORLD_AMBIENT_BACKEND_GL].fallbackViews,
			worldAmbient.backend[CLASSIC_WORLD_AMBIENT_BACKEND_VULKAN].ownedViews,
			worldAmbient.backend[CLASSIC_WORLD_AMBIENT_BACKEND_VULKAN].fallbackViews );
		for ( int viewIndex = 0;
				viewIndex < R_ClassicWorldAmbientDomain_NumViews(); ++viewIndex ) {
			const classicWorldAmbientDomainView_t *view =
				R_ClassicWorldAmbientDomain_ViewByIndex( viewIndex );
			if ( view == NULL ) {
				continue;
			}
			const char *sourceClass = "none";
			if ( view->failure
					== CLASSIC_WORLD_AMBIENT_FAILURE_SOURCE_SURFACE_FALLBACK
					&& view->failureDetail >= 0
					&& view->failureDetail
						< CLASSIC_WORLD_AMBIENT_SOURCE_SURFACE_COUNT ) {
				sourceClass = ClassicWorldAmbientDomainSourceSurface_Name(
					static_cast<classicWorldAmbientDomainSourceSurface_t>(
						view->failureDetail ) );
			}
			common->Printf(
				"Renderer shared world ambient view[%d]: scene=%d ready=%d failure=%s detail=%d sourceClass=%s surface=%d stage=%d worldPass=%s eval=%d hash=%016llx GL=%d/%s/%d/%d+%d/%d VK=%d/%s/%d/%d+%d/%d\n",
				viewIndex,
				view->scenePacketIndex,
				view->ready ? 1 : 0,
				ClassicWorldAmbientDomainFailure_Name( view->failure ),
				view->failureDetail,
				sourceClass,
				view->failureSourceSurfaceIndex,
				view->failureSourceStageIndex,
				MaterialResourceWorldPassFailure_Name(
					view->worldPassFailure ),
				view->evaluationStatus,
				static_cast<unsigned long long>( view->hash ),
				view->backendOutcome[CLASSIC_WORLD_AMBIENT_BACKEND_GL],
				ClassicWorldAmbientDomainFailure_Name(
					view->backendFailure[CLASSIC_WORLD_AMBIENT_BACKEND_GL] ),
				view->backendFailureDetail[CLASSIC_WORLD_AMBIENT_BACKEND_GL],
				view->backendDrawnPasses[CLASSIC_WORLD_AMBIENT_BACKEND_GL]
					[CLASSIC_WORLD_AMBIENT_PHASE_PRE_FOG],
				view->backendDrawnPasses[CLASSIC_WORLD_AMBIENT_BACKEND_GL]
					[CLASSIC_WORLD_AMBIENT_PHASE_POST_FOG],
				view->backendNoopPasses[CLASSIC_WORLD_AMBIENT_BACKEND_GL],
				view->backendOutcome[CLASSIC_WORLD_AMBIENT_BACKEND_VULKAN],
				ClassicWorldAmbientDomainFailure_Name(
					view->backendFailure[CLASSIC_WORLD_AMBIENT_BACKEND_VULKAN] ),
				view->backendFailureDetail[CLASSIC_WORLD_AMBIENT_BACKEND_VULKAN],
				view->backendDrawnPasses[CLASSIC_WORLD_AMBIENT_BACKEND_VULKAN]
					[CLASSIC_WORLD_AMBIENT_PHASE_PRE_FOG],
				view->backendDrawnPasses[CLASSIC_WORLD_AMBIENT_BACKEND_VULKAN]
					[CLASSIC_WORLD_AMBIENT_PHASE_POST_FOG],
				view->backendNoopPasses[CLASSIC_WORLD_AMBIENT_BACKEND_VULKAN] );
		}
	}
	{
		const classicInteractionDomainStats_t &interaction =
			R_ClassicInteractionDomain_Stats();
		common->Printf(
			"Renderer shared interaction: requested=%d prepared=%d valid=%d views=%d ready=%d fallback=%d lights=%d surfaces=%d primitives=%d draw=%d noop=%d shadow=%d/%d/%d+%d volumes=%d+%d maps=%d+%d modes=%d+%d csm=%d stages=%d/%d+%d/%d receivers=%d/%d/%d deform=%d/%d/%d hash=%016llx status=%s GL=%d/%d/%d VK=%d/%d/%d\n",
			r_rendererSharedWorldInteraction.GetBool() ? 1 : 0,
			interaction.prepared ? 1 : 0,
			interaction.frameValid ? 1 : 0,
			interaction.interactionViews,
			interaction.readyViews,
			interaction.fallbackViews,
			interaction.lights,
			interaction.surfaces,
			interaction.primitives,
			interaction.drawablePrimitives,
			interaction.noopPrimitives,
			interaction.shadowLights,
			interaction.shadowCasters,
			interaction.drawableShadowCasters,
			interaction.noopShadowCasters,
			interaction.logicalVolumeDraws,
			interaction.preloadVolumeDraws,
			interaction.shadowMapPasses,
			interaction.hybridShadowPasses,
			interaction.projectedShadowMapPasses,
			interaction.pointShadowMapPasses,
			interaction.csmShadowMapPasses,
			interaction.activeLightStages,
			interaction.inactiveLightStages,
			interaction.activeSurfaceStages,
			interaction.inactiveSurfaceStages,
			interaction.receiverSurfaces[CLASSIC_INTERACTION_RECEIVER_LOCAL],
			interaction.receiverSurfaces[CLASSIC_INTERACTION_RECEIVER_GLOBAL],
			interaction.receiverSurfaces[CLASSIC_INTERACTION_RECEIVER_TRANSLUCENT],
			interaction.materialDeformReceivers,
			interaction.completedDeformCasters,
			interaction.emptyDeformCasters,
			static_cast<unsigned long long>( interaction.hash ),
			interaction.status,
			interaction.backend[CLASSIC_INTERACTION_BACKEND_GL].ownedViews,
			interaction.backend[CLASSIC_INTERACTION_BACKEND_GL].fallbackViews,
			interaction.backend[CLASSIC_INTERACTION_BACKEND_GL].coverageMismatches,
			interaction.backend[CLASSIC_INTERACTION_BACKEND_VULKAN].ownedViews,
			interaction.backend[CLASSIC_INTERACTION_BACKEND_VULKAN].fallbackViews,
			interaction.backend[CLASSIC_INTERACTION_BACKEND_VULKAN].coverageMismatches );
		for ( int viewIndex = 0;
				viewIndex < R_ClassicInteractionDomain_NumViews(); ++viewIndex ) {
			const classicInteractionDomainView_t *view =
				R_ClassicInteractionDomain_ViewByIndex( viewIndex );
			if ( view == NULL ) {
				continue;
			}
			common->Printf(
				"Renderer shared interaction view[%d]: scene=%d pass=%d shadowPass=%d/%d mode=%s ready=%d failure=%s detail=%d drawPacket=%d light=%d receiver=%d stage=%d lights=%d surfaces=%d primitives=%d/%d noop=%d shadow=%d/%d/%d+%d volumes=%d+%d maps=%d+%d modes=%d+%d csm=%d hash=%016llx GL=%d/%s/%d/%d+%d/%d+%d+%d+%d/%d+%d VK=%d/%s/%d/%d+%d/%d+%d+%d+%d/%d+%d\n",
				viewIndex,
				view->scenePacketIndex,
				view->interactionPassPacketIndex,
				view->stencilShadowPassPacketIndex,
				view->shadowMapPassPacketIndex,
				ClassicInteractionDomainShadowMode_Name( view->shadowMode ),
				view->ready ? 1 : 0,
				ClassicInteractionDomainFailure_Name( view->failure ),
				view->failureDetail,
				view->failureDrawPacketIndex,
				view->failureLightOrdinal,
				view->failureReceiverOrdinal,
				view->failureStageIndex,
				view->lightCount,
				view->surfaceCount,
				view->primitiveCount,
				view->drawablePrimitiveCount,
				view->noopPrimitiveCount,
				view->shadowLightCount,
				view->shadowCasterCount,
				view->drawableShadowCasterCount,
				view->noopShadowCasterCount,
				view->logicalVolumeDrawCount,
				view->preloadVolumeDrawCount,
				view->shadowMapPassCount,
				view->hybridShadowPassCount,
				view->projectedShadowMapPassCount,
				view->pointShadowMapPassCount,
				view->csmShadowMapPassCount,
				static_cast<unsigned long long>( view->hash ),
				view->backendOutcome[CLASSIC_INTERACTION_BACKEND_GL],
				ClassicInteractionDomainFailure_Name(
					view->backendFailure[CLASSIC_INTERACTION_BACKEND_GL] ),
				view->backendFailureDetail[CLASSIC_INTERACTION_BACKEND_GL],
				view->backendDrawnPrimitives[CLASSIC_INTERACTION_BACKEND_GL],
				view->backendNoopPrimitives[CLASSIC_INTERACTION_BACKEND_GL],
				view->backendShadowCasters[CLASSIC_INTERACTION_BACKEND_GL],
				view->backendNoopShadowCasters[CLASSIC_INTERACTION_BACKEND_GL],
				view->backendLogicalVolumeDraws[CLASSIC_INTERACTION_BACKEND_GL],
				view->backendPreloadVolumeDraws[CLASSIC_INTERACTION_BACKEND_GL],
				view->backendShadowMapPasses[CLASSIC_INTERACTION_BACKEND_GL],
				view->backendHybridPasses[CLASSIC_INTERACTION_BACKEND_GL],
				view->backendOutcome[CLASSIC_INTERACTION_BACKEND_VULKAN],
				ClassicInteractionDomainFailure_Name(
					view->backendFailure[CLASSIC_INTERACTION_BACKEND_VULKAN] ),
				view->backendFailureDetail[CLASSIC_INTERACTION_BACKEND_VULKAN],
				view->backendDrawnPrimitives[CLASSIC_INTERACTION_BACKEND_VULKAN],
				view->backendNoopPrimitives[CLASSIC_INTERACTION_BACKEND_VULKAN],
				view->backendShadowCasters[CLASSIC_INTERACTION_BACKEND_VULKAN],
				view->backendNoopShadowCasters[CLASSIC_INTERACTION_BACKEND_VULKAN],
				view->backendLogicalVolumeDraws[CLASSIC_INTERACTION_BACKEND_VULKAN],
				view->backendPreloadVolumeDraws[CLASSIC_INTERACTION_BACKEND_VULKAN],
				view->backendShadowMapPasses[CLASSIC_INTERACTION_BACKEND_VULKAN],
				view->backendHybridPasses[CLASSIC_INTERACTION_BACKEND_VULKAN] );
			for ( int lightIndex = 0; lightIndex < view->lightCount;
					++lightIndex ) {
				const classicInteractionDomainLight_t *light =
					R_ClassicInteractionDomain_ViewLight( *view, lightIndex );
				if ( light == NULL ) {
					continue;
				}
				for ( int receiverIndex = CLASSIC_INTERACTION_RECEIVER_LOCAL;
						receiverIndex <= CLASSIC_INTERACTION_RECEIVER_GLOBAL;
						++receiverIndex ) {
					const classicInteractionDomainReceiver_t receiver =
						static_cast<classicInteractionDomainReceiver_t>(
							receiverIndex );
					const classicInteractionDomainShadowMapPass_t *mapPass =
						R_ClassicInteractionDomain_LightShadowMapPass(
							*light, receiver );
					if ( mapPass == NULL ) {
						continue;
					}
					common->Printf(
						"Renderer shared interaction map view[%d] light=%d receiver=%s mode=%s class=%s cascades=%d alias=%d plan=%016llx generation=%u casters=%d+%d features=%d+%d+%d+%d hash=%016llx\n",
						viewIndex, lightIndex,
						ClassicInteractionDomainReceiver_Name( receiver ),
						ClassicInteractionDomainShadowMode_Name(
							mapPass->mode ),
						R_ShadowMapLightClassName( mapPass->lightClass ),
						mapPass->lightClass == SHADOWMAP_LIGHT_POINT
							? 6 : mapPass->projected.state.cascadeCount,
						mapPass->resourceAlias ? 1 : 0,
						static_cast<unsigned long long>(
							mapPass->resourcePlanId ),
						mapPass->resourceGeneration,
						mapPass->mappedCasterCount,
						mapPass->supplementCasterCount,
						mapPass->hasStaticCasters ? 1 : 0,
						mapPass->hasDynamicCasters ? 1 : 0,
						mapPass->hasAlphaCasters ? 1 : 0,
						mapPass->hasTranslucentCasters ? 1 : 0,
						static_cast<unsigned long long>( mapPass->hash ) );
				}
			}
		}
	}
	{
		const classicFogBlendDomainStats_t &fogBlend =
			R_ClassicFogBlendDomain_Stats();
		common->Printf(
			"classicFogBlendDomain requested=%d prepared=%d frameValid=%d overflow=%d status=%s views=%d ready=%d fallback=%d lights=%d fog=%d blend=%d noopLights=%d surfaces=%d global=%d local=%d deformReceivers=%d stages=%d active=%d inactive=%d noopStages=%d primitives=%d fogReceivers=%d fogCaps=%d blendDraws=%d noop=%d textures=%d hash=%016llx\n",
			r_rendererSharedWorldFogBlend.GetBool() ? 1 : 0,
			fogBlend.prepared ? 1 : 0,
			fogBlend.frameValid ? 1 : 0,
			fogBlend.overflow ? 1 : 0,
			fogBlend.status,
			fogBlend.fogBlendViews,
			fogBlend.readyViews,
			fogBlend.fallbackViews,
			fogBlend.lights,
			fogBlend.fogLights,
			fogBlend.blendLights,
			fogBlend.noopLights,
			fogBlend.surfaces,
			fogBlend.receiverSurfaces[
				CLASSIC_FOG_BLEND_RECEIVER_GLOBAL ],
			fogBlend.receiverSurfaces[
				CLASSIC_FOG_BLEND_RECEIVER_LOCAL ],
			fogBlend.materialDeformReceivers,
			fogBlend.lightStages,
			fogBlend.activeLightStages,
			fogBlend.inactiveLightStages,
			fogBlend.noopLightStages,
			fogBlend.primitives,
			fogBlend.fogReceiverPrimitives,
			fogBlend.fogFrustumPrimitives,
			fogBlend.blendPrimitives,
			fogBlend.noopPrimitives,
			fogBlend.textures,
			static_cast<unsigned long long>( fogBlend.hash ) );
		for ( int backendIndex = 0;
				backendIndex < CLASSIC_FOG_BLEND_BACKEND_COUNT;
				++backendIndex ) {
			const classicFogBlendDomainBackend_t backend =
				static_cast<classicFogBlendDomainBackend_t>( backendIndex );
			const classicFogBlendDomainBackendCoverage_t &coverage =
				fogBlend.backend[ backendIndex ];
			common->Printf(
				"classicFogBlendDomain backend=%s ownedViews=%d fallbackViews=%d ownedFogReceivers=%d ownedFogCaps=%d ownedBlend=%d ownedNoops=%d ownedNoopStages=%d ownedNoopLights=%d mismatches=%d duplicate=%d untracked=%d\n",
				ClassicFogBlendDomainBackend_Name( backend ),
				coverage.ownedViews,
				coverage.fallbackViews,
				coverage.ownedFogReceiverPrimitives,
				coverage.ownedFogFrustumPrimitives,
				coverage.ownedBlendPrimitives,
				coverage.ownedNoopPrimitives,
				coverage.ownedNoopLightStages,
				coverage.ownedNoopLights,
				coverage.coverageMismatches,
				coverage.duplicateReports,
				coverage.untrackedFallbacks );
		}
		for ( int viewIndex = 0;
				viewIndex < R_ClassicFogBlendDomain_NumViews(); ++viewIndex ) {
			const classicFogBlendDomainView_t *view =
				R_ClassicFogBlendDomain_ViewByIndex( viewIndex );
			if ( view == NULL ) {
				continue;
			}
			common->Printf(
				"classicFogBlendDomain view[%d] scene=%d pass=%d ready=%d failure=%s detail=%d drawPacket=%d light=%d receiver=%d stage=%d lights=%d/%d+%d surfaces=%d stages=%d/%d+%d primitives=%d drawable=%d fogReceivers=%d fogCaps=%d blendDraws=%d noop=%d noopStages=%d noopLights=%d hash=%016llx GL=%d/%s/%d Vulkan=%d/%s/%d\n",
				viewIndex,
				view->scenePacketIndex,
				view->fogBlendPassPacketIndex,
				view->ready ? 1 : 0,
				ClassicFogBlendDomainFailure_Name( view->failure ),
				view->failureDetail,
				view->failureDrawPacketIndex,
				view->failureLightOrdinal,
				view->failureReceiverOrdinal,
				view->failureStageIndex,
				view->lightCount,
				view->fogLightCount,
				view->blendLightCount,
				view->surfaceCount,
				view->lightStageCount,
				view->activeLightStageCount,
				view->inactiveLightStageCount,
				view->primitiveCount,
				view->drawablePrimitiveCount,
				view->fogReceiverPrimitiveCount,
				view->fogFrustumPrimitiveCount,
				view->blendPrimitiveCount,
				view->noopPrimitiveCount,
				view->noopLightStageCount,
				view->noopLightCount,
				static_cast<unsigned long long>( view->hash ),
				view->backendOutcome[ CLASSIC_FOG_BLEND_BACKEND_GL ],
				ClassicFogBlendDomainFailure_Name(
					view->backendFailure[ CLASSIC_FOG_BLEND_BACKEND_GL ] ),
				view->backendFailureDetail[ CLASSIC_FOG_BLEND_BACKEND_GL ],
				view->backendOutcome[ CLASSIC_FOG_BLEND_BACKEND_VULKAN ],
				ClassicFogBlendDomainFailure_Name(
					view->backendFailure[
						CLASSIC_FOG_BLEND_BACKEND_VULKAN ] ),
				view->backendFailureDetail[
					CLASSIC_FOG_BLEND_BACKEND_VULKAN ] );
		}
	}
	{
		const classicSubviewDomainStats_t &subview =
			R_ClassicSubviewDomain_Stats();
		common->Printf(
			"classicSubviewDomain requested=%d prepared=%d frameValid=%d overflow=%d status=%s scenes=%d subviews=%d captures=%d ready=%d fallback=%d nested=%d nestedTransactions=%d maxNestingDepth=%d directMirror=%d remote=%d mirror=%d reflection=%d refraction=%d xray=%d colorCubemap=%d depth2D=%d depthCubemap=%d hash=%016llx\n",
			r_rendererSharedSubview.GetBool() ? 1 : 0,
			subview.prepared ? 1 : 0,
			subview.frameValid ? 1 : 0,
			subview.overflow ? 1 : 0,
			subview.status,
			subview.sourceScenes,
			subview.subviewScenes,
			subview.capturePackets,
			subview.readyViews,
			subview.fallbackViews,
			subview.nestedViews,
			subview.nestedTransactions,
			subview.maxNestingDepth,
			subview.directMirrorViews,
			subview.remoteCameraViews,
			subview.mirrorViews,
			subview.reflectionViews,
			subview.refractionViews,
			subview.xrayViews,
			subview.colorCubemapCaptures,
			subview.depth2DCaptures,
			subview.depthCubemapCaptures,
			static_cast<unsigned long long>( subview.hash ) );
		for ( int backendIndex = 0;
				backendIndex < CLASSIC_SUBVIEW_DOMAIN_BACKEND_COUNT;
				++backendIndex ) {
			const classicSubviewDomainBackend_t backend =
				static_cast<classicSubviewDomainBackend_t>( backendIndex );
			const classicSubviewDomainBackendCoverage_t &coverage =
				subview.backend[ backendIndex ];
			common->Printf(
				"classicSubviewDomain backend=%s ownedViews=%d fallbackViews=%d nestedOwned=%d nestedTransactions=%d nestedFallback=%d nestedFallbackTransactions=%d directMirror=%d remote=%d mirror=%d reflection=%d refraction=%d xray=%d colorCubemap=%d depth2D=%d depthCubemap=%d mismatches=%d duplicate=%d untracked=%d\n",
				ClassicSubviewDomainBackend_Name( backend ),
				coverage.ownedViews,
				coverage.fallbackViews,
				coverage.ownedNestedViews,
				coverage.ownedNestedTransactions,
				coverage.fallbackNestedViews,
				coverage.fallbackNestedTransactions,
				coverage.ownedDirectMirrorViews,
				coverage.ownedRemoteCameraViews,
				coverage.ownedMirrorViews,
				coverage.ownedReflectionViews,
				coverage.ownedRefractionViews,
				coverage.ownedXrayViews,
				coverage.ownedColorCubemapCaptures,
				coverage.ownedDepth2DCaptures,
				coverage.ownedDepthCubemapCaptures,
				coverage.coverageMismatches,
				coverage.duplicateReports,
				coverage.untrackedFallbacks );
		}
		for ( int viewIndex = 0;
				viewIndex < R_ClassicSubviewDomain_NumViews(); ++viewIndex ) {
			const classicSubviewDomainView_t *view =
				R_ClassicSubviewDomain_ViewByIndex( viewIndex );
			if ( view == NULL ) {
				continue;
			}
			common->Printf(
				"classicSubviewDomain view[%d] scene=%d parent=%d parentView=%d root=%d depth=%d subtree=%d capture=%d kind=%s captureType=%s ready=%d failure=%s detail=%d captureImage=%s rect=%d,%d %dx%d cube=%d depth=%d hash=%016llx GL=%d/%s/%d Vulkan=%d/%s/%d\n",
				viewIndex,
				view->scenePacketIndex,
				view->parentScenePacketIndex,
				view->parentViewIndex,
				view->rootViewIndex,
				view->nestingDepth,
				view->subtreeViewCount,
				view->capturePacketIndex,
				ClassicSubviewDomainKind_Name( view->kind ),
				ClassicSubviewDomainCaptureType_Name( view->captureType ),
				view->ready ? 1 : 0,
				ClassicSubviewDomainFailure_Name( view->failure ),
				view->failureDetail,
				view->captureImage != NULL ? view->captureImage->GetName() : "<none>",
				view->captureX,
				view->captureY,
				view->captureWidth,
				view->captureHeight,
				view->captureCubeFace,
				view->captureCopyDepth ? 1 : 0,
				static_cast<unsigned long long>( view->hash ),
				view->backendOutcome[ CLASSIC_SUBVIEW_DOMAIN_BACKEND_GL ],
				ClassicSubviewDomainFailure_Name(
					view->backendFailure[ CLASSIC_SUBVIEW_DOMAIN_BACKEND_GL ] ),
				view->backendFailureDetail[ CLASSIC_SUBVIEW_DOMAIN_BACKEND_GL ],
				view->backendOutcome[ CLASSIC_SUBVIEW_DOMAIN_BACKEND_VULKAN ],
				ClassicSubviewDomainFailure_Name(
					view->backendFailure[
						CLASSIC_SUBVIEW_DOMAIN_BACKEND_VULKAN ] ),
				view->backendFailureDetail[
					CLASSIC_SUBVIEW_DOMAIN_BACKEND_VULKAN ] );
		}
	}
	common->Printf(
		"Modern quality: master=%d pbr=%d probes=%d clusteredDecals=%d froxel=%d ssr=%d ssgi=%d (master=0 is complete Milestone-F rollback)\n"
		"PBR materials: parser=1 modernLighting=1 effective=%d generatedLegacyFallback=%d inferLegacy=%d debug=%d\n",
		r_rendererModernQuality.GetBool() ? 1 : 0,
		r_pbrMaterials.GetBool() ? 1 : 0,
		r_rendererReflectionProbes.GetBool() ? 1 : 0,
		r_rendererClusteredDecals.GetBool() ? 1 : 0,
		r_rendererFroxelVolumetrics.GetBool() ? 1 : 0,
		r_rendererSSR.GetBool() ? 1 : 0,
		r_rendererSSGI.GetBool() ? 1 : 0,
		( r_rendererModernQuality.GetBool() && r_pbrMaterials.GetBool() ) ? 1 : 0,
		r_pbrGeneratedLegacyFallback.GetBool() ? 1 : 0,
		r_pbrInferFromLegacyMaterials.GetBool() ? 1 : 0,
		r_pbrDebug.GetInteger() );
	R_ModernGLExecutor_PrintGfxInfo();
	R_ModernClusteredLighting_PrintGfxInfo();
	R_ModernShadowPlanner_PrintGfxInfo();
	{
		const rendererUploadStats_t &uploadStats = R_RendererUpload_Stats();
		common->Printf(
			"Renderer upload manager: frameStream=%s, staticAllocator=%d, buffers=%d index=%d, ring=%dKB, persistent=%d, mapRangeFallback=%d, staticLive=%d/%dKB, fences=%d/%d submitFailures=%d waits=%d timeouts=%d fallbacks=%d waitFailures=%d, legacyBridge=%d\n",
			uploadStats.dynamicFrameBridge ? "enabled" : "disabled",
			uploadStats.staticBufferAllocator ? 1 : 0,
			uploadStats.ringBufferCount,
			uploadStats.frameBufferIndex,
			uploadStats.ringSizeBytes / 1024,
			uploadStats.persistentMapped ? 1 : 0,
			uploadStats.mapRangeFallback ? 1 : 0,
			uploadStats.staticBuffersLive,
			uploadStats.staticBytesLive / 1024,
			uploadStats.frameFencesSubmitted,
			uploadStats.frameFencesRetired,
			uploadStats.frameFenceSubmissionFailures,
			uploadStats.frameFenceWaits,
			uploadStats.frameFenceTimeouts,
			uploadStats.frameFenceFallbacks,
			uploadStats.frameFenceWaitFailures,
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
	const bool appleGL21AutomaticInteractions =
		( RendererDriverQuirks_LastReport().flags & RENDERER_DRIVER_QUIRK_DISABLE_ARB2_INTERACTIONS ) != 0 &&
		r_appleARB2Interactions.GetInteger() == 0 && !glConfig.disableARB2Interactions;
	if ( appleGL21AutomaticInteractions ) {
		common->Printf( "Apple GL 2.1 automatic stock GLSL interactions active with simple ARB per-surface fallback\n" );
	}
	if ( glConfig.preferSimpleInteraction ) {
		common->Printf( appleGL21AutomaticInteractions
			? "Simple ARB interaction per-surface fallback armed for this renderer\n"
			: "Simple ARB interaction compatibility mode preferred for this renderer\n" );
	}
	if ( glConfig.disableARB2Interactions ) {
		common->Printf( "ARB2 light interaction compatibility bypass active for this renderer\n" );
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
	
	// the shadow pass gates on the core GL 2.0 glStencilOpSeparate entry point
	// plus wrap stencil ops, not on the legacy vendor extension strings
	bool tss = glStencilOpSeparate != NULL && tr.stencilIncr == GL_INCR_WRAP_EXT;

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
static void R_ClearActiveRenderTextures( void ) {
	backEnd.renderTexture = NULL;
	backEnd.feedbackRenderTexture = NULL;
	tr.activeRenderTexture = NULL;
}

static void R_ShutdownRenderTargetsBeforeImagePurge( void ) {
	R_ClearActiveRenderTextures();
	if ( glConfig.isInitialized ) {
		idRenderTexture::BindNull();
	}

	// Render textures keep pointers to idImage objects. Destroy/reset every
	// GL render-target owner before image purge or full renderer teardown can
	// invalidate those images.
	tr.ShutdownSpecialEffects();
	RB_ShutdownScenePostProcess();
	RB_ShutdownShadowMapResources();
	tr.ProcessPendingRenderTextureDeletes();
	R_ClearActiveRenderTextures();
}

static void R_PerformFullVidRestart( bool forceWindow ) {
	R_ShutdownRenderTargetsBeforeImagePurge();

	// Input is tied to the native window/context lifecycle.
	Sys_ShutdownInput();

	// Scalable GUI fonts are backed by scratch images.  Release their cached
	// faces and reset the one-shot console-atlas guard before those images lose
	// their device storage; the UI refresh below will rasterise them for the new
	// viewport instead of retaining stale metrics and empty texture objects.
	R_DoneFreeType();

	// Force image/object handles to rebuild against the new context.
	globalImages->PurgeAllImages();
	R_ShutdownFrameData();
	R_RendererMetrics_ShutdownGpuTimers();
	R_ClassicGuiDomain_ResetFrame();
	R_ClassicCinematicPostDomain_ResetFrame();
	R_ClassicSpecialFrameDomain_ResetFrame();
	R_ClassicWorldAmbientDomain_ResetFrame();
	R_ClassicInteractionDomain_ResetFrame();
	R_ClassicFogBlendDomain_ResetFrame();
	R_ClassicSubviewDomain_ResetFrame();
	R_MaterialResourceTable_Shutdown();
	R_RenderGraphResources_Shutdown();
	R_ModernGLExecutor_Shutdown();
	R_GpuSkinning_ContractShutdown();
	R_RendererUpload_Shutdown();
	RendererBootstrap_Shutdown();
	R_PurgeFramebufferCopyFBOs();
	R_GLDebugOutput_Shutdown();
	GLimp_Shutdown();
	glConfig.isInitialized = false;

	const bool latchedFullscreen = cvarSystem->GetCVarBool( "r_fullscreen" );
	if ( forceWindow ) {
		cvarSystem->SetCVarBool( "r_fullscreen", false );
	}

	R_InitOpenGL();
	cvarSystem->SetCVarBool( "r_fullscreen", latchedFullscreen );
	R_ClearActiveRenderTextures();

	globalImages->ReloadImages( true );

	R_InitFreeType();
	R_RefreshConsoleFontAtlas();
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
	R_RendererMetrics_ResetGpuFrameTiming( "vid_restart" );

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
		parms.hiddenWindow = r_hiddenWindow.GetBool();
		parms.fullScreen = ( forceWindow || parms.hiddenWindow ) ? false : r_fullscreen.GetBool();
		if ( parms.fullScreen ) {
			R_GetModeInfo( &parms.width, &parms.height, r_mode.GetInteger() );
		} else {
			R_GetWindowedModeInfo( &parms.width, &parms.height );
		}
		parms.borderless = !parms.hiddenWindow && !parms.fullScreen && r_borderless.GetBool();
		parms.displayHz = r_displayRefresh.GetInteger();
		parms.multiSamples = r_multiSamples.GetInteger();
		parms.stereo = false;
		if ( !GLimp_SetScreenParms( parms ) ) {
			common->Printf( "^3vid_restart partial failed, retrying full restart\n" );
			R_PerformFullVidRestart( forceWindow );
		} else {
			(void)R_ClearPendingGLErrors();
			// The context survived, but a mode change can alter the UI viewport.
			// Re-rasterise the fixed-cell sheet at the new display scale; cached
			// GUI fonts refresh lazily from videoRestartCount below.
			R_RefreshConsoleFontAtlas();
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
		common->Printf( "Renderer default material: using generated internal fallback for missing _default material decl\n" );
		tr.defaultMaterial = declManager->FindMaterial( "_default" );
	}
	if ( !tr.defaultMaterial ) {
		common->FatalError( "_default material fallback not available" );
	}
	declManager->FindMaterial( "_default", false );

	// needed by R_DeriveLightData
	declManager->FindMaterial( "lights/defaultPointLight" );
	declManager->FindMaterial( "lights/defaultProjectedLight" );
	declManager->FindMaterial( "gfx/lights/flashlight" );

	// Session wipes are used during map transitions after the level-load
	// precache window has closed.
	declManager->FindMaterial( "gfx/wipes/fade" );
	declManager->FindMaterial( "gfx/wipes/fade_blend" );
}


/*
=================
R_SizeUp_f

Keybinding command
=================
*/
static void R_SizeUp_f( const idCmdArgs &args ) {
	if ( r_screenFraction.GetInteger() + 10 > 200 ) {
		r_screenFraction.SetInteger( 200 );
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
==================
R_MigrateLegacyShadowMapContactQuality

Renderer cvars are registered by Init before the archived config and autoexec
files run. Perform value-based migrations at device startup after those files
and the initial StartupVariable pass, or an existing profile would be marked
migrated while the compiled defaults were still visible. Common's later
AddStartupCommands replay intentionally leaves explicit command-line +set
values authoritative over the migrated archive.
==================
*/
static void R_MigrateLegacyShadowMapContactQuality( void ) {
	if ( !r_shadowMapContactQualityMigrated.GetBool() ) {
		// Preserve customized profiles. Only replace the complete legacy tuple,
		// whose wide filters and caster slope offset blurred contact shadows and
		// amplified the old far-shell detachment.
		const bool legacyShadowQualityDefaults =
			idMath::Fabs( r_shadowMapFilterRadius.GetFloat() - 2.0f ) < 0.0001f &&
			idMath::Fabs( r_shadowMapPointFilterRadius.GetFloat() - 2.5f ) < 0.0001f &&
			r_shadowMapFilterTaps.GetInteger() == 13 &&
			r_shadowMapPointFilterTaps.GetInteger() == 13 &&
			idMath::Fabs( r_shadowMapPolygonFactor.GetFloat() - 0.75f ) < 0.0001f;
		if ( legacyShadowQualityDefaults ) {
			common->Printf( "Migrating legacy shadow-map defaults: restoring contact detail and balanced filtering\n" );
			r_shadowMapFilterRadius.SetFloat( 0.75f );
			r_shadowMapPointFilterRadius.SetFloat( 1.0f );
			r_shadowMapFilterTaps.SetInteger( 9 );
			r_shadowMapPointFilterTaps.SetInteger( 9 );
			r_shadowMapPolygonFactor.SetFloat( 0.25f );
		}
		r_shadowMapContactQualityMigrated.SetBool( true );
	}
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
	cmdSystem->AddCommand( "guiTraceProbe", R_GuiTraceProbe_f, CMD_FL_RENDERER, "diagnostic: GuiTrace every in-world gui surface and report hits" );
	cmdSystem->AddCommand( "screenshot", R_ScreenShot_f, CMD_FL_RENDERER, "takes a screenshot" );
	cmdSystem->AddCommand( "levelshot", R_LevelShot_f, CMD_FL_RENDERER | CMD_FL_CHEAT, "captures a 5-tile levelshot set" );
	cmdSystem->AddCommand( "envshot", R_EnvShot_f, CMD_FL_RENDERER, "takes an environment shot" );
	cmdSystem->AddCommand( "makeAmbientMap", R_MakeAmbientMap_f, CMD_FL_RENDERER|CMD_FL_CHEAT, "makes an ambient map" );
	cmdSystem->AddCommand( "benchmark", R_Benchmark_f, CMD_FL_RENDERER, "benchmark" );
	cmdSystem->AddCommand( "gfxInfo", GfxInfo_f, CMD_FL_RENDERER, "show graphics info" );
	cmdSystem->AddCommand( "rendererTierSelfTest", R_RendererTierSelfTest_f, CMD_FL_RENDERER, "run renderer tier-selection self tests" );
	cmdSystem->AddCommand( "rendererTierContractSelfTest", R_RendererTierContractSelfTest_f, CMD_FL_RENDERER, "run renderer tier workload-contract self tests" );
	cmdSystem->AddCommand( "rendererContextLadderSelfTest", R_RendererContextLadderSelfTest_f, CMD_FL_RENDERER, "run renderer context ladder self tests" );
	cmdSystem->AddCommand( "rendererCompatibilityGatesSelfTest", R_RendererCompatibilityGatesSelfTest_f, CMD_FL_RENDERER, "run renderer driver-quirk and fallback-gate self tests" );
	cmdSystem->AddCommand( "rendererBenchmarkSelfTest", R_RendererBenchmarkSelfTest_f, CMD_FL_RENDERER, "run renderer benchmark capture and percentile self tests" );
	cmdSystem->AddCommand( "rendererDefaultPromotionSelfTest", R_RendererDefaultPromotionSelfTest_f, CMD_FL_RENDERER, "run renderer default-promotion gate self tests" );
	cmdSystem->AddCommand( "rendererDefaultSafetySelfTest", R_RendererDefaultSafetySelfTest_f, CMD_FL_RENDERER, "run renderer conservative-default safety self tests" );
	cmdSystem->AddCommand( "rendererBenchmarkCapture", R_RendererBenchmarkCapture_f, CMD_FL_RENDERER, "print the latest renderer benchmark capture summary" );
	cmdSystem->AddCommand( "rendererUploadSelfTest", R_RendererUploadSelfTest_f, CMD_FL_RENDERER, "run renderer upload stream self tests" );
	cmdSystem->AddCommand( "rendererGpuSkinningSelfTest", R_RendererGpuSkinningSelfTest_f, CMD_FL_RENDERER, "run renderer GPU skinning contract self tests" );
	cmdSystem->AddCommand( "rendererContractsSelfTest", R_RendererContractsSelfTest_f, CMD_FL_RENDERER, "run renderer layout and buffer contract self tests" );
	cmdSystem->AddCommand( "rendererGpuTimerSelfTest", R_RendererGpuTimerSelfTest_f, CMD_FL_RENDERER, "run renderer GPU timer query self tests" );
	cmdSystem->AddCommand( "rendererTemporalPresentationStatus", R_TemporalPresentation_PrintStatus_f, CMD_FL_RENDERER, "report dynamic-resolution and temporal-history state" );
	cmdSystem->AddCommand( "rendererScenePacketSelfTest", R_RendererScenePacketSelfTest_f, CMD_FL_RENDERER, "run renderer front-end scene-packet self tests" );
	cmdSystem->AddCommand( "rendererRenderGraphSelfTest", R_RendererRenderGraphSelfTest_f, CMD_FL_RENDERER, "run renderer resource-graph self tests" );
	cmdSystem->AddCommand( "rendererRenderGraphResourceSelfTest", R_RendererRenderGraphResourceSelfTest_f, CMD_FL_RENDERER, "run renderer graph resource owner self tests" );
	cmdSystem->AddCommand( "rendererRenderGraphResourceDump", R_RendererRenderGraphResourceDump_f, CMD_FL_RENDERER, "dump the latest renderer graph resource handles" );
	cmdSystem->AddCommand( "rendererMaterialResourceTableSelfTest", R_RendererMaterialResourceTableSelfTest_f, CMD_FL_RENDERER, "run renderer material resource-table self tests" );
	cmdSystem->AddCommand( "rendererClassicGuiDomainSelfTest", R_RendererClassicGuiDomainSelfTest_f, CMD_FL_RENDERER, "run backend-neutral classic GUI whole-view contract self tests" );
	cmdSystem->AddCommand( "rendererClassicCinematicPostDomainSelfTest", R_RendererClassicCinematicPostDomainSelfTest_f, CMD_FL_RENDERER, "run sealed classic cinematic and authored post transaction self tests" );
	cmdSystem->AddCommand( "rendererClassicSpecialFrameDomainSelfTest", R_RendererClassicSpecialFrameDomainSelfTest_f, CMD_FL_RENDERER, "run sealed render-demo and Raven special-frame transaction self tests" );
	cmdSystem->AddCommand( "rendererClassicWorldAmbientDomainSelfTest", R_RendererClassicWorldAmbientDomainSelfTest_f, CMD_FL_RENDERER, "run backend-neutral classic world ambient whole-view contract self tests" );
	cmdSystem->AddCommand( "rendererClassicInteractionDomainSelfTest", R_RendererClassicInteractionDomainSelfTest_f, CMD_FL_RENDERER, "run backend-neutral classic interaction whole-view contract self tests" );
	cmdSystem->AddCommand( "rendererClassicFogBlendDomainSelfTest", R_RendererClassicFogBlendDomainSelfTest_f, CMD_FL_RENDERER, "run backend-neutral classic fog/blend whole-view contract self tests" );
	cmdSystem->AddCommand( "rendererClassicSubviewDomainSelfTest", R_RendererClassicSubviewDomainSelfTest_f, CMD_FL_RENDERER, "run backend-neutral capture-backed classic subview transaction self tests" );
	cmdSystem->AddCommand( "rendererClassicDeformDomainSelfTest", R_RendererClassicDeformDomainSelfTest_f, CMD_FL_RENDERER, "run sealed classic material-deform contract self tests" );
	cmdSystem->AddCommand( "rendererPBRMaterialSelfTest", R_RendererPBRMaterialSelfTest_f, CMD_FL_RENDERER, "run PBR material parser and classic fallback self tests" );
	cmdSystem->AddCommand( "rendererMaterialResourceTableDump", R_RendererMaterialResourceTableDump_f, CMD_FL_RENDERER, "dump the latest renderer material resource table" );
	cmdSystem->AddCommand( "rendererGeometryResourceSelfTest", R_RendererGeometryResourceSelfTest_f, CMD_FL_RENDERER, "run renderer geometry and instance packet self tests" );
	cmdSystem->AddCommand( "rendererGLStateCacheSelfTest", R_RendererGLStateCacheSelfTest_f, CMD_FL_RENDERER, "run renderer GL state-cache self tests" );
	cmdSystem->AddCommand( "rendererModernGLExecutorSelfTest", R_RendererModernGLExecutorSelfTest_f, CMD_FL_RENDERER, "run renderer modern GL executor self tests" );
	cmdSystem->AddCommand( "rendererGpuDrivenSelfTest", R_RendererGpuDrivenSelfTest_f, CMD_FL_RENDERER, "run renderer GL43 GPU-driven compute and indirect self tests" );
	cmdSystem->AddCommand( "rendererVisiblePathSelfTest", R_RendererVisiblePathSelfTest_f, CMD_FL_RENDERER, "run renderer visible modern depth-path self tests" );
	cmdSystem->AddCommand( "rendererGBufferSelfTest", R_RendererGBufferSelfTest_f, CMD_FL_RENDERER, "run renderer modern opaque G-buffer self tests" );
	cmdSystem->AddCommand( "rendererPBRVisibleSelfTest", R_RendererPBRVisibleSelfTest_f, CMD_FL_RENDERER, "run guarded modern PBR G-buffer/deferred/forward self tests" );
	cmdSystem->AddCommand( "rendererClusterGridSelfTest", R_RendererClusterGridSelfTest_f, CMD_FL_RENDERER, "run renderer clustered light-grid self tests" );
	cmdSystem->AddCommand( "rendererLightImageAtlasSelfTest", R_RendererLightImageAtlasSelfTest_f, CMD_FL_RENDERER, "run renderer modern light falloff/projection atlas self tests" );
	cmdSystem->AddCommand( "rendererShadowPlannerSelfTest", R_RendererShadowPlannerSelfTest_f, CMD_FL_RENDERER, "run renderer modern shadow-planner self tests" );
	cmdSystem->AddCommand( "rendererShadowProjectedDiagnosticSelfTest", R_RendererShadowProjectedDiagnosticSelfTest_f, CMD_FL_RENDERER, "run renderer projected shadow diagnostic scene self tests" );
	cmdSystem->AddCommand( "rendererDeferredResolveSelfTest", R_RendererDeferredResolveSelfTest_f, CMD_FL_RENDERER, "run renderer deferred light resolve self tests" );
	cmdSystem->AddCommand( "rendererForwardPlusSelfTest", R_RendererForwardPlusSelfTest_f, CMD_FL_RENDERER, "run renderer clustered forward+ self tests" );
	cmdSystem->AddCommand( "rendererModernVisibleSelfTest", R_RendererModernVisibleSelfTest_f, CMD_FL_RENDERER, "run renderer modern visible-frame composition self tests" );
	cmdSystem->AddCommand( "rendererModernCompatibilitySelfTest", R_RendererModernCompatibilitySelfTest_f, CMD_FL_RENDERER, "run renderer modern compatibility owner self tests" );
	cmdSystem->AddCommand( "rendererPassOwnershipSelfTest", R_RendererPassOwnershipSelfTest_f, CMD_FL_RENDERER, "run renderer modern/legacy pass ownership self tests" );
	cmdSystem->AddCommand( "rendererModernVisibilitySelfTest", R_RendererModernVisibilitySelfTest_f, CMD_FL_RENDERER, "run renderer modern visibility and occlusion self tests" );
	cmdSystem->AddCommand( "rendererLowOverheadSelfTest", R_RendererLowOverheadSelfTest_f, CMD_FL_RENDERER, "run renderer GL45 low-overhead self tests" );
	cmdSystem->AddCommand( "rendererShaderLibrarySelfTest", R_RendererModernGLShaderLibrarySelfTest_f, CMD_FL_RENDERER, "run renderer modern GL shader-library self tests" );
	cmdSystem->AddCommand( "rendererModernGLShaderLibrarySelfTest", R_RendererModernGLShaderLibrarySelfTest_f, CMD_FL_RENDERER, "run renderer modern GL shader-library self tests" );
	cmdSystem->AddCommand( "rendererShaderLibraryReload", R_RendererShaderLibraryReload_f, CMD_FL_RENDERER, "reload the internal modern GL shader library when r_rendererShaderReload is enabled" );
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
	glContextGeneration = 0;
	staticAllocCount = 0;
	frameShaderTime = 0.0f;
	frameShaderTimeMsec = 0;
	postProcessTexelSize.Set( 1.0f, 1.0f, 1.0f, 1.0f );
	postProcessSourceColorSpace.Set( 0.0f, 2.2f, 0.0f, 0.0f );
	postProcessSMAAQuality.Set( 0.0f, 0.10f, 8.0f, 2.0f );
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
	resolutionScaleCropActive = false;
	resolutionScaleWidth = 0;
	resolutionScaleHeight = 0;
	resolutionScaleSuppressed = false;
	guiRecursionLevel = 0;
	guiModel = NULL;
	demoGuiModel = NULL;
	pendingRenderTextureDeletes.Clear();
	useUIViewportFor2D = true;
	activeRenderTexture = NULL;
	portalSkyCaptureViewCallback = NULL;
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
	R_RendererMetrics_ResetGpuFrameTiming( "renderer init" );

	// route imagetools static-allocation counters into tr.pc before any
	// image or model loading can call R_StaticAlloc
	R_InstallImageToolsHooks();

	// bind the shared render-geometry library to live renderer behavior
	// before any model loading can allocate triangle surfaces
	R_InstallRenderGeoHooks();

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
	R_RendererMetrics_ResetGpuFrameTiming( "renderer shutdown" );

	R_DoneFreeType( );

	if ( glConfig.isInitialized ) {
		R_ShutdownRenderTargetsBeforeImagePurge();
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

	// idVertexCache::Shutdown() is a no-op until Init() has linked its list
	// sentinels, so this is safe on the dedicated renderer (which never creates
	// a context) and on an early-startup teardown. Do not re-add a
	// glConfig.isInitialized guard here: ShutdownOpenGL() clears that flag
	// without touching the cache, so a guarded call would leak every buffer
	// after a vid_restart teardown.
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
	R_RendererMetrics_ResetGpuFrameTiming( "begin level load" );
	// SetUnderwaterView is the only writer of this state, and it is driven by the game
	// each frame. A map change started while the player was submerged would otherwise
	// leave the effect live across the whole loading screen and into the first frames
	// of the new map, costing a full-screen copy per loading-screen present.
	underwaterAmount = 0.0f;
	underwaterTint.Zero();

	// The caller runs declManager->BeginLevelLoad() immediately before this, and
	// that purge can reach the generated glyph atlas materials. Put them back
	// before the loading screen draws its first frame of text.
	R_TTFRestoreAtlasMaterials();

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

	// Loading-screen work belongs to the old timing epoch. Start gameplay with
	// an empty delayed ring so budget capture cannot mix the two workloads.
	R_RendererMetrics_ResetGpuFrameTiming( "end level load" );
}

/*
========================
idRenderSystemLocal::InitOpenGL
========================
*/
void idRenderSystemLocal::InitOpenGL( void ) {
	R_MigrateLegacyShadowMapContactQuality();

	// if the device isn't started, start it now
	if ( !glConfig.isInitialized ) {
#ifdef OPENQ4_RENDERER_VK_MODULE
		// Vulkan backend seam (Phase C): device + swapchain bring-up through
		// the window services; no GL ladder, caps probe, or program loads
		extern bool VK_InitRenderDevice( void );
		if ( !VK_InitRenderDevice() ) {
			common->FatalError( "Vulkan renderer device initialization failed" );
		}
		// The Vulkan module does not run the OpenGL tier/bootstrap tail that
		// normally initializes shared scene resources.  The classic-GUI domain
		// only needs the backend-neutral scene-packet/material contract, so make
		// that one conservative feature explicit without advertising any GL
		// modern-visible capability.
		renderFeatureSet_t vkSharedFeatures = glConfig.renderFeatures;
		vkSharedFeatures.scenePackets = true;
		R_MaterialResourceTable_Init( glConfig.backendCaps, vkSharedFeatures );
		R_GpuSkinning_ContractInit( glConfig.backendCaps );
		// mirror the R_InitOpenGL tail the seam skips: the front-end needs
		// the vertex cache, a resolved back end, frame data, and gamma
		// tables before the first BeginFrame
		vertexCache.Init();
		r_renderer.SetModified();
		tr.SetBackEndRenderer();
		R_InitFrameData();
		R_SetColorMappings();
		// images loaded before the device existed stayed TEXTURE_NOT_LOADED,
		// exactly like the GL half without a context; create them now
		globalImages->ReloadImages( true );
#else
		int	err;

		R_InitOpenGL();

		globalImages->ReloadImages(true);

		err = glGetError();
		if ( err != GL_NO_ERROR ) {
			common->Printf( "glGetError() = 0x%x\n", err );
		}
#endif
	}
}

/*
========================
idRenderSystemLocal::ShutdownOpenGL
========================
*/
void idRenderSystemLocal::ShutdownOpenGL( void ) {
	if ( !glConfig.isInitialized ) {
		R_ClearActiveRenderTextures();
		useUIViewportFor2D = true;
		return;
	}

	R_ShutdownRenderTargetsBeforeImagePurge();
	if ( globalImages != NULL ) {
		globalImages->PurgeAllImages();
	}

	// free the context and close the window
	R_ShutdownFrameData();
	R_GpuSkinning_ContractShutdown();
	R_ClassicGuiDomain_ResetFrame();
	R_ClassicCinematicPostDomain_ResetFrame();
	R_ClassicSpecialFrameDomain_ResetFrame();
	R_ClassicWorldAmbientDomain_ResetFrame();
	R_ClassicInteractionDomain_ResetFrame();
	R_ClassicFogBlendDomain_ResetFrame();
	R_ClassicSubviewDomain_ResetFrame();
#ifdef OPENQ4_RENDERER_VK_MODULE
	// Vulkan backend seam (Phase C): the GL executor/upload/graph/debug
	// subsystems never initialized; device + window teardown is GLimp_Shutdown
	R_MaterialResourceTable_Shutdown();
	GLimp_Shutdown();
#else
	R_RendererMetrics_ShutdownGpuTimers();
	R_MaterialResourceTable_Shutdown();
	R_RenderGraphResources_Shutdown();
	R_ModernGLExecutor_Shutdown();
	R_RendererUpload_Shutdown();
	RendererBootstrap_Shutdown();
	R_PurgeFramebufferCopyFBOs();
	R_GLDebugOutput_Shutdown();
	GLimp_Shutdown();
#endif
	R_RendererModule_Shutdown();
	glConfig.isInitialized = false;
	R_ClearActiveRenderTextures();
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
