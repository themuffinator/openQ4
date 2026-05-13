// Copyright (C) 2004 Id Software, Inc.
//

#include "tr_local.h"

static idStrList		rg_extensionTokens;
static idStr			rg_extensionString;

static bool RendererStringEquals( const char *a, const char *b ) {
	if ( a == NULL || b == NULL ) {
		return false;
	}
	return idStr::Icmp( a, b ) == 0;
}

const char *RendererTier_Name( rendererTier_t tier ) {
	switch ( tier ) {
	case RENDERER_TIER_NULL:
		return "NullRenderer";
	case RENDERER_TIER_LEGACY_GL2_COMPAT:
		return "LegacyGL2Compat";
	case RENDERER_TIER_MODERN_GL33:
		return "ModernGL33";
	case RENDERER_TIER_MODERN_GL41:
		return "ModernGL41";
	case RENDERER_TIER_GPU_DRIVEN_GL43:
		return "GpuDrivenGL43";
	case RENDERER_TIER_LOW_OVERHEAD_GL45:
		return "LowOverheadGL45";
	case RENDERER_TIER_TOP_GL46:
		return "TopGL46";
	default:
		return "Unknown";
	}
}

const char *RendererTier_CVarName( rendererTier_t tier ) {
	switch ( tier ) {
	case RENDERER_TIER_LEGACY_GL2_COMPAT:
		return "legacy";
	case RENDERER_TIER_MODERN_GL33:
		return "gl33";
	case RENDERER_TIER_MODERN_GL41:
		return "gl41";
	case RENDERER_TIER_GPU_DRIVEN_GL43:
		return "gl43";
	case RENDERER_TIER_LOW_OVERHEAD_GL45:
		return "gl45";
	case RENDERER_TIER_TOP_GL46:
		return "gl46";
	case RENDERER_TIER_NULL:
	default:
		return "null";
	}
}

const char *RendererContextProfile_Name( rendererContextProfile_t profile ) {
	switch ( profile ) {
	case RENDERER_CONTEXT_PROFILE_COMPATIBILITY:
		return "compatibility";
	case RENDERER_CONTEXT_PROFILE_CORE:
		return "core";
	case RENDERER_CONTEXT_PROFILE_UNKNOWN:
	default:
		return "unknown";
	}
}

typedef struct rendererContextLadderStep_s {
	int							major;
	int							minor;
	rendererTierPreference_t		preference;
	const char					*coreLabel;
	const char					*compatibilityLabel;
} rendererContextLadderStep_t;

static const rendererContextLadderStep_t rg_contextLadderSteps[] = {
	{ 4, 6, RENDERER_TIER_PREF_GL46, "4.6 core", "4.6 compatibility" },
	{ 4, 5, RENDERER_TIER_PREF_GL45, "4.5 core", "4.5 compatibility" },
	{ 4, 3, RENDERER_TIER_PREF_GL43, "4.3 core", "4.3 compatibility" },
	{ 4, 1, RENDERER_TIER_PREF_GL41, "4.1 core", "4.1 compatibility" },
	{ 3, 3, RENDERER_TIER_PREF_GL33, "3.3 core", "3.3 compatibility" }
};

static int RendererContextLadder_FirstStepForPreference( rendererTierPreference_t preference ) {
	for ( int i = 0; i < static_cast<int>( sizeof( rg_contextLadderSteps ) / sizeof( rg_contextLadderSteps[0] ) ); ++i ) {
		if ( rg_contextLadderSteps[i].preference == preference ) {
			return i;
		}
	}
	return 0;
}

static bool RendererContextLadder_AddCandidate(
	rendererContextCandidate_t *candidates,
	int maxCandidates,
	int &count,
	int major,
	int minor,
	rendererContextProfile_t profile,
	bool explicitVersion,
	bool debugContext,
	const char *label ) {
	if ( candidates == NULL || count >= maxCandidates ) {
		return false;
	}

	rendererContextCandidate_t &candidate = candidates[count++];
	memset( &candidate, 0, sizeof( candidate ) );
	candidate.major = major;
	candidate.minor = minor;
	candidate.profile = profile;
	candidate.explicitVersion = explicitVersion;
	candidate.debugContext = debugContext;
	idStr::snPrintf(
		candidate.label,
		sizeof( candidate.label ),
		"%s%s",
		label ? label : "unknown",
		debugContext ? " debug" : "" );
	return true;
}

static void RendererContextLadder_AddCompatibilityFallback(
	rendererContextCandidate_t *candidates,
	int maxCandidates,
	int &count,
	bool debugContext ) {
	(void)RendererContextLadder_AddCandidate(
		candidates,
		maxCandidates,
		count,
		0,
		0,
		RENDERER_CONTEXT_PROFILE_COMPATIBILITY,
		false,
		debugContext,
		"compatibility fallback" );
}

