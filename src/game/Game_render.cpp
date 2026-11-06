// Game_render.cpp
//


#include "Game_local.h"
#include "../renderer/ImageOpts.h"

idCVar g_renderCasUpscale("g_renderCasUpscale", "0", CVAR_GAME | CVAR_BOOL, "toggles the optional CAS post-process pass when a CAS material is available");
idCVar g_renderFastNoPost("g_renderFastNoPost", "1", CVAR_GAME | CVAR_BOOL, "render through the direct no-post path when AA, blur, and CAS are disabled");
idCVar g_renderFastNoPostDirect("g_renderFastNoPostDirect", "1", CVAR_GAME | CVAR_BOOL, "render directly to the backbuffer when the no-post path has no active post-processing work");
idCVar g_renderCaptureCurrentRender("g_renderCaptureCurrentRender", "0", CVAR_GAME | CVAR_BOOL, "force an end-of-view _currentRender copy instead of relying on on-demand post-process capture");

enum openq4PostAAMode_t {
	OPENQ4_POST_AA_OFF = 0,
	OPENQ4_POST_AA_SMAA_1X_MEDIUM,
	OPENQ4_POST_AA_SMAA_1X_HIGH,
	OPENQ4_POST_AA_SMAA_1X_ULTRA,
	OPENQ4_POST_AA_SMAA_1X_COLOR_PROTOTYPE
};

enum openq4PostAASourceColorSpace_t {
	OPENQ4_POST_AA_SOURCE_NONE = -1,
	OPENQ4_POST_AA_SOURCE_LEGACY_PERCEPTUAL_LDR = 0,
	OPENQ4_POST_AA_SOURCE_LINEAR_LDR
};

enum openq4PostAADisableReason_t {
	OPENQ4_POST_AA_DISABLE_NONE = 0,
	OPENQ4_POST_AA_DISABLE_UNSUPPORTED_MODE,
	OPENQ4_POST_AA_DISABLE_UNAVAILABLE,
	OPENQ4_POST_AA_DISABLE_POST_LIGHTING_STACK,
	OPENQ4_POST_AA_DISABLE_INVALID_TARGET_SIZE
};

enum openq4PostAAWarningState_t {
	OPENQ4_POST_AA_WARNING_NONE = 0,
	OPENQ4_POST_AA_WARNING_UNSUPPORTED_MODE,
	OPENQ4_POST_AA_WARNING_UNAVAILABLE,
	OPENQ4_POST_AA_WARNING_POST_LIGHTING_STACK,
	OPENQ4_POST_AA_WARNING_INVALID_TARGET_SIZE
};

struct openq4PostAASchedule_t {
	int requestedCvarValue;
	openq4PostAAMode_t requestedMode;
	openq4PostAAMode_t effectiveMode;
	openq4PostAADisableReason_t disableReason;
	idRenderTexture* resolveTarget;
	const char* sourceImageName;
	const char* outputImageName;
	int sourceWidth;
	int sourceHeight;
	int outputWidth;
	int outputHeight;
	openq4PostAASourceColorSpace_t sourceColorSpace;
	const char* sourceColorSpaceName;
	const char* smaaQualityName;
	const char* smaaEdgeModeName;
	idVec4 smaaQuality;
};

struct openq4SMAAQualityPreset_t {
	const char* name;
	const char* edgeModeName;
	idVec4 shaderParams; // x=edge mode, y=edge threshold, z=max search steps, w=local contrast scale.
};

struct openq4SMAAResources_t {
	idRenderTexture* sceneSource;
	idRenderTexture* edgeBlend;
	idRenderTexture* weightsFinal;
	const char* sceneSourceName;
	const char* edgeBlendName;
	const char* weightsFinalName;
};

struct openq4SMAAPass_t {
	const char* passName;
	const idMaterial* material;
	idRenderTexture* primarySource;
	const char* primarySourceName;
	idRenderTexture* secondarySource;
	const char* secondarySourceName;
	idRenderTexture* output;
	const char* outputName;
	bool clearBeforeDraw;
	bool bindPostProcessSourceSize;
	bool bindPostProcessSourceColorSpace;
	openq4PostAASourceColorSpace_t sourceColorSpace;
};

static bool IsUsablePostProcessMaterial( const idMaterial* material ) {
	return material != NULL && !material->TestMaterialFlag( MF_DEFAULTED );
}

static openq4PostAASourceColorSpace_t ResolvePostAASourceColorSpace( void ) {
	return OPENQ4_POST_AA_SOURCE_LEGACY_PERCEPTUAL_LDR;
}

static const char* PostAASourceColorSpaceName( const openq4PostAASourceColorSpace_t sourceColorSpace ) {
	switch ( sourceColorSpace ) {
	case OPENQ4_POST_AA_SOURCE_LINEAR_LDR:
		return "linear-ldr";
	case OPENQ4_POST_AA_SOURCE_LEGACY_PERCEPTUAL_LDR:
		return "legacy-perceptual-ldr";
	default:
		return "none";
	}
}

static idVec4 PostAASourceColorSpaceVector( const openq4PostAASourceColorSpace_t sourceColorSpace ) {
	switch ( sourceColorSpace ) {
	case OPENQ4_POST_AA_SOURCE_LINEAR_LDR:
		return idVec4( 1.0f, 2.2f, 0.0f, 0.0f );
	default:
		return idVec4( 0.0f, 2.2f, 0.0f, 0.0f );
	}
}

static bool PostAAModeUsesSMAA( const openq4PostAAMode_t mode ) {
	return mode == OPENQ4_POST_AA_SMAA_1X_MEDIUM ||
		mode == OPENQ4_POST_AA_SMAA_1X_HIGH ||
		mode == OPENQ4_POST_AA_SMAA_1X_ULTRA ||
		mode == OPENQ4_POST_AA_SMAA_1X_COLOR_PROTOTYPE;
}

static openq4SMAAQualityPreset_t PostAASMAAQualityPreset( const openq4PostAAMode_t mode ) {
	openq4SMAAQualityPreset_t preset;
	preset.name = "none";
	preset.edgeModeName = "none";
	preset.shaderParams = idVec4( 0.0f, 0.0f, 0.0f, 0.0f );

	switch ( mode ) {
	case OPENQ4_POST_AA_SMAA_1X_HIGH:
		preset.name = "high-luma";
		preset.edgeModeName = "luma";
		preset.shaderParams = idVec4( 0.0f, 0.10f, 16.0f, 2.0f );
		break;
	case OPENQ4_POST_AA_SMAA_1X_ULTRA:
		preset.name = "ultra-luma";
		preset.edgeModeName = "luma";
		preset.shaderParams = idVec4( 0.0f, 0.05f, 32.0f, 2.0f );
		break;
	case OPENQ4_POST_AA_SMAA_1X_COLOR_PROTOTYPE:
		preset.name = "color-edge-prototype";
		preset.edgeModeName = "color";
		preset.shaderParams = idVec4( 1.0f, 0.10f, 16.0f, 2.0f );
		break;
	case OPENQ4_POST_AA_SMAA_1X_MEDIUM:
		preset.name = "medium-luma";
		preset.edgeModeName = "luma";
		preset.shaderParams = idVec4( 0.0f, 0.10f, 8.0f, 2.0f );
		break;
	default:
		break;
	}

	return preset;
}

static const char* PostAASMAAQualityName( const openq4PostAAMode_t mode ) {
	return PostAASMAAQualityPreset( mode ).name;
}

static const char* PostAASMAAEdgeModeName( const openq4PostAAMode_t mode ) {
	return PostAASMAAQualityPreset( mode ).edgeModeName;
}

static idVec4 PostAASMAAQualityVector( const openq4PostAAMode_t mode ) {
	return PostAASMAAQualityPreset( mode ).shaderParams;
}

static openq4SMAAResources_t BuildSMAAResources( rvmGameRender_t& gameRender ) {
	openq4SMAAResources_t resources;
	resources.sceneSource = gameRender.postProcessRT[2];
	resources.edgeBlend = gameRender.postProcessRT[1];
	resources.weightsFinal = gameRender.postProcessRT[0];
	resources.sceneSourceName = "_postProcessAlbedo2/source";
	resources.edgeBlendName = "_postProcessAlbedo1/edge-blend";
	resources.weightsFinalName = "_postProcessAlbedo0/weights-final";
	return resources;
}

static bool ValidateDistinctPostAAResource(
	const idRenderTexture* a,
	const char* aName,
	const idRenderTexture* b,
	const char* bName,
	const char* context ) {
	assert( a != NULL && b != NULL );
	if ( a == NULL || b == NULL ) {
		return false;
	}

	const bool distinct = ( a != b );
	assert( distinct );
	if ( !distinct ) {
		common->Warning(
			"PostAA SMAA %s aliases %s and %s; disabling SMAA to avoid render-target feedback.",
			context,
			aName,
			bName );
		return false;
	}

	return true;
}

static bool ValidateSMAAResourceOwnership( const openq4SMAAResources_t& resources ) {
	return ValidateDistinctPostAAResource(
			resources.sceneSource,
			resources.sceneSourceName,
			resources.edgeBlend,
			resources.edgeBlendName,
			"resource contract" ) &&
		ValidateDistinctPostAAResource(
			resources.sceneSource,
			resources.sceneSourceName,
			resources.weightsFinal,
			resources.weightsFinalName,
			"resource contract" ) &&
		ValidateDistinctPostAAResource(
			resources.edgeBlend,
			resources.edgeBlendName,
			resources.weightsFinal,
			resources.weightsFinalName,
			"resource contract" );
}

static bool QueryRenderTextureSize( idRenderTexture* renderTexture, int& width, int& height ) {
	width = 0;
	height = 0;

	if ( renderTexture == NULL ) {
		return false;
	}

	renderSystem->GetRenderTextureSize( renderTexture, width, height );
	return width > 0 && height > 0;
}

