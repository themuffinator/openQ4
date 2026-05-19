// Copyright (C) 2004 Id Software, Inc.
//

#include "tr_local.h"

static idStrList		rg_extensionTokens;
static idStr			rg_extensionString;
static rendererDriverQuirkReport_t rg_driverQuirkReport;

typedef struct rendererDriverQuirkRule_s {
	const char					*vendor;
	const char					*renderer;
	const char					*version;
	unsigned int				flags;
	const char					*summary;
} rendererDriverQuirkRule_t;

static const rendererDriverQuirkRule_t rg_driverQuirkRules[] = {
	{
		"Microsoft",
		"GDI Generic",
		"",
		RENDERER_DRIVER_QUIRK_FORCE_LEGACY,
		"Microsoft software OpenGL path is limited to the legacy compatibility renderer"
	},
	{
		"Microsoft",
		"OpenGL-D3D",
		"",
		RENDERER_DRIVER_QUIRK_FORCE_LEGACY,
		"Microsoft OpenGL-D3D translation path is limited to the legacy compatibility renderer"
	},
	{
		"OpenQ4Test",
		"Missing UBO",
		"",
		RENDERER_DRIVER_QUIRK_DISABLE_UBO,
		"synthetic missing-UBO downgrade"
	},
	{
		"OpenQ4Test",
		"Broken MRT",
		"",
		RENDERER_DRIVER_QUIRK_DISABLE_MRT,
		"synthetic MRT downgrade"
	},
	{
		"OpenQ4Test",
		"Missing Timer Query",
		"",
		RENDERER_DRIVER_QUIRK_DISABLE_TIMER_QUERY,
		"synthetic timer-query fallback"
	},
	{
		"OpenQ4Test",
		"Missing Buffer Storage",
		"",
		RENDERER_DRIVER_QUIRK_DISABLE_BUFFER_STORAGE,
		"synthetic GL45 buffer-storage downgrade"
	},
	{
		"OpenQ4Test",
		"Rejected Debug Context",
		"",
		RENDERER_DRIVER_QUIRK_REJECT_DEBUG_CONTEXT,
		"synthetic debug-context fallback"
	}
};

static bool RendererStringEquals( const char *a, const char *b ) {
	if ( a == NULL || b == NULL ) {
		return false;
	}
	return idStr::Icmp( a, b ) == 0;
}

static bool RendererDriverQuirk_MatchesText( const char *value, const char *pattern ) {
	if ( pattern == NULL || pattern[0] == '\0' ) {
		return true;
	}
	if ( value == NULL || value[0] == '\0' ) {
		return false;
	}
	return idStr::FindText( value, pattern, false ) >= 0;
}

static void RendererDriverQuirks_AppendSummary( const char *text ) {
	if ( text == NULL || text[0] == '\0' ) {
		return;
	}

	if ( rg_driverQuirkReport.summary[0] == '\0' || idStr::Icmp( rg_driverQuirkReport.summary, "none" ) == 0 ) {
		idStr::snPrintf( rg_driverQuirkReport.summary, sizeof( rg_driverQuirkReport.summary ), "%s", text );
		return;
	}

	const int used = idStr::Length( rg_driverQuirkReport.summary );
	if ( used >= static_cast<int>( sizeof( rg_driverQuirkReport.summary ) ) - 4 ) {
		return;
	}
	idStr::snPrintf(
		rg_driverQuirkReport.summary + used,
		sizeof( rg_driverQuirkReport.summary ) - used,
		"; %s",
		text );
}

static void RendererDriverQuirks_DisableModernBaseline( renderBackendCaps_t &caps ) {
	caps.hasUBO = false;
	caps.hasVAO = false;
	caps.hasInstancing = false;
	caps.hasTextureArrays = false;
	caps.hasMRT = false;
	caps.hasCompute = false;
	caps.hasSSBO = false;
	caps.hasDrawIndirect = false;
	caps.hasMultiDrawIndirect = false;
	caps.hasTextureViews = false;
	caps.hasBufferStorage = false;
	caps.hasDSA = false;
	caps.hasMultiBind = false;
	caps.hasGLSpirv = false;
	caps.hasBindlessTexture = false;
}