static void RendererContextLadder_AddVersionRange(
	rendererContextCandidate_t *candidates,
	int maxCandidates,
	int &count,
	int firstStep,
	rendererContextProfile_t profile,
	bool debugContext ) {
	const int stepCount = static_cast<int>( sizeof( rg_contextLadderSteps ) / sizeof( rg_contextLadderSteps[0] ) );
	for ( int i = firstStep; i < stepCount; ++i ) {
		const rendererContextLadderStep_t &step = rg_contextLadderSteps[i];
		const char *label = profile == RENDERER_CONTEXT_PROFILE_CORE ? step.coreLabel : step.compatibilityLabel;
		(void)RendererContextLadder_AddCandidate(
			candidates,
			maxCandidates,
			count,
			step.major,
			step.minor,
			profile,
			true,
			debugContext,
			label );
	}
}

int RendererContextLadder_Build(
	rendererContextCandidate_t *candidates,
	int maxCandidates,
	rendererTierPreference_t preference,
	bool debugContext,
	bool keepAutoCompatibility ) {
	if ( candidates == NULL || maxCandidates <= 0 ) {
		return 0;
	}

	int count = 0;
	const int passCount = debugContext ? 2 : 1;
	const rendererTier_t forcedTier = RendererTierPreference_ToForcedTier( preference );
	const bool forceModernCore = RendererTier_IsModern( forcedTier );
	const bool compatibilityOnly = preference == RENDERER_TIER_PREF_LEGACY || ( preference == RENDERER_TIER_PREF_AUTO && keepAutoCompatibility );
	const int firstStep = forceModernCore ? RendererContextLadder_FirstStepForPreference( preference ) : 0;

	for ( int pass = 0; pass < passCount; ++pass ) {
		const bool debugCandidate = debugContext && pass == 0;

		if ( compatibilityOnly ) {
			if ( preference == RENDERER_TIER_PREF_LEGACY ) {
				RendererContextLadder_AddCompatibilityFallback( candidates, maxCandidates, count, debugCandidate );
			} else {
				RendererContextLadder_AddVersionRange( candidates, maxCandidates, count, 0, RENDERER_CONTEXT_PROFILE_COMPATIBILITY, debugCandidate );
				RendererContextLadder_AddCompatibilityFallback( candidates, maxCandidates, count, debugCandidate );
			}
			continue;
		}

		RendererContextLadder_AddVersionRange( candidates, maxCandidates, count, firstStep, RENDERER_CONTEXT_PROFILE_CORE, debugCandidate );
		RendererContextLadder_AddVersionRange( candidates, maxCandidates, count, firstStep, RENDERER_CONTEXT_PROFILE_COMPATIBILITY, debugCandidate );
		RendererContextLadder_AddCompatibilityFallback( candidates, maxCandidates, count, debugCandidate );
	}

	return count;
}

rendererTierPreference_t RendererTierPreference_FromString( const char *value ) {
	if ( value == NULL || value[0] == '\0' || RendererStringEquals( value, "auto" ) || RendererStringEquals( value, "best" ) ) {
		return RENDERER_TIER_PREF_AUTO;
	}
	if ( RendererStringEquals( value, "legacy" ) || RendererStringEquals( value, "gl2" ) || RendererStringEquals( value, "compat" ) ) {
		return RENDERER_TIER_PREF_LEGACY;
	}
	if ( RendererStringEquals( value, "gl33" ) || RendererStringEquals( value, "3.3" ) ) {
		return RENDERER_TIER_PREF_GL33;
	}
	if ( RendererStringEquals( value, "gl41" ) || RendererStringEquals( value, "4.1" ) ) {
		return RENDERER_TIER_PREF_GL41;
	}
	if ( RendererStringEquals( value, "gl43" ) || RendererStringEquals( value, "4.3" ) ) {
		return RENDERER_TIER_PREF_GL43;
	}
	if ( RendererStringEquals( value, "gl45" ) || RendererStringEquals( value, "4.5" ) ) {
		return RENDERER_TIER_PREF_GL45;
	}
	if ( RendererStringEquals( value, "gl46" ) || RendererStringEquals( value, "4.6" ) ) {
		return RENDERER_TIER_PREF_GL46;
	}
	return RENDERER_TIER_PREF_AUTO;
}