static void SetPostAAScheduleSize( openq4PostAASchedule_t& schedule, idRenderTexture* sourceRT, idRenderTexture* outputRT ) {
	QueryRenderTextureSize( sourceRT, schedule.sourceWidth, schedule.sourceHeight );
	QueryRenderTextureSize( outputRT, schedule.outputWidth, schedule.outputHeight );
}

static bool ValidateSMAARenderTargetSizes( rvmGameRender_t& gameRender ) {
	const openq4SMAAResources_t resources = BuildSMAAResources( gameRender );
	if ( !ValidateSMAAResourceOwnership( resources ) ) {
		return false;
	}

	int width0, height0;
	int width1, height1;
	int width2, height2;
	const bool hasRT0 = QueryRenderTextureSize( gameRender.postProcessRT[0], width0, height0 );
	const bool hasRT1 = QueryRenderTextureSize( gameRender.postProcessRT[1], width1, height1 );
	const bool hasRT2 = QueryRenderTextureSize( gameRender.postProcessRT[2], width2, height2 );

	assert( hasRT0 && hasRT1 && hasRT2 );
	if ( !hasRT0 || !hasRT1 || !hasRT2 ) {
		return false;
	}

	const bool matchingSizes =
		width0 == width1 && width0 == width2 &&
		height0 == height1 && height0 == height2;
	assert( matchingSizes );
	return matchingSizes;
}

static bool SetSMAAPassSourceSize( const char* passName, idRenderTexture* sourceRT, idRenderTexture* outputRT ) {
	int sourceWidth, sourceHeight;
	int outputWidth, outputHeight;
	const bool hasSource = QueryRenderTextureSize( sourceRT, sourceWidth, sourceHeight );
	const bool hasOutput = QueryRenderTextureSize( outputRT, outputWidth, outputHeight );

	assert( hasSource && hasOutput );
	if ( !hasSource || !hasOutput ) {
		common->Warning( "PostAA SMAA %s pass has an invalid source or output render target; skipping SMAA.", passName );
		return false;
	}

	const bool matchingSizes = sourceWidth == outputWidth && sourceHeight == outputHeight;
	assert( matchingSizes );
	if ( !matchingSizes ) {
		common->Warning(
			"PostAA SMAA %s pass source/output size mismatch: source=%dx%d output=%dx%d; skipping SMAA.",
			passName,
			sourceWidth,
			sourceHeight,
			outputWidth,
			outputHeight );
		return false;
	}

	renderSystem->SetPostProcessSourceSize( sourceWidth, sourceHeight );
	return true;
}

static bool ValidateSMAAPassAliasing( const openq4SMAAPass_t& pass ) {
	assert( pass.output != NULL );
	assert( pass.primarySource != NULL );
	assert( pass.clearBeforeDraw );

	if ( pass.output == NULL || pass.primarySource == NULL ) {
		common->Warning( "PostAA SMAA %s pass has an incomplete resource contract; skipping SMAA.", pass.passName );
		return false;
	}

	if ( !pass.clearBeforeDraw ) {
		common->Warning( "PostAA SMAA %s pass is missing its mandatory pre-draw clear; skipping SMAA.", pass.passName );
		return false;
	}

	if ( !ValidateDistinctPostAAResource(
			pass.primarySource,
			pass.primarySourceName,
			pass.output,
			pass.outputName,
			pass.passName ) ) {
		return false;
	}

	if ( pass.secondarySource != NULL &&
		!ValidateDistinctPostAAResource(
			pass.secondarySource,
			pass.secondarySourceName,
			pass.output,
			pass.outputName,
			pass.passName ) ) {
		return false;
	}

	return true;
}

static bool ValidateSMAAPassSize( const openq4SMAAPass_t& pass ) {
	int sourceWidth, sourceHeight;
	int outputWidth, outputHeight;
	const bool hasSource = QueryRenderTextureSize( pass.primarySource, sourceWidth, sourceHeight );
	const bool hasOutput = QueryRenderTextureSize( pass.output, outputWidth, outputHeight );

	assert( hasSource && hasOutput );
	if ( !hasSource || !hasOutput ) {
		common->Warning( "PostAA SMAA %s pass has an invalid source or output render target; skipping SMAA.", pass.passName );
		return false;
	}

	const bool matchingSizes = sourceWidth == outputWidth && sourceHeight == outputHeight;
	assert( matchingSizes );
	if ( !matchingSizes ) {
		common->Warning(
			"PostAA SMAA %s pass source/output size mismatch: source=%dx%d output=%dx%d; skipping SMAA.",
			pass.passName,
			sourceWidth,
			sourceHeight,
			outputWidth,
			outputHeight );
		return false;
	}

	return true;
}

static void LogSMAARenderTargetSizes( rvmGameRender_t& gameRender ) {
	int width0, height0;
	int width1, height1;
	int width2, height2;
	if ( !QueryRenderTextureSize( gameRender.postProcessRT[0], width0, height0 ) ||
		!QueryRenderTextureSize( gameRender.postProcessRT[1], width1, height1 ) ||
		!QueryRenderTextureSize( gameRender.postProcessRT[2], width2, height2 ) ) {
		return;
	}

	static int loggedWidth0 = -1;
	static int loggedHeight0 = -1;
	static int loggedWidth1 = -1;
	static int loggedHeight1 = -1;
	static int loggedWidth2 = -1;
	static int loggedHeight2 = -1;
	if ( loggedWidth0 == width0 && loggedHeight0 == height0 &&
		loggedWidth1 == width1 && loggedHeight1 == height1 &&
		loggedWidth2 == width2 && loggedHeight2 == height2 ) {
		return;
	}

	common->Printf(
		"PostAA SMAA target sizes: _postProcessAlbedo2/source=%dx%d, _postProcessAlbedo1/edge=%dx%d, _postProcessAlbedo0/weights=%dx%d\n",
		width2,
		height2,
		width1,
		height1,
		width0,
		height0 );

	loggedWidth0 = width0;
	loggedHeight0 = height0;
	loggedWidth1 = width1;
	loggedHeight1 = height1;
	loggedWidth2 = width2;
	loggedHeight2 = height2;
}

static void LogSMAAResourceOwnership( const openq4SMAAResources_t& resources, const openq4PostAASourceColorSpace_t sourceColorSpace, const openq4PostAAMode_t mode ) {
	static idRenderTexture* loggedSceneSource = NULL;
	static idRenderTexture* loggedEdgeBlend = NULL;
	static idRenderTexture* loggedWeightsFinal = NULL;
	static int loggedSourceColorSpace = -1;
	static int loggedMode = -1;
	if ( loggedSceneSource == resources.sceneSource &&
		loggedEdgeBlend == resources.edgeBlend &&
		loggedWeightsFinal == resources.weightsFinal &&
		loggedSourceColorSpace == sourceColorSpace &&
		loggedMode == mode ) {
		return;
	}

	const openq4SMAAQualityPreset_t qualityPreset = PostAASMAAQualityPreset( mode );
	common->Printf(
		"PostAA SMAA resource ownership: sceneSource=%s, edgeBlend=%s, weightsFinal=%s; sourceSpace=%s; quality=%s edgeMode=%s threshold=%.3f searchSteps=%.0f localContrast=%.2f; pass clears=edge,weights,blend,normalize\n",
		resources.sceneSourceName,
		resources.edgeBlendName,
		resources.weightsFinalName,
		PostAASourceColorSpaceName( sourceColorSpace ),
		qualityPreset.name,
		qualityPreset.edgeModeName,
		qualityPreset.shaderParams.y,
		qualityPreset.shaderParams.z,
		qualityPreset.shaderParams.w );

	loggedSceneSource = resources.sceneSource;
	loggedEdgeBlend = resources.edgeBlend;
	loggedWeightsFinal = resources.weightsFinal;
	loggedSourceColorSpace = sourceColorSpace;
	loggedMode = mode;
}

static const char* PostAAModeName( const openq4PostAAMode_t mode ) {
	switch ( mode ) {
	case OPENQ4_POST_AA_SMAA_1X_MEDIUM:
		return "SMAA1xMedium";
	case OPENQ4_POST_AA_SMAA_1X_HIGH:
		return "SMAA1xHigh";
	case OPENQ4_POST_AA_SMAA_1X_ULTRA:
		return "SMAA1xUltra";
	case OPENQ4_POST_AA_SMAA_1X_COLOR_PROTOTYPE:
		return "SMAA1xColorPrototype";
	default:
		return "Off";
	}
}

static const char* PostAADisableReasonName( const openq4PostAADisableReason_t reason ) {
	switch ( reason ) {
	case OPENQ4_POST_AA_DISABLE_UNSUPPORTED_MODE:
		return "unsupported-mode";
	case OPENQ4_POST_AA_DISABLE_UNAVAILABLE:
		return "resources-unavailable";
	case OPENQ4_POST_AA_DISABLE_POST_LIGHTING_STACK:
		return "post-lighting-stack";
	case OPENQ4_POST_AA_DISABLE_INVALID_TARGET_SIZE:
		return "invalid-target-size";
	default:
		return "none";
	}
}

static openq4PostAAMode_t TranslatePostAASettings( const int cvarValue ) {
	switch ( cvarValue ) {
	case 1:
		return OPENQ4_POST_AA_SMAA_1X_MEDIUM;
	case 2:
		return OPENQ4_POST_AA_SMAA_1X_HIGH;
	case 3:
		return OPENQ4_POST_AA_SMAA_1X_ULTRA;
	case 4:
		return OPENQ4_POST_AA_SMAA_1X_COLOR_PROTOTYPE;
	default:
		return OPENQ4_POST_AA_OFF;
	}
}

static bool PostAAModeSupported( const int cvarValue ) {
	return cvarValue >= 0 && cvarValue <= 4;
}