static void RendererDriverQuirks_FormatFlags( unsigned int flags, char *buffer, int bufferSize ) {
	if ( buffer == NULL || bufferSize <= 0 ) {
		return;
	}

	buffer[0] = '\0';
	struct flagName_t {
		unsigned int flag;
		const char *name;
	};
	const flagName_t names[] = {
		{ RENDERER_DRIVER_QUIRK_FORCE_LEGACY, "forceLegacy" },
		{ RENDERER_DRIVER_QUIRK_DISABLE_UBO, "disableUBO" },
		{ RENDERER_DRIVER_QUIRK_DISABLE_MRT, "disableMRT" },
		{ RENDERER_DRIVER_QUIRK_DISABLE_TIMER_QUERY, "disableTimerQuery" },
		{ RENDERER_DRIVER_QUIRK_DISABLE_BUFFER_STORAGE, "disableBufferStorage" },
		{ RENDERER_DRIVER_QUIRK_REJECT_DEBUG_CONTEXT, "rejectDebugContext" }
	};

	if ( flags == RENDERER_DRIVER_QUIRK_NONE ) {
		idStr::snPrintf( buffer, bufferSize, "none" );
		return;
	}

	for ( int i = 0; i < static_cast<int>( sizeof( names ) / sizeof( names[0] ) ); ++i ) {
		if ( ( flags & names[i].flag ) == 0 ) {
			continue;
		}
		const int used = idStr::Length( buffer );
		idStr::snPrintf( buffer + used, bufferSize - used, "%s%s", used > 0 ? "," : "", names[i].name );
	}
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

const char *RendererTierPreference_CVarName( rendererTierPreference_t preference ) {
	switch ( preference ) {
	case RENDERER_TIER_PREF_LEGACY:
		return "legacy";
	case RENDERER_TIER_PREF_GL33:
		return "gl33";
	case RENDERER_TIER_PREF_GL41:
		return "gl41";
	case RENDERER_TIER_PREF_GL43:
		return "gl43";
	case RENDERER_TIER_PREF_GL45:
		return "gl45";
	case RENDERER_TIER_PREF_GL46:
		return "gl46";
	case RENDERER_TIER_PREF_AUTO:
	default:
		return "auto";
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
	const bool forcedModernTier = RendererTier_IsModern( forcedTier );
	const bool compatibilityOnly = preference == RENDERER_TIER_PREF_LEGACY || ( preference == RENDERER_TIER_PREF_AUTO && keepAutoCompatibility );
	const int firstStep = forcedModernTier ? RendererContextLadder_FirstStepForPreference( preference ) : 0;

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

		if ( forcedModernTier ) {
			// The visible renderer still rolls back through the ARB2 compatibility bridge.
			// Prefer versioned compatibility contexts for forced tiers so gameplay does not
			// land on a core profile before the modern visible path is complete.
			RendererContextLadder_AddVersionRange( candidates, maxCandidates, count, firstStep, RENDERER_CONTEXT_PROFILE_COMPATIBILITY, debugCandidate );
			RendererContextLadder_AddCompatibilityFallback( candidates, maxCandidates, count, debugCandidate );
			RendererContextLadder_AddVersionRange( candidates, maxCandidates, count, firstStep, RENDERER_CONTEXT_PROFILE_CORE, debugCandidate );
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
		caps.hasVBO &&
		caps.hasInstancing &&
		caps.hasTextureArrays &&
		caps.hasMapBufferRange;

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
			caps.hasBufferStorage && caps.hasDSA && caps.hasMultiBind && caps.hasSync;
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

static const char *RendererTierContract_Name( rendererTier_t tier ) {
	switch ( tier ) {
	case RENDERER_TIER_LEGACY_GL2_COMPAT:
		return "legacy-arb2-compat";
	case RENDERER_TIER_MODERN_GL33:
		return "modern-gl33-cpu-baseline";
	case RENDERER_TIER_MODERN_GL41:
		return "modern-gl41-mrt-post";
	case RENDERER_TIER_GPU_DRIVEN_GL43:
		return "gpu-driven-gl43";
	case RENDERER_TIER_LOW_OVERHEAD_GL45:
		return "low-overhead-gl45";
	case RENDERER_TIER_TOP_GL46:
		return "top-gl46-experimental";
	case RENDERER_TIER_NULL:
	default:
		return "null";
	}
}

static void RendererTierContract_AppendMissing( idStr &missing, const char *text ) {
	if ( text == NULL || text[0] == '\0' ) {
		return;
	}
	if ( missing.Length() > 0 ) {
		missing += ",";
	}
	missing += text;
}

void RendererTierContract_Evaluate(
	const renderBackendCaps_t &caps,
	const renderFeatureSet_t &features,
	rendererTierPreference_t requestedPreference,
	rendererTier_t selectedTier,
	bool legacyBridgeActive,
	rendererTierContractReport_t &report ) {
	memset( &report, 0, sizeof( report ) );
	report.requestedPreference = requestedPreference;
	report.requestedTier = RendererTierPreference_ToForcedTier( requestedPreference );
	report.selectedTier = selectedTier;
	idStr::Copynz( report.contractName, RendererTierContract_Name( selectedTier ), sizeof( report.contractName ) );

	report.legacyBridgeReady =
		legacyBridgeActive &&
		features.legacyARB2Bridge &&
		caps.hasFixedFunctionCompatibility;
	report.rollbackReady = legacyBridgeActive && features.legacyARB2Bridge;
	report.baselineReady =
		features.modernBaseline &&
		features.shaderLibrary &&
		features.scenePackets &&
		features.renderGraph &&
		RendererCaps_SupportsTier( caps, RENDERER_TIER_MODERN_GL33 ) &&
		caps.hasVBO &&
		caps.hasMapBufferRange;
	report.gl41Ready =
		selectedTier >= RENDERER_TIER_MODERN_GL41 &&
		features.modernGL41 &&
		report.baselineReady &&
		RendererCaps_SupportsTier( caps, RENDERER_TIER_MODERN_GL41 );
	report.gpuDrivenReady =
		selectedTier >= RENDERER_TIER_GPU_DRIVEN_GL43 &&
		features.gpuDriven &&
		report.baselineReady &&
		caps.hasCompute &&
		caps.hasSSBO &&
		caps.hasDrawIndirect &&
		caps.hasMultiDrawIndirect &&
		caps.hasTextureViews &&
		RendererCaps_SupportsTier( caps, RENDERER_TIER_GPU_DRIVEN_GL43 );
	report.lowOverheadReady =
		selectedTier >= RENDERER_TIER_LOW_OVERHEAD_GL45 &&
		features.lowOverhead &&
		features.persistentMappedUploads &&
		features.directStateAccess &&
		features.multiBind &&
		report.gpuDrivenReady &&
		caps.hasBufferStorage &&
		caps.hasDSA &&
		caps.hasMultiBind &&
		caps.hasSync &&
		RendererCaps_SupportsTier( caps, RENDERER_TIER_LOW_OVERHEAD_GL45 );
	report.topReady =
		selectedTier >= RENDERER_TIER_TOP_GL46 &&
		report.lowOverheadReady &&
		caps.hasGLSpirv &&
		RendererCaps_SupportsTier( caps, RENDERER_TIER_TOP_GL46 );
	report.cpuWorkloadReady =
		selectedTier == RENDERER_TIER_LEGACY_GL2_COMPAT
			? report.legacyBridgeReady
			: report.baselineReady;
	report.gpuWorkloadReady =
		selectedTier == RENDERER_TIER_LEGACY_GL2_COMPAT
			? report.legacyBridgeReady
			: ( selectedTier >= RENDERER_TIER_GPU_DRIVEN_GL43
				? report.gpuDrivenReady
				: report.baselineReady );
	report.noComputeRequired = selectedTier <= RENDERER_TIER_MODERN_GL41;

	switch ( selectedTier ) {
	case RENDERER_TIER_NULL:
		report.selectedReady = false;
		break;
	case RENDERER_TIER_LEGACY_GL2_COMPAT:
		report.selectedReady = RendererCaps_SupportsTier( caps, selectedTier ) && report.legacyBridgeReady;
		break;
	case RENDERER_TIER_MODERN_GL33:
		report.selectedReady = report.baselineReady;
		break;
	case RENDERER_TIER_MODERN_GL41:
		report.selectedReady = report.gl41Ready && !features.gpuDriven;
		break;
	case RENDERER_TIER_GPU_DRIVEN_GL43:
		report.selectedReady = report.gpuDrivenReady;
		break;
	case RENDERER_TIER_LOW_OVERHEAD_GL45:
		report.selectedReady = report.lowOverheadReady;
		break;
	case RENDERER_TIER_TOP_GL46:
		report.selectedReady = report.topReady;
		break;
	default:
		report.selectedReady = false;
		break;
	}

	report.degraded = report.requestedTier != RENDERER_TIER_NULL && report.requestedTier != selectedTier;
	report.requestedReady = !report.degraded && report.selectedReady;
	report.failClosed = report.degraded && report.selectedReady;

	idStr missing;
	if ( !caps.contextCreated ) {
		RendererTierContract_AppendMissing( missing, "context" );
	}
	if ( report.degraded ) {
		RendererTierContract_AppendMissing( missing, "requested-tier" );
	}
	if ( selectedTier == RENDERER_TIER_LEGACY_GL2_COMPAT && !report.legacyBridgeReady ) {
		RendererTierContract_AppendMissing( missing, "arb2-bridge" );
	}
	if ( RendererTier_IsModern( selectedTier ) && !report.baselineReady ) {
		if ( !features.shaderLibrary ) {
			RendererTierContract_AppendMissing( missing, "shader-library" );
		}
		if ( !features.scenePackets ) {
			RendererTierContract_AppendMissing( missing, "scene-packets" );
		}
		if ( !features.renderGraph ) {
			RendererTierContract_AppendMissing( missing, "render-graph" );
		}
		if ( !caps.hasVBO ) {
			RendererTierContract_AppendMissing( missing, "vbo" );
		}
		if ( !caps.hasVAO ) {
			RendererTierContract_AppendMissing( missing, "vao" );
		}
		if ( !caps.hasUBO ) {
			RendererTierContract_AppendMissing( missing, "ubo" );
		}
		if ( !caps.hasMRT ) {
			RendererTierContract_AppendMissing( missing, "mrt" );
		}
		if ( !caps.hasMapBufferRange ) {
			RendererTierContract_AppendMissing( missing, "map-range" );
		}
	}
	if ( selectedTier >= RENDERER_TIER_GPU_DRIVEN_GL43 && !report.gpuDrivenReady ) {
		if ( !caps.hasCompute ) {
			RendererTierContract_AppendMissing( missing, "compute" );
		}
		if ( !caps.hasSSBO ) {
			RendererTierContract_AppendMissing( missing, "ssbo" );
		}
		if ( !caps.hasDrawIndirect || !caps.hasMultiDrawIndirect ) {
			RendererTierContract_AppendMissing( missing, "indirect" );
		}
		if ( !caps.hasTextureViews ) {
			RendererTierContract_AppendMissing( missing, "texture-view" );
		}
	}
	if ( selectedTier >= RENDERER_TIER_LOW_OVERHEAD_GL45 && !report.lowOverheadReady ) {
		if ( !caps.hasBufferStorage ) {
			RendererTierContract_AppendMissing( missing, "buffer-storage" );
		}
		if ( !caps.hasDSA ) {
			RendererTierContract_AppendMissing( missing, "dsa" );
		}
		if ( !caps.hasMultiBind ) {
			RendererTierContract_AppendMissing( missing, "multi-bind" );
		}
		if ( !caps.hasSync ) {
			RendererTierContract_AppendMissing( missing, "sync" );
		}
	}
	if ( selectedTier >= RENDERER_TIER_TOP_GL46 && !report.topReady && !caps.hasGLSpirv ) {
		RendererTierContract_AppendMissing( missing, "gl-spirv" );
	}
	if ( RendererTier_IsModern( selectedTier ) && !report.rollbackReady ) {
		RendererTierContract_AppendMissing( missing, "rollback" );
	}
	idStr::Copynz( report.missing, missing.Length() > 0 ? missing.c_str() : "none", sizeof( report.missing ) );
}

void RendererTierContract_PrintGfxInfo( void ) {
	rendererTierContractReport_t report;
	RendererTierContract_Evaluate(
		glConfig.backendCaps,
		glConfig.renderFeatures,
		RendererTierPreference_FromString( r_glTier.GetString() ),
		glConfig.rendererTier,
		glConfig.allowARB2Path,
		report );
	common->Printf(
		"Renderer tier contract: requested=%s selected=%s contract=%s selectedReady=%d requestedReady=%d degraded=%d failClosed=%d rollback=%d legacy=%d baseline=%d gl41=%d gpuDriven=%d lowOverhead=%d top=%d cpuWorkload=%d gpuWorkload=%d noCompute=%d missing=%s\n",
		RendererTierPreference_CVarName( report.requestedPreference ),
		RendererTier_Name( report.selectedTier ),
		report.contractName,
		report.selectedReady ? 1 : 0,
		report.requestedReady ? 1 : 0,
		report.degraded ? 1 : 0,
		report.failClosed ? 1 : 0,
		report.rollbackReady ? 1 : 0,
		report.legacyBridgeReady ? 1 : 0,
		report.baselineReady ? 1 : 0,
		report.gl41Ready ? 1 : 0,
		report.gpuDrivenReady ? 1 : 0,
		report.lowOverheadReady ? 1 : 0,
		report.topReady ? 1 : 0,
		report.cpuWorkloadReady ? 1 : 0,
		report.gpuWorkloadReady ? 1 : 0,
		report.noComputeRequired ? 1 : 0,
		report.missing );
}

void RendererDriverQuirks_Apply( renderBackendCaps_t &caps, const rendererDriverInfo_t &driverInfo ) {
	memset( &rg_driverQuirkReport, 0, sizeof( rg_driverQuirkReport ) );
	idStr::snPrintf( rg_driverQuirkReport.summary, sizeof( rg_driverQuirkReport.summary ), "none" );

	const renderBackendCaps_t originalCaps = caps;
	for ( int i = 0; i < static_cast<int>( sizeof( rg_driverQuirkRules ) / sizeof( rg_driverQuirkRules[0] ) ); ++i ) {
		const rendererDriverQuirkRule_t &rule = rg_driverQuirkRules[i];
		if ( !RendererDriverQuirk_MatchesText( driverInfo.vendor, rule.vendor ) ||
			!RendererDriverQuirk_MatchesText( driverInfo.renderer, rule.renderer ) ||
			!RendererDriverQuirk_MatchesText( driverInfo.version, rule.version ) ) {
			continue;
		}

		rg_driverQuirkReport.flags |= rule.flags;
		rg_driverQuirkReport.rulesMatched++;
		RendererDriverQuirks_AppendSummary( rule.summary );
	}

	if ( ( rg_driverQuirkReport.flags & RENDERER_DRIVER_QUIRK_FORCE_LEGACY ) != 0 ) {
		RendererDriverQuirks_DisableModernBaseline( caps );
	}
	if ( ( rg_driverQuirkReport.flags & RENDERER_DRIVER_QUIRK_DISABLE_UBO ) != 0 ) {
		caps.hasUBO = false;
	}
	if ( ( rg_driverQuirkReport.flags & RENDERER_DRIVER_QUIRK_DISABLE_MRT ) != 0 ) {
		caps.hasMRT = false;
		if ( caps.maxDrawBuffers > 1 ) {
			caps.maxDrawBuffers = 1;
		}
		if ( caps.maxColorAttachments > 1 ) {
			caps.maxColorAttachments = 1;
		}
	}
	if ( ( rg_driverQuirkReport.flags & RENDERER_DRIVER_QUIRK_DISABLE_TIMER_QUERY ) != 0 ) {
		caps.hasTimerQuery = false;
	}
	if ( ( rg_driverQuirkReport.flags & RENDERER_DRIVER_QUIRK_DISABLE_BUFFER_STORAGE ) != 0 ) {
		caps.hasBufferStorage = false;
	}
	if ( ( rg_driverQuirkReport.flags & RENDERER_DRIVER_QUIRK_REJECT_DEBUG_CONTEXT ) != 0 ) {
		caps.debugContext = false;
	}

	rg_driverQuirkReport.changedCaps = memcmp( &originalCaps, &caps, sizeof( caps ) ) != 0;
	if ( rg_driverQuirkReport.rulesMatched > 0 ) {
		char flags[128];
		RendererDriverQuirks_FormatFlags( rg_driverQuirkReport.flags, flags, sizeof( flags ) );
		common->Printf(
			"Renderer driver quirks: applied rules=%d flags=%s summary='%s'\n",
			rg_driverQuirkReport.rulesMatched,
			flags,
			rg_driverQuirkReport.summary );
	}
}

const rendererDriverQuirkReport_t &RendererDriverQuirks_LastReport( void ) {
	return rg_driverQuirkReport;
}

void RendererCompatibilityGates_PrintGfxInfo( void ) {
	char flags[128];
	RendererDriverQuirks_FormatFlags( rg_driverQuirkReport.flags, flags, sizeof( flags ) );

	const bool forcedTierSupported =
		RendererTierPreference_ToForcedTier( RendererTierPreference_FromString( r_glTier.GetString() ) ) == RENDERER_TIER_NULL ||
		RendererTierPreference_ToForcedTier( RendererTierPreference_FromString( r_glTier.GetString() ) ) == glConfig.rendererTier;
	const bool debugFallback = glConfig.contextRequest.debugContext && !glConfig.backendCaps.debugContext;
	const bool lowOverheadReady = glConfig.renderFeatures.lowOverhead && glConfig.backendCaps.hasBufferStorage;

	common->Printf(
		"Renderer driver quirks: rules=%d flags=%s changedCaps=%d summary='%s'\n",
		rg_driverQuirkReport.rulesMatched,
		flags,
		rg_driverQuirkReport.changedCaps ? 1 : 0,
		rg_driverQuirkReport.summary );
	common->Printf(
		"Renderer compatibility gates: selected=%s baseline=%d UBO=%d MRT=%d timerQuery=%d bufferStorage=%d lowOverhead=%d debugFallback=%d forcedTierSupported=%d\n",
		RendererTier_Name( glConfig.rendererTier ),
		glConfig.renderFeatures.modernBaseline ? 1 : 0,
		glConfig.backendCaps.hasUBO ? 1 : 0,
		glConfig.backendCaps.hasMRT ? 1 : 0,
		glConfig.backendCaps.hasTimerQuery ? 1 : 0,
		glConfig.backendCaps.hasBufferStorage ? 1 : 0,
		lowOverheadReady ? 1 : 0,
		debugFallback ? 1 : 0,
		forcedTierSupported ? 1 : 0 );
}

void RendererCaps_FormatSummary( const renderBackendCaps_t &caps, char *buffer, int bufferSize ) {
	if ( buffer == NULL || bufferSize <= 0 ) {
		return;
	}

	idStr::snPrintf(
		buffer,
		bufferSize,
		"GL %d.%d %s, VBO:%d UBO:%d VAO:%d MRT:%d FBO:%d instancing:%d map_range:%d sync:%d compute:%d SSBO:%d indirect:%d MDI:%d buffer_storage:%d DSA:%d multi_bind:%d texture_view:%d gl_spirv:%d bindless:%d",
		caps.glMajor,
		caps.glMinor,
		RendererContextProfile_Name( caps.profile ),
		caps.hasVBO ? 1 : 0,
		caps.hasUBO ? 1 : 0,
		caps.hasVAO ? 1 : 0,
		caps.hasMRT ? 1 : 0,
		caps.hasFBO ? 1 : 0,
		caps.hasInstancing ? 1 : 0,
		caps.hasMapBufferRange ? 1 : 0,
		caps.hasSync ? 1 : 0,
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
	caps.hasVBO = baseline;
	caps.hasFBO = baseline;
	caps.hasMRT = baseline;
	caps.hasUBO = baseline;
	caps.hasVAO = baseline;
	caps.hasInstancing = baseline;
	caps.hasTextureArrays = baseline;
	caps.hasMapBufferRange = baseline;
	caps.hasSync = lowOverhead || gpuDriven || baseline;
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

bool RendererTierContract_RunSelfTest( void ) {
	struct rendererTierContractTestCase_t {
		const char *name;
		renderBackendCaps_t caps;
		rendererTierPreference_t preference;
		bool legacyBridge;
		rendererTier_t expectedSelected;
		bool expectedSelectedReady;
		bool expectedRequestedReady;
		bool expectedFailClosed;
		bool expectedCpuWorkload;
		bool expectedGpuDriven;
		bool expectedLowOverhead;
		bool expectedTop;
	};

	renderBackendCaps_t legacyCaps = RendererTierSelect_TestCaps( 2, 1, RENDERER_CONTEXT_PROFILE_COMPATIBILITY, false, false, false, false );
	renderBackendCaps_t gl33Caps = RendererTierSelect_TestCaps( 3, 3, RENDERER_CONTEXT_PROFILE_CORE, true, false, false, false );
	renderBackendCaps_t gl41Caps = RendererTierSelect_TestCaps( 4, 1, RENDERER_CONTEXT_PROFILE_CORE, true, false, false, false );
	renderBackendCaps_t gl43Caps = RendererTierSelect_TestCaps( 4, 3, RENDERER_CONTEXT_PROFILE_CORE, true, true, false, false );
	renderBackendCaps_t gl45Caps = RendererTierSelect_TestCaps( 4, 5, RENDERER_CONTEXT_PROFILE_CORE, true, true, true, false );
	renderBackendCaps_t gl46Caps = RendererTierSelect_TestCaps( 4, 6, RENDERER_CONTEXT_PROFILE_CORE, true, true, true, true );

	const rendererTierContractTestCase_t tests[] = {
		{
			"legacy bridge contract",
			legacyCaps,
			RENDERER_TIER_PREF_LEGACY,
			true,
			RENDERER_TIER_LEGACY_GL2_COMPAT,
			true,
			true,
			false,
			true,
			false,
			false,
			false
		},
		{
			"legacy bridge missing fails selected contract",
			legacyCaps,
			RENDERER_TIER_PREF_LEGACY,
			false,
			RENDERER_TIER_LEGACY_GL2_COMPAT,
			false,
			false,
			false,
			false,
			false,
			false,
			false
		},
		{
			"GL33 CPU workload baseline",
			gl33Caps,
			RENDERER_TIER_PREF_GL33,
			true,
			RENDERER_TIER_MODERN_GL33,
			true,
			true,
			false,
			true,
			false,
			false,
			false
		},
		{
			"GL41 remains compute-free",
			gl41Caps,
			RENDERER_TIER_PREF_GL41,
			true,
			RENDERER_TIER_MODERN_GL41,
			true,
			true,
			false,
			true,
			false,
			false,
			false
		},
		{
			"GL43 GPU-driven workload",
			gl43Caps,
			RENDERER_TIER_PREF_GL43,
			true,
			RENDERER_TIER_GPU_DRIVEN_GL43,
			true,
			true,
			false,
			true,
			true,
			false,
			false
		},
		{
			"GL45 low-overhead workload",
			gl45Caps,
			RENDERER_TIER_PREF_GL45,
			true,
			RENDERER_TIER_LOW_OVERHEAD_GL45,
			true,
			true,
			false,
			true,
			true,
			true,
			false
		},
		{
			"GL46 top workload",
			gl46Caps,
			RENDERER_TIER_PREF_GL46,
			true,
			RENDERER_TIER_TOP_GL46,
			true,
			true,
			false,
			true,
			true,
			true,
			true
		},
		{
			"forced GL43 on GL33 fails closed",
			gl33Caps,
			RENDERER_TIER_PREF_GL43,
			true,
			RENDERER_TIER_MODERN_GL33,
			true,
			false,
			true,
			true,
			false,
			false,
			false
		}
	};

	for ( int i = 0; i < static_cast<int>( sizeof( tests ) / sizeof( tests[0] ) ); ++i ) {
		const rendererTier_t selected = RendererTier_Select( tests[i].caps, tests[i].preference );
		renderFeatureSet_t features = RendererFeatureSet_Build( tests[i].caps, selected );
		features.legacyARB2Bridge = tests[i].legacyBridge;
		rendererTierContractReport_t report;
		RendererTierContract_Evaluate( tests[i].caps, features, tests[i].preference, selected, tests[i].legacyBridge, report );
		if ( selected != tests[i].expectedSelected ||
			report.selectedReady != tests[i].expectedSelectedReady ||
			report.requestedReady != tests[i].expectedRequestedReady ||
			report.failClosed != tests[i].expectedFailClosed ||
			report.cpuWorkloadReady != tests[i].expectedCpuWorkload ||
			report.gpuDrivenReady != tests[i].expectedGpuDriven ||
			report.lowOverheadReady != tests[i].expectedLowOverhead ||
			report.topReady != tests[i].expectedTop ) {
			common->Printf(
				"RendererTierContract self-test failed: %s selected=%s ready=%d request=%d failClosed=%d cpu=%d gpu=%d low=%d top=%d missing=%s\n",
				tests[i].name,
				RendererTier_Name( selected ),
				report.selectedReady ? 1 : 0,
				report.requestedReady ? 1 : 0,
				report.failClosed ? 1 : 0,
				report.cpuWorkloadReady ? 1 : 0,
				report.gpuDrivenReady ? 1 : 0,
				report.lowOverheadReady ? 1 : 0,
				report.topReady ? 1 : 0,
				report.missing );
			return false;
		}
	}

	common->Printf( "RendererTierContract self-test passed (%d cases)\n", static_cast<int>( sizeof( tests ) / sizeof( tests[0] ) ) );
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
			"forced gl43 starts at gl43 compatibility bridge",
			RENDERER_TIER_PREF_GL43,
			false,
			true,
			7,
			4,
			3,
			RENDERER_CONTEXT_PROFILE_COMPATIBILITY,
			false,
			true
		},
		{
			"forced gl33 starts at gl33 compatibility bridge",
			RENDERER_TIER_PREF_GL33,
			false,
			true,
			3,
			3,
			3,
			RENDERER_CONTEXT_PROFILE_COMPATIBILITY,
			false,
			true
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

bool RendererCompatibilityGates_RunSelfTest( void ) {
	const rendererDriverQuirkReport_t restoreReport = rg_driverQuirkReport;
	bool ok = true;
	int fallbackCases = 0;
	int quirkCases = 0;

	renderBackendCaps_t caps = RendererTierSelect_TestCaps( 4, 6, RENDERER_CONTEXT_PROFILE_COMPATIBILITY, true, true, true, true );
	caps.hasUBO = false;
	if ( RendererTier_Select( caps, RENDERER_TIER_PREF_AUTO ) != RENDERER_TIER_LEGACY_GL2_COMPAT ) {
		common->Printf( "RendererCompatibilityGates self-test failed: missing UBO did not downgrade to legacy\n" );
		ok = false;
	}
	fallbackCases++;

	caps = RendererTierSelect_TestCaps( 4, 6, RENDERER_CONTEXT_PROFILE_COMPATIBILITY, true, true, true, true );
	caps.hasMRT = false;
	caps.maxDrawBuffers = 1;
	caps.maxColorAttachments = 1;
	if ( RendererTier_Select( caps, RENDERER_TIER_PREF_AUTO ) != RENDERER_TIER_LEGACY_GL2_COMPAT ) {
		common->Printf( "RendererCompatibilityGates self-test failed: broken MRT did not downgrade to legacy\n" );
		ok = false;
	}
	fallbackCases++;

	caps = RendererTierSelect_TestCaps( 4, 6, RENDERER_CONTEXT_PROFILE_CORE, true, true, true, true );
	caps.hasTimerQuery = false;
	if ( RendererTier_Select( caps, RENDERER_TIER_PREF_AUTO ) != RENDERER_TIER_TOP_GL46 || caps.hasTimerQuery ) {
		common->Printf( "RendererCompatibilityGates self-test failed: missing timer query should disable timers without tier downgrade\n" );
		ok = false;
	}
	fallbackCases++;

	caps = RendererTierSelect_TestCaps( 4, 6, RENDERER_CONTEXT_PROFILE_CORE, true, true, true, true );
	caps.hasBufferStorage = false;
	if ( RendererTier_Select( caps, RENDERER_TIER_PREF_AUTO ) != RENDERER_TIER_GPU_DRIVEN_GL43 ) {
		common->Printf( "RendererCompatibilityGates self-test failed: missing buffer storage did not downgrade GL45/46 to GL43\n" );
		ok = false;
	}
	fallbackCases++;

	rendererContextCandidate_t candidates[RENDERER_CONTEXT_LADDER_MAX_CANDIDATES];
	memset( candidates, 0, sizeof( candidates ) );
	const int candidateCount = RendererContextLadder_Build(
		candidates,
		static_cast<int>( sizeof( candidates ) / sizeof( candidates[0] ) ),
		RENDERER_TIER_PREF_GL33,
		true,
		false );
	bool hasDebugCandidate = false;
	bool hasNonDebugFallback = false;
	for ( int i = 0; i < candidateCount; ++i ) {
		hasDebugCandidate |= candidates[i].debugContext;
		hasNonDebugFallback |= !candidates[i].debugContext;
	}
	caps = RendererTierSelect_TestCaps( 3, 3, RENDERER_CONTEXT_PROFILE_CORE, true, false, false, false );
	caps.debugContext = false;
	rendererContextRequest_t requestedDebug;
	memset( &requestedDebug, 0, sizeof( requestedDebug ) );
	requestedDebug.debugContext = true;
	if ( !hasDebugCandidate || !hasNonDebugFallback || !( requestedDebug.debugContext && !caps.debugContext ) ) {
		common->Printf( "RendererCompatibilityGates self-test failed: rejected debug-context fallback was not represented in the ladder\n" );
		ok = false;
	}
	fallbackCases++;

	struct quirkCase_t {
		const char *renderer;
		unsigned int expectedFlag;
		rendererTier_t expectedTier;
		bool expectedTimerQuery;
		bool expectedDebugContext;
	};
	const quirkCase_t quirkCasesTable[] = {
		{ "Missing UBO", RENDERER_DRIVER_QUIRK_DISABLE_UBO, RENDERER_TIER_LEGACY_GL2_COMPAT, true, true },
		{ "Broken MRT", RENDERER_DRIVER_QUIRK_DISABLE_MRT, RENDERER_TIER_LEGACY_GL2_COMPAT, true, true },
		{ "Missing Timer Query", RENDERER_DRIVER_QUIRK_DISABLE_TIMER_QUERY, RENDERER_TIER_TOP_GL46, false, true },
		{ "Missing Buffer Storage", RENDERER_DRIVER_QUIRK_DISABLE_BUFFER_STORAGE, RENDERER_TIER_GPU_DRIVEN_GL43, true, true },
		{ "Rejected Debug Context", RENDERER_DRIVER_QUIRK_REJECT_DEBUG_CONTEXT, RENDERER_TIER_TOP_GL46, true, false }
	};

	for ( int i = 0; i < static_cast<int>( sizeof( quirkCasesTable ) / sizeof( quirkCasesTable[0] ) ); ++i ) {
		caps = RendererTierSelect_TestCaps( 4, 6, RENDERER_CONTEXT_PROFILE_COMPATIBILITY, true, true, true, true );
		caps.debugContext = true;
		caps.hasTimerQuery = true;
		const rendererDriverInfo_t driverInfo = {
			"OpenQ4Test",
			quirkCasesTable[i].renderer,
			"1.0"
		};
		RendererDriverQuirks_Apply( caps, driverInfo );
		const rendererTier_t selected = RendererTier_Select( caps, RENDERER_TIER_PREF_AUTO );
		const rendererDriverQuirkReport_t &report = RendererDriverQuirks_LastReport();
		if ( ( report.flags & quirkCasesTable[i].expectedFlag ) == 0 ||
			selected != quirkCasesTable[i].expectedTier ||
			caps.hasTimerQuery != quirkCasesTable[i].expectedTimerQuery ||
			caps.debugContext != quirkCasesTable[i].expectedDebugContext ) {
			common->Printf(
				"RendererCompatibilityGates self-test failed: quirk '%s' selected %s flags=0x%x timer=%d debug=%d\n",
				quirkCasesTable[i].renderer,
				RendererTier_Name( selected ),
				report.flags,
				caps.hasTimerQuery ? 1 : 0,
				caps.debugContext ? 1 : 0 );
			ok = false;
		}
		quirkCases++;
	}

	rg_driverQuirkReport = restoreReport;
	if ( !ok ) {
		return false;
	}

	common->Printf(
		"RendererCompatibilityGates self-test passed (fallbacks=%d quirks=%d)\n",
		fallbackCases,
		quirkCases );
	return true;
}