rendererTier_t RendererTierPreference_ToForcedTier( rendererTierPreference_t preference ) {
	switch ( preference ) {
	case RENDERER_TIER_PREF_LEGACY:
		return RENDERER_TIER_LEGACY_GL2_COMPAT;
	case RENDERER_TIER_PREF_GL33:
		return RENDERER_TIER_MODERN_GL33;
	case RENDERER_TIER_PREF_GL41:
		return RENDERER_TIER_MODERN_GL41;
	case RENDERER_TIER_PREF_GL43:
		return RENDERER_TIER_GPU_DRIVEN_GL43;
	case RENDERER_TIER_PREF_GL45:
		return RENDERER_TIER_LOW_OVERHEAD_GL45;
	case RENDERER_TIER_PREF_GL46:
		return RENDERER_TIER_TOP_GL46;
	case RENDERER_TIER_PREF_AUTO:
	default:
		return RENDERER_TIER_NULL;
	}
}

bool RendererTier_IsModern( rendererTier_t tier ) {
	return tier >= RENDERER_TIER_MODERN_GL33;
}

static bool RendererCaps_HasVersion( const renderBackendCaps_t &caps, int major, int minor ) {
	if ( caps.glMajor > major ) {
		return true;
	}
	if ( caps.glMajor == major && caps.glMinor >= minor ) {
		return true;
	}
	return false;
}

bool RendererCaps_SupportsTier( const renderBackendCaps_t &caps, rendererTier_t tier ) {
	if ( !caps.contextCreated ) {
		return tier == RENDERER_TIER_NULL;
	}

	const bool baseline =
		caps.hasFBO &&
		caps.hasMRT &&
		caps.hasUBO &&
		caps.hasVAO &&
		caps.hasInstancing &&
		caps.hasTextureArrays;

	switch ( tier ) {
	case RENDERER_TIER_NULL:
		return false;
	case RENDERER_TIER_LEGACY_GL2_COMPAT:
		return caps.hasFixedFunctionCompatibility;
	case RENDERER_TIER_MODERN_GL33:
		return RendererCaps_HasVersion( caps, 3, 3 ) && baseline;
	case RENDERER_TIER_MODERN_GL41:
		return RendererCaps_HasVersion( caps, 4, 1 ) && baseline;
	case RENDERER_TIER_GPU_DRIVEN_GL43:
		return RendererCaps_HasVersion( caps, 4, 3 ) && baseline &&
			caps.hasCompute && caps.hasSSBO && caps.hasDrawIndirect &&
			caps.hasMultiDrawIndirect && caps.hasTextureViews;
	case RENDERER_TIER_LOW_OVERHEAD_GL45:
		return RendererCaps_HasVersion( caps, 4, 5 ) &&
			RendererCaps_SupportsTier( caps, RENDERER_TIER_GPU_DRIVEN_GL43 ) &&
			caps.hasBufferStorage && caps.hasDSA && caps.hasMultiBind;
	case RENDERER_TIER_TOP_GL46:
		return RendererCaps_HasVersion( caps, 4, 6 ) &&
			RendererCaps_SupportsTier( caps, RENDERER_TIER_LOW_OVERHEAD_GL45 ) &&
			caps.hasGLSpirv;
	default:
		return false;
	}
}

static rendererTier_t RendererTier_BestSupported( const renderBackendCaps_t &caps ) {
	if ( RendererCaps_SupportsTier( caps, RENDERER_TIER_TOP_GL46 ) ) {
		return RENDERER_TIER_TOP_GL46;
	}
	if ( RendererCaps_SupportsTier( caps, RENDERER_TIER_LOW_OVERHEAD_GL45 ) ) {
		return RENDERER_TIER_LOW_OVERHEAD_GL45;
	}
	if ( RendererCaps_SupportsTier( caps, RENDERER_TIER_GPU_DRIVEN_GL43 ) ) {
		return RENDERER_TIER_GPU_DRIVEN_GL43;
	}
	if ( RendererCaps_SupportsTier( caps, RENDERER_TIER_MODERN_GL41 ) ) {
		return RENDERER_TIER_MODERN_GL41;
	}
	if ( RendererCaps_SupportsTier( caps, RENDERER_TIER_MODERN_GL33 ) ) {
		return RENDERER_TIER_MODERN_GL33;
	}
	if ( RendererCaps_SupportsTier( caps, RENDERER_TIER_LEGACY_GL2_COMPAT ) ) {
		return RENDERER_TIER_LEGACY_GL2_COMPAT;
	}
	return RENDERER_TIER_NULL;
}

rendererTier_t RendererTier_Select( const renderBackendCaps_t &caps, rendererTierPreference_t preference ) {
	const rendererTier_t forcedTier = RendererTierPreference_ToForcedTier( preference );
	if ( forcedTier != RENDERER_TIER_NULL && RendererCaps_SupportsTier( caps, forcedTier ) ) {
		return forcedTier;
	}
	return RendererTier_BestSupported( caps );
}