static bool ValidateSMAAMaterial( const idMaterial* material, const char* materialName ) {
	if ( !IsUsablePostProcessMaterial( material ) ) {
		return false;
	}

	if ( material->GetNumStages() <= 0 ) {
		common->Warning( "SMAA material '%s' has no stages; disabling SMAA.", materialName );
		return false;
	}

	if ( !renderSystem->ValidateMaterialArbPrograms( material ) ) {
		common->Warning( "SMAA material '%s' uses an invalid or unavailable shader program; disabling SMAA.", materialName );
		return false;
	}

	return true;
}

static bool EvaluateSMAAAvailability( rvmGameRender_t& gameRender ) {
	// Let the backend reject unsupported SMAA before compiling its shaders.
	return renderSystem->ValidateSMAALookupTextures() && ValidateSMAAMaterial(
		gameRender.smaaEdgePostProcessMaterial,
		( gameRender.smaaEdgePostProcessMaterial != NULL ) ? gameRender.smaaEdgePostProcessMaterial->GetName() : "postprocess/smaa_edge" ) &&
		ValidateSMAAMaterial(
			gameRender.smaaWeightsPostProcessMaterial,
			( gameRender.smaaWeightsPostProcessMaterial != NULL ) ? gameRender.smaaWeightsPostProcessMaterial->GetName() : "postprocess/smaa_weights" ) &&
		ValidateSMAAMaterial(
			gameRender.smaaBlendPostProcessMaterial,
			( gameRender.smaaBlendPostProcessMaterial != NULL ) ? gameRender.smaaBlendPostProcessMaterial->GetName() : "postprocess/smaa_blend" ) &&
		( gameRender.postProcessRT[0] != NULL ) &&
		( gameRender.postProcessRT[1] != NULL ) &&
		( gameRender.postProcessRT[2] != NULL ) &&
		ValidateSMAAResourceOwnership( BuildSMAAResources( gameRender ) ) &&
		IsUsablePostProcessMaterial( gameRender.copyPostProcess1Material );
}

static openq4PostAASchedule_t BuildPostAAPasses( rvmGameRender_t& gameRender, const bool postLightingStackEnabled ) {
	openq4PostAASchedule_t schedule;
	schedule.requestedCvarValue = cvarSystem->GetCVarInteger( "r_postAA" );
	schedule.requestedMode = TranslatePostAASettings( schedule.requestedCvarValue );
	schedule.effectiveMode = schedule.requestedMode;
	schedule.disableReason = OPENQ4_POST_AA_DISABLE_NONE;
	schedule.resolveTarget = gameRender.postProcessRT[0];
	schedule.sourceImageName = "_postProcessAlbedo0";
	schedule.outputImageName = "_postProcessAlbedo0";
	schedule.sourceWidth = 0;
	schedule.sourceHeight = 0;
	schedule.outputWidth = 0;
	schedule.outputHeight = 0;
	schedule.sourceColorSpace = OPENQ4_POST_AA_SOURCE_NONE;
	schedule.sourceColorSpaceName = PostAASourceColorSpaceName( schedule.sourceColorSpace );
	schedule.smaaQualityName = PostAASMAAQualityName( schedule.requestedMode );
	schedule.smaaEdgeModeName = PostAASMAAEdgeModeName( schedule.requestedMode );
	schedule.smaaQuality = PostAASMAAQualityVector( schedule.requestedMode );
	SetPostAAScheduleSize( schedule, gameRender.postProcessRT[0], gameRender.postProcessRT[0] );

	if ( !PostAAModeSupported( schedule.requestedCvarValue ) ) {
		schedule.effectiveMode = OPENQ4_POST_AA_OFF;
		schedule.disableReason = OPENQ4_POST_AA_DISABLE_UNSUPPORTED_MODE;
		return schedule;
	}

	if ( PostAAModeUsesSMAA( schedule.requestedMode ) ) {
		if ( postLightingStackEnabled ) {
			schedule.effectiveMode = OPENQ4_POST_AA_OFF;
			schedule.disableReason = OPENQ4_POST_AA_DISABLE_POST_LIGHTING_STACK;
		} else if ( !gameRender.smaaAvailable ) {
			schedule.effectiveMode = OPENQ4_POST_AA_OFF;
			schedule.disableReason = OPENQ4_POST_AA_DISABLE_UNAVAILABLE;
		} else if ( !ValidateSMAARenderTargetSizes( gameRender ) ) {
			schedule.effectiveMode = OPENQ4_POST_AA_OFF;
			schedule.disableReason = OPENQ4_POST_AA_DISABLE_INVALID_TARGET_SIZE;
		} else {
			schedule.resolveTarget = gameRender.postProcessRT[2];
			schedule.sourceImageName = "_postProcessAlbedo2";
			schedule.outputImageName = "_postProcessAlbedo0";
			schedule.sourceColorSpace = ResolvePostAASourceColorSpace();
			schedule.sourceColorSpaceName = PostAASourceColorSpaceName( schedule.sourceColorSpace );
			schedule.smaaQualityName = PostAASMAAQualityName( schedule.effectiveMode );
			schedule.smaaEdgeModeName = PostAASMAAEdgeModeName( schedule.effectiveMode );
			schedule.smaaQuality = PostAASMAAQualityVector( schedule.effectiveMode );
			SetPostAAScheduleSize( schedule, gameRender.postProcessRT[2], gameRender.postProcessRT[0] );
		}
	}

	return schedule;
}

static int PostAAWarningStateForSchedule( const openq4PostAASchedule_t& schedule ) {
	switch ( schedule.disableReason ) {
	case OPENQ4_POST_AA_DISABLE_UNSUPPORTED_MODE:
		return OPENQ4_POST_AA_WARNING_UNSUPPORTED_MODE;
	case OPENQ4_POST_AA_DISABLE_UNAVAILABLE:
		return OPENQ4_POST_AA_WARNING_UNAVAILABLE;
	case OPENQ4_POST_AA_DISABLE_POST_LIGHTING_STACK:
		return OPENQ4_POST_AA_WARNING_POST_LIGHTING_STACK;
	case OPENQ4_POST_AA_DISABLE_INVALID_TARGET_SIZE:
		return OPENQ4_POST_AA_WARNING_INVALID_TARGET_SIZE;
	default:
		return OPENQ4_POST_AA_WARNING_NONE;
	}
}

static void LogPostAASchedule( const openq4PostAASchedule_t& schedule, const bool smaaResourcesAvailable ) {
	static int loggedRequestedValue = -9999;
	static int loggedRequestedMode = -1;
	static int loggedEffectiveMode = -1;
	static int loggedDisableReason = -1;
	static int loggedSMAAResourcesAvailable = -1;
	static int loggedSourceWidth = -1;
	static int loggedSourceHeight = -1;
	static int loggedOutputWidth = -1;
	static int loggedOutputHeight = -1;
	static int loggedSourceColorSpace = -1;
	static idStr loggedSMAAQualityName;
	static idStr loggedSMAAEdgeModeName;
	static float loggedSMAAThreshold = -9999.0f;
	static float loggedSMAASearchSteps = -9999.0f;
	static float loggedSMAALocalContrast = -9999.0f;
	const int smaaResourcesFlag = smaaResourcesAvailable ? 1 : 0;

	if ( loggedRequestedValue == schedule.requestedCvarValue &&
		loggedRequestedMode == schedule.requestedMode &&
		loggedEffectiveMode == schedule.effectiveMode &&
		loggedDisableReason == schedule.disableReason &&
		loggedSMAAResourcesAvailable == smaaResourcesFlag &&
		loggedSourceWidth == schedule.sourceWidth &&
		loggedSourceHeight == schedule.sourceHeight &&
		loggedOutputWidth == schedule.outputWidth &&
		loggedOutputHeight == schedule.outputHeight &&
		loggedSourceColorSpace == schedule.sourceColorSpace &&
		loggedSMAAQualityName.Icmp( schedule.smaaQualityName ) == 0 &&
		loggedSMAAEdgeModeName.Icmp( schedule.smaaEdgeModeName ) == 0 &&
		loggedSMAAThreshold == schedule.smaaQuality.y &&
		loggedSMAASearchSteps == schedule.smaaQuality.z &&
		loggedSMAALocalContrast == schedule.smaaQuality.w ) {
		return;
	}

	common->Printf(
		"PostAA requested=%s cvar=%d effective=%s quality=%s edgeMode=%s threshold=%.3f searchSteps=%.0f localContrast=%.2f source=%s(%dx%d) output=%s(%dx%d) sourceSpace=%s reason=%s smaaResources=%s\n",
		PostAAModeName( schedule.requestedMode ),
		schedule.requestedCvarValue,
		PostAAModeName( schedule.effectiveMode ),
		schedule.smaaQualityName,
		schedule.smaaEdgeModeName,
		schedule.smaaQuality.y,
		schedule.smaaQuality.z,
		schedule.smaaQuality.w,
		schedule.sourceImageName,
		schedule.sourceWidth,
		schedule.sourceHeight,
		schedule.outputImageName,
		schedule.outputWidth,
		schedule.outputHeight,
		schedule.sourceColorSpaceName,
		PostAADisableReasonName( schedule.disableReason ),
		smaaResourcesAvailable ? "ready" : "unavailable" );

	loggedRequestedValue = schedule.requestedCvarValue;
	loggedRequestedMode = schedule.requestedMode;
	loggedEffectiveMode = schedule.effectiveMode;
	loggedDisableReason = schedule.disableReason;
	loggedSMAAResourcesAvailable = smaaResourcesFlag;
	loggedSourceWidth = schedule.sourceWidth;
	loggedSourceHeight = schedule.sourceHeight;
	loggedOutputWidth = schedule.outputWidth;
	loggedOutputHeight = schedule.outputHeight;
	loggedSourceColorSpace = schedule.sourceColorSpace;
	loggedSMAAQualityName = schedule.smaaQualityName;
	loggedSMAAEdgeModeName = schedule.smaaEdgeModeName;
	loggedSMAAThreshold = schedule.smaaQuality.y;
	loggedSMAASearchSteps = schedule.smaaQuality.z;
	loggedSMAALocalContrast = schedule.smaaQuality.w;
}

static bool openQ4_BuildPortalSkyCaptureView( const renderView_t *view, renderView_t *portalSkyView ) {
	return gameLocal.BuildPortalSkyRenderView( view, portalSkyView, gameRenderWorld );
}

static void UpdatePostAAWarningState( rvmGameRender_t& gameRender, int warningState ) {
	if ( gameRender.postAAWarningState == warningState ) {
		return;
	}

	gameRender.postAAWarningState = warningState;
	switch ( warningState ) {
	case OPENQ4_POST_AA_WARNING_UNSUPPORTED_MODE:
		common->Warning( "Unsupported r_postAA value; falling back to no post AA." );
		break;
	case OPENQ4_POST_AA_WARNING_UNAVAILABLE:
		common->Warning( "SMAA is enabled, but the current SMAA resources are unavailable. Falling back to no post AA." );
		break;
	case OPENQ4_POST_AA_WARNING_POST_LIGHTING_STACK:
		common->Warning( "SMAA is enabled, but r_usePostLightingStack disables the current SMAA path. Falling back to no post AA." );
		break;
	case OPENQ4_POST_AA_WARNING_INVALID_TARGET_SIZE:
		common->Warning( "SMAA is enabled, but the post-process render target sizes are invalid or inconsistent. Falling back to no post AA." );
		break;
	default:
		break;
	}
}

static const idMaterial* FindPostProcessMaterial( const char* primaryName, const char* fallbackName ) {
	const idMaterial* material = declManager->FindMaterial( primaryName, false );
	if ( IsUsablePostProcessMaterial( material ) || fallbackName == NULL ) {
		return material;
	}

	if ( material != NULL ) {
		common->Warning( "Post-process material '%s' defaulted during parse; trying fallback '%s'.",
			primaryName,
			( fallbackName != NULL ) ? fallbackName : "<none>" );
	}

	material = declManager->FindMaterial( fallbackName, false );
	if ( IsUsablePostProcessMaterial( material ) ) {
		return material;
	}

	if ( material != NULL ) {
		common->Warning( "Fallback post-process material '%s' defaulted during parse.", fallbackName );
	}

	return NULL;
}

static void openQ4_RenderSceneDirect( const renderView_t *view, idRenderWorld *renderWorld, idCamera *portalSky, int renderFlags ) {
	renderSystem->BindRenderTexture( nullptr, nullptr );
	renderSystem->ClearRenderTarget( true, true, 1.0f, 0.0f, 0.0f, 0.0f );

	idPresentationRendererScope rendererScope;
	if ( portalSky ) {
		renderView_t portalSkyView = *view;
		portalSky->GetViewParms( &portalSkyView );
		renderWorld->RenderScene( &portalSkyView, ( renderFlags & ~RF_PRIMARY_VIEW ) | RF_DEFER_COMMAND_SUBMIT | RF_PORTAL_SKY );
	}

	renderWorld->RenderScene( view, renderFlags | RF_PENUMBRA_MAP );
}

static void openQ4_BindSceneRenderTexture( idRenderTexture *renderTexture ) {
	// Mark game-level 3D scene targets as feedback-capable so stock materials
	// that sample _currentRender, including heat haze, get an on-demand copy of
	// the active scene FBO. Fullscreen post passes bind with a NULL feedback
	// target to keep their source textures stable.
	renderSystem->BindRenderTexture( renderTexture, renderTexture );
}

static void openQ4_RenderSceneWorld( const renderView_t *view, idRenderWorld *renderWorld, idCamera *portalSky, int renderFlags ) {
	idPresentationRendererScope rendererScope;
	if ( portalSky ) {
		renderView_t portalSkyView = *view;
		portalSky->GetViewParms( &portalSkyView );
		renderWorld->RenderScene( &portalSkyView, ( renderFlags & ~RF_PRIMARY_VIEW ) | RF_DEFER_COMMAND_SUBMIT | RF_PORTAL_SKY );
	}

	renderWorld->RenderScene( view, renderFlags | RF_PENUMBRA_MAP );
}

static void openQ4_DrawFullScreenMaterial( const idMaterial *material ) {
	renderSystem->DrawStretchPic(
		0.0f, 0.0f, SCREEN_WIDTH, SCREEN_HEIGHT,
		0.0f, 1.0f, 1.0f, 0.0f,
		material );
}

// Below native, r_resolutionScaleMode 2 (sharpened) and 3 (nearest-neighbour)
// are upscales the renderer owns. A full-screen material can only stretch the
// scene bilinearly, which is what mode 1 asks for.
static bool openQ4_ScaledScenePresentationRequested( const renderPresentationState_t &presentation ) {
	if ( presentation.sceneWidth >= presentation.outputWidth
			&& presentation.sceneHeight >= presentation.outputHeight ) {
		return false;
	}
	const int mode = idMath::ClampInt( 0, 3, cvarSystem->GetCVarInteger( "r_resolutionScaleMode" ) );
	return mode == 2 || mode == 3;
}

// Presents a single-sample scene-extent target over the native output with the
// renderer's upscale. False when the renderer declines; nothing was drawn and
// the caller presents with its own material.
static bool openQ4_PresentScaledScene( const renderPresentationState_t &presentation,
		idRenderTexture *sceneColorTarget ) {
	return sceneColorTarget != NULL
		&& openQ4_ScaledScenePresentationRequested( presentation )
		&& renderSystem->PresentScaledScene( sceneColorTarget );
}

static void openQ4_LabelGameRenderTargets( rvmGameRender_t& gameRender ) {
	renderSystem->SetRenderTextureDebugName( gameRender.forwardRenderPassRT, "openQ4 forward scene MSAA" );
	renderSystem->SetRenderTextureDebugName( gameRender.forwardRenderPassResolvedRT, "openQ4 forward scene resolved" );
	renderSystem->SetRenderTextureDebugName( gameRender.postProcessRT[2], "openQ4 PostAA SMAA scene source" );
	renderSystem->SetRenderTextureDebugName( gameRender.postProcessRT[1], "openQ4 PostAA SMAA edge/blend" );
	renderSystem->SetRenderTextureDebugName( gameRender.postProcessRT[0], "openQ4 PostAA SMAA weights/final" );
	renderSystem->SetRenderTextureDebugName( gameRender.temporalHistoryRT[0], "openQ4 temporal history 0 (native)" );
	renderSystem->SetRenderTextureDebugName( gameRender.temporalHistoryRT[1], "openQ4 temporal history 1 (native)" );
}

static bool openQ4_ExecuteSMAAPass( const openq4SMAAPass_t& pass ) {
	if ( pass.material == NULL ) {
		common->Warning( "PostAA SMAA %s pass has no material; skipping SMAA.", pass.passName );
		return false;
	}

	if ( !ValidateSMAAPassAliasing( pass ) ) {
		return false;
	}

	if ( pass.bindPostProcessSourceColorSpace ) {
		renderSystem->SetPostProcessSourceColorSpace( PostAASourceColorSpaceVector( pass.sourceColorSpace ) );
	}

	if ( pass.bindPostProcessSourceSize ) {
		if ( !SetSMAAPassSourceSize( pass.passName, pass.primarySource, pass.output ) ) {
			return false;
		}
	} else if ( !ValidateSMAAPassSize( pass ) ) {
		return false;
	}

	renderSystem->BindRenderTexture( pass.output, NULL );
	renderSystem->ClearRenderTarget( true, true, 1.0f, 0.0f, 0.0f, 0.0f );
	openQ4_DrawFullScreenMaterial( pass.material );
	return true;
}

static bool openQ4_ApplySMAA( rvmGameRender_t& gameRender, const openq4PostAASourceColorSpace_t sourceColorSpace, const openq4PostAAMode_t mode ) {
	if ( !ValidateSMAARenderTargetSizes( gameRender ) ) {
		common->Warning( "PostAA SMAA render target sizes changed unexpectedly; falling back to no post AA." );
		return false;
	}
	LogSMAARenderTargetSizes( gameRender );
	const openq4SMAAResources_t resources = BuildSMAAResources( gameRender );
	LogSMAAResourceOwnership( resources, sourceColorSpace, mode );
	renderSystem->SetPostProcessSMAAQuality( PostAASMAAQualityPreset( mode ).shaderParams );

	openq4SMAAPass_t passes[4];
	memset( passes, 0, sizeof( passes ) );

	passes[0].passName = "edge";
	passes[0].material = gameRender.smaaEdgePostProcessMaterial;
	passes[0].primarySource = resources.sceneSource;
	passes[0].primarySourceName = resources.sceneSourceName;
	passes[0].output = resources.edgeBlend;
	passes[0].outputName = resources.edgeBlendName;
	passes[0].clearBeforeDraw = true;
	passes[0].bindPostProcessSourceSize = true;
	passes[0].bindPostProcessSourceColorSpace = true;
	passes[0].sourceColorSpace = sourceColorSpace;

	passes[1].passName = "weights";
	passes[1].material = gameRender.smaaWeightsPostProcessMaterial;
	passes[1].primarySource = resources.edgeBlend;
	passes[1].primarySourceName = resources.edgeBlendName;
	passes[1].output = resources.weightsFinal;
	passes[1].outputName = resources.weightsFinalName;
	passes[1].clearBeforeDraw = true;
	passes[1].bindPostProcessSourceSize = true;

	passes[2].passName = "blend";
	passes[2].material = gameRender.smaaBlendPostProcessMaterial;
	passes[2].primarySource = resources.sceneSource;
	passes[2].primarySourceName = resources.sceneSourceName;
	passes[2].secondarySource = resources.weightsFinal;
	passes[2].secondarySourceName = resources.weightsFinalName;
	passes[2].output = resources.edgeBlend;
	passes[2].outputName = resources.edgeBlendName;
	passes[2].clearBeforeDraw = true;
	passes[2].bindPostProcessSourceSize = true;

	passes[3].passName = "normalize";
	passes[3].material = gameRender.copyPostProcess1Material;
	passes[3].primarySource = resources.edgeBlend;
	passes[3].primarySourceName = resources.edgeBlendName;
	passes[3].output = resources.weightsFinal;
	passes[3].outputName = resources.weightsFinalName;
	passes[3].clearBeforeDraw = true;
	passes[3].bindPostProcessSourceSize = false;

	for ( int i = 0; i < 4; i++ ) {
		if ( !openQ4_ExecuteSMAAPass( passes[i] ) ) {
			renderSystem->BindRenderTexture( NULL, NULL );
			return false;
		}
	}

	renderSystem->BindRenderTexture( NULL, NULL );
	return true;
}