renderFeatureSet_t RendererFeatureSet_Build( const renderBackendCaps_t &caps, rendererTier_t tier ) {
	renderFeatureSet_t features;
	memset( &features, 0, sizeof( features ) );

	features.modernBaseline = tier >= RENDERER_TIER_MODERN_GL33;
	features.modernGL41 = tier >= RENDERER_TIER_MODERN_GL41;
	features.gpuDriven = tier >= RENDERER_TIER_GPU_DRIVEN_GL43;
	features.lowOverhead = tier >= RENDERER_TIER_LOW_OVERHEAD_GL45;
	features.persistentMappedUploads = features.lowOverhead && caps.hasBufferStorage;
	features.directStateAccess = features.lowOverhead && caps.hasDSA;
	features.multiBind = features.lowOverhead && caps.hasMultiBind;
	features.bindlessTextures = features.lowOverhead && caps.hasBindlessTexture;
	features.shaderLibrary = features.modernBaseline;
	features.scenePackets = features.modernBaseline || tier == RENDERER_TIER_LEGACY_GL2_COMPAT;
	features.renderGraph = features.modernBaseline || tier == RENDERER_TIER_LEGACY_GL2_COMPAT;

	return features;
}

void RendererCaps_FormatSummary( const renderBackendCaps_t &caps, char *buffer, int bufferSize ) {
	if ( buffer == NULL || bufferSize <= 0 ) {
		return;
	}

	idStr::snPrintf(
		buffer,
		bufferSize,
		"GL %d.%d %s, UBO:%d VAO:%d MRT:%d FBO:%d instancing:%d compute:%d SSBO:%d indirect:%d MDI:%d buffer_storage:%d DSA:%d multi_bind:%d texture_view:%d gl_spirv:%d bindless:%d",
		caps.glMajor,
		caps.glMinor,
		RendererContextProfile_Name( caps.profile ),
		caps.hasUBO ? 1 : 0,
		caps.hasVAO ? 1 : 0,
		caps.hasMRT ? 1 : 0,
		caps.hasFBO ? 1 : 0,
		caps.hasInstancing ? 1 : 0,
		caps.hasCompute ? 1 : 0,
		caps.hasSSBO ? 1 : 0,
		caps.hasDrawIndirect ? 1 : 0,
		caps.hasMultiDrawIndirect ? 1 : 0,
		caps.hasBufferStorage ? 1 : 0,
		caps.hasDSA ? 1 : 0,
		caps.hasMultiBind ? 1 : 0,
		caps.hasTextureViews ? 1 : 0,
		caps.hasGLSpirv ? 1 : 0,
		caps.hasBindlessTexture ? 1 : 0 );
}

static void GLCapabilityProbe_AddExtension( const char *extensionName ) {
	if ( extensionName == NULL || extensionName[0] == '\0' ) {
		return;
	}

	for ( int i = 0; i < rg_extensionTokens.Num(); ++i ) {
		if ( rg_extensionTokens[i].Icmp( extensionName ) == 0 ) {
			return;
		}
	}

	rg_extensionTokens.Append( extensionName );
	if ( rg_extensionString.Length() > 0 ) {
		rg_extensionString += " ";
	}
	rg_extensionString += extensionName;
}

static void GLCapabilityProbe_AddLegacyExtensions( const char *legacyExtensionsString ) {
	if ( legacyExtensionsString == NULL || legacyExtensionsString[0] == '\0' ) {
		return;
	}

	idLexer lexer( legacyExtensionsString, strlen( legacyExtensionsString ), "OpenGL extensions" );
	lexer.SetFlags( LEXFL_NOERRORS | LEXFL_NOWARNINGS | LEXFL_ALLOWPATHNAMES );
	idToken token;
	while ( lexer.ReadToken( &token ) ) {
		GLCapabilityProbe_AddExtension( token.c_str() );
	}
}

bool GLCapabilityProbe_HasExtension( const char *name ) {
	if ( name == NULL || name[0] == '\0' ) {
		return false;
	}

	for ( int i = 0; i < rg_extensionTokens.Num(); ++i ) {
		if ( rg_extensionTokens[i].Icmp( name ) == 0 ) {
			return true;
		}
	}

	if ( idStr::Cmpn( name, "GL_", 3 ) != 0 ) {
		idStr prefixed = "GL_";
		prefixed += name;
		for ( int i = 0; i < rg_extensionTokens.Num(); ++i ) {
			if ( rg_extensionTokens[i].Icmp( prefixed.c_str() ) == 0 ) {
				return true;
			}
		}
	}

	return false;
}