static const idMaterial* openQ4_SelectFinalPostProcessMaterial( rvmGameRender_t& gameRender, bool blurEnabled ) {
	if ( blurEnabled ) {
		return gameRender.blurPostProcessMaterial;
	}
	if ( g_renderCasUpscale.GetBool() && gameRender.casPostProcessMaterial != NULL ) {
		return gameRender.casPostProcessMaterial;
	}
	return gameRender.noPostProcessMaterial;
}

/*
========================
idGameLocal::ShutdownGameRenderSystem
========================
*/
void idGameLocal::ShutdownGameRenderSystem( void ) {
	if ( renderSystem != NULL ) {
		renderSystem->SetPortalSkyCaptureViewCallback( NULL );
	}

	for ( int i = 0; i < 3; i++ ) {
		if ( gameRender.postProcessRT[i] != NULL ) {
			renderSystem->DestroyRenderTexture( gameRender.postProcessRT[i] );
			gameRender.postProcessRT[i] = NULL;
		}
	}
	for ( int i = 0; i < 2; ++i ) {
		if ( gameRender.temporalHistoryRT[i] != NULL ) {
			renderSystem->DestroyRenderTexture( gameRender.temporalHistoryRT[i] );
			gameRender.temporalHistoryRT[i] = NULL;
		}
		// the other VR eye's history
		if ( gameRender.storedTemporalHistoryRT[i] != NULL ) {
			renderSystem->DestroyRenderTexture( gameRender.storedTemporalHistoryRT[i] );
			gameRender.storedTemporalHistoryRT[i] = NULL;
		}
	}

	if ( gameRender.forwardRenderPassRT != NULL ) {
		renderSystem->DestroyRenderTexture( gameRender.forwardRenderPassRT );
		gameRender.forwardRenderPassRT = NULL;
	}
	if ( gameRender.forwardRenderPassResolvedRT != NULL ) {
		renderSystem->DestroyRenderTexture( gameRender.forwardRenderPassResolvedRT );
		gameRender.forwardRenderPassResolvedRT = NULL;
	}

	gameRender.noPostProcessMaterial = NULL;
	gameRender.casPostProcessMaterial = NULL;
	gameRender.blurPostProcessMaterial = NULL;
	gameRender.blackPostProcessMaterial = NULL;
	gameRender.resolvePostProcessMaterial = NULL;
	gameRender.copyPostProcess1Material = NULL;
	gameRender.smaaEdgePostProcessMaterial = NULL;
	gameRender.smaaWeightsPostProcessMaterial = NULL;
	gameRender.smaaBlendPostProcessMaterial = NULL;
	gameRender.postProcessAvailable = false;
	gameRender.smaaAvailable = false;
	gameRender.forwardRenderSamples = 0;
	gameRender.hdrSceneTarget = false;
	gameRender.renderTargetWidth = 0;
	gameRender.renderTargetHeight = 0;
	gameRender.temporalHistoryWidth = 0;
	gameRender.temporalHistoryHeight = 0;
	gameRender.temporalHistoryReadIndex = 0;
	gameRender.temporalHistoryGeneration = 0;
	gameRender.temporalHistoryValid = false;
	gameRender.temporalHistorySlot = 0;
	gameRender.storedTemporalHistoryRT[0] = gameRender.storedTemporalHistoryRT[1] = NULL;
	gameRender.storedTemporalHistoryWidth = 0;
	gameRender.storedTemporalHistoryHeight = 0;
	gameRender.storedTemporalHistoryReadIndex = 0;
	gameRender.storedTemporalHistoryGeneration = 0;
	gameRender.storedTemporalHistoryValid = false;
	gameRender.videoRestartCount = ( renderSystem != NULL ) ? renderSystem->GetVideoRestartCount() : 0;
	gameRender.postAAWarningState = OPENQ4_POST_AA_WARNING_NONE;
}

/*
=======================================

Game Render

The engine renderer is designed to do two things, generate the geometry pass, and the shadow passes. The pipeline,
including post process, is now handled in the game code. This allows more granular control over how the final pixels,
are presented on screen based on whatever is going on in game.

=======================================
*/

static int openQ4_NextLowerMSAASampleCount( int samples ) {
	if ( samples > 8 ) {
		return 8;
	}
	if ( samples > 4 ) {
		return 4;
	}
	if ( samples > 2 ) {
		return 2;
	}
	return 0;
}

static renderPresentationState_t openQ4_GetPresentationState( void ) {
	renderPresentationState_t state;
	renderSystem->GetPresentationState( state );

	// Keep game-module startup robust when it precedes the first BeginFrame.
	// The engine normally supplies this fallback itself, but older call order
	// during a video restart can transiently expose an unlatched frame.
	if ( state.outputWidth <= 0 ) {
		state.outputWidth = Max( 1, renderSystem->GetScreenWidth() );
	}
	if ( state.outputHeight <= 0 ) {
		state.outputHeight = Max( 1, renderSystem->GetScreenHeight() );
	}
	if ( state.sceneWidth <= 0 ) {
		state.sceneWidth = state.outputWidth;
	}
	if ( state.sceneHeight <= 0 ) {
		state.sceneHeight = state.outputHeight;
	}

	return state;
}

static bool openQ4_AdvancedScreenSpaceRequested( void ) {
	return cvarSystem->GetCVarBool( "r_rendererModernQuality" )
		&& ( cvarSystem->GetCVarBool( "r_rendererFroxelVolumetrics" )
			|| cvarSystem->GetCVarBool( "r_rendererSSR" )
			|| cvarSystem->GetCVarBool( "r_rendererSSGI" ) );
}

static idRenderTexture* openQ4_CreateTemporalHistoryRenderTarget(
		int historyIndex, int targetWidth, int targetHeight ) {
	if ( targetWidth <= 0 || targetHeight <= 0 ) {
		return NULL;
	}

	idImageOpts opts;
	opts.format = FMT_RGBA8;
	opts.colorFormat = CFM_DEFAULT;
	opts.numLevels = 1;
	opts.textureType = TT_2D;
	opts.isPersistant = true;
	opts.width = targetWidth;
	opts.height = targetHeight;
	opts.numMSAASamples = 0;

	idImage *historyImage = renderSystem->CreateImage(
		va( "_temporalHistoryAlbedo%d", historyIndex ), &opts, TF_LINEAR );
	if ( historyImage == NULL ) {
		return NULL;
	}
	idRenderTexture *historyTarget =
		renderSystem->CreateRenderTexture( historyImage, NULL, NULL );
	if ( historyTarget != NULL ) {
		renderSystem->SetRenderTextureDebugName(
			historyTarget,
			va( "openQ4 temporal history %d (native)", historyIndex ) );
	}
	return historyTarget;
}

static void openQ4_ResetTemporalHistoryOwnership(
		rvmGameRender_t& gameRender, unsigned int historyGeneration ) {
	gameRender.temporalHistoryReadIndex = 0;
	gameRender.temporalHistoryGeneration = historyGeneration;
	gameRender.temporalHistoryValid = false;
}

static bool openQ4_SynchronizeTemporalHistory(
		rvmGameRender_t& gameRender,
		const renderPresentationState_t& presentation ) {
	if ( gameRender.temporalHistoryGeneration != presentation.historyGeneration ) {
		openQ4_ResetTemporalHistoryOwnership(
			gameRender, presentation.historyGeneration );
	}

	if ( presentation.outputWidth <= 0 || presentation.outputHeight <= 0 ) {
		return false;
	}

	bool resourcesChanged =
		gameRender.temporalHistoryWidth != presentation.outputWidth
		|| gameRender.temporalHistoryHeight != presentation.outputHeight;
	bool resourcesReady = true;
	for ( int i = 0; i < 2; ++i ) {
		idRenderTexture *&history = gameRender.temporalHistoryRT[i];
		int historyWidth = 0;
		int historyHeight = 0;
		QueryRenderTextureSize( history, historyWidth, historyHeight );
		if ( history != NULL
				&& ( historyWidth != presentation.outputWidth
					|| historyHeight != presentation.outputHeight ) ) {
			resourcesChanged = true;
			resourcesReady = renderSystem->ResizeRenderTexture(
				history,
				presentation.outputWidth,
				presentation.outputHeight ) && resourcesReady;
		}
		if ( history == NULL ) {
			resourcesChanged = true;
			// a VR frame's right eye keeps images of its own (VR-05)
			history = openQ4_CreateTemporalHistoryRenderTarget(
				gameRender.temporalHistorySlot * 2 + i,
				presentation.outputWidth,
				presentation.outputHeight );
		}
		QueryRenderTextureSize( history, historyWidth, historyHeight );
		resourcesReady = history != NULL
			&& historyWidth == presentation.outputWidth
			&& historyHeight == presentation.outputHeight
			&& resourcesReady;
	}

	if ( resourcesChanged ) {
		gameRender.temporalHistoryWidth = presentation.outputWidth;
		gameRender.temporalHistoryHeight = presentation.outputHeight;
		openQ4_ResetTemporalHistoryOwnership(
			gameRender, presentation.historyGeneration );
	}
	if ( !resourcesReady ) {
		common->Warning(
			"Temporal presentation history is unavailable at %d x %d; using spatial presentation.",
			presentation.outputWidth,
			presentation.outputHeight );
	}

	return resourcesReady;
}

static bool openQ4_ResolveTemporalPresentation(
		rvmGameRender_t& gameRender,
		idRenderTexture *sceneColorTarget,
		idRenderTexture *sceneDepthTarget ) {
	// The primary view can invalidate history after the frame state was first
	// queried (for example, on a camera cut), so consume a fresh generation.
	const renderPresentationState_t presentation = openQ4_GetPresentationState();
	const bool screenSpaceRequested = openQ4_AdvancedScreenSpaceRequested();
	const bool temporalHistoryEligible = presentation.temporalAARequested
		&& !presentation.captureFrozen && !presentation.captureForcedNative;
	if ( ( !temporalHistoryEligible && !screenSpaceRequested )
			|| sceneColorTarget == NULL || sceneDepthTarget == NULL ) {
		return false;
	}

	const bool historyResourcesReady = temporalHistoryEligible
		&& openQ4_SynchronizeTemporalHistory( gameRender, presentation );
	const int historyWriteIndex = gameRender.temporalHistoryReadIndex ^ 1;
	idRenderTexture *historyReadTarget = historyResourcesReady
		&& gameRender.temporalHistoryValid
		? gameRender.temporalHistoryRT[gameRender.temporalHistoryReadIndex]
		: NULL;
	idRenderTexture *historyWriteTarget = historyResourcesReady
		? gameRender.temporalHistoryRT[historyWriteIndex]
		: NULL;
	if ( !renderSystem->ResolveTemporalPresentation(
			sceneColorTarget,
			sceneDepthTarget,
			historyReadTarget,
			historyWriteTarget ) ) {
		return false;
	}

	if ( temporalHistoryEligible ) {
		gameRender.temporalHistoryGeneration = presentation.historyGeneration;
	}
	if ( temporalHistoryEligible && historyResourcesReady ) {
		gameRender.temporalHistoryReadIndex = historyWriteIndex;
		gameRender.temporalHistoryValid = true;
	} else {
		gameRender.temporalHistoryValid = false;
	}
	return true;
}

static bool openQ4_HDRSceneTargetRequested( void ) {
	// Both backends tone-map the scene before authored post effects, SMAA and
	// native HUD composition. The game-owned forward/MSAA resolve must retain
	// HDR energy through that handoff, including reduced-resolution scenes.
	return !cvarSystem->GetCVarBool( "r_skipPostProcess" )
		&& cvarSystem->GetCVarBool( "r_hdrSceneTarget" )
		&& ( cvarSystem->GetCVarBool( "r_hdrToneMap" ) || cvarSystem->GetCVarInteger( "r_hdrDebugView" ) > 0 );
}

static idRenderTexture* openQ4_CreateForwardRenderTarget( int requestedSamples,
		int targetWidth, int targetHeight, bool hdrScene, int& effectiveSamples ) {
	effectiveSamples = 0;
	int attemptSamples = Max( 0, requestedSamples );

	for ( ;; ) {
		idImageOpts albedoOpts;
		// Preserve stock blend clamping unless HDR tone mapping was explicitly
		// requested. Post-AA targets stay LDR after the engine tone-map pass.
		albedoOpts.format = hdrScene ? FMT_RGBA16F : FMT_RGBA8;
		albedoOpts.colorFormat = CFM_DEFAULT;
		albedoOpts.numLevels = 1;
		albedoOpts.textureType = TT_2D;
		albedoOpts.isPersistant = true;
		albedoOpts.width = targetWidth;
		albedoOpts.height = targetHeight;
		albedoOpts.numMSAASamples = attemptSamples;

		idImage* albedoImage = renderSystem->CreateImage( "_forwardRenderAlbedo", &albedoOpts, TF_LINEAR );

		idImageOpts depthOpts = albedoOpts;
		depthOpts.format = FMT_DEPTH_STENCIL;
		depthOpts.numMSAASamples = attemptSamples;
		idImage* depthImage = renderSystem->CreateImage( "_forwardRenderDepth", &depthOpts, TF_LINEAR );

		if ( albedoImage == NULL || depthImage == NULL ) {
			common->Warning( "Forward render target image allocation failed; falling back to direct rendering." );
			return NULL;
		}

		const int colorSamples = Max( 0, renderSystem->GetImageMSAASamples( albedoImage ) );
		const int depthSamples = Max( 0, renderSystem->GetImageMSAASamples( depthImage ) );
		if ( colorSamples != depthSamples ) {
			common->Warning(
				"Forward render target attachments disagree on MSAA (%d color, %d depth); retrying lower.",
				colorSamples,
				depthSamples );
			// Retry the next step below the larger allocated count. Using the
			// smaller count would skip a potentially compatible tier: for example,
			// a 4x color image paired with an 8x depth image should retry 4x,
			// rather than dropping directly to 2x.
			attemptSamples = openQ4_NextLowerMSAASampleCount( Max( colorSamples, depthSamples ) );
			if ( attemptSamples <= 0 && ( colorSamples > 0 || depthSamples > 0 ) ) {
				continue;
			}
			if ( attemptSamples <= 0 ) {
				break;
			}
			continue;
		}

		const int allocatedSamples = colorSamples;
		idRenderTexture* renderTarget = renderSystem->CreateRenderTexture( albedoImage, depthImage );
		if ( renderTarget != NULL ) {
			effectiveSamples = allocatedSamples;
			return renderTarget;
		}

		common->Warning(
			"Forward render target creation failed at %d MSAA samples; retrying lower.",
			allocatedSamples );
		if ( allocatedSamples <= 0 ) {
			break;
		}
		attemptSamples = openQ4_NextLowerMSAASampleCount( allocatedSamples );
	}

	common->Warning( "Forward render targets are unavailable; falling back to direct rendering." );
	return NULL;
}

/*
========================
idGameLocal::InitGameRenderSystem
========================
*/
void idGameLocal::InitGameRenderSystem(void) {
	ShutdownGameRenderSystem();

	if ( !renderSystem->IsOpenGLRunning() ) {
		return;
	}

	renderSystem->SetPortalSkyCaptureViewCallback( openQ4_BuildPortalSkyCaptureView );

	const renderPresentationState_t presentation = openQ4_GetPresentationState();
	const int targetWidth = presentation.sceneWidth;
	const int targetHeight = presentation.sceneHeight;
	const int requestedMsaaSamples = Max( 0, cvarSystem->GetCVarInteger( "r_multiSamples" ) );
	gameRender.hdrSceneTarget = openQ4_HDRSceneTargetRequested();

	gameRender.forwardRenderPassRT = openQ4_CreateForwardRenderTarget(
		requestedMsaaSamples,
		targetWidth,
		targetHeight,
		gameRender.hdrSceneTarget,
		gameRender.forwardRenderSamples );

	for(int i = 0; i < 3; i++)
	{
		idImageOpts opts;
		opts.format = FMT_RGBA8;
		opts.colorFormat = CFM_DEFAULT;
		opts.numLevels = 1;
		opts.textureType = TT_2D;
		opts.isPersistant = true;
		opts.width = targetWidth;
		opts.height = targetHeight;
		opts.numMSAASamples = 0;

		idImage* albedoImage = renderSystem->CreateImage(va("_postProcessAlbedo%d", i), &opts, TF_LINEAR);
		opts.format = FMT_DEPTH_STENCIL;
		idImage* depthImage = renderSystem->CreateImage(va("_postProcessDepth%d", i), &opts, TF_LINEAR);

		gameRender.postProcessRT[i] = renderSystem->CreateRenderTexture(albedoImage, depthImage, NULL);
	}

	{
		idImageOpts opts;
		opts.format = FMT_RGBA8;
		opts.colorFormat = CFM_DEFAULT;
		opts.numLevels = 1;
		opts.textureType = TT_2D;
		opts.isPersistant = true;
		opts.width = targetWidth;
		opts.height = targetHeight;
		opts.numMSAASamples = 0;

		opts.format = gameRender.hdrSceneTarget ? FMT_RGBA16F : FMT_RGBA8;
		idImage *albedoImage = renderSystem->CreateImage("_forwardRenderResolvedAlbedo", &opts, TF_LINEAR);
		// Match the forward pass depth attachment format so depth resolves/blits are valid
		// on drivers that reject DEPTH_STENCIL -> DEPTH-only copies.
		opts.format = FMT_DEPTH_STENCIL;
		idImage *depthImage = renderSystem->CreateImage("_forwardRenderResolvedDepth", &opts, TF_LINEAR);

		gameRender.forwardRenderPassResolvedRT = renderSystem->CreateRenderTexture(albedoImage, depthImage);
	}

	// Histories are intentionally lazy: the default-off feature carries no
	// native-resolution ping-pong allocation cost on a clean configuration.
	gameRender.temporalHistoryWidth = 0;
	gameRender.temporalHistoryHeight = 0;
	openQ4_ResetTemporalHistoryOwnership(
		gameRender, presentation.historyGeneration );
	if ( presentation.temporalAARequested ) {
		(void)openQ4_SynchronizeTemporalHistory( gameRender, presentation );
	}

	openQ4_LabelGameRenderTargets( gameRender );

	gameRender.blackPostProcessMaterial = FindPostProcessMaterial( "postprocess/black", "postprocess/openq4_black" );
	gameRender.noPostProcessMaterial = FindPostProcessMaterial( "postprocess/nopostprocess", "postprocess/openq4_nopostprocess" );
	gameRender.casPostProcessMaterial = FindPostProcessMaterial( "postprocess/casupscale", "postprocess/openq4_casupscale" );
	gameRender.blurPostProcessMaterial = FindPostProcessMaterial( "postprocess/blur", "postprocess/openq4_blur" );
	gameRender.resolvePostProcessMaterial = FindPostProcessMaterial( "postprocess/resolvepostprocess", "postprocess/openq4_resolvepostprocess" );
	gameRender.copyPostProcess1Material = FindPostProcessMaterial( "postprocess/copy_postprocess1", "postprocess/openq4_copy_postprocess1" );
	gameRender.smaaEdgePostProcessMaterial = FindPostProcessMaterial( "postprocess/smaa_edge", "postprocess/openq4_smaa_edge" );
	gameRender.smaaWeightsPostProcessMaterial = FindPostProcessMaterial( "postprocess/smaa_weights", "postprocess/openq4_smaa_weights" );
	gameRender.smaaBlendPostProcessMaterial = FindPostProcessMaterial( "postprocess/smaa_blend", "postprocess/openq4_smaa_blend" );
	gameRender.postProcessAvailable = (gameRender.noPostProcessMaterial != NULL) &&
		(gameRender.resolvePostProcessMaterial != NULL);
	gameRender.smaaAvailable = EvaluateSMAAAvailability( gameRender );
	{
		const openq4PostAASchedule_t postAASchedule = BuildPostAAPasses( gameRender, cvarSystem->GetCVarBool( "r_usePostLightingStack" ) );
		UpdatePostAAWarningState( gameRender, PostAAWarningStateForSchedule( postAASchedule ) );
		LogPostAASchedule( postAASchedule, gameRender.smaaAvailable );
	}
	if (!gameRender.postProcessAvailable) {
		common->Warning("Postprocess materials missing or invalid; falling back to direct render.");
	}

	common->Printf(
		"Forward render target MSAA: requested %d, effective %d\n",
		requestedMsaaSamples,
		gameRender.forwardRenderSamples );

	gameRender.renderTargetWidth = targetWidth;
	gameRender.renderTargetHeight = targetHeight;
}