const char *GLCapabilityProbe_ExtensionString( void ) {
	return rg_extensionString.c_str();
}

static void GLCapabilityProbe_QueryInt( GLenum name, int &value ) {
	GLint temp = 0;
	glGetIntegerv( name, &temp );
	if ( temp < 0 ) {
		temp = 0;
	}
	value = temp;
}

void GLCapabilityProbe_Build( renderBackendCaps_t &caps, const char *versionString, const char *legacyExtensionsString ) {
	memset( &caps, 0, sizeof( caps ) );
	rg_extensionTokens.Clear();
	rg_extensionString.Clear();

	caps.contextCreated = true;
	caps.profile = RENDERER_CONTEXT_PROFILE_UNKNOWN;
	caps.glVersion = versionString ? static_cast<float>( atof( versionString ) ) : 0.0f;
	caps.glMajor = static_cast<int>( caps.glVersion );
	caps.glMinor = static_cast<int>( ( caps.glVersion - static_cast<float>( caps.glMajor ) ) * 10.0f + 0.5f );

	if ( caps.glVersion >= 3.0f ) {
		GLint major = 0;
		GLint minor = 0;
		glGetIntegerv( GL_MAJOR_VERSION, &major );
		glGetIntegerv( GL_MINOR_VERSION, &minor );
		if ( major > 0 ) {
			caps.glMajor = major;
			caps.glMinor = minor;
			caps.glVersion = static_cast<float>( major ) + static_cast<float>( minor ) * 0.1f;
		}
	}

	if ( caps.glVersion >= 3.2f ) {
		GLint profileMask = 0;
		GLint contextFlags = 0;
		glGetIntegerv( GL_CONTEXT_PROFILE_MASK, &profileMask );
		glGetIntegerv( GL_CONTEXT_FLAGS, &contextFlags );
		if ( ( profileMask & GL_CONTEXT_CORE_PROFILE_BIT ) != 0 ) {
			caps.profile = RENDERER_CONTEXT_PROFILE_CORE;
		} else if ( ( profileMask & GL_CONTEXT_COMPATIBILITY_PROFILE_BIT ) != 0 ) {
			caps.profile = RENDERER_CONTEXT_PROFILE_COMPATIBILITY;
		}
		caps.debugContext = ( contextFlags & GL_CONTEXT_FLAG_DEBUG_BIT ) != 0;
		caps.forwardCompatibleContext = ( contextFlags & GL_CONTEXT_FLAG_FORWARD_COMPATIBLE_BIT ) != 0;
	}

	if ( caps.glVersion >= 3.0f && glGetStringi != NULL ) {
		GLint numExtensions = 0;
		glGetIntegerv( GL_NUM_EXTENSIONS, &numExtensions );
		for ( GLint i = 0; i < numExtensions; ++i ) {
			const char *extensionName = reinterpret_cast<const char *>( glGetStringi( GL_EXTENSIONS, i ) );
			GLCapabilityProbe_AddExtension( extensionName );
		}
	}

	if ( rg_extensionTokens.Num() == 0 ) {
		GLCapabilityProbe_AddLegacyExtensions( legacyExtensionsString );
	}

	caps.hasFixedFunctionCompatibility = caps.profile != RENDERER_CONTEXT_PROFILE_CORE;
	caps.hasARBVertexProgram = GLCapabilityProbe_HasExtension( "GL_ARB_vertex_program" );
	caps.hasARBFragmentProgram = GLCapabilityProbe_HasExtension( "GL_ARB_fragment_program" );
	caps.hasARBShaderObjects =
		GLCapabilityProbe_HasExtension( "GL_ARB_shader_objects" ) &&
		GLCapabilityProbe_HasExtension( "GL_ARB_vertex_shader" ) &&
		GLCapabilityProbe_HasExtension( "GL_ARB_fragment_shader" );
	caps.hasGLSL = caps.hasARBShaderObjects || caps.glVersion >= 2.0f;
	caps.hasVBO = caps.glVersion >= 1.5f || GLCapabilityProbe_HasExtension( "GL_ARB_vertex_buffer_object" );
	caps.hasPBO = caps.glVersion >= 2.1f || GLCapabilityProbe_HasExtension( "GL_ARB_pixel_buffer_object" ) || GLCapabilityProbe_HasExtension( "GL_EXT_pixel_buffer_object" );
	caps.hasFBO = caps.glVersion >= 3.0f || GLCapabilityProbe_HasExtension( "GL_ARB_framebuffer_object" ) || GLCapabilityProbe_HasExtension( "GL_EXT_framebuffer_object" );
	caps.hasSRGBTextures = caps.glVersion >= 2.1f || GLCapabilityProbe_HasExtension( "GL_EXT_texture_sRGB" );
	caps.hasFramebufferSRGB = caps.glVersion >= 3.0f || GLCapabilityProbe_HasExtension( "GL_ARB_framebuffer_sRGB" ) || GLCapabilityProbe_HasExtension( "GL_EXT_framebuffer_sRGB" );

	caps.hasUBO = caps.glVersion >= 3.1f || GLCapabilityProbe_HasExtension( "GL_ARB_uniform_buffer_object" );
	caps.hasVAO = caps.glVersion >= 3.0f || GLCapabilityProbe_HasExtension( "GL_ARB_vertex_array_object" );
	caps.hasInstancing = caps.glVersion >= 3.3f || GLCapabilityProbe_HasExtension( "GL_ARB_instanced_arrays" ) || GLCapabilityProbe_HasExtension( "GL_EXT_draw_instanced" );
	caps.hasTextureArrays = caps.glVersion >= 3.0f || GLCapabilityProbe_HasExtension( "GL_EXT_texture_array" );
	caps.hasTimerQuery = caps.glVersion >= 3.3f || GLCapabilityProbe_HasExtension( "GL_ARB_timer_query" ) || GLCapabilityProbe_HasExtension( "GL_EXT_timer_query" );
	caps.hasSync = caps.glVersion >= 3.2f || GLCapabilityProbe_HasExtension( "GL_ARB_sync" );
	caps.hasMapBufferRange = caps.glVersion >= 3.0f || GLCapabilityProbe_HasExtension( "GL_ARB_map_buffer_range" );
	caps.hasBufferStorage = caps.glVersion >= 4.4f || GLCapabilityProbe_HasExtension( "GL_ARB_buffer_storage" );
	caps.hasDSA = caps.glVersion >= 4.5f || GLCapabilityProbe_HasExtension( "GL_ARB_direct_state_access" );
	caps.hasMultiBind = caps.glVersion >= 4.4f || GLCapabilityProbe_HasExtension( "GL_ARB_multi_bind" );
	caps.hasCompute = caps.glVersion >= 4.3f || GLCapabilityProbe_HasExtension( "GL_ARB_compute_shader" );
	caps.hasSSBO = caps.glVersion >= 4.3f || GLCapabilityProbe_HasExtension( "GL_ARB_shader_storage_buffer_object" );
	caps.hasDrawIndirect = caps.glVersion >= 4.0f || GLCapabilityProbe_HasExtension( "GL_ARB_draw_indirect" );
	caps.hasMultiDrawIndirect = caps.glVersion >= 4.3f || GLCapabilityProbe_HasExtension( "GL_ARB_multi_draw_indirect" );
	caps.hasTextureViews = caps.glVersion >= 4.3f || GLCapabilityProbe_HasExtension( "GL_ARB_texture_view" );
	caps.hasGLSpirv = caps.glVersion >= 4.6f || GLCapabilityProbe_HasExtension( "GL_ARB_gl_spirv" );
	caps.hasBindlessTexture = GLCapabilityProbe_HasExtension( "GL_ARB_bindless_texture" ) || GLCapabilityProbe_HasExtension( "GL_NV_bindless_texture" );
	caps.hasDebugOutput = caps.glVersion >= 4.3f || GLCapabilityProbe_HasExtension( "GL_KHR_debug" ) || GLCapabilityProbe_HasExtension( "GL_ARB_debug_output" );

	GLCapabilityProbe_QueryInt( GL_MAX_TEXTURE_SIZE, caps.maxTextureSize );
	if ( caps.maxTextureSize <= 0 ) {
		caps.maxTextureSize = 256;
	}
	if ( caps.glVersion >= 1.3f || GLCapabilityProbe_HasExtension( "GL_ARB_multitexture" ) ) {
		GLCapabilityProbe_QueryInt( GL_MAX_TEXTURE_UNITS_ARB, caps.maxTextureUnits );
		GLCapabilityProbe_QueryInt( GL_MAX_TEXTURE_COORDS_ARB, caps.maxTextureCoords );
		GLCapabilityProbe_QueryInt( GL_MAX_TEXTURE_IMAGE_UNITS_ARB, caps.maxTextureImageUnits );
	}
	if ( caps.maxTextureUnits <= 0 ) {
		caps.maxTextureUnits = 1;
	}
	if ( caps.maxTextureCoords <= 0 ) {
		caps.maxTextureCoords = caps.maxTextureUnits;
	}
	if ( caps.maxTextureImageUnits <= 0 ) {
		caps.maxTextureImageUnits = caps.maxTextureUnits;
	}

	caps.maxDrawBuffers = 1;
	caps.maxColorAttachments = 1;
	if ( caps.glVersion >= 2.0f || GLCapabilityProbe_HasExtension( "GL_ARB_draw_buffers" ) ) {
		GLCapabilityProbe_QueryInt( GL_MAX_DRAW_BUFFERS_ARB, caps.maxDrawBuffers );
	}
	if ( caps.hasFBO ) {
		GLCapabilityProbe_QueryInt( GL_MAX_COLOR_ATTACHMENTS_EXT, caps.maxColorAttachments );
	}
	caps.hasMRT = caps.maxDrawBuffers >= 4 && caps.maxColorAttachments >= 4;
}

static renderBackendCaps_t RendererTierSelect_TestCaps(
	int major,
	int minor,
	rendererContextProfile_t profile,
	bool baseline,
	bool gpuDriven,
	bool lowOverhead,
	bool topEnd ) {
	renderBackendCaps_t caps;
	memset( &caps, 0, sizeof( caps ) );
	caps.contextCreated = true;
	caps.glMajor = major;
	caps.glMinor = minor;
	caps.glVersion = static_cast<float>( major ) + static_cast<float>( minor ) * 0.1f;
	caps.profile = profile;
	caps.hasFixedFunctionCompatibility = profile != RENDERER_CONTEXT_PROFILE_CORE;
	caps.hasFBO = baseline;
	caps.hasMRT = baseline;
	caps.hasUBO = baseline;
	caps.hasVAO = baseline;
	caps.hasInstancing = baseline;
	caps.hasTextureArrays = baseline;
	caps.hasCompute = gpuDriven;
	caps.hasSSBO = gpuDriven;
	caps.hasDrawIndirect = gpuDriven;
	caps.hasMultiDrawIndirect = gpuDriven;
	caps.hasTextureViews = gpuDriven;
	caps.hasBufferStorage = lowOverhead;
	caps.hasDSA = lowOverhead;
	caps.hasMultiBind = lowOverhead;
	caps.hasGLSpirv = topEnd;
	return caps;
}