/*
========================
idGameLocal::ResizeRenderTextures
========================
*/
void idGameLocal::ResizeRenderTextures(int width, int height) {
	// Resize all of the different render textures.
	bool resizeSucceeded = true;
	if ( gameRender.forwardRenderPassRT != NULL ) {
		resizeSucceeded = renderSystem->ResizeRenderTexture( gameRender.forwardRenderPassRT, width, height ) && resizeSucceeded;
	}
	if ( gameRender.forwardRenderPassResolvedRT != NULL ) {
		resizeSucceeded = renderSystem->ResizeRenderTexture( gameRender.forwardRenderPassResolvedRT, width, height ) && resizeSucceeded;
	}
	for ( int i = 0; i < 3; i++ ) {
		if ( gameRender.postProcessRT[i] != NULL ) {
			resizeSucceeded = renderSystem->ResizeRenderTexture( gameRender.postProcessRT[i], width, height ) && resizeSucceeded;
		}
	}
	if ( !resizeSucceeded ) {
		common->Warning( "A resized game render target became unavailable; rebuilding the pipeline with fallbacks." );
		InitGameRenderSystem();
		return;
	}

	gameRender.renderTargetWidth = width;
	gameRender.renderTargetHeight = height;
}

/*
====================
idGameLocal::SelectTemporalHistorySlot

openQ4 VR-05: a VR frame renders each eye through the same scene chain, and
temporal AA reprojects each from its own previous frame. The history the
active slot renders with lives in gameRender's temporalHistory fields; the
other slot's waits in its stored fields, and the two trade places here.
====================
*/
void idGameLocal::SelectTemporalHistorySlot( int slot ) {
	slot = slot == 1 ? 1 : 0;
	if ( slot == gameRender.temporalHistorySlot ) {
		return;
	}
	for ( int i = 0; i < 2; ++i ) {
		idRenderTexture *saved = gameRender.temporalHistoryRT[i];
		gameRender.temporalHistoryRT[i] = gameRender.storedTemporalHistoryRT[i];
		gameRender.storedTemporalHistoryRT[i] = saved;
	}
	const int savedWidth = gameRender.temporalHistoryWidth;
	const int savedHeight = gameRender.temporalHistoryHeight;
	const int savedReadIndex = gameRender.temporalHistoryReadIndex;
	const unsigned int savedGeneration = gameRender.temporalHistoryGeneration;
	const bool savedValid = gameRender.temporalHistoryValid;
	gameRender.temporalHistoryWidth = gameRender.storedTemporalHistoryWidth;
	gameRender.temporalHistoryHeight = gameRender.storedTemporalHistoryHeight;
	gameRender.temporalHistoryReadIndex = gameRender.storedTemporalHistoryReadIndex;
	gameRender.temporalHistoryGeneration = gameRender.storedTemporalHistoryGeneration;
	gameRender.temporalHistoryValid = gameRender.storedTemporalHistoryValid;
	gameRender.storedTemporalHistoryWidth = savedWidth;
	gameRender.storedTemporalHistoryHeight = savedHeight;
	gameRender.storedTemporalHistoryReadIndex = savedReadIndex;
	gameRender.storedTemporalHistoryGeneration = savedGeneration;
	gameRender.storedTemporalHistoryValid = savedValid;
	gameRender.temporalHistorySlot = slot;
}