bool RendererTierSelect_RunSelfTest( void ) {
	struct rendererTierTestCase_t {
		const char *name;
		renderBackendCaps_t caps;
		rendererTierPreference_t preference;
		rendererTier_t expectedTier;
	};

	const rendererTierTestCase_t tests[] = {
		{
			"no context selects null",
			renderBackendCaps_t(),
			RENDERER_TIER_PREF_AUTO,
			RENDERER_TIER_NULL
		},
		{
			"compatibility fallback survives without modern baseline",
			RendererTierSelect_TestCaps( 2, 1, RENDERER_CONTEXT_PROFILE_COMPATIBILITY, false, false, false, false ),
			RENDERER_TIER_PREF_AUTO,
			RENDERER_TIER_LEGACY_GL2_COMPAT
		},
		{
			"GL 3.3 baseline",
			RendererTierSelect_TestCaps( 3, 3, RENDERER_CONTEXT_PROFILE_CORE, true, false, false, false ),
			RENDERER_TIER_PREF_AUTO,
			RENDERER_TIER_MODERN_GL33
		},
		{
			"GL 4.3 gpu driven",
			RendererTierSelect_TestCaps( 4, 3, RENDERER_CONTEXT_PROFILE_CORE, true, true, false, false ),
			RENDERER_TIER_PREF_AUTO,
			RENDERER_TIER_GPU_DRIVEN_GL43
		},
		{
			"GL 4.5 low overhead",
			RendererTierSelect_TestCaps( 4, 5, RENDERER_CONTEXT_PROFILE_CORE, true, true, true, false ),
			RENDERER_TIER_PREF_AUTO,
			RENDERER_TIER_LOW_OVERHEAD_GL45
		},
		{
			"GL 4.6 top tier",
			RendererTierSelect_TestCaps( 4, 6, RENDERER_CONTEXT_PROFILE_CORE, true, true, true, true ),
			RENDERER_TIER_PREF_AUTO,
			RENDERER_TIER_TOP_GL46
		},
		{
			"forced lower tier stays lower",
			RendererTierSelect_TestCaps( 4, 6, RENDERER_CONTEXT_PROFILE_CORE, true, true, true, true ),
			RENDERER_TIER_PREF_GL33,
			RENDERER_TIER_MODERN_GL33
		},
		{
			"forced unsupported tier falls back",
			RendererTierSelect_TestCaps( 3, 3, RENDERER_CONTEXT_PROFILE_CORE, true, false, false, false ),
			RENDERER_TIER_PREF_GL43,
			RENDERER_TIER_MODERN_GL33
		}
	};

	for ( int i = 0; i < static_cast<int>( sizeof( tests ) / sizeof( tests[0] ) ); ++i ) {
		const rendererTier_t selected = RendererTier_Select( tests[i].caps, tests[i].preference );
		if ( selected != tests[i].expectedTier ) {
			common->Printf(
				"RendererTierSelect test failed: %s expected %s got %s\n",
				tests[i].name,
				RendererTier_Name( tests[i].expectedTier ),
				RendererTier_Name( selected ) );
			return false;
		}
	}

	common->Printf( "RendererTierSelect self-test passed (%d cases)\n", static_cast<int>( sizeof( tests ) / sizeof( tests[0] ) ) );
	return true;
}

bool RendererContextLadder_RunSelfTest( void ) {
	struct rendererContextLadderTestCase_t {
		const char *name;
		rendererTierPreference_t preference;
		bool debugContext;
		bool keepAutoCompatibility;
		int expectedCount;
		int expectedFirstMajor;
		int expectedFirstMinor;
		rendererContextProfile_t expectedFirstProfile;
		bool expectedFirstDebug;
		bool expectedLastExplicitVersion;
	};

	const rendererContextLadderTestCase_t tests[] = {
		{
			"auto compatibility bridge",
			RENDERER_TIER_PREF_AUTO,
			false,
			true,
			6,
			4,
			6,
			RENDERER_CONTEXT_PROFILE_COMPATIBILITY,
			false,
			false
		},
		{
			"auto modern core",
			RENDERER_TIER_PREF_AUTO,
			false,
			false,
			11,
			4,
			6,
			RENDERER_CONTEXT_PROFILE_CORE,
			false,
			false
		},
		{
			"legacy fallback with debug fallback",
			RENDERER_TIER_PREF_LEGACY,
			true,
			true,
			2,
			0,
			0,
			RENDERER_CONTEXT_PROFILE_COMPATIBILITY,
			true,
			false
		},
		{
			"forced gl43 starts at gl43",
			RENDERER_TIER_PREF_GL43,
			false,
			true,
			7,
			4,
			3,
			RENDERER_CONTEXT_PROFILE_CORE,
			false,
			false
		},
		{
			"forced gl33 starts at gl33",
			RENDERER_TIER_PREF_GL33,
			false,
			true,
			3,
			3,
			3,
			RENDERER_CONTEXT_PROFILE_CORE,
			false,
			false
		}
	};

	for ( int i = 0; i < static_cast<int>( sizeof( tests ) / sizeof( tests[0] ) ); ++i ) {
		rendererContextCandidate_t candidates[RENDERER_CONTEXT_LADDER_MAX_CANDIDATES];
		memset( candidates, 0, sizeof( candidates ) );
		const int count = RendererContextLadder_Build(
			candidates,
			static_cast<int>( sizeof( candidates ) / sizeof( candidates[0] ) ),
			tests[i].preference,
			tests[i].debugContext,
			tests[i].keepAutoCompatibility );
		if ( count != tests[i].expectedCount ) {
			common->Printf(
				"RendererContextLadder test failed: %s expected %d candidates got %d\n",
				tests[i].name,
				tests[i].expectedCount,
				count );
			return false;
		}
		if ( count <= 0 ) {
			common->Printf( "RendererContextLadder test failed: %s produced no candidates\n", tests[i].name );
			return false;
		}
		if ( candidates[0].major != tests[i].expectedFirstMajor ||
			candidates[0].minor != tests[i].expectedFirstMinor ||
			candidates[0].profile != tests[i].expectedFirstProfile ||
			candidates[0].debugContext != tests[i].expectedFirstDebug ) {
			common->Printf(
				"RendererContextLadder test failed: %s first candidate was %s %d.%d debug=%d\n",
				tests[i].name,
				RendererContextProfile_Name( candidates[0].profile ),
				candidates[0].major,
				candidates[0].minor,
				candidates[0].debugContext ? 1 : 0 );
			return false;
		}
		if ( candidates[count - 1].explicitVersion != tests[i].expectedLastExplicitVersion ) {
			common->Printf(
				"RendererContextLadder test failed: %s last explicitVersion expected %d got %d\n",
				tests[i].name,
				tests[i].expectedLastExplicitVersion ? 1 : 0,
				candidates[count - 1].explicitVersion ? 1 : 0 );
			return false;
		}
	}

	common->Printf( "RendererContextLadder self-test passed (%d cases)\n", static_cast<int>( sizeof( tests ) / sizeof( tests[0] ) ) );
	return true;
}