/*
====================
idGameLocal::RenderScene
====================
*/
void idGameLocal::RenderScene(const renderView_t *view, idRenderWorld *renderWorld, idCamera* portalSky, int renderFlags) {
	if ( view == NULL || renderWorld == NULL ) {
		return;
	}

	const bool previousUIViewportMode = renderSystem->GetUseUIViewportFor2D();
	renderSystem->SetUseUIViewportFor2D( false );

	const int currentVideoRestartCount = renderSystem->GetVideoRestartCount();
	if ( gameRender.videoRestartCount != currentVideoRestartCount ) {
		common->Printf( "Reinitializing game render targets after vid_restart (%d -> %d)\n",
			gameRender.videoRestartCount, currentVideoRestartCount );
		InitGameRenderSystem();
	}
	if ( gameRender.hdrSceneTarget != openQ4_HDRSceneTargetRequested() ) {
		common->Printf( "Reinitializing game scene targets for %s rendering\n",
			openQ4_HDRSceneTargetRequested() ? "HDR" : "LDR" );
		InitGameRenderSystem();
	}

	const renderPresentationState_t presentation = openQ4_GetPresentationState();
	const int screenWidth = presentation.outputWidth;
	const int screenHeight = presentation.outputHeight;
	const int sceneWidth = presentation.sceneWidth;
	const int sceneHeight = presentation.sceneHeight;
	const bool havePresentationDimensions =
		( screenWidth > 0 ) && ( screenHeight > 0 ) &&
		( sceneWidth > 0 ) && ( sceneHeight > 0 );

	bool needsTargetResize = false;
	if ( havePresentationDimensions &&
		gameRender.forwardRenderPassRT != NULL &&
		gameRender.forwardRenderPassResolvedRT != NULL &&
		gameRender.postProcessRT[0] != NULL ) {
		if ( gameRender.renderTargetWidth != sceneWidth ||
			gameRender.renderTargetHeight != sceneHeight ) {
			needsTargetResize = true;
		}
	}

	if ( needsTargetResize ) {
		common->Printf(
			"Resizing game scene targets to %d x %d for %d x %d output (%d%%)\n",
			sceneWidth,
			sceneHeight,
			screenWidth,
			screenHeight,
			presentation.effectiveScalePercent );
		ResizeRenderTextures( sceneWidth, sceneHeight );
	}
	// History remains native-resolution while dynamic resolution only resizes
	// the scene/post chain. Keep it entirely lazy while temporal AA is off.
	const bool temporalHistoryReady = presentation.temporalAARequested
		&& openQ4_SynchronizeTemporalHistory( gameRender, presentation );

	const bool canUsePostProcess = havePresentationDimensions &&
		gameRender.postProcessAvailable &&
		gameRender.forwardRenderPassRT != NULL &&
		gameRender.forwardRenderPassResolvedRT != NULL &&
		gameRender.postProcessRT[0] != NULL &&
		gameRender.resolvePostProcessMaterial != NULL;

	if ( !canUsePostProcess ) {
		// Fallback for stock Quake 4 assets or transient render-target invalidation.
		openQ4_RenderSceneDirect( view, renderWorld, portalSky, renderFlags );
		renderSystem->SetUseUIViewportFor2D( previousUIViewportMode );
		return;
	}

	// All game-owned 3D and post-process resources share the latched scene
	// extent. The final pass binds the native backbuffer, so it upscales while
	// HUD/menu submissions that follow continue at the output extent.
	renderSystem->SetPostProcessSourceSize(
		gameRender.renderTargetWidth,
		gameRender.renderTargetHeight );
	// Minimum render is used for screen captures(such as envcapture) calls, caller is responsible for all rendertarget setup.
	//if (view->minimumRender)
	//{
	//	RenderSky(view);
	//	if (view->cubeMapTargetImage)
	//	{
	//		renderView_t worldRefDef = *view;
	//		worldRefDef.cubeMapClearBuffer = false;
	//		renderWorld->RenderScene(&worldRefDef);
	//	}
	//	else
	//	{
	//		renderWorld->RenderScene(view);
	//	}
	//
	//	return;
	//}

	const bool blurEnabled = IsSpecialEffectEnabled( SPECIAL_EFFECT_BLUR ) &&
		( gameRender.blurPostProcessMaterial != NULL );
	const bool wantsCAS = g_renderCasUpscale.GetBool() && gameRender.casPostProcessMaterial != NULL;
	openq4PostAASchedule_t postAASchedule = BuildPostAAPasses( gameRender, cvarSystem->GetCVarBool( "r_usePostLightingStack" ) );
	const bool wantsSMAA = PostAAModeUsesSMAA( postAASchedule.requestedMode );
	if ( wantsSMAA && gameRender.smaaAvailable ) {
		const bool previousSMAAAvailable = gameRender.smaaAvailable;
		gameRender.smaaAvailable = EvaluateSMAAAvailability( gameRender );
		if ( gameRender.smaaAvailable != previousSMAAAvailable ) {
			postAASchedule = BuildPostAAPasses( gameRender, cvarSystem->GetCVarBool( "r_usePostLightingStack" ) );
		}
	}
	UpdatePostAAWarningState( gameRender, PostAAWarningStateForSchedule( postAASchedule ) );
	LogPostAASchedule( postAASchedule, gameRender.smaaAvailable );

	const bool useSMAA = PostAAModeUsesSMAA( postAASchedule.effectiveMode );
	const bool screenSpaceRequested = openQ4_AdvancedScreenSpaceRequested();
	const bool temporalResolveCandidate =
		( ( presentation.temporalAARequested
			&& !presentation.captureFrozen
			&& !presentation.captureForcedNative )
			|| screenSpaceRequested )
		&& gameRender.postProcessRT[1] != NULL;

	// Only SMAA that will run needs the offscreen chain. A request the context
	// cannot run (Apple's GL 2.1 context has no GLSL 1.30) keeps the direct
	// route, the only one that reaches its multisampled window.
	const bool canUseFastNoPost =
		g_renderFastNoPost.GetBool() &&
		gameRender.forwardRenderSamples <= 0 &&
		!blurEnabled &&
		!useSMAA &&
		!wantsCAS &&
		!presentation.temporalAARequested &&
		!screenSpaceRequested;

	if ( canUseFastNoPost ) {
		if ( g_renderFastNoPostDirect.GetBool() &&
			sceneWidth == screenWidth && sceneHeight == screenHeight ) {
			openQ4_RenderSceneDirect( view, renderWorld, portalSky, renderFlags );
			if ( g_renderCaptureCurrentRender.GetBool() ) {
				renderSystem->CaptureRenderToImage( "_currentRender" );
			}
			renderSystem->SetUseUIViewportFor2D( previousUIViewportMode );
			return;
		}

		openQ4_BindSceneRenderTexture( gameRender.forwardRenderPassResolvedRT );
		renderSystem->ClearRenderTarget( true, true, 1.0f, 0.0f, 0.0f, 0.0f );
		openQ4_RenderSceneWorld( view, renderWorld, portalSky, renderFlags );
		renderSystem->BindRenderTexture( nullptr, nullptr );

		// The no-post path previously copied _forwardRenderResolvedAlbedo through
		// _postProcessAlbedo0 before presenting it. Presenting the resolved scene
		// material directly removes two full-screen passes without changing pixels.
		renderSystem->ClearRenderTarget( false, true, 1.0f, 0.0f, 0.0f, 0.0f );
		if ( !openQ4_PresentScaledScene( presentation, gameRender.forwardRenderPassResolvedRT ) ) {
			openQ4_DrawFullScreenMaterial( gameRender.resolvePostProcessMaterial );
		}

		if ( g_renderCaptureCurrentRender.GetBool() ) {
			renderSystem->CaptureRenderToImage( "_currentRender" );
		}
		renderSystem->SetUseUIViewportFor2D( previousUIViewportMode );
		return;
	}

	// Render the scene to the forward render pass rendertexture.
	openQ4_BindSceneRenderTexture( gameRender.forwardRenderPassRT );
	{
		// Clear the color/depth buffers
		renderSystem->ClearRenderTarget(true, true, 1.0f, 0.0f, 0.0f, 0.0f);
	
		openQ4_RenderSceneWorld( view, renderWorld, portalSky, renderFlags );
	}
	renderSystem->BindRenderTexture(nullptr, nullptr);

	// Resolve our MSAA buffer.
	renderSystem->ResolveMSAA(
		gameRender.forwardRenderPassRT,
		gameRender.forwardRenderPassResolvedRT,
		blurEnabled || presentation.temporalAARequested || screenSpaceRequested
			|| cvarSystem->GetCVarBool( "r_msaaResolveDepth" ) );

	// Temporal AA consumes the pre-SMAA scene. Keep the mature SMAA schedule
	// intact so it can run immediately if temporal enqueue rejects.
	idRenderTexture *initialResolveTarget = temporalResolveCandidate
		&& temporalHistoryReady
		? gameRender.postProcessRT[0]
		: postAASchedule.resolveTarget;
	renderSystem->BindRenderTexture(initialResolveTarget, nullptr);
		renderSystem->ClearRenderTarget( true, true, 1.0f, 0.0f, 0.0f, 0.0f );
		openQ4_DrawFullScreenMaterial( gameRender.resolvePostProcessMaterial );
	renderSystem->BindRenderTexture( nullptr, nullptr );
	if ( useSMAA && ( !temporalResolveCandidate || !temporalHistoryReady ) ) {
		if ( !openQ4_ApplySMAA( gameRender, postAASchedule.sourceColorSpace, postAASchedule.effectiveMode ) ) {
			renderSystem->BindRenderTexture( gameRender.postProcessRT[0], nullptr );
			renderSystem->ClearRenderTarget( true, true, 1.0f, 0.0f, 0.0f, 0.0f );
			openQ4_DrawFullScreenMaterial( gameRender.resolvePostProcessMaterial );
			renderSystem->BindRenderTexture( nullptr, nullptr );
		}
	}

	const idMaterial* finalMaterial = openQ4_SelectFinalPostProcessMaterial( gameRender, blurEnabled );
	if ( finalMaterial == NULL ) {
		openQ4_RenderSceneDirect( view, renderWorld, portalSky, renderFlags );
		renderSystem->SetUseUIViewportFor2D( previousUIViewportMode );
		return;
	}

	if ( temporalResolveCandidate ) {
		// Keep all game post effects at the scene extent. The renderer temporal
		// resolve is then the sole native-output scene operation before UI.
		renderSystem->BindRenderTexture( gameRender.postProcessRT[1], NULL );
		renderSystem->ClearRenderTarget(
			true, true, 1.0f, 0.0f, 0.0f, 0.0f );
		openQ4_DrawFullScreenMaterial( finalMaterial );
		renderSystem->BindRenderTexture( NULL, NULL );

		if ( openQ4_ResolveTemporalPresentation(
				gameRender,
				gameRender.postProcessRT[1],
				gameRender.forwardRenderPassResolvedRT ) ) {
			if ( g_renderCaptureCurrentRender.GetBool() ) {
				renderSystem->CaptureRenderToImage( "_currentRender" );
			}
			renderSystem->SetUseUIViewportFor2D( previousUIViewportMode );
			return;
		}

		// The renderer rejected the temporal command before enqueue. Rebuild the
		// ordinary SMAA input and run its compatibility schedule now; accepted
		// temporal frames never stack both AA filters.
		if ( useSMAA && temporalHistoryReady ) {
			renderSystem->BindRenderTexture( postAASchedule.resolveTarget, NULL );
			renderSystem->ClearRenderTarget(
				true, true, 1.0f, 0.0f, 0.0f, 0.0f );
			openQ4_DrawFullScreenMaterial( gameRender.resolvePostProcessMaterial );
			renderSystem->BindRenderTexture( NULL, NULL );
			if ( !openQ4_ApplySMAA( gameRender,
					postAASchedule.sourceColorSpace,
					postAASchedule.effectiveMode ) ) {
				renderSystem->BindRenderTexture( gameRender.postProcessRT[0], NULL );
				renderSystem->ClearRenderTarget(
					true, true, 1.0f, 0.0f, 0.0f, 0.0f );
				openQ4_DrawFullScreenMaterial(
					gameRender.resolvePostProcessMaterial );
				renderSystem->BindRenderTexture( NULL, NULL );
			}
		}
	}

	// Both the SMAA and the no-AA schedules finish in _postProcessAlbedo0, and
	// the no-post material is its plain copy, so a renderer upscale presents
	// that target directly. Blur and CAS first run at the scene extent into
	// _postProcessAlbedo1, exactly as before a temporal resolve.
	idRenderTexture *scaledSceneSource = finalMaterial == gameRender.noPostProcessMaterial
		? gameRender.postProcessRT[0] : gameRender.postProcessRT[1];
	if ( scaledSceneSource != gameRender.postProcessRT[0] && scaledSceneSource != NULL
			&& openQ4_ScaledScenePresentationRequested( presentation ) ) {
		renderSystem->BindRenderTexture( scaledSceneSource, NULL );
		renderSystem->ClearRenderTarget( true, true, 1.0f, 0.0f, 0.0f, 0.0f );
		openQ4_DrawFullScreenMaterial( finalMaterial );
		renderSystem->BindRenderTexture( NULL, NULL );
	}

	// SS_POST_PROCESS stages use depth testing; reset backbuffer depth each frame
	// so final full-screen composition is deterministic across drivers/devices.
	renderSystem->ClearRenderTarget( false, true, 1.0f, 0.0f, 0.0f, 0.0f );
	if ( !openQ4_PresentScaledScene( presentation, scaledSceneSource ) ) {
		openQ4_DrawFullScreenMaterial( finalMaterial );
	}

	if ( g_renderCaptureCurrentRender.GetBool() ) {
		renderSystem->CaptureRenderToImage( "_currentRender" );
	}
	renderSystem->SetUseUIViewportFor2D( previousUIViewportMode );
}
